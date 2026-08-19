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
#include "menu_junction_model.inl"
#include "menu_card_model.inl"
#include "menu_config_model.inl"
#include "menu_tutorial_model.inl"
#include "menu_item_swap_model.inl"

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
                                      ph == MP_ALL_GIVER || ph == MP_ALL_DONE);
            // MP_ALL_WARN is deliberately NOT in that list as of v0.29.0 (#88).
            // States 106/107 are a steady two-option confirmation window shared
            // with Exchange and Split, so the model cannot know what it says;
            // menu_tts_magic.inl reads the window itself through
            // menu_dialog.inl. The model emitting a guessed sentence here is
            // what produced "Cannot take all magic. See the message on screen"
            // over a dialog that was actually asking a question.
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

    // =====================================================================
    // JUNCTION (#82). The wording for the screens Aaron listed as missing:
    // the stat grid, elemental and status junction, the preview, and the two
    // on-demand readouts.
    // =====================================================================
    {
        JunctionView j;
        memset(&j, 0, sizeof(j));
        j.charId = 0; j.charName = "Squall";
        // Bits 0..16 = junction abilities 1..17, i.e. every row on the screen.
        // The nine stat rows are bits 0..8; Elem-Atk is 9, ST-Atk 10, and the
        // two defence columns are unlocked by their -J / x2 / x4 abilities.
        j.unlockMask = 0x1FFFF;
        j.eligibleMask = 0xFFFFFFFFu;               // every stock slot does something
        for (int i = 0; i < 9; i++) { j.statBefore[i] = 40; j.statAfter[i] = 40; }
        j.statBefore[0] = j.statAfter[0] = 1837;    // HP
        for (int e = 0; e < 8; e++) j.elemDefBefore[e] = j.elemDefAfter[e] = 800;
        for (int s = 0; s < 13; s++) j.stDefBefore[s] = j.stDefAfter[s] = 100;

        // -- the grid cursor is NOT the slot number -------------------------
        // Grid index 0 is Status-Attack, not HP-J. Reading the cursor as a slot
        // would mislabel the entire left-hand column.
        check(JUNC_GRID_TO_SLOT[0]  == JSLOT_ST_ATK,   "grid 0 is status attack");
        check(JUNC_GRID_TO_SLOT[5]  == JSLOT_ELEM_ATK, "grid 5 is elemental attack");
        check(JUNC_GRID_TO_SLOT[10] == JSLOT_HP,       "grid 10 is HP");
        check(JUNC_GRID_TO_SLOT[11] == JSLOT_STR,      "grid 11 is Strength");
        check(JUNC_GRID_TO_SLOT[15] == -1,             "grid 15 is the blank cell, not a second HP");
        {
            int seen[JSLOT_COUNT]; memset(seen, 0, sizeof(seen));
            for (int i = 0; i < 20; i++) {
                const int s = JUNC_GRID_TO_SLOT[i];
                if (s < 0) continue;
                check(s >= 0 && s < JSLOT_COUNT, "grid maps outside the slot array");
                seen[s]++;
            }
            for (int s = 0; s < JSLOT_COUNT; s++)
                if (seen[s] != 1) { bad++; printf("  BAD: slot %d appears %d times in the grid\n", s, seen[s]); }
        }
        printf("junction grid: all 19 slots reachable exactly once, and index 15 "
               "is blank rather than a duplicate HP\n");

        // -- the grid line --------------------------------------------------
        char b[512];
        j.gridCursor = 11;                                  // Strength
        j.junction[JSLOT_STR] = 23; j.statAfter[1] = 68;    // Curaga
        JuncAnnounceGrid(j, b, sizeof(b));
        expect(b, "Strength, Curaga, 68", "a junctioned stat row");

        j.junction[JSLOT_STR] = 0; j.statAfter[1] = 42;
        JuncAnnounceGrid(j, b, sizeof(b));
        expect(b, "Strength, empty, 42", "an empty stat row still reports the stat");

        j.unlockMask = 0x1FFFF & ~(1u << 1);                // Str-J not learned
        JuncAnnounceGrid(j, b, sizeof(b));
        expect(b, "Strength, locked", "a row whose GF ability is not equipped");

        // The elemental and status rows are gated by the SAME mask, which the
        // first draft did not model at all -- it returned "unlocked" for every
        // non-stat row, so a character with no Elem-Def-J would have been told
        // "Elemental defence 1, empty" forever and never why.
        j.unlockMask = 0x1FFFF & ~0x6800u;                  // no Elem-Def-J at all
        j.gridCursor = 6;
        JuncAnnounceGrid(j, b, sizeof(b));
        expect(b, "Elemental defence 1, locked", "an elemental defence row with no Elem-Def ability");
        j.gridCursor = 0;
        j.unlockMask = 0x1FFFF & ~0x400u;                   // no ST-Atk-J
        JuncAnnounceGrid(j, b, sizeof(b));
        expect(b, "Status attack, locked", "the status attack row obeys bit 10, not bit 0");
        j.unlockMask = 0x1FFFF;

        // How many defence rows exist depends on which multiplier is equipped.
        check(JuncDefRowCount(0, false) == 0,             "no Elem-Def ability, no rows");
        check(JuncDefRowCount(1u << 11, false) == 1,      "Elem-Def-J alone gives one row");
        check(JuncDefRowCount(1u << 13, false) == 2,      "Elem-Def-J x2 gives two");
        check(JuncDefRowCount(1u << 14, false) == 4,      "Elem-Def-J x4 gives four");
        check(JuncDefRowCount(1u << 12, true)  == 1,      "ST-Def-J alone gives one row");
        check(JuncDefRowCount(1u << 16, true)  == 4,      "ST-Def-J x4 gives four");

        j.gridCursor = 15;
        JuncAnnounceGrid(j, b, sizeof(b));
        expect(b, "blank", "the blank cell");

        // -- elemental defence: only what is actually there -----------------
        j.gridCursor = 6;                                   // Elem-Def 1
        j.junction[JSLOT_ELEM_DEF] = 6;                     // Blizzaga
        j.elemDefAfter[1] = 850;                            // Ice resists 50%
        JuncAnnounceGrid(j, b, sizeof(b));
        expect(b, "Elemental defence 1 of 4, Blizzaga, Ice resists 50 percent",
               "a defence row names only the element that is actually resisted, "
               "and says how many rows the column has");

        // Eight elements at once would be unusable; only non-zero speaks.
        j.elemDefAfter[0] = 900;                            // Fire exactly immune
        JuncAnnounceGrid(j, b, sizeof(b));
        expect(b, "Elemental defence 1 of 4, Blizzaga, Fire immune, Ice resists 50 percent",
               "two resisted elements, and the six neutral ones stay silent");
        j.elemDefAfter[0] = 920;                            // Fire absorbs 20%
        JuncAnnounceGrid(j, b, sizeof(b));
        expect(b, "Elemental defence 1 of 4, Blizzaga, Fire absorbs 20 percent, Ice resists 50 percent",
               "absorption above 100 is spoken as absorbing the overflow");
        j.elemDefAfter[0] = 780;                            // Fire weak 20%
        JuncAnnounceGrid(j, b, sizeof(b));
        expect(b, "Elemental defence 1 of 4, Blizzaga, Fire weak 20 percent, Ice resists 50 percent",
               "below neutral is a WEAKNESS, which the halving draft could never have said");
        j.elemDefAfter[0] = 800;

        j.gridCursor = 7; j.junction[JSLOT_ELEM_DEF + 1] = 0;
        JuncAnnounceGrid(j, b, sizeof(b));
        expect(b, "Elemental defence 2 of 4, empty", "an empty defence slot says nothing more");

        // -- status defence --------------------------------------------------
        j.gridCursor = 1;                                   // ST-Def 1
        j.junction[JSLOT_ST_DEF] = 40;                      // Sleep
        j.stDefAfter[7] = 140;                              // Sleep resists 40%
        JuncAnnounceGrid(j, b, sizeof(b));
        expect(b, "Status defence 1 of 4, Sleep, Sleep resists 40 percent", "a status defence row");

        // -- attack slots ----------------------------------------------------
        j.gridCursor = 5; j.junction[JSLOT_ELEM_ATK] = 1;   // Fire
        j.elemAtkMask = 0x01; j.elemAtkPct = 50;
        JuncAnnounceGrid(j, b, sizeof(b));
        expect(b, "Elemental attack, Fire, Fire 50 percent", "elemental attack");

        j.gridCursor = 0; j.junction[JSLOT_ST_ATK] = 40;
        j.stAtkMask = (1u << 7); j.stAtkRaw = 160;
        JuncAnnounceGrid(j, b, sizeof(b));
        expect(b, "Status attack, Sleep, Sleep 60 percent", "status attack");

        // -- THE STATUS-ATTACK MASK IS ASSEMBLED, NOT STORED -----------------
        // **This is the check that would have caught the v0.23.0 silence.** The
        // mod read a u16 at block +0x1B4 and got zero for the single most likely
        // ST-Atk junction there is, because Sleep does not live in that byte.
        // 0x004E0FA0 takes seven bits from +0x1B4 and six from the status word
        // at +0x18C, and every one of the six is a status a player would junction.
        check(JuncAssembleStatusMask(0x00, 0x0000) == 0,      "nothing junctioned, no statuses");
        check(JuncAssembleStatusMask(0x01, 0x0000) == 0x0001, "Death comes from the low byte");
        check(JuncAssembleStatusMask(0x20, 0x0000) == 0x0020, "Berserk is bit 5 of the low byte");
        check(JuncAssembleStatusMask(0x7F, 0x0000) == 0x007F, "only the LOW SEVEN bits of +0x1B4 count");
        check(JuncAssembleStatusMask(0x80, 0x0000) == 0x0000, "bit 7 of +0x1B4 is NOT a status");
        check(JuncAssembleStatusMask(0x00, 0x0001) == 0x0080, "Sleep is bit 0 of the status word");
        check(JuncAssembleStatusMask(0x00, 0x0004) == 0x0100, "Slow  is bit 2 of the status word");
        check(JuncAssembleStatusMask(0x00, 0x0008) == 0x0200, "Stop  is bit 3 of the status word");
        check(JuncAssembleStatusMask(0x00, 0x0200) == 0x0400, "Curse is bit 9 of the status word");
        check(JuncAssembleStatusMask(0x00, 0x4000) == 0x0800, "Confuse is bit 14 of the status word");
        check(JuncAssembleStatusMask(0x00, 0x8000) == 0x1000, "Drain is bit 15 of the status word");
        check(JuncAssembleStatusMask(0x1A, 0x0000) == 0x001A, "Pain: Poison, Darkness and Silence together");
        // Every one of the thirteen must be reachable, or some junction is mute.
        {
            unsigned short seen = 0;
            for (int i = 0; i < 7; i++)  seen |= JuncAssembleStatusMask((unsigned char)(1u << i), 0);
            const unsigned int W[6] = { 0x0001, 0x0004, 0x0008, 0x0200, 0x4000, 0x8000 };
            for (int i = 0; i < 6; i++)  seen |= JuncAssembleStatusMask(0, W[i]);
            check(seen == 0x1FFF, "all thirteen junction statuses must be reachable");
        }
        check(JuncStatusAtkPct(100) == 0,  "100 raw is no status attack");
        check(JuncStatusAtkPct(160) == 60, "the ST-Atk percentage is offset by 100, unlike Elem-Atk");
        // A Sleep junction, read the way the game assembles it, end to end.
        j.stAtkMask = JuncAssembleStatusMask(0x00, 0x0001);
        j.stAtkRaw  = 130;
        JuncAnnounceGrid(j, b, sizeof(b));
        expect(b, "Status attack, Sleep, Sleep 30 percent",
               "a Sleep junction must speak -- it reads zero from +0x1B4 alone");
        j.stAtkMask = (1u << 7); j.stAtkRaw = 160;
        printf("junction rows: stat, locked, blank, elemental and status attack "
               "and defence all read correctly, with neutral entries silent\n");

        // -- THE PREVIEW, which is the point of the screen -------------------
        {
            JunctionView p = j;
            memset(p.junction, 0, sizeof(p.junction));
            memset(p.magics, 0, sizeof(p.magics));
            p.magics[0].id = 23; p.magics[0].qty = 47;      // Curaga
            p.magics[1].id = 21; p.magics[1].qty = 9;       // Cure
            p.gridCursor = 11;                              // Strength
            p.magicCursor = 0;
            p.statBefore[1] = 42; p.statAfter[1] = 68;
            JuncAnnounceMagicChoice(p, b, sizeof(b));
            expect(b, "Curaga, quantity 47, Str 42 to 68",
                   "the preview must say what the junction WOULD do");

            p.magicCursor = 1;
            p.statAfter[1] = 42;                            // Cure changes nothing here
            JuncAnnounceMagicChoice(p, b, sizeof(b));
            expect(b, "Cure, quantity 9, no change",
                   "a candidate that changes nothing must say so, not stay silent");

            p.magicCursor = 5;                              // an empty stock slot
            JuncAnnounceMagicChoice(p, b, sizeof(b));
            expect(b, "Empty", "an empty magic slot");

            // A spell the game will not accept in this row is drawn DIM, and
            // dim is invisible to a screen reader. The mask comes from the
            // game's own eligibility loop, so this is not the mod's opinion.
            p.magicCursor = 1;
            p.eligibleMask = ~(1u << 1);
            JuncAnnounceMagicChoice(p, b, sizeof(b));
            expect(b, "Cure, quantity 9, no effect here",
                   "an ineligible candidate must say so instead of reading like a valid one");
            // ...and it must not silently swallow a spell that IS eligible.
            p.eligibleMask = 0xFFFFFFFFu;
            p.statAfter[1] = p.statBefore[1];
            JuncAnnounceMagicChoice(p, b, sizeof(b));
            expect(b, "Cure, quantity 9, no change",
                   "\"no effect here\" and \"no change\" are different answers");

            // -- THE TWO ATTACK ROWS PREVIEWED AS "no change" -----------------
            // The delta walk only covers the defence arrays, so on Elem-Atk and
            // ST-Atk -- where the whole point is what the row would become --
            // every candidate read as doing nothing.
            p.gridCursor = 0;                                // Status attack
            p.magics[0].id = 40; p.magics[0].qty = 30;       // Sleep, 30 held
            p.magicCursor = 0;
            p.stAtkMaskBefore = 0; p.stAtkRawBefore = 100;   // nothing junctioned yet
            p.stAtkMask = JuncAssembleStatusMask(0x00, 0x0001);
            p.stAtkRaw  = 130;
            JuncAnnounceMagicChoice(p, b, sizeof(b));
            expect(b, "Sleep, quantity 30, Sleep 0 to 30 percent",
                   "a status-attack candidate must say what the row would become");

            // -- A JUNCTION IS A TRADE, AND BOTH SIDES ARE THE COMPARISON -----
            // Aaron: *"the mod said Confuse 8% or similar, but neglected to
            // mention the drop in the Stop status. A sighted player can see both
            // effects."* Junctioning over an occupied row drops what was there,
            // and v0.23.1 said only the rise -- describing a trade as a gift.
            p.stAtkMaskBefore = JuncAssembleStatusMask(0x00, 0x0008);   // Stop
            p.stAtkRawBefore  = 140;                                     // at 40%
            p.stAtkMask       = JuncAssembleStatusMask(0x00, 0x4000);    // Confuse
            p.stAtkRaw        = 108;                                     // at 8%
            JuncAnnounceMagicChoice(p, b, sizeof(b));
            expect(b, "Sleep, quantity 30, Stop 40 to 0 percent, Confuse 0 to 8 percent",
                   "the DROP must be spoken as well as the rise");

            // Same set, different percentage: one clause, not two.
            p.stAtkMaskBefore = p.stAtkMask; p.stAtkRawBefore = 130;
            JuncAnnounceMagicChoice(p, b, sizeof(b));
            expect(b, "Sleep, quantity 30, Confuse 30 to 8 percent",
                   "keeping the status and lowering the percentage is one delta");

            // A multi-status spell displacing another: the whole trade, grouped
            // by the transition rather than one clause per entry.
            p.stAtkMaskBefore = JuncAssembleStatusMask(0x04, 0x0000);    // Petrify
            p.stAtkRawBefore  = 150;
            p.stAtkMask       = JuncAssembleStatusMask(0x1A, 0x0000);    // Pain
            p.stAtkRaw        = 120;
            JuncAnnounceMagicChoice(p, b, sizeof(b));
            expect(b, "Sleep, quantity 30, Poison, Darkness, Silence 0 to 20 percent, "
                      "Petrify 50 to 0 percent",
                   "three statuses arriving share one clause; the displaced one keeps its own");
            p.stAtkMaskBefore = 0; p.stAtkRawBefore = 100;
            p.stAtkMask = 0; p.stAtkRaw = 100;

            p.gridCursor = 5;                                // Elemental attack
            p.elemAtkMaskBefore = 0; p.elemAtkPctBefore = 0;
            p.elemAtkMask = 0x01; p.elemAtkPct = 50;
            JuncAnnounceMagicChoice(p, b, sizeof(b));
            expect(b, "Sleep, quantity 30, Fire 0 to 50 percent",
                   "and so must an elemental-attack candidate");

            p.elemAtkMaskBefore = 0x04; p.elemAtkPctBefore = 80;   // Thunder 80%
            JuncAnnounceMagicChoice(p, b, sizeof(b));
            // Order follows the table, not the direction of the change -- the
            // same order the defence rows have always used, so the readout and
            // the preview agree about where an element sits in the list.
            expect(b, "Sleep, quantity 30, Fire 0 to 50 percent, Thunder 80 to 0 percent",
                   "swapping the element junction trades one for the other");
            p.elemAtkMaskBefore = 0; p.elemAtkPctBefore = 0;

            p.elemAtkMask = 0; p.elemAtkPct = 0;
            JuncAnnounceMagicChoice(p, b, sizeof(b));
            expect(b, "Sleep, quantity 30, no change",
                   "an attack candidate that gives the row nothing still says so");
            p.magics[0].id = 23; p.magics[0].qty = 47;
            p.gridCursor = 11;

            // -- the arrival header -------------------------------------------
            // Aaron read "no effect here" on spell after spell as a bug. It is
            // not: only thirteen spells in the game carry a status-attack value.
            // Said once on arrival it is information; said thirty-two times it
            // sounds like a hook that has given up.
            char h[192];
            p.gridCursor = 11; p.eligibleMask = 0xFFFFFFFFu;
            JuncMagicHeader(p, h, sizeof(h));
            expect(h, "Choose magic for Strength", "the header names the row being filled");

            // The grid's header names the CHARACTER: every line on that screen
            // differs per character and none of them say whose it is, and L1/R1
            // swaps the character without leaving the screen.
            JuncGridHeader(p, h, sizeof(h));
            expect(h, "Junction, Squall", "the grid header names the character");
            {
                JunctionView u = p; u.charName = nullptr;
                JuncGridHeader(u, h, sizeof(h));
                expect(h, "Junction", "and degrades to the bare header if the name is unknown");
            }

            p.gridCursor = 0; p.eligibleMask = 0;
            JuncMagicHeader(p, h, sizeof(h));
            expect(h, "Choose magic for Status attack. None of your magic affects this row",
                   "an unfillable row is called out ONCE, on arrival");

            p.gridCursor = 7;
            JuncMagicHeader(p, h, sizeof(h));
            expect(h, "Choose magic for Elemental defence 2. None of your magic affects this row",
                   "the defence rows are numbered in the header too");

            {   // An empty inventory is not an unfillable row -- do not blame the row.
                JunctionView e = p;
                memset(e.magics, 0, sizeof(e.magics));
                e.eligibleMask = 0;
                e.gridCursor = 0;
                JuncMagicHeader(e, h, sizeof(h));
                expect(h, "Choose magic for Status attack",
                       "with no magic at all the header must not blame the row");
            }
            p.eligibleMask = 0xFFFFFFFFu; p.gridCursor = 11;

            // On a defence slot the preview is the DELTA, which is what the
            // player is actually comparing between candidates.
            p.gridCursor = 6; p.magicCursor = 0;
            p.statAfter[1] = p.statBefore[1];
            for (int e = 0; e < 8; e++) p.elemDefBefore[e] = p.elemDefAfter[e] = 800;
            // Reset the status side too -- the fixture above left Sleep at 40%,
            // and the announcer correctly reported it as a second delta. That
            // was the fixture leaking, not the model over-reporting.
            for (int s = 0; s < 13; s++) p.stDefBefore[s] = p.stDefAfter[s] = 100;
            p.elemDefAfter[1] = 850;
            JuncAnnounceMagicChoice(p, b, sizeof(b));
            expect(b, "Curaga, quantity 47, Ice 0 to 50 percent",
                   "a defence preview reports the change, not the whole table");

            // A defence swap drops the old element as it raises the new one.
            p.elemDefBefore[0] = 870;                       // Fire was 70%
            JuncAnnounceMagicChoice(p, b, sizeof(b));
            expect(b, "Curaga, quantity 47, Fire 70 to 0 percent, Ice 0 to 50 percent",
                   "a defence swap reports the loss as well as the gain");
            p.elemDefBefore[0] = 800;

            // Ten statuses arriving at once is ONE clause, not ten -- and not
            // four and a shrug, which is what the old flat cap of four produced.
            {
                JunctionView g = p;
                for (int e = 0; e < 8; e++) g.elemDefBefore[e] = g.elemDefAfter[e] = 800;
                for (int s = 0; s < 13; s++) { g.stDefBefore[s] = 100; g.stDefAfter[s] = 100; }
                g.gridCursor = 1;                            // a status defence row
                const int SET[10] = { 1,2,3,4,5,7,8,9,10,11 };
                for (int i = 0; i < 10; i++) g.stDefAfter[SET[i]] = 120;
                JuncAnnounceMagicChoice(g, b, sizeof(b));
                expect(b, "Curaga, quantity 47, Poison, Petrify, Darkness, Silence, Berserk, "
                          "Sleep, Slow, Stop 0 to 20 percent, and more",
                       "a ten-status junction groups by its transition and admits the tail");
                check(strstr(b, "and more") != nullptr,
                      "anything dropped must be admitted, never silently truncated");
            }
        }
        printf("junction preview: reads the game's own before/after blocks, so a "
               "candidate's effect is spoken before it is committed\n");

        // -- the two on-demand readouts --------------------------------------
        {
            JunctionView r = j;
            for (int i = 0; i < 9; i++) r.statAfter[i] = (unsigned short)(10 * i + 5);
            r.statAfter[0] = 1837;
            JuncAnnounceStats(r, b, sizeof(b));
            expect(b, "Squall. HP 1837, Str 15, Vit 25, Mag 35, Spr 45, Spd 55, "
                      "Eva 65, Hit 75, Luck 85", "the stat readout");

            for (int e = 0; e < 8; e++) r.elemDefAfter[e] = 800;
            for (int s = 0; s < 13; s++) r.stDefAfter[s] = 100;
            JuncAnnounceResistances(r, b, sizeof(b));
            expect(b, "Elemental. no resistances or weaknesses. Status. no resistances or weaknesses",
                   "a wholly neutral character gets a short answer, not 21 zeroes");

            r.elemDefAfter[2] = 900; r.stDefAfter[0] = 200;
            JuncAnnounceResistances(r, b, sizeof(b));
            expect(b, "Elemental. Thunder immune. Status. Death immune",
                   "the readout names only what is non-zero");
        }
        printf("junction readouts: full stats and resistances on demand, with "
               "neutral values omitted rather than enumerated\n");

        // -- the percentage conversions, isolated ----------------------------
        // **The conversion must match menu_tts_status.inl, which reads the very
        // same words and validated its scale in play (magic 46 defence junction
        // = 66 percent at status index 5). The first draft halved the elemental
        // value; these anchors make that impossible to reintroduce.**
        check(JuncElemDefPct(800) == 0,    "800 raw is neutral");
        check(JuncElemDefPct(866) == 66,   "raw minus 800 IS the percentage -- no halving");
        check(JuncElemDefPct(900) == 100,  "900 raw is immune, not 50 percent");
        check(JuncElemDefPct(920) == 120,  "above 900 is absorption");
        check(JuncElemDefPct(780) == -20,  "below neutral is a weakness, and must stay negative");
        check(JuncStatusDefPct(100) == 0,  "100 raw is neutral status");
        check(JuncStatusDefPct(166) == 66, "the Status screen's validated 66 percent case");
        check(JuncStatusDefPct(200) == 100,"200 raw is status immunity");
        {
            char ph[96];
            check(!JuncResistPhrase("Fire", 0, true, ph, sizeof(ph)), "neutral says nothing at all");
            JuncResistPhrase("Fire", 50, true, ph, sizeof(ph));  expect(ph, "Fire resists 50 percent", "resist");
            JuncResistPhrase("Fire", 100, true, ph, sizeof(ph)); expect(ph, "Fire immune", "immune");
            JuncResistPhrase("Fire", 130, true, ph, sizeof(ph)); expect(ph, "Fire absorbs 30 percent", "absorb");
            JuncResistPhrase("Fire", -40, true, ph, sizeof(ph)); expect(ph, "Fire weak 40 percent", "weak");
            JuncResistPhrase("Sleep", 130, false, ph, sizeof(ph));
            expect(ph, "Sleep immune", "status cannot absorb, so it caps at immune");
        }
        printf("junction percentages: the scale matches the Status screen's, "
               "including weakness, immunity and absorption\n");

        // -- the number keys, mirroring the Status screen ---------------------
        {
            JunctionView k = j;
            for (int i = 0; i < 9; i++) k.statAfter[i] = (unsigned short)(10 * i + 5);
            k.statAfter[0] = 1837;
            struct { int key; const char* want; } K[] = {
                { 0, "Squall, HP 1837" },
                { 2, "Strength 15" }, { 3, "Vitality 25" }, { 4, "Magic 35" },
                { 5, "Spirit 45"   }, { 6, "Speed 55"    }, { 7, "Luck 85"  },
                { 8, "Evade 65, Hit 75" },
            };
            for (unsigned i = 0; i < sizeof(K)/sizeof(K[0]); i++) {
                JuncAnnounceStatKey(k, K[i].key, b, sizeof(b));
                expect(b, K[i].want, "number-key stat readout");
            }
            // Key 7 is Luck on the Status screen -- NOT the seventh row. Getting
            // that wrong would report Hit as Luck on one screen and not the other.
            check(strcmp((JuncAnnounceStatKey(k, 7, b, sizeof(b)), b), "Luck 85") == 0,
                  "key 7 must be Luck, matching the Status screen");

            k.elemAtkMask = 0x04; k.elemAtkPct = 80;
            k.stAtkMask = (1u << 5); k.stAtkRaw = 166;
            JuncAnnounceAttack(k, b, sizeof(b));
            expect(b, "Elemental attack. Thunder 80 percent. Status attack. Berserk 66 percent",
                   "the attack readout");
            k.elemAtkMask = 0; k.stAtkMask = 0;
            JuncAnnounceAttack(k, b, sizeof(b));
            expect(b, "No elemental attack. No status attack", "nothing junctioned to attack");
        }
        printf("junction number keys: 0 and 2-8 speak exactly what the Status "
               "screen speaks, and 1 and 9 carry attack and defence\n");

        // -- which states the poll may speak in --------------------------------
        // **This is the check that would have caught the v0.23.0 draft.** It had
        // the grid at 37, and 37 is the slide-in animation at 0x004DB008 -- a
        // state the game passes THROUGH, never one the player is IN. The same
        // shape as the Magic All-transfer latch two builds ago, so it gets a
        // gate rather than another BAT.
        check(JUNC_STATE_GRID == 52,        "the grid is state 52 (0x004DB29F), the handler that reads the D-pad");
        check(JUNC_STATE_MAGIC == 59,       "the magic list is state 59 (0x004DB575)");
        check(JUNC_STATE_ABIL_SLOTS == 24,  "the equipped-slots panel is state 24");
        check(JUNC_STATE_ABIL_LIST == 28,   "the available-ability list is state 28");
        check(JUNC_STATE_GRID != JUNC_STATE_GRID_SLIDE,
              "37 is the slide-in animation and must never be the grid");
        printf("junction states: the four steady states are pinned, and the slide-in "
               "animation is pinned as NOT one of them\n");

        // -- fuzz --------------------------------------------------------------
        {
            unsigned seed = 987654321u;
            for (int i = 0; i < 20000; i++) {
                seed = seed * 1103515245u + 12345u;
                const unsigned r = seed >> 8;
                JunctionView f = j;
                f.gridCursor  = (unsigned char)(r % 24);          // out of range on purpose, and every row
                f.magicCursor = (unsigned char)((r >> 5) % 40);
                f.unlockMask  = (r >> 9);
                f.eligibleMask = (r * 2654435761u);
                f.elemAtkMask = (unsigned char)(r >> 11);
                f.stAtkMask   = (unsigned short)((r >> 13) & 0x1FFF);
                // The BEFORE halves matter now: the delta collector reads them,
                // and the worst case for the grouped emitter is a before and an
                // after that share no entries at all.
                f.elemAtkMaskBefore = (unsigned char)(r >> 3);
                f.elemAtkPct        = (unsigned char)((r >> 7) % 200);
                f.elemAtkPctBefore  = (unsigned char)((r >> 17) % 200);
                f.stAtkMaskBefore   = (unsigned short)((r >> 2) & 0x1FFF);
                f.stAtkRaw          = (unsigned short)((r >> 6) % 300);
                f.stAtkRawBefore    = (unsigned short)((r >> 19) % 300);
                for (int e = 0; e < 8; e++)  f.elemDefBefore[e] = (unsigned short)((r >> (e % 9)) % 1400);
                for (int s = 0; s < 13; s++) f.stDefBefore[s]   = (unsigned char)((r >> (s % 11)) % 255);
                for (int s = 0; s < JSLOT_COUNT; s++) f.junction[s] = (unsigned char)((r >> (s % 16)) % 60);
                for (int e = 0; e < 8; e++)  f.elemDefAfter[e] = (unsigned short)((r >> (e % 12)) % 1400);
                for (int s = 0; s < 13; s++) f.stDefAfter[s]   = (unsigned char)((r >> (s % 10)) % 255);
                for (int k = 0; k < 32; k++) { f.magics[k].id = (unsigned char)((r >> (k % 18)) % 60);
                                               f.magics[k].qty = (unsigned char)((r >> (k % 14)) % 101); }
                // The buffers are the sizes the mod actually passes. The cursor
                // lines are short by construction; the on-demand readout is the
                // one that can legitimately list 21 entries, so it gets the
                // Status screen's 800 -- the fuzz found 512 too small for it,
                // which is precisely the case a hand-written test would miss.
                char fb[512], big[800];
                memset(fb, 0xCD, sizeof(fb)); JuncAnnounceGrid(f, fb, sizeof(fb));
                if (strlen(fb) >= sizeof(fb) - 1) { bad++; printf("  BAD: grid fuzz %d overflowed\n", i); break; }
                memset(fb, 0xCD, sizeof(fb)); JuncAnnounceMagicChoice(f, fb, sizeof(fb));
                if (strlen(fb) >= sizeof(fb) - 1) { bad++; printf("  BAD: choice fuzz %d overflowed\n", i); break; }
                memset(big, 0xCD, sizeof(big)); JuncAnnounceResistances(f, big, sizeof(big));
                if (strlen(big) >= sizeof(big) - 1) { bad++; printf("  BAD: resist fuzz %d overflowed\n", i); break; }
                memset(fb, 0xCD, sizeof(fb)); JuncAnnounceAttack(f, fb, sizeof(fb));
                if (strlen(fb) >= sizeof(fb) - 1) { bad++; printf("  BAD: attack fuzz %d overflowed\n", i); break; }
            }
            printf("junction fuzz: 20,000 random views across four announcers at the "
                   "buffer sizes the mod passes, no overflow\n");
        }
    }
    // =====================================================================
    // CARD ALBUM (#83). Aaron: *"announce each card's values in the order of
    // Top, Right, Down, Left... just announce each number with a clear space
    // between rather than saying 'Top 5, Right 2, Bottom 3, Left 4' as that is
    // verbose and will get old to the player."*
    // =====================================================================
    {
        char b[384];
        CardView c;
        memset(&c, 0, sizeof(c));

        // -- the generated table is the thing most worth pinning ------------
        // 110 cards lifted from the exe. These nine are the published values
        // every Triple Triad player knows, and most have four DISTINCT numbers
        // -- which matters, because a table read with two positions transposed
        // still passes any check that treats the four symmetrically.
        check(CARD_COUNT == 110, "there are 110 cards");
        struct { int id; const char* name; int t, r, bo, l; } K[] = {
            {   0, "Geezard",   1,  4, 1, 5 },
            {   1, "Funguar",   5,  1, 1, 3 },
            {  47, "PuPu",      3, 10, 2, 1 },
            {  83, "Shiva",     6,  7, 4, 9 },
            {  84, "Ifrit",     9,  6, 2, 8 },
            {  89, "Diablos",   5, 10, 8, 3 },
            {  94, "Alexander", 9, 10, 4, 2 },
            {  96, "Bahamut",  10,  8, 2, 6 },
            { 109, "Squall",   10,  4, 6, 9 },
        };
        for (unsigned i = 0; i < sizeof(K)/sizeof(K[0]); i++) {
            const CardDef& d = CARD_DEFS[K[i].id];
            if (strcmp(d.name, K[i].name) != 0) {
                bad++; printf("  BAD: card %d is \"%s\", expected \"%s\"\n",
                              K[i].id, d.name, K[i].name);
            }
            if (d.top != K[i].t || d.right != K[i].r || d.bottom != K[i].bo || d.left != K[i].l) {
                bad++;
                printf("  BAD: %s reads %d %d %d %d, published %d %d %d %d\n", K[i].name,
                       d.top, d.right, d.bottom, d.left, K[i].t, K[i].r, K[i].bo, K[i].l);
            }
        }
        // Every card must be usable: a name, four powers in range, at most one
        // element bit. A blank name or a zero power would be a generator bug
        // that only shows up on the one card the player happens to land on.
        for (int i = 0; i < CARD_COUNT; i++) {
            const CardDef& d = CARD_DEFS[i];
            if (!d.name || !d.name[0]) { bad++; printf("  BAD: card %d has no name\n", i); }
            if (d.top < 1 || d.top > 10 || d.right < 1 || d.right > 10 ||
                d.bottom < 1 || d.bottom > 10 || d.left < 1 || d.left > 10) {
                bad++; printf("  BAD: card %d (%s) has an out-of-range power\n", i, d.name);
            }
            if (d.elem && (d.elem & (d.elem - 1))) {
                bad++; printf("  BAD: card %d (%s) has two element bits\n", i, d.name);
            }
        }
        printf("card table: 110 cards from the exe, nine published values pinned, "
               "every name and power in range\n");

        // -- the list line --------------------------------------------------
        c.state = CARD_STATE_LIST;
        c.cursor = 0; c.count = 1;
        CardAnnounceLine(c, b, sizeof(b));
        expect(b, "Geezard, 1 4 1 5", "four bare numbers, clockwise from the top");

        // **The label is what keeps the four-number form readable.** A fifth bare
        // number joins the set: "Bite Bug 1 3 5 2 4" reads as five values, not
        // four values and a count. Caught in the BAT, and it is the one place on
        // this line where a word buys back the whole format.
        c.count = 3;
        CardAnnounceLine(c, b, sizeof(b));
        expect(b, "Geezard, 1 4 1 5, quantity 3",
               "a fifth number must be labelled, or it reads as a fifth VALUE");
        c.cursor = 2; c.count = 4;                       // Aaron's own example
        CardAnnounceLine(c, b, sizeof(b));
        expect(b, "Bite Bug, 1 3 3 5, quantity 4",
               "Aaron's own example card: the 4 is a count, not a left-hand value");
        c.cursor = 0; c.count = 3;

        c.count = CARD_NOT_HELD;
        CardAnnounceLine(c, b, sizeof(b));
        expect(b, "Geezard, 1 4 1 5, not held",
               "a seen-but-not-held card is DIMMED on screen, which a reader cannot see");

        c.count = CARD_UNKNOWN;
        CardAnnounceLine(c, b, sizeof(b));
        expect(b, "Card 1, not seen",
               "a never-seen card is a BLANK row -- naming it would leak the album");

        // Ten is "A", because that is what the card shows and what every guide,
        // opponent and rule discussion calls it. "Ten" would be a private word.
        c.cursor = 96; c.count = 1;                      // Bahamut, top 10
        CardAnnounceLine(c, b, sizeof(b));
        expect(b, "Bahamut, A 8 2 6", "ten is spoken as A, matching the card and every guide");

        c.cursor = 84; c.count = 2;                      // Ifrit, Fire
        CardAnnounceLine(c, b, sizeof(b));
        expect(b, "Ifrit, 9 6 2 8, Fire, quantity 2",
               "an element is named; most cards have none");

        // -- the header and the position ------------------------------------
        c.cursor = 0;
        CardAnnounceHeader(c, b, sizeof(b));
        expect(b, "Level 1, Monster", "the header is the page, which is the level");
        c.cursor = 60;
        CardAnnounceHeader(c, b, sizeof(b));
        expect(b, "Level 6, Boss", "level 6 is where the boss cards start");
        c.cursor = 84;
        CardAnnounceHeader(c, b, sizeof(b));
        expect(b, "Level 8, GF", "the GF cards");
        c.cursor = 109;
        CardAnnounceHeader(c, b, sizeof(b));
        expect(b, "Level 10, Player", "and the player cards are the last page");

        c.cursor = 109;
        CardAnnouncePosition(c, b, sizeof(b));
        expect(b, "Card 11 of 11, level 10 of 10, Player", "the position locates 110 cards");

        // The category boundaries are pure range tests in the game, and getting
        // one wrong would mislabel a whole run of rows.
        check(strcmp(CardCategory(54), "Monster") == 0 && strcmp(CardCategory(55), "Boss") == 0,
              "Monster ends at 54 and Boss begins at 55");
        check(strcmp(CardCategory(76), "Boss") == 0 && strcmp(CardCategory(77), "GF") == 0,
              "Boss ends at 76 and GF begins at 77");
        check(strcmp(CardCategory(98), "GF") == 0 && strcmp(CardCategory(99), "Player") == 0,
              "GF ends at 98 and Player begins at 99");
        check(CardLevel(0) == 0 && CardLevel(10) == 0 && CardLevel(11) == 1 && CardLevel(109) == 9,
              "eleven rows per level, ten levels, 110 cards exactly");

        // -- the readouts ----------------------------------------------------
        c.cursor = 0; c.count = 3;
        CardAnnounceDetail(c, b, sizeof(b));
        expect(b, "Geezard. Top 1, right 4, bottom 1, left 5. Element none. Holding 3. "
                  "Monster card, level 1",
               "the labelled form, for when you have stopped moving");
        c.count = CARD_UNKNOWN;
        CardAnnounceDetail(c, b, sizeof(b));
        expect(b, "Not seen yet", "and it withholds the same card the list withholds");

        // The summary panel is on screen the whole time -- MONSTER / BOSS / GF /
        // PLAYER / TOTAL down the right-hand side -- and v0.24.0 read none of it.
        // Aaron's screenshot is what caught it. The screen's own five numbers go
        // first, in the screen's own order, because they are what a guide or a
        // sighted player will ask about; the two the screen does NOT show follow.
        c.count = 1; c.totalHeld = 214; c.uniqueHeld = 88; c.seen = 97;
        c.gameMonster = 9; c.gameBoss = 0; c.gameGF = 2; c.gamePlayer = 0; c.gameTotal = 11;
        CardAnnounceTotals(c, b, sizeof(b));
        expect(b, "Monster 9, boss 0, GF 2, player 0, total 11. 88 different of 110, 97 seen",
               "the collection: the game's own five counters, then coverage and discovery");
        printf("card lines: terse when moving, labelled on demand, and the two "
               "states the screen conveys only by dimming or blanking a row\n");

        // -- fuzz -------------------------------------------------------------
        {
            unsigned seed = 24681357u;
            for (int i = 0; i < 20000; i++) {
                seed = seed * 1103515245u + 12345u;
                const unsigned r = seed >> 8;
                CardView f;
                memset(&f, 0, sizeof(f));
                f.state  = (unsigned short)(r % 16);
                f.cursor = (int)(r % 130) - 5;          // out of range on purpose
                f.count  = (int)((r >> 9) % 105) - 2;   // -2 and -1 both exercised
                f.totalHeld = (int)((r >> 3) % 20000);
                f.uniqueHeld = (int)((r >> 5) % 200);
                f.seen = (int)((r >> 7) % 200);
                char fb[384];
                memset(fb, 0xCD, sizeof(fb)); CardAnnounceLine(f, fb, sizeof(fb));
                if (strlen(fb) >= sizeof(fb) - 1) { bad++; printf("  BAD: card line fuzz %d overflowed\n", i); break; }
                memset(fb, 0xCD, sizeof(fb)); CardAnnounceDetail(f, fb, sizeof(fb));
                if (strlen(fb) >= sizeof(fb) - 1) { bad++; printf("  BAD: card detail fuzz %d overflowed\n", i); break; }
                memset(fb, 0xCD, sizeof(fb)); CardAnnouncePosition(f, fb, sizeof(fb));
                if (strlen(fb) >= sizeof(fb) - 1) { bad++; printf("  BAD: card position fuzz %d overflowed\n", i); break; }
                memset(fb, 0xCD, sizeof(fb)); CardAnnounceTotals(f, fb, sizeof(fb));
                if (strlen(fb) >= sizeof(fb) - 1) { bad++; printf("  BAD: card totals fuzz %d overflowed\n", i); break; }
                memset(fb, 0xCD, sizeof(fb)); CardAnnounceHeader(f, fb, sizeof(fb));
                if (strlen(fb) >= sizeof(fb) - 1) { bad++; printf("  BAD: card header fuzz %d overflowed\n", i); break; }
            }
            printf("card fuzz: 20,000 random views including out-of-range cursors and "
                   "counts, no overflow and no read past the 110-card table\n");
        }
    }

    // =====================================================================
    // CONFIG (#84). Aaron: *"There are some items in the Config menu which
    // we've essentially overridden or duplicated within the mod... when a
    // volume option is set the announcement can tell the player volume is
    // controlled by the accessibility mod and the keys... When ATB is selected
    // it should inform the player whether Enhanced Wait Mode is on/off and the
    // key for it."*
    // =====================================================================
    {
        char b[640];
        ConfigView g;
        memset(&g, 0, sizeof(g));
        g.state = CFG_STATE_LIST;
        g.bytes[3] = 100;                 // volume
        g.ewmEnabled = true;
        g.buttonsDefault = true;          // a stock pad, so no rescue warning

        // -- the four toggles, both ways --------------------------------------
        // Every one of these is shown on screen ONLY by palette: both words are
        // drawn, and the active one is merely brighter. There is no marker to
        // read, so this is the mod supplying the entire state of the row.
        struct { int row; unsigned mask; const char* off; const char* on; } T[] = {
            // Row 0's "Customize" case carries an extra warning and is checked
            // separately below; here it is the three plain toggles.
            { 1, 0x0004, "Cursor, Initial",     "Cursor, Memory" },
            { 3, 0x0100, "Scan, Once",          "Scan, Always" },
        };
        for (unsigned i = 0; i < sizeof(T)/sizeof(T[0]); i++) {
            g.cursor = T[i].row; g.flags = 0;
            CfgAnnounceRow(g, b, sizeof(b));
            expect(b, T[i].off, "toggle with the bit clear");
            g.flags = T[i].mask;
            CfgAnnounceRow(g, b, sizeof(b));
            expect(b, T[i].on, "toggle with the bit set");
        }
        g.flags = 0;

        // -- the Controller row is a TRAP, and the mod put the player next to it
        // Setting it to Customize opens a screen where Cancel is not handled at
        // all and the Steam rebinder is listening, so feeling for the way out
        // remaps a control with every press. Aaron walked into exactly that.
        //
        // v0.25.2: the warning is now UNCONDITIONAL on this row. v0.25.1 gated it
        // on the row already reading Customize, reasoning that a warning which
        // fires when nothing is wrong is noise. That was wrong, and Aaron said so
        // after the second trip: *"We should also add a warning against changing
        // the controller layout, since changing the game's defaults could conflict
        // with keys used by the mod."* A warning about a one-way door is only
        // useful on the side of it you can still turn back from.
        g.cursor = CFG_ROW_CONTROLLER;
        g.flags = 0; g.buttonsDefault = true;
        CfgAnnounceRow(g, b, sizeof(b));
        check(strncmp(b, "Controller, Normal", 18) == 0 &&
              strstr(b, "Leave this on Normal") != nullptr &&
              strstr(b, "Shift F9") != nullptr,
              "a stock pad on Normal is still warned, because the damage is one press away");
        g.flags = 0x0020;
        CfgAnnounceRow(g, b, sizeof(b));
        check(strstr(b, "Cancel does not work") != nullptr && strstr(b, "Shift F9") != nullptr,
              "and sitting on Customize warns BEFORE you confirm into the one-way door");
        g.buttonsDefault = false; g.flags = 0;
        CfgAnnounceRow(g, b, sizeof(b));
        check(strstr(b, "Buttons have been remapped") != nullptr && strstr(b, "Shift F9") != nullptr,
              "and a remapped pad is reported even when the row reads Normal -- which is "
              "exactly the state Aaron was left in");
        g.buttonsDefault = true; g.flags = 0;

        // -- the two rows the mod has taken over ------------------------------
        // A blind player who turns the game's Sound slider down and hears no
        // change would reasonably conclude the setting is broken. It is not --
        // the mod's mixer is what they are hearing. Saying so on the row turns a
        // dead control into an explained one.
        g.cursor = CFG_ROW_ATB; g.ewmEnabled = true;
        CfgAnnounceRow(g, b, sizeof(b));
        expect(b, "ATB, Active. Enhanced Wait Mode is on. Press O in battle to toggle it",
               "the ATB row reports the mod's wait mode and its key");
        g.flags = 0x0001; g.ewmEnabled = false;
        CfgAnnounceRow(g, b, sizeof(b));
        expect(b, "ATB, Wait. Enhanced Wait Mode is off. Press O in battle to toggle it",
               "the game's bit and the mod's mode are INDEPENDENT and both are spoken");
        g.flags = 0;

        g.cursor = CFG_ROW_SOUND;
        CfgAnnounceRow(g, b, sizeof(b));
        expect(b, "Sound, 100 percent. Volume is controlled by the mod: "
                  "F7 and F8 for music, F5 and F6 for effects",
               "the Sound row names the keys that actually work");
        g.bytes[3] = 0;
        CfgAnnounceRow(g, b, sizeof(b));
        check(strncmp(b, "Sound, 0 percent", 16) == 0, "and it still reads the game's own value");
        g.bytes[3] = 100;

        // -- the bars run the opposite way to their stored byte ---------------
        // Stored 0 is the FULL bar; Right DECREASES the byte. Speaking the raw
        // value would tell the player 0 is the most and 4 the least, which is
        // backwards from both the bar in front of them and any guide.
        check(CfgBarSteps(0) == 5 && CfgBarSteps(4) == 1,
              "the spoken number is the BAR LENGTH, not the stored byte");
        g.cursor = 5;                                   // Battle speed
        g.bytes[0] = 0; CfgAnnounceRow(g, b, sizeof(b));
        expect(b, "Battle speed, 5 of 5, fastest", "a full bar is the fastest");
        g.bytes[0] = 4; CfgAnnounceRow(g, b, sizeof(b));
        expect(b, "Battle speed, 1 of 5, slowest", "and the shortest is the slowest");
        g.bytes[0] = 1; CfgAnnounceRow(g, b, sizeof(b));
        expect(b, "Battle speed, 4 of 5", "only the two ends get an adjective");

        // The camera row shares the widget but not the meaning: a full bar there
        // is the MOST movement, not the fastest anything.
        g.cursor = 4; g.bytes[6] = 0;
        CfgAnnounceRow(g, b, sizeof(b));
        expect(b, "Camera movement, 5 of 5, most", "the camera row means amount, not speed");
        g.bytes[6] = 4;
        CfgAnnounceRow(g, b, sizeof(b));
        expect(b, "Camera movement, 1 of 5, least", "and the other end likewise");

        // -- the readouts ------------------------------------------------------
        g.cursor = 2;
        CfgAnnouncePosition(g, b, sizeof(b));
        expect(b, "Setting 3 of 9", "the position");
        CfgAnnounceHelp(g, b, sizeof(b));
        expect(b, "Set ATB", "the game's own help line");

        memset(g.bytes, 0, sizeof(g.bytes));
        g.bytes[3] = 100; g.flags = 0;
        CfgAnnounceAll(g, b, sizeof(b));
        expect(b, "Controller Normal, Cursor Initial, ATB Active, Scan Once, "
                  "Camera movement 5 of 5, most, Battle speed 5 of 5, fastest, "
                  "Battle message 5 of 5, fastest, Field message 5 of 5, fastest, "
                  "Sound 100 percent",
               "all nine settings in one pass");

        // -- the Customize screen ---------------------------------------------
        //
        // **The screen tells you how to leave and the mod was not reading it.**
        // The footer is mngrp bank 2 string 128, "{05}. to end, {05}( to default",
        // where each {05} is a button glyph -- the state-7 handler tests 0x0800 to
        // leave (0x004EE03E) and 0x0100 to reset everything (0x004EE007), and
        // Cancel is not among them. Aaron sat in there twice before a screenshot
        // showed the line that had been on screen the whole time.
        g.buttonsDefault = true;
        g.btnRemapActive = false;
        for (int i = 0; i < 12; i++) g.btnMap[i] = (unsigned char)(i + 1);
        g.customizePage = 0;
        g.customizeRow  = 0;
        CfgAnnounceCustomize(g, b, sizeof(b));
        check(strstr(b, "Field Map Controls") != nullptr, "the Customize page is named");
        check(strstr(b, "Cancel does nothing here") != nullptr,
              "the dead Cancel is called out, because silence there reads as a hang");
        check(strstr(b, "restore every button to default") != nullptr,
              "and the reset the screen itself offers is named, which is what was missing");
        check(strstr(b, "Do not change these") != nullptr,
              "with Aaron's warning that the mod's own keys assume the default layout");
        check(strstr(b, "Talk or Confirm") != nullptr,
              "and the row under the cursor, so the player knows what they are pointed at");
        g.customizePage = 2;
        CfgAnnounceCustomize(g, b, sizeof(b));
        check(strstr(b, "World Map Controls") != nullptr, "all three pages are named");

        // The same key does a different job on each page, so the label is indexed
        // by (page, row) -- bank 2 entries `row + 10 * (page + 2)`, doubled,
        // because every entry in that bank is a pair.
        g.customizeRow = 1;
        CfgAnnounceCustomizeRow(g, b, sizeof(b));
        expect(b, "Move back", "row 1 on the world map page");
        g.customizePage = 1;
        CfgAnnounceCustomizeRow(g, b, sizeof(b));
        expect(b, "Cancel", "the same row on the battle page is a different function");
        g.customizePage = 0;
        CfgAnnounceCustomizeRow(g, b, sizeof(b));
        expect(b, "Walk or Cancel", "and different again on the field page");

        // Rows 8 and 9 are the analog sticks: the draw loop takes a different
        // branch for them (flag byte 1) and never draws a key, so neither may
        // claim one.
        g.customizeRow = 8;
        CfgAnnounceCustomizeRow(g, b, sizeof(b));
        expect(b, "Walk", "row 8 is the left stick, and carries no key");

        // With the letters unknown the escape line names the mod's own hotkey
        // rather than guessing. **A wrong key on this screen is worse than no
        // key**: acting on it presses something that gets reassigned.
        CfgAnnounceCustomizeEscape(g, b, sizeof(b));
        check(strstr(b, "Shift F9") != nullptr,
              "an unknown key letter degrades to the mod's hotkey, never to a guess");
        printf("config rows: four toggles whose state is drawn only in palette, five "
               "bars with no readout at all, and the two rows the mod has taken over\n");
        printf("customize: page-dependent row labels, the analog rows that carry no "
               "key, and a way out that never guesses\n");

        // -- fuzz ---------------------------------------------------------------
        {
            unsigned seed = 13572468u;
            for (int i = 0; i < 20000; i++) {
                seed = seed * 1103515245u + 12345u;
                const unsigned r = seed >> 8;
                ConfigView f;
                memset(&f, 0, sizeof(f));
                f.state  = (unsigned short)(r % 16);
                f.cursor = (int)(r % 14) - 2;            // out of range on purpose
                f.flags  = (r >> 5) & 0xFFFF;
                for (int k = 0; k < 8; k++) f.bytes[k] = (unsigned char)((r >> k) % 256);
                f.ewmEnabled = ((r >> 3) & 1) != 0;
                f.customizePage = (int)((r >> 11) % 5) - 1;
                // Out-of-range rows on purpose, and a map that is NOT a
                // permutation: the reverse lookup must survive a table where a
                // logical button appears twice or not at all, which is exactly
                // what a half-written save or a mid-swap frame looks like.
                f.customizeRow  = (int)((r >> 13) % 14) - 2;
                f.btnRemapActive = ((r >> 17) & 1) != 0;
                for (int k = 0; k < 12; k++) f.btnMap[k] = (unsigned char)((r >> k) % 256);
                char fb[640];
                memset(fb, 0xCD, sizeof(fb)); CfgAnnounceRow(f, fb, sizeof(fb));
                if (strlen(fb) >= sizeof(fb) - 1) { bad++; printf("  BAD: config row fuzz %d overflowed\n", i); break; }
                memset(fb, 0xCD, sizeof(fb)); CfgAnnounceAll(f, fb, sizeof(fb));
                if (strlen(fb) >= sizeof(fb) - 1) { bad++; printf("  BAD: config all fuzz %d overflowed\n", i); break; }
                memset(fb, 0xCD, sizeof(fb)); CfgAnnounceCustomize(f, fb, sizeof(fb));
                if (strlen(fb) >= sizeof(fb) - 1) { bad++; printf("  BAD: config customize fuzz %d overflowed\n", i); break; }
                memset(fb, 0xCD, sizeof(fb)); CfgAnnounceCustomizeAll(f, fb, sizeof(fb));
                if (strlen(fb) >= sizeof(fb) - 1) { bad++; printf("  BAD: customize-all fuzz %d overflowed\n", i); break; }
                memset(fb, 0xCD, sizeof(fb)); CfgAnnounceCustomizeRow(f, fb, sizeof(fb));
                memset(fb, 0xCD, sizeof(fb)); CfgAnnounceCustomizePage(f, fb, sizeof(fb));
                memset(fb, 0xCD, sizeof(fb)); CfgAnnounceCustomizeEscape(f, fb, sizeof(fb));
                memset(fb, 0xCD, sizeof(fb)); CfgAnnounceHelp(f, fb, sizeof(fb));
                memset(fb, 0xCD, sizeof(fb)); CfgAnnouncePosition(f, fb, sizeof(fb));
            }
            printf("config fuzz: 20,000 random views including out-of-range rows and "
                   "bar bytes above 4, no overflow\n");
        }
    }

    // =======================================================================
    // v0.26.0 (#85): the Tutorial menu and the SeeD written exam.
    //
    // Aaron: *"There is a lot of information in the Tutorial section... In
    // particular we need to make the SeeD Exam Quiz and its questions
    // accessible - including the symbols it sometimes displays in various
    // questions."*
    // =======================================================================
    {
        char b[640];

        // -- the expander, which is the whole feature ------------------------
        //
        // Text bytes are `glyph + 0x20`; control codes are the bytes below it.
        // Helper writes English into FF8's encoding so the fixtures stay
        // readable and the real glyph table is still the thing under test.
        struct Enc {
            static int byteOf(char c) {
                char one[2] = { c, 0 };
                for (int i = 0; i < TUT_GLYPH_COUNT; i++)
                    if (strcmp(TUT_GLYPH[i], one) == 0) return 0x20 + i;
                return -1;
            }
            static int put(unsigned char* o, int n, const char* s) {
                for (const char* p = s; *p; p++) { int v = byteOf(*p); if (v >= 0) o[n++] = (unsigned char)v; }
                return n;
            }
        };
        const char* NAMES[8] = { "Squall", 0, 0, 0, "Rinoa", 0, 0, 0 };
        const char* GFS[16]  = { "Quezacotl", "Shiva", "Ifrit", "Siren", 0, 0, "Carbuncle",
                                 "Leviathan", 0, "Cerberus", "Alexander", 0, 0, 0, 0, 0 };
        unsigned char t[256];
        int n;
        TutTextInfo info;

        n = Enc::put(t, 0, "The Draw command extracts magic from enemies."); t[n++] = 0;
        TutExpand(t, n, NAMES, GFS, b, sizeof(b), &info, -1);
        expect(b, "The Draw command extracts magic from enemies.", "plain question text round-trips");

        // **A line break is a WRAP, not a sentence end.** v0.26.0 first turned
        // it into a full stop, which read fine on a fixture and split the game's
        // own questions in half the moment it met real data: test 4 question 9
        // came out "Squall's gunblade causes more damage. by pressing the first
        // Escape button at the right time." One space, and the fixed-width
        // padding around it, is the only rule that is right in both cases.
        n = Enc::put(t, 0, "GF stands for   "); t[n++] = 0x02;
        n = Enc::put(t, n, "Garden Fighter."); t[n++] = 0;
        TutExpand(t, n, NAMES, GFS, b, sizeof(b), &info, -1);
        expect(b, "GF stands for Garden Fighter.", "a line break is a space, and the padding goes");

        n = Enc::put(t, 0, "causes more damage"); t[n++] = 0x02;
        n = Enc::put(t, n, "by pressing "); t[n++] = 0x05; t[n++] = 0x20;
        n = Enc::put(t, n, " at the right time."); t[n++] = 0;
        TutExpand(t, n, NAMES, GFS, b, sizeof(b), &info, -1);
        expect(b, "causes more damage by pressing the first Escape button at the right time.",
               "so a sentence wrapped across two lines stays one sentence");

        // Two symbols back to back, which is test 23 question 2 verbatim. Before
        // the padding rule these fused into "buttonthe".
        n = Enc::put(t, 0, "Hold down "); t[n++] = 0x05; t[n++] = 0x23;
        t[n++] = 0x05; t[n++] = 0x21;
        n = Enc::put(t, n, " simultaneously."); t[n++] = 0;
        TutExpand(t, n, NAMES, GFS, b, sizeof(b), &info, -1);
        expect(b, "Hold down the Trigger button the second Escape button simultaneously.",
               "and two adjacent symbols do not fuse into one word");

        // -- the symbols -----------------------------------------------------
        //
        // Six questions ask "<icon> signifies X", and a test is only passed by
        // answering ALL TEN correctly -- so an unnamed icon does not make those
        // tests harder, it makes six of the thirty unpassable except by luck.
        n = 0; t[n++] = 0x05; t[n++] = 0x45;
        n = Enc::put(t, n, " signifies Junction Ability."); t[n++] = 0;
        TutExpand(t, n, NAMES, GFS, b, sizeof(b), &info, -1);
        expect(b, "the Junction Ability icon signifies Junction Ability.",
               "the ability icons are named, from the game's own Icon Explanation page");

        n = 0; t[n++] = 0x05; t[n++] = 0x49;
        n = Enc::put(t, n, " signifies Character Ability."); t[n++] = 0;
        TutExpand(t, n, NAMES, GFS, b, sizeof(b), &info, -1);
        check(strstr(b, "the Party Ability icon") != nullptr,
              "and the icon whose question is a TRAP is named truthfully -- this one is "
              "the Party icon and the correct answer is No");

        // A remappable button is spoken as what it DOES. The sprite is chosen
        // through the player's own map (0x004A2DF0), so a fixed shape name
        // would be wrong exactly as often as the player has remapped anything.
        n = 0; t[n++] = 0x03; t[n++] = 0x30;
        n = Enc::put(t, n, " presses "); t[n++] = 0x05; t[n++] = 0x23;
        n = Enc::put(t, n, " at the right time."); t[n++] = 0;
        TutExpand(t, n, NAMES, GFS, b, sizeof(b), &info, -1);
        expect(b, "Squall presses the Trigger button at the right time.",
               "a character name and a remappable button, both resolved");

        // The one sprite whose identity the exe does not establish. It is the
        // fixed physical button that hides the battle windows -- Start or
        // Select, and which is not provable -- so it stays unnamed rather than
        // guessed, and the sentence still reads.
        n = Enc::put(t, 0, "Press "); t[n++] = 0x05; t[n++] = 0x38;
        n = Enc::put(t, n, " to hide battle commands."); t[n++] = 0;
        TutExpand(t, n, NAMES, GFS, b, sizeof(b), &info, -1);
        expect(b, "Press a button to hide battle commands.",
               "an unprovable button stays unnamed rather than guessed");

        // An unknown sprite must still be AUDIBLE. Dropping it leaves a
        // sentence with a hole in it and the player answering blind.
        n = Enc::put(t, 0, "This "); t[n++] = 0x05; t[n++] = 0x7E;
        n = Enc::put(t, n, " is odd."); t[n++] = 0;
        TutExpand(t, n, NAMES, GFS, b, sizeof(b), &info, -1);
        expect(b, "This a symbol is odd.", "an unrecognised sprite is announced, never dropped");

        n = 0; t[n++] = 0x0C; t[n++] = 0x62;
        n = Enc::put(t, n, " can learn F Mag-RF."); t[n++] = 0;
        TutExpand(t, n, NAMES, GFS, b, sizeof(b), &info, -1);
        expect(b, "Ifrit can learn F Mag-RF.", "a GF is spoken with the player's own name for it");

        // With no name table the expander must degrade to a word, not to the
        // raw parameter and not to nothing.
        TutExpand(t, n, 0, 0, b, sizeof(b), &info, -1);
        expect(b, "the GF can learn F Mag-RF.", "and without the savemap it degrades to a neutral noun");

        // Colour codes carry nothing a listener can hear, and the answer slots
        // are structure rather than words.
        // **The answer LABELS have to be cut by LINE, not by marker.** The text
        // the game draws is the pre-processed buffer, and 0x004D4A80 does not
        // copy the 0x0B markers into it -- it diverts them to a position array
        // and copies "YES     NO" through as ordinary words. The first choice's
        // pen y over the line height is the line they start on. v0.26.0 read the
        // wrong field entirely and every message window said "the Confirm button
        // to quit"; this is the arithmetic that replaced it.
        n = Enc::put(t, 0, "Your score was 80."); t[n++] = 0x02;
        n = Enc::put(t, n, "You failed."); t[n++] = 0x02;
        n = Enc::put(t, n, "Better luck next time.");
        t[n++] = 0x02; t[n++] = 0x02; t[n++] = 0x02;
        n = Enc::put(t, n, "END"); t[n++] = 0;
        TutExpand(t, n, NAMES, GFS, b, sizeof(b), &info, 5);
        expect(b, "Your score was 80. You failed. Better luck next time.",
               "the labels are cut at the choice line and the message survives whole");
        check(info.labelCount == 1 && strcmp(info.labels[0], "End") == 0,
              "and the cut region is not discarded -- it is where the answer words live");
        TutExpand(t, n, NAMES, GFS, b, sizeof(b), &info, -1);
        check(strstr(b, "END") != nullptr,
              "and with no choices on screen nothing is cut");

        // **Everything after the first answer slot is a LABEL, not the question.**
        // Every stored question ends "...\n\n  <slot0>YES     <slot1>NO", so
        // emitting past that point made all three hundred of them read
        // "...the Gauntlet. YES NO" before the mod then said "Answer Yes".
        n = 0; t[n++] = 0x06; t[n++] = 0x24;
        n = Enc::put(t, n, "Magic uses MP."); t[n++] = 0x02;
        t[n++] = 0x0B; t[n++] = 0x20; n = Enc::put(t, n, "YES     ");
        t[n++] = 0x0B; t[n++] = 0x21; n = Enc::put(t, n, "NO"); t[n++] = 0;
        TutExpand(t, n, NAMES, GFS, b, sizeof(b), &info, -1);
        expect(b, "Magic uses MP.", "a stored 0x0B marker still cuts, for any caller reading the source");
        check(info.choiceCount == 2 && info.firstChoiceIndex == 0,
              "and both slots are still counted, with the first being slot 0 = Yes");
        printf("tutorial text: line breaks, colour, name and GF substitution, and every "
               "symbol class the SeeD exam actually uses\n");

        // -- the Tutorial list ------------------------------------------------
        TutorialView tv;
        memset(&tv, 0, sizeof(tv));
        tv.state = TUT_STATE_LIST; tv.testsPassed = 6; tv.seedRank = 7;
        tv.cursor = 0;
        TutAnnounceRow(tv, b, sizeof(b));
        expect(b, "Battle Operation. Battle Explanation", "the first row, title then help");
        tv.cursor = TUT_ROW_TEST;
        TutAnnounceRow(tv, b, sizeof(b));
        expect(b, "Test. Take Written Test to raise SeeD rank. Next is test 7 of 30",
               "the Test row says which test comes next -- no screen in the game does");
        tv.testsPassed = 30;
        TutAnnounceRow(tv, b, sizeof(b));
        check(strstr(b, "All 30 tests passed") != nullptr, "and stops offering one at the end");
        tv.testsPassed = 0;
        tv.cursor = TUT_ROW_REVIEW;
        TutAnnounceRow(tv, b, sizeof(b));
        check(strstr(b, "Not available") != nullptr,
              "a gated row says so, because confirming it only beeps");
        tv.testsPassed = 1;
        TutAnnounceRow(tv, b, sizeof(b));
        check(strstr(b, "1 test to review") != nullptr, "and one test is singular");
        tv.seedRank = -1; tv.cursor = TUT_ROW_TEST;
        TutAnnounceRow(tv, b, sizeof(b));
        check(strstr(b, "until you are a SeeD") != nullptr, "the rank gate is named too");

        tv.seedRank = 7; tv.testsPassed = 6;
        TutAnnounceStanding(tv, b, sizeof(b));
        expect(b, "SeeD rank 7 of 31. 6 of 30 written tests passed",
               "the standing, which sets the salary and which no screen states plainly");
        tv.testPick = 2;
        TutAnnounceTestPick(tv, b, sizeof(b));
        expect(b, "Test 3 of 6", "the review picker");
        tv.testPick = 8;
        TutAnnounceTestPick(tv, b, sizeof(b));
        check(strstr(b, "not yet passed") != nullptr, "and a row the game will refuse says why");

        // -- the exam ---------------------------------------------------------
        SeedExamView sv;
        memset(&sv, 0, sizeof(sv));
        sv.state = SEED_STATE_QUESTION; sv.testIndex = 6; sv.questionIndex = 3;
        sv.choice = 0; sv.choiceCount = 2;
        snprintf(sv.text, sizeof(sv.text), "%s", "Magic uses MP.");
        SeedAnnounceQuestion(sv, b, sizeof(b));
        expect(b, "Question 4 of 10. Magic uses MP. Answer Yes", "the whole question on arrival");

        // **The stored text opens with its own "Question N".** Speaking both
        // gives "Question 4 of 10. Question 4. Magic uses MP." The mod's label
        // is the one that survives, because it carries "of 10" and the game's
        // does not.
        snprintf(sv.text, sizeof(sv.text), "%s", "Question 4. Magic uses MP.");
        SeedAnnounceQuestion(sv, b, sizeof(b));
        expect(b, "Question 4 of 10. Magic uses MP. Answer Yes",
               "and the game's own duplicate question number is dropped");
        snprintf(sv.text, sizeof(sv.text), "%s", "Questionable premise.");
        SeedAnnounceText(sv, b, sizeof(b));
        expect(b, "Questionable premise.",
               "but only when real digits follow, so a word starting Question survives");
        snprintf(sv.text, sizeof(sv.text), "%s", "Magic uses MP.");
        sv.choice = 1;
        SeedAnnounceChoice(sv, b, sizeof(b));
        expect(b, "No", "and one word when the answer moves");

        // **The answers are not always Yes then No.** Section 95 string 7 -- the
        // "Really?" confirmation -- lists NO first, and other screens offer END
        // or GO BACK. The words come off the screen's own answer line; only the
        // capitalisation is the mod's, because a synth handed "NO" is liable to
        // spell it out.
        sv.labelCount = 2;
        snprintf(sv.labels[0], sizeof(sv.labels[0]), "%s", "No");
        snprintf(sv.labels[1], sizeof(sv.labels[1]), "%s", "Yes");
        sv.choice = 0;
        SeedAnnounceChoice(sv, b, sizeof(b));
        expect(b, "No", "a reversed answer line names cursor 0 as No");
        sv.choice = 1;
        SeedAnnounceChoice(sv, b, sizeof(b));
        expect(b, "Yes", "and cursor 1 as Yes -- the opposite of the hard-coded order");
        sv.labelCount = 0; sv.choice = 1;

        {
            // The splitter itself: a run of two or more spaces separates answers,
            // a single space does not, so "GO BACK" stays one answer.
            TutTextInfo li;
            TutSplitLabels("  YES     NO", &li);
            check(li.labelCount == 2 && strcmp(li.labels[0], "Yes") == 0 &&
                  strcmp(li.labels[1], "No") == 0, "YES/NO splits into two, title-cased");
            TutSplitLabels("  NO      YES", &li);
            check(li.labelCount == 2 && strcmp(li.labels[0], "No") == 0 &&
                  strcmp(li.labels[1], "Yes") == 0, "and the reversed line splits the other way");
            TutSplitLabels("  GO BACK", &li);
            check(li.labelCount == 1 && strcmp(li.labels[0], "Go back") == 0,
                  "a single space is part of the answer, not a separator");
            TutSplitLabels("  END", &li);
            check(li.labelCount == 1 && strcmp(li.labels[0], "End") == 0, "and one answer is one answer");
        }

        // **The running score is deliberately absent.** The game never shows
        // it, and a sighted player cannot know whether the answer they just
        // gave was right -- speaking it would be extra information, which is a
        // different thing from equal access to the same information.
        SeedAnnouncePosition(sv, b, sizeof(b));
        expect(b, "Question 4 of 10, test 7. All ten must be correct to pass",
               "position and the pass condition, and no score");
        sv.reviewMode = true;
        SeedAnnouncePosition(sv, b, sizeof(b));
        check(strstr(b, "review") != nullptr, "review mode is distinguished from a real attempt");

        sv.state = SEED_STATE_RESULT;
        snprintf(sv.message, sizeof(sv.message), "%s", "You passed!");
        sv.choiceCount = 1;
        SeedAnnounceMessage(sv, b, sizeof(b));
        expect(b, "You passed!", "a one-choice message window reads the message alone");
        sv.choiceCount = 2; sv.choice = 0;
        SeedAnnounceMessage(sv, b, sizeof(b));
        expect(b, "You passed!. Yes", "and a two-choice one adds the current answer");
        printf("tutorial menu: seven rows with both silent gates named, the review "
               "picker, and the exam's question, choice and result lines\n");

        // -- v0.27.0: the rest of the Tutorial section ------------------------
        //
        // Aaron, after a screenshot of the seven-row list: *"Take a look at that
        // and all the options / submenus it contains. We need to make sure we
        // account for and make all of these accessible."*
        {
            // The element and status symbols, named by the game's own Icon
            // Explanation pages -- section 89 strings 43, 46 and 47, each of
            // which draws the sprite and writes its name beside it. Reading that
            // page aloud is mildly circular and that is the point: the page IS
            // the legend for every other place these turn up.
            n = 0; t[n++] = 0x05; t[n++] = 0x5D;
            n = Enc::put(t, n, "  Fire"); t[n++] = 0x02;
            t[n++] = 0x05; t[n++] = 0x66;
            n = Enc::put(t, n, "  Poison"); t[n++] = 0;
            TutExpand(t, n, NAMES, GFS, b, sizeof(b), &info, -1);
            expect(b, "Fire symbol Fire Poison symbol Poison",
                   "the element and status legends name their own sprites");

            // 0x41 carries no article because its one sentence supplies one.
            n = Enc::put(t, 0, "The "); t[n++] = 0x05; t[n++] = 0x41;
            n = Enc::put(t, n, " indicates that the magic is junctioned."); t[n++] = 0;
            TutExpand(t, n, NAMES, GFS, b, sizeof(b), &info, -1);
            expect(b, "The junction marker indicates that the magic is junctioned.",
                   "a marker whose sentence supplies the article does not add a second one");

            // The help button is one of the two the Customize screen will not
            // rebind, and which of Start/Select it is is not established -- so it
            // stays unnamed and the sentence still carries its meaning.
            n = Enc::put(t, 0, "Press "); t[n++] = 0x05; t[n++] = 0x3B;
            n = Enc::put(t, n, " to see a help message."); t[n++] = 0;
            TutExpand(t, n, NAMES, GFS, b, sizeof(b), &info, -1);
            expect(b, "Press a button to see a help message.",
                   "and the unprovable help button is still not guessed at");

            // **A wrapped line must not gain a comma, and a legitimate opening
            // quote must not lose its space.** v0.27.0 tried a "short line = list
            // item" rule so the stats legend would read as a list; against the
            // real corpus it wrote "Same Wall uses Battle, Area wall" and "Wall
            // is assumed to have, 'A' value". A wrong comma is a lie about the
            // text; a flat list is only flat.
            n = Enc::put(t, 0, "Same Wall uses Battle"); t[n++] = 0x02;
            n = Enc::put(t, n, "Area wall (the frame)"); t[n++] = 0;
            TutExpand(t, n, NAMES, GFS, b, sizeof(b), &info, -1);
            expect(b, "Same Wall uses Battle Area wall (the frame)",
                   "a 21-character wrapped line is still a wrap");
            n = Enc::put(t, 0, "Wall is assumed to have 'A' value."); t[n++] = 0;
            TutExpand(t, n, NAMES, GFS, b, sizeof(b), &info, -1);
            expect(b, "Wall is assumed to have 'A' value.",
                   "and an opening quote keeps the space in front of it");
            // The space-eating rule exists only for the possessive that a padded
            // name substitution creates.
            n = 0; t[n++] = 0x03; t[n++] = 0x30;
            n = Enc::put(t, n, "'s gunblade."); t[n++] = 0;
            TutExpand(t, n, NAMES, GFS, b, sizeof(b), &info, -1);
            expect(b, "Squall's gunblade.", "while a possessive still closes up");

            // The magazine page.
            MagazineView mv;
            memset(&mv, 0, sizeof(mv));
            mv.first = 43; mv.last = 50; mv.record = 43;
            snprintf(mv.text, sizeof(mv.text), "%s",
                     "Status Window. Battle Tutorial 1/8. A status window is displayed.");
            MagAnnouncePage(mv, b, sizeof(b));
            expect(b, "Battle Operation. Status Window. Battle Tutorial 1/8. "
                      "A status window is displayed.",
                   "page 1 names which of the three magazines opened");
            mv.record = 44;
            MagAnnouncePage(mv, b, sizeof(b));
            check(strstr(b, "Battle Operation.") == nullptr,
                  "and later pages do not repeat it -- the text carries its own counter");
            mv.record = 50;
            MagAnnouncePage(mv, b, sizeof(b));
            check(strstr(b, "Last page") != nullptr,
                  "the last page says so, because Confirm stops turning pages there");
            mv.record = 43; mv.first = 64; mv.last = 67; mv.record = 64;
            MagAnnouncePage(mv, b, sizeof(b));
            check(strstr(b, "Icon Explanation") != nullptr,
                  "and the record range, not the module, is what names the topic");

            // Online Help.
            const char* HN[8] = { "Squall", 0, 0, 0, "Rinoa", 0, 0, 0 };
            TutAnnounceHelpRow(0, HN, 0, 3, b, sizeof(b));
            expect(b, "GF Junction. Junctioning a GF and setting commands. 1 of 3",
                   "an Online Help row, with its position");
            TutAnnounceHelpRow(5, HN, 1, 3, b, sizeof(b));
            check(strstr(b, "Squall's Status Screen") != nullptr,
                  "a row named after a character uses the player's own name");
            TutAnnounceHelpRow(7, HN, 2, 3, b, sizeof(b));
            check(strstr(b, "Rinoa's Status Screen") != nullptr,
                  "including the one that is character id 4, not 0");
            TutAnnounceHelpRow(7, 0, 2, 3, b, sizeof(b));
            check(strstr(b, "the character's Status Screen") != nullptr,
                  "and without the savemap it degrades to a noun rather than a blank");
            TutAnnounceHelpRow(99, HN, 0, 3, b, sizeof(b));
            expect(b, "", "an out-of-range descriptor says nothing at all");
            TutAnnounceHelpArrival(1, b, sizeof(b));
            check(strstr(b, "1 topic.") != nullptr, "one topic is singular");
            printf("tutorial section: the element and status legends, the magazine "
                   "pages, and the Online Help list\n");
        }

        // -- v0.28.0: the Information browser --------------------------------
        {
            TipsView tv;
            memset(&tv, 0, sizeof(tv));
            snprintf(tv.title, sizeof(tv.title), "%s", "Select term");
            tv.linkCount = 4; tv.cursor = 0;
            const char* L[4] = { "Basic Terms", "Elemental", "Status", "Menu" };
            for (int i = 0; i < 4; i++) snprintf(tv.links[i], TIPS_LABEL_MAX, "%s", L[i]);

            TipsAnnouncePage(tv, b, sizeof(b));
            expect(b, "Select term. 4 topics. Basic Terms, 1 of 4",
                   "a link page names itself, counts its topics and reads the one under "
                   "the cursor");
            tv.cursor = 2;
            TipsAnnounceLink(tv, b, sizeof(b));
            expect(b, "Status, 3 of 4", "and moving reads the link alone");

            // **The whole list on one key.** This is the thing a sighted player
            // gets for free -- a column of headings taken in at a glance -- and
            // the one a blind player would otherwise have to arrow through to
            // discover even exists.
            TipsAnnounceLinks(tv, b, sizeof(b));
            expect(b, "4 topics. Basic Terms, Elemental, Status, Menu",
                   "key 2 reads every topic on the page");

            TipsAnnounceNav(tv, b, sizeof(b));
            check(strstr(b, "Topic 3 of 4") != nullptr &&
                  strstr(b, "Cancel leaves Information") != nullptr &&
                  strstr(b, "Left for") == nullptr,
                  "at the root Cancel leaves, and absent siblings are not offered");
            tv.hasParent = true; tv.hasPrev = true; tv.hasNext = true;
            TipsAnnounceNav(tv, b, sizeof(b));
            check(strstr(b, "Cancel goes up a level") != nullptr &&
                  strstr(b, "Left for the previous page") != nullptr &&
                  strstr(b, "Right for the next page") != nullptr,
                  "and below it every route out is named -- including the two the "
                  "game shows only as a '1/2' in the title");

            // **A run of spaces inside a line is a COLUMN, not a gap.** The
            // Battle Report page draws "Walked      109751" in aligned columns;
            // as plain spaces the whole page reads "Walked 109751 Battles 41 Won
            // 35 Escaped 6" -- four labels and four numbers with nothing saying
            // which belongs to which. Aaron's screenshot of that page is what
            // made it concrete.
            n = Enc::put(t, 0, "Walked      109751"); t[n++] = 0;
            TutExpand(t, n, NAMES, GFS, b, sizeof(b), &info, -1);
            expect(b, "Walked, 109751", "an aligned column becomes a comma");
            check(info.columns, "and the caller is told, so it can put a stop after the row");

            // The guard that makes this safe where v0.27.0's line-length rule was
            // not: sentence spacing follows a full stop and is left alone.
            n = Enc::put(t, 0, "disabled in battle.  Death is KO"); t[n++] = 0;
            TutExpand(t, n, NAMES, GFS, b, sizeof(b), &info, -1);
            expect(b, "disabled in battle. Death is KO",
                   "but a double space after a full stop is sentence spacing, not a column");
            check(!info.columns, "and does not claim to be a table row");

            // **Trailing padding is not a column.** 12 of the 2,926 line breaks
            // in the Information corpus have spaces before them, and a comma
            // there lands at a WRAP -- the exact failure that killed the v0.27.0
            // heuristic. The run has to have a word after it on the same line.
            n = Enc::put(t, 0, "GF stands for   "); t[n++] = 0x02;
            n = Enc::put(t, n, "Garden Fighter."); t[n++] = 0;
            TutExpand(t, n, NAMES, GFS, b, sizeof(b), &info, -1);
            expect(b, "GF stands for Garden Fighter.",
                   "padding at the end of a line is still just padding");
            check(!info.columns, "and is not reported as a table row");

            // An indented continuation follows the space a line break emitted,
            // so the character before the run is a space, not a word.
            n = Enc::put(t, 0, "1.Battle area"); t[n++] = 0x02;
            n = Enc::put(t, n, "  Place cards here"); t[n++] = 0;
            TutExpand(t, n, NAMES, GFS, b, sizeof(b), &info, -1);
            expect(b, "1.Battle area Place cards here",
                   "and an indented continuation is not a column either");

            // A prose page: no links, and the sibling note must not double the
            // full stop the prose already ends with.
            memset(&tv, 0, sizeof(tv));
            snprintf(tv.title, sizeof(tv.title), "%s", "Status/About Status");
            snprintf(tv.body, sizeof(tv.body), "%s",
                     "Status signifies status effects, such as Poison and Petrify.");
            tv.hasNext = true; tv.hasParent = true;
            TipsAnnouncePage(tv, b, sizeof(b));
            expect(b, "Status/About Status. Status signifies status effects, such as "
                      "Poison and Petrify. More pages: left and right",
                   "a prose page reads whole, with one stop before the sibling note");
            TipsAnnounceLink(tv, b, sizeof(b));
            expect(b, "", "and a page with no links says nothing when the cursor cannot move");
            TipsAnnounceLinks(tv, b, sizeof(b));
            expect(b, "No topics on this page", "though asking outright still answers");

            // **A page that is genuinely blank has to say so.** "Select name"
            // under Person is nothing but story-gated links, and until you have
            // met somebody the game draws an empty window -- Aaron screenshotted
            // exactly that. A title followed by silence is indistinguishable from
            // the mod having failed.
            memset(&tv, 0, sizeof(tv));
            snprintf(tv.title, sizeof(tv.title), "%s", "Select name");
            TipsAnnouncePage(tv, b, sizeof(b));
            expect(b, "Select name. Nothing here yet",
                   "an empty page says it is empty rather than trailing off");

            // An out-of-range cursor must not read past the label array.
            tv.linkCount = 2; tv.cursor = 7;
            expect(TipsLink(tv, 7), "", "an out-of-range link index is empty, not garbage");
            expect(TipsLink(tv, -1), "", "and so is a negative one");
            printf("information: link pages, prose pages, every route out named, and "
                   "the full topic list on one key\n");
        }

        // -- fuzz ---------------------------------------------------------------
        {
            unsigned seed = 987654321u;
            unsigned char ft[128];
            for (int i = 0; i < 20000; i++) {
                seed = seed * 1103515245u + 12345u;
                const unsigned r = seed >> 8;
                // Random bytes INCLUDING control codes with truncated
                // parameters -- a control byte as the very last byte is the
                // case that walks off the end if the length check is wrong.
                const int len = 1 + (int)(r % (unsigned)sizeof(ft));
                for (int k = 0; k < len; k++) {
                    unsigned v = (r >> (k % 24)) ^ (unsigned)(k * 2654435761u);
                    ft[k] = (unsigned char)(v % 256);
                }
                char fb[640];
                memset(fb, 0xCD, sizeof(fb));
                TutExpand(ft, len, NAMES, GFS, fb, sizeof(fb), &info, -1);
                if (strlen(fb) >= sizeof(fb) - 1) { bad++; printf("  BAD: tutorial text fuzz %d overflowed\n", i); break; }

                TutorialView f;
                memset(&f, 0, sizeof(f));
                f.state = (unsigned short)(r % 36);
                f.cursor = (int)(r % 12) - 2;
                f.testPick = (int)((r >> 7) % 40) - 4;
                f.testsPassed = (int)((r >> 11) % 40) - 4;
                f.seedRank = (int)((r >> 15) % 40) - 4;
                memset(fb, 0xCD, sizeof(fb)); TutAnnounceRow(f, fb, sizeof(fb));
                if (strlen(fb) >= sizeof(fb) - 1) { bad++; printf("  BAD: tutorial row fuzz %d overflowed\n", i); break; }
                memset(fb, 0xCD, sizeof(fb)); TutAnnounceTestPick(f, fb, sizeof(fb));
                memset(fb, 0xCD, sizeof(fb)); TutAnnounceStanding(f, fb, sizeof(fb));
                memset(fb, 0xCD, sizeof(fb)); TutAnnouncePosition(f, fb, sizeof(fb));

                SeedExamView s2;
                memset(&s2, 0, sizeof(s2));
                s2.questionIndex = (int)((r >> 3) % 20) - 3;
                s2.testIndex = (int)((r >> 9) % 40) - 3;
                s2.choice = (int)((r >> 13) % 6) - 2;
                s2.choiceCount = (int)((r >> 17) % 6);
                memcpy(s2.text, fb, 64); s2.text[63] = '\0';
                memcpy(s2.message, fb, 64); s2.message[63] = '\0';
                memset(fb, 0xCD, sizeof(fb)); SeedAnnounceQuestion(s2, fb, sizeof(fb));
                if (strlen(fb) >= sizeof(fb) - 1) { bad++; printf("  BAD: seed question fuzz %d overflowed\n", i); break; }
                memset(fb, 0xCD, sizeof(fb)); SeedAnnounceMessage(s2, fb, sizeof(fb));
                memset(fb, 0xCD, sizeof(fb)); SeedAnnouncePosition(s2, fb, sizeof(fb));

                MagazineView m2;
                memset(&m2, 0, sizeof(m2));
                m2.first  = (int)((r >> 5) % 90) - 5;
                m2.last   = (int)((r >> 9) % 90) - 5;
                m2.record = (int)((r >> 13) % 90) - 5;
                memcpy(m2.text, fb, 64); m2.text[63] = '\0';
                memset(fb, 0xCD, sizeof(fb)); MagAnnouncePage(m2, fb, sizeof(fb));
                if (strlen(fb) >= sizeof(fb) - 1) { bad++; printf("  BAD: magazine fuzz %d overflowed\n", i); break; }
                memset(fb, 0xCD, sizeof(fb)); MagAnnouncePosition(m2, fb, sizeof(fb));
                memset(fb, 0xCD, sizeof(fb));
                TutAnnounceHelpRow((int)((r >> 19) % 14) - 2, NAMES,
                                   (int)((r >> 21) % 14) - 2, (int)((r >> 23) % 14) - 2,
                                   fb, sizeof(fb));
                memset(fb, 0xCD, sizeof(fb)); TutAnnounceHelpArrival((int)((r >> 3) % 14) - 2, fb, sizeof(fb));

                TipsView p2;
                memset(&p2, 0, sizeof(p2));
                p2.record    = (int)((r >> 4) % 600) - 20;
                p2.cursor    = (int)((r >> 11) % 40) - 8;
                p2.linkCount = (int)((r >> 15) % 40) - 8;   // out of range on purpose
                p2.hasPrev   = ((r >> 2) & 1) != 0;
                p2.hasNext   = ((r >> 6) & 1) != 0;
                p2.hasParent = ((r >> 8) & 1) != 0;
                memcpy(p2.title, fb, 32); p2.title[31] = '\0';
                memcpy(p2.body,  fb, 64); p2.body[63]  = '\0';
                for (int k = 0; k < TIPS_MAX_LINKS; k++) {
                    memcpy(p2.links[k], fb, 12); p2.links[k][11] = '\0';
                }
                memset(fb, 0xCD, sizeof(fb)); TipsAnnouncePage(p2, fb, sizeof(fb));
                if (strlen(fb) >= sizeof(fb) - 1) { bad++; printf("  BAD: tips page fuzz %d overflowed\n", i); break; }
                memset(fb, 0xCD, sizeof(fb)); TipsAnnounceLinks(p2, fb, sizeof(fb));
                if (strlen(fb) >= sizeof(fb) - 1) { bad++; printf("  BAD: tips links fuzz %d overflowed\n", i); break; }
                memset(fb, 0xCD, sizeof(fb)); TipsAnnounceLink(p2, fb, sizeof(fb));
                memset(fb, 0xCD, sizeof(fb)); TipsAnnounceNav(p2, fb, sizeof(fb));
                memset(fb, 0xCD, sizeof(fb)); TipsAnnounceAll(p2, fb, sizeof(fb));
            }
            printf("tutorial fuzz: 20,000 random byte strings including truncated control "
                   "codes, plus out-of-range rows, ranks and question numbers\n");
        }
    }

    // =======================================================================
    // ITEM ARRANGE: SWAPPED vs CANCELLED  (v0.29.0, #88)
    // -----------------------------------------------------------------------
    // Both arrange flows return to the same state whether the player confirmed
    // or cancelled, so the mod announced "Swapped" for a cancel. The decision
    // now rests on the id at the armed source slot.
    {
        check(!ItemSwapDecide(ITEM_SWAP_NO_ID, 0x21), "nothing armed is not a swap");
        check(!ItemSwapDecide(0x21, ITEM_SWAP_NO_ID), "an unreadable slot is not a swap");
        check(!ItemSwapDecide(ITEM_SWAP_NO_ID, ITEM_SWAP_NO_ID), "neither readable is not a swap");
        check( ItemSwapDecide(0x21, 0x35), "a different id at the source slot is a swap");
        check(!ItemSwapDecide(0x21, 0x21), "the same id at the source slot is a cancel");
        // The ambiguous case, pinned on purpose: swapping two slots that hold
        // the same item is a no-op, so "Cancelled" stays true of the inventory
        // and "Swapped" would be a claim about a change that did not happen.
        check(!ItemSwapDecide(0x11, 0x11),
              "two slots holding the same item must read as Cancelled");
        check( ItemSwapDecide(0x00, 0x01), "id 0 must be a real id, not a sentinel");
        check(!ItemSwapDecide(0x00, 0x00), "id 0 unchanged is still a cancel");
        printf("item arrange: Swapped only when the armed source slot actually changed\n");
    }

    printf("menu_sim: %s (%d bad)\n", bad ? "FAILED" : "OK", bad);
    return bad ? 1 : 0;
}
