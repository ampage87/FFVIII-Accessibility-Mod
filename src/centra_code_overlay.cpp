// centra_code_overlay.cpp -- the Centra Ruins five-digit code panel (#centra).
// See centra_code_overlay.h for the design and centra_code_model.inl for the
// script transcript everything here rests on.
//
// <windows.h> MUST come before ff8_addresses.h: that header declares accessors
// returning Windows types without pulling windows.h in itself, so it relies on
// the includer having defined it first.
#include <windows.h>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "centra_code_overlay.h"
#include "ff8_accessibility.h"
#include "ff8_addresses.h"
#include "field_navigation.h"   // the Game Controls box, published in v0.106.0
#include "mod_forward_decls.h"

#include "centra_code_model.inl"

namespace CentraCodeOverlay {

// The eleven Centra Ruins fields are ids 276..286. The code is readable in all
// of them -- var[364..368] is set before the player ever reaches the roof -- so
// the repeat key works anywhere in the ruins rather than only at the panel.
static const uint16_t CC_FIELD_FIRST = 276;   // crenter1
static const uint16_t CC_FIELD_LAST  = 286;   // crview1

static const int  KEY_CODE = 'C';
static const DWORD CODE_KEY_REPEAT_MS = 700;

static bool  s_inRuins      = false;
static bool  s_entryActive  = false;
static int   s_lastCursor   = -1;
static int   s_lastDigit    = -1;
static bool  s_boxOpen      = false;
static bool  s_codeKeyDown  = false;
static DWORD s_codeKeyTick  = 0;
// v0.116.0 (#centra): set the first time the roof statue is seen holding both
// eyes, and never cleared while the mod runs. See CcCodeRevealed.
static bool  s_codeSeen     = false;

// ============================================================================
// Guarded reads. No C++ objects in any function that uses __try (MSVC C2712);
// see tests/lint_seh.py.
// ============================================================================
static bool ReadVar(int index, int* out)
{
    bool ok = false;
    __try {
        *out = (int)(*(volatile const unsigned char*)
                     (uintptr_t)(CC_VAR_BASE + (uintptr_t)index));
        ok = true;
    } __except (EXCEPTION_EXECUTE_HANDLER) { ok = false; }
    return ok;
}

static bool ReadRow(int first, int* out)
{
    for (int i = 0; i < CC_DIGITS; i++) {
        if (!ReadVar(first + i, &out[i])) return false;
    }
    return true;
}

static unsigned CurrentField()
{
    if (!FF8Addresses::pCurrentFieldId) return 0xFFFFu;
    return (unsigned)(*FF8Addresses::pCurrentFieldId);
}

static void CloseBox(const char* why)
{
    if (!s_boxOpen) return;
    s_boxOpen = false;
    FieldNavigation::CloseControlsBox();
    Log::Field("CentraCode: controls box closed (%s)", why ? why : "");
}

static void LeaveRuins()
{
    if (!s_inRuins) return;
    CloseBox("left the ruins");
    s_inRuins     = false;
    s_entryActive = false;
    s_lastCursor  = -1;
    s_lastDigit   = -1;
    Log::Field("CentraCode: left the Centra Ruins");
}

// The code the roof statue is showing. Spoken on demand, because a player who
// has already been to the roof should not have to walk back up to check a digit
// they half-heard -- and because reading it off the statue is a picture, which
// is the whole reason this module exists.
static void SpeakCode(const char* why)
{
    // v0.116.0 (#centra), Aaron: "just want to make sure the mod doesn't offer
    // to tell the player the code if they haven't actually discovered it."
    // Nothing writes var[364..368] -- they hold the answer from the savemap --
    // so without this gate C would have solved the puzzle for the player on the
    // way in. See centra_code_model.inl for the four eye-socket bits.
    int status = 0;
    if (ReadVar(CC_VAR_STATUS, &status) && CcRoofStatueShowsCode(status) && !s_codeSeen) {
        s_codeSeen = true;
        Log::Field("CentraCode: the roof statue is showing the code "
                   "(var[359]=%d) -- C unlocked [v0.116.0]", status);
    }
    if (!CcCodeRevealed(status, s_codeSeen)) {
        Log::Field("CentraCode: C refused, the code has not been revealed yet "
                   "(var[359]=%d) (%s) [v0.116.0]", status, why ? why : "");
        ScreenReader::Speak(CC_CODE_UNKNOWN_TEXT, true);
        return;
    }
    int code[CC_DIGITS];
    if (!ReadRow(CC_VAR_TARGET0, code)) {
        Log::Field("CentraCode: code read faulted (%s)", why ? why : "");
        return;
    }
    char msg[96];
    CcSequenceText(msg, sizeof(msg), "Code:", code);
    if (msg[0] == '\0') {
        // Every digit has to be 0..9 or the row is not a code. Better silence
        // than a number the player will write down and act on.
        Log::Field("CentraCode: code not readable yet (%d,%d,%d,%d,%d) (%s)",
                   code[0], code[1], code[2], code[3], code[4], why ? why : "");
        ScreenReader::Speak("The code is not set yet.", true);
        return;
    }
    Log::Field("CentraCode: %s (%s)", msg, why ? why : "");
    ScreenReader::Speak(msg, true);
}

void Initialize()
{
    s_inRuins = false; s_entryActive = false;
    s_lastCursor = -1; s_lastDigit = -1;
    s_boxOpen = false; s_codeKeyDown = false; s_codeKeyTick = 0;
    s_codeSeen = false;
    Log::Field("CentraCode: initialised (fields %u..%u)",
               (unsigned)CC_FIELD_FIRST, (unsigned)CC_FIELD_LAST);
}

void Shutdown()
{
    CloseBox("shutdown");
}

void Update()
{
    const unsigned field = CurrentField();
    const bool inRuins = (field >= CC_FIELD_FIRST && field <= CC_FIELD_LAST);
    if (!inRuins) { LeaveRuins(); return; }

    if (!s_inRuins) {
        s_inRuins = true;
        s_entryActive = false;
        s_lastCursor = -1; s_lastDigit = -1;
        Log::Field("CentraCode: entered the Centra Ruins (field %u)", field);
    }

    // --- the latch ------------------------------------------------------------
    // Watched every poll, not only when C is pressed: the player will put the
    // eyes in, read the code off the screen and take them straight back out,
    // and by the time they think to press C at the door the live flags are
    // long gone. The moment of truth is the moment it is on screen.
    {
        int status = 0;
        if (!s_codeSeen && ReadVar(CC_VAR_STATUS, &status) &&
            CcRoofStatueShowsCode(status)) {
            s_codeSeen = true;
            Log::Field("CentraCode: the roof statue is showing the code "
                       "(var[359]=%d) -- C unlocked [v0.116.0]", status);
        }
    }

    // --- the repeat key, anywhere in the ruins -------------------------------
    const bool codeDown = (GetAsyncKeyState(KEY_CODE) & 0x8000) != 0;
    if (codeDown && !s_codeKeyDown) {
        DWORD now = GetTickCount();
        if (now - s_codeKeyTick >= CODE_KEY_REPEAT_MS) {
            s_codeKeyTick = now;
            SpeakCode("C pressed");
        }
    }
    s_codeKeyDown = codeDown;

    // --- the panel -----------------------------------------------------------
    int cursor = 0;
    if (!ReadVar(CC_VAR_CURSOR, &cursor)) return;
    if (!CcCursorValid(cursor)) return;      // a garbage read is not a cursor

    const bool active = CcEntryActive(cursor);

    int digit = -1;
    if (active) {
        // The selected position is 1-based; the digits start at var[1028].
        if (!ReadVar(CC_VAR_DIGIT0 + (cursor - 1), &digit)) return;
    }

    const bool justOpened = (active && !s_entryActive);
    if (justOpened) {
        // The panel has just opened: the box on screen, and ONE spoken line
        // carrying the scheme and the starting position. Two calls would not
        // work -- every line here interrupts, so the position would cut the
        // controls off mid-sentence. See CcOpeningText.
        Log::Field("CentraCode: code panel opened (cursor=%d digit=%d)", cursor, digit);
        if (FieldNavigation::OpenControlsBox(CC_CONTROLS_BOX)) s_boxOpen = true;
        char open[512];
        CcOpeningText(open, sizeof(open), cursor, digit);
        ScreenReader::Speak(open, true);
    } else if (CcShouldAnnounce(active, cursor, digit,
                                s_entryActive, s_lastCursor, s_lastDigit)) {
        char msg[64];
        CcPositionText(msg, sizeof(msg), cursor, digit);
        if (msg[0] != '\0') {
            Log::Field("CentraCode: %s", msg);
            // Interrupt: these arrive one per direction press and the newest is
            // always the one that matters.
            ScreenReader::Speak(msg, true);
        }
    }

    if (!active && s_entryActive) {
        // Triangle closed the panel. Read the row back so the player knows what
        // was submitted, and say whether it matched -- the script has already
        // decided by now, and silence here is the same silence that made the
        // Deep Sea terminals unreadable.
        CloseBox("panel closed");
        int entered[CC_DIGITS], target[CC_DIGITS];
        if (ReadRow(CC_VAR_DIGIT0, entered) && ReadRow(CC_VAR_TARGET0, target)) {
            char msg[128];
            CcSequenceText(msg, sizeof(msg), "Entered:", entered);
            if (msg[0] != '\0') {
                const bool ok = CcCodeMatches(entered, target);
                char full[192];
                snprintf(full, sizeof(full), "%s %s", msg,
                         ok ? "Correct." : "That is not the code.");
                Log::Field("CentraCode: panel closed -- %s", full);
                ScreenReader::Speak(full, true);
            }
        }
    }

    s_entryActive = active;
    s_lastCursor  = active ? cursor : -1;
    // v0.116.0: a one-frame 0xFF mid-wrap is not a digit and must not become
    // the baseline the next comparison is made against -- see CcShouldAnnounce.
    if (!active)                   s_lastDigit = -1;
    else if (CcDigitValid(digit))  s_lastDigit = digit;
}

}  // namespace CentraCodeOverlay
