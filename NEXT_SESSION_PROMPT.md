# Next Session Prompt: v0.15.9.11.3.9 BAT passed — ready to push, then post-chase backlog

## Where we are

**Chase scene closed. v0.15.9.11.3.9 BAT'd successfully 2026-05-15.** Aaron: *"That worked well. I think we can stick with that solution."* The `TRIGGER_DELAY_MS = 0` fix closes the ASK pre-open race window: pressing confirm during Squall's chase-trigger line now lands on the ASK instead of advancing the MES to the chase-start opcode. NVDA's overlapping-speech sequencing was acceptable in practice; fallback Options A and B documented in the source comment remain available if any future regression resurfaces the UX concern.

**HEAD on GitHub = v0.15.9.11.3.6** (commit `30bc7469`). Local sits at v0.15.9.11.3.9 with four squashed builds (.11.3.7 ASK polish + log cleanup, .11.3.8 keyboard suppression extension, .11.3.9 ASK race fix) ready to push. Verify HEAD at session start with `github:list_commits` if you want to be certain nothing has changed.

## Immediate (if not already done): push v0.15.9.11.3.9

Aaron runs `Utilities/push_to_github.vbs`. The script auto-reads version + top CHANGELOG entry. Claude NEVER pushes.

If the push has already happened by the time you read this, just verify HEAD via `github:list_commits` and move on to the backlog.

## Reference: v0.15.9.11.3.7 / .8 / .9 work bundled into this push (all BAT-confirmed)

The v0.15.9.11.3.7 ASK polish and log diagnostic cleanup both passed their BAT. They are inherited unchanged by v0.15.9.11.3.8 (no further code changes; this build only extends the chase keyboard suppression on top). The summary below is kept for context if v0.15.9.11.3.8's BAT exposes anything from these areas.

Three edits to `src/chase_ask_overlay.cpp`:

- `kChaseChoices[]` reordered from `{Manual, Auto, Original}` to `{Auto, Manual, Original}` — most-to-least support.
- `kChaseChoicesDefaultCursor` stays at 1, now points to Auto (the new slot 1). Protects button-mashers: confirm-without-navigate commits MODE_AUTO.
- `ANSWER_AUTO` and `ANSWER_MANUAL` swap values (1 ↔ 2) to match the new positions. `ANSWER_ORIGINAL` stays at 3. `CommitChoice` switch body unchanged; each case still routes to the correct `ChaseDetector::MODE_*`.
- Original label rewritten from `"Original: vanilla chase, no mod help"` to `"Original: vanilla, robot keeps getting up to pursue"` — conveys the X-ATM092 rise-and-pursue mechanic.
- Stale top-of-file design comment updated to reflect the v0.15.9.9 prompt change and the new option order.

INI persistence stores mode by name (`"auto"` / `"manual"` / `"original"`), not by `ANSWER_*` slot, so existing save files are unaffected.

**BAT plan:** Aaron triggers the chase. ASK announces `"X-ATM092 is heading right for you. How do you want to run?"` then reads the three labels in order: Auto, Manual, Original. Default cursor on Auto. Three success paths to verify:

1. **Confirm without navigating** → commits MODE_AUTO, chase auto-pilots as in v0.15.9.11.3.6.
2. **Arrow down once, confirm** → commits MODE_MANUAL, Manual behavior unchanged.
3. **Arrow down twice, confirm** → commits MODE_ORIGINAL, vanilla chase plays out (new Original label is announced on the way to position 3).

Also bundled into this build: log diagnostic cleanup across four sources of v0.15.9.x-era instrumentation that have served their research purpose and were dominating the v0.15.9.11.3.6 BAT logs (8.30 MB total):

- **`src/world_map.cpp::BuildDistanceCatalog`** — `[DEFER] Position is (0,0)` log throttled to one line per defer cycle (was firing every frame while in field mode, producing 1.79 MB of `ff8_world.log`).
- **`src/chase_auto_pilot.cpp::LogChaseActiveDiagnostic`** — early-return retirement. v0.15.9.3 diagnostic for the v0.15.9.4/.5/.6 camera-orientation research; research is complete.
- **`src/chase_auto_pilot.cpp::LogBridgeDiagnostic`** — early-return retirement. v0.15.9.8.2 all-slots dump for identifying the kani entity; v0.15.9.8.3 shipped the kani-slot override. State-transition log lines in `UpdateBridgeDance` (EAST→WEST, WEST→EAST, TIMEOUT, leap STARTED) are unchanged.
- **`src/chase_auto_pilot.cpp` per-second `tick=60` logger** — idle-sample suppression. The disc00_07h post-chase FMV pinned the auto-pilot in ENGAGED state with party frozen at `pos=(-210,-1000)` for 74 seconds; the tick logger fired 74 identical lines. Now logs the first idle sample, suppresses subsequent identical samples via a `suppressLog` flag, and emits a `tick log RESUMED after N idle samples` line on first motion.
- **`src/nav_log.cpp::CoordSample`** — `(fieldName, triIdx)` debounce. The COORD record was the v0.15.9.3–v0.15.9.5 camera research log; no checked-in consumer reads `ff8_nav_data.log` (audit confirmed). On `dotown_3` the BAT recorded 19 alternating COORD lines for triangles 30 and 22 in one second from triangle-boundary flicker. Now skips duplicate consecutive `(fieldName, triIdx)` samples. Note: file is opened in append mode and accumulated 4.66 MB across sessions; Aaron can delete the existing file to start clean.

