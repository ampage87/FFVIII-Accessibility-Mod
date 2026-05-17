// battle_tts_ewm_state.inl — Enhanced Wait Mode module state (statics, typedefs, constants).
// Included from battle_tts_ewm.inl. Do not compile independently.
// v0.16.4: Extracted from battle_tts_ewm.inl for size compliance (90 KB → split).
//
// MUST be included FIRST in the ewm.inl chain — every other ewm sub-.inl
// references the statics declared here.

// ----------------------------------------------------------------------------
// GF state snapshot diagnostic (v0.10.65) — watches the GF state machine
// region to find what triggers the actual GF fire. See gf_patch.inl for the
// poll loop (GF_PollStateChanges).
// ----------------------------------------------------------------------------
static uint8_t s_gfStateSnap[128] = {};  // snapshot of 0x01D76860-0x01D768DF (state machine area)
static uint8_t s_gfStructSnap[128] = {}; // snapshot of 0x01D76960-0x01D769DF (GF struct area)
static bool s_gfSnapValid = false;
static DWORD s_gfSnapLastTick = 0;

// ----------------------------------------------------------------------------
// Battle effect dispatcher hook (v0.12.48) — detects GF animation fire by
// polling battle_magic_id. See gf_effect.inl for PollBattleMagicId.
// ----------------------------------------------------------------------------
static const uint32_t BATTLE_EFFECT_FUNC_ADDR = 0x50AF20;

typedef void (__cdecl *BattleEffectFn)(void);
static BattleEffectFn s_originalBattleEffect = nullptr;
static bool s_battleEffectHookInstalled = false;
static uint32_t s_battleMagicIdAddr = 0;  // resolved at hook install time

// v0.12.49: Poll-based detection instead of hook (hook crashed due to unknown calling convention).
// Polls battle_magic_id every frame. When it changes to a GF effect ID, set s_gfAnimFired.
static int s_prevBattleMagicId = -1;

// ----------------------------------------------------------------------------
// Hardware breakpoint diagnostic (v0.10.63/70) — DR0 hardware BP infrastructure
// for tracking down the GF fire dispatch. See bp_diag.inl for the VEH handler
// and BP arming.
// ----------------------------------------------------------------------------
static volatile bool s_gfBPArmed = false;       // true while hardware BP is active
static volatile bool s_gfBPWantArm = false;     // set by mod thread, armed by game thread hook
static volatile int  s_gfBPHitCount = 0;        // number of VEH captures so far
static const int     GF_BP_MAX_HITS = 50;       // v0.10.91: increased from 20 to catch fire dispatch amid timer noise
static PVOID         s_gfVEHHandle = nullptr;   // VEH registration handle (Add/Remove in battle_tts.cpp Initialize/Shutdown)
static bool          s_gfBPF12WasDown = false;  // edge detection
static DWORD s_accessibilityTID = 0;            // v0.10.91: our thread ID, skip in BP arming

// ----------------------------------------------------------------------------
// Target selection diagnostic (v0.10.96) — 2-snapshot diff over the battle
// menu state region. See bp_diag.inl for TgtDiag_TakeSnapshot.
// ----------------------------------------------------------------------------
static const uint32_t TGTDIAG_SCAN_BASE = 0x01D76800;
static const int TGTDIAG_SCAN_SIZE = 1024;  // 0x01D76800-0x01D76BFF

struct TargetDiagSnapshot {
    uint8_t region[1024];
    uint8_t menuPhase;      // 0x01D768D0
    uint8_t activeChar;     // battle_current_active_character_id
    uint8_t cmdCursor;      // 0x01D76843
    uint8_t subCursor;      // 0x01D76844 (known sub-menu cursor)
};

static TargetDiagSnapshot s_tgtDiagSnaps[2] = {};
static int s_tgtDiagStage = 0;  // 0=ready, 1=first snap, 2=diffed

// v0.11.01: F12 diagnostic gutted (was Draw diagnostic v0.10.107-112).
// F12 now handled in dinput8.cpp for world map diagnostics.
// Draw spell constants retained for runtime use.
static const uint32_t DRAW_SPELL_BASE = 0x1D28F18;   // Enemy 1 slot 0
static const int      DRAW_SLOTS_PER_ENEMY = 4;
static const int      DRAW_SLOT_SIZE = 4;             // bytes per slot
static const int      DRAW_ENEMY_STRIDE = 0x47;       // bytes between enemy 1 and enemy 2
static const int      DRAW_ENEMY_COUNT = 4;            // enemies 1-4 (slots 3-6)

