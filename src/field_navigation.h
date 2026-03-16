// field_navigation.h - Field navigation assistance for blind players
//
// Provides two navigation modes for FF8 Original PC (Steam 2013) field maps:
//
//   Manual mode  — Announces directional guidance toward a selected target.
//                  The player walks; the mod gives compass-style instructions.
//
//   Auto-drive   — Simulates movement input to physically walk the character
//                  to a selected target using walkmesh A* path planning.
//                  (NOT YET IMPLEMENTED — reserved for a later step.)
//
// Targets the player can cycle through:
//   NPCs / interactive actors   (from JSM entity list)
//   Draw points                 (from JSM script scan)
//   Item pickups                (from JSM script scan)
//   Exits / doorways            (from INF gateway section of field DAT)
//
// Architecture (phased):
//   Step 1  — Module scaffold wire-in (this file / initial .cpp). No game data.
//   Step 2  — Runtime address research: player XY, triangle ID, walkmesh ptr.
//   Step 3  — Walkmesh parser (FF7-format triangles + adjacency).
//   Step 4  — A* path planner over triangle graph.
//   Step 5  — Object catalog builder (JSM entity parse + INF gateways).
//   Step 6  — Key bindings and TTS cycling announcements.
//   Step 7  — Manual guidance mode (direction announcements along path).
//   Step 8  — DirectInput COM wrapper for auto-drive input injection.
//   Step 9  — Auto-drive waypoint following + stuck detection.
//   Step 10 — Field transition handling and cache invalidation.
//
// v05.00 target.

#pragma once

#include <cstdint>

namespace FieldNavigation {

// Initialize the field navigation module.
// Safe to call before game addresses are fully resolved; the module will
// detect the field state lazily on the first Update() call.
// Call alongside other module Initialize() functions in dinput8.cpp.
void Initialize();

// Per-frame update — call from the AccessibilityThread loop every tick (~60 Hz).
// Handles key polling, object cycling, guidance announcements, and (eventually)
// auto-drive waypoint following.
// Call BEFORE FieldDialog::PollWindows() so navigation can be suspended when
// a dialog is active.
void Update();

// Shut down and release all resources.
// Call alongside other module Shutdown() functions in dinput8.cpp.
void Shutdown();

// Returns true if the module is currently active (initialized successfully).
bool IsActive();

}  // namespace FieldNavigation
