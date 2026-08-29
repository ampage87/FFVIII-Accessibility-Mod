// disc3_models_compile -- the three disc-3 blockers' pure models, checked
// against the bytecode they were read out of.
//
// WHY THIS EXISTS
// ---------------
// Aaron cannot reach any of these scenes for hours, so there is no BAT to fall
// back on and every number here has to be right on the first build. Each
// assertion below names the field, entity::method and dword it came from, and
// the fixtures are the game's own script words rather than my transcription of
// them -- a fixture addressed through the constant under test agrees with it by
// construction, which is the lesson v0.51.0 cost two BATs to relearn.
#include <cstdio>
#include <cstring>
#include <cstdint>
#include <cstddef>
#include <string>
#ifndef _WIN32
#define _stricmp strcasecmp
#include <strings.h>
#endif

#include "esthar_pandora_model.inl"
#include "space_rescue_model.inl"
#include "propagator_model.inl"

static int bad = 0;
static void check(bool ok, const char* what)
{
    if (!ok) { std::printf("  BAD: %s\n", what); bad++; }
}
static void checkStr(const char* got, const char* want, const char* what)
{
    if (strcmp(got, want) != 0) {
        std::printf("  BAD: %s\n       got  \"%s\"\n       want \"%s\"\n", what, got, want);
        bad++;
    }
}

// ---------------------------------------------------------------- JSM decode
static void dec(uint32_t w, int* op, int* par, bool* hasPar)
{
    if ((w & 0xFF000000u) == 0) { *op = (int)(w & 0xFFFFFF); *par = 0; *hasPar = false; return; }
    *op = (int)(w >> 24);
    int p = (int)(w & 0xFFFFFF);
    if (p & 0x800000) p -= 0x1000000;
    *par = p; *hasPar = true;
}
enum { OP_OPER = 0x01, OP_JPF = 0x03, OP_PSHN_L = 0x07, OP_PSHM_B = 0x0A,
       OP_POPM_B = 0x0B, OP_PSHM_W = 0x0C, OP_PUSHL_L = 0x12, OP_POPL_L = 0x0F,
       OP_EQ = 6, OP_GT = 7, OP_LT = 9, OP_NEG = 5, OP_BTN_HELD = 0x6D,
       OP_AND = 12, OP_OR = 13, OP_NOT = 15, OP_JMP = 0x02, OP_LE = 10,
       OP_GETTIMER = 0xA4, OP_PSHL = 0x08, OP_MAPJUMP = 0x29, OP_MAPJUMP3 = 0x2A,
       OP_MAPJUMPO = 0x5C };

// ===========================================================================
// THE ESTHAR BOARDING GATES, ALL EIGHT, OUT OF THE SHIPPED SCRIPTS (#110)
// ===========================================================================
//
// v0.57.0. The 2026-08-22 BAT stood on eccway21 with the window open and
// nothing happened. The reason is in these bytes: six of the eight boarding
// sites are `Linejump*::touch` -- the engine's walk-through event -- and the
// v0.55.0 model called them lifters and told the player to WAIT.
//
// Each fixture below is a whole method, extracted from its .jsm, containing:
//     PSHM_W var[672] ; PSHN_L 19 ; OPER EQ        the run-is-live gate
//   [ PSHM_B var[678] ; PSHN_L b  ; OPER AND ]     the already-used bit
//   [ PSHM_B var[1029]; PSHN_L 0  ; OPER EQ  ]     the busy gate (AUTO only)
//     GETTIMER ; PSHL 0 ; PSHN_L lo ; OPER GT      t > lo
//     PSHL 0   ; PSHN_L hi ; OPER LE|LT            t <= hi   (LT at ecmall1)
//     PSHN_L <417|418|419> ... MAPJUMP*            board
//
// The probe decodes each one and checks the MODEL against what it finds, so a
// window bound, a used-bit or a miss destination cannot drift from the game.
//
// A DECODE NOTE. .jsm entry offsets carry a 0x8000 flag the engine masks off
// (0x0052BF98 / 0x0052C0A4, `and eax,0x7fff`). Read unmasked, eccway21's
// entries[210..217] are 36247.. against a 6756-dword code array, seven methods
// collapse into their predecessor, and this trigger reads as `Edea::afkantei2`
// rather than `Linejump1::touch`. That is how v0.55.0 came to describe a
// walk-through line as a lifter. The offline loader masks now.

// eciway11.jsm dwords 631..659 -- JumpTimer::defalut, the CP1 gate
static const int      ECIWAY11_417_BASE = 631;
static const uint32_t ECIWAY11_417[] = {
    0x05000036, 0x0C0002A0, 0x07000013, 0x01000006, 0x03000017, 0x0A000405,
    0x07000000, 0x01000006, 0x03000013, 0x0A0002A6, 0x07000001, 0x0100000C,
    0x03000002, 0x0200000D, 0x000000A4, 0x08000000, 0x070002D0, 0x01000007,
    0x03000008, 0x08000000, 0x07000384, 0x0100000A, 0x03000004, 0x070001A1,
    0x07000000, 0x0000005C, 0x02FFFFEB, 0x1C000000, 0x06000008,
};

// ecoway3.jsm dwords 1775..1801 -- TimerJump::defalut, the CP3 gate
static const int      ECOWAY3_419_BASE = 1775;
static const uint32_t ECOWAY3_419[] = {
    0x0500009A, 0x0C0002A0, 0x07000013, 0x01000006, 0x03000015, 0x0A000405,
    0x07000000, 0x01000006, 0x03000011, 0x000000A4, 0x08000000, 0x07000000,
    0x01000007, 0x0300000B, 0x08000000, 0x070000B4, 0x0100000A, 0x03000007,
    0x070001A3, 0x07000064, 0x07000086, 0x07000000, 0x07000080, 0x2A000024,
    0x02FFFFED, 0x1C000000, 0x06000008,
};

// eccway11.jsm dwords 4797..4849 -- Linejump1::touch, the CP1 gate
static const int      ECCWAY11_417_BASE = 4797;
static const uint32_t ECCWAY11_417[] = {
    0x050000EA, 0x0000004E, 0x0A0002A6, 0x07000001, 0x0100000C, 0x03000007,
    0x070001A9, 0x07000AC1, 0x07FFFF7B, 0x070004AE, 0x070000C0, 0x2A000055,
    0x0C0002A0, 0x07000013, 0x01000006, 0x0300001F, 0x000000A4, 0x08000000,
    0x070002D0, 0x01000007, 0x03000013, 0x08000000, 0x07000387, 0x0100000A,
    0x03000008, 0x070001A1, 0x070003F0, 0x07FFFF86, 0x070004B1, 0x070000C0,
    0x2A00000A, 0x02000007, 0x070001A9, 0x07000AC1, 0x07FFFF7B, 0x070004AE,
    0x070000C0, 0x2A000055, 0x02000007, 0x070001A9, 0x07000AC1, 0x07FFFF7B,
    0x070004AE, 0x070000C0, 0x2A000055, 0x02000007, 0x070001A9, 0x07000AC1,
    0x07FFFF7B, 0x070004AE, 0x070000C0, 0x2A000055, 0x06000008,
};

// eccway21.jsm dwords 3509..3562 -- Linejump1::touch, the CP1 gate
static const int      ECCWAY21_417_BASE = 3509;
static const uint32_t ECCWAY21_417[] = {
    0x050000D7, 0x0000004E, 0x0C0002A0, 0x07000013, 0x01000006, 0x0300002A,
    0x0A0002A6, 0x07000001, 0x0100000C, 0x03000008, 0x070001A9, 0x07FFF94C,
    0x07FFFF15, 0x070004AE, 0x07000040, 0x2A000009, 0x0200001E, 0x000000A4,
    0x08000000, 0x070002D0, 0x01000007, 0x03000013, 0x08000000, 0x07000387,
    0x0100000A, 0x03000008, 0x070001A1, 0x070000A2, 0x07FFFFC0, 0x070004AE,
    0x07000000, 0x2A000007, 0x02000007, 0x070001A9, 0x07FFF94C, 0x07FFFF15,
    0x070004AE, 0x07000040, 0x2A000009, 0x02000007, 0x070001A9, 0x07FFF94C,
    0x07FFFF15, 0x070004AE, 0x07000040, 0x2A000009, 0x02000007, 0x070001A9,
    0x07FFF94C, 0x07FFFF15, 0x070004AE, 0x07000040, 0x2A000009, 0x06000008,
};

// eccway12.jsm dwords 1522..1574 -- Linejump1::touch, the CP2 gate
static const int      ECCWAY12_418_BASE = 1522;
static const uint32_t ECCWAY12_418[] = {
    0x0500009E, 0x0000004E, 0x0A0002A6, 0x07000002, 0x0100000C, 0x03000007,
    0x0700019E, 0x07FFED96, 0x07FFEFE3, 0x07000000, 0x07000040, 0x2A00006A,
    0x0C0002A0, 0x07000013, 0x01000006, 0x0300001F, 0x000000A4, 0x08000000,
    0x07000258, 0x0100000A, 0x03000013, 0x08000000, 0x0700012C, 0x01000007,
    0x03000008, 0x070001A2, 0x07FFFF1D, 0x0700001C, 0x07000000, 0x07000000,
    0x2A000046, 0x02000007, 0x0700019E, 0x07FFED96, 0x07FFEFE3, 0x07000000,
    0x07000040, 0x2A00006A, 0x02000007, 0x0700019E, 0x07FFED96, 0x07FFEFE3,
    0x07000000, 0x07000040, 0x2A00006A, 0x02000007, 0x0700019E, 0x07FFED96,
    0x07FFEFE3, 0x07000000, 0x07000040, 0x2A00006A, 0x06000008,
};

