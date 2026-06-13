# DEVNOTES_HISTORY - FF8 Accessibility Mod Build History Archive
## All detailed build tables, investigation narratives, and per-version test results

---

## v0.18.2.x menu chapter — main-menu submenus & character-select TTS (2026-06-02, shipped v0.18.2.23)

Opened from HEAD v0.18.1.13; shipped as v0.18.2.0–.23 (GitHub HEAD sat at v0.18.2.12 mid-chapter, then .13–.23 followed in one push). Closed #10, #46, #47, #48 (plus prior-chapter #42); #45 left open/blocked. Per-version summaries in CHANGELOG.md.

**(2) Refine quantity context — v0.18.2.0.** `PollRefineQuantity` orienting phrase announces when the quantity screen appears and what it does. BAT-confirmed ("informs me when the quantity screen appears and what it does").

**(1) Junction > Auto submenu, option readout + applied-confirmation — v0.18.2.1–.6.** Auto submenu = junction focus `+0x22E == 11` (stable while navigating), option cursor `+0x26A` (0 = Atk/up Str, 1 = Mag, 2 = Def/up HP); discovered free from SUBMON. A `focus==11` branch in `PollJunctionSubmenu` announces "Attack"/"Magic"/"Defense" on move; `/` reads each option's help via `JunctionAutoSpeakHelp()` spliced into the GF/Ability `/` chain in `MenuTTS::Update()` (v0.18.2.1, BAT-confirmed). Applied-confirmation was the hard part: confirm vs cancel CANNOT be told apart by focus path — both go 11->8->3. Dead ends: v0.18.2.3 magic-changed snapshot (silent on a no-op confirm); v0.18.2.4 `pEngineInputConfirmedButtons` (the menu never sets it — zero `[JuncBtnDiag]` all session; the menu uses a separate input path, the same reason the naming bypass had to fake VK_RETURN). SOLUTION (v0.18.2.6): a 1-byte HW WRITE breakpoint (DR3; DR0/1/2 are the battle BPs) on the junction working byte `pMenuStateA+0x6C2`, armed on Junction activation (`JuncAutoBP_Arm` in the `+0x1E8==17` block), disarmed in `ResetJunctionState`. The write comes from the game's auto-junction routine at **0x004BE790** (clear-loop ~0x004BE7A1 zeroes the array; accumulate-loop ~0x004BE84F ORs magic ids in — `+0x6C2` went 0->16->18=Blizzard); reached via 0x004BFB40 <- 0x004DA7F0 (the big junction-menu fn). v0.18.2.5 BAT + Aaron's confirm/cancel labelling proved the routine runs on a confirm (incl. a no-op confirm) and NOT on cancel — a clean discriminator (silent on all three cancels; not a staleness artifact). The lean VEH sets `s_juncAutoRoutineRan` when it fires at focus==11; the action-menu (focus==3) resolution announces "Junctioned automatically for <opt>" if set, silent if clear; flag cleared on each Auto-submenu entry. MinHook was the fallback but the BP held, so no hook needed (and 0x004BE790's signature wasn't greppable from the 98 MB .asm anyway). Built on `battle_tts_dmg_read_bp.inl`'s pattern.

**(3) Items Use-target — HP-on-use + roster source — v0.18.2.7–.12.** v0.18.2.7 (HP re-read) FAILED BAT: `GetCharacterHP` preferred the computed-stats array 0x1CFF000, which is NOT updated by an in-menu item use (only re-syncs on Item-screen re-entry). BAT also surfaced a 2nd bug: a 4-member Use list read the 4th member as "Unknown" because `GetPartyCharAtVisualPos` built the list from the 3-member battle formation `+0xAF0`, while the Use screen lists the FULL roster incl. benched members. v0.18.2.8 `[ItemDiag]` (auto, use-target only) scanned for the roster array + dumped HP sources; v0.18.2.9 data: after a Potion on Squall, savemap curHP=536 (live) vs computed-stats 336 (stale), savemap maxHP=0 (FF8 derives max HP at runtime). FIX path: curHP from the savemap, maxHP from computed stats (#10). Roster: GCW render order proved the screen lists the roster sorted by char index; the roster array at `pMenuStateA+0x1DB` is `0xFF`-terminated ([1,0,5,3,FF...]). v0.18.2.10 `GetPartyCharAtVisualPos` now sources the roster from `+0x1DB` (read to 0xFF, <=10), sorts by char index, maps the cursor, falls back to the battle formation only if empty — BAT-CONFIRMED (Squall/Zell HP live; roster correct, no Unknown; a Potion-error test confirmed the ordering) -> **#10 and #46 CLOSED**. Benched max-HP gap filed as #47: v0.18.2.11 `[ItemDiag2]` located a per-character menu HP display array at `pMenuStateA+0x71E`, stride 0x20, curHP +0 / maxHP +2, covering ALL available chars incl. benched (Quistis3-benched=861/861; 0x1CFF000 is battle-only — junk past slot 2). v0.18.2.12 `GetCharacterHP` reads maxHP from `+0x71E` gated by dispCur==savemap curHP; computed-stats/header kept as fallback — BAT-CONFIRMED (Quistis benched reads "861 of 861") -> **#47 CLOSED**. All `[ItemDiag]`/`[ItemDiag2]` removed.

**(4) Junction character-select reserve announce — v0.18.2.13–.14.** The Junction char-select read the 3 active members but was silent on the reserve in the lower box. Root cause: `AnnounceJuncCharSelect` mapped cursor->battle formation `savemap+0xAF0` and bailed `cursorPos>2`; the reserve isn't in `+0xAF0` ([1,0,5,FF]) — only in roster `+0x1DB` ([1,0,5,3], raw formation-then-reserve = the screen's visual order, UNLIKE the Item screen's char-index-sorted order, so `GetPartyCharAtVisualPos` was NOT reusable here). v0.18.2.14: `AnnounceJuncCharSelect` + `GetJuncSelectedCharIdx` source the char from roster `+0x1DB`, accept cursor 0-7, reserve HP from `+0x71E`, "Empty" for empty slots; keeps "Name, Level N, HP X of Y" + dream-aware model_id naming. BAT proved cursor 3 -> roster[3]=3 -> "Quistis, Level 18, HP 901 of 901"; cursor 4 -> empty. `[JCharDiag]` removed.

**Main-menu "Rearrange party order" panel — v0.18.2.15–.19.** From the bare main menu the cursor can hop onto the party panel (help bar "Rearrange party order") to reorder the 3 active members; it announced nothing. v0.18.2.15 `[PartyDiag]` proved: source cursor `+0x1D6` cycles 0/1/2 (never 3 — reserves are the separate Switch command); on the panel `+0x1B6==0x0F`. v0.18.2.16 announces the member at `+0x1D6` on the panel by reusing `AnnounceJuncCharSelect(slot)` (roster-indexed) — BAT "That worked!" (C2712 build error first: no `__try` in `MenuTTS::Update` since it holds a std::string; dropped the SEH wrapper). v0.18.2.17: the .16 BAT revealed TWO cursors — source-select picks a member, destination-select picks the slot to swap into; told apart by `+0x1B6` (source `0x0F` cursor `+0x1D6`, destination `0x10` cursor `+0x1D7` while `+0x1D6` stays locked). Update tracks partyMode 0/1/2 (off/source/dest), announces the member at the active cursor, and speaks a one-time "Choose destination" cue on entering dest-select — BAT "That worked!". v0.18.2.18: the reverse (party->command column) was silent until an up/down press because top cursor `+0x1E6` doesn't change on the panel; Update resets `s_prevCursor=0xFF` on leaving the panel so the next `PollMenuCursor` re-announces the command — BAT "That worked". v0.18.2.19: the panel announced from Junction but was silent from Item/other commands because the block was gated by the per-submenu `!s_*Active` flags, and `s_itemSubmenuActive`/`s_gfActive` get set merely by HOVERING those commands; re-gated on the bare-main-menu indicator `+0x1E8==0xFF` and detect the panel via `+0x1B6` alone — BAT "Fantastic!". Feature complete: reads consistently L/R between party and commands from every command.

**Magic/Status character-select — v0.18.2.20–.21.** Magic (top cursor 2) and Status (top cursor 3) have the same intermediate char-select step, which was silent. v0.18.2.20 diagnostic proved both reuse Junction's char-select, differing only by subsystem code: **Magic `+0x1E8==3`, Status `+0x1E8==5`**, both focus `+0x22E==0`, cursor `+0x1E9` -> roster `+0x1DB` (cursor reached 3 = reserve, so reserves are in range; `+0x26C/0x272/0x1D6/0x26A` stayed 0). v0.18.2.21 poller in `MenuTTS::Update` announces the member at `+0x1E9` via `AnnounceJuncCharSelect`, gated to the char-select phase by focus `{0,8}`, with `s_magStatActive`/`s_prevMagStatCursor` state and force-announce on entry. `[MagStatDiag]` removed.

**Active/reserve party grouping cue — v0.18.2.22–.23.** Sighted players see the 3 active battle slots set apart from reserves. The char-select now prepends a fieldset/legend-style cue the first time the cursor lands in a group or crosses into the other, spoken in the SAME utterance as the member (a separate Speak would be interrupted away); start cues only, no end cues. Active = char in the battle formation `savemap+0xAF0`. Added to the shared `AnnounceJuncCharSelect` behind an opt-in `announceGroup` arg (Junction + Magic/Status pass true; the Rearrange panel, active-only, keeps the default false); `s_charSelPrevGroup` resets on each (re)entry so every visit re-cues, but action-menu round-trips don't. v0.18.2.22 wording "Active Party Start"/"Reserve Party Start" — BAT "Beautiful!"; v0.18.2.23 shortened to "Active Party"/"Reserve Party" — BAT "announcements sound good".

**Diagnostics at push:** every session probe (`[PartyDiag]`, `[ItemDiag]`/`[ItemDiag2]`, `[JCharDiag]`, `[MagStatDiag]`) was removed when its feature landed. Standing reusable diagnostics kept: F12 GCW-capture harness, GF exists-dump (one-shot), save-subsystem offset logging (`LogSaveSubsystemChanges`/`LogSaveDiagState`, ~200/500 ms while on Save — pre-existing, candidate for a future trim), `[TTS]` audit trail, OpenGL screenshot capture.

---

## Track A: FindPortal off-by-one + Step 0 chase-protection guard (2026-05-31, pre-fix)

**bggate_6 turnstile** (B-Garden Front Gate 5, fieldId 0x00A3, engine `bggate_6` NOT `fepic1`). Local diag v0.17.9.13 (`FEPIC1_GATE_DIAG` in `field_navigation.cpp`, `[GATEDIAG]` armed on 0x00A3; v0.17.9.12 used the wrong name and never fired). Exits: `squallsd`->165 Hall 1 (north/in), `zell`/`zells`->162 (south/OUT). GATEDIAG result was NOT true-wall: 153/159 tris one component; spawn (tri4), turnstile Lines (tri15 'light'/tri115), OUT->162 boundaries (tri39/tri48), IN->165 (tri96)+INF gw (tri98) all reachable. Engine [DEADEND] flags 71/159 narrow. Symptom (Aaron): planner OK, steering OSCILLATES at the turnstile neck (narrow-gate, the long-deferred #29). OUT routes are 1-tri-wide corridors.

**ROOT CAUSE — FindPortal emits WALL edges as portals.** For neighbor[e], `FindPortal` read vertex pair ((e+1)%3,(e+2)%3); but the FF8 .id walkmesh stores neighbor[e] across edge (v[e], v[(e+1)%3]) — the (e,e+1) pair. Rotated ONE vertex off. Proof on bggate_6: FindPortal(13->148) returned tri13's west-wall edge (-1710,-598)-(-1710,-473), but 13&148 share the Y=-598 edge (-1710,-598)-(-1480,-598); tri13 nb[0]=148 maps to (v0,v1), tri148 nb[1]=13 maps to (v1,v2). On rectilinear fields the mis-picked edge is a wall; the wall-parallel COLLAPSE then slams the waypoint onto it ((-1686,-553)) -> wedge -> recovery ping-pong tri13<->14 -> give-up. The SAME (e+1,e+2) pair is in THREE funcs in `field_nav_pathfinding.inl`: FindPortal (~491-492), GetSharedEdgeLength (~98-99), EdgeMidpointPath (~972-973). THE FIX (Step 1): change the pair to vertexIdx[edgeIdx] and vertexIdx[(edgeIdx+1)%3] in all three. Deterministic sed: `sed 's/(edgeIdx + 1) % 3/edgeIdx/g; s/(edgeIdx + 2) % 3/(edgeIdx + 1) % 3/g'`. PARADOX (why it doesn't break everywhere): AGENT_RADIUS=30 + FF8 wall-slide + recovery mask it except on narrow/rounded gates. Same wall-hug reported on B-Garden Hall 6 + Balamb Hotel Exterior. SSFA funnel is shortest-path (cuts convex corners taut); AGENT_RADIUS too small on curves.

**AGREED SEQUENCE (one change per BAT):** (0) chase-protection guard FIRST [this section]; (1) fix the edge math in the 3 funcs, validate, BAT the CHASE FIRST (must stay 0 catches) then bggate_6/Hall6/Balamb; (2) center-seeking IF corners still hug; (3) trigger-line transparency IF the gate still snags. CHASE RISK: the chase clears today via hacks tuned ON the buggy portals (COLLAPSE v0.16.1.2, protected wps v0.17.8.19.1, prune-skip v0.17.8.19.2, recovery-off), so step 1 could regress the funnel chase fields — hence the guard + chase-first BAT.

**ROBOT / CHASE CHARACTERIZATION** (known-good auto full-clear v0.15.9.8.3, chase_events_extract 09:53-09:56, 0 catches): under current source the funnel-driven (FindPortal-affected) chase fields are domt2_1 + dotown_3 (both fallback MODE_TARGET). The rest are direction/staged/bridge (NOT funnel). On both funnel fields the kani slot reads a STATIONARY constant the whole field — a fixed hazard point, not a trajectory: domt2_1 (814,-875), closest approach 513; dotown_3 (16,0), closest 870.

**STEP 0 BUILT & PROVEN (this session).** Two layers, both in the new committed `/tests/` dir (`/Utilities/` is gitignored):
  - `tests/chase_pathfinding_guard.py` — pure-Python portal-correctness model over the guarded fields + the bggate_6 tri13->148 bug/fix witness (`--selftest` + bulk validated).
  - `tests/chase_harness.cpp` + `tests/gen_chase_fixture.py` — the real protection: the generator slices domt2_1/dotown_3/bggate_6 out of the committed `Plan & Research Documents/ff8_walkmeshes.json` (CONFIRMED git-tracked, 26,625,924 bytes, sha 28b8d97d) into `chase_fixtures.h`; the harness compiles the ACTUAL `field_nav_pathfinding.inl` behind a small shim and runs the real A*/funnel. Hard gate: walkmesh mesh integrity (every neighbor link shares exactly 2 verts). Reported (advisory pre-fix): funnel wp count, out-of-mesh count, closest approach to the kani point.
  - PROVEN in-container, current vs sed-fixed .inl: **domt2_1 — fix is NEUTRAL**: A* finds NO path (spawn tri 107 is in a 42-tri island with no route to the goal; structural — 0 narrow edges, perfect mesh integrity) under BOTH; the field clears via chase-drive direct steering + stuck-recovery + INF-gateway detection, not the funnel. **dotown_3 — fix IMPROVES it**: 30-wp zigzag / 5 out-of-mesh (current) -> 6-wp clean diagonal / 0 out-of-mesh (fixed); closest to the inert robot 895 -> 803, both far outside catch range. **bggate_6** reproduces the (-1686,-553) wall-hug under current code; gone under the fix. CONCLUSION: the FindPortal fix does NOT endanger the chase, and NO moving-robot model is needed — the in-game chase-first BAT stays the end-to-end catch check.
  - WIRED: `deploy.ps1` (NON-blocking; generates fixture, compiles harness via g++ or MSVC cl/vcvars, runs it, appends a `CHASEGUARD` block to the END of build_latest.log; also runs the Python portal check) and `.github/workflows/safety-checks.yml` (new BLOCKING `chase-harness` CI job: gen fixture -> g++ -> run; plus the existing `chase-pathfinding-guard` Python job). `tests/.gitignore` excludes the generated header + build outputs.
  - REMAINING: mirror the harness build into `push_to_github.ps1` Step 7c (CI is authoritative, low-urgency); in Step 1 promote "all funnel wps in-mesh == 0" + a golden-route/robot-margin snapshot (captured from the proven-good FIXED routes) to HARD asserts; strip `FEPIC1_GATE_DIAG` before push.

---

## v0.17.8.17.1 → .17.8: Chapter 2 — Laguna dream bundle (bugs #7 + #8, commit `b7067354`)

Seven incremental fixes squashed into commit `b7067354` on 2026-05-28. Per-build detail in `CHANGELOG.md` v0.17.8.17.1 .. .17.8 entries.

  - Bug #7 (field nav): fixed v0.17.8.17.1.
  - Bug #8 NAMES (in-battle): fixed v0.17.8.17.2.
  - Bug #8 COMMAND MENU: fixed v0.17.8.17.5.
  - Bug #8 NAMES (Victory screen): fixed v0.17.8.17.6.
  - Bug #8 NAMES (Main Menu audit -- Junction char-select + M-summary + Item Use-target + GF owner): fixed v0.17.8.17.7.
  - v0.17.8.17.8 cleanup: F12 Laguna diagnostic infrastructure removed.
  - FIELD entity catalog is N/A -- the catalog uses generic category labels (NPC, Event, Interaction, Exit, Gateway), not proper character names, so there is no dream-aware naming to fix there.

### Key carry-forwards (dream-party identity resolution)

  - **Dream party data lives in regular char-data array (CONFIRMED v0.17.8.17.5 + .17.7).** `char-data[SAVEMAP_PARTY_FORMATION[slot]]` IS the active dream character's struct: `commands[3]@+0x50`, `magics[32]@+0x10`, GF mask@+0x58, `exp@+0x04`, `model_id@+0x08`. The savemap formation (`SAVEMAP_PARTY_FORMATION = 0x1CFE74C`; menu reads `+0xAF1`) holds the STALE regular field formation `[05 00 01]` during a dream -- correct for INDEXING char-data, wrong as a NAME source.
  - **Three dream-identity sources, by context (BAT-confirmed during this chapter):**
    - In battle / victory (battle module live): `compStats[slot]+0x1C3` actor-kind (8=Laguna, 9=Kiros, 10=Ward). compStats base 0x1CFF000, stride 0x1D0.
    - Main menu (no battle): the loaded char struct's `model_id` (+0x08) -- the v0.17.8.17.7 BAT log captured `modelId=10/8/9` for Ward/Laguna/Kiros in mode-6 dream junction.
    - Field: `setpc` (field entity +0x255). v0.17.8.17.1 used this to fix bug #7.
  - **`ResolveDreamAwareCharId(charIdx)` (menu_tts_diagnostics.inl, v0.17.8.17.7):** THE canonical resolver for formation-index -> dream-aware name. Returns model_id when 8/9/10, else original index. Used by AnnounceMenuSummary, GetPartyMemberName, item Use-target naming; Junction GF-owner uses inline literal-address version.
  - **`GetBattleCharName(partySlot)` (battle_tts_menu_helpers.inl):** in-battle actor-kind override for dream, falls back to savemap for regular. All in-battle ally naming (turn/target/HP keys/command menu/victory/drawer) routes through this. Battle `CHAR_NAMES[8]` is only the actor-kind fallback for regular characters.
  - **`GetVictoryCharName(slot, fallbackId)` (battle_tts.cpp, v0.17.8.17.6):** dream-aware victory name; reads `s_dreamSlotCharId[slot]` snapshot captured per-frame during battle for cross-thread reach to the victory thread at mode 4.

---

## v0.17.8.11 → .15.1: Bug #10 — B-Garden Hall 6 NPC Xu mislabeled (single commit `c7b80872`)

Nine builds across two days. Xu was JSM `kanban2` (ent25, cat3, PSHM pos (4626,-3459)) on `bghall_3` (field 170) -- the SYM name was misleading; kanban2 IS Xu. The wrong path was a chara.one model-archive parser (v0.17.8.11-.14): MinHooked `chara_one_read_file`, parsed Mch/Char headers, cross-referenced SETMODEL's chara.one slot index against the parsed model class. Successive bug fixes through this chain (Bug A isMch flag in .12; Bug B SETMODEL opcParam vs stack in .14) concluded kanban2 = prop because p048 classified as prop. The disproof: Aaron's F11 screenshot of bghall_3 (`Logs/screenshots/f11_204546_707.png`) showed Xu visibly standing as a character model in front of Squall at the kanban2 spot, dialog box reading `Xu "Hey, Squall, heard you got your first mission already!"`. There is no signpost. The chara.one classifier was wrong about p048 (p048 IS a character model on this field, regardless of the 'p' prefix convention), AND more fundamentally: file-level model classification was the wrong mechanism entirely. The right question is gameplay behavior, not model identity.

**The clean fix (v0.17.8.15 + .15.1):** the JSM scan already had the behavior signal -- `jsmCategory == 3 (Other) && hasSetmodelInit` -> "NPC N". Everything else (Line walk-across, Background script-only) -> "Interaction N". Per Aaron's directive, NPC labels stay generic "NPC N" -- no SYM names exposed. v0.17.8.15 BAT confirmed the mechanism works (Xu announces as NPC). v0.17.8.15.1 added two follow-on fixes: dedupe counter (name-prefix match instead of all ENT_NPC, so friendly-named NPCs like Cid don't inflate the count) and announce sameType (type-based matching for JSM-injected entityIdx <= -300, so the "X of Y" suffix works for both NPC and Interaction). Final BAT showed `'NPC 1 1 of 1'` on bghall_3, all expected log lines present, no regressions. The whole chapter pushed as single commit `c7b80872`.

