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
#include "ff8_addresses.h"
#include "menu_tts.h"
#include "field_dialog.h"
#include "ff8_text_decode.h"
#include "field_archive.h"
#include <cstdio>
#include <cstring>
#include <string>
#include <tlhelp32.h>   // v0.18.2.5 diag: thread enumeration for the HW write BP

// Forward declarations for namespaces used in .inl files
namespace Log { void Menu(const char* format, ...); }
namespace ScreenReader { bool Speak(const char* text, bool interrupt = false); bool IsSpeaking(); }
namespace Config { void Load(); int GetInt(const char* key, int defaultValue); void SetInt(const char* key, int value); const char* GetPath(); }
// v0.25.1 (#84): defined in src/button_map_rescue.inl, which dinput8.cpp owns.
// The Config screen's Controller row reports whether FF8's own 12-button map is
// still stock, so it can say "Buttons have been remapped. Press Alt K" even when
// the row itself reads Normal.
namespace ButtonMapRescue { bool IsDefault(); }

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

// v0.08.62: Sub-flow offsets discovered via SUBMON during test session
static const int ITEM_TARGET_CURSOR_OFFSET       = 0x276; // party member cursor during Use target selection
static const int BATTLE_ITEM_CURSOR_OFFSET        = 0x285; // cursor in Battle item arrangement sub-screen
static const int PARTY_INDICES_OFFSET              = 0xAF1; // savemap offset: 3 bytes, char index 0-7 or 0xFF

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

// v0.08.62: Item sub-flow state tracking
// Use target: focus==14, +0x276 party cursor. Rearrange: focus~97, +0x272. Sort: focus flash 79. Battle: focus~30, +0x285.
static uint8_t  s_prevTargetCursor = 0xFF;          // v0.30.0: the target SLOT (bit index), not a packed position
static bool     s_prevTargetIsGF   = false;         // v0.30.0: which half of the target mask was showing
static uint8_t  s_prevTargetCharIdx = 0xFF;         // v0.18.2.7 (#10): char whose HP the Use-target poll is tracking
static uint16_t s_prevTargetHP = 0xFFFF;            // v0.18.2.7 (#10): last announced Use-target curHP (re-announce on change)
static uint8_t  s_prevBattleItemCursor = 0xFF;      // tracks +0x285 during Battle items
static bool     s_inUseTargetMode = false;           // true when focus==14 (Use target selection)
static bool     s_inRearrangeMode = false;           // true when focus stabilized ~97
static bool     s_inBattleMode = false;              // true when focus stabilized ~30
static bool     s_inBattleDestMode = false;           // v0.08.68: true when in battle dest (focus==36)
static uint8_t  s_battleSwapSrcPos = 0xFF;             // v0.08.77: source cursor when entering battle dest
static uint8_t  s_rearrangePrevFocus = 0;            // for detecting swap completion (99->97)
static bool     s_rearDestDiagValid = false;           // diagnostic: snap valid for rearrange destination
static uint8_t  s_rearDestDiagSnap[32] = {};            // diagnostic: snapshot for rearrange destination
static bool     s_batDestDiagValid = false;             // diagnostic: snap valid for battle destination
static uint8_t  s_batDestDiagSnap[32] = {};              // diagnostic: snapshot for battle destination
// v0.08.84: s_battleOrderLoaded, s_snapValid, s_battleOrderLivePtr removed.
// Battle TTS now reads from display struct at 0x1D8DFF4 directly.

