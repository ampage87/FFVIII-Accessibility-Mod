// chase_ask_overlay.cpp — Chase entry ASK overlay (manual / auto / original).
// See chase_ask_overlay.h for the public design notes.
//
// v0.15.1: New module. Triggered by Squall's "Forget it!  Let's go!" MES
// in any chase field (per v0.15.0 BAT confirmation — that's the exact
// chase-start dialog, NOT the "Run!" line that the deep research
// suggested). The trigger mechanism is the show_dialog hook in
// field_dialog.cpp, which calls our OnDialogText() on every decoded
// field dialog text.
//
// v0.15.1.2: Defer the open by 3000 ms so Squall's chase-trigger line
// plays first.
//
// v0.15.2.x: Several iterations attempted engine-rendered proxy slots.
// v0.15.2.1 BAT proved the engine doesn't render slots populated from
// outside the script-VM; rendering is bound to script-VM context.
// v0.15.2.2 abandoned the proxy and shipped TTS-only (no in-game
// dialog box).
//
// v0.15.8: Wired into DialogInject's OpenAsk pipeline (proven in
// v0.15.6.2 + v0.15.7.1 BAT). The chase ASK now renders as a real
// engine dialog with the player using FF8's natural arrow + X (confirm)
// keys to select. DialogInject owns rendering, cursor input, and answer
// detection. chase_ask_overlay's job shrinks to:
//   - Match the trigger MES, defer-open by 3s.
//   - Call DialogInject::OpenAsk("Mode?", {"Manual","Auto","Original"},...).
//   - Per-frame poll DialogInject::GetLastAnswer() for the user's choice.
//   - Dispatch to ChaseDetector::SetChaseMode based on the answer.
//
// All three answers currently route to MODE_MANUAL since Auto and
// Original aren't implemented yet (per Aaron's v0.15.8 plan). They
// announce a placeholder so it's clear which one was chosen and that
// fallback is happening.
//
// Squall and party still walk during the open ASK -- known limitation
// from v0.15.7.1, deferred to a later version. The dialog wiring works;
// input gating is a separate problem to solve.

#include "chase_ask_overlay.h"
#include "chase_detector.h"
#include "dialog_inject.h"
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
// before we preempt with our own prompt. 3000 ms covers a 5-word line
// at any reasonable TTS rate.
static const DWORD TRIGGER_DELAY_MS = 3000;

// v0.15.8: target slot for our injected ASK. Slot 2 is what
// Phase2_TestAsk uses; matches the v0.15.6.2 and v0.15.7.1 BAT layouts.
// Slots 0 and 1 are reserved for the engine's main dialog flow.
static const int CHASE_ASK_SLOT = 2;

// v0.15.8: choice list for the chase mode prompt. Three options per
// Aaron's v0.15.8 plan; all currently route to MODE_MANUAL since Auto
// (v0.15.9) and Original (v0.15.10) aren't implemented yet.
//
// v0.15.8.1: choice labels include brief descriptions so the engine
// dialog renders them and FieldDialog's [ASK] hook + DialogInject's
// cursor-change announcer speak the description naturally during
// navigation. Manual states the implemented behavior; Auto and Original
// honestly note they fall back to manual until v0.15.9 / v0.15.10
// implement them. The brief commit announces in CommitChoice ("Manual
// selected" etc.) replace the prior verbose mode-specific message.
static const char* kChaseChoices[] = {
    "Manual: one battle per field",
    "Auto: falls back to manual",
    "Original: falls back to manual"
};
static const int   kChaseChoicesCount = 3;
static const int   kChaseChoicesDefaultCursor = 1;  // Manual

// Answer values returned by DialogInject::GetLastAnswer (1-based).
static const int ANSWER_MANUAL   = 1;
static const int ANSWER_AUTO     = 2;
static const int ANSWER_ORIGINAL = 3;

// ============================================================================
// State
// ============================================================================

static bool s_initialized = false;

// Once-per-chase trigger flag. Set when ASK opens; cleared when
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

// ASK overlay state. s_askOpen flips true when DialogInject::OpenAsk
// returns true, and stays true until we observe a non--1
// GetLastAnswer() and dispatch the choice. IsAskActive() returns this.
static bool  s_askOpen          = false;

// ============================================================================
// Open / close / commit
// ============================================================================

// v0.15.8: hand off the open to DialogInject. Resets the answer slot
// first so a stale value from a previous test (e.g. Shift+F12) doesn't
// show up as an immediate commit. On success, mark the ASK open and
// once-fired; Update() will poll for the answer and dispatch.
static void OpenAsk()
{
    if (s_askOpen) return;

    DialogInject::ResetLastAnswer();
    // v0.15.8.1: pass announceCommit=false so DialogInject doesn't speak
    // "You chose <name>" on commit. CommitChoice below speaks a brief
    // mode-specific announce ("Manual selected" / "Automatic selected"
    // / "Original selected") instead -- single short line so the chase
    // resumes promptly without TTS still rattling on.
    bool ok = DialogInject::OpenAsk("Mode?",
                                    kChaseChoices,
                                    kChaseChoicesCount,
                                    kChaseChoicesDefaultCursor,
                                    CHASE_ASK_SLOT,
                                    /*announceCommit=*/false);
    if (!ok) {
        Log::Field("ChaseAskOverlay: DialogInject::OpenAsk returned false; "
                   "skipping chase mode prompt this run");
        // Don't set askFired -- a future trigger MES (e.g. on retry after
        // a death/load) can try again. The slot may have been busy.
        return;
    }
    s_askOpen = true;
    s_askFiredThisChase = true;
    Log::Field("ChaseAskOverlay: chase ASK opened via DialogInject "
               "(slot=%d, %d choices, default cursor=%d)",
               CHASE_ASK_SLOT, kChaseChoicesCount, kChaseChoicesDefaultCursor);
}

