// battle_tts_limit.inl -- the limit-break submenus, read out of the engine.
//
// Included from battle_tts_menu.inl AFTER battle_limit_model.inl (needs the
// kind tables) and after battle_tts_menu_state.inl (needs DecodeFF8String,
// BattleSpeak and the character names). Do not compile independently.
//
// ============================================================================
// v0.36.0 (#94): WHAT THIS READS AND WHY IT IS SAFE TO READ IT
// ============================================================================
//
// The scratch block at 0x01D768D0 is a UNION. The mod already knows this the
// hard way: 0x01D768D4 is the executing party slot during a Draw, a signed
// cursor here, and a function pointer in a limit list. So nothing below is read
// until the block has been positively identified, and "positively" means the
// same standard the module-pool work arrived at -- **two fields, both written
// by the creator, that must agree with each other.**
//
// For the shared list menu (Quistis, Irvine, Rinoa) those are:
//
//   [0x01D768D0] == 0x004C7CD0   the base-address callback, stored by
//                                0x004FF0E0. **This value appears nowhere else
//                                in the executable** -- the only four dword
//                                references to 0x004C7CD0 are the four limit
//                                cases of the command dispatch -- so it is a
//                                unique signature for "a limit list is open",
//                                not merely a plausible one. (v0.14.12 found
//                                the same pointer empirically; this is why.)
//   [0x01D768D4] == the name resolver for [0x01D768E6]'s kind, per the table
//                                in battle_limit_model.inl. Written by
//                                0x004C7D3F/0x004C7DB4/0x004C7E29/0x004C7E8F
//                                in the same call that set the kind byte.
//
// For Selphie's Slot (a different UI entirely, 0x004C7920):
//
//   [0x01D768D0] == charIdx * 464 + 0x01CFF032   (0x004C7960: the list base,
//                                NOT a callback -- which is exactly what tells
//                                the two menus apart)
//   [0x01D768D9] == charIdx, and it must be Selphie
//   [0x01D768DB] <= 0x0A                          the phase jump table's own
//                                bound at 0x004C7454
//
// **NOTHING HERE RE-DERIVES A TABLE.** Names and descriptions come from the
// engine's own resolvers -- the ones it just stored in the scratch block, or
// 0x0047EBD0 for the command's title and 0x0047EC70 for the Slot's two option
// labels. That is the #78 lesson applied: calling the game's function cannot
// get the table math wrong, and it tracks whatever the engine currently has
// loaded. It also means Rinoa's first row comes out as the dog's name from the
// savemap without this file knowing that 0x0047E4F0 special-cases index 0.
// ============================================================================

// --- the scratch block, shared list menu ------------------------------------
static const uint32_t LIM_BASEFN   = 0x01D768D0;  // dword == 0x004C7CD0
static const uint32_t LIM_NAMEFN   = 0x01D768D4;  // dword, name resolver
static const uint32_t LIM_DESCFN   = 0x01D768D8;  // dword, description resolver
static const uint32_t LIM_KIND     = 0x01D768E6;  // byte 0..3
static const uint32_t LIM_SLOT     = 0x01D768EB;  // byte -- PARTY SLOT 0..2
static const uint32_t LIM_CURSOR   = 0x01D768EC;  // byte[ ], indexed by LIM_COLSET
static const uint32_t LIM_COLUMNS  = 0x01D768F2;  // byte (4 for every limit list)
static const uint32_t LIM_VISIBLE  = 0x01D768E5;  // byte, min(count,4) -- rows drawn
static const uint32_t LIM_LASTROW  = 0x01D768F0;  // byte, last usable row
static const uint32_t LIM_PAGES    = 0x01D768F1;  // byte, (lastRow+4)/4
static const uint32_t LIM_COUNT    = 0x01D768F4;  // byte, rows in the list
static const uint32_t LIM_CMDID    = 0x01D768F5;  // byte, the battle command id
static const uint32_t LIM_COLSET   = 0x01D768F6;  // byte, which cursor slot is live
static const uint32_t LIM_USED     = 0x01D76904;  // byte[ ], queued-this-turn per row
static const uint32_t LIM_LIST_BASEFN_VALUE = 0x004C7CD0;

