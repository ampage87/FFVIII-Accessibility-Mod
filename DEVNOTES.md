**Purpose & context**

Aaron is the sole developer of the FF8 Accessibility Mod — a `dinput8.dll` injection for Final Fantasy VIII Steam 2013 (App ID 39150, FF8_EN.exe + FFNx v1.23.x) that makes the game playable for blind players via Windows SAPI TTS, navigation assistance, and entity catalogs. Aaron is blind and uses NVDA as his screen reader, and is also the primary tester.

**Project root:** `C:/Users/ampag/OneDrive/Documents/FFVIII-Accessibility-Mod/FF8_OriginalPC_mod/`

GitHub: `ampage87/FFVIII-Accessibility-Mod`. **GitHub HEAD = v0.17.8.7** (commit `4f36638b`, pushed + BAT-confirmed). **Local tree = v0.17.8.9 IN TREE, awaiting BAT** — folds in the unpushed v0.17.8.8 catalog-naming work plus the bghall_1 save-point fix and a size refactor. v0.17.8.8 pieces: (1) object/line dedupe (BAT-confirmed: Hall 4 kanban removed), (2) raw-SYM object relabel → "Interaction N" (BAT-confirmed: Hall 6 'kanban2' → "Interaction 3"). v0.17.8.9 pieces: (3) save-line own-script-constant detection now FIRES on bghall_1 — selphie's literal 0x12F/0x130 sets isSaveLine → "Save Point" (see the save-point section below); and (4) the size refactor. `FF8OPC_VERSION` = `0.17.8.9`, CHANGELOG top heading matches. Aaron pushes via `Utilities/push_to_github.ps1` (Claude never pushes); diagnostic builds stay LOCAL. **Size status (the former HARD BLOCKER is RESOLVED): the v0.17.8.8 dedupe+relabel pass was extracted from `field_nav_catalog.inl` into a new statement-fragment file `field_nav_catalog_dedupe.inl` (`#include`d inline mid-RefreshCatalog, byte-identical behavior), and `field_archive_jsm_scan.inl` lost four dead `if(false)` diagnostics + the LOCAL dump diag. Both now have headroom: catalog 74,387 B, scan 75,590 B. SEPARATE pre-existing issue: `field_nav_autodrive.inl` is 80,517 B (already over 80,000) — untouched this session; address if/when CI flags it.**

**bghall_1 save point — SOLVED by the v0.17.8.9 script dump (signal found):** the LOCAL dump of bghall_1 entities (zells/selphie/savePoint/saveline0) proved the save line is `ent5 'selphie'` (the SETLINE at (-700,-8593) currently shown as "Interaction 1"). Its script literally pushes the save-enable opcodes as constants: PUSH 303 (0x12F SAVEENABLE) and PUSH 304 (0x130 PHSENABLE) in BOTH method[6] (dwords 3624/3632) and method[7] (3657/3665). The control line `ent4 'zells'` has NONE of these (clean discriminator). Why the scanner missed it: selphie's ONLY 0x1C is the bare runtime-supplied dispatch in method[1] (`EXT_DISPATCH` empty-stack, like the dorm bed) — the save constants live in methods 6/7 and are never popped by a local 0x1C, so dispatch-resolution can't set foundSaveenable. savePoint (ent27) is unpositioned (X=PSHM135 Y=PSHM588, no SET3-shift) and its 0x1C resolves to a runtime PSHM; saveline0 (ent36) is a REQ-chain controller with a MAPJUMP (classified MAP_EXIT) and no statically-visible save op — so neither save-POINT entity can carry the label. **FIX (the chosen association, field-load, no cache, no heuristic guess): in the JSM scan, for a Line entity (jsmCategory==1) scan its full bytecode for literal PUSH of the save opcodes — set foundSaveenable when MENUSAVE(302) is present OR both SAVEENABLE(303) and PHSENABLE(304) are present. That makes signal-(a) fire -> isSaveLine -> the catalog surfaces selphie as "Save Point" at its own SETLINE center (-700,-8593), exactly where auto-drive already arrives.** Contrast (why the dorm bgryo2_1 already works): its savePoint gets a SET3-SHIFT position (229,97) and injects directly, and its saveline0 has a statically-visible save op + LATE-RESOLVE position — bghall_1 has neither, which is why the own-script-constant route on the LINE is the right fix here.