static void CloseAsk()
{
    if (!s_askOpen) return;
    s_askOpen = false;
    Log::Field("ChaseAskOverlay: chase ASK closed");
}

// v0.15.8: dispatch the user's answer. answer is 1-based per DialogInject
// (1=Manual, 2=Auto, 3=Original). All three currently route to
// MODE_MANUAL with a mode-specific announcement so it's clear which
// option was chosen and (for Auto/Original) that the implementation
// isn't complete yet.
//
// v0.15.8.1: trimmed to brief single-line announces ("Manual selected"
// / "Automatic selected" / "Original selected") per Aaron's UX feedback
// after v0.15.8 BAT. The verbose multi-clause messages were stacking on
// top of DialogInject's "You chose <name>" commit announce, leaving TTS
// still talking when the chase resumed. DialogInject's commit announce
// is now suppressed via OpenAsk's announceCommit=false; the choice
// labels themselves carry the description (read on cursor change /
// dialog-open ASK announce). Auto and Original say "selected" rather
// than reannouncing "falls back to manual" -- the dialog label already
// said it during navigation.
static void CommitChoice(int answer)
{
    const char* label = (answer >= 1 && answer <= kChaseChoicesCount)
                        ? kChaseChoices[answer - 1] : "?";
    Log::Field("ChaseAskOverlay: committed choice = %d (%s)", answer, label);

    switch (answer) {
        case ANSWER_MANUAL:
            ScreenReader::Speak("Manual selected", false);
            ChaseDetector::SetChaseMode(ChaseDetector::MODE_MANUAL);
            break;
        case ANSWER_AUTO:
            // v0.15.9: Auto now routes to MODE_AUTO instead of falling
            // back to MODE_MANUAL. ChaseAutoPilot picks this up via
            // ChaseDetector::GetChaseMode() and engages on each chase
            // field (currently configured: domt4_1 run-west, domt5_1
            // walk-south). chase_battle_freeze caps battles at 0 in
            // MODE_AUTO so any chase battle that does fire gets NO-OP'd.
            // Bridge (doopen2a) handling deferred to v0.15.9.1; player
            // drives the bridge manually in v0.15.9.
            ScreenReader::Speak("Automatic selected", false);
            ChaseDetector::SetChaseMode(ChaseDetector::MODE_AUTO);
            break;
        case ANSWER_ORIGINAL:
            // Original still falls back to MODE_MANUAL until v0.15.10
            // implements the chase-mod-active flag (vanilla chase
            // behavior with no battle cap).
            ScreenReader::Speak("Original selected", false);
            ChaseDetector::SetChaseMode(ChaseDetector::MODE_MANUAL);
            break;
        default:
            ScreenReader::Speak("Manual selected", false);
            ChaseDetector::SetChaseMode(ChaseDetector::MODE_MANUAL);
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
    s_initialized = true;
    s_askFiredThisChase = false;
    s_lastChaseActive   = false;
    s_askOpen           = false;
    s_triggerPending    = false;
    s_triggerTimestamp  = 0;
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
        // the delay window (rare).
        if (s_triggerPending) {
            Log::Field("ChaseAskOverlay: chase ended during deferred-open "
                       "window; cancelling pending open");
            s_triggerPending = false;
        }
        // If the ASK was somehow still open at chase end (player skipped
        // past the dialog?), close our shadow flag so we don't poll
        // forever. DialogInject's own state is independent and self-
        // disarming via its 60s timeout if no commit fires.
        if (s_askOpen) CloseAsk();
    }
    s_lastChaseActive = chaseActiveNow;

    // v0.15.1.2: Deferred-open trigger. If OnDialogText set
    // s_triggerPending and the delay has elapsed, open the ASK now.
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

    // v0.15.8: poll DialogInject for the committed answer.
    // GetLastAnswer() returns -1 until the user presses X (confirm) or
    // the 60s timeout fires. On non--1 we dispatch and close.
    if (s_askOpen) {
        int answer = DialogInject::GetLastAnswer();
        if (answer != -1) {
            Log::Field("ChaseAskOverlay: DialogInject::GetLastAnswer returned "
                       "%d; dispatching", answer);
            CommitChoice(answer);
        }
    }
}

void OnDialogText(const char* text)
{
    if (!s_initialized || !text) return;
    if (s_askFiredThisChase) return;
    if (s_triggerPending) return;  // v0.15.1.2: already deferred-pending
    if (!ChaseDetector::IsInChaseField()) return;

    // Cheap strstr filter — only act on the exact chase-trigger text.
    if (strstr(text, CHASE_TRIGGER_TEXT) == nullptr) return;

    // v0.15.1.2: defer the open by TRIGGER_DELAY_MS so field_dialog's
    // own speak path can announce Squall's line first.
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
