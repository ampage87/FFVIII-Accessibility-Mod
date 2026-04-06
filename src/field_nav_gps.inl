// field_nav_gps.inl — GPS guided navigation
// Included from field_navigation.cpp. Do not compile independently.
// Part of the FieldNavigation namespace.
//
// v0.12.18: Extracted from field_navigation.cpp for readability.

// ============================================================================
// GPS Guidance (v0.12.02)
// ============================================================================

// Get the target position for a catalog entry.
// Returns true if a valid position was found.
static bool GetCatalogTargetPos(int catalogIdx, float& outX, float& outY)
{
    if (catalogIdx < 0 || catalogIdx >= s_catalogCount) return false;
    const EntityInfo& tgt = s_catalog[catalogIdx];

    if (tgt.entityIdx <= -400) {
        // INF gateway exit
        int gwIdx = -(tgt.entityIdx + 400);
        if (gwIdx < 0 || gwIdx >= s_dedupGatewayCount) return false;
        outX = s_dedupGateways[gwIdx].centerX;
        outY = s_dedupGateways[gwIdx].centerY;
        return true;
    } else if (tgt.entityIdx <= -300) {
        // JSM-injected entity
        int jsmIdx = -(tgt.entityIdx + 300);
        if (jsmIdx < 0 || jsmIdx >= s_jsmEntityCount || !s_jsmEntities[jsmIdx].hasPosition) return false;
        outX = (float)s_jsmEntities[jsmIdx].posX;
        outY = (float)s_jsmEntities[jsmIdx].posY;
        return true;
    } else if (tgt.entityIdx <= -200) {
        // Trigger line
        int trigIdx = -(tgt.entityIdx + 200);
        if (trigIdx < 0 || trigIdx >= s_capturedLineCount) return false;
        outX = (float)(s_capturedLines[trigIdx].x1 + s_capturedLines[trigIdx].x2) / 2.0f;
        outY = (float)(s_capturedLines[trigIdx].y1 + s_capturedLines[trigIdx].y2) / 2.0f;
        return true;
    } else if (tgt.entityIdx >= 0 && tgt.entityIdx != s_playerEntityIdx) {
        return GetEntityPos(tgt.entityIdx, outX, outY);
    }
    return false;
}

// Compute 8-way screen direction name from entity coordinate delta.
// Entity +X = screen right, +Y = screen up (corrected v0.12.03).
// Returns index into GPS_DIR_NAMES (0=up, 1=up-right, ... 7=up-left).
static int ComputeScreenDirIndex(float dx, float dy)
{
    // atan2(dx, dy): angle from screen-up (0), clockwise.
    // dx>0 = right, dy>0 = up.
    double angle = atan2((double)dx, (double)dy);
    if (angle < 0) angle += 2.0 * NAV_PI;
    // Convert to 0-7 octant index (each octant = 45 degrees = pi/4)
    int idx = (int)((angle + NAV_PI / 8.0) / (NAV_PI / 4.0)) % 8;
    return idx;
}

static void StopGPS(const char* reason)
{
    if (!s_gpsActive) return;
    s_gpsActive = false;
    s_gpsCatalogIdx = -1;
    if (reason) {
        ScreenReader::Speak(reason, true);
    }
    Log::Field("FieldNavigation: [GPS] Stopped: %s", reason ? reason : "(silent)");
}

