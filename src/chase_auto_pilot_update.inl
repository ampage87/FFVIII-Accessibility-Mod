// chase_auto_pilot_update.inl -- per-tick Update function
//
// Textual include from chase_auto_pilot.cpp, LAST in the .inl chain.
// Depends on everything earlier in the chain:
//   - _state.inl:    s_* state, FieldDriveMode/FieldStage/FieldConfig types
//   - _route.inl:    (unused directly here; LookupConfig consumes it)
//   - _io.inl:       ReadSquallPosition, ReadKaniPosition, DistSquared, IntSqrt
//   - _helpers.inl:  IsDirectionLikeMode, PickStageIdx, LookupConfig,
//                    BuildFallbackConfig
//   - _diag.inl:     LogChaseActiveDiagnostic (currently a no-op stub)
//   - _bridge.inl:   UpdateBridgeDance
//   - _engage.inl:   Engage, Disengage
//
// The slim parent chase_auto_pilot.cpp wraps this with a `void Update()`
// declaration -- no, actually the function definition lives here. The
// slim parent only includes this .inl in the namespace block; the
// definition becomes the public API entry point.

void Update()
{
    if (!s_initialized) return;

    // v0.15.9.2.10: ASK gate state machine. Track chase-activation
    // transitions to reset the gate, and watch IsAskActive transitions to
    // detect when the ASK has been answered. See the state-declaration
    // comment block above for the full rationale.
    bool chaseActive = ChaseDetector::IsChaseActive();
    if (chaseActive && !s_prevChaseActive) {
        // Chase just started. Re-arm the gate -- a fresh ASK answer is
        // required for this chase session.
        s_askWasActive = false;
        s_askAnswered  = false;
        // v0.15.9.3: Reset diagnostic state. First tick of chase has no
        // previous-position sample (delta will print "N/A").
        s_chaseActiveTickCounter = 0;
        s_prevPosValid           = false;
        Log::Field("ChaseAutoPilot: chase activated, waiting for ASK to fire and be answered before engaging");
    } else if (!chaseActive && s_prevChaseActive) {
        // Chase just ended. Clear the gate state for next session.
        s_askWasActive = false;
        s_askAnswered  = false;
        // v0.15.9.3: Reset diagnostic state for next chase session.
        s_chaseActiveTickCounter = 0;
        s_prevPosValid           = false;
    }
    s_prevChaseActive = chaseActive;

    // v0.15.9.3: Pre-engage chase-active diagnostic. Fires once per second
    // (60 Update ticks) while chaseActive is true, regardless of engagement
    // state. Captures the ASK window plus the engaged-state ticks. Field
    // name from ChaseDetector's debounced name; uses "(name not settled)"
    // during the 2-second post-transition debounce so the log line still
    // appears even before the name resolves.
    if (chaseActive) {
        s_chaseActiveTickCounter++;
        if (s_chaseActiveTickCounter >= 60) {
            const char* fnameForDiag = ChaseDetector::GetDebouncedFieldName();
            if (fnameForDiag == nullptr || *fnameForDiag == '\0') {
                fnameForDiag = "(name not settled)";
            }
            LogChaseActiveDiagnostic(fnameForDiag);
            s_chaseActiveTickCounter = 0;
        }
    }

    if (chaseActive) {
        bool askActiveNow = ChaseAskOverlay::IsAskActive();
        if (askActiveNow) {
            if (!s_askWasActive) {
                s_askWasActive = true;
                Log::Field("ChaseAutoPilot: chase ASK observed open (auto-pilot stays disengaged)");
            }
        } else if (s_askWasActive && !s_askAnswered) {
            // ASK was open and is now closed -- user must have selected.
            s_askAnswered = true;
            Log::Field("ChaseAutoPilot: chase ASK answered, engagement gate is now open");
        }
    }

    // Engagement gate: chase field, auto mode, on field, AND chase ASK has
    // been answered for this chase session.
    bool inChaseField = ChaseDetector::IsInChaseField();
    bool autoMode     = (ChaseDetector::GetChaseMode() == ChaseDetector::MODE_AUTO);
    bool onField      = FF8Addresses::IsOnField();

    bool wantEngage = inChaseField && autoMode && onField && s_askAnswered;

    if (!wantEngage) {
        if (s_engaged) {
            const char* reason =
                !inChaseField ? "left chase field"             :
                !autoMode     ? "mode != auto"                  :
                !onField      ? "off-field (battle/menu/etc.)" :
                                "ASK not yet answered";
            Disengage(reason);
        }
        return;
    }

    // Engagement window is open. Get the debounced field name.
    const char* fieldName = ChaseDetector::GetDebouncedFieldName();
    if (fieldName == nullptr || *fieldName == '\0') {
        // Field name not yet settled (during the 2s name-debounce after
        // a transition). Don't engage yet; release any prior held state.
        if (s_engaged) Disengage("field name not settled");
        return;
    }

    // Field changed since last engagement? Disengage cleanly so the
    // new field starts with fresh direction-drive state.
    if (s_engaged && std::strcmp(s_engagedField, fieldName) != 0) {
        Disengage("field changed");
    }

    // v0.15.9.2.11: Field changed since last completion? Clear the
    // completion marker so the new field can be auto-piloted from scratch.
    if (s_completedField[0] != '\0' && std::strcmp(s_completedField, fieldName) != 0) {
        Log::Field("ChaseAutoPilot: field changed from completed field '%s' to '%s'; "
                   "clearing completion marker",
                   s_completedField, fieldName);
        s_completedField[0] = '\0';
    }

    // v0.15.9.2.11: If we've already completed auto-pilot on this field,
    // refuse to re-engage. Prevents the engage/arrive/disengage loop
    // discovered in the v0.15.9.2.10 BAT on domt2_1.
    if (s_completedField[0] != '\0' && std::strcmp(s_completedField, fieldName) == 0) {
        return;
    }

    // v0.15.9.2.7: Per-tick refresh path FIRST, before any config lookup.
    // v0.15.9.2.6 ran LookupConfig() and BuildFallbackConfig() at the top
    // of every Update tick. BuildFallbackConfig() always logs when it
    // succeeds, so on a fallback-engaged field it flooded the log with
    // hundreds of "fallback config built" lines per second (one per tick,
    // ~60Hz) — confirmed in the v0.15.9.2.6 BAT field log on domt2_1:
    // 16 seconds of stuck-at-wp-13 buried under ~960 spam messages, with
    // any v0.15.9.2.5 advance-on-stuck logs presumably drowned out.
    //
    // Fix: when we're already engaged on the same field, run the per-tick
    // refresh and diagnostic using the cached s_engagedX state (set by
    // Engage() once at fresh engagement). The config pointer is only
    // needed for fresh engagement; the engaged-state cache is sufficient
    // for everything else.
    if (s_engaged && std::strcmp(s_engagedField, fieldName) == 0) {
        // Already engaged on this field. Per-tick refresh depends on mode:
        //
        // MODE_DIRECTION: re-call StartDirectionDrive every tick. The API's
        // "already running" branch is idempotent and runs the keep-alive
        // pulse cycle (see field_nav_directiondrive.inl) so the engine
        // sees fresh KEYDOWN events periodically.
        //
        // MODE_TARGET: path-finding runs autonomously inside the F9 update
        // state machine (UpdateAutoDrive in field_nav_autodrive.inl). We
        // just verify it's still active. If chase-drive completed (arrived
        // at target or stuck-detection gave up), disengage so the player
        // knows we're done. The field-change branch above handles the
        // happy case where reaching the target triggered a field exit.
        if (IsDirectionLikeMode(s_engagedMode)) {
            // v0.15.9.7: For MODE_STAGED_DIRECTION, re-pick the active stage
            // based on current Y position. If it differs from the previously-
            // active stage, update s_engagedDirX/Y/Walk to the new stage's
            // values and log the transition. StartDirectionDrive's already-
            // running branch (see field_nav_directiondrive.inl) then picks up
            // the new analog/arrow values cleanly on the call below.
            if (s_engagedMode == MODE_STAGED_DIRECTION &&
                s_engagedStages != nullptr && s_engagedStageCount > 0) {
                int32_t pX = 0, pY = 0;
                if (ReadSquallPosition(pX, pY)) {
                    int newIdx = PickStageIdx(s_engagedStages, s_engagedStageCount, pY);
                    if (newIdx >= 0 && newIdx < s_engagedStageCount &&
                        newIdx != s_currentStageIdx) {
                        const FieldStage* stg = &s_engagedStages[newIdx];
                        Log::Field("ChaseAutoPilot: STAGED stage transition %d->%d at "
                                   "pos=(%d,%d) new dir=(%d,%d) walk=%d (activeMinY=%d)",
                                   (int)s_currentStageIdx, (int)newIdx,
                                   (int)pX, (int)pY,
                                   (int)stg->dirX, (int)stg->dirY, (int)stg->walk,
                                   (int)stg->activeMinY);
                        s_engagedDirX     = stg->dirX;
                        s_engagedDirY     = stg->dirY;
                        s_engagedWalk     = stg->walk;
                        s_currentStageIdx = newIdx;
                    }
                }
            }
            // v0.15.9.8.3: Bridge dance per-tick update. Owns the EAST/WEST
            // state machine on domt1_1; updates s_engagedDirX/Y when it
            // decides to flip direction, which the StartDirectionDrive
            // refresh below picks up on the same tick.
            if (s_engagedMode == MODE_BRIDGE_DANCE) {
                UpdateBridgeDance(fieldName);
            }
            FieldNavigation::StartDirectionDrive(s_engagedDirX, s_engagedDirY, s_engagedWalk);
        } else {
            if (!FieldNavigation::IsChaseDriveActive()) {
                Disengage("chase-drive completed (target reached or stuck)");
                return;
            }
        }

        // Per-second diagnostic, using cached engaged state (no cfg lookup).
        // v0.15.9.11.3.7: delta-zero suppression. The post-chase disc00_07h
        // FMV holds ChaseAutoPilot ENGAGED for ~74s with party position
        // frozen at (-210,-1000) -- the original tick logger fired 74 identical
        // lines, dominating ff8_field.log. New behavior: log the first idle
        // sample so the freeze is recorded, suppress subsequent identical
        // samples, log a RESUMED line on first motion after >=2 idle samples.
        static int32_t s_lastTickLogX     = 0;
        static int32_t s_lastTickLogY     = 0;
        static bool    s_lastTickLogValid = false;
        static int     s_idleTickCount    = 0;
        s_diagTickCounter++;
        if (s_diagTickCounter >= 60) {
            // v0.15.9.1.1: log first, THEN reset, so the printed tick
            // value matches the trigger (60) rather than always reading 0.
            int32_t pX = 0, pY = 0;
            bool gotPos = ReadSquallPosition(pX, pY);

            // v0.15.9.11.3.7: classify this sample for delta-zero
            // suppression. Update the cached prev-pos AFTER deciding
            // whether to suppress so the diff check fires correctly.
            bool isSameAsLast = gotPos && s_lastTickLogValid &&
                                pX == s_lastTickLogX && pY == s_lastTickLogY;
            bool suppressLog  = false;
            if (isSameAsLast) {
                s_idleTickCount++;
                if (s_idleTickCount > 1) suppressLog = true;
            } else {
                if (s_idleTickCount > 1 && gotPos && s_lastTickLogValid) {
                    Log::Field("ChaseAutoPilot: tick log RESUMED after %d idle samples "
                               "at field='%s' pos=(%d,%d)",
                               (int)s_idleTickCount, fieldName, (int)pX, (int)pY);
                }
                s_idleTickCount = 0;
            }
            if (gotPos) {
                s_lastTickLogX     = pX;
                s_lastTickLogY     = pY;
                s_lastTickLogValid = true;
            }

            // v0.15.9.2.8: also read the kani's position and compute the
            // squall-kani distance. This is purely diagnostic -- no behavior
            // change. Goal: test Aaron's collision-push hypothesis. If kdist
            // is consistently small when movement happens (party advances
            // through waypoints) and large when movement stops (stuck), the
            // kani's collision is the actual movement source and the auto-
            // pilot's analog/kb input is doing nothing. Conversely, if the
            // party moves while kani is far away, input injection works at
            // least sometimes and the wp-13 stuck has a different cause.
            int32_t kX = 0, kY = 0;
            bool gotKani = ReadKaniPosition(kX, kY);
            char kaniBuf[96];
            if (gotKani && gotPos) {
                int32_t kdist = IntSqrt(DistSquared(kX, kY, pX, pY));
                std::snprintf(kaniBuf, sizeof(kaniBuf),
                              " kani=(%d,%d) kdist=%d", (int)kX, (int)kY, (int)kdist);
            } else if (gotKani) {
                std::snprintf(kaniBuf, sizeof(kaniBuf),
                              " kani=(%d,%d) kdist=?", (int)kX, (int)kY);
            } else {
                std::snprintf(kaniBuf, sizeof(kaniBuf), " kani=UNRESOLVED");
            }

            // v0.16.1.1: parallel read of battleyarou's position. Same
            // logging shape as kani (" by=(X,Y) bydist=N" / " by=UNRESOLVED")
            // so it slots cleanly into both DIRECTION and TARGET tick lines.
            // See the comment block over ReadBattleyarouPosition for the
            // proximity-vs-timer diagnostic this enables.
            int32_t bX = 0, bY = 0;
            bool gotBy = ReadBattleyarouPosition(bX, bY);
            char byBuf[96];
            if (gotBy && gotPos) {
                int32_t bdist = IntSqrt(DistSquared(bX, bY, pX, pY));
                std::snprintf(byBuf, sizeof(byBuf),
                              " by=(%d,%d) bydist=%d", (int)bX, (int)bY, (int)bdist);
            } else if (gotBy) {
                std::snprintf(byBuf, sizeof(byBuf),
                              " by=(%d,%d) bydist=?", (int)bX, (int)bY);
            } else {
                std::snprintf(byBuf, sizeof(byBuf), " by=UNRESOLVED");
            }

            if (suppressLog) {
                // v0.15.9.11.3.7: idle sample - log suppressed.
            } else if (IsDirectionLikeMode(s_engagedMode)) {
                int32_t lX = (int32_t)s_engagedDirX * 1000;
                int32_t lY = (int32_t)s_engagedDirY * 1000;
                const char* modeStr =
                    (s_engagedMode == MODE_STAGED_DIRECTION) ? "STAGED" :
                    (s_engagedMode == MODE_BRIDGE_DANCE)     ? "BRIDGE_DANCE" :
                                                               "DIRECTION";
                if (gotPos) {
                    Log::Field("ChaseAutoPilot: tick=%d field='%s' mode=%s "
                               "dir=(%d,%d) walk=%d stage=%d pos=(%d,%d) lX=%d lY=%d%s%s",
                               s_diagTickCounter, fieldName, modeStr,
                               (int)s_engagedDirX, (int)s_engagedDirY, (int)s_engagedWalk,
                               (int)s_currentStageIdx,
                               pX, pY, lX, lY, kaniBuf, byBuf);
                } else {
                    Log::Field("ChaseAutoPilot: tick=%d field='%s' mode=%s "
                               "dir=(%d,%d) walk=%d stage=%d pos=READ_FAILED lX=%d lY=%d%s%s",
                               s_diagTickCounter, fieldName, modeStr,
                               (int)s_engagedDirX, (int)s_engagedDirY, (int)s_engagedWalk,
                               (int)s_currentStageIdx,
                               lX, lY, kaniBuf, byBuf);
                }
            } else {
                if (gotPos) {
                    int32_t dx = s_engagedTargetX - pX;
                    int32_t dy = s_engagedTargetY - pY;
                    Log::Field("ChaseAutoPilot: tick=%d field='%s' mode=TARGET "
                               "tgt=(%d,%d) walk=%d pos=(%d,%d) dist=(%d,%d)%s%s",
                               s_diagTickCounter, fieldName,
                               (int)s_engagedTargetX, (int)s_engagedTargetY, (int)s_engagedWalk,
                               pX, pY, dx, dy, kaniBuf, byBuf);
                } else {
                    Log::Field("ChaseAutoPilot: tick=%d field='%s' mode=TARGET "
                               "tgt=(%d,%d) walk=%d pos=READ_FAILED%s%s",
                               s_diagTickCounter, fieldName,
                               (int)s_engagedTargetX, (int)s_engagedTargetY, (int)s_engagedWalk,
                               kaniBuf, byBuf);
                }
            }
            s_diagTickCounter = 0;
        }
        return;
    }

    // Not engaged on this field yet -- look up config (explicit first,
    // then fallback). BuildFallbackConfig's log line fires at most once
    // per fresh engagement now, instead of once per tick.
    const FieldConfig* cfg = LookupConfig(fieldName);
    if (cfg == nullptr) {
        // v0.15.9.2.6: No per-field config -- try the generic fallback.
        // BuildFallbackConfig() asks FieldNavigation for the largest cluster
        // center (typically the main corridor / exit area) and returns a
        // synthesized MODE_TARGET config. If the walkmesh didn't load or no
        // clusters were found, fallback returns null and we behave as before
        // (player drives manually).
        cfg = BuildFallbackConfig(fieldName);
        if (cfg == nullptr) {
            // The strcmp above already disengaged us if we were previously
            // engaged on a different field.
            return;
        }
    }

    // Fresh engagement on this field.
    Engage(cfg);
}
