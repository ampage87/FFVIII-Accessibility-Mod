// dragon_fight_compile.cpp -- Laguna vs. the dragon, checked against the bytes
// of tvglen3's own script and of the opcode handler the cue hooks.
//
//   g++ -std=c++17 -O0 -Isrc -o dragon_fight_compile tests/dragon_fight_compile.cpp
//
// The two claims this mini-game rests on are "the attack is ANIME(47,1) on
// channel 2, and the block flag is read after it" and "opcode 0x30 blocks until
// the animation ends". Both are decoded below out of the shipped data rather
// than restated: the script words come from `tvglen3.jsm`, the handler bytes
// from FF8_EN.exe.
//
// v0.43.0 (#105).

#include <cstdio>
#include <cstdint>
#include <cstring>

#include "dragon_fight_model.inl"
using namespace DragonFightModel;

static int bad = 0;
static void check(bool ok, const char* what)
{
    if (!ok) { std::printf("  BAD: %s\n", what); bad++; }
}
static void checkStr(const char* got, const char* want, const char* what)
{
    if (std::strcmp(got, want) != 0) {
        std::printf("  BAD: %s -- got \"%s\", want \"%s\"\n", what, got, want);
        bad++;
    }
}

// ===========================================================================
// The engine's instruction decoder, at FF8_EN.exe 0x00530760:
//     if ((w & 0xFF000000) == 0)  opcode = w & 0xFFFFFF, no operand
//     else                        opcode = w >> 24, operand = sign-extend24(w)
// ===========================================================================
struct Ins { int op; int par; bool hasPar; };
static Ins decode(uint32_t w)
{
    Ins i;
    if ((w & 0xFF000000u) == 0) { i.op = (int)(w & 0xFFFFFFu); i.par = 0; i.hasPar = false; return i; }
    i.op = (int)(w >> 24);
    int p = (int)(w & 0xFFFFFFu);
    if (p & 0x800000) p -= 0x1000000;
    i.par = p; i.hasPar = true;
    return i;
}

// JSM opcodes this probe names.
enum { OP_JMP = 0x02, OP_JPF = 0x03, OP_OPER = 0x01, OP_PSHN_L = 0x07,
       OP_PSHL = 0x08, OP_PSHM_B = 0x0A, OP_POPM_B = 0x0B,
       OP_BTN_HELD = 0x6D, OP_BTN_PRESSED = 0x6E, OP_REQ = 0x16, OP_REQSW = 0x14,
       OP_ANIME = 0x30, OP_GAUGE = 0x13B, OP_EQ = 6, OP_SUB = 1 };

// ---- tvglen3.jsm dwords 662..691 -- dragon::default, the whole fight -------
static const uint32_t DRAGON_DEFAULT[] = {
    0x000000E8, 0x08000000, 0x07000054, 0x01000009, 0x03000003, 0x07000020,
    0x000000E7, 0x0700002F, 0x07000001, 0x30000002, 0x0A000404, 0x07000000,
    0x01000006, 0x03000004, 0x07000006, 0x07000024, 0x14000007, 0x0700007F,
    0x0700007F, 0x07000001, 0x21000004, 0x07000062, 0x07000030, 0x30000002,
    0x07000010, 0x000000E7, 0x35000003, 0x02FFFFD2, 0x02FFFFCA, 0x1C000000
};
// ---- dwords 341..351 -- laguna::damage, forty off var[1031] ---------------
static const uint32_t LAGUNA_DAMAGE[] = {
    0x05000024, 0x07000001, 0x07000001, 0x000000A1, 0x0A000407, 0x07000028,
    0x01000001, 0x0B000407, 0x07000000, 0x0A000407, 0x0000013B
};
// ---- dwords 764..771 -- dragon::damage, twelve off var[1032] --------------
static const uint32_t DRAGON_DAMAGE[] = {
    0x0500003E, 0x0A000408, 0x0700000C, 0x01000001, 0x0B000408, 0x07000001,
    0x0A000408, 0x0000013B
};
// ---- dwords 396..406 -- laguna::damage, the retry reset -------------------
static const uint32_t RETRY_RESET[] = {
    0x1600000B, 0x07000078, 0x0B000407, 0x07000078, 0x0B000408, 0x07000000,
    0x0A000407, 0x0000013B, 0x07000001, 0x0A000408, 0x0000013B
};
// ---- dwords 1410..1428 -- hantei::check, the input loop -------------------
static const uint32_t HANTEI_CHECK[] = {
    0x05000052, 0x07000001, 0x03000010, 0x07000080, 0x0000006D, 0x08000000,
    0x03000004, 0x07000004, 0x07000025, 0x16000007, 0x07000010, 0x0000006E,
    0x08000000, 0x03000004, 0x07000004, 0x07000026, 0x16000007, 0x02FFFFF0,
    0x06000008
};

