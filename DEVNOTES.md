**Purpose & context**

Aaron is the sole developer of the FF8 Accessibility Mod — a `dinput8.dll` injection for Final Fantasy VIII Steam 2013 (App ID 39150, FF8_EN.exe + FFNx v1.23.x) that makes the game playable for blind players via Windows SAPI TTS, navigation assistance, and entity catalogs. Aaron is blind and uses NVDA as his screen reader, and is also the primary tester.

**Project root:** `C:/Users/ampag/OneDrive/Documents/FFVIII-Accessibility-Mod/FF8_OriginalPC_mod/`

GitHub: `ampage87/FFVIII-Accessibility-Mod`. **GitHub HEAD = v0.17.8.17.8** (commit `b7067354`, Chapter 2 cleanup -- Laguna F12 diagnostic infrastructure removed; pushed 2026-05-28 22:52 UTC). The push folded v0.17.8.17.1 .. .17.8 (the full Laguna dream chapter: field nav player-detection fix, in-battle / command-menu / victory / main-menu / item-Use / GF-owner dream-NAME fixes, then F12 cleanup) into a single commit. Parent is `b6afa8cb` (v0.17.8.16.1, Chapter 1 close). **Local tree = v0.17.8.17.8** (matches HEAD; Chapter 3 patch staged on top -- see below). Aaron pushes via `Utilities/push_to_github.ps1` (Claude never pushes); diagnostic builds stay LOCAL. **Size status:** all source files comfortably under the 80 KB ceiling.

**bghall_1 save point — SOLVED + SHIPPED in v0.17.8.9 (BAT-confirmed; signal found via a now-removed LOCAL script dump):** the LOCAL dump of bghall_1 entities (zells/selphie/savePoint/saveline0) proved the save line is `ent5 'selphie'` (the SETLINE at (-700,-8593) currently shown as "Interaction 1"). Its script literally pushes the save-enable opcodes as constants: PUSH 303 (0x12F SAVEENABLE) and PUSH 304 (0x130 PHSENABLE) in BOTH method[6] (dwords 3624/3632) and method[7] (3657/3665). The control line `ent4 'zells'` has NONE of these (clean discriminator). Why the scanner missed it: selphie's ONLY 0x1C is the bare runtime-supplied dispatch in method[1] (`EXT_DISPATCH` empty-stack, like the dorm bed) — the save constants live in methods 6/7 and are never popped by a local 0x1C, so dispatch-resolution can't set foundSaveenable. savePoint (ent27) is unpositioned (X=PSHM135 Y=PSHM588, no SET3-shift) and its 0x1C resolves to a runtime PSHM; saveline0 (ent36) is a REQ-chain controller with a MAPJUMP (classified MAP_EXIT) and no statically-visible save op — so neither save-POINT entity can carry the label. **FIX (the chosen association, field-load, no cache, no heuristic guess): in the JSM scan, for a Line entity (jsmCategory==1) scan its full bytecode for literal PUSH of the save opcodes — set foundSaveenable when MENUSAVE(302) is present OR both SAVEENABLE(303) and PHSENABLE(304) are present. That makes signal-(a) fire -> isSaveLine -> the catalog surfaces selphie as "Save Point" at its own SETLINE center (-700,-8593), exactly where auto-drive already arrives.** Contrast (why the dorm bgryo2_1 already works): its savePoint gets a SET3-SHIFT position (229,97) and injects directly, and its saveline0 has a statically-visible save op + LATE-RESOLVE position — bghall_1 has neither, which is why the own-script-constant route on the LINE is the right fix here.

The v0.17.7.6.x chapter closed the bgroad_5 hallway calibration failure (full narrative in `DEVNOTES_HISTORY.md`); v0.17.8.0 closed bugs #5/#6, v0.17.8.1.1 closed #3, v0.17.8.3 closed #4, v0.17.8.4 removed a bogus camera catalog entry, v0.17.8.6 added the dorm bed + killed its duplicate exit, v0.17.8.7 filtered the `cardgamemaster` debug phantom + fixed the Event/Interaction double-injection that was also hiding the Directory, v0.17.8.8 added a general object/line dedupe (kanban signboard showed as both "Interaction 2" and "Kanban1" on bghall_2) plus a raw-SYM object relabel (standalone "Kanban2" on bghall_3 → "Interaction N"), and added save-line script-association detection. v0.17.8.9 completed that detection — the bghall_1 save point now labels via selphie's own-script save constants (see the save-point section above) — and refactored the two impacted .inl files back under the size ceiling. **Bugs #9 and #10 SOLVED + pushed: #9 in v0.17.8.10 (SegmentsCross), #10 in v0.17.8.15.1 (JSM behavior signal). Backlog: the runtime dialog-confirmation/disk-persistence layer (general answer to Director over-promotion).** Still deferred: Fire Cavern bug #1 (Quistis infirmary FMV premature), Laguna dream bugs #7 (field-nav player detection) and #8 (battle announces wrong party).