// --- the scratch block, Selphie's Slot ---------------------------------------
static const uint32_t SLOT_LISTPTR   = 0x01D768D0;  // dword, the list base itself
static const uint32_t SLOT_CURSOR    = 0x01D768D8;  // byte 0..1
static const uint32_t SLOT_SLOT      = 0x01D768D9;  // byte -- PARTY SLOT 0..2
static const uint32_t SLOT_PHASE     = 0x01D768DB;  // byte 0..0x0A
static const uint32_t SLOT_MAGICID   = 0x01D768DC;  // byte, the rolled spell
static const uint32_t SLOT_TIMES     = 0x01D768DD;  // byte, how many casts
static const uint32_t SLOT_CMDID     = 0x01D768E0;  // byte, the battle command id
static const uint32_t LIMIT_LIST_ORIGIN = 0x01CFF032;
static const int      LIMIT_LIST_STRIDE = 464;
static const int      SLOT_PHASE_INPUT  = 2;        // 0x004C74D8 sets it here
static const uint32_t LIMIT_PARTY_FORMATION = 0x01CFE74C;  // 3 bytes: slot -> charIdx

// --- engine resolvers we call ------------------------------------------------
typedef char* (__cdecl *LimitNameFn)(int);
typedef char* (__cdecl *LimitBaseFn)(int);
static const uint32_t CMD_NAME_FN  = 0x0047EBD0;   // battle command name
static const uint32_t SLOT_NAME_FN = 0x0047EC70;   // Slot's own string table

// ============================================================================
// Reading the state. Every one of these is SEH-guarded and holds no object
// with a destructor (MSVC C2712 -- tests/lint_seh.py).
// ============================================================================

// Slot -> character, for the log line only. The readers identify themselves
// from the engine's own fields; this is so a BAT log names a person.
static int LimitCharOfSlot(int slot)
{
    int c = -1;
    __try {
        if (LimitPartySlotValid(slot))
            c = *(volatile uint8_t*)(uintptr_t)(LIMIT_PARTY_FORMATION + slot);
    } __except (EXCEPTION_EXECUTE_HANDLER) { c = -1; }
    return c;
}

struct LimitListView {
    bool     valid;
    bool     cursorInRange;
    int      kind;
    int      slot;
    int      charIdx;
    int      cursor;
    int      count;
    int      columns;
    int      cmdId;
    int      colSet;      // 0x01D768F6 -- which of the cursor bytes is live
    int      visible;     // 0x01D768E5 -- rows drawn at once
    int      pages;       // 0x01D768F1 -- pages the renderer was told about
    int      lastUsable;  // 0x01D768F0
    uint32_t nameFn;
    uint32_t descFn;
    const uint8_t* listBase;
};

static bool LimitReadListView(LimitListView* v)
{
    bool ok = false;
    __try {
        if (*(volatile uint32_t*)(uintptr_t)LIM_BASEFN != LIM_LIST_BASEFN_VALUE) return false;

        v->nameFn  = *(volatile uint32_t*)(uintptr_t)LIM_NAMEFN;
        v->descFn  = *(volatile uint32_t*)(uintptr_t)LIM_DESCFN;
        v->kind    = *(volatile uint8_t*)(uintptr_t)LIM_KIND;
        if (!LimitKindAgrees(v->kind, v->nameFn)) return false;

        v->slot    = *(volatile uint8_t*)(uintptr_t)LIM_SLOT;
        if (!LimitPartySlotValid(v->slot)) return false;
        v->count   = *(volatile uint8_t*)(uintptr_t)LIM_COUNT;
        v->columns = *(volatile uint8_t*)(uintptr_t)LIM_COLUMNS;
        v->cmdId   = *(volatile uint8_t*)(uintptr_t)LIM_CMDID;

        const uint8_t colSet = *(volatile uint8_t*)(uintptr_t)LIM_COLSET;
        if (colSet > 3) return false;
        v->colSet = colSet;
        v->cursor = *(volatile uint8_t*)(uintptr_t)(LIM_CURSOR + colSet);
        v->visible    = *(volatile uint8_t*)(uintptr_t)LIM_VISIBLE;
        v->pages      = *(volatile uint8_t*)(uintptr_t)LIM_PAGES;
        v->lastUsable = *(volatile uint8_t*)(uintptr_t)LIM_LASTROW;

        if (v->count <= 0 || v->count > 64) return false;
        // v0.37.2 (#96): a cursor outside the list does NOT mean this is not a
        // limit list -- the 2026-08-20 log caught 0x01D768EC reading 5 with
        // five rows as the window closed. Refusing the whole view there threw
        // the session away, so the next readable frame re-announced the title
        // instead of the row. Identification and cursor validity are different
        // questions; keep the view and mark the row unreadable.
        v->cursorInRange = (v->cursor >= 0 && v->cursor < v->count);

        // The engine's own base-address callback, called the way the engine
        // calls it at 0x004FE28E rather than multiplied out here.
        LimitBaseFn baseFn = (LimitBaseFn)(uintptr_t)LIM_LIST_BASEFN_VALUE;
        v->listBase = (const uint8_t*)baseFn(v->slot);
        if (!v->listBase) return false;

        v->valid = true;
        ok = true;
    } __except (EXCEPTION_EXECUTE_HANDLER) { ok = false; }
    if (ok) v->charIdx = LimitCharOfSlot(v->slot);
    return ok;
}

