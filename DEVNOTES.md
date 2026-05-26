**Purpose & context**

Aaron is the sole developer of the FF8 Accessibility Mod — a `dinput8.dll` injection for Final Fantasy VIII Steam 2013 (App ID 39150, FF8_EN.exe + FFNx v1.23.x) that makes the game playable for blind players via Windows SAPI TTS, navigation assistance, and entity catalogs. Aaron is blind and uses NVDA as his screen reader, and is also the primary tester.

**Project root:** `C:/Users/ampag/OneDrive/Documents/FFVIII-Accessibility-Mod/FF8_OriginalPC_mod/`

GitHub: `ampage87/FFVIII-Accessibility-Mod`. **GitHub HEAD = v0.17.8.4** (commit `cb20fd88`, parent `17e6bd77` v0.17.8.3). Local tree at **v0.17.8.5.2** (dorm bed-ID runtime probe, iteration 3, in progress). Bugs #2, #3, #4, #5, #6 from the Fire Cavern list all closed.

The v0.17.7.6.x chapter closed the bgroad_5 hallway calibration failure (full narrative in `DEVNOTES_HISTORY.md`); v0.17.8.0 closed bugs #5 and #6, v0.17.8.1.1 closed bug #3, v0.17.8.3 closed bug #4, v0.17.8.4 removed a bogus camera catalog entry, from Aaron's 2026-05-18 Fire Cavern playthrough report. Current chapter: **v0.17.8.5 dorm-room catalog gaps (missing bed interaction + duplicate exit on bgryo2_1)**, then the Laguna dream bugs #7 (field-nav player detection) and #8 (battle announces wrong party).

---

## Where we are at session open

**v0.17.8.5.2 in tree — dorm bed-ID runtime probe (iteration 3), awaiting BAT.** `FF8OPC_VERSION` = `0.17.8.5.2`, CHANGELOG matches. GitHub HEAD = v0.17.8.4 (`cb20fd88`, pushed). Diagnostic builds stay LOCAL (not pushed; precedent: v0.17.8.2 went straight to the v0.17.8.3 fix). Aaron pushes via `Utilities/push_to_github.ps1` (Claude never pushes).

### v0.17.8.5.x — dorm-room catalog gaps (bgryo2_1) — DIAGNOSTIC chain

