// drive_progress_model.inl -- IS THE DRIVE ACTUALLY GETTING ANYWHERE?
//
// v0.131.1 (#centra). Aaron, after the ladder finally worked both ways: "only
// issue now is the auto-drive getting stuck when headed to that ladder... there
// was another part where I got stuck as well when headed to Ruins 7. See if you
// can make object avoidance / getting out of being stuck more intelligent."
//
// The two episodes in that log are NOT the same failure, and the recovery
// machinery treated them identically -- which is why neither got better.
//
// **crroof1, 19:15:26-19:15:36, tri 34: AN OSCILLATION.** The player alternates
// between exactly two positions, 30 units apart, forever:
//
//     pp=(1476,-187)  trigRedir=1  finalDelta=(16.9, 7.3)   kb=DR
//     pp=(1498,-167)  trigRedir=1  finalDelta=(-8.9,-3.9)   kb=UL
//
// He IS moving. The trigger-line avoidance is projecting the steer parallel to a
// line it does not want him to cross, and the tangential component flips sign
// each time he crosses back -- a textbook two-cycle. Every nudge in the recovery
// ladder pushes perpendicular to a corridor edge, which is orthogonal to the
// thing actually going wrong, and after five of them he cancelled.
//
// **crtower3, 19:15:07-19:15:14, tri 89: A HARD PIN.** The opposite signature:
//
//     pp=(-1305,-34)  trigRedir=0  moveDist=0  lX=989  for seven seconds
//
// He is not moving AT ALL. The commanded direction is into geometry, and the
// four-mode perpendicular escape guesses at which way is clear rather than
// finding out.
//
// So: one monitor, two verdicts. Path length and net displacement over a short
// window separate them completely -- oscillating means a long path with no net
// distance; pinned means no path at all -- and each verdict gets the remedy that
// matches it. Both are decided from the player's own position history, which
// costs nothing and cannot be wrong about whether he is getting anywhere.

// ============================================================================
// v0.131.4 (#centra): THE WINDOW HAS TO BE MEASURED IN TIME, NOT IN TICKS
// ============================================================================
// The 20:14 run says the monitor is reading the wrong clock. At 20:14:58 it
// declared "pinned -- 0 units walked in the last 32 ticks" while the drive's own
// diagnostic, thirty ticks either side of it, has the player covering ninety-nine
// units: `t=60 pp=(4468,-91)` then `t=90 pp=(4456,8)`. Both cannot be true of the
// same second, and the drive-vec line is the one reading the player's position
// directly.
//
// A DRIVE TICK IS NOT A GAME FRAME. UpdateAutoDrive runs on AccessibilityThread,
// which polls faster than the game renders, so a run of consecutive ticks
// routinely sees the same position -- not because the player is stuck but
// because the engine has not stepped him yet. Thirty-two of those ticks can be a
// fraction of a second of game time, and a fraction of a second of standing
// still is what every walking character does between frames.
//
// That is why the same log has forty-nine pins, twenty of them sweeping through
// every heading and finding nothing: most were never pins. The verdicts were
// right about the numbers they were given and the numbers covered no time.
//
// So the window is now sampled on the CLOCK -- one sample per 50 ms, thirty-two
// of them, a fixed 1.6 seconds however fast the mod happens to be polling. The
// thresholds below describe that second and a half: a character walking normally
// covers hundreds of units in it, so twelve is unambiguously stuck and three
// hundred walked for sixty gained is unambiguously a loop.
static const unsigned DRIVE_PROG_SAMPLE_MS = 50;    // one sample per 50 ms
static const int   DRIVE_PROG_SAMPLES   = 32;    // x 50 ms = a 1.6 s window
static const float DRIVE_PROG_NET_EPS   = 60.0f; // net displacement that counts as "nowhere"
static const float DRIVE_PROG_PATH_MIN  = 300.0f;// path length that counts as "moving"
static const float DRIVE_PROG_STALL_EPS = 12.0f; // path length that counts as "not moving"

// True when enough real time has passed to take another sample. Keeping this a
// pure function of the two stamps is what lets the probe test a whole window
// without a clock.
static bool DriveProgressDueForSample(unsigned nowMs, unsigned lastMs, bool everSampled)
{
    if (!everSampled) return true;
    return (unsigned)(nowMs - lastMs) >= DRIVE_PROG_SAMPLE_MS;
}

struct DriveProgressTracker {
    float x[DRIVE_PROG_SAMPLES];
    float y[DRIVE_PROG_SAMPLES];
    int   count;
    int   next;
};

static void DriveProgressClear(DriveProgressTracker* t)
{
    if (!t) return;
    t->count = 0;
    t->next  = 0;
}

static void DriveProgressPush(DriveProgressTracker* t, float px, float py)
{
    if (!t) return;
    t->x[t->next] = px;
    t->y[t->next] = py;
    t->next = (t->next + 1) % DRIVE_PROG_SAMPLES;
    if (t->count < DRIVE_PROG_SAMPLES) t->count++;
}

