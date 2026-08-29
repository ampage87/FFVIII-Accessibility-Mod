// rag_approach_pure.inl -- the Ragnarok is a helicopter, not a plane.
//
// A statement-free header fragment #included by world_map_drive.inl (the real
// build) and by tests/rag_approach_test.cpp.
//
// THE CORRECTION THAT MATTERS MOST HERE. v0.85.0 shipped this file explaining
// the 12:59 orbit as a TURNING CIRCLE -- "at 2,500 units per second the
// airship's turning circle is a thousand units and more across". Aaron, who
// flies it: "you can literally stop in midair and turn the ship in any direction
// you want. You do not have to turn in a wide circle." THAT IS THE WHOLE MODEL
// AND I HAD IT WRONG. The record is corrected here rather than quietly amended,
// for the same reason the v0.73.0 changelog corrected the v0.72.0 Propagator
// diagnosis: a wrong explanation that happens to sit next to a working fix is
// the most expensive kind of comment, because the next person builds on it.
//
// SO WHAT ACTUALLY CAUSED THE ORBIT. The executor's vehicle final-approach
// branch fires inside VEH_FINAL_APPROACH_DIST (1200 units) and says, in its own
// comment: "never stop to pivot: a stationary rotate-and-launch cycle at this
// range can only ORBIT the aim". For a car inside its own turning circle that is
// right, and #68 proved it. For a machine that can stop dead and spin on the
// spot it is exactly backwards -- and worse, 1200 units is HALF A SECOND of
// Ragnarok travel. The ship crossed the zone with the throttle pinned before any
// arc-steering could bend it, came out the far side, pivoted at ~1600, launched
// back in, and crossed again. The ring in the trace -- 1830, 1770, 1113, 1645,
// 958, 1618, 1281 -- is that cycle, not a radius the ship was unable to turn
// inside of. It was never flying a circle. IT WAS BEING FORBIDDEN TO STOP.
//
// The evidence for the correction was in the same trace all along: it holds one
// position for THIRTY-THREE CONSECUTIVE FRAMES while it pivots. A vehicle with a
// thousand-unit turning circle cannot do that.
//
// WHICH MAKES THE APPROACH SIMPLE, BECAUSE IT IS WHAT THE SHIP IS GOOD AT.
// Stop, aim, hop. Aim off by more than a hair and the throttle is shut entirely
// while it rotates -- free, instant, and costing only the time to turn. Aimed,
// and it spends one short pulse. If a pulse carries it PAST the target, the
// throttle shuts the moment the distance grows, and it re-aims from where it
// stopped. None of that is available to a car, and all of it is available here.
// ===========================================================================
// THE CORRECTION: UP WAS NEVER A THROTTLE.
//
// Aaron: "You ascend and descend using the up and down arrow keys." So every
// `wantUp` this file has spent three builds pulsing was an ALTITUDE command, and
// v0.85.0's "braked approach" was not braking anything -- it was intermittently
// pressing the CLIMB key and calling it gas.
//
// The 15:15 log settles it without any interpretation. Sixteen samples with UP
// held, dist frozen at 1806; then, with NOTHING PRESSED AT ALL:
//
//   [15:15:53] dist=1806 ... keys=U---
//   [15:15:54] dist=1471 ... keys=----
//   [15:15:55] dist=1037 ... keys=----
//
// FOUR HUNDRED UNITS A SECOND ON NO KEYS. The Ragnarok flies forward by itself;
// LEFT and RIGHT point it; UP and DOWN take it up and down. There is nothing to
// throttle.
//
// AND THAT EXPLAINS EVERY OBSERVATION OF THE LAST FOUR BUILDS AT ONCE. Outside
// RAG_BRAKE_DIST the ordinary steering branch holds wantUp continuously -- so on
// the long haul the ship CLIMBED the whole way, which is why 151 km crossings
// sailed over everything. Inside the brake distance the pulse took UP away, the
// ship settled back toward its natural low altitude, and it flew into the
// Fisherman's Horizon towers 1,806 units short of the pad. THE "BRAKED
// APPROACH" WAS AN UNCOMMANDED DESCENT, and it was doing the right thing on the
// cruise entirely by accident.
//
// So the policy is the one Aaron asked for outright: "make sure the airship
// ascends to max altitude when doing auto-drive so it doesn't get stuck against
// things on land." Climb on the cruise and hold it there. Come down only on the
// final approach, and only until the ship is low enough for the engine to set it
// down -- 0x54B860 refuses at |shipZ - groundH| >= 200, and three separate
// arrivals have now measured that gap at exactly 200, so that is both the
// natural altitude and the number to descend to.
static const double RAG_BRAKE_DIST   = 4000.0;  // where the approach begins

enum RagAlt { RAG_ALT_HOLD = 0, RAG_ALT_CLIMB, RAG_ALT_DESCEND };

// v0.92.0: CLIMB, AND NEVER COME DOWN. Aaron: "You do not have to descend in
// order to land. Pressing X to land/disembark descends the ship automatically."
//
// v0.89.0 built a descent for the final approach and v0.91.0 built a vertical
// settle on top of it, both on the assumption that the ship had to be LOW to be
// set down. IT DOES NOT. X flies the whole descent itself, from whatever height
// the ship is at, so every unit of altitude the drive gave away on the approach
// bought exactly nothing -- and cost the one thing that matters: flying the last
// stretch low is what put the ship into the Fisherman's Horizon towers at 15:15,
// and the cruise climb was added to stop precisely that.
//
// So the policy is now the whole of Aaron's original request and nothing else:
// "make sure the airship ascends to max altitude when doing auto-drive so it
// doesn't get stuck against things on land." Climb, arrive high, and let X do
// the rest. RAG_ALT_DESCEND survives as a value nothing returns, so the axis
// stays expressible if a reason for it is ever found.
static RagAlt RagAltitudeWant(bool flying)
{
    return flying ? RAG_ALT_CLIMB : RAG_ALT_HOLD;
}