The v0.17.7.6.x chapter closed the bgroad_5 hallway calibration failure (full narrative in `DEVNOTES_HISTORY.md`); v0.17.8.0 closed bugs #5/#6, v0.17.8.1.1 closed #3, v0.17.8.3 closed #4, v0.17.8.4 removed a bogus camera catalog entry, v0.17.8.6 added the dorm bed + killed its duplicate exit, v0.17.8.7 filtered the `cardgamemaster` debug phantom + fixed the Event/Interaction double-injection that was also hiding the Directory, v0.17.8.8 added a general object/line dedupe (kanban signboard showed as both "Interaction 2" and "Kanban1" on bghall_2) plus a raw-SYM object relabel (standalone "Kanban2" on bghall_3 → "Interaction N"), and added save-line script-association detection. v0.17.8.9 completed that detection — the bghall_1 save point now labels via selphie's own-script save constants (see the save-point section above) — and refactored the two impacted .inl files back under the size ceiling. **Current chapter: v0.17.8.9 (in tree, awaiting BAT) — bghall_1 save-point label DONE; remaining open threads are the runtime dialog-confirmation/disk-persistence layer (the general answer to Director over-promotion) and the two new B-Garden bugs (#9 Hallway 5 missing Hall 4 exit, #10 Hall 6 Xu mislabeled).** Still deferred: Laguna dream bugs #7 (field-nav player detection) and #8 (battle announces wrong party).

---

## Where we are at session open

**v0.17.8.7 IN TREE — cardgamemaster name filter + catalog double-injection fix, awaiting re-BAT.** `FF8OPC_VERSION` = `0.17.8.7`, CHANGELOG matches. GitHub HEAD = v0.17.8.6 (`e415be44`, pushed). Unpushed iteration history this session: (a) init-var testbl filter FAILED BAT; (b) replaced with SYM-name filter — cardgamemaster gone (✓) but exposed two catalog bugs; (c) added the Event/Interaction double-injection fix (item (1b) below). Aaron pushes via `Utilities/push_to_github.ps1` (Claude never pushes); diagnostic builds stay LOCAL.

### bgryo2_1 dorm bed — RESOLVED & PUSHED (v0.17.8.6)

Kept here as reference because the mechanism recurs. The bed = **`ent0 'squall'`, a trigger Line, SETLINE center (-50,496)**. Its "I should get some rest" AASK fires in ent0's own script via a **bare `0x1C` ext-dispatch whose sub-opcode index is supplied at RUNTIME** (logged `0x1C EMPTY STACK: ent=0 method=1`). Opcode constants are CORRECT (engine dispatch table in ff8_addresses.h: AASK=0x6F, MES 0x47, ASK 0x4A, AMES 0x65); encoding is high-byte (ip MAPJUMP3 word `0x2A000062`). The static scan cannot resolve a runtime-supplied 0x1C to a dialog opcode, so `foundDialogOp`=false and the line fell to `LINE_EVENT` (which the catalog hides). **Fix:** scan now routes a Line with `extDisp` (own 0x1C) that is NOT mapjump/battle/camera-scroll to `LINE_INTERACTIVE` (catalog Block 3 surfaces it at SETLINE center, at field load, before crossing) — symmetric with the cat2/3 INTERACTIVE_OBJECT extDisp proxy. Duplicate exit (`ent15 'l1'`, positionless MAP_EXIT, param=INT_MIN) suppressed in the catalog MAP_EXIT block. Diagnostics removed.

**Reusable facts:** field entity runtime stride = `0x1A0`; lines are entities 0/1/2; `[SETLINE]` idx encodes line index as `idx>>8`; JSM entity index = `(entityPtr - line0SetlinePtr)/0x1A0`. Catalog interactive-line surfacing = Block 3 in `field_nav_catalog.inl` (sentinel `-200-t`, SETLINE center). Background interactive objects surface via the JSM-injected block (sentinel `-300-j`, INTERACTIVE_OBJECT, SET3/struct position).

### v0.17.8.7 — phantom interactive objects + runtime confirmation (CURRENT)

