// world_garden.inl - Mobile Balamb Garden auto-drive (#80), PART 2b: the
// collision probes and the executor.
//
// PART OF world_map.cpp -- TEXTUAL INCLUDE. Do NOT compile standalone.
// Included AFTER world_garden_grid.inl (the grid), world_garden_berths.inl
// (the berth table) and world_garden_plan.inl (reachability, docks, the aboard
// latch and the planner), and BEFORE world_map_drive.inl.
//
// The design rationale, the engine facts and the BAT history are in
// world_garden_grid.inl's header -- read that first.



// ============================================================================
// Garden drive state + executor
// ============================================================================
static const double GD_ARRIVE_DIST   = 288.0;   // .54 BAT parked 379u short of the berth, turning a 362u walk into 666u
static const double GD_LOOKAHEAD     = 1536.0;
static const double GD_FINAL_DIST    = 1400.0;
static const int    GD_STEER_DEADZONE = 320;     // ~28 deg
static const int    GD_STEER_FWD_CONE = 576;     // ~50 deg
static const DWORD  GD_ANNOUNCE_MS   = 6000;
static const int    GD_MAX_REPLANS   = 12;
// A berth the hull cannot quite thread into must not become a dead stop. Once
// the drive has had to re-plan at all, any Garden-parkable cell within this
// growing radius of the intended park point counts as arrived, and on final
// give-up any parkable cell within GD_FALLBACK_PARK is accepted as a berth.
// The walk is then announced from where the hull actually stopped, so the
// player always gets "parked, N units to go" rather than "stuck".
static const double GD_ARRIVE_PER_REPLAN = 256.0;
// v0.20.80: and a CEILING on that widening. Unbounded, twelve replans turn the
// arrival radius into 288 + 12*256 = 3,360 units, which is how the offline
// model "arrives" at the Tomb of the Unknown King 3 km away -- and it would
// have announced arrival there in game too. The widening exists to forgive a
// hull that has been shuffled about by replans, not to redefine the
// destination. Two replans' worth is plenty.
static const double GD_ARRIVE_MAX_EXTRA = 512.0;
static const double GD_FALLBACK_PARK   = 6000.0;
static const DWORD  GD_STALL_MS      = 3500;     // forward wanted, none delivered
// .55 BAT: the Timber drive tripped the stall detector 3 s in, at the exact
// coordinate it started from -- the hull was still getting under way. The
// spurious replan then widened the arrival radius and cost 500 units of berth
// accuracy. Give it a run-up before the detector arms.
static const DWORD  GD_STALL_ARM_MS  = 2500;
static const DWORD  GD_PROGRESS_MS   = 15000;    // route length must shrink
// v0.20.86: how long the hull may turn in place, going nowhere, before the
// throttle comes back on. See the pivot-deadlock note in the steering block.
static const DWORD  GD_PIVOT_MS      = 1500;
static const DWORD  GD_REVERSE_MS    = 900;
// v0.20.72: measured off Aaron's v0.20.71 BAT, from [GDTRACE] at 1 Hz.
// Standing start: 0 -> 923 -> 1841 -> 1940 units/second, i.e. the Garden needs
// about a full second to reach cruise. GD_STALL_MS is 3500 and a reverse burns
// 900 of it, so a hull that is interrupted keeps being judged before it has had
// time to accelerate. The stall detector must therefore re-arm after EVERY
// throttle interruption, not just once at the start of the drive -- which is
// all `now - s_gdStartTick > GD_STALL_ARM_MS` ever did.
static const int    GD_CLEAR_STALL_GRACE = 3;   // clear-path stalls to ride out
// v0.20.80: A WALL-FOLLOW MAY NOT CARRY THE HULL AWAY FOREVER.
//
// The .79 Tomb BAT, four consecutive seconds: blk=0, aim=0, guard on for 229
// frames, keys=U-R-, and the goal distance climbing 3,935 -> 7,605. The guard
// releases on `!blocked && (aimClear || goal < guardHit - 256)`; aimClear was 0
// because the steer target was out of sight, and the goal distance was RISING,
// so the second clause could never fire either. The release condition is
// UNSATISFIABLE exactly when the guard is doing harm, and the only escape was
// the 900-frame timeout -- fifteen seconds the wrong way, then re-engage and
// do it again.
//
// A wall-follow is a LOCAL heuristic with a 256-unit horizon. A* costs 25-190 ms
// and knows the whole map. So bound the damage: once the guard has added more
// than this to the goal distance, it has failed at its job -- drop it and let
// the planner have the problem.
static const double GD_GUARD_MAX_DRIFT = 1000.0;
// v0.20.82: inside this range of the berth a drift abort releases the guard and
// KEEPS the route, instead of reversing and replanning the approach away.
static const double GD_NEAR_GOAL_KEEP   = 3000.0;
// v0.20.90: THE BEACH RUN -- stop asking the model, ask the engine.
//
// Inside this range of a beach_climb berth the executor drives STRAIGHT AT IT
// with the bow probes and the wall-follow suppressed, and logs what the engine
// does with it once a second. The .89 BAT proved the model cannot be talked
// round here: the planner routed to the beach and the executor's own
// GdLineClear refused every chord into it (aim=0 on every sample), so the hull
// wall-followed 1-2.6 km loops for two minutes with gate=208 throughout and
// never once touched the shore. Aaron: "If it runs into walls or something like
// that so be it, but we can at least use that data to survey the area."
static const double GD_BEACH_RUN_DIST = 2000.0;
static const DWORD  GD_BEACH_RUN_MS   = 14000;   // v0.20.93: 9 s was tight once the steering is honest
// v0.20.76 refusal probe
static const int    GD_PROBE_SWEEP_N  = 16;     // headings sampled around the compass
static const DWORD  GD_PROBE_SWEEP_MS = 700;    // hold each one long enough to move
static const int    GD_PROBE_NEAR    = 320;      // ~5 frames of hull travel
static const int    GD_PROBE_FAR     = 640;
// v0.20.71: THE BOW PROBES HAVE TO BE SHORTER THAN THE GAP THE HULL IS BEING
// ASKED TO DRIVE THROUGH. GD_PROBE_FAR is 640 units -- two and a half cells. In
// a corridor of clearance 1 there is a wall about 256 units off either beam, so
// a 640-unit forward probe strikes terrain on very nearly every heading: the
// hull reads `blocked` no matter which way it points, the wall-follow guard can
// never satisfy its release condition, and it orbits. That is the offline
// Trabia-Garden-to-Centra-Ruins trace exactly -- 4,100 frames, guard engaged the
// whole way, clearance 1 to 2, ending where it started. The probe was measuring
// the very walls it was supposed to be steering between.
// Same shape as the v0.20.68 shore-threading fault and the same remedy: when the
// gap is tight, look a shorter way ahead.
static const int    GD_PROBE_TIGHT_NEAR = 320;   // = GD_PROBE_NEAR: the near probe is
static const int    GD_PROBE_TIGHT_FAR  = 320;   // already short enough; only FAR moves
static const int    GD_TIGHT_CLEAR      = 3;    // cells; below this, use the short probes

static bool     s_gdActive        = false;
static DWORD    s_gdThrottleSince = 0;   // v0.20.72: last throttle interruption
static int      s_gdClearStalls   = 0;   // v0.20.72: stalls with nothing blocking
static int      s_gdProbeStep     = 0;   // v0.20.76 refusal probe
static DWORD    s_gdProbeTick     = 0;
static int      s_gdProbeBase     = 0;
static int32_t  s_gdProbeLastX = 0, s_gdProbeLastY = 0;
static int32_t  s_gdPrevX = 0, s_gdPrevY = 0;
static int      s_gdMoveFrames = 0, s_gdFrames = 0;   // per-second movement census
static int32_t  s_gdTargetX = 0, s_gdTargetY = 0;      // the park point
static int32_t  s_gdDestX = 0, s_gdDestY = 0;          // the destination marker
static char     s_gdDestName[64] = {};
static int32_t  s_gdWalkUnits = 0;
static bool     s_gdDriveIn    = false;   // dock by driving in, rather than park and walk
static int32_t  s_gdDockX = 0, s_gdDockY = 0;          // what a drive_in run presses at
static int      s_gdDockIdx = 0;                       // which dock site is being tried
static bool     s_gdPatrol  = false;                   // last-resort shoreline search
static DWORD    s_gdPatrolSince = 0;
static const DWORD GD_PATROL_MS = 90000;

// GdDockIsAfloat / GdMissingHint moved to world_garden_inlets.inl at v0.53.0
// for the 80 KB guard; both are pure questions about a destination.
static bool     s_gdOffGrid = false;                   // hull is on a cell the grid calls blocked
static DWORD    s_gdOffGridSince = 0;
// v0.20.60: the NOSE-IN phase. Reaching the approach point is not the docking --
// Aaron: "Garden needs to steer into the FH model on the map, not pull up
// alongside it - drive right into it." So on a drive-in run the approach point
// is only where the press STARTS: the hull then drives at the dock point with
// the wall-guard switched OFF (pushing into the shoreline is the whole point),
// and sweeps the frontage if the first press does not take. The sweep step is
// 384 rather than the marker-scale 512 because the dock point is only ~1000
// units away, so 384 is already a 21-degree swing per phase and the full
// +-1152 covers the gap's whole 450-unit mouth with margin either side.
static bool     s_gdNoseIn     = false;
static DWORD    s_gdNoseSince  = 0;
static int      s_gdNosePhase  = 0;
static const DWORD GD_NOSE_PHASE_MS = 4000;   // per press position
static const int   GD_NOSE_LATERAL  = 384;
// A press that does not take leaves the hull JAMMED against the shore, and a
// jammed hull cannot re-aim: simulated on the engine grid, phases 1..6 moved it
// zero units without this. So every phase after the first opens by reversing
// clear of the shore and only then presses at its own offset.
static const DWORD GD_NOSE_BACK_MS  = 900;
static const DWORD GD_NOSE_MS       = GD_NOSE_PHASE_MS * GD_NOSE_PHASES;
static DWORD    s_gdLastAnnounce = 0;
static DWORD    s_gdStartTick = 0;
static DWORD    s_gdStallSince = 0;
static DWORD    s_gdPivotLogged = 0;   // v0.20.86: rate-limit the pivot log
static bool     s_gdInDialog = false;  // v0.20.87: a cutscene has the floor
static bool     s_gdBeachRun = false;  // v0.20.90: driving straight at a beach berth
static DWORD    s_gdBeachSince = 0;
static DWORD    s_gdBeachLog = 0;
static double   s_gdBeachClosest = 1e30;  // v0.20.93: best approach during the run
// v0.20.96: for a beach_climb berth the ROUTE goes to the approach point and the
// beach push only starts once the hull is there. Zero means "no approach point".
static int32_t  s_gdBeachApproachX = 0, s_gdBeachApproachY = 0;
static const double GD_BEACH_ARM_DIST = 500.0;
static DWORD    s_gdReverseUntil = 0;
static DWORD    s_gdBestSince = 0;
static double   s_gdBestRemain = 1e30;
static int      s_gdReplans = 0;
static int      s_gdGuardDir = 0;
static bool     s_gdGuardOn = false;
static int      s_gdLastGuardDir = 0;   // v0.20.66: side of the previous wall-follow
static int      s_gdGuardCycles = 0;    // consecutive engagements with no progress
static int      s_gdGuardMinFrames = 0; // committed frames after a limit-cycle brake
static int      s_gdGuardFrames = 0;
static double   s_gdGuardHit = 0.0;    // distance to goal when the wall was hit
static int32_t  s_gdLastX = 0, s_gdLastY = 0;

