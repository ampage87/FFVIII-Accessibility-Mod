// repeat_prompt_compile.cpp -- position within a run of identical choice
// dialogs, driven with the Missile Base password's own shape.
//
//   g++ -std=c++17 -O0 -Isrc -o repeat_prompt_compile tests/repeat_prompt_compile.cpp
//
// The script at `gmtika4` dwords 2189-2320 asks four times on one message and
// checks 4, 3, 4, 0 over A-F -- E, D, E, A. Its four AASKs are four SEPARATE
// instructions, at dword indices 2223, 2240, 2257 and 2274.
//
// v0.42.0 (#103): the counter keys on the INSTRUCTION POINTER, because the
// v0.41.0 assumption -- one opcode firing per question -- is false, and the BAT
// showed it counting frames: forty "Letter N of 4" lines in three seconds
// without a letter being entered. Part 1 below decodes the engine fragments
// that settle it; part 2 drives the run.
//
// v0.41.0 (#102), v0.42.0 (#103).

#include <cstdio>
#include <cstdint>
#include <cstring>
#include <string>

// The two files under test are written for the game; give them the three things
// they touch. These are INPUTS, not reimplementations -- the logic being checked
// is entirely inside the .inl files.
#define FF8_URGENT_CUE_HOST_TEST 1
#define FF8_REPEAT_PROMPT_HOST_TEST 1
namespace Log { static void Dialog(const char*, ...) {} }
namespace FF8Addresses { static char* pCurrentFieldName = nullptr; }
static void SetField(const char* f) { FF8Addresses::pCurrentFieldName = (char*)f; }

#include "field_urgent_prompt.inl"     // UrgentSameName
#include "field_repeat_prompt.inl"

static int bad = 0;
static void check(bool ok, const char* what)
{
    if (!ok) { std::printf("  BAD: %s\n", what); bad++; }
}
static void checkPrefix(const char* want, const char* what)
{
    std::string got = RepeatPromptTakePrefix();
    if (got != want) {
        std::printf("  BAD: %s -- got \"%s\", want \"%s\"\n", what, got.c_str(), want);
        bad++;
    }
}

// ===========================================================================
// PART 1 -- the engine's own bytes
// ===========================================================================
//
// Three fragments of FF8_EN.exe, copied byte for byte. Together they say that a
// field opcode which has not finished re-runs from the SAME instruction, which
// is the entire premise of keying on the IP.

// 0x0052A621: the dispatcher loading the IP before it calls the handler.
//     66 8B 8E 76 01 00 00    mov cx, word ptr [esi + 0x176]
static const uint8_t DISPATCH_LOAD_IP[] = {
    0x66, 0x8B, 0x8E, 0x76, 0x01, 0x00, 0x00
};

// 0x0052A671: what it does with the handler's return value.
//     A8 02                   test al, 2
//     74 19                   je   +0x19            <- skip the increment
//     66 FF 86 76 01 00 00    inc  word ptr [esi + 0x176]
static const uint8_t DISPATCH_ADVANCE[] = {
    0xA8, 0x02, 0x74, 0x19, 0x66, 0xFF, 0x86, 0x76, 0x01, 0x00, 0x00
};

// 0x00529749: AASK's waiting path, out of the handler at execute_opcode_table
// [0x6F] = 0x005296C0.
//     ...
//     74 0C                   je   +0x0C
//     5F 5E                   pop  edi / pop esi
//     B8 05 00 00 00          mov  eax, 5
//     5B                      pop  ebx
//     83 C4 18                add  esp, 0x18
//     C3                      ret
static const uint8_t AASK_WAIT_RETURN[] = {
    0x8B, 0xCF, 0xD3, 0xE2, 0x84, 0x90, 0xD2, 0x00, 0x00, 0x00, 0x74, 0x0C,
    0x5F, 0x5E, 0xB8, 0x05, 0x00, 0x00, 0x00, 0x5B, 0x83, 0xC4, 0x18, 0xC3
};

