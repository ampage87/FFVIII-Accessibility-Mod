**Purpose & context**

Aaron is the sole developer of the FF8 Accessibility Mod — a `dinput8.dll` injection for Final Fantasy VIII Steam 2013 (App ID 39150, FF8_EN.exe + FFNx v1.23.x) that makes the game playable for blind players via Windows SAPI TTS, navigation assistance, and entity catalogs. Aaron is blind, uses NVDA, and is also the primary tester.

**Project root:** `C:/Users/ampag/OneDrive/Documents/FFVIII-Accessibility-Mod/FF8_OriginalPC_mod/`
**GitHub:** `ampage87/FFVIII-Accessibility-Mod`. Aaron pushes via `Utilities/push_to_github.ps1`; **Claude never pushes**; diagnostic builds stay LOCAL.

Per-chapter history lives in `CHANGELOG.md` (authoritative, one entry per version) and `DEVNOTES_HISTORY.md` (long-form narratives). This file is the live state + backlog + rules only; it stays under 10 KB.

---

## Where we are at session open

**GitHub HEAD = v0.18.1.3 (`af7bf5b`) — Ability screen (#42) Build 1 (ability-list phase) COMPLETE & PUSHED.** Parent v0.18.0.15 (`9102689`) — GF submenu (#41 + #44, CLOSED). The whole main-menu GF screen (#41) and AP readout (#44) shipped and verified row-by-row against the F11 screenshot `f11_170221_023.png` (Quezacotl: all 11 rows match name + AP + learned, Learning panel 117/140 matches detail key 5; screen "Complete!" == TTS "Learned"). Diagnostics off (GF_DIAG=0, GF_AP_DIAG=0). The accidental duplicate #40 (identical title to #41) was closed as not-planned during prep. **THIS SESSION: Ability submenu (#42) under 0.18.1.x** — first bump `FF8OPC_VERSION` -> 0.18.1.0 when the first Ability build is ready; read #42 + `NEXT_SESSION_PROMPT.md` first. **Versioning: 0.18.0.x = GF submenu (done), 0.18.1.x = Ability submenu, 0.18.3+ = other menus.**

**Ability screen (#42) discovery DONE (BAT 18:45, no rebuild — SUBMON already in HEAD).** The screen is the **"Use GF ability"** action screen (refine etc.), NOT a GF-picker/category/AP-learn flow (issue #42's body is the wrong model — needs rewriting; offered to Aaron). Confirmed offsets vs `pMenuStateA`: gate **`+0x1E8==14`**; phase **`+0x22E`** (3 = ability list, ~19–21 = refine item list); **`+0x258`** = ability-list cursor (0=I Mag-RF,1=Tool-RF); **`+0x2DF`** = item-list cursor; secondary `+0x230`/`+0x5DF`. Names/help/items read from GCW; `GetAbilityName()` covers the ids; no AP here. Full block + first-build plan in `NEXT_SESSION_PROMPT.md`.

**Ability Build 2b refinable-tag IMPLEMENTED (option 2b, settle-based) — v0.18.1.7, ABIL_DIAG=0, awaiting BAT (local, NOT pushed). If BAT passes this is the shippable build (carries Build 2 + the tag).** On the refine item list, name+qty still speaks instantly on `+0x2DF` move; after a ~400ms dwell on a real item, `AbilReadRefinePtr` reads `+0x2BE` and speaks "Refinable"/"Cannot be refined" as a queued 2nd clip (Aaron likes the pause). `+0x2BE` non-zero => refinable; it's 0 for non-refinable and cleared on leaving a refinable item, so a settled read has no false positives. Diagnostics gated off (`ABIL_DIAG 0`); `AbilDumpMenuWindow` wrapped under `#if ABIL_DIAG` (retained). New always-on log `[MenuTTS] Refine status <cur>: <text> (ptr=0x...)` for BAT verification. **On BAT:** open I Mag-RF item list, arrow to items pausing on each; confirm refinables (M-Stone Piece/Magic Stone/Wizard Stone/Arctic Wind/Fish Fin/Silence Powder) say "Refinable" and the rest "Cannot be refined"; spot-check Tool-RF too (different recipe set — validates the generality of the pointer approach). Read `Logs/ff8_menu.log` `[MenuTTS] Refine status` lines (ptr!=0 on refinables, 0 on others). If good: `Utilities/push_to_github.ps1` (Claude never pushes); then verify HEAD via `github:list_commits`. **Background (option 1 dead):** no synchronous/static per-item refinable flag exists in pMenuState `+0x000..+0x3FF`; `+0x2BE`/`+0x2C2` are the engine's refine-result pointers, populated lazily — hence the dwell. The rendered preview is unreliable per-move (stale text bleeds across items; e.g. a false "Dino Bone refinable" from adjacent Arctic Wind), so it's used only by the dwelling `/` reader.

**GF chapter detail (closed) — reusable specifics moved to `NEXT_SESSION_PROMPT.md` “Reusable from the GF chapter”; per-version detail in `CHANGELOG.md` v0.18.0.x.** Quick recap: GF-list cursor `pMenuStateA+0x253` (gate `+0x1E8==4`); detail keys 1..7 follow the Q/R-displayed GF (`s_gfDetailIdx` from the GCW header, not `+0x253`); detail values (HP `+0x12`, flat-cost level/EXP, compat `(6000-raw)/5`) are render-time only. Learn list paginated (cursor `+0x257`/`+0x258`), readout “&lt;name&gt;, Learned” / “&lt;name&gt;, C out of R AP”, empty rows “Empty Ability Slot”, `/` reads help only. AP from baked `ability_ap_cost[116]` + `gf_ability_slots[16][22]` (read `APs[slot-of-id]`, never `APs[id]`). `GF_DIAG=GF_AP_DIAG=0`. Open follow-ups: #43 (calibrate remaining GF EXP costs), “Siren A” name-table trim.

**Prior push: v0.17.9.17** (`808d4802`) — **Track A COMPLETE & PUSHED** (parent v0.17.9.11 `3478683`). F9 auto-drive: Step 1 neighbour-edge math (v0.17.9.14), Step 2 local bounded `EdgeCrossesScreenBound` gated `!s_chaseDriveActive` (v0.17.9.16), Step 3 bggate_6 turnstile via-lane (v0.17.9.16.2); diagnostics gated off at v0.17.9.17 (`FEPIC1_GATE_DIAG`=0, `LINEDIAG_ENABLED`=0). Detail: CHANGELOG v0.17.9.14–.17; DEVNOTES_HISTORY "Track A".

Earlier CLOSED + PUSHED: the **exit-destination interpreter** (`MapjumpResolver::InterpretExitMethod`, `field_archive_jsm_mapjump_resolver.inl`) resolves SCREEN_BOUND exits by forward-walking the JSM under the live game_moment flag. Shipped v0.17.9.6, generalized v0.17.9.11. Diagnostics behind `#define EXIT_TRACE_DIAG 0`.

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
- #42 Main-menu Ability screen TTS (cursor idx 5) — v0.18.1 chapter, ACTIVE this session
- #43 Calibrate remaining GF per-level EXP costs (#41 follow-up) — low-effort, do as GFs are obtained
- (#40 closed as duplicate of #41; #41 + #44 closed completed — GF chapter done)

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
