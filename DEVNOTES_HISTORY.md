# DEVNOTES_HISTORY - FF8 Accessibility Mod Build History Archive
## All detailed build tables, investigation narratives, and per-version test results

> This file is the archaeological record. Consult ONLY when you need to understand
> WHY a past decision was made, or to trace the evolution of a specific feature.

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
