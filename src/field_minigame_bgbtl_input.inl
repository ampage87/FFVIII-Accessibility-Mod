// field_minigame_bgbtl_input.inl -- v0.20.128 (#minigame-bgbtl)
//
// The button word the FIGHT actually reads, the key names for it, and the
// hold-to-block assist. Included from field_minigame_bgbtl.inl; part of the
// FieldNavigation namespace. Do not compile independently.
//
// ---------------------------------------------------------------------------
// WHY THIS FILE EXISTS
//
// Every earlier version of this module watched the WRONG WORD: it read
// pEngineInputValidButtons (0x01CD01F8) and pEngineInputConfirmedButtons
// (0x01CD0200). The field script reads NEITHER.
//
// Opcode 109 -- the only instruction in the mini-game that looks at input --
// is handler 0x0051DA50, and its second instruction is:
//
//     0051DA55  mov  esi, dword ptr [0x01CE48B0]
//     0051DA64  mov  edx, [ecx + sp*4]        ; TOS = the mask
//     0051DA67  and  edx, esi
//     0051DA76  mov  dword ptr [ecx+0x140], 1 ; local[0] = 1  (pressed)
//     0051DA86  mov  dword ptr [ecx+0x140], 0 ; local[0] = 0  (not pressed)
//
// So the fight tests **0x01CE48B0**, a LEVEL, with no edge detection and no
// consumption. Two consequences, both of which contradict what this module has
// been telling Aaron:
//
//   1. HOLDING the block key works. There is no tap requirement anywhere in
//      the instruction. v0.20.118's briefing said the opposite.
//   2. The word is written inside field_main (0x00476888, in the routine at
//      0x004767B0 that field_main calls at 0x0047246E). The briefing no longer
//      freezes field_main -- it vetoes the soldier's attacks instead -- so the
//      key learner reads this same live word the fight reads.
//
// ---------------------------------------------------------------------------
// THE MASKS, AND WHICH ONE IS THE BLOCK
//
// squall::squ_punchkeyscan0 -- bg2f_31 label 12, bgbtl_1 label 28 -- is one
// tight loop of four BTNTESTs. Decoded (both fields agree instruction for
// instruction, only the label numbers differ):
//
//     PUSHI 16   BTNTEST -> REQ squall::squ_punching0
//     PUSHI 64   BTNTEST -> REQ squall::squ_kicking0
//     PUSHI 128  BTNTEST -> REQ squall::squ_guarding0     <-- THE BLOCK
//     if var340 >= 3:
//     PUSHI 32   BTNTEST -> REQ squall::squ_punching1     <-- the heavy punch
//     JMP -53 (back to the top)
//
// **THE BLOCK IS MASK 128.** Aaron's 2026-08-15 log proves which key that is:
// every diagnostic line reading A=1 paired with a button word of 0x0080, and
// every X=1 line with 0x0040. He had been pressing the right key the entire
// time -- this module was watching the wrong flag and told him he had blocked
// nothing.
static const uintptr_t FIELD_BUTTONS = 0x01CE48B0;  // live, level

static const uint32_t BTN_PUNCH = 16;
static const uint32_t BTN_KICK  = 64;
static const uint32_t BTN_BLOCK = 128;
static const uint32_t BTN_HEAVY = 32;
static const uint32_t BTN_ALL   = BTN_PUNCH | BTN_KICK | BTN_BLOCK | BTN_HEAVY;

// squall::squ_guarding0 sets this to 1, waits 20 frames, sets it back to 0.
// squ_hpcalc0 -- the damage routine -- reads it and NOTHING ELSE to decide the
// band. Different byte in each field; both verified in the scripts.
static const int GUARD_VAR_HOST     = 1030;   // bg2f_31
static const int GUARD_VAR_MINIGAME = 1028;   // bgbtl_1

// squall::squ_punched_up0 increments this on a blocked hit and ZEROES it on an
// unblocked one. The keyscan gates the heavy punch on var340 >= 3.
static const int STREAK_VAR = 340;

