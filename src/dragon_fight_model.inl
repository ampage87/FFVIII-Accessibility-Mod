// dragon_fight_model.inl -- the PURE model of the Laguna vs. dragon mini-game
// (field `tvglen3`, Trabia Canyon, the movie shoot).
//
// Included from field_navigation.cpp before field_minigame_dragon.inl, and
// compiled standalone by tests/dragon_fight_compile.cpp. Nothing here touches
// game memory, Windows or the screen reader.
//
// ============================================================================
// THE SCENE, AS ITS OWN SCRIPT DEFINES IT (v0.43.0, #105)
// ============================================================================
//
// Aaron: *"There is a mini-game where Laguna fights a dragon... Laguna can
// attack and block. There are two gauges for Laguna's and the dragon's health.
// The player is going to need an audio notification when to block just like we
// did in the punch mini-game."*
//
// The scene is `tvglen3`, and its `.sym` names every part of it in romaji:
// `laguna` has **kougeki** (attack), **bougyo** (defend), **damage** and
// **taiki** (standby); `dragon` has **kougeki**, **damage** and **gaoo** (roar);
// and a referee entity called **hantei** (judgement) owns the input loop.
//
// THE INPUT LOOP -- `hantei::check`, dwords 1410-1428, is four instructions:
//
//     PSHN_L 0x80 / BTNTEST(0x6D)  -> REQ(laguna, script 37 = bougyo)   HELD
//     PSHN_L 0x10 / BTNTEST(0x6E)  -> REQ(laguna, script 38 = kougeki)  PRESSED
//     JMP back
//
// `0x6D` is the LEVEL test and `0x6E` the edge test, so **block is a hold and
// attack is a press** -- the same shape, and the same two masks, as the Garden
// battle in field_minigame_bgbtl.inl (block 0x80, and 0x10 there is the punch).
//
// THE FOUR VARIABLES -- field variables at `0x01CFE9B8 + index`:
//
//     var[1028]  0x01CFEDBC   1 while the block button is held (laguna::bougyo
//                             sets it, loops while the button is down, clears)
//     var[1030]  0x01CFEDBE   1 while the fight is running
//     var[1031]  0x01CFEDBF   LAGUNA'S HEALTH   120, minus 40 a hit  -> 3 hits
//     var[1032]  0x01CFEDC0   THE DRAGON'S      120, minus 12 a hit  -> 10 hits
//
// and `OP_0x13B(gauge, value)` is what draws them: `(0, var[1031])` is Laguna's
// bar and `(1, var[1032])` the dragon's. Losing rolls an AASK on message 27
// (retry or quit) and, on retry, writes 120 back into both.
//
// **THE WARNING.** `dragon::default` is the whole fight in thirty dwords:
//
//     loop:  rnd = RANDOM
//            if rnd < 128:  WAIT 15 ; again          (50%)
//            if rnd < 170:  WAIT  8 ; again          (16%)
//            ANIME(47, 1) on channel 2               <- the attack
//            if var[1028] == 0:  REQ(laguna, damage) <- the hit
//            ANIME(98, 48)                           <- recovery
//
// The block flag is read **after** the animation, and opcode `0x30`'s handler
// (`0x00526810`) blocks: on its first execution it pops the two arguments and
// returns 1 -- bit 1 clear, so the dispatcher does not advance the instruction
// pointer -- and returns 3 only once the animation has finished. So the window
// the player gets is exactly the length of animation 47, and it opens at the
// instant that opcode first runs. `ANIME(47, 1)` on channel 2 appears **twice
// in the whole field**, in `dragon::default` and in `dragon::kougeki`, and both
// are this attack; nothing else in `tvglen3` uses it.
//
// That is the cue, and it is a TONE rather than speech for the same reason the
// Garden battle's is: the block has to be a key already held when the check
// happens, so the useful signal is "hold it now", which a rising two-note tone
// delivers in 180 ms and a sentence does not.
// ============================================================================

