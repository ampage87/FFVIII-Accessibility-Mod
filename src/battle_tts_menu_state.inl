// battle_tts_menu_state.inl — Constants, name tables, structs, and module
// statics for battle_tts_menu.inl. Pure declarations; no function bodies
// beyond trivial table lookups (GetCommandName / GetMagicName /
// GetDrawEntryName).
//
// Included from battle_tts_menu.inl (via the textual include chain that
// reaches battle_tts.cpp through namespace BattleTTS). MUST be included
// FIRST in the menu chain so subsequent .inl files see every static and
// every type declared here.
//
// v0.16.5: Extracted from the v0.16.4 monolithic battle_tts_menu.inl as
// part of the mechanical .inl split. No behavior change.


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

// v0.45.0 (#106): **THE PARTY FORMATION IS STALE IN A DREAM BATTLE, AND THREE
// LISTS WERE READ THROUGH IT.**
//
// Aaron, on the Trabia Canyon dream: *"I rearranged Ward's magic but the
// rearrangement wasn't properly reflected in the battle menu, resulting in my
// casting spells which I did not intend."*
//
// `GetBattleCharName` has known since v0.17.8.17.2 that `SAVEMAP_PARTY_FORMATION`
// is stale here -- its own comment records a dream where the array read
// `[05 00 01]` (Selphie/Squall/Zell) while the live actors were Ward, Laguna and
// Kiros -- and it works around that with the actor-kind byte at compStats
// `+0x1C3`. But `BuildMagicList`, `BuildGFList` and `BuildCharCommandList` all
// index the savemap character array through that same stale byte, so in a dream
// they can build **another character's list** and the cursor position the player
// hears has nothing to do with the spell that gets cast.
//
// The bridge between the two is the character record's MODEL byte at `+0x08`.
// The menu side already relies on it (`menu_tts_junction.inl` logs
// `roster[0]=5 modelId=9` and names that character Kiros), so during a dream the
// savemap array holds the dream members with their model bytes set to 8, 9, 10.
// Given the actor kind, the character index is the record whose model matches.
//
// **This changes nothing in a normal battle.** Actor kinds there are 0-7, so the
// override never triggers and the formation byte is used exactly as before. It
// also changes nothing in a dream where the formation already happens to agree
// -- the search returns the same index. It differs only where the two disagree,
// which is the bug.
static const int SAVEMAP_CHAR_MODEL = 0x08;   // char record -> model id

static uint8_t BattleSlotCharIdx(uint8_t partySlot, uint8_t* outKind, bool* outOverride)
{
    uint8_t charIdx = 0xFF, kind = 0xFF;
    bool over = false;
    __try { charIdx = *(uint8_t*)(SAVEMAP_PARTY_FORMATION + partySlot); }
    __except (EXCEPTION_EXECUTE_HANDLER) { charIdx = 0xFF; }
    __try {
        kind = *(uint8_t*)(BATTLE_COMP_STATS_BASE
                           + partySlot * BATTLE_COMP_STATS_STRIDE + 0x1C3);
    } __except (EXCEPTION_EXECUTE_HANDLER) { kind = 0xFF; }

    if (kind >= 8 && kind <= 10) {
        __try {
            for (int i = 0; i < 8; i++) {
                const uint8_t model = *(uint8_t*)(SAVEMAP_CHAR_DATA_BASE
                                                  + i * SAVEMAP_CHAR_STRIDE + SAVEMAP_CHAR_MODEL);
                if (model != kind) continue;
                over = (i != (int)charIdx);
                charIdx = (uint8_t)i;
                break;
            }
        } __except (EXCEPTION_EXECUTE_HANDLER) { }
    }
    if (outKind)     *outKind = kind;
    if (outOverride) *outOverride = over;
    return charIdx;
}

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

// v0.13.46: Get name for a draw list entry (handles both magic spells and GFs).
// In FF8's draw system, magic IDs < 0x40 are spells, IDs >= 0x40 are GFs.
// GF index = draw_id - 0x40 (e.g. id=67=0x43 -> GF index 3 = Siren).
static const char* GetDrawEntryName(uint8_t drawId, char* nameBuf, int bufSize)
{
    if (drawId == 0) return "Empty";
    if (drawId < 0x40) return GetMagicName(drawId);
    // GF entry: look up name from savemap
    int gfIdx = drawId - 0x40;
    if (gfIdx >= 0 && gfIdx < 16) {
        __try {
            uint8_t* gfBase = (uint8_t*)(SAVEMAP_GF_BASE + gfIdx * SAVEMAP_GF_STRIDE);
            if (gfBase[0x11] != 0) {
                DecodeFF8String(gfBase, nameBuf, bufSize);
                if (nameBuf[0] != '\0') return nameBuf;
            }
        } __except(EXCEPTION_EXECUTE_HANDLER) {}
        static const char* GF_DRAW_NAMES[] = {
            "Quezacotl", "Shiva", "Ifrit", "Siren", "Brothers", "Diablos",
            "Carbuncle", "Leviathan", "Pandemona", "Cerberus", "Alexander",
            "Doomtrain", "Bahamut", "Cactuar", "Tonberry", "Eden"
        };
        return GF_DRAW_NAMES[gfIdx];
    }
    snprintf(nameBuf, bufSize, "Unknown(%u)", (unsigned)drawId);
    return nameBuf;
}