Verify in the BAT log tails that `ff8_world.log` is small, `ff8_field.log` is mostly ENGAGED/transition events (no `ChaseActiveDiag` or `BridgeDiag` lines, no run of identical `tick=60` lines during FMV), and `ff8_nav_data.log` no longer has boundary-flicker repeats.

ASK polish + log diagnostic cleanup BAT'd successfully — the chase ran clean through every field, the new option order announced correctly, the post-chase log size dropped substantially. Only failure surfaced was the doopen2a Ctrl leak that v0.15.9.11.3.8 addresses (see top of file). Cleanup work below is the post-chase backlog Aaron will return to once .11.3.8 BATs.

## After push: pick from the post-chase backlog

The backlog is in DEVNOTES.md. Reproduced here in rough priority order:

1. **Cleanup vestigial `WALK_REPRESS_PERIOD` state** in `field_nav_directiondrive.inl` — constants + counters still present from v0.15.9.7.x but unreferenced. Small, mechanical, zero risk. Good first-back-in task.
2. **`deploy.bat` "Version: SINGLE-PRONGED" regex** — cosmetic regression from v0.15.9.3. The deploy log prints the literal `Version: SINGLE-PRONGED` instead of the actual version string. Hunt the regex in `src/deploy.bat`.
3. **X-ATM092 battle-name fix** — battle TTS announces "X-ATM 6" instead of "X-ATM092" because the kernel.bin enemy-name decoder lacks mappings for FF8's stylized small-form digits. The v0.15.9.11.3.6 BAT confirmed in `ff8_battle.log`: TTS string is constructed as literal `"X-ATM?6?"`. Two fix paths: extend the character map with the missing byte mappings (right fix; needs ~30 min investigation of the encoding), or hardcoded enemy-ID→ASCII name table like the existing Blue Magic 0x92/0xAA pattern (band-aid). Affects any enemy whose name uses FF8's stylized characters, not just X-ATM092.
4. **Generalized countdown-timer hook** — the Dollet 30-min countdown is currently TTS'd via a chase-specific path. Generalize for future timers.
5. **Remove party members from field entity catalog** — Squall/Zell/Selphie/etc. appear in the field entity catalog as targetable entities, which they shouldn't be. Filter them out.

**Ask Aaron which one he wants to start with.** None are urgent, all are tidy-up. `WALK_REPRESS_PERIOD` cleanup is the fastest and lowest-risk if he wants a quick win. X-ATM092 is the most user-visible win.

### Deferred (don't pick from these without explicit Aaron direction)

- SeeD rank bug #27 (hypothesis: `FIELD_H_OFFSET = 0xF94` is wrong section size)
- Walk-and-talk dialog gap (hardcoded engine path)
- Refined-coord narrow-gate steering (#29)
- Fire Cavern entry (#28) + planner-fallback (#29)
- chase_diag::OnAskOpcodeFired snprintf bug
- `CHASE-AGENT FINAL SUMMARY` log regression (fix in DeactivateFreeze before clearing agent state)

## Hard constraints

- **Do NOT revert the AUTO `[CBF]` battle-suppressor cap to 0.** Aaron's 2026-05-13 directive: the fix was the input layer, not the band-aid. Cap stays `INT_MAX`. v0.15.9.11.3.6 BAT vindicates the call.
- **Filesystem MCP for all Windows project files.** Bash runs in a Linux container that can't reach the OneDrive mod directory.
- **Aaron pushes via `Utilities/push_to_github.vbs`.** Claude NEVER pushes.
- **NEVER re-enable SET3 opcode hook (0x1E)** — CI guard in `.github/workflows/safety-checks.yml`. Hangs infirmary scene.

## Session ritual reminder

Read `DEVNOTES.md` and this file at session start. Update both at every version bump AND after every BAT result. Every Claude response starts with `## Claude Says`.
