// world_catalog.inl - Location catalog + reachability + vehicle tracking
//
// PART OF world_map.cpp -- TEXTUAL INCLUDE. Do NOT compile standalone.
//
// v0.19.x ISOLATION: renamed VERBATIM from world_map_catalog.inl (only this
// header comment changed). The world catalog was already one self-contained
// file, so isolation is a pure rename -- behavior-identical by construction,
// proven byte-for-byte over the world_harness / run_diff_world fixtures.
// Holds the world-map destination catalog (s_locations[]) plus all the
// functions that derive from it: BFS reachability filter, distance-sorted
// catalog builder, vehicle-state tracker. Its only external deps remain
// LocationEntry + MAX_LOCATIONS (declared in world_map_state.inl).
//
// LOCATION_COUNT is defined here as the size of s_locations[]; state.inl
// has already declared MAX_LOCATIONS as a generous upper bound on this.
// We static_assert that LOCATION_COUNT <= MAX_LOCATIONS so state arrays
// never overflow.

// ============================================================================
// Location catalog — v0.14.85.1: rebuilt from canonical research doc
// ============================================================================
// Source: `Plan & Research Documents/World Map Location Coordinates Research
// Findings.md`. The doc enumerates the 26 numbered world-map markers plus
// 7 chocobo forests, 4 alien encounters, and Fire Cavern.
//
// LocationEntry struct is defined in world_map_state.inl (so state arrays
// can reference it); s_locations[] data and its derived LOCATION_COUNT
// live here in catalog.inl.

