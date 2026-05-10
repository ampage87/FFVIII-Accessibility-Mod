// dialog_inject.cpp -- Mod-driven engine dialog injection (Phase 1).
// See dialog_inject.h for design notes.
//
// v0.15.4: Initial Phase 1. Synthesizes a phantom script_context and
// calls opcode_mes(&ctx) directly to prove the engine renders. If
// successful, Phase 2 (chase ASK via opcode_ask) follows the same
// pattern with different opcode dispatch index and arg layout.

#include "dialog_inject.h"
#include "ff8_accessibility.h"
#include "ff8_addresses.h"
#include "mod_forward_decls.h"

#include <windows.h>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace DialogInject {

// ============================================================================
// Constants
// ============================================================================

// Phantom script_context size. Sized generously: the disassembly shows
// opcode_ask writes word [esi+0x204] = 0, so the buffer must be at least
// 0x206 bytes. 0x300 gives margin for any reads beyond that we have not
// yet enumerated.
static const size_t PHANTOM_CTX_SIZE = 0x300;

// Script-VM stack pointer offset within the script_context (confirmed by
// disassembly of opcode_mes at 0x00528F20).
static const size_t CTX_SP_OFFSET = 0x184;

// Phase 1 test parameters. SP=2 means two args on the script-VM stack:
//   stack[SP]   = ctx[2*4] = ctx[8]  = msg_id  (top of stack, popped first)
//   stack[SP-1] = ctx[1*4] = ctx[4]  = slot index (popped second)
// opcode_mes reads:
//   edi = [ebx + eax*4]     where eax = SP
//   esi = [ebx + eax*4 - 4]
// then validates esi < pop_script_args() return value. With SP=2, this
// returns 2, so slot must be < 2. We pick slot 1 to leave slot 0 free
// for the engine's own foreground dialog.
static const int8_t  TEST_SP        = 2;
static const int32_t TEST_MSG_ID    = 0;     // every field has msg 0
static const int32_t TEST_SLOT      = 1;     // pWindowsArray[1]

// Phase 2a test parameters. SP=6 means six args on the script-VM stack.
// From the opcode_ask disassembly at 0x00529520:
//   stack[SP-5] (ctx[+0x04]) -> edi -> slot index   (CONFIRMED)
//   stack[SP-4] (ctx[+0x08]) -> ecx -> msg_id       (CONFIRMED via field_get_dialog_string)
//   stack[SP-3] (ctx[+0x0C]) -> edx -> SWO_ASK arg2 (clamp lower bound)
//   stack[SP-2] (ctx[+0x10]) -> ecx -> SWO_ASK arg3 (clamp upper bound, written to slot+0x29)
//   stack[SP-1] (ctx[+0x14]) -> ebp -> SWO_ASK arg4 (clamped value, written to slot+0x2A)
//   stack[SP-0] (ctx[+0x18]) -> ebx -> SWO_ASK arg5 (aux byte, slot-indexed)
// Phase 2a fires in doani1_2 (Aaron's BAT field) where msg 0 is the
// Selphie elevator ASK with two choice lines ("Go up" / "Stay"). Setting
// the cursor range to [2, 3] should land the cursor on those lines.
static const int8_t  TEST_SP_ASK     = 6;
static const int32_t TEST_SLOT_ASK   = 2;     // pWindowsArray[2] -- avoid Phase 1's slot 1
static const int32_t TEST_MSG_ID_ASK = 0;     // every field has msg 0
static const int32_t TEST_ASK_ARG2   = 1;     // clamp lower bound
static const int32_t TEST_ASK_ARG3   = 3;     // clamp upper bound / firstQ candidate
static const int32_t TEST_ASK_ARG4   = 2;     // clamped value / lastQ candidate
static const int32_t TEST_ASK_ARG5   = 2;     // aux byte / curQ candidate

// JSM dispatch table opcode indices (mirrors ff8_addresses.h).
static const int OPCODE_MES_INDEX   = 0x47;
static const int OPCODE_ASK_INDEX   = 0x4A;

// ff8_win_obj layout — only the fields Phase 1 polls.
static const size_t WIN_OBJ_STRIDE              = 0x3C;
static const size_t WIN_OBJ_OPEN_CLOSE_OFFSET   = 0x1C;  // int16_t
static const size_t WIN_OBJ_STATE_OFFSET        = 0x24;  // uint32_t
static const size_t WIN_OBJ_FIELD16_OFFSET      = 0x16;  // byte
static const size_t WIN_OBJ_VELOCITY_OFFSET     = 0x1E;  // int16_t

// gameObj bitmask offsets (informational; not the render trigger but
// useful as diagnostic state — see Field dialog system disassembly
// analysis.md follow-up section).
static const size_t GAMEOBJ_ASK_MASK_OFFSET     = 0xD2;
static const size_t GAMEOBJ_WIN_MASK_OFFSET     = 0xD3;
static const size_t GAMEOBJ_MES_MASK_OFFSET     = 0xD4;

