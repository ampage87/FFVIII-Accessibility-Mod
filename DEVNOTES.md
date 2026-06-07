**Purpose & context**

Aaron is the sole developer of the FF8 Accessibility Mod — a `dinput8.dll` injection for Final Fantasy VIII Steam 2013 (App ID 39150, FF8_EN.exe + FFNx v1.23.x) that makes the game playable for blind players via Windows SAPI TTS, navigation assistance, and entity catalogs. Aaron is blind, uses NVDA, and is also the primary tester.

**Project root:** `C:/Users/ampag/OneDrive/Documents/FFVIII-Accessibility-Mod/FF8_OriginalPC_mod/`
**GitHub:** `ampage87/FFVIII-Accessibility-Mod`. Aaron pushes via `Utilities/push_to_github.ps1`; **Claude never pushes**; diagnostic builds stay LOCAL.

Per-chapter history lives in `CHANGELOG.md` (authoritative, one entry per version) and `DEVNOTES_HISTORY.md` (long-form narratives). This file is the live state + backlog + rules only; it stays under 10 KB.

---

## Where we are at session open

**Status limit chapter (#49) shipped through v0.18.2.33; local tree v0.18.2.34 (gated diags). Earlier session-open HEAD was v0.18.2.24 (`e5997de`).** The v0.18.2.x menu chapter shipped earlier (v0.18.2.23 = `34cbefc`, June 2; all main-menu character-select TTS — Junction reserve announce, Rearrange party-nav, Magic/Status char-select, active/reserve grouping cues — closed as **#48**). Per-version play-by-play in **DEVNOTES_HISTORY.md** ("v0.18.2.x menu chapter") + `CHANGELOG.md`.

**v0.18.2.24 (shipped, pushed, clean on `main`):** merged community **PR #26** (@djoleninja) — wholesale rewrite of all 21 disc-0 FMV audio descriptions; resolved a stale-PR conflict-marker collision by taking the PR author's text for all 21 (+ minor fixes), VTTs are RCDATA so rebuilt + BAT-confirmed; thank-you posted, loop closed. Full detail in `CHANGELOG.md` + `DEVNOTES_HISTORY.md`.

**Issues closed & shipped: #10, #42, #46, #47, #48, #49 (Status limit chapter, v0.18.2.33); PR #26 merged. OPEN: #54** Status detail pages 1-3 (NEXT chapter); **#50** Rinoa Angelo gauges (deferred follow-up to #49); **#45** junction non-command abilities (blocked — no GF teaches one yet).

**Reusable menu char-select offsets** (subsystem `+0x1E8`, focus `+0x22E`, cursor `+0x1E9` → roster `+0x1DB`, per-char HP `+0x71E`, Rearrange `+0x1B6`, active-party test `savemap+0xAF0`): full values in `NEXT_SESSION_PROMPT.md` (Reusable reference) + `CHANGELOG.md` v0.18.2.x.

**Recent shipped chapters** (detail in CHANGELOG + closed issues + NEXT_SESSION_PROMPT.md, not here): v0.18.1.x Ability "Use GF ability" refine flow (#42, closed); v0.18.0.x GF submenus (#41/#44, closed; #43 open follow-up to calibrate remaining GF EXP costs); v0.17.9.x F9 auto-drive "Track A" edge-math + the SCREEN_BOUND exit-destination interpreter (pushed `808d4802`).

**CURRENT — Status Limit Break chapter (#49) DONE & CLOSED (v0.18.2.33, BAT-confirmed all chars).** `src/menu_tts_status.inl` ships toggles (Gunblade Auto `savemap+0xD1C & 0x01`, Zell Duel-Auto `+0xD1C & 0x02`, Renzokuken Indicator `+0xD1D & 0x80` 0=ON inverted; keyed off GCW help, announce name+on/off, Renz "disabled" on focus when Gunblade on) + read-only per-char lists. Limit page = `pMenuStateA+0x257==3`; per-char cursor byte hardcoded Squall `+0x25F` (step1, leadingToggles4: cur0-3 = 2 toggles x ON/OFF, 4+ finishers) / Zell `+0x260` / Quistis `+0x262`(auto) / Rinoa `+0x263`. Baked move descs on toggle pages (GCW help lags there); learned-row→full-table index map for descs. **v0.18.2.34** = `[STBAND]`/`[STLIMIT]` diags restored behind `ST_LIMIT_DIAG=false` (flip to map Irvine's Shot page).
**NEXT — #54: Status detail pages 1-3** (stats/equipment, resistances, GF compat). Page index `+0x257` (3=limit done; 1-3 likely 0/1/2 — verify first). Same GCW-scrape pattern; honor #44 rule (meaning, not raw numbers). **#50** = Rinoa Angelo %-learned + currently-learning (deferred; data ~`savemap+0xB16`, GF-style known/learning/points). **#54 design FINALIZED (full spec in #54 comments):** each page on entry speaks its name + enumerates its number-key shortcuts. P1 stats 0-9 (0 overview/1 exp/2-7 six stats/8 eva+hit/9 equip); P2 1=Elem Attack, 2=Elem Resist, 3=Status Attack, 4=Status Resist (4 panels; attack panels render only when junctioned); P3 0=all GF compat (diamond=junctioned). Page index `+0x257`=0/1/2 (3=limit). Values are font-rendered TEXT in a window `SnapshotGcwBuffer` doesn't grab (find it / FFNx `character_data_1CFE74C`); P2 element/status ICONS are sprites → fixed-order tables. **NEXT = diagnostic build to locate the value buffers** (no design Qs left).

---

## Active backlog → GitHub Issues

Tracked bugs/tasks now live as **GitHub issues** on `ampage87/FFVIII-Accessibility-Mod`, not in this file. Check the tracker for the live list. Migrated set (2026-06-01):

- #30 menu_tts.cpp T-handler missing `!shift` gate (trivial)
- #31 FieldAnnounce display-name audit (0x0134/0x0136; verify Fire Cavern A 0x0088 `bdview1`)
- #32 Field-name populate race at Part-B arrival (diagnostic-log only; audio fine)
- #33 Update Dollet timer countdown deep-research doc (doc)
- #34 Verify bgryo1_1 'squalls' exit label (needs pre-SeeD save; flip `LINEDIAG_ENABLED`=1)
- #35 Verify dotown_2 'Selphie' chase exit label (flip `LINEDIAG_ENABLED`=1)
- #36 F9 auto-drive gateway target computes bogus driveSkipTrigIdx (~201) — range guard
- #37 Source-file size refactor queue (60/80 KB guard)
- #38 Parked diagnostics / contingency watch (re-enable only if symptom recurs)
- #39 Deferred / someday backlog (umbrella)
- #42 Main-menu Ability screen TTS (cursor idx 5) — CLOSED (v0.18.1 refine flow shipped)
- #43 Calibrate remaining GF per-level EXP costs (#41 follow-up) — low-effort, do as GFs are obtained
- (#40 closed as duplicate of #41; #41 + #44 closed completed — GF chapter done)

**Closed since the migration:** #10, #41, #42, #44, #46, #47, #48. **Open / blocked:** #45 (junction non-command abilities — no GF teaches one yet).

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