// eccway41.jsm dwords 1878..1930 -- Linejump1::touch, the CP2 gate
static const int      ECCWAY41_418_BASE = 1878;
static const uint32_t ECCWAY41_418[] = {
    0x050000AA, 0x0000004E, 0x0A0002A6, 0x07000002, 0x0100000C, 0x03000007,
    0x07000194, 0x07FFFDC8, 0x07FFFF64, 0x07FFFC49, 0x070000DE, 0x2A000010,
    0x0C0002A0, 0x07000013, 0x01000006, 0x0300001F, 0x000000A4, 0x08000000,
    0x07000258, 0x0100000A, 0x03000013, 0x08000000, 0x0700012C, 0x01000007,
    0x03000008, 0x070001A2, 0x07000278, 0x07FFFFE9, 0x07000000, 0x070000C0,
    0x2A00004B, 0x02000007, 0x07000194, 0x07FFFDC8, 0x07FFFF64, 0x07FFFC49,
    0x070000DE, 0x2A000010, 0x02000007, 0x07000194, 0x07FFFDC8, 0x07FFFF64,
    0x07FFFC49, 0x070000DE, 0x2A000010, 0x02000007, 0x07000194, 0x07FFFDC8,
    0x07FFFF64, 0x07FFFC49, 0x070000DE, 0x2A000010, 0x06000008,
};

// eccway41.jsm dwords 1971..2009 -- Linejump2::touch, the CP3 gate
static const int      ECCWAY41_419_BASE = 1971;
static const uint32_t ECCWAY41_419[] = {
    0x050000B2, 0x0C0002A0, 0x07000013, 0x01000006, 0x0300001C, 0x000000A4,
    0x08000000, 0x070000B4, 0x0100000A, 0x03000010, 0x08000000, 0x07000000,
    0x01000007, 0x03000005, 0x070001A3, 0x07000000, 0x0000005C, 0x02000007,
    0x070001CA, 0x0700001A, 0x070009BB, 0x07000000, 0x07000000, 0x2A000184,
    0x02000007, 0x070001CA, 0x0700001A, 0x070009BB, 0x07000000, 0x07000000,
    0x2A000184, 0x02000007, 0x070001CA, 0x0700001A, 0x070009BB, 0x07000000,
    0x07000000, 0x2A000184, 0x06000008,
};

// ecmall1.jsm dwords 4374..4412 -- Linejump1::touch, the CP3 gate
static const int      ECMALL1_419_BASE = 4374;
static const uint32_t ECMALL1_419[] = {
    0x050000F7, 0x0C0002A0, 0x07000013, 0x01000006, 0x0300001C, 0x000000A4,
    0x08000000, 0x07000000, 0x01000007, 0x03000010, 0x08000000, 0x070000B4,
    0x01000009, 0x03000005, 0x070001A3, 0x07000000, 0x0000005C, 0x02000007,
    0x070001CA, 0x07000198, 0x07FFF5F9, 0x07000000, 0x0700008A, 0x2A0000F0,
    0x02000007, 0x070001CA, 0x07000198, 0x07FFF5F9, 0x07000000, 0x0700008A,
    0x2A0000F0, 0x02000007, 0x070001CA, 0x07000198, 0x07FFF5F9, 0x07000000,
    0x0700008A, 0x2A0000F0, 0x06000008,
};

struct EpFix { const char* field; int dest; const char* kind; const uint32_t* code; int n; int base; };
static const EpFix EP_FIX[] = {
    { "eciway11", 417, "AUTO", ECIWAY11_417, (int)(sizeof(ECIWAY11_417)/sizeof(uint32_t)), ECIWAY11_417_BASE },
    { "ecoway3", 419, "AUTO", ECOWAY3_419, (int)(sizeof(ECOWAY3_419)/sizeof(uint32_t)), ECOWAY3_419_BASE },
    { "eccway11", 417, "LINE", ECCWAY11_417, (int)(sizeof(ECCWAY11_417)/sizeof(uint32_t)), ECCWAY11_417_BASE },
    { "eccway21", 417, "LINE", ECCWAY21_417, (int)(sizeof(ECCWAY21_417)/sizeof(uint32_t)), ECCWAY21_417_BASE },
    { "eccway12", 418, "LINE", ECCWAY12_418, (int)(sizeof(ECCWAY12_418)/sizeof(uint32_t)), ECCWAY12_418_BASE },
    { "eccway41", 418, "LINE", ECCWAY41_418, (int)(sizeof(ECCWAY41_418)/sizeof(uint32_t)), ECCWAY41_418_BASE },
    { "eccway41", 419, "LINE", ECCWAY41_419, (int)(sizeof(ECCWAY41_419)/sizeof(uint32_t)), ECCWAY41_419_BASE },
    { "ecmall1", 419, "LINE", ECMALL1_419, (int)(sizeof(ECMALL1_419)/sizeof(uint32_t)), ECMALL1_419_BASE },
};
static const int EP_FIX_N = (int)(sizeof(EP_FIX)/sizeof(EP_FIX[0]));

// ---- ssspace3 :: rinoa::default, dwords 225..244 --------------------------
// The verdict. Taken exactly once, after the approach animation.
static const uint32_t SSSPACE3_VERDICT[] = {
    0x12000410, 0x070000B4, 0x01000005, 0x01000007, 0x03000010, 0x12000410,
    0x070000B4, 0x01000009, 0x0300000C, 0x12000414, 0x070000B4, 0x01000005,
    0x01000007, 0x03000007, 0x12000414, 0x070000B4, 0x01000009, 0x03000003,
    0x07000001, 0x0B00040B,
};

// ---- ssspace3 :: director1::default, dwords 1147..1321 -------------------------
// THE DISPATCH TREE, and the reason the four mask constants are not a guess.
//
// v0.55.0: the earlier version of this probe asserted
// `SrMaskForX(4000) == SR_MASK_RIGHT`, which is the constant on both sides of
// its own test -- swapping SR_MASK_LEFT and SR_MASK_RIGHT in the model left it
// passing. (Mutation-tested; it survived.) So the mapping is now DERIVED here
// instead: director1 tests the four D-pad bits in a nested tree and, in each
// leaf, writes a direction code to var[1024] and REQSWs the matching handler.
// rinoa::keyl's own first instruction is `var[1024] == 1`, keyr's is `== 2`,
// keyu's `== 3`, keyd's `== 4`, so the codes name the directions without this
// probe having to assert which bit is which. Walking the tree therefore yields
// bit -> direction out of the shipped bytes, and THAT is what the model's
// constants are checked against.
static const int      DIRECTOR1_BASE = 1147;
// v0.63.2 (#111) THE BOOST, straight out of ssspace3.jsm.
//
// Aaron: "There is an option to boost for a limited duration. I am fairly sure
// this is toggled by holding down X, but please confirm in the game exe."
//
// Three windows of real bytecode, because the answer is spread across three
// places and reading any one of them alone gets it wrong:
//
//   DIRECTOR1_BOOST   director1::default 13-23  -- the mask, and var[1034]
//   RINOA_FUEL_INIT   rinoa::default     14-17  -- the step and the 8000 fuel
//   RINOA_KEYL_STEP   rinoa::keyl         5-25  -- the SECOND doubling
static const uint32_t DIRECTOR1_BOOST[] = {
    0x07000010, 0x0000006D, 0x08000000, 0x07000001, 0x01000006, 0x03000004,
    0x07000008, 0x0B00040A, 0x02000003, 0x07000004, 0x0B00040A,
};
static const uint32_t RINOA_FUEL_INIT[] = {
    0x07000004, 0x0B00040A, 0x07001F40, 0x0F00041C,
};
static const uint32_t RINOA_KEYL_STEP[] = {
    0x07000010, 0x0000006D, 0x08000000, 0x07000001, 0x01000006, 0x0300000A,
    0x07053680, 0x0000003D, 0x12000410, 0x0A00040A, 0x07000002, 0x01000002,
    0x01000000, 0x0F000410, 0x02000007, 0x0700A6D0, 0x0000003D, 0x12000410,
    0x0A00040A, 0x01000000, 0x0F000410,
};

static const uint32_t DIRECTOR1_DISPATCH[] = {
    0x07008000, 0x0000006D, 0x08000000, 0x07000001, 0x01000006, 0x0300003A,
    0x07001000, 0x0000006D, 0x08000000, 0x07000001, 0x01000006, 0x03000010,
    0x07000006, 0x0B000400, 0x07000002, 0x0000003C, 0x1200041C, 0x0A00040A,
    0x01000001, 0x0F00041C, 0x07000002, 0x07000012, 0x14000002, 0x07000002,
    0x07000022, 0x14000003, 0x02000024, 0x07004000, 0x0000006D, 0x08000000,
    0x07000001, 0x01000006, 0x03000010, 0x07000008, 0x0B000400, 0x07000002,
    0x0000003C, 0x1200041C, 0x0A00040A, 0x01000001, 0x0F00041C, 0x07000002,
    0x07000015, 0x14000002, 0x07000002, 0x07000024, 0x14000003, 0x0200000F,
    0x07000001, 0x0B000400, 0x07000002, 0x0000003C, 0x1200041C, 0x0A00040A,
    0x01000001, 0x0F00041C, 0x07000002, 0x0700000E, 0x14000002, 0x07000002,
    0x0700001D, 0x14000003, 0x02000071, 0x07002000, 0x0000006D, 0x08000000,
    0x07000001, 0x01000006, 0x0300003A, 0x07001000, 0x0000006D, 0x08000000,
    0x07000001, 0x01000006, 0x03000010, 0x07000005, 0x0B000400, 0x07000002,
    0x0000003C, 0x1200041C, 0x0A00040A, 0x01000001, 0x0F00041C, 0x07000002,
    0x07000013, 0x14000002, 0x07000002, 0x07000021, 0x14000003, 0x02000024,
    0x07004000, 0x0000006D, 0x08000000, 0x07000001, 0x01000006, 0x03000010,
    0x07000007, 0x0B000400, 0x07000002, 0x0000003C, 0x1200041C, 0x0A00040A,
    0x01000001, 0x0F00041C, 0x07000002, 0x07000014, 0x14000002, 0x07000002,
    0x07000023, 0x14000003, 0x0200000F, 0x07000002, 0x0B000400, 0x07000002,
    0x0000003C, 0x1200041C, 0x0A00040A, 0x01000001, 0x0F00041C, 0x07000002,
    0x0700000F, 0x14000002, 0x07000002, 0x0700001E, 0x14000003, 0x02000032,
    0x07001000, 0x0000006D, 0x08000000, 0x07000001, 0x01000006, 0x03000010,
    0x07000003, 0x0B000400, 0x07000002, 0x0000003C, 0x1200041C, 0x0A00040A,
    0x01000001, 0x0F00041C, 0x07000002, 0x07000010, 0x14000002, 0x07000002,
    0x0700001F, 0x14000003, 0x0200001D, 0x07004000, 0x0000006D, 0x08000000,
    0x07000001, 0x01000006, 0x03000010, 0x07000004, 0x0B000400, 0x07000002,
    0x0000003C, 0x1200041C, 0x0A00040A, 0x01000001, 0x0F00041C, 0x07000002,
    0x07000011, 0x14000002, 0x07000002, 0x07000020, 0x14000003, 0x02000008,
    0x07000009, 0x0B000400, 0x07000002, 0x0000003C, 0x07000002, 0x07000025,
    0x14000003,
};

