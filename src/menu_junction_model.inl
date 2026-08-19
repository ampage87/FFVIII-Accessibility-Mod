// menu_junction_model.inl -- v0.23.0 (#82)
//
// The Junction screen's ANNOUNCEMENT LOGIC: pure functions of a JunctionView,
// no Win32, no SEH, no absolute memory. Same split as menu_magic_model.inl, for
// the same reason -- the wording is then testable without playing the game, and
// the file that touches memory only has to be right about addresses.
//
// PART OF menu_tts.cpp -- TEXTUAL INCLUDE, before menu_tts_junction.inl.
// Also compiled directly by tests/menu_sim.cpp.
//
// ---------------------------------------------------------------------------
// WHERE THIS COMES FROM
//
// Junction module: dispatch index 17 -> creator 0x004E2DC0 -> 0x004DA230;
// update 0x004DA9B0, draw 0x004E04F0, jump table 0x004DFC54, 74 states.
// Full write-up in docs/JUNCTION_MENU.md.
//
// The savemap layout is the headline: **`char + 0x5C` is a 19-byte array of
// junctioned spell ids**, address `0x01CFE144 + 152*charId + slot`, and the slot
// order is NOT what several public FF8 layout tables say -- ST-Atk (+0x66) comes
// BEFORE Elem-Def (+0x67..), proven at 0x00496A13.
// ---------------------------------------------------------------------------

#ifndef MENU_JUNCTION_MODEL_INCLUDED
#define MENU_JUNCTION_MODEL_INCLUDED

// ===========================================================================
// The state machine's steady states.
//
// These live in the model, not next to the memory reads, because they are the
// one class of constant a green offline test can still pin: which state number
// the poll is allowed to speak in.
//
// **JUNC_STATE_GRID_SLIDE is here to be excluded.** The first draft of the
// hook had the grid at 37; 37 is the slide-in animation at 0x004DB008, which
// walks +0x40 from 0 to 0x1000 and only then hands over. A poll that read the
// grid there would have spoken at most once per visit, on whichever frame it
// happened to land -- indistinguishable from a flaky hook, and impossible to
// diagnose from a BAT. The steady grid state is 52 (0x004DB29F), the handler
// that actually reads the D-pad against the row count.
// ===========================================================================
static const int JUNC_STATE_GRID       = 52;   // 0x34, the junction grid
static const int JUNC_STATE_GRID_SLIDE = 37;   // 0x25, animation -- NEVER speak here
static const int JUNC_STATE_MAGIC      = 59;   // 0x3B, choosing a magic
static const int JUNC_STATE_ABIL_SLOTS = 24;   // 0x18, the equipped slots panel
static const int JUNC_STATE_ABIL_LIST  = 28;   // 0x1C, the available-ability list

// ===========================================================================
// Junction slots, 0..18, in savemap order.
// ===========================================================================
enum {
    JSLOT_HP = 0, JSLOT_STR, JSLOT_VIT, JSLOT_MAG, JSLOT_SPR,
    JSLOT_SPD, JSLOT_EVA, JSLOT_HIT, JSLOT_LUCK,
    JSLOT_ELEM_ATK = 9, JSLOT_ST_ATK = 10,
    JSLOT_ELEM_DEF = 11,          // 11..14
    JSLOT_ST_DEF   = 15,          // 15..18
    JSLOT_COUNT    = 19
};

// The screen is a 4-column x 5-row grid and the cursor (0..19) is NOT the slot
// number -- it is an index into this 20-byte table at 0x00B88604. Reading the
// cursor as a slot would put Status-Attack's label on HP-J.
//
// Grid index 15 duplicates HP and is the visually blank cell; it is reported as
// blank rather than as a second HP-J.
static const signed char JUNC_GRID_TO_SLOT[20] = {
    /* col 0, status   */ 10, 15, 16, 17, 18,
    /* col 1, elemental*/  9, 11, 12, 13, 14,
    /* col 2           */  0,  1,  2,  3,  4,
    /* col 3           */ -1,  5,  6,  7,  8,
};

// ===========================================================================
// Elements and statuses.
//
// **These orderings are PROVEN, not the usual community list taken on trust.**
// kernel.bin's magic table (section 1, 57 entries of 60 bytes) carries an
// element bitfield at entry+0x22 and a status mask at entry+0x28, and every
// single bit is pinned by a spell whose own name states it:
//
//   Fire->bit0  Blizzard->bit1  Thunder->bit2  Quake->bit3  Bio->bit4
//   Aero->bit5  Water->bit6     Holy->bit7
//
//   Death->0  Bio->1(Poison)  Break->2(Petrify)  Blind->3  Silence->4
//   Berserk->5  Zombie->6  Sleep->7  Slow->8  Stop->9
//   Pain->1,3,4,10 (Poison/Darkness/Silence/Curse, which is exactly what Pain
//   does, fixing bit 10)  Confuse->11  Drain->12
//
// That matters because the on-screen labels are SPRITES, not text: unlike the
// Magic menu there is no in-game string to read back, so a wrong table here
// would be undetectable in play. Deriving it from the spell data instead of
// from a wiki is the difference between knowing and assuming.
// ===========================================================================
static const char* const JUNC_ELEMENTS[8] = {
    "Fire", "Ice", "Thunder", "Earth", "Poison", "Wind", "Water", "Holy"
};
static const char* const JUNC_STATUSES[13] = {
    "Death", "Poison", "Petrify", "Darkness", "Silence", "Berserk", "Zombie",
    "Sleep", "Slow", "Stop", "Curse", "Confuse", "Drain"
};