// One row: its id, what is left of it, and whether it can be chosen.
static bool LimitReadRow(const LimitListView* v, int row,
                         int* outId, int* outRemaining, bool* outSelectable)
{
    bool ok = false;
    __try {
        const uint8_t* e = v->listBase + row * 5;
        const int id    = e[0];
        const int stock = e[1];
        const int flags = e[4];
        const int used  = *(volatile uint8_t*)(uintptr_t)(LIM_USED + row);
        const int rem   = LimitRowRemaining(v->columns, stock, used);
        *outId         = id;
        *outRemaining  = rem;
        *outSelectable = LimitRowSelectable(rem, flags);
        ok = true;
    } __except (EXCEPTION_EXECUTE_HANDLER) { ok = false; }
    return ok;
}

// Call one of the engine's string resolvers and decode what it hands back.
// Returns false rather than a placeholder: a wrong name spoken confidently is
// worse than no name (the Card Mod rule).
static bool LimitResolveText(uint32_t fn, int index, char* out, int outSize)
{
    out[0] = '\0';
    if (!fn) return false;
    const uint8_t* raw = nullptr;
    __try {
        LimitNameFn f = (LimitNameFn)(uintptr_t)fn;
        raw = (const uint8_t*)f(index);
        if (!raw || (uintptr_t)raw < 0x10000 || (uintptr_t)raw > 0x7FFFFFFF) raw = nullptr;
    } __except (EXCEPTION_EXECUTE_HANDLER) { raw = nullptr; }
    if (!raw) return false;
    DecodeFF8String(raw, out, outSize);
    return out[0] != '\0';
}

// The character's LIMIT command id -- the one the game scrolls across row 0
// while the limit toggle is set. 0x004BCEFC reads exactly this byte and hands
// it to 0x0047EBD0; see the derivation in battle_limit_model.inl. Returns -1
// when the slot is out of range or the read faults.
static int LimitCommandIdForSlot(int slot)
{
    int id = -1;
    __try {
        if (LimitPartySlotValid(slot))
            id = *(volatile uint8_t*)(uintptr_t)(LIMIT_CMD_ORIGIN + slot * LIMIT_LIST_STRIDE);
    } __except (EXCEPTION_EXECUTE_HANDLER) { id = -1; }
    return id;
}

// The name the player is looking at on row 0 when the limit toggle is on, in
// the game's own words. False means "say nothing new" -- the caller keeps its
// generic wording rather than inventing one.
static bool LimitCommandNameForSlot(int slot, char* out, int outSize)
{
    out[0] = '\0';
    const int id = LimitCommandIdForSlot(slot);
    if (id < 0) return false;
    return LimitResolveText(CMD_NAME_FN, id, out, outSize);
}

// ============================================================================
// Announcing
// ============================================================================

static bool     s_limInList        = false;
static int      s_limLastCursor    = -1;
static int      s_limLastKind      = -1;
static char     s_limRowDesc[192]  = {};   // what "/" will read
static bool     s_limSlotOpen      = false;
static int      s_limSlotCursor    = -1;
static int      s_limSlotMagic     = -1;
static int      s_limSlotTimes     = -1;
static char     s_limSlotDesc[192] = {};
static bool     s_limSlashWasDown  = false;

// Compose the spoken form of one row. Ammunition is the only kind whose stock
// is a quantity the player spends, so it is the only kind that reads a count --
// Blue Magic and Rinoa's two options carry 1 and would just add a word.
static void LimitComposeRow(const LimitListView* v, int row, char* out, int outSize)
{
    out[0] = '\0';
    int id = 0, remaining = 0;
    bool selectable = false;
    if (!LimitReadRow(v, row, &id, &remaining, &selectable)) return;

    char name[96];
    if (!LimitResolveText(v->nameFn, id, name, sizeof(name)) || name[0] == '\0') {
        // No name from the engine: say the row exists and nothing more. The
        // player can still count rows; he cannot un-hear a wrong name.
        snprintf(out, outSize, "Row %d%s", row + 1, selectable ? "" : ", not available");
        return;
    }

    if (v->kind == LIMIT_KIND_AMMO)
        snprintf(out, outSize, "%s, %d left%s", name, remaining,
                 selectable ? "" : ", not available");
    else
        snprintf(out, outSize, "%s%s", name, selectable ? "" : ", not available");
}