// ---- ssspace3 :: rinoa::key{l,r,u,d}, the unboosted arms ------------------
// Each is: push the axis var, push the step var, OPER, pop the axis var. The
// OPER is the whole sign convention.
static const uint32_t KEYL_ARM[] = {
    0x12000410, 0x0A00040A, 0x01000000, 0x0F000410,
};


static const uint32_t KEYR_ARM[] = {
    0x12000410, 0x0A00040A, 0x01000001, 0x0F000410,
};


static const uint32_t KEYU_ARM[] = {
    0x12000414, 0x0A00040A, 0x01000001, 0x0F000414,
};


static const uint32_t KEYD_ARM[] = {
    0x12000414, 0x0A00040A, 0x01000000, 0x0F000414,
};

// ===========================================================================
// THE PROPAGATOR PAIRING, TAKEN OUT OF ALL EIGHT SCRIPTS (#112)
// ===========================================================================
//
// v0.55.0. The previous version of this probe restated the pairing table as a
// literal and compared the model against it -- two copies of the same belief.
// Mutation-testing proved the point: changing PG_PENDING_MASK from 0x04 to
// 0x08 survived BOTH probes, because nothing anywhere read the mask out of the
// game.
//
// So it is read out of the game now. Two windows per Propagator, extracted
// programmatically from the shipped .jsm files, never typed:
//
//   *_GATE     the first six dwords of alien0N::defalut --
//                  PSHM_B var[446] ; PSHN_L <own> ; AND ; PSHN_L 0 ; EQ
//              which is "am I already dead?", and names the OWN bit.
//
//   *_RESOLVE  from the pending test to the end of the mercy block --
//                  PSHM_B var[445] ; PSHN_L <pendMask> ; AND ; ... ; JPF
//                  [no kill pending]  var[445] |= mask ; var[446] |= own ;
//                                     var[447] = own
//                  [pending]          PSHM_B var[447] ; PSHN_L <partner> ; AND
//                    [not my partner] var[446] &= ~var[447]   <- RESURRECTS it
//                                     var[446] |= own ; var[447] = own
//                    [my partner]     var[445] &= ~mask ; var[446] |= own
//                  then               var[437]++ ; if (var[437] > 24)
//                                     var[446] = 255
//
//              which names the PENDING MASK, the PARTNER bit, and the mercy
//              threshold -- and shows the resurrection that is the whole
//              reason the mod has to tell the player what to kill next.
//
// The eight partner bits are then checked to CLOSE: every A's partner is a B
// whose partner is A. That is a property of the eight scripts, and the model
// is checked against it rather than against a copy of itself.

// rgair1.jsm dwords 459..464 -- alien01::defalut, start through the am-I-dead gate
static const uint32_t RGAIR1_GATE[] = {
    0x05000010, 0x0A0001BE, 0x07000001, 0x0100000C, 0x07000000, 0x01000006,
};

// rgair1.jsm dwords 486..540 -- alien01::defalut, pair resolution + mercy
static const uint32_t RGAIR1_RESOLVE[] = {
    0x0A0001BD, 0x07000004, 0x0100000C, 0x07000000, 0x01000006, 0x0300000C,
    0x0A0001BD, 0x07000004, 0x0100000D, 0x0B0001BD, 0x0A0001BE, 0x07000001,
    0x0100000D, 0x0B0001BE, 0x07000001, 0x0B0001BF, 0x0200001D, 0x0A0001BF,
    0x07000080, 0x0100000C, 0x07000000, 0x01000006, 0x0300000D, 0x0A0001BE,
    0x0A0001BF, 0x0100000F, 0x0100000C, 0x0B0001BE, 0x0A0001BE, 0x07000001,
    0x0100000D, 0x0B0001BE, 0x07000001, 0x0B0001BF, 0x0200000B, 0x0A0001BD,
    0x07FFFFFB, 0x0100000C, 0x0B0001BD, 0x0A0001BE, 0x07000001, 0x0100000D,
    0x0B0001BE, 0x07000001, 0x0B0001BF, 0x0A0001B5, 0x07000001, 0x01000000,
    0x0B0001B5, 0x0A0001B5, 0x07000018, 0x01000007, 0x03000003, 0x070000FF,
    0x0B0001BE,
};

// rgroad1.jsm dwords 185..190 -- alien01::defalut, start through the am-I-dead gate
static const uint32_t RGROAD1_GATE[] = {
    0x05000010, 0x0A0001BE, 0x07000002, 0x0100000C, 0x07000000, 0x01000006,
};

// rgroad1.jsm dwords 213..267 -- alien01::defalut, pair resolution + mercy
static const uint32_t RGROAD1_RESOLVE[] = {
    0x0A0001BD, 0x07000004, 0x0100000C, 0x07000000, 0x01000006, 0x0300000C,
    0x0A0001BD, 0x07000004, 0x0100000D, 0x0B0001BD, 0x0A0001BE, 0x07000002,
    0x0100000D, 0x0B0001BE, 0x07000002, 0x0B0001BF, 0x0200001D, 0x0A0001BF,
    0x07000020, 0x0100000C, 0x07000000, 0x01000006, 0x0300000D, 0x0A0001BE,
    0x0A0001BF, 0x0100000F, 0x0100000C, 0x0B0001BE, 0x0A0001BE, 0x07000002,
    0x0100000D, 0x0B0001BE, 0x07000002, 0x0B0001BF, 0x0200000B, 0x0A0001BD,
    0x07FFFFFB, 0x0100000C, 0x0B0001BD, 0x0A0001BE, 0x07000002, 0x0100000D,
    0x0B0001BE, 0x07000002, 0x0B0001BF, 0x0A0001B5, 0x07000001, 0x01000000,
    0x0B0001B5, 0x0A0001B5, 0x07000018, 0x01000007, 0x03000003, 0x070000FF,
    0x0B0001BE,
};

// rgroad2.jsm dwords 204..209 -- alien01::defalut, start through the am-I-dead gate
static const uint32_t RGROAD2_GATE[] = {
    0x0500000A, 0x0A0001BE, 0x07000004, 0x0100000C, 0x07000000, 0x01000006,
};

// rgroad2.jsm dwords 311..365 -- alien01::defalut, pair resolution + mercy
static const uint32_t RGROAD2_RESOLVE[] = {
    0x0A0001BD, 0x07000004, 0x0100000C, 0x07000000, 0x01000006, 0x0300000C,
    0x0A0001BD, 0x07000004, 0x0100000D, 0x0B0001BD, 0x0A0001BE, 0x07000004,
    0x0100000D, 0x0B0001BE, 0x07000004, 0x0B0001BF, 0x0200001D, 0x0A0001BF,
    0x07000008, 0x0100000C, 0x07000000, 0x01000006, 0x0300000D, 0x0A0001BE,
    0x0A0001BF, 0x0100000F, 0x0100000C, 0x0B0001BE, 0x0A0001BE, 0x07000004,
    0x0100000D, 0x0B0001BE, 0x07000004, 0x0B0001BF, 0x0200000B, 0x0A0001BD,
    0x07FFFFFB, 0x0100000C, 0x0B0001BD, 0x0A0001BE, 0x07000004, 0x0100000D,
    0x0B0001BE, 0x07000004, 0x0B0001BF, 0x0A0001B5, 0x07000001, 0x01000000,
    0x0B0001B5, 0x0A0001B5, 0x07000018, 0x01000007, 0x03000003, 0x070000FF,
    0x0B0001BE,
};

// rgroad3.jsm dwords 650..659 -- alien02::defalut, start through the am-I-dead gate
static const uint32_t RGROAD3_GATE[] = {
    0x0500001E, 0x0C000100, 0x07000BBF, 0x01000008, 0x030000F5, 0x0A0001BE,
    0x07000008, 0x0100000C, 0x07000000, 0x01000006,
};

// rgroad3.jsm dwords 822..876 -- alien02::defalut, pair resolution + mercy
static const uint32_t RGROAD3_RESOLVE[] = {
    0x0A0001BD, 0x07000004, 0x0100000C, 0x07000000, 0x01000006, 0x0300000C,
    0x0A0001BD, 0x07000004, 0x0100000D, 0x0B0001BD, 0x0A0001BE, 0x07000008,
    0x0100000D, 0x0B0001BE, 0x07000008, 0x0B0001BF, 0x0200001D, 0x0A0001BF,
    0x07000004, 0x0100000C, 0x07000000, 0x01000006, 0x0300000D, 0x0A0001BE,
    0x0A0001BF, 0x0100000F, 0x0100000C, 0x0B0001BE, 0x0A0001BE, 0x07000008,
    0x0100000D, 0x0B0001BE, 0x07000008, 0x0B0001BF, 0x0200000B, 0x0A0001BD,
    0x07FFFFFB, 0x0100000C, 0x0B0001BD, 0x0A0001BE, 0x07000008, 0x0100000D,
    0x0B0001BE, 0x07000008, 0x0B0001BF, 0x0A0001B5, 0x07000001, 0x01000000,
    0x0B0001B5, 0x0A0001B5, 0x07000018, 0x01000007, 0x03000003, 0x070000FF,
    0x0B0001BE,
};

// rghang1.jsm dwords 248..253 -- alien01::defalut, start through the am-I-dead gate
static const uint32_t RGHANG1_GATE[] = {
    0x05000009, 0x0A0001BE, 0x07000010, 0x0100000C, 0x07000000, 0x01000006,
};