// ============================================================================
// v0.08.89: Junction submenu TTS — character select + action menu
// Phase 2: Character select (focus==0, +0x1E8==17, cursor at +0x1E9)
// Phase 3: Action menu (focus==3, cursor at +0x26C: 0=Junction,1=Off,2=Auto,3=Ability)
// ============================================================================
static const int JUNC_FOCUS_OFFSET        = 0x22E;  // focus state indicator
static const int JUNC_ACTIVE_OFFSET       = 0x1E8;  // 17=Junction active, 255=inactive
// v0.24.0 (#83): the Card album's dispatch index. The pair at 0x00B87ED8 + 7*8
// is {creator 0x004EF020, overlay 12 = menucrd.ovl}, and the main menu's own
// {textEntry, action} table at 0x00B87FE0 pairs text entry 6 ("Card" / "Look at
// cards") with action byte 7 -- two independent confirmations, which this needed
// because indices 20/22/30 ALSO use text group 13 and look like the card menu
// until you notice their overlay is menututo.ovl and their group 13 is a
// different SECTION.
static const int CARD_SUBSYSTEM_ID        = 7;
// v0.25.0 (#84): Config. Confirmed the same two ways Card was -- the main menu's
// {textEntry, action} table at 0x00B87FE0 pairs text entry 9 ("Config" /
// "Configuration Menu") with action byte 8, and dispatch index 8's overlay is 1
// = menucfg.ovl, which no other index uses.
static const int CONFIG_SUBSYSTEM_ID      = 8;
// v0.26.0 (#85): Tutorial, and the SeeD exam it launches. The main menu's
// {textEntry, action} table at 0x00B87FE0 -- stride 2, 0xFF-terminated, eleven
// rows -- pairs text entry 0x37 with action byte 0x14, and 0x14 & 0x1F = 20;
// dispatch index 20's overlay is 13 = menututo.ovl. **The exam is NOT a state of
// that module.** Tutorial state 17 PUSHES dispatch 23 (creator 0x004D4960,
// overlay 16 = menutest.ovl) and then parks in state 18 until it pops, so both
// modules sit in the pool together and the poll has to prefer the exam.
static const int TUTORIAL_SUBSYSTEM_ID    = 20;
static const int SEEDTEST_SUBSYSTEM_ID    = 23;
// v0.27.0 (#86): the Tutorial's other four rows. Battle Operation, Card Game
// Rules and Icon Explanation are three dispatch ids sharing ONE module
// (0x004C9060) -- their creators are identical and only the record range the
// Tutorial preloads tells them apart. Information is the menutips page browser.
static const int MAGAZINE_SUBSYSTEM_A     = 25;   // Battle Operation
static const int MAGAZINE_SUBSYSTEM_B     = 26;   // Card Game Rules
static const int MAGAZINE_SUBSYSTEM_C     = 31;   // Icon Explanation
static const int TIPS_SUBSYSTEM_ID        = 21;   // Information (v0.28.0)
static const int JUNC_CHARSEL_CURSOR_OFF  = 0x1E9;  // character select cursor (party formation index)
static const int JUNC_ACTION_CURSOR_OFF   = 0x26C;  // action menu cursor (0-3)

static const char* JUNC_ACTION_NAMES[] = { "Junction", "Off", "Auto", "Ability" };
static const int JUNC_ACTION_COUNT = 4;

// v0.08.95: Junction sub-option and GF list offsets
static const int JUNC_SUBOPTION_CURSOR_OFF = 0x268;  // 0=GF, 1=Magic (focus 37/38)
static const int JUNC_GF_LIST_CURSOR_OFF   = 0x26D;  // GF list cursor (focus 41), 0-based GF index
static const int JUNC_GF_TOGGLE_OFF        = 0x27F;  // GF junction toggle (0=unjunctioned, 1=junctioned)

static const char* JUNC_SUBOPTION_NAMES[] = { "GF", "Magic" };

#include "menu_ability_names.inl"

static const char* GetAbilityName(uint8_t id)
{
    if (id < ABILITY_NAME_COUNT) return ABILITY_NAMES[id];
    return "Unknown";
}

// Helper: Decode GCW buffer to a char array (isolates std::string from __try functions)

// --- GCW decoder, junction TTS (extracted v0.12.18) ---
// --- The shared yes/no window (#88, v0.29.0). Every confirmation dialog in the
//     main menu goes through one opener and stores its text in three globals;
//     this reads them, so no screen has to guess at the wording or the order of
//     the options. Must precede every screen that puts one up. ---
#include "menu_dialog.inl"

#include "menu_tts_junction.inl"

// --- Save screen TTS (extracted v0.12.18) ---
#include "menu_tts_save.inl"

