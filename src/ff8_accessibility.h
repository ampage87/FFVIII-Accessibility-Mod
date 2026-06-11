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
#define FF8OPC_VERSION "0.18.3.21"  // v0.18.3.21: #58 cleanup -- turned OFF the guard discovery diagnostics now that Manual (v0.18.3.14) and Original (v0.18.3.20) are both BAT-confirmed. GUARD_RECON_DIAG=0 ([GUARDPOS], field_nav_observe.inl) and GUARD_VAR_DIAG=0 ([GUARDVAR] + the [GUARDFREEZE] confirmation, field_dialog_lifecycle.inl); both retained behind their flags for one-line re-enable. The Manual var-pin and the [GUARDCUE] feature cue are unchanged. No behavior change -- shippable build for Manual+Original