// rghang1.jsm dwords 448..502 -- alien01::defalut, pair resolution + mercy
static const uint32_t RGHANG1_RESOLVE[] = {
    0x0A0001BD, 0x07000004, 0x0100000C, 0x07000000, 0x01000006, 0x0300000C,
    0x0A0001BD, 0x07000004, 0x0100000D, 0x0B0001BD, 0x0A0001BE, 0x07000010,
    0x0100000D, 0x0B0001BE, 0x07000010, 0x0B0001BF, 0x0200001D, 0x0A0001BF,
    0x07000040, 0x0100000C, 0x07000000, 0x01000006, 0x0300000D, 0x0A0001BE,
    0x0A0001BF, 0x0100000F, 0x0100000C, 0x0B0001BE, 0x0A0001BE, 0x07000010,
    0x0100000D, 0x0B0001BE, 0x07000010, 0x0B0001BF, 0x0200000B, 0x0A0001BD,
    0x07FFFFFB, 0x0100000C, 0x0B0001BD, 0x0A0001BE, 0x07000010, 0x0100000D,
    0x0B0001BE, 0x07000010, 0x0B0001BF, 0x0A0001B5, 0x07000001, 0x01000000,
    0x0B0001B5, 0x0A0001B5, 0x07000018, 0x01000007, 0x03000003, 0x070000FF,
    0x0B0001BE,
};

// rghang2.jsm dwords 587..592 -- alien01::defalut, start through the am-I-dead gate
static const uint32_t RGHANG2_GATE[] = {
    0x05000025, 0x0A0001BE, 0x07000020, 0x0100000C, 0x07000000, 0x01000006,
};

// rghang2.jsm dwords 702..756 -- alien01::defalut, pair resolution + mercy
static const uint32_t RGHANG2_RESOLVE[] = {
    0x0A0001BD, 0x07000004, 0x0100000C, 0x07000000, 0x01000006, 0x0300000C,
    0x0A0001BD, 0x07000004, 0x0100000D, 0x0B0001BD, 0x0A0001BE, 0x07000020,
    0x0100000D, 0x0B0001BE, 0x07000020, 0x0B0001BF, 0x0200001D, 0x0A0001BF,
    0x07000002, 0x0100000C, 0x07000000, 0x01000006, 0x0300000D, 0x0A0001BE,
    0x0A0001BF, 0x0100000F, 0x0100000C, 0x0B0001BE, 0x0A0001BE, 0x07000020,
    0x0100000D, 0x0B0001BE, 0x07000020, 0x0B0001BF, 0x0200000B, 0x0A0001BD,
    0x07FFFFFB, 0x0100000C, 0x0B0001BD, 0x0A0001BE, 0x07000020, 0x0100000D,
    0x0B0001BE, 0x07000020, 0x0B0001BF, 0x0A0001B5, 0x07000001, 0x01000000,
    0x0B0001B5, 0x0A0001B5, 0x07000018, 0x01000007, 0x03000003, 0x070000FF,
    0x0B0001BE,
};

// rgexit1.jsm dwords 191..196 -- alien01::defalut, start through the am-I-dead gate
static const uint32_t RGEXIT1_GATE[] = {
    0x05000009, 0x0A0001BE, 0x07000040, 0x0100000C, 0x07000000, 0x01000006,
};

// rgexit1.jsm dwords 230..284 -- alien01::defalut, pair resolution + mercy
static const uint32_t RGEXIT1_RESOLVE[] = {
    0x0A0001BD, 0x07000004, 0x0100000C, 0x07000000, 0x01000006, 0x0300000C,
    0x0A0001BD, 0x07000004, 0x0100000D, 0x0B0001BD, 0x0A0001BE, 0x07000040,
    0x0100000D, 0x0B0001BE, 0x07000040, 0x0B0001BF, 0x0200001D, 0x0A0001BF,
    0x07000010, 0x0100000C, 0x07000000, 0x01000006, 0x0300000D, 0x0A0001BE,
    0x0A0001BF, 0x0100000F, 0x0100000C, 0x0B0001BE, 0x0A0001BE, 0x07000040,
    0x0100000D, 0x0B0001BE, 0x07000040, 0x0B0001BF, 0x0200000B, 0x0A0001BD,
    0x07FFFFFB, 0x0100000C, 0x0B0001BD, 0x0A0001BE, 0x07000040, 0x0100000D,
    0x0B0001BE, 0x07000040, 0x0B0001BF, 0x0A0001B5, 0x07000001, 0x01000000,
    0x0B0001B5, 0x0A0001B5, 0x07000018, 0x01000007, 0x03000003, 0x070000FF,
    0x0B0001BE,
};

// rgguest2.jsm dwords 99..104 -- alien01::defalut, start through the am-I-dead gate
static const uint32_t RGGUEST2_GATE[] = {
    0x0500000C, 0x0A0001BE, 0x07000080, 0x0100000C, 0x07000000, 0x01000006,
};

// rgguest2.jsm dwords 157..211 -- alien01::defalut, pair resolution + mercy
static const uint32_t RGGUEST2_RESOLVE[] = {
    0x0A0001BD, 0x07000004, 0x0100000C, 0x07000000, 0x01000006, 0x0300000C,
    0x0A0001BD, 0x07000004, 0x0100000D, 0x0B0001BD, 0x0A0001BE, 0x07000080,
    0x0100000D, 0x0B0001BE, 0x07000080, 0x0B0001BF, 0x0200001D, 0x0A0001BF,
    0x07000001, 0x0100000C, 0x07000000, 0x01000006, 0x0300000D, 0x0A0001BE,
    0x0A0001BF, 0x0100000F, 0x0100000C, 0x0B0001BE, 0x0A0001BE, 0x07000080,
    0x0100000D, 0x0B0001BE, 0x07000080, 0x0B0001BF, 0x0200000B, 0x0A0001BD,
    0x07FFFFFB, 0x0100000C, 0x0B0001BD, 0x0A0001BE, 0x07000080, 0x0100000D,
    0x0B0001BE, 0x07000080, 0x0B0001BF, 0x0A0001B5, 0x07000001, 0x01000000,
    0x0B0001B5, 0x0A0001B5, 0x07000018, 0x01000007, 0x03000003, 0x070000FF,
    0x0B0001BE,
};

struct AlienFix { const char* field; const uint32_t* gate; int gateN;
                  const uint32_t* res;  int resN; };
static const AlienFix ALIEN_FIX[] = {
    { "rgair1", RGAIR1_GATE, (int)(sizeof(RGAIR1_GATE)/sizeof(uint32_t)), RGAIR1_RESOLVE, (int)(sizeof(RGAIR1_RESOLVE)/sizeof(uint32_t)) },
    { "rgroad1", RGROAD1_GATE, (int)(sizeof(RGROAD1_GATE)/sizeof(uint32_t)), RGROAD1_RESOLVE, (int)(sizeof(RGROAD1_RESOLVE)/sizeof(uint32_t)) },
    { "rgroad2", RGROAD2_GATE, (int)(sizeof(RGROAD2_GATE)/sizeof(uint32_t)), RGROAD2_RESOLVE, (int)(sizeof(RGROAD2_RESOLVE)/sizeof(uint32_t)) },
    { "rgroad3", RGROAD3_GATE, (int)(sizeof(RGROAD3_GATE)/sizeof(uint32_t)), RGROAD3_RESOLVE, (int)(sizeof(RGROAD3_RESOLVE)/sizeof(uint32_t)) },
    { "rghang1", RGHANG1_GATE, (int)(sizeof(RGHANG1_GATE)/sizeof(uint32_t)), RGHANG1_RESOLVE, (int)(sizeof(RGHANG1_RESOLVE)/sizeof(uint32_t)) },
    { "rghang2", RGHANG2_GATE, (int)(sizeof(RGHANG2_GATE)/sizeof(uint32_t)), RGHANG2_RESOLVE, (int)(sizeof(RGHANG2_RESOLVE)/sizeof(uint32_t)) },
    { "rgexit1", RGEXIT1_GATE, (int)(sizeof(RGEXIT1_GATE)/sizeof(uint32_t)), RGEXIT1_RESOLVE, (int)(sizeof(RGEXIT1_RESOLVE)/sizeof(uint32_t)) },
    { "rgguest2", RGGUEST2_GATE, (int)(sizeof(RGGUEST2_GATE)/sizeof(uint32_t)), RGGUEST2_RESOLVE, (int)(sizeof(RGGUEST2_RESOLVE)/sizeof(uint32_t)) },
};
static const int ALIEN_FIX_N = (int)(sizeof(ALIEN_FIX)/sizeof(ALIEN_FIX[0]));


