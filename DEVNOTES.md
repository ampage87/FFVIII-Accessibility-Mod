**Purpose & context**

Aaron is the sole developer of the FF8 Accessibility Mod — a `dinput8.dll` injection for Final Fantasy VIII Steam 2013 (App ID 39150, FF8_EN.exe + FFNx v1.23.x) that makes the game playable for blind players via Windows SAPI TTS, navigation assistance, and entity catalogs. Aaron is blind, uses NVDA, and is also the primary tester.

**Project root:** `C:/Users/ampag/OneDrive/Documents/FFVIII-Accessibility-Mod/FF8_OriginalPC_mod/`
**GitHub:** `ampage87/FFVIII-Accessibility-Mod`. Aaron pushes via `Utilities/push_to_github.ps1`; **Claude never pushes**; diagnostic builds stay LOCAL.

Per-chapter history lives in `CHANGELOG.md` (authoritative, one entry per version) and `DEVNOTES_HISTORY.md` (long-form narratives). This file is the live state + backlog + rules only; it stays under 10 KB.

---

## Where we are at session open

**★ CURRENT (v0.18.3.225 LOCAL — PUSH-READY) — #72: FULL GALBADIA LOOP GG→Timber→Dollet→Timber→GG PASSES (all 4 legs), plus Balamb.** Full read: NEXT_SESSION_PROMPT.md.

_.225 = release prep (no gameplay change):_ (1) split the two files that had grown past the 80 KB CI hard-fail during the unpushed world-map work — `world_map_drive.inl` (156 KB) → `world_map_drive_helpers.inl` + `world_map_drive.inl` + `world_map_drive_exec.inl` (the `UpdateAutoDrive` tail, mid-function textual include); `world_map_planner.inl` (96 KB) → `world_map_planner.inl` + `world_map_planner2.inl`. **Byte-identical splits** (statement/block-boundary textual `#include`, forward-decl order preserved); world_map.cpp include order updated; every `src/*.{cpp,inl}` now under 80 KB. Re-verified live on the split build: BG→Fire Cavern arrived with the exact same escape trace. (2) `AUTOTEST_CMD_ENABLED` gated to 0 (test channel off in shipped builds; flip to 1 + rebuild to drive the game). Push-readiness confirmed: version==CHANGELOG top heading (0.18.3.225), SET3 marker present, no oversized files. Aaron runs `Utilities/push_to_github.ps1`.

