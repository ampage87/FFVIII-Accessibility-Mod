// chase_auto_pilot_diag.inl -- pre-engage chase-active diagnostic helper
//
// Textual include from chase_auto_pilot.cpp, after _io.inl and _helpers.inl
// (uses ReadSquallPosition, ReadKaniPosition, DistSquared, IntSqrt,
// IsDirectionLikeMode, and the cached s_engaged* state).
//
// Currently retired in v0.15.9.11.3.7 -- the function early-returns. The
// machinery is kept intact below the return so it remains compilable and
// ready to revive if future camera-orientation research needs the
// (analog, world-delta) pair capture again. The call site in Update() is
// kept too so we don't have to re-wire it; the early-return makes it a
// no-op.

// ============================================================================
// v0.15.9.3: Chase-active diagnostic helper
// ============================================================================
//
// Pre-engage logging: runs once per second from the moment ChaseDetector
// reports chase_active = true (typically on entry to a chase field) through
// chase deactivation. Independent of chase_auto_pilot engagement state, so
// it covers the ASK window (where the existing engaged tick log is silent
// because chase_auto_pilot hasn't engaged yet).
//
// Output format:
//   ChaseActiveDiag: field='X' state=PRE-ENGAGE|ENGAGED-DIR|ENGAGED-TGT
//     pos=(pX,pY) delta=(dX,dY) dmag=N kani=(kX,kY) kdist=K [analog=(lX,lY)]
//
// `delta` is the difference between this tick's pos and the previous tick's
// pos (s_prevPosX/Y). `dmag` is the magnitude of that delta. `kdist` is the
// squall-kani distance. When the auto-pilot is engaged, `analog=(lX,lY)`
// reports the analog values we're injecting (so the post-BAT analysis can
// correlate analog -> world delta).
static void LogChaseActiveDiagnostic(const char* fieldName)
{
    // v0.15.9.11.3.7: function retired. Originally added in v0.15.9.3 to
    // derive camera orientation for the v0.15.9.4 domt4_1 / v0.15.9.5
    // domt3_2 / v0.15.9.6 domt5_1 direction configs by capturing the
    // (analog, world-delta) pairs during chase Auto. That research is
    // complete; the per-second log is now pure noise (especially during
    // the post-chase disc00_07h FMV where it fires for 74s with delta=0).
    // Early-return keeps the call sites compiling without modification.
    // If future camera research is needed, remove this gate and the data
    // capture comes back online.
    (void)fieldName;
    return;

    int32_t pX = 0, pY = 0;
    bool gotPos = ReadSquallPosition(pX, pY);

    int32_t kX = 0, kY = 0;
    bool gotKani = ReadKaniPosition(kX, kY);

    // Compose the delta substring from previous-tick pos. First tick of
    // chase: s_prevPosValid is false, delta is "N/A".
    char deltaBuf[64];
    if (gotPos && s_prevPosValid) {
        int32_t dX = pX - s_prevPosX;
        int32_t dY = pY - s_prevPosY;
        int32_t dMag = IntSqrt(DistSquared(0, 0, dX, dY));
        std::snprintf(deltaBuf, sizeof(deltaBuf),
                      " delta=(%d,%d) dmag=%d", (int)dX, (int)dY, (int)dMag);
    } else {
        std::snprintf(deltaBuf, sizeof(deltaBuf), " delta=N/A");
    }

    // Compose the kani substring.
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

    // Compose the state and (if engaged) analog substring.
    const char* stateStr;
    char analogBuf[64];
    analogBuf[0] = '\0';
    if (s_engaged) {
        if (IsDirectionLikeMode(s_engagedMode)) {
            stateStr = (s_engagedMode == MODE_STAGED_DIRECTION) ? "ENGAGED-STG" : "ENGAGED-DIR";
            int32_t lX = (int32_t)s_engagedDirX * 1000;
            int32_t lY = (int32_t)s_engagedDirY * 1000;
            std::snprintf(analogBuf, sizeof(analogBuf),
                          " analog=(%d,%d)", (int)lX, (int)lY);
        } else {
            stateStr = "ENGAGED-TGT";
            // MODE_TARGET doesn't have a stable analog -- the path-finder
            // changes it per tick to steer toward the current waypoint.
            // The [drive] log line elsewhere captures per-tick analog.
            std::snprintf(analogBuf, sizeof(analogBuf),
                          " tgt=(%d,%d)",
                          (int)s_engagedTargetX, (int)s_engagedTargetY);
        }
    } else {
        stateStr = "PRE-ENGAGE";
    }

    if (gotPos) {
        Log::Field("ChaseActiveDiag: field='%s' state=%s pos=(%d,%d)%s%s%s",
                   fieldName, stateStr, (int)pX, (int)pY,
                   deltaBuf, kaniBuf, analogBuf);
    } else {
        Log::Field("ChaseActiveDiag: field='%s' state=%s pos=READ_FAILED%s%s%s",
                   fieldName, stateStr, deltaBuf, kaniBuf, analogBuf);
    }

    // Update prev-pos for the NEXT tick's delta computation. Done last so
    // any future logging on this same tick (e.g. the engaged-branch tick log)
    // sees the pre-update value of s_prevPos.
    if (gotPos) {
        s_prevPosX = pX;
        s_prevPosY = pY;
        s_prevPosValid = true;
    }
}
