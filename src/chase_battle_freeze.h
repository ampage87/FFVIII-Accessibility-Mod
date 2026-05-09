// chase_battle_freeze.h — opcode_battle hook for chase manual mode.
//
// v0.15.1: New module. Hooks JSM opcode 0x69 (BATTLE) so that, in manual
// chase mode, the second-and-later kani-driven BATTLE call per chase
// field becomes a no-op. This caps the X-ATM092 chase at one battle per
// field, sidestepping the post-knockdown WAIT timer entirely.
//
// Pass-through cases:
//   - Chase mode is auto (caller passes through; auto-drive handles
//     freezing kani contact via its own logic when implemented).
//   - Player is not in a chase field.
//   - Calling entity is not kani (e.g. random encounter from a
//     non-chase background script).
//   - First battle in this chase field hasn't happened yet (count<1).
//
// Freeze behavior: when all gates are met, the hook returns 3 (the
// JSM-VM "advance to next opcode" code) without invoking the original
// BATTLE handler. The script proceeds past BATTLE as if a battle had
// just resolved with no effect. Risk: if the script depends on BATTLE
// pushing a return value to its stack (we haven't disassembled BATTLE
// to confirm), the post-BATTLE code path may misbehave. v0.15.1 BAT
// will surface that if it happens.

#pragma once

namespace ChaseBattleFreeze {

// One-time setup. Resolves opcode_battle (pExecuteOpcodeTable[0x69])
// and installs a MinHook detour. Idempotent.
void Initialize();

// Cleanup. Disables the hook.
void Shutdown();

}  // namespace ChaseBattleFreeze
