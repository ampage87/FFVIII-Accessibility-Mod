// battle_tts_menu.inl — Turn announce, command menu, magic/GF/item/draw sub-menus
// Included from battle_tts.cpp. Do not compile independently.
// v0.12.18: Extracted for readability.


// Confirmed addresses from v0.10.10 diagnostic
static const uint32_t BATTLE_CMD_CURSOR     = 0x01D76843; // BYTE, 0-3 (command slot index)
static const uint32_t BATTLE_MENU_PHASE     = 0x01D768D0; // BYTE (32=cmd menu, 3=executing, etc.)
static const uint32_t BATTLE_SUBMENU_CURSOR = 0x01D768EC; // BYTE, 0-N sub-menu list cursor (v0.10.16 confirmed)

// Savemap addresses
static const uint32_t SAVEMAP_PARTY_FORMATION = 0x1CFE74C; // 3 bytes: slot→charIdx (confirmed)
static const uint32_t SAVEMAP_CHAR_DATA_BASE  = 0x1CFE0E8; // char structs (UNCORRECTED — savemap stores ability IDs, not battle cmd IDs)
static const uint32_t SAVEMAP_CHAR_STRIDE     = 0x98;      // 152 bytes per character
static const uint32_t SAVEMAP_CHAR_EQUIP_CMD  = 0x50;      // 3 equipped command IDs within char struct

// Character names by savemap index (0=Squall through 7=Edea)
static const char* CHAR_NAMES[8] = {
    "Squall", "Zell", "Irvine", "Quistis",
    "Rinoa", "Selphie", "Seifer", "Edea"
};

// Savemap stores GF ABILITY IDs for equipped commands, not battle command IDs.
// Ability IDs = battle command IDs + 0x12.
// Slot 0 is always Attack (hardcoded, not stored in savemap).
static const char* GetCommandName(uint8_t abilityId) {
    switch (abilityId) {
        // Special: Attack is slot 0, hardcoded as 0x01
        case 0x01: return "Attack";
        // GF ability IDs for junctioned commands (ability = battle_cmd + 0x12)
        case 0x14: return "Magic";
        case 0x15: return "GF";
        case 0x16: return "Draw";
        case 0x17: return "Item";
        case 0x18: return "Card";
        case 0x19: return "Devour";
        case 0x21: return "MiniMog";
        case 0x22: return "Defend";
        case 0x23: return "Darkside";
        case 0x24: return "Recover";
        case 0x25: return "Absorb";
        case 0x26: return "Revive";
        case 0x27: return "LV Down";
        case 0x28: return "LV Up";
        case 0x29: return "Kamikaze";
        case 0x2A: return "Expendx2-1";
        case 0x2B: return "Expendx3-1";
        case 0x2C: return "Mad Rush";
        case 0x2D: return "Doom";
        case 0x36: return "Mug";
        case 0x38: return "Treatment";
        default:   return "???";
    }
}

// ============================================================================
// v0.10.17: Magic name table (kernel.bin IDs 0-56)
// ============================================================================
// IDs from Doomtrain wiki / kernel.bin Section 4. Verified against game at runtime.
static const char* MAGIC_NAMES[] = {
    "(none)",      // 0x00
    "Fire",        // 0x01
    "Fira",        // 0x02
    "Firaga",      // 0x03
    "Blizzard",    // 0x04
    "Blizzara",    // 0x05
    "Blizzaga",    // 0x06
    "Thunder",     // 0x07
    "Thundara",    // 0x08
    "Thundaga",    // 0x09
    "Water",       // 0x0A
    "Aero",        // 0x0B
    "Bio",         // 0x0C
    "Demi",        // 0x0D
    "Holy",        // 0x0E
    "Flare",       // 0x0F
    "Meteor",      // 0x10
    "Quake",       // 0x11
    "Tornado",     // 0x12
    "Ultima",      // 0x13
    "Apocalypse",  // 0x14
    "Cure",        // 0x15
    "Cura",        // 0x16
    "Curaga",      // 0x17
    "Life",        // 0x18
    "Full-Life",   // 0x19
    "Regen",       // 0x1A
    "Esuna",       // 0x1B
    "Dispel",      // 0x1C
    "Protect",     // 0x1D
    "Shell",       // 0x1E
    "Reflect",     // 0x1F
    "Aura",        // 0x20
    "Double",      // 0x21
    "Triple",      // 0x22
    "Haste",       // 0x23
    "Slow",        // 0x24
    "Stop",        // 0x25
    "Blind",       // 0x26
    "Confuse",     // 0x27
    "Sleep",       // 0x28
    "Silence",     // 0x29
    "Break",       // 0x2A
    "Death",       // 0x2B
    "Drain",       // 0x2C
    "Pain",        // 0x2D
    "Berserk",     // 0x2E
    "Float",       // 0x2F
    "Zombie",      // 0x30
    "Meltdown",    // 0x31
    "Scan",        // 0x32
    "Full-Cure",   // 0x33
    "Wall",        // 0x34
    "Rapture",     // 0x35
    "Percent",     // 0x36
    "Catastrophe", // 0x37
    "The End",     // 0x38
};
static const int MAGIC_NAMES_COUNT = sizeof(MAGIC_NAMES) / sizeof(MAGIC_NAMES[0]);