static void StartGPS(int catalogIdx)
{
    if (catalogIdx < 0 || catalogIdx >= s_catalogCount) return;
    const EntityInfo& tgt = s_catalog[catalogIdx];
    if (tgt.entityIdx == s_playerEntityIdx) return;

    float px = 0, py = 0, tx = 0, ty = 0;
    if (!GetEntityPos(s_playerEntityIdx, px, py)) return;
    if (!GetCatalogTargetPos(catalogIdx, tx, ty)) {
        ScreenReader::Speak("Target not located.", true);
        return;
    }

    float dx = tx - px;
    float dy = ty - py;
    float dist = sqrtf(dx * dx + dy * dy);
    int dirIdx = ComputeScreenDirIndex(dx, dy);

    s_gpsActive = true;
    s_gpsCatalogIdx = catalogIdx;
    s_gpsLastDist = dist;
    s_gpsLastAnnounceTime = GetTickCount();
    s_gpsArrivedAnnounced = false;
    s_gpsNearbyAnnounced = false;

    // v0.12.07: Compute per-target arrival distance.
    // Use the entity's actual talk radius so "In range" fires when the player
    // can genuinely interact. Ceiling step rounding (below) provides the safety
    // margin — steps never undercount, so the player always arrives inside
    // the zone before the step count reaches zero. Min 60 to avoid overshoot.
    s_gpsArriveDist = GPS_ARRIVE_DIST;  // default fallback for non-entity targets
    if (tgt.entityIdx >= 0 && tgt.entityIdx < MAX_ENTITIES) {
        // Runtime entity — read talk radius from entity struct.
        uint16_t talkRad = GetEntityTalkRadius(tgt.entityIdx);
        if (talkRad > 0) {
            s_gpsArriveDist = (float)talkRad;
            if (s_gpsArriveDist < 60.0f) s_gpsArriveDist = 60.0f;
        } else {
            s_gpsArriveDist = GPS_ARRIVE_DIST;  // NPC without talk radius yet
        }
    } else if (tgt.type == ENT_SAVE_POINT || tgt.type == ENT_DRAW_POINT) {
        s_gpsArriveDist = 200.0f;  // walk-on interaction zone (generous)
    } else if (tgt.type == ENT_OBJECT || tgt.type == ENT_BG_OBJECT) {
        s_gpsArriveDist = 200.0f;  // default for model-less objects
    }
    // GPS_NEARBY_DIST is relative to arrive distance — use 2x arrive.
    s_gpsNearbyDist = s_gpsArriveDist * 2.0f;
    if (s_gpsNearbyDist < GPS_NEARBY_DIST) s_gpsNearbyDist = GPS_NEARBY_DIST;

    // Initialize movement tracking for step calibration diagnostic.
    s_gpsDiagLastX = px;
    s_gpsDiagLastY = py;
    s_gpsDiagLastTick = GetTickCount();
    s_gpsDiagAccumDist = 0;
    s_gpsDiagFrames = 0;

    // v0.12.07: Ceiling rounding — always round up so the player walks at least
    // as many steps as announced. e.g., 1300/320 = 4.06 → 5 steps.
    int steps = (int)ceilf(dist / GPS_STEPS_DIVISOR);
    if (steps < 1) steps = 1;

    char buf[256];
    snprintf(buf, sizeof(buf), "Navigating to %s. %s, %d steps.",
             tgt.name, GPS_DIR_NAMES[dirIdx], steps);
    ScreenReader::Speak(buf, true);
    Log::Field("FieldNavigation: [GPS] Started: %s dir=%s dist=%.0f steps=%d arriveDist=%.0f nearbyDist=%.0f catIdx=%d",
               tgt.name, GPS_DIR_NAMES[dirIdx], dist, steps, s_gpsArriveDist, s_gpsNearbyDist, catalogIdx);
}

