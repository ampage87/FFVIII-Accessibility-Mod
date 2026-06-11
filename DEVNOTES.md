**Purpose & context**

Aaron is the sole developer of the FF8 Accessibility Mod — a `dinput8.dll` injection for Final Fantasy VIII Steam 2013 (App ID 39150, FF8_EN.exe + FFNx v1.23.x) that makes the game playable for blind players via Windows SAPI TTS, navigation assistance, and entity catalogs. Aaron is blind, uses NVDA, and is also the primary tester.

**Project root:** `C:/Users/ampag/OneDrive/Documents/FFVIII-Accessibility-Mod/FF8_OriginalPC_mod/`
**GitHub:** `ampage87/FFVIII-Accessibility-Mod`. Aaron pushes via `Utilities/push_to_github.ps1`; **Claude never pushes**; diagnostic builds stay LOCAL.

Per-chapter history lives in `CHANGELOG.md` (authoritative, one entry per version) and `DEVNOTES_HISTORY.md` (long-form narratives). This file is the live state + backlog + rules only; it stays under 10 KB.

---

## Where we are at session open

**GitHub HEAD = `1b32bf3` = v0.18.3.8 — Timber code chapter SHIPPED** (`faa31be`->`1b32bf3`, verified via list_commits 2026-06-10). The whole v0.18.3.x Timber code-entry chapter (announce + diag cleanup) is PUSHED. **#56 SHIPPED + CLOSED** (real-train code announce; #57 resolved). Prior shipped: #54 status submenu (v0.18.2.50, `faa31be`); v0.18.2.x menu char-select (#48, `34cbefc`); PR #26 FMV ADs. Per-version play-by-play in DEVNOTES_HISTORY + CHANGELOG.

