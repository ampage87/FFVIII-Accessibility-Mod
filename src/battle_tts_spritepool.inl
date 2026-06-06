// battle_tts_spritepool.inl — Per-frame slot-pool + anim-flag-region poll
// Included from battle_tts.cpp. Do not compile independently.
//
// ============================================================================
// v0.14.2 — Three-front diagnostic
// ============================================================================
//
// v0.14.1 BAT proved 0x01D28C44 is empty during damage events (38 heartbeats,
// all active=0). The session-100 [slot+8] timer hypothesis appears to apply
// to a non-damage-popup pool. Three diagnostic prongs in this build:
//
//   1. PollSpritePoolDiag  — keep polling 0x01D28C44 AND add 0x01D28C04
//      (the alternate address from the v0.13.93 hook comment). Tag log
//      lines with @C44 / @C04 to disambiguate. Cheap; rules out the
//      simpler "wrong base address" possibility.
//
//   2. PollAnimFlagRegion — poll 16 bytes at 0x01D280C0 (the anim flag
//      itself + 15 adjacent bytes). The session-100 disassembly grep
//      called out 0x01D280C2 as "set to 1 by FLAG_MANAGER when its
//      timer expires." Even though the FLAG_MANAGER callback isn't
//      firing for damage popups, this 16-byte cluster is right next
//      to the anim flag we already trust and might contain a damage-
//      popup-specific visibility flag set by a different code path.
//
//   3. (separate file: battle_tts_dmgbp.inl) — hardware write BP on
//      0x01D2834A (BATTLE_DAMAGE_DISPLAY_ADDR). Catches every WRITE to
//      the damage display value. The instruction that writes 235 (the
//      damage) is the parent of the damage popup pipeline; from there
//      we can walk forward to find the visibility decision.
//
// All three are diagnostic only. Production trigger stays at v0.13.90
// anim-flag-fall.
//
// ============================================================================
// Slot-pool layout (hypothesised — being tested by this build)
// ============================================================================
//
//   [+0x0] uint8  kind         — sprite type / callback index
//   [+0x4] u32?   payload?
//   [+0x8] uint16 timer        — per-frame countdown (the hypothesis)
//   [+0xC] uint8  entIdx       — entity index (sub_48E620 reads this)
//   [+0xF] uint8  doneFlag     — 0→1 marks rendered/done (hypothesis)
//
// 16 slots × 16 bytes = 256 bytes per pool.
//
// ============================================================================
// Anim-flag-region layout (unknown, being mapped)
// ============================================================================
//
//   [+0x0] uint8  damage anim flag  — confirmed: 1=animating, 0=done
//   [+0x1] ?
//   [+0x2] uint8? — disassembly says FLAG_MANAGER sets this to 1
//   [+0x3..0xF]  ?
//
// Whole 16-byte region polled per frame; any byte transition is logged.
//
// ============================================================================
// Volume control
// ============================================================================
//
//   * Both polls are anim-flag-gated. Outside damage windows they early-
//     return after clearing baselines.
//   * Edge-only logging (+ 1-Hz HEARTBEAT confirming the polls are alive).
//   * Smooth -1 timer countdown frames are filtered (DELTA only fires on
//     non-trivial changes).

// Pool A — original v0.14.1 hypothesis
static const uint32_t SPRITE_POOL_BASE_C44 = 0x01D28C44;
// Pool B — alternate address from v0.13.93 sub_482C90 hook comment
static const uint32_t SPRITE_POOL_BASE_C04 = 0x01D28C04;
static const int      SPRITE_POOL_SLOTS    = 16;
static const int      SPRITE_POOL_STRIDE   = 16;

// Per-pool prev state. Arrays parallel to slot index.
static uint16_t s_spPool44_PrevTimer[SPRITE_POOL_SLOTS] = {};
static uint8_t  s_spPool44_PrevKind[SPRITE_POOL_SLOTS] = {};
static uint8_t  s_spPool44_PrevDone[SPRITE_POOL_SLOTS] = {};
static bool     s_spPool44_PrevValid[SPRITE_POOL_SLOTS] = {};

static uint16_t s_spPool04_PrevTimer[SPRITE_POOL_SLOTS] = {};
static uint8_t  s_spPool04_PrevKind[SPRITE_POOL_SLOTS] = {};
static uint8_t  s_spPool04_PrevDone[SPRITE_POOL_SLOTS] = {};
static bool     s_spPool04_PrevValid[SPRITE_POOL_SLOTS] = {};

