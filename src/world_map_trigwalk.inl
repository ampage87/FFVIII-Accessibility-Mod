// world_map_trigwalk.inl -- v0.21.5 (#79)
//
// The world-map field-entry interpreter, re-implemented with the opcode
// semantics read out of FF8_EN.exe's own dispatch tables, and run live against
// the same state the game reads. Its job is to answer one question with
// evidence instead of inference:
//
//     when the player stands somewhere and no field loads, WHICH CONDITION
//     was not satisfied?
//
// Included from world_map.cpp after world_map_trigeval.inl (it uses
// WmSegmentIndex) and before the drive files. Also compiled directly by
// tests/trigwalk_test.cpp.
//
// ---------------------------------------------------------------------------
// WHY THIS EXISTS
//
// Aaron stood beside the orphanage lighthouse -- his screenshot shows Squall at
// its wall -- and nothing loaded, twice, from two marker positions. Every
// condition v0.21.2/.3 could measure passed. Then the control experiment landed:
// **Chocobo Forest 7, the segment next door, opened on demand.** So the engine's
// walker runs, entries work, and Edea's House alone is refused.
//
// Reading single addresses from the mod's own thread has now been wrong twice --
// the v0.21.3 "master gate" turned out to be a per-frame pointer that churns
// every tick and read CLEAR while an entry demonstrably succeeded. So this stops
// sampling addresses in isolation and evaluates the WHOLE program set, which
// gives the one thing single reads cannot: **a known-good case and a known-bad
// case through identical code.** If this says program 35 fires at the forest and
// program 34 fires at the orphanage, then the mod's model is right and the
// divergence is in something the model does not represent. If it says 34 fails,
// it names the clause.
//
// ---------------------------------------------------------------------------
// THE OPCODES, from the dispatch tables at 0x546CAC / 0x546D3C
//
//   0xFF02 STORY >=            0xFF03 STORY <
//   0xFF06 SEGMENT ==          sub_553910(X,Y) -> row*32+col, 8192-unit squares
//   0xFF07 BLOCK ==            the finer 128x96 grid; unused by these programs
//   0xFF08 DESTINATION         what loads -- NOT a test
//   0xFF09 VEHICLE ==          128/132 on foot, 48 Garden, 49 chocobo, 50 Ragnarok
//   0xFF0F Xoff <              offsets are X & 0x1FFF within the segment
//   0xFF10 Yoff <
//   0xFF11 Xoff >
//   0xFF12 Yoff >
//   0xFF21 UNK21               pass if [0x2040A34] != 0, else require
//                              ((byte[[0x20403A4]+0x6D]) & 1) == operand
//
// The tables below are generated from wmsetus.obj section 8 (offset 2928, 2652
// bytes) with those semantics. -1 means "the opcode is absent", which is always
// "no constraint".

struct TrigClause
{
    int16_t veh;                  // 0xFF09 operand, -1 = any
    uint16_t storyGte, storyLt;   // 0 = unbounded on that side
    int32_t xLt, yLt, xGt, yGt;   // -1 = absent
    int16_t unk20;                // -1 = absent
    int16_t dest;                 // 0xFF08
};

struct TrigProgram
{
    int16_t  idx;                 // program number, for the log
    int16_t  seg;                 // 0xFF06
    uint16_t storyGte, storyLt;   // top-level 0xFF02 / 0xFF03
    int16_t  unk21;               // -1 = absent
    int16_t  first, count;        // slice of TRIG_CLAUSES
};

