# Guided Navigation (GPS Mode) for World Map

## Summary
Add passive heading tracking that announces direction changes as the player moves toward a selected destination, similar to GPS turn-by-turn navigation.

## Current State (v0.11.16)
- Backspace gives on-demand bearing + distance to selected location
- Auto-drive (`\`) steers automatically toward the destination
- No passive guidance while the player steers manually

## Proposed Enhancement
When a location is selected (via `-`/`=`), passively monitor the player's heading relative to the target:
- Announce "turn left" / "turn right" / "on course" when heading deviates significantly
- Periodic distance updates (configurable interval)
- "Arrived" announcement on proximity
- Toggle on/off with a dedicated key (maybe F2, currently unused)

## Design Considerations
- Should NOT interrupt other TTS (dialog, battle events)
- Debounce heading changes to avoid rapid-fire announcements during turns
- Consider audio cues (tones) vs speech for direction — tones would be less intrusive
- Could combine with compass bearing ("turn right, target is northeast")
