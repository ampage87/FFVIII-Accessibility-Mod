// wm_story_pure.inl -- which destinations the story has taken off the map.
//
// A statement-free header fragment #included by world_catalog.inl (the real
// build) and by tests/wm_story_test.cpp, so the probe exercises the shipped
// rules rather than a restatement of them.
//
// Three questions, each with one answer, because v0.20.88's lesson was that a
// condition armed in one place and asked about in three silently loses a
// destination.

// ---------------------------------------------------------------------------
// Q1. Does the player have the Ragnarok?
//
// v0.20.101 asked this by reading `ragnarok_pos` (WORLDMAP +0x18) and calling a
// live coordinate proof of ownership. That is HALF the answer, and the half it
// misses is the common one: the slot holds the ship's PARKED position and reads
// ZERO WHILE YOU ARE INSIDE IT. It is the exact analogue of `bgu_pos`, which is
// why the mobile-Garden catalog entry is injected in the *not aboard* branch --
// the same fact, already relied on elsewhere in this file, just never applied
// here.
//
// The 2026-08-25 log is the proof, two catalog builds ninety seconds apart in
// one session:
//
//   23:03:38  engine vehicleId=50 (aboard)   ragnarok_pos zero  -> "not held"
//   23:05:09  engine vehicleId=0  (on foot)  ragnarok_pos live  -> "held"
//
// So the White SeeD Ship came back onto the map every time Aaron took off, and
// the only reason it did not stay back is that he landed. Being aboard the ship
// is the least ambiguous proof of ownership there is.
static bool WmRagnarokHeldPure(bool aboardRagnarok, bool parkedSlotLive)
{
    return aboardRagnarok || parkedSlotLive;
}

// ---------------------------------------------------------------------------
// Q2. Is the White SeeD Ship on the world map?
//
// Aaron: "White SeeD Ship is no longer on the world map after you get Ragnarok.
// It should not appear on the world map catalog again regardless of vehicle at
// this point." Both edges are story edges, and neither is a guess: it appears on
// disc 3 and it is gone the moment the Ragnarok is his.
//
// REGARDLESS OF VEHICLE is the operative phrase and it is why Q1 had to be
// fixed rather than this rule patched: the answer must not depend on what he
// happens to be riding when the catalog is built.
static bool WmWhiteSeedPresentPure(int disc, bool ragnarokHeld)
{
    if (disc < 3) return false;                    // not available before disc 3
    return !ragnarokHeld;                          // gone once the Ragnarok is his
}

// ---------------------------------------------------------------------------
// Q3. Should the catalog offer the mobile Balamb Garden?
//
// Aaron: "Mobile Balamb Garden should not be an option in the catalog when
// flying Ragnarok. To get to Garden you land at FH where Garden is parked."
//
// The log confirms the geography rather than taking it on trust. At 23:03:38
// the Garden's live savemap coordinate was (20360,-3850); Fisherman's Horizon's
// landing pad -- one of only three RAG_PAD rows in the whole table, picked out
// by the engine's own poly[14] bit 7 -- is (20480,-2560). **1,295 units apart.**
// The Garden is parked at FH, so FH is not a detour on the way to it; FH IS the
// way to it, and it is a pad the airship can actually set down on.
//
// Offering the Garden as well would offer a worse route to the same place: a
// hull the airship cannot land on, with no landing row and none possible, since
// the Garden's coordinate is read live from the savemap and no offline pass can
// generate ground for a thing that moves.
//
// Scoped to flight. On foot and in the car the mobile Garden is still how you
// find your ride again, which is what the entry was added for.
static bool WmOfferMobileGardenPure(bool posPlausible, bool flyingRagnarok)
{
    if (!posPlausible) return false;               // no coordinate, nothing to offer
    return !flyingRagnarok;
}

// ---------------------------------------------------------------------------
// Q4. Should the catalog offer the parked Ragnarok as a destination?
//
// Aaron: "There is no entry for the Ragnarok itself in the catalog once landed.
// Just like Mobile B-Garden, there needs to be an entry for the Ragnarok so the
// player can navigate to it and board it on the world map."
//
// The coordinate comes from the same slot the ownership test reads, and the
// property that made that test wrong is exactly what makes this one easy:
// `ragnarok_pos` holds the ship's PARKED position and is zero while he is
// inside it. A live coordinate therefore means "you own it and it is somewhere
// other than under you", which is precisely when a destination is useful.
//
// The flight check is belt-and-braces rather than load-bearing -- the slot is
// already zero in that case -- but it costs one boolean and it means the rule
// still reads correctly if that ever stops being true.
static bool WmOfferParkedRagnarokPure(bool posPlausible, bool flyingRagnarok)
{
    if (!posPlausible) return false;               // not his yet, or he is in it
    return !flyingRagnarok;
}