static uint32_t le32(const uint8_t* p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

// `66 8B 8E disp32` = mov cx, [esi + disp32]; `66 FF 86 disp32` = inc word
// ptr [esi + disp32]. Both displacements are decoded, never assumed.
static bool DecodeEsiDisp(const uint8_t* p, size_t n, const uint8_t* opBytes,
                          size_t opLen, uint32_t* disp)
{
    if (n < opLen + 4) return false;
    if (std::memcmp(p, opBytes, opLen) != 0) return false;
    *disp = le32(p + opLen);
    return true;
}

static void EngineFragments()
{
    static const uint8_t MOV_CX_ESI[] = { 0x66, 0x8B, 0x8E };
    static const uint8_t INC_W_ESI[]  = { 0x66, 0xFF, 0x86 };

    uint32_t dispLoad = 0;
    check(DecodeEsiDisp(DISPATCH_LOAD_IP, sizeof(DISPATCH_LOAD_IP),
                        MOV_CX_ESI, sizeof(MOV_CX_ESI), &dispLoad),
          "0x0052A621 decodes as mov cx,[esi+disp32]");
    check(dispLoad == REPEAT_PROMPT_IP_OFFSET,
          "**the dispatcher reads the IP from the offset this file uses**");

    // test al,2 / je / inc word [esi+disp32] -- the increment is CONDITIONAL,
    // and on the same field the load used.
    check(DISPATCH_ADVANCE[0] == 0xA8 && DISPATCH_ADVANCE[1] == 0x02,
          "the advance is gated on `test al, 2`");
    check(DISPATCH_ADVANCE[2] == 0x74,
          "**and it is SKIPPED when that bit is clear** (je), which is what "
          "lets an opcode re-run from where it is");
    uint32_t dispInc = 0;
    check(DecodeEsiDisp(DISPATCH_ADVANCE + 4, sizeof(DISPATCH_ADVANCE) - 4,
                        INC_W_ESI, sizeof(INC_W_ESI), &dispInc),
          "0x0052A675 decodes as inc word ptr [esi+disp32]");
    check(dispInc == dispLoad,
          "the IP that is advanced is the IP that was read");

    // AASK's waiting return: find `B8 imm32` (mov eax, imm32) followed by the
    // epilogue, and check bit 1 of that immediate.
    uint32_t ret = 0xFFFFFFFF;
    for (size_t i = 0; i + 5 <= sizeof(AASK_WAIT_RETURN); i++) {
        if (AASK_WAIT_RETURN[i] == 0xB8) { ret = le32(AASK_WAIT_RETURN + i + 1); break; }
    }
    check(ret == 5, "AASK's waiting path returns 5");
    check((ret & 2) == 0,
          "**and 5 has bit 1 clear, so the IP is NOT advanced** -- the same "
          "AASK fires again next frame, which is exactly what v0.41.0 counted");
    check(AASK_WAIT_RETURN[sizeof(AASK_WAIT_RETURN) - 1] == 0xC3,
          "that path returns immediately (ret)");
}

// ===========================================================================
// PART 2 -- the run
// ===========================================================================
//
// Two fake VM contexts. The IP goes in at the LITERAL offset 0x176: a fixture
// that wrote it through REPEAT_PROMPT_IP_OFFSET would agree with the constant
// under test by construction, which is the mistake battle_limit_compile was
// built to stop repeating.
static uint8_t g_ctxA[0x400];
static uint8_t g_ctxB[0x400];

static void Fire(uint8_t* ctx, unsigned ip, const std::string& prompt)
{
    *(uint16_t*)(ctx + 0x176) = (uint16_t)ip;     // literal, on purpose
    RepeatPromptOnOpcode(prompt, (uintptr_t)ctx);
}

int main()
{
    EngineFragments();

    const std::string PW    = "\"Please enter your password.\"";
    const std::string OTHER = "\"Is this what you were talking about?\"";

    // gmtika4's four AASK instructions, in script order.
    const unsigned L1 = 2223, L2 = 2240, L3 = 2257, L4 = 2274;

    // ---- the run, on the right field --------------------------------------
    SetField("gmtika4");
    Fire(g_ctxA, L1, PW); checkPrefix("Letter 1 of 4. ", "first prompt");

    // THE BUG. AASK re-runs every frame while the player is choosing; those
    // firings are the same question and must produce nothing at all.
    for (int f = 0; f < 40; f++) {
        Fire(g_ctxA, L1, PW);
        check(!RepeatPromptHasPrefix(),
              "**a re-run of the SAME instruction says nothing** -- v0.41.0 "
              "counted these and cycled 1,2,3,4 while nothing was entered");
        (void)RepeatPromptTakePrefix();
    }

    Fire(g_ctxA, L2, PW); checkPrefix("Letter 2 of 4. ", "second prompt");
    for (int f = 0; f < 5; f++) { Fire(g_ctxA, L2, PW); (void)RepeatPromptTakePrefix(); }
    Fire(g_ctxA, L3, PW); checkPrefix("Letter 3 of 4. ", "third prompt");
    Fire(g_ctxA, L4, PW); checkPrefix("Letter 4 of 4. ", "fourth prompt");

    // A wrong password jumps back to the first AASK -- the same instruction as
    // letter one. The count must restart, not run on to "Letter 5 of 4".
    Fire(g_ctxA, L1, PW);
    checkPrefix("Letter 1 of 4. ", "**a second attempt counts from one again**");
    Fire(g_ctxA, L2, PW); checkPrefix("Letter 2 of 4. ", "and carries on from there");

    // ---- taking the prefix consumes it ------------------------------------
    Fire(g_ctxA, L3, PW);
    check(RepeatPromptHasPrefix(), "a prefix is pending after a new site");
    (void)RepeatPromptTakePrefix();
    check(!RepeatPromptHasPrefix(), "**and taking it consumes it** -- it can never attach twice");
    checkPrefix("", "a second take yields nothing");

    // ---- a different entity at the same IP is a different site ------------
    RepeatPromptReset();
    Fire(g_ctxA, L1, PW); checkPrefix("Letter 1 of 4. ", "context A, first site");
    Fire(g_ctxB, L1, PW);
    checkPrefix("Letter 2 of 4. ",
        "**the same IP in another script context is not the same question**");

    // ---- everything else in the game is untouched -------------------------
    RepeatPromptReset();
    Fire(g_ctxA, L1, OTHER);
    checkPrefix("", "an unrelated prompt on the same field gets no prefix");
    Fire(g_ctxA, L1, PW); checkPrefix("Letter 1 of 4. ",
        "and an unrelated prompt in between resets the run");

    SetField("gmcont1");
    Fire(g_ctxA, L2, PW);
    checkPrefix("", "**the same prompt on another field gets nothing** -- the cue is field-scoped");
    SetField("gmtika4");
    Fire(g_ctxA, L1, PW); checkPrefix("Letter 1 of 4. ", "and leaving the field ended the run");

    SetField(nullptr);
    Fire(g_ctxA, L2, PW); checkPrefix("", "no field name, no prefix");

    // No context at all -- the poll rescan's case. It has no IP to offer, so it
    // must not be able to advance a run.
    SetField("gmtika4");
    RepeatPromptReset();
    RepeatPromptOnOpcode(PW, 0);
    checkPrefix("", "**a call with no VM context counts nothing** (the poll rescan)");

    // ---- matching ---------------------------------------------------------
    RepeatPromptReset();
    Fire(g_ctxA, L1, "PLEASE ENTER YOUR PASSWORD.");
    checkPrefix("Letter 1 of 4. ", "the prompt match ignores case");
    Fire(g_ctxA, L2, "");
    checkPrefix("", "an empty prompt matches nothing");
    Fire(g_ctxA, L2, "passwor");
    checkPrefix("", "and a truncated word is not a match");

    // ---- more sites than the script asks for ------------------------------
    RepeatPromptReset();
    Fire(g_ctxA, L1, PW); (void)RepeatPromptTakePrefix();
    Fire(g_ctxA, L2, PW); (void)RepeatPromptTakePrefix();
    Fire(g_ctxA, L3, PW); (void)RepeatPromptTakePrefix();
    Fire(g_ctxA, L4, PW); (void)RepeatPromptTakePrefix();
    Fire(g_ctxA, 9999, PW);
    checkPrefix("Letter 1 of 4. ",
        "a fifth distinct site restarts the run rather than saying 5 of 4");

    std::printf(bad ? "repeat_prompt_compile: FAILED (%d bad)\n"
                    : "repeat_prompt_compile: OK (%d bad)\n", bad);
    return bad ? 1 : 0;
}
