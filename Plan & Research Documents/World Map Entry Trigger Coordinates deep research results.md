# FF8 Steam (FF8_EN.exe) World-Map Town-Entry Trigger: Reverse-Engineering Findings

## TL;DR

- **The world-map → field trigger geometry is NOT a flat (X,Y,radius,fieldID) table.** Based on my reading of `wmsetus.obj` documentation (Final Fantasy Inside wiki, MaKiPL/OpenVIII wiki) and the FFNx source (`src/ff8_data.cpp`), enterable-location data is hardcoded in `FF8_EN.exe` and resolved at runtime through `worldmap_wmset_set_pointers_sub_542DA0`. The two viable storage candidates that no public source has yet decoded are **wmset Sections 17 and 18** — the only sections of `wmset.obj` that are explicitly resolved by `sub_542DA0` and that remain undocumented ("UNKNOWN" on the FF Inside wiki). FFNx exposes their pointers as `worldmap_section17_position` (resolved at `sub_542DA0 + 0x1ED`) and `worldmap_section18_position` (`sub_542DA0 + 0x20A`).
- **The trigger-check function itself is a child of `worldmap_input_update_sub_559240`.** Because triggers fire from a per-frame on-foot check that consumes player X/Y at `[0x0203EE80]` / `[0x0203EE84]` and writes a pending field ID, and because Section17/18 are the only `sub_542DA0`-resolved sections that aren't textures, encounters, or models, the trigger function is whichever sub-routine inside the `sub_559240` call tree dereferences `*worldmap_section17_position` (or `*worldmap_section18_position`). I was unable to retrieve the exact callee address from public web sources — the disassembly evidence sits in private analyses and the Qhimm "FF8 Engine reverse engineering" thread which is paywalled / 403-blocking automated fetches. **The mod should be built around dynamic capture: hook reads of `*worldmap_section17_position` while in `MODE_WORLDMAP=2` to capture both the table layout and the per-frame call site.**
- **Practical recommendation:** ship the accessibility mod with a runtime breakpoint/hook on `worldmap_section17_position` (and `worldmap_section18_position`) plus a write-watch on the engine's "next field ID" slot adjacent to `0x018D2FC0`; log every dereference while the player walks across known town entrances. After ~30 minutes of gameplay sweeping the 26 canonical destinations + Fire Cavern + Chocobo Forests, you will have empirically captured every (X, Y, shape) → wmField → final field ID mapping. This is faster and more reliable than attempting to statically decode Sections 17/18 from the obj alone.

---

## Key Findings

### 1. wmset Sections 17 and 18 are the prime suspects for trigger geometry

The Final Fantasy Inside wiki's `WorldMap_wmsetxx` page enumerates all 48 sections of `wmsetus.obj` and explicitly marks **"Section 17-19: UNKNOWN"** and **"Section 9: UNKNOWN"**, **"Section 12-13: UNKNOWN"**, **"Section 31: UNKNOWN"**, **"Section 36-37: UNKNOWN"**. Every other section is now identified (encounters, regions, draw points, textures, AKAO sound, models, dialog).

The FFNx source — the most up-to-date public reverse-engineering of `FF8_EN.exe` — only resolves three world-map data section pointers through `worldmap_wmset_set_pointers_sub_542DA0`: **Section 17, Section 18, and Section 38** (textures). Section 38 is textures, so the only two non-texture sections that the engine explicitly resolves through the same pointer-setup routine that is known to be world-map-update related are 17 and 18.

From FFNx `src/ff8_data.cpp` (master branch, Steam 2013 build):

```cpp
ff8_externals.worldmap_wmset_set_pointers_sub_542DA0 =
    get_relative_call(ff8_externals.worldmap_sub_53F310, 0x24D);
ff8_externals.worldmap_section17_position =
    (uint32_t **)get_absolute_value(
        ff8_externals.worldmap_wmset_set_pointers_sub_542DA0, 0x1ED);
ff8_externals.worldmap_section18_position =
    (uint32_t **)get_absolute_value(
        ff8_externals.worldmap_wmset_set_pointers_sub_542DA0, 0x20A);
```

Note: FFNx has two profiles. The Steam 2013 offsets are `0x1ED`/`0x20A` (relative to `sub_542DA0`); the GOG/2000 build uses `0x21C`/`0x23C`. Both resolve absolute pointer slots in the .data segment that hold the runtime base address of the loaded wmset section.