**ACTIVE CHAPTER — Timber train hijack minigame accessibility (#60 umbrella).** Three-mode design (**Auto/Skip** = bypass minigame via the success path->field925; **Manual** = player enters code, guards frozen via var1040; **Original** = player enters code + audio guard-proximity cue) mirroring the X-ATM092 chase. Sub-issues: **#56** code announce (SHIPPED v0.18.3.8, CLOSED), **#57** key layout (RESOLVED), **#58** guard awareness (ACTIVE), **#59** timer freeze+readout, **#60** mode ASK + Auto automation.

**#56/#57 shipped:** uncoupling code is **sprite-drawn**, read from FFNx varblock `0x1CFE9B8` bytes **1026-1029 (`tiagit*`)** / **1029-1032 (`tilink*`)**; `TrainCodeAnnounce()` (field_dialog_lifecycle.inl, from PollWindows) speaks them per settled code; repeat key re-announces. #57 keys = **1=Right/D, 2=Down/X, 3=Left/A, 4=Up/W, Q=quit**. Discovery diags off (behind `#define`). Detail: CHANGELOG.

**#58 GUARDS (ACTIVE; Manual BAT-CONFIRMED v0.18.3.14, Original BAT-CONFIRMED v0.18.3.20 -- Y-axis discriminator, two guards per car + correct lead time).** Guards DO catch Squall during code entry (real mechanic, NOT decorative). **Failure = a field ASK** ("Rinoa: What happened!? Squall!!!", `[AASK]` in ff8_dialog.log); field freezes, no mapjump; shared by catch AND timer-expiry. **Var map (tilink1, varblock 0x1CFE9B8 byte-indexed):** **1040 = patrol switch (1=patrol, 0 FREEZES guards)**, 1042 = round-active (validators gate on this), 1029-32 = live code (=[TRAINCODE-SAY]), 1024-27 = entered digits, 1028 cnt, 1043 = entry stage, reload spawn picked by savemap 724. **Machine entities:** blind2/blind3 (model 5/6) = the patrol guards walking rail **X=-1313** (no player-pos read; gated 1040); blind4 = master (method4=FAIL->MAPJUMP3 reload field902; method5=SUCCESS->MAPJUMP3 field925); blind5/6/7 = per-digit validators (gated 1042); blind8 = code display. Catch = a MOVING guard reaching ~talk radius 128 of the player (static entities never catch). Full .11-.13 discovery dumps archived in NEXT_SESSION_PROMPT.md. **MANUAL (v0.18.3.14, WORKS):** `GuardManualFreeze()` (field_dialog_lifecycle.inl, PollWindows) pins var1040=0 on `tilink*`; entry+win still work. Gate `tilink*` covers BOTH cars -- **tilink1(902)+tilink2(903)**. **Real win dest = field 892 (Forest Owls' Base)** via tilink2 MAPJUMP3 (NOT 925). INI `train_guard_mode` [Accessibility] 0=Orig/1=Manual/2=Skip, default Manual. **ORIGINAL (v0.18.3.20, BAT-CONFIRMED -- Aaron: exactly two guards per car, approaching lead time "just right"):** `GuardOriginalCue()` (field_nav_observe.inl, each tick before observer gates) announces PER-GUARD -- "Guard N approaching/close/clear" (recede close->approaching SILENT; only "close" interrupts). **Guard-vs-party = the Y AXIS (v0.18.3.20, per Aaron + F11 screenshots):** the train runs left-right; Squall drops DOWN to the panel (screen bottom) while the party stay ON THE ROOF (screen top), so a party member can be horizontally near Squall yet FAR on the depth/Y axis -- which Euclidean distance (v.17-.19) wrongly flagged. So proximity is judged on **|entity.Y - player.Y| ALONE**: close <=480, approaching <=960, clear >=1152 (hysteresis 960..1152). Guards patrol the corridor so their Y sweeps through Squall's (catch at |dY|~90); the roof party sit at |dY|>=~1360 and are ignored/never labelled. Motion gate kept as a secondary guard vs static same-lane props; per-guard lowest-free labels released on recede. (Axes ROTATED -- this Y axis is HORIZONTAL on screen, hence earlier "vertical" mislabel.) **tilink1 coords (v0.18.3.18 BAT):** player Y=1509; guards ent5/ent6 sweep Y -505..1662 (cross 1509); party ent1 Y=-415, ent2 Y=145 (|dY| 1924/1364, static). **BAT history:** .15-.17 distance variants; .18 live-motion; .19 approach-gated labels; .20 Y-axis. Mode = SHARED accessor `FieldDialog::GetTrainGuardMode()`/`SetTrainGuardMode()` + `enum TrainGuardModeVal` in field_dialog.h. Consts CLOSE_DY/APPROACH_DY/CLEAR_DY atop GuardOriginalCue(); [GUARDCUE] logs dY+dist+pos. **v0.18.3.21 cleanup: GUARD_RECON_DIAG ([GUARDPOS]) + GUARD_VAR_DIAG ([GUARDVAR]/[GUARDFREEZE]) turned OFF (retained behind flags) -- field log clean, build shippable for Manual+Original. [GUARDCUE] (feature, level-change only) + the Manual var-pin are unchanged.** **NEXT: mode ASK** (in-engine Skip/Manual/Original picker, modeled on `chase_ask_overlay` -> `SetTrainGuardMode()`), **then Skip** (tilink1->tilink2->892; capture a [GUARDVAR] win for the flags). Mind the no-catch SeeD-rank-up bonus. Timer (#59) = shared FF8 countdown engine (Dollet/Fire Cavern; #33); Aaron has a save just before the briefing.

**Issues closed & shipped: #10, #42, #46, #47, #48, #49 (Status limit chapter, v0.18.2.33); PR #26 merged. #54** Status detail pages 1-3 SHIPPED + CLOSED v0.18.2.50. OPEN: **#50** Rinoa Angelo gauges (deferred follow-up to #49); **#45** junction non-command abilities (blocked — no GF teaches one yet).

**Reusable menu char-select offsets** (subsystem `+0x1E8`, focus `+0x22E`, cursor `+0x1E9` → roster `+0x1DB`, per-char HP `+0x71E`, Rearrange `+0x1B6`, active-party test `savemap+0xAF0`): full values in `NEXT_SESSION_PROMPT.md` (Reusable reference) + `CHANGELOG.md` v0.18.2.x.

**#49 Status Limit SHIPPED+CLOSED (v0.18.2.33).** Offsets in CHANGELOG + #49. `ST_LIMIT_DIAG=false` in `menu_tts_status.inl` -- flip to map Irvine's Shot when he joins.
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
