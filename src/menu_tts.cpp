// menu_tts.cpp - In-game menu TTS for FF8 Accessibility Mod
//
// ============================================================================
// CURRENT STATE: v0.07.57 — LZSS decompression of .ff8 save files for block content TTS
// Title Continue (mode 1): slot +0x1FE, block +0x1F0, phase +0x1EE
// In-game Save (mode 6): slot +0x276, block +0x268, phase +0x266
// ============================================================================
//
// Top-level menu cursor: BYTE at pMenuStateA + 0x1E6 (confirmed working)
//   0=Junction, 1=Item, 2=Magic, 3=Status, 4=GF, 5=Ability,
//   6=Switch, 7=Card, 8=Config, 9=Tutorial, 10=Save
//
// v07.06: Title Screen → Continue stays in mode 1. No mode change.
// v07.07: 2048-byte pMenuStateA scan showed only rendering noise.
//   The save/load cursor is NOT in the pMenuStateA region.
//
// v07.08: F12 scans the ff8_win_obj windows array (pWindowsArray).
//   Each window is 0x3C bytes. Key fields:
//     +0x18: win_id, +0x1A: mode1, +0x1C: open_close_transition,
//     +0x24: state, +0x29: first_question, +0x2A: last_question,
//     +0x2B: current_choice_question
//   Dumps all 8 windows every 500ms, looking for active windows
//   with changing current_choice_question during Continue flow.
//   Also scans a 4096-byte region starting 2048 bytes before pMenuStateA
//   to check broader memory for cursor candidates.

#include "ff8_accessibility.h"
#include "menu_tts.h"
#include "field_dialog.h"
#include "ff8_text_decode.h"
#include "field_archive.h"
#include <cstdio>
#include <cstring>
#include <string>

using namespace FF8Addresses;

// ============================================================================
// Cursor offset from pMenuStateA
// ============================================================================
static const int CURSOR_OFFSET = 0x1E6;

// ============================================================================
// v0.08.29: Submenu cursor offsets (discovered via auto-monitor v0.08.28)
// ============================================================================
static const int SUBMENU_LIST_CURSOR_OFFSET  = 0x272;  // item list cursor (phase 1, confirmed v0.08.31 diagnostic)
static const int SUBMENU_PHASE_OFFSET        = 0x230;  // 0=action menu, 1=item/spell list
static const int SUBMENU_ACTION_CURSOR_OFFSET = 0x27F; // action menu cursor (range 0-3)
static const int ITEM_SUBPHASE_OFFSET          = 0x5DF; // v0.08.33: sub-phase within Item (3=item list, 2=action menu overlay)
static const int ITEM_FOCUS_STATE_OFFSET        = 0x22E; // v0.08.60: active focus indicator (3=action menu, 5=items list)

// Item submenu action menu options (phase 0, cursor 0-3)
static const char* ITEM_ACTION_NAMES[] = { "Use", "Rearrange", "Sort", "Battle" };
static const int ITEM_ACTION_COUNT = 4;

// ============================================================================
// Menu item name table (full visual order, 0-based)
// ============================================================================
static const char* MENU_ITEMS[] = {
    "Junction",   // 0
    "Item",       // 1
    "Magic",      // 2
    "Status",     // 3
    "GF",         // 4
    "Ability",    // 5
    "Switch",     // 6
    "Card",       // 7
    "Config",     // 8
    "Tutorial",   // 9
    "Save",       // 10
};
static const int MENU_ITEMS_COUNT = 11;

static const char* GetMenuItemName(uint8_t idx)
{
    if (idx < MENU_ITEMS_COUNT) return MENU_ITEMS[idx];
    return nullptr;
}

// ============================================================================
// v0.08.29: FF8 item name table — indexed by item_id (1-based, 0=empty)
// Source: FF8 kernel.bin item data, cross-referenced with cheat engine hex IDs.
// 198 items total (IDs 1-198). Some names in the 100+ range may need correction.
// ============================================================================
static const char* FF8_ITEM_NAMES[] = {
    nullptr,              //   0 = empty slot
    "Potion",             //   1 (0x01)
    "Potion+",            //   2 (0x02)
    "Hi-Potion",          //   3 (0x03)
    "Hi-Potion+",         //   4 (0x04)
    "X-Potion",           //   5 (0x05)
    "Mega-Potion",        //   6 (0x06)
    "Phoenix Down",       //   7 (0x07)
    "Mega Phoenix",       //   8 (0x08)
    "Elixir",             //   9 (0x09)
    "Megalixir",          //  10 (0x0A)
    "Antidote",           //  11 (0x0B)
    "Soft",               //  12 (0x0C)
    "Eye Drops",          //  13 (0x0D)
    "Echo Screen",        //  14 (0x0E)
    "Holy Water",         //  15 (0x0F)
    "Remedy",             //  16 (0x10)
    "Remedy+",            //  17 (0x11)
    "Hero-Trial",         //  18 (0x12)
    "Hero",               //  19 (0x13)
    "Holy War-Trial",     //  20 (0x14)
    "Holy War",           //  21 (0x15)
    "Shell Stone",        //  22 (0x16)
    "Protect Stone",      //  23 (0x17)
    "Aura Stone",         //  24 (0x18)
    "Death Stone",        //  25 (0x19)
    "Holy Stone",         //  26 (0x1A)
    "Flare Stone",        //  27 (0x1B)
    "Meteor Stone",       //  28 (0x1C)
    "Ultima Stone",       //  29 (0x1D)
    "Gysahl Greens",      //  30 (0x1E)
    "Phoenix Pinion",     //  31 (0x1F)
    "Friendship",         //  32 (0x20)
    "Tent",               //  33 (0x21)
    "Pet House",          //  34 (0x22)
    "Cottage",            //  35 (0x23)
    "G-Potion",           //  36 (0x24)
    "G-Hi-Potion",        //  37 (0x25)
    "G-Mega-Potion",      //  38 (0x26)
    "G-Returner",         //  39 (0x27)
    "Rename Card",        //  40 (0x28)
    "Amnesia Greens",     //  41 (0x29)
    "HP-J Scroll",        //  42 (0x2A)
    "Str-J Scroll",       //  43 (0x2B)
    "Vit-J Scroll",       //  44 (0x2C)
    "Mag-J Scroll",       //  45 (0x2D)
    "Spr-J Scroll",       //  46 (0x2E)
    "Spd-J Scroll",       //  47 (0x2F)
    "Luck-J Scroll",      //  48 (0x30)
    "Aegis Amulet",       //  49 (0x31)
    "Elem Atk",           //  50 (0x32)
    "Elem Guard",         //  51 (0x33)
    "Status Atk",         //  52 (0x34)
    "Status Guard",       //  53 (0x35)
    "Rosetta Stone",      //  54 (0x36)
    "Magic Scroll",       //  55 (0x37)
    "GF Scroll",          //  56 (0x38)
    "Draw Scroll",        //  57 (0x39)
    "Item Scroll",        //  58 (0x3A)
    "Gambler's Spirit",   //  59 (0x3B)
    "Healing Ring",       //  60 (0x3C)
    "Phoenix Spirit",     //  61 (0x3D)
    "Med Kit",            //  62 (0x3E)
    "Bomb Spirit",        //  63 (0x3F)
    "Hungry Cookpot",     //  64 (0x40)
    "Mog's Amulet",       //  65 (0x41)
    "Steel Pipe",         //  66 (0x42)
    "Star Fragment",      //  67 (0x43)
    "Energy Crystal",     //  68 (0x44)
    "Samantha Soul",      //  69 (0x45)
    "Healing Mail",       //  70 (0x46)
    "Silver Mail",        //  71 (0x47)
    "Gold Armor",         //  72 (0x48)
    "Diamond Armor",      //  73 (0x49)
    "Regen Ring",         //  74 (0x4A)
    "Giant's Ring",       //  75 (0x4B)
    "Gaea's Ring",        //  76 (0x4C)
    "Strength Love",      //  77 (0x4D)
    "Power Wrist",        //  78 (0x4E)
    "Hyper Wrist",        //  79 (0x4F)
    "Turtle Shell",       //  80 (0x50)
    "Orihalcon",          //  81 (0x51)
    "Adamantine",         //  82 (0x52)
    "Rune Armlet",        //  83 (0x53)
    "Force Armlet",       //  84 (0x54)
    "Magic Armlet",       //  85 (0x55)
    "Circlet",            //  86 (0x56)
    "Hypno Crown",        //  87 (0x57)
    "Royal Crown",        //  88 (0x58)
    "Jet Engine",         //  89 (0x59)
    "Rocket Engine",      //  90 (0x5A)
    "Moon Curtain",       //  91 (0x5B)
    "Steel Curtain",      //  92 (0x5C)
    "Glow Curtain",       //  93 (0x5D)
    "Accelerator",        //  94 (0x5E)
    "Monk's Code",        //  95 (0x5F)
    "Knight's Code",      //  96 (0x60)
    "Doc's Code",         //  97 (0x61)
    "Hundred Needles",    //  98 (0x62)
    "Three Stars",        //  99 (0x63)
    "Ribbon",             // 100 (0x64)
    "Normal Ammo",        // 101 (0x65)
    "Shotgun Ammo",       // 102 (0x66)
    "Dark Ammo",          // 103 (0x67)
    "Fire Ammo",          // 104 (0x68)
    "Demolition Ammo",    // 105 (0x69)
    "Fast Ammo",          // 106 (0x6A)
    "AP Ammo",            // 107 (0x6B)
    "Pulse Ammo",         // 108 (0x6C)
    "M-Stone Piece",      // 109 (0x6D)
    "Magic Stone",        // 110 (0x6E)
    "Wizard Stone",       // 111 (0x6F)
    "Ochu Tentacle",      // 112 (0x70)
    "Healing Water",      // 113 (0x71)
    "Cockatrice Pinion",  // 114 (0x72)
    "Zombie Powder",      // 115 (0x73)
    "Lightweight",        // 116 (0x74)
    "Sharp Spike",        // 117 (0x75)
    "Screw",              // 118 (0x76)
    "Saw Blade",          // 119 (0x77)
    "Mesmerize Blade",    // 120 (0x78)
    "Vampire Fang",       // 121 (0x79)
    "Fury Fragment",      // 122 (0x7A)
    "Betrayal Sword",     // 123 (0x7B)
    "Sleep Powder",       // 124 (0x7C)
    "Life Ring",          // 125 (0x7D)
    "Dragon Fang",        // 126 (0x7E)
    "Spider Web",         // 127 (0x7F)
    "Coral Fragment",     // 128 (0x80)
    "Curse Spike",        // 129 (0x81)
    "Black Hole",         // 130 (0x82)
    "Water Crystal",      // 131 (0x83)
    "Missile",            // 132 (0x84)
    "Mystery Fluid",      // 133 (0x85)
    "Running Fire",       // 134 (0x86)
    "Inferno Fang",       // 135 (0x87)
    "Malboro Tentacle",   // 136 (0x88)
    "Whisper",            // 137 (0x89)
    "Laser Cannon",       // 138 (0x8A)
    "Barrier",            // 139 (0x8B)
    "Power Generator",    // 140 (0x8C)
    "Dark Matter",        // 141 (0x8D)
    "Bomb Fragment",      // 142 (0x8E)
    "Red Fang",           // 143 (0x8F)
    "Arctic Wind",        // 144 (0x90)
    "North Wind",         // 145 (0x91)
    "Dynamo Stone",       // 146 (0x92)
    "Shear Feather",      // 147 (0x93)
    "Venom Fang",         // 148 (0x94)
    "Steel Orb",          // 149 (0x95)
    "Moon Stone",         // 150 (0x96)
    "Dino Bone",          // 151 (0x97)
    "Windmill",           // 152 (0x98)
    "Dragon Skin",        // 153 (0x99)
    "Fish Fin",           // 154 (0x9A)
    "Dragon Fin",         // 155 (0x9B)
    "Silence Powder",     // 156 (0x9C)
    "Poison Powder",      // 157 (0x9D)
    "Dead Spirit",        // 158 (0x9E)
    "Chef's Knife",       // 159 (0x9F)
    "Cactus Thorn",       // 160 (0xA0)
    "Shaman Stone",       // 161 (0xA1)
    "Fuel",               // 162 (0xA2)
    "Girl Next Door",     // 163 (0xA3)
    "Sorceress' Letter",  // 164 (0xA4)
    "Chocobo's Tag",      // 165 (0xA5)
    "Pet Nametag",        // 166 (0xA6)
    "Solomon Ring",       // 167 (0xA7)
    "Magical Lamp",       // 168 (0xA8)
    "HP Up",              // 169 (0xA9)
    "Str Up",             // 170 (0xAA)
    "Vit Up",             // 171 (0xAB)
    "Mag Up",             // 172 (0xAC)
    "Spr Up",             // 173 (0xAD)
    "Spd Up",             // 174 (0xAE)
    "Luck Up",            // 175 (0xAF)
    "LuvLuv G",           // 176 (0xB0)
    "Weapons Mon 1st",    // 177 (0xB1)
    "Weapons Mon Mar",    // 178 (0xB2)
    "Weapons Mon Apr",    // 179 (0xB3)
    "Weapons Mon May",    // 180 (0xB4)
    "Weapons Mon Jun",    // 181 (0xB5)
    "Weapons Mon Jul",    // 182 (0xB6)
    "Weapons Mon Aug",    // 183 (0xB7)
    "Combat King 001",    // 184 (0xB8)
    "Combat King 002",    // 185 (0xB9)
    "Combat King 003",    // 186 (0xBA)
    "Combat King 004",    // 187 (0xBB)
    "Combat King 005",    // 188 (0xBC)
    "Pet Pals Vol. 1",    // 189 (0xBD)
    "Pet Pals Vol. 2",    // 190 (0xBE)
    "Pet Pals Vol. 3",    // 191 (0xBF)
    "Pet Pals Vol. 4",    // 192 (0xC0)
    "Pet Pals Vol. 5",    // 193 (0xC1)
    "Pet Pals Vol. 6",    // 194 (0xC2)
    "Occult Fan I",       // 195 (0xC3)
    "Occult Fan II",      // 196 (0xC4)
    "Occult Fan III",     // 197 (0xC5)
    "Occult Fan IV",      // 198 (0xC6)
};
static const int FF8_ITEM_COUNT = 199;  // 0-198, index 0 = empty

static const char* GetItemName(uint8_t itemId)
{
    if (itemId > 0 && itemId < FF8_ITEM_COUNT && FF8_ITEM_NAMES[itemId])
        return FF8_ITEM_NAMES[itemId];
    return nullptr;  // caller formats fallback
}

// Item inventory location in savemap (corrected offset, -0x14 from research)
static const int ITEM_INVENTORY_OFFSET = 0x0B40;  // savemap + 0x0B40, 198 slots x 2 bytes

// ============================================================================
// State tracking
// ============================================================================
static bool     s_initialized = false;
static bool     s_wasMenuMode = false;
static uint8_t  s_prevCursor = 0xFF;

// Global mode tracking
static uint16_t s_prevGameMode = 0xFFFF;

// v0.08.60: Item submenu state tracking
// +0x22E is the active focus indicator: 3=action menu, 5=items list.
// Discovered via round-trip diagnostic (v0.08.59). Reliably toggles on
// Cancel (items→action) and Confirm (action→items) transitions.
static bool     s_itemSubmenuActive = false;
static uint8_t  s_prevItemCursor = 0xFF;           // tracks +0x272 item list cursor
static uint8_t  s_prevActionCursor = 0xFF;         // tracks +0x27F action cursor
static uint8_t  s_prevFocusState = 0xFF;           // tracks +0x22E (3=action, 5=items)
static uint8_t  s_pendingActionCursor = 0xFF;       // debounce: value waiting to be announced
static DWORD    s_pendingActionTime = 0;            // GetTickCount when pending was set (0=none)

