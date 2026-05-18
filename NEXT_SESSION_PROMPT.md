# Next Session Prompt: v0.17.5.4 LOCAL, awaiting BAT

## Greeting

Start with `## Claude Says`. Read `DEVNOTES.md` and THIS file before any work.

## Where we are

**GitHub HEAD = v0.17.5.2** (commit `6dc080a`, pushed 2026-05-17). **Local tree = v0.17.5.4**, awaiting BAT. v0.17.5.3 was BAT'd but its diagnostics are stacked into v0.17.5.4.

Recent history (most recent on top):
- **v0.17.5.4 = WorldMap polling stuck-at-startup fix** (the topic of this BAT)
- v0.17.5.3 = autodrive failure + TTS audit logging (BAT'd; diagnostics retained)
- v0.17.5.2 = funnel waypoint pruning (BAT'd clean, shipped to GitHub)
- v0.17.5.1 = GPS announcement hysteresis (BAT'd clean, shipped)
- v0.17.5 = load-time 90-degree axis quantization (BAT'd clean, shipped)

## v0.17.5.3 BAT result (the input to v0.17.5.4)

Aaron pressed `\` on bghall_1 to start autodrive. Two TTS messages fired in the same second:

```
[15:59:37] [TTS] "Driving."
[15:59:37] [TTS] "No locations available." (interrupt)
```

FieldNavigation autodrive started correctly ("Driving"). Immediately afterward, the WorldMap module's PollKeys() also responded to the same `\` press, found its catalog empty (correctly -- Aaron is on a field, not the world map), and announced "No locations available" with interrupt=true. The interrupt clobbered the "Driving" announcement, so Aaron heard the second one and not the first.

Root cause traced to `IsOnWorldMap()` in `world_map_segments.inl`. It only checked `WM_SCENE_FLAG`. At boot that memory reads 0 (zero-init), returning true. WorldMap's `Poll()` declared "Entered world map" and `s_onWorldMap` stayed latched. PollKeys() ran every tick on every screen.

The world log carries a long-standing diagnostic warning that surfaced this:

```
[15:58:52][WORLD] WorldMap: Entered world map
[15:58:53][WORLD] WorldMap: Warning - On world map but game mode is 0 (expected 2)
```

The warning was observing the disagreement but never acting on it.

## v0.17.5.4: World Map polling stuck-at-startup fix

### The change

Single function rewritten. `IsOnWorldMap()` in `world_map_segments.inl` now requires BOTH:

1. `FF8Addresses::pGameMode` resolved AND equal to `MODE_WORLDMAP` (= 2).
2. THEN scene flag at `WM_SCENE_FLAG` reads 0.

Either failure returns false. Both reads SEH-wrapped.

### Files changed

- `src/ff8_accessibility.h` -- version bump (0.17.5.4)
- `src/world_map_segments.inl` -- `IsOnWorldMap()` rewritten
- `CHANGELOG.md`, `DEVNOTES.md`, `NEXT_SESSION_PROMPT.md`

### What's NOT touched

FieldNavigation autodrive steering issue (see below). Funnel pruning. Quantization. Hysteresis. TTS audit logging (retained from v0.17.5.3 -- proved its worth surfacing this bug).

## Separate bug observed (DEFERRED to v0.17.6+)

The v0.17.5.3 BAT also shows that **FieldNavigation autodrive on bghall_1 fails despite manual GPS working**. Drive started dist=3899, ran 21 seconds, ended dist=3726 (173 units progress). Multiple recovery cycles + re-paths. Final tick analysis:

- player=(134,-7461), steer=(452,-7722)
- delta = (+318 east, -261 south)
- With `driveCamRight=(1,0) driveCamDown=(0,-1)`: correct response = RIGHT+DOWN
- Log shows `kb=U lX=1000 lY=0` -- UP key + analog right only (no Y component)

The vertical axis is inverted for autodrive on this field. Manual GPS works because it uses `s_camRight/Down` (CA-quantized at field load) while autodrive uses `s_driveCamRight/Down` (runtime calibration via CALIB phases). They're separate code paths since the v0.17.2 split.

This is the "Refined-coord narrow-gate steering" backlog item plus more. The autodrive steering pipeline needs its own dedicated investigation. Queued for v0.17.6+.

## Status check at session open

**If Aaron's first message is "BAT"**: v0.17.5.4 has been built and tested. Triage in this order:

### 1. `Logs/build_latest.log` tail

Confirm `Version: 0.17.5.4` and no compile errors. The new code uses `FF8Addresses::pGameMode` and `FF8Addresses::MODE_WORLDMAP`, both already used in `world_map.cpp` Poll(). Since `world_map_segments.inl` is textually included AFTER `#include "ff8_addresses.h"`, both symbols should resolve.

### 2. `Logs/ff8_world.log` -- no spurious "Entered world map"

If Aaron loaded into a field (any field, not the world map), the world log should NOT contain `WorldMap: Entered world map` for the boot/title/load sequence. The old buggy behavior emits that line at boot. The fix should suppress it.

The "Warning - On world map but game mode is 0" log line should also disappear: if `IsOnWorldMap()` correctly returns false when gameMode=0, Poll() never enters the entry block, so the warning never fires.

### 3. `Logs/ff8_mod.log` -- clean TTS sequence on `\`

When Aaron presses `\` on a field:
- `[TTS] "Driving."` (or `[TTS] "Target not yet located."`) should appear in isolation.
- NO follow-up `[TTS] "No locations available."` immediately after.

### 4. World map sanity check

When Aaron actually reaches the world map (e.g., walks out of Balamb Garden), the WorldMap module should still announce "World map." correctly and accept `\` for autodrive. The fix tightens the entry condition; it doesn't disable world map behavior when actually on the world map.

### 5. Aaron's qualitative report

The `\` key on fields should now produce only the FieldNavigation TTS, no clobbering. The underlying autodrive-fails-to-reach-target issue will STILL be present (that's a separate bug), but at least Aaron will hear the correct "Driving" announcement instead of the misleading "No locations available" message.

## What to do after the BAT

### If clean

Push v0.17.5.4 to GitHub. The fix is small and self-contained.

Then either:
- Address the autodrive steering issue (v0.17.6 -- the "vertical axis inverted on bghall_1 for autodrive only" bug). This needs its own investigation; the calibration pipeline in `field_nav_autodrive.inl` is where to start.
- Resume the v0.16.5.2 BAT triage backlog (FMV STOP/PLAY race, party-member-as-NPC, classroom entity catalog under-population, SeeD rank #27, Fire Cavern #28, planner-fallback #29).

### If a regression appears

The fix is minimal -- just a tighter gate on one function. The most likely regression would be: WorldMap module fails to declare entry when the player IS on the world map (because gameMode hasn't updated yet, or there's a transient state where the scene flag is set before gameMode). Mitigation: keep both checks but loosen one of them.

## Autodrive steering investigation (when we get to it)

The next step for the bghall_1 autodrive failure:

1. Read `field_nav_autodrive.inl` carefully -- the calibration pipeline (phase 1 = +X push, phase 2 = +Y push, calibration done = construct driveCamRight/driveCamDown).
2. Look at how steering vector is converted to keyboard + analog. Is the keyboard direction derived from screenY using the same axes as analog?
3. The bghall_1 BAT showed phase 1 FAILED (no movement on +X push) but phase 2 succeeded (+Y push moved -Y in world). That produced `driveCamRight=(1,0) driveCamDown=(0,-1)` defaults rather than calibrated values. Maybe phase 1 failure should trigger different fallback behavior.
4. Compare with the manual nav axes (camRight=(1,0) camDown=(0,-1) for bghall_1 from CA quantization) -- they're identical to driveCamDown but driveCamRight defaulted. So maybe defaulting to (1,0) when phase 1 fails is wrong; should use the CA-derived axes as fallback?

## Hard constraints (unchanged)

- **Filesystem MCP for all Windows project files.** Bash is Linux-container and can't see them.
- **Aaron pushes via `Utilities/push_to_github.ps1`**, Claude NEVER pushes.
- **NEVER re-enable SET3 opcode hook (0x1E).**
- **F-key handlers gated** on `!(GetAsyncKeyState(VK_MENU) & 0x8000)`.
- **F12 reserved** for per-session diagnostics only.
- **Source file size limits**: 60 KB warn, 80 KB fail.
- **OneDrive sync EPERM**: retry immediately on first edit attempt.
- **AUTO `[CBF]` battle-suppressor cap stays `INT_MAX`**.
- **`.inl` files are TEXTUAL INCLUDES**: no header guards, no namespace declarations inside.
- **CHANGELOG.md top heading must match `FF8OPC_VERSION`** or the push utility refuses.
- **Navigation direction announcements are screen-relative, not world-relative.**
- **AUTO-DRIVE uses `s_driveCam*` pair (v0.17.2), MANUAL-NAV uses `s_camRight/Down`.** Neither is written by anything other than the field-load handler.

## Notes for resumption

- v0.17.5.3's TTS audit trail is now permanent infrastructure. It surfaced this WorldMap bug in one BAT cycle.
- The `[drive] REFUSED` log from v0.17.5.3 didn't fire in this BAT (autodrive validation passed) but remains in place for future debugging.
- v0.17.5.4 is the third small fix in the v0.17.5.x series. All four (quantization, hysteresis, pruning, world-map gate) work together cleanly.
- The bghall_1 autodrive failure is real but architecturally distinct -- separate axis system, separate calibration pipeline, separate code path. Don't conflate it with the manual nav improvements.

## Classroom entity catalog (parallel track, still paused)

Still pending. Need from Aaron:
1. Field name confirm -- corroborated as `bg2f_2` from v0.17.5.x BATs.
2. F9 list contents (cycle through, get the two "interaction" names if they exist).

Low priority; deferred until Aaron specifically wants to address it.
