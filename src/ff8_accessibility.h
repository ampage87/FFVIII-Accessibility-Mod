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
#define FF8OPC_VERSION "0.18.3.48"  // v0.18.3.48: #66 forced party-select — also announce the action-bar option when focus moves UP onto the bar (LOCAL). Symmetric to the .47 bar->character announce, same +0x1B6 focus byte. PollForcedPartySelect now computes movedToBar (onBar && prev focus==2) alongside returnedFromBar; on movedToBar it speaks the current option name only ("Switch Member." / "Junction Exchange.", mirroring the toggle), so going up to the action bar is no longer silent until you flip. .47 confirmed working by ear (the focused character announces on landing; the toggle says option-name-only). The +0x1B6 read is thereby validated. FORCED_PSEL_DIAG still 1 for this BAT. Next BAT: win battle -> from a character go Up to the bar (confirm it announces the current option), flip Switch<->Junction (option name), come Down onto a character (character announces). If clean: reserve id->name fully off +0x1ED, FORCED_PSEL_DIAG 0, push.
