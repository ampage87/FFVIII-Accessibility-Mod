// world_map_drive_exec.inl - second half of UpdateAutoDrive (v0.18.3.225 split)
// Textually included MID-FUNCTION from world_map_drive.inl; not a standalone
// unit. Holds the recovery/steering tail through the final SetDriveKeys and
// the closing brace of UpdateAutoDrive. Do not include anywhere else.
    // #67 v0.18.3.80: on-foot UNIFIED onto the heading-based turn-then-go (the
    // vehicle branch below). The v0.18.3.79 [YAWPROBE] BAT proved 0x0203ED02 IS
    // the LIVE on-foot facing -- NOT frozen, NOT screen-relative: holding UP walks
    // a straight line whose COMPASS bearing == the yaw (matched within 1deg at two
    // different yaws in one run: yaw 316deg -> walked 315deg, yaw 355deg -> 356deg),
    // holding UP does NOT rotate the camera (yaw steady through each forward burst),
    // and RIGHT INCREASES the yaw (clockwise). So on foot desiredYaw == the target's
    // TorusBearing and the vehicle law err=TorusBearing-heading / turn-then-go
    // applies verbatim with ZERO offset. The .74-.78 screen-basis + greedy-probe
    // detour was built on the now-disproven "frozen heading" reading; it is bypassed
    // (gate forced false; left in place as dead code like the retired calibration)
    // so EVERYONE uses the heading steering below.
    if (false && isOnFoot) {
        // ===== #67 v0.18.3.74: SCREEN-RELATIVE self-calibrating steering =====
        // On foot, arrows WALK screen-relative and WM_HEADING is frozen (.73
        // diagnostic). Measure the screen->world basis from the character's own
        // motion, then press the arrow combo whose screen direction points at the
        // target. No heading, no pivot, no camera control.
        //
        // (A) Calibrate the basis once per drive: UP -> world delta = screen-up
        // vector; RIGHT -> screen-right vector. Returns each tick until done.
        if (s_driveCalPhase != DCAL_DONE) {
            DWORD el = now - s_driveCalStart;
            switch (s_driveCalPhase) {
                case DCAL_PROBE_UP:
                    SetDriveKeys(true, false, false, false);
                    if (el >= DRIVE_CAL_PROBE_MS) {
                        int32_t cdx = px - s_driveCalX, cdy = py - s_driveCalY;
                        WrapWorldDelta(cdx, cdy);
                        double cl = sqrt((double)cdx * cdx + (double)cdy * cdy);
                        if (cl >= DRIVE_CAL_MIN_MOVE) {
                            s_camUx = cdx / cl; s_camUy = cdy / cl;
                            Log::World("WorldMap: [SRDIAG] CAL UP -> uHat=(%.3f,%.3f) from d(%d,%d) len=%.0f",
                                       s_camUx, s_camUy, cdx, cdy, cl);
                            s_driveCalPhase = DCAL_SETTLE_UR; s_driveCalStart = now; s_driveCalTry = 0;
                            SetDriveKeys(false, false, false, false);
                        } else if (++s_driveCalTry >= DRIVE_CAL_MAX_TRY) {
                            Log::World("WorldMap: [SRDIAG] CAL UP no move (len=%.0f); keeping uHat default (%.3f,%.3f)",
                                       cl, s_camUx, s_camUy);
                            s_driveCalPhase = DCAL_SETTLE_UR; s_driveCalStart = now; s_driveCalTry = 0;
                            SetDriveKeys(false, false, false, false);
                        } else {
                            s_driveCalX = px; s_driveCalY = py; s_driveCalStart = now;
                        }
                    }
                    return;
                case DCAL_SETTLE_UR:
                    SetDriveKeys(false, false, false, false);
                    if (el >= DRIVE_CAL_SETTLE_MS) {
                        s_driveCalPhase = DCAL_PROBE_RIGHT; s_driveCalStart = now;
                        s_driveCalX = px; s_driveCalY = py; s_driveCalTry = 0;
                    }
                    return;
                case DCAL_PROBE_RIGHT:
                    SetDriveKeys(false, false, true, false);
                    if (el >= DRIVE_CAL_PROBE_MS) {
                        int32_t cdx = px - s_driveCalX, cdy = py - s_driveCalY;
                        WrapWorldDelta(cdx, cdy);
                        double cl = sqrt((double)cdx * cdx + (double)cdy * cdy);
                        if (cl >= DRIVE_CAL_MIN_MOVE) {
                            s_camRx = cdx / cl; s_camRy = cdy / cl;
                            Log::World("WorldMap: [SRDIAG] CAL RIGHT -> rHat=(%.3f,%.3f) from d(%d,%d) len=%.0f | basis ready",
                                       s_camRx, s_camRy, cdx, cdy, cl);
                            s_camBasisValid = true; s_driveCalPhase = DCAL_DONE;
                            SetDriveKeys(false, false, false, false);
                        } else if (++s_driveCalTry >= DRIVE_CAL_MAX_TRY) {
                            s_camRx = -s_camUy; s_camRy = s_camUx;   // perpendicular fallback; live-refresh fixes handedness
                            Log::World("WorldMap: [SRDIAG] CAL RIGHT no move (len=%.0f); derived rHat=(%.3f,%.3f) from uHat",
                                       cl, s_camRx, s_camRy);
                            s_camBasisValid = true; s_driveCalPhase = DCAL_DONE;
                            SetDriveKeys(false, false, false, false);
                        } else {
                            s_driveCalX = px; s_driveCalY = py; s_driveCalStart = now;
                        }
                    }
                    return;
                default:
                    s_driveCalPhase = DCAL_DONE;
                    break;
            }
        }

        // ===== #67 v0.18.3.77: GREEDY EMPIRICAL ARROW PROBE =====
        // PIVOT off the maintained screen->world basis. The .74/.75/.76 BATs all
        // sawed east-west at the route corner: a predicted basis is unreliable
        // exactly where the camera swings, and a 2-key diagonal (U-L-) drove him
        // WEST when the basis said NNE. This trusts ONLY measured progress: hold
        // ONE cardinal arrow for a window, measure whether it moved him toward the
        // steer target; KEEP it if the motion both happened (>=MIN_MOVE) and lined
        // up with the target (align>=ALIGN_MIN), else ROTATE to the next cardinal.
        // No basis, no trig, no diagonals -- can't be fooled by camera swing or
        // wall-slide. Single-arrow motion is clean; only the basis + 2-key combo
        // were the problem. Around the corner this naturally STAIRCASES: it
        // rejects the walled direction (<MIN_MOVE) and takes the open cardinal
        // that actually progresses toward the next route cell.
        bool inSidestep = (s_driveSidestepUntil != 0 && now < s_driveSidestepUntil);

        int32_t tdx = steerX - px, tdy = steerY - py;
        WrapWorldDelta(tdx, tdy);
        double tl = sqrt((double)tdx * tdx + (double)tdy * tdy);
        double thx = (tl >= 1.0) ? tdx / tl : 0.0;
        double thy = (tl >= 1.0) ? tdy / tl : 0.0;

        if (inSidestep) {
            // keep the probe window fresh so it re-evaluates cleanly once the
            // lateral slide ends
            s_driveProbeAnchorX = px; s_driveProbeAnchorY = py; s_driveProbeTime = now;
        } else if (!s_driveProbeValid) {
            s_driveProbeValid   = true;
            s_driveProbeArrow   = 0;                                // start by trying UP
            s_driveProbeAnchorX = px; s_driveProbeAnchorY = py;
            s_driveProbeTime    = now; s_driveProbeFails = 0;
        } else if (now - s_driveProbeTime >= DRIVE_PROBE_WINDOW_MS) {
            int32_t wdx = px - s_driveProbeAnchorX, wdy = py - s_driveProbeAnchorY;
            WrapWorldDelta(wdx, wdy);
            double disp  = sqrt((double)wdx * wdx + (double)wdy * wdy);
            double along = (disp >= 1.0) ? (wdx * thx + wdy * thy) / disp : 0.0;  // alignment with target dir
            bool good = (disp >= DRIVE_PROBE_MIN_MOVE) && (along >= DRIVE_PROBE_ALIGN_MIN);
            if (good) {
                s_driveProbeFails = 0;                                 // committed arrow is working -- keep it
            } else {
                s_driveProbeArrow = (s_driveProbeArrow + 1) & 3;       // rotate UP->RIGHT->DOWN->LEFT
                if (++s_driveProbeFails >= DRIVE_PROBE_MAX_FAILS) {     // no cardinal progresses: genuine pocket
                    s_driveProbeFails = 0;
                    if (s_drivePlannerEligible && s_drivePathPlanned &&
                        s_driveReplanCount < DRIVE_MAX_REPLANS) {
                        s_driveReplanCount++;
                        Log::World("WorldMap: [DRIVE] Probe found no progressing arrow -- re-planning (recovery %d/%d)",
                                   s_driveReplanCount, DRIVE_MAX_REPLANS);
                        PlanDrivePath(px, py);
                        s_driveProbeValid = false;
                        return;
                    }
                }
            }
            s_driveProbeAnchorX = px; s_driveProbeAnchorY = py; s_driveProbeTime = now;
        }

        if (inSidestep) {
            wantRight = (s_driveSidestepSign > 0);
            wantLeft  = (s_driveSidestepSign < 0);
        } else if (tl >= 1.0) {
            switch (s_driveProbeArrow) {
                case 0: wantUp    = true; break;
                case 1: wantRight = true; break;
                case 2: wantDown  = true; break;
                case 3: wantLeft  = true; break;
            }
        }

        if (DRIVE_STEER_DIAG) {
            static DWORD   s_srLast  = 0;
            static int32_t s_srLastX = 0, s_srLastY = 0;
            if (now - s_srLast >= (DWORD)DRIVE_STEER_DIAG_INTERVAL_MS) {
                double dmoved = CalculateWrappedDistance(s_srLastX, s_srLastY, px, py);
                const char* an = (s_driveProbeArrow == 0) ? "UP"
                               : (s_driveProbeArrow == 1) ? "RIGHT"
                               : (s_driveProbeArrow == 2) ? "DOWN" : "LEFT";
                Log::World("WorldMap: [SRDIAG] pos(%d,%d) d+%.0f | tgtBrg=%d arrow=%s fails=%d | dist=%.0f idx=%d/%d keys=%s%s%s%s%s",
                           px, py, dmoved, targetBearing, an, s_driveProbeFails,
                           dist, s_drivePathIdx, s_drivePathLen,
                           wantUp ? "U" : "-", wantDown ? "D" : "-",
                           wantLeft ? "L" : "-", wantRight ? "R" : "-",
                           inSidestep ? " SIDESTEP" : "");
                s_srLast = now; s_srLastX = px; s_srLastY = py;
            }
        }
    } else if (isOnFoot) {
        // ===== #67 v0.18.3.87: YAW-BASED SCREEN-RELATIVE 8-WAY STEERING =====
        // On foot, hdg (0x0203ED02) is the FIXED per-region CAMERA YAW, not a
        // steerable facing: .79's [YAWPROBE] proved holding UP walks a straight
        // line at the yaw bearing (within 1deg) and RIGHT walks screen-right
        // (yaw +90 CW). So the arrows are screen-relative WALK keys, and the
        // .80-.86 heading turn-then-go CANNOT steer here -- it presses pure UP
        // (= the yaw direction). On the .86 road BAT that was fatal: routing was
        // SOLVED (Squall stood ON the road, route correct up the road), but the
        // road ran due north while the yaw pointed NNW, so UP walked him ~28deg
        // off the thin road into the cliff beside it, every time (he could not
        // press the diagonal needed to track the road). FIX: steer in SCREEN
        // space. screenAngle = targetBearing - yaw is the target's direction
        // relative to screen-up (what UP walks); press the nearest of 8 arrow
        // combos (cardinals + diagonals) so he walks toward the target and
        // STAIRCASES along the road's bends. No turn-then-go, no pivot (a fixed
        // camera can't be rotated), no basis/probe. The reverse-burst un-wedge
        // (DOWN = screen-down, backs him off a wall) is kept for stuck recovery.
        if (inReverseBurst) {
            wantDown = true;
            // v0.18.3.151: sweep the exit heading. DOWN backs off the wall (proven
            // ~32u); adding an alternating turn rotates the exit so repeated bursts
            // don't re-enter the same pocket. m==0 straight, m==1 left, m==2 right.
            int m = s_unwedgeSweep % 3;
            if (m == 1) wantLeft = true;
            else if (m == 2) wantRight = true;
            // v0.18.3.201: reverse motion must not feed the camera-write trim estimator
            // (a DOWN burst measures ~180deg off the aim and could trigger a wrong flip).
            s_camwHadPrev = false;
        } else if (DRIVE_CAMWRITE) {
            // ===== #70 v0.18.3.201: CAMERA-WRITE STEERING (exe-verified; sim 24/24) =====
            // offline/CAMERA_EXE_ANALYSIS.md: on foot the engine recomputes the move heading
            // INSIDE 0x557A90 every tick as heading = camYaw(0x0203ED02) + key*512 +
            // triBias(0x020409EC)/2, BEFORE movement applies it -- which is why writing the
            // heading register never stuck while a key was held (.156), and why 8-way keys
            // could only ever aim in 45-degree steps of a camera we didn't control (the whole
            // .159-.200 calibration/probe/commitment tower fought that quantization). Control
            // the CAMERA instead: each frame write camYaw = bearing-to-waypoint - bias/2
            // (+ closed-loop trim), zero the camera-follow velocity WM_CAM_VEL so the engine's
            // follow physics can't drift the yaw between writes (its integrator moves yaw by
            // vel>>3 per frame), and hold plain UP. The ENGINE then aims the character itself
            // (snap within 0x100, else 0x200/frame -- at most 4 frames to any bearing), and all
            // native collision, wall-slide, step counting, and random encounters run untouched.
            // Bonus for a blind player: the camera now always faces the direction of travel, so
            // the audio/screen space stays consistent with motion.
            // Region-locked cameras (WM_CAM_LOCK==1: an arrival controller drags yaw toward
            // WM_CAM_FORCED at +-0x20/frame) are cleared once per drive and restored on stop.
            // Scripted keyframe cameras (WM_SCRIPT_CAM_PTR!=0, cinematics) pause the writes.
            // Offline (offline/nav_sim.py, faithful engine model incl. camera velocity physics,
            // turn rate, step gate, wall slide): all 24 validation pairs arrive; robust to a
            // 180deg-wrong initial yaw, a forced-yaw region lock, and triangle bias +-64
            // (offline/SIM_CAMERA_RESULTS.md). The old 8-way executor spent 77-82% of frames
            // in the turn loop on the same routes; this spends 2-3%.
            if (ReadMemDword32(WM_SCRIPT_CAM_PTR) != 0) {
                wantUp = true;   // cinematic owns the camera: keep walking, write nothing
            } else {
                int camwBias  = (int)(int16_t)ReadMemWord16(WM_TRI_BIAS);
                // ---- v0.18.3.202: F3 ENGINE-BLOCK RECOVERY (state machine; states declared
                // above StopAutoDrive). Produces camwAim, the bearing actually steered this
                // frame (normally the waypoint bearing). Escapes are validated ONLY by real
                // measured motion; obstacles that defeat the fan are LEARNED (AddNavBlock)
                // so the next re-plan routes around them -- no more sterile identical replans.
                // Freeze detector (v0.18.3.204: window 20 -> 40 frames per BAT203 sect. 6
                // item 3): <8u net displacement over 40 frames while UP is held = the
                // engine's hard block (no wall slide, d+0).
                bool camwBlockNow = false;
                {
                    int32_t fdx = px - s_camwFrzX, fdy = py - s_camwFrzY;
                    WrapWorldDelta(fdx, fdy);
                    if ((double)fdx * fdx + (double)fdy * fdy >= 64.0) {
                        s_camwFrzX = px; s_camwFrzY = py; s_camwFrzTicks = 0;
                    } else if (++s_camwFrzTicks >= 40 && s_camwRec == 0) {
                        camwBlockNow = true;
                        s_camwFrzTicks = 0;
                    }
                }
                if (s_camwRouteBlocked) {
                    s_camwRouteBlocked = false;
                    if (s_camwRec == 0) camwBlockNow = true;   // route stalled without a hard freeze (slide-orbit)
                }
                if (camwBlockNow) {
                    // v0.18.3.204 G1: learn on EVERY block (the .203 grind learned almost
                    // nothing because learning waited for fan-exhaust + an oracle veto).
                    s_camwFreezeN++;
                    if ((GetTickCount() - s_camwResumeT) > 2000 || s_camwResumeT == 0) s_camwLearnEp = 0;
                    CamwLearnBlock(px, py, targetBearing);
                    if (s_camwResumeT != 0 && (GetTickCount() - s_camwResumeT) <= 2000) {
                        // v0.18.3.204 G2: re-blocked within 2s of resuming -- the wall is
                        // still there. Go straight back to wall-follow on the SAME side
                        // (no re-fan) with DOUBLED commitment (cap 512u).
                        s_camwWfCommit *= 2; if (s_camwWfCommit > 512) s_camwWfCommit = 512;
                        s_camwRec = 2; s_camwWallTicks = 0;
                        s_camwWfStartX = px; s_camwWfStartY = py; s_camwWfClearRun = 0;
                        Log::World("WorldMap: [CAMW-REC] quick re-block #%d -> wall-follow same side, commit %d",
                                   s_camwFreezeN, s_camwWfCommit);
                    } else {
                        s_camwWfCommit = 64;
                        s_camwRec = 1; s_camwFanIdx = 0; s_camwFanTicks = 0;
                        Log::World("WorldMap: [CAMW-REC] engine block #%d at (%d,%d) tgt=%d -> fan-out",
                                   s_camwFreezeN, px, py, targetBearing);
                    }
                }
                int camwAim = targetBearing;
                if (s_camwRec == 1) {
                    // v0.18.3.204 FAN-OUT: 32 ABSOLUTE bearings on a 128au grid, ordered by
                    // |deviation| from the waypoint bearing, hold 15 frames each, 2 full
                    // cycles before giving up (BAT203 sect. 6 item 3). Escape = >=24u of
                    // real measured motion. (The old +-1024 relative fan couldn't find the
                    // one open lane when the waypoint bearing itself swung frame to frame.)
                    static int32_t s_camwFanAx = 0, s_camwFanAy = 0;
                    if (s_camwFanTicks == 0) { s_camwFanAx = px; s_camwFanAy = py; }
                    {
                        int fi   = s_camwFanIdx % 32;
                        int step = (fi + 1) / 2;
                        int dev  = ((fi & 1) ? +step : -step) * 128;
                        int base = (targetBearing + 64) & 0xF80;    // snap to the absolute 128au grid
                        camwAim  = (base + dev) & 0xFFF;
                    }
                    s_camwFanTicks++;
                    int32_t adx = px - s_camwFanAx, ady = py - s_camwFanAy;
                    WrapWorldDelta(adx, ady);
                    if (s_camwFanTicks >= 3 && (double)adx * adx + (double)ady * ady >= 576.0) {
                        s_camwFanBear = camwAim; s_camwRec = 2; s_camwWallTicks = 0;
                        s_camwWfStartX = px; s_camwWfStartY = py; s_camwWfClearRun = 0;
                        Log::World("WorldMap: [CAMW-REC] fan escape at bearing %d -> wall-follow (commit %d)",
                                   camwAim, s_camwWfCommit);
                    } else if (s_camwFanTicks >= 15) {
                        s_camwFanTicks = 0;
                        if (++s_camwFanIdx >= 64 || s_camwFreezeN > 8) {
                            // Fan exhausted (2 full cycles) or the leg keeps re-blocking:
                            // blocks were already learned at episode entry (G1); retreat
                            // along our own breadcrumbs, then re-plan around the fence.
                            s_camwRec = 3;
                            s_camwRetreatLeft = (s_camwCrumbN < 10) ? s_camwCrumbN : 10;
                            Log::World("WorldMap: [CAMW-REC] fan exhausted -> retreat %d crumbs",
                                       s_camwRetreatLeft);
                        }
                    }
                } else if (s_camwRec == 2) {
                    // v0.18.3.204 WALL-FOLLOW with HYSTERESIS + COMMITMENT (BAT203 sect. 6
                    // item 3). The .203 grind: exit on the FIRST 'clear' probe -> one step ->
                    // re-blocked, forever. Now exit only after the waypoint bearing's SWEPT
                    // probe stays clear >=8 consecutive frames AND we've travelled >= the
                    // commitment distance since the follow began (64u, doubled per quick
                    // re-block up to 512u).
                    camwAim = s_camwFanBear;
                    s_camwWallTicks++;
                    if (!SweptFootBlocked(px, py, targetBearing)) s_camwWfClearRun++;
                    else                                          s_camwWfClearRun = 0;
                    double camwTravel = CalculateWrappedDistance(px, py, s_camwWfStartX, s_camwWfStartY);
                    if (s_camwWfClearRun >= 8 && camwTravel >= (double)s_camwWfCommit) {
                        s_camwRec = 0;
                        s_camwResumeT = GetTickCount();
                        Log::World("WorldMap: [CAMW-REC] waypoint bearing swept-clear x8 after %.0fu -> normal steering",
                                   camwTravel);
                    } else if (s_camwFrzTicks >= 40) {           // the wall bearing itself blocked
                        CamwLearnBlock(px, py, s_camwFanBear);   // G1: this bearing is a wall too
                        s_camwRec = 1; s_camwFanIdx = 0; s_camwFanTicks = 0; s_camwFrzTicks = 0;
                        Log::World("WorldMap: [CAMW-REC] wall-follow blocked -> fan-out");
                    } else if (s_camwWallTicks >= 300) {         // not converging: fall back to retreat
                        s_camwRec = 3;
                        s_camwRetreatLeft = (s_camwCrumbN < 10) ? s_camwCrumbN : 10;
                        Log::World("WorldMap: [CAMW-REC] wall-follow timeout -> retreat");
                    }
                } else if (s_camwRec == 3) {
                    // RETREAT along breadcrumbs (stall-guarded: the gate is anisotropic, our own
                    // trail is NOT guaranteed re-walkable -- skip crumbs that won't come back).
                    if (s_camwRetreatLeft <= 0 || s_camwCrumbN <= 0) {
                        // v0.18.3.204 G3: REPLAN DISCIPLINE. Replans cost 10+ live seconds
                        // and the .203 loop burned them on IDENTICAL plans. Only replan with
                        // NEW learned knowledge; if this episode learned nothing (dedupe ate
                        // everything), INFLATE the fence along the last blocked bearing so
                        // the graph genuinely changes. Recovery replans start WIDE (24576u).
                        if (s_navBlkN <= s_camwPlanBlkN) {
                            // Inflation distances must clear AddNavBlock's 192u dedupe box
                            // around the 112u cell G1 already learned -- 240/304 landed
                            // INSIDE it on diagonal bearings (review finding, 2026-07-02),
                            // silently adding nothing. 384/512 always clear it.
                            const double thI = (double)s_camwLastBlockBearing / 4096.0 * 6.283185307179586;
                            if (!AddNavBlock(px + (int32_t)(sin(thI) * 384.0), py - (int32_t)(cos(thI) * 384.0)))
                                AddNavBlock(px + (int32_t)(sin(thI) * 512.0), py - (int32_t)(cos(thI) * 512.0));
                            Log::World("WorldMap: [CAMW-REC] no new knowledge -- inflated fence (overlay %d)", s_navBlkN);
                        }
                        if (s_drivePlannerEligible && s_driveReplanCount < DRIVE_MAX_REPLANS) {
                            s_driveReplanCount++;
                            s_planWideFirst = true;   // v0.18.3.204: detours need room immediately
                            Log::World("WorldMap: [CAMW-REC] re-planning around learned blocks (%d/%d, overlay %d)",
                                       s_driveReplanCount, DRIVE_MAX_REPLANS, s_navBlkN);
                            PlanDrivePath(px, py);
                            s_camwPlanBlkN = s_navBlkN;
                        }
                        s_camwRec = 0; s_camwFreezeN = 0; s_camwFrzTicks = 0;
                        s_camwLearnEp = 0; s_camwResumeT = 0; s_camwWfCommit = 64;
                    } else {
                        int32_t cbx = s_camwCrumbX[s_camwCrumbN - 1], cby = s_camwCrumbY[s_camwCrumbN - 1];
                        camwAim = TorusBearing(px, py, cbx, cby);
                        double cbd = CalculateWrappedDistance(px, py, cbx, cby);
                        if (cbd <= 48.0 || s_camwFrzTicks >= 20) {   // reached, or crumb unreachable
                            s_camwCrumbN--; s_camwRetreatLeft--; s_camwFrzTicks = 0;
                        }
                    }
                } else {
                    // v0.18.3.204 G4: CENTERLINE DISCIPLINE (BAT203 sect. 6 item 5). The
                    // .203 route line ran 36-88u from corridor walls -- inside the engine's
                    // sticky rejection zone. When perpendicular clearance L+R < 288u, offset
                    // the aim laterally by (R-L)/2 (clamp +-96u) so the character holds the
                    // corridor centerline (through the .203 neck this is z~-25086 at
                    // x=-44608). Probes at 16u pitch to 160u, learned cells included.
                    {
                        const double thb = (double)(targetBearing & 0xFFF) / 4096.0 * 6.283185307179586;
                        const double fwx = sin(thb), fwy = -cos(thb);    // forward unit
                        const double prx = cos(thb), pry = sin(thb);     // perpendicular (bearing+1024) unit
                        int Lc = 160, Rc = 160;
                        // v0.18.3.208: probes upgraded to the 8u-quantized FootBlocked8 --
                        // the .207 neck grind proved the 32u cache can't see the ~1u sliver
                        // wall, so G4 never engaged in exactly the corridor it was built for.
                        for (int d = 16; d <= 160; d += 16) {
                            if (Rc == 160) {
                                int32_t qx = px + (int32_t)(prx * d), qy = py + (int32_t)(pry * d);
                                if (FootBlocked8(qx, qy) || IsNavBlockedWorld(qx, qy)) Rc = d;
                            }
                            if (Lc == 160) {
                                int32_t qx = px - (int32_t)(prx * d), qy = py - (int32_t)(pry * d);
                                if (FootBlocked8(qx, qy) || IsNavBlockedWorld(qx, qy)) Lc = d;
                            }
                        }
                        if (Lc + Rc < 288) {
                            int lat = (Rc - Lc) / 2;
                            if (lat > 96) lat = 96; if (lat < -96) lat = -96;
                            int32_t apx = px + (int32_t)(fwx * 128.0 + prx * (double)lat);
                            int32_t apy = py + (int32_t)(fwy * 128.0 + pry * (double)lat);
                            camwAim = TorusBearing(px, py, apx, apy);
                        }
                    }
                    // NORMAL: drop a breadcrumb every 64u of travel (ring of 32).
                    if (s_camwCrumbN == 0 ||
                        CalculateWrappedDistance(px, py, s_camwCrumbX[s_camwCrumbN - 1],
                                                 s_camwCrumbY[s_camwCrumbN - 1]) >= 64.0) {
                        if (s_camwCrumbN >= 32) {
                            for (int ci = 1; ci < 32; ci++) {
                                s_camwCrumbX[ci - 1] = s_camwCrumbX[ci];
                                s_camwCrumbY[ci - 1] = s_camwCrumbY[ci];
                            }
                            s_camwCrumbN = 31;
                        }
                        s_camwCrumbX[s_camwCrumbN] = px; s_camwCrumbY[s_camwCrumbN] = py; s_camwCrumbN++;
                    }
                }
                int camwWrite = (camwAim - camwBias / 2 + s_camwTrim) & 0xFFF;
                if (!s_camwLockCleared && ReadMemDword32(WM_CAM_LOCK) == 1) {
                    s_camwLockCleared = true;
                    WriteMemDword32(WM_CAM_LOCK, 0);
                    Log::World("WorldMap: [CAMW] region camera lock cleared for drive (restored on stop)");
                }
                int camwVelBefore = (int)(int16_t)ReadMemWord16(WM_CAM_VEL);
                WriteMemWord16(WM_CAM_YAW, (uint16_t)camwWrite);
                WriteMemWord16(WM_CAM_VEL, 0);
                wantUp = true;
                // Closed-loop trim: on clean moves (>=16u this frame), compare the measured
                // world motion bearing to the bearing aimed LAST frame; a persistent constant
                // error (convention offset, un-modeled bias) is nulled slowly. Wall slides show
                // up as large transient errors -- the |e|<=600 window plus 8-sample averaging
                // keeps them from polluting the trim.
                if (s_camwHadPrev) {
                    int32_t mdx = px - s_camwPx, mdy = py - s_camwPy;
                    WrapWorldDelta(mdx, mdy);
                    if ((double)mdx * mdx + (double)mdy * mdy >= 256.0) {
                        double camwRad = atan2((double)mdx, -(double)mdy);   // 0=North, CW (TorusBearing convention)
                        int camwMeas = ((((int)(camwRad / 6.283185307179586 * 4096.0)) % 4096) + 4096) % 4096;
                        int camwErr  = ((((camwMeas - s_camwAimPrev + 2048) % 4096) + 4096) % 4096) - 2048;
                        // CONVENTION-FLIP INSURANCE: live .173-.200 arrivals prove motion ==
                        // camYaw in the mod's TorusBearing convention (no offset), but the .151/
                        // .152 BATs once measured forward = heading+2048. If that ever holds
                        // (some region/state we haven't seen), every clean move would read
                        // ~+-2048 error -- outside the +-600 trim window, so the slow trim could
                        // never fix it. Detect 6 consecutive clean moves at |err|>1600 and apply
                        // a one-time 2048 flip to the trim; the normal trim then fine-tunes.
                        static int s_camwFlipRun = 0;
                        if (camwErr > 1600 || camwErr < -1600) {
                            if (++s_camwFlipRun >= 6) {
                                s_camwTrim += 2048;
                                if (s_camwTrim > 2047) s_camwTrim -= 4096;
                                s_camwFlipRun = 0; s_camwErrAcc = 0; s_camwErrN = 0;
                                Log::World("WorldMap: [CAMW] 180deg convention flip applied -> trim=%d", s_camwTrim);
                            }
                        } else {
                            s_camwFlipRun = 0;
                        }
                        if (camwErr >= -600 && camwErr <= 600) {
                            s_camwErrAcc += camwErr;
                            if (++s_camwErrN >= 8) {
                                int camwAdj = s_camwErrAcc / 16;    // half the mean error per update
                                if (camwAdj != 0) {
                                    s_camwTrim -= camwAdj;
                                    if (s_camwTrim > 2047)  s_camwTrim -= 4096;
                                    if (s_camwTrim < -2048) s_camwTrim += 4096;
                                    Log::World("WorldMap: [CAMW] trim %+d -> %d (mean err %+d over 8 clean moves)",
                                               -camwAdj, s_camwTrim, s_camwErrAcc / 8);
                                }
                                s_camwErrAcc = 0; s_camwErrN = 0;
                            }
                        }
                    }
                }
                s_camwAimPrev = camwAim;   // v0.18.3.202: trim measures against the bearing actually steered
                s_camwPx = px; s_camwPy = py; s_camwHadPrev = true;
                if (DRIVE_STEER_DIAG) {
                    static DWORD s_camwDiagT = 0;
                    if (now - s_camwDiagT >= (DWORD)DRIVE_STEER_DIAG_INTERVAL_MS) {
                        // One line carries the full disambiguation set from CAMERA_EXE_ANALYSIS
                        // section 7: written vs read-back yaw, engine heading readback, bias,
                        // pre-write follow velocity, lock flag -- a single BAT verifies the model.
                        Log::World("WorldMap: [CAMW] pos(%d,%d) tgt=%d aim=%d rec=%d wrote=%d cam=%d mh=%u bias=%d vel0=%d trim=%d lock=%u idx=%d/%d dist=%.0f",
                                   px, py, targetBearing, camwAim, s_camwRec, camwWrite, GetWorldMapCameraYaw(),
                                   (unsigned)heading, camwBias, camwVelBefore, s_camwTrim,
                                   (unsigned)ReadMemDword32(WM_CAM_LOCK),
                                   s_drivePathIdx, s_drivePathLen, dist);
                        s_camwDiagT = now;
                    }
                }
            }
        } else {
            // v0.18.3.166 (cleanup): removed the legacy 8-way `sector` + STEPGUARD redirect that
            // used to live here -- it computed a `sector` that was then discarded (`(void)sector`),
            // so it never steered anything and only spammed the log with [STEPGUARD] lines. All
            // on-foot steering is done by the native self-calibrating + probe executor below.
            {
                // v0.18.3.152: 180-degree (2048) offset. The .151 BAT showed the
                // character WALKS the opposite way it "aligns": at yaw=573 (~aligned
                // to tgtBrg=494, NE) forward motion was SW and dist GREW; a reverse
                // burst at yaw=1755 moved SE -- both consistent with forward-travel =
                // heading+180deg (the move-heading register 0x0203FE52 is 180deg from
                // the (sin,-cos) bearing tgtBrg uses). So align the FORWARD direction,
                // not the facing: add 2048 to the target bearing. (Turn handedness is
                // unchanged -- a constant target shift doesn't flip which key reduces err.)
                // v0.18.3.154: DIRECT-HEADING steering (offline-sim proven). Instead of
                // pressing LEFT/RIGHT and fighting the game's coarse per-frame turn step
                // (512-1024 >> deadzone -> the .152/.153 spin: 138/149 frames turned, 8
                // walked), WRITE the move-heading register so forward already points at the
                // target, then just press UP. Forward travel = heading+2048, so to move
                // toward targetBearing set heading = (targetBearing + 2048) & 0xFFF. The mod
                // runs inside the game's address space and the move builder (0x53DA20) reads
                // 0x0203FE52 each frame; we press no turn key, so nothing else rewrites it.
                // SEH-guarded. If a BAT shows yaw NOT tracking the written value, the engine
                // re-derives heading and we fall back to arrow steering + exact path-follow.
                // v0.18.3.157: POSITION-WRITE navigation (offline-sim proven; replaces all
                // heading-based steering on foot). The .156 BAT proved on-foot movement is
                // SCREEN-RELATIVE: the character walks 8-way directions that ignore the written
                // heading entirely (yaw tracked our writes but the move bearing did not), so
                // neither arrow-turning nor direct-heading-writing can aim it. Instead, DRIVE
                // the character by writing its world position one engine foot-step (32u) per
                // frame straight toward the next waypoint (steerX/steerY), gated by the engine's
                // own rule (destination walkable + |dH| < 200) via WorldGroundHeightLocal. We
                // press NO keys, so the engine's screen-relative input movement contributes
                // nothing to fight the write. Sim (identical 32u position-stepping): reaches the
                // in-game start->Dollet case and 15/18 trio pairs; the 3 misses graze a real
                // cliff that needs finer planning (the planner-resolution follow-up).
                // v0.18.3.159: NATIVE-KEY empirical steering (supersedes the .157/.158 position-
                // write, which teleported and bypassed the step counter / random encounters). On
                // foot the arrows move SCREEN-RELATIVE (the .156 BAT proved heading can't aim the
                // character), so we SELF-CALIBRATE the screen->world mapping from the world-position
                // response to our own key presses, then press the 8-way arrow combo whose world
                // direction points at the next waypoint. The ENGINE performs all movement, so steps
                // count and encounters roll normally. Offline sim (screen-relative engine model with
                // camera swing): reaches the trio targets under constant / drifting / per-region
                // cameras. s_navTheta = learned world bearing of screen-UP; s_navHand = chirality.
                (void)targetBearing; (void)heading;
                static int     s_navHand    = 0;   // +1/-1 (0 = unknown), learned once and kept
                static int     s_navTheta   = 0;   // 0..4095, world bearing produced by pressing UP
                static int     s_navBoot    = 0;   // 0 = probe UP, 1 = probe RIGHT, 2 = running
                static int32_t s_navPx      = 0, s_navPy = 0;
                static int     s_navLastK   = 0;
                static bool    s_navHadPrev = false;
                // v0.18.3.168: empirical-unstick state (see the block after the probe below).
                static DWORD   s_navWinT     = 0;        // 250ms measurement-window start
                static int32_t s_navWinX     = 0, s_navWinY = 0;
                static int     s_navStallWin = 0;        // consecutive no-progress windows
                static bool    s_navEmp      = false;    // empirical key-sweep active
                static int     s_navEmpK     = 0;        // 8-way key currently under empirical test
                // v0.18.3.171: after a position-write assist, drop stale calibration so native re-learns
                // the camera from fresh key presses instead of reading the teleport jump as motion.
                if (s_driveNavResync) {
                    s_navBoot = 0; s_navHadPrev = false; s_navEmp = false;
                    s_navStallWin = 0; s_navWinT = 0; s_driveNavResync = false;
                }
                // Measure last frame's world motion produced by our previous key (if it moved).
                int navMvb = -1;
                if (s_navHadPrev) {
                    int32_t mdx = px - s_navPx, mdy = py - s_navPy;
                    WrapWorldDelta(mdx, mdy);
                    if ((double)mdx * mdx + (double)mdy * mdy > 64.0) {
                        double rad = atan2((double)mdx, -(double)mdy);   // 0 = North, CW
                        int u = (int)(rad / 6.283185307179586 * 4096.0);
                        navMvb = ((u % 4096) + 4096) % 4096;
                    }
                }
                // v0.18.3.173: STEER BY THE REAL CAMERA YAW. Read 0x0203ED02 each frame; if valid it
                // IS the world bearing of screen-UP, so set s_navTheta directly (hand=+1, RIGHT=CW) and
                // skip the learned bootstrap. Motion is still measured and, under diag, logged against
                // the register's prediction to confirm it. If the read faults (-1) we fall back to the
                // learned calibration below.
                int camYawNow = GetWorldMapCameraYaw();
                if (DRIVE_STEER_DIAG && camYawNow >= 0 && navMvb >= 0 && s_navHadPrev) {
                    int pred = (camYawNow + s_navLastK * 512) & 0xFFF;
                    int ve = ((((navMvb - pred + 2048) % 4096) + 4096) % 4096) - 2048;
                    Log::World("WorldMap: [CAMYAW] reg=%d lastK=%d pred=%d meas=%d err=%+d",
                               camYawNow, s_navLastK, pred, navMvb, ve);
                }
                if (camYawNow >= 0) { s_navTheta = camYawNow; s_navHand = 1; s_navBoot = 2; }
                int navK;
                if (s_navBoot == 0) {
                    navK = 0;                                  // press UP to learn the camera angle
                    if (navMvb >= 0) { s_navTheta = navMvb; s_navBoot = 1; }
                } else if (s_navBoot == 1) {
                    navK = 2;                                  // press RIGHT to learn chirality
                    if (navMvb >= 0) {
                        int diff = (((navMvb - s_navTheta) % 4096) + 4096) % 4096;
                        s_navHand = (diff < 2048) ? 1 : -1;    // RIGHT ~ +1024 (CW) => hand +1
                        s_navBoot = 2;
                    }
                } else {
                    if (camYawNow < 0 && navMvb >= 0) {         // register unavailable: track via motion
                        int implied = (((navMvb - s_navHand * s_navLastK * 512) % 4096) + 4096) % 4096;
                        // v0.18.3.160: signed wrap MUST add 2048 BEFORE the mod, else a 180deg
                        // error reads as zero and the estimate locks 180deg off (the .159 BAT:
                        // char drifted ~180deg the wrong way, U-L- locked, dist grew). Matches the
                        // offline sim's formula, which converged correctly.
                        int err = ((((implied - s_navTheta + 2048) % 4096) + 4096) % 4096) - 2048;
                        s_navTheta = (((s_navTheta + err / 2) % 4096) + 4096) % 4096;
                    }
                    // v0.18.3.176: ORACLE-UNRELIABILITY DETECTION. WorldGroundHeightLocal is wrong at
                    // ~15% of cells -- overhang/overlap cells where the engine stands on a LOWER surface
                    // our offline mesh doesn't represent (the .175 log: engine 0x0203FE30 = -551 where our
                    // oracle says -130; -883 where ours tops at -710). At those cells the oracle-gated
                    // probe approves steps the engine blocks -> wedge, and the teleport grounds the char
                    // at the wrong Z -> freeze. We can't fix the mesh from here, but the engine EXPOSES the
                    // character's true current ground height at 0x0203FE30. When it disagrees with our
                    // oracle, the oracle is untrustworthy here: drop the oracle-gated probe and drive
                    // EMPIRICALLY (exact-yaw seeded, real measured motion) until we're back on ground the
                    // oracle gets right. The engine's real collision is the source of truth.
                    int engGH = 0; __try { engGH = *(volatile int32_t*)0x0203FE30; } __except (EXCEPTION_EXECUTE_HANDLER) {}
                    int oraGH = WorldGroundHeightLocal(px, py);
                    bool oracleBad = (engGH != 0) && (oraGH != WGH_NO_GROUND) && (abs(engGH - oraGH) > 120);
                    if (oracleBad && !s_navEmp) {
                        s_navEmp = true; s_navStallWin = 0;
                        if (DRIVE_STEER_DIAG)
                            Log::World("WorldMap: [OBAD] pos(%d,%d) engH=%d oraH=%d diff=%d -> empirical steering",
                                       px, py, engGH, oraGH, engGH - oraGH);
                    }
                    int Dw  = TorusBearing(px, py, steerX, steerY);
                    int rel = (((Dw - s_navTheta) % 4096) + 4096) % 4096;   // 0..4095
                    int kpos = ((rel + 256) / 512) % 8;                     // nearest 8-way (hand +1)
                    int knear = (s_navHand > 0) ? kpos : ((8 - kpos) % 8);
                    // v0.18.3.164: PROBE around the calibrated key using the corrected height model
                    // (.162). Among the 8-way keys (calibrated-nearest first), pick the one whose
                    // 32u world step is walkable (|dH|<200) AND most reduces distance to the
                    // waypoint, so the character hugs the corridor and rounds canyon walls instead
                    // of drifting into them. Offline sim with this: 17/18 trio pairs thread.
                    int curH164 = WorldGroundHeightLocal(px, py);
                    double d0 = CalculateWrappedDistance(px, py, steerX, steerY);
                    int bestK = knear; double bestGain = -1e18; bool got = false; int stairK = -1;
                    static const int PORD[8] = {0, 1, -1, 2, -2, 3, -3, 4};
                    for (int oi = 0; oi < 8; oi++) {
                        int k = (((knear + PORD[oi]) % 8) + 8) % 8;
                        int wb = (s_navTheta + s_navHand * k * 512) & 0xFFF;
                        double th = (double)wb / 4096.0 * 6.283185307179586;
                        int32_t cx = px + (int32_t)(sin(th) * 32.0);
                        int32_t cy = py - (int32_t)(cos(th) * 32.0);
                        int ch = WorldGroundHeightLocal(cx, cy);
                        if (ch == WGH_NO_GROUND) continue;
                        if (curH164 != WGH_NO_GROUND && abs(ch - curH164) >= 200) continue;
                        double g = d0 - CalculateWrappedDistance(cx, cy, steerX, steerY);
                        if (g > bestGain) { bestGain = g; bestK = k; got = true; }
                        // v0.18.3.175: STAIRCASE along the route. PORD is nearest-first (the path
                        // direction first, then +/-1, +/-2...). Take the CLOSEST-to-path key that still
                        // makes forward progress, NOT the greediest. On a steep ramp the direct key is
                        // gate-blocked and the greedy pick veers sideways OFF the gate-legal centerline
                        // -- that drift is what stalled native at the start-climb and the canyon descent.
                        // The nearest-progressing key keeps the character hugging the ramp the planner
                        // already verified is collision-free, so it threads the ramp instead of teleporting.
                        if (stairK < 0 && g > 4.0) stairK = k;
                        if (PORD[oi] == 0 && g > 4.0) break;   // calibrated key already advances -- take it
                    }
                    navK = (stairK >= 0) ? stairK : (got ? bestK : knear);

                    // v0.18.3.168: EMPIRICAL UNSTICK. The calibrated probe above trusts s_navTheta to
                    // map keys->world and validates the PREDICTED cell. But s_navTheta only updates on
                    // clean motion (>8u), so the instant the character stops moving its calibration
                    // FREEZES: the probe keeps committing a key that points into a wall, the character
                    // wedges (d+0), and with no motion the calibration can never self-correct -- the
                    // deadlock behind every jam (.159-.167; .167 wedged d+0 at idx 1 pressing U--R
                    // forever). Break it by MEASURING reality. After a few no-progress 250ms windows,
                    // sweep the ACTUAL 8 keys one per window; the moment a key produces real
                    // displacement that REDUCES distance to the waypoint, RE-DERIVE s_navTheta from that
                    // measured world bearing and resume the calibrated probe. Needs no valid
                    // calibration to escape -- it trusts only what actually moved the character.
                    if (now - s_navWinT >= (DWORD)DRIVE_PROBE_WINDOW_MS) {
                        int32_t wdx = px - s_navWinX, wdy = py - s_navWinY;
                        WrapWorldDelta(wdx, wdy);
                        double wdisp = sqrt((double)wdx * wdx + (double)wdy * wdy);
                        if (!s_navEmp) {
                            if (wdisp < DRIVE_PROBE_MIN_MOVE) {
                                if (++s_navStallWin >= 3) { s_navEmp = true; s_navEmpK = 0; }
                            } else {
                                s_navStallWin = 0;
                            }
                        } else {
                            double before = CalculateWrappedDistance(s_navWinX, s_navWinY, steerX, steerY);
                            double after  = CalculateWrappedDistance(px, py, steerX, steerY);
                            if (wdisp >= DRIVE_PROBE_MIN_MOVE && after < before - 1.0) {
                                double rad = atan2((double)wdx, -(double)wdy);
                                int mb = ((((int)(rad / 6.283185307179586 * 4096.0)) % 4096) + 4096) % 4096;
                                // v0.18.3.185: back out theta from the ACTUAL key held this window
                                // (s_navLastK), not s_navEmpK -- s_navEmpK is now a fan-out index, not
                                // a key. s_navLastK is the key the engine actually moved on.
                                s_navTheta = (((mb - s_navHand * s_navLastK * 512) % 4096) + 4096) % 4096;
                                s_navEmp = false; s_navStallWin = 0;
                            } else {
                                s_navEmpK = (s_navEmpK + 1) & 7;   // this key is walled or wrong-way -- try the next
                            }
                        }
                        s_navWinX = px; s_navWinY = py; s_navWinT = now;
                    }
                    // v0.18.3.185: FAN-OUT recovery. The empirical sweep used to cycle the 8 keys
                    // blindly (0,1,2..7), so it routinely held a key pointing backwards/sideways and
                    // crawled or never recovered. Order the sweep around the CURRENT target-nearest key
                    // instead -- try the goal direction first, then +/-1, +/-2... -- so it locks onto a
                    // walkable heading toward the waypoint within a step or two. s_navEmpK is now the
                    // PORD index (0..7), not an absolute key. Offline sim: 5/18 -> 13/18 harsh, 35/36
                    // realistic. knear tracks the live camera yaw + target each frame, so the fan-out
                    // re-centers even as the camera rotates (the Dollet-exit yaw swing).
                    if (s_navEmp) navK = (((knear + PORD[s_navEmpK & 7]) % 8) + 8) % 8;
                }
                // v0.18.3.180: STEERING COMMITMENT. The .179 dense (50ms) log proved the teleports come
                // from a STEERING DITHER at waypoints: near a target the bearing to it swings every frame,
                // navK flips, and because the engine only translates on a SUSTAINED key hold, the rapid
                // flipping yields d+0 -- the character can't move even in fully-open terrain (all 8 dirs
                // walkable), so it teleports. Offline key-hold model reproduced it; holding the key fixes it.
                // Hold the chosen key ~150ms (a low-frequency steering loop: re-aims often enough to track
                // the route, not so fast it dithers); switch early only if the held key becomes blocked.
                // Running probe only -- bootstrap and empirical-unstick manage their own keys.
                {
                    static int   s_navHeldK = -1;
                    static DWORD s_navHeldT = 0;
                    const DWORD  COMMIT_MS  = 150;
                    if (s_navBoot == 2 && !s_navEmp) {
                        if (s_navHeldK >= 0 && (now - s_navHeldT) < COMMIT_MS) {
                            int    hb  = (s_navTheta + s_navHand * s_navHeldK * 512) & 0xFFF;
                            double hth = (double)hb / 4096.0 * 6.283185307179586;
                            int32_t hx = px + (int32_t)(sin(hth) * 32.0);
                            int32_t hy = py - (int32_t)(cos(hth) * 32.0);
                            int hgh = WorldGroundHeightLocal(hx, hy);
                            int cgh = WorldGroundHeightLocal(px, py);
                            bool heldOk = (hgh != WGH_NO_GROUND) && (cgh == WGH_NO_GROUND || abs(hgh - cgh) < 200);
                            if (heldOk) navK = s_navHeldK;                 // keep the sustained hold
                            else { s_navHeldK = navK; s_navHeldT = now; }   // held key blocked -> re-commit
                        } else {
                            s_navHeldK = navK; s_navHeldT = now;           // commit window expired -> re-commit
                        }
                    }
                }
                navK &= 7;
                wantUp    = (navK == 7 || navK == 0 || navK == 1);
                wantRight = (navK == 1 || navK == 2 || navK == 3);
                wantDown  = (navK == 3 || navK == 4 || navK == 5);
                wantLeft  = (navK == 5 || navK == 6 || navK == 7);
                s_navLastK = navK;
                s_navPx = px; s_navPy = py; s_navHadPrev = true;
            }
        }
        // v0.18.3.257 (#79): physics vehicle-detector. The geometry is here; every
        // DECISION lives in world_map_vehsig.inl, which tests/vehsig_test.cpp
        // replays real logged frames through -- see the note at the top of that
        // file for why v0.21.0 had to put two guards in front of it.
        {
            int vsCam = GetWorldMapCameraYaw();
            if (s_vsHad && vsCam >= 0) {
                int32_t vdx = px - s_vsPx, vdy = py - s_vsPy;
                WrapWorldDelta(vdx, vdy);
                if ((double)vdx * vdx + (double)vdy * vdy >= 64.0) {
                    int vsGap = ((((int)heading - vsCam + 2048) % 4096 + 4096) % 4096) - 2048;
                    if (vsGap < 0) vsGap = -vsGap;
                    double vr = atan2((double)vdx, -(double)vdy);
                    int mb = ((((int)(vr / 6.283185307179586 * 4096.0)) % 4096) + 4096) % 4096;
                    int em = ((((mb - (int)heading + 2048) % 4096 + 4096) % 4096)) - 2048; if (em < 0) em = -em;
                    int ec = ((((mb - vsCam + 2048) % 4096 + 4096) % 4096)) - 2048;        if (ec < 0) ec = -ec;

                    const int  vsId   = GetActiveVehicleId();
                    const bool vsFoot = (vsId >= 0 &&
                                         GetVehicleType((uint8_t)vsId) == VEH_ON_FOOT);
                    int vsVotes = 0;
                    const VehSigResult vsR = VehSigFeed(s_vsSig, vsGap, em < ec,
                                                        vsFoot, &vsVotes);
                    if (vsR == VS_ID_VETO) {
                        Log::World("WorldMap: [VEHSIG] verdict VETOED: motion sided with mh in %d of %d, but the engine's vehicleId=%d says ON FOOT -- staying on the foot steering law (#79)",
                                   vsVotes, VS_WINDOW, vsId);
                    } else if (vsR == VS_LATCH && !s_driveVehicleSig) {
                        s_driveVehicleSig = true;
                        Log::World("WorldMap: [VEHSIG] VEHICLE DETECTED: motion sided with mh in %d of last %d disagreement frames -- switching this drive to the vehicle steering law (#79)",
                                   vsVotes, VS_WINDOW);
                        ScreenReader::Speak("Vehicle detected.", true);
                        s_vsLastLog = now;
                    }
                }
            }
            s_vsPx = px; s_vsPy = py; s_vsHad = true;
        }
        if (DRIVE_STEER_DIAG) {
            static DWORD   s_yawLast  = 0;
            static int32_t s_yawLastX = 0, s_yawLastY = 0;
            if (now - s_yawLast >= (DWORD)DRIVE_STEER_DIAG_INTERVAL_MS) {
                double dmoved = CalculateWrappedDistance(s_yawLastX, s_yawLastY, px, py);
                int scrAng = ((int)targetBearing - (int)heading) & 0xFFF;
                Log::World("WorldMap: [YAWDRIVE] pos(%d,%d) d+%.0f | yaw=%u tgtBrg=%d scrAng=%d | steer(%d,%d) dist=%.0f idx=%d/%d | keys=%s%s%s%s%s%s",
                           px, py, dmoved, (unsigned)heading, targetBearing, scrAng,
                           steerX, steerY, dist, s_drivePathIdx, s_drivePathLen,
                           wantUp ? "U" : "-", wantDown ? "D" : "-",
                           wantLeft ? "L" : "-", wantRight ? "R" : "-",
                           inReverseBurst ? " REVERSE" : "", wallJam ? " JAM" : "");
                s_yawLast = now; s_yawLastX = px; s_yawLastY = py;
            }
        }
#if WM_MOTION_DIAG
        // v0.18.3.186: per-frame motion-fidelity capture for the offline sim. One UNthrottled line
        // per drive frame. From consecutive [MFRAME] positions the sim recovers the engine's exact
        // wall-slide (the bearing the executor INTENDED, ib = cam + hand*navK*512, vs the actual
        // motion bearing measured between frames), and from eZ vs oZ it builds the engine-truth
        // ground map vs our oracle (the oracle-miss correction). t=ms gives per-frame dt for speed.
        {
            // navK/hand are scoped to the steering block above; the keys field below already encodes
            // the chosen 8-way key and `cam` is the camera yaw, so the intended bearing
            // (cam + key*512) is reconstructed offline -- no need to reach those locals here.
            int camY = GetWorldMapCameraYaw();
            int charZ = 0, engZ = 0;
            __try { charZ = *(volatile int32_t*)WM_POS_Z; engZ = *(volatile int32_t*)0x0203FE30; }
            __except (EXCEPTION_EXECUTE_HANDLER) {}
            int oraZ = WorldGroundHeightLocal(px, py);
            Log::World("WorldMap: [MFRAME] t=%u p(%d,%d) cz=%d cam=%d mh=%u eZ=%d oZ=%d idx=%d/%d %c%c%c%c",
                       (unsigned)now, px, py, charZ, camY, (unsigned)heading, engZ, oraZ,
                       s_drivePathIdx, s_drivePathLen,
                       wantUp ? 'U' : '-', wantDown ? 'D' : '-', wantLeft ? 'L' : '-', wantRight ? 'R' : '-');
        }
#endif
    } else {
    if (inReverseBurst) {
        // #67 v0.18.3.71: DOWN-only reverse off the wall. Aaron confirmed DOWN
        // moves the character on the world map, so this is a clean backward burst
        // into open ground; normal steering re-aims him once he's free. No
        // simultaneous turn -- turning while wedged does nothing (.68/.69) and
        // only muddied the motion. The burst is now TRIGGERED from the stuck
        // check (proven to fire) rather than the hard-wedge vibration detector,
        // which never fired across the .68/.69/.70 BATs.
        wantDown = true;
    } else if (hardWedge && dist >= FINAL_APPROACH_FORWARD_DIST &&
               s_driveWedgeReverseCount < MAX_WEDGE_REVERSE) {
        // Vestigial fast-path: net-displacement hard-wedge (rarely trips).
        // DOWN-only, same as above; the stuck-check trigger is primary.
        s_driveWedgeReverseUntil = now + REVERSE_BURST_MS;
        s_driveWedgeReverseCount++;
        wantDown = true;
        Log::World("WorldMap: [DRIVE] Hard wedge -> reverse un-wedge burst %d/%d (off=%d, dist=%.0f)",
                   s_driveWedgeReverseCount, MAX_WEDGE_REVERSE, off, dist);
    } else if (dist < VEH_FINAL_APPROACH_DIST) {
        // v0.18.3.259 (#68): VEHICLE FINAL APPROACH -- inside the car's turning
        // circle (~1200u > measured turn radius ~1043u), never stop to pivot: a
        // stationary rotate-and-launch cycle at this range can only ORBIT the
        // aim (the .258 Balamb approach). Keep the gas down and arc-steer
        // toward the aim when misaimed; the moving sweep crosses the broad
        // entry trigger instead of circling it. Subsumes the old <200u pure-
        // forward band (within the deadzone this is pure forward anyway).
        wantUp = true;
        if (off > STEER_DEADZONE) {
            if (err >= 0) wantRight = true;
            else          wantLeft  = true;
        }
    } else if (off > STEER_FWD_CONE) {
        // PIVOT, turn-only -- v0.18.3.258 (#79): only beyond the forward cone
        // (~50deg) now. RIGHT raises the heading (clockwise) toward a target
        // clockwise of us (err>=0); LEFT lowers it. NO forward -- holding UP into
        // terrain locks the rotation. Errors inside the cone use the ARC band
        // below (drive AND turn), which is what removes the stop-start feel the
        // .257 car BAT reported: the old law pivoted for everything past the
        // deadzone, so every route bend braked the car.
        if (err >= 0) wantRight = true;
        else          wantLeft  = true;
    } else {
        // #67 v0.18.3.83: FORWARD-COLLISION GUARD. off<=STEER_DEADZONE means
        // "roughly aimed" -- but roughly-aimed-into-a-cliff is still a cliff. The
        // .82 BAT wedged with off=318 just inside the ~320 deadzone, walking due
        // north into the blocked fine(103,63) the route goes WEST around (hdg
        // frozen at 36 the whole time -- he never pivoted because off never
        // exceeded the deadzone, and the reverse just backed him into the same
        // wall again). So before committing UP, look where he's about to step:
        // probe the fine cell ~1 cell ahead of the CURRENT facing/camera-up, and
        // if it's blocked for foot/car, do NOT go straight -- press toward the
        // target side (err sign) instead. On this region's screen-relative
        // controls that walks him sideways into the open corridor; on a
        // turn-then-go region it pivots him there. Either way he stops nosing into
        // the rock. The guard reads the grid in front of him, so it fires ONLY
        // when something is actually there -- open-road steering and the deadzone
        // are unchanged (no orbit/oscillation regression).
        // #70 v0.18.3.109: skip the forward-collision guard on a navmesh funnel
        // path (same reason as the LOS clamp -- it reads the coarse grid that
        // marks the navmesh-walkable .81-box / false-coast cells blocked). On the
        // navmesh path the corridor is trusted; the guard only runs on the
        // fine-grid path / vehicles off-navmesh.
        if (!s_driveNavmeshPath) {
            double th   = (double)heading / 4096.0 * 6.283185307179586;
            double dirX = sin(th), dirY = -cos(th);   // heading 0=N(-Y), clockwise; +X=E
            int32_t aheadX = px + (int32_t)(dirX * 1024.0);   // one fine cell ahead
            int32_t aheadY = py + (int32_t)(dirY * 1024.0);
            int afc = WorldXToFineCol(aheadX), afr = WorldYToFineRow(aheadY);
            if (afc >= 0 && afc < WM_FINE_COLS && afr >= 0 && afr < WM_FINE_ROWS) {
                uint8_t acls = s_walkClassFine[afr][afc];
                if (acls == SEG_OCEAN ||
                    (acls == SEG_MOUNTAIN && s_steepFine[afr][afc] > WM_MTN_STEEP_BLOCK))
                    fwdGuard = true;
            }
        }
        if (fwdGuard) {
            if (err >= 0) wantRight = true;      // toward the target / open route side
            else          wantLeft  = true;
        } else if (off > STEER_DEADZONE) {
            // v0.18.3.258 (#79): ARC band restored -- the state.inl constants
            // (STEER_DEADZONE 320 / STEER_FWD_CONE 576) always described this
            // three-band law but the executor lost the middle gear in the .80
            // rewrite. Between ~28deg and ~50deg of error: hold forward AND
            // turn, exactly how a player drives the car manually -- the vehicle
            // keeps momentum and corrects on the move instead of braking to
            // rotate at every bend. The forward-collision guard above still
            // vetoes forward when the cell ahead is blocked.
            wantUp = true;
            if (err >= 0) wantRight = true;
            else          wantLeft  = true;
        } else {
            wantUp = true;                       // aligned and clear -> drive straight
        }
    }

    // #67 v0.18.3.66/.68: heading-VERIFICATION trace. hdg should rotate toward
    // tgtBrg while PIVOTing/REVERSE, err should shrink, off small on STRAIGHT.
    // Set DRIVE_STEER_DIAG=false before the #67 push.
    if (DRIVE_STEER_DIAG) {
        static DWORD   s_diagLast  = 0;
        static int32_t s_diagLastX = 0;
        static int32_t s_diagLastY = 0;
        if (now - s_diagLast >= (DWORD)DRIVE_STEER_DIAG_INTERVAL_MS) {
            double dmoved = CalculateWrappedDistance(s_diagLastX, s_diagLastY, px, py);
            const char* band = wantDown ? "REVERSE"
                             : (wantUp && (wantLeft || wantRight)) ? "ARC"
                             : (dist < VEH_FINAL_APPROACH_DIST) ? "FINAL"
                             : wantUp  ? "STRAIGHT"
                             : "PIVOT";
            Log::World("WorldMap: [HDG-DIAG] pos(%d,%d) d+%.0f | hdg=%u tgtBrg=%d err=%+d off=%d | steer(%d,%d) dist=%.0f idx=%d/%d | keys=%s%s%s%s %s%s%s",
                       px, py, dmoved, (unsigned)heading,
                       targetBearing, err, off,
                       steerX, steerY, dist, s_drivePathIdx, s_drivePathLen,
                       wantUp ? "U" : "-", wantDown ? "D" : "-",
                       wantLeft ? "L" : "-", wantRight ? "R" : "-",
                       band, wallJam ? " JAM" : "", fwdGuard ? " GUARD" : "");
            s_diagLast  = now;
            s_diagLastX = px;
            s_diagLastY = py;
        }
    }
    }  // end vehicle (else) heading-based steering

    SetDriveKeys(wantUp, wantLeft, wantRight, wantDown);

    // #67 v0.18.3.74: record this tick's keys + position for the next-tick
    // on-foot basis refresh (the live screen->world calibration).
    s_drivePrevUp      = wantUp;
    s_drivePrevDown    = wantDown;
    s_drivePrevLeft    = wantLeft;
    s_drivePrevRight   = wantRight;
    s_drivePrevX       = px;
    s_drivePrevY       = py;
    s_drivePrevHadKeys = (wantUp || wantDown || wantLeft || wantRight);
}
