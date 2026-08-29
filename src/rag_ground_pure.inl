// rag_ground_pure.inl -- aboard the Ragnarok is not the same as airborne in it,
// and a hull the engine waves through every polygon is never wedged.
//
// A statement-free header fragment #included by world_map_drive.inl (the real
// build) and by tests/rag_ground_test.cpp.
//
// THE 2026-08-25 12:37 BAT. Aaron boarded the Ragnarok where he had parked it,
// started a drive to Esthar City 26 km off, and the ship spent a minute
// wandering. The log's own numbers say what happened without any guessing:
//
//   [12:37:49] Stuck check 1/6 (moved 0 units in 3000ms window)
//   [12:37:49] Hard wedge -> reverse un-wedge burst 1/4 (off=1568, dist=26502)
//   ... twelve more bursts, and five full cycles of "no strike 1/3, 2/3, 3/3"
//
// and the distance going 26502, 26502, 26266, 26028, 25931, 25936, 24108,
// 24445, 24504, 24202 -- forward, backward, forward.
//
// TWO THINGS WERE WRONG, AND THE FIRST ONE CAUSED THE SECOND.
//
// ONE. THE REVERSE BURST WAS THE STALL, NOT THE CURE. The un-wedge machinery
// presses DOWN when net displacement from an anchor stays under WEDGE_NET_EPS
// for HARD_WEDGE_MS. That is a car backing off a cliff it has driven into, and
// the Ragnarok cannot drive into anything: 0x53E6B0 -- may this vehicle move
// onto this polygon -- falls through to `return 1` for vehicle 0x32, so every
// polygon on the map is open to it whether it is in the air or on its wheels.
// A burst fired at a ship that was never stuck simply undoes the progress it
// had made, which keeps net displacement small, which keeps the anchor from
// resetting, which fires the next burst. The loop is self-sustaining and it is
// entirely of the mod's own making.
//
// TWO. THE TURN BUDGET REFUNDED ON MOTION, SO IT WAS NEVER A BOUND. v0.80.0
// let flight spend a no-strike pass on a real heading change, "BOUNDED AT
// THREE", and the bound is the whole reason the excuse is safe. But the refund
// fired whenever the ship had MOVED more than the stuck threshold -- and a ship
// being shoved backward and forward moves a great deal while getting nowhere.
// The BAT shows 1/3, 2/3, 3/3 reached and reset five times over. Progress, not
// motion, is what has earned an excuse back.
static bool RagWedgeAllowed(bool aboardRagnarok)
{
    return !aboardRagnarok;
}

// Did this stuck window actually close the distance? DRIVE_STUCK_THRESHOLD is
// 100 units of movement; the same figure is a fair floor for progress, and
// asking for it against the TARGET is what motion could never fake.
static bool RagDriveMadeProgress(double distNow, double distThen, double eps)
{
    if (distThen <= 0.0) return true;        // no previous reading; do not punish
    return (distThen - distNow) >= eps;
}

// ---------------------------------------------------------------------------
// ON THE GROUND, OR IN THE AIR?
//
// The 12:37 screenshot shows the Ragnarok sitting on red rock beside a lake,
// nose down, parked. He had boarded it and never taken off, and the speed says
// the same thing louder than the picture: this drive covered about 2,500 units
// in 64 seconds, and the Fisherman's Horizon flight two hours earlier covered
// 63,283 in 35. FORTY-SIX TIMES SLOWER. Auto-drive was trundling him across
// Esthar at walking pace.
//
// A sighted player sees the ship on the ground. Aaron cannot, so the mod has to
// say it -- and it can, because the engine hands over both numbers. At 12:37:08
// the world-map slot dump reads char Z=-544 and the engine's live ground height
// at that position reads -544: ON THE GROUND MEANS THE ALTITUDE IS THE GROUND.
//
// The threshold is the engine's own. 0x54B860 refuses to set the ship down when
// |entity.y - groundH| >= 200, which is the game saying in its own voice that
// 200 units of daylight is no longer "on the ground". Using any other number
// here would be inventing a second opinion.
enum RagHeight { RAG_HEIGHT_UNKNOWN = 0, RAG_ON_GROUND, RAG_AIRBORNE };

static const int RAG_SETDOWN_GATE = 200;   // 0x54B860

static RagHeight RagHeightState(bool aboardRagnarok, bool readsOk,
                                int shipZ, int groundH)
{
    if (!aboardRagnarok) return RAG_HEIGHT_UNKNOWN;
    if (!readsOk)        return RAG_HEIGHT_UNKNOWN;
    const int gap = shipZ > groundH ? shipZ - groundH : groundH - shipZ;
    return (gap >= RAG_SETDOWN_GATE) ? RAG_AIRBORNE : RAG_ON_GROUND;
}

// Whether to start a Ragnarok drive at all.
//
// ONLY a positive measurement refuses. RAG_HEIGHT_UNKNOWN proceeds exactly as
// v0.83.0 did, so a build that cannot read the altitude behaves as today rather
// than grounding a working feature on a number nobody has validated in flight.
static bool RagDriveMayStart(RagHeight h)
{
    return h != RAG_ON_GROUND;
}

// ---------------------------------------------------------------------------
// v0.96.0: WHEN THE GROUND IS NOT GROUND.
//
// The 19:38 BAT ended with the ship PARKED ON THE FISHERMAN'S HORIZON PAD -- the
// screenshot shows it sitting on the circular platform with the bridge either
// side and ocean all round -- and the take-off check let a drive start anyway:
//
//   [RAG] height check: shipZ=-200 groundH=0 gap=200 -> AIRBORNE
//
// groundH=0 is the engine's NO GROUND reading. The FH pad is a man-made platform
// over water, not ordinary terrain, so the engine has no height for it. The gap
// was measured against a ground that is not there, came out at exactly 200, and
// landed precisely on the AIRBORNE side of a boundary it had no business being
// compared with. The ship then sat there for eighteen seconds with the throttle
// and the climb held, going nowhere, and the drive said "Stuck. Cannot reach
// destination." -- true, and useless.
//
// The rule itself has been right everywhere it had a ground to read: -544 against
// -544 on open terrain, and 900 to 3,400 on every genuine flight. Its one weak
// spot is exactly this: A HEIGHT TEST NEEDS A HEIGHT, and 0 means there is not
// one. The mod's own reader has a triangle there when the engine does not, and
// it is validated to within 14 units of the engine across 3,852 ground-truth
// samples, so it is the better answer rather than a second opinion.
static bool RagGroundReadable(int engineH) { return engineH != 0; }

static int RagGroundHeight(int engineH, int ownH, bool ownValid)
{
    if (RagGroundReadable(engineH)) return engineH;
    return ownValid ? ownH : engineH;      // no third source; 0 stands if both fail
}

// And what to tell him when the ship is commanded forward and does not move.
// There are exactly two reasons, and they need opposite actions.
enum RagStillWhy { RAG_STILL_UNKNOWN = 0, RAG_STILL_GROUNDED, RAG_STILL_BLOCKED };

static RagStillWhy RagWhyNotMoving(bool gapKnown, int gap, int setdownGate)
{
    if (!gapKnown) return RAG_STILL_UNKNOWN;
    return (gap < setdownGate) ? RAG_STILL_GROUNDED : RAG_STILL_BLOCKED;
}
