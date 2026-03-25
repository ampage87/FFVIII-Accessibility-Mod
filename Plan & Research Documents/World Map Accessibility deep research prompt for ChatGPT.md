# FF8 World Map Accessibility Deep Research Prompt for ChatGPT
## Date: 2026-03-24
## Purpose: Gather all information needed to build world map navigation TTS for blind players

---

## PROMPT TO SUBMIT TO CHATGPT DEEP RESEARCH:

---

I'm building an accessibility mod for blind players for Final Fantasy VIII (Steam 2013 PC edition, FF8_EN.exe, running with FFNx v1.23.x). The mod is a Win32 DLL (`dinput8.dll`) injected alongside FFNx. I've already built a field navigation system with an entity catalog (NPCs, doors, draw points) and auto-walk that pathfinds the player to selected targets. Now I need to extend this approach to the **world map**.

My goal is to make the world map fully navigable for a blind player. This means:
- An **entity catalog** of all reachable locations (towns, caves, forests, gardens, draw points, etc.) with compass directions and distances
- **Auto-navigation** that steers the player toward a selected destination
- **Filtering by reachability** — only showing locations the player can actually reach given their current position, continent, terrain, and transportation mode
- Support for **all transportation modes**: on foot, rental car, chocobo, Balamb Garden (mobile), and Ragnarok airship
- Leveraging the **Ragnarok's autopilot** functionality if possible, and potentially adapting it for other vehicles

I need you to research the FF8 PC world map system in as much detail as possible. My primary sources should be: the Qhimm wiki (wiki.ffrtt.ru), the FF8 Modding Wiki (hobbitdur.github.io/FF8ModdingWiki), myst6re's Maelstrom/Deling tools, the FFNx source code on GitHub (julianxhokaxhiu/FFNx), community tools like Cactilio/HobbitDur's world map editors, and any reverse engineering documentation. Focus on the **English Steam 2013 release** (not PSX, not Remastered).

---

### CRITICAL: SAVEMAP OFFSET CORRECTION

Many community resources calculate savemap offsets assuming the PC savemap header is 96 bytes (0x60). The **CONFIRMED** header size on the Steam 2013 build is **76 bytes (0x4C)**. All post-header offsets from community wikis/tools may be **0x14 (20 bytes) too high**.

My confirmed reference points:
- **Savemap base address: `0x1CFDC5C`**
- GFs start at savemap + 0x4C (16 GFs × 0x44 bytes each)
- Characters start at savemap + 0x48C (8 characters × 0x98 bytes each)
- Party formation at savemap + 0xAF0 (4 bytes, 0xFF terminated)

When providing any savemap-relative offsets, please note whether they come from a source that uses the 96-byte or 76-byte header assumption, so I can apply the correction.

---

### WHAT I ALREADY KNOW FROM FFNx SOURCE (ff8_data.cpp, ff8.h, save_data.h)

**Game mode:**
- `FF8_MODE_WORLDMAP = 2`

**World map entry/loop (resolved dynamically from main_loop):**
- `worldmap_enter_main` = `main_loop + 0x2C0`
- `worldmap_main_loop` = `main_loop + 0x2D0`

**World map main update function:**
- `worldmap_with_fog_sub_53FAC0` — the main per-frame world map function
- `worldmap_input_update_sub_559240` — input processing sub-function

**wmset section pointers (resolved by `worldmap_wmset_set_pointers_sub_542DA0`):**
- `worldmap_section17_position` (uint32_t**)
- `worldmap_section18_position` (uint32_t**)
- `worldmap_section38_position` through `worldmap_section42_position` (uint32_t**)
- These point to parsed sections of the wmset.obj file — I need to know what each section contains

**World map dialog:**
- `world_dialog_assign_text_sub_543790(int, int, char*)` — assigns dialog text on world map
- `world_dialog_question_assign_text_sub_5438D0(int, int, char*, int, int, int, uint8_t)` — question dialogs
- `worldmap_windows_idx_map` (char*) — maps window indices to something

**Steps and SeeD level:**
- `worldmap_update_steps_sub_6519D0` — step counter update
- `worldmap_update_seed_level_651C10` — SeeD level update
- `set_drawpoint_state_521D90(uint8_t, char)` — draw point state management

**Battle trigger on world map:**
- `battle_trigger_worldmap` = `worldmap_with_fog_sub_53FAC0 + 0x4EA` (US)
- `sub_541C80(WORD*)` — called at battle trigger point

**Viewport/position:**
- `current_viewport_x_dword_1A7764C`, `current_viewport_y_dword_1A77648`
- `current_viewport_width_dword_1A77654`, `current_viewport_height_dword_1A77650`

