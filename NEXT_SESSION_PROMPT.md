# Next Session Prompt: SeeD rank chapter (Chapter 5)

## Greeting

Start every response with `## Claude Says`. Read `DEVNOTES.md` and THIS file before any work.

## Where we are at session open

**Autodrive split refactor CLOSED + PUSHED.** GitHub HEAD = **v0.17.8.20** (commit `4bd5b86d`, pushed 2026-05-29 03:52 UTC). Parent is `3e3fcfa9` (v0.17.8.19.4, Chapter 4). `field_nav_autodrive.inl` was split into three files (helpers + calib + the trimmed autodrive), dropping it from 79.83 KB to 62.91 KB; zero behavior change, BAT-confirmed 2026-05-28. Local tree matches GitHub; nothing pending.

**DEVNOTES cleanup DONE (2026-05-29, no BAT).** `DEVNOTES.md` trimmed 36.58 KB → 25.01 KB. Migrated to `DEVNOTES_HISTORY.md` (newest-first, as proper `## vX.Y.Z` chapters): Chapter 2 (Laguna bundle + dream-identity reference), bug #10 (Xu), v0.17.8.7 (cardgamemaster), Chapter 1 (Fire Cavern FMV), and the bghall_1 save-point narrative. DEVNOTES keeps one-line pointers for each, plus the reusable bug-#10 entity-catalog learnings inline. The two fully-closed bug ledgers (gwgrass1 #7–#10, Fire Cavern #1–#8) were condensed to a short archival block. Chapters 3/4 and the v0.17.8.20 note were kept in DEVNOTES per the original plan. Still over the 10 KB soft limit, but the remainder is live operational content (intro, active backlog, fieldId catalog, session ritual & rules) — further trimming would touch in-use material, so it was left for Aaron's call.

BAT residual carried forward (low risk, no action required): the CALIB phase-1/2 *active* body in `field_nav_autodrive_calib.inl::RunCalibration()` is a verbatim move and was not directly logged during the v0.17.8.20 BAT, because F9 path-finding uses ca-quantized axes and skips calibration, and the chase that ran was direction-mode. It only executes on a waypoint chase-drive field (domt2_1-style). Next time such a field comes up in normal play, glance at `ff8_field.log` for `[CALIB] phase 1 done` / `phase 2 done` as a free confirmation. The call site and idle/fall-through path are already production-proven.

**Next task: Chapter 5 — SeeD rank + automatic salary announcement.** Surface 1
(R-key fix) is IMPLEMENTED locally as **v0.17.9.0**, awaiting BAT (not yet pushed).
Surface 2 (auto salary announcement) is still open. Details below.

### Chapter 5 confirmed finding (reuse for Surface 2)

**SeeD points (SeeD experience) live at savemap +0x0D6C (uint16). Rank = points /
100.** Confirmed by diffing three of Aaron's decompressed .ff8 saves:
- `.ff8` files are FF7/FF8 LZSS-compressed (4-byte LE size header, then the LZSS
  stream; N=4096 F=18 THRESHOLD=2 init-pos 0xFEE zero-filled buffer). Decompressed
  output carries the original-PC slot layout. **Live savemap offset X ==
  decompressed-file offset 0x184 + X** (anchored on Squall HP/EXP + Gil + location).
- pre-SeeD save: points = 500 (documented initial-rank-formula base, pre-grading).
  Rank-3 save: 392 (392/100 = 3). Same save post one salary: 383 (-9 = lose 10 per
  pay + 1 kill). Salary observed 7400->8900 = 1500 = Rank 3, matching 392/100.