static const LocationEntry s_locations[] = {
    // Numbered markers 1-26 (canonical FinalFantasyKingdom set)
    {"Balamb Garden",              24576,  -29406},
    {"Balamb Town",                13249,  -26779},   // canonical name 'Balamb'; kept 'Town' for clarity vs Garden
    {"Dollet",                    -15639,  -39437},
    {"Timber",                    -22564,   -4867},
    {"Galbadia Garden",           -37471,  -25062},
    // v0.18.3.199: the train/Galbadia station is a SEPARATE enterable field whose
    // footprint sits right beside the Galbadia Garden icon -- auto-drive to
    // G-Garden kept entering it. It has no research marker, so it was never in
    // this catalog. Added as its own destination.
    // v0.18.3.200: base set to the REAL coordinate discovered from the .198
    // mis-capture (logged as "Migrated mis-captured coord (-38394,-24803)"), so
    // distance/reachability and manual-capture attribution are correct (the .199
    // placeholder was ~800u east of the icon; the station is actually ~920u WEST).
    // Rename freely if a better canonical name is known.
    {"Galbadia Station",          -38394,  -24803},
    {"Deling City",               -61806,  -28649},
    {"Tomb of the Unknown King",  -42471,  -36562},
    {"D-District Prison",         -55306,   -4841},
    {"Galbadia Missile Base",     -71695,  -15591},
    // v0.20.65: (48811,-1653) was WRONG BY 30 KM -- it is a stretch of the
    // Esthar west coast, terrain 7, not Garden-navigable and carrying no
    // disembark bit, which is why four builds of dock-hunting found nothing
    // there to dock with. Aaron drove the Garden into FH by hand and the log
    // gives the answer outright: world map exited at (18895,-2122) into field
    // 'fhdeck2'. That is 1644 units from wmsetus location record 13,
    // (20480,-2560) -- a platform in open ocean, terrain 28, Garden-navigable,
    // with 122 Garden-disembark cells clustered around it. FH is a normal
    // drive-to destination and always was.
    {"Fisherman's Horizon",        20480,   -2560},
    {"Trabia Garden",              48893,  -57979},
    // v0.21.1: **THE MARKER WAS 9.6 KM FROM THE ORPHANAGE, AND IT IS THE SHUMI
    // FAILURE REPEATING ALMOST TO THE KILOMETRE.**
    //
    // Aaron, on disc 3: *"Auto-Drive says searching for entrance, then it sounds
    // like it is going back and forth in front of the location... Also tried to
    // randomly walk into it but was unsuccessful."* The drive was not failing --
    // the log's last twenty samples are dist=5 and dist=26, it was standing ON
    // the marker. There is simply nothing there.
    //
    // Found the same way Shumi was, and the method is worth trusting because it
    // was audited against the whole catalog first. Every named place the world
    // map DRAWS sits on a patch of texture page 8, and a correct marker sits a
    // few hundred units outside it:
    //
    //     Fire Cavern 241u   Shumi 349u   Galbadia Station 502u   Tomb 540u
    //     Chocobo Forests 619-734u   Centra Ruins 741u   Winhill 881u
    //     Timber 900u   Balamb Town 1002u   Trabia 1396u   Dollet 1397u
    //
    // Nineteen markers land in that band. **Edea's House was 7,110 units from
    // its nearest patch -- and that patch is a 32-poly one already claimed by
    // Chocobo Forest 7 at 728u.** It had no settlement of its own anywhere near
    // it.
    //
    // THE WHOLE SOUTHERN HALF OF THE MAP CONTAINS EXACTLY FOUR PAGE-8 PATCHES:
    //
    //     291 polys (  6145, 55295)  Centra Ruins        claimed, 741u
    //      32 polys (-21005, 69632)  Chocobo Forest 7    claimed, 728u
    //      32 polys ( 44018, 75776)  Chocobo Forest 6    claimed, 684u
    //     103 polys (-29583, 70090)  UNCLAIMED           <-- the orphanage
    //
    // 103 polys is settlement-sized (Winhill 248, Shumi 248, Tomb 64) and the
    // structure measures 800 x 900 units. It carries **terrain 29** -- the same
    // signature Shumi Village has, and the terrain the planner already notes is
    // "entered via terrain-29 polygon trigger, not wmsetus script event".
    // Its apron is terrain 29 on all four faces; the EAST face has the most of
    // it (7 polys at h -346..-188), and east is the side the player arrives from
    // after landing the Garden. This coordinate is that apron.
    //
    // WHY THE STORY GATE WAS A RED HERRING. The trigger table has program [32],
    // field 506 `ehhana1` (Edea's House - Flower Field), gated story >= 1750,
    // and the live story word reads 912 -- which looked like an answer and was
    // not one. Aaron: *"Edea's House is now open at this point in the game. It
    // becomes reachable at the start of disc 3, which is where I am at now."*
    // He is right, and the terrain-29 finding explains the contradiction: the
    // orphanage is entered by walking onto its polygons, exactly as Shumi is,
    // so program [32] is a LATER, scripted visit and has nothing to do with
    // this one. A gate that fails does not mean the door is locked when the
    // door is not that gate.
    //
    // The old marker was (-23150, 62853). If this one is also wrong the mod's
    // own entry capture will say so the moment a field loads within 3,000 units
    // -- which is how Galbadia Garden's coordinate was pinned.
    //
    // v0.21.6: IT WAS ALSO WRONG, BY 600 UNITS, AND THIS IS WHY.
    //
    // The page-8 archaeology above found the right STRUCTURE and then aimed at
    // the wrong part of it. The patch is real -- 103 polygons -- but a polygon
    // only opens the door if it carries the entry flag (byte 0x0E bit 3, which
    // sub_545EA0 tests first) AND is foot-walkable (byte 0x0F bit 7, the move
    // validator's flag). At Edea's House **only seven of the 103 are both**; the
    // other 96 are the building itself, ground Squall cannot stand on. The seven
    // cover x[-29975,-29310] y[69632,70078].
    //
    // "The EAST face has the most terrain-29 apron, and east is the side the
    // player arrives from" was a plausible inference and it put the marker 234
    // units east of the only ground that works. In the 2026-08-16 BAT Aaron got
    // to (-29144,70004) -- 287 units short -- with every gate in the exe passing
    // on every frame, because they all did pass. He was never on a trigger poly.
    //
    // This coordinate is the interior point of those seven triangles farthest
    // from their edge: 140 units of margin in every direction. The matching
    // firing-area row in world_map_trigger_data.inl carries the bbox.
    {"Edea's House",              -29459,   69772},
    // v0.51.0 (#109): THE OLD COORDINATE WAS NEVER MEASURED, AND IT WAS THE
    // CENTRA RUINS MOVED BY A ROUND NUMBER.
    //
    // (4887, 51285) is (6887, 55285) minus exactly (2000, 4000) -- the Centra
    // Ruins marker two lines below, nudged. It put the ship 4,205 units inland
    // on terrain 7, on a foot landmass the player cannot reach, and the
    // 2026-08-21 BAT is what that costs: the Garden parked at (2899,53093) and
    // announced *"White SeeD Ship is 27 hundred units north-east. Leave the
    // Garden and press backslash to walk the rest."* There is nothing there.
    // WORLDMAP_CATALOG_STORY_WINDOWS.md had already flagged it -- *"the
    // coordinate came from the original research document and has never been
    // driven to"* -- and the audit measured it as 4.5 km from any wmsetus
    // record. It was.
    //
    // THE REAL ONE IS wmsetus.obj SECTION 8, RECORD 17. That section is the
    // location-marker table this catalog is built from: 12-byte records of
    // int32 X, int32 Y, int16 Z, int16 flags at file offset 5580. Record 16 is
    // the Centra Ruins at (6887,55285) and record 18 is Edea's House at
    // (-29425,69458) -- both already in this file, both BAT-confirmed -- so the
    // base and the stride are not assumed, they are pinned by neighbours.
    //
    // Record 17 is (-17350, 46550), **Z = 0**, and it is the only record in the
    // whole table that sits at sea level on deep ocean (terrain 33). A ship
    // floats at zero. It carries no foot entry flag -- byte 0x0E bit 3 is clear
    // on every polygon within 2,600 units -- so it cannot be walked to at all,
    // and byte 0x0F reads 0x30: the Garden mask (0x20) is set and foot (0x80)
    // is not.
    //
    // The walkthroughs agree with the data on both counts. Position: *"a small,
    // U-shaped island north and slightly east of Edea's House"*, *"in a little
    // bay surrounded by mountainous terrain"* -- record 17 is 22,908 north and
    // 12,075 east of Edea's House, in a bay the terrain dump draws as exactly
    // that U. Method: *"run into it to board the White SeeD ship."*
    //
    // So it is a drive_in, like Fisherman's Horizon and Mobile Galbadia Garden:
    // the hull goes to the coordinate itself and there is no walk. See the
    // berth row in world_garden_berths.inl.
    // v0.54.0: THE SHIP, MEASURED. (-17974,47006) is the hull's position on the
    // frame the world map handed off to field 853 `se\sefront1` -- the White
    // SeeD Ship's deck -- during the v0.53.1 inlet sweep. Not derived, not
    // inferred: the coordinate the transition fired at. Aaron: *"The ship always
    // appears in the same place so these coordinates will be the same for all
    // players."*
    //
    // The two failed markers, for the record. (4887,51285) was the Centra Ruins
    // minus a round (2000,4000) and was never measured at all. (-17350,46550),
    // wmsetus record 17, was the real marker and **773 units short** -- see
    // world_garden_inlets.inl for why 773 was enough to miss.
    {"White SeeD Ship",           -17974,   47006},
    {"Great Salt Lake",            49888,   -2683},
    {"Esthar City",                57011,   -2295},
    {"Lunatic Pandora Lab",        79521,   -9135},
    {"Lunar Gate",                 88021,    7865},
    {"Sorceress Memorial",         81521,   11865},
    // v0.20.97: THE COORDINATE WAS WRONG. It was 7,248 units north-west of the
    // village, on a different foot landmass, and that is the whole six-build
    // story.
    //
    // Found by identifying the world map's own settlement marker. Every wmx
    // polygon carries a texture page in byte 14, and every named place on the
    // world map sits on a patch of page 8 with the location's wmsetus record
    // 600-1,300 units OUTSIDE it (Balamb Town 1,102, Timber 993, Deling City
    // 1,280, Winhill 809, Dollet 1,072, Trabia Garden 1,221 -- the arrival point
    // is deliberately outside the entry trigger so you don't re-enter on exit).
    //
    // Winter Island has exactly TWO page-8 patches:
    //     32 cells, terrain 1, centred (10752,-80384)  -- Chocobo Forest 2
    //     71 cells, terrain 29, centred (12274,-83958) -- SHUMI VILLAGE
    //
    // 32 cells of page 8 is the Chocobo Forest signature; there are seven such
    // patches map-wide. The 71-cell patch is town-sized (Balamb Town 135,
    // Trabia Garden 184, Winhill 68) and is the only other structure on the
    // island. wmsetus record 20 (13000,-83977) -- the point Aaron walked to and
    // stood on for fifty seconds with nothing loading -- is 725 units due EAST
    // of it, exactly the arrival-point offset every other town shows. He was
    // standing just outside the door.
    //
    // The old coordinate's foot landmass is 7,034 cells with no Garden landing
    // anywhere on it. The village's is 6,216 cells and is the one the Garden
    // already parks on for Chocobo Forest 2.
    // v0.20.98: THE MARKER IS THE DOOR, NOT THE BUILDING.
    //
    // The .97 BAT landed the Garden correctly and then the foot drive wedged
    // 778 units short, at (11962,-83245), on the dome's SOUTH-WEST face, and the
    // un-wedge bursts walked it back out to 2,478 and eventually into the
    // Chocobo Forest. The height map says why: the dome sits on a platform whose
    // rim clears the engine's 200-unit step gate on every side BUT ONE. Flood
    // the foot mask from the Garden's own step-off at (12952,-81477) with that
    // gate and exactly five cells of the structure come back reachable --
    //
    //   (12736,-83776) (12736,-83904) (12736,-83968) (12736,-84032) (12736,-84288)
    //
    // -- one 640-unit strip on the EAST face, all at height -878 on flat snow.
    // That is the apron in front of the entrance, and wmsetus record 20,
    // (13000,-83977), sits 264 units due east of its middle: you step out of
    // Shumi, cross the apron, and you are on the world map.
    //
    // So the destination is (12736,-83968) -- the middle of that strip, directly
    // west of the arrival record. The old marker was the centre of the building
    // footprint, which is not walkable at all, so the planner snapped to
    // whatever was nearest and sent the player at the back wall.
    {"Shumi Village",              12736,  -83968},
    // v0.20.100: MOBILE GALBADIA GARDEN, and it exists for a WINDOW.
    //
    // Aaron: "That trigger is where Mobile Galbadia Garden is located on the
    // World Map, so that is how it should be identified in the catalog. Also,
    // once the battle of the Gardens is over, Galbadia Garden disappears from
    // the world map forever."
    //
    // Pinned from the v0.20.99 BAT, where auto-drive to Edea's House crossed it:
    //
    //   [DRIVE] Manual field entry at (-24982,65761) -- nearest location
    //           Edea's House is 3437u away (> 3000), not capturing
    //   [fieldload] id=253 name='bgsido_4'
    //   FieldAnnounce: name='B-Garden - Headmaster's Office 5'
    //
    // Driving onto it loads field 253, so arrival needs no special handling --
    // the drive already ends when a field loads. It is a drive_in destination:
    // the hull goes to the coordinate itself, and 16 gexec3 routes from four
    // starts arrive with zero replans.
    //
    // No map archaeology could have found this. The spot is terrain 7, texture
    // page 128, with nothing but more of the same for 1,000 units in every
    // direction, and the nearest page-8 settlement patch is 5,249 units away.
    // The page-8 method finds places the map DRAWS; a scripted event zone is a
    // pure coordinate test with no geometry behind it. The candidate offered
    // before that BAT, (-29632,70108), was 6,365 units wrong.
    {"Mobile Galbadia Garden",    -24982,   65761},
    {"Winhill",                   -50285,    6320},
    {"Centra Ruins",                6887,   55285},
    {"Deep Sea Research Center", -119138,   86000},
    {"Cactuar Island",             54806,   62040},
    {"Tears' Point",               83021,   31865},
    {"Island Closest to Hell",   -105137,   -3802},
    {"Island Closest to Heaven",  102251,  -53082},

    // Chocobo Forests (7)
    {"Chocobo Forest 1",           11332,  -63659},
    {"Chocobo Forest 2",           10927,  -81010},
    {"Chocobo Forest 3",           51893,   -3959},
    {"Chocobo Forest 4",           97253,  -48250},
    {"Chocobo Forest 5",           17383,   22013},
    {"Chocobo Forest 6",           44504,   76259},
    {"Chocobo Forest 7",          -20953,   68906},

    // Alien Encounter (UFO/PuPu) sites (4)
    {"Alien Ship 1",               79823,  -61212},
    {"Alien Ship 2",               40495,   54649},
    {"Alien Ship 3",              -12952,  -10202},
    {"Alien Ship 4",              -48806,    5808},

    // Fire Cavern (early-game dungeon on Balamb Island). #67: corrected from
    // the speedrun-sourced placeholder (36864,-28672), which sits in open
    // ocean ~5120 units off the continent (so the accurate fine-grid filter
    // would wrongly drop it), to the real on-continent coordinate.
    {"Fire Cavern",                30326,  -29221}
};
static const int LOCATION_COUNT = sizeof(s_locations) / sizeof(s_locations[0]);

