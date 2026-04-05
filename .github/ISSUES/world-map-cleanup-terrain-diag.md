# Clean Up TERRAIN-DIAG Diagnostic Code in world_map.cpp

## Summary
Remove the TERRAIN-DIAG diagnostic logging from `LoadTerrainGrid()` in `world_map.cpp`. This diagnostic code dumps byte ranges for segments 0/200/400 and was used during development to verify the block-aware terrain parsing format. Now that the terrain grid is confirmed working (v0.11.16), this code can be removed.

## Code to Remove
In `LoadTerrainGrid()`, the block that scans segments 0, 200, and 400, logging:
- `[TERRAIN-DIAG] seg%d firstByte=0x%02X`
- `[TERRAIN-DIAG] seg%d byte0x0D@stride16_from12000: ...`
- `[TERRAIN-DIAG] seg%d raw_byte_counts: val32=... val33=... val34=...`
- `[TERRAIN-DIAG] seg%d bytes[12000..12063]: ...`

## Priority
Low — cosmetic cleanup, no functional impact. The diagnostic adds ~30 log lines on first world map entry but doesn't affect performance or behavior.
