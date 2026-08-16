// world_map_drive_helpers.inl - AD lifecycle helpers (v0.18.3.225 split)
// Split out of world_map_drive.inl (was 156 KB, over the 80 KB CI guard).
// Contains PressKey/SetDriveKeys, the mem R/W helpers, the CAMW recovery
// state + CamwLearnBlock, StopAutoDrive, the firing-area ESCAPE helpers,
// StartAutoDrive, StartSweep and the heading/position writers. Included
// from world_map.cpp BEFORE world_map_drive.inl (which holds UpdateAutoDrive).

// world_map_drive.inl - Auto-drive lifecycle + key injection (with v0.16.0 Part C)
//
// PART OF world_map.cpp -- TEXTUAL INCLUDE. Do NOT compile standalone.
//
// Held-key injection via keybd_event (PressKey/ReleaseKey/SetDriveKeys/
// ReleaseAllDriveKeys) plus the auto-drive lifecycle: StartAutoDrive,
// StopAutoDrive, StartSweep, UpdateAutoDrive.
//
// v0.16.0 Part C: StartAutoDrive checks s_destPlannerEligible[catIdx]
// before routing through PlanDrivePath. Ineligible destinations skip the
// planner entirely and use simple-coord steering (the v0.11.11-era design).
// This prevents the v0.14.95 closest-active-region fallback from misrouting
// drives toward unrelated destinations.

// keybd_event-based key injection. Arrow keys use scan codes 0x48 (UP),
// 0x4B (LEFT), 0x4D (RIGHT), all extended-key scancodes. v0.14.102 added
// the 'extended' parameter so A (gas pedal) and W (reverse) can be sent as
// non-extended keys per the v0.11.14 design.
static void PressKey(BYTE vk, BYTE scan, bool extended = true)
{
    keybd_event(vk, scan, extended ? KEYEVENTF_EXTENDEDKEY : 0, 0);
}

static void ReleaseKey(BYTE vk, BYTE scan, bool extended = true)
{
    keybd_event(vk, scan, (extended ? KEYEVENTF_EXTENDEDKEY : 0) | KEYEVENTF_KEYUP, 0);
}

static void ReleaseAllDriveKeys()
{
    if (s_keyUpHeld)    { ReleaseKey(VK_UP,    0x48); s_keyUpHeld    = false; }
    if (s_keyLeftHeld)  { ReleaseKey(VK_LEFT,  0x4B); s_keyLeftHeld  = false; }
    if (s_keyRightHeld) { ReleaseKey(VK_RIGHT, 0x4D); s_keyRightHeld = false; }
    if (s_keyDownHeld)  { ReleaseKey(VK_DOWN,  0x50); s_keyDownHeld  = false; }
    // v0.14.102: also release the gas pedal (A key, NOT extended).
    if (s_keyGasHeld)   { ReleaseKey('A',      0x1E, false); s_keyGasHeld = false; }
}

// Idempotent press/release: only generates events on state changes.
static void SetDriveKeys(bool up, bool left, bool right, bool down = false)
{
    if (up    && !s_keyUpHeld)    { PressKey(VK_UP,    0x48); s_keyUpHeld    = true; }
    if (!up   &&  s_keyUpHeld)    { ReleaseKey(VK_UP,  0x48); s_keyUpHeld    = false; }
    if (left  && !s_keyLeftHeld)  { PressKey(VK_LEFT,  0x4B); s_keyLeftHeld  = true; }
    if (!left &&  s_keyLeftHeld)  { ReleaseKey(VK_LEFT, 0x4B); s_keyLeftHeld = false; }
    if (right && !s_keyRightHeld) { PressKey(VK_RIGHT, 0x4D); s_keyRightHeld = true; }
    if (!right&&  s_keyRightHeld) { ReleaseKey(VK_RIGHT,0x4D); s_keyRightHeld = false; }
    // #67 v0.18.3.68: DOWN arrow (reverse) for the wedge-recovery burst.
    if (down  && !s_keyDownHeld)  { PressKey(VK_DOWN,  0x50); s_keyDownHeld  = true; }
    if (!down &&  s_keyDownHeld)  { ReleaseKey(VK_DOWN,0x50); s_keyDownHeld  = false; }
    // v0.14.102: gas pedal mirrors UP arrow. A key (scan 0x1E, NOT extended).
    if (up    && !s_keyGasHeld)   { PressKey('A',      0x1E, false); s_keyGasHeld   = true; }
    if (!up   &&  s_keyGasHeld)   { ReleaseKey('A',    0x1E, false); s_keyGasHeld   = false; }
}

// #67 v0.18.3.74: wrap a world-space delta into the torus' shortest representative.
static void WrapWorldDelta(int32_t& dx, int32_t& dy)
{
    const int32_t W = (int32_t)WM_WIDTH, H = (int32_t)WM_HEIGHT;
    if (dx >  W / 2) dx -= W;
    if (dx < -W / 2) dx += W;
    if (dy >  H / 2) dy -= H;
    if (dy < -H / 2) dy += H;
}

// #67 v0.18.3.74: pull a measured basis axis toward a freshly observed unit
// motion (EMA), renormalized. Tracks the world-map camera as it swings mid-drive.
static void RefreshBasisAxis(double& ax, double& ay, double nx, double ny)
{
    ax = ax * (1.0 - DRIVE_BASIS_EMA) + nx * DRIVE_BASIS_EMA;
    ay = ay * (1.0 - DRIVE_BASIS_EMA) + ny * DRIVE_BASIS_EMA;
    double l = sqrt(ax * ax + ay * ay);
    if (l > 1e-6) { ax /= l; ay /= l; }
}