// Shared frame & heartbeat counters (both pools polled in lock-step)
static uint32_t s_spPoolFrameCounter = 0;
static uint32_t s_spPoolHeartbeatFrame = 0;

// Anim-flag-region poll state
static const uint32_t ANIM_REGION_BASE = 0x01D280C0;
static const int      ANIM_REGION_SIZE = 16;
static uint8_t  s_animRegionPrev[ANIM_REGION_SIZE] = {};
static bool     s_animRegionPrevValid = false;
static uint32_t s_animRegionFrame = 0;
static uint32_t s_animRegionHeartbeat = 0;

// ============================================================================
// PollOnePool — edge detection for a single 16-slot pool
// ============================================================================
//
// Helper called by PollSpritePoolDiag for both pools. State arrays passed by
// pointer so we can reuse the same code for @C44 and @C04. Logs:
//   BASELINE / KIND-CHANGE / TIMER-START / TIMER-EXPIRED / DONE-FLAG / DELTA
// each tagged with the supplied pool tag.

static void PollOnePool(const char* tag, uint32_t base,
                        uint16_t* prevTimer, uint8_t* prevKind,
                        uint8_t* prevDone, bool* prevValid,
                        uint32_t frame, DWORD nowTick)
{
    uint8_t snap[SPRITE_POOL_SLOTS * SPRITE_POOL_STRIDE];
    __try {
        memcpy(snap, (void*)base, sizeof(snap));
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        return;
    }

    for (int i = 0; i < SPRITE_POOL_SLOTS; i++) {
        const uint8_t* slot = snap + i * SPRITE_POOL_STRIDE;
        uint16_t timer  = *(const uint16_t*)(slot + 0x8);
        uint8_t  kind   = slot[0x0];
        uint8_t  entIdx = slot[0xC];
        uint8_t  done   = slot[0xF];

        if (!prevValid[i]) {
            prevTimer[i] = timer;
            prevKind[i]  = kind;
            prevDone[i]  = done;
            prevValid[i] = true;
            if (kind != 0 || timer != 0 || done != 0) {
                char hex[64] = {};
                int hp = 0;
                for (int b = 0; b < 16; b++)
                    hp += snprintf(hex + hp, sizeof(hex) - hp, "%02X ", slot[b]);
                Log::Battle("BattleTTS: [SPRITE-POOL-DIAG] %s BASELINE i=%d frame=%u tick=%u "
                            "kind=0x%02X timer=%u entIdx=%u done=%u data=[%s]",
                            tag, i, frame, (unsigned)nowTick,
                            (unsigned)kind, (unsigned)timer,
                            (unsigned)entIdx, (unsigned)done, hex);
            }
            continue;
        }

        uint16_t prevT = prevTimer[i];
        uint8_t  prevK = prevKind[i];
        uint8_t  prevD = prevDone[i];

        if (kind != prevK) {
            char hex[64] = {};
            int hp = 0;
            for (int b = 0; b < 16; b++)
                hp += snprintf(hex + hp, sizeof(hex) - hp, "%02X ", slot[b]);
            Log::Battle("BattleTTS: [SPRITE-POOL-DIAG] %s KIND-CHANGE i=%d frame=%u tick=%u "
                        "kind=0x%02X->0x%02X timer=%u entIdx=%u done=%u data=[%s]",
                        tag, i, frame, (unsigned)nowTick,
                        (unsigned)prevK, (unsigned)kind,
                        (unsigned)timer, (unsigned)entIdx, (unsigned)done, hex);
        }

        if (prevT != 0 && timer == 0) {
            char hex[64] = {};
            int hp = 0;
            for (int b = 0; b < 16; b++)
                hp += snprintf(hex + hp, sizeof(hex) - hp, "%02X ", slot[b]);
            Log::Battle("BattleTTS: [SPRITE-POOL-DIAG] %s TIMER-EXPIRED i=%d frame=%u tick=%u "
                        "prevTimer=%u kind=0x%02X entIdx=%u done=%u data=[%s]",
                        tag, i, frame, (unsigned)nowTick,
                        (unsigned)prevT, (unsigned)kind,
                        (unsigned)entIdx, (unsigned)done, hex);
        }

        if (prevT == 0 && timer != 0) {
            Log::Battle("BattleTTS: [SPRITE-POOL-DIAG] %s TIMER-START i=%d frame=%u tick=%u "
                        "timer=%u kind=0x%02X entIdx=%u done=%u",
                        tag, i, frame, (unsigned)nowTick,
                        (unsigned)timer, (unsigned)kind,
                        (unsigned)entIdx, (unsigned)done);
        }

        if (prevD == 0 && done != 0) {
            Log::Battle("BattleTTS: [SPRITE-POOL-DIAG] %s DONE-FLAG i=%d frame=%u tick=%u "
                        "timer=%u kind=0x%02X entIdx=%u done=%u",
                        tag, i, frame, (unsigned)nowTick,
                        (unsigned)timer, (unsigned)kind,
                        (unsigned)entIdx, (unsigned)done);
        }

        if (timer != 0 && prevT != 0 && timer != prevT
            && (int)timer - (int)prevT != -1) {
            int diff = (int)timer - (int)prevT;
            Log::Battle("BattleTTS: [SPRITE-POOL-DIAG] %s DELTA i=%d frame=%u tick=%u "
                        "prevTimer=%u timer=%u diff=%+d kind=0x%02X entIdx=%u",
                        tag, i, frame, (unsigned)nowTick,
                        (unsigned)prevT, (unsigned)timer, diff,
                        (unsigned)kind, (unsigned)entIdx);
        }

        prevTimer[i] = timer;
        prevKind[i]  = kind;
        prevDone[i]  = done;
    }
}

