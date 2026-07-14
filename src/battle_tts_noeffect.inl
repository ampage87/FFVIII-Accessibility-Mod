// battle_tts_noeffect.inl — v0.13.83
//
// Detects "no effect" cases the kind=4 hook misses entirely.
//
// THE PROBLEM
// -----------
// When a player casts a status spell on a target who already has that
// status (e.g. Sleep on already-asleep ally), the engine short-circuits
// the spell-result path BEFORE calling sub_4877F0. The kind=4 hook never
// fires, [STATUS-Q] never queues, no announcement plays. Verified at
// 15:23:56 in the BAT log:
//   * SPRITE-SPAWN-SEQ fires for sub_48E830 (action announce sprite)
//   * TEXT-SPRITE-SEQ "Cast Sleep" fires
//   * Sprite despawns 8s later
//   * NO [SPELL-RESULT], NO [SPELL-MISS-SKIP], NO [STATUS-Q]
//
// THE FIX
// -------
// Hook the action-announce sprite spawn (sub_48E830, already hooked) for
// player magic casts (retaddr=0x0048594E AND arg[1]==0x16). Snapshot the
// target's HP and start a per-frame watchdog. During the watchdog window
// (~6 seconds), monitor for ANY of these activity indicators:
//   (a) HP delta from snapshot — spell did damage / healing
//   (b) status queue non-empty for the target slot — engine applied a
//       status change which PollStatusChanges already enqueued
//   (c) kind=4 hook already announced for this slot — existing path
//       handled it
//
// If the watchdog fires with NONE of those flags set, the spell genuinely
// produced no observable effect → announce "No effect on <target>".
//
// SCOPE (v1)
// ----------
//   * Player-cast magic only. retaddr=0x0048594E (player path) AND
//     arg[1]==0x16 (magic action ID). Enemy casts (retaddr=0x00489FC0)
//     deferred — enemy attacks usually DO cause change so the watchdog
//     would rarely fire there anyway, but adding noise risk isn't worth
//     it without testing.
//   * Single-target only. Multi-target Reflect / Wall scenarios deferred.
//
// COORDINATION WITH EXISTING PATHS
// --------------------------------
// The existing kind=4 path (in battle_tts_sprite.inl) announces immediately
// when a3=0x9 fires (Silence-immune, Sleep-on-sleeping for ENEMY targets).
// That path uses s_lastSpellMissAnnounceTick[slot] for its own dedup.
// We piggyback on that array: if it shows a recent kind=4 announcement
// for this slot, we stay silent. After our own announcement, we update
// the array so the kind=4 path (if it fires late) also stays silent.
//
// KNOWN LIMITATIONS
// -----------------
//   * Reflect: spell bounces off target onto the caster. From the
//     original target's POV, no HP/status change → would announce
//     "no effect". Wrong, but rare in normal play. Aaron can flag.
//   * v0.13.86: Same-value consecutive cap-heals (e.g. Cure 344 then
//     Cure 344 on the same full-HP target with no other action between)
//     will skip the second heal-on-cap announcement — the displayValue
//     baseline equals the current value so no change is detected. Any
//     intervening action that writes displayValue (damage event, heal
//     of a different amount) resets the baseline. Acceptable edge case.
//
// INCLUDE ORDER
// -------------
// Included by battle_tts.cpp BETWEEN battle_tts_sprite.inl (so we can see
// s_lastSpellMissAnnounceTick) and battle_tts_sprite_spawn.inl (so its
// HookedSub48E830 callback can call NoEffect_RecordSnapshot).
//
// Depends on (defined in earlier .inl files):
//   * GetEntityHP / GetEntityMaxHP — battle_tts.cpp body
//   * BitmaskToSlot — battle_tts_diagnostics.inl
//   * GetSlotName — battle_tts_hp.inl
//   * s_statusQueueCount — battle_status.inl
//   * s_lastSpellMissAnnounceTick — battle_tts_sprite.inl
//   * s_lastScanCastTick (v0.14.55) — battle_tts_sprite.inl
//   * Validate_AnnounceEvent — forward declared in battle_tts.cpp
//   * BattleSpeakEvent — battle_tts.cpp body
//   * ScanTTS::OnScanCast (v0.14.55) — forward declared at file scope
//     in battle_tts.cpp BEFORE `namespace BattleTTS {` opens, which puts
//     the declaration in the GLOBAL ::ScanTTS namespace where the linker
//     can find the definition in scan_tts.cpp. We do NOT redeclare it
//     inside this .inl, because this .inl is included inside
//     `namespace BattleTTS {` — a `namespace ScanTTS { ... }` block here
//     would create `BattleTTS::ScanTTS::OnScanCast`, which is a
//     different symbol that the linker can't find. (v0.14.55 BAT FAIL
//     LNK2019 was caused by exactly this mistake.)

