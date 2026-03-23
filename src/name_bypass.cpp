// name_bypass.cpp - Auto-bypass character/GF naming screens
//
// Strategy (AutoAccept):
//   1. Detour opcode_menuname (0x129). Call original first to preserve all
//      side effects (name buffer init, state flags, save buffer setup).
//   2. Set a pending flag + deadline. Update() injects VK_RETURN while the
//      naming UI is active (MODE_MENU) and the deadline hasn't expired.
//   3. Clear pending when the game leaves MODE_MENU or deadline passes.
//
// Opt-out: hold SHIFT when the naming screen fires — no injection occurs.
// Toggle:  F9 enables/disables the bypass at runtime.
//
// v04.26: Initial implementation.

#include "name_bypass.h"
#include "ff8_accessibility.h"
#include "ff8_addresses.h"
#include "minhook/include/MinHook.h"
#include <windows.h>

namespace NameBypass {

// ============================================================================
// Internal state
// ============================================================================

using OpcodeHandler_t = int (__cdecl *)(int);

static OpcodeHandler_t s_origMenuname = nullptr;
static bool s_initialized  = false;
static bool s_enabled      = true;   // toggled by F9
static bool s_pending      = false;  // confirm injection needed
static DWORD s_deadline    = 0;      // GetTickCount() deadline for injection
static DWORD s_lastInject  = 0;      // throttle: time of last keybd_event
static bool s_announced    = false;  // spoke "Naming screen bypassed" this session
static long s_sessionId    = 0;      // increments each time MENUNAME fires

// How long we keep injecting after MENUNAME fires (ms).
static const DWORD INJECT_WINDOW_MS  = 2000;
// Minimum gap between confirm key presses (ms).
static const DWORD INJECT_THROTTLE_MS = 250;

// ============================================================================
// MENUNAME opcode detour
// ============================================================================

static int __cdecl Hooked_Menuname(int entityPtr)
{
    long session = InterlockedIncrement(&s_sessionId);

    uint16_t mode = FF8Addresses::GetCurrentMode();
    const char* field = FF8Addresses::pCurrentFieldName ? FF8Addresses::pCurrentFieldName : "?";

    Log::Write("NameBypass: MENUNAME enter entity=0x%08X mode=%u field=%s session=%ld",
               (unsigned)entityPtr, (unsigned)mode, field, session);

    // If bypass is off or SHIFT is held, call original and let player name.
    if (!s_enabled) {
        Log::Write("NameBypass: bypass disabled, passing through session=%ld", session);
        return s_origMenuname ? s_origMenuname(entityPtr) : 3;
    }
    if (GetAsyncKeyState(VK_SHIFT) & 0x8000) {
        Log::Write("NameBypass: SHIFT held, opt-out session=%ld", session);
        return s_origMenuname ? s_origMenuname(entityPtr) : 3;
    }

    // Bypass strategy: Call the original handler to preserve all side effects
    // (character init, script state, junction setup), then immediately clear
    // the UI-open flag to prevent the naming screen from appearing.
    // v04.35 documented: 0x01CE490B = naming UI open flag.
    int ret = 0;
    if (s_origMenuname) {
        ret = s_origMenuname(entityPtr);
    }
    
    // Force-close the naming UI by clearing the open flag
    __try {
        volatile uint8_t* uiFlag = (volatile uint8_t*)0x01CE490BU;
        uint8_t flagBefore = *uiFlag;
        *uiFlag = 0;
        Log::Write("NameBypass: Cleared UI flag (was %u) session=%ld", (unsigned)flagBefore, session);
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        Log::Write("NameBypass: SEH clearing UI flag session=%ld", session);
    }
    
    Log::Write("NameBypass: Bypassed naming screen (orig ret=%d) session=%ld", ret, session);
    ScreenReader::Speak("Naming screen bypassed", false);

    return 3;  // script advance
}

// ============================================================================
// Initialize / Shutdown
// ============================================================================

bool Initialize()
{
    // v0.09.04: Naming bypass is now handled by field_dialog.cpp's Hook_opcode_menuname
    // (restored v04.35 approach minus enableGF). This module is disabled to avoid
    // dual MinHook conflicts on the same target.
    Log::Write("NameBypass: Disabled — bypass handled by field_dialog.cpp (v0.09.04)");
    s_initialized = false;
    return true;
}

void Shutdown()
{
    if (!s_initialized) return;
    uint32_t target = FF8Addresses::opcode_menuname;
    if (target != 0) {
        MH_RemoveHook(reinterpret_cast<LPVOID>(target));
    }
    s_origMenuname = nullptr;
    s_initialized  = false;
    Log::Write("NameBypass: Shutdown.");
}

// ============================================================================
// Update — called every frame (~16ms) from the accessibility thread
// ============================================================================

void Update()
{
    if (!s_pending) return;

    DWORD now = GetTickCount();

    // Check for timeout.
    if (now > s_deadline) {
        Log::Write("NameBypass: pending timed out after %ums session=%ld",
                   INJECT_WINDOW_MS, s_sessionId);
        s_pending = false;
        return;
    }

    // Only inject while NOT on the field (mode 1). The naming screen
    // can use mode 6 (menu) or mode 10 (tutorial/naming). If the game
    // has returned to field mode, the naming screen is done.
    uint16_t mode = FF8Addresses::GetCurrentMode();
    if (mode == FF8Addresses::MODE_FIELD) {
        Log::Write("NameBypass: returned to field mode, clearing pending session=%ld",
                   s_sessionId);
        s_pending = false;
        return;
    }

    // Throttle key presses.
    if (now - s_lastInject < INJECT_THROTTLE_MS) return;

    // Inject a RETURN (confirm) key press.
    Log::Write("NameBypass: injected CONFIRM vk=VK_RETURN mode=%u t=%lu session=%ld",
               (unsigned)mode, now, s_sessionId);

    keybd_event(VK_RETURN, 0, 0, 0);
    keybd_event(VK_RETURN, 0, KEYEVENTF_KEYUP, 0);
    s_lastInject = now;

    // One-time spoken announcement.
    if (!s_announced) {
        ScreenReader::Speak("Naming screen bypassed", false);
        s_announced = true;
    }
}

}  // namespace NameBypass
