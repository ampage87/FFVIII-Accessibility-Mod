// urgent_cue_compile.cpp -- the prison-escape prompt's model, driven with the
// BAT's own timeline.
//
//   g++ -std=c++17 -O0 -Isrc -o urgent_cue_compile tests/urgent_cue_compile.cpp
//
// The fixture is not invented. It is the 2026-08-20 log, to the second:
//
//   22:25:44  field 'gpexit2'
//   22:26:10  Rinoa speaks; disc01_03h starts
//   22:26:40  disc01_03h ends            <- the cue must NOT stop here
//   22:26:45  map jump to 'gppark1'      <- it must stop here
//
// v0.39.0 (#100).

#include <cstdio>
#include <cstdint>
#include <cstring>
#include <string>

#define FF8_URGENT_CUE_HOST_TEST 1
#include "field_urgent_prompt.inl"

static int bad = 0;
static void check(bool ok, const char* what)
{
    if (!ok) { std::printf("  BAD: %s\n", what); bad++; }
}

int main()
{
    // ---- matching ---------------------------------------------------------
    check(UrgentCueToArm("gpexit2", "disc01_03h.avi") == 0,
          "gpexit2 + disc01_03h arms the prison cue");
    check(UrgentCueToArm("GPEXIT2", "DISC01_03H.AVI") == 0,
          "and the match is case-insensitive on both, because FmvSkip lowercases and the field name does not");
    check(UrgentCueToArm("gpexit2", "disc01_03h") == 0,
          "the extension is optional");
    check(UrgentCueToArm("gpexit2", "disc01_02h.avi") == -1,
          "**the PRECEDING movie does not arm it** -- disc01_02h is the cutscene before the window opens");
    check(UrgentCueToArm("gpexit1", "disc01_03h.avi") == -1,
          "and neither does the right movie on the wrong field");
    check(UrgentCueToArm("gpexit2", "") == -1, "no movie, no arm");
    check(UrgentCueToArm("", "disc01_03h.avi") == -1, "no field, no arm");
    check(UrgentCueToArm("gpexit20", "disc01_03h.avi") == -1,
          "a field whose name merely starts with the cue's does not match");
    check(UrgentCueToArm("gpexit2", "disc01_03h.avix") == -1,
          "nor an AVI whose name merely starts with the cue's");

    // ---- the BAT's timeline, in milliseconds from the movie start ----------
    const uint32_t T0 = 1000000;          // disc01_03h starts (22:26:10)
    UrgentState st = { UrgentCueToArm("gpexit2", "disc01_03h.avi"), T0, 0, 0 };
    check(st.cue == 0, "armed");

    // Rinoa is still speaking for the first couple of seconds.
    check(!UrgentShouldSpeak(st, T0 +    0, true),  "silent at +0.0s: the head start has not elapsed");
    check(!UrgentShouldSpeak(st, T0 + 2000, true),  "silent at +2.0s: still inside the head start");
    check(!UrgentShouldSpeak(st, T0 + 3000, true),
          "**silent at +3.0s while Rinoa is still speaking** -- the head start expired but the channel is busy");
    check( UrgentShouldSpeak(st, T0 + 3000, false), "and speaks at +3.0s once she is done");

    st.lastSpokeAt = T0 + 3000; st.spokenCount = 1;
    check(!UrgentShouldSpeak(st, T0 + 4000, false), "does not repeat inside the interval");
    check( UrgentShouldSpeak(st, T0 + 5500, false), "repeats after it");

    // The movie ends at +30 s. The map jump is five seconds later, and he was
    // still walking: a cue that died with the movie would go quiet exactly when
    // it was still needed.
    check(UrgentStillLive(st, "gpexit2", T0 + 30000),
          "**still live at +30s, when disc01_03h ends** -- the movie arms it, the field keeps it");
    check(UrgentStillLive(st, "gpexit2", T0 + 34000), "still live at +34s, walking to the gateway");
    check(!UrgentStillLive(st, "gppark1", T0 + 35000),
          "**and dead the moment the field changes** -- leaving gpexit2 IS success");
    check(!UrgentStillLive(st, "gpexit2", T0 + 60000), "and dead at the cap even if the field never changes");
    check(UrgentStillLive(st, "gpexit2", T0 + 59999), "one millisecond before the cap it is alive");

    // Wrap-around: GetTickCount rolls over every 49.7 days and this arithmetic
    // must not care.
    const uint32_t NEAR_WRAP = 0xFFFFF000u;
    UrgentState w = { 0, NEAR_WRAP, 0, 0 };
    check(!UrgentShouldSpeak(w, (uint32_t)(NEAR_WRAP + 1000), false), "head start survives a tick wrap");
    check( UrgentShouldSpeak(w, (uint32_t)(NEAR_WRAP + 3000), false), "and expires across it");
    check( UrgentStillLive(w, "gpexit2", (uint32_t)(NEAR_WRAP + 30000)), "liveness survives a tick wrap");
    check(!UrgentStillLive(w, "gpexit2", (uint32_t)(NEAR_WRAP + 61000)), "and the cap still fires across it");

    // An idle state must never speak, whatever the clock says.
    UrgentState idle = { -1, 0, 0, 0 };
    check(!UrgentShouldSpeak(idle, 999999, false) && !UrgentStillLive(idle, "gpexit2", 999999),
          "an idle state is silent and dead");

    std::printf(bad ? "urgent_cue_compile: FAILED (%d bad)\n"
                    : "urgent_cue_compile: OK (%d bad)\n", bad);
    return bad ? 1 : 0;
}
