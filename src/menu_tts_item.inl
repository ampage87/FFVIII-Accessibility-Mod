// menu_tts_item.inl — Item submenu TTS (use, rearrange, sort, battle flows)
// Included from menu_tts.cpp. Do not compile independently.
// v0.12.18: Extracted for readability.

// ============================================================================
// v0.08.29: Item submenu TTS — polls phase + cursor, announces actions/items
// ============================================================================
// Called every frame while in menu mode. Detects Item submenu via +0x234==5.
// Phase 0 = action menu (Use/Rearrange/Sort/Battle)
// Phase 1 = item list (reads inventory from savemap)

static void ResetItemSubmenuState()
{
    if (s_itemSubmenuActive) {
        Log::Menu("[MenuTTS] Item submenu exited");
    }
    s_itemSubmenuActive = false;
    s_prevItemCursor = 0xFF;
    s_prevActionCursor = 0xFF;
    s_prevFocusState = 0xFF;
    s_pendingActionCursor = 0xFF;
    s_pendingActionTime = 0;
    // v0.08.62: reset sub-flow state
    s_prevTargetCursor = 0xFF;
    s_prevTargetCharIdx = 0xFF;   // v0.18.2.7 (#10)
    s_prevTargetHP = 0xFFFF;      // v0.18.2.7 (#10)
    s_prevBattleItemCursor = 0xFF;
    s_inUseTargetMode = false;
    s_inRearrangeMode = false;
    s_inBattleMode = false;
    s_inBattleDestMode = false;
    s_battleSwapSrcPos = 0xFF;
    s_rearrangePrevFocus = 0;
    s_rearDestDiagValid = false;
    s_batDestDiagValid = false;
}

// v0.08.64: Live character HP readout for Use target.
// CHAR_DATA_BASE (0x1CFE74C) and CHAR_STRUCT_SIZE (0x98) defined above.
// Runtime character struct: +0x00=current_hp(u16), +0x02=max_hp(u16), +0x04=exp(u32), +0x08=model_id(u8)

// v0.08.63: Get active party member name by cursor position (0-based)
// Reads savemap party indices at +0xAF1 (3 bytes: char index 0-7, or 0xFF=empty)
static const char* GetPartyMemberName(uint8_t cursorPos)
{
    if (cursorPos >= 3) return nullptr;
    uint8_t* party = (uint8_t*)SAVEMAP_BASE + PARTY_INDICES_OFFSET;
    uint8_t charIdx = party[cursorPos];
    Log::Menu("[MenuTTS] GetPartyMemberName: pos=%u partyRaw=[%u,%u,%u] charIdx=%u",
               (unsigned)cursorPos,
               (unsigned)party[0], (unsigned)party[1], (unsigned)party[2],
               (unsigned)charIdx);
    // v0.17.8.17.7: dream-aware name (charIdx is a formation index; stale in dreams)
    return GetCharacterNameByPortrait(ResolveDreamAwareCharId(charIdx));
}

// ============================================================================
// v0.08.86: Status ailment decoding from savemap character status byte (+0x96)
// FF8 persistent status flags (survive outside battle, shown in menu):
//   Bit 0 (0x01): KO/Dead
//   Bit 1 (0x02): Poison
//   Bit 2 (0x04): Petrify
//   Bit 3 (0x08): Darkness/Blind
//   Bit 4 (0x10): Silence
//   Bit 5 (0x20): Berserk
//   Bit 6 (0x40): Zombie
//   Bit 7 (0x80): unknown/unused
// ============================================================================
static const char* STATUS_NAMES[] = {
    "KO",        // bit 0
    "Poison",    // bit 1
    "Petrify",   // bit 2
    "Blind",     // bit 3
    "Silence",   // bit 4
    "Berserk",   // bit 5
    "Zombie",    // bit 6
    nullptr      // bit 7 (unused)
};

// Decode status byte into a comma-separated string. Returns empty string if no ailments.
static int FormatStatusAilments(uint8_t status, char* buf, int bufSize)
{
    int pos = 0;
    for (int bit = 0; bit < 7; bit++) {
        if ((status & (1 << bit)) && STATUS_NAMES[bit]) {
            if (pos > 0 && pos < bufSize - 2)
                pos += sprintf(buf + pos, ", ");
            if (pos < bufSize - 16)
                pos += sprintf(buf + pos, "%s", STATUS_NAMES[bit]);
        }
    }
    if (pos < bufSize) buf[pos] = '\0';
    return pos;
}

// v0.08.86: Runtime computed stats array.
// FFNx: char_comp_stats_1CFF000, span of 3 (one per active party slot).
// struct ff8_char_computed_stats: curr_hp at +0x172, max_hp at +0x174.
// Struct size = 0x1D0 (464 bytes) based on FFNx definition.
static const uint32_t COMP_STATS_BASE = 0x1CFF000;
static const int COMP_STATS_CURHP_OFFSET = 0x172;
static const int COMP_STATS_MAXHP_OFFSET = 0x174;
static const int COMP_STATS_STRUCT_SIZE  = 0x1D0;  // 464 bytes per entry (3 entries for party)

// v0.18.2.12 (#47): the menu also keeps a per-CHARACTER HP display array that
// covers every AVAILABLE character — including benched ones the 3-slot
// computed-stats array misses. pMenuStateA + 0x71E + charIdx*0x20: curHP at +0,
// maxHP at +2. BAT v0.18.2.11: Squall(0)=336/916, Zell(1)=64/585,
// Quistis(3,benched)=861/861, Selphie(5)=385/482 — all matching the battle
// members' computed stats.
static const int MENU_HP_ARRAY_OFFSET = 0x71E;  // pMenuStateA-relative
static const int MENU_HP_ARRAY_STRIDE = 0x20;
static const int MENU_HP_CUR_OFFSET   = 0x00;
static const int MENU_HP_MAX_OFFSET   = 0x02;

