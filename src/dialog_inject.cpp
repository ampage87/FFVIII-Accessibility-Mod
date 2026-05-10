// dialog_inject.cpp -- Mod-driven engine dialog injection.
// See dialog_inject.h for design notes.
//
// v0.15.4: Initial Phase 1. Synthesizes a phantom script_context and
// calls opcode_mes(&ctx) directly to prove the engine renders.
// v0.15.5: Phase 2a -- same recipe extended to opcode_ask. SP=6.
// v0.15.5.1: ASK-pending tracking bytes ctx[+0x174]/[+0x175] fix.
// v0.15.5.2: sub_49FD50(slot) call to wire arrow input.
// v0.15.5.3: Speak interrupt=false in AnnouncePhase*Result so the
//            FieldDialog hook's spoken dialog text is heard before
//            our diagnostic announcement.
// v0.15.6:  Phase 2b -- field_get_dialog_string override pattern.
//            Custom FF8-encoded buffer "Mode? / Manual / Auto / Original"
//            replaces the field's natural msg 0 text via the
//            existing field_dialog hook.
// v0.15.6.1: Phase 2b fix -- post-ASK slot+0x08 patching. v0.15.6 BAT
//            failed: zero [GETSTR-RAW] log lines proved FFNx's
//            replace_call had rewritten the engine's CALL
//            field_get_dialog_string operand to point at FFNx's own
//            function, leaving our hook on 0x00530750 dead. Fix moves
//            the substitution into Hook_opcode_ask (field_dialog.cpp)
//            which patches slot+0x08 between s_origAsk return and
//            ScanAndSpeakChoiceWindows. SetOverride now also captures
//            target slot, exposed via GetOverrideSlot().
// v0.15.6.2: v0.15.6.1 patch landed cleanly (POST-ASK-OVERRIDE log fired,
//            slot+0x08 = our buffer address 0x6E98E020) but Aaron still
//            heard Selphie's text. Root cause: our static buffer lives
//            in the DLL data section above 0x30000000, the upper bound
//            of field_dialog's IsValidTextPointer FF8-heap-range
//            heuristic. ScanAndSpeakChoiceWindows silently skipped the
//            slot (no [ASK] win[2] line in BAT log) and Hook_show_dialog
//            fell back to text_data2 (still holding the natural Selphie
//            pointer) and spoke that. Fix: expose the override buffer's
//            stable address range via new GetOverrideBufferStart/Size
//            APIs, and field_dialog.cpp's IsValidTextPointer whitelists
//            pointers within that range.

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

// Phase 1 test parameters. SP=2 means two args on the script-VM stack.
static const int8_t  TEST_SP        = 2;
static const int32_t TEST_MSG_ID    = 0;     // every field has msg 0
static const int32_t TEST_SLOT      = 1;     // pWindowsArray[1]

// Phase 2 test parameters. SP=6 means six args on the script-VM stack.
// EMPIRICAL ARG MAP (confirmed v0.15.5.1 BAT post-fire decode of slot fields):
//   stack[SP-5] (ctx[+0x04]) -> edi -> slot index
//   stack[SP-4] (ctx[+0x08]) -> ecx -> msg_id
//   stack[SP-3] (ctx[+0x0C]) -> SWO_ASK arg2 -> slot+0x29 (firstQ)
//   stack[SP-2] (ctx[+0x10]) -> SWO_ASK arg3 -> slot+0x2A (lastQ)
//   stack[SP-1] (ctx[+0x14]) -> SWO_ASK arg4 -> slot+0x2C (curQ, clamped to [firstQ, lastQ])
//   stack[SP-0] (ctx[+0x18]) -> SWO_ASK arg5 -> slot+0x2B (aux)
//
// The real signature is: set_window_object_ASK(slot, text, firstQ, lastQ, curQ, aux).
//
// v0.15.6 Phase 2b values for our injected "Mode? / Manual / Auto / Original":
//   Line 0 = "Mode?" (prompt prefix)
//   Line 1 = "Manual"  -> first choice (firstQ=1)
//   Line 2 = "Auto"
//   Line 3 = "Original" -> last choice (lastQ=3)
// Default cursor on Manual: curQ=1.
static const int8_t  TEST_SP_ASK     = 6;
static const int32_t TEST_SLOT_ASK   = 2;
static const int32_t TEST_MSG_ID_ASK = 0;     // unused when override active
static const int32_t TEST_ASK_FIRST_Q = 1;    // first choice line index
static const int32_t TEST_ASK_LAST_Q  = 3;    // last choice line index
static const int32_t TEST_ASK_CUR_Q   = 1;    // default cursor on Manual
static const int32_t TEST_ASK_AUX     = 0;    // aux byte

