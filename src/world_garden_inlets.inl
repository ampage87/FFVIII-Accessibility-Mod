// world_garden_inlets.inl -- the Centra inlet sweep, for a destination whose
// coordinate nobody has.
//
// PART OF world_map.cpp -- TEXTUAL INCLUDE. Do NOT compile standalone.
// Included AFTER world_garden_berths.inl (it uses struct GardenDock) and
// BEFORE world_garden_plan.inl, whose Garden_DockSite reads this table.
//
// ============================================================================
// v0.53.0 (#109): WE DO NOT KNOW WHERE THE WHITE SEED SHIP IS, SO STOP SAYING
// WE DO AND SEARCH FOR IT THE WAY THE GAME ASKS.
// ============================================================================
//
// Two BATs killed the fixed-coordinate model. v0.51.0 put the marker at
// wmsetus record 17, (-17350,46550) -- the only record in the whole
// location-marker table at sea level on deep ocean, in a bay matching every
// walkthrough's description. The hull was driven onto it twice, the second time
// with Edea's letter in hand, and the log reads `dist to press point 11` while
// the F11 screenshot shows an empty inlet. The coordinate is not the ship.
//
// It is not findable from the shipped data either, and that is worth writing
// down so nobody spends another day on it:
//
//   * NO ENTRY POLYGON. The flag sub_545EA0 tests -- byte 0x0E bit 3 -- is not
//     set on any polygon within twelve kilometres of that bay. Boarding is not
//     a terrain trigger.
//   * NOT IN THE TRIGGER TABLE. The 38 programs in wmsetus section 7 target 36
//     field ids and none of them is in 775..784, the `se\` fields.
//   * NO SAVEMAP RECORD. The WORLDMAP struct's four unnamed position slots are
//     empty in all 41 saves and in all three disc-3 saves.
//   * NOT A CONSTANT ANYWHERE. Neither wmsetus.obj, wmset.obj nor FF8_EN.exe
//     contains the coordinate of Mobile Galbadia Garden either -- and that one
//     is KNOWN, pinned by a BAT. Scripted world-map event zones are simply not
//     stored where static analysis can reach them.
//
// So the coordinate has to be found the way Mobile Galbadia Garden's was: by
// driving over it. The difference is that a sighted player is told to do
// exactly that -- Edea says only *"they may have stationed their ship by an
// inlet somewhere on the Centra continent"*, and the guides say *"sail around,
// around and around until you find the ship"*, *"look around all the jagged
// rocks"*, *"make 360 turns and check every corner"*. **That instruction is
// useless to a blind player, and automating it is the accessibility fix.**
//
// THE LIST. Every enclosed water pocket in Centra, found geometrically: a water
// cell is a candidate when land blocks at least 14 of 16 rays cast 2,560 units
// out, which is what "an inlet" means as a shape. Pockets of fewer than six
// cells are dropped as noise, each pocket contributes its single most enclosed
// cell, and only cells the Garden can actually reach from the orphanage survive
// -- one flood, the planner's own rules. That leaves FORTY-ONE, and they are
// ordered by distance from Edea's House because every account starts there:
// *"start out at the orphanage and go north"*, *"head north from Edea's house"*.
//
// The sweep ends the moment a field loads, and the mod's existing entry capture
// then records the real coordinate -- which is how this stops being a search at
// all on the second visit.
// A place the hull can be driven to and pressed at. `approach` is Garden water
// the planner can reach; `dock` is what the nose-in presses. (Declared here
// rather than in world_garden_plan.inl since v0.53.0 -- this file is included
// first, and the sweep table needs the type.)
struct GardenDock {
    const char* name;
    int32_t approach_x, approach_y;
    int32_t dock_x, dock_y;
};

