# Vehicle State, Car Position, and Town Entry Mechanics — Deep Research Prompt

## For ChatGPT (Deep Research mode)

---

## Context

I am developing a screen-reader accessibility mod for **Final Fantasy VIII (Steam 2013 release, FF8_EN.exe + FFNx v1.23.x)**. The mod (a `dinput8.dll` injection) provides text-to-speech navigation for blind players. One of its core features is **auto-drive (AD)**: the player selects a destination from a catalog and the mod injects keystrokes to steer them there.

AD currently works for on-foot travel and was just BAT-confirmed working for **rental car driving** as well — a fix to inject the `A` key (scan code 0x1E, NON-extended) continuously alongside the UP arrow restored car acceleration. This was rediscovered from earlier (v0.11.13/v0.11.14) builds via past-conversation search.

I now need to make car AD **robust**. Three things are required, all of which depend on memory addresses I cannot find in existing reverse-engineering documentation.

---

## What I already know (confirmed via runtime testing or prior deep research)

### Confirmed runtime addresses (Steam 2013, US, Build with FFNx)

| Address | Type | Meaning |
|---------|------|---------|
| `0x0203EE80` | DWORD | World-map player X (FOOT character) |
| `0x0203EE84` | DWORD | World-map player Y |
| `0x0203EE88` | DWORD | World-map player Z |
| `0x0203ED02` | WORD | Heading (0 = North, 0–4095 CW) |
| `0x0203ED2C` | WORD | Scene flag (0 = world map, non-0 = field/battle) |
| `0x02040A5E` | BYTE | "Locomotion" byte — see issue below |
| `0x01CFDC5C` | base | Savemap base |
| Savemap header | 76 bytes (0x4C), NOT the 96 bytes (0x60) that some research docs assume |

### Savemap WORLDMAP struct (per prior research, Hyne save editor `SaveData.h`)

Located at savemap + 0x1270 (under the 96-byte-header assumption; subtract 0x14 if applying to our 76-byte-header runtime):

| Offset | Size | Field |
|--------|------|-------|
| +0x00 | 12 | char_pos[6]: X, Z, Y, unk, unk, rotation |
| +0x18 | 12 | ragnarok_pos[6] |
| +0x24 | 12 | bgu_pos[6] (mobile Balamb Garden) |
| +0x30 | 12 | car_pos[6] (rental car) |
| +0x62 | 1 | car_rent (boolean: rental car possessed) |
| +0x6E | 1 | disp_map_config |
| +0x74 | 1 | vehicles_instructions bitfield |

**Important caveat from prior research:** *"PC save format may use a smaller 26-byte WORLDMAP_PC struct omitting position arrays. Use runtime addresses for live data."* — meaning these may exist in the on-disk save file but not at this runtime location, OR they may be at this location but the runtime engine doesn't update them in real time (only at save points).

### Locomotion byte enum (from `github.com/ff8-speedruns/ff8-memory/wmTerrain.md`)

| Value | Meaning |
|-------|---------|
| 0 | Squall on foot |
| 6 | Selphie on foot |
| 31 | Chocobo |
| 32–40 | Various invisible cars (rental, Garden car, Sky Blue Van, Classic, Esthar) |
| 48 | Mobile Balamb Garden |
| 50 | Ragnarok |

### Per-polygon terrain types (wmx.obj, polygon byte 0x0D — confirmed by prior research)

| Value | Terrain |
|-------|---------|
| 0–5 | Forest variants (Galbadia, Trabia, Esthar, Centra, Balamb, Esthar) |
| 6–7 | Plains |
| 8 | Desert |
| 9 | Snow |
| 10 | Beach |
| 27 | Railroad |
| 28 | Road |
| 29 | City/Town/Enterable Area |
| 32–34 | Ocean (shallow / light / dark) |

---

## The problem

### Issue 1: Locomotion byte is unreliable for cars

