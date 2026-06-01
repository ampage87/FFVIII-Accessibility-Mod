# Next Session Prompt: pick the next backlog item

## Greeting

Start every response with `## Claude Says`. Read `DEVNOTES.md` and THIS file before any work.

## Where we are at session open

**GitHub HEAD = v0.17.9.11** (`3478683`); local source = **v0.17.9.17**, the push candidate for all of Track A (nothing pushed yet). **Track A is COMPLETE — all three F9 auto-drive fixes BAT-passed, and the per-session diagnostics are now gated off for push:**
- **Step 1 (v0.17.9.14):** `FindPortal`/`GetSharedEdgeLength`/`EdgeMidpointPath` read the (e,e+1) neighbour-edge vertex pair the .id format stores (was (e+1,e+2), which emitted WALL edges as funnel portals and wedged narrow/rounded fields). Full Dollet chase = 0 catches.
- **Step 2 (v0.17.9.15–.16):** F9 path-finding uses a LOCAL bounded `EdgeCrossesScreenBound` test in the A* screen-bound avoidance, gated on `!s_chaseDriveActive` (chase keeps the global `IsSeparatedByTriggerLine`). Balamb Hotel bcsaka_1 F9-drives to the Town Square exit; chase still 0 catches.
- **Step 3 (v0.17.9.16.2):** bggate_6 front-gate turnstile slot selection — `ComputeAStarPathVia` (2-segment A*+stitch) + a bggate_6-only F9 hook forcing the correct one-way lane's mid-band via tri when the route crosses the turnstile (Y=-667 midline; north→WEST ≈(-1312,-532), south→EAST ≈(-1093,-586)). F9-only; chase byte-identical. BAT: both lanes thread perfectly.
- **v0.17.9.17:** `FEPIC1_GATE_DIAG`=0 (compiles out [GATEDIAG]+[TTRACE]); new `LINEDIAG_ENABLED`=0 toggle (was the always-on [LINEDIAG] loop in `field_nav_fieldscripts.inl`). Both one-line flips to re-enable.

**NEXT = Aaron deploys v0.17.9.17 to confirm a clean diagnostics-off build, then runs `Utilities/push_to_github.ps1` to push Steps 1+2+3** (Claude never pushes). Full per-step detail: CHANGELOG v0.17.9.14/.15/.16/.16.2/.17; deep narrative in DEVNOTES_HISTORY ("Track A"). The whole bug/task backlog now lives in **GitHub Issues** (#30–#39, migrated 2026-06-01) — see the Backlog section.

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