// v0.16.0: state arrays are sized to MAX_LOCATIONS in state.inl; assert
// the actual catalog fits.
static_assert(LOCATION_COUNT <= MAX_LOCATIONS, "s_locations[] catalog exceeds MAX_LOCATIONS; bump MAX_LOCATIONS in world_map_state.inl");

// Find the s_locations[] index of a location by its catalog center coords.
// The drive uses (s_driveTargetX, s_driveTargetY) which were captured from
// s_catalog[catIdx].(x, y) in StartAutoDrive — those values come from
// s_locations[] (or from the refined table if has_refined was true), so
// we need to search both sources to identify which s_locations[] slot a
// given target coord belongs to. Returns -1 if no match.
static int FindLocationIndexByTargetCoords(int32_t tx, int32_t ty)
{
    for (int i = 0; i < LOCATION_COUNT; i++) {
        if (s_locations[i].x == tx && s_locations[i].y == ty) return i;
        if (s_refinedHas[i] && s_refinedX[i] == tx && s_refinedY[i] == ty) return i;
    }
    return -1;
}

// ComputeReachability (BFS flood-fill -> s_reachable[][]) moved verbatim to
// world_map_geometry.inl, included before this file. BuildDistanceCatalog
// below calls it from there. Extracted for the #67/#65 host harness.

// ============================================================================
// v0.20.99: DESTINATIONS THE STORY HAS RETIRED
// ============================================================================
// Aaron: "The original location for Balamb Garden on the Balamb continent is no
// longer accessible since Garden is now mobile. This entry shouldn't be in the
// catalog anymore at this point in the game." Same for Galbadia Garden, which
// the Sorceress takes and which is simply gone from the world map until it
// reappears near Edea's House.
//
// THE SIGNAL IS ALREADY IN HAND AND IS NOW PROVEN ACROSS A WHOLE PLAYTHROUGH.
// `bgu_pos` -- WORLDMAP struct +0x24, the same read that builds the "Mobile
// Balamb Garden" entry below -- is ZERO while the Garden is parked and live
// from the moment it moves. Read out of Aaron's own 39 save files, decompressed
// offline (an .ff8 is a 4-byte length + LZS -> 8,192 bytes, savemap at +0x184):
//
//   slot1_save01 .. slot2_save27   bgu = (0,0)          <- 37 saves, all empty
//   slot2_save28, slot2_save29     bgu = (20271,-24355) <- mobile
//
// (20271,-24355) is the same coordinate the offline berth generator has used as
// GARDEN_START since v0.20.84, which is a second independent confirmation.
//
// Galbadia Garden rides the same signal, on Aaron's testimony: "G-Garden is
// already mobile by the time B-Garden becomes mobile." So there is no window
// where the Garden is mobile and the Galbadia site is still occupied, and no
// separate story flag is needed to retire it.
//
// NOT the mobile Galbadia Garden's position: the same 39-save sweep shows the
// four unexamined WORLDMAP slots (+0x0C, +0x3C, +0x48, +0x54) are empty in
// EVERY save of the playthrough, so Galbadia Garden has no position record
// there. When it reappears near Edea's House it will need a coordinate of its
// own and a flag of its own -- see WORLDMAP_CATALOG_OPEN_ITEMS.
static bool WmGardenIsMobile()
{
    int32_t bx = 0, by = 0;
    if (!WmSafeReadBytes(WM_BGU_POS_ADDR + WMS_VEHPOS_X_OFF, &bx, 4)) return false;
    if (!WmSafeReadBytes(WM_BGU_POS_ADDR + WMS_VEHPOS_Y_OFF, &by, 4)) return false;
    return (bx != 0 || by != 0) &&
           bx > -131072 && bx < 131072 && by > -98304 && by < 98304;
}