static int GuardVarFor(uint16_t field)
{
    return (field == FIELD_MINIGAME) ? GUARD_VAR_MINIGAME : GUARD_VAR_HOST;
}

// --------------------------------------------------------------- reading

static uint32_t ReadButtons()
{
    __try { return *(volatile uint32_t*)FIELD_BUTTONS; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}

static int ReadVarB(int off, bool* ok)
{
    __try { if (ok) *ok = true; return *(const uint8_t*)(VARBLOCK_BASE + off); }
    __except (EXCEPTION_EXECUTE_HANDLER) { if (ok) *ok = false; return -1; }
}

// THE DISPLAYED HP BARS, WHICH ARE NOT THE HP VARIABLES.
//
// Aaron's 15:26:42 screenshot, taken after F9: Squall alone on the wire and
// **the Galbadian Soldier's bar still full red** -- while the mod had just said
// "You win." The bars are a separate thing from var 354 / var 356. Opcode 315
// (0x00529BF0) is what moves them:
//
//     shl esi, 4                       ; esi = gauge id
//     mov word ptr [esi + 0x1D9CF5C], di
//
// and the scripts call it as `PUSHI 0, RDVARSW 354, op315` for Squall and
// `PUSHI 1, RDVARSW 356, op315` for the soldier -- but only from inside
// squ_hpcalc0 / gal_hpcalc0, which is to say only when a punch actually lands.
// The skip writes the variables directly, so the bars never hear about it.
//
// One word each keeps the picture honest for anyone watching.
static const uintptr_t HP_GAUGE_BASE = 0x01D9CF5C;
static const int GAUGE_SQUALL = 0;
static const int GAUGE_FOE    = 1;

static void SetHpGauge(int gaugeId, int16_t value)
{
    __try { *(int16_t*)(HP_GAUGE_BASE + (uintptr_t)(gaugeId << 4)) = value; }
    __except (EXCEPTION_EXECUTE_HANDLER) { }
}

static void WriteVarB(int off, uint8_t v)
{
    __try { *(uint8_t*)(VARBLOCK_BASE + off) = v; }
    __except (EXCEPTION_EXECUTE_HANDLER) { }
}

// --------------------------------------------------------------- naming keys
//
// The mod must be able to SAY which key throws the heavy punch, and it cannot
// know that from the exe: the four masks are pad bits, and the keyboard mapping
// lives in the 2013 wrapper's own config, not in FF8_EN.exe's remap table
// (0x01CFE740, which 0x004A2D60 only consults when [0x01CFE73C] & 0x20 -- clear
// on Aaron's machine, hence the identity mapping the log shows).
//
// So it learns them instead: whenever a mask bit RISES in the field button
// word, whatever candidate keys went down on the same tick are the candidates
// for that bit, and repeated presses intersect down to one. Costs nothing,
// works for a remapped keyboard or a gamepad, and needs no BAT to establish.

static const int KEY_CANDIDATES[] = {
    'A','B','C','D','E','F','G','H','I','J','K','L','M',
    'N','O','P','Q','R','S','T','U','V','W','X','Y','Z',
    '0','1','2','3','4','5','6','7','8','9',
    VK_SPACE, VK_RETURN, VK_LCONTROL, VK_RCONTROL, VK_LSHIFT, VK_RSHIFT,
    VK_LMENU, VK_RMENU, VK_TAB, VK_BACK, VK_OEM_COMMA, VK_OEM_PERIOD,
    VK_OEM_1, VK_OEM_2, VK_OEM_3, VK_OEM_4, VK_OEM_6, VK_OEM_7,
    VK_NUMPAD0, VK_NUMPAD1, VK_NUMPAD2, VK_NUMPAD3, VK_NUMPAD4,
    VK_NUMPAD5, VK_NUMPAD6, VK_NUMPAD7, VK_NUMPAD8, VK_NUMPAD9,
    VK_INSERT, VK_DELETE, VK_HOME, VK_END, VK_PRIOR, VK_NEXT,
};
static const int KEY_CANDIDATE_COUNT =
    (int)(sizeof(KEY_CANDIDATES) / sizeof(KEY_CANDIDATES[0]));

// One slot per mask bit, in the order punch/kick/block/heavy.
//
// v0.20.123: **SEEDED, NOT EMPTY.** The first "Heavy punch ready" of a session
// read "(key unknown)" if the player had not happened to tap that key during
// the briefing's calibration -- 14:32:33 in the 2026-08-15 log said it, and
// 14:32:40 said "D" once he had. The call to action is the one announcement
// that decides the fight, so it should never be the vague version.
//
// The seeds are MEASURED, not assumed: across two runs on Aaron's machine every
// press of W paired with mask 16, X with 64, A with 128 and D with 32 on
// 0x01CE48B0 -- and those are FF8 PC's stock bindings. They are deliberately
// left UNLOCKED, so the very first real press on a differently-configured
// machine overwrites the seed rather than arguing with it.
struct LearnedKey { uint32_t mask; int vk; int hits; bool locked; };
static LearnedKey s_learned[4] = {
    { BTN_PUNCH, 'W', 0, false },
    { BTN_KICK,  'X', 0, false },
    { BTN_BLOCK, 'A', 0, false },
    { BTN_HEAVY, 'D', 0, false },
};
static uint32_t s_btnPrev = 0;

static LearnedKey* SlotFor(uint32_t mask)
{
    for (int i = 0; i < 4; i++) if (s_learned[i].mask == mask) return &s_learned[i];
    return nullptr;
}

// Four rotating buffers, not one, because the disarm summary prints all four
// learned keys in a single Log::Field call -- with one static buffer every one
// of them would read as whichever was formatted last.
static const char* KeyName(int vk)
{
    static char pool[4][24];
    static int  slot = 0;
    char* buf = pool[slot]; slot = (slot + 1) & 3;
    if (vk == 0) return nullptr;
    if (vk >= 'A' && vk <= 'Z') { buf[0] = (char)vk; buf[1] = 0; return buf; }
    if (vk >= '0' && vk <= '9') { buf[0] = (char)vk; buf[1] = 0; return buf; }
    switch (vk) {
        case VK_SPACE:    return "Space";
        case VK_RETURN:   return "Enter";
        case VK_LCONTROL: case VK_RCONTROL: return "Control";
        case VK_LSHIFT:   case VK_RSHIFT:   return "Shift";
        case VK_LMENU:    case VK_RMENU:    return "Alt";
        case VK_TAB:      return "Tab";
        case VK_BACK:     return "Backspace";
        case VK_INSERT:   return "Insert";
        case VK_DELETE:   return "Delete";
        case VK_HOME:     return "Home";
        case VK_END:      return "End";
        case VK_PRIOR:    return "Page up";
        case VK_NEXT:     return "Page down";
        default: break;
    }
    if (vk >= VK_NUMPAD0 && vk <= VK_NUMPAD9) {
        buf[0] = 'N'; buf[1] = 'u'; buf[2] = 'm'; buf[3] = ' ';
        buf[4] = (char)('0' + (vk - VK_NUMPAD0)); buf[5] = 0;
        return buf;
    }
    return nullptr;
}

// Called every tick. Returns the mask bits that rose this tick.
static uint32_t LearnButtons()
{
    const uint32_t now  = ReadButtons() & BTN_ALL;
    const uint32_t rose = now & ~s_btnPrev;
    s_btnPrev = now;
    if (!rose) return 0;

    for (int i = 0; i < 4; i++) {
        LearnedKey& L = s_learned[i];
        if (!(rose & L.mask) || L.locked) continue;
        // Exactly one candidate down => that is the key. More than one and we
        // simply wait for a cleaner press; a wrong name is worse than none.
        int down = 0, vk = 0;
        for (int k = 0; k < KEY_CANDIDATE_COUNT; k++) {
            if (GetAsyncKeyState(KEY_CANDIDATES[k]) & 0x8000) {
                down++; vk = KEY_CANDIDATES[k];
                if (down > 1) break;
            }
        }
        if (down != 1) continue;
        if (L.vk == vk) { if (++L.hits >= 2) L.locked = true; }
        else            { L.vk = vk; L.hits = 1; }
    }
    return rose;
}

static const char* NameForMask(uint32_t mask)
{
    LearnedKey* L = SlotFor(mask);
    return L ? KeyName(L->vk) : nullptr;
}

// Copies into the caller's buffer, so several names can appear in one printf.
static void CopyKeyName(char* dst, size_t n, uint32_t mask)
{
    const char* p = NameForMask(mask);
    if (!p) p = "?";
    size_t i = 0;
    for (; p[i] && i + 1 < n; i++) dst[i] = p[i];
    dst[i] = 0;
}

static const char* ActionForMask(uint32_t mask)
{
    switch (mask) {
        case BTN_PUNCH: return "Punch";
        case BTN_KICK:  return "Kick";
        case BTN_BLOCK: return "Block";
        case BTN_HEAVY: return "Heavy punch";
        default:        return "Unknown";
    }
}

// --------------------------------------------------------------- the assist
//
// squall::squ_guarding0 raises the guard flag 4 frames after the keyscan REQs
// it, holds it 20 frames, drops it, then spends its animation tail with the
// priority-5 slot still occupied -- so the keyscan cannot re-REQ until the
// whole script returns. Holding the key therefore gives a DUTY CYCLE, not a
// guard: the offline model calibrated against Aaron's measured 0.72 s attack
// interval puts it near half, and his own 2026-08-15 run bears that out --
// 6 of the 9 hits he took while holding the key landed in the guarded damage
// band, 3 did not.
//
// Half a guard is not a mechanic a blind player can work with: the whole
// warning is the 8-to-10 frame wind-up inside gal0::g0_punching0/g0_kicking0,
// which is 133-167 ms. That is below human reaction time, so nothing the mod
// SAYS can close the gap -- the only honest fix is to make the held key mean
// what the briefing says it means.
//
// So while the block bit is held and the fight is live, the mod pins the guard
// flag the script itself sets. It changes no damage number, no timer and no
// script: squ_hpcalc0 still rolls the guarded band (15..46 instead of 40..103),
// the round still ends on the clock, and the soldier still has to be beaten on
// health to win. F9 turns it off for anyone who wants the raw duty cycle.
static bool  s_assist       = true;
static DWORD s_blockHeldAt  = 0;   // last tick the block bit was down
static bool  s_blockSeen    = false;
static bool s_assistPinned  = false;   // pinned on the last tick (for the log)
static int  s_assistTicks   = 0;

// v0.20.122: the assist now covers a short GRACE WINDOW after the key comes up.
// The scene demands that the player release block to throw the heavy punch, and
// in the 2026-08-15 14:10 log the very first thing that happened in that gap
// was an unguarded hit that took 47 HP and reset the block streak to zero --
// which is to say the game punished the one move it requires. The window is
// only as long as it takes to press a second key.
static const DWORD ASSIST_GRACE_MS = 1500;

static void ApplyBlockAssist(uint16_t field)
{
    const DWORD nowT = GetTickCount();
    if (ReadButtons() & BTN_BLOCK) { s_blockHeldAt = nowT; s_blockSeen = true; }
    const bool held = s_blockSeen && (nowT - s_blockHeldAt) < ASSIST_GRACE_MS;
    if (!s_assist || !held) {
        if (s_assistPinned) {
            Log::Field("FieldNavigation: [BGBTL-BLOCK] assist released after %d ticks",
                       s_assistTicks);
            s_assistPinned = false;
            s_assistTicks  = 0;
        }
        return;
    }
    WriteVarB(GuardVarFor(field), 1);
    if (!s_assistPinned) {
        s_assistPinned = true;
        s_assistTicks  = 0;
        Log::Field("FieldNavigation: [BGBTL-BLOCK] block key held -- guard var %d pinned",
                   GuardVarFor(field));
    }
    s_assistTicks++;
}
