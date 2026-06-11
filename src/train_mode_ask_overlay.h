// train_mode_ask_overlay.h -- Timber train guard-mode ASK overlay (#60).
//
// v0.18.3.23: New module, modeled directly on chase_ask_overlay. When the
// player begins the Timber train-hijack mission, this presents an in-engine
// ASK letting them choose how the mod handles the patrolling guards:
//
//   1. Manual -- guards move; the uncoupling codes and per-guard proximity
//                cues are announced (TGM_MANUAL = 0). The default.
//   2. Freeze -- guards are held in place; the player just enters codes
//                (TGM_FREEZE = 1).
//   3. Skip   -- bypass the train scene entirely (TGM_SKIP = 2). The bypass
//                itself isn't built yet, so this currently falls back to
//                Freeze (see CommitChoice in the .cpp).
//
// The choice is dispatched to FieldDialog::SetTrainGuardMode(), which persists
// it to the INI and updates the live cache the guard cue / freeze read.
//
// Trigger (pinned from a 2026-06-11 capture run): Watts' "Are you ready, sir!?"
// AASK fires in tiagit1 (the Forest Owls' base room). Selecting "Yeah"
// immediately MAPJUMP3s to tiyane1 (field 930, "Timber - Train 3"), where
// Rinoa's "Squall, over here!" line plays. We match that line, gated to
// tiyane1, once per mission. As with the chase ASK, DialogInject owns the
// rendering, cursor input, and answer detection; this overlay owns the
// trigger detection and the mode dispatch.

#pragma once

namespace TrainModeAskOverlay {

void Initialize();
void Shutdown();

// Per-tick driver. Resolves the deferred open, retries if the slot was busy,
// re-arms the once-per-mission flag on return to the briefing room, and polls
// DialogInject::GetLastAnswer() while the ASK is open. Near-no-op otherwise.
void Update();

// Called by field_dialog's show_dialog hook for every decoded field dialog
// text (field mode only). Cheap field-gate + strstr filter; sets the deferred
// open when it sees the tiyane1 trigger line.
void OnDialogText(const char* text);

// True while the train mode ASK is open. Reserved for future input-gating.
bool IsAskActive();

}  // namespace TrainModeAskOverlay
