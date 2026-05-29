# Next Session Prompt: BAT the v0.17.8.20 autodrive refactor, then devnotes cleanup, then SeeD rank chapter

## Greeting

Start every response with `## Claude Says`. Read `DEVNOTES.md` and THIS file before any work.

## Where we are at session open

**The autodrive split refactor is IMPLEMENTED LOCALLY as v0.17.8.20, awaiting BAT.** `FF8OPC_VERSION` is bumped to `0.17.8.20` and the matching `## v0.17.8.20` CHANGELOG entry is on top, so the build is push-ready the moment BAT passes. **GitHub HEAD is still v0.17.8.19.4** (commit `3e3fcfa9`, Chapter 4) until BAT passes and Aaron pushes.

What the refactor did (zero behavior change): split `field_nav_autodrive.inl` (was 79.83 KB, ~170 bytes under the 80 KB hard cap) into three files:
- `field_nav_autodrive_helpers.inl` (12.46 KB) — `InjectKey`, `ReleaseAllDirections`, `SetHeldDirections`, `SetAnalogFromVector`, `StopAutoDrive`, lifted verbatim.
- `field_nav_autodrive_calib.inl` (7.60 KB) — the CALIB phase statics + new `RunCalibration()` holding the heading-calibration state machine that used to sit inline at the top of `UpdateAutoDrive`. Call site is now `if (RunCalibration()) return;`.
- `field_nav_autodrive.inl` (now 62.91 KB, ~17 KB headroom) — holds only `UpdateAutoDrive`.

Include order in `field_navigation.cpp`: helpers → calib → autodrive, immediately before the old autodrive include position (so the call graph resolves top-down). Only structural change in the extracted code: phase 2's `if` became an `else if` so both phases fall to one trailing `return true;`. The v0.15.9.11.3.4 `InjectKey` comment and the v0.17.8.19.4 gateway pass-through block are untouched.

**Three tasks queued, in Aaron's stated order:**

1. **FIRST: BAT-triage the v0.17.8.20 refactor** (build + verify zero behavior change), then push if it passes.
2. **THEN: DEVNOTES cleanup** (migrate closed-chapter narratives to `DEVNOTES_HISTORY.md`).
3. **THEN: Chapter 5 — SeeD rank + automatic salary announcement.**

---

## Task 1: BAT-triage the v0.17.8.20 autodrive refactor

### Build + BAT shape

Single build, single BAT. After Aaron builds:
1. Verify the build succeeds and reports v0.17.8.20 (`Logs/build_latest.log` tail). A refactor that compiles is most of the battle — the risk surface is include-order / missing-static, both of which fail at compile time, not silently.
2. Aaron runs **F9 path-finding on any field** (calibration runs at drive start). Confirm `[CALIB] phase 1 done: ...` and `[CALIB] phase 2 done: ...` still fire in `ff8_field.log` — proves `RunCalibration()` is wired and the phase machine still advances and returns correctly.
3. Aaron runs the **X-ATM092 chase auto-pilot from before the Lapin Beach FMV**. Confirm `[chase-drive] STARTED ...`, `[funnel-prune] skipped for chase-drive ...`, and `[drive] chase-drive gateway pass-through ...` all fire, and the chase completes with **0 catches**. This exercises the helpers (`InjectKey` via the synthetic-buffer path, `SetHeldDirections`, `SetAnalogFromVector`, `StopAutoDrive`) and the v0.17.8.19.4 gateway block together.

### If BAT passes

Mark v0.17.8.20 **✅** in DEVNOTES (Size status line), then Aaron pushes via `Utilities/push_to_github.ps1` (single commit; Claude never pushes). After the push, verify with `github:list_commits` and update DEVNOTES + this file to the new HEAD. Then move to Task 2.

### If BAT fails

Most likely failure modes and where to look:
- **Compile error: undefined `RunCalibration` / helper** → include order in `field_navigation.cpp`. Helpers must precede calib must precede autodrive. Check the include block added after `static int s_deadClusterCount = 0;`.
- **Compile error: undefined static** → a calib/helper static is declared in `field_navigation.cpp` above the include block; confirm none was accidentally swept into the deletion. The calib statics (`s_calibPhase`, `s_calibTicks`, `s_calibStartX/Y`, `CALIB_SETTLE_TICKS`, `CALIB_MEASURE_TICKS`, `s_calibPending`) now live in `field_nav_autodrive_calib.inl`; everything else (`s_drive*`, `s_analog*`, `s_cam*`, `s_driveCam*`, `s_chaseDrive*`) stays in `field_navigation.cpp`.
- **CALIB log lines stop firing** → the `if (RunCalibration()) return;` call site, or the phase-1→phase-2 `else if` transition. Compare against the CHANGELOG note: true = tick consumed (caller returns), false = idle/done (fall through).
- **Chase regresses (catches)** → unlikely from a pure move, but if it happens, diff `field_nav_autodrive_helpers.inl` against the Chapter 4 close narrative — the v0.15.9.11.3.4 `InjectKey` synthetic-buffer path is the sensitive bit.

Triage, fix, re-BAT. Don't expand scope.

---

## Task 2: DEVNOTES cleanup (housekeeping, no BAT)

`DEVNOTES.md` is over the 10 KB soft limit. Migrate these closed-chapter narratives to `DEVNOTES_HISTORY.md`, leaving a one-line pointer in DEVNOTES for each:
- v0.17.8.7 `cardgamemaster` narrative (long overdue per the top-of-file note).
- Bug #10 (Hall 6 Xu mislabeled) chapter narrative — pushed; the full chara.one chain + revert + clean fix history should move.
- Chapter 2 Laguna bundle narrative — pushed and closed.
- Keep Chapter 3 (Scan-on-allies) and Chapter 4 (chase regression) in DEVNOTES for now — they're the two most recent; they migrate on the trim after the next one.