// menu_tts_status.inl reads the SAME arrays (+0x194 and +0x1A4) and had its own
// copies of these two tables. Two copies of a name table is exactly what put
// Float/Drain/Pain at the wrong spell ids in the Ability screen for months, so
// the Status screen now points at these. Its own comments record how it
// confirmed the orders in play -- Ice 0x02 / Thunder 0x04 / Water 0x40 on the
// attack bitmask, and Berserk at index 5 from a real junction -- which agrees
// with the kernel.bin derivation above, from a completely different direction.

// Stat-block offsets, from the junction-row descriptor table at 0x00B88618.
// HP is a u16; the rest are u8.
struct JuncStatDef { const char* name; unsigned short off; unsigned char isU16; };
static const JuncStatDef JUNC_STATS[9] = {
    { "HP",   0x0174, 1 },
    { "Str",  0x01BB, 0 },
    { "Vit",  0x01BC, 0 },
    { "Mag",  0x01BD, 0 },
    { "Spr",  0x01BE, 0 },
    { "Spd",  0x01BF, 0 },
    { "Eva",  0x01C1, 0 },
    { "Hit",  0x01C2, 0 },
    { "Luck", 0x01C0, 0 },
};

// Slot -> spoken name. The screen abbreviates ("Str-J"); spoken, the hyphen-J
// is noise, so the slot is named for what it does.
static const char* JuncSlotName(int slot)
{
    switch (slot) {
        case JSLOT_HP:   return "HP";
        case JSLOT_STR:  return "Strength";
        case JSLOT_VIT:  return "Vitality";
        case JSLOT_MAG:  return "Magic";
        case JSLOT_SPR:  return "Spirit";
        case JSLOT_SPD:  return "Speed";
        case JSLOT_EVA:  return "Evasion";
        case JSLOT_HIT:  return "Hit";
        case JSLOT_LUCK: return "Luck";
        case JSLOT_ELEM_ATK: return "Elemental attack";
        case JSLOT_ST_ATK:   return "Status attack";
        default: break;
    }
    if (slot >= JSLOT_ELEM_DEF && slot < JSLOT_ELEM_DEF + 4) return "Elemental defence";
    if (slot >= JSLOT_ST_DEF   && slot < JSLOT_ST_DEF   + 4) return "Status defence";
    return "Unknown";
}

// Which of the nine stat rows a slot corresponds to, or -1.
static int JuncStatIndexForSlot(int slot)
{
    return (slot >= JSLOT_HP && slot <= JSLOT_LUCK) ? slot : -1;
}

// ===========================================================================
// The view.
// ===========================================================================
struct JunctionView
{
    unsigned short state;         // module +0x10
    unsigned char  charId;
    const char*    charName;

    unsigned char  gridCursor;    // +0x276, 0..19
    unsigned char  groupCount;    // +0x277, usable rows in the cursor's column
    unsigned char  magicCursor;   // +0x26E, absolute 0..31
    unsigned int   eligibleMask;  // +0x24A, bit s = stock slot s does something here
    unsigned char  junction[JSLOT_COUNT];   // savemap char+0x5C
    unsigned int   unlockMask;    // 0x01D8B6A8 + 28*charId, bit r = row r usable

    MagicSlotView  magics[32];    // the character's stock, for the magic list

    // Two 464-byte stat blocks, already sampled: the baseline and the live
    // preview. The game's own arrow is a comparison of exactly these, so
    // "Str 42 to 68" costs two reads and no arithmetic.
    unsigned short statBefore[9]; // in JUNC_STATS order
    unsigned short statAfter[9];
    unsigned short elemDefBefore[8], elemDefAfter[8];   // raw, 800 = neutral
    unsigned char  stDefBefore[13], stDefAfter[13];     // raw, 100 = neutral
    unsigned char  elemAtkMask,   elemAtkPct;         // block +0x1C4 / +0x1C5, pct absolute
    unsigned char  elemAtkMaskBefore, elemAtkPctBefore;