static const char* GetMagicName(uint8_t id) {
    if (id < MAGIC_NAMES_COUNT) return MAGIC_NAMES[id];
    return "???";
}

// ============================================================================
// v0.10.17: Sub-menu tracking state
// ============================================================================

struct MagicEntry { uint8_t id; uint8_t qty; };
static MagicEntry s_turnMagicList[32] = {};   // filtered list of spells with qty>0
static int s_turnMagicCount = 0;               // number of entries in filtered list
static uint8_t s_turnSubmenuCursor = 0xFF;     // last sub-menu cursor value
static bool s_inSubmenu = false;               // true when sub-menu is open
static uint8_t s_submenuCommandId = 0;         // ability ID of the command that opened the sub-menu
static bool s_magicListBuilt = false;          // true after we build the magic list for current turn
static DWORD s_submenuDebounceTick = 0;        // GetTickCount() when debounce started
static bool s_submenuDebouncing = false;        // true for 300ms after turn start (ignores sub-menu cursor)
static bool s_pendingSubmenuEntry = false;       // v0.10.112: delayed submenu entry after command scroll
static DWORD s_pendingSubmenuTick = 0;           // v0.10.112: GetTickCount() when pending entry was scheduled

// Build the filtered magic list for the active character.
// Reads savemap char struct +0x10 (32 slots × 2 bytes: magic_id, qty).
// Only includes slots with qty > 0, preserving savemap order (ascending magic_id).
static void BuildMagicList(uint8_t partySlot)
{
    s_turnMagicCount = 0;
    s_magicListBuilt = false;
    if (partySlot >= 3) return;
    
    __try {
        uint8_t charIdx = *(uint8_t*)(SAVEMAP_PARTY_FORMATION + partySlot);
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

// ============================================================================
// v0.10.98: GF sub-menu list (junctioned GFs for active character)
// ============================================================================
struct GFEntry { uint8_t gfIdx; char name[32]; };
static GFEntry s_turnGFList[16] = {};          // filtered list of junctioned GFs
static int s_turnGFCount = 0;                  // number of entries in filtered list
static bool s_gfListBuilt = false;             // true after we build the GF list for current turn

// Build the filtered GF list for the active character.
// Reads savemap char struct +0x58 (uint16 bitmask of junctioned GFs).
// Only includes GFs that are junctioned. Order follows bit index (0-15).
static void BuildGFList(uint8_t partySlot)
{
    s_turnGFCount = 0;
    s_gfListBuilt = false;
    if (partySlot >= 3) return;
    
    __try {
        uint8_t charIdx = *(uint8_t*)(SAVEMAP_PARTY_FORMATION + partySlot);
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

// ============================================================================
// v0.10.101: Item sub-menu list (battle items from display struct)
// ============================================================================
// Battle items: The display struct at 0x1D8DFF4 IS the authoritative visual layout.
// v0.10.105 diagnostic proved it persists from field menu into battle.
// Format: 32 x {uint8 id, uint8 qty} in the visual order set by Items > Battle.
// Engine renders only entries with qty>0, so cursor position maps to Nth non-zero entry.
static const uint32_t BATTLE_ORDER_ADDR = 0x1CFE77C;   // uint8[32] inventory slot indices
static const uint32_t ITEM_INVENTORY_ADDR = 0x1CFE79C;  // 198 x {id, qty} byte pairs
static const int BATTLE_ITEM_MAX = 32;

struct BattleItemEntry { uint8_t id; uint8_t qty; };
static BattleItemEntry s_turnItemList[BATTLE_ITEM_MAX] = {};
static int s_turnItemCount = 0;
static bool s_itemListBuilt = false;

// v0.10.103: MinHook on 0x4F81F0 REMOVED (v0.10.102 proved ESI calling convention
// mismatch — controller struct passed via ESI register, not cdecl stack argument).
// v0.10.104: POOL-SCAN approach — deep research revealed the Item controller is a
// pool node at 0x1D76BC8 (10 slots × 0x78 bytes). Scan for [node+0x0C]==0x4F81F0
// to find the active Item controller, then read [node+0x54] for absolute inventory
// index. No hooking or list-building needed.

// ============================================================================
// v0.10.104: Battle UI task pool scanner
// ============================================================================
// The engine uses a 10-slot pool at 0x1D76BC8 for battle UI controllers.
// Each node is 0x78 bytes. The Item handler address (0x4F81F0) is stored at
// +0x0C (or possibly +0x08 — we check both). When the Item sub-menu is open,
// the node contains the controller state including:
//   +0x20: inventory pointer (should be 0x01CFE79C)
//   +0x24: battle_order pointer (should be 0x01CFE77C)
//   +0x54: absolute inventory index (int16) — the currently highlighted item
static const uint32_t POOL_BASE     = 0x1D76BC8;
static const int      POOL_SLOTS    = 10;
static const int      POOL_STRIDE   = 0x78;
static const uint32_t ITEM_HANDLER  = 0x4F81F0;
static const int      HANDLER_OFF_A = 0x0C;  // primary handler offset
static const int      HANDLER_OFF_B = 0x08;  // fallback handler offset
static const int      POOL_INUSE    = 0x12;  // uint16 in-use flag
static const int      POOL_INV_PTR  = 0x20;  // uint32 inventory pointer
static const int      POOL_CURSOR   = 0x54;  // int16 absolute inventory index

// Find the active Item controller node in the pool.
// Returns pointer to the node, or NULL if not found.
static uint8_t* FindItemControllerNode()
{
    __try {
        for (int i = 0; i < POOL_SLOTS; i++) {
            uint8_t* node = (uint8_t*)(POOL_BASE + i * POOL_STRIDE);
            uint32_t handler = *(uint32_t*)(node + HANDLER_OFF_A);
            if (handler == ITEM_HANDLER) return node;
        }
        // Fallback: check +0x08 in case handler is stored there
        for (int i = 0; i < POOL_SLOTS; i++) {
            uint8_t* node = (uint8_t*)(POOL_BASE + i * POOL_STRIDE);
            uint32_t handler = *(uint32_t*)(node + HANDLER_OFF_B);
            if (handler == ITEM_HANDLER) return node;
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
    return nullptr;
}

// Read the currently highlighted item from the pool node.
// Returns true if successful and populates outId/outQty.
static bool ReadItemFromPoolNode(uint8_t* node, uint8_t* outId, uint8_t* outQty, int16_t* outAbsIdx)
{
    if (!node) return false;
    __try {
        int16_t absIdx = *(int16_t*)(node + POOL_CURSOR);
        if (absIdx < 0 || absIdx >= 198) return false;
        uint8_t* inv = (uint8_t*)ITEM_INVENTORY_ADDR;
        *outAbsIdx = absIdx;
        *outId = inv[absIdx * 2];
        *outQty = inv[absIdx * 2 + 1];
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
    return false;
}

// v0.10.105: Display struct at 0x1D8DFF4 is the authoritative visual layout.
// Confirmed by F12 diagnostic: it IS populated during battle (persists from
// field menu Items > Battle arrangement). Contains 32 x {id, qty} pairs in
// visual order. Engine renders only entries with qty>0, skipping empties.
// We filter the same way to build a cursor-indexed list matching the screen.
static const uint32_t ITEM_DISPLAY_STRUCT = 0x1D8DFF4;  // 32 x {uint8 id, uint8 qty}

static void BuildItemList()
{
    s_turnItemCount = 0;
    s_itemListBuilt = false;
    __try {
        uint8_t* battleOrder = (uint8_t*)BATTLE_ORDER_ADDR;
        uint8_t* inventory   = (uint8_t*)ITEM_INVENTORY_ADDR;
        
        // v0.10.105: Two modes depending on display struct state.
        // When display struct at 0x1D8DFF4 is populated (after visiting field
        // menu Items > Battle), cursor indexes into ALL 32 positions (with gaps).
        // When zeroed (normal gameplay), cursor indexes into filtered battle items.
        uint8_t* ds = (uint8_t*)ITEM_DISPLAY_STRUCT;
        bool dsPopulated = false;
        for (int i = 0; i < 64; i++) {
            if (ds[i] != 0) { dsPopulated = true; break; }
        }
        
        int visCount = 0;
        if (dsPopulated) {
            // Display struct mode: cursor indexes ALL 32 positions including empties
            for (int i = 0; i < BATTLE_ITEM_MAX; i++) {
                s_turnItemList[i].id  = ds[i * 2];
                s_turnItemList[i].qty = ds[i * 2 + 1];
                if (s_turnItemList[i].id >= 1 && s_turnItemList[i].id < 33 && s_turnItemList[i].qty > 0)
                    visCount++;
            }
            s_turnItemCount = BATTLE_ITEM_MAX;
            Log::Battle("BattleTTS: [ITEM-LIST] Display struct mode: %d visible of %d", visCount, s_turnItemCount);
        } else {
            // Filtered mode: cursor indexes only valid battle items (compacted)
            for (int i = 0; i < BATTLE_ITEM_MAX; i++) {
                uint8_t invIdx = battleOrder[i];
                if (invIdx >= 198) continue;
                uint8_t id  = inventory[invIdx * 2];
                uint8_t qty = inventory[invIdx * 2 + 1];
                if (id >= 1 && id < 33 && qty > 0) {
                    s_turnItemList[s_turnItemCount].id  = id;
                    s_turnItemList[s_turnItemCount].qty = qty;
                    s_turnItemCount++;
                    visCount++;
                }
            }
            Log::Battle("BattleTTS: [ITEM-LIST] Filtered mode: %d battle items", s_turnItemCount);
        }
        
        s_itemListBuilt = true;
        Log::Battle("BattleTTS: [ITEM-LIST] battle_order loaded: %d battle items of %d total", visCount, s_turnItemCount);
        for (int i = 0; i < s_turnItemCount; i++) {
            uint8_t id = s_turnItemList[i].id;
            uint8_t qty = s_turnItemList[i].qty;
            if (id >= 1 && id < 33 && qty > 0) {
                Log::Battle("BattleTTS: [ITEM-LIST]   [%d] bo->inv id=%u qty=%u -> %s",
                           i, (unsigned)id, (unsigned)qty, GetBattleItemName(id));
            }
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        Log::Battle("BattleTTS: [ITEM-LIST] EXCEPTION reading battle_order");
    }
}

// ============================================================================
// v0.10.109: Draw sub-menu list (drawable spells from target enemy)
// ============================================================================
// Draw spell slots at 0x1D28F18 + enemyIdx * 0x47, 4 slots per enemy.
// Each slot is uint32, byte 0 = magic_id (0 = empty). Confirmed v0.10.107 diagnostic.
// Target enemy from target bitmask at 0x01D76884 (persists into Draw phase).
struct DrawEntry { uint8_t magicId; };
static DrawEntry s_turnDrawList[4] = {};        // all 4 slots (including empties)
static int s_turnDrawCount = 0;                 // number of non-empty entries
static bool s_drawListBuilt = false;
static int s_drawTargetSlot = -1;               // entity slot (3-6) of draw target

// v0.10.109 fix: Draw uses a DIFFERENT cursor byte from Magic/GF/Item.
// 0x01D768EC only fires during phase transitions (engine init), NOT during
// active up/down navigation of the draw spell list.
// 0x01D768D8 is the real draw cursor (confirmed by CURSOR-HUNT diagnostic).
static const uint32_t DRAW_CURSOR_ADDR = 0x01D768D8;
static const uint32_t DRAW_STOCK_CAST_ADDR = 0x01D768D9; // v0.10.111: 0=Stock, 1=Cast
static uint8_t s_drawCursorPrev = 0xFF;         // previous draw cursor value for change detection
static uint8_t s_drawStockCastPrev = 0xFF;      // previous Stock/Cast cursor value
static uint8_t s_lastDrawerPartySlot = 0xFF;    // v0.10.112: party slot of character who last used Draw
static uint8_t s_drawLastMenuPhase = 0xFF;      // v0.10.112: track menuPhase for phase-transition resets

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
            Log::Battle("BattleTTS: [DRAW-LIST]   [%d] id=%u (%s)",
                       i, (unsigned)mid, GetMagicName(mid));
        }
    }
}

// Turn/command tracking state
static uint8_t s_turnActiveCharId = 0xFF;    // last active_char_id we announced
static uint8_t s_turnCmdCursor = 0xFF;       // last command cursor position
static uint8_t s_turnCharCommands[4] = {};    // command IDs for current turn's 4 slots

static const char* GetBattleCharName(uint8_t partySlot) {
    if (partySlot >= 3) return "???";
    __try {
        uint8_t charIdx = *(uint8_t*)(SAVEMAP_PARTY_FORMATION + partySlot);
        if (charIdx < 8) return CHAR_NAMES[charIdx];
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
    return "???";
}

static void BuildCharCommandList(uint8_t partySlot) {
    s_turnCharCommands[0] = 0x01; // Attack always first
    s_turnCharCommands[1] = 0x00;
    s_turnCharCommands[2] = 0x00;
    s_turnCharCommands[3] = 0x00;
    if (partySlot >= 3) return;
    
    __try {
        uint8_t charIdx = *(uint8_t*)(SAVEMAP_PARTY_FORMATION + partySlot);
        if (charIdx >= 8) return;
        
        uint8_t* charBase = (uint8_t*)(SAVEMAP_CHAR_DATA_BASE + charIdx * SAVEMAP_CHAR_STRIDE);
        __try {
            s_turnCharCommands[1] = charBase[SAVEMAP_CHAR_EQUIP_CMD + 0];
            s_turnCharCommands[2] = charBase[SAVEMAP_CHAR_EQUIP_CMD + 1];
            s_turnCharCommands[3] = charBase[SAVEMAP_CHAR_EQUIP_CMD + 2];
            Log::Battle("BattleTTS: [CMD] charIdx=%d cmds=[0x%02X,0x%02X,0x%02X] = [%s,%s,%s]",
                       (int)charIdx, s_turnCharCommands[1], s_turnCharCommands[2], s_turnCharCommands[3],
                       GetCommandName(s_turnCharCommands[1]),
                       GetCommandName(s_turnCharCommands[2]),
                       GetCommandName(s_turnCharCommands[3]));
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            Log::Battle("BattleTTS: [CMD] EXCEPTION reading cmds for charIdx=%d", (int)charIdx);
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
}

static void PollTurnAndCommands()
{
    if (!s_pActiveCharId) return;
    
    __try {
        uint8_t activeChar = *s_pActiveCharId;
        
        // Turn start: active_char_id transitions to a valid slot
        if (activeChar < 3 && activeChar != s_turnActiveCharId) {
            s_turnActiveCharId = activeChar;
            BuildCharCommandList(activeChar);
            
            // Reset sub-menu state for new turn
            s_inSubmenu = false;
            s_turnSubmenuCursor = 0xFF;
            s_submenuCommandId = 0;
            s_magicListBuilt = false;
            s_turnMagicCount = 0;
            s_gfListBuilt = false;
            s_turnGFCount = 0;
            s_itemListBuilt = false;
            s_turnItemCount = 0;
            s_drawListBuilt = false;
            s_turnDrawCount = 0;
            s_drawTargetSlot = -1;
            s_drawCursorPrev = 0xFF;
            s_drawStockCastPrev = 0xFF;
            s_drawLastMenuPhase = 0xFF;
            s_pendingSubmenuEntry = false;
            s_pendingSubmenuTick = 0;
            s_submenuDebouncing = true;
            s_submenuDebounceTick = GetTickCount();
            
            // v0.10.97 fix: Snapshot current target bitmask+scope on new turn so
            // PollTargetSelection doesn't see a false "change" from 0 to stale value.
            __try { s_lastTargetBitmask = *(uint8_t*)BATTLE_TARGET_BITMASK; } __except(EXCEPTION_EXECUTE_HANDLER) {}
            __try { s_lastTargetScope = *(uint8_t*)BATTLE_TARGET_SCOPE; } __except(EXCEPTION_EXECUTE_HANDLER) {}
            s_inTargetSelect = false;
            
            // v0.10.22: Check limit toggle byte for initial announcement
            uint8_t initToggle = 0;
            __try { initToggle = *(uint8_t*)BATTLE_LIMIT_TOGGLE; } __except(EXCEPTION_EXECUTE_HANDLER) {}
            s_limitBreakActive = (initToggle == 64);
            s_lastLimitToggle = initToggle;
            
            // Announce "[Name]'s turn. [First command]." (or Limit Break if toggle=64)
            const char* name = GetBattleCharName(activeChar);
            const char* cmd = s_limitBreakActive ? "Limit Break" : GetCommandName(s_turnCharCommands[0]);
            char buf[128];
            snprintf(buf, sizeof(buf), "%s's turn. %s.", name, cmd);
            BattleSpeak(buf, PRIO_TURN, true);
            Log::Battle("BattleTTS: [TURN] %s (slot %d, limitToggle=%u)", buf, (int)activeChar, (unsigned)initToggle);
            
            // Set cursor to 0 so we don't re-announce the initial command
            s_turnCmdCursor = 0;
        }
        else if (activeChar == 0xFF && s_turnActiveCharId != 0xFF) {
            // Turn ended
            s_turnActiveCharId = 0xFF;
            s_turnCmdCursor = 0xFF;
            s_inSubmenu = false;
            s_turnSubmenuCursor = 0xFF;
        }
        
        // Command cursor navigation (only while a turn is active)
        if (s_turnActiveCharId < 3) {
            bool cmdCursorChangedThisFrame = false;  // v0.10.112: suppress false submenu entry
            uint8_t cursor = *(uint8_t*)BATTLE_CMD_CURSOR;
            if (cursor < 4 && cursor != s_turnCmdCursor) {
                s_turnCmdCursor = cursor;
                // Returning to command menu from sub-menu
                if (s_inSubmenu) {
                    s_inSubmenu = false;
                    s_turnSubmenuCursor = 0xFF;
                    // v0.10.112: Reset draw tracking so re-entry announces initial items
                    s_drawCursorPrev = 0xFF;
                    s_drawStockCastPrev = 0xFF;
                    s_drawListBuilt = false;
                    s_drawLastMenuPhase = 0xFF;
                    Log::Battle("BattleTTS: [SUBMENU] Exited sub-menu, back to command menu");
                }
                // v0.10.112: Suppress false submenu entry on this frame AND capture
                // baseline. Then schedule a delayed forced entry after 150ms so the
                // command name has time to speak before the submenu item queues.
                cmdCursorChangedThisFrame = true;
                __try { s_turnSubmenuCursor = *(uint8_t*)BATTLE_SUBMENU_CURSOR; } __except(EXCEPTION_EXECUTE_HANDLER) {}
                s_pendingSubmenuEntry = true;
                s_pendingSubmenuTick = GetTickCount();
                // v0.10.22: cursor=0 may be Attack or Limit Break depending on toggle byte
                const char* cmd;
                if (cursor == 0) {
                    uint8_t toggle = 0;
                    __try { toggle = *(uint8_t*)BATTLE_LIMIT_TOGGLE; } __except(EXCEPTION_EXECUTE_HANDLER) {}
                    s_limitBreakActive = (toggle == 64);
                    s_lastLimitToggle = toggle;
                    cmd = s_limitBreakActive ? "Limit Break" : GetCommandName(s_turnCharCommands[0]);
                } else {
                    cmd = GetCommandName(s_turnCharCommands[cursor]);
                }
                BattleSpeak(cmd, PRIO_MENU, true);
                Log::Battle("BattleTTS: [CMD-NAV] cursor=%d -> %s", (int)cursor, cmd);
            }
            
            // v0.10.19/20: Limit Break toggle detection moved to PollLimitToggleFast()
            
            // v0.10.17: Sub-menu cursor tracking
            // Debounce: ignore sub-menu cursor for 300ms after turn start.
            // The engine resets this byte during turn transitions, causing false
            // sub-menu entry detection (v0.10.17 glitch: "Fire" spoken on cmd menu).
            if (s_submenuDebouncing) {
                if (GetTickCount() - s_submenuDebounceTick > 300) {
                    s_submenuDebouncing = false;
                    // Capture current value as baseline after debounce expires
                    s_turnSubmenuCursor = *(uint8_t*)BATTLE_SUBMENU_CURSOR;
                }
            }
            
            uint8_t subCursor = *(uint8_t*)BATTLE_SUBMENU_CURSOR;
            if (!s_submenuDebouncing && !cmdCursorChangedThisFrame && subCursor != s_turnSubmenuCursor) {
                if (!s_inSubmenu && s_turnCmdCursor < 4) {
                    // Entering sub-menu — record which command opened it
                    s_submenuCommandId = s_turnCharCommands[s_turnCmdCursor];
                    s_inSubmenu = true;
                    s_pendingSubmenuEntry = false;  // v0.10.112: cancel delayed entry
                    Log::Battle("BattleTTS: [SUBMENU] Entered sub-menu for cmd 0x%02X (%s) at cursor %d",
                               (unsigned)s_submenuCommandId,
                               GetCommandName(s_submenuCommandId),
                               (int)s_turnCmdCursor);
                    
                    // Build spell list if Magic sub-menu
                    if (s_submenuCommandId == 0x14 && !s_magicListBuilt) { // 0x14 = Magic ability ID
                        BuildMagicList(s_turnActiveCharId);
                    }
                    // v0.10.98: Build GF list if GF sub-menu
                    if (s_submenuCommandId == 0x15 && !s_gfListBuilt) { // 0x15 = GF ability ID
                        BuildGFList(s_turnActiveCharId);
                    }
                    // v0.10.104: Build item list if Item sub-menu
                    if (s_submenuCommandId == 0x17 && !s_itemListBuilt) {
                        BuildItemList();
                    }
                    // v0.10.109: Build draw list if Draw sub-menu
                    if (s_submenuCommandId == 0x16 && !s_drawListBuilt) {
                        s_lastDrawerPartySlot = s_turnActiveCharId;  // v0.10.112: track who is drawing
                        BuildDrawList();
                    }

                }
                
                s_turnSubmenuCursor = subCursor;
                
                // Announce the current sub-menu item
                if (s_inSubmenu) {
                    if (s_submenuCommandId == 0x14 && s_magicListBuilt) {
                        // Magic sub-menu: read spell at cursor position
                        // Cursor 0-3 is visible position. With <=4 spells, maps directly.
                        // TODO: handle scroll offset for >4 spells (need to find page offset byte)
                        if ((int)subCursor < s_turnMagicCount) {
                            const char* spellName = GetMagicName(s_turnMagicList[subCursor].id);
                            int qty = (int)s_turnMagicList[subCursor].qty;
                            char buf[128];
                            snprintf(buf, sizeof(buf), "%s, %d", spellName, qty);
                            BattleSpeak(buf, PRIO_MENU, true);
                            Log::Battle("BattleTTS: [SUBMENU-NAV] Magic cursor=%d -> %s x%d (id=0x%02X)",
                                       (int)subCursor, spellName, qty,
                                       (unsigned)s_turnMagicList[subCursor].id);
                        } else {
                            Log::Battle("BattleTTS: [SUBMENU-NAV] Magic cursor=%d out of range (count=%d)",
                                       (int)subCursor, s_turnMagicCount);
                        }
                    } else if (s_submenuCommandId == 0x15 && s_gfListBuilt) {
                        // v0.10.98: GF sub-menu — announce junctioned GF at cursor position
                        if ((int)subCursor < s_turnGFCount) {
                            const char* gfName = s_turnGFList[subCursor].name;
                            char buf[128];
                            snprintf(buf, sizeof(buf), "%s", gfName);
                            BattleSpeak(buf, PRIO_MENU, true);
                            Log::Battle("BattleTTS: [SUBMENU-NAV] GF cursor=%d -> %s (gfIdx=%d)",
                                       (int)subCursor, gfName, (int)s_turnGFList[subCursor].gfIdx);
                        } else {
                            Log::Battle("BattleTTS: [SUBMENU-NAV] GF cursor=%d out of range (count=%d)",
                                       (int)subCursor, s_turnGFCount);
                        }
                    } else if (s_submenuCommandId == 0x17) {
                        // v0.10.106: Item sub-menu — dual-source announce (cleaned up from v0.10.105 diagnostic).
                        // Display struct at 0x1D8DFF4 when populated (after Items > Battle), else inv[cursor].
                        int sc = (int)subCursor;
                        uint8_t annId = 0, annQty = 0;
                        
                        // Check if display struct is populated
                        bool dsActive = false;
                        __try {
                            uint8_t* ds = (uint8_t*)ITEM_DISPLAY_STRUCT;
                            for (int q = 0; q < 64; q++) {
                                if (ds[q] != 0) { dsActive = true; break; }
                            }
                            if (dsActive && sc < 32) {
                                annId = ds[sc * 2];
                                annQty = ds[sc * 2 + 1];
                            }
                        } __except(EXCEPTION_EXECUTE_HANDLER) {}
                        
                        if (!dsActive) {
                            // Fallback: direct inventory at cursor position
                            __try {
                                uint8_t* inv = (uint8_t*)ITEM_INVENTORY_ADDR;
                                if (sc < 198) { annId = inv[sc * 2]; annQty = inv[sc * 2 + 1]; }
                            } __except(EXCEPTION_EXECUTE_HANDLER) {}
                        }
                        
                        int page = (sc / 4) + 1;
                        int itemNum = (sc % 4) + 1;
                        
                        if (annId >= 1 && annId < 33 && annQty > 0) {
                            const char* itemName = GetBattleItemName(annId);
                            char buf[128];
                            snprintf(buf, sizeof(buf), "%s, quantity %d, page %d, item %d",
                                     itemName, (int)annQty, page, itemNum);
                            BattleSpeak(buf, PRIO_MENU, true);
                            Log::Battle("BattleTTS: [ITEM] cursor=%d -> %s x%d page%d item%d",
                                       sc, itemName, (int)annQty, page, itemNum);
                        } else {
                            char buf[64];
                            snprintf(buf, sizeof(buf), "Empty, page %d, item %d", page, itemNum);
                            BattleSpeak(buf, PRIO_MENU, true);
                            Log::Battle("BattleTTS: [ITEM] cursor=%d -> Empty page%d item%d", sc, page, itemNum);
                        }
                    } else if (s_submenuCommandId == 0x16 && s_drawListBuilt) {
                        // v0.10.112: Draw sub-menu — generic subCursor fires on phase transitions,
                        // NOT during active navigation. All draw spell announces go through the
                        // draw-specific cursor poll at 0x01D768D8 below. Log only here.
                        Log::Battle("BattleTTS: [DRAW-NAV] generic subCursor=%d (ignored, handled by draw poll)",
                                   (int)subCursor);
                    } else {
                        // Other sub-menus — log for diagnostic
                        Log::Battle("BattleTTS: [SUBMENU-NAV] cmd=0x%02X cursor=%d (unhandled)",
                                   (unsigned)s_submenuCommandId, (int)subCursor);
                    }
                }
            }
            
            // v0.10.112: Delayed submenu entry after command scroll.
            // 150ms after scrolling to a new command, force-enter the submenu and
            // announce the current item with interrupt=false (queued after command name).
            if (s_pendingSubmenuEntry && !s_inSubmenu && 
                GetTickCount() - s_pendingSubmenuTick > 150) {
                s_pendingSubmenuEntry = false;
                if (s_turnCmdCursor < 4) {
                    uint8_t sc = 0;
                    __try { sc = *(uint8_t*)BATTLE_SUBMENU_CURSOR; } __except(EXCEPTION_EXECUTE_HANDLER) {}
                    s_submenuCommandId = s_turnCharCommands[s_turnCmdCursor];
                    s_inSubmenu = true;
                    s_turnSubmenuCursor = sc;
                    
                    Log::Battle("BattleTTS: [SUBMENU] Delayed entry for cmd 0x%02X (%s) cursor %d",
                               (unsigned)s_submenuCommandId,
                               GetCommandName(s_submenuCommandId), (int)sc);
                    
                    // Build lists
                    if (s_submenuCommandId == 0x14 && !s_magicListBuilt)
                        BuildMagicList(s_turnActiveCharId);
                    if (s_submenuCommandId == 0x15 && !s_gfListBuilt)
                        BuildGFList(s_turnActiveCharId);
                    if (s_submenuCommandId == 0x17 && !s_itemListBuilt)
                        BuildItemList();
                    if (s_submenuCommandId == 0x16 && !s_drawListBuilt) {
                        s_lastDrawerPartySlot = s_turnActiveCharId;
                        BuildDrawList();
                    }
                    
                    // Announce current item (queued, not interrupting command name)
                    if (s_submenuCommandId == 0x14 && s_magicListBuilt) {
                        if ((int)sc < s_turnMagicCount) {
                            const char* spellName = GetMagicName(s_turnMagicList[sc].id);
                            int qty = (int)s_turnMagicList[sc].qty;
                            char buf[128];
                            snprintf(buf, sizeof(buf), "%s, %d", spellName, qty);
                            BattleSpeak(buf, PRIO_MENU, false);
                            Log::Battle("BattleTTS: [SUBMENU-DELAYED] Magic cursor=%d -> %s x%d",
                                       (int)sc, spellName, qty);
                        }
                    } else if (s_submenuCommandId == 0x15 && s_gfListBuilt) {
                        if ((int)sc < s_turnGFCount) {
                            BattleSpeak(s_turnGFList[sc].name, PRIO_MENU, false);
                            Log::Battle("BattleTTS: [SUBMENU-DELAYED] GF cursor=%d -> %s",
                                       (int)sc, s_turnGFList[sc].name);
                        }
                    } else if (s_submenuCommandId == 0x17 && s_itemListBuilt) {
                        // Item: read from display struct or inventory
                        uint8_t annId = 0, annQty = 0;
                        __try {
                            uint8_t* ds = (uint8_t*)0x1D8DFF4;
                            bool dsActive = false;
                            for (int q = 0; q < 64; q++) { if (ds[q] != 0) { dsActive = true; break; } }
                            if (dsActive && (int)sc < 32) { annId = ds[sc * 2]; annQty = ds[sc * 2 + 1]; }
                            if (!dsActive && (int)sc < 198) {
                                uint8_t* inv = (uint8_t*)0x1CFE79C;
                                annId = inv[sc * 2]; annQty = inv[sc * 2 + 1];
                            }
                        } __except(EXCEPTION_EXECUTE_HANDLER) {}
                        if (annId >= 1 && annId < 33 && annQty > 0) {
                            const char* itemName = GetBattleItemName(annId);
                            char buf[128];
                            int page = ((int)sc / 4) + 1;
                            int itemNum = ((int)sc % 4) + 1;
                            snprintf(buf, sizeof(buf), "%s, quantity %d, page %d, item %d",
                                     itemName, (int)annQty, page, itemNum);
                            BattleSpeak(buf, PRIO_MENU, false);
                            Log::Battle("BattleTTS: [SUBMENU-DELAYED] Item cursor=%d -> %s x%d",
                                       (int)sc, itemName, (int)annQty);
                        }
                    }
                    // Draw and Item are handled by their dedicated poll blocks below
                }
            }
            
            // v0.10.109: Draw-specific cursor poll.
            // Draw uses a SEPARATE cursor byte (0x01D768D8) from other sub-menus.
            // 0x01D768EC only fires during engine init/phase transitions, NOT during
            // active up/down navigation. We poll 0x01D768D8 independently here.
            // NOTE: Also retry BuildDrawList if it hasn't succeeded yet — the target
            // bitmask at 0x01D76884 may not be set until after target confirmation,
            // which happens AFTER the submenu entry event fires.
            if (s_inSubmenu && s_submenuCommandId == 0x16 && !s_drawListBuilt) {
                BuildDrawList();  // retry until target bitmask is populated
            }
            if (s_inSubmenu && s_submenuCommandId == 0x16 && s_drawListBuilt) {
                // v0.10.112: Keep drawer slot updated every frame while draw submenu is open
                if (s_turnActiveCharId < 3)
                    s_lastDrawerPartySlot = s_turnActiveCharId;
                
                // v0.10.112: Detect menuPhase transitions to reset cursor tracking.
                // When canceling from Stock/Cast (phase 23) back to spell list (phase 14),
                // reset draw cursor prev so the current spell re-announces.
                // When canceling from spell list back to target select, reset everything.
                uint8_t drawPhaseNow = 0;
                __try { drawPhaseNow = *(uint8_t*)0x01D768D0; } __except(EXCEPTION_EXECUTE_HANDLER) {}
                if (s_drawLastMenuPhase != 0xFF && drawPhaseNow != s_drawLastMenuPhase) {
                    if (s_drawLastMenuPhase == 23 && drawPhaseNow < 23) {
                        // Left Stock/Cast prompt backward → back to spell list
                        s_drawCursorPrev = 0xFF;
                        s_drawStockCastPrev = 0xFF;
                        Log::Battle("BattleTTS: [DRAW] Phase %u->%u: reset cursor tracking (back to spell list)",
                                   (unsigned)s_drawLastMenuPhase, (unsigned)drawPhaseNow);
                    }
                    if (s_drawLastMenuPhase == 14 && drawPhaseNow < 14) {
                        // Left spell list backward → back to target selection
                        s_drawCursorPrev = 0xFF;
                        s_drawStockCastPrev = 0xFF;
                        s_drawListBuilt = false;  // force rebuild with potentially new target
                        s_lastTargetBitmask = 0;  // force target re-announce
                        s_lastTargetScope = 0;
                        Log::Battle("BattleTTS: [DRAW] Phase %u->%u: reset target+draw tracking (back to target select)",
                                   (unsigned)s_drawLastMenuPhase, (unsigned)drawPhaseNow);
                    }
                }
                s_drawLastMenuPhase = drawPhaseNow;
                
                uint8_t drawCur = 0xFF;
                __try { drawCur = *(uint8_t*)DRAW_CURSOR_ADDR; } __except(EXCEPTION_EXECUTE_HANDLER) {}
                if (drawCur != s_drawCursorPrev && drawCur < DRAW_SLOTS_PER_ENEMY) {
                    s_drawCursorPrev = drawCur;
                    uint8_t mid = s_turnDrawList[drawCur].magicId;
                    if (mid != 0) {
                        const char* spellName = GetMagicName(mid);
                        char buf[128];
                        snprintf(buf, sizeof(buf), "%s", spellName);
                        BattleSpeak(buf, PRIO_MENU, true);
                        Log::Battle("BattleTTS: [DRAW-CUR] draw_cursor=%d -> %s (id=%u)",
                                   (int)drawCur, spellName, (unsigned)mid);
                    } else {
                        BattleSpeak("Empty", PRIO_MENU, true);
                        Log::Battle("BattleTTS: [DRAW-CUR] draw_cursor=%d -> Empty", (int)drawCur);
                    }
                } else if (drawCur != s_drawCursorPrev && drawCur != 0xFF) {
                    s_drawCursorPrev = drawCur;  // out of range, track but don't announce
                }
                // v0.10.111: Stock/Cast cursor at 0x01D768D9 (0=Stock, 1=Cast)
                // v0.10.112: Only poll during Stock/Cast phase (menuPhase == 23).
                // menuPhase=14 is the spell list; Stock/Cast prompt is specifically at 23.
                // Without this guard, D9=0 (stale) triggers false "Stock" during spell list.
                uint8_t drawMenuPhase = 0;
                __try { drawMenuPhase = *(uint8_t*)0x01D768D0; } __except(EXCEPTION_EXECUTE_HANDLER) {}
                if (drawMenuPhase == 23) {
                    uint8_t stockCast = 0xFF;
                    __try { stockCast = *(uint8_t*)DRAW_STOCK_CAST_ADDR; } __except(EXCEPTION_EXECUTE_HANDLER) {}
                    if (stockCast != s_drawStockCastPrev && stockCast <= 1) {
                        s_drawStockCastPrev = stockCast;
                        const char* actionName = (stockCast == 0) ? "Stock" : "Cast";
                        BattleSpeak(actionName, PRIO_MENU, true);
                        Log::Battle("BattleTTS: [DRAW-ACTION] Stock/Cast cursor=%u -> %s (phase=%u)",
                                   (unsigned)stockCast, actionName, (unsigned)drawMenuPhase);
                    }
                }
            }
        }

    } __except (EXCEPTION_EXECUTE_HANDLER) {}
}

// ============================================================================
// Battle entry/exit detection
// ============================================================================