static void LimitCacheRowDescription(const LimitListView* v, int row)
{
    s_limRowDesc[0] = '\0';
    if (row < 0 || row >= v->count) return;
    int id = 0, remaining = 0;
    bool selectable = false;
    if (!LimitReadRow(v, row, &id, &remaining, &selectable)) return;
    LimitResolveText(v->descFn, id, s_limRowDesc, sizeof(s_limRowDesc));
}

// The submenu's title, in the game's own words: the battle command that opened
// it, through 0x0047EBD0. "Blue Magic", "Shot", "Combine" -- not this file's
// idea of what they should be called.
static void LimitComposeTitle(int cmdId, char* out, int outSize)
{
    if (!LimitResolveText(CMD_NAME_FN, cmdId, out, outSize))
        snprintf(out, outSize, "Limit");
}

static void PollLimitList()
{
    LimitListView v = {};
    if (!LimitReadListView(&v)) {
        if (s_limInList) {
            Log::Battle("BattleTTS: [LIMIT] list closed");
            s_limInList = false;
            s_limLastCursor = -1;
            s_limLastKind = -1;
            s_limRowDesc[0] = '\0';
        }
        return;
    }

    // v0.37.3 (#96): THE LIST IS A COLUMN OF FOUR, PAGED -- AND THE LAST PAGE
    // HAS EMPTY CELLS. The 2026-08-20 screenshot shows the window titled
    // "SPECIAL P. 1" with Laser Eye / Ultra Waves / Electrocute / LV?Death in
    // ONE column, and the geometry log agrees: rows=5 vis=4 pages=2 last=4.
    // Pressing Down from row 3 took the cursor to **7**, and Left walked it
    // 7 -> 6 -> 5 -> 4, where 4 is Gatling Gun. So
    //
    //     cursor = page * 4 + rowWithinPage
    //
    // and on the last page every cursor past the end is a cell that exists on
    // screen but holds nothing. **Those cells must SAY they are empty.**
    // Silence there is indistinguishable from the mod having broken -- the
    // v0.28.1 empty-page lesson -- and it is exactly where the stray
    // "Electrocute" was filling the gap with a wrong answer.
    char row[224];
    row[0] = '\0';
    if (v.cursorInRange) LimitComposeRow(&v, v.cursor, row, sizeof(row));
    else if (v.cursor >= 0 && v.cursor < v.pages * 4)
        snprintf(row, sizeof(row), "Empty");

    if (!s_limInList || v.kind != s_limLastKind) {
        // Opened. Lead with the command's own name and the row already under
        // the cursor in ONE utterance -- a separate title line is interrupted
        // by the row that follows it (the v0.33.3 lesson, and battle has an
        // ATB clock running on top of that).
        char title[64];
        LimitComposeTitle(v.cmdId, title, sizeof(title));
        char out[300];
        if (row[0]) snprintf(out, sizeof(out), "%s. %s", title, row);
        else        snprintf(out, sizeof(out), "%s", title);
        BattleSpeak(out, PRIO_MENU, true);
        Log::Battle("BattleTTS: [LIMIT] open kind=%d slot=%d charIdx=%d cmd=%d rows=%d "
                    "cursor=%d col=%d vis=%d pages=%d last=%d inRange=%d -> \"%s\"",
                    v.kind, v.slot, v.charIdx, v.cmdId, v.count, v.cursor,
                    v.colSet, v.visible, v.pages, v.lastUsable, (int)v.cursorInRange, out);
        s_limInList = true;
        s_limLastKind = v.kind;
        s_limLastCursor = v.cursor;
        LimitCacheRowDescription(&v, v.cursor);
        return;
    }

    if (v.cursor != s_limLastCursor) {
        s_limLastCursor = v.cursor;
        if (row[0]) {
            BattleSpeak(row, PRIO_MENU, true);
            Log::Battle("BattleTTS: [LIMIT] row %d/%d col=%d vis=%d pages=%d last=%d -> \"%s\"",
                        v.cursor, v.count, v.colSet, v.visible, v.pages, v.lastUsable, row);
        } else {
            // Kept, not swallowed: a cursor the list cannot explain is exactly
            // what the paging question needs to see.
            Log::Battle("BattleTTS: [LIMIT] row %d UNREADABLE (rows=%d col=%d vis=%d "
                        "pages=%d last=%d) -- past the last page, nothing spoken",
                        v.cursor, v.count, v.colSet, v.visible, v.pages, v.lastUsable);
        }
        LimitCacheRowDescription(&v, v.cursor);
    }
}

