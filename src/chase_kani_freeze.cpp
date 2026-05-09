// chase_kani_freeze.cpp -- Chase-agent freeze module. See header for v0.15.3 design.
//
// v0.15.3: SINGLE-PRONGED CLEANUP. Removed the static kani+battleyarou pin
// (v0.15.2.7-.8 layered design) entirely. v0.15.2.15 BAT confirmed the dynamic
// chase-agent pin (v0.15.2.14) handles every chase field that is not
// doopen2a; doopen2a uses chase_battle_freeze's strcmp guard plus the BATTLE
// NO-OP. The static kani+battleyarou pin proved inert in every chase field
// tested (kani 0-7 changed bytes, battleyarou 0 changed bytes per OTHERS-DIAG)
// because the actual chase agents in those fields were rinoa-slot, director0,
// etc., NOT kani or battleyarou. Removing the dead code reduces the per-frame
// cost in chase fields and removes a class of stale-pointer hazards on field
// handoff.
//
// Current design retained:
//   (1) Dynamic chase-agent pin: RegisterChaseAgent() arms a per-field full-
//       state pin on the BATTLE caller. StartCapture snapshots its INITIAL
//       state; ApplyFreezePin takes a full-state snapshot 1500ms in and pins
//       every byte every frame thereafter; EndCapture logs CHASE-AGENT FINAL
//       SUMMARY.
//
//   (2) fieldId-flip deactivation (v0.15.2.14): the freeze deactivates
//       IMMEDIATELY when pCurrentFieldId differs from the value captured at
//       FREEZE ACTIVATED time, before the 2-second name debounce settles.
//       Fixes the doopen2a -> dotown_3 handoff crash that recurred in
//       v0.15.2.10 / v0.15.2.13 BATs.
//
//   (3) OTHERS-DIAG diagnostic scanner (v0.15.2.9): at StartCapture,
//       snapshots all Others-array entities; at EndCapture, logs per-slot
//       byte-change counts. Useful for identifying chase agents in fields
//       where RegisterChaseAgent fails to resolve, and for auditing entity
//       behavior drift.
//
// Earlier v0.15.2.x history -- terse:
//   v0.15.2.3   -- DIAGNOSTIC capture (byte-diff window).
//   v0.15.2.3.1 -- Trigger fix: first frame of MODE_FIELD post-battle.
//   v0.15.2.4   -- Static kani anim-ID pin.
//   v0.15.2.5   -- +0x154 / +0x1FA sub-state layer.
//   v0.15.2.6   -- Position pin (subsumed).
//   v0.15.2.7   -- Static kani full-state pin.
//   v0.15.2.8   -- Parallel battleyarou pin.
//   v0.15.2.9   -- All-Others diagnostic scanner.
//   v0.15.2.10/.11 -- chase-field set adjustments.
//   v0.15.2.12  -- Passive-observer BATTLE hook.
//   v0.15.2.13  -- Active BATTLE NO-OP.
//   v0.15.2.14  -- Dynamic chase-agent pin + tightened deactivation.
//   v0.15.2.15  -- Surgical doopen2a skip in chase_battle_freeze.
//   v0.15.3     -- Removed static kani+battleyarou pin (this version).

#include "chase_kani_freeze.h"
#include "chase_detector.h"
#include "ff8_accessibility.h"
#include "ff8_addresses.h"
#include "field_archive.h"
#include "mod_forward_decls.h"

#include <windows.h>
#include <cstdint>
#include <cstring>
#include <cstdio>