    // Status attack. The mask is ASSEMBLED (see JuncAssembleStatusMask) and the
    // percentage is offset by 100 the way the defence rows are -- unlike the
    // elemental attack percentage, which is absolute. That asymmetry is the
    // game's, not a transcription slip: 0x004E12C3 uses +0x1C5 raw and
    // 0x004E0C7D does `sub eax, 0x64` on +0x1B6.
    unsigned short stAtkMask,       stAtkRaw;         // raw, 100 = nothing
    unsigned short stAtkMaskBefore, stAtkRawBefore;
};

// ===========================================================================
// The 13-bit junction status mask is NOT stored as 13 bits.
//
// **This is what made status attack silent in v0.23.0.** I read the mask as a
// u16 at block +0x1B4 and the percentage as a u16 at +0x1B6, and both were
// wrong in a way that produced silence rather than a wrong answer -- which is
// why it looked like the row was not hooked at all.
//
// The game assembles the mask itself, at 0x004E0FA0:
//
//     eax = block[0x1B4] & 0x7F           statuses 0..6
//     if (block[0x18C] & 0x0001) bit 7    Sleep
//     if (block[0x18C] & 0x0004) bit 8    Slow
//     if (block[0x18C] & 0x0008) bit 9    Stop
//     if (block[0x18C] & 0x0200) bit 10   Curse
//     if (block[0x18C] & 0x4000) bit 11   Confuse
//     if (block[0x18C] & 0x8000) bit 12   Drain
//
// which is FF8's real two-word status bitfield being folded into the 13 entries
// the junction screen shows. Sleep -- the single most likely thing to junction
// to ST-Atk -- lives entirely in the second word, so the old read returned zero
// for it.
//
// The bit numbering that comes out is the same one JUNC_STATUSES uses, and that
// is now confirmed from a third independent direction: kernel.bin's per-spell
// ST-Atk mask (+0x26) gives exactly one spell per bit, and each one is the
// spell of that name -- Sleep->7, Silence->4, Break->2 (Petrify), Death->0,
// Berserk->5, Zombie->6, Confuse->11, Drain->12, Slow->8, Stop->9, Blind->3
// (Darkness), Bio->1 (Poison), and Pain->1,3,4 (+Curse on defence).
// ===========================================================================
static unsigned short JuncAssembleStatusMask(unsigned char b1B4, unsigned int w18C)
{
    unsigned short m = (unsigned short)(b1B4 & 0x7F);
    if (w18C & 0x0001u) m |= 1u << 7;    // Sleep
    if (w18C & 0x0004u) m |= 1u << 8;    // Slow
    if (w18C & 0x0008u) m |= 1u << 9;    // Stop
    if (w18C & 0x0200u) m |= 1u << 10;   // Curse
    if (w18C & 0x4000u) m |= 1u << 11;   // Confuse
    if (w18C & 0x8000u) m |= 1u << 12;   // Drain
    return m;
}

static const int JUNC_STATUS_ATK_NEUTRAL = 100;
static int JuncStatusAtkPct(unsigned short raw) { return (int)raw - JUNC_STATUS_ATK_NEUTRAL; }

// ===========================================================================
// Raw -> percent.
//
// **The first draft of this halved the elemental value, and it was wrong.**
// menu_tts_status.inl reads the very same words and has for months, with the
// conversion validated in play: a magic-46 defence junction reads 66 % at status
// index 5, which only works if `pct = raw - neutral` with no division. Its
// comment states the scale outright -- 0 normal, 100 immune, above 100 absorb,
// below 0 weak -- and a resistance that read 50 % on the Junction screen and
// 100 % on the Status screen would have been a trust-destroying inconsistency
// that neither screen could have exposed on its own.
//
// Checking the sibling screen before shipping an inferred conversion is the
// same rule that eventually cracked Edea's House: when something is already
// decoded elsewhere in this codebase, that decode outranks a fresh inference.
// ===========================================================================
static void JuncAppendFwd(char* out, size_t n, const char* s)
{
    size_t l = strlen(out);
    if (l >= n - 1) return;
    snprintf(out + l, n - l, "%s", s);
}
static void JuncAppendIntFwd(char* out, size_t n, long v)
{
    char t[24]; snprintf(t, sizeof(t), "%ld", v);
    JuncAppendFwd(out, n, t);
}

static const int JUNC_ELEM_DEF_NEUTRAL   = 800;
static const int JUNC_STATUS_DEF_NEUTRAL = 100;

static int JuncElemDefPct(unsigned short raw)   { return (int)raw - JUNC_ELEM_DEF_NEUTRAL; }
static int JuncStatusDefPct(unsigned char raw)  { return (int)raw - JUNC_STATUS_DEF_NEUTRAL; }

