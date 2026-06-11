// field_dialog.cpp - Field dialog TTS via MinHook detour hooks
//
// ============================================================================
// CURRENT STATE: v0.16.2 -- field_dialog.cpp split into focused .inl files.
//
// The 88 KB / ~2000-line monolith of v0.16.1.4 has been carved into 8 files
// plus this slim parent. Pure mechanical split; no functional change.
//
//   - field_dialog_state.inl       typedefs, all static module state, struct
//                                  definitions (WindowState, PendingText),
//                                  window-object layout constants, FMV-poll
//                                  state, show_dialog dedup state, and the
//                                  MarkPendingAsSpoken forward declaration.
//   - field_dialog_helpers.inl     pointer validation (IsValidTextPointer,
//                                  ProbePointer, ProbeGetstrResult), window
//                                  accessors (GetWindowObj, GetWinText1/2,
//                                  GetWinOpenCloseTransition), text helpers
//                                  (TrimDecoded, IsSuffixOrSubstring,
//                                  fnv1a_prefix), CreateDetourHook.
//   - field_dialog_scan.inl        the central TTS-speak path:
//                                  ScanAndSpeakAllWindows,
//                                  ScanAndSpeakChoiceWindows,
//                                  MarkPendingAsSpoken, CheckPendingTexts.
//   - field_dialog_show_dialog.inl Hook_show_dialog -- the universal text-
//                                  renderer hook with OOR diagnostic, FNV-1a
//                                  hash dedup, scan-active suppression, and
//                                  battle drawer-name decoration.
//   - field_dialog_opcodes.inl     the dialog opcode hooks (mes/mesw/ask/
//                                  ames/aask/amesw) plus the diagnostic
//                                  opcode hooks (tuto/mesmode/ramesw),
//                                  Hook_field_get_dialog_string with the
//                                  DialogInject override path, and
//                                  RepeatLastDialog.
//   - field_dialog_diag.inl        dispatch instrumentation (DispatchStub,
//                                  DispatchStub_EDX, PatchDispatchSite,
//                                  UnpatchDispatchSite), naked counter hooks
//                                  (menu_draw_text, update_field_entities),
//                                  Hook_get_character_width + CheckGcwBuffer,
//                                  DiagRawWindowDump.
//   - field_dialog_menuname.inl    Hook_opcode_menuname -- the naming-screen
//                                  bypass with GF-diff-on-acquire detection.
//   - field_dialog_lifecycle.inl   Initialize, Shutdown, PollWindows. Last
//                                  in the include chain because it references
//                                  every hook function and the helpers from
//                                  all earlier .inls.
//
// This file holds the public-API thin shell: system includes, namespace
// forward declarations, the .inl chain in dependency order, and the small
// trailing public-API helpers (IsActive, IsDialogOpen, GetMenuDrawTextCallCount,
// GetGetCharWidthCallCount, SnapshotGcwBuffer). RepeatLastDialog lives in
// opcodes.inl. See field_dialog.h for the full public-API declaration.
// ============================================================================
//
// Version history (the field-dialog TTS evolution, retained for orientation):
//
// v04.00-diag2: MinHook detours on opcode handlers confirmed working.
// v04.01: Wire decoded dialog text to TTS. Deduplication. Pointer validation.
// v04.02: Fix choice dialogs -- parenthesis-based extraction.
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
// v04.14: Hook MESW (0x46) -- message + wait.
// v04.15: RAWDUMP diagnostic -- logs raw hex of ALL window slots every 2s
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
// v04.19: Enhanced show_dialog diagnostics -- track window_id distribution
//         including out-of-range IDs. Log OOR calls in detail. Confirmed
//         corridor thoughts do NOT use MODE_TUTO (stays MODE_FIELD=1).
// v04.20: Hook menu_draw_text and get_character_width. Confirmed both are
//         menu-system-only -- zero calls during field dialog or thoughts.
// v04.21: Hook opcode_mesmode (0x106) and opcode_ramesw (0x116). No calls
//         during thought gaps.
// v04.22: Hook update_field_entities + opcode dispatch instrumentation.
//         Confirmed script interpreter runs during gaps but zero dialog opcodes.
// v04.23: CRITICAL BUG FIXES from Plan Documents deep-research-report:
//         (a) Window struct stride 0x38 -> 0x3C (FFNx ff8.h confirms 0x3C).
//         (b) field_get_dialog_string signature: (int) -> (char*,int) per FFNx.
//         (c) show_dialog dedup: pointer-only -> FNV-1a hash (catches rewrites).
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
#include "field_dialog.h"    // v0.18.3.15: own header -- TrainGuardModeVal enum + Get/SetTrainGuardMode shared with FieldNavigation (#58)
#include "ff8_addresses.h"
#include "ff8_text_decode.h"
#include "config.h"          // v0.18.3.14: train_guard_mode (Manual freeze) read from INI (#58)
#include "battle_tts.h"       // v0.10.112: GetLastDrawerName() for draw result announcements
#include "field_archive.h"    // v0.18.3.2: DumpTrainCodeScripts() for the #56 JSM dump diagnostic
#include "minhook/include/MinHook.h"
#include <vector>

