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
// State tracking
// ============================================================================
static bool     s_initialized = false;
static bool     s_wasMenuMode = false;
static uint8_t  s_prevCursor = 0xFF;

// Global mode tracking
static uint16_t s_prevGameMode = 0xFFFF;

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
    Log::Write("[MenuTTS] Initialize() — v0.07.63 SETPLACE location display names in save block TTS");
    
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
        // F12: Save header memory scan diagnostic
        ScreenReader::Speak("Scanning for save headers", true);
        ScanForSaveHeaders();
        ScreenReader::Speak("Scan complete, check log", true);
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
            PollMenuCursor();
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
