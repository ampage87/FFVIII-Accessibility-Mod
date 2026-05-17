// chase_auto_pilot_io.inl -- entity-position reads and distance math
//
// Textual include from chase_auto_pilot.cpp, after _state.inl and
// _route.inl. Pure-read helpers that other .inl files (helpers, diag,
// bridge, engage, update) depend on. No internal dependencies of its
// own beyond the FF8Addresses + ChaseDetector externals.
//
// Layout:
//   - ReadSquallPosition (SEH-guarded entity[0] read)
//   - ReadKaniPosition   (SEH-guarded via ChaseDetector slot pointer)
//   - DistSquared        (avoid sqrt for threshold checks)
//   - IntSqrt            (display-only sqrt with std::sqrt under the hood)

// ============================================================================
// Entity[0] (Squall) position read for diagnostic logging
// ============================================================================

// SEH-guarded read of Squall's screen-space position. Returns true and
// fills (x, y) if successful; returns false if the address chain isn't
// resolved or the read faults (e.g. mid-field-load transition).
//
// Coordinates are integer divisions of the fixed-point bytes -- matching
// field_nav_helpers.inl::GetEntityPos so log values line up with anything
// FieldNavigation logs about player position. We don't fall back to the
// 0x20/0x24 simple-int16 path here because in chase fields Squall is
// always actively moving (or being driven by us) and the fixed-point
// path at 0x190/0x194 is always populated.
static bool ReadSquallPosition(int32_t& outX, int32_t& outY)
{
    if (!FF8Addresses::pFieldStateOthers) return false;
    __try {
        uint8_t* base = *reinterpret_cast<uint8_t**>(FF8Addresses::pFieldStateOthers);
        if (!base) return false;
        uint8_t* block = base + ENTITY_STRIDE_OTHERS * 0;  // entity[0] = Squall
        int32_t fpX = *(int32_t*)(block + 0x190);
        int32_t fpY = *(int32_t*)(block + 0x194);
        outX = fpX / 4096;
        outY = fpY / 4096;
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

// v0.15.9.2.8: SEH-guarded read of the kani's (X-ATM092 spider's) world
// position via ChaseDetector::GetKaniEntityPtr(). Returns true and fills
// (x, y) on success; returns false if the kani slot is unresolved in this
// field, the runtime block pointer is null, or the read faults.
//
// Used by the per-second diagnostic to test Aaron's hypothesis (raised
// after v0.15.9.2.7 BAT) that the kani's collision is what pushes Squall
// through chase fields -- not the auto-pilot's analog/keyboard input.
// If the kani sits right behind Squall (small distance) every time wp
// progress occurs, and stays far away when the party is stuck, that
// confirms the collision-push hypothesis and means the entire chase
// auto-pilot premise (input injection) needs rethinking.
//
// Reads at +0x190 (X*4096) / +0x194 (Y*4096), divided by 4096 -- same
// layout as Squall (entity blocks are uniform). The kani entity block
// pointer is owned by ChaseDetector which caches it at field-change time.
static bool ReadKaniPosition(int32_t& outX, int32_t& outY)
{
    uintptr_t kani = ChaseDetector::GetKaniEntityPtr();
    if (kani == 0) return false;
    __try {
        uint8_t* block = reinterpret_cast<uint8_t*>(kani);
        int32_t fpX = *(int32_t*)(block + 0x190);
        int32_t fpY = *(int32_t*)(block + 0x194);
        outX = fpX / 4096;
        outY = fpY / 4096;
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

// v0.16.1.1: SEH-guarded read of the battleyarou's world position via
// ChaseDetector::GetBattleyarouEntityPtr(). Mirrors ReadKaniPosition
// but targets the chase-progress director that fires BATTLE on
// doopen2a. Added to investigate the reproducible v0.16.1 catch on
// Town Square 5: BAT logs show BATTLE fires from entityPtr=0x0188CA04
// (battleyarou, Others slot 6) ~4 seconds after field entry, regardless
// of party position. By logging battleyarou's position alongside kani
// and party on each per-second tick we can distinguish two hypotheses:
//   (a) PROXIMITY catch -- battleyarou's method[4] is a movement loop
//       and its TALKRAD=500 acts as a catch radius. Expected signature:
//       battleyarou's distance to party closes from ~1165 at field
//       entry to <=500 right before BATTLE.
//   (b) TIMER catch -- a chase-session frame counter expires on
//       doopen2a regardless of geometry. Expected signature:
//       battleyarou's position stays near (0,-744) or hops in a fixed
//       pattern and is still far (>500) from the party when BATTLE fires.
// Same 0x190/0x194 fixed-point layout used by Squall/kani; battleyarou
// is in pFieldStateOthers like kani, so the entity stride and offsets
// are identical.
static bool ReadBattleyarouPosition(int32_t& outX, int32_t& outY)
{
    uintptr_t by = ChaseDetector::GetBattleyarouEntityPtr();
    if (by == 0) return false;
    __try {
        uint8_t* block = reinterpret_cast<uint8_t*>(by);
        int32_t fpX = *(int32_t*)(block + 0x190);
        int32_t fpY = *(int32_t*)(block + 0x194);
        outX = fpX / 4096;
        outY = fpY / 4096;
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

// Squared distance helper (avoids sqrt for log threshold checks).
static int32_t DistSquared(int32_t ax, int32_t ay, int32_t bx, int32_t by)
{
    int64_t dx = (int64_t)ax - (int64_t)bx;
    int64_t dy = (int64_t)ay - (int64_t)by;
    int64_t sq = dx*dx + dy*dy;
    if (sq > 0x7FFFFFFFLL) sq = 0x7FFFFFFFLL;
    return (int32_t)sq;
}

// Integer sqrt for distance display (returns 0 for negative input).
// v0.15.9.2.9: Newton's method from x=v doesn't converge in 6 iterations for
// large squared values (e.g. v=796850 converges to 12471 after 6 iter, real
// answer is 892). v0.15.9.2.8 BAT showed kdist values inflated 14x. Switch to
// std::sqrt via <cmath>; the cast back to int32_t truncates fractional pixels
// which is fine for distance display.
static int32_t IntSqrt(int32_t v)
{
    if (v <= 0) return 0;
    return (int32_t)std::sqrt((double)v);
}