static void Garden_Stop(const char* reason)
{
    s_gdBeachGoalIdx = -1;        // v0.20.88
    if (!s_gdActive) return;
    ReleaseAllDriveKeys();
    s_gdActive = false;
    s_drivePathLen = 0; s_drivePathIdx = 0; s_drivePathPlanned = false;
    Log::World("WorldMap: [GARDEN] stopped: %s", reason ? reason : "(silent)");
    if (reason && *reason) ScreenReader::Speak(reason, true);
}

// Called once per Poll while on the world map, BEFORE the catalog is built.
static void Garden_LogTriggerState(int32_t px, int32_t py);

static void Garden_UpdateAboard()
{
    if (!s_gdLoaded) return;
    const bool prev = s_gdAboard;
    const int  id   = GetActiveVehicleId();
    int32_t px = 0, py = 0, pz = 0;
    GetWorldMapPosition(&px, &py, &pz);

    const DWORD now = GetTickCount();
    // While a Garden drive is running, or while the engine names the Garden in
    // motion, the live position IS the hull -- so keep a record of where it is.
    if ((s_gdActive || id == 0x30) && (px != 0 || py != 0)) {
        s_gdHullX = px; s_gdHullY = py; s_gdHullKnown = true; s_gdOffSince = 0;
    }

    if (id == 0x30) {
        s_gdAboard = true;                      // the engine agrees: latch it
        s_gdEntryPending = false;
    } else if (id > 0 && id != 6) {
        s_gdAboard = false;                     // a DIFFERENT vehicle is moving
        s_gdEntryPending = false;
    } else if (s_gdEntryPending && (px != 0 || py != 0)) {
        // id is 0/foot and we have a fresh transition with a valid position.
        int32_t cx = 0, cy = 0, bx = 0, by = 0;
        const bool okC = GdReadMirror(WM_CHAR_POS_ADDR, &cx, &cy);
        const bool okB = GdReadMirror(WM_BGU_POS_ADDR,  &bx, &by);
        const double dChar = okC ? CalculateWrappedDistance(px, py, cx, cy) : 1e9;
        const double dBgu  = (okB && (bx || by)) ? CalculateWrappedDistance(px, py, bx, by) : 1e9;
        const char* verdict = "ambiguous -- latch unchanged";
        if (dChar < 64.0 && dBgu > 512.0) { s_gdAboard = false; s_gdHullKnown = false; verdict = "ON FOOT"; }
        else if (dBgu < 256.0 && dBgu < dChar) {
            s_gdAboard = true; verdict = "ABOARD";
            s_gdHullX = px; s_gdHullY = py; s_gdHullKnown = true; s_gdOffSince = 0;
        }
        s_gdEntryPending = false;
        Log::World("WorldMap: [GARDEN] entry classify: P=(%d,%d) |P-char|=%.0f |P-bgu|=%.0f -> %s",
                   px, py, dChar, dBgu, verdict);
    } else if (s_gdAboard && !s_gdActive && (px != 0 || py != 0)) {
        // ------------------------------------------------------------------
        // DISEMBARK. The v0.20.55 BAT showed the latch could never clear:
        // stepping off the mobile Garden does NOT leave the world map, so the
        // entry classification below never ran. The log has no world-map entry
        // at all between the park at 13:24:28 and the player walking away --
        // he pressed backslash 816 units from the berth and got a second
        // GARDEN drive instead of the walk-in.
        //
        // The test deliberately does NOT use the savemap mirror, whose refresh
        // rate is still unproven. While we know we are the hull -- an active
        // Garden drive, or the engine naming the Garden in motion -- the live
        // position IS the hull, so record it. Once the hull is stationary,
        // any sustained separation between the player and that recorded spot
        // means the player climbed out and walked off.
        if (s_gdHullKnown) {
            const double d = CalculateWrappedDistance(px, py, s_gdHullX, s_gdHullY);
            if (d > GD_DISEMBARK_DIST) {
                if (s_gdOffSince == 0) s_gdOffSince = now;
                else if (now - s_gdOffSince > GD_DISEMBARK_MS) {
                    s_gdAboard = false;
                    s_gdHullKnown = false;
                    Log::World("WorldMap: [GARDEN] disembark: %.0f units from the parked hull "
                               "(%d,%d) for %lums -> on foot", d, s_gdHullX, s_gdHullY,
                               (unsigned long)(now - s_gdOffSince));
                    s_gdOffSince = 0;
                }
            } else {
                s_gdOffSince = 0;
            }
        }
    }

    if (s_gdAboard == prev) return;

    Log::World("WorldMap: [GARDEN] aboard %d -> %d (vehicleId=%d, pos=(%d,%d))",
               (int)prev, (int)s_gdAboard, id, px, py);
    // The catalog means something entirely different now -- force a rebuild.
    s_catalogBuilt = false;
    if (s_gdAboard) {
        if (px != 0 || py != 0) Garden_ComputeReach(px, py);
        Garden_LogTriggerState(px, py);
        ScreenReader::Speak("Piloting Balamb Garden.", false);
    } else if (s_gdActive) {
        Garden_Stop("Left the Garden. Navigation stopped.");
    }
}

// ----------------------------------------------------------------------------
// v0.20.65: what the ENGINE will actually let the Garden drive into.
//
// Four builds were spent trying to dock at Fisherman's Horizon, and the reason
// none of them worked was a WRONG COORDINATE, not a missing trigger. The catalog
// had FH at (48811,-1653), 30 km east of the real thing, on the Esthar west coast
// where there is genuinely nothing to dock with. Aaron drove in by hand and the log
// settled it: world map exited at (18895,-2122) into field 'fhdeck2', 1644 units
// from wmsetus location record 13 at (20480,-2560).
//
// The v0.20.64 trigger-table reading was sound in itself and is kept, because it
// is still the right way to answer "can this vehicle enter this place":
//
//   * the field-entry programs (wmsetus mod-section 8 @ 2928, decoded in
//     world_map_trigger_data.inl) gate a destination on vehicle, story window
//     and REGION, where regions come from the 32x24 segment map @ 588
//   * the table's only Garden clause is program 20, locID 0x0172, region 0x0C,
//     story 636..3899 -- the northern sea around Trabia Garden
//   * a location whose segment carries NO region byte has no program, and is
//     entered unconditionally by any vehicle within range
//
// FH's real segment (col 18, row 12) is 0xFF -- no region, no program, no vehicle
// restriction. The engine admitted the Garden the moment it came within ~1650
// units. Applying a correct reading to a wrong coordinate is how v0.20.64
// produced a confident wrong answer; the lesson is to validate the INPUT to an
// analysis as hard as the analysis.
//
// This still logs the story word and the hull's region byte -- cheap, and it is
// the line that would have caught the coordinate error immediately.
static void Garden_LogTriggerState(int32_t px, int32_t py)
{
    uint16_t story = 0;
    if (!WmSafeReadBytes(WM_STORY_FLAG, &story, sizeof(story))) {
        Log::World("WorldMap: [GARDEN] trigger state: story word unreadable at 0x%08X", WM_STORY_FLAG);
        return;
    }
    const bool live = (story >= 636 && story < 3900);
    int region = -1;
    if (s_segmentRegionLoaded) {
        const int col = (int)((((int64_t)px + 0x60000) % 0x40000 + 0x40000) % 0x40000 / 8192);
        const int row = (int)((((int64_t)0x48000 - py) % 0x30000 + 0x30000) % 0x30000 / 8192);
        if (row >= 0 && row < WMX_SEG_ROWS && col >= 0 && col < WMX_SEG_COLS)
            region = s_segmentRegionMap[row][col];
    }
    Log::World("WorldMap: [GARDEN] trigger state: story=%u, the table's ONLY Garden clause "
               "(program 20, locID 0x0172, region 0x0C, story 636..3899) is %s; "
               "hull is in region 0x%02X",
               (unsigned)story, live ? "LIVE" : "EXPIRED/NOT YET ACTIVE", region & 0xFF);
    Log::World("WorldMap: [GARDEN] trigger state: a region byte of 0xFF means no trigger program "
               "and so no vehicle restriction -- Fisherman's Horizon (20480,-2560) is one of those");
}

static const char* GdCompass(int32_t fx, int32_t fy, int32_t tx, int32_t ty)
{
    const int b = TorusBearing(fx, fy, tx, ty) & 0xFFF;
    static const char* const kDirs[8] = { "north", "north-east", "east", "south-east",
                                          "south", "south-west", "west", "north-west" };
    return kDirs[((b + 256) & 0xFFF) / 512];
}