- Rejected decoy: +0x0D62 reads 3 but is the high word of the u32 total-step
  counter at +0x0D60, not a rank field. The OLD broken read was +0xF94+0x08
  (= +0xF9C); that region is dead zeros in every save (root cause of issue #27).
- Salary-relevant neighbours found in the same diff (for Surface 2 delta logic):
  salary-payment count at +0x0CDE (incremented 14->15 across one pay; 0 pre-SeeD),
  steps-since-pay at +0x0D64 (24405->161, wraps ~24575), gameplay Gil at +0x0B08.
- Known edge: pre-Dollet the base 500 makes R announce a provisional "Rank 5"
  rather than "No SeeD rank yet" (no separately-confirmed SeeD-membership flag
  yet). Brief window; add a membership-flag diagnostic only if Aaron wants strict
  pre-promotion gating.

---

## Task: Chapter 5 — SeeD rank + automatic salary announcement

Two related surfaces, one chapter. Both touch the SeeD-rank system. Original GitHub issue #27 was only about the R-key bug, but Aaron flagged a second related gap — the automatic salary event isn't announced either. Fixing both together keeps the rank-data investigation in one chapter.

### Surface 1 — R key reports "No SeeD rank yet"  [BAT-CONFIRMED v0.17.9.0.1, F12 diag removed in v0.17.9.0.2 — push-clean]

Fixed: `AnnounceSeedRank()` in `menu_tts_hotkeys.inl` reads SeeD points at
+0x0D6C (uint16) and announces rank = points/100 ("SeeD Rank N"; "SeeD Rank A"
for 3100+). The old +0xF9C read (dead zeros) is gone. **Pre-SeeD gate
(v0.17.9.0.1):** announces "No SeeD rank yet" when `points == 500 &&
salaryCount == 0` (salaryCount = uint16 at +0x0CDE) — pre-Dollet the pool sits at
the base 500 with no salary paid (pre-promotion modifiers are deferred to
graduation), so it stays exactly 500; a paid SeeD at exactly 500 still announces
Rank 5 (salaryCount > 0). A **LOCAL-ONLY F12 diagnostic** (dinput8.cpp,
!alt-gated) dumps `[SEEDDIAG]` to ff8_mod.log to validate/refine the gate —
**must be removed before the chapter is pushed** (F12-reserved rule). See the
"Chapter 5 confirmed finding" block above for the offset derivation.

BAT: (1) R on a SeeD save → correct "SeeD Rank N" (cross-check vs the salary
table); (2) R on the pre-SeeD save → "No SeeD rank yet"; (3) F12 on a pre-SeeD
save and a SeeD save → send both `[SEEDDIAG]` blocks from ff8_mod.log.

The original hypothesis below proved correct in spirit (offset wrong) — kept for
reference until the chapter closes.

The R-key shortcut always announced "No SeeD rank yet" regardless of the player's actual rank. The user's standing hypothesis: `FIELD_H_OFFSET = 0xF94` has wrong section size, so the rank-byte read either lands on the wrong byte or fails the validity check that drives the "no rank yet" fallback.

**Investigation steps:**
1. Read the current implementation of the R-key handler. It will be in one of the `*_tts*.inl` files or the dinput8 hotkey dispatch. Likely involves reading a savemap offset for the rank byte and a separate flag/section for "has-been-promoted".
2. Cross-check the offset against the savemap section table. Note: community deep-research savemap offsets are +0x14 too high (76-byte not 96-byte header); subtract 0x14 from any offset Aaron looks up.
3. If `FIELD_H_OFFSET` is wrong, find the correct one. The SeeD rank byte is well-known in the FF8 savemap layout; getting the right offset from a trusted source should resolve it.
4. Add an in-conversation deep-research prompt for Aaron to run if local sources aren't enough; always include the savemap +0x14 correction caveat in such prompts.

### Surface 2 — automatic SeeD salary announcement  [COMPLETE + BAT-CONFIRMED v0.17.9.1, push-ready]

**Status (v0.17.9.1):** done and confirmed. The v0.17.9.0.3 change-logger BAT pinned the real-time signature — at the salary chime, in ONE frame: gil (+0x0B08) jumped 7400->8900 (+1500 = rank-3 table), points (+0x0D6C) dropped 392->383, steps-since-pay (+0x0D64) reset 24575->0, while the counter (+0x0CDE) stayed 14 through close (it lags to the next save; that's why the file diff showed 14->15). `PollSeedSalary()` (dinput8.cpp) fires on that signature — steps dropped >10000 (reset) + gil up + points dropped 0..100 (excludes save loads & battle gil) — and speaks rank + amount + direction: "SeeD salary. Rank N. X gil." / "Promoted to Rank N. X gil." / "Dropped to Rank N. X gil." (rank 31 = "Rank A"). Rank = points/100 (after), amount = gil delta (ground truth), direction = rank after vs before; one `[SALARY] PAY:` line logged per payment. **BAT-confirmed:** a world-map salary spoke "SeeD salary. Rank 3. 1500 gil.". The verification `[SALARYDIAG] HB` heartbeat was stripped in v0.17.9.1 (push-ready). Promoted/dropped wordings share the same proven code path and will surface in normal play — not separately tested (can't easily force a rank change). **To push: one final BAT (build + a salary still announces), then `Utilities/push_to_github.ps1`** — this single commit carries the whole chapter (Surface 1 + Surface 2); confirm with `github:list_commits` and update HEAD here + DEVNOTES after.

The original investigation plan below is kept for reference.

### Surface 2 — automatic SeeD salary announcement (original plan)

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

## Other backlog (lower priority, pick if Chapter 5 stalls)

### E. Plan & Research Documents update (Dollet countdown doc)

Was mentioned in earlier NEXT_SESSION_PROMPT files; Aaron will know specifics.

### F. Optional follow-up — playable-cast monster_id mapping (very low priority)

We confirmed Squall = monster_id 0x00 in Chapter 3 BAT and the universal `+0xB3` lookup works for every slot, so a complete mapping isn't needed for correctness. Nice-to-have if we ever want a per-character override layer.

### G. Refactor queue (post-autodrive)

With `field_nav_autodrive.inl` now split (v0.17.8.20), the refactor queue still has (per carry-over): `chase_auto_pilot`, `field_dialog`, `field_archive_jsm`, `battle_tts_ewm`, `battle_tts_menu`. None are currently over the soft limit; revisit only when one approaches 60 KB. Current warning-zone files (60–80 KB, no action needed yet): `battle_tts_victory.inl` 77.08, `field_archive_jsm_scan.inl` 75.05, `field_nav_catalog.inl` 74.41, `ff8_addresses.cpp` 73.35, `scan_tts.cpp` 72.14, `field_nav_fieldscripts.inl` 70.54, `field_navigation.cpp` 70.39.

### H. `deploy.bat` "Version: SINGLE-PRONGED" cosmetic regex regression

Cosmetic bug from v0.15.3 in the deploy script's version-extraction regex. Doesn't affect builds. Lowest priority.

## Removed from backlog

- **DEVNOTES cleanup** — DONE 2026-05-29 (see "Where we are at session open"). Was Task 1.
- **Bug #8 NAMES (FIELD entity catalog)** — the field entity catalog uses generic category labels (NPC, Event, Interaction, Exit, Gateway), not proper character names. No dream-aware naming to fix. Removed.
- **Chase chapter as a deferred item** — Closed as Chapter 4 (v0.17.8.19.4, commit `3e3fcfa9`). No deferred sub-items remain.
- **Autodrive split refactor** — Closed + pushed v0.17.8.20 (commit `4bd5b86d`).

## Session startup ritual reminder

Aaron may say "BAT" mid-conversation. That always means: read `Logs/build_latest.log` tail for the version + success status, then the relevant domain log (`ff8_field.log`, `ff8_battle.log`, `ff8_menu.log`, `ff8_world.log`, `ff8_dialog.log`) tail. Never assume a BAT result without reading the log.

If Aaron raises a new bug not in this list, open a new chapter on it — that's higher priority than the backlog.

## Push-flow reminders

- Local CI mirror runs at push time and enforces the 80 KB hard limit on every `.inl` and `.cpp` source file. Watch zone is 60–80 KB (warning, doesn't block). Hard fail at 80 KB.
- If a push refuses for size, the fastest fix is a comment trim. The proper fix is a split refactor (the v0.17.8.20 autodrive work is the model).
- `FF8OPC_VERSION` in `src/ff8_accessibility.h` must match the top `## vX.Y.Z` heading in `CHANGELOG.md` or the push utility refuses.
- Aaron pushes via `Utilities/push_to_github.ps1`. Claude never pushes. After a successful push, verify with `github:list_commits` and update DEVNOTES + this file to reflect the new HEAD.