// v0.14.55: ScanTTS::OnScanCast is already forward-declared at file
// scope in battle_tts.cpp (line ~220, BEFORE `namespace BattleTTS {`).
// That declaration is in the global `::ScanTTS` namespace where the
// linker can find scan_tts.cpp's definition. We must NOT add a local
// `namespace ScanTTS { void OnScanCast(int); }` here — doing so creates
// a different symbol `BattleTTS::ScanTTS::OnScanCast` (because this
// .inl is included inside `namespace BattleTTS`) and the linker fails
// with LNK2019 (which is exactly what happened in v0.14.55).
//
// The call site below uses unqualified `ScanTTS::OnScanCast(...)`. C++
// name lookup from inside `namespace BattleTTS` first checks for
// `BattleTTS::ScanTTS::OnScanCast` (none) and falls through to the
// global `::ScanTTS::OnScanCast` declared in battle_tts.cpp.

// ============================================================================
// Tunables
// ============================================================================

// Watchdog duration. Long enough to cover typical cast animations (Sleep,
// Silence, Cure, Esuna ~3-6s) plus the kind=2 / kind=4 spell-result hook
// firings that resolve close to despawn time. Short enough that the
// announcement still feels timely.
static const DWORD NOEFFECT_WATCHDOG_MS = 6000;

// v0.18.3.236 (#73/#74): hard cap on watchdog deferral. The 2026-07-12 Ifrit
// BAT proved the 6 s watchdog can expire while the action is still animating
// (Blizzard-vs-Ifrit cast-to-impact exceeds 6 s), so the verdict was rendered
// from a pre-written displayValue and misread as heal-on-cap / no-effect.
// While the damage anim flag is up at expiry time we now DEFER the verdict
// until the animation ends, but never beyond this cap.
static const DWORD NOEFFECT_WATCHDOG_MAX_MS = 20000;

// Cross-coordination window with the kind=4 path. The kind=4 hook can
// fire before OR after our watchdog timer expires depending on the spell.
// 8 seconds covers either ordering.
static const DWORD NOEFFECT_KIND4_DEDUP_MS = 8000;

// ============================================================================
// State
// ============================================================================

struct PendingSpellNoEffect {
    bool      active;
    int       targetSlot;
    uint32_t  targetHpAtCast;
    DWORD     castTick;

    // Activity flags — set when any of the watchdog-window observations
    // indicate the spell DID have an effect. If all remain false at
    // watchdog fire time, we announce "No effect."
    bool      sawHpChange;
    bool      sawStatusQueueActivity;

    // v0.13.86: Pre-action displayValue baseline. Read from
    // s_displayValuePrevFrame (the last mod-thread snapshot, which
    // precedes the engine's same-frame pre-write of the new heal/damage
    // value). At watchdog expiry, if no other effect was observed but
    // the current displayValue differs from this baseline AND is > 0,
    // we infer a heal-on-cap and announce 'X recovers N HP' instead of
    // 'No effect.'
    uint16_t  displayValueAtCast;
};

static PendingSpellNoEffect s_pendingSpellNoEffect = {};

// ============================================================================
// v0.13.89: Animation-synced flush queue
// ============================================================================
//
// Problem: the kind=4 a3=0x9 immediate path in battle_tts_sprite.inl was
// firing "No effect on X" at engine-decision time, which is BEFORE the
// visual cast animation begins. v0.13.88 BAT confirmed at 22:23:25:
//
//   22:23:25  [SPELL-NOEFFECT] No effect on Grat.       <- TTS fires
//   22:23:25  [SPRITE-SPAWN-SEQ] #4 fn=sub_48E830 ...   <- action-announce
//   22:23:25  [SPRITE-POLL] NEW i=0 slot=1 kind=0x06    <- popup spawns
//
// Fix: queue the announcement and hold until the damage anim flag at
// 0x01D280C0 transitions 1->0 (animation finished). Same pattern
// battle_status.inl uses for "Squall silenced" etc., which is correctly
// timed.
//
// Also routes the watchdog's no-effect verdict and heal-on-cap verdict
// through the same queue for consistency. The watchdog already has a 6s
// delay built in, so its announcement timing changes minimally — the
// 500ms grace handles the case where the animation has already ended by
// the time the watchdog fires.

struct PendingNoEffectAnnounce {
    bool      active;
    int       slot;
    int       value;            // heal-on-cap amount, or 0 for no-effect
    DWORD     queuedTick;
    bool      animSawActive;    // set true once we observed anim flag == 1
    char      text[128];
    char      validateKind[16]; // "no-effect" or "heal-on-cap" for [VALIDATE]
};