static const GardenDock s_whiteSeedInlets[] = {
    // ========================================================================
    // [0] THE SHIP. BAT-CONFIRMED 2026-08-21, AND NOT A GUESS OF ANY KIND.
    //
    // The sweep found it on its ninth leg. The hull was crossing from inlet 7
    // to inlet 8 when the world map handed off:
    //
    //     [GARDEN] moving to dock site 8, approach (-20160,49600)
    //     [GROUNDH] pos(-17911,46995)
    //     WorldMap: Exited world map
    //     [GARDEN] stopped: Docked at White SeeD Ship.
    //     [DRIVE] Manual world-map exit -- lastPos=(-17974,47006)
    //     [fieldload] id=853 name='sefront1'
    //
    // Field 853 `se\sefront1` is the White SeeD Ship's deck. **(-17974, 47006)**
    // is the hull's position on the frame the transition fired, so it is inside
    // the boarding zone by construction rather than by inference. Aaron: *"The
    // ship always appears in the same place so these coordinates will be the
    // same for all players."*
    //
    // AND IT EXPLAINS THE TWO FAILURES. wmsetus record 17, (-17350,46550), was
    // **773 units short** -- close enough that it really was the ship's marker
    // and far enough that the nose-in never reached it, because park and dock
    // were the same point: the hull arrived on the mark, the bearing to the
    // dock became noise, and the lateral sweep went nowhere in particular. So
    // the approach here is a SEPARATE point 1,100 units due east, which is the
    // direction the hull was travelling when it worked. It drives west THROUGH
    // the boarding point instead of stopping on it.
    //
    // Both points are Garden water (terrain 33, clearance 3) with a clear line
    // between them.
    { "White SeeD Ship",  -16874,  47006,  -17974,  47006 },

    // ========================================================================
    // [1..] THE FALLBACK SWEEP. Every enclosed inlet in Centra, kept because it
    // is what found the ship in the first place and it costs nothing until the
    // known point misses. If these ever run, the log says so and something
    // above is wrong.
    // ========================================================================

    { "White SeeD Ship",  -27456,  63680,  -27456,  63680 },   //   6104u out:   5778 north,   1969 east   (pocket 10)
    { "White SeeD Ship",  -28608,  63296,  -28608,  63296 },   //   6216u out:   6162 north,    817 east   (pocket 20)
    { "White SeeD Ship",  -22848,  67136,  -22848,  67136 },   //   6975u out:   2322 north,   6577 east   (pocket 16)
    { "White SeeD Ship",  -19648,  72896,  -19648,  72896 },   //  10364u out:   3438 south,   9777 east   (pocket 31)
    { "White SeeD Ship",  -18624,  73024,  -18624,  73024 },   //  11374u out:   3566 south,  10801 east   (pocket 8)
    { "White SeeD Ship",  -20032,  76736,  -20032,  76736 },   //  11883u out:   7278 south,   9393 east   (pocket 15)
    { "White SeeD Ship",  -20032,  78144,  -20032,  78144 },   //  12794u out:   8686 south,   9393 east   (pocket 39)
    { "White SeeD Ship",  -30784,  49984,  -30784,  49984 },   //  19521u out:  19474 north,   1359 west   (pocket 9)
    { "White SeeD Ship",  -20160,  49600,  -20160,  49600 },   //  21913u out:  19858 north,   9265 east   (pocket 196)
    { "White SeeD Ship",  -28352,  42176,  -28352,  42176 },   //  27303u out:  27282 north,   1073 east   (pocket 28)
    { "White SeeD Ship",   -6720,  53952,   -6720,  53952 },   //  27495u out:  15506 north,  22705 east   (pocket 600)
    { "White SeeD Ship",    -960,  54080,    -960,  54080 },   //  32353u out:  15378 north,  28465 east   (pocket 230)
    { "White SeeD Ship",   -3264,  50112,   -3264,  50112 },   //  32537u out:  19346 north,  26161 east   (pocket 12)
    { "White SeeD Ship",  -35520,  37440,  -35520,  37440 },   //  32593u out:  32018 north,   6095 west   (pocket 52)
    { "White SeeD Ship",   -2496,  49856,   -2496,  49856 },   //  33308u out:  19602 north,  26929 east   (pocket 43)
    { "White SeeD Ship",   -2624,  49600,   -2624,  49600 },   //  33356u out:  19858 north,  26801 east   (pocket 7)
    { "White SeeD Ship",  -15552,  39104,  -15552,  39104 },   //  33374u out:  30354 north,  13873 east   (pocket 19)
    { "White SeeD Ship",  -14400,  39616,  -14400,  39616 },   //  33411u out:  29842 north,  15025 east   (pocket 15)
    { "White SeeD Ship",  -12736,  40000,  -12736,  40000 },   //  33857u out:  29458 north,  16689 east   (pocket 8)
    { "White SeeD Ship",  -13504,  39360,  -13504,  39360 },   //  34049u out:  30098 north,  15921 east   (pocket 6)
    { "White SeeD Ship",  -12992,  38592,  -12992,  38592 },   //  34968u out:  30866 north,  16433 east   (pocket 74)
    { "White SeeD Ship",  -36928,  34368,  -36928,  34368 },   //  35883u out:  35090 north,   7503 west   (pocket 84)
    { "White SeeD Ship",    3264,  50624,    3264,  50624 },   //  37727u out:  18834 north,  32689 east   (pocket 319)
    { "White SeeD Ship",    8000,  79680,    8000,  79680 },   //  38796u out:  10222 south,  37425 east   (pocket 175)
    { "White SeeD Ship",    9280,  72640,    9280,  72640 },   //  38836u out:   3182 south,  38705 east   (pocket 113)
    { "White SeeD Ship",   -7104,  37312,   -7104,  37312 },   //  39136u out:  32146 north,  22321 east   (pocket 11)
    { "White SeeD Ship",   -8128,  35520,   -8128,  35520 },   //  40067u out:  33938 north,  21297 east   (pocket 210)
    { "White SeeD Ship",    5824,  46656,    5824,  46656 },   //  41981u out:  22802 north,  35249 east   (pocket 6)
    { "White SeeD Ship",  -17216,  28864,  -17216,  28864 },   //  42390u out:  40594 north,  12209 east   (pocket 12)
    { "White SeeD Ship",    6336,  45888,    6336,  45888 },   //  42830u out:  23570 north,  35761 east   (pocket 16)
    { "White SeeD Ship",  -19904,  27328,  -19904,  27328 },   //  43192u out:  42130 north,   9521 east   (pocket 123)
    { "White SeeD Ship",    6336,  44480,    6336,  44480 },   //  43621u out:  24978 north,  35761 east   (pocket 77)
    { "White SeeD Ship",    3136,  40384,    3136,  40384 },   //  43652u out:  29074 north,  32561 east   (pocket 31)
    { "White SeeD Ship",   14656,  76736,   14656,  76736 },   //  44678u out:   7278 south,  44081 east   (pocket 7)
    { "White SeeD Ship",    9408,  47168,    9408,  47168 },   //  44776u out:  22290 north,  38833 east   (pocket 103)
    { "White SeeD Ship",   15424,  79040,   15424,  79040 },   //  45861u out:   9582 south,  44849 east   (pocket 232)
    { "White SeeD Ship",    3264,  35776,    3264,  35776 },   //  46937u out:  33682 north,  32689 east   (pocket 6)
    { "White SeeD Ship",    3392,  35136,    3392,  35136 },   //  47486u out:  34322 north,  32817 east   (pocket 55)
    { "White SeeD Ship",    1728,  32960,    1728,  32960 },   //  47986u out:  36498 north,  31153 east   (pocket 261)
    { "White SeeD Ship",    9024,  32192,    9024,  32192 },   //  53545u out:  37266 north,  38449 east   (pocket 51)
    { "White SeeD Ship",   18880,  43200,   18880,  43200 },   //  54981u out:  26258 north,  48305 east   (pocket 16)
};
static const int WHITE_SEED_INLET_COUNT =
    (int)(sizeof(s_whiteSeedInlets) / sizeof(s_whiteSeedInlets[0]));

