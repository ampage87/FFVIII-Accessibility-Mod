// world_map_locomotion.inl -- what the player is ACTUALLY riding.
//
// PART OF world_map.cpp -- TEXTUAL INCLUDE. Do NOT compile standalone.
// Included AFTER world_map_segments.inl (it uses GetWorldMapPosition,
// WmSafeReadBytes and GetActiveVehicleId) and BEFORE world_catalog.inl and
// world_map_drive.inl, both of which ask it questions.
// Compiled standalone by tests/locomotion_compile.cpp.
//
// ============================================================================
// WHY THIS EXISTS -- THE 2026-08-21 ESTHAR BAT (#118)
// ============================================================================
//
// Aaron drove to Lunar Gate, then to Sorceress Memorial, then to Tears' Point.
// The first walked the whole 31 km and stopped on its target. The second and
// third ran in circles and never arrived. The log says why, in three lines
// spread across four minutes:
//
//   20:40:41 [WM-ENTRY-DEBOUNCE] Snapshot baseline locomotion=3 (was 0,
//                                suppressed 102937ms of byte noise)
//   20:40:58 [VEH-POS-OVERRIDE]  foot DWORDs are moving; ignoring stale
//                                locomotion=3 and using foot pos (86900,6861)
//   20:41:54 [VEHPOS32] bgu_pos int32=(20360,-3850,200)
//
// **Locomotion 3 is VEH_GARDEN -- the Ship.** Aaron was on foot in Esthar, and
// had been for the whole session. The byte at that address is an ANIMATION-
// state byte; DEVNOTES has said since v0.18.3.255 that it is "never
// authoritative for vehicle detection", and v0.20.56 added a CORROBORATION
// GATE to `CheckVehicleChange` that requires a non-foot claim to be seconded by
// either the engine's in-motion vehicle id or that vehicle's own savemap mirror
// being where the player is.
//
// The gate worked. It was simply not on the path that fired. The world-map
// ENTRY DEBOUNCE has its own snapshot branch that returns before ever reaching
// it, and that branch assigned `s_lastVehicle = vehicle` outright. One noisy
// read of 3 at the instant the debounce window expired, and the mod believed
// Aaron was piloting a Ship for the rest of the session.
//
// TWO CONSEQUENCES, AND THE SECOND ONE IS THE ONE THAT LOOKED MYSTERIOUS:
//
//   1. POSITION. `GetWorldMapPosition_Active` repointed at the Balamb Garden
//      savemap mirror, 70 km away at (20360,-3850). The Tears' Point drive
//      started from there: `dist=72125 units (72 km)` for a target 25 km away,
//      `Fine path: 142 cells from fine(147,92)` -- Fisherman's Horizon.
//
//   2. STEERING. `isOnFoot` in world_map_drive.inl is derived from the same
//      byte, and the whole camera-write steering law lives behind
//      `} else if (isOnFoot) {`. With the byte claiming a Ship, every drive
//      after 20:40:41 took the VEHICLE branch: no camera write, no closed-loop
//      trim, four-way probe arrows against a camera the mod was no longer
//      steering. On the world map the arrows are SCREEN-relative and the camera
//      follows the character, so that is a feedback loop, and the trajectory
//      says so exactly -- 554 samples that orbit (87900,6950) at radius ~1400,
//      heading advancing a steady -32 degrees per sample, one revolution every
//      nine seconds, for thirty-five seconds until Aaron cancelled. Not a wedge
//      against terrain: a circle. The `[CAMW]` trim lines stop at 20:39:49 and
//      never appear again, which is that branch never being entered.
//
// ============================================================================
// WHY THE OLD GUARD COULD NOT HAVE CAUGHT IT, EITHER
// ============================================================================
//
// `GetWorldMapPosition_Active` already had a foot-motion override, and it did
// fire correctly at 20:40:58. Its rule was:
//
//     footAlive = (now - lastFootMoveTick) < 2000;
//
// -- "the foot DWORDs changed within the last two seconds". That is sound
// evidence when it is present, and it is ABSENT EXACTLY WHEN IT IS NEEDED: the
// player stands still to press the drive key. At 20:41:54 Aaron had cancelled a
// drive at 20:41:50 and pressed the catalog key twice; he had not moved for
// four seconds, the window had closed, and the override stayed silent while the
// drive read its start position from a Garden 70 km away.
//
// A two-second window on "did the player move" is a timer, not a discriminator.
//
// ============================================================================
// WHAT THIS MODULE DOES INSTEAD
// ============================================================================
//
// One verdict, latched, updated from evidence rather than from a clock.
//
// The physical fact underneath is unchanged and is what makes it work: **the
// engine's per-frame integrator updates the foot DWORDs only while the player
// IS the foot character.** Mount a vehicle and they freeze; the vehicle's
// savemap mirror moves instead. So:
//
//   * foot DWORDs moved this tick        -> FOOT. Definitive, no corroboration
//                                           needed, and it outranks the byte.
//   * byte claims a vehicle AND that claim is corroborated
//     (engine in-motion id names the same family, OR that vehicle's mirror is
//     within 600 units of the player)    -> VEHICLE.
//   * neither                            -> **KEEP THE LAST VERDICT.**
//
// That third line is the fix. Standing still is not evidence of anything, so it
// must not be allowed to change the answer -- and under the old code standing
// still is precisely what let the byte through.
//
// The corroboration test is not a second implementation: it is the v0.20.56
// gate, moved here whole, so `CheckVehicleChange` and this module cannot drift
// apart. `CheckVehicleChange` now calls it, including on the entry-debounce
// snapshot path that skipped it.
//
// BOOTSTRAP. Before any evidence exists the verdict is UNKNOWN, and the answer
// is the byte -- but a non-foot byte still has to pass corroboration. So the
// worst case at session start is the behaviour the corroboration gate already
// gives everywhere else, and an uncorroborated vehicle claim now reads as FOOT
// instead of being believed. Foot is also the safe default: it is what
// `GetVehicleType` already returns for unknown values.
//
// NOT CHANGED, DELIBERATELY: the mobile Garden. While `Garden_Active()` the
// mod's own Garden executor owns the drive, and this module reports VEHICLE
// unconditionally so nothing about that path moves. Genuine Garden piloting is
// covered by the two corroboration signals anyway -- the engine id names 48
// while the hull is in motion, and while it is parked the frozen foot DWORDs
// sit at the boarding point, which is next to the hull.

