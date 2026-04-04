// field_dialog.cpp - Field dialog TTS via MinHook detour hooks
//
// v04.00-diag2: MinHook detours on opcode handlers confirmed working.
// v04.01: Wire decoded dialog text to TTS. Deduplication. Pointer validation.
// v04.02: Fix choice dialogs — parenthesis-based extraction.
// v04.03: Robust choice extraction using line-index splitting with firstQ/lastQ.
//         Fixed apostrophe (0x43) reading as backtick.
// v04.04: Fix multi-page dialog duplication via suffix detection.
// v04.05: Fix speech interruption during walk-and-talk (queue mode).
// v04.06: SAPI dedicated game audio channel (survives NVDA key cancellation).
// v04.07: Fix missing simultaneous dialogs. Scan ALL 8 windows on every hook
//         fire, speak any window whose text is new. Per-window dedup tracking.
// v04.08: Fix catastrophic duplication in choice dialogs.
// v04.09: Fix no speech after choice dialog dismissed.
// v04.10: Remove isActiveChoice flag approach entirely.
// v04.11: DIAGNOSTIC BUILD. Log every hook fire + window text snapshot.
// v04.12: DIAGNOSTIC v2. Confirmed 3-18 second gaps where NO hooks fire.
// v04.13: HYBRID APPROACH. Hooks + PollWindows() polling fallback from
//         accessibility thread. Thread-safe via CRITICAL_SECTION.
// v04.14: Hook MESW (0x46) — message + wait.
// v04.15: RAWDUMP diagnostic — logs raw hex of ALL window slots every 2s
//         regardless of pointer validation, to find hidden text during gaps.
//         Fix [C0] junk being spoken (silently skip unknown bytes in decoder).
// v04.16: Hook field_get_dialog_string to catch text that bypasses window
//         system (Squall's thoughts, fixed-position dialogs). Deferred
//         speaking: text fetched but not spoken by opcode hooks within
//         500ms gets spoken by the poll thread. Min 3-char text filter.
// v04.17: Hook show_dialog (universal text renderer) for MODE_TUTO coverage.
//         Catches Squall's internal thoughts (gray italic text, no border)
//         which bypass all MES/ASK/AMES opcode hooks. show_dialog is called
//         for ALL text types; we filter to MODE_TUTO (10) only here.
// v04.18: Fix post-FMV garbled text (continuous re-snapshot period, not single).
//         Hook opcode_tuto for diagnostics. Broaden show_dialog logging to ALL
//         modes. Add field name change detection. Lower text pointer threshold.
// v04.19: Enhanced show_dialog diagnostics — track window_id distribution
//         including out-of-range IDs. Log OOR calls in detail. Confirmed
//         corridor thoughts do NOT use MODE_TUTO (stays MODE_FIELD=1).
// v04.20: Hook menu_draw_text and get_character_width. Confirmed both are
//         menu-system-only — zero calls during field dialog or thoughts.
// v04.21: Hook opcode_mesmode (0x106) and opcode_ramesw (0x116). No calls
//         during thought gaps.
// v04.22: Hook update_field_entities + opcode dispatch instrumentation.
//         Confirmed script interpreter runs during gaps but zero dialog opcodes.
// v04.23: CRITICAL BUG FIXES from Plan Documents deep-research-report:
//         (a) Window struct stride 0x38 → 0x3C (FFNx ff8.h confirms 0x3C).
//         (b) field_get_dialog_string signature: (int) → (char*,int) per FFNx.
//         (c) show_dialog dedup: pointer-only → FNV-1a hash (catches rewrites).
//         (d) Added text_data2 (+0x0C) support and open_close_transition (+0x1C).
//         (e) TrimDecoded preserves ellipsis-only lines as "(...)".
//         These three bugs likely explain ALL missing corridor dialog.
// v04.24: Disable GCW speak (garbled "-G'" from naming screen glyphs).
// v04.25: SAPI restored as primary speech (NVDA caused missed dialogs while moving).
//         Keyboard shortcuts: F5=Repeat dialog, F6=Cycle voice, F7/F8=Rate.
//         Auto-bypass character naming screen (forces default names for AD consistency).
//         RepeatLastDialog() tracks last spoken text for F5 repeat feature.
// v04.35: Naming screen fully bypassed without opening UI.
//         ASM analysis of opcode_menuname revealed 0x01CE490B = UI-open flag and
//         0x47E480(charIdx) = GF junction init. We call GF inits directly and return
//         3 (script advance), never setting the flag. No input needed at all.
//
// ff8_win_obj offsets (from FFNx ff8.h, v04.23 corrected):
//   +0x08: char* text_data1
//   +0x0C: char* text_data2
//   +0x18: uint8_t win_id
//   +0x1A: uint16_t mode1
//   +0x1C: int16_t open_close_transition
//   +0x24: uint32_t state
//   +0x29: uint8_t first_question
//   +0x2A: uint8_t last_question
//   +0x2B: uint8_t current_choice_question
//   +0x30: uint16_t field_30 (dialog id in battle/tuto)
//   +0x34: uint32_t callback1
//   +0x38: uint32_t callback2
//   Total size: 0x3C bytes per window (NOT 0x38!)

#include "ff8_accessibility.h"
#include "ff8_text_decode.h"
#include "battle_tts.h"       // v0.10.112: GetLastDrawerName() for draw result announcements
#include "minhook/include/MinHook.h"
#include <vector>

