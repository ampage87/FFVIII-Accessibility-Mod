# World-map location coordinates for an accessibility mod of Final Fantasy VIII 2013 Steam edition

## Executive summary

This research located a complete, community-maintained list of enterable/marker-based world-map destinations (the same set commonly referenced when using the Ragnarok’s “large world map” point-selection) and matched every item on that list to world-map coordinates usable in a mod. The location list itself is available as a numbered “World Map” key (1–26) with grouped items for Chocobo Forests and Alien/UFO encounters. citeturn25view0

For the 2013 Steam (PC) build, the most direct, mod-friendly coordinate system is the in-memory **World Map Coord X/Y/Z** triple documented by the speedrunning community as 4-byte values at fixed offsets from `FF8_EN.exe` (with alternative offsets for other language executables such as `FF8_FR.exe`). citeturn37view0 A companion variable, **World map camera direction**, is documented as a 2-byte value where **0 = North** and the range runs **0–4095**, which is useful for orientation cues in an accessibility layer. citeturn37view0

All 26 numbered world-map markers + the grouped Chocobo Forest and Alien Encounter points were found with explicit X/Y/Z values in a speedrun-oriented memory research repository; no per-item conversion from other versions was required for these targets. citeturn36view0turn37view0

## Locating the world-map locations list used for Ragnarok navigation

### Source located for the location list

The clearest “one-page” list of world-map locations (with a keyed map and a numbered item list) is on **Final Fantasy Kingdom**’s “Final Fantasy VIII World Map” page. It provides the canonical **numbered marker set (1–26)** and explicitly groups **A = Chocobo Forests** and **B = Alien Encounters** as additional world-map items. citeturn25view0

This page is not branded as a “Ragnarok speedrun” resource, but the list corresponds directly to the well-known set of world-map markers players navigate to (and speedrunners reference) once Ragnarok point-selection becomes available. The speedrunning community’s memory research repo includes coordinates for the same marker set, strongly supporting that we are matching the intended list. citeturn25view0turn36view0

### Canonicalized version of the list

To make the list mod-friendly, the names below are normalized into a canonical form (typos corrected, but semantic identity preserved). Two normalization changes are especially important:

- The Final Fantasy Kingdom list spells **“Esthar”** as **“Esther”**; this report uses **Esthar** consistently. citeturn25view0  
- “Tears Point” is often stylized as **“Tears’ Point”** elsewhere; this report uses **Tears Point** as the canonical key while noting the alternate spelling. citeturn25view0turn16search1  

Because your requirement includes “copy of the list,” the coordinate table later in this report reproduces the canonicalized numbered marker list (1–26) in the same ordering as the source list, plus expanded A/B groupings.

image_group{"layout":"carousel","aspect_ratio":"16:9","query":["Final Fantasy VIII world map locations numbered","Final Fantasy VIII world map","Final Fantasy VIII chocobo forest map","Final Fantasy VIII UFO PuPu locations map"],"num_per_query":1}

## Coordinate system and how it is obtained in the 2013 Steam build

### Coordinate format recommended for a mod

The most directly usable coordinate system for an accessibility mod (for “where am I?” and “how far to destination?”) is the **World Map Coord X / Y / Z** triple recorded in the speedrun memory-offset repository:

- `FF8_EN.exe+1C3EE80` = World Map Coord X (4 bytes)  
- `FF8_EN.exe+1C3EE84` = World Map Coord Y (4 bytes)  
- `FF8_EN.exe+1C3EE88` = World Map Coord Z (4 bytes) citeturn37view0  

The same repo documents offsets for at least one additional language executable (`FF8_FR.exe`) for the same variables, which matters if your mod targets non-English Steam installs. citeturn37view0

**Assumption about numeric type:** the repository describes these as **4 bytes** but does not explicitly label them as int vs float at the point the world-map coords are listed. This report treats them as **signed 32-bit numeric world units** (practically used as integers in the published coordinate list). citeturn37view0turn36view0

### Orientation aid: camera direction

For accessible navigation instructions (e.g., “turn left 30 degrees”), a useful auxiliary variable is:

- `FF8_EN.exe+1C3ED02` = World map camera direction (2 bytes), documented range **0 (North) – 4095** citeturn37view0  

The Qhimm modding wiki also documents multiple world-map camera variables and explicitly notes that the presented opcode work is for the Steam release, reinforcing that this memory-oriented approach is valid in the Steam PC lineage. citeturn26view0

### What Z means

