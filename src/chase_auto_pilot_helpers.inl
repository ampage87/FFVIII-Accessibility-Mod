// chase_auto_pilot_helpers.inl -- per-field lookup and small predicates
//
// Textual include from chase_auto_pilot.cpp, after _state.inl, _route.inl,
// and _io.inl. Holds:
//   - IsDirectionLikeMode  : does this mode use StartDirectionDrive?
//   - PickStageIdx         : pick MODE_STAGED_DIRECTION stage by Y
//   - LookupConfig         : find an explicit FieldConfig by name
//   - BuildFallbackConfig  : synthesize a MODE_TARGET fallback when
//                            LookupConfig misses (largest cluster /
//                            trigger line / INF gateway tiers)
//   - DirectionName        : compass name for log messages

// v0.15.9.7: Helper -- does this mode use direction-drive plumbing?
// MODE_DIRECTION, MODE_STAGED_DIRECTION, and (v0.15.9.8.3) MODE_BRIDGE_DANCE
// all call StartDirectionDrive / StopDirectionDrive. MODE_TARGET uses
// StartChaseDrive instead.
static inline bool IsDirectionLikeMode(FieldDriveMode mode)
{
    return mode == MODE_DIRECTION
        || mode == MODE_STAGED_DIRECTION
        || mode == MODE_BRIDGE_DANCE;
}

// v0.15.9.7: Helper -- find the active stage for a given Y position.
// Walks the stage array (DECREASING activeMinY order) and returns the
// index of the first stage whose activeMinY is <= posY. The last stage
// must have activeMinY = INT32_MIN so this always returns a valid index
// (count - 1 worst case).
static int PickStageIdx(const FieldStage* stages, int count, int32_t posY)
{
    if (stages == nullptr || count <= 0) return -1;
    for (int i = 0; i < count; ++i) {
        if (posY >= stages[i].activeMinY) return i;
    }
    return count - 1;  // fallback (last stage matches anything if activeMinY=INT32_MIN)
}

// ============================================================================
// Config lookup
// ============================================================================

static const FieldConfig* LookupConfig(const char* fieldName)
{
    if (fieldName == nullptr || *fieldName == '\0') return nullptr;
    for (int i = 0; i < kFieldConfigsCount; ++i) {
        if (std::strcmp(fieldName, kFieldConfigs[i].fieldName) == 0)
            return &kFieldConfigs[i];
    }
    return nullptr;
}