---

## Where we are at session open

**Chapter 3 BAT-VALIDATED, ready to push (v0.17.8.18.1). Scan-on-allies fix.** Chapter 2 closed cleanly at GitHub HEAD `b7067354` on 2026-05-28 22:52 UTC. Chapter 3 opened, fixed, and BAT-confirmed in a single session.

**BAT evidence (2026-05-28 17:05-17:07):** Squall scanned in regular battle, slot 1 (ally):
```
[SCAN-CACHE] Captured slot=1 name='Squall' monsterId=0x00 hasDesc=1
[SCAN-TTS] Auto-announce slot=1 msg='Squall. Uses a sword called a gunblade.
           Special skill is Renzokuken, using the gunblade. Silent, and a
           bit cold. Press numbers 0 through 9 for details.'
[SCAN-TTS] SpeakField slot=1 fieldId=2 msg='Uses a sword called a gunblade...'
```
The full canonical description played. Aaron also pressed `2` to re-query (the field-2 description path) -- both code paths work. Zero-regression check: Bite Bug at slot 3 also scanned (`monsterId=0x2C hasDesc=1`, full description played).

**Chapter 3 carry-forward learnings:**
  - **The architectural assumption was wrong on every count.** The original `scan_tts.cpp` comment claimed allies had no meaningful entry at `+0xB3`. Squall's BAT-captured byte was `0x00` -- a valid, specific scan-table index pointing to his canonical description. The `+0xB3 -> monster_id -> SCAN_TEXT_POSITIONS -> SCAN_TEXT_DATA` lookup chain is genuinely universal across allies and enemies.
  - **Squall is monster_id 0x00 in the scan table** (incidentally; not a magic value -- just where his entry happens to live). If we ever want the complete playable-cast mapping documented we can collect it during a future session, but the universal lookup makes that unnecessary -- it just works for every slot.
  - **One-patch fix shape.** No diagnostic infrastructure was added because the existing `[SCAN-CACHE]` log line printed `monsterId` and `hasDesc` for every captured slot regardless of ally/enemy. That was enough to verify both the success case and the would-be failure case without writing a new diagnostic. Worth remembering as a pattern: when a fix removes a guard, the log line beyond the guard often already gives you the verification you need.

**Code change shape (in `src/scan_tts.cpp`):**
  - `CaptureSnapshot`: removed `!snap.isAlly` from the lookup gate. Same `+0xB3` read, same `ResolveDescriptionSafe` call, applied unconditionally.
  - `BuildAutoAnnounce`: removed `!snap.isAlly` from the description-append check. `if (snap.hasDescription)` alone now gates the auto-announce description.
  - `SpeakField` case 2 (key `2` description query): collapsed the ally branch into the generic `snap.hasDescription ? description : "No description available."` path.
  - Architectural comment block rewritten to document the actual universal-lookup behavior. `ResolveDescriptionSafe`'s existing filters (`monsterId == 0xFF` sentinel, `pos == 0xFFFF`, `pos >= 0x4000`, empty decode) catch any genuinely-stale byte without needing the ally guard.

**Ready to push.** Aaron runs `Utilities/push_to_github.ps1` to push v0.17.8.18.1 as a single commit. GitHub HEAD before push = `b7067354`.

