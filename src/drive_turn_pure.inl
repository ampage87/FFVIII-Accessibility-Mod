// drive_turn_pure.inl -- a turn is not a wedge.
//
// A statement-free header fragment #included by world_map_state.inl (the real
// build) and by tests/drive_turn_test.cpp, so the probe exercises the shipped
// rule rather than a restatement of it.
//
// The world map's controls are tank-style: LEFT and RIGHT rotate, UP moves the
// facing. So a vehicle swinging onto a new bearing genuinely does not translate
// -- and the stuck detector measures translation, over a three-second window,
// with six strikes before it gives up.
//
// On foot and in the car the turn disappears inside that window. The Ragnarok
// is slower to come about, and the 23:04 BAT shows the drive that WORKED --
// Sorceress Memorial, 53 km, arrived -- spending one of its six lives on a
// heading change before it had moved an inch.
//
// The excuse is bounded, and the bound is the point. An un-wedge burst changes
// the heading too, so an unlimited excuse would mean a drive that spins in one
// place forever and never gives up. That is worse than giving up early: a
// give-up at least says so, out loud, and hands control back.
static bool DriveTurnExcusesStall(bool flying, int hdgDelta, int turnPasses, int turnMax)
{
    if (!flying) return false;                      // foot and car are BAT-tuned; leave them
    if (turnPasses >= turnMax) return false;        // the bound
    return hdgDelta > 128 || hdgDelta < -128;       // ~11 degrees of 4096
}

// Signed heading difference in [-2048, 2048), on the engine's 0..4095 circle.
static int DriveHeadingDelta(int now, int then)
{
    return (((now - then) + 6144) % 4096) - 2048;
}