// JSM dispatch table opcode indices (mirrors ff8_addresses.h).
static const int OPCODE_MES_INDEX   = 0x47;
static const int OPCODE_ASK_INDEX   = 0x4A;

// ff8_win_obj layout -- only the fields we poll.
static const size_t WIN_OBJ_STRIDE              = 0x3C;
static const size_t WIN_OBJ_OPEN_CLOSE_OFFSET   = 0x1C;
static const size_t WIN_OBJ_STATE_OFFSET        = 0x24;
static const size_t WIN_OBJ_FIELD16_OFFSET      = 0x16;
static const size_t WIN_OBJ_VELOCITY_OFFSET     = 0x1E;

// gameObj bitmask offsets.
static const size_t GAMEOBJ_ASK_MASK_OFFSET     = 0xD2;
static const size_t GAMEOBJ_WIN_MASK_OFFSET     = 0xD3;
static const size_t GAMEOBJ_MES_MASK_OFFSET     = 0xD4;

// Slot-polling cadence and duration after a fire.
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
// v0.15.6.1 Phase 2b: text override state
//
// When s_overrideActive != 0, field_dialog.cpp's Hook_opcode_ask patches
// slot s_overrideSlot's +0x08 (text_data1) to point at s_overrideText
// after s_origAsk returns and before ScanAndSpeakChoiceWindows reads it.
// The buffer s_overrideBuffer is statically allocated and persists for
// the dialog's lifetime (the engine reads slot+0x08 every frame while the
// dialog is open).
//
// Coordination is single-threaded: SetOverride is called immediately
// before opcode_ask on the game thread, and ClearOverride is called
// immediately after opcode_ask returns on the same thread. Hook_opcode_ask
// fires inside opcode_ask on the same thread, so there's no race.
//
// v0.15.6 originally relied on a hook of field_get_dialog_string but FFNx's
// replace_call pattern bypassed that hook entirely (BAT log: zero
// [GETSTR-RAW] lines despite unconditional first-10-calls logging).
// v0.15.6.1 moves the substitution to a point downstream of the bypass.
// ============================================================================
static const size_t OVERRIDE_BUFFER_SIZE = 256;
static uint8_t s_overrideBuffer[OVERRIDE_BUFFER_SIZE] = {0};
static volatile LONG s_overrideActive = 0;
static const char* s_overrideText = nullptr;
static int s_overrideSlot = -1;   // v0.15.6.1: target slot for post-ASK patching

// ============================================================================
// FF8 dialog text encoder (v0.15.6)
//
// Inverts the decode table in ff8_text_decode.cpp. Maps ASCII to FF8
// dialog encoding. '\n' becomes 0x02 (line break), null terminator is
// 0x00. Returns the number of bytes written (including the terminator).
// Unknown characters are skipped silently.
// ============================================================================
static int EncodeFf8(const char* in, uint8_t* out, int outSize) {
    if (in == nullptr || out == nullptr || outSize < 1) return 0;
    int n = 0;
    for (const char* p = in; *p && n < outSize - 1; ++p) {
        char c = *p;
        uint8_t enc = 0;
        bool wrote = true;
        if      (c == '\n') enc = 0x02;
        else if (c == ' ')  enc = 0x20;
        else if (c >= '0' && c <= '9') enc = (uint8_t)(0x21 + (c - '0'));
        else if (c == '%')  enc = 0x2B;
        else if (c == '/')  enc = 0x2C;
        else if (c == ':')  enc = 0x2D;
        else if (c == '!')  enc = 0x2E;
        else if (c == '?')  enc = 0x2F;
        else if (c == '+')  enc = 0x31;
        else if (c == '-')  enc = 0x32;
        else if (c == '=')  enc = 0x33;
        else if (c == '*')  enc = 0x34;
        else if (c == '&')  enc = 0x35;
        else if (c == '(')  enc = 0x38;
        else if (c == ')')  enc = 0x39;
        else if (c == '\'') enc = 0x40;
        else if (c == '#')  enc = 0x41;
        else if (c == 0x24) enc = 0x42;  // '$' as hex literal to avoid editor issues
        else if (c == '_')  enc = 0x44;
        else if (c == '.')  enc = 0x3B;
        else if (c == ',')  enc = 0x3C;
        else if (c == '~')  enc = 0x3D;
        else if (c == '"')  enc = 0x3E;
        else if (c >= 'A' && c <= 'Z') enc = (uint8_t)(0x45 + (c - 'A'));
        else if (c >= 'a' && c <= 'z') enc = (uint8_t)(0x5F + (c - 'a'));
        else wrote = false;  // skip unknown chars silently
        if (wrote) out[n++] = enc;
    }
    out[n++] = 0x00;  // terminator
    return n;
}