static PendingNoEffectAnnounce s_pendingNoEffectAnnounce = {};

// v0.18.3.236 (#74): flush-side dedup. The 2026-07-12 Ifrit BAT showed the
// same "No effect on Ifrit." spoken twice back-to-back (watchdog verdict
// flushed at 19:20:21, kind=4 a3=0x8 re-queued the identical text the same
// second). Remember what was last flushed so an identical re-queue for the
// same slot within the window is silently dropped.
static int   s_lastNoEffectFlushSlot = -1;
static DWORD s_lastNoEffectFlushTick = 0;
static char  s_lastNoEffectFlushText[128] = {};
static const DWORD NOEFFECT_REPEAT_DEDUP_MS = 4000;

// 500ms grace lets the kind=4 path's expected 0->1 transition show up
// (typically within 1-2 frames). If the flag never goes up by then, the
// announcement was for a case with no animation (e.g. late watchdog
// firing after the animation already ended) and we fire as fallback.
static const DWORD NOEFFECT_ANNOUNCE_GRACE_MS = 500;

// Final safety net: if the engine somehow keeps the anim flag stuck at 1
// for longer than this, fire anyway so the announcement isn't lost.
static const DWORD NOEFFECT_ANNOUNCE_TIMEOUT_MS = 10000;

// Queue an announcement to fire at the next animation 1->0 transition
// (or after the grace period if no animation runs). Replaces direct
// BattleSpeakEvent calls from both the kind=4 immediate path
// (sprite.inl) and the watchdog's no-effect / heal-on-cap verdicts
// (this file).
//
// validateKind is the string emitted as the [VALIDATE] kind= field;
// usually "no-effect" or "heal-on-cap".
static void NoEffect_QueueAnnouncement(int slot, int value,
                                       const char* text,
                                       const char* validateKind)
{
    if (slot < 0 || slot >= BATTLE_TOTAL_SLOTS) return;
    if (!text || !text[0]) return;
    if (!validateKind || !validateKind[0]) validateKind = "no-effect";

    // v0.18.3.236 (#74): drop an identical re-queue for the same slot right
    // after the same text was flushed (double-source race: watchdog verdict
    // + late kind=4 fire for one cast).
    if (slot == s_lastNoEffectFlushSlot &&
        s_lastNoEffectFlushTick != 0 &&
        (GetTickCount() - s_lastNoEffectFlushTick) < NOEFFECT_REPEAT_DEDUP_MS &&
        strncmp(text, s_lastNoEffectFlushText,
                sizeof(s_lastNoEffectFlushText)) == 0) {
        Log::Battle("BattleTTS: [NOEFFECT-DEDUP] identical text for slot=%d "
                    "flushed %ums ago, queue skipped: %s",
                    slot, (unsigned)(GetTickCount() - s_lastNoEffectFlushTick),
                    text);
        return;
    }

    s_pendingNoEffectAnnounce.active        = true;
    s_pendingNoEffectAnnounce.slot          = slot;
    s_pendingNoEffectAnnounce.value         = value;
    s_pendingNoEffectAnnounce.queuedTick    = GetTickCount();
    s_pendingNoEffectAnnounce.animSawActive = false;
    strncpy(s_pendingNoEffectAnnounce.text, text,
            sizeof(s_pendingNoEffectAnnounce.text) - 1);
    s_pendingNoEffectAnnounce.text[sizeof(s_pendingNoEffectAnnounce.text) - 1] = '\0';
    strncpy(s_pendingNoEffectAnnounce.validateKind, validateKind,
            sizeof(s_pendingNoEffectAnnounce.validateKind) - 1);
    s_pendingNoEffectAnnounce.validateKind[sizeof(s_pendingNoEffectAnnounce.validateKind) - 1] = '\0';

    Log::Battle("BattleTTS: [NOEFFECT-Q] slot=%d kind=%s queued: %s",
                slot, validateKind, text);
}

