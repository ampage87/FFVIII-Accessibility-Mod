// train_mode_ask_overlay.cpp -- Timber train guard-mode ASK overlay (#60).
// See train_mode_ask_overlay.h for the design notes.
//
// Mirrors chase_ask_overlay.cpp: this overlay is fed every decoded field
// dialog line via OnDialogText (from field_dialog's show_dialog hook). When it
// sees the post-"Yeah" trigger line in the right field, it defers a
// DialogInject::OpenAsk and polls GetLastAnswer() for the player's choice,
// then dispatches to FieldDialog::SetTrainGuardMode().
//
// Trigger discovery (capture run, 2026-06-11, ff8_dialog.log + ff8_field.log):
//   - tiagit1 (field 892, Forest Owls' base room): Watts AASK
//       "Are you ready, sir!?"  choices "Not yet" / "Yeah".
//   - Selecting "Yeah" -> MAPJUMP3 -> tiyane1 (field 930, "Timber - Train 3").
//   - tiyane1 intro: AMESW  Rinoa "Squall, over here!".
// So "right after Yeah" == "on entry to tiyane1". We match the Rinoa line,
// gated to tiyane1, once per mission. The field gate makes the otherwise
// generic line safe. We re-arm whenever the player re-enters tiagit1.

// <windows.h> MUST come before field_dialog.h: that header declares a couple
// of accessors returning LONG (a Windows type) without pulling in windows.h
// itself, so it relies on the includer having defined it first.
#include <windows.h>
#include <cstdio>
#include <cstring>

#include "train_mode_ask_overlay.h"
#include "dialog_inject.h"
#include "field_dialog.h"
#include "ff8_accessibility.h"
#include "ff8_addresses.h"
#include "mod_forward_decls.h"

// v0.18.3.24: Skip-discovery diagnostic. While ON, snapshots the savemap at
// mission start (tiyane1) and again at the field a successful uncouple drops
// the player into, then logs the byte-level diff to ff8_field.log ([SKIP-CAP]).
// That diff is the persistent story/state delta the real Skip must replicate.
// Set to 0 once Skip is built. Logging-only; no engine writes.
#define TRAIN_SKIP_CAPTURE_DIAG 0

