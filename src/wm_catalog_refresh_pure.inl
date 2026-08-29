// wm_catalog_refresh_pure.inl -- is the catalog he is cycling the catalog for
// the vehicle he is riding?
//
// A statement-free header fragment #included by world_catalog.inl (the real
// build) and by tests/wm_catalog_refresh_test.cpp.
//
// THE 2026-08-25 09:22 BAT: ONE CATALOG BUILD, TWO VEHICLES.
//
// The whole session contains a single "Catalog built (11 entries)" at 09:22:17,
// on foot. He then boarded the Ragnarok -- the engine vehicle id read 50, three
// separate times, and the drive steered as a vehicle on it -- and the catalog
// was never rebuilt. So he flew with the ON-FOOT catalog: eleven destinations
// filtered to what a walker could reach, out of the thirty-six the airship can
// go to. Aaron: "there are many locations that are not appearing in the catalog
// when I am aboard Ragnarok."
//
// WHY THE EXISTING WATCHER DID NOT FIRE. CheckVehicleChange is driven by the
// locomotion byte at 0x02040A5E, which DEVNOTES has called an ANIMATION-state
// byte since .255 and which the corroboration gate exists to distrust. In this
// session it never presented 50 for four consecutive polls -- the one verdict
// logged reads "the locomotion byte said -1". There is no [VEH-REJECT] line
// either: the byte never got as far as the gate. Meanwhile the ENGINE VEHICLE
// ID (0x020409E0) read 50 cleanly the entire time. The catalog builder has
// preferred that id since v0.18.3.258; nothing WATCHED it.
//
// WHY THIS WATCHER IS SAFE WHERE TRUSTING THE BYTE WAS NOT. Its only power is
// to invalidate a cache. It never assigns s_lastVehicle and never repoints the
// position source -- the two things that made v0.56.0's Esthar failure so
// expensive. A wrong answer here costs one redundant rebuild, and the rebuild
// itself re-resolves the vehicle from scratch. So the id may be trusted here on
// terms that would be reckless in CheckVehicleChange.
//
// AND IT COMPARES LIKE WITH LIKE. The class recorded at build time is derived
// from the id ALONE, not from the builder's byte-then-id resolution. If the two
// derivations disagreed -- byte says Garden, id says nothing -- a mismatch would
// be permanent and this would rebuild every tick forever. Same input, same
// function, both sides: a settled state is a fixed point.
// v0.93.0: AND THE DRIVE GUARD IS GONE, BECAUSE IT WAS GUARDING THE WRONG THING.
//
// This used to refuse to fire while a drive was active, on the grounds that
// "auto-drive injects arrow keys and cycles every vehicle byte the engine owns"
// -- which v0.14.94 established and which is entirely true OF THE LOCOMOTION
// BYTE. It is not true of the ENGINE VEHICLE ID, and the engine vehicle id is
// the only thing this watcher reads. That was the whole reason v0.82.0 chose it.
//
// The 18:18 BAT shows what the over-caution cost, twice in one session. Aaron
// auto-drove ON FOOT to the parked Ragnarok and pressed X to board when he got
// there:
//
//   [18:18:21] [DRIVE] Start -> Ragnarok at (82117,11779), dist=528
//   [18:18:22] [VEHID] engine vehicleId=50 -> steering as VEHICLE from drive start
//   [18:18:28] [DRIVE] Stopped: Cancelled.          <-- HE had to press backslash
//   [18:18:28] [VEHID] ... rebuilding (embark/disembark)
//
// The id read 50 the moment he boarded and this watcher was not allowed to look
// at it. The drive carried on flying the airship toward the coordinate where the
// airship used to be parked, with s_ragFlying still false from a start made on
// foot, until he cancelled it by hand. Aaron: "It needs to detect when that
// happens and update the catalog automatically and disable any auto-drive in
// progress."
static bool WmCatalogStale(bool onWorldMap, bool built,
                           int builtClass, int liveClass)
{
    if (!onWorldMap) return false;
    if (!built)      return false;   // a rebuild is already queued
    if (builtClass < 0) return false; // nothing has ever been built
    return liveClass != builtClass;
}

// Boarding or stepping off mid-drive does not merely stale the catalog -- it
// invalidates the DRIVE. Every latch StartAutoDrive took is now about the wrong
// vehicle: s_ragFlying, the landing row, the arrival radius, which key set the
// executor presses. There is nothing worth salvaging, so it stops and says so,
// and he chooses again from a catalog that matches what he is standing in.
static bool WmDriveInvalidByVehicle(bool driving, bool stale)
{
    return driving && stale;
}

// The change has to hold for a few consecutive polls before it commits, for the
// same reason CheckVehicleChange debounces: a rebuild runs a reachability flood
// and speaks, and neither should happen on a single transient read.
static bool WmStaleDebounce(int liveClass, int* pendClass, int* pendCount, int need)
{
    if (!pendClass || !pendCount) return false;
    if (*pendClass != liveClass) { *pendClass = liveClass; *pendCount = 1; }
    else                         { (*pendCount)++; }
    if (*pendCount < need) return false;
    *pendClass = -1;
    *pendCount = 0;
    return true;
}