The coordinate list that includes these world-map points carries this note: **“Z = 200 appears to be sea level.”** citeturn36view0  
However, many land locations have Z values well below 200 (often negative), so **Z should be treated cautiously** unless you validate its sign convention in-engine. For most accessibility navigation tasks, X/Y alone are sufficient.

## Canonicalized location list with world-map coordinates

### Provenance of coordinates

All coordinates in the tables below are taken from `ff8-speedruns/ff8-memory` under `world-map.md`, which enumerates “Specific locations” as (X, Y, Z) triples. citeturn36view0

This dataset includes:

- Every numbered world-map marker (1–26) from the source list citeturn25view0turn36view0  
- All 7 Chocobo Forest points (expanding the source list’s grouped “Chocobo Forests”) citeturn25view0turn36view0  
- All 4 Alien Encounter points (expanding the source list’s grouped “Alien Encounters”) citeturn25view0turn36view0  

### Location coordinates table

All rows use the same coordinate system: **World Map X/Y/Z** from the Steam PC memory coordinate set. citeturn36view0turn37view0  
Confidence is “High” when the canonicalized name is an unambiguous match to the coordinate dataset entry.

|#|Location|X|Y|Z|Coord format|Confidence|Notes|
|---|---|---|---|---|---|---|---|
|1|entity["place","Balamb Garden","ff8 world map"]|24576|-29406|-658|WM X/Y/Z|High||
|2|entity["place","Balamb","ff8 world map"]|13249|-26779|-304|WM X/Y/Z|High|Balamb Town marker in-game; often labeled just “Balamb”.|
|3|entity["place","Dollet","ff8 world map"]|-15639|-39437|-172|WM X/Y/Z|High||
|4|entity["place","Timber","ff8 world map"]|-22564|-4867|-700|WM X/Y/Z|High||
|5|entity["place","Galbadia Garden","ff8 world map"]|-37471|-25062|-573|WM X/Y/Z|High||
|6|entity["place","Deling City","ff8 world map"]|-61806|-28649|-892|WM X/Y/Z|High||
|7|entity["place","Tomb of the Unknown King","ff8 world map"]|-42471|-36562|-228|WM X/Y/Z|High||
|8|entity["place","D-District Prison","ff8 world map"]|-55306|-4841|-199|WM X/Y/Z|High||
|9|entity["place","Galbadia Missile Base","ff8 world map"]|-71695|-15591|-364|WM X/Y/Z|High||
|10|entity["place","Fisherman's Horizon","ff8 world map"]|48811|-1653|-430|WM X/Y/Z|High||
|11|entity["place","Trabia Garden","ff8 world map"]|48893|-57979|-800|WM X/Y/Z|High||
|12|entity["place","Edea's House","ff8 world map"]|-23150|62853|-648|WM X/Y/Z|High||
|13|entity["place","White SeeD Ship","ff8 world map"]|4887|51285|-480|WM X/Y/Z|High|Sometimes written “White Seed Ship”.|
|14|entity["place","Great Salt Lake","ff8 world map"]|49888|-2683|-333|WM X/Y/Z|High||
|15|entity["place","Esthar","ff8 world map"]|57011|-2295|-297|WM X/Y/Z|High|Source list spells this as “Esther”; canon is Esthar.|
|16|entity["place","Lunatic Pandora Lab","ff8 world map"]|79521|-9135|-570|WM X/Y/Z|High|Also called “Lunatic Pandora Laboratory”.|
|17|entity["place","Lunar Gate","ff8 world map"]|88021|7865|-328|WM X/Y/Z|High||
|18|entity["place","Sorceress Memorial","ff8 world map"]|81521|11865|-460|WM X/Y/Z|High||
|19|entity["place","Shumi Village","ff8 world map"]|10362|-76967|-845|WM X/Y/Z|High||
|20|entity["place","Winhill","ff8 world map"]|-50285|6320|-385|WM X/Y/Z|High||
|21|entity["place","Centra Ruins","ff8 world map"]|6887|55285|-582|WM X/Y/Z|High||
|22|entity["place","Deep Sea Research Center","ff8 world map"]|-119138|86000|324|WM X/Y/Z|High||
|23|entity["place","Cactuar Island","ff8 world map"]|54806|62040|-618|WM X/Y/Z|High||
|24|entity["place","Tears Point","ff8 world map"]|83021|31865|-347|WM X/Y/Z|High|Often written “Tears’ Point” (apostrophe).|
|25|entity["place","Island Closest To Hell","ff8 world map"]|-105137|-3802|-483|WM X/Y/Z|High||
|26|entity["place","Island Closest To Heaven","ff8 world map"]|102251|-53082|-467|WM X/Y/Z|High||

### Expanded grouped entries table