// F12 diagnostic: track menu_draw_text / get_character_width call counts
static bool     s_diagActive = false;
static DWORD    s_diagLastLogTime = 0;
static int      s_diagScanCount = 0;
static const int DIAG_SCAN_MAX = 60;  // 60 x 500ms = 30 seconds
static LONG     s_diagPrevMDT = 0;   // previous menu_draw_text count
static LONG     s_diagPrevGCW = 0;   // previous get_character_width count

// ============================================================================
// Save screen cursor detection (v07.12)
// ============================================================================
// The save/load screen (Title→Continue, or in-game Save) renders text via
// get_character_width. We poll the GCW buffer, decode it with DecodeMenuText,
// and parse for cursor position from the rendered text patterns.
//
// Text patterns observed:
//   "GAME FOLDER in use: Slot N" — N is the selected slot (1 or 2)
//   "Checking GAME FOLDER" — loading phase
//   "No save data" — empty slot
//   "Slot 1" / "Slot 2" + "FINAL FANTASY" — slot list screen
// ============================================================================

static bool     s_saveScreenActive = false;
static DWORD    s_saveLastPollTime = 0;
static int      s_saveCursorSlot = -1;     // -1 = unknown, 1 or 2
static std::string s_saveScreenPhase;      // "checking", "empty", "list", ""
static int      s_saveGcwZeroCount = 0;    // consecutive polls with no GCW data
static const int SAVE_POLL_INTERVAL = 300; // ms between polls
static const int SAVE_GCW_EXIT_THRESHOLD = 6; // ~1.8s of no data = left save screen

// v07.14 FINDING: GCW text is IDENTICAL regardless of cursor position.
// The cursor is a graphical element (hand icon), not encoded in text.
// v07.15 FINDING: 4KB memory scan around pMenuStateA found no cursor byte.
// Only noise (tick counter, rendering toggles). Cursor is elsewhere in memory.
//
// v07.16 APPROACH: Track arrow key presses to infer cursor position.
// The save slot screen has only 2 items (Slot 1, Slot 2) that wrap.
// We detect up/down presses via GetAsyncKeyState and toggle our own counter.

// ============================================================================
// v0.07.32: Save screen cursor — CONFIRMED at pMenuStateA + 0x1FE
// ============================================================================
// Discovered via wide memory scan (v07.25-29) + state region poll (v07.31).
// Offset +0x1FE from pMenuStateA holds the slot selection cursor:
//   0 = Slot 1, 1 = Slot 2
// The save screen controller function is at 0x004CB850 (menu_callbacks[9/10]).
// Absolute address: 0x01D76A9A + 0x1FE = 0x01D76C98
//
// Also at +0x1FD: rendering tick that toggles 0<->16 (ignore).
// ============================================================================

static const int SAVE_SLOT_CURSOR_OFFSET_MODE1 = 0x1FE;  // Title->Continue path (mode 1)
static const int SAVE_SLOT_CURSOR_OFFSET_MODE6 = 0x276;  // In-game Save path (mode 6)
static const int SAVE_SUBSYSTEM_ACTIVE_OFFSET  = 0x1E8;  // ==6 when save screen is open in mode 6
static const int SAVE_PHASE_OFFSET_MODE6         = 0x266;  // 1=slot list, 2+=block selection/saving (mode 6)
static const int SAVE_PHASE_OFFSET_MODE1         = 0x1EE;  // 1=slot list, 2+=block selection/saving (mode 1)
static uint8_t   s_prevSaveSlotCursor = 0xFF;
static bool      s_saveSubsystemWasActive = false; // track transitions for re-announce
static DWORD     s_saveSubsystemExitTime = 0;       // when subsystem went inactive (grace period)
static uint8_t   s_prevSavePhase = 0;               // track +0x266 for block->slot transition

// ============================================================================
// v0.07.48: Block cursor — CONFIRMED at +0x1F0 (mode 1) / +0x268 (mode 6)
// ============================================================================
// Values: 0-14 = block index (15 blocks per slot), 255 = not in block selection
static const int BLOCK_CURSOR_OFFSET_MODE1 = 0x1F0;
static const int BLOCK_CURSOR_OFFSET_MODE6 = 0x268;
static uint8_t   s_prevBlockCursor = 0xFF;

// ============================================================================
// Save data cache — populated by sm_pc_read hook (v0.07.53)
// ============================================================================
struct CachedSaveHeader {
    bool     valid;              // true if block has valid save data
    uint16_t location_id;
    uint32_t gil;
    uint32_t played_time_secs;
    uint8_t  char1_lvl;
    uint8_t  char1_portrait;
    uint8_t  char2_portrait;
    uint8_t  char3_portrait;
    uint8_t  nameRaw[12];        // menu-encoded name
    char     nameDecoded[32];    // UTF-8 decoded name
    char     partyStr[64];       // "Squall, Quistis, Zell" or "Squall" etc.
    char     locationStr[64];    // SETPLACE display name, or "Location NNN"
    uint32_t curr_disk;
    int      slot;               // 1 or 2
    int      block;              // 1-30 (file number)
};

static CachedSaveHeader s_saveCache[2][30] = {};  // [slot 0-1][block 0-29]
static int s_lastCachedSlot = 0;  // which slot was most recently loaded

// Parse "slotN_saveM" from a path to extract slot (1-2) and block (1-30)
static bool ParseSaveFilename(const char* filename, int* outSlot, int* outBlock)
{
    if (!filename) return false;
    // Find "slot" anywhere in the path
    const char* p = filename;
    while (*p) {
        if (p[0]=='s' && p[1]=='l' && p[2]=='o' && p[3]=='t' &&
            p[4]>='1' && p[4]<='2' && p[5]=='_' &&
            p[6]=='s' && p[7]=='a' && p[8]=='v' && p[9]=='e') {
            *outSlot = p[4] - '0';
            // Parse number after "save"
            const char* n = p + 10;
            int block = 0;
            while (*n >= '0' && *n <= '9') { block = block*10 + (*n-'0'); n++; }
            if (block >= 1 && block <= 30) {
                *outBlock = block;
                return true;
            }
        }
        p++;
    }
    return false;
}

// Decode menu-encoded name to UTF-8 C string (safe for SEH context)
static void DecodeNameToBuffer(const uint8_t* raw, int rawLen, char* out, int outSize)
{
    int pos = 0;
    for (int i = 0; i < rawLen && pos < outSize - 1; i++) {
        uint8_t c = raw[i];
        if (c == 0x00) { out[pos++] = ' '; }           // space
        else if (c >= 0x01 && c <= 0x0A) { out[pos++] = '0' + (c - 0x01); } // 0-9
        else if (c >= 0x25 && c <= 0x3E) { out[pos++] = 'A' + (c - 0x25); } // A-Z
        else if (c >= 0x3F && c <= 0x58) { out[pos++] = 'a' + (c - 0x3F); } // a-z
        else break;  // null terminator or unknown
    }
    // Trim trailing spaces
    while (pos > 0 && out[pos-1] == ' ') pos--;
    out[pos] = '\0';
}

// Portrait model ID to character name mapping.
// These are the party member model IDs stored in savemap_ff8_header.
// 0xFF = empty slot (no character in that position).
static const char* GetCharacterNameByPortrait(uint8_t portraitId)
{
    switch (portraitId) {
        case 0:  return "Squall";
        case 1:  return "Zell";
        case 2:  return "Irvine";
        case 3:  return "Quistis";
        case 4:  return "Rinoa";
        case 5:  return "Selphie";
        case 6:  return "Seifer";
        case 7:  return "Edea";
        case 8:  return "Laguna";
        case 9:  return "Kiros";
        case 10: return "Ward";
        case 0xFF: return nullptr;  // empty slot
        default: return nullptr;
    }
}

// ============================================================================
// SETPLACE location name table (v0.07.63)
// The save preview header at offset 0x0004 stores a SETPLACE location_id
// (range 0-250), NOT a field ID. This indexes directly into ~251 location
// name strings. Table sourced from ff8-speedruns/ff8-memory locationId.md.
// IDs 29, 34, 169, 247 are unused. 0xFFFF = "???".
// ============================================================================
static const char* SETPLACE_NAMES[] = {
    "Unknown",                            // 0
    "Balamb - Alcauld Plains",             // 1
    "Balamb - Gaulg Mountains",            // 2
    "Balamb - Rinaul Coast",               // 3
    "Balamb - Raha Cape",                  // 4
    "Timber - Roshfall Forest",            // 5
    "Timber - Mandy Beach",                // 6
    "Timber - Obel Lake",                  // 7
    "Timber - Lanker Plains",              // 8
    "Timber - Nanchucket Island",          // 9
    "Timber - Yaulny Canyon",              // 10
    "Dollet - Hasberry Plains",            // 11
    "Dollet - Holy Glory Cape",            // 12
    "Dollet - Long Horn Island",           // 13
    "Dollet - Malgo Peninsula",            // 14
    "Galbadia - Monterosa Plateau",        // 15
    "Galbadia - Lallapalooza Canyon",      // 16
    "Timber - Shenand Hill",               // 17
    "Galbadia - Gotland Peninsula",        // 18
    "Island Closest to Hell",              // 19
    "Great Plains of Galbadia",            // 20
    "Galbadia - Wilburn Hill",             // 21
    "Galbadia - Rem Archipelago",          // 22
    "Galbadia - Dingo Desert",             // 23
    "Winhill - Winhill Bluffs",            // 24
    "Winhill - Humphrey Archipelago",      // 25
    "Trabia - Winter Island",              // 26
    "Trabia - Sorbald Snowfield",          // 27
    "Trabia - Eldbeak Peninsula",          // 28
    nullptr,                               // 29 (unused)
    "Trabia - Hawkwind Plains",            // 30
    "Trabia - Albatross Archipelago",      // 31
    "Trabia - Bika Snowfield",             // 32
    "Trabia - Thor Peninsula",             // 33
    nullptr,                               // 34 (unused)
    "Trabia - Heath Peninsula",            // 35
    "Trabia - Trabia Crater",              // 36
    "Trabia - Vienne Mountains",           // 37
    "Esthar - Mordred Plains",             // 38
    "Esthar - Nortes Mountains",           // 39
    "Esthar - Fulcura Archipelago",        // 40
    "Esthar - Grandidi Forest",            // 41
    "Esthar - Millefeuille Archipelago",   // 42
    "Great Plains of Esthar",              // 43
    "Esthar City",                         // 44
    "Esthar - Great Salt Lake",            // 45
    "Esthar - West Coast",                 // 46
    "Esthar - Sollet Mountains",           // 47
    "Esthar - Abadan Plains",              // 48
    "Esthar - Minde Island",               // 49
    "Esthar - Kashkabald Desert",          // 50
    "Island Closest to Heaven",            // 51
    "Esthar - Talle Mountains",            // 52
    "Esthar - Shalmal Peninsula",          // 53
    "Centra - Lolestern Plains",           // 54
    "Centra - Almaj Mountains",            // 55
    "Centra - Lenown Plains",              // 56
    "Centra - Cape of Good Hope",          // 57
    "Centra - Yorn Mountains",             // 58
    "Centra - Cactuar Island",             // 59
    "Centra - Serengetti Plains",          // 60
    "Centra - Nectar Peninsula",           // 61
    "Centra - Centra Crater",              // 62
    "Centra - Poccarahi Island",           // 63
    "B-Garden - Library",                  // 64
    "B-Garden - Front Gate",               // 65
    "B-Garden - Classroom",                // 66
    "B-Garden - Cafeteria",                // 67
    "B-Garden - MD Level",                 // 68
    "B-Garden - 2F Hallway",               // 69
    "B-Garden - Hall",                     // 70
    "B-Garden - Infirmary",                // 71
    "B-Garden - Dormitory Double",         // 72
    "B-Garden - Dormitory Single",         // 73
    "B-Garden - Headmaster's Office",      // 74
    "B-Garden - Parking Lot",              // 75
    "B-Garden - Ballroom",                 // 76
    "B-Garden - Quad",                     // 77
    "B-Garden - Training Center",          // 78
    "B-Garden - Secret Area",              // 79
    "B-Garden - Hallway",                  // 80
    "B-Garden - Master Room",              // 81
    "B-Garden - Deck",                     // 82
    "Balamb - The Dincht's",               // 83
    "Balamb Hotel",                        // 84
    "Balamb - Town Square",                // 85
    "Balamb - Station Yard",               // 86
    "Balamb Harbor",                       // 87
    "Balamb - Residence",                  // 88
    "Train",                               // 89
    "Car",                                 // 90
    "Inside Ship",                         // 91
    "Fire Cavern",                         // 92
    "Dollet - Town Square",                // 93
    "Dollet - Lapin Beach",                // 94
    "Dollet Harbor",                       // 95
    "Dollet Pub",                          // 96
    "Dollet Hotel",                        // 97
    "Dollet - Residence",                  // 98
    "Dollet - Comm Tower",                 // 99
    "Dollet - Mountain Hideout",           // 100
    "Timber - City Square",                // 101
    "Timber TV Station",                   // 102
    "Timber - Forest Owls' Base",          // 103
    "Timber Pub",                          // 104
    "Timber Hotel",                        // 105
    "Timber - Train",                      // 106
    "Timber - Residence",                  // 107
    "Timber - TV Screen",                  // 108
    "Timber - Editorial Department",       // 109
    "Timber Forest",                       // 110
    "G-Garden - Front Gate",               // 111
    "G-Garden - Station",                  // 112
    "G-Garden - Hall",                     // 113
    "G-Garden - Hallway",                  // 114
    "G-Garden - Reception Room",           // 115
    "G-Garden - Classroom",                // 116
    "G-Garden - Clubroom",                 // 117
    "G-Garden - Dormitory",                // 118
    "G-Garden - Elevator Hall",            // 119
    "G-Garden - Master Room",              // 120
    "G-Garden - Auditorium",               // 121
    "G-Garden - Athletic Track",           // 122
    "G-Garden - Stand",                    // 123
    "G-Garden - Back Entrance",            // 124
    "G-Garden - Gymnasium",                // 125
    "Deling - Presidential Residence",     // 126
    "Deling City - Caraway's Mansion",     // 127
    "Deling City - Station Yard",          // 128
    "Deling City - City Square",           // 129
    "Deling City - Hotel",                 // 130
    "Deling City - Club",                  // 131
    "Deling City - Gateway",               // 132
    "Deling City - Parade",                // 133
    "Deling City - Sewer",                 // 134
    "Galbadia D-District Prison",          // 135
    "Desert",                              // 136
    "Galbadia Missile Base",               // 137
    "Winhill Village",                     // 138
    "Winhill Pub",                         // 139
    "Winhill - Vacant House",              // 140
    "Winhill - Mansion",                   // 141
    "Winhill - Residence",                 // 142
    "Winhill - Hotel",                     // 143
    "Winhill - Car",                       // 144
    "Tomb of the Unknown King",            // 145
    "Fishermans Horizon",                  // 146
    "FH - Residential Area",               // 147
    "FH - Sun Panel",                      // 148
    "FH - Mayor's Residence",              // 149
    "FH - Factory",                        // 150
    "FH - Festival Grounds",               // 151
    "FH - Hotel",                          // 152
    "FH - Residence",                      // 153
    "FH - Station Yard",                   // 154
    "FH - Horizon Bridge",                 // 155
    "FH - Seaside Station",                // 156
    "FH - Great Salt Lake",                // 157
    "FH - Mystery Building",               // 158
    "Esthar - City",                       // 159
    "Esthar - Odine's Laboratory",         // 160
    "Esthar - Airstation",                 // 161
    "Lunatic Pandora Approaching",         // 162
    "Esthar - Presidential Palace",        // 163
    "Presidential Palace - Hall",          // 164
    "Presidential Palace - Hallway",       // 165
    "Presidential Palace - Office",        // 166
    "Dr. Odine's Laboratory - Lobby",      // 167
    "Dr. Odine's Laboratory - Lab",        // 168
    nullptr,                               // 169 (deleted/unused)
    "Lunar Gate",                          // 170
    "Lunar Gate - Concourse",              // 171
    "Lunar Gate - Deep Freeze",            // 172
    "Esthar Sorceress Memorial",           // 173
    "Sorceress Memorial - Entrance",       // 174
    "Sorceress Memorial - Pod",            // 175
    "Sorceress Memorial - Ctrl Room",      // 176
    "Tears' Point",                        // 177
    "Lunatic Pandora Laboratory",          // 178
    "Emergency Landing Zone",              // 179
    "Spaceship Landing Zone",              // 180
    "Lunatic Pandora",                     // 181
    "Centra - Excavation Site",            // 182
    "Edea's House",                        // 183
    "Edea's House - Playroom",             // 184
    "Edea's House - Bedroom",              // 185
    "Edea's House - Backyard",             // 186
    "Edea's House - Oceanside",            // 187
    "Edea's House - Flower Field",         // 188
    "Centra Ruins",                        // 189
    "Trabia Garden - Front Gate",          // 190
    "T-Garden - Cemetery",                 // 191
    "T-Garden - Garage",                   // 192
    "T-Garden - Festival Stage",           // 193
    "T-Garden - Classroom",                // 194
    "T-Garden - Athletic Ground",          // 195
    "Mystery Dome",                        // 196
    "Shumi Village - Desert Village",      // 197
    "Shumi Village - Elevator",            // 198
    "Shumi Village - Village",             // 199
    "Shumi Village - Residence",           // 200
    "Shumi Village - Residence",           // 201
    "Shumi Village - Residence",           // 202
    "Shumi Village - Hotel",               // 203
    "Trabia Canyon",                       // 204
    "White SeeD Ship",                     // 205
    "White SeeD Ship",                     // 206
    "White SeeD Ship - Cabin",             // 207
    "Ragnarok - Cockpit",                  // 208
    "Ragnarok - Passenger Seat",           // 209
    "Ragnarok - Aisle",                    // 210
    "Ragnarok - Hangar",                   // 211
    "Ragnarok - Entrance",                 // 212
    "Ragnarok - Air Room",                 // 213
    "Ragnarok - Space Hatch",              // 214
    "Deep Sea Research Center",            // 215
    "Deep Sea Research Center - Lobby",    // 216
    "Deep Sea Research Center - Levels",   // 217
    "Deep Sea Deposit",                    // 218
    "Lunar Base - Control Room",           // 219
    "Lunar Base - Medical Room",           // 220
    "Lunar Base - Pod",                    // 221
    "Lunar Base - Dock",                   // 222
    "Lunar Base - Passageway",             // 223
    "Lunar Base - Locker",                 // 224
    "Lunar Base - Residential Zone",       // 225
    "Outer Space",                         // 226
    "Chocobo Forest",                      // 227
    "Wilderness",                          // 228
    "Ultimecia Castle - Hall",             // 229
    "Ultimecia Castle - Grand Hall",       // 230
    "Ultimecia Castle - Terrace",          // 231
    "Ultimecia Castle - Wine Cellar",      // 232
    "Ultimecia Castle - Passageway",       // 233
    "Ultimecia Castle - Elevator Hall",    // 234
    "Ultimecia Castle - Stairway Hall",    // 235
    "Ultimecia Castle - Treasure Rm",      // 236
    "Ultimecia Castle - Storage Room",     // 237
    "Ultimecia Castle - Art Gallery",      // 238
    "Ultimecia Castle - Flood Gate",       // 239
    "Ultimecia Castle - Armory",           // 240
    "Ultimecia Castle - Prison Cell",      // 241
    "Ultimecia Castle - Waterway",         // 242
    "Ultimecia Castle - Courtyard",        // 243
    "Ultimecia Castle - Chapel",           // 244
    "Ultimecia Castle - Clock Tower",      // 245
    "Ultimecia Castle - Master Room",      // 246
    nullptr,                               // 247 (unused)
    "Ultimecia Castle",                    // 248
    "Commencement Room",                   // 249
    "Queen of Cards",                      // 250
};
static const int SETPLACE_NAMES_COUNT = 251;