// Stop here and hand over to the on-foot drive. The remaining walk is measured
// from where the hull ACTUALLY stopped, not from the table, so a fallback berth
// announces its own true distance.
// v0.20.74: NEVER CLAIM ARRIVAL FROM A PLACE THE PLAYER CANNOT GET OFF.
//
// The .73 BAT: "the mod said I arrived at Shumi Village while the Garden was
// still in the water and I could not get off." The berth it drove to is
// terrain 33, height 0, open ocean, with no disembark bit within reach -- so
// "arrived" was a lie, and a particularly costly one for a blind player who
// then has nothing to act on.
//
// Arrival is now a claim about the DISEMBARK POINT, not about proximity. The
// hull must be standing on, or beside, a cell the engine will actually set the
// player down on. Three honest outcomes:
//
//   1. parked at a disembark point, destination close  -> "leave the Garden"
//   2. parked at a disembark point, destination far    -> "leave and walk", with
//                                                         range and compass
//   3. parked with NO disembark point                  -> say so plainly, and
//                                                         name the nearest one
//
// Aaron: "The mod has to make sure it is on land before announcing it has
// arrived, and in some instances where it may not be possible to get very
// close, such as the Tomb, it should get as close as it can then advise the
// player to get off and walk the rest of the way."
static bool GdCanDisembarkAt(int32_t px, int32_t py, int32_t* outX, int32_t* outY)
{
    const int r = GdRow(py), c = GdCol(px);
    if (r < 0 || r >= GD_ROWS) return false;
    // the engine's own set-down search (0x53E7A0) looks at a small fan around
    // the hull, so a step-off one cell away is the honest radius here.
    // v0.20.79: THE HULL ITSELF MUST BE ASHORE.
    //
    // Aaron, three times now: "The mod has to make sure it is on land before
    // announcing it has arrived", and "the Garden must go up a beach to get onto
    // land". I implemented "is there a disembark cell NEAR the hull" three times
    // instead, and the .78 BAT ended with the hull floating in open water 464
    // units off Shumi with a perfectly valid step-off it could not reach.
    //
    // The log settles it. Where Aaron GOT OFF the hull was ashore; where he
    // could not, it was afloat:
    //
    //   Centra Ruins  (got off)      terrain  7, land, foot-walkable
    //   Trabia Garden (got off)      terrain  7, land, foot-walkable
    //   Shumi .77/.78 (could not)    terrain 33, WATER
    //
    // So the test is the hull's OWN cell, not its neighbourhood.
    const uint8_t cls = s_gdCls[GdIdx(r, c)];
    if (cls & GDC_WATER) return false;      // afloat: there is nowhere to step
    if (!(cls & GDC_FOOT)) return false;    // ashore but not walkable: likewise
    if (outX && outY) { *outX = px; *outY = py; }
    return true;
}

static void Garden_Park(int32_t px, int32_t py, bool onPark, const char* how)
{
    const double walk = CalculateWrappedDistance(px, py, s_gdDestX, s_gdDestY);
    int32_t sx = 0, sy = 0;
    const bool canGetOff = GdCanDisembarkAt(px, py, &sx, &sy);
    char buf[256];
    if (!canGetOff) {
        // Do NOT say "arrived". Report the truth and give the player the range
        // and bearing they would need if they had another way in.
        {
            char wsay[48];
            WmSayDistance(walk, wsay, sizeof wsay);
            snprintf(buf, sizeof(buf),
                     "Balamb Garden stopped %s %s of %s, but there is "
                     "nowhere here to leave the Garden. The hull cannot get closer.",
                     wsay, GdCompass(px, py, s_gdDestX, s_gdDestY), s_gdDestName);
        }
    } else if (walk < 200.0) {
        snprintf(buf, sizeof(buf), "Balamb Garden parked at %s. Leave the Garden to go in.",
                 s_gdDestName);
    } else if (walk >= 5000.0) {
        // v0.20.88: "one hundred and twenty-two hundred units" is not a distance
        // anyone can hold in their head. Shumi's beach is 12 km from the village
        // by the game's own design -- south shore to north -- so past 5 km the
        // announcement switches to kilometres and says plainly that it is a hike.
        snprintf(buf, sizeof(buf),
                 "Balamb Garden parked as close as it can get. %s is %.1f kilometres %s "
                 "-- a long walk. Leave the Garden and press backslash to start it.",
                 s_gdDestName, walk / 1000.0,
                 GdCompass(px, py, s_gdDestX, s_gdDestY));
    } else {
        {
            char wsay[48];
            WmSayDistance(walk, wsay, sizeof wsay);
            snprintf(buf, sizeof(buf),
                     "Balamb Garden parked as close as it can get. %s is %s %s. "
                     "Leave the Garden and press backslash to walk the rest.",
                     s_gdDestName, wsay, GdCompass(px, py, s_gdDestX, s_gdDestY));
        }
    }
    Log::World("WorldMap: [GARDEN] parked (%s) at (%d,%d) parkBit=%d canDisembark=%d "
               "stepOff=(%d,%d) walk=%.0f to %s, %d replans",
               how, px, py, (int)onPark, (int)canGetOff, sx, sy, walk,
               s_gdDestName, s_gdReplans);
    Garden_Stop(buf);
}

static bool Garden_StartDrive(int catIdx)
{
    if (s_gdActive) return true;
    if (!s_gdLoaded) {
        ScreenReader::Speak("Garden navigation data is not loaded.", true);
        return false;
    }
    if (!s_catalogBuilt || catIdx < 0 || catIdx >= s_catalogCount) {
        ScreenReader::Speak("No locations available.", true);
        return false;
    }
    const LocationEntry& dest = s_catalog[catIdx];
    const GardenPark* gp = Garden_ParkFor(dest.name);
    char buf[256];
    // The .54 BAT: the hull parked, then backslash was pressed again while
    // still aboard, expecting the walk-in. Say so instead of re-driving.
    if (gp && gp->reachable && !gp->drive_in) {
        int32_t cx, cy, cz;
        GetWorldMapPosition_Active(&cx, &cy, &cz);
        if ((cx || cy) && CalculateWrappedDistance(cx, cy, gp->park_x, gp->park_y) < 1500.0) {
            snprintf(buf, sizeof(buf),
                     "Already parked at %s. Leave the Garden, then press backslash to walk there.",
                     dest.name);
            ScreenReader::Speak(buf, true);
            Log::World("WorldMap: [GARDEN] already at %s berth -- prompting the player to disembark", dest.name);
            return false;
        }
    }
    if (!gp || !gp->reachable) {
        // FH earns its own line: it is not "nowhere to set down", it is that the
        // world map has no Garden entry for it at all. Saying the right thing
        // matters -- the wrong one sent four builds looking for a dock.
        {
            snprintf(buf, sizeof(buf),
                     "Balamb Garden cannot reach %s. There is nowhere to set down on that landmass. "
                     "You will need the Ragnarok.", dest.name);
        }
        ScreenReader::Speak(buf, true);
        Log::World("WorldMap: [GARDEN] refused: %s has no park point", dest.name);
        return false;
    }
    int32_t px, py, pz;
    GetWorldMapPosition_Active(&px, &py, &pz);
    if (px == 0 && py == 0) {
        ScreenReader::Speak("Position unavailable. Try again.", true);
        return false;
    }
    // v0.20.88: arm (or clear) the beach-climb exception BEFORE anything asks
    // the grid a question about this berth. One goal cell, one destination.
    s_gdBeachGoalIdx = gp->beach_climb
                     ? GdIdx(GdRow(gp->park_y), GdCol(gp->park_x)) : -1;
    s_gdBeachApproachX = (gp->beach_climb && (gp->dock_x || gp->dock_y)) ? gp->dock_x : 0;
    s_gdBeachApproachY = (gp->beach_climb && (gp->dock_x || gp->dock_y)) ? gp->dock_y : 0;
    if (gp->beach_climb)
        Log::World("WorldMap: [GARDEN] beach climb armed for %s: goal cell (%d,%d) may be "
                   "entered from the water and the 200 cliff gate is waived for it. If the "
                   "engine refuses, this is where it will stop.",
                   dest.name, GdRow(gp->park_y), GdCol(gp->park_x));
    Garden_ComputeReach(px, py);
    if (!Garden_BerthReachable(gp)) {   // v0.20.89: arms the beach exception itself
        snprintf(buf, sizeof(buf),
                 "Balamb Garden cannot reach %s from here.", dest.name);
        ScreenReader::Speak(buf, true);
        Log::World("WorldMap: [GARDEN] refused: park (%d,%d) not in the hull's reachable set",
                   gp->park_x, gp->park_y);
        return false;
    }
    s_gdTargetX = gp->park_x; s_gdTargetY = gp->park_y;
    s_gdDestX = dest.x;       s_gdDestY = dest.y;
    s_gdWalkUnits = gp->walk_units;
    s_gdDriveIn   = gp->drive_in;
    // A drive_in row may name its own press point; zero means "the marker".
    s_gdDockX = (gp->dock_x || gp->dock_y) ? gp->dock_x : dest.x;
    s_gdDockY = (gp->dock_x || gp->dock_y) ? gp->dock_y : dest.y;
    s_gdDockIdx = 0;
    if (s_gdDriveIn) {
        const GardenDock* site = Garden_DockSite(dest.name, 0);
        if (site) {
            s_gdTargetX = site->approach_x; s_gdTargetY = site->approach_y;
            s_gdDockX   = site->dock_x;     s_gdDockY   = site->dock_y;
        }
    }
    strncpy(s_gdDestName, dest.name, sizeof(s_gdDestName) - 1);
    s_gdDestName[sizeof(s_gdDestName) - 1] = '\0';

    // v0.20.96: a beach_climb berth is ROUTED to its approach point -- the one
    // water cell the mouth opens from -- and pushed the last few hundred units
    // by the beach run. Routing straight at the berth is what kept delivering
    // the hull to the wrong side of it.
    const int32_t planX = s_gdBeachApproachX ? s_gdBeachApproachX : s_gdTargetX;
    const int32_t planY = s_gdBeachApproachY ? s_gdBeachApproachY : s_gdTargetY;
    if (!Garden_Plan(px, py, planX, planY)) {
        snprintf(buf, sizeof(buf), "Could not plot a course to %s.", dest.name);
        ScreenReader::Speak(buf, true);
        return false;
    }
    const DWORD now = GetTickCount();
    s_gdActive = true;
    s_gdLastAnnounce = now;
    s_gdStartTick = now;
    s_gdStallSince = now;
    s_gdThrottleSince = now;      // v0.20.72
    s_gdClearStalls = 0;          // v0.20.72
    s_gdProbeStep = 0; s_gdProbeTick = now;   // v0.20.76
    s_gdInDialog = false;         // v0.20.87
    s_gdBeachRun = false; s_gdBeachSince = 0; s_gdBeachLog = 0;   // v0.20.90
    s_gdBeachClosest = 1e30;      // v0.20.93
    s_gdProbeLastX = 0; s_gdProbeLastY = 0;
    s_gdMoveFrames = 0; s_gdFrames = 0;
    s_gdReverseUntil = 0;
    s_gdBestRemain = 1e30;
    s_gdBestSince = now;
    s_gdReplans = 0;
    s_gdGuardOn = false; s_gdGuardDir = 0; s_gdGuardFrames = 0;
    s_gdLastGuardDir = 0; s_gdGuardCycles = 0; s_gdGuardMinFrames = 0; s_gdGuardHit = 0.0;
    s_gdNoseIn = false; s_gdNoseSince = 0; s_gdNosePhase = 0;
    s_gdOffGrid = false; s_gdOffGridSince = 0;
    s_gdPatrol = false; s_gdPatrolSince = 0;
    s_gdLastX = px; s_gdLastY = py;

    const int km = (int)(CalculateWrappedDistance(px, py, s_gdTargetX, s_gdTargetY) / 1000.0);
    const char* verb = s_gdDriveIn ? "Docking Balamb Garden at" : "Piloting Balamb Garden to";
    if (km < 1) snprintf(buf, sizeof(buf), "%s %s. Very close.", verb, dest.name);
    else        snprintf(buf, sizeof(buf), "%s %s. %d kilometers.", verb, dest.name, km);
    ScreenReader::Speak(buf, true);
    Log::World("WorldMap: [GARDEN] start -> %s park=(%d,%d) walk=%d from (%d,%d)",
               dest.name, s_gdTargetX, s_gdTargetY, s_gdWalkUnits, px, py);
    return true;
}

