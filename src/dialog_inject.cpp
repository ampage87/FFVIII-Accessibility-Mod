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
//            pointers within that range. END-TO-END SUCCESS.
// v0.15.7:   Answer detection. v0.15.6.2 confirmed Aaron hears the prompt
//            and three options through the engine-rendered ASK. v0.15.7
//            polls slot+0x2B (cursor position) per frame while a Phase 2B
//            ASK is open. Each cursor change announces "Manual selected"
//            / "Auto selected" / "Original selected". On commit (gameObj.D2
//            bit for our slot clears OR slot state leaves 0xD), the final
//            cursor value is captured as the answer and announced as "You
//            chose X". GetLastAnswer() returns 1/2/3 for v0.15.8's chase
//            wiring, or -1 if no commit has happened yet. Pure read of
//            slot bytes; no engine state writes; no new hooks. Also fixes
//            the v0.15.5.1 POST-ASK readback that was reading slot+0x2C
//            (aux byte) and labeling it curQ -- curQ is at 0x2B per
//            field_dialog.cpp's offsets and the v0.15.6.2 BAT decoder.
// v0.15.7.1: Premature commit fix. v0.15.7 BAT showed answer-detection
//            firing 'commit reason=state left 0xD' on the very first
//            poll, before Aaron pressed any key. Root cause: dialog
//            state at slot+0x24 starts at 0x00, progresses 0->1->0xD over
//            ~450 ms. v0.15.7's commit detector treated 'state != 0xD' as
//            a commit signal, so the initial transient state==0 satisfied
//            'left 0xD' before 0xD was ever entered. Fix: track whether
//            the cursor-active state (0xD) has been entered at least once;
//            gate the 'state left 0xD' commit branch on having seen 0xD
//            first. Same belt-and-braces logic for the gameObj.D2 bit:
//            the bit IS set immediately after opcode_ask returns (BAT
//            log: PRE D2=0x00 -> POST D2=0x04), but if we ever armed
//            before the bit was set we'd trip the same way. Also adds the
//            entry-state observed time to log lines for diagnostic value.
//            Doc-only fix elsewhere: the FF8 confirm key is X, not Enter.
// v0.15.8:   Public OpenAsk() API. Refactors Phase2_TestAsk's body into
//            OpenAskInternal which takes a caller-supplied prompt + choice
//            list, encoded into the override buffer with the same EncodeFf8
//            utility. Phase2_TestAsk now calls OpenAskInternal with the
//            hardcoded 'Mode? / Manual / Auto / Original' test buffer.
//            New public OpenAsk() takes the same parameters and arms answer
//            detection identically. CurQToOptionName replaced with
//            s_phase2ChoiceNames[][32] populated by OpenAskInternal so
//            the cursor-change announcer can speak any caller-supplied
//            choice. ResetLastAnswer() exposed for callers that need to
//            distinguish 'stale answer from previous ASK' from 'still
//            waiting'. chase_ask_overlay v0.15.8 uses this to render the
//            chase mode prompt through the engine's native dialog system
//            (replacing v0.15.2.2's TTS-only fallback).

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
// EMPIRICAL ARG MAP (confirmed v0.15.5.1 BAT post-fire decode of slot fields,
// CORRECTED v0.15.7 against field_dialog.cpp's offset constants):
//   stack[SP-5] (ctx[+0x04]) -> edi -> slot index
//   stack[SP-4] (ctx[+0x08]) -> ecx -> msg_id
//   stack[SP-3] (ctx[+0x0C]) -> SWO_ASK arg2 -> slot+0x29 (firstQ)
//   stack[SP-2] (ctx[+0x10]) -> SWO_ASK arg3 -> slot+0x2A (lastQ)
//   stack[SP-1] (ctx[+0x14]) -> SWO_ASK arg4 -> slot+0x2B (curQ, clamped to [firstQ, lastQ])
//   stack[SP-0] (ctx[+0x18]) -> SWO_ASK arg5 -> slot+0x2C (aux)
//
// v0.15.5.1's commit comment had the offsets crossed (claimed curQ at 0x2C,
// aux at 0x2B). The v0.15.6.2 BAT log confirmed the correct mapping: the
// FieldDialog [ASK] hook in field_dialog.cpp reads curChoice from 0x2B and
// produced "curChoice=1" matching our TEST_ASK_CUR_Q=1. v0.15.7's answer
// detection reads 0x2B for cursor changes accordingly.
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
// v0.15.7: cursor position byte. The engine updates this on Up/Down arrows
// while the ASK dialog is open, clamped to [firstQ, lastQ]. We poll it for
// answer detection. (firstQ at 0x29, lastQ at 0x2A, curQ at 0x2B, aux at
// 0x2C -- per field_dialog.cpp offsets and v0.15.6.2 BAT confirmation.)
static const size_t WIN_OBJ_CUR_Q_OFFSET        = 0x2B;

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
// v0.15.7 Phase 2b answer-detection state
//
// Set by Phase2_TestAsk after opcode_ask returns successfully (retCode == 1
// = wait-for-answer). Update() polls per frame while active:
//   - Read slot+0x2B (curQ). On change, speak "<Option> selected".
//   - Watch gameObj.D2 bit for our slot AND slot state field. Commit is
//     either bit-clear (engine consumed our ASK and moved on) or state
//     leaves 0xD (engine transitioned past the cursor-active state).
//   - On commit: capture final curQ, speak "You chose <Option>", set
//     s_phase2LastAnswer for GetLastAnswer(), clear active flag.
//
// Initialized to 0xFF for s_phase2LastCurQ so the first poll after fire
// always announces the initial cursor position (matches our TEST_ASK_CUR_Q
// default). Single-threaded: only the game thread reads/writes these.
// ============================================================================
static bool   s_phase2Active     = false;
static int    s_phase2Slot       = -1;
static uint8_t s_phase2LastCurQ  = 0xFF;
static int    s_phase2LastAnswer = -1;
// v0.15.7.1: gates the 'state left 0xD' / 'D2 bit clear' commit branches.
// Set true the first time Update() observes state == 0xD AND the D2 bit
// for our slot is set. Until then, neither commit signal counts -- the
// dialog hasn't entered cursor-active state yet, so 'leaving' it is
// meaningless. The 60s timeout still fires regardless of this flag so
// a stuck arming can't poll forever.
static bool   s_phase2SeenActive = false;
// Sanity timeout: if neither commit signal fires within this window, force
// a clean shutdown of answer detection so we don't poll forever. The 60s
// ceiling is generous for any conceivable user pondering time.
static DWORD  s_phase2StartMs    = 0;

