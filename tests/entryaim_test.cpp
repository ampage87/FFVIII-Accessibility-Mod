// entryaim_test.cpp -- v0.21.6 (#79)
//
// The entry-polygon firing areas. A field entry fires only while the player
// stands on a wmx.obj polygon with byte 0x0E bit 3 set -- the flag sub_545EA0
// tests before it will evaluate a single entry program -- and only if that
// polygon is also foot-walkable (byte 0x0F bit 7). s_entryAims holds one aim
// point and bbox per destination, generated from those polygons.
//
// This file exists because the generator is the thing that could be wrong. The
// five aim points below were found by hand, in the field, over several BATs
// before any of this scanning existed. If the generator can reproduce those
// five it can be trusted for the ones nobody has walked to yet -- and if a
// future edit moves an aim point off its own patch, this fails.
//
//   g++ -std=c++17 -O0 -Isrc -o entryaim_test tests/entryaim_test.cpp

#include <cstdio>
#include <cstdint>
#include <cstring>
#include <cmath>

namespace Log { void World(const char*, ...) {} }

#include "world_map_trigger_data.inl"

static int bad = 0;
static void check(bool ok, const char* what)
{
    if (!ok) { bad++; printf("  BAD: %s\n", what); }
}

// sub_553910's segment index -- 32 x 24 squares of 8192 units. A patch and the
// program that owns it are the same segment by construction, which is what makes
// a binding a fact rather than a nearest-neighbour guess.
static int SegIndex(int32_t x, int32_t y)
{
    const int row = (int)((((uint32_t)(y + 0x48000)) % 0x30000) >> 13);
    const int col = (int)((((uint32_t)(x + 0x60000)) & 0x3FFFF) >> 13);
    return row * 32 + col;
}