static bool DriveProgressFull(const DriveProgressTracker* t)
{
    return t && t->count >= DRIVE_PROG_SAMPLES;
}

// Distance between the oldest and newest sample: how far the drive actually got.
static float DriveProgressNet(const DriveProgressTracker* t)
{
    if (!DriveProgressFull(t)) return 0.0f;
    const int oldest = t->next;                                  // ring is full
    const int newest = (t->next + DRIVE_PROG_SAMPLES - 1) % DRIVE_PROG_SAMPLES;
    const float dx = t->x[newest] - t->x[oldest];
    const float dy = t->y[newest] - t->y[oldest];
    return sqrtf(dx * dx + dy * dy);
}

// Total ground covered over the same window: how hard it tried.
static float DriveProgressPath(const DriveProgressTracker* t)
{
    if (!DriveProgressFull(t)) return 0.0f;
    float sum = 0.0f;
    for (int i = 1; i < DRIVE_PROG_SAMPLES; i++) {
        const int a = (t->next + i - 1) % DRIVE_PROG_SAMPLES;
        const int b = (t->next + i) % DRIVE_PROG_SAMPLES;
        const float dx = t->x[b] - t->x[a];
        const float dy = t->y[b] - t->y[a];
        sum += sqrtf(dx * dx + dy * dy);
    }
    return sum;
}

// Walking a long way and ending where you started. crroof1's two-cycle: 30 units
// each tick, ~900 units of path, ~30 units of net.
static bool DriveIsOscillating(const DriveProgressTracker* t)
{
    if (!DriveProgressFull(t)) return false;
    return DriveProgressPath(t) >= DRIVE_PROG_PATH_MIN &&
           DriveProgressNet(t)  <= DRIVE_PROG_NET_EPS;
}

// Not moving at all. crtower3's wall: moveDist=0 for seven seconds.
static bool DriveIsStalled(const DriveProgressTracker* t)
{
    if (!DriveProgressFull(t)) return false;
    return DriveProgressPath(t) <= DRIVE_PROG_STALL_EPS;
}

// ============================================================================
// THE ESCAPE: SWEEP THE HEADING, DO NOT GUESS AT IT
// ============================================================================
// Against a wall the mod knows one thing for certain -- the direction it is
// asking for does not work. So it stops asking for that one and tries the rest,
// nearest first, alternating sides so a wall on either hand is found in two
// probes rather than four. The list ends at 180 degrees because a heading that
// works nowhere in a full sweep is a pathing problem, not a steering one, and
// the recovery ladder above is where that belongs.
// v0.131.3: 135 and 180 are gone. Walking backwards to reach a goal in front of
// you is the pathfinder's job, not the steering's -- see DriveProbeMovedUsefully
// below for what they were actually doing.
static const int DRIVE_PROBE_ANGLES[] = { 30, -30, 60, -60, 90, -90 };
static const int DRIVE_PROBE_COUNT = (int)(sizeof(DRIVE_PROBE_ANGLES) /
                                           sizeof(DRIVE_PROBE_ANGLES[0]));
// Long enough for a step to register at 60 Hz, short enough that a full sweep
// is under two seconds.
static const int DRIVE_PROBE_TICKS = 12;

// ============================================================================
// v0.131.2 (#centra): AND HOLD THE ANGLE THAT WORKED
// ============================================================================
// The 19:46-19:47 run is the fix working -- six fields driven end to end, one
// recovery in the whole session, no cancellations -- but it also shows the sweep
// doing the same work over and over:
//
//     19:46:27  pinned ... sweeping from 30 degrees
//     19:46:27  unpinned at 30 degrees
//     19:46:28  pinned ... sweeping from 30 degrees
//     19:46:28  unpinned at -30 degrees
//     19:46:29  pinned ... 19:46:29 unpinned at 30 degrees
//
// Seven pins in ninety seconds, each cleared in under a second. v0.131.1's entry
// claimed the sweep "KEEPS the angle that worked, so he slides along the wall
// instead of turning straight back into it". It did not: the code dropped the
// rotation the instant he moved, so the next frame steered back into the wall
// and he re-pinned half a second later. The claim was right about what should
// happen and the code did the other thing.
//
// So the winning angle is now HELD for three quarters of a second after the
// unpin -- long enough to clear the corner rather than just the contact -- and a
// pin that happens during that hold resumes the sweep at the NEXT angle instead
// of re-trying the one that just bought half a second. Failing forward: each
// attempt starts from more information than the last, which is the difference
// between a search and a loop.
static const int DRIVE_PROBE_HOLD_TICKS = 45;