// ---------------------------------------------------------------------------
// The corroboration gate (v0.20.56, moved here at v0.56.0 so both callers share
// one copy). TRUE means: something that is actually about vehicles agrees that
// the player is riding `vehicle`.
//
// `footX/footY` is the player's foot position. When the claim is true and the
// vehicle is parked, the foot DWORDs are frozen at the boarding point beside
// it, so the mirror is close. When the claim is stale, the mirror is wherever
// that vehicle really is -- for Aaron on 2026-08-21, 70 km away.
// ---------------------------------------------------------------------------
static bool LocoCorroborated(uint8_t vehicle, int32_t footX, int32_t footY,
                             int* outId, double* outMirrorDist)
{
    if (outId) *outId = -1;
    if (outMirrorDist) *outMirrorDist = -1.0;
    if (IsFootLocomotion(vehicle)) return true;      // a foot claim needs nothing

    const int id = GetActiveVehicleId();
    if (outId) *outId = id;
    const bool idAgrees = (id > 0 &&
                           GetVehicleType((uint8_t)id) == GetVehicleType(vehicle));

    uintptr_t mirror = 0;
    switch (GetVehicleType(vehicle)) {
        case VEH_CAR:      mirror = WM_CAR_POS_ADDR;      break;
        case VEH_GARDEN:   mirror = WM_BGU_POS_ADDR;      break;
        case VEH_RAGNAROK: mirror = WM_RAGNAROK_POS_ADDR; break;
        default:           mirror = 0;                    break;
    }
    bool mirrorAgrees = false;
    if (mirror && (footX || footY)) {
        int32_t vpx = 0, vpy = 0;
        if (WmSafeReadBytes(mirror + WMS_VEHPOS_X_OFF, &vpx, 4) &&
            WmSafeReadBytes(mirror + WMS_VEHPOS_Y_OFF, &vpy, 4) && (vpx || vpy)) {
            const double d = CalculateWrappedDistance(footX, footY, vpx, vpy);
            if (outMirrorDist) *outMirrorDist = d;
            mirrorAgrees = (d < LOCO_MIRROR_AGREE_UNITS);
        }
    }
    return idAgrees || mirrorAgrees;
}