static const TrigClause TRIG_CLAUSES[] = {
    { 128,     0,     0,    -1,    -1,    -1,    -1,  -1,  20 },   // prog 0
    {  49,     0,     0,    -1,    -1,    -1,    -1,  -1,  20 },   // prog 0
    { 128,     0,     0,    -1,    -1,    -1,    -1,  -1,  33 },   // prog 1
    {  49,     0,     0,    -1,    -1,    -1,    -1,  -1,  33 },   // prog 1
    { 128,     0,     0,    -1,    -1,    -1,    -1,  -1,  34 },   // prog 2
    {  49,     0,     0,    -1,    -1,    -1,    -1,  -1,  34 },   // prog 2
    { 128,     0,     0,    -1,    -1,    -1,    -1,  -1,  19 },   // prog 3
    {  49,     0,     0,    -1,    -1,    -1,    -1,  -1,  19 },   // prog 3
    { 128,     0,     0,    -1,    -1,    -1,  4096,  -1,  19 },   // prog 4
    {  49,     0,     0,    -1,    -1,    -1,  4096,  -1,  19 },   // prog 4
    { 128,     0,     0,    -1,  4095,    -1,    -1,  -1,  35 },   // prog 4
    {  49,     0,     0,    -1,  4095,    -1,    -1,  -1,  35 },   // prog 4
    { 128,     0,     0,    -1,    -1,    -1,    -1,  -1,  36 },   // prog 5
    {  49,     0,     0,    -1,    -1,    -1,    -1,  -1,  36 },   // prog 5
    { 128,     0,     0,    -1,    -1,    -1,    -1,  -1,   9 },   // prog 6
    { 128,     0,     0,    -1,    -1,    -1,    -1,  -1,   3 },   // prog 7
    { 132,     0,     0,    -1,    -1,    -1,    -1,  -1,   3 },   // prog 7
    { 128,     0,     0,    -1,    -1,    -1,    -1,  -1,   8 },   // prog 8
    { 132,     0,     0,    -1,    -1,    -1,    -1,  -1,   8 },   // prog 8
    { 128,     0,   490,  4996,  6532,  3196,  4732,  -1,   6 },   // prog 9
    { 128,     0,  3900,    -1,    -1,    -1,  7000,  -1,   7 },   // prog 9
    { 128,   290,   315,    -1,    -1,    -1,    -1,  -1,   5 },   // prog 10
    { 128,     0,     0,    -1,    -1,    -1,    -1,  -1,   1 },   // prog 11
    { 132,     0,     0,    -1,    -1,    -1,    -1,  -1,   1 },   // prog 11
    { 128,     0,     0,    -1,    -1,    -1,    -1,  -1,   0 },   // prog 12
    { 128,     0,     0,    -1,    -1,  5536,    -1,  -1,   2 },   // prog 13
    { 128,     0,   570,  2048,    -1,    -1,    -1,  -1,   0 },   // prog 13
    { 128,     0,     0,  2559,    -1,    -1,    -1,  -1,  22 },   // prog 14
    {  49,     0,     0,  2559,    -1,    -1,    -1,  -1,  22 },   // prog 14
    { 128,     0,     0,    -1,    -1,  2560,    -1,  -1,  23 },   // prog 14
    {  49,     0,     0,    -1,    -1,  2560,    -1,  -1,  23 },   // prog 14
    { 128,     0,     0,    -1,    -1,    -1,  1432,  -1,  11 },   // prog 15
    { 132,     0,     0,    -1,    -1,    -1,  1432,  -1,  11 },   // prog 15
    { 128,     0,     0,    -1,    -1,    -1,    -1,  -1,  10 },   // prog 16
    { 132,     0,     0,    -1,    -1,    -1,    -1,  -1,  10 },   // prog 16
    { 128,     0,     0,    -1,    -1,    -1,    -1,  -1,   4 },   // prog 17
    {  -1,     0,  3900,    -1,    -1,    -1,    -1,  64,  13 },   // prog 19
    {  -1,     0,  3900,    -1,    -1,    -1,    -1,  -1,  12 },   // prog 20
    { 128,     0,  3000,    -1,    -1,    -1,    -1,  -1,  24 },   // prog 21
    { 128,  3000,  3900,    -1,    -1,    -1,    -1,  -1,  46 },   // prog 21
    { 128,     0,     0,    -1,    -1,    -1,    -1,  -1,  25 },   // prog 22
    { 128,     0,  3000,    -1,    -1,    -1,    -1,  -1,  28 },   // prog 23
    { 128,  3000,  5000,    -1,    -1,    -1,    -1,  -1,  57 },   // prog 23
    { 128,     0,     0,    -1,    -1,  6144,    -1,  -1,  14 },   // prog 24
    { 128,     0,     0,  6143,    -1,    -1,    -1,  -1,  15 },   // prog 24
    {  50,     0,  3000,  6655,    -1,    -1,    -1,  64,  27 },   // prog 25
    {  50,  3000,  3900,  6655,    -1,    -1,    -1,  64,  68 },   // prog 25
    {  49,  3900,     0,  6656,    -1,    -1,    -1,  -1,  68 },   // prog 25
    { 128,     0,  3900,    -1,    -1,  6656,    -1,  -1,  26 },   // prog 25
    { 132,     0,  3900,    -1,    -1,  6656,    -1,  -1,  26 },   // prog 25
    { 128,     0,  3900,    -1,    -1,    -1,    -1,  -1,  26 },   // prog 26
    { 132,     0,  3900,    -1,    -1,    -1,    -1,  -1,  26 },   // prog 26
    { 128,     0,  3900,    -1,    -1,    -1,    -1,  -1,  26 },   // prog 27
    { 132,     0,  3900,    -1,    -1,    -1,    -1,  -1,  26 },   // prog 27
    { 128,     0,  3900,    -1,    -1,    -1,    -1,  -1,  26 },   // prog 28
    { 132,     0,  3900,    -1,    -1,    -1,    -1,  -1,  26 },   // prog 28
    { 128,     0,  3000,    -1,    -1,    -1,    -1,  -1,  30 },   // prog 29
    { 128,  3000,     0,    -1,    -1,    -1,    -1,  -1,  69 },   // prog 29
    { 128,     0,  3000,    -1,    -1,    -1,    -1,  -1,  29 },   // prog 30
    { 128,     0,     0,    -1,    -1,    -1,    -1,  -1,  47 },   // prog 30
    { 128,     0,     0,    -1,    -1,    -1,    -1,  -1,  39 },   // prog 31
    {  49,     0,     0,    -1,    -1,    -1,    -1,  -1,  39 },   // prog 31
    { 128,     0,  2500,    -1,    -1,    -1,    -1,  -1,  31 },   // prog 32
    { 128,  2500,  3000,    -1,    -1,    -1,    -1,  -1,  48 },   // prog 32
    { 128,  3000,  3900,    -1,    -1,    -1,    -1,  -1,  49 },   // prog 32
    { 128,  3900,  5000,    -1,    -1,    -1,    -1,  -1,  31 },   // prog 32
    { 128,     0,     0,    -1,    -1,    -1,    -1,  -1,  16 },   // prog 33
    {  49,     0,     0,    -1,    -1,    -1,    -1,  -1,  16 },   // prog 33
    { 128,     0,     0,    -1,    -1,    -1,    -1,  -1,  18 },   // prog 34  <- EDEA'S HOUSE
    {  49,     0,     0,    -1,    -1,    -1,    -1,  -1,  18 },   // prog 34
    { 128,     0,     0,    -1,    -1,    -1,    -1,  -1,  38 },   // prog 35  <- CHOCOBO FOREST 7
    {  49,     0,     0,    -1,    -1,    -1,    -1,  -1,  38 },   // prog 35
    { 128,     0,     0,    -1,    -1,    -1,    -1,  -1,  37 },   // prog 36
    {  49,     0,     0,    -1,    -1,    -1,    -1,  -1,  37 },   // prog 36
    {  50,     0,     0,    -1,    -1,    -1,    -1,  64,  21 },   // prog 37
};