// ============================================================================
// v0.10.17: Sub-menu tracking state
// ============================================================================

struct MagicEntry { uint8_t id; uint8_t qty; };
static MagicEntry s_turnMagicList[32] = {};   // filtered list of spells with qty>0
static int s_turnMagicCount = 0;               // number of entries in filtered list
static uint8_t s_turnSubmenuCursor = 0xFF;     // last sub-menu cursor value
// v0.13.51 hotfix: s_inSubmenu moved to battle_tts_hp.inl so ewm.inl can
// read it. It's still modified here; the declaration just lives earlier in
// the translation unit now.
static uint8_t s_submenuCommandId = 0;         // ability ID of the command that opened the sub-menu
static bool s_magicListBuilt = false;          // true after we build the magic list for current turn
static DWORD s_submenuDebounceTick = 0;        // GetTickCount() when debounce started
static bool s_submenuDebouncing = false;        // true for 300ms after turn start (ignores sub-menu cursor)
static bool s_pendingSubmenuEntry = false;       // v0.10.112: delayed submenu entry after command scroll
static DWORD s_pendingSubmenuTick = 0;           // v0.10.112: GetTickCount() when pending entry was scheduled
static uint8_t s_prevMenuPhaseForTarget = 0xFF;   // v0.12.56: track menuPhase for target-exit detection
static bool s_wasInTargetPhase = false;              // v0.12.61: pure phase-based target tracking
static bool s_gfTargetAnnounced = false;              // v0.12.65: GF target announced this submenu session
static uint8_t s_prevTargetActive = 0xFF;              // v0.12.66: previous 0x01D7689D value for transition detection
static bool s_pendingGFCancel = false;                  // v0.12.72: deferred GF target cancel
static DWORD s_pendingGFCancelTick = 0;                 // v0.12.72: tick when GF cancel was deferred
static char s_pendingGFCancelName[64] = {};             // v0.12.72: name to announce if cancel confirmed
static uint8_t s_prevSubmenuMode = 0xFE;                // v0.12.75: promoted to file scope for exit detection
static bool s_drawPhase14Visited = false;               // v0.12.82: track first phase 14 visit per draw session

// v0.13.49: Definitive submenu-open detection via menuPhase dword.
// The engine repurposes 0x1D768D0: when on the command menu it holds a small
// phase number (0-43). When a submenu is open it holds a FUNCTION POINTER
// (0x004XXXXX range, i.e. >= 0x00400000). Reading as uint32 and checking
// >= 0x00400000 is unambiguous — adjacent bytes at D1-D3 may be non-zero
// during command menu, so a simple > 0xFF check gives false positives.
// Discovered via disassembly: the submenu per-frame handler at 0x4FDD90 calls
// `call dword ptr [0x1d768d0]` every frame, treating the value as a code pointer.
static bool s_submenuOpenByDword = false;  // true when dword check confirms submenu open

// v0.13.52: Deferred turn TTS state.
// Layer 1 (battle_tts_ewm.inl) keeps the ATB cap engaged during damage/action
// windows to prevent new turns from starting. But there's still a frame-level
// race: engine can set activeChar to 0-2 on the exact same frame an enemy
// attack lands (HP change + anim flag raised). The visual menu is already open
// at that point — we can't cleanly un-open it without patching engine state.
// As the next best thing, we defer the "X's turn. Attack." announcement behind
// any in-flight damage TTS so audio order is correct (damage first, turn next).
//
// v0.13.53: Added s_deferredTurnChar so PollDeferredTurnAnnounce can cancel
// the pending TTS if the turn has already advanced past the character we
// deferred for (e.g. damage TTS blocked release long enough for the user to
// finish their whole action). Firing a stale announcement mid-next-turn is
// much worse than silently dropping it.
static char    s_deferredTurnBuf[128] = "";
static bool    s_deferredTurnPending = false;
static DWORD   s_deferredTurnTick = 0;
static uint8_t s_deferredTurnChar = 0xFF;  // v0.13.53: active char at defer time

