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
#include <climits>     // v0.15.9.9: INT_MAX for the Auto-mode verification cap
#include <cstdio>
#include <cstring>
#include <intrin.h>    // v0.16.1.1: _ReturnAddress for engine caller capture

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
    // v0.16.1.1: capture the engine return address FIRST. _ReturnAddress
    // is a compiler intrinsic that resolves to the address the engine's
    // dispatcher will return to -- i.e. the instruction immediately after
    // the call site that invoked opcode_battle (0x69). Logged in the [CBF]
    // PASS line below to identify which engine dispatch path is firing the
    // BATTLE on doopen2a. Doing this at the very top of the function
    // before any other code keeps the captured address clean (no inlining
    // games shuffle it). Currently we only use this for diagnostic logging;
    // there is no behavior change tied to the value.
    void* retAddr = _ReturnAddress();

    LONG total = InterlockedIncrement(&s_callCount);

    bool inChase = ChaseDetector::IsInChaseField();

    if (inChase) {
        ChaseDetector::Mode mode = ChaseDetector::GetChaseMode();

        // v0.15.9.10: MODE_ORIGINAL short-circuit. The third ASK option lets
        // users opt in to the vanilla, unmodified chase scene -- no auto-pilot
        // engagement, no battle cap, no kani/battleyarou pin, no chase-agent
        // registration. We bail BEFORE incrementing s_chaseCallCount, BEFORE
        // logging [CBF] PASS/NO-OP, and BEFORE registering the chase agent so
        // chase_kani_freeze's per-frame pin stays inert too (chase_kani_freeze
        // also has its own MODE_ORIGINAL short-circuit at the top of StartCapture
        // and the pin tick, in case some other code path arms it). Result:
        // vanilla FF8 chase plays out exactly as Square shipped it -- battles
        // fire, robot pursues, ground shakes on the west trail, etc. The
        // mod's screen-reader assistance (field TTS, etc.) is unaffected.
        //
        // s_callCount has already been incremented above; that's intentional --
        // it's a session-wide opcode_battle call counter useful for diagnostics
        // regardless of mode. The chase-specific counters stay untouched.
        if (mode == ChaseDetector::MODE_ORIGINAL) {
            return s_origBattle(entityPtr);
        }

        LONG chaseN = InterlockedIncrement(&s_chaseCallCount);
        int  battleCount = ChaseDetector::GetCurrentFieldBattleCount();
        bool kaniCaller        = ChaseDetector::IsKaniEntityPtr((uintptr_t)entityPtr);
        bool battleyarouCaller = ChaseDetector::IsBattleyarouEntityPtr((uintptr_t)entityPtr);

        const char* whoTag = "other";
        if (kaniCaller)             whoTag = "kani";
        else if (battleyarouCaller) whoTag = "battleyarou";

        // v0.15.9: cap chase battles per mode.
        //   MANUAL: cap=1. First scripted chase battle per field passes
        //     through (the opening encounter); subsequent NO-OP'd.
        //   AUTO:   cap=0. ALL chase battles NO-OP'd. Even the scripted
        //     opener is suppressed since Auto mode is skipping the
        //     entire chase scene. The chase progresses as a movement-
        //     only scenario (chase_auto_pilot drives, robot scripts
        //     play visually but never reach a battle screen).
        // Cap=0 also acts as the safety net for the AI-rule fields:
        // if chase_auto_pilot's W-press hasn't engaged in time on the
        // west trail (race window at field entry), the ground-shake
        // battle that would fire from running gets NO-OP'd anyway.
        //
        // v0.15.9.9: AUTO-mode cap RAISED to INT_MAX for verification.
        // The v0.15.9.8.3 BAT showed 0 [CBF] NO-OPs on the whole chase --
        // but that doesn't tell us whether the underlying chase battle
        // calls were 0 (chase_auto_pilot doing all the work) or N>0
        // (suppressor doing the work as a band-aid). To find out, raise
        // the AUTO cap so any battle the auto-pilot fails to avoid will
        // PASS through to a real battle screen. Expected v0.15.9.9 BAT
        // outcome: 0 [CBF] PASS lines on chase fields AND 0 NO-OPs --
        // proves the suppressor is provably vestigial in AUTO mode and
        // can be removed entirely in v0.15.10. If any [CBF] PASS line
        // appears, the chase battle is a real regression we need to fix
        // in chase_auto_pilot before the suppressor can be retired;
        // revert this constant to 0 to restore the band-aid until the
        // regression is addressed. MANUAL cap stays at 1 (the scripted
        // first-battle-per-field pass-through preserves vanilla MANUAL
        // behavior).
        int  cap    = (mode == ChaseDetector::MODE_AUTO) ? INT_MAX : 1;
        bool freeze = (battleCount >= cap);

        if (freeze) {
            LONG freezeN = InterlockedIncrement(&s_freezeCount);
            Log::Field("[CBF] NO-OP chase BATTLE call #%ld (total #%ld, "
                       "freeze#%ld) field='%s' mode=%s battleCount=%d cap=%d "
                       "caller=%s entityPtr=0x%08X -- returning %d",
                       (long)chaseN, (long)total, (long)freezeN,
                       ChaseDetector::GetDebouncedFieldName(),
                       ChaseDetector::ChaseModeName(mode),
                       battleCount, cap, whoTag, (uint32_t)(uintptr_t)entityPtr,
                       JSM_RC_ADVANCE);
            return JSM_RC_ADVANCE;
        }

        // PASS: first chase battle in field. In MANUAL mode (cap=1)
        // this is the scripted opening encounter; subsequent battles
        // on the same field hit the NO-OP branch above. In AUTO mode
        // (cap=INT_MAX per v0.15.9.9 verification build) this branch
        // fires for ANY chase battle the auto-pilot fails to avoid --
        // and emits the [CBF] PASS log line we're looking for as proof
        // of v0.15.9.8.3 catch elimination. v0.15.9.9 expected: 0 PASS
        // lines on chase fields (auto-pilot self-sufficient). If a PASS
        // line fires, that's a real chase battle the auto-pilot let
        // through; the log identifies the field/timing for diagnosis.
        // Pre-v0.15.9.9 AUTO mode had cap=0 (this branch was unreachable
        // in AUTO); the band-aid is on hold for one BAT cycle so we can
        // verify whether the auto-pilot is sufficient on its own.
        //
        // Log it AND (in most fields) register the calling entity as
        // the field's chase agent so chase_kani_freeze can pin it
        // post-battle. RegisterChaseAgent is idempotent, so subsequent
        // PASS events with the same entityPtr in the same field are
        // no-ops.
        const char* fieldName = ChaseDetector::GetDebouncedFieldName();
        Log::Field("[CBF] PASS chase BATTLE call #%ld (total #%ld) "
                   "field='%s' mode=%s battleCount=%d cap=%d "
                   "caller=%s entityPtr=0x%08X retAddr=0x%08X",
                   (long)chaseN, (long)total,
                   fieldName,
                   ChaseDetector::ChaseModeName(mode),
                   battleCount, cap, whoTag, (uint32_t)(uintptr_t)entityPtr,
                   (uint32_t)(uintptr_t)retAddr);

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

    Log::Mod("ChaseBattleFreeze: Initialized v0.15.9.10 (opcode_battle hooked at "
             "0x%08X, trampoline=0x%08X). Gates: cap=1 in MANUAL (PASS at "
             "battleCount==0, NO-OP at >=1), cap=INT_MAX in AUTO "
             "(VERIFICATION BUILD -- chase battles PASS through to real "
             "battle screen; v0.15.9.8.3 catch elimination proven by "
             "v0.15.9.9 BAT showing 0 PASS lines on chase fields), "
             "MODE_ORIGINAL short-circuits before any chase logic so "
             "vanilla chase plays out unmodified; skip register-agent "
             "in doopen2a only; all NO-OPs return %d. "
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

// v0.15.9: Barometer accessors. ChaseDetector reads these at chase
// activation (snapshot) and deactivation (delta) to log the per-chase
// CHASE-END SUMMARY (battles_fired vs battles_suppressed).
int GetChaseBattleCallCount()   { return (int)s_chaseCallCount; }
int GetChaseBattleFreezeCount() { return (int)s_freezeCount; }

}  // namespace ChaseBattleFreeze