static const TrigProgram TRIG_PROGRAMS[] = {
    {   0,  49,   750,     0,  -1,   0, 2 },
    {   1,  81,     0,     0,  -1,   2, 2 },
    {   2, 145,     0,     0,  -1,   4, 2 },
    {   3, 149,   750,     0,  -1,   6, 2 },
    {   4, 150,   750,     0,  -1,   8, 4 },
    {   5, 219,     0,     0,  -1,  12, 2 },
    {   6, 234,     0,     0,  -1,  14, 1 },
    {   7, 238,    36,     0,  -1,  15, 2 },
    {   8, 264,   333,     0,  -1,  17, 2 },
    {   9, 267,   290,     0,  -1,  19, 2 },
    {  10, 268,     0,     0,  -1,  21, 1 },
    {  11, 273,     0,     0,  -1,  22, 2 },
    {  12, 274,     0,   570,  -1,  24, 1 },
    {  13, 275,     0,     0,  -1,  25, 2 },
    {  14, 279,     0,     0,  -1,  27, 4 },
    {  15, 327,   350,   490,  -1,  31, 2 },
    {  16, 361,   350,     0,  -1,  33, 2 },
    {  17, 365,   205,     0,  -1,  35, 1 },
    {  18, 370,  3900,     0,  -1,  36, 0 },
    {  19, 370,   636,     0,  -1,  36, 1 },
    {  20, 370,   636,     0,   0,  37, 1 },
    {  21, 373,  1600,     0,  -1,  38, 2 },
    {  22, 374,     0,     0,  -1,  40, 1 },
    {  23, 378,  1750,     0,  -1,  41, 2 },
    {  24, 393,   750,     0,  -1,  43, 2 },
    {  25, 406,  1750,     0,  -1,  45, 5 },
    {  26, 407,  1750,     0,  -1,  50, 2 },
    {  27, 438,  1750,     0,  -1,  52, 2 },
    {  28, 439,  1750,     0,  -1,  54, 2 },
    {  29, 441,  1750,     0,  -1,  56, 2 },
    {  30, 443,  1750,     0,  -1,  58, 2 },
    {  31, 466,     0,     0,  -1,  60, 2 },
    {  32, 506,  1750,     0,  -1,  62, 4 },
    {  33, 592,     0,     0,  -1,  66, 2 },
    {  34, 652,   900,     0,   0,  68, 2 },
    {  35, 653,     0,     0,  -1,  70, 2 },
    {  36, 693,     0,     0,  -1,  72, 2 },
    {  37, 705,     0,     0,  -1,  74, 1 },
};

