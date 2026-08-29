// nav_observe_window_test -- a NAV-OBSERVE window must not span a field change.
//
// WHY THIS EXISTS
// ---------------
// A sample is a subtraction between two player positions taken a few hundred
// milliseconds apart, and a field transition moves the player by teleport. The
// 20:25 log's two worst readings are both of these: rgroad2 UP reported
// DIVERGE=177deg -- delta (81,-1841), the exact opposite of the prediction --
// and rgexit1 LEFT reported 138deg, each timestamped on the second the field
// changed. Nothing was wrong with either room.
//
// It is not only the log. On a field whose CA is missing or degenerate the
// observer's samples feed the empirical calibration that overwrites the
// movement axes, so a bogus sample is a candidate for permanently steering him
// backwards in the one situation where he has no correct axes to fall back on.
#include <cstdio>
#include <cstdint>

#include "nav_observe_window_pure.inl"

static int bad = 0;
static void check(bool ok, const char* what)
{
    if (!ok) { std::printf("  BAD: %s\n", what); bad++; }
}

int main()
{
    std::printf("nav_observe_window_test\n");

    check(ObsWindowStep(0, false, false) == OBS_WIN_SKIP,
          "no arrow held is no window");
    check(ObsWindowStep(2, false, false) == OBS_WIN_SKIP,
          "**a diagonal is not sampled** -- UP+RIGHT matches either single-axis "
          "prediction, so the measured direction cannot settle between them");

    check(ObsWindowStep(1, true, false) == OBS_WIN_ANCHOR,
          "a new arrow starts a fresh window");
    check(ObsWindowStep(1, false, false) == OBS_WIN_ACCUMULATE,
          "**and the same arrow on the same field keeps counting** -- the rule "
          "must not throw away the ordinary case it exists to measure");

    check(ObsWindowStep(1, false, true) == OBS_WIN_ANCHOR,
          "**a field change under the window re-anchors it** -- this is the whole "
          "defect: the delta across a transition is the doorway, not the arrow, "
          "and it reported the exact opposite of the truth");
    check(ObsWindowStep(1, true, true) == OBS_WIN_ANCHOR,
          "and both at once is still just a re-anchor");
    check(ObsWindowStep(2, false, true) == OBS_WIN_SKIP,
          "**a field change does not promote a diagonal into a sample** -- the "
          "single-arrow gate comes first and stays first");

    std::printf(bad ? "nav_observe_window_test: FAILED (%d bad)\n"
                    : "nav_observe_window_test: OK (%d bad)\n", bad);
    return bad ? 1 : 0;
}
