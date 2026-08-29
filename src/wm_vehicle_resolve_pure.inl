// wm_vehicle_resolve_pure.inl -- which vehicle the mod should act on.
//
// A statement-free header fragment #included by world_catalog.inl and
// world_map_planner2.inl (the real build) and by tests/wm_vehicle_resolve_test.cpp.
//
// TWO SOURCES, AND ONLY ONE OF THEM IS ABOUT VEHICLES.
//
// The locomotion byte at 0x02040A5E is an ANIMATION-state byte -- DEVNOTES has
// said so since .255 -- and the v0.20.56 corroboration gate exists because it
// commits Ship, Car and Garden in the space of three seconds while the player is
// simply walking. The engine vehicle id at 0x020409E0 names a vehicle when it
// names anything at all. So the id wins when it is positive, and the byte is
// what is left when it is not. VEHICLE-POSITIVE ONLY: id 0 and id 6 mean foot or
// mean nothing, and neither is evidence against a byte that says otherwise.
//
// WHY THIS IS A SHARED PREDICATE AND NOT TWO COPIES. v0.82.0 found that the
// catalog was built for the wrong vehicle because nothing watched the engine id,
// and fixed it -- in the catalog. The 13:41 BAT is the same bug in the second
// place nobody checked:
//
//   [13:41:11] [DRIVE] Start -> Esthar City, dist=28005
//   [13:41:11] [TRIGAVOID] plan avoids 2 learned trigger circle(s)
//   [13:41:21] [PLAN] A* wall-clock bail at 157696 expansions
//   [13:41:21] [PLAN] GRID planner ok: ... -> 302 fine cells
//
// TEN SECONDS OF FROZEN GAME, and then a 302-cell WALKING ROUTE for an airship.
// PlanDrivePath has refused to plan for the Ragnarok since v0.78.0 -- the code
// is right there and reads `if (veh == VEH_RAGNAROK) ... return false` -- but it
// resolved `veh` from s_lastVehicle, which is the locomotion byte's verdict, and
// that byte never saw the Ragnarok in any BAT yet recorded. Every [PLAN] line in
// the log says `veh=0`: on foot, the whole time he was flying.
//
// The catalog and the planner now ask the same function, so the next place that
// needs this cannot quietly disagree with the two that already have it.
static int WmResolveVehicle(int byteVeh, int engineId, int idVeh, int footVeh)
{
    if (engineId > 0 && engineId != 6 && idVeh != footVeh) return idVeh;
    return byteVeh;
}
