# World Map Accessibility Deep Research Results
## Date: 2026-04-03
## Source: ChatGPT deep research from prompt `World Map Accessibility deep research prompt for ChatGPT.md`

---

**KEY FINDINGS SUMMARY (for quick reference):**

- **Player position**: Static DWORDs at `0x0203EE80` (X), `0x0203EE84` (Y), `0x0203EE88` (Z) — CONFIRMED by our v0.11.01 diagnostic
- **Heading**: WORD at `0x0203ED02` (0=North, 0–4095 clockwise, 12-bit angle system)
- **Vehicle mode**: BYTE at `0x02040A5E` (locomotion method — need locomotion.md for enum values)
- **Scene flag**: WORD at `0x0203ED2C` (0=world map, 1=field/battle)
- **Map dimensions**: 262,144 × 196,608 units, wrapping torus (32×24 segments × 4×4 blocks × 2,048 units)
- **Town entry triggers**: HARDCODED in engine — must build catalog manually from save states
- **Location names**: wmset Section 32 (FF8 text encoding)
- **Draw points**: wmset Section 35 (block coordinates + magic ID)
- **Region map**: wmset Section 2 (32×24 byte grid = regionID per segment)
- **Savemap worldmap struct**: savemap + 0x1270 (128 bytes) — but PC version may omit position arrays; use runtime addresses instead

---

# Full Research Results

(Original title: "FF8 world map internals for blind-accessible navigation")

## 1. The world map coordinate system spans a wrapping torus

FF8's world map is stored in `wmx.obj` inside `world.fs`. The geometry consists of **835 segments** of exactly **0x9000 (36,864) bytes** each. Segments 1–768 form the playable map arranged in a **32 × 24 grid**. Segments 769–835 are story-variant replacements (stationary Balamb Garden, hidden Esthar, Disc 4 alterations).

Each segment contains **16 blocks** in a 4×4 sub-grid. Every block has a bounding box of **2,048 × 2,048 units**. This gives total dimensions of `32 × 4 × 2048 = 262,144` units horizontally and `24 × 4 × 2048 = 196,608` units vertically. The map **wraps on both axes** — crossing any edge returns you to the opposite side, making the topology a torus. Angles use a **12-bit system (0–4095)** where 0 = North and values increase clockwise. One full rotation = 4096 units, so each unit ≈ 0.088°.

The vertex coordinate system within each block uses **signed int16** values: `(X, -Z, Y, padding)` where Z is negated. The block's origin sits at its upper-left corner. Collision is calculated **only** within the block whose bounding box contains the player.

**The 6-component position arrays** in the savemap worldmap struct (`uint16_t[6]`) represent: **X, Z, Y, unknown, unknown, rotation(0–4095)**. Confirmed by myst6re's Hyne save editor source code (`SaveData.h`).

Runtime coordinates are stored as **32-bit DWORDs** at static addresses (see §15 below), while the savemap stores them as 16-bit values.

**Source:** wiki.ffrtt.ru/index.php/FF8/WorldMap_wmx, github.com/myst6re/hyne SaveData.h. **Confirmed** for Steam 2013.

---

## 2. WMSET.OBJ contains 48 sections governing all world map gameplay

The file `wmsetus.obj` (English) lives in `world.fs`. It begins with a **192-byte header** — 48 × uint32 offsets pointing to each section.