// v0.15.8: caller-supplied choice names for the open ASK. Populated by
// OpenAskInternal when the ASK is armed. Update()'s cursor-change
// announcer reads s_phase2ChoiceNames[curQ - 1] for the spoken name and
// s_phase2ChoiceCount to bounds-check curQ. Static-storage so the names
// outlive the caller's stack frame; copied via strncpy at OpenAsk time
// because some callers may pass pointers into transient memory.
//
// v0.15.8.1: PHASE2_NAME_CAP bumped 32 -> 64 to fit descriptive choice
// labels (e.g. "Manual: one battle per field"). The cursor-announce msg
// buffer in Update() is also bumped 64 -> 128 to fit "<name> selected"
// for any name up to PHASE2_NAME_CAP.
static const int PHASE2_MAX_CHOICES   = 8;
static const int PHASE2_NAME_CAP      = 64;
static char  s_phase2ChoiceNames[PHASE2_MAX_CHOICES][PHASE2_NAME_CAP] = {};
static int   s_phase2ChoiceCount     = 0;
// v0.15.8.1: when false, suppress Update()'s commit-branch "You chose <name>"
// announce. Callers like chase_ask_overlay handle their own brief commit
// announce and don't want the generic one stacking on top. Set per-call
// in OpenAskInternal; defaults true so Phase2_TestAsk is unchanged.
static bool  s_phase2AnnounceCommit  = true;
static const DWORD PHASE2_TIMEOUT_MS = 60000;

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
// v0.15.7 Phase 2b answer detection
// ============================================================================
int GetLastAnswer() {
    return s_phase2LastAnswer;
}

