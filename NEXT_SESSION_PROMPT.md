# Next Session Prompt -- v0.15.9.8.3 READY-TO-BAT; bridge dance + kani-slot override

**Build state:** v0.15.9.8.3 implemented 2026-05-12, awaiting build. Two coordinated changes targeting the bridge's remaining catch from v0.15.9.8.1 / .8.2 BATs:

1. **ChaseDetector per-field kani-slot override** for `domt1_1`. Routes the kani entity pointer to Others slot 3 (SYM 'laguna'), which v0.15.9.8.2 BAT BridgeDiag (416 samples / 6.7 seconds) empirically proved is the actually-pursuing X-ATM092. The default `"kani"` SYM resolves to Others slot 6 which reads `(0, 0)` the entire transit -- the bridge field reuses the `laguna` template for its chase agent.
2. **MODE_BRIDGE_DANCE state machine** in chase_auto_pilot.cpp, used only on `domt1_1`. Per-tick (10Hz) classification of the kani's X-velocity as LEAPING (>200 units/tick), LANDED (<50 units/tick), or CHASING (everything between). Two states (EAST_LEG / WEST_LEG) with asymmetric transition rules: turn west when kani lands ahead AFTER observing a leap; turn east the instant a new leap STARTS mid-air. 5-second west-leg timeout safety net.

**HEAD on GitHub:** v0.15.9.7.8 (pushed 2026-05-12, commit `64f3b736`). Local tree is several versions ahead at v0.15.9.8.3 awaiting BAT.

## Read first

1. `DEVNOTES.md` -- current state (v0.15.9.8.3 READY-TO-BAT section at top, with full v0.15.9.8.2 BAT findings that drove the v0.15.9.8.3 thresholds).
2. This file -- BAT plan and verification markers.
3. The top of `src/chase_auto_pilot.cpp` -- inline rationale for the MODE_BRIDGE_DANCE implementation and threshold derivation.

## What just landed

**v0.15.9.8.2 BAT (2026-05-11/12) identified laguna at Others slot 3 as the bridge's chase agent.** 416 BridgeDiag samples over 6.7 seconds documented:

- Y is **constant** at -446 on the bridge -- jumps are X-axis only in walkmesh coords (visual jump is the 3D Z axis). This refuted the pre-BAT Y-axis-divergence hypothesis.
- Normal pursuit: ~106 units / 100ms tick.
- Leap: ~372 units / 100ms tick (3.5x normal).
- Landed: 0-1 units / tick.

The catch fires at tick ~67 when the party reaches X=2053 with laguna landed 1747u east at X=3836. v0.15.9.8.2 BAT recorded the same 1 catch as v0.15.9.8.1 (diagnostic-only build, no behavior change).

**v0.15.9.8.3 ships two coordinated changes** to break the catch:

### Change 1: ChaseDetector::ApplyPerFieldKaniOverride (chase_detector.cpp)

Called from `OnDebouncedFieldChange` after `ResolveKaniLocation`. For `domt1_1`, overwrites `s_kaniLoc` with arrayKind=2 (Others), arraySlot=3, symIdx=9, symName='laguna'. After the override, `ReadKaniPosition()` returns laguna's real position instead of slot 6's (0, 0).

### Change 2: MODE_BRIDGE_DANCE (chase_auto_pilot.cpp)

New drive mode (`MODE_BRIDGE_DANCE = 3`), explicit `kFieldConfigs` entry for `domt1_1`, `UpdateBridgeDance(fieldName)` helper runs at 10Hz.

**State machine:**

- **EAST_LEG (initial)**: drive east. When kani lands in front of party (`isLanded && kX > pX`) for 2 consecutive samples AND `wasLeaping` latch is true AND minDwell (1s) met -> turn west. The latch is essential because laguna's pre-chase 12-sample stationary phase would otherwise trip the landed-ahead detector on sample 1.
- **WEST_LEG**: drive west. When a new leap STARTS (`justStartedLeaping` edge) AND minDwell met -> turn east. Asymmetric rule from Aaron's mechanic: turn the instant the robot is mid-air, before it can course-correct.
- **WEST_LEG timeout**: 5 seconds with no leap fires -> bail back to east. Worst-case behavior = identical to v0.15.9.8.2 (1 catch).