**Trigger:** BAT of v0.17.8.6 found a phantom on **bgroad_5** (dorm corridor): catalog reads out `cardmaster` = `ent20 'cardgamemaster'`, but nothing is there (confirmed by F11 screenshot — empty corridor, and by log: `model=-1`). It comes through the PRE-EXISTING cat-3 INTERACTIVE_OBJECT path (sentinel -300-j), NOT the v0.17.8.6 line change. It is **debug leftover**: sits among `synkun`/`dammy`/`dammy02` (classic FF8 dummy entities), its init writes point at `testbl2` (field 91) and `testbl14` (field 100) — *test battle* fields; real interactables never reference those. It only surfaced because LATE-RESOLVE/ResolveStructPositions read a position (607,334) from its entity struct at others-idx 18 — note only 8 runtime entities are active, so this may be a STALE beyond-window struct read (alt root-cause hypothesis worth checking).

**Plan (Aaron approved both, 2026-05-26):**
  (1b) **Catalog double-injection + Directory-suppression fix — in v0.17.8.7 (`field_nav_catalog.inl`).** Found via the name-filter BAT on bghall_1: pathway-sign Lines (now LINE_INTERACTIVE via the v0.17.8.6 extDisp rule) appeared as BOTH "Event" and "Interaction" (flickering), and the Directory (igyous1) vanished. ONE root cause: the legacy "Event" block in `RefreshCatalog` skips campan/event/screenbound/unknown, leaving LINE_INTERACTIVE as the only type it emitted — and the "Interaction" block below ALSO emits LINE_INTERACTIVE with the SAME `-200-t` sentinel, so every interactive Line was injected twice (Event=ENT_OBJECT + Interaction=ENT_INTERACTION). The bogus ENT_OBJECT "Event" entry then tripped the JSM-injection block's `alreadyInCatalog` (type==ENT_OBJECT) test, suppressing the real Directory (also ENT_OBJECT). FIX: the "Event" block now also skips LINE_INTERACTIVE → it emits nothing (its UNKNOWN-only purpose was already removed in v0.12.12) → interactive Lines surface once ("Interaction N") and the Directory returns. NOTE: this was a PRE-EXISTING latent bug (since the Interaction block was added in v0.12.24); v0.17.8.6 only exposed it by classifying more Lines as INTERACTIVE. Stale comment in the Interaction block still says sentinel `-600-t` but the code uses `-200-t` — left as-is, harmless.
  (1) **Static debug-leftover filter (the "filter") — in v0.17.8.7 (name-based, after init-var attempt failed BAT).** FIRST ATTEMPT (init-var testbl filter) FAILED: it checked `s_initVarMaps[e].writes[].value` via `GetFieldNameById` for a `testbl*` name. Two failure modes found in BAT: (i) on **bghall_1** the phantom `cardgamemaster` (ent28) is promoted by the **Director post-pass** and has `initVars=0` — nothing to match; (ii) for entities that DO write testbl values (e.g. `l2` writes testbl8), scan-side `GetFieldNameById` did not return a `testbl*` string (the nav-side INITVARS-SUMMARY resolves names via a DIFFERENT path; both read the same `s_initVarMaps` values via `EnumerateInitVars`, so the mismatch is purely in name resolution — GetFieldNameById likely returns display names). FIX (current): `EntityIsDebugLeftover(e, sym)` in `field_archive_jsm_scan.inl` (forward-decl in state.inl) returns true when SYM name starts with `cardgamemaster` (reliable; the cardgamemaster/2/3 entities are invisible debug scaffolding that reference test fields and do nothing — real card games use visible CC-group NPCs + CARDGAME opcode), OR (secondary/bonus) init-vars ref a testbl field. Guard in ALL THREE promotion paths (main-scan #1, paired #2, Director). Mirrors the existing `camera`/party name filters. BAT CONFIRMED working (Card Player gone). NOTE the deeper issue exposed: the **Director over-promotes** (on bghall_1 it promoted 14 cat-3 entities incl. seito*/lights/lines/cardgamemaster); only positioned ones surface; the runtime layer (2) is the general answer.
  (2) **Runtime confirmation + DISK persistence (Aaron wants persist-across-restarts) — NEXT, larger piece.** The six dialog hooks call new `FieldNavigation::NoteRuntimeDialogEntity(entityPtr)`; resolve via `s_capturedLines[].entityAddr` (line) or runtime others/bg array (bg/other); record stable identity (fieldName + lineOrder/entityIndex) to a DISK file (per-field; survives restarts; becomes a shippable known-objects DB). RefreshCatalog re-applies each build. Purposes: (a) catch interactive objects the static extDisp proxy misses, (b) confirm/label static guesses, (c) DEMOTE static phantoms the player reaches that never fire dialog (the general catch-all for non-debug story-dormant NPCs). Files: field_dialog_opcodes.inl, field_navigation.h, field_navigation.cpp, field_nav_catalog.inl. Needs the codebase's file-I/O pattern + the entityPtr->identity resolution worked out. Intentionally NOT bundled with (1) so the filter BATs in isolation.
  (3) **Alt/also (not yet done):** consider gating ResolveStructPositions against the live `*pFieldStateOtherCount` instead of the hardcoded `>= 31`, so beyond-active-window stale struct positions don't surface phantoms generally. Verify it doesn't regress legit beyond-window save/draw points (e.g. Fire Cavern drpoint) first.

### Bug #4 — party member announced as NPC — CLOSED (pushed v0.17.8.3); v0.17.8.4 camera entry — CLOSED (pushed cb20fd88)

Bug #4: name-based party filter in `field_nav_catalog.inl` (`IsPartyCharacterSym()`), skips no-talk/push entities that are following party members (model 0-9, thru>0) OR party-character-named (any model/thru). BAT-confirmed on bgryo2_1 (all six party entities filtered incl. model-11 selphie). v0.17.8.4: name-scoped `camera` guard in `field_archive_jsm_director.inl` promotion loop; the bogus 'Camera' catalog entry is gone, Director still promotes real interactables. Both pushed.

### NEW bugs found in the first Laguna dream (gwgrass1) — separate chapters

7. **Laguna dream field nav fully broken.** Player entity not detected: log shows `player=ent-1` and every auto-drive attempt logs `[drive] REFUSED ... player_pos_known=0 player_entityIdx=-1`. The `setpc==0` player-detection heuristic in RefreshCatalog/Update fails in the Laguna dream (no entity has setpc==0, or the dream player uses a different marker). This breaks F9 navigation entirely in Laguna sequences. Needs its own diagnostic (dump setpc for all entities on gwgrass1).
8. **Laguna dream battle announces the real party.** Battle TTS says Squall/Zell/Selphie instead of Laguna/Kiros/Ward. The savemap formation still holds the real party char IDs during the dream (gwgrass1 formation logged as [5,0,1,255] = the real party). Battle-side fix — the dream party is swapped in via a different mechanism than the savemap formation array. Separate chapter.
9. **B-Garden Hallway 5 (`bgroad_5`, field 228) — exit to Hall 4 missing.** Reported by Aaron 2026-05-26 (the hall entered right after leaving the dorms). From the load: INF has only ONE active gateway — `gw[0] center=(-781,7) destId=174 'fhdeck4'`; bgroad_5 has lines=2 (idx=1 center=(-289,-4); idx=257 center=(3765,10)). Hall 4 = field 168 `feclock1` (cf. bghall_1 INF gw[3]), which is NOT among bgroad_5's gateways, so the Hall 4 exit must arrive via a JSM MAP_EXIT line that isn't surfacing (likely unresolved dest / no position) rather than an INF gateway. Also note CA projection is degenerate here (`d2len=0.000` -> identity defaults; NAV-PROJ-INIT WARNING) — same family as the old v0.17.7.6.x bgroad_5 calibration issue, worth checking it isn't related. Needs a focused bgroad_5 load capture (scan results + catalog) to confirm which line is the Hall 4 exit and why it's dropped.
10. **B-Garden Hall 6 (`bghall_3`, field 170) — NPC Xu labeled Interactive Object.** Reported by Aaron 2026-05-26. Likely the `shu` SYM entity (FF8 romanizes Xu as "Shu"; bghall_1 SYM also carries `grp[12]='shu'`). Root cause is almost certainly Director over-promotion: bghall_3's Director (`cornerlight`) promotes ~10 cat-3 targets to INTERACTIVE_OBJECT, and a runtime NPC (Xu/shu) is getting swept up. This is the SAME Director-over-promotion failure mode flagged for the runtime-confirmation layer (item (2) in the v0.17.8.7 chapter). Fix direction: the Director should not promote entities that classify as NPC (SETMODEL + TALKON/TALKRADIUS), or the runtime dialog-confirmation layer demotes/relabels them. Needs a bghall_3 scan + Director capture to confirm which promoted entity is Xu.

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
