**Purpose & context**

Aaron is the sole developer of the FF8 Accessibility Mod — a `dinput8.dll` injection for Final Fantasy VIII Steam 2013 (App ID 39150, FF8_EN.exe + FFNx v1.23.x) that makes the game playable for blind players via Windows SAPI TTS, navigation assistance, and entity catalogs. Aaron is blind and uses NVDA as his screen reader, and is also the primary tester.

**Project root:** `C:/Users/ampag/OneDrive/Documents/FFVIII-Accessibility-Mod/FF8_OriginalPC_mod/`

GitHub: `ampage87/FFVIII-Accessibility-Mod`. **GitHub HEAD = v0.17.7.0** (commit `8b9299c2`, pushed 2026-05-19 02:48:27 UTC).

v0.17.7.0 BAT'd clean on bghall_1 and bggate_6: build clean, both fields loaded without exception, catalog behavior matches v0.17.6.2 per Aaron's confirmation. The mechanical file split is verified end-to-end; the helper call chain (DumpEntityDiagOnce, DumpBgDiagOnce, DumpPartyStateOnce, DumpCoordDiagOnce, ResolveLatePositions, MatchSet3LateCaptures, ResolveStructPositions) is wired correctly and produces the same catalog output as pre-split.

v0.17.6.2 BAT was also a clean win. All four bghall_1 cross-field exit drives reach `Arrived.` with diagonal-kb wall-sliding through corridor turns (7–14 sec each, matches manual-nav travel times). The v0.17.6.x chapter (re-base F9 path-finding auto-drive onto manual nav's BAT-proven primitives, disable corridor-level steering, lean on FF8's built-in wall-sliding) is closed. Full per-drive timings, the [drive-vec] diagnostic that pinpointed the corridor-steering wedge, and all v0.17.x narrative now live in `DEVNOTES_HISTORY.md`.

v0.17.6.2 also exposed two within-field friction points in B-Garden (fepic1 push-through gate, generic entity-catalog names), prompting Track B (entity catalog overhaul). v0.17.7.0 (now pushed) was the prerequisite file split: `field_nav_catalog.inl` went 75.77 KB → 53.82 KB with two new helper files (`field_nav_catalog_diag.inl`, `field_nav_catalog_lateres.inl`). v0.17.7.1—.4 land the substantive Track B fixes.

---

## Where we are at session open

**v0.17.7.6.2 implemented locally, awaiting BAT.** `FF8OPC_VERSION` = `0.17.7.6.2`. CHANGELOG top heading matches.

v0.17.7.6.1 BAT'd partial on bgroad_5:
- The 2-sample threshold worked: `[NAV-CAL]` fired after 2 UP samples (vs. 3 in v0.17.7.6).
- The two-tier AD gate worked in principle: observer code path was enabled during AD on degenerate-CA + pending-cal fields.
- BUT AD pushed Aaron into a wall (`moveDist=0` for entire drive), no samples accumulated during AD, calibration didn't fire until Aaron walked manually with UP arrow.
- AD failed twice (gave up after recovery), then Aaron walked manually, calibration fired, but by then AD attempts had thrashed and Aaron was frustrated.

The v0.17.7.6.1 catch-22 mutated: AD pushes into wall -> no movement -> observer's 50-unit movement gate filters all samples -> calibration can't fire -> AD continues into wall -> AD gives up.

### v0.17.7.6.2 fix (in tree, awaiting BAT)

**Option A from the v0.17.7.6.1 BAT discussion: block AD until calibrated.**

Two changes:

1. **`field_nav_handlekeys.inl`** -- new refusal case in the AD-start chain. When `s_camAxesSource == "identity"` and `!s_camAxesEmpiricalApplied`, AD does not start. TTS announces: *"Camera not yet calibrated. Press an arrow key briefly to calibrate, then try again."* `[drive] REFUSED` line logged.

2. **`field_nav_observe.inl`** -- `ObsApplyEmpirical` speaks *"Camera calibrated."* after the existing `[NAV-CAL]` log line. Fires once per field load (lock flag prevents repeats).

v0.17.7.6.1's other changes are preserved: two-tier AD gate in observer (still allows sampling during AD on degenerate-CA in case some future field has wall-free wrong-direction injection), `EMPIRICAL_MIN_SAMPLES = 2`.