namespace DragonFightModel {

// ---- the field -------------------------------------------------------------
static const char* const DF_FIELD = "tvglen3";

// ---- the attack signature --------------------------------------------------
// Opcode 0x30 = ANIME. `operand` is the channel; the two arguments sit on the
// VM stack, first-pushed at [ctx + sp*4 - 4] and top at [ctx + sp*4].
static const int DF_OPCODE_ANIME  = 0x030;
static const int DF_ATTACK_CHAN   = 2;
static const int DF_ATTACK_ANIM   = 47;
static const int DF_ATTACK_SPEED  = 1;

// ---- the VM context --------------------------------------------------------
static const int DF_VMCTX_SP        = 0x184;  // signed byte
static const int DF_VMCTX_FLAG_BIT  = 0x174;  // which bit of +0x175 is "first"
static const int DF_VMCTX_FLAGS     = 0x175;
static const int DF_VMCTX_IP        = 0x176;  // dword index of the instruction
// Opcode 0xE7's handler (`0x005264A0`) is four instructions: pop one word and
// store it at [ctx + 0x208]. `dragon::default` writes 32 there before a third of
// its attacks and 16 after every one, so **that byte is what makes an attack
// quick** -- the same ANIME(47,1) played at double speed, which is exactly the
// 828 ms / 1610 ms pair the cue-to-hit log measured.
static const int DF_VMCTX_SPEED     = 0x208;
static const int DF_SPEED_FAST      = 32;
static const int DF_SPEED_NORMAL    = 16;

// ---- the variables ---------------------------------------------------------
static const unsigned DF_VAR_BASE   = 0x01CFE9B8;
static const int DF_VAR_BLOCKING    = 1028;
static const int DF_VAR_ACTIVE      = 1030;
static const int DF_VAR_LAGUNA_HP   = 1031;
static const int DF_VAR_DRAGON_HP   = 1032;

static const int DF_HP_FULL        = 120;
// The window, measured. The 2026-08-21 BAT logged **828 ms to 1610 ms** from the
// cue to the hit -- two clusters, the long and the short wind-up. That is why
// the outcome deadline below is 2600 ms and not the 2000 ms v0.43.0 guessed at,
// and it is also why speech WOULD fit here where it could not in the Garden
// battle's 133 ms. The tone stays anyway: the useful signal is "hold it now".
static const unsigned DF_OUTCOME_MS_V = 2600;

// v0.46.0. **A BRIEFING THAT CAN HANG THE FIGHT IS A TRAP**, and the 2026-08-21
// log is what one looks like: Laguna goes down at 20:51:48, the retry re-opens
// the Game Controls box at 20:51:58, two keys get named -- and then the log is
// SILENT for seventy seconds until the field changes. The box holds the guard
// flag pinned and swallows every cue while it is up, so the fight ran on with
// nothing announced and nothing able to hurt Laguna: exactly *"after I lost the
// first time I no longer heard any sound cues."*
//
// Three things follow, and only the third is a timeout:
//
//   * the box opens ONCE PER VISIT. A player who just died is mid-flow and did
//     not ask to be taught the controls again;
//   * a restart says so instead, in one line;
//   * and if the box is ever up this long with nothing pressed, it closes
//     itself. Nothing this module does may leave the fight unplayable.
static const unsigned DF_BRIEF_TIMEOUT_MS = 45000;

// After the box closes, the guard stays pinned for one full wind-up. An attack
// that started BEHIND the box would otherwise land on a player who never heard
// a cue for it -- which is the `**hit with no cue outstanding**` at 20:51:38.
static const unsigned DF_GRACE_MS = DF_OUTCOME_MS_V;
static const int DF_LAGUNA_HIT     = 40;   // three hits and the scene is lost
static const int DF_DRAGON_HIT     = 12;   // ten hits and it is won

// The two masks `hantei::check` tests, which are also two of the four the Garden
// battle learns keys for -- so the names come from that learner rather than from
// a table here.
static const unsigned DF_MASK_BLOCK  = 0x80;   // BTNTEST 0x6D, a LEVEL -- hold
static const unsigned DF_MASK_ATTACK = 0x10;   // BTNTEST 0x6E, an EDGE -- tap

// A health byte the fight has not set up yet is not a health byte. The first
// BAT caught `laguna=216 dragon=0` before the scene wrote 120/120, and v0.43.0
// took the 216 as a starting point and then reported the drop to 120 as a HIT.
static bool DfHpValid(int hp) { return hp >= 0 && hp <= DF_HP_FULL; }
static bool DfFightReady(int lagunaHp, int dragonHp, int active)
{
    return active == 1 && DfHpValid(lagunaHp) && DfHpValid(dragonHp);
}
// Both sides at full is the scene handing control over: `dic::defalut` writes
// 120/120 to start it and `laguna::damage` writes the same pair on a retry.
static bool DfFightStarting(int lagunaHp, int dragonHp, int active)
{
    return active == 1 && lagunaHp == DF_HP_FULL && dragonHp == DF_HP_FULL;
}

static unsigned DfVarAddr(int index) { return DF_VAR_BASE + (unsigned)index; }

// Hits a side can still take. Rounded UP, because a remainder is still a hit
// the player has to survive or land.
static int DfHitsLeft(int hp, int perHit)
{
    if (perHit <= 0) return 0;
    if (hp <= 0) return 0;
    return (hp + perHit - 1) / perHit;
}

static bool DfIsAttackAnime(int channel, int argA, int argB)
{
    return channel == DF_ATTACK_CHAN &&
           argA == DF_ATTACK_ANIM && argB == DF_ATTACK_SPEED;
}

// **THE ATTACK'S OWN FULL STOP (v0.47.0).** Both attack scripts end the same
// way: block check, damage REQ, then `ANIME(98, 48)` on the same channel 2 --
// `dragon::default` dword 685, `dragon::kougeki` dword 759, and nothing else in
// the field uses that pair. It starts at the instant the check has been made,
// so it is the exact resolution marker, and unlike a timer it cannot be wrong.
//
// It has to be, because **the wind-up is interruptible.** The 2026-08-21 log:
//
//     21:13:11  ip=531 ch=2 (47,1) speed=32   ATTACK (quick)
//     21:13:12  ip=638 ch=5 (42,1)            <- dragon::damage: Laguna hit it
//     21:13:13  ip=717 ch=5 (85,43)              and preempted the wind-up
//     21:13:14  "Blocked."                    <- FALSE. the 2600 ms timer fired
//     21:13:15  ip=545 ch=2 (98,48)           <- the real end of the attack
//     21:13:15  "Hit. Laguna, 2 hits left."
//
// A quick attack that should have landed in 828 ms took nearly four seconds
// because the player hit the dragon during it, and a fixed deadline announced a
// block that never happened and then a hit with no cue left to pair it with.
static const int DF_RECOVER_ANIM  = 98;
static const int DF_RECOVER_SPEED = 48;
static bool DfIsRecoveryAnime(int channel, int argA, int argB)
{
    return channel == DF_ATTACK_CHAN &&
           argA == DF_RECOVER_ANIM && argB == DF_RECOVER_SPEED;
}

// v0.48.0: WHICH attack that recovery ends. Both attack scripts have the same
// shape -- ANIME(47,1), block check, damage REQ, ANIME(98,48) -- and the two
// animations sit FOURTEEN dwords apart in both of them (`dragon::default`
// offline 671 and 685, `dragon::kougeki` 745 and 759; runtime 531/545 and
// 605/619). So a recovery names its own attack: the cue whose instruction
// pointer is exactly this far in front of it.
//
// Without that, the 2026-08-21 log paired them by age and got both wrong at
// once -- `21:42:04 recovery ip=619` was charged to the ip=531 cue four seconds
// old ("hit 3969 ms after the cue") and the ip=605 cue then took the ip=545
// recovery twelve seconds later. The two attacks overlap often enough that
// oldest-first is a coin toss, and the gap is a property of the script rather
// than of the load address, so it holds wherever the field loads.
static const int DF_RECOVER_IP_GAP = 14;

// After the recovery starts, the damage script still has to run -- it is a REQ
// at priority 6, not an inline call -- so the health drop lands a beat later.
// In the log the two are inside the same second; this is that beat with room.
static const unsigned DF_RESOLVE_MS = 1000;

// And a backstop: a cue whose recovery is never seen at all must not sit in the
// queue forever. It expires silently, because by then nothing true can be said
// about it.
static const unsigned DF_CUE_MAX_MS = 15000;

// One pending outcome per cue in flight. `resolveAt` is set when the attack's
// own recovery animation starts -- see DfIsRecoveryAnime -- and nothing is
// called blocked until then.
struct PendingCue { uint32_t at; uint32_t resolveAt; bool fast; uint16_t ip; };
static const int DF_CUE_SLOTS = 4;
static PendingCue s_cues[DF_CUE_SLOTS];

static void DfCueClear()
{
    for (int i = 0; i < DF_CUE_SLOTS; i++) {
        s_cues[i].at = 0; s_cues[i].resolveAt = 0; s_cues[i].fast = false;
        s_cues[i].ip = 0;
    }
}
static void DfCuePush(uint32_t now, bool fast, uint16_t ip)
{
    if (!now) now = 1;
    for (int i = 0; i < DF_CUE_SLOTS; i++)
        if (!s_cues[i].at) { s_cues[i].at = now; s_cues[i].resolveAt = 0;
                             s_cues[i].fast = fast; s_cues[i].ip = ip; return; }
    s_cues[0].at = now; s_cues[0].resolveAt = 0; s_cues[0].fast = fast;
    s_cues[0].ip = ip;
}
static int DfCueOldest(bool unresolvedOnly)
{
    int best = -1;
    for (int i = 0; i < DF_CUE_SLOTS; i++) {
        if (!s_cues[i].at) continue;
        if (unresolvedOnly && s_cues[i].resolveAt) continue;
        if (best < 0 || (int32_t)(s_cues[i].at - s_cues[best].at) < 0) best = i;
    }
    return best;
}
// v0.48.0: the cue this recovery is the end of, by instruction pointer rather
// than by age. `matched` says whether the script named it or whether this fell
// back to oldest-first, because a fallback that is not visible is a signature
// change that looks like working code.
static int DfCueForRecovery(uint16_t recovIp, bool* matched)
{
    if (matched) *matched = false;
    int best = -1;
    for (int i = 0; i < DF_CUE_SLOTS; i++) {
        if (!s_cues[i].at || s_cues[i].resolveAt) continue;
        if ((int)s_cues[i].ip + DF_RECOVER_IP_GAP != (int)recovIp) continue;
        if (best < 0 || (int32_t)(s_cues[i].at - s_cues[best].at) < 0) best = i;
    }
    if (best >= 0) { if (matched) *matched = true; return best; }
    return DfCueOldest(true);
}
// Is there a cue in flight that is NEWER than this one? Then the player is
// still holding for that one, and "Blocked." would be heard as permission to
// let go. The 2026-08-21 log has exactly that: `21:41:40` a quick cue rings,
// the previous cue's block is announced a beat later, and the hit lands.
static bool DfCueNewerLive(int self)
{
    for (int i = 0; i < DF_CUE_SLOTS; i++) {
        if (i == self || !s_cues[i].at) continue;
        if ((int32_t)(s_cues[i].at - s_cues[self].at) > 0) return true;
    }
    return false;
}
// The cue this health drop belongs to, consumed. The damage script runs right
// after the block check, so the cue whose recovery fired most recently is the
// one that landed -- not the oldest, which is a different attack still in the
// air. 0 when there is none, which is itself worth logging.
static uint32_t DfCueTakeForHit()
{
    int best = -1;
    for (int i = 0; i < DF_CUE_SLOTS; i++) {
        if (!s_cues[i].at || !s_cues[i].resolveAt) continue;
        if (best < 0 || (int32_t)(s_cues[i].resolveAt - s_cues[best].resolveAt) > 0)
            best = i;
    }
    if (best < 0) best = DfCueOldest(false);
    if (best < 0) return 0;
    const uint32_t at = s_cues[best].at;
    s_cues[best].at = 0;
    s_cues[best].resolveAt = 0;
    s_cues[best].ip = 0;
    return at;
}

// What to say when a side's health moves. Hits rather than points: the bars
// carry no numbers, and "two hits left" is the thing being decided.
static void DfHealthLine(bool laguna, int hp, char* out, size_t n)
{
    out[0] = '\0';
    const int per  = laguna ? DF_LAGUNA_HIT : DF_DRAGON_HIT;
    const int left = DfHitsLeft(hp, per);
    const char* who = laguna ? "Laguna" : "Dragon";
    if (left <= 0)      snprintf(out, n, "%s down.", who);
    else if (left == 1) snprintf(out, n, "%s, one hit left.", who);
    else                snprintf(out, n, "%s, %d hits left.", who, left);
}

// The readout bound to "/" while this field is loaded.
static void DfStatusLine(int lagunaHp, int dragonHp, bool blocking,
                         char* out, size_t n)
{
    snprintf(out, n, "Laguna %d hits left. Dragon %d hits left. Block %s.",
             DfHitsLeft(lagunaHp, DF_LAGUNA_HIT),
             DfHitsLeft(dragonHp, DF_DRAGON_HIT),
             blocking ? "held" : "not held");
}

}  // namespace DragonFightModel

