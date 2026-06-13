// ff8_accessibility.h - Core header for FF8 Original PC Accessibility Mod
//
// Lean post-v0.15.12.0 cleanup. The inline-changelog chain that had
// accreted on the macro line (421 KB by v0.15.11.0) was moved to
// `ff8_accessibility_history.h`, which is NOT included by the build.
// Going forward this header holds only the version macro and the
// system includes other modules rely on it to transitively provide.
//
// The CANONICAL changelog lives in `CHANGELOG.md` at the project root.
// Older entries (pre-v0.15.12.0) are preserved in `CHANGELOG_HISTORY.md`.
//
// FF8 runtime address resolution: see `ff8_addresses.h` /
// `ff8_addresses.cpp` for the resolver that computes addresses at
// runtime using the same offset-chain technique as FFNx.

#pragma once

#include <windows.h>
#include <cstdint>
#include <string>

// ================================================================
// FF8 Original PC Accessibility Mod version
// Increment on every build change. Must match the top `## vX.Y.Z`
// heading in CHANGELOG.md or `Utilities/push_to_github.ps1` will
// refuse to push.
// ================================================================
#define FF8OPC_VERSION "0.18.3.32"  // v0.18.3.32: #62 + #63 -- gate off the remaining battle diagnostics from the concluded damage-number investigation. #62: VictoryAutoCapture (the per-victory Logs/victory_auto_*.bmp/.png) now sits behind BATTLE_DIAG_SCREENSHOTS. #63: new BATTLE_DIAG_LOGGING master flag (battle_tts.h, default 0) + DiagLogBattle no-op macro silence the per-frame/per-event battle-log flood -- [POPUP-TIME-DIAG] and [SPRITE-POOL-DIAG] (whole diagnostic bodies gated, also covered by BATTLE_DIAG_SCREENSHOTS so the capture path still works if re-enabled), plus [SPRITE-POLL], [SPRITE-ALLOC-V99], [DMG-RENDER], [DMG-POPUP-CREATE] and their STATS lines. Hooks/publishers that feed live features (sub_5068B0 render-tick publish, TriggerImmediateHPFlush, ScanTTS::OnScanPopupDespawn, the DMG InterlockedExchange publishes) are untouched. Gate, don't delete.
