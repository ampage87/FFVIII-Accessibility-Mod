// jsm_invistalk_test -- HIDE + TALKON in one init is an invisible interaction
// point, not an absent object.
//
// WHY THIS EXISTS
// ---------------
// Aaron, after the 12:30 BAT: "in the passenger compartment is the terminal you
// are supposed to interact with to hear the briefing on the Propagators, but the
// terminal is not appearing in the catalog."
//
// rgguest2's `comp` IS that terminal, and its init is SET3 / HIDE(0x061) /
// TALKON(0x057). FF8 paints the terminal into the background art and leaves a
// hidden model behind it purely to carry the talk target -- the same trick it
// uses for panels, consoles and signs all over the disc. The catalog's HIDE
// filter, written for a prop the script has not revealed yet (the sewer's
// un-knocked ladder), dropped it.
//
// The discriminator is narrow on purpose: BOTH opcodes, BOTH in method 0. The
// catalog harness can set the resulting flag directly, so it cannot tell whether
// the scanner derives it correctly -- and a mutant that asked only for HIDE
// passed the whole suite. This test drives the real scanner over the real
// rgguest2 bytes, plus two copies of those bytes with exactly one word changed,
// so the rule is pinned from both sides rather than only from the side that
// happens to be true on this disc.
#include <cstdio>
#include <cstring>
#include <cstdint>
#include <cstddef>
#include <string>
#include <vector>
#include <cstdarg>
#include <sys/mman.h>

#include <windows.h>          // tests/winshim

#include "jsm_invistalk_fixtures.h"

namespace Log {
    static bool g_verbose = false;
    void Field(const char* fmt, ...) {
        if (!g_verbose) return;
        va_list ap; va_start(ap, fmt); vprintf(fmt, ap); va_end(ap); putchar('\n');
    }
    void Mod(const char* fmt, ...) { (void)fmt; }
}

// Which .jsm the seam should serve this run.
static const unsigned char* g_jsm = nullptr;
static size_t g_jsmLen = 0;

namespace FieldArchive {
bool TestExtractInnerFile(const char* fieldName, const char* innerExt,
                          std::vector<uint8_t>& out)
{
    (void)fieldName;
    if (strcmp(innerExt, ".jsm") == 0) { out.assign(g_jsm, g_jsm + g_jsmLen); return true; }
    if (strcmp(innerExt, ".sym") == 0) {
        out.assign(JSM_INVISTALK_SYM, JSM_INVISTALK_SYM + sizeof(JSM_INVISTALK_SYM));
        return true;
    }
    return false;
}
}  // namespace FieldArchive

#define FF8OPC_ARCHIVE_TEST_SEAM 1
#include "field_archive.cpp"

static int bad = 0;
static void check(bool ok, const char* what)
{
    if (!ok) { std::printf("  BAD: %s\n", what); bad++; }
}

// Scan one variant and report what the scanner made of the named entity.
static bool invisTalkOf(const unsigned char* jsm, size_t n, const char* sym)
{
    g_jsm = jsm; g_jsmLen = n;
    FieldArchive::JSMEntityInfo ents[128];
    int count = 0;
    if (!FieldArchive::ScanJSMScripts("rgguest2", ents, 128, count)) {
        std::printf("  BAD: the scanner refused the fixture\n"); bad++; return false;
    }
    for (int i = 0; i < count; i++)
        if (strcmp(ents[i].symName, sym) == 0) return ents[i].invisibleTalkTarget;
    std::printf("  BAD: '%s' not found in the scan\n", sym); bad++;
    return false;
}

int main()
{
    std::printf("jsm_invistalk_test\n");
    // The MAPJUMP resolver's interpreter reads the live field variable bank; on
    // Windows its __try makes that harmless, here it is a real fault.
    if (mmap((void*)0x01CFE000, 0x4000, PROT_READ | PROT_WRITE,
             MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0) == MAP_FAILED)
        std::fprintf(stderr, "WARNING: mmap(0x01CFE000) failed\n");
    FieldArchive::ForceInitializedForTest();

    check(invisTalkOf(JSM_INVISTALK_REAL, sizeof(JSM_INVISTALK_REAL), "comp"),
          "**the Ragnarok terminal is an invisible interaction point** -- its own init "
          "calls HIDE and TALKON together, which is FF8 saying the picture is in the "
          "background and this model is here to be talked to");

    check(!invisTalkOf(JSM_INVISTALK_NO_TALK, sizeof(JSM_INVISTALK_NO_TALK), "comp"),
          "**HIDE alone is NOT enough** -- that is the sewer's un-knocked ladder and "
          "every scene actor parked off-stage, which is exactly what the catalog's "
          "HIDE filter was built to drop");

    check(!invisTalkOf(JSM_INVISTALK_NO_HIDE, sizeof(JSM_INVISTALK_NO_HIDE), "comp"),
          "**and TALKON alone is NOT enough either** -- almost every NPC on the disc "
          "enables talking, and none of them needs an exemption from a filter that "
          "was never going to touch them");

    check(!invisTalkOf(JSM_INVISTALK_LATE_TALK, sizeof(JSM_INVISTALK_LATE_TALK), "comp"),
          "**and the two have to be in the INIT, not merely somewhere in the "
          "scripts** -- an entity that enables talking later has not told us its "
          "picture is in the background; nothing on this disc separates the two "
          "readings by itself, which is why this fixture manufactures the case");

    check(!invisTalkOf(JSM_INVISTALK_REAL, sizeof(JSM_INVISTALK_REAL), "alien01"),
          "**and the Propagator in the same room is not one** -- it is a visible "
          "monster, and a rule this narrow has to say no to the entity standing "
          "next to the one it says yes to");

    std::printf(bad ? "jsm_invistalk_test: FAILED (%d bad)\n" : "jsm_invistalk_test: OK (%d bad)\n", bad);
    return bad ? 1 : 0;
}