| Section | Contents | Accessibility relevance |
|---------|----------|----------------------|
| **1** | Encounter data supplier — maps (regionID, groundID) → encounter set index | Tells you what enemies spawn here |
| **2** | Region map — 32×24 byte grid (768 bytes), one byte = regionID per segment | **Your continent/region lookup table** |
| **3** | Encounter flags — 1 byte per group (0=roads/no encounters, 2=normal, 3=desert, 12=forest, 128=Islands of Heaven/Hell) | Terrain type classification |
| **4** | Encounter tables — 16 bytes per set, 8 uint16 scene.out IDs | Encounter details |
| **5–6** | Lunar Cry encounter data | Post-Lunar Cry Esthar |
| **7–8** | Road/rail/bridge geometry + scripts | — |
| **14** | Side quest dialog text (Obel Lake, trains, draw points, vehicle controls) | **Vehicle instruction strings** |
| **16** | 3D models for world objects (vehicles, Lunatic Pandora, landmarks) | — |
| **32** | **Location name strings** (Ragnarok destinations, town names) | **Your location name table** |
| **35** | **World map draw points** (block X/Y + magic ID) | **Draw point catalog** |
| **38–42** | Textures (terrain, roads, vehicles) | — |
| **43–48** | AKAO sound/music data | — |

---

## 3. Location entry points are engine-hardcoded, but names come from Section 32

**Town entry trigger coordinates and radii are not stored in wmset.obj or any other parsed data file.** They appear to be hardcoded within the engine executable.

The transition mechanism works through **wm2field.tbl** (located in `main.fs`), which maps world map entry events to field IDs. Its format: **24 bytes per entry** with two 12-byte scenarios each containing `(int16 X, int16 Y, int16 triangleID, uint16 fieldID, uint8 direction, uint8[3] padding)`. The game uses 64 WM dummy fields (`wm00`–`wm71`, field IDs 0–71) as intermediaries.

**Section 32** contains location name strings: Forest, Garden, Garden: Station, Missile Base, Winhill, Edea's House, Seaside Station, City, Airstation, Lunatic Pandora Laboratory, Sorceress Memorial, Tears' Point, etc.

**Practical approach:** Build a hardcoded location catalog by recording coordinates from save states. The **250 location IDs** from ff8-speedruns/ff8-memory `locationId.md` give every named region.

---

## 4. Terrain type is stored per-polygon in byte 13 of each wmx.obj triangle

Each polygon in `wmx.obj` is **16 bytes**. Ground type at **offset 0x0D**. Encounter flags in Section 3 provide indirect classification: `0` = roads, `2` = normal terrain, `3` = desert, `12` = forest, `128` = Islands of Heaven/Hell.

**Walkability per vehicle** determined by engine collision checking ground type. No explicit walkability matrix documented — needs reverse-engineering.

**Critical:** Fetch `wmTerrain.md`, `locomotion.md`, `world-map.md` from `github.com/ff8-speedruns/ff8-memory`.

---

## 5. Vehicle system uses a single locomotion byte

**Locomotion Method** at `FF8_EN.exe+1C40A5E` (1 byte). Exact enum values in `locomotion.md` from ff8-speedruns repo.

Vehicle instruction bitfield at savemap worldmap struct offset `+0x74`: bit0=Car, bit2=BGU, bit3=Chocobo, bit4=Ragnarok. This tracks which prompts were shown, **not the current vehicle**.

---

## 6. Ragnarok autopilot

Activates from zoomed-out world map view while piloting Ragnarok. Destination names in Section 32. Full list: Balamb Garden, Balamb, Dollet, Timber, Galbadia Garden, Deling City, Tomb of the Unknown King, D-District Prison, Galbadia Missile Base, FH, Trabia Garden, Edea's House, White SeeD Ship, Great Salt Lake, Esthar City, Lunatic Pandora Lab, Lunar Gate, Sorceress Memorial, Shumi Village, Winhill, Centra Ruins, Deep Sea Research Center, Cactuar Island, Tears' Point.

**Recommended:** Build own steering system rather than hooking the existing autopilot.

---

## 7. Runtime position at three static DWORD addresses (CONFIRMED)

| Address (abs) | Size | Description |
|---------|------|-------------|
| `0x0203EE80` | DWORD | **World Map Coord X** |
| `0x0203EE84` | DWORD | **World Map Coord Y** |
| `0x0203EE88` | DWORD | **World Map Coord Z** |
| `0x02040A5E` | BYTE | **Locomotion Method** |
| `0x0203ED02` | WORD | **Camera/heading direction** (0=North, 0–4095) |
| `0x0203ED08` | WORD | Camera tilt |
| `0x0203ED2C` | WORD | **Scene flag** (0=world map, 1=field/battle) |
| `0x020409E0` | BYTE | Danger value |

