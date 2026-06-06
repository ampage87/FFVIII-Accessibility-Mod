# Deep Research Request: FF8 World Map Entry Trigger Coordinates (v2 — sharpened)

## Why a v2

A previous deep-research pass (`World Map Accessibility deep research results.md`, 2026-04-03) established that town / location entry triggers on the FF8 world map are **not** stored in `wmset.obj` or `wm2field.tbl` as simple `(X, Y, radius, fieldID)` records. The `wm2field.tbl` (24 bytes per entry) is a **post-transition** table — it tells the engine which field to load AFTER the trigger fires, with `(X, Y, triangleID, fieldID, direction)` describing the **destination spawn point inside the field**, not the world-map position where the player crossed the trigger.

The actual world-map → field decision is hardcoded in `FF8_EN.exe` per multiple community references, but its precise structure has not yet been documented in publicly available reverse-engineering. The earlier prompt (v1) asked for a flat trigger table; that framing was wrong. This v2 asks the more accurate question: **how does the engine actually decide, each frame, that the player has entered a location's trigger zone, and what coordinates / radii / regions does that decision use?**

A separate first-party investigation by Claude (2026-05-05) traced the disassembly of two candidate functions and confirmed that neither is the trigger-check function:

- `world_dialog_assign_text_sub_543790` (0x00543790): location-name popup. Reads a 16-byte stride table at `0xC761A0` (this is the location NAME table, not the trigger table) and computes torus-aware distance using constants `0xFFFE0000` / `0x00040000` (= ±131072 wrap on the 262144 X axis). It uses player position read from `[0x0203EE80]`, but as a name-popup proximity check, not for transitioning.
- `sub_543A40` (called 14 times, near 0x00543790): a slot-management utility for the `0xC761A0` table — sets bytes to 0xFF to mark slots vacant. Not the trigger logic.

So the trigger function is somewhere else in `FF8_EN.exe`, and we need to find it.

## What I need

Find the function (or functions) inside `FF8_EN.exe` that, while the player is on the world map (game mode 2 = MODE_WORLDMAP), each frame:

1. Read the player's world-map position (`X` at `0x0203EE80`, `Y` at `0x0203EE84`).
2. Compare against per-location trigger geometry (point + radius, AABB, OBB, polygon, line segment, or whatever the format is).
3. Initiate a transition to a wm dummy field (`wm00`–`wm71`) when the player crosses the trigger.

## What I want as deliverables

Ideally the research returns:

1. **The address(es) of the trigger-check function(s)** — the per-frame proximity check that decides "yes, transition to wm/town now".
2. **The trigger data structure address and format** — where in `FF8_EN.exe` (or `wmset.obj` / another data file) the per-location trigger geometry lives, with byte-level field layout (sizes, offsets, meaning of each field).
3. **The coordinates (and radii / shapes) for every enterable on-foot world-map location.** Both the canonical 26 Ragnarok-autopilot destinations AND additional foot-only entry points like Fire Cavern.
4. **Mapping from each trigger entry to a destination wm dummy field** so we can correlate "(X, Y) trigger → wm dummy field XX → final field ID via wm2field.tbl".
5. If the trigger structure is more complex than a flat table — for example, if there are per-region or per-game_moment overrides, or if the trigger data is loaded from `wmset.obj` Section N — please document the indirection.

If a complete table can't be extracted in one pass, please at least pinpoint the trigger-check function and its disassembly so we can do empirical capture (set a breakpoint or inject a hook to log the trigger comparison values during gameplay).

## Known technical context

### Confirmed runtime addresses (Steam 2013, FF8_EN.exe, image base 0x00400000)

| Address (abs) | Size | Purpose |
|---|---|---|
| `0x0203EE80` | DWORD | Player world map X |
| `0x0203EE84` | DWORD | Player world map Y |
| `0x0203EE88` | DWORD | Player world map Z |
| `0x0203ED02` | WORD | Heading (0–4095, 0=North CW) |
| `0x0203ED2C` | WORD | Scene flag (0=world map, 1=field/battle) |
| `0x02040A5E` | BYTE | Locomotion mode |
| `pGameMode` | WORD | Game mode (resolved at runtime; `MODE_WORLDMAP=2`, `MODE_FIELD=1`) |

### Confirmed coordinate system

- Toroidal world: 262144 × 196608 units (32×24 segments × 4×4 blocks × 2048 units).
- Torus wrap constants: `±0x40000` = ±262144 (X full width); `0xFFFE0000` (-0x20000 sign-extended) = -131072 (half X, used for shortest-path torus distance).
- Heading: 12-bit, 0=North, increments clockwise; 4096 = full circle.
- Runtime player address samples (BAT-validated):
  - At Balamb Garden: `(29941, -30093)` maps to seg(19, 20) on a 32×24 grid.
  - X requires `+131072` offset before division by 8192 to get segment column.
  - Y does not need an offset (negative wraps via torus).