// ---- FF8_EN.exe 0x00526810 -- opcode 0x30's handler -----------------------
//     56                    push esi
//     8B 74 24 08           mov  esi, [esp+8]          ; ctx
//     B8 01 00 00 00        mov  eax, 1
//     8A 8E 74 01 00 00     mov  cl, [esi + 0x174]     ; which bit
//     D3 E0                 shl  eax, cl
//     8A 8E 75 01 00 00     mov  cl, [esi + 0x175]     ; the flag byte
//     84 C1                 test cl, al                ; FIRST EXECUTION?
//     74 47                 je   already_running
//     0F BE 96 84 01 00 00  movsx edx, byte [esi+0x184]; the VM stack pointer
//     66 8B 4C 96 FC        mov  cx, [esi + edx*4 - 4] ; the first argument
static const uint8_t ANIME_HANDLER[] = {
    0x56, 0x8B, 0x74, 0x24, 0x08, 0xB8, 0x01, 0x00, 0x00, 0x00, 0x8A, 0x8E,
    0x74, 0x01, 0x00, 0x00, 0xD3, 0xE0, 0x8A, 0x8E, 0x75, 0x01, 0x00, 0x00,
    0x84, 0xC1, 0x74, 0x47, 0x0F, 0xBE, 0x96, 0x84, 0x01, 0x00, 0x00, 0x66,
    0x8B, 0x4C, 0x96, 0xFC
};

