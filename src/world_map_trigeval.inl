// world_map_trigeval.inl -- v0.21.2 (#79)
//
// The world-map entry trigger, evaluated the way THE GAME evaluates it, and
// logged so a BAT can say which condition is failing.
//
// Included from world_map.cpp after world_map_segments.inl (needs the position
// readers) and before the drive files.
//
// ---------------------------------------------------------------------------
// WHY THIS EXISTS: THE DECODE THE MOD HAS BEEN USING IS WRONG
//
// Aaron stood at Edea's House -- his F11 screenshot shows Squall beside the
// orphanage lighthouse -- and nothing loaded, twice, from two different marker
// positions. The mod's own planner said the entrance was story-gated shut. It
// was not: *"Edea's House is now open at this point in the game. It becomes
// reachable at the start of disc 3, which is where I am at now."*
//
// He was right, and the opcode dispatch table in FF8_EN.exe says why. The
// interpreter at sub_546100 indexes a jump table at 0x546CAC through a byte map
// at 0x546D3C, and reading those two tables out of the exe gives a different
// opcode meaning than the research document this module was built on:
//
//   opcode   old reading             WHAT THE DISPATCH TABLE ACTUALLY SAYS
//   0xFF06   "program header,        slot 2 -> 0x00546192: SEGMENT TEST.
//            location/field id"      Calls sub_553910(X,Y) and compares the
//                                    operand to its return.
//   0xFF08   "region equals"         slot 35 -- the DESTINATION id, not a test
//   0xFF0F   unknown                 slot 5 -> 0x005463A7: X offset < operand
//   0xFF10   unknown                 slot 6 -> 0x00546406: Y offset < operand
//   0xFF11   unknown                 slot 7 -> 0x00546461: X offset > operand
//   0xFF12   unknown                 slot 8 -> 0x005464C0: Y offset > operand
//
// and sub_553910 is four lines of arithmetic:
//
//     row = ((Y + 0x48000) mod 0x30000) >> 13      ; 24 rows of 8192
//     col = ((X + 0x60000) &   0x3FFFF) >> 13      ; 32 cols of 8192
//     return row * 32 + col
//
// **So a program's "location id" is a SEGMENT INDEX -- an 8192-unit square of
// the world map -- and the four unknown opcodes are the X/Y bounds that narrow
// it.** The numbers this module has been calling field ids and regions are the
// other way round.
//
// The formula reproduces the mod's own segment arithmetic exactly: the catalog
// point (-23150,62853) prints as seg(13,19), and 19*32+13 = 621.
//
// ---------------------------------------------------------------------------
// WHAT THAT MAKES OF EDEA'S HOUSE
//
// Decoding wmsetus section 8 again with the corrected opcodes, program 34 reads:
//
//     SEGMENT== 652        -> x[-32768,-24576]  y[65536,73728]
//     STORY>=   900
//     UNK21     0
//     BEGIN
//       VEHICLE==128 (on foot)  AND  DEST 18
//       VEHICLE==49  (chocobo)  AND  DEST 18
//     END
//
// **Segment 652 is the orphanage.** Aaron's live story word reads 912, he is on
// foot, and his position at the lighthouse -- (-29585,70739) -- is inside that
// box. Every condition the old decode knows about passes.
//
// And program 32, the one this module reported as the story-locked Edea's House
// entrance, is `SEGMENT== 506` = x[81920,90112] y[24576,32768] -- the far east
// of the map, nowhere near Centra. It was never Edea's House at all.
//
// ---------------------------------------------------------------------------
// SO WHAT IS LEFT, AND WHY THIS IS AN INSTRUMENT AND NOT A FIX
//
// Two conditions the mod has never modelled, either of which would explain a
// silent door, and NEITHER of which can be settled from static analysis:
//
//   1. **UNK21** (0xFF21, handler 0x00546A11):
//          if ([0x2040A34] != 0) pass
//          else pass only if ((*(byte*)([0x20403A4] + 0x6D)) & 1) == operand
//      Program 34's operand is 0, so that bit must read 0.
//
//   2. **WHOSE POSITION IS TESTED.** Every position opcode has two paths:
//          if ([0x2040A30] != 0) use the cached block coords at
//                                [0x2040A24] / [0x2040A28], divided by 4
//          else                  use the live player position
//      If a vehicle still owns the position while Aaron is on foot, the segment
//      test runs against the GARDEN, which he parked 6.3 km away in segment 621
//      -- a different square -- and no amount of walking would ever open the
//      door.
//
// One BAT reading these values answers it. Guessing would be a third guess.

static const uint32_t WM_TRIG_POSFLAG   = 0x02040A30;  // != 0: a cached position owns the test
static const uint32_t WM_TRIG_BLOCKX    = 0x02040A24;  // cached block col (/4 = segment col)
static const uint32_t WM_TRIG_BLOCKY    = 0x02040A28;  // cached block row (/4 = segment row)
static const uint32_t WM_TRIG_UNK21_SKIP= 0x02040A34;  // != 0: UNK21 passes unconditionally
static const uint32_t WM_TRIG_UNK21_PTR = 0x020403A4;  // -> struct; bit 0 of +0x6D is the gate