static void PollSpritePoolDiag()
{
    uint8_t animFlag = 0;
    __try {
        animFlag = *(uint8_t*)BATTLE_DAMAGE_ANIM_FLAG;
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        return;
    }

    if (animFlag == 0) {
        for (int i = 0; i < SPRITE_POOL_SLOTS; i++) {
            s_spPool44_PrevValid[i] = false;
            s_spPool04_PrevValid[i] = false;
        }
        s_spPoolFrameCounter = 0;
        s_spPoolHeartbeatFrame = 0;
        return;
    }

    s_spPoolFrameCounter++;
    DWORD nowTick = GetTickCount();

    PollOnePool("@C44", SPRITE_POOL_BASE_C44,
                s_spPool44_PrevTimer, s_spPool44_PrevKind,
                s_spPool44_PrevDone, s_spPool44_PrevValid,
                s_spPoolFrameCounter, nowTick);

    PollOnePool("@C04", SPRITE_POOL_BASE_C04,
                s_spPool04_PrevTimer, s_spPool04_PrevKind,
                s_spPool04_PrevDone, s_spPool04_PrevValid,
                s_spPoolFrameCounter, nowTick);

    // Combined HEARTBEAT every 60 polled frames (~1 s at 60 fps).
    if (s_spPoolFrameCounter - s_spPoolHeartbeatFrame >= 60) {
        s_spPoolHeartbeatFrame = s_spPoolFrameCounter;

        char buf44[256] = {};
        int b44 = 0, active44 = 0;
        for (int i = 0; i < SPRITE_POOL_SLOTS; i++) {
            if (s_spPool44_PrevTimer[i] == 0
                && s_spPool44_PrevKind[i] == 0
                && s_spPool44_PrevDone[i] == 0) continue;
            active44++;
            if (b44 < (int)sizeof(buf44) - 48) {
                b44 += snprintf(buf44 + b44, sizeof(buf44) - b44,
                                "%si%d:k=0x%02X,t=%u,d=%u",
                                b44 ? " " : "", i,
                                (unsigned)s_spPool44_PrevKind[i],
                                (unsigned)s_spPool44_PrevTimer[i],
                                (unsigned)s_spPool44_PrevDone[i]);
            }
        }

        char buf04[256] = {};
        int b04 = 0, active04 = 0;
        for (int i = 0; i < SPRITE_POOL_SLOTS; i++) {
            if (s_spPool04_PrevTimer[i] == 0
                && s_spPool04_PrevKind[i] == 0
                && s_spPool04_PrevDone[i] == 0) continue;
            active04++;
            if (b04 < (int)sizeof(buf04) - 48) {
                b04 += snprintf(buf04 + b04, sizeof(buf04) - b04,
                                "%si%d:k=0x%02X,t=%u,d=%u",
                                b04 ? " " : "", i,
                                (unsigned)s_spPool04_PrevKind[i],
                                (unsigned)s_spPool04_PrevTimer[i],
                                (unsigned)s_spPool04_PrevDone[i]);
            }
        }

        Log::Battle("BattleTTS: [SPRITE-POOL-DIAG] HEARTBEAT frame=%u tick=%u "
                    "@C44 active=%d [%s] | @C04 active=%d [%s]",
                    s_spPoolFrameCounter, (unsigned)nowTick,
                    active44, buf44, active04, buf04);
    }
}

