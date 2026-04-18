# DEVNOTES_HISTORY - FF8 Accessibility Mod Build History Archive
## All detailed build tables, investigation narratives, and per-version test results

> This file is the archaeological record. Consult ONLY when you need to understand
> WHY a past decision was made, or to trace the evolution of a specific feature.

---

## Sessions 75–76 (2026-04-17) — EWM Cap→Freeze + Principle Validation

### v0.13.57 (session 75) — Cap→Freeze + Damage-Anim Diagnostic

**Conceptual change.** Prior versions used a "cap at max-1" sandwich: entities advanced naturally during freeze windows but clamped at max-1. During long holds (GF summons, damage windows) everyone converged at 11999, erasing the natural ATB race. Changed sandwich to true freeze: restore ATB to exact pre-sandwich value, so entities stay at whatever value they had when freeze engaged. Same for GF loading counter.

**Files touched:**
- `src/battle_tts_ewm.inl` — removed cap-at-max-1 logic from POST-FREEZE for both ATB and GF loading counter; new `EWM_FormatATBSnapshot()` + `EWM_PollDiagnostics()` called each frame from `EWM_UpdateBattle`. New log tags: `[DMG-DIAG]`, `[ACT-DIAG]`, `[FRZ-DIAG]`, `[POST-REL]`.

**BAT result:** Aaron reported "I didn't notice any overlap, which was fantastic!" — the TTS-damage-overlap bug that sessions 69–74 chased is **resolved** by switching to freeze semantics + keeping the dispatch hooks from v0.13.55/56.

**Side observation:** Aaron noticed party getting 2–3 turns per enemy turn. Initially thought a bug, but analysis showed this is just 3v1 arithmetic + G-Soldier's slower speed. Both sides' ATB was correctly frozen during each other's actions (verified by ATB snapshots in log). Chose option 1: live with it (matches Wait mode economy).

### v0.13.58 (session 76) — Turn-Counter Diagnostic (first attempt)

**Goal:** Let Aaron A/B test EWM-on vs EWM-off turn ratios empirically to confirm freeze has zero impact on turn economy.

**Implementation:** `EWM_TrackTurnCount()` detects a turn start by watching each slot's ATB transition from high (>10000) to low (<2000). Called from inside `EWM_UpdateBattle`.

**Bug:** `EWM_UpdateBattle` is gated behind `s_initAnnounceDone && s_ewmEnabled && s_ewmHookInstalled`, so the `s_inBattle` transitions the tracker tried to detect weren't reliably visible. Reset-on-battle-start and summary-on-battle-end only fired on the first battle of a session. Counters ran continuously across all subsequent battles.

### v0.13.59 (session 76) — Turn-Counter Lifecycle Fix

**Architectural split.** Broke the tracker into three functions called from three different lifecycle points:
- `EWM_ResetTurnCount()` — called from `OnBattleEnter()` in `battle_tts.cpp`
- `EWM_TrackTurnCount()` — per-frame poll called from `BattleTTS::Update()` gated only on `s_inBattle`
- `EWM_LogTurnCountSummary()` — called from `OnBattleExit()` before `s_inBattle = false`

Each hook now fires exactly when it's supposed to regardless of EWM state or init progress.

**BAT result (10-battle A/B test):**

| | EWM ON | EWM OFF |
|---|---|---|
| Battles (with ≥1 enemy turn) | 3 of 5 | 4 of 4 |
| Party turns (aggregated) | 18 | 21 |
| Enemy turns (aggregated) | 8 | 9 |
| **Party:enemy ratio** | **2.25 : 1** | **2.33 : 1** |

Ratios within 3.5% of each other. 2v2 battles matched exactly (2.25:1 both). 1-enemy single-turn battles all landed at exactly 2.00:1 regardless of EWM state. **Conclusion:** EWM freeze has no measurable effect on turn economy. Principle validated empirically.

### v0.13.60 (session 76) — Turn-Summary Format Cosmetic Fix

**Bug found in BAT log:** Summary line printed `s7=257 (total=0)` — `s7` doesn't exist. `BATTLE_TOTAL_SLOTS=7` means enemy slots are s3–s6 (indices 3, 4, 5, 6). The format string included a spurious `s7` which was an out-of-bounds read of whatever happened to be in the adjacent memory. Ratio math was always correct (loop summed the right indices); only the per-slot display had garbage.

Fixed format string to print s3–s6 only. No behavior change; next battle logs will show clean summary. Ships silent — no further BAT needed.

### Key Learnings from Sessions 75–76

