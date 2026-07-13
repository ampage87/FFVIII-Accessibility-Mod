// world_map_drive.inl - UpdateAutoDrive (the per-frame AD executor).
// v0.18.3.225: the lifecycle helpers moved to world_map_drive_helpers.inl and
// the function body is split at ~half via a mid-function include of
// world_map_drive_exec.inl, so each .inl stays under the 80 KB CI guard.
// world_map.cpp includes: ..._helpers.inl, then THIS file.

static void UpdateAutoDrive()
{
    if (!s_driveActive) return;

    int32_t px, py, pz;
    GetWorldMapPosition_Active(&px, &py, &pz);
    uint16_t heading = GetWorldMapHeading();

    if (px == 0 && py == 0) return;

    double dist = CalculateWrappedDistance(px, py, s_driveTargetX, s_driveTargetY);
    DWORD now = GetTickCount();

    // v0.18.3.151: reverse-un-wedge exit-heading sweep counter. Advanced once per
    // reverse burst (stuck-trigger below); the reverse key-set turns by a swept side
    // so successive bursts back out of a pocket on different headings instead of
    // reversing straight back into the same wall (the .150 SW/Alien-Ship pocket:
    // DOWN-only freed 32u then immediately re-wedged, d+0 forever).
    static int s_unwedgeSweep = 0;

    // #70 v0.18.3.97: bridge-out arrival. While bridging out of a start pocket,
    // once the character reaches the nearest road cell, drop bridge mode and
    // re-plan from here -- the road's guard-exempt edges let the fresh plan
    // route normally toward the target.
    if (s_driveBridgeActive) {
        double distBridge = CalculateWrappedDistance(px, py, s_driveBridgeX, s_driveBridgeY);
        if (distBridge < WM_BRIDGE_ARRIVE_DIST) {
            Log::World("WorldMap: [DRIVE] Bridged out of pocket (reached road, dB=%.0f) -> re-planning to %s",
                       distBridge, s_driveTargetName);
            s_driveBridgeActive   = false;
            PlanDrivePath(px, py);
            s_driveProbeValid     = false;
            s_driveStuckCount     = 0;
            s_driveStuckX         = px;
            s_driveStuckY         = py;
            s_driveStuckCheckTime = now;
        }
    }

    // #67 v0.18.3.68: refresh the reverse un-wedge budget on genuine progress
    // toward the target (got >= WEDGE_PROGRESS_EPS closer). Reversing is bounded
    // per wedge, but the budget renews once a reverse actually helps him round a
    // corner -- so a true no-progress jam still gives up instead of reversing
    // forever.
    if (dist < s_driveWedgeProgressDist - WEDGE_PROGRESS_EPS) {
        s_driveWedgeReverseCount = 0;
        s_driveWedgeProgressDist = dist;
    }

    bool isOnFoot = (s_lastVehicle < 0) ||
                    (GetVehicleType((uint8_t)s_lastVehicle) == VEH_ON_FOOT);

    // One-shot approach announcement (suppressed during sweep).
    if (!s_driveApproachAnnounced && dist < DRIVE_APPROACH_DIST && !s_sweepActive) {
        s_driveApproachAnnounced = true;
        int distKm = (int)(dist / 1000.0);
        char buf[128];
        if (distKm < 1) {
            snprintf(buf, sizeof(buf), "Approaching %s.", s_driveTargetName);
        } else {
            snprintf(buf, sizeof(buf), "Approaching %s. %d kilometers.", s_driveTargetName, distKm);
        }
        ScreenReader::Speak(buf, true);
        s_driveLastAnnounce = now;
    }
    s_driveLastDist = dist;
    s_driveLastPosX = px;
    s_driveLastPosY = py;

    if (!s_sweepActive && now - s_driveLastAnnounce >= DRIVE_ANNOUNCE_INTERVAL_MS) {
        s_driveLastAnnounce = now;
        int distKm = (int)(dist / 1000.0);
        char buf[64];
        if (distKm < 1) {
            snprintf(buf, sizeof(buf), "Less than 1 kilometer.");
        } else {
            snprintf(buf, sizeof(buf), "%d kilometers.", distKm);
        }
        ScreenReader::Speak(buf, true);
    }

    // v0.14.99: Sweep abort on drift. MUST run BEFORE the sweep state machine.
    if (s_sweepActive && dist > DRIVE_FINAL_APPROACH_DIST * 3.0) {   // v0.18.3.144 (#70): widen entrance search 1500->3000 (Timber marker sits >1500u from its real entry trigger; STEPGUARD keeps the spiral on land)
        s_sweepAbortCount++;
        int abortLimit = s_destFootFriendly ? DRIVE_BOUNCE_ABORT_THRESHOLD : 1;
        if (s_sweepAbortCount >= abortLimit) {
            char buf[200];
            snprintf(buf, sizeof(buf),
                     "Arrived near %s. You may need to enter on foot.",
                     s_driveTargetName);
            ScreenReader::Speak(buf, true);
            Log::World("WorldMap: [DRIVE-BOUNCE] %s (sweep-abort %d/%d, dist=%.0f)",
                       buf, s_sweepAbortCount, DRIVE_BOUNCE_ABORT_THRESHOLD, dist);
            StopAutoDrive(nullptr);
            return;
        }
        Log::World("WorldMap: [DRIVE-SWEEP] Aborting (drifted out of final approach: dist=%.0f, threshold=%.0f) \u2014 retry %d/%d, returning to normal steering",
                   dist, DRIVE_FINAL_APPROACH_DIST * 3.0, s_sweepAbortCount, DRIVE_BOUNCE_ABORT_THRESHOLD);
        s_sweepActive            = false;
        s_sweepPhase             = 0;
        s_sweepTurning           = true;
        s_finalApproachEnterTick = 0;
    }

    // Sweep state machine -- v0.18.3.196: SPIRAL-ORBIT entrance search.
    if (s_sweepActive) {
        // The old blind turn+walk walked forward from wherever the character faced, drifted ~3000u
        // and bounced. Instead, steer the character AROUND the target on an expanding circle, so it
        // stays local and sweeps across an entry trigger that is OFFSET from the icon (e.g. Galbadia
        // Garden: the icon sits on the visual ring while the real field trigger is at the CENTER). The
        // moment the field loads the arrival machinery ends the drive (MODE_FIELD) and capture-on-
        // success persists the true entrance. Steering uses the camera yaw (proven), and the orbit
        // never leaves ~900u of the target so it can't drift out and bounce.
        static double s_orbitAng  = 0.0;
        static DWORD  s_orbitLast = 0;
        if (s_orbitLast == 0 || (now - s_orbitLast) > 1000) s_orbitLast = now;   // (re)seed after a pause
        double dt = (double)(now - s_orbitLast) / 1000.0; s_orbitLast = now;
        // v0.18.3.197: ANGLE SPEED tied to what the character can actually walk at the current
        // radius (the .196 fixed 2.5 rad/s outran it past ~643u, so the outer rings were never
        // reached). Cap angular speed to char_speed / R so the character keeps up and the circle is
        // truly traced out to the widened max radius.
        int Rcur = 250 + s_sweepPhase * 180; if (Rcur > 1500) Rcur = 1500;
        double wmax = 900.0 / (double)(Rcur > 1 ? Rcur : 1);   // ~char speed (u/s) / R
        double w = wmax < 1.6 ? wmax : 1.6;
        s_orbitAng += dt * w;
        if (s_orbitAng >= 6.283185307179586) {
            s_orbitAng -= 6.283185307179586;
            s_sweepPhase++;       // one revolution done; widen the circle
            if (s_sweepPhase > SWEEP_MAX_PHASES) { StopAutoDrive("Could not find entrance."); return; }
            int rr = 250 + s_sweepPhase * 180; if (rr > 1500) rr = 1500;
            Log::World("WorldMap: [DRIVE-SWEEP] orbit revolution %d/%d (radius %d)",
                       s_sweepPhase, SWEEP_MAX_PHASES, rr);
        }
        int R = 250 + s_sweepPhase * 180; if (R > 1500) R = 1500;
        int32_t ox = s_driveTargetX + (int32_t)(sin(s_orbitAng) * (double)R);
        int32_t oy = s_driveTargetY - (int32_t)(cos(s_orbitAng) * (double)R);
        int brg  = TorusBearing(px, py, ox, oy);
        int camY = GetWorldMapCameraYaw();
        int base = (camY >= 0) ? camY : 0;
        int rel  = (((brg - base) % 4096) + 4096) % 4096;
        int k    = ((rel + 256) / 512) % 8;   // nearest 8-way key toward the orbit point
        SetDriveKeys(k == 7 || k == 0 || k == 1,   // up
                     k == 5 || k == 6 || k == 7,   // left
                     k == 1 || k == 2 || k == 3,   // right
                     k == 3 || k == 4 || k == 5);  // down
        return;
    }

    // Final-approach timeout (on-foot only).
    if (isOnFoot && dist < DRIVE_FINAL_APPROACH_DIST) {
        if (s_finalApproachEnterTick == 0) {
            s_finalApproachEnterTick = now;
            Log::World("WorldMap: [DRIVE] Entered final approach zone (dist=%.0f)", dist);
        }
        // v0.18.3.206: [TRIGREADY] -- live view of the decoded entry condition during the
        // approach: is the engine's CURRENT poly an entry poly (byte14 bit 3, read from the
        // engine's own record), and are we inside the decoded firing-area bbox? One glance
        // at the log now separates "standing in the right place but not on an entry poly"
        // from "never reached the area at all".
        if (DRIVE_STEER_DIAG && s_driveEntryAim >= 0) {
            static DWORD s_trT = 0;
            if (now - s_trT >= 500) {
                const EntryAimInfo& ea = s_entryAims[s_driveEntryAim];
                const bool inA = (px >= ea.x0 && px <= ea.x1 && py >= ea.y0 && py <= ea.y1);
                Log::World("WorldMap: [TRIGREADY] pos(%d,%d) entryPoly=%d inArea=%d dist=%.0f",
                           px, py, EngineOnEntryPoly() ? 1 : 0, inA ? 1 : 0, dist);
                s_trT = now;
            }
        }
        if (now - s_finalApproachEnterTick > FINAL_APPROACH_TIMEOUT_MS) {
            // v0.18.3.206: MOW THE DECODED FIRING AREA instead of the blind spiral. The
            // .205 Timber failure: the orbit swept radii 610-1330u around a firing patch
            // that lies entirely within 432u of the seed -- a hole exactly where the
            // target was. Serpentine waypoints INSIDE the decoded bbox (clipped to 768u
            // around the aim for big areas), fed to the normal executor (full steering,
            // collision recovery, arrival machinery). Two attempts, then the old sweep.
            if (s_driveEntryAim >= 0 && s_mowTried < 2) {
                s_mowTried++;
                const EntryAimInfo& ea = s_entryAims[s_driveEntryAim];
                int32_t cx0 = ea.x0, cx1 = ea.x1, cy0 = ea.y0, cy1 = ea.y1;
                if (cx1 - cx0 > 768) { cx0 = ea.aimX - 384; cx1 = ea.aimX + 384; }
                if (cy1 - cy0 > 768) { cy0 = ea.aimY - 384; cy1 = ea.aimY + 384; }
                int n = 0; bool rev = (s_mowTried == 2);   // 2nd attempt mows the other way
                for (int32_t yy = cy0 + 48; yy <= cy1 - 16 && n < DRIVE_PATH_MAX - 1; yy += 96, rev = !rev) {
                    const int32_t xa = rev ? (cx1 - 32) : (cx0 + 32);
                    const int32_t xb = rev ? (cx0 + 32) : (cx1 - 32);
                    for (int half = 0; half < 2; half++) {
                        const int32_t wx = half ? xb : xa;
                        s_drivePathWX[n] = wx; s_drivePathWY[n] = yy;
                        int fc = WorldXToFineCol(wx), fr = WorldYToFineRow(yy);
                        if (fr < 0) fr = 0; else if (fr >= WM_FINE_ROWS) fr = WM_FINE_ROWS - 1;
                        if (fc < 0) fc = 0; else if (fc >= WM_FINE_COLS) fc = WM_FINE_COLS - 1;
                        s_drivePath[n] = PackSeg(fr, fc);
                        n++;
                    }
                }
                if (n >= 2) {
                    s_drivePathLen = n; s_drivePathIdx = 0;
                    s_drivePathPlanned = true; s_drivePathWorld = true;
                    s_finalApproachEnterTick = now;   // fresh window for the mow pass
                    Log::World("WorldMap: [ENTRYMOW] pass %d: mowing firing area, %d waypoints, box x[%d,%d] y[%d,%d]",
                               s_mowTried, n, cx0, cx1, cy0, cy1);
                    ScreenReader::Speak("Searching the entrance area.", true);
                    return;
                }
            }
            Log::World("WorldMap: [DRIVE] Final-approach timeout (%dms in zone, no entry)",
                       (int)(now - s_finalApproachEnterTick));
            StartSweep(px, py, now);
            return;
        }
    } else {
        s_finalApproachEnterTick = 0;
    }

    // Stuck detection.
    // v0.18.3.204 G3: SUPPRESSED while a CAMW-REC recovery episode is active. The .203 log
    // shows the generic stuck-check firing 10-second replans (identical routes) WHILE the
    // fan/wall-follow was mid-recovery, wasting ~30s per episode and yanking the route out
    // from under it. Recovery owns the situation; the F2/G3 route-based give-up (40s without
    // 128u of route progress) is the terminal backstop instead.
    if (s_camwRec != 0) {
        s_driveStuckX = px; s_driveStuckY = py; s_driveStuckCheckTime = now;
    } else
    if (now - s_driveStuckCheckTime >= DRIVE_STUCK_CHECK_INTERVAL_MS) {
        double moved = CalculateWrappedDistance(s_driveStuckX, s_driveStuckY, px, py);
        if (moved < DRIVE_STUCK_THRESHOLD) {
            s_driveStuckCount++;
            Log::World("WorldMap: [DRIVE] Stuck check %d/%d (moved %.0f units in %dms window)",
                       s_driveStuckCount, DRIVE_STUCK_MAX, moved, DRIVE_STUCK_CHECK_INTERVAL_MS);
            // #67 v0.18.3.72: REVERSE un-wedge on every mid-route stuck check,
            // bounded by the give-up counter -- NOT a separate 4-use budget. The
            // .71 BAT proved the separate MAX_WEDGE_REVERSE budget exhausted early
            // and NEVER reset across the encounter-fragmented drive (each random
            // encounter resumes him on the same neck), so after 4 bursts the
            // reverse stopped firing for the rest of the drive and the stuck check
            // fell through to a futile re-plan (identical route every time). Now:
            // while we still have give-up headroom (stuckCount < DRIVE_STUCK_MAX),
            // fire a DOWN-only reverse burst and let stuckCount keep climbing -- so
            // it fires on every check (~3s apart) and still gives up after
            // DRIVE_STUCK_MAX if reversing never helps. stuckCount resets on
            // resume, so each post-encounter segment gets a fresh set of attempts.
            // (Plan: DOWN backs him off the wall into open ground; once unblocked,
            // the normal PIVOT can rotate him to re-aim -- rotation appears to need
            // open space, which is why turning in place while pinned does nothing.)
            // #67 v0.18.3.74: on foot, the recurring "wedge" was an artifact of
            // steering against a frozen heading; screen-relative steering should
            // not re-create it. If he still stalls against geometry, SIDESTEP
            // (slide laterally past it, alternating sides) on the first stuck
            // window, then fall through to the mid-route re-plan. Vehicles keep
            // the DOWN reverse burst (they rotate-then-go; this rework is foot-only).
            // #67 v0.18.3.82: on-foot now uses the SAME reverse un-wedge as
            // vehicles. The .80 unify put on-foot STEERING on the vehicle
            // heading turn-then-go but left on-foot RECOVERY pointing at the
            // SIDESTEP, whose ONLY consumer lives in the now-disabled screen-
            // relative branch (if (false && isOnFoot)) -- so on foot had NO
            // working recovery at all: the .81 Dollet BAT logged "SIDESTEP
            // left" but pressed no key and stayed wedged at the cliff corner.
            // On the world map the controls are tank-style (UP walks the
            // facing; LEFT/RIGHT rotate, and rotation needs forward motion), so
            // a lateral sidestep can't free a nosed-in character anyway -- the
            // only move that works is REVERSE: DOWN backs him off the cliff into
            // open ground, where the heading PIVOT can finish rotating him to
            // the route bearing and walk him AROUND the corner. DOWN moves the
            // character on foot (Aaron-confirmed, .71). Same bound as vehicles:
            // fire each stuck check (~3s) while there's give-up headroom; the
            // progress watermark renews the budget once a reverse gets him
            // closer, else it escalates to re-plan / give-up below.
            if (!s_drivePathWorld && dist >= DRIVE_FINAL_APPROACH_DIST && s_driveStuckCount < DRIVE_STUCK_MAX) {
                // v0.18.3.165: reverse-burst recovery is for the OLD fine-cell path only. On the
                // native 128u path it fires constantly and shoves the character backward (screen-
                // relative DOWN), overriding the sequential+probe executor (the .164 run: keys all
                // -D--, dist GREW). The probe already finds a walkable direction every frame, and
                // genuine dead-ends fall through to the mid-route re-plan below, so skip it here.
                s_driveWedgeReverseUntil = now + REVERSE_BURST_MS;
                s_unwedgeSweep++;   // v0.18.3.151: next burst exits on a different heading
                Log::World("WorldMap: [DRIVE] Stuck -> reverse un-wedge burst (check %d/%d, dist=%.0f, DOWN-only off the wall%s)",
                           s_driveStuckCount, DRIVE_STUCK_MAX, dist, isOnFoot ? ", on foot" : "");
                s_driveStuckX         = px;
                s_driveStuckY         = py;
                s_driveStuckCheckTime = now;
                return;
            }
            if (isOnFoot && dist < DRIVE_FINAL_APPROACH_DIST && s_driveStuckCount >= 2) {
                // v0.18.3.182: FINAL-APPROACH ENTRY via position-write. The .181 run navigated
                // Dollet->Timber perfectly (1 teleport) and reached dist=67 -- the doorstep -- but
                // then stalled: at the route END there's no pursuit lookahead, so the native executor
                // orbits the fixed target, and the on-foot SWEEP freezes (it's heading-based and can't
                // steer screen-relative on-foot movement). The terrain to the target is open and the
                // target cell is walkable. Drive the last stretch by writing the character ONTO the
                // target -- proven to cross the town-entry trigger in .158 (that's how it entered
                // Dollet) -- grounded at the engine's true height (.178) so it isn't left floating.
                int eZ = 0; __try { eZ = *(volatile int32_t*)0x0203FE30; } __except (EXCEPTION_EXECUTE_HANDLER) {}
                int oZ = WorldGroundHeightLocal(s_driveTargetX, s_driveTargetY);
                int32_t tgZ = (oZ != WGH_NO_GROUND) ? oZ : ((eZ != 0) ? eZ : 0x7FFFFFFF);
                WriteWorldMapPosition(s_driveTargetX, s_driveTargetY, tgZ);
                PlayTeleportCue();
                Log::World("WorldMap: [DRIVE] final approach on foot -> position-write onto target (%d,%d) z=%d to cross town entry",
                           s_driveTargetX, s_driveTargetY, tgZ);
                s_driveStuckCount = 0; s_driveStuckX = s_driveTargetX; s_driveStuckY = s_driveTargetY; s_driveStuckCheckTime = now;
                return;
            }
            // #67 v0.18.3.59: mid-route stuck recovery. Before giving up, re-plan
            // from the player's CURRENT position -- a fresh clearance-weighted
            // route from where he actually is, which steers him out of a wall
            // pocket he drifted into rather than stranding him. Bounded by
            // DRIVE_MAX_REPLANS per drive so a true hard-jam still terminates.
            // Planner-routed mid-route drives only (final approach uses the sweep
            // above; simple-coord drives have no path to re-plan).
            // v0.18.3.170: PINCH ASSIST (native path). A mid-route stuck on the native 128u path
            // means the 8-way executor is orbiting a narrow ramp/canyon it can't thread (calibration
            // isn't exact enough in-game to hold the sub-cell line). Instead of re-planning the same
            // route, slide the character along the planner's gate-verified centerline through the
            // pinch, then resume native keys. Bounded to ~24 waypoints per burst so encounters are
            // only skipped through the pinch; if still pinched after, it re-triggers and advances
            // another burst, guaranteeing forward progress.
            // v0.18.3.185: PINCH TELEPORT DISABLED (Aaron's call). It fired far too often, became a
            // crutch, and the post-write stale-Z grounding cascaded it the WHOLE way to Timber. The
            // native fan-out recovery below (empirical unstick, now ordered toward the target) escapes
            // the oracle-invisible wedges on its own -- the .183 ->Dollet drive already arrived with
            // ZERO teleports, proving native recovery threads a full leg. On a hard stuck we now re-plan
            // (block just below) instead of teleporting; only the final-approach town-entry write
            // remains (it has to cross the entry trigger). Offline sim: blind 0-7 sweep completed
            // 5/18 harsh-wall legs, fan-out completed 13/18 harsh and 35/36 at realistic wall density.
            if (false && s_drivePathWorld && s_drivePathPlanned && s_drivePathLen > 0 &&
                dist >= DRIVE_FINAL_APPROACH_DIST &&
                s_driveStuckCount >= DRIVE_REPLAN_TRIGGER) {
                s_driveAssistActive = true;
                s_driveAssistEndIdx = s_drivePathIdx + 8;   // v0.18.3.171: SHORT burst -- nudge through the
                                                            // immediate pinch, then hand back so native does
                                                            // the open-terrain walking (and rolls encounters).
                if (s_driveAssistEndIdx > s_drivePathLen - 1) s_driveAssistEndIdx = s_drivePathLen - 1;
                int32_t snapX = s_drivePathWX[s_drivePathIdx], snapY = s_drivePathWY[s_drivePathIdx];
                // v0.18.3.184: ground the teleport at the ORACLE height, NOT the engine register.
                // 0x0203FE30 is STALE immediately after a position-write -- it still holds the PREVIOUS
                // cell's height until the engine re-settles the character by walking. The .183
                // Dollet->Timber log proved the .178 "trust the engine when it disagrees" rule is
                // backwards here: at the snap cell the stale engine read -786 while the oracle (which
                // matches the engine's height function EXACTLY during real walking, diff 0) read -1271,
                // so .178 grounded the character at -786 -- 485u ABOVE the true ground -> floating ->
                // the engine refuses key input -> d+0 frozen -> the pinch re-fires and teleports the
                // WHOLE way to Timber. (The .177 "overhang" that motivated .178 was itself a stale
                // engine read, not a real overlap.) Grounding at the oracle keeps the char on the
                // mesh so native keys resume after the burst. The engine register is only a last
                // resort when the oracle has no ground (ocean/void).
                int engZnow = 0; __try { engZnow = *(volatile int32_t*)0x0203FE30; } __except (EXCEPTION_EXECUTE_HANDLER) {}
                int oraZ = WorldGroundHeightLocal(snapX, snapY);
                s_driveTeleZ = 0x7FFFFFFF;   // burst grounds per-cell at the oracle (chA), never the stale engine Z
                int32_t snapZ = (oraZ != WGH_NO_GROUND) ? oraZ : ((engZnow != 0) ? engZnow : 0x7FFFFFFF);
                WriteWorldMapPosition(snapX, snapY, snapZ); // snap onto centerline + ground the char
                {
                    int engZ = 0, engH = 0;
                    __try { engZ = *(volatile int32_t*)WM_POS_Z; engH = *(volatile int32_t*)0x0203FE30; }
                    __except (EXCEPTION_EXECUTE_HANDLER) {}
                    Log::World("WorldMap: [TELEZ] snap(%d,%d) ourZ=%d engZ(before)=%d engH=%d",
                               snapX, snapY, snapZ, engZ, engH);
                }
                s_driveStuckCount = 0; s_driveStuckX = px; s_driveStuckY = py; s_driveStuckCheckTime = now;
                // v0.18.3.172: audio cue + post-teleport distance announcement for the blind player.
                PlayTeleportCue();
                {
                    double tdist = CalculateWrappedDistance(snapX, snapY, s_driveTargetX, s_driveTargetY);
                    int tkm = (int)(tdist / 1000.0);
                    char tbuf[160];
                    if (tkm < 1) snprintf(tbuf, sizeof(tbuf), "%s. Very close.", s_driveTargetName);
                    else         snprintf(tbuf, sizeof(tbuf), "%d kilometers to %s.", tkm, s_driveTargetName);
                    ScreenReader::Speak(tbuf, true);
                }
                Log::World("WorldMap: [DRIVE] pinch -> position-write assist along route centerline (idx %d -> %d)",
                           s_drivePathIdx, s_driveAssistEndIdx);
                return;
            }
            if (s_drivePlannerEligible && s_drivePathPlanned &&
                dist >= DRIVE_FINAL_APPROACH_DIST &&
                s_driveStuckCount >= DRIVE_REPLAN_TRIGGER &&
                s_driveReplanCount < DRIVE_MAX_REPLANS) {
                s_driveReplanCount++;
                Log::World("WorldMap: [DRIVE] Stuck mid-route -- re-planning from current position (recovery %d/%d)",
                           s_driveReplanCount, DRIVE_MAX_REPLANS);
#if NAVMESH_DIAG
                // v0.18.3.161: record the obstacle we jammed on (toward the next waypoint) so the
                // re-plan routes AROUND the real cliff the height-model-based planner stepped into.
                if (s_drivePathPlanned && s_drivePathLen > 0) {
                    int wbi = s_drivePathIdx + 1; if (wbi > s_drivePathLen - 1) wbi = s_drivePathLen - 1;
                    int32_t wbx, wby;
                    FineCellCenterToWorld(UnpackCol(s_drivePath[wbi]), UnpackRow(s_drivePath[wbi]), &wbx, &wby);
                    int32_t bdx = wbx - px, bdy = wby - py; WrapWorldDelta(bdx, bdy);
                    double bl = sqrt((double)bdx * bdx + (double)bdy * bdy);
                    if (bl > 1.0) {
                        AddNavBlock(px + (int32_t)(bdx / bl * 384.0), py + (int32_t)(bdy / bl * 384.0));
                        Log::World("WorldMap: [DRIVE] marked discovered obstacle ahead (nav-block #%d)", s_navBlkN);
                    }
                }
#endif
                PlanDrivePath(px, py);
                s_driveProbeValid     = false;   // #67 v0.18.3.77: re-probe on the new route
                s_driveStuckCount     = 0;
                s_driveStuckX         = px;
                s_driveStuckY         = py;
                s_driveStuckCheckTime = now;
                return;
            }
            if (s_driveStuckCount >= DRIVE_STUCK_MAX) {
                StopAutoDrive("Stuck. Cannot reach destination.");
                return;
            }
        } else {
            s_driveStuckCount = 0;
        }
        s_driveStuckX         = px;
        s_driveStuckY         = py;
        s_driveStuckCheckTime = now;
    }

    // #67 stage 2: the planner path is now FINE cells (1024-unit) routed around
    // mountains/ocean. Advance the path cursor forward to the cell nearest the
    // player (never backward), then steer toward a small lookahead further along
    // the path -- this hugs the walkable route around obstacles instead of
    // cutting a straight line at a far segment centre.
    // v0.18.3.179 DIAG: dump the FULL planned route once per (re)plan. The offline sim (with the
    // mod's exact mesh) finds a clean gate-legal route to Dollet that the executor threads with zero
    // teleports -- so either the in-game planner routes differently (onto the cliff) or the executor
    // can't follow the clean route. This dump settles it: compare the in-game waypoints to the
    // offline clean route.
    if (s_drivePathPlanned && s_drivePathWorld && s_drivePathLen > 0) {
        static int s_dbgLen = -1; static int32_t s_dbgW0 = 0x7FFFFFFF;
        if (s_drivePathLen != s_dbgLen || s_drivePathWX[0] != s_dbgW0) {
            s_dbgLen = s_drivePathLen; s_dbgW0 = s_drivePathWX[0];
            Log::World("WorldMap: [ROUTEDUMP] len=%d idx=%d", s_drivePathLen, s_drivePathIdx);
            for (int i = 0; i < s_drivePathLen; i += 4) {
                char b[220]; int p = 0;
                for (int j = i; j < i + 4 && j < s_drivePathLen; j++)
                    p += snprintf(b + p, sizeof(b) - p, "%d:(%d,%d) ", j, s_drivePathWX[j], s_drivePathWY[j]);
                Log::World("WorldMap: [ROUTEDUMP] %s", b);
            }
        }
    }
    if (s_drivePathPlanned && s_drivePathLen > 0) {
      if (s_drivePathWorld) {
        // v0.18.3.164: SEQUENTIAL advance -- only step the cursor forward once we've REACHED the
        // current 128u waypoint (within ~80u). The old .163 "advance while the next is closer"
        // rule mis-picked waypoints in winding canyons and steered the character across the wall
        // it was supposed to round (the Dollet->Timber jam). Offline sim: this clean sequential
        // follow threads the canyons (17/18 trio pairs).
        // v0.18.3.181: PURSUIT cursor advance. The .164 "advance only within 80u" left the character
        // ORBITING waypoints it couldn't converge on -- 8-way granularity (45deg) overshoots a close
        // fixed target, so it never got within 80u, dithered, and the stuck-check teleported (the .180
        // log: oscillating 124-208u from the waypoint, moving but never advancing). Instead, set the
        // cursor just AHEAD of the character's NEAREST route waypoint (never backward). The steer target
        // is then always a stable ~1-cell-ahead point that moves forward with the character -> the
        // bearing stays steady, the key stays held (no dither), and it flows THROUGH waypoints instead
        // of orbiting them.
        {
            // v0.18.3.183: search only a small window ahead, and ONLY advance when the character is
            // ACTUALLY near a forward waypoint. The .181 version had no distance guard and scanned the
            // whole route, so when the character was stuck the "nearest forward" was just the current
            // cursor -> the cursor ran 1/frame to the route END while the character stood still, and
            // then a single assist snapped it the WHOLE way (Dollet->Timber in one ~32000u teleport).
            int nj = s_drivePathIdx; double bd = 1e30;
            int jend = s_drivePathIdx + 6; if (jend > s_drivePathLen) jend = s_drivePathLen;
            for (int j = s_drivePathIdx; j < jend; j++) {
                double dj = CalculateWrappedDistance(px, py, s_drivePathWX[j], s_drivePathWY[j]);
                if (dj < bd) { bd = dj; nj = j; }
            }
            // v0.18.3.201: HEIGHT-AWARE waypoint advance (offline-sim proven, 24/24 pairs).
            // The 2D radius test alone marked a waypoint "reached" while the character stood
            // ~200u BELOW it (on a validated ramp flanked by a cliff); the cursor then aimed
            // past the ramp into the cliff face, where every heading was gate-blocked -- the
            // exact live "advanced then wedged" jam. Also require the character's ground
            // height to be within 100u of the waypoint's before advancing.
            // v0.18.3.202: advance radius 192 -> 64. The .201 BAT proved the engine hard-blocks
            // when a non-walkable poly is within ~112u AHEAD (offline/BAT201_ANALYSIS.md); a 192u
            // advance radius permits ~90u corner cuts off the validated polyline -- fatal in the
            // ~200u-wide corridors this map is full of. 64u keeps the pursuit line pinned to the
            // planned centerline the clearance-aware planner (.202) verified.
            // v0.18.3.208: ABEAM advance. The .207 neck grind: G4 centerline discipline
            // walks a line PARALLEL to the waypoint row (char held z~-25086, waypoints on
            // the -25024 row, ~62u lateral offset), so the 64u advance radius never
            // triggered even as the character walked PAST the waypoints -- the cursor
            // pinned and the stall machinery churned. Also advance when laterally near
            // (<=144u) AND the NEXT waypoint is already closer than the current one
            // (we're abeam or beyond it). Height gate applies to both paths.
            bool advNow = (bd <= 64.0);
            if (!advNow && bd <= 144.0 && nj < s_drivePathLen - 1) {
                double dnx = CalculateWrappedDistance(px, py, s_drivePathWX[nj + 1], s_drivePathWY[nj + 1]);
                if (dnx < bd) advNow = true;
            }
            if (advNow) {                      // only step the cursor forward when we've reached a waypoint
                int wpH = WorldGroundHeightLocal(s_drivePathWX[nj], s_drivePathWY[nj]);
                int chH = WorldGroundHeightLocal(px, py);
                bool hOk = (wpH == WGH_NO_GROUND || chH == WGH_NO_GROUND || abs(wpH - chH) < 100);
                if (hOk) {
                    int adv = nj + 1; if (adv > s_drivePathLen - 1) adv = s_drivePathLen - 1;
                    if (adv > s_drivePathIdx) s_drivePathIdx = adv;
                } else if (DRIVE_STEER_DIAG) {
                    static DWORD s_wpadvT = 0;
                    if (now - s_wpadvT >= 1000) {
                        Log::World("WorldMap: [WPADV] hold at wp %d: wpH=%d charH=%d (dh=%d) -- 2D-close but not reached vertically",
                                   nj, wpH, chH, wpH - chH);
                        s_wpadvT = now;
                    }
                }
            }
            // v0.18.3.202: F2 ROUTE-PROGRESS watchdog (REPLACES the .186 goalDist cursor skip).
            // The .201 BAT proved the goalDist skip is poison on horseshoe routes: G-Garden->
            // Dollet legitimately walks AWAY from Dollet for ~7km, so "goalDist not improving"
            // fired every 1.5s, raced the cursor +4 cells each time, and the character beelined
            // to skipped-ahead waypoints -- 86u off the centerline, into the north wall's 112u
            // probe cone, hard freeze. Progress is now measured ALONG THE ROUTE (distance to the
            // current waypoint + 128u per remaining cell). The cursor is NEVER skipped past
            // unreached waypoints; if route-progress stalls >=4s the leg is BLOCKED and the F3
            // recovery (fan-out / retreat / learned-block replan, in the steering block) takes it.
            if (s_driveEscapeActive) {
                // v0.18.3.222: the firing-area escape deliberately steers OFF the
                // route (toward the destination, around the area), so route
                // progress does not advance -- suppress the stall/give-up
                // watchdogs and let them reseed when the escape clears.
            } else {
                // v0.18.3.216: identity includes s_driveWatchdogGen so a battle/field
                // pause-resume reseeds the clock (stale-clock instant stall fix).
                DWORD rpKey = s_driveStartTime + s_driveWatchdogGen;
                static DWORD  s_rpT = 0; static double s_rpBest = 1e30; static DWORD s_rpDrive = 0;
                if (s_rpDrive != rpKey) { s_rpDrive = rpKey; s_rpT = 0; s_rpBest = 1e30; }
                double wpd = CalculateWrappedDistance(px, py, s_drivePathWX[s_drivePathIdx], s_drivePathWY[s_drivePathIdx]);
                double remaining = wpd + 128.0 * (double)(s_drivePathLen - 1 - s_drivePathIdx);
                if (remaining < s_rpBest - 64.0) {          // real route progress -> reset
                    s_rpBest = remaining; s_rpT = now;
                } else if (remaining > s_rpBest + 512.0) {  // re-plan / route change: re-seed, don't punish
                    s_rpBest = remaining; s_rpT = now;
                } else if (s_rpT != 0 && now - s_rpT >= 4000) {
                    s_camwRouteBlocked = true;              // consumed by the F3 recovery
                    s_rpT = now; s_rpBest = remaining;
                    if (DRIVE_STEER_DIAG)
                        Log::World("WorldMap: [DRIVE] route-progress stalled 4s (remaining=%.0f, idx=%d/%d) -> F3 recovery",
                                   remaining, s_drivePathIdx, s_drivePathLen);
                    // v0.18.3.207: STALL ESCALATION. The .206 Timber leg stalled 10 times in
                    // a row at the SAME cursor (idx 59, char oscillating ~119u from a waypoint
                    // it could never reach) -- the fan always escaped somewhere, so the fan-
                    // exhaust retreat/replan never fired, and only the 40s give-up ended it.
                    // Track consecutive stalls at one cursor: 2nd stall = the waypoint itself
                    // is unreachable -> skip the cursor past it (bounded, height gate waived,
                    // logged); 3rd stall = force the retreat->replan path (each stall already
                    // learned a block via G1, so the replan carries new knowledge).
                    static int s_rpStallIdx = -1; static int s_rpStallN = 0;
                    if (s_drivePathIdx == s_rpStallIdx) s_rpStallN++;
                    else { s_rpStallIdx = s_drivePathIdx; s_rpStallN = 1; }
                    if (s_rpStallN == 2) {
                        int adv = s_drivePathIdx + 1;
                        if (adv > s_drivePathLen - 1) adv = s_drivePathLen - 1;
                        s_drivePathIdx = adv;
                        Log::World("WorldMap: [WPSKIP] waypoint unreachable after 2 stalls -> cursor %d/%d",
                                   s_drivePathIdx, s_drivePathLen);
                    } else if (s_rpStallN >= 3) {
                        s_rpStallN = 0; s_rpStallIdx = -1;
                        s_camwRec = 3;
                        s_camwRetreatLeft = (s_camwCrumbN < 6) ? s_camwCrumbN : 6;
                        Log::World("WorldMap: [CAMW-REC] 3 stalls at one cursor -> forced retreat + replan");
                    }
                } else if (s_rpT == 0) {                    // first frame of a drive: seed
                    s_rpBest = remaining; s_rpT = now;
                }
                // v0.18.3.204 G3: leg give-up -- ONLY route-based (the generic stuck-stop is
                // suppressed while recovery runs). If route-remaining hasn't improved 128u in
                // ~40s despite recovery, this leg genuinely can't proceed from here.
                static DWORD  s_rpGiveT = 0; static double s_rpGiveBest = 1e30; static DWORD s_rpGiveDrive = 0;
                if (s_rpGiveDrive != rpKey) { s_rpGiveDrive = rpKey; s_rpGiveT = now; s_rpGiveBest = remaining; }  // v0.18.3.216: rpKey (see above)
                if (remaining < s_rpGiveBest - 128.0) { s_rpGiveBest = remaining; s_rpGiveT = now; }
                else if (remaining > s_rpGiveBest + 512.0) { s_rpGiveBest = remaining; s_rpGiveT = now; }  // replan jump
                else if (now - s_rpGiveT >= 40000) {
                    StopAutoDrive("Cannot reach the destination from here.");
                    return;
                }
            }
        }
      } else {
        int pfc = WorldXToFineCol(px);
        int pfr = WorldYToFineRow(py);
        while (s_drivePathIdx < s_drivePathLen - 1) {
            int cR = UnpackRow(s_drivePath[s_drivePathIdx]);
            int cC = UnpackCol(s_drivePath[s_drivePathIdx]);
            int nR = UnpackRow(s_drivePath[s_drivePathIdx + 1]);
            int nC = UnpackCol(s_drivePath[s_drivePathIdx + 1]);
            int dCur  = abs(pfr - cR) + abs(pfc - cC);
            int dNext = abs(pfr - nR) + abs(pfc - nC);
            if (dNext <= dCur) s_drivePathIdx++; else break;
        }
      }
    }

    // v0.18.3.170: PINCH ASSIST execution. While active, walk the character along the verified
    // route centerline one 32u step per frame toward the current waypoint (the cursor advance above
    // moves s_drivePathIdx forward as each waypoint is reached). Every step is collision-free by the
    // planner's own edge check, so this always clears the pinch. Hand back to native keys once we've
    // advanced past the jam (s_driveAssistEndIdx) or reached the path end.
    // v0.18.3.171: SETTLE handoff. After a position-write assist, give the engine a few frames with
    // NO input and NO writes so it re-acquires the character on the walkmesh and re-enables key
    // movement -- the .170 BAT froze on the first native frames after each assist (d+0 on every key,
    // even in open terrain), which is the engine not yet responding to keys after a direct position
    // write. Then native (with calibration resynced below) takes over.
    if (s_driveSettleFrames > 0) {
        s_driveSettleFrames--;
        ReleaseAllDriveKeys();
        s_driveStuckX = px; s_driveStuckY = py; s_driveStuckCheckTime = now;
        return;
    }
    if (s_driveAssistActive) {
        if (!s_drivePathWorld || !s_drivePathPlanned || s_drivePathLen <= 0 ||
            s_drivePathIdx >= s_driveAssistEndIdx || s_drivePathIdx >= s_drivePathLen - 1) {
            s_driveAssistActive = false;
            s_driveSettleFrames = 5;   // settle, then resync native (below) and walk
            s_driveNavResync    = true;
        } else {
            int32_t tx = s_drivePathWX[s_drivePathIdx], ty = s_drivePathWY[s_drivePathIdx];
            int32_t adx = tx - px, ady = ty - py; WrapWorldDelta(adx, ady);
            double al = sqrt((double)adx * adx + (double)ady * ady);
            int32_t cx = px, cy = py;
            if (al >= 1.0) { cx = px + (int32_t)(adx / al * 32.0); cy = py + (int32_t)(ady / al * 32.0); }
            int curHa = WorldGroundHeightLocal(px, py);
            int chA   = WorldGroundHeightLocal(cx, cy);
            if (chA != WGH_NO_GROUND && (curHa == WGH_NO_GROUND || abs(chA - curHa) < 200)) {
                int32_t bz = (s_driveTeleZ != 0x7FFFFFFF) ? s_driveTeleZ : chA;   // v0.18.3.178: engine-true Z at bad cells
                WriteWorldMapPosition(cx, cy, bz);    // write Z too, keep the char grounded
                ReleaseAllDriveKeys();
                s_driveStuckX = px; s_driveStuckY = py; s_driveStuckCheckTime = now; // keep stuck watermark fresh
                return;
            }
            s_driveAssistActive = false;   // centerline step unexpectedly blocked -> settle + native recover
            s_driveSettleFrames = 5;
            s_driveNavResync    = true;
        }
    }

    // #67 v0.18.3.63: steering target = a few cells AHEAD ALONG THE PATH, not the
    // first cell >=2400u away in a straight line. The straight-line target could
    // land across a canyon wall (the route winds), so the player jammed aiming at
    // it and the wall-slide spun him the wrong way (.62 BAT drove to Galbadia
    // Garden, away from Dollet). DRIVE_LOOKAHEAD_CELLS along the route keeps the
    // aim inside the walkable corridor; the path cursor above holds idx at the
    // cell nearest the player, so this target stays a few cells ahead of him and
    // re-points him back onto the route if he drifts. On final approach / path
    // end, steer to the real destination coordinate.
    // #67 v0.18.3.78: steering target = the first route cell at least
    // DRIVE_PLAN_LOOKAHEAD_DIST (~2400u) AHEAD along the path, not the immediately
    // adjacent cell. The .77 BAT proved the greedy probe steers correctly but
    // ORBITS: with a 1-cell (~1024u) target, every ~180u walk step swung the
    // target bearing ~15deg, so the probe re-decided every window and circled the
    // first waypoint on open grass (idx stuck at 0, tgtBrg swinging wildly, dist
    // pinned ~15.8km) instead of committing north and crossing into the open field
    // the F11 showed right in front of him. A ~2400u target swings only ~4deg per
    // step, so the bearing is stable and the probe locks onto the northward
    // cardinal and carries him up the corridor. This restores the OLD
    // DRIVE_PLAN_LOOKAHEAD_DIST rule (cut to 1 cell in .64 only because the THEN
    // heading-steering drove straight at a far target and jammed into walls -- the
    // probe doesn't, it rejects walled cardinals and staircases, so a far target
    // is now safe AND necessary to stop the orbit). On final approach / path end,
    // steer to the real destination coordinate.
    int32_t steerX = s_driveTargetX;
    int32_t steerY = s_driveTargetY;
    // v0.18.3.221: FIRING-AREA ESCAPE overrides all other steering. Drive
    // straight for the escape point until the character is clear of the area,
    // then hand back to the normal route (which had its leading in-area
    // waypoints skipped when the escape was armed).
    if (s_driveEscapeActive) {
        // v0.18.3.223: hold while the character is inside the steer-arm box OR the
        // straight line to the on-route target still crosses it -- i.e. until the
        // character has rounded the area and can head to the route unobstructed.
        int stillIn = 0;
        if (s_driveEscapeAreaIdx >= 0) {
            const EntryAimInfo& ea = s_entryAims[s_driveEscapeAreaIdx];
            double bx0 = ea.x0 - EA_STEER_ARM, by0 = ea.y0 - EA_STEER_ARM;
            double bx1 = ea.x1 + EA_STEER_ARM, by1 = ea.y1 + EA_STEER_ARM;
            bool inside = InPaddedArea(ea, px, py, EA_STEER_ARM);
            bool lineBlocked = SegCrossesBox(px, py, s_driveEscapeTgtX, s_driveEscapeTgtY,
                                             bx0, by0, bx1, by1);
            stillIn = (inside || lineBlocked) ? 1 : 0;
        }
        if (stillIn == 1) {
            // Route AROUND the area's box toward the captured on-route waypoint.
            EscapeSteerAround(s_driveEscapeAreaIdx, px, py,
                              s_driveEscapeTgtX, s_driveEscapeTgtY, &steerX, &steerY);
            static DWORD s_escLogT = 0;
            if (DRIVE_STEER_DIAG && now - s_escLogT >= 500) {
                Log::World("WorldMap: [ESCAPE] routing around %s: pos(%d,%d) -> steer(%d,%d) [tgt (%d,%d)]",
                           s_entryAims[s_driveEscapeAreaIdx].name, px, py, steerX, steerY,
                           s_driveEscapeTgtX, s_driveEscapeTgtY);
                s_escLogT = now;
            }
        } else {
            s_driveEscapeActive  = false;
            // v0.18.3.223: resume at the ON-ROUTE ESCAPE TARGET index, then the
            // nearest waypoint AT OR AHEAD of it. The .222 "nearest overall"
            // re-snap picked a waypoint BEHIND the character (Timber's route
            // hugs the area before curving to Yaulny, so its early waypoints sit
            // back toward Timber) -- steering there drove the character back in.
            if (s_drivePathPlanned && s_drivePathWorld && s_drivePathLen > 0) {
                int floor = (s_driveEscapeTgtIdx > s_drivePathIdx)
                            ? s_driveEscapeTgtIdx : s_drivePathIdx;
                if (floor > s_drivePathLen - 1) floor = s_drivePathLen - 1;
                int bestJ = floor; double bestD = 1e30;
                for (int j = floor; j < s_drivePathLen; j++) {
                    double dj = CalculateWrappedDistance(px, py, s_drivePathWX[j], s_drivePathWY[j]);
                    if (dj < bestD) { bestD = dj; bestJ = j; }
                }
                s_drivePathIdx = bestJ;
            }
            Log::World("WorldMap: [ESCAPE] cleared %s (wide berth) at (%d,%d) -- resuming route (idx=%d/%d, escTgtIdx=%d)",
                       (s_driveEscapeAreaIdx >= 0) ? s_entryAims[s_driveEscapeAreaIdx].name : "?",
                       px, py, s_drivePathIdx, s_drivePathLen, s_driveEscapeTgtIdx);
            s_driveEscapeAreaIdx = -1;
        }
    }
    if (s_driveEscapeActive) {
        // escape steering set above; skip normal target/path selection
    } else if (s_driveBridgeActive) {
        // #70 v0.18.3.97: steer at the road cell, not the dead-end pocket route.
        steerX = s_driveBridgeX;
        steerY = s_driveBridgeY;
    } else if (s_drivePathPlanned && s_drivePathLen > 0 &&
        s_drivePathIdx < s_drivePathLen - 1 &&
        dist >= DRIVE_FINAL_APPROACH_DIST) {
        int wi = s_drivePathIdx;
        // #68 v0.18.3.100: CORNER-CAP the forward lookahead at a sharp SUSTAINED
        // bend. The route winds; the ~2400u lookahead on one leg lands on the
        // NEXT leg, so the straight chord cuts the inside of the corner and pins
        // the character against the canyon wall the route goes around. The .99
        // planner fix put him on a real 61-cell road route, then he wedged at
        // idx 5/61 exactly this way (an L: long WEST leg, then a 1-cell-wide
        // NORTH canyon; the lookahead aimed up the north leg while he was still
        // on the west leg -> he cut the corner, pinned against the east wall,
        // and the steer target whipsawed between the corner cell and an up-leg
        // cell). Stopping the lookahead at the leg's apex makes him aim AT the
        // corner, walk the leg into it, then turn up the next leg from inside
        // the corridor -- no cut, no pin. Straight/open legs keep the far
        // lookahead (stable bearing, no orbit -- preserves .78); a staircase
        // (alternating 1-cell steps) is NOT a sustained turn so it also keeps
        // the far target. Foot + vehicle both benefit (neither should cut a
        // corner into a wall).
        int legDR = 0, legDC = 0;
        if (s_drivePathIdx < s_drivePathLen - 1) {
            int dr0 = UnpackRow(s_drivePath[s_drivePathIdx + 1]) - UnpackRow(s_drivePath[s_drivePathIdx]);
            int dc0 = UnpackCol(s_drivePath[s_drivePathIdx + 1]) - UnpackCol(s_drivePath[s_drivePathIdx]);
            legDR = (dr0 > 0) - (dr0 < 0);
            legDC = (dc0 > 0) - (dc0 < 0);
        }
        for (int j = s_drivePathIdx; j <= s_drivePathLen - 1; ++j) {
            int32_t cx, cy;
            FineCellCenterToWorld(UnpackCol(s_drivePath[j]), UnpackRow(s_drivePath[j]), &cx, &cy);
            int32_t ddx = cx - px, ddy = cy - py;
            WrapWorldDelta(ddx, ddy);
            wi = j;
            // corner cap: if the path bends away from the current leg direction
            // when leaving cell j, and the new direction PERSISTS (sustained,
            // not a staircase), j is the leg's apex -- aim there and stop.
            if (j > s_drivePathIdx && j + 1 <= s_drivePathLen - 1) {
                int sdr = UnpackRow(s_drivePath[j + 1]) - UnpackRow(s_drivePath[j]);
                int sdc = UnpackCol(s_drivePath[j + 1]) - UnpackCol(s_drivePath[j]);
                int sDR = (sdr > 0) - (sdr < 0);
                int sDC = (sdc > 0) - (sdc < 0);
                if (sDR != legDR || sDC != legDC) {
                    bool sustained = true;
                    if (j + 2 <= s_drivePathLen - 1) {
                        int s2dr = UnpackRow(s_drivePath[j + 2]) - UnpackRow(s_drivePath[j + 1]);
                        int s2dc = UnpackCol(s_drivePath[j + 2]) - UnpackCol(s_drivePath[j + 1]);
                        sustained = (((s2dr > 0) - (s2dr < 0)) == sDR &&
                                     ((s2dc > 0) - (s2dc < 0)) == sDC);
                    }
                    if (sustained) break;   // wi = j is the corner apex
                }
            }
            if ((double)ddx * ddx + (double)ddy * ddy >=
                DRIVE_PLAN_LOOKAHEAD_DIST * DRIVE_PLAN_LOOKAHEAD_DIST)
                break;
        }
        // #67 v0.18.3.82: clamp the far lookahead target back to the farthest
        // route cell reachable from the player by a CLEAR straight line. The
        // route WINDS around cliffs, so a ~2400u-ahead target can sit on the far
        // side of a cliff corner the route goes around; aiming straight at it
        // drives the character INTO the cliff. The .81 Dollet BAT wedged exactly
        // this way at fine(103,64): the steer target was NNW across the blocked
        // fine(103,63) cliff, so he pressed north into the rock, and on the
        // world map's tank controls rotation needs forward motion -- nosed into
        // terrain he could neither advance nor pivot out (hdg frozen at 36).
        // Walking wi back to a clear-LOS cell makes him aim ALONG the open
        // corridor (here due WEST, around the cliff) and turn there in open
        // ground before reaching the rock. Engages ONLY near walls; on open
        // ground the far line is already clear so the far target stands
        // (stable bearing, no orbit -- preserves the .78 fix). The route cursor
        // holds s_drivePathIdx on the cell nearest the player, whose line is
        // trivially clear, so the clamp always terminates with a valid target.
        // #70 v0.18.3.109: BYPASS the coarse-grid LOS clamp on a navmesh funnel
        // path. The funnel legs are navmesh-walkable by construction, but the
        // corridor legitimately crosses fine cells the COARSE 1024u grid marks
        // blocked (the .81 Dollet-coast box, false-coast steep-mountain), and
        // FineLineClearFootCar reads that grid -- so on the navmesh path it walked
        // the steer target back to the player's own cell (.108 BAT: idx pinned
        // 0/30, steer target ~1 cell ahead, player on open grass oscillating; the
        // navmesh said walkable, the coarse grid said blocked, the clamp trusted
        // the coarse grid). The corner-cap above keeps the aim inside the corridor
        // on a navmesh path, so the clamp is both redundant and harmful there.
        if (!s_driveNavmeshPath) {
            // #70 v0.18.3.129: FLOOR the LOS-clamp walkback at idx+1 so the steer
            // target can never collapse onto the player's OWN cell. On the .128
            // breakthrough run the drive followed the road ~15km to ~5km from
            // Dollet, then a region-entry re-plan dropped a fresh navmesh route
            // whose every forward straight-line crosses the .81 false-coast box
            // (cols 104-111 rows 59-69, forced steep-mountain): FineLineClearFootCar
            // marked them all blocked, so this clamp walked wi all the way back to
            // s_drivePathIdx -- the player's CURRENT cell -- leaving the steer
            // target ~12u away. targetBearing then went to noise and the 8-way
            // sector flipped (-D-R/--L-/-DL-) at idx 0/8 until a random battle
            // paused the drive. The NEXT path cell (idx+1) is navmesh-adjacent by
            // construction, so steering AT it is always walkable even when the
            // COARSE grid marks the straight line blocked; flooring there keeps the
            // drive staircasing forward through the false-coast cells into Dollet
            // instead of oscillating in place. The .110 funnel-corner protection is
            // intact for every cell beyond idx+1 -- the clamp still walks far
            // targets back to the nearest clear cell, it just never lands on the
            // player himself (which steers nowhere).
            int wiFloor = s_drivePathIdx + 1;
            if (wiFloor > s_drivePathLen - 1) wiFloor = s_drivePathLen - 1;
            int pfc = WorldXToFineCol(px), pfr = WorldYToFineRow(py);
            while (wi > wiFloor &&
                   !FineLineClearFootCar(pfc, pfr,
                                         UnpackCol(s_drivePath[wi]),
                                         UnpackRow(s_drivePath[wi]))) {
                wi--;
            }
        }
        // v0.18.3.155: STABLE SEQUENTIAL follow (offline-sim proven). Override the far
        // ~2400u lookahead / corner-cap / LOS-clamp 'wi' computed above. With the .154
        // direct-heading write working, that far selector OSCILLATED between adjacent
        // corridor cells (BAT: steer whipsawed (-29184,-26112)<->(-29184,-27136), idx
        // pinned 0/28, the character limit-cycled and never advanced). Each A* path cell
        // is navmesh-adjacent to the next, so steering AT the next cell (idx+1) and letting
        // the path cursor advance walks the validated polyline with no corner-cut and no
        // oscillation. Sim: this carries the char the full ~20km from the stuck start to
        // Dollet's doorstep, vs ~700u net for the oscillating selector.
        wi = s_drivePathIdx + 1;
        if (wi > s_drivePathLen - 1) wi = s_drivePathLen - 1;
        if (s_drivePathWorld) {
            // v0.18.3.164: aim at the CURRENT 128u waypoint (sequential follow; the advance loop
            // above moves the cursor on once we reach it). Hugs the validated corridor exactly.
            steerX = s_drivePathWX[s_drivePathIdx]; steerY = s_drivePathWY[s_drivePathIdx];
        } else {
            FineCellCenterToWorld(UnpackCol(s_drivePath[wi]), UnpackRow(s_drivePath[wi]),
                                  &steerX, &steerY);
        }
    }

    // #67 v0.18.3.66: CLOSED-LOOP steering on the REAL facing. The .65 F12
    // heading scan PROVED WM_HEADING (0x0203ED02, what GetWorldMapHeading reads)
    // IS the live facing -- holding RIGHT raised it ~80/sample (clockwise), LEFT
    // lowered it, in the SAME compass units as TorusBearing (0 = North,
    // clockwise). The earlier "frozen" reads were the character not rotating
    // while jammed forward into terrain, NOT a dead sensor. So read the real
    // heading and TURN-THEN-GO: when off-aim, PIVOT with turn-only (no forward,
    // so the engine rotates him freely and the heading updates -- exactly the
    // scan's turn-only phase); once aligned (within STEER_DEADZONE), drive
    // forward. A true heading lets the pivot stop the instant we're aligned --
    // no oscillation, no wall-slide corruption, no motion-derivation, and no
    // arc-into-wall (holding UP into terrain is what locked the old rotation).
    int targetBearing = TorusBearing(px, py, steerX, steerY);
    int err = ((int)targetBearing - (int)heading) & 0xFFF;
    if (err > 2048) err -= 4096;             // signed [-2048,2048], 0 = dead ahead
    int off = err < 0 ? -err : err;

    // Wall-jam watchdog: if he stops moving while aimed (nosed into terrain),
    // force a pivot to find open ground.
    if (CalculateWrappedDistance(s_driveLastMovePosX, s_driveLastMovePosY, px, py) >= WALL_SLIDE_EPS) {
        s_driveLastMoveTime = now;
        s_driveLastMovePosX = px;
        s_driveLastMovePosY = py;
    }
    bool wallJam = (now - s_driveLastMoveTime) > WALL_SLIDE_MS;

    bool wantUp = false, wantLeft = false, wantRight = false, wantDown = false;
    bool fwdGuard = false;   // #67 v0.18.3.83: forward-collision guard fired this tick

    // #67 v0.18.3.68: REVERSE un-wedge. On the world map the character only
    // rotates WHILE moving forward, so once he noses into terrain he can't move,
    // can't turn, and his heading freezes (the recurring 15km coastal-corner
    // jam: hdg stuck at 501, pressing forward into the same wall forever). The
    // one move always available is BACKWARD -- reversing off the wall regains
    // open ground and motion, and turning toward the target while reversing
    // brings him out re-aimed. Fires on a HARD wedge (no real movement for
    // HARD_WEDGE_MS, longer than the WALL_SLIDE_MS slide window), as a bounded
    // burst; the progress watermark above renews the per-episode budget the
    // moment a reverse actually gets him closer, so a true hard-jam still falls
    // through to stuck detection / re-plan / give-up rather than reversing forever.
    // #67 v0.18.3.69: wedge = no NET travel, not no movement. The .68 BAT showed
    // the jam is a wall-VIBRATION -- the player bounces ~60u east-west every frame
    // (per-frame d+ 30-62) while netting ~zero progress, which kept resetting the
    // move-timer so the .68 hard-wedge (keyed on no-movement) NEVER fired and the
    // reverse never triggered. Key it on NET displacement from an anchor: the
    // anchor only resets when he genuinely relocates WEDGE_NET_EPS, so vibration-
    // in-place still counts as wedged once HARD_WEDGE_MS elapses.
    if (CalculateWrappedDistance(s_driveWedgeAnchorX, s_driveWedgeAnchorY, px, py) >= WEDGE_NET_EPS) {
        s_driveWedgeAnchorX    = px;
        s_driveWedgeAnchorY    = py;
        s_driveWedgeAnchorTime = now;
    }
    bool inReverseBurst = (s_driveWedgeReverseUntil != 0 && now < s_driveWedgeReverseUntil);
    if (!inReverseBurst && s_driveWedgeReverseUntil != 0) {
        s_driveWedgeReverseUntil = 0;        // burst ended; re-anchor for a fresh window
        s_driveWedgeAnchorX      = px;
        s_driveWedgeAnchorY      = py;
        s_driveWedgeAnchorTime   = now;
    }
    bool hardWedge = (now - s_driveWedgeAnchorTime) > HARD_WEDGE_MS;


// v0.18.3.225: UpdateAutoDrive body continues in world_map_drive_exec.inl
// (split to keep each .inl under the 80 KB CI size guard; textual include,
//  byte-identical to the pre-split single function).
#include "world_map_drive_exec.inl"
