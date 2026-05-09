// chase_detector.h — Dollet / X-ATM092 chase scene state authority
//
// v0.15.1: New module. Tracks whether the player is currently in a chase
// field, debounces the lagging pCurrentFieldName, counts battles per field,
// and resolves kani's slot in pFieldStateBackgrounds (per v0.15.0 BAT
// finding 1: kani is a Background script entity, NOT in pFieldStateOthers).
//
// v0.15.2.8: Adds parallel resolution for the 'battleyarou' entity. v0.15.2.7
// BAT in 'domt5_1' showed kani is dead code in that field (FINAL SUMMARY
// changed_bytes=0/612 across 10 seconds, but battle still triggered).
// Hypothesis: 'battleyarou' is the universal chase entity. We now resolve
// both kani and battleyarou per field, so chase_kani_freeze can pin both
// every frame regardless of which one is the active chase agent.
//
// This module is the single source of truth for chase-scene state across
// chase_ask_overlay, chase_battle_freeze, and chase_diag. The chase field
// name set is hard-coded based on v0.15.0 BAT confirmation:
//   domt4_1, domt5_1, domt2_1, doopen2a, dotown_3, dotown_2, dotown_1
//   (and possibly domt3_2 — added defensively)
//
// Chase mode persists in ff8_accessibility.ini under the [Chase] section,
// key `chase_mode` (values "auto" or "manual", default "manual").

#pragma once

#include <cstdint>

namespace ChaseDetector {

// Chase mode constants. Stored in INI as the lowercase string.
enum Mode {
    MODE_MANUAL = 0,   // "manual" — cap chase battles at 1 per field
    MODE_AUTO   = 1,   // "auto"   — auto-drive (v0.15.2+ stretch goal; falls
                        //              back to manual until implemented)
};

// One-time setup. Reads chase_mode from INI; resets per-field counters.
void Initialize();

// Cleanup. Resets state.
void Shutdown();

// Per-tick driver. Polls field id/name with debounce, tracks game-mode
// transitions for battle counting, refreshes kani-slot cache on field
// change, and detects chase-end (transition out of chase field set).
// Cheap when no chase-related state has changed.
void Update();

// True when the player is currently in a known chase field (after the
// 2-second debounce on pCurrentFieldName has settled). Returns false in
// any other situation, including before chase entry, during battle, in
// menus, on the world map, etc.
bool IsInChaseField();

// Returns the current debounced field name, or "" if not yet settled or
// not in a field. The pointer is valid until the next field transition.
const char* GetDebouncedFieldName();

// True between first entry into a chase field and transition out (i.e.
// across the entire chase sequence including non-X-ATM fields like
// doopen2a's bridge). Used by chase_ask_overlay to gate the once-per-
// chase ASK trigger; resets to false on chase-end so a future replay
// can re-trigger.
bool IsChaseActive();

// Number of battles entered since the current field was loaded. Resets
// on every field transition (with debounce). Used by chase_battle_freeze
// to decide whether to no-op a kani BATTLE call.
int GetCurrentFieldBattleCount();

// Returns the kani entity's runtime block address in the live entity
// arrays (pFieldStateBackgrounds or pFieldStateOthers), or 0 if kani is
// not present in the current field. Cached at field-change time and
// re-resolved if the cache is stale. Used by chase_battle_freeze to
// identify "calling entity is kani" via pointer equality.
uintptr_t GetKaniEntityPtr();

// True if the given entity pointer matches kani's cached runtime block
// address in this field. Cheap pointer comparison; returns false if
// kani slot is unknown for the current field.
bool IsKaniEntityPtr(uintptr_t entityPtr);

// v0.15.2.8: Same accessors for the 'battleyarou' entity. Returns 0 / false
// if battleyarou is not present in the current field.
uintptr_t GetBattleyarouEntityPtr();
bool      IsBattleyarouEntityPtr(uintptr_t entityPtr);

// Chase-mode accessors. The mode persists across sessions in INI.
Mode GetChaseMode();
void SetChaseMode(Mode m);

// Stringify the chase mode for logs/TTS. Returns "auto" or "manual".
const char* ChaseModeName(Mode m);

// Diagnostic accessors used by chase_diag to enrich its log streams
// without duplicating the JSMCounts lookup logic. v0.15.2.8: this struct
// is also used for non-kani entities (e.g. battleyarou); the name is
// kept for backward compatibility.
struct KaniLocation {
    int   symIdx;          // index in SYM names (-1 if not found)
    int   arraySlot;       // slot within the live array (-1 if not found)
    int   arrayKind;       // 0 = unknown, 1 = backgrounds, 2 = others
    char  symName[32];     // copy of the matched SYM name (e.g. "kani"/"Kani")
};
KaniLocation GetKaniLocation();
KaniLocation GetBattleyarouLocation();  // v0.15.2.8

// v0.15.2.9: SYM-name accessors for the all-Others diagnostic scanner
// in chase_kani_freeze. Returns the SYM name at the given index, or "?"
// if the index is out of range. Names are cached at field-change time.
const char* GetSymName(int idx);
int         GetSymNameCount();

}  // namespace ChaseDetector
