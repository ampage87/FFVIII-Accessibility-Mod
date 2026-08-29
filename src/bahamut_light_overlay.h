// bahamut_light_overlay.h -- the Deep Sea Research Center blue-light puzzle
// (#bahamut-light).
//
// v0.98.0: New module. `sdcore1` (field 846) is the corridor that guards
// Bahamut: a blue light pulses, and the party may only walk while it is dark.
// Walking while it glows throws a random encounter.
//
// The whole puzzle is one byte -- `var[1024]` at 0x01CFEDB8 -- which the field's
// own `Director::default` raises and lowers, and which two watcher entities
// (`battlekun1`, `battlekun4`) read to decide whether a held direction becomes
// a battle. See bahamut_light_model.inl for the transcript that establishes it.
//
// On arrival the player is asked, in speech, how they want to cross:
//
//   G  Guide -- the mod calls "Go" the instant the light clears and "Stop" the
//               instant it arms. The default if the prompt goes unanswered.
//   S  Skip  -- the mod holds the game's OWN disarm pair, the one the corridor's
//               `Hojo` safe-pocket lines write (var[1028] = 1, var[1024] = 0),
//               so the light never arms and the player can walk straight to
//               Bahamut.
//
// v0.100.0: THE PROMPT IS SPEECH AND TWO KEYS, NOT AN ENGINE DIALOG. v0.98.0
// opened a DialogInject ASK on window slot 2 and every line Bahamut speaks --
// including all three of his AASKs -- is on window slot 2. It left that window
// stuck open and the scene had nothing to talk through. The model file carries
// the log lines. Nothing here touches an engine window.

#pragma once

namespace BahamutLightOverlay {

void Initialize();
void Shutdown();

// Per-tick driver, called from dinput8's frame loop. Reads the two puzzle bytes
// while the party is in sdcore1, speaks the prompt once the puzzle is proven
// live, watches G and S for the answer, speaks the Go / Stop edges in Guide mode
// and holds the disarm pair in Skip mode. Near-no-op everywhere else.
void Update();

// True while the puzzle prompt is still unanswered in sdcore1.
bool IsPromptPending();

}  // namespace BahamutLightOverlay