// #67 v0.18.3.75: re-perpendicularize unit axis (ax,ay) against unit (bx,by)
// (Gram-Schmidt), keeping the screen basis a valid rotating orthonormal frame as
// the camera swings. Without this, refreshing only the UP axis (the common case
// -- steering presses UP most of the time) would let uHat drift onto rHat and
// collapse the basis.
static void OrthonormalizeAgainst(double& ax, double& ay, double bx, double by)
{
    double d = ax * bx + ay * by;
    ax -= d * bx; ay -= d * by;
    double l = sqrt(ax * ax + ay * ay);
    if (l > 1e-6) { ax /= l; ay /= l; }
}

// #67 v0.18.3.82: is the straight line between two fine cells clear of cells
// that block FOOT/CAR travel (ocean, or steep mountain) -- the SAME block rule
// the clearance field and IsFineTraversable use for foot/car. Self-contained
// (reads only s_walkClassFine/s_steepFine + the SEG_*/WM_MTN_STEEP_BLOCK consts
// in state.inl) so it has no include-order dependency. Torus-aware; samples each
// interpolated cell between the endpoints. Used to clamp the drive's lookahead
// steer target so it is never aimed THROUGH a cliff corner the winding route
// goes AROUND. (The Dollet patch ledge is forced-steep MOUNTAIN, so it counts as
// blocked here too -- the target can't be aimed across it either.)
static bool FineLineClearFootCar(int c0, int r0, int c1, int r1)
{
    int dc = c1 - c0, dr = r1 - r0;
    if (dc >  WM_FINE_COLS / 2) dc -= WM_FINE_COLS;
    if (dc < -WM_FINE_COLS / 2) dc += WM_FINE_COLS;
    if (dr >  WM_FINE_ROWS / 2) dr -= WM_FINE_ROWS;
    if (dr < -WM_FINE_ROWS / 2) dr += WM_FINE_ROWS;
    int adc = dc < 0 ? -dc : dc;
    int adr = dr < 0 ? -dr : dr;
    int steps = adc > adr ? adc : adr;
    if (steps <= 0) return true;
    for (int i = 1; i <= steps; i++) {
        int cc = c0 + (int)((int64_t)dc * i / steps);
        int rr = r0 + (int)((int64_t)dr * i / steps);
        int wc = ((cc % WM_FINE_COLS) + WM_FINE_COLS) % WM_FINE_COLS;
        int wr = ((rr % WM_FINE_ROWS) + WM_FINE_ROWS) % WM_FINE_ROWS;
        uint8_t cls = s_walkClassFine[wr][wc];
        if (cls == SEG_OCEAN ||
            (cls == SEG_MOUNTAIN && s_steepFine[wr][wc] > WM_MTN_STEEP_BLOCK))
            return false;
    }
    return true;
}

