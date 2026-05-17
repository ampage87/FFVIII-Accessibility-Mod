// chase_auto_pilot_engage.inl -- Engage / Disengage transitions
//
// Textual include from chase_auto_pilot.cpp, after _io.inl, _helpers.inl,
// _diag.inl, _bridge.inl. Holds the two functions that flip s_engaged and
// install / tear down the underlying FieldNavigation drives:
//   - Engage    : install direction-drive, staged-direction, bridge-dance,
//                 or chase-drive based on cfg->mode; activate the
//                 synthetic keyboard buffer
//   - Disengage : tear down the appropriate drive, log a completion gate
//                 if reason matches, deactivate the synthetic keyboard,
//                 reset all cached engaged state

static void Engage(const FieldConfig* cfg)
{
    if (cfg == nullptr) return;

    // v0.15.9.11.3: Activate the synthetic keyboard buffer BEFORE installing
    // the analog override + injecting keys. Once active, our GetDeviceState
    // detour returns the synthetic buffer instead of real DirectInput state,
    // so the user's physical key presses no longer reach the engine. The
    // auto-pilot's InjectKey calls below ALSO update the synthetic buffer
    // (gated by ChaseKeyboard::IsActive() inside InjectKey itself), so the
    // engine sees exactly the keys the auto-pilot wants pressed -- no more,
    // no less. Activate clears the buffer to known-empty state.
    ChaseKeyboard::Activate();

    bool ok = false;
    if (cfg->mode == MODE_DIRECTION) {
        FieldNavigation::StartDirectionDrive(cfg->dirX, cfg->dirY, cfg->walk);
        // StartDirectionDrive doesn't return a status; assume success unless
        // log evidence proves otherwise. F9 mutex is the only known refusal
        // cause and chase auto-pilot doesn't engage while F9 runs.
        ok = true;
    } else if (cfg->mode == MODE_STAGED_DIRECTION) {
        // v0.15.9.7: Multi-stage direction drive. Pick the initial stage by
        // current Y position and start direction-drive with that stage's
        // params. Per-tick refresh in Update() re-picks the stage each tick
        // and updates s_engagedDirX/Y/Walk when the active stage changes.
        if (cfg->stages != nullptr && cfg->stageCount > 0) {
            int32_t pX = 0, pY = 0;
            bool gotPos = ReadSquallPosition(pX, pY);
            if (!gotPos) {
                Log::Field("ChaseAutoPilot: MODE_STAGED_DIRECTION on '%s' pos read failed at "
                           "engage; defaulting to first stage", cfg->fieldName);
                pY = INT32_MAX;
            }
            int idx = PickStageIdx(cfg->stages, cfg->stageCount, pY);
            if (idx >= 0 && idx < cfg->stageCount) {
                const FieldStage* stg = &cfg->stages[idx];
                FieldNavigation::StartDirectionDrive(stg->dirX, stg->dirY, stg->walk);
                s_currentStageIdx = idx;
                Log::Field("ChaseAutoPilot: MODE_STAGED_DIRECTION on '%s' initial pos=(%d,%d) -> "
                           "stage %d/%d dir=(%d,%d) walk=%d (activeMinY=%d)",
                           cfg->fieldName, (int)pX, (int)pY,
                           idx, (int)cfg->stageCount,
                           (int)stg->dirX, (int)stg->dirY, (int)stg->walk,
                           (int)stg->activeMinY);
                ok = true;
            } else {
                Log::Field("ChaseAutoPilot: MODE_STAGED_DIRECTION on '%s' PickStageIdx "
                           "failed for posY=%d (stageCount=%d)",
                           cfg->fieldName, (int)pY, (int)cfg->stageCount);
            }
        } else {
            Log::Field("ChaseAutoPilot: MODE_STAGED_DIRECTION on '%s' has no stages defined",
                       cfg->fieldName);
        }
    } else if (cfg->mode == MODE_BRIDGE_DANCE) {
        // v0.15.9.8.3: Bridge dance starts in EAST_LEG state, driving east
        // at running speed via the existing direction-drive plumbing.
        // UpdateBridgeDance() flips s_engagedDirX/Y when the state machine
        // transitions, and the per-tick StartDirectionDrive refresh below
        // picks up the new values via its already-running diff branch.
        FieldNavigation::StartDirectionDrive(+1, 0, /*walk=*/false);
        s_bridgeDanceState        = BRIDGE_DANCE_EAST;
        s_bridgeLastKaniValid     = false;
        s_bridgeSampleCounter     = 0;
        s_bridgeConsecLandSamples = 0;
        s_bridgeWasLeaping        = false;
        s_bridgeTicksSinceXition  = 0;
        s_bridgeLeapCount         = 0;
        ok = true;
    } else if (cfg->mode == MODE_TARGET) {
        // v0.15.9.2: path-finding drive. StartChaseDrive validates state
        // (no F9 active, no dialog open, on field) and returns false on
        // failure. We don't retry -- if it fails on this Update tick, the
        // gate must have flipped (e.g., dialog opened) and we'll try again
        // on the next tick when the gate re-evaluates.
        //
        // v0.15.9.2.14: Pass the trigger-line index. Only the fallback path
        // (BuildFallbackConfig) sets s_fallbackTriggerLineIdx >= 0 currently.
        // Explicit per-field configs use point targets (pass -1). When the
        // index is >= 0, chase-drive enables cross-product sign-flip line-
        // crossing detection -- the player walks ONTO the line and the drive
        // stops the instant they cross, which is what fires FF8's screen
        // transition.
        //
        // v0.15.9.2.15: Also pass INF gateway crossing-line endpoints. Set
        // by BuildFallbackConfig when GetGatewayNearestCluster succeeded.
        // Mutually exclusive with the trigger-line index (only one wins per
        // engagement). Explicit per-field configs pass all zeros = no
        // crossing line, plain point arrival.
        int trigIdx = -1;
        int32_t gwX1 = 0, gwY1 = 0, gwX2 = 0, gwY2 = 0;
        if (cfg == &s_fallbackConfig) {
            trigIdx = s_fallbackTriggerLineIdx;
            gwX1 = s_fallbackGwLineX1; gwY1 = s_fallbackGwLineY1;
            gwX2 = s_fallbackGwLineX2; gwY2 = s_fallbackGwLineY2;
        }
        ok = FieldNavigation::StartChaseDrive(cfg->targetX, cfg->targetY,
                                              trigIdx,
                                              gwX1, gwY1, gwX2, gwY2,
                                              cfg->walk);
    }

    if (!ok) {
        Log::Field("ChaseAutoPilot: failed to engage on field='%s' mode=%d",
                   cfg->fieldName, (int)cfg->mode);
        return;
    }

    std::strncpy(s_engagedField, cfg->fieldName, sizeof(s_engagedField) - 1);
    s_engagedField[sizeof(s_engagedField) - 1] = '\0';
    s_engaged          = true;
    s_engagedMode      = cfg->mode;
    s_engagedTargetX   = cfg->targetX;
    s_engagedTargetY   = cfg->targetY;
    s_diagTickCounter  = 0;

    if (cfg->mode == MODE_STAGED_DIRECTION) {
        // v0.15.9.7: For STAGED mode, s_engagedDirX/Y/Walk track the active
        // stage (set by the engagement branch above), not cfg->dirX/Y/Walk.
        // s_currentStageIdx was set by the staged branch in the if/else.
        const FieldStage* stg = &cfg->stages[s_currentStageIdx];
        s_engagedDirX       = stg->dirX;
        s_engagedDirY       = stg->dirY;
        s_engagedWalk       = stg->walk;
        s_engagedStages     = cfg->stages;
        s_engagedStageCount = cfg->stageCount;
    } else {
        s_engagedDirX       = cfg->dirX;
        s_engagedDirY       = cfg->dirY;
        s_engagedWalk       = cfg->walk;
        s_engagedStages     = nullptr;
        s_engagedStageCount = 0;
        s_currentStageIdx   = -1;
    }

    if (cfg->mode == MODE_DIRECTION) {
        Log::Field("ChaseAutoPilot: ENGAGED on field='%s' mode=DIRECTION direction=%s "
                   "%s (dirX=%d dirY=%d walk=%d)",
                   cfg->fieldName, DirectionName(cfg->dirX, cfg->dirY),
                   cfg->walk ? "WALKING" : "running",
                   (int)cfg->dirX, (int)cfg->dirY, (int)cfg->walk);
    } else if (cfg->mode == MODE_STAGED_DIRECTION) {
        Log::Field("ChaseAutoPilot: ENGAGED on field='%s' mode=STAGED_DIRECTION "
                   "starting stage %d/%d direction=%s %s (dirX=%d dirY=%d walk=%d)",
                   cfg->fieldName, (int)s_currentStageIdx, (int)cfg->stageCount,
                   DirectionName(s_engagedDirX, s_engagedDirY),
                   s_engagedWalk ? "WALKING" : "running",
                   (int)s_engagedDirX, (int)s_engagedDirY, (int)s_engagedWalk);
    } else if (cfg->mode == MODE_BRIDGE_DANCE) {
        Log::Field("ChaseAutoPilot: ENGAGED on field='%s' mode=BRIDGE_DANCE "
                   "initial state=EAST_LEG direction=east running (dirX=+1 dirY=0 walk=0). "
                   "Will turn west when kani lands in front, east when kani leaps; "
                   "west-leg timeout %d ticks (~5s) for safety.",
                   cfg->fieldName, (int)kBridgeWestTimeoutTicks);
    } else {
        Log::Field("ChaseAutoPilot: ENGAGED on field='%s' mode=TARGET tgt=(%d,%d) "
                   "%s (walk=%d)",
                   cfg->fieldName, (int)cfg->targetX, (int)cfg->targetY,
                   cfg->walk ? "WALKING" : "running",
                   (int)cfg->walk);
    }
}