// Per-frame poll. Called from BattleTTS::Update() AFTER
// PollPendingSpellNoEffect so any queue-write that happened this frame is
// visible. Three flush triggers:
//   anim-done : animSawActive && animFlag == 0 (animation finished)
//   no-anim   : !animSawActive && elapsed >= grace (no animation observed)
//   timeout   : elapsed >= overall timeout (safety net)
static void PollPendingNoEffectAnnouncements()
{
    if (!s_pendingNoEffectAnnounce.active) return;

    uint8_t animFlag = 0;
    __try { animFlag = *(uint8_t*)BATTLE_DAMAGE_ANIM_FLAG; }
    __except(EXCEPTION_EXECUTE_HANDLER) {}

    if (animFlag != 0) {
        s_pendingNoEffectAnnounce.animSawActive = true;
    }

    DWORD now = GetTickCount();
    DWORD elapsed = now - s_pendingNoEffectAnnounce.queuedTick;

    bool flushNow = false;
    const char* trigger = nullptr;

    if (s_pendingNoEffectAnnounce.animSawActive && animFlag == 0) {
        flushNow = true;
        trigger = "anim-done";
    } else if (!s_pendingNoEffectAnnounce.animSawActive &&
               elapsed >= NOEFFECT_ANNOUNCE_GRACE_MS) {
        flushNow = true;
        trigger = "no-anim";
    } else if (elapsed >= NOEFFECT_ANNOUNCE_TIMEOUT_MS) {
        flushNow = true;
        trigger = "timeout";
    }

    if (!flushNow) return;

    // v0.18.3.236 (#73/#74): supersede check. If a REAL damage/heal flush
    // spoke for this slot after this announcement was queued, the engine
    // resolved the action with an observable effect — the queued no-effect /
    // heal-on-cap verdict is stale and wrong. The 2026-07-12 Ifrit BAT is the
    // canonical case: "Ifrit takes 62 damage." flushed milliseconds after the
    // spurious "Ifrit recovers 62 HP." was queued; the queued line then spoke
    // 1.5 s later anyway. Legitimate heal-on-cap (Cure on full-HP target)
    // is unaffected: no HP change means no flush, so the tick stays older
    // than queuedTick.
    {
        int dslot = s_pendingNoEffectAnnounce.slot;
        if (dslot >= 0 && dslot < BATTLE_TOTAL_SLOTS) {
            DWORD lastFlush = s_lastFlushAnnounceTick[dslot];
            if (lastFlush != 0 &&
                (int32_t)(lastFlush - s_pendingNoEffectAnnounce.queuedTick) >= 0) {
                Log::Battle("BattleTTS: [NOEFFECT-DROP] '%s' (slot=%d, kind=%s) "
                            "superseded by real HP flush %ums after queue -- dropped",
                            s_pendingNoEffectAnnounce.text, dslot,
                            s_pendingNoEffectAnnounce.validateKind,
                            (unsigned)(lastFlush - s_pendingNoEffectAnnounce.queuedTick));
                s_pendingNoEffectAnnounce.active = false;
                return;
            }
        }
    }

    // Validate breadcrumb fires at flush time so the [VALIDATE] log line
    // sits right next to the actual TTS call.
    Validate_AnnounceEvent(s_pendingNoEffectAnnounce.validateKind,
                           s_pendingNoEffectAnnounce.slot,
                           s_pendingNoEffectAnnounce.value,
                           s_pendingNoEffectAnnounce.text,
                           trigger);
    BattleSpeakEvent(s_pendingNoEffectAnnounce.text, false);
    Log::Battle("BattleTTS: [NOEFFECT-FLUSH] %s (slot=%d, kind=%s, trigger=%s, %ums)",
                s_pendingNoEffectAnnounce.text,
                s_pendingNoEffectAnnounce.slot,
                s_pendingNoEffectAnnounce.validateKind,
                trigger, (unsigned)elapsed);

    // v0.18.3.236 (#74): record what was flushed for the re-queue dedup.
    s_lastNoEffectFlushSlot = s_pendingNoEffectAnnounce.slot;
    s_lastNoEffectFlushTick = GetTickCount();
    strncpy(s_lastNoEffectFlushText, s_pendingNoEffectAnnounce.text,
            sizeof(s_lastNoEffectFlushText) - 1);
    s_lastNoEffectFlushText[sizeof(s_lastNoEffectFlushText) - 1] = '\0';

    s_pendingNoEffectAnnounce.active = false;
}

