// world_map_heading_scan.inl - #67 live-facing discovery diagnostic
//
// PART OF world_map.cpp -- TEXTUAL INCLUDE. Do NOT compile standalone.
// Included AFTER world_map_drive.inl (uses SetDriveKeys/ReleaseAllDriveKeys)
// and the segment readers (GetWorldMapPosition/GetWorldMapHeading).
//
// WHY THIS EXISTS
// ---------------
// Every world-map auto-drive failure since v0.18.3.55 traces to one missing
// sensor: we cannot read which way the character is FACING. WM_HEADING
// (0x0203ED02) is frozen -- it returns a constant all session, so it was never
// the real facing. The motion-derived heading we fall back on is lossy (lags
// the turn) and corrupted whenever he slides along a wall (his motion then
// follows the wall, not his facing), which is what produces the bang-bang
// oscillation and the canyon wedge.
//
// The real facing DOES exist in memory (FFNx's world.cpp reads the live camera
// out of sub_4023D0; the savemap WORLDMAP struct carries a rotation field). We
// just have the wrong address. This diagnostic finds the right one
// deterministically and blind-accessibly: while the player stands still, it
// INJECTS a known turn -- hold RIGHT ~1.6s, then LEFT ~1.6s, turn-only (no
// forward, so he pivots roughly in place) -- and dumps a window of candidate
// memory each ~150ms. The value that RAMPS one way during RIGHT and REVERSES
// during LEFT is the live facing. No sighted step, no manual timing: press F12,
// then send Logs/ff8_world.log.
//
// Requirements honored: TTS announcement on completion; pause/resume if a
// random encounter pulls us off the world map mid-scan (driven by Poll()'s
// existing world-map enter/exit detection, same mechanism auto-drive uses).
//
// Gate-don't-delete: set HEADING_SCAN_DIAG 0 to retire after the address is
// found. RETIRED v0.18.3.73 -- superseded by world_map_camera_scan.inl. The
// v0.18.3.72 BAT proved WM_HEADING is frozen for on-foot AND that this scan's
// premise was wrong for the on-foot case: it injected RIGHT/LEFT, which are
// WALK keys on foot (not rotation), so it could never surface a live facing.
// The camera scan injects the real G/H camera-rotate keys instead. F12 now
// routes to WorldMap::TriggerCameraScan (dinput8.cpp), so this is no longer an
// F12 handler -- kept gated-off for reference only.

#define HEADING_SCAN_DIAG 0

// ---- Candidate memory windows (interpreted as int16 words) -----------------
// foot: around the live foot position DWORDs (WM_POS_X=0x0203EE80) -- the foot
//       character struct, the most likely home of the on-foot facing.
// hdng: around WM_HEADING(0x0203ED02)/WM_SCENE_FLAG(0x0203ED2C) -- confirms the
//       frozen field and catches any live facing nearby.
// save: savemap char_pos[6] (X,Z,Y,unk,unk,rotation) at WM_CHAR_POS_ADDR; the
//       rotation field sits at +0x0A.
struct HScanWin { const char* label; uintptr_t base; int bytes; };
static const int HSCAN_WIN_COUNT = 3;
static const HScanWin s_hscanWins[HSCAN_WIN_COUNT] = {
    { "foot", 0x0203EE00u, 256 },
    { "hdng", 0x0203ED00u,  64 },
    { "save", 0x01CFFEB8u,  32 },
};
static const int HSCAN_MAX_WIN_WORDS = 128;          // largest window = 256B = 128 words
static const int HSCAN_TOTAL_WORDS   = 128 + 32 + 16; // 176

// ---- Timing ----------------------------------------------------------------
static const DWORD HSCAN_SETTLE_PRE_MS    = 400;   // let residual motion settle, snapshot baseline
static const DWORD HSCAN_TURN_MS          = 1600;  // hold each turn this long
static const DWORD HSCAN_SETTLE_MID_MS    = 400;   // pause between RIGHT and LEFT
static const DWORD HSCAN_SAMPLE_INTERVAL_MS = 150; // ~10-11 samples per turn

enum HeadingScanState {
    HSCAN_OFF = 0,
    HSCAN_SETTLE_PRE,
    HSCAN_TURN_RIGHT,
    HSCAN_SETTLE_MID,
    HSCAN_TURN_LEFT,
    HSCAN_DONE
};

static HeadingScanState s_hscanState        = HSCAN_OFF;
static bool             s_hscanSuspended    = false;   // paused by encounter (off world map)
static DWORD            s_hscanStateStart    = 0;
static DWORD            s_hscanLastSample    = 0;
static int              s_hscanSampleIdx     = 0;
static int16_t          s_hscanBase[HSCAN_TOTAL_WORDS];   // baseline (pre-turn) word values
static int16_t          s_hscanPrev[HSCAN_TOTAL_WORDS];   // previous-sample word values

