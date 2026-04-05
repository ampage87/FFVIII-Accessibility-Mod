# Vehicle-Aware BFS for World Map Location Filtering

## Summary
The terrain BFS currently treats all ocean segments as impassable (on-foot only). Different vehicles should unlock different terrain types.

## Current State (v0.11.16)
- 32×24 terrain grid classifies segments as LAND (types 0-29) or OCEAN (types 32-34)
- BFS flood-fill from player position finds reachable land segments
- Catalog filters locations to only those on reachable segments
- Works correctly for on-foot traversal

## Proposed Enhancement
Add vehicle awareness to BFS by reading the locomotion byte at `0x02040A5E`:
- **On foot** (loco 0/6): Land only (current behavior)
- **Chocobo** (loco 31): Land + shallow ocean (type 32)
- **Car** (loco 32-40): Land only (same as foot, but no forests — needs separate terrain type)
- **Garden** (loco 48): Land + all ocean
- **Ragnarok** (loco 50): All segments reachable (skip BFS entirely)

## Notes
- Locomotion byte cycles through animation sub-states while moving — need to sample and latch
- Shallow ocean (type 32) vs deep ocean (types 33-34) distinction already tracked in terrain grid classification
- May need to split the current binary LAND/OCEAN grid into a multi-value grid (LAND/SHALLOW/DEEP)
- Requires in-game access to each vehicle type for testing
