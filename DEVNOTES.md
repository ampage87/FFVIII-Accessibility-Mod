**Purpose & context**

Aaron is the sole developer of the FF8 Accessibility Mod — a `dinput8.dll` injection for Final Fantasy VIII Steam 2013 (App ID 39150, FF8_EN.exe + FFNx v1.23.x) that makes the game playable for blind players via Windows SAPI TTS, navigation assistance, and entity catalogs. Aaron is blind, uses NVDA, and is also the primary tester.

**Project root:** `C:/Users/ampag/OneDrive/Documents/FFVIII-Accessibility-Mod/FF8_OriginalPC_mod/`
**GitHub:** `ampage87/FFVIII-Accessibility-Mod`. Aaron pushes via `Utilities/push_to_github.ps1`; **Claude never pushes**; diagnostic builds stay LOCAL.

Per-chapter history lives in `CHANGELOG.md` (authoritative, one entry per version) and `DEVNOTES_HISTORY.md` (long-form narratives). This file is the live state + backlog + rules only; it stays under 10 KB.

---

## Where we are at session open

**★ LATEST (v0.18.3.259 BAT PASSED, Aaron: "Much better!" — PUSH CANDIDATE. B-Garden→Balamb 11.3km/23s first attempt; inside the 1200u vehicle final circle: 53 ARC/6 FINAL/0 PIVOT/0 REVERSE — entry-aim orbit GONE (#68 comment posted). Engine vehicleId=33 first-tick again; last wedge burst 3km out. #79 CLOSED at .258. .254 = pushed HEAD c6e758d; .255–.259 local).** Remaining #68 vehicle item: mid-route terrain scrapes on pass legs (6 bursts, dist 3000–3900 corridor) — route clearance at car scale, likely offline work. Banked: 0x020409E0 = TRUE vehicle id (populated by catalog time; entry-tick reads 0); 0x02040A5E = footstep anim counter; 0x02040A68 = module-reload state.

**Previous (#77 closed at .243, #78 closed at .249):** field insert family fully mapped, all resolved in `FieldExpandRawVars` before `Decode()` — **0x04** numbers (value at `0x1D2B4B0+param*4`), **0x0C/0x0D** names (engine resolvers 0x47E970/0x47EA30 via fn ptr), **0x0E+** deferred shared-text table (ptr **0x01D2B80C** = `u16 count; u16 offsets[]; FF8 strings`). **Process rule:** two wrong theories → dump the bytes.

**★ 2026-07-12 playtest chapter (v0.18.3.238, PUSHED; #71–#77 resolved, #71 open low-priority for the SHOW/HIDE scene-actor follow-up only):** battle-pause auto-drive resume (#72, `field_nav_battlepause.inl`), no-effect watchdog deferral + a3==0x09 filter (#73/#74), engine timer-visible byte **0x01D2B813** gates countdown dismissal (#75), Fire Cavern renumbering (#76). Detail: CHANGELOG `## v0.18.3.236`–`.238` + the closed issues.

**.225→.235 chapter (pushed): #70 FULL GALBADIA LOOP passes all 4 legs + Balamb; then the .229–.235 NPC-catalog fixes** (EXTSCAN, sticky talkability latch, SYM-mistype guard, catalog.inl split). Play-by-play: CHANGELOG `## v0.18.3.211`–`.235`.

**★ .211 routenet topology (CURRENT DATA — `src/world_map_routenet.inl`, 13 nodes/14 edges/1,099 pts):** Galbadia pass edge RETIRED (impassable live); Timber/Dollet route east via junctions `Yaulny Plains` (−31345,−25449) and `Hasberry Plains` (−16452,−39492, outside Dollet's bbox). Hard transit rule: junctions + at-mouth starts + target only; `RouteNetPlan` first, grid A* fallback. Camera RE'd: heading = camYaw + key·512 + bias/2. Tears' Point excluded (vehicle-only). MUST-READ before route work: `offline/ROUTENET.md`, `offline/BAT203_ANALYSIS.md` (stateful find-poly MRU collision gate; ~1u sliver walls need 8u sweeps), `offline/TRIGGER_FIRING_AREAS.md` (`s_entryAims[7]`); tooling in `offline/`. Full detail: CHANGELOG `## v0.18.3.209`–`.211`. Research docs = LEADS ONLY.

**★ wmx.obj FORMAT CRACKED + VALIDATED** → spec in `Plan & Research Documents/WMX_OBJ_FORMAT.md` (READ before world-map work): 835 segs×0x9000, poly=16B (types @[13,14,15]), vert=8B (UP=neg-Y); locator 0x53DC70 + Z-mirror, 15/15 landmarks. Galbadia (incl. Dollet) = one landmass, Balamb across ocean. Walkability = the 200u height-STEP gate (§12), not a ground-type table.

**Older shipped chapters (digest — play-by-play in CHANGELOG/HISTORY/closed issues):** battle diag #62-#64, #54 status, #48 char-select, PR #26 FMV ADs, #65/#66 Switch screens (`menu_tts_switch.inl`), Timber train hijack #56-#60, status chapter #49/#54 + #10/#42/#46/#47 (`menu_tts_status.inl`). **#67 on-foot drive PUSHED** (main v0.18.3.87+; OPEN as all-continent umbrella; robustness = #68 executor / #69 terrain-ID). The #70 navmesh saga (.104-.124) lives in CHANGELOG.md. OPEN: **#61** (decoder spoken-"L", root `FF8TextDecode::Decode`), **#50** Angelo gauges, **#45** junction non-command abilities (blocked). Handedness: RIGHT=yaw+90 CW.

---

## Active backlog → GitHub Issues

Tracked bugs/tasks live as **GitHub issues** on `ampage87/FFVIII-Accessibility-Mod`. Run `github:list_issues` for the live set. Breadcrumbs: **#34/#35** need a pre-SeeD save + `LINEDIAG_ENABLED=1` for exit labels; **#37** = 60/80 KB source-size refactor queue; **#43** = calibrate remaining GF per-level EXP as GFs are obtained; **#45** blocked (no GF teaches a non-command ability yet).

Pre-existing related: #28 Fire Cavern entry trigger; #21 dialog location-names-as-numbers; plus the open battle/menu-TTS set. **File new bugs on GitHub, not here.**

**Known limitation (not a mod bug, no issue):** JAWS intercepts game keys (arrows, Backspace) until the user presses Insert+3 for passthrough. NVDA is unaffected.

---

## Reference: known fieldIds (geometric-trigger destinations)
- **Fire Cavern A** (approach field, world-map terrain trigger): `0x0088`, engine `bdview1`, trigger ≈ (30260, -29221). Two-stage entry: the world-map trigger drops you into this approach field, not the interior.
- **Balamb Town gate** (planner destination, not geometric): `0x006A`, `bcgate_1`, ≈ (12894, -26776).
- **B-Garden Front Gate 5** (push-through gate, Track A): `0x00A3`, engine `bggate_6` (NOT `fepic1`).

---

## Architecture, safety & workflow rules

- Read **`DEVNOTES.md`** and **`NEXT_SESSION_PROMPT.md`** at session start; `DEVNOTES_HISTORY.md` only when tracing past decisions. Update DEVNOTES + NEXT_SESSION at every version bump AND after every BAT.
- **Version bump = ONE place:** `FF8OPC_VERSION` in `src/ff8_accessibility.h`. Every bump pairs with a new top-of-file `CHANGELOG.md` entry; `push_to_github.ps1` refuses if the top `## vX.Y.Z` heading ≠ the macro.
- **Filesystem MCP for all Windows project files** (`filesystem:read_text_file/edit_file/write_file/...`). Bash is a Linux container that can't reach OneDrive — use it only for processing in-context text. `filesystem:edit_file` CORRUPTS on a literal `$` in the replacement (truncates + doubles the file) — use hex `0x24` in source or rewrite with `write_file`. OneDrive sometimes throws a transient EPERM on edit_file rename — retry once.
- **NEVER re-enable the SET3 hook (0x1E)** — any interception hangs the infirmary scene. CI guard in `.github/workflows/safety-checks.yml`.
- **Victory/transition TTS hooks the text renderer, NOT memory** — memory dumps everything at once; the player presses blindly through unannounced screens.
- **F-key handlers gated on `!(GetAsyncKeyState(VK_MENU) & 0x8000)`** (no Alt combos). **F12 is per-session diagnostic ONLY** — search all source for existing `VK_F12` and remove old code before hooking a new one.
- **Source size (v0.16.0 CI guard):** 60 KB soft / 80 KB hard. Split via textual `#include` of `.inl` from the parent `.cpp` (no header guards, no namespaces inside `.inl`; `*_state.inl` statics included first). No `deploy.bat` change needed. Push-script mirrors the guard at Step 7c.
- **Diagnostic gating pattern:** gate behind `#define X 0`, don't delete — keeps the tool available.
- **GitHub commit history is authoritative** for "when did X change". Run `github:list_commits` before quoting push state.
- **Entity-catalog identity (carry-forward):** SYM names are unreliable as identity hints (kanban2 was Xu) — never expose them; classify by JSM behavior signals (`jsmCategory`, `hasSetmodelInit`, `hasDialogReqTarget`, `foundExtDispatch`), not the model filename. NPC/interaction labels are generic ("NPC N" / "Interaction N") only. Announce-time counters need TYPE-based matching for JSM-injected entries (entityIdx ≤ −300), not the legacy `entityIdx ≥ 0` test.
- **Navigation is screen-relative, not world-relative** (v0.17.0): cardinals map to arrow keys; project through `s_camRight/Down` before `atan2`. **F9 auto-drive uses `s_camRight/Down`** (quantized at load); **chase-drive uses `s_driveCam*`** (empirical CALIB) — don't cross the streams. **F9 corridor-level steering is OFF** (v0.17.6.2); funnel waypoints + FF8 wall-sliding are its only steering.
- **EWM and battle-menu TTS are load-bearing.** EWM preserves "first-to-fill acts first, no skipped turns, natural ally/enemy ratio". Pure mechanical splits only unless Aaron explicitly approves a refactor.
- **Arrival detection and empirical-coord capture need the underlying decision VERIFIED, not just signal-presence.** Mid-drive replan must honor the same planner-eligibility gate as the initial Start. When "fixing" a planner decline, don't substitute a different region (the v0.14.95 mistake).
- **One change per BAT cycle.** If a fix doesn't engage, READ THE LOG before iterating; identical BAT outcomes across "fixes" mean the diagnosis is wrong; when stuck on a theory, ask Aaron for one specific observation and act on it (that unlocked the Chapter-4 chase fix). Aaron's domain knowledge is ground truth, but his recipes need empirical verification.
- **Logs:** `ff8_field.log` is large — bash-grep it from stored tool results. `ff8_nav_data.log` logs every player triangle change (`[…] COORD field tri X Y`) regardless of auto-pilot state. F11 screenshots are gold for BAT context. `[MAPJUMP-HOOK]` is the live engine oracle for exit destinations.
- **`deploy.bat` version-extract regex** needs the `/B` anchor (v0.15.10.1). Inline-changelog accretion is dead (retired v0.15.12.0) — `CHANGELOG.md` is canonical.
- Every Claude response starts with `## Claude Says`.
