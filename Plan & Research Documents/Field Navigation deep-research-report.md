# Field Navigation Design and Implementation Plan for FF8 OG (Steam 2013) Accessibility Mod

## Executive summary

This document proposes and justifies a **two-mode field navigation system**—**manual guidance** and **auto-drive**—for the Final Fantasy VIII **original PC edition** (Steam 2013), targeting **field navigation only** (not world map). The design is built around three facts about the FF8/FF7-era field engine:

The field engine places entities on a **walkmesh** (triangulated navigation mesh), and scripting can explicitly set an entity’s position using a **triangle ID plus X/Y** (Z is derived from the triangle). citeturn1search0  
FF8 field assets include an **[ID] walkmesh** file explicitly documented as the **same format as FF7’s walkmesh**, enabling robust pathfinding using a triangle adjacency graph. citeturn3search0turn2search2turn26search0  
FF8 field scripts (JSM) are structured around **entity types (doors/lines/background/other)**, letting us build an “object catalog” for navigation and correlate it with map exits and interactions. citeturn5search2  

### Recommended architecture

**Auto mode** should use **walkmesh-based A\* pathfinding** (triangle graph) and then “drive” the character using **simulated movement input**, not teleportation. This is the most reliable way to avoid hangups and handle corridors, walls, and walkmesh barriers. citeturn26search0turn2search2  

**Manual mode** should provide **directional guidance** (up/down/left/right—optionally diagonals if desired) using the same path plan, plus user-friendly proximity messages (“close”, “in interaction range”). This avoids over-instruction and increases trust.

### Core risks addressed

Auto navigation getting stuck on geometry or NPC collision → mitigated with **walkmesh pathing**, **stuck detection**, and **replan/escape behaviors**. citeturn26search0  
Manual guidance being misleading due to camera perspective → mitigated by guiding along **path waypoints** (not direct line-of-sight), and by using conservative “direction bins” plus periodic updates.  
Exit/trigger ambiguity → mitigated by parsing field **INF gateways/triggers** or deriving exit candidates from script entities, with clear labeling derived from entity type. citeturn2search10turn5search2  

## Field navigation experience and modes

### User-visible controls and flow

This section defines the user experience; the “how” is implemented in later sections.

**Target cycling**  
The mod provides two keys (example: `+` and `-`) to move forward/backward through a list of “field objects” such as:
NPCs / interactive field actors  
Draw points  
Item pickups (chests / ground items)  
Exits (doorways / gateways) citeturn2search10turn5search10turn5search2  

**Target selection state**  
When cycling reaches an object, the mod announces:
Type + label: “NPC”, “Draw point”, “Exit”, etc.  
Index context: “3 of 9”  
Optional hint (if available): triangle/path distance estimate, or “near/far”.

**Modes**
Manual mode: The mod **does not move the character**. It repeatedly announces steering guidance toward the selected target (“move right”, “move down”, etc.).  
Auto mode: The mod **drives movement** by simulating movement input. It should look like normal walking/running, respecting collisions and walkmesh constraints.

**Cancellation**  
Any manual movement input by the player or a dedicated cancel key cancels auto navigation immediately and announces “navigation canceled.”

### Guidance model for manual mode

Manual guidance should be clear and stable:

Base guidance unit: **one primary direction** (up/down/left/right).  
Update cadence: 2–4 times per second (configurable), plus on major direction change.  
Milestones:
“Starting navigation to: …”  
“Turn left / go up …”  
“Close—press confirm to interact.”  
“Arrived.”  

Manual mode should use the path plan (triangle sequence) to pick the **next waypoint direction**, rather than pointing directly at a target behind a wall. This is the single most important accuracy improvement for blind navigation.

### Auto-drive model

Auto-drive should behave like a “light autopilot”:

Uses the same path plan as manual mode.  
Holds movement inputs in short slices (e.g., 50–150 ms decisions), constantly correcting direction.  
Stops when:
Target is in interaction range (or same/adjacent triangle, depending on object type)  
A dialog/menu/cutscene is detected  
Stuck detection triggers and recovery fails beyond a time budget

## Technical data sources in the FF8 field engine

This section identifies *what* to read from the game/files and *why* it is sufficient to implement navigation.

### Walkmesh triangles and connectivity

FF8 field walkmesh is stored in an **ID section/file** documented as “same format as FF7.” citeturn3search0turn2search2  
FF7 walkmesh documentation describes:

A triangle pool (“sectors”), each with 3 vertices (x,y,z), and  
An access pool giving the adjacent triangle across each edge (`0xFFFF` = blocked). citeturn26search0  

This is exactly the structure needed for:

Building a navigation graph (triangles as nodes, adjacency as edges)  
Finding shortest paths with A\*  
Generating waypoints as triangle centroids or edge midpoints

### Field scripting: entities, interactions, and movement primitives

FF8 field scripts are in **JSM**. The header provides counts of entity categories (doors, lines, backgrounds, others) and the entity ordering. citeturn5search2  

This matters because your navigation object list can be grounded in the engine’s own entity taxonomy:

Doors → likely map exits or doorway triggers  
Lines → walkmesh boundaries or special triggers  
Others → NPCs, interactive objects, drawpoints, etc. citeturn5search2  

Movement and state in scripts is implemented through opcodes including:

`SET` and `SET3` (set position with triangle ID and X/Y; Z derived), which confirms the walkmesh coordinate model. citeturn1search0  
`DRAWPOINT` and `SETDRAWPOINT` (draw point interaction/state). citeturn5search10  
`DOORLINEON` / `DOORLINEOFF` (door line toggles). citeturn5search10  
`PGETINFO` can retrieve party member worldspace coordinates, demonstrating that the engine maintains coordinates that can be exposed/used (even if you do not call the opcode directly). citeturn1search6  

### Gateways and triggers (exits)

Field “triggers & gateways” are described as a distinct section (INF) in field DAT layout references, and FF8 field formats list **[INF] Field Gateways**. citeturn2search10turn3search0  

Practical implication: the most robust “exit list” is derived from **INF gateway/triggers** (preferred), or from door entities as a fallback.

## Implementation architecture and algorithms

This is the core technical plan intended to be handed to Claude. It is designed as a **phased implementation**: you can ship value early (object cycling + manual guidance), then harden auto-drive and exits parsing.

### Architecture overview

Add a new module: `FieldNavigation` with responsibilities:

State machine for selection (list, current index, mode, active target)  
Object catalog builder (NPCs/items/drawpoints/exits)  
Walkmesh loader + A\* planner  
Steering controller (manual guidance and auto-drive)  
Safety/cancellation gates (dialog/menu/cutscene)

Integrate it into the existing 60 Hz background tick loop.

**Observed in provided source:** your mod already has a background thread and module update loop in `src/dinput8.cpp`, calling `TitleScreen::Update()` and `FieldDialog::PollWindows()` and sleeping 16 ms. (See “Actionable next steps” for exact line references.)

### Object catalog: what to cycle through

You need a canonical list of objects with at least:

`type` (npc / drawpoint / item / exit / unknown)  
`stable_id` (for de-dup and user cycling stability)  
`target_triangle` (triangle on walkmesh)  
Optional `label` / `hint`

#### Preferred derivation strategy

Use a **hybrid** approach:

Parse **JSM** to get entity counts/types and stable ordering. citeturn5search2  
Parse or decode exit geometry from **INF gateways/triggers** (exits). citeturn2search10turn3search0  
Use **runtime state** (where possible) for “which entities are currently active/interactable” (e.g., doorline enabled, drawpoint depleted, etc.). `SETDRAWPOINT` suggests stateful availability. citeturn5search10  

If INF parsing is not available immediately, ship a first version that includes:
Other entities from JSM (likely NPC/object interactions)  
Known draw points derived from scripts (presence of `DRAWPOINT`) citeturn5search10  
Exits as “door entities” with best-effort target triangles (may initially be “unknown location” until INF is added) citeturn5search2  

#### Table: object types, minimum data, best source

| Object type | Why it matters | Minimum needed for nav | Primary source | Fallback source |
|---|---|---|---|---|
| NPC / interactive object | Talk/interaction targets | target triangle (and/or XY) + stable id | JSM “other entity” + runtime active flags citeturn5search2 | JSM-only “other entity” list |
| Draw point | Core FF8 interaction | availability + target triangle | Script opcode presence (`DRAWPOINT` / `SETDRAWPOINT`) + runtime state citeturn5search10 | Script-only (always list; mark “availability unknown”) |
| Item pickup | Loot / progression | target triangle | Script scan for item-related triggers (map-specific) | Runtime detection of interaction windows/UI (weak) |
| Exit / doorway | Progression between rooms | gateway geometry + target triangle | INF gateways/triggers citeturn2search10turn3search0 | Door entities + script `DOORLINEON` hints citeturn5search10turn5search2 |

### Walkmesh loading and A* path planning

