// ff8_addresses.cpp - Runtime address resolution for FF8 Original PC (Steam 2013)
//
// Implements the same offset-chain resolution technique used by FFNx.
// See FFNx src/ff8_data.cpp and src/patch.cpp for the reference implementation.

#include "ff8_addresses.h"
#include "ff8_accessibility.h"
#include <cstdio>

namespace FF8Addresses {

// --- Resolved addresses ---
WORD*     pGameMode = nullptr;
WORD*     pMenuStateA = nullptr;
uint32_t* pMenuStateB = nullptr;
uint8_t*  pTitleCursorPos = nullptr;
uint32_t  main_menu_controller = 0;
uint32_t  main_menu_enter = 0;
uint32_t  main_menu_main_loop = 0;
uint32_t  main_loop = 0;
uint32_t  go_to_main_menu = 0;
uint32_t* pMode0LoopHandler = nullptr;
uint32_t* pGameLoopMainLoop = nullptr;
uint32_t  pubintro_main_loop = 0;
uint32_t  credits_main_loop = 0;
uint32_t  pGameObjGlobal = 0;
uint32_t  movieObjectAddr = 0;
uint32_t* pMovieIsPlaying = nullptr;
uint32_t* pMovieIntroPak = nullptr;
WORD*     pMovieCurrentFrame = nullptr;
WORD*     pMovieTotalFrames = nullptr;
char**    discPakFilenames = nullptr;
WORD*     pMode0InitFlag = nullptr;
uint8_t*  pMode0Phase = nullptr;
WORD*     pCurrentFieldId = nullptr;
char*     pCurrentFieldName = nullptr;

// v04.00: Field dialog opcode addresses
uint32_t* pExecuteOpcodeTable = nullptr;
uint32_t  opcode_mesw = 0;
uint32_t  opcode_mes = 0;
uint32_t  opcode_messync = 0;
uint32_t  opcode_ask = 0;
uint32_t  opcode_winclose = 0;
uint32_t  opcode_amesw = 0;
uint32_t  opcode_ames = 0;
uint32_t  opcode_aask = 0;
uint32_t  field_get_dialog_string = 0;
uint32_t  set_window_object = 0;
uint8_t*  pWindowsArray = nullptr;
uint32_t  world_dialog_assign_text = 0;

// v04.17: show_dialog and TUTO addresses
uint32_t  show_dialog_addr = 0;
uint32_t  sub_4A0880 = 0;
uint32_t  sub_4A0C00 = 0;
uint32_t  opcode_tuto = 0;
uint8_t*  pCurrentTutorialId = nullptr;

// v04.21: additional opcodes
uint32_t  opcode_mesmode = 0;
uint32_t  opcode_ramesw = 0;

// v04.25: naming screen bypass
uint32_t  opcode_menuname = 0;

// v05.56: SETLINE/LINEON/LINEOFF for trigger zone detection
uint32_t  opcode_setline = 0;
uint32_t  opcode_lineon = 0;
uint32_t  opcode_lineoff = 0;

// v05.78: TALKRADIUS/PUSHRADIUS for interaction distance detection
uint32_t  opcode_talkradius = 0;
uint32_t  opcode_pushradius = 0;

// v04.28+: engine input button state variables
uint32_t* pEngineInputConfirmedButtons = nullptr;
uint32_t* pEngineInputValidButtons     = nullptr;

// v04.30: engine_eval_keyboard_gamepad_input
uint32_t engine_eval_keyboard_gamepad_input_addr = 0;

// v05.82: Gamepad state struct for analog joystick steering
uint8_t* pGamepadStates = nullptr;

// v05.84: DirectInput gamepad device/state pointers
uint32_t* pDinputGamepadDevicePtr = nullptr;
uint32_t* pDinputGamepadStatePtr  = nullptr;
uint32_t  dinput_update_gamepad_status_addr = 0;

// v05.88: DirectInput keyboard buffer pointer
uint8_t** pKeyboardState = nullptr;

// v05.89: get_key_state function address for hooking
uint32_t get_key_state_addr = 0;

// v04.22: update_field_entities (script interpreter main loop)
uint32_t  update_field_entities_addr = 0;

// v04.20: menu_draw_text and get_character_width
uint32_t  menu_draw_text_addr = 0;
uint32_t  get_character_width_addr = 0;

// v05.00: Field entity state arrays
uint8_t*  pFieldStateOtherCount = nullptr;
uint8_t** pFieldStateOthers     = nullptr;

// v05.50: Background entity state array
uint8_t*  pFieldStateBackgroundCount = nullptr;
uint8_t** pFieldStateBackgrounds     = nullptr;

// v05.03: Field scripts init hook targets
uint32_t  field_scripts_init_addr = 0;
uint32_t  read_field_data_addr    = 0;

// v05.09: INF walkmesh pointer (nullptr until address chain resolved)
uint8_t** pFieldInfData = nullptr;

// v05.10: set_current_triangle hook target
uint32_t  set_current_triangle_addr = 0;

// --- Internal state ---
static uint32_t s_start = 0;
static bool s_resolved = false;

// ============================================================================
// Low-level helpers — same logic as FFNx patch.cpp
// ============================================================================

// Read a CALL (E8) or JMP (E9) instruction at base+offset
// and return the absolute target address.
static uint32_t get_relative_call(uint32_t base, uint32_t offset)
{
    uint8_t opcode = *(uint8_t*)(base + offset);
    uint8_t size = 1;  // E8 or E9

    if (opcode == 0xE8 || opcode == 0xE9) {
        size = 1;
    } else if (opcode == 0xFF) {
        // FF 15 xx xx xx xx = call dword ptr [addr]
        size = 2;
    } else {
        Log::Write("FF8Addresses: WARNING - unexpected opcode 0x%02X at 0x%08X + 0x%X",
                   opcode, base, offset);
        size = 1;  // try anyway
    }

    int32_t rel = *(int32_t*)(base + offset + size);
    uint32_t target = base + offset + rel + size + 4;
    return target;
}

// Read a 4-byte absolute value at base+offset.
static uint32_t get_absolute_value(uint32_t base, uint32_t offset)
{
    return *(uint32_t*)(base + offset);
}

// ============================================================================
// Version detection — identify which FF8 executable we're running in
// Same signature checks as FFNx common.cpp get_version()
// ============================================================================

enum FF8Version {
    VER_UNKNOWN = 0,
    VER_FF8_12_US,
    VER_FF8_12_US_NV,
    VER_FF8_12_FR,
    VER_FF8_12_FR_NV,
    VER_FF8_12_DE,
    VER_FF8_12_DE_NV,
    VER_FF8_12_SP,
    VER_FF8_12_SP_NV,
    VER_FF8_12_IT,
    VER_FF8_12_IT_NV,
    VER_FF8_12_US_EIDOS,
    VER_FF8_12_US_EIDOS_NV,
    VER_FF8_12_JP,
    VER_FF8_12_JP_NV,
};

static FF8Version detect_version()
{
    __try {
        uint32_t v1 = *(uint32_t*)0x401004;
        uint32_t v2 = *(uint32_t*)0x401404;

        Log::Write("FF8Addresses: Version check v1=0x%08X v2=0x%08X", v1, v2);

        if (v1 == 0x3885048D && v2 == 0x159618)  return VER_FF8_12_US;
        if (v1 == 0x3885048D && v2 == 0x1597C8)  return VER_FF8_12_US_NV;
        if (v1 == 0x1085048D && v2 == 0x159B48)  return VER_FF8_12_FR;
        if (v1 == 0x1085048D && v2 == 0x159CF8)  return VER_FF8_12_FR_NV;
        if (v1 == 0xA885048D && v2 == 0x159C48)  return VER_FF8_12_DE;
        if (v1 == 0xA885048D && v2 == 0x159DF8)  return VER_FF8_12_DE_NV;
        if (v1 == 0x8085048D && v2 == 0x159C38)  return VER_FF8_12_SP;
        if (v1 == 0x8085048D && v2 == 0x159DE8)  return VER_FF8_12_SP_NV;
        if (v1 == 0xB885048D && v2 == 0x159BC8)  return VER_FF8_12_IT;
        if (v1 == 0xB885048D && v2 == 0x159D78)  return VER_FF8_12_IT_NV;
        if (v1 == 0x2885048D && v2 == 0x159598)  return VER_FF8_12_US_EIDOS;
        if (v1 == 0x2885048D && v2 == 0x159748)  return VER_FF8_12_US_EIDOS_NV;
        if (v1 == 0x1B6E9CC  && v2 == 0x7C8DFFC9) {
            uint32_t v3 = *(uint32_t*)0x401010;
            if (v3 == 0x24AC) return VER_FF8_12_JP_NV;
            return VER_FF8_12_JP;
        }
    }
    __except(EXCEPTION_EXECUTE_HANDLER) {
        Log::Write("FF8Addresses: Exception during version detection!");
    }

    return VER_UNKNOWN;
}

static uint32_t get_start_address(FF8Version ver)
{
    switch (ver) {
    case VER_FF8_12_US:           return 0x55AC07;
    case VER_FF8_12_US_NV:        return 0x55ADB7;
    case VER_FF8_12_FR:           return 0x55B137;
    case VER_FF8_12_FR_NV:        return 0x55B2E7;
    case VER_FF8_12_DE:           return 0x55B237;
    case VER_FF8_12_DE_NV:        return 0x55B3E7;
    case VER_FF8_12_SP:           return 0x55B227;
    case VER_FF8_12_SP_NV:        return 0x55B3D7;
    case VER_FF8_12_IT:           return 0x55B1B7;
    case VER_FF8_12_IT_NV:        return 0x55B367;
    case VER_FF8_12_US_EIDOS:     return 0x55AB87;
    case VER_FF8_12_US_EIDOS_NV:  return 0x55AD37;
    case VER_FF8_12_JP:           return 0x55F487;
    case VER_FF8_12_JP_NV:        return 0x55F6E7;
    default: return 0;
    }
}

static bool is_jp(FF8Version ver)
{
    return ver == VER_FF8_12_JP || ver == VER_FF8_12_JP_NV;
}

static const char* version_name(FF8Version ver)
{
    switch (ver) {
    case VER_FF8_12_US:           return "FF8 1.2 US";
    case VER_FF8_12_US_NV:        return "FF8 1.2 US (NV/Steam)";
    case VER_FF8_12_FR:           return "FF8 1.2 FR";
    case VER_FF8_12_FR_NV:        return "FF8 1.2 FR (NV/Steam)";
    case VER_FF8_12_DE:           return "FF8 1.2 DE";
    case VER_FF8_12_DE_NV:        return "FF8 1.2 DE (NV/Steam)";
    case VER_FF8_12_SP:           return "FF8 1.2 SP";
    case VER_FF8_12_SP_NV:        return "FF8 1.2 SP (NV/Steam)";
    case VER_FF8_12_IT:           return "FF8 1.2 IT";
    case VER_FF8_12_IT_NV:        return "FF8 1.2 IT (NV/Steam)";
    case VER_FF8_12_US_EIDOS:     return "FF8 1.2 US (Eidos)";
    case VER_FF8_12_US_EIDOS_NV:  return "FF8 1.2 US (Eidos NV)";
    case VER_FF8_12_JP:           return "FF8 1.2 JP";
    case VER_FF8_12_JP_NV:        return "FF8 1.2 JP (NV)";
    default: return "Unknown";
    }
}

// ============================================================================
// Extract menu state addresses from main_menu_controller machine code.
//
// The function starts with a known prologue:
//   81 EC xx xx xx xx       SUB ESP, imm32
//   66 A1 xx xx xx xx       MOV AX, [imm32]    <-- pMenuStateA (WORD*)
//   53                      PUSH EBX
//   8B 1D xx xx xx xx       MOV EBX, [imm32]   <-- pMenuStateB (DWORD*)
//
// We extract the two absolute addresses from the instruction operands.
// ============================================================================
static bool extract_menu_state_addresses(uint32_t func_addr)
{
    uint8_t* code = (uint8_t*)func_addr;

    // Verify prologue pattern: SUB ESP, imm32 = 81 EC xx xx xx xx
    if (code[0] != 0x81 || code[1] != 0xEC) {
        Log::Write("FF8Addresses: WARNING - main_menu_controller prologue mismatch at +00: "
                   "expected 81 EC, got %02X %02X", code[0], code[1]);
        return false;
    }

    // +06: MOV AX, [imm32] = 66 A1 xx xx xx xx
    if (code[6] != 0x66 || code[7] != 0xA1) {
        Log::Write("FF8Addresses: WARNING - main_menu_controller prologue mismatch at +06: "
                   "expected 66 A1, got %02X %02X", code[6], code[7]);
        return false;
    }
    uint32_t addrA = *(uint32_t*)(code + 8);
    pMenuStateA = (WORD*)addrA;
    Log::Write("FF8Addresses:   pMenuStateA (WORD*) = 0x%08X  [extracted from MOV AX,[addr] at +06]", addrA);

    // +0C: PUSH EBX = 53
    if (code[12] != 0x53) {
        Log::Write("FF8Addresses: WARNING - expected PUSH EBX (53) at +0C, got %02X", code[12]);
        // Non-fatal, continue
    }

    // +0D: MOV EBX, [imm32] = 8B 1D xx xx xx xx
    if (code[13] != 0x8B || code[14] != 0x1D) {
        Log::Write("FF8Addresses: WARNING - main_menu_controller prologue mismatch at +0D: "
                   "expected 8B 1D, got %02X %02X", code[13], code[14]);
        return false;
    }
    uint32_t addrB = *(uint32_t*)(code + 15);
    pMenuStateB = (uint32_t*)addrB;
    Log::Write("FF8Addresses:   pMenuStateB (DWORD*) = 0x%08X [extracted from MOV EBX,[addr] at +0D]", addrB);

    return true;
}

// ============================================================================
// Main resolution chain
// Follows the same path as FFNx ff8_data.cpp ff8_find_externals()
// ============================================================================

bool Resolve()
{
    if (s_resolved) return true;

    Log::Write("FF8Addresses: === Beginning address resolution ===");

    // Step 1: Detect game version
    FF8Version ver = detect_version();
    if (ver == VER_UNKNOWN) {
        Log::Write("FF8Addresses: ERROR - Could not detect FF8 version.");
        return false;
    }
    Log::Write("FF8Addresses: Detected %s", version_name(ver));

    s_start = get_start_address(ver);
    if (s_start == 0) {
        Log::Write("FF8Addresses: ERROR - No start address for this version.");
        return false;
    }
    Log::Write("FF8Addresses: start = 0x%08X", s_start);

    bool jp = is_jp(ver);

    // Wrap everything in SEH so a bad pointer doesn't crash the game
    __try {
        // ---- Chain: start -> winmain -> main_entry ----
        uint32_t winmain = get_relative_call(s_start, 0xDB);
        Log::Write("FF8Addresses:   winmain = 0x%08X", winmain);

        uint32_t main_entry = get_relative_call(winmain, 0x4D);
        Log::Write("FF8Addresses:   main_entry = 0x%08X", main_entry);

        if (jp) {
            main_entry = get_relative_call(main_entry, 0x0);
            Log::Write("FF8Addresses:   main_entry (JP redirect) = 0x%08X", main_entry);
        }

        // ---- Chain: main_entry -> pubintro -> credits -> main menu ----
        pubintro_main_loop = get_absolute_value(main_entry, 0x180);
        Log::Write("FF8Addresses:   pubintro_main_loop = 0x%08X", pubintro_main_loop);

        credits_main_loop = get_absolute_value(pubintro_main_loop, 0x6D);
        Log::Write("FF8Addresses:   credits_main_loop = 0x%08X", credits_main_loop);

        go_to_main_menu = get_absolute_value(credits_main_loop, 0xE2);
        Log::Write("FF8Addresses:   go_to_main_menu_main_loop = 0x%08X", go_to_main_menu);

        main_menu_enter = get_absolute_value(go_to_main_menu, 0x19);
        Log::Write("FF8Addresses:   main_menu_enter = 0x%08X", main_menu_enter);

        main_menu_main_loop = get_absolute_value(go_to_main_menu, 0x2B);
        Log::Write("FF8Addresses:   main_menu_main_loop = 0x%08X", main_menu_main_loop);

        // ---- v01.11: Extract runtime pointers from main_loop ----
        // v01.09/10 revealed the handler struct is empty at startup.
        // Instead of trying to find it statically, we extract pointers for
        // runtime polling: game object, init flag, and phase byte.

        // ---- Chain: main_menu_main_loop -> sub_470630 -> main_loop -> _mode ----
        uint32_t sub_470630 = get_absolute_value(main_menu_main_loop, 0xE4);
        Log::Write("FF8Addresses:   sub_470630 = 0x%08X", sub_470630);

        main_loop = get_absolute_value(sub_470630, 0x24);
        Log::Write("FF8Addresses:   main_loop = 0x%08X", main_loop);

        uint32_t mode_offset = jp ? 0x118 : 0x115;
        uint32_t mode_addr = get_absolute_value(main_loop, mode_offset);
        pGameMode = (WORD*)mode_addr;
        Log::Write("FF8Addresses:   _mode (WORD*) = 0x%08X", mode_addr);

        // ---- Chain: main_menu_main_loop -> menu_callbacks -> main_menu_controller ----
        uint32_t sub_497380 = get_relative_call(main_menu_main_loop, 0xAA);
        Log::Write("FF8Addresses:   sub_497380 = 0x%08X", sub_497380);

        uint32_t sub_4B3310 = get_relative_call(sub_497380, 0xD3);
        Log::Write("FF8Addresses:   sub_4B3310 = 0x%08X", sub_4B3310);

        uint32_t sub_4B3140 = get_relative_call(sub_4B3310, 0xC8);
        Log::Write("FF8Addresses:   sub_4B3140 = 0x%08X", sub_4B3140);

        uint32_t sub_4BDB30 = get_relative_call(sub_4B3140, 0x4);
        Log::Write("FF8Addresses:   sub_4BDB30 = 0x%08X", sub_4BDB30);

        uint32_t menu_callbacks_addr = get_absolute_value(sub_4BDB30, 0x11);
        Log::Write("FF8Addresses:   menu_callbacks array = 0x%08X", menu_callbacks_addr);

        uint32_t menu_callback_16_func = *(uint32_t*)(menu_callbacks_addr + 16 * 8);
        Log::Write("FF8Addresses:   menu_callbacks[16].func = 0x%08X", menu_callback_16_func);

        main_menu_controller = get_absolute_value(menu_callback_16_func, 0x8);
        Log::Write("FF8Addresses:   main_menu_controller = 0x%08X", main_menu_controller);

        // ---- Extract cursor/state addresses from main_menu_controller prologue ----
        Log::Write("FF8Addresses: --- Extracting menu state addresses from prologue ---");
        if (!extract_menu_state_addresses(main_menu_controller)) {
            Log::Write("FF8Addresses: WARNING - Could not extract menu state addresses from prologue.");
            Log::Write("FF8Addresses: Falling back to hex dump for manual analysis.");
            // Dump first 32 bytes for debugging
            uint8_t* code = (uint8_t*)main_menu_controller;
            for (int i = 0; i < 32; i += 16) {
                Log::Write("  +%02X: %02X %02X %02X %02X %02X %02X %02X %02X  "
                           "%02X %02X %02X %02X %02X %02X %02X %02X",
                           i,
                           code[i+0],  code[i+1],  code[i+2],  code[i+3],
                           code[i+4],  code[i+5],  code[i+6],  code[i+7],
                           code[i+8],  code[i+9],  code[i+10], code[i+11],
                           code[i+12], code[i+13], code[i+14], code[i+15]);
            }
        }

        // ---- Derive title cursor position from pMenuStateA ----
        // diag6b memory sweep confirmed: cursor byte is at pMenuStateA + 0x1F6.
        // Values: 0=New Game, 1=Continue, 2=Credits.
        if (pMenuStateA != nullptr) {
            pTitleCursorPos = (uint8_t*)((uintptr_t)pMenuStateA + 0x1F6);
            Log::Write("FF8Addresses:   pTitleCursorPos (BYTE*) = 0x%08X  [pMenuStateA + 0x1F6]",
                       (uint32_t)(uintptr_t)pTitleCursorPos);
        } else {
            Log::Write("FF8Addresses: WARNING - pMenuStateA not resolved, cannot derive pTitleCursorPos.");
        }

        // ---- Extract game object + mode-0 control pointers from main_loop ----
        Log::Write("FF8Addresses: --- Extracting runtime pointers from main_loop ---");
        if (main_loop != 0) {
            uint8_t* code = (uint8_t*)main_loop;

            // 1. Game object pointer: MOV ESI, [addr] = 8B 35 [addr]
            for (int i = 0; i < 60; i++) {
                if (code[i] == 0x8B && code[i+1] == 0x35) {
                    pGameObjGlobal = *(uint32_t*)(code + i + 2);
                    Log::Write("FF8Addresses:   pGameObjGlobal = 0x%08X (from main_loop+%02X)",
                               pGameObjGlobal, i);
                    uint32_t gameObj = *(uint32_t*)pGameObjGlobal;
                    Log::Write("FF8Addresses:   *pGameObjGlobal = 0x%08X (game object)", gameObj);
                    break;
                }
            }

            // 2. Mode-0 init flag: 66 39 1D [addr] = CMP WORD [addr], reg
            //    At main_loop+0x0A in v01.09 dump.
            for (int i = 0; i < 30; i++) {
                if (code[i] == 0x66 && code[i+1] == 0x39 && code[i+2] == 0x1D) {
                    uint32_t addr = *(uint32_t*)(code + i + 3);
                    pMode0InitFlag = (WORD*)addr;
                    Log::Write("FF8Addresses:   pMode0InitFlag = 0x%08X (from main_loop+%02X, val=%u)",
                               addr, i, (unsigned)*pMode0InitFlag);
                    break;
                }
            }

            // 3. Mode-0 phase byte: 80 3D [addr] 04 = CMP BYTE [addr], 4
            //    At main_loop+0xD5 in v01.09 dump.
            for (int i = 0xC0; i < 0xF0; i++) {
                if (code[i] == 0x80 && code[i+1] == 0x3D) {
                    uint32_t addr = *(uint32_t*)(code + i + 2);
                    uint8_t cmpVal = code[i + 6];
                    pMode0Phase = (uint8_t*)addr;
                    Log::Write("FF8Addresses:   pMode0Phase = 0x%08X (from main_loop+%02X, cmp=%u, val=%u)",
                               addr, i, cmpVal, (unsigned)*pMode0Phase);
                    break;
                }
            }
        } else {
            Log::Write("FF8Addresses:   main_loop not resolved, skipping pointer extraction.");
        }

        // ---- Chain: main_loop -> field -> update_field_entities -> opcode table -> movie_object ----
        // Follows FFNx ff8_data.cpp resolution for movie detection.
        Log::Write("FF8Addresses: --- Resolving movie object (FMV detection) ---");
        {
            uint32_t field_main_loop_addr = get_absolute_value(main_loop, jp ? 0x144 + 3 : 0x144);
            Log::Write("FF8Addresses:   field_main_loop = 0x%08X", field_main_loop_addr);

            uint32_t sub_471F70 = get_relative_call(field_main_loop_addr, 0x148);
            Log::Write("FF8Addresses:   sub_471F70 = 0x%08X", sub_471F70);

            uint32_t sub_4767B0 = get_relative_call(sub_471F70, jp ? 0x4FE - 2 : 0x4FE);
            Log::Write("FF8Addresses:   sub_4767B0 = 0x%08X", sub_4767B0);

            uint32_t update_field_entities = get_relative_call(sub_4767B0, jp ? 0x14E + 1 : 0x14E);
            update_field_entities_addr = update_field_entities;
            Log::Write("FF8Addresses:   update_field_entities = 0x%08X", update_field_entities);

            pExecuteOpcodeTable = (uint32_t*)get_absolute_value(update_field_entities, 0x65A);
            Log::Write("FF8Addresses:   execute_opcode_table = 0x%08X", (uint32_t)pExecuteOpcodeTable);
            uint32_t* execute_opcode_table = pExecuteOpcodeTable;  // local alias for existing code below

            uint32_t opcode_movieready = execute_opcode_table[0xA3];
            Log::Write("FF8Addresses:   opcode_movieready [0xA3] = 0x%08X", opcode_movieready);

            uint32_t prepare_movie = get_relative_call(opcode_movieready, 0x99);
            Log::Write("FF8Addresses:   prepare_movie = 0x%08X", prepare_movie);

            movieObjectAddr = get_absolute_value(prepare_movie, 0xDB);
            Log::Write("FF8Addresses:   movie_object (struct addr) = 0x%08X", movieObjectAddr);

            // Resolve disc_pak_filenames from prepare_movie + 0xB2
            discPakFilenames = (char**)get_absolute_value(prepare_movie, 0xB2);
            Log::Write("FF8Addresses:   disc_pak_filenames = 0x%08X", (uint32_t)discPakFilenames);

            if (movieObjectAddr != 0 && movieObjectAddr < 0x7FFFFFFF) {
                // ff8_movie_obj field offsets (from FFNx ff8.h):
                //   +0x00000 = movie_current_frame (WORD)
                //   +0x00002 = movie_total_frames (WORD)
                //   +0x4C4A4 = movie_intro_pak (DWORD) - index into disc_pak_filenames
                //   +0x4C4A8 = movie_is_playing (DWORD)
                pMovieCurrentFrame = (WORD*)(movieObjectAddr + 0x00);
                pMovieTotalFrames  = (WORD*)(movieObjectAddr + 0x02);
                pMovieIntroPak     = (uint32_t*)(movieObjectAddr + 0x4C4A4);
                pMovieIsPlaying    = (uint32_t*)(movieObjectAddr + 0x4C4A8);

                Log::Write("FF8Addresses:   pMovieIntroPak     = 0x%08X (val=%u)",
                           (uint32_t)pMovieIntroPak, *pMovieIntroPak);
                Log::Write("FF8Addresses:   pMovieIsPlaying    = 0x%08X (val=%u)",
                           (uint32_t)pMovieIsPlaying, *pMovieIsPlaying);
                Log::Write("FF8Addresses:   pMovieCurrentFrame = 0x%08X (val=%u)",
                           (uint32_t)pMovieCurrentFrame, (unsigned)*pMovieCurrentFrame);
                Log::Write("FF8Addresses:   pMovieTotalFrames  = 0x%08X (val=%u)",
                           (uint32_t)pMovieTotalFrames, (unsigned)*pMovieTotalFrames);

                // Try to log the current movie filename
                if (discPakFilenames != nullptr) {
                    uint32_t pakIdx = *pMovieIntroPak;
                    char* fname = discPakFilenames[pakIdx];
                    if (fname != nullptr) {
                        Log::Write("FF8Addresses:   Current movie [%u] = \"%s\"", pakIdx, fname);
                    }
                }
            } else {
                Log::Write("FF8Addresses:   WARNING - movie_object address looks invalid (0x%08X)", movieObjectAddr);
                movieObjectAddr = 0;
            }
        }

        // ---- Resolve current_field_id and current_field_name ----
        // FFNx: current_field_id = (WORD*)get_absolute_value(main_loop, 0x21F)
        // FFNx: current_field_name = (char*)get_absolute_value(opcode_effectplay2, 0x75)
        Log::Write("FF8Addresses: --- Resolving field ID and field name ---");
        {
            uint32_t field_id_offset = jp ? 0x21F + 6 : 0x21F;
            uint32_t field_id_addr = get_absolute_value(main_loop, field_id_offset);
            pCurrentFieldId = (WORD*)field_id_addr;
            Log::Write("FF8Addresses:   pCurrentFieldId (WORD*) = 0x%08X (from main_loop+0x%X, val=%u)",
                       field_id_addr, field_id_offset, (unsigned)*pCurrentFieldId);

            // current_field_name from opcode_effectplay2 (opcode table index 0x21)
            uint32_t* exec_table = (uint32_t*)get_absolute_value(
                get_relative_call(
                    get_relative_call(
                        get_relative_call(get_absolute_value(main_loop, jp ? 0x144 + 3 : 0x144), 0x148),
                        jp ? 0x4FE - 2 : 0x4FE),
                    jp ? 0x14E + 1 : 0x14E),
                0x65A);
            uint32_t opcode_effectplay2 = exec_table[0x21];
            pCurrentFieldName = (char*)get_absolute_value(opcode_effectplay2, 0x75);
            Log::Write("FF8Addresses:   pCurrentFieldName (char*) = 0x%08X (val=\"%s\")",
                       (uint32_t)pCurrentFieldName, pCurrentFieldName ? pCurrentFieldName : "(null)");
        }

        // ---- v04.00: Resolve field dialog opcode addresses ----
        Log::Write("FF8Addresses: --- Resolving field dialog opcodes ---");
        if (pExecuteOpcodeTable != nullptr) {
            opcode_mesw     = pExecuteOpcodeTable[0x46];
            opcode_mes      = pExecuteOpcodeTable[0x47];
            opcode_messync  = pExecuteOpcodeTable[0x48];
            opcode_ask      = pExecuteOpcodeTable[0x4A];
            opcode_winclose = pExecuteOpcodeTable[0x4C];
            opcode_amesw    = pExecuteOpcodeTable[0x64];
            opcode_ames     = pExecuteOpcodeTable[0x65];
            opcode_aask     = pExecuteOpcodeTable[0x6F];

            Log::Write("FF8Addresses:   opcode_mesw     [0x46] = 0x%08X", opcode_mesw);
            Log::Write("FF8Addresses:   opcode_mes      [0x47] = 0x%08X", opcode_mes);
            Log::Write("FF8Addresses:   opcode_messync  [0x48] = 0x%08X", opcode_messync);
            Log::Write("FF8Addresses:   opcode_ask      [0x4A] = 0x%08X", opcode_ask);
            Log::Write("FF8Addresses:   opcode_winclose [0x4C] = 0x%08X", opcode_winclose);
            Log::Write("FF8Addresses:   opcode_amesw    [0x64] = 0x%08X", opcode_amesw);
            Log::Write("FF8Addresses:   opcode_ames     [0x65] = 0x%08X", opcode_ames);
            Log::Write("FF8Addresses:   opcode_aask     [0x6F] = 0x%08X", opcode_aask);

            // Resolve sub-functions from opcode_mes
            if (opcode_mes != 0) {
                field_get_dialog_string = get_relative_call(opcode_mes, 0x5D);
                set_window_object       = get_relative_call(opcode_mes, 0x66);
                Log::Write("FF8Addresses:   field_get_dialog_string = 0x%08X (from opcode_mes+0x5D)",
                           field_get_dialog_string);
                Log::Write("FF8Addresses:   set_window_object       = 0x%08X (from opcode_mes+0x66)",
                           set_window_object);

                // Resolve windows array from set_window_object+0x11
                if (set_window_object != 0) {
                    uint32_t winAddr = get_absolute_value(set_window_object, 0x11);
                    pWindowsArray = (uint8_t*)winAddr;
                    Log::Write("FF8Addresses:   pWindowsArray = 0x%08X (from set_window_object+0x11)",
                               winAddr);
                }
            }
        } else {
            Log::Write("FF8Addresses:   WARNING - execute_opcode_table not resolved, skipping dialog opcodes.");
        }

        // ---- v04.17: Resolve show_dialog (universal text renderer) ----
        // Chain: credits_main_loop -> sub_470440 -> sub_49ACD0 -> sub_4A0880
        //        -> sub_4A0C00 -> show_dialog
        // Same chain as FFNx ff8_data.cpp.
        Log::Write("FF8Addresses: --- Resolving show_dialog (v04.17) ---");
        {
            uint32_t sub_470440 = get_absolute_value(credits_main_loop, 0xD2);
            Log::Write("FF8Addresses:   sub_470440 = 0x%08X", sub_470440);

            uint32_t sub_49ACD0 = get_relative_call(sub_470440, jp ? 0x9C : 0x98);
            Log::Write("FF8Addresses:   sub_49ACD0 = 0x%08X", sub_49ACD0);

            sub_4A0880 = get_relative_call(sub_49ACD0, 0x58);
            Log::Write("FF8Addresses:   sub_4A0880 = 0x%08X", sub_4A0880);

            sub_4A0C00 = get_absolute_value(sub_4A0880, 0x33);
            Log::Write("FF8Addresses:   sub_4A0C00 = 0x%08X", sub_4A0C00);

            show_dialog_addr = get_relative_call(sub_4A0C00, 0x5F);
            Log::Write("FF8Addresses:   show_dialog = 0x%08X", show_dialog_addr);
        }

        // ---- v04.17: Resolve opcode_tuto and current_tutorial_id ----
        if (pExecuteOpcodeTable != nullptr) {
            opcode_tuto = pExecuteOpcodeTable[0x177];
            Log::Write("FF8Addresses:   opcode_tuto     [0x177] = 0x%08X", opcode_tuto);

            if (opcode_tuto != 0) {
                pCurrentTutorialId = (uint8_t*)get_absolute_value(opcode_tuto, 0x2A);
                Log::Write("FF8Addresses:   pCurrentTutorialId (BYTE*) = 0x%08X",
                           (uint32_t)(uintptr_t)pCurrentTutorialId);
            }

            // v04.21: mesmode and ramesw
            opcode_mesmode = pExecuteOpcodeTable[0x106];
            opcode_ramesw = pExecuteOpcodeTable[0x116];
            Log::Write("FF8Addresses:   opcode_mesmode  [0x106] = 0x%08X", opcode_mesmode);
            Log::Write("FF8Addresses:   opcode_ramesw   [0x116] = 0x%08X", opcode_ramesw);

            // v04.25: menuname (character naming screen)
            opcode_menuname = pExecuteOpcodeTable[0x129];
            Log::Write("FF8Addresses:   opcode_menuname [0x129] = 0x%08X", opcode_menuname);

            // v05.56: SETLINE/LINEON/LINEOFF for trigger zone detection
            opcode_setline = pExecuteOpcodeTable[0x39];
            opcode_lineon  = pExecuteOpcodeTable[0x3A];
            opcode_lineoff = pExecuteOpcodeTable[0x3B];
            Log::Write("FF8Addresses:   opcode_setline  [0x039] = 0x%08X", opcode_setline);
            Log::Write("FF8Addresses:   opcode_lineon   [0x03A] = 0x%08X", opcode_lineon);
            Log::Write("FF8Addresses:   opcode_lineoff  [0x03B] = 0x%08X", opcode_lineoff);

            // v05.78: TALKRADIUS/PUSHRADIUS for interaction distance detection
            opcode_talkradius = pExecuteOpcodeTable[0x62];
            opcode_pushradius = pExecuteOpcodeTable[0x63];
            Log::Write("FF8Addresses:   opcode_talkradius [0x062] = 0x%08X", opcode_talkradius);
            Log::Write("FF8Addresses:   opcode_pushradius [0x063] = 0x%08X", opcode_pushradius);

            // v04.28+/v04.30: engine input variables and eval hook target
            // Chain: pubintro_main_loop+0x4 -> +0x16 -> +0x4A6 -> abs@+0x3C / +0x62
            {
                uint32_t engine_eval_process_input = get_relative_call(pubintro_main_loop, 0x4);
                engine_eval_keyboard_gamepad_input_addr = get_relative_call(engine_eval_process_input, 0x16);
                uint32_t engine_eval_is_button_pressed = get_relative_call(engine_eval_keyboard_gamepad_input_addr, 0x4A6);
                pEngineInputValidButtons     = (uint32_t*)get_absolute_value(engine_eval_is_button_pressed, 0x3C);
                pEngineInputConfirmedButtons = (uint32_t*)get_absolute_value(engine_eval_is_button_pressed, 0x62);
                Log::Write("FF8Addresses:   engine_eval_process_input           = 0x%08X", engine_eval_process_input);
                Log::Write("FF8Addresses:   engine_eval_keyboard_gamepad_input  = 0x%08X", engine_eval_keyboard_gamepad_input_addr);
                Log::Write("FF8Addresses:   engine_eval_is_button_pressed        = 0x%08X", engine_eval_is_button_pressed);
                Log::Write("FF8Addresses:   pEngineInputValidButtons     = 0x%08X", (uint32_t)(uintptr_t)pEngineInputValidButtons);
                Log::Write("FF8Addresses:   pEngineInputConfirmedButtons = 0x%08X", (uint32_t)(uintptr_t)pEngineInputConfirmedButtons);
            }

            // v05.82: Resolve gamepad state struct for analog steering.
            // Chain: field_main_loop+0x16C -> check_game_is_paused
            //        -> +0xE2 -> init_pause_menu
            //        -> -0x290 = pause_menu_with_vibration
            //        -> +0xE3 -> get_vibration_capability
            //        -> get_absolute_value(+0x11) - 0xB = gamepad_states
            // Same chain as FFNx ff8_data.cpp.
            Log::Write("FF8Addresses: --- Resolving gamepad state struct (v05.82) ---");
            {
                uint32_t field_main_loop_for_gp = get_absolute_value(main_loop, jp ? 0x144 + 3 : 0x144);
                uint32_t check_game_is_paused = get_relative_call(field_main_loop_for_gp, 0x16C);
                uint32_t init_pause_menu = get_relative_call(check_game_is_paused, 0xE2);
                uint32_t pause_menu_with_vibration = init_pause_menu - 0x290;
                uint32_t get_vibration_capability = get_relative_call(pause_menu_with_vibration, 0xE3);
                // FFNx: gamepad_states = (ff8_gamepad_state*)(get_absolute_value(get_vibration_capability, 0xE + 3) - 0xB)
                uint32_t raw_addr = get_absolute_value(get_vibration_capability, 0x11);
                uint32_t gamepad_states_addr = raw_addr - 0xB;
                pGamepadStates = (uint8_t*)gamepad_states_addr;

                Log::Write("FF8Addresses:   check_game_is_paused        = 0x%08X", check_game_is_paused);
                Log::Write("FF8Addresses:   init_pause_menu             = 0x%08X", init_pause_menu);
                Log::Write("FF8Addresses:   pause_menu_with_vibration   = 0x%08X", pause_menu_with_vibration);
                Log::Write("FF8Addresses:   get_vibration_capability    = 0x%08X", get_vibration_capability);
                Log::Write("FF8Addresses:   pGamepadStates (raw=0x%08X - 0xB) = 0x%08X", raw_addr, gamepad_states_addr);

                // v05.84: Resolve dinput_gamepad_device and dinput_gamepad_state pointers.
                // Chain: engine_eval_keyboard_gamepad_input+0x1B -> dinput_update_gamepad_status
                //   -> +0x16 = dinput_gamepad_device ptr, +0x1B = dinput_gamepad_state ptr
                uint32_t dinput_update_gamepad_status = get_relative_call(engine_eval_keyboard_gamepad_input_addr, 0x1B);
                dinput_update_gamepad_status_addr = dinput_update_gamepad_status;
                pDinputGamepadDevicePtr = (uint32_t*)get_absolute_value(dinput_update_gamepad_status, 0x16);
                pDinputGamepadStatePtr  = (uint32_t*)get_absolute_value(dinput_update_gamepad_status, 0x1B);
                Log::Write("FF8Addresses:   dinput_update_gamepad_status = 0x%08X", dinput_update_gamepad_status);
                Log::Write("FF8Addresses:   pDinputGamepadDevicePtr = 0x%08X (val=0x%08X)",
                           (uint32_t)(uintptr_t)pDinputGamepadDevicePtr, *pDinputGamepadDevicePtr);
                Log::Write("FF8Addresses:   pDinputGamepadStatePtr  = 0x%08X (val=0x%08X)",
                           (uint32_t)(uintptr_t)pDinputGamepadStatePtr, *pDinputGamepadStatePtr);

                // v05.88: Resolve keyboard_state buffer pointer for arrow key suppression.
                // Chain: main_loop -> field_main_loop -> sub_471F70 -> sub_4767B0
                //        -> +0x156 -> ctrl_keyboard_actions
                //        -> +0x5 -> get_key_state
                //        -> get_absolute_value(+0x27) -> keyboard_state (byte**)
                {
                    uint32_t fml = get_absolute_value(main_loop, jp ? 0x144 + 3 : 0x144);
                    uint32_t s471F70 = get_relative_call(fml, 0x148);
                    uint32_t s4767B0 = get_relative_call(s471F70, jp ? 0x4FE - 2 : 0x4FE);
                    uint32_t ctrl_keyboard_actions = get_relative_call(s4767B0, 0x156);
                    uint32_t get_key_state_fn = get_relative_call(ctrl_keyboard_actions, 0x5);
                    get_key_state_addr = get_key_state_fn;
                    uint32_t kb_state_addr = get_absolute_value(get_key_state_fn, 0x27);
                    pKeyboardState = (uint8_t**)kb_state_addr;
                    Log::Write("FF8Addresses:   ctrl_keyboard_actions   = 0x%08X", ctrl_keyboard_actions);
                    Log::Write("FF8Addresses:   get_key_state           = 0x%08X", get_key_state_fn);
                    Log::Write("FF8Addresses:   pKeyboardState (byte**) = 0x%08X (val=0x%08X)",
                               kb_state_addr, (uint32_t)(uintptr_t)*pKeyboardState);
                }

                // Diagnostic: dump the first 32 bytes of the gamepad state struct.
                // This lets us verify the struct layout at runtime.
                if (gamepad_states_addr > 0x10000 && gamepad_states_addr < 0x7FFFFFFF) {
                    uint8_t* gp = (uint8_t*)gamepad_states_addr;
                    Log::Write("FF8Addresses:   [GPDIAG] gamepad_states first 32 bytes:");
                    Log::Write("FF8Addresses:   [GPDIAG] +00: %02X %02X %02X %02X %02X %02X %02X %02X  %02X %02X %02X %02X %02X %02X %02X %02X",
                               gp[0], gp[1], gp[2], gp[3], gp[4], gp[5], gp[6], gp[7],
                               gp[8], gp[9], gp[10], gp[11], gp[12], gp[13], gp[14], gp[15]);
                    Log::Write("FF8Addresses:   [GPDIAG] +10: %02X %02X %02X %02X %02X %02X %02X %02X  %02X %02X %02X %02X %02X %02X %02X %02X",
                               gp[16], gp[17], gp[18], gp[19], gp[20], gp[21], gp[22], gp[23],
                               gp[24], gp[25], gp[26], gp[27], gp[28], gp[29], gp[30], gp[31]);
                    // entries_offset is at +0x18 in ff8_gamepad_vibration_state
                    uint8_t entries_offset = gp[0x18];
                    Log::Write("FF8Addresses:   [GPDIAG] entries_offset (byte at +0x18) = %u", (unsigned)entries_offset);
                    // Each entry is 0x14 bytes (sizeof ff8_gamepad_vibration_state_entry = 16 bytes... let me check)
                    // From ff8.h: entry has analog_disabled(1), analog_flags(1), keyscan(2), analog_rx(1),
                    //   analog_ry(1), analog_lx(1), analog_ly(1), field_8(2), field_A(2), field_C(2),
                    //   field_E(2), keyon(2), keyscan_invert(2) = 20 bytes total
                    // entries[] starts at offset 0x1C in ff8_gamepad_vibration_state
                    uint32_t entry_base = 0x1C + entries_offset * 20;
                    Log::Write("FF8Addresses:   [GPDIAG] active entry at struct+0x%X:", entry_base);
                    if (entry_base + 20 < 0xC4) { // sizeof(ff8_gamepad_vibration_state) = 0xC4
                        uint8_t* entry = gp + entry_base;
                        Log::Write("FF8Addresses:   [GPDIAG]   analog_disabled=%u analog_flags=0x%02X keyscan=0x%04X",
                                   entry[0], entry[1], *(uint16_t*)(entry + 2));
                        Log::Write("FF8Addresses:   [GPDIAG]   analog_rx=%u analog_ry=%u analog_lx=%u analog_ly=%u",
                                   entry[4], entry[5], entry[6], entry[7]);
                        Log::Write("FF8Addresses:   [GPDIAG]   keyon=0x%04X keyscan_invert=0x%04X",
                                   *(uint16_t*)(entry + 16), *(uint16_t*)(entry + 18));
                    }
                } else {
                    Log::Write("FF8Addresses:   WARNING - gamepad_states address looks invalid");
                    pGamepadStates = nullptr;
                }
            }
        }

        // ---- v04.20: Resolve menu_draw_text (low-level text renderer) ----
        // Chain: sub_497380 -> sub_4B3410 -> sub_4BE4D0 -> sub_4BECC0 -> menu_draw_text
        // Same chain as FFNx ff8_data.cpp.
        Log::Write("FF8Addresses: --- Resolving menu_draw_text (v04.20) ---");
        {
            // sub_497380 already resolved above
            uint32_t sub_4B3410 = get_relative_call(sub_497380, 0xAC);
            Log::Write("FF8Addresses:   sub_4B3410 = 0x%08X", sub_4B3410);

            uint32_t sub_4BE4D0 = get_relative_call(sub_4B3410, 0x68);
            Log::Write("FF8Addresses:   sub_4BE4D0 = 0x%08X", sub_4BE4D0);

            uint32_t sub_4BECC0 = get_relative_call(sub_4BE4D0, 0x39);
            Log::Write("FF8Addresses:   sub_4BECC0 = 0x%08X", sub_4BECC0);

            menu_draw_text_addr = get_relative_call(sub_4BECC0, 0x127);
            Log::Write("FF8Addresses:   menu_draw_text = 0x%08X", menu_draw_text_addr);

            if (menu_draw_text_addr != 0) {
                get_character_width_addr = get_relative_call(menu_draw_text_addr, jp ? 0x1E1 : 0x1D0);
                Log::Write("FF8Addresses:   get_character_width = 0x%08X", get_character_width_addr);
            }
        }

        // ---- v05.00: Resolve field entity state arrays (player triangle) ----
        // Chain: sub_471F70 -> read_field_data -> field_scripts_init -> static addresses
        // Same chain as FFNx ff8_data.cpp: field_scripts_init derives both arrays.
        Log::Write("FF8Addresses: --- Resolving field entity state arrays (v05.00) ---");
        {
            uint32_t field_main_loop_addr_v5 = get_absolute_value(main_loop, jp ? 0x144 + 3 : 0x144);
            uint32_t sub_471F70_v5 = get_relative_call(field_main_loop_addr_v5, 0x148);
            read_field_data_addr = get_relative_call(sub_471F70_v5, 0x23A);
            Log::Write("FF8Addresses:   read_field_data = 0x%08X", read_field_data_addr);

            uint32_t field_scripts_init = get_relative_call(read_field_data_addr, jp ? 0xEDC : 0xE49);
            field_scripts_init_addr = field_scripts_init;
            Log::Write("FF8Addresses:   field_scripts_init = 0x%08X", field_scripts_init_addr);

            // v05.10: resolve set_current_triangle_sub_45E160
            // Chain (FFNx ff8_data.cpp):
            //   sub_472B30 = get_relative_call(sub_4767B0, jp ? 0x4C9+3 : 0x4C9)
            //   sub_530810 = get_relative_call(sub_472B30, jp ? 0x35D+7 : 0x35D)
            //   sub_533CD0 = get_relative_call(sub_530810, 0x27B)
            //   sub_530C30 = get_relative_call(sub_533CD0, 0x28E)
            //   field_push_mch = get_relative_call(sub_530C30, 0x46A)
            //   set_current_triangle = get_relative_call(field_push_mch, 0x48)
            uint32_t sub_4767B0_v5 = get_relative_call(sub_471F70_v5, jp ? 0x4FE - 2 : 0x4FE);
            uint32_t sub_472B30 = get_relative_call(sub_4767B0_v5, jp ? 0x4C9 + 3 : 0x4C9);
            uint32_t sub_530810 = get_relative_call(sub_472B30, jp ? 0x35D + 7 : 0x35D);
            uint32_t sub_533CD0 = get_relative_call(sub_530810, 0x27B);
            uint32_t sub_530C30 = get_relative_call(sub_533CD0, 0x28E);
            uint32_t field_push_mch = get_relative_call(sub_530C30, 0x46A);
            set_current_triangle_addr = get_relative_call(field_push_mch, 0x48);
            Log::Write("FF8Addresses:   sub_4767B0  = 0x%08X", sub_4767B0_v5);
            Log::Write("FF8Addresses:   sub_472B30  = 0x%08X", sub_472B30);
            Log::Write("FF8Addresses:   sub_530810  = 0x%08X", sub_530810);
            Log::Write("FF8Addresses:   sub_533CD0  = 0x%08X", sub_533CD0);
            Log::Write("FF8Addresses:   sub_530C30  = 0x%08X", sub_530C30);
            Log::Write("FF8Addresses:   field_push_mch_vertices_rect = 0x%08X", field_push_mch);
            Log::Write("FF8Addresses:   set_current_triangle_addr = 0x%08X", set_current_triangle_addr);

            // pFieldStateOtherCount: BYTE* — MOVZX at field_scripts_init+0x2C4 operand
            // FFNx: (uint8_t*)get_absolute_value(field_scripts_init, 0x2C3+0x1)
            uint32_t otherCountAddr = get_absolute_value(field_scripts_init, 0x2C4);
            pFieldStateOtherCount = (uint8_t*)otherCountAddr;
            Log::Write("FF8Addresses:   pFieldStateOtherCount (BYTE*) = 0x%08X (val=%u)",
                       otherCountAddr, (unsigned)*pFieldStateOtherCount);

            // pFieldStateOthers: ptr-to-array — MOV at field_scripts_init+0x62E operand
            // FFNx: (ff8_field_state_other**)get_absolute_value(field_scripts_init, 0x62C+0x2)
            uint32_t othersAddr = get_absolute_value(field_scripts_init, 0x62E);
            pFieldStateOthers = (uint8_t**)othersAddr;
            Log::Write("FF8Addresses:   pFieldStateOthers (uint8_t**) = 0x%08X", othersAddr);

            // v05.50: pFieldStateBackgroundCount: BYTE* — at field_scripts_init+0x2CE
            // FFNx: (uint8_t*)get_absolute_value(field_scripts_init, 0x2CD+0x1)
            uint32_t bgCountAddr = get_absolute_value(field_scripts_init, 0x2CE);
            pFieldStateBackgroundCount = (uint8_t*)bgCountAddr;
            Log::Write("FF8Addresses:   pFieldStateBackgroundCount (BYTE*) = 0x%08X (val=%u)",
                       bgCountAddr, (unsigned)*pFieldStateBackgroundCount);

            // v05.50: pFieldStateBackgrounds: ptr-to-array — at field_scripts_init+0x50D
            // FFNx: (ff8_field_state_background**)get_absolute_value(field_scripts_init, 0x50B+0x2)
            uint32_t bgAddr = get_absolute_value(field_scripts_init, 0x50D);
            pFieldStateBackgrounds = (uint8_t**)bgAddr;
            Log::Write("FF8Addresses:   pFieldStateBackgrounds (uint8_t**) = 0x%08X", bgAddr);
        }

        // ---- v01.13: game_loop_obj.main_loop is resolved via deferred scan ----
        // The game object is zero-initialized at startup; game_loop_obj is
        // populated later during engine init. Call TryResolveDeferredGameLoop()
        // from the polling loop once the game is running.
        Log::Write("FF8Addresses: --- game_loop_obj.main_loop will be resolved via deferred scan ---");
        Log::Write("FF8Addresses:   pGameObjGlobal = 0x%08X, pubintro_main_loop = 0x%08X",
                   pGameObjGlobal, pubintro_main_loop);

        // ---- Log current values as sanity check ----
        Log::Write("FF8Addresses: --- Current values snapshot ---");
        Log::Write("FF8Addresses:   *_mode = %u", (unsigned)*pGameMode);
        if (pMenuStateA)
            Log::Write("FF8Addresses:   *pMenuStateA = %u (0x%04X)", (unsigned)*pMenuStateA, (unsigned)*pMenuStateA);
        if (pMenuStateB)
            Log::Write("FF8Addresses:   *pMenuStateB = %u (0x%08X)", *pMenuStateB, *pMenuStateB);
        if (pMode0LoopHandler) {
            Log::Write("FF8Addresses:   *pMode0LoopHandler = 0x%08X", *pMode0LoopHandler);
            if (*pMode0LoopHandler == pubintro_main_loop)
                Log::Write("FF8Addresses:     -> matches pubintro_main_loop (FMV phase)");
            else if (*pMode0LoopHandler == credits_main_loop)
                Log::Write("FF8Addresses:     -> matches credits_main_loop");
            else if (*pMode0LoopHandler == main_menu_main_loop)
                Log::Write("FF8Addresses:     -> matches main_menu_main_loop (TITLE MENU)");
            else
                Log::Write("FF8Addresses:     -> unknown handler");
        }

        s_resolved = true;
        Log::Write("FF8Addresses: === Resolution complete. All critical addresses found. ===");
        return true;
    }
    __except(EXCEPTION_EXECUTE_HANDLER) {
        Log::Write("FF8Addresses: EXCEPTION during resolution chain! Code=0x%08X",
                   GetExceptionCode());
        return false;
    }
}

// ============================================================================
// v01.13: Deferred resolution of game_loop_obj.main_loop
//
// The game object is zero-initialized at process start. The engine populates
// game_loop_obj later during init. This function scans the game object for
// a known function pointer (pubintro_main_loop, credits_main_loop, or
// main_menu_main_loop) and locks onto that offset.
//
// Call from the polling loop. Returns true once resolved (cached).
// Safe to call repeatedly — it's a no-op after first success.
// ============================================================================

static bool s_deferredScanAttempted = false;
static bool s_diagnosticDumped = false;
static DWORD s_firstScanTime = 0;

bool TryResolveDeferredGameLoop()
{
    // Already resolved?
    if (pGameLoopMainLoop != nullptr)
        return true;
    
    // Prerequisites
    if (pGameObjGlobal == 0 || pubintro_main_loop == 0)
        return false;
    
    uint32_t gameObjBase = *(uint32_t*)pGameObjGlobal;
    if (gameObjBase == 0 || gameObjBase >= 0x7FFFFFFF)
        return false;
    
    DWORD now = GetTickCount();
    if (s_firstScanTime == 0) s_firstScanTime = now;
    
    // Scan a very wide range of the game object for any known handler.
    // The struct has platform-dependent fields (DDSURFACEDESC, DDCAPS_DX5, etc.)
    // so the offset could be far from the FFNx header's estimate of 0xA1C.
    // Use SEH to safely handle access beyond the allocated region.
    uint32_t targets[3] = { pubintro_main_loop, credits_main_loop, main_menu_main_loop };
    const char* names[3] = { "pubintro_main_loop", "credits_main_loop", "main_menu_main_loop" };
    
    __try {
        for (uint32_t off = 0x000; off < 0x4000; off += 4) {
            uint32_t val = *(uint32_t*)(gameObjBase + off);
            for (int i = 0; i < 3; i++) {
                if (val == targets[i]) {
                    pGameLoopMainLoop = (uint32_t*)(gameObjBase + off);
                    Log::Write("FF8Addresses: DEFERRED SCAN: Found %s (0x%08X) at game_object + 0x%X "
                               "(abs addr 0x%08X, elapsed %ums)",
                               names[i], val, off,
                               (uint32_t)pGameLoopMainLoop, (now - s_firstScanTime));
                    return true;
                }
            }
        }
    }
    __except(EXCEPTION_EXECUTE_HANDLER) {
        // Hit unreadable memory — not unexpected for a wide scan.
        if (!s_deferredScanAttempted) {
            Log::Write("FF8Addresses: Deferred scan hit unreadable memory in game object region.");
        }
    }
    
    // After 5 seconds, do a diagnostic: scan entire nearby memory for our targets.
    // This helps us find if the pointers exist ANYWHERE near the game object.
    if (!s_diagnosticDumped && (now - s_firstScanTime) >= 5000) {
        s_diagnosticDumped = true;
        Log::Write("FF8Addresses: === DIAGNOSTIC: Wide memory scan for handler function pointers ===");
        Log::Write("FF8Addresses:   gameObjBase = 0x%08X", gameObjBase);
        Log::Write("FF8Addresses:   Looking for: pubintro=0x%08X credits=0x%08X main_menu=0x%08X",
                   pubintro_main_loop, credits_main_loop, main_menu_main_loop);
        
        // Scan from gameObjBase-0x1000 to gameObjBase+0x8000
        uint32_t scanStart = (gameObjBase > 0x1000) ? (gameObjBase - 0x1000) : 0x10000;
        uint32_t scanEnd = gameObjBase + 0x8000;
        int foundCount = 0;
        
        __try {
            for (uint32_t addr = scanStart; addr < scanEnd; addr += 4) {
                uint32_t val = *(uint32_t*)addr;
                for (int i = 0; i < 3; i++) {
                    if (val == targets[i]) {
                        int32_t relOff = (int32_t)(addr - gameObjBase);
                        Log::Write("FF8Addresses:   FOUND %s at abs=0x%08X (gameObj%+d / 0x%X)",
                                   names[i], addr, relOff, (uint32_t)(addr - gameObjBase));
                        foundCount++;
                    }
                }
            }
        }
        __except(EXCEPTION_EXECUTE_HANDLER) {
            Log::Write("FF8Addresses:   (scan stopped at unreadable memory)");
        }
        
        if (foundCount == 0) {
            Log::Write("FF8Addresses:   NO matches found in scan range 0x%08X - 0x%08X", scanStart, scanEnd);
            Log::Write("FF8Addresses:   The game object may not store these function pointers,");
            Log::Write("FF8Addresses:   or the game object base (0x%08X) may be wrong.", gameObjBase);
        }
        Log::Write("FF8Addresses: === END DIAGNOSTIC ===");
    }
    
    if (!s_deferredScanAttempted) {
        s_deferredScanAttempted = true;
    }
    
    return false;
}

// ============================================================================
// v05.00: Player entity and triangle helpers
// ============================================================================

// ff8_field_state_other layout offsets (from FFNx ff8.h):
//   ff8_field_state_common: 0x000 - 0x187 (0x188 bytes)
//   current_triangle_id (WORD) at 0x188 + 0x72 = 0x1FA
//     (0x72 = 114 bytes of gap1 before current_triangle_id)
//   model_id (INT16) at 0x188 + 0x90 = 0x218
//   setpc (BYTE) at 0x188 + 0xCD = 0x255

static const uint32_t ENTITY_OFFSET_TRIANGLE_ID = 0x1FA; // WORD: walkmesh triangle
static const uint32_t ENTITY_OFFSET_SETPC       = 0x255; // BYTE: 1 = player character

// Get the base address of the flat entity state array.
// pFieldStateOthers is a global POINTER to the array base — must be dereferenced once.
// Entity stride is 0x264 bytes (from FFNx field.cpp: 0x264 * i).
static const uint32_t ENTITY_STRIDE = 0x264;

static uint8_t* GetEntityBlock(int entityIndex)
{
    if (!HasFieldStateArrays()) return nullptr;
    if (entityIndex < 0 || entityIndex >= (int)*pFieldStateOtherCount) return nullptr;
    // pFieldStateOthers is the address of a game global holding a pointer to the flat array.
    // pFieldStateOthers[0] (i.e. *(uint32_t*)pFieldStateOthers) is the flat array base pointer.
    // This matches FFNx: *(uint32_t*)ff8_externals.field_state_others + 0x264 * i
    uint8_t* base = reinterpret_cast<uint8_t*>(
        *reinterpret_cast<uint32_t*>(pFieldStateOthers));
    if (base == nullptr) return nullptr;
    return base + ENTITY_STRIDE * entityIndex;
}

int GetPlayerEntityIndex()
{
    if (!HasFieldStateArrays()) return -1;
    if (*pGameMode != MODE_FIELD) return -1;

    uint8_t count = *pFieldStateOtherCount;
    for (int i = 0; i < (int)count; i++) {
        uint8_t* block = GetEntityBlock(i);
        if (block == nullptr) continue;
        uint8_t setpc = *(block + ENTITY_OFFSET_SETPC);
        if (setpc != 0) return i;
    }
    return -1;
}

uint16_t GetEntityTriangleId(int entityIndex)
{
    uint8_t* block = GetEntityBlock(entityIndex);
    if (block == nullptr) return 0xFFFF;
    return *(uint16_t*)(block + ENTITY_OFFSET_TRIANGLE_ID);
}

}  // namespace FF8Addresses
