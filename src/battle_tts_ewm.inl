// battle_tts_ewm.inl — Enhanced Wait Mode, GF fire prevention, ATB hook, FFNx hook.
// Included from battle_tts.cpp inside `namespace BattleTTS`. Do not compile independently.
// v0.12.18: Extracted from battle_tts.cpp for readability.
// v0.16.4: Mechanically split into 9 sub-.inl files for size compliance (was 91 KB).
//
// The chain below is dependency-ordered:
//   state    → all module statics, typedefs, constants (MUST be first)
//   gf_patch → GF fire prevention layer (HookedGFTimerUpdate, EWM_ClampGFState,
//              EWM_RestoreGFPatch, GF_LogHookStats, GF_PollStateChanges,
//              EWM_InstallGFHook)
//   gf_effect→ battle_magic_id polling (PollBattleMagicId, IsGFEffectId,
//              GFEffectIdToIndex, FindPartySlotForGF, EWM_InstallBattleEffectHook)
//   bp_diag  → hardware BP / target diagnostic / function entry scan
//              (GF_BP_VectoredHandler, GF_BP_ArmAllThreads, GF_BP_AutoArm,
//              TgtDiag_TakeSnapshot, GF_BP_PollKey, GF_ScanForFunctionEntry)
//   atb_hook → HookedATBUpdate + EWM lifecycle (EWM_LoadConfig, EWM_SaveConfig,
//              EWM_PollToggle, EWM_InstallHook)
//   dispatch → sub_483470 + sub_482F80 hooks (HookedProcessReady,
//              HookedActionExecute, installers, EWM_LogDispatchStats)
//   ffnx     → FFNx GF loading counter hook (HookedFFNxBattleUpdate,
//              FindFFNxModuleBase, ScanModuleForSignature,
//              ScanAllModulesForSignature, FindFunctionEntry,
//              EWM_InstallFFNxGFHook)
//   diag     → diagnostic helpers (EWM_IsExecutingPhase, EWM_FormatATBSnapshot,
//              EWM_PollDiagnostics, EWM_ResetTurnCount, EWM_LogTurnCountSummary,
//              EWM_TrackTurnCount, EWM_DiagLogATB)
//   update   → EWM_UpdateBattle (the per-frame state machine — calls helpers
//              from gf_patch.inl and diag.inl, MUST be last)
#include "battle_tts_ewm_state.inl"
#include "battle_tts_ewm_gf_patch.inl"
#include "battle_tts_ewm_gf_effect.inl"
#include "battle_tts_ewm_bp_diag.inl"
#include "battle_tts_ewm_atb_hook.inl"
#include "battle_tts_ewm_dispatch.inl"
#include "battle_tts_ewm_ffnx.inl"
#include "battle_tts_ewm_diag.inl"
#include "battle_tts_ewm_update.inl"