// ============================================================================
// API called from HookedSub48E830 (battle_tts_sprite_spawn.inl)
// ============================================================================
//
// Records a snapshot when the engine spawns a player-magic action-announce
// sprite. The caller is expected to gate on:
//   retaddr == 0x0048594E   (player action popup pair)
//   arg[1] == 0x16          (magic cast action ID)
// targetMask is sub_48E830's arg[0] — the target bitmask. For single-target
// casts this has exactly one bit set. We refuse multi-bit masks for v1.
//
// Idempotency: overwriting an existing pending snapshot is fine — a player
// cannot fire two sub_48E830 player-magic events within the watchdog window
// (turns are sequential under EWM, and even non-EWM casts take >100ms).
static void NoEffect_RecordSnapshot(uint32_t targetMask)
{
    // v0.14.55: Scan suppression and announcement at the action layer.
    // The Scan-name popup fires from sub_48D200 just before sub_48E830
    // calls this snapshot recorder, so s_lastScanCastTick is always
    // fresh by the time we get here. Scan never produces a HP / status
    // / display change — the watchdog has nothing to observe and would
    // queue 'No effect on <target>' ~6 s later. Same idea as v0.14.51's
    // CancelNoEffectWatchdogForSlot, but earlier in the pipeline so we
    // never record the snapshot in the first place. Critically, this
    // also catches the compacted view shown on repeat Scans of the same
    // target in a battle — v0.14.54 BAT proved that view skips both
    // sub_B687C0 and sub_84F860 hooks, so the action-layer detection
    // here is the only path that fires for repeat Scans. We resolve the
    // target slot from targetMask and call ScanTTS::OnScanCast directly
    // so the player gets the genuine Scan announcement; OnScanCast's
    // own dedup makes a no-op out of any subsequent dispatcher / text
    // hook fires for the same Scan UI session.
    {
        // Read-and-clear atomically so the tick is consumed by exactly
        // one call to NoEffect_RecordSnapshot. Without this, a fast
        // follow-up cast on a different target within SCAN_CAST_RECENT_MS
        // would also be suppressed (e.g. Scan Bite Bug 1, then Sleep on
        // Bite Bug 2 a half-second later — the second call would see
        // the same Scan tick and skip the legitimate Sleep watchdog).
        DWORD scanCastTick = (DWORD)InterlockedExchange(
            &s_lastScanCastTick, 0);
        if (scanCastTick != 0 &&
            (GetTickCount() - scanCastTick) <= SCAN_CAST_RECENT_MS) {
            // Resolve target slot for the announcement.
            int scanSlot = -1;
            if (targetMask != 0 && (targetMask & (targetMask - 1)) == 0) {
                scanSlot = BitmaskToSlot((uint8_t)(targetMask & 0xFF));
            }
            Log::Battle("BattleTTS: [NOEFFECT-WATCH] Scan detected "
                        "(scanCastTick=%u age=%ums, mask=0x%X resolvedSlot=%d), "
                        "skipping snapshot and announcing directly",
                        (unsigned)scanCastTick,
                        (unsigned)(GetTickCount() - scanCastTick),
                        (unsigned)targetMask, scanSlot);
            if (scanSlot >= 0 && scanSlot < BATTLE_TOTAL_SLOTS) {
                // v0.14.57: action-layer cue — owns the 30 s hook-suppression
                // window so the Scan UI's sub_84F860 / sub_B687C0 hooks
                // (which fire 5-15 s later when the window opens) don't
                // re-announce. Without this flag the player heard the
                // 'Bite Bug. Level 9. HP 162 of 162.' announcement TWICE
                // per Scan in v0.14.56 BAT (once at cast-commit, once at
                // UI-open).
                ScanTTS::OnScanCast(scanSlot, /*fromActionLayer=*/true);
            }
            // Don't record the snapshot. The watchdog stays inactive; no
            // 'No effect on <target>' will be queued.
            return;
        }
    }

    // v0.14.49: Draw-Stock suppression. When the player chose
    // Draw > Target > Spell > Stock, the action transfers a spell to
    // inventory and does NOT touch the enemy. The watchdog has no
    // HP / status / flush signal to observe and would announce
    // 'No effect on <enemy>' ~6 s later. Skip the snapshot in this case.
    //
    // IMPORTANT: only suppress for Stock. Draw > Cast (D9 == 1) DOES
    // affect the target -- a Fire cast on a Fire-absorbing enemy
    // genuinely has no effect and SHOULD announce. So we gate strictly on
    // (Draw command was selected this turn) AND (live Stock/Cast byte == 0).
    //
    // s_submenuCommandId persists across the turn-end -> action-execute
    // transition: PollTurnAndCommands sets it inside EnterSubmenu during
    // Draw nav (= 0x16), and only resets it to 0 on the NEXT turn's start.
    // So at sub_48E830 fire time it's still 0x16 for the just-confirmed
    // Draw action. We do NOT add s_inSubmenu to the gate because that
    // flag IS cleared on activeChar -> 0xFF before sub_48E830 fires.
    //
    // We read the live byte at 0x01D768D9 every time (not s_drawStockCastPrev)
    // because the latter stays 0xFF when the player confirms Stock at the
    // default cursor without ever moving it. The live byte is the engine's
    // authoritative selection at the moment the action commits.
    if (s_submenuCommandId == 0x16) {
        uint8_t stockCastNow = 0xFF;
        __try { stockCastNow = *(uint8_t*)0x01D768D9; } __except(EXCEPTION_EXECUTE_HANDLER) {}
        if (stockCastNow == 0) {
            Log::Battle("BattleTTS: [NOEFFECT-WATCH] Draw-Stock detected "
                        "(D9=0, cmd=0x16, mask=0x%X), skipping snapshot",
                        (unsigned)targetMask);
            return;
        }
    }

    if (targetMask == 0) return;
    // Single-target only for v1. (mask & (mask-1)) == 0 detects power-of-two.
    if ((targetMask & (targetMask - 1)) != 0) return;

    int slot = BitmaskToSlot((uint8_t)(targetMask & 0xFF));
    if (slot < 0 || slot >= BATTLE_TOTAL_SLOTS) return;

    // Refuse if target is empty / KO'd — no point watching for change.
    if (GetEntityMaxHP(slot) == 0) return;

    s_pendingSpellNoEffect.active                = true;
    s_pendingSpellNoEffect.targetSlot            = slot;
    s_pendingSpellNoEffect.targetHpAtCast        = GetEntityHP(slot);
    s_pendingSpellNoEffect.castTick              = GetTickCount();
    s_pendingSpellNoEffect.sawHpChange           = false;
    s_pendingSpellNoEffect.sawStatusQueueActivity = false;
    // v0.13.86: read pre-action displayValue baseline from the mod thread's
    // last snapshot (which precedes the engine's same-frame pre-write).
    s_pendingSpellNoEffect.displayValueAtCast    = s_displayValuePrevFrame;

    Log::Battle("BattleTTS: [NOEFFECT-WATCH] start slot=%d hp=%u (mask=0x%X) displayBase=%u",
                slot, s_pendingSpellNoEffect.targetHpAtCast,
                (unsigned)targetMask,
                (unsigned)s_pendingSpellNoEffect.displayValueAtCast);
}