// v0.15.8: Replaces the v0.15.7 hardcoded "Manual/Auto/Original" switch
// with a lookup into s_phase2ChoiceNames, populated by OpenAskInternal
// from caller-supplied strings. curQ is 1-based and bounded by
// s_phase2ChoiceCount; values outside that range return nullptr so the
// caller falls back to a generic "Choice N" announce.
static const char* CurQToOptionName(uint8_t curQ) {
    if (curQ < 1 || (int)curQ > s_phase2ChoiceCount) return nullptr;
    int idx = (int)curQ - 1;
    if (idx < 0 || idx >= PHASE2_MAX_CHOICES) return nullptr;
    if (s_phase2ChoiceNames[idx][0] == '\0') return nullptr;
    return s_phase2ChoiceNames[idx];
}

static uint8_t ReadSlotCurQ(int slot) {
    if (FF8Addresses::pWindowsArray == nullptr) return 0xFF;
    if (slot < 0 || slot >= 8) return 0xFF;
    uint8_t* slotBase = FF8Addresses::pWindowsArray + (slot * WIN_OBJ_STRIDE);
    uint8_t curQ = 0xFF;
    __try {
        curQ = *(uint8_t*)(slotBase + WIN_OBJ_CUR_Q_OFFSET);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 0xFF;
    }
    return curQ;
}

static uint32_t ReadSlotState(int slot) {
    if (FF8Addresses::pWindowsArray == nullptr) return 0xFFFFFFFFu;
    if (slot < 0 || slot >= 8) return 0xFFFFFFFFu;
    uint8_t* slotBase = FF8Addresses::pWindowsArray + (slot * WIN_OBJ_STRIDE);
    uint32_t state = 0xFFFFFFFFu;
    __try {
        state = *(uint32_t*)(slotBase + WIN_OBJ_STATE_OFFSET);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 0xFFFFFFFFu;
    }
    return state;
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
    // v0.15.7: disarm answer detection on shutdown so a subsequent re-init
    // doesn't reuse stale state. Don't clear s_phase2LastAnswer -- a caller
    // that has already read it via GetLastAnswer() may rely on its value
    // until a new ASK is fired.
    s_phase2Active     = false;
    s_phase2SeenActive = false;
    s_phase2Slot       = -1;
    s_initialized = false;
    Log::Dialog("[DLG-INJ] Shutdown.");
}