**Laguna bundle (Chapter 2, all PUSHED):**
  - Bug #7 (field nav): fixed v0.17.8.17.1.
  - Bug #8 NAMES (in-battle): fixed v0.17.8.17.2.
  - Bug #8 COMMAND MENU: fixed v0.17.8.17.5.
  - Bug #8 NAMES (Victory screen): fixed v0.17.8.17.6.
  - Bug #8 NAMES (Main Menu audit -- Junction char-select + M-summary + Item Use-target + GF owner): fixed v0.17.8.17.7.
  - v0.17.8.17.8 cleanup: F12 Laguna diagnostic infrastructure removed.
  - Bug #8 NAMES (FIELD entity catalog): documented follow-up; needs dream-field model-ID observation before fixing.

GitHub HEAD = `b7067354` (v0.17.8.17.8, Chapter 2 closed).

### Last chapter closed: Chapter 2 (Laguna dream bundle -- bugs #7 + #8)

Seven incremental fixes squashed into commit `b7067354` on 2026-05-28. Detailed narrative in `CHANGELOG.md` v0.17.8.17.1 .. .17.8 entries. Key carry-forwards:

  - **Dream party data lives in regular char-data array (CONFIRMED v0.17.8.17.5 + .17.7).** `char-data[SAVEMAP_PARTY_FORMATION[slot]]` IS the active dream character's struct: `commands[3]@+0x50`, `magics[32]@+0x10`, GF mask@+0x58, `exp@+0x04`, `model_id@+0x08`. The savemap formation (`SAVEMAP_PARTY_FORMATION = 0x1CFE74C`; menu reads `+0xAF1`) holds the STALE regular field formation `[05 00 01]` during a dream -- correct for INDEXING char-data, wrong as a NAME source.
  - **Three dream-identity sources, by context (BAT-confirmed during this chapter):**
    - In battle / victory (battle module live): `compStats[slot]+0x1C3` actor-kind (8=Laguna, 9=Kiros, 10=Ward). compStats base 0x1CFF000, stride 0x1D0.
    - Main menu (no battle): the loaded char struct's `model_id` (+0x08) -- the v0.17.8.17.7 BAT log captured `modelId=10/8/9` for Ward/Laguna/Kiros in mode-6 dream junction.
    - Field: `setpc` (field entity +0x255). v0.17.8.17.1 used this to fix bug #7.
  - **`ResolveDreamAwareCharId(charIdx)` (menu_tts_diagnostics.inl, v0.17.8.17.7):** THE canonical resolver for formation-index -> dream-aware name. Returns model_id when 8/9/10, else original index. Used by AnnounceMenuSummary, GetPartyMemberName, item Use-target naming; Junction GF-owner uses inline literal-address version.
  - **`GetBattleCharName(partySlot)` (battle_tts_menu_helpers.inl):** in-battle actor-kind override for dream, falls back to savemap for regular. All in-battle ally naming (turn/target/HP keys/command menu/victory/drawer) routes through this. Battle `CHAR_NAMES[8]` is only the actor-kind fallback for regular characters.
  - **`GetVictoryCharName(slot, fallbackId)` (battle_tts.cpp, v0.17.8.17.6):** dream-aware victory name; reads `s_dreamSlotCharId[slot]` snapshot captured per-frame during battle for cross-thread reach to the victory thread at mode 4.

### Last chapter closed: Chapter 1 (Fire Cavern bug #1, Quistis infirmary FMV premature)

Closed across two stacked patches squashed into one commit:

- **v0.17.8.16** -- engine cue-clock fix (`fmv_audio_desc.cpp`). Replaced wall-clock cue timer with engine-active-time accumulator that only advances when `FF8Addresses::IsMoviePlaying()` returns true. BAT-confirmed 2026-05-27 18:10-18:12 on `disc00_01h.avi`: 17-second gate held, cues fired at correct offsets.
- **v0.17.8.16.1** -- AD content rewrite for the same FMV. Engine fix BAT revealed the AD itself was wrong (misidentified Quistis as Dr. Kadowaki; framed Squall as leaving rather than lying in bed). Frame-verified via ffmpeg (27 frames @ 0.5s intervals). Rewrote `Audio Descriptions/disc00_01h.vtt` and corrected the `FMV_SCENE_REFERENCE.md` entry so future AD authors don't repeat the misidentification. BAT-confirmed 2026-05-28 (Aaron: "sounded good").

### Earlier closed chapter: bug #10 -- Hall 6 Xu mislabeled (v0.17.8.11 - v0.17.8.15.1, single-commit on GitHub)

