**Purpose & context**

Aaron is the sole developer of the FF8 Accessibility Mod — a `dinput8.dll` injection for Final Fantasy VIII Steam 2013 (App ID 39150, FF8_EN.exe + FFNx v1.23.x) that makes the game playable for blind players via Windows SAPI TTS, navigation assistance, and entity catalogs. Aaron is blind, uses NVDA, and is also the primary tester.

**Project root:** `C:/Users/ampag/OneDrive/Documents/FFVIII-Accessibility-Mod/FF8_OriginalPC_mod/`
**GitHub:** `ampage87/FFVIII-Accessibility-Mod`. Aaron pushes via `Utilities/push_to_github.ps1`; **Claude never pushes**; diagnostic builds stay LOCAL.

Per-chapter history lives in `CHANGELOG.md` (authoritative, one entry per version) and `DEVNOTES_HISTORY.md` (long-form narratives). This file is the live state + backlog + rules only; it stays under 10 KB.

---

## Where we are at session open

**Latest PUSHED = `c9a4c777` = v0.18.3.33 — #64 (Scan-UI screenshot) gated off; BAT-confirmed, pushed, and CLOSED** (parent `f48538d`=v0.18.3.32 = #62/#63; `f741495`=v0.18.3.31 = #62 screenshots-folder fix; `bb3ba05`=v0.18.3.30 = Timber train hijack). All three battle diagnostic-capture families are now gated: damage-number (#62), battle-log spam (#63), scan UI (#64). Prior shipped: #54 status submenu (v0.18.2.50, `faa31be`); #48 menu char-select (`34cbefc`); PR #26 FMV ADs. Per-version play-by-play in DEVNOTES_HISTORY + CHANGELOG.

**ACTIVE CHAPTER: #65 Switch submenu TTS.** **LOCAL = `v0.18.3.37`** (main-menu Switch FEATURE-COMPLETE + BAT-confirmed; `.34`–`.37` unpushed stack: discovery→v1→polish→LV/HP). **Confirmed (v0.18.3.34 `[SwitchDiag]` BAT):** Switch subsystem **`+0x1E8 == 10`**; focus **`+0x22E`**: **12 = two-option action bar**, **2 = member-select list**; action option **`+0x25E`**: **0 = Switch Member** (help "Please make a party of 3"), **1 = Junction Exchange** (help "Exchanges all that is junctioned") — both options real. Member list renders 3 active + highlighted candidate as `<Name>LVHP` tokens (4th = cursor target). Swap rewrites a working buffer (`+0x262`/`+0x265` hold char-ids; e.g. `1↔3` = Zell↔Quistis) and the on-screen trio; roster `+0x1DB` reorders on EXIT (active = first 3). **STILL OPEN:** the source/destination cursor bytes (`+0x24C`/`+0x24E`/`+0x262` cluster — tangled in the one busy capture) and the savemap active-party commit timing/target (`+0xAF1`/`+0x0B04` did NOT fire — commit happens as the screen tears down, after the diag un-gates). **v1 (`menu_tts_switch.inl`, `PollSwitchSubmenu`, gated `+0x1E8==10`):** speak-what's-shown — action-bar option (from `+0x25E` + help), member list active trio on entry, candidate name + active/reserve on cursor move, new party after a swap. Reads names by scanning the GCW for known `CHAR_NAMES`, so it doesn't need the cursor bytes. `SWITCH_DISCOVERY_DIAG` now OFF (=0; structure confirmed; gate, don't delete). v0.18.3.36 fixed the post-swap interrupt + exact labels; **v0.18.3.37** adds level + HP to the member-list announces ("Name, active/reserve, Level N, HP X of Y") via the new POD+SEH helper `SwitchCharLevelHP`, reusing `AnnounceJuncCharSelect`'s reads (EXP→level flat 1000; HP from computed-stats `0x1CFF000` for live-formation members, else menu HP array `+0x71E` for benched), with the char-id taken from the name scan. **Main-menu Switch is DONE (LOCAL), BAT-confirmed.** **Next:** Aaron pushes the `.34`–`.37` stack (+ the `.32` CHANGELOG-heading restore) via `Utilities/push_to_github.ps1`; then either close #65 or keep it open for the forced party/junction-switch story screens (the agreed follow-up). Visual reference: `Logs/screenshots/f11_201005_871.png` (Switch Member) + `f11_201019_730.png` (Junction Exchange) — left column = 3 active w/ name+LV+HP, right-top = reserve portraits, right-bottom = highlighted char's name+LV+HP. **Battle diagnostic cleanup arc COMPLETE** (repo clean at pushed `v0.18.3.33`; #62/#63/#64 closed). The `## v0.18.3.32` CHANGELOG-heading restore is intact and rides the next push. Other open backlog (deprioritized): #61, #50/#51/#52/#53, Irvine Shot limit (on hold).

**TIMBER TRAIN HIJACK CHAPTER (#60) — DONE / PUSHED (`bb3ba05`, v0.18.3.30) / #56-#60 ALL CLOSED.** Fully playable blind; 3 modes via in-engine ASK: **Manual** (DEFAULT, guards move + code announce + per-guard cues), **Freeze** (guards held var1040=0, just enter codes), **Skip** (bypass to field 892, −1 SeeD-rank cost). Filed **#61** this session (the text decoder renders the page-break/line-break control code as a literal "L" game-wide; root site `FF8TextDecode::Decode`; v0.18.3.28 was only a narrow speak-layer workaround). Optional/not built: full-auto "Auto", cosmetic ASK box width clip. Full per-mode discovery detail (3-mode scheme, var maps, #56/#57 code-announce, #58 guard Y-axis discriminator, #59 timer fix) archived to DEVNOTES_HISTORY.md (2026-06-12); see CHANGELOG + closed issues #56-#60.

**Issues closed & shipped: #10, #42, #46, #47, #48, #49 (Status limit chapter, v0.18.2.33); PR #26 merged. #54** Status detail pages 1-3 SHIPPED + CLOSED v0.18.2.50. OPEN: **#50** Rinoa Angelo gauges (deferred follow-up to #49); **#45** junction non-command abilities (blocked — no GF teaches one yet).

**Reusable menu char-select offsets** (subsystem `+0x1E8`, focus `+0x22E`, cursor `+0x1E9` → roster `+0x1DB`, per-char HP `+0x71E`, Rearrange `+0x1B6`, active-party test `savemap+0xAF0`): full values in `NEXT_SESSION_PROMPT.md` (Reusable reference) + `CHANGELOG.md` v0.18.2.x.

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