// Shared phrasing, so Junction and Status describe the same number identically.
// Returns false when the value is neutral and nothing should be said.
static bool JuncResistPhrase(const char* name, int pct, bool allowAbsorb,
                             char* out, size_t n)
{
    if (pct == 0) return false;
    out[0] = '\0';
    JuncAppendFwd(out, n, name);
    if (pct >= 100 && allowAbsorb && pct > 100) {
        JuncAppendFwd(out, n, " absorbs "); JuncAppendIntFwd(out, n, pct - 100);
        JuncAppendFwd(out, n, " percent");
    } else if (pct >= 100) {
        JuncAppendFwd(out, n, " immune");
    } else if (pct > 0) {
        JuncAppendFwd(out, n, " resists "); JuncAppendIntFwd(out, n, pct);
        JuncAppendFwd(out, n, " percent");
    } else {
        JuncAppendFwd(out, n, " weak "); JuncAppendIntFwd(out, n, -pct);
        JuncAppendFwd(out, n, " percent");
    }
    return true;
}

// ---------------------------------------------------------------------------
static void JuncAppend(char* out, size_t n, const char* s)
{
    size_t l = strlen(out);
    if (l >= n - 1) return;
    snprintf(out + l, n - l, "%s", s);
}
static void JuncAppendInt(char* out, size_t n, long v)
{
    char t[24]; snprintf(t, sizeof(t), "%ld", v);
    JuncAppend(out, n, t);
}

// The slot under the grid cursor, or -1 for the blank cell.
static int JuncSlotAtCursor(const JunctionView& v)
{
    if (v.gridCursor >= 20) return -1;
    return JUNC_GRID_TO_SLOT[v.gridCursor];
}

// Is this slot usable?
//
// The mask at 0x01D8B6A8 + 28*charId is a bitmap of the JUNCTION ABILITIES the
// character's GFs grant, and **bit N is ability id N+1** -- HP-J is ability 1
// and sits at bit 0. That off-by-one is not a guess: the game's own gate at
// 0x004DE531 switches on the slot and tests
//
//   slot 0..8   1 << slot          HP-J(1)..Luck-J(9)          -> bits 0..8
//   slot 9      0x200              Elem-Atk-J(10)              -> bit 9
//   slot 10     0x400              ST-Atk-J(11)                -> bit 10
//   slot 11..14 0x6800             Elem-Def-J(12) / x2(14) / x4(15)
//   slot 15..18 0x19000            ST-Def-J(13)  / x2(16) / x4(17)
//
// and every one of those five constants lands on exactly the ability ids that
// unlock that row once you subtract one. Five independent agreements is a
// derivation; one would have been a coincidence.
//
// Being able to say "locked" matters more here than anywhere else on the
// screen: a sighted player sees a greyed row, and a blind player who is told
// only "Speed, empty" will keep trying to junction into a row the game will
// never accept.
static unsigned int JuncUnlockBitsForSlot(int slot)
{
    if (slot < 0) return 0;
    if (slot <= JSLOT_LUCK)     return 1u << slot;
    if (slot == JSLOT_ELEM_ATK) return 0x200u;
    if (slot == JSLOT_ST_ATK)   return 0x400u;
    if (slot <  JSLOT_ST_DEF)   return 0x6800u;
    if (slot <  JSLOT_COUNT)    return 0x19000u;
    return 0;
}

static bool JuncSlotUnlocked(const JunctionView& v, int slot)
{
    const unsigned int bits = JuncUnlockBitsForSlot(slot);
    return bits != 0 && (v.unlockMask & bits) != 0;
}

// How many defence rows of each kind exist. Elem-Def-J alone gives one row,
// x2 gives two, x4 gives four; ST-Def is the same shape with its own three
// abilities. Used to report the row the cursor is on as "3 of 4" rather than
// implying four rows always exist.
static int JuncDefRowCount(unsigned int mask, bool status)
{
    const unsigned int one = status ? (1u << 12) : (1u << 11);   // ST-Def-J(13) / Elem-Def-J(12)
    const unsigned int two = status ? (1u << 15) : (1u << 13);   // x2  (16 / 14)
    const unsigned int four= status ? (1u << 16) : (1u << 14);   // x4  (17 / 15)
    if (mask & four) return 4;
    if (mask & two)  return 2;
    if (mask & one)  return 1;
    return 0;
}