namespace FieldDialog {

typedef int (__cdecl *OpcodeHandler_t)(int);
// v04.23: Fixed signature. FFNx voice.cpp shows: char* (char* msgBase, int dialogId)
typedef char* (__cdecl *FieldGetDialogString_t)(char* msgBase, int dialogId);
typedef char (__cdecl *ShowDialog_t)(int32_t window_id, uint32_t state, int16_t a3);

// ============================================================================
// Module state
// ============================================================================

static bool s_initialized = false;
static CRITICAL_SECTION s_cs;  // Protects s_winState[] (game thread hooks + poll thread)

static OpcodeHandler_t s_origMes = nullptr;
static OpcodeHandler_t s_origMesw = nullptr;  // v04.14: MESW (0x46) — message + wait
static OpcodeHandler_t s_origAsk = nullptr;
static OpcodeHandler_t s_origAmes = nullptr;
static OpcodeHandler_t s_origAask = nullptr;
static OpcodeHandler_t s_origAmesw = nullptr;
static FieldGetDialogString_t s_origGetDialogString = nullptr;  // v04.16
static ShowDialog_t s_origShowDialog = nullptr;  // v04.17
static OpcodeHandler_t s_origTuto = nullptr;  // v04.18: opcode_tuto (0x177)
static OpcodeHandler_t s_origMesmode = nullptr;  // v04.21: mesmode (0x106)
static OpcodeHandler_t s_origRamesw = nullptr;   // v04.21: ramesw (0x116)
static OpcodeHandler_t s_origMenuname = nullptr; // v04.25/v04.35: naming screen bypass

// v04.20: get_character_width — per-glyph function
typedef uint32_t (__cdecl *GetCharWidth_t)(uint32_t charCode);
static GetCharWidth_t s_origGetCharWidth = nullptr;

// v04.22: update_field_entities — naked counter hook
static void* s_origUpdateFieldEntities_raw = nullptr;
static volatile LONG s_ufeCallCount = 0;
static LONG s_ufeLastReported = 0;

// v04.22: opcode dispatch instrumentation
// We patch the CALL [eax*4 + table] at update_field_entities + 0x657
// to redirect through our code cave, which logs the opcode index (EAX).
static const int OPCODE_HIST_SIZE = 512;
static volatile LONG s_opcodeHistogram[OPCODE_HIST_SIZE] = {0};
static volatile LONG s_opcodeOverflow = 0;           // opcodes >= OPCODE_HIST_SIZE
static LONG s_opcodeHistogramPrev[OPCODE_HIST_SIZE] = {0};
static LONG s_opcodeOverflowPrev = 0;
static bool s_dispatchPatched = false;
static uint8_t s_dispatchOrigBytes[8] = {0};  // saved original bytes
static uint32_t s_dispatchAddr = 0;           // address of the CALL instruction
static uint32_t s_opcodeTableAddr = 0;        // original table address for our cave

// Code cave for dispatch instrumentation.
// The original code at update_field_entities + 0x657 is:
//   FF 14 85 [table_addr]  = call dword ptr [eax*4 + table_addr]  (7 bytes)
// We replace those 7 bytes with: E9 [rel32] 90 90 = JMP our_cave + 2 NOPs
//
// Our cave: logs EAX (opcode index), does the original CALL, then JMPs back.
// Key constraint: the handler is __cdecl(int entityPtr). entityPtr is already
// on the stack when we arrive. We MUST NOT push anything before calling the
// handler, or it'll see the wrong parameter.
//
// We use static variables instead of the stack to save/restore registers.
// This is safe because script execution is single-threaded.
static uint32_t s_dispatchRetAddr = 0;  // address after the 7-byte patch
static uint32_t s_savedEdx = 0;
static uint32_t s_handlerAddr = 0;

__declspec(naked) static void DispatchStub()
{
    __asm {
        // EAX = opcode index. Stack: [entityPtr, ...]
        // 1. Log to histogram
        pushfd
        cmp eax, 512
        jae stub_overflow
        lock inc dword ptr [s_opcodeHistogram + eax*4]
        jmp stub_docall
    stub_overflow:
        lock inc dword ptr [s_opcodeOverflow]
    stub_docall:
        popfd
        // 2. Compute handler address without touching the stack
        mov dword ptr [s_savedEdx], edx
        mov edx, dword ptr [s_opcodeTableAddr]
        mov edx, dword ptr [edx + eax*4]    // edx = handler function ptr
        mov dword ptr [s_handlerAddr], edx
        mov edx, dword ptr [s_savedEdx]     // restore EDX
        // 3. Call the handler. Stack is clean: [entityPtr, ...]
        //    CALL pushes our return addr, handler sees entityPtr at [esp+4]. Correct.
        call dword ptr [s_handlerAddr]
        // 4. Handler returned. JMP back to instruction after the patch site.
        jmp dword ptr [s_dispatchRetAddr]
    }
}

// Same as DispatchStub but opcode index is in EDX (FF 14 95 encoding)
__declspec(naked) static void DispatchStub_EDX()
{
    __asm {
        // EDX = opcode index. Stack: [entityPtr, ...]
        pushfd
        cmp edx, 512
        jae stub_edx_overflow
        lock inc dword ptr [s_opcodeHistogram + edx*4]
        jmp stub_edx_docall
    stub_edx_overflow:
        lock inc dword ptr [s_opcodeOverflow]
    stub_edx_docall:
        popfd
        // Compute handler address using EDX as index
        mov dword ptr [s_savedEdx], edx    // save EDX (it's both index AND might be clobbered)
        push eax                            // save EAX
        mov eax, dword ptr [s_opcodeTableAddr]
        mov eax, dword ptr [eax + edx*4]   // eax = handler function ptr
        mov dword ptr [s_handlerAddr], eax
        pop eax                             // restore EAX
        mov edx, dword ptr [s_savedEdx]     // restore EDX
        call dword ptr [s_handlerAddr]
        jmp dword ptr [s_dispatchRetAddr]
    }
}

// v04.22: Patch the dispatch CALL site to redirect through DispatchStub.
static bool PatchDispatchSite()
{
    if (FF8Addresses::update_field_entities_addr == 0 || !FF8Addresses::pExecuteOpcodeTable)
        return false;

    s_dispatchAddr = FF8Addresses::update_field_entities_addr + 0x657;
    s_dispatchRetAddr = s_dispatchAddr + 7;  // instruction after the 7-byte CALL

    // Verify the instruction is FF 14 85 [4-byte addr]
    uint8_t* code = (uint8_t*)s_dispatchAddr;
    if (code[0] != 0xFF || code[1] != 0x14 || (code[2] != 0x85 && code[2] != 0x95)) {
        Log::Write("FieldDialog: [DISPATCH] Expected FF 14 85/95 at 0x%08X, got %02X %02X %02X",
                   s_dispatchAddr, code[0], code[1], code[2]);
        return false;
    }
    bool indexInEdx = (code[2] == 0x95);  // 0x85=EAX, 0x95=EDX
    Log::Write("FieldDialog: [DISPATCH] Opcode index register: %s", indexInEdx ? "EDX" : "EAX");

    // Read the table address from the instruction operand
    s_opcodeTableAddr = *(uint32_t*)(code + 3);
    Log::Write("FieldDialog: [DISPATCH] Found dispatch at 0x%08X, table=0x%08X",
               s_dispatchAddr, s_opcodeTableAddr);

    // Verify table matches what we resolved
    if (s_opcodeTableAddr != (uint32_t)FF8Addresses::pExecuteOpcodeTable) {
        Log::Write("FieldDialog: [DISPATCH] WARNING: table mismatch! Expected 0x%08X",
                   (uint32_t)FF8Addresses::pExecuteOpcodeTable);
        // Use the one from the instruction, not our resolved one
    }

    // Save original bytes
    memcpy(s_dispatchOrigBytes, code, 7);

    // Make writable
    DWORD oldProtect;
    if (!VirtualProtect((LPVOID)s_dispatchAddr, 7, PAGE_EXECUTE_READWRITE, &oldProtect)) {
        Log::Write("FieldDialog: [DISPATCH] VirtualProtect failed (err=%u)", GetLastError());
        return false;
    }

    // Write: E9 [rel32] 90 90  (JMP DispatchStub/DispatchStub_EDX + 2 NOPs)
    void* stubTarget = indexInEdx ? (void*)&DispatchStub_EDX : (void*)&DispatchStub;
    int32_t rel = (int32_t)((uint32_t)stubTarget - (s_dispatchAddr + 5));
    code[0] = 0xE9;  // JMP rel32
    *(int32_t*)(code + 1) = rel;
    code[5] = 0x90;  // NOP
    code[6] = 0x90;  // NOP

    // Restore protection
    VirtualProtect((LPVOID)s_dispatchAddr, 7, oldProtect, &oldProtect);

    s_dispatchPatched = true;
    Log::Write("FieldDialog: [DISPATCH] Patched! JMP to 0x%08X (%s stub), ret to 0x%08X",
               (uint32_t)stubTarget, indexInEdx ? "EDX" : "EAX", s_dispatchRetAddr);

    // Diagnostic: dump x86 bytes before the dispatch to understand how
    // the engine extracts the opcode index from JSM instruction data.
    // This covers the bytecode interpreter loop in update_field_entities.
    {
        uint32_t dumpStart = s_dispatchAddr - 128;
        // Restore original bytes temporarily for clean dump
        DWORD dp;
        VirtualProtect((LPVOID)s_dispatchAddr, 7, PAGE_EXECUTE_READWRITE, &dp);
        memcpy((void*)s_dispatchAddr, s_dispatchOrigBytes, 7);

        const uint8_t* p = (const uint8_t*)dumpStart;
        for (int row = 0; row < 9; row++) {
            char hexBuf[256];
            int hp = 0;
            uint32_t addr = dumpStart + row * 16;
            hp += snprintf(hexBuf + hp, 256 - hp, "%08X: ", addr);
            for (int b = 0; b < 16; b++)
                hp += snprintf(hexBuf + hp, 256 - hp, "%02X ", p[row * 16 + b]);
            Log::Write("FieldDialog: [X86DUMP] %s", hexBuf);
        }

        // Also dump the instruction decoder function at 0x00530760
        // (called from the dispatch site to extract opcode from raw dword)
        uint32_t decoderAddr = 0x00530760;
        const uint8_t* dp2 = (const uint8_t*)decoderAddr;
        Log::Write("FieldDialog: [X86DUMP] === Instruction decoder at 0x%08X ===", decoderAddr);
        for (int row = 0; row < 16; row++) {
            char hexBuf2[256];
            int hp2 = 0;
            uint32_t a2 = decoderAddr + row * 16;
            hp2 += snprintf(hexBuf2 + hp2, 256 - hp2, "%08X: ", a2);
            for (int b = 0; b < 16; b++)
                hp2 += snprintf(hexBuf2 + hp2, 256 - hp2, "%02X ", dp2[row * 16 + b]);
            Log::Write("FieldDialog: [X86DUMP] %s", hexBuf2);
        }

        // Re-apply patch
        code = (uint8_t*)s_dispatchAddr;
        code[0] = 0xE9;
        *(int32_t*)(code + 1) = rel;
        code[5] = 0x90;
        code[6] = 0x90;
        VirtualProtect((LPVOID)s_dispatchAddr, 7, dp, &dp);
    }

    return true;
}

static void UnpatchDispatchSite()
{
    if (!s_dispatchPatched || s_dispatchAddr == 0) return;

    DWORD oldProtect;
    if (VirtualProtect((LPVOID)s_dispatchAddr, 7, PAGE_EXECUTE_READWRITE, &oldProtect)) {
        memcpy((void*)s_dispatchAddr, s_dispatchOrigBytes, 7);
        VirtualProtect((LPVOID)s_dispatchAddr, 7, oldProtect, &oldProtect);
    }
    s_dispatchPatched = false;
    Log::Write("FieldDialog: [DISPATCH] Unpatched.");
}

// v04.20: menu_draw_text — naked hook for zero-overhead call counting.
// We don't know the signature, so we just increment a counter and jump
// to the original via the trampoline. This preserves all registers and
// stack state perfectly.
static void* s_origMenuDrawText_raw = nullptr;
static volatile LONG s_menuDrawTextCallCount = 0;
static LONG s_menuDrawTextLastReported = 0;  // for delta in diagnostic

// v04.20: get_character_width counters (declared early for use in show_dialog diag)
static volatile LONG s_gcwCallCount = 0;
static LONG s_gcwLastReported = 0;

static const size_t WIN_OBJ_SIZE = 0x3C;  // v04.23: was 0x38 (WRONG), FFNx ff8.h confirms 0x3C
static const size_t WIN_OBJ_TEXT1_OFFSET = 0x08;
static const size_t WIN_OBJ_TEXT2_OFFSET = 0x0C;  // v04.23: text_data2 (secondary text buffer)
static const size_t WIN_OBJ_WINID_OFFSET = 0x18;
static const size_t WIN_OBJ_MODE1_OFFSET = 0x1A;  // v04.23: uint16_t mode1
static const size_t WIN_OBJ_OPEN_CLOSE_OFFSET = 0x1C;  // v04.23: int16_t open_close_transition
static const size_t WIN_OBJ_STATE_OFFSET = 0x24;
static const size_t WIN_OBJ_FIRST_Q_OFFSET = 0x29;
static const size_t WIN_OBJ_LAST_Q_OFFSET = 0x2A;
static const size_t WIN_OBJ_CUR_CHOICE_OFFSET = 0x2B;
static const size_t WIN_OBJ_FIELD30_OFFSET = 0x30;  // v04.23: uint16_t (dialog id in battle/tuto)
static const int MAX_WINDOWS = 8;

// ============================================================================
// Per-window deduplication state
//
// Each of the 8 windows tracks its own last-spoken text independently.
// This allows simultaneous dialogs in different windows to both be spoken.
//
// lastRawText stores the raw decoded text for any window. When the choice
// handler speaks a formatted version (with "Selected:" labels), it also
// stores the raw version here. The all-windows scanner checks against both
// lastSpokenText and lastRawText, so it naturally skips choice windows
// without needing a separate lock flag.
// ============================================================================

struct WindowState {
    std::string lastSpokenText;      // Full decoded text last sent to TTS
    std::string lastRawText;         // Raw decoded text (for all-windows dedup)
    std::string lastChoicePrompt;    // For choice dialog dedup
    uint8_t lastSpokenChoice;        // Last choice index spoken
    bool skipLogged;                 // Have we logged the skip for current text?

    WindowState() : lastSpokenChoice(0xFF), skipLogged(false) {}