// ============================================================================
// Per-frame poll — called from BattleTTS::Update()
// ============================================================================
//
// Two duties:
//   1. While the watchdog is active, monitor activity indicators every
//      frame so transient signals (e.g. status queue populated this frame,
//      drained next frame by a flush) are not lost.
//   2. When the watchdog expires, decide whether to announce.
//
// Order-of-call note: this is invoked AFTER PollHPChanges and
// PollStatusChanges in BattleTTS::Update(). Those polls have already run
// for the current frame, so any HP delta or status enqueue triggered by
// engine activity in the prior frame's events is visible to us here.
static void PollPendingSpellNoEffect()
{
    if (!s_pendingSpellNoEffect.active) return;

    int slot = s_pendingSpellNoEffect.targetSlot;
    DWORD now = GetTickCount();

    // --- Step 1: collect activity indicators for this frame ---

    // (a) HP change since cast time. We only need the boolean — the actual
    //     HP delta announcement is handled by PollHPChanges.
    if (!s_pendingSpellNoEffect.sawHpChange) {
        uint32_t curHp = GetEntityHP(slot);
        if (curHp != s_pendingSpellNoEffect.targetHpAtCast) {
            s_pendingSpellNoEffect.sawHpChange = true;
        }
    }

    // (b) Status queue populated for this slot at any point during the
    //     window. PollStatusChanges sets queue_count > 0 the same frame
    //     it observes a status bit flip. The flush later empties the
    //     queue — our flag persists across the flush.
    if (!s_pendingSpellNoEffect.sawStatusQueueActivity) {
        if (slot >= 0 && slot < BATTLE_TOTAL_SLOTS &&
            s_statusQueueCount[slot] > 0) {
            s_pendingSpellNoEffect.sawStatusQueueActivity = true;
        }
    }

    // (c) v0.13.86: FlushHPAnnouncements already spoke for this slot.
    //     The engine pre-applies HP before sub_48E830 fires, so our own
    //     HP snapshot at cast time captures POST-action HP and check (a)
    //     never trips. But HP-TRACK polls between frames and observes
    //     the delta, so FlushHPAnnouncements DOES fire and announce.
    //     We piggyback on s_lastFlushAnnounceTick to detect that.
    if (!s_pendingSpellNoEffect.sawHpChange) {
        DWORD lastFlush = s_lastFlushAnnounceTick[slot];
        if (lastFlush != 0 &&
            (int32_t)(lastFlush - s_pendingSpellNoEffect.castTick) >= 0) {
            s_pendingSpellNoEffect.sawHpChange = true;
        }
    }

    // --- Step 2: time check ---
    DWORD elapsed = now - s_pendingSpellNoEffect.castTick;
    if (elapsed < NOEFFECT_WATCHDOG_MS) return;

    // v0.18.3.236 (#73/#74): action-in-flight deferral. If the damage anim
    // flag is up when the watchdog expires, the engine is still playing the
    // action out — any verdict now would read a pre-written displayValue and
    // misclassify (the 2026-07-12 Ifrit BAT: every Blizzard verdict rendered
    // mid-animation as a phantom "recovers N HP" or "No effect"). Defer the
    // verdict until the animation ends, up to NOEFFECT_WATCHDOG_MAX_MS.
    if (elapsed < NOEFFECT_WATCHDOG_MAX_MS) {
        uint8_t inFlightAnim = 0;
        __try { inFlightAnim = *(uint8_t*)BATTLE_DAMAGE_ANIM_FLAG; }
        __except(EXCEPTION_EXECUTE_HANDLER) {}
        if (inFlightAnim != 0) {
            // Log once per second at most (the poll runs every frame).
            static DWORD s_lastDeferLogTick = 0;
            if ((now - s_lastDeferLogTick) >= 1000) {
                s_lastDeferLogTick = now;
                Log::Battle("BattleTTS: [NOEFFECT-WATCH] slot=%d verdict deferred "
                            "(action still animating, %ums elapsed)",
                            slot, (unsigned)elapsed);
            }
            return;
        }
    }

    // Consume the pending entry regardless of outcome.
    s_pendingSpellNoEffect.active = false;

    // Re-validate slot still active (target might have been KO'd by
    // something else during the window).
    if (slot < 0 || slot >= BATTLE_TOTAL_SLOTS) return;
    if (GetEntityMaxHP(slot) == 0) {
        Log::Battle("BattleTTS: [NOEFFECT-WATCH] slot=%d vacated, dropping",
                    slot);
        return;
    }

    // --- Step 3: cross-coordinate with kind=4 path ---
    // If the existing spell-result kind=4 hook already announced for this
    // slot within the dedup window, stay silent.
    DWORD lastKind4 = s_lastSpellMissAnnounceTick[slot];
    if (lastKind4 != 0 && (now - lastKind4) < NOEFFECT_KIND4_DEDUP_MS) {
        Log::Battle("BattleTTS: [NOEFFECT-WATCH] slot=%d kind=4 already "
                    "announced %ums ago, skipping",
                    slot, (unsigned)(now - lastKind4));
        return;
    }

    // --- Step 4: outcome ---
    if (s_pendingSpellNoEffect.sawHpChange ||
        s_pendingSpellNoEffect.sawStatusQueueActivity) {
        // Spell had an effect — normal HP / status announcement already
        // fired (or will fire imminently via its own queue). Stay silent.
        Log::Battle("BattleTTS: [NOEFFECT-WATCH] slot=%d had effect "
                    "(hp=%d, status=%d), no announce",
                    slot,
                    s_pendingSpellNoEffect.sawHpChange ? 1 : 0,
                    s_pendingSpellNoEffect.sawStatusQueueActivity ? 1 : 0);
        return;
    }

    // --- Step 4b: v0.13.86 heal-on-cap detection ---
    // No HP change observed AND no status change AND no flush announcement,
    // but the engine may have computed a heal that capped at max HP. The
    // displayValue at 0x01D2834A holds the engine-computed amount (verified
    // in v0.13.85 BAT: Cure on damaged Grat showed display=344 even though
    // HP delta was only +72). If the current displayValue differs from the
    // pre-action baseline AND is non-zero, the engine wrote a number to
    // visually announce the heal — we should speak it.
    //
    // Aaron's principle: announce what's shown, not what HP did. A Cure on
    // a full-HP Grat that 'would have healed 344' visually displays 344;
    // we say 'Grat recovers 344 HP' even though no HP actually moved.
    //
    // Limitation: same-value consecutive cap-heals (Cure 344 then Cure 344
    // on the same target) won't fire — baseline equals current. Acceptable.
    {
        uint16_t curDisplay = 0;
        __try {
            curDisplay = *(uint16_t*)BATTLE_DAMAGE_DISPLAY_ADDR;
        } __except(EXCEPTION_EXECUTE_HANDLER) {}

        if (curDisplay > 0 &&
            curDisplay != s_pendingSpellNoEffect.displayValueAtCast) {
            char nameBufHeal[64];
            const char* nameHeal = GetSlotName(slot, nameBufHeal,
                                               sizeof(nameBufHeal));
            char healBuf[160];
            if (nameHeal && nameHeal[0]) {
                snprintf(healBuf, sizeof(healBuf),
                         "%s recovers %u HP.", nameHeal,
                         (unsigned)curDisplay);
            } else {
                snprintf(healBuf, sizeof(healBuf),
                         "Recovers %u HP.", (unsigned)curDisplay);
            }

            // v0.13.89: route through queue so the announcement waits
            // for animation 1->0 (same pattern as no-effect / status-apply).
            // Mark the kind=4 dedup tick now (not at flush time) so any
            // late kind=4 fire stays silent.
            s_lastSpellMissAnnounceTick[slot] = now;

            NoEffect_QueueAnnouncement(slot, (int)curDisplay, healBuf,
                                       "heal-on-cap");
            Log::Battle("BattleTTS: [HEAL-ON-CAP] %s queued (slot=%d, displayVal=%u, base=%u, watchdog %ums)",
                        healBuf, slot, (unsigned)curDisplay,
                        (unsigned)s_pendingSpellNoEffect.displayValueAtCast,
                        (unsigned)elapsed);
            return;
        }
    }

    // No HP change, no status queue activity, no kind=4 announcement
    // → genuine no-effect.
    char nameBuf[64];
    const char* name = GetSlotName(slot, nameBuf, sizeof(nameBuf));
    char buf[128];
    if (name && name[0]) {
        snprintf(buf, sizeof(buf), "No effect on %s.", name);
    } else {
        snprintf(buf, sizeof(buf), "No effect.");
    }

    // v0.13.89: route through queue so the announcement waits for
    // animation 1->0. Mark the kind=4 dedup tick now (not at flush time)
    // so any late kind=4 fire stays silent.
    s_lastSpellMissAnnounceTick[slot] = now;

    NoEffect_QueueAnnouncement(slot, 0, buf, "no-effect");
    Log::Battle("BattleTTS: [SPELL-NOEFFECT] %s queued (slot=%d, watchdog %ums)",
                buf, slot, (unsigned)elapsed);
}