**Carry-forward learnings:**
- **SYM names are unreliable as identity hints.** kanban2 IS Xu. The internal name was placed for the script author, not for us. Don't infer entity behavior from SYM.
- **File-level model classification is the wrong primitive for behavior questions.** p048's filename prefix is a convention, not a contract; on this field it loads a character. When asking "how does the player interact with this entity", look at the JSM behavior signals (`jsmCategory`, `hasSetmodelInit`, `hasDialogReqTarget`, `hasTalkSetup`, `foundExtDispatch`), not at the model file.
- **NPC label policy:** generic `"NPC N"` / `"Interaction N"` only. Friendly names (Cid, Quistis) come from a different path (the runtime entity name resolver). The raw-SYM relabel sequence must never expose SYM names.
- **Announce-time counters need type-based matching for JSM-injected entries.** Legacy `entityIdx >= 0` test was "is this a runtime entity" -- it fails for JSM-injected NPCs/Interactions (entityIdx <= -300). When adding a new entity TYPE to the catalog, also extend the corresponding announce sameType branch.

---

## v0.17.8.7: cardgamemaster debug phantom filter + Event/Interaction double-injection fix

Filtered the `cardgamemaster` debug phantom from the field entity catalog and fixed the Event/Interaction double-injection that was also hiding the Directory. Full per-build detail in `CHANGELOG.md` v0.17.8.7 entry. (No standalone narrative was carried in DEVNOTES beyond the closure-paragraph one-liner; recorded here for completeness during the 2026-05-29 DEVNOTES trim.)

---

## v0.17.8.16 + .16.1: Chapter 1 — Fire Cavern bug #1 (Quistis infirmary FMV premature, commit `b6afa8cb`)

Closed across two stacked patches squashed into one commit:

- **v0.17.8.16** -- engine cue-clock fix (`fmv_audio_desc.cpp`). Replaced wall-clock cue timer with engine-active-time accumulator that only advances when `FF8Addresses::IsMoviePlaying()` returns true. BAT-confirmed 2026-05-27 18:10-18:12 on `disc00_01h.avi`: 17-second gate held, cues fired at correct offsets.
- **v0.17.8.16.1** -- AD content rewrite for the same FMV. Engine fix BAT revealed the AD itself was wrong (misidentified Quistis as Dr. Kadowaki; framed Squall as leaving rather than lying in bed). Frame-verified via ffmpeg (27 frames @ 0.5s intervals). Rewrote `Audio Descriptions/disc00_01h.vtt` and corrected the `FMV_SCENE_REFERENCE.md` entry so future AD authors don't repeat the misidentification. BAT-confirmed 2026-05-28 (Aaron: "sounded good").

---

## v0.17.8.9: bghall_1 save point label (save-line script-association detection)

SOLVED + SHIPPED in v0.17.8.9 (BAT-confirmed; signal found via a now-removed LOCAL script dump). The LOCAL dump of bghall_1 entities (zells/selphie/savePoint/saveline0) proved the save line is `ent5 'selphie'` (the SETLINE at (-700,-8593) then shown as "Interaction 1"). Its script literally pushes the save-enable opcodes as constants: PUSH 303 (0x12F SAVEENABLE) and PUSH 304 (0x130 PHSENABLE) in BOTH method[6] (dwords 3624/3632) and method[7] (3657/3665). The control line `ent4 'zells'` has NONE of these (clean discriminator). Why the scanner missed it: selphie's ONLY 0x1C is the bare runtime-supplied dispatch in method[1] (`EXT_DISPATCH` empty-stack, like the dorm bed) -- the save constants live in methods 6/7 and are never popped by a local 0x1C, so dispatch-resolution can't set foundSaveenable. savePoint (ent27) is unpositioned (X=PSHM135 Y=PSHM588, no SET3-shift) and its 0x1C resolves to a runtime PSHM; saveline0 (ent36) is a REQ-chain controller with a MAPJUMP (classified MAP_EXIT) and no statically-visible save op -- so neither save-POINT entity can carry the label.

**FIX (the chosen association, field-load, no cache, no heuristic guess): in the JSM scan, for a Line entity (jsmCategory==1) scan its full bytecode for literal PUSH of the save opcodes -- set foundSaveenable when MENUSAVE(302) is present OR both SAVEENABLE(303) and PHSENABLE(304) are present. That makes signal-(a) fire -> isSaveLine -> the catalog surfaces selphie as "Save Point" at its own SETLINE center (-700,-8593), exactly where auto-drive already arrives.** Contrast (why the dorm bgryo2_1 already works): its savePoint gets a SET3-SHIFT position (229,97) and injects directly, and its saveline0 has a statically-visible save op + LATE-RESOLVE position -- bghall_1 has neither, which is why the own-script-constant route on the LINE is the right fix here.

---

## v0.17.7.6 → v0.17.7.6.2: Empirical camera-axes calibration chapter (pushed 2026-05-20 05:47 UTC as `d3321665` squashed onto `6abcb8f`)

Closed-loop empirical correction for fields whose .ca file produces a degenerate 2D camera projection. Three iterations, each a stepping stone:

### v0.17.7.6 (BAT'd partial)
- Per-arrow ring buffer in `field_nav_observe.inl`. When `s_camAxesSource == "identity"` and observer fires, normalized measured direction pushes into matching arrow's buffer. 3-sample consensus within 10 degrees -> quantize to nearest 90° cardinal, derive orthogonal axis via R(-90°), overwrite both `s_cam*X/Y` and `s_driveCam*X/Y`. `s_camAxesSource` -> `"empirical-corrected"`. One-shot lock prevents oscillation.
- New `[NAV-CAL]` log channel.
- State reset in `HookedFieldScriptsInit`.
- BAT on bgroad_5: math correct (`camRight (1,0)->(0,-1), camDown (0,-1)->(-1,0), det=-1`) but took 80 seconds to fire. AD-gate (`s_driveActive || s_chaseDriveActive` early-return) suppressed observer for entire AD attempt; calibration only fired when Aaron walked manually after AD gave up.

### v0.17.7.6.1 (BAT'd partial)
- Two-tier AD gate in observer: chase always suppresses (own calibration loop); regular AD suppresses except when `s_camAxesSource == "identity" && !s_camAxesEmpiricalApplied`. AD's SendInput-injected keys produce same GetAsyncKeyState visibility as hand presses.
- `EMPIRICAL_MIN_SAMPLES` lowered 3 -> 2 (halves time-to-correction).
- Fixed misleading `[NAV-PROJ-INIT]` log line that hardcoded `source=ca-quantized` even on degenerate branch where code correctly set source to `"identity"`.
- BAT on bgroad_5: math + threshold worked (NAV-CAL fired after 2 UP samples) but AD pushed Aaron into a wall, `moveDist=0` for entire drive, observer's 50-unit gate filtered all samples. Calibration didn't fire until Aaron walked manually with UP after AD gave up. The catch-22 mutated: AD-into-wall -> no movement -> no samples -> no calibration.

### v0.17.7.6.2 (BAT'd CLEAN — Aaron confirmed)
- Option A: block AD on uncalibrated degenerate-CA fields with TTS instruction.
- New refusal case in `field_nav_handlekeys.inl` AD-start chain: when `strcmp(s_camAxesSource, "identity") == 0 && !s_camAxesEmpiricalApplied`, AD does NOT start. TTS announces *"Camera not yet calibrated. Press an arrow key briefly to calibrate, then try again."* `[drive] REFUSED` line logged.
- `ObsApplyEmpirical` now speaks *"Camera calibrated."* after `[NAV-CAL]` log line (lock prevents repeats).
- BAT on bgroad_5: Aaron triggered AD -> heard refusal TTS -> walked UP a few seconds -> heard "Camera calibrated." -> retried AD -> drove correctly to dormitory.

### Key learnings

- **`s_camAxesSource = "identity"` is the canonical signal for degenerate-CA + pending-calibration state.** All three iterations gated their changes on this string. The mod has three CA states: `"ca-quantized"` (CA file's 2D projection non-degenerate), `"identity"` (degenerate fallback, pending calibration), `"empirical-corrected"` (after `[NAV-CAL]` fires).
- **Observer's 50-unit movement gate is correct.** Designed to filter out player-stationary noise. But it makes AD-on-wall a dead state for calibration sampling.
- **AD pushing into wall is not a recoverable state for empirical calibration.** With no movement, no signal. v0.17.7.6.3 (parked) considered: synthetic look-around inject at field load to pre-calibrate without user action. Not implemented because v0.17.7.6.2's UX ("walk first, then AD") proved acceptable in BAT.
- **GetAsyncKeyState reflects SendInput-injected synthetic state.** Confirmed by v0.17.7.6.1's two-tier gate working in principle (observer code path enabled during AD); when player actually moved, samples flowed. The chase auto-pilot's analog injection via `s_fakeDIJOYSTATE2` is independent of GetAsyncKeyState observability — observer sees the keyboard side.
- **F11 screenshot was load-bearing for verifying the math.** axis2=+X in bgroad_5's .ca matched the visual hallway orientation (vanishing point at center-back = screen-up = world +X). camDown=(-1,0) post-correction confirmed.

### Files touched across .6 → .6.2

- `src/ff8_accessibility.h` -- version
- `src/field_navigation.cpp` -- state declarations, ScreenReader forward decl already present
- `src/field_nav_observe.inl` -- ring buffer, consensus logic, quantization, two-tier gate, TTS speak
- `src/field_nav_fieldscripts.inl` -- state reset, log line fix
- `src/field_nav_handlekeys.inl` -- AD-refusal case
- `CHANGELOG.md` -- three entries
- `DEVNOTES.md`, `NEXT_SESSION_PROMPT.md` -- updated through each iteration

No catalog changes, no auto-drive direction injection logic changes, no CA parser changes, no GPS cardinal computation changes.

---


> This file is the archaeological record. Consult ONLY when you need to understand
> WHY a past decision was made, or to trace the evolution of a specific feature.

---

## v0.17.6.2: F9 corridor-level steering disabled (PUSHED 2026-05-19, commit a42d4aeb)

### BAT result

Reading `Logs/ff8_mod.log` start-to-finish (build at 18:32:13, session ends 18:43:11):

Four bghall_1 cross-field exit drives, all reaching `Arrived.`:
1. **Hall 8 (east exit)**: 18:32:49 `Driving.` -> 18:32:58 `Arrived.` -> field transition. 9 seconds. ✓
2. **Front Gate 5 (west exit)**: 18:33:17 `Driving.` -> 18:33:31 field transition to fepic1. 14 seconds. ✓
3. **Hall 4 (west exit)**: 18:36:53 `Driving.` -> 18:36:55 `northwest, 10 steps.` -> 18:36:58 `Nearby. northwest, 2 steps.` -> 18:36:59 `In range.` -> 18:36:59 `Arrived.` -> 18:37:00 `B-Garden - Hall 4`. 7 seconds. ✓ **Textbook clean run with diagonal-kb wall-sliding through the corridor turn.**
4. **Hall 10 (from bghall_4)**: 18:37:27 `Driving.` -> 18:37:29 `north, 24 steps.` -> 18:37:37 `northeast, 4 steps.` -> 18:37:37 `Nearby. north, 2 steps.` -> 18:37:38 `In range.` -> 18:37:38 `Arrived.` -> 18:37:39 `B-Garden - Hall 10`. 12 seconds. ✓

The diagonal-kb direction announcements during drives (`northwest, 10 steps` -> `north, 24 steps` -> `northeast, 4 steps`) confirm the analog is producing diagonal masks and FF8's wall-sliding is handling corridor turns naturally, exactly as the v0.17.6.2 design predicted. The drive timings (7-14 sec) match manual-nav travel times.

v0.17.6.1 mechanics that stayed (and worked):
- Recovery counter reset on tri advance: didn't need to fire in any successful drive because the drives just *worked*.
- `MAX_RECOVERY_PHASES=30`: same, didn't fire.
- `[drive-vec]` diagnostic: stayed on, would have surfaced any new failure mode; none surfaced.

v0.17.6.2 is rolled forward as the correct fix and stays in place.

### New issues exposed by BAT (out of scope for v0.17.6.2, captured in NEXT_SESSION_PROMPT)

**Push-through gate at fepic1 (B-Garden - Front Gate 5, fieldId=0x00A3).** After the successful Front Gate 5 cross-field drive landed Aaron in fepic1 at 18:33:31, multiple within-field drives failed:
- 18:34:47 `Driving.` (target: `Interaction 3` at southwest 6 steps). Drive oscillated `south, 4 steps` -> `southwest, 3 steps` -> `south, 3 steps` -> `southwest, 3 steps` for 32 seconds. Aaron cancelled at 18:35:19. No `Arrived.`
- 18:35:25 `Driving.` (target: `Interaction 2` at south 3 steps). Drive went `southwest, 2 steps` -> `south, 3 steps` -> `southwest, 2 steps`. Aaron cancelled at 18:35:36. No `Arrived.`
- 18:35:40 `Driving.` (target: `Interaction 1` at north 4 steps). Drive went north toward Interaction 1 -> 18:35:54 `Nearby. northeast, 2 steps.` -> 18:35:55 `B-Garden - Hall 1` transition. Drive walked the player into the wrong exit (back to Hall 1) instead of completing.

Aaron's diagnosis confirmed: the front gate has a **push-through gate** -- a scripted gate the player walks INTO at a specific point to trigger an animation that pushes them through to the exit on the south side. The walkmesh almost certainly treats the gate as a wall (since you can't normally walk through it), so A* can't find a path through, only around -- and there is no "around" because the gate spans the full corridor.

The push-through is likely a PUSHRADIUS or SETLINE trigger entity that fires a scripted JUMP/MOVA opcode teleporting the player to the south side. Without engine support for this mechanic, F9 can't path through it.

**Generic entity catalog names.** fepic1's catalog shows `Interaction 1`, `Interaction 2`, `Interaction 3`, `Light 1 of 1`, `NPC 1 of 1` -- and Cafeteria 1 (fieldId=0x009A) showed `Son 1 of 1`. For a blind player these names are useless friction -- Aaron has to brute-force cycle and drive to each Interaction to figure out which one is the gate trigger, which is the guard, etc. Until the catalog has meaningful labels, even solving push-through routing leaves Aaron needing to know WHICH entity IS the gate.

Both issues queued for v0.17.7.x. Track B (catalog labels) selected first.

### v0.17.6.2 technical details

One change, one BAT cycle. The v0.17.6.1 BAT [drive-vec] log on bghall_1 Save Point exposed the dominant failure mode: corridor-level steering keeps re-introducing the exact waypoint that drive-start pre-skip discarded, wedging the player against a wall pressing pure LEFT for hundreds of ticks.

#### What [drive-vec] showed

Save Point drive setup: player at `(-568,-8218)` in tri 358, target Save Point at `(-700,-8593)` in tri 8 (corridor 358 -> 71 -> 70 -> 8). Funnel produced 2 waypoints: `wp 0=(-626,-8215)` (the collapsed corridor edge midpoint between tri 358 and 71) and `wp 1=(-700,-8593)` (the save point itself).

Drive-start pre-skip correctly bumped past wp 0 (only 58 units from player < PRE_SKIP_DIST=120): `[drive] pre-skip wp 0 (dist=58 < 120)`. With wp 0 skipped, the drive should steer toward wp 1 (south-west, into a `kb=DL` diagonal). For one tick at t=30, that worked:

```
t=30  corOverride=0  corSteer=(-700,-8593)  finalDelta=(-132,-375)  lX=-332 lY=943  kb=DL
```

But at t=60, the corridor-level steering block's `s_driveTotalTicks >= 30` gate opened and the block overrode `steerX/Y` to the shared-edge midpoint between the player's current tri 358 and next tri 71:

```
t=60   corOverride=1  corSteer=(-626,-8215)  finalDelta=(-57.8, 2.6)  lX=-999 lY=-44  kb=L
```

That's the EXACT point pre-skip just discarded. The corridor steering algorithm doesn't know about pre-skip and computes its own "local target" from the same shared edge the funnel collapsed wp 0 onto. Result: analog flipped from south-west diagonal to pure west, keyboard collapsed from `DL` to `L`, the player pressed LEFT into a wall, moveDist=0 for hundreds of ticks. Recovery fired, re-pathed, corridor steering picked the same point again, player wedged again. The drive ended with `Gave up. Distance remaining: 555.`

#### Why manual nav succeeds at the same position

Manual nav announced "Save Point. south, 2 steps. Nearby." at the same position. It uses the same funnel waypoints but has no corridor-level override -- it computes the analog directly from `(target - player) * camAxes` and presses arrow keys for the dominant axes. From `(-568,-8218)` toward `(-700,-8593)` the dominant axes are LEFT (`dx=-132`) and DOWN (`dy=-375`), so the keyboard fires `DL` diagonal. FF8's built-in wall-sliding handles the corridor turn: the player walks south-west, slides along the west wall, and naturally tracks the corridor through tri 358 -> 71 -> 70 -> 8 to the save point.

#### The fix

