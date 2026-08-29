// button_mask_test.cpp -- BTNTEST masks and what to say about them
// (#centra, v0.120.0).
//
// Every assertion was written against a mutant. The masks are the ones the
// scanner actually finds on the disc, not invented ones.
#include <cstdio>
#include <cstring>
#include <cstdint>
#include <cstddef>

#include "button_mask_model.inl"

static int g_fail = 0;
static void CHECK(bool c, const char* w) { if (!c) { printf("FAIL: %s\n", w); g_fail++; } }
static void EQ(const char* got, const char* want, const char* w)
{ if (strcmp(got, want) != 0) { printf("FAIL: %s\n  got  \"%s\"\n  want \"%s\"\n", w, got, want); g_fail++; } }

int main()
{
    // THE BIT ORDER. 0x00C0 is the commonest BTNTEST mask in the game -- 101 of
    // the 112 masked LINE entities disc-wide -- and under this order it reads
    // "Cross or Square", the action pair. Under the raw PlayStation pad order
    // (Select first) the same mask would be "Left or L2", which no script would
    // ever wait on. That is the independent confirmation of v0.67.3's order.
    CHECK(strcmp(BtnPadName(6), "Cross") == 0, "bit 6 is Cross");
    CHECK(strcmp(BtnPadName(7), "Square") == 0, "bit 7 is Square");
    CHECK(strcmp(BtnPadName(4), "Triangle") == 0, "bit 4 is Triangle");
    CHECK(strcmp(BtnPadName(12), "Up") == 0, "bit 12 is Up");
    CHECK(strcmp(BtnPadName(15), "Left") == 0, "bit 15 is Left");
    CHECK(BtnPadName(-1) == nullptr, "no name below the range");
    CHECK(BtnPadName(16) == nullptr, "no name above the range");

    // The Centra ladder, and the D-pad masks from the same code panel, which is
    // the one place a human has confirmed the mapping by feel.
    CHECK(BtnMaskNthButton(0x00C0, 0) == 6, "the ladder's first button is Cross");
    CHECK(BtnMaskNthButton(0x00C0, 1) == 7, "its second is Square");
    CHECK(BtnMaskNthButton(0x00C0, 2) == -1, "and there is no third");
    CHECK(BtnMaskCount(0x00C0) == 2, "the ladder takes either of two buttons");
    CHECK(BtnMaskNthButton(0x1000, 0) == 12, "0x1000 is Up");
    CHECK(BtnMaskNthButton(0x8000, 0) == 15, "0x8000 is Left");
    CHECK(BtnMaskNthButton(0x0010, 0) == 4,  "0x0010 is Triangle");
    CHECK(BtnMaskNthButton(0x00C0, -1) == -1, "a negative index is not a button");

    // "PRESS ANYTHING" IS NOT AN INSTRUCTION. 0xFFFF appears 16 times on the
    // disc and every one is a cutscene skip; announcing "press L2" there would
    // be both arbitrary and wrong. Kills `mask != 0` on its own.
    CHECK(BtnMaskIsAnyButton(0xFFFF), "0xFFFF is the everything-mask");
    CHECK(!BtnMaskIsAnyButton(0x00C0), "a real action is not the everything-mask");
    CHECK(BtnMaskIsActionable(0x00C0), "the ladder is worth announcing");
    CHECK(BtnMaskIsActionable(0x0080), "so is a Square-only switch");
    CHECK(!BtnMaskIsActionable(0), "no mask, nothing to say");
    CHECK(!BtnMaskIsActionable(0xFFFF), "the everything-mask is not an instruction");

    // THE SENTENCE. The key beats the pad name, because "press Cross" is
    // useless to someone playing on a keyboard and the engine knows the binding.
    char b[160];
    BtnPressText(b, sizeof b, "Left Ladder Up", "Enter", "Cross", nullptr);
    EQ(b, "Left Ladder Up. Press Enter to use it.", "the key wins when there is one");

    // ...and the pad name is the fallback, not the default. Kills a mutant that
    // prefers padName.
    BtnPressText(b, sizeof b, "Left Ladder Up", nullptr, "Cross", nullptr);
    EQ(b, "Left Ladder Up. Press the Cross button to use it.", "the pad name is the fallback");
    BtnPressText(b, sizeof b, "Left Ladder Up", "", "Cross", nullptr);
    EQ(b, "Left Ladder Up. Press the Cross button to use it.", "an empty key is no key");

    // A label with nothing to press, and something to press with no label, both
    // have to come out as sentences rather than fragments.
    BtnPressText(b, sizeof b, "Left Ladder Up", nullptr, nullptr, nullptr);
    EQ(b, "Left Ladder Up.", "no button at all -> just the name");
    BtnPressText(b, sizeof b, nullptr, "Enter", "Cross", nullptr);
    EQ(b, "Press Enter to use it.", "no label -> just the instruction");
    BtnPressText(b, sizeof b, "", "Enter", nullptr, nullptr);
    EQ(b, "Press Enter to use it.", "an empty label is no label");
    BtnPressText(b, sizeof b, nullptr, nullptr, nullptr, nullptr);
    EQ(b, "", "nothing in, nothing out");

    // A short buffer stays terminated rather than running off the end.
    char tiny[10];
    BtnPressText(tiny, sizeof tiny, "Left Ladder Up", "Enter", "Cross", nullptr);
    CHECK(strlen(tiny) < sizeof(tiny), "a short buffer stays terminated");
    BtnPressText(nullptr, 0, "x", "y", "z", "w");   // must not fault


    // v0.121.0: THE BRIDGE HAS TO SAY WHERE IT LEADS. On crtower2 he asked for
    // "Left Ladder Down", which is up on the landing, so the island-bridge
    // search sent him to "Left Ladder Up" -- correctly -- and the mod announced
    // the ladder he had NOT chosen with no explanation.
    BtnPressText(b, sizeof b, "Left Ladder Up", "X", "Cross", "Left Ladder Down");
    EQ(b, "Left Ladder Up. Press X to use it. That is the way to Left Ladder Down.",
       "a bridge names the target it leads to");

    // ...but not when the bridge IS the target. Kills a mutant that appends
    // unconditionally and produces "Ladder Up ... the way to Ladder Up."
    BtnPressText(b, sizeof b, "Ladder Up", "X", "Cross", "Ladder Up");
    EQ(b, "Ladder Up. Press X to use it.", "no tail when the bridge is the target");
    BtnPressText(b, sizeof b, "Ladder Up", "X", "Cross", "");
    EQ(b, "Ladder Up. Press X to use it.", "an empty leads-to adds nothing");

    // And never onto a sentence that does not exist -- a bare " That is the way
    // to X." with no action in front of it is not a thing to say.
    BtnPressText(b, sizeof b, nullptr, nullptr, nullptr, "Left Ladder Down");
    EQ(b, "", "no tail without a sentence to hang it on");

    // The tail still fits after a fallback pad name, and a buffer too small for
    // it keeps the instruction rather than a truncated tail.
    BtnPressText(b, sizeof b, "Left Ladder Up", nullptr, "Cross", "Left Ladder Down");
    EQ(b, "Left Ladder Up. Press the Cross button to use it. That is the way to Left Ladder Down.",
       "the tail follows the pad-name form too");
    {
        char mid[40];
        BtnPressText(mid, sizeof mid, "Ladder Up", "X", "Cross", "Left Ladder Down");
        CHECK(strlen(mid) < sizeof(mid), "a mid-sized buffer stays terminated");
        CHECK(strncmp(mid, "Ladder Up. Press X to use it.", 29) == 0,
              "and keeps the instruction it started with");
    }


    // ------------------------------------------------------------------
    // v0.122.0: NEAR AN END IS NOT ON THE LINE.
    // ------------------------------------------------------------------
    // crroof1's ladder, and the position the drive kept stopping at: the line
    // runs (862,-952) to (971,-839) and the player was at (1050,-753). Sixty
    // units from the endpoint by the old test, and three quarters of the
    // segment's own length past its far end by this one.
    {
        const float X1 = 862.0f, Y1 = -952.0f, X2 = 971.0f, Y2 = -839.0f;
        const float t = BtnLineParam(1050.0f, -753.0f, X1, Y1, X2, Y2);
        CHECK(t > 1.5f, "the log's stopping point is well past the far end");
        CHECK(!BtnLineIsAlongside(t), "and is therefore NOT on the ladder");

        // Both endpoints and the middle are on it; so is a little past either
        // end, because the 15%% margin is what v0.18.3.304 already settled for
        // exactly this geometry.
        CHECK(BtnLineIsAlongside(BtnLineParam(X1, Y1, X1, Y1, X2, Y2)), "the first endpoint is on it");
        CHECK(BtnLineIsAlongside(BtnLineParam(X2, Y2, X1, Y1, X2, Y2)), "the second endpoint is on it");
        CHECK(BtnLineIsAlongside(0.5f), "the middle is on it");
        CHECK(BtnLineIsAlongside(0.0f) && BtnLineIsAlongside(1.0f), "the ends are on it");
        CHECK(BtnLineIsAlongside(-0.15f) && BtnLineIsAlongside(1.15f), "the margin is inclusive");
        CHECK(!BtnLineIsAlongside(-0.16f) && !BtnLineIsAlongside(1.16f), "and bounded");

        // Standing beside the line but off to one side is still alongside --
        // the projection is along the line, not a radius. Kills a mutant that
        // measures distance instead of the parameter.
        const float side = BtnLineParam(860.0f, -840.0f, X1, Y1, X2, Y2);   // 80u off the midpoint, at right angles
        CHECK(BtnLineIsAlongside(side), "perpendicular offset does not move the foot off the segment");

        // A degenerate segment has no "past the end" to be past.
        CHECK(BtnLineIsAlongside(BtnLineParam(50.0f, 50.0f, 10.0f, 10.0f, 10.0f, 10.0f)),
              "a zero-length line is always alongside");
    }

    printf("button_mask_test: fail=%d\n", g_fail);
    return g_fail ? 1 : 0;
}
