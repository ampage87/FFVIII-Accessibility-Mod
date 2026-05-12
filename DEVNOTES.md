**Purpose & context**

Aaron is the sole developer of the FF8 Accessibility Mod -- a `dinput8.dll` injection for Final Fantasy VIII Steam 2013 (App ID 39150, FF8_EN.exe + FFNx v1.23.x) that makes the game playable for blind players via Windows SAPI text-to-speech, navigation assistance, entity catalogs. Aaron is blind himself and is the primary tester, using NVDA as his screen reader.

**Project root:** `C:/Users/ampag/OneDrive/Documents/FFVIII-Accessibility-Mod/FF8_OriginalPC_mod/`

GitHub: `ampage87/FFVIII-Accessibility-Mod`. **HEAD = v0.15.9.7.8** (pushed 2026-05-12, commit `64f3b736`). Local tree matches HEAD. Push utility squashed v0.15.9.2.16-.18, .3, .4, .5, .6, .7, .7.1-.7.8 into a single commit using the top CHANGELOG section as commit message (expected behavior).

---

## Current state: v0.15.9.8.3 READY-TO-BAT -- bridge dance + kani-slot override

**v0.15.9.8.3 READY-TO-BAT 2026-05-12.** Two coordinated changes targeting the bridge's remaining 1 catch from v0.15.9.8.1 / .8.2 BATs: (1) ChaseDetector per-field kani-slot override routes `domt1_1` to Others slot 3 (SYM 'laguna') -- empirically confirmed as the actually-pursuing X-ATM092 in v0.15.9.8.2 BAT. (2) `MODE_BRIDGE_DANCE` state machine in chase_auto_pilot.cpp drives the EAST/WEST dance using kani X-velocity classification.

### v0.15.9.8.2 BAT findings (the empirical foundation)

BridgeDiag ran 416 samples over 6.7 seconds of bridge transit (00:00:53 -> 00:01:02). Six non-zero entity slots, only one tracked the party:

| Slot | SYM | Behavior |
|---|---|---|
| Backgrounds 0/1 | zell2/selphie2 | static at (16, 0) -- irrelevant |
| Others 0 | irvine | tracks party exactly (party-leader proxy) |
| Others 1 | rinoa | trails ~145 units |
| Others 2 | quistis | trails ~348 units |
| **Others 3** | **laguna** | **THE KANI: X range -4419 -> 3836 (vaults party). Y constant -446.** |

ChaseDetector's default `"kani"` SYM resolves to Others slot 6 which reads `(0, 0)` throughout. The bridge field reuses the `laguna` SYM template for X-ATM092.

