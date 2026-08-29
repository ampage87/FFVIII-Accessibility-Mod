// rag_arrival_test -- the Ragnarok's arrival rule and the words it says.
//
// WHY THIS EXISTS
// ---------------
// Every other drive in this mod ends when a field loads, which is why
// world_map_drive.inl has no distance arrival test at all. Flying does not end
// that way: the ship arrives ABOVE a piece of ground and nothing happens until
// the player sets it down. Without a radius the drive would press UP over the
// spot forever, and without something said he would never know he was there.
//
// And it must not land for him. Aaron: "Let's not have auto-drive automatically
// land and instead prompt the player to land just like we do with Garden."
#include <cstdio>
#include <cstring>
#include <cstdint>
#include <cstdlib>
#include <string>

#define _stricmp strcasecmp
#define FF8OPC_RAG_HOST_TEST 1     // RagIsFlying needs the live engine; skip it

#include "wm_distance_pure.inl"
#include "rag_landing_model.inl"
#include "world_rag_arrival.inl"

static int bad = 0;
static void check(bool ok, const char* what)
{ if (!ok) { std::printf("  BAD: %s\n", what); bad++; } }
static bool has(const char* hay, const char* needle)
{ return std::strstr(hay, needle) != nullptr; }

int main()
{
    // ------------------------------------------------------------------
    // v0.83.0: THE RADIUS IS PER ROW, AND THAT IS THE 10:08 BAT.
    //
    // One radius of 512 for every destination let the drive stop 464 and 506
    // units from pads whose ground reaches only 384 and 465 from the middle --
    // over ocean and over a cliff face. Fisherman's Horizon has 889 units of
    // room and is the one that landed.
    // ------------------------------------------------------------------
    check(RagArriveDecide(true, true, RAG_PAD, 464.0, 288.0) == RAG_FLY_ON,
          "**a stop 464 units out counts as arrived at a pad with a 288 radius** -- "
          "that is the Deep Sea Research Center failing in the 10:08 BAT, and the "
          "polygon under that coordinate is ocean");
    check(RagArriveDecide(true, true, RAG_PAD, 506.0, 369.0) == RAG_FLY_ON,
          "**a stop 506 units out counts as arrived with a 369 radius** -- that is "
          "the Esthar Airstation failing, on terrain 29, a cliff face");
    check(RagArriveDecide(true, true, RAG_PAD, 501.0, 512.0) == RAG_OVER_PAD,
          "**Fisherman's Horizon no longer arrives** -- 501 inside a 512 radius is "
          "the one landing in that BAT that WORKED, and nothing here may break it");

    // A zero or absent radius must fall back rather than arrive instantly: a row
    // that somehow carried 0 would otherwise stop the drive at the first tick.
    check(RagArriveDecide(true, true, RAG_PAD, 500.0, 0.0) == RAG_OVER_PAD &&
          RagArriveDecide(true, true, RAG_PAD, 600.0, 0.0) == RAG_FLY_ON,
          "**a zero radius is not treated as 'use the default'** -- it would either "
          "arrive instantly or never arrive at all");

    // Every shipped row's radius must fit INSIDE its own ground, which is the
    // property the whole change exists to establish.
    {
        int32_t tightest = 1 << 30; const char* who = "none";
        for (int i = 0; i < RAG_LANDING_COUNT; i++) {
            const RagLanding& r = RAG_LANDINGS[i];
            check(r.arrive >= 288,
                  "**a row's radius is at or under the 256-unit move step** -- the "
                  "ship cannot reliably stop inside a disc smaller than one step");
            check(r.arrive <= 512, "a row's radius exceeds the 512 fallback");
            // v0.97.0: THE MARGIN IS THE POINT. Aaron: "A few times when the mod
            // told me to land I was not on a spot where the game would let me
            // land." The ship may stop ANYWHERE inside the radius, so what has to
            // be landable is the radius plus room for whatever the game wants
            // around the ship -- and v0.83.0 left as little as 32 units of it.
            // The generator now measures clearance on the polygons and holds a
            // WALK row to a full move step of margin; a PAD keeps 96, because its
            // coordinate is the game's own and cannot be moved to find more room.
            const int32_t margin = r.clear - r.arrive;
            check(r.kind != RAG_WALK || margin >= 256,
                  "**a walk row has under a move step of landing margin** -- the ship "
                  "may stop ANYWHERE inside the radius, so what must be landable is "
                  "the radius PLUS room for whatever the game wants around the ship. "
                  "v0.83.0 left as little as 32 units of it, and Aaron had to nudge "
                  "the ship forward or back to land");
            check(r.kind != RAG_PAD || margin >= 96,
                  "**a pad has no landing margin at all** -- a pad coordinate is the "
                  "game's own and cannot be moved to find more room, but it still has "
                  "to hold the radius it was given");
            check(r.clear > r.arrive,
                  "**a row's radius reaches past its own ground** -- then the ship is "
                  "allowed to stop somewhere it cannot land, which is the whole defect");
            check(r.groundH <= 0,
                  "**a landing point sits above sea level** -- world-map heights are "
                  "negative upward, so a positive one means the row was generated "
                  "from something other than the polygon under the point");
            if (r.arrive < tightest) { tightest = r.arrive; who = r.name; }
        }
        std::printf("  (all %d rows: radius in [288,512]; tightest %s at %d)\n",
                    (int)RAG_LANDING_COUNT, who, (int)tightest);
    }

    std::printf("rag_arrival_test\n");

    // ---- the decision ----------------------------------------------------
    check(RagArriveDecide(false, true, RAG_PAD, 0.0, RAG_ARRIVE_DIST) == RAG_FLY_ON,
          "**a player who is not flying is never arrived by this rule** -- it is "
          "the one arrival test in the world map that is not driven by a field "
          "load, and letting it fire on foot would end a walk in mid-sentence");
    // v0.80.0, from the 23:05 BAT: this used to be RAG_FLY_ON, and it meant the
    // three MOBILE destinations -- Mobile Balamb Garden, Mobile Galbadia Garden
    // and the White SeeD Ship, whose coordinates are read live out of the
    // savemap and can never have a generated landing -- had no arrival at all.
    // Aaron flew at Mobile Balamb Garden three times and the drive never ended.
    check(RagArriveDecide(true, false, RAG_PAD, 0.0, RAG_ARRIVE_DIST) == RAG_OVER_MARKER,
          "**a destination with no landing row still ARRIVES** -- an airship that "
          "reaches its destination and keeps pressing forward is the failure this "
          "rule exists to prevent, and whether the mod happens to know where to "
          "set down does not change that");
    check(RagArriveDecide(true, false, RAG_PAD, RAG_ARRIVE_DIST + 1, RAG_ARRIVE_DIST) == RAG_FLY_ON,
          "and it still has to get there first");

    check(RagArriveDecide(true, true, RAG_PAD, RAG_ARRIVE_DIST, RAG_ARRIVE_DIST) == RAG_FLY_ON,
          "at exactly the radius it is still flying");
    check(RagArriveDecide(true, true, RAG_PAD, RAG_ARRIVE_DIST - 1, RAG_ARRIVE_DIST) == RAG_OVER_PAD,
          "**inside the radius over a pad, it is over the pad**");
    check(RagArriveDecide(true, true, RAG_WALK, RAG_ARRIVE_DIST - 1, RAG_ARRIVE_DIST) == RAG_OVER_SPOT,
          "**and over a walk row it is over the spot** -- the two end the same "
          "drive and mean different things to the player, so they are different "
          "answers rather than one with a flag");
    check(RAG_ARRIVE_DIST >= 400.0 && RAG_ARRIVE_DIST <= 900.0,
          "**the radius is a couple of the airship's own move steps** -- 0x100 "
          "each, from the engine's step table; the Deep Sea Research Center's pad "
          "is 768 units across, so a tighter radius would fly past it and a much "
          "looser one would stop short of it");

    // ---- the words -------------------------------------------------------
    char b[256];
    RagArrivalLine(RAG_OVER_PAD, "Fisherman's Horizon", 0, "north", b, sizeof b);
    check(has(b, "Press X to land") && has(b, "Fisherman's Horizon"),
          "**a pad tells him to land, and names the place**");
    check(!has(b, "walk"),
          "**and says nothing about walking** -- landing on a pad IS the arrival");

    RagArrivalLine(RAG_OVER_SPOT, "Balamb Town", 704, "east", b, sizeof b);
    check(has(b, "Press X to land") && has(b, "backslash") && has(b, "east"),
          "**a short walk gives him the key that starts it and the direction** -- "
          "the Garden's parked announcement has done exactly this since v0.20.79");
    // v0.97.0: Aaron: "'Units' doesn't really mean anything to a player. Let's
    // ensure that all auto-drive and navigation functionality references 'km'."
    check(has(b, "0.7 kilometers") && !has(b, "units"),
          "**the walk is still spoken in units** -- a player has no idea how far a "
          "unit is and no reason to learn, and the catalog has said kilometers all "
          "along");

    RagArrivalLine(RAG_OVER_SPOT, "Shumi Village", 12288, "north", b, sizeof b);
    check(has(b, "12.3 kilometers") && has(b, "long walk"),
          "**past five kilometres it switches to kilometres and says it is a "
          "hike** -- 'one hundred and twenty-two hundred units' is not a distance "
          "anyone can hold in their head, which the Garden learned at v0.20.88");

    // ---- the marker wording promises nothing about the ground ------------
    RagArrivalLine(RAG_OVER_MARKER, "Mobile Balamb Garden", 0, "west", b, sizeof b);
    check(has(b, "Mobile Balamb Garden") && has(b, "Try pressing X"),
          "**with no landing row it says TRY** -- there is no generated ground "
          "under a Garden that is under way, so promising a parking space beside "
          "it would be inventing one");
    check(!has(b, "backslash") && !has(b, "hundred units"),
          "**and quotes no walk** -- a distance measured from a landing the mod "
          "did not choose is a number it cannot stand behind");

    // ---- it never lands by itself ---------------------------------------
    static const RagArrive kAll[3] = { RAG_OVER_PAD, RAG_OVER_SPOT, RAG_OVER_MARKER };
    for (int k = 0; k < 3; k++) {
        RagArrivalLine(kAll[k], "Anywhere", 100, "west", b, sizeof b);
        check(has(b, "Press X to land") || has(b, "pressing X to land"),
              "**every arrival asks HIM to land** -- the mod flies him to the spot "
              "and stops; the last act of arriving is the player's");
    }

    // ---- and the table and the rule agree about pads ---------------------
    {
        const RagLanding* fh = RagLandingFor("Fisherman's Horizon");
        check(fh != nullptr && RagArriveDecide(true, true, fh->kind, 0.0, RAG_ARRIVE_DIST) == RAG_OVER_PAD,
              "the shipped table's pads reach the pad branch");
        const RagLanding* bt = RagLandingFor("Balamb Town");
        check(bt != nullptr && RagArriveDecide(true, true, bt->kind, 0.0, RAG_ARRIVE_DIST) == RAG_OVER_SPOT,
              "and its walk rows reach the walk branch");
    }

    std::printf(bad ? "rag_arrival_test: FAILED (%d bad)\n"
                    : "rag_arrival_test: OK (%d bad)\n", bad);
    return bad ? 1 : 0;
}