// ===========================================================================
// The grid line.
//
// "Strength, Curaga, 68"      -- slot, what is junctioned, the resulting stat
// "Strength, empty, 42"       -- nothing junctioned; the stat still matters
// "Strength, locked"          -- the GF ability for this row is not equipped
// "Elemental defence 1, Blizzaga, Ice 50 percent"
// "Status defence 2, empty"
//
// The resistance rows name only what is ACTUALLY THERE. Eight elements and
// thirteen statuses read out in full on every cursor move would be unusable,
// and a zero is not news -- so zero-valued entries are silence, and the whole
// list lives behind a hotkey instead.
// ===========================================================================
static void JuncAnnounceGrid(const JunctionView& v, char* out, size_t n)
{
    if (!out || n == 0) return;
    out[0] = '\0';

    const int slot = JuncSlotAtCursor(v);
    if (slot < 0) { JuncAppend(out, n, "blank"); return; }

    JuncAppend(out, n, JuncSlotName(slot));
    // The defence rows are numbered on screen; say which one, and out of how
    // many, because how many exist depends on which x2/x4 ability is equipped
    // and there is nothing else to tell the player where the column ends.
    if (slot >= JSLOT_ELEM_DEF && slot < JSLOT_ELEM_DEF + 4) {
        JuncAppend(out, n, " "); JuncAppendInt(out, n, slot - JSLOT_ELEM_DEF + 1);
        const int tot = JuncDefRowCount(v.unlockMask, false);
        if (tot > 1) { JuncAppend(out, n, " of "); JuncAppendInt(out, n, tot); }
    } else if (slot >= JSLOT_ST_DEF && slot < JSLOT_ST_DEF + 4) {
        JuncAppend(out, n, " "); JuncAppendInt(out, n, slot - JSLOT_ST_DEF + 1);
        const int tot = JuncDefRowCount(v.unlockMask, true);
        if (tot > 1) { JuncAppend(out, n, " of "); JuncAppendInt(out, n, tot); }
    }

    if (!JuncSlotUnlocked(v, slot)) { JuncAppend(out, n, ", locked"); return; }

    const unsigned id = v.junction[slot];
    JuncAppend(out, n, ", ");
    if (id == 0) JuncAppend(out, n, "empty");
    else {
        const char* nm = MagicSpellName(id);
        if (nm) JuncAppend(out, n, nm);
        else { JuncAppend(out, n, "magic "); JuncAppendInt(out, n, (long)id); }
    }

    // The consequence.
    const int row = JuncStatIndexForSlot(slot);
    if (row >= 0) {
        JuncAppend(out, n, ", ");
        JuncAppendInt(out, n, (long)v.statAfter[row]);
        return;
    }
    if (id == 0) return;   // an empty resistance slot has nothing to report

    if (slot == JSLOT_ELEM_ATK) {
        for (int e = 0; e < 8; e++)
            if (v.elemAtkMask & (1u << e)) {
                JuncAppend(out, n, ", "); JuncAppend(out, n, JUNC_ELEMENTS[e]);
                JuncAppend(out, n, " "); JuncAppendInt(out, n, (long)v.elemAtkPct);
                JuncAppend(out, n, " percent");
            }
        return;
    }
    if (slot == JSLOT_ST_ATK) {
        int said = 0;
        for (int s = 0; s < 13; s++)
            if (v.stAtkMask & (1u << s)) {
                JuncAppend(out, n, ", ");
                JuncAppend(out, n, JUNC_STATUSES[s]);
                said++;
            }
        if (said) {
            JuncAppend(out, n, " ");
            JuncAppendInt(out, n, (long)JuncStatusAtkPct(v.stAtkRaw));
            JuncAppend(out, n, " percent");
        }
        return;
    }
    char ph[96];
    if (slot >= JSLOT_ELEM_DEF && slot < JSLOT_ELEM_DEF + 4) {
        for (int e = 0; e < 8; e++)
            if (JuncResistPhrase(JUNC_ELEMENTS[e], JuncElemDefPct(v.elemDefAfter[e]), true, ph, sizeof(ph)))
                { JuncAppend(out, n, ", "); JuncAppend(out, n, ph); }
        return;
    }
    for (int s = 0; s < 13; s++)
        if (JuncResistPhrase(JUNC_STATUSES[s], JuncStatusDefPct(v.stDefAfter[s]), false, ph, sizeof(ph)))
            { JuncAppend(out, n, ", "); JuncAppend(out, n, ph); }
}

// ===========================================================================
// The magic list, while choosing what to junction.
//
// **This is the line the screen exists for.** The game keeps the baseline at
// one address and the live preview at another, and draws its up/down arrow by
// comparing them -- so the mod can say what a junction WOULD do before the
// player commits, which is the one thing a blind player otherwise cannot know.
//
// "Curaga, quantity 47, Strength 42 to 68"
// "Blizzaga, quantity 12, Ice 0 to 50 percent"
// "Cure, quantity 9, no change"
// ===========================================================================
// WHAT MOVES, IN BOTH DIRECTIONS.
//
// Aaron, v0.23.1 BAT: *"it is only announcing the one value when in fact two or
// more may change... the mod said Confuse 8% or similar, but neglected to
// mention the drop in the Stop status. A sighted player can see both effects.
// Make sure this fix applies to elements and other magic junctions as well
// where increasing one value decreases another."*
//
// **The attack rows were the offenders, and the cause was a design note I wrote
// in v0.23.1 and was wrong about**: "an attack row is a SET plus one
// percentage... naming the outgoing set as well would double the sentence for
// no gain". It is not no gain. Junctioning Confuse over Stop RAISES Confuse and
// DROPS Stop, and on screen both arrows are visible at once. Saying only the
// rise describes a trade as though it were a gift.
//
// The fix GENERALISES rather than special-cases. An attack row is turned into
// the same per-entry before/after table the defence rows already are -- an
// entry's percentage is `(mask has it) ? row percentage : 0` -- and then every
// row type goes through one collector and one emitter. There is no longer a
// code path that CAN report one side of a change.
//
// The old flat cap of four is gone with it, because it could truncate exactly
// the drop that was missing. Deltas are GROUPED by their (from, to) pair
// instead, which is the shape junction changes actually have: a whole set
// moving 0 -> N or M -> 0. Ten statuses arriving together become one clause
// naming ten statuses, not ten clauses -- and not four clauses and a shrug.
// ===========================================================================
struct JuncDeltaItem { const char* name; short from; short to; };