static double GdRemainingRoute(int32_t x, int32_t y)
{
    if (!s_drivePathWorld || s_drivePathLen <= 0) return 0.0;
    double rem = CalculateWrappedDistance(x, y, s_drivePathWX[s_drivePathIdx], s_drivePathWY[s_drivePathIdx]);
    for (int i = s_drivePathIdx; i < s_drivePathLen - 1; i++)
        rem += CalculateWrappedDistance(s_drivePathWX[i], s_drivePathWY[i],
                                        s_drivePathWX[i + 1], s_drivePathWY[i + 1]);
    return rem;
}

// Choose the side to turn when the bow is blocked: scan symmetric heading
// offsets outward and commit to the first side that clears. Committing matters
// more than choosing well -- the previous behaviour (turn toward the target
// side, never move) limit-cycles at +-1 turn step against a wall, which is the
// "moveDist=0 for 18 seconds, 11 identical re-paths" signature in the
// 2026-07-31 BAT (issue H2).
// v0.20.71: pick the probe pair from how much room the hull actually has.
static void GdProbesFor(int32_t x, int32_t y, int* outNear, int* outFar)
{
    const int r = GdRow(y), c = GdCol(x);
    const bool tight = (r >= 0 && r < GD_ROWS) &&
                       ((int)s_gdClear[GdIdx(r, c)] < GD_TIGHT_CLEAR);
    *outNear = tight ? GD_PROBE_TIGHT_NEAR : GD_PROBE_NEAR;
    *outFar  = tight ? GD_PROBE_TIGHT_FAR  : GD_PROBE_FAR;
}

static int GdPickSide(int32_t x, int32_t y, int heading, int probeFar)
{
    static const int kFan[8] = { 256, 512, 768, 1024, 1280, 1536, 1792, 2048 };
    for (int i = 0; i < 8; i++) {
        const bool right = !GdProbe(x, y, heading + kFan[i], probeFar);
        const bool left  = !GdProbe(x, y, heading - kFan[i], probeFar);
        if (right && !left) return  1;
        if (left && !right) return -1;
        if (right && left)  return  1;
    }
    return 1;
}

