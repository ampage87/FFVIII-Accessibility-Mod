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

// ============================================================================
// v0.15.9.1: Direction-based auto-drive for chase scenes
// ============================================================================
//
// Activates the fake gamepad + analog override with a fixed screen-relative
// direction (lX/lY = dirX*1000 / dirY*1000). Used by chase_auto_pilot to
// drive the party through chase fields without engaging path-finding
// auto-drive.
//
// dirX / dirY are screen-relative direction signs in {-1, 0, +1}:
//   dirX = +1 -> screen right    dirX = -1 -> screen left
//   dirY = +1 -> screen down     dirY = -1 -> screen up
//
// walk=true also injects W (cancel scancode 0x11) via SendInput so the party
// walks instead of runs. FF8 PC default keymap: cancel = W = walk modifier
// on foot.
//
// Mutually exclusive with the F9 path-finding auto-drive: refuses to start
// if path-finding is active; the F9 handler refuses to start if direction-
// drive is active. Calling StartDirectionDrive while already running
// updates the direction values + walk state in place (cheap; chase_auto_pilot
// can call this every tick safely).
void StartDirectionDrive(int8_t dirX, int8_t dirY, bool walk);

// Releases all keys held by direction-drive (arrows + W if walking),
// deactivates analog override, removes the fake gamepad. Idempotent.
void StopDirectionDrive();

// True while direction-drive is currently active.
bool IsDirectionDriveActive();

// ============================================================================
// v0.15.9.2: Chase auto-pilot path-finding drive
// ============================================================================
//
// Starts the F9 path-finding auto-drive toward an explicit target position
// (targetX, targetY in walkmesh world coords), without using the entity
// catalog. Used by chase_auto_pilot to drive the party through chase
// fields with full walkmesh-aware navigation, A*+funnel pathing, and
// stuck-detection / wiggle recovery.
//
// walk=true holds W (cancel scancode 0x11) so the party walks instead
// of runs. FF8 PC default keymap.
//
// Returns true if the drive started successfully (path computed, fake
// gamepad installed, arrow + W held); false if prerequisites failed
// (F9 already active, dialog open, off-field, or path computation
// failed).
//
// Mutually exclusive with the F9 path-finding auto-drive triggered by
// the backslash hotkey: only one path-finding drive can run at a time.
// Direction-drive (StartDirectionDrive) is also mutually exclusive --
// chase auto-pilot routes to one or the other per-field via its config
// table.
bool StartChaseDrive(int32_t targetX, int32_t targetY,
                     int triggerLineIdx,
                     int32_t crossLineX1, int32_t crossLineY1,
                     int32_t crossLineX2, int32_t crossLineY2,
                     bool walk);

// Stops the chase-drive: releases W if held, releases held arrows,
// deactivates analog override, removes the fake gamepad. Idempotent.
void StopChaseDrive();

// True while chase-drive is currently active.
bool IsChaseDriveActive();

// ============================================================================
// v0.15.9.2.6: Walkmesh cluster query for chase auto-pilot fallback
// ============================================================================
//
// Returns the center of the largest cluster found by the dead-end scanner
// at field-load time (HookedFieldScriptsInit's BFS through 1-2 neighbor
// triangles). "Largest" = highest triCount across all clusters; ties broken
// by first found.
//
// Used by chase_auto_pilot to pick a default MODE_TARGET destination on chase
// fields that aren't in its explicit per-field config table. The largest
// cluster is typically the main corridor or south exit area; the path-finder
// figures out the route from the player's current triangle.
//
// Returns true and fills *outX, *outY on success. Returns false if the
// walkmesh wasn't loaded for this field, if the scanner found no clusters,
// or if outX/outY is null.
bool GetLargestClusterCenter(int32_t* outX, int32_t* outY);

