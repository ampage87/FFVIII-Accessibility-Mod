// menu_tts.h - In-game menu TTS for FF8 Accessibility Mod
//
// v07.00: MENUDIAG diagnostic — logs menu_draw_text calls, polls menu state
//         variables, and dumps menu_callbacks array to identify cursor tracking.
//
// Architecture:
//   The in-game menu (Triangle button) uses FF8_MODE_MENU (game mode 6).
//   The menu_callbacks[] array holds function pointers for each submenu.
//   Index 16 = top-level main menu (render + controller).
//   main_menu_controller is a massive function (~0x1000 bytes) that handles
//   all top-level cursor input and dispatches to submenus.
//
//   pMenuStateA (WORD*) and pMenuStateB (DWORD*) are the first two memory
//   reads in main_menu_controller. One of them likely holds the cursor index.
//
// This diagnostic build will:
//   1. Resolve menu_callbacks array and log all 28 entries
//   2. Hook menu_draw_text to log every string drawn during menu mode
//   3. Poll pMenuStateA, pMenuStateB, and nearby memory for cursor changes
//   4. Detect game mode transitions to/from MODE_MENU (6)

#pragma once

namespace MenuTTS {

// Called once during startup (after MinHook init, before MH_EnableHook).
void Initialize();

// Called every frame from AccessibilityThread.
void Update();

}  // namespace MenuTTS