// v0.08.86: Get character HP + status.
// Primary source: computed stats at 0x1CFF000 (live, updates on item use).
// Fallback: savemap character section for curHP, header for lead maxHP.
static bool GetCharacterHP(uint8_t charIdx, uint16_t& curHP, uint16_t& maxHP)
{
    if (charIdx > 7) return false;
    __try {
        // Read from savemap as baseline
        uint8_t* smChar = (uint8_t*)SAVEMAP_BASE + CHARS_OFFSET + charIdx * CHAR_STRUCT_SIZE;
        curHP = *(uint16_t*)(smChar + CHR_CURR_HP);
        maxHP = *(uint16_t*)(smChar + CHR_MAX_HP);

        // v0.18.2.12 (#47): prefer the menu's per-character HP display array, which
        // covers ALL available characters — including benched ones. Confirm the entry
        // belongs to this character by requiring its curHP field to match the savemap
        // curHP, then take maxHP from it. (FF8 derives max HP at runtime; the savemap
        // char struct doesn't store it — BAT showed savemap maxHP=0.)
        bool gotMax = false;
        if (pMenuStateA && charIdx <= 10) {
            uint8_t* disp = (uint8_t*)pMenuStateA + MENU_HP_ARRAY_OFFSET + charIdx * MENU_HP_ARRAY_STRIDE;
            uint16_t dispCur = *(uint16_t*)(disp + MENU_HP_CUR_OFFSET);
            uint16_t dispMax = *(uint16_t*)(disp + MENU_HP_MAX_OFFSET);
            if (dispMax > 0 && dispMax < 10000 && dispCur == curHP) {
                maxHP = dispMax;
                gotMax = true;
            }
        }

        // Fallback: computed-stats array for the 3 battle slots (formation-indexed).
        // Keep curHP from the SAVEMAP: it updates live on an in-menu item use, whereas
        // computed curHP is stale until the Item screen is rebuilt (BAT #10: after a
        // Potion, savemap=536, computed=336).
        if (!gotMax) {
            uint8_t* formation = (uint8_t*)SAVEMAP_BASE + 0xAF0;
            int partySlot = -1;
            for (int i = 0; i < 4; i++) {
                if (formation[i] == charIdx) { partySlot = i; break; }
            }
            if (partySlot >= 0 && partySlot < 3) {
                uint8_t* cs = (uint8_t*)COMP_STATS_BASE + partySlot * COMP_STATS_STRUCT_SIZE;
                uint16_t csHP = *(uint16_t*)(cs + COMP_STATS_CURHP_OFFSET);
                uint16_t csMax = *(uint16_t*)(cs + COMP_STATS_MAXHP_OFFSET);
                if (csMax > 0 && csMax < 10000) {
                    maxHP = csMax;
                    if (curHP == 0 && csHP > 0) curHP = csHP;  // safety if savemap unreadable
                }
            } else if (maxHP == 0 && formation[0] == charIdx) {
                maxHP = *(uint16_t*)((uint8_t*)SAVEMAP_BASE + HDR_CHAR1_MAX_HP);
            }
        }

        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

// v0.08.86: Get character status byte from savemap
static uint8_t GetCharacterStatus(uint8_t charIdx)
{
    if (charIdx > 7) return 0;
    __try {
        uint8_t* smChar = (uint8_t*)SAVEMAP_BASE + CHARS_OFFSET + charIdx * CHAR_STRUCT_SIZE;
        return *(smChar + CHR_STATUS);
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        return 0;
    }
}

// v0.08.86: Build full party member announcement string.
// Format: "Name, HP X of Y" or "Name, HP X" (if no maxHP)
// Appends status ailments if present: ", Poison, Blind"
// Appends "KO" for dead characters (curHP=0).
static int FormatPartyMemberAnnouncement(uint8_t charIdx, const char* name,
                                          bool isUseEntry, char* buf, int bufSize)
{
    uint16_t curHP = 0, maxHP = 0;
    bool hasHP = GetCharacterHP(charIdx, curHP, maxHP);
    uint8_t status = GetCharacterStatus(charIdx);
    
    int pos = 0;
    if (isUseEntry)
        pos += sprintf(buf + pos, "Use on ");
    
    pos += sprintf(buf + pos, "%s", name);
    
    if (hasHP) {
        if (maxHP > 0)
            pos += sprintf(buf + pos, ", HP %u of %u", (unsigned)curHP, (unsigned)maxHP);
        else
            pos += sprintf(buf + pos, ", HP %u", (unsigned)curHP);
    }
    
    // Append status ailments
    if (status != 0) {
        char statusBuf[128] = {};
        int slen = FormatStatusAilments(status, statusBuf, sizeof(statusBuf));
        if (slen > 0)
            pos += sprintf(buf + pos, ", %s", statusBuf);
    } else if (hasHP && curHP == 0) {
        // No status flags but HP is 0 — character is KO
        pos += sprintf(buf + pos, ", KO");
    }
    
    return pos;
}

// ===========================================================================
// v0.30.0 (#89): THE USE-TARGET LIST, READ FROM THE ENGINE
// ---------------------------------------------------------------------------
// Aaron: *"The list of characters / party members / GFs doesn't always seem to
// be accurate. Most of the time it is, but sometimes not."*
//
// 0x004F8600..0x004F86BF builds the target list and settles every part of it:
//
//     xor  ebp, ebp
//     test bl, 2            ; the item targets CHARACTERS
//     call 0x004AD030       ;   -> 16-bit character mask
//     mov  bp, ax
//     test bl, 4            ; the item targets GFs
//     call 0x004AD090       ;   -> 16-bit GF mask
//     shl  eax, 0x10        ;   GFs live in the HIGH half
//     or   ebp, eax
//     test ebp, 0xffff0000
//     mov  byte [esi+0x64], 1   ; GF bits present -> two columns
//     ...            [esi+0x64], 0   ; characters   -> one column
//     mov  dword [esi+0x38], ebp    ; THE MASK
//
// and the two mask builders say what the bits mean:
//
//   0x004AD030  bit i = character i, for i in 0..7, set when that character's
//               savemap +0x94 "exists" byte is odd (0x01CFE17C, stride 0x98,
//               eight of them) -- AND, when [0x01CFE97A] & 1, further limited
//               to the three ids in the battle formation at 0x01CFE74C.
//   0x004AD090  bit 16+j = GF j, for j in 0..15, set when its savemap +0x11
//               byte is odd (0x01CFDCB9, stride 0x44, sixteen of them).
//
// **The cursor at +0x58 is the BIT INDEX, not a position in a packed list.**
// The draw code proves it: with characters only it puts row `cur` at
// `y = cur*13 + 0x42` in a single column (0x004F8886), and with GFs at
// `row = cur/2, col = cur&1` (0x004F889C). Position comes from the SLOT. **The
// screen does not compact -- it leaves gaps.**
//
// What the mod did instead: it collected an 0xFF-terminated roster from
// pMenuStateA+0x1DB, SORTED it by character index, and used the cursor as a
// position in that packed list. That agrees with the truth **only when the set
// bits happen to run 0,1,2,... with no gaps** -- which is exactly Aaron's party
// today (Squall 0 through Selphie 5) and exactly why it was right most of the
// time. Recruit Seifer or Edea over a gap, or hit the case where
// [0x01CFE97A] & 1 narrows the mask to a three-member formation like {0,2,5},
// and every name below the gap shifts by one. **And it never handled GFs at
// all: a GF target list was read out as party members.**
// ===========================================================================

static const uintptr_t ITEM_POOL_BASE = 0x01D76BC8;
static const uintptr_t ITEM_POOL_END  = 0x01D77078;   // base + 10 * 0x78
static const uintptr_t ITEM_LIST_HEAD = 0x01D76B48;
static const uint32_t  ITEM_STATE_FN  = 0x004F81F0;   // creator 0x004F8010

static const int ITMO_TARGET_MASK = 0x38;   // u32: low 16 = characters, high 16 = GFs
static const int ITMO_TARGET_CUR  = 0x58;   // i16, the BIT INDEX under the cursor
static const int ITMO_TARGET_KIND = 0x64;   // 0 = characters, 1 = GFs

static const int ITEM_TARGET_MAX_SLOT = 16;

static uint8_t* FindItemModule()
{
    __try {
        uint8_t* m = *(uint8_t* volatile*)ITEM_LIST_HEAD;
        for (int i = 0; i < 12 && m; i++) {
            const uintptr_t a = (uintptr_t)m;
            if (a < ITEM_POOL_BASE || a >= ITEM_POOL_END) break;
            if ((a - ITEM_POOL_BASE) % 0x78 != 0) break;
            if (*(uint32_t*)(m + 0x08) == ITEM_STATE_FN) return m;
            m = *(uint8_t* volatile*)m;
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) { return nullptr; }
    return nullptr;
}

struct ItemTargetSel
{
    bool     isGF;        // which half of the mask this screen is showing
    int      slot;        // the bit index under the cursor == character id / GF id
    bool     available;   // is that slot's bit actually set
    uint32_t mask;
    int      rawCursor;   // for the log, so a bad read is diagnosable
    int      rawKind;
    const char* how;      // which identification answered
};

// v0.30.1 (#89): **the pool walk alone was not enough, and the BAT proved it.**
// v0.30.0 logged "engine read failed" on every entry to the target screen while
// the screenshot showed eleven GFs drawn and the cursor sitting on Alexander.
// So the module was there and the walk did not find it.
//
// The fix is not to trust a slot instead. It is to accept EITHER identification
// and require the same evidence from both: the module's own state word must be
// the state the caller is already reading out of it. `pMenuStateA + 0x21E` is
// pool slot 2, and the Item reader has been reading its state byte from
// +0x22E == slot2 + 0x10 successfully all along -- so checking +0x10 against the
// live state turns that base from an assumption into a positive test, and if the
// two ever disagree the log says which one answered.
// v0.30.2 (#89): **the walk really does fail, and the BAT settled it.** With
// this helper in place the GF target list read perfectly on every row --
// "Use target entered [slot2 (walk found nothing)]: GF slot 0 avail=1
// mask=0x07FF0000" -- so the Item module IS at pMenuStateA+0x21E and
// FindItemModule does not see it. Why is still open; the pool dump below is
// there to answer it next time without costing a test of its own.
//
// The identification accepts either source and demands the same evidence from
// both: the module's own state word must fall in the range the caller is in.
// That is what keeps the alias a TEST rather than the fixed-slot assumption this
// audit exists to remove.
static void ItemDumpPoolOnce()
{
    static bool s_done = false;
    if (s_done) return;
    s_done = true;
    __try {
        uint8_t* head = *(uint8_t* volatile*)ITEM_LIST_HEAD;
        Log::Menu("[MenuTTS] pool dump: head=%p base=%p end=%p",
                  (void*)head, (void*)ITEM_POOL_BASE, (void*)ITEM_POOL_END);
        for (int i = 0; i < 10; i++) {
            uint8_t* m = (uint8_t*)(ITEM_POOL_BASE + i * 0x78);
            Log::Menu("[MenuTTS]   slot %d @%p inUse=%u state=%u upd=0x%08X "
                      "next=%p prev=%p",
                      i, (void*)m, (unsigned)m[0x12],
                      (unsigned)*(uint16_t*)(m + 0x10),
                      (unsigned)*(uint32_t*)(m + 0x08),
                      (void*)*(uint8_t**)m, (void*)*(uint8_t**)(m + 4));
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        Log::Menu("[MenuTTS] pool dump: faulted");
    }
}

static uint8_t* ItemModuleBaseInStates(int lo, int hi, const char** how)
{
    *how = "walk";
    uint8_t* m = FindItemModule();
    if (m) {
        int st = -1;
        __try { st = (int)*(volatile uint16_t*)(m + 0x10); }
        __except(EXCEPTION_EXECUTE_HANDLER) { st = -1; }
        if (st >= lo && st <= hi) return m;
    }
    // The slot-2 alias, VERIFIED rather than assumed: the Item reader has been
    // taking its state byte from +0x22E == slot2 + 0x10 all along, so requiring
    // that word to agree with the live state turns the base into a positive test.
    if (!pMenuStateA) return nullptr;
    uint8_t* alias = (uint8_t*)pMenuStateA + 0x21E;
    int st = -1;
    __try { st = (int)*(volatile uint16_t*)(alias + 0x10); }
    __except(EXCEPTION_EXECUTE_HANDLER) { return nullptr; }
    if (st < lo || st > hi) return nullptr;
    *how = m ? "slot2 (walk found a different state)" : "slot2 (walk found nothing)";
    ItemDumpPoolOnce();
    return alias;
}

static uint8_t* ItemTargetBase(int expectState, const char** how)
{
    return ItemModuleBaseInStates(expectState, expectState, how);
}

static bool ItemReadTargetSel(int expectState, ItemTargetSel& sel)
{
    memset(&sel, 0, sizeof(sel));
    const char* how = "";
    uint8_t* m = ItemTargetBase(expectState, &how);
    if (!m) return false;
    sel.how = how;
    __try {
        const uint32_t mask = *(volatile uint32_t*)(m + ITMO_TARGET_MASK);
        const int      cur  = (int)*(volatile int16_t*)(m + ITMO_TARGET_CUR);
        const int      kind = (int)*(volatile uint8_t*)(m + ITMO_TARGET_KIND);
        sel.rawCursor = cur;
        sel.rawKind   = kind;
        if (cur < 0 || cur >= ITEM_TARGET_MAX_SLOT) return false;
        sel.mask      = mask;
        sel.isGF      = (kind != 0);
        sel.slot      = cur;
        sel.available = (mask & (1u << (cur + (sel.isGF ? 16 : 0)))) != 0;
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}

// The whole line for the row the cursor is on. GFs are named from the savemap,
// so a renamed GF reads as the player named it.
static void ItemFormatTarget(const ItemTargetSel& sel, bool isEntry,
                             char* buf, size_t n)
{
    if (!buf || n == 0) return;
    buf[0] = '\0';
    if (sel.isGF) {
        char gfn[32] = {};
        DecodeGFName(sel.slot, gfn, sizeof(gfn));
        snprintf(buf, n, "%s%s", isEntry ? "Use on " : "",
                 gfn[0] ? gfn : "Guardian Force");
        // An unobtained GF still occupies its slot -- the screen leaves the gap
        // rather than closing it -- so say the slot is not a target instead of
        // reading a name the player cannot pick.
        if (!sel.available) {
            const size_t l = strlen(buf);
            snprintf(buf + l, (l < n) ? n - l : 0, ", not available");
        }
        return;
    }
    const uint8_t charIdx = (uint8_t)sel.slot;
    const char* name = GetCharacterNameByPortrait(ResolveDreamAwareCharId(charIdx));
    if (!name) {
        snprintf(buf, n, "%sslot %d", isEntry ? "Use on " : "", sel.slot + 1);
        return;
    }
    if (!sel.available) {
        snprintf(buf, n, "%s%s, not available", isEntry ? "Use on " : "", name);
        return;
    }
    FormatPartyMemberAnnouncement(charIdx, name, isEntry, buf, (int)n);
}

// v0.30.0 (#89): GetPartyCharAtVisualPos is GONE.
//
// It collected an 0xFF-terminated roster from pMenuStateA+0x1DB, sorted it by
// character index, and treated the target cursor as a position in that packed
// list. The engine's cursor is the BIT INDEX of the slot (see
// ItemReadTargetSel above), and the screen leaves gaps rather than packing, so
// the two agreed only while the available characters happened to run 0,1,2,...
// with nothing missing. It is deleted rather than kept as a fallback: a
// fallback that is wrong in exactly the cases the fix exists for is not a
// safety net, it is a second answer nobody asked for.


// SEH-protected: reads submenu offsets and announces changes.
// v0.08.60: Primary detection via +0x22E focus state (3=action menu, 5=items list).
//   On 5→3: items→action transition, announce current action option.
//   On *→5: action→items transition, announce current item.
//   Debounced +0x27F for left/right action cursor changes.
//   +0x272 item list cursor changes announced immediately when focus==5.

static void AnnounceItemAtCursor(uint8_t cursor)
{
    uint8_t* inv = (uint8_t*)SAVEMAP_BASE + ITEM_INVENTORY_OFFSET;
    uint8_t itemId  = inv[cursor * 2];
    uint8_t itemQty = inv[cursor * 2 + 1];
    char buf[256];

    if (itemId == 0) {
        sprintf(buf, "Empty");
    } else {
        const char* name = GetItemName(itemId);
        if (name)
            sprintf(buf, "%s, %u", name, (unsigned)itemQty);
        else
            sprintf(buf, "Item %u, %u", (unsigned)itemId, (unsigned)itemQty);
    }
    ScreenReader::Speak(buf, true);
    Log::Menu("[MenuTTS] Item list cursor %u: id=%u qty=%u -> \"%s\"",
               (unsigned)cursor, (unsigned)itemId, (unsigned)itemQty, buf);
}

// v0.08.68: Battle item announcement with page/item position.
// Format: "Name, quantity N, page P, item I" or "Empty, page P, item I"
// Battle screen shows 4 items per page.
static const int BATTLE_ITEMS_PER_PAGE = 4;

// v0.08.85: battle_order working buffer code removed (v0.08.68–v0.08.83).
// The battle arrangement screen has its own display struct at 0x1D8DFF4
// containing {item_id, quantity} pairs that reflect the actual screen content,
// including filtering and live swap updates. We read that directly now.

// v0.08.84: Battle arrangement display struct at 0x1D8DFF4.
// Format: savemap_ff8_item pairs {item_id, quantity} at 2 bytes per slot.
// Engine builds this filtered list from battle_order on screen open.
// qty=0 means empty slot. This is what the screen actually shows.
static const uint32_t BATTLE_DISPLAY_STRUCT = 0x1D8DFF4;

// v0.29.0 (#88): the item id sitting at the SOURCE slot when a swap was armed.
// Both arrange flows leave their destination screen to the same state whether
// the player confirmed or cancelled, so the transition proves nothing; the slot
// does. 0xFFFF = nothing armed.
static uint16_t s_swapSrcIdAtArm = ITEM_SWAP_NO_ID;

static uint16_t ItemBattleSlotId(uint8_t pos)
{
    uint16_t v = 0xFFFF;
    __try { v = ((const uint8_t*)BATTLE_DISPLAY_STRUCT)[pos * 2]; }
    __except(EXCEPTION_EXECUTE_HANDLER) { v = 0xFFFF; }
    return v;
}
static uint16_t ItemInventorySlotId(uint8_t pos)
{
    uint16_t v = 0xFFFF;
    __try { v = ((const uint8_t*)(SAVEMAP_BASE + ITEM_INVENTORY_OFFSET))[pos * 2]; }
    __except(EXCEPTION_EXECUTE_HANDLER) { v = 0xFFFF; }
    return v;
}
// True only if the armed source slot now holds something different. A swap of
// two slots holding the SAME id is indistinguishable from a cancel and is also
// a no-op, so reporting it as cancelled costs the player nothing.
static bool ItemSwapHappened(uint16_t nowId)
{
    // The judgement is in menu_item_swap_model.inl so tests/menu_sim.cpp can
    // exercise it; this wrapper only consumes the arm so it cannot fire twice.
    const bool changed = ItemSwapDecide(s_swapSrcIdAtArm, nowId);
    s_swapSrcIdAtArm = ITEM_SWAP_NO_ID;
    return changed;
}

static void AnnounceBattleItemAtCursor(uint8_t cursor)
{
    // v0.08.84: Read from the engine's display struct at 0x1D8DFF4.
    // This is the filtered/ordered list that the battle arrangement screen renders.
    uint8_t itemId = 0, itemQty = 0;
    __try {
        uint8_t* disp = (uint8_t*)BATTLE_DISPLAY_STRUCT;
        itemId  = disp[cursor * 2];
        itemQty = disp[cursor * 2 + 1];
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        Log::Menu("[MenuTTS] Battle display struct read failed at cursor %u", (unsigned)cursor);
    }
    
    int page = (cursor / BATTLE_ITEMS_PER_PAGE) + 1;
    int itemNum = (cursor % BATTLE_ITEMS_PER_PAGE) + 1;
    char buf[256];

    if (itemQty == 0) {
        sprintf(buf, "Empty, page %d, item %d", page, itemNum);
    } else {
        const char* name = GetItemName(itemId);
        if (name)
            sprintf(buf, "%s, quantity %u, page %d, item %d", name, (unsigned)itemQty, page, itemNum);
        else
            sprintf(buf, "Item %u, quantity %u, page %d, item %d", (unsigned)itemId, (unsigned)itemQty, page, itemNum);
    }
    ScreenReader::Speak(buf, true);
    Log::Menu("[MenuTTS] Battle cursor %u: disp id=%u qty=%u page=%d item=%d -> \"%s\"",
               (unsigned)cursor, (unsigned)itemId, (unsigned)itemQty, page, itemNum, buf);
}

static void PollItemSubmenu()
{
    if (!pMenuStateA) return;
    // Only poll when top-level cursor is on Item (index 1)
    if (s_prevCursor != 1) {
        if (s_itemSubmenuActive) ResetItemSubmenuState();
        return;
    }

    __try {
        uint8_t* base = (uint8_t*)pMenuStateA;
        uint8_t focusState = *(base + ITEM_FOCUS_STATE_OFFSET);  // 3=action, 5=items, 14=use target, etc.
        uint8_t actionCur  = *(base + SUBMENU_ACTION_CURSOR_OFFSET);
        uint8_t listCur    = *(base + SUBMENU_LIST_CURSOR_OFFSET);

        // Submenu just became active (top-level cursor landed on Item)
        if (!s_itemSubmenuActive) {
            s_itemSubmenuActive = true;
            s_prevItemCursor = 0xFF;
            s_prevActionCursor = actionCur;
            s_prevFocusState = focusState;
            s_pendingActionCursor = 0xFF;
            s_pendingActionTime = 0;
            s_prevTargetCursor = 0xFF;
            s_prevTargetCharIdx = 0xFF;   // v0.18.2.7 (#10)
            s_prevTargetHP = 0xFFFF;      // v0.18.2.7 (#10)
            s_prevBattleItemCursor = 0xFF;
            s_inUseTargetMode = false;
            s_inRearrangeMode = false;
            s_inBattleMode = false;
            s_inBattleDestMode = false;
            s_battleSwapSrcPos = 0xFF;
            s_rearrangePrevFocus = 0;
            Log::Menu("[MenuTTS] Item submenu entered (focus=%u actionCur=%u listCur=%u)",
                       (unsigned)focusState, (unsigned)actionCur, (unsigned)listCur);
        }

        // === FOCUS STATE TRANSITIONS (+0x22E) ===
        // Core transitions: 5↔3 via intermediates (5>2>3, 3>4>5).
        // Sub-flow focus values: 14=Use target, ~97=Rearrange, ~30=Battle, 79=Sort flash.
        if (focusState != s_prevFocusState) {
            Log::Menu("[MenuTTS] Item focus: %u -> %u (actionCur=%u listCur=%u)",
                       (unsigned)s_prevFocusState, (unsigned)focusState,
                       (unsigned)actionCur, (unsigned)listCur);

            // --- Arriving at Action menu (focus==3) ---
            if (focusState == 3 && s_prevFocusState != 3 && s_prevFocusState != 0xFF) {
                // v0.08.63: Check if returning from Sort (focus 79->3, actionCur resets to 0)
                bool fromSort = (s_prevFocusState == 79);
                if (fromSort) {
                    ScreenReader::Speak("Items sorted", true);
                    Log::Menu("[MenuTTS] Sort executed (focus 79->3)");
                }
                // Exiting sub-flow modes
                if (s_inUseTargetMode) {
                    s_inUseTargetMode = false;
                    Log::Menu("[MenuTTS] Use target mode exited");
                }
                if (s_inRearrangeMode) {
                    s_inRearrangeMode = false;
                    Log::Menu("[MenuTTS] Rearrange mode exited");
                }
                if (s_inBattleMode) {
                    s_inBattleMode = false;
                    Log::Menu("[MenuTTS] Battle mode exited");
                }
                // Announce current action option
                // v0.08.64: After Sort, queue the action name with interrupt=false
                // so user hears "Items sorted" then "Use" in sequence.
                if (actionCur < ITEM_ACTION_COUNT) {
                    ScreenReader::Speak(ITEM_ACTION_NAMES[actionCur], !fromSort);
                    Log::Menu("[MenuTTS] Item action (focus->3) cursor %u: %s%s",
                               (unsigned)actionCur, ITEM_ACTION_NAMES[actionCur],
                               fromSort ? " (queued after sort)" : "");
                }
                s_prevActionCursor = actionCur;
                s_pendingActionTime = 0;
            }
            // --- Arriving at Items list (focus==5) ---
            else if (focusState == 5 && s_prevFocusState != 5 && s_prevFocusState != 0xFF) {
                // Exiting sub-flow modes (cancel from Use target returns via 14->4->5)
                if (s_inUseTargetMode) {
                    s_inUseTargetMode = false;
                    Log::Menu("[MenuTTS] Use target mode exited (back to items)");
                }
                AnnounceItemAtCursor(listCur);
                s_prevItemCursor = listCur;
            }
            // --- v0.08.86: Arriving at Use target selection (focus==14) ---
            else if (focusState == 14 && !s_inUseTargetMode) {
                s_inUseTargetMode = true;
                // v0.30.0 (#89): the slot, the kind and the availability all come
                // from the module now -- see ItemReadTargetSel's header.
                ItemTargetSel sel;
                if (ItemReadTargetSel((int)focusState, sel)) {
                    s_prevTargetCursor = (uint8_t)sel.slot;
                    s_prevTargetIsGF   = sel.isGF;
                    char buf[256];
                    ItemFormatTarget(sel, true, buf, sizeof(buf));
                    ScreenReader::Speak(buf, true);
                    Log::Menu("[MenuTTS] Use target entered [%s]: %s slot %d avail=%d "
                              "mask=0x%08X -> \"%s\"",
                              sel.how, sel.isGF ? "GF" : "char", sel.slot,
                              (int)sel.available, (unsigned)sel.mask, buf);
                    uint16_t initCur = 0, initMax = 0;
                    const uint8_t charIdx = sel.isGF ? 0xFF : (uint8_t)sel.slot;
                    if (!sel.isGF) GetCharacterHP(charIdx, initCur, initMax);
                    s_prevTargetCharIdx = charIdx;
                    s_prevTargetHP = sel.isGF ? 0xFFFF : initCur;
                } else {
                    // Refuse rather than guess -- and say so, so a BAT can see it.
                    // Say WHY, not just that. v0.30.0 logged one sentence that
                    // covered three different causes and told us none of them.
                    ItemTargetSel d; const char* how = "";
                    uint8_t* base = ItemTargetBase((int)focusState, &how);
                    Log::Menu("[MenuTTS] Use target entered: read failed -- base=%s "
                              "(%s) walk=%p slot2=%p state=%u",
                              base ? "found" : "NONE", how,
                              (void*)FindItemModule(),
                              (void*)(pMenuStateA ? (uint8_t*)pMenuStateA + 0x21E : nullptr),
                              (unsigned)focusState);
                    if (base && ItemReadTargetSel((int)focusState, d))
                        Log::Menu("[MenuTTS]   (retry succeeded: cursor=%d kind=%d)",
                                  d.rawCursor, d.rawKind);
                }
            }
            // --- v0.08.64: Rearrange mode detection (focus stabilizes ~97) ---
            else if (focusState >= 94 && focusState <= 100 && !s_inRearrangeMode) {
                s_inRearrangeMode = true;
                s_rearrangePrevFocus = focusState;
                s_prevItemCursor = listCur;
                s_prevTargetCursor = 0xFF;  // reset dest cursor for when focus hits 99
                AnnounceItemAtCursor(listCur);
                Log::Menu("[MenuTTS] Rearrange mode entered (focus=%u listCur=%u)",
                           (unsigned)focusState, (unsigned)listCur);
            }
            // --- v0.08.70: Battle mode detection (focus stabilizes ~30) ---
            else if (focusState >= 26 && focusState <= 35 && !s_inBattleMode) {
                s_inBattleMode = true;
                s_inBattleDestMode = false;
                uint8_t battleCur = *(base + BATTLE_ITEM_CURSOR_OFFSET);
                s_prevBattleItemCursor = battleCur;
                AnnounceBattleItemAtCursor(battleCur);
                Log::Menu("[MenuTTS] Battle mode entered (focus=%u battleCur=%u)",
                           (unsigned)focusState, (unsigned)battleCur);

            }
            // --- v0.08.77: Battle destination entered (focus==36) ---
            else if (focusState == 36 && s_inBattleMode && s_prevFocusState != 36) {
                s_inBattleDestMode = true;
                s_battleSwapSrcPos = s_prevBattleItemCursor;  // remember source for swap
                s_swapSrcIdAtArm = ItemBattleSlotId(s_battleSwapSrcPos);
                uint8_t batDestCur = *(base + 0x286);
                s_prevBattleItemCursor = batDestCur;
                AnnounceBattleItemAtCursor(batDestCur);
                Log::Menu("[MenuTTS] Battle dest entered (focus=36 srcPos=%u destCur=%u)",
                           (unsigned)s_battleSwapSrcPos, (unsigned)batDestCur);
            }

            // --- v0.08.79: Battle swap detection (returning from dest to source) ---
            //
            // v0.29.0 (#88): **"returning to the source" is NOT "a swap happened".**
            // State 36 leaves to state 30 on BOTH paths -- Cancel at 0x004F9D9D
            // (`test bl,0x10` -> sound 3 -> `mov word[esi+0x10], 0x1E`) does no
            // swap at all, while Confirm at 0x004F9DB2 goes the long way round
            // through state 41 and arrives at the same 30. The mod saw both as
            // 36 -> 30 and said "Swapped" either way, so backing out of a battle
            // arrange told the player their inventory had changed when it had
            // not. The two live slot bytes settle it: compare what is actually
            // in the two positions against what was there before.
            if (s_inBattleMode && s_inBattleDestMode && focusState >= 26 && focusState <= 35) {
                // Was in dest mode (focus==36), now back to source (~30)
                uint8_t destPos = s_prevBattleItemCursor;  // last dest cursor position
                s_inBattleDestMode = false;
                const bool swapped = ItemSwapHappened(ItemBattleSlotId(s_battleSwapSrcPos));
                ScreenReader::Speak(swapped ? "Swapped" : "Cancelled", true);
                Log::Menu("[MenuTTS] Battle swap pos %u <-> %u -> \"%s\"",
                           (unsigned)s_battleSwapSrcPos, (unsigned)destPos,
                           swapped ? "Swapped" : "Cancelled");
                s_battleSwapSrcPos = 0xFF;
                uint8_t battleCur = *(base + BATTLE_ITEM_CURSOR_OFFSET);
                s_prevBattleItemCursor = battleCur;
                AnnounceBattleItemAtCursor(battleCur);
            }

            // --- v0.08.64: Rearrange swap detection (focus 99->97) ---
            //
            // v0.29.0 (#88): same defect as the battle path above. Cancel in
            // state 99 (0x004FB999: `test al,0x10` -> sound 3 -> `mov
            // word[esi+0x10], 0x61`) lands on 97 exactly as a completed swap
            // does, and state 100 -- which is where the real swap happens -- is
            // entered by a same-frame chain the poll can never observe. It also
            // refuses outright when both slots hold the SAME item id
            // (0x004FB9EB) and stays in 99. Compare the slots instead of
            // trusting the transition.
            // v0.29.1 (#88): ARM HERE, not in the destination-cursor block below.
            // That block runs later in the same poll, AFTER
            // `s_rearrangePrevFocus = focusState` a few lines down has already
            // set it to 99 -- so its "am I arriving?" test was false on every
            // frame including the first, the arm never fired, and v0.29.0 said
            // "Cancelled" for every rearrange including real swaps. That is the
            // v0.29.0 defect inverted: same lie, other direction.
            if (s_inRearrangeMode && s_rearrangePrevFocus != 99 && focusState == 99) {
                s_swapSrcIdAtArm = ItemInventorySlotId((uint8_t)s_prevItemCursor);
                Log::Menu("[MenuTTS] Rearrange armed: source slot %u holds id=%u",
                          (unsigned)s_prevItemCursor, (unsigned)s_swapSrcIdAtArm);
            }

            if (s_inRearrangeMode && s_rearrangePrevFocus == 99 && focusState == 97) {
                const bool swapped = ItemSwapHappened(ItemInventorySlotId((uint8_t)s_prevItemCursor));
                ScreenReader::Speak(swapped ? "Swapped" : "Cancelled", true);
                Log::Menu("[MenuTTS] Rearrange 99->97: source slot %u now id=%u -> \"%s\"",
                          (unsigned)s_prevItemCursor,
                          (unsigned)ItemInventorySlotId((uint8_t)s_prevItemCursor),
                          swapped ? "Swapped" : "Cancelled");
                // Re-announce item at cursor after swap
                AnnounceItemAtCursor(listCur);
                s_prevItemCursor = listCur;
                s_prevTargetCursor = 0xFF;  // reset dest cursor for next source→dest cycle
            }
            if (s_inRearrangeMode) {
                s_rearrangePrevFocus = focusState;
            }

            s_prevFocusState = focusState;
        }

        // === ACTION CURSOR: debounced left/right (only when at action menu focus==3) ===
        if (focusState == 3) {
            if (actionCur != s_prevActionCursor) {
                s_pendingActionCursor = actionCur;
                s_pendingActionTime = GetTickCount();
                s_prevActionCursor = actionCur;
            }
            if (s_pendingActionTime != 0) {
                DWORD now = GetTickCount();
                if (actionCur == s_pendingActionCursor &&
                    (now - s_pendingActionTime) >= 200) {
                    if (s_pendingActionCursor < ITEM_ACTION_COUNT) {
                        ScreenReader::Speak(ITEM_ACTION_NAMES[s_pendingActionCursor], true);
                        Log::Menu("[MenuTTS] Item action (debounced) cursor %u: %s",
                                   (unsigned)s_pendingActionCursor,
                                   ITEM_ACTION_NAMES[s_pendingActionCursor]);
                    }
                    s_pendingActionTime = 0;
                } else if (actionCur != s_pendingActionCursor) {
                    s_pendingActionCursor = actionCur;
                    s_pendingActionTime = now;
                }
            }
        } else {
            s_pendingActionTime = 0;
            s_prevActionCursor = actionCur;
        }

        // === ITEM LIST CURSOR: immediate announce when items list has focus ===
        if (focusState == 5) {
            if (listCur != s_prevItemCursor) {
                AnnounceItemAtCursor(listCur);
                s_prevItemCursor = listCur;
            }
        }

        // === v0.08.86: USE TARGET CURSOR (+0x276 party member selection) ===
        // v0.18.2.7 (#10): also re-announce when the selected target's HP changes
        // from using an item. The cursor stays on the same character through a use,
        // so the cursor-move check alone never re-reads the (now updated) HP.
        if (s_inUseTargetMode) {
            ItemTargetSel sel;
            if (ItemReadTargetSel((int)focusState, sel)) {
                const uint8_t charIdx = sel.isGF ? 0xFF : (uint8_t)sel.slot;
                uint16_t curHP = 0, maxHP = 0;
                if (!sel.isGF) GetCharacterHP(charIdx, curHP, maxHP);
                const bool moved = (sel.slot != (int)s_prevTargetCursor) ||
                                   (sel.isGF != s_prevTargetIsGF);
                // v0.18.2.7 (#10): the cursor stays put through a use, so the
                // move check alone never re-reads the healed HP.
                const bool hpChanged = (!moved && !sel.isGF &&
                                        charIdx == s_prevTargetCharIdx &&
                                        s_prevTargetHP != 0xFFFF && curHP != s_prevTargetHP);
                if (moved || hpChanged) {
                    char buf[256];
                    ItemFormatTarget(sel, false, buf, sizeof(buf));
                    ScreenReader::Speak(buf, true);
                    Log::Menu("[MenuTTS] Use target [%s]: %s slot %d avail=%d hp=%u/%u "
                              "mask=0x%08X -> \"%s\"%s",
                              sel.how, sel.isGF ? "GF" : "char", sel.slot, (int)sel.available,
                              (unsigned)curHP, (unsigned)maxHP, (unsigned)sel.mask, buf,
                              hpChanged ? " (HP changed)" : "");
                    s_prevTargetCursor  = (uint8_t)sel.slot;
                    s_prevTargetIsGF    = sel.isGF;
                    s_prevTargetCharIdx = charIdx;
                    s_prevTargetHP      = sel.isGF ? 0xFFFF : curHP;
                }
            }
        }

        // === v0.08.63: REARRANGE ITEM CURSOR (reuse +0x272 during rearrange source, focus ~97) ===
        if (s_inRearrangeMode && focusState == 97) {
            if (listCur != s_prevItemCursor) {
                AnnounceItemAtCursor(listCur);
                s_prevItemCursor = listCur;
            }
        }

        // === v0.08.64: REARRANGE DESTINATION CURSOR (+0x276 during focus==99) ===
        if (s_inRearrangeMode && focusState == 99) {
            uint8_t destCur = *(base + ITEM_TARGET_CURSOR_OFFSET);  // +0x276 reused for destination
            if (destCur != s_prevTargetCursor) {
                AnnounceItemAtCursor(destCur);
                Log::Menu("[MenuTTS] Rearrange dest cursor %u", (unsigned)destCur);
                s_prevTargetCursor = destCur;
            }
        }

        // === v0.08.68: BATTLE ITEM CURSOR (+0x285 for source browsing) ===
        if (s_inBattleMode && focusState >= 26 && focusState <= 35) {
            uint8_t battleCur = *(base + BATTLE_ITEM_CURSOR_OFFSET);
            if (battleCur != s_prevBattleItemCursor) {
                AnnounceBattleItemAtCursor(battleCur);
                s_prevBattleItemCursor = battleCur;
            }
        }

        // === v0.08.68: BATTLE DESTINATION CURSOR (+0x286 during focus==36) ===
        static const int BATTLE_DEST_CURSOR_OFFSET = 0x286;
        if (s_inBattleMode && focusState == 36) {
            uint8_t batDestCur = *(base + BATTLE_DEST_CURSOR_OFFSET);
            if (batDestCur != s_prevBattleItemCursor) {
                AnnounceBattleItemAtCursor(batDestCur);
                s_prevBattleItemCursor = batDestCur;
            }
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        Log::Menu("[MenuTTS] SEH exception in PollItemSubmenu");
    }
}

// ============================================================================
// v0.09.41: Help bar text extraction from GCW buffer
// ============================================================================
// The GCW buffer captures all text rendered each frame. One render cycle:
//   [menu items][help text][character name(s)][location]
// The static menu items prefix is constant: "JunctionItemMagic...Save".
// After "Save", the help text runs until the first party member name.

// Build the static prefix from MENU_ITEMS[] (computed once)
static const char* GetMenuItemsPrefix()
{
    static char s_prefix[128] = {};
    static bool s_built = false;
    if (!s_built) {
        int pos = 0;
        for (int i = 0; i < MENU_ITEMS_COUNT && pos < 120; i++)
            pos += sprintf(s_prefix + pos, "%s", MENU_ITEMS[i]);
        s_built = true;
    }
    return s_prefix;
}

// All possible character names to search for as help text end markers
static const char* HELP_END_MARKERS[] = {
    "Squall", "Zell", "Irvine", "Quistis", "Rinoa", "Selphie",
    "Seifer", "Edea", "Laguna", "Kiros", "Ward",
    nullptr
};