static void Disengage(const char* reason)
{
    if (!s_engaged) return;
    if (IsDirectionLikeMode(s_engagedMode)) {
        FieldNavigation::StopDirectionDrive();
    } else {
        FieldNavigation::StopChaseDrive();
    }

    // v0.15.9.11.3: Deactivate the synthetic keyboard buffer. From here on,
    // GetDeviceState pass-through is restored; any physical key presses from
    // the user reach FF8 again. Note that the auto-pilot's StopDirectionDrive
    // / StopChaseDrive above already released arrow keys via SendInput, so
    // the engine sees a clean key-up transition for anything that was held.
    ChaseKeyboard::Deactivate();

    Log::Field("ChaseAutoPilot: DISENGAGED (%s) was on field='%s' mode=%d",
               reason ? reason : "?", s_engagedField, (int)s_engagedMode);

    // v0.15.9.2.11: When chase-drive completes (Arrived or Stuck) mark
    // this field as auto-pilot-done so we don't re-engage in a loop. The
    // reason string from the per-tick refresh path is
    // "chase-drive completed (target reached or stuck)". Match a stable
    // substring rather than the whole string in case the reason text is
    // ever refined.
    if (reason && std::strstr(reason, "chase-drive completed") != nullptr &&
        s_engagedField[0] != '\0') {
        std::strncpy(s_completedField, s_engagedField, sizeof(s_completedField) - 1);
        s_completedField[sizeof(s_completedField) - 1] = '\0';
        Log::Field("ChaseAutoPilot: field '%s' marked auto-pilot complete; "
                   "won't re-engage until field changes", s_completedField);
    }

    s_engagedField[0]  = '\0';
    s_engaged          = false;
    s_engagedMode      = MODE_DIRECTION;
    s_engagedDirX      = 0;
    s_engagedDirY      = 0;
    s_engagedTargetX   = 0;
    s_engagedTargetY   = 0;
    s_engagedWalk      = false;
    s_diagTickCounter  = 0;
    // v0.15.9.8.3: Reset bridge dance state.
    s_bridgeDanceState        = BRIDGE_DANCE_EAST;
    s_bridgeLastKaniValid     = false;
    s_bridgeSampleCounter     = 0;
    s_bridgeConsecLandSamples = 0;
    s_bridgeWasLeaping        = false;
    s_bridgeTicksSinceXition  = 0;
    s_bridgeLeapCount         = 0;
    // v0.15.9.7: Reset staged-direction state.
    s_engagedStages     = nullptr;
    s_engagedStageCount = 0;
    s_currentStageIdx   = -1;
}
