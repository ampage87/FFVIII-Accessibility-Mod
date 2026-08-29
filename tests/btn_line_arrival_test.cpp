// btn_line_arrival_test.cpp -- the drive's arrival on a button line (#centra, v0.130.0).
//
// Every fixture here is a real line from Aaron's logs, at a real position he
// was actually standing in, with the outcome he actually got. The point of the
// build is that "arrived" stops meaning "somewhere on this line's infinite
// extension".
#include <cstdio>
#include <cmath>
#include <cstdint>
#include <cstring>

#include "button_mask_model.inl"

static int g_fail = 0;
static void CHECK(bool c, const char* w) { if (!c) { printf("FAIL: %s\n", w); g_fail++; } }

// crroof1's two ladder lines, as the SETLINE hook captured them.
static const float UP_X1 = 862, UP_Y1 = -952, UP_X2 = 971, UP_Y2 = -839;
static const float DN_X1 = 444, DN_Y1 = -509, DN_X2 = 505, DN_Y2 = -385;
static const float ARRIVE = 60.0f;

static bool Arrive(float px, float py, float x1, float y1, float x2, float y2)
{
    const float t = BtnLineParam(px, py, x1, y1, x2, y2);
    return BtnLineArrivalOk(t, BtnPointSegDistSq(px, py, x1, y1, x2, y2), ARRIVE);
}

int main()
{
    // THE 17:58:25 FIXTURE. "arrived at a BTNTEST line 0x00C0 -- Ladder Down.
    // Press X to use it." The player was at (175,-329), the position the
    // drive-vec line one tick earlier recorded. crroof1's Ladder Down segment is
    // 321 units from there. He pressed X and nothing happened.
    {
        const float t = BtnLineParam(175, -329, DN_X1, DN_Y1, DN_X2, DN_Y2);
        CHECK(BtnLineIsAlongside(t),
              "the foot falls INSIDE the segment span -- which is exactly why "
              "the projection parameter alone could not catch this");
        const float d = sqrtf(BtnPointSegDistSq(175, -329, DN_X1, DN_Y1, DN_X2, DN_Y2));
        CHECK(d > 300.0f && d < 340.0f, "while the segment is 320 units away");
        CHECK(!Arrive(175, -329, DN_X1, DN_Y1, DN_X2, DN_Y2),
              "so it is NOT an arrival any more");
    }

    // THE 18:15 AND 18:21 FIXTURES. t=1.02 and t=1.03 -- off the end, inside the
    // old ±15% margin, announced, and the press did nothing both times.
    {
        const float dx = UP_X2 - UP_X1, dy = UP_Y2 - UP_Y1;
        for (float t = 1.01f; t < 1.06f; t += 0.01f) {
            const float px = UP_X1 + dx * t, py = UP_Y1 + dy * t;
            CHECK(BtnLineIsAlongside(t), "the old margin called this alongside");
            CHECK(!Arrive(px, py, UP_X1, UP_Y1, UP_X2, UP_Y2),
                  "past the end of the ladder line is not an arrival");
        }
        // And just inside the end still is, so the rule is "on the segment",
        // not "near the middle" -- being too strict costs a drive that runs its
        // clock out, and this line is only 157 units long.
        for (float t = 0.0f; t <= 1.0f; t += 0.1f) {
            const float px = UP_X1 + dx * t, py = UP_Y1 + dy * t;
            CHECK(Arrive(px, py, UP_X1, UP_Y1, UP_X2, UP_Y2),
                  "standing anywhere ON the segment is");
        }
    }

    // THE ONES THAT WORKED stay working: t=0.99 (17:31:54, climbed at 17:32:04),
    // t=0.76 standing ON the line, and t=0.33 (16:54:52).
    {
        const float dx = UP_X2 - UP_X1, dy = UP_Y2 - UP_Y1;
        CHECK(Arrive(UP_X1 + dx * 0.99f, UP_Y1 + dy * 0.99f, UP_X1, UP_Y1, UP_X2, UP_Y2),
              "t=0.99 on the segment arrives");
        CHECK(Arrive(UP_X1 + dx * 0.33f, UP_Y1 + dy * 0.33f, UP_X1, UP_Y1, UP_X2, UP_Y2),
              "and so does t=0.33");
    }

    // PERPENDICULAR DISTANCE IS WHAT THE OLD TEST WAS MISSING. Walking parallel
    // to a line at 300 units sweeps t across the whole segment without ever
    // approaching it -- the failure in one sentence.
    {
        const float dx = DN_X2 - DN_X1, dy = DN_Y2 - DN_Y1;
        const float len = sqrtf(dx * dx + dy * dy);
        const float nx = -dy / len, ny = dx / len;   // unit perpendicular
        for (float t = 0.1f; t <= 0.9f; t += 0.2f) {
            const float bx = DN_X1 + dx * t, by = DN_Y1 + dy * t;
            CHECK(BtnLineIsAlongside(BtnLineParam(bx + nx * 300, by + ny * 300,
                                                  DN_X1, DN_Y1, DN_X2, DN_Y2)),
                  "300 units to the side is still 'alongside'");
            CHECK(!Arrive(bx + nx * 300, by + ny * 300, DN_X1, DN_Y1, DN_X2, DN_Y2),
                  "but it is not an arrival");
            CHECK(Arrive(bx + nx * 50, by + ny * 50, DN_X1, DN_Y1, DN_X2, DN_Y2),
                  "while 50 units to the side is");
            CHECK(!Arrive(bx + nx * 70, by + ny * 70, DN_X1, DN_Y1, DN_X2, DN_Y2),
                  "and 70 is past the arrival radius");
        }
    }

    // A degenerate line must not divide by zero or arrive from across the field.
    CHECK(Arrive(100, 100, 100, 100, 100, 100), "a zero-length line arrives at its point");
    CHECK(!Arrive(900, 900, 100, 100, 100, 100), "and not from 1100 units away");

    printf("btn_line_arrival_test: fail=%d\n", g_fail);
    return g_fail ? 1 : 0;
}
