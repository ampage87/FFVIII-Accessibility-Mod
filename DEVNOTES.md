**Purpose & context**

Aaron is the sole developer of the FF8 Accessibility Mod — a `dinput8.dll` injection for Final Fantasy VIII Steam 2013 (App ID 39150, FF8_EN.exe + FFNx v1.23.x) that makes the game playable for blind players via Windows SAPI TTS, navigation assistance, and entity catalogs. Aaron is blind, uses NVDA, and is also the primary tester.

**Project root:** `C:/Users/ampag/OneDrive/Documents/FFVIII-Accessibility-Mod/FF8_OriginalPC_mod/`
**GitHub:** `ampage87/FFVIII-Accessibility-Mod`. Aaron pushes via `Utilities/push_to_github.ps1`; **Claude never pushes**; diagnostic builds stay LOCAL.

Per-chapter history lives in `CHANGELOG.md` (authoritative, one entry per version) and `DEVNOTES_HISTORY.md` (long-form narratives). This file is the live state + backlog + rules only; it stays under 10 KB.

---

## Where we are at session open

**SESSION IN PROGRESS (0.18.2.x chapter — menu polish + new submenus). LOCAL build = v0.18.2.6, NOT pushed. GitHub HEAD still v0.18.1.13.** Task progress: **(2) refine quantity context — DONE** (v0.18.2.0, BAT-confirmed 2026-06-02 "informs me when the quantity screen appears and what it does"; no GitHub issue, was ad-hoc). **(5) junction non-command abilities — filed #45**, blocked: none of the current GFs teach a character/party ability yet, so can't test. **(1) Junction>Auto submenu — option readout + `/` help BAT-confirmed (v0.18.2.1). Applied-confirmation DONE & BAT-confirmed (v0.18.2.6, 2026-06-02): snapshot (.3) and input-bit (.4) approaches dropped; v0.18.2.5 located the auto-junction routine (0x004BE790) via a HW write-BP, and Aaron confirmed (labelled BAT) it runs on a confirm — including a no-op confirm — but NOT on cancel, so the write-BP is promoted to the live confirm detector. Announce = 'Junctioned automatically for <opt>' on confirm, silent on cancel.** Discovery came free from SUBMON (it auto-runs on submenu entry): Auto submenu = junction focus `+0x22E == 11` (stable while navigating), option cursor `+0x26A` (0=Atk/up Str, 1=Mag, 2=Def/up HP), GCW help tracked it exactly. New `focus==11` branch in `PollJunctionSubmenu` announces terse "Attack"/"Magic"/"Defense" on move; `/` reads each option's help ("Junctions magic to raise Strength/Magic/HP") via a new `JunctionAutoSpeakHelp()` spliced into the GF/Ability `/` chain in `MenuTTS::Update()`. **Confirm vs cancel CANNOT be told apart by focus path — BOTH go 11->8->3.** Dead ends: v0.18.2.3 magic-changed snapshot (silent on no-op confirm); v0.18.2.4 `pEngineInputConfirmedButtons` (menu never sets it — zero [JuncBtnDiag] all session; menu uses a separate input path, same reason the naming bypass faked VK_RETURN). **SOLUTION (v0.18.2.6):** a 1-byte HW WRITE breakpoint (DR3; DR0/1/2 are the battle BPs) on the junction working byte pMenuStateA+0x6C2, armed on Junction activation (JuncAutoBP_Arm in the +0x1E8==17 block), disarmed in ResetJunctionState. The write comes from the game's auto-junction routine at **0x004BE790** (clear-loop ~0x004BE7A1 zeroes the array, accumulate-loop ~0x004BE84F OR-s magic ids in: +0x6C2 went 0->16->18=Blizzard); reached via 0x004BFB40 <- 0x004DA7F0 (the big junction-menu fn). v0.18.2.5 BAT + Aaron's confirm/cancel labelling PROVED: routine runs on confirm (incl no-op confirm) and NOT on cancel — a clean discriminator (silent on all 3 cancels; not a staleness artifact). VEH (now lean) sets s_juncAutoRoutineRan when it fires at focus==11; the action-menu (focus==3) resolution announces 'Junctioned automatically for <opt>' if set, silent if clear; flag cleared on each Auto-submenu entry. MinHook was the fallback if the BP proved unreliable, but it didn't, so no hook needed (and 0x004BE790's signature couldn't be read from chat anyway — 98MB .asm not greppable). Built on battle_tts_dmg_read_bp.inl's pattern. NEXT BAT: Junction>char>action>Auto — confirm (expect announce), re-confirm a no-op (expect announce), cancel (expect silence). Remaining session tasks: (4) main-menu party nav beyond 3 leads [SUBMON discovery]; (3) Items submenu glitch on item use [overlaps #10].