// ============================================================================
// v0.10.98: GF sub-menu list (junctioned GFs for active character)
// ============================================================================
struct GFEntry { uint8_t gfIdx; char name[32]; };
static GFEntry s_turnGFList[16] = {};          // filtered list of junctioned GFs
static int s_turnGFCount = 0;                  // number of entries in filtered list
static bool s_gfListBuilt = false;             // true after we build the GF list for current turn

// ============================================================================
// v0.14.42: Item sub-menu — read directly from engine's battle items buffer.
// ============================================================================
//
// Architecture (confirmed by FF8_EN.exe disassembly, session 71):
//
// The in-battle items submenu reads from a dedicated buffer at 0x1D28E78,
// stride 5 bytes per entry, 32 entries (range 0x1D28E78..0x1D28F18).
// Each entry: byte 0 = item id, byte 1 = quantity, bytes 2-4 = unused.
// The cursor at 0x01D768EC indexes this buffer DIRECTLY (cursor=N -> entry N).
// Pages are simple 4-per-page: page = cursor/4 + 1, slot = cursor%4 + 1.
// Empty entries (id=0 or qty=0) are still cursor-positioned -> announce as
// "Empty page N item M".
//
// Buffer population (confirmed at 0x0048C6E0 — battle-start populator):
//   1. Clear all 32 entries to {0,0,0,0,0}.
//   2. Walk full inventory at 0x1CFE79C (198 x {id,qty}, stride 2).
//   3. For each entry with id > 0 AND id < 33:
//        pos = arrangement[id - 1]    ; byte at 0x1CFE77C + (id - 1)
//        battle_buffer[pos].id  = id
//        battle_buffer[pos].qty = qty
//
// So 0x1CFE77C is the user's saved Battle arrangement, indexed by item id
// (NOT by position as we previously assumed). It's how the player's
// "Items > Battle" rearrangement persists between battles.
//
// Aaron's reported visual layout (saved arrangement bytes
// 00 03 04 05 06 07 01 08 09 0A 0B 0C 0D 0E 0F 02...) decodes as:
//   id=1  Potion        -> position 0
//   id=7  Phoenix Down  -> position 1
//   id=9  Elixir        -> position 9   <- matches "page 3 slot 2"
//   id=16 Remedy        -> position 2
// ...with empty positions between Remedy and Elixir, exactly as observed.
//
// As item qtys decrement to 0 during battle, the engine zeroes the
// corresponding entry's id (function at 0x00486B40), creating gaps. The
// engine renders gaps as blank rows. We mirror that with "Empty" announces.
static const uint32_t BATTLE_ITEM_BUFFER_ADDR  = 0x1D28E78;  // 32 x 5-byte entries
static const uint32_t BATTLE_ITEM_ARRANGE_ADDR = 0x1CFE77C;  // 32 bytes, arrangement[id-1] = position
static const uint32_t ITEM_INVENTORY_ADDR      = 0x1CFE79C;  // 198 x {id, qty} byte pairs (full inv)
static const int      BATTLE_ITEM_BUFFER_STRIDE = 5;         // bytes per entry
static const int      BATTLE_ITEM_MAX           = 32;         // entry count

// In v0.14.42 we no longer compact battle items into a filtered list. Cursor
// indexes the engine buffer DIRECTLY, so per-cursor reads happen at announce
// time. We retain s_itemListBuilt as a guard for ordering (so the announce
// path doesn't fire before BuildItemList has logged the snapshot), and keep
// s_turnItemList as a one-shot snapshot purely for diagnostic logging.
struct BattleItemEntry { uint8_t id; uint8_t qty; };
static BattleItemEntry s_turnItemList[BATTLE_ITEM_MAX] = {};
static int s_turnItemCount = 0;
static bool s_itemListBuilt = false;

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

// ============================================================================
// v0.12.52: Magic inventory snapshot for Draw validation
// Captures all 3 party members' magic arrays at turn start.
// When "Received" fires, we diff to verify which character gained spells.
// ============================================================================
static const uint32_t DRAW_EXEC_SLOT_ADDR = 0x01D768D4; // engine's executing character party slot
struct MagicSlot { uint8_t id; uint8_t qty; };
static MagicSlot s_magicSnapshotBefore[3][32] = {};  // [partySlot][magicSlotIdx]
static bool s_magicSnapshotValid = false;

// v0.12.52: party slot (0-2) of the most recent validated drawer, or 0xFF.
// Populated by DiffMagicInventories (in battle_tts_menu_lists.inl) and read
// by GetLastDrawerName() in battle_tts.cpp.
static uint8_t s_lastValidatedDrawSlot = 0xFF;

// Turn/command tracking state
static uint8_t s_turnActiveCharId = 0xFF;    // last active_char_id we announced
static uint8_t s_turnCmdCursor = 0xFF;       // last command cursor position
static uint8_t s_turnCharCommands[4] = {};    // command IDs for current turn's 4 slots
