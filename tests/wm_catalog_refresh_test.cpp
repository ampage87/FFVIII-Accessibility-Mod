// wm_catalog_refresh_test.cpp -- the rule that noticed he had boarded.
//
// The 2026-08-25 09:22 BAT contains exactly one "Catalog built (11 entries)",
// on foot, and three separate reads of engine vehicleId=50 afterwards. He flew
// the whole session cycling the on-foot catalog. This is the watcher that would
// have caught it, and the guards that stop it firing when it must not.
#include <cstdio>
#include "wm_catalog_refresh_pure.inl"

static int bad = 0;
static void chk(bool ok, const char* what)
{
    if (!ok) { printf("  BAD: %s\n", what); bad++; }
}

// class 0 = foot/car/chocobo, 1 = Garden/ship, 2 = Ragnarok
enum { FOOT = 0, GARDEN = 1, RAG = 2 };

int main()
{
    printf("wm_catalog_refresh_test\n");

    // -----------------------------------------------------------------------
    // The BAT, exactly: on the world map, not driving, a built catalog whose
    // class is FOOT, and an engine id now saying Ragnarok.
    // -----------------------------------------------------------------------
    chk(WmCatalogStale(true, true, FOOT, RAG) == true,
        "**boarding the Ragnarok does not make the on-foot catalog stale** -- this "
        "is the 09:22 BAT: one build at 09:22:17 for eleven walkable destinations, "
        "then a whole flight spent cycling it");

    // And the way back. Landing has to shrink the list again -- Aaron: "when they
    // land only locations within walking distance should be in the catalog."
    chk(WmCatalogStale(true, true, RAG, FOOT) == true,
        "**disembarking does not make the Ragnarok catalog stale** -- the list has "
        "to shrink back to what a walker can reach");

    chk(WmCatalogStale(true, true, FOOT, GARDEN) == true, "boarding the Garden is not noticed");

    // A settled state must be a fixed point, or this rebuilds every tick forever.
    chk(WmCatalogStale(true, true, FOOT, FOOT) == false,
        "**an unchanged class still reads as stale** -- a rebuild runs a "
        "reachability flood and speaks; doing that every tick is worse than the bug");
    chk(WmCatalogStale(true, true, RAG, RAG) == false, "unchanged Ragnarok class reads stale");

    // -----------------------------------------------------------------------
    // The guards.
    // -----------------------------------------------------------------------
    chk(WmCatalogStale(false, true, FOOT, RAG) == false,
        "**it fires off the world map** -- there is no catalog to invalidate there");

    // v0.93.0: and it MUST fire mid-drive, which is the opposite of what this
    // asserted for eleven builds. The old guard was aimed at the LOCOMOTION BYTE,
    // which arrow injection really does cycle; this watcher reads the engine
    // vehicle id, which it does not, and that was the whole reason v0.82.0 chose
    // the id. The 18:18 BAT: he auto-drove on foot to the parked Ragnarok,
    // pressed X on arrival, the id read 50 immediately -- and the drive flew on
    // toward where the ship used to be parked until he cancelled it by hand.
    chk(WmDriveInvalidByVehicle(true, WmCatalogStale(true, true, FOOT, RAG)) == true,
        "**boarding mid-drive leaves the drive running** -- every latch it took is "
        "about the vehicle he was in when it started: the flying flag, the landing "
        "row, the arrival radius, which key set the executor presses");
    chk(WmDriveInvalidByVehicle(true, WmCatalogStale(true, true, RAG, FOOT)) == true,
        "stepping off mid-drive leaves the drive running");
    chk(WmDriveInvalidByVehicle(false, WmCatalogStale(true, true, FOOT, RAG)) == false,
        "**it stops a drive that was not running** -- there is nothing to stop, and "
        "StopAutoDrive would speak a cancellation into an idle world map");
    chk(WmDriveInvalidByVehicle(true, WmCatalogStale(true, true, RAG, RAG)) == false,
        "**an unchanged vehicle stops the drive** -- that is every tick of every "
        "flight, and the drive would never survive its first second");

    chk(WmCatalogStale(true, false, FOOT, RAG) == false,
        "**it fires when a rebuild is already queued** -- s_catalogBuilt=false "
        "means the next tick builds anyway");

    chk(WmCatalogStale(true, true, -1, RAG) == false,
        "**it fires before anything has ever been built** -- -1 is 'no catalog yet', "
        "not 'a catalog for class -1'");

    // -----------------------------------------------------------------------
    // The debounce. A rebuild is expensive and audible; one transient read of
    // the id must not trigger it.
    // -----------------------------------------------------------------------
    {
        int pc = -1, pn = 0;
        chk(WmStaleDebounce(RAG, &pc, &pn, 4) == false, "committed on poll 1 of 4");
        chk(WmStaleDebounce(RAG, &pc, &pn, 4) == false, "committed on poll 2 of 4");
        chk(WmStaleDebounce(RAG, &pc, &pn, 4) == false, "committed on poll 3 of 4");
        chk(WmStaleDebounce(RAG, &pc, &pn, 4) == true,
            "**four consecutive agreeing polls did not commit** -- then nothing ever would");
        // and it resets, so the next change starts its own count
        chk(pc == -1 && pn == 0, "the counter did not reset after committing");
    }
    {
        // A flicker restarts the count rather than accumulating toward a commit.
        int pc = -1, pn = 0;
        WmStaleDebounce(RAG, &pc, &pn, 4);
        WmStaleDebounce(RAG, &pc, &pn, 4);
        WmStaleDebounce(GARDEN, &pc, &pn, 4);           // different answer: restart
        chk(pn == 1, "**a changed answer accumulated instead of restarting the count** -- "
                     "two of one and one of another would then commit as if it were three of one");
        chk(WmStaleDebounce(GARDEN, &pc, &pn, 4) == false, "committed on poll 2 after a flicker");
        chk(WmStaleDebounce(GARDEN, &pc, &pn, 4) == false, "committed on poll 3 after a flicker");
        chk(WmStaleDebounce(GARDEN, &pc, &pn, 4) == true, "never committed after a flicker");
    }

    printf("wm_catalog_refresh_test: %s (%d bad)\n", bad ? "FAILED" : "OK", bad);
    return bad ? 1 : 0;
}