void Update() {
    if (!s_initialized) return;

    // v0.15.7: per-frame answer-detection poll while a Phase 2B ASK is open.
    // Runs independently of (and concurrently with) the 3-second slot-state
    // diagnostic poll below. Single-threaded so no synchronization needed.
    if (s_phase2Active) {
        uint8_t curQ = ReadSlotCurQ(s_phase2Slot);
        uint8_t askMask = 0, winMask = 0, mesMask = 0;
        bool maskOk = ReadGameObjMasks(askMask, winMask, mesMask);
        uint32_t state = ReadSlotState(s_phase2Slot);
        uint8_t mySlotBit = (uint8_t)(1u << s_phase2Slot);
        bool slotBitSet = maskOk && ((askMask & mySlotBit) != 0);
        bool slotBitClear = maskOk && ((askMask & mySlotBit) == 0);
        bool stateAtAsk = (state == 0x0000000Du);
        bool stateLeftAsk = (state != 0xFFFFFFFFu) && (state != 0x0000000Du);
        DWORD elapsed = GetTickCount() - s_phase2StartMs;
        bool timedOut = (elapsed >= PHASE2_TIMEOUT_MS);

        // v0.15.7.1: gate commit signals on having entered the active
        // state at least once. The dialog's state field progresses
        // 0 -> 1 -> 0xD over ~450 ms after opcode_ask returns. Without
        // this gate, the initial transient state == 0 satisfies
        // 'state != 0xD' and we trip the commit before the dialog has
        // even rendered. We require BOTH state == 0xD AND the D2 bit
        // for our slot to be set before we'll honor 'state left 0xD'
        // or 'D2 bit clear' as commit signals.
        if (!s_phase2SeenActive && stateAtAsk && slotBitSet) {
            s_phase2SeenActive = true;
            Log::Dialog("[DLG-INJ] v0.15.7.1 active-state observed slot=%d t+%ums "
                        "(state=0x%X D2=0x%02X); commit gating now armed",
                        s_phase2Slot, elapsed, state, askMask);
        }

        // Cursor-change announce. Skip the 0xFF sentinel (read failure or
        // pre-render) and skip values outside [firstQ, lastQ] -- the engine
        // clamps them, but we belt-and-brace check.
        // v0.15.8: bound check uses s_phase2ChoiceCount (set by
        // OpenAskInternal from caller's numChoices) instead of hardcoded 3.
        if (curQ != s_phase2LastCurQ && curQ != 0xFF
            && curQ >= 1 && (int)curQ <= s_phase2ChoiceCount) {
            const char* name = CurQToOptionName(curQ);
            if (name != nullptr) {
                // v0.15.8.1: msg buffer bumped 64 -> 128 to fit
                // descriptive choice labels (PHASE2_NAME_CAP=64) plus
                // " selected" suffix without snprintf truncation.
                char msg[128];
                snprintf(msg, sizeof(msg), "%s selected", name);
                Log::Dialog("[DLG-INJ] v0.15.7 cursor-change slot=%d curQ %u->%u announce=\"%s\"",
                            s_phase2Slot, s_phase2LastCurQ, curQ, msg);
                if (ScreenReader::IsAvailable()) {
                    // interrupt=false to queue after any in-flight choice TTS
                    // (ScanAndSpeakChoiceWindows or our diagnostic announce).
                    ScreenReader::Speak(msg, false);
                }
            } else {
                Log::Dialog("[DLG-INJ] v0.15.7 cursor-change slot=%d curQ %u->%u (no name; out of range)",
                            s_phase2Slot, s_phase2LastCurQ, curQ);
            }
            s_phase2LastCurQ = curQ;
        }

        // Commit detection.
        // v0.15.7.1: 'state left 0xD' and 'D2 bit clear' only count after
        // s_phase2SeenActive went true (i.e., we observed the dialog
        // actively in cursor-input state). Timeout is unconditional.
        bool commitFromState = s_phase2SeenActive && stateLeftAsk;
        bool commitFromMask  = s_phase2SeenActive && slotBitClear;
        if (commitFromState || commitFromMask || timedOut) {
            const char* reason = timedOut ? "timeout"
                                : commitFromMask ? "D2 bit clear"
                                : "state left 0xD";
            // Capture the final cursor value. We use s_phase2LastCurQ
            // (the most recent observed value) rather than re-reading the
            // slot at commit time -- the engine may have already cleared
            // or repurposed the slot, but we know what was selected the
            // last time the user moved the cursor.
            // v0.15.8: bound check now uses s_phase2ChoiceCount; default
            // fallback is 1 (the first choice, which OpenAskInternal
            // typically assigns to the safe option).
            int answer = (int)s_phase2LastCurQ;
            if (answer < 1 || answer > s_phase2ChoiceCount) {
                // No cursor moves observed; fall back to the first
                // choice. On timeout this is the only meaningful value;
                // on legitimate commit the user pressed X on the default
                // choice without moving.
                answer = 1;
                Log::Dialog("[DLG-INJ] v0.15.7 commit reason=%s no cursor moves observed; "
                            "defaulting answer to %d (%s)", reason, answer,
                            CurQToOptionName(1) ? CurQToOptionName(1) : "Choice 1");
            } else {
                Log::Dialog("[DLG-INJ] v0.15.7 commit reason=%s capturing answer=%d",
                            reason, answer);
            }
            s_phase2LastAnswer = answer;

            const char* name = CurQToOptionName((uint8_t)answer);
            if (name != nullptr && !timedOut && s_phase2AnnounceCommit
                && ScreenReader::IsAvailable()) {
                // v0.15.8.1: msg buffer bumped 64 -> 128 to match the
                // cursor-announce buffer above. Commit announce is gated
                // on s_phase2AnnounceCommit so callers (chase_ask_overlay)
                // can suppress it when they speak their own brief
                // mode-specific commit message.
                char msg[128];
                snprintf(msg, sizeof(msg), "You chose %s", name);
                Log::Dialog("[DLG-INJ] v0.15.7 announce=\"%s\"", msg);
                ScreenReader::Speak(msg, false);
            } else if (name != nullptr && !timedOut && !s_phase2AnnounceCommit) {
                Log::Dialog("[DLG-INJ] v0.15.8.1 commit announce suppressed by caller "
                            "(answer=%d name=\"%s\")", answer, name);
            }
            s_phase2Active     = false;
            s_phase2SeenActive = false;
            s_phase2Slot       = -1;
        }
    }

    // 3-second slot-state diagnostic poll (unchanged from v0.15.6).
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

// ============================================================================
// v0.15.8: OpenAskInternal -- the shared core for Phase2_TestAsk and OpenAsk.
//
// Encodes a caller-supplied prompt + choices into the override buffer,
// fires opcode_ask through the same v0.15.5.x recipe, and arms answer
// detection identically to v0.15.7's Phase2_TestAsk. Returns the opcode
// return code: 1 on success (wait-for-answer), 5 on slot-busy, -999 on
// crash, other on validation failure.
//
// Phase2_TestAsk is a thin wrapper that calls this with the hardcoded
// 'Mode? / Manual / Auto / Original' test buffer (slot=2, defaultCursor=1).
// The public OpenAsk() also calls this with caller-supplied parameters.
// ============================================================================
static int OpenAskInternal(const char* prompt,
                           const char* const* choices,
                           int numChoices,
                           int defaultCursor,
                           int slot,
                           const char* logBanner,
                           bool announceCommit) {
    if (!s_initialized) {
        Log::Dialog("[DLG-INJ] OpenAskInternal: module not initialized; aborting.");
        return -1;
    }
    if (!FF8Addresses::IsOnField()) {
        Log::Dialog("[DLG-INJ] OpenAskInternal: not in field mode (mode=%u); aborting.",
                    (unsigned)FF8Addresses::GetCurrentMode());
        if (ScreenReader::IsAvailable()) {
            ScreenReader::Speak("Dialog inject requires field mode.", true);
        }
        return -2;
    }
    if (FF8Addresses::opcode_ask == 0 || FF8Addresses::pWindowsArray == nullptr) {
        Log::Dialog("[DLG-INJ] OpenAskInternal: addresses not resolved; aborting.");
        if (ScreenReader::IsAvailable()) {
            ScreenReader::Speak("Dialog inject ASK addresses missing.", true);
        }
        return -3;
    }
    if (prompt == nullptr || choices == nullptr) {
        Log::Dialog("[DLG-INJ] OpenAskInternal: null prompt or choices; aborting.");
        return -4;
    }
    if (numChoices < 1 || numChoices > PHASE2_MAX_CHOICES) {
        Log::Dialog("[DLG-INJ] OpenAskInternal: numChoices=%d out of range [1, %d]; aborting.",
                    numChoices, PHASE2_MAX_CHOICES);
        return -5;
    }
    if (slot < 0 || slot >= 8) {
        Log::Dialog("[DLG-INJ] OpenAskInternal: slot=%d out of range; aborting.", slot);
        return -6;
    }
    if (defaultCursor < 1 || defaultCursor > numChoices) {
        Log::Dialog("[DLG-INJ] OpenAskInternal: defaultCursor=%d out of [1, %d]; clamping to 1.",
                    defaultCursor, numChoices);
        defaultCursor = 1;
    }

    s_testCounter++;
    Log::Dialog("[DLG-INJ] ===== %s TEST #%d START =====",
                logBanner ? logBanner : "OPEN-ASK", s_testCounter);
    Log::Dialog("[DLG-INJ] Target: opcode_ask(slot=%d) via dispatch table[0x%02X]",
                slot, OPCODE_ASK_INDEX);
    Log::Dialog("[DLG-INJ] FF8OPC_VERSION = %s", FF8OPC_VERSION);
    LogResolvedAddresses();

    // v0.15.8: build the override buffer text from prompt + choices.
    // Format: "<prompt>\n<choice1>\n<choice2>\n...<choiceN>\0"
    // matching the v0.15.6 hardcoded layout (Line 0 = prompt, Lines 1-N = choices).
    char composed[256];
    int cp = 0;
    int promptLen = (int)strlen(prompt);
    if (promptLen > 200) promptLen = 200;
    memcpy(composed + cp, prompt, promptLen);
    cp += promptLen;
    for (int i = 0; i < numChoices; i++) {
        if (cp >= (int)sizeof(composed) - 2) break;
        composed[cp++] = '\n';
        const char* ch = choices[i] ? choices[i] : "";
        int chLen = (int)strlen(ch);
        if (chLen > 60) chLen = 60;
        if (cp + chLen >= (int)sizeof(composed) - 1) break;
        memcpy(composed + cp, ch, chLen);
        cp += chLen;
    }
    composed[cp] = '\0';

    int encodedLen = EncodeFf8(composed, s_overrideBuffer, OVERRIDE_BUFFER_SIZE);
    Log::Dialog("[DLG-INJ] v0.15.8 override text: \"%s\" -> %d bytes encoded",
                composed, encodedLen);
    {
        char hex[256];
        int hp = 0;
        for (int i = 0; i < encodedLen && hp < (int)sizeof(hex) - 4; ++i) {
            hp += snprintf(hex + hp, sizeof(hex) - hp, "%02X ", s_overrideBuffer[i]);
        }
        Log::Dialog("[DLG-INJ] override buffer hex: %s", hex);
    }

    // v0.15.8: copy choice names into static storage for Update()'s
    // cursor-change announcer to read. strncpy + explicit terminator
    // because some callers may pass strings longer than PHASE2_NAME_CAP.
    for (int i = 0; i < PHASE2_MAX_CHOICES; i++) {
        s_phase2ChoiceNames[i][0] = '\0';
    }
    for (int i = 0; i < numChoices; i++) {
        const char* ch = choices[i] ? choices[i] : "";
        strncpy(s_phase2ChoiceNames[i], ch, PHASE2_NAME_CAP - 1);
        s_phase2ChoiceNames[i][PHASE2_NAME_CAP - 1] = '\0';
    }
    s_phase2ChoiceCount = numChoices;

    SlotSnapshot pre = {0, 0, 0, 0};
    bool preOk = ReadSlotSnapshot(slot, pre);
    Log::Dialog("[DLG-INJ] PRE  ASK slot=%d trans=0x%04X vel=0x%04X state=0x%08X field16=0x%02X %s",
                slot,
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
            uint8_t mySlotBit = (uint8_t)(1u << slot);
            if (askMask & mySlotBit) {
                Log::Dialog("[DLG-INJ] WARNING: gameObj.D2 bit %d already set; "
                            "opcode_ask will return 5 without rendering.",
                            slot);
            }
        }
    }

    // Phantom ctx with v0.15.5.1 ASK-pending tracking bytes.
    memset(s_phantomCtx, 0, sizeof(s_phantomCtx));
    s_phantomCtx[CTX_SP_OFFSET] = (uint8_t)TEST_SP_ASK;
    s_phantomCtx[0x174] = 0;  // shift count
    s_phantomCtx[0x175] = 1;  // bit 0 set (forces .alloc path)

    *(int32_t*)(s_phantomCtx + (TEST_SP_ASK - 5) * 4) = slot;
    *(int32_t*)(s_phantomCtx + (TEST_SP_ASK - 4) * 4) = 0;            // msg_id (unused under override)
    *(int32_t*)(s_phantomCtx + (TEST_SP_ASK - 3) * 4) = 1;            // firstQ
    *(int32_t*)(s_phantomCtx + (TEST_SP_ASK - 2) * 4) = numChoices;   // lastQ
    *(int32_t*)(s_phantomCtx + (TEST_SP_ASK - 1) * 4) = defaultCursor;// curQ
    *(int32_t*)(s_phantomCtx + (TEST_SP_ASK - 0) * 4) = 0;            // aux

    Log::Dialog("[DLG-INJ] phantom ctx ASK: SP=%d at +0x%X",
                TEST_SP_ASK, (unsigned)CTX_SP_OFFSET);
    Log::Dialog("[DLG-INJ]   ctx[+0x174]=%u ctx[+0x175]=0x%02X (ASK-pending tracking)",
                s_phantomCtx[0x174], s_phantomCtx[0x175]);
    Log::Dialog("[DLG-INJ]   stack: slot=%d msg_id=0 firstQ=1 lastQ=%d curQ=%d aux=0",
                slot, numChoices, defaultCursor);

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
            sub_49fd50_fn(slot);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            Log::Dialog("[DLG-INJ] WARNING: sub_49FD50(%d) raised exception", slot);
        }

        uint8_t postCurSlot = 0xFF;
        __try { postCurSlot = *pCurrentDialogSlot; }
        __except (EXCEPTION_EXECUTE_HANDLER) {}

        Log::Dialog("[DLG-INJ] sub_49FD50(%d): pCurrentDialogSlot 0x%02X -> 0x%02X",
                    slot, preCurSlot, postCurSlot);
    }

    SetOverride(slot, (const char*)s_overrideBuffer);
    Log::Dialog("[DLG-INJ] v0.15.6.1 SetOverride active for slot %d; "
                "opcode_ask post-call patch will swap slot[+0x08] = 0x%08X",
                slot, (uint32_t)(uintptr_t)s_overrideBuffer);

    typedef int (__cdecl *opcode_ask_t)(void*);
    opcode_ask_t opcode_ask_fn = (opcode_ask_t)(uintptr_t)opcodeAskAddr;

    int retCode = -999;
    bool crashed = false;
    __try {
        retCode = opcode_ask_fn(s_phantomCtx);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        crashed = true;
    }

    ClearOverride();
    Log::Dialog("[DLG-INJ] v0.15.6.1 ClearOverride called");

    if (crashed) {
        Log::Dialog("[DLG-INJ] *** opcode_ask RAISED EXCEPTION ***");
        if (ScreenReader::IsAvailable()) {
            ScreenReader::Speak("Dialog inject phase two B crashed. See dialog log.", true);
        }
        return -999;
    }

    Log::Dialog("[DLG-INJ] opcode_ask returned %d  "
                "(1=wait, 5=slot-busy, 3=advance, other=undocumented)",
                retCode);

    SlotSnapshot post = {0, 0, 0, 0};
    bool postOk = ReadSlotSnapshot(slot, post);
    Log::Dialog("[DLG-INJ] POST ASK slot=%d trans=0x%04X vel=0x%04X state=0x%08X field16=0x%02X %s",
                slot,
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
        uint8_t* slotBase = FF8Addresses::pWindowsArray + (slot * WIN_OBJ_STRIDE);
        __try {
            uint8_t firstQ = *(uint8_t*)(slotBase + 0x29);
            uint8_t lastQ  = *(uint8_t*)(slotBase + 0x2A);
            uint8_t curQ   = *(uint8_t*)(slotBase + 0x2B);
            uint8_t aux    = *(uint8_t*)(slotBase + 0x2C);
            char* text1 = *(char**)(slotBase + 0x08);
            Log::Dialog("[DLG-INJ] POST ASK slot[+0x29]firstQ=%u slot[+0x2A]lastQ=%u "
                        "slot[+0x2B]curQ=%u slot[+0x2C]aux=%u text1=0x%08X (override=0x%08X)",
                        firstQ, lastQ, curQ, aux,
                        (uint32_t)(uintptr_t)text1,
                        (uint32_t)(uintptr_t)s_overrideBuffer);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            Log::Dialog("[DLG-INJ] POST ASK choice fields unread.");
        }
    }

    // Arm answer detection only on the wait-for-answer success path.
    if (retCode == 1) {
        s_phase2Active     = true;
        s_phase2Slot       = slot;
        s_phase2LastCurQ   = 0xFF;   // forces first-poll announce
        s_phase2LastAnswer = -1;
        s_phase2SeenActive = false;  // v0.15.7.1
        s_phase2AnnounceCommit = announceCommit;  // v0.15.8.1
        s_phase2StartMs    = GetTickCount();
        Log::Dialog("[DLG-INJ] v0.15.7 answer-detection armed for slot %d (timeout %u ms, "
                    "announceCommit=%d)",
                    s_phase2Slot, PHASE2_TIMEOUT_MS, (int)announceCommit);
    } else {
        Log::Dialog("[DLG-INJ] v0.15.7 answer-detection NOT armed (retCode=%d != 1)",
                    retCode);
    }

    return retCode;
}