// ============================================================================
// v0.15.6.1 Phase 2b: override flag management
// ============================================================================
static void SetOverride(int slot, const char* text) {
    s_overrideSlot = slot;
    s_overrideText = text;
    InterlockedExchange(&s_overrideActive, 1);
}

static void ClearOverride() {
    InterlockedExchange(&s_overrideActive, 0);
    s_overrideText = nullptr;
    s_overrideSlot = -1;
}

// Public override API for field_dialog.cpp's Hook_opcode_ask.
bool IsOverrideActive() {
    return InterlockedCompareExchange(&s_overrideActive, 0, 0) != 0;
}

const char* GetOverrideText() {
    return s_overrideText;
}

int GetOverrideSlot() {
    return s_overrideSlot;
}

// v0.15.6.2: expose the static override buffer's address range so
// field_dialog.cpp's IsValidTextPointer can whitelist pointers within
// it. The buffer's location is fixed for the DLL's lifetime; these
// accessors do not depend on the override flag being active.
const unsigned char* GetOverrideBufferStart() {
    return (const unsigned char*)s_overrideBuffer;
}

unsigned int GetOverrideBufferSize() {
    return (unsigned int)OVERRIDE_BUFFER_SIZE;
}

// ============================================================================
// Helpers
// ============================================================================

static void* GetGameObjPtr() {
    if (FF8Addresses::pGameObjGlobal == 0) return nullptr;
    void* gameObj = nullptr;
    __try {
        gameObj = *(void**)(uintptr_t)FF8Addresses::pGameObjGlobal;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return nullptr;
    }
    return gameObj;
}

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

struct SlotSnapshot {
    int16_t  openClose;
    int16_t  velocity;
    uint32_t state;
    uint8_t  field16;
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
// v0.15.5.3: interrupt=false so this queues after the FieldDialog hook's
// spoken dialog text rather than preempting it.
static void AnnouncePhase1Result(int slot, int retCode) {
    char msg[160];
    snprintf(msg, sizeof(msg),
             "Dialog inject phase one. Slot %d. Return code %d.",
             slot, retCode);
    if (ScreenReader::IsAvailable()) {
        ScreenReader::Speak(msg, false);
    }
}

static void LogPhase1Banner(int testIdx, int slot, int msgId) {
    Log::Dialog("[DLG-INJ] ===== PHASE 1 TEST #%d START =====", testIdx);
    Log::Dialog("[DLG-INJ] Target: opcode_mes(slot=%d, msg_id=%d) via dispatch table[0x%02X]",
                slot, msgId, OPCODE_MES_INDEX);
    Log::Dialog("[DLG-INJ] FF8OPC_VERSION = %s", FF8OPC_VERSION);
}

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
    Log::Dialog("[DLG-INJ] Initialize -- module ready (v%s).", FF8OPC_VERSION);
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

    if ((now - s_lastPollMs) < POLL_INTERVAL_MS) return;
    s_lastPollMs = now;

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

    if (elapsed >= POLL_DURATION_MS) {
        Log::Dialog("[DLG-INJ] ===== TEST POLL COMPLETE (slot=%d, %d samples) =====",
                    s_pollSlot, s_pollSampleIdx);
        s_pollActive = false;
    }
}

