// chase_battle_freeze.cpp — chase BATTLE opcode safety net + agent identifier.
// See chase_battle_freeze.h for the v0.15.2.14/v0.15.2.15 design rationale.
//
// HOOK CONTRACT (v0.15.2.15):
//   - When ChaseDetector::IsInChaseField() is false: pass through, silent.
//   - When IsInChaseField() and GetCurrentFieldBattleCount() == 0:
//       * pass through (the scripted opening encounter fires)
//       * register the calling entity with chase_kani_freeze as the
//         field's chase agent so the post-battle pin can target it
//         -- EXCEPT in doopen2a (v0.15.2.15), where the agent doubles
//         as the chase-progress director and pinning it blocks the
//         transition to dotown_3. Only the BATTLE NO-OP runs there.
//       * log [CBF] PASS
//   - When IsInChaseField() and GetCurrentFieldBattleCount() >= 1:
//       * return JSM_RC_ADVANCE (= 3) without invoking s_origBattle
//       * log [CBF] NO-OP, increment freeze#
//
// The NO-OP is the safety net for cases where the agent pin in
// chase_kani_freeze fails to engage (bad pointer, JSMCounts unavailable,
// agent in an unsupported array). When the pin works correctly, the
// agent stays on the ground and never reaches the BATTLE opcode again,
// so freeze# stays low. A high freeze# count means the pin is missing
// the agent and the safety net is doing all the work.

#include "chase_battle_freeze.h"
#include "chase_detector.h"
#include "chase_kani_freeze.h"
#include "ff8_accessibility.h"
#include "ff8_addresses.h"
#include "mod_forward_decls.h"
#include "minhook/include/MinHook.h"

#include <windows.h>
#include <cstdio>
#include <cstring>