int main()
{
    printf("entry firing areas: %d\n", ENTRY_AIM_COUNT);

    // 1. THE INVARIANT. An aim point outside its own bbox is a typo that would
    //    send the drive to a spot that cannot open the door -- exactly the bug
    //    this whole table exists to prevent.
    for (int i = 0; i < ENTRY_AIM_COUNT; i++) {
        const EntryAimInfo& e = s_entryAims[i];
        if (!(e.aimX >= e.x0 && e.aimX <= e.x1 && e.aimY >= e.y0 && e.aimY <= e.y1)) {
            bad++;
            printf("  BAD: %s aim (%d,%d) is outside its own bbox x[%d,%d] y[%d,%d]\n",
                   e.name, e.aimX, e.aimY, e.x0, e.x1, e.y0, e.y1);
        }
        if (e.x0 >= e.x1 || e.y0 >= e.y1) {
            bad++; printf("  BAD: %s has an empty or inverted bbox\n", e.name);
        }
    }
    printf("every aim point lies inside its own firing area\n");

    // 2. Names must be unique -- FindEntryAim returns the FIRST match, so a
    //    duplicate would silently shadow whichever row came second.
    for (int i = 0; i < ENTRY_AIM_COUNT; i++)
        if (FindEntryAim(s_entryAims[i].name) != i) {
            bad++; printf("  BAD: duplicate firing-area name '%s'\n", s_entryAims[i].name);
        }
    check(FindEntryAim("No Such Place") < 0, "FindEntryAim invented a match");
    printf("names are unique and lookup is exact\n");

    // 3. THE REGRESSION GUARD. These five were found by hand in the field. The
    //    generator's output must still sit inside each proven area -- that
    //    agreement is the only reason to trust the generated rows.
    struct { const char* name; int32_t x, y; } PROVEN[] = {
        { "Timber",           -22580,  -5291 },
        { "Dollet",           -14513, -39119 },
        { "Balamb Town",       12560, -26800 },
        { "Fire Cavern",       30239, -29528 },
        { "Galbadia Station", -38914, -24767 },
    };
    for (unsigned k = 0; k < sizeof(PROVEN)/sizeof(PROVEN[0]); k++) {
        const int i = FindEntryAim(PROVEN[k].name);
        if (i < 0) { bad++; printf("  BAD: %s lost its row\n", PROVEN[k].name); continue; }
        const EntryAimInfo& e = s_entryAims[i];
        if (!(PROVEN[k].x >= e.x0 && PROVEN[k].x <= e.x1 &&
              PROVEN[k].y >= e.y0 && PROVEN[k].y <= e.y1)) {
            bad++;
            printf("  BAD: the field-proven %s point (%d,%d) fell outside its bbox\n",
                   PROVEN[k].name, PROVEN[k].x, PROVEN[k].y);
        }
    }
    printf("all five field-proven entry points still lie inside their areas\n");

    // 4. **EDEA'S HOUSE.** The whole point of v0.21.6. Only 7 of segment 652's
    //    103 entry polygons are foot-walkable; they cover x[-29975,-29310]
    //    y[69632,70078]. The aim must be inside that, the two markers the mod
    //    shipped before must be outside it, and so must the closest Aaron
    //    actually got during the 2026-08-16 BAT -- 287 units short, with every
    //    gate in the exe passing on every frame.
    {
        const int i = FindEntryAim("Edea's House");
        check(i >= 0, "Edea's House has no firing area");
        if (i >= 0) {
            const EntryAimInfo& e = s_entryAims[i];
            check(e.x0 == -29975 && e.x1 == -29310 && e.y0 == 69632 && e.y1 == 70078,
                  "Edea's House bbox is not the seven walkable trigger triangles");
            struct { int32_t x, y; const char* what; } OUT[] = {
                { -23150, 62853, "the original marker" },
                { -28950, 70090, "the v0.21.1 page-8 apron marker" },
                { -29144, 70004, "Aaron's closest approach in the 2026-08-16 BAT" },
                { -29585, 70739, "his position at the lighthouse" },
            };
            for (unsigned k = 0; k < sizeof(OUT)/sizeof(OUT[0]); k++) {
                const bool in = OUT[k].x >= e.x0 && OUT[k].x <= e.x1 &&
                                OUT[k].y >= e.y0 && OUT[k].y <= e.y1;
                if (in) {
                    bad++;
                    printf("  BAD: %s (%d,%d) is inside the patch -- then it should have "
                           "opened, and the model is wrong\n",
                           OUT[k].what, OUT[k].x, OUT[k].y);
                }
            }
            const double dx = e.aimX - (-29144), dy = e.aimY - 70004;
            printf("Edea's House: aim (%d,%d), %.0f units from where the BAT stalled; "
                   "all four historical positions are outside the patch\n",
                   e.aimX, e.aimY, sqrt(dx*dx + dy*dy));
        }
    }

    // 5. The known-good control. Chocobo Forest 7 opened on demand at
    //    (-20953,68906) in an earlier session, so its patch must be the one
    //    beside that point -- and that point sits just outside it, which is
    //    what "the marker is not the doorstep" means everywhere in this table.
    {
        const int i = FindEntryAim("Chocobo Forest 7");
        check(i >= 0, "Chocobo Forest 7 has no firing area");
        if (i >= 0) {
            const EntryAimInfo& e = s_entryAims[i];
            const double d = sqrt(pow(e.aimX - (-20953.0), 2) + pow(e.aimY - 68906.0, 2));
            check(d < 1000.0, "the forest patch is nowhere near the point that worked");
            printf("Chocobo Forest 7: aim (%d,%d), %.0f units from the position that "
                   "opened it -- same patch, and the old marker was outside\n",
                   e.aimX, e.aimY, d);
        }
    }

    // 5b. **WINHILL, AND THE REASON THE FIVE WERE HELD BACK.** Program 24 splits
    //     segment 393 at Xoff 6144 -- destination 14 on the high side, 15 on the
    //     low -- and v0.21.6's whole-patch aim landed at Xoff 5825, the wrong
    //     half, a different destination from the one the marker sits over. The
    //     ENTIRE bbox must clear the split, not just the aim point, or a drive
    //     that arrives at the near edge opens the wrong door.
    {
        const int i = FindEntryAim("Winhill");
        check(i >= 0, "Winhill has no firing area");
        if (i >= 0) {
            const EntryAimInfo& e = s_entryAims[i];
            const int32_t loOff = (e.x0 + 0x60000) & 0x1FFF;
            const int32_t aiOff = (e.aimX + 0x60000) & 0x1FFF;
            check(loOff > 6144, "Winhill's bbox crosses the Xoff 6144 clause split");
            check(aiOff > 6144, "Winhill's aim is on the destination-15 side of the split");
            check(!(e.aimX == -51519 && e.aimY == 6326),
                  "the v0.21.6 whole-patch aim came back -- it is on the wrong half");
            printf("Winhill: aim Xoff %d, bbox west edge Xoff %d -- both clear the "
                   "6144 split, so the whole area is destination 14\n", aiOff, loOff);
        }
    }

    // 5c. The four markers whose binding is not an inference: each sits in the
    //     same 8192-unit segment as its own patch, and that segment has exactly
    //     one entry program. If a future edit moves an aim out of its marker's
    //     segment, the binding has silently become a guess again.
    {
        struct { const char* name; int32_t mx, my; } SAME[] = {
            { "Deling City",       -61806, -28649 },
            { "D-District Prison", -55306,  -4841 },
            { "Trabia Garden",      48893, -57979 },
            { "Winhill",           -50285,   6320 },
        };
        for (unsigned k = 0; k < sizeof(SAME)/sizeof(SAME[0]); k++) {
            const int i = FindEntryAim(SAME[k].name);
            if (i < 0) { bad++; printf("  BAD: %s lost its row\n", SAME[k].name); continue; }
            const EntryAimInfo& e = s_entryAims[i];
            const int ms = SegIndex(SAME[k].mx, SAME[k].my);
            const int as = SegIndex(e.aimX, e.aimY);
            if (ms != as) {
                bad++;
                printf("  BAD: %s marker is in segment %d but its aim is in %d -- "
                       "different segment means a different entry program\n",
                       SAME[k].name, ms, as);
            }
        }
        printf("Deling City, D-District Prison, Trabia Garden and Winhill each aim "
               "inside their own marker's segment\n");
    }

    // 5c-2. v0.56.0 (#118): ESTHAR. Every aim must be in the segment whose
    //       program grants it. For Sorceress Memorial and Tears' Point that is
    //       also the MARKER's segment, which makes those two bindings facts of
    //       the same class as Deling City's. The other three are argued in
    //       world_map_trigger_data.inl; here we pin the segment each one claims,
    //       so an edit that drifts an aim into a neighbouring square -- which
    //       would silently change which program owns it -- fails.
    {
        struct { const char* name; int seg; bool markerSameSeg; int32_t mx, my; } E[] = {
            { "Esthar City",        438, false, 57011,  -2295 },
            { "Lunatic Pandora Lab",378, false, 79521,  -9135 },
            { "Lunar Gate",         443, false, 88021,   7865 },
            { "Sorceress Memorial", 441, true,  81521,  11865 },
            { "Tears' Point",       506, true,  83021,  31865 },
        };
        for (unsigned k = 0; k < sizeof(E)/sizeof(E[0]); k++) {
            const int i = FindEntryAim(E[k].name);
            if (i < 0) { bad++; printf("  BAD: %s has no firing area\n", E[k].name); continue; }
            const EntryAimInfo& e = s_entryAims[i];
            const int as = SegIndex(e.aimX, e.aimY);
            if (as != E[k].seg) {
                bad++;
                printf("  BAD: %s aims in segment %d, but its entry program is in %d\n",
                       E[k].name, as, E[k].seg);
            }
            // The aim must be inside its own bbox -- a generated row whose aim
            // fell outside its area would steer the drive at a point the mow
            // logic then refuses to visit.
            if (!(e.aimX >= e.x0 && e.aimX <= e.x1 && e.aimY >= e.y0 && e.aimY <= e.y1)) {
                bad++; printf("  BAD: %s aim is outside its own bbox\n", E[k].name);
            }
            // Where the marker shares the segment, say so -- that is the
            // evidence class, and losing it would matter.
            if (E[k].markerSameSeg && SegIndex(E[k].mx, E[k].my) != E[k].seg) {
                bad++;
                printf("  BAD: %s marker was supposed to be in segment %d\n", E[k].name, E[k].seg);
            }
            // **AND THE OLD MARKER MUST NOT BE INSIDE THE NEW AREA.** Every one
            // of these five markers stands on ZERO entry triangles -- that is
            // the whole finding -- so if a future edit produced an area that
            // contained the old marker, the area would be describing ground the
            // engine does not fire on.
            if (E[k].mx >= e.x0 && E[k].mx <= e.x1 && E[k].my >= e.y0 && E[k].my <= e.y1) {
                bad++;
                printf("  BAD: %s's dead marker (%d,%d) is inside its new firing area\n",
                       E[k].name, E[k].mx, E[k].my);
            }
        }
        printf("Esthar's five aims each sit in the segment whose program grants them, "
               "inside their own bbox, and none contains the dead marker it replaces\n");
    }

    // 5c-3. The Esthar retarget is a big move, and a big move is exactly where a
    //       transcription slip hides. Pin the measured distances.
    {
        struct { const char* name; int32_t mx, my; double lo, hi; } D[] = {
            { "Sorceress Memorial", 81521,  11865,  3600,  3800 },
            { "Tears' Point",       83021,  31865,  6600,  6800 },
            { "Lunatic Pandora Lab",79521,  -9135,  7300,  7500 },
            { "Lunar Gate",         88021,   7865,  7600,  7800 },
            { "Esthar City",        57011,  -2295, 12000, 12200 },
        };
        for (unsigned k = 0; k < sizeof(D)/sizeof(D[0]); k++) {
            const int i = FindEntryAim(D[k].name);
            if (i < 0) continue;
            const EntryAimInfo& e = s_entryAims[i];
            const double dx = (double)e.aimX - D[k].mx, dy = (double)e.aimY - D[k].my;
            const double d  = std::sqrt(dx*dx + dy*dy);
            if (d < D[k].lo || d > D[k].hi) {
                bad++;
                printf("  BAD: %s moved %.0fu from its marker, expected %.0f-%.0f\n",
                       D[k].name, d, D[k].lo, D[k].hi);
            }
        }
        printf("the five Esthar retargets moved 3.7-12.1 km, each within 100u of "
               "the measured value\n");
    }

    // 5d. **THE BAT POSITIONS**, and an honest measurement of what they are.
    //
    //     Where the game actually let him through is the only ground truth this
    //     table has -- but the arrival line's `lastPos` is the DRIVE's last
    //     sample, not the frame the engine ran its polygon test on. At running
    //     speed that is over 100 units per tick, so lastPos routinely sits just
    //     past the patch it fired on: checked against the raw triangles, all
    //     four of the 2026-08-16 entries are 59-166 units outside one. Asserting
    //     strict containment here would be asserting something the instrument
    //     cannot measure, so the bound is the sampling lag.
    //
    //     What this still catches is the failure that matters: an area bound to
    //     the wrong door. v0.21.7 gave Trabia only segment 149 and he entered
    //     from the segment-150 half, 1,363 units away and 818 outside that
    //     bbox -- this check fails on that and passes on the corrected row.
    {
        const int32_t SAMPLE_LAG = 200;      // > the 166u worst case measured
        struct { const char* name; int32_t x, y; const char* when; } ENTERED[] = {
            { "Winhill",                  -50668,   6331, "field 0x029E, the northern door" },
            { "Deling City",              -61095, -29919, "glrent1" },
            { "Tomb of the Unknown King", -42310, -36761, "" },
            { "Trabia Garden",             49966, -58834, "gnview1 -- the segment-150 half" },
        };
        int32_t worst = 0; const char* worstName = "";
        for (unsigned k = 0; k < sizeof(ENTERED)/sizeof(ENTERED[0]); k++) {
            const int i = FindEntryAim(ENTERED[k].name);
            if (i < 0) { bad++; printf("  BAD: %s lost its row\n", ENTERED[k].name); continue; }
            const EntryAimInfo& e = s_entryAims[i];
            int32_t dx = 0, dy = 0;
            if (ENTERED[k].x < e.x0) dx = e.x0 - ENTERED[k].x;
            if (ENTERED[k].x > e.x1) dx = ENTERED[k].x - e.x1;
            if (ENTERED[k].y < e.y0) dy = e.y0 - ENTERED[k].y;
            if (ENTERED[k].y > e.y1) dy = ENTERED[k].y - e.y1;
            const int32_t miss = dx > dy ? dx : dy;
            if (miss > worst) { worst = miss; worstName = ENTERED[k].name; }
            if (miss > SAMPLE_LAG) {
                bad++;
                printf("  BAD: %s was entered at (%d,%d) [%s], %d units outside its own "
                       "firing area x[%d,%d] y[%d,%d] -- too far to be sampling lag, so "
                       "this area is bound to the wrong door\n",
                       ENTERED[k].name, ENTERED[k].x, ENTERED[k].y, ENTERED[k].when,
                       miss, e.x0, e.x1, e.y0, e.y1);
            }
        }
        printf("all four 2026-08-16 entry positions sit within the %du sampling lag of "
               "their own firing area (worst: %s at %du)\n", SAMPLE_LAG, worstName, worst);
    }

    // 5e. Trabia spans two programs. Segment 150 only grants destination 19 above
    //     Yoff 4096, so every part of the bbox that falls in 150 must clear it,
    //     or the rectangle would promise a door the low half does not open.
    {
        const int i = FindEntryAim("Trabia Garden");
        if (i >= 0) {
            const EntryAimInfo& e = s_entryAims[i];
            const int32_t lo = (int32_t)((((uint32_t)(e.y0 + 0x48000)) % 0x30000) & 0x1FFF);
            const int32_t hi = (int32_t)((((uint32_t)(e.y1 + 0x48000)) % 0x30000) & 0x1FFF);
            check(lo > 4096 && hi > 4096,
                  "part of Trabia's bbox is below the Yoff 4096 split, where segment 150 "
                  "gives destination 35 instead of 19");
            printf("Trabia Garden: bbox spans Yoff %d..%d, entirely above the 4096 split "
                   "that segment 150's clause needs\n", lo, hi);
        }
    }

    // 6. Firing areas double as no-go zones for routes to OTHER destinations
    //    (planner2's +4096 penalty), so two areas must never overlap -- an
    //    overlap would make one destination permanently expensive to reach.
    for (int i = 0; i < ENTRY_AIM_COUNT; i++)
        for (int j = i + 1; j < ENTRY_AIM_COUNT; j++) {
            const EntryAimInfo& a = s_entryAims[i], & b = s_entryAims[j];
            if (a.x0 <= b.x1 && b.x0 <= a.x1 && a.y0 <= b.y1 && b.y0 <= a.y1) {
                bad++; printf("  BAD: %s and %s overlap\n", a.name, b.name);
            }
        }
    printf("no two firing areas overlap\n");

    printf("entryaim: %s (%d bad)\n", bad ? "FAILED" : "OK", bad);
    return bad ? 1 : 0;
}
