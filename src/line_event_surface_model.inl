// line_event_surface_model.inl -- A LINE YOU HAVE TO PRESS A BUTTON ON IS
//                                 SOMETHING YOU DO
//
// v0.132.2 (#shumi). Aaron, on the Shumi Village stone hunt: "I did not find the
// interaction though to pick up the wind stone in the catalog. Make sure all the
// stones the player needs to pick up are identified in the catalog. They should
// all be identified as interactions in the catalog I think."
//
// HE IS RIGHT, AND THE LOG SHOWS THE GATE HAD ALREADY OPENED FOR HIM. The
// Sculptor asked for wind stones at 17:31:22. Twelve minutes later, in Village 1:
//
//     [JSMScan] line ent1 'Mitukeru' method1 is gated on var[610] (1B) op6 1
//     [SETLINE] call#36 line(-948,-5451)->(-966,-5750) center=(-957,-5600)
//     [SETLINE] call#37 line(-966,-5750)->(-1150,-6068) center=(-1058,-5909)
//     [refresh] line1 'Mitukeru' named 'Search Spot' from the sym table
//     [refresh] line2 'Mitukeru2' named 'Search Spot' from the sym table
//     [refresh] catalog: 4 entries
//       cat0 Card Game / cat1 Exit to Elevator / cat2 Exit to Village 2 /
//       cat3 Exit to Hotel 1
//
// The gate was open -- no "inert" verdict this time, where an hour earlier there
// had been one. The lines were placed, active, and NAMED. And the catalog still
// did not contain them, so the mod told a blind player that the only things in
// Village 1 were a card game and three ways out of it.
//
// THE REASON IS THE LINE'S TYPE. The catalog surfaces a line as an interaction
// when its JSM type is LINE_INTERACTIVE, which the scanner assigns to a line
// carrying MES/ASK/AMES/AASK or an extended dispatch. `Mitukeru` carries none of
// those: it does its work by REQ-ing a method on the party leader, and v0.61.0's
// promotion rule ("it dispatches to an entity that talks") deliberately does not
// fire for a party member. So it stays LINE_EVENT -- a class the catalog treats
// as scenery, on the reasoning that SHOW/HIDE/BATTLE lines are visual effects
// the player never chooses.
//
// **BUT THIS ONE WAITS FOR A BUTTON.** `Mitukeru`'s touch method opens with
// BTNTEST 0x00C0 -- Cross or Square -- and does nothing at all until one is
// held. That is not a visual effect. A line that fires when you walk across it
// is scenery or a doorway; a line that fires only when you walk onto it AND
// press a button is a thing the player has to decide to do. It is the same
// signal v0.120.0 read to stop auto-drive pushing through a Centra ladder, and
// the same one that tells the mod to say "press X to use it".
//
// THE BLAST RADIUS IS 16 LINES ACROSS 13 FIELDS. Of 248 LINE_EVENT lines on the
// disc, 232 wait on nothing and stay exactly as they are. The 16 that wait on a
// button are:
//
//     tmgate1  Mitukeru, Mitukeru2  (00C0)   <- the wind stone, both halves
//     tmmura2  Mitukeru             (00C0)   <- another stone
//     tmmin1   Hakken               (0040)   <- the Artisan's house stone
//     tmmin1   Yomu                 (0040)
//     tmhtr1   Mawaruu2             (0040)
//     fegate1  waterswitch0         -- a switch
//     fhroof1  Down2, Down3         -- ways down off a roof
//     fhhtl1   Linetanmatu, fhtown1 Outai, fhwisef2 roboline0,
//     glfurin1 eventline3, glfurin3 eventline0,
//     ecmall1a Jump, ecoway3a Jump
//
// Every one of them reads as something a player does on purpose. And the change
// is PURELY ADDITIVE -- it can only put a line into the catalog that was absent,
// never remove one that was there -- so the worst case is an extra "Interaction"
// entry in eleven fields outside Shumi Village, against a stone hunt that could
// not be completed at all.
//
// WHY NOT GATE IT ON THE CURATED NAME, the way v0.115.0 gated camera pans. That
// would surface 4 of the 16 -- the three Shumi stone spots that v0.132.0 happened
// to name, and nothing else -- which fixes this week's field and leaves
// `waterswitch0` and the two ways down off Fisherman's Horizon's roof exactly as
// invisible as the wind stone was. The button is the property that makes these
// interactions; the name is only whether we have got round to describing them.

// isEventLine: the captured line's JSM type is JSM_ENT_LINE_EVENT.
// touchButtonMask: the BTNTEST mask its non-init scripts wait on, 0 for none.
//
// A line of any other type is not this function's business -- it reaches the
// catalog through the paths that already exist, and answering true for one would
// emit it twice.
static bool EventLineSurfaces(bool isEventLine, unsigned touchButtonMask)
{
    if (!isEventLine) return false;
    return touchButtonMask != 0;
}