// v0.10.91: Auto-arm READ BP state. See bp_diag.inl for GF_BP_AutoArm.
static uint8_t s_gfAutoArmLastActive = 0;  // previous value of 0x01D76971
static bool s_gfAutoArmDone = false;        // only auto-arm once per battle

// v0.10.63: Function entry scan flag. See bp_diag.inl for GF_ScanForFunctionEntry.
static bool s_gfFuncScanDone = false;

// ----------------------------------------------------------------------------
// EWM core state — config, toggle, freeze flag. See atb_hook.inl for the
// lifecycle/toggle functions and the ATB hook itself.
// ----------------------------------------------------------------------------
static bool s_ewmEnabled = true;          // Enhanced Wait Mode toggle
static bool s_ewmFreezing = false;        // currently requesting freeze
static bool s_ewmConfigLoaded = false;    // config file has been read
static bool s_ewmOKeyWasDown = false;     // edge detection for O key

// ----------------------------------------------------------------------------
// Dispatch-layer hooks (v0.13.55/56) — sub_483470 and sub_482F80. See
// dispatch.inl for the hook bodies and installers.
// ----------------------------------------------------------------------------
static const uint32_t PROCESS_READY_FUNC_ADDR = 0x00483470;
typedef void (__cdecl *ProcessReadyFn)(void);
static ProcessReadyFn s_originalProcessReady = nullptr;
static bool s_processReadyHookInstalled = false;
static volatile bool s_blockProcessReady = false;
static volatile LONG s_processReadyCalls = 0;
static volatile LONG s_processReadyBlocks = 0;
static volatile LONG s_processReadyPasses = 0;
static DWORD s_processReadyLogTick = 0;

static const uint32_t ACTION_EXECUTE_FUNC_ADDR = 0x00482F80;
typedef void (__cdecl *ActionExecuteFn)(void);
static ActionExecuteFn s_originalActionExecute = nullptr;
static bool s_actionExecuteHookInstalled = false;
static volatile LONG s_actionExecuteCalls  = 0;
static volatile LONG s_actionExecuteBlocks = 0;
static volatile LONG s_actionExecutePasses = 0;

// ----------------------------------------------------------------------------
// FFNx GF loading counter hook (v0.10.77). See ffnx.inl for module scan
// and hook install. (s_ffnxGFHookInstalled and s_ffnxHookCallCount are
// declared earlier in battle_tts.cpp's main statics block.)
// ----------------------------------------------------------------------------
typedef void (__cdecl *FFNxBattleUpdateFn)(void);
static FFNxBattleUpdateFn s_originalFFNxBattleUpdate = nullptr;
static uint32_t s_ffnxGFFuncAddr = 0;  // resolved address of FFNx function

// ----------------------------------------------------------------------------
// EWM_UpdateBattle state (turn edge tracking, grace periods, signal tracking).
// See update.inl for the state machine that consumes these.
// ----------------------------------------------------------------------------
static uint8_t s_ewmLastActiveChar = 0xFF;  // track active_char_id changes for turn edge
static bool s_ewmNewTurnGrace = false;       // v0.10.41: suppress phase-based release until non-executing phase seen

// v0.13.53: Post-turn grace period (1s after a player action ends).
static const DWORD EWM_POST_TURN_GRACE_MS = 1000;
static uint8_t s_ewmPrevSeenActiveChar = 0xFF;
static DWORD s_ewmPostTurnGraceEnd = 0;

// v0.13.54: Post-action cooldown (500ms after the last damage/action signal cleared).
static const DWORD EWM_POST_ACTION_COOLDOWN_MS = 500;
static DWORD s_ewmLastSignalTime = 0;

// ----------------------------------------------------------------------------
// Diagnostic state (v0.13.57-60). See diag.inl for the poll functions that
// consume these.
// ----------------------------------------------------------------------------
// v0.13.57: Damage-anim transition diagnostic.
static uint8_t s_diagPrevDamageAnim = 0;
static uint32_t s_diagPrevActionInProgress = 0;
static bool s_diagPrevShouldCap = false;
static DWORD s_diagFreezeReleaseTime = 0;
static int s_diagPostReleaseLogsRemaining = 0;

// v0.13.58: Per-slot turn counter for EWM-on vs EWM-off ratio comparison.
static uint32_t s_prevSlotATB[BATTLE_TOTAL_SLOTS] = {};
static int      s_slotTurnCount[BATTLE_TOTAL_SLOTS] = {};
static bool     s_slotATBInit = false;
static bool     s_turnCountPrevInBattle = false;

// GF loading diagnostic (v0.10.57): ATB-log throttle/cap.
static DWORD s_ewmDiagLastTick = 0;
static int s_ewmDiagCount = 0;
static const int EWM_DIAG_MAX = 40;  // max samples per cap session
