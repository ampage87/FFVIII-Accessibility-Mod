// world_garden_berths.inl - the Garden's berth (park-point) table.
//
// PART OF world_map.cpp -- TEXTUAL INCLUDE. Do NOT compile standalone.
//
// v0.20.74 SPLIT: world_garden.inl reached 80,659 bytes against an 81,920-byte
// CI hard fail, with 1.2 KB of headroom and more berth work still to come. The
// table and its lookup are the largest self-contained block in that file and
// have no dependencies beyond GardenPark (declared in world_map_state.inl), so
// they move here verbatim. Behaviour-identical by construction; both garden
// harnesses cover it.
//
// Included AFTER world_garden_grid.inl and BEFORE world_garden.inl.

// Park table -- generated offline, see Plan & Research Documents/
// "Balamb Garden Auto-Drive - offline analysis.md".
// ============================================================================
// v0.20.58, after the v0.20.57 BAT corrected two model errors:
//
//  * FOOT connectivity was computed with the 200-unit gate applied between
//    128-unit cells. The FOOT move step is 0x20 = 32 units, so a 128 cell is
//    four steps and the faithful budget is 800 -- the same 4x error that made
//    the Garden read the continental shelf as a wall. At 200 the rule severed
//    real landmasses; Edea's House was cut off from Chocobo Forest 7's.
//  * the berth demanded that ONE cell be both Garden-traversable AND carry the
//    disembark bit. Those are two different cells: the hull stops on water it
//    may occupy, the player steps off onto adjacent ground the game allows.
//    Shumi Village's own cell carries the disembark bit and is NOT
//    Garden-traversable, so the old rule could never see it.
//
// park_x/park_y is now the HULL STOP POINT: the nearest cell to the
// destination that the Garden can occupy and that has a legal step-off point
// (byte15 & 0x02, foot-walkable, same landmass as the destination) within 384
// units. walk_units is measured from there, which is where the player actually
// ends up standing. Berths further than 6 km on foot are not berths for that
// place and are reported unreachable rather than offered as a hike.
// (struct GardenPark is declared in world_map_state.inl -- earlier files read it.)
// v0.20.83: BERTHS NOW STAND OFF BY 2-3 KILOMETRES.
//
// Aaron, after the .82 BAT: "it stopped essentially on top of the destination so
// the Garden could not land. It should stop 2-3km away from the destination on
// land so the player can get off the Garden."
//
// Obvious in hindsight -- the thing is a flying school. .82 parked it 281 units
// from the Tomb of the Unknown King, which is the length of the hull, and there
// was nowhere to set down. A berth is now the MOST OPEN land cell (highest
// clearance) in a 2,000-3,000 unit band around the marker, preferring ~2,500,
// and the walk figure the mod announces is that standoff. Destinations with no
// land in the band -- small islands, offshore platforms -- fall back to the
// nearest usable land within 6 km.
// v0.20.84: REGENERATED ON THE SHELF-BASED BEACH RULE, AND ONLY WHERE IT HAD TO BE.
//
// 17 of the 22 berths .83 shipped still satisfy all four constraints under the
// corrected grid and are kept VERBATIM -- including every one Aaron has driven
// (Tomb of the Unknown King, Trabia Garden, Centra Ruins, Deling City, Winhill,
// Timber, Balamb Town, Fire Cavern). A regeneration that moves a BAT-proven
// berth to a marginally better score is a regression risk for no gain, so this
// one moves only what fails:
//
//   Galbadia Garden, Galbadia Station   walk was 4.5-6.0 km; re-picked
//   Chocobo Forest 5                    walk was 5.4 km; re-picked
//   Shumi Village, Alien Ship 1         berth was on a DIFFERENT foot landmass
//                                       -- the .79 defect that told Aaron to
//                                       walk 4,471 units across open water
//   Dollet, Chocobo Forest 2 and 6      NEW: the shelf rule reaches them now
//
// The generator is offline/gen_berths.py and applies all four constraints in one
// pass, because every previous regeneration applied a subset and re-broke
// whatever the missing one protected.
static const GardenPark s_gardenParks[] = {
    { "Balamb Garden",                   23680,  -27008,   2639, true, false,     0,      0, false },
    // v0.20.84: unchanged coordinate, and the point is that it was never the
    // coordinate. .83 answered "plan FAILED ...->(15232,-25216)" because Balamb
    // island was a one-way pocket of the OLD height-based beach rule -- see the
    // GDC_BEACH note in world_garden_grid.inl.
    { "Balamb Town",                     15232,  -25216,   2639, true, false,     0,      0, false },
    // v0.20.84: NEW. The shelf-based beach rule opened the Dollet coast; the
    // 2-3 km band has no land on that landmass, so this is the nearest usable.
    { "Dollet",                         -19072,  -40320,   3540, true, false,     0,      0, false },
    { "Timber",                         -22656,   -7552,   2563, true, false,     0,      0, false },
    { "Galbadia Garden",                -34176,  -21888,   4525, true, false,     0,      0, false },
    { "Galbadia Station",               -34688,  -21376,   4979, true, false,     0,      0, false },
    { "Deling City",                    -61824,  -25728,   2944, true, false,     0,      0, false },
    { "Tomb of the Unknown King",       -44672,  -37248,   2391, true, false,     0,      0, false },
    { "D-District Prison",              -56704,   -2176,   3034, true, false,     0,      0, false },
    { "Galbadia Missile Base",          -69248,  -17024,   2810, true, false,     0,      0, false },
    // v0.20.65 / .73: FH is not a berth at all -- the platform is terrain 28,
    // outside the Garden whitelist, and its segment carries no region byte and so
    // no vehicle restriction. The field trigger fires at ~1650 units and this
    // stand-off is 975 out, so the handoff happens on approach, exactly as Aaron
    // drives it by hand. drive_in, walk 0.
    { "Fisherman's Horizon",             19584,   -2944,      0, true, true,      0,      0, false },
    { "Trabia Garden",                   51072,  -56960,   2405, true, false,     0,      0, false },
    { "Edea's House",                   -20864,   63872,   2405, true, false,     0,      0, false },
    // v0.51.0 (#109): A DRIVE-IN, AND IT NEVER COULD HAVE BEEN ANYTHING ELSE.
    //
    // The old row parked the hull at (2944,53376) and told the player to walk
    // 2,988 units to a marker that was 4.2 km from the ship (world_catalog.inl
    // has the whole story). With the marker corrected to wmsetus record 17,
    // (-17350,46550), the destination is open ocean: there is no land to park
    // on, no step-off, and nothing to walk. You board the White SeeD Ship by
    // running Balamb Garden into it.
    //
    // Park IS the marker. The cell is Garden-traversable water (byte 0x0F bit 5
    // set, bit 7 clear) and it is in the flood from everywhere that matters:
    // 16 gexec3 routes -- four starts x four headings, including the exact
    // Garden position in Aaron's current save and both starts from the failed
    // BAT -- arrive with **zero replans**, ending 260-287 units out, inside
    // GD_ARRIVE_DIST. Arrival then hands over to the nose-in, which presses
    // straight at the dock point with the wall guard off, because here grinding
    // into the target is the objective.
    //
    // dock 0,0 means "press at the location marker", which is the ship.
    // v0.54.0: the approach point 1,100 units east of the boarding point, and
    // the boarding point as the dock. Garden_StartDrive overrides both from
    // Garden_DockSite(name, 0), which is the same pair -- they are written out
    // here so the row reads as what it is rather than as a placeholder, and so
    // the catalog's reachability test asks about the point the hull drives to.
    { "White SeeD Ship",                -16874,   47006,      0, true, true, -17974,  47006, false },
    { "Great Salt Lake",                     0,       0,      0, false, false,     0,      0, false },
    { "Esthar City",                         0,       0,      0, false, false,     0,      0, false },
    { "Lunatic Pandora Lab",                 0,       0,      0, false, false,     0,      0, false },
    { "Lunar Gate",                          0,       0,      0, false, false,     0,      0, false },
    { "Sorceress Memorial",                  0,       0,      0, false, false,     0,      0, false },
    // v0.20.97: AN ORDINARY BERTH. Nine builds of beach machinery were spent on
    // a shore that never had to be climbed, because the destination marker was
    // on the wrong part of Winter Island (see world_catalog.inl).
    //
    // With the marker corrected to the village itself, (12274,-83958), the
    // generator's four constraints are satisfied with nothing special at all:
    // the berth lands on the SAME 6,216-cell foot landmass the Garden already
    // parks on for Chocobo Forest 2 -- ground Aaron has stood on -- 2,435 units
    // out, clearance 6.
    //
    // 16 gexec3 routes (four starts x four headings) arrive, zero replans, no
    // beach_climb, no approach point. The old berth (2944,-82048) was on the
    // island's foot-isolated west plateau: 7,034 cells, 53 Garden-masked
    // polygons in one wedge, and no route from it to anything.
    //
    // v0.20.98: MOVED EAST, BECAUSE THE WALK HAS A SIDE TOO. .97's berth
    // (12672,-81536) parked fine -- Aaron's hull ended at (12952,-81477) -- but
    // it sits WEST of the dome, so the walk in ran at the blind south-west face.
    // The generator scores a berth on distance and clearance and knows nothing
    // about which way the destination opens; that is the same blind spot .96
    // found on the water. This berth is chosen with one extra condition: the
    // straight line from its step-off to the door stays entirely inside the
    // gate-200 foot set, so the final approach has an unobstructed corridor.
    { "Shumi Village",                   13184,  -81536,   2472, true, false,     0,      0, false },
    // v0.20.100: MOBILE GALBADIA GARDEN -- a drive_in, like Fisherman's Horizon.
    //
    // The hull drives onto the coordinate itself and the story trigger fires:
    // that is exactly what happened in the .99 BAT, unasked, on the way to
    // Edea's House. So there is no berth and no walk -- park is the marker,
    // drive_in true, walk 0.
    //
    // Verified reachable: planner cell (127,414), WALK, clearance 5, and 16
    // gexec3 routes (Balamb / Trabia / Centra / Tomb x four headings) all arrive
    // with ZERO replans.
    //
    // The catalog entry is gated by WmStoryRetired -> WmGalbadiaGardenPresent,
    // so this row is only ever offered inside the story window.
    { "Mobile Galbadia Garden",         -24982,   65761,      0, true, true,      0,      0, false },
    { "Winhill",                        -48256,    7808,   2459, true, false,     0,      0, false },
    { "Centra Ruins",                     9344,   54400,   2550, true, false,     0,      0, false },
    { "Deep Sea Research Center",            0,       0,      0, false, false,     0,      0, false },
    { "Cactuar Island",                      0,       0,      0, false, false,     0,      0, false },
    { "Tears' Point",                        0,       0,      0, false, false,     0,      0, false },
    { "Island Closest to Hell",              0,       0,      0, false, false,     0,      0, false },
    { "Island Closest to Heaven",            0,       0,      0, false, false,     0,      0, false },
    { "Chocobo Forest 1",                 8832,  -62848,   2712, true, false,     0,      0, false },
    // v0.20.84: NEW, opened by the shelf rule.
    { "Chocobo Forest 2",                13440,  -80512,   2485, true, false,     0,      0, false },
    { "Chocobo Forest 3",                    0,       0,      0, false, false,     0,      0, false },
    { "Chocobo Forest 4",                    0,       0,      0, false, false,     0,      0, false },
    { "Chocobo Forest 5",                14208,   22912,   3360, true, false,     0,      0, false },
    // v0.20.84: NEW, opened by the shelf rule; nearest usable land is 5.4 km out.
    { "Chocobo Forest 6",                45184,   70784,   5414, true, false,     0,      0, false },
    { "Chocobo Forest 7",               -18560,   70016,   2576, true, false,     0,      0, false },
    // v0.20.84: HIDDEN -- the .83 berth was 2,816 units away across water on a
    // different foot landmass, the same defect as Shumi's.
    { "Alien Ship 1",                        0,       0,      0, false, false,     0,      0, false },
    { "Alien Ship 2",                        0,       0,      0, false, false,     0,      0, false },
    { "Alien Ship 3",                   -15744,   -9856,   2842, true, false,     0,      0, false },
    { "Alien Ship 4",                   -48256,    7808,   2111, true, false,     0,      0, false },
    // v0.20.84: likewise unchanged, and likewise the reason it vanished from the
    // catalog once the hull left Balamb. Both are back because the flood is
    // symmetric again, not because the berth moved.
    { "Fire Cavern",                     28288,  -27776,   2560, true, false,     0,      0, false },
};
static const int GARDEN_PARK_COUNT = (int)(sizeof(s_gardenParks) / sizeof(s_gardenParks[0]));

static const GardenPark* Garden_ParkFor(const char* name)
{
    if (!name) return nullptr;
    for (int i = 0; i < GARDEN_PARK_COUNT; i++)
        if (strcmp(s_gardenParks[i].name, name) == 0) return &s_gardenParks[i];
    return nullptr;
}
