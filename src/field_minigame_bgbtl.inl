// field_minigame_bgbtl.inl — the Garden-battle mini-game (#minigame-bgbtl)
//
// PART OF field_navigation.cpp -- TEXTUAL INCLUDE. Do NOT compile standalone.
// Included after field_nav_mapjump_diag.inl, inside namespace FieldNavigation.
//
// ===========================================================================
// THE SCENE, AS THE SCRIPTS ACTUALLY DEFINE IT  (v0.20.119 rewrite)
// ===========================================================================
//
// Squall and a Galbadian soldier trade punch / kick / block hanging off the
// flying vehicle. It spans two fields: it starts in the HOST field 144
// 'bg2f_31' over the battle FMV disc01_32h.avi, then MAPJUMPOs to field 152
// 'bgbtl_1'. Losing loads field 95 'testbl6' (Game Over); winning ends with
// director0's MAPJUMP3 to field 675 'ggback1' (Galbadia Garden).
//
// Everything below is read out of those two fields' own scripts, decoded with
// minigame/jsm2.py, whose instruction decoding is verified against the engine's
// decoder at FF8_EN.exe VA 0x00530760 and its 399-handler opcode table at
// 0x00B8DE94. Where a claim rests on a measurement from Aaron's machine rather
// than on the scripts, it says so.
//
// THE LOOP.  squall::squ_punchkeyscan0 (bg2f_31 label 12, bgbtl_1 label 28) is
// one tight loop of four BTNTESTs (opcode 109) and a JMP -53:
//
//     mask  16 -> squ_punching0      an ordinary punch
//     mask  64 -> squ_kicking0       a kick
//     mask 128 -> squ_guarding0      THE BLOCK
//     mask  32 -> squ_punching1      THE HEAVY PUNCH, gated on var340 >= 3
//
// Opcode 109's handler (0x0051DA50) reads the LEVEL of 0x01CE48B0 and sets
// local[0] to 1 or 0. There is no edge and no consumption: **holding works.**
//
// THE DAMAGE.  squ_hpcalc0 -- the routine that runs when Squall is hit -- rolls
// a byte and reads ONE flag, Squall's guard flag (var 1030 in bg2f_31, var 1028
// in bgbtl_1, set by squ_guarding0 and held for 20 frames):
//
//     guarded    15 + rnd/8    ->  15..46
//     unguarded  40 + rnd/4    ->  40..103
//
// Every one of the 35 hits in Aaron's 2026-08-15 log falls inside one of those
// two bands, and the 9 that fall in the guarded band are all from the attempt
// he spent holding the block key. HE HAS BEEN BLOCKING ALL ALONG; the module
// was watching var 1031, which is the SOLDIER'S guard flag, and told him he had
// blocked nothing across four attempts.
//
// THE WIN CONDITION.  director0::default waits for var 80 >= 580 and then
// compares `foeHP < squallHP`. Blocking alone therefore LOSES: the soldier's
// health never falls. gal_hpcalc0 pays for attacking --
//
//     soldier guarding   10 + rnd/16
//     ordinary punch     30 + rnd/4
//     kick               15 + rnd/6
//     squ_punching1     300 + rnd      -> 300..555 of the soldier's 600
//
// -- and squ_punching1 is unlocked by var 340, which squ_punched_up0 increments
// on a BLOCKED hit and zeroes on an unblocked one, reaching 3 exactly when the
// game REQs director5::sys_mes to tell a sighted player. So the scene is
// "block three in a row, then throw the heavy punch, twice."
//
// THE WARNING IS 133 ms.  gal0::g0_punching0 WAITs 10 frames between its REQ
// and the hit, g0_kicking0 WAITs 8 -- 167 ms and 133 ms at 60 Hz, and the log
// agrees to within a tick (REQ +484 ms, hit +625 ms). Nothing the mod SAYS can
// fit in that, so the tone is a confirmation and the block must be something
// already held. squ_guarding0 gives a ~50% duty cycle on its own, which is why
// the mod pins the guard flag while the key is down (see the assist in
// field_minigame_bgbtl_input.inl).
//
// VAR 80 IS THE MOVIE'S FRAME NUMBER.  0x0052A016: if a movie is running, call
// 0x005305A0 and store the result at [0x00B8EE90]+0x50. It does not advance
// when no movie is playing -- which is why v0.20.117's F10, writing 579 and
// waiting for the engine to carry it to 580, stalled for fifty-three seconds
// with Squall being punched throughout.
//
// THE ARM.  The fight is live ~15 s before field 152 loads, so the module arms
// on the battle FMV (disc01_32h.avi, which plays nowhere else) with the game's
// own "Punch Block Kick" legend as a backstop. On a RETRY the AVI name is
// latched, so the briefing opens when the host field reloads instead -- the
// legend is 43 seconds late on a retry and that is what "the enemy is
// accumulating hits on Squall" was.
//
// THE PAUSE.  field_main_loop (0x0046FEE0) skips its call to field_main
// (0x00471F70) when the engine's own pause flag is set, so a one-byte RET at
// field_main's entry is the engine's own pause point. Verified by signature
// before writing, restored on every exit path, and watched by a heartbeat
// thread that puts the byte back if the mod's loop ever dies.
//
// HOOK DISCIPLINE (unchanged, and it is what keeps this safe):
//   * the REQ hook (opcode 0x014) is installed only while armed, and removed
//     the instant it is not;
//   * the hook body reads two words, writes one ring slot, and chains. No
//     logging, allocation, speech or locks inside the hook;
//   * everything else happens on the mod's own tick in Update().

