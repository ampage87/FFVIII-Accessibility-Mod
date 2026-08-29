// battle_tts_menu_lists.inl — Sub-menu list builders.
// Included from battle_tts_menu.inl after battle_tts_menu_state.inl.
// Reads state declared there; called from EnterSubmenu (battle_tts_menu_helpers.inl),
// PollTurnAndCommands (battle_tts_menu_poll.inl), and DialogInject for Draw
// validation. Do not compile independently.
//
// v0.16.5: Extracted from the v0.16.4 monolithic battle_tts_menu.inl as
// part of the mechanical .inl split. No behavior change.


// Build the filtered magic list for the active character.
// Reads savemap char struct +0x10 (32 slots × 2 bytes: magic_id, qty).
// Only includes slots with qty > 0, preserving savemap order (ascending magic_id).
static void BuildMagicList(uint8_t partySlot)
{
    s_turnMagicCount = 0;
    s_magicListBuilt = false;
    if (partySlot >= 3) return;
    
    uint8_t kind = 0xFF; bool over = false;
    const uint8_t charIdxOuter = BattleSlotCharIdx(partySlot, &kind, &over);
    if (over) Log::Battle("BattleTTS: [MAGIC-LIST] dream slot %d: formation was stale, "
                          "actor kind %d -> char %d", (int)partySlot, (int)kind, (int)charIdxOuter);
    __try {
        uint8_t charIdx = charIdxOuter;
        if (charIdx >= 8) return;
        
        uint8_t* charBase = (uint8_t*)(SAVEMAP_CHAR_DATA_BASE + charIdx * SAVEMAP_CHAR_STRIDE);
        uint8_t* magicBase = charBase + 0x10;  // 32 slots × 2 bytes
        
        for (int i = 0; i < 32 && s_turnMagicCount < 32; i++) {
            uint8_t magicId = magicBase[i * 2];
            uint8_t qty = magicBase[i * 2 + 1];
            if (magicId == 0 || qty == 0) continue;
            s_turnMagicList[s_turnMagicCount].id = magicId;
            s_turnMagicList[s_turnMagicCount].qty = qty;
            s_turnMagicCount++;
        }
        
        s_magicListBuilt = true;
        Log::Battle("BattleTTS: [MAGIC-LIST] charIdx=%d has %d spells:", (int)charIdx, s_turnMagicCount);
        for (int i = 0; i < s_turnMagicCount; i++) {
            Log::Battle("BattleTTS: [MAGIC-LIST]   [%d] id=0x%02X (%s) x%d",
                       i, s_turnMagicList[i].id,
                       GetMagicName(s_turnMagicList[i].id),
                       (int)s_turnMagicList[i].qty);
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        Log::Battle("BattleTTS: [MAGIC-LIST] EXCEPTION reading magic for partySlot=%d", (int)partySlot);
    }
}

// Build the filtered GF list for the active character.
// Reads savemap char struct +0x58 (uint16 bitmask of junctioned GFs).
// Only includes GFs that are junctioned. Order follows bit index (0-15).
static void BuildGFList(uint8_t partySlot)
{
    s_turnGFCount = 0;
    s_gfListBuilt = false;
    if (partySlot >= 3) return;
    
    __try {
        uint8_t charIdx = BattleSlotCharIdx(partySlot, 0, 0);
        if (charIdx >= 8) return;
        
        uint8_t* charBase = (uint8_t*)(SAVEMAP_CHAR_DATA_BASE + charIdx * SAVEMAP_CHAR_STRIDE);
        uint16_t gfMask = *(uint16_t*)(charBase + 0x58);
        
        for (int gfIdx = 0; gfIdx < 16 && s_turnGFCount < 16; gfIdx++) {
            if (!(gfMask & (1 << gfIdx))) continue;
            
            uint8_t* gfBase = (uint8_t*)(SAVEMAP_GF_BASE + gfIdx * SAVEMAP_GF_STRIDE);
            s_turnGFList[s_turnGFCount].gfIdx = (uint8_t)gfIdx;
            DecodeFF8String(gfBase, s_turnGFList[s_turnGFCount].name, sizeof(s_turnGFList[s_turnGFCount].name));
            s_turnGFCount++;
        }
        
        s_gfListBuilt = true;
        Log::Battle("BattleTTS: [GF-LIST] charIdx=%d has %d junctioned GFs:", (int)charIdx, s_turnGFCount);
        for (int i = 0; i < s_turnGFCount; i++) {
            Log::Battle("BattleTTS: [GF-LIST]   [%d] gfIdx=%d name='%s'",
                       i, (int)s_turnGFList[i].gfIdx, s_turnGFList[i].name);
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        Log::Battle("BattleTTS: [GF-LIST] EXCEPTION reading GFs for partySlot=%d", (int)partySlot);
    }
}

// Read one entry from the battle items buffer. Returns false on access error.
static bool ReadBattleItemEntry(int cursor, uint8_t* outId, uint8_t* outQty)
{
    if (cursor < 0 || cursor >= BATTLE_ITEM_MAX) return false;
    __try {
        const uint8_t* p = (const uint8_t*)(BATTLE_ITEM_BUFFER_ADDR + cursor * BATTLE_ITEM_BUFFER_STRIDE);
        *outId  = p[0];
        *outQty = p[1];
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

// Compute visual (page, slot) for a cursor position. With v0.14.42 the
// cursor IS the visual position — no boIdx indirection needed.
static void GetItemVisualPos(int cursor, int* outPage, int* outItem)
{
    if (cursor < 0) cursor = 0;
    *outPage = (cursor / 4) + 1;
    *outItem = (cursor % 4) + 1;
}

static void BuildItemList()
{
    s_turnItemCount = 0;
    s_itemListBuilt = false;
    __try {
        // Snapshot the battle items buffer to s_turnItemList for diagnostic
        // logging only. The announce path reads from the engine buffer live
        // (so qty decrements during battle are picked up immediately).
        for (int i = 0; i < BATTLE_ITEM_MAX; i++) {
            uint8_t id = 0, qty = 0;
            if (!ReadBattleItemEntry(i, &id, &qty)) break;
            s_turnItemList[i].id  = id;
            s_turnItemList[i].qty = qty;
        }
        s_turnItemCount = BATTLE_ITEM_MAX;
        s_itemListBuilt = true;

        int populated = 0;
        for (int i = 0; i < BATTLE_ITEM_MAX; i++) {
            if (s_turnItemList[i].id >= 1 && s_turnItemList[i].id < 33 && s_turnItemList[i].qty > 0)
                populated++;
        }
        Log::Battle("BattleTTS: [ITEM-LIST] battle_buffer @ 0x%08X: %d populated of %d positions",
                   (unsigned)BATTLE_ITEM_BUFFER_ADDR, populated, BATTLE_ITEM_MAX);
        for (int i = 0; i < BATTLE_ITEM_MAX; i++) {
            uint8_t id = s_turnItemList[i].id;
            uint8_t qty = s_turnItemList[i].qty;
            if (id >= 1 && id < 33 && qty > 0) {
                Log::Battle("BattleTTS: [ITEM-LIST]   [%d] id=%u qty=%u -> %s",
                           i, (unsigned)id, (unsigned)qty, GetBattleItemName(id));
            }
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        Log::Battle("BattleTTS: [ITEM-LIST] EXCEPTION reading battle items buffer");
    }
}

static void SnapshotAllMagicInventories()
{
    s_magicSnapshotValid = false;
    __try {
        for (int slot = 0; slot < 3; slot++) {
            uint8_t charIdx = BattleSlotCharIdx((uint8_t)slot, 0, 0);
            if (charIdx >= 8) {
                memset(s_magicSnapshotBefore[slot], 0, sizeof(s_magicSnapshotBefore[slot]));
                continue;
            }
            uint8_t* magicBase = (uint8_t*)(SAVEMAP_CHAR_DATA_BASE + charIdx * SAVEMAP_CHAR_STRIDE + 0x10);
            for (int i = 0; i < 32; i++) {
                s_magicSnapshotBefore[slot][i].id  = magicBase[i * 2];
                s_magicSnapshotBefore[slot][i].qty = magicBase[i * 2 + 1];
            }
        }
        s_magicSnapshotValid = true;
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        Log::Battle("BattleTTS: [DRAW-VALID] EXCEPTION snapshotting magic inventories");
    }
}

// Called from field_dialog.cpp when "Received" fires.
// Diffs current magic inventories against the snapshot, logs changes per character.
// Returns the party slot (0-2) that actually gained spells, or 0xFF if none/error.
static uint8_t DiffMagicInventories(uint8_t claimedSlot)
{
    if (!s_magicSnapshotValid) {
        return 0xFF;
    }
    uint8_t gainedSlot = 0xFF;
    __try {
        for (int slot = 0; slot < 3; slot++) {
            uint8_t charIdx = BattleSlotCharIdx((uint8_t)slot, 0, 0);
            if (charIdx >= 8) continue;
            uint8_t* magicBase = (uint8_t*)(SAVEMAP_CHAR_DATA_BASE + charIdx * SAVEMAP_CHAR_STRIDE + 0x10);
            for (int i = 0; i < 32; i++) {
                if (magicBase[i * 2] != s_magicSnapshotBefore[slot][i].id ||
                    magicBase[i * 2 + 1] != s_magicSnapshotBefore[slot][i].qty) {
                    gainedSlot = (uint8_t)slot;
                    break;  // found the recipient, no need to enumerate every change
                }
            }
            if (gainedSlot != 0xFF) break;  // only one character gains per draw
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
    // Re-snapshot so next "Received" diffs against fresh state
    SnapshotAllMagicInventories();
    return gainedSlot;
}

static void BuildDrawList()
{
    s_turnDrawCount = 0;
    s_drawListBuilt = false;
    s_drawTargetSlot = -1;
    memset(s_turnDrawList, 0, sizeof(s_turnDrawList));
    
    // Determine which enemy from target bitmask
    uint8_t tgtMask = 0;
    __try { tgtMask = *(uint8_t*)0x01D76884; } __except(EXCEPTION_EXECUTE_HANDLER) {}
    if (tgtMask == 0) return;
    
    int slot = BitmaskToSlot(tgtMask);
    if (slot < BATTLE_ALLY_SLOTS || slot >= BATTLE_TOTAL_SLOTS) return;
    int enemyIdx = slot - BATTLE_ALLY_SLOTS;
    s_drawTargetSlot = slot;
    
    // Read draw spell slots for this enemy
    uint32_t enemyBase = DRAW_SPELL_BASE + enemyIdx * DRAW_ENEMY_STRIDE;
    __try {
        for (int i = 0; i < DRAW_SLOTS_PER_ENEMY; i++) {
            uint8_t magicId = *(uint8_t*)(enemyBase + i * DRAW_SLOT_SIZE);
            s_turnDrawList[i].magicId = magicId;
            if (magicId != 0) s_turnDrawCount++;
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) { return; }
    
    s_drawListBuilt = true;
    
    // Log the draw list
    char nameBuf[64];
    const char* enemyName = GetSlotName(slot, nameBuf, sizeof(nameBuf));
    Log::Battle("BattleTTS: [DRAW-LIST] target=%s (slot%d) %d drawable spells:",
               enemyName, slot, s_turnDrawCount);
    for (int i = 0; i < DRAW_SLOTS_PER_ENEMY; i++) {
        uint8_t mid = s_turnDrawList[i].magicId;
        if (mid != 0) {
            char dnameBuf[64];
            Log::Battle("BattleTTS: [DRAW-LIST]   [%d] id=%u (%s%s)",
                       i, (unsigned)mid, GetDrawEntryName(mid, dnameBuf, sizeof(dnameBuf)),
                       (mid >= 0x40) ? " [GF]" : "");
        }
    }
}