// Slot-polling cadence and duration after a Phase 1 fire.
static const DWORD POLL_DURATION_MS = 3000;
static const DWORD POLL_INTERVAL_MS = 100;

// ============================================================================
// State
// ============================================================================

static bool   s_initialized   = false;

// Phantom context buffer. Static so it persists for the dialog's lifetime
// (the engine retains the pointer indirectly through sub_49FD50 / window
// state). Zero-initialized at file scope; re-zeroed on each fire.
static uint8_t s_phantomCtx[PHANTOM_CTX_SIZE] = {0};

// Slot-poll state for verification logging.
static bool   s_pollActive    = false;
static int    s_pollSlot      = -1;
static DWORD  s_pollStartMs   = 0;
static DWORD  s_lastPollMs    = 0;
static int    s_pollSampleIdx = 0;

// Sequential test counter so log lines are easy to correlate.
static int    s_testCounter   = 0;

// ============================================================================
// Helpers
// ============================================================================

// Read the gameObj base pointer. pGameObjGlobal stores the *address of the
// global* that holds a pointer to the game object. Returns nullptr if the
// address chain is not yet resolved or the dereferenced pointer is null.
static void* GetGameObjPtr() {
    if (FF8Addresses::pGameObjGlobal == 0) {
        return nullptr;
    }
    void* gameObj = nullptr;
    __try {
        gameObj = *(void**)(uintptr_t)FF8Addresses::pGameObjGlobal;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return nullptr;
    }
    return gameObj;
}