// Forward declarations for cross-module namespaces (restored in v0.14.28 build recovery).
namespace Log {
    void Dialog(const char* format, ...);
    void Write(const char* format, ...);
    void Field(const char* format, ...);   // v0.18.3.2: ff8_field.log sink, for TrainCodeJsmDump (#56)
}
namespace ScreenReader { bool Speak(const char* text, bool interrupt = false); }
// v0.14.63: ScanTTS::IsScreenActive() lets us know when the Scan UI window
// is currently open. We suppress the show_dialog speak path during that window
// (and only that window) because the rendered scan text duplicates the
// scan_tts.cpp auto-announce. All other battle UI (Cast Fire, mid-battle
// dialog, etc.) speaks normally through this hook.
namespace ScanTTS { bool IsScreenActive(); }

// v0.15.1: Chase scene wiring.
// - ChaseAskOverlay::OnDialogText is called from Hook_show_dialog for every
//   decoded text the engine renders. The overlay does a cheap strncmp filter
//   for Squall's chase-trigger MES ("Forget it!  Let's go!") and opens the
//   manual-vs-auto-drive ASK when matched.
// - ChaseDiag::OnAskOpcodeFired is called from Hook_opcode_ask and
//   Hook_opcode_aask. When chase-diag is enabled (F12), it snapshots all 8
//   pWindowsArray slots so chase_ask_overlay can mine real engine-set ASK
//   template values for v0.15.2 proxy-window tuning. No-op when disabled.
namespace ChaseAskOverlay { void OnDialogText(const char* text); }
namespace ChaseDiag       { void OnAskOpcodeFired(const char* opcodeLabel); }

// v0.15.6.1 Phase 2b: dialog_inject.cpp's text override coordination.
// When IsOverrideActive() returns true, our Hook_opcode_ask patches
// slot[GetOverrideSlot()]+0x08 (text_data1) with GetOverrideText() AFTER
// s_origAsk returns and BEFORE ScanAndSpeakChoiceWindows reads the slot.
// This puts our buffer in front of both TTS and the engine's render/input
// reads. The flag is set immediately before opcode_ask and cleared
// immediately after, so natural game ASKs are not affected.
//
// v0.15.6 originally proposed an override path through this file's existing
// Hook_field_get_dialog_string but FFNx's replace_call rewrote the engine's
// internal CALL field_get_dialog_string operand to point at FFNx's own
// function, leaving our hook on engine 0x00530750 dead. The v0.15.6 BAT
// log proves this: zero [GETSTR-RAW] lines despite the hook's unconditional
// first-10-calls logging. v0.15.6.1 moves the substitution to a point
// downstream of FFNx's bypass.
namespace DialogInject {
    bool        IsOverrideActive();
    const char* GetOverrideText();
    int         GetOverrideSlot();   // v0.15.6.1
    const unsigned char* GetOverrideBufferStart();   // v0.15.6.2
    unsigned int         GetOverrideBufferSize();    // v0.15.6.2
}

namespace FieldDialog {

// .inl include chain. ORDER MATTERS (forward-reference rules apply):
//   - state.inl first: declares types, structs, all module-static state,
//     and the MarkPendingAsSpoken forward decl.
//   - helpers.inl: pure utilities (pointer validation, window accessors,
//     trim/hash/suffix, CreateDetourHook). Uses only types from state.inl.
//   - scan.inl: defines MarkPendingAsSpoken plus the speak path
//     (ScanAndSpeakAllWindows, ScanAndSpeakChoiceWindows, CheckPendingTexts).
//     Calls into helpers.
//   - show_dialog.inl: defines Hook_show_dialog. Calls into scan.inl
//     (MarkPendingAsSpoken) and helpers.
//   - opcodes.inl: defines all opcode hooks (mes/mesw/ask/ames/aask/amesw,
//     field_get_dialog_string, tuto/mesmode/ramesw) plus RepeatLastDialog.
//     Calls into scan.inl.
//   - diag.inl: defines dispatch instrumentation (naked stubs + patch site),
//     naked counter hooks, GCW capture, DiagRawWindowDump. Uses helpers.
//   - menuname.inl: defines Hook_opcode_menuname. Isolated; uses only
//     ScreenReader + savemap reads.
//   - lifecycle.inl LAST: Initialize / Shutdown / PollWindows wire up every
//     hook function declared in earlier .inls.
#include "field_dialog_state.inl"
#include "field_dialog_helpers.inl"
#include "field_dialog_scan.inl"
#include "field_dialog_show_dialog.inl"
#include "field_dialog_opcodes.inl"
#include "field_dialog_diag.inl"
#include "field_dialog_menuname.inl"
#include "field_dialog_lifecycle.inl"

// ============================================================================
// Tiny public-API helpers -- kept in parent .cpp for visibility.
// Initialize, Shutdown, PollWindows live in lifecycle.inl above.
// RepeatLastDialog lives in opcodes.inl above.
// ============================================================================

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
// Safe to call from any thread -- int16/uint32 reads are atomic on x86.
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