static uint32_t le32(const uint8_t* p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static void HandlerBytes()
{
    // mov cl, [esi + disp32] -- twice, and both displacements are decoded.
    check(ANIME_HANDLER[10] == 0x8A && ANIME_HANDLER[11] == 0x8E &&
          (int)le32(ANIME_HANDLER + 12) == DF_VMCTX_FLAG_BIT,
          "**the handler reads the bit index from the offset the hook uses**");
    check(ANIME_HANDLER[18] == 0x8A && ANIME_HANDLER[19] == 0x8E &&
          (int)le32(ANIME_HANDLER + 20) == DF_VMCTX_FLAGS,
          "and the flag byte from the offset the hook uses");
    check(ANIME_HANDLER[16] == 0xD3 && ANIME_HANDLER[17] == 0xE0 &&
          ANIME_HANDLER[24] == 0x84 && ANIME_HANDLER[25] == 0xC1,
          "the test is `1 << bit` against that byte -- exactly what the hook does");
    check(ANIME_HANDLER[26] == 0x74,
          "**and it BRANCHES on it** -- start the animation, or poll one already "
          "running. That branch is what makes one attack one cue.");
    check(ANIME_HANDLER[28] == 0x0F && ANIME_HANDLER[29] == 0xBE &&
          ANIME_HANDLER[30] == 0x96 && (int)le32(ANIME_HANDLER + 31) == DF_VMCTX_SP,
          "the VM stack pointer is a SIGNED byte at the offset the hook uses");
    check(ANIME_HANDLER[35] == 0x66 && ANIME_HANDLER[36] == 0x8B &&
          ANIME_HANDLER[37] == 0x4C && ANIME_HANDLER[38] == 0x96 &&
          (int8_t)ANIME_HANDLER[39] == -4,
          "**and the first argument is at [ctx + sp*4 - 4]**, which is where the "
          "hook reads the animation id from");
}

static void ScriptWords()
{
    // ---- the attack, and the flag read AFTER it ---------------------------
    //
    // Walk dragon::default and find the ANIME whose arguments are the attack.
    int animeAt = -1;
    for (size_t i = 2; i < sizeof(DRAGON_DEFAULT) / 4; i++) {
        const Ins ins = decode(DRAGON_DEFAULT[i]);
        if (ins.op != OP_ANIME) continue;
        const Ins a = decode(DRAGON_DEFAULT[i - 2]);
        const Ins b = decode(DRAGON_DEFAULT[i - 1]);
        if (a.op != OP_PSHN_L || b.op != OP_PSHN_L) continue;
        if (DfIsAttackAnime(ins.par, a.par, b.par)) { animeAt = (int)i; break; }
    }
    check(animeAt >= 0,
          "**the attack signature the hook matches is really in dragon::default**");
    if (animeAt < 0) return;

    // Exactly one of them in this routine: two would make the cue ambiguous.
    int animeCount = 0;
    for (size_t i = 2; i < sizeof(DRAGON_DEFAULT) / 4; i++) {
        const Ins ins = decode(DRAGON_DEFAULT[i]);
        if (ins.op != OP_ANIME) continue;
        const Ins a = decode(DRAGON_DEFAULT[i - 2]);
        const Ins b = decode(DRAGON_DEFAULT[i - 1]);
        if (a.op == OP_PSHN_L && b.op == OP_PSHN_L &&
            DfIsAttackAnime(ins.par, a.par, b.par)) animeCount++;
    }
    check(animeCount == 1, "and only once in the loop");

    // The recovery is in the same routine, after the block check.
    int recovAt = -1;
    for (size_t i = 2; i < sizeof(DRAGON_DEFAULT) / 4; i++) {
        const Ins ins = decode(DRAGON_DEFAULT[i]);
        if (ins.op != OP_ANIME) continue;
        const Ins a = decode(DRAGON_DEFAULT[i - 2]);
        const Ins b = decode(DRAGON_DEFAULT[i - 1]);
        if (a.op != OP_PSHN_L || b.op != OP_PSHN_L) continue;
        if (DfIsRecoveryAnime(ins.par, a.par, b.par)) { recovAt = (int)i; break; }
    }
    check(recovAt > animeAt,
          "**the recovery animation follows the attack in the same routine** -- "
          "which is what makes it the attack's full stop rather than a guess");

    // v0.48.0: and it is exactly DF_RECOVER_IP_GAP dwords behind it. That gap
    // is what lets a recovery name WHICH attack it ends when two overlap, and
    // it is read out of the script here rather than copied off a log -- an
    // invented number could not survive this decode.
    check(recovAt - animeAt == DF_RECOVER_IP_GAP,
          "**the gap the recovery is matched by is the gap in the script** -- "
          "pairing by age charged a recovery to a cue four seconds older on "
          "2026-08-21 and got both outcomes wrong at once");

    // The very next instructions read the block flag and compare it with zero.
    const Ins v = decode(DRAGON_DEFAULT[animeAt + 1]);
    check(v.op == OP_PSHM_B && v.par == DF_VAR_BLOCKING,
          "**the instruction after the animation reads the BLOCK flag** -- which "
          "is why the window is the animation and the cue belongs at its start");
    check(decode(DRAGON_DEFAULT[animeAt + 2]).op == OP_PSHN_L &&
          decode(DRAGON_DEFAULT[animeAt + 2]).par == 0 &&
          decode(DRAGON_DEFAULT[animeAt + 3]).op == OP_OPER &&
          decode(DRAGON_DEFAULT[animeAt + 3]).par == OP_EQ,
          "compared against zero");
    check(decode(DRAGON_DEFAULT[animeAt + 4]).op == OP_JPF,
          "and branched on");
    // Not blocking -> REQ laguna::damage (script 36 on entity 7).
    check(decode(DRAGON_DEFAULT[animeAt + 6]).par == 36 &&
          decode(DRAGON_DEFAULT[animeAt + 7]).op == OP_REQSW &&
          decode(DRAGON_DEFAULT[animeAt + 7]).par == 7,
          "the unblocked branch runs laguna::damage");

    // ---- the health arithmetic -------------------------------------------
    check(decode(LAGUNA_DAMAGE[4]).op == OP_PSHM_B &&
          decode(LAGUNA_DAMAGE[4]).par == DF_VAR_LAGUNA_HP,
          "laguna::damage reads Laguna's health from the variable the model names");
    check(decode(LAGUNA_DAMAGE[5]).op == OP_PSHN_L &&
          decode(LAGUNA_DAMAGE[5]).par == DF_LAGUNA_HIT,
          "**and takes off exactly the amount the model divides by**");
    check(decode(LAGUNA_DAMAGE[6]).op == OP_OPER && decode(LAGUNA_DAMAGE[6]).par == OP_SUB &&
          decode(LAGUNA_DAMAGE[7]).op == OP_POPM_B &&
          decode(LAGUNA_DAMAGE[7]).par == DF_VAR_LAGUNA_HP,
          "writing it back");
    check(decode(LAGUNA_DAMAGE[10]).op == OP_GAUGE,
          "then redrawing the gauge -- the bar the player cannot read");

    check(decode(DRAGON_DAMAGE[1]).par == DF_VAR_DRAGON_HP &&
          decode(DRAGON_DAMAGE[2]).par == DF_DRAGON_HIT &&
          decode(DRAGON_DAMAGE[4]).op == OP_POPM_B &&
          decode(DRAGON_DAMAGE[4]).par == DF_VAR_DRAGON_HP,
          "and the dragon loses its own, smaller amount");

    // ---- the retry writes both back to full --------------------------------
    check(decode(RETRY_RESET[1]).par == DF_HP_FULL &&
          decode(RETRY_RESET[2]).par == DF_VAR_LAGUNA_HP &&
          decode(RETRY_RESET[3]).par == DF_HP_FULL &&
          decode(RETRY_RESET[4]).par == DF_VAR_DRAGON_HP,
          "**both sides start at the same 120**, which is what makes 3 and 10 the "
          "hit counts rather than a guess");

    // ---- the two buttons ---------------------------------------------------
    check(decode(HANTEI_CHECK[3]).par == (int)DF_MASK_BLOCK &&
          decode(HANTEI_CHECK[4]).op == OP_BTN_HELD,
          "**block is mask 0x80 tested as a LEVEL** -- holding works, and it is "
          "the mask the key names are looked up by");
    check(decode(HANTEI_CHECK[8]).par == 37 && decode(HANTEI_CHECK[9]).op == OP_REQ,
          "and it runs laguna::bougyo");
    check(decode(HANTEI_CHECK[10]).par == (int)DF_MASK_ATTACK &&
          decode(HANTEI_CHECK[11]).op == OP_BTN_PRESSED,
          "**attack is mask 0x10 tested as an EDGE** -- it has to be tapped");
    check(decode(HANTEI_CHECK[15]).par == 38 && decode(HANTEI_CHECK[16]).op == OP_REQ,
          "and it runs laguna::kougeki");
}

int main()
{
    HandlerBytes();
    ScriptWords();

    // ---- what it says ------------------------------------------------------
    char buf[192];

    check(DfHitsLeft(120, DF_LAGUNA_HIT) == 3, "Laguna survives three hits");
    check(DfHitsLeft(120, DF_DRAGON_HIT) == 10, "the dragon takes ten");
    check(DfHitsLeft(80, DF_LAGUNA_HIT) == 2 && DfHitsLeft(40, DF_LAGUNA_HIT) == 1,
          "and the count comes down one at a time");
    check(DfHitsLeft(0, DF_LAGUNA_HIT) == 0, "zero is down");
    check(DfHitsLeft(1, DF_DRAGON_HIT) == 1,
          "**a remainder is still a hit** -- rounding down would say the fight "
          "is over while it is still on");

    DfHealthLine(true, 120, buf, sizeof(buf));
    checkStr(buf, "Laguna, 3 hits left.", "Laguna at full");
    DfHealthLine(true, 40, buf, sizeof(buf));
    checkStr(buf, "Laguna, one hit left.", "the last hit is worth saying in words");
    DfHealthLine(true, 0, buf, sizeof(buf));
    checkStr(buf, "Laguna down.", "and zero is not `0 hits left`");
    DfHealthLine(false, 96, buf, sizeof(buf));
    checkStr(buf, "Dragon, 8 hits left.", "the dragon after one hit");

    DfStatusLine(80, 96, true, buf, sizeof(buf));
    checkStr(buf, "Laguna 2 hits left. Dragon 8 hits left. Block held.",
             "the slash readout");
    DfStatusLine(80, 96, false, buf, sizeof(buf));
    checkStr(buf, "Laguna 2 hits left. Dragon 8 hits left. Block not held.",
             "and it says when the block is NOT held, which is the mistake to catch");

    // ---- the variable addresses --------------------------------------------
    check(DfVarAddr(DF_VAR_LAGUNA_HP) == 0x01CFEDBFu &&
          DfVarAddr(DF_VAR_DRAGON_HP) == 0x01CFEDC0u &&
          DfVarAddr(DF_VAR_BLOCKING)  == 0x01CFEDBCu &&
          DfVarAddr(DF_VAR_ACTIVE)    == 0x01CFEDBEu,
          "the four variables resolve where the field variable block puts them");

    check(!DfIsAttackAnime(2, 98, 48), "the recovery animation is not the attack");
    check(DfIsRecoveryAnime(2, 98, 48), "**but it IS the recovery**");
    check(!DfIsRecoveryAnime(2, 47, 1) && !DfIsRecoveryAnime(5, 98, 48),
          "and nothing else on this field is");
    check(DF_RESOLVE_MS > 0 && DF_RESOLVE_MS <= 2000,
          "the beat the damage REQ gets after the recovery is short and bounded");
    check(DF_CUE_MAX_MS > DF_OUTCOME_MS_V * 2,
          "**the backstop is far longer than any wind-up** -- a cue the player "
          "interrupted took nearly four seconds and a shorter one called it "
          "blocked while it was still swinging");
    check(!DfIsAttackAnime(4, 47, 1),  "and neither is animation 47 on another channel");

    // ---- when the fight is real (v0.44.0) ----------------------------------
    //
    // The first BAT logged `laguna=216 dragon=0` before the scene wrote its
    // health, and v0.43.0 took the 216 as a starting point -- then reported the
    // drop to 120 as a HIT, and rang the cue 47 seconds before the first bar
    // was drawn.
    check(!DfHpValid(216), "**216 is not a health value** -- that is the byte "
                           "before the scene has written it");
    check(DfHpValid(0) && DfHpValid(120), "0 and 120 are");
    check(!DfFightReady(216, 0, 1),
          "so the fight is not ready while those bytes are still garbage");
    check(!DfFightReady(120, 120, 0), "nor while var[1030] says it is not running");
    check(DfFightReady(120, 120, 1) && DfFightReady(40, 96, 1),
          "and it is ready once both are in range and it is running");

    check(DfFightStarting(120, 120, 1),
          "**both bars at full is the scene handing control over** -- what "
          "dic::defalut writes to start it and laguna::damage writes on a retry");
    check(!DfFightStarting(80, 120, 1) && !DfFightStarting(120, 108, 1),
          "and a fight in progress is not a fight starting");

    // The measured window has to fit inside the deadline that calls a miss a
    // block, or every long wind-up would be announced as blocked and then hit.
    check(DF_OUTCOME_MS_V > 1610,
          "**the outcome deadline clears the longest measured cue-to-hit** "
          "(828 ms and 1610 ms, 2026-08-21)");

    // ---- the briefing may not hang the fight (v0.46.0) ---------------------
    //
    // The 2026-08-21 log: the retry box stayed open for seventy seconds with the
    // guard pinned and every cue swallowed. Two constants stop that from being
    // possible again, and both have to hold.
    check(DF_BRIEF_TIMEOUT_MS > 0,
          "**the Game Controls box has a timeout at all** -- without one it can "
          "hold the guard pinned and swallow every cue indefinitely");
    check(DF_BRIEF_TIMEOUT_MS >= 20000,
          "and it is long enough to read the box and try the keys");
    check(DF_GRACE_MS >= 1610,
          "**the post-box grace covers a full wind-up** -- an attack that started "
          "behind the box must not land on a player who heard no cue for it "
          "(the `hit with no cue outstanding` at 20:51:38)");
    check(DF_GRACE_MS <= DF_OUTCOME_MS_V,
          "but no longer than one outcome window, so the fight is not held soft");

    // Both attack sites fire. The 2026-08-21 log shows the hook catching
    // ip=531 and ip=605, seventy-four dwords apart -- exactly the gap between
    // dragon::default's ANIME (offline dword 671) and dragon::kougeki's (745).
    check(745 - 671 == 605 - 531,
          "**the two runtime attack sites are the two scripts that attack** -- "
          "the offline gap and the logged gap are the same seventy-four dwords");

    // And the SAME recovery gap holds in dragon::kougeki, so one constant
    // serves both attack sites: 545 - 531 and 619 - 605 are both fourteen, the
    // two pairs the 2026-08-21 log caught the hook on.
    check(545 - 531 == DF_RECOVER_IP_GAP && 619 - 605 == DF_RECOVER_IP_GAP,
          "**both runtime attack/recovery pairs are that same gap apart** -- "
          "which is why one constant matches a recovery to its own attack");
    check(685 - 671 == DF_RECOVER_IP_GAP && 759 - 745 == DF_RECOVER_IP_GAP,
          "and so are the offline dwords they were found at");

    // ---- the queue, replayed against the log it got wrong (v0.48.0) --------
    //
    // Two sequences straight out of ff8_field.log, 2026-08-21. Neither is a
    // hypothetical: the first is the round Laguna lost, the second is the pair
    // of overlapping attacks that both resolved against the wrong cue.
    {
        // 21:41:38 quick attack -- 21:41:39 its recovery -- 21:41:40 the NEXT
        // quick attack -- and one beat later v0.47.0 said "Blocked." into it.
        DfCueClear();
        DfCuePush(1000, true, 531);
        bool named = false;
        int c = DfCueForRecovery(545, &named);
        check(c >= 0 && named && s_cues[c].ip == 531,
              "**a recovery names its own attack by the script gap**");
        s_cues[c].resolveAt = 2000 + DF_RESOLVE_MS;

        DfCuePush(3000, true, 531);          // 21:41:40, the second wind-up
        check(DfCueNewerLive(c),
              "**and a block that resolves under a live cue is superseded** -- "
              "saying it is what put Laguna down on 2026-08-21");

        s_cues[c].at = 0; s_cues[c].resolveAt = 0; s_cues[c].ip = 0;
        const int c2 = DfCueForRecovery(545, &named);
        check(c2 >= 0 && named && s_cues[c2].at == 3000,
              "the second wind-up then takes its own recovery");
        s_cues[c2].resolveAt = 3900;
        check(!DfCueNewerLive(c2),
              "and nothing newer is in the air behind it");
        check(DfCueTakeForHit() == 3000,
              "**so the hit is charged to the attack that landed**, not to one "
              "that resolved seconds earlier");
    }
    {
        // 21:42:00 a normal attack (ip 531) -- 21:42:03 a quick one (ip 605) --
        // 21:42:04 recovery ip=619. v0.47.0 charged that to the older cue and
        // logged `hit 3969 ms after the cue`; it belongs to the quick one.
        DfCueClear();
        DfCuePush(1000, false, 531);
        DfCuePush(4000, true,  605);
        bool named = false;
        const int c = DfCueForRecovery(619, &named);
        check(c >= 0 && named && s_cues[c].at == 4000,
              "**the kougeki recovery goes to the kougeki attack** -- pairing by "
              "age put it on a cue three seconds older");
        s_cues[c].resolveAt = 5000;
        check(DfCueTakeForHit() == 4000,
              "and the health drop follows it there");

        const int c2 = DfCueForRecovery(545, &named);
        check(c2 >= 0 && named && s_cues[c2].at == 1000,
              "leaving the default attack to be ended by its own recovery, "
              "twelve seconds later");
    }
    {
        // A recovery whose gap names nothing still has to resolve something --
        // a silent queue is worse than an approximate one, and the fallback
        // announces itself in the log.
        DfCueClear();
        DfCuePush(1000, false, 999);
        bool named = true;
        const int c = DfCueForRecovery(545, &named);
        check(c >= 0 && !named,
              "**an unrecognised gap falls back to oldest-first and says so** -- "
              "a signature change must not look like working code");
        DfCueClear();
        check(DfCueForRecovery(545, &named) < 0 && DfCueTakeForHit() == 0,
              "and an empty queue resolves nothing at all");
    }

    // =======================================================================
    // THE GAME CONTROLS BOX MUST FIT THE BOX.
    // =======================================================================
    // FF8's window measurer sizes the box to the text and its own clamps stop
    // there: past ~34 columns a line wraps, and past ten rows the box scrolls --
    // 16 frames a scroll, and the opening lines gone by the time anyone reads
    // it. This is the check that a future edit to the wording cannot quietly
    // push it over.
    {
        char screen[256];
        // The longest key names the learner can produce are the button words,
        // and "?" is what it writes when it has not learned one yet. Measure
        // with something wider than either so the check has headroom.
        std::snprintf(screen, sizeof(screen), DF_SCREEN_FMT, "Space", "Space");
        int cols = 0, lines = 0;
        DfScreenMeasure(screen, &cols, &lines);
        std::printf("  [controls box] %d cols x %d lines\n", cols, lines);
        check(cols <= DF_SCREEN_COLS,
              "**no line of the controls box is wider than the window** -- FF8's "
              "measurer wraps past about 34 columns and the wrap lands where the "
              "wording did not expect it");
        check(lines <= DF_SCREEN_LINES,
              "**and it is no taller than the box** -- a screen that scrolls has "
              "lost its first lines before the player gets to them");
        // ...and with the narrowest names too, because the check is about the
        // format, not about one substitution.
        std::snprintf(screen, sizeof(screen), DF_SCREEN_FMT, "W", "X");
        DfScreenMeasure(screen, &cols, &lines);
        check(cols <= DF_SCREEN_COLS && lines <= DF_SCREEN_LINES,
              "fits with short key names as well");
        check(std::strstr(screen, "DRAGON FIGHT") != nullptr &&
              std::strstr(screen, "F9 skip") != nullptr,
              "and it still says what it is and how to leave it");
    }

    std::printf(bad ? "dragon_fight_compile: FAILED (%d bad)\n"
                    : "dragon_fight_compile: OK (%d bad)\n", bad);
    return bad ? 1 : 0;
}