_.223/.224 ROUTE-AROUND ESCAPE (fixes Aaron's Timber→Dollet failure):_ Aaron's GG→Timber→Dollet run: leg 1 fine, Timber→Dollet failed. Character spawns N of Timber but the route to Dollet leaves SW (via Yaulny); .222 steered straight at the destination (Dollet, S) → crossed Timber (re-entry) → cleared on the wrong (N) side → routenet declined ("no transit-legal path", then "off-network 15000u+"). **.223:** the escape now routes AROUND the area's padded box toward the first ON-ROUTE waypoint outside it (not the destination), via `EscapeSteerAround` (inside→exit nearest edge; outside+clear→target; outside+blocked→best reachable box corner, Liang–Barsky `SegCrossesBox`); holds until outside the box AND with a clear line to that waypoint. **.224:** on clear, resume at the on-route escape-target index (`s_driveEscapeTgtIdx`), never a waypoint BEHIND the character — .222/.223 re-snapped to the nearest overall waypoint, which for Timber's route (it hugs the area before curving away) sat back toward Timber → pivoted back in. **VERIFIED LIVE: full loop GG→Timber→Dollet→Timber→GG, ALL 4 legs arrived** (Timber→Dollet routed N-then-W around Timber, via Yaulny→Hasberry, arrived Dollet 0x013D). Field→world-map exits between legs done via the catalog: **DOWN-walk out** (Timber walks straight out) or cycle `=`/`-` to "Exit to World Map" + `\` (needs a brief move first to calibrate the field camera). **Balamb regression PASS** (BG→Fire Cavern: rounds BG's NE corner, resumes escTgtIdx=16). GitHub HEAD v0.18.3.104 (~73 unpushed; Claude never pushes).

_Escape mechanism (`world_map_drive.inl` `ArmFiringAreaEscape`, called from StartAutoDrive + the .220 deferred resume; the fix for spawning ON a location's own re-entry trigger): EXCURSION SKIP past leading in-area world-path waypoints (`EA_PAD`=192); STEER-AROUND the padded box (`EA_STEER_ARM`=768) toward the first on-route waypoint outside it; resume at that on-route index. Target's own area exempt. Heritage: .221 (destination-steer, inside-pad only) → .222 (wide 768 berth, straight-at-destination; failed Timber→Dollet because the destination was across the area) → .223/.224 (route-around + on-route resume, current)._

_Automation loop (LOCAL test infra, gated OFF at .225):_ live log tailing via `_fsopen(_SH_DENYWR)` (.212); `src/autotest_cmd.inl` polls `Logs/autotest_cmd.txt` for `KEY`/`WAIT`/`SHOT` commands and injects via the ChaseKeyboard DIK overlay + VK SendInput (.213–.218), acking `[AUTOTEST]` in ff8_mod.log. Reaches FF8's DirectInput buffer AND the mod's own GetAsyncKeyState hotkeys (arrows don't arrive via plain OS SendInput; JAWS must be EXITED). Flip `AUTOTEST_CMD_ENABLED` to 1 + rebuild to use. Details in CHANGELOG `## v0.18.3.212`–`.218`, `.225`.

_Nav bugs the loop found+fixed this session (all VERIFIED live; detail in CHANGELOG):_ **.216** stale watchdog clocks across battle-resume → `s_driveWatchdogGen`. **.217** `GetWorldMapPosition_Active` foot-motion override (stale vehicle byte returned wrong-continent `car_pos`). **.219** routenet 4-plan cap counted battle resumes. **.220** resume replan deferred 20 ticks (stale pre-pause position). **.221–.224** the FIRING-AREA ESCAPE (above). **.225** the 80 KB file splits.

_Aaron's test cases — ALL PASS on .225 (Slot2Save1 = outside G-Garden; Slot1 Block 3 = "Balamb - Alcauld Plains", outside B-Garden):_ full Galbadia loop **GG→Timber→Dollet→Timber→GG** (all 4 legs, field↔world-map exits between them), and Balamb **BG→Fire Cavern / BG→Balamb Town**. Field→world-map exit in the loop: DOWN-walk out (Timber) or catalog `=`/`-` to "Exit to World Map" + `\` (needs a brief move first to calibrate the field camera). Battles auto-escaped with `KEY Z+C 25000`.

**★ .211 routenet topology (CURRENT DATA — `src/world_map_routenet.inl`, 13 nodes/14 edges/1,099 pts; full detail in CHANGELOG `## v0.18.3.211` + `offline/ROUTENET.md`):** Galbadia pass edge RETIRED (impassable live). Timber/Dollet traffic routes the long way east via junctions `Yaulny Plains` (−31345,−25449) and `Hasberry Plains` (−16452,−39492, outside Dollet's bbox → Timber traffic skirts Dollet). Validated links: `GG East↔Yaulny`, `Yaulny↔Hasberry`, `Hasberry↔Dollet`, `Yaulny↔Timber`, and the Balamb-side `Balamb Garden↔Fire Cavern` / `Balamb Town↔Fire Cavern`. Offline matrix 20/20; live-confirmed this session for the whole Galbadia trio. .210 heritage: hard transit rule (junctions + at-mouth starts + target only). .209: the route NETWORK + `RouteNetPlan`-first + grid A* fallback + `EngineSimC(hatch="away")` replica. Must-read offline docs: **`offline/ROUTENET.md`**, **`offline/BAT203_ANALYSIS.md`** (engine collision gate partly STATEFUL — find-poly 8-entry MRU cache; ~1u sliver walls need 8u sweeps; G1–G4 suite), **`offline/TRIGGER_FIRING_AREAS.md`** (triggers fully decoded, `s_entryAims[7]`; the Balamb-gate fix lives here). Camera RE'd (.201/.202): heading = camYaw + key·512 + bias/2; executor writes camYaw+zero-vel+UP. Tears' Point excluded (vehicle-only). Offline tooling in `offline/` (extract_wmx.py, ff8_walkmesh.py, nav_sim.py, gen_routenet.py). Research docs = LEADS ONLY. GitHub HEAD v0.18.3.104 (~58 unpushed; Claude never pushes).

**★ wmx.obj FORMAT CRACKED + VALIDATED** → spec in `Plan & Research Documents/WMX_OBJ_FORMAT.md` (READ before world-map work): 835 segs×0x9000, poly=16B (types @[13,14,15]), vert=8B (UP=neg-Y); locator 0x53DC70 + Z-mirror, 15/15 landmarks. Galbadia (incl. Dollet) = one landmass, Balamb across ocean. Walkability = the 200u height-STEP gate (§12), not a ground-type table.

**Older shipped chapters (digest — play-by-play in CHANGELOG/HISTORY/closed issues):** battle diag #62-#64, #54 status, #48 char-select, PR #26 FMV ADs, #65/#66 Switch screens (`menu_tts_switch.inl`), Timber train hijack #56-#60, status chapter #49/#54 + #10/#42/#46/#47 (`menu_tts_status.inl`; `ST_LIMIT_DIAG=false` maps Irvine's Shot limit when he joins). **#67 on-foot drive PUSHED** (main v0.18.3.87; road-pref routing, yaw-based 8-way executor; OPEN as all-continent umbrella; robustness = #68 executor / #69 terrain-ID, .88-.95 pushed `c591803b`, .90 height-step guard core). The #70 navmesh saga (.104-.124) lives in CHANGELOG.md. OPEN: **#61** (decoder spoken-"L", root `FF8TextDecode::Decode`), **#50** Angelo gauges, **#45** junction non-command abilities (blocked). Handedness: RIGHT=yaw+90 CW.

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