static void Garden_Update()
{
    if (!s_gdActive) return;
    if (!Garden_IsAboard()) {
        Garden_Stop("Left the Garden. Navigation stopped.");
        return;
    }
    int32_t px, py, pz;
    GetWorldMapPosition_Active(&px, &py, &pz);
    if (px == 0 && py == 0) return;
    const uint16_t heading = GetWorldMapHeading();
    const DWORD now = GetTickCount();

    // ------------------------------------------------------------------------
    // v0.20.87: A SCRIPTED CUTSCENE HAS THE FLOOR -- PAUSE, DO NOT FIGHT IT.
    //
    // Three BATs running ended with the hull "wedged" within twenty units of
    // (60993,-44513) on the Trabia approach, and .86's refusal sweep made it
    // look like an engine block: thirteen consecutive samples over nine seconds,
    // all reading hd=3636, the heading never once moving. Aaron named it:
    //
    //   "There is a spot on the world map that causes the Garden to go haywire
    //    temporarily. Nida even comments on it when you hit that spot. It is
    //    part of the game's lore as that spot is where a Lunar Cry happened."
    //
    // The dialog log has it exactly, one second before the hull stopped:
    //
    //   [00:12:56] [SHOW_DIALOG-TEXT] win[0] mode=2 text=""Huh!?""
    //   [00:13:12] [SHOW_DIALOG-TEXT] win[0] mode=2 text="Nida "The gauge is
    //                                                      going berserk!?""
    //
    // A field-dialog window was open for the whole fifteen seconds. The engine
    // ignores movement input while one is up, which is precisely mv=0/57 with a
    // frozen heading -- nothing was refusing the terrain, nothing was pinned.
    // The mod then reversed and replanned in the middle of a cutscene.
    //
    // The FIELD auto-drive has done the right thing here since v05.37 ("Suspend
    // key injection during dialog (scripted cutscenes lock movement). Don't stop
    // the drive -- just pause until dialog clears."). The Garden drive never got
    // it. Same predicate, same treatment: release the keys, hold every watchdog
    // at the current instant so none of them fire on time that was never ours,
    // and pick up where the scene left off.
    // ------------------------------------------------------------------------
    if (FieldDialog::IsDialogOpen()) {
        ReleaseAllDriveKeys();
        s_gdStallSince    = now;
        s_gdThrottleSince = now;
        s_gdBestSince     = now;
        s_gdProbeTick     = now;
        s_gdLastAnnounce  = now;
        if (!s_gdInDialog) {
            s_gdInDialog = true;
            Log::World("WorldMap: [GARDEN] cutscene at (%d,%d) -- a dialog window is open, "
                       "so the engine is ignoring movement. Pausing the drive; this is not a stall.",
                       px, py);
        }
        return;
    }
    if (s_gdInDialog) {
        s_gdInDialog = false;
        Log::World("WorldMap: [GARDEN] cutscene over at (%d,%d) -- resuming the drive", px, py);
    }

    // v0.20.72 DIAGNOSTIC: count how many of the last second's frames produced
    // ANY change in the raw position. The .71 BAT froze at (4179,51136) with a
    // byte-identical position for 36 seconds across both forward and reverse,
    // in open water where every one of 32 bearings passes the engine's own step
    // gate. Three things could do that -- the engine refusing every move, the
    // hull moving too slowly to see at 1 Hz, or the mod reading a position that
    // is no longer being updated -- and this counter separates them.
    s_gdFrames++;
    if (px != s_gdPrevX || py != s_gdPrevY) s_gdMoveFrames++;
    s_gdPrevX = px; s_gdPrevY = py;

    // ---- cursor: nearest waypoint in a forward window, monotonic
    if (s_drivePathWorld && s_drivePathLen > 0) {
        int best = s_drivePathIdx;
        double bd = CalculateWrappedDistance(px, py, s_drivePathWX[best], s_drivePathWY[best]);
        for (int j = s_drivePathIdx + 1; j < s_drivePathLen && j < s_drivePathIdx + 41; j++) {
            const double d = CalculateWrappedDistance(px, py, s_drivePathWX[j], s_drivePathWY[j]);
            if (d < bd) { bd = d; best = j; }
        }
        s_drivePathIdx = best;
        while (s_drivePathIdx < s_drivePathLen - 1 &&
               CalculateWrappedDistance(px, py, s_drivePathWX[s_drivePathIdx],
                                        s_drivePathWY[s_drivePathIdx]) < GD_ARRIVE_DIST)
            s_drivePathIdx++;
    }

    const double goalDist = CalculateWrappedDistance(px, py, s_gdTargetX, s_gdTargetY);
    const int hr = GdRow(py), hc = GdCol(px);
    const bool onPark = (hr >= 0 && hr < GD_ROWS) && (s_gdCls[GdIdx(hr, hc)] & GDC_PARK);

    // ------------------------------------------------------------------
    // v0.20.63: OFF-GRID RECOVERY -- the hull is standing somewhere the
    // planner grid calls untraversable.
    //
    // This is not a rare corner. The 256-unit grid is deliberately
    // CONSERVATIVE: a cell is traversable only if all four of its 128-unit
    // sub-points are, so every coastline has a fringe of cells the engine
    // will happily let the hull skim through and the grid calls solid. Once
    // the hull is in one, EVERY probe fails -- GdLineClear rejects the first
    // sample because it is still in the blocked cell, and GdStepOpen reads
    // edge bits that a blocked cell never has -- so the wall guard fans out,
    // finds "blocked" on every heading, and the executor is paralysed. The
    // .62 BAT is exactly this: `bow blocked` on every replan, `remain`
    // unchanged, hull motionless at (-16605,-1050) for ninety seconds. Its
    // fine cell there is terrain 33 at height 0, open water; it is the 256
    // cell around it that is blocked, by a cliff in its other corner.
    //
    // So when the hull is off-grid, stop asking the grid anything. Steer at
    // the nearest traversable cell centre and drive, guard bypassed, until
    // back on it.
    if (!(hr >= 0 && hr < GD_ROWS) || !(s_gdCls[GdIdx(hr, hc)] & GDC_WALK)) {
        if (!s_gdOffGrid) {
            s_gdOffGrid = true; s_gdOffGridSince = now;
            Log::World("WorldMap: [GARDEN] OFF-GRID at (%d,%d) cell (%d,%d) -- "
                       "the hull is on a cell the planner calls blocked", px, py, hr, hc);
        }
        int sr = hr, sc = hc;
        if (GdSnapWalk(&sr, &sc, 20)) {
            int32_t tx = 0, ty = 0;
            GdCellToWorld(sr, sc, &tx, &ty);
            int oerr = ((int)TorusBearing(px, py, tx, ty) - (int)heading) & 0xFFF;
            if (oerr > 2048) oerr -= 4096;
            const int ooff = oerr < 0 ? -oerr : oerr;
            // Six seconds of nosing at it is enough; after that back out
            // instead, because the way in may not be the way out.
            const bool backOut = (now - s_gdOffGridSince) > 6000;
            bool oUp = !backOut && (ooff <= GD_STEER_FWD_CONE);
            bool oL = false, oR = false;
            if (ooff > GD_STEER_DEADZONE) { if (oerr >= 0) oR = true; else oL = true; }
            if (now - s_gdLastAnnounce >= GD_ANNOUNCE_MS) {
                s_gdLastAnnounce = now;
                Log::World("WorldMap: [GARDEN] off-grid recovery: steering to (%d,%d)%s",
                           tx, ty, backOut ? " in reverse" : "");
            }
            SetDriveKeys(oUp, oL, oR, backOut);
            return;
        }
        Log::World("WorldMap: [GARDEN] off-grid at (%d,%d) with no traversable cell within 20 -- stopping", px, py);
        Garden_Stop("Balamb Garden is stuck. Navigation stopped.");
        return;
    }
    if (s_gdOffGrid) {
        s_gdOffGrid = false;
        Log::World("WorldMap: [GARDEN] back on the grid at (%d,%d)", px, py);
        Garden_Plan(px, py, s_gdTargetX, s_gdTargetY);
        s_gdStallSince = now; s_gdBestRemain = 1e30; s_gdBestSince = now;
        s_gdGuardOn = false; s_gdGuardDir = 0; s_gdGuardFrames = 0;
    }

    // ---- v0.20.63: SHORELINE PATROL, the last resort for a drive_in
    // destination whose discrete contact points have all been pressed without
    // a handoff. Aim at the marker, hug whatever blocks the way, and trace.
    if (s_gdPatrol) {
        if (now - s_gdPatrolSince > GD_PATROL_MS) {
            char pb[256];
            snprintf(pb, sizeof(pb),
                     "Balamb Garden could not dock at %s. Searched the whole shore.", s_gdDestName);
            Log::World("WorldMap: [GARDEN] patrol of %s exhausted at (%d,%d) -- no field handoff anywhere "
                       "the hull can reach", s_gdDestName, px, py);
            Garden_Stop(pb);
            return;
        }
        static DWORD s_patTrace = 0;
        if (now - s_patTrace >= 1000) {
            s_patTrace = now;
            Log::World("WorldMap: [GARDEN] patrol trace (%d,%d) hd=%d dist to %s %d",
                       px, py, (int)heading, s_gdDestName,
                       (int)CalculateWrappedDistance(px, py, s_gdDestX, s_gdDestY));
        }
        int pNear = GD_PROBE_NEAR, pFar = GD_PROBE_FAR;
        GdProbesFor(px, py, &pNear, &pFar);
        const bool pBlocked = GdProbe(px, py, heading, pNear) ||
                              GdProbe(px, py, heading, pFar);
        int perr = ((int)TorusBearing(px, py, s_gdDestX, s_gdDestY) - (int)heading) & 0xFFF;
        if (perr > 2048) perr -= 4096;
        const int poff = perr < 0 ? -perr : perr;
        bool pUp = true, pL = false, pR = false;
        if (pBlocked) {
            // Keep the shore on one side and KEEP MOVING. v0.20.63 set pUp
            // false here, so a blocked bow turned on the spot forever -- the
            // .63 BAT patrol sat at (47710,-5141) for 82 seconds doing exactly
            // that. Creep forward whenever the near probe is clear.
            pR = true;
            pUp = !GdProbe(px, py, heading, pNear);
        } else if (poff > GD_STEER_DEADZONE) {
            if (perr >= 0) pR = true; else pL = true;
            pUp = (poff <= GD_STEER_FWD_CONE);
        }
        SetDriveKeys(pUp, pL, pR, false);
        return;
    }

    // ---- arrival: parked. The radius widens once the drive has had to
    // re-plan, but only onto ground the player can actually step off onto.
    const double arriveExtra = (GD_ARRIVE_PER_REPLAN * s_gdReplans < GD_ARRIVE_MAX_EXTRA)
                             ? GD_ARRIVE_PER_REPLAN * s_gdReplans : GD_ARRIVE_MAX_EXTRA;
    const double arriveTol = GD_ARRIVE_DIST + arriveExtra;   // v0.20.80: bounded
    if (goalDist < GD_ARRIVE_DIST || (onPark && s_gdReplans > 0 && goalDist < arriveTol)) {
        if (s_gdDriveIn && !s_gdPatrol) {
            if (!s_gdNoseIn) {
                s_gdNoseIn = true; s_gdNoseSince = now; s_gdNosePhase = 0;
                Log::World("WorldMap: [GARDEN] drive-in reached the approach point (%d,%d) -- "
                           "nosing in at %s, dock point (%d,%d)",
                           px, py, s_gdDestName, s_gdDockX, s_gdDockY);
                ScreenReader::Speak("Moving in to dock.", true);
            }
        } else {
            Garden_Park(px, py, onPark, "arrived");
            return;
        }
    }

    // ---- periodic progress announcement
    if (now - s_gdLastAnnounce >= GD_ANNOUNCE_MS) {
        s_gdLastAnnounce = now;
        char buf[128];
        const int km = (int)(goalDist / 1000.0);
        if (km < 1) snprintf(buf, sizeof(buf), "Less than 1 kilometer.");
        else        snprintf(buf, sizeof(buf), "%d kilometers.", km);
        ScreenReader::Speak(buf, true);
    }

    // ---- NOSE-IN: press straight at the dock point, guard OFF.
    // Everything below (wall-follow, stall/replan, LOS clamping) exists to stop
    // the hull grinding into terrain. Here grinding into terrain is the whole
    // objective, so none of it runs. If the first press does not take, sweep
    // the frontage a few hundred units at a time -- the docking region is not
    // decoded, so the honest thing is to sweep it and log where.
    if (s_gdNoseIn) {
        const DWORD elapsed = now - s_gdNoseSince;
        const int phases = GdNosePhases(s_gdDestName, s_gdDockIdx);
        const int phase = (int)(elapsed / GD_NOSE_PHASE_MS);
        if (phase != s_gdNosePhase && phase < phases) {
            s_gdNosePhase = phase;
            Log::World("WorldMap: [GARDEN] nose-in site %d phase %d at (%d,%d)",
                       s_gdDockIdx, phase, px, py);
        }
        // A failed sweep is only useful evidence if it says where it swept, so
        // trace the hull once a second while it is pressing.
        static DWORD s_noseTrace = 0;
        if (now - s_noseTrace >= 1000) {
            s_noseTrace = now;
            Log::World("WorldMap: [GARDEN] nose-in trace site %d (%d,%d) hd=%d dist to press point %d",
                       s_gdDockIdx, px, py, heading,
                       (int)CalculateWrappedDistance(px, py, s_gdDockX, s_gdDockY));
        }
        if (elapsed > (DWORD)phases * GD_NOSE_PHASE_MS) {
            Log::World("WorldMap: [GARDEN] nose-in exhausted %d phases at (%d,%d) on dock site %d for %s",
                       GD_NOSE_PHASES, px, py, s_gdDockIdx, s_gdDestName);
            // v0.20.62: another place the hull can touch this landmass? Go there
            // rather than reporting failure -- one BAT should settle which site
            // the docking lives at, not one site per BAT.
            const GardenDock* next = Garden_DockSite(s_gdDestName, s_gdDockIdx + 1);
            if (next && Garden_Plan(px, py, next->approach_x, next->approach_y)) {
                s_gdDockIdx++;
                s_gdTargetX = next->approach_x; s_gdTargetY = next->approach_y;
                s_gdDockX   = next->dock_x;     s_gdDockY   = next->dock_y;
                s_gdNoseIn = false; s_gdNoseSince = 0; s_gdNosePhase = 0;
                s_gdStartTick = now; s_gdStallSince = now; s_gdReverseUntil = 0;
                s_gdBestRemain = 1e30; s_gdBestSince = now; s_gdReplans = 0;
                s_gdGuardOn = false; s_gdGuardDir = 0; s_gdGuardFrames = 0;
                s_gdLastAnnounce = now;
                Log::World("WorldMap: [GARDEN] moving to dock site %d, approach (%d,%d) press (%d,%d)",
                           s_gdDockIdx, next->approach_x, next->approach_y,
                           next->dock_x, next->dock_y);
                char sb[96];
                GdSiteAdvanceLine(s_gdDestName, s_gdDockIdx, sb, sizeof(sb));
                ScreenReader::Speak(sb, true);
                SetDriveKeys(false, false, false, false);
                return;
            }
            // v0.20.63: sites exhausted -- PATROL the shore. Pressing at the
            // eight cells that touch FH has now failed at both clusters, so the
            // remaining possibility is a proximity trigger somewhere along the
            // coast that is not at a contact point. Aim at the marker with the
            // wall guard ON and let the hull hug the shoreline for ninety
            // seconds, tracing once a second. If nothing fires in that time,
            // there is no world-map proximity trigger for this destination and
            // the search moves into the world-map event scripts -- and the trace
            // is the evidence for saying so.
            // v0.52.0 (#109): A SHORELINE PATROL NEEDS A SHORELINE.
            //
            // The patrol hugs whatever blocks the bow. On open water nothing
            // does, so it aims at a dock point it is already sitting on and
            // turns on the spot. The 2026-08-21 log is ninety seconds of it:
            // every trace line reads `dist to White SeeD Ship 5` while the
            // heading walks 0..4095 and the position moves by ten units. That
            // is not a search, it is a wait with the engine running.
            //
            // A dock point on water gets the nose-in and nothing after it.
            if (!s_gdPatrol && !GdDockIsAfloat(s_gdDriveIn, s_gdDockX, s_gdDockY)) {
                s_gdPatrol = true; s_gdPatrolSince = now;
                s_gdNoseIn = false; s_gdNosePhase = 0;
                s_gdGuardOn = false; s_gdGuardDir = 0; s_gdGuardFrames = 0;
                Log::World("WorldMap: [GARDEN] all %d dock sites tried -- patrolling the %s shoreline",
                           s_gdDockIdx + 1, s_gdDestName);
                ScreenReader::Speak("Searching along the shore.", true);
                SetDriveKeys(false, false, false, false);
                return;
            }
            if (!s_gdPatrol)
                Log::World("WorldMap: [GARDEN] dock point (%d,%d) is open water -- no shore to "
                           "patrol, stopping here", s_gdDockX, s_gdDockY);
            char b2[256];
            // v0.52.0 (#109): SAY WHAT IS ACTUALLY WRONG.
            //
            // For a dock point on open water the hull has been driven onto the
            // exact coordinate and nothing happened, which does not mean it
            // could not get there -- the 2026-08-21 log has it sitting five
            // units from the mark. It means the thing is not there to be hit.
            // For the White SeeD Ship that is a known prerequisite and the
            // player can do something about it, so say it rather than making
            // "could not dock" sound like a navigation failure.
            if (GdDockIsAfloat(s_gdDriveIn, s_gdDockX, s_gdDockY)) {
                snprintf(b2, sizeof(b2),
                         "Balamb Garden is right on the spot for %s and nothing is there. "
                         "%s", s_gdDestName, GdMissingHint(s_gdDestName));
            } else {
                snprintf(b2, sizeof(b2),
                         "Balamb Garden could not dock at %s. Tried every point on its shore the hull can reach.",
                         s_gdDestName);
            }
            Garden_Stop(b2);
            return;
        }
        // Back clear of the shore first so the next press can actually re-aim.
        if (s_gdNosePhase > 0 &&
            (elapsed - (DWORD)s_gdNosePhase * GD_NOSE_PHASE_MS) < GD_NOSE_BACK_MS) {
            SetDriveKeys(false, false, false, true);
            return;
        }
        // Aim at the dock point, offset along the frontage by the phase so the
        // whole mouth gets tried: 0, +384, -384, +768, -768, +1152, -1152.
        const int k = (s_gdNosePhase + 1) / 2;
        const int sign = (s_gdNosePhase % 2) ? 1 : -1;
        const int lateral = (s_gdNosePhase == 0) ? 0 : sign * k * GD_NOSE_LATERAL;
        const int bear = TorusBearing(px, py, s_gdDockX, s_gdDockY) & 0xFFF;
        const double perp = ((bear + 1024) & 0xFFF) / 4096.0 * 6.283185307179586;
        const int32_t nx = s_gdDockX + (int32_t)(sin(perp) * lateral);
        const int32_t ny = s_gdDockY - (int32_t)(cos(perp) * lateral);
        int nerr = ((int)TorusBearing(px, py, nx, ny) - (int)heading) & 0xFFF;
        if (nerr > 2048) nerr -= 4096;
        const int noff = nerr < 0 ? -nerr : nerr;
        bool nUp = (noff <= GD_STEER_FWD_CONE), nL = false, nR = false;
        if (noff > GD_STEER_DEADZONE) { if (nerr >= 0) nR = true; else nL = true; }
        SetDriveKeys(nUp, nL, nR, false);
        return;
    }

    // ---- aim point, line-of-sight clamped
    int32_t steerX = s_gdTargetX, steerY = s_gdTargetY;
    if (!(goalDist < GD_FINAL_DIST && GdLineClear(px, py, s_gdTargetX, s_gdTargetY)) &&
        s_drivePathWorld && s_drivePathLen > 0) {
        // v0.20.68: SHORE THREADING. A beach is one or two cells wide -- there
        // are only 8,565 climbable water-to-land entries on the entire map --
        // and a 1536-unit lookahead lets the hull cut the corner and arrive at
        // the land cell across a boundary that is NOT climbable. The engine
        // then refuses the move and the hull sits at the water's edge holding
        // UP, which is the `stalled at (3446,36029)` and `stalled at
        // (-8879,56836)` signature from the .66 and .67 BATs. Close to land,
        // follow the route waypoint by waypoint so the hull threads the actual
        // gateway the planner chose instead of improvising its own.
        int ti = s_drivePathIdx;
        const int hcl = (hr >= 0 && hr < GD_ROWS) ? (int)s_gdClear[GdIdx(hr, hc)] : 32;
        const double look = (hcl >= 3) ? GD_LOOKAHEAD : 256.0;
        double acc = CalculateWrappedDistance(px, py, s_drivePathWX[ti], s_drivePathWY[ti]);
        while (ti < s_drivePathLen - 1 && acc < look) {
            acc += CalculateWrappedDistance(s_drivePathWX[ti], s_drivePathWY[ti],
                                            s_drivePathWX[ti + 1], s_drivePathWY[ti + 1]);
            ti++;
        }
        while (ti > s_drivePathIdx && !GdLineClear(px, py, s_drivePathWX[ti], s_drivePathWY[ti]))
            ti--;
        steerX = s_drivePathWX[ti]; steerY = s_drivePathWY[ti];
    }

    int err = ((int)TorusBearing(px, py, steerX, steerY) - (int)heading) & 0xFFF;
    if (err > 2048) err -= 4096;
    const int off = err < 0 ? -err : err;

    const bool inReverse = (s_gdReverseUntil != 0 && now < s_gdReverseUntil);
    int gdProbeNear = GD_PROBE_NEAR, gdProbeFar = GD_PROBE_FAR;
    GdProbesFor(px, py, &gdProbeNear, &gdProbeFar);
    const bool blocked = !inReverse &&
                         (GdProbe(px, py, heading, gdProbeNear) ||
                          GdProbe(px, py, heading, gdProbeFar));
    const bool aimClear = GdLineClear(px, py, steerX, steerY);

    // ------------------------------------------------------------------------
    // v0.20.90: THE BEACH RUN.
    // ------------------------------------------------------------------------
    const bool beachBerth = (s_gdBeachGoalIdx >= 0);
    const bool beachArmed = s_gdBeachApproachX
        ? (CalculateWrappedDistance(px, py, s_gdBeachApproachX, s_gdBeachApproachY)
           < GD_BEACH_ARM_DIST)
        : (goalDist < GD_BEACH_RUN_DIST);
    if (beachBerth && !inReverse && !s_gdBeachRun && beachArmed) {
        s_gdBeachRun = true; s_gdBeachSince = now; s_gdBeachLog = 0;
        s_gdBeachClosest = goalDist;
        Log::World("WorldMap: [GDBEACH] %.0f units from the %s berth -- driving straight at it "
                   "with the probes and the wall-follow OFF for %lu ms. Every line below is the "
                   "engine's own answer: mv is how many of the last second's frames moved the "
                   "hull, gate is its altitude (>0 = below sea level), cls bit 0x20 = still water.",
                   goalDist, s_gdDestName, (unsigned long)GD_BEACH_RUN_MS);
    }
    if (s_gdBeachRun) {
        if (goalDist < s_gdBeachClosest) s_gdBeachClosest = goalDist;
        const int hr2 = GdRow(py), hc2 = GdCol(px);
        const uint8_t hcls2 = (hr2 >= 0 && hr2 < GD_ROWS) ? s_gdCls[GdIdx(hr2, hc2)] : 0;
        int32_t gate2 = 0;
        if (!WmSafeReadBytes(0x0203EE88, &gate2, 4)) gate2 = INT32_MIN;
        const bool ashore = !(hcls2 & GDC_WATER);
        if (now - s_gdBeachLog >= 1000) {
            s_gdBeachLog = now;
            const int e0 = ((int)TorusBearing(px, py, s_gdTargetX, s_gdTargetY)
                            - (int)heading) & 0xFFF;
            const int e1 = e0 > 2048 ? e0 - 4096 : e0;
            Log::World("WorldMap: [GDBEACH] t=%lums pos=(%d,%d) goal=%.0f (closest %.0f) "
                       "hd=%d berthOff=%d mv=%d/%d gate=%d cls=0x%02X %s",
                       (unsigned long)(now - s_gdBeachSince), px, py, goalDist,
                       s_gdBeachClosest, (int)heading, e1 < 0 ? -e1 : e1,
                       s_gdMoveFrames, s_gdFrames,
                       (int)gate2, (unsigned)hcls2, ashore ? "ASHORE" : "afloat");
        }
        if (ashore || goalDist < GD_ARRIVE_DIST) {
            Log::World("WorldMap: [GDBEACH] CLIMBED at (%d,%d) after %lu ms -- gate=%d cls=0x%02X. "
                       "The engine allows this shore; the model does not, and the model is what "
                       "needs correcting.", px, py,
                       (unsigned long)(now - s_gdBeachSince), (int)gate2, (unsigned)hcls2);
            s_gdBeachRun = false;
        } else if (now - s_gdBeachSince > GD_BEACH_RUN_MS) {
            Log::World("WorldMap: [GDBEACH] REFUSED at (%d,%d) after %lu ms straight at the berth "
                       "-- gate=%d, still afloat, %.0f units short (closest approach %.0f). "
                       "The hull moved throughout, so read this as 'did not get there', "
                       "not as 'the engine refused'.", px, py,
                       (unsigned long)(now - s_gdBeachSince),
                       (int)gate2, goalDist, s_gdBeachClosest);
            s_gdBeachRun = false;
            char bmsg[192];
            char bsay[48];
            WmSayDistance(goalDist, bsay, sizeof bsay);
            snprintf(bmsg, sizeof(bmsg),
                     "Balamb Garden could not get up the beach at %s. It is %s "
                     "away across the water. Stopping here.",
                     s_gdDestName, bsay);
            Garden_Stop(bmsg);
            return;
        }
        // v0.20.93: STEER AT THE BERTH, NOT AT THE WAYPOINT.
        //
        // The .92 run said REFUSED twice and it was not entitled to. `off` and
        // `err` above are measured against steerX/steerY -- the PLANNER CURSOR --
        // and this block reused them, so "drive straight at it" drove straight at
        // whatever waypoint the cursor happened to hold, while the cursor kept
        // advancing and replanning underneath it (three OFF-GRID recoveries in
        // the last four seconds of that run).
        //
        // The trace says so plainly: mv=23/45, 46/128, 78/294, 92/351, 110/489,
        // 140/547 -- the hull MOVED on roughly half of every frame, the engine
        // refused nothing -- while hd swung 2071 -> 1337 -> 2261 -> 2541 -> 1329
        // -> 865 and the distance went 1970 -> 1286 -> 591 -> 570 -> 911 -> 2277.
        // It closed to 591 units of the berth and then drove away from it. That
        // is my steering, not the engine's answer, and reporting it as "REFUSED"
        // would have written a false fact into the record.
        const int berr0 = ((int)TorusBearing(px, py, s_gdTargetX, s_gdTargetY)
                           - (int)heading) & 0xFFF;
        const int berr  = berr0 > 2048 ? berr0 - 4096 : berr0;
        const int boff  = berr < 0 ? -berr : berr;
        // v0.20.94: AND DO NOT HOLD FULL THROTTLE WHILE POINTING THE WRONG WAY.
        //
        // .93 steered at the berth correctly and still could not converge, and
        // the reason is geometry I already had written down elsewhere in this
        // file: the Garden turns at most 9 units of heading per frame and
        // cruises at 32 units per frame, so its turning circle is about 1,300
        // units. Driving flat out at a target 1,000-2,000 units away means the
        // circle is wider than the approach -- it can only spiral past.
        //
        // The .93 trace is that spiral, twice: berthOff starts at 53 and 182
        // (pointed straight at it), the hull closes to 1,194 and 1,035, then
        // berthOff blows out to 932, 1350, 1754 and both runs settle into the
        // same wandering loop, ending at the identical coordinate (1791,-80541).
        // Two runs, same attractor -- that is an orbit, not an obstruction.
        //
        // So throttle only inside the same forward cone the normal executor
        // uses, and turn on the spot outside it. The probes and the wall-follow
        // stay off, which is the whole point of the run; only the accelerator
        // is disciplined. The .86 pivot escape still applies, because a
        // stationary hull cannot always turn.
        const bool bUp = (boff <= GD_STEER_FWD_CONE) ||
                         (now - s_gdStallSince) > GD_PIVOT_MS;
        s_gdGuardOn = false; s_gdGuardDir = 0; s_gdGuardFrames = 0;
        s_gdBestSince = now; s_gdThrottleSince = now;
        if (bUp) s_gdStallSince = now;
        SetDriveKeys(bUp, boff > GD_STEER_DEADZONE && berr < 0,
                          boff > GD_STEER_DEADZONE && berr >= 0, false);
        return;
    }

    bool wantUp = false, wantLeft = false, wantRight = false, wantDown = false;
    if (inReverse) {
        wantDown = true;
    } else {
        // ------------------------------------------------------------------
        // v0.20.66: LIMIT-CYCLE BRAKE.
        //
        // The .65 BAT wedged 19 km short of Centra Ruins, and the position
        // track is unmistakable: the hull ran back and forth along the SAME
        // 1100-unit diagonal between (2996,36650) and (3653,35747), about five
        // seconds a lap, for ninety seconds. No `bow blocked` was logged at the
        // wedge, so the hull was never against terrain -- it was steering
        // itself in circles.
        //
        // The mechanism is the `aimClear` half of the Bug2 leave condition.
        // Turn off the wall, the aim comes back into line of sight from the new
        // heading, the guard releases, the hull swings at the goal, the bow
        // blocks, the guard re-engages and turns it away. Nothing in that loop
        // requires the hull to have got any CLOSER, so it can run forever.
        //
        // The obvious fix -- demand progress before any aim-clear release --
        // was tried and REGRESSED four routes in the offline matrix, because in
        // a tight coastal approach the guard then holds on past the berth. So
        // the brake is armed by evidence instead: count guard engagements that
        // happen without the hull having got closer since the last one, and
        // only when three stack up do we call it a cycle, flip the side and
        // commit to it long enough to actually leave. A normal wall-follow
        // never reaches the counter and behaves exactly as it did before.
        if (s_gdGuardOn) {
            s_gdGuardFrames++;
            const bool held = s_gdGuardFrames < s_gdGuardMinFrames;
            if (!held && !blocked && s_gdGuardFrames > 8 &&
                (aimClear || goalDist < s_gdGuardHit - 256.0)) {
                s_gdGuardOn = false; s_gdGuardDir = 0; s_gdGuardFrames = 0;
            } else if (s_gdGuardFrames > 900) {          // 15 s: never orbit forever
                Log::World("WorldMap: [GARDEN] wall-follow timed out at (%d,%d) -- releasing", px, py);
                s_gdGuardOn = false; s_gdGuardDir = 0; s_gdGuardFrames = 0;
            }
        }
        if (!s_gdGuardOn && blocked) {
            const bool noProgress = s_gdGuardHit > 0.0 && goalDist > s_gdGuardHit - 128.0;
            if (noProgress) s_gdGuardCycles++; else s_gdGuardCycles = 0;
            s_gdGuardOn = true; s_gdGuardFrames = 0; s_gdGuardMinFrames = 0;
            // v0.20.71: the SIDE choice keeps the FULL-LENGTH fan. The two
            // probes answer different questions: "is something immediately in
            // my way?" must be measured at less than the corridor width, but
            // "which way round is it?" must be measured far enough to see past
            // it. Shortening this one as well is what wedged Galbadia Station
            // in the offline matrix -- at 256 units every bearing in the fan
            // reads clear, so GdPickSide always returned +1 and the hull
            // committed to the wrong side of the obstacle.
            int side = GdPickSide(px, py, heading, GD_PROBE_FAR);
            const bool brake = (s_gdGuardCycles >= 3 && s_gdLastGuardDir != 0);
            if (brake) {
                side = -s_gdLastGuardDir;      // the last way round did not work
                s_gdGuardMinFrames = 180;      // 3 s committed, no early release
                s_gdGuardCycles = 0;
            }
            s_gdGuardDir = side;
            s_gdLastGuardDir = side;
            s_gdGuardHit = goalDist;
            Log::World("WorldMap: [GARDEN] bow blocked at (%d,%d) hd=%d -- wall-follow to the %s%s",
                       px, py, (int)heading, side > 0 ? "right" : "left",
                       brake ? " (limit cycle -- reversing the side and committing)" : "");
        }
        if (s_gdGuardOn) {
            // Wall-follow: keep turning the committed way, and creep forward
            // on every frame the bow happens to be clear. That forward creep
            // is what actually gets the hull around a headland.
            if (s_gdGuardDir > 0) wantRight = true; else wantLeft = true;
            wantUp = !blocked;
        } else {
            if (off > GD_STEER_DEADZONE) { if (err >= 0) wantRight = true; else wantLeft = true; }
            wantUp = (off <= GD_STEER_FWD_CONE);
            // v0.20.86: A STATIONARY GARDEN CANNOT ALWAYS TURN, SO WITHHOLDING
            // THE THROTTLE TO TURN CAN DEADLOCK.
            //
            // The .84 and .85 BATs both stopped at the same place on the Trabia
            // approach -- (60976,-44518) and (60993,-44530) -- and the trace is
            // the same fourteen seconds twice over:
            //
            //   pos frozen  hd=3616 UNCHANGED  clear=2  off=582
            //   blk=0 aim=1 guard=0/0/0  mv=0/57  keys=--R-
            //
            // off is 582 against a 576 cone, so the executor pressed RIGHT with
            // no throttle -- and the heading never moved either. The engine
            // applies rotation as part of a move, so when the candidate step is
            // refused the hull neither travels nor turns. Nothing then changes
            // off, so nothing ever restores the throttle: turn -> no move -> no
            // turn. Only the 15-second route-progress watchdog broke it, twice.
            //
            // The cone is still right for steering; it is wrong as an absolute
            // veto. With the bow clear, creeping forward is exactly what the
            // wall-follow already does to get around a headland, so after a
            // second and a half of going nowhere the throttle goes back on. A
            // healthy pivot completes well inside that -- 582 units of heading
            // at the measured 9 u/frame is about 1.1 s.
            if (!wantUp && !blocked && (now - s_gdStallSince) > GD_PIVOT_MS &&
                (now - s_gdStartTick) > GD_STALL_ARM_MS) {
                wantUp = true;
                if (now - s_gdPivotLogged > 3000) {
                    s_gdPivotLogged = now;
                    Log::World("WorldMap: [GARDEN] pivot deadlock at (%d,%d) hd=%d off=%d "
                               "-- turning in place is going nowhere, adding throttle",
                               px, py, (int)heading, (int)off);
                }
            }
        }
    }
    SetDriveKeys(wantUp, wantLeft, wantRight, wantDown);

    // ---- stall + progress watchdogs
    const double movedSince = CalculateWrappedDistance(s_gdLastX, s_gdLastY, px, py);
    if (movedSince >= 48.0) { s_gdLastX = px; s_gdLastY = py; s_gdStallSince = now; }
    // v0.20.72: the arm now applies after every throttle interruption, not only
    // at the start of the drive. Judging a hull that is still accelerating is
    // how the .71 BAT talked itself into a standstill.
    const bool stalled = wantUp && (now - s_gdStallSince) > GD_STALL_MS &&
                         (now - s_gdStartTick) > GD_STALL_ARM_MS &&
                         (now - s_gdThrottleSince) > GD_STALL_ARM_MS;

    const double remain = GdRemainingRoute(px, py);
    if (remain < s_gdBestRemain - 256.0) { s_gdBestRemain = remain; s_gdBestSince = now; }
    const bool noProgress = (now - s_gdBestSince) > GD_PROGRESS_MS;

    // v0.20.80: has the wall-follow taken us further from the goal than it is
    // ever allowed to? See GD_GUARD_MAX_DRIFT.
    const bool guardDrift = s_gdGuardOn && s_gdGuardHit > 0.0 &&
                            goalDist > s_gdGuardHit + GD_GUARD_MAX_DRIFT;

    // ------------------------------------------------------------------------
    // v0.20.70 DIAGNOSTIC: once-a-second drive trace.
    //
    // Three BATs running have now ended at a destination THE OFFLINE MATRIX
    // SAYS ARRIVES. .69 passed 450 runs with zero failures and still wedged at
    // Centra Ruins and Shumi Village in the game, so the divergence is not
    // going to be found by running the model harder -- the model is the thing
    // that is wrong. The only way to close that gap is to record, from inside
    // the game, the same state the model computes, and diff them.
    //
    // Every field here has a direct counterpart in gexec3.run(). Whatever
    // disagrees is the bug.
    // ------------------------------------------------------------------------
    {
        static DWORD s_gdTrace = 0;
        if (now - s_gdTrace >= 1000) {
            s_gdTrace = now;
            const int cl = (hr >= 0 && hr < GD_ROWS) ? (int)s_gdClear[GdIdx(hr, hc)] : -1;
            // v0.20.76: the value the Garden terrain whitelist is gated on
            // (FF8_EN.exe 0x53E3D3). If this differs between afloat and ashore
            // the whole rule resolves itself without any further guessing.
            int32_t gate = 0;
            if (!WmSafeReadBytes(0x0203EE88, &gate, 4)) gate = INT32_MIN;
            const uint8_t hcls = (hr >= 0 && hr < GD_ROWS) ? s_gdCls[GdIdx(hr, hc)] : 0;
            Log::World("WorldMap: [GDTRACE] pos=(%d,%d) hd=%d cell=(%d,%d) clear=%d "
                       "steer=(%d,%d) wp=%d/%d goal=%.0f remain=%.0f off=%d "
                       "blk=%d aim=%d guard=%d/%d/%d rev=%d replans=%d mv=%d/%d "
                       "gate=%d cls=0x%02X keys=%c%c%c%c",
                       px, py, (int)heading, hr, hc, cl,
                       steerX, steerY, s_drivePathIdx, s_drivePathLen,
                       goalDist, remain, off,
                       blocked ? 1 : 0, aimClear ? 1 : 0,
                       s_gdGuardOn ? 1 : 0, s_gdGuardDir, s_gdGuardFrames,
                       inReverse ? 1 : 0, s_gdReplans,
                       s_gdMoveFrames, s_gdFrames, (int)gate, (unsigned)hcls,
                       wantUp ? 'U' : '-', wantLeft ? 'L' : '-',
                       wantRight ? 'R' : '-', wantDown ? 'D' : '-');
            s_gdMoveFrames = 0; s_gdFrames = 0;
        }
    }

    // v0.20.72: A STALL WITH A CLEAR PATH IS NOT AN OBSTRUCTION -- DO NOT REVERSE.
    //
    // At the .71 freeze the trace read `blk=0 aim=1 guard=0/0/0 keys=U---`:
    // nothing in front of the bow, clean line of sight to the steer target, no
    // wall-follow engaged. The hull was not stuck on anything. Reversing there
    // threw away every unit of speed it had, and since the Garden needs about a
    // second to spool back up and the stall detector fires every 3.5, each
    // cycle ended slower than the last -- 48, 35, 15, 7, 0 units/second. The mod
    // was not defeated by the terrain, it was defeating itself.
    //
    // With a clear path the right answer is to KEEP THE THROTTLE DOWN and let
    // the hull accelerate. Ride out a few of these before falling through to the
    // reverse-and-replan, which is for the case where something really is
    // in the way.
    if (!inReverse && stalled && !blocked && aimClear &&
        s_gdClearStalls < GD_CLEAR_STALL_GRACE) {
        s_gdClearStalls++;
        Log::World("WorldMap: [GARDEN] stalled at (%d,%d) with a CLEAR path "
                   "(blk=0 aim=1) -- holding the throttle instead of reversing (%d/%d)",
                   px, py, s_gdClearStalls, GD_CLEAR_STALL_GRACE);
        s_gdStallSince   = now;
        s_gdThrottleSince = now;
        return;
    }

    // ------------------------------------------------------------------------
    // v0.20.76: REFUSAL PROBE -- ask the engine which way it will actually go.
    //
    // Aaron cannot drive the Garden by hand to somewhere he cannot see, so the
    // manual trace that settled Fisherman's Horizon is not available here. This
    // gets the same ground truth automatically: when the engine has refused to
    // move the hull for a full second while the throttle is down and nothing is
    // blocking, sweep the heading through a fan and record, for each direction,
    // whether the hull ACTUALLY MOVED and what the grid believes about the cell
    // one move-step that way.
    //
    // Directions that move are the engine saying yes. Directions that do not are
    // it saying no. That is the real rule, sampled at the exact place my beach
    // model is wrong -- and it cannot crash the game, because it only presses
    // keys the auto-drive already presses.
    // ------------------------------------------------------------------------
    if (!inReverse && s_gdFrames > 30 && s_gdMoveFrames == 0 && wantUp && !blocked &&
        s_gdProbeStep < GD_PROBE_SWEEP_N) {
        // Rotate steadily with the throttle down and sample once per interval:
        // no new control path, just the keys the auto-drive already presses.
        if (now - s_gdProbeTick >= GD_PROBE_SWEEP_MS) {
            s_gdProbeTick = now;
            const bool movedThisStep =
                CalculateWrappedDistance(s_gdProbeLastX, s_gdProbeLastY, px, py) > 8.0;
            const double a = (double)heading / 4096.0 * 6.283185307179586;
            const int32_t ax = px + (int32_t)(sin(a) * 64.0);
            const int32_t ay = py - (int32_t)(cos(a) * 64.0);
            const int ar = GdRow(ay), ac = GdCol(ax);
            const uint8_t acls = (ar >= 0 && ar < GD_ROWS) ? s_gdCls[GdIdx(ar, ac)] : 0;
            Log::World("WorldMap: [GDPROBE] %2d/%d at (%d,%d) hd=%4d -> ENGINE %s | one step "
                       "ahead cell (%d,%d) cls=0x%02X walk=%d water=%d beach=%d park=%d h=%d",
                       s_gdProbeStep + 1, GD_PROBE_SWEEP_N, px, py, (int)heading,
                       movedThisStep ? "MOVED  " : "refused",
                       ar, ac, (unsigned)acls,
                       (acls & GDC_WALK) ? 1 : 0, (acls & GDC_WATER) ? 1 : 0,
                       (acls & GDC_BEACH) ? 1 : 0, (acls & GDC_PARK) ? 1 : 0,
                       (ar >= 0 && ar < GD_ROWS) ? (int)s_gdH[GdIdx(ar, ac)] : 0);
            s_gdProbeLastX = px; s_gdProbeLastY = py;
            s_gdProbeStep++;
            if (s_gdProbeStep >= GD_PROBE_SWEEP_N)
                Log::World("WorldMap: [GDPROBE] sweep complete at (%d,%d) -- the MOVED lines are "
                           "the engine's own answer to what the Garden may enter from here", px, py);
        }
        SetDriveKeys(true, false, true, false);   // forward + turn right: sweep the compass
        return;
    }

    // v0.20.82: NEAR THE GOAL, A DRIFT ABORT MUST NOT THROW THE APPROACH AWAY.
    //
    // The .81 Tomb BAT got to 1,006 units from the berth -- the closest any
    // build has come -- and then the drift abort fired, reversed, replanned, and
    // sent the hull EIGHT TO TEN KILOMETRES back west. It did that nine times.
    // Aaron: "It sounded like it got very close, but then may have fallen off
    // the peninsula back into the water."
    //
    // The abort itself is right: the guard had carried the hull from 1,006 to
    // 2,007. What is wrong is the recovery. A full reverse-and-replan is for a
    // hull that is lost; a hull one kilometre from its berth is not lost, it is
    // circling a peninsula. Release the guard, keep the route, and let it try
    // again from where it stands.
    if (!inReverse && guardDrift && !stalled && !noProgress &&
        goalDist < GD_NEAR_GOAL_KEEP) {
        // v0.20.90: say what is being MEASURED. goalDist is the distance to the
        // BERTH, and this line named the DESTINATION -- so at Shumi Village,
        // where the berth is a beach 12 km from the marker, it announced "1,460
        // units from Shumi Village" while the hull was still at sea and 10 km
        // from the village. Aaron: "the mod said I was 1km out, but I could
        // clearly hear the Garden was still in the ocean." That reading sent us
        // both after a coordinate error that may not exist.
        Log::World("WorldMap: [GARDEN] wall-follow drift at (%d,%d) but only %.0f units "
                   "from the %s berth (marker is %.0f away) -- releasing the guard and "
                   "keeping the approach rather than reversing away from it",
                   px, py, goalDist, s_gdDestName,
                   CalculateWrappedDistance(px, py, s_gdDestX, s_gdDestY));
        s_gdGuardOn = false; s_gdGuardDir = 0; s_gdGuardFrames = 0; s_gdGuardHit = 0.0;
        s_gdStallSince = now; s_gdThrottleSince = now;
        return;
    }

    if (!inReverse && (stalled || noProgress || guardDrift)) {
        if (guardDrift) {
            Log::World("WorldMap: [GARDEN] wall-follow at (%d,%d) has carried the hull "
                       "%.0f units FURTHER from the %s berth than where it engaged "
                       "(%.0f -> %.0f) -- abandoning it and replanning %d/%d",
                       px, py, goalDist - s_gdGuardHit, s_gdDestName,
                       s_gdGuardHit, goalDist, s_gdReplans + 1, GD_MAX_REPLANS);
            s_gdGuardOn = false; s_gdGuardDir = 0; s_gdGuardFrames = 0; s_gdGuardHit = 0.0;
        }
        Log::World("WorldMap: [GARDEN] %s at (%d,%d) remain=%.0f -- reverse + replan %d/%d",
                   stalled ? "stalled" : (guardDrift ? "guard drift" : "no route progress"),
                   px, py, remain, s_gdReplans + 1, GD_MAX_REPLANS);
        s_gdReverseUntil = now + GD_REVERSE_MS;
        s_gdThrottleSince = now + GD_REVERSE_MS;   // v0.20.72: re-arm AFTER the reverse
        s_gdClearStalls = 0;
        s_gdStallSince = now;
        s_gdBestRemain = 1e30;
        s_gdBestSince = now;
        s_gdGuardOn = false; s_gdGuardDir = 0;
        if (++s_gdReplans > GD_MAX_REPLANS) {
            // Graceful degradation: if the hull is standing somewhere the
            // player can disembark and is anywhere near the intended berth,
            // that IS a result -- a longer walk beats a dead stop.
            if (onPark && goalDist < GD_FALLBACK_PARK) {
                Garden_Park(px, py, true, "fallback berth");
                return;
            }
            char buf[192];
            snprintf(buf, sizeof(buf),
                     "Balamb Garden cannot get to %s from here. Stopping.", s_gdDestName);
            Garden_Stop(buf);
            return;
        }
        Garden_ComputeReach(px, py);
        // Escalate the clearance target on repeated failure: after two
        // attempts insist on open water, trading route length for a corridor
        // the hull can actually turn in.
        const int esc = (s_gdReplans < 2) ? GD_CLEAR_TARGET : (s_gdReplans < 5 ? 12 : 18);
        if (!Garden_Plan(px, py, s_gdTargetX, s_gdTargetY, esc)) {
            char buf[192];
            snprintf(buf, sizeof(buf), "Lost the course to %s. Stopping.", s_gdDestName);
            Garden_Stop(buf);
            return;
        }
    }
}

static void Garden_Toggle()
{
    if (s_gdActive) { Garden_Stop("Cancelled."); return; }
    Garden_StartDrive(s_catalogIndex);
}

static bool Garden_Active() { return s_gdActive; }

// Poll's world-map exit edge asks this: on a docking run, leaving the world map
// IS the arrival, not an interruption.
static bool Garden_LeavingIsArrival(char* out, size_t n)
{
    if (!s_gdActive || !s_gdDriveIn) return false;
    snprintf(out, n, "Docked at %s.", s_gdDestName);
    return true;
}

