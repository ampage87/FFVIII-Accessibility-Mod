# Next Session Prompt: v0.16.1.4 doopen2a threshold revision, awaiting BAT

## Greeting

Start with `## Claude Says` per session ritual. Read `DEVNOTES.md` and THIS file before any work.

## Where we are

**GitHub HEAD = v0.16.0.3** (commit `1e3d7fd5`, pushed 2026-05-16 22:09:49 UTC). **Local tree = v0.16.1.4** on top of v0.16.1.3 (staged SE->S with too-loose threshold), v0.16.1.2 (funnel COLLAPSE), v0.16.1.1 (diagnostics), v0.16.1 (chase split). All five unpushed.

### What v0.16.1.4 changes

Two-line change in `src/chase_auto_pilot_route.inl`: the Y-threshold on stage 0 of `kStages_doopen2a[]` goes from `-1500` to `-631`. Plus an extensively rewritten comment block above the table explaining the derivation from Aaron's manual chase BAT trace, and a similar rewrite of the kFieldConfigs[] doopen2a entry comment.

The MODE_STAGED_DIRECTION mechanism itself is unchanged from v0.16.1.3. Only the SE-phase duration shrinks.

### Why this threshold and not something else

v0.16.1.3 BAT (2026-05-16 20:38:38) reached BATTLE in 1 second on doopen2a with auto-pilot at position `(-446, -821)`. Distance from there to battleyarou's JSM-init position `(0, -744)` is 453 units, inside the TALKRAD=500 catch zone. The v0.16.1.3 threshold of `Y<-1500` required ~1.94 seconds of SE motion to reach the (supposedly) safe `X >= -185` boundary -- but that trajectory walks the party THROUGH battleyarou's static catch zone.

Aaron then ran a successful manual chase BAT (2026-05-16 21:10:38-21:10:47). 9 seconds total, 0 catches. The `ff8_nav_data.log` COORD trace captured every triangle-change waypoint:

```
t=0       (-974, -166)   spawn          tri 52
t=0+      (-856, -450)                  tri 51
t=1       (-783, -669)                  tri 49
t=1.5     (-629, -891)   MAX EAST       tri 46
t=2       (-662, -1351)                 tri 97
...
t=4       (-1068, -3542) EXIT TRIGGER   tri 151
```

Shape: SE briefly (max east X=-629), then SOUTH along the western corridor. Aaron's closest approach to battleyarou's static zone was 646 units at the max-east excursion -- 146 units of margin from TALKRAD=500.

The new threshold `Y<-631` ends stage 0 at approximately `(-629, -631)` -- same X as Aaron's max-east excursion, 639 units from battleyarou (139-unit margin). At the auto-pilot's measured SE rate of 528 east/sec and 716 south/sec, that takes ~0.65 seconds. Then stage 1 (pure south) drifts west naturally at -76/sec from camDown vector `(-0.097, -0.995)`, walking the party along the western corridor to the SW screen-boundary trigger at approximately `(-905, -3447)` after ~3.6 more seconds. Total field time: ~4.25 seconds.

Aaron's manual BAT also revealed that **battleyarou's TALKRAD expands from 500 to 700 at the 7-second mark** (`[TALKRAD] CHANGED @0x1F8: 500 -> 700` at 21:10:45 with context bytes shifting `@21E 0->2 @244 0->3`). The chase mechanic is "outrun an expanding catch radius", not "avoid a fixed circle." The v0.16.1.4 ~4.25s field time beats the 7s expansion comfortably.

### Critical correction baked into the rationale

**KANI HAS NO ACTIVE PROXIMITY CATCH ON DOOPEN2A.** Aaron's manual run passed within 162 units of kani at `(-685, -2284)` (at position `(-807, -2391)`) and was not caught. The v0.16.1.3 and earlier commentary that attributed v0.16.1.2's t=3 catch to kani was wrong; that catch's caller was always `entityPtr=0x0188CA04` (battleyarou) in the `[CBF] PASS` log.

### Unexplained but bypassed