// ============================================================================
// v0.18.3.201: SEH-isolated raw memory helpers for the camera-write steering.
// Same C2712 isolation pattern as WriteWorldMapHeading below: each __try lives
// in its own function so callers with C++ unwinding compile cleanly.
// ============================================================================
static uint16_t ReadMemWord16(uint32_t addr)
{
    __try { return *(volatile uint16_t*)addr; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}
static uint32_t ReadMemDword32(uint32_t addr)
{
    __try { return *(volatile uint32_t*)addr; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}
static void WriteMemWord16(uint32_t addr, uint16_t v)
{
    __try { *(volatile uint16_t*)addr = v; }
    __except (EXCEPTION_EXECUTE_HANDLER) {}
}
static void WriteMemDword32(uint32_t addr, uint32_t v)
{
    __try { *(volatile uint32_t*)addr = v; }
    __except (EXCEPTION_EXECUTE_HANDLER) {}
}

// v0.18.3.206: is the character standing on an ENTRY poly right now? The decoded trigger
// master gate (offline/TRIGGER_FIRING_AREAS.md): sub_545EA0 tests byte14 bit 3 of the
// current walkmesh poly record ([[0x20409FC]]+0x0E) before any program can fire. Reading
// the engine's own record is exact -- no oracle involved. Diagnostic use ([TRIGREADY]).
static bool EngineOnEntryPoly()
{
    __try {
        const uint8_t* rec = *(const uint8_t* volatile*)0x020409FC;
        if (!rec) return false;
        return (rec[0x0E] & 0x08) != 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

// v0.18.3.201: CAMERA-WRITE steering state (see the steering block in UpdateAutoDrive).
// s_camwTrim is a learned constant (persists across drives); the rest reset per drive.
static int     s_camwTrim        = 0;      // closed-loop bearing trim, signed au [-2048,2047]
static int     s_camwErrAcc      = 0;      // accumulated measured-vs-aimed bearing error
static int     s_camwErrN        = 0;      // samples in s_camwErrAcc
static bool    s_camwHadPrev     = false;  // previous-frame position/aim valid
static int32_t s_camwPx = 0, s_camwPy = 0; // previous-frame position
static int     s_camwAimPrev     = 0;      // bearing aimed last frame (au)
static bool    s_camwLockCleared = false;  // we zeroed WM_CAM_LOCK for this drive; restore on stop

// v0.18.3.202: F3 ENGINE-BLOCK RECOVERY state (offline/BAT201_ANALYSIS.md sect. 5; the S4 run
// proved this recovers even against a wrong planner). The engine hard-blocks (d+0, no slide)
// when a non-walkable poly lies within ~112u of the heading; when that happens correct aiming
// cannot help. Recovery: FAN-OUT bearings around the waypoint bearing until one actually
// moves the character (trust only measured motion), WALL-FOLLOW that bearing until the
// waypoint bearing's own 112u probe clears, and if the fan exhausts, RETREAT along our own
// breadcrumbs, LEARN the obstacle cell (AddNavBlock -> planner overlay), and re-plan around it.
static int     s_camwRec         = 0;      // 0 normal, 1 fan-out, 2 wall-follow, 3 retreat
static int     s_camwFanIdx      = 0;      // fan hold index (0..63 = 2 cycles of 32 bearings; v0.18.3.204)
static int     s_camwFanTicks    = 0;      // frames current fan bearing has been held
static int     s_camwFanBear     = 0;      // escape bearing (wall-follow aim)
static int     s_camwWallTicks   = 0;      // frames in wall-follow
static int     s_camwFreezeN     = 0;      // recovery episodes on the current leg
static int32_t s_camwFrzX = 0, s_camwFrzY = 0;   // freeze-detector / fan-motion anchor
static int     s_camwFrzTicks    = 0;
static bool    s_camwRouteBlocked = false; // set by the F2 route-progress watchdog
static int32_t s_camwCrumbX[32], s_camwCrumbY[32];   // breadcrumb ring, 64u spacing
static int     s_camwCrumbN = 0;           // crumbs stored (<=32)
static int     s_camwRetreatLeft = 0;      // crumbs left to retreat this episode
// v0.18.3.204 (BAT203_ANALYSIS sect. 6): G1/G2/G3 state. The .203 BAT ground in a
// block->fan->'clear'->block loop because (a) blocks were only learned on fan-exhaust and
// vetoed by the (statically wrong) oracle, (b) wall-follow exited on the FIRST clear probe
// and instantly re-blocked, (c) replans carried no new knowledge so they were identical.
static int     s_camwLearnEp     = 0;      // blocks learned this episode (cap 12)
static int32_t s_camwWfStartX = 0, s_camwWfStartY = 0;   // wall-follow start (travel gate)
static int     s_camwWfClearRun  = 0;      // consecutive frames desired bearing swept-clear
static int     s_camwWfCommit    = 64;     // min travel before wall-follow may exit (64..512, x2 on quick re-block)
static DWORD   s_camwResumeT     = 0;      // when normal steering last resumed (quick-re-block test)
static int     s_camwPlanBlkN    = 0;      // s_navBlkN at the last plan (replans need NEW knowledge)
static int     s_camwLastBlockBearing = 0; // bearing of the most recent engine block (fence inflation)

// v0.18.3.206: decoded-entry targeting state (see s_entryAims in world_map_trigger_data.inl).
static int  s_driveEntryAim = -1;    // index into s_entryAims for the current destination (-1 none)
static int  s_mowTried      = 0;     // firing-area mow attempts this drive (max 2)

// v0.18.3.204: G1 ENGINE-TRUTH learning -- record a blocked cell on EVERY engine-block
// event, trusting the engine over the oracle (NO walkability veto: half the .203 blocks
// happened at cells our oracle calls clear, because the engine's find-poly MRU cache is
// STATEFUL -- BAT203_ANALYSIS sect. 2 -- so no static model can predict them). Cell at 112u
// along the blocked bearing; if that spot is already known, step to 176 then 240 so repeated
// blocks at one wall THICKEN the fence instead of dissolving into dedupe. All candidates are
// >=96u from the character (no self-fencing). Cap 12 per recovery episode; the planner's
// prune valve releases any cell that turns out to make the goal unplannable.
static void CamwLearnBlock(int32_t cpx, int32_t cpy, int bearingAu)
{
    if (s_camwLearnEp >= 12) return;
    s_camwLastBlockBearing = bearingAu & 0xFFF;
    const double th = (double)(bearingAu & 0xFFF) / 4096.0 * 6.283185307179586;
    static const int LD[3] = { 112, 176, 240 };
    for (int i = 0; i < 3; i++) {
        int32_t bx = cpx + (int32_t)(sin(th) * (double)LD[i]);
        int32_t by = cpy - (int32_t)(cos(th) * (double)LD[i]);
        if (AddNavBlock(bx, by)) {
            s_camwLearnEp++;
            Log::World("WorldMap: [CAMW-REC] learned engine block at (%d,%d) d=%d brg=%d (overlay %d, ep %d)",
                       bx, by, LD[i], bearingAu & 0xFFF, s_navBlkN, s_camwLearnEp);
            return;
        }
    }
}

// ============================================================================
// Auto-drive lifecycle (v0.14.86)
// ============================================================================
static void StopAutoDrive(const char* reason)
{
    if (!s_driveActive) return;
    ReleaseAllDriveKeys();
    s_driveActive = false;
    s_sweepActive = false;
    s_sweepPhase = 0;
    s_sweepTurning = true;
    s_finalApproachEnterTick = 0;
    s_drivePathLen      = 0;
    s_drivePathIdx      = 0;
    s_drivePathPlanned  = false;
    s_driveGoalSegCount = 0;
    s_driveAwaitingArrivalDecision = false;
    s_driveExitTick                = 0;
    s_destFootFriendly     = true;
    s_drivePlannerEligible = true;   // v0.16.0.2: reset to safe default
    // #67 v0.18.3.74: screen-relative steering teardown.
    s_driveCalPhase      = DCAL_DONE;
    s_camBasisValid      = false;
    s_driveSidestepUntil = 0;
    s_drivePrevHadKeys   = false;
    // v0.18.3.201: camera-write steering teardown -- restore the region camera lock if
    // we cleared it (so scenery-facing regions get their forced yaw back), and reset the
    // per-drive measurement state. s_camwTrim is kept (a learned constant offset).
    if (s_camwLockCleared) { WriteMemDword32(WM_CAM_LOCK, 1); s_camwLockCleared = false; }
    s_camwHadPrev = false; s_camwErrAcc = 0; s_camwErrN = 0;
    s_drivePausedInField = false;   // v0.18.3.203: any stop clears a pending field-pause resume
    if (reason && *reason) {
        ScreenReader::Speak(reason, true);
        Log::World("WorldMap: [DRIVE] Stopped: %s", reason);
    } else {
        Log::World("WorldMap: [DRIVE] Stopped (silent)");
    }
}

// v0.18.3.170: PINCH ASSIST state. The native 8-way executor reliably handles open terrain
// (and random encounters roll normally), but it can't precisely thread one-cell-wide ramps /
// canyons -- the screen->world calibration it learns from in-game motion is never exact enough
// to hold the sub-cell line, so it orbits the pinch (offline, with perfect calibration, it
// threads them; the residual in-game error is the difference). When that happens we briefly
// slide the character along the planner's gate-verified route centerline (proven collision-free
// at 32u) until clear of the pinch, then hand control back to native keys. Encounters are only
// skipped during these short assists.
static bool s_driveAssistActive = false;
static int  s_driveAssistEndIdx = 0;
static bool s_driveNavResync    = false;  // v0.18.3.171: re-bootstrap native calibration after an assist
static int  s_driveSettleFrames = 0;      // v0.18.3.171: let the engine re-acquire after a position-write
static int  s_driveTeleZ        = 0x7FFFFFFF; // v0.18.3.178: Z to write during a teleport; engine-true at oracle-bad cells, else per-cell oracle

// v0.18.3.172: teleport audio cue. When the recovery teleport (position-write back onto the
// route) fires, play a rising-shimmer SFX so a blind player hears that a teleport happened.
// The wav is embedded as RCDATA (IDR_WAV_TELEPORT) and played from memory; loaded once.
static void PlayTeleportCue()
{
    static const void* s_wavPtr = nullptr;
    static bool s_wavTried = false;
    if (!s_wavTried) {
        s_wavTried = true;
        HMODULE hm = GetModuleHandleA("dinput8.dll");
        if (hm) {
            HRSRC hRes = FindResourceA(hm, MAKEINTRESOURCEA(IDR_WAV_TELEPORT), RT_RCDATA);
            if (hRes) {
                HGLOBAL hData = LoadResource(hm, hRes);
                if (hData) s_wavPtr = LockResource(hData);   // valid for the life of the module
            }
        }
        if (!s_wavPtr) Log::World("WorldMap: [DRIVE] teleport cue wav (IDR_WAV_TELEPORT) not found");
    }
    if (s_wavPtr) PlaySoundA((LPCSTR)s_wavPtr, NULL, SND_MEMORY | SND_ASYNC | SND_NODEFAULT);
}

// v0.18.3.221 (revised .221b): FIRING-AREA ESCAPE helpers.
// The B-Garden front-gate spawn sits just OUTSIDE BG's decoded north edge, but
// the BG<->Fire Cavern routenet edge begins at BG's aim vertex (deep inside the
// bbox), so the route pulls the character back SOUTH into BG within ~560ms.
// Two mechanisms, both padded so an edge-spawn is handled:
//   (1) SKIP leading world-path waypoints inside any non-target padded bbox
//       (unconditional -- stops the route diving back in from just outside).
//   (2) When the start is inside a padded non-target bbox, STEER straight out
//       by the nearest real edge + margin until padding-clear.
static const int EA_ESCAPE_MARGIN = 256;   // push the escape point past the edge
static const int EA_PAD           = 192;   // waypoint-skip: treat within-this as "in"
// v0.18.3.222: the steer-out must engage with a WIDE berth, not just the padded
// bbox. The BG spawn sits ~383u N of BG's edge -- outside EA_PAD -- so .221 let
// the character drive straight at the edge-hugging rejoin waypoint (BG's NE
// corner) and the engine's stateful MRU-cache falsely blocked that skimming
// move (BAT203). Steering toward the DESTINATION from there is clear of BG, so
// arm the destination-steer whenever the start is within EA_STEER_ARM of a
// non-target area, and hold it until the character is EA_STEER_ARM clear.
static const int EA_STEER_ARM     = 768;

static bool InPaddedArea(const EntryAimInfo& ea, int32_t px, int32_t py, int pad)
{
    return px >= ea.x0 - pad && px <= ea.x1 + pad &&
           py >= ea.y0 - pad && py <= ea.y1 + pad;
}

// v0.18.3.223: does segment (ax,ay)-(bx,by) cross the axis-aligned box
// [x0,x1]x[y0,y1]? Liang-Barsky parametric clip.
static bool SegCrossesBox(double ax, double ay, double bx, double by,
                          double x0, double y0, double x1, double y1)
{
    double dx = bx - ax, dy = by - ay;
    double t0 = 0.0, t1 = 1.0;
    double p[4] = { -dx, dx, -dy, dy };
    double q[4] = { ax - x0, x1 - ax, ay - y0, y1 - ay };
    for (int i = 0; i < 4; i++) {
        if (p[i] == 0.0) { if (q[i] < 0.0) return false; }
        else {
            double r = q[i] / p[i];
            if (p[i] < 0.0) { if (r > t1) return false; if (r > t0) t0 = r; }
            else            { if (r < t0) return false; if (r < t1) t1 = r; }
        }
    }
    return t0 <= t1;
}

// v0.18.3.223: next steer point that routes (px,py) AROUND area ai's padded box
// toward (tx,ty). Inside the box -> exit by the nearest edge; outside with a
// clear line -> the target; outside but the line crosses the box -> the best
// reachable padded-box corner. Iterating this per frame walks the perimeter.
static void EscapeSteerAround(int ai, int32_t px, int32_t py,
                              int32_t tx, int32_t ty, int32_t* sx, int32_t* sy)
{
    const EntryAimInfo& ea = s_entryAims[ai];
    double x0 = ea.x0 - EA_STEER_ARM, x1 = ea.x1 + EA_STEER_ARM;
    double y0 = ea.y0 - EA_STEER_ARM, y1 = ea.y1 + EA_STEER_ARM;
    // Inside the padded box: exit by the nearest edge.
    if (px > x0 && px < x1 && py > y0 && py < y1) {
        double dW = px - x0, dE = x1 - px, dS = py - y0, dN = y1 - py;
        double b = dW; int side = 0;
        if (dE < b) { b = dE; side = 1; }
        if (dS < b) { b = dS; side = 2; }
        if (dN < b) { b = dN; side = 3; }
        *sx = px; *sy = py;
        if (side == 0) *sx = (int)(x0 - 16); else if (side == 1) *sx = (int)(x1 + 16);
        else if (side == 2) *sy = (int)(y0 - 16); else *sy = (int)(y1 + 16);
        return;
    }
    // Outside with a clear line: steer straight at the target.
    if (!SegCrossesBox(px, py, tx, ty, x0, y0, x1, y1)) { *sx = tx; *sy = ty; return; }
    // Else round the best reachable corner (test against a slightly-shrunk box so
    // the corner itself, which lies on the full box, is reachable).
    double sx0 = x0 + 16, sy0 = y0 + 16, sx1 = x1 - 16, sy1 = y1 - 16;
    double cx[4] = { x0, x1, x1, x0 }, cy[4] = { y0, y0, y1, y1 };
    double best = 1e18; int bi = -1;
    for (int i = 0; i < 4; i++) {
        if (SegCrossesBox(px, py, cx[i], cy[i], sx0, sy0, sx1, sy1)) continue;
        double d = CalculateWrappedDistance(px, py, (int32_t)cx[i], (int32_t)cy[i])
                 + CalculateWrappedDistance((int32_t)cx[i], (int32_t)cy[i], tx, ty);
        if (d < best) { best = d; bi = i; }
    }
    if (bi >= 0) { *sx = (int)cx[bi]; *sy = (int)cy[bi]; }
    else         { *sx = tx; *sy = ty; }
}

// Index of a NON-target firing area whose padded bbox contains (px,py), or -1.
static int EscapeAreaContaining(int32_t px, int32_t py, const char* targetName, int pad)
{
    for (int i = 0; i < ENTRY_AIM_COUNT; i++) {
        const EntryAimInfo& ea = s_entryAims[i];
        if (targetName && strcmp(ea.name, targetName) == 0) continue;   // target area is fine
        if (InPaddedArea(ea, px, py, pad)) return i;
    }
    return -1;
}

// Exit point just OUTSIDE the nearest real edge of area `ai`, pushed by margin.
static void ComputeEscapePoint(int ai, int32_t px, int32_t py, int32_t* ex, int32_t* ey)
{
    const EntryAimInfo& ea = s_entryAims[ai];
    int dW = px - ea.x0, dE = ea.x1 - px, dS = py - ea.y0, dN = ea.y1 - py;
    int best = dW; int side = 0;
    if (dE < best) { best = dE; side = 1; }
    if (dS < best) { best = dS; side = 2; }
    if (dN < best) { best = dN; side = 3; }
    *ex = px; *ey = py;
    switch (side) {
        case 0: *ex = ea.x0 - EA_ESCAPE_MARGIN; break;
        case 1: *ex = ea.x1 + EA_ESCAPE_MARGIN; break;
        case 2: *ey = ea.y0 - EA_ESCAPE_MARGIN; break;
        case 3: *ey = ea.y1 + EA_ESCAPE_MARGIN; break;
    }
}

static void ArmFiringAreaEscape(int32_t px, int32_t py)
{
    s_driveEscapeActive  = false;
    s_driveEscapeAreaIdx = -1;

    // (1) EXCURSION SKIP: the route's early waypoints (hop-on + the routenet
    // edge's location-aim end) dip INTO the non-target area even when the start
    // is just outside it, so skip PAST the last in-area waypoint in a forward
    // window -- landing on the first waypoint that has left the area heading to
    // the destination. Bounded so a target that legitimately neighbours the
    // area isn't over-skipped.
    if (s_drivePathPlanned && s_drivePathWorld && s_drivePathLen > 0) {
        int scanEnd = s_drivePathIdx + 60;
        if (scanEnd > s_drivePathLen - 1) scanEnd = s_drivePathLen - 1;
        int lastInArea = -1;
        for (int k = s_drivePathIdx; k <= scanEnd; k++) {
            if (EscapeAreaContaining(s_drivePathWX[k], s_drivePathWY[k],
                                     s_driveTargetName, EA_PAD) >= 0)
                lastInArea = k;
        }
        if (lastInArea >= s_drivePathIdx && lastInArea < s_drivePathLen - 1) {
            Log::World("WorldMap: [ESCAPE] skipping in-area route excursion: pathIdx %d -> %d/%d (last in-area wp %d)",
                       s_drivePathIdx, lastInArea + 1, s_drivePathLen, lastInArea);
            s_drivePathIdx = lastInArea + 1;
        }
    }

    // (2) STEER-AROUND: engage if the START is within EA_STEER_ARM of a non-target
    // bbox. v0.18.3.223: route AROUND the box toward the first on-route waypoint
    // OUTSIDE the area (not the final destination) -- the routenet edge already
    // knows the walkable exit direction, so following it avoids both re-entry and
    // the wrong-side displacement that broke Timber->Dollet.
    int ai = EscapeAreaContaining(px, py, s_driveTargetName, EA_STEER_ARM);
    if (ai < 0) return;
    // Capture the escape target = the first post-skip world-path waypoint that
    // lies outside this area's steer-arm box; fall back to the destination.
    s_driveEscapeTgtX = s_driveTargetX;
    s_driveEscapeTgtY = s_driveTargetY;
    s_driveEscapeTgtIdx = -1;
    if (s_drivePathPlanned && s_drivePathWorld && s_drivePathLen > 0) {
        for (int k = s_drivePathIdx; k < s_drivePathLen; k++) {
            if (EscapeAreaContaining(s_drivePathWX[k], s_drivePathWY[k],
                                     s_driveTargetName, EA_STEER_ARM) != ai) {
                s_driveEscapeTgtX = s_drivePathWX[k];
                s_driveEscapeTgtY = s_drivePathWY[k];
                s_driveEscapeTgtIdx = k;
                break;
            }
        }
    }
    ComputeEscapePoint(ai, px, py, &s_driveEscapeX, &s_driveEscapeY);
    s_driveEscapeActive  = true;
    s_driveEscapeAreaIdx = ai;
    Log::World("WorldMap: [ESCAPE] start within %du of %s firing area x[%d,%d] y[%d,%d] at (%d,%d) -> route around toward on-route pt (%d,%d)",
               EA_STEER_ARM, s_entryAims[ai].name, s_entryAims[ai].x0, s_entryAims[ai].x1,
               s_entryAims[ai].y0, s_entryAims[ai].y1, px, py,
               s_driveEscapeTgtX, s_driveEscapeTgtY);
}

static void StartAutoDrive(int catIdx)
{
    if (s_driveActive) return;
    // v0.18.3.257 (#79): reset the physics vehicle-detector per drive, so a
    // stale ring from a car drive can never latch a following foot drive.
    s_driveVehicleSig = false;
    s_vsHad = false; s_vsPx = 0; s_vsPy = 0; VehSigReset(s_vsSig);
    // v0.18.3.258 Part D (#79): sample the engine vehicle id at every drive
    // start -- the post-init confirmation record the .257 entry dumps couldn't
    // provide (they fire before the setup writers run).
    Log::World("WorldMap: [DRIVE] engine vehicleId=%d at drive start", GetActiveVehicleId());
    if (!s_catalogBuilt || s_catalogCount == 0) {
        ScreenReader::Speak("No locations available.", true);
        return;
    }
    if (catIdx < 0 || catIdx >= s_catalogCount) {
        ScreenReader::Speak("Invalid destination.", true);
        return;
    }

    int32_t px, py, pz;
    GetWorldMapPosition_Active(&px, &py, &pz);
    if (px == 0 && py == 0) {
        ScreenReader::Speak("Position unavailable. Try again.", true);
        return;
    }

    s_sweepAbortCount = 0;

    const LocationEntry& dest = s_catalog[catIdx];

    // v0.14.89: prefer refined entry coord when available.
    int locIdx = FindLocationIndexByTargetCoords(dest.x, dest.y);
    if (locIdx >= 0 && s_refinedHas[locIdx]) {
        s_driveTargetX = s_refinedX[locIdx];
        s_driveTargetY = s_refinedY[locIdx];
        Log::World("WorldMap: [DRIVE] Using refined entry for %s: (%d,%d) instead of catalog (%d,%d)",
                   dest.name, s_refinedX[locIdx], s_refinedY[locIdx], dest.x, dest.y);
    } else {
        s_driveTargetX = dest.x;
        s_driveTargetY = dest.y;
    }
    strncpy(s_driveTargetName, dest.name, sizeof(s_driveTargetName) - 1);
    s_driveTargetName[sizeof(s_driveTargetName) - 1] = '\0';

    // v0.18.3.206: DECODED ENTRY AREAS. If the destination's field-entry trigger is fully
    // decoded (s_entryAims, from offline/TRIGGER_FIRING_AREAS.md), make sure the drive
    // target lies INSIDE the firing area: keep the refined/proven coordinate when it
    // already does (Timber's seed, G-Garden's ggview1 entrance...), else retarget to the
    // validated aim point. This replaces marker-guessing with the engine's own geometry.
    s_driveEntryAim = FindEntryAim(s_driveTargetName);
    s_mowTried      = 0;
    if (s_driveEntryAim >= 0) {
        const EntryAimInfo& ea = s_entryAims[s_driveEntryAim];
        const bool inArea = (s_driveTargetX >= ea.x0 && s_driveTargetX <= ea.x1 &&
                             s_driveTargetY >= ea.y0 && s_driveTargetY <= ea.y1);
        if (!inArea) {
            Log::World("WorldMap: [ENTRYAIM] %s target (%d,%d) is OUTSIDE the decoded firing area x[%d,%d] y[%d,%d] -- retargeting to aim (%d,%d)",
                       s_driveTargetName, s_driveTargetX, s_driveTargetY,
                       ea.x0, ea.x1, ea.y0, ea.y1, ea.aimX, ea.aimY);
            s_driveTargetX = ea.aimX;
            s_driveTargetY = ea.aimY;
        } else {
            Log::World("WorldMap: [ENTRYAIM] %s target (%d,%d) inside decoded firing area (aim (%d,%d) available)",
                       s_driveTargetName, s_driveTargetX, s_driveTargetY, ea.aimX, ea.aimY);
        }
        if (ea.footOnly)
            Log::World("WorldMap: [ENTRYAIM] %s is FOOT-ONLY (no car-entry clause)", s_driveTargetName);
    }

    double dist = CalculateWrappedDistance(px, py, dest.x, dest.y);
    DWORD now = GetTickCount();

    s_driveActive            = true;
    s_driveStartTime         = now;
    s_driveLastAnnounce      = now;
    s_driveLastDist          = dist;
    s_driveStuckX            = px;
    s_driveStuckY            = py;
    s_driveStuckCheckTime    = now;
    s_driveStuckCount        = 0;
    s_driveReplanCount       = 0;   // #67 v0.18.3.59: fresh recovery budget per drive
    s_driveAssistActive      = false; // v0.18.3.170: fresh pinch-assist state per drive
    s_driveAssistEndIdx      = 0;
    s_driveNavResync         = false; // v0.18.3.171
    s_driveSettleFrames      = 0;
    s_camwHadPrev            = false; // v0.18.3.201: fresh camera-write measurement state per drive
    s_camwErrAcc             = 0;
    s_camwErrN               = 0;
    s_drivePausedInField     = false; // v0.18.3.203
    s_drivePauseTick         = 0;
    s_driveEscapeActive      = false; // v0.18.3.221: armed after planning (below)
    s_driveEscapeAreaIdx     = -1;
    // v0.18.3.202: fresh F3 recovery state per drive (breadcrumbs from a previous drive
    // would retreat the character along a stale trail).
    s_camwRec                = 0;
    s_camwFanIdx             = 0;
    s_camwFanTicks           = 0;
    s_camwWallTicks          = 0;
    s_camwFreezeN            = 0;
    s_camwFrzX               = px;
    s_camwFrzY               = py;
    s_camwFrzTicks           = 0;
    s_camwRouteBlocked       = false;
    s_camwCrumbN             = 0;
    s_camwRetreatLeft        = 0;
    s_camwLearnEp            = 0;     // v0.18.3.204
    s_camwWfCommit           = 64;
    s_camwWfClearRun         = 0;
    s_camwResumeT            = 0;
    s_camwPlanBlkN           = s_navBlkN;
    s_sweptFailN             = 0;     // v0.18.3.204: swept-fail edges are per-drive (stale
                                      // world-keyed entries from old routes shouldn't tax new ones)
    s_driveBridgeActive      = false; // #70 v0.18.3.97: fresh bridge-out state per drive
    // v0.21.4: **STATE THAT SURVIVED A DRIVE AND CHANGED THE NEXT ONE.**
    //
    // s_drivePathWorld is the worst of them: it gates the reverse un-wedge and
    // the WHOLE route-progress/give-up watchdog block in UpdateAutoDrive, it was
    // cleared on neither the planner-ineligible path nor in StopAutoDrive, and
    // it is also written by the GARDEN executor. So whether a foot drive had
    // route watchdogs at all could depend on what the previous drive did --
    // including a drive by a different vehicle.
    //
    // s_driveInDialog must start false or a drive begun during a cutscene would
    // never take the resume branch that reseeds the watchdog clocks.
    s_drivePathWorld         = false;
    s_driveInDialog          = false;
    s_driveBridgeCount       = 0;
    // #67 v0.18.3.62: reset motion-derived heading tracking for the new drive.
    s_driveMoveHeading    = -1;     // unknown until he moves
    s_driveHeadRefX       = px;
    s_driveHeadRefY       = py;
    s_driveTurnSign       = 1;      // initial guess; self-calibrates from observed rotation
    s_driveTurnedSinceRef = false;
    s_driveLastTurnRight  = false;
    s_driveLastMoveTime   = now;
    s_driveLastMovePosX   = px;
    s_driveLastMovePosY   = py;
    s_driveWedgeReverseUntil = 0;       // #67 v0.18.3.68: fresh reverse un-wedge state
    s_driveWedgeReverseCount = 0;
    s_driveWedgeProgressDist = dist;
    s_driveWedgeAnchorX      = px;       // #67 v0.18.3.69: net-displacement wedge anchor
    s_driveWedgeAnchorY      = py;
    s_driveWedgeAnchorTime   = now;
    s_driveApproachAnnounced = (dist < DRIVE_APPROACH_DIST);
    s_finalApproachEnterTick = 0;
    s_sweepActive            = false;
    s_sweepPhase             = 0;
    s_sweepTurning           = true;

    s_driveOnFootAtStart = (s_lastVehicle < 0) ||
                           (GetVehicleType((uint8_t)s_lastVehicle) == VEH_ON_FOOT);

    // #67 v0.18.3.74: screen-relative steering reset + calibration arm (foot only).
    // The basis (what UP / RIGHT do in world space) is measured at drive start so
    // we never assume a fixed camera orientation. A close start or a vehicle skips
    // the probe and relies on the default + live refresh.
    s_camBasisValid      = false;
    s_camUx = 0.0; s_camUy = -1.0;          // default North until measured
    s_camRx = 1.0; s_camRy =  0.0;          // default East  until measured
    s_drivePrevHadKeys   = false;
    s_driveSidestepUntil = 0;
    s_driveSidestepSign  = 1;
    s_driveProbeValid    = false;   // #67 v0.18.3.77: re-probe the steering arrow on a fresh drive
    s_driveCalTry        = 0;
    // #67 v0.18.3.77: the greedy arrow-probe needs no measured basis, so skip the
    // UP/RIGHT calibration wobble entirely -- it just trusts measured progress.
    s_driveCalPhase = DCAL_DONE;
    s_camBasisValid = true;

    int distKm = (int)(dist / 1000.0);
    char buf[160];
    if (distKm < 1) {
        snprintf(buf, sizeof(buf), "Driving to %s. Very close.", s_driveTargetName);
    } else {
        snprintf(buf, sizeof(buf), "Driving to %s. %d kilometers.", s_driveTargetName, distKm);
    }
    ScreenReader::Speak(buf, true);
    Log::World("WorldMap: [DRIVE] Start \u2192 %s at (%d,%d), dist=%.0f units (%d km)",
               s_driveTargetName, s_driveTargetX, s_driveTargetY, dist, distKm);

    // v0.14.103.7: classify foot-friendliness for the sweep-abort threshold.
    s_destFootFriendly = IsLocationFootFriendly(s_driveTargetX, s_driveTargetY);
    {
        int destCol = WorldXToSegCol(s_driveTargetX);
        int destRow = WorldYToSegRow(s_driveTargetY);
        uint8_t destReg = (s_segmentRegionLoaded &&
                           destCol >= 0 && destCol < WMX_SEG_COLS &&
                           destRow >= 0 && destRow < WMX_SEG_ROWS)
                          ? s_segmentRegionMap[destRow][destCol]
                          : 0xFF;
        Log::World("WorldMap: [DRIVE] Destination foot-friendly=%s (target seg(%d,%d) region=0x%02X)",
                   s_destFootFriendly ? "YES" : "NO",
                   destCol, destRow, (unsigned)destReg);
    }

    // v0.14.94: run the path planner once. Sets s_drivePath[]/Len/Idx/Planned
    // and s_driveGoalSegs[]/Count. On failure (Ragnarok, region map not
    // loaded, no matching trigger program, no path), s_drivePathPlanned
    // stays false and AD falls back to catalog-center steering with the
    // v0.14.93 distance-based arrival heuristic. UpdateAutoDrive picks the
    // active mode from s_drivePathPlanned each tick.
    //
    // v0.16.0 Part C: gate on s_destPlannerEligible[locIdx]. Geometric-trigger
    // destinations (no foot clause in s_triggerPrograms[] for this region)
    // must skip the planner entirely -- the v0.14.95 closest-active-region
    // fallback misroutes them toward unrelated destinations. They use the
    // v0.11.11-era simple-coord steering (catalog-center, bearing-based)
    // implemented in UpdateAutoDrive's non-planner branch.
    //
    // v0.16.0.1: index by locIdx (s_locations master-table position) NOT
    // catIdx (s_catalog BFS-filtered/distance-sorted position). The v0.16.0
    // BAT showed Fire Cavern (master idx 37, catIdx 2) reading Dollet's
    // eligibility (master idx 2 = YES) and running the planner anyway,
    // hitting the closest-active-region fallback toward seg(18,20). Part B
    // caught the off-target arrival but the planner walk shouldn't have
    // fired at all. FindLocationIndexByTargetCoords resolves locIdx above.
    //
    // v0.16.0.2: persist the decision in s_drivePlannerEligible so the
    // replan-on-world-map-re-entry path in Poll() can honor it too. The
    // v0.16.0.1 BAT showed Fire Cavern starting correctly via simple-coord
    // (planned=0) but the replan after a random encounter called
    // PlanDrivePath unconditionally and converted the drive to planner-
    // routed toward the wrong destination. One decision, made once.
    bool plannerEligible = (locIdx >= 0 && locIdx < LOCATION_COUNT &&
                            s_destPlannerEligible[locIdx]);
    s_drivePlannerEligible = plannerEligible;
    if (plannerEligible) {
        PlanDrivePath(px, py);
    } else {
        Log::World("WorldMap: [DRIVE] Geometric-trigger destination %s (locIdx=%d, planner-ineligible) \u2014 using simple-coord steering",
                   s_driveTargetName, locIdx);
        s_drivePathLen      = 0;
        s_drivePathIdx      = 0;
        s_drivePathPlanned  = false;
        s_driveGoalSegCount = 0;
    }

    // v0.18.3.221: if we're standing inside a non-target firing area, arm the
    // escape so the executor exits it before following the route.
    ArmFiringAreaEscape(px, py);
}

static void StartSweep(int32_t px, int32_t py, DWORD now)
{
    s_sweepActive   = true;
    s_sweepPhase    = 1;
    s_sweepTurning  = true;
    s_sweepStateEnd = now + SWEEP_TURN_BASE_MS;
    s_driveStuckX = px;
    s_driveStuckY = py;
    s_driveStuckCheckTime = now;
    s_driveStuckCount = 0;
    ScreenReader::Speak("Searching for entrance.", true);
    // v0.21.2: a sweep means the mod arrived and no field loaded. That is the
    // exact moment to record what the game's own trigger test is seeing.
    LogTriggerEvaluation("sweep-start");
    LogTriggerWalk("sweep-start");
    Log::World("WorldMap: [DRIVE-SWEEP] Started (target=%s, phase 1 turning right %dms)",
               s_driveTargetName, SWEEP_TURN_BASE_MS);
}

// v0.18.3.154: write the move-heading register (0x0203FE52). SEH isolated in its
// own function so UpdateAutoDrive (which has C++ unwinding) doesn't hit C2712.
static void WriteWorldMapHeading(uint16_t h)
{
    __try { *(volatile uint16_t*)WM_HEADING = h; }
    __except (EXCEPTION_EXECUTE_HANDLER) {}
}

// v0.18.3.157: write the on-foot world position (WM_POS_X/Y DWORDs). SEH isolated
// for the same C2712 reason as WriteWorldMapHeading. The engine snaps Z to the
// walkmesh under X/Y each frame, so we don't write Z.
static void WriteWorldMapPosition(int32_t x, int32_t y, int32_t z = 0x7FFFFFFF)
{
    __try {
        *(volatile int32_t*)WM_POS_X = x;
        *(volatile int32_t*)WM_POS_Y = y;
        // v0.18.3.174: also write Z (ground height) so the character is GROUNDED after a teleport.
        // The .173 BAT proved that after a position-write the engine receives input (facing/move-
        // heading swings) but refuses to TRANSLATE the character (d+0 on every key) -- the signature
        // of an ungrounded character (can't walk in the air). We had assumed the engine snaps Z each
        // frame; it evidently doesn't after a far jump, so we set it explicitly.
        if (z != 0x7FFFFFFF) *(volatile int32_t*)WM_POS_Z = z;
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
}

// v0.18.3.173: read the FIXED per-region world-map CAMERA YAW (0x0203ED02). The v0.18.3.79
// [YAWPROBE] RE proved holding UP walks the world bearing == this yaw (within ~1deg) and RIGHT
// = yaw+90 CW, so the on-foot 8-way keys map to world directions (yaw + k*45deg). Reading it
// directly lets the executor steer EXACTLY instead of learning the angle from noisy motion
// (the source of the drift/orbit). Returns -1 if the read faults.
static int GetWorldMapCameraYaw()
{
    __try {
        return (int)(*(volatile uint16_t*)0x0203ED02) & 0xFFF;
    } __except (EXCEPTION_EXECUTE_HANDLER) { return -1; }
}