1. **The overlap bug's real root cause** was the cap's "converge at max-1" behavior, not a dispatch race. Dispatch hooks (v0.13.55/56) were necessary defense-in-depth but didn't solve it alone. Switching cap→freeze eliminated the simultaneous-tie-dispatch-at-release condition.

2. **EWM principle crystallized.** Memory #26 updated: "Enhanced Wait Mode retrofits FF8 into sequential turn-based — only ONE action/menu occurs at a time. ATB still races normally; whoever fills first goes first (no advantage, same turn economy as vanilla). During ANY action (menu, attack, GF, damage anim), ALL other ATB freezes. Gameplay-neutral — doesn't add turns or prevent enemy attacks, just sequences them for TTS. Preserve: (1) first-to-fill acts first, (2) no skipped turns, (3) natural ally/enemy ratio."

3. **Diagnostic lifecycle hooks beat inline state machines.** Version 0.13.58 tried to detect battle-enter/exit transitions inside the per-frame tick; v0.13.59 split the work across the battle lifecycle functions that already existed. Cleaner, more reliable, easier to read.

4. **Turn-counter detector is EWM-independent.** ATB high→low watch works whether the freeze sandwich is active or not, so A/B comparisons are apples-to-apples. Reusable for any future turn-economy question.

### v0.13.55–56 Outcome (retrospective)

Keep both dispatch hooks (`sub_483470` + `sub_482F80`) — they're doing their job at the dispatch layer even though the visible-overlap symptom was actually solved by cap→freeze at the ATB layer. The hooks provide defense-in-depth for edge cases (fast enemy attacks during transition-hold windows). Removing them is not planned.

---

## Sessions 69–74 (2026-04-16 → 2026-04-17) — Damage / Command-Menu Overlap Race

**7+ builds across 6 sessions.** Progressive attempts to stop enemy attacks from landing visually during the next player's command menu. Full per-session transcripts at `/mnt/transcripts/`; key decisions summarized:

### v0.13.51 (session 69) — Draw-submenu ATB cap hotfix
Unrelated to overlap bug but shipped same session. Three-edit fix: moved `s_inSubmenu` from menu.inl to hp.inl so ewm.inl can read it; extended `submenuOpen` check to `(menuPhaseDword >= 0x00400000) || s_inSubmenu`. Aaron BAT-confirmed 20:07:43: "It is working for suppressing ATB while moving around menus."

### v0.13.52 (sessions 70–71) — Dual-layer TTS ordering fix
**Layer 1** (ewm.inl): extended damage-TTS-hold block to cover five signals: `s_ewmHoldForDamageTTS`, `s_anyHpPending`, `s_damageAnimWasActive`, `*(uint8_t*)0x01D280C0 != 0`, `*(uint32_t*)0x01D27B00 != 0`. When any active AND `activeChar == 0xFF`, cap all slots with `excludeSlot=0xFF`.
**Layer 2** (menu.inl): added `s_deferredTurnBuf/Pending/Tick` state; turn-announce site defers if damage signals active; new `PollDeferredTurnAnnounce()` wired into `battle_tts.cpp::Update()` after `PollHPChanges`.
Result: TTS ordering correct, but enemy attack still lands visually during menu.

### v0.13.53 (session 72) — Post-turn grace + stale-deferral cancel
Added 1000 ms post-turn grace after activeChar transitions player→0xFF. `s_ewmPrevSeenActiveChar`, `s_ewmPostTurnGraceEnd`, `EWM_POST_TURN_GRACE_MS`. Also: `s_deferredTurnChar` tracks turn at defer time, cancels if activeChar changed. Dropped `ScreenReader::IsSpeaking()` gate in `PollDeferredTurnAnnounce` (caused 4-second-stale announcements during menu nav). Narrower race, still fails for GF-completion case.

### v0.13.54 (session 72) — Post-action cooldown
`EWM_POST_ACTION_COOLDOWN_MS = 500`, tracked via `s_ewmLastSignalTime`. `damageOrActionActive = postTurnGraceActive || anyActiveNow || postActionCooldown`. Narrows the window further but doesn't close it — fast actions where `[0x01D27B00]` flickers between 0 and 1 too briefly for mod thread to observe.

### v0.13.55 (session 73) — **Architectural shift**: MinHook on sub_483470
Cap-based approach abandoned in favor of direct dispatch intercept. `s_blockProcessReady = damageOrActionActive || (activeChar < 3)`. Hook returns early when flag set.
**Critical BAT result**: log showed `[DISPATCH] sub_483470: calls=6 blocks=6 block-flag=1` during bug window — hook caught every call, yet enemy attack still landed. Proved `sub_483470` is not the sole dispatch path.