The source location list groups these as **A = Chocobo Forests** and **B = Alien Encounters** rather than enumerating individual coordinates. citeturn25view0  
The speedrun coordinate dataset provides the individual points below. citeturn36view0

**Chocobo Forest points**

|#|Location|X|Y|Z|Coord format|Confidence|Notes|
|---|---|---|---|---|---|---|---|
|A1|entity["place","Chocobo Forest (1/7)","ff8 world map"]|11332|-63659|-632|WM X/Y/Z|High|From consolidated “Chocobo Forests” marker list.|
|A2|entity["place","Chocobo Forest (2/7)","ff8 world map"]|10927|-81010|-885|WM X/Y/Z|High|From consolidated “Chocobo Forests” marker list.|
|A3|entity["place","Chocobo Forest (3/7)","ff8 world map"]|51893|-3959|-795|WM X/Y/Z|High|From consolidated “Chocobo Forests” marker list.|
|A4|entity["place","Chocobo Forest (4/7)","ff8 world map"]|97253|-48250|-831|WM X/Y/Z|High|From consolidated “Chocobo Forests” marker list.|
|A5|entity["place","Chocobo Forest (5/7)","ff8 world map"]|17383|22013|-436|WM X/Y/Z|High|From consolidated “Chocobo Forests” marker list.|
|A6|entity["place","Chocobo Forest (6/7)","ff8 world map"]|44504|76259|-222|WM X/Y/Z|High|From consolidated “Chocobo Forests” marker list.|
|A7|entity["place","Chocobo Forest (7/7)","ff8 world map"]|-20953|68906|-435|WM X/Y/Z|High|From consolidated “Chocobo Forests” marker list.|

**Alien encounter points**

|#|Location|X|Y|Z|Coord format|Confidence|Notes|
|---|---|---|---|---|---|---|---|
|B1|entity["place","Alien Encounter (1/4)","ff8 world map"]|79823|-61212|-459|WM X/Y/Z|High|From consolidated “Alien Encounters” marker list.|
|B2|entity["place","Alien Encounter (2/4)","ff8 world map"]|40495|54649|-494|WM X/Y/Z|High|From consolidated “Alien Encounters” marker list.|
|B3|entity["place","Alien Encounter (3/4)","ff8 world map"]|-12952|-10202|-6|WM X/Y/Z|High|From consolidated “Alien Encounters” marker list.|
|B4|entity["place","Alien Encounter (4/4)","ff8 world map"]|-48806|5808|-476|WM X/Y/Z|High|From consolidated “Alien Encounters” marker list.|

### Copyable dataset export

CSV (canonical markers 1–26 + expanded A/B groupings):

```csv
id,name,x,y,z,coord_format
1,Balamb Garden,24576,-29406,-658,world map X/Y/Z
2,Balamb,13249,-26779,-304,world map X/Y/Z
3,Dollet,-15639,-39437,-172,world map X/Y/Z
4,Timber,-22564,-4867,-700,world map X/Y/Z
5,Galbadia Garden,-37471,-25062,-573,world map X/Y/Z
6,Deling City,-61806,-28649,-892,world map X/Y/Z
7,Tomb of the Unknown King,-42471,-36562,-228,world map X/Y/Z
8,D-District Prison,-55306,-4841,-199,world map X/Y/Z
9,Galbadia Missile Base,-71695,-15591,-364,world map X/Y/Z
10,Fisherman's Horizon,48811,-1653,-430,world map X/Y/Z
11,Trabia Garden,48893,-57979,-800,world map X/Y/Z
12,Edea's House,-23150,62853,-648,world map X/Y/Z
13,White SeeD Ship,4887,51285,-480,world map X/Y/Z
14,Great Salt Lake,49888,-2683,-333,world map X/Y/Z
15,Esthar,57011,-2295,-297,world map X/Y/Z
16,Lunatic Pandora Lab,79521,-9135,-570,world map X/Y/Z
17,Lunar Gate,88021,7865,-328,world map X/Y/Z
18,Sorceress Memorial,81521,11865,-460,world map X/Y/Z
19,Shumi Village,10362,-76967,-845,world map X/Y/Z
20,Winhill,-50285,6320,-385,world map X/Y/Z
21,Centra Ruins,6887,55285,-582,world map X/Y/Z
22,Deep Sea Research Center,-119138,86000,324,world map X/Y/Z
23,Cactuar Island,54806,62040,-618,world map X/Y/Z
24,Tears Point,83021,31865,-347,world map X/Y/Z
25,Island Closest To Hell,-105137,-3802,-483,world map X/Y/Z
26,Island Closest To Heaven,102251,-53082,-467,world map X/Y/Z
A1,Chocobo Forest (1/7),11332,-63659,-632,world map X/Y/Z
A2,Chocobo Forest (2/7),10927,-81010,-885,world map X/Y/Z
A3,Chocobo Forest (3/7),51893,-3959,-795,world map X/Y/Z
A4,Chocobo Forest (4/7),97253,-48250,-831,world map X/Y/Z
A5,Chocobo Forest (5/7),17383,22013,-436,world map X/Y/Z
A6,Chocobo Forest (6/7),44504,76259,-222,world map X/Y/Z
A7,Chocobo Forest (7/7),-20953,68906,-435,world map X/Y/Z
B1,Alien Encounter (1/4),79823,-61212,-459,world map X/Y/Z
B2,Alien Encounter (2/4),40495,54649,-494,world map X/Y/Z
B3,Alien Encounter (3/4),-12952,-10202,-6,world map X/Y/Z
B4,Alien Encounter (4/4),-48806,5808,-476,world map X/Y/Z
```