void MenuTTS::Initialize()
{
    Log::Menu("[MenuTTS] Initialize() — v0.09.21 GF-ACQ via MENUNAME, suppress char on GF");
    
    if (pMenuStateA == nullptr) {
        Log::Menu("[MenuTTS] WARNING: pMenuStateA not resolved, menu TTS disabled");
        return;
    }
    
    Log::Menu("[MenuTTS] Menu cursor at pMenuStateA + 0x%X = absolute 0x%08X",
               CURSOR_OFFSET, (uint32_t)(uintptr_t)pMenuStateA + CURSOR_OFFSET);
    Log::Menu("[MenuTTS] Save slot cursor mode1 at pMenuStateA + 0x%X = absolute 0x%08X",
               SAVE_SLOT_CURSOR_OFFSET_MODE1, (uint32_t)(uintptr_t)pMenuStateA + SAVE_SLOT_CURSOR_OFFSET_MODE1);
    Log::Menu("[MenuTTS] Save slot cursor mode6 at pMenuStateA + 0x%X = absolute 0x%08X",
               SAVE_SLOT_CURSOR_OFFSET_MODE6, (uint32_t)(uintptr_t)pMenuStateA + SAVE_SLOT_CURSOR_OFFSET_MODE6);
    
    
    s_initialized = true;
    Log::Menu("[MenuTTS] Initialize() complete");
}

// ============================================================================
// Helper: poll menu cursor with SEH protection (separate function to avoid
// C2712 — __try can't coexist with C++ object unwinding in same function)
// ============================================================================

// --- Menu diagnostics, memory monitor, SUBMON (extracted v0.12.18) ---
#include "menu_tts_diagnostics.inl"

// --- Item submenu TTS (extracted v0.12.18) ---
// v0.30.0 (#89): the Item screen's Use-target list can be GFs, and it names them
// from the savemap the same way the GF screen does. menu_tts_gf.inl is included
// further down this same translation unit, so a declaration is all that is
// needed -- lint_xtu's rule is about crossing TUs, and this does not.
static void DecodeGFName(int idx, char* out, int outSize);

#include "menu_submon.inl"
#include "menu_item_swap_model.inl"
#include "menu_tts_item.inl"

// --- Help bar + hotkeys (extracted v0.12.18) ---
#include "menu_tts_hotkeys.inl"

// --- Main-menu GF screen TTS / discovery (#41, v0.18.0) ---
#include "menu_tts_gf.inl"

// --- Magic submenu: the pure announcement logic, shared verbatim with
//     tests/menu_sim.cpp (#81, v0.22.0). Must precede menu_tts_ability.inl,
//     which now uses this file's MAGIC_SPELL_NAMES instead of its own partial
//     copy -- that copy was correct to id 37 and wrong from 38 on. ---
#include "menu_magic_model.inl"

// --- Junction screen announcement logic (#82, v0.23.0). Pure functions of a
//     JunctionView, shared verbatim with tests/menu_sim.cpp. Needs the magic
//     model above for MagicSpellName / MagicSlotView. ---
#include "menu_junction_model.inl"

// --- The refine screen's shape, from its own state machine (#91, v0.33.0) ---
#include "menu_refine_model.inl"
#include "menu_card_data.inl"   // CARD_DEFS[110] -- Card Mod names its rows from it

// --- Main-menu Ability screen TTS (#42, v0.18.1) ---
#include "menu_tts_ability.inl"

// --- Item shops and Junk Shops (#92, v0.34.0) ---
// After the ability file on purpose: the Junk Shop's character picker indexes
// the SET BITS of a mask exactly as the refine screen's does, and reuses
// AbilCharAtPickerRow() and REFINE_CHAR_NAMES rather than growing a second copy
// of a thing this project has already got wrong once.
#include "menu_shop_model.inl"
#include "menu_tts_shop.inl"

// --- Status screen limit-break page TTS (#49, v0.18.2.27) ---
#include "menu_tts_status.inl"

// --- Switch submenu TTS (#65, v0.18.3.35) ---
#include "menu_tts_switch.inl"

// --- Magic submenu TTS (#81, v0.22.0). After junction.inl (roster + the
//     dream-party name rule) and after the model above. ---
#include "menu_tts_magic.inl"

