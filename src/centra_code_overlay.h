// centra_code_overlay.h -- the Centra Ruins five-digit code panel (#centra).
//
// v0.115.0: New module. Two statues in the Centra Ruins take the pair of eyes.
// The one on the roof (`crroof1`, field 280) SHOWS a five-digit code; the one by
// the sealed room (`crtower3`, field 285) asks you to type it back, and typing
// it opens the way to Odin.
//
// A sighted player reads the code off the roof statue and watches five digits
// tick as they hold left and right. Both halves of that are pictures, so both
// halves were missing.
//
// The whole panel is six bytes of field variable, all of them read straight out
// of `crtower3::director0::puteye0`:
//
//     var[1027]        which digit is selected, 0 while the panel is shut
//     var[1028..1032]  the five digits as entered
//     var[364..368]    the five the roof statue is showing
//
// and the script's own comparison of the two sets is what opens the door. See
// centra_code_model.inl for the decompiled loop, the button masks and the
// arithmetic.

#pragma once

namespace CentraCodeOverlay {

void Initialize();
void Shutdown();

// Per-tick driver, called from dinput8's frame loop. While the party is in the
// Centra Ruins it watches the six bytes: it opens a Game Controls box and
// speaks the controls the moment the panel appears, announces the selected
// position and its value on every change, reads the entry back when the panel
// closes, and answers the C key with the code the roof statue is showing.
// Near-no-op in every other field.
void Update();

}  // namespace CentraCodeOverlay
