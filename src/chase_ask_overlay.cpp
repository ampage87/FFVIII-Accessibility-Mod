// chase_ask_overlay.cpp — Chase entry ASK overlay (manual / auto-drive).
// See chase_ask_overlay.h for the public design notes.
//
// v0.15.1: New module. Triggered by Squall's "Forget it!  Let's go!" MES
// in any chase field (per v0.15.0 BAT confirmation — that's the exact
// chase-start dialog, NOT the "Run!" line that the deep research
// suggested). The trigger mechanism is the show_dialog hook in
// field_dialog.cpp, which calls our OnDialogText() on every decoded
// field dialog text.
//
// Engine-window template values:
//   The plan says to capture mode1 / state values from a real engine
//   ASK fired before the chase via chase_diag's ASK snapshot logging
//   (added in change #2 of v0.15.1). For v0.15.1's first BAT we use
//   educated-guess defaults: mode1=0x05, state=0x07, first_question=1,
//   last_question=2, current_choice_question=1. If the engine doesn't
//   render the slot correctly, the TTS + keyboard fallback still works.
//
// The chase ASK fires once per chase session. We track the once-fired
// flag with ChaseDetector::IsChaseActive() — the flag resets when
// chase_active flips back to false (i.e. when we land on a non-chase
// field, including Lapin Beach FMV at chase end), so a future replay
// of the chase in the same play session can re-trigger.

#include "chase_ask_overlay.h"
#include "chase_detector.h"
#include "ff8_accessibility.h"
#include "ff8_addresses.h"
#include "mod_forward_decls.h"

#include <windows.h>
#include <cstdio>
#include <cstring>

