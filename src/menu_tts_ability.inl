// menu_tts_ability.inl — Main-menu Ability screen TTS (#42), v0.18.1 chapter
// Included from menu_tts.cpp AFTER menu_tts_gf.inl so it can reuse
// GcwAbilityNames(), NormalizeAbilityToGcw(), GetAbilityName(), ABILITY_NAMES,
// pMenuStateA, and the Log/ScreenReader/FieldDialog/FF8TextDecode facilities.
// Do not compile independently.
//
// The main-menu "Ability" screen (top-level cursor 5) is the "Use GF ability"
// ACTION screen — using a GF menu/command ability (the *-RF refine family plus
// Med LV Up / Card Mod) from the menu. It is NOT a GF-pick / category / AP-learn
// screen — that is the GF screen (#41, done). Confirmed offsets (BAT 2026-06-01,
// isolated SUBMON pass), all relative to pMenuStateA:
//   +0x1E8 == 14         : Ability screen active (gate; analog of GF==4 / Junc==17)
//   +0x22E               : phase — 3 = ability list ; ~19–21 = selected ability's
//                          refine item list (Build 2)
//   +0x258 (+0x257 page) : ability-list cursor (0-based). +0x258 confirmed toggling
//                          0<->1 in lockstep with the help-text swap between the two
//                          abilities; +0x257 read too in case longer lists paginate.
//   +0x2DF               : refine item-list cursor (Build 2)
//
// Build 1 (v0.18.1.0–.3): the ABILITY-LIST phase. Announce the highlighted
// ability NAME on cursor move; the "/" key reads its help; empty padded slots
// say "Empty Ability Slot". Build 2 (v0.18.1.4): the refine ITEM-LIST phase
// (+0x22E >= 19, cursor +0x2DF) — announce the source item's name + quantity on
// move (read from the savemap inventory), and "/" reads the refine preview
// ("N will refine into M <Magic>") parsed from the GCW. Build 2b (v0.18.1.7):
// a settle-based "Refinable / Cannot be refined" tag — on a brief dwell, read
// the engine's refine-result pointer (+0x2BE; non-zero => the current ability
// can refine the highlighted item) and speak the status as a second clip.
// (Memory analysis ruled out any synchronous per-item flag: +0x2BE is the only
// refinability signal and it populates a few frames after the cursor lands,
// hence the dwell. The rendered preview is unreliable per-move — stale text
// bleeds across items — so it is used only for the dwelling "/" reader.)

// Confirmation logging (ability-list parse + item-list inventory/preview cross-
// check). Gate off once Build 2 is confirmed; never delete.
#define ABIL_DIAG 0

// (diag, v0.18.1.8) Refine-flow sub-phase map: logs the phase byte (+0x22E) plus
// the candidate recipient / quantity cursor bytes on any change, across the whole
// refine flow (item list -> character picker -> quantity selector). One pass maps
// every sub-phase's +0x22E value and confirms which byte is each live cursor,
// before Builds 3/4 gate on them. Gate off once mapped; never delete.
#define REFINE_FLOW_DIAG 0

// (diag, retired v0.18.1.13) Recipient magic-stock locator. Job done: the stock
// lives in the SAVEMAP character magic array (base+0x048C + id*152, Magics[32] at
// +0x10), confirmed Water = spell id 10 against the in-game panel. Kept gated off.
#define RECIP_STOCK_DIAG 0

static const int ABIL_GATE_OFFSET   = 0x1E8;   // == 14 on the Ability screen
static const int ABIL_PHASE_OFFSET  = 0x22E;   // 3 = ability list
static const int ABIL_CURSOR_P1_OFF = 0x257;   // ability-list cursor (page 1 candidate)
static const int ABIL_CURSOR_P2_OFF = 0x258;   // ability-list cursor (confirmed)
static const int ABIL_GATE_VALUE    = 14;      // +0x1E8 value on this screen
static const int ABIL_PHASE_LIST    = 3;       // +0x22E value on the ability list
static const int ABIL_PHASE_ITEM_MIN = 19;     // +0x22E >= this = refine item list (Build 2)
static const DWORD ABIL_REFINE_SETTLE_MS = 400;  // dwell before the result ptr is reliable
static const int ABIL_MAGIC_MAX        = 100;   // FF8 per-spell cap

// v0.33.0 (#91) DELETED HERE: the seven pMenuStateA offsets (+0x2BE, +0x2DE,
// +0x2DF, +0x2E0, +0x2E4, +0x2E5, +0x2E7, +0x2E9) this file used to drive the
// refine flow. They were found by SUBMON and every one of them pointed at a real
// byte -- pMenuStateA + 0x296 is the slot-3 module base, so they were module
// +0x28 and +0x48..+0x53. Two were read for something they are not, and the
// v0.32.1 BAT collected the bill on both: see menu_refine_model.inl. The reads
// now come off AbilRefineBase() at offsets taken from the engine's own
// instructions, and are named RFO_* below.

// Menu-usable ("Use GF ability") ability id block: the *-RF refine family (97–113)
// plus Med LV Up (114) and Card Mod (115). Restricting the GCW name match to this
// block avoids colliding with menu-item tokens (GF/Item/Magic/Card = ids 20–25)
// and battle-only command abilities, which matters because these names contain
// internal spaces ("I Mag-RF"). Anything outside this range that ever shows up on
// the screen will surface as an unmatched/short list under ABIL_DIAG.
// v0.29.0 (#88): was 97, and the list really starts at 92.
//
// The engine's own mask builder at 0x004C2B40 accepts every learned GF ability
// in [0x5C, 0x74) -- `cmp eax, 0x5C / jl skip / cmp eax, 0x74 / jge skip` --
// which is **92..115**, and the list builder at 0x004E770F emits `id = 92 +
// bitIndex` for all 24 bits. Ids 92-96 are Haggle, Sell-High, Familiar, Call
// Shop and Junk Shop, all Tonberry abilities and obtainable long before the end
// of the game. With the low bound at 97 the parser's "longest contiguous run of
// ability names" started at the SIXTH real row, so every row was announced as
// the ability five places further down -- a screen that reads as correct while
// naming the wrong thing, which is the worst failure mode for a sole tester.
static const int ABIL_MENU_ID_LO = 92;
static const int ABIL_MENU_ID_HI = 115;

static bool  s_abilActive  = false;   // on the Ability screen (top-level cursor 5)
static int   s_abilLastSel = -1;      // last announced ability-list index (dedupe)
static int   s_abilPrev257 = -1;
static int   s_abilPrev258 = -1;
static DWORD s_abilPoll    = 0;

// (/) on-demand help: the ability currently under the list cursor. Valid only
// while the ability list is up (cleared off it) so "/" falls back to the normal
// help-bar reader everywhere else.
static bool s_abilSelValid     = false;
static char s_abilSelName[64]  = {};
static char s_abilSelDesc[192] = {};

// (Build 2) Refine item-list phase state: cursor dedupe + the refine preview
// stashed for the "/" reader (valid only while the item list is up).
static int   s_abilItemLastCur    = -1;
static DWORD s_abilItemPoll       = 0;
static bool  s_abilItemSelValid   = false;
static char  s_abilItemRefine[192] = {};
static DWORD s_abilItemSettleAt    = 0;     // (2b) tick when the item cursor last moved
static bool  s_abilItemStatusSpoken = true; // (2b) refinable status already spoken this item
// (Build 3) recipient picker + (Build 4) quantity selector dedupe / context.
static int   s_abilRecipLast   = -1;        // last announced recipient char id
static int   s_abilQtyLast     = -1;        // last announced number-to-refine
// (v0.33.2) What was staged in the quantity popup, kept so the OUTCOME can be
// spoken once the popup closes. State 0x2A performs the refine in one frame and
// is never reliably seen; the source's own count across the close is.
static bool  s_abilQtyArmed    = false;
static int   s_abilQtySrcId    = 0;
static int   s_abilQtyOwned    = -1;        // source count while the popup was up
static long  s_abilQtyConsumed = 0;
static long  s_abilQtyProduced = 0;
static long  s_abilQtyTotal    = -1;
static char  s_abilQtySrcName[48] = {};
static char  s_abilQtyResName[48] = {};
static int   s_abilYieldIn     = 1;         // per-recipe input count ("<in> will refine into..")
static int   s_abilYieldOut    = 0;         // per-recipe output count ("..into <out> <Magic>")
static char  s_abilYieldMagic[48] = {};     // resulting magic name (e.g. "Waters")
static DWORD s_abilRecipSettleAt    = 0;    // (stock) tick when the recipient last changed
static bool  s_abilRecipStockSpoken = true; // (stock) "has N <Magic>" already spoken this recipient

