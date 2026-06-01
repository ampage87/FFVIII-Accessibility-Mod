**Purpose & context**

Aaron is the sole developer of the FF8 Accessibility Mod — a `dinput8.dll` injection for Final Fantasy VIII Steam 2013 (App ID 39150, FF8_EN.exe + FFNx v1.23.x) that makes the game playable for blind players via Windows SAPI TTS, navigation assistance, and entity catalogs. Aaron is blind, uses NVDA, and is also the primary tester.

**Project root:** `C:/Users/ampag/OneDrive/Documents/FFVIII-Accessibility-Mod/FF8_OriginalPC_mod/`
**GitHub:** `ampage87/FFVIII-Accessibility-Mod`. Aaron pushes via `Utilities/push_to_github.ps1`; **Claude never pushes**; diagnostic builds stay LOCAL.

Per-chapter history lives in `CHANGELOG.md` (authoritative, one entry per version) and `DEVNOTES_HISTORY.md` (long-form narratives). This file is the live state + backlog + rules only; it stays under 10 KB.

---

## Where we are at session open

**Local working version = v0.18.0.15 — BAT-CONFIRMED, READY TO PUSH.** The whole GF submenu (#41 + #44) is done and verified. GitHub HEAD still v0.17.9.17 `808d4802`; **next action is Aaron running `Utilities/push_to_github.ps1`** (Claude never pushes), then close #41 + #44. Verified v0.18.0.15 row-by-row against the F11 screenshot `f11_170221_023.png` (Quezacotl ability list): all 11 rows match name + AP + learned status (screen "Complete!" == TTS "Learned"), and the Learning panel "SumMag+30% 117/140" matches detail key 5. Diagnostics off (GF_DIAG=0, GF_AP_DIAG=0); FF8OPC_VERSION == CHANGELOG top == 0.18.0.15. **Active chapter: Main-Menu GF + Ability TTS** (#41 GF, #42 Ability, #44 key-5/Learn AP). **Versioning: 0.18.0.x = GF submenu, 0.18.1.x = Ability submenu, 0.18.3+ = other menus.**

**BAT-CONFIRMED through v0.18.0.8** ("worked great"): GF list name announce; detail-panel number keys 1..7 (1=name, 2="HP X of X", 3=level + "equipped by" via char `+0x58`, 4="EXP to next level X, Current EXP Y", 5=learning ability NAME, 6=compat first-3, 7=compat next-3); entry hint; Q/R cycles the displayed GF and both the announce and keys 1..7 follow it. Displayed GF resolved from the GCW header (`MatchDetailGFIndex` -> `s_gfDetailIdx`); Q/R does NOT move grid cursor `+0x253`, and the displayed index is not in the 0x1C0..0x2C0 band.

**Detail-screen mechanics (all confirmed):** Max HP = stored HPs u16 (+0x12) == displayed max. Level/next-EXP = flat per-level cost (level=exp/cost+1, next=cost-exp%cost); `GF_EXP_PER_LEVEL[16]` holds only confirmed costs (Quez/Shiva/Ifrit/Diablos 500, Siren 400, rest 0 -> EXP-only, no guessed level). Compat display=(6000-raw)/5, raw u16 @ char `+0x70+gfIdx*2`. Computed values are render-time only (wide pMenuStateA search = 0 hits; that probe removed). `GF_DIAG` = 0; `GF_AP_DIAG` = 0 (AP probe resolved — AP is sprite-drawn, baked table used).

**Learn list (#41/#44) — COMPLETE + BAT-CONFIRMED v0.18.0.15.** Paginated, NOT filtered, ~11 rows/page; active cursor byte = whichever of `+0x257`(page1)/`+0x258`(page2) just changed (0-based into the rendered page). **Row readout = name + AP only** (no help text on cursor move): learned -> "&lt;name&gt;, Learned"; else "&lt;name&gt;, C out of R AP" (one format, even at 0). Empty rows say "Empty Ability Slot". The `desc` parse is retained (drives empty-row detection + the `/` stash) but no longer spoken on move. **`/` key (#3): reads ONLY the help description** of the row under the cursor (no name repeat; falls back to name / "Empty Ability Slot" if no desc) — distinct from detail key 5 (ability being *learned*). Single override at the `/` dispatch in `MenuTTS::Update` (`GFSpeakSelectedAbilityHelp()` true on the learn list, else normal help bar).

**AP readout (#44) — COMPLETE + BAT-CONFIRMED v0.18.0.15 (verified vs F11 screenshot).** On-screen AP is sprite-drawn (NOT in GCW text), so AP is computed from baked tables, not scraped. `ability_ap_cost[116]` (required AP per unified id, per-ability constant) + `gf_ability_slots[16][22]` (each GF's slot order, used only to map id->slot for the savemap `APs[+0x24]` read), both in menu_tts_gf.inl. Anchor-validated, live-confirmed (Quez learning SumMag+30% id 85 -> slot 2 -> APs[2]=117 / cost 140), and the v0.18.0.15 BAT matched all 11 Quez rows + the Learning panel against `f11_170221_023.png`. **Learn rows:** "&lt;name&gt;, C out of R AP" / "&lt;name&gt;, Learned". **Detail key 5:** "Learning &lt;name&gt;, C of R AP". Helpers `GFAbilityApCost(id)` / `GFReadAbilityAP(gf,id,*cur)`. `GF_AP_DIAG`=0. Caveat: Auto-Haste (id 73) cost uncertain (Hyne 250 vs FFWiki 150), Cerberus-only, verify in-game. **Key insight (don't re-derive):** the displayed learn list is SORTED by ability id and is a SUBSET of the 22 slots; current AP must be read as APs[slot-of-id] (slot from gf_ability_slots), NOT APs[id].

**GitHub HEAD = v0.17.9.17** (`808d4802`) — **Track A COMPLETE & PUSHED** (parent v0.17.9.11 `3478683`). F9 auto-drive: Step 1 neighbour-edge math (v0.17.9.14), Step 2 local bounded `EdgeCrossesScreenBound` gated `!s_chaseDriveActive` (v0.17.9.16), Step 3 bggate_6 turnstile via-lane (v0.17.9.16.2); diagnostics gated off at v0.17.9.17 (`FEPIC1_GATE_DIAG`=0, `LINEDIAG_ENABLED`=0). Detail: CHANGELOG v0.17.9.14–.17; DEVNOTES_HISTORY "Track A".

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
- #41 Main-menu GF screen TTS (cursor idx 4) — v0.18.0 chapter
- #42 Main-menu Ability screen TTS (cursor idx 5) — v0.18.0 chapter

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