### v0.13.56 (session 74) — Add sub_482F80 hook, awaiting BAT
Hypothesis: `sub_483470` + `sub_482F80` are a natural pair called back-to-back under the same engine gate. 483470 = queue/player-menu side, 482F80 = execution side. Added second MinHook, plus "passes" counter and always-on dispatch stats log for better diagnosis. See current DEVNOTES.md for full details.

### Cumulative key learnings from this saga
- Cap-based prevention has an unavoidable 16 ms race window at mod-thread poll boundaries
- `[0x01D27B00]` is not a persistent animation flag for fast actions
- Dispatch-layer hooks are the right intervention point (atomic, no race)
- Multi-function dispatch: FF8 appears to split queue processing and action execution across two functions
- `s_blockProcessReady` read is safe with `volatile bool` on x86 (no explicit synchronization needed)

---

## Session 67 (2026-04-16) — GF Submenu Fix + Code Cleanup + Draw Fix (v0.13.49→v0.13.50)

3 builds. Key changes:
1. **GF submenu detection build error**: `wasCommandMenu` undeclared after previous session's cleanup removed it. Fixed stray reference in `sm == 0x00` fallback block.
2. **Code cleanup — `EnterSubmenu()` helper**: Consolidated ~60 lines of duplicated list-building code across 4 detection paths (submenuMode, dword, subCursor, delayed entry) into single 20-line shared function. Removed dead variables: `s_submenuReentryNeeded`, `s_lastMenuPhaseForReentry`, `s_commandMenuPhase`, `s_prevMenuPhaseForSubmenu`. Removed 20-line commented-out phase fallback → 2-line note.
3. **Draw submenu false exit — FIXED**: `EnterSubmenu()` sets `s_prevSubmenuMode = 0x01`, and the submenuMode exit handler detects 0x01→0xFE as an exit. But Draw's internal phase transitions (target → spell list → Stock/Cast) briefly flip submenuMode to 0xFE, causing false exits. Fix: suppressed submenuMode exit when `s_submenuCommandId == 0x16` (Draw). Real Draw exits caught by cmdCursor change handler.
4. **EWM issues identified**: (a) Command menu interrupts damage TTS — need delay after attack animations. (b) Enemy attacks during menu navigation — ATB releasing during submenu transitions.

---

## Session 54 (2026-04-11) — Victory TTS phase detection + encoding fix (v0.13.24–26)

6 builds. Key breakthroughs:
1. **Fixed battle text encoding**: Was using kernel.bin encoding (0x02-0x1B=A-Z) which was WRONG. Battle text uses standard FF8 menu encoding (0x45-0x5E=A-Z, 0x5F-0x78=a-z). Same as enemy names in DecodeFF8String.
2. **Phase-to-text-ID mapping confirmed**: textID 22/23=EXP, 21=Items, 109=GF AP, 121=GF level-up, 127=ability learned. Smart BTXT logging (only log NEW/CHANGED text IDs) + F12 step markers mapped phases precisely.
3. **Phase-based victory TTS built**: BTXT hook sets phase flags → victory thread announces per-phase. GF AP ("GF received 2 AP.") confirmed working. Level-up detection via savemap EXP polling confirmed.
4. **sub_5348E0 NOT called during victory**: Hooked it (E9 JMP from FFNx), zero calls during mode 4. Victory code expands 0x0A control codes inline at 0x4A51F0.
5. **Memory-based TTS pivots corrected**: Aaron flagged twice that TTS must come from text renderer hooks, not memory address dumps. All-at-once memory reads leave blind players pressing through unannounced screens. Added to project memory #25.
6. **Victory screen layout documented from screenshots**: EXP shows Name/Lv/EXP Acquired/Current EXP/Next LEVEL. Items shown one-at-a-time. GF AP/Level-Up/Ability each in center box under "Raising GF" header.

## Session 53 (2026-04-11) — Battle text retrieval function identified (v0.13.14–22)

10 builds. BREAKTHROUGH: sub_47EC70 identified as get_battle_text(text_id) via disassembly. 261 call sites. Signature: looks up 16-bit offset at 0x1CF8B50+id*2, returns ptr to 0x1CF3E48+base+offset. 1774 calls during victory. Text IDs: 11 (per-frame graphics), 22/23 (one-time labels), 29/30/31 (per-frame character rows). Also identified sub_47EA30 (entity names), sub_5348E0 (ctrl code expansion), sub_4A37E0 (text display). tkmenu functions (4BD850/4BD630/4BD920) confirmed ZERO calls during battle — dead code.
> For current state, read `DEVNOTES.md` and `NEXT_SESSION_PROMPT.md`.
>
> **Versioning note**: Builds prior to v0.07.16 used the old format without the
> leading "0." (e.g. "v07.15" instead of "v0.07.15"). All versions in this file
> use the old format since that's what the logs and code said at the time.