// --- Junction grid / magic-choice / ability readouts (#82, v0.23.0). After
//     the model (wording), after junction.inl (GetJuncSelectedCharIdx and the
//     JUNC_* offsets) and after magic.inl (MagicCharName, MagicTextToGlyphs). ---
#include "menu_tts_junction_stats.inl"

// --- Card album (#83, v0.24.0). Model first (wording, shared with menu_sim),
//     then the memory layer. After magic.inl for the module-pool constants. ---
#include "menu_card_model.inl"
#include "menu_tts_card.inl"

// --- Config screen (#84, v0.25.0). ---
#include "menu_config_model.inl"
#include "menu_tts_config.inl"

// --- Tutorial menu and the SeeD written exam (#85, v0.26.0). Model first, then
//     the memory layer, same as every screen since Magic. ---
#include "menu_magazine_art.inl"
#include "menu_tutorial_model.inl"
#include "menu_tts_tutorial.inl"

void MenuTTS::Update()
{
    if (!s_initialized) return;
    if (pGameMode == nullptr || pMenuStateA == nullptr) return;

    // #66: forced party-select (game mode 10 — Rinoa join, etc.). Self-gates on mode; runs before the menu-mode gate.
    PollForcedPartySelect();

#if FORCED_PSEL_DIAG
    PollForcedPselDiag();   // forced party-select discovery probe (now off; gate, don't delete)
#endif
    
    uint16_t mode = *pGameMode;
    bool isMenuMode = (mode == 6);
    
    // ========================================================================
    // GLOBAL MODE TRACKING — fires every frame regardless of isMenuMode
    // ========================================================================
    if (mode != s_prevGameMode) {
        Log::Menu("[MenuTTS] === GAME MODE CHANGE: %u -> %u ===",
                   (unsigned)s_prevGameMode, (unsigned)mode);
        s_prevGameMode = mode;
    }
    
    // v0.09.19: GF acquisition detection moved to field_dialog.cpp Hook_opcode_menuname
    // (snapshot before/after original handler call — zero polling cost)
    
    // F12 reserved for per-session diagnostic builds (set up in battle_tts.cpp or other modules).
    // Old save header scan + savemap offset check removed v0.10.72.

    // v0.08.21: Menu mode hotkeys
    if (isMenuMode) {
        // v0.14.75: M = full menu summary (party / Gil / play time / location).
        // Moved from F11 to free F11 for the global on-demand screenshot trigger
        // in dinput8.cpp. The Shift+F11 (StartMemoryMonitor) and Ctrl+F11
        // (DumpMenuScreenData) bindings were research diagnostics for cursor /
        // savemap-offset discovery; both investigations are closed and the
        // diagnostics are dormant. Removed in v0.14.75 along with the F11
        // reassignment. The PollMemoryMonitor() call that paired with
        // StartMemoryMonitor was also removed here — with no caller of
        // StartMemoryMonitor left, the monitor can never activate, so polling
        // it every frame was just a no-op. The Start/Poll/StartMemoryMonitor
        // function definitions in menu_tts_diagnostics.inl remain in place
        // (harmless dead code) in case a future investigation needs them.
        // M was confirmed free across all source files via dryRun probes
        // before binding here.
        if (GetAsyncKeyState('M') & 1) {
            AnnounceMenuSummary();
        }
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
        // / = Read help bar text (v0.09.41). On the GF ability-to-learn list,
        // re-read the help of the ability under the cursor instead (#3); the
        // helper returns false off that list so the normal help bar still works.
        if (GetAsyncKeyState(VK_OEM_2) & 1) {
            // v0.31.0 (#90): on a Weapons Monthly page, "/" describes the
            // picture. Claimed FIRST because the magazine covers the screen --
            // nothing underneath it has a help bar to read.
            if (!MagazineSpeakArt() && !GFSpeakSelectedAbilityHelp() &&
                !AbilitySpeakSelectedHelp() && !JunctionAutoSpeakHelp() &&
                !StatusLimitSpeakSelectedHelp())
                AnnounceHelpText();
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
                
                Log::Menu("[MenuTTS] Scan %d: MDT+%ld GCW+%ld gcwLen=%d",
                           s_diagScanCount, deltaMDT, deltaGCW, gcwLen);
                Log::Menu("[MenuTTS]   hex: %s%s",
                           hexDump, (gcwLen > 64) ? "..." : "");
                Log::Menu("[MenuTTS]   text: \"%s\"", decoded.c_str());
            } else if (s_diagScanCount <= 3) {
                Log::Menu("[MenuTTS] Scan %d: MDT+%ld GCW+%ld gcwLen=0 (no chars)",
                           s_diagScanCount, deltaMDT, deltaGCW);
            }
            
            if (s_diagScanCount >= DIAG_SCAN_MAX) {
                Log::Menu("[MenuTTS] GCW text capture complete (%d scans)",
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
        ResetGFSubmenuState();    // v0.18.0 (#41)
        ResetAbilitySubmenuState(); // v0.18.1 (#42)
        Log::Menu("[MenuTTS] Menu opened (mode 6)");
        

        // If save screen was active and we re-entered the menu, deactivate it
        if (s_saveScreenActive) {
            Log::Menu("[MenuTTS] Save screen exited (re-entered mode 6)");
            s_saveScreenActive = false;
            s_saveCursorSlot = -1;
            s_saveGcwZeroCount = 0;
            s_prevSaveSlotCursor = 0xFF;
        }
    }
    
    // Detect exiting menu mode
    if (!isMenuMode && s_wasMenuMode) {
        Log::Menu("[MenuTTS] Menu closed (left mode 6), last cursor=%u", (unsigned)s_prevCursor);
        // v0.08.28: Stop submenu monitor on menu exit
        if (s_submonActive) SubmonStop();
        s_submonStableSince = 0;
        ResetItemSubmenuState();  // v0.08.29
        if (s_juncActive) ResetJunctionState();  // v0.08.89
        
        // If exiting menu with cursor on Save, activate mode 1 save detection
        // but DON'T reset s_prevSaveSlotCursor here — let PollSaveScreen
        // handle the first announcement via GCW detection
        if (s_prevCursor == 10 && mode == MODE_FIELD) {
            s_saveScreenActive = true;
            // Don't reset s_prevSaveSlotCursor — avoid phantom announcement
            // GCW detection will reset it when save screen text appears
            Log::Menu("[MenuTTS] Save screen pre-activated (menu->Save, mode 1)");
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

            // v0.08.28: Auto submenu cursor discovery.
            // v0.26.2: gated off -- see SUBMON_DIAG in menu_tts_diagnostics.inl.
            // It is a discovery tool with nothing left to discover in this menu,
            // and it was writing 87% of the BAT log.
#if SUBMON_DIAG
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
#endif // SUBMON_DIAG

            // v0.08.29: Item submenu TTS
            PollItemSubmenu();

            // v0.08.89: Junction submenu TTS
            if (s_prevCursor == 0 && !s_itemSubmenuActive) {
                PollJunctionSubmenu();
                // v0.23.0 (#82): the grid, the magic-choice list with its live
                // stat preview, and the number-key readouts. Self-gating on the
                // state word, and deliberately AFTER PollJunctionSubmenu so the
                // older screens keep first claim on any state they both see.
                PollJunctionStats();
                JunctionNumberKeys();
            } else if (s_prevCursor != 0) {
                if (s_juncActive) ResetJunctionState();
                ResetJunctionStats();
            }

            // v0.24.0 (#83): the Card album. Gated on the subsystem id rather
            // than the top-level cursor, because the album is also reachable
            // from places other than the main menu's Card entry.
            PollCardMenu();
            CardNumberKeys();

            // v0.25.0 (#84): the Config screen. Every value on it is invisible
            // to a reader -- the toggles show their setting only by palette and
            // the bars have no readout at all.
            PollConfigMenu();
            ConfigNumberKeys();

            // v0.26.0 (#85): the Tutorial menu and the SeeD written exam. Aaron
            // singled the exam out: *"we need to make the SeeD Exam Quiz and its
            // questions accessible -- including the symbols it sometimes
            // displays in various questions."* Ten questions, and all ten must
            // be right, which is why the symbols cannot be left unnamed.
            PollTutorialMenu();
            TutorialNumberKeys();

            // v0.18.0 (#41): GF screen TTS / discovery (top-level cursor == 4)
            if (s_prevCursor == 4 && !s_itemSubmenuActive) {
                PollGFSubmenu();
            } else if (s_prevCursor != 4 && s_gfActive) {
                ResetGFSubmenuState();
            }

            // v0.18.1 (#42): Ability screen TTS (top-level cursor == 5)
            if (s_prevCursor == 5 && !s_itemSubmenuActive) {
                PollAbilitySubmenu();
            } else if (s_prevCursor != 5 && s_abilActive) {
                ResetAbilitySubmenuState();
            }

            // v0.18.2.16/.17/.18/.19: main-menu "Rearrange party order". From the
            // bare main menu (+0x1E8==0xFF — any open submenu changes it: junction
            // =17, ability=14, etc.) the cursor can move left onto the party panel to
            // reorder the 3 active members. Region flag +0x1B6: command column =3,
            // party source-select =0x0F (cursor +0x1D6), party dest-select =0x10
            // (cursor +0x1D7). Slots 0/1/2 = roster[0..2]; announce the member at the
            // active cursor (AnnounceJuncCharSelect), with a one-time "Choose
            // destination" cue on entering dest-select.
            // v0.18.2.19: gate on +0x1E8==0xFF, NOT the per-submenu !s_*Active flags —
            // s_itemSubmenuActive/s_gfActive are set merely by hovering the Item/GF
            // command, which blocked the party panel from every command but Junction.
            {
                uint8_t* pmd = (uint8_t*)pMenuStateA;
                bool bareMenu = (pmd[0x1E8] == 0xFF);
                uint8_t b1B6 = pmd[0x1B6];
                int partyMode = 0;            // 0=off, 1=source-select, 2=dest-select
                uint8_t partySlot = 0xFF;
                if (bareMenu && b1B6 == 0x0F)      { partyMode = 1; partySlot = pmd[0x1D6]; }
                else if (bareMenu && b1B6 == 0x10) { partyMode = 2; partySlot = pmd[0x1D7]; }
                static int s_prevPartyMode = 0;
                static uint8_t s_prevPartySlot = 0xFF;
                if (partyMode != 0) {
                    if (partyMode == 2 && s_prevPartyMode != 2) {
                        // Entered destination-select: cue the phase; announce on moves.
                        ScreenReader::Speak("Choose destination", true);
                        s_prevPartySlot = partySlot;
                    } else if (partyMode != s_prevPartyMode || partySlot != s_prevPartySlot) {
                        AnnounceJuncCharSelect(partySlot);
                        s_prevPartySlot = partySlot;
                    }
                    s_prevPartyMode = partyMode;
                } else {
                    if (s_prevPartyMode != 0) {
                        // v0.18.2.18: left the party panel back to the command column.
                        // The top cursor +0x1E6 didn't change while on the party, so
                        // the next PollMenuCursor would stay silent; force it to
                        // re-announce the command under the cursor (0xFF is the same
                        // "announce on next poll" sentinel menu-open uses).
                        s_prevCursor = 0xFF;
                    }
                    s_prevPartyMode = 0;
                    s_prevPartySlot = 0xFF;
                }
            }

            // v0.18.2.21: Magic (top cursor 2) and Status (top cursor 3) character-
            // select. Both reuse Junction's char-select screen — confirmed by the
            // v0.18.2.20 [MagStatDiag] probe: Magic subsystem +0x1E8==3, Status
            // +0x1E8==5, both with focus +0x22E==0 and the char cursor at +0x1E9
            // indexing the roster at +0x1DB (cursor reached 3 = roster[3] = the
            // reserve member, so the range covers reserves too). Announce the member
            // under the cursor via AnnounceJuncCharSelect, exactly like Junction.
            // (The party block above is gated on +0x1E8==0xFF so it never overlaps;
            // Junction's own +0x1E8==17 is excluded here too. Focus gate keeps this to
            // the char-select phase — the per-character spell/status screen has a
            // different focus, so we fall silent there rather than misread +0x1E9.)
            {
                uint8_t* pmd = (uint8_t*)pMenuStateA;
                uint8_t sub = pmd[0x1E8];
                uint8_t msFocus = pmd[0x22E];
                // v0.18.2.27 (#49): Status limit-break page TTS — toggles
                // (Gunblade Auto / Duel-Auto / Renzokuken Indicator) + read-only
                // limit-move list names. Runs whenever the Status subsystem is
                // active; PollStatusLimit internally gates to the limit page.
                if (sub == 5) { PollStatusLimit(); PollStatusDetailPages(); StatusDetailHotkeys(); PollStatusDetailDiag(); PollStatusCalcDiag(); PollStatusPage2Diag(); PollStatusMagScan(); } else { ResetStatusLimit(); ResetStatusDetailPages(); }
                bool magStatCharSel =
                    ((s_prevCursor == 2 && sub == 3) || (s_prevCursor == 3 && sub == 5)) &&
                    (msFocus == 0 || msFocus == 8);
                static bool    s_magStatActive = false;
                static uint8_t s_prevMagStatCursor = 0xFF;
                if (magStatCharSel) {
                    if (!s_magStatActive) {
                        s_magStatActive = true;
                        s_prevMagStatCursor = 0xFF;  // force announce on entry
                        ResetCharSelGroup();         // v0.18.2.22: re-cue active/reserve on entry
                    }
                    uint8_t cur = pmd[0x1E9];
                    if (cur <= 7 && cur != s_prevMagStatCursor) {
                        AnnounceJuncCharSelect(cur, true);
                        s_prevMagStatCursor = cur;
                    }
                } else if (s_magStatActive) {
                    s_magStatActive = false;
                    s_prevMagStatCursor = 0xFF;
                    ResetCharSelGroup();
                }

                // v0.22.0 (#81): everything AFTER a character is chosen. The
                // block above only covers the character-select phase and its
                // comment said as much -- "the per-character spell/status screen
                // has a different focus, so we fall silent there". This is that
                // screen. PollMagicSubmenu re-checks the gate itself and stays
                // quiet unless the Magic module is really open, so it is safe to
                // call whenever the character-select block is not speaking.
                if (!magStatCharSel) PollMagicSubmenu();
                else                 ResetMagicSubmenu();
            }

            // Switch screen TTS (#65/#66): the main-menu Switch (subsystem +0x1E8==10)
            // runs the unified PollSwitchScreen engine (off=0x78). The forced screen
            // (game mode 10) is driven separately by PollForcedPartySelect at the top
            // of Update.
            {
                uint8_t* pmdSw = (uint8_t*)pMenuStateA;
                if (s_prevCursor == 6 && pmdSw[0x1E8] == 10) PollSwitchSubmenu();
                else                                         ResetSwitchSubmenu();
            }
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
                Log::Menu("[MenuTTS] Save subsystem became active, reset for announce");
            }
            if (!subsysActive && s_saveSubsystemWasActive) {
                s_saveSubsystemExitTime = GetTickCount();  // start grace period
                Log::Menu("[MenuTTS] Save subsystem became inactive (block selection?)");
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

    // v0.29.0 (#88): the save module's two confirmation dialogs. Called from
    // OUTSIDE both the mode-1 and mode-6 blocks on purpose -- it finds the
    // module by walking the pool, so it does not care which path opened the
    // save screen, and it is silent whenever that module is not in the pool.
    // Before this, "Data exists.  Overwrite?" was announced as nothing at all
    // on either path, and its cursor defaults to No.
    PollSaveConfirmDialog();

    // v0.34.0 (#92): item shops and Junk Shops. Also OUTSIDE both mode blocks,
    // and for a stronger reason than the save dialog: **a shop is opened from
    // the FIELD, not from the main menu**, so gating this on mode 6 would have
    // left it silent in the only place it is ever used. Both readers find their
    // module by walking the pool and say nothing when it is not there.
    PollShops((int)mode);

    // "/" inside a shop: the item's description, or the weapon's material list
    // in a Junk Shop. The menu-mode "/" chain above cannot serve this -- it only
    // runs in mode 6 -- so the key is bound again here and claimed only while a
    // shop module is actually open.
    if (GetAsyncKeyState(VK_OEM_2) & 1) ShopSpeakDetail();

    s_wasMenuMode = isMenuMode;
}
