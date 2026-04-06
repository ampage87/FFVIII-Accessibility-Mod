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

}  // namespace WorldMap