static void UpdateGPS()
{
    if (!s_gpsActive) return;
    if (s_playerEntityIdx < 0) return;
    if (s_gpsCatalogIdx < 0 || s_gpsCatalogIdx >= s_catalogCount) {
        StopGPS("Navigation off.");
        return;
    }

    // Stop GPS if dialog opens.
    if (FieldDialog::IsDialogOpen()) return;

    float px = 0, py = 0, tx = 0, ty = 0;
    if (!GetEntityPos(s_playerEntityIdx, px, py)) return;
    if (!GetCatalogTargetPos(s_gpsCatalogIdx, tx, ty)) return;

    float dx = tx - px;
    float dy = ty - py;
    float dist = sqrtf(dx * dx + dy * dy);
    int dirIdx = ComputeScreenDirIndex(dx, dy);
    // v0.12.07: Ceiling rounding for steps.
    int steps = (int)ceilf(dist / GPS_STEPS_DIVISOR);
    if (steps < 1) steps = 1;

    // --- Movement rate tracking (for step calibration diagnostic) ---
    {
        float mdx = px - s_gpsDiagLastX;
        float mdy = py - s_gpsDiagLastY;
        float moveDist = sqrtf(mdx * mdx + mdy * mdy);
        if (moveDist > 0.5f) {  // only track meaningful movement
            s_gpsDiagAccumDist += moveDist;
            s_gpsDiagFrames++;
        }
        s_gpsDiagLastX = px;
        s_gpsDiagLastY = py;

        // Log movement rate every 3 seconds for calibration.
        DWORD diagNow = GetTickCount();
        if (diagNow - s_gpsDiagLastTick >= 3000 && s_gpsDiagAccumDist > 0) {
            float elapsed = (float)(diagNow - s_gpsDiagLastTick) / 1000.0f;
            float unitsPerSec = s_gpsDiagAccumDist / elapsed;
            float unitsPerFrame = (s_gpsDiagFrames > 0) ? s_gpsDiagAccumDist / (float)s_gpsDiagFrames : 0;
            Log::Field("FieldNavigation: [GPS-SPEED] %.1f units/sec, %.1f units/frame, %d frames in %.1fs, total=%.0f units",
                       unitsPerSec, unitsPerFrame, s_gpsDiagFrames, elapsed, s_gpsDiagAccumDist);
            s_gpsDiagLastTick = diagNow;
            s_gpsDiagAccumDist = 0;
            s_gpsDiagFrames = 0;
        }
    }

    // --- Arrival check (v0.12.04: per-target threshold from talk radius) ---
    if (dist <= s_gpsArriveDist && !s_gpsArrivedAnnounced) {
        s_gpsArrivedAnnounced = true;
        ScreenReader::Speak("In range.", true);
        Log::Field("FieldNavigation: [GPS] In range (dist=%.0f steps=%d)", dist, steps);
        StopGPS(nullptr);  // silent stop after arrival
        return;
    }

    // --- Nearby announcement (one-shot, v0.12.04: per-target threshold) ---
    if (dist <= s_gpsNearbyDist && !s_gpsNearbyAnnounced) {
        s_gpsNearbyAnnounced = true;
        char buf[128];
        snprintf(buf, sizeof(buf), "Nearby. %s, %d steps.", GPS_DIR_NAMES[dirIdx], steps);
        ScreenReader::Speak(buf, true);
        s_gpsLastAnnounceTime = GetTickCount();
        s_gpsLastDist = dist;
        Log::Field("FieldNavigation: [GPS] Nearby: dir=%s dist=%.0f steps=%d", GPS_DIR_NAMES[dirIdx], dist, steps);
        return;
    }

    // --- Periodic distance+direction announcement ---
    DWORD now = GetTickCount();
    DWORD interval = GPS_ANNOUNCE_INTERVAL_FAR;
    if (dist < GPS_CLOSE_DIST) interval = GPS_ANNOUNCE_INTERVAL_CLOSE;
    else if (dist < s_gpsNearbyDist) interval = GPS_ANNOUNCE_INTERVAL_NEAR;

    if (now - s_gpsLastAnnounceTime >= interval) {
        s_gpsLastAnnounceTime = now;
        char buf[128];
        snprintf(buf, sizeof(buf), "%s, %d steps.", GPS_DIR_NAMES[dirIdx], steps);
        ScreenReader::Speak(buf, true);
        Log::Field("FieldNavigation: [GPS] Update: dir=%s dist=%.0f steps=%d", GPS_DIR_NAMES[dirIdx], dist, steps);
        s_gpsLastDist = dist;
    }
}

