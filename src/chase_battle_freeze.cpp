// chase_battle_freeze.cpp — opcode_battle hook for chase manual mode.
// See chase_battle_freeze.h for the public design notes.
//
// v0.15.1: New module. The opcode_battle handler address is read from
// pExecuteOpcodeTable[0x69] at Initialize. We install a MinHook detour
// that gates on chase_detector state:
//
//   if (mode==auto || !chaseField || !isKani || battleCount<1)
//       return s_origBattle(entityPtr);
//   else
//       return 3;  // advance the script-VM past BATTLE without firing
//
// The "calling entity is kani" check uses pointer equality between
// entityPtr (which the JSM-VM passes to opcode handlers — confirmed in
// field_dialog.cpp's existing handlers) and ChaseDetector's cached
// kani entity block address (resolved from JSMCounts + SYM lookup).

#include "chase_battle_freeze.h"
#include "chase_detector.h"
#include "ff8_accessibility.h"
#include "ff8_addresses.h"
#include "mod_forward_decls.h"
#include "minhook/include/MinHook.h"

#include <windows.h>
#include <cstdio>

namespace ChaseBattleFreeze {

// JSM-VM "advance to next opcode" return code. Used by most field
// opcodes to indicate "I'm done, move on".
static const int JSM_RC_ADVANCE = 3;

// JSM opcode index for BATTLE. Per the v0.15.1 plan and FFNx ff8_data,
// execute_opcode_table[0x69] is opcode_battle.
static const int OPCODE_BATTLE_INDEX = 0x69;

typedef int (__cdecl *OpcodeHandler_t)(int);

// ============================================================================
// State
// ============================================================================

static bool             s_initialized = false;
static OpcodeHandler_t  s_origBattle  = nullptr;
static uint32_t         s_battleAddr  = 0;
static volatile LONG    s_passThruCount = 0;
static volatile LONG    s_freezeCount   = 0;

// ============================================================================
// Hook
// ============================================================================

static int __cdecl Hook_opcode_battle(int entityPtr)
{
    // Fast bail-out: hook overhead is one comparison + a couple of
    // function calls; cheap enough to leave installed at all times.
    bool freeze = false;
    int  battleCount = 0;
    bool inChase = ChaseDetector::IsInChaseField();
    bool kaniCaller = false;
    ChaseDetector::Mode mode = ChaseDetector::GetChaseMode();

    if (mode == ChaseDetector::MODE_MANUAL && inChase) {
        battleCount = ChaseDetector::GetCurrentFieldBattleCount();
        kaniCaller  = ChaseDetector::IsKaniEntityPtr((uintptr_t)entityPtr);
        if (kaniCaller && battleCount >= 1) {
            freeze = true;
        }
    }

    if (freeze) {
        InterlockedIncrement(&s_freezeCount);
        Log::Field("ChaseBattleFreeze: NO-OP kani BATTLE in '%s' "
                   "(battleCount=%d, freeze#%ld) — returning %d",
                   ChaseDetector::GetDebouncedFieldName(),
                   battleCount, (long)s_freezeCount, JSM_RC_ADVANCE);
        return JSM_RC_ADVANCE;
    }

    // Pass-through. Log occasionally so we know the hook is alive.
    LONG passN = InterlockedIncrement(&s_passThruCount);
    if (passN <= 3 || (passN % 50) == 0) {
        Log::Field("ChaseBattleFreeze: pass-through #%ld "
                   "(mode=%s inChase=%d kaniCaller=%d count=%d)",
                   passN, ChaseDetector::ChaseModeName(mode),
                   (int)inChase, (int)kaniCaller, battleCount);
    }
    return s_origBattle(entityPtr);
}

// ============================================================================
// Public API
// ============================================================================

void Initialize()
{
    if (s_initialized) return;

    if (!FF8Addresses::pExecuteOpcodeTable) {
        Log::Mod("ChaseBattleFreeze: pExecuteOpcodeTable not resolved; skipping");
        return;
    }

    __try {
        s_battleAddr = FF8Addresses::pExecuteOpcodeTable[OPCODE_BATTLE_INDEX];
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        s_battleAddr = 0;
    }
    if (s_battleAddr == 0) {
        Log::Mod("ChaseBattleFreeze: opcode_battle (0x%02X) address read failed; "
                 "skipping", OPCODE_BATTLE_INDEX);
        return;
    }

    MH_STATUS st = MH_CreateHook(
        (LPVOID)s_battleAddr,
        (LPVOID)&Hook_opcode_battle,
        (LPVOID*)&s_origBattle);
    if (st != MH_OK) {
        Log::Mod("ChaseBattleFreeze: MH_CreateHook failed for opcode_battle "
                 "at 0x%08X (status=%d)", s_battleAddr, (int)st);
        return;
    }

    Log::Mod("ChaseBattleFreeze: Initialized (opcode_battle hooked at 0x%08X, "
             "trampoline=0x%08X). Hook activated by global MH_EnableHook(ALL).",
             s_battleAddr, (uint32_t)(uintptr_t)s_origBattle);
    s_initialized = true;
}

void Shutdown()
{
    if (!s_initialized) return;
    if (s_battleAddr != 0) {
        MH_DisableHook((LPVOID)s_battleAddr);
        MH_RemoveHook((LPVOID)s_battleAddr);
    }
    s_origBattle = nullptr;
    s_battleAddr = 0;
    s_initialized = false;
    Log::Mod("ChaseBattleFreeze: Shutdown.");
}

}  // namespace ChaseBattleFreeze