// v0.20.100: MOBILE GALBADIA GARDEN HAS A WINDOW, NOT AN APPEARANCE.
//
//   before it appears near Edea's House  ->  not in the catalog
//   present near Edea's House            ->  the destination
//   after the battle of the Gardens      ->  GONE FOREVER
//
// Aaron gave the closing edge outright: "by the time the battle of the Gardens
// is over, you are on disc 3. You can use that as a signal to know when
// Galbadia Garden is 100% gone from the world map." The savemap header carries
// the disc at +0x44 as a 0-indexed uint32 -- disc 1 reads 0, and the 41-save
// sweep shows the disc-2 transition at slot2_save11.
//
// The OPENING edge is a savemap byte, narrowed but NOT YET CONFIRMED. The
// filter: bytes identical in all 40 saves before slot2_save30 and changed in
// slot2_save30 -- the one save where Galbadia Garden is present, every earlier
// one being either "still at its original site" or "vanished". That took an
// 84-byte raw diff down to three fields:
//
//     savemap+0x0B94..0x0B95   0000 -> 01b7      an append-only list of uint16s
//     savemap+0x0E8D           00   -> 02        ISOLATED byte, static neighbours
//     savemap+0x0FDC           00   -> 03        inside a region that churns
//
// +0x0E8D is the one that behaves like a state enum: a single byte that is zero
// for 100,000 seconds of play and steps to 2 exactly when the Garden appears,
// with its neighbourhood unchanged throughout. The other two sit in regions that
// are already moving for other reasons.
//
// SO THIS SHIPS AS A CANDIDATE, AND THE DISC RULE MAKES THE FAILURE MODE SAFE:
// if +0x0E8D is the wrong byte the destination can only appear at the wrong time
// WITHIN disc 2, never after. All three candidates are logged every catalog
// build so the next BAT either confirms it or names the right one.
static const uint32_t WMS_DISC_OFF     = 0x0044;   // uint32, 0-indexed
static const uint32_t WMS_GGSTATE_OFF  = 0x0E8D;   // candidate: Galbadia Garden world-map state
static const uint32_t WMS_GGALT1_OFF   = 0x0B94;   // logged for the record
static const uint32_t WMS_GGALT2_OFF   = 0x0FDC;   // logged for the record

static int WmCurrentDisc()
{
    uint32_t d = 0;
    if (!WmSafeReadBytes(WM_SAVEMAP_BASE + WMS_DISC_OFF, &d, 4)) return 0;
    return (d <= 3u) ? (int)d + 1 : 0;             // 0 = unreadable/implausible
}

static bool WmGalbadiaGardenPresent()
{
    const int disc = WmCurrentDisc();
    if (disc >= 3) return false;                   // Aaron: gone forever by disc 3
    uint8_t st = 0;
    if (!WmSafeReadBytes(WM_SAVEMAP_BASE + WMS_GGSTATE_OFF, &st, 1)) return false;
    return st != 0;
}

// v0.20.101: THE WHITE SEED SHIP HAS A WINDOW TOO, AND BOTH ITS EDGES ARE
// ALREADY-VALIDATED SIGNALS.
//
// Aaron: "the White SeeD Ship -- it only becomes available on Disc 3 and
// disappears from the World Map when the player receives the Ragnarok."
//
//   opening edge: disc >= 3               -- the same header field, +0x44
//   closing edge: ragnarok_pos goes live  -- WORLDMAP struct +0x18
//
// The closing edge needs no new discovery. `ragnarok_pos` is the exact analogue
// of `bgu_pos`, whose "zero while parked, live from the moment you have it"
// behaviour is proven across all 41 of Aaron's saves. Ragnarok is +0x18 and is
// empty in every one of them, which is correct -- it is a disc-3 vehicle he does
// not have yet. When he receives it, that slot fills, exactly as the Garden's
// did at slot2_save28.
//
// So unlike Galbadia Garden, NEITHER EDGE HERE IS A GUESS.
// v0.81.0: ...AND THE OTHER HALF, WHICH IS THE ONE HE ACTUALLY HITS.
// The slot above is the ship's PARKED position and reads zero while he is
// inside it -- see wm_story_pure.inl for the two log lines that prove it. Being
// aboard is the least ambiguous proof of ownership there is, so it is asked
// first and the slot is only consulted when he is not flying.
static bool WmHaveRagnarok()
{
    int32_t rx = 0, ry = 0;
    bool slotLive = false;
    if (WmSafeReadBytes(WM_RAGNAROK_POS_ADDR + WMS_VEHPOS_X_OFF, &rx, 4) &&
        WmSafeReadBytes(WM_RAGNAROK_POS_ADDR + WMS_VEHPOS_Y_OFF, &ry, 4)) {
        slotLive = (rx != 0 || ry != 0) &&
                   rx > -131072 && rx < 131072 && ry > -98304 && ry < 98304;
    }
    return WmRagnarokHeldPure(RagIsFlying(), slotLive);
}

static bool WmWhiteSeedShipPresent()
{
    return WmWhiteSeedPresentPure(WmCurrentDisc(), WmHaveRagnarok());
}

// One predicate, asked in every place the catalog is assembled. The v0.20.88
// lesson: a condition armed in one place and asked about in three silently
// loses a destination -- so this is the only test, and every builder calls it.
static bool WmStoryRetired(const char* name)
{
    if (!name) return false;
    if (strcmp(name, "Mobile Galbadia Garden") == 0) return !WmGalbadiaGardenPresent();
    if (strcmp(name, "White SeeD Ship") == 0)        return !WmWhiteSeedShipPresent();
    if (strcmp(name, "Balamb Garden") != 0 &&
        strcmp(name, "Galbadia Garden") != 0) return false;
    return WmGardenIsMobile();
}