// v0.90.0: FORWARD IS ITS OWN AXIS, AND IT ONLY OPENS WHEN THE SHIP IS AIMED.
//
// Aaron gave the full scheme: "you go forward and backward using A and W just
// like the Garden does. You ascend and descend using the up/down arrow keys...
// You turn left/right using the left and right arrow keys." A is the throttle,
// the up arrow is the climb, and SetDriveKeys pressed BOTH on one flag because
// for a car they are the same pedal. That conflation is why the evidence read
// two different ways in two builds: v0.88.0 called UP the throttle and v0.89.0
// called it the altitude, AND BOTH WERE HALF RIGHT.
//
// With the axes separated, the throttle rule is the one rotation-is-free makes
// obvious: do not fly and turn at the same time. Turning under power is what a
// wide arc is made of, and an arc is what put the ship in orbit at v0.85.0.
// Stop, turn, go.
static bool RagForwardWant(bool flying, int off, int cone)
{
    if (!flying) return false;      // the car has its own pedal and its own rules
    return off <= cone;             // aimed -> go; not aimed -> turn first
}

static const int RAG_FINAL_CONE = 96;            // ~8.4 degrees of 4096

static int RagSteerCone(bool flying, double dist, int defaultCone)
{
    if (!flying) return defaultCone;
    if (dist >= RAG_BRAKE_DIST) return defaultCone;
    return RAG_FINAL_CONE;
}

// ---------------------------------------------------------------------------
// AND IF IT STILL ORBITS, IT HAS TO SAY SO.
//
// The BAT ran "Stuck check 1/6, 2/6, 1/6, 2/6" for fifty-eight seconds and never
// reached six. The give-up counter resets on MOVEMENT, and a ship crossing and
// re-crossing its destination is moving beautifully -- 3,000 units a window. v0.84.0 fixed exactly this for the
// turn budget and left the give-up counter alone, which was half a lesson
// learned. For flight, the strike is scored on PROGRESS: the one measure an
// orbit cannot fake.
//
// Deliberately NOT applied on foot or in the car. There, a drive that is shoving
// itself off a wall is making no progress either, and the reverse-burst recovery
// that #67 spent many builds tuning depends on those strikes being spent slowly.
static bool RagStuckStrike(bool flying, double moved, double movedFloor,
                           double distNow, double distThen, double progressFloor)
{
    if (!flying) return moved < movedFloor;      // unchanged for everything else
    if (distThen <= 0.0) return false;           // no previous reading; no strike
    return (distThen - distNow) < progressFloor;
}

// ---------------------------------------------------------------------------
// COMMANDED FORWARD, AND DID NOT MOVE.
//
// Aaron: "Did a short test this time and got stuck heading for FH. I gained
// altitude then it seemed to go the rest of the way fine. Can you tell if the
// ship is getting stuck?" YES, AND THE 15:15 LOG SAYS IT IN ONE LINE REPEATED
// SIXTEEN TIMES:
//
//   [RAGSTEER] dist=1806 hdg=2255 off=65 err=-65 cone=96 gas=1 grew=0 keys=U---
//
// Distance frozen at 1806. Heading frozen at 2255. Aimed well inside the cone.
// AND UP IS BEING PRESSED. The mod asked the ship to go forward and the ship did
// not go forward, for fifteen seconds, 1,806 units short of the Fisherman's
// Horizon pad at (20871,-4323).
//
// For this hull that can mean exactly one thing. 0x53E6B0 falls through to
// `return 1` for vehicle 0x32, so no POLYGON is closed to it -- which means what
// stopped it is not in the polygon mask at all. It is a structure standing up
// off the ground, and FH is the largest structure on the map. Aaron cleared it
// by gaining altitude, which is the same fact from the pilot's seat.
//
// This is a DIFFERENT FAILURE from the give-up counter's ordinary one, and it
// deserves a different answer. "Stuck. Cannot reach destination." is true and
// useless: it tells a blind pilot nothing he can act on. Two strikes is also
// plenty -- the ordinary six exist because a car's stall is ambiguous, and this
// one is not ambiguous at all.
static const int RAG_BLOCKED_MAX = 2;

static bool RagBlockedStrike(bool flying, int gasTicks, double moved, double movedFloor)
{
    if (!flying)        return false;   // only the airship can be blocked this way
    if (gasTicks <= 0)  return false;   // never asked it to go -- pivoting is not blocked
    return moved < movedFloor;          // asked, and it did not go
}

// ---------------------------------------------------------------------------
// AND THE SETTLE THAT v0.91.0 ADDED IS GONE.
//
// That build read the 16:18 arrivals -- 2,472 and 2,654 units up against the 200
// of 0x54B860 -- and concluded the ship had arrived somewhere it could not land,
// so it held the announcement back and brought the ship down vertically on the
// spot. THE PREMISE WAS WRONG. X descends by itself, so the ship was always able
// to land at those heights, and the hold only delayed the message by up to
// twelve seconds while pressing DOWN for no reason.
//
// The 200 constant keeps ONE job, the one it has actually earned: telling
// GROUNDED from AIRBORNE at drive start (rag_ground_pure.inl). On the ground the
// altitude IS the ground -- measured at -544 against a ground height of -544 --
// and every genuine flight has read 900 to 3,400. It has never misread. What it
// is NOT is a ceiling on landing, and it should not have been described as one.