// Snapshot every window's words into baseline + prev. SEH-guarded per window.
static void HScanSnapshotBaseline()
{
    int wi = 0;
    for (int w = 0; w < HSCAN_WIN_COUNT; w++) {
        int nwords = s_hscanWins[w].bytes / 2;
        if (nwords > HSCAN_MAX_WIN_WORDS) nwords = HSCAN_MAX_WIN_WORDS;
        __try {
            const int16_t* p = (const int16_t*)s_hscanWins[w].base;
            for (int i = 0; i < nwords; i++) { s_hscanBase[wi + i] = p[i]; s_hscanPrev[wi + i] = p[i]; }
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            for (int i = 0; i < nwords; i++) { s_hscanBase[wi + i] = 0; s_hscanPrev[wi + i] = 0; }
            Log::World("WorldMap: [HSCAN] baseline read fault on window %s", s_hscanWins[w].label);
        }
        wi += nwords;
    }
}

// One sample: log foot pos + frozen WM_HEADING (anchors), then for each window
// the words that changed since the previous sample, with per-sample delta (s)
// and delta-from-baseline (b). The live facing is the offset whose s-delta is
// consistently one sign through RIGHT and flips through LEFT.
static void HScanSample(const char* phase, const char* injKey)
{
    int32_t px = 0, py = 0, pz = 0;
    GetWorldMapPosition(&px, &py, &pz);
    uint16_t rawH = GetWorldMapHeading();
    Log::World("WorldMap: [HSCAN] %s s%d key=%s pos(%d,%d) WM_HEADING=%u",
               phase, s_hscanSampleIdx, injKey, px, py, (unsigned)rawH);

    int wi = 0;
    for (int w = 0; w < HSCAN_WIN_COUNT; w++) {
        int nwords = s_hscanWins[w].bytes / 2;
        if (nwords > HSCAN_MAX_WIN_WORDS) nwords = HSCAN_MAX_WIN_WORDS;

        int16_t cur[HSCAN_MAX_WIN_WORDS];
        bool ok = true;
        __try {
            const int16_t* p = (const int16_t*)s_hscanWins[w].base;
            for (int i = 0; i < nwords; i++) cur[i] = p[i];
        } __except (EXCEPTION_EXECUTE_HANDLER) { ok = false; }

        if (!ok) {
            Log::World("WorldMap: [HSCAN]   %s: read fault", s_hscanWins[w].label);
            wi += nwords;
            continue;
        }

        char line[640];
        int  pos = 0;
        int  changed = 0;
        for (int i = 0; i < nwords && pos < (int)sizeof(line) - 48 && changed < 40; i++) {
            int16_t prevv = s_hscanPrev[wi + i];
            if (cur[i] != prevv) {
                int dS = (int)cur[i] - (int)prevv;
                int dB = (int)cur[i] - (int)s_hscanBase[wi + i];
                pos += snprintf(line + pos, sizeof(line) - pos,
                                "+0x%03X:%d->%d(s%+d,b%+d) ",
                                i * 2, (int)prevv, (int)cur[i], dS, dB);
                changed++;
            }
            s_hscanPrev[wi + i] = cur[i];
        }
        if (changed > 0)
            Log::World("WorldMap: [HSCAN]   %s changed: %s", s_hscanWins[w].label, line);
        else
            Log::World("WorldMap: [HSCAN]   %s changed: (none)", s_hscanWins[w].label);

        wi += nwords;
    }
    s_hscanSampleIdx++;
}

// ---- Public trigger (F12, from dinput8.cpp) --------------------------------
void TriggerHeadingScan()
{
#if HEADING_SCAN_DIAG
    if (s_hscanState != HSCAN_OFF) {
        // Already running -> F12 cancels.
        ReleaseAllDriveKeys();
        s_hscanState     = HSCAN_OFF;
        s_hscanSuspended = false;
        ScreenReader::Speak("Heading scan cancelled.", true);
        Log::World("WorldMap: [HSCAN] Cancelled by F12.");
        return;
    }
    if (!s_onWorldMap) {
        ScreenReader::Speak("Heading scan only works on the world map.", true);
        return;
    }
    if (s_driveActive) {
        ScreenReader::Speak("Cancel auto-drive before running the heading scan.", true);
        return;
    }

    s_hscanState     = HSCAN_SETTLE_PRE;
    s_hscanSuspended = false;
    s_hscanStateStart = GetTickCount();
    s_hscanLastSample = 0;
    s_hscanSampleIdx  = 0;
    ReleaseAllDriveKeys();   // clean key state

    int32_t px = 0, py = 0, pz = 0;
    GetWorldMapPosition(&px, &py, &pz);
    Log::World("WorldMap: [HSCAN] === Heading scan START at pos(%d,%d), WM_HEADING(raw,frozen?)=%u ===",
               px, py, (unsigned)GetWorldMapHeading());
    Log::World("WorldMap: [HSCAN] windows: foot@0x0203EE00(256B) hdng@0x0203ED00(64B) save@0x01CFFEB8(32B); RIGHT %ums then LEFT %ums, sample every %ums. Find the offset whose s-delta holds one sign in RIGHT and flips in LEFT.",
               (unsigned)HSCAN_TURN_MS, (unsigned)HSCAN_TURN_MS, (unsigned)HSCAN_SAMPLE_INTERVAL_MS);
    ScreenReader::Speak("Heading scan started. Hold still.", true);
#else
    ScreenReader::Speak("Heading scan is not enabled in this build.", true);
#endif
}

