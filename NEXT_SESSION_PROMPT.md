# Next Session Prompt: Main-Menu GF + Ability screen TTS (v0.18.0.x)

## Greeting

Start every response with `## Claude Says`. Read `DEVNOTES.md` and THIS file before any work.

## Where we are at session open

**Local working version = v0.18.0.15 — BAT-CONFIRMED, READY TO PUSH.** GF submenu (#41 + #44) done and verified row-by-row against the F11 screenshot. GitHub HEAD still v0.17.9.17 `808d4802`. **Next action: Aaron runs `Utilities/push_to_github.ps1`, then Claude closes #41 + #44** (verify `github:list_commits` first). Diagnostics off; FF8OPC_VERSION == CHANGELOG == 0.18.0.15. **Active chapter: Main-Menu GF + Ability screen TTS** — tracked as **#41 (GF screen, cursor idx 4)** and **#42 (Ability screen, cursor idx 5)**; close each when it ships. **#44** = key-5 / Learn-list AP readout (v2). **Versioning scheme (Aaron): 0.18.0.x = GF submenu, 0.18.1.x = Ability submenu, 0.18.3+ = other menus.**

Next concrete steps:
1. **GF list name TTS — BAT-CONFIRMED.** Speaks the GF name on cursor move (index `+0x253`, gate `+0x1E8==4`); empty cells say "Empty".
2. **GF detail-screen number keys 1..7 + entry hint + keys 6/7 "Compatibility:" + Q/R name announce — BAT-CONFIRMED through v0.18.0.7.** 1=name, 2="HP X of X", 3=level + "equipped by" (char `+0x58`), 4="EXP to next level X, Current EXP Y", 5=learning ability, 6=compat first-3, 7=compat next-3.
2a. **v0.18.0.8 — BAT-CONFIRMED ("worked great").** Number keys follow the Q/R-displayed GF (`s_gfDetailIdx`, resolved from the GCW header). v0.18.0.7's index read had landed in the diagnostic `GFDiagProbeDetail` by mistake instead of `SpeakGFDetailField`; v0.18.0.8 corrected it.
  *(Editor caution: `gfIdx = ms[0x253]` appears in BOTH GFDiagProbeDetail and SpeakGFDetailField — include trailing context when editing either.)*
3. **Detail screen SOLVED (no memory hunt):** max HP = stored `HPs` (+0x12) == displayed max (confirmed 5 GFs). Level/next via FLAT per-level cost (level=exp/cost+1, next=cost-(exp%cost)). `GF_EXP_PER_LEVEL[16]` holds ONLY confirmed costs: Quez/Shiva/Ifrit/Diablos=500, **Siren=400**; all others 0 = announce EXP only (no guessed level). **CALIBRATE each remaining GF as obtained**: read its level off a detail screenshot, set its `GF_EXP_PER_LEVEL` entry (Brothers next ~disc 1; Eden documented 1000, confirm in-game). The v0.18.0.2 wide search returned 0 hits for all GFs — computed values are transient render-time only; probe removed.
4. **Learn / ability-to-learn list — COMPLETE, readout streamlined v0.18.0.15.** Paginated, NOT filtered, ~11 rows/page; active cursor byte = whichever of `+0x257`(page1)/`+0x258`(page2) just changed. **Row readout = name + AP only** (no help text on cursor move): learned -> "&lt;name&gt;, Learned"; else "&lt;name&gt;, C out of R AP" (one format, even at 0). Empty rows say "Empty Ability Slot". **`/` key (#3, folded into #41): reads ONLY the help description** of the row under the cursor (no name repeat; falls back to name / "Empty Ability Slot"). Override at the `/` dispatch in `MenuTTS::Update`: `GFSpeakSelectedAbilityHelp()` (gf.inl) true on the learn list, else normal help bar. `desc` parse retained for empty-row detection + the `/` stash (`s_gfLearnSel*`). BAT-CONFIRMED v0.18.0.15.
4a. **AP readout (#44) — BAT-CONFIRMED v0.18.0.15 (verified vs F11 screenshot).** On-screen AP is sprite-drawn (NOT in GCW text), so AP comes from baked tables in menu_tts_gf.inl: `ability_ap_cost[116]` (required AP per unified id) + `gf_ability_slots[16][22]` (slot order, maps id->slot for savemap `APs[+0x24]`). Anchor-validated + live-confirmed (Quez learning SumMag+30% id 85 -> slot 2 -> APs[2]=117 / cost 140). Rows "C out of R AP" / "Learned"; key 5 "Learning &lt;name&gt;, C of R AP". Helpers `GFAbilityApCost` / `GFReadAbilityAP`. Caveat: Auto-Haste (id 73, Cerberus-only) cost uncertain — verify when obtained. Display list is id-sorted + a subset; all lookups key off id so order is irrelevant.
5. **Ship the GF submenu — BAT-CONFIRMED v0.18.0.15, READY TO PUSH.** Verified row-by-row vs the F11 screenshot `f11_170221_023.png` (Quez ability list): all 11 rows match name + AP + learned ("Complete!"=="Learned"), Learning panel 117/140 matches key 5. Build is push-ready (version==changelog, diagnostics off). **Next: Aaron runs `push_to_github.ps1`; then Claude verifies `github:list_commits` and closes #41 + #44.** After that the GF submenu chapter is done; next is #42 (Ability screen).
6. **Ability screen (#42):** `menu_tts_ability.inl`, `PollAbilitySubmenu()` on `s_prevCursor == 5`, phase machine (GF-pick → category → ability list). Reuse `GetAbilityName()` + the GF read from #41.

**CONFIRMED GF memory map (v0.18.0/.1 BATs — reuse, do NOT re-derive):**
   - **GF-list cursor**: `pMenuStateA + 0x253` = canonical GF index 0..15. Gate `pMenuStateA + 0x1E8 == 4`.
   - **GF savemap record** (base `SAVEMAP_BASE + 0x4C`, stride `0x44`, 16 records, canonical order): name `+0x00`, **Current EXP u32 `+0x0C`** (Diablos 4000 ✓), **obtained `+0x11`**, **current HP u16 `+0x12`** (730 ✓), complete_abilities[16] `+0x14`, APs[24] `+0x24`, kills `+0x3C`, **learning ability id `+0x40`** (Diablos 0x57=87=GFHP plus 10%). Level NOT stored (computed).
   - **Compatibility** = each CHARACTER's `gf_compat[16]` at char `+0x70` (u16, indexed by GF id). **Display = `(6000 - raw) / 5`** — confirmed exactly: Squall 2756→648, Zell 3132→573, Quistis 3000→600, Selphie 2966→606. Detail panel shows EXISTING chars in model order (Squall0/Zell1/Irvine2/Quistis3/Rinoa4/Selphie5/…); roster grows 4→6 as Rinoa/Irvine join. Char struct: stride `CHAR_STRUCT_SIZE` from `CHARS_OFFSET`, `CHR_EXISTS`/`CHR_MODEL_ID` per menu_tts_diagnostics.inl.
   - **GF detail screen**: single stat panel, no list cursor; detected by GCW containing "Compatibility".
   - **Learn / ability list**: **paginated, NOT filtered, ~11 rows/page**; active cursor byte = whichever just changed (page 1 `+0x257`, page 2 `+0x258`, 0-based into the rendered page). Empty rows below a short last page = cursor in `[count,22)` w/ blank help -> "Empty Ability Slot". AP cur/req per row is v2 (#44). HELP = description.
   - Residual: "Siren" decodes "Siren A" (name-table artifact, both dump + GCW) — trim later.

Reminder — the main-menu GF screen is distinct from the already-done **battle** GF submenu (`battle_tts_menu_*.inl`) and GF summon audio (`gf_audio_desc.cpp`). Don't duplicate.

## Prior chapter (closed + pushed)

**GitHub HEAD = v0.17.9.17** (`808d4802`) — **Track A is COMPLETE & PUSHED** (parent v0.17.9.11 `3478683`). All three F9 auto-drive fixes shipped, diagnostics gated off:
- **Step 1 (v0.17.9.14):** `FindPortal`/`GetSharedEdgeLength`/`EdgeMidpointPath` read the (e,e+1) neighbour-edge vertex pair the .id format stores (was (e+1,e+2), which emitted WALL edges as funnel portals and wedged narrow/rounded fields). Full Dollet chase = 0 catches.
- **Step 2 (v0.17.9.15–.16):** F9 path-finding uses a LOCAL bounded `EdgeCrossesScreenBound` test in the A* screen-bound avoidance, gated on `!s_chaseDriveActive` (chase keeps the global `IsSeparatedByTriggerLine`). Balamb Hotel bcsaka_1 F9-drives to the Town Square exit; chase still 0 catches.
- **Step 3 (v0.17.9.16.2):** bggate_6 front-gate turnstile slot selection — `ComputeAStarPathVia` (2-segment A*+stitch) + a bggate_6-only F9 hook forcing the correct one-way lane's mid-band via tri when the route crosses the turnstile (Y=-667 midline; north→WEST ≈(-1312,-532), south→EAST ≈(-1093,-586)). F9-only; chase byte-identical. BAT: both lanes thread perfectly.
- **v0.17.9.17:** `FEPIC1_GATE_DIAG`=0 (compiles out [GATEDIAG]+[TTRACE]); new `LINEDIAG_ENABLED`=0 toggle (was the always-on [LINEDIAG] loop in `field_nav_fieldscripts.inl`). Both one-line flips to re-enable.

**NEXT = pick a GitHub issue** (#30–#39, migrated this session; see Backlog) — Track A is closed. Full per-step detail: CHANGELOG v0.17.9.14/.15/.16/.16.2/.17; deep narrative in DEVNOTES_HISTORY ("Track A"). The whole bug/task backlog now lives in **GitHub Issues** (#30–#39, migrated 2026-06-01) — see the Backlog section.

Last two chapters, both CLOSED + PUSHED — the **exit-destination interpreter** (`MapjumpResolver::InterpretExitMethod` in `field_archive_jsm_mapjump_resolver.inl`): v0.17.9.6 shipped it for the dorm/corridor SCREEN_BOUND exits; v0.17.9.11 fixed a JPF (conditional-jump) off-by-one (taken target = `ip+param`, k=0; JMP stays k=1) that generalized it to flag-gated switch-on-game_moment exits. Engine-validated vs the live `[MAPJUMP-HOOK]` oracle (bcport_2 'Director' → 120, was a wrong 121; dorm 228/245 unchanged; l1 correctly inactive at game_moment=205). Diagnostics retained behind `#define EXIT_TRACE_DIAG 0` (flip to 1 to re-probe a field). Full per-version detail: `CHANGELOG.md` v0.17.9.6 / v0.17.9.11; one-paragraph recap in `DEVNOTES.md`.

## Backlog (pick with Aaron)

The backlog now lives in **GitHub Issues** on `ampage87/FFVIII-Accessibility-Mod` (not in DEVNOTES/this file). Use `github:list_issues` to pull the live list. Migrated 2026-06-01: **#30** menu_tts T-handler `!shift` gate · **#31** FieldAnnounce display-name audit (0x0134/0x0136 + Fire Cavern A 0x0088) · **#32** field-name populate race (log-only) · **#33** Dollet timer doc · **#34** verify bgryo1_1 'squalls' exit (pre-SeeD) · **#35** verify dotown_2 'Selphie' exit · **#36** F9 gateway bogus driveSkipTrigIdx · **#37** source-size refactor queue · **#38** parked diagnostics (contingency) · **#39** deferred/someday umbrella. Pre-existing related: **#28** Fire Cavern entry trigger, **#21** dialog location-names-as-numbers, plus the open battle/menu-TTS set.

For #34 / #35 the verification needs `LINEDIAG_ENABLED` flipped to 1 in `field_navigation.cpp` (then a local rebuild) and a traversal of the relevant field. **File any new bug as a GitHub issue, not in these docs.**

## Closed / verified — do NOT re-open

- **deploy.bat "Version: SINGLE-PRONGED" version bug — ALREADY FIXED, re-verified 2026-05-31.** `src/deploy.bat` extracts the version with `findstr /B /C:"#define FF8OPC_VERSION "` — the `/B` begin-of-line anchor (v0.15.10.1) matches only the real macro line, and the v0.15.12.0 cleanup moved the inline changelog off that line so no comment false-matches. `ff8_accessibility.h` has exactly one begin-of-line `#define FF8OPC_VERSION`; build logs print the correct version (e.g. "Version: 0.17.9.11"). No code change wanted — editing this would risk regressing working build infrastructure. (DEVNOTES previously listed this as open; that was stale and is now corrected.)
- **Exit-interpreter chapters** (Bug 4 core + the JPF generalization): closed + pushed (v0.17.9.6 `502516c3`, v0.17.9.11 `3478683`). Detail in CHANGELOG.
- **Chapter 5** (SeeD rank R-key + auto salary announcement): closed + pushed v0.17.9.1 (`5c3af6a5`). See the SeeD savemap reference below.
- **Chapters 1–4, autodrive split, DEVNOTES cleanup:** all closed + pushed (CHANGELOG / DEVNOTES_HISTORY).

## SeeD savemap reference (carry-forward — reuse for any future SeeD work)

Confirmed by diffing three of Aaron's decompressed `.ff8` saves and validated live in the Chapter 5 BATs:
- `.ff8` files are FF7/FF8 LZSS-compressed (4-byte LE size header, then the stream; N=4096 F=18 THRESHOLD=2, init-pos 0xFEE, zero-filled buffer). **Live savemap offset X == decompressed-file offset 0x184 + X** (anchored on Squall HP/EXP + Gil + location).
- **SeeD points (experience): +0x0D6C (uint16). Rank = points / 100** (+1 per kill, −10 per salary). Pre-Dollet the pool sits at the base 500 (pre-promotion modifiers deferred to graduation).
- **Salary-payment count: +0x0CDE (uint16)** — +1 per pay but LAGS the chime (updates at next save); 0 pre-SeeD. Do NOT use as a real-time trigger.
- **Steps-since-pay: +0x0D64 (uint16)**, wraps ~24,575; resets to ~0 at payment — the real-time salary trigger, combined with the gil rise.
- **Gameplay Gil: +0x0B08 (uint32).** Header Gil at +0x08, header saveCount at +0x06, location at +0x00.
- Rejected decoy: +0x0D62 reads a plausible small number but is the high word of the u32 step counter at +0x0D60.
- **Savemap header is 76 bytes (0x4C), not 96.** Community/deep-research offsets run +0x14 too high — subtract 0x14. Always include this caveat in any savemap deep-research prompt.
- Accepted residual edge: a freshly-promoted Rank-5 SeeD at exactly 500 points, never paid and never having killed, would briefly hear "No SeeD rank yet". Extremely narrow; revisit only if a clean SeeD-membership flag is ever isolated.

## Session ritual & push-flow reminders

- **"BAT" mid-conversation** always means: read `Logs/build_latest.log` tail for the version + success status, then read the relevant domain log (`ff8_field.log`, `ff8_battle.log`, `ff8_menu.log`, `ff8_world.log`, `ff8_dialog.log`). Read the FULL relevant log, not just the tail — the event may be earlier than the tail. World-map movement is in `ff8_world.log`. Never assume a BAT result without reading the log. `ff8_field.log` is large — bash-grep it from stored tool results.
- **Aaron pushes via `Utilities/push_to_github.ps1`; Claude NEVER pushes.** The utility refuses unless `CHANGELOG.md`'s top `## vX.Y.Z` heading matches `FF8OPC_VERSION`. After a push, verify with `github:list_commits` and update DEVNOTES + this file to the new HEAD.
- **Deploy** is `deploy.vbs` → `src/deploy.ps1` → `src/deploy.bat` (build + copy DLL + write `Logs/build_latest.log`).
- **Version bump = one place:** `FF8OPC_VERSION` in `src/ff8_accessibility.h`, paired with a new top `CHANGELOG.md` entry.
- **Push-size guard:** local CI mirror enforces 80 KB hard limit per `.inl`/`.cpp` at push (60–80 KB is a non-blocking warning). If a push refuses for size, a comment trim is the quick fix; a split refactor is the proper one.
- **F12 is the per-session diagnostic key only** — one at a time; remove any prior F12 diagnostic before adding a new one, and strip (or gate `#define X 0`) diagnostics before a chapter is pushed.
- Update `DEVNOTES.md` + this file at every version bump and after every BAT.
