**Purpose & context**

Aaron is the sole developer of the FF8 Accessibility Mod — a `dinput8.dll` injection for Final Fantasy VIII Steam 2013 (App ID 39150, FF8_EN.exe + FFNx v1.23.x) that makes the game playable for blind players via Windows SAPI TTS, navigation assistance, and entity catalogs. Aaron is blind and uses NVDA as his screen reader, and is also the primary tester.

**Project root:** `C:/Users/ampag/OneDrive/Documents/FFVIII-Accessibility-Mod/FF8_OriginalPC_mod/`

GitHub: `ampage87/FFVIII-Accessibility-Mod`. **GitHub HEAD = v0.17.6.2** (commit `a42d4aeb`, pushed 2026-05-19 01:01:26 UTC). **Local tree = v0.17.7.0** (file split, awaiting BAT before push).

v0.17.6.2 BAT was a clean win. All four bghall_1 cross-field exit drives reach `Arrived.` with diagonal-kb wall-sliding through corridor turns (7–14 sec each, matches manual-nav travel times). The v0.17.6.x chapter (re-base F9 path-finding auto-drive onto manual nav's BAT-proven primitives, disable corridor-level steering, lean on FF8's built-in wall-sliding) is closed. Full per-drive timings, the [drive-vec] diagnostic that pinpointed the corridor-steering wedge, and all v0.17.x narrative now live in `DEVNOTES_HISTORY.md`.

v0.17.6.2 also exposed two within-field friction points in B-Garden (fepic1 push-through gate, generic entity-catalog names), prompting Track B (entity catalog overhaul). v0.17.7.0 is the prerequisite file split: `field_nav_catalog.inl` went 75.77 KB → 53.82 KB with two new helper files (`field_nav_catalog_diag.inl`, `field_nav_catalog_lateres.inl`). No functional change. Awaiting BAT to confirm catalog behavior unchanged; then v0.17.7.1—.4 land the substantive Track B fixes.

---

## Where we are at session open

**Active work: v0.17.7.x Track B — entity catalog overhaul.** v0.17.7.0 (file split) shipped locally; awaiting BAT before push. Investigation phase complete — see Track B findings below and the v0.17.7.1+ plan in `NEXT_SESSION_PROMPT.md`. Aaron's 2026-05-18 clarification expanded Track B from "better labels" to four structural issues; the substantive fixes land across v0.17.7.1 through v0.17.7.4.

**Friction points from v0.17.6.2 BAT (driving Track B):**

1. **Push-through gate at fepic1 (Front Gate 5, fieldId=0x00A3).** A scripted gate spans the corridor; walkmesh treats it as a wall so A* can't path through. Multiple within-field drives oscillated and one walked the player into the wrong exit. Aaron's diagnosis: PUSHRADIUS or SETLINE trigger fires scripted JUMP/MOVA teleporting the player south. → Track A, deferred until Track B done.
2. **Entity catalog issues (four-pronged, scope per Aaron 2026-05-18).** Catalog should surface only useful entities: save points, draw points, interactive objects (signs, switches), NPCs, event triggers, exits. Currently:
   - **Misclassification.** fepic1's exits surface as `Interaction 1/2/3` instead of `Exit to <field>`. The v0.12.24 block demotes ALL screen-bound trigger lines to Interaction on any field with an Interactive Object — was meant for dormitories (where SETLINE serves dual purpose: walk through to exit OR press X to interact) but mistakenly demotes fepic1's genuine exits because the field also has interactive objects.
   - **Missing entities.** Signs and other examined-only interactive objects aren't catalogued. JSM scanner promotes background entities to `JSM_ENT_INTERACTIVE_OBJECT` only when `hasPosition || hasPshmCoords`; SETLINE-positioned entities (signs in Cafeteria/dormitories) extract `setlineX1/Y1` into local variables but never write them back to `info.posX/Y`, so the position gate fails.
   - **Light regression.** `Light 1 of 1` is appearing again in fepic1. `ENTITY_SKIP_NAMES` (213 entries) is consulted by `IsBgControllerName`, which only fires in the BG-entity path. Light entities entering via JSM `JSM_ENT_INTERACTIVE_OBJECT` promotion (with own SET3 + 0x1C extended dispatch) bypass the filter entirely — the v0.08.04 `!strstr(symName, "light")` guard sits only on paired-entity inheritance, not the primary promotion gate.
   - **Generic naming + SYM leaks.** Runtime NPC classification sets `entName = "NPC"` literal and never calls `ResolveFriendlyName`. Result: every named NPC reads as `NPC 1 of N`. Where SYM does flow through, missing display map entries (e.g. `son`) leak as `Son 1 of 1`.

