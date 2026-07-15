// field_dialog_state.inl -- module state, structs, constants, typedefs.
//
// Included FIRST inside the FieldDialog namespace from field_dialog.cpp.
// Every other .inl in the chain references symbols declared here.
//
// .inl rules: no header guards, no namespace declarations inside, no
// pragma once. Textual include, lives inside the parent namespace.

// ============================================================================
// Function-pointer typedefs for MinHook trampolines
// ============================================================================

typedef int (__cdecl *OpcodeHandler_t)(int);
// v04.23: Fixed signature. FFNx voice.cpp shows: char* (char* msgBase, int dialogId)
typedef char* (__cdecl *FieldGetDialogString_t)(char* msgBase, int dialogId);
typedef char (__cdecl *ShowDialog_t)(int32_t window_id, uint32_t state, int16_t a3);

// v04.20: get_character_width -- per-glyph function
typedef uint32_t (__cdecl *GetCharWidth_t)(uint32_t charCode);

// ============================================================================
// Module state -- initialization + thread sync
// ============================================================================

static bool s_initialized = false;
static CRITICAL_SECTION s_cs;  // Protects s_winState[] (game thread hooks + poll thread)

// ============================================================================
// Trampoline pointers -- written by MinHook in Initialize()
// ============================================================================

static OpcodeHandler_t s_origMes = nullptr;
static OpcodeHandler_t s_origMesw = nullptr;  // v04.14: MESW (0x46) -- message + wait
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
static GetCharWidth_t s_origGetCharWidth = nullptr;

// v04.22: update_field_entities -- naked counter hook
static void* s_origUpdateFieldEntities_raw = nullptr;
static volatile LONG s_ufeCallCount = 0;
static LONG s_ufeLastReported = 0;

// v04.20: menu_draw_text -- naked hook for zero-overhead call counting.
// We don't know the signature, so we just increment a counter and jump
// to the original via the trampoline. This preserves all registers and
// stack state perfectly.
static void* s_origMenuDrawText_raw = nullptr;
static volatile LONG s_menuDrawTextCallCount = 0;
static LONG s_menuDrawTextLastReported = 0;  // for delta in diagnostic

// v04.20: get_character_width counters (declared early for use in show_dialog diag)
static volatile LONG s_gcwCallCount = 0;
static LONG s_gcwLastReported = 0;

// ============================================================================
// v04.22: opcode dispatch instrumentation
// We patch the CALL [eax*4 + table] at update_field_entities + 0x657
// to redirect through our code cave, which logs the opcode index (EAX).
// ============================================================================

static const int OPCODE_HIST_SIZE = 512;
static volatile LONG s_opcodeHistogram[OPCODE_HIST_SIZE] = {0};
static volatile LONG s_opcodeOverflow = 0;           // opcodes >= OPCODE_HIST_SIZE
static LONG s_opcodeHistogramPrev[OPCODE_HIST_SIZE] = {0};
static LONG s_opcodeOverflowPrev = 0;
static bool s_dispatchPatched = false;
static uint8_t s_dispatchOrigBytes[8] = {0};  // saved original bytes
static uint32_t s_dispatchAddr = 0;           // address of the CALL instruction
static uint32_t s_opcodeTableAddr = 0;        // original table address for our cave

// Used by DispatchStub / DispatchStub_EDX (defined in diag.inl) to save/restore
// registers and JMP back to the engine after the handler returns. Statics
// instead of stack because script execution is single-threaded.
static uint32_t s_dispatchRetAddr = 0;  // address after the 7-byte patch
static uint32_t s_savedEdx = 0;
static uint32_t s_handlerAddr = 0;

// ============================================================================
// Window-object layout constants
// ============================================================================

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
    std::string decoded;   // Decoded text (as of FETCH time -- inserts may be stale)
    DWORD fetchTime;       // GetTickCount() when fetched
    bool spoken;           // Set true when spoken by opcode hook or poll
    int messageId;         // FF8 message ID for logging
    // v0.18.3.250 (#80): raw FF8 bytes of the message, captured at fetch time.
    // v0.18.3.251 (#80): the .250 BAT proved the snapshot is NOT enough — the
    // drain re-decode of the frozen copy STILL resolved the insert empty while
    // live decodes the same tick resolved "Cure": the engine MUTATES the
    // message buffer in place between script-fetch and render (pokes the
    // insert param). So the drain now re-reads the LIVE buffer via rawPtr
    // (SEH-guarded) and uses this snapshot only as the fallback when the live
    // read faults. raw[0]==0x00 => no capture; rawPtr may dangle after a
    // field transition, which the SEH copy absorbs.
    uint8_t raw[512];
    const char* rawPtr;    // v0.18.3.251 (#80): live message pointer at fetch
};

static const int MAX_PENDING = 8;
static PendingText s_pending[MAX_PENDING];
static int s_pendingCount = 0;
static std::string s_lastGetstrText;  // Last text returned by field_get_dialog_string

static const DWORD PENDING_SPEAK_DELAY_MS = 500;

// ============================================================================
// v04.17 show_dialog hook state -- diagnostics + dedup
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

// v04.23: track text pointer + hash per window in show_dialog
// Replaces pointer-only check that missed in-place buffer rewrites
static char* s_sdLastTextPtr[MAX_WINDOWS] = {0};
static uint32_t s_sdLastHash[MAX_WINDOWS] = {0};
static std::string s_sdLastDecoded[MAX_WINDOWS];

// ============================================================================
// v04.16 / v04.21 opcode hook diagnostic counters
// ============================================================================

// v04.16 diagnostic: count calls to see if the hook fires at all
static int s_getstrCallCount = 0;
static DWORD s_getstrLastDiagTime = 0;

static int s_tutoCallCount = 0;
static int s_mesmodeCallCount = 0;
static int s_rameswCallCount = 0;

// ============================================================================
// v04.20: get_character_width accumulation buffer
//
// Hook_get_character_width fires for EVERY glyph rendered. We accumulate raw
// FF8 char codes; on a 200ms gap (no new chars), the poll thread decodes and
// speaks the accumulated text. Catches text via any code path.
// ============================================================================

static const int GCW_BUF_SIZE = 1024;
static uint8_t s_gcwBuf[GCW_BUF_SIZE];
static volatile LONG s_gcwBufLen = 0;
static volatile DWORD s_gcwLastCallTime = 0;
static std::string s_gcwLastSpoken;  // dedup

// ============================================================================
// PollWindows() state -- FMV transition tracking + raw-dump throttle
// ============================================================================

// Periodic raw dump of ALL window slots (every ~2 seconds)
static DWORD s_lastRawDumpTime = 0;

// v04.16: Track FMV transitions to suppress stale text after FMV
static bool s_lastPollMoviePlaying = false;
static DWORD s_movieEndTime = 0;
static const DWORD FMV_SUPPRESS_MS = 1500;
// v04.18: Continuous re-snapshot period after FMV suppression expires.
// Every poll cycle during this period captures window state without speaking.
// This prevents rapidly-changing garbage in window buffers from being spoken.
static DWORD s_postFmvResnapEndTime = 0;
static const DWORD POST_FMV_RESNAP_MS = 2500;  // re-snapshot for 2.5 more seconds

// ============================================================================
// Forward declarations for cross-.inl references
//
// scan.inl defines MarkPendingAsSpoken, which is called by show_dialog.inl
// and opcodes.inl (both included AFTER scan.inl in the parent .cpp chain,
// so technically they see the definition already). This forward declaration
// mirrors the original monolithic file's pattern and documents the call
// site for readers.
// ============================================================================

static void MarkPendingAsSpoken(const std::string& spokenText);