static int JuncCollectDeltas(const JunctionView& v, int slot,
                             JuncDeltaItem* out, int cap)
{
    int n = 0;
    if (slot == JSLOT_ELEM_ATK) {
        for (int e = 0; e < 8 && n < cap; e++) {
            const int a = (v.elemAtkMaskBefore & (1u << e)) ? (int)v.elemAtkPctBefore : 0;
            const int b = (v.elemAtkMask       & (1u << e)) ? (int)v.elemAtkPct       : 0;
            if (a == b) continue;
            out[n].name = JUNC_ELEMENTS[e]; out[n].from = (short)a; out[n].to = (short)b; n++;
        }
        return n;
    }
    if (slot == JSLOT_ST_ATK) {
        const int pa = JuncStatusAtkPct(v.stAtkRawBefore);
        const int pb = JuncStatusAtkPct(v.stAtkRaw);
        for (int s = 0; s < 13 && n < cap; s++) {
            const int a = (v.stAtkMaskBefore & (1u << s)) ? pa : 0;
            const int b = (v.stAtkMask       & (1u << s)) ? pb : 0;
            if (a == b) continue;
            out[n].name = JUNC_STATUSES[s]; out[n].from = (short)a; out[n].to = (short)b; n++;
        }
        return n;
    }
    // Defence rows. BOTH tables are walked whichever defence row the cursor is
    // on: a junction that moved the other one would otherwise go unmentioned,
    // and an unexpected change is worth more than a tidy sentence.
    for (int e = 0; e < 8 && n < cap; e++) {
        const int a = JuncElemDefPct(v.elemDefBefore[e]), b = JuncElemDefPct(v.elemDefAfter[e]);
        if (a == b) continue;
        out[n].name = JUNC_ELEMENTS[e]; out[n].from = (short)a; out[n].to = (short)b; n++;
    }
    for (int s = 0; s < 13 && n < cap; s++) {
        const int a = JuncStatusDefPct(v.stDefBefore[s]), b = JuncStatusDefPct(v.stDefAfter[s]);
        if (a == b) continue;
        out[n].name = JUNC_STATUSES[s]; out[n].from = (short)a; out[n].to = (short)b; n++;
    }
    return n;
}

// Emit the deltas, grouped by (from, to). Returns how many were spoken.
//
// The caps exist only so a pathological state cannot overflow the caller's
// buffer; grouping means a real junction never reaches them. Anything dropped
// is admitted with ", and more" -- a silent truncation would read as
// completeness, which is the failure this whole change is about.
static int JuncAppendDeltas(const JuncDeltaItem* d, int n, char* out, size_t cap)
{
    const int MAX_GROUPS = 4, MAX_NAMES = 8;
    bool used[24];
    for (int i = 0; i < 24; i++) used[i] = false;

    int spoken = 0, groups = 0, omitted = 0;
    for (int i = 0; i < n && i < 24; i++) {
        if (used[i]) continue;
        if (groups >= MAX_GROUPS) { omitted++; continue; }

        int named = 0;
        for (int j = i; j < n && j < 24; j++) {
            if (used[j] || d[j].from != d[i].from || d[j].to != d[i].to) continue;
            used[j] = true;
            if (named < MAX_NAMES) {
                JuncAppend(out, cap, ", ");
                JuncAppend(out, cap, d[j].name);
                named++; spoken++;
            } else {
                omitted++;
            }
        }
        JuncAppend(out, cap, " ");     JuncAppendInt(out, cap, (long)d[i].from);
        JuncAppend(out, cap, " to ");  JuncAppendInt(out, cap, (long)d[i].to);
        JuncAppend(out, cap, " percent");
        groups++;
    }
    if (omitted) JuncAppend(out, cap, ", and more");
    return spoken;
}

