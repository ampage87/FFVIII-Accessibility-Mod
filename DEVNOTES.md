**Purpose & context**

Aaron is the sole developer of the FF8 Accessibility Mod — a `dinput8.dll` injection for Final Fantasy VIII Steam 2013 (App ID 39150, FF8_EN.exe + FFNx v1.23.x) that makes the game playable for blind players via Windows SAPI TTS, navigation assistance, and entity catalogs. Aaron is blind, uses NVDA, and is also the primary tester.

**Project root:** `C:/Users/ampag/OneDrive/Documents/FFVIII-Accessibility-Mod/FF8_OriginalPC_mod/`
**GitHub:** `ampage87/FFVIII-Accessibility-Mod`. Aaron pushes via `Utilities/push_to_github.ps1`; **Claude never pushes**; diagnostic builds stay LOCAL.

Per-chapter history lives in `CHANGELOG.md` (authoritative, one entry per version) and `DEVNOTES_HISTORY.md` (long-form narratives). This file is the live state + backlog + rules only; it stays under 10 KB.

---

## Where we are at session open

**Recent shipped (all CLOSED):** battle diagnostics (#62/#63/#64), #54 status, #48 char-select, PR #26 FMV ADs. Play-by-play in HISTORY + CHANGELOG.

**#65 + #66 Switch screens — DONE / PUSHED v0.18.3.51 / #66 CLOSED.** `PollSwitchScreen` in `menu_tts_switch.inl`; offsets/gating in closed #66 + CHANGELOG.

**#67 World-Map on-foot auto-drive to Dollet — PUSHED (main v0.18.3.87).** The .54-.87 chain landed as ONE squashed commit (was v0.18.3.52 `74bc49a2`). Routing solved by road-pref (.84-.86); executor by yaw-based screen-relative 8-way steering (.87): on foot hdg(0x0203ED02) is the FIXED per-region CAMERA YAW, so screenAngle=(targetBearing-yaw)&0xFFF -> nearest of 8 arrow combos staircases along the road. ARRIVED (sweep-assisted final approach). #67 stays OPEN as the all-continent umbrella; remaining robustness carved into **#68** (executor end-game: route-follower stalls near route end, steer-target oscillation, arrives only via re-plan+sweep) and **#69** (terrain ID: route from geometry to retire the road crutch + .81 hardcode). Handedness if revisited: RIGHT=yaw+90 CW (swap R<->L if bends track wrong).

**#69 builds 1-4 (.88 steepness, .89 offroad 40->20, .90 MECHANISM 2, .91 road cost-preference dropped) ALL BAT'd PASS.** **.90 = the core fix:** per-cell s_elevFine (avg vertex elevation) in RasterizeTriFine; PlanPathFine blocks an inter-cell edge when |elev[a]-elev[b]| > WM_CLIMB_STEP=400 (road-to-road EXEMPT). Cliff face measured 1513-1570 (~4x threshold) so 400 is well-calibrated. By .91 (road help gone) the guard+clearance+steepness carry the Galbadia->Dollet route ALONE and it ARRIVES (the ~5km hang = #68 executor end-game). Files: state.inl/segments.inl/planner.inl. **Builds 5-6 (.92/.93) REGRESSED; build 7 = v0.18.3.94 RESTORES the .85 override -- BAT'd PASS (Dollet reached again).** Cracked by reading the INITIAL plan (not a re-plan): from the SAME save start fine(99,72) that reached Dollet in .91, .93 reported Dollet UNREACHABLE (14-cell dead-end 15 short at the .81 wall). The ONLY .91->.93 routing change was removing the .85 override (offroad was already 0). The Timber->Dollet road runs THROUGH the .81 box (cols 104-111 rows 59-69); the .85 override carves those road cells back to walkable so the route threads the box -- without it the patch walls the road off and DISCONNECTS Dollet. **LESSON (permanent): BOTH the .81 AABB and the .85 override are LOAD-BEARING; only WM_OFFROAD_PENALTY (=0) was safely retired; the .90 guard improves general routing but does NOT replace the Dollet hardcodes.** **Build 8 = v0.18.3.95 (BUILT, pending BAT) = diag-off pre-push:** ROUTE_MAP_DIAG 1->0 (silences [ROUTEMAP]/[ELEVSTEP]/[ELEVMAP]); NO behavior change. BAT .95: BUILDS CLEAN + Dollet still ARRIVES with those lines GONE + Balamb; if clean, PUSH #69 chain (.88-onward; .81+.85 kept, only offroad retired) ONE commit; close #67 (umbrella -- Aaron's call) + BAT 3 coord audit. Backlog #61/#50/#45/#37/Irvine (hold). (OneDrive macro-revert risk: re-sync ff8_accessibility.h if V-key/[PLAN] version != CHANGELOG top.)

**TIMBER TRAIN HIJACK (#56–#60) — DONE / PUSHED / ALL CLOSED** (3 ASK modes; detail in HISTORY/CHANGELOG/closed #56–#60). **#61 still OPEN** (decoder spoken-"L"; root `FF8TextDecode::Decode`).

**Status chapter (#49/#54) — SHIPPED + CLOSED; PR #26 merged; #10/#42/#46/#47/#48 shipped.** Offsets / field orders / Status-Atk stock-count derivation in CHANGELOG + closed #49/#54 (`menu_tts_status.inl`). Forward: `ST_LIMIT_DIAG=false` flips to map Irvine's Shot limit when he joins. OPEN: **#50** Angelo gauges; **#45** junction non-command abilities (blocked — no GF teaches one yet).

---

## Active backlog → GitHub Issues

Tracked bugs/tasks now live as **GitHub issues** on `ampage87/FFVIII-Accessibility-Mod`, not in this file. Check the tracker for the live list. Migrated set (2026-06-01):

Run `github:list_issues` for the live set with full descriptions. Mod-specific breadcrumbs worth keeping here: **#34/#35** need a pre-SeeD save + `LINEDIAG_ENABLED=1` to verify their exit labels; **#37** = the 60/80 KB source-size refactor queue; **#43** = calibrate remaining GF per-level EXP as GFs are obtained. **Open / blocked: #45** (junction non-command abilities — no GF teaches one yet).

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