namespace GardenBattle {

static const uint16_t FIELD_MINIGAME = 152;   // bgbtl_1  -- the fight
static const uint16_t FIELD_GAMEOVER = 95;    // testbl6  -- Game Over screen
static const uint16_t FIELD_HOST     = 144;   // bg2f_31  -- hosted the opening
static const uint16_t FIELD_VICTORY  = 675;   // ggback1  -- you won

// Whatever field was current when the legend armed us. 144 in the v0.20.103
// BAT, but recorded rather than assumed -- the whole lesson of this arc is that
// the field hosting the fight is not the field the fight is named after.
static uint16_t s_hostField = 0xFFFF;

// Fields the fight legitimately spans. Anything else means it is over.
static bool IsFightField(uint16_t f)
{
    return f == FIELD_MINIGAME || f == FIELD_GAMEOVER ||
           f == FIELD_HOST     || f == s_hostField;
}

static const uint32_t OPCODE_REQ = 0x014;

// VM context layout, same constants field_nav_mapjump_diag.inl uses.
static const ptrdiff_t VMCTX_SP_OFFSET = 0x184;

// Field VM variable block. FFNx calls it field_vars_stack_1CFE9B8; the mod
// already documents it in field_nav_mapjump_diag.inl.
static const uintptr_t VARBLOCK_BASE = 0x01CFE9B8;

// The four health values. Byte offsets into the variable block, signed int16.
// Derived offline from bgbtl_1's hpcalc pair and bg2f_31's initialiser; the
// unscaled, sign-extended addressing is confirmed at 0x0051CBF0 in the exe.
static const int HP_SQUALL_MAX = 350;
static const int HP_SQUALL_CUR = 354;
static const int HP_FOE_MAX    = 352;
static const int HP_FOE_CUR    = 356;

// THE FIGHT CLOCK -- the answer to "why does the foe keep swinging after he is
// out of health", and to Aaron's "it should go to the FMV where Squall rescues
// Rinoa, not all the way to the G-Garden entrance".
//
// bgbtl_1's driver, director0::talk, is a ladder of waits on ONE value:
//     dw  970  OP_0x0E 80   then 1
//     dw  976  OP_0x0E 80   then 20
//     dw  990  OP_0x0E 80   then 580     <- THE FIGHT ENDS HERE
//     dw  995  read var 356 (foe HP), read var 354 (Squall HP), branch
//     dw 1004  REQ gal0 fall   /   dw 1012  REQ squall fall
//     dw 1026  OP_0x0E 80   then 750     <- the ending plays out
//     dw 1043  OP_0x0E 80   then 840
//     dw 1060  OP_0x0E 80   then 1057
//     dw 1078  MAPJUMP3 675 (ggback1)    <- the LAST instruction
//
// So the fight is a FIXED-LENGTH round. HP is not checked until the clock
// reaches 580; that is why zeroing the foe changes nothing until then. And the
// rescue scene is everything between 580 and 1057 -- which is exactly what
// v0.20.110's forged jump to 675 threw away.
//
// OP_0x0E reads a DWORD, unscaled, from the same variable block. From the exe,
// handler 0x0051CB70 (opcode table 0x00B8DE94 entry 0x0E):
//     mov ecx, dword ptr [eax + 0x1CFE9B8]
// which is the 32-bit sibling of the RDVARB (0x0051CBB0) and RDVARW
// (0x0051CBF0) handlers that pinned HP. And bytes 80 AND 81 were both in the
// v0.20.103 whole-block trace's "moved during fight" list, counting upward --
// the low half of this counter.
//
// `OP_0x0E` appears SIX times in bgbtl_1 and TWICE in bg2f_31, and in all eight
// its operand is 80. It reads one thing and one thing only.
static const int      FIGHT_CLOCK     = 80;    // uint32 at VARBLOCK+80

// RETIRED IN v0.20.123, kept as a note because someone will be tempted again:
// this module used to pause the scene by writing a one-byte RET over the entry
// of field_main (0x00471F70). It worked, but window rendering hangs off that
// same function -- field_main -> 0x00471010 -> 0x0052BC00 -> 0x004A0880 -- so a
// frozen field can never draw the Game Controls box. The briefing now pauses the
// FIGHT rather than the FIELD, by vetoing the soldier's attack REQs. Do not
// bring the RET back.

// RETIRED IN v0.20.111, kept as a note because the FINDING is still true and
// someone will be tempted again. director0::talk ends with
//     push 675, 1019, 3384, 0, 128 ; MAPJUMP3 77
// written into the engine transition block at 0x01CE4760 (type byte 1; +2 dest
// field, +4/+6/+8 args, +0xC inline operand, +0xE top of stack), and field 675
// is ggback1 -- all confirmed, including by a BAT log showing
// [fieldload] id=675 name='ggback1' on a win.
//
// REPRODUCING THOSE BYTES IS NOT THE SAME AS REPRODUCING THAT MOMENT. The
// script reaches that line having finished the fight and torn its scene down.
// Firing the jump mid-fight crashed the game on arrival in Galbadia Garden.
// The skip now zeroes the foe's HP instead and lets the script do its own
// ending. Do not bring the jump back.

// THERE IS NO TIME LIMIT ON THE BRIEFING, and v0.20.108 removed the one there
// was. Aaron: "why have the cap at all?" -- and he is right: a player taking
// their time is not a fault condition, and in the v0.20.107 BAT the cap fired
// while he was still deciding. The only bound left is the round clock, which
// ends the briefing before the fight can be lost to a reading speed.
static const DWORD REMIND_MS          = 12000;    // "Press Enter to start..."

// The FMV that hosts the opening of the fight. Arming on it freezes the scene
// BEFORE the first punch lands: in the v0.20.106 BAT, Squall was already down
// to 440 of 600 by the time the legend text appeared seven seconds later.
static const char* ARM_AVI = "disc01_32h.avi";
static const DWORD BLOCK_HOLD_MS      = 450;   // a hit report will not stomp
                                               // a Block cue this fresh

// ---------------------------------------------------------------- event ring

struct ReqEvent { DWORD tick; uint16_t entity; int16_t label; };

static const int RING_SIZE = 256;             // power of two
static volatile ReqEvent s_ring[RING_SIZE];
static volatile LONG s_ringWrite = 0;         // written by the hook only
static LONG          s_ringRead  = 0;         // read by Update() only

static uint32_t s_origReq = 0;
static bool     s_installed = false;
static bool     s_inMinigame = false;
static bool     s_useTone = true;             // v0.20.110: TONE IS THE DEFAULT.
                                              // 700 ms is not much room for a
                                              // word, and Aaron chose the tone
                                              // after hearing both.
static DWORD    s_lastCue = 0;
static DWORD    s_enterTick = 0;
static DWORD    s_blockCueUntil = 0;          // priority hold, see Cue()
static bool     s_briefing  = false;          // briefing up, field frozen
static DWORD    s_briefStart = 0;
static bool     s_awaitRelease = false;
static bool     s_needKeyUp    = false;  // confirm must be seen UP first
static bool     s_briefedThisAttempt = false;   // one briefing per attempt
static DWORD    s_lastRemind = 0;
static bool     s_aviLatched = false;   // this movie has already armed us once
static bool     s_sawGameOver = false;  // the next host-field load is the hallway
static int      s_cuesSuppressed = 0;         // how many the gate ate

// Health tracking. Percentages are rounded to the nearest 5 so a one-point
// scratch does not produce a new announcement every quarter second.
static int  s_pctSquall = -1;
static int  s_pctFoe    = -1;
static bool s_foeDown    = false;
static bool s_wonAnnounced = false;   // the win has been spoken already
static bool s_playerDown = false;
static bool s_sawFoeAlive    = false;   // guards against a stale carry-over
static bool s_sawPlayerAlive = false;
static bool s_skipActive     = false;   // F9: hold the fight until it ends

// The scene's running tally, reported once in the disarm summary. `s_guardSeen`
// is the one fact worth keeping from the v0.20.113 instrumentation: whether the
// guard mechanism engaged at all. It is now set by WatchStreak, because var 340
// is incremented once per BLOCKED HIT while the guard flag has one rising edge
// per hold -- see the note above WatchStreak.
static bool s_guardSeen  = false;
static int  s_attacks    = 0;
static int  s_guards     = 0;            // successful blocks
static int  s_briefCount = 0;            // briefings this scene (alternates the pause)
static bool s_pausedThisAttempt = false;

static bool     s_announceBlock = false;
static bool     s_fmvSkipAsked = false;
static int      s_vetoed        = 0;      // attack REQs the skip turned into no-ops
static bool     s_reached152    = false;  // the fight has reached bgbtl_1
static int      s_bestStreak    = 0;
static DWORD    s_lastHeavyCue  = 0;
static bool     s_heavyArmed    = false;
static int16_t  s_lastReqLabel  = -1;
static DWORD    s_priorityUntil = 0;      // health reports yield until this
static const int   STREAK_FOR_HEAVY = 3;  // squ_punchkeyscan0's `var340 >= 3`
static const DWORD HEAVY_REPEAT_MS  = 5000;
static const DWORD HEAVY_HOLD_MS    = 2000;
// The briefing gives the round back this far before the resolution at 580.
static const uint32_t CLOCK_BRIEF_LIMIT = 420;
static DWORD    s_lastBriefPoll = 0;
// A held key auto-repeats; without this the learner re-announces on every
// repeat and each announcement interrupts the one before it.
static const DWORD LEARN_REPEAT_MS = 1200;
static DWORD    s_lastLearnSpoke[4] = { 0, 0, 0, 0 };
static int      s_streak = -1;            // var 340, blocks in a row
static bool     s_heavyAnnounced = false;

// The button word, the key learner and the hold-to-block assist. Split out
// because this file is 70 KB and the 80 KB CI cap is per source file.
#include "field_minigame_bgbtl_input.inl"

// The Game Controls box. Needs the button masks and the learned key names from
// the file above, so it is included after it.
static const int BRIEF_COLS = 34;      // ~8 px per glyph in a 320 px screen
static char      s_briefScreen[512];
#include "field_minigame_bgbtl_dialog.inl"

// The fight's state bytes. VAR 1031 going 0 -> 1 is a block whether or not a
// script was REQ'd for it -- which matters, because if the engine sets that
// flag directly a REQ hook is blind to every block the player makes.
// squall::squ_punching1 -- the heavy punch, the keyscan's fourth BTNTEST.
static int16_t HeavyPunchLabel(uint16_t field)
{
    return (field == FIELD_MINIGAME) ? (int16_t)30 : (int16_t)14;
}

static void CueReport(const char* text);          // defined below, with the gate
static void AnnounceHeavy();                     // ...and so is this
static void ResumeNarration(const char* why);    // ...and the movie's voice

// THE HEAVY PUNCH, AND WHY THIS FIGHT IS WINNABLE AT ALL.
//
// squall::squ_punched_up0 -- the script that runs when Squall is hit -- reads
// the guard flag and does one of two things:
//     guarded      var340 = var340 + 1;  if var340 == 3 -> REQ director5::sys_mes
//     not guarded  var340 = 0
// and the keyscan gates its fourth BTNTEST on `var340 >= 3`. That fourth mask
// is 32, and it REQs squall::squ_punching1.
//
// squ_hpcalc0's twin, gal_hpcalc0, is where that pays: for an ordinary punch
// the soldier takes 30 + rnd/4, for a kick 15 + rnd/6, for a GUARDED punch
// 10 + rnd/16 -- but for squ_punching1 it takes **300 + rnd**, i.e. 300 to 555
// out of 600. Two of them end the fight.
//
// So the scene is not "survive the timer", it is "block three in a row, then
// throw the heavy punch, twice". THAT is the mechanic the player has to be told
// about, and no previous version of this briefing mentioned it, because the
// label numbers were one out and the guard flag being watched was the SOLDIER'S.
static void WatchStreak()
{
    bool ok = false;
    const int now = ReadVarB(STREAK_VAR, &ok);
    if (!ok || now < 0 || now > 64) return;
    if (now == s_streak) return;
    const int was = s_streak;
    s_streak = now;
    if (was < 0) return;                        // first read, nothing to say
    Log::Field("FieldNavigation: [BGBTL-STREAK] var340 %d -> %d", was, now);
    // v0.20.120: **THE BLOCK COUNT COMES FROM HERE NOW, NOT FROM THE GUARD
    // FLAG.** The 2026-08-15 run reported "2 BLOCKED" while this counter walked
    // 0 -> 6, and both numbers were honest: with the assist pinning the guard
    // flag, the flag has one rising edge per HOLD, not one per hit. var 340 is
    // incremented by squ_punched_up0 exactly once per blocked hit, which is the
    // thing the player wants counted and told about.
    if (now > was) {
        s_guards += (now - was);
        s_guardSeen = true;
        if (now > s_bestStreak) s_bestStreak = now;
        s_announceBlock = true;
    }
    if (now == 0 && was > 0) { s_heavyAnnounced = false; return; }
    // v0.20.121: **THIS IS THE ONE ANNOUNCEMENT THAT DECIDES THE FIGHT, AND IT
    // WAS BEING TALKED OVER.** In the 13:45 run var 340 walked 0 -> 8 and the
    // game's own hint (director5::sys_mes) fired on cue, and Aaron: "I tried
    // blocking attack after attack in the first run and never heard the power
    // punch or whatever it is called announce." It WAS spoken -- and then the
    // health report a fraction of a second later called Speak(..., interrupt)
    // and cut it off.
    //
    // So it goes out on its own path: it never yields to a block cue, it holds
    // the floor against health reports for a moment afterwards, and while the
    // streak stays armed and unused it repeats, because a call to action the
    // player missed once is worth saying twice.
    if (now >= STREAK_FOR_HEAVY) {
        s_heavyArmed = true;
        if (!s_heavyAnnounced || GetTickCount() - s_lastHeavyCue > HEAVY_REPEAT_MS)
            AnnounceHeavy();
    }
}

// v0.20.122: **THE HEAVY PUNCH WAS ARMED AND THEN TAKEN AWAY BEFORE IT COULD BE
// THROWN.** Aaron: "I held down block and heard it announce the power punch was
// ready, but when I pressed D I never heard a punch land."
//
// The 14:10 log has it exactly. In field 152 it WORKED -- squ_punching1 at
// 14:10:21, gal0::g0_punched_up0 at 14:10:22, and the foe fell 600 -> 66, a
// 534-damage hit. In field 144 the same REQ fired at 14:10:06 and NOTHING
// happened: no g0_punched_up0, no HP change.
//
// Two things conspire, and both of them punish the player for the one move the
// scene requires:
//
//   1. THE STREAK IS LOST BY LETTING GO. var 340 is zeroed by any unguarded
//      hit, and to throw the punch you must release block. At 14:10:06 Squall
//      took 332 -> 285 on exactly that gap and `var340 3 -> 0` with it, which
//      shuts the keyscan's `var340 >= 3` gate -- so a HELD heavy key produced
//      one REQ attempt and then nothing for the six seconds he kept holding it.
//   2. THAT ONE ATTEMPT COLLIDED. squ_punching1 is REQ'd into the same priority
//      slot squ_guarding0 uses, and the guard script was still running out its
//      animation tail, so the engine dropped it.
//
// Holding the gate open fixes both: it survives the hit that opens the window,
// and it lets a held key keep retrying until the slot frees. The counter is
// held at 4 rather than 3 so the game's own hint (director5::sys_mes, REQ'd on
// `var340 == 3`) does not re-fire on every tick.
static void HoldHeavyArmed(uint16_t field, int16_t lastLabel)
{
    if (!s_heavyArmed) return;
    if (lastLabel == HeavyPunchLabel(field)) {       // it landed -- let go
        s_heavyArmed     = false;
        s_heavyAnnounced = false;
        Log::Field("FieldNavigation: [BGBTL] heavy punch thrown -- gate released");
        return;
    }
    bool ok = false;
    const int v = ReadVarB(STREAK_VAR, &ok);
    if (ok && v < STREAK_FOR_HEAVY) WriteVarB(STREAK_VAR, STREAK_FOR_HEAVY + 1);
}
static DWORD s_lastHpPoll = 0;

static int ReadHp(int off, bool* ok)
{
    __try {
        *ok = true;
        return (int)*(const int16_t*)(VARBLOCK_BASE + off);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        *ok = false;
        return 0;
    }
}

// Clamped, because current HP goes negative rather than stopping at zero --
// the v0.20.103 trace caught the soldier at -18.
static int HpPercent(int cur, int max)
{
    if (max <= 0) return -1;
    if (cur <= 0) return 0;
    if (cur >= max) return 100;
    int pct = (cur * 100 + max / 2) / max;
    int r = ((pct + 2) / 5) * 5;         // nearest 5
    // v0.20.110: NEVER round a live fighter down to zero. The v0.20.109 BAT
    // announced "Foe 0" at 2/600 -- 0.3% rounds to 0 -- and then the foe hit
    // Aaron, which reads as the mod being wrong about the one number it is
    // there to report. Zero now means zero.
    if (r <= 0) r = 5;
    return r;
}

typedef int (__cdecl *OpcodeFunc_t)(void* ctx, int param);

// ------------------------------------------------------------------ the hook
//
// Deliberately tiny. Two reads, one store, chain. Anything more belongs in
// Update(). If this ever needs to grow, it does not -- move the work.
static bool IsSoldierAttack(uint16_t field, int16_t label);   // defined below

// v0.20.121: HOW THE SKIP STOPS THE SOLDIER WITHOUT TOUCHING ANY STATE.
//
// v0.20.120 tried parking Squall's health at zero, because gal0::g0_fall0 gates
// every attack on `RDVARSW 354 > 0`. It works, and it also loses the fight: in
// the 2026-08-15 13:46 run F10 was followed by the Game Over screen four
// seconds after field 152 loaded. Zero HP is a lethal value however briefly it
// is held, and the resolution is not the only thing that reads it.
//
// So the skip vetoes the attack at the REQ instead. The hook already sits on
// opcode 0x014 and already reads the label off the VM stack; while the skip is
// active it OVERWRITES that stack slot with the soldier's own `push` script
// before chaining. Both fields' push scripts are, in full:
//
//     PUSH8 n
//     RET 8
//
// -- so the engine performs a completely ordinary REQ, with the same two pops,
// the same priority slot and the same bookkeeping, of a script that does
// nothing. No variable is written, no timer is moved, nothing is forged. The
// attack simply never starts, which means no wind-up, no punch sound and no
// call into squ_hpcalc0.
// Also used by the briefing (v0.20.123): while the Game Controls are up the
// game keeps running so the box can be drawn, and this is what keeps the fight
// from happening around it.
static int16_t SkipVetoLabel(uint16_t field, int16_t label)
{
    if (!IsSoldierAttack(field, label)) return label;
    return (field == FIELD_MINIGAME) ? (int16_t)45    // gal0::push
                                     : (int16_t)48;   // g_hei0::push
}

// v0.20.130: **THE BRIEFING VETOED THE SOLDIER AND LEFT SQUALL SWINGING.**
// Aaron: *"as I press punch or kick on the controls screen I can hear Squall
// striking the enemy. When I press enter to actually start, the enemy's HP is
// not full."*
//
// The 2026-08-15 log measures it exactly. Briefing 1 ran 34.5 s while he tapped
// his four keys to have them named, and the first health line after it reads
// **`Foe 431/600`** -- 169 damage already dealt before "Game start." Briefing 2
// lasted 5.4 s, he pressed nothing but Enter, and it reads `Foe 600/600`.
//
// The cause is the same thing that makes the briefing work at all: since
// v0.20.123 the field is NOT frozen, so `squ_punchkeyscan0` is live behind the
// box. Its four BTNTESTs read the same button word the key learner does, and a
// tap on punch or kick REQs `squ_punching0` / `squ_kicking0`, which reach
// `gal_hpcalc0` and take 30 + rnd/4 off the soldier. **The calibration step was
// a free hit on the enemy, and the player was being charged for learning their
// own controls in the only currency this fight has.**
//
// So the briefing now vetoes BOTH fighters. Squall's three attack scripts are
// redirected to `squall::push` -- his own entity's `PUSH8 n ; RET 8` no-op, the
// same trick the soldier's veto has used since .123.
//
// `squ_guarding0` is deliberately NOT vetoed: it only raises the guard flag,
// costs nobody anything, and the block key is the one control worth letting the
// player feel while they are being told about it.
//
// Label derivation, checked against two known-good values: a `.sym` group
// (count, start) spans count+1 slots and its names run
// header, default, talk, push, ... -- so `push` is always start+3. bgbtl_1's
// squall group is (14, 22), giving squall::push = 25 and keyscan = 28, which is
// the number the log prints. bg2f_31's is (18, 0), giving squall::push = 3 and
// keyscan = 12, likewise. The same arithmetic reproduces gal0::push = 45 and
// g_hei0::push = 48, which this file already used and the fight already proved.
struct PlayerAttackSet { uint16_t field; int16_t punch, heavy, kick, push; };
static const PlayerAttackSet PLAYER_ATTACKS[] = {
    //                punching0  punching1  kicking0  push
    { FIELD_HOST,        13,        14,        15,      3 },   // bg2f_31 squall
    { FIELD_MINIGAME,    29,        30,        31,     25 },   // bgbtl_1 squall
};

static int16_t BriefingVetoLabel(uint16_t field, int16_t label)
{
    const int16_t soldier = SkipVetoLabel(field, label);
    if (soldier != label) return soldier;
    for (int i = 0; i < (int)(sizeof(PLAYER_ATTACKS) / sizeof(PLAYER_ATTACKS[0])); i++) {
        const PlayerAttackSet& p = PLAYER_ATTACKS[i];
        if (p.field != field) continue;
        if (label == p.punch || label == p.heavy || label == p.kick) return p.push;
        return label;
    }
    return label;
}

static int __cdecl HookedReq(void* ctx, int param)
{
    if (s_inMinigame && ctx != nullptr) {
        __try {
            const signed char sp = *(const signed char*)((const char*)ctx + VMCTX_SP_OFFSET);
            const int16_t label  = *(const int16_t*)((const char*)ctx + (int)sp * 4);
            int16_t label2 = label;
            if (s_skipActive || s_briefing) {
                const uint16_t fid = FF8Addresses::pCurrentFieldId
                                   ? *FF8Addresses::pCurrentFieldId : 0xFFFF;
                const int16_t veto = s_briefing ? BriefingVetoLabel(fid, label)
                                                : SkipVetoLabel(fid, label);
                if (veto != label) {
                    *(int16_t*)((char*)ctx + (int)sp * 4) = veto;
                    s_vetoed++;
                    // v0.20.122: RECORD WHAT ACTUALLY RAN, NOT WHAT WAS ASKED
                    // FOR. Aaron: "I tried F9 to skip and it caused the
                    // beep/block tone to spam repeatedly. I didn't hear any
                    // actual punches from the enemy just the beep as if they
                    // were about to." Both halves are exactly right: the veto
                    // worked, and the ring still carried the ORIGINAL label, so
                    // every cancelled attack still rang the block tone -- and
                    // rang it faster than a real fight, because `push` returns
                    // instantly and the driver loops straight back round. 171
                    // cues in that run. Logging the label the engine really ran
                    // fixes the noise and the trace in one place.
                    label2 = veto;
                }
            }
            const LONG w = s_ringWrite;
            s_ring[w & (RING_SIZE - 1)].tick   = GetTickCount();
            s_ring[w & (RING_SIZE - 1)].entity = (uint16_t)param;
            s_ring[w & (RING_SIZE - 1)].label  = label2;
            s_ringWrite = w + 1;
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            // A bad read here must never take the game down. Drop the event.
        }
    }
    return ((OpcodeFunc_t)s_origReq)(ctx, param);
}

// ------------------------------------------------------- labels, PER FIELD
//
// Label numbers index the FIELD'S OWN entry-point table -- they mean nothing
// across fields. v0.20.110 cued bgbtl_1's numbers while the fight was running
// in the host field and produced total silence, so there are two tables.
//
// The measured wind-up, from that same fight, one line per attack:
//     +7468  label 55 -> +8203  damage    +9546  label 56 -> +10140 damage
//     +12546 label 56 -> +13140 damage    +14812 label 55 -> +15546 damage
// -- about 600-700 ms from the mod's REQ hook to the damage report, of which
// the script's own wind-up is 133-167 ms and the rest is the mod's poll
// interval. It is the wind-up that bounds what a player can react to.
//
// v0.20.119: THE LABEL NUMBERS WERE ALL ONE LOW, AND IT INVALIDATED THREE BATs.
//
// A .sym group is (count, start) and it covers count+1 label slots, because the
// slot at `start` holds the GROUP HEADER -- the bare entity name that precedes
// the `entity::method` list. Proof, from bg2f_31: squall is (18, 0) and the
// next group starts at 19, so squall owns 0..18 = nineteen slots, and the .sym
// lists nineteen strings for it ('squall' plus eighteen methods). Read off by
// one, squ_punchkeyscan0 lands on label 11, whose body is a positioning script;
// read correctly it lands on 12, and label 12 IS the keyscan -- four BTNTESTs
// and a JMP -53 back to the top. Every name in both fields matches its body
// under the corrected rule and several contradict it under the old one.
//
// So these are the SOLDIER'S TWO ATTACK SCRIPTS, not three:
//     bg2f_31   55 g_hei0::g0_punching0   56 g_hei0::g0_kicking0
//     bgbtl_1   49 gal0::g0_punching0     50 gal0::g0_kicking0
// The third label the old table cued (54 / 48) is g0_fall0, the soldier's
// DRIVER -- it fires once and then loops choosing attacks. And what the old
// table called an attack, g0_guarding0, is the soldier REACTING to a punch of
// Squall's: the keyscan REQs it immediately after Squall attacks.
struct AttackSet { uint16_t field; int16_t a, b; };
static const AttackSet ATTACKS[] = {
    { FIELD_HOST,     55, 56 },   // bg2f_31 g_hei0::g0_punching0 / g0_kicking0
    { FIELD_MINIGAME, 49, 50 },   // bgbtl_1 gal0::g0_punching0  / g0_kicking0
};

static bool IsSoldierAttack(uint16_t field, int16_t label)
{
    for (int i = 0; i < (int)(sizeof(ATTACKS) / sizeof(ATTACKS[0])); i++) {
        if (ATTACKS[i].field != field) continue;
        return label == ATTACKS[i].a || label == ATTACKS[i].b;
    }
    return false;                  // unknown field: log, never cue blind
}

// The wind-up between the attack REQ and the hit, read off the scripts:
// g0_punching0 WAITs 10 frames before REQing squ_punched_up0, g0_kicking0
// WAITs 8. At 60 Hz that is 167 ms and 133 ms -- and Aaron's own log agrees to
// within a tick (attack REQ at +484 ms, the hit at +625 ms). It is recorded
// here because it is the reason the cue can only ever be a CONFIRMATION: no
// announcement, tone or speech, fits inside 133 ms of warning, so the block has
// to be something the player is already holding when the cue arrives.

// ------------------------------------------------------------------ naming
//
// Straight from each field's own .sym. Used for the trace, so every line in the
// log is readable -- and so a per-field mismatch like the v0.20.110 one shows up
// as a wrong NAME rather than as silence.
static const char* LabelName(uint16_t field, int16_t label)
{
    if (field == FIELD_HOST) {
        switch (label) {                       // bg2f_31, groups re-read v0.20.119
            case 12: return "squall::squ_punchkeyscan0";
            case 13: return "squall::squ_punching0";
            case 14: return "squall::squ_punching1";
            case 15: return "squall::squ_kicking0";
            case 16: return "squall::squ_guarding0";
            case 17: return "squall::squ_punched_up0";
            case 18: return "squall::squ_lookaround0";
            case 43: return "stc0::squ_hpcalc0";
            case 44: return "stc0::gal_hpcalc0";
            case 54: return "g_hei0::g0_fall0";
            case 55: return "g_hei0::g0_punching0";
            case 56: return "g_hei0::g0_kicking0";
            case 57: return "g_hei0::g0_guarding0";
            case 58: return "g_hei0::g0_punched_up0";
            case 87: return "director5::sys_mes";
            default: return nullptr;
        }
    }
    switch (label) {                           // bgbtl_1
        case  6: return "rinoa::squ_hpcalc0";
        case  7: return "rinoa::gal_hpcalc0";
        case 28: return "squall::squ_punchkeyscan0";
        case 29: return "squall::squ_punching0";
        case 30: return "squall::squ_punching1";
        case 31: return "squall::squ_kicking0";
        case 32: return "squall::squ_guarding0";
        case 33: return "squall::squ_punched_up0";
        case 34: return "squall::squ_timeover0";
        case 36: return "squall::squ_show0";
        case 41: return "shade0::shade_move0";
        case 48: return "gal0::g0_fall0";
        case 49: return "gal0::g0_punching0";
        case 50: return "gal0::g0_kicking0";
        case 51: return "gal0::g0_guarding0";
        case 52: return "gal0::g0_punched_up0";
        case 53: return "gal0::gal0_timeover0";
        case 78: return "director5::sys_mes";
        case 85: return "director0::default";
        default: return nullptr;
    }
}

// THE BLOCK TONE. v0.20.110 used Beep(), which has no volume control at all --
// it is whatever the system beep is, and Aaron: "the tone is a bit too short and
// soft, making it difficult to hear." Over an FMV, battle music and the fight's
// own punch sounds, that is not good enough for a 700 ms window.
//
// So it is now a synthesised waveform played through PlaySound at FULL SCALE:
// two rising square-ish tones, 1046 Hz then 1568 Hz (C6 -> G6), 90 ms each with
// a short taper so it does not click. 180 ms total, well inside the window, and
// a rising pair is unmistakably not a game sound.
//
// SND_ASYNC so it never stalls the game thread -- the reason v0.20.103 moved
// Beep off it in the first place. winmm.lib added to deploy.bat for this.
static const int   TONE_RATE  = 22050;
static const int   TONE_MS    = 90;          // per half
static const int   TONE_HZ_A  = 1046;
static const int   TONE_HZ_B  = 1568;

#pragma pack(push, 1)
struct WavHeader {
    char     riff[4]; uint32_t riffSize; char wave[4];
    char     fmt[4];  uint32_t fmtSize;
    uint16_t format, channels;
    uint32_t rate, byteRate;
    uint16_t align, bits;
    char     data[4]; uint32_t dataSize;
};
#pragma pack(pop)

static const int   TONE_SAMPLES = (TONE_RATE * TONE_MS) / 1000 * 2;
static uint8_t     s_toneBuf[sizeof(WavHeader) + TONE_SAMPLES * 2];
static bool        s_toneBuilt = false;

static void BuildTone()
{
    if (s_toneBuilt) return;
    WavHeader* h = (WavHeader*)s_toneBuf;
    memcpy(h->riff, "RIFF", 4); memcpy(h->wave, "WAVE", 4);
    memcpy(h->fmt,  "fmt ", 4); memcpy(h->data, "data", 4);
    h->fmtSize = 16; h->format = 1; h->channels = 1;
    h->rate = TONE_RATE; h->bits = 16; h->align = 2;
    h->byteRate = TONE_RATE * 2;
    h->dataSize = TONE_SAMPLES * 2;
    h->riffSize = sizeof(WavHeader) - 8 + h->dataSize;

    int16_t* pcm = (int16_t*)(s_toneBuf + sizeof(WavHeader));
    const int half = TONE_SAMPLES / 2;
    for (int i = 0; i < TONE_SAMPLES; i++) {
        const int    n    = (i < half) ? i : (i - half);
        const double freq = (i < half) ? TONE_HZ_A : TONE_HZ_B;
        const double ph   = 2.0 * 3.14159265358979 * freq * n / TONE_RATE;
        double v = (sin(ph) >= 0.0 ? 0.85 : -0.85);      // square: carries
        v += 0.15 * sin(ph);                             // a little body
        // 4 ms taper at each end of each half, so it does not click.
        const int taper = TONE_RATE * 4 / 1000;
        if (n < taper)          v *= (double)n / taper;
        if (half - n < taper)   v *= (double)(half - n) / taper;
        int sv = (int)(v * 32000.0);
        if (sv >  32767) sv =  32767;
        if (sv < -32768) sv = -32768;
        pcm[i] = (int16_t)sv;
    }
    s_toneBuilt = true;
}

static void PlayTone()
{
    BuildTone();
    PlaySoundA((LPCSTR)s_toneBuf, nullptr,
               SND_MEMORY | SND_ASYNC | SND_NODEFAULT);
}

// Nothing may talk over the briefing, and nothing may cue while the field is
// frozen -- there is no fight to react to yet. Suppressed cues are counted so
// the log shows exactly what the gate cost.
static bool CuesGated(const char* what)
{
    if (!s_briefing) return false;
    s_cuesSuppressed++;
    Log::Field("FieldNavigation: [BGBTL] suppressed '%s' -- briefing in progress", what);
    return true;
}

// The reaction cue. One word, or one tone. Nothing longer fits in 700 ms.
static void CueBlock()
{
    if (CuesGated("Block")) return;
    const DWORD now = GetTickCount();
    // The tone is a PlaySound and does not interrupt speech, but the SPEECH
    // form of the cue does -- and it must not take the floor from the heavy
    // punch call.
    if (!s_useTone && now < s_priorityUntil) return;
    if (now - s_lastCue < 120) return;         // never stutter on a double fire
    s_lastCue = now;
    s_blockCueUntil = now + BLOCK_HOLD_MS;
    if (s_useTone) PlayTone();
    else           ScreenReader::Speak("Block", true);
}

// A report, not a cue -- it says what just happened. It must never interrupt
// a Block cue the player is still acting on, because the cue is the only one
// of the two that is time-critical.
// The heavy punch is the only thing in this scene that wins it, so its call
// outranks every other line the module can produce. It ignores the block-cue
// hold on the way out and holds the floor on the way back, which is what stops
// a health report a fifth of a second later from cutting it in half.
static void AnnounceHeavy()
{
    if (CuesGated("Heavy punch ready")) return;
    const DWORD now = GetTickCount();
    s_heavyAnnounced = true;
    s_lastHeavyCue   = now;
    s_priorityUntil  = now + HEAVY_HOLD_MS;
    s_lastCue        = now;
    const char* key = NameForMask(BTN_HEAVY);
    char msg[96];
    if (key) snprintf(msg, sizeof(msg), "Heavy punch ready. Let go of block and press %s.", key);
    else     snprintf(msg, sizeof(msg), "Heavy punch ready. Let go of block and use your fourth action key.");
    ScreenReader::Speak(msg, true);
    Log::Field("FieldNavigation: [BGBTL] HEAVY PUNCH announced (%s)", key ? key : "key unknown");
}

static void CueReport(const char* text)
{
    if (CuesGated(text)) return;
    const DWORD now = GetTickCount();
    if (now < s_priorityUntil) {
        Log::Field("FieldNavigation: [BGBTL] held '%s' -- heavy-punch call has the floor",
                   text);
        return;
    }
    if (now < s_blockCueUntil) {
        Log::Field("FieldNavigation: [BGBTL] held '%s' -- Block cue still current", text);
        return;
    }
    s_lastCue = now;
    ScreenReader::Speak(text, true);
}

// -------------------------------------------------------------- health
//
// Aaron: "we just announce health each time it changes, and for that we keep
// it short like 'You 75, Foe 50, You 25, Foe 10'."
//
// Every change, not thresholds -- the measured rate makes that affordable.
// The v0.20.103 winning run had twelve damage events in 92 seconds, one every
// two to seven seconds, so a two-word line per change never crowds the fight.
// Both fighters in one line when they change together, which the trace showed
// happens: two hits inside one poll interval.
static void PollHealth(const char* why)
{
    // v0.20.120: while the skip is holding the numbers they are the mod's, not
    // the game's -- and it parks Squall at zero on purpose. Reporting that back
    // as "You 0" would be a lie told in the player's own voice.
    if (s_skipActive && why == nullptr) return;
    bool ok1 = false, ok2 = false, ok3 = false, ok4 = false;
    const int sMax = ReadHp(HP_SQUALL_MAX, &ok1);
    const int sCur = ReadHp(HP_SQUALL_CUR, &ok2);
    const int fMax = ReadHp(HP_FOE_MAX,    &ok3);
    const int fCur = ReadHp(HP_FOE_CUR,    &ok4);
    if (!(ok1 && ok2 && ok3 && ok4)) {
        Log::Field("FieldNavigation: [BGBTL-HP] read failed");
        return;
    }

    const int ps = HpPercent(sCur, sMax);
    const int pf = HpPercent(fCur, fMax);

    if (why) {                    // arm / exit snapshot, logged not spoken
        Log::Field("FieldNavigation: [BGBTL-HP] %s Squall %d/%d (%d%%)  Foe %d/%d (%d%%)",
                   why, sCur, sMax, ps, fCur, fMax, pf);
        s_pctSquall = ps;
        s_pctFoe    = pf;
        if (sCur > 0) s_sawPlayerAlive = true;
        if (fCur > 0) s_sawFoeAlive    = true;
        return;
    }

    if (sCur > 0) s_sawPlayerAlive = true;
    if (fCur > 0) s_sawFoeAlive    = true;

    // OUTCOME.
    //
    // v0.20.111 announced "You win." when the foe's HP reached zero and it was
    // wrong: the foe sat at 0/600 for SEVENTY SECONDS, kept swinging, and
    // Squall bled 1000 -> -31 and LOST. Zero foe HP does not end the fight --
    // the round is decided at clock 580 on `foeHP < squallHP`, so if Squall
    // reaches zero as well the comparison goes against him.
    //
    // v0.20.115 answered that by moving the win to the field-675 transition,
    // which is true but arrives a full minute after the player has actually
    // won. Aaron: "The You Win announcement still comes when the player reaches
    // G-Garden. That should announce either when the enemy hits 0 HP or the
    // player uses the F9 to automatically win."
    //
    // He is right, and the way to make the early announcement HONEST rather
    // than optimistic is to make it TRUE: putting the foe at zero engages the
    // same protection F9 uses -- attacks vetoed at the REQ, both HP values
    // pinned -- so nothing can take the win back between here and the
    // resolution. The player did the work; the remaining clock is ceremony.
    //
    // Checked BEFORE the "nothing changed" gate below: a decision this
    // important must not depend on a rounded percentage having moved.
    if (!s_foeDown && s_sawFoeAlive && fCur <= 0) {
        s_foeDown = true;
        s_wonAnnounced = true;
        s_skipActive = true;              // hold the win, exactly as F9 does
        SetHpGauge(GAUGE_FOE, 0);         // and let a watcher see it
        Log::Field("FieldNavigation: [BGBTL] foe HP reached %d -- WIN called from HP; "
                   "attacks vetoed and HP pinned so it cannot be taken back", fCur);
        ResumeNarration("the round is won -- the rescue scene is worth hearing");
        ScreenReader::Speak("You win.", true);
        return;
    }
    if (!s_playerDown && s_sawPlayerAlive && sCur <= 0) {
        s_playerDown = true;
        Log::Field("FieldNavigation: [BGBTL] Squall reached %d -- loss called from HP", sCur);
        ResumeNarration("the round is lost");
        ScreenReader::Speak("You are down.", true);
        return;
    }

    // v0.20.128: ONE ANNOUNCEMENT PER OUTCOME. In the 2026-08-15 log the loss
    // was called at 16:13:06 ("You are down.") and the very next poll said
    // "You 0" -- the same fact, a second later, in a voice that sounds like it
    // is still reporting a live fight. Once the round is decided there is
    // nothing left to report.
    if (s_playerDown || s_foeDown) return;

    const bool sChanged = (ps >= 0 && ps != s_pctSquall);
    const bool fChanged = (pf >= 0 && pf != s_pctFoe);
    if (!sChanged && !fChanged) return;

    Log::Field("FieldNavigation: [BGBTL-HP] Squall %d/%d (%d%%)  Foe %d/%d (%d%%)",
               sCur, sMax, ps, fCur, fMax, pf);
    if (sChanged) s_pctSquall = ps;
    if (fChanged) s_pctFoe    = pf;

    char line[64];
    if (sChanged && fChanged) snprintf(line, sizeof(line), "You %d, Foe %d", ps, pf);
    else if (sChanged)        snprintf(line, sizeof(line), "You %d", ps);
    else                      snprintf(line, sizeof(line), "Foe %d", pf);
    CueReport(line);
}

// -------------------------------------------------------------- the pause
//
// Five things were competing for one pair of ears at the exact moment the
// player has to learn the controls. Speaking louder or shorter does not fix
// that; removing four of the five does.

// v0.20.123: THE FREEZE IS GONE, AND IT HAD TO GO FOR THE PICTURE.
//
// Since v0.20.106 the briefing paused this scene with a one-byte RET at
// field_main's entry. Window rendering is field_main -> 0x00471010 ->
// 0x0052BC00 -> 0x004A0880, so that RET also guaranteed no dialog box could
// ever be drawn, or even finish opening. Aaron asked for the Game Controls to
// be visible; that is not compatible with the pause as it was built.
//
// The briefing now pauses the FIGHT rather than the FIELD, using the mechanism
// the F9 skip proved on 2026-08-15: **the soldier's attack REQs are vetoed and
// both HP values are pinned.** Nothing reaches Squall, nothing makes a sound,
// and the game keeps running -- which draws the box, keeps the movie playing,
// and lets the key learner read the real button word instead of the engine-side
// stand-in it needed while everything was stopped.
//
// It also deletes a whole class of danger: there is no longer any patched byte
// to restore, so no heartbeat, no guard thread, and no way for a crash in the
// mod to leave the player's game stranded.

static const char* BRIEF_TEXT =
    "Garden battle. Squall fights the soldier hand to hand, and the round is "
    "won by whoever has more health when time runs out. "
    "Hold your block key down. The mod keeps the block up while you hold it, "
    "and says blocked on every hit you stop. "
    "Three blocks in a row arm a heavy punch that takes most of the soldier's "
    "health. The mod will tell you when it is ready and which key throws it. "
    "Two of those win the fight. "
    "Tap each of your action keys now and I will name them. "
    "Press Enter to start. Space repeats this, and during the fight Space turns "
    "the block hold off and on. "
    "F9 wins the fight for you and stops the soldier.";

// The screen copy names the player's real keys wherever the learner has them,
// and falls back to the action name where it does not. Rebuilt every time the
// briefing opens, because a key learned during one attempt should show up in
// the next one's box.
static void BuildBriefScreenText()
{
    char pk[24], kk[24], bk[24], hk[24];
    CopyKeyName(pk, sizeof(pk), BTN_PUNCH);
    CopyKeyName(kk, sizeof(kk), BTN_KICK);
    CopyKeyName(bk, sizeof(bk), BTN_BLOCK);
    CopyKeyName(hk, sizeof(hk), BTN_HEAVY);
    // v0.20.124: SHORTER, AND PARKED AT THE TOP. Aaron's 15:00:56 screenshot
    // shows the box working and nothing clipped by it -- but the game's OWN
    // legend window (the small "W Punch / A Block / X Kick" panel it draws at
    // the lower left) lands on top of two of our lines and hides them. Six
    // lines instead of eleven, no blank spacers, and pinned to the top of the
    // screen, leaves that corner and the two HP bars alone.
    snprintf(s_briefScreen, sizeof(s_briefScreen),
             "GARDEN BATTLE\n"
             "Block %s   Punch %s\n"
             "Kick %s   Heavy punch %s\n"
             "Hold Block. 3 blocks in a row\n"
             "arm the Heavy punch. 2 of those win.\n"
             "Enter start   Space repeat   F9 skip",
             bk, pk, kk, hk);
}

static void SpeakBriefing()
{
    ScreenReader::Speak(BRIEF_TEXT, true);
    s_lastRemind = GetTickCount();
}

// THE FIGHT OWNS THE AUDIO CHANNEL, NOT THE MOVIE BEHIND IT.
//
// The whole fight plays over disc01_33h.avi, and that movie has an audio
// description track. In the 2026-08-15 log its cues landed in the middle of the
// round -- "Turquoise energy and fire flash; Galbadia Garden presses in at the
// edge." at 16:14:07, ONE SECOND before "Heavy punch ready" had to fight it for
// the same channel. Both are speech, both interrupt, and only one of them is
// something the player can act on.
//
// So narration is suppressed from the moment the briefing opens until the round
// is decided, and resumed the instant it is: a win, a loss, F9, or the module
// disarming. The cues are DROPPED rather than queued (fmv_audio_desc consumes
// them on their timestamp), so the rescue scene's own descriptions -- which are
// the ones worth hearing, and which all fall after the win -- still play.
static void ResumeNarration(const char* why)
{
    FmvAudioDesc::SetSuppressed(false);
    Log::Field("FieldNavigation: [BGBTL] movie narration resumed (%s)", why);
}

// Opens the briefing and pauses the fight. ONE briefing per attempt: the game
// re-shows its legend at every phase change, and v0.20.106 re-opened the
// briefing on each one -- twice in the middle of a live fight. A fresh attempt
// (a pass through the Game Over screen) clears the flag; nothing else does.
static void OpenBriefing(const char* why)
{
    if (s_briefing || s_briefedThisAttempt) return;
    s_briefCount++;
    s_briefing           = true;
    s_briefedThisAttempt = true;
    s_briefStart         = GetTickCount();
    s_awaitRelease       = false;
    s_needKeyUp          = true;      // see the note in Update()
    s_lastBriefPoll      = 0;
    for (int i = 0; i < 4; i++) s_lastLearnSpoke[i] = 0;
    FmvAudioDesc::SetSuppressed(true);
    BuildBriefScreenText();
    s_pausedThisAttempt = OpenBriefDialog(s_briefScreen);
    SpeakBriefing();
    Log::Field("FieldNavigation: [BGBTL] briefing %d opened by %s (attacks vetoed, "
               "HP pinned, dialog %s)",
               s_briefCount, why, s_pausedThisAttempt ? "shown" : "NOT shown");
}

// Ends the briefing and hands the fight back. One place, so the veto, the box
// and the narration suppression can never get out of step.
static void EndBriefing(const char* why, bool announce)
{
    if (!s_briefing) return;
    s_briefing = false;
    s_awaitRelease = false;
    CloseBriefDialog();
    // NOT ResumeNarration() -- see the note above it. The round is starting, not
    // ending, and the movie's description track is the loudest thing competing
    // with the block cue. Disarm(), the win and the loss all resume it.
    Log::Field("FieldNavigation: [BGBTL] briefing ended after %lums (%s)",
               (unsigned long)(GetTickCount() - s_briefStart), why);
    // Restart the clock so REQ offsets read from the start of the FIGHT rather
    // than from the arm, and so the 5-minute arm cap does not inherit however
    // long the player spent on the briefing.
    s_enterTick = GetTickCount();
    if (announce) ScreenReader::Speak("Game start.", true);
}

// ------------------------------------------------------------------ install

static bool Install()
{
    if (s_installed) return true;
    if (FF8Addresses::pExecuteOpcodeTable == nullptr) {
        Log::Field("FieldNavigation: [BGBTL] cannot install -- opcode table not resolved");
        return false;
    }
    uint32_t* table = FF8Addresses::pExecuteOpcodeTable;
    uint32_t* entry = &table[OPCODE_REQ];
    s_origReq = *entry;

    DWORD oldProtect = 0;
    if (!VirtualProtect(entry, sizeof(uint32_t), PAGE_READWRITE, &oldProtect)) {
        Log::Field("FieldNavigation: [BGBTL] VirtualProtect failed (err=%lu)", GetLastError());
        return false;
    }
    *entry = (uint32_t)(uintptr_t)&HookedReq;
    DWORD restore = 0;
    VirtualProtect(entry, sizeof(uint32_t), oldProtect, &restore);

    s_installed = true;
    s_ringRead = s_ringWrite;
    Log::Field("FieldNavigation: [BGBTL] REQ hook installed: [0x014] 0x%08X -> 0x%08X",
               s_origReq, (uint32_t)(uintptr_t)&HookedReq);
    return true;
}

static void Uninstall()
{
    if (!s_installed) return;
    uint32_t* entry = &FF8Addresses::pExecuteOpcodeTable[OPCODE_REQ];
    DWORD oldProtect = 0;
    if (VirtualProtect(entry, sizeof(uint32_t), PAGE_READWRITE, &oldProtect)) {
        *entry = s_origReq;
        DWORD restore = 0;
        VirtualProtect(entry, sizeof(uint32_t), oldProtect, &restore);
    }
    s_installed = false;
    Log::Field("FieldNavigation: [BGBTL] REQ hook removed");
}

// The skip. F9 once makes the fight unloseable and stops the soldier; F9 twice
// leaves the scene. Split out at 85 KB -- see the file's own header.
#include "field_minigame_bgbtl_skip.inl"

// ------------------------------------------------------------ field changes

// The arm. Called from the legend text (the real start) and from the field id
// (a backstop, ~15 s later). Idempotent -- the second one is a no-op.
static void Arm(const char* why)
{
    if (s_inMinigame) return;
    s_inMinigame     = true;
    s_enterTick      = GetTickCount();
    s_cuesSuppressed = 0;
    s_blockCueUntil  = 0;
    s_lastCue        = 0;
    s_pctSquall      = -1;
    s_pctFoe         = -1;
    s_foeDown        = false;
    s_wonAnnounced   = false;
    s_playerDown     = false;
    s_sawFoeAlive    = false;
    s_sawPlayerAlive = false;
    s_skipActive     = false;
    s_lastHpPoll     = 0;
    s_streak         = -1;
    s_heavyAnnounced = false;
    s_vetoed         = 0;
    s_sawGameOver        = false;
    s_briefedThisAttempt = false;
    s_heavyArmed         = false;
    s_reached152     = false;
    s_bestStreak     = 0;
    s_fmvSkipAsked   = false;
    s_hostField      = FF8Addresses::pCurrentFieldId
                     ? *FF8Addresses::pCurrentFieldId : 0xFFFF;
    Install();
    Log::Field("FieldNavigation: [BGBTL] ARMED by %s (host field %u, cue mode: %s)",
               why, (unsigned)s_hostField, s_useTone ? "tone" : "speech");
    // Pause the FIGHT first, THEN talk: the soldier's attacks are vetoed and
    // both HP values pinned while the briefing plays, so nothing can hit Squall
    // while he is listening, and the ducker drops the music on its own because
    // we are speaking.
    s_briefedThisAttempt = false;
    OpenBriefing(why);
    PollHealth("armed");
}

static void Disarm(uint16_t fieldId, const char* why)
{
    if (!s_inMinigame) return;
    EndBriefing("disarming", false);      // never leave the veto in place
    s_inMinigame = false;
    ResumeNarration("disarming");     // never leave the player's descriptions off
    Uninstall();
    PollHealth("on-exit");
    Log::Field("FieldNavigation: [BGBTL] disarmed by %s (field %u, %d cues suppressed)",
               why, fieldId, s_cuesSuppressed);
    Log::Field("FieldNavigation: [BGBTL] BLOCK SUMMARY: %d attacks cued, %d BLOCKED "
               "(Squall's guard flag rose: %s), best streak reached %d, hold assist %s, "
               "%d briefing(s), %d attack(s) vetoed, dialog box %s",
               s_attacks, s_guards, s_guardSeen ? "YES" : "NEVER",
               s_bestStreak, s_assist ? "ON" : "OFF",
               s_briefCount, s_vetoed, s_pausedThisAttempt ? "shown" : "not shown");
    {
        // One buffer per name. v0.20.119 called NameForMask twice per mask into
        // a four-slot pool and printed "punch=W kick=X block=W heavy=X" -- four
        // labels, two values, and it read as a mapping collision that was not
        // there.
        char kp[24], kk[24], kb[24], kh[24];
        CopyKeyName(kp, sizeof(kp), BTN_PUNCH);
        CopyKeyName(kk, sizeof(kk), BTN_KICK);
        CopyKeyName(kb, sizeof(kb), BTN_BLOCK);
        CopyKeyName(kh, sizeof(kh), BTN_HEAVY);
        Log::Field("FieldNavigation: [BGBTL] KEYS LEARNED: punch=%s kick=%s "
                   "block=%s heavy=%s", kp, kk, kb, kh);
    }
    // THE ONLY HONEST WIN SIGNAL. Foe HP reaching zero is not one -- see the
    // note in PollHealth. This transition is.
    // Only if the fight did not already say so -- from the foe reaching zero,
    // or from F9. Reaching Galbadia Garden is the last possible moment to
    // notice, not the right one.
    if (fieldId == FIELD_VICTORY && !s_wonAnnounced)
        ScreenReader::Speak("You win.", true);
    s_skipActive = false;
    s_hostField = 0xFFFF;
}

// The real trigger: the game's own on-screen legend. It lands ~15 s before the
// mini-game's field loads, which is why v0.20.102 and .103 both opened the
// fight late. Fed from field_dialog's show_dialog hook.
static void OnLegendText()
{
    if (!s_inMinigame) {
        Arm("the game's own Punch/Block/Kick legend");
        return;
    }
    // The legend re-appears at every phase change. OpenBriefing is a no-op
    // unless this is a fresh attempt, which is the whole fix for v0.20.106's
    // briefing re-opening twice mid-fight.
    OpenBriefing("the legend on a fresh attempt");
}

// LAST LINE OF DEFENCE, plus the earliest possible arm. Called from
// PollBattlePauseResume, which sits ABOVE Update()'s IsOnField() and
// HasFieldStateArrays() early-returns -- GardenBattle::Update() owns the
// un-pause and lives BELOW them, so if either gate ever went false while the
// field was frozen the briefing's own cap would never run.
//
// It also arms on the FMV. v0.20.106 armed on the legend text and found Squall
// ALREADY AT 440 OF 600 -- he takes a quarter of his health in the seven
// seconds between the movie starting and the legend appearing, before the
// player is told anything. disc01_32h.avi is the Garden-battle movie and plays
// nowhere else, so its name is a safe and much earlier trigger.
// Split out because MSVC will not allow __try in a function that needs object
// unwinding, and std::string needs unwinding (C2712, v0.20.109). It does not
// want SEH anyway: this reads FmvSkip's own state through a normal C++ call,
// not engine memory through a raw pointer, so there is nothing here for a
// structured handler to catch.
static bool IsGardenBattleAvi()
{
    const std::string avi = FmvSkip::GetCurrentAviName();
    return avi == ARM_AVI;
}

static void FreezeWatchdog()
{
    if (s_inMinigame) return;

    // The name outlives the playback, so latch it: without this the module
    // would re-arm the instant the fight disarmed and freeze the game again.
    if (!IsGardenBattleAvi()) { s_aviLatched = false; return; }
    if (s_aviLatched) return;
    s_aviLatched = true;
    Arm("the Garden-battle FMV starting");
}

static void OnFieldLoaded(uint16_t fieldId)
{
    if (!s_inMinigame) {
        // Backstop only. By the time this fires the fight is already running.
        if (fieldId == FIELD_MINIGAME) Arm("field 152 (backstop -- legend was missed)");
        return;
    }
    if (IsFightField(fieldId)) {
        if (fieldId == FIELD_GAMEOVER) {
            s_sawGameOver = true;
            Log::Field("FieldNavigation: [BGBTL] still armed across the Game Over screen");
            return;
        }
        // v0.20.122: **AFTER A GAME OVER, "TRY AGAIN" PUTS YOU BACK IN THE
        // HALLWAY, NOT INTO THE FIGHT.** Aaron: "you are put back in the
        // B-Garden hallway before the game starts. All of the mini-game
        // accessibility machinery needs to stop and wait to turn on again when
        // the mini-game begins."
        //
        // v0.20.121 briefed on the host field's RELOAD, which in the 14:10 log
        // is 14:10:35 -- fifty-three seconds before the battle FMV starts at
        // 14:11:28. He confirmed the briefing and then walked a hallway with
        // the REQ hook live, the assist pinning a guard flag and cues armed.
        //
        // So the module now lets go completely and waits for the same signal
        // that armed it the first time: disc01_32h.avi. The AVI latch clears
        // itself as soon as that name stops being reported, so the movie
        // starting again re-arms and re-briefs exactly like attempt one.
        if (fieldId == FIELD_HOST && s_sawGameOver) {
            Disarm(fieldId, "back in the hallway -- waiting for the fight to start again");
            return;
        }
        Log::Field("FieldNavigation: [BGBTL] still armed across field %u", fieldId);
        return;
    }
    Disarm(fieldId, "leaving the fight");
}

static bool OnGameOverScreen(uint16_t fieldId) { return fieldId == FIELD_GAMEOVER; }
static bool IsArmed() { return s_inMinigame; }

// ------------------------------------------------------------------ update

static void Update()
{
    if (!s_inMinigame) return;
    const DWORD now = GetTickCount();

    // The briefing owns the scene until the player says otherwise. X is the
    // in-game confirm key; Enter is accepted too because it costs nothing and
    // is not a mini-game key.
    //
    // The resume waits for the key to be RELEASED. X is also Kick -- if we
    // thawed on the press, the frame after would read a held X and throw a
    // kick the player did not ask for.
    if (s_briefing) {
        const uint16_t fidNow = FF8Addresses::pCurrentFieldId
                              ? *FF8Addresses::pCurrentFieldId : 0xFFFF;
        // v0.20.121: **CONFIRM IS ENTER, NOT X.** Aaron: "pressing X confirms it
        // is the kick but also closes the Game Controls and starts the game."
        // He is right and it was always a conflict: X is mask 64, the kick, so
        // the key that dismissed the briefing was also one of the four the
        // briefing exists to teach. Enter is not a fight key.
        const bool confirm = (GetAsyncKeyState(VK_RETURN) & 0x8000) != 0;

        // v0.20.121: **THE RETRY BRIEFING WAS BEING DISMISSED BY THE KEYPRESS
        // THAT CAUSED IT.** Aaron: "I only heard Game Controls once -- on the
        // first attempt -- rather than at the start of each attempt." The log
        // is unambiguous: briefing 2 took a confirm 500 ms in, briefing 3 took
        // one after 93 ms. He was still holding X from choosing "Try again" on
        // the Game Over menu, the host field reloaded, the briefing opened into
        // a held key and ended before it had said a word.
        //
        // So a press only counts once the key has been SEEN UP since the
        // briefing opened. The first briefing is unaffected -- nothing is held
        // when a movie starts.
        if (s_needKeyUp) {
            s_awaitRelease = false;          // nothing counts until it is up
            if (!confirm) {
                s_needKeyUp = false;
                Log::Field("FieldNavigation: [BGBTL] confirm key released -- "
                           "the briefing will now accept it");
            }
        }

        // THE CALIBRATION STEP, and the reason the briefing is worth pausing
        // for at all. The four masks are PAD BITS; which physical key produces
        // each one lives in the 2013 wrapper's own input config, not in
        // FF8_EN.exe, so the mod cannot read it and must not guess -- guessing
        // is exactly how four earlier builds told Aaron to press the wrong
        // thing. The soldier's attacks are vetoed while the box is up, so the
        // player can tap every key safely and the mod names each one.
        {
            const uint32_t rose = LearnButtons();   // the field runs now
            if (rose) {
                for (int i = 0; i < 4; i++) {
                    const uint32_t m = s_learned[i].mask;
                    if (!(rose & m)) continue;
                    // v0.20.130: **ONE NAME PER PRESS, NOT ONE PER RISING EDGE.**
                    // Aaron's 17:57 briefing said "Punch, W." FIFTY-THREE TIMES
                    // -- eleven of them inside two seconds -- because a held key
                    // auto-repeats and every repeat is a fresh rising edge on
                    // the button word. Each one interrupted the last, so the
                    // sentence never finished. A deliberate re-tap is still
                    // worth confirming, so this debounces per mask rather than
                    // going silent once the binding locks.
                    if (now - s_lastLearnSpoke[i] < LEARN_REPEAT_MS) continue;
                    s_lastLearnSpoke[i] = now;
                    const char* key = KeyName(s_learned[i].vk);
                    char msg[64];
                    if (key) snprintf(msg, sizeof(msg), "%s, %s.", ActionForMask(m), key);
                    else     snprintf(msg, sizeof(msg), "%s.", ActionForMask(m));
                    ScreenReader::Speak(msg, true);
                    Log::Field("FieldNavigation: [BGBTL-LEARN] %s = mask %u, vk 0x%02X%s",
                               ActionForMask(m), m, s_learned[i].vk,
                               s_learned[i].locked ? " (locked)" : "");
                    if (m == BTN_BLOCK) PlayTone();
                }
            }
        }

        if (!s_awaitRelease && confirm && !s_needKeyUp) {
            s_awaitRelease = true;
            Log::Field("FieldNavigation: [BGBTL] confirm pressed %lums into the briefing "
                       "-- waiting for release",
                       (unsigned long)(now - s_briefStart));
        } else if (s_awaitRelease && !confirm) {
            EndBriefing("player confirmed", true);
        } else if (now - s_lastRemind > REMIND_MS && !ScreenReader::IsSpeaking()) {
            // Silence must never be mistakable for a hang. There is no cap
            // behind this -- the reminder simply repeats until the player is
            // ready, which is the point.
            s_lastRemind = now;
            ScreenReader::Speak("Tap your action keys and I will name them. "
                                "Press Enter to start, Space to hear the controls "
                                "again.", true);
        }
        // v0.20.123: THE ONE THING THE FREEZE USED TO BUY THAT THE VETO DOES
        // NOT. Without the field_main pause the movie behind the fight keeps
        // playing, so an unread briefing can run the round's clock down. The
        // round resolves at 580; this ends the briefing well before that and
        // says why, rather than letting the player lose to a reading speed.
        if (s_briefing) {
            bool cok = false;
            const uint32_t c = ReadClock(&cok);
            if (cok && fidNow == FIELD_MINIGAME && c > CLOCK_BRIEF_LIMIT) {
                Log::Field("FieldNavigation: [BGBTL] briefing ended by the round clock "
                           "(%u past %u)", c, (unsigned)CLOCK_BRIEF_LIMIT);
                EndBriefing("the round is about to be decided", false);
                ScreenReader::Speak("Starting now.", true);
            }
        }
        if (s_briefing) return;      // nothing else runs while the box is up
    }

    const uint16_t fid = FF8Addresses::pCurrentFieldId
                       ? *FF8Addresses::pCurrentFieldId : 0xFFFF;

    LearnButtons();

    // The assist runs BEFORE the state watcher, so a guard it pins this tick is
    // the guard the watcher reports and the guard squ_hpcalc0 reads.
    if (!s_skipActive) ApplyBlockAssist(fid);

    WatchStreak();
    if (s_announceBlock) { s_announceBlock = false; CueReport("Blocked."); }

    // The skip hold runs EVERY tick -- a 250 ms poll would let a hit through,
    // and one hit at low HP is the whole difference.
    SkipTick();
    HoldHeavyArmed(fid, s_lastReqLabel);

    // Drain the ring. Everything is logged; only some of it is spoken.
    while (s_ringRead != s_ringWrite) {
        const ReqEvent e = *(const ReqEvent*)&s_ring[s_ringRead & (RING_SIZE - 1)];
        s_ringRead++;
        const bool attack = IsSoldierAttack(fid, e.label);
        s_lastReqLabel = e.label;
        if (attack) {
            s_attacks++;
            CueBlock();
        }
    }

    // Health, four times a second. Damage that arrives without a REQ we
    // recognise still gets announced.
    if (now - s_lastHpPoll > 250) {
        s_lastHpPoll = now;
        PollHealth(nullptr);
    }
}

// Player toggle: speech cue vs tone cue. The reaction window is 700 ms, which
// a one-word cue can just about fit and a tone fits comfortably -- so which
// one Aaron can actually act on is still his call, not ours.
static void ToggleCueMode()
{
    // While the briefing is up there are no cues to toggle, so F9 repeats it.
    // A blind player who missed a word should not have to guess or restart.
    if (s_briefing) {
        SpeakBriefing();
        s_briefStart = GetTickCount();      // the cap restarts with the speech
        s_awaitRelease = false;
        Log::Field("FieldNavigation: [BGBTL] briefing repeated on request");
        return;
    }
    // v0.20.121: Space, not F9 -- F10 is the Windows menu key so the skip moved
    // to F9, and the block hold moved to the key that was already "repeat the
    // controls" while the briefing was up. One key, two jobs, neither of which
    // collides with a fight button.
    // v0.20.119: mid-fight, this is the block hold. The cue mode was settled two
    // BATs ago -- Aaron picked the tone and has not asked for speech since --
    // whereas the hold is a real accessibility choice someone might want to
    // make either way, and it is the only thing in this scene worth a key.
    s_assist = !s_assist;
    ScreenReader::Speak(s_assist ? "Block hold on." : "Block hold off.", true);
    Log::Field("FieldNavigation: [BGBTL] block hold assist -> %s",
               s_assist ? "ON" : "OFF");
}

}  // namespace GardenBattle

// Fed every decoded field-dialog string by field_dialog's show_dialog hook.
// Three strstr calls and out -- this runs on every line of dialogue in the
// game, so it does nothing else. "Punch", "Block" and "Kick" together in one
// window is the mini-game's legend and appears nowhere else.
void GardenBattleOnDialogText(const char* text)
{
    if (!text) return;
    if (!strstr(text, "Punch")) return;
    if (!strstr(text, "Block")) return;
    if (!strstr(text, "Kick"))  return;
    GardenBattle::OnLegendText();
}