Pure cleanup, single-edit pass, no BAT. (Was backlog item D; Aaron elevated it to a task this session.)

---

## Task 3: Chapter 5 — SeeD rank + automatic salary announcement

Two related surfaces, one chapter. Both touch the SeeD-rank system. Original GitHub issue #27 was only about the R-key bug, but Aaron flagged a second related gap — the automatic salary event isn't announced either. Fixing both together keeps the rank-data investigation in one chapter.

### Surface 1 — R key reports "No SeeD rank yet"

The R-key shortcut always announces "No SeeD rank yet" regardless of the player's actual rank. The user's standing hypothesis: `FIELD_H_OFFSET = 0xF94` has wrong section size, so the rank-byte read either lands on the wrong byte or fails the validity check that drives the "no rank yet" fallback.

**Investigation steps:**
1. Read the current implementation of the R-key handler. It will be in one of the `*_tts*.inl` files or the dinput8 hotkey dispatch. Likely involves reading a savemap offset for the rank byte and a separate flag/section for "has-been-promoted".
2. Cross-check the offset against the savemap section table. Note: community deep-research savemap offsets are +0x14 too high (76-byte not 96-byte header); subtract 0x14 from any offset Aaron looks up.
3. If `FIELD_H_OFFSET` is wrong, find the correct one. The SeeD rank byte is well-known in the FF8 savemap layout; getting the right offset from a trusted source should resolve it.
4. Add an in-conversation deep-research prompt for Aaron to run if local sources aren't enough; always include the savemap +0x14 correction caveat in such prompts.

### Surface 2 — automatic SeeD salary announcement

The game shows a text window when the player receives their SeeD salary (every N steps). The window displays the current SeeD rank, the amount of gil received, and an indication of whether the rank went up, down, or stayed the same (probably three different text templates). We need an automatic TTS announcement when this window appears, similar to other automatic announcements the mod already handles (level-up, junction acquired, item received). The output should include all three pieces — rank, amount, and direction-of-change.

**Investigation steps:**
1. Identify which game subsystem renders the salary window. Likely the field text/dialog system (`opcode_mes` family) or the world map text system, since salary ticks happen during field traversal.
2. The salary tick is a step-counter event — `tkmoney`-related code in the disassembly is a likely starting point. Search `Game Files/disassembly/` for "salary" / "seed_pay" / "tkmoney" / step-counter increments.
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

---

## Other backlog (lower priority, pick if Tasks 1–3 stall)

### E. Plan & Research Documents update (Dollet countdown doc)

Was mentioned in earlier NEXT_SESSION_PROMPT files; Aaron will know specifics.

### F. Optional follow-up — playable-cast monster_id mapping (very low priority)

We confirmed Squall = monster_id 0x00 in Chapter 3 BAT and the universal `+0xB3` lookup works for every slot, so a complete mapping isn't needed for correctness. Nice-to-have if we ever want a per-character override layer.

### G. Refactor queue (post-autodrive)

With `field_nav_autodrive.inl` now split, the refactor queue still has (per carry-over): `chase_auto_pilot`, `field_dialog`, `field_archive_jsm`, `battle_tts_ewm`, `battle_tts_menu`. None are currently over the soft limit; revisit only when one approaches 60 KB. Current warning-zone files (60–80 KB, no action needed yet): `battle_tts_victory.inl` 77.08, `field_archive_jsm_scan.inl` 75.05, `field_nav_catalog.inl` 74.41, `ff8_addresses.cpp` 73.35, `scan_tts.cpp` 72.14, `field_nav_fieldscripts.inl` 70.54, `field_navigation.cpp` 70.39.

### H. `deploy.bat` "Version: SINGLE-PRONGED" cosmetic regex regression

Cosmetic bug from v0.15.3 in the deploy script's version-extraction regex. Doesn't affect builds. Lowest priority.

## Removed from backlog

- **Bug #8 NAMES (FIELD entity catalog)** — the field entity catalog uses generic category labels (NPC, Event, Interaction, Exit, Gateway), not proper character names. No dream-aware naming to fix. Removed.
- **Chase chapter as a deferred item** — Closed as Chapter 4 (v0.17.8.19.4, commit `3e3fcfa9`). No deferred sub-items remain.

## Session startup ritual reminder

Aaron may say "BAT" mid-conversation. That always means: read `Logs/build_latest.log` tail for the version + success status, then the relevant domain log (`ff8_field.log`, `ff8_battle.log`, `ff8_menu.log`, `ff8_world.log`, `ff8_dialog.log`) tail. Never assume a BAT result without reading the log.

If Aaron raises a new bug not in this list, open a new chapter on it — that's higher priority than the backlog.

## Push-flow reminders (from Chapter 4 hiccup)

- Local CI mirror runs at push time and enforces the 80 KB hard limit on every `.inl` and `.cpp` source file. Watch zone is 60–80 KB (warning, doesn't block). Hard fail at 80 KB.
- If a push refuses for size, the fastest fix is a comment trim. The proper fix is a split refactor (the v0.17.8.20 work above is the model).
- `FF8OPC_VERSION` in `src/ff8_accessibility.h` must match the top `## vX.Y.Z` heading in `CHANGELOG.md` or the push utility refuses.
- Aaron pushes via `Utilities/push_to_github.ps1`. Claude never pushes. After a successful push, verify with `github:list_commits` and update DEVNOTES + this file to reflect the new HEAD.
