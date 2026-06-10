**Purpose & context**

Aaron is the sole developer of the FF8 Accessibility Mod — a `dinput8.dll` injection for Final Fantasy VIII Steam 2013 (App ID 39150, FF8_EN.exe + FFNx v1.23.x) that makes the game playable for blind players via Windows SAPI TTS, navigation assistance, and entity catalogs. Aaron is blind, uses NVDA, and is also the primary tester.

**Project root:** `C:/Users/ampag/OneDrive/Documents/FFVIII-Accessibility-Mod/FF8_OriginalPC_mod/`
**GitHub:** `ampage87/FFVIII-Accessibility-Mod`. Aaron pushes via `Utilities/push_to_github.ps1`; **Claude never pushes**; diagnostic builds stay LOCAL.

Per-chapter history lives in `CHANGELOG.md` (authoritative, one entry per version) and `DEVNOTES_HISTORY.md` (long-form narratives). This file is the live state + backlog + rules only; it stays under 10 KB.

---

## Where we are at session open

**GitHub HEAD = `faa31be` = v0.18.2.50 — #54 status submenu SHIPPED + tagged** (verified via git transcript `92297c8..faa31be -> main` + local ref). The whole status detail submenu (Pages 1/2/3, all keys) is PUSHED. **#54 CLOSED** (v0.18.2.50, closing comment posted 2026-06-09). Earlier shipped: the v0.18.2.x menu char-select chapter (#48, `34cbefc`) + PR #26 FMV ADs (v0.18.2.24). Per-version play-by-play in DEVNOTES_HISTORY + CHANGELOG.