// ============================================================================
// Catalog management
// ============================================================================
// v0.82.0: the vehicle rule class the LIVE catalog was built for, derived from
// the engine vehicle id alone. -1 = nothing built yet. See
// wm_catalog_refresh_pure.inl for why this is id-only rather than the builder's
// byte-then-id resolution.
static int s_catalogVehIdClass = -1;
static int s_pendingVehIdClass = -1;
static int s_pendingVehIdCount = 0;
static const int VEHID_DEBOUNCE_POLLS = 4;


// v0.82.0: the live-vehicle entries (the mobile Garden, and now the parked
// Ragnarok) are appended AFTER the reachability filter has run, in distance
// order, so the list the player pages through stays sorted. Both used to inline
// the same insertion; a second copy of a shift-right loop is how off-by-ones get
// in, so there is one.
static int WmInsertCatalogByDistance(const char* name, int32_t x, int32_t y,
                                     int32_t px, int32_t py)
{
    if (s_catalogCount >= MAX_LOCATIONS) return -1;
    const double d = CalculateWrappedDistance(px, py, x, y);
    int at = s_catalogCount;
    for (int i = 0; i < s_catalogCount; i++) {
        if (CalculateWrappedDistance(px, py, s_catalog[i].x, s_catalog[i].y) > d) {
            at = i; break;
        }
    }
    for (int i = s_catalogCount; i > at; i--) s_catalog[i] = s_catalog[i - 1];
    LocationEntry e;
    e.name = name; e.x = x; e.y = y;
    s_catalog[at] = e;
    s_catalogCount++;
    return at;
}

// The vehicle rule class implied by the ENGINE VEHICLE ID alone. Deliberately
// not the builder's byte-then-id resolution -- see wm_catalog_refresh_pure.inl.
static int WmVehIdClass(int vid)
{
    if (vid <= 0) return GetBfsRuleClass(VEH_ON_FOOT);
    return GetBfsRuleClass(GetVehicleType((uint8_t)vid));
}

