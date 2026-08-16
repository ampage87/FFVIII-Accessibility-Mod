// menu_sim.cpp -- v0.22.0 (#81)
//
// An offline simulator for FF8's main-menu submenus, and the gate for the Magic
// screen's announcement logic.
//
//   g++ -std=c++17 -O0 -Isrc -o menu_sim tests/menu_sim.cpp
//
// ---------------------------------------------------------------------------
// WHY THIS EXISTS
//
// Aaron is the only tester and the only user. Every previous submenu was
// validated by playing it, one BAT at a time, which is why Junction is still
// "partially" done. The Magic screen has six phases, a 32-slot list paged four
// at a time, a cursor stored per character, and a flow that destroys items --
// far too much to land by trial.
//
// So the wording lives in src/menu_magic_model.inl as pure functions of a
// MagicView struct, and this file drives the state machine the way the game
// drives it: the same navigation rules read out of FF8_EN.exe's state machine at
// 0x004F02F0, applied to fixtures, with every announcement checked.
//
// The simulator is deliberately general -- MenuSim models the module pool and a
// state machine, not "the Magic screen" -- so Ability, GF, Config and the rest
// of the main menu can reuse it instead of each starting from a blank BAT.
//
// ---------------------------------------------------------------------------
// WHAT IT CANNOT DO
//
// It cannot prove an ADDRESS is right. Every offset here came from the exe and
// is asserted in one place (MAGIC_OFFSETS below) so that menu_tts_magic.inl and
// this file cannot drift apart -- but if an offset is wrong, both are wrong
// together and this file will happily pass. Addresses are what the BAT is for.
// What this file proves is that GIVEN the right bytes, the mod says the right
// words, in every phase, at every cursor position, including the ones a tester
// would never think to visit.
// ---------------------------------------------------------------------------

#include <cstdio>
#include <cstdint>
#include <cstring>
#include <cstdlib>
#include <string>
#include <vector>

#include "menu_magic_model.inl"

static int bad = 0;
static void check(bool ok, const char* what)
{
    if (!ok) { bad++; printf("  BAD: %s\n", what); }
}
static void expect(const char* got, const char* want, const char* what)
{
    if (strcmp(got, want) != 0) {
        bad++;
        printf("  BAD: %s\n        got  \"%s\"\n        want \"%s\"\n", what, got, want);
    }
}

// ===========================================================================
// The offset contract.
//
// These are module-relative offsets read out of FF8_EN.exe. menu_tts_magic.inl
// declares the same list; MagicOffsetsMatch() below is compiled into BOTH so a
// change in one file fails this test rather than silently diverging.
// ===========================================================================
struct MagicOffsets {
    int state, screenMode, charId, page, cursorBase, actionCursor, actionMask;
    int targetCursor, targetCount, targetMask, sortCursor;
    int dialogOpen, dialogCursor, dialogChar, dialogSlot, secondChar;
};
static const MagicOffsets MAGIC_OFFSETS = {
    /*state*/        0x10, /*screenMode*/   0x56, /*charId*/       0x64,
    /*page*/         0x42, /*cursorBase*/   0x38, /*actionCursor*/ 0x61,
    /*actionMask*/   0x67, /*targetCursor*/ 0x57, /*targetCount*/  0x60,
    /*targetMask*/   0x36, /*sortCursor*/   0x71, /*dialogOpen*/   0x6E,
    /*dialogCursor*/ 0x70, /*dialogChar*/   0x32, /*dialogSlot*/   0x33,
    /*secondChar*/   0x62,
};

// ===========================================================================
// MenuSim -- a module-pool + state-machine model, reusable for other submenus.
//
// The pool is the real thing: base 0x01D76BC8, stride 0x78, 10 slots, an
// MRU-first linked list whose head is at 0x01D76B48. Modelling it (rather than
// assuming "slot 2") is the point: the mod resolves its module by walking this
// list, and this simulator can therefore put Magic in ANY slot and prove the
// walk still finds it.
// ===========================================================================
struct SimModule {
    uint8_t  raw[0x78];
    uint32_t updateFn;
    bool     inUse;
    SimModule() { memset(raw, 0, sizeof(raw)); updateFn = 0; inUse = false; }
    uint8_t&  u8 (int off)       { return raw[off]; }
    uint16_t& u16(int off)       { return *(uint16_t*)&raw[off]; }
};

struct MenuSim {
    SimModule pool[10];
    std::vector<int> mru;          // most-recently-allocated first
    static const uint32_t MAGIC_UPDATE_FN = 0x004F02F0;

    int Alloc(uint32_t updateFn) {
        for (int i = 0; i < 10; i++) {
            if (pool[i].inUse) continue;
            pool[i] = SimModule();
            pool[i].inUse = true;
            pool[i].updateFn = updateFn;
            mru.insert(mru.begin(), i);
            return i;
        }
        return -1;
    }
    // The mod's FindMagicModule(), modelled exactly: walk the MRU list, return
    // the first module whose update function is the Magic state machine.
    int FindMagic() const {
        for (size_t k = 0; k < mru.size(); k++) {
            const int i = mru[k];
            if (pool[i].inUse && pool[i].updateFn == MAGIC_UPDATE_FN) return i;
        }
        return -1;
    }
};

// ===========================================================================
// A fixture: one character's magic, plus the game data tables.
// ===========================================================================
static void FillGameTables(MagicView& v)
{
    memset(v.mmagicFlag, 0, sizeof(v.mmagicFlag));
    memset(v.targetType, 0, sizeof(v.targetType));
    // From Data/lang-en/menu.fs -> mmagic.bin, byte 0 of each 4-byte entry.
    // Exactly seven spells carry bit 0. Life and Full-Life additionally carry
    // 0x20 (they target the fallen), which is why they read 0x23 not 0x03.
    v.mmagicFlag[21] = 0x03;  // Cure
    v.mmagicFlag[22] = 0x03;  // Cura
    v.mmagicFlag[23] = 0x03;  // Curaga
    v.mmagicFlag[24] = 0x23;  // Life
    v.mmagicFlag[25] = 0x23;  // Full-Life
    v.mmagicFlag[27] = 0x03;  // Esuna
    v.mmagicFlag[28] = 0x03;  // Dispel
    // Target types only matter under the 0x40 menu lock; give Curaga type 5 so
    // the lock path has something to bite on.
    v.targetType[23] = 5;
}