**ACTIVE CHAPTER — Timber train hijack minigame accessibility (#60 umbrella).** **CODE-ENTRY MECHANIC CRACKED (#56/#57) via F11 screenshots + probe (practice field `tiagit5`):** win[4] is a FROZEN template (`"The code is... . + - =!"`, byte-identical every frame); the live 4-digit code is **sprite-drawn** yellow numbers over it — confirmed: codes `1232`@17:25:41 and `4331`@17:25:49 change while the win[2] legend stays constant. So the code is NOT in any text buffer — must read it from game memory. **Keypad legend (resolves #57):** directional cross, fixed mapping **1=Right, 2=Down, 3=Left, 4=Up**; W/A/D/X = player's directional keys, Q=quit (e.g. `4 3 3 1` = Up,Left,Left,Right). Exe disasm confirmed the sprite-drawn finding (field text engine `0x4a0d10`/`0x4a1200` has no inline number-from-variable code; the 4 values live in the `tiagit5` field varblock). **Announce the DIGITS** (e.g. "4 3 3 1"), matching the visual (Aaron). The `[TRAINCODE]` probe stays gated/retained. **Local HEAD now v0.18.3.2** = `[SCRIPT-DUMP]` JSM dump: `FieldArchive::DumpTrainCodeScripts()` (field_archive_jsm_dump.inl) dumps the code-apparatus entities (SYM `ango*`/`key*`, else all 'Other') via DumpEntityScript; `TrainCodeJsmDump()` (gated `TRAIN_JSM_DUMP_DIAG 1`, field_dialog_diag.inl) auto-fires once on `tiagit*` entry from PollWindows -> ff8_field.log. LOCAL; v0.18.3.2 BAT'd 2026-06-09: **the 4 code digits = field-varblock vars 1026-1029 (values 1-4)** -- Keykantoku (ent13) writes them, Angoyarukun (ent12) displays, Keyjokantoku (ent14) draws+generates; varblock base 0x1CFE9B8. v0.18.3.3 `[CODEVAR]` BAT'd: **digits = bytes at 0x1CFE9B8+1026..1029, values 1-4, left-to-right** (only candidate reading four 1-4 values on the ~5s cycle; word/long/idx*2/idx*4 = garbage/zero). v0.18.3.4 BAT'd: **practice panel WORKS** -- Aaron heard the codes (assertive TTS), entered them (W/A/D/X; 1=R,2=D,3=L,4=U), completed the practice; order confirmed correct. v0.18.3.5 BAT'd: real code field = **`tilink1`** (id902; trail tiagit1/3/4->tiyane1/2/3->titrain1->tilink1), apparatus present (Ango/Key/Keyjo) -- but 1026-1029 there read `[1 1 1 x]`, NOT the live code (practice never produced 3 identical leading digits), so the digits sit at DIFFERENT indices on tilink1. v0.18.3.7 BAT'd -- **#56 DONE + CLOSED.** Real-train code = **tilink1 vars 1029-1032** (practice 1026-1029 shifted +3; found via `[TRAINWIN]`). `TrainCodeAnnounce` picks the base per field (1026 tiagit*, 1029 tilink*); Aaron entered the spoken codes and uncoupled the car ([TRAINCODE-SAY] 2221/4334 matched [TRAINWIN]). #57 resolved (1=Right,2=Down,3=Left,4=Up). v0.18.3.8 (cleanup, awaiting build): turned off `TRAIN_FIELD_SCAN_DIAG` + `GUARD_RECON_DIAG` (retained behind #define). **#58 guard recon (this session):** runtime ents 5/6 = GalHei1/GalHei2 patrol X~-1315, Y~[-505,1640], ~150u/s, opposite phase, talk/push=128/48 (detection is script logic, not radius); a guard passed within 71u of the stationary player at the panel with NO catch -> catch is during the *crossing*, not at the device. #58 next: dump GalHei1/2 + controller JSM for the line-of-sight/catch trigger. Recon posted to #58. Three-mode design (Auto/Manual/Original) mirroring the X-ATM092 chase. Sub-issues: **#56** announce Rinoa's uncoupling code numbers (FIRST target), **#57** code-entry key layout, **#58** guard awareness, **#59** timer freeze+readout. Working the **code-entry mechanic first** on the briefing-room PRACTICE panel (no timer/guards, repeatable).  Timer is the shared FF8 countdown engine (same as Dollet/Fire Cavern — see #33 + `Plan & Research Documents/Dollet timer...results.md`); locate via in-process scanner (no CE/x64dbg). Aaron has a save just before the briefing.

**Issues closed & shipped: #10, #42, #46, #47, #48, #49 (Status limit chapter, v0.18.2.33); PR #26 merged. #54** Status detail pages 1-3 SHIPPED + CLOSED v0.18.2.50. OPEN: **#50** Rinoa Angelo gauges (deferred follow-up to #49); **#45** junction non-command abilities (blocked — no GF teaches one yet).

**Reusable menu char-select offsets** (subsystem `+0x1E8`, focus `+0x22E`, cursor `+0x1E9` → roster `+0x1DB`, per-char HP `+0x71E`, Rearrange `+0x1B6`, active-party test `savemap+0xAF0`): full values in `NEXT_SESSION_PROMPT.md` (Reusable reference) + `CHANGELOG.md` v0.18.2.x.

**Recent shipped chapters** (detail in CHANGELOG + closed issues + NEXT_SESSION_PROMPT.md, not here): v0.18.1.x Ability "Use GF ability" refine flow (#42, closed); v0.18.0.x GF submenus (#41/#44, closed; #43 open follow-up to calibrate remaining GF EXP costs); v0.17.9.x F9 auto-drive "Track A" edge-math + the SCREEN_BOUND exit-destination interpreter (pushed `808d4802`).

**Status Limit chapter (#49) DONE & CLOSED (v0.18.2.33).** `menu_tts_status.inl`: limit page `+0x257==3`; toggles `savemap+0xD1C` (Gunblade/Duel) + `+0xD1D&0x80` (Renz, 0=ON); per-char cursor `+0x25F..0x263`. `ST_LIMIT_DIAG=false` (`[STBAND]`/`[STLIMIT]`; flip to map Irvine's Shot). Detail: CHANGELOG + #49.
**#54 Status detail pages (Pages 1/2/3 all IMPLEMENTED; LOCAL).** Ships in `menu_tts_status.inl`. Page 1 keys 0/1/2-7/8/9; Page 3 key 0 GF compat — BAT-confirmed. **Page 2 (v0.18.2.44):** key 1 Elem-Atk, key 2 Elem-Resist (8 words @comp+0x194, `%=word−800`), key 4 Status-Resist (13 bytes @+0x1A4, `%=byte−100`); full offsets/orders in CHANGELOG + NEXT_SESSION. Key 3 Status-Atk DONE (v0.18.2.50): % = stock count of the junctioned ST-Atk spell (ST-Atk-J inflicts 1%/spell, max 100 — Wiki/Neoseeker/GameFAQs; the earlier "66" WAS the answer = 66 Berserk stocked, not coincidence). Reads char+0x66 id → ST_MAGIC_NAMES spell name + count from magics[32] @char+0x10 (u16: id low byte, qty high). Speaks “Status attack, Berserk, 66 percent”; [STPAGE2] key=3 log shows magicId+stock. Scanner SHELVED (3 variants failed; value never in a kernel table). ST_PAGE2_DIAG + ST_MAGSCAN off. **NEXT: BAT v0.18.2.50** Status→Page 2 key 3 → confirm name + % (flip packing if stock reads wrong), then push Pages 1/2/3 as ONE package (HEAD v0.18.2.34; confirm `list_commits`). **#50** Angelo gauges deferred.

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