// v0.53.0: A SWEEP IS NOT A DOCKING ATTEMPT, AND MUST NOT COST LIKE ONE.
//
// The nose-in exists to find an unknown docking point on a known shore, so it
// spends seven four-second phases sweeping the frontage. Forty-one inlets at
// twenty-eight seconds each is nineteen minutes of pressing alone. When the
// destination has no coordinate the question at each stop is only "is it
// here?", which the engine answers on the first frame the hull is in place, so
// the sweep gets the centre phase and one lateral and moves on.
// The nose-in phase counts live here rather than in world_garden.inl because
// this file is included first and GdNosePhases needs both.
static const int GD_NOSE_PHASES       = 7;   // centre, then +-384, +-768, +-1152
static const int GD_NOSE_SWEEP_PHASES = 2;   // centre plus one lateral, and move on

static bool GdIsInletSweep(const char* dest)
{
    return dest && strcmp(dest, "White SeeD Ship") == 0;
}
// Site 0 is the known boarding point and gets a full seven-phase press -- its
// lateral sweep is the margin around a coordinate measured once. Sites 1 and up
// are search stops, where the only question is "is it here?".
static int GdNosePhases(const char* dest, int siteIdx)
{
    if (!GdIsInletSweep(dest) || siteIdx <= 0) return GD_NOSE_PHASES;
    return GD_NOSE_SWEEP_PHASES;
}

