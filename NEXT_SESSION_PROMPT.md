# Next Session Prompt: pick the next backlog item

## Greeting

Start every response with `## Claude Says`. Read `DEVNOTES.md` and THIS file before any work.

## Where we are at session open

**IN FLIGHT — Bug 4: dormitory/corridor exit-destination resolver (sandbox interpreter).** The PROMOTED interpreter build v0.17.9.6 is BAT-CONFIRMED + engine-cross-validated on both acceptance fields (see UPDATE7) — CLEARED TO PUSH; this chapter closes on push. The Chapter 5 recap below is the last *pushed* state.

### Bug 4 status (active)

- **Root finding (walk-through BAT, 16:58 log):** A SCREEN_BOUND exit's destination is NOT a single varblock value. It is a **hardcoded immediate operand of whichever MAPJUMP3 the script branches to.** `bgryo2_1` 'squalls' carries three MAPJUMP3s (->228 Hallway 5, ->231, ->174 Hall 10); flag-gated control flow picks one. Engine ground truth this run: **destField=228 (Hallway 5)**; addr-as-literal mislabels it **174 (Hall 10)**. At fire time `varblock[0xAE]=0` while destField=228 sat on the VM stack — so reading the varblock at the marker address (at any lifecycle point) is dead. The "174 later" coincidence is just that field-id 174 == the varblock address 0xAE.
- **Current resolver's two defects:** (a) it resets its simulated stack at every jump target, so it never follows the *taken* branch; (b) it treats the push opcode's operand as a varblock address, ignoring the bank field (bank 0 = immediate) — which is what manufactured the bogus "varblock 0xAE" marker.
- **Fix direction (agreed):** a forward **concrete JSM micro-interpreter** run at catalog-build time — follow JMP/JPF/JMPB using live variable reads for conditions, model pushes/compares, stop at the first reached MAPJUMP3, read the bank-decoded destField off the virtual stack. Read-only; never calls the engine MAPJUMP3, never writes 0x01CE4762. No caching, no manual traversal, always-current. (Caching was rejected by Aaron — can't hand-traverse every exit.)
- **v0.17.9.2 LOCAL diagnostic** — `[BC-DUMP]` full-method disassembler in `field_archive_jsm_mapjump_resolver.inl` (`DumpLineBytecode`, called at end of `MapjumpResolver::Run`), gated to {bgryo2_1, bgroad_5, bghall_5}. Passive log; no behavior change. **STRIP before any push.** Not on F12. **BUILT + BAT-CONFIRMED (18:23 log).**
- **BAT RESULT (decisive):** the engine fires the **FIRST** of squalls m7's three MAPJUMP3s. Live `[MAPJUMP-HOOK]` stack `[228,2696,-10,-39,192] -> destField=228 (Hallway 5)` matches group #1's `0x07` operands verbatim (ip 3756-3761). The resolver instead stack-underflows on groups #1/#2 and "resolves" group #3 (->174 Hall 10) = the bug. Confirmed: `[PSHM-DEST] ... -> field 174 [addr-as-literal]`, catalog shows "Exit to Hall 10" (engine truth = Hallway 5). The three destFields are hardcoded immediates (228/231/174).
- **Two model corrections from the dump:** (1) `0x07` pushes its operand as an **immediate** (the 5 live stack values equal the 5 `0x07` operands verbatim, incl. -10/-39 — impossible as addresses), so the varblock-read theory is fully dead. (2) `bgroad_5` squalls m7 has two MAPJUMP3s ->237 (double room) and **->245 (= bgryo2_1, the single room)**, gated by `PSHSM_W 0x0100` + an immediate compare, confirming the SeeD/story split exactly as Aaron described.
- **RESOLVED (v0.17.9.3 OPDUMP + capstone) — authoritative JSM opcode model.** The mod's opcode-name table was mislabeled, which is exactly why the resolver's CFG broke. True semantics (from handler disassembly): `0x00` = push literal (whole word); `0x07` = push sign-extended **immediate**; `0x0A`/`0x0C` = push varblock **byte/word** at `0x01CFE9B8+param` (live read); `0x0B`/`0x0D` = pop -> varblock byte/word; `0x08` = push local-frame[param] (buffer at ctx+0x140); `0x01` = **binary-op/compare** (param selects the operator via secondary table `0x00B8DE4C`; pops 2, pushes 1); `0x02` = **JMP** unconditional (IP += param); `0x03` = **JPF** (pop; if zero, IP += param); `0x05`/`0x06` = method entry/exit frame ops; `0x2A` = MAPJUMP3 (pop 5). Jump target = curIP + 1 + (int16)param. VM struct: stack base = ctx+0, SP byte = ctx+0x184, IP word = ctx+0x176.
- **Control-flow puzzle SOLVED:** the dorm/corridor exit gate idiom is `0x0C push varblock[0x100]; 0x07 push <imm>; 0x01 compare(opN); 0x03 JPF` — control FALLS THROUGH to the first MAPJUMP3 when the comparison is TRUE. bgryo2_1: `varblock[0x100] <op9> 570` is true now -> fires group #1 -> 228. bgroad_5: `varblock[0x100] <op9> 177` selects 237 (double room) vs 245 (single room). The live branch variable is **the varblock WORD at offset 0x100** (`0x01CFEAB8`); the destinations are immediates. (The old "varblock 0xAE" was just the resolver mis-reading the third MAPJUMP3's immediate push of 174.)
- **NEXT ACTION:** lock the opcode semantics via **deep research** (exact prompt was provided to Aaron; primary path because the on-disk disassembly is readable only through a ~3-line peephole via the dryRun-marker trick, so reading 4-5 full handler bodies is ~50+ calls — impractical in-session; the push handler entry at `0x0051C5C0` was confirmed to be a normal prologue). Validate the research answer against the on-disk disassembly with **targeted single-instruction marker reads** and against the runtime oracle (bgryo2_1 squalls m7 must yield 228; bgroad_5 must yield 245 post-SeeD / 237 pre-SeeD). THEN write the forward concrete interpreter. Need: do `0x01`/`0x02`/`0x03` consume the two preceding pushes as a compare; what memory does `0x0C` PSHSM_W (operand 0x100) read; confirm `0x07` immediate; and HOW does control reach the first MAPJUMP3 at mod-dword 3761 given the `0x01` JMP at 3754 → 3764 (the puzzle the CFG can't currently explain).
- **Disassembly access note:** files are at `Game Files/disassembly/FF8_EN_.text_0x00*.asm`; line format `0xADDR:  MNEMONIC  OPERANDS`. `read_text_file` errors over 1MB (can't bulk-read mid-file), and `head` always starts at line 1, so mid-file ranges aren't directly readable. Use the `filesystem:edit_file` dryRun-marker trick (oldText=`0x0051C5C0:` -> newText=`0x0051C5C0: ;<<MARK`) for ~3 lines of context each side; the diff hunk header gives the line number. Dispatch table `0x00B8DE94` is in `.data` (no `.asm` dump) — go straight to known handler addresses instead.
- **`[OPDUMP]` diagnostic (v0.17.9.3 + v0.17.9.4, both BAT-confirmed):** `DumpOpcodeHandlers` in `field_nav_mapjump_diag.inl`, once at hook install. v0.17.9.3 dumped `table[0x00..0x40]` + key handler bytes (capstone -> the opcode model above; dispatch base `0x00B8DE94`, `table[0x07]=0x0051C990`). v0.17.9.4 dumped the opcode-`0x01` CAL operator sub-table at `0x00B8DE4C` (18 entries 0x00..0x11). **Capstone confirmed at the instruction level:** sub `0x06`=EQ (sete), `0x07`=GT (setg), `0x08`=GE (setge), `0x09`=LS (setl, signed <), `0x0A`=LE (setle), `0x0B`=NT (setne); `0x00`=ADD, `0x01`=SUB, `0x02`=MUL, `0x03`=DIV, `0x04`=MOD, `0x05`=NEG. CAL pops value2 (top) + value1 (deeper) and computes `value1 <op> value2`. **Gate operators CONFIRMED:** bgryo2_1 = `game_moment LS 570`, bgroad_5 = `game_moment LS 177` (param 9 in both per `[BC-DUMP]`). In-container recipe: `pip install capstone --break-system-packages`, `Cs(CS_ARCH_X86, CS_MODE_32)`, base = the logged `@0xADDR`.
- **NEXT ACTION — v0.17.9.6 PROMOTION BUILT, AWAITING BAT.** The validated interpreter is now production: `ShadowInterpretMethod`->`InterpretExitMethod`, leaf SEH wrapper `SafeInterpretExitMethod`, and `MapjumpResolver::Run` sets `info.param` from it for every SCREEN_BOUND entity (first MAPJUMP-bearing method; logs ` [INTERP]`), with the old abstract `ResolveMapjumpDest` retained only as fallback. The interpreter returns a plain positive literal (masked 0xFFFF, never bit31), so it flows through the existing literal path in `HookedFieldScriptsInit` (the `& 0x80000000` test is false -> uses the value directly as destFieldId) — NO downstream change. ALL diagnostics stripped: `[BC-DUMP]`/`[MAPJUMP-CTX]`/`[SHADOW]` (resolver) + both `[OPDUMP]` blocks (`field_nav_mapjump_diag.inl`); `[MAPJUMP-HOOK]` live hooks RETAINED (low-volume engine oracle). The field allow-list is gone, so the interpreter now runs on EVERY field's SCREEN_BOUND lines — this BAT is the multi-field regression. **Expected:** `[MAPJUMP-RES] ... (SCREEN_BOUND): param 0x... -> 0x000000E4 [INTERP]` (228) for bgryo2_1, `-> 0x000000F5 [INTERP]` (245) for bgroad_5; catalog labels "Exit to B-Garden - Hallway 5" / "...Dormitory Single 1"; NO `[PSHM-DEST]` addr-as-literal line. If clean -> push (Aaron runs `Utilities/push_to_github.vbs`). **Then (separate BAT):** fix the dropped real-door `MAP_EXIT 'l1'` in bgryo2_1 (ent15, `param=-2147483648`, logs `[refresh] MAP_EXIT 'l1' dropped: no position, unresolved dest`) — different code path from SCREEN_BOUND.
- **Validation oracle for whichever source lands first (deep research or OPDUMP+capstone):** interpreter must yield bgryo2_1 squalls m7 -> 228 and bgroad_5 squalls m7 -> 245 (post-SeeD) / 237 (pre-SeeD). Deep-research prompt saved at `Plan & Research Documents/JSM exit interpreter opcode semantics deep research prompt.md`.
- **IP reconciliation note:** runtime hook reported firing IP=2969 while the operand-equivalent in mod scriptData-dword space is 3761 (delta 792 = a fixed base offset between engine IP and mod dword index). The operand fingerprint, not the IP, is the reliable cross-reference; it points unambiguously at squalls m7 group #1.
- **Two conflicting decoders in-tree:** resolver/dump high-byte model (matches runtime) vs Deling-style `DecodeJSMInstruction` in `field_archive_jsm_helpers.inl` (bit31 opcode, no inline params) — the high-byte model is correct for this build.
  - **UPDATE6 (v0.17.9.6 PARTIAL BAT, 2026-05-30 12:15 build):** Build clean, deployed, STABLE. Today's run exercised a Dollet X-ATM092 chase ONLY (dotown_3 -> dotown_2 -> dotown_1 -> wm12) — NOT the dorm acceptance test — but it gives three useful signals. (1) **Interpreter is live multi-field & stable:** ran through a full chase + multiple field loads with no SEH faults; chase finished 0 catches. (2) **Where it fires it is CORRECT:** `dotown_2` SCREEN_BOUND ent4 'G_Army01' (m5) and ent5 'G_Army02' (m7) logged `(SCREEN_BOUND): ... -> 0x00000154 [INTERP]` (340) / `-> 0x00000158 [INTERP]` (344), matching `dotown_2`'s INF gateway destIds (340/344) AND the live `[MAPJUMP-HOOK] destField=340`. (3) **A partial-coverage gap surfaced:** on the SAME field `dotown_2` ent2 'Selphie' (m4), and on `bcport_2` ent0 'Director' (m4), the interpreter did NOT apply — both fell to `[VARBLOCK fallback]` then `[PSHM-DEST] ... [addr-as-literal]` (Selphie -> 340, coincidence addr 0x154==340; Director -> 121 'Balamb Harbor 2', addr 0x79, unverified). So addr-as-literal is NOT fully eliminated, contrary to UPDATE5's acceptance bar.
  - **NEXT SESSION — do in order:**
    1. **Confirm the headline fix on its own fields.** Load a post-SeeD save that drops into Squall's single room (`bgryo2_1`) and walk to the corridor (`bgroad_5`). Expect `[MAPJUMP-RES] ... (SCREEN_BOUND): ... -> 0x000000E4 [INTERP]` (228, bgryo2_1) / `-> 0x000000F5 [INTERP]` (245, bgroad_5), catalog labels 'B-Garden - Hallway 5' / 'Dormitory Single 1', and NO `[PSHM-DEST] ... [addr-as-literal]` on those lines. Automated check: load the save, then read `Logs/ff8_field.log` after the room loads — no on-screen step required.
    2. **Diagnose the partial-coverage gap.** Find why `InterpretExitMethod` applied to dotown_2 G_Army01/02 but not dotown_2 Selphie (m4) / bcport_2 Director (m4). Hypothesis: the entity's first MAPJUMP-bearing method either (i) reaches a MAPJUMP whose destField is a varblock-sourced push (not an immediate) so the interpreter returns no concrete literal, or (ii) concrete execution bails on an unhandled opcode / branches away from every MAPJUMP3. Decide per-case whether the fallback is CORRECT-by-design (genuine varblock destField) or a GAP to close. NB: if Director's 'Balamb Harbor 2' (121) is actually wrong, that is the SAME bug class as bgryo2_1's old 174 mislabel and must be fixed before claiming Bug 4 closed.
    3. Only after 1 + 2 are clean: push (Aaron runs `Utilities/push_to_github.vbs`), then the separate dropped `MAP_EXIT 'l1'` BAT.
  - **Where to look:** the `dotown_2` (12:20:06) and `bcport_2` (12:21:57) `[MAPJUMP-RES]` + `[PSHM-DEST]` lines from the 2026-05-30 run are near the tail of `Logs/ff8_field.log`.
  - **UPDATE7 (v0.17.9.6 FULL BAT on the acceptance fields, 2026-05-30 12:37 run — PASS, CLEARED TO PUSH):** Item 1 above is DONE. Both dorm fields were walked in both directions, and the interpreter's build-time `[INTERP]` value equals the live `[MAPJUMP-HOOK]` engine destField on each.
    - **bgroad_5** (corridor, field 228): `[MAPJUMP-RES] bgroad_5 ent1 'squalls' (SCREEN_BOUND): param 0x800000ED -> 0x000000F5 [INTERP]` = 245 (correctly the 2nd MAPJUMP3, not the old 0xED->237). JSMScan `param=245`; catalog `cat1 ... 'Exit to B-Garden - Dormitory Single 1'`; engine walk-through 12:37:32 `MAPJUMP3 ... destField=245`. MATCH.
    - **bgryo2_1** (single room, field 245): abstract resolver still underflows groups #1/#2 and decodes #3 as VARBLOCK 0x00AE, but the interpreter overrides: `(SCREEN_BOUND): param 0x800000E4 -> 0x000000E4 [INTERP]` = 228. JSMScan `param=228`; catalog `cat1 ... 'Exit to B-Garden - Hallway 5'`; engine 12:37:10 `MAPJUMP3 ... destField=228`. MATCH. No `[PSHM-DEST] ... [addr-as-literal]` on either `squalls` line. Multi-field + stable (F9 drive into bgroad_5, empirical cam-cal applied, no SEH faults).
    - **CHANGELOG** top heading `## v0.17.9.6` is push-quality and matches `FF8OPC_VERSION` — the push utility won't refuse.
    - **DO THIS FIRST next session:** Aaron pushes via `Utilities/push_to_github.ps1`; then `github:list_commits` to confirm the new HEAD, and update DEVNOTES + this file (Bug 4 core CLOSED).
    - **Remaining follow-ups (separate, non-blocking, NOT regressions):** (i) bgryo2_1 `MAP_EXIT 'l1'` still dropped (`[refresh] MAP_EXIT 'l1' dropped: no position, unresolved dest (param=-2147483648)`; ent15 'l1' m1 ip=3299 stack-underflows — a Map Exit, different code path from SCREEN_BOUND). Redundant door: the `squalls` boundary already gives a correct navigable exit, so the room is fully usable. (ii) interpreter coverage gap on dotown_2 'Selphie' (m4) / bcport_2 'Director' (m4) — still addr-as-literal, identical to shipped behavior; decide per-case if correct-by-design or worth extending the interpreter.

**Versions:** GitHub HEAD = **v0.17.9.1** (commit `5c3af6a5`). Local source macro + deployed DLL = **v0.17.9.6 (PROMOTED interpreter; diagnostics stripped, only `[MAPJUMP-HOOK]` oracle retained)**. **BAT-CONFIRMED + engine-cross-validated 2026-05-30 (see UPDATE7) — CLEARED TO PUSH.** After the push, GitHub HEAD becomes v0.17.9.6 and the Bug 4 core chapter closes.

---

## Chapter 5 (last pushed state)

Chapter 5 delivered two SeeD-rank surfaces, both BAT-confirmed:
- **Surface 1 — R key reports the real SeeD rank.** `AnnounceSeedRank()` in `menu_tts_hotkeys.inl` reads SeeD points at savemap +0x0D6C (uint16), announces rank = points/100 ("SeeD Rank N"; "SeeD Rank A" at 3100+). Pre-SeeD gate: "No SeeD rank yet" when `points == 500 && salaryCount == 0` (salaryCount = uint16 at +0x0CDE). Replaced the old dead-zeros +0xF9C read (the issue #27 bug).
- **Surface 2 — automatic SeeD salary announcement.** `PollSeedSalary()` in `dinput8.cpp` polls each non-title frame and detects a payment by its one-frame memory signature — steps-since-pay (+0x0D64) resets by >10000, gameplay gil (+0x0B08) increases, SeeD points (+0x0D6C) drop 0..100 — then speaks rank + amount + direction ("SeeD salary. Rank N. X gil." / "Promoted to Rank N. X gil." / "Dropped to Rank N. X gil."). The salary-payment counter at +0x0CDE is NOT the trigger (it lags to the next save). Promoted/dropped wordings share the proven same-rank code path and will surface in normal play; not separately forced.

**Next task: pick a backlog item with Aaron.** Nothing is mid-flight. If Aaron raises a new bug, open a chapter on it — that's higher priority than the backlog below. The active backlog also lives in `DEVNOTES.md`; the items here are the lower-priority menu plus a couple of carry-forward notes.

### SeeD savemap reference (carry-forward — reuse for any future SeeD work)

Confirmed by diffing three of Aaron's decompressed `.ff8` saves and validated live in the Chapter 5 BATs:
- `.ff8` files are FF7/FF8 LZSS-compressed (4-byte LE size header, then the LZSS stream; N=4096 F=18 THRESHOLD=2 init-pos 0xFEE zero-filled buffer). **Live savemap offset X == decompressed-file offset 0x184 + X** (anchored on Squall HP/EXP + Gil + location).
- **SeeD points (experience): +0x0D6C (uint16). Rank = points / 100.** +1 per kill, -10 per salary. Pre-Dollet the pool sits at the base 500 (pre-promotion modifiers deferred to graduation).
- **Salary-payment count: +0x0CDE (uint16)** — increments by 1 per pay but LAGS the chime (updates at next save, not at payment); 0 pre-SeeD. Do NOT use as a real-time trigger.
- **Steps-since-pay: +0x0D64 (uint16)**, wraps ~24,575; resets to ~0 at payment (the real-time salary trigger, combined with the gil rise).
- **Gameplay Gil: +0x0B08 (uint32).** Header Gil at +0x08, header saveCount at +0x06, location at +0x00.
- Rejected decoy: +0x0D62 reads a plausible small number but is the high word of the u32 total-step counter at +0x0D60.
- **Savemap header is 76 bytes (0x4C), not 96.** Community/deep-research offsets are +0x14 too high — subtract 0x14. Always include this caveat in any savemap deep-research prompt.
- Residual edge (accepted): a freshly-promoted Rank-5 SeeD at exactly 500 points, never paid and never having killed, would briefly hear "No SeeD rank yet". Extremely narrow; revisit only if a clean SeeD-membership flag is ever isolated.

---

## Backlog (priority order — pick with Aaron)

The fuller active backlog is in `DEVNOTES.md` ("Active backlog"). Highlights and lower-priority items:

### From the DEVNOTES active backlog

- **Track A: push-through gate routing** at fepic1 and any other scripted-gate field. Three candidate fixes; the strategy decision is the first step.
- **v0.16.5.2 triage carry-over (3 open):** (1) FMV STOP/PLAY race pattern — pause/resume AD cue timer on engine STOP/PLAY instead of free-running on wall clock; (2) POLL tutorial garble — reject `[…]`/unprintable tokens in the POLL path or suppress POLL win[0] briefly on tutorial-end; (3) party member announced as NPC in 2-member parties on bdin2/bdin3 — party-filter likely keys on per-field model index instead of checking formation[] by character-ID.
- **Pre-v0.17.0 carry-over:** Ifrit/GF AD miss heartbeat (parked, add only if it recurs); `menu_tts.cpp` T-handler `!shift` gate (one-liner); FieldAnnounce display-name audit for fieldIds 0x0134/0x0136; field-name populate race at Part B arrival (log only); deep-research doc updates (Dollet countdown).

### E. Plan & Research Documents update (Dollet countdown doc)

`Plan & Research Documents/Dollet timer countdown deep research results.md` — wrong-math fix + LIVE TIMER FOUND appendix. Aaron will know specifics.

### F. Optional — playable-cast monster_id mapping (very low priority)

Squall = monster_id 0x00 confirmed (Chapter 3) and the universal `+0xB3` lookup works for every slot, so a full mapping isn't needed for correctness. Nice-to-have only if we ever want a per-character override layer.

### G. Refactor queue (size management)

No source file is over the 80 KB hard limit. Warning-zone files (60–80 KB, no action needed yet): `battle_tts_victory.inl` 77.08, `field_archive_jsm_scan.inl` 75.05, `field_nav_catalog.inl` 74.41, `ff8_addresses.cpp` 73.35, `scan_tts.cpp` 72.14, `field_nav_fieldscripts.inl` 70.54, `field_navigation.cpp` 70.39. Split a file only before a substantive edit would push it over 80 KB (the v0.17.8.20 autodrive split is the model).


### Long-deferred (don't pick without Aaron's direction)

Remove party members from field entity catalog · walk-and-talk dialog gap (hardcoded engine path) · refined-coord narrow-gate steering (#29) · Fire Cavern #28 + planner-fallback #29 · per-world-map vehicle-aware BFS / guided GPS mode · Battle Scan TTS keys 9/0 (status resist/active statuses) · Junction menu TTS · more victory screen polish · `chase_diag::OnAskOpcodeFired` snprintf bug · refined-coord persistence (JSON or %APPDATA% store) · engine-write hook for cleaner countdown freeze (cosmetic ±1-s flicker).

## Carry-forward BAT residual (low risk, no action required)

The CALIB phase-1/2 *active* body in `field_nav_autodrive_calib.inl::RunCalibration()` is a verbatim move (v0.17.8.20 split) and was not directly logged during that BAT — F9 path-finding uses ca-quantized axes and skips calibration, and the chase that ran was direction-mode. It only executes on a waypoint chase-drive field (domt2_1-style). Next time such a field comes up in normal play, glance at `ff8_field.log` for `[CALIB] phase 1 done` / `phase 2 done` as a free confirmation. The call site and idle/fall-through path are already production-proven.

## Removed from backlog (closed)

- **`deploy.bat` "Version: SINGLE-PRONGED" regex** — Already fixed (verified 2026-05-29, no code change needed). `deploy.bat` carries the v0.15.10.1 `/B` begin-of-line anchor on its `findstr`, and `deploy.ps1` (which writes `build_latest.log`) uses a `^`-anchored `Select-String` regex. Current build logs print the real version (e.g. `Version: 0.17.9.0.3`). The backlog entry was stale, predating the v0.15.10.1 fix.
- **Chapter 5 (SeeD rank + salary)** — Closed + pushed v0.17.9.1 (commit `5c3af6a5`). Both surfaces BAT-confirmed.
- **DEVNOTES cleanup** — DONE 2026-05-29.
- **Bug #8 NAMES (field entity catalog)** — no dream-aware naming to fix; the catalog uses generic category labels by design. Removed.
- **Chase chapter** — Closed as Chapter 4 (v0.17.8.19.4, commit `3e3fcfa9`).
- **Autodrive split refactor** — Closed + pushed v0.17.8.20 (commit `4bd5b86d`).

## Session startup ritual reminder

Aaron may say "BAT" mid-conversation. That always means: read `Logs/build_latest.log` tail for the version + success status, then the relevant domain log (`ff8_field.log`, `ff8_battle.log`, `ff8_menu.log`, `ff8_world.log`, `ff8_dialog.log`) tail. Never assume a BAT result without reading the log. Read the FULL relevant log, not just the tail — an event may be earlier than the tail, and world-map movement is in `ff8_world.log`, not `ff8_mod.log`.

If Aaron raises a new bug not in this list, open a new chapter on it — that's higher priority than the backlog.

## Push-flow reminders

- Local CI mirror runs at push time and enforces the 80 KB hard limit on every `.inl` and `.cpp` source file. Watch zone is 60–80 KB (warning, doesn't block). Hard fail at 80 KB.
- If a push refuses for size, the fastest fix is a comment trim. The proper fix is a split refactor (the v0.17.8.20 autodrive work is the model).
- `FF8OPC_VERSION` in `src/ff8_accessibility.h` must match the top `## vX.Y.Z` heading in `CHANGELOG.md` or the push utility refuses.
- Aaron pushes via `Utilities/push_to_github.ps1`. Claude never pushes. After a successful push, verify with `github:list_commits` and update DEVNOTES + this file to reflect the new HEAD.
- F12 is reserved for the current session's diagnostic only — one at a time; remove any prior F12 diagnostic before adding a new one, and strip diagnostics before a chapter is pushed.