namespace TrainModeAskOverlay {

// ============================================================================
// Constants
// ============================================================================

// The line that plays the instant tiyane1 loads, right after "Yeah". The
// decoded text the engine renders is  Rinoa "Squall, over here!"  -- we match
// the inner substring so the speaker prefix / quote encoding don't matter.
static const char* TRIGGER_TEXT = "Squall, over here!";

// Gate the trigger to the first train-roof field so the (generic) line can't
// misfire elsewhere. tiyane1 = field 930.
static const char* TRIGGER_FIELD = "tiyane1";

// Re-arm the once-per-mission flag whenever the player is back in the briefing
// room (tiagit1 = field 892), so a reload / replay can prompt again.
static const char* REARM_FIELD = "tiagit1";

// Open on the next tick after the trigger (mirrors the chase ASK's 0 ms): the
// trigger line has already queued in TTS, and our prompt + default-option
// label queue right behind it. The default cursor is the safe option, so a
// player still pressing confirm from the "Yeah" answer lands on Manual.
static const DWORD TRIGGER_DELAY_MS = 0;

// If OpenAsk fails (slot momentarily busy at the trigger instant), retry this
// often while we're still in tiyane1 and haven't yet opened.
static const DWORD RETRY_MS = 200;

// Slot 2: same slot the chase ASK uses (the two never coexist). Slots 0 and 1
// are reserved for the engine's main dialog flow.
static const int ASK_SLOT = 2;

// Choice list, in display order; #1 is the default. The labels carry a brief
// description that DialogInject speaks on cursor change. Kept apostrophe-free
// (the dialog encoder's table is limited); the spoken commit messages below
// use SAPI, so those can be richer.
//   1 Manual -> guards move + code/proximity cues   (TGM_MANUAL = 0)
//   2 Freeze -> guards held, just enter codes        (TGM_FREEZE = 1)
//   3 Skip   -> warp to the post-mission room + replay the win's persistent
//              savemap delta (TGM_SKIP = 2, see ExecuteSkipBypass). The
//              replayed delta also lowers the SeeD rank by one -- kept as an
//              intentional, deterministic cost of skipping, surfaced in the label.
static const char* kChoices[] = {
    "Manual: guards patrol, codes and guard alerts are announced",
    "Freeze: guards held in place, just enter the codes",
    "Skip: jump to the end of the train mission, drops your SeeD rank by one"
};
static const int kChoicesCount  = 3;
static const int kDefaultCursor = 1;   // Manual

static const int ANSWER_MANUAL = 1;
static const int ANSWER_FREEZE = 2;
static const int ANSWER_SKIP   = 3;

// ============================================================================
// State
// ============================================================================
static bool  s_initialized      = false;
static bool  s_askFired         = false;   // once per mission
static bool  s_triggerPending   = false;
static DWORD s_triggerTimestamp = 0;
static bool  s_askOpen          = false;
static char  s_lastField[32]    = {0};     // for the tiagit1 re-arm edge
static bool  s_skipPending      = false;   // Skip chosen; execute warp next tick
static DWORD s_skipTimestamp    = 0;

static bool FieldIs(const char* name)
{
    const char* fn = FF8Addresses::pCurrentFieldName;
    return fn && name && _stricmp(fn, name) == 0;
}

// ============================================================================
// Skip-discovery capture (v0.18.3.24, behind TRAIN_SKIP_CAPTURE_DIAG)
// ============================================================================
#if TRAIN_SKIP_CAPTURE_DIAG

// Confirmed live savemap base (the SeeD-salary poll reads gil/points/steps from
// here). We snapshot a generous span that covers the known fields (steps 0xD64,
// points 0xD6C, gil 0xB08) plus headroom for the story/event flags.
static const uint32_t SKIP_SM_BASE  = 0x1CFDC5C;
static const int      SKIP_SNAP_LEN = 0x1000;   // 4 KB first pass; widen if needed

static unsigned char s_smBefore[SKIP_SNAP_LEN];
static unsigned char s_smAfter [SKIP_SNAP_LEN];
static bool s_capHaveBefore  = false;
static bool s_capVisitedLink = false;
static bool s_capHaveAfter   = false;

// Raw guarded copy -- no C++ objects in this function (SEH + std::string don't
// mix; C2712).
static void SnapSavemap(unsigned char* dst, int len)
{
    __try {
        memcpy(dst, (const void*)(uintptr_t)SKIP_SM_BASE, (size_t)len);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        memset(dst, 0, (size_t)len);
    }
}

static bool FieldStartsWith(const char* s, const char* pfx)
{
    return s && pfx && _strnicmp(s, pfx, (int)strlen(pfx)) == 0;
}

// Called on every field change (from Update). Captures BEFORE at the first
// tiyane1, tracks the tilink code cars, and captures AFTER + diffs at the first
// non-roof field once the code cars were visited (= the successful-uncouple
// destination).
static void MaybeCaptureSkipState(const char* fn)
{
    if (!fn) return;

    if (!s_capHaveBefore && _stricmp(fn, "tiyane1") == 0) {
        SnapSavemap(s_smBefore, SKIP_SNAP_LEN);
        s_capHaveBefore  = true;
        s_capVisitedLink = false;
        s_capHaveAfter   = false;
        Log::Field("[SKIP-CAP] BEFORE captured at tiyane1 (savemap 0x%08X, %d bytes)",
                   SKIP_SM_BASE, SKIP_SNAP_LEN);
        return;
    }

    if (FieldStartsWith(fn, "tilink")) {
        s_capVisitedLink = true;
        return;
    }

    if (s_capHaveBefore && s_capVisitedLink && !s_capHaveAfter &&
        !FieldStartsWith(fn, "tiyane") && !FieldStartsWith(fn, "tilink") &&
        !FieldStartsWith(fn, "titrain")) {
        SnapSavemap(s_smAfter, SKIP_SNAP_LEN);
        s_capHaveAfter = true;
        Log::Field("[SKIP-CAP] AFTER captured -- successful-uncouple return field = '%s'. "
                   "Diffing savemap vs the tiyane1 baseline...", fn);

        int  changed = 0;
        char line[256];
        int  pos = 0;
        line[0] = 0;
        for (int i = 0; i < SKIP_SNAP_LEN; i++) {
            if (s_smBefore[i] != s_smAfter[i]) {
                changed++;
                pos += sprintf_s(line + pos, sizeof(line) - pos,
                                 "0x%03X:%02X>%02X ", i, s_smBefore[i], s_smAfter[i]);
                if (pos > (int)sizeof(line) - 20) {
                    Log::Field("[SKIP-CAP]   %s", line);
                    pos = 0; line[0] = 0;
                }
                if (changed >= 400) break;   // safety cap against flooding
            }
        }
        if (pos > 0) Log::Field("[SKIP-CAP]   %s", line);
        Log::Field("[SKIP-CAP] %d changed byte(s) in [0x000..0x%X] from base 0x%08X. "
                   "Offsets are savemap-relative. Expect gametime/step-counter churn "
                   "(steps near 0xD64); the story flag is a small targeted change.",
                   changed, SKIP_SNAP_LEN - 1, SKIP_SM_BASE);
    }
}
#endif  // TRAIN_SKIP_CAPTURE_DIAG

// ============================================================================
// Skip bypass (v0.18.3.25)
// ============================================================================
// On Skip: reproduce the persistent savemap state a successful uncouple leaves
// behind, then request the same field transition the win fires (-> field 892,
// the Forest Owls' base room). Both the savemap delta and the transition values
// were captured from a real winning run (2026-06-11; [SKIP-CAP] + [MAPJUMP-HOOK]
// in ff8_field.log). No engine call -- the field loop polls the transition block.
static void ExecuteSkipBypass()
{
    // (1) Persistent savemap delta (base 0x01CFDC5C; offsets are savemap-relative).
    // These are the post-win byte values. The capture's step/clock counters are
    // intentionally omitted (the player didn't take those steps).
    struct DeltaByte { unsigned short off; unsigned char val; };
    static const DeltaByte kDelta[] = {
        {0x000,0x6A},
        {0xCCC,0x1E},{0xCCD,0xA2},{0xCD0,0x9D},{0xCD1,0x01},
        {0xDAC,0xD0},{0xDAD,0x01},{0xDB0,0x87},
        {0xE1F,0xFF},{0xE20,0xFF},{0xE23,0x23},{0xE25,0x00},{0xE29,0xDC},{0xE2B,0x01},
        {0xE31,0x23},{0xE34,0x00},{0xE36,0x00},{0xE38,0x3C},
        {0xE40,0xFF},{0xE42,0xFF},{0xE44,0xFF},{0xE50,0xA0},{0xE51,0x00},
    };
    const uintptr_t SM_BASE = 0x01CFDC5C;
    int wrote = 0;
    __try {
        for (size_t i = 0; i < sizeof(kDelta) / sizeof(kDelta[0]); i++) {
            *(volatile unsigned char*)(SM_BASE + kDelta[i].off) = kDelta[i].val;
            wrote++;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        Log::Field("TrainModeAskOverlay: [SKIP] savemap delta write FAILED after %d (code 0x%08X)",
                   wrote, GetExceptionCode());
    }

    // (2) Request the win's field transition. The engine's field loop polls this
    // block (documented in field_nav_mapjump_diag.inl) and performs the load.
    // Values are the exact ones the real MAPJUMP3 produced (destField 892, X 66,
    // Y 64760, Z 0, inline 62, top 128). transition_type is written LAST so the
    // loop never sees the request with a stale destination.
    __try {
        *(volatile unsigned short*)0x01CE4762 = 892;     // destField
        *(volatile unsigned short*)0x01CE4764 = 66;       // X
        *(volatile unsigned short*)0x01CE4766 = 64760;    // Y
        *(volatile unsigned short*)0x01CE4768 = 0;         // Z
        *(volatile unsigned short*)0x01CE476C = 62;        // inline_param
        *(volatile unsigned short*)0x01CE476E = 128;       // topOfStack
        *(volatile unsigned char*) 0x01CE4760 = 1;         // transition_type (arms it)
        Log::Field("TrainModeAskOverlay: [SKIP] %d delta bytes applied; warp to field 892 requested",
                   wrote);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        Log::Field("TrainModeAskOverlay: [SKIP] transition-block write FAILED (code 0x%08X)",
                   GetExceptionCode());
    }
}

// ============================================================================
// Open / close / commit
// ============================================================================

// Returns true if the ASK is now open. On failure (slot busy) returns false
// WITHOUT setting s_askFired, so Update() retries while still in tiyane1.
static bool OpenAsk()
{
    if (s_askOpen) return true;

    DialogInject::ResetLastAnswer();
    bool ok = DialogInject::OpenAsk(
        "How do you want to handle the train guards?",
        kChoices, kChoicesCount, kDefaultCursor, ASK_SLOT,
        /*announceCommit=*/false);   // CommitChoice speaks its own brief line
    if (!ok) {
        Log::Field("TrainModeAskOverlay: DialogInject::OpenAsk returned false "
                   "(slot busy?); will retry while in %s", TRIGGER_FIELD);
        return false;
    }
    s_askOpen  = true;
    s_askFired = true;
    Log::Field("TrainModeAskOverlay: mode ASK opened (slot=%d, %d choices, default=%d)",
               ASK_SLOT, kChoicesCount, kDefaultCursor);
    return true;
}

static void CloseAsk()
{
    if (!s_askOpen) return;
    s_askOpen = false;
    Log::Field("TrainModeAskOverlay: mode ASK closed");
}

// answer is 1-based per DialogInject.
static void CommitChoice(int answer)
{
    const char* label = (answer >= 1 && answer <= kChoicesCount) ? kChoices[answer - 1] : "?";
    Log::Field("TrainModeAskOverlay: committed choice = %d (%s)", answer, label);

    switch (answer) {
        case ANSWER_MANUAL:
            ScreenReader::Speak("Manual selected", false);
            FieldDialog::SetTrainGuardMode(FieldDialog::TGM_MANUAL);
            break;
        case ANSWER_FREEZE:
            ScreenReader::Speak("Freeze selected", false);
            FieldDialog::SetTrainGuardMode(FieldDialog::TGM_FREEZE);
            break;
        case ANSWER_SKIP:
            // Real bypass: warp to the post-mission room and replay the win's
            // persistent savemap state. Deferred one beat (s_skipPending) so the
            // ASK dialog fully closes before the field transition is requested.
            ScreenReader::Speak("Skipping to the end of the train mission.", false);
            FieldDialog::SetTrainGuardMode(FieldDialog::TGM_SKIP);
            s_skipPending   = true;
            s_skipTimestamp = GetTickCount() + 400;
            break;
        default:
            ScreenReader::Speak("Manual selected", false);
            FieldDialog::SetTrainGuardMode(FieldDialog::TGM_MANUAL);
            break;
    }
    CloseAsk();
}

// ============================================================================
// Public API
// ============================================================================

void Initialize()
{
    if (s_initialized) return;
    s_initialized      = true;
    s_askFired         = false;
    s_triggerPending   = false;
    s_triggerTimestamp = 0;
    s_askOpen          = false;
    s_lastField[0]     = 0;
    s_skipPending      = false;
    s_skipTimestamp    = 0;
    Log::Mod("TrainModeAskOverlay: Initialized.");
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

    // Re-arm the once-per-mission flag when the player (re-)enters the briefing
    // room, so a reload / replay prompts again. Edge-detected on the field name.
    const char* fn = FF8Addresses::pCurrentFieldName;
    if (fn && _strnicmp(fn, s_lastField, sizeof(s_lastField) - 1) != 0) {
#if TRAIN_SKIP_CAPTURE_DIAG
        MaybeCaptureSkipState(fn);
#endif
        if (_stricmp(fn, REARM_FIELD) == 0 && s_askFired) {
            Log::Field("TrainModeAskOverlay: back in %s; re-arming the mode ASK", REARM_FIELD);
            s_askFired = false;
        }
        sprintf_s(s_lastField, sizeof(s_lastField), "%.31s", fn);
    }

    // Deferred Skip bypass: fire a beat after the player chose Skip, once the
    // ASK has closed, so the warp + savemap writes don't race the dialog teardown.
    if (s_skipPending && GetTickCount() >= s_skipTimestamp) {
        s_skipPending = false;
        ExecuteSkipBypass();
    }

    // Deferred open (with retry while still in tiyane1).
    if (s_triggerPending && GetTickCount() >= s_triggerTimestamp) {
        if (!s_askFired && FieldIs(TRIGGER_FIELD)) {
            if (OpenAsk()) {
                s_triggerPending = false;
            } else {
                s_triggerTimestamp = GetTickCount() + RETRY_MS;   // try again shortly
            }
        } else {
            s_triggerPending = false;
            Log::Field("TrainModeAskOverlay: deferred-open aborted (fired=%d inField=%d)",
                       (int)s_askFired, (int)FieldIs(TRIGGER_FIELD));
        }
    }

    // If the scene advanced past tiyane1 with the ASK still open (the player
    // walked on without answering), let it go -- the default mode stands.
    if (s_askOpen && !FieldIs(TRIGGER_FIELD)) {
        Log::Field("TrainModeAskOverlay: left %s with the ASK still open; "
                   "closing it (the current mode stands)", TRIGGER_FIELD);
        CloseAsk();
    }

    // Poll DialogInject for the committed answer (-1 until X / confirm or the
    // 60 s timeout; otherwise 1-based).
    if (s_askOpen) {
        int answer = DialogInject::GetLastAnswer();
        if (answer != -1) {
            Log::Field("TrainModeAskOverlay: GetLastAnswer returned %d; dispatching", answer);
            CommitChoice(answer);
        }
    }
}

void OnDialogText(const char* text)
{
    if (!s_initialized || !text) return;
    if (s_askFired || s_triggerPending) return;
    if (!FieldIs(TRIGGER_FIELD)) return;
    if (strstr(text, TRIGGER_TEXT) == nullptr) return;

    s_triggerPending   = true;
    s_triggerTimestamp = GetTickCount() + TRIGGER_DELAY_MS;
    Log::Field("TrainModeAskOverlay: trigger line detected in %s: \"%s\"; "
               "opening the mode ASK", TRIGGER_FIELD, text);
}

bool IsAskActive()
{
    return s_initialized && s_askOpen;
}

}  // namespace TrainModeAskOverlay
