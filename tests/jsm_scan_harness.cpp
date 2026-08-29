// jsm_scan_harness -- runs the REAL ScanJSMScripts over real field files on the
// build host and dumps what it found.
//
// Until v0.59.0 nothing on this side of the build could execute the field-script
// scanner at all: field_archive.cpp is a Win32 translation unit and its archive
// reader wants the game's 294 MB field.fs. Both are dealt with here -- a minimal
// windows.h shim (tests/winshim) and the FF8OPC_ARCHIVE_TEST_SEAM hook, which
// feeds already-extracted per-field files straight off disk.
//
//   ./jsm_scan_harness <dir> [field ...]      dump those fields
//   ./jsm_scan_harness <dir> --all            dump every field in <dir>
//
// <dir> holds one subdirectory per field, each containing <field>.jsm/.sym/...
// (fieldsim/ffield.py writes exactly that layout).
//
// The dump is the scanner's own answers -- entity, category, type, position,
// destination -- so two builds can be diffed against each other directly.
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include <cstdint>
#include <algorithm>
#include <cstdarg>
#include <sys/mman.h>
#include <dirent.h>
#include <sys/stat.h>

#include <windows.h>          // tests/winshim

namespace Log {
    static bool g_verbose = false;
    void Field(const char* fmt, ...) {
        if (!g_verbose) return;
        va_list ap; va_start(ap, fmt); vprintf(fmt, ap); va_end(ap); putchar('\n');
    }
    void Mod(const char* fmt, ...) { (void)fmt; }
}

static std::string g_root;
static std::string g_field;

// The seam field_archive.cpp calls instead of its archive reader. It is declared
// inside namespace FieldArchive, so define it there.
namespace FieldArchive {
bool TestExtractInnerFile(const char* fieldName, const char* innerExt,
                          std::vector<uint8_t>& out)
{
    std::string p = g_root + "/" + fieldName + "/" + fieldName + innerExt;
    FILE* f = fopen(p.c_str(), "rb");
    if (!f) return false;
    fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
    out.resize((size_t)(n > 0 ? n : 0));
    if (n > 0 && fread(out.data(), 1, (size_t)n, f) != (size_t)n) { fclose(f); return false; }
    fclose(f);
    return true;
}
}  // namespace FieldArchive

#define FF8OPC_ARCHIVE_TEST_SEAM 1
#include "field_archive.cpp"

// v0.62.2: the decoded story gate, so the golden can see it. Format:
//   var[<addr>]:<width>B<op><value>   or "-" when the scanner found none.
static const char* gateBuf(const FieldArchive::JSMEntityInfo& e) {
    static char b[48];
    if (!e.hasGate) return "-";
    static const char* kOps[] = { "?", "?", "?", "?", "?", "?",
                                  "==", ">", ">=", "<", "<=", "!=" };
    const char* op = (e.gateOp <= 11) ? kOps[e.gateOp] : "?";
    snprintf(b, sizeof b, "var[%d]:%dB%s%d", (int)e.gateAddr, (int)e.gateWidth,
             op, (int)e.gateValue);
    return b;
}

static void dumpField(const char* name)
{
    g_field = name;
    FieldArchive::JSMEntityInfo ents[128];
    int n = 0;
    if (!FieldArchive::ScanJSMScripts(name, ents, 128, n)) {
        printf("%-10s SCAN FAILED\n", name);
        return;
    }
    printf("### %s  entities=%d\n", name, n);
    for (int i = 0; i < n; i++) {
        const FieldArchive::JSMEntityInfo& e = ents[i];
        printf("  g%-3d slot%-3d cat%d %-20s %-22s pos=%s(%d,%d) tri=%u param=%d "
               "model=%d talk=%d dlg=%d item=%d req=%d interp=%d gate=%s btn=%04X\n",
               e.jsmIndex, e.runtimeSlot, e.jsmCategory, e.symName,
               FieldArchive::JSMEntityTypeName(e.type),
               e.hasPosition ? "" : "?", (int)e.posX, (int)e.posY,
               (unsigned)e.posTriangle, e.param, 0,
               e.hasTalkSetup ? 1 : 0, e.hasDialogReqTarget ? 1 : 0,
               e.isItemPickup ? 1 : 0, e.isReqTarget ? 1 : 0,
               e.paramFromInterp ? 1 : 0, gateBuf(e),
               (unsigned)e.touchButtonMask);
    }
}

int main(int argc, char** argv)
{
    if (argc < 2) { fprintf(stderr, "usage: jsm_scan_harness <dir> [--all | field...]\n"); return 2; }
    g_root = argv[1];
    // The MAPJUMP resolver's interpreter reads the live field variable bank at
    // 0x01CFE9B8 (field_archive_jsm_mapjump_resolver.inl:362). On Windows its
    // __try makes that harmless; here it is a real fault, so give it a page of
    // zeros -- which is also what the bank reads as before any script has run.
    if (mmap((void*)0x01CFE000, 0x4000, PROT_READ | PROT_WRITE,
             MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0) == MAP_FAILED)
        fprintf(stderr, "WARNING: mmap(0x01CFE000) failed -- the exit resolver will fault\n");
    // The archive reader is bypassed by the seam, but the guards still test it.
    FieldArchive::ForceInitializedForTest();
    std::vector<std::string> fields;
    if (argc >= 3 && strcmp(argv[2], "--all") == 0) {
        DIR* d = opendir(g_root.c_str());
        if (!d) { fprintf(stderr, "cannot open %s\n", g_root.c_str()); return 2; }
        struct dirent* de;
        while ((de = readdir(d)) != nullptr) {
            if (de->d_name[0] == '.' || de->d_name[0] == '_') continue;
            struct stat st;
            std::string p = g_root + "/" + de->d_name;
            if (stat(p.c_str(), &st) == 0 && S_ISDIR(st.st_mode)) fields.push_back(de->d_name);
        }
        closedir(d);
        std::sort(fields.begin(), fields.end());
    } else {
        for (int i = 2; i < argc; i++) {
            if (strcmp(argv[i], "-v") == 0) { Log::g_verbose = true; continue; }
            fields.push_back(argv[i]);
        }
    }
    for (const std::string& f : fields) dumpField(f.c_str());
    return 0;
}
