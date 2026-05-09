// chase_kani_freeze.cpp — Capture and freeze kani's post-battle wakeup
// transition. See chase_kani_freeze.h for design notes.
//
// v0.15.2.3   — DIAGNOSTIC ONLY. Capture triggered by game-mode 3->non-3.
// v0.15.2.3.1 — Capture trigger fixed: now fires on first frame of
//               MODE_FIELD (1) after a battle, with a 10s window.
//               v0.15.2.3 fired during mode 5 (post-battle fade transition
//               where the engine pauses entity updates), capturing zero
//               byte changes.
// v0.15.2.4   — ADDS FREEZE on three animation-ID bytes (+0x150, +0x23F,\n//               +0x241). Conservative first attempt.\n// v0.15.2.5   — Layer on +0x154 and +0x1FA sub-state pins. v0.15.2.4 BAT\n//               proved the +0x150/+0x23F/+0x241 pin is purely a RENDERING\n//               pin: in domt4_1 across two consecutive captures, the\n//               FINAL SUMMARY for those three bytes was empty (pin held)\n//               but kani still woke up and triggered battles #2 and #3.\n//               Wakeup-driver bytes that v0.15.2.4 left unpinned:\n//                 +0x154 (dword LSB): 0x14 -> 0x0C (sub-state countdown)\n//                 +0x1FA (byte):      0x14 -> 0x0C (sub-state mirror)\n//               Engine appears to decrement the +0x154 LSB from 0x14 over\n//               ~5 seconds; once it crosses some threshold, AI/movement\n//               logic starts moving kani toward Squall regardless of the\n//               anim ID pin. Capture #2 INITIAL +0x154 = 0x0C \u2014 engine\n//               PERSISTED the post-wakeup value across battle/field reload\n//               (only +0x150 got reset to 0x21 by the engine's init), so\n//               each successive battle woke up faster than the last.\n//               v0.15.2.5 pins +0x150, +0x154, +0x1FA, +0x23F, +0x241 \u2014 the\n//               full \"down\" state to byte +0x14 / 0x21 values seen in the\n//               first capture's INITIAL. NEXT_SESSION_PROMPT.md scoped\n//               v0.15.2.6 for position pinning if AI still moves kani\n//               despite the full sub-state pin.\n//\n// The diagnostic capture runs alongside the freeze so the next BAT can\n// verify the pin holds: in the FINAL SUMMARY, +0x150/+0x154/+0x1FA/\n// +0x23F/+0x241 should ALL be ABSENT (no net change) if the freeze is\n// working. If kani still wakes up despite the full sub-state pin, look at\n// position bytes (+0x140-+0x148, +0x190-+0x199, +0x1B5-+0x1BD) \u2014 those\n// are the v0.15.2.6 candidates.

#include "chase_kani_freeze.h"
#include "chase_detector.h"
#include "ff8_accessibility.h"
#include "ff8_addresses.h"
#include "field_archive.h"  // v0.15.2.9: JSMCounts for all-Others scanner
#include "mod_forward_decls.h"

#include <windows.h>
#include <cstdint>
#include <cstring>

