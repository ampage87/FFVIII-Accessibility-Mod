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
#define FF8OPC_VERSION "0.18.3.33"  // v0.18.3.33: #64 -- gate off the Scan-UI diagnostic screenshot. HookedScanGetText (sub_B687C0 hook, scan_tts.cpp) was writing a timestamped Logs/screenshots/scan_*.bmp/.png pair on every Scan cast -- a v0.14.65.1 developer aid for validating the in-memory stat/element/status reads against the rendered UI, now concluded. New master flag SCAN_DIAG_SCREENSHOTS (scan_tts.h, default 0) gates the capture block; the OnScanPopupSpawn announce and all s_scanCache snapshot reads (the actual TTS source) stay outside the gate. Kept separate from BATTLE_DIAG_SCREENSHOTS so the two capture families toggle independently. Gate, don't delete.
