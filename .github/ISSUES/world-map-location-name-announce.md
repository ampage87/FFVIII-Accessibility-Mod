# Auto-Announce Location Names on World Map Approach

## Summary
Hook `world_dialog_assign_text_sub_543790` to automatically announce location names via TTS when the player approaches a town or landmark on the world map.

## Current State (v0.11.16)
- Location names are only announced when manually cycling with `-`/`=`
- The game engine displays location names on-screen when approaching towns, but this is not captured by TTS
- The FFNx function `world_dialog_assign_text_sub_543790` handles this text assignment

## Proposed Enhancement
- Hook `world_dialog_assign_text_sub_543790` (address in FFNx source `ff8_data.cpp`)
- Extract the location name string being assigned
- Announce via TTS with debounce to avoid repeats
- Should work alongside existing field dialog hooks

## Notes
- This is the world map equivalent of field dialog TTS
- Need to verify the function signature and calling convention from FFNx source
- May need to handle encoding (same +0x20 offset as savemap names, or standard ASCII)