// ============================================================================
// External cancellation API (v0.14.51)
// ============================================================================
//
// Called by other modules (currently ScanTTS::OnScanCast) when they have
// already produced an authoritative TTS announcement for a given slot,
// and the no-effect watchdog should NOT fire its fallback for that slot.
//
// The Scan case is the motivating example: a Scan cast goes through the
// same sub_48E830 player-magic action-announce path that records the
// watchdog snapshot, but Scan deals no HP / status / display change
// because it only opens an info window. Without cancellation the
// watchdog would queue 'No effect on <target>' ~6 s after the cast,
// stepping on or contradicting the genuine Scan announcement.
//
// Cancellation clears BOTH the snapshot stage (s_pendingSpellNoEffect,
// not yet expired) AND any already-queued announcement
// (s_pendingNoEffectAnnounce, watchdog already fired but anim-flush has
// not yet flushed). In normal Scan timing only the snapshot stage is
// active; the queued-announcement clear is defensive.
static void NoEffect_CancelForSlot(int slot)
{
    if (slot < 0 || slot >= BATTLE_TOTAL_SLOTS) return;

    if (s_pendingSpellNoEffect.active &&
        s_pendingSpellNoEffect.targetSlot == slot) {
        Log::Battle("BattleTTS: [NOEFFECT-CANCEL] watchdog snapshot cleared "
                    "for slot=%d (external authoritative announcement)",
                    slot);
        s_pendingSpellNoEffect.active = false;
    }

    if (s_pendingNoEffectAnnounce.active &&
        s_pendingNoEffectAnnounce.slot == slot) {
        Log::Battle("BattleTTS: [NOEFFECT-CANCEL] queued announcement '%s' "
                    "cleared for slot=%d (external authoritative announcement)",
                    s_pendingNoEffectAnnounce.text, slot);
        s_pendingNoEffectAnnounce.active = false;
    }
}