namespace ChaseKaniFreeze {

// ============================================================================
// Constants
// ============================================================================

static const uint16_t MODE_BATTLE_VAL = 3;
static const uint16_t MODE_FIELD_VAL  = 1;
static const DWORD    CAPTURE_DURATION_MS = 10000;
static const int      STRIDE_BACKGROUND = 0x1B4;
static const int      STRIDE_OTHER      = 0x264;
static const int      MAX_STRIDE        = STRIDE_OTHER;
static const DWORD    SNAPSHOT_DELAY_MS = 1500;
static const uint16_t SNAPSHOT_OFFSET_START = 0x140;
static const int      MAX_OTHERS = 32;

// ============================================================================
// State
// ============================================================================

// --- Capture trigger / general
static uint16_t   s_lastGameMode      = 0xFFFF;
static bool       s_capturing         = false;
static DWORD      s_captureStartTick  = 0;
static int        s_tickN             = 0;
static bool       s_battleSeenRecently = false;

// --- Freeze (drives fieldId-flip deactivation; armed by StartCapture)
static bool       s_freezeActive      = false;
static char       s_freezeFieldName[64] = "";
static uint16_t   s_freezeFieldId     = 0xFFFF;
static DWORD      s_freezeStartTick   = 0;

// --- All-Others diagnostic scanner (v0.15.2.9, retained as diagnostic)
static int        s_othersCountSnapshot = 0;
static uintptr_t  s_othersBaseSnapshot  = 0;
static int        s_othersStartSymIdx   = 0;
static uint8_t    s_othersInitial[MAX_OTHERS][MAX_STRIDE];

// --- Dynamic chase-agent pin (v0.15.2.14)
//
// Set by RegisterChaseAgent (called from chase_battle_freeze on first PASS
// per chase field, except doopen2a). Read by StartCapture, ApplyFreezePin,
// EndCapture. Cleared on field change inside ApplyFreezePin.
//
// Write order in RegisterChaseAgent: identity first, then pointer LAST. The
// non-zero pointer is the "armed" signal observed by ApplyFreezePin from
// the mod thread. On x86, 32-bit aligned scalar writes are atomic and
// store-store reordering is forbidden, so the mod thread sees a coherent
// view: either uninitialized (s_chaseAgentPtr == 0) or fully populated.
static uintptr_t  s_chaseAgentPtr            = 0;
static int        s_chaseAgentStrideBytes    = 0;
static int        s_chaseAgentArrayKind      = 0;     // 1=Backgrounds, 2=Others
static int        s_chaseAgentSlot           = -1;
static int        s_chaseAgentSymIdx         = -1;
static char       s_chaseAgentSymName[32]    = "";
static char       s_chaseAgentFieldName[64]  = "";
static uint8_t    s_chaseAgentInitial[MAX_STRIDE];
static bool       s_haveChaseAgentInitial    = false;
static uint8_t    s_chaseAgentSnapshot[MAX_STRIDE];
static bool       s_haveChaseAgentSnapshot   = false;

// ============================================================================
// Helpers
// ============================================================================

static void LogHexRow(const char* label, const uint8_t* buf, int off, int /*len*/)
{
    Log::Field("KaniFreeze: %s +0x%03X: "
               "%02X %02X %02X %02X %02X %02X %02X %02X  "
               "%02X %02X %02X %02X %02X %02X %02X %02X",
               label, off,
               buf[0],  buf[1],  buf[2],  buf[3],
               buf[4],  buf[5],  buf[6],  buf[7],
               buf[8],  buf[9],  buf[10], buf[11],
               buf[12], buf[13], buf[14], buf[15]);
}

// v0.15.2.14: Read the raw current fieldId from FF8Addresses, SEH-guarded.
// Returns 0xFFFF if the address isn't resolved or the read faults.
static uint16_t ReadCurrentFieldId()
{
    if (!FF8Addresses::pCurrentFieldId) return 0xFFFF;
    __try {
        return *FF8Addresses::pCurrentFieldId;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 0xFFFF;
    }
}

// v0.15.2.14: Reset all chase-agent state. Called on field change and at
// Initialize/Shutdown.
static void ClearChaseAgent()
{
    s_chaseAgentPtr            = 0;
    s_chaseAgentStrideBytes    = 0;
    s_chaseAgentArrayKind      = 0;
    s_chaseAgentSlot           = -1;
    s_chaseAgentSymIdx         = -1;
    s_chaseAgentSymName[0]     = '\0';
    s_chaseAgentFieldName[0]   = '\0';
    s_haveChaseAgentInitial    = false;
    s_haveChaseAgentSnapshot   = false;
}

// ============================================================================
// StartCapture
// ============================================================================

static void StartCapture(uint16_t prevMode, uint16_t curMode)
{
    if (!ChaseDetector::IsInChaseField()) return;

    s_capturing        = true;
    s_captureStartTick = GetTickCount();
    s_tickN            = 0;

    s_freezeActive = true;
    {
        const char* curField = ChaseDetector::GetDebouncedFieldName();
        size_t len = strlen(curField);
        if (len >= sizeof(s_freezeFieldName)) len = sizeof(s_freezeFieldName) - 1;
        memcpy(s_freezeFieldName, curField, len);
        s_freezeFieldName[len] = '\0';
    }
    // v0.15.2.14: capture raw fieldId for fast deactivation in ApplyFreezePin.
    s_freezeFieldId   = ReadCurrentFieldId();
    s_freezeStartTick = GetTickCount();

    Log::Field("KaniFreeze: FREEZE ACTIVATED -- v0.15.3 dynamic agent pin only "
               "(static kani+battleyarou pin removed); in field '%s' "
               "(fieldId=0x%04X) until field change",
               s_freezeFieldName, (unsigned)s_freezeFieldId);

    Log::Field("KaniFreeze: ===== CAPTURE STARTED =====");
    Log::Field("KaniFreeze: trigger: mode %u->%u, field='%s'",
               (unsigned)prevMode, (unsigned)curMode,
               ChaseDetector::GetDebouncedFieldName());

    // v0.15.2.14: Chase-agent INITIAL capture if RegisterChaseAgent has armed
    // an agent for this field. SEH-guarded; on read failure, agent pin stays
    // disarmed (the BATTLE NO-OP carries the load). Reset s_haveChaseAgentSnapshot
    // so ApplyFreezePin will take a fresh full-state snapshot at SNAPSHOT_DELAY_MS.
    s_haveChaseAgentInitial  = false;
    s_haveChaseAgentSnapshot = false;
    if (s_chaseAgentPtr && s_chaseAgentStrideBytes > 0) {
        __try {
            memcpy(s_chaseAgentInitial, (const void*)s_chaseAgentPtr,
                   (size_t)s_chaseAgentStrideBytes);
            s_haveChaseAgentInitial = true;
            Log::Field("KaniFreeze: CHASE-AGENT INITIAL snapshot "
                       "(agentPtr=0x%08X stride=0x%X arrayKind=%d slot=%d "
                       "symIdx=%d sym='%s'):",
                       (uint32_t)s_chaseAgentPtr, s_chaseAgentStrideBytes,
                       s_chaseAgentArrayKind, s_chaseAgentSlot,
                       s_chaseAgentSymIdx, s_chaseAgentSymName);
            int rows = (s_chaseAgentStrideBytes + 15) / 16;
            for (int r = 0; r < rows; r++) {
                int off = r * 16;
                int remaining = s_chaseAgentStrideBytes - off;
                if (remaining >= 16) {
                    LogHexRow("AGENT-INIT", s_chaseAgentInitial + off, off, 16);
                } else if (remaining > 0) {
                    uint8_t pad[16] = {};
                    memcpy(pad, s_chaseAgentInitial + off, (size_t)remaining);
                    LogHexRow("AGENT-INIT", pad, off, remaining);
                }
            }
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            Log::Field("KaniFreeze: CHASE-AGENT initial read at 0x%08X failed -- "
                       "agent pin will be inactive this capture",
                       (uint32_t)s_chaseAgentPtr);
        }
    } else {
        Log::Field("KaniFreeze: no chase agent registered for field='%s' -- "
                   "agent pin inactive (BATTLE NO-OP is the only suppression)",
                   ChaseDetector::GetDebouncedFieldName());
    }

    // All-Others diagnostic scanner (unchanged from v0.15.2.9).
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
                }
            }
        }
    }
}

