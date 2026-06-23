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
// world_map_camera_scan.inl). Triggered from dinput8.cpp's F12 handler.
// Injects the real G/H camera-rotate keys, finds the camera-angle memory
// field, and measures whether on-foot walking is camera-relative.
void TriggerCameraScan();

}  // namespace WorldMap