// Look up SETPLACE display name by location_id from save preview header.
// Returns human-readable location name, or fallback for unknown IDs.
static const char* GetLocationNameById(uint16_t locationId)
{
    if (locationId == 0xFFFF) return "???";
    if (locationId < SETPLACE_NAMES_COUNT) {
        const char* name = SETPLACE_NAMES[locationId];
        if (name) return name;
    }
    // Out of range or nullptr entry — return nullptr, caller will format fallback
    return nullptr;
}

// ============================================================================
// LZSS decompression for .ff8 save files (v0.07.57)
// Standard FF7/FF8 LZSS: N=4096, F=18, THRESHOLD=2, ring init=0, start=0xFEE
// Same algorithm as field_archive.cpp DecompressLZSS, duplicated here to
// avoid cross-module coupling (field_archive version is static).
// ============================================================================
static int LzssDecompressSave(const uint8_t* src, int srcLen,
                              uint8_t* dst, int dstCap)
{
    uint8_t ring[4096];
    memset(ring, 0, sizeof(ring));
    int r = 0xFEE;  // N - F = 4096 - 18
    int si = 0, di = 0;
    unsigned int flags = 0;

    while (si < srcLen) {
        flags >>= 1;
        if ((flags & 0x100) == 0) {
            if (si >= srcLen) break;
            flags = (unsigned int)src[si++] | 0xFF00;
        }
        if (flags & 1) {
            // Literal byte
            if (si >= srcLen || di >= dstCap) break;
            uint8_t c = src[si++];
            dst[di++] = c;
            ring[r] = c;
            r = (r + 1) & 0xFFF;
        } else {
            // Back-reference (2 bytes)
            if (si + 1 >= srcLen) break;
            int b0 = src[si++];
            int b1 = src[si++];
            int offset = b0 | ((b1 & 0xF0) << 4);
            int length = (b1 & 0x0F) + 3;  // THRESHOLD+1=3
            for (int k = 0; k < length; k++) {
                if (di >= dstCap) break;
                uint8_t c = ring[(offset + k) & 0xFFF];
                dst[di++] = c;
                ring[r] = c;
                r = (r + 1) & 0xFFF;
            }
        }
    }
    return di;
}

// Called from FmvSkip's CreateFileA hook when a .ff8 save file is opened.
// v0.07.57: data is the entire raw .ff8 file. We LZSS-decompress it first,
// then parse the clean savemap_ff8_header from the decompressed output.
void MenuTTS_CacheSaveHeader(const char* filename, const uint8_t* data, int dataLen)
{
    if (!filename || !data || dataLen < 8) return;
    
    int slot = 0, block = 0;
    if (!ParseSaveFilename(filename, &slot, &block)) {
        Log::Write("[SaveCache] Could not parse slot/block from: %s", filename);
        return;
    }
    
    int si = slot - 1;
    int bi = block - 1;
    if (si < 0 || si >= 2 || bi < 0 || bi >= 30) return;
    
    // .ff8 format: first 4 bytes = uint32 LE compressed payload size
    uint32_t compressedSize = *(uint32_t*)data;
    if ((int)compressedSize + 4 > dataLen || compressedSize == 0) {
        Log::Write("[SaveCache] slot%d_save%02d: bad compressed size %u (file %d bytes)",
                   slot, block, compressedSize, dataLen);
        s_saveCache[si][bi].valid = false;
        return;
    }
    
    Log::Write("[SaveCache] slot%d_save%02d: file=%d bytes, compressed=%u",
               slot, block, dataLen, compressedSize);
    
    // Decompress into 16K buffer (PC save can be ~8000+ bytes with wrapper data)
    uint8_t decompBuf[16384] = {};
    int decompLen = LzssDecompressSave(data + 4, (int)compressedSize,
                                       decompBuf, sizeof(decompBuf));
    
    Log::Write("[SaveCache] slot%d_save%02d: decompressed %d bytes",
               slot, block, decompLen);
    
    if (decompLen < 0x50) {
        Log::Write("[SaveCache] slot%d_save%02d: decompression too short (%d bytes)",
                   slot, block, decompLen);
        s_saveCache[si][bi].valid = false;
        return;
    }
    
    // Search for ALL 0x08FF occurrences in decompressed data for diagnostic purposes.
    // The PC save format has a 384-byte header before the actual savemap.
    // Log every hit so we can verify we're using the correct one.
    int hitCount = 0;
    int smOffset = -1;
    for (int i = 0; i + 1 < decompLen; i++) {
        if (decompBuf[i] == 0xFF && decompBuf[i+1] == 0x08) {
            int candidate = i - 2;
            uint8_t lvl = (candidate >= 0 && candidate + 4 + 0x10 < decompLen)
                          ? decompBuf[candidate + 4 + 0x10] : 0;
            Log::Write("[SaveCache]   0x08FF hit #%d at decomp +%d (savemap +%d), lvl_byte=%u",
                       hitCount, i, candidate, (unsigned)lvl);
            // Dump 32 bytes starting from candidate+4 (the header area)
            if (candidate >= 0 && candidate + 4 + 32 <= decompLen) {
                char hx[128] = {};
                int hp = 0;
                for (int j = 0; j < 32 && hp < 120; j++)
                    hp += sprintf(hx + hp, "%02X ", decompBuf[candidate + 4 + j]);
                Log::Write("[SaveCache]     hdr[0..31]: %s", hx);
            }
            hitCount++;
            if (smOffset < 0 && candidate >= 0 && candidate + 4 + 0x4C <= decompLen
                && lvl >= 1 && lvl <= 100) {
                smOffset = candidate;  // take first valid candidate
            }
        }
    }
    Log::Write("[SaveCache] slot%d_save%02d: %d total 0x08FF hits, using savemap at +%d",
               slot, block, hitCount, smOffset);
    
    if (smOffset < 0) {
        char hexOut[128] = {};
        int hp = 0;
        int dumpLen = (decompLen < 32) ? decompLen : 32;
        for (int i = 0; i < dumpLen && hp < 120; i++)
            hp += sprintf(hexOut + hp, "%02X ", decompBuf[i]);
        Log::Write("[SaveCache] slot%d_save%02d: no valid 0x08FF found. First 32: %s",
                   slot, block, hexOut);
        s_saveCache[si][bi].valid = false;
        return;
    }
    
    // Parse savemap_ff8_header at smOffset + 4 (skip checksum + fixed_value)
    const uint8_t* hdr = decompBuf + smOffset + 4;
    CachedSaveHeader* h = &s_saveCache[si][bi];
    h->valid            = true;
    h->location_id      = *(uint16_t*)(hdr + 0x00);
    h->gil              = *(uint32_t*)(hdr + 0x08);
    h->played_time_secs = *(uint32_t*)(hdr + 0x0C);
    h->char1_lvl        = *(hdr + 0x10);
    h->char1_portrait   = *(hdr + 0x11);
    h->char2_portrait   = *(hdr + 0x12);
    h->char3_portrait   = *(hdr + 0x13);
    // .ff8 save file stores names with +0x20 offset from sysfnt encoding.
    // Subtract 0x20 from each non-zero byte to get standard menu encoding.
    for (int i = 0; i < 12; i++) {
        uint8_t b = hdr[0x14 + i];
        h->nameRaw[i] = (b >= 0x20) ? (b - 0x20) : b;
    }
    h->curr_disk        = *(uint32_t*)(hdr + 0x44);
    h->slot  = slot;
    h->block = block;
    DecodeNameToBuffer(h->nameRaw, 12, h->nameDecoded, sizeof(h->nameDecoded));
    s_lastCachedSlot = slot;
    
    // Build party member string from portrait IDs.
    // Collect non-empty party members into a comma-separated list.
    h->partyStr[0] = '\0';
    {
        int ppos = 0;
        uint8_t portraits[3] = { h->char1_portrait, h->char2_portrait, h->char3_portrait };
        for (int i = 0; i < 3; i++) {
            const char* name = GetCharacterNameByPortrait(portraits[i]);
            if (name) {
                if (ppos > 0 && ppos < 60) ppos += sprintf(h->partyStr + ppos, ", ");
                if (ppos < 60) ppos += sprintf(h->partyStr + ppos, "%s", name);
            }
        }
        Log::Write("[SaveCache] slot%d_save%02d: portraits=[%u,%u,%u] party=\"%s\"",
                   slot, block, portraits[0], portraits[1], portraits[2], h->partyStr);
    }
    
    // Build location string from SETPLACE location_id (v0.07.63).
    // The uint16 at save offset 0x0004 is a SETPLACE index (0-250), NOT a field ID.
    h->locationStr[0] = '\0';
    const char* locName = GetLocationNameById(h->location_id);
    if (locName) {
        snprintf(h->locationStr, sizeof(h->locationStr), "%s", locName);
    } else {
        snprintf(h->locationStr, sizeof(h->locationStr), "Location %u", h->location_id);
    }
    
    int hrs = h->played_time_secs / 3600;
    int mins = (h->played_time_secs % 3600) / 60;
    Log::Write("[SaveCache] slot%d_save%02d: \"%s\" lvl=%u loc=%u (%s) gil=%u time=%dh%02dm party=[%s]",
               slot, block, h->nameDecoded, h->char1_lvl,
               h->location_id, h->locationStr, h->gil, hrs, mins, h->partyStr);
}

// Check if the save subsystem is active in mode 6
// pMenuStateA + 0x1E8 == 6 when the slot selection screen is showing
static bool IsSaveSubsystemActive()
{
    if (!pMenuStateA) return false;
    __try {
        uint8_t val = *((uint8_t*)pMenuStateA + SAVE_SUBSYSTEM_ACTIVE_OFFSET);
        return (val == 6);
    } __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}

// Announce the current save slot
static void AnnounceSaveSlot()
{
    const char* slotName = (s_saveCursorSlot == 1) ? "Slot 1" : "Slot 2";
    ScreenReader::Speak(slotName, true);
    Log::Write("[MenuTTS] Save slot: %s", slotName);
}

// SEH-protected helper to read the save slot cursor byte.
// Separated from PollSaveScreen because __try can't coexist with
// C++ objects (std::string) that require unwinding (C2712).
static DWORD s_saveSlotLastPoll = 0;

