**Purpose & context**

Aaron is the sole developer of the FF8 Accessibility Mod — a `dinput8.dll` injection for Final Fantasy VIII Steam 2013 (App ID 39150, FF8_EN.exe + FFNx v1.23.x) that makes the game playable for blind players via Windows SAPI TTS, navigation assistance, and entity catalogs. Aaron is blind and uses NVDA as his screen reader, and is also the primary tester.

**Project root:** `C:/Users/ampag/OneDrive/Documents/FFVIII-Accessibility-Mod/FF8_OriginalPC_mod/`

GitHub: `ampage87/FFVIII-Accessibility-Mod`. **GitHub HEAD = v0.17.7.6.2** (commit `d3321665`, pushed 2026-05-20 05:47 UTC, squashed from local v0.17.7.6 / .6.1 / .6.2 onto parent `6abcb8f`).

The v0.17.7.6.x chapter closed the bgroad_5 hallway calibration failure: empirical correction from NAV-OBSERVE measurements, two-tier AD gate, 2-sample consensus threshold, AD-refusal-with-TTS on uncalibrated degenerate-CA fields, "Camera calibrated" confirmation. Full narrative in `DEVNOTES_HISTORY.md`. The .6.x chapter and the larger v0.17.7.x catalog overhaul track are both feature-complete and pushed.

---

## Where we are at session open

**v0.17.8.0 BAT'd CLEAN — Aaron confirmed both fixes work.** Battle test confirmed GF damage announcements during GF-HP-SUB window (bug #6 fixed) and absence of GF breakpoint TTS announcement (bug #5 fixed). Ready to push via `Utilities/push_to_github.ps1`.

Fire Cavern playthrough bug list (Aaron's 2026-05-18 report) progress:
1. Quistis' FMV in the Infirmary fired prematurely — deferred
2. ~~Manual field navigation direction lag and inaccurate direction guidance~~ — ✅ closed by v0.17.7.6.2
3. Garbage announced by TTS following completion of a tutorial scene — **next single build (v0.17.8.1)**
4. Party member announced as NPC in catalog when party consists of just two members — deferred
5. ~~Breakpoint on display timer announced when GF sequence starts~~ — ✅ closed by v0.17.8.0
6. ~~Damage not announced when a character is summoning and the GF takes the damage in place of the character~~ — ✅ closed by v0.17.8.0

### Up next: v0.17.8.1 — Bug #3 (tutorial TTS garbage)

From the v0.16.5.2 BAT triage note: `[POLL] win[0] Speaking: ",e 3in*retone3 e~HP~B:All08E%~!/..."` after `[TUTO]` mode 10→1. Two candidate fixes:
- **Preferred**: reject `[…]` tokens / unprintable garbage in the POLL path (filter before SAPI). More general, catches similar future cases.
- **Alternative**: suppress POLL win[0] for ~500 ms after the `[TUTO]` mode 10→1 transition.

Needs investigation into the POLL pipeline in the dialog system to find where to insert the filter. Likely in `dialog_inject.cpp` or similar. The 2026-05-18 Fire Cavern playthrough log should have a clean reproduction example to verify the fix against.

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
