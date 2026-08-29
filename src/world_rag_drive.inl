// world_rag_drive.inl -- the airship's drive owns its own aim, its own keys and
// its own path, and takes nothing from the vehicles that touch the ground.
//
// WHY THIS FILE EXISTS, IN AARON'S WORDS: "Let's make sure to separate the
// machinery for each of our vehicles... so each one is an independent *.inl file
// and any adjustments to one cannot negatively impact the other. Make sure all
// the ground-based avoidance machinery and whatnot is excluded from the
// Ragnarok's auto-drive code."
//
// He is right, and the evidence is five builds long. Every one of these was
// found by a separate BAT, one at a time, each costing a session:
//
//   v0.80.0  the forward-collision guard steered the ship sideways down a coast
//   v0.84.0  the reverse un-wedge burst fired thirteen times at a ship that
//            cannot wedge, undoing its own progress each time
//   v0.87.0  the planner handed the airship a 302-cell WALKING route
//   v0.94.0  the firing-area escape swung the aim round Sorceress Memorial for a
//            minute while the ship was already pointing dead at Esthar
//   v0.95.0  the planner again -- v0.87.0's guard sat BELOW the two calls that
//            do the planning, so it had never once run
//
// Five different subsystems, all written for a vehicle that touches the ground,
// all reached by one shared executor. Guarding them one at a time is how the
// last five builds went, and the fifth was a guard that was already there.
//
// SO THIS FILE STOPS DOING THAT. Instead of a guard per subsystem, discovered by
// a BAT, there is ONE INVARIANT, asserted once, after every override has had its
// turn: WHEN THE AIRSHIP IS FLYING, THE AIM IS THE DESTINATION. Whatever moved
// it -- the escape, a path, a bridge, an LOS clamp, a corner cap, a sweep, or
// something nobody has found yet -- is undone before the executor sees it. A
// subsystem that has not been discovered yet is covered by the same line as the
// four that have.
//
// The same for the route: a flying drive has no path. The airship has nothing to
// route around (0x53E6B0 falls through to `return 1` for vehicle 0x32), so a
// non-empty path can only be a walking route that leaked in, and it is dropped
// rather than followed.
//
// WHAT THIS FILE IS NOT, YET. The Garden has a genuinely separate executor --
// "when it owns the drive the foot/car one is not ticked at all" -- and that is
// the right end state here too. It is deliberately NOT done in this build:
// world_map.cpp cannot be syntax-checked against the winshim, so a new executor
// would reach MSVC for the first time on Aaron's machine with no host test
// behind it, and it would land in the same build as a fix for a live stall. One
// suspect per regression. This file is where that executor goes when it is
// written, and the invariant below is what it will be built out of.

// Does the airship own this drive? Latched at StartAutoDrive, so it cannot
// change under a tick.
static bool RagFlightOwnsDrive()
{
    return s_driveActive && s_ragFlying;
}

// THE INVARIANT. Called after every aim override, and after the path follower.
//
// aimX/aimY come in as whatever the shared machinery decided, and go out as the
// destination -- or as the Lunatic Pandora detour, which is the ONE override the
// airship has of its own, because the Pandora reaches higher than the ship can
// climb and is the only thing on this map that cannot be answered by going over
// it.
#include "world_rag_drive_clamp_body.inl"