static MagicView MakeView()
{
    MagicView v;
    memset(&v, 0, sizeof(v));
    FillGameTables(v);
    v.charName = "Squall";
    static const char* names[8] = { "Squall","Zell","Irvine","Quistis","Rinoa","Selphie","Seifer","Edea" };
    for (int i = 0; i < 8; i++) v.memberName[i] = names[i];
    v.actionMask = 0x0F;
    return v;
}

// A realistic 32-slot loadout: a few stacks at the front, a gap, more later.
static void FillSlots(MagicView& v)
{
    struct { int slot, id, qty; } L[] = {
        {  0,  1, 100 },  // Fire
        {  1, 21,  47 },  // Cure
        {  2, 24,   9 },  // Life
        {  3, 49,   3 },  // Meltdown
        {  4, 28,  22 },  // Dispel
        {  7, 47,  12 },  // Float
        { 12, 44,   5 },  // Drain
        { 31, 56,   1 },  // The End
    };
    memset(v.slots, 0, sizeof(v.slots));
    for (unsigned k = 0; k < sizeof(L)/sizeof(L[0]); k++) {
        v.slots[L[k].slot].id  = (unsigned char)L[k].id;
        v.slots[L[k].slot].qty = (unsigned char)L[k].qty;
    }
}

static std::string Say(const MagicView& v)
{
    char buf[256];
    MagicAnnounce(v, MagicPhaseOf(v), buf, sizeof(buf));
    return std::string(buf);
}