---

## v07.xx: Menu TTS + Save Screen (2026-03-16)

### pMenuStateA Region Layout
- +0x00..+0x01: Transient input button flags (0x4000=UP, 0x1000=DOWN, 0x0010=confirm)
- +0x12..+0x19: Per-item animation/state flags (toggle 0x00↔0x40)
- +0x20: Active rendering callback index (per-frame oscillation — NOT cursor)
- +0x1CA: Rendering artifact. +0x1CE: Frame/tick counter.
- +0x1E6: **TOP-LEVEL CURSOR INDEX** (0-10)
- +0x1EC/+0x1ED: Submenu-related state

### Build Table v07.00–v07.15

| Build | Changes | Result |
|-------|---------|--------|
| v07.00 | MENUDIAG: menu_tts.cpp, dump pMenuStateA | pMenuStateA/B are input toggles |
| v07.01 | Per-frame WORD tracking at 20 offsets | +0x20 oscillates every frame |
| v07.02 | TTS using +0x20 as cursor | WRONG — repeats |
| v07.03 | Wide 512-byte scan, 500ms sampling | **FOUND CURSOR at +0x1E6** |
| v07.04 | Clean TTS using +0x1E6 | **WORKING** — all 11 items |
| v07.05 | Save slot entry diagnostic | Save screen leaves mode 6 |
| v07.06 | Global mode tracking | Title→Continue stays mode 1 |
| v07.07 | F12 2048-byte pMenuStateA scan | Only noise — cursor NOT here |
| v07.08 | F12 ff8_win_obj windows scan | Zero changes — not used |
| v07.09 | F12 MDT/GCW call counts | ~90-210 MDT/500ms confirmed |
| v07.10 | F12 GCW text capture | Hex captured, wrong encoding |
| v07.11 | Menu font decoder (sysfnt/Deling) | **CONFIRMED** — all text decodes |
| v07.12 | GCW text parsing "in use: Slot N" | Only fires after pressing X |
| v07.13 | Revised parsing, skip "Checking" | Same problem |
| v07.14 | GCW decode diagnostic | **KEY**: Text identical regardless of cursor |
| v07.15 | F12 4KB memory scan around pMenuStateA | Cursor NOT in pMenuStateA region |

### Save Screen Investigation Details (v07.06–v07.15)

**Save screen text rendering**: Uses standard menu_draw_text / get_character_width pipeline. GCW buffer fills to 1024 bytes/500ms with ~90-210 MDT calls per interval.

**GCW text patterns**: "LoadSlot 1FINAL FANTASY Slot 2FINAL FANTASY GAME FOLDER..." — repeats per frame, IDENTICAL regardless of cursor position.

**Memory scan results (v07.15)**: 4KB around pMenuStateA, 500ms intervals. Only 4 offsets changed: tick counter (-0x8), rendering toggles (+0x1FD, +0x1FE, +0x5E8). None correlated with arrow presses.

**Empirical glyph mapping** (v07.10): A=0x25, F=0x2A, I=0x2D, L=0x30, S=0x37, space=0x00.

---

## v06.xx: Field Navigation — Drive Reliability (2026-03-14 to 2026-03-15)

### Build Table v06.02–v06.22

| Build | Key Change | Result |
|-------|-----------|--------|
| v06.02 | Horizontal wall-parallel fix | Quistis+corridor reachable |
| v06.03 | A* trigger-line exemption for exits | Exits pathfinding works |
| v06.04 | Trigger exemption for events + preserve wp | Recovery doesn't lose waypoints |
| v06.05 | Trigger-safe recovery wiggle | Superseded by v06.06 |
| v06.06 | Edge-midpoint recovery replaces wiggle | Fires correctly, wall-stuck remains |
| v06.07 | Micro-nudge perpendicular to wall | Breaks wall contact, reveals oscillation |
| v06.08 | Overshoot detection + NavLog wiring | Fixes corridor oscillation |
| v06.09 | Stuck grace period + cancel threshold | Garden exploration test |
| v06.10 | CoordSample wired + progress detection | Recovery wp advance bug |
| v06.11 | wpIdx>2 guard attempt | Still too aggressive |
| v06.12 | genuineProgress gate | Fixed recovery escalation |
| v06.13 | PROJDIAG + camera analysis | Confirmed no projection needed |
| v06.14 | Per-field heading calibration | bg2f_1 NPC ARRIVED first time |
| v06.15 | Relaxed stuck thresh, remove arrow cancel | "Better than ever" |
| v06.16 | Simplified recovery pipeline | Clean re-path+nudge cycle |
| v06.17 | Corridor steering + wall bias | Per-tick edge midpoint target |
| v06.18 | Trigger proximity exemption | Target-side lines exempt |
| v06.19 | Narrow corridor bias reduction | Still oscillates |
| v06.20 | Wall bias disabled entirely | Corridors stable |
| v06.21 | Talk radius expansion (2.5×) | Elevator corridor NPC solved |
| v06.22 | Corridor steering trigger check | Final navigation build |

