**Purpose & context**

Aaron is the sole developer of the FF8 Accessibility Mod — a `dinput8.dll` injection for Final Fantasy VIII Steam 2013 (App ID 39150, FF8_EN.exe + FFNx v1.23.x) that makes the game playable for blind players via Windows SAPI TTS, navigation assistance, and entity catalogs. Aaron is blind and uses NVDA as his screen reader, and is also the primary tester.

**Project root:** `C:/Users/ampag/OneDrive/Documents/FFVIII-Accessibility-Mod/FF8_OriginalPC_mod/`

GitHub: `ampage87/FFVIII-Accessibility-Mod`. **GitHub HEAD = v0.17.8.10** (commit `59f1a9dd`, bug-#9 SegmentsCross fix, BAT-confirmed). **Local tree = v0.17.8.15.1** (v0.17.8.15 BAT confirmed the NPC mechanism works -- Xu now announces as NPC, not Interaction -- but exposed two follow-on bugs in the label/announce path: (A) the dedupe counter inflated kanban2 to "NPC 2" because it counted friendly-named ENT_NPC entries like Cid; (B) the announce sameType test used entityIdx >= 0 which failed for JSM-injected NPCs/Interactions, producing the "1 of 0" suffix. v0.17.8.15.1 fixes both: name-prefix counting in dedupe.inl + type-based sameType in announce.inl. Awaiting BAT.) Aaron pushes via `Utilities/push_to_github.ps1` (Claude never pushes); diagnostic builds stay LOCAL. **Size status:** all changed files comfortably under the 80 KB ceiling.

**bghall_1 save point — SOLVED + SHIPPED in v0.17.8.9 (BAT-confirmed; signal found via a now-removed LOCAL script dump):** the LOCAL dump of bghall_1 entities (zells/selphie/savePoint/saveline0) proved the save line is `ent5 'selphie'` (the SETLINE at (-700,-8593) currently shown as "Interaction 1"). Its script literally pushes the save-enable opcodes as constants: PUSH 303 (0x12F SAVEENABLE) and PUSH 304 (0x130 PHSENABLE) in BOTH method[6] (dwords 3624/3632) and method[7] (3657/3665). The control line `ent4 'zells'` has NONE of these (clean discriminator). Why the scanner missed it: selphie's ONLY 0x1C is the bare runtime-supplied dispatch in method[1] (`EXT_DISPATCH` empty-stack, like the dorm bed) — the save constants live in methods 6/7 and are never popped by a local 0x1C, so dispatch-resolution can't set foundSaveenable. savePoint (ent27) is unpositioned (X=PSHM135 Y=PSHM588, no SET3-shift) and its 0x1C resolves to a runtime PSHM; saveline0 (ent36) is a REQ-chain controller with a MAPJUMP (classified MAP_EXIT) and no statically-visible save op — so neither save-POINT entity can carry the label. **FIX (the chosen association, field-load, no cache, no heuristic guess): in the JSM scan, for a Line entity (jsmCategory==1) scan its full bytecode for literal PUSH of the save opcodes — set foundSaveenable when MENUSAVE(302) is present OR both SAVEENABLE(303) and PHSENABLE(304) are present. That makes signal-(a) fire -> isSaveLine -> the catalog surfaces selphie as "Save Point" at its own SETLINE center (-700,-8593), exactly where auto-drive already arrives.** Contrast (why the dorm bgryo2_1 already works): its savePoint gets a SET3-SHIFT position (229,97) and injects directly, and its saveline0 has a statically-visible save op + LATE-RESOLVE position — bghall_1 has neither, which is why the own-script-constant route on the LINE is the right fix here.

The v0.17.7.6.x chapter closed the bgroad_5 hallway calibration failure (full narrative in `DEVNOTES_HISTORY.md`); v0.17.8.0 closed bugs #5/#6, v0.17.8.1.1 closed #3, v0.17.8.3 closed #4, v0.17.8.4 removed a bogus camera catalog entry, v0.17.8.6 added the dorm bed + killed its duplicate exit, v0.17.8.7 filtered the `cardgamemaster` debug phantom + fixed the Event/Interaction double-injection that was also hiding the Directory, v0.17.8.8 added a general object/line dedupe (kanban signboard showed as both "Interaction 2" and "Kanban1" on bghall_2) plus a raw-SYM object relabel (standalone "Kanban2" on bghall_3 → "Interaction N"), and added save-line script-association detection. v0.17.8.9 completed that detection — the bghall_1 save point now labels via selphie's own-script save constants (see the save-point section above) — and refactored the two impacted .inl files back under the size ceiling. **Current chapter: bug #10 (Hall 6 Xu mislabeled) — IN PROGRESS (see #10 entry). Bug #9 SOLVED in v0.17.8.10, BAT-confirmed. Backlog: the runtime dialog-confirmation/disk-persistence layer (general answer to Director over-promotion).** Still deferred: Laguna dream bugs #7 (field-nav player detection) and #8 (battle announces wrong party).

---

## Where we are at session open

**v0.17.8.15.1 IN TREE -- label + announce follow-on fixes (LOCAL), awaiting BAT.** `FF8OPC_VERSION` = `0.17.8.15.1`, CHANGELOG top heading matches. GitHub HEAD = v0.17.8.10 (`59f1a9dd`). v0.17.8.11 through .15.1 are local-only deltas.

### v0.17.8.15.1 -- dedupe counter + announce sameType for JSM-injected NPCs/Interactions (CURRENT, awaiting BAT)

**BAT result for v0.17.8.15:** The NPC mechanism worked -- kanban2 now correctly typed NPC, not Interaction. But Aaron heard `"NPC 2, 1 of 0"` which is wrong on both counts. Field log confirmed: kanban2 (cat6, entityIdx=-325) was correctly relabeled via `[dedup] relabeled raw-SYM object 'kanban2' -> NPC 2 (Other + SETMODEL-init) [v0.17.8.15]`, but the catalog has 7 entries total and at least one of cat0-cat4 has type ENT_NPC (a runtime friendly-named NPC like Cid).

**Two distinct bugs:**

- **Dedupe counter inflated.** My v0.17.8.15 counter `if (newCatalog[c].type == ENT_NPC) n++` counted friendly-named NPCs (Cid, Quistis, etc.) toward the generic "NPC N" sequence. Fixed by counting only entries whose name literally starts with `"NPC %d"` -- the generic-relabel-only sequence.

- **Announce sameType missed JSM-injected types.** The pre-existing announce code in `field_nav_announce.inl` used `ce.entityIdx >= 0` for NPC sameType matching (legacy heuristic from before type classification was reliable). JSM-injected NPCs (entityIdx <= -300) failed this test, so they didn't count themselves in typeNum or typeTotal -> the `if (typeNum == 0) typeNum = 1` defaulted and typeTotal stayed 0 -> "1 of 0". Same bug applied to ENT_INTERACTION (no typeLabel branch existed at all, no sameType matched). This was the watch-list item flagged from v0.17.8.13/.14's `"Interaction 3 1 of 0"`. Fixed by adding type-based sameType clauses for both NPC (alongside the legacy entityIdx clause) and Interaction (new), plus a typeLabel branch for ENT_INTERACTION.

**Files touched:** `field_nav_catalog_dedupe.inl` (counter), `field_nav_announce.inl` (typeLabel cascade + 2 sameType cascades), `ff8_accessibility.h` (version bump), `CHANGELOG.md` (new entry).

**BAT expectation on bghall_3:**
- kanban2 -> `"NPC 1 1 of 1"` (or `"NPC 1 X of Y"` where Y includes friendly-named NPCs now too, depending on what's in cat0-cat4)
- line3 / line4 -> `"Interaction 1 1 of 2"` / `"Interaction 2 2 of 2"` (unchanged -- trigger-line path is untouched)
- Log line: `[dedup] relabeled raw-SYM object 'kanban2' -> NPC 1 (Other + SETMODEL-init) [v0.17.8.15.1]`
- Friendly-named NPCs (if present in catalog, e.g. Cid) announce by name with the new type-based count -- e.g. `"Cid 1 of 2"` if Cid + kanban2 are both in catalog.

### v0.17.8.15 -- chara.one chain reverted, clean JSM-behavior fix (BAT'd 2026-05-27, NPC mechanism confirmed)

**Why the revert.** Aaron's BAT screenshot of bghall_3 (`Logs/screenshots/f11_204546_707.png`) showed Xu visibly standing in front of Squall at the kanban2 spot, with the dialog box reading `Xu "Hey, Squall, heard you got your first mission already!"`. There is no signpost. The internal SYM name "kanban2" is misleading. This disproved the v0.17.8.13/.14 conclusion ("kanban2 IS a sign, chara.one classifier is correct, mechanism is right just doesn't help this specific case"). The chara.one classifier had returned `isChar=0` for p048 but p048 IS Xu's character model on this field -- the classifier was wrong. More fundamentally, file-level classification (is this model a character?) was the wrong question entirely. The right question is gameplay behavior: does the player walk up + Confirm (NPC) or walk across (Interaction)?

**Clean fix:** JSM behavior signal already in the data we scan. `jsmCategory == 3 (Other) && hasSetmodelInit` -> "NPC N". Everything else (Line, Background, no SETMODEL) -> "Interaction N". Per Aaron's directive, NPC labels never expose SYM names -- pure "NPC N" + the existing " X of Y" announce-time suffix.

**Files touched:** field_charaone_parse.{h,cpp} stubbed (no longer in deploy.bat); ff8_addresses.{h,cpp} chara.one block + Resolve __try block removed; dinput8.cpp Init/Shutdown calls + include removed; field_navigation.cpp include removed; deploy.bat field_charaone_parse.cpp line removed; field_archive.h `int setmodelSlot` -> `bool hasSetmodelInit`; field_archive_jsm_scan.inl setmodelSlotInit + opcParam capture removed, replaced with `info.hasSetmodelInit = foundSetmodelInit`; field_nav_catalog_dedupe.inl NPC-override block rewritten + [NPC-skip] diagnostic removed; ff8_accessibility.h version bumped to 0.17.8.15; CHANGELOG.md entry added.

**BAT expectations on bghall_3:**
- line3 -> "Interaction 1" (unchanged)
- line4 -> "Interaction 2" (unchanged)
- kanban2 -> "NPC 1" (was: "Interaction 3") -- this is the visible fix
- F9 nav-cycle: "NPC 1, 1 of 1" at kanban2's position (or whatever the announce-time suffix produces)
- Log line: `[dedup] relabeled raw-SYM object 'kanban2' -> NPC 1 (Other + SETMODEL-init) [v0.17.8.15]`
- GONE: any `[NPC-skip]` lines (v0.17.8.12 diagnostic removed)
- GONE: any `[JSMScan] SETMODEL-init ...` lines (v0.17.8.13 diagnostic removed)
- GONE: any `FieldCharaOneParse:` log lines (entire module gone)

**Watch list during BAT:** the pre-existing "Interaction 3 1 of 0" announce bug from v0.17.8.13/.14. If the announce code reports "NPC 1 1 of 0" instead of "NPC 1 1 of 1", that's a separate counter bug in field_navigation.cpp's announce path -- log + investigate but don't gate on it.

### v0.17.8.11 - v0.17.8.14 (REVERTED in v0.17.8.15)

**What v0.17.8.13 BAT proved:** SETMODEL takes its chara.one slot index INLINE in the opcode word's low 24 bits (`opcParam`), not on the script VM stack. Every entity across four BAT'd fields (bgryo2_1, bgroad_5, bghall_5, bghall_3) had `pushCount=0 stk=(empty)` and `opcParam` matching the sequential slot index. v0.17.8.11's stack-based grab rejected every entity because the `pushCount > 0` gate was never true.

**This build:** replaces the v0.17.8.13 diagnostic block AND the v0.17.8.11 stack capture in `field_archive_jsm_scan.inl` with a single `opcParam`-based read (same range/sentinel guards). Net change to scan.inl is a small reduction (~30 lines diagnostic removed, ~20 lines fix added). No other files touched in this delta.

### Bug C: kanban2 is genuinely a prop -- discovered, NOT fixed in v0.17.8.14

The v0.17.8.13 dump also revealed an unrelated finding that matters for Aaron's user experience but doesn't change the mechanism fix:

- `ent25 'kanban2' opcParam=0x00000D` -> chara.one slot 13 = `p048` -> classifier correctly says PROP.
- bghall_3 DOES have an Xu character entity: `ent13 'shu'` (Japanese romanization), loading character slot 1 = d000. But shu has no navigable position in current story state, so she's not in the catalog.
- kanban2 is positioned at (4626,-3459) -- a bulletin board. Examining it kicks off a story script that REQs Xu's walk-up animation and dialog. The 5-second delay between catalog arrival and dialog firing in the v0.17.8.11 BAT is her walk-up.

**So after v0.17.8.14:** kanban2 gets `setmodelSlot=13`, `IsCharacterModel(170, 13)` correctly returns false (it IS a prop), the NPC override correctly does NOT fire, and kanban2 stays labeled "Interaction 3". The mechanism is now correct -- it just doesn't change kanban2's specific label.

### BAT expectations for v0.17.8.14

- Mod log: `FieldCharaOneParse: hooked chara_one_read_file at 0x...` at startup (unchanged).
- Field log on every visited field: the `[JSMScan] SETMODEL-init ...` lines from v0.17.8.13 are GONE (diagnostic removed).
- Field log on bghall_3: `[dedup] NPC-skip 'kanban2': fid=170 setmodelSlot=13 fieldParsed=1 isChar=0 [v0.17.8.12]` -- note `setmodelSlot=13` (was -1). isChar=0 because p048 is a prop. This is correct behavior.
- Field log on bghall_3: `[dedup] relabeled raw-SYM object 'kanban2' -> Interaction 3 [v0.17.8.8]` -- unchanged.
- F9 announces "Interaction 3" for kanban2. **Unchanged from before.** This is the expected outcome of fixing the mechanism on a genuinely-prop entity.
- F9 on OTHER fields where a positioned, raw-SYM character entity exists: should now correctly relabel to NPC via the v0.17.8.11 success path. (No specific test field identified -- if you find one during normal play that previously labeled "Interaction N" but should be a person, that's the verification.)

### Decision point for v0.17.8.15

With the mechanism now correct, the kanban2 = Xu user-facing issue needs a separate fix. Three options when next session opens:

- **A. Accept it.** kanban2 IS a sign. "Interaction 3" is technically correct.
- **B. REQ-chain analysis.** If a raw-SYM object REQs into an entity whose model IS a character, label this entity "NPC". Catches kanban2 -> shu chain. More complex; requires the REQ resolver to follow the REQ chain to the character entity and check its setmodelSlot.
- **C. Prop-name labeling.** Surface known-prop kanban entities as "Bulletin board" instead of "Interaction N". Simpler than B; doesn't communicate that there's a person involved.

Aaron's call.

### v0.17.8.13 — SETMODEL-init diagnostic (BAT'd, mechanism confirmed)

Diagnostic delivered the answer in one BAT. See CHANGELOG.md v0.17.8.13 entry.

### v0.17.8.12 — chara.one Mch=Char (BAT'd, Bug A fixed; Bug B exposed)

Fix still in place. `[NPC-skip]` diagnostic stays one more cycle.

### v0.17.8.11 — chara.one classifier hook (BAT'd, classification axis fixed in .12)

Full narrative in `CHANGELOG.md`.

### What was here before (now in history)

The v0.17.8.7 cardgamemaster narrative + v0.17.8.6 dorm-bed rationale that previously held this section will be moved to `DEVNOTES_HISTORY.md` on the next size-trim pass.



### NEW bugs found in the first Laguna dream (gwgrass1) — separate chapters

7. **Laguna dream field nav fully broken.** Player entity not detected: log shows `player=ent-1` and every auto-drive attempt logs `[drive] REFUSED ... player_pos_known=0 player_entityIdx=-1`. The `setpc==0` player-detection heuristic in RefreshCatalog/Update fails in the Laguna dream (no entity has setpc==0, or the dream player uses a different marker). This breaks F9 navigation entirely in Laguna sequences. Needs its own diagnostic (dump setpc for all entities on gwgrass1).
8. **Laguna dream battle announces the real party.** Battle TTS says Squall/Zell/Selphie instead of Laguna/Kiros/Ward. The savemap formation still holds the real party char IDs during the dream (gwgrass1 formation logged as [5,0,1,255] = the real party). Battle-side fix — the dream party is swapped in via a different mechanism than the savemap formation array. Separate chapter.
9. **B-Garden hub Hall 4 exit missing — SOLVED (v0.17.8.10, BAT-confirmed 2026-05-27).** Correct field is the HUB `bghall_5` (field 174, display "Hall 10"), NOT bgroad_5 — the original report's field was wrong. Its only path to Hall 4 (`bghall_2`, field 168, INF dest alias `feclock1`) is an INF gateway with no SETLINE. The catalog gateway screen-filter used `IsSeparatedByTriggerLine()`, an INFINITE-line side test; the Hall 6 doorway exit (line9, SCREEN_BOUND, short segment on the far EAST edge x in [4206,5042]) extended to infinity passed between the player and the west-edge Hall 4 gateway (center -4572,3777, whose Y≈3777 lay on that line's extension), so it was filtered every refresh. Confirmed by a [gw-diag] read-only capture (removed). FIX: added `SegmentsCross()` (bounded segment-vs-segment intersection) in `field_navigation.cpp`; the gateway filter now hides a gateway only if the player→gateway SEGMENT actually crosses a boundary SEGMENT. Entity screen-filtering still uses the infinite-line helper (unchanged — minimal blast radius). Contrast that confirmed the mechanism: Hall 6's single gateway → Hall 10 always surfaced (no short edge-line collinear with it).
10. **B-Garden Hall 6 (`bghall_3`, field 170) -- NPC Xu labeled "Interaction 3" -- v0.17.8.15 FULL REVERT + CLEAN FIX, awaiting BAT.** Xu = JSM `kanban2` (ent25, cat3, PSHM pos (4626,-3459)). Resolution arc:

   **The wrong path (v0.17.8.11-.14, all REVERTED):** built a chara.one model-archive parser + MinHook on `chara_one_read_file` to classify NPC vs prop by reading the model file header. Successive bug fixes: Bug A (isMch flag in v0.17.8.12), Bug B (SETMODEL opcParam vs stack in v0.17.8.14). v0.17.8.13/.14 concluded kanban2 = prop because p048 classified as prop.

   **The disproof (v0.17.8.15):** Aaron's screenshot (`Logs/screenshots/f11_204546_707.png`) shows Xu visibly standing as a character model in front of Squall at the kanban2 spot, with dialog box `Xu "Hey, Squall, heard you got your first mission already!"`. There is no signpost. The classifier was wrong about p048 (it IS a character model on this field, regardless of the 'p' prefix convention). And more fundamentally: file-level model classification was the wrong mechanism. What matters is the gameplay role.

   **The clean fix (v0.17.8.15):** behavior signal already in the JSM scan -- `jsmCategory == 3 (Other) && hasSetmodelInit` -> "NPC N". Everything else (Line walk-across, Background script-only) -> "Interaction N". No chara.one needed. Per Aaron's directive, NPC labels stay generic "NPC N" -- no SYM names exposed.

   **What this teaches (carry forward):** SYM names are unreliable as identifiers (kanban2 IS Xu). File-level model classification is unreliable as a behavior signal (p048 IS a character on bghall_3, regardless of prefix convention). When the question is "how does the player interact with this entity", look at the JSM behavior signals (jsmCategory, hasSetmodelInit, hasDialogReqTarget, hasTalkSetup, foundExtDispatch), not at the model file.

   **BAT expectation for v0.17.8.15:** kanban2 -> "NPC 1" with log line `[dedup] relabeled raw-SYM object 'kanban2' -> NPC 1 (Other + SETMODEL-init) [v0.17.8.15]`. line3/line4 still "Interaction 1"/"Interaction 2". No `[NPC-skip]`, no `[JSMScan] SETMODEL-init`, no `FieldCharaOneParse:` lines anywhere.

**v0.17.8.1.1 (pushed) closed bug #3.** Fire Cavern playthrough bug list (Aaron's 2026-05-18 report) progress:
1. Quistis' FMV in the Infirmary fired prematurely — deferred
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