### Regression safety

The AD-refusal gate fires only on fields where `s_camAxesSource == "identity"` (degenerate-CA fallback). All CA-valid fields (source `"ca-quantized"`) behave identically to v0.17.7.6.1 = identical to v0.17.7.5.5. After calibration applies on a degenerate field, source becomes `"empirical-corrected"` and the gate stops firing for that field load.

The TTS announcement at `[NAV-CAL]` is purely additive (one new `ScreenReader::Speak` call). No other logic changes.

### Expected BAT outcome

**Primary (bgroad_5):**
- Aaron enters bgroad_5. `[NAV-PROJ-INIT] source=identity` logged.
- Aaron presses backslash to start AD.
- TTS: *"Camera not yet calibrated. Press an arrow key briefly to calibrate, then try again."*
- `[drive] REFUSED` logged. AD does not start.
- Aaron presses UP arrow for ~3 seconds.
- Observer collects 2 UP samples; `[NAV-CAL]` fires.
- TTS: *"Camera calibrated."*
- Aaron retries AD (backslash again).
- AD starts with corrected axes; drives correctly to dormitory.

**Regression sanity:**
- bghall_1, bg2f_2, other CA-valid fields: no behavioral change. AD starts immediately as before.
- bgroad_5 entered, Aaron walks before triggering AD: same as above but the refusal message never fires.

### Open question for later (deferred)

If the walk-then-retry UX becomes annoying after Aaron lives with it, v0.17.7.6.3 could add a one-shot synthetic look-around at field load on degenerate-CA fields. Defer unless Aaron asks.

---

**Open question (deferred, not blocking)**: bgryo1_1 (Dormitory Double 1)
resolver picked addr 0xE7 = 231 (Hallway 8) for its single SCREEN_BOUND line,
but Aaron's actual return transition in the v0.17.7.5.3 BAT went to bgroad_5
(Hallway 5, field 228). INF gateway log showed destId=174 (Hall 10) -- also
doesn't match. Either the dormitory has multiple SCREEN_BOUND exits and only
one was captured at that BAT point, or the addr-as-literal pattern doesn't
hold for this field. Investigate if Aaron reports dormitory exit labeling
issues later; otherwise leave it.

**Friction points from v0.17.6.2 BAT (driving Track B):** unchanged.

**Aaron's taxonomy (2026-05-18):** Exit (MAPJUMP, destFieldId > 0), Interaction (TALKRADIUS/TALKON), Event (no talk setup, but MES/BATTLE/SHOW/HIDE/MOVE/REQ), NPC / Save / Draw / Shop / Card. Exclusion rule: drop entities with NO talkradius AND outside the walkmesh.

**Track B sequencing:**
1. ✅ **v0.17.7.0**: file split. Pushed `8b9299c2`.
2. ✅ **v0.17.7.1 / .1.1 / .1.2**: walkmesh rule + hasExtDispatch + runtime PSHM + INF fallback.
3. ✅ **v0.17.7.2**: gated dumps + diagnostics.
4. ✅ **v0.17.7.3**: all-method POPM_W capture.
5. ✅ **v0.17.7.4**: runtime MAPJUMP-family dispatch table hooks.
6. ✅ **v0.17.7.5**: static resolver + VM stack/RESULT validation.
7. ✅ **v0.17.7.5.1-.5.5**: catalog overhaul cumulative batch. Pushed `6abcb8f`.
8. ✅ **v0.17.7.6**: empirical calibration math (stepping stone, BAT'd partial).
9. ✅ **v0.17.7.6.1**: two-tier AD gate + 2-sample threshold + log-line fix (stepping stone, BAT'd partial -- AD-into-wall failure revealed).
10. ✅ **v0.17.7.6.2**: block AD on uncalibrated degenerate-CA + TTS instruction + "Camera calibrated" confirmation. BAT'd clean -- Aaron confirmed bgroad_5 AD works after one calibration walk.
11. **v0.17.7.7**: SETLINE-position promotion + NPC `ResolveFriendlyName`.
12. **v0.17.7.8**: Shop/Card Game → NPC announce-layer collapse.
13. **v0.17.7.9** (optional): SYM override layer for residual leaks.

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
