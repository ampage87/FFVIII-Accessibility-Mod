**Purpose & context**

Aaron is the sole developer of the FF8 Accessibility Mod — a `dinput8.dll` injection for Final Fantasy VIII Steam 2013 (App ID 39150, FF8_EN.exe + FFNx v1.23.x) that makes the game playable for blind players via Windows SAPI TTS, navigation assistance, and entity catalogs. Aaron is blind, uses NVDA, and is also the primary tester.

**Project root:** `C:/Users/ampag/OneDrive/Documents/FFVIII-Accessibility-Mod/FF8_OriginalPC_mod/`
**GitHub:** `ampage87/FFVIII-Accessibility-Mod`. Aaron pushes via `Utilities/push_to_github.ps1`; **Claude never pushes**; diagnostic builds stay LOCAL.

Per-chapter history lives in `CHANGELOG.md` (authoritative, one entry per version) and `DEVNOTES_HISTORY.md` (long-form narratives). This file is the live state + backlog + rules only; it stays under 10 KB.

---

## Where we are at session open

**Latest PUSHED = v0.18.3.51 — #65/#66 Switch screens unified into one memory-first engine; #66 CLOSED.** (#65 main-menu Switch shipped earlier v0.18.3.37 `b342a5df`, already closed.) Battle diagnostic captures all gated (#62/#63/#64 closed). Prior shipped: #54 status (v0.18.2.50), #48 char-select, PR #26 FMV ADs. Play-by-play in DEVNOTES_HISTORY + CHANGELOG.

**LOCAL (built, not pushed) = v0.18.3.52 — #67 step 1: world-map Balamb-nav CI guard.** Pure extraction of the coord/segment/BFS math into new `src/world_map_geometry.inl` (no Win32/SEH; included after state.inl) + new CI job `world-map-harness` (compiles the REAL geometry vs a committed wmx.obj terrain snapshot `tests/world_map_terrain_grid.txt` → `gen_world_map_fixture.py`; asserts Balamb on-foot reaches Garden/Town/Fire Cavern, excludes Dollet (Galbadia), exactly 3). Container-verified PASS + negative-control FAIL. NO runtime change. BAT-CONFIRMED v0.18.3.52 (Garden→Balamb Town auto-drive worked — extraction is behavior-preserving); ready to push. Mirrors the `chase-harness` pattern.

**#65 + #66 Switch screens — DONE / PUSHED v0.18.3.51 / #66 CLOSED.** Main-menu Switch (menu mode 6) + forced party-select (game mode 10) are the SAME UI on one engine `PollSwitchScreen(state, off)` in `menu_tts_switch.inl` (wrappers off=0 forced / off=0x78 main, gated `s_prevCursor==6 && +0x1E8==10`). **Off-relative offsets:** focus +0x1B6 (2=grid/0x0C=bar), option +0x1E6 (0=Switch/1=Junction), cursor +0x1E7, active +0x1E8/reserve +0x1E9, active slots +0x1EA (0xFF=empty), reserves +0x1ED. **Fixed:** roster +0x1DB, menu HP +0x71E, savemap +0xAF0/+0x48C. All BAT-confirmed.

**GitHub MCP connector configured** (`github:` tools for issues/comments/commits; curl raw.githubusercontent.com still fine for file reads). **ACTIVE: #67 World-Map nav (guard landed v0.18.3.52).** Galbadia catalog comes up empty: the live position maps onto ocean cells in the wmx grid (negative-X / western hemisphere) so on-foot BFS reaches 1/768 and the catalog filters to 0; Balamb (positive-X) works. Root NOT yet pinned between (a) coord-mapping offset skewed west vs (b) terrain classifier mislabeling Galbadia land — do NOT guess (v0.14.73). NEXT: a blind-accessible `WM_CALIB_DIAG` position-trace (live pos→seg→terrain class + region byte as Aaron walks Galbadia) to disambiguate, then fix + add a 2nd harness assertion (Galbadia empty→Deling/Timber/Galbadia Garden present). **Then #61** (decoder spoken-"L"; root `FF8TextDecode::Decode`). Backlog: #50-53, #37 (file-size refactor), Irvine Shot page (hold).

**TIMBER TRAIN HIJACK (#56–#60) — DONE / PUSHED (`bb3ba05`, v0.18.3.30) / ALL CLOSED.** 3 in-engine ASK modes (Manual/Freeze/Skip). **#61 still OPEN** (decoder spoken-"L"; root `FF8TextDecode::Decode`). Detail in DEVNOTES_HISTORY + CHANGELOG + closed #56–#60.

**Issues closed & shipped: #10, #42, #46, #47, #48, #49 (Status limit chapter, v0.18.2.33); PR #26 merged. #54** Status detail pages 1-3 SHIPPED + CLOSED v0.18.2.50. OPEN: **#50** Rinoa Angelo gauges (deferred follow-up to #49); **#45** junction non-command abilities (blocked — no GF teaches one yet).

**#49 Status Limit + #54 Status detail pages 1/2/3 — SHIPPED + CLOSED (v0.18.2.33 / v0.18.2.50).** All offsets, field orders, and the Status-Atk stock-count derivation are in CHANGELOG + closed issues #49/#54 (lives in `menu_tts_status.inl`). Forward note: `ST_LIMIT_DIAG=false` in `menu_tts_status.inl` flips to map Irvine's Shot limit when he joins (the on-hold Irvine Shot page backlog item). **#50** Angelo gauges deferred.

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
- **B-Garden Cafeteria 1** (raw SYM `Son` leaks): `0x009A`.

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
