// world_map_announce.inl - Navigation announcements
//
// PART OF world_map.cpp -- TEXTUAL INCLUDE. Do NOT compile standalone.
//
// Reads s_catalog[], s_catalogIndex, and player position; emits TTS via
// ScreenReader::Speak. Called from PollKeys (keys.inl) when the user
// cycles or queries bearing.

// ============================================================================
// Navigation announcements
// ============================================================================
// ============================================================================
// v0.56.0 (#118): the coordinate the player is told about.
// ============================================================================
// When a destination's field-entry trigger is decoded, the DRIVE walks to the
// decoded aim, not to the catalog marker -- StartDrive retargets. Until Esthar
// every aim sat within 800 units of its marker, so announcing the marker and
// walking to the aim differed by less than the readout's own rounding.
//
// Esthar's five aims are 3.7 to 12.1 km from their markers. Announcing the
// marker would have the catalog say "Esthar City, 7 kilometres, east" and the
// drive then say "Driving to Esthar City. 19 kilometres." -- and for someone
// navigating entirely by those numbers, that is not a rounding difference, it
// is two different answers to the same question.
//
// So the bearing and the distance are measured to the DOOR when the door is
// known, and to the marker otherwise. The catalog and the drive now agree.
// Nothing else moves: the marker is still what the catalog is built from, what
// BFS reachability filters on, and what arrival capture compares against.
static void AnnounceTargetPoint(int index, int32_t* tx, int32_t* ty)
{
    *tx = s_catalog[index].x;
    *ty = s_catalog[index].y;
    const int ai = FindEntryAim(s_catalog[index].name);
    if (ai < 0) return;
    const EntryAimInfo& ea = s_entryAims[ai];
    const bool markerInArea = (*tx >= ea.x0 && *tx <= ea.x1 && *ty >= ea.y0 && *ty <= ea.y1);
    if (!markerInArea) { *tx = ea.aimX; *ty = ea.aimY; }
}

static void AnnounceLocation(int index)
{
    if (index < 0 || index >= s_catalogCount || !s_catalogBuilt) return;
    
    int32_t px, py, pz;
    GetWorldMapPosition(&px, &py, &pz);
    
    int32_t atx, aty;
    AnnounceTargetPoint(index, &atx, &aty);
    double distance = CalculateWrappedDistance(px, py, atx, aty);
    int distanceKm = (int)(distance / 1000.0);  // rough conversion to kilometers
    
    // #80: while piloting the Garden, say plainly which destinations it cannot
    // set down beside, and how far the walk is from where it can.
    const char* gsuffix = "";
    char gwalk[64] = {};
    if (Garden_IsAboard() && strcmp(s_catalog[index].name, "Mobile Balamb Garden") != 0) {
        const GardenPark* gp = Garden_ParkFor(s_catalog[index].name);
        if (!gp || !gp->reachable) {
            gsuffix = " The Garden cannot reach this.";
        } else if (!Garden_BerthReachable(gp)) {   // v0.20.89
            gsuffix = " Not reachable from here.";
        } else if (gp->walk_units >= 200) {
            char wsay[48];
            WmSayDistance((double)gp->walk_units, wsay, sizeof wsay);
            snprintf(gwalk, sizeof(gwalk), " Then %s on foot.", wsay);
            gsuffix = gwalk;
        }
    }

    char buf[256];
    if (distanceKm < 1) {
        snprintf(buf, sizeof(buf), "%s. Very close.%s", s_catalog[index].name, gsuffix);
    } else {
        snprintf(buf, sizeof(buf), "%s. %d kilometers away.%s",
                 s_catalog[index].name, distanceKm, gsuffix);
    }
    
    // v0.20.74: INTERRUPT. Aaron: "if the user presses a catalog key, a location
    // starts reading, and the user presses another catalog key then the readout
    // of the first catalog entry should be interrupted and the next one should
    // read. This way the user can quickly mash the catalog keys to cycle between
    // catalog entries." World map only -- the field catalog is left alone.
    ScreenReader::Speak(buf, true);
    Log::World("WorldMap: [LOCATION] %s", buf);
}

static void AnnounceBearing()
{
    if (!s_catalogBuilt || s_catalogIndex >= s_catalogCount) return;
    
    int32_t px, py, pz;
    GetWorldMapPosition(&px, &py, &pz);
    
    // Calculate bearing to selected location -- to its decoded door where one
    // is known, so this agrees with what the drive will say (see
    // AnnounceTargetPoint above).
    int32_t tx, ty;
    AnnounceTargetPoint(s_catalogIndex, &tx, &ty);
    
    // Handle world wrapping for shortest path
    int32_t dx = tx - px;
    int32_t dy = ty - py;
    
    if (abs(dx) > WM_WIDTH / 2) {
        if (dx > 0) dx -= (int32_t)WM_WIDTH;
        else dx += (int32_t)WM_WIDTH;
    }
    if (abs(dy) > WM_HEIGHT / 2) {
        if (dy > 0) dy -= (int32_t)WM_HEIGHT;
        else dy += (int32_t)WM_HEIGHT;
    }
    
    // Convert to bearing (0=North, clockwise)
    double radians = atan2(dx, -dy);  // -dy because FF8 Y increases downward
    double degrees = radians * 180.0 / 3.14159;
    if (degrees < 0) degrees += 360.0;
    
    const char* direction;
    if (degrees < 22.5 || degrees >= 337.5) direction = "North";
    else if (degrees < 67.5) direction = "Northeast";
    else if (degrees < 112.5) direction = "East";
    else if (degrees < 157.5) direction = "Southeast";
    else if (degrees < 202.5) direction = "South";
    else if (degrees < 247.5) direction = "Southwest";
    else if (degrees < 292.5) direction = "West";
    else direction = "Northwest";
    
    double distance = CalculateWrappedDistance(px, py, tx, ty);
    int distanceKm = (int)(distance / 1000.0);
    
    char buf[256];
    snprintf(buf, sizeof(buf), "%s. %s, %d kilometers.", 
             s_catalog[s_catalogIndex].name, direction, distanceKm);
    
    ScreenReader::Speak(buf, true);   // v0.20.74: interruptible, as above
    Log::World("WorldMap: [BEARING] %s", buf);
}
