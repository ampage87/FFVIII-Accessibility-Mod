# Next Session Prompt

## Status: v0.14.103.5 IMPLEMENTED, awaiting BAT.

## v0.14.103.4 BAT regression

Aaron's report: **"the bounce detection and arrival announcement at B-Garden worked as expected. However, it no longer drives the car into towns that support entry by car such as Balamb."**

So:
- Balamb Garden (non-car-friendly) → bounce-arrived announce works ✓
- Balamb Town (car-friendly) → broken ✗ (regressed in v0.14.103.3)

## Root cause

v0.14.103.3's sweep-abort hook fires bounce-arrived on the FIRST sweep abort. But for car-friendly towns the engine fires MODE_FIELD on a SUBSEQUENT pass through the trigger area.

The v0.14.103 BAT log captured this exactly for foot AD to Balamb:
```
[21:43:44] Final-approach timeout
[21:43:44] [DRIVE-SWEEP] Started
[21:43:46] [DRIVE-SWEEP] Aborting (drifted) — returning to normal steering
[21:43:50] Entered final approach zone again
[21:43:53] Arrival via game-mode (MODE_FIELD)
```

Cycle 1 sweep aborts → drive resumes normal steering → cycle 2 enters zone → MODE_FIELD fires within 2 seconds. v0.14.103.3 stopped at "cycle 1 sweep aborts" and announced bounce-arrived, killing the retry pattern.

## v0.14.103.5 fix

Add a retry counter. Allow up to 3 sweep-abort cycles before declaring bounce-arrived.

- New constant: `DRIVE_BOUNCE_ABORT_THRESHOLD = 3` (~40 seconds of retries)
- New state: `s_sweepAbortCount` (file-scope static int, reset on `StartAutoDrive`)
- Sweep-abort hook in `UpdateAutoDrive`:
  - Increments counter
  - If counter >= threshold: announce bounce-arrived + StopAutoDrive (v0.14.103.3 behavior)
  - If counter < threshold: log retry N/M and reset sweep state to resume normal steering (v0.14.99 behavior)

For Balamb (car-friendly): up to 2 retry cycles, MODE_FIELD typically fires before threshold trip → normal arrival.
For Balamb Garden (non-car-friendly): 3 cycles → threshold trip → bounce-arrived announce.

## v0.14.103.5 BAT plan

Two tests this BAT:

**Test 1: AD to Balamb Town (car-friendly)**
- Rent car in Balamb area
- AD to Balamb Town
- Expected: 1-2 sweep-abort cycles with retry log, then `[DRIVE] Arrival via game-mode (mode=1 MODE_FIELD)` and normal "Arrived at Balamb Town" announce

**Test 2: AD to Balamb Garden (non-car-friendly)**
- From same save (or after returning to world map from Balamb)
- AD to Balamb Garden
- Expected: 3 sweep-abort cycles, then `[DRIVE-BOUNCE]` log + TTS "Arrived near Balamb Garden. You may need to enter on foot."

Upload `Logs/ff8_world.log`.

## Tuning notes

- If Test 1 fails (Balamb still bounce-arrives): threshold may need to be higher (5? 7?). BAT log would show the actual cycle count needed.
- If Test 2 fires before 3 cycles: probably fine if it announces correctly, just less patient.
- If neither test works: may need to differentiate car-friendly vs non-car-friendly via wmsetus Section 8 data instead of count-based heuristic.

## After successful BAT — v0.14.104 cleanup

1. Strip `[VEH-VERIFY]` diagnostic block from `StartAutoDrive`
2. Strip `s_vehVerifyFired` one-shot flag and Initialize reset
3. Remove dead per-frame bounce-detection block from `UpdateAutoDrive` (~line 3580)
4. Remove `s_carBounceFrames`, `s_carBouncePrevX/Y`, `s_carBounceLastTick` declarations
5. Remove `DRIVE_BOUNCE_FRAMES_THRESHOLD`, `DRIVE_BOUNCE_DELTA_THRESHOLD`, `DRIVE_BOUNCE_SAMPLE_INTERVAL_MS` constants
6. Remove bounce-state resets from `StopAutoDrive` and `Initialize`
7. KEEP `DRIVE_BOUNCE_ABORT_THRESHOLD` and `s_sweepAbortCount` (these are the live mechanism now)
8. Bump version to 0.14.104
9. Push consolidated v0.14.103 → v0.14.104 to GitHub

## v0.14.103 / .1 / .2 / .3 / .4 / .5 cumulative changes (for v0.14.104 commit)

- Forest avoidance for cars (3-state terrain classifier, IsSegmentTraversable car/forest gate)
- Bounce-arrived detection at sweep-abort site with 3-retry threshold
- WM_SAVEMAP_TO_DWORD_SCALE corrected from 4096 → 1 (BAT-empirically validated)
- New `GetWorldMapPosition_Active` helper (vehicle-aware position read; foot fallback unchanged)
- WORLDMAP struct addresses confirmed: savemap+0x125C, char_pos[6]/ragnarok_pos[6]/bgu_pos[6]/car_pos[6] at offsets 0x00/0x18/0x24/0x30, car_rent flag at +0x62
- Vehicle-agnostic announce wording: "Arrived near X. You may need to enter on foot."

## GitHub state

`main` HEAD = `423e58a1` (v0.14.102 pushed). Local v0.14.103/.1/.2/.3/.4/.5 in working tree.

## Persistent rules carried forward

- `## Claude Says` prefix on every response
- Filesystem MCP only for Windows project files; bash for Linux container
- Claude NEVER pushes — Aaron uses `Utilities/push_to_github.bat`
- F12 reserved for diagnostic builds only
- SET3 opcode hook permanently disabled
- BAT workflow: `Logs/build_latest.log` first, then domain log
- Session checkpoint rule: update DEVNOTES + NEXT_SESSION_PROMPT at every version bump and after every BAT
- Always check `Plan & Research Documents/` AND past conversations BEFORE proposing new logic
- Always call `github:list_commits` before quoting GitHub state
- Forward declarations required when static function is used before its definition
- Diagnostic dumps must fire when the data they're dumping is actually loaded
- Cross-check arithmetic against authoritative target addresses, not summary descriptions
- When BAT log seems to show absence of feature exercise, ASK Aaron rather than assuming
- Locomotion byte at 0x02040A5E does NOT reliably indicate rental car state
- Don't try to detect bouncing as frozen-position; it's oscillation. Use higher-level signals like 'sweep aborted on drift'.
- When making assumptions about engine internals, verify empirically before relying on them
- Search log file format strings using unique fragments rather than visible bracket prefixes
- Accessibility wording principle: prefer concrete action verbs over abstractions
- **NEW LESSON (v0.14.103.5): Don't replace a "loop forever" pathology with "give up immediately." The middle ground (retry counter with sane limit) preserves both the success case (engine eventually accepts entry) and the failure case (give up gracefully). Pre-existing infinite-loop behavior often has emergent value that's invisible until you remove it.**