namespace ChaseAskOverlay {

// ============================================================================
// Constants
// ============================================================================

// The exact chase-trigger MES, confirmed by v0.15.0 BAT. Note the
// double-space — v0.15.0 dialog log capture preserved it.
static const char* CHASE_TRIGGER_TEXT = "Forget it!  Let's go!";

// v0.15.1.2: Delay between trigger detection and ASK open. Gives the
// engine + NVDA time to display and speak Squall's chase-trigger line
// before we preempt with our own prompt. v0.15.1.1 BAT showed our
// SpeakInitialPrompt(interrupt=true) clobbered the SAPI queue before
// Squall's line played, so Aaron never heard it. 3000 ms covers a
// 5-word line at any reasonable TTS rate.
static const DWORD TRIGGER_DELAY_MS = 3000;

// ff8_win_obj layout (from FFNx ff8.h, mirrors field_dialog.cpp).
static const size_t WIN_OBJ_SIZE                = 0x3C;
// v0.15.2.1: Geometry fields at the start of the struct. Captured
// values from a real engine ASK: 50 00 0A 00 CC 00 5D 00. v0.15.2
// left these as zero, which may have caused the engine to cull our
// slot from the render pass.
static const size_t WIN_OBJ_GEOM0_OFFSET        = 0x00;  // uint16 × 4: 0x0050, 0x000A, 0x00CC, 0x005D
static const size_t WIN_OBJ_TEXT1_OFFSET        = 0x08;
static const size_t WIN_OBJ_TEXT2_OFFSET        = 0x0C;
static const size_t WIN_OBJ_WINID_OFFSET        = 0x18;
static const size_t WIN_OBJ_MODE1_OFFSET        = 0x1A;
static const size_t WIN_OBJ_OPEN_CLOSE_OFFSET   = 0x1C;
static const size_t WIN_OBJ_STATE_OFFSET        = 0x24;
static const size_t WIN_OBJ_FIRST_Q_OFFSET      = 0x29;
static const size_t WIN_OBJ_LAST_Q_OFFSET       = 0x2A;
static const size_t WIN_OBJ_CUR_CHOICE_OFFSET   = 0x2B;
static const size_t WIN_OBJ_FIELD30_OFFSET      = 0x30;
static const size_t WIN_OBJ_CALLBACK1_OFFSET    = 0x34;
static const size_t WIN_OBJ_CALLBACK2_OFFSET    = 0x38;
static const int    MAX_WINDOWS                 = 8;

// Engine-window template values, captured from a real engine AASK
// snapshot via chase_diag::OnAskOpcodeFired in v0.15.1.2 BAT.
// Captured slot[0] when Aaron triggered an in-game ASK:
//   state    = 0x0000000D     (was guessing 0x07)
//   mode1    = 0x00001000      (was guessing 0x0005)
//   trans    = 0x00001000      (open_close_transition; was 0)
//   cb1, cb2 = 0x00000000      (NULL! engine doesn't use these for ASK)
// v0.15.1's lockup root cause: setting cb1/cb2 to non-NULL function
// pointers caused the engine to dispatch to them every frame at
// ~180Hz. Real ASKs have NULL callbacks at +0x34 / +0x38.
static const uint16_t TEMPLATE_MODE1 = 0x1000;
static const uint32_t TEMPLATE_STATE = 0x0000000D;
static const int16_t  TEMPLATE_TRANS = 0x1000;

// v0.15.2.1: Geometry values captured from a real engine ASK.
// Bytes 0x00-0x07: 50 00 0A 00 CC 00 5D 00 (4 × uint16 LE).
// Likely x, y, w, h or similar dialog box geometry. v0.15.2 left
// these as zero, which may have caused the engine to cull our slot.
static const uint16_t TEMPLATE_GEOM[4] = { 0x0050, 0x000A, 0x00CC, 0x005D };

// Choice indices for the ASK.
enum AskChoice {
    CHOICE_AUTO_DRIVE = 0,
    CHOICE_MANUAL     = 1,
    NUM_CHOICES       = 2,
};

static const char* CHOICE_LABELS[NUM_CHOICES] = {
    "Auto-drive",
    "Manual",
};

// ============================================================================
// State
// ============================================================================

static bool s_initialized = false;

// Once-per-chase trigger flag. Set when ASK fires; cleared when
// ChaseDetector::IsChaseActive() flips back to false (chase-end).
static bool s_askFiredThisChase = false;
// Tracks the chase-active edge so we know when to clear the fired flag.
static bool s_lastChaseActive = false;

// v0.15.1.2: Deferred-open state. OnDialogText sets s_triggerPending
// when it detects the chase-trigger MES but does NOT call OpenAsk
// synchronously. Update() polls and calls OpenAsk after
// TRIGGER_DELAY_MS has elapsed, so Squall's line plays through first.
static bool  s_triggerPending   = false;
static DWORD s_triggerTimestamp = 0;

// ASK overlay state.
static bool      s_askOpen          = false;
static int       s_currentHighlight = CHOICE_MANUAL;  // default to safe option
static int       s_proxySlotIdx     = -1;             // pWindowsArray slot we
                                                      //   allocated, or -1 if
                                                      //   we couldn't grab one
// Encoded option strings for the proxy window. Stored static so the
// engine has a stable buffer to read while the window is open.
static uint8_t   s_encodedOptionsBuf[256] = {};

// Edge-detection state for the keyboard handler.
static bool s_upWasDown    = false;
static bool s_downWasDown  = false;
static bool s_enterWasDown = false;
static bool s_oneWasDown   = false;
static bool s_twoWasDown   = false;

// ============================================================================
// FF8 text encoding (UTF-8 ASCII subset -> FF8 char codes)
//
// Mirror of the decoder table at the top of ff8_text_decode.cpp.
// The proxy window's text buffer is read by the engine as FF8-encoded
// bytes; we convert our ASCII strings on the fly.
//
// FF8 char codes (from ff8_text_decode.cpp):
//   0x20-0x39: A-Z   (offset 0x20 = 'A')  -- WAIT, double-check
//   We replicate just the printable subset we need: A-Z, a-z, 0-9,
//   space, period, comma, apostrophe, dash, newline (0x01 = newline
//   per FFNx convention).
// ============================================================================

static uint8_t EncodeChar(char c)
{
    // Per ff8_text_decode.cpp's character table:
    //   ' ' (space) = 0x00 in many contexts, but we use 0x20 for visible.
    //   For dialog text, FF8 uses different mappings; we approximate
    //   the most-common subset. If the engine renders garbage, we
    //   tune in v0.15.2 with the captured snapshot data.
    //
    // This encoder is a pragmatic best-effort. It may produce slightly
    // wrong output if the engine's table differs in this build; the
    // TTS + keyboard fallback path always speaks the right text via
    // ScreenReader, so the user experience is correct regardless.
    if (c >= 'A' && c <= 'Z') return (uint8_t)(c - 'A' + 0x20);
    if (c >= 'a' && c <= 'z') return (uint8_t)(c - 'a' + 0x3A);
    if (c >= '0' && c <= '9') return (uint8_t)(c - '0' + 0x54);
    if (c == ' ')             return 0x6E;   // approximate
    if (c == '.')             return 0x6F;
    if (c == ',')             return 0x70;
    if (c == '\'')            return 0x71;
    if (c == '-')             return 0x72;
    if (c == '\n')            return 0x01;   // newline (ASK option separator)
    if (c == '?')             return 0x73;
    return 0x6E;  // fallback to space-ish glyph
}

// Encode "Chase mode?\nAuto-drive\nManual\0" into s_encodedOptionsBuf.
static void EncodeAskOptions()
{
    static const char* kPlain = "Chase mode?\nAuto-drive\nManual";
    size_t out = 0;
    for (size_t i = 0; kPlain[i] != '\0' && out < sizeof(s_encodedOptionsBuf) - 1; i++) {
        s_encodedOptionsBuf[out++] = EncodeChar(kPlain[i]);
    }
    s_encodedOptionsBuf[out] = 0x00;  // FF8 string terminator
}

// ============================================================================
// Proxy window slot allocation
//
// Walk pWindowsArray looking for a slot whose state field is 0 (free).
// If we find one, populate it with our ASK and return its index. If
// no free slot is available, return -1; the TTS + keyboard fallback
// will handle the choice without an engine-rendered window.
// ============================================================================

static uint8_t* GetWindowObj(int idx)
{
    if (!FF8Addresses::pWindowsArray) return nullptr;
    return FF8Addresses::pWindowsArray + (idx * WIN_OBJ_SIZE);
}

static int FindFreeWindowSlot()
{
    if (!FF8Addresses::pWindowsArray) return -1;
    for (int i = 0; i < MAX_WINDOWS; i++) {
        uint8_t* w = GetWindowObj(i);
        __try {
            uint32_t st = *(uint32_t*)(w + WIN_OBJ_STATE_OFFSET);
            if (st == 0) return i;
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            // Slot read failed; skip.
        }
    }
    return -1;
}

// Confirm callback registered at +0x34. The engine calls it when the
// player picks an option in the rendered window. We read which option
// is highlighted from the win_obj's current_choice_question field.
//
// Stub for v0.15.1: signature follows the engine convention (single
// int parameter, returns int). Real signature TBD via runtime
// observation; if the engine never invokes our callback (because we
// haven't wired it correctly), the TTS + keyboard fallback handles
// the choice instead.
static int __cdecl ConfirmCallback(int /*arg*/)
{
    // We can't safely act on engine-callback choice in v0.15.1 because
    // we haven't validated the calling convention. Log the call and
    // let the TTS path own the actual choice.
    Log::Field("ChaseAskOverlay: ConfirmCallback fired (engine path);"
               " TTS path retains ownership of the actual choice");
    return 0;
}

static int __cdecl CancelCallback(int /*arg*/)
{
    Log::Field("ChaseAskOverlay: CancelCallback fired (engine path);"
               " ignored — chase ASK has no cancel option");
    return 0;
}

// Populate the slot with ASK fields. Best-effort — if any write fails
// under SEH the slot is abandoned and we fall through to the TTS path.
//
// v0.15.2: template values updated from captured engine ASK snapshot.
// mode1, state, trans now match what the engine puts in real ASK
// slots; callbacks are NULL (the engine doesn't read +0x34 / +0x38
// for ASK windows — v0.15.1's non-NULL callbacks caused the 180Hz
// dispatch loop). Both text1 and text2 point to our encoded buffer
// (real engine ASKs have both populated, ~75 bytes apart, suggesting
// the engine reads either depending on render path).
static bool PopulateProxySlot(int slotIdx)
{
    uint8_t* w = GetWindowObj(slotIdx);
    if (!w) return false;

    EncodeAskOptions();
    char* textBuf = reinterpret_cast<char*>(s_encodedOptionsBuf);

    __try {
        // v0.15.2.1: Geometry writes — captured from a real engine ASK.
        // v0.15.2 left these as zero which may have hidden the slot
        // from the render pass.
        uint16_t* geom = (uint16_t*)(w + WIN_OBJ_GEOM0_OFFSET);
        geom[0] = TEMPLATE_GEOM[0];
        geom[1] = TEMPLATE_GEOM[1];
        geom[2] = TEMPLATE_GEOM[2];
        geom[3] = TEMPLATE_GEOM[3];

        // v0.15.2: both text pointers populated (was text2=nullptr).
        *(char**)(w + WIN_OBJ_TEXT1_OFFSET) = textBuf;
        *(char**)(w + WIN_OBJ_TEXT2_OFFSET) = textBuf;

        // Window identity / template values.
        *(uint8_t*)(w + WIN_OBJ_WINID_OFFSET)        = (uint8_t)slotIdx;
        *(uint16_t*)(w + WIN_OBJ_MODE1_OFFSET)       = TEMPLATE_MODE1;
        // v0.15.2: open_close_transition = TEMPLATE_TRANS (0x1000) per
        // captured snapshot. v0.15.1's value of 0 (closed) probably
        // hid the window even after state went to 0x0D.
        *(int16_t*)(w + WIN_OBJ_OPEN_CLOSE_OFFSET)   = TEMPLATE_TRANS;
        *(uint32_t*)(w + WIN_OBJ_STATE_OFFSET)       = TEMPLATE_STATE;

        // ASK choice fields.
        *(uint8_t*)(w + WIN_OBJ_FIRST_Q_OFFSET)      = 1;
        *(uint8_t*)(w + WIN_OBJ_LAST_Q_OFFSET)       = (uint8_t)NUM_CHOICES;
        *(uint8_t*)(w + WIN_OBJ_CUR_CHOICE_OFFSET)   =
            (uint8_t)(s_currentHighlight + 1);  // 1-based

        // field_30 (battle/tuto dialog id) — not relevant for field ASK.
        *(uint16_t*)(w + WIN_OBJ_FIELD30_OFFSET)     = 0;

        // v0.15.2: Callbacks NULL. Real engine ASKs have NULL at
        // +0x34 and +0x38 (confirmed by captured snapshot). Setting
        // these to function pointers in v0.15.1 was the root cause
        // of the 180Hz dispatch loop — the engine apparently reads
        // these fields for some other dispatch purpose, not ASK
        // confirm/cancel handlers. Our PollKeys handles input.
        *(uint32_t*)(w + WIN_OBJ_CALLBACK1_OFFSET)   = 0;
        *(uint32_t*)(w + WIN_OBJ_CALLBACK2_OFFSET)   = 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        Log::Field("ChaseAskOverlay: SEH while populating slot %d", slotIdx);
        return false;
    }

    Log::Field("ChaseAskOverlay: proxy slot %d populated v0.15.2.1 "
               "(textBuf=0x%08X, mode1=0x%04X, state=0x%08X, trans=0x%04X, "
               "firstQ=1, lastQ=%d, curQ=%d, geom=[0x%04X 0x%04X 0x%04X 0x%04X], "
               "callbacks=NULL)",
               slotIdx, (uint32_t)(uintptr_t)textBuf,
               (unsigned)TEMPLATE_MODE1, (unsigned)TEMPLATE_STATE,
               (unsigned)TEMPLATE_TRANS, NUM_CHOICES, s_currentHighlight + 1,
               TEMPLATE_GEOM[0], TEMPLATE_GEOM[1],
               TEMPLATE_GEOM[2], TEMPLATE_GEOM[3]);
    return true;
}

// v0.15.2.1: Diagnostic helper — dump the current state of our proxy
// slot to the field log. Used to detect engine modifications to our
// writes (e.g., engine resetting state to 0 because it doesn't
// recognize the slot as valid). Called from Update() while the ASK
// is open, at 1Hz so we don't spam the log.
static DWORD s_lastProxySnapshotTick = 0;
static void LogProxySlotSnapshot(const char* label)
{
    if (s_proxySlotIdx < 0) return;
    uint8_t* w = GetWindowObj(s_proxySlotIdx);
    if (!w) return;
    __try {
        uint16_t g0  = *(uint16_t*)(w + 0x00);
        uint16_t g1  = *(uint16_t*)(w + 0x02);
        uint16_t g2  = *(uint16_t*)(w + 0x04);
        uint16_t g3  = *(uint16_t*)(w + 0x06);
        uint32_t t1p = *(uint32_t*)(w + WIN_OBJ_TEXT1_OFFSET);
        uint32_t t2p = *(uint32_t*)(w + WIN_OBJ_TEXT2_OFFSET);
        uint8_t  wid = *(uint8_t*)(w + WIN_OBJ_WINID_OFFSET);
        uint16_t m1  = *(uint16_t*)(w + WIN_OBJ_MODE1_OFFSET);
        int16_t  tr  = *(int16_t*)(w + WIN_OBJ_OPEN_CLOSE_OFFSET);
        uint32_t st  = *(uint32_t*)(w + WIN_OBJ_STATE_OFFSET);
        uint8_t  fq  = *(uint8_t*)(w + WIN_OBJ_FIRST_Q_OFFSET);
        uint8_t  lq  = *(uint8_t*)(w + WIN_OBJ_LAST_Q_OFFSET);
        uint8_t  cq  = *(uint8_t*)(w + WIN_OBJ_CUR_CHOICE_OFFSET);
        uint32_t cb1 = *(uint32_t*)(w + WIN_OBJ_CALLBACK1_OFFSET);
        uint32_t cb2 = *(uint32_t*)(w + WIN_OBJ_CALLBACK2_OFFSET);
        Log::Field("ChaseAskOverlay: [SLOT-SNAP %s] slot=%d "
                   "geom=[0x%04X 0x%04X 0x%04X 0x%04X] t1=0x%08X t2=0x%08X "
                   "winId=%u mode1=0x%04X trans=0x%04X state=0x%08X "
                   "firstQ=%u lastQ=%u curQ=%u cb1=0x%08X cb2=0x%08X",
                   label, s_proxySlotIdx,
                   g0, g1, g2, g3, t1p, t2p, wid, m1, (unsigned short)tr, st,
                   fq, lq, cq, cb1, cb2);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        Log::Field("ChaseAskOverlay: [SLOT-SNAP %s] SEH while reading slot %d",
                   label, s_proxySlotIdx);
    }
}

static void ReleaseProxySlot()
{
    if (s_proxySlotIdx < 0) return;
    uint8_t* w = GetWindowObj(s_proxySlotIdx);
    if (!w) { s_proxySlotIdx = -1; return; }
    __try {
        // Clear state and text pointers — make the slot look free again.
        *(uint32_t*)(w + WIN_OBJ_STATE_OFFSET)     = 0;
        *(int16_t*)(w + WIN_OBJ_OPEN_CLOSE_OFFSET) = 0;
        *(char**)(w + WIN_OBJ_TEXT1_OFFSET)        = nullptr;
        *(char**)(w + WIN_OBJ_TEXT2_OFFSET)        = nullptr;
        *(uint32_t*)(w + WIN_OBJ_CALLBACK1_OFFSET) = 0;
        *(uint32_t*)(w + WIN_OBJ_CALLBACK2_OFFSET) = 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) { /* nothing we can do */ }
    Log::Field("ChaseAskOverlay: released proxy slot %d", s_proxySlotIdx);
    s_proxySlotIdx = -1;
}

// ============================================================================
// TTS path — speak the prompt + currently highlighted option
// ============================================================================

static void SpeakInitialPrompt()
{
    // Two-sentence prompt: prompt + how to use, then the current
    // highlight. Aaron uses NVDA + SAPI; SpeakChannel2 with interrupt
    // ensures the announce isn't queued behind the chase-trigger
    // dialog that just played.
    char buf[256];
    snprintf(buf, sizeof(buf),
             "Chase mode. Auto-drive or Manual. "
             "Use up and down to choose. Press Enter to confirm. "
             "Currently selected: %s.",
             CHOICE_LABELS[s_currentHighlight]);
    ScreenReader::Speak(buf, true);
    Log::Field("ChaseAskOverlay: spoke initial prompt; highlight=%s",
               CHOICE_LABELS[s_currentHighlight]);
}

static void SpeakHighlightChange()
{
    ScreenReader::Speak(CHOICE_LABELS[s_currentHighlight], true);
}

// ============================================================================
// Open / close / commit
// ============================================================================

static void OpenAsk()
{
    if (s_askOpen) return;
    s_askOpen = true;
    s_currentHighlight = (ChaseDetector::GetChaseMode() == ChaseDetector::MODE_AUTO)
                         ? CHOICE_AUTO_DRIVE : CHOICE_MANUAL;

    // v0.15.2: re-enable engine-rendered proxy window with corrected
    // template values from the v0.15.1.2 BAT snapshot capture. The
    // v0.15.1 lockup is fixed because callbacks are now NULL (real
    // engine ASKs have NULL at +0x34 / +0x38, not function pointers).
    // Hybrid input: engine renders + shows visual cursor for sighted
    // spectators; PollKeys still owns the actual input via writes to
    // the slot's curQ field. On commit, CommitChoice releases the
    // slot via state=0.
    // v0.15.2.2: Drop the engine-rendered proxy slot.
    //
    // v0.15.2.1 BAT proved decisively that the engine doesn't render
    // slots populated from outside the script VM. The SLOT-SNAP
    // diagnostic logged slot state every second for 13 seconds after
    // a fully-correct PopulateProxySlot (captured-from-real-ASK
    // template values: state=0x0D, mode1=0x1000, trans=0x1000, geom
    // 50-0A-CC-5D, t1=t2=non-NULL, callbacks=NULL, firstQ/lastQ/curQ
    // matching a real ASK). Every single SLOT-SNAP showed our
    // writes preserved EXACTLY — the engine never touched the slot
    // until the next field-to-battle transition wiped it. Conclusion:
    // the engine's renderer doesn't iterate pWindowsArray looking for
    // active dialogs. Rendering is bound to script-VM context — only
    // slots that an active script has parked on via opcode_ask /
    // opcode_aask are rendered. Replicating that binding from a DLL
    // hook would require disassembly work to locate the script-VM's
    // 'current dialog slot' reference — a v0.15.3+ investigation,
    // outside the scope of v0.15.2.x. v0.15.2.x ships with the chase
    // ASK as TTS+keyboard only; the engine-rendered visual remains an
    // open feature gap to revisit when the script-VM mechanism is
    // understood, or when a different rendering path (e.g.
    // intercepting the renderer directly) becomes tractable.
    //
    // The five engine-ASK iterations (v0.15.0–0.15.2.1) are preserved
    // in CHANGELOG.md history. Helper functions FindFreeWindowSlot,
    // PopulateProxySlot, ReleaseProxySlot, LogProxySlotSnapshot,
    // SyncEngineCursor remain in source as unreferenced statics
    // (MSVC C4505 is off at default /W3, compiles clean) so a future
    // version can re-enable proxy-slot rendering without rewriting.
    s_proxySlotIdx = -1;
    Log::Field("ChaseAskOverlay: engine proxy slot disabled "
               "(v0.15.2.2 — visual ASK deferred to v0.15.3+; "
               "chase ASK runs on TTS+keyboard for now)");

    SpeakInitialPrompt();
    Log::Field("ChaseAskOverlay: ASK opened (proxySlot=%d)", s_proxySlotIdx);
}

static void CloseAsk()
{
    if (!s_askOpen) return;
    ReleaseProxySlot();
    s_askOpen = false;
    Log::Field("ChaseAskOverlay: ASK closed");
}

static void CommitChoice(AskChoice choice)
{
    Log::Field("ChaseAskOverlay: committed choice = %s",
               CHOICE_LABELS[choice]);

    if (choice == CHOICE_AUTO_DRIVE) {
        // Auto-drive isn't shipped in v0.15.1 — fall back to manual
        // and tell the user.
        ScreenReader::Speak(
            "Auto-drive is not yet implemented. Falling back to manual.",
            true);
        ChaseDetector::SetChaseMode(ChaseDetector::MODE_MANUAL);
    } else {
        ScreenReader::Speak(
            "Manual mode selected. One battle per chase field will be allowed.",
            true);
        ChaseDetector::SetChaseMode(ChaseDetector::MODE_MANUAL);
    }

    s_askFiredThisChase = true;
    CloseAsk();
}

// ============================================================================
// Keyboard polling (TTS + keyboard fallback path)
// ============================================================================

// v0.15.2: keep engine's visual cursor in sync with our highlight
// state. When PollKeys updates s_currentHighlight, also write the
// 1-based curQ value to the engine's slot so the rendered cursor
// tracks the selection. SEH-guarded against pWindowsArray races.
static void SyncEngineCursor()
{
    if (s_proxySlotIdx < 0) return;
    uint8_t* w = GetWindowObj(s_proxySlotIdx);
    if (!w) return;
    __try {
        *(uint8_t*)(w + WIN_OBJ_CUR_CHOICE_OFFSET) =
            (uint8_t)(s_currentHighlight + 1);
    } __except (EXCEPTION_EXECUTE_HANDLER) { /* nothing */ }
}

static void PollKeys()
{
    if (!s_askOpen) {
        // Keep edge state in sync so a stuck-down key from before the
        // ASK opened doesn't fire a spurious event the moment the ASK
        // appears.
        s_upWasDown    = (GetAsyncKeyState(VK_UP)     & 0x8000) != 0;
        s_downWasDown  = (GetAsyncKeyState(VK_DOWN)   & 0x8000) != 0;
        s_enterWasDown = (GetAsyncKeyState(VK_RETURN) & 0x8000) != 0;
        s_oneWasDown   = (GetAsyncKeyState('1')        & 0x8000) != 0;
        s_twoWasDown   = (GetAsyncKeyState('2')        & 0x8000) != 0;
        return;
    }

    // Suppress accelerators while Alt is held (mirror of the v0.14.105
    // F-key Alt-gate; keeps Alt+F4 close from firing our handlers).
    bool alt = (GetAsyncKeyState(VK_MENU) & 0x8000) != 0;

    bool up    = (GetAsyncKeyState(VK_UP)     & 0x8000) != 0;
    bool down  = (GetAsyncKeyState(VK_DOWN)   & 0x8000) != 0;
    bool enter = (GetAsyncKeyState(VK_RETURN) & 0x8000) != 0;
    bool one   = (GetAsyncKeyState('1')        & 0x8000) != 0;
    bool two   = (GetAsyncKeyState('2')        & 0x8000) != 0;

    if (!alt && up && !s_upWasDown) {
        // Cycle highlight up — wraps.
        s_currentHighlight = (s_currentHighlight + NUM_CHOICES - 1) % NUM_CHOICES;
        SyncEngineCursor();  // v0.15.2: visual cursor follows our state
        SpeakHighlightChange();
    }
    if (!alt && down && !s_downWasDown) {
        s_currentHighlight = (s_currentHighlight + 1) % NUM_CHOICES;
        SyncEngineCursor();  // v0.15.2: visual cursor follows our state
        SpeakHighlightChange();
    }
    if (!alt && enter && !s_enterWasDown) {
        CommitChoice((AskChoice)s_currentHighlight);
    }
    // Number-key shortcuts as a courtesy — Aaron can pick directly.
    if (!alt && one && !s_oneWasDown) {
        s_currentHighlight = CHOICE_AUTO_DRIVE;
        CommitChoice(CHOICE_AUTO_DRIVE);
    }
    if (!alt && two && !s_twoWasDown) {
        s_currentHighlight = CHOICE_MANUAL;
        CommitChoice(CHOICE_MANUAL);
    }

    s_upWasDown    = up;
    s_downWasDown  = down;
    s_enterWasDown = enter;
    s_oneWasDown   = one;
    s_twoWasDown   = two;
}

// ============================================================================
// Public API
// ============================================================================

void Initialize()
{
    if (s_initialized) return;
    s_initialized = true;
    s_askFiredThisChase = false;
    s_lastChaseActive   = false;
    s_askOpen           = false;
    s_proxySlotIdx      = -1;
    s_currentHighlight  = CHOICE_MANUAL;
    s_triggerPending    = false;  // v0.15.1.2
    s_triggerTimestamp  = 0;       // v0.15.1.2
    Log::Mod("ChaseAskOverlay: Initialized.");
}

void Shutdown()
{
    if (!s_initialized) return;
    if (s_askOpen) CloseAsk();
    s_initialized = false;
}

void Update()
{
    if (!s_initialized) return;

    // Reset the once-per-chase flag when chase ends.
    bool chaseActiveNow = ChaseDetector::IsChaseActive();
    if (s_lastChaseActive && !chaseActiveNow) {
        if (s_askFiredThisChase) {
            Log::Field("ChaseAskOverlay: chase ended; clearing fired flag");
        }
        s_askFiredThisChase = false;
        // v0.15.1.2: also clear deferred-pending if chase ended during
        // the delay window (rare, but covers the case where Aaron
        // dies/loads or the chase scene gets cancelled).
        if (s_triggerPending) {
            Log::Field("ChaseAskOverlay: chase ended during deferred-open "
                       "window; cancelling pending open");
            s_triggerPending = false;
        }
        // If the ASK was somehow still open at chase end (player skipped
        // dialog?), close it so we don't leave an orphan slot.
        if (s_askOpen) CloseAsk();
    }
    s_lastChaseActive = chaseActiveNow;

    // v0.15.1.2: Deferred-open trigger. If OnDialogText set
    // s_triggerPending and the delay has elapsed, open the ASK now.
    // Re-check chase state in case Aaron exited the chase during the
    // delay (extremely unlikely with a 3s window, but cheap to verify).
    if (s_triggerPending && GetTickCount() >= s_triggerTimestamp) {
        s_triggerPending = false;
        if (!s_askFiredThisChase && ChaseDetector::IsInChaseField()) {
            Log::Field("ChaseAskOverlay: deferred-open timer expired; "
                       "opening ASK now");
            OpenAsk();
        } else {
            Log::Field("ChaseAskOverlay: deferred-open aborted "
                       "(askFired=%d inChase=%d)",
                       (int)s_askFiredThisChase,
                       (int)ChaseDetector::IsInChaseField());
        }
    }

    PollKeys();

    // v0.15.2.1: While the ASK is open, periodically log the proxy
    // slot state so we can see what the engine modifies. 1 Hz to
    // avoid log spam. Helps diagnose 'engine doesn't render our
    // populated slot' — if state goes to 0 or fields get reset, the
    // engine is touching our slot. If state stays at 0x0D, the
    // engine is ignoring us entirely (our slot lacks something it
    // needs to be a render target).
    if (s_askOpen && s_proxySlotIdx >= 0) {
        DWORD now = GetTickCount();
        if (now - s_lastProxySnapshotTick >= 1000) {
            LogProxySlotSnapshot("while-open");
            s_lastProxySnapshotTick = now;
        }
    }
}

void OnDialogText(const char* text)
{
    if (!s_initialized || !text) return;
    if (s_askFiredThisChase) return;
    if (s_triggerPending) return;  // v0.15.1.2: already deferred-pending
    if (!ChaseDetector::IsInChaseField()) return;

    // Cheap strncmp filter — only act on the exact chase-trigger text.
    if (strstr(text, CHASE_TRIGGER_TEXT) == nullptr) return;

    // v0.15.1.2: defer the open by TRIGGER_DELAY_MS so field_dialog's
    // own speak path can announce Squall's line first. Setting the
    // pending flag here — Update() polls and fires OpenAsk when the
    // timer expires.
    s_triggerPending   = true;
    s_triggerTimestamp = GetTickCount() + TRIGGER_DELAY_MS;
    Log::Field("ChaseAskOverlay: chase trigger MES detected: \"%s\"; "
               "deferring ASK open by %u ms so Squall's line plays first",
               text, (unsigned)TRIGGER_DELAY_MS);
}

bool IsAskActive()
{
    return s_initialized && s_askOpen;
}

}  // namespace ChaseAskOverlay