void Phase1_TestMes() {
    if (!s_initialized) {
        Log::Dialog("[DLG-INJ] Phase1_TestMes: module not initialized; aborting.");
        return;
    }

    if (!FF8Addresses::IsOnField()) {
        Log::Dialog("[DLG-INJ] Phase1_TestMes: not in field mode (mode=%u); aborting.",
                    (unsigned)FF8Addresses::GetCurrentMode());
        if (ScreenReader::IsAvailable()) {
            ScreenReader::Speak("Dialog inject requires field mode.", true);
        }
        return;
    }

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

    SlotSnapshot pre = {0, 0, 0, 0};
    bool preOk = ReadSlotSnapshot(TEST_SLOT, pre);
    Log::Dialog("[DLG-INJ] PRE  slot=%d trans=0x%04X vel=0x%04X state=0x%08X field16=0x%02X %s",
                TEST_SLOT,
                preOk ? (uint16_t)pre.openClose : 0xFFFFu,
                preOk ? (uint16_t)pre.velocity  : 0xFFFFu,
                preOk ? pre.state               : 0xFFFFFFFFu,
                preOk ? pre.field16             : 0xFFu,
                preOk ? "" : "(read fail)");

    {
        uint8_t askMask = 0, winMask = 0, mesMask = 0;
        if (ReadGameObjMasks(askMask, winMask, mesMask)) {
            Log::Dialog("[DLG-INJ] PRE  gameObj.D2(ASK)=0x%02X D3(win)=0x%02X D4(MES)=0x%02X",
                        askMask, winMask, mesMask);
        }
    }

    memset(s_phantomCtx, 0, sizeof(s_phantomCtx));
    s_phantomCtx[CTX_SP_OFFSET] = (uint8_t)TEST_SP;
    *(int32_t*)(s_phantomCtx + (TEST_SP    ) * 4) = TEST_MSG_ID;
    *(int32_t*)(s_phantomCtx + (TEST_SP - 1) * 4) = TEST_SLOT;

    Log::Dialog("[DLG-INJ] phantom ctx: SP=%d at +0x%X, msg_id=%d at +0x%X, slot=%d at +0x%X",
                TEST_SP, (unsigned)CTX_SP_OFFSET,
                TEST_MSG_ID, (unsigned)((TEST_SP    ) * 4),
                TEST_SLOT,   (unsigned)((TEST_SP - 1) * 4));

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
        Log::Dialog("[DLG-INJ] *** opcode_mes RAISED EXCEPTION ***");
        if (ScreenReader::IsAvailable()) {
            ScreenReader::Speak("Dialog inject crashed. See dialog log.", true);
        }
        return;
    }

    Log::Dialog("[DLG-INJ] opcode_mes returned %d  "
                "(3=advance/success, 5=wait/slot-busy, other=undocumented)",
                retCode);

    SlotSnapshot post = {0, 0, 0, 0};
    bool postOk = ReadSlotSnapshot(TEST_SLOT, post);
    Log::Dialog("[DLG-INJ] POST slot=%d trans=0x%04X vel=0x%04X state=0x%08X field16=0x%02X %s",
                TEST_SLOT,
                postOk ? (uint16_t)post.openClose : 0xFFFFu,
                postOk ? (uint16_t)post.velocity  : 0xFFFFu,
                postOk ? post.state               : 0xFFFFFFFFu,
                postOk ? post.field16             : 0xFFu,
                postOk ? "" : "(read fail)");

    {
        uint8_t askMask = 0, winMask = 0, mesMask = 0;
        if (ReadGameObjMasks(askMask, winMask, mesMask)) {
            Log::Dialog("[DLG-INJ] POST gameObj.D2(ASK)=0x%02X D3(win)=0x%02X D4(MES)=0x%02X",
                        askMask, winMask, mesMask);
        }
    }

    AnnouncePhase1Result(TEST_SLOT, retCode);

    s_pollActive    = true;
    s_pollSlot      = TEST_SLOT;
    s_pollStartMs   = GetTickCount();
    s_lastPollMs    = 0;
    s_pollSampleIdx = 0;
    Log::Dialog("[DLG-INJ] Slot poll active for %u ms at %u ms cadence.",
                POLL_DURATION_MS, POLL_INTERVAL_MS);
}

// ============================================================================
// Phase 2 -- opcode_ask call (Phase 2a in v0.15.5; Phase 2b override in v0.15.6)
// ============================================================================