**CRITICAL: Y is constant on the bridge.** The pre-BAT v0.15.9.8.2 Y-axis-divergence theory (Aaron's mechanic descriptions interpreted as Y-match for landing, Y-divergence for jumping) is REFUTED. Jumps are X-axis only in walkmesh coords. The visual jump is the 3D Z axis. v0.15.9.8.3 uses X-velocity instead.

**Per-tick X-delta analysis** (laguna at Others slot 3, 100ms sample period):

- Pre-chase stationary (12 samples ~1.2s): dX=0, laguna at far-west spawn X=-4419.
- Normal pursuit (ticks 13-47, ~3.5s): 70-140 units/tick (avg ~106). Slightly faster than party (~93).
- Leap (ticks 48-59, ~1.2s): 350-500 units/tick (avg ~372, 3.5x normal).
- Landed (ticks 61-65, ~0.5s): 0-1 units/tick. laguna at X=3836.
- Catch fires at tick ~67 when party reaches X=2053 (laguna 1747u east, blocking).

Catch is followed by script-side teleport of party to X=3529, then field transition to doopen2a 1 second later.

### v0.15.9.8.3 design (in chase_auto_pilot.cpp)

MODE_BRIDGE_DANCE state machine, 10Hz sampling (every 6 Update ticks):

**Thresholds:** `kBridgeLeapThreshold=200`, `kBridgeLandThreshold=50`, `kBridgeLandConsec=2`, `kBridgeMinDwellTicks=60` (1s), `kBridgeWestTimeoutTicks=300` (5s).

**EAST_LEG -> WEST_LEG**: `isLanded && kX > pX && wasLeaping` for 2 consecutive samples AND minDwell met. The `wasLeaping` latch is essential -- without it the pre-chase stationary phase would trip the landed-ahead detector on sample 1.

**WEST_LEG -> EAST_LEG**: `justStartedLeaping` edge (instant turn the moment we detect a new leap mid-air, so party slips past while robot can't course-correct).

**WEST_LEG timeout**: 5-second bail-back-to-east safety net. Empirically we don't have west-leg robot behavior data (the v0.15.9.8.2 catch fired before any turn). Without timeout, a bridge variant where the robot never re-leaps would leave the party retreating forever. Worst-case timeout fire = identical to v0.15.9.8.2 behavior (1 catch at X=2053).

**ChaseDetector override** in `chase_detector.cpp::ApplyPerFieldKaniOverride(fieldName)`, called from `OnDebouncedFieldChange` after `ResolveKaniLocation`. For `domt1_1`: overwrites s_kaniLoc to arrayKind=2, arraySlot=3, symIdx=9, symName='laguna'. With the override, ReadKaniPosition() returns laguna's pos and UpdateBridgeDance can read X-velocity reliably.

### Expected v0.15.9.8.3 BAT outcomes

- `ChaseDetector: field='domt1_1' v0.15.9.8.3 OVERRIDE -> kani -> Others slot 3 (SYM 'laguna')` at field-debounce settle.
- `ChaseAutoPilot: ENGAGED on field='domt1_1' mode=BRIDGE_DANCE initial state=EAST_LEG ...` after ASK answered.
- `BridgeDance: sample state=... motion=LEAPING|LANDED|CHASING ...` at 10Hz.
- `BridgeDance: leap #1 STARTED state=EAST ...` ~5s into transit.
- `BridgeDance: EAST->WEST transition kani=... party=... kdx=...` ~1s after that.
- **Best**: `BridgeDance: WEST->EAST transition` on second leap, 0 catches.
- **Acceptable**: `BridgeDance: WEST->EAST TIMEOUT` after 5s, 0-1 catches; bridge still progresses.
- **Worst**: unforeseen interaction; BridgeDiag captures slot trajectories for post-BAT analysis.

BridgeDiag remains active for one more BAT cycle to confirm slot-3 consistency and capture west-leg robot behavior.

### Risk

Medium. East-leg detection fully calibrated from v0.15.9.8.2 data. West-leg detection inferred from Aaron's verbal description with no empirical backing. 5s timeout backstops any failure mode.

---

## Pre-v0.15.9.8.3 history (kept for context)

### v0.15.9.8.2 BAT result (2026-05-11/12)

Diagnostic-only build. Bridge took 9 seconds with 1 catch at X=2053 (matches v0.15.9.8.1 baseline as expected). BridgeDiag dumped 416 lines identifying laguna at Others slot 3 as the moving kani. Findings detailed above drive v0.15.9.8.3.

### v0.15.9.8.1 BAT result (town square clean)

**v0.15.9.8.1 BAT SUCCESS 2026-05-12.** Aaron's prediction matched the result exactly: zero catches on the town square, just the one on the bridge left.

### v0.15.9.8.1 BAT result on doopen2a

TargetY bumped from -3703 to -3800 (97 units past the south trigger line). A* found a path further south than v0.15.9.8 -- party reached pos `(-955, -3672)`, just 31 units north of the trigger line at y ~= -3703. Field transitioned cleanly 3 seconds later, before the chase script's catch evaluator window expired. **Zero `[CBF]` catches on doopen2a.**

Comparison v0.15.9.8 vs v0.15.9.8.1 (south-stop position):

- v0.15.9.8 (tgt=-3703): party stopped at `(-958, -3541)` -- 162 units north of trigger -- catch fired during 8s wait
- v0.15.9.8.1 (tgt=-3800): party stopped at `(-955, -3672)` -- 31 units north of trigger -- field transitioned in 3s, no catch

The walkmesh DID extend further south than v0.15.9.8's stop point; A* was conservatively pulling its goal back from the requested target. Asking for a target further south let A* find a path further south. The 31-unit gap to the actual trigger line is close enough that either (a) the party crossed it during the final south burst between 1Hz log samples or (b) the engine's trigger detection accepts the walkmesh-edge stop as a crossing.

### Cumulative progression

| Version | Total chase catches | Delta |
|---|---|---|
| v0.15.9.2.18 (baseline before refinement) | 11 | -- |
| v0.15.9.7.8 (west trail extended-key fix) | 3 | -8 |
| v0.15.9.8 (town square south-target fix) | 2 | -1 |
| v0.15.9.8.1 (town square targetY -3800) | 1 | -1 |
| v0.15.9.8.2 (diagnostic-only, no behavior change) | 1 | 0 |
| **v0.15.9.8.3 target (bridge dance)** | **0** | **-1** |

**91% reduction from baseline as of v0.15.9.8.1. v0.15.9.8.3 targets the final catch.**

### Open questions

1. **Bridge (domt1_1) west-leg robot behavior.** Empirically unknown -- v0.15.9.8.3 BAT will give us the first data on what laguna does when the party turns west. The 5s timeout fallback covers any failure mode.
2. **dotown_1 stuck at (-210, -1000).** UNBLOCKED 2026-05-12: Aaron confirmed the Lapin Beach FMV does fire after the extract is generated. The stall is pre-FMV, not a real problem.

---

## Pre-v0.15.9.8.1 history (kept for context)

### v0.15.9.8 BAT result (town square partial fix)

**v0.15.9.8 ready to BAT 2026-05-12.** The v0.15.9.7.8 BAT was a success on the west trail (the original goal of v0.15.9.7+). Reviewing the full BAT log identified two remaining issues: 2 catches on `doopen2a` (town square) and 1 catch on `domt1_1` (bridge). v0.15.9.8 addresses the bigger of the two -- the town square.

### Diagnosis of doopen2a from v0.15.9.7.8 BAT field log

`doopen2a` has **0 INF gateways** (per `INF parsed: 0 active gateways for 'doopen2a'` at line 4499). The field exposes exactly two SETLINE trigger lines:

- `idx=1` line(-1432,660) -> (-1349,1048) center (-1390, 854) -- NW
- `idx=257` line(-814,-3717) -> (-1091,-3689) center (-952, -3703) -- **SOUTH (the actual exit)**

`BuildFallbackConfig` dropped to tier 2 (TRIGGER-LINE) because INF gateways were unavailable, then `GetTriggerLineNearestCluster` mis-selected the NW trigger because the walkmesh's largest dead-end cluster lands at `(-1315, 503)` (an interior corner, not an exit). The party was steered NW; the catch evaluator fired during the mis-directed run; the post-catch script knockback teleported them south to `(-1042, -917)` (that's the 1187 dmag I'd previously and wrongly attributed to general script-control); the A* path was now stale; party sat stuck for ~9 seconds with 2 catches firing during the wait.

Aaron's recipe (the actual ground truth): *"The Town Square does require the party to keep running. You mostly head down from where you enter the field in order to get to the next."* My prior "script-controlled / no footsteps because the script owns positioning" theory was wrong -- the party SHOULD be running, and they weren't because the auto-pilot was steering against the chase direction.

### The fix that landed in v0.15.9.8

New explicit MODE_TARGET config entry for `doopen2a` in `kFieldConfigs[]` (`src/chase_auto_pilot.cpp`), inserted between `domt5_1` and `dotown_2` to match chase-route order:

```cpp
{ "doopen2a", MODE_TARGET,
  /*dirX=*/0, /*dirY=*/0,
  /*targetX=*/-952, /*targetY=*/-3703,
  /*walk=*/false,
  /*stages=*/nullptr, /*stageCount=*/0 },
```

Targets the SOUTH SETLINE trigger center directly, bypassing the cluster heuristic. Running per Aaron's recipe.

### Backlog status

The v0.15.9.x chase auto-pilot is functionally complete for the playable chase route, with v0.15.9.8 being the targeted polish on the remaining heavy-catch field. After the v0.15.9.8 BAT, the remaining catch sources are: `domt1_1` bridge (1 catch mid-traversal -- low priority polish) and any open questions on `dotown_1`'s south-exit / FMV completion.

### Field-name mapping (Aaron's authoritative in-game geography, confirmed 2026-05-12)

Past sessions misidentified several chase fields. The correct mapping is:

| Aaron's name | Field code | Direction |
|---|---|---|
| chase start | `domt4_1` | south-east |
| (intermediate, transitions ~instantly) | `domt3_2` | west |
| **west trail** | `domt5_1` | walk SW → S → SE |
| one field between west trail and bridge | `domt2_1` | TARGET south |
| **bridge** | `domt1_1` | due east |
| **town square** | `doopen2a` | running south (v0.15.9.8 explicit MODE_TARGET to south trigger) |
| (after town square) | `dotown_3`, `dotown_2`, `dotown_1` | south sequence to Lapin Beach FMV |

**Important:** `doopen2a` is NOT the bridge. Past DEVNOTES backlog entries referencing "bridge AI rule #2" on `doopen2a` were wrong — that field is the town square, and the v0.15.9.7.8 BAT analysis initially attributed it to script-controlled positioning. That was also wrong. The actual cause was the cluster heuristic mis-selecting the NW trigger when the field requires running south. v0.15.9.8 adds an explicit MODE_TARGET entry pointing at the south trigger center `(-952, -3703)` which sidesteps the heuristic entirely.

### v0.15.9.7.8 BAT [CBF] catch counts by field

| Field | Aaron's name | Time | Catches | Notes |
|---|---|---|---|---|
| domt4_1 | chase start | 3 s | 0 | running SE, clean |
| domt3_2 | (intermediate) | 1 s | 0 | transitions immediately |
| **domt5_1** | **west trail** | 15 s | **0** ✨ | the v0.15.9.7.8 win |
| domt2_1 | (between west trail and bridge) | 14 s | 0 | TARGET, brief stall |
| **domt1_1** | **bridge** | 9 s | **1** | east traversal, catch mid-bridge ~22:22:04 |
| **doopen2a** | **town square** | 13 s | **2** | wrong direction (NW vs south); fixed in v0.15.9.8 |
| dotown_3 | (after town square) | 15 s | 0 | brief stall, recovers |
| dotown_2 | | 13 s | 0 | south run, brief stall |
| dotown_1 | (final pre-FMV) | 22+ s | 0 | stuck at (-210,-1000); extract ends here without field-change |
| **Total** | | | **3** | -8 from v0.15.9.2.18 baseline of 11; v0.15.9.8 targets -2 (down to 1) |

### Open question (dotown_1)

The extract ends at 22:23:22 with the party stuck at `dotown_1` pos (-210,-1000), still ENGAGED, no field-change or chase-end markers captured. Need to confirm with Aaron whether the BAT proceeded to the Lapin Beach FMV after the extract was generated, or whether the chase stalled there. Historically `dotown_1` has been a stall-then-recover field (v0.15.9.2.17 stalled 24 s, v0.15.9.2.18 stalled 9 s before FMV).

---

## Pre-v0.15.9.7.8 history (kept for context)

**v0.15.9.7.7 BAT FAILED 2026-05-12.** Field log analysis showed v0.15.9.7.7 ran exactly as intended (pure keyboard, no fake gamepad, override=0 gamepad=0 confirmed) but party still ran at dmag 750-855 and `[CBF]` catch fired 3 times.

Aaron's verbatim diagnosis: "I am quite certain it is W. I suspect the W key press is not making its way to the game. You also mentioned that we're repressing W on every tick, but we should be holding W down continuously."

### Root cause: `InjectKey` always sets `KEYEVENTF_EXTENDEDKEY`

In `field_nav_autodrive.inl`:

```cpp
inp.ki.dwFlags = KEYEVENTF_SCANCODE | KEYEVENTF_EXTENDEDKEY  // <-- unconditional
               | (down ? 0 : KEYEVENTF_KEYUP);
```

Arrow keys are extended (E0 prefix in hardware); letter keys like W are NOT. Every W injection from v0.15.9.7 onward produced malformed scancode `E0+0x11` that FF8's DirectInput reader didn't recognize as W. Arrows worked because they're correctly extended. W never worked.

Our own `world_map.cpp` v0.14.102 comment flagged this exact issue for car driving: "A and W are NOT extended keys; arrow keys ARE extended." The car-control `PressKey`/`ReleaseKey` was fixed in v0.14.102 with the same approach. The field-nav `InjectKey` was just never updated.

### The fix

1. `InjectKey` gains an `extended` parameter (default `true` preserves arrow-call compat).
2. W call sites in `field_nav_directiondrive.inl` pass `extended=false` (3 sites).
3. Defensive W re-press path REMOVED per Aaron's hold-vs-tap point. With the extended-key fix, one KEYDOWN at engagement is enough; OS holds state until KEYUP at Disengage.
4. `InjectKey` now logs `SendInput` failures (return != 1) for future diagnosis.

### Sequence

| Version | Walk-mode input | Result |
|---|---|---|
| v0.15.9.7 - .7.4 | analog 1000 + W (broken extended) | Caught |
| v0.15.9.7.5 | analog 350 + W (broken) | Caught |
| v0.15.9.7.6 | analog 0 + W (broken) | Caught |
| v0.15.9.7.7 | no gamepad + W (broken) | Caught (W never reached engine) |
| **v0.15.9.7.8 (this)** | **no gamepad + W (FIXED extended=false, held)** | **Predicted: walks, 0 catches** |

### Predicted v0.15.9.7.8 BAT outcome

- Field log: `FRESH-START W KEYDOWN injected (scancode=0x11, extended=0) -- will be held continuously until Disengage (no re-press)` ONCE at engagement on `domt5_1`. NO subsequent re-press lines.
- Audio: walking-speed footsteps from first audible step.
- `domt5_1` transit ~13-18s, dmag ~270-303 matching manual.
- 0 catches, no robot.
- If `[InjectKey] SendInput FAILED` ever appears, we'd know input was dropped at OS level.

### Risk

Very low. `InjectKey` signature is backward-compatible. Arrow callers unchanged. Only 3 W sites edited. Re-press deletion is the noisy code path; clean removal. `WALK_REPRESS_PERIOD` constant and counters remain in file as vestigial documentation but are no longer referenced.

### v0.15.9.7.7 BAT result (2026-05-12 21:58:55) -- FAILED

All v0.15.9.7.7 markers confirmed in `Logs/ff8_field.log`. Pure keyboard path active. But W was dropped due to KEYEVENTF_EXTENDEDKEY bug. Party ran at dmag 750-855 (manual is 270-303). `[CBF]` chase battle calls fired at 21:59:00, 21:59:07, 21:59:13 — three legitimate catch triggers blocked by the mod's freeze. The audio Aaron heard was real.

**v0.15.9.7.6 BAT FAILED 2026-05-12.** Aaron: "Still triggered the robot on the west trail. It sounded like the party was running. Are you sure W was being held the whole time and that only directional keys were sent to the game?"

Honest answer: I wasn't sure. The code calls `InjectKey(W, KEYDOWN)` at field entry and re-presses every tick, with the only release in `StopDirectionDrive`. So in theory W is held. But we never verified SendInput's return value, only logged the first re-press, and never logged the engine's perceived state. Intent != evidence.

### The likely cause

Even with analog magnitude at 0, the fake gamepad was still INSTALLED. FF8 PC may have an input-mode flag based on gamepad presence:

- Gamepad attached: analog magnitude is the speed authority; W treated as keyboard-only modifier that the analog code path ignores.
- No gamepad: keyboard + W is the speed authority.

Aaron's manual play has no gamepad; manual `domt5_1` walks in 13s with 0 catches. Our fake gamepad install (even zeroed) may have flipped FF8 into "gamepad mode" -- W ignored for speed, even though keyboard direction reached the engine.

### What v0.15.9.7.7 changes

In `StartDirectionDrive`:

- **walk=true**: skip `DD_InstallFakeGamepad()`, leave `s_analogOverrideActive=false`. Pure keyboard arrows + W. Mirrors manual play exactly.
- **walk=false**: unchanged (install fake gamepad, full deflection).
- **Walk-state flip mid-engagement**: run→walk uninstalls fake gamepad + disables override; walk→run reinstalls + re-enables.

The v0.15.9-era assumption that "keyboard alone doesn't move the party" came from one early BAT and was never isolated. Aaron's manual play empirically refutes it. v0.15.9.7.7 re-tests.

### Added diagnostics

To stop guessing whether W is actually reaching the engine:

- Every W press/release logs scancode (`0x11`).
- Fresh-start W KEYDOWN logs explicitly with marker `FRESH-START W KEYDOWN injected`.
- Walk-state flip logs the path swap.
- W re-press logs every 60 ticks (≈1s) with current override+gamepad flags. Instead of v0.15.9.7.1's first-only latch.
- `STARTED` log now includes `override=N gamepad=N`.

Noisy for one BAT cycle. Restored after diagnosis.

### Sequence

| Version | Analog | Fake gamepad | Result |
|---|---|---|---|
| v0.15.9.7 - .7.4 | 1000 | installed | Caught |
| v0.15.9.7.5 | 350 | installed | Caught |
| v0.15.9.7.6 | 0 | installed | Caught |
| **v0.15.9.7.7 (this)** | **n/a (no override)** | **not installed** | **Predicted: walks, 0 catches** |

### Risk

Medium. If the v0.15.9-era "keyboard alone doesn't move" finding is still operative for our SendInput path, the party won't move on `domt5_1`. Fallback v0.15.9.7.8 re-installs the fake gamepad but freezes its state.

If the party walks and the catch still fires, the chase script reads something orthogonal to player input (state, animation, scripted timer). Investigate FF8 disasm / FFNx source.

### Verification log markers for the BAT

Unique to v0.15.9.7.7 -- presence confirms the new build actually ran:

- `FRESH START walk=1 -- keyboard-only path (no fake gamepad, no analog override)`
- `FRESH-START W KEYDOWN injected (scancode=0x11)`
- `STARTED dir=(-1,+1) walk=1 lX=0 lY=0 arrows=0x06 override=0 gamepad=0`
- `walk modifier RE-PRESSED (tick #N in current engagement, period=1, override=0, gamepad=0)` -- once per second

### v0.15.9.7.6 BAT result (2026-05-12) -- FAILED

At analog magnitude 0 but fake gamepad still installed, party ran and caught the robot. Implies the fake gamepad's PRESENCE (not its analog values) flips FF8 into a mode that ignores W. v0.15.9.7.7 removes the install.

For BAT plan see `NEXT_SESSION_PROMPT.md`.

**v0.15.9.7.5 BAT FAILED 2026-05-12.** Aaron's report: "Still ran and triggered the robot. Should we consider just disabling analog navigation and using just directional keys when walking is required?"

Magnitude 350 was still above whatever threshold triggers the catch. We don't know if the threshold is a speed value above 350, or if the catch evaluator reads something other than speed entirely (e.g., "is analog non-zero"). Either way, the fix is the same.

### Aaron's suggestion

Mirror manual play exactly. He has no joystick connected; his manual `domt5_1` traversal uses keyboard arrows + W only, no analog input. Reliably walks the field in 13 seconds with 0 catches. Our auto-pilot should do the same shape of input.

### The fix

One-line constant change in `src/field_nav_directiondrive.inl`:

```cpp
static const int WALK_ANALOG_MAGNITUDE = 0;   // v0.15.9.7.6: zero deflection
```

When `walk=true`, the analog goes to (0, 0). Keyboard arrows + W modifier do all the work. The fake gamepad stays installed (v0.15.9 BAT proved its presence matters for FF8's input dispatch path).

### Sequence

| Version | Analog | W | Result |
|---|---|---|---|
| v0.15.9.7 - .7.4 | 1000 (full) | held | Caught |
| v0.15.9.7.5 | 350 (partial) | held | Caught |
| **v0.15.9.7.6 (this)** | **0 (none)** | **held** | **Predicted: walks, 0 catches** |

### Risk

Very low. One-line constant. The walk=false path is untouched.

### Fallback

If the party doesn't move at all (centered fake gamepad + active analog override might block keyboard reading), v0.15.9.7.7 disables `s_analogOverrideActive` for walk fields. Fake gamepad stays installed (presence matters) but engine reads from saved-pointer state, equivalent to no gamepad being active for input.

If v0.15.9.7.7 also fails, we go to the FF8 disasm / FFNx source.

### v0.15.9.7.5 BAT result (2026-05-12) -- FAILED on domt5_1 (party ran at magnitude 350)

At magnitude 350 the party still ran and the robot triggered. Either FF8's analog walk threshold is much lower than the PSX-50/127 estimate (~394 in our scale), or the catch evaluator reads a signal other than speed entirely. v0.15.9.7.6 sidesteps the question with magnitude=0.

> "Are we emulating directional keys or the analog stick? Could it be that the way we are emulating the navigation is not respecting the W key being held down to walk? I know the directional keys respect that, but not sure if the analog stick does."

### The root cause (finally)

FF8 PC walk/run determination depends on input source:

- **Keyboard + W**: W asserts walking regardless of arrow deflection. Aaron's manual play uses this path.
- **Analog stick**: speed = deflection magnitude. Full deflection = run; partial = walk. **W doesn't apply to analog.**

Our direction-drive emulates BOTH. We write fake-gamepad analog at **full deflection** (`lX = dirX * 1000`), so the catch evaluator on chase fields reads "running" regardless of W press state. Walking audio (driven by W + keyboard arrows) sounded correct, but the catch evaluator is on the analog code path.

This explains every catch from v0.15.9.7 onward:

- v0.15.9.7: party caught (analog at 1000, W eventually swallowed).
- v0.15.9.7.1: defensive W re-press period=30. Walking audible eventually. Still caught (analog at 1000).
- v0.15.9.7.2: period=1, walking audible from frame 1. Still caught (analog at 1000).
- v0.15.9.7.3: Y-flip wrong direction. Stuck.
- v0.15.9.7.4: SW->S->SE direction restored, domt3_2 fixed. Walking audible. Still caught (analog at 1000).

### What v0.15.9.7.5 changes

One source file: `src/field_nav_directiondrive.inl`. Two constants added:

```cpp
static const int RUN_ANALOG_MAGNITUDE  = 1000;  // full deflection = running
static const int WALK_ANALOG_MAGNITUDE = 350;   // ~35% deflection = walking
```

Both analog-write sites in `StartDirectionDrive` (fresh-start branch + already-running branch) select `walk ? WALK_ANALOG_MAGNITUDE : RUN_ANALOG_MAGNITUDE`. When walk=false (every chase field except domt5_1), behavior is unchanged. When walk=true (domt5_1), analog is now at 35% deflection per axis, which produces walking speed in the engine.

W press is **kept** as belt-and-suspenders: covers any walk-modifier analog consultation FF8 might do, and preserves walking-animation/footstep audio cues for Aaron's accessibility.

### Magnitude calibration (350)

PSX FF8 walking threshold was at analog value ~50 / 127. Scaled to our `±1000` DirectInput convention, that's ~394. 350 stays solidly below.

Diagonals (SW, SE, NW, NE) at 350 per axis = vector magnitude `~495`, still below the run-threshold vector magnitude `~557`. Diagonals walk too.

FF8 PC analog deadzone is small (~100), so 350 is well above it. Party should move.

If 350 hits the deadzone (party doesn't move), v0.15.9.7.6 bumps to 400-500. If 350 is still above the run threshold (catch fires), v0.15.9.7.6 drops to 250.

### Predicted v0.15.9.7.5 BAT outcome

- `[direction-drive] STARTED dir=(-1,+1) walk=1 lX=-350 lY=350 arrows=0x06` log (note the new magnitudes).
- Audio: walking-speed footsteps throughout `domt5_1` (unchanged; was already correct).
- `domt5_1` transit ~13-18s.
- **0 catches on `domt5_1`. No robot.**
- Full chase completes cleanly.

### Risk

Very low. Two constants added; two write-site selectors. Walk=false fields untouched. The existing logging continues to work; `lX`/`lY` values in the log now reflect the chosen magnitude, which doubles as confirmation that the new constant is selected.

### v0.15.9.7.4 BAT result (2026-05-11/12) -- PARTIAL on domt5_1 (domt3_2 fixed, west trail still catches)

Aaron's clarified cardinal-direction recipe (the authoritative ground truth):

> "The first field, where the chase actually begins, is good to go.
> The second field, between the starting field and the west trail, you said has the character going east when it should be west, northwest, west.
> This latest build broke the AD on the west trail. It should be heading generally southwest, south, southeast."

Aaron's terminology mapped to our analog convention:

| Aaron's word | Arrow keys | (dirX, dirY) |
|---|---|---|
| southwest | Down + Left | (-1, +1) |
| south | Down | (0, +1) |
| southeast | Down + Right | (+1, +1) |
| west | Left | (-1, 0) |
| northwest | Up + Left | (-1, -1) |
| east | Right | (+1, 0) |

v0.15.9.7.4 makes two config changes in `src/chase_auto_pilot.cpp`:

1. **`domt5_1` revert**: back to `MODE_STAGED_DIRECTION` with `kStages_domt5_1[]` (the exact v0.15.9.7 / .7.1 / .7.2 config). Stages encode SW=(-1,+1) -> S=(0,+1) -> SE=(+1,+1) with Y thresholds 2200 and 1100.
2. **`domt3_2` fix**: flip dirX from `+1` (east) to `-1` (west). Single MODE_DIRECTION for now; if the "northwest" middle stage matters, v0.15.9.7.5 adds staging.

The `WALK_REPRESS_PERIOD=1` change from v0.15.9.7.2 stays in place as defensive coverage.

### Lessons (preserved in Finding #31 of lessons doc)

- Always translate the player's natural-language descriptions to cardinal directions BEFORE coding. The "LEFT and slightly UP" phrasing was ambiguous: "up" could mean screen-up OR position-on-the-trail. Cardinal terms (southwest/south/southeast/west/northwest/etc.) are unambiguous and translate directly to `(dirX, dirY)` via `DD_DirsToArrowMask`.
- Aaron's cardinal terminology mapping table is now documented near the top of the lessons doc for quick reference in future direction discussions.
- The v0.15.9.5 BAT "success" on `domt3_2` (5s / 1 catch attributed to "script-forced co-location") was misdiagnosed -- it was a direction-conflict catch firing in 5 seconds. The v0.15.9.5 comments are kept in `chase_auto_pilot.cpp` for history but the diagnosis is now corrected to direction-conflict in the v0.15.9.7.4 comment block.

### The carryover hypothesis now becomes testable

Aaron's pushback after v0.15.9.7.2 was: "Did you check the possibility if the prior field is somehow carrying over?" At the time I dismissed it in favor of a direction-conflict theory on `domt5_1`; v0.15.9.7.3 was supposed to test that theory and failed.

v0.15.9.7.4 sets up the actual carryover test:

- `domt3_2` going east while Aaron presses west was a sustained direction conflict for ~5 seconds in every BAT since v0.15.9.5.
- That may have primed some script state (kani aggression timer, chase-fighting flag, etc.) that carried into `domt5_1` and triggered the catch even with correct staged direction and walking.
- Fixing `domt3_2` to go west removes the priming.
- If `domt5_1` now gets 0 catches with the same staged-direction config that was failing in v0.15.9.7.2, carryover was the cause.
- If catches still fire on `domt5_1` after both fixes, v0.15.9.7.5 investigates SendInput vs physical-key input differences.

### Predicted v0.15.9.7.4 BAT outcome

- `domt3_2`: transit ~2-3s (matching Aaron's manual) with 0 catches. Log shows `ENGAGED ... mode=DIRECTION direction=WEST RUNNING (dirX=-1 dirY=0 walk=0)`.
- `domt5_1`: transit ~13-18s. Ideally 0 catches if carryover hypothesis is right. Log shows `ENGAGED ... mode=STAGED_DIRECTION ... starting stage 0/3 direction=SOUTHWEST WALKING`. Stage transitions log at Y=2200 and Y=1100.
- No AD stuck. Party reaches each field's exit.

### Risk

Very low. Two config-table edits. `domt3_2` is a single dirX sign flip. `domt5_1` reverts to a config that navigated successfully in three prior BATs (v0.15.9.7, .7.1, .7.2). `kStages_domt5_1[]` unchanged.

### v0.15.9.7.3 BAT result (2026-05-11) -- FAILED on domt5_1 (AD stuck at spawn)

### Aaron's correction

> "You mentioned that the mod is pushing east on that field, but when I played through manually I went mostly LEFT and slightly UP."

In analog terms his recipe is `(dirX=-1, dirY=-1)` -- screen-up-left. Our config had `(dirX=-1, dirY=+1)` -- screen-down-left. **The Y axis was flipped.**

Reinterpreting his trace: he presses LEFT+UP on the keyboard, gets world-coord ΔX=+1659 and ΔY=-3130. So this field's camera maps screen-LEFT to world-east (X axis inverted, known from v0.15.9.6) AND screen-UP to world-Y-decreasing (Y axis also inverted, surfaced now).

When our mod set dirY=+1 (screen-down) we asked the engine to move the party AWAY from the exit. The walkmesh is narrow, so the script's forced "flee down the trail" motion still drove the party to the exit (which is why v0.15.9.7 BAT navigated the geometry). But the engine evidently reads the analog-vs-script direction conflict as a catch trigger, independent of W modifier state.

This explains why v0.15.9.7.2's period=1 re-press confirmed walking but didn't help: the catch isn't on running. It's on the analog direction conflict.

### What v0.15.9.7.3 ships

One config-table entry rewritten in `src/chase_auto_pilot.cpp`. domt5_1 switches from `MODE_STAGED_DIRECTION` (three stages with dirY=+1) to `MODE_DIRECTION` (single direction with dirY=-1):

```cpp
{ "domt5_1", MODE_DIRECTION,
  /*dirX=*/-1, /*dirY=*/-1,
  /*targetX=*/0, /*targetY=*/0,
  /*walk=*/true,
  /*stages=*/nullptr, /*stageCount=*/0 },
```

Aaron's manual trace shows ONE direction throughout the 13-second traversal (dmag 267-303 walking, no transitions). Single direction sufficient. The MODE_STAGED_DIRECTION machinery stays in place but is unused for domt5_1; other fields may use stages later.

The WALK_REPRESS_PERIOD=1 change from v0.15.9.7.2 stays in place as defensive coverage.

### Aaron's domt5_1 trace (the gold), reinterpreted

```
Keyboard input:  LEFT + slightly UP (held throughout)
  -> analog (dirX=-1, dirY=-1) = screen up-left
Camera mapping:  screen-LEFT = world-east (X+)
                 screen-UP = world-Y-decreasing
World result:    spawn (-1004,3253) -> exit (655,123)
                 ΔX = +1659 (east), ΔY = -3130 (Y-)
Speed:           dmag 267-303 throughout (walking)
Time:            13 seconds
Catches:         0
```

My mistake in the v0.15.9.7.2 analysis: I read the world-coord output (X+, Y-) and labeled it "south-east in world" without checking which screen direction produces it. The output direction was correct; the input direction was wrong.

### Walking is still only for the west trail

Unchanged from v0.15.9.7.2. domt5_1 is the only chase field with `walk=true`. WALK_REPRESS_PERIOD=1 re-press branch is dead code on all walk=false fields. No behavior change for any field other than domt5_1.

### Predicted v0.15.9.7.3 BAT outcome

- `ENGAGED on field='domt5_1' mode=DIRECTION ...` with `dirX=-1 dirY=-1 walk=1`.
- `[direction-drive] STARTED dir=(-1,-1) walk=1` log.
- `[direction-drive] walk modifier RE-PRESSED (defensive, period=1 ticks)` within ~17ms.
- Audio: walking-speed footsteps throughout domt5_1.
- **0 catches on domt5_1. No robot appearance.**
- Transit ~13-18s.

If X-axis interpretation was ALSO wrong, v0.15.9.7.4 tries (dirX=+1, dirY=-1) or other quadrants empirically.

### Risk

Low. One config entry edited. dirY sign flipped, mode changed, stages pointer changed to nullptr.

### v0.15.9.7.2 BAT result (2026-05-11, AUTO mode) -- PARTIAL on domt5_1 (walking from first frame, but robot still triggered)

The period=1 re-press succeeded fully at the audio level: Aaron heard walking-speed footsteps on domt5_1 from the very first audible frame. No "sounds like running first then walking" transition. The W press reaches the engine reliably with the new cadence.

But the robot still appeared. Walking didn't prevent the catch.

Aaron's correction: "you mentioned that the mod is pushing east on that field, but when I played through manually I went mostly LEFT and slightly UP." The Y axis of our stage 0 was inverse of his recipe. The walkmesh was forcing the party along the trail despite the wrong Y, so the field navigated, but the engine flagged the analog-vs-script direction conflict as a catch.

### v0.15.9.7.1 BAT result (2026-05-11, AUTO mode) -- PARTIAL on domt5_1 (walking confirmed, but robot still triggered)

v0.15.9.7.1 successfully shipped the defensive re-press: Aaron heard walking footsteps on domt5_1, confirming the period=30 re-press eventually landed the W press. But the robot still triggered, meaning a catch fired despite the walking audio.

The manual playthrough run that followed (20:14-20:20) captured ground truth: Aaron's physical W press is held BEFORE the engine's first-frame running-vs-walking check on field-load. Our period=30 re-press fires at ~tick 30 (~500ms), well after that check has already committed "player is running" to a script variable.

### v0.15.9.7 BAT result (2026-05-11 19:35, AUTO mode) -- PARTIAL on domt5_1 (geometry OK, wrong speed)

Build succeeded at 19:35:05. Chase fired and `MODE_STAGED_DIRECTION` engaged on domt5_1. Geometry was navigated successfully — stages transitioned through SW→S→SE and the party reached the south exit. But Aaron heard running footsteps and the kani arrived on its 6-second timer.

Aaron's framing: "Somehow we triggered the robot on the west trail. Remember we need to be holding down the W key to walk on this field. The robot appeared quickly as if we'd run and running on this field triggers it to appear."

The full chase did eventually progress through dotown_2 (19:39:07) and dotown_1 (19:39:22), confirming the staged-direction approach navigates the S-curve correctly. Only the walk modifier failed to reach the engine.

### v0.15.9.5 BAT result (2026-05-11 18:11-18:14, AUTO mode)

**Field-by-field comparison vs v0.15.9.4 baseline:**

| Field | v0.15.9.4 | v0.15.9.5 | Catches | Drive |
|---|---|---|---|---|
| domt4_1 | 3s / 0 | **3s / 0** | 0 | DIR south-east (reproducible) |
| domt3_2 | 6s / 1 | **5s / 1** | 0 | DIR east (NEW v0.15.9.5, -1s from CALIB skip) |
| domt5_1 | 30s / 3 | 32s / 3 | 0 | TGT (382,235) WALK |
| domt2_1 | 17s / 0 | 16s / 0 | 0 | INF-gateway fallback |
| domt1_1 | 10s / 1 | 11s / 1 | 0 | INF-gateway fallback |
| doopen2a | 15s / 2 | 15s / 2 | 0 | INF-gateway fallback (bridge) |
| dotown_3 | 18s / 0 | 19s / 0 | 0 | INF-gateway fallback |
| dotown_2 | 16s / 0 | 15s / 0 | 0 | DIR south |
| dotown_1 | 8s / 0 | 9s / 0 | 0 | DIR south |
| **Total** | **2:06 / 7** | **2:09 / 7** | 0 | Within natural BAT variance |

**Outcome: PARTIAL as predicted.** v0.15.9.5 saved 1 second on domt3_2 via CALIB skip. Catch on domt3_2 still fires (1 catch, script-forced co-location) -- the kani is teleported on top of the party at field entry; the catch animation IS the mechanism that advances the party through the field. Cannot be eliminated without structural change.

**Three-signal pattern continues to work where applicable.** domt4_1 reproducibly clean (0 catches, 3 seconds, second BAT in a row). domt3_2 transit time improvement consistent with prediction.

### v0.15.9.4 BAT result (2026-05-11 17:27-17:30, AUTO mode)

**Field-by-field comparison vs v0.15.9.2.18 baseline:**

| Field | v0.15.9.2.18 | v0.15.9.4 | Catches | Drive |
|---|---|---|---|---|
| domt4_1 | 17s / 3 catches | **3s / 0 catches** | -3 | DIR south-east (NEW v0.15.9.4) |
| domt3_2 | 5s / ~1 | 6s / 1 | 0 | INF-gateway fallback |
| domt5_1 | 44s / ~3 | **30s / 3** | 0 | TGT (382,235) WALK |
| domt2_1 | 16s / ~1-2 | 17s / **0** | -1 | INF-gateway fallback |
| domt1_1 | 11s / ~1 | 10s / 1 | 0 | INF-gateway fallback |
| doopen2a | 15s / 2 | 15s / 2 | 0 | INF-gateway fallback (bridge) |
| dotown_3 | 20s / 0 | 18s / 0 | 0 | INF-gateway fallback |
| dotown_2 | 15s / 0 | 16s / 0 | 0 | DIR south |
| dotown_1 | 9s / 0 | 8s / 0 | 0 | DIR south |
| **Total** | **2:32 / 11** | **2:06 / 7** | **-4** | |

Notable: domt5_1 dropped 14 seconds without any config change — cascade benefit from auto-pilot arriving in better shape (not in catch-recovery freeze). domt2_1 also dropped a catch from cascade.

**Critical empirical data point: analog effectiveness on domt4_1 with the right direction.** v0.15.9.4 engaged-tick deltas with analog=(+1000,+1000):

```
[17:28:04] (engage) pos=(-578,3036) delta=(79,-96)   dmag=124  kani=(-1645,4154) kdist=1545
[17:28:05] (sec 1)  pos=(-226,2202) delta=(352,-834) dmag=905  kani=(-947,3515)  kdist=1497
[17:28:06] (sec 2)  pos=(-325,1319) delta=(-99,-883) dmag=888  kani=(-519,2550)  kdist=1246
[17:28:06] Field transition to domt3_2
```

~900 units/sec of party motion in roughly the analog direction (south-east-ish). Compare to v0.15.9.3 (WEST direction) where the cleanest between-catches sample showed only `dmag=61`. **Pressing the right direction is ~15x faster than pressing the wrong direction** on this field. Analog isn't subtle on domt4_1; cooperating with the script's flow unlocks running speed.

### v0.15.9.3 BAT findings (2026-05-11 17:03-17:06, AUTO mode)

### v0.15.9.2.18 BAT result (2026-05-11 16:26-16:30, AUTO mode)

Chase commit at 16:26:41, chase climax FMV (`disc00_07h.avi`) at 16:29:16 -- **2 minutes 35 seconds end-to-end with no manual intervention**.

Field-by-field timings and catches:

| Field | Time | Catches | Drive mode |
|---|---|---|---|
| domt4_1 (chase start) | 17s | ~1-2 | DIRECTION RUN LEFT |
| domt3_2 | 5s | ~0-1 | INF-gateway fallback |
| domt5_1 (walking) | 44s | ~3-4 | TARGET (382,235) WALK |
| domt2_1 | 16s | ~1-2 | INF-gateway fallback |
| domt1_1 | 11s | ~0-1 | INF-gateway fallback |
| doopen2a (bridge) | 15s | 2+ | INF-gateway fallback |
| dotown_3 | 20s | 0 | INF-gateway fallback |
| dotown_2 | 15s | 0 | DIRECTION SOUTH (v0.15.9.2.17) |
| dotown_1 | 9s | 0 | DIRECTION SOUTH (v0.15.9.2.18) |

**Total NO-OPed catches: 11** (from `[CBF] NO-OP chase BATTLE call` log lines):

- #1 (kdist=401) on domt4_1 at 16:26:42 -- 1 second after Auto commit, kani close at chase start.
- #5 (kdist=1753) on domt5_1 at 16:27:21 -- mid walking field.
- #10 (kdist=1807) on doopen2a at 16:28:17 -- bridge.
- #11 (kdist=1412) on doopen2a at 16:28:22 -- bridge again.
- Catches #2-#4 and #6-#9 are scattered across the intermediate mountain trail and town fields up to and including the bridge.
- **No catches after #11**: dotown_3, dotown_2, dotown_1 all zero catches.

The direction-mode fixes (v0.15.9.2.17 for dotown_2, v0.15.9.2.18 for dotown_1) demonstrate the principle: once chase-scene scripts aren't fighting the navigation system, catches drop to zero. Refinement work targets the fields where they're still high: mountain trail and bridge.

### v0.15.9.2.17 BAT findings (2026-05-11 16:11-16:14, AUTO mode)

Mixed result. Auto-pilot drove the entire chase end-to-end and the chase climax FMV did fire. But there was a 24-second stall on `dotown_1`.

**dotown_2 fix (NEW in v0.15.9.2.17) worked perfectly:**

```
[16:13:25] ENGAGED on dotown_2 mode=DIRECTION direction=south running
[16:13:26] pos=(-1363,3599)
[16:13:27] pos=(-1102,2739)  -- 900 units/sec south
[16:13:28] pos=(-779,1839)
[16:13:39] dotown_1 transition
```

14-second clear vs the 41-second manual baseline.

**dotown_1 stuck:**

```
[16:13:40] ENGAGED on dotown_1 mode=TARGET tgt=(-246,-555) running
[16:13:40] [CALIB] phase 1 FAILED: no movement (dist=0.0)
[16:13:41] [CALIB] phase 2 FAILED: derived camDown=(0,-1) from camRight perpendicular
[16:13:48] pos=(89,2297) -- 3300 units of correct south-west movement, then stuck
[16:13:49..16:14:12] velocity-stuck / wp-skipping cycle for 24 seconds
[16:14:13] disc00_07h.avi FMV finally fires
```

CALIB failure is intrinsic to dotown_1's first-second state (not Aaron's manual input -- this BAT had no manual interference). Same fix as dotown_2.

### v0.15.9.2.16 BAT findings (2026-05-11 15:21-15:24, AUTO mode)

Auto-pilot drove the chase route end-to-end through `dotown_3` (one field further than v0.15.9.2.15), then the bug fired on `dotown_2`. Field log on dotown_2:

```
[chase-drive] target on different walkmesh island (start tri 45, goal tri 137)
[chase-drive] redirecting to trigger line 3 center=(-840,908) tri=77 dist=3699
[A*] No path from tri 45 to tri 77 (48 iterations)
[chase-drive] STARTED tgt=(-197,-600) walk=0 player=(-1642,4518) waypoints=0 startDist=3699 trigIdx=-1 crossLine=yes
[CALIB] phase 1 moved (237,72), phase 2 moved (72,-237)
[drive] stopped: Arrived.
ChaseAutoPilot: DISENGAGED (chase-drive completed)
ChaseAutoPilot: field 'dotown_2' marked auto-pilot complete; won't re-engage
```

Three A* failures (player on tri 45, gateway target on tri 137, trigger-line bridge on tri 77 -- all on disconnected walkmesh islands). chase-drive STARTED with `waypoints=0`. Calibration moved the player ~500 units; arrival check fired trivially; v0.15.9.2.11's completion marker locked the field. Aaron drove dotown_2 manually for 39 seconds, then dotown_1 also manually (auto-pilot tried to engage there with 65 valid waypoints but calibration failed due to Aaron's manual key conflicts with the fake gamepad), until disc00_07h.avi (the chase climax FMV) fired naturally.

v0.15.9.2.17 fixes dotown_2 only. dotown_3 (30 waypoints, real arrival) and dotown_1 (65 waypoints) keep the generic INF-gateway fallback.

### v0.15.9.2.15 BAT findings: significantly farther than before, but chase not complete

BAT'd 2026-05-11 14:00-14:05. Aaron loaded a save just before the chase trigger, walked into the chase scene, committed Auto in the chase ASK at 14:03:02, then went hands-off. Field log sequence:

- 14:03:02 -- `ChaseDetector: chase_mode set to 'auto' (persisted)` (Aaron commits Auto)
- 14:03:18 -- field announce: MH-3 (`domt3_2`)
- 14:03:24 -- field announce: MH-7 (`domt5_1`)
- 14:04:06 -- field announce: MH-1
- 14:04:22 -- field announce: Town Square 1
- 14:04:33 -- field announce: Town Square 5
- 14:04:48 -- field announce: Town Square 10 = `dotown_3`
- 14:04:49 -- `chase DEACTIVATED on entry to 'dotown_3' (non-chase field). CHASE-END SUMMARY mode=auto battles_fired=0 battles_suppressed=11`

**The mod's `CHASE-END SUMMARY` is misleading.** It fires when ChaseDetector sees a field not in its chase-fields list, not when the in-game chase ends. The actual in-game chase ends only when the Lapin Beach FMV plays (robot chasing Squall on the beach). There are 2-3 more fields after dotown_3 before that FMV fires. v0.15.9.2.15's auto-pilot disengaged on dotown_3 because the detector flipped state, so the final fields were not driven.

**11 battles suppressed indicates slow steering, not success.** The chase is designed so a skilled player can outrun X-ATM092 without any battles firing. Each battle call is the robot catching up; cap=0 NO-OPs the battle but the catch happened. Steering speed refinement is a deferred priority: get the full chase working first, then tune for zero catches.

What v0.15.9.2.15 DID achieve, beyond all previous builds:
- Multi-field chase auto-driving via the generic INF-gateway fallback. Each chase field's INF gateway found, direction-test selected the forward-progress one, chase-drive aimed at it, crossing detected, field transition fired.
- Domt5_1 (west trail) and several subsequent chase fields traversed without intervention.
- Survived doopen2a (bridge area, presumably one of Town Square 1 / 5) without the planned PJUMPA + AI rule #2 reverse-direction state machine.

The INF gateway insight from v0.15.9.2.14's failure: SETLINE Line entities can be Event Trigger (kani battle calls), not Screen Boundary, on chase fields like domt2_1. INF gateways are the engine's actual screen-transition mechanism. `GatewayInfo` gained `lineX1/Y1/lineX2/Y2` endpoint fields; `GetGatewayNearestCluster` picks the gateway whose direction-from-player aligns with the player->cluster vector (dot product > 0). `StartChaseDrive` accepts explicit cross-line endpoints; `BuildFallbackConfig` uses three-tier preference: INF gateway -> trigger line -> largest cluster center.

### Files changed in v0.15.9.2.15

- `src/field_archive.h`, `src/field_archive.cpp` -- `GatewayInfo` gains line endpoint fields; `LoadINFGateways` stores them.
- `src/field_navigation.h`, `src/field_navigation.cpp` -- new public `GetGatewayNearestCluster` API; new state vars `s_driveCrossLineX1/Y1/X2/Y2`, `s_driveCrossLineActive`.
- `src/field_nav_directiondrive.inl` -- `StartChaseDrive` signature extended with `crossLineX1/Y1/X2/Y2` endpoints.
- `src/field_nav_autodrive.inl` -- crossing block reads from `s_driveCrossLine*` state for chase-drive.
- `src/chase_auto_pilot.cpp` -- `BuildFallbackConfig` three-tier preference (gateway -> trigger line -> cluster); `Engage` passes both trigger index and gateway endpoints.
- `src/ff8_accessibility.h` -- version 0.15.9.2.15.
- `CHANGELOG.md` top entry, push-ready.

---

## Recent history

- **v0.15.9.8.3** -- READY-TO-BAT 2026-05-12. Bridge dance + kani-slot override.
- **v0.15.9.8.2** -- BAT 2026-05-11/12. Diagnostic-only; BridgeDiag identified laguna at Others slot 3 as the moving kani on the bridge. Drove v0.15.9.8.3 thresholds.
- **v0.15.9.8.1** -- BAT SUCCESS 2026-05-12. targetY -3800 on doopen2a, zero catches.
- **v0.15.9.7.8** -- **BAT SUCCESS 2026-05-12.** West trail walked, full chase completed end-to-end. ROOT CAUSE was InjectKey's unconditional KEYEVENTF_EXTENDEDKEY for non-extended W. Fixed + defensive re-press removed.
- **v0.15.9.7.7** -- BAT FAILED 2026-05-12. Pure keyboard, no fake gamepad. W never reached engine due to extended-key bug. Logs confirmed the build ran correctly.
- **v0.15.9.7.6** -- BAT FAILED 2026-05-12. Magnitude 0 but fake gamepad still installed; party ran.
- **v0.15.9.7.5** -- BAT FAILED 2026-05-12. Magnitude 350 still ran and caught. Either the threshold is well below 350 or the catch evaluator reads something other than speed.
- **v0.15.9.7.4** -- BAT PARTIAL 2026-05-11/12. `domt3_2` direction fix landed (Aaron heard running across the field). `domt5_1` still caught, ruling out carryover hypothesis. Aaron's follow-up question identified the analog-magnitude issue.
- **v0.15.9.7.3** -- BAT FAILED 2026-05-11. Y-axis flip on domt5_1 to `(dirX=-1, dirY=-1)`. AD stuck at spawn; Aaron couldn't manually find the next field either. Reverted in v0.15.9.7.4.
- **v0.15.9.7.2** -- BAT PARTIAL 2026-05-11. WALK_REPRESS_PERIOD=1 re-press succeeded (walking from first frame audible) but robot still triggered. Pushed Aaron to ask about carryover.
- **v0.15.9.7.1** -- BAT PARTIAL 2026-05-11. Defensive W re-press with period=30. Walking eventually audible but late.
- **v0.15.9.7** -- BAT PARTIAL 2026-05-11 19:35. `MODE_STAGED_DIRECTION` navigated the S-curve geometry on domt5_1 correctly. But party ran; W KEYDOWN was swallowed.
- **v0.15.9.6** -- BAT FAILED 2026-05-11 18:31-18:34. Held south-east (+1,+1) drove party into east wall.
- **v0.15.9.5** -- BAT SUCCESSFUL 2026-05-11 18:11-18:14. domt3_2 went from 6s/1 catch to 5s/1 catch. -1s from CALIB skip. Catch is script-forced co-location (cannot be eliminated without structural change). domt4_1 reproducibly 0 catches/3s. Total chase 2:09/7 catches (consistent with v0.15.9.4 within BAT variance).
- **v0.15.9.4** -- BAT SUCCESSFUL 2026-05-11 17:27-17:30. domt4_1 dropped from 3 catches/16s to 0 catches/3s with RUN SOUTH-EAST. Total chase 2:06/7 catches (was 2:35/11 catches). Three-signal analysis validated. Not yet pushed.
- **v0.15.9.3** -- BAT SUCCESSFUL 2026-05-11 17:03-17:06. Diagnostic build. Pre-engage chase-active log + delta computation gave us the data to identify domt4_1's correct direction. Not pushed.
- **v0.15.9.2.18** -- BAT SUCCESSFUL 2026-05-11 16:26-16:30. Full chase end-to-end in 2:35, disc00_07h.avi fired naturally, 11 NO-OPed catches concentrated on mountain trail + bridge, zero catches on town fields. Chase auto-pilot feature-complete for AUTO mode. Not yet pushed.
- **v0.15.9.2.17** -- BAT 2026-05-11 16:11-16:14. AUTO mode drove the full chase end-to-end and disc00_07h.avi did fire. dotown_2 direction-fix worked perfectly (14s clear). dotown_1 stuck for 24s on CALIB failure before drifting into the FMV trigger. Not pushed.
- **v0.15.9.2.16** -- BAT 2026-05-11 15:21-15:24. AUTO mode drove one field further than v0.15.9.2.15 (cleared dotown_3 via INF-gateway). Disengaged on dotown_2 because A* found no path (walkmesh fragmented). Not pushed.
- **v0.15.9.2.15** -- BAT MAJOR PROGRESS 2026-05-11 14:00-14:05, pushed 2026-05-11. Multi-field chase auto-driving via INF-gateway fallback. Manual exploration BAT 14:54-15:01 confirmed post-bridge route through to chase climax FMV.
- **v0.15.9.2.14** -- trigger-line CROSSING detection. SETLINE filter rejected Event Trigger lines on domt2_1.
- **v0.15.9.2.13** -- tightened chase-drive arrive distance 300 -> 60.
- **v0.15.9.2.12** -- flipped fallback walk default to false (running).
- **v0.15.9.2.11** -- per-field completion marker to stop re-engagement loop.
- **v0.15.9.2.10** -- re-added ASK gate (chase auto-pilot wedged input during scripted intro).
- **v0.15.9.2.9** -- velocity-stuck advance + `std::sqrt` replacing IntSqrt.
- **v0.15.9.2.8** -- kani-position diagnostic (refuted collision-push hypothesis).
- **v0.15.9.2.7** -- fixed v0.15.9.2.6 log spam (moved BuildFallbackConfig out of per-tick path).
- **v0.15.9.2.6** -- generic chase-field fallback via largest cluster center.
- **v0.15.9.2.5** -- advance funnel wp on chase-drive no-progress. West trail (domt5_1) end-to-end.
- **v0.15.8.1** -- BAT SUCCESS, pushed 2026-05-10.
- **v0.15.x** trail: see `DEVNOTES_HISTORY.md`.

---

## Backlog

### Next development priorities

1. **BAT v0.15.9.8.3 on the bridge.** Expected markers: `ChaseDetector ... v0.15.9.8.3 OVERRIDE ... Others slot 3 (SYM 'laguna')` at field-debounce settle, `ChaseAutoPilot: ENGAGED ... mode=BRIDGE_DANCE` after ASK answered, `BridgeDance: sample state=... motion=...` at 10Hz throughout transit, `BridgeDance: leap #1 STARTED state=EAST ...` ~5s in, `BridgeDance: EAST->WEST transition` ~1s after that. Best case: `BridgeDance: WEST->EAST transition` on second leap with 0 catches recorded for `domt1_1` in `[CBF] count by field`. Acceptable: `BridgeDance: WEST->EAST TIMEOUT` after 5s with 0-1 catches.
2. **Bridge dance refinement** based on v0.15.9.8.3 BAT data. If west-leg timeout fires, characterize laguna's west-leg behavior and update detection. If the dance works, cleanup pass: trim BridgeDiag verbosity, consider removing BridgeDiag entirely.
3. **`dotown_1` south-exit / FMV completion.** UNBLOCKED 2026-05-12: Aaron confirmed the Lapin Beach FMV does fire after the extract is generated. The stuck-at-(-210,-1000) is the pre-FMV stall, not a real problem.
4. **Stall recovery improvements** on `domt2_1`, `dotown_3`, `dotown_2` -- brief stalls of 5-7 s each. Lower priority than catch elimination.
5. **Push v0.15.9.8.x to GitHub** after the bridge work is complete. Aaron runs `Utilities/push_to_github.vbs`. Claude does NOT push.
2. **Add Finding #33 to the lessons doc.** Already done during the v0.15.9.7.8 checkpoint.
3. **Clean up the vestigial re-press state.** `WALK_REPRESS_PERIOD` constant, `s_walkRepressCounter`, `s_walkRepressLogged` in `field_nav_directiondrive.inl` are no longer referenced. Safe to delete after a confidence cycle.
4. **v0.15.10 -- Original = chase-mod-active flag.** Vanilla-engine chase behavior for the Original choice. Not blocking on full-chase completion.
5. **Consolidate key-injection helpers** (lower priority, per Finding #33). Unify `world_map.cpp`'s `PressKey`/`ReleaseKey` with field-nav's `InjectKey`.
6. **F9 path-finding cleanup.** Apply v0.15.9.2.4's kb-from-analog and v0.15.9.2.5's advance-on-stuck to F9 path-finding by dropping the `s_chaseDriveActive`-only gates.
7. **v0.15.x cleanup** -- remove `Phase2_TestAsk` + Shift+F12 diagnostic.

### Standalone

- X-ATM092 battle-name fix.
- Generalized countdown-timer hook.
- Cleanup: dead `Hook_field_get_dialog_string` override branch.

### Deferred

- `chase_diag::OnAskOpcodeFired` snprintf bug.
- Remove party members from entity catalog.
- SeeD rank bug #27, walk-and-talk dialog gap.
- X-ATM092 chase audio descriptions during the chase.
- Refined-coord narrow-gate steering.
- Fire Cavern entry (#28) + planner-fallback (#29).
- Cosmetic rename: `chase_kani_freeze` -> `chase_agent_pin`.

---

## Key infrastructure (reference)

**Chase auto-pilot per-field config** (`src/chase_auto_pilot.cpp`):

```cpp
enum FieldDriveMode { MODE_DIRECTION, MODE_TARGET, MODE_STAGED_DIRECTION, MODE_BRIDGE_DANCE };
struct FieldStage {            // v0.15.9.7
    int8_t  dirX, dirY;
    bool    walk;
    int32_t activeMinY;        // stage active when party Y >= this value
};
struct FieldConfig {
    const char*       fieldName;
    FieldDriveMode    mode;
    int8_t            dirX, dirY;
    int32_t           targetX, targetY;
    bool              walk;
    const FieldStage* stages;      // v0.15.9.7, null for non-staged modes
    int               stageCount;
};
```

- `domt4_1`: `MODE_DIRECTION` RUN SOUTH-EAST (dirX=+1, dirY=+1, walk=false). Explicit config (v0.15.9.4).
- `domt3_2`: `MODE_DIRECTION` RUN EAST (dirX=+1, dirY=0, walk=false). Explicit config (v0.15.9.5).
- `domt5_1`: `MODE_STAGED_DIRECTION` walk SW→S→SE by Y. Stages `kStages_domt5_1` (v0.15.9.7).
- `domt1_1`: `MODE_BRIDGE_DANCE` EAST/WEST state machine driven by kani X-velocity (v0.15.9.8.3).
- `doopen2a`: `MODE_TARGET` south to (-952, -3800) (v0.15.9.8.1).
- `dotown_2`, `dotown_1`: `MODE_DIRECTION` RUN SOUTH (dirY=+1).
- All other chase fields: generic fallback via `BuildFallbackConfig`. Three-tier preference:
  1. `GetGatewayNearestCluster` -- INF gateway aligned with player->cluster direction (cross-product test).
  2. `GetTriggerLineNearestCluster` -- SETLINE Screen Boundary/Unknown lines (rejects Event Trigger).
  3. `GetLargestClusterCenter` -- largest walkmesh dead-end cluster center.

**Chase-fields list (incomplete).** ChaseDetector's chase-fields list does not extend through to the Lapin Beach FMV. dotown_3 deactivates the detector despite the in-game chase still being active. Extending this list is the next priority.

**`CHASE-END SUMMARY` log line is mod-side bookkeeping, not in-game chase end.** It fires on entry to any field not in the chase-fields list. The true in-game chase end is the Lapin Beach FMV.

**Chase-drive API** (`src/field_nav_directiondrive.inl`): `StartChaseDrive(targetX, targetY, walk, crossLineX1, Y1, X2, Y2)` / `StopChaseDrive()` / `IsChaseDriveActive()`. Path-finding A*+funnel via shared F9 infrastructure.

**Chase-drive state**: `s_chaseDriveActive`, `s_chaseDriveWalk`, `s_chaseDriveTargetX/Y`, `s_driveCrossLineX1/Y1/X2/Y2`, `s_driveCrossLineActive`. The cross-line state is read by `UpdateAutoDrive`'s crossing block for chase-drive (no longer dependent on `s_capturedLines` for chase-drive, since gateways aren't SETLINE entities).

**Mutex:** chase-drive vs direction-drive vs F9 path-finding. F9 backslash refuses to cancel chase-drive. Arrow-cancel suppressed during chase-drive.

**Camera calibration** (per field, on first drive): `camRight` = world dir produced by lX=+1000; `camDown` = world dir produced by lY=+1000.

---

## Session ritual

Read `DEVNOTES.md` and `NEXT_SESSION_PROMPT.md` at START of every session.

Update DEVNOTES + NEXT_SESSION_PROMPT at TWO points: every version bump and after every BAT result.

**Filesystem MCP for all Windows project files.** Bash runs in a Linux container.

**Aaron pushes via `Utilities/push_to_github.vbs`.** Claude NEVER pushes.

**Build/BAT cycle.** Aaron runs `deploy.vbs`. "BAT" = built and tested -> read `Logs/build_latest.log` tail then domain log.

**Keep `Plan & Research Documents/Auto-drive lessons from chase auto-pilot.md` updated** with new findings as chase / F9 auto-drive work progresses. The v0.15.9.2.15 INF-gateway success deserves a new finding entry (priority for next session if not already added).
