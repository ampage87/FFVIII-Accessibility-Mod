// vehsig_test.cpp -- v0.21.0 (#79)
//
// The foot/vehicle discriminator, driven by REAL FRAMES out of Aaron's
// 2026-08-15 walk to Edea's House. Nothing in the tree compiled
// world_map_drive_exec.inl before a Windows build, which is how a heuristic
// that fires on a walking man survived. This is the check that would have
// caught it.
//
//   g++ -std=c++17 -O0 -Isrc -o vehsig_test tests/vehsig_test.cpp

#include <cstdio>
#include <cstdint>

#include "world_map_vehsig.inl"

static int bad = 0;
static void check(bool ok, const char* what)
{
    if (!ok) { bad++; printf("  BAD: %s\n", what); }
}

// ---------------------------------------------------------------------------
// THE FIXTURE, and why it is trustworthy.
//
// Every disagreement frame the ring saw before the 21:18:56 verdict, recovered
// from the [MFRAME] lines of ff8_world.log: {gap, motion sided with mh}. The
// gap is |mh - camYaw| folded to 0..2048. Replayed through the OLD policy this
// reaches 20 of 24 and latches -- the fixture reproduces the reported defect
// exactly, which is what makes it a fixture and not a guess.
struct VsFrame { int gap; int mhSided; };
static const VsFrame WALK_TO_EDEA[] = {
    {507,0}, {361,1}, {574,1}, {1086,1}, {1598,1}, {1535,1}, {1533,1},
    {1530,1}, {1526,1}, {1521,1}, {1542,1}, {1535,1}, {1527,1}, {1518,1},
    {1526,1}, {1523,1}, {1529,1}, {1516,1}, {1977,1}, {1465,0}, {953,0},
    {441,0}, {881,1}, {369,0}, {452,1}
};
static const int WALK_N = (int)(sizeof(WALK_TO_EDEA) / sizeof(WALK_TO_EDEA[0]));

// The policy as it stood before v0.21.0: no turn guard, no id veto.
static bool OldPolicyLatches(const VsFrame* f, int n)
{
    uint32_t ring = 0; int count = 0;
    for (int i = 0; i < n; i++) {
        if (f[i].gap <= VS_GAP_MIN) continue;
        ring = ((ring << 1) | (uint32_t)(f[i].mhSided ? 1 : 0)) & 0x00FFFFFFu;
        if (count < VS_WINDOW) count++;
        if (count < VS_WINDOW) continue;
        int v = 0;
        for (int b = 0; b < VS_WINDOW; b++) v += (int)((ring >> b) & 1u);
        if (v >= VS_MAJORITY) return true;
    }
    return false;
}

static int RunFrames(const VsFrame* f, int n, bool idSaysOnFoot,
                     int* vetoes, int* votesOut)
{
    VehSigRing st; VehSigReset(st);
    int latched = 0; if (vetoes) *vetoes = 0;
    for (int i = 0; i < n; i++) {
        int votes = 0;
        VehSigResult r = VehSigFeed(st, f[i].gap, f[i].mhSided != 0,
                                    idSaysOnFoot, &votes);
        if (r == VS_LATCH)   { latched = 1; if (votesOut) *votesOut = votes; break; }
        if (r == VS_ID_VETO) { if (vetoes) (*vetoes)++; if (votesOut) *votesOut = votes; }
    }
    return latched;
}

int main()
{
    // 1. THE REGRESSION. The old policy latches on this walk; the new one must
    //    not -- with or without a readable vehicle id.
    check(OldPolicyLatches(WALK_TO_EDEA, WALK_N),
          "the fixture no longer reproduces the old defect -- it is not a fixture");
    check(RunFrames(WALK_TO_EDEA, WALK_N, true, nullptr, nullptr) == 0,
          "a walk to Edea's House still latches as a vehicle");
    check(RunFrames(WALK_TO_EDEA, WALK_N, false, nullptr, nullptr) == 0,
          "a walk latches even with the vehicle id unreadable");
    printf("walk to Edea's House: %d real frames, old policy latches, new policy does not\n",
           WALK_N);

    // 2. A REAL VEHICLE MUST STILL BE CAUGHT. A car holds its heading while the
    //    camera trails at an offset: the gap PERSISTS instead of closing, and
    //    the motion keeps siding with mh. With the id unreadable -- the only
    //    case this fallback exists for -- that must still latch.
    {
        VsFrame veh[VS_WINDOW];
        for (int i = 0; i < VS_WINDOW; i++) { veh[i].gap = 900; veh[i].mhSided = 1; }
        int votes = 0;
        check(RunFrames(veh, VS_WINDOW, false, nullptr, &votes) == 1,
              "a vehicle with an unreadable id is no longer detected");
        check(votes == VS_WINDOW, "the reported vote count is wrong");
        // ...and a wandering gap is still fine, so long as it is not closing.
        VsFrame wob[VS_WINDOW];
        for (int i = 0; i < VS_WINDOW; i++) {
            wob[i].gap = (i & 1) ? 1200 : 900;   // rises and falls, never a turn-in
            wob[i].mhSided = 1;
        }
        check(RunFrames(wob, VS_WINDOW, false, nullptr, nullptr) == 1,
              "a vehicle whose gap wobbles is no longer detected");
        printf("vehicle with an unreadable id: still detected, %d of %d votes\n",
               votes, VS_WINDOW);
    }

    // 3. THE ID IS AUTHORITATIVE. Same vehicle-shaped evidence, but the engine
    //    says on foot -- the verdict must be refused, announced ONCE, and the
    //    ring cleared so it does not re-fire every frame afterwards.
    {
        VsFrame veh[VS_WINDOW * 3];
        for (int i = 0; i < VS_WINDOW * 3; i++) { veh[i].gap = 900; veh[i].mhSided = 1; }
        int vetoes = 0;
        check(RunFrames(veh, VS_WINDOW * 3, true, &vetoes, nullptr) == 0,
              "the engine's on-foot id did not veto the verdict");
        check(vetoes == 1, "the veto was announced more than once");
        printf("engine id says on foot: verdict refused, announced once\n");
    }

    // 4. A TURN NEVER VOTES. The camera is slewed to a new bearing and the
    //    character rotates to catch it: the gap closes frame by frame. That is
    //    the exact sequence from the 21:18:55 waypoint, and only its opening
    //    frame -- the one with no predecessor to compare against -- may vote.
    {
        VehSigRing st; VehSigReset(st);
        const int turn[] = { 1977, 1465, 953, 441 };
        for (int i = 0; i < 4; i++) VehSigFeed(st, turn[i], true, false, nullptr);
        check(st.count == 2, "a rotation was counted as evidence");
        printf("turn transient: %d of 4 frames voted (the opening frame and the "
               "one that establishes the run)\n", st.count);
    }

    // 5. Frames where the two references agree carry no information at all.
    {
        VehSigRing st; VehSigReset(st);
        check(VehSigFeed(st, VS_GAP_MIN, true, false, nullptr) == VS_NOTHING,
              "a frame at the gap threshold voted");
        check(st.count == 0, "an agreeing frame reached the ring");
    }

    // 6. A reset really resets -- a car drive must not arm the next foot drive.
    {
        VehSigRing st; VehSigReset(st);
        for (int i = 0; i < VS_WINDOW; i++) VehSigFeed(st, 900, true, false, nullptr);
        VehSigReset(st);
        check(st.ring == 0 && st.count == 0 && st.prevGap == -1 && !st.vetoed,
              "VehSigReset left state behind");
    }

    printf("vehsig: %s (%d bad)\n", bad ? "FAILED" : "OK", bad);
    return bad ? 1 : 0;
}