Nine builds across two days. The wrong path was a chara.one model-archive parser (v0.17.8.11-.14): MinHooked `chara_one_read_file`, parsed Mch/Char headers, cross-referenced SETMODEL's chara.one slot index against the parsed model class. Successive bug fixes through this chain (Bug A isMch flag in .12; Bug B SETMODEL opcParam vs stack in .14) concluded kanban2 = prop because p048 classified as prop. The disproof: Aaron's F11 screenshot of bghall_3 (`Logs/screenshots/f11_204546_707.png`) showed Xu visibly standing as a character model in front of Squall at the kanban2 spot, dialog box reading `Xu "Hey, Squall, heard you got your first mission already!"`. There is no signpost. The chara.one classifier was wrong about p048 (p048 IS a character model on this field, regardless of the 'p' prefix convention), AND more fundamentally: file-level model classification was the wrong mechanism entirely. The right question is gameplay behavior, not model identity.

**The clean fix (v0.17.8.15 + .15.1):** the JSM scan already had the behavior signal -- `jsmCategory == 3 (Other) && hasSetmodelInit` -> "NPC N". Everything else (Line walk-across, Background script-only) -> "Interaction N". Per Aaron's directive, NPC labels stay generic "NPC N" -- no SYM names exposed. v0.17.8.15 BAT confirmed the mechanism works (Xu announces as NPC). v0.17.8.15.1 BAT confirmed the two follow-on label/announce fixes (counter overcounted by friendly-named NPCs; sameType test missed JSM-injected entityIdx <= -300). Final BAT showed `'NPC 1 1 of 1'` on bghall_3, all expected log lines present, no regressions.

**Carry-forward learnings (live below in "Session ritual & rules"; key ones repeated here for proximity to the chapter that taught them):**

- **SYM names are unreliable as identity hints.** kanban2 IS Xu. The internal name was placed for the script author, not for us. Don't infer entity behavior from SYM.
- **File-level model classification is the wrong primitive for behavior questions.** p048's filename prefix is a convention, not a contract; on this field it loads a character. When asking "how does the player interact with this entity", look at the JSM behavior signals (`jsmCategory`, `hasSetmodelInit`, `hasDialogReqTarget`, `hasTalkSetup`, `foundExtDispatch`), not at the model file.
- **NPC label policy:** generic `"NPC N"` / `"Interaction N"` only. Friendly names (Cid, Quistis) come from a different path (the runtime entity name resolver). The raw-SYM relabel sequence must never expose SYM names.
- **Announce-time counters need type-based matching for JSM-injected entries.** Legacy `entityIdx >= 0` test was "is this a runtime entity" -- it fails for JSM-injected NPCs/Interactions (entityIdx <= -300). When adding a new entity TYPE to the catalog, also extend the corresponding announce sameType branch.

### What was here before (now condensed)

The full bug #10 chapter narrative (v0.17.8.11-.14 chara.one chain + v0.17.8.15/.15.1 revert + clean fix) is preserved in commit history (commits `c7b80872` and predecessors local-squashed into it) and in CHANGELOG.md (v0.17.8.11 through .15.1 entries). Move v0.17.8.7 cardgamemaster narrative + this bug #10 chapter to `DEVNOTES_HISTORY.md` on the next size-trim pass (also overdue: v0.17.8.7).



### NEW bugs found in the first Laguna dream (gwgrass1) — separate chapters

