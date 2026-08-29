// wm_vehicle_resolve_test.cpp -- the engine id wins when it names a vehicle.
#include <cstdio>
#include "wm_vehicle_resolve_pure.inl"

enum { FOOT = 0, CAR = 1, GARDEN = 2, RAG = 3 };
static int bad = 0;
static void chk(bool ok, const char* w) { if (!ok) { printf("  BAD: %s\n", w); bad++; } }

int main()
{
    printf("wm_vehicle_resolve_test\n");

    // The 13:41 BAT: the byte says foot, the engine id says 50 (Ragnarok).
    chk(WmResolveVehicle(FOOT, 50, RAG, FOOT) == RAG,
        "**the byte outvotes an engine id that names the Ragnarok** -- that is the "
        "13:41 planner spending ten seconds of frozen game on a 302-cell WALKING "
        "route for an airship, because every [PLAN] line read veh=0");

    chk(WmResolveVehicle(FOOT, 48, GARDEN, FOOT) == GARDEN, "a Garden boarding is not picked up");

    // VEHICLE-POSITIVE ONLY. id 0 and id 6 are foot or are nothing, and neither
    // is evidence against a byte that says otherwise -- the byte is noisy, not
    // worthless, and this is what v0.18.3.258 settled.
    chk(WmResolveVehicle(GARDEN, 0, FOOT, FOOT) == GARDEN,
        "**an engine id of 0 overrides a byte that says Garden** -- 0 means foot OR "
        "means unreadable, and demoting a real vehicle on it would repoint the "
        "position source mid-drive");
    chk(WmResolveVehicle(GARDEN, 6, FOOT, FOOT) == GARDEN, "engine id 6 overrides the byte");
    // id 6 is Selphie-foot and GetVehicleType maps it to FOOT, so in every real
    // call the foot guard below would catch it anyway. It is named explicitly all
    // the same, and pinned here, because "6 means foot" is a fact about
    // GetVehicleType rather than about this rule -- and a predicate that only
    // works while a function somewhere else keeps a mapping is a trap.
    chk(WmResolveVehicle(GARDEN, 6, RAG, FOOT) == GARDEN,
        "**engine id 6 is trusted when something maps it to a real vehicle** -- 6 is "
        "a foot id by name, not by whatever the type table happens to say today");
    chk(WmResolveVehicle(RAG, -1, FOOT, FOOT) == RAG, "an unreadable engine id overrides the byte");

    // An id that maps to foot is not an upgrade either, however positive it is.
    chk(WmResolveVehicle(GARDEN, 33, FOOT, FOOT) == GARDEN,
        "**a positive id that resolves to FOOT still demotes the byte** -- the rule "
        "is an upgrade, never a downgrade");

    // And with nothing to say, the byte stands.
    chk(WmResolveVehicle(FOOT, 0, FOOT, FOOT) == FOOT, "foot everywhere does not read as foot");
    chk(WmResolveVehicle(CAR, 0, FOOT, FOOT) == CAR, "a car byte is lost");

    printf("wm_vehicle_resolve_test: %s (%d bad)\n", bad ? "FAILED" : "OK", bad);
    return bad ? 1 : 0;
}
