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
static const int ABIL_ITEM_CURSOR_OFF = 0x2DF; // refine item-list cursor (0-based)
static const int   ABIL_REFINE_PTR_OFF  = 0x2BE; // engine refine-result pointer (Build 2b)
static const DWORD ABIL_REFINE_SETTLE_MS = 400;  // dwell before the pointer is reliable
// Refine-flow sub-phases (Builds 3/4). +0x22E stays 21 across all three, so the
// sub-phase is read from these markers instead:
static const int ABIL_RECIP_ID_OFF   = 0x2DE; // recipient FF8 character id (Build 3)
static const int ABIL_RECIP_SLOT_OFF = 0x2E0; // recipient party slot 0-3 (Build 3)
static const int ABIL_QTY_OWNED_OFF  = 0x2E4; // owned/max; non-zero only in quantity
static const int ABIL_QTY_COUNT_OFF  = 0x2E5; // number to refine (Build 4)
static const int ABIL_REFINE_SUBPH_OFF = 0x2E9; // 255 = item list, 0 = recipient flow (picker/quantity)
static const int ABIL_QTY_ACTIVE_OFF   = 0x2E7; // within recipient flow: 1 = quantity, 0 = char picker
static const int ABIL_RECIP_STOCK_OFF  = 0x2E6; // (unreliable: transient post-refine amount, not stock)
static const int ABIL_MAGIC_MAX        = 100;   // FF8 per-spell cap

// Menu-usable ("Use GF ability") ability id block: the *-RF refine family (97–113)
// plus Med LV Up (114) and Card Mod (115). Restricting the GCW name match to this
// block avoids colliding with menu-item tokens (GF/Item/Magic/Card = ids 20–25)
// and battle-only command abilities, which matters because these names contain
// internal spaces ("I Mag-RF"). Anything outside this range that ever shows up on
// the screen will surface as an unmatched/short list under ABIL_DIAG.
static const int ABIL_MENU_ID_LO = 97;
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