static const int TRIG_PROGRAM_N = (int)(sizeof(TRIG_PROGRAMS) / sizeof(TRIG_PROGRAMS[0]));

// The mod's own story-window predicate, kept identical in shape: gte == 0 means
// no lower bound, lt == 0 means no upper bound.
static bool TwStoryOk(uint16_t gte, uint16_t lt, uint16_t story)
{
    if (gte != 0 && story <  gte) return false;
    if (lt  != 0 && story >= lt)  return false;
    return true;
}

// The vehicle sub-dispatch at 0x00546254 fans out on (operand - 0x21); the
// on-foot handler at 0x00546282 accepts an engine id of 0..9 or exactly 0x80.
static bool TwVehicleOk(int16_t operand, int vehId)
{
    if (operand < 0) return true;                         // no vehicle clause
    if (operand == 128 || operand == 132)                 // on foot
        return (vehId >= 0 && vehId <= 9) || vehId == 128;
    return vehId == operand;                              // 48 Garden, 49 chocobo, 50 Ragnarok
}

enum TwVerdict
{
    TW_MATCH = 0,          // this program would fire, and `dest` says what loads
    TW_SEGMENT,            // the player is not in this program's square
    TW_STORY,              // top-level story window
    TW_UNK21,              // the UNK21 bit
    TW_NO_CLAUSE           // in the square, past the top-level gates, but no
                           // clause matched (vehicle / story / coordinate bounds)
};

struct TwInput
{
    int32_t  x, y;
    uint16_t story;
    int      vehId;
    int      unk21Bit;     // (byte[[0x20403A4]+0x6D]) & 1, or -1 if unreadable
    bool     unk21Skip;    // [0x2040A34] != 0
};

// Evaluate one program. `dest` receives the destination on a match.
static TwVerdict TwEvaluate(const TrigProgram& p, const TwInput& in, int* dest)
{
    if (WmSegmentIndex(in.x, in.y) != p.seg) return TW_SEGMENT;
    if (!TwStoryOk(p.storyGte, p.storyLt, in.story)) return TW_STORY;
    if (p.unk21 >= 0 && !in.unk21Skip) {
        // -1 (unreadable) cannot refuse: an unknown does not get to fail a gate.
        if (in.unk21Bit >= 0 && in.unk21Bit != p.unk21) return TW_UNK21;
    }
    const int32_t xo = in.x & 0x1FFF, yo = in.y & 0x1FFF;
    for (int i = 0; i < p.count; i++) {
        const TrigClause& c = TRIG_CLAUSES[p.first + i];
        if (!TwVehicleOk(c.veh, in.vehId))                 continue;
        if (!TwStoryOk(c.storyGte, c.storyLt, in.story))   continue;
        if (c.xLt >= 0 && !(xo <  c.xLt))                  continue;
        if (c.yLt >= 0 && !(yo <  c.yLt))                  continue;
        if (c.xGt >= 0 && !(xo >  c.xGt))                  continue;
        if (c.yGt >= 0 && !(yo >  c.yGt))                  continue;
        if (dest) *dest = c.dest;
        return TW_MATCH;
    }
    return TW_NO_CLAUSE;
}

static const char* TwVerdictName(TwVerdict v)
{
    switch (v) {
        case TW_MATCH:     return "MATCH";
        case TW_SEGMENT:   return "not in this segment";
        case TW_STORY:     return "story window";
        case TW_UNK21:     return "UNK21 bit";
        default:           return "no clause matched (vehicle/story/coords)";
    }
}