// ============================================================================
// PollAnimFlagRegion — Option 2 — 16 bytes at 0x01D280C0
// ============================================================================
//
// Polls 16 bytes starting at the anim flag every frame while the gate is
// open. Logs any byte transition with [ANIM-REGION] tag. Heartbeat at 1 Hz
// summarises the current 16-byte snapshot in hex even when no edges fire.
// 0x01D280C0 = byte 0 (anim flag itself, always non-zero during the gate).
// 0x01D280C2 = byte 2 (called out by session-100 disassembly grep as a
// flag set by FLAG_MANAGER when its timer expires).

static void PollAnimFlagRegion()
{
    uint8_t animFlag = 0;
    __try {
        animFlag = *(uint8_t*)BATTLE_DAMAGE_ANIM_FLAG;
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        return;
    }

    if (animFlag == 0) {
        s_animRegionPrevValid = false;
        s_animRegionFrame = 0;
        s_animRegionHeartbeat = 0;
        return;
    }

    s_animRegionFrame++;
    DWORD nowTick = GetTickCount();

    uint8_t snap[ANIM_REGION_SIZE];
    __try {
        memcpy(snap, (void*)ANIM_REGION_BASE, sizeof(snap));
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        return;
    }

    if (!s_animRegionPrevValid) {
        memcpy(s_animRegionPrev, snap, sizeof(snap));
        s_animRegionPrevValid = true;
        // BASELINE: dump the initial state on first frame of each window.
        char hex[64] = {};
        int hp = 0;
        for (int b = 0; b < ANIM_REGION_SIZE; b++)
            hp += snprintf(hex + hp, sizeof(hex) - hp, "%02X ", snap[b]);
        Log::Battle("BattleTTS: [ANIM-REGION] BASELINE frame=%u tick=%u data=[%s]",
                    s_animRegionFrame, (unsigned)nowTick, hex);
        return;
    }

    // Detect any byte transition. Report each one with offset, prev, new.
    for (int b = 0; b < ANIM_REGION_SIZE; b++) {
        if (snap[b] != s_animRegionPrev[b]) {
            Log::Battle("BattleTTS: [ANIM-REGION] CHANGE +0x%X (0x%08X) frame=%u tick=%u "
                        "%u -> %u",
                        b, ANIM_REGION_BASE + b,
                        s_animRegionFrame, (unsigned)nowTick,
                        (unsigned)s_animRegionPrev[b], (unsigned)snap[b]);
        }
    }
    memcpy(s_animRegionPrev, snap, sizeof(snap));

    // HEARTBEAT every 60 polled frames — full 16-byte snapshot in hex so we
    // see what's in the region even when nothing's changing.
    if (s_animRegionFrame - s_animRegionHeartbeat >= 60) {
        s_animRegionHeartbeat = s_animRegionFrame;
        char hex[64] = {};
        int hp = 0;
        for (int i = 0; i < ANIM_REGION_SIZE; i++)
            hp += snprintf(hex + hp, sizeof(hex) - hp, "%02X ", snap[i]);
        Log::Battle("BattleTTS: [ANIM-REGION] HEARTBEAT frame=%u tick=%u data=[%s]",
                    s_animRegionFrame, (unsigned)nowTick, hex);
    }
}

// ============================================================================
// PollDamageSlotDiag — v0.14.5 — 24 bytes at 0x01D28344 (the popup struct slot)
// ============================================================================
//
// v0.14.4 BAT proved sub_48EF80 fires too early — it creates the
// action-announce popup struct, not the damage-number popup. The damage
// number popup is rendered later, at impact time, via a code path we
// haven't located yet. Between sub_48EF80 firing (action start) and the
// anim flag falling (popup-fade end), SOMETHING in the engine state must
// transition at impact time so the rendering pipeline knows to draw the
// number.
//
// This diagnostic polls 24 bytes at 0x01D28344 — the slot 0 of the popup
// struct array that sub_48EF80 fills — every frame while the anim flag
// is up. Logs:
//
//   - BASELINE: full 24-byte hex dump on first frame of each event
//   - CHANGE +0xN: every byte transition with old/new values
//   - HEARTBEAT: full hex snapshot every 1 s for slow-changing fields
//   - FINAL: full 24-byte hex dump just before anim flag falls
//
// Cross-reference with [DMG-POPUP-CREATE] (start of event), the spell
// animation duration, and [HP-TRACK] Anim flag cleared (end of event).
// We're looking for a byte transition that fires at impact time — the
// moment the player would actually see the damage number on screen.
//
// Hypothesis: there's likely a state byte in the slot struct that
// transitions when the popup goes from "queued" to "rendering". Could be
// at any offset; the v0.14.1 hypothesis was [+8] uint16 timer for the C44
// pool, which was empty for damage events. THIS pool (0x01D28344) is the
// one sub_48EF80 actually writes, so the timer hypothesis is more likely
// to apply here.