// ============================================================================
// THE GAME CONTROLS BOX (#105, v0.67.2)
// ============================================================================
//
// Aaron: *"We want to be consistent whenever we implement a Game Controls
// dialog prior to the start of a mini-game."* The Garden battle and the space
// rescue both put theirs in one of FF8's own dialog windows; this fight was
// spoken and nothing more.
//
// SEPARATE FROM THE SPOKEN BRIEF, on purpose. The spoken version can afford to
// be a paragraph; the box cannot. FF8's window is 320 px wide and its own
// measurer sizes the box to the text, so past about 34 columns a line wraps and
// past ten lines the box SCROLLS -- and a box that scrolls has lost its opening
// lines by the time anyone reads it. The format lives here, in the pure model,
// so the probe can measure it without a Trabia Canyon.
//
// The two %s are the block and attack key names, which the learner supplies at
// run time and which are three characters at the very most.
// The two key names get a line each. The first draft put them on one --
// "Hold %s block   Tap %s attack" -- and the probe measured it at THIRTY-FIVE
// columns the moment it was given a five-letter key name like "Space", which is
// one past the wrap. A line each is inside the window whatever the learner
// comes back with, and reads better besides.
static const char* DF_SCREEN_FMT =
    "DRAGON FIGHT\n"
    "Hold %s to block.\n"
    "Tap %s to attack.\n"
    "Rising two note tone: it is\n"
    "winding up. Hold block.\n"
    "3 hits and Laguna is down.\n"
    "Enter start   / health   F9 skip";

// The box's limits, from AMES's own clamps and the row arithmetic at
// 0x004A07DB (rows that fit = (height - 12) / 16).
static const int DF_SCREEN_COLS  = 34;
static const int DF_SCREEN_LINES = 10;

// Widest line and line count of an already-formatted screen.
static void DfScreenMeasure(const char* s, int* outCols, int* outLines)
{
    int cols = 0, run = 0, lines = 1;
    for (const char* p = s; p && *p; p++) {
        if (*p == '\n') { if (run > cols) cols = run; run = 0; lines++; continue; }
        run++;
    }
    if (run > cols) cols = run;
    if (outCols) *outCols = cols;
    if (outLines) *outLines = lines;
}
