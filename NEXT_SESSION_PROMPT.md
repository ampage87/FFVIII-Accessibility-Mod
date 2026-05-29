# Next Session Prompt: Chapter 4 BAT CONFIRMED, push pending, Chapter 5 queued

## Greeting

Start every response with `## Claude Says`. Read `DEVNOTES.md` and THIS file before any work.

## Where we are at session open

**Mid-chapter, BAT confirmed, push pending.** Local tree = **v0.17.8.19.4** (Chapter 4, chase regression fix — chase-drive gateway pass-through). BAT-confirmed 2026-05-28 at 19:39 by Aaron: chase reached Lapin Beach with 0 catches, gateway pass-through override log fired correctly. NOT YET pushed. GitHub HEAD remains v0.17.8.18.1 (`10e776e5`, Chapter 3 Scan-on-allies).

**If Aaron has not yet pushed v0.17.8.19.4:** remind him that `Utilities/push_to_github.ps1` will squash the four-build .19.x chain into one Chapter 4 commit. The chain (.19.1 protected waypoints, .19.2 prune skip, .19.3 flag-set ordering, .19.4 gateway pass-through) represents iterative debugging — only .19.4 is the actual fix, but .19.1-.19.3 are kept as defensive infrastructure that produces a denser, more robust chase path. CHANGELOG.md has the full narrative; the push utility will check the version-vs-changelog match.

**If Aaron has already pushed:** verify with `github:list_commits` that the new commit is on top of `10e776e5`. Update DEVNOTES so the "GitHub HEAD" line reflects the new commit and Chapter 4 is logged under "Last chapter closed". Then move to Chapter 5.

## Chapter 4 BAT signals that fired (for historical reference)

In `Logs/ff8_field.log` from the 2026-05-28 19:39 BAT:

1. **`[funnel-prune] skipped for chase-drive (32 waypoints kept; ...)`** on `domt2_1` — confirms v0.17.8.19.2 + .19.3 (the prune-skip + flag ordering) are wired correctly. Path goes from 8 waypoints (pruned) to 32 (full funnel output).
2. **`[drive] chase-drive gateway pass-through: player=(...) gw=(-93,-3414) toGwLen=... -> steer overridden to (..., -371X) (300 units past gateway)`** — fires once `wp 30/32 reached`. `toGwLen` decreases monotonically (457 → 428 → 398 in the BAT log) as the party walks straight through.
3. **No `[CBF] PASS chase BATTLE` lines anywhere in the run** — chase completed without any battle escapes.
4. **Per-tick `[drive-vec]` at gateway approach** shows `corSteer` using the overridden value (`-135,-3711` at t=810 in BAT log), with `finalDelta=(-98.2,-691.0)` — strong sustained southward push, no UR oscillation.
5. **MAPJUMP to Lapin Beach** fires cleanly at end of chase.

## Chapter 4 lessons (carry-forward for future debugging)

Four-build chapter (.19.1 → .19.4) burned three BATs on a wrong diagnosis (prune theory) before the right one (gateway pass-through). Lessons:

1. **When a fix doesn't engage as expected, READ THE LOG before iterating.** v0.17.8.19.2 was the correct prune-skip implementation but my new "skipped for chase-drive" log line didn't fire in the BAT — that should have been the first clue. I assumed the fix was in the right code path; it was, but the gating flag was set too late (caught only in v0.17.8.19.3).
2. **When "fix" after "fix" leaves the BAT outcome IDENTICAL, the original diagnosis is probably wrong.** v0.17.8.19.1 caught at (-110,-3407). v0.17.8.19.2 caught at (-110,-3407). v0.17.8.19.3 caught at (-110,-3407). Three consecutive identical-coordinates catches should have triggered "the path-density theory is wrong, this is something downstream of pathing" much earlier than it did.
3. **Aaron's question "are we stopping right before [the gateway], or walking through it?" unlocked the chapter.** Trajectory analysis (per-tick `[drive-vec]` reading) revealed the oscillation pattern that pointed directly to the pass-through bug. When stuck on a wrong theory, ask the user for one targeted observational question and act on it.
4. **CHASE-DRIVE'S CHAIN-ADVANCE NEVER ADVANCES PAST THE LAST WAYPOINT.** The funnel's last waypoint sits at the gateway center. Without an explicit override, `steerX/steerY` aims AT the gateway, not THROUGH it. v0.17.8.19.4 gates this override on `s_chaseDriveActive && s_driveCrossLineActive && s_waypointIdx == s_waypointCount-1`. Any future change to how chase-drive computes its terminal target needs to preserve this property.
5. **The v0.15.9.2.15 "offset 300 units past line" logic in the gotCrossLine block IS NOT ENOUGH on its own.** It mutates `tx, tz, dx, dz` but those values are overwritten by the waypoint chain-advance block later in the same function. Both pieces are needed: the gotCrossLine block handles arrival detection (cross-product sign-flip), and the v0.17.8.19.4 pass-through handles the analog vector.