### MAJOR FINDING: Walkmesh = Entity Coordinates (2026-03-15)
.id walkmesh stores X,Y already in entity/screen space. No 3D→2D projection. Confirmed via CoordSample empirical data: mean diff=(14,34) std=(41,71). The .ca camera file is for background rendering only.

### v06.09 Garden Exploration Test Results
- Arrived: bggate_5 cdfield8, bggate_1 transitions (×2)
- Gave up: bg2f_1 NPC (×2), bggate_2 NPC (micro-oscillation)
- Open bugs: micro-oscillation (tri 126↔127), trigger-line crossing during drives

---

## v05.xx: Field Navigation — Entity Catalog + Pathfinding (2026-03-11 to 2026-03-14)

### Build Table v05.47–v05.96

| Build | Key Change | Result |
|-------|-----------|--------|
| v05.47 | SYM names + INF gateways | Working |
| v05.50 | pFieldStateBackgrounds | Working |
| v05.53 | Model-based character names | Quistis correct |
| v05.56 | SETLINE opcode hook | Coords at offset 0x188 |
| v05.61 | Y-axis fix (0x194 not 0x198) | Positions correct |
| v05.64 | A* walkmesh (FF7/PSX inline format) | Paths found, steering broken |
| v05.67 | Inverted heading mapping | First Arrived! |
| v05.70 | Screen filtering via SETLINE | 5 hidden back, 1 front |
| v05.76 | Heading: Y inverted, X direct | Left/right correct |
| v05.84 | Fake gamepad injection | Analog movement confirmed |
| v05.89 | get_key_state hook | **Analog steering works** |
| v05.94 | Vertex dedup + funnel fix | FindPortal succeeds |
| v05.96 | triB center cross product | Portal assignment fixed |

### Entity Classification (bgroom_1)
| Ent | Model | SYM | Actual | Catalog |
|-----|-------|-----|--------|---------|
| ent0 | -1 | Director | Controller | filtered |
| ent1 | 0 | Squall | Squall | player |
| ent2 | 8 | Squall_u | Quistis | "Quistis" |
| ent3-6 | 10-15 | various | NPCs | "NPC" |

### Screen Filtering Architecture
SETLINE trigger lines as screen boundaries. Cross product sign test. Layers: IsSeparatedByTriggerLine → screenFiltered[] → transition/event detection → gateway filtering → crossing detection.

---

## v05.97–v06.01: Navigation Pipeline Overhaul

| Build | Key Change |
|-------|-----------|
| v05.97 | Wall-parallel portal skip (epsilon=5.0) — too aggressive |
| v05.98 | Pre-skip nearby waypoints |
| v05.99 | Corridor-center steering bias — broke bgroom_1 |
| v06.00 | Tightened epsilon to 0.5 |
| v06.01 | New pipeline: wall-parallel skip + agent-radius shrinking + BFS island check |

---

## v0.07.93–v0.07.99: Interactive Objects, Exit Naming, INF Gateways

- **v0.07.99**: SET3 push stack diagnostic. `hasPshmCoords`/`pshmAddrX/Y/Z` added.
- **v0.07.98**: Interactive object classification infrastructure.
- **v0.07.97**: Model≥10 NPC fix (push-before-talk timing).
- **v0.07.95–96**: Exit naming, world map labels, redundant JSM exit suppression.
- **v0.07.93–94**: INF gateway parser (Deling format), dedup, catalog integration.

---

## Pre-v05: Dialog, FMV, Title Screen

### v04.36: Field Dialog TTS
All MES/ASK/AMES/AASK/AMESW/RAMESW opcodes hooked. show_dialog hook for tutorials/thoughts. Naming screen bypassed via enableGF() calls.

### v03.00: FMV Audio Descriptions + Skip
ReadFile EOF hook for FMV skip. WebVTT-timed audio descriptions via SAPI.

### v02.00: Title Screen TTS
Cursor tracking for New Game/Continue/Credits.