// ============================================================================
// v0.131.3 (#centra): "DID HE MOVE" IS THE WRONG QUESTION. "DID HE GET CLOSER"
//                      IS THE RIGHT ONE.
// ============================================================================
// The 20:04:09 drive -- crroof1's ramp, from the foot of the ladder back to the
// field exit -- thrashed for forty-one seconds and gave up 512 units short. The
// log shows the sweep succeeding over and over while getting nowhere:
//
//     20:04:12  pinned, sweeping from 30    unpinned at -30
//     20:04:12  pinned, sweeping from 60    unpinned at -60
//     20:04:13  pinned, sweeping from 90    unpinned at 90
//     20:04:13  pinned, sweeping from -90   unpinned at -135
//     20:04:15  oscillating -- 496 units walked, 25 units gained
//     ...  and at 20:04:45, "unpinned at 180 degrees"
//
// **Unpinned at 180 degrees.** The escape's success test was "has he moved 45
// units", and walking backwards passes it. So the sweep would turn him around,
// declare victory, hold the reversal for three quarters of a second, and pin
// again facing the wall -- with the oscillation detector then firing on the
// resulting shuffle. Three mechanisms, each doing what it was told, adding up to
// forty-one seconds of nothing. That is v0.131.1 and .2 making this drive WORSE,
// and it is the escape's fault.
//
// An escape is only an escape if it makes ground toward where the drive was
// trying to go. Sliding along a wall at 30 or 60 degrees does; a right-angle
// squeeze past a corner barely does; a reversal never does. So the movement is
// now measured against the direction the drive actually wanted, and a probe that
// moves him somewhere useless keeps sweeping instead of claiming success.
static const float DRIVE_PROBE_USEFUL_DOT = 0.25f;   // within ~75 degrees of the goal

static bool DriveProbeMovedUsefully(float mdx, float mdy, float wantX, float wantY)
{
    const float mLen = sqrtf(mdx * mdx + mdy * mdy);
    if (mLen <= DRIVE_PROG_NET_EPS) return false;          // has not moved enough
    const float wLen = sqrtf(wantX * wantX + wantY * wantY);
    if (wLen <= 0.001f) return true;                       // no goal direction to judge by
    return ((mdx * wantX + mdy * wantY) / (mLen * wLen)) >= DRIVE_PROBE_USEFUL_DOT;
}

// Where a fresh sweep should start, given how the previous one ended. A pin that
// follows an unpin closely enough to still be inside the hold is the same wall,
// so it escalates; anything later is a new obstacle and starts from the top.
static int DriveProbeResumeIndex(bool withinHold, int lastGoodIdx)
{
    if (!withinHold) return 0;
    const int next = lastGoodIdx + 1;
    return (next >= DRIVE_PROBE_COUNT) ? 0 : next;
}

static int DriveProbeAngle(int idx)
{
    if (idx < 0 || idx >= DRIVE_PROBE_COUNT) return 0;
    return DRIVE_PROBE_ANGLES[idx];
}

static void DriveRotateSteer(float dx, float dy, int degrees, float* outX, float* outY)
{
    const float r = (float)degrees * 3.14159265f / 180.0f;
    const float c = cosf(r), s = sinf(r);
    if (outX) *outX = dx * c - dy * s;
    if (outY) *outY = dx * s + dy * c;
}

// The redirect that caused the oscillation is suspended, not deleted: a few
// seconds of steering straight at the waypoint is enough to break a two-cycle,
// and the avoidance is still wanted for the rest of the drive.
static const int DRIVE_REDIRECT_SUPPRESS_TICKS = 180;   // ~3 s

// ============================================================================
// v0.131.5 (#centra): ONE OVERSHOOT ADVANCE PER TICK
// ============================================================================
// crroof1's ramp is a switchback, and the 20:50:20 drive down it is the last
// thing still failing on that field: forty-one seconds, six recoveries, gave up
// 511 units short. The drive-vec lines show what it does, and it is not a
// steering problem.
//
//     t=30   pp=(1178,-513)  wpRaw=(1343,-58)    <- first waypoint, NORTH
//     t=60   pp=(1357,-96)   wpRaw=(1459,-19)    <- second, further north
//     t=90   pp=(1232,-373)  wpRaw=(1334,-672)   <- the FINAL TARGET, south
//
// Four waypoints, and between two log lines it went from following the second to
// having none left, so from then on it steered straight at the exit across
// ground the walkmesh does not connect -- jammed at tri 19, re-pathed, ran back
// north, and did the whole thing again.
//
// The chain-advance itself is sound: it stops at the first waypoint it has not
// reached. What runs away is the OVERSHOOT rule beside it -- "we got close and
// are now moving away, so count it as reached" -- which `continue`s and can
// therefore consume the rest of the list in a single tick. On a path that
// doubles back, moving away from the waypoint behind you is what walking the
// next leg LOOKS like, so every remaining waypoint reads as overshot at once.
//
// A waypoint genuinely reached still chains, because arriving at several in a
// row is real and common on a tight funnel. An overshoot is an inference, and
// one inference per tick is enough: the next tick will make the same call again
// if it is still true, and the drive will have moved in between.
static bool DriveOvershootAdvanceAllowed(int overshootsThisTick)
{
    return overshootsThisTick < 1;
}