#### Walkmesh parsing

Because FF8 ID is “same format as FF7,” implement the FF7 triangle+access format parser. citeturn2search2turn26search0  

From the FF7 walkmesh structure:

Read `NoS` (# triangles)  
Read triangle vertex triples  
Read the access pool adjacency (3 neighbors per triangle; `0xFFFF` blocked) citeturn26search0  

Then build adjacency:

```cpp
struct Tri {
  Vec3 v[3];
  uint16_t neigh[3]; // 0xFFFF = none
  Vec3 centroid() const { return (v[0]+v[1]+v[2]) / 3.0f; }
};
```

Edge weights: distance between centroids (good enough), or uniform weight for speed.

#### A* planning

Inputs:
start_tri = player triangle id  
goal_tri = target triangle id  

Output:
vector<uint16_t> tri_path

Pseudocode:

```cpp
vector<uint16_t> AStar(uint16_t start, uint16_t goal, const vector<Tri>& tris) {
  auto h = [&](uint16_t t) {
    return distance(tris[t].centroid(), tris[goal].centroid());
  };

  // open set: (f_score, tri)
  priority_queue<Node> open;
  vector<float> g(tris.size(), INF), f(tris.size(), INF);
  vector<int> came(tris.size(), -1);

  g[start] = 0;
  f[start] = h(start);
  open.push({f[start], start});

  while (!open.empty()) {
    auto [fcur, cur] = open.pop_min();
    if (cur == goal) return reconstruct(came, goal);

    for (int e=0; e<3; e++) {
      uint16_t nxt = tris[cur].neigh[e];
      if (nxt == 0xFFFF) continue;

      float tentative = g[cur] + distance(tris[cur].centroid(), tris[nxt].centroid());
      if (tentative < g[nxt]) {
        came[nxt] = cur;
        g[nxt] = tentative;
        f[nxt] = tentative + h(nxt);
        open.push({f[nxt], nxt});
      }
    }
  }
  return {}; // unreachable
}
```

Performance: NoS (triangle count) in a single field is typically modest; A\* should complete quickly (milliseconds) under normal conditions, and should only re-run on:
New target selection  
Stuck recovery  
Field load / transition

### Steering and movement control

#### Waypoint derivation

Convert `tri_path` to `waypoints` as centroids:

```cpp
for tri in tri_path:
  waypoints.push_back(tris[tri].centroid());
```

Optional improvement: use edge midpoint between consecutive triangles to reduce corner-cutting.

#### Manual guidance direction selection

At each tick:

current_pos estimate: centroid of current player triangle (or real XY if known)  
next_wp = waypoints[current_index] (closest forward waypoint)

Compute vector `d = next_wp - current_pos`.

Map to directions:

If |d.x| > |d.y|: “left/right” else “up/down”  
(Optionally include diagonals if you want better guidance.)

Announce only when:
Direction changes, or  
Timer (e.g., 500ms) elapsed

#### Auto-drive control

Auto-drive emits directional inputs toward the next waypoint.

Key behaviors:

Arrive: when within distance threshold or when player reaches next triangle in path, advance waypoint.  
Stop: when in goal triangle and within interaction distance threshold.  
Cancel: on user input or dialog state.

##### Stuck detection

“Stuck” if:
Player triangle id does not change for N ticks (e.g., 60 ticks = 1 second), **and**  
Distance to next waypoint does not decrease beyond epsilon

Recovery:
Try slight “wiggle” pattern (right 200ms → left 200ms → down 200ms)  
Replan A\* from current triangle to goal  
If still stuck after T seconds, abort and announce “can’t reach target from here”

### Input simulation strategy

Auto navigation requires actually driving the game. The most robust approach for your architecture (a dinput8 proxy) is:

Add a lightweight **DirectInput COM wrapper** that intercepts keyboard `GetDeviceState` and ORs in desired movement keys when auto-drive is active.

Rationale: This avoids relying on internal FF8 button masks, and it respects the game’s DirectInput polling.

If you cannot implement the wrapper immediately, a fallback is `SendInput`, but DirectInput games can be inconsistent with it; the COM-wrapper approach is preferred.

Practical design:
Maintain an atomic “injected direction state” (up/down/left/right).  
On `GetDeviceState` for the keyboard, when auto-drive active, set DIK codes for movement keys.

You should make movement keys configurable (arrow keys vs WASD) because keyboard bindings vary.

## Diagnostics and test plan

### Logging format for navigation debugging

Use structured, single-line logs that support quick diffing:

Field transitions:
`FIELDNAV field="<name>" loaded walkmesh_tris=<N> objects=<M>`

Object cycling:
`FIELDNAV select idx=<i>/<M> type=<type> id=<stable_id> tri=<t> label="<label>"`

Path planning:
`FIELDNAV plan start_tri=<s> goal_tri=<g> path_len=<k> status=<ok|unreachable>`

Steering tick (throttled to e.g. 5 Hz):
`FIELDNAV tick mode=<manual|auto> cur_tri=<c> wp_i=<w> dir=<U|D|L|R> dist=<d> stuck=<0|1>`

Arrival:
`FIELDNAV arrived id=<stable_id> tri=<t> action_hint="<press confirm>"`

### Test plan

#### Basic selection

Go to an early field with multiple NPCs and an exit.  
Expected:
`FIELDNAV ... objects=<M>` logged on entry.  
Cycling announces “NPC 1 of M”, etc.

#### Manual guidance correctness

Pick a nearby NPC.  
Expected:
Direction calls should change logically as you round corners.  
When close, announcement “Close—press confirm.”

Log signature:
`tick mode=manual ... dir=... dist=...` decreasing overall; direction changes at choke points.

#### Auto-drive robustness

Pick an NPC across the room requiring turns.  
Expected:
Auto-drive reaches the target without running into walls endlessly.  
If it collides with an NPC, either it navigates around or triggers stuck recovery and replans.

Log signature:
Triangles should change along path:
`plan ... path_len=k`  
Then `tick ... cur_tri=` advancing; eventually `arrived`.

#### Stuck recovery validation

Choose a target behind a blocked edge (unreachable area), or interrupt movement.  
Expected:
Auto-drive attempts recovery, replans, and then aborts with a clear message.

Log signature:
`plan ... status=unreachable` OR repeated stuck=1 and then abort.

## Actionable next steps checklist with provided-source insertion points

All items below reference your attached mod source as **observed in provided source**.

### Add a new module scaffold

Create:
`src/field_navigation.h`  
`src/field_navigation.cpp`

Expose:
`FieldNavigation::Initialize()`  
`FieldNavigation::Update()`  
`FieldNavigation::Shutdown()`  

### Integrate module into the background thread loop

In `src/dinput8.cpp`:

Add initialization call near existing module initialization:

Observed lines:
`FieldDialog::Initialize();` at **line 81** (observed in provided source).  
Insert:
`FieldNavigation::Initialize();` **after line 81** (or before, but keep grouping consistent).

Add per-tick update inside the loop:

Observed:
`FieldDialog::PollWindows();` at **line 126** (observed in provided source).  
Insert:
`FieldNavigation::Update();` **before line 126** so navigation can be suspended if a dialog is detected.

Add shutdown call in cleanup:

Observed:
`FieldDialog::Shutdown();` at **line 134** (observed in provided source).  
Insert:
`FieldNavigation::Shutdown();` near other module shutdowns.

### Extend the umbrella header for module inclusion

In `src/ff8_accessibility.h`:

Observed:
`#include "field_dialog.h"` at **line 83** (observed in provided source).  
Add:
`#include "field_navigation.h"` after line 83.

### Decide and implement input simulation

Preferred:
Implement a DirectInput COM wrapper inside `src/dinput8.cpp` (or split into a new `dinput_wrapper.cpp`) that intercepts keyboard `GetDeviceState`.

If you ship without it initially:
Ship manual mode first (no injection), and keep auto mode behind a compile-time flag.

### Implement walkmesh parsing and planning

Implement walkmesh parser based on FF7 walkmesh format (triangles + access pool). citeturn26search0turn2search2  

Implement A\* as described above and cache the parsed mesh per field load.

### Implement minimal object catalog (phase one)

Phase one object catalog:
Use JSM entity typing and ordering as a stable index basis. citeturn5search2  
Add draw point detection via presence of `DRAWPOINT` opcode where possible. citeturn5search10  
Mark exits as “door entities” until INF parsing is added. citeturn2search10turn5search2  

### Add instrumentation and confirm on-device behavior

Implement the logging formats above and validate:

Object list populates on field entry  
Paths plan and triangles advance during auto-drive  
Manual guidance changes directions at sensible times

If any required detail (field name resolution, INF parsing, runtime triangle ID reads) is not yet wired, mark it in code as TODO and ensure logs explicitly state “UNSPECIFIED / NOT IMPLEMENTED” rather than failing silently.