static void ResetAbilitySubmenuState()
{
    s_abilActive  = false;
    s_abilLastSel = -1;
    s_abilPrev257 = -1;
    s_abilPrev258 = -1;
    s_abilPoll    = 0;
    s_abilSelValid    = false;
    s_abilSelName[0]  = '\0';
    s_abilSelDesc[0]  = '\0';
    s_abilItemLastCur   = -1;
    s_abilItemPoll      = 0;
    s_abilItemSelValid  = false;
    s_abilItemRefine[0] = '\0';
    s_abilItemSettleAt     = 0;
    s_abilItemStatusSpoken = true;
    s_abilRecipLast   = -1;
    s_abilQtyLast     = -1;
    s_abilQtyArmed    = false;
    s_abilQtyOwned    = -1;
    s_abilYieldIn     = 1;
    s_abilYieldOut    = 0;
    s_abilYieldMagic[0] = '\0';
    s_abilRecipSettleAt    = 0;
    s_abilRecipStockSpoken = true;
}

// SEH raw read of the gate / phase / cursor bytes. No C++ objects (C2712-safe).
static bool AbilReadState(int* gate, int* phase, int* cur257, int* cur258)
{
    __try {
        uint8_t* ms = (uint8_t*)pMenuStateA;
        if (ms == nullptr) return false;
        *gate   = (int)ms[ABIL_GATE_OFFSET];
        *phase  = (int)ms[ABIL_PHASE_OFFSET];
        *cur257 = (int)ms[ABIL_CURSOR_P1_OFF];
        *cur258 = (int)ms[ABIL_CURSOR_P2_OFF];
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}

// Parse the displayed ability list out of the decoded GCW. The menu-ability
// names (ids 97–115) appear ONLY in the ability list — never in the menu bar
// ("GF"/"Item"/"Magic"/"Card" are ids 20–25, excluded) nor in the help text —
// so the list is simply the longest contiguous run of those names in the
// buffer. We FORWARD-SCAN and keep the rightmost-longest run; no reliance on a
// menu-token anchor (the menu bar scrolls — when the cursor sits on Ability the
// bar renders "GFAbilitySwitchCardConfigTutorialSave...", with Junction/Item/
// Magic/Status scrolled off, so an earlier "Junction"-anchored parse found
// nothing). Fills outIds in display order; *outListStart = decoded index where
// row 0 begins (used to slice the preceding help text). std::string -> no __try.
static int ParseAbilityList(const std::string& dec, uint8_t outIds[], int maxOut,
                            size_t* outListStart)
{
    *outListStart = std::string::npos;
    const std::vector<std::string>& names = GcwAbilityNames();

    uint8_t best[64]; int bestN = 0; size_t bestStart = std::string::npos;
    size_t i = 0;
    while (i < dec.size()) {
        // Longest menu-ability name matching at i (decides whether a run starts).
        int    mid = -1; size_t mlen = 0;
        for (int id = ABIL_MENU_ID_LO; id <= ABIL_MENU_ID_HI; id++) {
            const std::string& nm = names[id];
            size_t len = nm.size();
            if (len == 0 || len <= mlen || i + len > dec.size()) continue;
            if (dec.compare(i, len, nm) == 0) { mid = id; mlen = len; }
        }
        if (mid < 0) { i++; continue; }
        // Extend a run of back-to-back ability names from here.
        uint8_t run[64]; int rn = 0; size_t runStart = i;
        size_t j = i;
        while (j < dec.size() && rn < 64) {
            int    rid = -1; size_t rlen = 0;
            for (int id = ABIL_MENU_ID_LO; id <= ABIL_MENU_ID_HI; id++) {
                const std::string& nm = names[id];
                size_t len = nm.size();
                if (len == 0 || len <= rlen || j + len > dec.size()) continue;
                if (dec.compare(j, len, nm) == 0) { rid = id; rlen = len; }
            }
            if (rid < 0) break;
            run[rn++] = (uint8_t)rid;
            j += rlen;
        }
        if (rn >= bestN) {                 // rightmost run of the max length wins
            bestN = rn; bestStart = runStart;
            for (int k = 0; k < rn; k++) best[k] = run[k];
        }
        i = (j > i) ? j : i + 1;
    }
    if (bestN <= 0) return 0;
    *outListStart = bestStart;
    int count = (bestN < maxOut) ? bestN : maxOut;
    for (int k = 0; k < count; k++) outIds[k] = best[k];
    return count;
}

// v0.29.0 (#88): the ability list as the ENGINE holds it -- a flat array of ids
// with the count immediately after it, both written by 0x004E770F:
//     edx = 0x01D8CB54; for (bit = 0; bit < 24; bit++)
//         if (mask & (1 << bit)) *edx++ = 92 + bit;
//     [0x01D8CB6C] = written count;
// The cursor at +0x258 indexes this directly, across pages. No SEH/std::string
// mixing: this touches neither.
static const uintptr_t ABIL_ENGINE_IDS   = 0x01D8CB54;
static const uintptr_t ABIL_ENGINE_COUNT = 0x01D8CB6C;

static bool AbilReadEngineList(uint8_t* out, int maxOut, int& outCount)
{
    outCount = 0;
    __try {
        const int n = (int)*(volatile uint8_t*)ABIL_ENGINE_COUNT;
        if (n <= 0 || n > 24) return false;
        const uint8_t* src = (const uint8_t*)ABIL_ENGINE_IDS;
        const int lim = (n < maxOut) ? n : maxOut;
        for (int i = 0; i < lim; i++) {
            const uint8_t id = src[i];
            // The array only ever holds 92..115; anything else means the buffer
            // is not populated yet and the parse is the safer source.
            if (id < 92 || id > 115) return false;
            out[i] = id;
        }
        outCount = lim;
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}

// ===========================================================================
// v0.32.0 (#91): WHERE THE REFINE SCREEN ACTUALLY LIVES, AND WHICH ABILITIES
// GO SOMEWHERE ELSE ENTIRELY
// ---------------------------------------------------------------------------
// Aaron: *"I want to ensure that all of the various abilities are accessible...
// I don't have access to all of the abilities in-game and it isn't realistic to
// BAT each and every one."*
//
// So the Ability screen was read out of the exe instead. Picking a row runs
// 0x004E7990, which looks the ability up in the descriptor table at
// `0x01CF7F28 + id*8` and branches on the TYPE byte at `+5`:
//
//   0xFF   nothing happens at all
//   0x81   -> [esi+0x3C] = 0x0C, state 0x12: a MODAL MESSAGE (dispatch 12,
//          module 0x004EA890) -- "can't use that here"
//   0x80   -> the SHOP. [0x01D8CB6D] decides: zero opens it (state 0x10),
//          non-zero refuses with a beep (state 8). Call Shop and Junk Shop.
//   else   -> [esi+0x3D] = the ability id, [esi+0x3C] = 0x13, state 0x17:
//          push dispatch 19 -- **the REFINE screen is its own module**,
//          creator 0x004D7180, update fn 0x004D7410, draw 0x004D90E0.
//
// So this file drives a screen that is NOT the Ability module. Its fields, read
// from its own state machine rather than guessed:
//
//   +0x10  state (0..0x2C)
//   +0x45  sub-mode (0..4, jump table at 0x004D8B40)
//   +0x48  the chosen character id  (0x004D7589, via 0x004AD030/0x004ABC40)
//   +0x49  the SOURCE-LIST cursor -- ABSOLUTE; the row on screen is
//          cursor % 11 (0x004D75A7), the same 11-a-page layout as everywhere
//   +0x4A  the character-picker cursor, packed over available characters
//
// Those are exactly the bytes this file has been reading as pMenuStateA+0x2DE,
// +0x2DF and +0x2E0 -- pool slot 3 plus 0x48/0x49/0x4A. **The offsets were
// right and the reason was never written down**, which is how the GF screen's
// "two cursors" stayed wrong for eight builds.
// ===========================================================================

static const uintptr_t ABIL_POOL_BASE   = 0x01D76BC8;
static const uintptr_t ABIL_POOL_END    = 0x01D77078;
static const uintptr_t ABIL_LIST_HEAD   = 0x01D76B48;
static const uint32_t  ABIL_REFINE_FN   = 0x004D7410;   // creator 0x004D7180
static const uintptr_t ABIL_SLOT3_ALIAS = 0x01D76D30;   // pool base + 3 * 0x78
static const int       ABIL_REFINE_STATE_MAX = 0x2C;    // 0x004D7432

// The ability id the Ability module handed to the refine screen: its own
// +0x3D, i.e. pMenuStateA + 0x21E + 0x3D.
static const int ABIL_CHOSEN_ID_OFF = 0x25B;

// Same shape as the Item screen's identification: try the walk, fall back to the
// slot alias, and require BOTH to agree with something already known -- here,
// that the module's state word is inside the refine machine's range. The walk is
// not assumed to work; on the Item module it provably cannot.
static uint8_t* AbilRefineBase()
{
    __try {
        uint8_t* m = *(uint8_t* volatile*)ABIL_LIST_HEAD;
        for (int i = 0; i < 12 && m; i++) {
            const uintptr_t a = (uintptr_t)m;
            if (a < ABIL_POOL_BASE || a >= ABIL_POOL_END) break;
            if ((a - ABIL_POOL_BASE) % 0x78 != 0) break;
            if (*(uint32_t*)(m + 0x08) == ABIL_REFINE_FN) return m;
            m = *(uint8_t* volatile*)m;
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) { /* fall through */ }
    __try {
        uint8_t* alias = (uint8_t*)ABIL_SLOT3_ALIAS;
        if (alias[0x12] == 0) return nullptr;                   // slot not in use
        const int st = (int)*(volatile uint16_t*)(alias + 0x10);
        if (st < 0 || st > ABIL_REFINE_STATE_MAX) return nullptr;
        return alias;
    } __except(EXCEPTION_EXECUTE_HANDLER) { return nullptr; }
}

// Which ability opened this screen. -1 when it cannot be read.
static int AbilChosenId()
{
    __try {
        uint8_t* ms = (uint8_t*)pMenuStateA;
        if (!ms) return -1;
        const int id = (int)ms[ABIL_CHOSEN_ID_OFF];
        return (id >= ABIL_MENU_ID_LO && id <= ABIL_MENU_ID_HI) ? id : -1;
    } __except(EXCEPTION_EXECUTE_HANDLER) { return -1; }
}

static const int ABIL_ID_CARD_MOD = 115;

// ===========================================================================
// THE REFINE MODULE'S OWN FIELDS  (v0.33.0, #91)
// ---------------------------------------------------------------------------
// v0.32.x read this screen through pMenuStateA offsets found by SUBMON. They
// pointed at the right bytes -- pMenuStateA + 0x296 IS the slot-3 module base,
// so +0x2DE..+0x2E9 are module +0x48..+0x53 -- but two of them were read for
// something they are not, and the v0.32.1 BAT cashed in both:
//
//   +0x2E7 was called "1 = quantity screen". It is +0x4F, the NUMBER TO REFINE,
//          which merely happens to be 1 when the popup opens.
//   +0x2E9 was called "255 = item list, 0 = recipient flow". It is +0x53, and
//          the engine only clears it at state 0x1A -- **the recipient picker,
//          which exists on sub-mode 0 alone.**
//
// So the reads are now taken off the module base, at offsets that come from the
// engine's own instructions, and the flow is driven by the state machine.
// ===========================================================================
static const int RFO_STATE      = 0x10;   // u16, jump table 0x004D8A8C
static const int RFO_RECIPES    = 0x24;   // recipe table ptr (stride 8)
static const int RFO_RESULTPTR  = 0x28;   // decoded result name, 0 = not refinable
static const int RFO_SRCLIST    = 0x30;   // source list base (items or cards)
static const int RFO_ABILITY    = 0x44;   // the ability id that opened the screen
static const int RFO_SUBMODE    = 0x45;   // descriptor byte +5 (0x004D71DF)
static const int RFO_RECIPE_N   = 0x46;   // recipe count (0x004D71F4)
static const int RFO_LEVEL      = 0x47;   // the player's refine level (0x004D7274)
static const int RFO_CHARID     = 0x48;   // resolved character id (0x004D7589)
static const int RFO_SRCCUR     = 0x49;   // source cursor / sub-mode 2 char cursor
static const int RFO_PICKCUR    = 0x4A;   // recipient cursor / magic-grid cursor
static const int RFO_QTY_MAX    = 0x4C;   // most that can be refined (0x004D89F1)
static const int RFO_RECIPE_IDX = 0x4D;   // recipe index (0x004D7A43)
static const int RFO_QTY        = 0x4F;   // number to refine (0x004D7980 etc.)
static const int RFO_QTY_OPEN   = 0x51;   // 1 while the popup is up (0x004D89F8)
static const int RFO_PAGES      = 0x52;   // source-list page count

static const uintptr_t RF_CHAR_MAGIC_BASE   = 0x01CFE0F8;  // 0x004D8CDD
static const int       RF_CHAR_MAGIC_STRIDE = 152;
static const int       RF_CHAR_MAGIC_SLOTS  = 32;          // 0x004D7BEE
static const uintptr_t RF_CARD_LIST         = 0x01D8B064;  // built at 0x004D7344
static const int       RF_CARD_SLOTS        = 110;

// Everything the pollers need, read once per poll behind one SEH frame. POD
// only -- C2712 forbids a std::string anywhere near a __try.
struct AbilRefineView
{
    bool ok;
    int  state, submode, abilityId, charId, srcCur, pickCur;
    int  qtyMax, qty, qtyOpen, recipeIdx, recipeN, level;
    uint32_t recipes, resultPtr, srcList;
    bool haveRecipe;
    RefineRecipe recipe;
};

static bool AbilReadRefineView(AbilRefineView* v)
{
    uint8_t* b = AbilRefineBase();
    if (!b) { v->ok = false; return false; }
    __try {
        v->state     = (int)*(volatile uint16_t*)(b + RFO_STATE);
        v->submode   = (int)b[RFO_SUBMODE];
        v->abilityId = (int)b[RFO_ABILITY];
        v->charId    = (int)b[RFO_CHARID];
        v->srcCur    = (int)b[RFO_SRCCUR];
        v->pickCur   = (int)b[RFO_PICKCUR];
        v->qtyMax    = (int)b[RFO_QTY_MAX];
        v->qty       = (int)(int8_t)b[RFO_QTY];
        v->qtyOpen   = (int)b[RFO_QTY_OPEN];
        v->recipeIdx = (int)b[RFO_RECIPE_IDX];
        v->recipeN   = (int)b[RFO_RECIPE_N];
        v->level     = (int)b[RFO_LEVEL];
        v->recipes   = *(volatile uint32_t*)(b + RFO_RECIPES);
        v->resultPtr = *(volatile uint32_t*)(b + RFO_RESULTPTR);
        v->srcList   = *(volatile uint32_t*)(b + RFO_SRCLIST);
        v->haveRecipe = false;
        if (v->recipes && v->recipeIdx >= 0 && v->recipeIdx < v->recipeN && v->recipeN <= 255) {
            uint8_t* r = (uint8_t*)(uintptr_t)v->recipes + v->recipeIdx * 8;
            v->recipe.resultPer = (int)*(volatile uint16_t*)(r + 2);
            v->recipe.level     = (int)r[4];
            v->recipe.sourceId  = (int)r[5];
            v->recipe.sourcePer = (int)r[6];
            v->recipe.resultId  = (int)r[7];
            v->haveRecipe = true;
        }
        v->ok = (v->submode >= 0 && v->submode <= RF_SUB_MAX &&
                 v->state >= 0 && v->state <= RF_ST_MAX);
        return v->ok;
    } __except(EXCEPTION_EXECUTE_HANDLER) { v->ok = false; return false; }
}

// A source row, whatever kind of list it is. qty 0 / id 0 means an empty slot.
static bool AbilReadSourceRow(const AbilRefineView& v, int idx, int* id, int* qty)
{
    const RefineSourceKind k = RefineSourceKindOf(v.submode);
    __try {
        if (k == RF_SRC_MAGIC) {
            if (idx < 0 || idx >= RF_CHAR_MAGIC_SLOTS) return false;
            if (v.charId < 0 || v.charId > 7) return false;
            uint8_t* m = (uint8_t*)RF_CHAR_MAGIC_BASE + v.charId * RF_CHAR_MAGIC_STRIDE;
            *id  = (int)m[idx * 2];
            *qty = (int)m[idx * 2 + 1];
            return true;
        }
        if (k == RF_SRC_CARDS) {
            if (idx < 0 || idx >= RF_CARD_SLOTS) return false;
            uint8_t* c = (uint8_t*)RF_CARD_LIST;
            const int raw = (int)c[idx * 2];
            *id  = raw ? raw - 1 : -1;      // the creator stores cardId + 1
            *qty = (int)c[idx * 2 + 1];
            return true;
        }
        if (idx < 0 || idx >= 198) return false;
        uint8_t* inv = (uint8_t*)SAVEMAP_BASE + ITEM_INVENTORY_OFFSET;
        *id  = (int)inv[idx * 2];
        *qty = (int)inv[idx * 2 + 1];
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}

// How many of the refine's RESULT the destination already holds -- the number
// the popup's third row will land on once the count is added. Magic lives in the
// character's 32 slots (scanned exactly as 0x004C2C8D does); items in the 198
// inventory slots (0x0047ED18). Returns 0 when the result is not held at all,
// and -1 only when nothing could be read.
static int AbilResultStock(const AbilRefineView& v)
{
    if (!v.haveRecipe) return -1;
    const bool magic = RefineResultIsMagic(v.submode);
    __try {
        if (magic) {
            if (v.charId < 0 || v.charId > 7) return -1;
            uint8_t* m = (uint8_t*)RF_CHAR_MAGIC_BASE + v.charId * RF_CHAR_MAGIC_STRIDE;
            for (int i = 0; i < RF_CHAR_MAGIC_SLOTS; i++)
                if (m[i * 2] == (uint8_t)v.recipe.resultId) return (int)m[i * 2 + 1];
            return 0;
        }
        uint8_t* inv = (uint8_t*)SAVEMAP_BASE + ITEM_INVENTORY_OFFSET;
        for (int i = 0; i < 198; i++)
            if (inv[i * 2] == (uint8_t)v.recipe.resultId) return (int)inv[i * 2 + 1];
        return 0;
    } __except(EXCEPTION_EXECUTE_HANDLER) { return -1; }
}

// The character mask the picker is indexed against, rebuilt exactly as
// 0x004AD030 builds it: bit i set when character i's savemap "exists" byte is
// odd, narrowed to the battle formation when [0x01CFE97A] & 1. Reproduced
// rather than called -- the mod does not call into the game from its own thread.
static int AbilCharMask()
{
    __try {
        int mask = 0;
        for (int i = 0; i < 8; i++)
            if (((uint8_t*)0x01CFE17C)[i * 0x98] & 1) mask |= (1 << i);
        if (((uint8_t*)0x01CFE97A)[0] & 1) {
            int form = 0;
            for (int i = 0; i < 3; i++) {
                const uint8_t c = ((uint8_t*)0x01CFE74C)[i];
                if (c != 0xFF && c < 8) form |= (1 << c);
            }
            mask &= form;
        }
        return mask;
    } __except(EXCEPTION_EXECUTE_HANDLER) { return 0; }
}

// The nth set bit of the mask == the character on picker row n (0x004ABC40).
static int AbilCharAtPickerRow(int mask, int row)
{
    if (row < 0) return -1;
    for (int i = 0; i < 32; i++) {
        if (mask & (1 << i)) {
            if (row == 0) return i;
            row--;
        }
    }
    return -1;
}

// v0.32.0 suppressed Card Mod's row names ("Card 1", "Card 2") because the card
// list's order could not be checked against any save Aaron has. **It can be
// checked against the ENGINE.** The creator at 0x004D7344 builds the list
// itself -- it walks card ids 0..0x6D and writes {cardId + 1, count} into
// 0x01D8B064 for every card owned -- so AbilReadSourceRow() names the row out of
// the existing 110-card table with no guess anywhere. AbilSourceListIsItems() is
// gone with the suppression it existed to drive; sub-mode, not ability id, now
// says what a row is.

// SEH read of savemap inventory slot `idx` (item id + quantity). The refine
// source-item list appears to be the full inventory in order; the ABIL_DIAG
// cross-check logs the GCW so the BAT can confirm this mapping. C2712-safe.
static bool AbilReadInvSlot(int idx, uint8_t* id, uint8_t* qty)
{
    __try {
        uint8_t* inv = (uint8_t*)SAVEMAP_BASE + ITEM_INVENTORY_OFFSET;
        *id  = inv[idx * 2];
        *qty = inv[idx * 2 + 1];
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}

#if ABIL_DIAG
// (diag, v0.18.1.6) Hex dump of the FULL menu-state struct (+0x000..+0x3FF) on
// each item-cursor move, in two 512-byte halves (keeps log lines a safe length).
// Widened from the +0x200 window (v0.18.1.5), which contained no per-item
// refinable flag: this lets us scan the whole struct for either a static
// per-row refinable array or a per-cursor flag. SEH wraps only the raw copy.
static void AbilDumpMenuWindow(int cur)
{
    uint8_t buf[1024];
    bool ok = false;
    __try {
        uint8_t* ms = (uint8_t*)pMenuStateA;
        if (ms) { memcpy(buf, ms, sizeof(buf)); ok = true; }
    } __except(EXCEPTION_EXECUTE_HANDLER) { ok = false; }
    if (!ok) return;
    char hex[512 * 2 + 1];
    for (int half = 0; half < 2; half++) {
        int base = half * 512;
        for (int i = 0; i < 512; i++) sprintf(hex + i * 2, "%02X", buf[base + i]);
        Log::Menu("[ABILDIAG-WIN] cur=%d base=0x%03X %s", cur, base, hex);
    }
}
#endif

// v0.32.1 (#91) The result of a refine is always a NAMED THING -- an item on
// the item-output recipes, a spell on the magic ones -- and the sentence ends
// exactly where that name ends. Matching the LONGEST name from the game's own
// tables at the position right after "into <count> " gives the cut precisely,
// with no dependence on what the screen happens to draw next. Returns npos when
// nothing matches, so the caller can fall back to bounding by the layout.
static const int ABIL_ITEM_ID_MAX = 198;

static size_t RefineResultEnd(const std::string& dec, size_t after)
{
    size_t q = after;
    while (q < dec.size() && dec[q] == ' ') q++;
    while (q < dec.size() && dec[q] >= '0' && dec[q] <= '9') q++;
    while (q < dec.size() && dec[q] == ' ') q++;
    if (q >= dec.size()) return std::string::npos;

    size_t best = 0;
    for (int id = 1; id <= ABIL_ITEM_ID_MAX; id++) {
        const char* nm = GetItemName(id);
        if (!nm || !nm[0]) continue;
        const size_t L = strlen(nm);
        if (L > best && dec.compare(q, L, nm) == 0) best = L;
    }
    for (int id = 1; id < MAGIC_SPELL_NAME_COUNT; id++) {
        const char* nm = MAGIC_SPELL_NAMES[id];
        if (!nm || !nm[0]) continue;
        const size_t L = strlen(nm);
        if (L > best && dec.compare(q, L, nm) == 0) best = L;
    }
    if (!best) return std::string::npos;

    size_t e = q + best;
    if (e < dec.size() && dec[e] == 's') e++;   // "30 Shell Stones", "10 Curagas"
    return e;
}

// Extract the refine preview ("N will refine into M <result>") from the decoded
// GCW for the "/" reader. Anchor on "will refine into", walk back over the
// leading "<count> " digits, and cut at the end of the result's own name.
//
// v0.32.1 (#91): the cut used to be "stop at the first party-name marker",
// which held only because the magic-refine screens draw the recipient panel
// next. An ITEM-output recipe has no recipient, so no party name is ever drawn
// and the slice ran to the end of the buffer -- the v0.32.0 BAT read Tool-RF as
//   "1 will refine into 30 Shell StonesCoral FragmentBetrayal SwordDead Spirit..."
// and Ammo-RF as
//   "100 will refine into 1 Dark MatterZombie PowderPet Pals Vol.2..."
// with the whole drawn source list glued on. RefineResultEnd() now ends the
// sentence at the result itself; the layout bounds below remain as the fallback
// for a result neither table knows.
//
// Empty when the highlighted item can't be refined (no preview rendered).
// std::string -> no __try.
static std::string ParseRefinePreview(const std::string& dec, int cursor)
{
    size_t w = dec.rfind("will refine into");
    if (w == std::string::npos) return std::string();
    size_t s = w;
    while (s > 0 && dec[s - 1] == ' ') s--;
    while (s > 0 && dec[s - 1] >= '0' && dec[s - 1] <= '9') s--;

    const size_t after = w + 16;             // just past "will refine into"

    // The precise cut: the end of the result's own name. When the tables know
    // the result, nothing about the surrounding layout matters.
    size_t e = RefineResultEnd(dec, after);
    if (e != std::string::npos) {
        if (e <= s) return std::string();
        std::string named = dec.substr(s, e - s);
        while (!named.empty() && named.back() == ' ') named.pop_back();
        return named;
    }

    // Otherwise fall back to bounding by what the screen draws after the
    // sentence, which is one of two things depending on the recipe.
    e = dec.size();

    // (a) The party panel, which is what the magic-refine screens put next.
    for (const char** m = HELP_END_MARKERS; *m; m++) {
        size_t p = dec.find(*m, after);
        if (p != std::string::npos && p < e) e = p;
    }

    // (b) v0.32.1 (#91): **the source list, which is what the ITEM-refine
    //     screens put next -- and they have no party panel at all.**
    //
    // The v0.32.0 BAT read Tool-RF's preview as
    //     "1 will refine into 30 Shell StonesCoral FragmentBetrayal Sword..."
    // and Ammo-RF's as
    //     "100 will refine into 1 Dark MatterZombie PowderPet Pals Vol.2..."
    // -- the whole drawn source list glued onto the end of the sentence. An
    // item-output recipe has no recipient, so no party name is ever drawn, so
    // (a) found no marker and the slice ran to the end of the buffer.
    //
    // The rows that follow are the source list's CURRENT PAGE, and this file
    // already knows which rows those are: eleven per page from (cursor/11)*11,
    // read out of the savemap inventory. Cutting at the first of them that
    // appears after the sentence bounds the preview by the screen's own layout
    // instead of by a guess about wording.
    if (cursor >= 0) {
        const int first = (cursor / 11) * 11;
        for (int r = first; r < first + 11; r++) {
            uint8_t iid = 0, iq = 0;
            if (!AbilReadInvSlot(r, &iid, &iq) || iid == 0) continue;
            const char* nm = GetItemName(iid);
            if (!nm || !nm[0]) continue;
            const size_t p = dec.find(nm, after);
            // Require something between "into" and the row name, so a result
            // whose own name starts with a row name cannot cut to nothing.
            if (p != std::string::npos && p > after + 2 && p < e) e = p;
        }
    }

    if (e <= s) return std::string();
    std::string out = dec.substr(s, e - s);
    while (!out.empty() && out.back() == ' ') out.pop_back();
    return out;
}

// FF8 character ids (the value held in +0x2DE). Squall/Rinoa are renameable; the
// defaults here are used until/unless a savemap name read is added. std::string-free.
static const char* REFINE_CHAR_NAMES[] = {
    "Squall", "Zell", "Irvine", "Quistis", "Rinoa",
    "Selphie", "Seifer", "Edea", "Laguna", "Kiros", "Ward"
};

// v0.49.0 (#107): **INDEXING THAT TABLE BY THE RAW ID IS WRONG IN A DREAM.**
// The 2026-08-21 screenshot of L Mag-RF has Laguna / Ward / Kiros in the NAME
// panel; the mod said "Squall" / "Irvine" / "Selphie". The character ids (0, 2,
// 5) were right -- during a Laguna dream the dream member's record is loaded
// into char-data[id], and it is the record's MODEL byte that says who that is.
// Every other party list in the mod already knows this (Junction v0.17.8.17.7,
// Item, the battle magic list v0.45.0); the refine picker was the last one
// reading the table straight.
//
// The offsets are spelled out here rather than borrowed from
// ResolveDreamAwareCharId, which is the same rule in menu_tts_diagnostics.inl:
// tests/menu_ability_compile.cpp maps a real savemap and can therefore check
// THIS function, and menu_tts_junction.inl makes the same local copy for the
// same kind of reason. If a third copy ever appears, they belong in one file.
static const int REFINE_CHARS_OFF   = 0x48C;   // savemap -> character records
static const int REFINE_CHAR_STRIDE = 0x98;
static const int REFINE_CHAR_MODEL  = 0x08;    // the record's model id

static const char* RefinePartyName(int charId)
{
    if (charId < 0 || charId > 10) return nullptr;
    int nameId = charId;
    __try {
        const uint8_t modelId = *((uint8_t*)SAVEMAP_BASE + REFINE_CHARS_OFF
                                  + charId * REFINE_CHAR_STRIDE + REFINE_CHAR_MODEL);
        if (modelId >= 8 && modelId <= 10) nameId = modelId;   // Laguna/Kiros/Ward
    } __except(EXCEPTION_EXECUTE_HANDLER) { }
    return REFINE_CHAR_NAMES[nameId];
}

// FF8 magic spell names by spell id (kernel.bin Section 1 order). Maps the refine-
// result magic name (parsed from the preview, e.g. "Waters") back to its spell id
// so the recipient's stock can be located in their savemap magic array. Water=10
// is confirmed against the in-game panel; ids 1-40 follow the documented order
// (elemental, GF-tier, healing, support). A name not in this table is treated as
// unknown -> the stock line is skipped (name only) rather than risk a wrong count.
// Extend with the status-magic ids (41+) once they're confirmed the same way.
// v0.22.0 (#81): **THE TABLE THAT USED TO LIVE HERE WAS WRONG.** It ran
// "Slow, Stop, Float, Drain, Pain" at ids 36-40 and stopped there, with a note
// to extend it "once they're confirmed". 36 and 37 are right; 38, 39 and 40 are
// Blind, Confuse and Sleep. Float is 47, Drain 44, Pain 45. So a refine whose
// yield was Float, Drain or Pain looked up a DIFFERENT spell's stock and spoke
// that number -- and this is the one line in this file where a wrong answer is
// worse than no answer, since its whole job is "you already have N of these".
//
// The canonical 57-entry table now lives in menu_magic_model.inl, in the same id
// space the Magic submenu indexes, cross-checked against the game's own
// mmagic.bin: its field-usable bit lands on exactly Cure, Cura, Curaga, Life,
// Full-Life, Esuna and Dispel, which a table with the wrong ids could not
// produce. tests/menu_sim.cpp pins all three corrected ids.

// Map a (possibly pluralised) refine-result magic name to its spell id, or -1.
static int MagicNameToId(const char* nm)
{
    if (!nm || !nm[0]) return -1;
    const int N = MAGIC_SPELL_NAME_COUNT;
    for (int id = 1; id < N; id++)
        if (MAGIC_SPELL_NAMES[id][0] && strcmp(nm, MAGIC_SPELL_NAMES[id]) == 0) return id;
    // The preview pluralises the yield ("20 Waters"); retry without a trailing 's'.
    size_t L = strlen(nm);
    if (L > 1 && nm[L - 1] == 's') {
        char base[48];
        size_t c = (L - 1 < sizeof(base) - 1) ? L - 1 : sizeof(base) - 1;
        memcpy(base, nm, c); base[c] = '\0';
        for (int id = 1; id < N; id++)
            if (MAGIC_SPELL_NAMES[id][0] && strcmp(base, MAGIC_SPELL_NAMES[id]) == 0) return id;
    }
    // The old table spelled id 25 "Full-life"; the canonical one uses the game's
    // own "Full-Life". Accept the old spelling so a preview quoting it still
    // resolves rather than silently losing the stock line.
    if (strcmp(nm, "Full-life") == 0 || strcmp(nm, "Full-lifes") == 0) return 25;
    return -1;
}

// Read a character's current stock of spellId from the savemap. The 8 character
// structs sit at SAVEMAP_BASE+0x048C, 152 bytes each, indexed by character id;
// Magics[32] (32 x {spell_id, qty}) is at struct+0x10. Returns the qty, 0 if the
// spell isn't in the character's list, or -1 on bad input / read fault. C2712-safe.
static int AbilReadRecipStock(int charId, int spellId)
{
    if (charId < 0 || charId > 7 || spellId <= 0 || spellId > 255) return -1;
    __try {
        uint8_t* mag = (uint8_t*)SAVEMAP_BASE + 0x048C + charId * 152 + 0x10;
        for (int k = 0; k < 32; k++)
            if (mag[k * 2] == (uint8_t)spellId) return (int)mag[k * 2 + 1];
        return 0;
    } __except(EXCEPTION_EXECUTE_HANDLER) { return -1; }
}

// Pull the per-recipe yield out of a clean refine-preview phrase
// ("<in> will refine into <out> <Magic>", e.g. "1 will refine into 20 Waters").
// Used to compute the quantity step's running total. std::string -> no __try.
static void ParseRefineYield(const std::string& phrase, int* inN, int* outN,
                             char* magic, size_t magicSz)
{
    *inN = 1; *outN = 0; if (magic && magicSz) magic[0] = '\0';
    if (phrase.empty()) return;

    size_t i = 0; int n = 0; bool any = false;
    while (i < phrase.size() && phrase[i] >= '0' && phrase[i] <= '9') { n = n*10 + (phrase[i]-'0'); i++; any = true; }
    if (any && n > 0) *inN = n;

    size_t w = phrase.find("will refine into");
    if (w == std::string::npos) return;
    size_t j = w + (sizeof("will refine into") - 1);
    while (j < phrase.size() && phrase[j] == ' ') j++;
    int m = 0; any = false;
    while (j < phrase.size() && phrase[j] >= '0' && phrase[j] <= '9') { m = m*10 + (phrase[j]-'0'); j++; any = true; }
    if (any) *outN = m;
    while (j < phrase.size() && phrase[j] == ' ') j++;
    if (magic && magicSz) {
        size_t k = 0;
        while (j < phrase.size() && k < magicSz - 1) magic[k++] = phrase[j++];
        magic[k] = '\0';
        while (k > 0 && magic[k-1] == ' ') magic[--k] = '\0';
    }
}

// (Build 3 + stock) Character-picker handler. On a +0x2DE (character-id) change,
// announce the recipient name immediately; then, after the same dwell the item
// list uses, announce "has <N> <Magic>" — N = that character's current stock of
// the refine-result magic, read from the savemap. The magic name is the stashed
// yield (s_abilYieldMagic); its spell id is mapped from the name (MagicNameToId).
// If the magic isn't in the name table the stock line is skipped (name only).
// char[]/snprintf/Speak only.
static void PollRefineCharPicker(int charId, int slot, const char* what)
{
    if (charId != s_abilRecipLast) {
        s_abilRecipLast = charId;
        s_abilQtyLast   = -1;              // re-arm the quantity announce
        s_abilRecipSettleAt    = GetTickCount();
        s_abilRecipStockSpoken = false;

        const char* nm = RefinePartyName(charId);
        char out[64];
        if (nm) snprintf(out, sizeof(out), "%s", nm);
        else    snprintf(out, sizeof(out), "Character %d", charId);
        ScreenReader::Speak(out, true);
        Log::Menu("[MenuTTS] Refine %s: id=%d slot=%d \"%s\"", what, charId, slot, out);
    }

    // Stock follow-up after the settle dwell (mirrors the 2b name-then-detail beat).
    if (!s_abilRecipStockSpoken &&
        GetTickCount() - s_abilRecipSettleAt >= ABIL_REFINE_SETTLE_MS) {
        s_abilRecipStockSpoken = true;
        if (s_abilYieldMagic[0]) {
            int sid   = MagicNameToId(s_abilYieldMagic);
            int stock = (sid > 0) ? AbilReadRecipStock(charId, sid) : -1;
            if (stock >= 0) {
                char d[96];
                snprintf(d, sizeof(d), "has %d %s", stock, s_abilYieldMagic);
                ScreenReader::Speak(d, false);   // queue so it follows the name
                Log::Menu("[MenuTTS] Refine recip stock: id=%d magic=\"%s\" sid=%d stock=%d",
                          charId, s_abilYieldMagic, sid, stock);
            } else {
                Log::Menu("[MenuTTS] Refine recip stock: id=%d magic=\"%s\" sid=%d (unmapped, name only)",
                          charId, s_abilYieldMagic, sid);
            }
        }
    }
}

// Name a refine result / source id in the namespace its sub-mode says it is in.
// Sub-modes 0 and 2 grant MAGIC (0x004C2D20); 1, 3 and 4 grant ITEMS
// (0x0047ED00) -- the table at 0x004D8B7C. Naming a magic id out of the item
// table is exactly the Card Mod failure with different numbers.
static const char* RefineThingName(int id, bool magic)
{
    if (magic) {
        if (id > 0 && id < MAGIC_SPELL_NAME_COUNT && MAGIC_SPELL_NAMES[id])
            return MAGIC_SPELL_NAMES[id];
        return nullptr;
    }
    const char* nm = (id > 0 && id <= ABIL_ITEM_ID_MAX) ? GetItemName(id) : nullptr;
    return (nm && nm[0]) ? nm : nullptr;
}

// How many of `id` the current source list holds, found by id rather than by
// row -- by the time the popup is up the cursor may be on the party panel.
static int AbilSourceOwned(const AbilRefineView& v, int id)
{
    const RefineSourceKind k = RefineSourceKindOf(v.submode);
    const int slots = (k == RF_SRC_MAGIC) ? RF_CHAR_MAGIC_SLOTS
                    : (k == RF_SRC_CARDS) ? RF_CARD_SLOTS : 198;
    for (int i = 0; i < slots; i++) {
        int rid = 0, rq = 0;
        if (!AbilReadSourceRow(v, i, &rid, &rq)) break;
        if (rid == id && rq > 0) return rq;
    }
    return 0;
}

// (v0.33.0) The quantity popup, read from the recipe rather than from the text.
//
// This screen said NOTHING through v0.32.1 -- the gate it sat behind was
// sub-mode 0's recipient marker, and the two abilities that reach the popup
// without a recipient never set it. The numbers now come from the same recipe
// entry the engine multiplies at state 0x2A (0x004D7A4C..0x004D7A74), so they
// match the three rows the screen draws: result total, number to refine, and
// what is left of the source.
static void PollRefineQuantity(const AbilRefineView& v, int ownedNow)
{
    const int count = v.qty;
    // State 0x28 is where the engine writes +0x4C and +0x4F. Polling inside it
    // caught the pre-write zeroes once in the v0.33.0 BAT and said "Number to
    // refine 0, makes 0 Death Stone" a beat before the real line.
    if (!RefineQtyReady(count, v.qtyMax)) return;
    if (count == s_abilQtyLast) return;
    const bool firstEntry = (s_abilQtyLast < 0);
    s_abilQtyLast   = count;
    s_abilRecipLast = -1;              // re-arm the picker announce if we back up

    const bool magicOut = RefineResultIsMagic(v.submode);
    const char* resName = v.haveRecipe ? RefineThingName(v.recipe.resultId, magicOut) : nullptr;
    const char* srcName = nullptr;
    if (v.haveRecipe) {
        const RefineSourceKind k = RefineSourceKindOf(v.submode);
        if      (k == RF_SRC_MAGIC) srcName = RefineThingName(v.recipe.sourceId, true);
        else if (k == RF_SRC_CARDS) srcName = (v.recipe.sourceId >= 0 &&
                                               v.recipe.sourceId < CARD_COUNT)
                                              ? CARD_DEFS[v.recipe.sourceId].name : nullptr;
        else                        srcName = RefineThingName(v.recipe.sourceId, false);
    }

    char out[224];
    int n = 0;
    if (firstEntry) n += snprintf(out + n, sizeof(out) - n, "Number to refine ");
    n += snprintf(out + n, sizeof(out) - n, "%d", count);
    if (v.qtyMax > 0) n += snprintf(out + n, sizeof(out) - n, " of %d", v.qtyMax);

    const int stock = AbilResultStock(v);
    RefineQtyLine q = { 0, 0, 0, -1 };
    if (v.haveRecipe) {
        q = RefineQtyMath(v.recipe, count, ownedNow, stock);
        if (resName) n += snprintf(out + n, sizeof(out) - n, ", makes %ld %s", q.produced, resName);
        else         n += snprintf(out + n, sizeof(out) - n, ", makes %ld", q.produced);
        // The screen's third row is the RESULTING STOCK, not the amount made --
        // "Cura 87" while making 16, because Squall already held 71. Saying only
        // the produced count hides the 100 cap, which is where the materials go
        // to waste.
        if (q.total >= 0) n += snprintf(out + n, sizeof(out) - n, " for %ld total", q.total);
        if (ownedNow >= 0) {
            if (srcName) n += snprintf(out + n, sizeof(out) - n, ", %ld %s left", q.remaining, srcName);
            else         n += snprintf(out + n, sizeof(out) - n, ", %ld left", q.remaining);
        }
    }

    // Stage the outcome announce. Everything PollRefineOutcome needs is right
    // here and none of it survives the popup closing.
    if (v.haveRecipe && ownedNow >= 0) {
        s_abilQtyArmed    = true;
        s_abilQtySrcId    = v.recipe.sourceId;
        s_abilQtyOwned    = ownedNow;
        s_abilQtyConsumed = q.consumed;
        s_abilQtyProduced = q.produced;
        s_abilQtyTotal    = q.total;
        snprintf(s_abilQtySrcName, sizeof(s_abilQtySrcName), "%s", srcName ? srcName : "");
        snprintf(s_abilQtyResName, sizeof(s_abilQtyResName), "%s", resName ? resName : "");
    } else {
        s_abilQtyArmed = false;
    }

    ScreenReader::Speak(out, true);
    Log::Menu("[MenuTTS] Refine quantity: count=%d max=%d owned=%d stock=%d total=%ld recipe=%d "
              "(src=%d x%d -> res=%d x%d magicOut=%d) first=%d -> \"%s\"",
              count, v.qtyMax, ownedNow, stock, q.total, v.recipeIdx,
              v.haveRecipe ? v.recipe.sourceId : -1, v.haveRecipe ? v.recipe.sourcePer : -1,
              v.haveRecipe ? v.recipe.resultId : -1, v.haveRecipe ? v.recipe.resultPer : -1,
              magicOut ? 1 : 0, firstEntry ? 1 : 0, out);
}

// (v0.33.2) Say what happened when the quantity popup closes.
//
// The engine performs the refine in state 0x2A and jumps straight out of it
// (0x004D7DEA), so a poller running at 80 ms cannot count on seeing that state
// at all. Cancel and confirm both clear +0x51 and both land the player back on a
// list with the cursor where it was -- so nothing at all was spoken for the one
// action on this screen that changes the save.
//
// The two are told apart by MEASUREMENT, not inference: a confirm consumes
// exactly sourcePer * count of the source, a cancel consumes nothing. Anything
// else says nothing rather than guessing.
static bool PollRefineOutcome(const AbilRefineView& v)
{
    const int ownedAfter = AbilSourceOwned(v, s_abilQtySrcId);
    const RefineOutcome o = RefineOutcomeOf(s_abilQtyOwned, ownedAfter, s_abilQtyConsumed);

    char out[192];
    if (o == RF_OUT_DONE) {
        int n = snprintf(out, sizeof(out), "Refined %ld", s_abilQtyConsumed);
        if (s_abilQtySrcName[0]) n += snprintf(out + n, sizeof(out) - n, " %s", s_abilQtySrcName);
        n += snprintf(out + n, sizeof(out) - n, " into %ld", s_abilQtyProduced);
        if (s_abilQtyResName[0]) n += snprintf(out + n, sizeof(out) - n, " %s", s_abilQtyResName);
        if (s_abilQtyTotal >= 0) n += snprintf(out + n, sizeof(out) - n, ", %ld total", s_abilQtyTotal);
    } else if (o == RF_OUT_CANCELLED) {
        snprintf(out, sizeof(out), "Cancelled");
    } else {
        Log::Menu("[MenuTTS] Refine outcome: unknown (before=%d after=%d consumed=%ld) -- silent",
                  s_abilQtyOwned, ownedAfter, s_abilQtyConsumed);
        return false;
    }

    // v0.33.4: seed the dedupe for whatever screen the engine drops us on, or
    // the outcome gets stepped on ONE POLL LATER instead of in the same one.
    //
    // The v0.33.3 BAT: "Refined 3 Tent into 30 Curaga, 30 total" and, in the
    // same second, "Squall" and "has 30 Curagas". A sub-mode 0 refine lands back
    // on the RECIPIENT PICKER, and the popup had re-armed that announcer on its
    // way past -- so the picker spoke, interrupting, and the player heard the
    // character's name instead of what had just happened to his items. The
    // picker's own information is already inside the outcome line ("30 total").
    s_abilRecipLast        = v.charId;
    s_abilRecipStockSpoken = true;
    s_abilQtyLast          = -1;      // the next popup must announce as a first entry

    ScreenReader::Speak(out, true);
    Log::Menu("[MenuTTS] Refine outcome: %s (src=%d before=%d after=%d consumed=%ld "
              "produced=%ld) -> \"%s\"",
              (o == RF_OUT_DONE) ? "done" : "cancelled",
              s_abilQtySrcId, s_abilQtyOwned, ownedAfter,
              s_abilQtyConsumed, s_abilQtyProduced, out);
    return true;
}

static void PollAbilityItemList()
{
    DWORD now = GetTickCount();
    if (now - s_abilItemPoll < 80) return;
    s_abilItemPoll = now;

    AbilRefineView v;
    if (!AbilReadRefineView(&v)) { s_abilItemSelValid = false; return; }

    const RefinePhase      phase = RefinePhaseOf(v.state, v.submode, v.qtyOpen);
    const RefineSourceKind kind  = RefineSourceKindOf(v.submode);
    const int cur = (RefineCursorOffsetFor(phase) == 0x4A) ? v.pickCur : v.srcCur;

    // ---- the quantity popup -----------------------------------------------
    // Reached from every sub-mode, and until v0.33.0 spoken by none of them
    // except sub-mode 0. The source quantity comes from the row the recipe
    // itself names, not from the cursor -- by the time the popup is up the
    // cursor may be sitting on the party panel.
    if (phase == RF_PHASE_QUANTITY) {
        PollRefineQuantity(v, v.haveRecipe ? AbilSourceOwned(v, v.recipe.sourceId) : -1);
        return;
    }
    // (v0.33.2) The popup has just closed. Confirm and cancel both clear +0x51
    // and both land here with the cursor where it was, so without this the one
    // action that changes the save is the one action that says nothing.
    if (s_abilQtyArmed) {
        s_abilQtyArmed = false;
        // Return when it spoke. Falling through would announce the source row in
        // the SAME poll, and both utterances interrupt -- so the row would cut
        // the outcome off mid-sentence and the player would hear only the row he
        // was already on. (Caught by the driven test, not by the pure one.)
        if (PollRefineOutcome(v)) return;
    }
    s_abilQtyLast = -1;                    // re-arm for the next drill-in

    // ---- a character picker ------------------------------------------------
    // Sub-mode 0 picks WHO RECEIVES the magic (cursor +0x4A, after the item);
    // sub-mode 2 picks WHOSE MAGIC is the source (cursor +0x49, before the
    // list even exists). Same panel, opposite ends of the flow.
    if (phase == RF_PHASE_RECIPIENT || phase == RF_PHASE_CHARSRC) {
        // On sub-mode 2 this picker runs BEFORE anything has been chosen, so
        // there is no yield to report yet -- and a yield left over from the last
        // flow would be reported as this character's stock of it.
        if (phase == RF_PHASE_CHARSRC) s_abilYieldMagic[0] = '\0';
        const int cid = (phase == RF_PHASE_RECIPIENT && v.charId >= 0 && v.charId < 11)
                        ? v.charId
                        : AbilCharAtPickerRow(AbilCharMask(), cur);
        if (cid >= 0)
            PollRefineCharPicker(cid, cur,
                                 (phase == RF_PHASE_CHARSRC) ? "source character" : "recipient");
        return;
    }
    s_abilRecipLast = -1;

    // ---- a source list -----------------------------------------------------
    // Items (sub-modes 0/1/3), one character's 32 magic slots (sub-mode 2's
    // grid), or the card list the creator builds at 0x004D7344 (sub-mode 4).
    int id = 0, qty = 0;
    const bool ok = AbilReadSourceRow(v, cur, &id, &qty);
    if (!ok) { s_abilItemSelValid = false; return; }

    // Re-parse the refine preview each poll so the "/" reader tracks the cursor.
    // Only the item list pages by eleven, so only it can bound the slice that way.
    uint8_t gcw[1024];
    int len = FieldDialog::SnapshotGcwBuffer(gcw, sizeof(gcw));
    std::string dec = (len > 0) ? FF8TextDecode::DecodeMenuText(gcw, len) : std::string();
    std::string preview = dec.empty() ? std::string()
                        : ParseRefinePreview(dec, (kind == RF_SRC_ITEMS) ? cur : -1);
    s_abilItemSelValid = true;
    snprintf(s_abilItemRefine, sizeof(s_abilItemRefine), "%s", preview.c_str());
    if (!preview.empty())
        ParseRefineYield(preview, &s_abilYieldIn, &s_abilYieldOut,
                         s_abilYieldMagic, sizeof(s_abilYieldMagic));

    if (cur != s_abilItemLastCur) {          // announce name + quantity on a move
        s_abilItemLastCur = cur;

        char out[256];
        const char* nm = nullptr;
        if      (kind == RF_SRC_MAGIC) nm = RefineThingName(id, true);
        else if (kind == RF_SRC_CARDS) nm = (id >= 0 && id < CARD_COUNT) ? CARD_DEFS[id].name : nullptr;
        else                           nm = RefineThingName(id, false);

        const bool empty = (id <= 0) || (qty <= 0 && kind != RF_SRC_ITEMS);
        if (empty)       snprintf(out, sizeof(out), "Empty");
        else if (nm)     snprintf(out, sizeof(out), "%s, %d", nm, qty);
        else             snprintf(out, sizeof(out), "%d, %d", id, qty);

        ScreenReader::Speak(out, true);
        Log::Menu("[MenuTTS] Refine source %d: ability=%d submode=%d kind=%d state=0x%02X "
                  "id=%d qty=%d -> \"%s\"",
                  cur, v.abilityId, v.submode, (int)kind, v.state, id, qty, out);

        // Arm the settle-based refinable tag (2b). Empty slots get no status.
        s_abilItemSettleAt     = now;
        s_abilItemStatusSpoken = empty;
    }

    // (2b) Once the cursor has rested on a real row long enough for the engine's
    // refine-result pointer (+0x28) to populate, speak "Refinable" / "Cannot be
    // refined" once, queued after the name.
    if (!s_abilItemStatusSpoken && (now - s_abilItemSettleAt) >= ABIL_REFINE_SETTLE_MS) {
        s_abilItemStatusSpoken = true;
        const bool refinable = (v.resultPtr != 0);
        const char* status = refinable ? "Refinable" : "Cannot be refined";
        ScreenReader::Speak(status, false);   // queued after the name (no interrupt)
        Log::Menu("[MenuTTS] Refine status %d: %s (ptr=0x%08X)",
                  cur, status, (unsigned)v.resultPtr);
    }
}

#if REFINE_FLOW_DIAG
// (diag) Log the refine-flow phase byte (+0x22E) and the candidate recipient /
// quantity cursor bytes whenever any of them changes. A single pass through
// item list -> select item -> character picker (move between chars) -> select
// -> quantity selector (arrow the count) maps every sub-phase's +0x22E value
// and confirms which byte is each live cursor. Raw reads isolated in SEH.
static void RefineFlowDiag()
{
    int v22E=-1,v2DE=-1,v2DF=-1,v2E0=-1,v2E1=-1,v2E3=-1,v2E4=-1,v2E5=-1,v2E7=-1,v2E9=-1;
    bool ok=false;
    __try {
        uint8_t* ms=(uint8_t*)pMenuStateA;
        if (ms) {
            v22E=ms[0x22E]; v2DE=ms[0x2DE]; v2DF=ms[0x2DF]; v2E0=ms[0x2E0];
            v2E1=ms[0x2E1]; v2E3=ms[0x2E3]; v2E4=ms[0x2E4]; v2E5=ms[0x2E5];
            v2E7=ms[0x2E7]; v2E9=ms[0x2E9];
            ok=true;
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) { ok=false; }
    if (!ok) return;

    static int l22E=-2,l2DE=-2,l2DF=-2,l2E0=-2,l2E1=-2,l2E3=-2,l2E4=-2,l2E5=-2,l2E7=-2,l2E9=-2;
    if (v22E==l22E && v2DE==l2DE && v2DF==l2DF && v2E0==l2E0 && v2E1==l2E1 &&
        v2E3==l2E3 && v2E4==l2E4 && v2E5==l2E5 && v2E7==l2E7 && v2E9==l2E9) return;
    l22E=v22E; l2DE=v2DE; l2DF=v2DF; l2E0=v2E0; l2E1=v2E1; l2E3=v2E3;
    l2E4=v2E4; l2E5=v2E5; l2E7=v2E7; l2E9=v2E9;

    Log::Menu("[REFINEDIAG] 22E=%d | 2DE=%d 2E0=%d (recip?) | 2DF=%d (item) | "
              "2E5=%d 2E4=%d 2E7=%d (qty?) | 2E1=%d 2E3=%d 2E9=%d",
              v22E, v2DE, v2E0, v2DF, v2E5, v2E4, v2E7, v2E1, v2E3, v2E9);
}
#endif

// Dispatched from MenuTTS::Update() while mode==6 and top-level cursor == 5.
// Branches on +0x22E: ability-list phase (==3) vs refine item-list phase (>=19).
static void PollAbilitySubmenu()
{
    if (!s_abilActive) {
        s_abilActive = true;
        Log::Menu("[ABILITY] entered Ability screen (top-level cursor 5)");
    }

    int gate = -1, phase = -1, c257 = -1, c258 = -1;
    if (!AbilReadState(&gate, &phase, &c257, &c258)) return;
    if (gate != ABIL_GATE_VALUE) return;          // not actually on the Ability screen

#if REFINE_FLOW_DIAG
    RefineFlowDiag();   // map +0x22E + cursor bytes across all refine sub-phases
#endif

    if (phase != ABIL_PHASE_LIST) {
        if (phase >= ABIL_PHASE_ITEM_MIN) {
            // Drilled into a selected ability's refine source-item list (Build 2).
            // Re-arm the ability list for when we return; "/" now reads the refine
            // preview (set inside PollAbilityItemList), not the ability help.
            s_abilSelValid = false;
            s_abilLastSel  = -1;
            s_abilPrev257  = -1;
            s_abilPrev258  = -1;
            PollAbilityItemList();
            return;
        }
        // Transitioning between phases / leaving — nothing to announce.
        s_abilSelValid     = false;
        s_abilItemSelValid = false;
        s_abilLastSel     = -1;
        s_abilPrev257     = -1;
        s_abilPrev258     = -1;
        s_abilItemLastCur = -1;
        return;
    }

    // On the ability list: the refine item list isn't up, so "/" uses the ability
    // help (not the refine preview).
    s_abilItemSelValid = false;
    s_abilItemLastCur  = -1;

    DWORD now = GetTickCount();
    if (now - s_abilPoll < 80) return;
    s_abilPoll = now;

    uint8_t gcw[1024];
    int len = FieldDialog::SnapshotGcwBuffer(gcw, sizeof(gcw));
    if (len <= 0) return;
    std::string dec = FF8TextDecode::DecodeMenuText(gcw, len);
    if (dec.empty()) return;

    uint8_t ids[64];
    size_t  listStart = std::string::npos;
    int count = ParseAbilityList(dec, ids, 64, &listStart);

    // v0.29.0 (#88): **take the list from the engine, not from the screen.**
    //
    // The parse above reads the GCW, which holds only the ELEVEN ROWS CURRENTLY
    // DRAWN -- the list is paged (`0x004E7915`: cursor / 11 = page, % 11 = row,
    // recombined at 0x004E7982) and the cursor at +0x258 is ABSOLUTE. So with
    // more than eleven menu abilities, page 2 gave a parse of 8 rows against a
    // cursor of 11..18, every row failed `cur < count`, and the mod announced
    // "Empty Ability Slot" for a screen full of perfectly good abilities.
    //
    // The game keeps the flat list where it can simply be read: ids at
    // 0x01D8CB54, count immediately after at 0x01D8CB6C (0x004E770F). Indexing
    // that with the absolute cursor cannot be off by a page, and it also cannot
    // be thrown by a name the decoder renders differently. The GCW parse is kept
    // only for `listStart`, which slices the help text.
    {
        uint8_t engIds[64]; int engCount = 0;
        if (AbilReadEngineList(engIds, 64, engCount) && engCount > 0) {
            count = engCount;
            for (int k = 0; k < engCount; k++) ids[k] = engIds[k];
        }
    }

    // Help description = the text between the menu bar's trailing "Save" and the
    // first ability row (e.g. "Refine Water/Ice Magic from an item").
    std::string desc;
    if (listStart != std::string::npos && listStart > 0) {
        size_t sp = dec.rfind("Save", listStart);
        if (sp != std::string::npos) {
            size_t ds = sp + 4;
            if (listStart > ds) desc = dec.substr(ds, listStart - ds);
        }
    }

    bool ch257 = (c257 != s_abilPrev257);
    bool ch258 = (c258 != s_abilPrev258);

#if ABIL_DIAG
    if (ch257 || ch258 || s_abilLastSel < 0) {
        char lst[256]; int lp = 0;
        for (int i = 0; i < count && lp < 240; i++)
            lp += snprintf(lst + lp, sizeof(lst) - lp, "%u%s",
                           (unsigned)ids[i], (i + 1 < count) ? "," : "");
        Log::Menu("[ABILDIAG] phase=%d 257=%d 258=%d count=%d ids=[%s] help=\"%s\"",
                  phase, c257, c258, count, lst, desc.c_str());
    }
#endif

    s_abilPrev257 = c257;
    s_abilPrev258 = c258;

    if (count <= 0) return;   // parse miss (captured under ABIL_DIAG); nothing to say

    // Resolve the active cursor value: whichever byte just moved (prefer the
    // confirmed +0x258), else the current +0x258 when nothing has been announced
    // yet (fresh entry; +0x258 is true on the first poll because prev == -1).
    int cur = -1;
    if      (ch258) cur = c258;
    else if (ch257) cur = c257;
    else if (s_abilLastSel < 0) cur = c258;
    if (cur < 0) return;

    // Classify the focused row. 0..count-1 = a real ability; count..63 = an empty
    // slot (the list area pads with focusable blank rows below the abilities —
    // confirmed in the BAT reaching index 10 with count=2, help=""). >=64 is noise.
    // Dedupe key: real row -> its index; empty row -> 200 + index (so each empty
    // slot announces once as it's entered, and never collides with a real row).
    int  key;
    bool emptySlot;
    if      (cur < count) { key = cur;       emptySlot = false; }
    else if (cur < 64)    { key = 200 + cur; emptySlot = true;  }
    else return;

    if (key == s_abilLastSel) return;     // announce only on a real change
    s_abilLastSel = key;

    if (!emptySlot) {
        uint8_t id = ids[cur];
        const char* nm = GetAbilityName(id);
        // Stash for the "/" on-demand help re-read (help text; name as fallback).
        s_abilSelValid = true;
        snprintf(s_abilSelName, sizeof(s_abilSelName), "%s", nm ? nm : "Unknown");
        snprintf(s_abilSelDesc, sizeof(s_abilSelDesc), "%s", desc.c_str());
        ScreenReader::Speak(nm ? nm : "Unknown", true);
        Log::Menu("[MenuTTS] Ability row %d/%d: id=%u \"%s\"",
                  cur, count, (unsigned)id, nm ? nm : "Unknown");
    } else {
        // Empty slot: announce it, and stash it so "/" repeats "Empty Ability
        // Slot" (AbilitySpeakSelectedHelp falls back to the name when desc empty).
        s_abilSelValid = true;
        snprintf(s_abilSelName, sizeof(s_abilSelName), "Empty Ability Slot");
        s_abilSelDesc[0] = '\0';
        ScreenReader::Speak("Empty Ability Slot", true);
        Log::Menu("[MenuTTS] Ability row %d/%d: empty slot", cur, count);
    }
}

// (/) On-demand help for the ability under the list cursor. Bound after the GF
// handler in MenuTTS::Update(); returns true when it spoke so every other screen
// falls back to the normal help bar. Speaks the help description only (no name
// repeat — the row announce already gave the name); falls back to the name when
// no description was captured. Char[]/snprintf/Speak only.
static bool AbilitySpeakSelectedHelp()
{
    if (!s_abilActive) return false;
    // Refine item list up: "/" reads the refine preview ("N will refine into M X").
    if (s_abilItemSelValid) {
        char out[256];
        snprintf(out, sizeof(out), "%s",
                 s_abilItemRefine[0] ? s_abilItemRefine : "No refine information");
        ScreenReader::Speak(out, true);
        Log::Menu("[MenuTTS] Refine info (/): \"%s\"", out);
        return true;
    }
    if (!s_abilSelValid) return false;
    char out[256];
    if (s_abilSelDesc[0])
        snprintf(out, sizeof(out), "%s", s_abilSelDesc);
    else
        snprintf(out, sizeof(out), "%s", s_abilSelName);
    ScreenReader::Speak(out, true);
    Log::Menu("[MenuTTS] Ability help (/): \"%s\"", out);
    return true;
}