// v0.21.3: **THE MASTER GATE, and it is the first thing sub_545EA0 does.**
//
//     0x00545EA3  mov  eax, [0x020409FC]
//     0x00545EAB  test byte ptr [eax + 0x0E], 8
//     0x00545EAF  jne  <walk the programs>
//     0x00545EB1  xor  eax, eax ; ret        <-- NOTHING IS EVALUATED
//
// With bit 3 clear the game never looks at a single entry program, anywhere on
// the world map. Every condition v0.21.2 instrumented passed at Edea's House --
// liveSeg 652, live position in use, story 912, UNK21 bit 0, on foot -- so this
// is the one candidate left, and it is one bit.
static const uint32_t WM_TRIG_STATE_PTR = 0x020409FC;  // -> world-map state; +0x0E bit 3 arms entry

// sub_553910, transcribed. Same arithmetic, same rounding for negatives.
static int WmSegmentIndex(int32_t x, int32_t y)
{
    int32_t yy = (y + 0x48000) % 0x30000;
    if (yy < 0) yy += 0x30000;
    int32_t xx = (x + 0x60000) & 0x3FFFF;
    return (int)((yy >> 13) * 32 + (xx >> 13));
}

static void WmSegmentBounds(int seg, int32_t* x0, int32_t* x1, int32_t* y0, int32_t* y1)
{
    const int row = seg / 32, col = seg % 32;
    int32_t sx = col * 8192 - 0x60000;  if (sx < -131072) sx += 262144;
    int32_t sy = row * 8192 - 0x48000;  if (sy <  -98304) sy += 196608;
    *x0 = sx; *x1 = sx + 8192; *y0 = sy; *y1 = sy + 8192;
}

static DWORD s_trigEvalLast = 0;

// Once a second on the world map, and on demand when a drive gives up looking
// for an entrance. Read-only: every address is fetched through the same guarded
// reader the rest of this module uses, and nothing is written.
static void LogTriggerEvaluation(const char* why)
{
    int32_t px = 0, py = 0, pz = 0;
    GetWorldMapPosition(&px, &py, &pz);
    if (px == 0 && py == 0) return;

    uint32_t posFlag = 0, blockX = 0, blockY = 0, skip21 = 0, ptr21 = 0, statePtr = 0;
    WmSafeReadBytes(WM_TRIG_POSFLAG,    &posFlag, 4);
    WmSafeReadBytes(WM_TRIG_BLOCKX,     &blockX,  4);
    WmSafeReadBytes(WM_TRIG_BLOCKY,     &blockY,  4);
    WmSafeReadBytes(WM_TRIG_UNK21_SKIP, &skip21,  4);
    WmSafeReadBytes(WM_TRIG_UNK21_PTR,  &ptr21,   4);
    WmSafeReadBytes(WM_TRIG_STATE_PTR,  &statePtr, 4);

    uint8_t gate = 0xFF;
    if (statePtr) {
        uint8_t b = 0;
        if (WmSafeReadBytes(statePtr + 0x0E, &b, 1)) gate = b;
    }

    uint8_t bit21 = 0xFF;
    if (ptr21) {
        uint8_t b = 0;
        if (WmSafeReadBytes(ptr21 + 0x6D, &b, 1)) bit21 = (uint8_t)(b & 1);
    }

    const int liveSeg = WmSegmentIndex(px, py);
    // The cached path the game would use instead when posFlag is non-zero.
    const int cachedSeg = (int)(((int32_t)blockY / 4) * 32 + ((int32_t)blockX / 4));

    int32_t x0, x1, y0, y1;
    WmSegmentBounds(liveSeg, &x0, &x1, &y0, &y1);

    Log::World("WorldMap: [TRIGEVAL] %s pos(%d,%d) liveSeg=%d box x[%d,%d] y[%d,%d] | "
               "posFlag=%u cachedBlock=(%d,%d) cachedSeg=%d %s | story=%u | "
               "UNK21 skip=%u bit=%u | vehId=%d | GATE [%08X+0x0E]=0x%02X bit3=%s",
               why, px, py, liveSeg, x0, x1, y0, y1,
               (unsigned)posFlag, (int)blockX, (int)blockY, cachedSeg,
               posFlag ? "<-- THE GAME IS USING THE CACHED ONE" : "(live position is used)",
               (unsigned)GetCurrentStoryFlag(),
               (unsigned)skip21, (unsigned)bit21, GetActiveVehicleId(),
               (unsigned)statePtr, (unsigned)gate,
               (gate == 0xFF) ? "UNREADABLE" : ((gate & 8) ? "SET (entry armed)"
                                                          : "CLEAR <-- NO PROGRAM IS EVALUATED"));
}

// v0.21.5: the program walk rides this same throttle. Declared here, defined in
// world_map_trigwalk.inl, which is included immediately after this file.
static void LogTriggerWalk(const char* why);

static void TriggerEvalTick()
{
    const DWORD now = GetTickCount();
    if (now - s_trigEvalLast < 1000) return;
    s_trigEvalLast = now;
    LogTriggerEvaluation("tick");
    LogTriggerWalk("tick");
}