static void PollSaveSlotCursor(int offset, bool announceFirst)
{
    // Throttle to every 100ms to avoid performance impact
    DWORD now = GetTickCount();
    if (now - s_saveSlotLastPoll < 100) return;
    s_saveSlotLastPoll = now;
    
    __try {
        uint8_t slotCursor = *((uint8_t*)pMenuStateA + offset);
        
        if (slotCursor > 1) {
            // Cursor out of slot range (in block selection or other sub-screen)
            // Reset so we re-announce when returning to slot list
            s_prevSaveSlotCursor = 0xFF;
            return;
        }
        
        if (slotCursor != s_prevSaveSlotCursor) {
            if (s_prevSaveSlotCursor != 0xFF || announceFirst) {
                s_saveCursorSlot = slotCursor + 1;  // 0->Slot 1, 1->Slot 2
                AnnounceSaveSlot();
            }
            s_prevSaveSlotCursor = slotCursor;
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
}



// SEH-protected helper: check phase offset for block->slot transition
// Returns true if transition detected (caller should reset s_prevSaveSlotCursor)
static bool CheckSavePhaseTransition(int phaseOffset)
{
    __try {
        uint8_t phase = *((uint8_t*)pMenuStateA + phaseOffset);
        if (s_prevSavePhase >= 2 && phase == 1) {
            // Returned from block selection to slot list
            Log::Write("[MenuTTS] Block->slot transition (+0x%03X: %u->%u), reset for announce",
                       phaseOffset, (unsigned)s_prevSavePhase, (unsigned)phase);
            s_prevSavePhase = phase;
            return true;
        }
        s_prevSavePhase = phase;
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
    return false;
}

// ============================================================================
// F11 DIAGNOSTIC: Scan process memory for savemap_ff8 headers
// Searches all readable memory for the 0x08FF fixed_value fingerprint,
// then validates surrounding fields to confirm savemap_ff8 structs.
// This finds where the game stores loaded save data during the save screen.
// ============================================================================
// Struct to hold scan results (no C++ objects, safe for SEH)
struct SaveScanHit {
    uint32_t addr;       // absolute address of savemap base
    int32_t  relOffset;  // offset from pMenuStateA
    uint16_t locId;
    uint32_t gil;
    uint32_t playTime;
    uint8_t  lvl;
    uint32_t disk;
    uint8_t  nameRaw[12];
    uint8_t  hdrHex[48];
};

// SEH-safe inner scan — no C++ objects allowed
static int ScanForSaveHeaders_Inner(SaveScanHit* hits, int maxHits)
{
    SYSTEM_INFO si;
    GetSystemInfo(&si);
    
    uint8_t* addr = (uint8_t*)si.lpMinimumApplicationAddress;
    uint8_t* maxAddr = (uint8_t*)si.lpMaximumApplicationAddress;
    int foundCount = 0;
    
    while (addr < maxAddr && foundCount < maxHits) {
        MEMORY_BASIC_INFORMATION mbi;
        if (VirtualQuery(addr, &mbi, sizeof(mbi)) == 0) break;
        
        if (mbi.State == MEM_COMMIT &&
            (mbi.Protect == PAGE_READWRITE || mbi.Protect == PAGE_READONLY ||
             mbi.Protect == PAGE_EXECUTE_READ || mbi.Protect == PAGE_EXECUTE_READWRITE)) {
            
            uint8_t* base = (uint8_t*)mbi.BaseAddress;
            SIZE_T size = mbi.RegionSize;
            
            __try {
                for (SIZE_T off = 0; off + 0x4C < size; off += 2) {
                    if (base[off] == 0xFF && base[off+1] == 0x08) {
                        uint8_t* smBase = base + off - 2;
                        uint8_t* hdr = smBase + 4;
                        
                        uint16_t locId = *(uint16_t*)(hdr + 0x00);
                        uint32_t gil = *(uint32_t*)(hdr + 0x08);
                        uint32_t playTime = *(uint32_t*)(hdr + 0x0C);
                        uint8_t  lvl = *(hdr + 0x10);
                        uint8_t  nameFirst = *(hdr + 0x14);
                        uint32_t disk = *(uint32_t*)(hdr + 0x44);
                        
                        // Require at least one non-zero gameplay field
                        // to filter out zeroed memory regions
                        bool hasData = (lvl > 0 || gil > 0 || playTime > 0);
                        bool valid = hasData &&
                                     (locId < 1000) &&
                                     (lvl <= 100) &&
                                     (playTime < 360000) &&
                                     (disk <= 4) &&
                                     (nameFirst <= 0x60);
                        
                        if (valid) {
                            SaveScanHit* h = &hits[foundCount];
                            h->addr = (uint32_t)(uintptr_t)smBase;
                            h->relOffset = (int32_t)((uintptr_t)smBase - (uintptr_t)pMenuStateA);
                            h->locId = locId;
                            h->gil = gil;
                            h->playTime = playTime;
                            h->lvl = lvl;
                            h->disk = disk;
                            for (int i = 0; i < 12; i++) h->nameRaw[i] = hdr[0x14 + i];
                            for (int i = 0; i < 48; i++) h->hdrHex[i] = hdr[i];
                            foundCount++;
                        }
                    }
                    if (foundCount >= maxHits) break;
                }
            } __except(EXCEPTION_EXECUTE_HANDLER) {}
        }
        
        addr = (uint8_t*)mbi.BaseAddress + mbi.RegionSize;
    }
    return foundCount;
}

// SEH-safe name pattern scan — finds "Squall" (0x37 0x53 0x57 0x3F 0x4E 0x4E) in memory
// Returns addresses where the 6-byte pattern was found
struct NameScanHit {
    uint32_t addr;
    int32_t  relOffset;  // from pMenuStateA
    uint8_t  context[64]; // bytes surrounding the match (32 before, 32 after)
};

static int ScanForNamePattern_Inner(NameScanHit* hits, int maxHits)
{
    // "Squall" in menu encoding
    // S=0x37, q=0x4F, u=0x53, a=0x3F, l=0x4A, l=0x4A
    static const uint8_t SQUALL[] = { 0x37, 0x4F, 0x53, 0x3F, 0x4A, 0x4A };
    
    SYSTEM_INFO si;
    GetSystemInfo(&si);
    
    uint8_t* addr = (uint8_t*)si.lpMinimumApplicationAddress;
    uint8_t* maxAddr = (uint8_t*)si.lpMaximumApplicationAddress;
    int foundCount = 0;
    
    while (addr < maxAddr && foundCount < maxHits) {
        MEMORY_BASIC_INFORMATION mbi;
        if (VirtualQuery(addr, &mbi, sizeof(mbi)) == 0) break;
        
        if (mbi.State == MEM_COMMIT &&
            (mbi.Protect == PAGE_READWRITE || mbi.Protect == PAGE_READONLY ||
             mbi.Protect == PAGE_EXECUTE_READ || mbi.Protect == PAGE_EXECUTE_READWRITE)) {
            
            uint8_t* base = (uint8_t*)mbi.BaseAddress;
            SIZE_T size = mbi.RegionSize;
            
            __try {
                for (SIZE_T off = 32; off + 38 < size; off++) {
                    if (base[off] == 0x37 && base[off+1] == 0x4F &&
                        base[off+2] == 0x53 && base[off+3] == 0x3F &&
                        base[off+4] == 0x4A && base[off+5] == 0x4A) {
                        
                        NameScanHit* h = &hits[foundCount];
                        h->addr = (uint32_t)(uintptr_t)(base + off);
                        h->relOffset = (int32_t)((uintptr_t)(base + off) - (uintptr_t)pMenuStateA);
                        // Copy 32 bytes before and 32 bytes after
                        for (int i = 0; i < 64; i++)
                            h->context[i] = base[off - 32 + i];
                        foundCount++;
                    }
                    if (foundCount >= maxHits) break;
                }
            } __except(EXCEPTION_EXECUTE_HANDLER) {}
        }
        
        addr = (uint8_t*)mbi.BaseAddress + mbi.RegionSize;
    }
    return foundCount;
}

// Outer function — uses C++ objects for logging/decoding
static void ScanForSaveHeaders()
{
    Log::Write("[SaveScan] === F12 SAVE HEADER MEMORY SCAN ===");
    
    // Pass 1: scan for 0x08FF fixed_value with non-zero data
    Log::Write("[SaveScan] --- Pass 1: 0x08FF fingerprint ---");
    static const int MAX_HITS = 50;
    SaveScanHit hits[MAX_HITS];
    int count = ScanForSaveHeaders_Inner(hits, MAX_HITS);
    
    for (int i = 0; i < count; i++) {
        SaveScanHit* h = &hits[i];
        
        std::string decodedName = FF8TextDecode::DecodeMenuText(h->nameRaw, 12);
        int hrs = h->playTime / 3600;
        int mins = (h->playTime % 3600) / 60;
        
        Log::Write("[SaveScan] FOUND #%d at 0x%08X (pMenuStateA%+d)",
                   i, h->addr, h->relOffset);
        Log::Write("[SaveScan]   name=\"%s\" lvl=%u loc=%u gil=%u time=%dh%02dm disk=%u",
                   decodedName.c_str(), (unsigned)h->lvl, (unsigned)h->locId,
                   h->gil, hrs, mins, h->disk);
        
        char hex1[80] = {}, hex2[80] = {};
        int hp1 = 0, hp2 = 0;
        for (int j = 0; j < 24 && hp1 < 75; j++)
            hp1 += sprintf(hex1 + hp1, "%02X ", h->hdrHex[j]);
        for (int j = 24; j < 48 && hp2 < 75; j++)
            hp2 += sprintf(hex2 + hp2, "%02X ", h->hdrHex[j]);
        Log::Write("[SaveScan]   hdr+00: %s", hex1);
        Log::Write("[SaveScan]   hdr+24: %s", hex2);
    }
    Log::Write("[SaveScan] Pass 1 complete: %d hits", count);
    
    // Pass 2: scan for "Squall" name pattern in menu encoding
    Log::Write("[SaveScan] --- Pass 2: Squall name pattern (37 4F 53 3F 4A 4A) ---");
    static const int MAX_NAME_HITS = 30;
    NameScanHit nameHits[MAX_NAME_HITS];
    int nameCount = ScanForNamePattern_Inner(nameHits, MAX_NAME_HITS);
    
    for (int i = 0; i < nameCount; i++) {
        NameScanHit* h = &nameHits[i];
        
        // Log address and context hex
        Log::Write("[SaveScan] NAME #%d at 0x%08X (pMenuStateA%+d)",
                   i, h->addr, h->relOffset);
        
        // Hex dump: 32 bytes before the name
        char hex1[100] = {};
        int hp1 = 0;
        for (int j = 0; j < 32 && hp1 < 95; j++)
            hp1 += sprintf(hex1 + hp1, "%02X ", h->context[j]);
        Log::Write("[SaveScan]   before: %s", hex1);
        
        // Hex dump: name + 26 bytes after
        char hex2[100] = {};
        int hp2 = 0;
        for (int j = 32; j < 64 && hp2 < 95; j++)
            hp2 += sprintf(hex2 + hp2, "%02X ", h->context[j]);
        Log::Write("[SaveScan]   name+: %s", hex2);
    }
    Log::Write("[SaveScan] Pass 2 complete: %d name hits", nameCount);
    Log::Write("[SaveScan] === SCAN DONE ===");
}

// SEH-protected block cursor poller — announces "Block N" on cursor change
static DWORD s_blockCursorLastPoll = 0;

static void PollBlockCursor(int offset)
{
    DWORD now = GetTickCount();
    if (now - s_blockCursorLastPoll < 100) return;
    s_blockCursorLastPoll = now;
    
    __try {
        uint8_t blockIdx = *((uint8_t*)pMenuStateA + offset);
        
        if (blockIdx > 14) return;
        
        if (blockIdx != s_prevBlockCursor) {
            int blockNum = blockIdx + 1;
            
            // Look up cached save data for this block
            // blockIdx maps to save file number: blockIdx+1 = save file 01-15
            // Slot comes from s_saveCursorSlot (1 or 2)
            int si = (s_saveCursorSlot > 0 ? s_saveCursorSlot : 1) - 1;
            int bi = blockIdx;  // save file = blockIdx + 1, array index = blockIdx
            
            char buf[512];
            if (si >= 0 && si < 2 && bi >= 0 && bi < 30 &&
                s_saveCache[si][bi].valid && s_saveCache[si][bi].char1_lvl > 0) {
                CachedSaveHeader* h = &s_saveCache[si][bi];
                int hrs = h->played_time_secs / 3600;
                int mins = (h->played_time_secs % 3600) / 60;
                // Format: "Block N: Party, Level L, Location, H hours M minutes, G gil"
                int pos = sprintf(buf, "Block %d: ", blockNum);
                // Party members (or lead character name if no portrait data)
                if (h->partyStr[0])
                    pos += sprintf(buf + pos, "%s", h->partyStr);
                else
                    pos += sprintf(buf + pos, "%s", h->nameDecoded);
                pos += sprintf(buf + pos, ", Level %u", (unsigned)h->char1_lvl);
                // Location
                if (h->locationStr[0])
                    pos += sprintf(buf + pos, ", %s", h->locationStr);
                pos += sprintf(buf + pos, ", %d hours %d minutes, %u gil",
                               hrs, mins, h->gil);
            } else {
                sprintf(buf, "Block %d: Empty", blockNum);
            }
            
            ScreenReader::Speak(buf, true);
            Log::Write("[MenuTTS] Block cursor: %s (idx=%u)", buf, (unsigned)blockIdx);
            s_prevBlockCursor = blockIdx;
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
}

// Poll GCW buffer for save screen text + track arrow keys for cursor
static void PollSaveScreen()
{
    DWORD now = GetTickCount();
    if (now - s_saveLastPollTime < SAVE_POLL_INTERVAL) return;
    s_saveLastPollTime = now;
    
    // Snapshot the GCW buffer
    uint8_t gcwBuf[1024];
    int gcwLen = FieldDialog::SnapshotGcwBuffer(gcwBuf, sizeof(gcwBuf));
    
    if (gcwLen > 0) {
        s_saveGcwZeroCount = 0;
        
        // Decode and check for save screen markers
        std::string decoded = FF8TextDecode::DecodeMenuText(gcwBuf, gcwLen);
        
        bool isSaveScreen = (decoded.find("GAME FOLDER") != std::string::npos ||
                             (decoded.find("Slot") != std::string::npos &&
                              decoded.find("FINAL FANTASY") != std::string::npos));
        
        if (isSaveScreen) {
            if (!s_saveScreenActive) {
                s_saveScreenActive = true;
                s_prevSaveSlotCursor = 0xFF;  // force announce on first open
                s_prevSavePhase = 0;           // reset phase tracking for mode 1
                Log::Write("[MenuTTS] Save screen detected (GCW)");
            }
            s_saveGcwZeroCount = 0;  // reset exit timer while text is present
        }
        
        // Poll the cursor byte whenever save screen is active (mode 1 offset)
        // announceFirst=true: save screen is confirmed open, announce initial slot
        if (s_saveScreenActive) {
            // v0.07.46: check +0x1EE phase for block->slot transition in mode 1
            if (CheckSavePhaseTransition(SAVE_PHASE_OFFSET_MODE1)) {
                s_prevSaveSlotCursor = 0xFF;  // force re-announce
            }
            PollSaveSlotCursor(SAVE_SLOT_CURSOR_OFFSET_MODE1, true);
            // v0.07.48: Poll block cursor during block selection
            if (s_prevSavePhase >= 2) {
                PollBlockCursor(BLOCK_CURSOR_OFFSET_MODE1);
            } else {
                s_prevBlockCursor = 0xFF;
            }
        }
    } else {
        // No GCW data this poll
        if (s_saveScreenActive) {
            // Still poll the cursor byte even without GCW data (mode 1 offset)
            if (CheckSavePhaseTransition(SAVE_PHASE_OFFSET_MODE1)) {
                s_prevSaveSlotCursor = 0xFF;  // force re-announce
            }
            PollSaveSlotCursor(SAVE_SLOT_CURSOR_OFFSET_MODE1, true);
            // v0.07.48: Poll block cursor during block selection
            if (s_prevSavePhase >= 2) {
                PollBlockCursor(BLOCK_CURSOR_OFFSET_MODE1);
            } else {
                s_prevBlockCursor = 0xFF;
            }
            
            s_saveGcwZeroCount++;
            // Only exit after a longer timeout (20 polls = 6 seconds)
            // to handle slow save screen rendering from in-game menu path
            if (s_saveGcwZeroCount >= 20) {
                Log::Write("[MenuTTS] Save screen exited (no GCW data for %d polls)",
                           s_saveGcwZeroCount);
                s_saveScreenActive = false;
                s_saveCursorSlot = -1;
                s_saveScreenPhase.clear();
                s_saveGcwZeroCount = 0;
                s_prevSaveSlotCursor = 0xFF;
            }
        }
    }
}

// ============================================================================
// Initialize
// ============================================================================
void MenuTTS::Initialize()
{
    Log::Write("[MenuTTS] Initialize() — v0.08.61 Item submenu TTS (+0x22E with intermediate transitions)");
    
    if (pMenuStateA == nullptr) {
        Log::Write("[MenuTTS] WARNING: pMenuStateA not resolved, menu TTS disabled");
        return;
    }
    
    Log::Write("[MenuTTS] Menu cursor at pMenuStateA + 0x%X = absolute 0x%08X",
               CURSOR_OFFSET, (uint32_t)(uintptr_t)pMenuStateA + CURSOR_OFFSET);
    Log::Write("[MenuTTS] Save slot cursor mode1 at pMenuStateA + 0x%X = absolute 0x%08X",
               SAVE_SLOT_CURSOR_OFFSET_MODE1, (uint32_t)(uintptr_t)pMenuStateA + SAVE_SLOT_CURSOR_OFFSET_MODE1);
    Log::Write("[MenuTTS] Save slot cursor mode6 at pMenuStateA + 0x%X = absolute 0x%08X",
               SAVE_SLOT_CURSOR_OFFSET_MODE6, (uint32_t)(uintptr_t)pMenuStateA + SAVE_SLOT_CURSOR_OFFSET_MODE6);
    
    
    s_initialized = true;
    Log::Write("[MenuTTS] Initialize() complete");
}

// ============================================================================
// Helper: poll menu cursor with SEH protection (separate function to avoid
// C2712 — __try can't coexist with C++ object unwinding in same function)
// ============================================================================
static void PollMenuCursor()
{
    uint8_t* base = (uint8_t*)pMenuStateA;
    
    __try {
        uint8_t cursor = *(base + CURSOR_OFFSET);
        
        if (cursor != s_prevCursor) {
            const char* name = GetMenuItemName(cursor);
            if (name) {
                ScreenReader::Speak(name, true);
                Log::Write("[MenuTTS] Cursor %u -> %u: %s",
                           (unsigned)s_prevCursor, (unsigned)cursor, name);
            } else {
                Log::Write("[MenuTTS] Cursor %u -> %u (unknown index)",
                           (unsigned)s_prevCursor, (unsigned)cursor);
            }
            
            s_prevCursor = cursor;
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
}

// v0.08.22: Capture GCW text periodically in menu mode to find help text
static DWORD s_lastMenuGcwCapture = 0;
static std::string s_lastMenuGcwText;

static void CaptureMenuGcwText()
{
    DWORD now = GetTickCount();
    if (now - s_lastMenuGcwCapture < 500) return;  // every 500ms
    s_lastMenuGcwCapture = now;
    
    uint8_t gcwBuf[2048];
    int gcwLen = FieldDialog::SnapshotGcwBuffer(gcwBuf, sizeof(gcwBuf));
    if (gcwLen > 0) {
        std::string decoded = FF8TextDecode::DecodeMenuText(gcwBuf, gcwLen);
        if (!decoded.empty() && decoded != s_lastMenuGcwText) {
            Log::Write("[MenuGCW] cursor=%u text(%d): \"%s\"",
                       (unsigned)s_prevCursor, gcwLen, decoded.c_str());
            s_lastMenuGcwText = decoded;
        }
    }
}

// SEH-protected diagnostic logger for save state during block selection
static void LogSaveDiagState()
{
    __try {
        uint8_t e8 = *((uint8_t*)pMenuStateA + 0x1E8);
        uint8_t c276 = *((uint8_t*)pMenuStateA + 0x276);
        Log::Write("[MenuTTS] SaveDiag: +0x1E8=%u +0x276=%u prevSlot=%u",
                   (unsigned)e8, (unsigned)c276, (unsigned)s_prevSaveSlotCursor);
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
}

// Log key offsets while save subsystem is active, to find block selection signal
static uint8_t s_saveDiagSnap[8] = {};
static bool    s_saveDiagSnapValid = false;

static void LogSaveSubsystemChanges()
{
    __try {
        // Candidate offsets that changed during save flow in v0.07.36 scan
        static const int offsets[] = { 0x1E8, 0x230, 0x24A, 0x24B, 0x266, 0x268, 0x276, 0x22E };
        static const int N = 8;
        uint8_t cur[8];
        for (int i = 0; i < N; i++)
            cur[i] = *((uint8_t*)pMenuStateA + offsets[i]);
        
        if (!s_saveDiagSnapValid) {
            memcpy(s_saveDiagSnap, cur, N);
            s_saveDiagSnapValid = true;
            Log::Write("[SaveActive] init: 1E8=%u 230=%u 24A=%u 24B=%u 266=%u 268=%u 276=%u 22E=%u",
                       cur[0],cur[1],cur[2],cur[3],cur[4],cur[5],cur[6],cur[7]);
            return;
        }
        
        for (int i = 0; i < N; i++) {
            if (cur[i] != s_saveDiagSnap[i]) {
                Log::Write("[SaveActive] +0x%03X: %u -> %u",
                           offsets[i], (unsigned)s_saveDiagSnap[i], (unsigned)cur[i]);
                s_saveDiagSnap[i] = cur[i];
            }
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
}

// ============================================================================
// v0.08.17: Menu data diagnostic — dump character stats, scan for Gil/time
// ============================================================================
// Known addresses from FFNx:
//   character_data_1CFE74C: base of 8 × savemap_ff8_character (152 bytes each)
//   field_vars_stack_1CFE9B8: varblock base (save offset 0xD10)
// Character struct layout (savemap_ff8_character from save_data.h):
//   +0x00: uint16 current_hp
//   +0x02: uint16 max_hp
//   +0x04: uint32 exp
//   +0x08: uint8 model_id (0=Squall,1=Zell,2=Irvine,3=Quistis,4=Rinoa,5=Selphie,6=Seifer,7=Edea)
//   +0x09: uint8 weapon_id
//   +0x0A-0x0F: str,vit,mag,spr,spd,lck
//   +0x94: uint8 exists (non-zero if character exists)
//   +0x96: uint8 status
static const uint32_t CHAR_DATA_BASE = 0x1CFE74C;  // hardcoded Steam 2013 en-US
static const int CHAR_STRUCT_SIZE = 0x98;  // 152 bytes per character
static const int CHAR_COUNT = 8;
static const char* CHAR_NAMES[] = {
    "Squall", "Zell", "Irvine", "Quistis", "Rinoa", "Selphie", "Seifer", "Edea"
};

// v0.08.18: Confirmed savemap base from v0.08.17 diagnostic.
// Gil=5000 at 0x1CFDC64, time=619 at 0x1CFDC68.
// savemap_ff8_header.gil is at header+0x08, so header starts at 0x1CFDC5C.
// Characters[0].current_hp=486 at 0x1CFE0E8, characters[0].exp=6500 at 0x1CFE0EC.
static const uint32_t SAVEMAP_BASE = 0x1CFDC5C;

// savemap_ff8_header offsets (from save_data.h)
static const int HDR_LOCATION_ID   = 0x00;  // uint16
static const int HDR_CHAR1_CURR_HP = 0x02;  // uint16
static const int HDR_CHAR1_MAX_HP  = 0x04;  // uint16
static const int HDR_SAVE_COUNT    = 0x06;  // uint16
static const int HDR_GIL           = 0x08;  // uint32
static const int HDR_PLAYED_TIME   = 0x0C;  // uint32 (seconds)
static const int HDR_CHAR1_LVL     = 0x10;  // uint8
static const int HDR_PORTRAITS     = 0x11;  // uint8[3] (char1, char2, char3)
static const int HDR_SQUALL_NAME   = 0x14;  // uint8[12] FF8-encoded
static const int HDR_RINOA_NAME    = 0x20;  // uint8[12]
static const int HDR_ANGELO_NAME   = 0x2C;  // uint8[12]
static const int HDR_BOKO_NAME     = 0x38;  // uint8[12]
static const int HDR_CURR_DISK     = 0x44;  // uint32
static const int HDR_CURR_SAVE     = 0x48;  // uint32
static const int HDR_SIZE          = 0x4C;

// After header: 16 GFs at 68 bytes each = 0x440 bytes
static const int GF_SECTION_SIZE   = 16 * 68;  // 0x440
// Characters start at header + HDR_SIZE + GF_SECTION_SIZE = 0x4C + 0x440 = 0x48C
static const int CHARS_OFFSET      = HDR_SIZE + GF_SECTION_SIZE;  // 0x48C from savemap base

// savemap_ff8_character offsets (from save_data.h)
static const int CHR_CURR_HP   = 0x00;  // uint16
static const int CHR_MAX_HP    = 0x02;  // uint16
static const int CHR_EXP       = 0x04;  // uint32
static const int CHR_MODEL_ID  = 0x08;  // uint8
static const int CHR_WEAPON_ID = 0x09;  // uint8
static const int CHR_STR       = 0x0A;  // uint8
static const int CHR_VIT       = 0x0B;  // uint8
static const int CHR_MAG       = 0x0C;  // uint8
static const int CHR_SPR       = 0x0D;  // uint8
static const int CHR_SPD       = 0x0E;  // uint8
static const int CHR_LCK       = 0x0F;  // uint8
static const int CHR_EXISTS    = 0x94;  // uint8
static const int CHR_STATUS    = 0x96;  // uint8

// ============================================================================
// v0.08.27: Savemap offset verification — compare current offsets vs deep research
// ============================================================================
static void VerifySavemapOffsets()
{
    Log::Write("[OFFSET-VERIFY] === Savemap Offset Verification (v0.08.27) ===");
    Log::Write("[OFFSET-VERIFY] SAVEMAP_BASE = 0x%08X", SAVEMAP_BASE);

    uint8_t* sm = (uint8_t*)SAVEMAP_BASE;

    // --- Gil comparison ---
    uint32_t gilOurs     = *(uint32_t*)(sm + 0x08);    // our current: header+0x08
    uint32_t gilResearch = *(uint32_t*)(sm + 0x0B1C);  // research: gameplay Gil
    Log::Write("[OFFSET-VERIFY] GIL: ours(+0x08)=%u  research(+0x0B1C)=%u  %s",
               gilOurs, gilResearch,
               (gilOurs == gilResearch) ? "MATCH" : "DIFFER");

    // --- Play time comparison ---
    uint32_t timeOurs     = *(uint32_t*)(sm + 0x0C);    // our current: header+0x0C (stale)
    uint32_t timeResearch = *(uint32_t*)(sm + 0x0CE0);  // research: live game time
    Log::Write("[OFFSET-VERIFY] TIME: ours(+0x0C)=%u sec  research(+0x0CE0)=%u sec  %s",
               timeOurs, timeResearch,
               (timeOurs == timeResearch) ? "MATCH" : "DIFFER");

    // --- Party composition ---
    uint8_t partyOurs[3], partyResearch[4];
    partyOurs[0] = *(sm + 0xAF1);
    partyOurs[1] = *(sm + 0xAF2);
    partyOurs[2] = *(sm + 0xAF3);
    partyResearch[0] = *(sm + 0x0B04);
    partyResearch[1] = *(sm + 0x0B05);
    partyResearch[2] = *(sm + 0x0B06);
    partyResearch[3] = *(sm + 0x0B07);
    Log::Write("[OFFSET-VERIFY] PARTY: ours(+0xAF1)=(%u,%u,%u)  research(+0x0B04)=(%u,%u,%u,%u)",
               partyOurs[0], partyOurs[1], partyOurs[2],
               partyResearch[0], partyResearch[1], partyResearch[2], partyResearch[3]);

    // --- Character[0] (Squall) comparison ---
    // Our offset: savemap + 0x48C
    // Research offset: savemap + 0x4A0
    uint8_t* charOurs     = sm + 0x48C;
    uint8_t* charResearch = sm + 0x4A0;
    uint16_t hpOurs     = *(uint16_t*)(charOurs + 0x00);
    uint16_t maxHpOurs  = *(uint16_t*)(charOurs + 0x02);
    uint32_t expOurs    = *(uint32_t*)(charOurs + 0x04);
    uint8_t  modelOurs  = *(charOurs + 0x08);
    uint16_t hpRes      = *(uint16_t*)(charResearch + 0x00);
    uint16_t maxHpRes   = *(uint16_t*)(charResearch + 0x02);
    uint32_t expRes     = *(uint32_t*)(charResearch + 0x04);
    uint8_t  modelRes   = *(charResearch + 0x08);
    Log::Write("[OFFSET-VERIFY] CHAR[0] OURS(+0x48C): hp=%u/%u exp=%u model=%u",
               hpOurs, maxHpOurs, expOurs, (unsigned)modelOurs);
    Log::Write("[OFFSET-VERIFY] CHAR[0] RESEARCH(+0x4A0): hp=%u/%u exp=%u model=%u",
               hpRes, maxHpRes, expRes, (unsigned)modelRes);
    bool charMatch = (hpOurs == hpRes && expOurs == expRes && modelOurs == modelRes);
    Log::Write("[OFFSET-VERIFY] CHAR[0]: %s", charMatch ? "MATCH" : "DIFFER");

    // --- GF[0] (Quetzalcoatl) name comparison ---
    // Our offset: savemap + 0x4C (header 0x4C)
    // Research offset: savemap + 0x60 (header 0x60)
    uint8_t* gfOurs     = sm + 0x4C;
    uint8_t* gfResearch = sm + 0x60;
    // GF name is first 12 bytes, FF8-encoded. Decode both.
    uint8_t gfNameOursAdj[12], gfNameResAdj[12];
    for (int i = 0; i < 12; i++) {
        gfNameOursAdj[i] = (gfOurs[i] >= 0x20) ? (gfOurs[i] - 0x20) : gfOurs[i];
        gfNameResAdj[i] = (gfResearch[i] >= 0x20) ? (gfResearch[i] - 0x20) : gfResearch[i];
    }
    char gfNameOursDec[32] = {}, gfNameResDec[32] = {};
    DecodeNameToBuffer(gfNameOursAdj, 12, gfNameOursDec, sizeof(gfNameOursDec));
    DecodeNameToBuffer(gfNameResAdj, 12, gfNameResDec, sizeof(gfNameResDec));
    // Also raw hex
    char gfHexOurs[64] = {}, gfHexRes[64] = {};
    for (int i = 0; i < 12; i++) {
        sprintf(gfHexOurs + i*3, "%02X ", gfOurs[i]);
        sprintf(gfHexRes + i*3, "%02X ", gfResearch[i]);
    }
    Log::Write("[OFFSET-VERIFY] GF[0] OURS(+0x4C): raw=[%s] decoded='%s'",
               gfHexOurs, gfNameOursDec);
    Log::Write("[OFFSET-VERIFY] GF[0] RESEARCH(+0x60): raw=[%s] decoded='%s'",
               gfHexRes, gfNameResDec);

    // --- Additional character scan: dump char[0] through char[2] at both offsets ---
    for (int c = 0; c < 3; c++) {
        uint8_t* cOurs = sm + 0x48C + 0x98 * c;
        uint8_t* cRes  = sm + 0x4A0 + 0x98 * c;
        Log::Write("[OFFSET-VERIFY] CHAR[%d] ours(+0x%X): hp=%u/%u exp=%u model=%u exists=%u",
                   c, 0x48C + 0x98*c,
                   *(uint16_t*)(cOurs+0x00), *(uint16_t*)(cOurs+0x02),
                   *(uint32_t*)(cOurs+0x04), (unsigned)*(cOurs+0x08),
                   (unsigned)*(cOurs+0x94));
        Log::Write("[OFFSET-VERIFY] CHAR[%d] research(+0x%X): hp=%u/%u exp=%u model=%u exists=%u",
                   c, 0x4A0 + 0x98*c,
                   *(uint16_t*)(cRes+0x00), *(uint16_t*)(cRes+0x02),
                   *(uint32_t*)(cRes+0x04), (unsigned)*(cRes+0x08),
                   (unsigned)*(cRes+0x94));
    }

    // --- Location ID at both header offsets ---
    uint16_t locOurs = *(uint16_t*)(sm + 0x00);  // our current
    uint16_t locRes  = *(uint16_t*)(sm + 0x04);  // research says +0x04
    Log::Write("[OFFSET-VERIFY] LOCATION: ours(+0x00)=%u  research(+0x04)=%u",
               (unsigned)locOurs, (unsigned)locRes);

    // --- Current field ID (research says +0x0D52) ---
    uint16_t fieldId = *(uint16_t*)(sm + 0x0D52);
    Log::Write("[OFFSET-VERIFY] FIELD_ID(+0x0D52)=%u", (unsigned)fieldId);

    // --- SeeD test level (research says +0x0D43) ---
    uint8_t seedTest = *(sm + 0x0D43);
    Log::Write("[OFFSET-VERIFY] SEED_TEST_LVL(+0x0D43)=%u", (unsigned)seedTest);

    // --- Item inventory spot check (research says +0x0B54) ---
    uint8_t* items = sm + 0x0B54;
    int itemCount = 0;
    for (int i = 0; i < 198; i++) {
        if (items[i*2] != 0) itemCount++;
    }
    Log::Write("[OFFSET-VERIFY] ITEMS(+0x0B54): %d non-empty slots out of 198", itemCount);
    // Log first 5 non-empty items
    int logged = 0;
    for (int i = 0; i < 198 && logged < 5; i++) {
        uint8_t id = items[i*2];
        uint8_t qty = items[i*2 + 1];
        if (id != 0) {
            Log::Write("[OFFSET-VERIFY]   item[%d]: id=%u qty=%u", i, (unsigned)id, (unsigned)qty);
            logged++;
        }
    }

    // --- CORRECTED research offsets (subtract 0x14 from research values) ---
    Log::Write("[OFFSET-VERIFY] === Corrected Research Offsets (research - 0x14) ===");

    // Gameplay Gil: research +0x0B1C -> corrected +0x0B08
    uint32_t gilCorrected = *(uint32_t*)(sm + 0x0B08);
    Log::Write("[OFFSET-VERIFY] GIL CORRECTED(+0x0B08)=%u  ours(+0x08)=%u  %s",
               gilCorrected, gilOurs,
               (gilCorrected == gilOurs) ? "MATCH" : "DIFFER");

    // Live game time: research +0x0CE0 -> corrected +0x0CCC
    uint32_t timeCorrected = *(uint32_t*)(sm + 0x0CCC);
    Log::Write("[OFFSET-VERIFY] TIME CORRECTED(+0x0CCC)=%u sec  ours(+0x0C)=%u sec  %s",
               timeCorrected, timeOurs,
               (timeCorrected == timeOurs) ? "MATCH" : "DIFFER");

    // Active party: research +0x0B04 -> corrected +0x0AF0
    uint8_t partyCorrected[4];
    partyCorrected[0] = *(sm + 0x0AF0);
    partyCorrected[1] = *(sm + 0x0AF1);
    partyCorrected[2] = *(sm + 0x0AF2);
    partyCorrected[3] = *(sm + 0x0AF3);
    Log::Write("[OFFSET-VERIFY] PARTY CORRECTED(+0x0AF0)=(%u,%u,%u,%u)  ours(+0xAF1)=(%u,%u,%u)",
               partyCorrected[0], partyCorrected[1], partyCorrected[2], partyCorrected[3],
               partyOurs[0], partyOurs[1], partyOurs[2]);

    // Item inventory: research +0x0B54 -> corrected +0x0B40
    uint8_t* itemsCorrected = sm + 0x0B40;
    int itemCountCorr = 0;
    for (int i = 0; i < 198; i++) {
        if (itemsCorrected[i*2] != 0) itemCountCorr++;
    }
    Log::Write("[OFFSET-VERIFY] ITEMS CORRECTED(+0x0B40): %d non-empty slots out of 198", itemCountCorr);
    int loggedCorr = 0;
    for (int i = 0; i < 198 && loggedCorr < 5; i++) {
        uint8_t id2 = itemsCorrected[i*2];
        uint8_t qty2 = itemsCorrected[i*2 + 1];
        if (id2 != 0) {
            Log::Write("[OFFSET-VERIFY]   item[%d]: id=%u qty=%u", i, (unsigned)id2, (unsigned)qty2);
            loggedCorr++;
        }
    }

    // Current field ID: research +0x0D52 -> corrected +0x0D3E
    uint16_t fieldIdCorr = *(uint16_t*)(sm + 0x0D3E);
    Log::Write("[OFFSET-VERIFY] FIELD_ID CORRECTED(+0x0D3E)=%u", (unsigned)fieldIdCorr);

    // SeeD test level: research +0x0D43 -> corrected +0x0D2F
    uint8_t seedTestCorr = *(sm + 0x0D2F);
    Log::Write("[OFFSET-VERIFY] SEED_TEST CORRECTED(+0x0D2F)=%u", (unsigned)seedTestCorr);

    // Config: research +0x0AF0 -> corrected +0x0ADC
    Log::Write("[OFFSET-VERIFY] CONFIG region CORRECTED(+0x0ADC) first 20 bytes:");
    {
        char cfgHex[120] = {};
        int hp = 0;
        for (int i = 0; i < 20 && hp < 100; i++)
            hp += sprintf(cfgHex + hp, "%02X ", *(sm + 0x0ADC + i));
        Log::Write("[OFFSET-VERIFY]   %s", cfgHex);
    }

    Log::Write("[OFFSET-VERIFY] === End Offset Verification ===");
}

static void DumpMenuScreenData()
{
    Log::Write("[MENUDIAG] === Menu Screen Data Dump (v0.08.18) ===");

    // --- 1. Savemap header (confirmed base 0x1CFDC5C) ---
    {  // SEH removed — diagnostic function, crash is acceptable
        uint8_t* hdr = (uint8_t*)SAVEMAP_BASE;
        uint16_t locId    = *(uint16_t*)(hdr + HDR_LOCATION_ID);
        uint16_t hdrHp    = *(uint16_t*)(hdr + HDR_CHAR1_CURR_HP);
        uint16_t hdrMaxHp = *(uint16_t*)(hdr + HDR_CHAR1_MAX_HP);
        uint16_t saveCnt  = *(uint16_t*)(hdr + HDR_SAVE_COUNT);
        uint32_t gil      = *(uint32_t*)(hdr + HDR_GIL);
        uint32_t timeSec  = *(uint32_t*)(hdr + HDR_PLAYED_TIME);
        uint8_t  lvl      = *(hdr + HDR_CHAR1_LVL);
        uint8_t  port1    = *(hdr + HDR_PORTRAITS + 0);
        uint8_t  port2    = *(hdr + HDR_PORTRAITS + 1);
        uint8_t  port3    = *(hdr + HDR_PORTRAITS + 2);
        uint32_t disk     = *(uint32_t*)(hdr + HDR_CURR_DISK);
        uint32_t currSave = *(uint32_t*)(hdr + HDR_CURR_SAVE);
        Log::Write("[MENUDIAG] HEADER @0x%08X:", SAVEMAP_BASE);
        Log::Write("[MENUDIAG]   locId=%u hp=%u/%u saveCount=%u gil=%u",
                   (unsigned)locId, (unsigned)hdrHp, (unsigned)hdrMaxHp,
                   (unsigned)saveCnt, gil);
        Log::Write("[MENUDIAG]   time=%u sec (%u:%02u:%02u) lvl=%u disk=%u currSave=%u",
                   timeSec, timeSec/3600, (timeSec%3600)/60, timeSec%60,
                   (unsigned)lvl, disk, currSave);
        Log::Write("[MENUDIAG]   portraits=(%u,%u,%u)",
                   (unsigned)port1, (unsigned)port2, (unsigned)port3);
        // Decode character names (FF8 encoded)
        for (int n = 0; n < 4; n++) {
            int nameOff = HDR_SQUALL_NAME + n * 12;
            const char* label = (n==0)?"squall":(n==1)?"rinoa":(n==2)?"angelo":"boko";
            char raw[64] = {};
            int rp = 0;
            for (int i = 0; i < 12 && rp < 60; i++)
                rp += sprintf(raw + rp, "%02X ", hdr[nameOff + i]);
            // Live savemap names are +0x20 offset from menu encoding.
            // Subtract 0x20 from each non-zero byte before decoding.
            uint8_t adjusted[12];
            for (int i = 0; i < 12; i++) {
                uint8_t b = hdr[nameOff + i];
                adjusted[i] = (b >= 0x20) ? (b - 0x20) : b;
            }
            char decodedName[32] = {};
            DecodeNameToBuffer(adjusted, 12, decodedName, sizeof(decodedName));
            Log::Write("[MENUDIAG]   name_%s: raw=[%s] decoded='%s'",
                       label, raw, decodedName);
        }
    }

    // --- 2. Character structs (8 chars starting at savemap+0x50C) ---
    {
        uint8_t* charBase = (uint8_t*)(SAVEMAP_BASE + CHARS_OFFSET);
        Log::Write("[MENUDIAG] CHARACTER DATA @0x%08X (savemap+0x%X, 8 x %d bytes):",
                   (uint32_t)(SAVEMAP_BASE + CHARS_OFFSET), CHARS_OFFSET, CHAR_STRUCT_SIZE);
        for (int c = 0; c < CHAR_COUNT; c++) {
            uint8_t* ch = charBase + CHAR_STRUCT_SIZE * c;
            uint16_t curHp  = *(uint16_t*)(ch + CHR_CURR_HP);
            uint16_t maxHp  = *(uint16_t*)(ch + CHR_MAX_HP);
            uint32_t exp    = *(uint32_t*)(ch + CHR_EXP);
            uint8_t  modelId = *(ch + CHR_MODEL_ID);
            uint8_t  weapId  = *(ch + CHR_WEAPON_ID);
            uint8_t  str = *(ch + CHR_STR), vit = *(ch + CHR_VIT);
            uint8_t  mag = *(ch + CHR_MAG), spr = *(ch + CHR_SPR);
            uint8_t  spd = *(ch + CHR_SPD), lck = *(ch + CHR_LCK);
            uint8_t  exists  = *(ch + CHR_EXISTS);
            uint8_t  status  = *(ch + CHR_STATUS);
            const char* name = (modelId < 8) ? CHAR_NAMES[modelId] : "???";
            Log::Write("[MENUDIAG]   char[%d] @0x%08X model=%u(%s) exists=%u HP=%u/%u EXP=%u "
                       "weapon=%u str=%u vit=%u mag=%u spr=%u spd=%u lck=%u status=%u",
                       c, (uint32_t)(uintptr_t)ch, (unsigned)modelId, name, (unsigned)exists,
                       (unsigned)curHp, (unsigned)maxHp, exp,
                       (unsigned)weapId, (unsigned)str, (unsigned)vit,
                       (unsigned)mag, (unsigned)spr, (unsigned)spd, (unsigned)lck,
                       (unsigned)status);
        }
    }

    // --- 3. Party composition + SeeD rank scan ---
    // savemap_ff8_field_h contains SeeD EXP at offset +0x08 (uint16).
    // Party order is stored as battle_order[32] in savemap_ff8_items.
    // These sections follow characters in the savemap.
    // Characters end at savemap + 0x50C + 8*0x98 = savemap + 0x98C
    // After chars: shops, limit breaks, items (with battle_order), then battle, field_h, field.
    // Let's dump 512 bytes after the character section to find party + SeeD.
    {
        uint32_t postCharStart = SAVEMAP_BASE + CHARS_OFFSET + CHAR_STRUCT_SIZE * CHAR_COUNT;
        Log::Write("[MENUDIAG] POST-CHARACTER REGION @0x%08X (savemap+0x%X, 512 bytes hex):",
                   postCharStart, CHARS_OFFSET + CHAR_STRUCT_SIZE * CHAR_COUNT);
        for (int off = 0; off < 512; off += 16) {
            uint8_t* p = (uint8_t*)(postCharStart + off);
            char line[120] = {};
            int lpos = 0;
            lpos += sprintf(line + lpos, "+0x%04X ", CHARS_OFFSET + CHAR_STRUCT_SIZE * CHAR_COUNT + off);
            for (int b = 0; b < 16 && lpos < 100; b++)
                lpos += sprintf(line + lpos, "%02X ", p[b]);
            Log::Write("[MENUDIAG]   %s", line);
        }
    }

    // --- 4. Scan for SeeD EXP (uint16, range 1-31000ish) in savemap ---
    // Also look for battle_order bytes (party composition: 3 bytes, each 0-7 or 0xFF)
    {
        // Scan from after header to end of savemap (~4KB after characters)
        uint32_t scanStart = SAVEMAP_BASE + HDR_SIZE;  // after header
        uint32_t scanEnd   = SAVEMAP_BASE + 0x1800;    // ~6KB total
        Log::Write("[MENUDIAG] PARTY SCAN 0x%08X - 0x%08X:", scanStart, scanEnd);
        int partyHits = 0;
        for (uint32_t addr = scanStart; addr < scanEnd - 3 && partyHits < 15; addr++) {
            uint8_t b0 = *(uint8_t*)addr;
            uint8_t b1 = *(uint8_t*)(addr + 1);
            uint8_t b2 = *(uint8_t*)(addr + 2);
            // Only Squall at this point: (0, 0xFF, 0xFF)
            if (b0 == 0 && b1 == 0xFF && b2 == 0xFF) {
                uint32_t smOff = addr - SAVEMAP_BASE;
                Log::Write("[MENUDIAG]   [PARTY] 0x%08X (savemap+0x%X) = (0, FF, FF)",
                           addr, smOff);
                partyHits++;
            }
        }
    }

    // --- 5. Hex dump of header raw bytes for verification ---
    {
        uint8_t* hdr = (uint8_t*)SAVEMAP_BASE;
        Log::Write("[MENUDIAG] HEADER RAW (first 0x4C bytes):");
        for (int off = 0; off < 0x4C; off += 16) {
            char line[120] = {};
            int lpos = 0;
            lpos += sprintf(line + lpos, "+0x%02X ", off);
            for (int b = 0; b < 16 && off + b < 0x4C && lpos < 100; b++)
                lpos += sprintf(line + lpos, "%02X ", hdr[off + b]);
            Log::Write("[MENUDIAG]   %s", line);
        }
    }

    Log::Write("[MENUDIAG] === End Menu Screen Data Dump ===");
}

// ============================================================================
// v0.08.20: Menu open summary — announce party, Gil, time, location
// ============================================================================
static const int PARTY_OFFSET = 0xAF1;  // 3 bytes: active party member indices

static void AnnounceMenuSummary()
{
    uint8_t* sm = (uint8_t*)SAVEMAP_BASE;

    // Read header fields
    uint32_t gil      = *(uint32_t*)(sm + HDR_GIL);
    uint32_t timeSec  = *(uint32_t*)(sm + HDR_PLAYED_TIME);
    uint8_t  lvl      = *(sm + HDR_CHAR1_LVL);
    uint16_t locId    = *(uint16_t*)(sm + HDR_LOCATION_ID);

    // Location name from SETPLACE table
    const char* locName = GetLocationNameById(locId);
    char locBuf[64];
    if (!locName) {
        sprintf(locBuf, "Location %u", (unsigned)locId);
        locName = locBuf;
    }

    // Read party indices
    uint8_t party[3];
    party[0] = *(sm + PARTY_OFFSET + 0);
    party[1] = *(sm + PARTY_OFFSET + 1);
    party[2] = *(sm + PARTY_OFFSET + 2);

    // Build party string with HP for each member
    char partyBuf[512] = {};
    int pp = 0;
    uint8_t* charBase = sm + CHARS_OFFSET;
    for (int i = 0; i < 3; i++) {
        uint8_t idx = party[i];
        if (idx > 7) continue;  // 0xFF = empty slot
        uint8_t* ch = charBase + CHAR_STRUCT_SIZE * idx;
        uint16_t hp = *(uint16_t*)(ch + CHR_CURR_HP);
        const char* name = (idx < 8) ? CHAR_NAMES[idx] : "Unknown";
        if (pp > 0) pp += sprintf(partyBuf + pp, ". ");
        uint32_t exp = *(uint32_t*)(ch + CHR_EXP);
        int charLvl;
        if (i == 0) {
            charLvl = lvl;  // header level is accurate for lead
        } else {
            charLvl = (int)(exp / 1000) + 1;
            if (charLvl > 100) charLvl = 100;
        }
        if (i == 0) {
            uint16_t maxHp = *(uint16_t*)(sm + HDR_CHAR1_MAX_HP);
            pp += sprintf(partyBuf + pp, "%s Level %d, HP %u of %u",
                          name, charLvl, (unsigned)hp, (unsigned)maxHp);
        } else {
            pp += sprintf(partyBuf + pp, "%s Level %d, HP %u",
                          name, charLvl, (unsigned)hp);
        }
    }

    // Format play time
    int hours = timeSec / 3600;
    int mins  = (timeSec % 3600) / 60;
    char timeBuf[32];
    if (hours > 0)
        sprintf(timeBuf, "%d hours %d minutes", hours, mins);
    else
        sprintf(timeBuf, "%d minutes", mins);

    // Build full announcement
    char announce[1024];
    sprintf(announce, "%s. %s. %u Gil. Play time %s.",
            locName, partyBuf, gil, timeBuf);

    ScreenReader::Speak(announce, true);
    Log::Write("[MenuTTS] Menu summary: %s", announce);
}

// ============================================================================
// v0.08.22: Left-panel cursor diagnostic + help text capture
// ============================================================================
// When F11+Shift is pressed, starts a memory monitor that:
//   1. Snapshots 2KB around pMenuStateA every 200ms for 15 seconds
//   2. Logs any byte changes — these reveal the left-panel cursor address
//   3. Captures GCW buffer on each snapshot to see rendered help text
// Instructions: Press Shift+F11, then press LEFT arrow to move cursor to party
// member. The monitor will log which bytes changed.

static bool     s_memMonitorActive = false;
static DWORD    s_memMonitorStart = 0;
static const DWORD MEM_MONITOR_DURATION_MS = 15000;  // 15 seconds
static const int MEM_MONITOR_REGION_SIZE = 2048;  // bytes to monitor
static uint8_t  s_memMonitorSnap[2048] = {};
static bool     s_memMonitorSnapValid = false;
static DWORD    s_memMonitorLastPoll = 0;
static int      s_memMonitorChangeCount = 0;

static void StartMemoryMonitor()
{
    s_memMonitorActive = true;
    s_memMonitorStart = GetTickCount();
    s_memMonitorSnapValid = false;
    s_memMonitorChangeCount = 0;
    s_memMonitorLastPoll = 0;
    Log::Write("[MEMMON] === Started memory monitor (15s, 2KB around pMenuStateA) ===");
    Log::Write("[MEMMON] Press LEFT arrow to move cursor to party member panel.");
    ScreenReader::Speak("Memory monitor started. Press left to select party member.", true);
}

static void PollMemoryMonitor()
{
    if (!s_memMonitorActive) return;
    DWORD now = GetTickCount();

    // Check timeout
    if (now - s_memMonitorStart > MEM_MONITOR_DURATION_MS) {
        Log::Write("[MEMMON] === Monitor stopped (%d changes detected) ===", s_memMonitorChangeCount);
        ScreenReader::Speak("Monitor done", true);
        s_memMonitorActive = false;
        return;
    }

    // Poll every 200ms
    if (now - s_memMonitorLastPoll < 200) return;
    s_memMonitorLastPoll = now;

    uint8_t* base = (uint8_t*)pMenuStateA;
    uint8_t cur[2048];
    memcpy(cur, base, MEM_MONITOR_REGION_SIZE);

    if (!s_memMonitorSnapValid) {
        memcpy(s_memMonitorSnap, cur, MEM_MONITOR_REGION_SIZE);
        s_memMonitorSnapValid = true;
        Log::Write("[MEMMON] Initial snapshot taken. Waiting for changes...");
        return;
    }

    // Compare and log changes (skip known noise: rendering ticks, etc.)
    for (int i = 0; i < MEM_MONITOR_REGION_SIZE; i++) {
        if (cur[i] != s_memMonitorSnap[i]) {
            // Skip known noisy offsets: 0x1E6 (menu cursor we already track),
            // 0x1E4-0x1E5 (rendering state), 0x1FD (render tick)
            int off = i;
            if (off == 0x1E6 || off == 0x1E4 || off == 0x1E5 || off == 0x1FD) {
                s_memMonitorSnap[i] = cur[i];
                continue;
            }
            Log::Write("[MEMMON] +0x%03X: %u -> %u  (elapsed=%ums)",
                       off, (unsigned)s_memMonitorSnap[i], (unsigned)cur[i],
                       now - s_memMonitorStart);
            s_memMonitorSnap[i] = cur[i];
            s_memMonitorChangeCount++;
        }
    }

    // Also capture GCW buffer to see help text
    static DWORD s_lastGcwCapture = 0;
    if (now - s_lastGcwCapture >= 1000) {  // once per second
        s_lastGcwCapture = now;
        uint8_t gcwBuf[1024];
        int gcwLen = FieldDialog::SnapshotGcwBuffer(gcwBuf, sizeof(gcwBuf));
        if (gcwLen > 0) {
            std::string decoded = FF8TextDecode::DecodeMenuText(gcwBuf, gcwLen);
            if (!decoded.empty()) {
                Log::Write("[MEMMON] GCW text (%d chars): \"%s\"", gcwLen, decoded.c_str());
            }
        }
    }
}

// ============================================================================
// v0.08.28: Auto submenu cursor discovery monitor
// ============================================================================
// Automatically activates when the player enters a submenu (top-level cursor
// stable for 500ms in mode 6). Monitors 4KB around pMenuStateA for any byte
// changes. When the player exits the submenu (top-level cursor changes),
// logs a summary showing which offsets changed and their value ranges.
// Offsets with small-integer patterns (0-20, changing by 1) are flagged as
// likely cursor candidates.

static const int SUBMON_REGION_SIZE = 4096;
static bool     s_submonActive = false;
static DWORD    s_submonStableSince = 0;     // when top-level cursor last changed
static uint8_t  s_submonSubmenu = 0xFF;       // which submenu we're monitoring
static uint8_t  s_submonSnap[SUBMON_REGION_SIZE] = {};
static bool     s_submonSnapValid = false;
static DWORD    s_submonLastPoll = 0;

// Per-offset tracking: how many times each byte changed, and the min/max values seen
static uint16_t s_submonChangeCount[SUBMON_REGION_SIZE] = {};
static uint8_t  s_submonMinVal[SUBMON_REGION_SIZE] = {};
static uint8_t  s_submonMaxVal[SUBMON_REGION_SIZE] = {};
static uint8_t  s_submonFirstVal[SUBMON_REGION_SIZE] = {};
static int      s_submonTotalPolls = 0;

// Known noisy offsets to ignore (rendering ticks, counters, etc.)
static bool IsSubmonNoiseOffset(int off)
{
    // Top-level cursor (we already track this)
    if (off == 0x1E6) return true;
    // Known rendering noise from v0.08.22 memory monitor
    if (off == 0x1E4 || off == 0x1E5 || off == 0x1E9) return true;
    if (off == 0x1FD) return true;  // render tick
    if (off == 0x1CE || off == 0x1CA) return true;  // timer/countdown
    if (off == 0x1EA) return true;  // animation timer
    if (off >= 0x5E0 && off <= 0x5F8) return true;  // rendering state region
    return false;
}

static void SubmonStart(uint8_t submenuIdx)
{
    s_submonActive = true;
    s_submonSubmenu = submenuIdx;
    s_submonSnapValid = false;
    s_submonLastPoll = 0;
    s_submonTotalPolls = 0;
    memset(s_submonChangeCount, 0, sizeof(s_submonChangeCount));
    memset(s_submonMinVal, 0xFF, sizeof(s_submonMinVal));
    memset(s_submonMaxVal, 0, sizeof(s_submonMaxVal));
    memset(s_submonFirstVal, 0, sizeof(s_submonFirstVal));
    const char* name = GetMenuItemName(submenuIdx);
    Log::Write("[SUBMON] === Started monitoring submenu %u (%s) ===",
               (unsigned)submenuIdx, name ? name : "?");
}

static void SubmonStop()
{
    if (!s_submonActive) return;
    s_submonActive = false;

    // Log summary: which offsets changed, how many times, value range
    const char* name = GetMenuItemName(s_submonSubmenu);
    Log::Write("[SUBMON] === Summary for submenu %u (%s): %d polls ===",
               (unsigned)s_submonSubmenu, name ? name : "?", s_submonTotalPolls);

    // First pass: collect all changed offsets
    int changedCount = 0;
    for (int i = 0; i < SUBMON_REGION_SIZE; i++) {
        if (s_submonChangeCount[i] > 0 && !IsSubmonNoiseOffset(i))
            changedCount++;
    }
    Log::Write("[SUBMON] %d offsets changed (excluding noise)", changedCount);

    // Log each changed offset, flagging likely cursors
    for (int i = 0; i < SUBMON_REGION_SIZE; i++) {
        if (s_submonChangeCount[i] == 0 || IsSubmonNoiseOffset(i)) continue;
        uint8_t minV = s_submonMinVal[i];
        uint8_t maxV = s_submonMaxVal[i];
        uint8_t firstV = s_submonFirstVal[i];
        int range = (int)maxV - (int)minV;
        int changes = s_submonChangeCount[i];

        // Flag as likely cursor if: small value range (0-30), changes > 1
        const char* flag = "";
        if (range >= 1 && range <= 30 && maxV <= 50 && changes >= 2)
            flag = " <<< CURSOR CANDIDATE";
        else if (range >= 1 && range <= 10 && changes >= 2)
            flag = " <<< POSSIBLE CURSOR";

        Log::Write("[SUBMON]   +0x%03X: changes=%d first=%u min=%u max=%u range=%d%s",
                   i, changes, (unsigned)firstV, (unsigned)minV, (unsigned)maxV, range, flag);
    }

    Log::Write("[SUBMON] === End summary ===");
}

static void SubmonPoll()
{
    if (!s_submonActive) return;

    DWORD now = GetTickCount();
    if (now - s_submonLastPoll < 150) return;  // poll every 150ms
    s_submonLastPoll = now;
    s_submonTotalPolls++;

    uint8_t* base = (uint8_t*)pMenuStateA;
    uint8_t cur[SUBMON_REGION_SIZE];
    __try {
        memcpy(cur, base, SUBMON_REGION_SIZE);
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        return;
    }

    if (!s_submonSnapValid) {
        memcpy(s_submonSnap, cur, SUBMON_REGION_SIZE);
        // Initialize first/min/max from initial snapshot
        for (int i = 0; i < SUBMON_REGION_SIZE; i++) {
            s_submonFirstVal[i] = cur[i];
            s_submonMinVal[i] = cur[i];
            s_submonMaxVal[i] = cur[i];
        }
        s_submonSnapValid = true;
        return;
    }

    // Compare and track changes
    for (int i = 0; i < SUBMON_REGION_SIZE; i++) {
        if (cur[i] != s_submonSnap[i]) {
            s_submonChangeCount[i]++;
            if (cur[i] < s_submonMinVal[i]) s_submonMinVal[i] = cur[i];
            if (cur[i] > s_submonMaxVal[i]) s_submonMaxVal[i] = cur[i];

            // Log individual changes for non-noise offsets (first 200 only to limit spam)
            if (!IsSubmonNoiseOffset(i) && s_submonChangeCount[i] <= 5) {
                Log::Write("[SUBMON] +0x%03X: %u -> %u (change #%d)",
                           i, (unsigned)s_submonSnap[i], (unsigned)cur[i],
                           s_submonChangeCount[i]);
            }
            s_submonSnap[i] = cur[i];
        }
    }
}

// ============================================================================
// v0.08.29: Item submenu TTS — polls phase + cursor, announces actions/items
// ============================================================================
// Called every frame while in menu mode. Detects Item submenu via +0x234==5.
// Phase 0 = action menu (Use/Rearrange/Sort/Battle)
// Phase 1 = item list (reads inventory from savemap)

static void ResetItemSubmenuState()
{
    if (s_itemSubmenuActive) {
        Log::Write("[MenuTTS] Item submenu exited");
    }
    s_itemSubmenuActive = false;
    s_prevItemCursor = 0xFF;
    s_prevActionCursor = 0xFF;
    s_prevFocusState = 0xFF;
    s_pendingActionCursor = 0xFF;
    s_pendingActionTime = 0;
}

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
    Log::Write("[MenuTTS] Item list cursor %u: id=%u qty=%u -> \"%s\"",
               (unsigned)cursor, (unsigned)itemId, (unsigned)itemQty, buf);
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
        uint8_t focusState = *(base + ITEM_FOCUS_STATE_OFFSET);  // 3=action, 5=items
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
            Log::Write("[MenuTTS] Item submenu entered (focus=%u actionCur=%u listCur=%u)",
                       (unsigned)focusState, (unsigned)actionCur, (unsigned)listCur);
        }

        // === FOCUS STATE TRANSITIONS (+0x22E) ===
        // +0x22E transitions through intermediates: 5→2→3 (items→action), 3→4→5 (action→items).
        // We only act on arriving at the stable endpoints (3 or 5), not intermediates.
        if (focusState != s_prevFocusState) {
            Log::Write("[MenuTTS] Item focus: %u -> %u (actionCur=%u listCur=%u)",
                       (unsigned)s_prevFocusState, (unsigned)focusState,
                       (unsigned)actionCur, (unsigned)listCur);

            if (focusState == 3 && s_prevFocusState != 3 && s_prevFocusState != 0xFF) {
                // Arrived at Action menu (from 2, which came from 5): announce action
                if (actionCur < ITEM_ACTION_COUNT) {
                    ScreenReader::Speak(ITEM_ACTION_NAMES[actionCur], true);
                    Log::Write("[MenuTTS] Item action (focus->3) cursor %u: %s",
                               (unsigned)actionCur, ITEM_ACTION_NAMES[actionCur]);
                }
                s_prevActionCursor = actionCur;
                s_pendingActionTime = 0;
            }
            else if (focusState == 5 && s_prevFocusState != 5 && s_prevFocusState != 0xFF) {
                // Arrived at Items list (from 4, which came from 3): announce item
                AnnounceItemAtCursor(listCur);
                s_prevItemCursor = listCur;
            }

            s_prevFocusState = focusState;
        }

        // === ACTION CURSOR: debounced left/right (works at any focus state) ===
        if (focusState >= 3) {
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
                        Log::Write("[MenuTTS] Item action (debounced) cursor %u: %s",
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
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        Log::Write("[MenuTTS] SEH exception in PollItemSubmenu");
    }
}

// ============================================================================
// v0.08.21: Individual info hotkeys (G/T/R/L) for menu mode
// ============================================================================
static void AnnounceGil()
{
    uint32_t gil = *(uint32_t*)((uint8_t*)SAVEMAP_BASE + HDR_GIL);
    char buf[64];
    sprintf(buf, "%u Gil", gil);
    ScreenReader::Speak(buf, true);
    Log::Write("[MenuTTS] Gil: %u", gil);
}

static void AnnouncePlayTime()
{
    // v0.08.27: Use live game timer at +0x0CCC instead of stale header at +0x0C.
    // Header time only syncs at save/load; live timer ticks every second.
    static const int LIVE_TIME_OFFSET = 0x0CCC;
    uint32_t timeSec = *(uint32_t*)((uint8_t*)SAVEMAP_BASE + LIVE_TIME_OFFSET);
    int hours = timeSec / 3600;
    int mins  = (timeSec % 3600) / 60;
    int secs  = timeSec % 60;
    char buf[64];
    if (hours > 0)
        sprintf(buf, "Play time: %d hours, %d minutes, %d seconds", hours, mins, secs);
    else
        sprintf(buf, "Play time: %d minutes, %d seconds", mins, secs);
    ScreenReader::Speak(buf, true);
    Log::Write("[MenuTTS] Time: %u sec (%d:%02d:%02d)", timeSec, hours, mins, secs);
}

static void AnnounceLocation()
{
    uint16_t locId = *(uint16_t*)((uint8_t*)SAVEMAP_BASE + HDR_LOCATION_ID);
    const char* locName = GetLocationNameById(locId);
    char buf[128];
    if (locName)
        sprintf(buf, "%s", locName);
    else
        sprintf(buf, "Location %u", (unsigned)locId);
    ScreenReader::Speak(buf, true);
    Log::Write("[MenuTTS] Location: %s (id=%u)", buf, (unsigned)locId);
}

static void AnnounceSeedRank()
{
    // SeeD rank is stored in field_h section of savemap.
    // field_h starts after: header(0x4C) + GFs(0x440) + chars(8*0x98=0x4C0)
    //   + shops(16*20=0x140) + limit_breaks(0x14) + items(0x198)
    // = 0x4C + 0x440 + 0x4C0 + 0x140 + 0x14 + 0x198 = 0xF94
    // field_h.seedExp is at field_h + 0x08 (uint16)
    // SeeD level = seedExp / 100 (approximate, game uses a lookup table)
    // For now, just report the raw seedExp value and compute approximate level.
    static const int FIELD_H_OFFSET = 0xF94;  // from savemap base
    static const int SEED_EXP_IN_FIELD_H = 0x08;  // uint16 within field_h
    uint16_t seedExp = *(uint16_t*)((uint8_t*)SAVEMAP_BASE + FIELD_H_OFFSET + SEED_EXP_IN_FIELD_H);
    if (seedExp == 0) {
        ScreenReader::Speak("No SeeD rank yet", true);
        Log::Write("[MenuTTS] SeeD: no rank (seedExp=0)");
    } else {
        // SeeD level is roughly seedExp / 100, clamped 1-31
        int seedLvl = seedExp / 100;
        if (seedLvl < 1) seedLvl = 1;
        if (seedLvl > 31) seedLvl = 31;
        char buf[64];
        sprintf(buf, "SeeD Level %d", seedLvl);
        ScreenReader::Speak(buf, true);
        Log::Write("[MenuTTS] SeeD: level %d (seedExp=%u)", seedLvl, (unsigned)seedExp);
    }
}

// ============================================================================
// Update — called every frame from AccessibilityThread
// ============================================================================
void MenuTTS::Update()
{
    if (!s_initialized) return;
    if (pGameMode == nullptr || pMenuStateA == nullptr) return;
    
    uint16_t mode = *pGameMode;
    bool isMenuMode = (mode == 6);
    
    // ========================================================================
    // GLOBAL MODE TRACKING — fires every frame regardless of isMenuMode
    // ========================================================================
    if (mode != s_prevGameMode) {
        Log::Write("[MenuTTS] === GAME MODE CHANGE: %u -> %u ===",
                   (unsigned)s_prevGameMode, (unsigned)mode);
        s_prevGameMode = mode;
    }
    
    // ========================================================================
    // F12 DIAGNOSTIC: Capture GCW (get_character_width) text during save screen
    // Snapshots the accumulated character codes every 500ms, decodes them,
    // and logs the resulting text. This shows exactly what the save screen renders.
    // ========================================================================
    if (GetAsyncKeyState(VK_F12) & 1) {
        if (GetAsyncKeyState(VK_CONTROL) & 0x8000) {
            // Ctrl+F12: Savemap offset verification diagnostic
            ScreenReader::Speak("Savemap offset check", true);
            VerifySavemapOffsets();
            ScreenReader::Speak("Done, check log", true);
        } else {
            // F12: Save header memory scan diagnostic
            ScreenReader::Speak("Scanning for save headers", true);
            ScanForSaveHeaders();
            ScreenReader::Speak("Scan complete, check log", true);
        }
    }

    // v0.08.21: Menu mode hotkeys
    if (isMenuMode) {
        // F11 = full menu summary
        // Shift+F11 = start memory monitor (15s, tracks left-panel cursor)
        // Ctrl+F11 = diagnostic dump
        if (GetAsyncKeyState(VK_F11) & 1) {
            if (GetAsyncKeyState(VK_SHIFT) & 0x8000) {
                StartMemoryMonitor();
            } else if (GetAsyncKeyState(VK_CONTROL) & 0x8000) {
                ScreenReader::Speak("Menu data scan", true);
                DumpMenuScreenData();
                ScreenReader::Speak("Done, check log", true);
            } else {
                AnnounceMenuSummary();
            }
        }
        // Poll memory monitor if active
        PollMemoryMonitor();
        // G = Gil
        if (GetAsyncKeyState('G') & 1) {
            AnnounceGil();
        }
        // T = Play time (TODO: countdown timer check)
        if (GetAsyncKeyState('T') & 1) {
            AnnouncePlayTime();
        }
        // L = Location
        if (GetAsyncKeyState('L') & 1) {
            AnnounceLocation();
        }
        // R = SeeD Rank
        if (GetAsyncKeyState('R') & 1) {
            AnnounceSeedRank();
        }
    }
    
    // Periodic GCW text capture
    if (s_diagActive) {
        DWORD now = GetTickCount();
        
        if (now - s_diagLastLogTime >= 500 && s_diagScanCount < DIAG_SCAN_MAX) {
            s_diagLastLogTime = now;
            s_diagScanCount++;
            
            // Snapshot call counts
            LONG curMDT = FieldDialog::GetMenuDrawTextCallCount();
            LONG curGCW = FieldDialog::GetGetCharWidthCallCount();
            LONG deltaMDT = curMDT - s_diagPrevMDT;
            LONG deltaGCW = curGCW - s_diagPrevGCW;
            s_diagPrevMDT = curMDT;
            s_diagPrevGCW = curGCW;
            
            // Snapshot and decode the GCW buffer
            uint8_t gcwBuf[1024];
            int gcwLen = FieldDialog::SnapshotGcwBuffer(gcwBuf, sizeof(gcwBuf));
            
            if (gcwLen > 0) {
                // Decode the MENU font glyph indices to UTF-8 (not field dialog encoding)
                std::string decoded = FF8TextDecode::DecodeMenuText(gcwBuf, gcwLen);
                
                // Also log raw hex of first 64 bytes for analysis
                char hexDump[200] = {};
                int hexPos = 0;
                int hexMax = (gcwLen < 64) ? gcwLen : 64;
                for (int i = 0; i < hexMax && hexPos < 190; i++) {
                    hexPos += sprintf(hexDump + hexPos, "%02X ", gcwBuf[i]);
                }
                
                Log::Write("[MenuTTS] Scan %d: MDT+%ld GCW+%ld gcwLen=%d",
                           s_diagScanCount, deltaMDT, deltaGCW, gcwLen);
                Log::Write("[MenuTTS]   hex: %s%s",
                           hexDump, (gcwLen > 64) ? "..." : "");
                Log::Write("[MenuTTS]   text: \"%s\"", decoded.c_str());
            } else if (s_diagScanCount <= 3) {
                Log::Write("[MenuTTS] Scan %d: MDT+%ld GCW+%ld gcwLen=0 (no chars)",
                           s_diagScanCount, deltaMDT, deltaGCW);
            }
            
            if (s_diagScanCount >= DIAG_SCAN_MAX) {
                Log::Write("[MenuTTS] GCW text capture complete (%d scans)",
                           s_diagScanCount);
                s_diagActive = false;
                ScreenReader::Speak("Text capture complete", true);
            }
        }
    }
    
    // ========================================================================
    // MENU MODE TTS — only active during mode 6
    // ========================================================================
    
    // Detect entering menu mode
    if (isMenuMode && !s_wasMenuMode) {
        s_prevCursor = 0xFF;
        s_submonStableSince = 0;  // v0.08.28: reset submenu monitor
        s_submonActive = false;
        ResetItemSubmenuState();  // v0.08.29
        Log::Write("[MenuTTS] Menu opened (mode 6)");
        

        // If save screen was active and we re-entered the menu, deactivate it
        if (s_saveScreenActive) {
            Log::Write("[MenuTTS] Save screen exited (re-entered mode 6)");
            s_saveScreenActive = false;
            s_saveCursorSlot = -1;
            s_saveGcwZeroCount = 0;
            s_prevSaveSlotCursor = 0xFF;
        }
    }
    
    // Detect exiting menu mode
    if (!isMenuMode && s_wasMenuMode) {
        Log::Write("[MenuTTS] Menu closed (left mode 6), last cursor=%u", (unsigned)s_prevCursor);
        // v0.08.28: Stop submenu monitor on menu exit
        if (s_submonActive) SubmonStop();
        s_submonStableSince = 0;
        ResetItemSubmenuState();  // v0.08.29
        
        // If exiting menu with cursor on Save, activate mode 1 save detection
        // but DON'T reset s_prevSaveSlotCursor here — let PollSaveScreen
        // handle the first announcement via GCW detection
        if (s_prevCursor == 10 && mode == MODE_FIELD) {
            s_saveScreenActive = true;
            // Don't reset s_prevSaveSlotCursor — avoid phantom announcement
            // GCW detection will reset it when save screen text appears
            Log::Write("[MenuTTS] Save screen pre-activated (menu->Save, mode 1)");
        }
    }
    
    // While in menu mode: poll cursor and announce changes
    if (isMenuMode) {
        // Suppress menu cursor during save screen transitions (grace period)
        bool inSaveGrace = (s_saveSubsystemExitTime != 0 &&
                            (GetTickCount() - s_saveSubsystemExitTime) < 2000);
        if (!inSaveGrace) {
            // v0.08.28: Track cursor changes for auto submenu monitor
            uint8_t prevCursorBeforePoll = s_prevCursor;
            PollMenuCursor();
            CaptureMenuGcwText();  // v0.08.22: log rendered help text

            // v0.08.28: Auto submenu cursor discovery
            if (s_prevCursor != prevCursorBeforePoll) {
                // Cursor just changed
                if (prevCursorBeforePoll != 0xFF && s_submonActive) {
                    // Real cursor movement while monitor active — stop and summarize
                    SubmonStop();
                }
                // Record when cursor stabilized (including initial 0xFF->N assignment)
                s_submonStableSince = GetTickCount();
            }
            // If cursor has been stable for 500ms, start monitoring
            if (!s_submonActive && s_submonStableSince > 0 &&
                s_prevCursor < MENU_ITEMS_COUNT &&
                (GetTickCount() - s_submonStableSince) > 500) {
                SubmonStart(s_prevCursor);
                s_submonStableSince = 0;  // don't re-trigger
            }
            // Poll the monitor
            SubmonPoll();

            // v0.08.29: Item submenu TTS
            PollItemSubmenu();
        }
        
        // v0.07.40: Poll save slot cursor in mode 6 using +0x276
        // Only when the save subsystem is actually active (+0x1E8 == 6)
        {
            bool subsysActive = (s_prevCursor == 10 && IsSaveSubsystemActive());
            
            if (subsysActive && !s_saveSubsystemWasActive) {
                // Just became active (first open, or returned from block list)
                s_prevSaveSlotCursor = 0xFF;  // force re-announce
                s_prevSavePhase = 0;           // reset phase tracking
                s_saveDiagSnapValid = false;   // reset diagnostic snapshot
                s_saveSubsystemExitTime = 0;   // clear grace period
                Log::Write("[MenuTTS] Save subsystem became active, reset for announce");
            }
            if (!subsysActive && s_saveSubsystemWasActive) {
                s_saveSubsystemExitTime = GetTickCount();  // start grace period
                Log::Write("[MenuTTS] Save subsystem became inactive (block selection?)");
            }
            s_saveSubsystemWasActive = subsysActive;
            
            if (subsysActive) {
                // v0.07.44: Track +0x266 phase to detect block->slot transition
                if (CheckSavePhaseTransition(SAVE_PHASE_OFFSET_MODE6)) {
                    s_prevSaveSlotCursor = 0xFF;  // force re-announce
                }
                
                PollSaveSlotCursor(SAVE_SLOT_CURSOR_OFFSET_MODE6, true);
                // v0.07.48: Poll block cursor during block selection
                if (s_prevSavePhase >= 2) {
                    PollBlockCursor(BLOCK_CURSOR_OFFSET_MODE6);
                } else {
                    s_prevBlockCursor = 0xFF;  // reset when back on slot list
                }
                // Diagnostic: log key offset changes while subsystem active
                static DWORD s_lastSubsysDiag = 0;
                DWORD diagNow = GetTickCount();
                if (diagNow - s_lastSubsysDiag >= 200) {
                    s_lastSubsysDiag = diagNow;
                    LogSaveSubsystemChanges();
                }
            } else if (s_prevCursor == 10) {
                // Cursor on Save but subsystem not active (e.g. in block list)
                // Log what +0x1E8 is for diagnostic
                static DWORD s_lastDiagTime = 0;
                DWORD now = GetTickCount();
                if (now - s_lastDiagTime >= 500) {
                    s_lastDiagTime = now;
                    LogSaveDiagState();
                }
            } else if (s_prevSaveSlotCursor != 0xFF) {
                s_prevSaveSlotCursor = 0xFF;
            }
        }
    }
    
    // ========================================================================
    // SAVE SCREEN DETECTION — runs outside mode 6 (save screen is mode 1)
    // ========================================================================
    if (!isMenuMode) {
        PollSaveScreen();
    }
    
    s_wasMenuMode = isMenuMode;
}