**Savemap world map struct (from save_data.h):**
```c
struct savemap_ff8_worldmap {
    uint16_t char_pos[6];           // character/party position
    uint16_t unknown_pos1[6];       // unknown entity position
    uint16_t ragnarok_pos[6];       // Ragnarok airship position
    uint16_t balamb_garden_pos[6];  // Balamb Garden position
    uint16_t car_pos[6];            // rental car position
    uint16_t unknown_pos2[6];       // unknown position
    uint16_t unknown_pos3[6];       // unknown position
    uint16_t unknown_pos4[6];       // unknown position
    uint16_t steps_related;
    uint8_t car_rent;               // car rental active flag
    uint8_t unk1[7];
    uint16_t unk2;
    uint16_t unk3;
    uint8_t disp_map_config;        // 0:none|1:minisphere|2:minimap
    uint8_t unk4;
    uint16_t car_steps_related;
    uint16_t car_steps_related2;
    uint8_t vehicles_instructions_worldmap;
    uint8_t pupu_quest;
    uint8_t obel_quest[8];
    uint8_t unk5[2];
};
```

Each position array has 6 uint16_t values — I need to know what the 6 components represent (x, y, z, angle, ...?).

**Character model loading:**
- `worldmap_chara_one` — loads character models on world map
- `worldmap_sub_545E20`, `worldmap_sub_545F10`, `worldmap_sub_546100` — related

---

### QUESTIONS — PLEASE ANSWER EACH IN DETAIL

#### 1. WORLD MAP COORDINATE SYSTEM

- What coordinate system does FF8's world map use? (origin, axis orientation, scale)
- What is the **range** of X and Y coordinates? (The world wraps — what are the min/max values?)
- How does the **Z axis** work? (Is it elevation? Terrain height?)
- The savemap stores positions as `uint16_t[6]` — what do all 6 components represent? (My guess: X, Y, Z, facing angle, ?, ?)
- Is the world map a flat grid that wraps, or a sphere?
- How do world map coordinates relate to the **minimap** / **sphere map** coordinates?
- What is the approximate **real-world scale** — how many coordinate units per game "step"?

#### 2. WMSET.OBJ FILE FORMAT

The `wmset.obj` file is the core world map data file. I need its complete section breakdown.