// ===========================================================================
static void JuncAnnounceMagicChoice(const JunctionView& v, char* out, size_t n)
{
    if (!out || n == 0) return;
    out[0] = '\0';

    const int mslot = v.magicCursor;
    if (mslot < 0 || mslot >= 32) { JuncAppend(out, n, "Empty"); return; }
    const unsigned id  = v.magics[mslot].id;
    const unsigned qty = v.magics[mslot].qty;
    if (id == 0 || qty == 0) { JuncAppend(out, n, "Empty"); return; }

    const char* nm = MagicSpellName(id);
    if (nm) JuncAppend(out, n, nm);
    else { JuncAppend(out, n, "Magic "); JuncAppendInt(out, n, (long)id); }
    JuncAppend(out, n, ", quantity ");
    JuncAppendInt(out, n, (long)qty);

    // The game itself works out, one stock slot at a time, whether a spell does
    // anything in the row being filled -- 0x004DE485 loops the 32 stock entries
    // calling 0x004C2E50(spellId, slot) and banks the answers as a 32-bit mask
    // at module +0x2C. On screen the ineligible entries are simply drawn dim.
    // Dim is invisible to a screen reader, and this is the Magic screen's
    // "cannot be cast here" problem again: without it the player picks a spell,
    // hears the same list back, and never learns why nothing happened.
    if ((v.eligibleMask & (1u << mslot)) == 0) {
        JuncAppend(out, n, ", no effect here");
        return;
    }

    const int slot = JuncSlotAtCursor(v);
    const int row  = JuncStatIndexForSlot(slot);
    int said = 0;

    if (row >= 0) {
        if (v.statBefore[row] != v.statAfter[row]) {
            JuncAppend(out, n, ", "); JuncAppend(out, n, JUNC_STATS[row].name);
            JuncAppend(out, n, " "); JuncAppendInt(out, n, (long)v.statBefore[row]);
            JuncAppend(out, n, " to "); JuncAppendInt(out, n, (long)v.statAfter[row]);
            said++;
        }
    } else {
        // Everything that is not a plain stat row is a TABLE, and a junction
        // rewrites the whole table: the outgoing spell's entries fall as the
        // incoming spell's rise. Both halves are the comparison.
        JuncDeltaItem d[24];
        const int nd = JuncCollectDeltas(v, slot, d, 24);
        said = JuncAppendDeltas(d, nd, out, n);
    }
    if (!said) JuncAppend(out, n, ", no change");
}

// ===========================================================================
// The header spoken on arriving in the magic list.
//
// **Aaron read "no effect here" on every spell as a bug, and it was not one.**
// Only thirteen spells in the whole game carry a status-attack value -- one per
// junctionable status -- so on ST-Atk almost everything a player is holding
// genuinely does nothing, and hearing that spell after spell sounds exactly
// like a hook that has given up.
//
// Saying it once, up front, turns the same fact into information: you are told
// the row is unfillable from your stock instead of discovering it thirty-two
// times. The per-spell qualifier stays, for the mixed case where some do work.
// ===========================================================================
// The grid's arrival header. It names the character because the grid is the one
// screen whose every line differs per character and none of them say whose it is
// -- and because L1/R1 swaps the character without leaving the screen.
static void JuncGridHeader(const JunctionView& v, char* out, size_t n)
{
    if (!out || n == 0) return;
    out[0] = '\0';
    JuncAppend(out, n, "Junction");
    if (v.charName && v.charName[0]) { JuncAppend(out, n, ", "); JuncAppend(out, n, v.charName); }
}

static void JuncMagicHeader(const JunctionView& v, char* out, size_t n)
{
    if (!out || n == 0) return;
    out[0] = '\0';
    const int slot = JuncSlotAtCursor(v);
    JuncAppend(out, n, "Choose magic for ");
    JuncAppend(out, n, (slot >= 0) ? JuncSlotName(slot) : "this slot");
    if (slot >= JSLOT_ELEM_DEF && slot < JSLOT_ELEM_DEF + 4) {
        JuncAppend(out, n, " "); JuncAppendInt(out, n, slot - JSLOT_ELEM_DEF + 1);
    } else if (slot >= JSLOT_ST_DEF && slot < JSLOT_ST_DEF + 4) {
        JuncAppend(out, n, " "); JuncAppendInt(out, n, slot - JSLOT_ST_DEF + 1);
    }

    // Anything held but ineligible? Count what is actually in stock so an empty
    // inventory is not reported as an unfillable row.
    int held = 0;
    for (int i = 0; i < 32; i++)
        if (v.magics[i].id && v.magics[i].qty) held++;
    if (held > 0 && (v.eligibleMask & 0xFFFFFFFFu) == 0)
        JuncAppend(out, n, ". None of your magic affects this row");
}

// ===========================================================================
// The hotkey readouts. Full pictures, on demand, never automatic.
// ===========================================================================
static void JuncAnnounceStats(const JunctionView& v, char* out, size_t n)
{
    if (!out || n == 0) return;
    out[0] = '\0';
    if (v.charName && v.charName[0]) { JuncAppend(out, n, v.charName); JuncAppend(out, n, ". "); }
    for (int i = 0; i < 9; i++) {
        if (i) JuncAppend(out, n, ", ");
        JuncAppend(out, n, JUNC_STATS[i].name);
        JuncAppend(out, n, " ");
        JuncAppendInt(out, n, (long)v.statAfter[i]);
    }
}

