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
#define FF8OPC_VERSION "0.18.3.51"  // v0.18.3.51: Switch-screen cleanup for release (LOCAL). Both screens (main-menu Switch + forced party-select) are BAT-confirmed, so the discovery/confirmation scaffolding is retired: FORCED_PSEL_DIAG -> 0 (kept gated, per gate-don't-delete); the obsolete #65 SWITCH_DISCOVERY_DIAG band-monitor (PollSwitchDiscoveryDiag/ResetSwitchDiscoveryDiag + its dispatch branch in menu_tts.cpp) removed outright — the screen is fully mapped AND unified, and FORCED_PSEL_DIAG is the superior general probe if ever needed. menu_tts_switch.inl header comment refreshed to describe the unified +0x78 engine (the old #65 GCW code s_sw*/old PollSwitchSubmenu/SwitchCandidatePhrase was already removed in v0.18.3.49). NO runtime TTS behavior change — diagnostics + dead code only. BAT: open main menu -> Switch to confirm everything still announces (and the logs are quiet of [SwitchMenu]/[ForcedPSel]/[SwitchDiag]). Then push (carries .49 unify + .50 delay fix + .51 cleanup).
