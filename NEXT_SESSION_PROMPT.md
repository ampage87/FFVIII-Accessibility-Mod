# Next Session Priority: Wall-stuck recovery + trigger-line crossing

## Current State (v06.09)

Build is stable and deployed. NavLog is fully wired and collecting structured data.
Logs are in `Logs/` subfolder (ff8_accessibility.log, ff8_nav_data.log, build_latest.log).

### What's working well
- Quistis (bgroom_1) reached reliably
- 2/3 Trepie Groupies (bgroom_1) reached
- Screen transitions: multiple successful Arrived drives (bggate_1, bggate_5)
- Overshoot detection fires correctly (v06.08)
- Stuck detection grace period prevents false early cancels (v06.09)
- Arrow key cancel during recovery (phase ≥4) lets player regain control (v06.09)
- MAX_RECOVERY_PHASES=12 auto-cancels with "Stuck. Distance remaining: N." message
- NavLog collecting rich structured data across all fields

### Known issues (prioritized)

**1. Wire NavLog::CoordSample into HookedSetCurrentTriangle (HIGH PRIORITY — small change, big unlock)**
The 3D→2D coordinate transform for angled fields (bg2f_1 etc.) is unknown and blocks
pathfinding on those fields. CoordSample logs both the raw 3D walkmesh vertex data
(from the hook's int16_t[3] arguments) AND the entity's 2D position (from offset
0x190/0x194) every time the player moves to a new triangle. A few minutes of walking
around bg2f_1 will produce dozens of paired 3D↔2D data points — enough to solve
the projection matrix numerically. Implementation: ~20 lines in HookedSetCurrentTriangle,
only log when the changed entity is the player (changedIdx == s_playerEntityIdx).
Log format: `COORD\tfield\ttriId\t3dX\t3dY\t3dZ\t2dX\t2dY`
The 3D coords are the triangle center from the vertex args; the 2D coords are from
the entity struct at 0x190/0x194 (divided by 4096). Once we have the data, we can
solve for the per-field affine transform in Python offline.

**2. Micro-oscillation fools stuck detector (bggate_2 NPC — HIGH PRIORITY)**
Player bounces between tri 126↔127 at Y≈728↔759, moving ~31 units each cycle.
This is enough to reset stuck detection (DRIVE_STUCK_MIN_DIST=20), so recovery
phase never exceeds 1 and MAX_RECOVERY_PHASES never fires. Player oscillates
for the full 2400-tick timeout then "Gave up." at dist=5044. Fix needed: track
whether the player is making progress toward the TARGET, not just moving at all.
Potential approach: compare dist-to-target at each stuck window. If dist hasn't
decreased meaningfully over N windows, declare stuck even if position changed.

**3. Trigger-line crossing during drives (bghall_1, bggate_6 — MEDIUM PRIORITY)**
Some drives accidentally cross trigger lines and cause field transitions.
NavLog shows "unknown" end reason (field changed mid-drive). The A* avoids
trigger lines but the funnel path can cut close to them, and analog steering
momentum carries the player through. Fix needed: check player position against
trigger lines each tick during drive, and either stop or redirect if about to cross.

**4. bg2f_1 corridor NPC consistently unreachable (BLOCKED — needs CoordSample data from issue #1)**
Entity at (323,-3651) but walkmesh coordinates are in 3D space (Z=484-10413).
The 3D→2D transform is hardcoded in FF8_EN.exe and unknown. Walkmesh path leads
to a wall because coordinates don't align on this angled field. NavLog CoordSample
(not yet wired) will collect empirical data to solve this. Can't fix pathfinding
until we know the coordinate transform.

**5. Wall-stuck on multiple fields (bggate_1 tri 315, bghall_1)**
Player gets stuck against invisible collision geometry. Micro-nudge direction
doesn't help (moveDist=0). The current recovery alternates edge-midpoint re-path
and micro-nudge, but neither breaks free. May need more aggressive random-walk
recovery after repeated failures.

## v06.09 Session Test Data Summary

Fields tested: bgroom_1, bg2f_1, bghall_1, bghall_4, bggate_1, bggate_2, bggate_4, bggate_5, bggate_6

| Result | Count | Examples |
|--------|-------|---------|
| Arrived | 3+ | bggate_5 cdfield8, bggate_1 screen transitions |
| Gave up (2400 ticks) | 3 | bg2f_1 NPC (×2), bggate_2 NPC |
| Cancelled (phase≥4) | 5+ | bggate_1 NPC, bghall_1 exits, bggate_5 ddtower3 |
| Field transition (unknown) | 3 | bghall_1→bghall_4, bggate_6→bghall_1 |

## Implementation Suggestions

### For issue #1 (CoordSample wiring):
In `HookedSetCurrentTriangle`, after the `changeCount == 1` block that stores
the triangle center, add a check: if `changedIdx == s_playerEntityIdx`, read
the entity's 2D position from offset 0x190/0x194 (divided by 4096) and call
`NavLog::CoordSample(fieldName, triId, cx3d, cy3d, cz3d, entity2dX, entity2dY)`.
The 3D center is computed from the raw vertex args (x0+x1+x2)/3 for all three
axes. This gives us paired 3D↔2D data every time the player changes triangles.
After collecting data from bg2f_1, write a Python script to solve for the
affine transform: `[2dX, 2dY] = M * [3dX, 3dY, 3dZ] + offset`.

### For issue #2 (micro-oscillation):
Add a "progress toward target" check alongside the existing "moved at all" check.
Track `s_driveProgressDist` = distance to target at the start of each stuck window.
If the current dist is not significantly less than `s_driveProgressDist` after N
consecutive stuck windows, trigger recovery even though the player "moved."

### For issue #3 (trigger-line crossing):
Add a per-tick check in UpdateAutoDrive: if player position has crossed any
non-target trigger line since last tick, stop the drive immediately with
"Screen changed." or redirect.

### For issue #5 (wall-stuck):
After MAX_RECOVERY_PHASES, instead of just stopping, try one last "toward target"
burst — steer directly at the target for 30 ticks, ignoring waypoints. If that
doesn't close distance, then stop.

## Key Code Locations

- `UpdateAutoDrive()` — main drive loop, waypoint advancement, stuck detection
- `StopAutoDrive()` — drive cleanup
- Drive start logic — in `HandleKeys()` under the `drive && !s_driveWasDown` block
- Stuck detection — search for `DRIVE_STUCK_THRESH` and `s_driveStuckTicks`
- Recovery phases — search for `s_driveWigglePhase`
- NavLog calls — search for `NavLog::` in field_navigation.cpp
- The file is ~3500+ lines; use targeted reads with head/tail

## Recovery Instructions
1. Read DEVNOTES.md for full history
2. Read this file for immediate context
3. Use filesystem MCP tools for Windows files (not bash)
4. `deploy.bat` is the ONLY build script; `deploy.vbs`/`deploy.ps1` is the UI wrapper
5. Bump `FF8OPC_VERSION` in `ff8_accessibility.h` + two locations in `field_navigation.cpp` for every build
6. When Aaron says "BAT" → read tail of `Logs/ff8_accessibility.log` immediately
7. Nav data log: `Logs/ff8_nav_data.log` (append-mode, persistent across sessions)
8. Build log: `Logs/build_latest.log` (written by deploy.ps1)