static const FieldConfig* BuildFallbackConfig(const char* fieldName)
{
    if (fieldName == nullptr || *fieldName == '\0') return nullptr;
    int32_t tgtX = 0, tgtY = 0;
    // v0.15.9.2.15: Three-tier target preference for chase fallback:
    //   1. INF gateway crossing line  -- the engine's actual screen-transition
    //      exit, with explicit destination field ID. Picked by direction-
    //      alignment with the cluster (not nearest-to-cluster, because the
    //      entry-back gateway can be geometrically closer; see header).
    //   2. SETLINE trigger nearest cluster -- works on fields whose JSM Line
    //      entities are SCREEN_BOUND/UNKNOWN, not on Event Trigger chase fields
    //      like domt2_1. v0.15.9.2.14's primary mechanism. Still useful for
    //      non-chase fallback uses of this path.
    //   3. Cluster center only -- plain point-distance arrival as last resort.
    //      Chase fields hit this path only when both gateway and SETLINE
    //      lookups fail (rare; should never happen in practice).
    s_fallbackTriggerLineIdx = -1;
    s_fallbackGwLineX1 = 0; s_fallbackGwLineY1 = 0;
    s_fallbackGwLineX2 = 0; s_fallbackGwLineY2 = 0;

    int32_t gwX1 = 0, gwY1 = 0, gwX2 = 0, gwY2 = 0;
    bool gotGw = FieldNavigation::GetGatewayNearestCluster(&tgtX, &tgtY,
                                                           &gwX1, &gwY1,
                                                           &gwX2, &gwY2);
    int trigIdx = -1;
    if (gotGw) {
        s_fallbackGwLineX1 = gwX1; s_fallbackGwLineY1 = gwY1;
        s_fallbackGwLineX2 = gwX2; s_fallbackGwLineY2 = gwY2;
    } else {
        bool gotTrig = FieldNavigation::GetTriggerLineNearestCluster(&tgtX, &tgtY, &trigIdx);
        if (gotTrig) {
            s_fallbackTriggerLineIdx = trigIdx;
        } else {
            if (!FieldNavigation::GetLargestClusterCenter(&tgtX, &tgtY)) {
                Log::Field("ChaseAutoPilot: fallback for field='%s' UNAVAILABLE "
                           "(walkmesh not loaded; no gateways, no triggers, no clusters)",
                           fieldName);
                return nullptr;
            }
        }
    }
    std::strncpy(s_fallbackFieldName, fieldName, sizeof(s_fallbackFieldName) - 1);
    s_fallbackFieldName[sizeof(s_fallbackFieldName) - 1] = '\0';
    s_fallbackConfig.fieldName = s_fallbackFieldName;
    s_fallbackConfig.mode      = MODE_TARGET;
    s_fallbackConfig.dirX      = 0;
    s_fallbackConfig.dirY      = 0;
    s_fallbackConfig.targetX   = tgtX;
    s_fallbackConfig.targetY   = tgtY;
    // v0.15.9.2.12: Default to RUNNING (walk=false). The chase as a whole is
    // Squall fleeing X-ATM092 at top speed; running is the right default for
    // any chase field we don't have an explicit config for. Aaron confirmed
    // after the v0.15.9.2.11 BAT: "the party should be running on this field
    // not walking" (re: domt2_1). The walking-mode exception (Aaron's AI rule
    // #1) applies only to domt5_1 where running shakes the cliff path and
    // the party gets caught -- that field has an explicit config with
    // walk=true. Previous default of walk=true was a misreading.
    s_fallbackConfig.walk      = false;
    if (gotGw) {
        Log::Field("ChaseAutoPilot: fallback config built for field='%s' mode=TARGET "
                   "tgt=(%d,%d) walk=0 running INF-GATEWAY line(%d,%d)->(%d,%d) (cross-product detection)",
                   fieldName, (int)tgtX, (int)tgtY,
                   (int)gwX1, (int)gwY1, (int)gwX2, (int)gwY2);
    } else if (trigIdx >= 0) {
        Log::Field("ChaseAutoPilot: fallback config built for field='%s' mode=TARGET "
                   "tgt=(%d,%d) walk=0 running TRIGGER-LINE idx=%d (cross-product detection)",
                   fieldName, (int)tgtX, (int)tgtY, trigIdx);
    } else {
        Log::Field("ChaseAutoPilot: fallback config built for field='%s' mode=TARGET "
                   "tgt=(%d,%d) walk=0 running (cluster-center fallback, no gateway/trigger found)",
                   fieldName, (int)tgtX, (int)tgtY);
    }
    return &s_fallbackConfig;
}

// Compass name for log messages. Returns a short descriptive string for
// the (dirX, dirY) pair so the FREEZE/ENGAGED log lines are readable.
static const char* DirectionName(int8_t dirX, int8_t dirY)
{
    if (dirX == 0 && dirY == 0) return "target";
    if (dirX == 0 && dirY <  0) return "north";
    if (dirX == 0 && dirY >  0) return "south";
    if (dirX <  0 && dirY == 0) return "west";
    if (dirX >  0 && dirY == 0) return "east";
    if (dirX <  0 && dirY <  0) return "northwest";
    if (dirX >  0 && dirY <  0) return "northeast";
    if (dirX <  0 && dirY >  0) return "southwest";
    if (dirX >  0 && dirY >  0) return "southeast";
    return "?";
}