## Gaps, ambiguities, confidence assessment

### Unresolved or missing items table

All 26 numbered markers in the located list have explicit world-map coordinates in the speedrun coordinate dataset, and both grouped categories (Chocobo Forests, Alien Encounters) are resolved into individual coordinate points. citeturn25view0turn36view0

|Item from list|Status|Evidence|Notes|
|---|---|---|---|
|None|No missing coordinates|FinalFantasyKingdom list matched to ff8-memory coordinate set citeturn25view0turn36view0|Name normalization applied (e.g., Esther → Esthar).|

### Ambiguities that matter for implementation

The only practical ambiguities are naming/labeling issues rather than “unknown coordinates”:

- “Balamb” in the marker list refers to the **town marker**, not the continent; the coordinate is therefore treated as the town’s world-map marker. citeturn25view0turn36view0  
- Esthar is misspelled as “Esther” in the numbered list; coordinate mapping uses the canonical spelling. citeturn25view0turn36view0  
- Tears Point is often written with an apostrophe; your mod may want to treat these as the same canonical destination string. citeturn16search1turn36view0  

### Version notes and conversion assumptions

- The **coordinates themselves** in this report are already in a Steam-PC world-map coordinate system and do not require conversion to be used with the in-memory World Map Coord X/Y/Z variables documented in the same repository. citeturn36view0turn37view0  
- For **other releases** (e.g., different PC builds or newer ports), the **coordinate system may be identical** at the game-data level, but **memory offsets will differ**. This report avoids claiming “universal” offsets and only asserts offsets for the executables explicitly documented (e.g., `FF8_EN.exe`, `FF8_FR.exe`). citeturn37view0turn26view0  

### Notes on alternative “Ragnarok speedrun list” sources

The speedrun community’s `ff8.wiki` website appears in speedrun community pages, but direct access to its root site returned a 403 in this research environment. citeturn13view0  
Because of that, this report anchors the **list** to FinalFantasyKingdom’s explicit numbered marker key and anchors the **coordinates** to the accessible speedrun memory repository.

```mermaid
flowchart LR
  A[FinalFantasyKingdom world map key (1–26, A, B)] --> B[Canonicalized location list]
  C[ff8-speedruns/ff8-memory world-map.md] --> D[World map X/Y/Z for each point]
  E[ff8-speedruns/ff8-memory README offsets] --> F[In-memory X/Y/Z & camera direction vars]
  B --> G[Join: location -> coord]
  D --> G
  F --> H[Accessibility mod reads player X/Y/Z]
  G --> I[Navigation: distance + bearing + nearest target]
  H --> I
```
citeturn25view0turn36view0turn37view0turn26view0

## Appendix source URLs

```text
FinalFantasyKingdom – Final Fantasy VIII World Map (numbered location list)
https://www.finalfantasykingdom.net/finalfantasyviiiworldmaps.php

ff8-speedruns/ff8-memory – world-map.md (world map coordinates list)
https://github.com/ff8-speedruns/ff8-memory/blob/main/world-map.md

Raw world-map.md (useful for scripting ingestion)
https://raw.githubusercontent.com/ff8-speedruns/ff8-memory/refs/heads/main/world-map.md

ff8-speedruns/ff8-memory – README.md (Steam PC memory offsets including World Map Coord X/Y/Z)
https://github.com/ff8-speedruns/ff8-memory/blob/main/README.md

Qhimm Modding Wiki – FF8 world map camera (Steam-oriented memory notes/opcodes)
https://qhimm-modding.fandom.com/wiki/FF8/Engine/WorldMapCamera
```