static const uint32_t DAMAGE_SLOT_BASE = 0x01D28344;
static const int      DAMAGE_SLOT_SIZE = 24;
static uint8_t  s_damageSlotPrev[DAMAGE_SLOT_SIZE] = {};
static bool     s_damageSlotPrevValid = false;
static uint32_t s_damageSlotFrame = 0;
static uint32_t s_damageSlotHeartbeat = 0;
static bool     s_damageSlotAnimWasUp = false;

static void PollDamageSlotDiag()
{
    uint8_t animFlag = 0;
    __try {
        animFlag = *(uint8_t*)BATTLE_DAMAGE_ANIM_FLAG;
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        return;
    }

    if (animFlag == 0) {
        // On the falling edge, dump a FINAL snapshot for cross-reference.
        if (s_damageSlotAnimWasUp && s_damageSlotPrevValid) {
            char hex[96] = {};
            int hp = 0;
            for (int b = 0; b < DAMAGE_SLOT_SIZE; b++)
                hp += snprintf(hex + hp, sizeof(hex) - hp, "%02X ", s_damageSlotPrev[b]);
            Log::Battle("BattleTTS: [SLOT0-DIAG] FINAL frame=%u tick=%u data=[%s]",
                        s_damageSlotFrame, (unsigned)GetTickCount(), hex);
        }
        s_damageSlotPrevValid = false;
        s_damageSlotFrame = 0;
        s_damageSlotHeartbeat = 0;
        s_damageSlotAnimWasUp = false;
        return;
    }

    s_damageSlotAnimWasUp = true;
    s_damageSlotFrame++;
    DWORD nowTick = GetTickCount();

    uint8_t snap[DAMAGE_SLOT_SIZE];
    __try {
        memcpy(snap, (void*)DAMAGE_SLOT_BASE, sizeof(snap));
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        return;
    }

    if (!s_damageSlotPrevValid) {
        memcpy(s_damageSlotPrev, snap, sizeof(snap));
        s_damageSlotPrevValid = true;
        char hex[96] = {};
        int hp = 0;
        for (int b = 0; b < DAMAGE_SLOT_SIZE; b++)
            hp += snprintf(hex + hp, sizeof(hex) - hp, "%02X ", snap[b]);
        Log::Battle("BattleTTS: [SLOT0-DIAG] BASELINE frame=%u tick=%u data=[%s]",
                    s_damageSlotFrame, (unsigned)nowTick, hex);
        return;
    }

    // Detect any byte transition. Each change gets its own log line so we
    // can grep / timeline cross-reference cleanly.
    for (int b = 0; b < DAMAGE_SLOT_SIZE; b++) {
        if (snap[b] != s_damageSlotPrev[b]) {
            Log::Battle("BattleTTS: [SLOT0-DIAG] CHANGE +0x%X (0x%08X) frame=%u tick=%u "
                        "%u -> %u",
                        b, DAMAGE_SLOT_BASE + b,
                        s_damageSlotFrame, (unsigned)nowTick,
                        (unsigned)s_damageSlotPrev[b], (unsigned)snap[b]);
        }
    }
    memcpy(s_damageSlotPrev, snap, sizeof(snap));

    // HEARTBEAT every 60 polled frames — full 24-byte snapshot.
    if (s_damageSlotFrame - s_damageSlotHeartbeat >= 60) {
        s_damageSlotHeartbeat = s_damageSlotFrame;
        char hex[96] = {};
        int hp = 0;
        for (int i = 0; i < DAMAGE_SLOT_SIZE; i++)
            hp += snprintf(hex + hp, sizeof(hex) - hp, "%02X ", snap[i]);
        Log::Battle("BattleTTS: [SLOT0-DIAG] HEARTBEAT frame=%u tick=%u data=[%s]",
                    s_damageSlotFrame, (unsigned)nowTick, hex);
    }
}