7. **Laguna dream field nav fully broken.** Player entity not detected: log shows `player=ent-1` and every auto-drive attempt logs `[drive] REFUSED ... player_pos_known=0 player_entityIdx=-1`. The `setpc==0` player-detection heuristic in RefreshCatalog/Update fails in the Laguna dream (no entity has setpc==0, or the dream player uses a different marker). This breaks F9 navigation entirely in Laguna sequences. Needs its own diagnostic (dump setpc for all entities on gwgrass1).
8. **Laguna dream battle announces the real party.** Battle TTS says Squall/Zell/Selphie instead of Laguna/Kiros/Ward. The savemap formation still holds the real party char IDs during the dream (gwgrass1 formation logged as [5,0,1,255] = the real party). Battle-side fix — the dream party is swapped in via a different mechanism than the savemap formation array. Separate chapter.
9. **B-Garden hub Hall 4 exit missing — SOLVED (v0.17.8.10, BAT-confirmed 2026-05-27).** Correct field is the HUB `bghall_5` (field 174, display "Hall 10"), NOT bgroad_5 — the original report's field was wrong. Its only path to Hall 4 (`bghall_2`, field 168, INF dest alias `feclock1`) is an INF gateway with no SETLINE. The catalog gateway screen-filter used `IsSeparatedByTriggerLine()`, an INFINITE-line side test; the Hall 6 doorway exit (line9, SCREEN_BOUND, short segment on the far EAST edge x in [4206,5042]) extended to infinity passed between the player and the west-edge Hall 4 gateway (center -4572,3777, whose Y≈3777 lay on that line's extension), so it was filtered every refresh. Confirmed by a [gw-diag] read-only capture (removed). FIX: added `SegmentsCross()` (bounded segment-vs-segment intersection) in `field_navigation.cpp`; the gateway filter now hides a gateway only if the player→gateway SEGMENT actually crosses a boundary SEGMENT. Entity screen-filtering still uses the infinite-line helper (unchanged — minimal blast radius). Contrast that confirmed the mechanism: Hall 6's single gateway → Hall 10 always surfaced (no short edge-line collinear with it).
10. **B-Garden Hall 6 (`bghall_3`, field 170) -- NPC Xu labeled "Interaction 3" -- SOLVED (v0.17.8.15.1, BAT-confirmed + pushed 2026-05-27).** Xu was JSM `kanban2` (ent25, cat3, PSHM pos (4626,-3459)) -- the SYM name was misleading; kanban2 IS Xu. Nine builds across two days: v0.17.8.11-.14 attempted a chara.one model-archive classifier (NPC vs prop by reading model file headers, MinHooked on `chara_one_read_file`), disproved by Aaron's F11 screenshot (`Logs/screenshots/f11_204546_707.png`) showing Xu visibly standing as a character at the kanban2 spot. v0.17.8.15 ripped out the entire chara.one chain and replaced it with a clean JSM behavior signal: `jsmCategory == 3 (Other) && hasSetmodelInit` -> "NPC N". v0.17.8.15.1 added two follow-on fixes: dedupe counter (name-prefix match instead of all ENT_NPC, so friendly-named NPCs like Cid don't inflate the count) and announce sameType (type-based matching for JSM-injected entityIdx <= -300, so the "X of Y" suffix works for both NPC and Interaction). Final BAT showed `'NPC 1 1 of 1'` on bghall_3 with all expected log lines. The whole chapter pushed as single commit `c7b80872`. **Carry-forward learnings in the "Last chapter closed" section above.**

**v0.17.8.1.1 (pushed) closed bug #3.** Fire Cavern playthrough bug list (Aaron's 2026-05-18 report) progress:
1. **Quistis' FMV in the Infirmary fired prematurely** -- ✅ closed by v0.17.8.16 (timing fix) + v0.17.8.16.1 (AD content rewrite), squashed into commit `b6afa8cb`.
2. ~~Manual field navigation direction lag and inaccurate direction guidance~~ — ✅ closed by v0.17.7.6.2
3. ~~Garbage announced by TTS following completion of a tutorial scene~~ — ✅ closed by v0.17.8.1.1
4. ~~Party member announced as NPC in catalog~~ — ✅ closed by v0.17.8.3 (BAT-confirmed bgryo2_1; pushed)
5. ~~Breakpoint on display timer announced when GF sequence starts~~ — ✅ closed by v0.17.8.0
6. ~~Damage not announced when a character is summoning and the GF takes the damage in place of the character~~ — ✅ closed by v0.17.8.0
7. Laguna dream field nav broken (player not detected) — NEW, deferred
8. Laguna dream battle announces real party not Laguna/Kiros/Ward — NEW, deferred

**Tooling lesson (carry forward):** `filesystem:edit_file` corrupts a file when the replacement text contains a literal dollar-sign character — it truncates the replacement and appends the original content, doubling file size. Use the hex literal `0x24` in source instead, or rewrite the whole file with `filesystem:write_file`. This bit us once on `field_dialog_scan.inl` (11.46 KB → 27.07 KB) during the v0.17.8.1 work. (Also: OneDrive occasionally throws a transient EPERM on `edit_file` rename — just retry once.)

---