bgryo2_1 (Squall's dorm, SeeD-uniform scene). Expected catalog: 1 interaction (bed), 1 save point, 1 exit. Actual (v0.17.8.4): save point + TWO exits, no bed. Both gaps PREDATE recent builds.

**SETTLED (decisive):**
1. **Bed = an AASK rest prompt.** `Logs/ff8_dialog.log` on interaction: `[AASK]` "Squall 'I should get some rest.'" choices Rest / Don't rest. So the bed shows a 2-choice dialog (not a cutscene, not a text-less rest).
2. **Root cause of `dialog=0` everywhere = 0x1C dispatch.** On this field the dialog opcodes (MES 0x47, ASK 0x4A, AMES 0x65, AASK 0x6F — all <0x100) are dispatched through the `0x1C` ext-dispatch prefix (sub-opcode pushed as a literal, then `0x1C`). The opcode histogram tops out at `0x34`; `0x1C` appears 20x; no dialog high-byte appears. The scanner's per-entity dialog check (`s_hasDialogAny`, in the `ScanJSMScripts` opcode loop) tests only the HIGH BYTE against MES/ASK/AMES/AASK, so it sees `0x1C` (sets `s_hasExtDispatchArr`/extDisp=1) and never recognizes the dialog. `DumpEntityScript` DOES resolve `0x1C`->sub-opcode, which is why dumps can show it. **This is the core bug to fix.**
3. **`suit` (ent19) = cutscene controller, not the bed** (REQ/REQEW the zell/zells anim methods 130-141, positionless). The earlier "ent0 REQs interactive entity" was the fallback heuristic (reqTgts=[] empty), not real.
4. **Duplicate exit confirmed.** `ent15 'l1'` = positionless MAP_EXIT, `param=-2147483648` (unresolved runtime-var dest). Real exit = `ent1 'squalls'` SCREEN_BOUND -> Hall 10 (resolves via VARBLOCK 0x800000AE -> field 174; its line center (12,5)).

**SETTLED via the v0.17.8.5.2 runtime probe: the bed = `ent0 'squall'`, the Event line, at SETLINE center (-50,496).** `Logs/ff8_dialog.log` on bed interaction: `[AASK-ENTITY] entityPtr=0x0188B818` (repeated). This load's `[SETLINE] call#1 ent=0x0188B818 idx=1` = line 0; entity stride 0x1A0; so index = (0x0188B818-0x0188B818)/0x1A0 = 0 = ent0. The AASK runs in ent0's OWN script (not via a REQEW to another entity).

**Why the scanner misses it (RESOLVED):** the opcode constants are CORRECT, not wrong — `ff8_addresses.h` documents the engine's own dispatch table and `opcode_aask` is index `0x6F`, matching `JSM_OP_AASK=0x6F` (MES 0x47, ASK 0x4A, AMES 0x65 also confirmed). The encoding is confirmed high-byte (ip 3761 MAPJUMP3 = word `0x2A000062` -> opcode 0x2A). Yet ent0's full script (all 8 methods dumped) has NO `0x6F` opcode and NO resolvable `0x1C`. The only `0x1C` in ent0 is method[1]: a BARE `EXT_DISPATCH` (logged `[JSMScan] 0x1C EMPTY STACK: ent=0 method=1`) whose dispatch index is supplied at RUNTIME (not by a preceding literal push the static scan can see), so the scan can only set `extDisp=1`, never `foundDialogOp`. That runtime-supplied `0x1C` dispatch is how the rest prompt (AASK) is reached, and it is exactly what a static high-byte scan cannot resolve. So a pure scan-side dialog-detection fix CANNOT see this bed; the fix must use a signal that survives runtime dispatch.

**Signals ent0 DOES carry at scan time:** classified `LINE_EVENT` (foundEventOp from method[2] REQEW); `extDisp=1` (the bare 0x1C); `hasDialogReqTarget=1` (but only via the weak fallback: unresolved REQ + an IntObj 'suit' happens to exist on this field — fragile, would not fire on a field with no IntObj); a captured SETLINE position (-50,496). The catalog currently SKIPS `LINE_EVENT` (v0.12.12) and only surfaces dual-purpose `SCREEN_BOUND` lines via `hasDialogReqTarget`.

**v0.17.8.6 FIX = approach C, runtime-authoritative (Aaron's call 2026-05-26). SCOPE: bgryo2_1 itself only needs bed+save+exit; the mechanism must be field-agnostic/robust because OTHER fields have interactive background objects reached the same runtime-0x1C way.** Validated against the real RefreshCatalog (read this session):
  - Block 3 ("Interaction N") already surfaces `LINE_INTERACTIVE` captured lines at their SETLINE center via sentinel `-200-t`. `LINE_EVENT` is surfaced by NO block — that is exactly why the bed vanishes. Background interactive objects already have a path (scan promotes to `INTERACTIVE_OBJECT` when extDisp + position, e.g. Directory 'dic').
  - **Robust general signal = an entity that actually fires MES/ASK/AMES/AASK at runtime is interactive, period.** The six dialog hooks in field_dialog_opcodes.inl already receive the executing `entityPtr`. Mechanism: (1) each hook calls new `FieldNavigation::NoteRuntimeDialogEntity(entityPtr)`; (2) it resolves the entity by matching `s_capturedLines[].entityAddr` (line, e.g. bed) or the runtime others-array range (background/other), records a stable identity (fieldName + lineOrder / entity index) in a persistent per-field set (in-memory, resets on game restart); (3) RefreshCatalog re-applies the set each build and surfaces marked lines/entities as Interactions at SETLINE-center / struct pos, independent of the static LINE_EVENT/INTERACTIVE classification the scan can't derive. First visit: bed appears the instant you cross it; revisits: already there.
  - Static half stays CONSERVATIVE on purpose (runtime is authoritative; do NOT pre-surface speculative LINE_EVENT lines field-wide — that is the over-surfacing trap).
  - Same build also: suppress dead `l1` exit (positionless JSM MAP_EXIT with unresolved dest, even when s_gatewayCount==0 — add `if (!hasPos && (je.param<0 || je.param>=FIELD_DISPLAY_NAMES_COUNT)) continue;` in the MAP_EXIT block) and REMOVE all dorm diagnostics (scan `[dorm-diag]` block + dialog `[ASK-ENTITY]`/`[AASK-ENTITY]` lines).
  - Files: field_dialog_opcodes.inl (add note call x6, strip diag), field_navigation.h (decl), field_navigation.cpp (CapturedTriggerLine += runtimeInteractive; new registry + NoteRuntimeDialogEntity + re-apply at RefreshCatalog start), field_nav_catalog.inl (Block 3 surface runtimeInteractive lines; l1 suppression), field_archive_jsm_scan.inl (drop [dorm-diag]). PENDING Aaron OK on in-memory keying before implementation.

**[STATUS] v0.17.8.6 = STATIC HALF IMPLEMENTED LOCAL (not pushed), awaiting BAT.** Aaron's 2026-05-26 requirement clarified: detection MUST happen BEFORE the player crosses/triggers (runtime-only is useless for FINDING things), so static pre-detection leads. Done in v0.17.8.6:
  - `field_archive_jsm_scan.inl` Line classification: new `else if (foundExtDispatch)` branch -> `LINE_INTERACTIVE`, ordered AFTER mapjump/battle/bgdraw-scroll so exits/battle/camera lines keep their type. ent0 (extDisp=1, no mapjump/battle/camera) -> INTERACTIVE -> catalog Block 3 surfaces it at SETLINE center (-50,496) at field load. Symmetric with the cat2/3 INTERACTIVE_OBJECT extDisp proxy. foundDialogOp beds unaffected.
  - `field_nav_catalog.inl` MAP_EXIT block: drop exits with no position AND unresolved dest (param<0 || >=FIELD_DISPLAY_NAMES_COUNT, keep -2 worldmap) -> kills the dead 'l1' duplicate exit.
  - Removed `[dorm-diag]` scan block + `[ASK-ENTITY]`/`[AASK-ENTITY]` dialog logging. Version bumped to 0.17.8.6 (ff8_accessibility.h + CHANGELOG).
  - Known tradeoff to watch on BAT: a Line whose 0x1C is only sound/particle (no dialog) surfaces as a phantom 'Interaction'. Aaron to check field-wide over-surfacing.

**[TODO] v0.17.8.7 = RUNTIME CONFIRMATION + DISK PERSISTENCE (Aaron wants persist-across-restarts).** The six dialog hooks call new `FieldNavigation::NoteRuntimeDialogEntity(entityPtr)`; resolve entity via s_capturedLines[].entityAddr (line) or runtime others/background array (bg/other); record stable identity (fieldName + lineOrder/entityIndex) to a DISK-persisted per-field registry (survives restarts; becomes a shippable known-objects DB). RefreshCatalog re-applies it each build. Purpose: (a) catch interactive objects the static extDisp proxy misses, (b) CONFIRM/label static guesses, (c) optionally demote static phantoms that never fire dialog. Files: field_dialog_opcodes.inl, field_navigation.h, field_navigation.cpp, field_nav_catalog.inl.

**Entity runtime layout (proven):** field entity stride = `0x1A0` (the three `[SETLINE]` line pointers `0x0188B818 / 0x0188B9B8 / 0x0188BB58` are exactly `0x1A0` apart); lines are entities 0/1/2; `[SETLINE]` idx encodes the line index as `idx>>8` (call#1 idx=1->line0 center(-50,496); call#2 idx=257->line1 'squalls' (12,5); call#3 idx=513->line2 'squallsd' (73,704)). So JSM entity index = `(entityPtr - line0SetlinePtr) / 0x1A0`.

**v0.17.8.5.2 change:** added `[ASK-ENTITY]`/`[AASK-ENTITY] entityPtr=0x...` logging to the existing `opcode_ask`/`opcode_aask` hooks in `field_dialog_opcodes.inl` (they already receive `int entityPtr`). No other change; the scan-side `[dorm-diag]` block from v0.17.8.5.1 is still present (harmless; ff8_field.log will again be ~400KB — only its head is needed next time).

**BAT plan:** reload bgryo2_1, interact with the bed, send `Logs/ff8_dialog.log` (tiny; has `[AASK-ENTITY] entityPtr`) + head of `Logs/ff8_field.log` (for the `[SETLINE]` base ptr). Compute bed entity index. Then build v0.17.8.6.

### v0.17.8.6 FIX design (next; will be pushed)
Three parts, then remove ALL dorm diagnostics (the `[dorm-diag]` scan block in `field_archive_jsm_scan.inl` AND the `[ASK-ENTITY]`/`[AASK-ENTITY]` logging in `field_dialog_opcodes.inl`):
1. **Dialog detection through 0x1C (core).** In the `ScanJSMScripts` per-entity opcode loop, when high byte == `0x1C`, resolve the pushed sub-opcode (as `DumpEntityScript` does) and test IT against MES/ASK/AMES/AASK so `s_hasDialogAny` is set for `0x1C`-dispatched dialog. The bed entity then gets real `dialog=1`.
2. **Surface the bed with a position.** Once the bed entity has dialog=1, classify/surface it as an interaction in `RefreshCatalog` (`field_nav_catalog.inl`). If it's a LINE (e.g. ent0), it's currently skipped by the event-line block (v0.12.12) AND positionless — give it its SETLINE center (idx>>8 maps line->entity; ent0 -> (-50,496)). If it's an Other, use its SET3/paired coords. Be careful not to surface genuine cutscene-trigger event lines elsewhere — gate on real dialog=1.
3. **Suppress the duplicate exit.** In the catalog MAP_EXIT injection loop, drop a MAP_EXIT with NO position AND an unresolved/marker param (param==INT_MIN / 0x8000xxxx). Safe: positionless exits aren't navigable; the real Hall 10 screen-boundary remains.
[OPEN: confirm after the probe that the bed entity then classifies + positions correctly; may need one more BAT.]

### Bug #4 — party member announced as NPC — CLOSED (pushed v0.17.8.3); v0.17.8.4 camera entry — CLOSED (pushed cb20fd88)

Bug #4: name-based party filter in `field_nav_catalog.inl` (`IsPartyCharacterSym()`), skips no-talk/push entities that are following party members (model 0-9, thru>0) OR party-character-named (any model/thru). BAT-confirmed on bgryo2_1 (all six party entities filtered incl. model-11 selphie). v0.17.8.4: name-scoped `camera` guard in `field_archive_jsm_director.inl` promotion loop; the bogus 'Camera' catalog entry is gone, Director still promotes real interactables. Both pushed.

### NEW bugs found in the first Laguna dream (gwgrass1) — separate chapters

7. **Laguna dream field nav fully broken.** Player entity not detected: log shows `player=ent-1` and every auto-drive attempt logs `[drive] REFUSED ... player_pos_known=0 player_entityIdx=-1`. The `setpc==0` player-detection heuristic in RefreshCatalog/Update fails in the Laguna dream (no entity has setpc==0, or the dream player uses a different marker). This breaks F9 navigation entirely in Laguna sequences. Needs its own diagnostic (dump setpc for all entities on gwgrass1).
8. **Laguna dream battle announces the real party.** Battle TTS says Squall/Zell/Selphie instead of Laguna/Kiros/Ward. The savemap formation still holds the real party char IDs during the dream (gwgrass1 formation logged as [5,0,1,255] = the real party). Battle-side fix — the dream party is swapped in via a different mechanism than the savemap formation array. Separate chapter.

**v0.17.8.1.1 (pushed) closed bug #3.** Fire Cavern playthrough bug list (Aaron's 2026-05-18 report) progress:
1. Quistis' FMV in the Infirmary fired prematurely — deferred
2. ~~Manual field navigation direction lag and inaccurate direction guidance~~ — ✅ closed by v0.17.7.6.2
3. ~~Garbage announced by TTS following completion of a tutorial scene~~ — ✅ closed by v0.17.8.1.1
4. ~~Party member announced as NPC in catalog~~ — ✅ closed by v0.17.8.3 (BAT-confirmed bgryo2_1; pending push)
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
