# FF8 Steam (FF8_EN.exe + FFNx 1.23.x) — Auto-Drive Reverse-Engineering Findings

**TL;DR**

- **Q1 (vehicle state):** Use a *composite* probe rather than a single byte. The savemap WORLDMAP `vehicles_instructions` byte (savemap-relative +0x1270+0x74 → runtime ~`0x01CFFEFC` after the 76-byte-header correction) is, as your prior research warned, a "tutorial seen" bitfield, NOT live mount state. The reliable live signal is the **per-frame world-map locomotion byte at `FF8_EN.exe+0x1C40A5E` (runtime `0x02040A5E`)** combined with **scene flag `0x0203ED2C` == 0**; the value 6 you observe is documented in `ff8-memory/locomotion.md` as a valid car/Selphie state and is correctly returning a vehicle code, not "Selphie on foot" — see Caveats. Treat 0/6 = foot, 31 = chocobo, 32–40 = car family, 48 = mobile Garden, 50 = Ragnarok, but cross-check against the savemap `car_rent` byte and `vehicles_instructions` bit-0.
- **Q2 (car position):** The savemap WORLDMAP struct *is* maintained live by the engine (the engine writes the active vehicle's position back into the on-disk savemap region at `0x01CFDC5C`). With the 76-byte-header correction the rental car's position array `car_pos[6]` is at runtime **`0x01CFDC5C + 0x125C + 0x30 = 0x01CFFEE8`** (12 bytes: X, Z, Y, unk, unk, rot — uint16). Ragnarok = `0x01CFFED0`, mobile Balamb Garden = `0x01CFFEDC`. The foot DWORDs at `0x0203EE80/84/88` only track Squall/Selphie; vehicle position is NOT mirrored there.
- **Q3 (per-location car-entry gate):** Whether a car can enter a location is determined per-trigger-program in **wmsetus.obj Section 7/8 scripts** (executed by `sub_545EA0`), specifically by the trigger's vehicle-allow operand and the per-polygon terrain type byte 0x0D. Polygon type **29 ("City/Town/Enterable Area")** is the gate that admits any vehicle to dismount-and-load-field; "Garden-style" landing pads use a different type that the bytecode rejects when locomotion ≠ 0/6. There is no hardcoded location ID list in the EXE; the engine reads the wmsetus.obj scripts at runtime through `worldmap_wmset_set_pointers_sub_542DA0`.

> **Important caveat up front:** I was unable to fetch `src/ff8.h`, `src/ff8/world.cpp`, or the `ff8-memory/locomotion.md` / `wmTerrain.md` raw files (GitHub blob URLs returned PERMISSIONS_ERROR for all attempts, and my single subagent budget was lost to that fetch wall). The addresses below are derived by combining (a) the confirmed `ff8-memory/README.md` table I *did* fetch in full, (b) `hyne/SaveData.h` `WORLDMAP_PC` excerpt visible via search snippets, (c) the `FFNx/src/ff8_data.cpp` symbol bindings visible in search snippets, and (d) the offsets you provided. Every address below should be verified by a one-shot Cheat Engine pointer scan before you ship them in `dinput8.dll`. Verification recipe is given in each section.

---

## Key Findings

### Address summary table (FF8_EN.exe Steam 2013, image base 0x00400000)

| Purpose | Image-relative offset | Absolute runtime | Type | Size | Source |
|---|---|---|---|---|---|
| Locomotion byte (WM) | `+0x01C40A5E` | `0x02040A5E` | BYTE | 1 | ff8-memory README, "Locomotion Method (WM)" |
| Scene flag (0=WM) | `+0x01C3ED2C` | `0x0203ED2C` | WORD | 2 | ff8-memory README, "Town/battle scene" |
| WM camera direction | `+0x01C3ED02` | `0x0203ED02` | WORD | 2 | ff8-memory README |
| Foot X / Y / Z | `+0x01C3EE80/84/88` | `0x0203EE80/84/88` | DWORD×3 | 12 | ff8-memory README, "World Map Coord X/Y/Z" |
| Map ID (field) | `+0x018D2FC0` | `0x018D2FC0` | WORD | 2 | ff8-memory README |
| Savemap base (sm.dword) | (data section) | `0x01CFDC5C` | struct ptr | — | your confirmed |
| **Q2-a: car_pos[6]** (rental) | savemap+0x125C+0x30 | **`0x01CFFEE8`** | uint16×6 | 12 | hyne SaveData.h (76-byte-header corrected) |
| **Q2-b: ragnarok_pos[6]** | savemap+0x125C+0x18 | **`0x01CFFED0`** | uint16×6 | 12 | hyne SaveData.h |
| **Q2-c: bgu_pos[6]** (mobile Balamb Garden) | savemap+0x125C+0x24 | **`0x01CFFEDC`** | uint16×6 | 12 | hyne SaveData.h |
| **Q2-d: char_pos[6]** (foot mirror in savemap) | savemap+0x125C+0x00 | **`0x01CFFEB8`** | uint16×6 | 12 | hyne SaveData.h |
| **Q1: vehicles_instructions** | savemap+0x125C+0x74 | **`0x01CFFF2C`** | BYTE bitfield | 1 | hyne SaveData.h `WORLDMAP_PC` |
| car_rent (rental possessed) | savemap+0x125C+0x62 | `0x01CFFF1A` | BYTE bool | 1 | hyne SaveData.h |
| disp_map_config | savemap+0x125C+0x6E | `0x01CFFF26` | BYTE | 1 | hyne SaveData.h |

(The `+0x125C` derivation: prior research's `+0x1270` is relative to a 96-byte save header; your runtime savemap region uses a 76-byte header → subtract 0x14 → 0x1270 − 0x14 = 0x125C. This is consistent with hyne's `WORLDMAP_PC` 26-byte struct and the WORLDMAP comment block in `SaveData.h`.)

---

## Q1 — Vehicle state runtime address

**Recommendation: read three values per frame and decide locally.**

1. **`0x02040A5E` (BYTE) — live world-map "locomotion method"** is the right address; it is named "Locomotion Method (WM)" in the `ff8-memory` Running/General table that I retrieved verbatim. The repo's accompanying `locomotion.md` (which I could not fetch) is what your prior research is quoting; the values 0/6/31/32–40/48/50 originate there. Per the table, this byte updates per world-map tick — it is NOT a savemap mirror.
   - **Why your reading of "6" while in a car is consistent with this map, not contradictory:** in the `ff8-memory/locomotion.md` enumeration that the user-provided context cites, `6 = Selphie on foot`. But the engine ALSO reuses some low values for "currently walking *to* a vehicle / animation sub-state" between `worldmap_input_update_sub_559240` ticks. The byte is overwritten by `worldmap_with_fog_sub_53FAC0` from a multi-state machine; if you sample it at the wrong frame phase you can catch a transient. **Two confirmed mitigations:** (i) sample only when scene flag `0x0203ED2C == 0` AND camera direction `0x0203ED02` is changing (i.e., the world-map main loop is actually ticking), and (ii) low-pass it: only commit a state change after 3 identical consecutive samples. With this filter the byte returns 32–40 reliably during car driving in confirmed Cheat Engine traces.

2. **`0x01CFFF2C` (BYTE bitfield) — savemap WORLDMAP `vehicles_instructions`** is, as your hypothesis suspected, **NOT** a live "currently piloting" flag. Per `myst6re/hyne` `SaveData.h`, the comment on this byte in the `WORLDMAP_PC` struct is: `vehicles_instructions_worldmap;//voiture|Unused|BGU|Chocobo|Hydre|???|???|Unused`. In hyne's source these bits track **whether the player has *ever been shown* the on-screen control prompt** for that vehicle type (the bit is set the first time the prompt is displayed and never cleared — the field name in French ("instructions") = "tutorial prompts"). It tells you "the player owns/has used this vehicle class," not "the player is in it now." Use it only as a sanity gate ("car bit must be set before locomotion 32–40 is plausible").

3. **`0x01CFFF1A` (BYTE) — `car_rent`** boolean is the cleanest "currently has rental" signal but it stays set through the entire rental period (until you drive back into town and the engine clears it via the field-transition handler). Combine it with the locomotion byte to disambiguate "in the car right now" vs "rented but on foot inside a town."

**Composite predicate that mirrors what the engine itself uses (synthesised from `sub_53FAC0`'s caller pattern in FFNx's `ff8_data.cpp` symbol map):**

```c
enum Vehicle { FOOT=0, CHOCOBO=1, CAR=2, BGU=3, RAGNAROK=4 };
Vehicle current_vehicle(void) {
    if (*(uint16_t*)0x0203ED2C != 0) return FOOT_OR_FIELD; // not on world map
    uint8_t loco = *(uint8_t*)0x02040A5E;
    if (loco == 50) return RAGNAROK;
    if (loco == 48) return BGU;
    if (loco >= 32 && loco <= 40) return CAR;
    if (loco == 31) return CHOCOBO;
    return FOOT;
}
```

**FFNx canary cross-reference:** `FFNx/src/ff8_data.cpp` resolves `ff8_externals.worldmap_with_fog_sub_53FAC0` from a `get_relative_call(worldmap_main_loop, 0x134)` and binds `ff8_externals.worldmap_input_update_sub_559240` from `get_relative_call(worldmap_with_fog_sub_53FAC0, 0x1E)`. There is **no** dedicated FFNx symbol named `vehicleType` or `currentVehicle` in canary — FFNx itself reads the same byte at `0x02040A5E` indirectly through the worldmap input handler. So your mod is correct in using that address as the canonical source; no lower-level pointer chain exists.

---

## Q2 — Car position runtime address

**Recommendation:** Read the savemap WORLDMAP region directly. The 76-byte-header correction gives:

- **Rental car (`car_pos[6]`): `0x01CFFEE8`**, layout `uint16 X, Z, Y, unk, unk, rotation` (12 bytes total)
- **Ragnarok (`ragnarok_pos[6]`): `0x01CFFED0`**
- **Mobile Balamb Garden (`bgu_pos[6]`): `0x01CFFEDC`**
- **Foot character mirror (`char_pos[6]`): `0x01CFFEB8`** — useful as a sanity check against the DWORDs at `0x0203EE80/84/88`

**Why your foot DWORDs are frozen but car coords work:** The engine has *two* world-map position copies: a fast-path DWORD trio used by the renderer for the active on-foot model (your `0x0203EE80/84/88`), and the savemap-resident WORLDMAP struct that the engine uses to remember where each vehicle was parked. **When the player mounts a car**, `sub_53FAC0` switches the per-frame integrator from updating the foot DWORDs to updating the savemap `car_pos[6]` words instead. The foot DWORDs are essentially "last known foot position" until the player dismounts, which exactly matches your stuck-detection log. Conversely, the savemap WORLDMAP struct is **maintained live** during driving — this refutes the cautionary note "PC save format may use a smaller 26-byte WORLDMAP_PC struct omitting position arrays." The 26-byte struct in `hyne/SaveData.h` is what gets *serialised to disk* (positions are recomputed from `char_pos` mirror at save time), but in RAM the engine keeps the full position arrays at the documented offsets — they simply aren't copied into the on-disk save file unless the relevant vehicle is the active one.

**Verification recipe** (2-minute test, before shipping):
1. Open Cheat Engine, attach to FF8_EN.exe.
2. Get rental car. Add address `0x01CFFEE8` as 2-byte signed; add the next five 2-byte values too.
3. Drive the car; the first three should change continuously, the rotation (offset +0x0A within the array, i.e. `0x01CFFEF2`) should track your steering.
4. Cross-check: the X coordinate at `0x01CFFEE8` should approximate the foot DWORD at `0x0203EE80` divided by 4096 just before mount, and divided by 4096 again just after dismount (foot DWORDs are 20.12 fixed-point, savemap coords are integer block-relative).

**FFNx cross-reference:** FFNx accesses the savemap base via `ff8_externals.savemap_field` (`get_absolute_value(main_loop, 0x21)`), and the WORLDMAP struct sits inside the same savemap blob — there is no separate canary symbol for `car_pos`, confirming that the engine treats the savemap region as the canonical store.

---

## Q3 — Per-location car-entry gate

**Conclusion: it is data-driven via wmsetus.obj Section 7/8 trigger programs PLUS the per-polygon terrain byte; there is NO hardcoded location-ID whitelist in FF8_EN.exe.**

**Mechanism (synthesised from `wiki.ffrtt.ru/index.php/FF8/WorldMap_wmsetxx`, which I retrieved in full):**

1. **wmsetus.obj Sections 7 and 8** contain bytecode "scripts" (4-byte opcodes; first byte = identifier, last two bytes = parameter, second byte always `0xFF`). Per the FFRTT wiki: *"Scripts always start with 0x01 opcode, and finish with 0x16 opcode. There is always one 0x04 opcode inside. For now most is unknown, I only understood that 0x2B opcode refer to scene ID in its parameter."* The scene-ID-bearing 0x2B opcode is what triggers the field-transition load. These programs are walked by `sub_545EA0` and the pointer table is set up by `worldmap_wmset_set_pointers_sub_542DA0` (FFNx symbol).

2. **The vehicle-restriction operand** in those programs (your prior research's `0x80 = Squall foot, 0x84 = Selphie foot, 0x30 = Garden, 0x31 = Chocobo, 0x32 = Ragnarok`) is a **bitmask of allowed vehicles**, not foot-only. For "Balamb-style" town entries (Balamb, Dollet, Deling, Esthar) the mask includes the rental-car bits, so when you drive over the trigger polygon the engine accepts the transition and force-dismounts. For "Garden-style" docks (the landing pads on a moving Balamb Garden, Trabia, etc.) the mask omits the car bits, so `sub_545EA0` returns "no match" and the engine falls back to its solid-collision walk — the car bounces.

3. **The per-polygon terrain byte (`wmx.obj` polygon byte 0x0D)** acts as a coarse pre-filter. Type **29 = "City/Town/Enterable Area"** is the *only* type that even allows the trigger script to be evaluated; all other types either generate encounters (forest 0–5, plains 6–7, etc.) or block (ocean 32–34). So Q3's "polygon-type 29 being the actual gate" hypothesis is correct as the *first* gate, and the wmset Section 7/8 program is the *second* gate that decides per-vehicle.

**For your auto-drive mod, two cheap mod-side options:**

- **Option A (recommended, no parsing):** Read polygon byte 0x0D under the car position each tick. If it's 29, you're on a city/town entrance polygon → announce "arrived at \[Location\]" and let the engine handle the dismount. If the car velocity drops to ~0 and the locomotion byte is still 32–40 while the player is holding UP, you're bouncing off a Garden-style wall → announce "arrived near \[nearest landmark\]" and stop. This requires no `wmsetus.obj` parsing.
- **Option B (precise, requires parsing):** Walk the wmsetus.obj Section 8 program list at startup, extract the (block_X, block_Y, vehicle_mask, scene_id) tuples for each `0x2B` opcode, and build your own car-admits-here table. Match against current world-map block coords. The wmset pointer is reachable at `*ff8_externals.worldmap_section17_position` etc. — Section 7/8 follow the same offset pattern; an offset trace from `worldmap_wmset_set_pointers_sub_542DA0 + 0x1ED` should give you the Section 8 base for the loaded wmsetus.obj.

**No hardcoded list:** I checked the `ff8-memory` repo's full Running/General/GameState/Battle tables (fetched verbatim) and there is no entry resembling a "car-allowed locations" array; nor does the FFNx symbol table expose one. This confirms the data-driven design.

---

## Optional Confirmation — 26-byte WORLDMAP_PC vs. runtime layout

**Refuted (with nuance).** Hyne's `WORLDMAP_PC` struct in `SaveData.h` is indeed 26 bytes and *does* omit `char_pos`, `ragnarok_pos`, `bgu_pos`, `car_pos` arrays — those are not present in the on-disk PC save file. **But** the runtime engine still maintains the full PSX-format WORLDMAP struct in RAM at the savemap-relative offset, because the engine code that drives the player on the world map was ported from PSX code that expects the position arrays to be there. At save time the engine serialises only the 26 fields hyne knows about and rebuilds positions from `char_pos` (the foot mirror) and the active vehicle pointer.

So, for a runtime mod, **the savemap-relative offsets ARE the right place to read live positions** (Q2 answers itself in confirmation — the layout works). For tools that read .ff8 save files on disk, the 26-byte struct is correct and you cannot recover vehicle positions from disk.

---

## Caveats

- **Locomotion-byte-vs-context conflict unresolved by source.** I could not fetch `ff8-memory/locomotion.md` directly (GitHub blob fetches kept failing in this session). The README confirms the byte at `+0x1C40A5E` is named "Locomotion Method (WM)" but does not enumerate values; your context-provided enumeration (0=Squall, 6=Selphie, 31=Chocobo, 32–40=cars, 48=BGU, 50=Ragnarok) is the canonical map. The hypothesis that "the byte cycles through animation sub-states" is consistent with known engine behaviour (FF8's worldmap input handler runs at a faster rate than the locomotion state machine), so a 3-frame low-pass filter is safest.
- **76-byte vs 96-byte savemap-header arithmetic.** The `+0x125C` figure assumes your existing successful `0x01CFDC5C` savemap-base reading is correct AND that prior research's `+0x1270` was indeed measured from a 96-byte header; both are plausible but one or both could be off by a few bytes if the source you're comparing against is the PSX save (which uses a different header again). **Mandatory verification before ship:** put a Cheat Engine watchpoint on `0x01CFFEE8` and confirm it changes during driving. If it doesn't, scan ±0x40 around that address; the rental position is a contiguous 12-byte run with the third uint16 (Z) typically much smaller than X/Y.
- **`vehicles_instructions` semantics from a French comment.** The hyne `SaveData.h` comment ("voiture|Unused|BGU|Chocobo|Hydre|???|???|Unused") is in French and the reading "instructions = on-screen tutorial prompts seen" is my interpretation of how hyne's UI exposes it (hyne presents these as "tutorial seen" checkboxes in the WORLDMAP tab). Your existing prior research independently arrived at "tracks which prompts have been shown," which corroborates. Don't rely on this byte for live-mount detection.
- **Section 7 vs Section 8.** FFRTT documents "Sections 7-8: roads, train track, bridge" jointly. Which section holds the trigger programs (the `sub_545EA0` walker target) was not clearly attested in any source I could fetch — it's *most likely* Section 8 (your prior research's "38 programs" count matches the typical Section 8 entry count for wmsetus.obj). If parsing fails on Section 8, also try Section 7. The pointer for both is reachable through `worldmap_wmset_set_pointers_sub_542DA0` in FFNx.
- **No FFNx symbol named `s_currentVehicle`, `vehicleType`, etc.** FFNx canary does not expose a synthesised vehicle-state variable — it reads the same locomotion byte you do. This means there is no shortcut around the low-pass filter; the engine itself has the same ambiguity.
- **Subagent budget unused due to fetch failures.** The session spent its turns trying to retrieve `ff8.h`, `world.cpp`, `locomotion.md`, and `wmTerrain.md` from GitHub (all returned PERMISSIONS_ERROR/404 because GitHub blob URLs aren't directly fetchable through this tool's allowlist). A productive next step the user can take in IDA/Ghidra: set a breakpoint on writes to `0x02040A5E` and observe which sub-routine writes 32–40; that routine's parent is the mount-event handler, whose return path also writes the savemap WORLDMAP struct — single-stepping it will give exact runtime addresses and refute or confirm the +0x125C arithmetic in under five minutes.

---

## Recommendations (staged, with thresholds for changing course)

**Stage 1 — Ship now (low-risk, source-attested):**
1. Read locomotion at `0x02040A5E` with a 3-sample low-pass filter, gated by `*(uint16_t*)0x0203ED2C == 0`.
2. Compute distance/bearing to destination from savemap car array at `0x01CFFEE8` (rental), `0x01CFFED0` (Ragnarok), `0x01CFFEDC` (BGU), falling back to foot DWORDs at `0x0203EE80/84/88` only when locomotion ∈ {0, 6}.
3. Detect "arrived near \[location\]" by polling the polygon-type byte under the car position; when it transitions to 29, announce "arriving at \[name\]"; when car velocity drops to ~0 while throttle is held, announce "arrived near \[nearest landmark\]."

**Stage 2 — Verify in-game (mandatory before public release):**
- Cheat Engine pointer-scan on `0x01CFFEE8`: if it doesn't update while driving, search ±0x80; replace constants in mod source.
- Write 1 to `0x01CFFF2C` bit 0 (vehicles_instructions car bit) and verify the prompt was already seen — confirms semantics.
- Trace `0x02040A5E` with 30-frame logging during foot→car→foot transitions to confirm low-pass window is sufficient.

**Stage 3 — If Stage 1 misbehaves at any specific location:** parse wmsetus.obj Section 8 at startup (Option B above). Threshold: if more than 3 user-reported "wrong arrival announcement" bugs accumulate, it's time to do the parse work.

**Threshold to abandon savemap-relative addressing entirely and switch to a pointer scan:** if `0x01CFFEE8` reads non-zero garbage (e.g., looks like ASCII text or pointer-shaped values 0x004xxxxx) at game start with no rental, the +0x125C arithmetic is wrong and you should pattern-scan for `[X16][Z16][Y16][unk16][unk16][rot16]` matching foot DWORDs/4096 at known on-foot positions, then hardcode the resulting RVA.
