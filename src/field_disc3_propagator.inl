// field_disc3_propagator.inl -- #112, the Ragnarok Propagator puzzle.
// PART OF field_disc3.inl. Do NOT compile standalone.
//
// v0.66.0 REWRITE. Aaron played the v0.55.0 version and asked for something
// quite different:
//
//   *"We don't want to simply announce when entering the field. We also don't
//    want to proactively inform the player where to find it's pair. Instead we
//    want the NPC in the catalog to say 'Red Propagator', 'Purple Propagator',
//    etc. We also need to prevent the Propagator from moving when the player
//    and the Propagator are on the same field, otherwise the player is going to
//    get caught while using navigation tools in the mod."*
//
// Three changes, and they pull in the same direction: the mod should make the
// hunt POSSIBLE to a blind player, not perform it for him.
//
//   1. NOTHING IS ANNOUNCED ON ARRIVAL. Walking into a room and being told what
//      is in it, where its pair is and what to do about it is not a hunt. The
//      colour now reaches the player the same way every other thing in a room
//      does -- through the catalog, as "Red Propagator" (PgCatalogName).
//
//   2. THE PAIR'S LOCATION IS ANSWERED, NEVER VOLUNTEERED. The rule ("the next
//      kill must be the other green one") is still spoken when the board
//      changes, because the consequence of a kill is otherwise invisible until
//      you walk back into a room and find the thing standing there again. WHERE
//      that other one is comes only from the help key.
//
//   3. THEY ARE HELD STILL. See PgHoldDecide in propagator_model.inl for the
//      mechanism and for why it is a bubble rather than a permanent freeze.