// What to say on moving to the next site. A sweep is a count-out, because the
// player needs to know it is progressing and roughly how much is left; a real
// dock attempt is still "trying the other side".
static void GdSiteAdvanceLine(const char* dest, int idx, char* out, size_t n)
{
    // idx 0 is the known berth, so the sweep counts from 1 and its total
    // excludes it: moving to site 1 is "Inlet 1 of 41", not "2 of 42".
    if (GdIsInletSweep(dest)) snprintf(out, n, "Inlet %d of %d.", idx, WHITE_SEED_INLET_COUNT - 1);
    else                      snprintf(out, n, "No dock here. Trying the other side.");
}

// v0.52.0 (#109): TWO SMALL QUESTIONS THE DRIVE-IN FAILURE PATH HAS TO ASK.
//
// The 2026-08-21 BAT drove the hull onto the White SeeD Ship's coordinate --
// `dist to White SeeD Ship 5` -- and then spent ninety seconds "searching the
// shore" of a bay it was floating in the middle of, and finished by reporting
// that it could not dock. Every part of that was wrong except the driving.
//
// Is the press point on water? Then there is no shore to patrol and the hull
// has already been everywhere it can be, so the search is over the moment the
// nose-in is.
static bool GdDockIsAfloat(bool driveIn, int32_t dockX, int32_t dockY)
{
    if (!driveIn) return false;
    const int r = GdRow(dockY), c = GdCol(dockX);
    if (!s_gdLoaded || r < 0 || r >= GD_ROWS) return false;
    return (s_gdCls[GdIdx(r, (c % GD_COLS + GD_COLS) % GD_COLS)] & GDC_WATER) != 0;
}

// And what should the player be told? "Could not dock" reads as a navigation
// failure, and navigation is the one thing that worked. Where the game has a
// prerequisite the player can act on, name it; otherwise say plainly that the
// spot is right and the thing is not there.
static const char* GdMissingHint(const char* dest)
{
    // The White SeeD Ship does not exist on the world map until Edea has told
    // you where it is. Every walkthrough says so -- *"you will not find it
    // unless you talk to her first"* -- and the BAT screenshot is an empty bay
    // with the Garden sitting on the mark.
    if (dest && strcmp(dest, "White SeeD Ship") == 0)
        return "Searched every inlet in Centra and the ship was in none of them. "
               "If you have not spoken to Edea yet, do that first -- the ship "
               "does not appear until she tells you where it is.";
    return "Whatever is meant to be here has not appeared yet.";
}