// ============================================================================
// EndCapture
// ============================================================================

static void EndCapture(DWORD elapsedMs)
{
    // v0.15.2.14: Chase-agent FINAL summary. Compares current state to
    // s_chaseAgentInitial. The success metric is changed_bytes ~ 0 -- meaning
    // the pin is holding the agent still after the snapshot kicks in.
    if (s_chaseAgentPtr && s_chaseAgentStrideBytes > 0 && s_haveChaseAgentInitial) {
        uint8_t agentFinal[MAX_STRIDE];
        bool readOk = false;
        __try {
            memcpy(agentFinal, (const void*)s_chaseAgentPtr,
                   (size_t)s_chaseAgentStrideBytes);
            readOk = true;
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            readOk = false;
        }
        if (readOk) {
            int changedCount = 0;
            for (int i = 0; i < s_chaseAgentStrideBytes; i++) {
                if (s_chaseAgentInitial[i] != agentFinal[i]) changedCount++;
            }
            Log::Field("KaniFreeze: CHASE-AGENT FINAL SUMMARY t=%lums tick=%d "
                       "sym='%s' changed_bytes=%d/%d:",
                       (unsigned long)elapsedMs, s_tickN, s_chaseAgentSymName,
                       changedCount, s_chaseAgentStrideBytes);
            for (int i = 0; i < s_chaseAgentStrideBytes; i++) {
                if (s_chaseAgentInitial[i] != agentFinal[i]) {
                    Log::Field("KaniFreeze:   AGENT +0x%03X: 0x%02X -> 0x%02X (delta=%d)",
                               i, s_chaseAgentInitial[i], agentFinal[i],
                               (int)agentFinal[i] - (int)s_chaseAgentInitial[i]);
                }
            }
        }
    }

    // All-Others diagnostic FINAL (unchanged from v0.15.2.9).
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
        Log::Field("KaniFreeze: OTHERS-DIAG summary: %d/%d slots had byte changes",
                   activeSlots, s_othersCountSnapshot);
    }

    Log::Field("KaniFreeze: ===== CAPTURE COMPLETE (elapsed=%lums, ticks=%d) =====",
               (unsigned long)elapsedMs, s_tickN);
    s_capturing = false;
}