Diagnostic logging (v0.14.101 build) confirmed: when the player is **demonstrably in a rental car** — confirmed by the game's distinctive engine-running sound and by the car visibly driving on screen — the byte at `0x02040A5E` reads **6 (Selphie on foot)**, not any of the 32–40 car values from the official enum.

Hypothesis: this byte cycles through animation sub-states and is captured in a non-canonical state most of the time, OR it represents the on-foot CHARACTER's animation rather than the active vehicle. The engine clearly tracks "in vehicle" state somewhere else — there is a vehicle-instruction popup, the engine prevents forest entry, the engine plays engine-running audio, the engine force-dismounts the player at certain town triggers, etc.

### Issue 2: Foot position freezes while in the car

The DWORDs at `0x0203EE80/84/88` give the foot character's world-map position. While in the rental car, this position is **frozen** (the foot character is hidden inside the car). Diagnostic logs show 5 stuck-detection windows over 15 seconds with `player=(16031,-26948)` unchanged — yet the car was visibly driving across the world map.

Therefore the **car's actual position is stored at a different runtime address**. AD needs that address to compute distance-to-destination, bearing-to-destination, and arrival detection while in a vehicle.

### Issue 3: Per-location car entry capability

The player has confirmed empirically:
- **Cars CAN enter "Balamb-style" towns** (Balamb Town, Dollet, Deling City, Esthar). Driving the car onto the town's entry trigger causes the engine to **force-dismount the player** and load the field, just like walking in.
- **Cars CANNOT enter "Garden-style" locations** (Balamb Garden, and presumably similar non-town locations). The car **bounces off the walls** of these locations — invisible collision prevents driving onto the trigger.

The mod needs to know which locations admit a car so AD can announce "arrived near [Location]" when a car bounces off a non-car-friendly location, vs. waiting for the engine's natural field transition for car-friendly towns.

---

## Research questions (in priority order)

### Q1: Vehicle state runtime address

What is the live runtime memory address (or pointer chain) of a flag/byte/word that reliably indicates:

(a) whether the player is currently piloting any vehicle (vs. on foot),
(b) which vehicle they are piloting (rental car / story-script car / Garden car / mobile Balamb Garden / Ragnarok / Chocobo).

Candidates to investigate:
- The `vehicles_instructions` bitfield at savemap WORLDMAP struct +0x74 (existing prior research suggests this only tracks "which prompts have been shown," not current state — verify or refute).
- Other bytes in the WORLDMAP struct or in the live world-map state region around `0x0203ED02..0x02040A5E`.
- Any flag set/cleared by the worldmap_with_fog_sub_53FAC0 routine when the player mounts/dismounts a vehicle.
- FFNx canary source (`github.com/julianxhokaxhiu/FFNx`, particularly `src/ff8.h` and `src/ff8_data.cpp`) may contain symbol definitions naming this state.

### Q2: Car position runtime address

What is the live runtime memory address of the rental car's world-map position?

The savemap WORLDMAP struct has `car_pos[6]` at +0x30 (12 bytes: X, Z, Y, unk, unk, rot — uint16 each). Either:
(a) confirm this *runtime* address is `savemap_base + 0x125C + 0x30 = 0x01CFEEE8` (after the 76-byte-header correction subtracting 0x14 from the 0x1270 offset), and verify it updates live during driving, OR
(b) provide the actual runtime address if it's elsewhere.

For full coverage, please also identify analogous addresses for `ragnarok_pos`, `bgu_pos` (mobile Balamb Garden), and any other vehicle position arrays.

### Q3: Per-location car-entry capability

What data source determines which world-map entry triggers admit a car (auto-dismount the player and load the field) vs. which reject the car (collision wall, car bounces off)?