**Thresholds:** `kBridgeLeapThreshold=200`, `kBridgeLandThreshold=50`, `kBridgeLandConsec=2`, `kBridgeMinDwellTicks=60`, `kBridgeWestTimeoutTicks=300`.

BridgeDiag from v0.15.9.8.2 remains active for one more BAT cycle to confirm slot-3 consistency and capture west-leg robot behavior (which is empirically unknown).

## BAT plan for v0.15.9.8.3

Aaron's BAT cycle:

1. `deploy.vbs` -> `src/deploy.ps1` -> `src/deploy.bat`
2. Launch FF8, load the save near chase start.
3. Trigger the chase. Choose **Auto** mode at the chase ASK.
4. Go hands-off through the whole chase, especially the bridge. Pay attention to whether the party AUDIBLY changes direction (footsteps reversing) when the robot lands in front, and again when the robot leaps.
5. After the chase climax FMV, exit FF8 and regenerate `Logs/chase_events_extract.log` via `Utilities/dump_chase_events.vbs`.

### Verification markers (in `Logs/ff8_field.log`)

**On `domt1_1` entry (field-debounce settle), expect:**

```
ChaseDetector: field='domt1_1' kani symIdx=12 -> Others slot 6 (...)
ChaseDetector: field='domt1_1' v0.15.9.8.3 OVERRIDE -> kani -> Others slot 3 (SYM 'laguna'), ...
```

The default SYM resolution fires first, then the override fires. **If the OVERRIDE line is absent, the override path didn't run -- investigate `ApplyPerFieldKaniOverride` call site in `OnDebouncedFieldChange`.**

**On engagement (after ASK answered), expect:**

```
ChaseAutoPilot: ENGAGED on field='domt1_1' mode=BRIDGE_DANCE initial state=EAST_LEG direction=east running (dirX=+1 dirY=0 walk=0). ...
```

**Per-sample at 10Hz throughout transit, expect:**

```
BridgeDance: sample state=EAST motion=CHASING|LEAPING|LANDED kani=(X,Y) party=(X,Y) kdx=N consecLanded=N wasLeaping=0|1 dwell=N
```

**Around the leap moment (~5s into transit), expect:**

```
BridgeDance: leap #1 STARTED state=EAST kani=(X,Y) party=(X,Y) kdx=~400
... [several LEAPING samples] ...
... [LANDED samples with kani.X > party.X] ...
BridgeDance: EAST->WEST transition kani=(~3836,-446) party=(~1500,-607) kdx=~0 (landed_in_front for 2 samples, leapCount=1, dwell=N)
```

**After the turn, watch for what laguna does:**

- **Best case**: laguna leaps again (westward, or some other re-engagement) -> `BridgeDance: leap #2 STARTED state=WEST ...` -> `BridgeDance: WEST->EAST transition ...`. Party then runs east unobstructed across the rest of the bridge. **0 catches on domt1_1.**
- **Acceptable**: no second leap fires within 5 seconds -> `BridgeDance: WEST->EAST TIMEOUT ...`. Party reverts to east, may get caught at X=2053 like v0.15.9.8.2 (0-1 catches).
- **Worst**: unforeseen failure mode. BridgeDiag captures the slot trajectories during the west leg; post-BAT analysis tells us what laguna did.

**On field exit (east-edge SETLINE crossing into doopen2a), expect:**

```
ChaseDetector: fieldId changed to 0x... -- starting 2000 ms name-debounce
ChaseAutoPilot: DISENGAGED (field changed) was on field='domt1_1' mode=3
ChaseDetector: name debounce settled: id=0x... name='doopen2a'
```

Bridge transit time goal: 10-15 seconds (vs 9 seconds in v0.15.9.8.2 -- the dance adds a west-leg detour). Catch count goal: 0.

### Three-way outcome triage

**SUCCESS (the dance works):**