All are static addresses — no pointer chains needed.

---

## 8. Continent regions

| Group ID | Region |
|----------|--------|
| 0 | Trabia |
| 1 | Balamb + FH |
| 2 | Esthar West |
| 3 | Esthar South East + Lab |
| 4 | Mordor + Esthar North |
| 5 | Centra |
| 6 | Galbadia borders |
| 7 | Galbadia middle |
| 255 | Seas |

Region lookup: `segmentX = floor(playerX / 8192)`, `segmentY = floor(playerY / 8192)`, then `Section2[segmentY * 32 + segmentX]`.

No bridges or shallow water crossings — continents fully separated by ocean.

---

## 9–14. (See full research for encounter details, draw points, story gating, mesh structure, text encoding)

Key draw point info: Section 35, 4-byte entries (blockX, blockY, magicID). 128 draw points, all invisible. State tracked in 32 bytes at `0x18FEA4C`.

Story gates: game_moment at `0x18FEAB8`, world map version at `0x18FEAC2` (3=Esthar unlocked).

---

## 15. Complete runtime address reference

### World map position and state
| Address | Size | Type | Description |
|---------|------|------|-------------|
| `+1C3EE80` | 4 | DWORD | World map X |
| `+1C3EE84` | 4 | DWORD | World map Y |
| `+1C3EE88` | 4 | DWORD | World map Z |
| `+1C40A5E` | 1 | BYTE | Locomotion method |
| `+1C3ED02` | 2 | WORD | Camera/heading (0–4095) |
| `+1C3ED08` | 2 | WORD | Camera tilt |
| `+1C3ED2C` | 2 | WORD | Scene flag |
| `+1C409E0` | 1 | BYTE | Danger value |

### Game state
| Address | Size | Type | Description |
|---------|------|------|-------------|
| `+18D2FC0` | 2 | WORD | Current map/field ID |
| `+18FEAB8` | 2 | WORD | Story progress (game_moment) |
| `+18FEAC2` | 1 | BYTE | World map version |
| `+18FEA4C` | 32 | bytes | Draw point states |

### Savemap worldmap struct at savemap + 0x1270 (128 bytes)
| Offset | Size | Field |
|--------|------|-------|
| +0x00 | 12 | char_pos[6]: X, Z, Y, unk, unk, rot |
| +0x0C | 12 | unknown_pos1[6] |
| +0x18 | 12 | ragnarok_pos[6] |
| +0x24 | 12 | bgu_pos[6] |
| +0x30 | 12 | car_pos[6] |
| +0x3C–0x5F | 36 | unknown positions |
| +0x62 | 1 | car_rent |
| +0x6E | 1 | disp_map_config |
| +0x74 | 1 | vehicles_instructions bitfield |
| +0x75 | 1 | pupu_quest |
| +0x76 | 8 | obel_quest[8] |

**NOTE:** PC save format may use a smaller 26-byte WORLDMAP_PC struct omitting position arrays. Use runtime addresses for live data.

---

## Implementation guidance

**Compass bearing:** `bearing = (atan2(dx, dy) * 4096 / 2π - heading + 4096) % 4096`

**Location name popup:** Hook `world_dialog_assign_text_sub_543790` for TTS parity with game's own proximity detection.

**Vehicle change:** Poll locomotion byte at `+1C40A5E` each frame, announce on change.

**Auto-navigation:** Inject directional input via analog stick hooks at `worldmap_input_update_sub_559240`.

**Critical repos to fetch:** `github.com/ff8-speedruns/ff8-memory` (locomotion.md, wmTerrain.md, world-map.md), `github.com/Extapathy/OpenFF8` (memory.h with typed C structs).