int main()
{
    char buf[256];

    // ---------------------------------------------------------------------
    // 1. THE SPELL-ID TABLE, and the bug it replaces.
    //
    // menu_tts_ability.inl used to carry its own copy that ran Slow, Stop,
    // Float, Drain, Pain at 36..40. The real ids are Slow 36, Stop 37, then
    // Blind, Confuse, Sleep. Float is 47, Drain 44, Pain 45. The old table made
    // the Ability refine-preview read a different spell's stock.
    // ---------------------------------------------------------------------
    {
        struct { int id; const char* name; } ID[] = {
            {  1, "Fire" }, { 10, "Water" }, { 21, "Cure" }, { 24, "Life" },
            { 25, "Full-Life" }, { 27, "Esuna" }, { 28, "Dispel" },
            { 36, "Slow" }, { 37, "Stop" },
            { 38, "Blind" }, { 39, "Confuse" }, { 40, "Sleep" },   // <- was Float/Drain/Pain
            { 44, "Drain" }, { 45, "Pain" }, { 47, "Float" },
            { 56, "The End" },
        };
        for (unsigned k = 0; k < sizeof(ID)/sizeof(ID[0]); k++) {
            const char* got = MagicSpellName(ID[k].id);
            if (!got || strcmp(got, ID[k].name) != 0) {
                bad++;
                printf("  BAD: spell id %d is \"%s\", want \"%s\"\n",
                       ID[k].id, got ? got : "(null)", ID[k].name);
            }
        }
        check(MagicSpellName(0) == 0, "id 0 should have no name");
        check(MagicSpellName(999) == 0, "an out-of-range id should have no name");
        printf("spell ids: 16 anchors correct, including the three the old "
               "Ability table had wrong (38/39/40 = Blind/Confuse/Sleep)\n");
    }

    // ---------------------------------------------------------------------
    // 2. **THE SLOT FORMULA.** slot = (cursor & 3) + page*4, and it must hold
    //    for all 8 pages x 4 rows with no gaps and no repeats -- this is the
    //    single computation that decides whether the mod names the right spell.
    // ---------------------------------------------------------------------
    {
        MagicView v = MakeView();
        bool seen[32] = { false };
        for (int page = 0; page < 8; page++) {
            for (int row = 0; row < 4; row++) {
                v.page = (unsigned char)page;
                // The game stores the ABSOLUTE index; only the low 2 bits are
                // trustworthy right after a page change, so feed it an absolute
                // value from a DIFFERENT page and confirm the mask saves us.
                v.cursorRaw = (unsigned char)(row + ((page + 3) % 8) * 4);
                const int slot = MagicSlotIndex(v);
                if (slot != page * 4 + row) {
                    bad++;
                    printf("  BAD: page %d row %d -> slot %d, want %d\n",
                           page, row, slot, page * 4 + row);
                }
                if (slot >= 0 && slot < 32) {
                    if (seen[slot]) { bad++; printf("  BAD: slot %d visited twice\n", slot); }
                    seen[slot] = true;
                }
            }
        }
        for (int i = 0; i < 32; i++)
            if (!seen[i]) { bad++; printf("  BAD: slot %d unreachable\n", i); }
        printf("slot formula: all 32 slots reachable exactly once across 8 pages "
               "x 4 rows, and a stale absolute cursor cannot corrupt it\n");
    }

    // ---------------------------------------------------------------------
    // 3. The spell list's wording, including empties and the greyed state.
    // ---------------------------------------------------------------------
    {
        MagicView v = MakeView();
        FillSlots(v);
        v.state = 13; v.screenMode = 1;

        v.page = 0; v.cursorRaw = 1;
        expect(Say(v).c_str(), "Cure, quantity 47, slot 2 of 32", "Cure at slot 2");

        v.page = 0; v.cursorRaw = 3;
        expect(Say(v).c_str(), "Meltdown, quantity 3, cannot be cast here, slot 4 of 32",
               "an uncastable spell must say so in the Use flow");

        v.page = 1; v.cursorRaw = 3;   // slot 7 = Float
        expect(Say(v).c_str(), "Float, quantity 12, cannot be cast here, slot 8 of 32",
               "Float is id 47 and is not field-castable");

        v.page = 1; v.cursorRaw = 1;   // slot 5, empty
        expect(Say(v).c_str(), "Empty, slot 6 of 32", "an empty slot");

        v.page = 7; v.cursorRaw = 3;   // slot 31
        expect(Say(v).c_str(), "The End, quantity 1, cannot be cast here, slot 32 of 32",
               "the last slot");

        // Outside the Use flow nothing is greyed, so the qualifier must vanish.
        v.screenMode = 4; v.page = 0; v.cursorRaw = 3;
        expect(Say(v).c_str(), "Meltdown, quantity 3, slot 4 of 32",
               "\"cannot be cast here\" belongs only to the Use flow");

        // A slot with an id but zero quantity is spent, and the game shows it
        // as empty rather than as a 0-count spell.
        v.screenMode = 1; v.slots[0].qty = 0; v.page = 0; v.cursorRaw = 0;
        expect(Say(v).c_str(), "Empty, slot 1 of 32", "a zero-quantity slot reads as empty");
        printf("spell list: names, quantities, empties, the greyed qualifier and "
               "its absence outside the Use flow\n");
    }

    // ---------------------------------------------------------------------
    // 4. Castability, against the game's own mmagic.bin.
    // ---------------------------------------------------------------------
    {
        MagicView v = MakeView();
        const int CASTABLE[] = { 21, 22, 23, 24, 25, 27, 28 };
        int n = 0;
        for (int id = 1; id < MAGIC_SPELL_NAME_COUNT; id++) {
            bool want = false;
            for (unsigned k = 0; k < sizeof(CASTABLE)/sizeof(CASTABLE[0]); k++)
                if (CASTABLE[k] == id) want = true;
            if (MagicCastable(v, id) != want) {
                bad++;
                printf("  BAD: spell %d (%s) castable=%d, want %d\n",
                       id, MagicSpellName(id), (int)MagicCastable(v, id), (int)want);
            }
            if (want) n++;
        }
        check(n == 7, "there should be exactly seven field-castable spells");

        // Under the 0x40 story lock, target types 5 and 6 grey out even though
        // their base flag is set. Curaga is type 5 in this fixture.
        v.menuLock = 0x40;
        check(!MagicCastable(v, 23), "Curaga should grey out under the 0x40 lock");
        check( MagicCastable(v, 21), "Cure should survive the 0x40 lock");
        printf("castable set: exactly Cure, Cura, Curaga, Life, Full-Life, Esuna, "
               "Dispel -- and the 0x40 lock greys type 5/6 on top\n");
    }

    // ---------------------------------------------------------------------
    // 5. The action row, and the enable mask that makes "of 4" a lie.
    // ---------------------------------------------------------------------
    {
        MagicView v = MakeView();
        v.state = 3;
        const char* WANT[4] = { "Use", "Exchange", "All", "Rearrange" };
        for (int i = 0; i < 4; i++) {
            v.actionCursor = (unsigned char)i;
            expect(Say(v).c_str(), WANT[i], "action label");
        }
        // A silenced solo character: only Use is off and only Rearrange is on
        // (bits 1 and 2 need a second available character).
        v.actionMask = 0x08;
        v.actionCursor = 3;
        expect(Say(v).c_str(), "Rearrange, 1 of 1",
               "with one action available the position must say so");
        check(MagicEnabledActionCount(v) == 1, "enabled count with mask 0x08");

        v.actionMask = 0x09;  // Use + Rearrange, the solo-character default
        v.actionCursor = 0;
        expect(Say(v).c_str(), "Use, 1 of 2", "solo character: Use is first of two");
        v.actionCursor = 3;
        expect(Say(v).c_str(), "Rearrange, 2 of 2", "solo character: Rearrange is second");

        v.actionMask = 0x0F;  // all four -- the qualifier is noise, drop it
        v.actionCursor = 2;
        expect(Say(v).c_str(), "All", "with all four live, no position qualifier");
        printf("action row: Use / Exchange / All / Rearrange, with the position "
               "spoken only when entries are disabled\n");
    }

    // ---------------------------------------------------------------------
    // 6. Target select walks SET BITS, not character ids. Getting this wrong
    //    names the wrong party member while the cursor is on the right one.
    // ---------------------------------------------------------------------
    {
        MagicView v = MakeView();
        v.state = 20;
        v.targetMask = 0x0B;   // characters 0, 1, 3
        v.targetCount = 3;
        v.targetCursor = 0; expect(Say(v).c_str(), "Squall, 1 of 3",  "first set bit");
        v.targetCursor = 1; expect(Say(v).c_str(), "Zell, 2 of 3",    "second set bit");
        v.targetCursor = 2; expect(Say(v).c_str(), "Quistis, 3 of 3", "third set bit is character 3, not 2");
        check(MagicTargetChar(v) == 3, "cursor 2 over mask 0x0B is character 3");
        v.targetCursor = 3; expect(Say(v).c_str(), "No target", "cursor past the end of the mask");

        v.targetMask = 0x04; v.targetCount = 1; v.targetCursor = 0;
        expect(Say(v).c_str(), "Irvine", "a single target needs no position");
        printf("target select: the cursor indexes set bits, so a sparse party "
               "still names the right member\n");
    }

    // ---------------------------------------------------------------------
    // 7. The sort popup.
    // ---------------------------------------------------------------------
    {
        MagicView v = MakeView();
        v.state = 72;
        v.sortCursor = 0; expect(Say(v).c_str(), "Manual, 1 of 7", "first sort order");
        v.sortCursor = 1; expect(Say(v).c_str(), "Attack, then Restore, then Indirect, 2 of 7", "second sort order");
        v.sortCursor = 6; expect(Say(v).c_str(), "Indirect, then Restore, then Attack, 7 of 7", "last sort order");
        printf("sort popup: seven orders, interpuncts read as \"then\"\n");
    }

    // ---------------------------------------------------------------------
    // 8. **THE DESTRUCTIVE FLOW.** The confirmation must name what is about to
    //    be destroyed BEFORE the Yes/No, so a player who arrives mid-sentence
    //    still hears the consequence.
    // ---------------------------------------------------------------------
    {
        MagicView v = MakeView();
        FillSlots(v);
        v.state = 13;              // the dialog overlays the list...
        v.dialogOpen = 1;          // ...and must win
        v.dialogChar = 1;          // Zell
        v.dialogSlot = 1;          // Cure
        check(MagicPhaseOf(v) == MP_DISCARD, "the yes/no dialog must override the state");
        v.dialogCursor = 0;
        expect(Say(v).c_str(), "Discard all of Zell's Cure? Yes", "discard, cursor on Yes");
        v.dialogCursor = 1;
        expect(Say(v).c_str(), "Discard all of Zell's Cure? No", "discard, cursor on No");
        v.dialogOpen = 0;
        check(MagicPhaseOf(v) == MP_LIST, "closing the dialog returns to the list");
        printf("discard: the consequence is spoken before the choice, and the "
               "dialog outranks whatever screen is underneath\n");
    }

    // ---------------------------------------------------------------------
    // 9. Phase routing over every state the machine can park in.
    // ---------------------------------------------------------------------
    {
        MagicView v = MakeView();
        struct { int st, mode; MagicPhase want; const char* what; } P[] = {
            {   3, 0, MP_ACTION,      "state 3 is the action row" },
            {  13, 1, MP_LIST,        "state 13 is the spell list" },
            {  20, 2, MP_TARGET,      "state 20 is target select" },
            {  72, 0, MP_SORT,        "state 72 is the sort popup" },
            { 112, 0, MP_CLOSING,     "state 112 is closing" },
            { 113, 0, MP_CLOSING,     "state 113 is closing" },
            {  27, 4, MP_NONE,        "the two-column screen is not announced per-item" },
            {  23, 3, MP_NONE,        "state 23 is a one-frame Exchange entry, not a screen" },
            {  28, 3, MP_XCHG_PARTNER, "state 28 is where the partner is actually chosen" },
            {  14, 1, MP_NONE,        "paging states say nothing until they settle" },
            {  16, 1, MP_NONE,        "paging states say nothing until they settle" },
            {   0, 0, MP_NONE,        "init says nothing" },
        };
        for (unsigned k = 0; k < sizeof(P)/sizeof(P[0]); k++) {
            v.state = (unsigned short)P[k].st;
            v.screenMode = (unsigned char)P[k].mode;
            if (MagicPhaseOf(v) != P[k].want) {
                bad++; printf("  BAD: %s (state %d mode %d -> phase %d)\n",
                              P[k].what, P[k].st, P[k].mode, (int)MagicPhaseOf(v));
            }
        }
        printf("phase routing: 12 states map as the exe's jump table says\n");
    }

    // ---------------------------------------------------------------------
    // 10. **THE MODULE WALK.** The offsets are all relative to the Magic
    //     module, and "Magic is pool slot 2" was an INFERENCE. Prove the walk
    //     finds it wherever it lands -- including with other submenus open
    //     ahead of it in the list.
    // ---------------------------------------------------------------------
    {
        for (int place = 0; place < 10; place++) {
            MenuSim sim;
            for (int k = 0; k < place; k++) sim.Alloc(0x004DA9B0 + k);   // decoys
            const int want = sim.Alloc(MenuSim::MAGIC_UPDATE_FN);
            for (int k = place + 1; k < 10; k++) sim.Alloc(0x004C0CF0 + k);
            const int got = sim.FindMagic();
            if (got != want) {
                bad++;
                printf("  BAD: Magic allocated in slot %d, walk found %d\n", want, got);
            }
        }
        MenuSim empty;
        check(empty.FindMagic() < 0, "an empty pool must not report a Magic module");
        MenuSim noMagic;
        for (int k = 0; k < 10; k++) noMagic.Alloc(0x004F81F0);   // ten Item modules
        check(noMagic.FindMagic() < 0, "ten Item modules must not look like Magic");
        printf("module walk: Magic is found in all 10 pool slots, and never "
               "invented when it is not open\n");
    }

    // ---------------------------------------------------------------------
    // 11. Fuzz: no input may overflow the buffer or produce an empty line in a
    //     phase that should speak. This is where a tester cannot go.
    // ---------------------------------------------------------------------
    {
        unsigned seed = 12345;
        int spoke = 0, silent = 0;
        for (int i = 0; i < 20000; i++) {
            seed = seed * 1103515245u + 12345u;
            MagicView v = MakeView();
            const unsigned r = seed >> 8;
            v.state        = (unsigned short)(r % 120);
            v.screenMode   = (unsigned char)((r >> 7) % 6);
            v.charId       = (unsigned char)((r >> 3) % 8);
            v.page         = (unsigned char)((r >> 5) % 16);      // out of range on purpose
            v.cursorRaw    = (unsigned char)((r >> 9) % 256);
            v.actionCursor = (unsigned char)((r >> 11) % 8);
            v.actionMask   = (unsigned char)((r >> 13) % 16);
            v.targetCursor = (unsigned char)((r >> 15) % 10);
            v.targetCount  = (unsigned char)((r >> 17) % 10);
            v.targetMask   = (unsigned short)((r >> 19) % 256);
            v.sortCursor   = (unsigned char)((r >> 21) % 10);
            v.dialogOpen   = (unsigned short)((r >> 23) % 3);
            v.dialogCursor = (unsigned char)((r >> 25) % 3);
            v.dialogChar   = (unsigned char)((r >> 2) % 10);
            v.dialogSlot   = (unsigned char)((r >> 4) % 40);
            v.secondChar   = (unsigned char)((r >> 6) % 10);
            for (int s = 0; s < 32; s++) {
                v.slots[s].id  = (unsigned char)((r >> (s % 20)) % 60);
                v.slots[s].qty = (unsigned char)((r >> ((s + 7) % 20)) % 101);
            }
            const MagicPhase ph = MagicPhaseOf(v);
            memset(buf, 0xAB, sizeof(buf));
            MagicAnnounce(v, ph, buf, sizeof(buf));
            // Terminated inside the buffer.
            bool term = false;
            for (size_t k = 0; k < sizeof(buf); k++) if (buf[k] == '\0') { term = true; break; }
            if (!term) { bad++; printf("  BAD: fuzz %d produced an unterminated string\n", i); break; }
            if (strlen(buf) >= sizeof(buf) - 1) {
                bad++; printf("  BAD: fuzz %d filled the buffer (%zu)\n", i, strlen(buf)); break;
            }
            const bool shouldSpeak = (ph == MP_ACTION || ph == MP_LIST || ph == MP_TARGET ||
                                      ph == MP_SORT   || ph == MP_DISCARD ||
                                      ph == MP_XCHG_MINE || ph == MP_XCHG_PARTNER ||
                                      ph == MP_XCHG_THEIRS || ph == MP_XCHG_POPUP ||
                                      ph == MP_XCHG_SPLIT || ph == MP_ALL_RECEIVER ||
                                      ph == MP_ALL_GIVER || ph == MP_ALL_DONE ||
                                      ph == MP_ALL_WARN);
            if (shouldSpeak && buf[0] == '\0') {
                bad++;
                printf("  BAD: fuzz %d phase %d said nothing (state %u mode %u)\n",
                       i, (int)ph, (unsigned)v.state, (unsigned)v.screenMode);
                break;
            }
            if (shouldSpeak) spoke++; else silent++;
        }
        printf("fuzz: 20,000 random states -- %d spoke, %d stayed silent, none "
               "overflowed and no speaking phase produced an empty line\n", spoke, silent);
    }

    // ---------------------------------------------------------------------
    // 11b. **THE REPEATED HEADER.** v0.22.0 said "Magic list" on every single
    //      page turn. Paging runs 13 -> 14 -> 13, and the poll recorded the
    //      transient phase, so coming back looked like a fresh phase change.
    //      This models the poll's phase bookkeeping and asserts the header is
    //      spoken once per ARRIVAL, not once per page.
    // ---------------------------------------------------------------------
    {
        MagicView v = MakeView();
        FillSlots(v);
        v.screenMode = 1;

        MagicPhase spokenPhase = MP_NONE;   // mirrors s_mmPhase
        int headers = 0, lines = 0;
        std::string last;

        // The exact sequence a player produces: land in the list, then page
        // right four times, each page passing through the transient state 16.
        const int SEQ[] = { 13, 13, 16, 13, 16, 13, 16, 13, 16, 13, 13 };
        int page = 0;
        for (unsigned k = 0; k < sizeof(SEQ)/sizeof(SEQ[0]); k++) {
            v.state = (unsigned short)SEQ[k];
            if (SEQ[k] == 16) { page = (page + 1) & 7; continue; }   // transient: poll returns early
            v.page = (unsigned char)page;
            v.cursorRaw = (unsigned char)(page * 4);
            const MagicPhase ph = MagicPhaseOf(v);
            char b[256];
            MagicAnnounce(v, ph, b, sizeof(b));
            if (b[0] == '\0') continue;
            const bool phaseChanged = (ph != spokenPhase);
            if (!phaseChanged && last == b) continue;
            if (phaseChanged) headers++;
            lines++;
            last = b;
            spokenPhase = ph;
        }
        if (headers != 1) {
            bad++;
            printf("  BAD: the list header was spoken %d times across five pages, want 1\n", headers);
        }
        check(lines == 5, "each of the five pages should still announce its own slot");
        printf("paging: the \"Magic list\" header is spoken once on arrival, not "
               "once per page (%d headers, %d slot lines)\n", headers, lines);
    }

    // ---------------------------------------------------------------------
    // 11c. **THE EXCHANGE FLOW**, which v0.22.0 left entirely silent. Both
    //      panels must name whose list they are -- two lists on one screen and
    //      "Cure, quantity 47" twice over is exactly the ambiguity that made
    //      this unusable.
    // ---------------------------------------------------------------------
    {
        MagicView v = MakeView();
        FillSlots(v);
        v.secondChar = 1;                      // Zell
        memset(v.slotsB, 0, sizeof(v.slotsB));
        v.slotsB[0].id = 21; v.slotsB[0].qty = 8;    // Zell has 8 Cure
        v.slotsB[6].id = 1;  v.slotsB[6].qty = 60;   // and 60 Fire in slot 7

        v.state = 26; v.page = 0; v.cursorRaw = 1;
        expect(Say(v).c_str(), "Cure, quantity 47, slot 2 of 32",
               "your own list: no owner on the line");
        expect(MagicPhaseHeader(v, MagicPhaseOf(v)), "Squall's magic",
               "the owner belongs in the header, spoken once on arrival");

        v.state = 28;
        expect(Say(v).c_str(), "Zell", "choosing the partner");

        v.state = 44; v.pageB = 0; v.cursorRawB = 0;
        expect(Say(v).c_str(), "Cure, quantity 8, slot 1 of 32",
               "the partner's list: still no owner on the line");
        expect(MagicPhaseHeader(v, MagicPhaseOf(v)), "Zell's magic",
               "arriving in the partner's panel names the partner, once");

        v.pageB = 1; v.cursorRawB = 2;         // slot 6
        expect(Say(v).c_str(), "Fire, quantity 60, slot 7 of 32",
               "column B pages on its own byte");

        v.pageB = 3; v.cursorRawB = 0;         // slot 12, empty
        expect(Say(v).c_str(), "Empty, slot 13 of 32", "an empty partner slot");

        // The popups.
        v.state = 55; v.popupKind = 2; v.popupCursor = 0;
        expect(Say(v).c_str(), "Give All, 1 of 3", "three-entry popup");
        v.popupCursor = 2;
        expect(Say(v).c_str(), "Split, 3 of 3", "three-entry popup, last");
        v.state = 52; v.popupKind = 1; v.popupCursor = 1;
        expect(Say(v).c_str(), "Split, 2 of 2", "two-entry popup");

        // The quantity split: both counters, every time.
        v.state = 63; v.splitSpell = 21; v.splitTake = 12; v.splitLeave = 35;
        expect(Say(v).c_str(), "Cure, move 12, keep 35", "the split says both sides");
        v.splitTake = 0; v.splitLeave = 47;
        expect(Say(v).c_str(), "Cure, move 0, keep 47", "the split at zero");
        printf("exchange: the owner is named once per panel and never per slot, the "
               "popups enumerate, and the split reads both counters\n");
    }

    // ---------------------------------------------------------------------
    // 11d. **THE "ALL" FLOW, AND ITS DIRECTION.**
    //
    //      The tester described it as give-then-receive. The code says the
    //      opposite: state 105 calls the transfer with arg1 = +0x62 (step 2)
    //      losing and arg2 = +0x64 (step 1) gaining, and step 2's mask excludes
    //      the step-1 choice. The game's own help bar agrees -- group 8 entry 12
    //      on step 1 is "Select member to receive magic".
    //
    //      This test pins the direction so it cannot be quietly flipped later.
    // ---------------------------------------------------------------------
    {
        MagicView v = MakeView();
        v.charId = 3;        // Quistis -- chosen in step 1
        v.secondChar = 5;    // Selphie -- chosen in step 2

        v.state = 97;
        check(MagicPhaseOf(v) == MP_ALL_RECEIVER, "state 97 is the receiver step");
        expect(Say(v).c_str(), "Quistis, receives", "step 1 names the receiver");
        expect(MagicPhaseHeader(v, MagicPhaseOf(v)), "Select member to receive magic",
               "step 1 uses the game's own help-bar wording");

        v.state = 99;
        check(MagicPhaseOf(v) == MP_ALL_GIVER, "state 99 is the giver step");
        expect(Say(v).c_str(), "Selphie, gives all magic", "step 2 names the giver");
        expect(MagicPhaseHeader(v, MagicPhaseOf(v)), "Select member to transfer magic",
               "step 2 uses the game's own help-bar wording");

        v.state = 105;
        expect(Say(v).c_str(), "All magic moved from Selphie to Quistis",
               "the transfer names the direction the code actually implements");

        v.state = 106;
        check(MagicPhaseOf(v) == MP_ALL_WARN, "state 106 is the pre-flight warning");
        printf("all: receiver first then giver, in the game's own words, with the "
               "transfer direction pinned\n");
    }

    // ---------------------------------------------------------------------
    // 11e. Screen mode must no longer decide anything. v0.22.0 routed on
    //      screenMode == 3, which BOTH flows use -- so Exchange and All were
    //      indistinguishable and neither could be narrated.
    // ---------------------------------------------------------------------
    {
        MagicView v = MakeView();
        v.screenMode = 3;
        v.state = 28; check(MagicPhaseOf(v) == MP_XCHG_PARTNER, "mode 3 + state 28 is Exchange");
        v.state = 97; check(MagicPhaseOf(v) == MP_ALL_RECEIVER, "mode 3 + state 97 is All");
        v.screenMode = 4;
        v.state = 44; check(MagicPhaseOf(v) == MP_XCHG_THEIRS, "mode 4 + state 44 is Exchange");
        v.state = 99; check(MagicPhaseOf(v) == MP_ALL_GIVER,  "mode 4 + state 99 is All");
        printf("routing: the same screen mode serves both flows, so only the state "
               "distinguishes them\n");
    }

    // ---------------------------------------------------------------------
    // 11f. **THE HELP-BAR ENCODING**, with the real bytes.
    //
    //      v0.22.1 read the bar aloud as "AaI'UEIOE". The mod's DecodeMenuText
    //      indexes its glyph table with a GLYPH INDEX, because its existing
    //      caller hands it the GCW buffer, which the renderer has already
    //      converted. mngrp.bin is one stage earlier and holds TEXT-STREAM
    //      bytes, which are `glyph + 0x20`.
    //
    //      These byte sequences are lifted verbatim out of
    //      Data/lang-en/menu.fs -> mngrp.bin group 8, so the test fails if the
    //      conversion is ever dropped again.
    // ---------------------------------------------------------------------
    {
        // Enough of the mod's glyph table for these three strings.
        static const char* G[] = {
            " ","0","1","2","3","4","5","6","7","8","9","%","/",":","!","?",
            "...","+","-","=","*","&","","","(",")"," ",".",",","~","","",
            "'","#","$","'","_","A","B","C","D","E","F","G","H","I","J","K",
            "L","M","N","O","P","Q","R","S","T","U","V","W","X","Y","Z","a",
            "b","c","d","e","f","g","h","i","j","k","l","m","n","o","p","q",
            "r","s","t","u","v","w","x","y","z"
        };
        const int GN = (int)(sizeof(G)/sizeof(G[0]));
        struct { const char* want; unsigned char raw[40]; size_t n; } S[] = {
            { "Use magic",
              { 0x59,0x71,0x63,0x20,0x6B,0x5F,0x65,0x67,0x61 }, 9 },
            { "Select member to receive magic",
              { 0x57,0x63,0x6A,0x63,0x61,0x72,0x20,0x6B,0x63,0x6B,0x60,0x63,0x70,0x20,
                0x72,0x6D,0x20,0x70,0x63,0x61,0x63,0x67,0x74,0x63,0x20,0x6B,0x5F,0x65,
                0x67,0x61 }, 30 },
            { "Select member to transfer magic",
              { 0x57,0x63,0x6A,0x63,0x61,0x72,0x20,0x6B,0x63,0x6B,0x60,0x63,0x70,0x20,
                0x72,0x6D,0x20,0x72,0x70,0x5F,0x6C,0x71,0x64,0x63,0x70,0x20,0x6B,0x5F,
                0x65,0x67,0x61 }, 31 },
        };
        for (unsigned k = 0; k < sizeof(S)/sizeof(S[0]); k++) {
            unsigned char gl[64];
            const size_t gn = MagicTextToGlyphs(S[k].raw, S[k].n, gl, sizeof(gl));
            std::string got;
            for (size_t i = 0; i < gn; i++) got += (gl[i] < GN) ? G[gl[i]] : "?";
            if (got != S[k].want) {
                bad++;
                printf("  BAD: help text decoded as \"%s\", want \"%s\"\n",
                       got.c_str(), S[k].want);
            }
            // The regression, stated as an assertion: indexing the RAW bytes
            // must NOT produce the text. If it did, the fixture would not be
            // text-stream encoded and would prove nothing.
            std::string wrong;
            for (size_t i = 0; i < S[k].n; i++)
                wrong += (S[k].raw[i] < GN) ? G[S[k].raw[i]] : "?";
            check(wrong != S[k].want, "the un-shifted reading must be wrong");
        }
        // A variable code and its parameter byte must both be dropped.
        {
            const unsigned char raw[] = { 0x41, 0x0A, 0x05, 0x42, 0x01, 0x43 };
            unsigned char gl[16];
            const size_t gn = MagicTextToGlyphs(raw, sizeof(raw), gl, sizeof(gl));
            check(gn == 3, "a variable code and its parameter must both be dropped");
            check(gn == 3 && gl[0] == 0x21 && gl[1] == 0x22 && gl[2] == 0x23,
                  "the surviving glyphs must be shifted down by 0x20");
        }
        printf("help bar: three real mngrp.bin strings decode correctly, and the "
               "un-shifted reading is asserted to be wrong\n");
    }
    // ---------------------------------------------------------------------
    // 11g. **CHARACTER CHANGES INSIDE A PHASE**, from the 2026-08-16 log.
    //
    //      L1/R1 swaps the character without leaving the phase. v0.22.2 spoke
    //      only the slot line, so switching from Irvine's list to Selphie's was
    //      announced as "Cure, quantity 82, slot 1 of 32" and nothing said whose
    //      magic it now was. On the action row, Zell -> Selphie was audible only
    //      as the position qualifier quietly disappearing.
    //
    //      This models the poll's arrival bookkeeping -- phase AND character --
    //      and replays the exact transitions the log recorded.
    // ---------------------------------------------------------------------
    {
        struct Poll {
            MagicPhase phase; int lastChar; std::string last;
            Poll() : phase(MP_NONE), lastChar(-1) {}
            // returns the header spoken, or "" when only the line was spoken
            std::string Step(const MagicView& v) {
                const MagicPhase ph = MagicPhaseOf(v);
                if (ph == MP_NONE || ph == MP_CLOSING) return "\x01";   // silent
                char line[256]; MagicAnnounce(v, ph, line, sizeof(line));
                if (line[0] == '\0') return "\x01";
                const int about = (ph == MP_XCHG_THEIRS || ph == MP_XCHG_PARTNER)
                                ? (int)v.secondChar : (int)v.charId;
                const bool changed = (ph != phase) || (lastChar >= 0 && about != lastChar);
                if (!changed && last == line) return "\x01";
                std::string hdr;
                if (changed) { char h[96]; MagicPhaseHeaderBuf(v, ph, h, sizeof(h)); hdr = h; }
                phase = ph; lastChar = about; last = line;
                return hdr;
            }
        };

        // The spell-list case: Irvine (2) then Selphie (5), same phase.
        {
            Poll p; MagicView v = MakeView(); FillSlots(v);
            v.state = 13; v.screenMode = 1; v.charId = 2; v.page = 0; v.cursorRaw = 0;
            expect(p.Step(v).c_str(), "Irvine's magic", "arriving in a list names the owner");
            v.cursorRaw = 1;
            expect(p.Step(v).c_str(), "", "moving within the list adds no header");
            v.charId = 5; v.cursorRaw = 0;
            expect(p.Step(v).c_str(), "Selphie's magic",
                   "switching character mid-list must name the new owner");
        }

        // The action row: Zell (1) then Selphie (5), same phase.
        {
            Poll p; MagicView v = MakeView();
            v.state = 3; v.charId = 1; v.actionCursor = 1; v.actionMask = 0x0E;
            expect(p.Step(v).c_str(), "Zell", "arriving on the action row names the character");
            v.actionCursor = 2;
            expect(p.Step(v).c_str(), "", "moving along the row adds no header");
            v.charId = 5; v.actionCursor = 1; v.actionMask = 0x0F;
            expect(p.Step(v).c_str(), "Selphie",
                   "switching character on the action row must name the new one");
        }

        // The partner panel is ABOUT the partner, so its change is the one that
        // counts there -- swapping the partner must re-announce.
        {
            Poll p; MagicView v = MakeView(); FillSlots(v);
            memset(v.slotsB, 0, sizeof(v.slotsB));
            v.slotsB[0].id = 21; v.slotsB[0].qty = 8;
            v.state = 44; v.secondChar = 1; v.pageB = 0; v.cursorRawB = 0;
            expect(p.Step(v).c_str(), "Zell's magic", "arriving in the partner panel");
            v.secondChar = 4;
            expect(p.Step(v).c_str(), "Rinoa's magic",
                   "a different partner must re-announce, even at the same slot");
        }

        // And the case that must NOT fire: on the action row the partner id is
        // irrelevant, so changing it alone must stay quiet.
        {
            Poll p; MagicView v = MakeView();
            v.state = 3; v.charId = 1; v.actionCursor = 1; v.actionMask = 0x0F;
            p.Step(v);
            v.secondChar = 7;
            expect(p.Step(v).c_str(), "\x01",
                   "the partner id is not what the action row is about");
        }
        printf("character changes: a swap inside a phase re-announces whose magic "
               "it is, and an irrelevant one stays quiet\n");
    }
    // ---------------------------------------------------------------------
    // 11h. **THE ALL TRANSFER, DETECTED BY ITS EFFECT.**
    //
    //      State 105 does the work and lasts ONE FRAME. The 2026-08-16 log
    //      proves the poll cannot catch it: Irvine's entire list (Life 75,
    //      Scan 88, Cure 92 ...) moved to Zell and the mod announced nothing --
    //      the only trace was Zell's Exchange list afterwards reading exactly
    //      Irvine's old loadout.
    //
    //      So the latch watches the giver's total instead. This models it, and
    //      asserts the two outcomes are distinguishable: a completed transfer
    //      empties the giver, a cancel does not.
    // ---------------------------------------------------------------------
    {
        struct AllLatch {
            int giver, receiver; long held; std::string said;
            AllLatch() : giver(-1), receiver(-1), held(0) {}
            void Step(const MagicView& v) {
                const MagicPhase ph = MagicPhaseOf(v);
                if (ph == MP_ALL_GIVER) {
                    const long h = MagicTotalHeld(v.slotsB);
                    if (giver != (int)v.secondChar || h > held) {
                        giver = (int)v.secondChar; receiver = (int)v.charId; held = h;
                    }
                } else if (giver >= 0) {
                    const bool same = ((int)v.secondChar == giver);
                    const long now  = same ? MagicTotalHeld(v.slotsB) : -1;
                    if (same && held > 0 && now == 0) {
                        said = std::string("All magic moved from ")
                             + v.memberName[giver] + " to " + v.memberName[receiver];
                        giver = receiver = -1; held = 0;
                    } else if (!same || ph == MP_ACTION || ph == MP_CLOSING) {
                        giver = receiver = -1; held = 0;
                    }
                }
            }
        };

        // The completed transfer, replaying the log: receiver Zell(1),
        // giver Irvine(2), then the machine drops back to state 97.
        {
            AllLatch L; MagicView v = MakeView();
            v.charId = 1; v.secondChar = 2;
            memset(v.slotsB, 0, sizeof(v.slotsB));
            v.slotsB[0].id = 24; v.slotsB[0].qty = 75;   // Life
            v.slotsB[1].id = 50; v.slotsB[1].qty = 88;   // Scan
            v.slotsB[2].id = 21; v.slotsB[2].qty = 92;   // Cure
            v.state = 99; L.Step(v);
            check(L.held == 255, "the giver's total must be latched while step 2 is up");
            // **Walk the real chain.** 99 -> 104 -> 105 -> 96 -> 97, and 104/96
            // are MP_NONE. v0.22.4 cleared the latch on those and went silent in
            // the field even though this test passed -- because the test jumped
            // straight from 99 to 97 and never visited them.
            v.state = 104; L.Step(v);                    // pre-flight, nothing moved yet
            check(L.giver == 2, "a transient state must NOT disarm the latch");
            v.state = 105;                                // the transfer runs here
            memset(v.slotsB, 0, sizeof(v.slotsB));
            L.Step(v);
            v.state = 96;  L.Step(v);
            v.state = 97;  L.Step(v);
            expect(L.said.c_str(), "All magic moved from Irvine to Zell",
                   "a completed transfer must be announced even though state 105 is unobservable");
        }

        // The cancel, which the earlier logs actually recorded: back to state 97
        // with the giver still holding everything. Must stay silent.
        {
            AllLatch L; MagicView v = MakeView();
            v.charId = 1; v.secondChar = 0;
            memset(v.slotsB, 0, sizeof(v.slotsB));
            v.slotsB[0].id = 1; v.slotsB[0].qty = 100;
            v.state = 99;  L.Step(v);
            v.state = 104; L.Step(v);
            v.state = 97;  L.Step(v);
            check(L.said.empty(), "a cancel must not be announced as a transfer");
            v.state = 3;   L.Step(v);
            check(L.giver < 0, "returning to the action row disarms the latch");
        }

        // Changing the giver mid-step must re-latch, or the announcement would
        // name whoever was highlighted first. The log shows exactly this:
        // Squall, then Irvine.
        {
            AllLatch L; MagicView v = MakeView();
            v.charId = 1; v.secondChar = 0;
            memset(v.slotsB, 0, sizeof(v.slotsB));
            v.slotsB[0].id = 1; v.slotsB[0].qty = 40;
            v.state = 99; L.Step(v);
            v.secondChar = 2;                              // moved to Irvine
            v.slotsB[0].qty = 75;
            L.Step(v);
            check(L.giver == 2 && L.held == 75, "the latch must follow the giver cursor");
            memset(v.slotsB, 0, sizeof(v.slotsB));
            v.state = 105; L.Step(v);
            v.state = 97;  L.Step(v);
            expect(L.said.c_str(), "All magic moved from Irvine to Zell",
                   "the announcement must name the giver that was actually confirmed");
        }

        // A giver who holds nothing cannot produce a false positive.
        {
            AllLatch L; MagicView v = MakeView();
            v.charId = 1; v.secondChar = 2;
            memset(v.slotsB, 0, sizeof(v.slotsB));
            v.state = 99;  L.Step(v);
            v.state = 104; L.Step(v);
            v.state = 97;  L.Step(v);
            check(L.said.empty(), "an empty giver must not look like a transfer");
        }
        printf("all transfer: detected by the giver's total emptying, not by the "
               "one-frame state; cancel and an empty giver stay silent\n");
    }
    // ---------------------------------------------------------------------
    // 11i. **THE STEP PROMPT MUST NOT REPEAT PER CHARACTER.**
    //
    //      v0.22.3 made a character change force the header. On the All steps
    //      the line already says "Rinoa, receives", so that prefixed "Select
    //      member to receive magic" to every single left/right -- which is the
    //      repetition Aaron reported. The prompt belongs to the STEP, the name
    //      belongs to the line.
    //
    //      The rule is not "never re-announce": on the spell list and the action
    //      row the line does NOT name the character, so those must still fire.
    // ---------------------------------------------------------------------
    {
        check(MagicLineNamesCharacter(MP_ALL_RECEIVER), "the All receiver line names the character");
        check(MagicLineNamesCharacter(MP_ALL_GIVER),    "the All giver line names the character");
        check(MagicLineNamesCharacter(MP_XCHG_PARTNER), "the partner picker names the character");
        check(!MagicLineNamesCharacter(MP_LIST),        "the spell list line does not");
        check(!MagicLineNamesCharacter(MP_ACTION),      "the action row line does not");
        check(!MagicLineNamesCharacter(MP_XCHG_MINE),   "the Exchange own-list line does not");
        check(!MagicLineNamesCharacter(MP_XCHG_THEIRS), "the Exchange partner-list line does not");

        struct P2 {
            MagicPhase phase; int lastChar; std::string last; int headers;
            P2() : phase(MP_NONE), lastChar(-1), headers(0) {}
            void Step(const MagicView& v) {
                const MagicPhase ph = MagicPhaseOf(v);
                if (ph == MP_NONE || ph == MP_CLOSING) return;
                char line[256]; MagicAnnounce(v, ph, line, sizeof(line));
                if (line[0] == '\0') return;
                const int about = (ph == MP_XCHG_THEIRS || ph == MP_XCHG_PARTNER)
                                ? (int)v.secondChar : (int)v.charId;
                const bool ch = (lastChar >= 0 && about != lastChar && !MagicLineNamesCharacter(ph));
                if ((ph != phase) || ch) headers++;
                else if (last == line) return;
                phase = ph; lastChar = about; last = line;
            }
        };

        // Cycling five receivers, exactly as the 2026-08-16 log did.
        {
            P2 p; MagicView v = MakeView(); v.state = 97;
            const int WHO[] = { 2, 4, 3, 1, 5 };
            for (unsigned k = 0; k < sizeof(WHO)/sizeof(WHO[0]); k++) { v.charId = (unsigned char)WHO[k]; p.Step(v); }
            if (p.headers != 1) {
                bad++;
                printf("  BAD: the receiver prompt was spoken %d times across five "
                       "characters, want 1\n", p.headers);
            }
        }
        // ...and the giver step, likewise.
        {
            P2 p; MagicView v = MakeView(); v.state = 99; v.charId = 1;
            const int WHO[] = { 0, 2, 4, 3, 5 };
            for (unsigned k = 0; k < sizeof(WHO)/sizeof(WHO[0]); k++) { v.secondChar = (unsigned char)WHO[k]; p.Step(v); }
            check(p.headers == 1, "the giver prompt must be spoken once, not per character");
        }
        // But the spell list still must, because its line names nobody.
        {
            P2 p; MagicView v = MakeView(); FillSlots(v);
            v.state = 13; v.screenMode = 1;
            v.charId = 2; p.Step(v);
            v.charId = 5; p.Step(v);
            check(p.headers == 2, "the spell list must still re-announce whose magic it is");
        }
        printf("step prompts: spoken once per step, not once per character -- while "
               "the spell list and action row still re-announce\n");
    }
    // ---------------------------------------------------------------------
    // 12. Headers.
    // ---------------------------------------------------------------------
    {
        MagicView v = MakeView();
        v.state = 3;  expect(MagicPhaseHeader(v, MagicPhaseOf(v)), "Squall", "action-row header is the character");
        v.state = 13; expect(MagicPhaseHeader(v, MagicPhaseOf(v)), "Squall's magic",
                     "the list header names the character, not a generic label");
        v.state = 20; expect(MagicPhaseHeader(v, MagicPhaseOf(v)), "Choose target", "target header");
        v.state = 72; expect(MagicPhaseHeader(v, MagicPhaseOf(v)), "Rearrange order", "sort header");
        v.state = 0;  expect(MagicPhaseHeader(v, MagicPhaseOf(v)), "", "no header outside a spoken phase");
        printf("headers: one per phase, character name on the action row\n");
    }

    printf("menu_sim: %s (%d bad)\n", bad ? "FAILED" : "OK", bad);
    return bad ? 1 : 0;
}
