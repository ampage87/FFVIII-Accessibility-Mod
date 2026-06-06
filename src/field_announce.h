// field_announce.h - Automatic field-name announcement on field load.
//
// v0.15.2.14: When the player enters a new field, look up the
// human-readable display name for the field's ID in FIELD_DISPLAY_NAMES
// (the 982-entry catalog generated from ff8-speedruns/ff8-memory mapId.md)
// and announce it via the screen reader. Same names that the entity
// catalog and -/+/Backspace navigation already expose, just spoken
// automatically on entry instead of waiting for the player to ask.
//
// Skips:
//   - Title screen / startup (fieldId == 0)
//   - Out-of-range field IDs (>= FIELD_DISPLAY_NAMES_COUNT)
//   - Non-field game modes (battle, menu, world map, etc.)
//   - Re-entry to the same field already announced (no spam on
//     mid-field battle exits, dialog-driven mode flips, etc.)
//
// Debounce: an 800ms hold window after a fieldId flip before announcing,
// so very-rapid transitions (chase-scene field stitching, scripted
// cuts) don't fire two TTS lines back-to-back.

#pragma once

namespace FieldAnnounce {

// One-time setup. Resets state.
void Initialize();

// Cleanup. Resets state.
void Shutdown();

// Per-tick driver. Polls FF8Addresses::pCurrentFieldId, applies the
// debounce, and calls ScreenReader::Speak with the catalog name on
// confirmed field changes. Cheap on no-change frames (one read +
// one comparison).
void Update();

}  // namespace FieldAnnounce
