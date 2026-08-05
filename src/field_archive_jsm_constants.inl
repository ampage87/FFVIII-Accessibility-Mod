// field_archive_jsm_constants.inl — JSM opcode IDs + entity-type name table.
// Included from field_archive_jsm.inl. Do not compile independently.

// ============================================================================
// JSM script scanner — entity classification by opcode signatures
// ============================================================================
//
// JSM bytecode format (32-bit fixed-width stack machine):
//   Each instruction is 4 bytes (uint32).
//   Bit 31 = 0: PSHN_L (push literal), value = bits 0-30.
//   Bit 31 = 1: opcode, opcode_id = bits 16-30, inline_param = bits 0-15.
//
// JSM file layout:
//   Bytes 0-3:  entity count bytes (byte0, byte1=doors, byte2=lines, byte3=backgrounds)
//   Bytes 4-5:  offsetScriptEntryPoints (uint16, byte offset from file start)
//   Bytes 6-7:  offsetScriptData (uint16, byte offset from file start)
//   Bytes 8 to offsetScriptEntryPoints-1: entity group table (2 bytes per entity)
//     Each entry: bit15 = class flag (set for Door/Line/Bg), bits 0-14 = method count
//   offsetScriptEntryPoints to offsetScriptData-1: script entry point table
//     Each entry: uint16 = dword index into script data section
//   offsetScriptData to EOF: script instructions (4 bytes each)
//
// Entity ordering in the table: Door → Line → Background → Other.

// JSM opcode IDs (from FF8 scripting reference).
static const uint16_t JSM_OP_SET      = 0x01D;  // 2D position
static const uint16_t JSM_OP_SET3     = 0x01E;  // 3D position
static const uint16_t JSM_OP_SETLINE  = 0x039;  // trigger line geometry
static const uint16_t JSM_OP_SETMODEL = 0x02B;  // assign 3D model
static const uint16_t JSM_OP_TALKRADIUS = 0x056;  // v0.19.4 diagnostic: HELD at 0x056 (known-wrong SPUREADY) so this log-only build classifies EXACTLY like the last BAT. The real TALKRADIUS 0x62 correction ships WITH the pickup discriminator in v0.19.5, once [ITEMDUMP]/[MODELSIG] confirm the pickup's actual interaction opcodes (it reads as NPC today, which implies TALKON is already present -- so the fix may not even be a TALKRADIUS change).
static const uint16_t JSM_OP_TALKON   = 0x057;  // enable talk interaction
static const uint16_t JSM_OP_MAPJUMP  = 0x029;  // field transition
static const uint16_t JSM_OP_MAPJUMP3 = 0x02A;  // field transition (3D)
static const uint16_t JSM_OP_SETDRAWPOINT = 0x155;  // configure draw point
static const uint16_t JSM_OP_DRAWPOINT   = 0x137;  // open draw point menu
static const uint16_t JSM_OP_MENUSAVE    = 0x12E;  // open save menu
static const uint16_t JSM_OP_SAVEENABLE  = 0x12F;  // enable saving
static const uint16_t JSM_OP_MENUSHOP    = 0x11E;  // open shop
static const uint16_t JSM_OP_CARDGAME    = 0x13A;  // card game
static const uint16_t JSM_OP_LADDERUP    = 0x025;  // ladder up
static const uint16_t JSM_OP_LADDERDOWN  = 0x026;  // ladder down
static const uint16_t JSM_OP_LADDERUP2   = 0x027;  // FIX-4: ladder up (variant 2)
static const uint16_t JSM_OP_LADDERDOWN2 = 0x028;  // FIX-4: ladder down (variant 2)
static const uint16_t JSM_OP_DISCJUMP    = 0x038;  // disc change transition
static const uint16_t JSM_OP_MAPJUMPO    = 0x05C;  // map jump (other variant)
static const uint16_t JSM_OP_SHOW        = 0x060;  // make entity visible
static const uint16_t JSM_OP_HIDE        = 0x061;  // make entity invisible
static const uint16_t JSM_OP_UNUSE       = 0x01A;  // deactivate entity
static const uint16_t JSM_OP_USE         = 0x0E5;  // reactivate entity
static const uint16_t JSM_OP_RET         = 0x004;  // return from script
static const uint16_t JSM_OP_PARTICLEON  = 0x14E;  // particle effect on
static const uint16_t JSM_OP_PARTICLEOFF = 0x14F;  // particle effect off
static const uint16_t JSM_OP_ADDITEM     = 0x125;  // add item to inventory
static const uint16_t JSM_OP_WORLDMAPJUMP = 0x10D; // world map transition
static const uint16_t JSM_OP_PHSENABLE   = 0x130;  // enable PHS at save point
static const uint16_t JSM_OP_MENUPHS     = 0x11B;  // open PHS menu
static const uint16_t JSM_OP_DOORLINEON  = 0x143;  // door trigger line on
static const uint16_t JSM_OP_DOORLINEOFF = 0x142;  // door trigger line off