**Track B sequencing (closed):** v0.17.7.0 file split → v0.17.7.1–.5.5 catalog overhaul → v0.17.7.6–.6.2 calibration. All pushed.

**Track B follow-ups (deferred, not blocking):** v0.17.7.7 SETLINE-position promotion + NPC ResolveFriendlyName; v0.17.7.8 Shop/Card Game → NPC announce-layer collapse; v0.17.7.9 (optional) SYM override layer for residual leaks. Revisit after the Fire Cavern bug list lands.

**Open question (deferred, not blocking)**: bgryo1_1 (Dormitory Double 1) resolver picked addr 0xE7 = 231 (Hallway 8) for its single SCREEN_BOUND line, but Aaron's actual return transition in the v0.17.7.5.3 BAT went to bgroad_5 (Hallway 5, field 228). INF gateway log showed destId=174 (Hall 10) — also doesn't match. Either the dormitory has multiple SCREEN_BOUND exits and only one was captured at that BAT point, or the addr-as-literal pattern doesn't hold for this field. Investigate if dormitory exit labeling issues recur; otherwise leave it.

---

## Active backlog (priority order)

### v0.17.7.x track parking

- **Track A: push-through gate routing** at fepic1 and any other scripted-gate field. Three candidate fixes; strategy decision is the first step when Aaron returns to it.

### v0.16.5.2 BAT triage carry-over (was 5 bugs; 2 ✅ closed by v0.17.8.0)