v0.16.1.2's t=3 catch with party at `(-870, -1900)` (1447 units from battleyarou's static position) is still unexplained. The catch was fired by battleyarou's script from 1447 units away. Aaron walks through similar positions in his manual run without trigger. Hypothesis: battleyarou has a velocity- or motion-vector-based catch in addition to the static proximity zone, and the auto-pilot's faster speed or different motion vector trips it. The v0.16.1.4 route empirically avoids it by matching the shape of Aaron's path -- east-first to ~X=-629, then west-corridor south. Whatever the mechanism, this shape works.

## Reading the BAT log

### Scenario A -- Chase clears cleanly (expected)

Look for:

- doopen2a's per-tick `ChaseAutoPilot: tick=60 ...` line shows `mode=STAGED dir=(1,1)` initially (stage 0 = SE).
- After ~0.5-0.8 seconds the per-tick log shows `dir=(0,1)` (stage 1 = S). Stage 0 is short now, so the transition fires fast.
- Party `pos.X` should increase from ~-974 at engage toward ~-629 during stage 0, then drift slowly west during stage 1 (toward `-905` or so by exit).
- Party `pos.Y` should drop from `-166` at engage past `-631` (stage transition) and continue down to `-3414` or beyond (south exit).
- `bydist` (party-to-origin distance) starts around 988 (sqrt(974^2 + 166^2)) and grows as party moves south. `bydist` proxy for battleyarou distance isn't meaningful (battleyarou is UNUSE'd, tracked at (0,0)), but the per-tick log will still print it.
- Predicted battleyarou static-zone distance: starts at 1133, drops to 639 at stage transition, then grows again. Never below 500.
- No `[CBF] PASS` line on doopen2a.
- A clean `fieldId changed to 0x0158` transition to dotown_3 at ~4-5 seconds after engage.

If this happens: chase chapter reopens AND closes. Aaron pushes v0.16.1+v0.16.1.1+v0.16.1.2+v0.16.1.3+v0.16.1.4 as a single git push via `Utilities/push_to_github.ps1`. The commit body will be the v0.16.1.4 CHANGELOG entry.

### Scenario B -- Chase still catches on doopen2a

Possible sub-scenarios:

**B.1: Per-tick log shows `dir=(1,1)` (stage 0) when catch fires.**
Stage 0 is too long and we're back inside battleyarou's zone. Possible if the empirical SE rate differs from the v0.16.1.3 BAT measurement (528 east/sec, 716 south/sec). If the party reaches `Y=-631` with `X` significantly less than `-629` (more west, like `X<-700`), the threshold isn't catching us where expected. Try a higher (less negative) threshold like `-500` or `-450` to end SE earlier.

