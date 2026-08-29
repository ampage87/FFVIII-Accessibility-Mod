// drive_bridge_target_model.inl -- THE BRIDGE REDIRECT HAS TO SURVIVE THE DRIVE
//
// v0.118.0 (#centra). The 2026-08-28 log, on crtower2, driving to the exit up
// to crtower3:
//
//   [drive] gateway target -> crossing line (1125,942)->(1214,1016) destFieldId=285
//   [drive] target on different walkmesh island (start tri 37, goal tri 73)
//           -- searching for bridge trigger
//   [drive] redirecting to trigger line 1 center=(1406,-384) tri=37 dist=74
//   [drive] started toward ent-401 gw1 waypoints=3
//   [drive] tick=120 dist=1708 player=(1432,-404) steer=(693,-1284) moveDist=1271
//   [drive] recovery 1 -- A* failed from tri 36, keeping old 3 wp
//   ... five more recoveries, moveDist=0 every one ...
//   [drive] stopped: Cancelled.
//
// It found the right answer and then threw it away. crtower2's exit to
// crtower3 is on the far island; the only way across is the left ladder, and
// v06.01's island check picked it correctly -- trigger line 1, "Left Ladder Up",
// **74 units from the player**. Then the drive walked in the opposite direction
// and froze.
//
// WHY. The redirect writes the bridge into `_tx`/`_tz`, which are LOCALS in
// HandleKeys. UpdateAutoDrive does not read them: it re-derives the target from
// the catalog index on **every tick** (`ei <= -400` -> the dedup gateway's own
// centre). So the drive's target snapped straight back to the gateway at
// (1170,979) -- 1408 away, 1708 with the trigger-line's 300-unit push, which is
// the `dist=1708` in the log to the unit. Every recovery then re-pathed to that
// same gateway, A* correctly refused (it is on an island the player cannot
// reach), and the drive spent six recoveries and twelve seconds going nowhere.
//
// The bridge was never wrong. It just was not written anywhere the drive looks.
//
// So it is written here, in state the tick loop reads, and it overrides the
// per-tick target for the whole drive. Two consequences follow and both are
// wanted:
//
//   * the recovery re-paths aim at the bridge as well, because they re-path to
//     the same `tx`/`tz` the tick uses. That is the fix for the recovery loop,
//     not just for the first waypoint.
//   * crossing detection has to move with it. A gateway target seeds
//     s_driveCrossLine* from the gateway's own line; if the drive is actually
//     heading for a ladder 1400 units away from that line, the "have we crossed
//     yet" test is asking about the wrong line. The redirect re-seeds it.
//
// SCOPE. This changes nothing for a drive whose target is on the player's own
// island, which is every drive that already worked -- BridgeRedirectApplies is
// false unless the island check fired AND found a reachable bridge.

static bool BridgeRedirectApplies(bool bridgeActive, int bridgeLineIdx)
{
    return bridgeActive && bridgeLineIdx >= 0;
}

// The target the tick loop should steer at. Falls through to the original for
// every ordinary drive, so the caller can apply it unconditionally rather than
// wrapping it in an `if` that a future edit could get wrong.
static void BridgeDriveTarget(bool bridgeActive, int bridgeLineIdx,
                              float bridgeX, float bridgeY,
                              float origX, float origY,
                              float* outX, float* outY)
{
    if (outX == nullptr || outY == nullptr) return;
    if (BridgeRedirectApplies(bridgeActive, bridgeLineIdx)) {
        *outX = bridgeX;
        *outY = bridgeY;
        return;
    }
    *outX = origX;
    *outY = origY;
}

// A bridge that turns out to be in the player's OWN triangle needs no path.
// crtower2's ladder was `tri=37` with the player standing on tri 37, and A*
// from a triangle to itself is a degenerate query whose funnelled output the
// .118 log shows steering 1149 units the wrong way. One waypoint, straight at
// it, is both correct and shorter.
static bool BridgeNeedsNoPath(int startTri, int bridgeTri)
{
    return startTri >= 0 && bridgeTri >= 0 && startTri == bridgeTri;
}