// ============================================================================
// ApplyFreezePin
// ============================================================================
//
// v0.15.3: only the dynamic chase-agent pin is active here. The static
// kani+battleyarou pin (v0.15.2.7-.8) was removed -- v0.15.2.x BATs proved
// kani and battleyarou were inert in every chase field; the actual chase
// agents (rinoa-slot in domt5_1, robot-slots elsewhere) are pinned via
// RegisterChaseAgent.
//
// v0.15.2.14 tightened deactivation preserved: capture pCurrentFieldId at
// FREEZE ACTIVATED time and check it every frame. If it differs, deactivate
// immediately; the existing debounced-name check stays as backup. Plus an
// SEH-guarded probe before each write: if the agent pointer faults, skip.

static void DeactivateFreeze(const char* reasonFmt, ...)
{
    char reason[256];
    va_list ap;
    va_start(ap, reasonFmt);
    vsnprintf(reason, sizeof(reason), reasonFmt, ap);
    va_end(ap);

    Log::Field("KaniFreeze: FREEZE DEACTIVATED -- %s", reason);
    s_freezeActive            = false;
    s_freezeFieldName[0]      = '\0';
    s_freezeFieldId           = 0xFFFF;
    s_freezeStartTick         = 0;
    s_othersCountSnapshot     = 0;
    s_othersBaseSnapshot      = 0;
    s_othersStartSymIdx       = 0;
    ClearChaseAgent();
}

