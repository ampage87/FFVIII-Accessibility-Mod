// name_bypass.h - Auto-bypass character/GF naming screens for blind players
//
// Detours opcode_menuname (0x129) via MinHook. When the naming screen fires,
// auto-injects a confirm key press so the game accepts the default name and
// continues without requiring sighted interaction.
//
// UX:
//   - Speak "Naming screen bypassed" once per screen on success.
//   - Hold SHIFT during the naming trigger to opt out and interact manually.
//   - F9 hotkey toggles the bypass on/off at runtime.
//
// v04.26: Initial implementation.

#pragma once

namespace NameBypass {

    // Call once after MH_Initialize() and opcode table resolution.
    bool Initialize();

    // Call once at shutdown before MH_Uninitialize().
    void Shutdown();

    // Call every frame from the accessibility thread main loop.
    void Update();

}  // namespace NameBypass