namespace ChaseBattleFreeze {

static const int JSM_RC_ADVANCE = 3;
static const int OPCODE_BATTLE_INDEX = 0x69;

typedef int (__cdecl *OpcodeHandler_t)(int);

// ============================================================================
// State
// ============================================================================

static bool             s_initialized    = false;
static OpcodeHandler_t  s_origBattle     = nullptr;
static uint32_t         s_battleAddr     = 0;
static volatile LONG    s_callCount      = 0;
static volatile LONG    s_chaseCallCount = 0;
static volatile LONG    s_freezeCount    = 0;

// ============================================================================
// Hook
// ============================================================================

static int __cdecl Hook_opcode_battle(int entityPtr)
{
    LONG total = InterlockedIncrement(&s_callCount);

    bool inChase = ChaseDetector::IsInChaseField();

    if (inChase) {
        LONG chaseN = InterlockedIncrement(&s_chaseCallCount);
        int  battleCount = ChaseDetector::GetCurrentFieldBattleCount();
        bool kaniCaller        = ChaseDetector::IsKaniEntityPtr((uintptr_t)entityPtr);
        bool battleyarouCaller = ChaseDetector::IsBattleyarouEntityPtr((uintptr_t)entityPtr);
        ChaseDetector::Mode mode = ChaseDetector::GetChaseMode();

        const char* whoTag = "other";
        if (kaniCaller)             whoTag = "kani";
        else if (battleyarouCaller) whoTag = "battleyarou";

        bool freeze = (mode == ChaseDetector::MODE_MANUAL && battleCount >= 1);

        if (freeze) {
            LONG freezeN = InterlockedIncrement(&s_freezeCount);
            Log::Field("[CBF] NO-OP chase BATTLE call #%ld (total #%ld, "
                       "freeze#%ld) field='%s' mode=%s battleCount=%d "
                       "caller=%s entityPtr=0x%08X -- returning %d",
                       (long)chaseN, (long)total, (long)freezeN,
                       ChaseDetector::GetDebouncedFieldName(),
                       ChaseDetector::ChaseModeName(mode),
                       battleCount, whoTag, (uint32_t)(uintptr_t)entityPtr,
                       JSM_RC_ADVANCE);
            return JSM_RC_ADVANCE;
        }

        // PASS: first chase battle in field (or auto mode). Log it AND
        // (in most fields) register the calling entity as the field's
        // chase agent so chase_kani_freeze can pin it post-battle.
        // RegisterChaseAgent is idempotent, so subsequent PASS events
        // with the same entityPtr in the same field are no-ops.
        const char* fieldName = ChaseDetector::GetDebouncedFieldName();
        Log::Field("[CBF] PASS chase BATTLE call #%ld (total #%ld) "
                   "field='%s' mode=%s battleCount=%d "
                   "caller=%s entityPtr=0x%08X",
                   (long)chaseN, (long)total,
                   fieldName,
                   ChaseDetector::ChaseModeName(mode),
                   battleCount, whoTag, (uint32_t)(uintptr_t)entityPtr);

        // v0.15.2.15: skip the dynamic pin in doopen2a only. The v0.15.2.14
        // BAT showed that the BATTLE caller in doopen2a (Others slot 4,
        // 'director0', entityPtr=0x0188CA04) doubles as the chase-progress
        // director -- pinning its full state for the rest of the field
        // session blocks the chase-end script from triggering the
        // transition to dotown_3. doopen2a only has one chase battle in
        // the whole sequence, so the BATTLE NO-OP fallback (cap at 1) is
        // sufficient there. Other chase fields (domt1_1..domt5_1) don't
        // share the "agent doubles as director" pattern and worked
        // perfectly with the pin in v0.15.2.14.
        //
        // The static kani+battleyarou pin in chase_kani_freeze still runs
        // in doopen2a -- those entities are dormant there per OTHERS-DIAG
        // (kani: 7 changed bytes, battleyarou: 0), so the pin is inert.
        // Only the dynamic agent pin is skipped.
        if (fieldName != nullptr && std::strcmp(fieldName, "doopen2a") == 0) {
            Log::Field("[CBF] PASS in doopen2a -- skipping RegisterChaseAgent "
                       "(agent is chase-progress director; pin would block "
                       "transition to dotown_3). BATTLE NO-OP carries the load.");
        } else {
            // v0.15.2.14: hand the entity pointer to chase_kani_freeze. It
            // resolves array/slot/symIdx, logs [CHASE-AGENT] with full
            // identity, and arms the per-frame pin that activates on the
            // next StartCapture (post-battle game-mode 3 -> 1 transition).
            ChaseKaniFreeze::RegisterChaseAgent((uintptr_t)entityPtr);
        }
    }

    return s_origBattle(entityPtr);
}

// ============================================================================
// Public API
// ============================================================================

void Initialize()
{
    if (s_initialized) return;

    if (!FF8Addresses::pExecuteOpcodeTable) {
        Log::Mod("ChaseBattleFreeze: pExecuteOpcodeTable not resolved; skipping");
        return;
    }

    __try {
        s_battleAddr = FF8Addresses::pExecuteOpcodeTable[OPCODE_BATTLE_INDEX];
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        s_battleAddr = 0;
    }
    if (s_battleAddr == 0) {
        Log::Mod("ChaseBattleFreeze: opcode_battle (0x%02X) address read failed; "
                 "skipping", OPCODE_BATTLE_INDEX);
        return;
    }

    MH_STATUS st = MH_CreateHook(
        (LPVOID)s_battleAddr,
        (LPVOID)&Hook_opcode_battle,
        (LPVOID*)&s_origBattle);
    if (st != MH_OK) {
        Log::Mod("ChaseBattleFreeze: MH_CreateHook failed for opcode_battle "
                 "at 0x%08X (status=%d)", s_battleAddr, (int)st);
        return;
    }

    Log::Mod("ChaseBattleFreeze: Initialized v0.15.2.15 (opcode_battle hooked at "
             "0x%08X, trampoline=0x%08X). Gates: PASS+register-agent on "
             "battleCount==0 (skip register-agent in doopen2a only); "
             "NO-OP on battleCount>=1 (return %d). "
             "Hook activated by global MH_EnableHook(ALL).",
             s_battleAddr, (uint32_t)(uintptr_t)s_origBattle, JSM_RC_ADVANCE);
    s_initialized = true;
}

void Shutdown()
{
    if (!s_initialized) return;
    if (s_battleAddr != 0) {
        MH_DisableHook((LPVOID)s_battleAddr);
        MH_RemoveHook((LPVOID)s_battleAddr);
    }
    Log::Mod("ChaseBattleFreeze: Shutdown. Total opcode_battle calls: %ld "
             "(chase-field: %ld, NO-OP'd: %ld). Healthy run: NO-OP count "
             "should be low (pin keeping agent down).",
             (long)s_callCount, (long)s_chaseCallCount, (long)s_freezeCount);
    s_origBattle = nullptr;
    s_battleAddr = 0;
    s_initialized = false;
}

}  // namespace ChaseBattleFreeze
