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

    // v0.07.93: Corrected INF format from Deling (myst6re's InfFile.h).
    // INF PC format = 676 bytes:
    //   0x00: name[9] + control(1) + unknown[6] + pvp(2) + cameraFocusHeight(2) = 20 bytes
    //   0x14: cameraRange[8] (8 x 8 = 64 bytes)
    //   0x54: screenRange[2] (2 x 8 = 16 bytes)
    //   0x64: gateways[12] (12 x 32 = 384 bytes)
    //   0x1E4: triggers[12] (12 x 16 = 192 bytes)
    //   Total: 676 bytes
    //
    // Gateway struct (32 bytes):
    //   +0:  exitLine[0] (int16 x,y,z = 6 bytes)
    //   +6:  exitLine[1] (int16 x,y,z = 6 bytes)
    //   +12: destinationPoint (int16 x,y,z = 6 bytes)
    //   +18: fieldId (uint16)
    //   +20: unknown1[4] (4 x uint16 = 8 bytes)
    //   +28: unknown2 (uint32 = 4 bytes)
    if (infData.size() < 676) {
        Log::Write("FieldArchive: INF too small (%d bytes, expected 676)", (int)infData.size());
        return false;
    }

    const uint8_t* base = infData.data();
    int maxGw = (maxGateways < 12) ? maxGateways : 12;

    for (int i = 0; i < maxGw; i++) {
        const uint8_t* gw = base + 0x64 + i * 32;
        // Exit line: two 3D vertices (trigger line the player crosses)
        int16_t  x1 = *(const int16_t*)(gw + 0);
        int16_t  y1 = *(const int16_t*)(gw + 2);
        // int16_t  z1 = *(const int16_t*)(gw + 4);  // Z = height, not used for 2D nav
        int16_t  x2 = *(const int16_t*)(gw + 6);
        int16_t  y2 = *(const int16_t*)(gw + 8);
        // int16_t  z2 = *(const int16_t*)(gw + 10);
        // Destination field ID at offset +18 within the 32-byte gateway.
        uint16_t destId = *(const uint16_t*)(gw + 18);

        // Skip unused gateways (dest = 0xFFFF or 0x7FFF).
        if (destId == 0xFFFF || destId == 0x7FFF) continue;

        // Also skip gateways where both exit line vertices are (0,0,0).
        if (x1 == 0 && y1 == 0 && x2 == 0 && y2 == 0) continue;

        GatewayInfo& info = gateways[outCount];
        // Center of exit line = navigation target position for compass.
        // X = screen-horizontal, Y = screen-vertical (matching entity coords).
        info.centerX = (float)(x1 + x2) / 2.0f;
        info.centerZ = (float)(y1 + y2) / 2.0f;  // "centerZ" = centerY in our coord system
        info.destFieldId = destId;

        // Look up destination field name.
        const char* destName = GetFieldNameById(destId);
        if (destName) {
            strncpy(info.destFieldName, destName, 63);
            info.destFieldName[63] = '\0';
        } else {
            snprintf(info.destFieldName, 64, "field_%u", (unsigned)destId);
        }

        Log::Write("FieldArchive: [INF-GW] gw[%d] line=(%d,%d)->(%d,%d) center=(%.0f,%.0f) "
                   "destId=%u dest='%s'",
                   i, (int)x1, (int)y1, (int)x2, (int)y2,
                   info.centerX, info.centerZ,
                   (unsigned)destId, info.destFieldName);
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

    // v0.07.93: Corrected INF format from Deling.
    // Triggers at offset 0x1E4, 12 entries, 16 bytes each.
    // Trigger struct (16 bytes):
    //   +0:  trigger_line[0] (int16 x,y,z = 6 bytes)
    //   +6:  trigger_line[1] (int16 x,y,z = 6 bytes)
    //   +12: doorID (uint8)
    //   +13: _blank[3] (3 bytes padding)
    if (infData.size() < 676) return false;

    const uint8_t* base = infData.data();
    int maxTrig = (maxTriggers < 12) ? maxTriggers : 12;

    for (int i = 0; i < maxTrig; i++) {
        const uint8_t* trig = base + 0x1E4 + i * 16;
        int16_t x1 = *(const int16_t*)(trig + 0);
        int16_t y1 = *(const int16_t*)(trig + 2);
        // int16_t z1 = *(const int16_t*)(trig + 4); // height, not used for 2D nav
        int16_t x2 = *(const int16_t*)(trig + 6);
        int16_t y2 = *(const int16_t*)(trig + 8);
        // int16_t z2 = *(const int16_t*)(trig + 10);

        // Skip empty triggers (both vertices at origin).
        if (x1 == 0 && y1 == 0 && x2 == 0 && y2 == 0) continue;

        TriggerInfo& info = triggers[outCount];
        info.centerX = (float)(x1 + x2) / 2.0f;
        info.centerZ = (float)(y1 + y2) / 2.0f;  // "centerZ" = centerY
        info.x1 = x1;
        info.z1 = y1;  // stored as z1 for legacy compat but is actually Y
        info.x2 = x2;
        info.z2 = y2;
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
    // JSM header (confirmed by ScanJSMScripts and deling JsmFile.cpp):
    //   byte 0 = doors, byte 1 = lines, byte 2 = backgrounds, byte 3 = others
    // NOTE: Previous code used b1/b2/b3 which was WRONG. Corrected in v0.07.73.
    counts.doors        = jsmData[0];
    counts.lines        = jsmData[1];
    counts.backgrounds  = jsmData[2];

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

// v0.07.64: Look up field ID by internal name (e.g. "bghall_1" → 165).
// Linear search through the FL-derived field name list.
int GetFieldIdByInternalName(const char* internalName)
{
    if (!internalName || !s_initialized) return -1;
    // s_fieldNames stores lowercase basenames indexed by field ID.
    // Do case-insensitive compare.
    for (size_t i = 0; i < s_fieldNames.size(); i++) {
        if (_stricmp(internalName, s_fieldNames[i].c_str()) == 0)
            return (int)i;
    }
    return -1;
}

// ============================================================================
// JSM script scanner — entity classification by opcode signatures
// ============================================================================
//
// JSM bytecode format (32-bit fixed-width stack machine):
//   Each instruction is 4 bytes (uint32).
//   Bit 31 = 0: PSHN_L (push literal), value = bits 0-30.
//   Bit 31 = 1: opcode, opcode_id = bits 16-30, inline_param = bits 0-15.
//
// JSM file layout:
//   Bytes 0-3:  entity count bytes (byte0, byte1=doors, byte2=lines, byte3=backgrounds)
//   Bytes 4-5:  offsetScriptEntryPoints (uint16, byte offset from file start)
//   Bytes 6-7:  offsetScriptData (uint16, byte offset from file start)
//   Bytes 8 to offsetScriptEntryPoints-1: entity group table (2 bytes per entity)
//     Each entry: bit15 = class flag (set for Door/Line/Bg), bits 0-14 = method count
//   offsetScriptEntryPoints to offsetScriptData-1: script entry point table
//     Each entry: uint16 = dword index into script data section
//   offsetScriptData to EOF: script instructions (4 bytes each)
//
// Entity ordering in the table: Door → Line → Background → Other.

// JSM opcode IDs (from FF8 scripting reference).
static const uint16_t JSM_OP_SET      = 0x01D;  // 2D position
static const uint16_t JSM_OP_SET3     = 0x01E;  // 3D position
static const uint16_t JSM_OP_SETLINE  = 0x039;  // trigger line geometry
static const uint16_t JSM_OP_SETMODEL = 0x02B;  // assign 3D model
static const uint16_t JSM_OP_TALKON   = 0x057;  // enable talk interaction
static const uint16_t JSM_OP_MAPJUMP  = 0x029;  // field transition
static const uint16_t JSM_OP_MAPJUMP3 = 0x02A;  // field transition (3D)
static const uint16_t JSM_OP_SETDRAWPOINT = 0x155;  // configure draw point
static const uint16_t JSM_OP_DRAWPOINT   = 0x137;  // open draw point menu
static const uint16_t JSM_OP_MENUSAVE    = 0x12E;  // open save menu
static const uint16_t JSM_OP_SAVEENABLE  = 0x12F;  // enable saving
static const uint16_t JSM_OP_MENUSHOP    = 0x11E;  // open shop
static const uint16_t JSM_OP_CARDGAME    = 0x13A;  // card game
static const uint16_t JSM_OP_LADDERUP    = 0x025;  // ladder up
static const uint16_t JSM_OP_LADDERDOWN  = 0x026;  // ladder down
static const uint16_t JSM_OP_DISCJUMP    = 0x038;  // disc change transition
static const uint16_t JSM_OP_MAPJUMPO    = 0x05C;  // map jump (other variant)
static const uint16_t JSM_OP_SHOW        = 0x060;  // make entity visible
static const uint16_t JSM_OP_HIDE        = 0x061;  // make entity invisible
static const uint16_t JSM_OP_UNUSE       = 0x01A;  // deactivate entity
static const uint16_t JSM_OP_USE         = 0x0E5;  // reactivate entity
static const uint16_t JSM_OP_RET         = 0x004;  // return from script
static const uint16_t JSM_OP_PARTICLEON  = 0x14E;  // particle effect on
static const uint16_t JSM_OP_PARTICLEOFF = 0x14F;  // particle effect off
static const uint16_t JSM_OP_ADDITEM     = 0x125;  // add item to inventory
static const uint16_t JSM_OP_WORLDMAPJUMP = 0x10D; // world map transition
static const uint16_t JSM_OP_PHSENABLE   = 0x130;  // enable PHS at save point
static const uint16_t JSM_OP_MENUPHS     = 0x11B;  // open PHS menu
static const uint16_t JSM_OP_DOORLINEON  = 0x143;  // door trigger line on
static const uint16_t JSM_OP_DOORLINEOFF = 0x142;  // door trigger line off

// v0.07.82: Camera/scroll opcodes for trigger line classification.
// All < 0x100 → detected directly as primary opcodes (high byte), no 0x1C dispatch.
static const uint16_t JSM_OP_BGDRAW        = 0x099;  // draw/show background layer
static const uint16_t JSM_OP_BGOFF         = 0x09A;  // hide background layer
static const uint16_t JSM_OP_BGANIME       = 0x095;  // start background animation
static const uint16_t JSM_OP_BGANIMESPEED  = 0x09B;  // set background anim speed
static const uint16_t JSM_OP_DSCROLL       = 0x071;  // direct scroll (instant)
static const uint16_t JSM_OP_LSCROLL       = 0x072;  // linear scroll (smooth)
static const uint16_t JSM_OP_CSCROLL       = 0x073;  // curved scroll
static const uint16_t JSM_OP_DSCROLLA      = 0x074;  // direct scroll variant A
static const uint16_t JSM_OP_LSCROLLA      = 0x075;  // linear scroll variant A
static const uint16_t JSM_OP_CSCROLLA      = 0x076;  // curved scroll variant A
static const uint16_t JSM_OP_SCROLLSYNC    = 0x077;  // wait for scroll
static const uint16_t JSM_OP_DSCROLLP      = 0x07F;  // direct scroll P
static const uint16_t JSM_OP_LSCROLLP      = 0x080;  // linear scroll P
static const uint16_t JSM_OP_CSCROLLP      = 0x081;  // curved scroll P
static const uint16_t JSM_OP_SETCAMERA     = 0x10A;  // set camera position (>0xFF, via 0x1C)
static const uint16_t JSM_OP_MES           = 0x047;  // display dialog
static const uint16_t JSM_OP_ASK           = 0x04A;  // display dialog with choices
static const uint16_t JSM_OP_AMES          = 0x065;  // auto-position message
static const uint16_t JSM_OP_AASK          = 0x06F;  // auto-position choices
static const uint16_t JSM_OP_BATTLE        = 0x069;  // trigger battle
static const uint16_t JSM_OP_MOVE          = 0x03E;  // move entity to position
static const uint16_t JSM_OP_REQ           = 0x014;  // invoke script on other entity
static const uint16_t JSM_OP_REQSW         = 0x015;  // invoke script (wait)
static const uint16_t JSM_OP_REQEW         = 0x016;  // invoke script (exec wait)

const char* JSMEntityTypeName(JSMEntityType t)
{
    switch (t) {
        case JSM_ENT_DRAW_POINT:        return "Draw Point";
        case JSM_ENT_SAVE_POINT:        return "Save Point";
        case JSM_ENT_SHOP:              return "Shop";
        case JSM_ENT_CARD_GAME:         return "Card Game";
        case JSM_ENT_LADDER:            return "Ladder";
        case JSM_ENT_MAP_EXIT:          return "Map Exit";
        case JSM_ENT_NPC:               return "NPC";
        case JSM_ENT_DOOR:              return "Door";
        case JSM_ENT_LINE_TRIGGER:      return "Line Trigger";
        case JSM_ENT_LINE_CAMERA_PAN:   return "Camera Pan";
        case JSM_ENT_LINE_SCREEN_BOUND: return "Screen Boundary";
        case JSM_ENT_LINE_EVENT:        return "Event Trigger";
        case JSM_ENT_BACKGROUND:        return "Background";
        default:                        return "Unknown";
    }
}

// Byte-swap a 32-bit value from big-endian to little-endian.
// FF8 JSM script instructions are stored big-endian (PS1 heritage).
static uint32_t SwapBE32(uint32_t v)
{
    return ((v & 0xFF) << 24) | ((v & 0xFF00) << 8) |
           ((v >> 8) & 0xFF00) | ((v >> 24) & 0xFF);
}

// Decode a single 32-bit JSM instruction (already byte-swapped to native).
// Returns true if it's an opcode (bit 31 set), false if PSHN_L.
//
// Encoding (confirmed from myst6re's deling JsmExpression.cpp):
//   Bit 31 = 1: opcode. ID = bits 1-14 (shift right 1, mask 0x3FFF). Bit 0 = sub flag.
//   Bit 31 = 0: PSHN_L (push literal). Value = bits 0-30, sign-extended from bit 30.
//   All opcode parameters come from the stack — there are NO inline parameters in the word.
static bool DecodeJSMInstruction(uint32_t word, uint16_t& opcodeOut, uint16_t& paramOut, int32_t& pushValueOut)
{
    if (word & 0x80000000) {
        // Opcode: bits 1-14 = opcode ID, bit 0 = sub-opcode flag
        opcodeOut  = (uint16_t)((word >> 1) & 0x3FFF);
        paramOut   = (uint16_t)(word & 1);  // sub flag only
        pushValueOut = 0;
        return true;
    } else {
        // PSHN_L: bits 0-30 = literal value (signed)
        opcodeOut = 0;
        paramOut  = 0;
        // Sign-extend from 31 bits to 32.
        pushValueOut = (int32_t)(word & 0x7FFFFFFF);
        if (word & 0x40000000) pushValueOut |= (int32_t)0x80000000;  // sign bit
        return false;
    }
}

bool ScanJSMScripts(const char* fieldName, JSMEntityInfo* outEntities, int maxEntities, int& outCount)
{
    outCount = 0;
    if (!s_initialized) return false;

    // Load the full JSM file.
    std::vector<uint8_t> jsmData;
    if (!ExtractInnerFile(fieldName, ".jsm", jsmData)) {
        return false;
    }
    if (jsmData.size() < 8) return false;

    // --- Parse header ---
    // Byte mapping (corrected per deling JsmFile.cpp and NEXT_SESSION_PROMPT research):
    //   byte 0 = countDoors, byte 1 = countLines, byte 2 = countBackgrounds, byte 3 = countOthers
    //   bytes 4-5 = byte offset of script entry point table (uint16 LE)
    //   bytes 6-7 = byte offset of script data section (uint16 LE)
    // NOTE: LoadJSMCounts still uses b1=doors — to be fixed after this is validated.
    int countDoors  = jsmData[0];
    int countLines  = jsmData[1];
    int countBg     = jsmData[2];
    int countOthersH = jsmData[3];  // header's Others count (for logging)

    // Bytes 4-5: byte offset of first entity group entry (uint16 LE).
    // Bytes 6-7: byte offset of script data section (uint16 LE).
    // Entity group table runs from byte 8 to posFirst-1 (but posFirst may equal 8 if no groups).
    // Script entry point table runs from posFirst to posScripts-1.
    // Script data runs from posScripts to EOF.
    uint16_t posFirst   = *(const uint16_t*)(jsmData.data() + 4);  // byte offset of entry point table
    uint16_t posScripts = *(const uint16_t*)(jsmData.data() + 6);  // byte offset of script data

    // Entity group table: bytes 8 to posFirst-1, 2 bytes per entity.
    int totalEntities = ((int)posFirst - 8) / 2;
    if (totalEntities <= 0 || totalEntities > 128) {
        Log::Write("FieldArchive: [JSMScan] bad entity count %d for '%s'", totalEntities, fieldName);
        return false;
    }

    int countOthers = totalEntities - countDoors - countLines - countBg;
    if (countOthers < 0) countOthers = 0;

    Log::Write("FieldArchive: [JSMScan] '%s': %d entities (D=%d L=%d B=%d O=%d hdrO=%d) "
               "posFirst=%d posScripts=%d fileSize=%d",
               fieldName, totalEntities, countDoors, countLines, countBg, countOthers,
               countOthersH,
               (int)posFirst, (int)posScripts, (int)jsmData.size());

    // Validate offsets.
    if (posFirst >= jsmData.size() || posScripts >= jsmData.size() ||
        posScripts <= posFirst) {
        Log::Write("FieldArchive: [JSMScan] invalid offsets for '%s'", fieldName);
        return false;
    }

    // --- Parse entity group table ---
    // Each entry: uint16 LE.
    //   Bits 0-6:  method/script count for this entity (max 127)
    //   Bits 7-15: label = starting index into the script entry point table
    // The label directly encodes the start method index — no cumulative tracking needed.
    // Previous code used bits 0-14 as method count, which made entity 0 swallow all methods.
    struct EntityGroup {
        int methodCount;
        int startMethodIdx;  // index into the script entry point table (from label)
        uint16_t rawEntry;   // for diagnostic logging
    };
    EntityGroup groups[128] = {};
    for (int e = 0; e < totalEntities; e++) {
        uint16_t entry = *(const uint16_t*)(jsmData.data() + 8 + e * 2);
        groups[e].rawEntry       = entry;
        groups[e].methodCount    = entry & 0x7F;         // bits 0-6
        groups[e].startMethodIdx = (int)(entry >> 7);    // bits 7-15 = label
    }

    // (Entity group boundary diagnostics logged below after SYM names are loaded.)

    // Script entry point table: from posFirst to posScripts-1.
    // Each entry is uint16 LE = dword-index into script data section.
    int entryPointTableSize = (int)(posScripts - posFirst);
    int totalMethods = entryPointTableSize / 2;
    const uint16_t* entryPoints = (const uint16_t*)(jsmData.data() + posFirst);

    // Script data section: array of native uint32 (little-endian on PC!).
    // The PC port already byte-swapped from PS1 big-endian format.
    // Each instruction is 4 bytes. Entry points are dword indices.
    const uint32_t* scriptData = (const uint32_t*)(jsmData.data() + posScripts);
    int scriptDataDwords = (int)(jsmData.size() - posScripts) / 4;

    Log::Write("FieldArchive: [JSMScan] totalMethods=%d scriptDataDwords=%d othersH=%d",
               totalMethods, scriptDataDwords, countOthersH);

    // Confirmed: The PC engine reads raw file bytes as native LE uint32 with NO byte-swap.
    // Opcode = high byte (SHR word,24). Param = low 24 bits. High byte 0 = push literal.
    // Opcodes > 0xFF use a two-stage dispatch via prefix opcode 0x1C.
    // Diagnostic: opcode frequency histogram to validate against runtime OPCODE-HIST.
    {
        int opcodeHist[0x100] = {};  // primary opcodes are 0x00-0xFF
        int totalOpcodes = 0, totalPushes = 0;
        for (int d = 0; d < scriptDataDwords; d++) {
            uint32_t w = scriptData[d];  // native LE read
            uint8_t highByte = (uint8_t)(w >> 24);
            if (highByte == 0) {
                totalPushes++;
            } else {
                opcodeHist[highByte]++;
                totalOpcodes++;
            }
        }
        Log::Write("FieldArchive: [JSMScan] stats: %d opcodes, %d pushes out of %d",
                   totalOpcodes, totalPushes, scriptDataDwords);
        char histBuf[2048] = {};
        int hp = 0;
        for (int i = 0; i < 0x100 && hp < 1900; i++) {
            if (opcodeHist[i] > 0)
                hp += snprintf(histBuf + hp, 2048 - hp, "%02X=%d ", i, opcodeHist[i]);
        }
        if (hp > 0)
            Log::Write("FieldArchive: [JSMScan] opcodes: %s", histBuf);
    }

    // --- Also load SYM names for cross-reference ---
    char symNames[128][32] = {};
    int symCount = 0;
    LoadSYMNames(fieldName, symNames, 128, symCount);

    // Diagnostic: log entity group boundaries with SYM names.
    for (int e = 0; e < totalEntities; e++) {
        // SYM excludes doors. Current assumption: entities are ordered
        // Door[0..D-1], Line[D..D+L-1], Bg[D+L..D+L+B-1], Other[D+L+B..total-1]
        // so symIdx = e - countDoors.
        int symIdx = e - countDoors;
        const char* sym = (symIdx >= 0 && symIdx < symCount) ? symNames[symIdx] : "(door)";
        if (e < 15 || groups[e].methodCount > 0) {
            Log::Write("FieldArchive: [JSMScan] grp[%d] raw=0x%04X methods=%d startIdx=%d sym='%s'",
                       e, (unsigned)groups[e].rawEntry, groups[e].methodCount,
                       groups[e].startMethodIdx, sym);
        }
    }

    // v0.07.84: REQ-following for indirect MAPJUMP detection.
    // Many FF8 exit entities don't call MAPJUMP directly — they use REQ/REQSW
    // to invoke another entity's method which contains the actual MAPJUMP.
    // Phase 1 (during scan): record which methods contain MAPJUMP and which
    // entities call REQ to which target methods.
    // Phase 2 (after scan): if an unclassified entity REQs a method with MAPJUMP,
    // classify it as MAP_EXIT with that destination.
    static const int MAX_METHOD_MAPJUMPS = 4096;
    // v0.07.87: Extended to track PSHM_W addresses read in MAPJUMP-containing methods.
    static const int MAX_PSHM_PER_METHOD = 16;
    struct MethodMapjump {
        bool found;
        int destFieldId;
        int32_t pshmAddrs[MAX_PSHM_PER_METHOD];  // memory addresses read by PSHM_W
        int pshmCount;
    };
    static MethodMapjump s_methodMapjumps[MAX_METHOD_MAPJUMPS];
    memset(s_methodMapjumps, 0, sizeof(s_methodMapjumps));

    static const int MAX_REQ_PER_ENTITY = 8;
    struct ReqCallInfo { int targetEntity; int targetMethod; };
    struct EntityReqs { ReqCallInfo calls[MAX_REQ_PER_ENTITY]; int count; };
    static EntityReqs s_entityReqs[128];
    memset(s_entityReqs, 0, sizeof(s_entityReqs));

    // v0.07.87: Per-entity POPM_W tracking for variable-dispatch exit detection.
    static const int MAX_POPM_PER_ENTITY = 16;
    struct EntityPopms { int32_t addrs[MAX_POPM_PER_ENTITY]; int count; };
    static EntityPopms s_entityPopms[128];
    memset(s_entityPopms, 0, sizeof(s_entityPopms));

    // --- Scan each entity ---
    for (int e = 0; e < totalEntities && outCount < maxEntities; e++) {
        JSMEntityInfo& info = outEntities[outCount];
        memset(&info, 0, sizeof(info));
        info.jsmIndex = e;
        info.param = -1;

        // Determine JSM category from index ranges.
        // Order: Door[0..D-1], Line[D..D+L-1], Bg[D+L..D+L+B-1], Other[D+L+B..total-1]
        int catStart = 0;
        if (e < countDoors) {
            info.jsmCategory = 0;  // Door
            info.type = JSM_ENT_DOOR;
        } else if (e < countDoors + countLines) {
            info.jsmCategory = 1;  // Line
            info.type = JSM_ENT_LINE_TRIGGER;
        } else if (e < countDoors + countLines + countBg) {
            info.jsmCategory = 2;  // Background
            info.type = JSM_ENT_BACKGROUND;
        } else {
            info.jsmCategory = 3;  // Other
            info.type = JSM_ENT_UNKNOWN;  // will be classified by opcodes
        }

        // Map to SYM name. SYM excludes doors: SYM[i] = JSM entity[i + countDoors].
        int symIdx = e - countDoors;
        if (symIdx >= 0 && symIdx < symCount) {
            strncpy(info.symName, symNames[symIdx], 31);
            info.symName[31] = '\0';
        }

        // --- Scan this entity's scripts for signature opcodes ---
        // We scan ALL methods (init + interaction scripts).
        // Track push stack for extracting parameters (last N pushes before an opcode).
        static const int PUSH_STACK_MAX = 8;
        int32_t pushStack[PUSH_STACK_MAX] = {};
        int pushCount = 0;

        bool foundSetDrawpoint = false;
        bool foundDrawpoint    = false;
        bool foundMenusave     = false;
        bool foundSaveenable   = false;
        bool foundMenushop     = false;
        bool foundCardgame     = false;
        bool foundLadder       = false;
        bool foundMapjump      = false;
        bool foundSetmodel     = false;
        bool foundTalkon       = false;
        bool foundDoorline     = false;
        bool foundParticleon   = false;
        bool foundAdditem      = false;
        int  mapjumpDestField  = -1;
        int  drawpointId       = -1;
        int  shopId            = -1;

        // v0.07.82: Camera/scroll/event flags for Line entity classification.
        bool foundBgdraw       = false;  // BGDRAW or BGOFF
        bool foundScroll       = false;  // any DSCROLL/LSCROLL/CSCROLL/SETCAMERA
        bool foundEventOp      = false;  // SHOW/HIDE/USE/UNUSE/MES/ASK/BATTLE/MOVE/REQ
        bool foundBattle       = false;  // BATTLE specifically

        // Each entity occupies methodCount + 1 method slots in the entry point table.
        // The group entry's count omits the init script (method 0), so we loop
        // from 0 to methodCount INCLUSIVE to cover all methods.
        for (int m = 0; m <= groups[e].methodCount; m++) {
            int methodIdx = groups[e].startMethodIdx + m;
            if (methodIdx >= totalMethods) break;

            uint16_t scriptStart = entryPoints[methodIdx];

            // Find the end of this method: either the start of the next method,
            // or the end of the script data.
            uint16_t scriptEnd = (uint16_t)scriptDataDwords;
            if (methodIdx + 1 < totalMethods)
                scriptEnd = entryPoints[methodIdx + 1];

            // Reset push stack for each method.
            pushCount = 0;

            // v0.07.84: Per-method MAPJUMP tracking for REQ-following.
            bool methodHasMapjump = false;
            int  methodMapjumpDest = -1;

            // v0.07.87: Per-method PSHM_W address tracking for variable-dispatch.
            int32_t methodPshmAddrs[MAX_PSHM_PER_METHOD] = {};
            int methodPshmCount = 0;

            for (int ip = (int)scriptStart; ip < (int)scriptEnd && ip < scriptDataDwords; ip++) {
                uint32_t word = scriptData[ip];  // native LE read of raw file bytes
                uint8_t highByte = (uint8_t)(word >> 24);

                // v0.07.75: SVDUMP diagnostic logging disabled — position extraction confirmed.
                bool detailDump = false;

                if (highByte == 0) {
                    // Push literal: value = full dword (high byte is 0, so max 0x00FFFFFF)
                    int32_t pushVal = (int32_t)word;
                    if (detailDump) {
                        Log::Write("FieldArchive: [SVDUMP] ent=%d m=%d ip=%d PUSH 0x%X (%d) stk=%d",
                                   e, m, ip, (unsigned)pushVal, (int)pushVal, pushCount + 1);
                    }
                    if (pushCount < PUSH_STACK_MAX) {
                        pushStack[pushCount++] = pushVal;
                    } else {
                        for (int s = 0; s < PUSH_STACK_MAX - 1; s++)
                            pushStack[s] = pushStack[s + 1];
                        pushStack[PUSH_STACK_MAX - 1] = pushVal;
                    }
                    continue;
                }

                // Opcode: high byte = primary opcode index (0x01-0xFF).
                // Low 24 bits = param (sign-extended if bit 23 set).
                uint16_t opcode = (uint16_t)highByte;
                int32_t opcParam = (int32_t)(word & 0x00FFFFFF);
                if (word & 0x00800000) opcParam |= (int32_t)0xFF000000;  // sign extend

                if (detailDump) {
                    Log::Write("FieldArchive: [SVDUMP] ent=%d m=%d ip=%d OP 0x%02X param=%d stk=%d word=0x%08X",
                               e, m, ip, (int)opcode, (int)opcParam, pushCount, word);
                }

                // Extended opcodes: primary 0x1C is a prefix for extended dispatch.
                // The engine's 0x1C handler POPS the extended opcode index from the
                // script VM stack (pushed by a preceding PSHN_L), then calls table[popped].
                if (opcode == 0x1C && pushCount > 0) {
                    // Extended dispatch: the 0x1C handler POPS the extended opcode
                    // index from the stack (NOT from the instruction param).
                    // The preceding PSHN_L pushed the dispatch table index.
                    int32_t extOp = pushStack[--pushCount];  // pop
                    // v0.07.75: 0x1C expansion logging disabled — opcode dispatch confirmed.
                    // (Was limited to 100 per field for diagnostic purposes.)
                    if (extOp >= 0 && extOp < 0x200) {
                        opcode = (uint16_t)extOp;
                    }
                } else if (opcode == 0x1C && pushCount == 0) {
                    // Stack empty when 0x1C fires — our simulation lost track.
                    // Try using the LAST pushed value (peek at push history).
                    // For now, log this as a diagnostic.
                    static int s_emptyCount = 0;
                    if (s_emptyCount < 5) {
                        Log::Write("FieldArchive: [JSMScan] 0x1C EMPTY STACK: ent=%d method=%d", e, m);
                        s_emptyCount++;
                    }
                }

                // Model stack effects of known primary opcodes instead of
                // flushing the entire stack. The old flush-all approach caused
                // 0x1C to always hit EMPTY STACK for save/draw point entities
                // whose dispatch index comes from PSHM_W (runtime memory push).
                //
                // For opcodes where we know the stack effect, model it.
                // For unknown opcodes, leave the stack untouched.
                // This is less precise but FAR better than flushing everything.
                if (highByte != 0 && opcode != 0x1C) {
                    switch (highByte) {
                        // Push opcodes: push 1 value from game memory onto VM stack.
                        // We push the param (memory address) as a placeholder.
                        case 0x07: // PSHM_W - push word from memory
                        case 0x09: // PSHM_B - push byte from memory
                        case 0x0A: // PSHM_L - push long from memory
                        case 0x0C: // PSHSM_W - push from special memory
                        case 0x0D: // PSHSM_B - push byte from special memory
                        {
                            // Push a marker: negative offset flags it as "from memory".
                            // 0x1C handler can still pop this; value may or may not
                            // be a valid opcode index (it's a runtime value).
                            int32_t marker = 0x00FF0000 | (opcParam & 0xFFFF);  // marker + mem addr
                            if (pushCount < PUSH_STACK_MAX)
                                pushStack[pushCount++] = marker;
                            // v0.07.87: Record PSHM_W reads for variable-dispatch detection.
                            if (highByte == 0x07 && methodPshmCount < MAX_PSHM_PER_METHOD) {
                                // Deduplicate: only add if not already tracked.
                                bool dup = false;
                                for (int d = 0; d < methodPshmCount; d++)
                                    if (methodPshmAddrs[d] == opcParam) { dup = true; break; }
                                if (!dup)
                                    methodPshmAddrs[methodPshmCount++] = opcParam;
                            }
                            break;
                        }
                        // Pop 1 opcodes:
                        case 0x02: // JPF - conditional jump, pops condition
                        case 0x08: // POPM_W - pop to memory word
                        case 0x0B: // POPM_L - pop to memory long
                            if (pushCount > 0) pushCount--;
                            // v0.07.87: Record POPM_W writes for variable-dispatch detection.
                            if ((highByte == 0x08 || highByte == 0x0B) && e < 128) {
                                if (s_entityPopms[e].count < MAX_POPM_PER_ENTITY) {
                                    bool dup = false;
                                    for (int d = 0; d < s_entityPopms[e].count; d++)
                                        if (s_entityPopms[e].addrs[d] == opcParam) { dup = true; break; }
                                    if (!dup)
                                        s_entityPopms[e].addrs[s_entityPopms[e].count++] = opcParam;
                                }
                            }
                            break;
                        // No stack effect (control flow only):
                        case 0x01: // JMP
                        case 0x03: // JMPB
                        case 0x04: // JMPF variant
                        case 0x05: // LBL
                        case 0x06: // RET
                            break;
                        // All other primary opcodes: unknown stack effect.
                        // Don't flush — leave stack as-is. Some opcodes pop args
                        // and push results, but we can't model them all. Leaving
                        // the stack alone gives 0x1C the best chance of finding
                        // its dispatch index.
                        default:
                            break;
                    }
                }

                // --- Check for signature opcodes ---

                // SET3: position from init script (method 0).
                // Primary: 4 stack params (X, Y, Z, triangleId) — works for literal pushes.
                // Fallback: 3 stack params (X, Y, Z) + triangle from opcParam.
                // The fallback handles entities that use PSHM_W for coordinates
                // (e.g. bggate_2 dp01: X/Y/Z from PSHM_W markers, tri=194 in opcParam).
                // v0.07.75: Added 3-param fallback for draw point position extraction.
                if (opcode == JSM_OP_SET3 && m == 0 && !info.hasPosition) {
                    if (pushCount >= 4) {
                        info.posX = (int16_t)pushStack[pushCount - 4];
                        info.posY = (int16_t)pushStack[pushCount - 3];
                        info.posZ = (int16_t)pushStack[pushCount - 2];
                        info.posTriangle = (uint16_t)pushStack[pushCount - 1];
                        info.hasPosition = true;
                    } else if (pushCount >= 3 && opcParam >= 0 && opcParam < 4096) {
                        // Triangle index from instruction inline param.
                        info.posX = (int16_t)pushStack[pushCount - 3];
                        info.posY = (int16_t)pushStack[pushCount - 2];
                        info.posZ = (int16_t)pushStack[pushCount - 1];
                        info.posTriangle = (uint16_t)opcParam;
                        info.hasPosition = true;
                    }
                }

                // SET: 2D position from init script (method 0).
                // Primary: 3 stack params (X, Y, triangleId).
                // Fallback: 2 stack params (X, Y) + triangle from opcParam.
                if (opcode == JSM_OP_SET && m == 0 && !info.hasPosition) {
                    if (pushCount >= 3) {
                        info.posX = (int16_t)pushStack[pushCount - 3];
                        info.posY = (int16_t)pushStack[pushCount - 2];
                        info.posZ = 0;
                        info.posTriangle = (uint16_t)pushStack[pushCount - 1];
                        info.hasPosition = true;
                    } else if (pushCount >= 2 && opcParam >= 0 && opcParam < 4096) {
                        info.posX = (int16_t)pushStack[pushCount - 2];
                        info.posY = (int16_t)pushStack[pushCount - 1];
                        info.posZ = 0;
                        info.posTriangle = (uint16_t)opcParam;
                        info.hasPosition = true;
                    }
                }

                // SETDRAWPOINT: stack param = draw point ID.
                if (opcode == JSM_OP_SETDRAWPOINT) {
                    foundSetDrawpoint = true;
                    if (pushCount >= 1) drawpointId = pushStack[pushCount - 1];
                }
                if (opcode == JSM_OP_DRAWPOINT) foundDrawpoint = true;

                // Save point.
                if (opcode == JSM_OP_MENUSAVE)   foundMenusave = true;
                if (opcode == JSM_OP_SAVEENABLE)  foundSaveenable = true;
                if (opcode == JSM_OP_PHSENABLE)   foundSaveenable = true;  // also save-point indicator

                // Shop.
                if (opcode == JSM_OP_MENUSHOP) {
                    foundMenushop = true;
                    if (pushCount >= 1) shopId = pushStack[pushCount - 1];
                }

                // Card game.
                if (opcode == JSM_OP_CARDGAME) foundCardgame = true;

                // Ladder.
                if (opcode == JSM_OP_LADDERUP || opcode == JSM_OP_LADDERDOWN)
                    foundLadder = true;

                // Map transitions.
                if (opcode == JSM_OP_MAPJUMP || opcode == JSM_OP_MAPJUMP3 ||
                    opcode == JSM_OP_DISCJUMP || opcode == JSM_OP_MAPJUMPO ||
                    opcode == JSM_OP_WORLDMAPJUMP) {
                    foundMapjump = true;
                    methodHasMapjump = true;  // v0.07.84: per-method tracking
                    // Destination field ID is the deepest (first) push in the sequence.
                    // For MAPJUMP: stack has FieldID, X, Y, TriID (4 pushes).
                    // For MAPJUMP3: stack has FieldID, X, Y, Z, TriID (5 pushes).
                    // The field ID is the oldest push.
                    if (opcode == JSM_OP_MAPJUMP && pushCount >= 4)
                        mapjumpDestField = pushStack[pushCount - 4];
                    else if (opcode == JSM_OP_MAPJUMP3 && pushCount >= 5)
                        mapjumpDestField = pushStack[pushCount - 5];
                    else if (opcode == JSM_OP_DISCJUMP && pushCount >= 5)
                        mapjumpDestField = pushStack[pushCount - 5];
                    else if (opcode == JSM_OP_MAPJUMPO && pushCount >= 4)
                        mapjumpDestField = pushStack[pushCount - 4];
                    else if (opcode == JSM_OP_WORLDMAPJUMP)
                        mapjumpDestField = -2;  // sentinel: goes to world map
                    else if (pushCount >= 1)
                        mapjumpDestField = pushStack[0];  // best guess: oldest push
                    methodMapjumpDest = mapjumpDestField;  // v0.07.84
                }

                // Model assignment / talk.
                if (opcode == JSM_OP_SETMODEL) foundSetmodel = true;
                if (opcode == JSM_OP_TALKON)   foundTalkon = true;

                // Door trigger line.
                if (opcode == JSM_OP_DOORLINEON || opcode == JSM_OP_DOORLINEOFF)
                    foundDoorline = true;

                // Particle effect (draw points and save points use this).
                if (opcode == JSM_OP_PARTICLEON) foundParticleon = true;

                // Item pickup.
                if (opcode == JSM_OP_ADDITEM) foundAdditem = true;

                // v0.07.82: Camera/scroll opcodes for Line entity classification.
                if (opcode == JSM_OP_BGDRAW || opcode == JSM_OP_BGOFF ||
                    opcode == JSM_OP_BGANIME || opcode == JSM_OP_BGANIMESPEED)
                    foundBgdraw = true;
                if (opcode == JSM_OP_DSCROLL || opcode == JSM_OP_LSCROLL ||
                    opcode == JSM_OP_CSCROLL || opcode == JSM_OP_DSCROLLA ||
                    opcode == JSM_OP_LSCROLLA || opcode == JSM_OP_CSCROLLA ||
                    opcode == JSM_OP_SCROLLSYNC ||
                    opcode == JSM_OP_DSCROLLP || opcode == JSM_OP_LSCROLLP ||
                    opcode == JSM_OP_CSCROLLP || opcode == JSM_OP_SETCAMERA)
                    foundScroll = true;
                if (opcode == JSM_OP_SHOW || opcode == JSM_OP_HIDE ||
                    opcode == JSM_OP_USE || opcode == JSM_OP_UNUSE ||
                    opcode == JSM_OP_MES || opcode == JSM_OP_ASK ||
                    opcode == JSM_OP_AMES || opcode == JSM_OP_AASK ||
                    opcode == JSM_OP_MOVE ||
                    opcode == JSM_OP_REQ || opcode == JSM_OP_REQSW || opcode == JSM_OP_REQEW)
                    foundEventOp = true;
                if (opcode == JSM_OP_BATTLE) foundBattle = true;

                // v0.07.84: Extract REQ/REQSW/REQEW call targets for indirect MAPJUMP detection.
                // REQ pops 3 values: entity_id, method_id, priority.
                // We record the target so we can check if it contains MAPJUMP.
                if ((opcode == JSM_OP_REQ || opcode == JSM_OP_REQSW || opcode == JSM_OP_REQEW) &&
                    pushCount >= 3 && e < 128) {
                    int reqTargetEnt  = pushStack[pushCount - 3];
                    int reqTargetMeth = pushStack[pushCount - 2];
                    // Validate: target entity must be a valid JSM index, method must be non-negative.
                    if (reqTargetEnt >= 0 && reqTargetEnt < totalEntities &&
                        reqTargetMeth >= 0 && reqTargetMeth < 100 &&
                        s_entityReqs[e].count < MAX_REQ_PER_ENTITY) {
                        s_entityReqs[e].calls[s_entityReqs[e].count].targetEntity = reqTargetEnt;
                        s_entityReqs[e].calls[s_entityReqs[e].count].targetMethod = reqTargetMeth;
                        s_entityReqs[e].count++;
                    }
                    // Model stack effect: REQ pops 3 values.
                    if (pushCount >= 3) pushCount -= 3;
                }

                // v0.07.72: Removed the old pushCount=0 flush here.
                // Stack effects are now modeled per-opcode above.
                // The old flush wiped the dispatch index for 0x1C calls
                // that followed PSHM_W (runtime memory push) instructions.
            }
            // v0.07.84: Record per-method MAPJUMP for REQ-following.
            if (methodHasMapjump && methodIdx >= 0 && methodIdx < MAX_METHOD_MAPJUMPS) {
                s_methodMapjumps[methodIdx].found = true;
                s_methodMapjumps[methodIdx].destFieldId = methodMapjumpDest;
                // v0.07.87: Copy PSHM_W addresses for variable-dispatch matching.
                int copyCount = (methodPshmCount < MAX_PSHM_PER_METHOD) ? methodPshmCount : MAX_PSHM_PER_METHOD;
                for (int p = 0; p < copyCount; p++)
                    s_methodMapjumps[methodIdx].pshmAddrs[p] = methodPshmAddrs[p];
                s_methodMapjumps[methodIdx].pshmCount = copyCount;
            }
        }

        // --- Classify entity type based on found opcodes ---
        // Priority: most specific first.
        if (foundSetDrawpoint || foundDrawpoint) {
            info.type = JSM_ENT_DRAW_POINT;
            info.param = drawpointId;
        } else if (foundMenusave || foundSaveenable) {
            info.type = JSM_ENT_SAVE_POINT;
        } else if (foundMenushop) {
            info.type = JSM_ENT_SHOP;
            info.param = shopId;
        } else if (foundCardgame) {
            info.type = JSM_ENT_CARD_GAME;
        } else if (foundLadder) {
            info.type = JSM_ENT_LADDER;
        } else if (foundMapjump) {
            info.type = JSM_ENT_MAP_EXIT;
            info.param = mapjumpDestField;
        } else if (foundSetmodel && foundTalkon) {
            info.type = JSM_ENT_NPC;
        } else if (foundDoorline && info.jsmCategory == 0) {
            info.type = JSM_ENT_DOOR;  // keep as door
        }
        // Otherwise, keep the default from JSM category assignment above.

        // v0.07.82: Classify Line entities by opcode signatures.
        // Priority: MAPJUMP > save/draw (already above) > battle > event > camera pan > default.
        // Only reclassify entities that are still JSM_ENT_LINE_TRIGGER (not already
        // promoted to DRAW_POINT/SAVE_POINT/SHOP/etc. by the block above).
        if (info.jsmCategory == 1 && info.type == JSM_ENT_LINE_TRIGGER) {
            if (foundMapjump) {
                info.type = JSM_ENT_LINE_SCREEN_BOUND;
            } else if (foundBattle) {
                info.type = JSM_ENT_LINE_EVENT;
            } else if (foundEventOp) {
                info.type = JSM_ENT_LINE_EVENT;
            } else if (foundBgdraw || foundScroll) {
                info.type = JSM_ENT_LINE_CAMERA_PAN;
            } else {
                // No distinctive opcodes — default to camera pan (most common
                // line entity type per deep research).
                info.type = JSM_ENT_LINE_CAMERA_PAN;
            }
        }

        // v0.07.84: REQ-following post-classification.
        // If this entity is still unclassified (or just "background/unknown")
        // and it calls REQ/REQSW/REQEW to a method that contains MAPJUMP,
        // classify it as MAP_EXIT with that destination.
        if ((info.type == JSM_ENT_UNKNOWN || info.type == JSM_ENT_BACKGROUND ||
             info.type == JSM_ENT_NPC) && e < 128 && info.jsmCategory == 3) {
            for (int r = 0; r < s_entityReqs[e].count; r++) {
                int tgtEnt  = s_entityReqs[e].calls[r].targetEntity;
                int tgtMeth = s_entityReqs[e].calls[r].targetMethod;
                if (tgtEnt < 0 || tgtEnt >= totalEntities) continue;
                // Convert entity-relative method index to global method index.
                // Method 0 = init, method 1 = first interaction, etc.
                int globalMethIdx = groups[tgtEnt].startMethodIdx + tgtMeth;
                if (globalMethIdx < 0 || globalMethIdx >= MAX_METHOD_MAPJUMPS) continue;
                if (s_methodMapjumps[globalMethIdx].found) {
                    info.type = JSM_ENT_MAP_EXIT;
                    info.param = s_methodMapjumps[globalMethIdx].destFieldId;
                    Log::Write("FieldArchive: [JSMScan] REQ-follow: ent%d '%s' -> ent%d method%d has MAPJUMP dest=%d",
                               e, info.symName, tgtEnt, tgtMeth, info.param);
                    break;
                }
            }
        }

        // v0.07.87: Variable-dispatch exit detection.
        // If this "Other" entity writes to a memory address (POPM_W) that a
        // MAPJUMP-containing method also reads (PSHM_W), this entity likely
        // sets a dispatch variable that triggers a map transition in the
        // Director entity's script loop. Classify as MAP_EXIT.
        // v0.07.88: Filter out very low memory addresses (0-7) — these are
        // scratch/temp variables used by virtually every entity (e.g. loop
        // counters, temp flags) and produce massive false positive rates.
        // Real dispatch variables use higher addresses.
        static const int32_t VAR_DISPATCH_MIN_ADDR = 8;
        if ((info.type == JSM_ENT_UNKNOWN || info.type == JSM_ENT_BACKGROUND ||
             info.type == JSM_ENT_NPC) && e < 128 && info.jsmCategory == 3 &&
            s_entityPopms[e].count > 0) {
            for (int p = 0; p < s_entityPopms[e].count; p++) {
                int32_t writeAddr = s_entityPopms[e].addrs[p];
                if (writeAddr < VAR_DISPATCH_MIN_ADDR) continue;  // skip scratch vars
                bool matched = false;
                int matchDest = -1;
                for (int mi = 0; mi < totalMethods && mi < MAX_METHOD_MAPJUMPS && !matched; mi++) {
                    if (!s_methodMapjumps[mi].found) continue;
                    for (int r = 0; r < s_methodMapjumps[mi].pshmCount; r++) {
                        if (s_methodMapjumps[mi].pshmAddrs[r] == writeAddr) {
                            matched = true;
                            matchDest = s_methodMapjumps[mi].destFieldId;
                            break;
                        }
                    }
                }
                if (matched) {
                    info.type = JSM_ENT_MAP_EXIT;
                    info.param = matchDest;
                    Log::Write("FieldArchive: [JSMScan] var-dispatch: ent%d '%s' writes addr %d "
                               "-> matches MAPJUMP method dest=%d",
                               e, info.symName, (int)writeAddr, matchDest);
                    break;
                }
            }
        }

        // v0.07.72: SYM-name fallback classification.
        // Extended opcodes (MENUSAVE, DRAWPOINT, etc.) are dispatched via 0x1C,
        // and save/draw point entities often push the dispatch index from a
        // runtime memory variable (PSHM_W), not a literal. Our scanner can't
        // know the runtime value, so opcode-based classification fails.
        // Fall back to SYM naming conventions for unclassified entities.
        if (info.type == JSM_ENT_UNKNOWN || info.type == JSM_ENT_BACKGROUND) {
            int symIdx2 = e - countDoors;
            if (symIdx2 >= 0 && symIdx2 < symCount) {
                const char* sn = symNames[symIdx2];
                // FF8 uses consistent SYM naming: "savePoint", "svpt", "dp01", etc.
                if (_strnicmp(sn, "save", 4) == 0 || _strnicmp(sn, "svpt", 4) == 0) {
                    info.type = JSM_ENT_SAVE_POINT;
                } else if (_strnicmp(sn, "dp", 2) == 0 && (sn[2] >= '0' && sn[2] <= '9')) {
                    info.type = JSM_ENT_DRAW_POINT;
                    // drawpointId remains -1 (unknown from static scan)
                } else if (_strnicmp(sn, "shop", 4) == 0) {
                    info.type = JSM_ENT_SHOP;
                }
            }
        }

        outCount++;
    }

    // v0.07.88: Diagnostic — log POPM_W addresses for unclassified "Other" entities.
    // This helps identify which memory addresses real exit entities write to.
    for (int e2 = 0; e2 < outCount && e2 < 128; e2++) {
        const JSMEntityInfo& ei = outEntities[e2];
        if (ei.jsmCategory != 3) continue;  // only Others
        if (ei.type != JSM_ENT_UNKNOWN && ei.type != JSM_ENT_NPC) continue;  // only unclassified
        if (s_entityPopms[ei.jsmIndex].count == 0) continue;
        char addrBuf[256] = {};
        int pos = 0;
        for (int p = 0; p < s_entityPopms[ei.jsmIndex].count && pos < 240; p++)
            pos += snprintf(addrBuf + pos, 256 - pos, "%d ", (int)s_entityPopms[ei.jsmIndex].addrs[p]);
        Log::Write("FieldArchive: [JSMScan] POPM_W diag: ent%d '%s' type=%s writes=[%s]",
                   ei.jsmIndex, ei.symName, JSMEntityTypeName(ei.type), addrBuf);
    }

    // v0.07.89: Diagnostic — dump ALL s_methodMapjumps entries for the field.
    // This shows what PSHM_W addresses each MAPJUMP-containing method reads,
    // so we can compare against POPM_W addresses from exit entities (l1-l6 etc.)
    // and find where the variable-dispatch chain breaks.
    {
        int mjCount = 0;
        for (int mi = 0; mi < totalMethods && mi < MAX_METHOD_MAPJUMPS; mi++) {
            if (!s_methodMapjumps[mi].found) continue;
            mjCount++;
            // Find which entity owns this method by checking group boundaries.
            int ownerEnt = -1;
            for (int oe = 0; oe < totalEntities; oe++) {
                int mStart = groups[oe].startMethodIdx;
                int mEnd   = mStart + groups[oe].methodCount;  // inclusive range is [mStart..mEnd]
                if (mi >= mStart && mi <= mEnd) {
                    ownerEnt = oe;
                    break;
                }
            }
            int ownerSym = ownerEnt - countDoors;
            const char* ownerName = (ownerSym >= 0 && ownerSym < symCount) ? symNames[ownerSym] : "?";
            // Build PSHM_W address list.
            char pshmBuf[256] = {};
            int pp = 0;
            for (int r = 0; r < s_methodMapjumps[mi].pshmCount && pp < 240; r++)
                pp += snprintf(pshmBuf + pp, 256 - pp, "%d ", (int)s_methodMapjumps[mi].pshmAddrs[r]);
            Log::Write("FieldArchive: [JSMScan] MAPJUMP-method diag: method=%d owner=ent%d '%s' dest=%d pshm=[%s]",
                       mi, ownerEnt, ownerName, s_methodMapjumps[mi].destFieldId, pshmBuf);
        }
        Log::Write("FieldArchive: [JSMScan] MAPJUMP-method diag: %d methods with MAPJUMP out of %d total",
                   mjCount, totalMethods);
    }

    // v0.07.93: INF gateway diagnostic using correct Deling format.
    // Gateways at offset 0x64, stride 32, fieldId at +18.
    {
        std::vector<uint8_t> infDiag;
        if (ExtractInnerFile(fieldName, ".inf", infDiag) && infDiag.size() >= 676) {
            const uint8_t* infBase = infDiag.data();
            // Log field name from INF header (first 9 bytes).
            char infName[10] = {};
            memcpy(infName, infBase, 9);
            Log::Write("FieldArchive: [INF-DIAG] '%s' size=%d infName='%s'",
                       fieldName, (int)infDiag.size(), infName);
            for (int gi = 0; gi < 12; gi++) {
                const uint8_t* gw = infBase + 0x64 + gi * 32;
                int16_t  x1 = *(const int16_t*)(gw + 0);
                int16_t  y1 = *(const int16_t*)(gw + 2);
                int16_t  z1 = *(const int16_t*)(gw + 4);
                int16_t  x2 = *(const int16_t*)(gw + 6);
                int16_t  y2 = *(const int16_t*)(gw + 8);
                int16_t  z2 = *(const int16_t*)(gw + 10);
                // Destination point (spawn position in target field)
                int16_t  dx = *(const int16_t*)(gw + 12);
                int16_t  dy = *(const int16_t*)(gw + 14);
                int16_t  dz = *(const int16_t*)(gw + 16);
                uint16_t destId = *(const uint16_t*)(gw + 18);
                const char* destName = (destId < 0x7FFF && destId < (uint16_t)s_fieldNames.size())
                                       ? GetFieldNameById(destId) : nullptr;
                float cx = (float)(x1 + x2) / 2.0f;
                float cy = (float)(y1 + y2) / 2.0f;
                Log::Write("FieldArchive: [INF-DIAG] gw[%d] line=(%d,%d,%d)->(%d,%d,%d) center=(%.0f,%.0f) "
                           "dest=(%d,%d,%d) fieldId=%u '%s'",
                           gi, (int)x1, (int)y1, (int)z1, (int)x2, (int)y2, (int)z2,
                           cx, cy, (int)dx, (int)dy, (int)dz,
                           (unsigned)destId,
                           destName ? destName : (destId == 0xFFFF ? "UNUSED" : (destId == 0x7FFF ? "SENTINEL" : "?")));
            }
        } else {
            Log::Write("FieldArchive: [INF-DIAG] no INF or too small for '%s' (size=%d)",
                       fieldName, infDiag.empty() ? 0 : (int)infDiag.size());
        }
    }

    // --- Log results ---
    int drawPoints = 0, savePoints = 0, shops = 0, cards = 0, ladders = 0, exits = 0;
    int lineCameraPans = 0, lineScreenBounds = 0, lineEvents = 0;
    for (int i = 0; i < outCount; i++) {
        const JSMEntityInfo& e = outEntities[i];
        switch (e.type) {
            case JSM_ENT_DRAW_POINT:        drawPoints++; break;
            case JSM_ENT_SAVE_POINT:        savePoints++; break;
            case JSM_ENT_SHOP:              shops++; break;
            case JSM_ENT_CARD_GAME:         cards++; break;
            case JSM_ENT_LADDER:            ladders++; break;
            case JSM_ENT_MAP_EXIT:          exits++; break;
            case JSM_ENT_LINE_CAMERA_PAN:   lineCameraPans++; break;
            case JSM_ENT_LINE_SCREEN_BOUND: lineScreenBounds++; break;
            case JSM_ENT_LINE_EVENT:        lineEvents++; break;
            default: break;
        }
    }
    Log::Write("FieldArchive: [JSMScan] '%s' results: %d entities scanned — "
               "DrawPts=%d SavePts=%d Shops=%d Cards=%d Ladders=%d Exits=%d "
               "LineCamPan=%d LineScreenBd=%d LineEvent=%d",
               fieldName, outCount, drawPoints, savePoints, shops, cards, ladders, exits,
               lineCameraPans, lineScreenBounds, lineEvents);

    // Detailed log for each classified entity.
    for (int i = 0; i < outCount; i++) {
        const JSMEntityInfo& e = outEntities[i];
        if (e.type == JSM_ENT_UNKNOWN || e.type == JSM_ENT_BACKGROUND ||
            e.type == JSM_ENT_DOOR || e.type == JSM_ENT_LINE_TRIGGER ||
            e.type == JSM_ENT_LINE_CAMERA_PAN)  // v0.07.82: camera pans are too numerous to log individually
            continue;  // skip uninteresting entries to reduce log noise

        const char* destName = "";
        if (e.type == JSM_ENT_MAP_EXIT && e.param >= 0) {
            destName = GetFieldNameById((uint16_t)e.param);
            if (!destName) destName = "(unknown)";
        }

        Log::Write("FieldArchive: [JSMScan]   ent%d cat=%d type=%s sym='%s' "
                   "pos=%s(%d,%d,%d tri=%d) param=%d%s%s",
                   e.jsmIndex, e.jsmCategory, JSMEntityTypeName(e.type),
                   e.symName,
                   e.hasPosition ? "YES" : "no",
                   (int)e.posX, (int)e.posY, (int)e.posZ, (int)e.posTriangle,
                   e.param,
                   (e.type == JSM_ENT_MAP_EXIT && e.param >= 0) ? " dest=" : "",
                   destName);
    }

    return true;
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