    void Reset() {
        lastSpokenText.clear();
        lastRawText.clear();
        lastChoicePrompt.clear();
        lastSpokenChoice = 0xFF;
        skipLogged = false;
    }
};

static WindowState s_winState[MAX_WINDOWS];

// ============================================================================
// v04.16: Pending text from field_get_dialog_string hook
//
// Text fetched via field_get_dialog_string may or may not end up in a window.
// Normal dialog: opcode hook speaks it -> appears in s_winState -> dedup works.
// Thoughts/etc: no opcode hook fires -> text never spoken -> we catch it here.
//
// The getstr hook stores decoded text with a timestamp. PollWindows checks:
// if pending text hasn't been spoken by any other mechanism within 500ms,
// speak it as a "thought" or off-screen dialog.
// ============================================================================

static const int MIN_TEXT_LENGTH = 3;  // Skip junk like "'," or "C0"

// v04.25: Track last spoken dialog for repeat (F5)
static std::string s_lastDialogSpoken;

struct PendingText {
    std::string decoded;   // Decoded text
    DWORD fetchTime;       // GetTickCount() when fetched
    bool spoken;           // Set true when spoken by opcode hook or poll
    int messageId;         // FF8 message ID for logging
};

static const int MAX_PENDING = 8;
static PendingText s_pending[MAX_PENDING];
static int s_pendingCount = 0;
static std::string s_lastGetstrText;  // Last text returned by field_get_dialog_string

// Forward declaration (defined after ScanAndSpeakChoiceWindows)
static void MarkPendingAsSpoken(const std::string& spokenText);

// ============================================================================
// Pointer validation
// ============================================================================

static bool IsValidTextPointer(const char* ptr)
{
    uintptr_t addr = (uintptr_t)ptr;
    // v04.18: lowered from 0x00A00000 to 0x00010000 to catch thought text
    // that may use lower-address buffers. ProbePointer() still provides safety.
    return (addr >= 0x00010000 && addr <= 0x30000000);
}

static bool ProbePointer(const char* ptr)
{
    if (!ptr) return false;
    __try {
        volatile uint8_t probe = *(const uint8_t*)ptr;
        (void)probe;
        return true;
    }
    __except(EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

// ============================================================================
// Window helpers
// ============================================================================

static uint8_t* GetWindowObj(int index)
{
    if (!FF8Addresses::pWindowsArray) return nullptr;
    return FF8Addresses::pWindowsArray + (index * WIN_OBJ_SIZE);
}

static char* GetWinText1(uint8_t* winObj)
{
    if (!winObj) return nullptr;
    return *(char**)(winObj + WIN_OBJ_TEXT1_OFFSET);
}

static char* GetWinText2(uint8_t* winObj)
{
    if (!winObj) return nullptr;
    return *(char**)(winObj + WIN_OBJ_TEXT2_OFFSET);
}

static int16_t GetWinOpenCloseTransition(uint8_t* winObj)
{
    if (!winObj) return 0;
    return *(int16_t*)(winObj + WIN_OBJ_OPEN_CLOSE_OFFSET);
}

// ============================================================================
// Suffix detection for multi-page dialog dedup
// ============================================================================

static bool IsSuffixOrSubstring(const std::string& fullText, const std::string& newText)
{
    if (fullText.empty() || newText.empty()) return false;
    if (newText.length() >= fullText.length()) return false;

    // Check if fullText ends with newText
    size_t pos = fullText.length() - newText.length();
    if (fullText.compare(pos, newText.length(), newText) == 0)
        return true;

    // Try stripping leading punctuation/whitespace
    size_t trimStart = newText.find_first_not_of(" .\"'");
    if (trimStart != std::string::npos && trimStart > 0) {
        std::string trimmed = newText.substr(trimStart);
        if (!trimmed.empty() && trimmed.length() < fullText.length()) {
            pos = fullText.length() - trimmed.length();
            if (fullText.compare(pos, trimmed.length(), trimmed) == 0)
                return true;
        }
    }

    // Check if fullText contains newText anywhere
    if (fullText.find(newText) != std::string::npos)
        return true;

    return false;
}

// ============================================================================
// Helper: trim whitespace and leading/trailing periods
// ============================================================================

static std::string TrimDecoded(const std::string& text)
{
    // v04.23: preserve ellipsis-only lines instead of discarding them
    size_t start = text.find_first_not_of(" .");
    size_t end = text.find_last_not_of(" .");
    if (start == std::string::npos) {
        // All dots/spaces — check if there are meaningful dots
        size_t dotCount = 0;
        for (char c : text) if (c == '.') dotCount++;
        if (dotCount >= 3) return "(...)";
        return "";
    }
    return text.substr(start, end - start + 1);
}

// ============================================================================
// Core: scan ALL windows and speak any new text
//
// Called after every opcode handler returns (and at end of choice handler).
// Checks all 8 windows for text that hasn't been spoken yet. Deduplicates
// against both lastSpokenText and lastRawText so choice windows (whose
// formatted text differs from raw decoded text) are naturally skipped.
// ============================================================================

static void ScanAndSpeakAllWindows(const char* opcodeLabel)
{
    if (!FF8Addresses::pWindowsArray) return;

    for (int i = 0; i < MAX_WINDOWS; i++) {
        WindowState& ws = s_winState[i];

        uint8_t* winObj = GetWindowObj(i);
        char* text1 = GetWinText1(winObj);

        // Skip windows with no valid text
        if (!text1 || !IsValidTextPointer(text1) || !ProbePointer(text1))
            continue;
        if (*(const uint8_t*)text1 == 0x00) {
            // Window was cleared — reset choice state if it was active
            ws.Reset();
            continue;
        }

        std::string decoded = TrimDecoded(FF8TextDecode::Decode((const uint8_t*)text1, 512));
        if (decoded.empty()) continue;

        // v04.16: Skip very short text fragments (stale data, control codes)
        if ((int)decoded.length() < MIN_TEXT_LENGTH) continue;

        // Exact duplicate for this window — skip
        if (decoded == ws.lastSpokenText || decoded == ws.lastRawText)
            continue;

        // Page advance: new text is a portion of what this window already spoke
        if (IsSuffixOrSubstring(ws.lastSpokenText, decoded) ||
            IsSuffixOrSubstring(ws.lastRawText, decoded)) {
            if (!ws.skipLogged) {
                ws.skipLogged = true;
                Log::Write("FieldDialog: [%s] win[%d] Skipping page advance (already spoken)",
                           opcodeLabel, i);
            }
            continue;
        }

        // Also check if this text was just spoken in ANY other window
        bool spokenElsewhere = false;
        for (int j = 0; j < MAX_WINDOWS; j++) {
            if (j == i) continue;
            if (decoded == s_winState[j].lastSpokenText ||
                decoded == s_winState[j].lastRawText) {
                spokenElsewhere = true;
                break;
            }
        }
        if (spokenElsewhere) continue;

        // New text for this window — speak it
        ws.lastSpokenText = decoded;
        ws.lastRawText = decoded;
        ws.skipLogged = false;

        // v04.16: Mark this text as spoken in pending queue
        MarkPendingAsSpoken(decoded);

        Log::Write("FieldDialog: [%s] win[%d] Speaking: \"%s\"",
                   opcodeLabel, i, decoded.c_str());
        s_lastDialogSpoken = decoded;  // v04.25: track for F5 repeat
        ScreenReader::Speak(decoded.c_str(), false);  // Queue mode
    }
}

// ============================================================================
// Core: scan ALL windows for choice dialogs
//
// Choice opcodes (ASK/AASK) need special handling for choice navigation.
// We scan all windows for any that have valid firstQ/lastQ fields.
// The choice handler stores lastRawText so that the all-windows scanner
// (called at the end) naturally deduplicates against choice windows.
// ============================================================================

static void ScanAndSpeakChoiceWindows(const char* opcodeLabel)
{
    if (!FF8Addresses::pWindowsArray) return;

    for (int i = 0; i < MAX_WINDOWS; i++) {
        uint8_t* winObj = GetWindowObj(i);
        char* text1 = GetWinText1(winObj);

        if (!text1 || !IsValidTextPointer(text1) || !ProbePointer(text1))
            continue;
        if (*(const uint8_t*)text1 == 0x00)
            continue;

        uint8_t firstQ = *(uint8_t*)(winObj + WIN_OBJ_FIRST_Q_OFFSET);
        uint8_t lastQ = *(uint8_t*)(winObj + WIN_OBJ_LAST_Q_OFFSET);
        uint8_t curChoice = *(uint8_t*)(winObj + WIN_OBJ_CUR_CHOICE_OFFSET);

        // Only process this window as a choice dialog if it has valid choice fields
        if (firstQ == 0 && lastQ == 0) continue;
        if (lastQ < firstQ) continue;

        FF8TextDecode::ChoiceDialog dialog =
            FF8TextDecode::DecodeChoices((const uint8_t*)text1, 512, firstQ, lastQ);

        if (dialog.prompt.empty() && dialog.choices.empty()) continue;

        WindowState& ws = s_winState[i];

        int choiceIndex = (int)curChoice - (int)firstQ;
        int numChoices = (int)(lastQ - firstQ + 1);

        // Same dialog, same choice = skip (do nothing)
        if (dialog.prompt == ws.lastChoicePrompt && curChoice == ws.lastSpokenChoice)
            continue;

        // Same dialog, different choice = interrupt with new choice text
        if (dialog.prompt == ws.lastChoicePrompt && curChoice != ws.lastSpokenChoice) {
            ws.lastSpokenChoice = curChoice;

            if (choiceIndex >= 0 && choiceIndex < (int)dialog.choices.size()) {
                Log::Write("FieldDialog: [%s] win[%d] Choice changed -> %d: \"%s\"",
                           opcodeLabel, i, choiceIndex + 1,
                           dialog.choices[choiceIndex].c_str());
                ScreenReader::Speak(dialog.choices[choiceIndex].c_str(), true);
            } else {
                char buf[64];
                snprintf(buf, sizeof(buf), "Choice %d of %d", choiceIndex + 1, numChoices);
                ScreenReader::Speak(buf, true);
            }
            continue;
        }

        // New choice dialog
        ws.lastChoicePrompt = dialog.prompt;
        ws.lastSpokenChoice = curChoice;
        ws.skipLogged = false;

        Log::Write("FieldDialog: [%s] win[%d] Parsed %d choices (firstQ=%u lastQ=%u curChoice=%u)",
                   opcodeLabel, i, (int)dialog.choices.size(), firstQ, lastQ, curChoice);

        std::string fullText = dialog.prompt;
        if (!dialog.choices.empty()) {
            for (int c = 0; c < (int)dialog.choices.size(); c++) {
                fullText += ". ";
                if (c == choiceIndex)
                    fullText += "Selected: ";
                fullText += dialog.choices[c];
            }
        }

        ws.lastSpokenText = fullText;
        // Also store raw text so the all-windows scanner won't re-speak it
        ws.lastRawText = TrimDecoded(FF8TextDecode::Decode((const uint8_t*)text1, 512));

        // v04.16: Mark both versions as spoken in pending queue
        MarkPendingAsSpoken(fullText);
        MarkPendingAsSpoken(ws.lastRawText);

        Log::Write("FieldDialog: [%s] win[%d] Speaking: \"%s\"",
                   opcodeLabel, i, fullText.c_str());
        s_lastDialogSpoken = fullText;  // v04.25: track for F5 repeat
        ScreenReader::Speak(fullText.c_str(), false);
    }

    // Also scan all windows for any new regular dialog
    // (another window might have new text alongside the choice window).
    // The all-windows scanner will naturally skip choice windows because
    // lastRawText matches the current decoded text.
    ScanAndSpeakAllWindows(opcodeLabel);
}

// ============================================================================
// v04.16: Mark pending text as spoken (called when opcode hooks speak text)
// ============================================================================

static void MarkPendingAsSpoken(const std::string& spokenText)
{
    for (int i = 0; i < s_pendingCount; i++) {
        if (!s_pending[i].spoken && s_pending[i].decoded == spokenText) {
            s_pending[i].spoken = true;
        }
    }
}

// ============================================================================
// v04.16: field_get_dialog_string hook
//
// This is the low-level function that fetches message text from the field's
// message table. Called by ALL dialog opcodes, but ALSO by non-standard
// code paths (Squall's thoughts, etc.) that bypass the window system.
//
// We log every call and store the text as "pending". If it gets spoken by
// an opcode hook within 500ms, we mark it spoken. Otherwise, PollWindows
// speaks it as a thought/off-screen dialog.
// ============================================================================

// SEH probe must be in its own function — MSVC can't mix __try with C++ objects
static bool ProbeGetstrResult(const char* ptr)
{
    if (!ptr) return false;
    __try {
        volatile uint8_t probe = *(const uint8_t*)ptr;
        (void)probe;
        return (probe != 0x00);  // Also reject empty strings
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

// v04.16 diagnostic: count calls to see if the hook fires at all
static int s_getstrCallCount = 0;
static DWORD s_getstrLastDiagTime = 0;

// v04.23: Fixed signature to match FFNx: (char* msgBase, int dialogId)
static char* __cdecl Hook_field_get_dialog_string(char* msgBase, int dialogId)
{
    char* result = s_origGetDialogString(msgBase, dialogId);

    // Diagnostic: log first few calls unconditionally, then periodic summary
    s_getstrCallCount++;
    if (s_getstrCallCount <= 10) {
        Log::Write("FieldDialog: [GETSTR-RAW] call#%d base=0x%08X dialogId=%d result=0x%08X",
                   s_getstrCallCount, (uint32_t)(uintptr_t)msgBase, dialogId,
                   (uint32_t)(uintptr_t)result);
    } else {
        DWORD now = GetTickCount();
        if ((now - s_getstrLastDiagTime) >= 5000) {
            s_getstrLastDiagTime = now;
            Log::Write("FieldDialog: [GETSTR-DIAG] %d total calls so far", s_getstrCallCount);
        }
    }

    if (!ProbeGetstrResult(result)) return result;

    std::string decoded = TrimDecoded(FF8TextDecode::Decode((const uint8_t*)result, 512));
    if (decoded.empty() || (int)decoded.length() < MIN_TEXT_LENGTH) return result;

    // Skip if identical to last fetch (opcodes call this multiple times)
    if (decoded == s_lastGetstrText) return result;
    s_lastGetstrText = decoded;

    Log::Write("FieldDialog: [GETSTR] dialogId=%d text=\"%s\"", dialogId, decoded.c_str());

    // Store as pending
    EnterCriticalSection(&s_cs);

    // Check if already in pending list
    bool alreadyPending = false;
    for (int i = 0; i < s_pendingCount; i++) {
        if (s_pending[i].decoded == decoded) {
            alreadyPending = true;
            break;
        }
    }

    if (!alreadyPending) {
        // Shift out oldest if full
        if (s_pendingCount >= MAX_PENDING) {
            for (int i = 1; i < MAX_PENDING; i++)
                s_pending[i - 1] = s_pending[i];
            s_pendingCount = MAX_PENDING - 1;
        }
        s_pending[s_pendingCount].decoded = decoded;
        s_pending[s_pendingCount].fetchTime = GetTickCount();
        s_pending[s_pendingCount].spoken = false;
        s_pending[s_pendingCount].messageId = dialogId;
        s_pendingCount++;
    }

    LeaveCriticalSection(&s_cs);
    return result;
}

// ============================================================================
// v04.16: Check pending texts and speak any that haven't been spoken
// by opcode hooks within the timeout. Called from PollWindows.
// ============================================================================

static const DWORD PENDING_SPEAK_DELAY_MS = 500;

static void CheckPendingTexts()
{
    DWORD now = GetTickCount();

    for (int i = 0; i < s_pendingCount; i++) {
        if (s_pending[i].spoken) continue;
        if ((now - s_pending[i].fetchTime) < PENDING_SPEAK_DELAY_MS) continue;

        // This text was fetched 500ms+ ago and never spoken by opcode hooks.
        // Check if it matches any window's lastSpokenText (opcode hook might
        // have spoken it without exact string match due to formatting).
        bool alreadySpoken = false;
        for (int w = 0; w < MAX_WINDOWS; w++) {
            if (s_pending[i].decoded == s_winState[w].lastSpokenText ||
                s_pending[i].decoded == s_winState[w].lastRawText ||
                IsSuffixOrSubstring(s_winState[w].lastSpokenText, s_pending[i].decoded) ||
                IsSuffixOrSubstring(s_winState[w].lastRawText, s_pending[i].decoded)) {
                alreadySpoken = true;
                break;
            }
        }

        s_pending[i].spoken = true;  // Mark as handled either way

        if (!alreadySpoken) {
            Log::Write("FieldDialog: [GETSTR-DEFERRED] msgId=%d Speaking: \"%s\"",
                       s_pending[i].messageId, s_pending[i].decoded.c_str());
            ScreenReader::Speak(s_pending[i].decoded.c_str(), false);
        }
    }

    // Compact: remove old spoken entries
    int writeIdx = 0;
    for (int i = 0; i < s_pendingCount; i++) {
        if (!s_pending[i].spoken || (now - s_pending[i].fetchTime) < 2000) {
            if (writeIdx != i) s_pending[writeIdx] = s_pending[i];
            writeIdx++;
        }
    }
    s_pendingCount = writeIdx;
}

// ============================================================================
// v04.17: show_dialog hook — catches MODE_TUTO (Squall's thoughts)
//
// show_dialog(window_id, state, a3) is the universal text renderer.
// Called for ALL dialog types. We only handle MODE_TUTO here; regular
// field dialog is handled by the opcode hooks above (lower latency).
//
// FFNx interaction: FFNx patches one CALL site (sub_4A0C00+0x5F) via
// replace_call, storing the original address. Our MinHook patches the
// function prologue. Call chain: FFNx wrapper -> our hook -> original.
// Both hook chains execute safely.
// ============================================================================

static std::string s_lastTutoText;  // Dedup for tutorial/thought text
static DWORD s_lastTutoSpeakTime = 0;

// v04.17 diagnostic: track show_dialog call frequency and game modes seen
static int s_showDialogCallCount = 0;
static DWORD s_showDialogLastDiagTime = 0;
static uint32_t s_showDialogLastMode = 0xFFFFFFFF;

// v04.19: track ALL window IDs seen by show_dialog, including out-of-range
static int s_sdWinIdCounts[32] = {0};  // bucket window_ids 0-30, 31=overflow
static int s_sdNegativeWinCount = 0;

// v04.23: FNV-1a hash for detecting in-place content changes
static uint32_t fnv1a_prefix(const uint8_t* p, size_t n)
{
    uint32_t h = 2166136261u;
    for (size_t i = 0; i < n; i++) h = (h ^ p[i]) * 16777619u;
    return h;
}

// v04.23: track text pointer + hash per window in show_dialog
// Replaces pointer-only check that missed in-place buffer rewrites
static char* s_sdLastTextPtr[MAX_WINDOWS] = {0};
static uint32_t s_sdLastHash[MAX_WINDOWS] = {0};
static std::string s_sdLastDecoded[MAX_WINDOWS];

static char __cdecl Hook_show_dialog(int32_t window_id, uint32_t state, int16_t a3)
{
    // Call original first — let the game render normally
    char result = s_origShowDialog(window_id, state, a3);

    // v04.19: Track ALL window_ids seen (including out-of-range)
    s_showDialogCallCount++;
    if (window_id < 0) {
        s_sdNegativeWinCount++;
    } else if (window_id < 32) {
        s_sdWinIdCounts[window_id]++;
    } else {
        s_sdWinIdCounts[31]++;  // overflow bucket
    }

    uint32_t currentMode = FF8Addresses::pGameMode ? *FF8Addresses::pGameMode : 0xDEAD;

    // v04.19: Enhanced periodic diagnostic — show window_id distribution
    {
        DWORD now = GetTickCount();
        if ((now - s_showDialogLastDiagTime) >= 5000 || currentMode != s_showDialogLastMode) {
            s_showDialogLastDiagTime = now;
            s_showDialogLastMode = currentMode;

            // Build window_id distribution string
            char dist[256];
            int dpos = 0;
            for (int w = 0; w < 32; w++) {
                if (s_sdWinIdCounts[w] > 0) {
                    dpos += snprintf(dist + dpos, sizeof(dist) - dpos,
                                     " w%d=%d", w, s_sdWinIdCounts[w]);
                }
            }
            if (s_sdNegativeWinCount > 0) {
                dpos += snprintf(dist + dpos, sizeof(dist) - dpos,
                                 " neg=%d", s_sdNegativeWinCount);
            }
            // v04.20: include menu_draw_text call rate
            LONG mdtCount = s_menuDrawTextCallCount;
            LONG mdtDelta = mdtCount - s_menuDrawTextLastReported;
            s_menuDrawTextLastReported = mdtCount;

            LONG gcwCount = s_gcwCallCount;
            LONG gcwDelta = gcwCount - s_gcwLastReported;
            s_gcwLastReported = gcwCount;

            LONG ufeCount = s_ufeCallCount;
            LONG ufeDelta = ufeCount - s_ufeLastReported;
            s_ufeLastReported = ufeCount;

            Log::Write("FieldDialog: [SHOW_DIALOG-DIAG] %d total calls, mode=%u, ufe=%ld(+%ld), mdt=%ld(+%ld), gcw=%ld(+%ld), dist:%s",
                       s_showDialogCallCount, currentMode, ufeCount, ufeDelta, mdtCount, mdtDelta, gcwCount, gcwDelta, dist);

            // v04.22: dump opcode histogram delta (only non-zero entries)
            if (s_dispatchPatched) {
                char opbuf[2048];
                int opos = 0;
                for (int op = 0; op < OPCODE_HIST_SIZE; op++) {
                    LONG cur = s_opcodeHistogram[op];
                    LONG delta = cur - s_opcodeHistogramPrev[op];
                    s_opcodeHistogramPrev[op] = cur;
                    if (delta > 0 && opos < 2000) {
                        opos += snprintf(opbuf + opos, sizeof(opbuf) - opos,
                                         " %03X=%ld", op, delta);
                    }
                }
                LONG ovfDelta = s_opcodeOverflow - s_opcodeOverflowPrev;
                s_opcodeOverflowPrev = s_opcodeOverflow;
                if (ovfDelta > 0)
                    opos += snprintf(opbuf + opos, sizeof(opbuf) - opos, " OVF=%ld", ovfDelta);
                if (opos > 0)
                    Log::Write("FieldDialog: [OPCODE-HIST]%s", opbuf);
            }

            // Reset counters for next interval
            memset(s_sdWinIdCounts, 0, sizeof(s_sdWinIdCounts));
            s_sdNegativeWinCount = 0;
        }
    }

    // v04.19: Log ANY call with window_id outside 0-7 range (these are
    // currently missed by our text detection and might be thoughts)
    if (window_id < 0 || window_id >= MAX_WINDOWS) {
        // Log the first 20 out-of-range calls in detail, then periodic
        static int s_oorCount = 0;
        s_oorCount++;
        if (s_oorCount <= 20) {
            Log::Write("FieldDialog: [SHOW_DIALOG-OOR] winId=%d state=%u a3=%d mode=%u",
                       window_id, state, (int)a3, currentMode);
        }
        return result;
    }
    if (!FF8Addresses::pWindowsArray) return result;

    uint8_t* winObj = GetWindowObj(window_id);
    if (!winObj) return result;

    char* text1 = GetWinText1(winObj);
    char* text2 = GetWinText2(winObj);  // v04.23: also check text_data2

    // v04.23: Use the first valid text pointer (prefer text1)
    char* textPtr = nullptr;
    if (text1 && IsValidTextPointer(text1) && ProbePointer(text1) && *(const uint8_t*)text1 != 0x00)
        textPtr = text1;
    else if (text2 && IsValidTextPointer(text2) && ProbePointer(text2) && *(const uint8_t*)text2 != 0x00)
        textPtr = text2;
    if (!textPtr) return result;

    // v04.23: Hash-based change detection instead of pointer-only.
    // Catches in-place buffer rewrites that the old pointer check missed.
    uint32_t hash = fnv1a_prefix((const uint8_t*)textPtr, 64);
    if (textPtr == s_sdLastTextPtr[window_id] && hash == s_sdLastHash[window_id])
        return result;  // truly unchanged
    s_sdLastTextPtr[window_id] = textPtr;
    s_sdLastHash[window_id] = hash;

    std::string decoded = TrimDecoded(FF8TextDecode::Decode((const uint8_t*)textPtr, 512));
    if (decoded.empty() || (int)decoded.length() < MIN_TEXT_LENGTH) return result;

    // Dedup against last decoded for this window in show_dialog
    if (decoded == s_sdLastDecoded[window_id]) return result;
    s_sdLastDecoded[window_id] = decoded;

    // v04.23: Log text changes with transition info for debugging
    uint8_t tutoId = FF8Addresses::pCurrentTutorialId ?
                     *FF8Addresses::pCurrentTutorialId : 0xFF;
    int16_t transition = GetWinOpenCloseTransition(winObj);
    bool usedText2 = (textPtr == text2 && textPtr != text1);
    Log::Write("FieldDialog: [SHOW_DIALOG-TEXT] win[%d] mode=%u tutoId=%u state=%u tr=%d%s text=\"%s\"",
               window_id, currentMode, (unsigned)tutoId, state, (int)transition,
               usedText2 ? " [T2]" : "", decoded.c_str());

    // Check if opcode hooks already spoke this
    EnterCriticalSection(&s_cs);
    WindowState& ws = s_winState[window_id];
    bool alreadySpoken = (decoded == ws.lastSpokenText || decoded == ws.lastRawText);

    if (!alreadySpoken) {
        for (int w = 0; w < MAX_WINDOWS && !alreadySpoken; w++) {
            if (w == window_id) continue;
            if (decoded == s_winState[w].lastSpokenText ||
                decoded == s_winState[w].lastRawText)
                alreadySpoken = true;
        }
    }

    // v04.18: Also check suffix/substring (opcode hooks might have spoken
    // a longer version that includes this text)
    if (!alreadySpoken) {
        for (int w = 0; w < MAX_WINDOWS; w++) {
            if (IsSuffixOrSubstring(s_winState[w].lastSpokenText, decoded) ||
                IsSuffixOrSubstring(s_winState[w].lastRawText, decoded)) {
                alreadySpoken = true;
                break;
            }
        }
    }

    if (!alreadySpoken) {
        s_lastTutoText = decoded;
        s_lastTutoSpeakTime = GetTickCount();
        ws.lastSpokenText = decoded;
        ws.lastRawText = decoded;
        ws.skipLogged = false;

        MarkPendingAsSpoken(decoded);

        // v0.10.112: In battle mode, prepend character name to "Received" draw results.
        // "Received 4 Blizzards!" -> "Squall received 4 Blizzards!"
        std::string speakText = decoded;
        if (currentMode == 3 && decoded.length() > 8 && decoded.compare(0, 8, "Received") == 0) {
            const char* drawer = BattleTTS::GetLastDrawerName();
            if (drawer) {
                speakText = std::string(drawer) + " r" + decoded.substr(1);
            }
        }

        Log::Write("FieldDialog: [SHOW_DIALOG-SPEAK] win[%d] mode=%u Speaking: \"%s\"",
                   window_id, currentMode, speakText.c_str());
        s_lastDialogSpoken = speakText;  // v04.25: track for F5 repeat
        ScreenReader::Speak(speakText.c_str(), false);  // Queue mode
    } else {
        Log::Write("FieldDialog: [SHOW_DIALOG-TEXT] win[%d] (already spoken by opcode hook)",
                   window_id);
    }
    LeaveCriticalSection(&s_cs);

    return result;
}

// ============================================================================
// v04.18: opcode_tuto hook — catches when the game triggers tutorial/thought
// This opcode sets game mode to MODE_TUTO. By hooking it we can see exactly
// when thoughts/tutorials are triggered and what tutorial ID is used.
// ============================================================================

static int s_tutoCallCount = 0;

static int __cdecl Hook_opcode_tuto(int entityPtr)
{
    uint8_t tutoIdBefore = FF8Addresses::pCurrentTutorialId ?
                           *FF8Addresses::pCurrentTutorialId : 0xFF;
    uint16_t modeBefore = FF8Addresses::pGameMode ?
                          *FF8Addresses::pGameMode : 0xFFFF;

    int result = s_origTuto(entityPtr);

    uint8_t tutoIdAfter = FF8Addresses::pCurrentTutorialId ?
                          *FF8Addresses::pCurrentTutorialId : 0xFF;
    uint16_t modeAfter = FF8Addresses::pGameMode ?
                         *FF8Addresses::pGameMode : 0xFFFF;

    s_tutoCallCount++;
    const char* fieldName = FF8Addresses::pCurrentFieldName ?
                            FF8Addresses::pCurrentFieldName : "(null)";
    Log::Write("FieldDialog: [TUTO] call#%d field=%s tutoId=%u->%u mode=%u->%u",
               s_tutoCallCount, fieldName,
               (unsigned)tutoIdBefore, (unsigned)tutoIdAfter,
               (unsigned)modeBefore, (unsigned)modeAfter);

    return result;
}

// ============================================================================
// v04.21: opcode_mesmode hook (0x106) — sets message display mode.
// Mode 2 = borderless (used for thoughts). Fires BEFORE the text is shown.
// ============================================================================

static int s_mesmodeCallCount = 0;

static int __cdecl Hook_opcode_mesmode(int entityPtr)
{
    uint16_t modeBefore = FF8Addresses::pGameMode ? *FF8Addresses::pGameMode : 0xFFFF;
    int result = s_origMesmode(entityPtr);
    uint16_t modeAfter = FF8Addresses::pGameMode ? *FF8Addresses::pGameMode : 0xFFFF;

    s_mesmodeCallCount++;
    const char* fieldName = FF8Addresses::pCurrentFieldName ?
                            FF8Addresses::pCurrentFieldName : "(null)";
    Log::Write("FieldDialog: [MESMODE] call#%d field=%s mode=%u->%u entity=0x%08X",
               s_mesmodeCallCount, fieldName,
               (unsigned)modeBefore, (unsigned)modeAfter, (uint32_t)entityPtr);

    // Also scan windows — mesmode often precedes the dialog text
    EnterCriticalSection(&s_cs);
    ScanAndSpeakAllWindows("MESMODE");
    LeaveCriticalSection(&s_cs);

    return result;
}

// ============================================================================
// v04.21: opcode_ramesw hook (0x116) — remote AMESW.
// One entity triggers auto-positioned dialog+wait on another entity.
// This might be how Quistis's script triggers Squall's thought text.
// ============================================================================

static int s_rameswCallCount = 0;

static int __cdecl Hook_opcode_ramesw(int entityPtr)
{
    uint16_t modeBefore = FF8Addresses::pGameMode ? *FF8Addresses::pGameMode : 0xFFFF;
    int result = s_origRamesw(entityPtr);
    uint16_t modeAfter = FF8Addresses::pGameMode ? *FF8Addresses::pGameMode : 0xFFFF;

    s_rameswCallCount++;
    const char* fieldName = FF8Addresses::pCurrentFieldName ?
                            FF8Addresses::pCurrentFieldName : "(null)";
    Log::Write("FieldDialog: [RAMESW] call#%d field=%s mode=%u->%u entity=0x%08X",
               s_rameswCallCount, fieldName,
               (unsigned)modeBefore, (unsigned)modeAfter, (uint32_t)entityPtr);

    // Scan windows after — the remote dialog text should now be loaded
    EnterCriticalSection(&s_cs);
    ScanAndSpeakAllWindows("RAMESW");
    LeaveCriticalSection(&s_cs);

    return result;
}

// ============================================================================
// v04.20: menu_draw_text naked hook
//
// Zero-overhead: just atomically increment a counter and jump to the
// original function. Preserves ALL registers, flags, and stack state.
// We use this to measure call rate during thought gaps.
// ============================================================================

__declspec(naked) static void Hook_menu_draw_text_naked()
{
    __asm {
        lock inc dword ptr [s_menuDrawTextCallCount]
        jmp dword ptr [s_origMenuDrawText_raw]
    }
}

// v04.22: update_field_entities naked hook — counts script interpreter calls
__declspec(naked) static void Hook_update_field_entities_naked()
{
    __asm {
        lock inc dword ptr [s_ufeCallCount]
        jmp dword ptr [s_origUpdateFieldEntities_raw]
    }
}

// ============================================================================
// v04.20: get_character_width hook — fires for EVERY glyph rendered.
// Accumulates FF8 char codes into a buffer. On a gap (no calls for 200ms),
// the poll thread decodes and speaks the accumulated text.
// This catches ALL rendered text regardless of code path.
// ============================================================================

// Accumulation buffer: stores raw FF8 char codes as they're rendered
static const int GCW_BUF_SIZE = 1024;
static uint8_t s_gcwBuf[GCW_BUF_SIZE];
static volatile LONG s_gcwBufLen = 0;
static volatile DWORD s_gcwLastCallTime = 0;
static std::string s_gcwLastSpoken;  // dedup

static uint32_t __cdecl Hook_get_character_width(uint32_t charCode)
{
    InterlockedIncrement(&s_gcwCallCount);
    s_gcwLastCallTime = GetTickCount();

    // Accumulate char code (only printable range, skip control codes)
    // FF8 char codes: 0x00-0x19 = A-Z, 0x1A-0x33 = a-z, 0x34-0x3D = 0-9
    // 0x3E = space, 0x40+ = punctuation. 0x00 = terminator in strings but
    // get_character_width receives the actual code, not terminator.
    if (charCode <= 0xFF) {
        LONG pos = InterlockedIncrement(&s_gcwBufLen) - 1;
        if (pos < GCW_BUF_SIZE) {
            s_gcwBuf[pos] = (uint8_t)charCode;
        }
    }

    return s_origGetCharWidth(charCode);
}

// Called from PollWindows — check if accumulated chars form speakable text
static void CheckGcwBuffer()
{
    DWORD lastCall = s_gcwLastCallTime;
    if (lastCall == 0) return;

    DWORD elapsed = GetTickCount() - lastCall;
    LONG bufLen = s_gcwBufLen;

    // Wait for a gap: no new chars for 200ms and buffer has content
    if (elapsed < 200 || bufLen == 0) return;

    // Snapshot and reset buffer
    int len = (bufLen < GCW_BUF_SIZE) ? (int)bufLen : GCW_BUF_SIZE;
    uint8_t localBuf[GCW_BUF_SIZE];
    memcpy(localBuf, s_gcwBuf, len);
    InterlockedExchange(&s_gcwBufLen, 0);

    // Decode using our FF8 text decoder
    std::string decoded = TrimDecoded(FF8TextDecode::Decode(localBuf, len));
    if (decoded.empty() || (int)decoded.length() < MIN_TEXT_LENGTH) return;

    // Dedup: skip if same as last GCW spoken or any window state
    if (decoded == s_gcwLastSpoken) return;
    for (int w = 0; w < MAX_WINDOWS; w++) {
        if (decoded == s_winState[w].lastSpokenText ||
            decoded == s_winState[w].lastRawText) return;
        if (IsSuffixOrSubstring(s_winState[w].lastSpokenText, decoded) ||
            IsSuffixOrSubstring(s_winState[w].lastRawText, decoded)) return;
    }

    s_gcwLastSpoken = decoded;
    Log::Write("FieldDialog: [GCW-SPEAK] %d chars -> \"%s\"", len, decoded.c_str());
    ScreenReader::Speak(decoded.c_str(), false);
}

// ============================================================================
// Detour handlers
// ============================================================================

static int __cdecl Hook_opcode_mes(int entityPtr)
{
    int result = s_origMes(entityPtr);
    EnterCriticalSection(&s_cs);
    ScanAndSpeakAllWindows("MES");
    LeaveCriticalSection(&s_cs);
    return result;
}

static int __cdecl Hook_opcode_mesw(int entityPtr)
{
    int result = s_origMesw(entityPtr);
    EnterCriticalSection(&s_cs);
    ScanAndSpeakAllWindows("MESW");
    LeaveCriticalSection(&s_cs);
    return result;
}

static int __cdecl Hook_opcode_ask(int entityPtr)
{
    int result = s_origAsk(entityPtr);
    EnterCriticalSection(&s_cs);
    ScanAndSpeakChoiceWindows("ASK");
    LeaveCriticalSection(&s_cs);
    return result;
}

static int __cdecl Hook_opcode_ames(int entityPtr)
{
    int result = s_origAmes(entityPtr);
    EnterCriticalSection(&s_cs);
    ScanAndSpeakAllWindows("AMES");
    LeaveCriticalSection(&s_cs);
    return result;
}

static int __cdecl Hook_opcode_aask(int entityPtr)
{
    int result = s_origAask(entityPtr);
    EnterCriticalSection(&s_cs);
    ScanAndSpeakChoiceWindows("AASK");
    LeaveCriticalSection(&s_cs);
    return result;
}

static int __cdecl Hook_opcode_amesw(int entityPtr)
{
    int result = s_origAmesw(entityPtr);
    EnterCriticalSection(&s_cs);
    ScanAndSpeakAllWindows("AMESW");
    LeaveCriticalSection(&s_cs);
    return result;
}

// ============================================================================
// v04.25: Repeat last dialog (F5 hotkey)
// ============================================================================

void RepeatLastDialog()
{
    if (s_lastDialogSpoken.empty()) {
        ScreenReader::Speak("No dialog to repeat.", true);
        return;
    }
    Log::Write("FieldDialog: [REPEAT] \"%s\"", s_lastDialogSpoken.c_str());
    ScreenReader::Speak(s_lastDialogSpoken.c_str(), true);  // Interrupt current speech
}

// ============================================================================
// v04.25: opcode_menuname hook (0x129) — auto-bypass character naming screen
// The game sets default character names before calling MENUNAME.
// We hook it, call the original to open the naming screen, then
// auto-confirm with simulated Enter key presses to keep default names.
// ============================================================================

// v0.09.05: Restored FULL v04.35 bypass including enableGF calls.
// enableGF(charIdx) at 0x47E480 does essential character junction init —
// without it, subsequent scripts malfunction (infirmary scene breaks).
// The enableGF calls DO mark GFs 0-5 as obtained in savemap, even before
// the player earns them. This is handled at the TTS layer by reading the
// game's own displayed GF list rather than filtering by savemap exists flags.
static int __cdecl Hook_opcode_menuname(int entityPtr)
{
    const char* fieldName = FF8Addresses::pCurrentFieldName ?
                            FF8Addresses::pCurrentFieldName : "?";
    Log::Write("FieldDialog: [MENUNAME] Bypassing naming screen. field=%s entity=0x%08X",
               fieldName, (uint32_t)entityPtr);
    // v0.09.14: Smart bypass with correct parameter reading.
    // Disassembly of opcode_menuname shows: param = entityPtr[stackPtr * 4]
    // where stackPtr = byte at entityPtr+0x184. Stack is at START of struct.
    int param = -1;
    __try {
        uint8_t* ep = (uint8_t*)entityPtr;
        uint8_t sp = ep[0x184];
        param = *(int32_t*)(ep + sp * 4);
        Log::Write("FieldDialog: [MENUNAME] stackPtr=%u, param=%d (at +0x%02X)",
                   (unsigned)sp, param, sp * 4);
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        Log::Write("FieldDialog: [MENUNAME] SEH reading param");
    }

    // Character names (0-7) and GF names (8-23)
    static const char* s_charNames[] = {
        "Squall", "Zell", "Irvine", "Quistis", "Rinoa", "Selphie", "Seifer", "Edea"
    };
    static const char* s_gfNames[] = {
        "Quezacotl", "Shiva", "Ifrit", "Siren", "Brothers", "Diablos",
        "Carbuncle", "Leviathan", "Pandemona", "Cerberus", "Alexander",
        "Doomtrain", "Bahamut", "Cactuar", "Tonberry", "Eden"
    };

    // v0.09.19: Snapshot GF exists flags BEFORE calling original handler.
    // The original's switch table writes GF data to savemap. By diffing
    // before/after, we detect acquisitions at exactly the right moment
    // with zero polling cost during normal gameplay.
    static const uint32_t SAVEMAP_BASE_ADDR = 0x1CFDC5C;
    static const int GF_STRUCT_SIZE_LOCAL = 0x44;  // 68 bytes per GF
    static const int GF_COUNT_LOCAL = 16;
    uint8_t gfBefore[16] = {};
    {
        uint8_t* sm = (uint8_t*)SAVEMAP_BASE_ADDR;
        for (int g = 0; g < GF_COUNT_LOCAL; g++)
            gfBefore[g] = sm[0x4C + g * GF_STRUCT_SIZE_LOCAL + 0x11];
    }

    // Call original handler for ALL params — it does essential init work
    // (switch table writes savemap data, GF assignments, character setup).
    // Then suppress the naming UI by clearing the trigger flags.
    Log::Write("FieldDialog: [MENUNAME] Calling original handler (param=%d)", param);
    int result = s_origMenuname(entityPtr);
    // Clear naming UI triggers before main loop sees them
    __try {
        *(uint8_t*)0x01CE4760 = 0;   // pMode0Phase - clear naming UI mode
        *(uint8_t*)0x01CE490B = 0;   // naming flag - clear
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        Log::Write("FieldDialog: [MENUNAME] SEH clearing UI flags");
    }

    // v0.09.19: Check for new GF acquisitions (exists flag 0→non-zero)
    bool gfAcquired = false;
    {
        static const char* GF_NAMES_LOCAL[] = {
            "Quezacotl", "Shiva", "Ifrit", "Siren", "Brothers", "Diablos",
            "Carbuncle", "Leviathan", "Pandemona", "Cerberus", "Alexander",
            "Doomtrain", "Bahamut", "Cactuar", "Tonberry", "Eden"
        };
        uint8_t* sm = (uint8_t*)SAVEMAP_BASE_ADDR;
        for (int g = 0; g < GF_COUNT_LOCAL; g++) {
            uint8_t after = sm[0x4C + g * GF_STRUCT_SIZE_LOCAL + 0x11];
            if (gfBefore[g] == 0 && after != 0) {
                gfAcquired = true;
                char buf[128];
                sprintf(buf, "GF %s acquired", GF_NAMES_LOCAL[g]);
                ScreenReader::Speak(buf, false);  // queue after any dialog
                Log::Write("FieldDialog: [MENUNAME] %s (idx=%d, flag 0x%02X)",
                           buf, g, (unsigned)after);
            }
        }
    }

    // v0.09.21: Announce character name only when no GF was acquired.
    // When the original handler writes GF data (e.g. Quistis/Rinoa at study panel),
    // the GF announcement is sufficient — the character name is noise.
    // Squall (param 0) always announces because no GFs are given with him.
    if (param >= 0 && param <= 7 && !gfAcquired) {
        ScreenReader::Speak(s_charNames[param], false);
        Log::Write("FieldDialog: [MENUNAME] Character: %s", s_charNames[param]);
    } else if (param >= 0 && param <= 7) {
        Log::Write("FieldDialog: [MENUNAME] Character: %s (suppressed, GF acquired)", s_charNames[param]);
    }
    Log::Write("FieldDialog: [MENUNAME] UI suppressed, returning %d", result);
    return result;
}

// ============================================================================
// MinHook detour creation
// ============================================================================

static bool CreateDetourHook(uint32_t targetAddr, OpcodeHandler_t newHandler,
                              OpcodeHandler_t* outOriginal, const char* label)
{
    if (targetAddr == 0) {
        Log::Write("FieldDialog: Cannot hook %s - address is null", label);
        return false;
    }

    MH_STATUS status = MH_CreateHook(
        (LPVOID)targetAddr, (LPVOID)newHandler, (LPVOID*)outOriginal);

    if (status != MH_OK) {
        Log::Write("FieldDialog: MH_CreateHook failed for %s at 0x%08X (status=%d)",
                   label, targetAddr, (int)status);
        return false;
    }

    Log::Write("FieldDialog: Hooked %s: target=0x%08X trampoline=0x%08X",
               label, targetAddr, (uint32_t)(uintptr_t)*outOriginal);
    return true;
}

// ============================================================================
// Public interface
// ============================================================================

bool Initialize()
{
    if (s_initialized) return true;

    Log::Write("FieldDialog: === Initializing field dialog hooks (v04.36) ===");

    InitializeCriticalSection(&s_cs);

    if (FF8Addresses::opcode_mes == 0) {
        Log::Write("FieldDialog: ERROR - opcode_mes not resolved.");
        return false;
    }

    Log::Write("FieldDialog: pWindowsArray = 0x%08X",
               (uint32_t)(uintptr_t)FF8Addresses::pWindowsArray);

    bool anySuccess = false;

    if (CreateDetourHook(FF8Addresses::opcode_mesw, Hook_opcode_mesw, &s_origMesw, "opcode_mesw"))
        anySuccess = true;
    if (CreateDetourHook(FF8Addresses::opcode_mes, Hook_opcode_mes, &s_origMes, "opcode_mes"))
        anySuccess = true;
    if (CreateDetourHook(FF8Addresses::opcode_ask, Hook_opcode_ask, &s_origAsk, "opcode_ask"))
        anySuccess = true;
    if (CreateDetourHook(FF8Addresses::opcode_ames, Hook_opcode_ames, &s_origAmes, "opcode_ames"))
        anySuccess = true;
    if (CreateDetourHook(FF8Addresses::opcode_aask, Hook_opcode_aask, &s_origAask, "opcode_aask"))
        anySuccess = true;
    if (CreateDetourHook(FF8Addresses::opcode_amesw, Hook_opcode_amesw, &s_origAmesw, "opcode_amesw"))
        anySuccess = true;

    // v04.16: Hook field_get_dialog_string (different signature than opcodes)
    if (FF8Addresses::field_get_dialog_string != 0) {
        MH_STATUS st = MH_CreateHook(
            (LPVOID)FF8Addresses::field_get_dialog_string,
            (LPVOID)Hook_field_get_dialog_string,
            (LPVOID*)&s_origGetDialogString);
        if (st == MH_OK) {
            Log::Write("FieldDialog: Hooked field_get_dialog_string: target=0x%08X trampoline=0x%08X",
                       FF8Addresses::field_get_dialog_string,
                       (uint32_t)(uintptr_t)s_origGetDialogString);
            anySuccess = true;
        } else {
            Log::Write("FieldDialog: FAILED to hook field_get_dialog_string (status=%d)", (int)st);
        }
    }

    // v04.18: Hook opcode_tuto (0x177) for diagnostic logging
    if (FF8Addresses::opcode_tuto != 0) {
        if (CreateDetourHook(FF8Addresses::opcode_tuto, Hook_opcode_tuto, &s_origTuto, "opcode_tuto"))
            anySuccess = true;
    } else {
        Log::Write("FieldDialog: WARNING - opcode_tuto not resolved");
    }

    // v04.21: Hook opcode_mesmode (0x106) and opcode_ramesw (0x116)
    if (FF8Addresses::opcode_mesmode != 0) {
        if (CreateDetourHook(FF8Addresses::opcode_mesmode, Hook_opcode_mesmode, &s_origMesmode, "opcode_mesmode"))
            anySuccess = true;
    } else {
        Log::Write("FieldDialog: WARNING - opcode_mesmode not resolved");
    }
    if (FF8Addresses::opcode_ramesw != 0) {
        if (CreateDetourHook(FF8Addresses::opcode_ramesw, Hook_opcode_ramesw, &s_origRamesw, "opcode_ramesw"))
            anySuccess = true;
    } else {
        Log::Write("FieldDialog: WARNING - opcode_ramesw not resolved");
    }

    // v04.17: Hook show_dialog (universal text renderer) for MODE_TUTO
    if (FF8Addresses::show_dialog_addr != 0) {
        MH_STATUS st = MH_CreateHook(
            (LPVOID)FF8Addresses::show_dialog_addr,
            (LPVOID)Hook_show_dialog,
            (LPVOID*)&s_origShowDialog);
        if (st == MH_OK) {
            Log::Write("FieldDialog: Hooked show_dialog: target=0x%08X trampoline=0x%08X",
                       FF8Addresses::show_dialog_addr,
                       (uint32_t)(uintptr_t)s_origShowDialog);
            anySuccess = true;
        } else {
            Log::Write("FieldDialog: FAILED to hook show_dialog (status=%d)", (int)st);
        }
    } else {
        Log::Write("FieldDialog: WARNING - show_dialog not resolved, TUTO/thoughts won't be caught");
    }

    // v04.20: Hook menu_draw_text (naked counter for call-rate diagnostic)
    if (FF8Addresses::menu_draw_text_addr != 0) {
        MH_STATUS st = MH_CreateHook(
            (LPVOID)FF8Addresses::menu_draw_text_addr,
            (LPVOID)Hook_menu_draw_text_naked,
            (LPVOID*)&s_origMenuDrawText_raw);
        if (st == MH_OK) {
            Log::Write("FieldDialog: Hooked menu_draw_text: target=0x%08X trampoline=0x%08X",
                       FF8Addresses::menu_draw_text_addr,
                       (uint32_t)(uintptr_t)s_origMenuDrawText_raw);
            anySuccess = true;
        } else {
            Log::Write("FieldDialog: FAILED to hook menu_draw_text (status=%d)", (int)st);
        }
    } else {
        Log::Write("FieldDialog: WARNING - menu_draw_text not resolved");
    }

    // v04.20: Hook get_character_width (per-glyph, accumulation-based text capture)
    if (FF8Addresses::get_character_width_addr != 0) {
        MH_STATUS st = MH_CreateHook(
            (LPVOID)FF8Addresses::get_character_width_addr,
            (LPVOID)Hook_get_character_width,
            (LPVOID*)&s_origGetCharWidth);
        if (st == MH_OK) {
            Log::Write("FieldDialog: Hooked get_character_width: target=0x%08X trampoline=0x%08X",
                       FF8Addresses::get_character_width_addr,
                       (uint32_t)(uintptr_t)s_origGetCharWidth);
            anySuccess = true;
        } else {
            Log::Write("FieldDialog: FAILED to hook get_character_width (status=%d)", (int)st);
        }
    } else {
        Log::Write("FieldDialog: WARNING - get_character_width not resolved");
    }

    // v0.09.08: DISABLED update_field_entities hook for infirmary glitch diagnosis.
    // This naked hook intercepts the script interpreter entry point.
    Log::Write("FieldDialog: [DIAG] update_field_entities hook DISABLED for infirmary glitch test");

    // v0.09.04: Restored menuname hook (v04.35 style, minus enableGF)
    if (FF8Addresses::opcode_menuname != 0) {
        if (CreateDetourHook(FF8Addresses::opcode_menuname, Hook_opcode_menuname, &s_origMenuname, "opcode_menuname"))
            anySuccess = true;
    } else {
        Log::Write("FieldDialog: WARNING - opcode_menuname not resolved");
    }

    // v0.09.08: DISABLED dispatch patch + update_field_entities hook for infirmary glitch diagnosis.
    // These intercept EVERY script opcode execution — prime suspects for NPC walk hang.
    // if (PatchDispatchSite()) { anySuccess = true; }
    Log::Write("FieldDialog: [DIAG] Dispatch patch DISABLED for infirmary glitch test");

    if (!anySuccess) {
        Log::Write("FieldDialog: ERROR - No hooks were installed.");
        return false;
    }

    for (int i = 0; i < MAX_WINDOWS; i++)
        s_winState[i].Reset();

    s_initialized = true;
    Log::Write("FieldDialog: === Initialization complete ===");
    return true;
}

// ============================================================================
// Polling fallback: called from accessibility thread every ~100ms
//
// Catches dialogs that bypass our hooked opcodes (e.g. Squall's internal
// thoughts, some NPC chatter). The hooks remain the primary low-latency
// path for most dialogs; this is a safety net.
//
// Thread safety: acquires s_cs to coordinate with hook callbacks.
// ============================================================================

// Periodic raw dump of ALL window slots (every ~2 seconds)
// Logs pointer, first 8 hex bytes, and state for EVERY window regardless
// of validation. This catches text that our pointer checks might filter out.
static DWORD s_lastRawDumpTime = 0;

static void DiagRawWindowDump()
{
    DWORD now = GetTickCount();
    if ((now - s_lastRawDumpTime) < 2000) return;
    s_lastRawDumpTime = now;

    // Check if any window has anything interesting (non-zero text pointer)
    bool anyInteresting = false;
    for (int i = 0; i < MAX_WINDOWS; i++) {
        uint8_t* winObj = GetWindowObj(i);
        char* text1 = GetWinText1(winObj);
        if (text1 != nullptr) { anyInteresting = true; break; }
    }
    if (!anyInteresting) return;

    char buf[2048];
    int pos = 0;
    pos += snprintf(buf + pos, sizeof(buf) - pos, "[RAWDUMP]");

    for (int i = 0; i < MAX_WINDOWS; i++) {
        uint8_t* winObj = GetWindowObj(i);
        char* text1 = GetWinText1(winObj);
        uint32_t state = *(uint32_t*)(winObj + WIN_OBJ_STATE_OFFSET);

        if (!text1 && state == 0) continue;  // truly empty

        uintptr_t addr = (uintptr_t)text1;
        if (!text1) {
            pos += snprintf(buf + pos, sizeof(buf) - pos,
                            " [%d NULL st=%u]", i, state);
            continue;
        }

        // Try to read first 8 bytes with SEH (even if pointer looks bad)
        uint8_t raw[8] = {0};
        bool readable = false;
        __try {
            for (int j = 0; j < 8; j++) raw[j] = ((uint8_t*)text1)[j];
            readable = true;
        } __except(EXCEPTION_EXECUTE_HANDLER) {}

        // v04.23: also show text_data2 pointer and open_close_transition
        char* text2 = GetWinText2(winObj);
        int16_t transition = GetWinOpenCloseTransition(winObj);

        if (readable) {
            pos += snprintf(buf + pos, sizeof(buf) - pos,
                            " [%d t1=%08X t2=%08X st=%u tr=%d hex=%02X%02X%02X%02X%02X%02X%02X%02X]",
                            i, (uint32_t)addr, (uint32_t)(uintptr_t)text2,
                            state, (int)transition,
                            raw[0], raw[1], raw[2], raw[3],
                            raw[4], raw[5], raw[6], raw[7]);
        } else {
            pos += snprintf(buf + pos, sizeof(buf) - pos,
                            " [%d t1=%08X t2=%08X st=%u tr=%d UNREADABLE]",
                            i, (uint32_t)addr, (uint32_t)(uintptr_t)text2,
                            state, (int)transition);
        }
    }

    Log::Write("%s", buf);
}

// v04.16: Track FMV transitions to suppress stale text after FMV
static bool s_lastPollMoviePlaying = false;
static DWORD s_movieEndTime = 0;
static const DWORD FMV_SUPPRESS_MS = 1500;
// v04.18: Continuous re-snapshot period after FMV suppression expires.
// Every poll cycle during this period captures window state without speaking.
// This prevents rapidly-changing garbage in window buffers from being spoken.
static DWORD s_postFmvResnapEndTime = 0;
static const DWORD POST_FMV_RESNAP_MS = 2500;  // re-snapshot for 2.5 more seconds

void PollWindows()
{
    if (!s_initialized || !FF8Addresses::pWindowsArray) return;
    // v04.17: Also poll during MODE_TUTO (thoughts/tutorials)
    if (!FF8Addresses::IsOnField() && !FF8Addresses::IsOnTuto()) return;

    // v04.16: Suppress polling during and briefly after FMV playback
    // Prevents stale/garbled window text from being spoken during transitions
    bool movieNow = FF8Addresses::IsMoviePlaying();
    if (movieNow) {
        s_lastPollMoviePlaying = true;
        return;
    }
    if (s_lastPollMoviePlaying) {
        // FMV just ended — capture current stale text as "already spoken"
        // so the poller doesn't re-announce it when suppression ends.
        s_lastPollMoviePlaying = false;
        s_movieEndTime = GetTickCount();
        EnterCriticalSection(&s_cs);
        for (int i = 0; i < MAX_WINDOWS; i++) {
            uint8_t* winObj = GetWindowObj(i);
            char* text1 = GetWinText1(winObj);
            if (text1 && IsValidTextPointer(text1) && ProbePointer(text1) && *(const uint8_t*)text1 != 0x00) {
                std::string decoded = TrimDecoded(FF8TextDecode::Decode((const uint8_t*)text1, 512));
                if (!decoded.empty()) {
                    s_winState[i].lastSpokenText = decoded;
                    s_winState[i].lastRawText = decoded;
                }
            }
        }
        LeaveCriticalSection(&s_cs);
        Log::Write("FieldDialog: [POLL] FMV ended, captured stale text, suppressing %ums + resnap %ums",
                   FMV_SUPPRESS_MS, POST_FMV_RESNAP_MS);
        return;
    }
    if (s_movieEndTime != 0) {
        DWORD elapsed = GetTickCount() - s_movieEndTime;
        if (elapsed < FMV_SUPPRESS_MS) {
            return;  // Still in post-FMV suppression window
        }
        // v04.18: After suppression, enter continuous re-snapshot period.
        // Start the timer on first entry into this phase.
        if (s_postFmvResnapEndTime == 0) {
            s_postFmvResnapEndTime = GetTickCount() + POST_FMV_RESNAP_MS;
            Log::Write("FieldDialog: [POLL] Post-FMV suppression ended, starting resnap period");
        }
        if (GetTickCount() < s_postFmvResnapEndTime) {
            // Still in re-snapshot period: capture current window text as
            // "already spoken" without speaking. This absorbs rapidly-changing
            // garbage that appears in window buffers during transitions.
            EnterCriticalSection(&s_cs);
            for (int i = 0; i < MAX_WINDOWS; i++) {
                uint8_t* winObj = GetWindowObj(i);
                char* text1 = GetWinText1(winObj);
                if (text1 && IsValidTextPointer(text1) && ProbePointer(text1) && *(const uint8_t*)text1 != 0x00) {
                    std::string decoded = TrimDecoded(FF8TextDecode::Decode((const uint8_t*)text1, 512));
                    if (!decoded.empty()) {
                        s_winState[i].lastSpokenText = decoded;
                        s_winState[i].lastRawText = decoded;
                    }
                }
            }
            // Also reset show_dialog per-window tracking to absorb pointer changes
            for (int i = 0; i < MAX_WINDOWS; i++) {
                s_sdLastTextPtr[i] = nullptr;
                s_sdLastHash[i] = 0;  // v04.23: reset hashes too
                s_sdLastDecoded[i].clear();
            }
            LeaveCriticalSection(&s_cs);
            return;
        }
        // Re-snapshot period ended — clear FMV state, resume normal polling
        Log::Write("FieldDialog: [POLL] Post-FMV resnap period ended, resuming normal polling");
        s_movieEndTime = 0;
        s_postFmvResnapEndTime = 0;
    }

    DiagRawWindowDump();

    EnterCriticalSection(&s_cs);
    ScanAndSpeakAllWindows("POLL");
    CheckPendingTexts();  // v04.16: Speak deferred getstr texts
    // v04.24: Disabled GCW speak — was diagnostic from v04.20, now causes
    // garbled "-G'" speech from character naming screen menu glyphs.
    // CheckGcwBuffer();
    LeaveCriticalSection(&s_cs);
}

void Shutdown()
{
    if (!s_initialized) return;

    if (s_origMesw)  MH_DisableHook((LPVOID)FF8Addresses::opcode_mesw);
    if (s_origMes)   MH_DisableHook((LPVOID)FF8Addresses::opcode_mes);
    if (s_origAsk)   MH_DisableHook((LPVOID)FF8Addresses::opcode_ask);
    if (s_origAmes)  MH_DisableHook((LPVOID)FF8Addresses::opcode_ames);
    if (s_origAask)  MH_DisableHook((LPVOID)FF8Addresses::opcode_aask);
    if (s_origAmesw) MH_DisableHook((LPVOID)FF8Addresses::opcode_amesw);
    if (s_origGetDialogString) MH_DisableHook((LPVOID)FF8Addresses::field_get_dialog_string);
    if (s_origTuto)  MH_DisableHook((LPVOID)FF8Addresses::opcode_tuto);
    if (s_origMesmode) MH_DisableHook((LPVOID)FF8Addresses::opcode_mesmode);
    if (s_origRamesw)  MH_DisableHook((LPVOID)FF8Addresses::opcode_ramesw);
    if (s_origMenuname) MH_DisableHook((LPVOID)FF8Addresses::opcode_menuname);
    if (s_origShowDialog) MH_DisableHook((LPVOID)FF8Addresses::show_dialog_addr);
    if (s_origMenuDrawText_raw) MH_DisableHook((LPVOID)FF8Addresses::menu_draw_text_addr);
    if (s_origGetCharWidth) MH_DisableHook((LPVOID)FF8Addresses::get_character_width_addr);
    if (s_origUpdateFieldEntities_raw) MH_DisableHook((LPVOID)FF8Addresses::update_field_entities_addr);
    UnpatchDispatchSite();

    s_origMesw = s_origMes = s_origAsk = s_origAmes = s_origAask = s_origAmesw = nullptr;
    s_origGetDialogString = nullptr;
    s_origTuto = nullptr;
    s_origMesmode = nullptr;
    s_origRamesw = nullptr;
    s_origMenuname = nullptr;
    s_origShowDialog = nullptr;
    s_origMenuDrawText_raw = nullptr;
    s_origGetCharWidth = nullptr;
    s_origUpdateFieldEntities_raw = nullptr;

    EnterCriticalSection(&s_cs);
    for (int i = 0; i < MAX_WINDOWS; i++)
        s_winState[i].Reset();
    s_pendingCount = 0;
    s_lastGetstrText.clear();
    s_lastTutoText.clear();
    LeaveCriticalSection(&s_cs);

    DeleteCriticalSection(&s_cs);

    s_initialized = false;
    Log::Write("FieldDialog: Shutdown complete.");
}

bool IsActive()
{
    return s_initialized;
}

// v05.39: Returns true if any dialog window is currently actively displayed.
// After a dialog is dismissed, FF8 leaves state non-zero and text pointers
// valid, but sets open_close_transition (offset +0x1C) to 0.  RAWDUMP confirms:
//   active dialog:  st=7 tr=4096  or  st=13 tr=4096
//   dismissed:      st=7 tr=0     (text still readable, but dialog is gone)
// So the reliable indicator is: state != 0 AND open_close_transition != 0.
// Safe to call from any thread — int16/uint32 reads are atomic on x86.
bool IsDialogOpen()
{
    if (!s_initialized || !FF8Addresses::pWindowsArray) return false;
    __try {
        for (int i = 0; i < MAX_WINDOWS; i++) {
            uint8_t* winObj = GetWindowObj(i);
            if (!winObj) continue;
            uint32_t state = *(uint32_t*)(winObj + WIN_OBJ_STATE_OFFSET);
            if (state == 0) continue;
            // open_close_transition at +0x1C: non-zero = actively displayed.
            int16_t transition = *(int16_t*)(winObj + 0x1C);
            if (transition != 0)
                return true;
        }
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {}
    return false;
}

// v07.09: Expose text rendering call counters for save screen diagnostic.
LONG GetMenuDrawTextCallCount()
{
    return InterlockedCompareExchange(&s_menuDrawTextCallCount, 0, 0);
}

LONG GetGetCharWidthCallCount()
{
    return InterlockedCompareExchange(&s_gcwCallCount, 0, 0);
}

// v07.10: Snapshot and reset the GCW accumulation buffer.
int SnapshotGcwBuffer(uint8_t* outBuf, int maxLen)
{
    LONG bufLen = InterlockedExchange(&s_gcwBufLen, 0);
    int len = (bufLen < maxLen) ? (int)bufLen : maxLen;
    if (len > GCW_BUF_SIZE) len = GCW_BUF_SIZE;
    if (len > 0) {
        memcpy(outBuf, s_gcwBuf, len);
    }
    return len;
}

}  // namespace FieldDialog