**Aaron's clarified taxonomy (2026-05-18):**
- **Exit**: trigger line with MAPJUMP target (destFieldId > 0). Label: `"Exit to <field name>"`. Unresolved destination falls back to bare `"Exit"`.
- **Interaction**: requires confirm. Has TALKRADIUS or TALKON in its script.
- **Event**: crosses the line to fire. No TALKRADIUS/TALKON. MES/BATTLE/SHOW/HIDE/MOVE/REQ in script.
- **NPC / Save Point / Draw Point**: as today. Shop and Card Game internal types fold into NPC at the announce layer (shopkeepers and card masters are NPCs).
- **Exclusion rule**: drop entities with NO talkradius AND outside the walkmesh. Either condition alone keeps the entity.

**Track B sequencing (full plan in NEXT_SESSION_PROMPT.md):**
1. ✅ **v0.17.7.0**: file split (`field_nav_catalog.inl` 75.77 KB → 53.82 KB), no functional change. Awaiting BAT.
2. **v0.17.7.1**: walkmesh exclusion rule + per-line exit/interaction/event discriminator using TALKRADIUS/TALKON detection. Kills lights and fixes fepic1's misclassification.
3. **v0.17.7.2**: SETLINE-position promotion (signs reach catalog) + runtime NPC `ResolveFriendlyName` routing (148 entries in display map unlocked).
4. **v0.17.7.3**: Shop/Card Game → NPC announce-layer collapse.
5. **v0.17.7.4** (optional): SYM override layer for residual leaks like `Son`.

---

## Active backlog (priority order)

### v0.17.7.x track parking

- **Track A: push-through gate routing** at fepic1 and any other scripted-gate field. Three candidate fixes; strategy decision is the first step when Aaron returns to it.

### v0.16.5.2 BAT triage carry-over (5 bugs, deferred)

1. **FMV STOP/PLAY race** — Quistis infirmary AD fired 22 s before engine resumed FMV playback. Engine STOP/PLAY visible in `ff8_mod.log`; fix is to pause/resume the AD cue timer on those transitions instead of free-running on wall clock.
2. **POLL tutorial garble** — `[POLL] win[0] Speaking: ",e 3in*retone3 e~HP~B:All08E%~!/..."` after `[TUTO]` mode 10→1. Reject `[…]` tokens / unprintable garbage in POLL path, or suppress POLL win[0] for ~500 ms on tutorial-end.
3. **Party member announced as NPC** in 2-member parties on bdin2/bdin3. `party-filter` works on later fields but not earlier ones — likely keys on per-field model index instead of checking formation[] directly by character-ID.
4. **GF-BP diagnostic spam** at every GF cast (350+ `[GF-BP] #50 ACCESS` lines in a fraction of a second). Leftover diagnostic; gate behind `#define GF_BP_AUTOARM_DIAG 0`.
5. **Missing damage announce when GF substitutes for char** (GF-HP-SUB enabled, damage to Shiva's HP not tracked). HP-TRACK doesn't watch the GF HP address while GF-HP-SUB is active; subscribe it during the HP-SUB window.

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
