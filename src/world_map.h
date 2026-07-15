// world_map.h - World map navigation TTS for blind players
//
// v0.11.03: Initial implementation — location catalog, cycling, compass
//
// Uses the same UX pattern as field navigation:
//   - / = : Cycle through nearby locations
//   Backspace : Announce bearing + distance to selected location
//   \         : Auto-drive toward selected location (future)
//
// Location list is generated on world map entry, sorted by distance,
// and stays frozen until the player leaves and re-enters the world map.

#pragma once

namespace WorldMap {

void Initialize();
void Update();      // Called every ~16ms from accessibility thread
void Shutdown();

// #67: F12 live-facing discovery diagnostic (defined in
// world_map_heading_scan.inl). RETIRED v0.18.3.73 (HEADING_SCAN_DIAG 0) --
// kept declared so the gated-off file still links; no longer the F12 handler.
void TriggerHeadingScan();

// #67: F12 world-map camera-control discovery diagnostic (defined in
// world_map_camera_scan.inl). RETIRED (CAMERA_SCAN_DIAG 0) -- kept declared
// so the gated-off file still links; no longer the F12 handler.
void TriggerCameraScan();

// #79 v0.18.3.255: F12 vehicle-state dump (defined in world_map_segments.inl).
// Current F12 handler (per-session diagnostic key). Logs [VEHDUMP]: player
// position, savemap per-vehicle position mirrors (char/rag/bgu/car), the
// animation byte at 0x02040A5E, car_rent, and a hex window -- so a
// car-state vs foot-state diff identifies the authoritative vehicle signal
// at world-map entry. Also fired automatically on every world-map entry.
void TriggerVehicleDump();

}  // namespace WorldMap
