// world_map_vehsig.inl -- v0.21.0 (#79)
//
// The foot-versus-vehicle motion discriminator, extracted from the middle of
// UpdateAutoDrive so it can be TESTED. Nothing in the mod compiled
// world_map_drive_exec.inl before a Windows build, which is exactly how the
// defect below survived: the policy sat inside a 66 KB fragment that no host
// harness could reach. The geometry (reading the camera yaw, measuring the
// motion bearing) stays in the executor; every decision lives here.
//
// Included from world_map.cpp before the drive files; also included directly by
// tests/vehsig_test.cpp, which replays real frames out of Aaron's logs.
//
// ---------------------------------------------------------------------------
// WHAT THE DISCRIMINATOR IS FOR
//
// A vehicle and a walking character answer the steering keys differently, so
// the auto-drive runs two laws. #79 added a physics fallback for the case where
// the engine's vehicle id could not be read: while moving, compare the motion
// bearing against the model heading (mh) and against the camera yaw. When the
// two references disagree by more than 300/4096 (~26 degrees), whichever one the
// motion follows says what the player is riding.
//
// ---------------------------------------------------------------------------
// WHY IT FIRED FIVE TIMES ON A MAN WALKING TO EDEA'S HOUSE
//
// > "It would get 1-2km out, then announce vehicle detected, then wouldn't
// >  finish the approach and the distance would increase once again." -- Aaron
//
// The log is unambiguous, and the shape repeats four times:
//
//     21:29:27  YAWDRIVE  dist=4750     <- the FOOT law, walking straight in
//     21:29:29  YAWDRIVE  dist=3305
//     21:29:31  YAWDRIVE  dist=1604
//     21:29:33  YAWDRIVE  dist=16       <- SIXTEEN UNITS from the waypoint
//     21:29:33  VEHSIG    VEHICLE DETECTED (24 of 24)
//     21:29:34  HDG-DIAG  dist=293      <- the VEHICLE law takes over
//     21:29:36  HDG-DIAG  dist=1921
//     21:29:38  HDG-DIAG  dist=2469
//     ...then an orbit at 1200-1400 that never closes again.
//
// **The verdict lands at the waypoint, every time, because the waypoint is what
// creates the evidence.** When the route advances, the mod SLEWS THE CAMERA to
// the new bearing in a single write and the character then rotates to catch up
// over several frames. Through all of them he is still walking the OLD heading,
// so the motion sides with mh -- by construction, not because he is in a car.
// One frame from that log says it outright:
//
//     cam=3403 mh=3403   (walking straight, no disagreement, no vote)
//     cam=3855 mh=3403   <- camera written; gap 452; he has not turned yet
//
// and during the turn that follows, the gap CLOSES frame by frame: 1977, 1465,
// 953, 441 ... A vehicle's gap does not do that. It persists while the vehicle
// drives on.
//
// Of the 25 voting frames the ring saw before the first verdict, every one came
// from a turn. The comment this file replaces claimed the verdict was
// "unreachable on foot (0/153 disagreement frames)". The control data was
// honest; it simply never contained a mod-driven camera slew.
//
// ---------------------------------------------------------------------------
// THE TWO GUARDS
//
//   1. A RUN of closing gaps is a turn in progress and does not vote. One
//      shrinking frame is noise and still counts; two in a row is a rotation.
//   2. The engine's own vehicle id (0x020409E0) VETOES a verdict outright. It
//      read 0 -- on foot -- at every drive start of that session while this
//      heuristic declared a vehicle five times. **A heuristic does not overrule
//      a direct reading.** An unreadable id still makes no claim, so the
//      fallback keeps doing the job it was built for.

static const int VS_WINDOW        = 24;   // votes in the ring
static const int VS_MAJORITY      = 20;   // votes needed to call it a vehicle
static const int VS_GAP_MIN       = 300;  // ~26 deg: below this the two agree
static const int VS_GAP_CLOSE_EPS = 8;    // gap shrink that counts as "closing"
static const int VS_TURN_RUN      = 2;    // consecutive closings that mean "turning"

struct VehSigRing
{
    uint32_t ring;      // last VS_WINDOW votes, bit 1 = motion sided with mh
    int      count;     // votes collected, saturates at VS_WINDOW
    int      prevGap;   // |mh - camYaw| on the previous sampled frame, -1 = none
    int      closeRun;  // consecutive frames whose gap has been closing
    bool     vetoed;    // the engine id has already vetoed a verdict this drive
};

enum VehSigResult
{
    VS_NOTHING = 0,     // no verdict this frame
    VS_TURNING = 1,     // frame ignored: the character is mid-turn
    VS_LATCH   = 2,     // VEHICLE -- switch this drive to the vehicle law
    VS_ID_VETO = 3      // majority reached, but the engine says on foot
};

static void VehSigReset(VehSigRing& st)
{
    st.ring = 0; st.count = 0; st.prevGap = -1; st.closeRun = 0; st.vetoed = false;
}

// One sampled frame. `gap` is |mh - camYaw| folded to 0..2048, `mhSided` is
// true when the measured motion bearing was closer to mh than to the camera,
// and `idSaysOnFoot` is the engine's vehicle id resolved to a verdict (false
// when the id is unreadable, which makes no claim either way).
//
// `mhSidedOut`, when non-null, receives the vote count behind a verdict so the
// caller can log the number it acted on.
static VehSigResult VehSigFeed(VehSigRing& st, int gap, bool mhSided,
                               bool idSaysOnFoot, int* mhSidedOut)
{
    if (gap <= VS_GAP_MIN) return VS_NOTHING;      // the references agree

    // Guard 1. A single shrinking gap is noise; a RUN of them is a rotation.
    // The 21:18:55 waypoint turn reads 1977, 1465, 953, 441 -- monotone, and
    // everything after its opening frame is thrown away. A vehicle whose gap
    // merely wobbles keeps every frame, which is what the test asserts.
    const bool closing = (st.prevGap >= 0 && gap < st.prevGap - VS_GAP_CLOSE_EPS);
    st.prevGap = gap;
    st.closeRun = closing ? (st.closeRun + 1) : 0;
    if (st.closeRun >= VS_TURN_RUN) return VS_TURNING;

    st.ring = ((st.ring << 1) | (mhSided ? 1u : 0u)) & 0x00FFFFFFu;
    if (st.count < VS_WINDOW) st.count++;
    if (st.count < VS_WINDOW) return VS_NOTHING;

    int votes = 0;
    for (int b = 0; b < VS_WINDOW; b++) votes += (int)((st.ring >> b) & 1u);
    if (mhSidedOut) *mhSidedOut = votes;
    if (votes < VS_MAJORITY) return VS_NOTHING;

    if (idSaysOnFoot) {                            // guard 2
        st.ring = 0; st.count = 0;                 // do not re-fire every frame
        const bool first = !st.vetoed;
        st.vetoed = true;
        return first ? VS_ID_VETO : VS_NOTHING;
    }
    return VS_LATCH;
}
