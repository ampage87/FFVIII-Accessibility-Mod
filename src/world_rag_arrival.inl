// world_rag_arrival.inl -- the Ragnarok's arrival, which is not like anybody else's.
//
// A statement-free header fragment #included by world_map.cpp (the real build)
// and by tests/rag_arrival_test.cpp, so the probe exercises the shipped rule.
//
// EVERY OTHER DRIVE IN THIS MOD ENDS WHEN A FIELD LOADS. That is why there is no
// distance-based arrival test anywhere in world_map_drive.inl: the game-mode
// watcher sees MODE_FIELD and the drive is over, which is both simpler and more
// truthful than any radius. It works because walking, driving and sailing all
// end by touching a door.
//
// Flying does not. The Ragnarok arrives ABOVE a piece of ground and nothing
// happens at all until the player sets it down -- so for most destinations the
// drive ends in mid-air over a field, with no event to notice. That case needs
// a radius, and it needs something said.
//
// AND IT MUST NOT LAND BY ITSELF. Aaron: *"Let's not have auto-drive
// automatically land and instead prompt the player to land just like we do with
// Garden."* The Garden has parked and told him to get off since v0.20.79, and
// for the same reason: the last act of arriving is the player's. The mod flies
// him to the spot and says so; he decides to come down.
// Is the player in the airship right now? Both signals the world map already
// trusts, in the order world_catalog.inl trusts them: the locomotion byte's
// mapping, and the engine's "in motion" vehicle id, which is vehicle-positive
// only (it reads 0 at world-map entry and names a vehicle only while one is
// moving, so it may confirm and must never deny).
#ifndef FF8OPC_RAG_HOST_TEST
static bool RagIsFlying()
{
    if (GetVehicleType(GetLocomotionMode()) == VEH_RAGNAROK) return true;
    const int vid = GetActiveVehicleId();
    return vid > 0 && GetVehicleType((uint8_t)vid) == VEH_RAGNAROK;
}
#endif

enum RagArrive {
    RAG_FLY_ON = 0,   // keep flying
    RAG_OVER_PAD,     // above a landing pad -- setting down IS the arrival
    RAG_OVER_SPOT,    // above the nearest ground it can land on; a walk follows
    // Above the destination itself, with no landing row to consult. The three
    // MOBILE destinations are here permanently and can never leave: Mobile
    // Balamb Garden, Mobile Galbadia Garden and the White SeeD Ship all have
    // live coordinates read out of the savemap, so no generator can pin a
    // landing beside them. v0.79.0 gave them no arrival at all and the 23:05
    // BAT flew at Mobile Balamb Garden three times without ever ending.
    RAG_OVER_MARKER,
};

// v0.83.0: THE FALLBACK ONLY, NOW. THE REAL RADIUS IS PER ROW.
//
// This was the single arrival radius for every destination, and the comment
// beside it read: "The pads are hundreds of units across -- the Deep Sea
// Research Center's is 768 by 768 -- so this lands on them rather than beside
// them." The right number and the wrong conclusion. 768 ACROSS IS 384 FROM THE
// MIDDLE. 512 > 384, so the drive was free to stop a hundred and thirty units
// clear of the pad, and the 2026-08-25 10:08 BAT is that arithmetic happening:
//
//   pad                        room   stopped at   landed
//   Fisherman's Horizon         889          501      yes
//   Deep Sea Research Center    384     464, 499       no
//   Esthar Airstation           465          506       no
//
// The polygons under the two failures say it without reference to any radius:
// terrain 34 (ocean) under one Deep Sea stop, terrain 29 (cliff face) under the
// Esthar one. Every row now carries a radius measured from its own ground, and
// the landing POINTS moved too -- 23 of the 40 walk rows sat on strips 64 to
// 256 units wide, because "landable AND foot-walkable" is most easily satisfied
// exactly on the seam between them.
//
// 512 survives as the value used when there is no row to consult at all, which
// is the three MOBILE destinations and nothing else.
static const double RAG_ARRIVE_DIST = 512.0;

// dist -- units from the ship to the LANDING point, not to the destination
// marker; those are the same thing only for a pad.
// arriveDist -- the row's own radius, or RAG_ARRIVE_DIST when there is no row.
static RagArrive RagArriveDecide(bool flying, bool hasLanding, int kind, double dist,
                                 double arriveDist)
{
    // FLYING IS THE WHOLE TEST. Not "flying to a place we have a landing for":
    // an airship that reaches its destination and keeps pressing forward is the
    // failure this rule exists to prevent, and whether the mod happens to know
    // where to set down does not change that.
    if (!flying) return RAG_FLY_ON;
    if (arriveDist <= 0.0) arriveDist = RAG_ARRIVE_DIST;
    if (dist >= arriveDist) return RAG_FLY_ON;
    if (!hasLanding) return RAG_OVER_MARKER;
    return (kind == RAG_PAD) ? RAG_OVER_PAD : RAG_OVER_SPOT;
}

// v0.92.0: THE KEY IS NAMED NOW, BECAUSE IT IS KNOWN. Aaron at v0.79.0: "I
// think you use the same key to land Ragnarok that you do to disembark the
// mobile Garden" -- a guess, so the wording stayed vague on purpose. He has
// since confirmed it outright: "You embark/disembark using X", and "pressing X
// to land/disembark descends the ship automatically". A prompt that names the
// key is worth more to someone who cannot see the ship than one that describes
// the intention.
//
// What he hears when the ship stops. Three shapes, because three situations:
// a pad, a short walk, and a walk long enough that the number stops helping.
static void RagArrivalLine(RagArrive what, const char* name, int32_t walk,
                           const char* compass, char* out, size_t n)
{
    if (out == nullptr || n == 0) return;
    // v0.97.0: the walk, in the unit the player actually has a feel for.
    char wbuf[48];
    WmSayDistance((double)walk, wbuf, sizeof wbuf);
    if (what == RAG_OVER_MARKER) {
        // No landing row, so no promise about the ground: say where he is and
        // let him try. The mobile destinations are the permanent members of
        // this branch, and a Garden under way is not something to promise a
        // parking space beside.
        snprintf(out, n, "Over %s. Try pressing X to land here.", name);
    } else if (what == RAG_OVER_PAD) {
        snprintf(out, n, "Over the %s landing pad. Press X to land and go in.", name);
    } else if (walk < 400) {
        snprintf(out, n, "Over %s. Press X to land and go in.", name);
    } else if (walk >= 5000) {
        // The Garden learned this at v0.20.88: "one hundred and twenty-two
        // hundred units" is not a distance anyone can hold in their head.
        snprintf(out, n,
                 "Landed as close as it can get. %s is %s %s -- a long walk. "
                 "Press X to land, then backslash to start it.",
                 name, wbuf, compass ? compass : "away");
    } else {
        snprintf(out, n,
                 "Over the closest ground it can land on. %s is %s %s. "
                 "Press X to land, then backslash to walk the rest.",
                 name, wbuf, compass ? compass : "away");
    }
    out[n - 1] = '\0';
}