- 0 `[CBF]` catches on `domt1_1` in the BAT extract's `[CBF] count by field` table.
- Log shows `EAST->WEST transition` followed by `WEST->EAST transition` (no TIMEOUT).
- Bridge transit 10-15 seconds.
- Total chase catch count drops to 0.
- Cleanup pass in v0.15.9.8.4: trim BridgeDiag verbosity (it's served its purpose), shrink per-sample logging to transition-only.

**ACCEPTABLE (timeout fires, partial improvement):**

- `BridgeDance: WEST->EAST TIMEOUT` appears in log.
- 0-1 catches on `domt1_1`.
- v0.15.9.8.4 characterizes laguna's west-leg behavior from BridgeDiag samples and tightens the WEST->EAST detection. Possibly add an additional turn-east criterion (e.g., "laguna sufficiently west of party with no recent leap, safe to resume east").

**FAIL (something broke or got worse):**

- More than 1 catch on `domt1_1`, OR the dance oscillates (multiple EAST->WEST and WEST->EAST transitions per second despite `kBridgeMinDwellTicks=60` debounce), OR `BridgeDance: kani read FAILED` lines appear (override didn't work).
- For oscillation: thresholds were misjudged; v0.15.9.8.4 widens the leap/land gap and bumps minDwell.
- For kani read failure: investigate `ApplyPerFieldKaniOverride` -- maybe Others slot 3 is null at the moment of engagement, or arrayKind=2 doesn't resolve correctly on this field.
- For more catches: the dance is actively making things worse (e.g., turning west blocks party from making progress, or the chase script penalizes direction changes); rollback v0.15.9.8.3 to v0.15.9.8.2's no-behavior-change state and rethink.

## Cumulative chase catch progression

| Version | Total chase catches | Delta |
|---|---|---|
| v0.15.9.2.18 (baseline) | 11 | -- |
| v0.15.9.7.8 (west trail) | 3 | -8 |
| v0.15.9.8 (town square) | 2 | -1 |
| v0.15.9.8.1 (town square refinement) | 1 | -1 |
| v0.15.9.8.2 (diagnostic) | 1 | 0 |
| **v0.15.9.8.3 target** | **0** | **-1** |

A successful v0.15.9.8.3 BAT closes out the chase-scene catch elimination work.

## Outstanding items after v0.15.9.8.3 BAT

Pending BAT outcome:

1. **`dotown_1` south-exit / FMV completion.** UNBLOCKED 2026-05-12: Aaron confirmed Lapin Beach FMV fires after extract ends. Stall is pre-FMV, not a problem.
2. **Stall recovery improvements** on `domt2_1`, `dotown_3`, `dotown_2` -- brief stalls of 5-7s each. Lower priority than catch elimination.
3. **Aaron pushes v0.15.9.8.x to GitHub** after the bridge work is complete via `Utilities/push_to_github.vbs`. Claude does NOT push. Every version bump from v0.15.9.7.8 onward must have a matching top-of-CHANGELOG entry; the push utility validates this.
4. **Lessons doc** -- add Finding #35 (or whatever number) capturing the SYM-name-resolution-fallback insight: when the default SYM-name match resolves to an empty slot, the field may be reusing a generic NPC SYM template for the chase agent. The per-field override pattern in `ApplyPerFieldKaniOverride` is the model for similar fixes if they arise elsewhere.
5. **Cleanup** of BridgeDiag once the dance is proven (v0.15.9.8.4 candidate).

## What happens if v0.15.9.8.3 BAT succeeds

Update DEVNOTES + CHANGELOG to capture the BAT result. The chase auto-pilot is then complete for catch elimination across the playable route. Aaron can choose to ship v0.15.9.8.3 to GitHub before moving on to v0.15.10 (Original = chase-mod-active flag) or other backlog items.

## What happens if v0.15.9.8.3 BAT returns ACCEPTABLE or FAIL

Iterate on thresholds / detection logic. v0.15.9.8.4 either tightens the WEST->EAST detection for the timeout-fire case, or rolls back to v0.15.9.8.2 for the FAIL case and rethinks the bridge mechanic from scratch using the new west-leg BridgeDiag data.
