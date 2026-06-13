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
#define FF8OPC_VERSION "0.18.3.37"  // v0.18.3.37: #65 Switch submenu — level + HP in the announcements (LOCAL; BAT-confirmed working, tested under the .36 string since the code landed before this bump). menu_tts_switch.inl now reads LV/HP by char-id (new POD+SEH helper SwitchCharLevelHP, reusing AnnounceJuncCharSelect's proven path: level via ComputeCharLevel from EXP at savemap+0x48C+id*0x98+0x04; HP from the computed-stats slot 0x1CFF000 when the char is in the live formation +0xAF0, else the menu HP array +0x71E+id*0x20 which covers benched). The candidate's char-id comes free from the GCW name scan (CHAR_NAMES is char-id indexed). Member-list announces are now "Name, active/reserve, Level N, HP X of Y." (entry + on-move), with a graceful "Name, active/reserve." fallback if the read fails. The main-menu Switch submenu (#65) is now feature-complete: options, active trio, candidate w/ LV+HP, swap result, both Switch Member and Junction Exchange. Next: close #65; then the forced party/junction-switch story screens (the follow-up).
