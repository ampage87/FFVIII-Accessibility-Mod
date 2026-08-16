## v0.21.0

#79: **the auto-drive called a walking man a vehicle, five times, and orbited
Edea's House.**

> *"I landed the Garden outside Edea's House, but the auto-drive failed to get
> me there on foot. It would get 1-2km out, then announce vehicle detected, then
> wouldn't finish the approach and the distance would increase once again."*
> — Aaron

---

### The log, in seven lines

```
21:29:27  YAWDRIVE  dist=4750     <- the FOOT law, walking straight in
21:29:29  YAWDRIVE  dist=3305
21:29:31  YAWDRIVE  dist=1604
21:29:33  YAWDRIVE  dist=16       <- SIXTEEN UNITS from the waypoint
21:29:33  VEHSIG    VEHICLE DETECTED (24 of 24)
21:29:34  HDG-DIAG  dist=293      <- the VEHICLE law takes over
21:29:38  HDG-DIAG  dist=2469     ...and then an orbit at 1200-1400, forever
```

**The foot law was working.** It closed 4,750 units to 16 without a stumble. The
discriminator fired in that same second and handed a walking character to the
turn-then-go law written for a car, which pushed him back out and held him in a
circle he could not leave. The same shape repeats at 21:21:18 (dist 4), 21:27:06
(dist 84) and 21:28:41 (dist 14) — **every verdict lands the instant the drive
reaches a waypoint.**

### Why the waypoint, and why it was always going to happen

Because the waypoint is what manufactures the evidence.

The discriminator compares the measured motion bearing against two references —
the model heading (`mh`) and the camera yaw — on frames where those two disagree
by more than ~26°. Whichever the motion follows names the vehicle.

When the route advances a waypoint **the mod slews the camera to the new bearing
in a single write**, and the character then rotates to catch up over several
frames. Through all of them he is still walking the *old* heading. Two adjacent
frames from the log say it outright:

```
cam=3403  mh=3403      walking straight -- the references agree, no vote
cam=3855  mh=3403      camera written; gap 452; he has not turned yet
```

and the turn that follows reads `1977, 1465, 953, 441` — the gap **closing**,
frame by frame, as the rotation completes. Every one of those frames sides with
`mh`, and not one of them says anything about what the player is riding.

**Of the 25 voting frames the ring held at the first verdict, every single one
came from a turn.** The old comment claimed the verdict was "unreachable on foot
(0/153 disagreement frames)". That control data was honest — it simply never
contained a mod-driven camera slew.

### Two guards, one of them authoritative

**1. A run of closing gaps is a turn, and cannot vote.** One shrinking frame is
noise and still counts; two in a row is a rotation. A vehicle's gap does not
systematically close — it persists while the vehicle drives on, and a gap that
merely wobbles still votes.

**2. The engine's own vehicle id vetoes the verdict.** `0x020409E0` read **0 —
on foot — at every drive start of that session**, while this heuristic declared
a vehicle five times. A heuristic does not get to overrule a direct reading. An
unreadable id (-1) still makes no claim, so the fallback keeps doing the exact
job it was built for.

### The policy is now testable, because that is why this survived

**Nothing in the tree compiled `world_map_drive_exec.inl`** before a Windows
build — no host harness reaches it, so a 66 KB fragment of steering logic had no
check at all. The decisions are extracted into **`src/world_map_vehsig.inl`**
(the geometry stays in the executor) and **`tests/vehsig_test.cpp`** drives them
with the 25 real frames recovered from `ff8_world.log`:

* the **old** policy latches on that fixture — the test asserts this first, so a
  fixture that stops reproducing the defect fails loudly rather than passing
  vacuously
* the **new** policy does not latch, with the id readable *or* unreadable
* a vehicle with an unreadable id is **still detected**, whether its gap is
  steady or wobbling
* the same vehicle-shaped evidence with the engine saying on foot is refused,
  and announced **once**, not once per frame
* the `1977, 1465, 953, 441` rotation contributes 2 votes instead of 4
* `VehSigReset` really resets, so a car drive cannot arm the following foot drive

### Verification

* `vehsig_test` **OK (0 bad)** — new gate, add it to the run list
* `lint_seh` OK (88 files); `minigame_bgbtl_compile` 0 errors / 0 bad;
  `garden_harness` 26 ok / 0 bad; `catalog_story_test` 0 failures;
  `garden_aboard_test` and `world_map_harness` pass
* `world_map_drive_exec.inl` 66,357 → **66,207**; `field_navigation.cpp`
  untouched at 81,645

**NOT MSVC-built.**

### BAT

Stand outside Edea's House on foot and auto-drive to it.

1. **No "Vehicle detected."** should be spoken at any point.
2. The distance should fall and keep falling — no bounce back out to 2 km, no
   orbit.
3. If you want the guard's own voice, grep `[VEHSIG]`: silence is the pass. A
   `verdict VETOED ... vehicleId=0 says ON FOOT` line means the physics latch
   still reached a majority and the engine id caught it — worth telling me
   about, but harmless.

Then drive somewhere in the car to confirm the vehicle law still engages —
`[VEHID] engine vehicleId=… -> steering as VEHICLE from drive start` should
appear at the drive start, which is the path that does the work now.