static void ApplyFreezePin()
{
    if (!s_freezeActive) return;

    // v0.15.2.14: raw fieldId check FIRST (catches handoff before debounce).
    uint16_t curFieldId = ReadCurrentFieldId();
    if (curFieldId != 0xFFFF && s_freezeFieldId != 0xFFFF
        && curFieldId != s_freezeFieldId) {
        DeactivateFreeze("fieldId changed 0x%04X -> 0x%04X (pre-debounce)",
                         (unsigned)s_freezeFieldId, (unsigned)curFieldId);
        return;
    }

    // Backup: debounced-name check (existing logic).
    const char* curField = ChaseDetector::GetDebouncedFieldName();
    if (curField[0] != '\0' && strcmp(curField, s_freezeFieldName) != 0) {
        DeactivateFreeze("field changed from '%s' to '%s' (debounce-settled)",
                         s_freezeFieldName, curField);
        return;
    }

    // ----- v0.15.2.14: Chase-agent pin (the only pin in v0.15.3) -----
    //
    // The agent is the actual entity that called BATTLE in this field's
    // first chase encounter (e.g. rinoa-slot in domt5_1). We pin its
    // full post-header state region to keep it on the ground after the
    // first battle exits, so it never wakes up and follows Squall around.
    if (s_chaseAgentPtr) {
        __try {
            volatile uint8_t probe = *reinterpret_cast<uint8_t*>(s_chaseAgentPtr);
            (void)probe;

            uint8_t* agent = reinterpret_cast<uint8_t*>(s_chaseAgentPtr);
            if (!s_haveChaseAgentSnapshot) {
                DWORD elapsed = GetTickCount() - s_freezeStartTick;
                if (elapsed >= SNAPSHOT_DELAY_MS) {
                    int snapLen = s_chaseAgentStrideBytes - SNAPSHOT_OFFSET_START;
                    if (snapLen > 0 && snapLen <= MAX_STRIDE) {
                        memcpy(s_chaseAgentSnapshot + SNAPSHOT_OFFSET_START,
                               agent + SNAPSHOT_OFFSET_START,
                               (size_t)snapLen);
                        s_haveChaseAgentSnapshot = true;
                        Log::Field("KaniFreeze: CHASE-AGENT full-state snapshot "
                                   "taken at t=%lums sym='%s' (+0x%03X..+0x%03X = "
                                   "%d bytes); pinning every frame for the rest "
                                   "of this field session",
                                   (unsigned long)elapsed, s_chaseAgentSymName,
                                   (unsigned)SNAPSHOT_OFFSET_START,
                                   (unsigned)s_chaseAgentStrideBytes, snapLen);
                    }
                }
            } else {
                int snapLen = s_chaseAgentStrideBytes - SNAPSHOT_OFFSET_START;
                if (snapLen > 0 && snapLen <= MAX_STRIDE) {
                    memcpy(agent + SNAPSHOT_OFFSET_START,
                           s_chaseAgentSnapshot + SNAPSHOT_OFFSET_START,
                           (size_t)snapLen);
                }
            }
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            // Bad agent pointer this frame -- skip silently.
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
    s_tickN              = 0;
    s_battleSeenRecently = false;
    s_freezeActive       = false;
    s_freezeFieldName[0] = '\0';
    s_freezeFieldId      = 0xFFFF;
    s_freezeStartTick    = 0;
    s_othersCountSnapshot = 0;
    s_othersBaseSnapshot  = 0;
    s_othersStartSymIdx   = 0;
    ClearChaseAgent();

    Log::Mod("ChaseKaniFreeze: Initialized (v0.15.3 DYNAMIC AGENT PIN ONLY). "
             "On chase-field battle exit, pins the dynamically-registered "
             "chase agent (the entity that called BATTLE on first contact in "
             "the field). Static kani+battleyarou pin REMOVED in v0.15.3 -- "
             "v0.15.2.x BATs proved both entities were inert (0-7 changed bytes) "
             "in every chase field; the actual chase agents are pinned via "
             "RegisterChaseAgent. fieldId-flip deactivation kept (fixes the "
             "doopen2a -> dotown_3 handoff crash from v0.15.2.10/.13). "
             "OTHERS-DIAG diagnostic scanner kept for future agent-resolution "
             "audits. chase_battle_freeze handles registration via "
             "RegisterChaseAgent on the first PASS event per field (with "
             "v0.15.2.15 doopen2a skip).");
}

void Shutdown()
{
    s_capturing = false;
    s_freezeActive = false;
    ClearChaseAgent();
}

void Update()
{
    if (!FF8Addresses::pGameMode) return;
    uint16_t curMode = 0xFFFF;
    __try { curMode = *FF8Addresses::pGameMode; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return; }

    if (curMode == MODE_BATTLE_VAL) {
        s_battleSeenRecently = true;
    }
    if (!s_capturing
        && s_battleSeenRecently
        && curMode == MODE_FIELD_VAL
        && s_lastGameMode != MODE_FIELD_VAL)
    {
        StartCapture(s_lastGameMode, curMode);
        s_battleSeenRecently = false;
    }
    s_lastGameMode = curMode;

    ApplyFreezePin();

    if (!s_capturing) return;

    DWORD now = GetTickCount();
    DWORD elapsed = now - s_captureStartTick;
    s_tickN++;

    if (elapsed >= CAPTURE_DURATION_MS) {
        EndCapture(elapsed);
    }
}

// v0.15.2.14: Resolve entityPtr to (arrayKind, slot, symIdx, symName) and
// arm the chase-agent pin. Unchanged in v0.15.3.
void RegisterChaseAgent(uintptr_t entityPtr)
{
    if (!entityPtr) return;

    // Idempotent within a field: if we've already armed the same pointer
    // for the current field, skip silently.
    const char* curField = ChaseDetector::GetDebouncedFieldName();
    if (s_chaseAgentPtr == entityPtr
        && curField[0] != '\0'
        && strcmp(curField, s_chaseAgentFieldName) == 0) {
        return;
    }

    if (curField[0] == '\0') {
        Log::Field("[CHASE-AGENT-UNRESOLVED] entityPtr=0x%08X registered before "
                   "field name debounced -- skipping (BATTLE NO-OP carries the load)",
                   (uint32_t)entityPtr);
        return;
    }

    FieldArchive::JSMCounts jsmCounts = {};
    if (!FieldArchive::LoadJSMCounts(curField, jsmCounts)) {
        Log::Field("[CHASE-AGENT-UNRESOLVED] field='%s' entityPtr=0x%08X -- "
                   "JSMCounts unavailable, cannot resolve slot",
                   curField, (uint32_t)entityPtr);
        return;
    }

    int doors = jsmCounts.doors;
    int lines = jsmCounts.lines;
    int bgs   = jsmCounts.backgrounds;
    int oths  = jsmCounts.others;

    int  resolvedArrayKind = 0;
    int  resolvedSlot      = -1;
    int  resolvedStride    = 0;
    int  resolvedSymIdx    = -1;
    uintptr_t othersBase = 0;
    uintptr_t bgsBase    = 0;

    // Try Others array.
    if (oths > 0 && FF8Addresses::pFieldStateOthers) {
        __try {
            othersBase = (uintptr_t)*reinterpret_cast<uint8_t**>(
                FF8Addresses::pFieldStateOthers);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            othersBase = 0;
        }
        if (othersBase) {
            if (entityPtr >= othersBase) {
                uintptr_t off = entityPtr - othersBase;
                if ((off % (uintptr_t)STRIDE_OTHER) == 0) {
                    int slot = (int)(off / (uintptr_t)STRIDE_OTHER);
                    if (slot >= 0 && slot < oths) {
                        resolvedArrayKind = 2;  // Others
                        resolvedSlot      = slot;
                        resolvedStride    = STRIDE_OTHER;
                        resolvedSymIdx    = doors + lines + bgs + slot;
                    }
                }
            }
        }
    }

    // Try Backgrounds array if Others didn't resolve.
    if (resolvedArrayKind == 0 && bgs > 0 && FF8Addresses::pFieldStateBackgrounds) {
        __try {
            bgsBase = (uintptr_t)*reinterpret_cast<uint8_t**>(
                FF8Addresses::pFieldStateBackgrounds);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            bgsBase = 0;
        }
        if (bgsBase) {
            if (entityPtr >= bgsBase) {
                uintptr_t off = entityPtr - bgsBase;
                if ((off % (uintptr_t)STRIDE_BACKGROUND) == 0) {
                    int slot = (int)(off / (uintptr_t)STRIDE_BACKGROUND);
                    if (slot >= 0 && slot < bgs) {
                        resolvedArrayKind = 1;  // Backgrounds
                        resolvedSlot      = slot;
                        resolvedStride    = STRIDE_BACKGROUND;
                        resolvedSymIdx    = doors + lines + slot;
                    }
                }
            }
        }
    }

    if (resolvedArrayKind == 0) {
        Log::Field("[CHASE-AGENT-UNRESOLVED] field='%s' entityPtr=0x%08X "
                   "(othersBase=0x%08X stride=0x%X count=%d, "
                   "bgsBase=0x%08X stride=0x%X count=%d) -- "
                   "pointer doesn't lie inside either array, skipping pin",
                   curField, (uint32_t)entityPtr,
                   (uint32_t)othersBase, STRIDE_OTHER, oths,
                   (uint32_t)bgsBase, STRIDE_BACKGROUND, bgs);
        return;
    }

    const char* sym = ChaseDetector::GetSymName(resolvedSymIdx);

    // Read first 16 bytes of header for fingerprint.
    uint8_t header[16] = {};
    bool headerOk = false;
    __try {
        memcpy(header, (const void*)entityPtr, 16);
        headerOk = true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        headerOk = false;
    }

    // Commit identity FIRST, then pointer LAST so the mod thread sees a
    // coherent armed view.
    s_chaseAgentStrideBytes = resolvedStride;
    s_chaseAgentArrayKind   = resolvedArrayKind;
    s_chaseAgentSlot        = resolvedSlot;
    s_chaseAgentSymIdx      = resolvedSymIdx;
    {
        size_t len = strlen(sym);
        if (len >= sizeof(s_chaseAgentSymName)) len = sizeof(s_chaseAgentSymName) - 1;
        memcpy(s_chaseAgentSymName, sym, len);
        s_chaseAgentSymName[len] = '\0';
    }
    {
        size_t len = strlen(curField);
        if (len >= sizeof(s_chaseAgentFieldName)) len = sizeof(s_chaseAgentFieldName) - 1;
        memcpy(s_chaseAgentFieldName, curField, len);
        s_chaseAgentFieldName[len] = '\0';
    }
    s_haveChaseAgentInitial  = false;
    s_haveChaseAgentSnapshot = false;
    s_chaseAgentPtr = entityPtr;  // armed signal -- write LAST

    if (headerOk) {
        Log::Field("[CHASE-AGENT] field='%s' entityPtr=0x%08X "
                   "-> array=%s slot=%d symIdx=%d sym='%s' "
                   "stride=0x%X "
                   "header[0x00..0x10]: %02X %02X %02X %02X %02X %02X %02X %02X "
                   "%02X %02X %02X %02X %02X %02X %02X %02X",
                   curField, (uint32_t)entityPtr,
                   (resolvedArrayKind == 2) ? "Others" : "Backgrounds",
                   resolvedSlot, resolvedSymIdx, sym, resolvedStride,
                   header[0], header[1], header[2], header[3],
                   header[4], header[5], header[6], header[7],
                   header[8], header[9], header[10], header[11],
                   header[12], header[13], header[14], header[15]);
    } else {
        Log::Field("[CHASE-AGENT] field='%s' entityPtr=0x%08X "
                   "-> array=%s slot=%d symIdx=%d sym='%s' stride=0x%X "
                   "(header read faulted)",
                   curField, (uint32_t)entityPtr,
                   (resolvedArrayKind == 2) ? "Others" : "Backgrounds",
                   resolvedSlot, resolvedSymIdx, sym, resolvedStride);
    }
}

}  // namespace ChaseKaniFreeze