// ============================================================================
// Selphie's Slot
// ============================================================================

struct LimitSlotView {
    bool valid;
    int  slot;
    int  charIdx;
    int  phase;
    int  cursor;
    int  magicId;
    int  times;
    int  cmdId;
    const uint8_t* listBase;
};

static bool LimitReadSlotView(LimitSlotView* v)
{
    bool ok = false;
    __try {
        const uint32_t ptr = *(volatile uint32_t*)(uintptr_t)SLOT_LISTPTR;
        if (ptr == LIM_LIST_BASEFN_VALUE) return false;   // that is the OTHER menu

        // v0.36.1: this byte is the PARTY SLOT. v0.36.0 compared it against
        // Selphie's savemap index (5), which no party slot can ever equal, so
        // the Slot window was refused every single time it opened.
        v->slot = *(volatile uint8_t*)(uintptr_t)SLOT_SLOT;
        if (!LimitPartySlotValid(v->slot)) return false;

        // Second identification: the pointer the creator stored must be the
        // list base for the SLOT byte it stored beside it. Two fields, written
        // by the same routine, that have to agree on a 32-bit value.
        const uint32_t expect = (uint32_t)(LIMIT_LIST_ORIGIN + v->slot * LIMIT_LIST_STRIDE);
        if (ptr != expect) return false;

        // Third: the command that opened this window was Slot. 0x01D768E0 is
        // shared with another menu, so on its own it proves nothing -- together
        // with the pointer it means the Slot creator ran.
        v->cmdId = *(volatile uint8_t*)(uintptr_t)SLOT_CMDID;
        if (v->cmdId != SLOT_COMMAND_ID) return false;

        v->phase = *(volatile uint8_t*)(uintptr_t)SLOT_PHASE;
        if (!SlotPhaseValid(v->phase)) return false;

        v->cursor  = *(volatile uint8_t*)(uintptr_t)SLOT_CURSOR;
        if (!SlotCursorValid(v->cursor)) return false;

        v->magicId = *(volatile uint8_t*)(uintptr_t)SLOT_MAGICID;
        v->times   = *(volatile uint8_t*)(uintptr_t)SLOT_TIMES;
        v->listBase = (const uint8_t*)(uintptr_t)ptr;
        v->valid = true;
        ok = true;
    } __except (EXCEPTION_EXECUTE_HANDLER) { ok = false; }
    if (ok) v->charIdx = LimitCharOfSlot(v->slot);
    return ok;
}

// "Cast" / "Do over", and their descriptions, exactly as 0x004C7538 gets them.
static bool LimitSlotOptionText(const LimitSlotView* v, int cursor,
                                bool wantDescription, char* out, int outSize)
{
    out[0] = '\0';
    int entryId = 0;
    __try {
        entryId = v->listBase[SlotOptionEntryOffset(cursor)];
    } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    // v0.36.2: the label is the entry id itself (the row drawer at 0x004C7A92);
    // the explanation is two further on (the help line at 0x004C7538). v0.36.1
    // used id+2 for the label, so every option announced its own description.
    const int idx = wantDescription ? SlotOptionHelpIndex(entryId)
                                    : SlotOptionLabelIndex(entryId);
    return LimitResolveText(SLOT_NAME_FN, idx, out, outSize);
}