void Phase2_TestAsk() {
    // Hardcoded test buffer: 'Mode? / Manual / Auto / Original' on slot 2,
    // default cursor on Manual. Used by Shift+F12 for the standalone
    // diagnostic test path. Will be removed once chase wiring is fully
    // proven (per Aaron's v0.15.8 plan: "Shift+F12 dialog test becomes
    // redundant").
    //
    // v0.15.8.1: passes announceCommit=true so the diagnostic still hears
    // "You chose <name>" -- Shift+F12 has no other commit announce, so
    // suppressing it would leave the test with no audible commit signal.
    static const char* kTestChoices[] = { "Manual", "Auto", "Original" };
    int retCode = OpenAskInternal("Mode?", kTestChoices, 3, 1, 2,
                                  "PHASE 2B", true);

    AnnouncePhase2Result(2, retCode);

    // Also run the 3-second slot-state diagnostic poll for visibility
    // into the open transition, as v0.15.4-v0.15.7.1 did.
    s_pollActive    = true;
    s_pollSlot      = 2;
    s_pollStartMs   = GetTickCount();
    s_lastPollMs    = 0;
    s_pollSampleIdx = 0;
    Log::Dialog("[DLG-INJ] ASK slot poll active for %u ms at %u ms cadence.",
                POLL_DURATION_MS, POLL_INTERVAL_MS);
}

// ============================================================================
// v0.15.8: public OpenAsk + ResetLastAnswer
// v0.15.8.1: announceCommit param plumbs through to OpenAskInternal
// ============================================================================
bool OpenAsk(const char* prompt,
             const char* const* choices,
             int numChoices,
             int defaultCursor,
             int slot,
             bool announceCommit) {
    int retCode = OpenAskInternal(prompt, choices, numChoices, defaultCursor,
                                  slot, "OPEN-ASK", announceCommit);
    return (retCode == 1);
}

void ResetLastAnswer() {
    s_phase2LastAnswer = -1;
    Log::Dialog("[DLG-INJ] v0.15.8 ResetLastAnswer called");
}

}  // namespace DialogInject
