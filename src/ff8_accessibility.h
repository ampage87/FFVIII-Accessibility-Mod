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
#define FF8OPC_VERSION "0.18.3.52"  // v0.18.3.52: World-map Balamb-nav regression guard (#67 prep, LOCAL). Pure extraction of the world-map coordinate/segment/BFS math (WorldXToSegCol/Row, SegmentCenterToWorld, TorusBearing, GetVehicleType, GetBfsRuleClass, IsCanonicalLocomotion, IsSegmentTraversable, CalculateWrappedDistance, ComputeReachability) out of world_map_segments.inl / world_map_catalog.inl into a new Win32/SEH-free src/world_map_geometry.inl (included right after state.inl), so a host g++ can compile it. New CI job world-map-harness compiles that REAL geometry against a committed wmx.obj terrain-grid snapshot (tests/world_map_terrain_grid.txt -> gen_world_map_fixture.py -> world_map_fixtures.h) and hard-asserts the Balamb continent on-foot reachable set never regresses: Balamb Garden / Balamb Town / Fire Cavern reachable, Dollet (Galbadian continent) excluded, exactly those three catalog locations reachable on foot. Container-verified PASS + negative-control FAIL. NO runtime behavior change (pure move + new test). BAT: build to confirm the extraction still links; world-map nav should behave identically.
