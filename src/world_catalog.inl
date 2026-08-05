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
    {"Fisherman's Horizon",        48811,   -1653},
    {"Trabia Garden",              48893,  -57979},
    {"Edea's House",              -23150,   62853},
    {"White SeeD Ship",             4887,   51285},
    {"Great Salt Lake",            49888,   -2683},
    {"Esthar City",                57011,   -2295},
    {"Lunatic Pandora Lab",        79521,   -9135},
    {"Lunar Gate",                 88021,    7865},
    {"Sorceress Memorial",         81521,   11865},
    {"Shumi Village",              10362,  -76967},
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
// Catalog management
// ============================================================================
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

    if (!s_walkGridLoaded) {
        s_catalogCount = LOCATION_COUNT;
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
            int vid = GetActiveVehicleId();
            if (vid > 0 && vid != 6) {
                VehicleType vt = GetVehicleType((uint8_t)vid);
                if (vt != VEH_ON_FOOT) veh = vt;
            }
            Log::World("WorldMap: [BFS] engine vehicleId=%d -> catalog vehicle type %d (class %d)",
                       vid, (int)veh, GetBfsRuleClass(veh));
        }
        int         pfc = WorldXToFineCol(px);
        int         pfr = WorldYToFineRow(py);
        Log::World("WorldMap: [BFS] Player at (%d,%d) -> fine(col=%d,row=%d), vehicle type %d",
                   px, py, pfc, pfr, (int)veh);

        if (veh == VEH_RAGNAROK) {
            s_catalogCount = LOCATION_COUNT;
            Log::World("WorldMap: [BFS] Ragnarok mode — catalog unfiltered (%d entries)",
                       s_catalogCount);
        } else {
            // #67: continuous flood-fill over the fine rasterized walk grid
            // replaces the 32x24 segment BFS. A location is kept if its fine
            // cell (or an immediate neighbour, for coastal slop) is reachable.
            ComputeReachabilityFine(pfc, pfr, veh);

            int kept = 0;
            for (int i = 0; i < LOCATION_COUNT; i++) {
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

    if (s_catalogCount == 0) {
        Log::World("WorldMap: [BFS] WARNING — no reachable locations from current position");
    }

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

static void CheckVehicleChange()
{
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
        // Window expired this tick. Snapshot baseline silently.
        if (IsCanonicalLocomotion(vehicle)) {
            int prev = s_lastVehicle;
            s_lastVehicle = vehicle;
            Log::World("WorldMap: [WM-ENTRY-DEBOUNCE] Snapshot baseline locomotion=%u (was %d, suppressed %lums of byte noise)",
                       vehicle, prev, (unsigned long)elapsed);
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