// ---------------------------------------------------------------------------
// The verdict itself.
// ---------------------------------------------------------------------------
static void LocoTick()
{
    int32_t fx = 0, fy = 0, fz = 0;
    GetWorldMapPosition(&fx, &fy, &fz);

    const bool footMoved = s_locoHadFoot && (fx != s_locoFootX || fy != s_locoFootY);
    s_locoFootX = fx; s_locoFootY = fy; s_locoHadFoot = true;

    // 1. Foot motion outranks everything. The integrator only runs the foot
    //    DWORDs while the player is the foot character, so this is not an
    //    inference.
    if (footMoved) {
        if (s_locoVerdict != LOCO_FOOT) {
            Log::World("WorldMap: [LOCO] verdict FOOT -- foot DWORDs moved to (%d,%d) "
                       "while the locomotion byte said %d", fx, fy, s_lastVehicle);
        }
        s_locoVerdict = LOCO_FOOT;
        return;
    }

    // 2. A vehicle claim, only if something that is actually about vehicles
    //    seconds it.
    if (s_lastVehicle >= 0 && !IsFootLocomotion((uint8_t)s_lastVehicle)) {
        int id = -1; double md = -1.0;
        if (LocoCorroborated((uint8_t)s_lastVehicle, fx, fy, &id, &md)) {
            if (s_locoVerdict != LOCO_VEHICLE) {
                Log::World("WorldMap: [LOCO] verdict VEHICLE (%d) -- corroborated "
                           "(engine id=%d, |foot-mirror|=%.0f)",
                           s_lastVehicle, id, md);
            }
            s_locoVerdict = LOCO_VEHICLE;
            return;
        }
        // Uncorroborated. Say so ONCE per distinct byte value, then hold.
        if (s_locoLastRejected != s_lastVehicle) {
            s_locoLastRejected = s_lastVehicle;
            Log::World("WorldMap: [LOCO] locomotion=%d NOT corroborated "
                       "(engine id=%d, |foot-mirror|=%.0f) -- holding verdict %s",
                       s_lastVehicle, id, md,
                       s_locoVerdict == LOCO_VEHICLE ? "VEHICLE"
                                                     : (s_locoVerdict == LOCO_FOOT ? "FOOT" : "UNKNOWN"));
        }
    }

    // 3. A foot byte with no foot motion is still a foot claim, and it needs no
    //    corroboration -- but it must not overturn a standing VEHICLE verdict,
    //    because a parked vehicle produces exactly this and the byte cycles.
    if (s_lastVehicle >= 0 && IsFootLocomotion((uint8_t)s_lastVehicle) &&
        s_locoVerdict == LOCO_UNKNOWN) {
        s_locoVerdict = LOCO_FOOT;
    }
    // 4. Otherwise: no evidence this tick. Keep the last verdict. Standing
    //    still is not information, and treating it as information is the whole
    //    bug this file exists for.
}

// True when the player is on foot (or on a Chocobo, which piggybacks on the
// foot character). This is what the position source and the steering law ask.
static bool LocoIsFoot()
{
    if (s_locoVerdict == LOCO_FOOT)    return true;
    if (s_locoVerdict == LOCO_VEHICLE) return false;
    // UNKNOWN: fall back to the byte, with the same corroboration requirement,
    // so an uncorroborated vehicle claim reads as foot rather than being
    // believed outright.
    if (s_lastVehicle < 0) return true;
    if (IsFootLocomotion((uint8_t)s_lastVehicle)) return true;
    int32_t fx = 0, fy = 0, fz = 0;
    GetWorldMapPosition(&fx, &fy, &fz);
    return !LocoCorroborated((uint8_t)s_lastVehicle, fx, fy, nullptr, nullptr);
}

// Reset on world-map exit / session boundaries: a verdict must not survive into
// a context where none of the evidence that produced it still applies.
static void LocoReset()
{
    s_locoVerdict      = LOCO_UNKNOWN;
    s_locoHadFoot      = false;
    s_locoFootX        = 0;
    s_locoFootY        = 0;
    s_locoLastRejected = -999;
}