// v0.15.5.3: interrupt=false so this queues after the FieldDialog hook's
// [ASK] spoken dialog text rather than preempting it.
static void AnnouncePhase2Result(int slot, int retCode) {
    char msg[160];
    snprintf(msg, sizeof(msg),
             "Dialog inject phase two B. Slot %d. Return code %d.",
             slot, retCode);
    if (ScreenReader::IsAvailable()) {
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

    if (FF8Addresses::opcode_ask == 0 || FF8Addresses::pWindowsArray == nullptr) {
        Log::Dialog("[DLG-INJ] Phase2_TestAsk: addresses not resolved; aborting.");
        if (ScreenReader::IsAvailable()) {
            ScreenReader::Speak("Dialog inject ASK addresses missing.", true);
        }
        return;
    }

    s_testCounter++;
    Log::Dialog("[DLG-INJ] ===== PHASE 2B TEST #%d START =====", s_testCounter);
    Log::Dialog("[DLG-INJ] Target: opcode_ask(slot=%d) via dispatch table[0x%02X]",
                TEST_SLOT_ASK, OPCODE_ASK_INDEX);
    Log::Dialog("[DLG-INJ] FF8OPC_VERSION = %s", FF8OPC_VERSION);
    LogResolvedAddresses();

    // v0.15.6: encode the custom prompt + 3 options into our static buffer.
    // The FieldDialog hook on field_get_dialog_string will return this
    // buffer instead of the natural game data when SetOverride is active.
    const char* customText = "Mode?\nManual\nAuto\nOriginal";
    int encodedLen = EncodeFf8(customText, s_overrideBuffer, OVERRIDE_BUFFER_SIZE);
    Log::Dialog("[DLG-INJ] v0.15.6 override text: \"%s\" -> %d bytes encoded", customText, encodedLen);
    {
        char hex[256];
        int hp = 0;
        for (int i = 0; i < encodedLen && hp < (int)sizeof(hex) - 4; ++i) {
            hp += snprintf(hex + hp, sizeof(hex) - hp, "%02X ", s_overrideBuffer[i]);
        }
        Log::Dialog("[DLG-INJ] override buffer hex: %s", hex);
    }

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
            uint8_t mySlotBit = (uint8_t)(1u << TEST_SLOT_ASK);
            if (askMask & mySlotBit) {
                Log::Dialog("[DLG-INJ] WARNING: gameObj.D2 bit %d already set; "
                            "opcode_ask will return 5 without rendering.",
                            TEST_SLOT_ASK);
            }
        }
    }

    // Phantom ctx with v0.15.5.1 ASK-pending tracking bytes.
    memset(s_phantomCtx, 0, sizeof(s_phantomCtx));
    s_phantomCtx[CTX_SP_OFFSET] = (uint8_t)TEST_SP_ASK;
    s_phantomCtx[0x174] = 0;  // shift count
    s_phantomCtx[0x175] = 1;  // bit 0 set (forces .alloc path)

    *(int32_t*)(s_phantomCtx + (TEST_SP_ASK - 5) * 4) = TEST_SLOT_ASK;
    *(int32_t*)(s_phantomCtx + (TEST_SP_ASK - 4) * 4) = TEST_MSG_ID_ASK;
    *(int32_t*)(s_phantomCtx + (TEST_SP_ASK - 3) * 4) = TEST_ASK_FIRST_Q;
    *(int32_t*)(s_phantomCtx + (TEST_SP_ASK - 2) * 4) = TEST_ASK_LAST_Q;
    *(int32_t*)(s_phantomCtx + (TEST_SP_ASK - 1) * 4) = TEST_ASK_CUR_Q;
    *(int32_t*)(s_phantomCtx + (TEST_SP_ASK - 0) * 4) = TEST_ASK_AUX;

    Log::Dialog("[DLG-INJ] phantom ctx ASK: SP=%d at +0x%X",
                TEST_SP_ASK, (unsigned)CTX_SP_OFFSET);
    Log::Dialog("[DLG-INJ]   ctx[+0x174]=%u ctx[+0x175]=0x%02X (ASK-pending tracking)",
                s_phantomCtx[0x174], s_phantomCtx[0x175]);
    Log::Dialog("[DLG-INJ]   stack: slot=%d msg_id=%d firstQ=%d lastQ=%d curQ=%d aux=%d",
                TEST_SLOT_ASK, TEST_MSG_ID_ASK,
                TEST_ASK_FIRST_Q, TEST_ASK_LAST_Q, TEST_ASK_CUR_Q, TEST_ASK_AUX);

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
            Log::Dialog("[DLG-INJ] WARNING: exception reading dispatch table.");
        }
    }

    Log::Dialog("[DLG-INJ] FIRING opcode_ask(0x%08X)(ctx=0x%08X)...",
                opcodeAskAddr, (uint32_t)(uintptr_t)s_phantomCtx);

    // v0.15.5.2: sub_49FD50(slot) sets pCurrentDialogSlot for arrow input.
    {
        const uint32_t SUB_49FD50_ADDR = 0x0049FD50;
        typedef void (__cdecl *sub_49fd50_t)(int);
        sub_49fd50_t sub_49fd50_fn = (sub_49fd50_t)(uintptr_t)SUB_49FD50_ADDR;

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

        Log::Dialog("[DLG-INJ] sub_49FD50(%d): pCurrentDialogSlot 0x%02X -> 0x%02X",
                    TEST_SLOT_ASK, preCurSlot, postCurSlot);
    }

    // v0.15.6.1 Phase 2b: activate the post-ASK slot patch BEFORE calling
    // opcode_ask. Inside Hook_opcode_ask (field_dialog.cpp), after s_origAsk
    // returns and before ScanAndSpeakChoiceWindows reads slot+0x08, the
    // override-active branch overwrites slot+0x08 with our override buffer.
    // The TTS path then decodes our text instead of the natural field text.
    SetOverride(TEST_SLOT_ASK, (const char*)s_overrideBuffer);
    Log::Dialog("[DLG-INJ] v0.15.6.1 SetOverride active for slot %d; "
                "opcode_ask post-call patch will swap slot[+0x08] = 0x%08X",
                TEST_SLOT_ASK, (uint32_t)(uintptr_t)s_overrideBuffer);

    typedef int (__cdecl *opcode_ask_t)(void*);
    opcode_ask_t opcode_ask_fn = (opcode_ask_t)(uintptr_t)opcodeAskAddr;

    int retCode = -999;
    bool crashed = false;
    __try {
        retCode = opcode_ask_fn(s_phantomCtx);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        crashed = true;
    }

    // Clear immediately after the opcode returns so other engine callers
    // of opcode_ask (e.g. natural game scripts) don't trigger the post-ASK
    // slot patch.
    ClearOverride();
    Log::Dialog("[DLG-INJ] v0.15.6.1 ClearOverride called");

    if (crashed) {
        Log::Dialog("[DLG-INJ] *** opcode_ask RAISED EXCEPTION ***");
        if (ScreenReader::IsAvailable()) {
            ScreenReader::Speak("Dialog inject phase two B crashed. See dialog log.", true);
        }
        return;
    }

    Log::Dialog("[DLG-INJ] opcode_ask returned %d  "
                "(1=wait, 5=slot-busy, 3=advance, other=undocumented)",
                retCode);

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

    if (postOk && FF8Addresses::pWindowsArray != nullptr) {
        uint8_t* slotBase = FF8Addresses::pWindowsArray + (TEST_SLOT_ASK * WIN_OBJ_STRIDE);
        __try {
            uint8_t firstQ = *(uint8_t*)(slotBase + 0x29);
            uint8_t lastQ  = *(uint8_t*)(slotBase + 0x2A);
            uint8_t curQ_2 = *(uint8_t*)(slotBase + 0x2C);
            char* text1 = *(char**)(slotBase + 0x08);
            Log::Dialog("[DLG-INJ] POST ASK slot[+0x29]firstQ=%u slot[+0x2A]lastQ=%u slot[+0x2C]curQ=%u text1=0x%08X (override=0x%08X)",
                        firstQ, lastQ, curQ_2,
                        (uint32_t)(uintptr_t)text1,
                        (uint32_t)(uintptr_t)s_overrideBuffer);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            Log::Dialog("[DLG-INJ] POST ASK choice fields unread.");
        }
    }

    AnnouncePhase2Result(TEST_SLOT_ASK, retCode);

    s_pollActive    = true;
    s_pollSlot      = TEST_SLOT_ASK;
    s_pollStartMs   = GetTickCount();
    s_lastPollMs    = 0;
    s_pollSampleIdx = 0;
    Log::Dialog("[DLG-INJ] ASK slot poll active for %u ms at %u ms cadence.",
                POLL_DURATION_MS, POLL_INTERVAL_MS);
}

}  // namespace DialogInject