namespace ChaseKaniFreeze {

// ============================================================================
// Constants
// ============================================================================

// Game mode for battle (matches chase_detector.cpp's MODE_BATTLE_VAL).
static const uint16_t MODE_BATTLE_VAL = 3;

// Game mode for active field. v0.15.2.3.1: critical for triggering
// the capture in the right window. v0.15.2.3 BAT showed that the
// post-battle sequence is mode 3 (battle) -> mode 5 (fade-to-field
// transition, ~6 seconds, ENGINE DOES NOT UPDATE ENTITY BLOCKS) ->
// mode 1 (active field, entity state machine runs). Triggering the
// capture on mode 3 -> non-3 fired during mode 5 and caught zero
// byte changes for 5 seconds straight. We now wait for the first
// frame of mode 1 after a battle has been seen.
static const uint16_t MODE_FIELD_VAL = 1;

// Capture window duration. v0.15.2.3 used 5s and missed the wakeup
// entirely because the trigger fired during the mode-5 transition.
// v0.15.2.3.1 starts the capture at first-frame-of-mode-1, when the
// engine resumes entity updates; 10s comfortably covers the visible
// "kani on ground" period plus the standing animation onset. v0.15.2.3
// BAT timing: field re-init at 17:38:37, second battle at 17:38:44,
// so the wakeup completed within ~7 seconds of mode 1 starting.
static const DWORD CAPTURE_DURATION_MS = 10000;

// Entity strides per array (mirrors chase_detector.cpp).
static const int STRIDE_BACKGROUND = 0x1B4;
static const int STRIDE_OTHER      = 0x264;
static const int MAX_STRIDE        = STRIDE_OTHER;  // larger of the two

// Mid-window summary tick. Halfway through the new 10s window.
static const DWORD MID_SUMMARY_AT_MS = 5000;

// ============================================================================
// State
// ============================================================================

static uint16_t   s_lastGameMode      = 0xFFFF;
static bool       s_capturing         = false;
static DWORD      s_captureStartTick  = 0;
static uintptr_t  s_kaniPtr           = 0;
static int        s_strideBytes       = 0;
static int        s_arrayKind         = 0;     // 1=Backgrounds, 2=Others
static int        s_tickN             = 0;     // ticks since capture start
static bool       s_midSummaryFired   = false;

// v0.15.2.3.1: tracks whether the player has been in battle since the
// last capture (or since module init). Set to true when game mode reads
// MODE_BATTLE_VAL; cleared after a capture starts. The capture trigger
// is the FIRST FRAME of MODE_FIELD_VAL with this flag true, so we land
// in the window where the engine actively updates entities (rather than
// the mode-5 transition where the v0.15.2.3 trigger landed).
static bool       s_battleSeenRecently = false;

// v0.15.2.4: per-frame freeze state. Activated at capture start (when
// we've witnessed a chase-field battle exit), deactivated on field change.
// While active, every Update() tick re-writes the three animation-ID
// bytes (+0x150, +0x23F, +0x241) to 0x21 to keep kani locked in the
// "on the ground" pose. We re-resolve the kani pointer each frame via
// ChaseDetector::GetKaniEntityPtr() rather than caching, in case the
// engine relocates the entity arrays during the field session.
static bool       s_freezeActive       = false;
static char       s_freezeFieldName[64] = "";

// v0.15.2.7: BRUTE-FORCE FULL-STATE PIN. v0.15.2.6 BAT in domt4_1 showed
// the position pin successfully prevented kani from colliding with Squall
// (no battle), but Aaron heard kani's running animation/audio playing in
// place — the +0x150 anim ID pin doesn't drive rendered animation, and
// the engine reads other unpinned bytes (+0x162 candidate, anim shadows,
// etc.) for actual playback. Aaron's design pivot: "keep the robot from
// getting up, rather than locking it in place when it does."
//
// The brute-force fix: snapshot kani's full state region (+0x140 to end
// of stride) at t=SNAPSHOT_DELAY_MS post-activation, after Phase A re-init
// has settled but before Phase C wakeup begins. Then memcpy that snapshot
// back over every frame. Every byte the engine would otherwise modify to
// drive wakeup — anim playback, AI state, position, collision flags —
// stays at "down and settled" forever. Header bytes (+0x000..+0x028) NOT
// pinned — that's where heartbeat/frame counters live and freezing them
// could trip engine life-detection.
//
// Subsumes v0.15.2.6's position pin (those regions are inside +0x140..end).
// The five sub-state byte writes from v0.15.2.5 are preserved as belt-and-
// suspenders for the t < SNAPSHOT_DELAY_MS grace period — once the
// snapshot kicks in, the memcpy overwrites them with the same values.
//
// Field-change deactivation clears s_haveFullSnapshot so the next chase
// field captures a fresh post-Phase-A snapshot for that field's geometry.
static bool       s_haveFullSnapshot   = false;
static uint8_t    s_fullSnapshot[MAX_STRIDE];
static DWORD      s_freezeStartTick    = 0;

// Snapshot timing & range constants.
// SNAPSHOT_DELAY_MS — wait this long after FREEZE ACTIVATED before taking
// the snapshot. v0.15.2.5 BAT timing showed Phase A re-init's last write
// was at t=765ms (+0x207), Phase B is quiet, Phase C wakeup begins at
// t=5375ms. 1500ms is solidly in Phase B with margin on both sides.
// SNAPSHOT_OFFSET_START — first byte to pin. +0x140 is the start of the
// known-meaningful state region (X position dword). Below that is mostly
// zeros plus the heartbeat at +0x028 we want to NOT pin.
static const DWORD    SNAPSHOT_DELAY_MS    = 1500;
static const uint16_t SNAPSHOT_OFFSET_START = 0x140;

// v0.15.2.8: PARALLEL BATTLEYAROU PIN. v0.15.2.7 BAT in domt5_1 proved
// kani is dead code there (FINAL SUMMARY changed_bytes=0/612 over 10
// seconds, but battle still triggered). The chase battle in fields
// beyond domt4_1 must come from a different entity. Top candidate:
// 'battleyarou' (Japanese for "battle guy"), which has the same 3-method
// JSM signature as kani in BOTH domt4_1 and domt5_1, and appears as an
// Interactive Object in domt5_1's JSMScan output (param=-1, no position
// — runtime-driven).
//
// We now resolve battleyarou via ChaseDetector::GetBattleyarouEntityPtr
// in StartCapture, snapshot its full state at the same t=1500ms moment
// as kani, and memcpy back every frame. NO belt-and-suspenders byte
// writes for battleyarou — the kani byte values (0x21, 0x14) are
// kani-specific magic numbers from kani's BAT data; we don't yet know
// battleyarou's state-byte semantics. Snapshot-only is safer.
//
// Diagnostic: log INITIAL hex dump on activation and FINAL summary at
// capture end. Per-tick FIRST CHANGE diff is NOT done for battleyarou
// (would double the log volume); the INITIAL/FINAL pair is enough to
// answer (a) is battleyarou active in this field? (b) did the pin hold?
//
// If battleyarou doesn't exist in the current field (s_battleyarouPtr=0),
// the pin is inert — kani-only behavior, identical to v0.15.2.7.
static uintptr_t  s_battleyarouPtr            = 0;
static int        s_battleyarouStrideBytes    = 0;
static int        s_battleyarouArrayKind      = 0;     // 1=Backgrounds, 2=Others
static uint8_t    s_battleyarouInitial[MAX_STRIDE];
static bool       s_haveBattleyarouSnapshot   = false;
static uint8_t    s_battleyarouSnapshot[MAX_STRIDE];

// v0.15.2.9: ALL-OTHERS DIAGNOSTIC SCANNER. v0.15.2.8 BAT in domt5_1
// confirmed both kani (slot 8) and battleyarou (slot 10) are dormant
// (FINAL SUMMARY changed_bytes=0/612 for both), but battle still
// triggered. The actual chase agent is a different entity in the field.
// Candidates from JSMScan output: dic, onkyou, plane1 (Director),
// liti, gura, saidotoujou — or it could be a SETLINE-driven trigger
// zone with no entity animating.
//
// This scanner snapshots ALL Others slots' INITIAL state in StartCapture
// and reports per-slot changed_bytes counts in EndCapture. Slots with
// changed_bytes > 0 during the 10s window are running scripts/AI/anim;
// the slot with the most changes is the most likely chase-agent
// candidate. NO PIN — diagnostic only. v0.15.2.10 will pin whichever
// entity the data identifies.
//
// Memory: MAX_OTHERS × STRIDE_OTHER bytes (32 × 612 = 19,584 bytes).
static const int MAX_OTHERS = 32;
static int       s_othersCountSnapshot = 0;
static uintptr_t s_othersBaseSnapshot  = 0;
static int       s_othersStartSymIdx   = 0;     // doors+lines+bgs (for sym-name lookup)
static uint8_t   s_othersInitial[MAX_OTHERS][MAX_STRIDE];

// Snapshot at capture start (for delta summary at end).
static uint8_t    s_initial[MAX_STRIDE];

// Previous-tick snapshot (for diff'ing each frame).
static uint8_t    s_prev[MAX_STRIDE];

// Track whether each byte has had its first change logged yet. Resetting
// the tracker on capture start gives us a clean per-window timeline.
static bool       s_byteFirstChangeLogged[MAX_STRIDE];

// ============================================================================
// Helpers
// ============================================================================

static bool ReadKaniBlock(uint8_t* dst, int n)
{
    if (!s_kaniPtr || n <= 0) return false;
    __try {
        memcpy(dst, (const void*)s_kaniPtr, (size_t)n);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

static void LogHexRow(const char* label, const uint8_t* buf, int off, int len)
{
    // 16 bytes per line; len should be 16 in normal use.
    Log::Field("KaniFreeze: %s +0x%03X: "
               "%02X %02X %02X %02X %02X %02X %02X %02X  "
               "%02X %02X %02X %02X %02X %02X %02X %02X",
               label, off,
               buf[0],  buf[1],  buf[2],  buf[3],
               buf[4],  buf[5],  buf[6],  buf[7],
               buf[8],  buf[9],  buf[10], buf[11],
               buf[12], buf[13], buf[14], buf[15]);
    (void)len;  // currently fixed at 16 per call
}

static void LogInitialSnapshot()
{
    Log::Field("KaniFreeze: INITIAL snapshot (kaniPtr=0x%08X stride=0x%X arrayKind=%d):",
               (uint32_t)s_kaniPtr, s_strideBytes, s_arrayKind);
    int rows = (s_strideBytes + 15) / 16;
    for (int r = 0; r < rows; r++) {
        int off = r * 16;
        int remaining = s_strideBytes - off;
        if (remaining >= 16) {
            LogHexRow("INIT", s_initial + off, off, 16);
        } else if (remaining > 0) {
            // Pad the last partial row with zeros for readable formatting.
            uint8_t pad[16] = {};
            memcpy(pad, s_initial + off, (size_t)remaining);
            LogHexRow("INIT", pad, off, remaining);
        }
    }
}

static void LogChangeSummary(const char* label, const uint8_t* finalBuf,
                             DWORD elapsedMs)
{
    int changedCount = 0;
    for (int i = 0; i < s_strideBytes; i++) {
        if (s_initial[i] != finalBuf[i]) changedCount++;
    }
    Log::Field("KaniFreeze: %s SUMMARY t=%lums tick=%d "
               "changed_bytes=%d/%d:",
               label, (unsigned long)elapsedMs, s_tickN,
               changedCount, s_strideBytes);
    for (int i = 0; i < s_strideBytes; i++) {
        if (s_initial[i] != finalBuf[i]) {
            Log::Field("KaniFreeze:   +0x%03X: 0x%02X -> 0x%02X (delta=%d)",
                       i, s_initial[i], finalBuf[i],
                       (int)finalBuf[i] - (int)s_initial[i]);
        }
    }
}

static void StartCapture(uint16_t prevMode, uint16_t curMode)
{
    if (!ChaseDetector::IsInChaseField()) return;

    uintptr_t kaniPtr = ChaseDetector::GetKaniEntityPtr();
    if (!kaniPtr) {
        Log::Field("KaniFreeze: battle exit detected (mode %u->%u) but "
                   "GetKaniEntityPtr returned 0; skipping capture.",
                   (unsigned)prevMode, (unsigned)curMode);
        return;
    }

    ChaseDetector::KaniLocation loc = ChaseDetector::GetKaniLocation();
    int stride = (loc.arrayKind == 1) ? STRIDE_BACKGROUND
               : (loc.arrayKind == 2) ? STRIDE_OTHER
               : 0;
    if (stride == 0) {
        Log::Field("KaniFreeze: kani arrayKind=%d unrecognized; skipping capture",
                   loc.arrayKind);
        return;
    }

    // Snapshot the entity block. If the read fails, abandon — the engine
    // may not have finished restoring the field yet.
    s_kaniPtr      = kaniPtr;
    s_strideBytes  = stride;
    s_arrayKind    = loc.arrayKind;
    if (!ReadKaniBlock(s_initial, s_strideBytes)) {
        Log::Field("KaniFreeze: initial read at 0x%08X failed; skipping capture",
                   (uint32_t)kaniPtr);
        return;
    }
    memcpy(s_prev, s_initial, (size_t)s_strideBytes);
    memset(s_byteFirstChangeLogged, 0, sizeof(s_byteFirstChangeLogged));

    s_capturing        = true;
    s_captureStartTick = GetTickCount();
    s_tickN            = 0;
    s_midSummaryFired  = false;

    // v0.15.2.4: Activate the per-frame freeze. Save the current debounced
    // field name so ApplyFreezePin() can detect when the player leaves the
    // field and self-deactivate. If a battle hasn't actually been witnessed
    // (defensive: shouldn't happen since StartCapture's caller gates on
    // s_battleSeenRecently), the diagnostic still runs but the freeze
    // would be a no-op once the field changes.
    s_freezeActive = true;
    {
        const char* curField = ChaseDetector::GetDebouncedFieldName();
        size_t len = strlen(curField);
        if (len >= sizeof(s_freezeFieldName)) len = sizeof(s_freezeFieldName) - 1;
        memcpy(s_freezeFieldName, curField, len);
        s_freezeFieldName[len] = '\0';
    }
    // v0.15.2.7: Mark the brute-force pin start time on FIRST freeze
    // activation in this field. ApplyFreezePin will take the full-state
    // snapshot at SNAPSHOT_DELAY_MS post-this tick, then pin the entire
    // +0x140..stride region every frame. Subsequent captures in the same
    // field do NOT reset the start tick — they continue using the
    // existing snapshot from the first activation. Field-change
    // deactivation in ApplyFreezePin clears s_haveFullSnapshot so the
    // next chase field gets a fresh snapshot timer.
    if (!s_haveFullSnapshot) {
        s_freezeStartTick = GetTickCount();
    }

    Log::Field("KaniFreeze: FREEZE ACTIVATED — pinning +0x150/+0x23F/+0x241="
               "0x21 and +0x154/+0x1FA=0x14 every frame; full-state snapshot "
               "of +0x%03X..+0x%03X will be taken at t=%lums and pinned every "
               "frame thereafter, in field '%s' until field change",
               (unsigned)SNAPSHOT_OFFSET_START, (unsigned)s_strideBytes,
               (unsigned long)SNAPSHOT_DELAY_MS, s_freezeFieldName);

    Log::Field("KaniFreeze: ===== CAPTURE STARTED =====");
    Log::Field("KaniFreeze: trigger: mode %u->%u, field='%s', kaniPtr=0x%08X, stride=0x%X",
               (unsigned)prevMode, (unsigned)curMode,
               ChaseDetector::GetDebouncedFieldName(),
               (uint32_t)kaniPtr, stride);
    LogInitialSnapshot();

    // v0.15.2.8: parallel battleyarou capture. Resolve battleyarou's
    // runtime entity address; if present, snapshot INITIAL and log a hex
    // dump. The per-frame pin and the FINAL summary are wired in
    // ApplyFreezePin and EndCapture respectively. If battleyarou is not
    // present in this field, all battleyarou state is left zeroed and
    // the pin is inert — v0.15.2.7-equivalent behavior for that field.
    s_battleyarouPtr         = 0;
    s_battleyarouStrideBytes = 0;
    s_battleyarouArrayKind   = 0;
    {
        uintptr_t byouPtr = ChaseDetector::GetBattleyarouEntityPtr();
        ChaseDetector::KaniLocation byouLoc = ChaseDetector::GetBattleyarouLocation();
        int byouStride = (byouLoc.arrayKind == 1) ? STRIDE_BACKGROUND
                       : (byouLoc.arrayKind == 2) ? STRIDE_OTHER
                       : 0;
        if (byouPtr && byouStride > 0) {
            __try {
                memcpy(s_battleyarouInitial, (const void*)byouPtr, (size_t)byouStride);
                s_battleyarouPtr         = byouPtr;
                s_battleyarouStrideBytes = byouStride;
                s_battleyarouArrayKind   = byouLoc.arrayKind;
                Log::Field("KaniFreeze: BATTLEYAROU INITIAL snapshot "
                           "(byouPtr=0x%08X stride=0x%X arrayKind=%d "
                           "symIdx=%d arraySlot=%d):",
                           (uint32_t)byouPtr, byouStride, byouLoc.arrayKind,
                           byouLoc.symIdx, byouLoc.arraySlot);
                int rows = (byouStride + 15) / 16;
                for (int r = 0; r < rows; r++) {
                    int off = r * 16;
                    int remaining = byouStride - off;
                    if (remaining >= 16) {
                        LogHexRow("BYOU-INIT", s_battleyarouInitial + off, off, 16);
                    } else if (remaining > 0) {
                        uint8_t pad[16] = {};
                        memcpy(pad, s_battleyarouInitial + off, (size_t)remaining);
                        LogHexRow("BYOU-INIT", pad, off, remaining);
                    }
                }
            } __except (EXCEPTION_EXECUTE_HANDLER) {
                Log::Field("KaniFreeze: BATTLEYAROU initial read at 0x%08X failed; "
                           "battleyarou pin will be inactive this capture",
                           (uint32_t)byouPtr);
                s_battleyarouPtr         = 0;
                s_battleyarouStrideBytes = 0;
                s_battleyarouArrayKind   = 0;
            }
        } else {
            Log::Field("KaniFreeze: battleyarou not present in field='%s' "
                       "(symIdx=%d arraySlot=%d arrayKind=%d) — "
                       "battleyarou pin inactive this field",
                       ChaseDetector::GetDebouncedFieldName(),
                       byouLoc.symIdx, byouLoc.arraySlot, byouLoc.arrayKind);
        }
    }
    s_haveBattleyarouSnapshot = false;

    // v0.15.2.9: snapshot ALL Others slots in the current field for the
    // diagnostic scanner. Skips silently if JSMCounts unavailable, the
    // pFieldStateOthers base hasn't been allocated yet, or the field has
    // zero Others. Caps at MAX_OTHERS = 32 slots; chase fields all have
    // <= 18 Others, so the cap doesn't trigger in practice.
    s_othersCountSnapshot = 0;
    s_othersBaseSnapshot  = 0;
    s_othersStartSymIdx   = 0;
    {
        FieldArchive::JSMCounts jsmCounts = {};
        const char* curField = ChaseDetector::GetDebouncedFieldName();
        if (curField[0] != '\0' && FieldArchive::LoadJSMCounts(curField, jsmCounts)) {
            s_othersStartSymIdx = jsmCounts.doors + jsmCounts.lines + jsmCounts.backgrounds;
            int wantCount = jsmCounts.others;
            if (wantCount > MAX_OTHERS) wantCount = MAX_OTHERS;
            if (wantCount > 0 && FF8Addresses::pFieldStateOthers) {
                uint8_t* base = nullptr;
                __try {
                    base = *reinterpret_cast<uint8_t**>(FF8Addresses::pFieldStateOthers);
                } __except (EXCEPTION_EXECUTE_HANDLER) {
                    base = nullptr;
                }
                if (base) {
                    int captured = 0;
                    for (int slot = 0; slot < wantCount; slot++) {
                        uint8_t* src = base + (size_t)slot * (size_t)STRIDE_OTHER;
                        __try {
                            memcpy(s_othersInitial[slot], src, (size_t)STRIDE_OTHER);
                            captured++;
                        } __except (EXCEPTION_EXECUTE_HANDLER) {
                            memset(s_othersInitial[slot], 0, (size_t)STRIDE_OTHER);
                        }
                    }
                    s_othersCountSnapshot = wantCount;
                    s_othersBaseSnapshot  = (uintptr_t)base;
                    Log::Field("KaniFreeze: OTHERS-DIAG snapshot taken: %d/%d slots "
                               "captured, othersStartSymIdx=%d (doors=%d lines=%d "
                               "bgs=%d), othersBase=0x%08X",
                               captured, wantCount, s_othersStartSymIdx,
                               jsmCounts.doors, jsmCounts.lines, jsmCounts.backgrounds,
                               (uint32_t)base);
                } else {
                    Log::Field("KaniFreeze: OTHERS-DIAG skipped — "
                               "pFieldStateOthers not yet allocated");
                }
            } else {
                Log::Field("KaniFreeze: OTHERS-DIAG skipped — wantCount=%d", wantCount);
            }
        } else {
            Log::Field("KaniFreeze: OTHERS-DIAG skipped — "
                       "LoadJSMCounts failed for field='%s'", curField);
        }
    }
}

// Diff current vs prev snapshot; log first-change events. Returns the
// number of bytes that differ from prev this tick (NOT first-change).
static int DiffAndLogFirstChanges(const uint8_t* cur, DWORD elapsedMs)
{
    int diffCount = 0;
    for (int i = 0; i < s_strideBytes; i++) {
        if (cur[i] != s_prev[i]) {
            diffCount++;
            if (!s_byteFirstChangeLogged[i]) {
                Log::Field("KaniFreeze: t=%lums tick=%d +0x%03X: "
                           "FIRST CHANGE 0x%02X -> 0x%02X",
                           (unsigned long)elapsedMs, s_tickN,
                           i, s_prev[i], cur[i]);
                s_byteFirstChangeLogged[i] = true;
            }
            s_prev[i] = cur[i];
        }
    }
    return diffCount;
}

static void EndCapture(DWORD elapsedMs)
{
    // Read one more time so the FINAL summary reflects current state.
    uint8_t finalBuf[MAX_STRIDE];
    if (ReadKaniBlock(finalBuf, s_strideBytes)) {
        LogChangeSummary("FINAL", finalBuf, elapsedMs);
    } else {
        Log::Field("KaniFreeze: FINAL read failed; using prev buffer for summary");
        LogChangeSummary("FINAL (from prev)", s_prev, elapsedMs);
    }

    // v0.15.2.8: parallel battleyarou FINAL summary. Compare current
    // battleyarou state to the INITIAL snapshot we captured in StartCapture.
    if (s_battleyarouPtr && s_battleyarouStrideBytes > 0) {
        uint8_t byouFinal[MAX_STRIDE];
        bool readOk = false;
        __try {
            memcpy(byouFinal, (const void*)s_battleyarouPtr, (size_t)s_battleyarouStrideBytes);
            readOk = true;
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            readOk = false;
        }
        if (readOk) {
            int changedCount = 0;
            for (int i = 0; i < s_battleyarouStrideBytes; i++) {
                if (s_battleyarouInitial[i] != byouFinal[i]) changedCount++;
            }
            Log::Field("KaniFreeze: BATTLEYAROU FINAL SUMMARY t=%lums tick=%d "
                       "changed_bytes=%d/%d:",
                       (unsigned long)elapsedMs, s_tickN,
                       changedCount, s_battleyarouStrideBytes);
            for (int i = 0; i < s_battleyarouStrideBytes; i++) {
                if (s_battleyarouInitial[i] != byouFinal[i]) {
                    Log::Field("KaniFreeze:   BYOU +0x%03X: 0x%02X -> 0x%02X (delta=%d)",
                               i, s_battleyarouInitial[i], byouFinal[i],
                               (int)byouFinal[i] - (int)s_battleyarouInitial[i]);
                }
            }
        } else {
            Log::Field("KaniFreeze: BATTLEYAROU FINAL read failed; "
                       "summary not produced");
        }
    }

    // v0.15.2.9: ALL-OTHERS DIAGNOSTIC FINAL summary. For each Others slot
    // we snapshotted in StartCapture, compute changed-bytes count vs the
    // INITIAL snapshot. Log per-slot results with sym name. Slots with
    // changed_bytes > 0 are running scripts/AI/anim during the chase
    // window; the slot with the most changes is the prime suspect for
    // the chase agent.
    if (s_othersCountSnapshot > 0 && s_othersBaseSnapshot) {
        Log::Field("KaniFreeze: OTHERS-DIAG FINAL t=%lums tick=%d "
                   "(scanning %d slots, othersStartSymIdx=%d):",
                   (unsigned long)elapsedMs, s_tickN,
                   s_othersCountSnapshot, s_othersStartSymIdx);
        int activeSlots = 0;
        for (int slot = 0; slot < s_othersCountSnapshot; slot++) {
            uint8_t* src = (uint8_t*)s_othersBaseSnapshot + (size_t)slot * (size_t)STRIDE_OTHER;
            uint8_t curBuf[STRIDE_OTHER];
            bool readOk = false;
            __try {
                memcpy(curBuf, src, (size_t)STRIDE_OTHER);
                readOk = true;
            } __except (EXCEPTION_EXECUTE_HANDLER) {
                readOk = false;
            }
            int symIdx = s_othersStartSymIdx + slot;
            const char* sym = ChaseDetector::GetSymName(symIdx);
            if (!readOk) {
                Log::Field("KaniFreeze:   OTHERS-DIAG slot=%2d symIdx=%d sym='%s' READ FAILED",
                           slot, symIdx, sym);
                continue;
            }
            int changed = 0;
            for (int i = 0; i < STRIDE_OTHER; i++) {
                if (s_othersInitial[slot][i] != curBuf[i]) changed++;
            }
            if (changed > 0) activeSlots++;
            Log::Field("KaniFreeze:   OTHERS-DIAG slot=%2d symIdx=%d sym='%s' "
                       "changed_bytes=%d/%d",
                       slot, symIdx, sym, changed, STRIDE_OTHER);
        }
        Log::Field("KaniFreeze: OTHERS-DIAG summary: %d/%d slots had byte changes "
                   "during the 10s window",
                   activeSlots, s_othersCountSnapshot);
    }

    Log::Field("KaniFreeze: ===== CAPTURE COMPLETE (elapsed=%lums, ticks=%d) =====",
               (unsigned long)elapsedMs, s_tickN);
    s_capturing = false;
}

// v0.15.2.4: Per-frame freeze writer. Called every Update() while
// s_freezeActive is true. Pins kani's wakeup-control bytes until the
// player leaves the field. Self-clears when ChaseDetector reports a
// different debounced field name (skipping the empty-string debounce-
// window state to avoid spurious deactivation during transitions).
//
// v0.15.2.5: Pin set extended from {+0x150, +0x23F, +0x241}=0x21 to
// also include {+0x154, +0x1FA}=0x14. v0.15.2.4 BAT proved the anim-ID
// trio is purely a RENDERING pin — the FINAL SUMMARY confirmed those
// three bytes were never modified, but kani still woke up because the
// engine's countdown lives in +0x154 / +0x1FA.
//
// v0.15.2.7: BRUTE-FORCE FULL-STATE PIN. v0.15.2.6 BAT showed the
// position pin prevented collision in domt4_1 (no battle) but Aaron
// heard kani's running animation playing in place — the engine drives
// rendered animation from bytes we never pinned. Aaron's design pivot:
// stop the wakeup itself, not paper over it with position lock.
//
// New strategy: at t=SNAPSHOT_DELAY_MS (1500ms) post-FREEZE-ACTIVATED,
// snapshot kani's entire post-header state (+0x140..stride) and pin
// EVERY byte in that range to its post-Phase-A "settled down" value
// every subsequent frame. Anim playback drivers, AI state, position,
// collision flags — anything the engine modifies during wakeup — gets
// overwritten before it can take effect. Header bytes (+0x000..+0x028)
// stay free so the engine's heartbeat/frame counter still ticks.
//
// The five sub-state byte writes (+0x150, +0x23F, +0x241=0x21,
// +0x154/+0x1FA=0x14) are preserved during the t<SNAPSHOT_DELAY_MS
// grace period to keep kani down through Phase A. Once the full pin
// kicks in, the byte writes become redundant (the snapshot embeds
// those same values) but harmless.
//
// v0.15.2.8: PARALLEL BATTLEYAROU PIN. v0.15.2.7 BAT in domt5_1
// showed kani's full-state pin worked perfectly (FINAL SUMMARY
// changed_bytes=0/612) but battle STILL triggered — kani is dead
// code in that field. We now also pin battleyarou (resolved per
// field via ChaseDetector::GetBattleyarouEntityPtr). Snapshot-only,
// no belt-and-suspenders byte writes (kani's magic 0x21/0x14 values
// don't apply). Same SNAPSHOT_DELAY_MS=1500ms timing. If battleyarou
// is the actual chase agent in domt5_1, freezing it should prevent
// the chase battle. If battleyarou is also dead in that field,
// v0.15.2.9 will try the next candidate (plane1 Director, dic, etc.).
static void ApplyFreezePin()
{
    if (!s_freezeActive) return;

    // Field-change check. During the 2s name-debounce after a fieldId flip,
    // GetDebouncedFieldName returns "" — don't deactivate then, since we
    // don't yet know the destination field. Only deactivate on a confirmed
    // change to a different non-empty name.
    const char* curField = ChaseDetector::GetDebouncedFieldName();
    if (curField[0] != '\0' && strcmp(curField, s_freezeFieldName) != 0) {
        Log::Field("KaniFreeze: FREEZE DEACTIVATED — field changed from '%s' to '%s'",
                   s_freezeFieldName, curField);
        s_freezeActive            = false;
        s_freezeFieldName[0]      = '\0';
        s_haveFullSnapshot        = false;  // v0.15.2.7: next field gets a fresh post-Phase-A snapshot
        s_haveBattleyarouSnapshot = false;  // v0.15.2.8: same for battleyarou
        s_battleyarouPtr          = 0;
        s_battleyarouStrideBytes  = 0;
        s_battleyarouArrayKind    = 0;
        s_freezeStartTick         = 0;
        s_othersCountSnapshot     = 0;       // v0.15.2.9
        s_othersBaseSnapshot      = 0;
        s_othersStartSymIdx       = 0;
        return;
    }

    // Re-resolve kani each frame. ChaseDetector returns 0 if not in a
    // chase field or if the slot can't be resolved — transient, not an
    // error, just skip this frame.
    uintptr_t kaniPtr = ChaseDetector::GetKaniEntityPtr();
    if (!kaniPtr) return;

    // SEH-guarded: take the snapshot if it's time, then write all pinned
    // values. Two layers of pin: belt-and-suspenders byte writes (always),
    // and the full-state memcpy (once the post-Phase-A snapshot is taken).
    __try {
        uint8_t* kani = reinterpret_cast<uint8_t*>(kaniPtr);

        // v0.15.2.5 belt-and-suspenders byte pins. Cover the t<1500ms grace
        // period before the full snapshot kicks in. Once s_haveFullSnapshot
        // is true, the memcpy below overwrites these with the same values
        // (since the snapshot was taken AFTER these writes had been firing
        // for 1500ms, the snapshotted bytes are 0x21 / 0x14).
        kani[0x150] = 0x21;
        kani[0x23F] = 0x21;
        kani[0x241] = 0x21;
        kani[0x154] = 0x14;
        kani[0x1FA] = 0x14;

        // v0.15.2.7: take the full-state snapshot once per field, after
        // Phase A has settled. Then pin the snapshot every subsequent
        // frame.
        if (!s_haveFullSnapshot) {
            DWORD elapsed = GetTickCount() - s_freezeStartTick;
            if (elapsed >= SNAPSHOT_DELAY_MS) {
                int snapLen = s_strideBytes - SNAPSHOT_OFFSET_START;
                if (snapLen > 0 && snapLen <= MAX_STRIDE) {
                    memcpy(s_fullSnapshot + SNAPSHOT_OFFSET_START,
                           kani + SNAPSHOT_OFFSET_START,
                           (size_t)snapLen);
                    s_haveFullSnapshot = true;
                    Log::Field("KaniFreeze: full-state snapshot taken at t=%lums "
                               "(+0x%03X..+0x%03X = %d bytes); pinning every "
                               "frame for the rest of this field session",
                               (unsigned long)elapsed,
                               (unsigned)SNAPSHOT_OFFSET_START,
                               (unsigned)s_strideBytes,
                               snapLen);
                }
            }
        } else {
            int snapLen = s_strideBytes - SNAPSHOT_OFFSET_START;
            if (snapLen > 0 && snapLen <= MAX_STRIDE) {
                memcpy(kani + SNAPSHOT_OFFSET_START,
                       s_fullSnapshot + SNAPSHOT_OFFSET_START,
                       (size_t)snapLen);
            }
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        // Bad pointer this frame — skip silently. Will retry next tick.
    }

    // v0.15.2.8: parallel battleyarou pin. Skip if battleyarou not present
    // in the current field (s_battleyarouPtr == 0). Snapshot at the same
    // t=1500ms moment as kani; memcpy back every frame thereafter. NO
    // belt-and-suspenders byte writes (kani's 0x21/0x14 magic values are
    // kani-specific). SEH-guarded separately so a bad battleyarou pointer
    // doesn't take out the kani pin path.
    if (s_battleyarouPtr) {
        __try {
            uint8_t* byou = reinterpret_cast<uint8_t*>(s_battleyarouPtr);
            if (!s_haveBattleyarouSnapshot) {
                DWORD elapsed = GetTickCount() - s_freezeStartTick;
                if (elapsed >= SNAPSHOT_DELAY_MS) {
                    int snapLen = s_battleyarouStrideBytes - SNAPSHOT_OFFSET_START;
                    if (snapLen > 0 && snapLen <= MAX_STRIDE) {
                        memcpy(s_battleyarouSnapshot + SNAPSHOT_OFFSET_START,
                               byou + SNAPSHOT_OFFSET_START,
                               (size_t)snapLen);
                        s_haveBattleyarouSnapshot = true;
                        Log::Field("KaniFreeze: BATTLEYAROU full-state snapshot "
                                   "taken at t=%lums (+0x%03X..+0x%03X = %d bytes); "
                                   "pinning every frame for the rest of this field "
                                   "session",
                                   (unsigned long)elapsed,
                                   (unsigned)SNAPSHOT_OFFSET_START,
                                   (unsigned)s_battleyarouStrideBytes,
                                   snapLen);
                    }
                }
            } else {
                int snapLen = s_battleyarouStrideBytes - SNAPSHOT_OFFSET_START;
                if (snapLen > 0 && snapLen <= MAX_STRIDE) {
                    memcpy(byou + SNAPSHOT_OFFSET_START,
                           s_battleyarouSnapshot + SNAPSHOT_OFFSET_START,
                           (size_t)snapLen);
                }
            }
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            // Bad battleyarou pointer this frame — skip silently.
        }
    }
}

// ============================================================================
// Public API
// ============================================================================

void Initialize()
{
    s_lastGameMode       = 0xFFFF;
    s_capturing          = false;
    s_captureStartTick   = 0;
    s_kaniPtr            = 0;
    s_strideBytes        = 0;
    s_arrayKind          = 0;
    s_tickN              = 0;
    s_midSummaryFired    = false;
    s_battleSeenRecently = false;
    s_freezeActive            = false;
    s_freezeFieldName[0]      = '\0';
    s_haveFullSnapshot        = false;
    s_freezeStartTick         = 0;
    s_battleyarouPtr          = 0;
    s_battleyarouStrideBytes  = 0;
    s_battleyarouArrayKind    = 0;
    s_haveBattleyarouSnapshot = false;
    s_othersCountSnapshot     = 0;
    s_othersBaseSnapshot      = 0;
    s_othersStartSymIdx       = 0;
    Log::Mod("ChaseKaniFreeze: Initialized (v0.15.2.9 DIAGNOSTIC + DUAL-ENTITY "
             "FULL-STATE PIN + ALL-OTHERS SCANNER; captures kani entity bytes "
             "for %lums after first frame of MODE_FIELD following any chase-field "
             "battle, AND pins +0x150/+0x23F/+0x241=0x21, +0x154/+0x1FA=0x14 "
             "every frame, AND snapshots BOTH kani and battleyarou entity "
             "+0x%03X..stride regions at t=%lums post-activation then pins both "
             "snapshots every frame until field change, AND snapshots ALL Others "
             "slots in the field for per-slot changed-bytes diagnostic at "
             "capture-end — v0.15.2.10 will pin whichever slot the diagnostic "
             "identifies as the active chase agent).",
             (unsigned long)CAPTURE_DURATION_MS,
             (unsigned)SNAPSHOT_OFFSET_START,
             (unsigned long)SNAPSHOT_DELAY_MS);
}

void Shutdown()
{
    s_capturing               = false;
    s_freezeActive            = false;
    s_haveFullSnapshot        = false;
    s_freezeStartTick         = 0;
    s_battleyarouPtr          = 0;
    s_battleyarouStrideBytes  = 0;
    s_battleyarouArrayKind    = 0;
    s_haveBattleyarouSnapshot = false;
    s_othersCountSnapshot     = 0;
    s_othersBaseSnapshot      = 0;
    s_othersStartSymIdx       = 0;
}

void Update()
{
    // Read current game mode under SEH.
    if (!FF8Addresses::pGameMode) return;
    uint16_t curMode = 0xFFFF;
    __try { curMode = *FF8Addresses::pGameMode; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return; }

    // v0.15.2.3.1 trigger: first frame of MODE_FIELD (1) after a battle
    // (3) has been seen. v0.15.2.3 used the simpler edge prev==3 && cur!=3,
    // which fired during mode 5 (post-battle fade transition) where the
    // engine has all entity updates paused — the BAT log captured 0 byte
    // changes for the entire 5s window because of this. We now defer the
    // capture start until the engine has actually returned to active
    // field-mode and entity state machines are running again.
    if (curMode == MODE_BATTLE_VAL) {
        s_battleSeenRecently = true;
    }
    if (!s_capturing
        && s_battleSeenRecently
        && curMode == MODE_FIELD_VAL
        && s_lastGameMode != MODE_FIELD_VAL)
    {
        StartCapture(s_lastGameMode, curMode);
        s_battleSeenRecently = false;  // re-arm only after the next battle
    }
    s_lastGameMode = curMode;

    // v0.15.2.4: Apply per-frame freeze pin (no-op if !s_freezeActive).
    // Runs regardless of whether the diagnostic capture is currently
    // active — the freeze persists past the 10s capture window, until
    // the player leaves the field.
    ApplyFreezePin();

    // If we're not capturing, nothing else to do.
    if (!s_capturing) return;

    // Drive the capture loop.
    DWORD now = GetTickCount();
    DWORD elapsed = now - s_captureStartTick;
    s_tickN++;

    uint8_t cur[MAX_STRIDE];
    if (!ReadKaniBlock(cur, s_strideBytes)) {
        Log::Field("KaniFreeze: read failed mid-capture at t=%lums; aborting",
                   (unsigned long)elapsed);
        s_capturing = false;
        return;
    }

    int diffCount = DiffAndLogFirstChanges(cur, elapsed);

    // Mid-window summary: a short pulse so the log shows progress even if
    // the transition fires near the end of the window.
    if (!s_midSummaryFired && elapsed >= MID_SUMMARY_AT_MS) {
        int totalChanged = 0;
        for (int i = 0; i < s_strideBytes; i++) {
            if (s_byteFirstChangeLogged[i]) totalChanged++;
        }
        Log::Field("KaniFreeze: MID-WINDOW heartbeat t=%lums tick=%d "
                   "diff_this_tick=%d total_changed_so_far=%d",
                   (unsigned long)elapsed, s_tickN, diffCount, totalChanged);
        s_midSummaryFired = true;
    }

    // End of window?
    if (elapsed >= CAPTURE_DURATION_MS) {
        EndCapture(elapsed);
    }
}

}  // namespace ChaseKaniFreeze
