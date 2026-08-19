// menu_save_query_compile.cpp -- v0.23.4 (#120)
//
// Compile probe and offline gate for src/menu_tts_save_query.inl. No host
// harness builds menu_tts.cpp, so without this the Save overwrite dialog would
// get no pre-MSVC check at all -- and the difference between Aaron waking up to
// a build and to a compiler error is exactly this file.
//
//   g++ -std=c++17 -O0 -Isrc -o menu_save_query_compile tests/menu_save_query_compile.cpp
//
// It RUNS as well as compiles: the module pool and the query-window globals are
// mapped at the game's own addresses and filled with real FF8 text-stream bytes
// lifted out of mngrp.bin, so the walk, the fallback, the state gate, the text
// shift and the wording are all exercised rather than merely parsed.
//
// What it cannot check is whether 0x004E3090 really is the Save state machine
// or whether 0x37 really is the confirmation -- an offline fixture agrees with
// whatever the file believes. Those are what the BAT is for.
//
// Same honest limitation as menu_magic_compile.cpp: the host is 64-bit and the
// game 32-bit, so pointer fields planted here are eight bytes where the real
// module has four. This checks CONTROL FLOW and sentinel handling, not the byte
// layout -- hence every planted pointer is written at full width.

#include <cstdio>
#include <cstdarg>
#include <cstring>
#include <cstdint>
#include <cstddef>
#include <string>
#include <sys/mman.h>

typedef unsigned long  DWORD;
typedef unsigned short WORD;
typedef unsigned char  BYTE;

// Standard headers first: libstdc++ has its own __try/__catch and redefining
// them earlier turns <string> into a syntax error.
#undef __try
#undef __catch
#undef __throw_exception_again
#define __try if (1)
#define __except(x) else
#define EXCEPTION_EXECUTE_HANDLER 1

static DWORD g_tick = 100000;
static DWORD GetTickCount() { return g_tick; }

namespace Log { void Menu(const char*, ...) {} }
namespace ScreenReader {
    char g_last[512];
    int  g_count = 0;
    bool Speak(const char* t, bool = false) {
        snprintf(g_last, sizeof(g_last), "%s", t ? t : "");
        g_count++;
        return true;
    }
    bool IsSpeaking() { return false; }
}

// The mod's glyph-table decoder, stubbed as the inverse of the `- 0x20` shift
// the reader applies. That keeps this a check of CONTROL FLOW; the ENCODING
// itself is pinned in tests/menu_sim.cpp against bytes lifted from the real
// mngrp.bin, because a stub cannot falsify its own convention.
namespace FF8TextDecode {
    std::string DecodeMenuText(const uint8_t* d, size_t n) {
        std::string s;
        for (size_t i = 0; i < n; i++) s += (char)(d[i] + 0x20);
        return s;
    }
}

// Read from the host translation unit by menu_tts_save_query.inl.
static WORD*   pMenuStateA = nullptr;
static uint8_t s_prevBlockCursor = 0xFF;
static bool    s_saveQueryDialogOpen = false;

#include "menu_magic_model.inl"
#include "menu_tts_save_query.inl"

static int bad = 0;
static void check(bool ok, const char* what)
{
    if (!ok) { bad++; printf("  BAD: %s\n", what); }
}
static void expect(const char* got, const char* want, const char* what)
{
    if (strcmp(got, want) != 0) {
        bad++;
        printf("  BAD: %s\n        got  \"%s\"\n        want \"%s\"\n", what, got, want);
    }
}

static void* MapAt(uintptr_t addr, size_t len)
{
    const uintptr_t pg = addr & ~(uintptr_t)0xFFF;
    const size_t    sz = ((addr + len) - pg + 0xFFF) & ~(size_t)0xFFF;
    void* p = mmap((void*)pg, sz, PROT_READ | PROT_WRITE,
                   MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0);
    return (p == MAP_FAILED) ? nullptr : p;
}