// Read the three gameObj dialog bitmasks. Returns true if read succeeded.
static bool ReadGameObjMasks(uint8_t& askMask, uint8_t& winMask, uint8_t& mesMask) {
    void* gameObj = GetGameObjPtr();
    if (gameObj == nullptr) return false;
    __try {
        askMask = *(uint8_t*)((uintptr_t)gameObj + GAMEOBJ_ASK_MASK_OFFSET);
        winMask = *(uint8_t*)((uintptr_t)gameObj + GAMEOBJ_WIN_MASK_OFFSET);
        mesMask = *(uint8_t*)((uintptr_t)gameObj + GAMEOBJ_MES_MASK_OFFSET);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
    return true;
}

// Snapshot a slot's polled fields. Returns false on read failure.
struct SlotSnapshot {
    int16_t  openClose;   // [+0x1C]
    int16_t  velocity;    // [+0x1E]
    uint32_t state;       // [+0x24]
    uint8_t  field16;     // [+0x16]
};

static bool ReadSlotSnapshot(int slot, SlotSnapshot& out) {
    if (FF8Addresses::pWindowsArray == nullptr) return false;
    if (slot < 0 || slot >= 8) return false;
    uint8_t* slotBase = FF8Addresses::pWindowsArray + (slot * WIN_OBJ_STRIDE);
    __try {
        out.openClose = *(int16_t*) (slotBase + WIN_OBJ_OPEN_CLOSE_OFFSET);
        out.velocity  = *(int16_t*) (slotBase + WIN_OBJ_VELOCITY_OFFSET);
        out.state     = *(uint32_t*)(slotBase + WIN_OBJ_STATE_OFFSET);
        out.field16   = *(uint8_t*) (slotBase + WIN_OBJ_FIELD16_OFFSET);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
    return true;
}

// SAPI-friendly Phase 1 result announcement.
//
// v0.15.5.3: changed interrupt flag from true -> false so this
// announcement QUEUES after the FieldDialog hook's spoken dialog text
// rather than preempting it. The v0.15.5.2 BAT confirmed that the
// FieldDialog [MES]/[ASK] hook fires DURING opcode_mes/opcode_ask and
// speaks the dialog text via SAPI, but our subsequent Speak with
// interrupt=true was cutting that text off mid-sentence and replacing
// it with the diagnostic phase announcement. Aaron heard only the
// phase announcement, never the dialog text. With interrupt=false,
// SAPI plays the dialog text first, then queues the phase announcement
// after.
static void AnnouncePhase1Result(int slot, int retCode) {
    char msg[160];
    snprintf(msg, sizeof(msg),
             "Dialog inject phase one. Slot %d. Return code %d.",
             slot, retCode);
    if (ScreenReader::IsAvailable()) {
        ScreenReader::Speak(msg, false);  // queue, don't interrupt
    }
}

// Diagnostic-only banner for the start of a Phase 1 attempt.
static void LogPhase1Banner(int testIdx, int slot, int msgId) {
    Log::Dialog("[DLG-INJ] ===== PHASE 1 TEST #%d START =====", testIdx);
    Log::Dialog("[DLG-INJ] Target: opcode_mes(slot=%d, msg_id=%d) via dispatch table[0x%02X]",
                slot, msgId, OPCODE_MES_INDEX);
    Log::Dialog("[DLG-INJ] FF8OPC_VERSION = %s", FF8OPC_VERSION);
}

// Log resolved addresses at fire time so we can diagnose missing chains.
static void LogResolvedAddresses() {
    Log::Dialog("[DLG-INJ] pExecuteOpcodeTable    = 0x%08X",
                (uint32_t)(uintptr_t)FF8Addresses::pExecuteOpcodeTable);
    Log::Dialog("[DLG-INJ] opcode_mes (cached)    = 0x%08X",
                FF8Addresses::opcode_mes);
    Log::Dialog("[DLG-INJ] pGameObjGlobal (addr)  = 0x%08X",
                FF8Addresses::pGameObjGlobal);
    void* gameObj = GetGameObjPtr();
    Log::Dialog("[DLG-INJ] *pGameObjGlobal        = 0x%08X",
                (uint32_t)(uintptr_t)gameObj);
    Log::Dialog("[DLG-INJ] pWindowsArray          = 0x%08X",
                (uint32_t)(uintptr_t)FF8Addresses::pWindowsArray);
    Log::Dialog("[DLG-INJ] pCurrentFieldId        = 0x%08X (val=%u)",
                (uint32_t)(uintptr_t)FF8Addresses::pCurrentFieldId,
                FF8Addresses::pCurrentFieldId ? *FF8Addresses::pCurrentFieldId : 0xFFFFu);
    if (FF8Addresses::pCurrentFieldName != nullptr) {
        Log::Dialog("[DLG-INJ] pCurrentFieldName      = '%s'",
                    FF8Addresses::pCurrentFieldName);
    }
}

// ============================================================================
// Public API
// ============================================================================

void Initialize() {
    if (s_initialized) return;
    s_initialized = true;
    s_pollActive = false;
    s_testCounter = 0;
    Log::Dialog("[DLG-INJ] Initialize -- Phase 1 module ready (v%s).", FF8OPC_VERSION);
}

void Shutdown() {
    if (!s_initialized) return;
    s_pollActive = false;
    s_initialized = false;
    Log::Dialog("[DLG-INJ] Shutdown.");
}

void Update() {
    if (!s_initialized) return;
    if (!s_pollActive) return;

    DWORD now = GetTickCount();
    DWORD elapsed = now - s_pollStartMs;

    // Cadence gate.
    if ((now - s_lastPollMs) < POLL_INTERVAL_MS) return;
    s_lastPollMs = now;

    // Snapshot the slot.
    SlotSnapshot snap = {0, 0, 0, 0};
    bool ok = ReadSlotSnapshot(s_pollSlot, snap);

    uint8_t askMask = 0, winMask = 0, mesMask = 0;
    bool maskOk = ReadGameObjMasks(askMask, winMask, mesMask);

    Log::Dialog("[DLG-INJ] poll #%d t+%4ums slot=%d "
                "trans=0x%04X vel=0x%04X state=0x%08X field16=0x%02X "
                "gameObj.D2=0x%02X D3=0x%02X D4=0x%02X %s",
                s_pollSampleIdx, elapsed, s_pollSlot,
                ok ? (uint16_t)snap.openClose : 0xFFFFu,
                ok ? (uint16_t)snap.velocity  : 0xFFFFu,
                ok ? snap.state               : 0xFFFFFFFFu,
                ok ? snap.field16             : 0xFFu,
                askMask, winMask, mesMask,
                ok ? (maskOk ? "" : "(mask read fail)")
                   : "(slot read fail)");
    s_pollSampleIdx++;

    // Stop after the duration.
    if (elapsed >= POLL_DURATION_MS) {
        Log::Dialog("[DLG-INJ] ===== PHASE 1 TEST POLL COMPLETE (slot=%d, %d samples) =====",
                    s_pollSlot, s_pollSampleIdx);
        s_pollActive = false;
    }
}

void Phase1_TestMes() {
    if (!s_initialized) {
        Log::Dialog("[DLG-INJ] Phase1_TestMes: module not initialized; aborting.");
        return;
    }

    // Field-mode guard. Field opcodes are only valid in MODE_FIELD.
    if (!FF8Addresses::IsOnField()) {
        Log::Dialog("[DLG-INJ] Phase1_TestMes: not in field mode (mode=%u); aborting.",
                    (unsigned)FF8Addresses::GetCurrentMode());
        if (ScreenReader::IsAvailable()) {
            ScreenReader::Speak("Dialog inject requires field mode.", true);
        }
        return;
    }

    // Required-address guard.
    if (FF8Addresses::opcode_mes == 0) {
        Log::Dialog("[DLG-INJ] Phase1_TestMes: opcode_mes not resolved; aborting.");
        if (ScreenReader::IsAvailable()) {
            ScreenReader::Speak("Dialog inject opcode address missing.", true);
        }
        return;
    }
    if (FF8Addresses::pWindowsArray == nullptr) {
        Log::Dialog("[DLG-INJ] Phase1_TestMes: pWindowsArray not resolved; aborting.");
        if (ScreenReader::IsAvailable()) {
            ScreenReader::Speak("Dialog inject windows array missing.", true);
        }
        return;
    }

    s_testCounter++;
    LogPhase1Banner(s_testCounter, TEST_SLOT, TEST_MSG_ID);
    LogResolvedAddresses();

    // Pre-fire snapshot of the target slot.
    SlotSnapshot pre = {0, 0, 0, 0};
    bool preOk = ReadSlotSnapshot(TEST_SLOT, pre);
    Log::Dialog("[DLG-INJ] PRE  slot=%d trans=0x%04X vel=0x%04X state=0x%08X field16=0x%02X %s",
                TEST_SLOT,
                preOk ? (uint16_t)pre.openClose : 0xFFFFu,
                preOk ? (uint16_t)pre.velocity  : 0xFFFFu,
                preOk ? pre.state               : 0xFFFFFFFFu,
                preOk ? pre.field16             : 0xFFu,
                preOk ? "" : "(read fail)");

    // Pre-fire snapshot of the gameObj bitmasks.
    {
        uint8_t askMask = 0, winMask = 0, mesMask = 0;
        if (ReadGameObjMasks(askMask, winMask, mesMask)) {
            Log::Dialog("[DLG-INJ] PRE  gameObj.D2(ASK)=0x%02X D3(win)=0x%02X D4(MES)=0x%02X",
                        askMask, winMask, mesMask);
        } else {
            Log::Dialog("[DLG-INJ] PRE  gameObj masks unread.");
        }
    }

    // Build the phantom script_context. Zero everything first to ensure
    // unused fields read as 0 (avoids stale state from a prior test).
    memset(s_phantomCtx, 0, sizeof(s_phantomCtx));

    // SP byte: signed; opcode_mes uses MOVSX so any high bit causes a
    // negative value, which would index BEFORE the buffer. Keep TEST_SP
    // small and non-negative.
    s_phantomCtx[CTX_SP_OFFSET] = (uint8_t)TEST_SP;

    // Stack args. Order: opcode_mes pulls msg_id from stack[SP] and slot
    // from stack[SP-1]. The validator pop_script_args returns a max-arg
    // count; with SP=2 it returns 2, so slot must be < 2. Slot 1 is OK.
    *(int32_t*)(s_phantomCtx + (TEST_SP    ) * 4) = TEST_MSG_ID;
    *(int32_t*)(s_phantomCtx + (TEST_SP - 1) * 4) = TEST_SLOT;

    Log::Dialog("[DLG-INJ] phantom ctx: SP=%d at +0x%X, msg_id=%d at +0x%X, slot=%d at +0x%X",
                TEST_SP, (unsigned)CTX_SP_OFFSET,
                TEST_MSG_ID, (unsigned)((TEST_SP    ) * 4),
                TEST_SLOT,   (unsigned)((TEST_SP - 1) * 4));

    // Resolve opcode_mes from the dispatch table at call time, in case
    // the cached value at FF8Addresses::opcode_mes was stale.
    uint32_t opcodeMesAddr = FF8Addresses::opcode_mes;
    if (FF8Addresses::pExecuteOpcodeTable != nullptr) {
        __try {
            uint32_t fromTable = FF8Addresses::pExecuteOpcodeTable[OPCODE_MES_INDEX];
            if (fromTable != 0) {
                if (fromTable != opcodeMesAddr) {
                    Log::Dialog("[DLG-INJ] WARNING: dispatch table opcode_mes=0x%08X "
                                "differs from cached=0x%08X; using table.",
                                fromTable, opcodeMesAddr);
                }
                opcodeMesAddr = fromTable;
            }
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            Log::Dialog("[DLG-INJ] WARNING: exception reading dispatch table; "
                        "using cached opcode_mes=0x%08X.", opcodeMesAddr);
        }
    }

    Log::Dialog("[DLG-INJ] FIRING opcode_mes(0x%08X)(ctx=0x%08X)...",
                opcodeMesAddr, (uint32_t)(uintptr_t)s_phantomCtx);

    // Call the opcode. __cdecl, single arg = script_context*. Return
    // codes from the opcode (per disassembly): 3 = advance PC,
    // 5 = wait/retry next frame (slot busy or pending).
    typedef int (__cdecl *opcode_mes_t)(void*);
    opcode_mes_t opcode_mes_fn = (opcode_mes_t)(uintptr_t)opcodeMesAddr;

    int retCode = -999;
    bool crashed = false;
    __try {
        retCode = opcode_mes_fn(s_phantomCtx);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        crashed = true;
    }

    if (crashed) {
        Log::Dialog("[DLG-INJ] *** opcode_mes RAISED EXCEPTION *** "
                    "phantom ctx layout likely needs more fields populated.");
        if (ScreenReader::IsAvailable()) {
            ScreenReader::Speak("Dialog inject crashed. See dialog log.", true);
        }
        return;
    }

    Log::Dialog("[DLG-INJ] opcode_mes returned %d  "
                "(3=advance/success, 5=wait/slot-busy, other=undocumented)",
                retCode);

    // Post-fire snapshot of the target slot.
    SlotSnapshot post = {0, 0, 0, 0};
    bool postOk = ReadSlotSnapshot(TEST_SLOT, post);
    Log::Dialog("[DLG-INJ] POST slot=%d trans=0x%04X vel=0x%04X state=0x%08X field16=0x%02X %s",
                TEST_SLOT,
                postOk ? (uint16_t)post.openClose : 0xFFFFu,
                postOk ? (uint16_t)post.velocity  : 0xFFFFu,
                postOk ? post.state               : 0xFFFFFFFFu,
                postOk ? post.field16             : 0xFFu,
                postOk ? "" : "(read fail)");

    // Post-fire snapshot of the gameObj bitmasks.
    {
        uint8_t askMask = 0, winMask = 0, mesMask = 0;
        if (ReadGameObjMasks(askMask, winMask, mesMask)) {
            Log::Dialog("[DLG-INJ] POST gameObj.D2(ASK)=0x%02X D3(win)=0x%02X D4(MES)=0x%02X",
                        askMask, winMask, mesMask);
        }
    }

    // Audible result.
    AnnouncePhase1Result(TEST_SLOT, retCode);

    // Start per-frame slot poll for ~3 sec to verify rendering is alive.
    s_pollActive    = true;
    s_pollSlot      = TEST_SLOT;
    s_pollStartMs   = GetTickCount();
    s_lastPollMs    = 0;  // force first sample on next Update
    s_pollSampleIdx = 0;
    Log::Dialog("[DLG-INJ] Slot poll active for %u ms at %u ms cadence.",
                POLL_DURATION_MS, POLL_INTERVAL_MS);
}

// ============================================================================
// Phase 2a -- experimental opcode_ask call
// ============================================================================
//
// v0.15.5: Same recipe as Phase 1, different opcode and slot. Calls
// opcode_ask(&phantom_ctx) with SP=6 to render the field's natural ASK
// at msg 0 in slot 2. The field's MSD content drives what's displayed;
// our six stack args set: slot index (CONFIRMED via assertion check at
// 0x52955A), msg_id (CONFIRMED via field_get_dialog_string call at
// 0x5295CD), and four values that propagate to set_window_object_ASK
// args 2-5 (clamp range + firstQ/lastQ/aux).
//
// Aaron's BAT environment is doani1_2 (Dollet Comm Tower top), where
// msg 0 is the Selphie elevator ASK with two choice lines ("Go up" /
// "Stay"). Setting our cursor range to [2,3] should land the cursor
// on those lines.
//
// opcode_ask returns 1 on the first call ("wait for user to choose").
// The engine's input handler updates slot+0x2B (curQ) on arrows and
// clears state on Enter. Phase 2a doesn't yet detect the answer
// commit; that's a Phase 2b deliverable. For now we just confirm
// the dialog renders with cursor and is navigable.

static void AnnouncePhase2Result(int slot, int retCode) {
    char msg[160];
    snprintf(msg, sizeof(msg),
             "Dialog inject phase two A. Slot %d. Return code %d.",
             slot, retCode);
    if (ScreenReader::IsAvailable()) {
        // v0.15.5.3: interrupt=false so this queues after the FieldDialog
        // hook's [ASK] spoken dialog text rather than preempting it.
        // See AnnouncePhase1Result comment for full rationale.
        ScreenReader::Speak(msg, false);
    }
}

void Phase2_TestAsk() {
    if (!s_initialized) {
        Log::Dialog("[DLG-INJ] Phase2_TestAsk: module not initialized; aborting.");
        return;
    }

    if (!FF8Addresses::IsOnField()) {
        Log::Dialog("[DLG-INJ] Phase2_TestAsk: not in field mode (mode=%u); aborting.",
                    (unsigned)FF8Addresses::GetCurrentMode());
        if (ScreenReader::IsAvailable()) {
            ScreenReader::Speak("Dialog inject requires field mode.", true);
        }
        return;
    }

    if (FF8Addresses::opcode_ask == 0) {
        Log::Dialog("[DLG-INJ] Phase2_TestAsk: opcode_ask not resolved; aborting.");
        if (ScreenReader::IsAvailable()) {
            ScreenReader::Speak("Dialog inject ASK opcode address missing.", true);
        }
        return;
    }
    if (FF8Addresses::pWindowsArray == nullptr) {
        Log::Dialog("[DLG-INJ] Phase2_TestAsk: pWindowsArray not resolved; aborting.");
        if (ScreenReader::IsAvailable()) {
            ScreenReader::Speak("Dialog inject windows array missing.", true);
        }
        return;
    }

    s_testCounter++;
    Log::Dialog("[DLG-INJ] ===== PHASE 2A TEST #%d START =====", s_testCounter);
    Log::Dialog("[DLG-INJ] Target: opcode_ask(slot=%d, msg_id=%d) via dispatch table[0x%02X]",
                TEST_SLOT_ASK, TEST_MSG_ID_ASK, OPCODE_ASK_INDEX);
    Log::Dialog("[DLG-INJ] FF8OPC_VERSION = %s", FF8OPC_VERSION);
    LogResolvedAddresses();

    // Pre-fire snapshot of the target slot.
    SlotSnapshot pre = {0, 0, 0, 0};
    bool preOk = ReadSlotSnapshot(TEST_SLOT_ASK, pre);
    Log::Dialog("[DLG-INJ] PRE  ASK slot=%d trans=0x%04X vel=0x%04X state=0x%08X field16=0x%02X %s",
                TEST_SLOT_ASK,
                preOk ? (uint16_t)pre.openClose : 0xFFFFu,
                preOk ? (uint16_t)pre.velocity  : 0xFFFFu,
                preOk ? pre.state               : 0xFFFFFFFFu,
                preOk ? pre.field16             : 0xFFu,
                preOk ? "" : "(read fail)");

    {
        uint8_t askMask = 0, winMask = 0, mesMask = 0;
        if (ReadGameObjMasks(askMask, winMask, mesMask)) {
            Log::Dialog("[DLG-INJ] PRE  ASK gameObj.D2(ASK)=0x%02X D3(win)=0x%02X D4(MES)=0x%02X",
                        askMask, winMask, mesMask);
            // Slot-busy gate inside opcode_ask reads gameObj+0xD2 bit (1<<slot).
            // If already set, opcode_ask returns 5 without doing anything.
            uint8_t mySlotBit = (uint8_t)(1u << TEST_SLOT_ASK);
            if (askMask & mySlotBit) {
                Log::Dialog("[DLG-INJ] WARNING: gameObj.D2 bit %d already set; "
                            "opcode_ask will return 5 without rendering.",
                            TEST_SLOT_ASK);
            }
        } else {
            Log::Dialog("[DLG-INJ] PRE  ASK gameObj masks unread.");
        }
    }

    // Build the phantom script_context for ASK. Re-use the same buffer
    // as Phase 1 (zeroed each fire) since the two tests don't overlap
    // in time -- only one F12 / Shift+F12 press can be in flight at a
    // time given the 3-second poll window.
    memset(s_phantomCtx, 0, sizeof(s_phantomCtx));
    s_phantomCtx[CTX_SP_OFFSET] = (uint8_t)TEST_SP_ASK;

    // v0.15.5.1: ASK-pending tracking bytes. The v0.15.5 BAT showed
    // opcode_ask returning 1 but slot 2 stayed all zeros and gameObj.D2
    // bit 2 stayed 0 -- meaning the call exited via an early-return path
    // (0x529683 -> sub_49FD50 / sub_49FD70 / sub_4A0660 -> ++[esi+0x204]
    // -> ret 1) before reaching the .alloc branch that calls
    // set_window_object_ASK.
    //
    // The early exit is the test at 0x52956D-0x529582:
    //   cl = [esi+0x174]              ; "current pending ASK slot" tracker
    //   al = [esi+0x175]              ; "ASK pending bitmask" tracker
    //   edx = 1 << cl
    //   test al, dl                   ; ctx[+0x175] & (1 << ctx[+0x174])
    //   je 0x529622                   ; if 0, branch to answer-correlation path
    //                                 ; if non-zero, fall through to .alloc
    //
    // With ctx[+0x174] = 0 and ctx[+0x175] = 1, the test passes
    // (al & dl = 1 & 1 = 1), the je is NOT taken, and execution falls
    // through to:
    //   slot-busy check at 0x529588 (gameObj.D2 & (1<<slot)),
    //   then -- if not busy -- to .alloc at 0x5295AB which calls
    //   field_get_dialog_string + set_window_object_ASK and sets the
    //   gameObj.D2 bit.
    //
    // The natural script-VM execution sets these tracking bytes via a
    // preparatory opcode before the ASK opcode runs. We don't run that
    // preparatory opcode, so we must set them ourselves.
    s_phantomCtx[0x174] = 0;  // shift count
    s_phantomCtx[0x175] = 1;  // bit 0 set

    // Stack args (SP=6). See header comment for the mapping.
    *(int32_t*)(s_phantomCtx + (TEST_SP_ASK - 5) * 4) = TEST_SLOT_ASK;     // slot
    *(int32_t*)(s_phantomCtx + (TEST_SP_ASK - 4) * 4) = TEST_MSG_ID_ASK;   // msg_id
    *(int32_t*)(s_phantomCtx + (TEST_SP_ASK - 3) * 4) = TEST_ASK_ARG2;     // SWO_ASK arg2
    *(int32_t*)(s_phantomCtx + (TEST_SP_ASK - 2) * 4) = TEST_ASK_ARG3;     // SWO_ASK arg3
    *(int32_t*)(s_phantomCtx + (TEST_SP_ASK - 1) * 4) = TEST_ASK_ARG4;     // SWO_ASK arg4
    *(int32_t*)(s_phantomCtx + (TEST_SP_ASK - 0) * 4) = TEST_ASK_ARG5;     // SWO_ASK arg5

    Log::Dialog("[DLG-INJ] phantom ctx ASK: SP=%d at +0x%X",
                TEST_SP_ASK, (unsigned)CTX_SP_OFFSET);
    Log::Dialog("[DLG-INJ]   ctx[+0x174] = %u  ctx[+0x175] = 0x%02X  (ASK-pending tracking bytes; v0.15.5.1)",
                s_phantomCtx[0x174], s_phantomCtx[0x175]);
    Log::Dialog("[DLG-INJ]   stack[SP-5] @ +0x%02X = %d (slot)",
                (unsigned)((TEST_SP_ASK - 5) * 4), TEST_SLOT_ASK);
    Log::Dialog("[DLG-INJ]   stack[SP-4] @ +0x%02X = %d (msg_id)",
                (unsigned)((TEST_SP_ASK - 4) * 4), TEST_MSG_ID_ASK);
    Log::Dialog("[DLG-INJ]   stack[SP-3] @ +0x%02X = %d (SWO_ASK arg2 / clamp lo)",
                (unsigned)((TEST_SP_ASK - 3) * 4), TEST_ASK_ARG2);
    Log::Dialog("[DLG-INJ]   stack[SP-2] @ +0x%02X = %d (SWO_ASK arg3 / clamp hi / firstQ?)",
                (unsigned)((TEST_SP_ASK - 2) * 4), TEST_ASK_ARG3);
    Log::Dialog("[DLG-INJ]   stack[SP-1] @ +0x%02X = %d (SWO_ASK arg4 / lastQ?)",
                (unsigned)((TEST_SP_ASK - 1) * 4), TEST_ASK_ARG4);
    Log::Dialog("[DLG-INJ]   stack[SP-0] @ +0x%02X = %d (SWO_ASK arg5 / aux?)",
                (unsigned)((TEST_SP_ASK - 0) * 4), TEST_ASK_ARG5);

    // Resolve opcode_ask from the dispatch table at fire time, with
    // cached fallback (mirrors the Phase 1 pattern; the v0.15.4 BAT
    // showed FFNx wraps these table entries).
    uint32_t opcodeAskAddr = FF8Addresses::opcode_ask;
    if (FF8Addresses::pExecuteOpcodeTable != nullptr) {
        __try {
            uint32_t fromTable = FF8Addresses::pExecuteOpcodeTable[OPCODE_ASK_INDEX];
            if (fromTable != 0) {
                if (fromTable != opcodeAskAddr) {
                    Log::Dialog("[DLG-INJ] dispatch table opcode_ask=0x%08X "
                                "differs from cached=0x%08X; using table.",
                                fromTable, opcodeAskAddr);
                }
                opcodeAskAddr = fromTable;
            }
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            Log::Dialog("[DLG-INJ] WARNING: exception reading dispatch table; "
                        "using cached opcode_ask=0x%08X.", opcodeAskAddr);
        }
    }

    Log::Dialog("[DLG-INJ] FIRING opcode_ask(0x%08X)(ctx=0x%08X)...",
                opcodeAskAddr, (uint32_t)(uintptr_t)s_phantomCtx);

    // v0.15.5.2: Call sub_49FD50(slot) BEFORE opcode_ask to set
    // pCurrentDialogSlot. The .alloc branch we take doesn't set this
    // internally; only the stage-1 setup path at 0x529683 does (via its
    // own sub_49FD50 call). Without pCurrentDialogSlot pointing at our
    // slot, the engine's input handler doesn't send arrow keys to
    // slot+0x2B (curQ), so the cursor doesn't move and Aaron doesn't
    // hear FF8's standard cursor-move SFX.
    //
    // sub_49FD50 takes one int arg (slot index) and writes it to the
    // global byte at 0x01D2B51C (pCurrentDialogSlot). Hardcoded address
    // 0x0049FD50 since it's a stable internal helper not in the JSM
    // opcode dispatch table (no FFNx wrapping concern). If a future
    // game-version mismatch surfaces, promote to FF8Addresses.
    {
        const uint32_t SUB_49FD50_ADDR = 0x0049FD50;
        typedef void (__cdecl *sub_49fd50_t)(int);
        sub_49fd50_t sub_49fd50_fn = (sub_49fd50_t)(uintptr_t)SUB_49FD50_ADDR;

        // Read pCurrentDialogSlot before and after the call for diagnostic.
        const uint32_t PCURRENT_DIALOG_SLOT_ADDR = 0x01D2B51C;
        uint8_t* pCurrentDialogSlot = (uint8_t*)(uintptr_t)PCURRENT_DIALOG_SLOT_ADDR;
        uint8_t preCurSlot = 0xFF;
        __try { preCurSlot = *pCurrentDialogSlot; }
        __except (EXCEPTION_EXECUTE_HANDLER) {}

        __try {
            sub_49fd50_fn(TEST_SLOT_ASK);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            Log::Dialog("[DLG-INJ] WARNING: sub_49FD50(%d) raised exception", TEST_SLOT_ASK);
        }

        uint8_t postCurSlot = 0xFF;
        __try { postCurSlot = *pCurrentDialogSlot; }
        __except (EXCEPTION_EXECUTE_HANDLER) {}

        Log::Dialog("[DLG-INJ] sub_49FD50(%d): pCurrentDialogSlot 0x%02X -> 0x%02X (enables arrow input for this slot)",
                    TEST_SLOT_ASK, preCurSlot, postCurSlot);
    }

    // Call. opcode_ask is __cdecl, single arg = script_context*.
    // Return codes: 1 = wait for answer, 5 = slot busy, 3 = advance
    // (typically only after the answer has been read on a subsequent
    // call within the script-VM loop -- which we don't run).
    typedef int (__cdecl *opcode_ask_t)(void*);
    opcode_ask_t opcode_ask_fn = (opcode_ask_t)(uintptr_t)opcodeAskAddr;

    int retCode = -999;
    bool crashed = false;
    __try {
        retCode = opcode_ask_fn(s_phantomCtx);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        crashed = true;
    }

    if (crashed) {
        Log::Dialog("[DLG-INJ] *** opcode_ask RAISED EXCEPTION *** "
                    "phantom ctx layout likely needs more fields populated.");
        if (ScreenReader::IsAvailable()) {
            ScreenReader::Speak("Dialog inject phase two A crashed. See dialog log.", true);
        }
        return;
    }

    Log::Dialog("[DLG-INJ] opcode_ask returned %d  "
                "(1=wait, 5=slot-busy, 3=advance, other=undocumented)",
                retCode);

    // Post-fire snapshot.
    SlotSnapshot post = {0, 0, 0, 0};
    bool postOk = ReadSlotSnapshot(TEST_SLOT_ASK, post);
    Log::Dialog("[DLG-INJ] POST ASK slot=%d trans=0x%04X vel=0x%04X state=0x%08X field16=0x%02X %s",
                TEST_SLOT_ASK,
                postOk ? (uint16_t)post.openClose : 0xFFFFu,
                postOk ? (uint16_t)post.velocity  : 0xFFFFu,
                postOk ? post.state               : 0xFFFFFFFFu,
                postOk ? post.field16             : 0xFFu,
                postOk ? "" : "(read fail)");

    {
        uint8_t askMask = 0, winMask = 0, mesMask = 0;
        if (ReadGameObjMasks(askMask, winMask, mesMask)) {
            Log::Dialog("[DLG-INJ] POST ASK gameObj.D2(ASK)=0x%02X D3(win)=0x%02X D4(MES)=0x%02X",
                        askMask, winMask, mesMask);
        }
    }

    // Read the slot's choice fields (firstQ, lastQ, curQ, second cursor).
    // These tell us which arg landed in which slot field, mapping the
    // SWO_ASK args empirically.
    if (postOk && FF8Addresses::pWindowsArray != nullptr) {
        uint8_t* slotBase = FF8Addresses::pWindowsArray + (TEST_SLOT_ASK * WIN_OBJ_STRIDE);
        __try {
            uint8_t firstQ = *(uint8_t*)(slotBase + 0x29);
            uint8_t lastQ  = *(uint8_t*)(slotBase + 0x2A);
            uint8_t curQ_2 = *(uint8_t*)(slotBase + 0x2C);
            Log::Dialog("[DLG-INJ] POST ASK slot[+0x29]firstQ=%u slot[+0x2A]lastQ=%u slot[+0x2C]curQ_2=%u",
                        firstQ, lastQ, curQ_2);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            Log::Dialog("[DLG-INJ] POST ASK choice fields unread.");
        }
    }

    AnnouncePhase2Result(TEST_SLOT_ASK, retCode);

    // Start per-frame slot poll for ~3 sec to verify rendering is alive.
    s_pollActive    = true;
    s_pollSlot      = TEST_SLOT_ASK;
    s_pollStartMs   = GetTickCount();
    s_lastPollMs    = 0;
    s_pollSampleIdx = 0;
    Log::Dialog("[DLG-INJ] ASK slot poll active for %u ms at %u ms cadence.",
                POLL_DURATION_MS, POLL_INTERVAL_MS);
}

}  // namespace DialogInject