// ============================================================================
// v0.15.9.2.14: Trigger-line lookup for chase auto-pilot fallback
// ============================================================================
//
// Finds the SETLINE-defined trigger line (lineType=SCREEN_BOUND or UNKNOWN)
// whose center is closest to the largest dead-end cluster center. Used by
// chase-drive's fallback to target an actual screen-transition trigger
// instead of a walkmesh dead-end cluster.
//
// Why both: the cluster center heuristic finds the "deepest" pocket of the
// walkmesh (e.g., south of domt2_1's 42-triangle south cluster centered at
// (276,-728)). The screen-transition trigger lives at the boundary of that
// pocket -- the SETLINE coords are typically within a few hundred units of
// the cluster center. Picking the trigger line nearest the cluster combines
// "go to the deep pocket" with "actually cross the line."
//
// Once we have the trigger index, chase-drive uses it for:
//   - Cross-product sign-flip detection (s_driveTrigCrossStart) so the
//     drive stops the instant the player crosses the line, not when they
//     get "close enough" to a point.
//   - Heading offset (the crossing-check block in UpdateAutoDrive shoves
//     the steering target 300 units past the line center so the player's
//     momentum carries them across).
//   - A* avoidance exemption (via s_driveSkipTrigIdx) so the path planner
//     doesn't try to route AROUND the target line.
//
// Returns true and fills *outX, *outY with the trigger line center, plus
// *outTrigIdx with the index into s_capturedLines[]. Returns false if no
// clusters were found, no trigger lines are active, or any pointer is null.
bool GetTriggerLineNearestCluster(int32_t* outX, int32_t* outY, int* outTrigIdx);

// ============================================================================
// v0.15.9.2.15: INF gateway lookup for chase auto-pilot fallback
// ============================================================================
//
// Finds the INF gateway whose direction-from-player aligns with the
// direction-from-player toward the largest dead-end cluster. INF gateways are
// the engine's native screen-transition exits with explicit destination field
// IDs; they're the authoritative "exit" mechanism on chase fields whose
// SETLINE entities are Event Triggers (battle/animation), not screen bounds.
//
// Why direction-aligned rather than nearest-to-cluster: a chase field has TWO
// gateways -- one back where the party came from, one forward to the next
// chase field. The cluster center sits in the south-corridor pocket; "nearest
// to cluster" can pick the wrong gateway (e.g., on domt2_1 the north-entry
// gateway at (300,1588) is closer to the cluster (276,-728) than the south-
// exit gateway at (-93,-3414)). The direction test uses player position as the
// pivot: forward-progress gateways have a positive dot product with the
// player->cluster vector; reverse-direction (entry-back) gateways are negative.
//
// Once we have the gateway, chase-drive uses it for:
//   - Navigation target: gateway center (the visible "go here" point).
//   - Cross-product sign-flip detection on the gateway's line endpoints
//     (returned via outLineX1/Y1/X2/Y2). The drive stops the instant the
//     player physically crosses the line -- which is what fires FF8's screen
//     transition.
//   - 300-unit heading offset past the line center so player momentum carries
//     them through (handled in UpdateAutoDrive's crossing block).
//
// Returns true and fills *outX, *outY (gateway center) plus *outLine[X1Y1X2Y2]
// (full line endpoints for crossing detection). Returns false if no INF
// gateways were loaded for this field, the largest cluster lookup failed, the
// player position isn't readable, or any pointer is null.
bool GetGatewayNearestCluster(int32_t* outX, int32_t* outY,
                              int32_t* outLineX1, int32_t* outLineY1,
                              int32_t* outLineX2, int32_t* outLineY2);

// v0.106.0 (#bahamut-light): THE MOD'S GAME CONTROLS BOX, for modules that live
// in another translation unit.
//
// Every mini-game that has one of these -- the space rescue, the Garden battle,
// the dragon -- opens it the same way: `GardenBattle::OpenBriefDialog` puts the
// text in FF8'S OWN dialog window, which is what makes it look like the rest of
// the game rather than like a mod, and `FieldOverlay::Show` is the fallback for
// the build or the moment where that window will not open. Both of those live
// inside field_navigation.cpp's .inl chain and are static, so a module outside
// it cannot reach them; this pair is that same sequence behind one name.
//
// Returns true if the game's own window took it, false if the overlay did.
// Either way something is on the screen unless both refused.
bool OpenControlsBox(const char* text);
void CloseControlsBox();

}  // namespace FieldNavigation