// ---------------------------------------------------------------------------
// The live report. Same inputs the game reads, evaluated once a second and
// again whenever a drive gives up looking for an entrance.
static void LogTriggerWalk(const char* why)
{
    int32_t px = 0, py = 0, pz = 0;
    GetWorldMapPosition(&px, &py, &pz);
    if (px == 0 && py == 0) return;

    uint32_t skip21 = 0, ptr21 = 0;
    WmSafeReadBytes(WM_TRIG_UNK21_SKIP, &skip21, 4);
    WmSafeReadBytes(WM_TRIG_UNK21_PTR,  &ptr21,  4);
    int bit21 = -1;
    if (ptr21) { uint8_t b = 0; if (WmSafeReadBytes(ptr21 + 0x6D, &b, 1)) bit21 = b & 1; }

    TwInput in;
    in.x = px; in.y = py;
    in.story = GetCurrentStoryFlag();
    in.vehId = GetActiveVehicleId();
    in.unk21Bit = bit21;
    in.unk21Skip = (skip21 != 0);

    const int seg = WmSegmentIndex(px, py);
    int reported = 0;
    for (int i = 0; i < TRIG_PROGRAM_N; i++) {
        if (TRIG_PROGRAMS[i].seg != seg) continue;      // only this square
        int dest = -1;
        const TwVerdict v = TwEvaluate(TRIG_PROGRAMS[i], in, &dest);
        reported++;
        if (v == TW_MATCH) {
            // v0.21.6: this used to end "...the refusal is elsewhere", which read
            // as a finding and was really a blind spot -- see the [ENTRYPATCH]
            // line below, which is the half of the answer this loop cannot see.
            Log::World("WorldMap: [TRIGWALK] %s seg=%d prog %d -> segment/story/vehicle "
                       "gates PASS, destination %d (story=%u veh=%d unk21 bit=%d skip=%u). "
                       "The polygon gate decides the rest -- see [ENTRYPATCH].",
                       why, seg, TRIG_PROGRAMS[i].idx, dest,
                       (unsigned)in.story, in.vehId, bit21, (unsigned)skip21);
        } else {
            Log::World("WorldMap: [TRIGWALK] %s seg=%d prog %d -> refused: %s "
                       "(story=%u needs [%u,%u) | veh=%d | unk21 bit=%d needs %d skip=%u)",
                       why, seg, TRIG_PROGRAMS[i].idx, TwVerdictName(v),
                       (unsigned)in.story, (unsigned)TRIG_PROGRAMS[i].storyGte,
                       (unsigned)TRIG_PROGRAMS[i].storyLt, in.vehId,
                       bit21, (int)TRIG_PROGRAMS[i].unk21, (unsigned)skip21);
        }
    }
    if (reported == 0) {
        Log::World("WorldMap: [TRIGWALK] %s seg=%d -- no entry program covers this square",
                   why, seg);
    }

    // ------------------------------------------------------------------------
    // v0.21.6: THE GATE THIS INTERPRETER WAS MISSING.
    //
    // Everything above resolves to an 8192-unit SEGMENT, which is why the
    // 2026-08-16 log printed "MATCH, destination 18" at all twenty-three of
    // Aaron's positions across 8 km of Centra while nothing loaded. A program
    // matching is necessary and nowhere near sufficient: sub_545EA0's FIRST act
    // is `test byte [eax+0x0E], 8` on the polygon under the player, and only the
    // hand-painted entry polygons at each gate mouth carry that bit. The
    // s_entryAims bboxes in world_map_trigger_data.inl are those patches,
    // restricted to the ones that are also foot-walkable. Report which one the
    // player is standing in, so a MATCH is never again read as "the door should
    // have opened and did not".
    //
    // The bbox is the patch's bounding rectangle, so this slightly over-reports
    // at a ragged patch edge: it answers "are you on the doorstep", not "are you
    // on this exact triangle". That is the resolution the question needs, and
    // the aim points carry margin precisely so the difference never bites.
    // ------------------------------------------------------------------------
    {
        int inIdx = -1;
        for (int i = 0; i < ENTRY_AIM_COUNT; i++) {
            const EntryAimInfo& e = s_entryAims[i];
            if (px >= e.x0 && px <= e.x1 && py >= e.y0 && py <= e.y1) { inIdx = i; break; }
        }
        if (inIdx >= 0) {
            Log::World("WorldMap: [ENTRYPATCH] %s standing INSIDE %s's entry-polygon patch "
                       "at (%d,%d) -- this is the ground that opens the door",
                       why, s_entryAims[inIdx].name, px, py);
        } else {
            // Name the nearest known patch and the miss distance. When a drive
            // reports "arrived" and no field loads, this line is the answer.
            int best = -1; double bestD = 0;
            for (int i = 0; i < ENTRY_AIM_COUNT; i++) {
                const EntryAimInfo& e = s_entryAims[i];
                const double d = CalculateWrappedDistance(px, py, e.aimX, e.aimY);
                if (best < 0 || d < bestD) { best = i; bestD = d; }
            }
            if (best >= 0 && bestD <= 4000.0) {
                Log::World("WorldMap: [ENTRYPATCH] %s NOT on an entry polygon at (%d,%d) -- "
                           "nearest patch is %s, aim (%d,%d), %.0f units away",
                           why, px, py, s_entryAims[best].name,
                           s_entryAims[best].aimX, s_entryAims[best].aimY, bestD);
            }
        }
    }
}
