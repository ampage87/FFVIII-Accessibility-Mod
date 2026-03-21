// ff8_addresses.h - Runtime address resolution for FF8 Original PC (Steam 2013)
//
// Uses the same offset-chain technique as FFNx to resolve memory addresses
// at runtime. All addresses are computed from the known entry point for each
// game version, following CALL/JMP instruction chains through the executable.
//
// No ASLR on this 32-bit executable, so once resolved, addresses are stable
// for the lifetime of the process.

#pragma once

#include <windows.h>
#include <cstdint>

namespace FF8Addresses {

// Must be called once at startup (after exe is loaded).
// Returns true if all critical addresses were resolved.
bool Resolve();

// --- Resolved addresses (populated by Resolve()) ---

// Pointer to WORD holding the current game mode.
// Compare against GameMode values below.
extern WORD* pGameMode;

// Candidate cursor/state variables found at the start of main_menu_controller.
// These are the first two memory reads the function performs.
// pMenuStateA: WORD read at main_menu_controller+0x08 operand
// pMenuStateB: DWORD read at main_menu_controller+0x0E operand
extern WORD*    pMenuStateA;   // candidate cursor position (WORD at 0x01D76A9A on US NV)
extern uint32_t* pMenuStateB;  // candidate menu state (DWORD at 0x01D76A9C on US NV)

// Title screen cursor position (BYTE at pMenuStateA + 0x1F6).
// Values: 0=New Game, 1=Continue, 2=Credits.
// Discovered via diag6b memory sweep. Only valid when title screen is active.
extern uint8_t* pTitleCursorPos;

// Pointer to the main_menu_controller function address.
extern uint32_t main_menu_controller;

// Pointer to the main_menu_enter function address.
extern uint32_t main_menu_enter;

// Pointer to the main_menu_main_loop function address.
extern uint32_t main_menu_main_loop;

// Pointer to the main_loop function address (the giant switch).
extern uint32_t main_loop;

// Address of the go_to_main_menu_main_loop function.
extern uint32_t go_to_main_menu;

// Pointer to the game's "current mode-0 loop handler" function pointer.
// During intro FMV this points to pubintro_main_loop, during credits
// to credits_main_loop, and during the title menu to main_menu_main_loop.
// nullptr if we couldn't extract it from go_to_main_menu's instruction encoding.
extern uint32_t* pMode0LoopHandler;

// v01.13: Direct pointer to game_loop_obj.main_loop inside the game object.
// This is the live function pointer that changes as the game transitions
// between pubintro_main_loop -> credits_main_loop -> main_menu_main_loop.
// Resolved at runtime by computing offset 0xA1C within the game object
// (game_loop_obj starts at +0xA0C, main_loop is at +0x10 within main_obj).
// Validated against pubintro_main_loop at startup.
extern uint32_t* pGameLoopMainLoop;

// Known function addresses for comparison with *pMode0LoopHandler.
extern uint32_t pubintro_main_loop;
extern uint32_t credits_main_loop;

// Address of the global that holds the game object pointer.
// main_loop loads ESI from this address: MOV ESI, [pGameObjGlobal]
extern uint32_t pGameObjGlobal;

// --- FMV / Movie detection (v01.12) ---
// Base address of the ff8_movie_obj struct in game memory.
// Resolved via: opcode_movieready -> prepare_movie -> movie_object.
extern uint32_t movieObjectAddr;

// Direct pointer to movie_is_playing (DWORD) at movie_object + 0x4C4A8.
// 0 = no movie playing, non-zero = movie is active.
extern uint32_t* pMovieIsPlaying;

// Direct pointer to movie_intro_pak (DWORD) at movie_object + 0x4C4A4.
// Index into the disc_pak_filenames array identifying which movie is loaded.
extern uint32_t* pMovieIntroPak;

// Direct pointer to movie_current_frame (WORD) at movie_object + 0x00.
extern WORD* pMovieCurrentFrame;

// Direct pointer to movie_total_frames (WORD) at movie_object + 0x02.
extern WORD* pMovieTotalFrames;

// Pointer to the disc_pak_filenames array (char**).
// Resolved from prepare_movie + 0xB2. Indexed by movie_intro_pak.
extern char** discPakFilenames;

// Mode-0 init flag (WORD*). When non-zero, mode-0 initialization is complete.
// Extracted from main_loop: CMP WORD [addr], 0
extern WORD* pMode0InitFlag;

// Mode-0 phase byte. Controls which sub-phase runs within mode 0.
// Value 4 = credits. Other values TBD via runtime logging.
// Extracted from main_loop: CMP BYTE [addr], 4
extern uint8_t* pMode0Phase;

// Pointer to WORD holding the current field ID (when mode==1).
// Resolved from main_loop+0x21F, same as FFNx's current_field_id.
extern WORD* pCurrentFieldId;

// Pointer to char buffer holding the current field name string.
// Resolved from opcode_effectplay2+0x75, same as FFNx's current_field_name.
extern char* pCurrentFieldName;

// --- Game mode constants ---
// These are the RAW values stored at *pGameMode.
// Note: FFNx's MODE_MAIN_MENU=200 is a driver_mode, not the raw value.
// The title/main menu screen uses raw mode 0 in the game's memory.
enum GameMode : uint16_t {
    MODE_INITIAL    = 0,    // Intro, title screen, credits (raw value)
    MODE_FIELD      = 1,
    MODE_WORLDMAP   = 2,
    MODE_SWIRL      = 3,
    MODE_AFTER_BATTLE = 4,
    MODE_5          = 5,
    MODE_MENU       = 6,
    MODE_7          = 7,
    MODE_CARDGAME   = 8,
    MODE_9          = 9,
    MODE_TUTO       = 10,
    MODE_11         = 11,
    MODE_INTRO      = 12,
    MODE_BATTLE     = 999,
};

// Convenience: is the game in the initial/title/credits state?
// Note: mode 0 is shared between intro FMV, title screen, and credits.
// Callers should use additional context (timing, menu state vars) to
// distinguish these sub-states.
inline bool IsOnInitialMode() {
    return pGameMode != nullptr && *pGameMode == MODE_INITIAL;
}

// Convenience: is the game in the field module?
inline bool IsOnField() {
    return pGameMode != nullptr && *pGameMode == MODE_FIELD;
}

// Convenience: is the game in tutorial/thought mode?
inline bool IsOnTuto() {
    return pGameMode != nullptr && *pGameMode == MODE_TUTO;
}

// Convenience: is the title menu actually active (not FMV or credits)?
// Returns true only when mode==0 AND the current loop handler is
// main_menu_main_loop.  Checks pGameLoopMainLoop first (v01.13),
// falls back to pMode0LoopHandler, returns false if neither resolved.
inline bool IsTitleMenuActive() {
    if (pGameMode == nullptr || *pGameMode != MODE_INITIAL)
        return false;
    // v01.13: prefer game object polling (works for engine-rendered intros)
    if (pGameLoopMainLoop != nullptr)
        return (*pGameLoopMainLoop == main_menu_main_loop);
    if (pMode0LoopHandler != nullptr)
        return (*pMode0LoopHandler == main_menu_main_loop);
    return false;  // couldn't resolve — caller should use fallback
}

// What is the current mode-0 loop handler? Returns 0 if not resolved.
inline uint32_t GetCurrentLoopHandler() {
    if (pGameLoopMainLoop != nullptr)
        return *pGameLoopMainLoop;
    if (pMode0LoopHandler != nullptr)
        return *pMode0LoopHandler;
    return 0;
}

// v01.13: Deferred resolution of game_loop_obj.main_loop.
// Call from polling loop. Returns true once resolved (no-op after success).
bool TryResolveDeferredGameLoop();

// Was the handler pointer successfully resolved?
inline bool HasMode0HandlerPtr() {
    return pMode0LoopHandler != nullptr || pGameLoopMainLoop != nullptr;
}

// Is an FMV currently playing?
inline bool IsMoviePlaying() {
    return pMovieIsPlaying != nullptr && *pMovieIsPlaying != 0;
}

// Was the movie object successfully resolved?
inline bool HasMovieObject() {
    return pMovieIsPlaying != nullptr;
}

// Get current mode value, or 0xFFFF if not resolved.
inline uint16_t GetCurrentMode() {
    return pGameMode ? *pGameMode : 0xFFFF;
}

// Get menu state A (WORD), or 0xFFFF if not resolved.
inline uint16_t GetMenuStateA() {
    return pMenuStateA ? *pMenuStateA : 0xFFFF;
}

// Get menu state B (DWORD), or 0xFFFFFFFF if not resolved.
inline uint32_t GetMenuStateB() {
    return pMenuStateB ? *pMenuStateB : 0xFFFFFFFF;
}

// Get title screen cursor position (BYTE), or 0xFF if not resolved.
inline uint8_t GetTitleCursorPos() {
    return pTitleCursorPos ? *pTitleCursorPos : 0xFF;
}

// --- v04.00: Field dialog opcode hooks ---

// The JSM opcode dispatch table (array of function pointers).
// Each entry is the address of the handler for that opcode index.
// Resolved from update_field_entities + 0x65A.
extern uint32_t* pExecuteOpcodeTable;

// Opcode handler addresses (read from pExecuteOpcodeTable at init).
extern uint32_t opcode_mesw;      // 0x46 — display dialog message + wait
extern uint32_t opcode_mes;       // 0x47 — display dialog message
extern uint32_t opcode_messync;   // 0x48 — wait for dialog dismiss
extern uint32_t opcode_ask;       // 0x4A — display dialog with choices
extern uint32_t opcode_winclose;  // 0x4C — close dialog window
extern uint32_t opcode_amesw;     // 0x64 — auto-positioning MES variant
extern uint32_t opcode_ames;      // 0x65 — auto-positioning MES variant
extern uint32_t opcode_aask;      // 0x6F — auto-positioning ASK variant

// field_get_dialog_string: called from opcode_mes+0x5D.
// Returns char* to the raw FF8-encoded dialog text for the current message.
extern uint32_t field_get_dialog_string;

// set_window_object: called from opcode_mes+0x66.
// Configures a dialog window in the windows array.
extern uint32_t set_window_object;

// ff8_win_obj windows array: active dialog window objects.
// Resolved from set_window_object+0x11.
// Each element is sizeof(ff8_win_obj) = 0x38 bytes.
extern uint8_t* pWindowsArray;

// World map dialog functions (char* text passed as parameter).
extern uint32_t world_dialog_assign_text;

// v04.17: show_dialog — universal text rendering function.
// Signature: char show_dialog(int32_t window_id, uint32_t state, int16_t a3)
// Resolved via: sub_4A0880 -> sub_4A0C00 -> show_dialog (CALL at +0x5F).
// ALL dialog text (field, battle, world, tutorial/thoughts) passes through this.
extern uint32_t show_dialog_addr;

// Intermediate addresses in the show_dialog resolution chain.
extern uint32_t sub_4A0880;
extern uint32_t sub_4A0C00;

// opcode_tuto (dispatch table index 0x177) — triggers tutorial/thought overlay.
// Sets game mode to MODE_TUTO (10). Used for Squall's internal thoughts.
extern uint32_t opcode_tuto;

// current_tutorial_id (BYTE*) — which tutorial/thought is active.
// Resolved from opcode_tuto + 0x2A.
extern uint8_t* pCurrentTutorialId;

// Convenience: is the execute_opcode_table resolved?
inline bool HasOpcodeTable() {
    return pExecuteOpcodeTable != nullptr;
}

// v04.21: additional dialog-related opcodes
extern uint32_t opcode_mesmode;   // 0x106 — set message display mode (boxed/borderless)
extern uint32_t opcode_ramesw;    // 0x116 — remote AMESW (entity triggers dialog on another)

// v04.25: naming screen bypass
extern uint32_t opcode_menuname;  // 0x129 — open character naming screen

// v05.56: SETLINE/LINEON/LINEOFF for trigger zone detection
extern uint32_t opcode_setline;   // 0x039 — define trigger line coordinates
extern uint32_t opcode_lineon;    // 0x03A — enable trigger line
extern uint32_t opcode_lineoff;   // 0x03B — disable trigger line

// v05.78: TALKRADIUS/PUSHRADIUS for interaction distance detection
extern uint32_t opcode_talkradius; // 0x062 — set talk interaction radius
extern uint32_t opcode_pushradius; // 0x063 — set push/collision radius

// v0.08.03: SET3 opcode for runtime position capture of PSHM_W entities
extern uint32_t opcode_set3;       // 0x01E — set entity position (X, Y, Z, triangle)

// v0.08.07: PSHM_W opcode for shared memory read diagnostics
extern uint32_t opcode_pshm_w;     // 0x00C — push shared memory word (unsigned)

// v04.28+: engine input button state variables.
// Both are written by engine_eval_is_button_pressed each frame.
// confirmed_buttons = buttons just pressed this frame (edge-triggered).
// valid_buttons     = buttons currently held (level-triggered).
// Chain: pubintro_main_loop+0x4 -> +0x16 -> +0x4A6 -> abs@+0x62 / +0x3C
extern uint32_t* pEngineInputConfirmedButtons;  // abs @ engine_eval_is_button_pressed+0x62
extern uint32_t* pEngineInputValidButtons;      // abs @ engine_eval_is_button_pressed+0x3C

// v04.30: engine_eval_keyboard_gamepad_input — processes raw hardware into button states.
// Hooked to inject confirm bits AFTER hardware state is built, in the main game thread,
// so values are fresh when the naming screen's confirm-check reads them.
// Chain: pubintro_main_loop+0x4 -> engine_eval_process_input+0x16
extern uint32_t engine_eval_keyboard_gamepad_input_addr;

// v05.82: Gamepad state struct for analog joystick steering.
// ff8_gamepad_state contains ff8_gamepad_vibration_state[2] (one per port),
// each containing ff8_gamepad_vibration_state_entry[8] with analog_lx/analog_ly
// (uint8_t, 128=center, 0=left/up, 255=right/down).
// Resolved via: check_game_is_paused -> pause_menu_with_vibration
//               -> get_vibration_capability -> gamepad_states
// Layout from FFNx ff8.h:
//   state_by_port[0].entries[entries_offset].analog_lx/analog_ly
//   entries_offset = state_by_port[0].entries_offset (uint8_t at +0x18)
extern uint8_t* pGamepadStates;  // base of ff8_gamepad_state struct

// v05.84: DirectInput gamepad device/state pointers for fake-gamepad injection.
// dinput_gamepad_device is a POINTER TO a LPDIRECTINPUTDEVICE8A.
// If the VALUE at this address is null, the game skips gamepad analog processing.
// dinput_gamepad_state is a POINTER TO a LPDIJOYSTATE2.
// The game reads lX/lY from the struct pointed to by this address.
// Resolved from: engine_eval_keyboard_gamepad_input+0x1B -> dinput_update_gamepad_status
//   -> +0x16 = dinput_gamepad_device, +0x1B = dinput_gamepad_state
extern uint32_t* pDinputGamepadDevicePtr;   // address of the device pointer variable
extern uint32_t* pDinputGamepadStatePtr;    // address of the DIJOYSTATE2 pointer variable
extern uint32_t  dinput_update_gamepad_status_addr;  // function address for hook

// Convenience: is the gamepad state struct resolved?
inline bool HasGamepadStates() {
    return pGamepadStates != nullptr;
}

// Convenience: are the DirectInput gamepad pointers resolved?
inline bool HasDinputGamepadPtrs() {
    return pDinputGamepadDevicePtr != nullptr && pDinputGamepadStatePtr != nullptr;
}

// v05.88: DirectInput keyboard buffer pointer for arrow key suppression.
// keyboard_state is a byte** — *pKeyboardState points to the 256-byte
// DirectInput keyboard buffer. Each byte at scan code index N is 0x80 if
// key N is pressed, 0x00 otherwise.
// Resolved via: sub_4767B0+0x156 -> ctrl_keyboard_actions
//               -> +0x5 -> get_key_state
//               -> get_absolute_value(+0x27) -> keyboard_state (byte**)
extern uint8_t** pKeyboardState;

// v05.89: get_key_state function address for hooking.
// This is the function that fills the keyboard buffer from hardware/FFNx.
// Called from ctrl_keyboard_actions+0x5 inside engine_eval_keyboard_gamepad_input.
// Hooking this lets us zero arrow keys AFTER the buffer is filled but BEFORE
// ctrl_keyboard_actions reads direction from it.
extern uint32_t get_key_state_addr;

// Convenience: is the keyboard state buffer resolved?
inline bool HasKeyboardState() {
    return pKeyboardState != nullptr;
}

// Convenience: is get_key_state hookable?
inline bool HasGetKeyState() {
    return get_key_state_addr != 0;
}

// v04.22: update_field_entities — script interpreter main loop
extern uint32_t update_field_entities_addr;

// v04.20: menu_draw_text — low-level text rendering function.
// Draws individual characters/strings. Called from menu system but
// potentially also from field thought rendering. Resolved via:
// sub_497380 -> sub_4B3410 -> sub_4BE4D0 -> sub_4BECC0 -> menu_draw_text
extern uint32_t menu_draw_text_addr;

// Convenience: is show_dialog resolved?
inline bool HasShowDialog() {
    return show_dialog_addr != 0;
}

// get_character_width: per-glyph function called for EVERY rendered character.
// Signature: uint32_t __cdecl get_character_width(uint32_t charCode)
// Resolved from menu_draw_text + 0x1D0 (US) / 0x1E1 (JP).
extern uint32_t get_character_width_addr;

// Convenience: is menu_draw_text resolved?
inline bool HasMenuDrawText() {
    return menu_draw_text_addr != 0;
}

// Convenience: is get_character_width resolved?
inline bool HasGetCharWidth() {
    return get_character_width_addr != 0;
}

// --- v05.03: Field scripts init hook address ---
// Stored for MinHook installation in FieldNavigation::Initialize().
extern uint32_t field_scripts_init_addr;
extern uint32_t read_field_data_addr;

// --- v05.09: INF walkmesh data pointer ---
// Points to the global that holds a pointer to the current field's loaded
// INF walkmesh triangle table. Populated by the field loader on each field
// transition. nullptr until address chain is resolved.
// INF triangle layout: int16_t x[3], y[3], z[3] per triangle (+ flags).
// Triangle center = average of 3 vertex (x,z) pairs.
extern uint8_t** pFieldInfData;

// --- v05.10: set_current_triangle_sub_45E160 ---
// Resolved via: sub_4767B0 -> sub_472B30 -> sub_530810 -> sub_533CD0
//               -> sub_530C30 -> field_push_mch -> set_current_triangle
// Signature (FFNx): void __cdecl set_current_triangle(int, int, int)
// Called every time an entity moves to a new walkmesh triangle.
// Hooking this gives us a direct window into triangle data and the INF buffer.
extern uint32_t set_current_triangle_addr;

// --- v05.00: Field entity state arrays (for player triangle tracking) ---
//
// Resolved chain: main_loop -> field_main_loop (0x144)
//                           -> sub_471F70 (0x148)
//                           -> read_field_data (0x23A)
//                           -> field_scripts_init (0xE49 US / 0xEDC JP)
//                           -> pFieldStateOtherCount (0x2C4)
//                           -> pFieldStateOthers (0x62E)
//
// Within each entity state block (ff8_field_state_other from FFNx ff8.h):
//   offset 0x1FA (WORD)   : current_triangle_id -- walkmesh triangle entity stands on
//   offset 0x218 (INT16)  : model_id            -- 3D model slot index for this entity
//   offset 0x255 (BYTE)   : setpc               -- non-zero if this IS the player character

// Byte count of 'other' entities loaded in the current field.
// Entity 0 is usually the background script; player is the one with setpc != 0.
extern uint8_t*  pFieldStateOtherCount;

// Pointer to the array of entity state block pointers.
// pFieldStateOthers[i] points to the raw state block for entity i.
// Each block is ~0x280 bytes; use byte offsets above to read fields.
extern uint8_t** pFieldStateOthers;

// Convenience: returns the player entity index (first entity with setpc != 0).
// Returns -1 if not in field mode or arrays not resolved.
int GetPlayerEntityIndex();

// Convenience: returns the current triangle ID for a given entity index.
// Returns 0xFFFF if the index is out of range or arrays not resolved.
uint16_t GetEntityTriangleId(int entityIndex);

// --- v05.50: Background entity state array (the "other" half of entities) ---
// Background entities are script-only (no 3D model/walkmesh position).
// They include interactive objects, NPCs without models, triggers, doors.
// Stride = 0x1B4 bytes (ff8_field_state_background from FFNx ff8.h).
// Common struct (0x188 bytes) shared with "other" entities.
extern uint8_t*  pFieldStateBackgroundCount;
extern uint8_t** pFieldStateBackgrounds;

// --- v0.07.24: Music volume function address (for hooking) ---
// This is the game's set_midi_volume function, which FFNx replaces with
// set_music_volume_for_channel. Resolved from main_loop -> sm_battle_sound.
extern uint32_t pSetMidiVolume;  // game address of set_midi_volume

// --- v0.07.25: Save file read function address (for hooking) ---
// Signature: uint32_t sm_pc_read(char* filename, void* buffer)
// Resolved from main_loop + 0x9C. Called by the game when loading save files
// from disk (e.g. when opening the save/load screen).
extern uint32_t sm_pc_read_addr;

// Convenience: are the entity state arrays resolved?
inline bool HasFieldStateArrays() {
    return pFieldStateOtherCount != nullptr && pFieldStateOthers != nullptr;
}

// Convenience: are the background entity arrays resolved?
inline bool HasFieldStateBackgrounds() {
    return pFieldStateBackgroundCount != nullptr && pFieldStateBackgrounds != nullptr;
}

}  // namespace FF8Addresses