static void BuildDistanceCatalog()
{
    int32_t px, py, pz;
    GetWorldMapPosition(&px, &py, &pz);

    static bool s_deferLogged = false;
    if (px == 0 && py == 0) {
        if (!s_deferLogged) {
            Log::World("WorldMap: [DEFER] Position is (0,0); retrying each tick until valid (one log per defer cycle)");
            s_deferLogged = true;
        }
        return;
    }
    if (s_deferLogged) {
        Log::World("WorldMap: [DEFER] Position became valid (%d,%d); proceeding with catalog build", px, py);
        s_deferLogged = false;
    }

    // Copy all locations and compute distances
    for (int i = 0; i < LOCATION_COUNT; i++) {
        s_catalog[i] = s_locations[i];
        int32_t dist = (int32_t)CalculateWrappedDistance(px, py, s_locations[i].x, s_locations[i].y);
        s_catalog[i].x = dist;  // temporarily store distance here
    }

    std::sort(s_catalog, s_catalog + LOCATION_COUNT, [](const LocationEntry& a, const LocationEntry& b) {
        return a.x < b.x;
    });

    // Restore original coordinates
    for (int i = 0; i < LOCATION_COUNT; i++) {
        for (int j = 0; j < LOCATION_COUNT; j++) {
            if (strcmp(s_catalog[i].name, s_locations[j].name) == 0) {
                s_catalog[i].x = s_locations[j].x;
                s_catalog[i].y = s_locations[j].y;
                break;
            }
        }
    }

    // v0.20.99: drop the destinations the story has retired BEFORE any filter
    // runs, so every branch below counts the same list. nLive replaces
    // LOCATION_COUNT from here down.
    int nLive = 0;
    {
        int retired = 0;
        for (int i = 0; i < LOCATION_COUNT; i++) {
            if (WmStoryRetired(s_catalog[i].name)) { retired++; continue; }
            if (nLive != i) s_catalog[nLive] = s_catalog[i];
            nLive++;
        }
        if (retired) {
            Log::World("WorldMap: [STORY] %d destination(s) retired -- the Garden is mobile, "
                       "so the fixed Garden sites are empty ground", retired);
        }
        // v0.20.100: the raw evidence beside the verdict, every build. The
        // opening edge of the Galbadia Garden window is a CANDIDATE byte; this
        // line is what confirms it or names the right one, and it costs nothing.
        {
            uint8_t st = 0, alt2 = 0; uint16_t alt1 = 0;
            WmSafeReadBytes(WM_SAVEMAP_BASE + WMS_GGSTATE_OFF, &st,   1);
            WmSafeReadBytes(WM_SAVEMAP_BASE + WMS_GGALT1_OFF,  &alt1, 2);
            WmSafeReadBytes(WM_SAVEMAP_BASE + WMS_GGALT2_OFF,  &alt2, 1);
            Log::World("WorldMap: [STORY] disc=%d  gg[0x0E8D]=%u  alt[0x0B94]=%u  alt[0x0FDC]=%u"
                       "  -> Mobile Galbadia Garden %s | Ragnarok %s -> White SeeD Ship %s",
                       WmCurrentDisc(), (unsigned)st, (unsigned)alt1, (unsigned)alt2,
                       WmGalbadiaGardenPresent() ? "PRESENT" : "absent",
                       WmHaveRagnarok() ? "held" : "not held",
                       WmWhiteSeedShipPresent() ? "PRESENT" : "absent");
        }
    }

    if (!s_walkGridLoaded) {
        s_catalogCount = nLive;
        Log::World("WorldMap: [BFS] Fine walk grid not loaded — catalog unfiltered (%d entries)",
                   s_catalogCount);
    } else {
        VehicleType veh = GetVehicleType(GetLocomotionMode());
        // v0.18.3.258 Part D (#79): the engine vehicle id (0x020409E0) upgrades
        // the BFS class when it names a known vehicle -- so Garden/Ragnarok
        // boardings get the right reachability filter AT catalog time, and the
        // exam car is labeled correctly (car shares the foot land-only class,
        // so its filtering is unchanged). VEHICLE-POSITIVE ONLY: foot (0/6),
        // unknown, or unreadable ids leave the legacy byte-derived class as-is.
        // The raw id is logged every build for the #79 confirmation record.
        {
            // v0.87.0: through the shared predicate now. The rule is unchanged;
            // what changed is that the PLANNER asks the same function, after the
            // 13:41 BAT found it resolving the vehicle from the locomotion byte
            // alone and planning a 302-cell walking route for an airship.
            int vid = GetActiveVehicleId();
            veh = (VehicleType)WmResolveVehicle(
                      (int)veh, vid,
                      (int)GetVehicleType((uint8_t)(vid > 0 ? vid : 0)),
                      (int)VEH_ON_FOOT);
            Log::World("WorldMap: [BFS] engine vehicleId=%d -> catalog vehicle type %d (class %d)",
                       vid, (int)veh, GetBfsRuleClass(veh));
        }
        int         pfc = WorldXToFineCol(px);
        int         pfr = WorldYToFineRow(py);
        Log::World("WorldMap: [BFS] Player at (%d,%d) -> fine(col=%d,row=%d), vehicle type %d",
                   px, py, pfc, pfr, (int)veh);

        if (veh == VEH_RAGNAROK) {
            s_catalogCount = nLive;
            Log::World("WorldMap: [BFS] Ragnarok mode — catalog unfiltered (%d entries)",
                       s_catalogCount);
        } else {
            // #67: continuous flood-fill over the fine rasterized walk grid
            // replaces the 32x24 segment BFS. A location is kept if its fine
            // cell (or an immediate neighbour, for coastal slop) is reachable.
            ComputeReachabilityFine(pfc, pfr, veh);

            int kept = 0;
            for (int i = 0; i < nLive; i++) {
                if (IsFineCellReachable(s_catalog[i].x, s_catalog[i].y)) {
                    if (kept != i) s_catalog[kept] = s_catalog[i];
                    kept++;
                }
            }
            s_catalogCount = kept;
            Log::World("WorldMap: [BFS] Filtered to %d reachable locations (vehicle type %d)",
                       s_catalogCount, (int)veh);
        }
    }

    // ========================================================================
    // #80: mobile Balamb Garden
    // ========================================================================
    // Aboard the Garden the reachability filter above is meaningless -- it ran
    // a land-only rule against a hull whose whole point is crossing oceans --
    // so it is replaced wholesale. Every destination stays in the catalog; the
    // ones the Garden can actually set down beside move to the front in
    // distance order and the rest follow, still selectable and announced with
    // the reason. Hiding them would leave the player wondering whether the mod
    // had simply lost Esthar.
    if (Garden_IsAboard()) {
        Garden_ComputeReach(px, py);
        // Rebuild from s_locations rather than reusing whatever the foot filter
        // above left behind: that filter COMPACTS s_catalog in place, so the
        // slots past its kept-count hold stale duplicates. Today it happens to
        // keep everything for the Garden (IsFineTraversable waves the Garden
        // through), but relying on that would make this silently emit duplicate
        // destinations the day that rule changes.
        static int   order[MAX_LOCATIONS];
        static double odist[MAX_LOCATIONS];
        // v0.20.99: the retired sites are skipped HERE too. This branch rebuilds
        // from s_locations rather than from the compacted s_catalog, so it has
        // to ask the same question again -- exactly the shape of the .88 bug,
        // which is why WmStoryRetired is the single predicate both sides use.
        int nOrder = 0;
        for (int i = 0; i < LOCATION_COUNT; i++) {
            if (WmStoryRetired(s_locations[i].name)) continue;
            order[nOrder++] = i;
            odist[i] = CalculateWrappedDistance(px, py, s_locations[i].x, s_locations[i].y);
        }
        std::sort(order, order + nOrder,
                  [](int a, int b) { return odist[a] < odist[b]; });
        // v0.20.74: SHOW ONLY WHAT THIS VEHICLE CAN ACTUALLY REACH.
        //
        // Aaron: "Only show destinations that are reachable using the current
        // vehicle ... Exclude locations that are out of range for the current
        // vehicle." Up to .73 the Garden catalog kept every destination and
        // merely sorted the unreachable ones to the back, on the theory that
        // hiding Esthar would look like a bug. In practice it makes the list
        // long and slow to page through with a screen reader, and every entry
        // past the reachable ones is a dead end. The foot catalog has always
        // filtered; the Garden catalog now does too.
        static LocationEntry tmp[MAX_LOCATIONS];
        int n = 0, hidden = 0;
        for (int k = 0; k < nOrder; k++) {
            const LocationEntry& le = s_locations[order[k]];
            const GardenPark* gp = Garden_ParkFor(le.name);
            const bool ok = Garden_BerthReachable(gp);   // v0.20.89
            if (ok) tmp[n++] = le; else hidden++;
        }
        memcpy(s_catalog, tmp, sizeof(LocationEntry) * (size_t)n);
        s_catalogCount = n;
        Log::World("WorldMap: [GARDEN] catalog: %d destinations the Garden can reach, "
                   "%d hidden as out of range for this vehicle", n, hidden);
    } else {
        // Not aboard: if the Garden is mobile and parked somewhere, put it in
        // the catalog so the player can find their ride again.
        // WmSafeReadBytes rather than a local __try: BuildDistanceCatalog
        // contains std::sort with a lambda, and MSVC rejects __try in a
        // function that requires object unwinding (C2712).
        int32_t bx = 0, by = 0;
        if (!WmSafeReadBytes(WM_BGU_POS_ADDR + WMS_VEHPOS_X_OFF, &bx, 4)) bx = 0;
        if (!WmSafeReadBytes(WM_BGU_POS_ADDR + WMS_VEHPOS_Y_OFF, &by, 4)) by = 0;
        const bool posOk = (bx != 0 || by != 0) &&
                           bx > -131072 && bx < 131072 && by > -98304 && by < 98304;
        // v0.81.0: not while flying. Aaron: "Mobile Balamb Garden should not be
        // an option in the catalog when flying Ragnarok. To get to Garden you
        // land at FH where Garden is parked." The rule and its evidence live in
        // wm_story_pure.inl; the short version is that the Garden's live
        // coordinate is 1,295 units from the FH landing pad, so FH is not a
        // detour on the way to the Garden -- it IS the way to it, and unlike the
        // Garden it is somewhere the airship can actually set down.
        const bool flyingRag = RagIsFlying();
        const bool plausible = WmOfferMobileGardenPure(posOk, flyingRag);
        if (!plausible && posOk && flyingRag) {
            Log::World("WorldMap: [GARDEN] 'Mobile Balamb Garden' (%d,%d) withheld -- "
                       "flying the Ragnarok; land at Fisherman's Horizon, where the "
                       "Garden is parked", bx, by);
        }
        if (plausible) {
            const int at = WmInsertCatalogByDistance("Mobile Balamb Garden", bx, by, px, py);
            if (at >= 0)
                Log::World("WorldMap: [GARDEN] added 'Mobile Balamb Garden' at (%d,%d), %.0f units away, catalog slot %d",
                           bx, by, CalculateWrappedDistance(px, py, bx, by), at);
        } else if (!posOk && (bx != 0 || by != 0)) {
            Log::World("WorldMap: [GARDEN] bgu_pos (%d,%d) rejected as implausible", bx, by);
        }

        // v0.82.0: AND THE SHIP HE FLEW IN ON. Aaron: "There is no entry for the
        // Ragnarok itself in the catalog once landed. Just like Mobile B-Garden,
        // there needs to be an entry for the Ragnarok so the player can navigate
        // to it and board it on the world map."
        //
        // Same slot the ownership test reads, and the property that made THAT
        // test wrong is what makes this one easy: ragnarok_pos holds the PARKED
        // position and is zero while he is inside it. A live coordinate means
        // "yours, and not under you" -- exactly when a destination is useful.
        //
        // Appended after the reachability filter for the same reason the Garden
        // is: it is his ride, not a place, and a walker who cannot reach it
        // still needs to be told where it is.
        int32_t rgx = 0, rgy = 0;
        if (!WmSafeReadBytes(WM_RAGNAROK_POS_ADDR + WMS_VEHPOS_X_OFF, &rgx, 4)) rgx = 0;
        if (!WmSafeReadBytes(WM_RAGNAROK_POS_ADDR + WMS_VEHPOS_Y_OFF, &rgy, 4)) rgy = 0;
        const bool ragPosOk = (rgx != 0 || rgy != 0) &&
                              rgx > -131072 && rgx < 131072 && rgy > -98304 && rgy < 98304;
        if (WmOfferParkedRagnarokPure(ragPosOk, flyingRag)) {
            const int at = WmInsertCatalogByDistance("Ragnarok", rgx, rgy, px, py);
            if (at >= 0)
                Log::World("WorldMap: [RAG] added 'Ragnarok' at (%d,%d), %.0f units away, catalog slot %d",
                           rgx, rgy, CalculateWrappedDistance(px, py, rgx, rgy), at);
        }
    }

    if (s_catalogCount == 0) {
        Log::World("WorldMap: [BFS] WARNING — no reachable locations from current position");
    }

    // v0.82.0: what this catalog was built FOR, so the watcher can tell when it
    // has stopped being true. Recorded on every build, including the early
    // returns' successors, so the two can never drift.
    s_catalogVehIdClass = WmVehIdClass(GetActiveVehicleId());
    s_pendingVehIdClass = -1;
    s_pendingVehIdCount = 0;

    s_catalogBuilt = true;
    s_catalogIndex = 0;
    if (s_catalogCount > 0) {
        Log::World("WorldMap: Catalog built (%d entries), nearest: %s",
                   s_catalogCount, s_catalog[0].name);
    }
}