// ---------------------------------------------------------------------------
// Fixtures. These are the game's own strings as they sit in mngrp.bin section 1
// bank 5 (entries 5, 6, 7): TEXT-STREAM bytes, which are `glyph + 0x20`. With
// the decoder stub above, encoding is the identity -- so writing ASCII here IS
// writing the text-stream form, and a reader that forgot the shift would show
// up as mangled output rather than passing quietly.
//
// The prompt carries the real double space, because collapsing it is one of the
// things this file is here to prove.
static const char* PROMPT_BYTES = "Data exists.  Overwrite?";
static const char* YES_BYTES    = "Yes";
static const char* NO_BYTES     = "No";

static void PlantText(uintptr_t slot, const char* s)
{
    static char pool[8][128];
    static int  n = 0;
    if (!s) { *(const char* volatile*)slot = nullptr; return; }
    snprintf(pool[n], sizeof(pool[0]), "%s", s);
    *(const char* volatile*)slot = pool[n];
    n = (n + 1) % 8;
}

static uint8_t* PlantModule(int slotIdx, uint16_t state, uint8_t panel,
                            uint8_t cursor, uint8_t block, uint8_t saveSlot)
{
    uint8_t* m = (uint8_t*)(SQ_POOL_BASE + slotIdx * 0x78);
    memset(m, 0, 0x78);
    *(uint32_t*)(m + SQO_UPDATE_FN) = SQ_SAVE_STATE_FN;
    *(uint16_t*)(m + SQO_STATE)     = state;
    *(m + SQO_PANEL)     = panel;
    *(m + SQO_QUERY_CUR) = cursor;
    *(m + SQO_BLOCK_CUR) = block;
    *(m + SQO_SLOT_CUR)  = saveSlot;
    return m;
}

static void Tick() { g_tick += 200; }

