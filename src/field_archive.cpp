// field_archive.cpp - FF8 field archive reader implementation
//
// Two-level fi/fl/fs archive extraction for the Steam 2013 PC version.
// See field_archive.h for architecture overview.
//
// v05.47

#include <windows.h>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <vector>
#include <string>
#include "field_archive.h"
#include "ff8_accessibility.h"

namespace FieldArchive {

// ============================================================================
// FI entry: 12 bytes per file in the archive
// ============================================================================
struct FiEntry {
    uint32_t uncompSize;   // uncompressed size in bytes
    uint32_t offset;       // byte offset into the .fs file
    uint32_t compression;  // 0 = raw, non-zero = LZSS compressed
};

// ============================================================================
// Module state
// ============================================================================
static bool s_initialized = false;
static std::string s_gamePath;        // e.g. "C:\...\FINAL FANTASY VIII\"
static std::string s_archivePath;     // e.g. "C:\...\Data\lang-en\"

// Outer archive data (loaded once at init).
static std::vector<std::string> s_outerFL;   // filenames from field.fl
static std::vector<FiEntry>     s_outerFI;   // index from field.fi
static std::string              s_outerFSPath; // path to field.fs

// Field name lookup: fieldId → basename extracted from FL.
// fieldId = fl_index / 3 (each field has 3 entries: fi, fl, fs).
static std::vector<std::string> s_fieldNames;  // indexed by field ID

// ============================================================================
// LZSS decompression (standard FF8/FF7 variant)
// ============================================================================
static bool DecompressLZSS(const uint8_t* input, uint32_t inputSize,
                           uint8_t* output, uint32_t outputSize)
{
    // FF8 LZSS: 4096-byte ring buffer, start position 0xFEE.
    // Flag byte (8 bits, LSB first): 1=literal, 0=reference.
    // Reference: 2 bytes → 12-bit offset, 4-bit length (+3).
    uint8_t ring[4096];
    memset(ring, 0, sizeof(ring));
    int ringPos = 0xFEE;

    uint32_t inPos  = 0;
    uint32_t outPos = 0;

    while (outPos < outputSize && inPos < inputSize) {
        uint8_t flags = input[inPos++];
        for (int bit = 0; bit < 8 && outPos < outputSize; bit++) {
            if (flags & (1 << bit)) {
                // Literal byte.
                if (inPos >= inputSize) return false;
                uint8_t b = input[inPos++];
                output[outPos++] = b;
                ring[ringPos] = b;
                ringPos = (ringPos + 1) & 0xFFF;
            } else {
                // Back-reference.
                if (inPos + 1 >= inputSize) return false;
                uint8_t b1 = input[inPos++];
                uint8_t b2 = input[inPos++];
                int offset = b1 | ((b2 & 0xF0) << 4);
                int length = (b2 & 0x0F) + 3;
                for (int i = 0; i < length && outPos < outputSize; i++) {
                    uint8_t b = ring[(offset + i) & 0xFFF];
                    output[outPos++] = b;
                    ring[ringPos] = b;
                    ringPos = (ringPos + 1) & 0xFFF;
                }
            }
        }
    }
    return (outPos == outputSize);
}

// ============================================================================
// File I/O helpers
// ============================================================================

// Read an entire file into a buffer.  Returns false on failure.
static bool ReadFileToBuffer(const char* path, std::vector<uint8_t>& out)
{
    FILE* f = fopen(path, "rb");
    if (!f) return false;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    if (sz <= 0) { fclose(f); return false; }
    fseek(f, 0, SEEK_SET);
    out.resize((size_t)sz);
    size_t rd = fread(out.data(), 1, (size_t)sz, f);
    fclose(f);
    return (rd == (size_t)sz);
}

// Read a chunk of a file at a given offset and size.
static bool ReadFileChunk(const char* path, uint32_t offset, uint32_t size,
                          std::vector<uint8_t>& out)
{
    FILE* f = fopen(path, "rb");
    if (!f) return false;
    if (fseek(f, (long)offset, SEEK_SET) != 0) { fclose(f); return false; }
    out.resize(size);
    size_t rd = fread(out.data(), 1, size, f);
    fclose(f);
    return (rd == size);
}

// Extract basename (without extension) from an FL path.
// Input:  "C:\ff8\Data\eng\FIELD\mapdata\bc\bcroom_1.fi"
// Output: "bcroom_1"
static std::string ExtractBasename(const std::string& flPath)
{
    // Find last backslash or forward slash.
    size_t lastSep = flPath.find_last_of("\\/");
    std::string filename = (lastSep != std::string::npos)
                           ? flPath.substr(lastSep + 1)
                           : flPath;
    // Strip extension.
    size_t dot = filename.find_last_of('.');
    if (dot != std::string::npos)
        filename = filename.substr(0, dot);
    return filename;
}

// Case-insensitive string comparison.
static bool StrEqualNoCase(const char* a, const char* b)
{
    return _stricmp(a, b) == 0;
}

// ============================================================================
// Parse outer field.fl (newline-separated paths)
// ============================================================================
static bool ParseOuterFL(const std::string& path)
{
    std::vector<uint8_t> data;
    if (!ReadFileToBuffer(path.c_str(), data)) return false;

    s_outerFL.clear();
    std::string line;
    for (size_t i = 0; i < data.size(); i++) {
        char c = (char)data[i];
        if (c == '\n' || c == '\r') {
            if (!line.empty()) {
                s_outerFL.push_back(line);
                line.clear();
            }
        } else {
            line += c;
        }
    }
    if (!line.empty()) s_outerFL.push_back(line);
    return !s_outerFL.empty();
}

// ============================================================================
// Parse outer field.fi (12 bytes per entry)
// ============================================================================
static bool ParseOuterFI(const std::string& path)
{
    std::vector<uint8_t> data;
    if (!ReadFileToBuffer(path.c_str(), data)) return false;

    size_t count = data.size() / 12;
    s_outerFI.resize(count);
    for (size_t i = 0; i < count; i++) {
        const uint8_t* p = data.data() + i * 12;
        s_outerFI[i].uncompSize  = *(const uint32_t*)(p + 0);
        s_outerFI[i].offset      = *(const uint32_t*)(p + 4);
        s_outerFI[i].compression = *(const uint32_t*)(p + 8);
    }
    return !s_outerFI.empty();
}

// ============================================================================
// Build field ID → name mapping from FL
// ============================================================================
static void BuildFieldNameMap()
{
    s_fieldNames.clear();
    // Each field occupies 3 consecutive FL entries (.fi, .fl, .fs).
    // Field ID = entry_index / 3.
    for (size_t i = 0; i + 2 < s_outerFL.size(); i += 3) {
        std::string name = ExtractBasename(s_outerFL[i]);
        // Convert to lowercase for consistent matching.
        for (auto& ch : name) ch = (char)tolower((unsigned char)ch);
        s_fieldNames.push_back(name);
    }
    Log::Write("FieldArchive: Built field name map: %d fields", (int)s_fieldNames.size());
}

// ============================================================================
// Extract a single entry from the outer archive (field.fs)
// ============================================================================
static bool ExtractOuterEntry(size_t flIndex, std::vector<uint8_t>& out)
{
    if (flIndex >= s_outerFI.size()) return false;

    const FiEntry& fi = s_outerFI[flIndex];
    if (fi.uncompSize == 0) return false;

    // Read raw data from field.fs at the given offset.
    // The size to read from the FS depends on compression:
    // - If uncompressed, read uncompSize bytes at offset.
    // - If compressed, we need to read the compressed data.  The compressed
    //   size isn't stored in the FI — it's implied by the gap to the next entry
    //   or to EOF.  However, FF8's outer archive entries for fields are
    //   typically uncompressed.  For safety, compute compressed size from the
    //   gap to the next entry.
    if (fi.compression == 0) {
        return ReadFileChunk(s_outerFSPath.c_str(), fi.offset, fi.uncompSize, out);
    } else {
        // Compressed: compute compressed size as gap to next entry.
        uint32_t compSize = fi.uncompSize; // fallback: assume same size
        // Look ahead for the next entry with a higher offset.
        for (size_t j = flIndex + 1; j < s_outerFI.size(); j++) {
            if (s_outerFI[j].offset > fi.offset) {
                compSize = s_outerFI[j].offset - fi.offset;
                break;
            }
        }
        std::vector<uint8_t> compressed;
        if (!ReadFileChunk(s_outerFSPath.c_str(), fi.offset, compSize, compressed))
            return false;

        // FF8 compressed format: 4-byte header (uncompressed size, LE),
        // then LZSS data starts at offset 4.
        uint32_t hdrSkip = 4;
        if (compressed.size() <= hdrSkip) return false;
        out.resize(fi.uncompSize);
        return DecompressLZSS(compressed.data() + hdrSkip,
                              (uint32_t)(compressed.size() - hdrSkip),
                              out.data(), fi.uncompSize);
    }
}

// ============================================================================
// Find the outer FL index for a given field name + extension.
// fieldName: e.g. "bcroom_1"
// extension: e.g. ".fi" or ".fl" or ".fs"
// Returns the index into s_outerFL, or (size_t)-1 if not found.
// ============================================================================
static size_t FindOuterIndex(const char* fieldName, const char* extension)
{
    // Build the target suffix: "fieldname.ext" (case-insensitive).
    // FL paths may use \ or / as separator, so we check the character
    // immediately before the match is a path separator.
    char target[128];
    snprintf(target, sizeof(target), "%s%s", fieldName, extension);
    int tlen = (int)strlen(target);

    for (size_t i = 0; i < s_outerFL.size(); i++) {
        const std::string& fl = s_outerFL[i];
        int flen = (int)fl.size();
        if (flen >= tlen + 1) {  // +1 for the preceding separator
            const char* suffix = fl.c_str() + flen - tlen;
            char sep = *(suffix - 1);
            if ((sep == '\\' || sep == '/') &&
                _stricmp(suffix, target) == 0)
                return i;
        } else if (flen == tlen) {
            // Exact match (no path prefix).
            if (_stricmp(fl.c_str(), target) == 0)
                return i;
        }
    }
    return (size_t)-1;
}

// ============================================================================
// Extract a file from the inner (per-field) archive.
// fieldName: e.g. "bcroom_1"
// innerExt:  e.g. ".sym" or ".inf"
// out: receives the extracted file data
// ============================================================================
static bool ExtractInnerFile(const char* fieldName, const char* innerExt,
                             std::vector<uint8_t>& out)
{
    // Step 1: Find the 3 outer entries for this field.
    size_t outerFiIdx = FindOuterIndex(fieldName, ".fi");
    size_t outerFlIdx = FindOuterIndex(fieldName, ".fl");
    size_t outerFsIdx = FindOuterIndex(fieldName, ".fs");
    Log::Write("FieldArchive: [extract] '%s%s' outer indices: fi=%d fl=%d fs=%d",
               fieldName, innerExt, (int)outerFiIdx, (int)outerFlIdx, (int)outerFsIdx);
    if (outerFiIdx == (size_t)-1 || outerFlIdx == (size_t)-1 || outerFsIdx == (size_t)-1) {
        Log::Write("FieldArchive: Field '%s' not found in outer archive", fieldName);
        return false;
    }
    // (Per-entry FI logging removed in v05.48 — archive confirmed working.)

    // Step 2: Extract the inner fi, fl, fs from the outer archive.
    std::vector<uint8_t> innerFiData, innerFlData, innerFsData;
    if (!ExtractOuterEntry(outerFiIdx, innerFiData)) {
        Log::Write("FieldArchive: Failed to extract inner .fi for '%s'", fieldName);
        return false;
    }
    if (!ExtractOuterEntry(outerFlIdx, innerFlData)) {
        Log::Write("FieldArchive: Failed to extract inner .fl for '%s'", fieldName);
        return false;
    }
    if (!ExtractOuterEntry(outerFsIdx, innerFsData)) {
        Log::Write("FieldArchive: Failed to extract inner .fs for '%s'", fieldName);
        return false;
    }
    Log::Write("FieldArchive: [extract] inner data: fi=%d bytes, fl=%d bytes, fs=%d bytes",
               (int)innerFiData.size(), (int)innerFlData.size(), (int)innerFsData.size());

    // (Inner FL hex dump removed in v05.48 — archive format confirmed.)

    // Step 3: Parse inner FL.
    // The inner FL may use null terminators (not newlines) as separators.
    // We accept both '\n', '\r', and '\0' as delimiters.
    std::vector<std::string> innerFL;
    {
        std::string line;
        for (size_t i = 0; i < innerFlData.size(); i++) {
            char c = (char)innerFlData[i];
            if (c == '\n' || c == '\r' || c == '\0') {
                if (!line.empty()) { innerFL.push_back(line); line.clear(); }
            } else {
                line += c;
            }
        }
        if (!line.empty()) innerFL.push_back(line);
    }

    // Step 4: Parse inner FI (12 bytes per entry).
    size_t innerFiCount = innerFiData.size() / 12;
    std::vector<FiEntry> innerFI(innerFiCount);
    for (size_t i = 0; i < innerFiCount; i++) {
        const uint8_t* p = innerFiData.data() + i * 12;
        innerFI[i].uncompSize  = *(const uint32_t*)(p + 0);
        innerFI[i].offset      = *(const uint32_t*)(p + 4);
        innerFI[i].compression = *(const uint32_t*)(p + 8);
    }

    // Step 5: Find the target file in the inner FL.
    // Inner FL paths look like: "C:\ff8\...\bcroom_1.sym"
    // We match by extension suffix.
    char targetSuffix[32];
    snprintf(targetSuffix, sizeof(targetSuffix), "%s%s", fieldName, innerExt);
    int tgtLen = (int)strlen(targetSuffix);

    // (Inner FL listing removed in v05.48.)

    size_t targetIdx = (size_t)-1;
    for (size_t i = 0; i < innerFL.size(); i++) {
        const std::string& fn = innerFL[i];
        int flen = (int)fn.size();
        if (flen >= tgtLen) {
            if (_stricmp(fn.c_str() + flen - tgtLen, targetSuffix) == 0) {
                targetIdx = i;
                break;
            }
        }
    }

    Log::Write("FieldArchive: [extract] searching for '%s' -> targetIdx=%d",
               targetSuffix, (int)targetIdx);
    if (targetIdx == (size_t)-1 || targetIdx >= innerFiCount) {
        Log::Write("FieldArchive: [extract] target '%s' not found in inner FL", targetSuffix);
        return false;
    }

    // Step 6: Extract from inner FS.
    const FiEntry& entry = innerFI[targetIdx];
    if (entry.uncompSize == 0) return false;
    if (entry.offset + entry.uncompSize > innerFsData.size() && entry.compression == 0) {
        // Bounds check for uncompressed.
        // For compressed, the compressed data may be smaller.
        if (entry.offset >= innerFsData.size()) return false;
    }

    if (entry.compression == 0) {
        // Uncompressed: direct copy from inner FS.
        uint32_t avail = (uint32_t)(innerFsData.size() - entry.offset);
        uint32_t copyLen = (entry.uncompSize < avail) ? entry.uncompSize : avail;
        out.assign(innerFsData.begin() + entry.offset,
                   innerFsData.begin() + entry.offset + copyLen);
    } else {
        // Compressed: compute compressed size from gap to next entry.
        uint32_t compSize = (uint32_t)(innerFsData.size() - entry.offset); // fallback: rest of FS
        for (size_t j = targetIdx + 1; j < innerFiCount; j++) {
            if (innerFI[j].offset > entry.offset) {
                compSize = innerFI[j].offset - entry.offset;
                break;
            }
        }
        if (entry.offset + compSize > innerFsData.size())
            compSize = (uint32_t)(innerFsData.size() - entry.offset);

        // FF8 compressed format: skip 4-byte uncompressed-size header.
        uint32_t hdrSkip = 4;
        if (compSize <= hdrSkip) {
            Log::Write("FieldArchive: [extract] inner compressed entry too small");
            out.clear();
            return false;
        }
        out.resize(entry.uncompSize);
        if (!DecompressLZSS(innerFsData.data() + entry.offset + hdrSkip,
                            compSize - hdrSkip,
                            out.data(), entry.uncompSize)) {
            Log::Write("FieldArchive: LZSS decompression failed for '%s%s'",
                       fieldName, innerExt);
            out.clear();
            return false;
        }
    }

    return !out.empty();
}

// ============================================================================
// Public API
// ============================================================================

bool Initialize()
{
    if (s_initialized) return true;

    // Auto-detect game path from EXE location.
    // dinput8.dll sits in the same directory as FF8_EN.exe.
    char dllPath[MAX_PATH] = {};
    GetModuleFileNameA(nullptr, dllPath, MAX_PATH);

    // Strip filename to get directory.
    char* lastSlash = strrchr(dllPath, '\\');
    if (lastSlash) *(lastSlash + 1) = '\0';

    s_gamePath = dllPath;
    s_archivePath = s_gamePath + "Data\\lang-en\\";
    s_outerFSPath = s_archivePath + "field.fs";

    Log::Write("FieldArchive: Game path: %s", s_gamePath.c_str());
    Log::Write("FieldArchive: Archive path: %s", s_archivePath.c_str());

    // Parse outer FL.
    std::string flPath = s_archivePath + "field.fl";
    if (!ParseOuterFL(flPath)) {
        Log::Write("FieldArchive: FAILED to parse %s", flPath.c_str());
        return false;
    }
    Log::Write("FieldArchive: Outer FL: %d entries", (int)s_outerFL.size());

    // Parse outer FI.
    std::string fiPath = s_archivePath + "field.fi";
    if (!ParseOuterFI(fiPath)) {
        Log::Write("FieldArchive: FAILED to parse %s", fiPath.c_str());
        return false;
    }
    Log::Write("FieldArchive: Outer FI: %d entries", (int)s_outerFI.size());

    // Verify consistency.
    if (s_outerFL.size() != s_outerFI.size()) {
        Log::Write("FieldArchive: WARNING - FL/FI count mismatch: %d vs %d",
                   (int)s_outerFL.size(), (int)s_outerFI.size());
    }

    // Build field name → ID mapping.
    BuildFieldNameMap();

    s_initialized = true;
    Log::Write("FieldArchive: Initialized OK.");
    return true;
}

bool LoadSYMNames(const char* fieldName, char names[][32], int maxNames, int& outCount)
{
    outCount = 0;
    if (!s_initialized) return false;

    std::vector<uint8_t> symData;
    if (!ExtractInnerFile(fieldName, ".sym", symData)) {
        // Not all fields have SYM files — this is normal.
        return false;
    }

    Log::Write("FieldArchive: SYM for '%s': %d bytes", fieldName, (int)symData.size());
    // (SYM hex dump removed in v05.48 — format confirmed: 32-byte fixed-width records.)

    // Parse SYM: 32-byte fixed-width records, no newline separators.
    // Each record is exactly 32 bytes. The name is null-terminated within
    // the record, with remaining bytes as nulls or spaces.
    // Entity names come first (no "::" in the name).
    // Once we hit a record containing "::", that's a function name — stop.
    size_t pos = 0;
    while (pos + 32 <= symData.size() && outCount < maxNames) {
        // Extract 32-byte record.
        char record[33] = {};
        memcpy(record, symData.data() + pos, 32);
        record[32] = '\0';
        pos += 32;

        // Trim trailing spaces, nulls, and newlines.
        int end = 31;
        while (end >= 0 && (record[end] == ' ' || record[end] == '\0' ||
                           record[end] == '\n' || record[end] == '\r')) end--;
        record[end + 1] = '\0';

        // Skip empty records.
        if (record[0] == '\0') continue;

        // Stop at function names (contain "::").
        if (strstr(record, "::") != nullptr) break;

        // Store the entity name.
        strncpy(names[outCount], record, 31);
        names[outCount][31] = '\0';
        outCount++;
    }

    Log::Write("FieldArchive: SYM parsed: %d entity names for '%s'", outCount, fieldName);

    return (outCount > 0);
}

bool LoadINFGateways(const char* fieldName, GatewayInfo* gateways, int maxGateways, int& outCount)
{
    outCount = 0;
    if (!s_initialized) return false;

    std::vector<uint8_t> infData;
    if (!ExtractInnerFile(fieldName, ".inf", infData)) {
        Log::Write("FieldArchive: No INF for '%s'", fieldName);
        return false;
    }

    Log::Write("FieldArchive: INF for '%s': %d bytes", fieldName, (int)infData.size());

    // INF format (PC version, 672 bytes):
    //   0x00: Camera ranges (16 bytes)
    //   0x10: Screen ranges (16 bytes)
    //   0x20: 12 Gateways (each 24 bytes = 288 bytes)
    //   0x140: 16 Triggers (each 24 bytes = 384 bytes)
    //   0x2A0: End (672 bytes total)
    if (infData.size() < 672) {
        Log::Write("FieldArchive: INF too small (%d bytes, expected 672)", (int)infData.size());
        return false;
    }

    const uint8_t* base = infData.data();
    int maxGw = (maxGateways < 12) ? maxGateways : 12;

    for (int i = 0; i < maxGw; i++) {
        const uint8_t* gw = base + 0x20 + i * 24;
        int16_t  x1 = *(const int16_t*)(gw + 0);
        int16_t  y1 = *(const int16_t*)(gw + 2);
        int16_t  z1 = *(const int16_t*)(gw + 4);
        int16_t  x2 = *(const int16_t*)(gw + 6);
        int16_t  y2 = *(const int16_t*)(gw + 8);
        int16_t  z2 = *(const int16_t*)(gw + 10);
        uint16_t destId = *(const uint16_t*)(gw + 12);

        // Skip unused gateways (dest = 0xFFFF or 0x7FFF).
        if (destId == 0xFFFF || destId == 0x7FFF) continue;

        // Also skip gateways where both vertices are (0,0,0) — definitely unused.
        if (x1 == 0 && z1 == 0 && x2 == 0 && z2 == 0) continue;

        GatewayInfo& info = gateways[outCount];
        info.centerX = (float)(x1 + x2) / 2.0f;
        info.centerZ = (float)(z1 + z2) / 2.0f;
        info.destFieldId = destId;

        // Look up destination field name.
        const char* destName = GetFieldNameById(destId);
        if (destName) {
            strncpy(info.destFieldName, destName, 63);
            info.destFieldName[63] = '\0';
        } else {
            snprintf(info.destFieldName, 64, "field_%u", (unsigned)destId);
        }

        outCount++;
    }

    Log::Write("FieldArchive: INF parsed: %d active gateways for '%s'", outCount, fieldName);
    return (outCount > 0);
}

bool LoadINFTriggers(const char* fieldName, TriggerInfo* triggers, int maxTriggers, int& outCount)
{
    outCount = 0;
    if (!s_initialized) return false;

    std::vector<uint8_t> infData;
    if (!ExtractInnerFile(fieldName, ".inf", infData)) {
        return false;
    }

    // INF must be at least 676 bytes (0x2A4).
    if (infData.size() < 676) return false;

    const uint8_t* base = infData.data();
    int maxTrig = (maxTriggers < 16) ? maxTriggers : 16;

    // v05.54 INFDUMP removed in v05.58 — INF trigger section confirmed bogus on PC.
    // Real trigger data comes from SETLINE opcode hooks at runtime.

    // Triggers start at offset 0x140, each 24 bytes.
    // Format TBD — parsing below uses vertex-pair hypothesis.
    // The hex dump above will reveal the actual format.
    for (int i = 0; i < maxTrig; i++) {
        const uint8_t* trig = base + 0x140 + i * 24;
        int16_t x1 = *(const int16_t*)(trig + 0);
        int16_t y1 = *(const int16_t*)(trig + 2);
        int16_t z1 = *(const int16_t*)(trig + 4);
        int16_t x2 = *(const int16_t*)(trig + 6);
        int16_t y2 = *(const int16_t*)(trig + 8);
        int16_t z2 = *(const int16_t*)(trig + 10);

        // Skip empty triggers (both vertices at origin or sentinel values).
        if (x1 == 0 && z1 == 0 && x2 == 0 && z2 == 0) continue;

        TriggerInfo& info = triggers[outCount];
        info.centerX = (float)(x1 + x2) / 2.0f;
        info.centerZ = (float)(z1 + z2) / 2.0f;
        info.x1 = x1;
        info.z1 = z1;
        info.x2 = x2;
        info.z2 = z2;
        info.triggerIdx = i;
        outCount++;
    }

    Log::Write("FieldArchive: INF parsed: %d active triggers for '%s'", outCount, fieldName);
    return (outCount > 0);
}

const char* GetFieldNameById(uint16_t fieldId)
{
    if (fieldId < s_fieldNames.size())
        return s_fieldNames[fieldId].c_str();
    return nullptr;
}

bool LoadJSMCounts(const char* fieldName, JSMCounts& counts)
{
    counts = {};
    if (!s_initialized) return false;

    std::vector<uint8_t> jsmData;
    if (!ExtractInnerFile(fieldName, ".jsm", jsmData)) {
        Log::Write("FieldArchive: No JSM for '%s'", fieldName);
        return false;
    }

    Log::Write("FieldArchive: JSM for '%s': %d bytes", fieldName, (int)jsmData.size());

    // JSM hex dump removed in v05.58 — format confirmed.

    // JSM header format (from Qhimm wiki / OpenVIII research):
    // The JSM file starts with an 8-byte header:
    //   Byte 0: count of "other" entity scripts (NPCs/characters)
    //   Byte 1: count of door entity scripts  
    //   Byte 2: count of line entity scripts (walk-on triggers)
    //   Byte 3: count of background entity scripts
    //   Bytes 4-7: offset to script data section (uint32 LE)
    //
    // Entity ordering in the entry point table is:
    //   Doors first, then Lines, then Backgrounds, then Others.
    //
    // NOTE: This byte assignment is tentative. The hex dump above will let us
    // verify empirically by comparing counts against known fields (e.g. bghoke_2
    // which has 15 SYM names and 10 entity states). If the counts look wrong,
    // we'll swap byte assignments in the next build.
    if (jsmData.size() < 8) {
        Log::Write("FieldArchive: JSM too small for header");
        return false;
    }

    // Read raw header bytes.
    // JSM header: byte 0 = reserved (always 0),
    //             byte 1 = doors, byte 2 = lines, byte 3 = backgrounds.
    // "Others" count is NOT stored — derived from entry point table.
    counts.doors        = jsmData[1];
    counts.lines        = jsmData[2];
    counts.backgrounds  = jsmData[3];

    // Bytes 4-5: offset to script entry points section (uint16 LE).
    // Entity entry points run from byte 8 to this offset.
    uint16_t scriptEntryOffset = *(const uint16_t*)(jsmData.data() + 4);
    int totalEntityEntries = (scriptEntryOffset - 8) / 2;
    counts.others = totalEntityEntries - counts.doors - counts.lines - counts.backgrounds;
    if (counts.others < 0) counts.others = 0;

    // Now parse entity entry points to find which non-door entities use the
    // "other" struct (no flag bit set). SYM names correspond to non-door
    // entities in order. We need to find which SYM indices map to "other"
    // entities (the ones in pFieldStateOthers).
    //
    // Entry point format: 2 bytes each.
    //   For entity entries: scriptCount = entry & 0x7F, label = entry >> 7
    //   The flag bit (bit 7+ in the label, i.e. entry & 0x80) is set for
    //   door, line, and background entities but NOT for "other" entities.
    //
    // Entity order in entry point table: doors first, then lines, then
    // backgrounds, then others. SYM lists the same entities minus doors.
    // So SYM[0] = first line entity, SYM[lines-1] = last line entity,
    // SYM[lines] = first background entity, etc.

    Log::Write("FieldArchive: JSM header: doors=%d lines=%d bg=%d, scriptEntryOff=%d, totalEntities=%d, others=%d",
               counts.doors, counts.lines, counts.backgrounds,
               (int)scriptEntryOffset, totalEntityEntries, counts.others);
    Log::Write("FieldArchive: JSM raw bytes: b0=%d b1=%d b2=%d b3=%d",
               (int)jsmData[0], (int)jsmData[1], (int)jsmData[2], (int)jsmData[3]);

    return true;
}

// ============================================================================
// Walkmesh (ID file) loader for A* pathfinding
// ============================================================================
//
// FF8 PC walkmesh format (.id inner file):
//   The ID file contains the walk mesh for a field. Format:
//
//   Section 1 — Triangles:
//     uint32_t numTriangles
//     For each triangle (24 bytes each):
//       int16_t v0idx, v1idx, v2idx      — vertex indices
//       int16_t n0, n1, n2               — neighbor triangle on each edge
//                                           (0x7FFF or 0xFFFF = no neighbor)
//       uint16_t walkFlags               — collision/access flags
//       padding (variable — see below)
//
//   Section 2 — Vertices (after triangles):
//     uint32_t numVertices
//     For each vertex (8 bytes each):
//       int16_t x, y, z, padding(2 bytes)
//
// NOTE: The exact per-triangle record size may vary. We auto-detect
// by checking if the vertex count at expected offsets is reasonable.
// Common sizes: 16 bytes/tri (compact) or 24 bytes/tri.

bool LoadWalkmesh(const char* fieldName, WalkmeshData& outMesh)
{
    outMesh = {};
    if (!s_initialized) return false;

    std::vector<uint8_t> idData;
    if (!ExtractInnerFile(fieldName, ".id", idData)) {
        Log::Write("FieldArchive: No ID (walkmesh) for '%s'", fieldName);
        return false;
    }

    Log::Write("FieldArchive: ID for '%s': %d bytes", fieldName, (int)idData.size());
    if (idData.size() < 4) return false;

    const uint8_t* raw = idData.data();
    size_t sz = idData.size();

    // FF8 PC walkmesh format (confirmed via hex analysis of start0.id):
    // Same as FF7/PSX inline format:
    //   uint32 numTriangles
    //   triangle[0..N-1] — each 24 bytes: 3 vertices × (int16 X, Y, Z, pad)
    //   access[0..N-1]   — each 6 bytes: 3 × int16 neighbor index
    //
    // Total = 4 + N*24 + N*6 = 4 + N*30
    // Vertices are INLINED per-triangle (not indexed). "pad" = v[0].z repeated.
    // Neighbor -1 (0xFFFF) = edge not crossable.

    uint32_t numTri = *(const uint32_t*)(raw + 0);
    Log::Write("FieldArchive: ID header: numTri=%u", numTri);

    if (numTri == 0 || numTri > 4096) {
        Log::Write("FieldArchive: ID walkmesh bad tri count: %u", numTri);
        return false;
    }

    // Verify file size: 4 + numTri*24 (vertices) + numTri*6 (access) = 4 + numTri*30
    uint32_t expectedSize = 4 + numTri * 30;
    if (sz < expectedSize) {
        Log::Write("FieldArchive: ID walkmesh truncated: expected %u bytes, have %u",
                   expectedSize, (uint32_t)sz);
        return false;
    }

    // Section offsets.
    uint32_t vertSectionStart   = 4;
    uint32_t accessSectionStart = 4 + numTri * 24;

    // Allocate. FF8 inline format: vertices are stored per-triangle (not shared).
    // We build a deduplicated vertex array so that FindPortal() and
    // GetSharedEdgeLength() can look up shared edge vertices by index.
    // v05.94: Build vertex array + vertexIdx to fix funnel algorithm.
    outMesh.numTriangles = (int)numTri;
    outMesh.triangles    = new WalkmeshTriangle[numTri];
    memset(outMesh.triangles, 0, sizeof(WalkmeshTriangle) * numTri);

    // First pass: extract all inline vertices and deduplicate.
    // Max vertices = numTri * 3, but many are shared between adjacent triangles.
    int maxVerts = (int)(numTri * 3);
    outMesh.vertices = new WalkmeshVertex[maxVerts];
    outMesh.numVertices = 0;

    for (uint32_t t = 0; t < numTri; t++) {
        const int16_t* verts = (const int16_t*)(raw + vertSectionStart + t * 24);
        float sumX = 0, sumY = 0;
        for (int v = 0; v < 3; v++) {
            int16_t vx = verts[v * 4 + 0]; // X
            int16_t vy = verts[v * 4 + 1]; // Y
            int16_t vz = verts[v * 4 + 2]; // Z
            sumX += vx;
            sumY += vy;
            // Deduplicate: find existing vertex with same coords.
            int foundIdx = -1;
            for (int i = 0; i < outMesh.numVertices; i++) {
                if (outMesh.vertices[i].x == vx &&
                    outMesh.vertices[i].y == vy &&
                    outMesh.vertices[i].z == vz) {
                    foundIdx = i;
                    break;
                }
            }
            if (foundIdx >= 0) {
                outMesh.triangles[t].vertexIdx[v] = (uint16_t)foundIdx;
            } else {
                int idx = outMesh.numVertices++;
                outMesh.vertices[idx].x = vx;
                outMesh.vertices[idx].y = vy;
                outMesh.vertices[idx].z = vz;
                outMesh.triangles[t].vertexIdx[v] = (uint16_t)idx;
            }
        }
        outMesh.triangles[t].centerX = sumX / 3.0f;
        outMesh.triangles[t].centerY = sumY / 3.0f;

        // Access section: 6 bytes per triangle (3 × int16 neighbor).
        const int16_t* access = (const int16_t*)(raw + accessSectionStart + t * 6);
        for (int e = 0; e < 3; e++) {
            int16_t nb = access[e];
            outMesh.triangles[t].neighbor[e] = (nb == -1) ? 0xFFFF : (uint16_t)nb;
        }
    }

    Log::Write("FieldArchive: ID vertex dedup: %d triangles -> %d unique vertices (from %d inline)",
               (int)numTri, outMesh.numVertices, (int)(numTri * 3));

    // Diagnostic: dump first 3 triangles.
    for (uint32_t t = 0; t < numTri && t < 3; t++) {
        const int16_t* verts = (const int16_t*)(raw + vertSectionStart + t * 24);
        const WalkmeshTriangle& tri = outMesh.triangles[t];
        Log::Write("FieldArchive: ID tri[%u] v0=(%d,%d,%d) v1=(%d,%d,%d) v2=(%d,%d,%d) "
                   "neighbors=(%u,%u,%u) center=(%.0f,%.0f)",
                   t,
                   (int)verts[0], (int)verts[1], (int)verts[2],
                   (int)verts[4], (int)verts[5], (int)verts[6],
                   (int)verts[8], (int)verts[9], (int)verts[10],
                   (unsigned)tri.neighbor[0], (unsigned)tri.neighbor[1],
                   (unsigned)tri.neighbor[2],
                   tri.centerX, tri.centerY);
    }

    // Validate: neighbor indices should be < numTri or 0xFFFF.
    int badNeighbors = 0;
    for (uint32_t t = 0; t < numTri; t++) {
        for (int e = 0; e < 3; e++) {
            uint16_t nb = outMesh.triangles[t].neighbor[e];
            if (nb != 0xFFFF && nb >= numTri) badNeighbors++;
        }
    }
    if (badNeighbors > 0) {
        Log::Write("FieldArchive: ID WARNING: %d out-of-range neighbor indices!", badNeighbors);
    }

    outMesh.valid = true;
    Log::Write("FieldArchive: Walkmesh loaded: %d triangles for '%s' (inline vertex format)",
               outMesh.numTriangles, fieldName);
    return true;
}

void FreeWalkmesh(WalkmeshData& mesh)
{
    if (mesh.triangles) { delete[] mesh.triangles; mesh.triangles = nullptr; }
    if (mesh.vertices)  { delete[] mesh.vertices;  mesh.vertices = nullptr; }
    mesh.numTriangles = 0;
    mesh.numVertices  = 0;
    mesh.valid = false;
}

void Shutdown()
{
    s_outerFL.clear();
    s_outerFI.clear();
    s_fieldNames.clear();
    s_initialized = false;
    Log::Write("FieldArchive: Shutdown.");
}

bool IsReady()
{
    return s_initialized;
}

}  // namespace FieldArchive