// ---- Pause / resume (called from Poll on world-map exit/entry) -------------
static void HScanPause()
{
    if (s_hscanState == HSCAN_OFF || s_hscanSuspended) return;
    ReleaseAllDriveKeys();
    s_hscanSuspended = true;
    Log::World("WorldMap: [HSCAN] Paused (left world map -- likely random encounter).");
    ScreenReader::Speak("Heading scan paused.", true);
}

static void HScanResume()
{
    if (s_hscanState == HSCAN_OFF || !s_hscanSuspended) return;
    s_hscanSuspended  = false;
    s_hscanStateStart = GetTickCount();
    s_hscanLastSample = 0;
    s_hscanSampleIdx  = 0;
    // Restart whichever phase was interrupted from its beginning so its sample
    // ramp is clean: re-snapshot the baseline and (for a turn phase) re-press
    // the turn key.
    if (s_hscanState == HSCAN_TURN_RIGHT || s_hscanState == HSCAN_TURN_LEFT) {
        HScanSnapshotBaseline();
        SetDriveKeys(false,
                     s_hscanState == HSCAN_TURN_LEFT,
                     s_hscanState == HSCAN_TURN_RIGHT);
        Log::World("WorldMap: [HSCAN] Resumed -- restarting %s phase from the top.",
                   s_hscanState == HSCAN_TURN_RIGHT ? "RIGHT" : "LEFT");
    } else {
        Log::World("WorldMap: [HSCAN] Resumed -- continuing settle phase.");
    }
    ScreenReader::Speak("Resuming heading scan.", true);
}

// ---- State machine (called each frame from Poll while on the world map) ----
static void UpdateHeadingScan()
{
    if (s_hscanState == HSCAN_OFF || s_hscanSuspended) return;

    DWORD now     = GetTickCount();
    DWORD inState = now - s_hscanStateStart;

    switch (s_hscanState) {
        case HSCAN_SETTLE_PRE:
            SetDriveKeys(false, false, false);
            if (inState >= HSCAN_SETTLE_PRE_MS) {
                HScanSnapshotBaseline();
                Log::World("WorldMap: [HSCAN] Baseline captured; turning RIGHT.");
                s_hscanState      = HSCAN_TURN_RIGHT;
                s_hscanStateStart = now;
                s_hscanSampleIdx  = 0;
                s_hscanLastSample = 0;
                SetDriveKeys(false, false, true);   // RIGHT only (no forward/gas)
            }
            break;

        case HSCAN_TURN_RIGHT:
            SetDriveKeys(false, false, true);
            if (now - s_hscanLastSample >= HSCAN_SAMPLE_INTERVAL_MS) {
                s_hscanLastSample = now;
                HScanSample("RIGHT", "R");
            }
            if (inState >= HSCAN_TURN_MS) {
                SetDriveKeys(false, false, false);
                s_hscanState      = HSCAN_SETTLE_MID;
                s_hscanStateStart = now;
            }
            break;

        case HSCAN_SETTLE_MID:
            SetDriveKeys(false, false, false);
            if (inState >= HSCAN_SETTLE_MID_MS) {
                Log::World("WorldMap: [HSCAN] Turning LEFT.");
                s_hscanState      = HSCAN_TURN_LEFT;
                s_hscanStateStart = now;
                s_hscanSampleIdx  = 0;
                s_hscanLastSample = 0;
                SetDriveKeys(false, true, false);   // LEFT only
            }
            break;

        case HSCAN_TURN_LEFT:
            SetDriveKeys(false, true, false);
            if (now - s_hscanLastSample >= HSCAN_SAMPLE_INTERVAL_MS) {
                s_hscanLastSample = now;
                HScanSample("LEFT", "L");
            }
            if (inState >= HSCAN_TURN_MS) {
                SetDriveKeys(false, false, false);
                s_hscanState      = HSCAN_DONE;
                s_hscanStateStart = now;
            }
            break;

        case HSCAN_DONE:
            ReleaseAllDriveKeys();
            Log::World("WorldMap: [HSCAN] === Heading scan COMPLETE. The live facing is the foot/hdng/save offset whose per-sample (s) delta holds one sign across the RIGHT samples and flips across the LEFT samples. Frame counters ramp the SAME sign in both phases; position words move only if he translated. ===");
            ScreenReader::Speak("Heading scan complete.", true);
            s_hscanState = HSCAN_OFF;
            break;

        default:
            s_hscanState = HSCAN_OFF;
            break;
    }
}