`field_nav_autodrive.inl` line ~635: the corridor-level steering condition is wrapped with `false &&`, matching the pattern v06.20 used to disable wall-avoidance. The entire block stays with its original v06.17/v0.15.9.2.3 rationale plus a new v0.17.6.2 block explaining why it's off and what to flip if a future field regresses without it. To re-enable later (for elongated-corridor maze fields like Fire Cavern, if needed), flip `false &&` to `true &&` -- and consider gating on `currentWpDist > 200.0f` so the override only fires when the current waypoint is far enough that an intermediate edge midpoint adds value.

Chase-drive is unaffected; it has skipped this block since v0.15.9.2.3 (corridor steering on rotated cameras / stale triId hit chase first).

#### Files changed

- `src/ff8_accessibility.h` -- 0.17.6.1 -> 0.17.6.2
- `src/field_nav_autodrive.inl` -- corridor-level steering block gated with `false &&` (~50 lines of new rationale comments)
- `CHANGELOG.md`, `DEVNOTES.md`, `NEXT_SESSION_PROMPT.md`

---

## v0.17.6.1: recovery counter reset on tri advance + [drive-vec] diagnostic (LOCAL, BAT'd 2026-05-18; rolled forward to v0.17.6.2)

Three changes, one BAT cycle. The common thread: v0.17.6.0 proved the steering pipeline is correct but the recovery system gives up too early on multi-triangle corridor traversals, and the 2-second `[drive] tick=` log can't catch transient pipeline state. v0.17.6.1 fixes the recovery counter and adds enough per-tick visibility to triage whatever fails next.

### Change 1: recovery counter resets when player advances to a new triangle

The v0.17.6.0 Save Point drive on bghall_1 made genuine progress through five corridor triangles (367 -> 366 -> 363 -> 362 -> 359), but each triangle escape needed 2-3 recovery cycles. `s_driveWigglePhase` only resets when the player crosses funnel waypoint index 3, which never happened because the path kept re-pathing back to waypoint 0 after each recovery. The global counter inflated to 12 (the old `MAX_RECOVERY_PHASES` cap) and the drive gave up at recovery 12 in tri 362 -- still making real progress.

v0.17.6.1 adds a new state variable `s_lastRecoveryTri` (in `field_navigation.cpp`, near the other recovery statics). When the recovery block fires, it reads the player's current walkmesh triangle from `entity +0x1FA`; if that tri differs from `s_lastRecoveryTri`, that's genuine corridor progress and `s_driveWigglePhase` resets to 0. Each new triangle along the corridor earns a fresh recovery budget. The state is initialized to `0xFFFF` at drive start in `field_nav_handlekeys.inl` so the first recovery doesn't see a stale tri from a prior drive.

### Change 2: MAX_RECOVERY_PHASES 12 -> 30

Safety net for cases the tri-advance reset doesn't catch. With the reset working, the v0.17.6.0 Save Point case would have run at phase max ~3 per triangle and never gotten anywhere near 30. The new ceiling fires only on "this triangle is permanently unreachable" failures, not on slow corridor traversals.

### Change 3: [drive-vec] per-tick pipeline diagnostic

The v0.17.6.0 BAT log showed `lX=-840 lY=-542` for multiple consecutive 120-tick log windows even as the player oscillated between two positions. The existing `[drive] tick=` log fires every 2 seconds and only shows post-projection state, which made it hard to tell whether the analog projection itself was wrong or just stuck on a stale waypoint.

v0.17.6.1 adds a `[drive-vec]` log that fires every 30 ticks (~0.5 s) and shows the intermediate values at each stage of the steering pipeline. Format:

```
[drive-vec] t=N tri=T pp=(px,pz) wpRaw=(wx,wy) corOverride=0|1 corSteer=(sx,sy) trigRedir=0|1 finalDelta=(dx,dz) lX=lx lY=ly kb=mask wig=W phase=P
```

Three new tracking variables (`vecWpRawX/Y`, `vecCorridorOverrode`, `vecTrigRedirected`) record stage outputs as the existing pipeline runs; they cost essentially nothing per tick and the log is gated by `s_driveTotalTicks % 30 == 0`. To turn off after triage, raise `DRIVE_VEC_LOG_INTERVAL`.

### Files changed

- `src/ff8_accessibility.h` -- 0.17.6.0 -> 0.17.6.1
- `src/field_navigation.cpp` -- `MAX_RECOVERY_PHASES` 12 -> 30; new `s_lastRecoveryTri` state
- `src/field_nav_handlekeys.inl` -- reset `s_lastRecoveryTri` at drive start
- `src/field_nav_autodrive.inl` -- recovery block tri-advance reset; three pipeline tracking flags; [drive-vec] log emit
- `CHANGELOG.md`, `DEVNOTES.md`, `NEXT_SESSION_PROMPT.md`

---

## v0.17.6.0: F9 auto-drive re-bases on manual nav primitives (LOCAL, BAT'd 2026-05-18; rolled forward to v0.17.6.1)

Three connected changes, one BAT cycle. The common thread: F9 auto-drive previously ran independent steering / arrival / crossing pipelines from manual nav. Those pipelines were where the bghall_1 BAT failures came from. Manual nav's v0.17.5.x pipeline is BAT-proven across five fields; v0.17.6.0 has F9 share that pipeline wherever practical, while leaving chase-drive's separate (also BAT-proven) empirical-calibration path alone.

### Change 1: F9 uses .ca-quantized axes; CALIB is chase-drive-only

`SetAnalogFromVector` now branches on `s_chaseDriveActive`. Chase keeps reading `s_driveCamRight/Down` (the empirical pair, written by CALIB phase 1/2). F9 reads `s_camRight/Down` (the manual-nav quantized pair, set at field load from .ca). F9's handlekeys block no longer initiates CALIB — it sets `s_calibPhase = 3` unconditionally.

Why this fixes the bghall_1 BAT: CALIB phase 1 injects `lX=+1000` for 24 ticks and measures the resulting walkmesh delta. When the player is wedged against geometry at drive start, the engine doesn't move them, dist < 5 fails the phase, and `s_driveCamRight` keeps its default `(1,0)`. On rotated-camera fields this means F9 reads wrong axes and steering produces the kb-vs-analog disagreement Aaron's log showed. The quantized .ca axes can't fail this way: they're set once at field load from a constant data file, with deterministic 2D normalization + det correction + 90-degree snap.

Chase-drive deliberately stays on empirical calibration. Per the chase doc Finding #10, the empirical path is the verified-working axis source for rotated-camera chase fields (domt5_1 etc.).

### Change 2: Draw points use talkRadius, save points stay walk-onto