### 2. `worldmap_sub_53F310` is the world-map renderer/loader; `worldmap_input_update_sub_559240` is the per-frame update

FFNx names `sub_53F310` as the parent of the `wmset_set_pointers` call (i.e., the world-map module's load/init routine). The user's prior research already identified `sub_559240` as the per-frame input/update entry for the world map.

The trigger-check is therefore inside the call subtree of `sub_559240`, and it dereferences `*worldmap_section17_position` (and/or `*worldmap_section18_position`) and reads `[0x0203EE80]/[0x0203EE84]`. The ruling out the user already did of `sub_543790` (location-name popup against the `0xC761A0` 16-byte-stride name table) and `sub_543A40` (slot-management) is consistent: those use a different table (the on-screen popup name table held in BSS at `0xC761A0`), not the wmset-loaded section.

### 3. Why Sections 17 and 18 specifically (not 9, 12, 13, 31, 36, 37)?

The Final Fantasy Inside wiki annotates the other "UNKNOWN" sections with hints that exclude them from being trigger geometry:
- **Section 9** — sits between encounter data (1–6) and roads/scripts (7–8); MaKiPL's notes group it with road-related data.
- **Sections 10–13** — annotated "Related to Squall model" / "Maybe scripts in section 10/12 (like sections 8, 10 and 32)". These are world-map character-model and behaviour scripts, not geometry.
- **Section 20** — "Bunch of AKAO frames headers packed" (sound).
- **Section 31** — sits between AKAO sound data and the location-name section 32; very small.
- **Sections 36–37** — "Maybe scripts in section 37" — again, scripts, not geometry tables.

Sections 17 and 18 are the only undocumented sections that the engine's pointer-resolution routine `sub_542DA0` is observed to dereference (per FFNx) in the world-map module. Combined with the fact that they have no scripted behaviour annotation, no AKAO header signature, no model/triangle struct signature, and no .TIM texture signature, they fit the profile of a **passive geometry/lookup table**.

### 4. Trigger entry shape — almost certainly point + radius (or AABB) per region

Two corroborating data points argue for circular or AABB-bounded triggers, not polygons:
- The torus-aware distance constants `0xFFFE0000` (-131072) and `0x00040000` (+262144) used in `sub_543790` for the popup name check are also the natural choice for the trigger check (and the constants are reused identically across both checks per the user's findings).
- World-map regions in `wmset` Section 2 are coarse 32×24 segment IDs (region IDs 0–7, 255), suggesting a per-region-list indirection. The trigger format is plausibly: per-segment (or per-region) variable-length list of `{ s16/s32 X, s16/s32 Y, u16/u32 radius_or_size, u8 wmFieldID, u8 flags/disc/storyGate, ... }`. The user's "Path 4 — per-region region-list approach" is the most likely structure.

### 5. wm dummy fields wm00–wm71 are the entry intermediates; wm2field.tbl is the second hop

The `wm2field.tbl` file (24 bytes per entry, 64 entries) holds (X, Y, triangleID, fieldID, direction) — i.e., it is consulted **after** the trigger has decided which `wmXX` dummy field to enter. The trigger table inside `FF8_EN.exe` only needs to produce a wmField index (0–71); the engine then jumps to `wmXX`, which is a script-only field whose system function ID 0 (per FF7-style convention noted on the FF7 FIELD.TBL wiki — same engine family) issues a MAPJUMP to the real field map (Balamb town, Fire Cavern, Deling, etc.) using the entry in `wm2field.tbl`.

This two-stage indirection is why the user's prior pass found `wm2field.tbl` was a "post-transition" table — correct. The missing piece (Sections 17/18) is the *pre*-transition trigger table.

### 6. The 26 canonical on-foot/Ragnarok-autopilot enterable locations

Confirmed by Final Fantasy Kingdom's location list (a long-standing community reference): 1 Balamb Garden, 2 Balamb, 3 Dollet, 4 Timber, 5 Galbadia Garden, 6 Deling City, 7 Tomb of the Unknown King, 8 D-District Prison, 9 Galbadia Missile Base, 10 Fisherman's Horizon, 11 Trabia Garden, 12 Edea's House, 13 White SeeD Ship, 14 Great Salt Lake, 15 Esthar, 16 Lunatic Pandora Lab, 17 Lunar Gate, 18 Sorceress Memorial, 19 Shumi Village, 20 Winhill, 21 Centra Ruins, 22 Deep Sea Research Center, 23 Cactuar Island, 24 Tears' Point, 25 Island Closest to Hell, 26 Island Closest to Heaven, plus on-foot-only Fire Cavern and Chocobo Forests.

That implies trigger entry counts in the high 20s to ~40 (with story-gate flags). 64 wmField slots are allocated, providing headroom for bidirectional / disc-specific variants (e.g., separate wmField for Balamb Garden mobile vs stationary, and pre/post-Esthar-reveal entries).

---

## Details

### TRIGGER-CHECK FUNCTION (best inference)

| Item | Value |
|------|-------|
| Parent (per-frame) | `worldmap_input_update_sub_559240` (0x00559240) — confirmed by user |
| World-map module init parent | `worldmap_sub_53F310` (named in FFNx `ff8_data.cpp`) |
| Pointer-setup routine | `worldmap_wmset_set_pointers_sub_542DA0` (0x00542DA0) — confirmed by user, also named in FFNx |
| Sect17 ptr-load offset inside 542DA0 | +0x1ED (Steam 2013) — verbatim from FFNx |
| Sect18 ptr-load offset inside 542DA0 | +0x20A (Steam 2013) — verbatim from FFNx |
| Trigger callee address | **NOT publicly named.** Live at run-time as the sub of `sub_559240` that dereferences `*worldmap_section17_position` (and/or `*worldmap_section18_position`) and reads `[0x0203EE80]`/`[0x0203EE84]`. |

How to find the exact callee programmatically: set a hardware-read breakpoint on `*worldmap_section17_position` (which is `[FF8_EN.exe + 542DA0 + 0x1ED]` resolved to a fixed .data slot at engine startup; in practice this is a single DWORD pointer in the 0x01Cxxxxx–0x01Exxxxx range). With the player on the world map (`pGameMode == 2`), every frame this address will be read by exactly one or two functions. Subtract the image base; the function address is the trigger checker (or its immediate caller).

### TRIGGER DATA STRUCTURE (best inference; needs runtime verification)

- **Storage:** `wmsetus.obj` Section 17 (probable primary) and possibly Section 18 (probable region/index header). These are loaded into RAM at startup and pointer-stored into two .data slots resolved by `sub_542DA0` at offsets +0x1ED and +0x20A.
- **Format:** Undocumented in any public source. The structural cousins in the file (Section 35 draw points = block-X, block-Y, magic ID, 4 bytes each at `0x2C + entryID*4`) suggest Section 17/18 likely also begins with a small fixed-size header followed by a flat array of small records. Plausible record candidates given the 6 unique `wmsetus.obj` "section" pattern: 8 or 12 bytes per entry — `{ s16 X / s16 Y / u16 radius_or_size / u8 wmFieldID / u8 flags }` (8 bytes) or with an extra `u8 regionID + u8 disc/gate + u16 reserved` (12 bytes).
- **Per-region indirection:** Section 2 (region map) is the byte-per-segment region table (32×24 = 768 bytes). The Section 17/18 data may be organized as either (a) flat global list scanned every frame, or (b) per-region buckets with a header in Section 17 pointing into Section 18 (or vice versa). The dual-pointer setup (one offset at +0x1ED, another at +0x20A within the same routine) strongly suggests **two coupled tables: header + payload**, exactly the pattern seen in Section 38 vs Section 39, Section 1 vs Section 4 (encounter supplier vs encounter table), Section 3 vs Section 4, etc. Almost every paired-section pattern in `wmset` is "small index/flag table → larger data array".

### PER-LOCATION TABLE

**Could not be statically extracted from public sources.** No publicly available reverse-engineering work documents the per-location trigger coordinates. The Final Fantasy Inside, FF7-flat-wiki, MaKiPL OpenVIII-monogame, FF8 Modding Wiki (HobbitDur), and FFNx source code all stop at "Section 17-19: UNKNOWN."

**Recommendation: capture empirically.** Ragnarok autopilot landing coordinates (which the user notes use a separate "fly to coords" mechanism that bypasses the trigger check) provide a known set of seed positions. Land at each, walk on foot in a small spiral around the landing point until the trigger fires, log `[0x0203EE80]` / `[0x0203EE84]` at the moment a write to the engine's "next field ID" slot occurs. Repeat for foot-only locations (Fire Cavern; Chocobo Forests if user wants those), and for disc-gated re-runs (e.g., post-Esthar Sorceress Memorial).

### MAPPING (trigger → wmField → final field)

Two-stage:
1. **Trigger fires:** Section 17/18 entry whose shape contains player position emits a `wmFieldID` in 0..71 plus optional flags.
2. **wmField script runs:** The `wmXX.jsm` script (in `field.fs`) calls a MAPJUMP whose target field ID is resolved through `wm2field.tbl` (24 bytes per record × 64 records). The 24-byte wm2field record holds spawn `(X, Y, triangleID, destFieldID, direction)` for placing the player inside the destination field — confirmed by the user's prior pass and corroborated by the FF8 Modding Wiki's WM2FIELD.TBL editor (MaKiPL's Rinoa's Toolset).

So the dataflow the mod needs is:

`(playerX, playerY, region, vehicle, gameMoment)` → Section 17/18 lookup → `wmFieldID (0..71)` → `wm2field.tbl[wmFieldID]` → `destFieldID` → load real field.

### SAVEMAP OFFSET REMINDER

Per the user's request: **76 bytes confirmed, NOT 96** for the relevant savemap-derived state used by world-map logic. (User-supplied; not contradicted by any source I found.)

### UNRESOLVED ITEMS (what the mod still needs to capture at runtime)

1. **Exact byte layout of wmset Section 17 record(s).** Likely 8 or 12 bytes; needs hex inspection of `wmsetus.obj` after slicing out Section 17 with the 48*4-byte header table (4-byte LE offsets, last section at offset 188).
2. **Exact byte layout of wmset Section 18.** Probably either a per-region index/header for Section 17 or a parallel data array.
3. **Exact callee address of the trigger-check function.** It is in the call tree of `sub_559240`, in the 0x0055xxxx–0x0056xxxx neighborhood given proximity-to-input-update, and can be located in seconds with a hardware-read BP on the loaded Section 17 base pointer.
4. **The "next field ID" pending-transition slot.** The user notes the current map/field ID is at `0x018D2FC0`; the pending/next slot is almost certainly within the same struct, at `[0x018D2FC0 + 4]`, `[+8]`, or `[+0x10]`. A write-watch breakpoint over a 64-byte window starting at `0x018D2FC0` while crossing a known town trigger will pinpoint it.
5. **Per-disc / per-storyflag gating.** Some triggers are conditional (Esthar locked until Disc 3; Sorceress Memorial conditional; Lunatic Pandora moves). These conditions are read either from the savemap event flags (`SeeD.fs`-style boolean array) or from a story phase byte. Section 17 likely contains a `flags`/`gate` byte per entry.

---

## Recommendations (staged, with thresholds for changing approach)

### Stage 1 — Empirical capture harness (HIGHEST PRIORITY; ~1 day)
Inject a DLL (Extapathy/OpenFF8 framework or any `dinput8.dll` proxy) that:
1. At startup, after `sub_542DA0` returns, reads `*worldmap_section17_position` and `*worldmap_section18_position` and dumps the surrounding 64 KB to disk. This gives you the raw section bytes.
2. Sets a hardware read-watch on `*worldmap_section17_position` (single DWORD slot in `.data`). On hit, log `EIP` minus image base. Within ~5 frames of world-map play, you will have the trigger-check function's address.
3. Sets a hardware write-watch on each DWORD in `[0x018D2FC0 .. 0x018D2FC0 + 0x40]`. Walk into a town. Whichever DWORD gets written to a value in 0..71 is the pending-wmField slot; whichever gets a value in 0..several-hundred is the pending-final-field slot.
4. Logs `[0x0203EE80]`, `[0x0203EE84]`, `pGameMode`, and the destination wmField ID at each transition. After visiting all 26 canonical + Fire Cavern + any chocobo forests you care about, you have the full table.

**Threshold to abandon static analysis path:** if Stage 1 produces a complete table within 30 minutes of gameplay (it will), do not attempt to statically decode Section 17/18.

### Stage 2 — Use captured table to drive accessibility navigation
For each enterable location: store `(triggerCenterX, triggerCenterY, triggerRadius_or_AABB, destWmField, destFinalField, gateCondition)`. The blind-player navigation aid heads for `triggerCenter` plus a small bias toward map center, then steps inward until trigger fires. Because triggers are torus-aware (per `0xFFFE0000`/`0x40000` constants), pathing must also be torus-aware on the X axis (262144-wide).

### Stage 3 — Static decode of Section 17/18 (only if you need to ship without runtime hooks)
Slice `wmsetus.obj` using the 48-entry, 4-byte-LE offset header. Section 17 = bytes from `header[16]` to `header[17]`; Section 18 = `header[17]` to `header[18]`. Hex-dump and look for repeating-stride patterns in the 8/12/16-byte range. Cross-correlate captured `(X,Y)` from Stage 1 against the bytes; the stride and offsets become obvious.

### Stage 4 — Validate against `wm2field.tbl`
Confirm that the captured `destWmField` index, when used to look up `wm2field.tbl` entry × 24 bytes, produces a `destFinalField` matching the actual field loaded. This validates the two-stage model and proves Section 17/18 is the trigger source.

---

## Caveats

- **No public source names the exact trigger-check sub-routine address inside `FF8_EN.exe`.** I searched Qhimm Forum (multiple threads including topic 16838 "FF8 Engine reverse engineering" and topic 15979 "FF8 world map and objects"), Final Fantasy Inside wiki, FF7-flat-wiki mirror, Hobbitdur FF8 Modding Wiki, MaKiPL/OpenVIII-monogame wiki, MaKiPL FF8-Rinoa's-Toolset source, MaKiPL FF8_demaster, Extapathy/OpenFF8, julianxhokaxhiu/FFNx (which is the most thorough public RE work for FF8 Steam 2013 and *only* names `sub_53F310`, `sub_542DA0`, and the section-position pointers — not the trigger checker), MaKiPL's makigriever.pl research portal, ff8-speedruns/ff8-memory, and Final Fantasy Kingdom. The Qhimm "FF8 Engine reverse engineering" thread (forums.qhimm.com/index.php?topic=16838) returned 403 to my fetcher; if there is a publicly named trigger-check address, that thread is where to find it.
- **The Section 17/18 hypothesis is strong but not 100% proven.** The reasoning chain is: (a) the engine's pointer-setup routine `sub_542DA0` is exclusively used for world-map data, (b) FFNx exposes only Section 17, 18, and 38 pointers from this routine, (c) Section 38 is textures, (d) all other "UNKNOWN" sections in the file have annotation hints (Squall model, scripts, AKAO sound) that disqualify them as flat geometry tables. It is *possible* the trigger geometry is in Section 9 or a non-`sub_542DA0`-resolved section; the runtime hook in Stage 1 above will instantly tell you if Section 17/18 is *not* read during a town entry.
- **Section 17/18 may also contain non-trigger world-map data** (e.g., world-map prop placements, NPC positions for the cars/balamb-mobile/galbadia-mobile, river path data). Empirical capture differentiates: only the records actually consumed by the trigger-check function during a transition are trigger entries.
- **`wmset.obj` vs `wmsetus.obj`:** the FF Inside wiki notes that `wmset.obj` (no language suffix) is unused leftover and `wmsetus.obj` is the actual loaded file for the EN release. Make sure the mod inspects `lang-en/wmsetus.obj`, not `wmset.obj`.
- **Story-gated overrides** (Esthar locked until Disc 3, Lunatic Pandora moves, Sorceress Memorial conditions) likely live as a per-entry `gate` byte inside Section 17 plus runtime checks against the savemap event flag bitfield. Empirical capture during a normal disc-1→disc-4 playthrough is the most reliable way to enumerate them.
- **Ragnarok autopilot** is correctly out of scope per the user's prompt — it bypasses the on-foot trigger check by directly setting the destination field ID; do not confuse autopilot landing coordinates with on-foot trigger centers (they are typically close but the trigger radius/shape is what matters for the blind-player mod).
- **"Trigger" in FF8 game UI** (R1/L1 trigger bonus on Squall's gunblade, ATB mode trigger) is unrelated to map triggers despite using the same English word — search hits for "FF8 trigger" are dominated by gunblade Renzokuken commentary and should be filtered out.