**B.2: Per-tick log shows `dir=(0,1)` (stage 1) and catch fires somewhere in the south leg.**
Stage 1 trajectory clips into something we missed. Check `pos` at catch tick. If `X > -500` at catch, we're still in battleyarou's static zone (probably the SE phase ended too east). If `X < -1100` and catch fires at `Y` around `-2284` (kani's row), the kani-inert finding was wrong -- but Aaron's manual data argues strongly against this. Re-check the entityPtr in the `[CBF] PASS` line; if it's not battleyarou's `0x0188CA04`, it's a different catch source entirely.

**B.3: Party reaches `(-905, -3447)` or thereabouts but chase doesn't end.**
The SW screen-boundary trigger is in a different location than Aaron's exit. Need to dump SETLINE entries from the doopen2a SCRIPT-DUMP in `Logs/ff8_field.log` to find the actual line endpoints. The south SETLINE `idx=257 center=(-952, -3703)` was the v0.15.9.8 target, but Aaron exited at `(-1068, -3542)` which is north of that center -- so the line must span up to at least `-3542` in Y. Endpoints needed.

**B.4: Party gets stuck somewhere in stage 0 or stage 1.**
Walkmesh issue. Check for `velocity-stuck` or `LINEOFF` events around the stuck position. Stage 0 hits a wall to the east (unlikely from spawn but possible if the immediate east is blocked).

### Scenario C -- Build error

Read `Logs/build_latest.log` tail first with filesystem MCP (not bash).

Likely failure modes for v0.16.1.4:

- **None expected.** The only code change is a single integer constant (`-1500` -> `-631`) in `kStages_doopen2a[]`. The surrounding comment block is the main edit but comments don't affect the build.
- **CHANGELOG version mismatch** -- both should be at v0.16.1.4 in `src/ff8_accessibility.h` and the top heading in `CHANGELOG.md`. Verify with grep before pushing.

### Scenario D -- Some other field regresses

Less likely (we only changed doopen2a's stage 0 threshold), but worth a quick check on the other chase fields (domt4_1, domt3_2, domt5_1, domt2_1, domt1_1) in the BAT log. The doopen2a change is isolated.

## After v0.16.1.4 lands

Once the chase clears and Aaron pushes:

1. Update DEVNOTES + this file: `GitHub HEAD = v0.16.1.4`, `commit = <new-hash>`, `Local HEAD matches GitHub`.
2. Resume the size-split queue:
   - **v0.16.2**: split `src/field_dialog.cpp` (88 KB).
   - **v0.16.3**: split `src/field_archive_jsm.inl` (91 KB).
   - **v0.16.4**: split `src/battle_tts_ewm.inl` (90 KB).
   - **v0.16.5**: split `src/battle_tts_menu.inl` (82 KB).

## Hard constraints (unchanged)

- **Filesystem MCP for all Windows project files.** Bash runs in a Linux container that can't reach the OneDrive mod directory.
- **Aaron pushes via `Utilities/push_to_github.ps1`**, Claude NEVER pushes.
- **NEVER re-enable SET3 opcode hook (0x1E)** -- CI guard.
- **F-key handlers gated** on `!(GetAsyncKeyState(VK_MENU) & 0x8000)`.
- **F12 reserved** for per-session diagnostics.
- **Source file size limits**: 60 KB warn, 80 KB fail (CI enforced).
- **OneDrive sync EPERM**: retry immediately on first edit attempt.
- **AUTO `[CBF]` battle-suppressor cap stays `INT_MAX`** -- Aaron's 2026-05-13 directive.
- **`.inl` files are TEXTUAL INCLUDES**: no header guards, no namespace declarations inside, `state.inl` always first.
- Every Claude response starts with `## Claude Says`.

## Key lessons from this session

1. **`ff8_nav_data.log` is the silent goldmine for chase debugging.** It logs every triangle change with `[timestamp] COORD field tri X Y ...` regardless of auto-pilot state -- including manual runs. The `ChaseAutoPilot` per-tick log in `ff8_field.log` only fires when the auto-pilot is engaged, but `ff8_nav_data.log` always logs movement. Use it whenever Aaron's manual play is the ground truth.
2. **Aaron's domain knowledge is ground truth, but his recipes need empirical verification.** The "SE several steps then S to the exit gateway" recipe pointed in the right direction but with the wrong magnitudes. Aaron's max-east excursion at X=-629 -- much less east than the v0.16.1.3 derivation expected -- only became visible from the position trace.
3. **Multiple catch sources on one field may not all be active.** doopen2a's JSMScan listed both kani and battleyarou as catch entities; only battleyarou is actually active. The TALKRAD log only fires for battleyarou's entity pointer, which was a missed clue.
4. **Per-field problems require per-field analysis.** The robot's position resets at every field boundary. doopen2a is its own self-contained proximity-catch problem. The "save time on earlier fields" model was completely wrong.

## Quick reference -- what to check first when this session opens

- [ ] Read `DEVNOTES.md` (current state + backlog).
- [ ] Read this `NEXT_SESSION_PROMPT.md` (you're already here).
- [ ] If Aaron says "BAT": read `Logs/build_latest.log` tail first. Then `Logs/ff8_nav_data.log` tail for the doopen2a COORD trace and `Logs/ff8_field.log` grep for the `ChaseAutoPilot` per-tick lines. Walk Scenario A/B/C above.
- [ ] If the chase clears: confirm with Aaron and walk through the push process.
- [ ] If something else regresses: walk Scenario D.