New split:
- `ENT_SAVE_POINT` → `arriveDist = 30.0f` (unchanged; the save crystal only activates when the player overlaps it).
- Runtime-entity targets (`entityIdx >= 0`) including NPCs, Objects, and runtime-entity Draw Points → read engine-set talkRadius, clamp to 60-unit floor.
- JSM-injected Draw Points (`entityIdx <= -300`, no runtime entity slot, e.g. Fire Cavern 'drpoint') → `arriveDist = 120.0f` (matches GPS_ARRIVE_DIST's default).

### Change 3: INF exit gateways auto-cross like trigger lines

- At drive start, handlekeys finds the raw INF gateway in `s_gateways[]` whose `destFieldId` matches the dedup-catalog entry and is nearest to the player, then seeds its line endpoints into `s_driveCrossLine*` and sets `s_driveCrossLineActive = true`.
- `UpdateAutoDrive`'s crossing block condition widened from `s_chaseDriveActive && s_driveCrossLineActive` to just `s_driveCrossLineActive`. Chase-drive AND F9 gateway both flow through the same code path.
- F9 trigger-line targets unchanged.

### Files changed

- `src/ff8_accessibility.h` — version bump
- `src/field_nav_autodrive.inl` — `SetAnalogFromVector` branches on `s_chaseDriveActive`; crossing block condition widened to `s_driveCrossLineActive` only
- `src/field_nav_handlekeys.inl` — F9 drive-start replaces CALIB initiator with unconditional `s_calibPhase=3`; arrival block splits save vs draw vs runtime-entity; new gateway-crossing setup block
- `CHANGELOG.md`, `DEVNOTES.md`, `NEXT_SESSION_PROMPT.md`

---

## v0.17.5.4: World Map polling stuck-at-startup fix (shipped to GitHub as commit `b54fa75`)

Narrow bug fix. One function changed.

`IsOnWorldMap()` in `world_map_segments.inl` now requires BOTH signals to agree:

1. `FF8Addresses::pGameMode` resolved AND equal to `MODE_WORLDMAP` (= 2).
2. THEN the scene flag at `WM_SCENE_FLAG` reads 0.

If either fails, returns false. SEH-wrapped on both reads. Prior `IsOnWorldMap()` returned true at startup before pGameMode resolved, causing world-map polling to fire and clobber post-drive TTS with "No locations available."

---

## v0.17.5.3: Autodrive failure + TTS audit logging (LOCAL, BAT'd -- diagnostics retained)

No behaviour changes. Two new log channels.

**1. `ScreenReader::Speak` -- TTS audit trail.** Every actually-spoken utterance logged to `ff8_mod.log` as `ScreenReader: [TTS] "<text>"` (with `(interrupt)` suffix when applicable). Permanent diagnostic; every "what did the mod say?" question from now on is answerable from the log.

**2. `[drive] REFUSED` -- autodrive validation-fail log.** When the `\` key's validation gate fails, log the full context: field name, catalog index/size, target entityIdx/gatewayIdx, target type and name, whether `GetEntityPos` succeeded for the player and target, and the player's own entity index.

Files changed: `src/ff8_accessibility.h`, `src/screen_reader.cpp`, `src/field_nav_handlekeys.inl`.

---

## v0.17.5.2: Funnel waypoint pruning (shipped to GitHub as commit `6dc080a`)

Reduces SSFA micro-corner waypoint count without changing the path's macro shape or wall avoidance properties. BAT confirmed nearly 5x waypoint reduction on the test case.

| Field | A* tris | Pre-prune wp | Post-prune wp | Sweeps |
|-------|---------|--------------|---------------|--------|
| bghall_1 | 11 | 11 | 5 | 7 |
| bg2f_2 | 46 | 46 | 10 | 37 |

New function `PruneCollinearWaypoints` called at the end of `FunnelPath`: for each interior waypoint B with neighbors A and C, compute perpendicular distance from B to segment AC. If `perpDist < PRUNE_PERP_EPSILON = 50.0f`, remove B. Sweep-to-stable (cap 100 sweeps as safety bound). First and last waypoints preserved. Real corners (perp dist > 50) untouched. 50-unit epsilon is below typical FF8 wall thickness (~100+ units).

---

## v0.17.5.1: GPS announcement hysteresis (BAT'd, hysteresis works)

Point release on top of v0.17.5. Quantization architecture untouched; only the announcement cadence changes.

In `field_nav_gps.inl::UpdateGPS`, replace the v0.17.0 sector-change + step-change + waypoint-force cadence with cardinal-change-only-with-hysteresis:

- Announce ONLY when `dirIdx != s_gpsLastDirIdx` AND the new value has held steady for 500ms (`GPS_DIR_HYSTERESIS_MS`).
- Step-count changes: log only, no speech.
- Waypoint advances: log only, no speech.
- Nearby/in-range one-shots: unchanged.

Mechanism: two new statics `s_gpsPendingDirIdx` and `s_gpsPendingDirSince`. New cardinal -> candidate. Same candidate held 500ms -> promote and speak. Different candidate before 500ms -> reset timer.

---

## v0.17.5: Load-time 90-degree quantization (BAT'd, architecture works)

Replaces the v0.17.4 passive calibration loop with a single load-time quantization step.

Architecture:
1. At field load, parse .ca file into 2D-normalized camRight/camDown (v0.17.0.1 -- unchanged).
2. Det convention check: if det(camRight, camDown) > 0, negate camDown (v0.17.4 -- unchanged).
3. **Quantize camRight to nearest 90-degree world cardinal; derive camDown via (x,y) -> (y,-x) rotation (v0.17.5 -- new).**
4. Mirror to drive-private pair (unchanged).

Why it works: v0.17.3 BAT clean samples showed engine RIGHT direction = world cardinal on every tested field (bghall_1 CA angle 7.8 -> world east, bghall_4 23.8 -> east, bg2f_1 65.4 -> north, bg2f_2 60.5 det-fixed -> north +5-11deg residual, bgroom_1 -62.5 -> south). FF8 engine appears to use 90-deg-quantized camera matrix for DIJOYSTATE2 -> walkmesh transform. Quantizing at load makes mod prediction match engine actual on every clean field.

Removed from v0.17.4: `ObsCalibrateAxes()`, `s_fieldCalibratedManual` flag, observer hold-state reset at field load, observe.inl include order change.

---

## v0.17.4: Det fix + passive self-correcting calibration (BAT'd, calibration removed in v0.17.5)

Det fix worked perfectly (bg2f_2 classroom now navigable). Passive calibration was unstable -- accepted curve-walked samples that reversed correct cals. Aaron's question "is calibration really necessary?" exposed that engine appears to use 90-deg-quantized camera axes, and v0.17.5 ships the quantization approach instead.

**Fix 1: det convention check at CA load.** After v0.17.0.1 2D normalization, check `det = camRight.x*camDown.y - camRight.y*camDown.x`. If positive (left-handed projection — axis1 points world-up after normalization), negate camDown to force det=-1.

**Fix 2: passive self-correcting calibration.** New `ObsCalibrateAxes()` called from `ObserveArrowResponse()`. Stricter gating: heldTicks >= 30, delta magnitude >= 100, dot(predicted, measured) >= 0.5 (60° cone). When passing: compute signed theta = atan2(cross, dot), rotate BOTH s_camRight and s_camDown by theta. (Removed in v0.17.5.)

---

## v0.17.3: Passive arrow-response observer (BAT'd, diagnostic complete)

Pure diagnostic instrumentation. BAT captured 66 single-arrow observations across 5 fields. Per-field rotation analysis:

- bghall_1: -7.8° (in 22.5° sector tolerance)
- bghall_4 (elevator): -23.8° (borderline)
- bg2f_1 (2nd-floor hall): +24.6° (wrong cardinals)
- bg2f_2 (classroom): det=+1.0 (left-handed, UP/DOWN exactly opposite)
- bgroom_1 (dorm): -27.5°

Key learning: rotation is uniform across all four arrows within a field (stdev <0.5° on clean samples), so a single clean observation is enough to fully calibrate.

Observer gates: no auto-drive, no chase-drive, player entity detected, no dialog open, exactly one arrow held. Throttled to one sample per 1.5s. Logs `[NAV-OBSERVE]` lines with predicted vs measured axes and divergence angle.

---

## v0.17.2: Camera-axes state separation + diagnostic logging (BAT'd, state separation confirmed)

BAT result: state separation works as designed. Hypothesis A (calibration corruption) is definitively ruled out. But cardinals on `bg2f_1` and the classroom were still wrong, confirming hypothesis B (CA-vs-engine field-specific mismatch). Need v0.17.3 observer to gather divergence data before designing the fix.

**Manual-nav pair (`s_camRightX/Y, s_camDownX/Y`):** set once at field load by `HookedFieldScriptsInit` from CA values. Never written by auto-drive.

**Auto-drive private pair (`s_driveCamRightX/Y, s_driveCamDownX/Y`):** NEW. Mirrors manual-nav pair at field load. Overwritten by `[CALIB]` phase 1/2 writes. Read only by `field_nav_autodrive.inl::SetAnalogFromVector`.

**Diagnostic tag (`s_camAxesSource`):** NEW `const char*`. Set to `"ca-file"` or `"identity"` at field load.

---

## v0.17.1: Path-aware GPS direction (BAT'd, path logic confirmed working)

v0.17.1 BAT (2026-05-17 17:32) RESULT: path-aware logic CONFIRMED WORKING. Funnel produced 11 and 38 waypoints across two GPS sessions; waypoint advance fired through the sequence; overshoot detection caught three sub-arrive-distance passes correctly. But manual-nav cardinals at the GPS sessions used corrupted camera axes (see v0.17.2 above).

Approach: Run A* on the walkmesh from player triangle to target triangle (reusing `ComputeAStarPath` in `field_nav_pathfinding.inl`), funnel-smooth the corridor (`FunnelPath`), and announce the cardinal toward the NEXT waypoint rather than the final destination.

Key implementation choices:
- **GPS-private waypoint buffer.** `s_gpsWaypoints[MAX_GPS_WAYPOINTS=64][2]` in `field_nav_gps.inl`. Separate from the shared `s_waypoints[]` used by `UpdateAutoDrive`.
- **Save/restore around the A* call.** `BuildGpsPath` snapshots shared state before A* runs and restores after copying the funnel result into the GPS buffer.
- **Waypoint advance threshold = 200 units** (`GPS_WP_ARRIVE_DIST`).
- **Overshoot detection** mirrors auto-drive.
- **Cadence: waypoint advance forces announcement.** The v0.17.0 minimum-interval floor (3s) is broken by direction changes; v0.17.1 also breaks it on waypoint advance.
- **Trigger-line targets** pass `skipTriggerIdx = -(entityIdx + 200)` to `ComputeAStarPath`.

DO NOT TOUCH BuildGpsPath / AdvanceGpsWaypoint / save-restore of shared waypoint state — those work as designed.

---

## v0.17.0.1: CA normalization fix (SHIPPED LOCAL, PROVEN BY BAT)

v0.17.0 BAT log surfaced the bug immediately: `bghall_1` `[NAV-PROJ-INIT]` showed `camRight=(0.991,0.135) camDown=(0.044,-0.330)`. camRight has 2D magnitude 1.0, camDown only 0.333 — asymmetric scale biases `atan2(sD, sR)` toward east/west.

`.ca` axes are stored as 3D unit vectors. On tilted cameras (most Balamb Garden interiors), axis1's 3D magnitude is dominated by its Z component (depth into floor); the XY projection is short. v0.17.0 divided by 4096 and used the raw XY components verbatim. Fix: normalize the 2D projection of axis0 and axis1 to unit length before writing `s_camRight/Down`.

The chase auto-pilot's empirical calibration in `field_nav_autodrive.inl` already does this correctly — it divides the measured walkmesh delta by `cdist`. So chase fields with the chase calibration always had correct normalized values; manual nav with CA-derived values didn't, on tilted-camera fields.

Fix in `field_nav_fieldscripts.inl`, after `LoadCameraAxes`:

```cpp
float r2x = (float)s_cameraAxes.axis0[0] / 4096.0f;
float r2y = (float)s_cameraAxes.axis0[1] / 4096.0f;
float r2len = sqrtf(r2x*r2x + r2y*r2y);
float d2x = (float)s_cameraAxes.axis1[0] / 4096.0f;
float d2y = (float)s_cameraAxes.axis1[1] / 4096.0f;
float d2len = sqrtf(d2x*d2x + d2y*d2y);
if (r2len > 0.001f && d2len > 0.001f) {
    s_camRightX = r2x / r2len; s_camRightY = r2y / r2len;
    s_camDownX  = d2x / d2len; s_camDownY  = d2y / d2len;
}
```

`[NAV-PROJ-INIT]` log line now says `source=ca-file-normalized` (was `ca-file`).

---

## v0.17.0: Manual nav direction projection (SHIPPED, partially correct)

Bug 2 from the v0.16.5.2 BAT triage. Manual GPS direction announcements were correct on default-camera fields and inverted on fields with rotated cameras. Root cause: the GPS direction code computed the world-space bearing `atan2(dx, dy)` from raw entity coordinates and labeled it with screen-relative names.

**The two-system gap.** Two camera-axis storage variables weren't reconciled:
- `s_camRightX/Y, s_camDownX/Y`: used by direction-computation code paths. Reset to identity on every field load. Previously populated ONLY by chase auto-pilot's empirical calibration.
- `s_cameraAxes`: populated at field load by `LoadCameraAxes`. **Was being read into the struct but never consumed.**

v0.17.0 plugs the leak. After `LoadCameraAxes()` returns successfully, `s_camRightX/Y/DownX/DownY` are derived from `s_cameraAxes.axis0` and `axis1` (normalized by /4096) and written into the same statics the chase calibration would use.

**.ca file format** (38 bytes per setting):
- bytes  0–5: axis0 (int16 x, y, z) — **screen-right vector in world XY basis**
- bytes  6–11: axis1 (int16 x, y, z) — **screen-down vector in world XY basis**
- bytes 12–17: axis2 (int16 x, y, z) — screen-forward (depth), unused for nav labels
- bytes 18–29: camera world position (3 × int32)
- bytes 30–31: zoom (int16)
- bytes 32–37: padding

Axis vectors are int16 fixed-point; divide by 4096 for normalized unit vectors.

Forward projection:
```
screenRight = dx*camRightX + dy*camRightY     // camRight = axis0[0..1]/4096
screenDown  = dx*camDownX  + dy*camDownY      // camDown  = axis1[0..1]/4096
```

**Cardinal vocabulary** `GPS_DIR_NAMES[]` in `field_navigation.cpp` is now `north / northeast / east / southeast / south / southwest / west / northwest`. Per Aaron, cardinals map directly to arrow keys (north = up, east = right, south = down, west = left).

**GPS cadence rewrite.** Old cadence: time-interval-based, every 3 s / 1.5 s / 0.8 s. Produced bursts on long stretches and continuous spam in final approach. New cadence: in nearby zone GPS Updates are silent; outside it, Update fires only when cardinal sector changes OR step count changes AND `GPS_ANNOUNCE_INTERVAL_FAR` (3 s) has elapsed.

**Diagnostic logging.** `[NAV-PROJ-INIT]` at every field load, `[NAV-PROJ] start/update` at GPS announcements.

---

## v0.16.4 battle_tts_ewm.inl split (shipped commit `5d16179a` 2026-05-17 18:31 UTC)

Pure mechanical split of `src/battle_tts_ewm.inl` (91.79 KB monolith) into a 2.17 KB slim shell + nine sub-`.inl` files. Behavior byte-for-byte identical to v0.16.3.

Include chain (dependency-ordered, included textually from the slim parent inside `namespace BattleTTS`):

```
state → gf_patch → gf_effect → bp_diag → atb_hook → dispatch → ffnx → diag → update
```

File sizes: state 8.4 KB, gf_patch 8.9 KB, gf_effect 6.9 KB, bp_diag 17.1 KB, atb_hook 12.3 KB, dispatch 5.7 KB, ffnx 9.7 KB, diag 12.0 KB, update 13.8 KB, slim shell 2.2 KB. Largest sub-file `bp_diag.inl` at 17.1 KB — well under the 60 KB warn line.

`state.inl` declares every static (must come first). `update.inl` calls helpers from `gf_patch.inl` and `diag.inl` (must come last). The `atb_hook.inl` folds in the EWM lifecycle (`EWM_LoadConfig`/`SaveConfig`/`PollToggle`/`InstallHook`) because `EWM_InstallHook` installs `HookedATBUpdate`.

`battle_tts.cpp` unchanged. Statics declared in `state.inl` referenced by `battle_tts.cpp` itself (e.g. `OnBattleEnter` resets `s_gfSnapValid`/`s_gfAutoArmDone`/`s_tgtDiagStage`; `Initialize`/`Shutdown` use `s_gfVEHHandle` for VEH register/unregister) remain visible via file-scope `static` across the textual-include boundary. `deploy.bat` unchanged.

### v0.16.4 BAT result (clean)

Build succeeded, runtime verified. Every EWM subsystem fired its expected log lines, confirming the split is byte-for-byte functional:

- `[EWM] ATB hook @ 0x004842B0 — MH_OK` (atb_hook.inl)
- `[EWM] GF timer hook @ 0x004B0500 — MH_OK` (gf_patch.inl)
- `[GF-BP] VEH registered: handle=0x0F3E74F8` (bp_diag.inl)
- `[GF-EFFECT] Resolved battle_magic_id at 0x01D99A68 (poll mode)` (gf_effect.inl)
- `Initialized v0.16.4 (EWM=ON, ATB=OK, GF=OK, FFNx=FAIL, PATCH=OK, BT=deferred)` — FFNx=FAIL is pre-existing v0.10.77 prologue-padding mismatch, not a regression
- `[GF-PATCH] APPLIED: 0x004B04B4 = 0xC3 (RET)` at battle start, `RESTORED ... (MOV)` at end
- `[EWM] ATB capped/released`, `[FRZ-DIAG]`, `[POST-REL]`, `[EWM-DIAG]`, `[DMG-DIAG]`, `[ACT-DIAG]` all firing
- v0.13.57 ATB exact-value restore semantics preserved (POST-REL lines show entities at non-max values like 6615/12000, not all converged at 11999)

### Open question from v0.16.4 BAT (not v0.16.4-related)

In the second battle, Zell summoned Ifrit and his audio description didn't play. The log shows:

- `[GF-EFFECT] Resolved battle_magic_id at 0x01D99A68 (poll mode)` ✓ install
- `[HP-CHECK] GF substitution: slot0 -> Ifrit: 1650 HP.` ✓ HP display switched
- GF cast animation, damage popup, kill announce all fired correctly
- **No `[GF-EFFECT] Animation detected: effectId=200 gfIdx=2 slot=0` line anywhere** — `PollBattleMagicId` never observed magicId transitioning to 200, so `GfAudioDesc::OnGFAnimationStart(200)` was never called
- `ff8_mod.log` is silent for `[GF-AD]` runtime lines (only the 18 init load lines)

Two reasons this isn't likely v0.16.4-related:
1. `gf_effect.inl` is byte-for-byte extracted; poll body and statics are identical to v0.16.3
2. The install resolution line fires, proving `EWM_InstallBattleEffectHook` ran and set `s_battleMagicIdAddr` correctly

Hypothesis: engine wrote `200` to `0x01D99A68` for fewer frames than the mod thread's poll period covered, or skipped the value entirely (writing the damage formula directly without staging through `battle_magic_id`). Intermittent, not refactor-correlated.

**Diagnostic candidate if it recurs**: add a 1-second `[GF-EFFECT-POLL] magicId=N prev=M` heartbeat to `PollBattleMagicId` to see what values the engine *does* write during a GF cast. Backlog item, queued in DEVNOTES.md.

---

## v0.16.1 chase_auto_pilot refactor + X-ATM092 chase chapter (v0.16.1 → v0.16.1.4, shipped commit `5c08a1a` 2026-05-16)

Five logical versions, one GitHub push. The v0.16.1 refactor pulled the 108 KB `chase_auto_pilot.cpp` monolith into 8 `.inl` files + a 6.47 KB slim parent + an `_history.h` archive, mirroring v0.16.0's world_map template. The v0.16.1.1/.2/.3/.4 follow-ons started as diagnostic builds for an unrelated chase regression on doopen2a (Town Square 5) and ended with the X-ATM092 chase auto-pilot chapter closed end-to-end.

### v0.16.1.4 BAT result -- chase closed

2026-05-16 21:54:20. doopen2a position trace from `ff8_nav_data.log`:

```
t=0       (-915, -176)   spawn          tri 52
t=0+      (-738, -392)                  tri 51
t=0+      (-574, -647)   MAX EAST       tri 50
t=0+      (-577, -678)                  tri 49
t=1       (-615, -889)                  tri 48  [stage 1 active]
... (south, drifting west) ...
t=5       (-936, -3653)  EXIT TRIGGER   tri 154
```

Field cleared in ~5 seconds of movement, no `[CBF] PASS` lines, no catches. Chase continued cleanly through dotown_3 (~14s, MODE_DIRECTION south + path-find handled the NW dead-end cluster the party started in), dotown_2 (~13s), dotown_1 (~11s) to the chase climax.

**Empirical numbers vs v0.16.1.4 threshold-derivation predictions:**

| Metric | Predicted | Actual | Notes |
|---|---|---|---|
| Spawn | `(-974, -166)` | `(-915, -176)` | spawn drifts +59 east run-to-run |
| Max east X | `-629` | `-574` | auto-pilot got 55 more east than SE-rate model predicted |
| Min battleyarou distance | 639 | 582 | still 82-unit margin outside TALKRAD=500 |
| Min kani distance | n/a | 166 | inert confirmed |
| Exit position | `(-905, -3447)` | `(-936, -3653)` | 31 west, 206 south of prediction; same SW corner |
| Total field time | ~4.25s | ~5s | well under 7s TALKRAD expansion |

### Findings shipped as part of the chase chapter

1. **Kani is inert on doopen2a.** Confirmed twice: Aaron's manual run (162 units, no catch), v0.16.1.4 auto-pilot (166 units, no catch). All pre-v0.16.1.4 commentary attributing doopen2a catches to kani was wrong.
2. **Battleyarou has a static TALKRAD=500 catch zone around its JSM-init position `(0, -744)`.** Confirmed by v0.16.1.3 BAT (453 units from `(0, -744)`, caught at t=1s).
3. **Battleyarou's TALKRAD expands to 700 at the 7-second mark** on doopen2a, per `[TALKRAD] CHANGED @0x1F8: 500 -> 700` at 21:10:45 in Aaron's manual run with context bytes shifting `@21E 0->2 @244 0->3` indicating a script state transition. The chase mechanic is "outrun an expanding catch radius."
4. **`ff8_nav_data.log` is the silent goldmine for chase debugging.** Logs every player triangle change as `[timestamp] COORD field tri X Y ...` regardless of auto-pilot state -- including manual runs. The `ChaseAutoPilot` per-tick log in `ff8_field.log` only fires when the auto-pilot is engaged, but `ff8_nav_data.log` always logs movement. This is how the v0.16.1.4 fix was derived from Aaron's manual chase trace.

### Open question (no blocker)

Why did v0.16.1.2's t=3 catch fire at `(-870, -1900)` -- 1447 units from battleyarou's static position, far outside any TALKRAD? The `[CBF] PASS` caller was battleyarou (`entityPtr=0x0188CA04`). Aaron's manual run passes through similar positions without catch. Hypothesis: battleyarou has a velocity- or motion-vector-based catch in addition to the static proximity zone, and the auto-pilot's faster motion or different direction vector trips it on the direct south path. The v0.16.1.4 east-first / west-corridor route empirically avoids it. The mechanism remains uncharacterized; revisit only if a future regression makes it relevant.

### Diagnostic logging from v0.16.1.1 (still in place)

`ReadBattleyarouPosition` SEH-guarded helper in `chase_auto_pilot_io.inl`; `by=(X,Y) bydist=N` per-tick suffix added to the four `ChaseAutoPilot::tick` log paths in `chase_auto_pilot_update.inl`; `_ReturnAddress()` capture on `[CBF] PASS` line in `chase_battle_freeze.cpp`. Useful baseline for any future chase regression.

### v0.16.1 file layout (shipped in v0.16.1.4)

- `chase_auto_pilot.cpp` slim parent (6.47 KB), namespace block + `.inl` chain + public API.
- `chase_auto_pilot_history.h` (19.15 KB, `#if 0`, NOT in build).
- Eight `.inl` files: `state` (15.67 KB) -> `route` (21.01 KB) -> `io` (4.3 KB, +0.7 in v0.16.1.1) -> `helpers` (6.81 KB) -> `diag` (5.23 KB) -> `bridge` (7.06 KB) -> `engage` (11.27 KB) -> `update` (16.6 KB, +0.4 in v0.16.1.1).

Largest `.inl` still well under the 60 KB CI warn line.

### Sub-version breakdown

- **v0.16.1** -- pure-refactor split of `chase_auto_pilot.cpp` (108 KB -> 6.47 KB slim parent + 8 `.inl` + history). Mirrors v0.16.0 world_map.cpp template. CI allowlist entry removed. BAT (2x): caught the party on doopen2a ~4 seconds after entry regardless of party position. Aaron directed diagnose-forward instead of version-bisection -- v0.16.1.1 is the diagnostic build.
- **v0.16.1.1** -- chase doopen2a catch diagnostic. Three pure-diagnostic additions: `ReadBattleyarouPosition`, ` by=(X,Y) bydist=N` per-tick log suffix, `_ReturnAddress()` capture on `[CBF] PASS`. BAT: battleyarou reads `(0,0)` on every chase field (entity UNUSE'd, not tracked); `bydist` increases as party moves away from origin. Surfaced the domt2_1 5s stuck (`pos=(3,-1603)`, `moveDist=160` then `moveDist=0`, 3 velocity-stuck skips over 5s) as a salient observation -- later confirmed by Aaron as the **scripted X-ATM092 landing animation**, immutable cinematic, not a pathing bug. Diagnostic logging retained going forward.
- **v0.16.1.2** -- funnel wall-parallel portal COLLAPSE fix in `field_nav_pathfinding.inl::FunnelPath`. Wall-parallel portals now emit a midpoint waypoint instead of being skipped, with `SKIP_WALL_PARALLEL_LEGACY = false` toggle as Fix A fallback. BAT: COLLAPSE fired correctly on domt2_1, party reached the new waypoint cleanly, but the 5s stuck persisted unchanged (because it's the cinematic, see above). Doesn't help the chase but doesn't regress anything; COLLAPSE code stays in for other walkmesh cases.
- **v0.16.1.3** -- doopen2a config switched from `MODE_TARGET (-952, -3800)` to `MODE_STAGED_DIRECTION` with `kStages_doopen2a[]` table per Aaron's sighted-player recipe. Threshold initially wrong: `Y<-1500` required 1.94 seconds of SE motion to reach the (assumed-safe) `X>=-185` boundary, but that trajectory walked the party THROUGH battleyarou's static 500-unit zone at t=1s. BAT caught at `(-446, -821)`, 453 units from `(0, -744)`.
- **v0.16.1.4** -- threshold corrected to `Y<-631` based on Aaron's manual chase BAT trace from `ff8_nav_data.log`. End of stage 0 at approximately `(-629, -631)` -- same X as Aaron's max-east excursion, ~146 unit safety margin (actual 82 in BAT). BAT clean.

### Lessons that carried forward

1. **`ff8_nav_data.log` is the silent goldmine.** Logs every triangle change regardless of auto-pilot state. Use whenever Aaron's manual play is the ground truth.
2. **Aaron's domain knowledge is ground truth, but his recipes need empirical verification.** The "SE several steps then S to the exit gateway" recipe pointed in the right direction but with the wrong magnitudes (max east X=-629 was much less east than the model predicted).
3. **Multiple catch sources on one field may not all be active.** doopen2a's JSMScan listed both kani and battleyarou as catch entities; only battleyarou is active. The TALKRAD log only fires for battleyarou's entity pointer, which was a missed clue across multiple BATs.
4. **Per-field problems require per-field analysis.** The robot's position resets at every field boundary. Each chase field is its own self-contained proximity-catch problem.
5. **MODE_STAGED_DIRECTION threshold math needs empirical SE-rate calibration per field.** The doopen2a camera vectors (`camRight=(0.927,-0.376)`, `camDown=(-0.097,-0.995)`) plus FF8's non-normalized analog produce a faster diagonal than the normalized-speed model predicts. v0.16.1.3 BAT measured 528 east + 716 south per second for SE; v0.16.1.4 BAT measured even higher (the party reached X=-574 by Y=-647, implying ~700 east per second).

---

## v0.16.0.x — world_map.cpp split + Parts B/C + CI guard + drive fixes (commits ending at `1e3d7fd5` 2026-05-16)

v0.16.0 / .0.1 / .0.2 / .0.3 shipped as four sequential commits to GitHub.

### v0.16.0 — world_map.cpp split + Parts B/C + CI guard

10-file refactor of the 222.80 KB / 4452-line v0.15.13.2 `src/world_map.cpp` monolith. Now a slim parent + 9 `.inl` files plus `world_map_history.h` archive.

Three behavioral safety nets accompany the structural split:

- **Part B** (arrival.inl): off-target-distance cap on arrival.
- **Part C** (drive.inl): `StartAutoDrive` checks `s_destPlannerEligible[locIdx]` before calling `PlanDrivePath`. Ineligible destinations skip the planner entirely; UpdateAutoDrive's non-planner branch handles them with v0.11.11-era simple-coord steering.
- **ComputePlannerEligibility** (planner.inl): runs once at Initialize, after LoadTriggerZones. Sets `s_destPlannerEligible[LOCATION_COUNT]`. Logs per-catalog YES/NO classification. 11/38 eligible in master catalog.

CI guard (`.github/workflows/safety-checks.yml`) `source-file-size-check` job: 60 KB soft warning, 80 KB hard fail. Existing oversized files allowlisted with v0.16.x ticket numbers.

#### File split layout

`state.inl` first (types/state). Then `segments.inl` (coord math + archive I/O), `trigger_data.inl` (38 wmsetus.obj Section 8 programs + LogTriggerPrograms), `catalog.inl` (`s_locations[]` + LOCATION_COUNT + BFS reachability), `announce.inl`, `planner.inl` (A* + ComputePlannerEligibility), **`drive.inl`** (StopAutoDrive + StartAutoDrive + UpdateAutoDrive), **`arrival.inl`** (ResolveDeferredArrival; AFTER drive.inl because it calls StopAutoDrive), `keys.inl` (PollKeys). Slim `world_map.cpp` opens namespace, includes all 9 .inl files in dependency order, defines `Initialize` / `Update` / `Shutdown` / `Poll`. `world_map_history.h` holds the pulled-out v0.14.31-v0.15.13.2 changelog narrative (NOT in build path, `#if 0` wrapper). `MAX_LOCATIONS = 64` in state.inl decouples state-array sizing from `LOCATION_COUNT`.

### v0.16.0.1 — "Position unavailable" fix + Part C locIdx fix

(1) `GetWorldMapPosition_Active` guards vehicle-pos overwrite with `if (vx != 0 || vy != 0)`; stale `s_lastVehicle=37` (VEH_CAR) no longer clobbers valid foot DWORDs. `[VEH-POS-FALLBACK]` diagnostic log added (later quieted in v0.16.0.3). (2) Part C gate switched from `s_destPlannerEligible[catIdx]` (BFS-filtered) to `[locIdx]` (master-table).

### v0.16.0.2 — Fire Cavern works

Three fixes from v0.16.0.1 BAT log. **(1)** Poll() replan-gate honors planner-eligibility via new `s_drivePlannerEligible` flag. On world-map re-entry after random-encounter pause, replan now gated; eligible destinations replan via PlanDrivePath, ineligible stay on simple-coord steering (no closest-active-region fallback misroute). **(2)** Part B two-tier cap: 2500 planner-eligible / 8000 geometric-trigger. Refined-coord capture self-corrects to actual trigger position on first arrival; subsequent visits fall back inside the strict 2500 cap. **(3)** Fire Cavern refined-coord hardcoded at (30326,-29221) in Initialize() alongside Balamb Town.

**v0.16.0.2 BAT result summary** (15:33:49 -> 15:40:32):

Fix 3 (Initialize hardcode) -- VERIFIED at module init:
```
[INIT] Refined entry default: Balamb Town (12896,-26711)
[INIT] Refined entry default: Fire Cavern (30326,-29221)
```

Fire Cavern drive -- CLEAN ARRIVAL in 7 seconds. Used refined entry (30326,-29221) instead of catalog (36864,-28672). `Geometric-trigger destination Fire Cavern (locIdx=37, planner-ineligible) using simple-coord steering`. No `[PLAN-DEBUG]` walk. Arrival: `dist=66, lastPos=(30260,-29221), planned=0, fieldId=0x0088, fieldName='', elapsed=547ms`. Refined coord auto-updated from (30326,-29221) -> (30260,-29221) (66 units west, the actual approach-trigger entry point).

Balamb Town drive -- 4 encounter cycles, all resumed correctly via planner. Encounters at dist=12926, 10436, 9351, 5157, 1496 -- each `Paused via game-mode (MODE_SWIRL)` -> `Replanning after world-map re-entry` -> normal `[PLAN-DEBUG]` walk -> resume. Final arrival: `dist=65, fieldId=0x006A, fieldName='bcgate_1', elapsed=563ms`. Refined coord re-captured at (12894,-26776) -- 2 units off the prior hardcode.

Fix 1 (Poll() replan-gate) verification: the gate's `if (s_drivePlannerEligible) PlanDrivePath else log+keep-simple-coord` ran the planner-eligible branch 4 times for Balamb Town. The planner-ineligible branch did not fire because Fire Cavern arrived too quickly to encounter a battle -- but the conditional is structurally exercised and the eligible-side behavior matches expected.

Fix 2 (two-tier 2500/8000 cap) verification: structurally in place but unused this BAT. Fire Cavern arrived at dist=66 (well inside strict 2500), Balamb Town arrived at dist=65 (also inside). The 8000 cap is a safety net for future geometric-trigger destinations on their first visit before refined-coord capture.

### v0.16.0.3 — VEH-POS-FALLBACK log transition-only

`world_map_segments.inl` -- fallback log now uses a function-local static to fire only when `s_lastVehicle` changes from the last-logged value. No heartbeat. Eliminates ~1800-line floods like v0.16.0.2 BAT produced; one line per distinct vehicle byte that triggers the fallback. **BAT confirmed: zero fallback lines in 733-line log** (vs ~1800 in 2262 for v0.16.0.2).

Bonus: this BAT exercised the planner-ineligible branch of v0.16.0.2's Fix 1. Fire Cavern drive hit a random encounter, re-entered the world map, and Poll() correctly logged `[DRIVE] Planner-ineligible destination -- keeping simple-coord steering, not replanning` before reaching final-approach arrival at dist=66. Both branches of Fix 1 (eligible via Balamb Town, ineligible via Fire Cavern) are now empirically verified. Bonus 2: Fire Cavern A's engine `fieldName='bdview1'` captured this run (the populate race resolved in time), confirming fieldId 0x0088 ↔ 'bdview1'.

### v0.16.0.x open follow-ups (carried to backlog, not blockers)

1. **`fieldName=''` race at Fire Cavern arrival.** 7-second drives can beat the field-name pointer's populate timing in Part B's snapshot. **DIAGNOSTIC LOG ONLY, audio is fine** -- FieldAnnounce reads the pointer hundreds of ms later and announces correctly. Backlog action: either retry briefly in Part B before logging, or accept (fieldId alone is sufficient).
2. **Fire Cavern A fieldId/fieldName mapping** -- confirmed in v0.16.0.3 BAT: `fieldId=0x0088`, engine `fieldName='bdview1'`. Useful data for the FieldAnnounce display-name catalog audit backlog item (`src/field_display_names.h`). Confirm 0x0088 ↔ 'bdview1' ↔ "Fire Cavern A" mapping is consistent end-to-end.

---

## Sessions 66+ (2026-04-27 → 2026-05-05) — Scan TTS chapter (v0.14.50 → v0.14.82) + World Map saga (v0.14.83 → v0.14.90.3)

Two consecutive multi-build sagas, both pushed to GitHub. The detailed version-by-version build narratives live in the GitHub commit messages — see commits below — so this archive entry is a topical index rather than a duplicate of the commit text.

### Scan TTS chapter (v0.14.50 → v0.14.82) — closed at GitHub commit `7c7afdf3`

Auto-announce + interactive number-key UI for the Scan spell. 33 builds. Major beats:

- **v0.14.50–58:** action-layer detection via popup hook (`sub_48D200` text_id=0x06 val=0x32 = ID 50). Watchdog cancel for spurious "no effect" announces (Cure-on-asleep, Sleep-on-immune). 30s action-layer lock + sub_84F860 dispatcher hook (both later retired).
- **v0.14.59–61:** UX redesign — silent action-layer + auto-announce on `sub_B687C0` first-fire (window-render). Single-channel SAPI mode to eliminate dual-voice overlap.
- **v0.14.66-diag → v0.14.71:** type-label capture via `sub_47EC70(99)+sub_47EC70(36)` byte-decode (Fly Monster / Earth Monster / etc.). Established that not every FF8 enemy has a type label (Fastitocalon has no rendered type — engine never calls the pair). FF8 text decode rules: uppercase encoded byte = decoded char + 4, lowercase encoded = decoded - 2.
- **v0.14.72:** **architectural lesson** — sub_47EC70 hook conflict between scan_tts.cpp and battle_tts_victory.inl. MinHook silently fails with `MH_ERROR_ALREADY_CREATED` when two installers race for the same address; the loser is identifiable only via the failure log. Resolution: single canonical hook in victory module, ScanTTS observes via public `HandleBattleText(textId, result)` forward call. Pattern: cooperating modules MUST share one MinHook installer per address.
- **v0.14.73–v0.14.74:** elemental affinity (keys 6/7/8) + 8-stat split (offensive on key 5 / defensive on key 6). Required reading `Plan & Research Documents/Scan spell deep research results.md` for the FF8 800-anchored u16 elemental scale (NOT FF7-style buckets — v0.14.73 shipped wrong, fixed in v0.14.73.1). **Lesson: always consult `Plan & Research Documents/` BEFORE picking an interpretation when one exists.**
- **v0.14.74.1–v0.14.76:** BENT_STATUS_RESIST_BASE offset hunt. Deep research's `+0x4C` hypothesis was wrong (v0.14.74 BAT showed alternating 169/251 garbage). [SCAN-STRUCT] 121-byte hex dump diagnostic on Grat + T-Rexaur + Tonberry confirmed the actual offset is `+0x80`. v0.14.74.2/3 fixed cross-battle stale-data leaks (s_prevBattleMagicId reset, enemy slot fingerprint snapshot at battle exit).
- **v0.14.79:** popup-hook condition relaxed to match BOTH text_id 0x02 AND 0x06 — different cast contexts use different text_ids. Prior v0.14.55-78 only matched 0x06; repeat scans in the same battle were silently dropped.
- **v0.14.80–82:** chance-based weakness tiering. Used the FF Wiki Magic-cast formula `chance = Magic/4 - Spirit/4 + 100 - byte` with assumed player Magic=30. v0.14.81 shipped 60% threshold; v0.14.82 relaxed to 50% after nightsolo-canon cross-reference revealed three vulnerabilities at 53–57% being incorrectly dropped (Bite Bug Sleep, Caterchipillar Sleep, Fastitocalon Darkness). Symmetric two-tier resistance announce on key 9 (`Resists` 100-199, `Immune to` 200+).

**Architectural lessons captured into DEVNOTES.md "Key learnings & principles":**
- Two MinHook installers on same address silently fail (v0.14.72 lesson).
- `cdecl(byte)` engine functions leave garbage in upper bits of ECX — must mask `slotIndex & 0xFF` (v0.14.57 lesson).
- `.inl` files are included INSIDE `namespace BattleTTS {` — cross-namespace forward decls placed inside `.inl` files resolve as nested (v0.14.55 namespace trap).
- Default argument values may appear only ONCE per translation unit (v0.14.57 C2572).
- Popup hook (`sub_48D200`) as action-layer cue: filter by `text_id=0x06 && (value & 0xFF)==spell_id` — reliable across Magic-menu / Draw-Cast / Magic-Stock paths and view modes.
- Always consult `Plan & Research Documents/` before interpreting any FF8 engine data field.

### World Map saga (v0.14.83 → v0.14.90.3) — closed at GitHub commits `aef75aac` (v0.14.83→85.3), `0b06ab1` (v0.14.86→90.2), `683f1531` (v0.14.90.3)

Restoration of features lost in the v0.14.24 build damage / v0.14.31 partial recovery, then forward into auto-drive. 14 builds across three sub-chapters.

**Sub-chapter 0 — regression fix (v0.14.83 → v0.14.84):** WorldMap::HandleKeyPress was orphaned during the v0.14.31 recovery (defined in .cpp but never declared in .h, never called). Nav keys (-/=/Backspace) silently dead since v0.14.24. Fix: replaced with PollKeys() invoked from end of Poll(). Vehicle-change spam from locomotion-byte transients (Ragnarok 4→8→12→...→60 over ~15s) silenced via canonical-mode whitelist guard. v0.14.84 added `\` placeholder + restoration roadmap.

**Sub-chapter 1 — catalog reachability filter (v0.14.85 → v0.14.85.3):** wmx.obj polygon-format BFS walker. **Lesson: trust authoritative deep-research docs over past-chat code fragments.** v0.14.85's flat-stride parser came from a past-chat fragment but was structurally wrong — wmx.obj segments have a 68-byte header + 16 variable-length blocks at offsets specified in the segment header, polygons inside blocks. Correct walker: 195 land + 573 ocean = 768 segments, recognizable FF8 continents in the [TERRAIN] grid dump. Catalog rewritten with 38 canonical entries from `Plan & Research Documents/World Map Location Coordinates Research Findings.md` (ff8-speedruns coords matching the runtime player-position address). v0.14.85.2 added vehicle-class-change rebuild trigger. v0.14.85.3 dropped the unvalidated mode 4 = Ragnarok mapping (was Claude's guess; Aaron's save has no Ragnarok); IsCanonicalLocomotion whitelist {0, 3, 6, 31, 32-40, 48, 50}; GetBfsRuleClass returns 0/1/2 (land-only / ocean-allowed / no-filter).

**Sub-chapter 2 — auto-drive (v0.14.86 → v0.14.90.2):** `\` key triggers compass-following drive to nearest catalog entry. Reconstructed from past chats v0.11.05–v0.11.10 via `conversation_search`. keybd_event injection (worldmap input pipeline is separate from field input — fake gamepad doesn't reach it). Bearing-relative steering (3 zones: ahead <18°, diagonal 18-45°, side >45°). Sweep search for narrow entrances (alternating turn-walk, 6 phases). Persist-through-battle (target captured by VALUE at StartAutoDrive, survives catalog rebuilds). Refined-coord empirical capture on first arrival (parallel `s_refinedX/Y/Has[]` arrays). v0.14.90 added 4-poll locomotion debounce. **v0.14.90.2 lesson:** distance-based arrival sidesteps mode-register timing races — `s_driveLastDist < 1500 at exit` is robust because battles fire anywhere but field entries only happen near targets.

**Sub-chapter 2 hotfix #6 — animation-byte suppression (v0.14.90.3):** every world-map re-entry post-battle fired three spurious `Vehicle change:` announcements over ~3s. Root cause: locomotion byte cycles through canonical values (0/3/6) during the camera zoom-in animation, each held ~1s — well past the 4-poll debounce. Fix: time-gate `CheckVehicleChange` for `WM_ENTRY_DEBOUNCE_MS = 3000ms` after every world-map entry. **Lesson: animation-residue byte noise can mimic real engine state for hundreds of milliseconds.** Frame-scale debounce is insufficient when the noise values are themselves canonical and held long. The discriminator must be a different signal entirely (in this case, recency of world-map entry). Non-canonical byte at window expiry kept prior `s_lastVehicle` (graceful fallback — doing nothing is safer than committing the unknown). BAT-passed Tue 2026-05-05 18:12 with Chocobo drive Balamb-Garden→Balamb-Town across two pause/resume cycles.

**Architectural lessons captured into DEVNOTES.md "Key learnings & principles":**
- Even canonical locomotion values can be transient — debounce alone is insufficient when noise values match the real value-set; need a different discriminator (e.g. recency of state transition).
- Distance-based arrival sidesteps mode-register timing races. Don't read `pGameMode` at the moment of `IsOnWorldMap` flipping false — register hasn't transitioned yet.
- Refined-coord empirical capture beats pre-deep-research trigger-table investigation when data acquisition is slower than runtime observation.
- Recovery-from-build-damage gotcha: audit lifecycle wiring (Install/Poll/Reset hook calls) AND feature parity against past BAT logs. The v0.14.31 recovery only restored what triggered linker errors, leaving HandleKeyPress orphaned.
- GitHub API write tools (`github:create_or_update_file`, `github:push_files`) count as pushes. Using them bypasses the user's local clone, which then diverges from origin without anyone realizing. Always: Claude provides version + commit description; Aaron's utility does the actual push.

### GitHub state at chapter close

`main` HEAD = `683f1531` (v0.14.90.3). Local in sync. Recovery story for the failed first push attempt of v0.14.90.3: a prior Claude session had used `github:create_or_update_file` to put v0.14.90.2 on GitHub directly, leaving Aaron's local clone parented on `aef75aac` instead of `0b06ab1`. Fix was `git fetch origin && git reset origin/main` (mixed reset preserved working tree), then re-run push utility — clean fast-forward.

---

## Session 65 (2026-04-26) — v0.14.23→v0.14.32 build recovery + damage-timing regression fix

### Context

A prior Sonnet session damaged the codebase across ~14 source files while attempting to add diagnostics to `battle_tts.cpp`. Aaron reported the v0.14.23 BAT failed at compile time. Investigation revealed catastrophic state:

- Helper functions (`BattleSpeak`, `BattleSpeakEvent`, `GetEntityHP`, `GetEntityMaxHP`, `GetEntityBlock`, `IsEntityKO`, `CountActiveEnemies`) had been removed from `battle_tts.cpp` but were still required by every `.inl` file.
- `s_ewmEnabled` was duplicated — declared in both `battle_tts.cpp` and `battle_tts_ewm.inl(948)`, causing C2374 redefinition error.
- 9 `.inl` files existed on disk but were not `#include`d anywhere: `battle_status.inl`, `battle_tts_dmgbp.inl`, `battle_tts_dmg_popup_hook.inl`, `battle_tts_dmg_read_bp.inl`, `battle_tts_dmg_render_hook.inl`, `battle_tts_roi_calib.inl`, `battle_tts_spritepool.inl`, `battle_tts_sprite_spawn.inl`, `battle_tts_validate.inl`. That's why `POPUP_RECORD_STRIDE`, `POPUP_TABLE_BASE`, `POPUP_TRACK_MAX`, `s_pollScreenshotDirEnsured`, `s_pollFrameCounter`, `Validate_AnnounceEvent`, `RoiCalib_OnSwapBuffers`, `DrainDeferredTextSpriteLog`, `TriggerImmediateHPFlush` were all undefined at compile time.

### v0.14.24 — Architectural recovery from GitHub HEAD v0.13.61

1. Restored helper functions to `battle_tts.cpp` before the `.inl` includes (matches GitHub HEAD v0.13.61 pattern).
2. Removed duplicate `s_ewmEnabled` from `battle_tts.cpp` (canonical declaration is in `battle_tts_ewm.inl`).
3. Reordered + expanded include chain: helpers → diagnostics → hp → ewm → menu → sprite → status → noeffect → sprite_spawn → validate → dmgbp → dmg_popup_hook → dmg_read_bp → dmg_render_hook → spritepool → roi_calib → screenshot → victory.
   - `noeffect` moved BEFORE `sprite` was reverted: `sprite.inl` calls `NoEffect_QueueAnnouncement`, but `noeffect.inl` also depends on sprite. Resolution: forward-declared `NoEffect_QueueAnnouncement` in `battle_tts.cpp`'s cross-.inl forward-decl block, then included `noeffect.inl` AFTER `battle_status.inl`.
   - `roi_calib` placed BEFORE `screenshot` despite roi_calib.inl's stale comment claiming "AFTER screenshot" — `HookedSwapBuffers` in screenshot.inl calls `RoiCalib_OnSwapBuffers()` with no forward declaration.

### v0.14.25–0.14.30 — Cross-module namespace forward-decl architectural fix

Multiple files (dinput8.cpp, ff8_addresses.cpp, screen_reader.cpp, log.cpp, config.cpp, title_screen.cpp) were missing namespace forward declarations for Log/ScreenReader/Config/NavLog after Sonnet's edits. Created `src/mod_forward_decls.h` with all cross-module forward decls and added `#include "mod_forward_decls.h"` to those 6 files. dinput8.cpp also got missing module includes (ff8_addresses.h, field_dialog.h, field_navigation.h, fmv_audio_desc.h, fmv_skip.h, game_audio.h, world_map.h) and an inline TitleScreen forward decl block (no title_screen.h exists).

### v0.14.31 — Linker error fixes

Fixed 5 linker errors (build now succeeds):
1. WorldMap::Update + Shutdown bodies were deleted from `world_map.cpp` by Sonnet — restored as thin wrappers (Update() calls Poll(), Shutdown() resets module state).
2. `dinput8.cpp` TitleScreen::Initialize forward decl was `bool` but title_screen.cpp defines `void` — fixed dinput8.cpp.
3. ScreenReader::Speak / SpeakChannel2 unresolved across 9 .obj files. **MSVC includes return type in name mangling** (`?Speak@ScreenReader@@YAX...` for void vs `YA_N...` for bool). Inline forward decls said `void Speak` but screen_reader.cpp defines `bool Speak`. Changed all 9 inline decls to `bool`: world_map.cpp, name_bypass.cpp, game_audio.cpp, fmv_skip.cpp, fmv_audio_desc.cpp, field_dialog.cpp, field_navigation.cpp, battle_tts.cpp, menu_tts.cpp.

### v0.14.31 BAT — Build OK, but 4 runtime regressions surfaced

1. **Damage announcement timing regression —** firing at animation END instead of impact-time. `Logs/ff8_battle.log` (timestamp 21:54:21–22:07:27) shows the explicit measurement: `[ROI-LIVE-SHADOW] event 23 SUMMARY: ... yellowLeadVsAnimFlag=13219ms`. The visible damage led the announcement by 13.2 seconds. ZERO `[DMG-RENDER]` or `[DMG-RENDER-HOOK] MH_OK` lines anywhere in the file. The announcements all said `trigger=anim-done` (the v0.13.90 fallback path) instead of `trigger=dmg-render` (the v0.14.10 production trigger).
2. Status effect spell miss/no-effect not announced (cast Blind on Biggs/Elvoret, no announcement).
3. Magic/GF submenu auto-announce inconsistent.
4. Number key 2 announced GF (Shiva) details instead of Squall's HP, no GF being summoned.

### v0.14.32 — Damage timing regression fix (bug 1 only)

**Root cause confirmed via log read:** the v0.14.24 build recovery rebuilt `battle_tts.cpp` from GitHub HEAD v0.13.61 (which predates the v0.14.4 / v0.14.10 damage hooks). The `.inl` files for the new hooks were correctly wired into the include chain, but the corresponding `Install()` and `Reset()` calls were never added to `OnBattleEnter()`. So the impact-time render hook on `sub_5068B0` was sitting in source code, fully written, but never installed at runtime. With no hook installed, `s_lastDmgRenderTick` stayed 0, the impact-time trigger in `PollHPChanges` never fired, and the announcement fell back to the v0.13.90 anim-flag-fall path — the OLD pre-v0.14.10 "announce at animation end" timing.

**Fix applied to `src/battle_tts.cpp` OnBattleEnter():**
- Added `DmgPopupHook_Reset()` and `DmgRenderHook_Reset()` to the reset block (after HP tracking reset).
- Added `DmgPopupHook_Install()` and `DmgRenderHook_Install()` to the install block (after `EWM_InstallBattleEffectHook`). Install functions self-guard on `s_dmgPopupHookInstalled` / `s_dmgRenderHookInstalled` flags so calling every battle is safe.

### v0.14.32 BAT — Bug 1 fully resolved

Log evidence (timestamp 22:32:33–22:34:04):
- `[DMG-RENDER-HOOK] MH_OK — hooked sub_5068B0` install line present at battle entry
- `[DMG-POPUP-HOOK] sub_48EF80 hook @ 0x0048EF80 — MH_OK` install line present at battle entry
- `[DMG-RENDER] slot=0x01D28344 dmg=76 active=0x08 tick=35620828` fired during damage event
- `[HP-TRACK] Damage popup rendered via sub_5068B0 — impact-time trigger dmg=76 (was 62 ms after anim-up)`
- `[VALIDATE] kind=kill slot=3 value=76 trigger=dmg-render tts="Grat takes 76 damage. Defeated."`
- `[POPUP-SPAWN] Immediate HP flush completed - anim-flag bypass active` (fallback correctly suppressed)
- `[ROI-LIVE-SHADOW] event 4 SUMMARY: ... yellowLeadVsAnimFlag=-109ms` (announcement landed essentially synchronized with visible damage; compare v0.14.31's 13219ms gap)

Bugs 2, 3, 4 deferred to next session — likely same regression class (missing Install calls).

---

## Session 83 (2026-04-24) — v0.13.78 observational hook rework

### Static walk of sub_487DF0 VM opcode table + text render pipeline

With the Assembly_Files.zip extracted and in /home/claude/asm_files, did a comprehensive static walk answering Q1 from the session 82 handoff ("Is sub_495280 actually the right target?").

**Q1 answered definitively: sub_495280 is a TEXT writer, not a sprite spawner.** It writes null-terminated FF8-encoded byte strings into a 16-slot × 48-byte ring buffer at 0x01D29034. Confirmed from its 13-instruction body, plus the fact that the `ent` values seen in session 80's `[SPRITE-POLL] NEW` events (0x01D29034, 064, 094, 0C4 — 48-byte stride) are pointers STORED inside action-announce records (bytes[12-15] per v0.13.74 record layout), not the sprite records themselves.

### New discoveries in session 83

1. **sub_4952F0 is a SECONDARY text ring buffer writer** at 0x01D29334 (16 × 48 bytes, rotating counter at 0x01D2A279). Called from VM opcode handlers at 0x004884E8 and 0x0048854B (style byte 1 and 3 respectively).

2. **sub_495330 is an int-to-digit-string formatter** (5-digit, divide-by-10 with magic number 0xCCCCCCCD, strips leading zeros, looks up kernel.bin glyph codepoints via sub_47EC70(0xb)). 5 callers: 0x004883FC (status duration), 0x0048D5DF (action-announce MP cost inside sub_48D200), and three in an unknown function at 0x00490???.

3. **sub_482C90 is a sprite slot allocator** managing a linked-list pool of 16 slots × 16 bytes at 0x01D28C04 (metadata) + 0x01D28C44 (data). Each slot's first dword is a polymorphic update/render callback. 6 allocation sites register 5 distinct callbacks: 0x47E030 (general task spawner), 0x48ACD0 (flag manager), 0x48AC60 (one-shot text render), 0x48AC90 (timed text render), 0x48E620 (complex per-frame update).

4. **VM opcode 0x3C at 0x00489F30 is "apply damage to HP":** reads 2 bytes from script, sign-extends, adds to `battler[slot].field_18`. This is the ground-truth HP delta hook point — deferred to v0.13.79+ pending BAT3 results.

5. **VM opcode table decoded:** 61 entries at 0x0048A0B8. 39/61 decoded directly by re-encoding the garbled disassembly; remaining 22 identified by their exit addresses (jmp 0x487eba). Status-text composition opcode at 0x00488235 uses sub_47EAF0 (name lookup) + sub_47EC70 (kernel string) + sub_47E970 (text lookup) + sub_495210 × 3 (concat) + sub_495280 (ring write) + sub_47E220 (task 8 enqueue).

### Three hypotheses for floating damage NUMBER path

- **(A)** Damage numbers ARE in the 0x01D280C4 table with kind=0x01 (physical), renderer reads damage from HP-delta state rather than record bytes[4-5].
- **(B)** Damage numbers go through the unknown function at 0x00490??? that chains sub_495330 + sub_495280 + sub_48E5F0.
- **(C)** Damage numbers are rendered by the graphics pipeline directly from battler state — no gameplay hook catches them.

v0.13.78's observational hooks (sub_495280 + sub_4952F0 with retaddr capture) should resolve which of A/B/C is correct based on BAT3 log retaddrs.

### v0.13.78 implementation details

- Both hooks use the **deferred-log pattern**: hook body is SEH-free and Log-free, only does memory reads of static globals + fixed 24-byte raw-byte copy from the text arg. Atomically claims a ring-buffer slot via InterlockedIncrement, fills fields, _WriteBarrier, sets `valid=1`.
- Drainer runs from HookedSwapBuffers (battle_tts_screenshot.inl) where SEH and Log are safe. Drains up to the current claim, skipping invalid slots (left for next frame). Logs first-30 unconditionally, dedups thereafter by (writer, retaddr, first-8-bytes-of-text).
- Both hooks install from OnBattleEnter. State reset per battle via ResetSub495280HookState().

### Addresses added to the canonical list

0x00488235, 0x0048834C, 0x0048847A, 0x004884E8, 0x0048854B, 0x00489F30 (VM opcode handlers). 0x004952F0, 0x00495330 (text helpers). 0x01D28C04, 0x01D28C44 (sprite slot pool). 0x01D29334, 0x01D2A279 (secondary text ring).

---

## Session 82 (2026-04-23/24) — Minimal sub_495280 hook diagnostic

### v0.13.76 — First sub_495280 hook (hung)

Added MinHook on sub_495280 @ 0x00495280 with first-N unconditional context log + dedup. Hook decoded `raw=47 5F 71 72 20 57 6A 63 63 6E 00` → `"Cast Sleep"` correctly from retaddr=0x0048D772 (action-announce caller inside sub_48D200 family). Game thread tick rate then degraded to zero over ~2 seconds (GF-HOOK counter 60 → 11 → 0). Hung without crashing. Suspected cause: SEH setup/teardown inside hook body on arbitrary game threads, combined with Log::Battle from those threads.

### v0.13.77 — Minimal diagnostic hook (passed)

Shrank hook to `s_origSub495280(text); InterlockedIncrement(&s_sub495280CallCount);` — no SEH, no Log, no decoder, no context snapshot. Full Grat battle (including Sleep cast) played to victory with no hang. Confirmed MinHook trampoline for sub_495280 is safe; the hang was in our logging/SEH path.

Two open questions at session 82 close: (Q1) Is sub_495280 the right target? (Q2) What specifically caused the hang?

### Session 81 open question resolution (carried into 83)

Session 81 found sub_495280 via direct asm search for 0x01D29034 (observed `ent` value from session 80 SPRITE-POLL NEW events). The connection was real at the text-writer level, but session 83 reframed the meaning: sub_495280 is the TEXT writer used by multiple systems including action-announce text and VM status-text opcodes, not the floating-sprite spawner per se.

---

## Session 81 (2026-04-22) — sub_495280 discovery via direct asm search

### Definitive find

Direct search for `0x1d29034` in the asm file hit at 0x004952A3 inside sub_495280 (17 callers per callxrefs). Walking the function confirmed it's a text-writer into a 16-slot × 48-byte ring buffer at 0x01D29034. Slot counter at 0x01D2A278 (byte, self-wraps via AND 0xF on entry).

**sub_495280 full body (0x00495280–0x004952CC):**
```
0x00495280: mov  al, [0x1D2A278]      ; read slot counter (ring buffer idx)
0x00495285: push esi
0x00495286: and  al, 0xF                ; mask to 4 bits — 16-slot wrap
0x00495288: mov  esi, [esp+8]           ; esi = arg1 = source text ptr (null-terminated)
0x0049528C: mov  [0x1D2A278], al        ; normalize slot idx back
0x00495291: mov  eax, [0x1D2A278]
0x00495296: and  eax, 0xFF
0x0049529B: mov  dl, [esi]               ; first byte
0x0049529D: lea  eax, [eax + eax*2]     ; 3*slot
0x004952A0: shl  eax, 4                   ; 48*slot  ← 48-byte stride MATCH
0x004952A3: lea  eax, [eax + 0x1D29034] ; dest = 0x1D29034 + 48*slot  ← observed ent values
0x004952A9: mov  ecx, eax
0x004952AB: mov  [ecx], dl                ; write byte
0x004952AD–BB: loop: read, write, inc, loop until null terminator
0x004952BD: mov  cl, [0x1D2A278]
0x004952C3: pop  esi
0x004952C4: inc  cl                       ; advance slot counter
0x004952C6: mov  [0x1D2A278], cl         ; (wraps via 0xf mask on next entry)
0x004952CC: ret
```

Session 81 reframed by session 83: sub_495280 is TEXT, not sprite. Session 83 found sub_4952F0 as second text writer, plus the real sprite slot allocator sub_482C90 at 0x01D28C44.

### False leads ruled out in session 81

1. sub_487DF0 effect-VM — deep research confirmed battle-effect animation VM, not sprite spawner. Opcode table at 0x0048A0B8 handles palette/texture/SFX/vibration. Session 83 revisited and found TWO opcodes that DO write text (0x00488235 status text, 0x004884E8 / 0x0048854B simple text); one (0x0048834C path B) allocates a sprite slot via sub_482C90.
2. sub_485DC0 — walked prologue, found status-clear loop (iterates battler-struct status bytes at [slot*0xD0 + 0x1D27BC8], calls sub_486B40 for each, clears). Not a sprite spawner.
3. sub_483940 — started walking, found RNG-heavy path with internal jump table at 0x00483C40 (12 entries). Likely animation dispatcher, not spawner.

### Session 81 prep walk — function containing 0x0048594E

The player-action caller retaddr into sub_48E830. Partially mapped to `sub_485610` with inventory of its sibling calls: sub_47E080(5) (anim trigger), sub_483D60 (target mask), sub_485EC0 (resolver), sub_486DC0 OR sub_486E00 (outcome class), sub_485DC0 (unknown), sub_483940 (unknown), sub_4877F0 (spell result dispatch), sub_48E830 (action-announce commit), sub_486A10 (status-slot lookup).

**Session 83 revision:** sub_486DC0 and sub_486E00 turned out to be RANDOM TARGET PICKERS (not damage calculators as originally guessed). DEVNOTES "outcome-class A/B" annotation was wrong. The real damage calc is deeper in the VM.

### Second popup counter at 0x01D28DF0

Immediately after the call 0x48e830 at 0x00485949, the function increments a separate byte counter at 0x01D28DF0. Session 81 flagged this as a candidate "second sprite table." Session 83 walked all references to 0x01D28DF0 and found they're all internal to the action-announce system — no separate table. Confirmed dead-end.

---

## Session 80 (2026-04-19) — sub_48E830 confirmed action-announce-only

### Dual-hook BAT + frame-level sprite observer

Versions through session: v0.13.72 added sub_48E830 hook in new battle_tts_sprite_spawn.inl with `[SPRITE-SPAWN-SEQ]` logging. v0.13.73–.74 added SwapBuffers-based frame-level poll of the 0x01D280C4 record table with `[SPRITE-POLL] NEW/KIND/DESPAWN` transitions. v0.13.75 added adjacent-slot record dumps + auto-screenshots on NEW transitions.

### BAT matrix (single encounter, Grat x1, 16:26:46–16:28:50)

10 events captured via `[SPRITE-POLL] NEW`:

| # | Action | kind | val | style | ent | HP delta |
|---|---|---|---|---|---|---|
| 1 | Draw | 0x06 | 40 | 0x0B | 0x01D29034 | 0 |
| 2 | Draw-Cast Sleep | 0x06 | 40 | 0x0B | 0x01D29064 | 0 |
| 3 | Draw-Cast Silence | 0x06 | 41 | 0x0B | 0x01D29094 | 0 |
| 4 | Draw-Cast Berserk | 0x06 | 46 | 0x0B | 0x01D290C4 | 0 |
| 5 | Magic Berserk hit | 0x02 | 7 | 0x0B | 0x01CF907D | -185 |
| 6 | Berserk-Squall Atk hit | 0x01 | 0 | 0x0D | 0x00000000 | -159 |
| 7 | Quistis self-whip MISS | 0x01 | 0 | 0x0D | 0x00000000 | 0 |
| 8 | Berserk-Squall Atk hit | 0x01 | 0 | 0x0D | 0x00000000 | -118 |
| 9 | Enemy Grat atk | 0x08 | 2 | 0x0D | 0x00000000 | -70 |
| 10 | Quistis Atk kill | 0x01 | 0 | 0x0D | 0x00000000 | -98→168 |

### Definitive conclusions from session 80

1. sub_48E830 + sub_48D200 are confirmed paired-finalizer for action-announce popups only. Both fire 1:1 on every player action (12 calls each per battle). Neither handles the per-result floating damage/Miss/status-icon sprite.
2. The real sprite render path writes somewhere outside the 0x01D280C4 20-byte-stride table (or writes without incrementing 0x01D280C0). The frame-level poll sees only action-announce records, never a damage-number record.
3. Physical attack misses are NOT announced. Existing sub_4877F0 kind=4 a3=0x9 path catches spell/Draw-cast misses only. Aaron's Quistis self-whip-miss was silent in TTS.
4. Enemy damage flows through a different caller (retaddr=0x00489FC0) than player actions (retaddr=0x0048594E). Both route through sub_48E830, so both are action-announce-class.

### Screenshot system confirmed working (v0.13.74+)

SwapBuffers hook captures on every `[SPRITE-POLL] NEW` and every kind=4 dispatcher fire (400ms delayed, 10/battle cap). Session 80 produced 10 poll_NEW_*.png + 6 kind4_*.png in Logs/screenshots/. Hit/miss pair for visual comparison: poll_NEW_162823_746_...slot0_kind01_val0.png (Quistis self-miss) vs poll_NEW_162843_213_...slot0_kind01_val0.png (Quistis hit on Grat for 168).

### Cross-session revision: sub_486DC0 / sub_486E00 annotation

Session 80 prep walk annotated these as "outcome-class A/B calculators." Session 83 disassembly showed they are actually RANDOM TARGET PICKERS (for live-ally / live-enemy selection). Treat the old annotation as wrong.

---

## Sessions 75–76 (2026-04-17) — EWM Cap→Freeze + Principle Validation

### v0.13.57 (session 75) — Cap→Freeze + Damage-Anim Diagnostic

**Conceptual change.** Prior versions used a "cap at max-1" sandwich: entities advanced naturally during freeze windows but clamped at max-1. During long holds (GF summons, damage windows) everyone converged at 11999, erasing the natural ATB race. Changed sandwich to true freeze: restore ATB to exact pre-sandwich value, so entities stay at whatever value they had when freeze engaged. Same for GF loading counter.

**Files touched:**
- `src/battle_tts_ewm.inl` — removed cap-at-max-1 logic from POST-FREEZE for both ATB and GF loading counter; new `EWM_FormatATBSnapshot()` + `EWM_PollDiagnostics()` called each frame from `EWM_UpdateBattle`. New log tags: `[DMG-DIAG]`, `[ACT-DIAG]`, `[FRZ-DIAG]`, `[POST-REL]`.

**BAT result:** Aaron reported "I didn't notice any overlap, which was fantastic!" — the TTS-damage-overlap bug that sessions 69–74 chased is **resolved** by switching to freeze semantics + keeping the dispatch hooks from v0.13.55/56.

**Side observation:** Aaron noticed party getting 2–3 turns per enemy turn. Initially thought a bug, but analysis showed this is just 3v1 arithmetic + G-Soldier's slower speed. Both sides' ATB was correctly frozen during each other's actions (verified by ATB snapshots in log). Chose option 1: live with it (matches Wait mode economy).

### v0.13.58 (session 76) — Turn-Counter Diagnostic (first attempt)

**Goal:** Let Aaron A/B test EWM-on vs EWM-off turn ratios empirically to confirm freeze has zero impact on turn economy.

**Implementation:** `EWM_TrackTurnCount()` detects a turn start by watching each slot's ATB transition from high (>10000) to low (<2000). Called from inside `EWM_UpdateBattle`.

**Bug:** `EWM_UpdateBattle` is gated behind `s_initAnnounceDone && s_ewmEnabled && s_ewmHookInstalled`, so the `s_inBattle` transitions the tracker tried to detect weren't reliably visible. Reset-on-battle-start and summary-on-battle-end only fired on the first battle of a session. Counters ran continuously across all subsequent battles.

### v0.13.59 (session 76) — Turn-Counter Lifecycle Fix

**Architectural split.** Broke the tracker into three functions called from three different lifecycle points:
- `EWM_ResetTurnCount()` — called from `OnBattleEnter()` in `battle_tts.cpp`
- `EWM_TrackTurnCount()` — per-frame poll called from `BattleTTS::Update()` gated only on `s_inBattle`
- `EWM_LogTurnCountSummary()` — called from `OnBattleExit()` before `s_inBattle = false`

Each hook now fires exactly when it's supposed to regardless of EWM state or init progress.

**BAT result (10-battle A/B test):**

| | EWM ON | EWM OFF |
|---|---|---|
| Battles (with ≥1 enemy turn) | 3 of 5 | 4 of 4 |
| Party turns (aggregated) | 18 | 21 |
| Enemy turns (aggregated) | 8 | 9 |
| **Party:enemy ratio** | **2.25 : 1** | **2.33 : 1** |

Ratios within 3.5% of each other. 2v2 battles matched exactly (2.25:1 both). 1-enemy single-turn battles all landed at exactly 2.00:1 regardless of EWM state. **Conclusion:** EWM freeze has no measurable effect on turn economy. Principle validated empirically.

### v0.13.60 (session 76) — Turn-Summary Format Cosmetic Fix

**Bug found in BAT log:** Summary line printed `s7=257 (total=0)` — `s7` doesn't exist. `BATTLE_TOTAL_SLOTS=7` means enemy slots are s3–s6 (indices 3, 4, 5, 6). The format string included a spurious `s7` which was an out-of-bounds read of whatever happened to be in the adjacent memory. Ratio math was always correct (loop summed the right indices); only the per-slot display had garbage.

Fixed format string to print s3–s6 only. No behavior change; next battle logs will show clean summary. Ships silent — no further BAT needed.

### Key Learnings from Sessions 75–76

1. **The overlap bug's real root cause** was the cap's "converge at max-1" behavior, not a dispatch race. Dispatch hooks (v0.13.55/56) were necessary defense-in-depth but didn't solve it alone. Switching cap→freeze eliminated the simultaneous-tie-dispatch-at-release condition.

2. **EWM principle crystallized.** Memory #26 updated: "Enhanced Wait Mode retrofits FF8 into sequential turn-based — only ONE action/menu occurs at a time. ATB still races normally; whoever fills first goes first (no advantage, same turn economy as vanilla). During ANY action (menu, attack, GF, damage anim), ALL other ATB freezes. Gameplay-neutral — doesn't add turns or prevent enemy attacks, just sequences them for TTS. Preserve: (1) first-to-fill acts first, (2) no skipped turns, (3) natural ally/enemy ratio."

3. **Diagnostic lifecycle hooks beat inline state machines.** Version 0.13.58 tried to detect battle-enter/exit transitions inside the per-frame tick; v0.13.59 split the work across the battle lifecycle functions that already existed. Cleaner, more reliable, easier to read.

4. **Turn-counter detector is EWM-independent.** ATB high→low watch works whether the freeze sandwich is active or not, so A/B comparisons are apples-to-apples. Reusable for any future turn-economy question.

### v0.13.55–56 Outcome (retrospective)

Keep both dispatch hooks (`sub_483470` + `sub_482F80`) — they're doing their job at the dispatch layer even though the visible-overlap symptom was actually solved by cap→freeze at the ATB layer. The hooks provide defense-in-depth for edge cases (fast enemy attacks during transition-hold windows). Removing them is not planned.

---

## Sessions 69–74 (2026-04-16 → 2026-04-17) — Damage / Command-Menu Overlap Race

**7+ builds across 6 sessions.** Progressive attempts to stop enemy attacks from landing visually during the next player's command menu. Full per-session transcripts at `/mnt/transcripts/`; key decisions summarized:

### v0.13.51 (session 69) — Draw-submenu ATB cap hotfix
Unrelated to overlap bug but shipped same session. Three-edit fix: moved `s_inSubmenu` from menu.inl to hp.inl so ewm.inl can read it; extended `submenuOpen` check to `(menuPhaseDword >= 0x00400000) || s_inSubmenu`. Aaron BAT-confirmed 20:07:43: "It is working for suppressing ATB while moving around menus."

### v0.13.52 (sessions 70–71) — Dual-layer TTS ordering fix
**Layer 1** (ewm.inl): extended damage-TTS-hold block to cover five signals: `s_ewmHoldForDamageTTS`, `s_anyHpPending`, `s_damageAnimWasActive`, `*(uint8_t*)0x01D280C0 != 0`, `*(uint32_t*)0x01D27B00 != 0`. When any active AND `activeChar == 0xFF`, cap all slots with `excludeSlot=0xFF`.
**Layer 2** (menu.inl): added `s_deferredTurnBuf/Pending/Tick` state; turn-announce site defers if damage signals active; new `PollDeferredTurnAnnounce()` wired into `battle_tts.cpp::Update()` after `PollHPChanges`.
Result: TTS ordering correct, but enemy attack still lands visually during menu.

### v0.13.53 (session 72) — Post-turn grace + stale-deferral cancel
Added 1000 ms post-turn grace after activeChar transitions player→0xFF. `s_ewmPrevSeenActiveChar`, `s_ewmPostTurnGraceEnd`, `EWM_POST_TURN_GRACE_MS`. Also: `s_deferredTurnChar` tracks turn at defer time, cancels if activeChar changed. Dropped `ScreenReader::IsSpeaking()` gate in `PollDeferredTurnAnnounce` (caused 4-second-stale announcements during menu nav). Narrower race, still fails for GF-completion case.

### v0.13.54 (session 72) — Post-action cooldown
`EWM_POST_ACTION_COOLDOWN_MS = 500`, tracked via `s_ewmLastSignalTime`. `damageOrActionActive = postTurnGraceActive || anyActiveNow || postActionCooldown`. Narrows the window further but doesn't close it — fast actions where `[0x01D27B00]` flickers between 0 and 1 too briefly for mod thread to observe.

### v0.13.55 (session 73) — **Architectural shift**: MinHook on sub_483470
Cap-based approach abandoned in favor of direct dispatch intercept. `s_blockProcessReady = damageOrActionActive || (activeChar < 3)`. Hook returns early when flag set.
**Critical BAT result**: log showed `[DISPATCH] sub_483470: calls=6 blocks=6 block-flag=1` during bug window — hook caught every call, yet enemy attack still landed. Proved `sub_483470` is not the sole dispatch path.

### v0.13.56 (session 74) — Add sub_482F80 hook, awaiting BAT
Hypothesis: `sub_483470` + `sub_482F80` are a natural pair called back-to-back under the same engine gate. 483470 = queue/player-menu side, 482F80 = execution side. Added second MinHook, plus "passes" counter and always-on dispatch stats log for better diagnosis. See current DEVNOTES.md for full details.

### Cumulative key learnings from this saga
- Cap-based prevention has an unavoidable 16 ms race window at mod-thread poll boundaries
- `[0x01D27B00]` is not a persistent animation flag for fast actions
- Dispatch-layer hooks are the right intervention point (atomic, no race)
- Multi-function dispatch: FF8 appears to split queue processing and action execution across two functions
- `s_blockProcessReady` read is safe with `volatile bool` on x86 (no explicit synchronization needed)

---

## Session 67 (2026-04-16) — GF Submenu Fix + Code Cleanup + Draw Fix (v0.13.49→v0.13.50)

3 builds. Key changes:
1. **GF submenu detection build error**: `wasCommandMenu` undeclared after previous session's cleanup removed it. Fixed stray reference in `sm == 0x00` fallback block.
2. **Code cleanup — `EnterSubmenu()` helper**: Consolidated ~60 lines of duplicated list-building code across 4 detection paths (submenuMode, dword, subCursor, delayed entry) into single 20-line shared function. Removed dead variables: `s_submenuReentryNeeded`, `s_lastMenuPhaseForReentry`, `s_commandMenuPhase`, `s_prevMenuPhaseForSubmenu`. Removed 20-line commented-out phase fallback → 2-line note.
3. **Draw submenu false exit — FIXED**: `EnterSubmenu()` sets `s_prevSubmenuMode = 0x01`, and the submenuMode exit handler detects 0x01→0xFE as an exit. But Draw's internal phase transitions (target → spell list → Stock/Cast) briefly flip submenuMode to 0xFE, causing false exits. Fix: suppressed submenuMode exit when `s_submenuCommandId == 0x16` (Draw). Real Draw exits caught by cmdCursor change handler.
4. **EWM issues identified**: (a) Command menu interrupts damage TTS — need delay after attack animations. (b) Enemy attacks during menu navigation — ATB releasing during submenu transitions.

---

## Session 54 (2026-04-11) — Victory TTS phase detection + encoding fix (v0.13.24–26)

6 builds. Key breakthroughs:
1. **Fixed battle text encoding**: Was using kernel.bin encoding (0x02-0x1B=A-Z) which was WRONG. Battle text uses standard FF8 menu encoding (0x45-0x5E=A-Z, 0x5F-0x78=a-z). Same as enemy names in DecodeFF8String.
2. **Phase-to-text-ID mapping confirmed**: textID 22/23=EXP, 21=Items, 109=GF AP, 121=GF level-up, 127=ability learned. Smart BTXT logging (only log NEW/CHANGED text IDs) + F12 step markers mapped phases precisely.
3. **Phase-based victory TTS built**: BTXT hook sets phase flags → victory thread announces per-phase. GF AP ("GF received 2 AP.") confirmed working. Level-up detection via savemap EXP polling confirmed.
4. **sub_5348E0 NOT called during victory**: Hooked it (E9 JMP from FFNx), zero calls during mode 4. Victory code expands 0x0A control codes inline at 0x4A51F0.
5. **Memory-based TTS pivots corrected**: Aaron flagged twice that TTS must come from text renderer hooks, not memory address dumps. All-at-once memory reads leave blind players pressing through unannounced screens. Added to project memory #25.
6. **Victory screen layout documented from screenshots**: EXP shows Name/Lv/EXP Acquired/Current EXP/Next LEVEL. Items shown one-at-a-time. GF AP/Level-Up/Ability each in center box under "Raising GF" header.

## Session 53 (2026-04-11) — Battle text retrieval function identified (v0.13.14–22)

10 builds. BREAKTHROUGH: sub_47EC70 identified as get_battle_text(text_id) via disassembly. 261 call sites. Signature: looks up 16-bit offset at 0x1CF8B50+id*2, returns ptr to 0x1CF3E48+base+offset. 1774 calls during victory. Text IDs: 11 (per-frame graphics), 22/23 (one-time labels), 29/30/31 (per-frame character rows). Also identified sub_47EA30 (entity names), sub_5348E0 (ctrl code expansion), sub_4A37E0 (text display). tkmenu functions (4BD850/4BD630/4BD920) confirmed ZERO calls during battle — dead code.
> For current state, read `DEVNOTES.md` and `NEXT_SESSION_PROMPT.md`.
>
> **Versioning note**: Builds prior to v0.07.16 used the old format without the
> leading "0." (e.g. "v07.15" instead of "v0.07.15"). All versions in this file
> use the old format since that's what the logs and code said at the time.

---

## v07.xx: Menu TTS + Save Screen (2026-03-16)

### pMenuStateA Region Layout
- +0x00..+0x01: Transient input button flags (0x4000=UP, 0x1000=DOWN, 0x0010=confirm)
- +0x12..+0x19: Per-item animation/state flags (toggle 0x00↔0x40)
- +0x20: Active rendering callback index (per-frame oscillation — NOT cursor)
- +0x1CA: Rendering artifact. +0x1CE: Frame/tick counter.
- +0x1E6: **TOP-LEVEL CURSOR INDEX** (0-10)
- +0x1EC/+0x1ED: Submenu-related state

### Build Table v07.00–v07.15

| Build | Changes | Result |
|-------|---------|--------|
| v07.00 | MENUDIAG: menu_tts.cpp, dump pMenuStateA | pMenuStateA/B are input toggles |
| v07.01 | Per-frame WORD tracking at 20 offsets | +0x20 oscillates every frame |
| v07.02 | TTS using +0x20 as cursor | WRONG — repeats |
| v07.03 | Wide 512-byte scan, 500ms sampling | **FOUND CURSOR at +0x1E6** |
| v07.04 | Clean TTS using +0x1E6 | **WORKING** — all 11 items |
| v07.05 | Save slot entry diagnostic | Save screen leaves mode 6 |
| v07.06 | Global mode tracking | Title→Continue stays mode 1 |
| v07.07 | F12 2048-byte pMenuStateA scan | Only noise — cursor NOT here |
| v07.08 | F12 ff8_win_obj windows scan | Zero changes — not used |
| v07.09 | F12 MDT/GCW call counts | ~90-210 MDT/500ms confirmed |
| v07.10 | F12 GCW text capture | Hex captured, wrong encoding |
| v07.11 | Menu font decoder (sysfnt/Deling) | **CONFIRMED** — all text decodes |
| v07.12 | GCW text parsing "in use: Slot N" | Only fires after pressing X |
| v07.13 | Revised parsing, skip "Checking" | Same problem |
| v07.14 | GCW decode diagnostic | **KEY**: Text identical regardless of cursor |
| v07.15 | F12 4KB memory scan around pMenuStateA | Cursor NOT in pMenuStateA region |

### Save Screen Investigation Details (v07.06–v07.15)

**Save screen text rendering**: Uses standard menu_draw_text / get_character_width pipeline. GCW buffer fills to 1024 bytes/500ms with ~90-210 MDT calls per interval.

**GCW text patterns**: "LoadSlot 1FINAL FANTASY Slot 2FINAL FANTASY GAME FOLDER..." — repeats per frame, IDENTICAL regardless of cursor position.

**Memory scan results (v07.15)**: 4KB around pMenuStateA, 500ms intervals. Only 4 offsets changed: tick counter (-0x8), rendering toggles (+0x1FD, +0x1FE, +0x5E8). None correlated with arrow presses.

**Empirical glyph mapping** (v07.10): A=0x25, F=0x2A, I=0x2D, L=0x30, S=0x37, space=0x00.

---

## v06.xx: Field Navigation — Drive Reliability (2026-03-14 to 2026-03-15)

### Build Table v06.02–v06.22

| Build | Key Change | Result |
|-------|-----------|--------|
| v06.02 | Horizontal wall-parallel fix | Quistis+corridor reachable |
| v06.03 | A* trigger-line exemption for exits | Exits pathfinding works |
| v06.04 | Trigger exemption for events + preserve wp | Recovery doesn't lose waypoints |
| v06.05 | Trigger-safe recovery wiggle | Superseded by v06.06 |
| v06.06 | Edge-midpoint recovery replaces wiggle | Fires correctly, wall-stuck remains |
| v06.07 | Micro-nudge perpendicular to wall | Breaks wall contact, reveals oscillation |
| v06.08 | Overshoot detection + NavLog wiring | Fixes corridor oscillation |
| v06.09 | Stuck grace period + cancel threshold | Garden exploration test |
| v06.10 | CoordSample wired + progress detection | Recovery wp advance bug |
| v06.11 | wpIdx>2 guard attempt | Still too aggressive |
| v06.12 | genuineProgress gate | Fixed recovery escalation |
| v06.13 | PROJDIAG + camera analysis | Confirmed no projection needed |
| v06.14 | Per-field heading calibration | bg2f_1 NPC ARRIVED first time |
| v06.15 | Relaxed stuck thresh, remove arrow cancel | "Better than ever" |
| v06.16 | Simplified recovery pipeline | Clean re-path+nudge cycle |
| v06.17 | Corridor steering + wall bias | Per-tick edge midpoint target |
| v06.18 | Trigger proximity exemption | Target-side lines exempt |
| v06.19 | Narrow corridor bias reduction | Still oscillates |
| v06.20 | Wall bias disabled entirely | Corridors stable |
| v06.21 | Talk radius expansion (2.5×) | Elevator corridor NPC solved |
| v06.22 | Corridor steering trigger check | Final navigation build |

### MAJOR FINDING: Walkmesh = Entity Coordinates (2026-03-15)
.id walkmesh stores X,Y already in entity/screen space. No 3D→2D projection. Confirmed via CoordSample empirical data: mean diff=(14,34) std=(41,71). The .ca camera file is for background rendering only.

### v06.09 Garden Exploration Test Results
- Arrived: bggate_5 cdfield8, bggate_1 transitions (×2)
- Gave up: bg2f_1 NPC (×2), bggate_2 NPC (micro-oscillation)
- Open bugs: micro-oscillation (tri 126↔127), trigger-line crossing during drives

---

## v05.xx: Field Navigation — Entity Catalog + Pathfinding (2026-03-11 to 2026-03-14)

### Build Table v05.47–v05.96

| Build | Key Change | Result |
|-------|-----------|--------|
| v05.47 | SYM names + INF gateways | Working |
| v05.50 | pFieldStateBackgrounds | Working |
| v05.53 | Model-based character names | Quistis correct |
| v05.56 | SETLINE opcode hook | Coords at offset 0x188 |
| v05.61 | Y-axis fix (0x194 not 0x198) | Positions correct |
| v05.64 | A* walkmesh (FF7/PSX inline format) | Paths found, steering broken |
| v05.67 | Inverted heading mapping | First Arrived! |
| v05.70 | Screen filtering via SETLINE | 5 hidden back, 1 front |
| v05.76 | Heading: Y inverted, X direct | Left/right correct |
| v05.84 | Fake gamepad injection | Analog movement confirmed |
| v05.89 | get_key_state hook | **Analog steering works** |
| v05.94 | Vertex dedup + funnel fix | FindPortal succeeds |
| v05.96 | triB center cross product | Portal assignment fixed |

### Entity Classification (bgroom_1)
| Ent | Model | SYM | Actual | Catalog |
|-----|-------|-----|--------|---------|
| ent0 | -1 | Director | Controller | filtered |
| ent1 | 0 | Squall | Squall | player |
| ent2 | 8 | Squall_u | Quistis | "Quistis" |
| ent3-6 | 10-15 | various | NPCs | "NPC" |

### Screen Filtering Architecture
SETLINE trigger lines as screen boundaries. Cross product sign test. Layers: IsSeparatedByTriggerLine → screenFiltered[] → transition/event detection → gateway filtering → crossing detection.

---

## v05.97–v06.01: Navigation Pipeline Overhaul

| Build | Key Change |
|-------|-----------|
| v05.97 | Wall-parallel portal skip (epsilon=5.0) — too aggressive |
| v05.98 | Pre-skip nearby waypoints |
| v05.99 | Corridor-center steering bias — broke bgroom_1 |
| v06.00 | Tightened epsilon to 0.5 |
| v06.01 | New pipeline: wall-parallel skip + agent-radius shrinking + BFS island check |

---

## v0.07.93–v0.07.99: Interactive Objects, Exit Naming, INF Gateways

- **v0.07.99**: SET3 push stack diagnostic. `hasPshmCoords`/`pshmAddrX/Y/Z` added.
- **v0.07.98**: Interactive object classification infrastructure.
- **v0.07.97**: Model≥10 NPC fix (push-before-talk timing).
- **v0.07.95–96**: Exit naming, world map labels, redundant JSM exit suppression.
- **v0.07.93–94**: INF gateway parser (Deling format), dedup, catalog integration.

---

## Session 84 cont (2026-04-25) — v0.13.82 anim-flag edge fix + semantic miss correction

### Why this build existed

Two focused fixes based on BAT6 findings:

**1. Anim-flag screenshot was firing on the wrong edge.** v0.13.81 fired the audit screenshot on the 0→1 transition of `0x01D280C0`. BAT6 proved this was too early — the engine raises the anim flag when damage *computation* begins, before the damage-number sprite is composited. Verified by examining `sprite_animflag_172733_862_s3=-80.png` (Squall walking, no damage number visible). Fix: move `FireAnimFlagScreenshot()` from the 0→1 detection block to the three `FlushHPAnnouncements` branches (anim-done at 1→0 edge, anim-timeout, heal-timeout).

**2. "Miss on X" was semantically wrong for spell no-effect events.** BAT5/6 showed `sub_4877F0` kind=4 a3=0x9 fires for cases like Sleep-on-already-asleep and Silence-on-immune. The engine renders **nothing** on the target. We were saying "Miss on Grat" which doesn't match the visual. Fix: change TTS to "No effect on X" for that path. Keep `HookedPopupSprite` text_id=0xED saying "Miss" — that IS a real miss. New Validate kind: `no-effect` (was `miss-spell`). Log tag: `[SPELL-NOEFFECT]` (was `[SPELL-MISS]`). Physical miss kind stays `miss-popup` — unchanged.

---

## Session 85 (2026-04-25) — v0.13.83 timeout-based no-effect watchdog

v0.13.82 BAT exposed a third no-effect case the kind=4 hook misses entirely: **ally-already-status**. When a player casts a status spell on an ally who already has that status (e.g. Sleep on already-asleep ally), the engine short-circuits the spell-result path BEFORE calling `sub_4877F0`. The kind=4 hook never fires, no announcement plays. Verified at 15:23:56 in BAT log: `SPRITE-SPAWN-SEQ` fires for `sub_48E830`, `TEXT-SPRITE-SEQ` "Cast Sleep" fires, sprite despawns 8s later, NO `[SPELL-RESULT]`, NO `[STATUS-Q]`.

**Fix:** Hook the action-announce sprite spawn (`sub_48E830`, already hooked) for player magic casts (`retaddr=0x0048594E AND arg[1]==0x16`). Snapshot target HP at cast and start a 6-second per-frame watchdog. Activity indicators monitored: (a) HP delta, (b) status queue non-empty for target slot, (c) kind=4 hook already announced via `s_lastSpellMissAnnounceTick`. If ALL false at watchdog expiry → announce "No effect on <target>".

New file: `src/battle_tts_noeffect.inl` (~250 lines). Tunables: `NOEFFECT_WATCHDOG_MS=6000`, `NOEFFECT_KIND4_DEDUP_MS=8000`. Coordination: piggybacks on `s_lastSpellMissAnnounceTick[slot]` (the kind=4 path's dedup array) for both directions — reads to skip if kind=4 already spoke, writes to silence late kind=4 fires.

Scope (v1): player-cast magic only (`retaddr=0x0048594E`), single-target only. Enemy casts deferred (rarely produce no-effect). Multi-target Reflect/Wall scenarios deferred.

---

## Session 86 part 1 (2026-04-25) — v0.13.84/85 HEAL-DIAG diagnostic for heal-on-cap address hunt

Aaron asked: when Cure is cast on a damaged target and overcaps (e.g. Cure for 344 on 488/560 HP target heals to 560, displays "344" but only +72 HP delta), do we announce 344 or 72? Existing `FlushHPAnnouncements` uses `max(abs(delta), displayValue)` which would give 344 IF `0x01D2834A` holds heal values. Hypothesis untested.

**v0.13.84 diagnostic:** Added `HealDiag_DumpDisplayRegion(tag)` helper to `battle_tts_hp.inl` — dumps 32 bytes (16 uint16s) centered on `BATTLE_DAMAGE_DISPLAY_ADDR` at four call sites in `PollHPChanges`: anim-start (0→1), anim-done (1→0), anim-timeout, heal-timeout. All four sites lived inside `if (s_anyHpPending)` block.

**v0.13.84 BAT result:** Cure on full-HP Grat produced "No effect on Grat" (the v0.13.83 watchdog firing) but NO HEAL-DIAG output. Diagnostic was placed inside the same HP-delta-gated block we were investigating around — gating defeated the purpose for max-HP cap events.

**v0.13.85 fix:** Added ungated anim flag transition tracker (`s_healDiagPrevAnim`) at top of `PollHPChanges`. Fires HEAL-DIAG with tags `ungated-start` and `ungated-done` on every anim flag 0→1 and 1→0 transition regardless of HP delta state. Kept the four existing in-block calls.

**v0.13.85 BAT confirmed:** `0x01D2834A` holds heal values too. Cure on damaged Grat (488→560, delta+72): `[HEAL-DIAG] ungated-start: display@idx8=344`. HP-TRACK detected delta+72 BEFORE the watchdog could snapshot (engine pre-applies HP). FlushHPAnnouncements correctly used `max(72, 344)=344` and announced "Grat recovers 344 HP." — existing path worked.

But TWO new bugs surfaced from this BAT, requiring v0.13.86.

---

## Session 86 part 2 (2026-04-25) — v0.13.86 heal-on-cap announcement fix

v0.13.85 BAT exposed two bugs in the watchdog/heal interaction:

**Bug 1: Watchdog double-announces "no effect" after a successful heal-on-cap.** The engine pre-applies HP (488→560) BEFORE `sub_48E830` fires. `NoEffect_RecordSnapshot` captures `targetHpAtCast=560` (post-heal). Six seconds later, the watchdog reads HP=560, sees no change, fires "No effect on Grat" — even though FlushHPAnnouncements correctly said "Grat recovers 344 HP" earlier. Double-announce.

**Bug 2: Full-HP heal target gets nothing announced.** Cure on full-HP target. HP delta = 0, `s_anyHpPending` stays false, `FlushHPAnnouncements` not called for that slot. Watchdog sees no HP change, no status, fires "No effect on Grat." Aaron's principle: announce what's shown, not what HP did. Engine wrote 344 to displayValue — we should speak it.

**Fix design** (one cohesive set of changes, both bugs share the mechanism):

1. `battle_tts_hp.inl` adds `s_displayValuePrevFrame` (uint16_t, updated at end of every PollHPChanges call). Mod thread snapshot. Game thread reads this from `NoEffect_RecordSnapshot` to capture pre-action displayValue — since hooks fire BETWEEN mod thread polls, the value seen by the hook is from the LAST mod poll, which precedes the engine's same-frame pre-write.

2. `battle_tts_hp.inl` adds `s_lastFlushAnnounceTick[BATTLE_TOTAL_SLOTS]`. `FlushHPAnnouncements` stamps `s_lastFlushAnnounceTick[slot] = now` after each damage or heal announcement.

3. `battle_tts_noeffect.inl` adds `displayValueAtCast` field to `PendingSpellNoEffect` struct, captured via `s_displayValuePrevFrame` at `RecordSnapshot` time.

4. `battle_tts_noeffect.inl` `PollPendingSpellNoEffect` adds two new logic blocks:
   - **Step 1c (Bug 1 fix):** Check `s_lastFlushAnnounceTick[slot] >= castTick`. If a flush spoke for this slot during the watchdog window, mark `sawHpChange=true`. Suppresses no-effect.
   - **Step 4b (Bug 2 fix):** At expiry, if no other effect observed, read current displayValue. If `displayValue > 0 AND displayValue != displayValueAtCast`, announce "X recovers N HP." via `Validate_AnnounceEvent("heal-on-cap", ...)`.

**v0.13.86 BAT verified both fixes:**
- 21:09:39 Quistis Cure on Quistis (771→861, partial cap, display=345): `[VALIDATE] kind=heal slot=0 value=345 trigger=new-turn tts="Quistis recovers 345 HP."` (existing flush). Then 21:09:45: `[NOEFFECT-WATCH] slot=0 had effect (hp=1, status=0), no announce` (Bug 1 fix via Step 1c). No double-announce.
- 21:09:54 Squall Cure on full-HP Grat (display=297, HP delta=0): `[VALIDATE] kind=heal-on-cap slot=3 value=297 trigger=watchdog tts="Grat recovers 297 HP."` (Bug 2 fix via Step 4b; displayBase=345, current=297).
- Zero `[SPELL-NOEFFECT]` lines in entire log = no false positives.

Known limitation: same-value consecutive cap-heals (Cure 344 → Cure 344 on same target with no intervening action) won't fire — baseline equals current. Acceptable.

---

## Session 86 part 3 (2026-04-25) — v0.13.87 cleanup pass

With v0.13.86 verified working, removed the HEAL-DIAG diagnostic infrastructure that served its purpose in v0.13.84/85:
- `HealDiag_DumpDisplayRegion` helper function
- `s_healDiagPrevAnim` ungated tracker static
- Block in `PollHPChanges` that reads anim flag and calls HealDiag at transitions
- Four redundant in-block calls (anim-start, anim-done, anim-timeout, heal-timeout)

Retained the v0.13.86 fix logic intact:
- `s_displayValuePrevFrame` (still updated at end of PollHPChanges)
- `s_lastFlushAnnounceTick[BATTLE_TOTAL_SLOTS]` (still stamped by FlushHPAnnouncements)
- Watchdog Step (c) and Step 4b (battle_tts_noeffect.inl)

Also updated KNOWN LIMITATIONS comment block in `battle_tts_noeffect.inl` to remove the now-fixed "Cure on full-HP target" entry, replaced with note about same-value consecutive cap-heals being the new limitation.

No behavior change. v0.13.87 is purely a cleanup version.

---

## Sessions 88–94 (2026-04-25 to 04-26) — sprite-visibility detection: failed investigation

Fourteen-session arc (80–94) attempting to detect when the floating damage-number sprite becomes visible on screen. **Conclusion: not feasible with the approaches tried.** v0.13.90 animation-end timing remains the working baseline.

### v0.13.88 — text-writer hook call counter

Added `[TEXT-CTR]` diagnostic counting `sub_495280` and `sub_4952F0` invocations per damage event. Confirmed both fire during action-announce paths but their call counts and timing did not correlate with floating damage-number visibility. Hypothesis A (text writers handle floating numbers) **falsified**.

### v0.13.89 — no-effect timing queue

Moved no-effect / heal-on-cap announcements through `NoEffect_QueueAnnouncement` so they flush on anim flag 1→0 like status-apply does. Aaron BAT-confirmed: no-effect TTS now lands at the same moment status TTS lands. Codified as invariant #16.

### v0.13.90 — removed `HP_ANIM_TIMEOUT_MS` safety net

The 4-second `HP_ANIM_TIMEOUT_MS` was firing prematurely on long spell animations (Fire ~6s, Meteor, Ultima). Removed it entirely; PollHPChanges now trusts the 0x01D280C0 anim flag's natural 1→0 transition exclusively. BAT-confirmed working from 1.2s physical to 6s+ spells. Codified as invariant #17. **This is the current shipped damage-timing behavior.** `HP_HEAL_TIMEOUT_MS = 1500ms` retained for the no-anim fallback path only.

### v0.13.91–92 — popup-table wide-net diagnostics

Added `[POPUP-LIFE-DIAG]` and `[POPUP-TIME-DIAG]` casting a wider net on the 0x01D280C4 popup table. BAT showed the popup table only ever holds action-announce records ("Cast Fire", "Draw Sleep", etc.) — it is not the path floating damage numbers take. Sessions 80–92's popup-table thesis **falsified**.

### v0.13.93 — architectural pivot to sprite allocator (FAILED)

Removed all popup-table monitoring code. Hooked `sub_482C90` (identified in session 83 research as the sprite allocator) and watched for `CALLBACK_COMPLEX_UPDATE` (0x48E620) allocations as the damage-sprite signal.

BAT result: `[SPRITE-HOOK] Installed at 0x00482C90` succeeded, but the hook **never fires during damage events**. `[DISPATCH] sub_483470: calls=0 blocks=0 passes=0` across full battles. Animation logs continued to show `trigger=anim-done`. The session-83 identification of `sub_482C90` as the damage sprite allocator was either incomplete or wrong.

### v0.13.94 — failure analysis and decision

No new code. Documented exhaustive failure across all four investigated vectors:

1. Popup-table monitoring (sessions 80–92) — only captures action announcements
2. Text-writer hooks (session 78+, v0.13.88) — fire but don't correlate with sprite visibility
3. Sprite allocator hook (sessions 93–94) — installs but never fires
4. Alternative sprite tables (0x01D28DF0 etc., session 81/83) — all walked, all dead-ends

Decision: stop chasing sprite visibility through engine code paths. Session 95 will attempt graphics-pipeline interception (gl draw calls / framebuffer ROI diff) as a single time-boxed final attempt; if that fails, accept v0.13.90 as the final damage-timing solution and pivot to Limit Breaks, buff announcement, and GF audio descriptions.

Invariants #18 (sprite system pivot) and #19 (sprite detection not feasible via engine paths) recorded.

---

## Pre-v05: Dialog, FMV, Title Screen

### v04.36: Field Dialog TTS
All MES/ASK/AMES/AASK/AMESW/RAMESW opcodes hooked. show_dialog hook for tutorials/thoughts. Naming screen bypassed via enableGF() calls.

### v03.00: FMV Audio Descriptions + Skip
ReadFile EOF hook for FMV skip. WebVTT-timed audio descriptions via SAPI.

### v02.00: Title Screen TTS
Cursor tracking for New Game/Continue/Credits.

---

## Timber Train Hijack chapter (#56-#60) — full discovery detail (archived from DEVNOTES 2026-06-12; chapter shipped v0.18.3.30 / `bb3ba05`, all sub-issues CLOSED)

**Timber 3-mode scheme (Aaron 2026-06-11, NO vanilla option -- useless to a blind player):** **Manual** (DEFAULT) = guards move + code announce + per-guard proximity cues; **Freeze** = guards held (var1040=0), player just enters codes; **Skip** = bypass (DONE, BAT v0.18.3.25/.26). INI `train_guard_mode` 0=Manual/1=Freeze/2=Skip -- numeric mapping = the old 0=Original/1=Manual/2=Skip RELABELED (behaviors per number unchanged; DEFAULT flips 1->0). Sub-issues: **#56** code announce (SHIPPED v0.18.3.8, CLOSED), **#57** key layout (RESOLVED), **#58** guard cue+suppression (SHIPPED v0.18.3.21, CLOSED), **#59** timer freeze+readout, **#60** mode ASK picker (BAT-CONFIRMED v0.18.3.23 -- `train_mode_ask_overlay`; trigger = Rinoa 'Squall, over here!' in tiyane1 after Watts 'Yeah' in tiagit1; renders as a native dialog, navigates, sets the mode; Skip interim = Freeze; minor cosmetic clip on the box width, TTS unaffected) + Skip bypass (BAT-CONFIRMED v0.18.3.26 -- `ExecuteSkipBypass`: writes the win's 23-byte persistent savemap delta at base 0x01CFDC5C + the engine transition-request block at 0x01CE4760 (type=1, destField=892, X=66/Y=64760/Z=0, inline=62, top=128) to warp to field 892; deferred ~400ms post-commit; capture diag off. Confirmed: lands in tiagit1, post-mission dummy-President dialogue plays. The replayed delta carries a net -1 SeeD rank (capture run had failures) -- KEPT as an intentional 'cost of skipping'; v0.18.3.26 label says so. #60 mode-ASK + Skip DONE).

**#56/#57 shipped:** uncoupling code is **sprite-drawn**, read from FFNx varblock `0x1CFE9B8` bytes **1026-1029 (`tiagit*`)** / **1029-1032 (`tilink*`)**; `TrainCodeAnnounce()` (field_dialog_lifecycle.inl, from PollWindows) speaks them per settled code; repeat key re-announces. #57 keys = **1=Right/D, 2=Down/X, 3=Left/A, 4=Up/W, Q=quit**. v0.18.3.27 (#60/#57): "/" on tiagit*/tilink* announces "Code entry keys. 1 is D, 2 is X, 3 is A, 4 is W." (TrainCodeKeyHelp in field_dialog_lifecycle.inl, field-gated, number-first; menu-mode "/" help unaffected since it's gated to mode 6). v0.18.3.28 (#60/#57): Rinoa's briefing line "...if I relay the code 3124, you'll push L L L L..." (4 key sprites decoded as "L L L L") rewritten to "...push A, D, X, W..." + a "/" reminder, via ApplyTrainCodeKeyFix in field_dialog_helpers.inl called from ScanAndSpeakAllWindows (dedup keys keep the original text so no re-speak; match = the unique 4-L run, single page-break L untouched). Discovery diags off (behind `#define`). Detail: CHANGELOG.

**#58 GUARDS (CLOSED -- SHIPPED v0.18.3.21, pushed HEAD 3515729). Manual-cue BAT-confirmed v0.18.3.20 (Y-axis discriminator) + Freeze-suppression v0.18.3.14. NOTE: user-facing labels relabeled for the #60 picker -- old 'Original' cue = new 'Manual', old 'Manual' freeze = new 'Freeze'; numeric INI 0/1/2 unchanged.** Guards DO catch Squall during code entry (real mechanic, NOT decorative). **Failure = a field ASK** ("Rinoa: What happened!? Squall!!!", `[AASK]` in ff8_dialog.log); field freezes, no mapjump; shared by catch AND timer-expiry. **Var map (tilink1, varblock 0x1CFE9B8 byte-indexed):** **1040 = patrol switch (1=patrol, 0 FREEZES guards)**, 1042 = round-active (validators gate on this), 1029-32 = live code (=[TRAINCODE-SAY]), 1024-27 = entered digits, 1028 cnt, 1043 = entry stage, reload spawn picked by savemap 724. **Machine entities:** blind2/blind3 (model 5/6) = the patrol guards walking rail **X=-1313** (no player-pos read; gated 1040); blind4 = master (method4=FAIL->MAPJUMP3 reload field902; method5=SUCCESS->MAPJUMP3 field925); blind5/6/7 = per-digit validators (gated 1042); blind8 = code display. Catch = a MOVING guard reaching ~talk radius 128 of the player (static entities never catch). Full .11-.13 discovery dumps archived in NEXT_SESSION_PROMPT.md. **MANUAL (v0.18.3.14, WORKS):** `GuardManualFreeze()` (field_dialog_lifecycle.inl, PollWindows) pins var1040=0 on `tilink*`; entry+win still work. Gate `tilink*` covers BOTH cars -- **tilink1(902)+tilink2(903)**. **Real win dest = field 892 (Forest Owls' Base)** via tilink2 MAPJUMP3 (NOT 925). INI `train_guard_mode` [Accessibility] 0=Orig/1=Manual/2=Skip, default Manual. **ORIGINAL (v0.18.3.20, BAT-CONFIRMED -- Aaron: exactly two guards per car, approaching lead time "just right"):** `GuardOriginalCue()` (field_nav_observe.inl, each tick before observer gates) announces PER-GUARD -- "Guard N approaching/close/clear" (recede close->approaching SILENT; only "close" interrupts). **Guard-vs-party = the Y AXIS (v0.18.3.20, per Aaron + F11 screenshots):** the train runs left-right; Squall drops DOWN to the panel (screen bottom) while the party stay ON THE ROOF (screen top), so a party member can be horizontally near Squall yet FAR on the depth/Y axis -- which Euclidean distance (v.17-.19) wrongly flagged. So proximity is judged on **|entity.Y - player.Y| ALONE**: close <=480, approaching <=960, clear >=1152 (hysteresis 960..1152). Guards patrol the corridor so their Y sweeps through Squall's (catch at |dY|~90); the roof party sit at |dY|>=~1360 and are ignored/never labelled. Motion gate kept as a secondary guard vs static same-lane props; per-guard lowest-free labels released on recede. (Axes ROTATED -- this Y axis is HORIZONTAL on screen, hence earlier "vertical" mislabel.) **tilink1 coords (v0.18.3.18 BAT):** player Y=1509; guards ent5/ent6 sweep Y -505..1662 (cross 1509); party ent1 Y=-415, ent2 Y=145 (|dY| 1924/1364, static). **BAT history:** .15-.17 distance variants; .18 live-motion; .19 approach-gated labels; .20 Y-axis. Mode = SHARED accessor `FieldDialog::GetTrainGuardMode()`/`SetTrainGuardMode()` + `enum TrainGuardModeVal` in field_dialog.h. Consts CLOSE_DY/APPROACH_DY/CLEAR_DY atop GuardOriginalCue(); [GUARDCUE] logs dY+dist+pos. **v0.18.3.21 cleanup: GUARD_RECON_DIAG ([GUARDPOS]) + GUARD_VAR_DIAG ([GUARDVAR]/[GUARDFREEZE]) turned OFF (retained behind flags). [GUARDCUE] (feature, level-change only) + the Manual var-pin unchanged.** Timer (#59) = shared FF8 countdown engine (Dollet/Fire Cavern; #33). **v0.18.3.29:** train 5-min timer is the same global 0x01CFE92C (SECONDS) but 300s fell below the old 500-3000 SECONDS floor -> never activated; the static leftover 79 hit EnterActive every tick -> 5.62MB UNKNOWN-log flood. countdown_timer.cpp: ClassifyUnits widened to 1-14999=SECONDS (dropped MINUTES 5-60, would 60x-misscale the last minute) + decrement-gated activation (s_actPrevRaw/s_actDecrements, ACT_DECREMENTS_NEEDED=2). T=announce, Shift+T=freeze, cues 1:00/0:30. **v0.18.3.30** fixed the leftover-active glitch (engine freezes the global at ~79 instead of zeroing): ACTIVE now also EnterInactive()s after STALL_TIMEOUT_MS=8s with no change (s_lastDecrementTickMs); FROZEN untouched. Same glitch fixed on Dollet/Fire Cavern too.