- How many **sections** does wmset.obj contain?
- For each section, what does it store? I know FFNx references sections 17, 18, 38, 39, 40, 41, 42 specifically — what are those?
- Which section(s) contain **location/POI definitions** (towns, caves, forests)?
- Which section(s) contain **location names** (text strings)?
- Which section(s) contain **terrain/mesh data**?
- Which section(s) contain **encounter region definitions**?
- Which section(s) contain **draw point locations**?
- Where is the file located on disk in the Steam 2013 build? (Path within the game data archives)
- How is it loaded into memory at runtime? What addresses hold the parsed section pointers?
- Are there community tools that parse wmset.obj? (Deling, Cactilio, HobbitDur's editor?)

#### 3. LOCATION / POINT OF INTEREST DEFINITIONS

This is critical — I need a complete catalog of interactive locations on the world map.

- Where are **town/dungeon entry points** defined? (The locations you walk to and press X to enter)
- What data does each entry point contain? (coordinates, entry field ID, name, radius/hitbox, required vehicle?)
- How many total entry points exist on the world map?
- Are location names stored in wmset.obj, or in a separate text/string archive?
- How does the game display the **location name popup** when you approach a town? Where is that name data?
- Are there different categories of locations? (e.g., towns, dungeons, chocobo forests, Shumi Village, Lunatic Pandora, etc.)
- Do location entry points have **conditions** (story progression gates, vehicle requirements)?
- Where are **chocobo forests** defined? Are they in the same system as towns?
- Where are **world map draw points** defined? How many are there? What are their coordinates and magic types?
- Where is the **Ragnarok landing zone** data? (It can only land in certain areas)

Please provide a **complete list of all world map locations** with their approximate coordinates if available — this will form the basis of my entity catalog.

#### 4. TERRAIN SYSTEM AND WALKABILITY

- How is the world map terrain structured? (Triangle mesh? Grid? Sectors?)
- What **terrain types** exist? (Grass, forest, mountain, water, beach, road, snow, desert, etc.)
- Where is terrain type data stored? (In wmset.obj? Which section?)
- How does the game determine **walkability** for each terrain type for each vehicle?
- What is the **terrain type ID** scheme? (e.g., type 0 = grass, type 1 = forest, etc.)
- How large are terrain segments/tiles?
- Is there a **heightmap** or is the mesh purely 2D with visual 3D?

#### 5. TRANSPORTATION MODE TRACKING AND RESTRICTIONS

FF8 has multiple transportation modes on the world map. For each mode, I need to know:

**General:**
- Where is the **current transportation mode** stored in memory? (Which byte/flag says "on foot" vs "in car" vs "on chocobo" vs "in Garden" vs "in Ragnarok"?)
- Is it a single byte enum, or multiple flags?
- What are the **flag values/IDs** for each mode?

**On foot:**
- What terrain types can the player walk on?
- What is the movement speed?
- Can the player walk through shallow water?

**Rental car:**
- What terrain types can the car drive on? (Roads only? Grass too?)
- Where is the car rental state stored? (`car_rent` in savemap — is that all?)
- What is the car's movement speed?
- Can the car be parked anywhere, or only at specific spots?
- Are there fuel/step limits? (The `car_steps_related` fields suggest so)

**Chocobo:**
- What terrain types can chocobos traverse? (Can they cross shallow water? Mountains?)
- Where is the "currently riding chocobo" state stored?
- Where did the chocobo come from? (Chocobo forest locations)
- Can you dismount anywhere?
- Is there a chocobo type system (regular vs special)?

**Balamb Garden (mobile):**
- After which story event does Garden become mobile?
- What terrain types can Garden traverse? (Water? Shallow only? Deep ocean?)
- Where is the **Garden position** stored at runtime (not just savemap)?
- How does Garden movement differ from walking? (Speed, input?)
- Where is the **"Garden is mobile" flag** stored?
- Can Garden dock at certain locations? Where are docking points defined?

**Ragnarok:**
- What terrain types can Ragnarok fly over? (Everything — it's an airship?)
- Where is the Ragnarok available flag?
- Where is its **current position** stored at runtime?
- What is its movement speed?
- Can it land anywhere, or only on specific terrain? Where are valid **landing zones** defined?
- After landing, does the player disembark on foot?

#### 6. RAGNAROK AUTOPILOT SYSTEM

The Ragnarok has a built-in autopilot in the game that can fly to certain destinations.

- How does the **autopilot menu** work? What triggers it?
- Where is the **list of autopilot destinations** stored?
- What memory addresses control the autopilot state (engaged/disengaged, target coordinates)?
- How does the autopilot navigate? (Direct line? Does it follow waypoints?)
- Can the autopilot list be **modified at runtime** to add custom destinations?
- Could the autopilot's navigation algorithm be **adapted or reused** for other vehicles? (e.g., setting a target coordinate and having the car auto-steer toward it)
- Where is the autopilot's **target position** stored?
- Is there a **callback or flag** that indicates autopilot has reached its destination?

#### 7. PLAYER POSITION AND HEADING AT RUNTIME

Beyond the savemap, the game must have runtime position data.

- Where is the **player's current world map position** at runtime? (Not savemap — the live, per-frame value)
- Where is the **player's facing direction / heading**?
- Where is the **movement velocity** or **is-moving flag**?
- Are there separate runtime position variables for each vehicle, or does the "current entity" switch based on transportation mode?
- How is the **camera orientation** determined on the world map? (Following player? Fixed north-up?)

#### 8. CONTINENT AND ISLAND BOUNDARIES

For filtering reachable locations, I need to know continent/region boundaries.

- Does the game define **named continents or regions**? Where is this data?
- How can I determine **which continent/island the player is on**?
- Is there a region ID system, or do I need to infer it from coordinates + terrain?
- What are the approximate **bounding coordinates** of each major landmass:
  - Balamb (island with Balamb Town, Balamb Garden, Fire Cavern)
  - Galbadia (large western continent with Timber, Deling City, D-District Prison)
  - Trabia (northern snowy continent with Trabia Garden)
  - Esthar (large eastern continent with Esthar City, Lunatic Pandora Lab, Lunar Gate)
  - Centra (southern continent with ruins, Edea's House)
  - Winhill area
  - Islands (Island Closest to Heaven, Island Closest to Hell, Cactuar Island, etc.)
- Are there **bridge** or **shallow water crossing** areas that connect landmasses on foot?
- What happens at the **world map edges**? (Wrapping? The world is a sphere/cylinder?)

#### 9. LOCATION ENTRY / FIELD TRANSITION TRIGGERS

When the player reaches a town on the world map and presses X, the game transitions to a field.

- Where are the **entry trigger zones** defined? (Circle around a point? Rectangle? Polygon?)
- What is the typical **trigger radius** for a town entry point?
- How does the game detect the player is "at" a location? (Distance check? Terrain check? Both?)
- Where is the **target field ID** stored for each entry point?
- Is the entry point check done per-frame, or only on button press?
- Does the location name popup use the same trigger zone as the entry point?
- Are some entry points **conditional** on story progress? Where are these conditions checked?
- For locations with **multiple entry points** (e.g., Esthar has multiple approaches), how are they organized?

#### 10. ENCOUNTER REGIONS

- Where are **random encounter regions** defined on the world map?
- What data does each region contain? (Encounter table ID, encounter rate, terrain type association?)
- Can I read the encounter region to tell the player what type of area they're in?
- Do encounter rates vary by terrain type? By vehicle? (Cars prevent encounters?)

#### 11. WORLD MAP DRAW POINTS

- How many draw points exist on the world map?
- Where are their **coordinates** defined?
- Where is their **state** (available / depleted) stored? (`set_drawpoint_state_521D90` handles this)
- What **magic** does each draw point offer?
- Please provide a **complete list of world map draw points** with coordinates and magic types if available.

#### 12. STORY PROGRESSION AND LOCATION AVAILABILITY

Locations on the world map change throughout the game:

- Some locations are **only accessible at certain story points** (e.g., Lunar Gate, Deep Sea Research Center). Where are these gates stored?
- **Balamb Garden moves** after a certain event — how does the game track its position?
- **Esthar** is hidden/revealed at a story point — how does the game control this?
- The **Ragnarok** becomes available at a specific point — what flag?
- **Lunatic Pandora** appears and disappears — how tracked?
- Are there any locations that **permanently close** after certain story events?
- Where is the **game moment / story progression variable** stored? (I know `field_vars_stack_1CFE9B8 + 0x100` = game_moment from FFNx — is this used for world map gating too?)

#### 13. WORLD MAP MESH AND RENDERING

I don't need rendering details, but mesh data may help with navigation:

- How is the world map divided into **segments/blocks**?
- How many segments are there?
- What is the **polygon/triangle count** per segment?
- Does each segment have a **bounding box** I could use for broad-phase collision/navigation?
- Is there a **navigation mesh** or simplified walkability grid, or is walkability purely based on terrain type?

#### 14. WORLD MAP TEXT AND STRINGS

- Where are **location name strings** stored? (In wmset.obj? In the main kernel/text archives?)
- What encoding are they in? (FF8's custom character encoding?)
- How does the game display the **"Balamb"** popup when you approach a town?
- Is there a **string table** I can read to get all location names with their IDs?
- Are world map dialog strings (e.g., "Board the Garden?" prompts) stored separately?

#### 15. KNOWN MEMORY ADDRESSES ON WORLD MAP

Please compile any known static addresses relevant to the world map in the Steam 2013 build:

- Player position (runtime, not savemap)
- Current vehicle/transportation mode
- Current facing/heading angle
- Is-moving flag
- Current terrain type under player
- World map active flag
- Garden position (runtime)
- Ragnarok position (runtime)
- Encounter counter / steps since last encounter
- Any world map state machine / phase variable

---

### SPECIFIC USE CASES TO DESIGN FOR

Please keep these accessibility use cases in mind when answering — if any of your answers have implications for these scenarios, please note them:

1. **"What's nearby?"** — Player presses a key, gets a list of reachable locations sorted by distance, with compass direction and distance for each. Must filter out locations across water/mountains that can't be reached with current vehicle.

2. **"Go to Balamb Town"** — Player selects a destination, mod auto-steers toward it. Needs: target coordinates, current position, heading calculation, terrain-aware pathfinding (or at least straight-line with obstacle detection).

3. **"What am I near?"** — When approaching a location, automatically announce its name (like the game's popup, but via TTS).

4. **"What can I do here?"** — At a location, announce if it's enterable, if there's a draw point, if it's a chocobo forest, etc.

5. **"Change vehicle"** — Detect when player boards/exits a vehicle and announce the change.

6. **Ragnarok destinations** — Read the autopilot destination list and announce options.

7. **Continent awareness** — "You are on Balamb island. Reachable locations: Balamb Town (northeast, 500 units), Balamb Garden (north, 200 units), Fire Cavern (east, 800 units)."

---

### OUTPUT FORMAT

Please provide answers with:
1. **Specific byte offsets** relative to struct bases or file positions where possible
2. **Data types** (uint8, uint16, uint32, int16, float, etc.)
3. **Address ranges** for the Steam 2013 PC build where known
4. **File paths** within the game's archive structure
5. **Source citations** — which wiki page, tool, or research documented this
6. **Confidence level** — mark anything uncertain as "unconfirmed" vs "confirmed by [source]"
7. **Complete location/draw point lists** with coordinates where available — these become my hardcoded catalog

If exact PC addresses aren't known, provide PSX equivalents with notes on how to translate, or provide the file format so I can parse the data myself from the game files.

Thank you for being as thorough as possible — this research directly enables a blind player to explore FF8's world map independently through audio navigation.