// ============================================================================
// Vehicle state tracking (v0.14.85.3)
// ============================================================================
static const char* GetVehicleName(uint8_t mode)
{
    if (mode == 0 || mode == 6) return "On foot";
    if (mode == 3)               return "Ship";
    if (mode == 31)              return "Chocobo";
    if (mode >= 32 && mode <= 40) return "Car";
    if (mode == 48)              return "Garden";
    if (mode == 50)              return "Ragnarok";
    return "Unknown vehicle";
}

// v0.14.90: debounce window. See world_map_history.h for full v0.14.90 / .90.2
// / .90.3 rationale: even canonical locomotion values can read transiently;
// the world-map re-entry animation can cycle through several canonical values
// over ~3 seconds and would otherwise fire spurious announcements.
static const int DEBOUNCE_POLLS = 4;
static int     s_pendingVehicle      = -1;
static int     s_pendingVehicleCount = 0;

static const DWORD WM_ENTRY_DEBOUNCE_MS = 3000;
static DWORD s_wmEntryTick = 0;

// v0.56.0 (#118): IsFootLocomotion moved to world_map_geometry.inl, beside
// IsCanonicalLocomotion. world_map_locomotion.inl needs it and is included
// BEFORE this file, so defining it here was an MSVC-only build break --
// caught by tests/catalog_story_test.cpp compiling the real include order.

// v0.93.0: world_map_drive_helpers.inl is included five files later, so the
// definition is not in scope here. A forward declaration rather than a reorder:
// the include order in world_map.cpp is load-bearing and documented line by line
// (locomotion before catalog, catalog before planner), and moving a file to
// borrow one function is how that gets quietly broken.
static void StopAutoDrive(const char* reason);

// v0.82.0: THE SIGNAL THAT ACTUALLY MOVED WHEN HE BOARDED.
//
// CheckVehicleChange below watches the locomotion byte and, in the 09:22 BAT,
// never saw the Ragnarok at all -- the one verdict logged reads "the locomotion
// byte said -1", and there is no [VEH-REJECT] line, so it never even reached the
// corroboration gate. The engine vehicle id read 50 cleanly throughout. The
// result was a whole flight spent cycling the ELEVEN-entry on-foot catalog.
//
// This watcher's only power is to invalidate the cache. It never assigns
// s_lastVehicle and never repoints the position source -- the two things that
// made v0.56.0's Esthar failure expensive -- so a wrong answer costs one
// redundant rebuild and nothing else. That is why the id may be trusted here on
// terms that would be reckless twenty lines down.
static void CheckVehicleIdChange()
{
    if (Garden_Active()) return;

    // The world-map re-entry animation is exactly when a fresh catalog is being
    // built anyway; leave it alone until the window closes.
    if (s_wmEntryTick != 0) {
        DWORD elapsed = GetTickCount() - s_wmEntryTick;
        if (elapsed < WM_ENTRY_DEBOUNCE_MS) {
            s_pendingVehIdClass = -1;
            s_pendingVehIdCount = 0;
            return;
        }
    }

    const int live = WmVehIdClass(GetActiveVehicleId());
    if (!WmCatalogStale(s_onWorldMap, s_catalogBuilt, s_catalogVehIdClass, live)) {
        s_pendingVehIdClass = -1;
        s_pendingVehIdCount = 0;
        return;
    }
    if (!WmStaleDebounce(live, &s_pendingVehIdClass, &s_pendingVehIdCount,
                         VEHID_DEBOUNCE_POLLS)) return;

    Log::World("WorldMap: [VEHID] engine vehicle id now implies rule class %d, catalog was "
               "built for class %d -- rebuilding (embark/disembark)",
               live, s_catalogVehIdClass);

    // v0.93.0: AND IF HE BOARDED OR STEPPED OFF MID-DRIVE, THE DRIVE IS OVER.
    //
    // Aaron: "If I am on the ground and auto-driving to Ragnarok, then press X
    // within range, it needs to detect that I have boarded the ship, turn off
    // auto-drive, update the catalog to reflect reachable locations by the ship,
    // and stand by for the player to cycle the catalog to select a destination."
    //
    // Every latch StartAutoDrive took is now about the wrong vehicle -- the
    // flying flag, the landing row, the arrival radius, which key set the
    // executor presses -- so there is nothing worth carrying over. It stops, says
    // what he is in, and hands the choice back.
    if (WmDriveInvalidByVehicle(s_driveActive, true)) {
        char msg[160];
        snprintf(msg, sizeof msg,
                 "%s Auto-drive stopped. Cycle the catalog to choose where to go.",
                 live == 2 ? "Aboard the Ragnarok."
               : live == 1 ? "Aboard the Garden."
                           : "On foot.");
        Log::World("WorldMap: [VEHID] the vehicle changed mid-drive -- stopping the drive; "
                   "every latch it took (flying, landing row, arrival radius, key set) is "
                   "about the vehicle he was in when it started");
        StopAutoDrive(msg);
    }

    s_catalogBuilt = false;
}