// v0.07.82: Camera/scroll opcodes for trigger line classification.
// All < 0x100 → detected directly as primary opcodes (high byte), no 0x1C dispatch.
static const uint16_t JSM_OP_BGDRAW        = 0x099;  // draw/show background layer
static const uint16_t JSM_OP_BGOFF         = 0x09A;  // hide background layer
static const uint16_t JSM_OP_BGANIME       = 0x095;  // start background animation
static const uint16_t JSM_OP_BGANIMESPEED  = 0x09B;  // set background anim speed
static const uint16_t JSM_OP_DSCROLL       = 0x071;  // direct scroll (instant)
static const uint16_t JSM_OP_LSCROLL       = 0x072;  // linear scroll (smooth)
static const uint16_t JSM_OP_CSCROLL       = 0x073;  // curved scroll
static const uint16_t JSM_OP_DSCROLLA      = 0x074;  // direct scroll variant A
static const uint16_t JSM_OP_LSCROLLA      = 0x075;  // linear scroll variant A
static const uint16_t JSM_OP_CSCROLLA      = 0x076;  // curved scroll variant A
static const uint16_t JSM_OP_SCROLLSYNC    = 0x077;  // wait for scroll
static const uint16_t JSM_OP_DSCROLLP      = 0x07F;  // direct scroll P
static const uint16_t JSM_OP_LSCROLLP      = 0x080;  // linear scroll P
static const uint16_t JSM_OP_CSCROLLP      = 0x081;  // curved scroll P
static const uint16_t JSM_OP_SETCAMERA     = 0x10A;  // set camera position (>0xFF, via 0x1C)
static const uint16_t JSM_OP_MES           = 0x047;  // display dialog
static const uint16_t JSM_OP_ASK           = 0x04A;  // display dialog with choices
static const uint16_t JSM_OP_AMES          = 0x065;  // auto-position message
static const uint16_t JSM_OP_AASK          = 0x06F;  // auto-position choices
static const uint16_t JSM_OP_BATTLE        = 0x069;  // trigger battle
static const uint16_t JSM_OP_MOVE          = 0x03E;  // move entity to position
static const uint16_t JSM_OP_REQ           = 0x014;  // invoke script on other entity
static const uint16_t JSM_OP_REQSW         = 0x015;  // invoke script (wait)
static const uint16_t JSM_OP_REQEW         = 0x016;  // invoke script (exec wait)

const char* JSMEntityTypeName(JSMEntityType t)
{
    switch (t) {
        case JSM_ENT_DRAW_POINT:        return "Draw Point";
        case JSM_ENT_SAVE_POINT:        return "Save Point";
        case JSM_ENT_SHOP:              return "Shop";
        case JSM_ENT_CARD_GAME:         return "Card Game";
        case JSM_ENT_LADDER:            return "Ladder";
        case JSM_ENT_MAP_EXIT:          return "Map Exit";
        case JSM_ENT_NPC:               return "NPC";
        case JSM_ENT_DOOR:              return "Door";
        case JSM_ENT_LINE_TRIGGER:      return "Line Trigger";
        case JSM_ENT_LINE_CAMERA_PAN:   return "Camera Pan";
        case JSM_ENT_LINE_SCREEN_BOUND: return "Screen Boundary";
        case JSM_ENT_LINE_EVENT:        return "Event Trigger";
        case JSM_ENT_LINE_INTERACTIVE: return "Interactive Line";
        case JSM_ENT_BACKGROUND:        return "Background";
        case JSM_ENT_INTERACTIVE_OBJECT: return "Interactive Object";
        case JSM_ENT_DIRECTOR:          return "Director";
        default:                        return "Unknown";
    }
}
