// rag_landing_model.inl -- where the Ragnarok sets down, as data and a lookup.
//
// A statement-free header fragment #included by the world-map catalog (the real
// build) and by tests/rag_landing_test.cpp, so the probe exercises the shipped
// table rather than a restatement of it.
//
// WHY THE RAGNAROK IS THE EASIEST VEHICLE AND THE HARDEST TO PARK
// ---------------------------------------------------------------
// Easiest: it has no movement mask at all. `0x53E6B0` -- "may this vehicle move
// onto this polygon" -- runs a switch on the vehicle id and falls through to
// `return 1` for 0x32. The airship flies over ocean, mountain and forest alike,
// so there is no route to plan and nothing to steer around. Every other vehicle
// in this mod needed a navmesh; this one needs a straight line.
//
// Hardest: the only question the world map asks about it is where it may SET
// DOWN, and that is a different bit on a different byte. `0x53E730` reads
// `(typeTriple >> 8) & 0x80` for vehicle 0x32 -- poly[14] bit 7 -- and
// `0x54B860` additionally refuses if `|entity.y - groundH| >= 200`. Of the
// 473,193 polygons in wmx.obj, 77,447 carry that bit: 97% of plains, 0% of
// ocean, 0% of mountain, and 0% of forest, which is exactly what an airship
// setting down in a field should look like.
//
// So a Ragnarok destination is not a place to drive to. It is a place to land,
// and the two are only sometimes the same.
enum RagLandingKind {
    // The destination IS a pad: fly to the spot and set down on it. Identified
    // from the game's own data rather than by hand -- a record in the wmsetus
    // location table (section 7, offset 5580) that is landable and NOT
    // foot-walkable. Exactly three of its fifty-two live records qualify, and
    // they are exactly the three Aaron named: Fisherman's Horizon, the Deep Sea
    // Research Center and the Esthar Airstation. A place you can set down on
    // but cannot walk onto is a place you arrive at by landing.
    RAG_PAD = 0,
    // Land as close as it can and walk the rest. The cell is landable AND
    // foot-walkable AND foot-connected to the destination marker under the
    // engine's own step gate, so the landing is never across a strait from the
    // place it serves. Note this is the OPPOSITE of the Garden's berths, which
    // are deliberately held 2-3 km off because the hull cannot approach a town:
    // Aaron asked for "as close as possible", and an airship sets down in a
    // field.
    RAG_WALK = 1,
};

struct RagLanding {
    const char* name;     // matches the world-map catalog name exactly
    int32_t     x;        // where to fly to and land, in world units
    int32_t     y;
    int32_t     walk;     // units left to walk after landing; 0 for a pad
    int         kind;     // RAG_PAD or RAG_WALK
    // v0.83.0: how close the ship must get before the drive calls it arrived.
    // Per row, because the ground is per row. Measured offline as the radius of
    // the largest all-landable disc around the landing point, less a margin,
    // floored at 288 (the move step is 0x100 = 256; a radius at or under one
    // step cannot be hit) and capped at 512. The 10:08 BAT is why this stopped
    // being one number: 512 was WIDER than the Deep Sea Research Center's pad
    // is deep (384), so the drive was allowed to stop clear of the thing it was
    // aiming at, over ocean.
    int32_t     arrive;
    // The mean vertex height of the polygon at the landing point, for comparing
    // against the engine's own live ground height at 0x0203FE30 on arrival. A
    // diagnostic, not a gate -- see the arrival log in world_map_drive.inl.
    int32_t     groundH;
    // v0.97.0: the measured radius of the all-landable disc around the point, on
    // the polygons. Shipped rather than left in a generator comment so the MARGIN
    // -- clearance minus arrival radius -- is a property CI can hold, which is
    // what v0.83.0 could not do and what let 32-unit margins ship.
    int32_t     clear;
};

#include "rag_landing_table.inl"

// The landing for a catalog destination, or nullptr if the table has no row.
// A missing row is not a hidden destination -- the catalog still offers it and
// the drive still flies there; it means the mod has nothing to say about where
// to set down, which is worth hearing rather than silently withholding.
static const RagLanding* RagLandingFor(const char* name)
{
    if (name == nullptr) return nullptr;
    for (int i = 0; i < RAG_LANDING_COUNT; i++)
        if (_stricmp(RAG_LANDINGS[i].name, name) == 0) return &RAG_LANDINGS[i];
    return nullptr;
}

// How far the player still has to walk once he is down. A pad is zero by
// definition: landing on it IS the arrival.
static int32_t RagWalkAfterLanding(const RagLanding* r)
{
    if (r == nullptr) return -1;
    return (r->kind == RAG_PAD) ? 0 : r->walk;
}