namespace Props {

// Derived ids (field_disc3.inl explains the derivation and the id-OR-name
// rule). rg block = +74: rgair1 821 .. rgroad3 844.
static const uint16_t RG_ID_LO = 821, RG_ID_HI = 845;

static bool  s_on = false;
static uint8_t s_lastDead = 0xFE;     // impossible, so the first read seeds
static bool  s_lastPending = false;
static char  s_lastField[32] = "";

// --- the hold ---------------------------------------------------------------
static bool     s_holding    = false;  // are we pinning the speed right now?
static int      s_holdIdx    = -1;     // the live entity we are pinning
static uint16_t s_saveCur    = 0;      // its last non-zero +0x1FE
static uint16_t s_saveWalk   = 0;      // its last non-zero +0x200
static bool     s_saveValid  = false;
static int      s_loggedIdx  = -2;     // last identity logged (-2 = none yet)
static int      s_loggedHow  = 0;      // ...and which key answered (0 = none yet)
static DWORD    s_holdStart  = 0;      // when the current hold began, for the silent-veto check
static bool     s_silentWarned = false;
// How long a hold may run with every hook live and the veto never firing before
// the mod says out loud that it is probably holding the wrong entity. Three
// seconds: long enough that a Propagator idling between patrol legs cannot trip
// it, short enough to appear in the same breath as the hold in a BAT log.
static const DWORD PG_SILENT_VETO_MS = 3000;
static uint16_t s_nameFieldId = 0xFFFF;// the RAW field id when the NAME last changed
static bool     s_engaged     = false; // he released this one: his fight now
static bool     s_reachCued   = false; // the "press Confirm" cue, once per approach
static bool     s_confirmWas  = false; // Confirm edge detection

// ---------------------------------------------------------------------------
// v0.68.0: THE PIN LOST A RACE, SO IT STOPPED RACING
// ---------------------------------------------------------------------------
// Aaron's first run at the Propagators: *"Suppression didn't seem to work as I
// stood still and each one ran for me it seemed."* The log agrees with him
// exactly, in every field:
//
//   16:25:10  holding ent2 still at 2595 units (speed 7929/7929 saved)
//   16:25:12  released ent2 (he walked into range)
//
// He did not walk into range. Two seconds after the hold went on, the distance
// had fallen from 2595 to under 600 -- about 930 units a second, which is the
// charge speed of 8000 divided by the engine's own >> 8. The Propagator crossed
// the room while the mod was holding it still.
//
// WHY. Writing [ent+0x1FE] once per mod tick is a race against a script that
// re-issues its speed every few frames, and the mod's tick is not a frame. Every
// tick we wrote a zero; in between, the script wrote 8000 back and the engine
// spent the gap walking. The v0.66.0 comment even says the pin "is a race with
// the script and the loser of that race is the player" -- it was right, and the
// player lost.
//
// SO STOP RACING AND VETO AT THE SOURCE. The mod already patches the field
// opcode table for the Trabia dragon's ANIME hook; the same one line does it
// here. Opcode 0x03D is "set this entity's move speed" (0x005233E0, writes
// +0x1FE and +0x200) and 0x079 is MOVEAPPROACH, which copies +0x200 into +0x1FE
// when a move starts. Both receive the ENTITY'S OWN VM CONTEXT as their first
// argument -- the same block the speed lives in -- so the hook does not have to
// guess who is calling. It lets the original run, and if the caller is the
// Propagator we are holding, it puts the zero back in the same frame, before
// the movement pass can read it.
//
// That is frame-exact by construction, and it cannot be outrun by a script
// however often it loops.
//
// The hooks stay installed for the session. With s_heldBlock at zero they are a
// pointer compare and a tail call -- the same shape the dragon's hook has worn
// through every BAT since v0.43.0.
static const int PG_OPCODE_SPEED    = 0x03D;   // set move speed
static const int PG_OPCODE_APPROACH = 0x079;   // move toward an entity
// v0.70.0: and the one that actually starts the fight.
//
// Aaron, after the 00:02 run: *"Looks like the Propagator battles trigger just
// by walking up to them, pressing Confirm isn't required. Can we make it
// required? This would give blind players the same ability as sighted players
// to dodge them."*
//
// The log shows exactly that, and shows why v0.69.0's Confirm gate could not
// have stopped it: at 00:02:59 the Propagator is 213 units away, still held --
// "speed reads 0/0, veto fired 464 times" -- and the battle starts anyway. The
// movement veto stops the monster MOVING. It does nothing about the monster's
// SCRIPT, which is still running its loop, and that loop ends in
//
//     94 PSHN_L2 1 / 95 op 0x05B / 96 PSHL 0 / 97 JMPB 76   ; am I touching him?
//     98 PSHN_L 85 / 99 PSHN_L 1 / 100 BATTLE               ; then fight
//
// Holding a monster still and then walking into it is still walking into it.
//
// WHY NOT VETO THE BATTLE OPCODE ITSELF. Because everything AFTER it in that
// script is the pairing bookkeeping -- var[445], var[446], var[447], var[437] --
// and a BATTLE that returns without fighting drops the script straight into the
// code that marks the Propagator dead. Vetoing 0x069 would record a kill that
// never happened, and the puzzle would be unsolvable in a way the player could
// not see. So the veto goes one instruction earlier, on the QUESTION rather
// than the answer: while a Propagator is held, it is told it is not touching
// anybody, and its script goes round the loop again without ever reaching the
// battle. Nothing is faked -- "not touching" is exactly true of a monster the
// player has not yet said yes to.
static const int PG_OPCODE_TOUCH = 0x05B;   // ISTOUCHING(entity) -> local 0

// THE ARMED BLOCK, AND THE TWO DIFFERENT THINGS DONE TO IT.
//
// s_heldBlock arms the CONTACT veto -- the thing that decides whether a
// Propagator may touch him before he has said yes. Every gated Propagator is
// armed, including the two that never move.
//
// s_pinSpeed additionally arms the MOVEMENT veto. Only the five that actually
// move need it, and running it on the two that do not would be a write into an
// entity whose script never asks for a speed in the first place.
//
// They were one flag until the 12:30 BAT, and rgroad1 is what that cost:
// Aaron, "Found another one that activated without me pressing X." It had been
// standing perfectly still, running ISTOUCHING sixty times a second, and the
// mod had decided a monster with nothing to freeze was a monster with nothing
// to do.
static volatile uintptr_t s_heldBlock = 0;     // the block whose contact is refused, 0 = none
static volatile bool      s_pinSpeed  = false; // ...and is its movement pinned too?
static uint32_t s_origSpeedOp    = 0;
static uint32_t s_origApproachOp = 0;
static uint32_t s_origTouchOp    = 0;
static bool     s_hooksInstalled = false;
static volatile long s_hookVetoes = 0;         // how often the veto has fired
static volatile long s_hookAsked  = 0;         // ...and the last speed asked for

static void PgZeroIfHeld(void* ctx)
{
    const uintptr_t held = s_heldBlock;
    if (!held || !s_pinSpeed || (uintptr_t)ctx != held) return;
    // Through CTX, not through s_heldBlock. They are equal by the test above, and
    // writing through the argument keeps them equal: a later edit that loosened
    // that guard would then corrupt the caller it was handed rather than quietly
    // going on pinning the right entity for the wrong reason.
    const uintptr_t at = (uintptr_t)ctx;
    __try {
        volatile uint16_t* cur  = (volatile uint16_t*)(at + PG_OFF_SPEED_CUR);
        volatile uint16_t* walk = (volatile uint16_t*)(at + PG_OFF_SPEED_WALK);
        const uint16_t asked = *cur ? *cur : *walk;
        if (asked) s_hookAsked = (long)asked;
        *cur = 0;
        *walk = 0;
        s_hookVetoes = s_hookVetoes + 1;
    } __except (EXCEPTION_EXECUTE_HANDLER) { }
}

static int __cdecl PgHookedSpeed(void* ctx, int param)
{
    const int r = ((OpcodeFunc_t)s_origSpeedOp)(ctx, param);
    PgZeroIfHeld(ctx);
    return r;
}

static int __cdecl PgHookedApproach(void* ctx, int param)
{
    const int r = ((OpcodeFunc_t)s_origApproachOp)(ctx, param);
    PgZeroIfHeld(ctx);
    return r;
}

// The VM's local slots live at ctx + 0x140 + n*4 (PSHL/POPL, 0x0051CAB0). The
// touch opcode leaves its answer in local 0 -- which is why the script's very
// next instruction is PSHL 0.
static const uintptr_t PG_OFF_LOCAL0 = 0x140;
static volatile long s_touchVetoes = 0;

static void PgRefuseContactIfHeld(void* ctx)
{
    const uintptr_t held = s_heldBlock;
    if (!held || (uintptr_t)ctx != held) return;
    __try {
        *(volatile int32_t*)((uintptr_t)ctx + PG_OFF_LOCAL0) = 0;   // not touching
        s_touchVetoes = s_touchVetoes + 1;
    } __except (EXCEPTION_EXECUTE_HANDLER) { }
}

static int __cdecl PgHookedTouch(void* ctx, int param)
{
    const int r = ((OpcodeFunc_t)s_origTouchOp)(ctx, param);
    PgRefuseContactIfHeld(ctx);
    return r;
}

// ============================================================================
// THE PASSENGER COMPARTMENT, AND THE FOURTH HOOK (#112, v0.75.0)
// ============================================================================
// Aaron, after the 12:30 BAT: *"Let's try to freeze the one in the passenger
// compartment as well. I know that one is a bit different but it is jarring the
// way it works right now."*
//
// It is different, and the difference is real: rgguest2's Propagator is not a
// patrol and not a chase, it is a CUTSCENE. Its whole script is one straight
// run -- and the order of it is what makes this solvable:
//
//     [7]  0x04E            <- event start; sets the "a script is running" byte
//     [8]  PSH 840; 0x176
//     [15] PSH 9000; 0x03D  <- charge speed
//     [20] ...; 0x07C       <- the charge itself
//     [26] PSH 8; 0x03C     <- an eight-FRAME wait, not a wait for the move
//     [42] 0x0F2 / 0x0F5    <- screen
//     [44] ...; 0x0EE/0x0EF <- the fade
//     [53] PSH 816; 0x069   <- BATTLE
//     [58] ... the pair bookkeeping ...
//     [113] MAPJUMP3 -> rgroad2
//
// WHY THE MOVEMENT VETO CANNOT BE USED HERE. Two reasons, and the second one is
// the one that matters. The wait at [26] is a FRAME count, not a wait for the
// move to finish, so pinning the speed would stop the monster walking and the
// script would arrive at BATTLE about a fifth of a second later anyway. And the
// fade at [44] runs before the battle, so anything held after that point is
// held behind a white screen with the player's control already taken away.
//
// SO THE HOLD GOES AT THE FIRST INSTRUCTION INSTEAD OF THE LAST. The engine's
// script VM has its own idiom for "not yet": an opcode handler returns 1 and
// the executor re-runs the SAME instruction next frame, exactly as MOVEWAIT
// (0x07D) does -- `cmp word [ctx+0x21E], 2; sete al; inc eax`, and the loop at
// 0x005235A7 turns that into "re-execute" or "advance". Nothing is faked and
// nothing is skipped: the script simply has not got to instruction 7 yet.
//
// So while the yellow one is alive and he has not pressed Confirm, this module
// answers 1 for its 0x04E and the cutscene has not started. No control lock, no
// music change, no charge, no fade. He walks around a normal room, finds the
// terminal, hears the briefing that is the entire point of that room -- and
// when he walks up to the Propagator and presses Confirm, the deferral stops,
// 0x04E runs, and the scene plays exactly as Squaresoft wrote it.
//
// WHY THIS ONE IS DECIDED INSIDE THE HOOK RATHER THAN ARMED BY THE TICK. Every
// other Propagator is armed by Update(), which needs the debounced field NAME
// and is therefore seconds late. Seconds late is fine for a monster that has to
// walk across a room; it is useless here, because this script starts on the
// first frame of the field. So the test is made from things that are true
// immediately: the RAW field id (which does not lag), the entity's own model id
// in its live block, and var[446]. Nothing has to have been resolved first.
// SETMODEL's parameter, as the live block carries it. Used by the guest gate
// below and by PgResolveByModel further down.
static const uintptr_t PG_OFF_MODEL_ID   = 0x218;
static const int      PG_OPCODE_EVENT   = 0x04E;   // event start; the cutscene's first instruction
static const int      PG_VM_RERUN       = 1;       // "ask me again next frame" (exec loop 0x0052363C)
static const uint16_t PG_GUEST_FIELD_ID = 832;     // rgguest2
static const int      PG_GUEST_MODEL    = 2;       // its alien01's SETMODEL parameter
static const uint8_t  PG_GUEST_BIT      = 0x80;    // its bit in var[446]

static uint32_t s_origEventOp = 0;
static volatile bool s_guestReleased = false;   // he pressed Confirm; let the scene run
static volatile long s_guestDefers   = 0;       // how many frames it has been held at [7]

// Is THIS ctx the passenger compartment's Propagator, right now, with the
// cutscene still to be consented to? Every term is readable on frame one.
static bool PgDeferGuestCutscene(void* ctx)
{
    if (s_guestReleased) return false;
    if (D3FieldId() != PG_GUEST_FIELD_ID) return false;
    __try {
        if (*(volatile int16_t*)((uintptr_t)ctx + PG_OFF_MODEL_ID) != PG_GUEST_MODEL) return false;
        // Already dead: its script takes the skip branch and never reaches
        // instruction 7 at all, but deferring a dead monster's event would be a
        // room that never finishes loading, so the test is made explicitly.
        if ((*(volatile uint8_t*)PG_ADDR_DEAD & PG_GUEST_BIT) != 0) return false;
    } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    s_guestDefers = s_guestDefers + 1;
    return true;
}

static int __cdecl PgHookedEvent(void* ctx, int param)
{
    // BEFORE the original, not after. This is the one hook that must not let
    // the engine's handler run at all -- 0x04E's whole job is to set the byte
    // at 0x01CE4903 that takes the player's control away, and a deferral that
    // takes his control away first has deferred nothing worth having.
    if (PgDeferGuestCutscene(ctx)) return PG_VM_RERUN;
    return ((OpcodeFunc_t)s_origEventOp)(ctx, param);
}

static bool PgPatchOpcode(int op, void* fn, uint32_t* saveOrig)
{
    uint32_t* entry = &FF8Addresses::pExecuteOpcodeTable[op];
    *saveOrig = *entry;
    DWORD oldProtect = 0;
    if (!VirtualProtect(entry, sizeof(uint32_t), PAGE_READWRITE, &oldProtect)) return false;
    *entry = (uint32_t)(uintptr_t)fn;
    DWORD restore = 0;
    VirtualProtect(entry, sizeof(uint32_t), oldProtect, &restore);
    return true;
}

// A HOOK IS NOT A THING YOU INSTALL ONCE. The 2026-08-24 log is the proof:
// before the forced fight in the passenger compartment the counters read
// "veto fired 27 times ... contact refused 12 times"; after it, every single
// hold reported "veto fired 0 times ... contact refused 0 times" and he was
// attacked twice on walk-up without pressing Confirm. Zero is not a hold that
// lost a race -- a hold that loses a race still fires. Zero means the hooks
// were not being called at all, because the battle -> field return rebuilds
// pExecuteOpcodeTable and puts the engine's own handlers back, and the old
// s_hooksInstalled latch made that permanent: it had been true since the first
// field, so nothing ever re-patched.
//
// So the latch is gone and the question is asked instead of remembered. Each
// tick, compare the three entries against our own addresses. An entry that is
// not ours is the engine's -- save THAT as the original and patch over it.
// The equality test is what keeps this safe to run every tick: our address can
// never be saved as the original, so no re-install can ever build a hook that
// calls itself.
static bool PgEnsureHook(int op, void* fn, uint32_t* saveOrig)
{
    uint32_t* entry = &FF8Addresses::pExecuteOpcodeTable[op];
    if (*entry == (uint32_t)(uintptr_t)fn) return false;    // still ours, nothing to do
    PgPatchOpcode(op, fn, saveOrig);
    return true;                                            // it had been taken back
}

static volatile long s_hookReinstalls = 0;

static void PgVerifyHooks()
{
    if (FF8Addresses::pExecuteOpcodeTable == nullptr) {
        static bool warned = false;
        if (!warned) {
            warned = true;
            Log::Field("FieldNavigation: [PROPAGATOR] cannot veto movement -- the opcode "
                       "table is not resolved. The per-tick pin is all there is, and the "
                       "2026-08-23 run showed that is not enough");
        }
        s_hooksInstalled = false;
        return;
    }
    const bool a = PgEnsureHook(PG_OPCODE_SPEED,    (void*)&PgHookedSpeed,    &s_origSpeedOp);
    const bool b = PgEnsureHook(PG_OPCODE_APPROACH, (void*)&PgHookedApproach, &s_origApproachOp);
    const bool c = PgEnsureHook(PG_OPCODE_TOUCH,    (void*)&PgHookedTouch,    &s_origTouchOp);
    const bool d = PgEnsureHook(PG_OPCODE_EVENT,   (void*)&PgHookedEvent,    &s_origEventOp);
    if (!(a || b || c || d)) return;                        // the common case: all four ours

    const bool first = !s_hooksInstalled;
    s_hooksInstalled = true;
    if (!first) s_hookReinstalls = s_hookReinstalls + 1;
    Log::Field("FieldNavigation: [PROPAGATOR] movement veto %s (speed=%d approach=%d touch=%d, "
               "event=%d, re-installs so far %ld): [0x03D] 0x%08X -> 0x%08X, [0x079] 0x%08X -> 0x%08X",
               first ? "installed" : "**RE-INSTALLED after the table was restored**",
               (int)a, (int)b, (int)c, (int)d, s_hookReinstalls,
               s_origSpeedOp, (uint32_t)(uintptr_t)&PgHookedSpeed,
               s_origApproachOp, (uint32_t)(uintptr_t)&PgHookedApproach);
    Log::Field("FieldNavigation: [PROPAGATOR] contact veto: [0x05B] 0x%08X -> 0x%08X "
               "-- a held Propagator is told it is not touching him, so its script "
               "never reaches the BATTLE two instructions later",
               s_origTouchOp, (uint32_t)(uintptr_t)&PgHookedTouch);
    Log::Field("FieldNavigation: [PROPAGATOR] passenger-compartment gate: [0x04E] 0x%08X -> 0x%08X "
               "-- the cutscene's own first instruction answers \"not yet\" until he presses "
               "Confirm, so the room stays a room",
               s_origEventOp, (uint32_t)(uintptr_t)&PgHookedEvent);
}

static void PgInstallHooks() { PgVerifyHooks(); }

// How many of the three table entries are ours RIGHT NOW. The diagnostic line
// under a hold used to report only how often the veto had fired, which reads
// the same -- zero -- whether the hook is missing or simply has not been asked
// yet. This says which.
static int PgHooksLive()
{
    if (FF8Addresses::pExecuteOpcodeTable == nullptr) return 0;
    int n = 0;
    if (FF8Addresses::pExecuteOpcodeTable[PG_OPCODE_EVENT]     == (uint32_t)(uintptr_t)&PgHookedEvent)     ++n;
    if (FF8Addresses::pExecuteOpcodeTable[PG_OPCODE_SPEED]    == (uint32_t)(uintptr_t)&PgHookedSpeed)    ++n;
    if (FF8Addresses::pExecuteOpcodeTable[PG_OPCODE_APPROACH] == (uint32_t)(uintptr_t)&PgHookedApproach) ++n;
    if (FF8Addresses::pExecuteOpcodeTable[PG_OPCODE_TOUCH]    == (uint32_t)(uintptr_t)&PgHookedTouch)    ++n;
    return n;
}

static void ForgetHold(const char* why);    // defined below

static void Reset()
{
    // FORGET, do not restore. See ForgetHold.
    ForgetHold("left the Ragnarok");
    s_on = false; s_lastDead = 0xFE; s_lastPending = false; s_lastField[0] = '\0';
    s_loggedIdx = -2; s_loggedHow = 0; s_nameFieldId = 0xFFFF;
    s_guestReleased = false; s_guestDefers = 0;
    s_engaged = false; s_reachCued = false; s_confirmWas = false;
}

// NAME ONLY, deliberately -- unlike Space, which needs the id.
//
// v0.55.0: this used to be "name OR id in 821..845". The id range is DERIVED
// (field_disc3.inl explains how), and a derived range that is wrong does not
// merely fail to fire -- it fires somewhere else, and the failure mode is the
// mod announcing Propagator kill counts in the middle of an unrelated scene
// for the rest of the game. Nothing here is reflex-timed: routes, colours and
// pair status are all fine arriving with pCurrentFieldName's 2-5 second lag.
// So the id buys nothing worth that risk and is only LOGGED, which is what
// turns one BAT into a confirmed derivation.
static bool InRagnarok()
{
    if (!PgIsRagnarokField(D3FieldName())) return false;
    const uint16_t id = D3FieldId();
    if (id < RG_ID_LO || id > RG_ID_HI) {
        static bool s_warned = false;
        if (!s_warned) {
            s_warned = true;
            Log::Field("FieldNavigation: [PROPAGATOR] '%s' is id %u, OUTSIDE the derived "
                       "range %u..%u -- the rg-block derivation is wrong; the feature still "
                       "works (it is name-driven) but fix the range",
                       D3FieldName(), (unsigned)id, (unsigned)RG_ID_LO, (unsigned)RG_ID_HI);
        }
    }
    return true;
}

static bool ReadState(uint8_t* dead, uint8_t* pendBit, bool* pending)
{
    uint8_t d = 0, pb = 0, pf = 0;
    if (!D3ReadU8(D3VarAddr(PG_VAR_DEAD), &d)) return false;
    if (!D3ReadU8(D3VarAddr(PG_VAR_PENDBIT), &pb)) return false;
    if (!D3ReadU8(D3VarAddr(PG_VAR_PENDFLAG), &pf)) return false;
    *dead = d; *pendBit = pb; *pending = (pf & PG_PENDING_MASK) != 0;
    return true;
}

// ---------------------------------------------------------------------------
// The live entity, and the two words that decide whether it walks.
// ---------------------------------------------------------------------------

// The Others block for a live entity index, or nullptr.
static uint8_t* PgBlock(int liveIdx)
{
    if (liveIdx < 0 || liveIdx >= MAX_ENTITIES) return nullptr;
    if (!FF8Addresses::pFieldStateOthers) return nullptr;
    __try {
        uint8_t* base = *reinterpret_cast<uint8_t**>(FF8Addresses::pFieldStateOthers);
        if (!base) return nullptr;
        return base + ENTITY_STRIDE * (DWORD)liveIdx;
    } __except (EXCEPTION_EXECUTE_HANDLER) { return nullptr; }
}

static bool PgReadSpeeds(int liveIdx, uint16_t* cur, uint16_t* walk)
{
    uint8_t* b = PgBlock(liveIdx);
    if (!b) return false;
    __try {
        *cur  = *(volatile uint16_t*)(b + PG_OFF_SPEED_CUR);
        *walk = *(volatile uint16_t*)(b + PG_OFF_SPEED_WALK);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

static bool PgWriteSpeeds(int liveIdx, uint16_t cur, uint16_t walk)
{
    uint8_t* b = PgBlock(liveIdx);
    if (!b) return false;
    __try {
        *(volatile uint16_t*)(b + PG_OFF_SPEED_CUR)  = cur;
        *(volatile uint16_t*)(b + PG_OFF_SPEED_WALK) = walk;
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

// WHICH LIVE ENTITY IS THE PROPAGATOR.
//
// THE LIVE INDEX IS NOT THE SCRIPT SLOT, and rgroad3 is where assuming it was
// cost a BAT. Aaron: "the Propagator in Aisle 6 still attacked without pressing
// confirm." The live Others array holds only the entities the scene actually
// instantiated, compacted, so a model-less script object takes no place in it.
// rgroad3 puts the model-less `dic` at slot 2, ahead of alien01 and alien02, so
// alien02's script slot is 4 and its live index is 3 -- and the mod spent that
// whole room holding `dp01`, the draw point, perfectly still.
//
// It read back its own zeros and reported "speed reads 0/0" the entire time.
// The number that told the truth was the veto count, which only rises when the
// REAL entity's script runs an opcode: 0 on rgroad3, hundreds everywhere else.
//
// So the model id is asked first. SETMODEL's parameter is written by the
// entity's own script and the live block carries it at +0x218, so compaction
// cannot disturb it; a unique match is proof. The catalog's join is asked
// second -- it is the same key plus a position tiebreak, but it is built by the
// catalog refresh, which has not necessarily run on the tick this module first
// asks. The script slot is last, and it is now understood to be a guess.
static int PgLiveCount()
{
    if (!FF8Addresses::pFieldStateOtherCount) return 0;
    __try {
        const int n = (int)*FF8Addresses::pFieldStateOtherCount;
        return (n < MAX_ENTITIES) ? n : MAX_ENTITIES;
    } __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}

// Exactly one live block carrying this model id, or -1. TWO matches is not a
// tie to be broken here -- it is evidence the key is not unique in this field,
// and pinning the wrong one of two Propagator-shaped things is the failure this
// function exists to stop.
static int PgResolveByModel(int model)
{
    if (model < 0) return -1;
    const int n = PgLiveCount();
    int found = -1, hits = 0;
    for (int i = 0; i < n; i++) {
        uint8_t* b = PgBlock(i);
        if (!b) continue;
        int id = -1;
        __try { id = (int)*(volatile int16_t*)(b + PG_OFF_MODEL_ID); }
        __except (EXCEPTION_EXECUTE_HANDLER) { continue; }
        if (id != model) continue;
        if (hits == 0) found = i;
        hits++;
    }
    return (hits == 1) ? found : -1;
}

static int PgResolveLive(const Propagator* p)
{
    if (!p) return -1;
    const int byModel = PgResolveByModel(p->model);

    int byJoin = -1;
    for (int i = 0; i < MAX_ENTITIES; i++) {
        const FieldArchive::JSMEntityInfo* je = FindJSMByLiveEntity(i);
        if (je && je->symName[0] && _stricmp(je->symName, p->entity) == 0) { byJoin = i; break; }
    }

    int chosen; const char* how;
    if      (byModel >= 0) { chosen = byModel; how = "model id"; }
    else if (byJoin  >= 0) { chosen = byJoin;  how = "catalog join"; }
    else                   { chosen = p->slot; how = "**script slot -- a guess**"; }

    // THE LOG IS NOT LATCHED ON THE FIRST ANSWER. The model id needs the live
    // array populated and the join needs the catalog refresh to have run, so the
    // first tick in a field can legitimately fall through to the guess and the
    // second tick get it right. Latching printed the guess and hid the
    // correction. It now prints once per DISTINCT answer, so the sequence is
    // visible and a disagreement cannot go unread.
    // ...and when the KEY changes, not only the answer. rgroad1's slot happens
    // to equal its model rank, so the first tick logged "**script slot -- a
    // guess**" and the correction a tick later was silent -- leaving a reader
    // of that log to conclude the whole room ran on a guess. Which key replied
    // is the finding; the index is only where it pointed.
    const int howId = (byModel >= 0) ? 1 : (byJoin >= 0) ? 2 : 3;
    if (chosen != s_loggedIdx || howId != s_loggedHow) {
        s_loggedIdx = chosen;
        s_loggedHow = howId;
        Log::Field("FieldNavigation: [PROPAGATOR] '%s' %s is live entity %d by %s "
                   "(model %d -> %d, catalog join -> %d, script slot %d)%s",
                   p->field, p->entity, chosen, how,
                   p->model, byModel, byJoin, p->slot,
                   (byModel >= 0 && byJoin >= 0 && byModel != byJoin)
                       ? "  *** model and join disagree -- the model wins ***"
                       : (byModel >= 0 && byModel != p->slot)
                           ? "  (the script slot would have been wrong here)" : "");
    }
    return chosen;
}

// TWO WAYS TO STOP HOLDING, AND THE DIFFERENCE MATTERS.
//
// GiveBack writes the saved speed back into the entity. It is only ever called
// while the entity is still THERE: the player walked into reach, or the thing
// died. Forget writes nothing at all and is what happens when the field is
// leaving or gone.
//
// The distinction is not fussiness. The live Others array is rebuilt on every
// field load, and slot 2 in the next room is somebody else entirely -- so a
// "restore" that fires after the transition does not give a Propagator its
// speed back, it hands a stranger a number. And pCurrentFieldName, which this
// module keys on, lags the transition by seconds; the raw id at
// pCurrentFieldId does not, which is why the hold remembers the id it began
// under and drops everything the moment that changes. This is the same defence
// chase_kani_freeze needed for the same reason, and it learned it by crashing.
static void ForgetHold(const char* why)
{
    s_heldBlock = 0;            // the veto goes quiet first, always
    s_pinSpeed  = false;
    if (s_holding)
        Log::Field("FieldNavigation: [PROPAGATOR] forgetting the hold on ent%d (%s) "
                   "-- writing anything into that slot now would be writing into "
                   "whatever the next field put there", s_holdIdx, why ? why : "?");
    s_holding = false;
    s_holdIdx = -1;
    s_saveValid = false;
    s_saveCur = s_saveWalk = 0;
}

// THE CONFIRM KEY, read off the field button word the fight itself reads.
// Bit 5 is Circle and bit 6 is Cross in FF8's pad numbering -- on Aaron's
// keyboard D and X -- and either is taken as "yes", along with Enter, because
// which of the two a player thinks of as Confirm depends on the game he played
// last and getting it wrong here means a Propagator that cannot be fought.
static const uint32_t PG_ADDR_BUTTONS = 0x01CE48B0;
static const uint16_t PG_MASK_CIRCLE  = 0x0020;
static const uint16_t PG_MASK_CROSS   = 0x0040;

static bool PgConfirmDown()
{
    uint16_t w = 0;
    if (D3ReadU16(PG_ADDR_BUTTONS, &w) &&
        (w & (PG_MASK_CIRCLE | PG_MASK_CROSS)) != 0) return true;
    return (GetAsyncKeyState(VK_RETURN) & 0x8000) != 0;
}

static void GiveBackHold(const char* why)
{
    s_heldBlock = 0;            // stop vetoing before handing the speed back,
    s_pinSpeed  = false;
    if (!s_holding) return;     // or the hook undoes the restore

    const int idx = s_holdIdx;
    const uint16_t c = s_saveCur, w = s_saveWalk;
    const bool valid = s_saveValid;
    s_holding = false;
    if (idx >= 0 && valid) {
        PgWriteSpeeds(idx, c, w);
        Log::Field("FieldNavigation: [PROPAGATOR] released ent%d (%s): speed %u/%u given back",
                   idx, why ? why : "?", (unsigned)c, (unsigned)w);
    } else if (idx >= 0) {
        // A gate-only Propagator has no speed to hand back -- it never had one.
        // The release still gets a line, because "nothing was written" and
        // "nothing happened" look identical in a log that only reports writes.
        Log::Field("FieldNavigation: [PROPAGATOR] released ent%d (%s): contact is "
                   "its own again -- nothing to give back, this one never moved",
                   idx, why ? why : "?");
    }
}

// The distance between the party and the Propagator, in world units.
// False when either position is unavailable, which PgHoldDecide reads as
// "do not interfere".
static bool PgDistance(int liveIdx, long* out)
{
    float px = 0, py = 0, ex = 0, ey = 0;
    if (s_playerEntityIdx < 0) return false;
    if (!GetEntityPos(s_playerEntityIdx, px, py)) return false;
    if (!GetEntityPos(liveIdx, ex, ey)) return false;
    const double dx = (double)px - (double)ex, dy = (double)py - (double)ey;
    *out = (long)std::sqrt(dx * dx + dy * dy);
    // ZERO IS NOT A DISTANCE, IT IS A READING THAT HAS NOT HAPPENED YET.
    //
    // On the first tick in a room the entity array has been rebuilt and both
    // blocks can still read the same value, so the party and the monster come
    // out standing on the same point. Every gated field in the 20:25 log opens
    // with it -- "gating ent3 at 0 units (field id 840)" -- and one second
    // later that room announced "Green Propagator within reach. Press Confirm
    // to fight it." while the thing was across the corridor.
    //
    // The announcement is the harmless half. The same reading also satisfies
    // the release test, so a Confirm pressed on the frame he walks in -- which
    // is exactly when a player is still mashing through the last room's dialog
    // -- would un-gate a monster he has never seen, for the rest of the scene.
    //
    // Two entities cannot really occupy one point to the unit, and if they ever
    // did the cost of refusing to measure it is one frame: he shifts a unit and
    // the cue and the key both work. The cost of believing it is the feature.
    if (*out == 0) return false;
    return true;
}

static void DriveHold(const Propagator* here, uint8_t dead, bool confirmHit)
{
    // THE NAME AND THE ID MUST AGREE. This module keys on pCurrentFieldName,
    // which is debounced and arrives seconds after the transition;
    // pCurrentFieldId does not. So the id observed when the name last changed is
    // remembered, and the moment the live id walks away from it the name is
    // known to be stale -- the entity array has already been rebuilt, and both
    // holding and restoring would be operating on a different field's entities.
    // Nothing at all is written until the name catches up. chase_kani_freeze
    // needed exactly this defence, and learned it by crashing.
    const uint16_t nowId = D3FieldId();
    if (nowId != s_nameFieldId) {
        ForgetHold("the field id moved and the name has not caught up");
        return;
    }

    // GATE, not freezable. rgroad1 and rgair1 never move and were therefore
    // never touched by this module -- and both run ISTOUCHING into BATTLE while
    // standing still, which is how one of them took Aaron without a keypress.
    if (!here || !here->gate) { ForgetHold("not a field we gate"); return; }

    const int idx = PgResolveLive(here);
    if (idx < 0) { ForgetHold("no live entity"); return; }
    if (s_holdIdx != idx) { ForgetHold("entity changed"); s_holdIdx = idx; }

    // Remember the engine's own speed while it is still the engine's. Never
    // save a zero: releasing to zero would leave the entity stalled inside a
    // move its script is already blocked on, which is the one outcome worse
    // than not holding it at all.
    uint16_t cur = 0, walk = 0;
    const bool read = PgReadSpeeds(idx, &cur, &walk);
    if (here->freezable && read && !s_holding && (cur != 0 || walk != 0)) {
        if (cur  != 0) s_saveCur  = cur;
        if (walk != 0) s_saveWalk = walk;
        s_saveValid = (s_saveCur != 0 || s_saveWalk != 0);
    }

    long dist = 0;
    const bool posKnown = PgDistance(idx, &dist);

    // THE ONE THING THIS MODULE STILL VOLUNTEERS, and it is volunteered because
    // a held monster is invisible: it makes no sound, it does not move, and
    // without being told that Confirm is what starts the fight a player can
    // stand on top of one indefinitely. Once per approach, and it goes quiet
    // again when he walks away, so pacing past one does not chatter.
    if (s_holding && !s_engaged) {
        if (PgInReach(posKnown, dist)) {
            if (!s_reachCued) {
                s_reachCued = true;
                char cue[160];
                snprintf(cue, sizeof cue,
                         "%s Propagator within reach. Press Confirm to fight it.",
                         here->colour);
                if (cue[0] >= 'a' && cue[0] <= 'z') cue[0] = (char)(cue[0] - 'a' + 'A');
                D3Say("PROPAGATOR", cue, false);
            }
        } else if (s_reachCued && posKnown && dist > PG_REACH_UNITS + 200) {
            s_reachCued = false;      // hysteresis, so the edge does not chatter
        }
    }

    // WHAT THE ENTITY ACTUALLY READS, while we think we are holding it. The
    // 2026-08-23 run had a log full of confident "holding" lines and a
    // Propagator crossing the room underneath them; a hold that reports itself
    // rather than the thing it is holding is how that went unnoticed for a whole
    // scene. Once a second, no more.
    if (s_holding) {
        static DWORD s_lastDiag = 0;
        const DWORD now2 = GetTickCount();
        if (now2 - s_lastDiag >= 1000) {
            s_lastDiag = now2;
            Log::Field("FieldNavigation: [PROPAGATOR] held ent%d: speed reads %u/%u, "
                       "%ld units away, veto fired %ld time%s (last asked for %ld), "
                       "contact refused %ld time%s, cutscene deferred %ld frame%s, hooks live %d/4 (re-installs %ld)",
                       idx, (unsigned)cur, (unsigned)walk, dist,
                       (long)s_hookVetoes, s_hookVetoes == 1 ? "" : "s",
                       (long)s_hookAsked,
                       (long)s_touchVetoes, s_touchVetoes == 1 ? "" : "s",
                       (long)s_guestDefers, s_guestDefers == 1 ? "" : "s",
                       PgHooksLive(), (long)s_hookReinstalls);

            // A SILENT VETO IS THE SIGNATURE OF HOLDING THE WRONG THING, and
            // this line exists because the mod could not tell the difference
            // for two builds. A Propagator's script runs its move opcodes sixty
            // times a second, so if the hooks are all in the table and the veto
            // has still never fired after seconds of holding, we are not
            // holding a Propagator -- we are holding something that has no
            // script, reading back our own zeros, and reporting them as proof.
            // That is exactly what rgroad3 did while it pinned the draw point.
            // BOTH counters, because a gated-but-not-frozen Propagator never
            // trips the movement veto by design -- rgair1 and rgroad1 never ask
            // for a speed. What every Propagator does, moving or not, is run
            // ISTOUCHING sixty times a second. Testing only the movement veto
            // here would accuse those two on every single hold.
            if (s_hookVetoes == 0 && s_touchVetoes == 0 && s_guestDefers == 0 &&
                PgHooksLive() == 4 && s_holdStart != 0 &&
                (now2 - s_holdStart) >= PG_SILENT_VETO_MS && !s_silentWarned) {
                s_silentWarned = true;
                Log::Field("FieldNavigation: [PROPAGATOR] *** ent%d HAS RUN NO SCRIPT IN "
                           "%u ms WITH ALL FOUR HOOKS LIVE -- this is almost certainly "
                           "NOT the Propagator. '%s' expects model %d at script slot %d; "
                           "the speed reading 0/0 is this mod reading back its own write "
                           "to whatever entity %d actually is ***",
                           idx, (unsigned)(now2 - s_holdStart), here->field,
                           here->model, here->slot, idx);
            }
        }
    }

    switch (PgHoldDecide(here, dead, posKnown, dist, s_holding, confirmHit, s_engaged)) {
        case PG_HOLD_ON:
            // A MOVEMENT HOLD NEEDS SOMETHING TO GIVE BACK; A CONTACT GATE DOES
            // NOT. Waiting for a nonzero speed before arming was right while the
            // two were one flag, and wrong the moment they were not: rgair1's
            // and rgroad1's Propagators never set a speed at all, so waiting for
            // one there means waiting forever with the gate open.
            if (here->freezable && !s_saveValid) return;
            s_heldBlock = (uintptr_t)PgBlock(idx);   // arm the frame-exact veto
            s_pinSpeed  = here->freezable;
            if (!s_holding) {
                s_holding = true;
                s_hookVetoes = 0; s_hookAsked = 0; s_touchVetoes = 0; s_guestDefers = 0;
                s_holdStart = GetTickCount(); s_silentWarned = false;
                if (here->freezable)
                    Log::Field("FieldNavigation: [PROPAGATOR] holding ent%d still at %ld units "
                               "(speed %u/%u saved, field id %u) -- it cannot close on him "
                               "from there", idx, dist, (unsigned)s_saveCur,
                               (unsigned)s_saveWalk, (unsigned)nowId);
                else
                    Log::Field("FieldNavigation: [PROPAGATOR] gating ent%d at %ld units "
                               "(field id %u) -- this one never moves, so there is nothing "
                               "to pin; its contact test is refused until he presses Confirm",
                               idx, dist, (unsigned)nowId);
            }
            if (here->freezable) PgWriteSpeeds(idx, 0, 0);
            break;
        case PG_HOLD_OFF:
            s_engaged = true;      // and it stays his fight: nothing re-holds it
            // The passenger compartment's is released by letting its cutscene
            // start, not by handing a speed back. Same key, same reach, a
            // different thing on the other end of it.
            if (!s_guestReleased && D3FieldId() == PG_GUEST_FIELD_ID) {
                s_guestReleased = true;
                Log::Field("FieldNavigation: [PROPAGATOR] passenger-compartment cutscene "
                           "released after %ld deferred frames -- it plays from here exactly "
                           "as it was written", (long)s_guestDefers);
            }
            GiveBackHold("he pressed Confirm within reach");
            D3Say("PROPAGATOR", "Released.", false);
            break;
        case PG_HOLD_DEAD:
            s_engaged = true;
            GiveBackHold("it is dead");
            break;
        case PG_HOLD_NONE:
        default:
            break;
    }
}

static void Update(bool slash)
{
    if (!InRagnarok()) {
        if (s_on) { Log::Field("FieldNavigation: [PROPAGATOR] left the Ragnarok"); Reset(); }
        return;
    }
    if (!s_on) {
        s_on = true;
        // Assert the table once, where a BAT can see it. A pairing that does
        // not close is the one defect that would quietly teach the player the
        // wrong move.
        PgInstallHooks();
        Log::Field("FieldNavigation: [PROPAGATOR] aboard (field '%s' id %u); pair table %s",
                   D3FieldName(), (unsigned)D3FieldId(),
                   PgTableConsistent() ? "consistent" : "**INCONSISTENT -- do not trust it**");
    }

    // EVERY TICK, not once aboard. A battle -> field return puts the engine's
    // handlers back in the table; see PgVerifyHooks. Cheap: three compares, and
    // it only writes on the tick the table changed under us.
    PgVerifyHooks();

    uint8_t dead = 0, pendBit = 0; bool pending = false;
    if (!ReadState(&dead, &pendBit, &pending)) return;

    const char* nm = D3FieldName();
    const Propagator* here = PgForField(nm);
    const bool movedField = (nm && *nm && _stricmp(nm, s_lastField) != 0);
    if (movedField) {
        strncpy(s_lastField, nm, sizeof(s_lastField) - 1);
        s_lastField[sizeof(s_lastField) - 1] = '\0';
        // NOTHING IS SPOKEN HERE. v0.55.0 announced the colour, the rule and
        // the pair's location on every arrival; Aaron asked for the room to be
        // quiet and for the catalog to carry the colour instead. The log keeps
        // everything, because the log is where a BAT reads it.
        ForgetHold("new field");
        s_loggedIdx = -2; s_loggedHow = 0;
        s_guestReleased = false; s_guestDefers = 0;
        s_nameFieldId = D3FieldId();
        s_engaged = false; s_reachCued = false;   // a new room is a new approach
        Log::Field("FieldNavigation: [PROPAGATOR] '%s' dead=0x%02X pendBit=0x%02X pending=%d "
                   "here=%s (silent on arrival by design -- the catalog names it)",
                   nm, dead, pendBit, (int)pending, here ? here->colour : "-");
    }

    // HOLD IT STILL. Every tick, because the pin is a race with the script that
    // sets the speed and the loser of that race is the player.
    // Confirm, as an EDGE. A held key must not release one Propagator and then
    // the next one he walks up to.
    const bool confirmNow = PgConfirmDown();
    const bool confirmHit = confirmNow && !s_confirmWas;
    s_confirmWas = confirmNow;

    DriveHold(here, dead, confirmHit);

    // NOTHING IS SAID AFTER A KILL EITHER, as of v0.69.0. Aaron: *"scrap the
    // post-battle announcements... The whole point is the player is supposed to
    // listen to the terminal in the passenger compartment, which tells them to
    // kill them in matching color pairs. The current post-battle help text
    // essentially does this and makes the dialogue in the passenger compartment
    // redundant."*
    //
    // He is right, and it is the same mistake the arrival announcement was: the
    // game already teaches this puzzle, in its own words, at a terminal the
    // player is meant to find. A mod that recites the rule after every kill has
    // not made the game accessible, it has replaced the part of it that was
    // worth playing. The board is still TRACKED -- the state is read every tick
    // and the help key reports all of it -- it simply is not narrated.
    s_lastDead = dead; s_lastPending = pending;

    // THE HELP KEY IS WHERE THE LOCATIONS LIVE NOW. Asked for, not volunteered.
    if (slash) {
        char b[256], c[256];
        PgStatusLine(dead, pendBit, pending, b, sizeof(b), true);
        D3Say("PROPAGATOR", b, true);
        if (here) { PgHereLine(here, dead, pendBit, pending, c, sizeof(c)); D3Say("PROPAGATOR", c, false); }
        if (here && here->gate) {
            D3Say("PROPAGATOR",
                  !s_holding
                      ? "It is free -- you have already chosen to fight this one."
                      : here->freezable
                            ? "It is being held still. Walk up to it and press Confirm to fight it."
                            : "This one does not move. Walk up to it and press Confirm to fight it.",
                  false);
        }
    }
}

// ============================================================================
// ON HOLDING THEM STILL -- WHAT THIS BUILD ACTUALLY DOES
// ============================================================================
//
// v0.55.0 shipped without this and said why: the only mechanism it had found
// was the mode byte (`dic` writes var[1050], or var[1051] in rghang1, and mode
// 0 is a patrol whose battle branch is unreachable), and that mechanism covers
// four of the eight, races a script that rewrites the byte every one to two
// frames, and -- the part that matters most -- leaves a MOVING target. A
// Propagator that patrols is nearly as bad for a blind player as one that
// charges: the catalog's bearing is stale before he has finished walking it.
//
// v0.66.0 uses the engine's own per-entity move speed instead. It is better on
// every axis: it works in all five fields that move, it is one word rather than
// a race with a script, it stops the entity DEAD rather than slowing it, and it
// blocks the script inside its move so the contact test and the BATTLE after it
// are never reached. See the block comment above PgHoldDecide for the addresses
// and for why the hold is a bubble rather than a permanent freeze.
//
// WHAT IS STILL NOT WRITTEN, and will not be: var[446], var[447], var[445] bit
// 2 and var[437]. Those are the puzzle's answer, not its controls. The mod
// holds a monster still; it does not decide who is dead.
//
// AND WHAT IS DELIBERATELY LEFT ALONE. rgair1 and rgroad1 never move -- their
// scripts contain no move opcode at all -- so there is nothing to hold, and
// rgguest2's Propagator is a cutscene that ends in the MAPJUMP3 out of the
// room. Freezing that one would block the script before its own exit and strand
// the player. Three fields where the honest answer is "do nothing", and doing
// nothing in them is the whole reason the freezable column exists.

} // namespace Props