## Chapter 5 — SeeD rank + automatic salary announcement

Two related surfaces, one chapter. Both touch the SeeD-rank system. Original GitHub issue #27 was only about the R-key bug, but Aaron flagged a second related gap — the automatic salary event isn't announced either. Fixing both together keeps the rank-data investigation in one chapter.

### Surface 1 — R key reports "No SeeD rank yet"

The R-key shortcut always announces "No SeeD rank yet" regardless of the player's actual rank. The user's standing hypothesis (per userMemories carry-over): `FIELD_H_OFFSET = 0xF94` has wrong section size, so the rank-byte read either lands on the wrong byte or fails the validity check that drives the "no rank yet" fallback.

**Investigation steps:**
1. Read the current implementation of the R-key handler. It will be in one of the `*_tts*.inl` files or the dinput8 hotkey dispatch. Likely involves reading a savemap offset for the rank byte and a separate flag/section for "has-been-promoted".
2. Cross-check the offset against the savemap section table. Note: community deep-research savemap offsets are +0x14 too high (76-byte not 96-byte header); subtract 0x14 from any offset Aaron looks up.
3. If `FIELD_H_OFFSET` is wrong, find the correct one. The SeeD rank byte is well-known in the FF8 savemap layout; getting the right offset from a trusted source should resolve it.
4. Add an in-conversation deep-research prompt for Aaron to run on ChatGPT if local sources aren't enough; always include the savemap +0x14 correction caveat in such prompts.

### Surface 2 — automatic SeeD salary announcement

The game shows a text window when the player receives their SeeD salary (every N steps). The window displays:
- The current SeeD rank.
- The amount of gil received.
- An indication of whether the rank went up, down, or stayed the same (probably three different text templates).

We need an automatic TTS announcement when this window appears, similar to other automatic announcements the mod already handles (level-up, junction acquired, item received). The output should include all three pieces — rank, amount, and direction-of-change.

**Investigation steps:**
1. Identify which game subsystem renders the salary window. Likely the field text/dialog system (`opcode_mes` family) or the world map text system, since salary ticks happen during field traversal.
2. The salary tick is a step-counter event — `tkmoney`-related code in the disassembly is a likely starting point. Search `Game Files/disassembly/` via `project_knowledge_search` for "salary" / "seed_pay" / "tkmoney" / step-counter increments.
3. Capture the rank-up / rank-down / rank-same text templates from `kernel.bin` text or the field text pool. The three variants will need separate template strings to detect.
4. Hook the text-render path at the right point so we can read the rank/amount/direction without disrupting normal display. Don't try to read it from memory ahead of the render — pattern-match the rendered text or hook the render call itself (same approach as the victory screen text hook chain).

**Pre-investigation note (carry-forward from prior chapters):**
- Victory text pre-rendered into GPU command lists during mode 3→5→100→4 transition. Hook the text renderer, not memory addresses (Aaron has corrected this approach repeatedly across other chapters). The salary window may follow the same pattern.
- FF8 has three text modules: `asm_tkbtl.cpp` (battle), `asm_tkmenu.cpp` (menu), `asm_tklib.cpp` (shared). Salary is field-mode, so likely tklib or a field-specific module.

### Investigation order

Start with Surface 1 (R-key fix) because the rank-byte offset work directly informs Surface 2 — once we know where the rank actually lives in the savemap, the salary hook can read it for the announcement, and the rank-direction detection becomes a before/after delta around the salary event.