// SEH read of the refine item-list cursor (+0x2DF). C2712-safe.
static bool AbilReadItemCursor(int* cur)
{
    __try {
        uint8_t* ms = (uint8_t*)pMenuStateA;
        if (ms == nullptr) return false;
        *cur = (int)ms[ABIL_ITEM_CURSOR_OFF];
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}

// SEH read of the engine's refine-result pointer (+0x2BE). It is populated
// (non-zero) a few frames after the cursor lands on a REFINABLE source item,
// reads 0 for items the current ability can't refine, and is cleared when the
// cursor leaves a refinable item — so once the cursor has settled (~400ms) a
// non-zero value reliably means "refinable" with no false positives. Drives the
// Build 2b status tag. C2712-safe.
static bool AbilReadRefinePtr(uint32_t* ptr)
{
    __try {
        uint8_t* ms = (uint8_t*)pMenuStateA;
        if (ms == nullptr) return false;
        *ptr = *(volatile uint32_t*)(ms + ABIL_REFINE_PTR_OFF);
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}

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

// Extract the refine preview ("N will refine into M <Magic>") from the decoded
// GCW for the "/" reader. It sits between the selected ability name and the
// party panel; anchor on "will refine into", walk back over the leading
// "<count> " digits, and stop at the first party-name marker. Empty when the
// highlighted item can't be refined (no preview rendered). std::string -> no __try.
static std::string ParseRefinePreview(const std::string& dec)
{
    size_t w = dec.rfind("will refine into");
    if (w == std::string::npos) return std::string();
    size_t s = w;
    while (s > 0 && dec[s - 1] == ' ') s--;
    while (s > 0 && dec[s - 1] >= '0' && dec[s - 1] <= '9') s--;
    size_t e = dec.size();
    for (const char** m = HELP_END_MARKERS; *m; m++) {
        size_t p = dec.find(*m, w);
        if (p != std::string::npos && p < e) e = p;
    }
    if (e <= s) return std::string();
    std::string out = dec.substr(s, e - s);
    while (!out.empty() && out.back() == ' ') out.pop_back();
    return out;
}

// SEH read of the refine-flow sub-phase markers + recipient/quantity cursors.
// C2712-safe (no C++ objects in __try).
static bool AbilReadRefineSub(int* recipId, int* recipSlot, int* qtyOwned,
                              int* qtyCount, int* subPhase, int* qtyActive)
{
    __try {
        uint8_t* ms = (uint8_t*)pMenuStateA;
        if (ms == nullptr) return false;
        *recipId   = (int)ms[ABIL_RECIP_ID_OFF];
        *recipSlot = (int)ms[ABIL_RECIP_SLOT_OFF];
        *qtyOwned  = (int)ms[ABIL_QTY_OWNED_OFF];
        *qtyCount  = (int)ms[ABIL_QTY_COUNT_OFF];
        *subPhase  = (int)ms[ABIL_REFINE_SUBPH_OFF];
        *qtyActive = (int)ms[ABIL_QTY_ACTIVE_OFF];
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}

// FF8 character ids (the value held in +0x2DE). Squall/Rinoa are renameable; the
// defaults here are used until/unless a savemap name read is added. std::string-free.
static const char* REFINE_CHAR_NAMES[] = {
    "Squall", "Zell", "Irvine", "Quistis", "Rinoa",
    "Selphie", "Seifer", "Edea", "Laguna", "Kiros", "Ward"
};

// FF8 magic spell names by spell id (kernel.bin Section 1 order). Maps the refine-
// result magic name (parsed from the preview, e.g. "Waters") back to its spell id
// so the recipient's stock can be located in their savemap magic array. Water=10
// is confirmed against the in-game panel; ids 1-40 follow the documented order
// (elemental, GF-tier, healing, support). A name not in this table is treated as
// unknown -> the stock line is skipped (name only) rather than risk a wrong count.
// Extend with the status-magic ids (41+) once they're confirmed the same way.
static const char* const MAGIC_NAMES[] = {
    "",                                                    // 0
    "Fire", "Fira", "Firaga",                              // 1-3
    "Blizzard", "Blizzara", "Blizzaga",                    // 4-6
    "Thunder", "Thundara", "Thundaga",                     // 7-9
    "Water",                                               // 10 (confirmed)
    "Aero", "Bio", "Demi", "Holy", "Flare",                // 11-15
    "Meteor", "Quake", "Tornado", "Ultima", "Apocalypse",  // 16-20
    "Cure", "Cura", "Curaga", "Life", "Full-life",         // 21-25
    "Regen", "Esuna", "Dispel", "Protect", "Shell",        // 26-30
    "Reflect", "Aura", "Double", "Triple", "Haste",        // 31-35
    "Slow", "Stop", "Float", "Drain", "Pain"               // 36-40
};

// Map a (possibly pluralised) refine-result magic name to its spell id, or -1.
static int MagicNameToId(const char* nm)
{
    if (!nm || !nm[0]) return -1;
    const int N = (int)(sizeof(MAGIC_NAMES) / sizeof(MAGIC_NAMES[0]));
    for (int id = 1; id < N; id++)
        if (MAGIC_NAMES[id][0] && strcmp(nm, MAGIC_NAMES[id]) == 0) return id;
    // The preview pluralises the yield ("20 Waters"); retry without a trailing 's'.
    size_t L = strlen(nm);
    if (L > 1 && nm[L - 1] == 's') {
        char base[48];
        size_t c = (L - 1 < sizeof(base) - 1) ? L - 1 : sizeof(base) - 1;
        memcpy(base, nm, c); base[c] = '\0';
        for (int id = 1; id < N; id++)
            if (MAGIC_NAMES[id][0] && strcmp(base, MAGIC_NAMES[id]) == 0) return id;
    }
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
static void PollRefineCharPicker(int charId, int slot)
{
    if (charId != s_abilRecipLast) {
        s_abilRecipLast = charId;
        s_abilQtyLast   = -1;              // re-arm the quantity announce
        s_abilRecipSettleAt    = GetTickCount();
        s_abilRecipStockSpoken = false;

        const int nNames = (int)(sizeof(REFINE_CHAR_NAMES) / sizeof(REFINE_CHAR_NAMES[0]));
        char out[64];
        if (charId >= 0 && charId < nNames) snprintf(out, sizeof(out), "%s", REFINE_CHAR_NAMES[charId]);
        else                                snprintf(out, sizeof(out), "Character %d", charId);
        ScreenReader::Speak(out, true);
        Log::Menu("[MenuTTS] Refine recipient: id=%d slot=%d \"%s\"", charId, slot, out);
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

// (Build 4) Quantity-selector handler: announce the number to refine + the running
// spell total on a +0x2E5 change. Total = count * out / in (exact for the common
// in==1 recipes); yield is taken from the clean preview stashed before the popup.
static void PollRefineQuantity(int count, int owned)
{
    if (count == s_abilQtyLast) return;
    s_abilQtyLast   = count;
    s_abilRecipLast = -1;          // re-arm the recipient announce if we back up

    char out[96];
    if (s_abilYieldOut > 0 && s_abilYieldIn > 0) {
        long total = (long)count * s_abilYieldOut / s_abilYieldIn;
        if (s_abilYieldMagic[0]) snprintf(out, sizeof(out), "%d, %ld %s", count, total, s_abilYieldMagic);
        else                     snprintf(out, sizeof(out), "%d, %ld", count, total);
    } else {
        snprintf(out, sizeof(out), "%d", count);
    }
    ScreenReader::Speak(out, true);
    Log::Menu("[MenuTTS] Refine quantity: count=%d owned=%d -> \"%s\"", count, owned, out);
}

// Refine item-list phase handler (Build 2). Announces the highlighted source
// item's name + quantity (from the savemap inventory) on +0x2DF move; stashes
// the refine preview for "/". std::string here -> the raw reads are isolated in
// the SEH helpers above.
static void PollAbilityItemList()
{
    DWORD now = GetTickCount();
    if (now - s_abilItemPoll < 80) return;
    s_abilItemPoll = now;

    int cur = -1;
    if (!AbilReadItemCursor(&cur)) return;
    if (cur < 0 || cur >= 198) { s_abilItemSelValid = false; return; }

    // Re-parse the refine preview each poll so the "/" reader tracks the cursor.
    uint8_t gcw[1024];
    int len = FieldDialog::SnapshotGcwBuffer(gcw, sizeof(gcw));
    std::string dec = (len > 0) ? FF8TextDecode::DecodeMenuText(gcw, len) : std::string();
    std::string preview = dec.empty() ? std::string() : ParseRefinePreview(dec);
    s_abilItemSelValid = true;
    snprintf(s_abilItemRefine, sizeof(s_abilItemRefine), "%s", preview.c_str());

    // Read the refine-flow sub-phase markers once (memory-only, frame-stable):
    //   +0x2E9 (subPh):  255 = item list, 0 = recipient flow (picker/quantity)
    //   +0x2E7 (qActive): within the recipient flow, 1 = quantity, 0 = char picker
    // Using +0x2E7 — not the flickery "Number to refine" GCW text — keeps the phase
    // from flapping between picker and quantity, which was double-speaking names.
    // (+0x2E4/owned can't be used: it lingers non-zero after backing out of quantity.)
    int rId = -1, rSlot = -1, qOwned = -1, qCount = -1, subPh = -1, qActive = -1;
    bool haveSub = AbilReadRefineSub(&rId, &rSlot, &qOwned, &qCount, &subPh, &qActive);
    bool inQuantity = haveSub && subPh == 0 && qActive == 1;

    // Stash the clean per-item yield while it's reliable (item list / character
    // picker), before the quantity popup muddies the preview text, so the quantity
    // step can report the running spell total.
    if (!preview.empty() && !inQuantity)
        ParseRefineYield(preview, &s_abilYieldIn, &s_abilYieldOut,
                         s_abilYieldMagic, sizeof(s_abilYieldMagic));

    // Sub-phase routing. Gated on s_abilItemLastCur >= 0 so it never fires before
    // an item has been browsed (kills the one-frame recipient blip at entry).
    if (s_abilItemLastCur >= 0 && haveSub && subPh == 0) {
        if (qActive == 1) PollRefineQuantity(qCount, qOwned);  // quantity selector
        else              PollRefineCharPicker(rId, rSlot);    // character picker
        return;
    }
    // Item list: re-arm the recipient/quantity announcers for the next drill-in.
    s_abilRecipLast = -1;
    s_abilQtyLast   = -1;

    uint8_t id = 0, qty = 0;
    bool ok = AbilReadInvSlot(cur, &id, &qty);

#if ABIL_DIAG
    if (cur != s_abilItemLastCur) {
        const char* dn = (ok && id) ? GetItemName(id) : "(none)";
        Log::Menu("[ABILDIAG-ITEM] cur=%d invId=%u qty=%u name=\"%s\" preview=\"%s\"",
                  cur, (unsigned)id, (unsigned)qty, dn ? dn : "?", s_abilItemRefine);
        AbilDumpMenuWindow(cur);   // full struct +0x000..+0x3FF — hunt a persisted refinable flag
    }
#endif

    if (cur != s_abilItemLastCur) {          // announce name + quantity on a move
        s_abilItemLastCur = cur;

        char out[256];
        bool empty = (!ok || id == 0);
        if (empty) {
            snprintf(out, sizeof(out), "Empty");
        } else {
            const char* nm = GetItemName(id);
            if (nm) snprintf(out, sizeof(out), "%s, %u", nm, (unsigned)qty);
            else    snprintf(out, sizeof(out), "Item %u, %u", (unsigned)id, (unsigned)qty);
        }
        ScreenReader::Speak(out, true);
        Log::Menu("[MenuTTS] Refine item %d: id=%u qty=%u -> \"%s\"",
                  cur, (unsigned)id, (unsigned)qty, out);

        // Arm the settle-based refinable tag (2b). Empty slots get no status.
        s_abilItemSettleAt     = now;
        s_abilItemStatusSpoken = empty;
    }

    // (2b) Once the cursor has rested on a real item long enough for the engine's
    // refine-result pointer (+0x2BE) to populate, speak "Refinable" / "Cannot be
    // refined" once, queued after the name. Non-zero pointer => the current
    // ability can refine the highlighted item.
    if (!s_abilItemStatusSpoken && (now - s_abilItemSettleAt) >= ABIL_REFINE_SETTLE_MS) {
        s_abilItemStatusSpoken = true;
        uint32_t rp = 0;
        bool refinable = AbilReadRefinePtr(&rp) && rp != 0;
        const char* status = refinable ? "Refinable" : "Cannot be refined";
        ScreenReader::Speak(status, false);   // queued after the name (no interrupt)
        Log::Menu("[MenuTTS] Refine status %d: %s (ptr=0x%08X)",
                  cur, status, (unsigned)rp);
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