static void CheckVehicleChange()
{
    // #80: arrow-key injection makes the animation byte cycle, exactly as it
    // does for the foot/car drive (which is already exempt via s_driveActive).
    if (Garden_Active()) return;

    // v0.14.94: while a drive is active, ignore the locomotion byte entirely.
    // AD's keybd_event arrow-key injection causes the byte to cycle through
    // canonical vehicle values; debounce passes them; downstream arrival
    // logic gets poisoned. Skip while driving.
    if (s_driveActive) {
        s_pendingVehicle      = -1;
        s_pendingVehicleCount = 0;
        return;
    }

    uint8_t vehicle = GetLocomotionMode();

    // v0.14.90.3: suppress vehicle-change processing during world-map re-entry
    // animation window.
    if (s_wmEntryTick != 0) {
        DWORD now = GetTickCount();
        DWORD elapsed = now - s_wmEntryTick;
        if (elapsed < WM_ENTRY_DEBOUNCE_MS) {
            s_pendingVehicle      = -1;
            s_pendingVehicleCount = 0;
            return;
        }
        // Window expired this tick. Snapshot baseline.
        //
        // v0.56.0 (#118): THIS PATH USED TO SKIP THE CORROBORATION GATE, and
        // that is the whole of Aaron's 2026-08-21 Esthar failure. It assigned
        // `s_lastVehicle = vehicle` outright, so one noisy read of 3 (Ship) at
        // the instant the window expired was believed for the rest of the
        // session -- repointing the drive's position source at the Balamb
        // Garden mirror 70 km away AND flipping the steering law to the vehicle
        // executor, which walks the player in circles on foot.
        //
        // The gate twenty lines below already existed and was already right.
        // It was simply not on this path. A snapshot is a commit like any
        // other, so it goes through the same door.
        if (IsCanonicalLocomotion(vehicle)) {
            int32_t ppx = 0, ppy = 0, ppz = 0;
            GetWorldMapPosition(&ppx, &ppy, &ppz);
            int corrId = -1; double corrDist = -1.0;
            if (LocoCorroborated(vehicle, ppx, ppy, &corrId, &corrDist)) {
                int prev = s_lastVehicle;
                s_lastVehicle = vehicle;
                Log::World("WorldMap: [WM-ENTRY-DEBOUNCE] Snapshot baseline locomotion=%u (was %d, suppressed %lums of byte noise)",
                           vehicle, prev, (unsigned long)elapsed);
            } else {
                Log::World("WorldMap: [WM-ENTRY-DEBOUNCE] Snapshot locomotion=%u (%s) NOT corroborated "
                           "(engine id=%d, |P-mirror|=%.0f) -- keeping s_lastVehicle=%d",
                           vehicle, GetVehicleName(vehicle), corrId, corrDist, s_lastVehicle);
            }
        } else {
            Log::World("WorldMap: [WM-ENTRY-DEBOUNCE] Window expired with non-canonical locomotion=%u; keeping s_lastVehicle=%d",
                       vehicle, s_lastVehicle);
        }
        s_wmEntryTick         = 0;
        s_pendingVehicle      = -1;
        s_pendingVehicleCount = 0;
        return;
    }

    if (!IsCanonicalLocomotion(vehicle)) {
        s_pendingVehicle      = -1;
        s_pendingVehicleCount = 0;
        return;
    }

    if ((int)vehicle == s_lastVehicle) {
        s_pendingVehicle      = -1;
        s_pendingVehicleCount = 0;
        return;
    }

    if ((int)vehicle == s_pendingVehicle) {
        s_pendingVehicleCount++;
    } else {
        s_pendingVehicle      = vehicle;
        s_pendingVehicleCount = 1;
    }

    if (s_pendingVehicleCount < DEBOUNCE_POLLS) {
        return;
    }

    s_pendingVehicle      = -1;
    s_pendingVehicleCount = 0;

    // v0.20.56 (#80): CORROBORATION GATE. The v0.20.55 BAT caught this byte
    // committing 3 (Ship), 33/36/39 (Car) and 48 (Garden) in the space of
    // three seconds while the player was simply WALKING away from a parked
    // Garden -- it announced "Car" three times and "Garden" once, and each
    // commit also repointed GetWorldMapPosition_Active at that vehicle's
    // savemap mirror (the car's is 50 km away at the Missile Base). DEVNOTES
    // has said since .255 that this byte is an ANIMATION-state byte and is
    // "never authoritative for vehicle detection" -- but the commit path still
    // trusted it outright. It now has to be seconded by one of the two signals
    // that ARE about vehicles: the engine's in-motion id, or that vehicle's own
    // savemap position being where the player actually is.
    if (!IsFootLocomotion(vehicle)) {
        int32_t ppx = 0, ppy = 0, ppz = 0;
        GetWorldMapPosition(&ppx, &ppy, &ppz);
        int corrId = -1; double corrDist = -1.0;
        if (!LocoCorroborated(vehicle, ppx, ppy, &corrId, &corrDist)) {
            Log::World("WorldMap: [VEH-REJECT] locomotion=%u (%s) not corroborated "
                       "(engine id=%d, |P-mirror|=%.0f) -- keeping s_lastVehicle=%d",
                       vehicle, GetVehicleName(vehicle), corrId, corrDist, s_lastVehicle);
            return;
        }
    }

    {
        if (s_lastVehicle != -1) {
            const char* newVehicle = GetVehicleName(vehicle);
            char buf[128];
            snprintf(buf, sizeof(buf), "%s.", newVehicle);
            ScreenReader::Speak(buf, true);
            Log::World("WorldMap: Vehicle change: %s (mode %u)", newVehicle, vehicle);

            VehicleType oldType = (s_lastVehicle >= 0)
                                  ? GetVehicleType((uint8_t)s_lastVehicle)
                                  : VEH_ON_FOOT;
            VehicleType newType = GetVehicleType(vehicle);
            int oldClass = GetBfsRuleClass(oldType);
            int newClass = GetBfsRuleClass(newType);
            if (oldClass != newClass && s_onWorldMap) {
                s_catalogBuilt = false;
                Log::World("WorldMap: [BFS] Vehicle rule class changed (%d -> %d), forcing catalog rebuild",
                           oldClass, newClass);
            }
        }
        s_lastVehicle = vehicle;
    }
}