**GitHub HEAD = v0.18.1.13 (`9cba532`) — Ability screen (#42) refine flow COMPLETE, PUSHED & #42 CLOSED.** The whole "Use GF ability" refine flow now speaks end to end (ability list, refinable tag, recipient picker + magic stock, quantity + running total), BAT-confirmed and shipped 2026-06-02. Push committed the milestones v0.18.1.4 (item list) → .7 (2b tag) → .13 (Builds 3/4 + polish + stock) on top of v0.18.1.3 (`af7bf5b`, Build 1). Detail in `CHANGELOG.md` v0.18.1.4–.13 and the block below; minor follow-ups recorded in the #42 closing comment (non-Water `MAGIC_NAMES` entries unverified, status-magic ids 41+ name-only, multi-input recipe totals, renameable names). **NEXT SESSION:** `github:list_issues` for the live backlog; the Ability screen's passive/shop abilities (Haggle, Call Shop, Junk Shop, etc.) are not yet handled if a new issue covers them.

**Ability screen (#42) discovery DONE (BAT 18:45, no rebuild — SUBMON already in HEAD).** The screen is the **"Use GF ability"** action screen (refine etc.), NOT a GF-picker/category/AP-learn flow (issue #42's body is the wrong model — needs rewriting; offered to Aaron). Confirmed offsets vs `pMenuStateA`: gate **`+0x1E8==14`**; phase **`+0x22E`** (3 = ability list, ~19–21 = refine item list); **`+0x258`** = ability-list cursor (0=I Mag-RF,1=Tool-RF); **`+0x2DF`** = item-list cursor; secondary `+0x230`/`+0x5DF`. Names/help/items read from GCW; `GetAbilityName()` covers the ids; no AP here. Full block + first-build plan in `NEXT_SESSION_PROMPT.md`.

**Refine flow (#42) v0.18.1.13 — BAT-CONFIRMED ("worked great", I-Mag RF: "Squall … has 100 Waters" etc. matched the panel). CLEAN BUILD, READY TO PUSH (local, NOT yet pushed — Aaron runs `Utilities/push_to_github.ps1`).** Tool-RF couldn't be tested (no refinable items on hand) but it's N/A: item-producing refines (Tool/Ammo/Med-RF item results) go to inventory with no character recipient, so the recipient-stock announce only applies to the Mag-RF family. v0.18.1.12 BAT confirmed the savemap layout and pinned **Water = spell id 10** (Quistis array `{10,60}`=panel 60, Selphie `{10,40}`=40, Squall `{10,100}`). Recipe ptr `*(+0x2BE)` is just preview TEXT and `0Ccodes` was empty → result magic mapped from its NAME, not a runtime id. **Implemented:** char picker speaks name now (interrupt) then "has N <Magic>" after `ABIL_REFINE_SETTLE_MS` (400 ms), queued (mirrors 2b). `AbilReadRecipStock(charId,sid)` scans the savemap magic array `SAVEMAP_BASE + 0x048C + charId*152 + 0x10` (32 x {id,qty}) for `id==sid`→qty (0 if absent, -1 on fault). `MagicNameToId()` maps the stashed `s_abilYieldMagic` (strips trailing plural 's') via `MAGIC_NAMES[]` (ids 1-40: elemental/GF-tier/healing/support; Water=10 CONFIRMED). Unmapped magic → name only (no wrong number). State: `s_abilRecipSettleAt`/`s_abilRecipStockSpoken` (+reset). `RECIP_STOCK_DIAG`=0 (retired). **On BAT:** hover recipients — expect e.g. "Squall … has 100 Waters", "Quistis … has 60 Waters" matching the panel. Verify against `Logs/ff8_menu.log` `[MenuTTS] Refine recip stock: id=.. sid=10 stock=..` lines. **If counts match → PUSH:** Aaron runs `Utilities/push_to_github.ps1` (Claude NEVER pushes); then verify HEAD via `github:list_commits` and update issue #42. One push carries Builds 2b/3/4 + all fixes (HEAD is still v0.18.1.3 `af7bf5b`). **Caveat:** `MAGIC_NAMES` ids 1-40 only Water is in-game-verified; the rest follow the documented kernel.bin Section 1 order — if any non-Water refine announces a wrong count, fix that table entry. Status magics (41+) intentionally omitted (name-only) until confirmed. Quantity multi-input recipes (X→Y,X>1) total math still unverified; renameable Squall/Rinoa use default names. **Confirmed menu offsets (rel pMenuStateA):** recipient `+0x2DE`=char id / `+0x2E0`=slot; quantity `+0x2E5`=count / `+0x2E4`=owned (lingers); `+0x2E7`=quantity-active (1/0); `+0x2E9`=subphase (255/0). **Savemap:** char structs `+0x048C` (id*152), Magics[32] at `+0x10`; item inv `+0x0B40`. (All external savemap offsets are -0x14 vs the 0x4C header.)

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