### Confirmed entry / location coordinate samples

These are the canonical Ragnarok-autopilot coordinates from `ff8-speedruns/ff8-memory` (the same dataset whose values match the runtime player-position address). They mark approximate centers of locations, NOT trigger-zone centers. Per the prior deep research, **trigger zones are offset from these centers by ~300–800 units** in some unspecified direction.

| Location | X | Y |
|---|---|---|
| Balamb Garden | 24576 | -29406 |
| Balamb (town) | 13249 | -26779 |
| Dollet | -15639 | -39437 |
| Timber | -22564 | -4867 |
| Galbadia Garden | -37471 | -25062 |
| Deling City | -61806 | -28649 |
| Tomb of the Unknown King | -42471 | -36562 |
| D-District Prison | -55306 | -4841 |
| Galbadia Missile Base | -71695 | -15591 |
| Fisherman's Horizon | 48811 | -1653 |
| Trabia Garden | 48893 | -57979 |
| Edea's House | -23150 | 62853 |
| White SeeD Ship | 4887 | 51285 |
| Great Salt Lake | 49888 | -2683 |
| Esthar City | 57011 | -2295 |
| Lunatic Pandora Lab | 79521 | -9135 |
| Lunar Gate | 88021 | 7865 |
| Sorceress Memorial | 81521 | 11865 |
| Shumi Village | 10362 | -76967 |
| Winhill | -50285 | 6320 |
| Centra Ruins | 6887 | 55285 |
| Deep Sea Research Center | -119138 | 86000 |
| Cactuar Island | 54806 | 62040 |
| Tears' Point | 83021 | 31865 |
| Island Closest to Hell | -105137 | -3802 |
| Island Closest to Heaven | 102251 | -53082 |

Plus Fire Cavern (foot-only Balamb-island dungeon, NOT in the Ragnarok set) — coordinate uncertain; v0.11.11 wmx.obj polygon analysis suggested approximately `(36864, -28672)` but this hasn't been validated against an actual entry.

### Functions of interest (confirmed addresses, prior investigation)

- **`worldmap_input_update_sub_559240`** (0x00559240): main world-map per-frame update. Called 4 times per frame per the `callxrefs.txt`. Likely the parent of the trigger-check call.
- **`worldmap_wmset_set_pointers_sub_542DA0`** (0x00542DA0): resolves wmset.obj section pointers at startup. The relative-offset table at `0x01E9DC3C` is base-relative — `[eax*4 + 0x01E9DC3C]` indexes into it. **The 48 wmset section pointers should resolve through this; identifying the section that holds trigger data (if any) would be a major win.**
- **`world_dialog_assign_text_sub_543790`** (0x00543790): location name popup proximity check (NOT the trigger check, but does similar math against `0xC761A0` table).
- **`sub_543A40`**: slot management for the `0xC761A0` name table. Not relevant to triggers.

### Files attached / referenced

The full FF8_EN.exe disassembly is available split across these files:

```
FF8_EN_.text_0x00401000.asm   (0x00401000 – 0x00501000)
FF8_EN_.text_0x00501000.asm   (0x00501000 – 0x00601000)  ← contains both 0x00543790 and 0x00559240
FF8_EN_.text_0x00601000.asm   (0x00601000 – 0x00701000)
FF8_EN_.text_0x00701000.asm   (0x00701000 – 0x00801000)
FF8_EN_.text_0x00801000.asm   (0x00801000 – 0x00901000)
FF8_EN_.text_0x00901000.asm   (0x00901000 – 0x00A01000)
FF8_EN_.text_0x00A01000.asm   (0x00A01000 – 0x00B01000)
FF8_EN_.text_0x00B01000.asm   (0x00B01000 – 0x00B69000)
FF8_EN_sections.txt           (PE section layout)
FF8_EN_functions.txt          (8390-entry function index)
FF8_EN_callxrefs.txt          (call cross-references)
FF8_EN_strings.txt            (213,396 ASCII strings with addresses)
FF8_EN_strings_condensed.txt  (1021 useful strings)
```

### Suggested investigation paths

These are hypotheses based on the prior investigation — not confirmed. Please follow whichever lead pans out, or alternative leads.

#### Path 1 — Trace from `sub_559240`

The world-map main update calls multiple subroutines per frame. One of them is the trigger check. Disassemble `sub_559240` and follow each call target. Look specifically for any sub-call that:

- Reads `[0x0203EE80]` (player X) or `[0x0203EE84]` (player Y).
- Has a comparison with a constant or table value followed by a conditional jump to "set field transition state".
- Uses torus-aware distance (look for the `0xFFFE0000` / `0x40000` constants).

#### Path 2 — Find writes to the field-transition state

When the trigger fires, something writes the destination field ID into the engine's "go to this field" state. Search for writes to the candidate addresses for "next field ID" and trace backward to the trigger-check function. The current map/field ID is at `0x018D2FC0`; the engine likely has a "next/pending field ID" address near there. Finding the function that writes to that address while in MODE_WORLDMAP gets us to the trigger check.

#### Path 3 — wmset.obj sections beyond the documented 48

The prior research catalogs sections 1–8, 14, 16, 32, 35, 38–48 of `wmsetus.obj`. Sections 9–13, 15, 17–31, 33–34, 36–37 are unaccounted for. **One of these may be the trigger geometry table.** If `sub_542DA0` resolves a section pointer that gets dereferenced inside `sub_559240` for a trigger check, that's the section.

#### Path 4 — Per-region region-list approach

The world map is divided into 9 regions (group IDs 0–7 + 255 for seas) per `wmset` Section 2. The trigger check might be region-scoped: look up `regionID = Section2[segY*32 + segX]`, then iterate only the triggers within that region. If so, the trigger data structure may be organized as a per-region linked list or array, not a flat global table.

#### Path 5 — wm dummy fields are the entry mechanism

There are 64 dummy fields `wm00`–`wm71` (field IDs 0–71). Each one is the intermediate stage of "left world map, entering this location." The engine must have a mapping from `(world map X, world map Y, currentRegion, currentVehicle)` to `wmField ID`. Finding that mapping IS the trigger table. Searching for code that writes a small integer (0–71) to the field-ID state while in MODE_WORLDMAP is one heuristic.

### Coordinate conventions reminder

All coordinates in this prompt are in the **runtime / Ragnarok-autopilot / ff8-speedruns** coordinate system, which matches the values at `[0x0203EE80]` / `[0x0203EE84]` directly (no conversion needed). Some prior community research uses a different coordinate system (positive-Y, magnitudes shifted by ~50000 units) — that is the **savemap** coordinate system, which is mostly irrelevant to runtime trigger checks. Stick with the runtime addresses.

### Out of scope

- Battle / field encounter logic.
- Random encounter trigger zones (different mechanism, in `wmset` Section 1 / Section 4).
- Draw point trigger zones (also a different mechanism, `wmset` Section 35).
- Ragnarok autopilot landing logic — that uses a wholly separate "fly to coords" mechanism that doesn't go through the on-foot trigger check.
- Story-gated overrides (e.g., Esthar locked until Disc 3) — useful context but not required for a first-pass trigger table.

### Ideal output structure

```
1. TRIGGER-CHECK FUNCTION
   Address:       0x00XXXXXX
   Function name: <descriptive>
   Called from:   <parent>, ~N times per frame
   Disassembly:   <annotated key bytes>
   Behavior:      <one-paragraph plain English>

2. TRIGGER DATA STRUCTURE
   Base address:  <data address or wmset section number>
   Entry size:    N bytes
   Entry count:   N
   Field layout:
     +0x00 (N bytes): <field>
     ...
   Per-region indirection: <yes/no, details>

3. PER-LOCATION TABLE
   For each enterable location:
     Name: <Balamb / Balamb Garden / etc.>
     Trigger center X: <int32>
     Trigger center Y: <int32>
     Trigger radius / shape: <radius int / AABB / polygon>
     Destination wm dummy field: <wm00..wm71>
     Story / region gate: <if any>

4. SAVEMAP OFFSET REMINDER
   Confirmed savemap header is 76 bytes (0x4C), NOT 96 bytes (0x60).
   Subtract 0x14 from any post-header savemap offset cited in older
   community research.

5. UNRESOLVED ITEMS
   Any locations whose trigger data couldn't be confirmed (e.g., Disc-4
   alternative entries, story-locked transitions).
```

If a complete per-location table isn't recoverable, at minimum return parts 1, 2, and 4. The accessibility mod will then run an empirical-capture session to fill in part 3 from real gameplay using the trigger-check function address as a hook target.

## Why this matters

Without canonical trigger coordinates, an accessibility mod for blind players has to either (a) approximate by walking the player to the autopilot center coordinate and sweep-search for the entrance trigger by alternating turn-and-walk, or (b) ship a manually-curated catalog that's inevitably incomplete for foot-only locations. Both approaches are inferior to direct trigger-zone targeting using the engine's own data. The mod's user is blind and cannot spot a town's entrance visually — exact entrance coordinates are the only way to make navigation reliable.