// ============================================================================
// Reset on battle entry
// ============================================================================
static void ResetNoEffectState()
{
    s_pendingSpellNoEffect.active                = false;
    s_pendingSpellNoEffect.targetSlot            = -1;
    s_pendingSpellNoEffect.targetHpAtCast        = 0;
    s_pendingSpellNoEffect.castTick              = 0;
    s_pendingSpellNoEffect.sawHpChange           = false;
    s_pendingSpellNoEffect.sawStatusQueueActivity = false;
    s_pendingSpellNoEffect.displayValueAtCast    = 0;  // v0.13.86

    // v0.13.89: animation-synced flush queue
    s_pendingNoEffectAnnounce.active           = false;
    s_pendingNoEffectAnnounce.slot             = -1;
    s_pendingNoEffectAnnounce.value            = 0;
    s_pendingNoEffectAnnounce.queuedTick       = 0;
    s_pendingNoEffectAnnounce.animSawActive    = false;
    s_pendingNoEffectAnnounce.text[0]          = '\0';
    s_pendingNoEffectAnnounce.validateKind[0]  = '\0';

    // v0.18.3.236 (#74): flush-side dedup state
    s_lastNoEffectFlushSlot    = -1;
    s_lastNoEffectFlushTick    = 0;
    s_lastNoEffectFlushText[0] = '\0';
}
