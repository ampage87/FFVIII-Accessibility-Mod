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
static void AnnounceLocation(int index)
{
    if (index < 0 || index >= s_catalogCount || !s_catalogBuilt) return;
    
    int32_t px, py, pz;
    GetWorldMapPosition(&px, &py, &pz);
    
    double distance = CalculateWrappedDistance(px, py, s_catalog[index].x, s_catalog[index].y);
    int distanceKm = (int)(distance / 1000.0);  // rough conversion to kilometers
    
    char buf[256];
    if (distanceKm < 1) {
        snprintf(buf, sizeof(buf), "%s. Very close.", s_catalog[index].name);
    } else {
        snprintf(buf, sizeof(buf), "%s. %d kilometers away.", s_catalog[index].name, distanceKm);
    }
    
    ScreenReader::Speak(buf);
    Log::World("WorldMap: [LOCATION] %s", buf);
}

static void AnnounceBearing()
{
    if (!s_catalogBuilt || s_catalogIndex >= s_catalogCount) return;
    
    int32_t px, py, pz;
    GetWorldMapPosition(&px, &py, &pz);
    
    // Calculate bearing to selected location
    int32_t tx = s_catalog[s_catalogIndex].x;
    int32_t ty = s_catalog[s_catalogIndex].y;
    
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
    
    ScreenReader::Speak(buf);
    Log::World("WorldMap: [BEARING] %s", buf);
}