Candidates:
- A per-polygon "ENTERABLE" flag in `wmx.obj` (one prior research note mentioned a texture/collision flag at bit 0x08 named ENTERABLE — confirm or refute).
- A per-trigger-program flag or vehicle predicate in `wmsetus` Section 8 (the field-entry bytecode walked by sub_545EA0). Section 8 has been decoded into 38 programs, each with vehicle-restriction operands like 0x80 = Squall foot, 0x84 = Selphie foot, 0x30 = Garden, 0x31 = Chocobo, 0x32 = Ragnarok. Most clauses use TRIG_VEH_FOOT only — but Aaron's testing shows cars CAN enter Balamb Town, so foot-only clauses aren't the gate.
- A hardcoded list in the engine binary (likely in or near `worldmap_with_fog_sub_53FAC0` or the field-transition handler).
- Some other mechanism (e.g. polygon-type 29 "City/Town/Enterable Area" being the actual gate, with the engine force-dismounting whenever a car drives onto a type-29 polygon adjacent to a Section-8-triggered region).

If hardcoded, please provide the address of the list and (if feasible) decode the location IDs it contains. Otherwise, identify the data source so the mod can read it directly.

---

## Approach guidance

- **Authoritative repos:**
  - `github.com/julianxhokaxhiu/FFNx` (canary branch, `src/ff8.h`, `src/ff8_data.cpp`, `src/ff8/world.cpp`)
  - `github.com/ff8-speedruns/ff8-memory` (especially `wmTerrain.md`, `locomotion.md`, `world-map.md`, `mapId.md`, `locationId.md`)
  - `github.com/myst6re/hyne` (`SaveData.h` for canonical savemap layout)
  - `github.com/Extapathy/OpenFF8` (`memory.h` for typed C structs of the runtime state)
  - The Qhimm Modding Wiki and `wiki.ffrtt.ru/index.php/FF8`

- **Engine functions of likely interest:**
  - `worldmap_with_fog_sub_53FAC0` (per-frame world-map tick)
  - `worldmap_input_update_sub_559240` (input handler)
  - `worldmap_wmset_set_pointers_sub_542DA0` (wmset section setup)
  - `sub_545EA0` (Section 8 trigger program walker)
  - `sub_53FF6E` references at `[0x2036b70]` (the world-map "on-foot" gate)
  - `worldmap_update_steps_sub_6519D0` (step counting — may also handle vehicle position updates)

- **Verification approach:** wherever possible, return the confirmation method (e.g., "FFNx exposes this as `worldMapState->vehicleType` at `vehicleStatePtr + 0x14`, populated by sub_5XXXXX during mount events; verified by reading at offset N during testing"). Speculative answers without verification mechanism are less useful than narrower answers backed by source-code or disassembly references.

- **Format:** structured Markdown with one section per research question. For each address, include the offset relative to the FF8_EN.exe image base (typically `0x00400000`), the absolute runtime address (image base + offset), the data type, the size, and a brief rationale or verification note. Cross-reference any FFNx canary symbol names where available.

- **Out of scope for this prompt:** fuel mechanics. (We have decided to handle fuel separately or by giving the player unlimited fuel if AD turns out to be too indirect.)

---

## Optional confirmation (if you have time)

Confirm or refute the existing prior-research note:

> "PC save format may use a smaller 26-byte WORLDMAP_PC struct omitting position arrays. Use runtime addresses for live data."

If true, the savemap-relative offsets above only describe the on-disk save layout and the runtime addresses must be found independently (which is what this whole prompt is about anyway). If false (i.e., the runtime engine DOES expose the full WORLDMAP struct at savemap + 0x125C with the 76-byte-header correction), then Q2 answers itself and the runtime engine simply maintains the savemap struct live.

---

## What I'll do with the answers

- **Q1 (vehicle state)** drives a `s_currentVehicle` runtime variable that AD reads each frame to choose between foot-mode steering (existing) and car-mode steering (new path with forest avoidance).
- **Q2 (car position)** is read every AD tick to compute distance/bearing to destination, replacing the foot-character position when in a vehicle.
- **Q3 (car-entry gate)** lets AD distinguish "drive into town" (engine handles dismount, AD waits for field-transition) from "drive close to location and announce arrival" (AD detects car bouncing off walls and stops with "arrived near [Location]").

Thanks!