### BAT shape for Chapter 5

A multi-build chapter. Probable structure:
- First build: R-key offset fix, BAT confirms R announces real rank.
- Second build: add salary-window hook with a one-shot log diagnostic (no TTS yet), BAT captures the salary-text template strings and which render path it goes through.
- Third build: wire up the TTS announcement for all three variants (rank-up / rank-down / rank-same), BAT confirms each one is announced correctly.
- Fourth build (if needed): cleanup pass, then squash-push.

Counts as one chapter regardless of build count.

## Other backlog (lower priority, pick if Chapter 5 stalls)

### D. DEVNOTES_HISTORY.md trim (housekeeping)

`DEVNOTES.md` is over the 10KB soft limit. Items to migrate to `DEVNOTES_HISTORY.md`:
- v0.17.8.7 `cardgamemaster` narrative (long overdue per the current top-of-file note).
- Bug #10 (Hall 6 Xu mislabeled) chapter narrative — now pushed, the full chara.one chain + revert + clean fix history should move.
- Chapter 2 Laguna bundle narrative now that it's pushed and closed.
- Chapter 3 Scan-on-allies stays in DEVNOTES (most recent before Chapter 4), moves on the next trim.
- Chapter 4 chase-regression detail will move on the trim after that.

Pure cleanup. Single-edit pass, no BAT.

### E. Plan & Research Documents update (Dollet countdown doc)

Was mentioned in earlier NEXT_SESSION_PROMPT files; Aaron will know specifics.

### F. Optional follow-up — playable-cast monster_id mapping (very low priority)

We confirmed Squall = monster_id 0x00 in Chapter 3 BAT and the universal `+0xB3` lookup works for every slot, so a complete mapping isn't needed for correctness. Nice-to-have if we ever want a per-character override layer.

### G. Refactor queue (only if a file approaches 80KB)

Carry-over from userMemories: `chase_auto_pilot`, `field_dialog`, `field_archive_jsm`, `battle_tts_ewm`, `battle_tts_menu`. **NEW: `field_nav_autodrive.inl` joins the queue** — at 81,750 bytes (79.83 KB) after v0.17.8.19.4 trim, it's now the closest file to the 80 KB hard limit. The Chapter 4 push needed an emergency comment trim to fit under the CI guard. Any future change in `UpdateAutoDrive` will push it over. Split candidates: the CALIB phase 1/2 state machine (~150 lines, self-contained) into `field_nav_autodrive_calib.inl`; or the SetAnalogFromVector + StopAutoDrive helpers into `field_nav_autodrive_helpers.inl`. Current other status: `field_nav_pathfinding.inl`, `field_nav_directiondrive.inl`, etc. are comfortably under the ceiling.

### H. `deploy.bat` "Version: SINGLE-PRONGED" cosmetic regex regression

Cosmetic bug from v0.15.3 in the deploy script's version-extraction regex. Doesn't affect builds. Lowest priority.

## Removed from backlog

- **Bug #8 NAMES (FIELD entity catalog)** — Earlier docs listed this as a Chapter 2 follow-up. NOT an issue: the field entity catalog uses generic category labels (NPC, Event, Interaction, Exit, Gateway), not proper character names. There is no dream-aware naming to fix. Removed.
- **Chase chapter as a deferred item** — Closed as Chapter 4 (v0.17.8.19.4). The chase code itself (chase_auto_pilot, chase_detector, chase_battle_freeze, chase_kani_freeze, chase_ask_overlay, etc.) is fully implemented and recent; v0.17.8.19.4's gateway pass-through restores known-working behavior. No deferred sub-items remain.

## Session startup ritual reminder

Aaron may say "BAT" mid-conversation. That always means: read `Logs/build_latest.log` tail for the version + success status, then the relevant domain log (`ff8_field.log`, `ff8_battle.log`, `ff8_menu.log`, `ff8_world.log`, `ff8_dialog.log`) tail. Never assume a BAT result without reading the log.

If Aaron raises a new bug not in this list, open a new chapter on it — that's higher priority than the backlog.