int main()
{
    printf("menu_tts_save_query.inl compiles\n");

    // --- 1. The wording, with no addresses involved at all -------------------
    expect(SaveQueryLine("Data exists. Overwrite?", "No").c_str(),
           "Data exists. Overwrite? No.", "prompt + No");
    expect(SaveQueryLine("Data exists. Overwrite?", "Yes").c_str(),
           "Data exists. Overwrite? Yes.", "prompt + Yes");
    // A prompt that already ends in punctuation must not collect a second stop.
    expect(SaveQueryLine("Data exists. Overwrite?", "").c_str(),
           "Data exists. Overwrite?", "empty option leaves the question alone");

    // --- 2. The decoder: shift, double space, line break, sentinels ----------
    {
        uint8_t raw[64];
        int n = 0;
        for (const char* p = PROMPT_BYTES; *p; p++) raw[n++] = (uint8_t)*p;
        raw[n] = 0;
        expect(SaveQueryDecode(raw, n, "fallback").c_str(),
               "Data exists. Overwrite?", "double space collapses to one");

        // 0x02 is the line break. SaveQueryCopyText turns it into a space; if
        // it ever stops doing so, MagicTextToGlyphs drops it and the words weld.
        uint8_t two[32];
        uint8_t src[32];
        int k = 0;
        src[k++] = 'a'; src[k++] = 0x02; src[k++] = 'b'; src[k] = 0;
        const int cn = SaveQueryCopyText(src, two, sizeof(two));
        expect(SaveQueryDecode(two, cn, "fallback").c_str(), "a b",
               "a line break separates words rather than welding them");

        expect(SaveQueryDecode(raw, 0, "fallback").c_str(), "fallback",
               "no text falls back to the literal");
        expect(SaveQueryDecode(raw, -1, "fallback").c_str(), "fallback",
               "a fault falls back to the literal");

        uint8_t out[16];
        check(SaveQueryCopyText(nullptr, out, sizeof(out)) == 0,
              "a null text pointer reads as no text");
        check(SaveQueryCopyText((const uint8_t*)SQ_TEXT_FALLBACK, out, sizeof(out)) == 0,
              "the getter's own fallback sentinel reads as no text");
        check(SaveQueryCopyText((const uint8_t*)SQ_TEXT_EMPTY, out, sizeof(out)) == 0,
              "the empty-string constant reads as no text");
    }

    // --- 3. Everything below needs the real addresses mapped ----------------
    // The list head, the pool and all three query-window globals live between
    // 0x01D76B48 and 0x01D77310 -- one mapping, because MAP_FIXED_NOREPLACE
    // refuses a second call that lands in a page the first already took.
    if (!MapAt(SQ_LIST_HEAD, (size_t)((SQ_PROMPT_PTR + 0x10) - SQ_LIST_HEAD))) {
        printf("  (could not map the game address ranges -- skipping the live checks)\n");
        printf("menu_save_query_compile: %s (%d bad)\n", bad ? "FAILED" : "OK", bad);
        return bad ? 1 : 0;
    }

    uint8_t** head = (uint8_t**)SQ_LIST_HEAD;

    // 3a. Empty list -> no module, no crash.
    *head = nullptr;
    check(SaveQueryFindModule() == nullptr, "an empty list must yield no module");

    // 3b. Save found in each of the ten slots, behind a chain of decoys.
    for (int place = 0; place < 10; place++) {
        for (int i = 0; i < 10; i++) {
            uint8_t* m = (uint8_t*)(SQ_POOL_BASE + i * 0x78);
            memset(m, 0, 0x78);
            *(uint32_t*)(m + SQO_UPDATE_FN) = 0xDEADBEEF;   // a decoy module
            *(uint8_t**)m = (i + 1 < 10) ? (uint8_t*)(SQ_POOL_BASE + (i + 1) * 0x78)
                                         : nullptr;
        }
        uint8_t* want = (uint8_t*)(SQ_POOL_BASE + place * 0x78);
        *(uint32_t*)(want + SQO_UPDATE_FN) = SQ_SAVE_STATE_FN;
        *head = (uint8_t*)SQ_POOL_BASE;
        check(SaveQueryFindModule() == want, "Save must be found in every pool slot");
    }

    // 3c. A pointer outside the pool, a misaligned one and a cycle must all
    //     terminate rather than walk into the process or hang the game thread.
    *head = (uint8_t*)(SQ_POOL_BASE - 0x1000);
    check(SaveQueryFindModule() == nullptr, "an out-of-pool head must be refused");
    *head = (uint8_t*)(SQ_POOL_BASE + 3);
    check(SaveQueryFindModule() == nullptr, "a misaligned head must be refused");
    {
        uint8_t* a = (uint8_t*)(SQ_POOL_BASE + 0 * 0x78);
        uint8_t* b = (uint8_t*)(SQ_POOL_BASE + 1 * 0x78);
        memset(a, 0, 0x78); memset(b, 0, 0x78);
        *(uint8_t**)a = b; *(uint8_t**)b = a;
        *head = a;
        check(SaveQueryFindModule() == nullptr, "a cycle must terminate, not hang");
    }

    // 3d. The fallback is only accepted when the update function agrees.
    {
        static WORD fakeState[0x400];
        pMenuStateA = fakeState;
        memset(fakeState, 0, sizeof(fakeState));
        check(SaveQueryModuleFallback() == nullptr,
              "a historical base with the wrong update fn must be refused");
        uint8_t* base6 = (uint8_t*)fakeState + SQ_FALLBACK_MODE6;
        *(uint32_t*)(base6 + SQO_UPDATE_FN) = SQ_SAVE_STATE_FN;
        check(SaveQueryModuleFallback() == base6, "the mode-6 base must be accepted");
        memset(fakeState, 0, sizeof(fakeState));
        uint8_t* base1 = (uint8_t*)fakeState + SQ_FALLBACK_MODE1;
        *(uint32_t*)(base1 + SQO_UPDATE_FN) = SQ_SAVE_STATE_FN;
        check(SaveQueryModuleFallback() == base1, "the mode-1 base must be accepted");
        pMenuStateA = nullptr;
    }

    // --- 4. The poll, driven the way the game drives it ---------------------
    PlantText(SQ_PROMPT_PTR, PROMPT_BYTES);
    PlantText(SQ_OPT_PTR[0], YES_BYTES);
    PlantText(SQ_OPT_PTR[1], NO_BYTES);

    // 4a. Block selection (state 0x21) must say nothing.
    ResetSaveQueryDialog();
    ScreenReader::g_count = 0;
    *head = PlantModule(2, 0x21, 5, 1, 6, 0);
    Tick(); PollSaveOverwriteDialog();
    check(ScreenReader::g_count == 0, "the block list must not trigger the dialog");
    check(!s_saveQueryDialogOpen, "the guard flag stays down outside the dialog");

    // 4b. The dialog opens: the game's sentence and the armed option, once.
    *head = PlantModule(2, SQ_STATE_OVERWRITE, SQ_PANEL_OVERWRITE, 1, 6, 0);
    Tick(); PollSaveOverwriteDialog();
    expect(ScreenReader::g_last, "Data exists. Overwrite? No.", "the dialog announces on arrival");
    check(ScreenReader::g_count == 1, "arrival speaks exactly once");
    check(s_saveQueryDialogOpen, "the guard flag goes up while the dialog is open");

    // 4c. Standing still must stay silent.
    Tick(); PollSaveOverwriteDialog();
    Tick(); PollSaveOverwriteDialog();
    check(ScreenReader::g_count == 1, "a stationary cursor must not repeat the line");

    // 4d. Moving to Yes says only the option.
    *(uint8_t*)((uint8_t*)(SQ_POOL_BASE + 2 * 0x78) + SQO_QUERY_CUR) = 0;
    Tick(); PollSaveOverwriteDialog();
    expect(ScreenReader::g_last, "Yes", "a cursor move speaks the option alone");
    check(ScreenReader::g_count == 2, "a cursor move speaks exactly once");

    // 4e. Confirming Yes: the screen moves on by itself, so the block latch
    //     must be left alone -- clearing it here would talk over the save.
    s_prevBlockCursor = 6;
    *head = PlantModule(2, 0x38, 7, 0, 6, 0);
    Tick(); PollSaveOverwriteDialog();
    check(ScreenReader::g_count == 2, "confirming must not speak here");
    check(s_prevBlockCursor == 6, "confirming must NOT reset the block latch");
    check(!s_saveQueryDialogOpen, "the guard flag comes down on close");

    // 4f. Cancelling on No returns to a block list that redraws silently, so
    //     the latch IS cleared and menu_tts_save.inl re-reads the block.
    ResetSaveQueryDialog();
    *head = PlantModule(2, SQ_STATE_OVERWRITE, SQ_PANEL_OVERWRITE, 1, 6, 0);
    Tick(); PollSaveOverwriteDialog();
    s_prevBlockCursor = 6;
    *head = PlantModule(2, 0x20, 5, 1, 6, 0);
    Tick(); PollSaveOverwriteDialog();
    check(s_prevBlockCursor == 0xFF, "cancelling must reset the block latch");

    // 4g. The state gate is the whole safety story: the panel byte alone, or
    //     the state alone, must not open the dialog.
    ResetSaveQueryDialog();
    ScreenReader::g_count = 0;
    *head = PlantModule(2, 0x36, SQ_PANEL_OVERWRITE, 1, 6, 0);   // the opening frame
    Tick(); PollSaveOverwriteDialog();
    check(ScreenReader::g_count == 0, "state 0x36 alone must not announce");
    *head = PlantModule(2, SQ_STATE_OVERWRITE, 5, 1, 6, 0);      // wrong panel
    Tick(); PollSaveOverwriteDialog();
    check(ScreenReader::g_count == 0, "the wrong panel must not announce");

    // 4h. An out-of-range cursor names nothing rather than guessing.
    ResetSaveQueryDialog();
    *head = PlantModule(2, SQ_STATE_OVERWRITE, SQ_PANEL_OVERWRITE, 9, 6, 0);
    Tick(); PollSaveOverwriteDialog();
    check(ScreenReader::g_count == 0, "an impossible cursor must not be named");

    // 4i. Null option pointers fall back to the literals, still in one sentence.
    ResetSaveQueryDialog();
    PlantText(SQ_PROMPT_PTR, nullptr);
    PlantText(SQ_OPT_PTR[1], nullptr);
    *head = PlantModule(2, SQ_STATE_OVERWRITE, SQ_PANEL_OVERWRITE, 1, 6, 0);
    Tick(); PollSaveOverwriteDialog();
    expect(ScreenReader::g_last, "Data exists. Overwrite? No.",
           "null text pointers fall back to the literals");

    printf("menu_save_query_compile: %s (%d bad)\n", bad ? "FAILED" : "OK", bad);
    return bad ? 1 : 0;
}