// Elements first, then statuses; non-zero only. "Nothing" when a category is
// entirely neutral -- which is itself the answer, and a short one.
static void JuncAnnounceResistances(const JunctionView& v, char* out, size_t n)
{
    if (!out || n == 0) return;
    out[0] = '\0';
    char ph[96];
    JuncAppend(out, n, "Elemental. ");
    int said = 0;
    for (int e = 0; e < 8; e++)
        if (JuncResistPhrase(JUNC_ELEMENTS[e], JuncElemDefPct(v.elemDefAfter[e]), true, ph, sizeof(ph)))
            { if (said++) JuncAppend(out, n, ", "); JuncAppend(out, n, ph); }
    if (!said) JuncAppend(out, n, "no resistances or weaknesses");
    JuncAppend(out, n, ". Status. ");
    said = 0;
    for (int s = 0; s < 13; s++)
        if (JuncResistPhrase(JUNC_STATUSES[s], JuncStatusDefPct(v.stDefAfter[s]), false, ph, sizeof(ph)))
            { if (said++) JuncAppend(out, n, ", "); JuncAppend(out, n, ph); }
    if (!said) JuncAppend(out, n, "no resistances or weaknesses");
}

// ===========================================================================
// NUMBER-KEY READOUTS, mirroring the Status screen so there is one mapping to
// learn rather than two. Aaron: *"Let's use number keys 0-9 ... like we are
// doing on the Status screen. Both S and E are used by FF8 itself."*
//
//   0       character: name, level, HP        (Status key 0)
//   2..7    Str, Vit, Mag, Spr, Spd, Luck     (Status keys 2..7, same order)
//   8       Evade and Hit                     (Status key 8)
//   1       elemental and status ATTACK       (Status page-1 key 1 is elem-atk)
//   9       elemental and status DEFENCE
//
// 0 and 2..8 are identical to what the Status screen already speaks. 1 and 9
// are the two that do not collide, and they split the resistance picture into
// what you inflict and what you resist.
// ===========================================================================
static void JuncAnnounceAttack(const JunctionView& v, char* out, size_t n)
{
    if (!out || n == 0) return;
    out[0] = '\0';
    int said = 0;
    for (int e = 0; e < 8; e++)
        if (v.elemAtkMask & (1u << e)) {
            JuncAppend(out, n, said++ ? ", " : "Elemental attack. ");
            JuncAppend(out, n, JUNC_ELEMENTS[e]); JuncAppend(out, n, " ");
            JuncAppendInt(out, n, (long)v.elemAtkPct); JuncAppend(out, n, " percent");
        }
    if (!said) JuncAppend(out, n, "No elemental attack");
    JuncAppend(out, n, ". ");
    said = 0;
    for (int s = 0; s < 13; s++)
        if (v.stAtkMask & (1u << s)) {
            JuncAppend(out, n, said++ ? ", " : "Status attack. ");
            JuncAppend(out, n, JUNC_STATUSES[s]);
        }
    if (said) {
        JuncAppend(out, n, " ");
        JuncAppendInt(out, n, (long)JuncStatusAtkPct(v.stAtkRaw));
        JuncAppend(out, n, " percent");
    } else {
        JuncAppend(out, n, "No status attack");
    }
}

// Key 0 and keys 2..8, in the Status screen's own words and order.
static void JuncAnnounceStatKey(const JunctionView& v, int key, char* out, size_t n)
{
    if (!out || n == 0) return;
    out[0] = '\0';
    if (key == 0) {
        if (v.charName && v.charName[0]) { JuncAppend(out, n, v.charName); JuncAppend(out, n, ", "); }
        JuncAppend(out, n, "HP "); JuncAppendInt(out, n, (long)v.statAfter[0]);
        return;
    }
    if (key == 8) {
        JuncAppend(out, n, "Evade "); JuncAppendInt(out, n, (long)v.statAfter[6]);
        JuncAppend(out, n, ", Hit ");  JuncAppendInt(out, n, (long)v.statAfter[7]);
        return;
    }
    static const struct { int key; const char* label; int row; } K[] = {
        { 2, "Strength", 1 }, { 3, "Vitality", 2 }, { 4, "Magic", 3 },
        { 5, "Spirit",   4 }, { 6, "Speed",    5 }, { 7, "Luck",  8 },
    };
    for (unsigned i = 0; i < sizeof(K)/sizeof(K[0]); i++)
        if (K[i].key == key) {
            JuncAppend(out, n, K[i].label); JuncAppend(out, n, " ");
            JuncAppendInt(out, n, (long)v.statAfter[K[i].row]);
            return;
        }
}

#endif // MENU_JUNCTION_MODEL_INCLUDED