static void PollLimitSlot()
{
    LimitSlotView v = {};
    if (!LimitReadSlotView(&v)) {
        if (s_limSlotOpen) {
            Log::Battle("BattleTTS: [LIMIT-SLOT] closed");
            s_limSlotOpen = false;
            s_limSlotCursor = -1;
            s_limSlotMagic = -1;
            s_limSlotTimes = -1;
            s_limSlotDesc[0] = '\0';
        }
        return;
    }

    // Nothing to say until the roll has landed. 0x004C7920 zeroes the phase and
    // the roll only fills 0x01D768DC/DD once phase 0 runs, so an earlier frame
    // would announce a spell of id 0 cast zero times -- the same pre-write
    // frame that made the refine quantity screen read "0" in v0.33.1.
    if (v.phase != SLOT_PHASE_INPUT) return;
    if (!SlotSpellReady(v.magicId, v.times)) return;

    char option[96];
    LimitSlotOptionText(&v, v.cursor, false, option, sizeof(option));

    const bool rolled = (v.magicId != s_limSlotMagic || v.times != s_limSlotTimes);

    if (!s_limSlotOpen || rolled) {
        char title[64];
        char out[320];
        const char* spell = GetMagicName((uint8_t)v.magicId);
        if (!s_limSlotOpen) {
            LimitComposeTitle(v.cmdId, title, sizeof(title));
            snprintf(out, sizeof(out), "%s. %s, %d %s. %s",
                     title, spell, v.times, LimitTimesWord(v.times), option);
        } else {
            snprintf(out, sizeof(out), "%s, %d %s. %s",
                     spell, v.times, LimitTimesWord(v.times), option);
        }
        BattleSpeak(out, PRIO_MENU, true);
        Log::Battle("BattleTTS: [LIMIT-SLOT] %s slot=%d charIdx=%d magic=%d(%s) times=%d cursor=%d -> \"%s\"",
                    s_limSlotOpen ? "rolled" : "open", v.slot, v.charIdx,
                    v.magicId, spell, v.times, v.cursor, out);
        s_limSlotOpen = true;
        s_limSlotMagic = v.magicId;
        s_limSlotTimes = v.times;
        s_limSlotCursor = v.cursor;
        LimitSlotOptionText(&v, v.cursor, true, s_limSlotDesc, sizeof(s_limSlotDesc));
        return;
    }

    if (v.cursor != s_limSlotCursor) {
        s_limSlotCursor = v.cursor;
        if (option[0]) {
            BattleSpeak(option, PRIO_MENU, true);
            Log::Battle("BattleTTS: [LIMIT-SLOT] option %d -> \"%s\"", v.cursor, option);
        }
        LimitSlotOptionText(&v, v.cursor, true, s_limSlotDesc, sizeof(s_limSlotDesc));
    }
}

// "/" reads the description of whatever the cursor is on, the same key that
// reads the help bar everywhere else in the mod. It is only claimed while one
// of these windows is open, so it cannot shadow anything else in battle.
static void PollLimitHelpKey()
{
    const bool down = (GetAsyncKeyState(VK_OEM_2) & 0x8000) != 0;
    const bool pressed = down && !s_limSlashWasDown;
    s_limSlashWasDown = down;
    if (!pressed) return;

    const char* text = nullptr;
    if (s_limInList && s_limRowDesc[0])       text = s_limRowDesc;
    else if (s_limSlotOpen && s_limSlotDesc[0]) text = s_limSlotDesc;
    if (!text) return;

    BattleSpeak(text, PRIO_MENU, true);
    Log::Battle("BattleTTS: [LIMIT] help -> \"%s\"", text);
}

// ============================================================================
// v0.36.1 (#94): "IS A LIMIT WINDOW ON SCREEN RIGHT NOW?"
// ============================================================================
//
// The 2026-08-19 BAT caught the generic command-submenu handler speaking over
// the limit menu:
//
//   [LIMIT]   open kind=3 ... -> "Shot. Normal Ammo, 20 left"
//   [SUBMENU] Exit detected via submenu mode 0x01->0xFE
//   [SUBMENU] Exit announce: Attack (cursor=0, was cmd 0x01)
//
// Opening the limit list looks exactly like leaving a submenu to that handler,
// so it announced the command it thought we were returning to -- "Attack",
// interrupting, ~10 ms after the ammo line. The same shape as the v0.14.37 Item
// bug, and with the same effect: the only thing Aaron actually heard was the
// wrong word.
//
// v0.14.14 already had a guard for the ENTRY path, but it tested
// `s_inLimitSubmenu`, which PollLimitDiag sets AFTER PollTurnAndCommands has
// run -- so on the one frame that matters it is still false. **A flag written
// later in the same frame cannot gate something that runs earlier in it.** This
// predicate reads the engine instead, so it is true the moment the window
// exists regardless of poll order, and it is used on both the entry and the
// exit path.
static bool LimitMenuIsOpenNow()
{
    LimitListView lv = {};
    if (LimitReadListView(&lv)) return true;
    LimitSlotView sv = {};
    if (LimitReadSlotView(&sv)) return true;
    return false;
}

static void PollLimitMenus()
{
    PollLimitList();
    PollLimitSlot();
    PollLimitHelpKey();
}
