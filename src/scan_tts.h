// scan_tts.h - Scan spell TTS announcements
//
// ============================================================================
// CURRENT ARCHITECTURE (v0.14.60 — announce-on-window-render)
// ============================================================================
//
// Two-stage UX: action-layer captures snapshot silently, sub_B687C0
// first fire announces, interactive number-key queries between.
//
// Stage 1 — Action layer (silent snapshot capture).
//   OnScanCast(slot, fromActionLayer=true) fires from the action-commit
//   detectors:
//     * NoEffect_RecordSnapshot in battle_tts_noeffect.inl when the
//       Scan-name popup (text_id=0x06 value=0x32) hits sub_48D200 at
//       cast confirm.
//     * PollBattleMagicId in battle_tts_ewm.inl when battle_magic_id
//       transitions to 39 (Draw-Cast Scan path).
//   Both paths capture a per-slot ScanSnapshot (name, level, HP,
//   monster_id, description for enemies) into the cache. NO SPEECH.
//   Sets s_pendingScanSlot AND resets s_scanHookFireCount=0 so the
//   first sub_B687C0 fire below counts as #1 of THIS scan event.
//
// Stage 2 — Window-render trigger (auto-announce).
//   HookedScanGetText (the sub_B687C0 hook) fires when the engine
//   actually reads scan text to render the Scan UI window on screen.
//   On count==1 (first fire of this scan), it calls OnScanPopupSpawn
//   which reads s_pendingScanSlot, speaks the cached snapshot's
//   announce line, and sets s_scanScreenActiveSlot so the keyboard
//   router knows to redirect number keys. Subsequent fires (count>=2)
//   are silent no-ops — the engine re-reads text per frame while the
//   window is open.
//
//   v0.14.60 architecture note: The kind=0x06 val=50 popup IS NOT used
//   to trigger the announce, despite spawning at cast-confirm. v0.14.59
//   used it as the trigger and BAT proved it fires ~9 seconds before
//   the actual window opens visually — the announce played at start of
//   cast animation rather than when the player saw the window. The
//   popup record IS still used for the close-edge detection below.
//
// Stage 3 — Screen-close detector.
//   OnScanPopupDespawn fires from the [SPRITE-POLL] DESPAWN emitter in
//   battle_tts_screenshot.inl when the kind=0x06 val=50 popup record
//   leaves the table (the player dismissed the Scan UI). Clears
//   s_scanScreenActiveSlot. The snapshot cache is retained so
//   re-scanning a previously-scanned target within the same battle
//   works without redundant action-layer plumbing.
//
// Number-key routing.
//   While IsScreenActive() returns true, PollHPCheckKeys in
//   battle_tts_hp.inl routes 1..0 to SpeakField(fieldId) instead of the
//   default ally-HP behavior. Field IDs:
//     1 = Name           5 = Stats (Def/Mag/Spd)
//     2 = Description    6 = Weaknesses
//     3 = Level          7 = Absorbs
//     4 = HP             8 = Nullifies
//                        9 = Status Resistances
//                        0 = Active Statuses
//   v0.14.59 implemented 1..4. Keys 5..0 currently respond
//   "Not implemented yet." and land in v0.14.61..v0.14.64 per the
//   chapter plan in NEXT_SESSION_PROMPT.md.
//
// Allies vs enemies. Allies skip the description (no flavor text exists
// for them). The auto-announce omits the description segment for ally
// targets; SpeakField(2) replies "No description available." for allies.
//
// ============================================================================
// HISTORICAL NOTES (chapter v0.14.50 → v0.14.59, retained for context)
// ============================================================================
//   v0.14.50: First slice. Announced on Draw-Cast (magicId==39 path).
//             Worked for Draw-Cast only.
//   v0.14.51: Watchdog cancel for the no-effect false positive that
//             followed every successful Scan.
//   v0.14.52..54: Path-agnostic detection via sub_B687C0 + sub_84F860
//                  hooks. v0.14.54 BAT proved both miss the compacted
//                  view (repeat scan of same target).
//   v0.14.55..56: Action-layer detection via the existing popup hook
//                  on sub_48D200. Fires on every Scan regardless of
//                  view path. v0.14.55 LNK2019 (.inl namespace trap)
//                  fixed in v0.14.56.
//   v0.14.57:    Two-tier dedup (30 s action-layer lock + per-slot
//                hook-suppression). Three Scans → three announces.
//   v0.14.58:    Mask sub_B687C0 callback's slotIndex to low byte
//                (engine call site only sets CL).
//   v0.14.59:    Retired the lock (silent action-layer = no purpose),
//                retired sub_84F860 (full-view-only). Announce trigger
//                was the kind=0x06 val=50 popup-spawn edge — BAT showed
//                this fired too early (cast-commit, not window-open).
//   v0.14.60:    Move announce trigger from popup-spawn to first fire
//                of sub_B687C0 hook (window-render time). sub_B687C0
//                is no longer vestigial. Per-scan reset of
//                s_scanHookFireCount keys 'first fire' to THIS scan.

#pragma once

namespace ScanTTS {

// One-shot init. Installs the sub_B687C0 hook (the announce trigger
// in v0.14.60+) and resets in-memory state.
void Initialize();

// Called from the action-layer detectors (popup hook in noeffect.inl,
// magicId==39 in ewm.inl). Captures the per-slot snapshot silently and
// arms the window-render auto-announce. targetSlot is the resolved
// slot index 0..6; pass -1 to skip with a diagnostic.
//
// fromActionLayer:
//   true  — authoritative cast-time signal. Captures snapshot, sets
//            s_pendingScanSlot, resets s_scanHookFireCount, no speech.
//   false — reserved/defensive. No code path currently calls with
//            false in v0.14.60+ (sub_B687C0 hook calls
//            OnScanPopupSpawn directly on first fire instead of
//            routing through OnScanCast).
void OnScanCast(int targetSlot, bool fromActionLayer = false);

// Called from HookedScanGetText (sub_B687C0 hook) on the first fire
// per Scan event. Reads s_pendingScanSlot, speaks the cached snapshot's
// announce line, sets s_scanScreenActiveSlot.
void OnScanPopupSpawn();

// Called from the [SPRITE-POLL] DESPAWN emitter on the falling edge of
// the same popup record. Clears s_scanScreenActiveSlot. Snapshot cache
// is retained for re-query support.
void OnScanPopupDespawn();

// True while a Scan window is open on screen (i.e. between the popup
// spawn and despawn edges). PollHPCheckKeys consults this to decide
// whether to route number keys 1..0 to SpeakField or to the default
// ally-HP handlers.
bool IsScreenActive();

// The battle-entity slot whose Scan UI is currently open, or -1 if none.
int GetActiveSlot();

// Speak one field of the active Scan's snapshot. fieldId:
//   1=Name 2=Description 3=Level 4=HP 5=Stats 6=Weak 7=Absorb 8=Nullify
//   9=StatusRes 0=ActiveStatus.
// Currently 1..4 are wired; 5..0 reply "Not implemented yet."
void SpeakField(int fieldId);

// Battle-entry reset. Clears the snapshot cache and the screen-state
// flags. Called from BattleTTS::OnBattleEnter (which itself runs from
// battle_tts.cpp on the mode 999 transition).
void OnBattleEnter();

}  // namespace ScanTTS