int main()
{
    // =====================================================================
    // #110 ESTHAR -- every gate, decoded, and the model checked against it
    // =====================================================================
    {
        check(EpTablesConsistent(), "**the generated route and site tables close**");
        check(EP_TIMER_ADDR == 0x01CFE92Cu,
              "**the clock is GETTIMER's own address** (handler 0x00521710 is "
              "`mov eax,[0x01CFE92C]`), not the var[1024] HUD mirror that flooded "
              "the 2026-08-22 BAT");
        check(EP_SITE_COUNT == 8, "eight boarding sites");
        check(EP_FIX_N == 8, "and eight extracted gates to check them against");

        for (int k = 0; k < EP_FIX_N; k++) {
            const EpFix& F = EP_FIX[k];
            int lo = -1, hi = -1, dest = -1, usedBit = 0, missDest = -1;
            bool hiExcl = false, sawLive = false, sawBusy = false, isTouch = false;

            for (int i = 0; i + 3 < F.n; i++) {
                int o, p; bool h; dec(F.code[i], &o, &p, &h);
                int o2, p2; bool h2; dec(F.code[i+1], &o2, &p2, &h2);
                int o3, p3; bool h3; dec(F.code[i+2], &o3, &p3, &h3);
                if (o == OP_PSHM_W && p == EP_VAR_MISSION && o2 == OP_PSHN_L && p2 == 19 &&
                    o3 == OP_OPER && p3 == OP_EQ) sawLive = true;
                if (o == OP_PSHM_B && p == EP_VAR_USED && o2 == OP_PSHN_L &&
                    o3 == OP_OPER && p3 == OP_AND && !usedBit) usedBit = p2;
                if (o == OP_PSHM_B && p == EP_VAR_BUSY && o2 == OP_PSHN_L && p2 == 0 &&
                    o3 == OP_OPER && p3 == OP_EQ) sawBusy = true;
                if (o == OP_PSHN_L && (p == 417 || p == 418 || p == 419) && dest < 0) dest = p;
                // THE WINDOW. The two bounds are not in a fixed order: the CP1
                // and CP3 gates test `t > lo` first, the CP2 gates test
                // `t <= hi` first. So take whichever comparison each one is,
                // rather than assuming a sequence -- assuming it is what made
                // this probe read the CP2 windows as -1..-1 on its first run.
                if (o == OP_PSHN_L && o2 == OP_OPER) {
                    if (p2 == OP_GT && lo < 0) lo = p;
                    else if ((p2 == OP_LE || p2 == OP_LT) && hi < 0) {
                        hi = p; hiExcl = (p2 == OP_LT);
                    }
                }
            }
            // The miss destination: the first MAPJUMP whose pushed field is a
            // real city field and NOT the boarding field. (Filtering by numeric
            // range alone catches 417/418/419 themselves.)
            for (int i = 0; i + 1 < F.n && missDest < 0; i++) {
                int o, p; bool h; dec(F.code[i], &o, &p, &h);
                if (o != OP_PSHN_L) continue;
                if (p == 417 || p == 418 || p == 419) continue;
                if (!EpFieldName(p)) continue;
                for (int q = i + 1; q < F.n && q <= i + 6; q++) {
                    int o2, p2; bool h2; dec(F.code[q], &o2, &p2, &h2);
                    if (o2 == OP_MAPJUMP || o2 == OP_MAPJUMP3 || o2 == OP_MAPJUMPO) {
                        missDest = p; break;
                    }
                    if (o2 != OP_PSHN_L && o2 != OP_OPER) break;
                }
            }
            isTouch = (strcmp(F.kind, "LINE") == 0);

            char m[220];
            snprintf(m, sizeof(m), "%s -> %d: the script gates on var[672]==19", F.field, F.dest);
            check(sawLive, m);
            snprintf(m, sizeof(m), "%s -> %d: the boarding push is field %d", F.field, F.dest, F.dest);
            check(dest == F.dest, m);
            snprintf(m, sizeof(m), "%s -> %d: a timer window was found (got %d..%d)",
                     F.field, F.dest, lo, hi);
            check(lo >= 0 && hi > lo, m);
            snprintf(m, sizeof(m), "%s -> %d: only the AUTO sites gate on var[1029]", F.field, F.dest);
            check(sawBusy == (strcmp(F.kind, "AUTO") == 0), m);

            // ...and now the MODEL, against all of that.
            const EstharSite* site = nullptr;
            for (int q = 0; q < EP_SITE_COUNT; q++)
                if (strcmp(EP_SITES[q].field, F.field) == 0 &&
                    EP_TARGET_FIELD[0] >= 0 &&
                    ((F.dest == 417 && EP_SITES[q].cp == EP_CP1) ||
                     (F.dest == 418 && EP_SITES[q].cp == EP_CP2) ||
                     (F.dest == 419 && EP_SITES[q].cp == EP_CP3))) { site = &EP_SITES[q]; break; }
            snprintf(m, sizeof(m), "%s -> %d has a row in EP_SITES", F.field, F.dest);
            check(site != nullptr, m);
            if (!site) continue;

            snprintf(m, sizeof(m), "**%s -> %d: model window %d..%d%s is the script's %d..%d%s**",
                     F.field, F.dest, site->lo, site->hi, site->hiExclusive ? " (exclusive)" : "",
                     lo, hi, hiExcl ? " (exclusive)" : "");
            check(site->lo == lo && site->hi == hi && site->hiExclusive == hiExcl, m);

            snprintf(m, sizeof(m), "**%s -> %d: model used-bit 0x%02X is the script's 0x%02X**",
                     F.field, F.dest, site->usedBit, usedBit);
            check(site->usedBit == usedBit, m);

            snprintf(m, sizeof(m), "%s -> %d: model kind %s matches", F.field, F.dest,
                     site->kind == EP_AUTO ? "AUTO" : "LINE");
            check((site->kind == EP_LINE) == isTouch, m);

            if (missDest > 0) {
                snprintf(m, sizeof(m), "**%s -> %d: model miss-branch %d is the script's %d**",
                         F.field, F.dest, site->missField, missDest);
                check(site->missField == missDest, m);
            }
        }

        // The six LINE sites are the whole point: waiting on one does nothing.
        // Pin the count, so a future edit cannot quietly turn them back into
        // stand-and-wait and reintroduce the BAT failure.
        {
            int autos = 0, lines = 0;
            for (int q = 0; q < EP_SITE_COUNT; q++)
                (EP_SITES[q].kind == EP_AUTO ? autos : lines)++;
            check(autos == 2 && lines == 6,
                  "**two stand-and-wait sites and six walk-through lines**");
            check(EpSiteAt("eciway11", EP_CP1) && EpSiteAt("eciway11", EP_CP1)->kind == EP_AUTO,
                  "eciway11 is the CP1 stand-and-wait site");
            check(EpSiteAt("ecoway3", EP_CP3) && EpSiteAt("ecoway3", EP_CP3)->kind == EP_AUTO,
                  "ecoway3 is the CP3 stand-and-wait site");
            check(EpSiteAt("eccway21", EP_CP1) && EpSiteAt("eccway21", EP_CP1)->kind == EP_LINE,
                  "**eccway21 -- where the BAT failed -- is a LINE, not a place to wait**");
        }

        // The safety property the whole design rests on: from eccway11 and
        // eccway21 the CP1 line MISSES to eciway11, which fires by itself. So
        // crossing early costs nothing.
        {
            const EstharSite* a1 = EpSiteAt("eccway11", EP_CP1);
            const EstharSite* b1 = EpSiteAt("eccway21", EP_CP1);
            check(a1 && b1 && a1->missField == 425 && b1->missField == 425,
                  "**both CP1 lines miss to eciway11**, so crossing early lands you on "
                  "the site that fires on its own");
            const EstharSite* c3 = EpSiteAt("ecmall1", EP_CP3);
            const EstharSite* d3 = EpSiteAt("eccway41", EP_CP3);
            check(c3 && d3 && c3->missField == 458 && d3->missField == 458,
                  "and both CP3 lines miss to ecoway3, likewise");
        }

        // Every city field must be able to reach every contact point, or the
        // route table would strand the player somewhere.
        {
            int stranded = 0;
            for (int i = 0; i < EP_HOP_COUNT; i++)
                for (int cp = 0; cp < EP_CP_COUNT; cp++)
                    if (EP_HOPS[i].to[cp].hops < 0) stranded++;
            check(stranded == 0, "every city field reaches every contact point");
            check(EP_HOP_COUNT == 47, "47 city fields in the route table");
        }

        // The briefing's own numbers, which the planning windows must match.
        check(EP_CP1_HI == 15*60 && EP_CP1_LO == 12*60, "point 1 is 15:00 down to 12:00");
        check(EP_CP2_HI == 10*60 && EP_CP2_LO ==  5*60, "point 2 is 10:00 down to 5:00");
        check(EP_CP3_HI ==  3*60 && EP_CP3_LO ==      0, "point 3 is 3:00 down to 0:00");

        char c[64];
        EpClock(905, c, sizeof(c)); checkStr(c, "15 minutes 5 seconds", "the clock reads in words");
        EpClock(60,  c, sizeof(c)); checkStr(c, "1 minute 0 seconds",  "and singular at one minute");
    }

    // =====================================================================
    // #111 SPACE -- the win box and the direction to press
    // =====================================================================
    {
        // The verdict fixture must be four comparisons against 180 on vars
        // 1040 and 1044, and nothing else.
        int cmp = 0, box = 0;
        const int n = (int)(sizeof(SSSPACE3_VERDICT)/sizeof(SSSPACE3_VERDICT[0]));
        for (int i = 0; i < n; i++) {
            int o,p; bool h; dec(SSSPACE3_VERDICT[i], &o, &p, &h);
            if (o == OP_PUSHL_L && (p == 1040 || p == 1044)) cmp++;
            if (o == OP_PSHN_L && p == SR_WIN_BOX) box++;
        }
        check(cmp == 4 && box == 4,
              "**the verdict is four tests of vars 1040/1044 against 180** -- the "
              "model's win box is the game's, not a tolerance I picked");
        int o,p; bool h;
        dec(SSSPACE3_VERDICT[n-1], &o, &p, &h);
        check(o == OP_POPM_B && p == 1035, "and it writes var[1035], the win flag");
        check((SR_ADDR_X - 0x01CFE9B8u) == 1040 && (SR_ADDR_Y - 0x01CFE9B8u) == 1044,
              "the addresses are those variables' own slots");

        // THE SIGNS. Read out of the key handlers: the mask to press on an axis
        // is the one whose handler moves the variable toward zero. Getting this
        // backwards would actively steer the player away from Rinoa.
        int oX,pX; bool hX;
        dec(KEYL_ARM[2], &oX, &pX, &hX);
        check(oX == OP_OPER && pX == 0, "rinoa::keyl ADDs to X, so LEFT raises it");
        dec(KEYR_ARM[2], &oX, &pX, &hX);
        check(oX == OP_OPER && pX == 1, "rinoa::keyr SUBs from X, so RIGHT lowers it");
        dec(KEYU_ARM[2], &oX, &pX, &hX);
        check(oX == OP_OPER && pX == 1, "rinoa::keyu SUBs from Y, so UP lowers it");
        dec(KEYD_ARM[2], &oX, &pX, &hX);
        check(oX == OP_OPER && pX == 0, "rinoa::keyd ADDs to Y, so DOWN raises it");
        dec(KEYL_ARM[0], &oX, &pX, &hX);
        check(pX == 1040, "keyl operates on X");
        dec(KEYU_ARM[0], &oX, &pX, &hX);
        check(pX == 1044, "keyu operates on Y");

        // ---- bit -> direction, walked out of director1's own tree ----------
        //
        // Each leaf is `PSHN_L <code> ; POPM_B var[1024]`, and the enclosing
        // BTN_HELD tests are the bits that produced it. Walk the tree keeping
        // the set of bits currently known-held, and record the code each leaf
        // writes. A leaf reached with exactly one bit held names that bit.
        {
            const int n = (int)(sizeof(DIRECTOR1_DISPATCH)/sizeof(DIRECTOR1_DISPATCH[0]));
            // bitFor[code] accumulates the mask asserted true on the path.
            uint16_t bitFor[16]; int seen[16];
            for (int k = 0; k < 16; k++) { bitFor[k] = 0; seen[k] = 0; }
            // A straight-line walk is enough: the tree is a chain of
            // `PSHN_L m ; BTN_HELD ; PSHL 0 ; PSHN_L 1 ; OPER EQ ; JPF +d`
            // blocks, and the code between a test and its JPF target is the
            // "held" arm. Track the innermost pending test per position by
            // scanning for tests whose JPF target is beyond the leaf.
            struct { uint16_t mask; int endIdx; } pend[8]; int np = 0;
            for (int i = 0; i < n; i++) {
                int o, p; bool h; dec(DIRECTOR1_DISPATCH[i], &o, &p, &h);
                while (np > 0 && i >= pend[np-1].endIdx) np--;
                if (o == OP_PSHN_L && i + 5 < n) {
                    int o2, p2; bool h2; dec(DIRECTOR1_DISPATCH[i+1], &o2, &p2, &h2);
                    int o6, p6; bool h6; dec(DIRECTOR1_DISPATCH[i+5], &o6, &p6, &h6);
                    if (o2 == OP_BTN_HELD && o6 == OP_JPF && np < 8) {
                        pend[np].mask = (uint16_t)p;
                        pend[np].endIdx = (i + 5) + p6;   // targets are i + rel
                        np++;
                        i += 5;
                        continue;
                    }
                }
                // A leaf write: PSHN_L code ; POPM_B var[1024].
                if (o == OP_PSHN_L && i + 1 < n) {
                    int o2, p2; bool h2; dec(DIRECTOR1_DISPATCH[i+1], &o2, &p2, &h2);
                    if (o2 == OP_POPM_B && p2 == 1024 && p >= 0 && p < 16) {
                        if (np == 1) { bitFor[p] = pend[0].mask; seen[p] = 1; }
                        else if (np > 1) { seen[p] = 2; }   // diagonal, two bits
                    }
                }
            }
            // rinoa::key{l,r,u,d} gate on var[1024] == 1/2/3/4 respectively --
            // that is the first instruction of each, decoded below -- so the
            // codes are the directions.
            check(seen[1] == 1 && bitFor[1] == SR_MASK_LEFT,
                  "**director1: the bit that yields direction code 1 (rinoa::keyl) is SR_MASK_LEFT**");
            check(seen[2] == 1 && bitFor[2] == SR_MASK_RIGHT,
                  "**director1: code 2 (rinoa::keyr) is SR_MASK_RIGHT**");
            check(seen[3] == 1 && bitFor[3] == SR_MASK_UP,
                  "**director1: code 3 (rinoa::keyu) is SR_MASK_UP**");
            check(seen[4] == 1 && bitFor[4] == SR_MASK_DOWN,
                  "**director1: code 4 (rinoa::keyd) is SR_MASK_DOWN**");
            check(seen[5] == 2 && seen[6] == 2 && seen[7] == 2 && seen[8] == 2,
                  "and codes 5-8 are the four diagonals, each under two bits");
            // All four masks distinct, so a copy-paste cannot alias two of them.
            check(SR_MASK_LEFT != SR_MASK_RIGHT && SR_MASK_UP != SR_MASK_DOWN &&
                  SR_MASK_LEFT != SR_MASK_UP && SR_MASK_LEFT != SR_MASK_DOWN &&
                  SR_MASK_RIGHT != SR_MASK_UP && SR_MASK_RIGHT != SR_MASK_DOWN,
                  "the four D-pad masks are four different bits");
            check(SR_MASK_BOOST == 16,
                  "and the boost bit is 16, the mask each handler tests for the long step");
        }

        // ---------------------------------------------------------------
        // THE BOOST (v0.63.2)
        // ---------------------------------------------------------------
        {
            int o, p; bool h;
            // director1::default 13-23: PSHN_L <mask>; BTN_HELD; ... == 1;
            // JZ 4 -> {8 -> var1034}; JMP 3 -> {4 -> var1034}.
            dec(DIRECTOR1_BOOST[0], &o, &p, &h);
            check(o == OP_PSHN_L && p == SR_MASK_BOOST,
                  "**director1 tests SR_MASK_BOOST for the boost** -- 0x0010, which "
                  "is the learner's own slot; Aaron's own logs measured 0x0040 for X");
            dec(DIRECTOR1_BOOST[1], &o, &p, &h);
            check(o == OP_BTN_HELD,
                  "**with BTN_HELD, a LEVEL test** -- the boost is held, not toggled");
            dec(DIRECTOR1_BOOST[6], &o, &p, &h);
            check(o == OP_PSHN_L && p == SR_STEP_VAR_BOOST, "held -> var[1034] = 8");
            dec(DIRECTOR1_BOOST[7], &o, &p, &h);
            check(o == OP_POPM_B && p == 1034, "...written to var[1034]");
            dec(DIRECTOR1_BOOST[9], &o, &p, &h);
            check(o == OP_PSHN_L && p == SR_STEP, "not held -> var[1034] = 4");
            dec(DIRECTOR1_BOOST[10], &o, &p, &h);
            check(o == OP_POPM_B && p == 1034, "...to the same byte");

            // rinoa::default 14-17: the step starts at 4 and the fuel at 8000.
            dec(RINOA_FUEL_INIT[0], &o, &p, &h);
            check(o == OP_PSHN_L && p == SR_STEP, "the scene opens unboosted");
            dec(RINOA_FUEL_INIT[2], &o, &p, &h);
            check(o == OP_PSHN_L && p == SR_FUEL_FULL,
                  "**and with 8000 units of fuel** -- SR_FUEL_FULL");
            dec(RINOA_FUEL_INIT[3], &o, &p, &h);
            check(o == OP_POPL_L && p == 1052,
                  "written to var[1052], which is SR_ADDR_FUEL and gauge 0");

            // rinoa::keyl 5-25: the SAME mask again, and this time a x2 on top
            // of the 8 director1 already put in var[1034]. Reading only one of
            // the two places says the boost is twice the speed. It is four.
            dec(RINOA_KEYL_STEP[0], &o, &p, &h);
            check(o == OP_PSHN_L && p == SR_MASK_BOOST,
                  "the key handler tests the boost mask for itself");
            {
                int mulAt = -1;
                for (int i = 0; i < 14; i++) {
                    int o2, p2; bool h2; dec(RINOA_KEYL_STEP[i], &o2, &p2, &h2);
                    if (o2 == OP_OPER && p2 == 2) mulAt = i;      // 2 = mul
                }
                check(mulAt > 0, "**and multiplies** in its boosted branch");
                int o3, p3; bool h3; dec(RINOA_KEYL_STEP[mulAt - 1], &o3, &p3, &h3);
                check(o3 == OP_PSHN_L && p3 == 2, "...by two");
                dec(RINOA_KEYL_STEP[mulAt - 2], &o3, &p3, &h3);
                check(o3 == OP_PSHM_B && p3 == 1034,
                      "...applied to var[1034], which director1 has already set to 8");
            }
            // The unboosted branch adds var[1034] with no multiply at all.
            {
                bool plainAdd = false;
                for (int i = 15; i + 1 < (int)(sizeof(RINOA_KEYL_STEP)/sizeof(RINOA_KEYL_STEP[0])); i++) {
                    int o2, p2, o3, p3; bool h2, h3;
                    dec(RINOA_KEYL_STEP[i], &o2, &p2, &h2);
                    dec(RINOA_KEYL_STEP[i+1], &o3, &p3, &h3);
                    if (o2 == OP_PSHM_B && p2 == 1034 && o3 == OP_OPER && p3 == 0)
                        plainAdd = true;
                }
                check(plainAdd, "and the unboosted branch adds var[1034] straight");
            }
            check(SR_STEP_BOOST == SR_STEP_VAR_BOOST * 2 && SR_STEP_BOOST == 16,
                  "**so a boosted frame moves 16 and an unboosted one 4 -- FOUR "
                  "times, not twice**");
            check(SR_STEP_BOOST == SR_STEP * 4, "which is the same statement");
        }

        // The fuel, in words. Bands only, and only on the way down.
        check(SrFuelPct(SR_FUEL_FULL) == 100 && SrFuelPct(0) == 0,
              "the gauge reads 100 at 8000 and 0 at nothing");
        check(SrFuelPct(SR_FUEL_FULL / 2) == 50, "and half way at half");
        check(SrFuelPct(-500) == 0,
              "**a negative gauge reads empty, not a negative percentage** -- "
              "nothing clamps var[1052], so it does go past zero");
        check(SrFuelBand(100) == 3 && SrFuelBand(51) == 3, "above half is no news");
        check(SrFuelBand(50) == 2 && SrFuelBand(26) == 2, "half");
        check(SrFuelBand(25) == 1 && SrFuelBand(1)  == 1, "low");
        check(SrFuelBand(0) == 0, "empty");
        {
            std::string empty = SrFuelWord(0);
            check(empty.find("gauge") != std::string::npos,
                  "**the empty line names the GAUGE, not a cutoff** -- no script in "
                  "ssspace1/2/3 reads var[1052] and FF8_EN.exe holds no reference to "
                  "its address, so the boost keeps working after the bar bottoms out");
        }
        check(std::string(SrFuelWord(3)).empty(), "and a full gauge has nothing to say");

        check(SrMaskForX( 4000) == SR_MASK_RIGHT, "**Rinoa right of centre -> press RIGHT**");
        check(SrMaskForX(-4000) == SR_MASK_LEFT,  "**Rinoa left of centre -> press LEFT**");
        check(SrMaskForY( 4000) == SR_MASK_UP,    "**Rinoa above centre -> press UP**");
        check(SrMaskForY(-4000) == SR_MASK_DOWN,  "**Rinoa below centre -> press DOWN**");
        check(SrMaskForX(0) == 0 && SrMaskForY(0) == 0, "and centred asks for nothing");
        // v0.63.0: STEERING stops at the AIM box, a third of the verdict's, so
        // that hearing "centred" leaves room for a 400 ms reaction instead of
        // parking the player on the boundary of a test taken at an instant he
        // cannot see coming. The verdict itself is untouched.
        check(SrMaskForX(SR_AIM_BOX - 1) == 0, "the axis goes quiet at the aim box");
        check(SrMaskForX(SR_WIN_BOX - 1) == SR_MASK_RIGHT,
              "**and still steers at 179 -- inside the game's box, outside the aim box**");
        check(SrCentred(SR_WIN_BOX - 1, 0) && !SrHeld(SR_WIN_BOX - 1, 0),
              "179 wins the scene but is not what the mod calls centred");
        check(SR_AIM_BOX < SR_WIN_BOX, "the aim box is inside the win box, not the other way round");
        check(SrMaskForX(SR_WIN_BOX) != 0,     "and speaks again one unit outside it");

        check(SrCentred(0,0) && SrCentred(179,-179), "inside the box counts as centred");
        check(!SrCentred(180,0) && !SrCentred(0,-180), "the boundary itself does not");
        check(!SrCentred(SR_CLAMP_X, SR_CLAMP_Y), "and neither does the far corner");
    }

    // =====================================================================
    // #112 PROPAGATOR -- the pairing, which is what actually solves it
    // =====================================================================
    {
        // ---- read own / partner / pending mask / mercy out of the eight ----
        struct Derived { uint8_t own, partner, pendMask; int mercy; bool resurrects; };
        Derived D[8];
        check(ALIEN_FIX_N == 8, "eight alien scripts were extracted");
        for (int k = 0; k < ALIEN_FIX_N; k++) {
            Derived d; d.own = 0; d.partner = 0; d.pendMask = 0; d.mercy = -1;
            d.resurrects = false;
            int o, p; bool h;

            // GATE: PSHM_B 446 ; PSHN_L own ; AND
            for (int i = 0; i + 2 < ALIEN_FIX[k].gateN; i++) {
                dec(ALIEN_FIX[k].gate[i], &o, &p, &h);
                if (o != OP_PSHM_B || p != PG_VAR_DEAD) continue;
                int o2, p2; bool h2; dec(ALIEN_FIX[k].gate[i+1], &o2, &p2, &h2);
                int o3, p3; bool h3; dec(ALIEN_FIX[k].gate[i+2], &o3, &p3, &h3);
                if (o2 == OP_PSHN_L && o3 == OP_OPER && p3 == OP_AND) {
                    d.own = (uint8_t)p2; break;
                }
            }
            // RESOLVE: the first PSHM_B 445 ; PSHN_L m ; AND names the mask;
            //          the first PSHM_B 447 ; PSHN_L b ; AND names the partner;
            //          PSHM_B 446 ; PSHM_B 447 ; NOT ; AND is the resurrection;
            //          PSHM_B 437 ; PSHN_L t ; GT is the mercy threshold.
            for (int i = 0; i + 2 < ALIEN_FIX[k].resN; i++) {
                dec(ALIEN_FIX[k].res[i], &o, &p, &h);
                int o2, p2; bool h2; dec(ALIEN_FIX[k].res[i+1], &o2, &p2, &h2);
                int o3, p3; bool h3; dec(ALIEN_FIX[k].res[i+2], &o3, &p3, &h3);
                if (o == OP_PSHM_B && o2 == OP_PSHN_L && o3 == OP_OPER) {
                    if (p == PG_VAR_PENDFLAG && p3 == OP_AND && !d.pendMask)
                        d.pendMask = (uint8_t)p2;
                    if (p == PG_VAR_PENDBIT && p3 == OP_AND && !d.partner)
                        d.partner = (uint8_t)p2;
                    if (p == 437 && p3 == OP_GT) d.mercy = p2;
                }
                if (i + 3 < ALIEN_FIX[k].resN && o == OP_PSHM_B && p == PG_VAR_DEAD &&
                    o2 == OP_PSHM_B && p2 == PG_VAR_PENDBIT && o3 == OP_OPER && p3 == OP_NOT) {
                    int o4, p4; bool h4; dec(ALIEN_FIX[k].res[i+3], &o4, &p4, &h4);
                    if (o4 == OP_OPER && p4 == OP_AND) d.resurrects = true;
                }
            }
            D[k] = d;

            char m[200];
            snprintf(m, sizeof(m), "%s: script names own=0x%02X partner=0x%02X",
                     ALIEN_FIX[k].field, d.own, d.partner);
            check(d.own != 0 && d.partner != 0, m);

            snprintf(m, sizeof(m), "%s: the pending flag is var[%d] bit 0x%02X",
                     ALIEN_FIX[k].field, PG_VAR_PENDFLAG, d.pendMask);
            check(d.pendMask == PG_PENDING_MASK, m);

            snprintf(m, sizeof(m), "%s: an unmatched kill RESURRECTS the pending one",
                     ALIEN_FIX[k].field);
            check(d.resurrects, m);

            snprintf(m, sizeof(m), "%s: the game's own mercy is var[437] > 24, not %d",
                     ALIEN_FIX[k].field, d.mercy);
            check(d.mercy == 24, m);

            // ...and THAT is what the model is checked against.
            const Propagator* mp = PgForField(ALIEN_FIX[k].field);
            snprintf(m, sizeof(m), "**%s: the model's bit 0x%02X / partner 0x%02X are the "
                                   "script's own 0x%02X / 0x%02X**",
                     ALIEN_FIX[k].field, mp ? mp->bit : 0, mp ? mp->partnerBit : 0,
                     d.own, d.partner);
            check(mp && mp->bit == d.own && mp->partnerBit == d.partner, m);
        }

        // The pairing CLOSES: every partner bit belongs to a script whose own
        // partner bit points back. Eight scripts, one involution, no orphans.
        for (int k = 0; k < 8; k++) {
            int mate = -1;
            for (int q = 0; q < 8; q++) if (D[q].own == D[k].partner) mate = q;
            char m[160];
            snprintf(m, sizeof(m), "%s's partner 0x%02X is a real script, and it points back",
                     ALIEN_FIX[k].field, D[k].partner);
            check(mate >= 0 && D[mate].partner == D[k].own, m);
            check(mate != k, "and nothing is its own partner");
        }
        // The eight own-bits cover 0xFF exactly, so var[446]==255 really is
        // "all eight down" and not "some subset plus a spare bit".
        {
            uint8_t cover = 0; int distinct = 0;
            for (int k = 0; k < 8; k++) { if (!(cover & D[k].own)) distinct++; cover |= D[k].own; }
            check(cover == PG_ALL_DEAD && distinct == 8,
                  "**the eight bits are distinct and cover 0xFF, which is the puzzle's exit**");
        }
    }

    {
        check(PgTableConsistent(),
              "**every Propagator's partner points back at it, agrees on the "
              "colour, and the eight bits cover 0xFF**");
        check(PG_COUNT == 8, "eight of them");

        // The bit pairs, exactly as the eight alien scripts test them.
        struct { const char* f; uint8_t own, partner; const char* colour; } B[] = {
            { "rgair1",   0x01, 0x80, "yellow" }, { "rgguest2", 0x80, 0x01, "yellow" },
            { "rgroad1",  0x02, 0x20, "green"  }, { "rghang2",  0x20, 0x02, "green"  },
            { "rgroad2",  0x04, 0x08, "red"    }, { "rgroad3",  0x08, 0x04, "red"    },
            { "rghang1",  0x10, 0x40, "purple" }, { "rgexit1",  0x40, 0x10, "purple" },
        };
        for (unsigned k = 0; k < sizeof(B)/sizeof(B[0]); k++) {
            const Propagator* p = PgForField(B[k].f);
            char m[160];
            snprintf(m, sizeof(m), "%s is bit 0x%02X, pairs with 0x%02X, %s",
                     B[k].f, B[k].own, B[k].partner, B[k].colour);
            check(p && p->bit == B[k].own && p->partnerBit == B[k].partner &&
                  strcmp(p->colour, B[k].colour) == 0, m);
        }
        // Four colours, two each -- never three of one.
        const char* cols[4] = { "yellow", "green", "red", "purple" };
        for (int c = 0; c < 4; c++) {
            int cnt = 0;
            for (int i = 0; i < PG_COUNT; i++) if (!strcmp(PG_LIST[i].colour, cols[c])) cnt++;
            char m[96]; snprintf(m, sizeof(m), "exactly two %s Propagators", cols[c]);
            check(cnt == 2, m);
        }
        // rgroad3's fightable one is alien02 -- alien01 there is the decoy.
        const Propagator* r3 = PgForField("rgroad3");
        check(r3 && strcmp(r3->entity, "alien02") == 0,
              "**rgroad3's Propagator is alien02** -- its alien01 is cutscene-only "
              "and announcing it would send the player at a phantom");
        check(strcmp(PG_DECOY_FIELD, "rgroad3") == 0 &&
              strcmp(PG_DECOY_ENTITY, "alien01") == 0, "and the decoy is named so it can be hidden");

        char buf[256];
        PgStatusLine(0xFF, 0, false, buf, sizeof(buf), true);
        checkStr(buf, "All eight are down. The lift is open.", "the finished state");
        PgStatusLine(0x00, 0, false, buf, sizeof(buf), true);
        checkStr(buf, "8 left. No kill is pending, so any one may be killed next.", "the opening state");
        // One yellow down and unmatched. ASKED FOR, the mod names the other
        // yellow and the room it is in, because that is the move that stops the
        // revive and he asked.
        PgStatusLine(0x01, 0x01, true, buf, sizeof(buf), true);
        checkStr(buf, "7 left. A yellow one is down and unmatched -- kill the other yellow "
                      "one, in the passenger cabin, next, or the first comes back.",
                 "**an unmatched kill names its match and its room WHEN ASKED**");
        // VOLUNTEERED, it names the rule and stops. Aaron, 2026-08-24: "We also
        // don't want to proactively inform the player where to find it's pair."
        PgStatusLine(0x01, 0x01, true, buf, sizeof(buf), false);
        checkStr(buf, "7 left. A yellow one is down and unmatched -- the next kill must be "
                      "the other yellow one, or the first comes back.",
                 "**and volunteers the RULE without the room** -- the hunt is his");
        check(strstr(buf, "passenger cabin") == nullptr,
              "**no location leaks into the unprompted line**");

        // ---- the catalog name, which replaces every arrival announcement ----
        {
            char nm[32];
            check(PgCatalogName("rgroad2", "alien01", nm, sizeof nm) &&
                  strcmp(nm, "Red Propagator") == 0, "rgroad2's alien01 is the Red Propagator");
            check(PgCatalogName("rghang1", "alien01", nm, sizeof nm) &&
                  strcmp(nm, "Purple Propagator") == 0, "rghang1's is the Purple one");
            check(PgCatalogName("rgair1", "alien01", nm, sizeof nm) &&
                  strcmp(nm, "Yellow Propagator") == 0, "rgair1's is the Yellow one");
            check(PgCatalogName("rghang2", "alien01", nm, sizeof nm) &&
                  strcmp(nm, "Green Propagator") == 0, "rghang2's is the Green one");
            check(PgCatalogName("rgroad3", "alien02", nm, sizeof nm) &&
                  strcmp(nm, "Red Propagator") == 0,
                  "**and in rgroad3 it is alien02 that gets the name**");
            check(!PgCatalogName("rgroad3", "alien01", nm, sizeof nm),
                  "**the decoy is never named**");
            check(!PgCatalogName("rgcock1", "alien01", nm, sizeof nm),
                  "a field with no Propagator names nothing");
            check(!PgCatalogName("bgmd1_4", "handle", nm, sizeof nm),
                  "and neither does an unrelated symbol");
            check(PgCatalogDrop("rgroad3", "alien01"), "the decoy is dropped");
            check(!PgCatalogDrop("rgroad3", "alien02"), "the real one is not");
            check(!PgCatalogDrop("rgroad2", "alien01"),
                  "**and alien01 elsewhere is emphatically not dropped** -- it is the "
                  "Propagator in six of the eight rooms");
            // Every colour a room can announce must round-trip: four colours,
            // eight rooms, and both members of a pair must say the same word.
            for (int i = 0; i < PG_COUNT; i++) {
                char a[32], b2[32];
                const Propagator* q = PgForBit(PG_LIST[i].partnerBit);
                check(PgCatalogName(PG_LIST[i].field, PG_LIST[i].entity, a, sizeof a),
                      "every Propagator has a catalog name");
                check(q && PgCatalogName(q->field, q->entity, b2, sizeof b2) &&
                      strcmp(a, b2) == 0,
                      "**and a pair announces the same colour in both rooms**");
            }
        }

        // ---- holding them still --------------------------------------------
        {
            const Propagator* road2 = PgForField("rgroad2");
            const Propagator* air1  = PgForField("rgair1");
            const Propagator* gst2  = PgForField("rgguest2");
            check(road2 && road2->freezable, "the corridor patroller is held");
            check(air1 && !air1->freezable,
                  "**the stationary one is not** -- there is nothing to hold, and a "
                  "write with no purpose is how the space rescue lost four builds");
            check(gst2 && !gst2->freezable,
                  "**and the cutscene one is not** -- its script ends in the MAPJUMP3 "
                  "out of the room, and freezing it would strand him there");
            int nFreeze = 0;
            for (int i = 0; i < PG_COUNT; i++) if (PG_LIST[i].freezable) nFreeze++;
            check(nFreeze == 5, "five of the eight move and are held");

            // HELD UNTIL HE REACHES IT AND SAYS SO. Aaron, after one caught
            // him at a doorway: "could we make it so the monster is held in
            // place until the player gets within 2 steps from the monster and
            // presses X / the Confirm key? That would prevent the player from
            // being chased at all."
            //
            // Arguments are (p, dead, posKnown, dist, holding, confirmHit, engaged).
            check(PgHoldDecide(road2, 0x00, true, 2000, false, false, false) == PG_HOLD_ON,
                  "**a Propagator across the room is held**");
            check(PgHoldDecide(road2, 0x00, true, 2000, true, false, false) == PG_HOLD_ON,
                  "and stays held");
            check(PgHoldDecide(road2, 0x00, true, 100, true, false, false) == PG_HOLD_ON,
                  "**standing on top of one does NOT release it** -- this is the "
                  "doorway case: distance alone cannot tell 'he walked up to me' "
                  "from 'he walked in'");
            check(PgHoldDecide(road2, 0x00, true, 100, true, true, false) == PG_HOLD_OFF,
                  "**reach AND Confirm together release it** -- he walks up to the "
                  "thing and starts the fight himself, and is never chased");
            check(PgHoldDecide(road2, 0x00, true, 2000, true, true, false) == PG_HOLD_ON,
                  "**Confirm from across the room does nothing** -- it is a key he "
                  "presses at scenery all day");
            check(PgHoldDecide(road2, 0x00, false, 0, true, true, false) == PG_HOLD_ON,
                  "and neither does Confirm when we cannot see how far away it is");
            check(PgHoldDecide(road2, 0x00, true, PG_REACH_UNITS, true, true, false) == PG_HOLD_OFF &&
                  PgHoldDecide(road2, 0x00, true, PG_REACH_UNITS + 1, true, true, false) == PG_HOLD_ON,
                  "the reach boundary is inclusive and it is where it says it is");
            // Once released it is his fight. A monster that re-froze after being
            // let go would be a monster that can never be fought.
            check(PgHoldDecide(road2, 0x00, true, 2000, false, false, true) == PG_HOLD_NONE,
                  "**nothing re-holds one he has already released**");
            check(PgHoldDecide(road2, 0x00, true, 100, true, true, true) == PG_HOLD_NONE,
                  "and Confirm on an engaged one is not a second release");
            // No measurement is no longer a reason to let go: the hold is not on
            // a timer any more, so an unmeasurable Propagator is simply held.
            check(PgHoldDecide(road2, 0x00, false, 0, false, false, false) == PG_HOLD_ON,
                  "**an unmeasurable Propagator is held, not freed** -- there is no "
                  "distance rule left for it to fall through");
            // Dead, or not ours: hands off.
            check(PgHoldDecide(road2, 0x04, true, 2000, false, false, false) == PG_HOLD_NONE,
                  "a dead Propagator is not held");
            check(PgHoldDecide(road2, 0x04, true, 2000, true, false, false) == PG_HOLD_DEAD,
                  "**and one that dies while held is let go SILENTLY** -- \"Released.\" "
                  "is the answer to a key he pressed, and a monster that just died "
                  "did not ask him anything");
            // v0.74.0: the stationary ones are GATED, not frozen. Not moving does
            // not make a Propagator harmless -- rgroad1's ran ISTOUCHING into
            // BATTLE 815 standing still and took him without a keypress.
            check(PgHoldDecide(air1, 0x00, true, 2000, false, false, false) == PG_HOLD_ON,
                  "**the stationary one is gated like every other** -- freezing and "
                  "consent are different problems, and this table had one column "
                  "for both until the 12:30 BAT");
            check(air1 != nullptr && !air1->freezable && air1->gate,
                  "and it says so in the table: nothing to freeze, everything to gate");
            {
                // v0.75.0: rgguest2 is gated too now. Aaron: "Let's try to freeze
                // the one in the passenger compartment as well... it is jarring the
                // way it works right now." It is still not FROZEN -- its script's
                // post-charge wait is an eight-frame count rather than a wait for
                // the move, so a pinned speed would stop the walk and the battle
                // would arrive anyway -- and it has no contact test to refuse. The
                // hold goes on the cutscene's first instruction instead
                // (PgHookedEvent); what the model owns is that this one takes part
                // in the reach cue and the Confirm release like every other.
                const Propagator* guest = PgForField("rgguest2");
                check(guest != nullptr && !guest->freezable && guest->gate,
                      "**rgguest2 is gated but not frozen** -- the two columns exist "
                      "precisely so this row can say that");
                check(PgHoldDecide(guest, 0x00, true, 2000, false, false, false) == PG_HOLD_ON,
                      "so the module does take part in it");
                check(PgHoldDecide(guest, 0x00, true, 100, true, true, false) == PG_HOLD_OFF,
                      "**and Confirm within reach releases it** -- which is what lets "
                      "its cutscene start");
            }
            check(PgHoldDecide(nullptr, 0x00, true, 2000, false, false, false) == PG_HOLD_NONE,
                  "and neither is a room with no Propagator in it");
            check(PgInReach(true, 100) && !PgInReach(true, 5000) && !PgInReach(false, 0),
                  "and 'within reach' needs a measurement to be true");
            check(PG_OFF_SPEED_CUR == 0x1FE && PG_OFF_SPEED_WALK == 0x200,
                  "the two speed words are where the engine's own step reads them");
        }

        // Standing in front of the match.
        const Propagator* g2 = PgForField("rgguest2");
        PgHereLine(g2, 0x01, 0x01, true, buf, sizeof(buf));
        checkStr(buf, "yellow Propagator. This is the match for the pending kill -- "
                      "killing it now finishes the pair.", "the go-ahead");
        // Standing in front of the WRONG one while a kill is pending.
        const Propagator* h1 = PgForField("rghang1");
        PgHereLine(h1, 0x01, 0x01, true, buf, sizeof(buf));
        checkStr(buf, "purple Propagator. Do NOT kill it yet: a yellow one is unmatched and "
                      "would come back. Its pair is in the exit passage.",
                 "**and the warning that is the whole puzzle**");
        PgHereLine(h1, 0x00, 0x00, false, buf, sizeof(buf));
        checkStr(buf, "purple Propagator. Its pair is in the exit passage.", "the quiet case");
        PgHereLine(h1, 0x10, 0x00, false, buf, sizeof(buf));
        checkStr(buf, "The purple Propagator here is already down.", "and a dead one says so");
        check(PgIsRagnarokField("rghang1") && !PgIsRagnarokField("eccway11"),
              "the Ragnarok test is by prefix");
        check((PG_ADDR_DEAD - 0x01CFE9B8u) == PG_VAR_DEAD &&
              (PG_ADDR_PENDBIT - 0x01CFE9B8u) == PG_VAR_PENDBIT &&
              (PG_ADDR_PENDFLAG - 0x01CFE9B8u) == PG_VAR_PENDFLAG,
              "the three variables resolve where the field variable block puts them");
    }

    std::printf(bad ? "disc3_models_compile: FAILED (%d bad)\n"
                    : "disc3_models_compile: OK (%d bad)\n", bad);
    return bad ? 1 : 0;
}