1. **FMV STOP/PLAY race** — Quistis infirmary AD fired 22 s before engine resumed FMV playback. Engine STOP/PLAY visible in `ff8_mod.log`; fix is to pause/resume the AD cue timer on those transitions instead of free-running on wall clock. (= Fire Cavern bug #1)
2. **POLL tutorial garble** — `[POLL] win[0] Speaking: ",e 3in*retone3 e~HP~B:All08E%~!/..."` after `[TUTO]` mode 10→1. Reject `[…]` tokens / unprintable garbage in POLL path, or suppress POLL win[0] for ~500 ms on tutorial-end. (= Fire Cavern bug #3, **next single build v0.17.8.1**)
3. **Party member announced as NPC** in 2-member parties on bdin2/bdin3. `party-filter` works on later fields but not earlier ones — likely keys on per-field model index instead of checking formation[] directly by character-ID. (= Fire Cavern bug #4)
4. ~~**GF-BP diagnostic spam**~~ — ✅ closed by v0.17.8.0 (gated behind `#define GF_BP_AUTOARM_DIAG 0`).
5. ~~**Missing damage announce when GF substitutes for char**~~ — ✅ closed by v0.17.8.0 (wired PollGFSummonState into Update + OR'd predicate with s_gfHpSubstitutionActive).

### Pre-v0.17.0 carry-over backlog

1. **Ifrit / GF audio description miss diagnostic** (v0.16.4 BAT): if it recurs in any future battle BAT, add 1-second `[GF-EFFECT-POLL] magicId=N prev=M` heartbeat to `PollBattleMagicId` in `src/battle_tts_ewm_gf_effect.inl` to capture engine writes to `0x01D99A68` during GF cast. v0.16.5 BAT confirmed Ifrit AD fires correctly so the v0.16.4 miss was intermittent engine timing, not refactor-related. Heartbeat stays parked.
2. **`menu_tts.cpp` T-handler `!shift` gate**. One-line cleanup.
3. **FieldAnnounce display-name catalog audit** in `src/field_display_names.h`. Wrong mappings for fieldIds 0x0134 / 0x0136. Verify Fire Cavern A (fieldId 0x0088, engine `fieldName='bdview1'`) end-to-end.
4. **Field-name populate race** at Part B arrival check — diagnostic log only, audio fine.
5. **Deep-research doc updates**: `Plan & Research Documents/Dollet timer countdown deep research results.md` — wrong-math fix + LIVE TIMER FOUND appendix.

### Long-deferred (don't pick without Aaron's direction)

Remove party members from field entity catalog · walk-and-talk dialog gap (hardcoded engine path) · SeeD rank bug #27 (`FIELD_H_OFFSET = 0xF94` hypothesis) · refined-coord narrow-gate steering (#29) · Fire Cavern #28 + planner-fallback #29 · per-world-map vehicle-aware BFS, guided GPS mode · Battle: Scan TTS keys 9/0 (status resist/active statuses) · Junction menu TTS · more victory screen polish · `chase_diag::OnAskOpcodeFired` snprintf bug · refined-coord persistence (JSON or %APPDATA% store) · engine-write hook for cleaner countdown freeze (cosmetic ±1-s flicker).

### v0.17.6.x candidates retired but on standby

All v0.17.6.0/.1/.2 BAT'd successfully. Remaining standbys may not be needed:
- v0.17.6.3: Re-enable corridor steering with `currentWpDist > 200.0f` gate. Only revisit if a long-corridor field overshoots without it.
- v0.17.6.4: Spatial triangle lookup fallback for stale engine triId. Only revisit if a drive fails with engine reporting wrong triangle.
- v0.17.6.5/.6: Simplified recovery / funnel waypoint visibility validation. Not needed unless symptoms surface.

---

## Recently shipped (one-liners; full narratives in `DEVNOTES_HISTORY.md`)

- **v0.17.7.0** (`8b9299c2`): mechanical file split of `field_nav_catalog.inl` (75.77 KB → 53.82 KB). Two new helper `.inl` files: `field_nav_catalog_diag.inl` (one-shot diagnostic dumps), `field_nav_catalog_lateres.inl` (late position resolution). Dropped v0.12.17 dead VARBLOCK-POS `if (false)` block. No functional change. Prerequisite for the Track B catalog-overhaul fixes landing in v0.17.7.1–.4. BAT'd clean on bghall_1 + bggate_6.
- **v0.17.6.x** (peak `a42d4aeb`): F9 path-finding auto-drive re-based on manual-nav primitives. `.ca`-quantized axes, talkRadius arrival distances, INF gateway auto-cross. Recovery counter resets on tri-advance with `[drive-vec]` per-tick diagnostic. Corridor-level steering disabled — funnel waypoints + FF8 wall-sliding only. BAT-confirmed across four bghall_1 cross-field exits.
- **v0.17.5.x** (peak `b54fa75`): World-map-polling boot fix; `[TTS]` and `[drive] REFUSED` audit logs; funnel collinear-waypoint pruning (5×); GPS 500 ms hysteresis; load-time 90° `.ca` axis quantization (replaced v0.17.4 passive calibration).
- **v0.17.0 → v0.17.4**: Manual-nav direction projection wired through `s_cameraAxes`; 2D normalization; A* + funnel path-aware GPS; camera-axes state separation (manual `s_camRight/Down` vs drive `s_driveCam*`); passive arrow-response observer; det convention check.
- **v0.16.x** (peak `5d16179a`): Source size-split chapter — world_map, chase_auto_pilot, field_dialog, field_archive_jsm, battle_tts_ewm, battle_tts_menu. Every `src/*.cpp`/`*.inl` now under 80 KB hard fail. Client-side mirror of CI size guards in `Utilities/push_to_github.ps1`. Wired `PollDeferredTurnAnnounce` (latent since v0.13.52).

---

## Catalog of known fieldIds for geometric-trigger destinations

- **Fire Cavern A** (approach field, world-map trigger): `fieldId=0x0088`, engine `fieldName='bdview1'`. Trigger position ≈ (30260, -29221).
- **Balamb Town gate** (planner destination, not geometric): `fieldId=0x006A`, fieldName=`bcgate_1`. Trigger position ≈ (12894, -26776).
- **B-Garden Front Gate 5** (push-through gate, fix deferred): `fieldId=0x00A3`, fieldName=`fepic1`.
- **B-Garden Cafeteria 1** (raw SYM `Son` leaks): `fieldId=0x009A`.

---

## Session ritual & rules

- Read **`DEVNOTES.md`** and **`NEXT_SESSION_PROMPT.md`** at session start. Read `DEVNOTES_HISTORY.md` only when tracing past decisions.
- Update both files at every version bump AND after every BAT result.
- **Filesystem MCP for all Windows project files.** Bash runs in a Linux container that can't reach the OneDrive mod directory. Use `filesystem:read_text_file/edit_file/write_file/list_directory/get_file_info`. Bare tools `create_file`/`str_replace`/`view`/`bash_tool` operate on the container's Linux filesystem.
- **Aaron pushes via `Utilities/push_to_github.ps1`**, Claude NEVER pushes. The utility refuses if `CHANGELOG.md`'s top heading doesn't match `FF8OPC_VERSION`.
- F-key handlers gated on `!(GetAsyncKeyState(VK_MENU) & 0x8000)`.
- F12 reserved for per-session diagnostics. Search source files for existing `VK_F12` references and remove old code before hooking new diagnostic.
- **NEVER re-enable SET3 hook (0x1E)** — CI guard in `.github/workflows/safety-checks.yml`.
- DEVNOTES under 10 KB. When this file approaches the limit, move completed-chapter material to `DEVNOTES_HISTORY.md`.
- `deploy.bat` version-extract regex requires `/B` anchor (v0.15.10.1).
- **`.inl` textual-include pattern** for source splitting; no `deploy.bat` change needed (only the parent `.cpp` is compiled). No header guards, no namespace declarations inside `.inl`. State declarations in `*_state.inl` go first.
- **Inline-changelog accretion is dead** (retired v0.15.12.0). Canonical changelog is `CHANGELOG.md`.
- **F11 screenshots are gold for BAT context.**
- **Diagnostic-feature gating pattern**: gate behind `#define X 0` instead of deleting.
- **Source file size limits (v0.16.0 CI guard)**: 60 KB soft warning, 80 KB hard fail. Split before substantive edits cross the warning line. Client-side mirror in `Utilities/push_to_github.ps1` Step 7c since v0.16.5.2.
- **Arrival detection needs VERIFICATION, not just signal-presence.**
- **Empirical-data capture (refined coords) needs the underlying decision VALIDATED before storage.**
- **Geometric-trigger vs script-trigger destinations need different navigation strategies.**
- **When "fixing" a planner decline, don't substitute a different region — that's the v0.14.95 mistake.**
- **Mid-drive replan must honor the same planner-eligibility gate as initial Start.**
- **Two-stage destination entry** (Fire Cavern, possibly other major dungeons): the world-map terrain trigger drops the player into an approach field, not the destination interior.
- **GitHub commit history is authoritative for "when did X change" questions.** Use `github:list_commits` before quoting any push state.
- **`ff8_nav_data.log` is the silent goldmine for spatial debugging.** Logs every player triangle change as `[timestamp] COORD field tri X Y ...` regardless of auto-pilot state — including manual runs.
- **Aaron's domain knowledge is ground truth, but his recipes need empirical verification.**
- **Multiple catch sources on one field may not all be active.** Always verify the `[CBF] PASS` caller (`entityPtr=`) against the actual entity identity.
- **EWM is load-bearing.** Preserve "first-to-fill acts first, no skipped turns, natural ally/enemy ratio". Default to pure mechanical splits unless Aaron explicitly approves a refactor.
- **Battle menu TTS is also load-bearing** (v0.16.5). Every command, spell name, GF name, item with qty, target selection, all-target announce, Stock/Cast, cancel-restore is user-facing. Pure mechanical splits only.
- **Navigation direction announcements are screen-relative, not world-relative** (v0.17.0). Cardinals map to arrow keys (north=up, east=right, south=down, west=left). World-space `atan2(dx, dy)` is wrong on rotated cameras — always project through `s_camRight/Down` first.
- **AUTO-DRIVE F9 path uses `s_camRight/Down`** (v0.17.6.0, quantized at field load). **CHASE-DRIVE uses `s_driveCam*`** (empirical CALIB, unchanged). Don't cross the streams.
- **F9 corridor-level steering is OFF** (v0.17.6.2, BAT-confirmed). Funnel waypoints + FF8 wall-sliding are F9's only steering. Chase-drive has been on this regime since v0.15.9.2.3.
- **One change per BAT cycle.** v0.15.9 chase work taught this the hard way (five wasted cycles chasing W timing when the bug was in a different code path).
- **Verifying user-facing features after a refactor requires comparing against a known-working baseline log.** Absence of an expected log line doesn't automatically mean the refactor broke it — it might be intermittent. If something looks suspicious, look at the install/resolution path first; if that fired, the runtime path is structurally identical.
- **AUTO `[CBF]` battle-suppressor cap stays `INT_MAX`** (Aaron 2026-05-13).
- Every Claude response starts with `## Claude Says`.
