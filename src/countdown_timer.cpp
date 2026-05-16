// countdown_timer.cpp — Mission countdown timer accessibility implementation.
//
// See countdown_timer.h for the module overview and the research
// background. Quick recap of what this build is and isn't:
//
//   What it is: read 0x01CFECCC (field var 724, uint16, "Dollet mission
//     time") each frame, run a state machine that fires TTS
//     announcements at boundaries, expose T-key for read and Shift+T
//     for an experimental freeze.
//
//   What it isn't: a confirmed-working freeze. The research strongly
//     suggests the live engine timer lives at a separate address and
//     this snapshot is updated only when the field script calls
//     GETTIMER. The BAT will show us how the snapshot actually
//     behaves during the chase; if it doesn't update frequently, the
//     scheduler won't fire and v0.15.13 needs an in-mod scanner for
//     the engine global OR a SETTIMER opcode hook for start detection.
//
// Heavy diagnostic logging is intentional: this is the first build that
// touches FF8's mission timer, and the BAT log is how we learn how the
// memory actually behaves. Every read-value change, every state
// transition, every hotkey press, and every units-classification
// decision is logged.

#include "countdown_timer.h"
#include "ff8_accessibility.h"
#include "mod_forward_decls.h"

#include <windows.h>
#include <cstdint>
#include <cstdio>

namespace CountdownTimer {

// ============================================================================
// Memory addresses (from deep research, 2026-05-16)
// ============================================================================
//
// Field-var stack base on FF8 Steam 2013 = 0x01CFE9B8. Field var 724
// ("Dollet mission time") sits at byte offset 724 = 0x2D4 within the
// persistent-vars region (offsets 0-1023 of the stack, saved to disk).
// The 0x14 savemap correction does NOT apply to the field-var stack
// (those are two separate memory regions per the research).
//
// Wiki source: ff7-flat-wiki FF8/Variables (Shard, Aali, myst6re).
//   "Word | 724 | Dollet mission time"
// Confirmed shared between Dollet (30-min mission) and Fire Cavern
// (10/20/30/40-min player-selected). Same engine countdown system;
// var 724 is the script-side snapshot via GETTIMER opcode (0x0A4).

static constexpr uintptr_t FIELD_VAR_STACK_BASE = 0x01CFE9B8;
static constexpr uintptr_t TIMER_VAR724_ADDR    = FIELD_VAR_STACK_BASE + 724; // 0x01CFECCC

// ============================================================================
// State
// ============================================================================

enum class State : uint8_t {
    INACTIVE,
    ACTIVE,
    FROZEN,
};

enum class Units : uint8_t {
    UNKNOWN,
    SECONDS,      // raw value is seconds remaining (e.g. 1800 for Dollet 30:00)
    MINUTES,      // raw value is minutes remaining (e.g. 30 for Dollet)
    FRAMES_30HZ,  // raw value is frames at 30Hz logic tick (e.g. 54000)
};

static State s_state = State::INACTIVE;
static Units s_units = Units::UNKNOWN;

// Last observed raw value read from TIMER_VAR724_ADDR. Cached so we can
// detect transitions without re-reading.
static uint16_t s_lastRawValue = 0;

// Current remaining time IN SECONDS, derived from raw via units conversion.
// While FROZEN, this stays pinned to the value at freeze entry.
static int s_remainingSec = 0;

// Initial remaining time (seconds) at the start of the current session.
// Used to gate scheduled-announcement boundaries — we don't announce
// "25 minutes remaining" if the timer started below that mark (e.g. on
// a 10-minute Fire Cavern run).
static int s_initialSec = 0;

// While FROZEN, this is the raw value we keep writing back to memory
// each frame (experimental freeze — see header docs).
static uint16_t s_frozenRawValue = 0;

// Bitmap of which scheduled-announcement boundaries have fired this
// session. Bit indices map to BOUNDARY_SEC[] below.
static uint32_t s_announcedMask = 0;

// Edge-detection state for T and Shift+T hotkeys.
static bool s_tWas = false;

// Diagnostic counters and timestamps.
static DWORD s_sessionStartTickMs = 0;
static DWORD s_lastLogTickMs      = 0;
static int   s_lastLoggedRaw      = -1;  // -1 = no log yet; suppresses repeat logs

// Scheduled-announcement boundaries, descending, in SECONDS remaining.
// On detection of "remaining <= boundary AND boundary not yet announced
// AND boundary was below the session initial value", fire and set bit.
static const int BOUNDARY_SEC[] = {
    1500,  // bit 0 - 25:00
    1200,  // bit 1 - 20:00
     900,  // bit 2 - 15:00
     600,  // bit 3 - 10:00
     300,  // bit 4 -  5:00
      60,  // bit 5 -  1:00
      30,  // bit 6 - 30 seconds
};
static const int BOUNDARY_COUNT = sizeof(BOUNDARY_SEC) / sizeof(BOUNDARY_SEC[0]);

// ============================================================================
// Memory access (SEH-wrapped reads/writes)
// ============================================================================

// Returns the raw uint16 at TIMER_VAR724_ADDR, or -1 on access fault.
// Pure read; never modifies memory. SEH-wrapped to defend against pages
// that aren't mapped yet during early startup.
static int ReadVar724Raw()
{
    __try {
        return *reinterpret_cast<volatile uint16_t*>(TIMER_VAR724_ADDR);
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        return -1;
    }
}

// Writes a uint16 to TIMER_VAR724_ADDR. SEH-wrapped. Used by the
// experimental freeze to pin the snapshot.
static void WriteVar724Raw(uint16_t value)
{
    __try {
        *reinterpret_cast<volatile uint16_t*>(TIMER_VAR724_ADDR) = value;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        // ignore — log once at most via the caller's transition handling
    }
}

// ============================================================================
// Units classification
// ============================================================================
//
// On the first nonzero raw observation, classify what units the engine
// uses. Three plausible values from the research:
//
//   1800 (and 600/1200/2400 for Fire Cavern 10/20/40-min) → SECONDS
//   30   (and 10/20/40 for Fire Cavern)                  → MINUTES
//   54000 (and 18000/36000 only — 72000 doesn't fit uint16) → FRAMES_30HZ
//
// Note: with a uint16 ceiling of 65535, Fire Cavern's 40-min option in
// FRAMES_30HZ (72000) doesn't fit, which weakly argues against the
// frames hypothesis. But Dollet is fine either way at 54000.
//
// We classify by range with generous tolerances so the units stay
// correct even if the script writes a slightly different starting value
// or if a few seconds elapse between SETTIMER and our first observation.

static Units ClassifyUnits(uint16_t raw)
{
    // Frames @ 30Hz: 30-minute Dollet = 54000, 10-minute Fire Cavern = 18000,
    // 20-minute = 36000. Range: 15000-58000 covers everything that fits
    // in uint16. Has highest priority because no other interpretation
    // produces values this large.
    if (raw >= 15000 && raw <= 60000) {
        return Units::FRAMES_30HZ;
    }
    // Seconds: 30-min Dollet = 1800, 10-min FC = 600, 40-min FC = 2400.
    // Range: 500-3000 covers all known mission timers in seconds.
    if (raw >= 500 && raw <= 3000) {
        return Units::SECONDS;
    }
    // Minutes: 10-40 covers all known durations.
    if (raw >= 5 && raw <= 60) {
        return Units::MINUTES;
    }
    return Units::UNKNOWN;
}

static const char* UnitsName(Units u)
{
    switch (u) {
        case Units::SECONDS:     return "SECONDS";
        case Units::MINUTES:     return "MINUTES";
        case Units::FRAMES_30HZ: return "FRAMES_30HZ";
        default:                 return "UNKNOWN";
    }
}

static int RawToSeconds(uint16_t raw, Units u)
{
    switch (u) {
        case Units::SECONDS:     return (int)raw;
        case Units::MINUTES:     return (int)raw * 60;
        case Units::FRAMES_30HZ: return (int)raw / 30;
        default:                 return 0;
    }
}

// ============================================================================
// Announcement helpers
// ============================================================================

static void FormatAndSpeakRemaining(int seconds)
{
    if (seconds < 0) {
        ScreenReader::Speak("No timer active.", true);
        return;
    }

    char msg[96];
    if (seconds >= 120) {
        int mins = seconds / 60;
        int secs = seconds % 60;
        if (secs == 0) {
            snprintf(msg, sizeof(msg), "%d minutes remaining.", mins);
        } else {
            snprintf(msg, sizeof(msg), "%d minutes %d seconds remaining.",
                     mins, secs);
        }
    } else if (seconds >= 60) {
        int secs = seconds % 60;
        if (secs == 0) {
            snprintf(msg, sizeof(msg), "1 minute remaining.");
        } else {
            snprintf(msg, sizeof(msg), "1 minute %d seconds remaining.", secs);
        }
    } else {
        snprintf(msg, sizeof(msg), "%d seconds remaining.", seconds);
    }
    ScreenReader::Speak(msg, true);
}

static void SpeakBoundary(int boundarySec)
{
    char msg[64];
    if (boundarySec >= 60) {
        int mins = boundarySec / 60;
        snprintf(msg, sizeof(msg), "%d minute%s remaining.",
                 mins, mins == 1 ? "" : "s");
    } else {
        snprintf(msg, sizeof(msg), "%d seconds remaining.", boundarySec);
    }
    ScreenReader::Speak(msg, true);
}

// ============================================================================
// State transitions
// ============================================================================

static void EnterActive(uint16_t firstRaw)
{
    Units units = ClassifyUnits(firstRaw);
    if (units == Units::UNKNOWN) {
        // Don't enter ACTIVE on values we can't classify — it's probably
        // noise (e.g. a value left over in 724 from some non-timer use,
        // or partial memory init at startup). Stay INACTIVE; if the
        // value actually IS a real timer in some range we didn't
        // anticipate, the log will tell us and we add a range in v0.15.13.
        Log::Mod("[CountdownTimer] Observed nonzero value %u but units "
                 "UNKNOWN (no classification matched: not minutes 5-60, "
                 "not seconds 500-3000, not frames 15000-60000). Staying "
                 "INACTIVE. Add a units range in v0.15.13 if this turns "
                 "out to be a real timer.", (unsigned)firstRaw);
        return;
    }

    s_units = units;
    s_initialSec = RawToSeconds(firstRaw, units);
    s_remainingSec = s_initialSec;
    s_lastRawValue = firstRaw;
    s_announcedMask = 0;
    s_sessionStartTickMs = GetTickCount();
    s_state = State::ACTIVE;

    // Pre-flag boundaries already past the session start. Rule:
    // fire boundary iff boundary < initial. Anything boundary >= initial
    // is pre-flagged so it never fires.
    for (int i = 0; i < BOUNDARY_COUNT; i++) {
        if (BOUNDARY_SEC[i] >= s_initialSec) {
            s_announcedMask |= (1u << i);
        }
    }

    Log::Mod("[CountdownTimer] ENTER ACTIVE: rawValue=%u units=%s "
             "initialSec=%d (%dm%02ds) preFlaggedBoundaryMask=0x%X",
             (unsigned)firstRaw, UnitsName(units), s_initialSec,
             s_initialSec / 60, s_initialSec % 60, s_announcedMask);

    // Initial announcement.
    char msg[96];
    int mins = s_initialSec / 60;
    int secs = s_initialSec % 60;
    if (secs == 0) {
        snprintf(msg, sizeof(msg), "Timer started. %d minutes remaining.", mins);
    } else {
        snprintf(msg, sizeof(msg),
                 "Timer started. %d minutes %d seconds remaining.", mins, secs);
    }
    ScreenReader::Speak(msg, true);
}

static void EnterInactive(const char* reason)
{
    State prev = s_state;
    s_state = State::INACTIVE;
    s_units = Units::UNKNOWN;
    s_remainingSec = 0;
    s_initialSec = 0;
    s_lastRawValue = 0;
    s_announcedMask = 0;
    s_frozenRawValue = 0;
    Log::Mod("[CountdownTimer] ENTER INACTIVE: prev=%d reason=%s",
             (int)prev, reason);
}

static void CheckScheduledAnnouncements()
{
    // Fire any boundary that has been crossed since last frame.
    // remaining <= boundary AND boundary not yet announced.
    for (int i = 0; i < BOUNDARY_COUNT; i++) {
        uint32_t bit = (1u << i);
        if (s_announcedMask & bit) continue;
        if (s_remainingSec <= BOUNDARY_SEC[i]) {
            s_announcedMask |= bit;
            Log::Mod("[CountdownTimer] BOUNDARY %d seconds reached "
                     "(actual remaining=%d)", BOUNDARY_SEC[i], s_remainingSec);
            SpeakBoundary(BOUNDARY_SEC[i]);
            // Fire only one boundary per frame to avoid stacking speech.
            break;
        }
    }
}

// ============================================================================
// Hotkey polling (T and Shift+T)
// ============================================================================
//
// T: announce remaining time on demand (only fires when IsActive(); when
//    not active, lets menu_tts.cpp's existing T = AnnouncePlayTime handle
//    the press in menu mode).
//
// Shift+T: toggle the experimental freeze.
//
// menu_tts.cpp's bare-T handler runs in mode 6 only (in-game menu) and
// must be gated on !shift so Shift+T doesn't fire BOTH handlers.
// (v0.15.12.0 adds that gate in the same commit.)

static void PollHotkeys()
{
    bool tkey  = (GetAsyncKeyState('T')      & 0x8000) != 0;
    bool shift = (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0;
    bool alt   = (GetAsyncKeyState(VK_MENU)  & 0x8000) != 0;

    // Edge detection on T.
    bool tEdge = tkey && !s_tWas;
    s_tWas = tkey;

    if (!tEdge || alt) return;

    if (shift) {
        ToggleFreeze();
    } else if (IsActive()) {
        // Only fire on bare-T when a countdown is detected. Outside
        // that, let menu_tts.cpp's existing T = AnnouncePlayTime handler
        // run (or stay silent in field mode where it doesn't fire).
        AnnounceRemaining();
    }
    // else: bare T with no active timer in field mode — silent fall-through.
}

// ============================================================================
// Public API
// ============================================================================

void Initialize()
{
    s_state = State::INACTIVE;
    s_units = Units::UNKNOWN;
    s_lastRawValue = 0;
    s_remainingSec = 0;
    s_initialSec = 0;
    s_frozenRawValue = 0;
    s_announcedMask = 0;
    s_tWas = false;
    s_sessionStartTickMs = 0;
    s_lastLogTickMs = 0;
    s_lastLoggedRaw = -1;
    Log::Mod("[CountdownTimer] Initialize: reading field var 724 at 0x%08X "
             "(uint16). T=announce, Shift+T=experimental freeze (rewrite "
             "snapshot each frame). Boundaries: 25/20/15/10/5:00, 1:00, 0:30.",
             (uint32_t)TIMER_VAR724_ADDR);
}

void Shutdown()
{
    if (s_state == State::FROZEN) {
        Log::Mod("[CountdownTimer] Shutdown: was FROZEN, releasing.");
    }
    s_state = State::INACTIVE;
}

void Update()
{
    PollHotkeys();

    int rawSigned = ReadVar724Raw();
    if (rawSigned < 0) {
        // Access fault — game memory not mapped yet, or address invalid.
        // Stay in current state; quietly skip this frame.
        return;
    }
    uint16_t raw = (uint16_t)rawSigned;

    // Log any change in the raw value, with rate-limiting (once per 50ms
    // when changing, so during a rapid countdown we still see the
    // progression but don't flood the log per-frame). Also log at startup
    // (when s_lastLoggedRaw == -1) so we know the initial state.
    DWORD now = GetTickCount();
    bool changed = ((int)raw != s_lastLoggedRaw);
    bool firstObservation = (s_lastLoggedRaw == -1);
    if ((changed && (now - s_lastLogTickMs) >= 50) || firstObservation) {
        Log::Mod("[CountdownTimer] var724 raw=%u (prev=%d) state=%d "
                 "tickMs=%lu", (unsigned)raw, s_lastLoggedRaw,
                 (int)s_state, (unsigned long)now);
        s_lastLogTickMs = now;
        s_lastLoggedRaw = (int)raw;
    }

    switch (s_state) {
        case State::INACTIVE:
            if (raw > 0) {
                // Timer might have just been set. Try to enter ACTIVE.
                EnterActive(raw);
            }
            break;

        case State::ACTIVE: {
            if (raw == 0) {
                // Timer hit zero or got cleared.
                EnterInactive("snapshot=0");
                break;
            }
            // Update remaining time from raw + units.
            int newRemaining = RawToSeconds(raw, s_units);
            if (newRemaining != s_remainingSec) {
                s_remainingSec = newRemaining;
                s_lastRawValue = raw;
                CheckScheduledAnnouncements();
            }
            break;
        }

        case State::FROZEN: {
            // EXPERIMENTAL: rewrite the snapshot value back to memory
            // each frame to hold the displayed timer. If 0x01CFECCC is
            // the live engine global, this freezes everything; if it's
            // a periodic snapshot, the engine timer continues underneath
            // and the BAT will show that via continued game-over or the
            // raw value re-decrementing despite our writes.
            WriteVar724Raw(s_frozenRawValue);
            // We don't fire scheduled announcements while frozen; the
            // remaining stays pinned. If something else changes raw out
            // from under our write (e.g., the engine is faster than us),
            // s_lastLoggedRaw will diverge from s_frozenRawValue and the
            // change-logging path above will record that divergence.
            break;
        }
    }
}

bool IsActive()
{
    return s_state == State::ACTIVE || s_state == State::FROZEN;
}

void AnnounceRemaining()
{
    if (s_state == State::INACTIVE) {
        ScreenReader::Speak("No timer active.", true);
        return;
    }
    // Diagnostic line includes the raw value so we can correlate
    // announcements with what's actually in memory.
    Log::Mod("[CountdownTimer] AnnounceRemaining (T key): state=%d "
             "raw=%u units=%s remainingSec=%d",
             (int)s_state, (unsigned)s_lastRawValue,
             UnitsName(s_units), s_remainingSec);
    FormatAndSpeakRemaining(s_remainingSec);
    if (s_state == State::FROZEN) {
        ScreenReader::Speak("Timer is frozen.", false);
    }
}

void ToggleFreeze()
{
    if (s_state == State::INACTIVE) {
        Log::Mod("[CountdownTimer] ToggleFreeze (Shift+T): no timer active.");
        ScreenReader::Speak("No timer to freeze.", true);
        return;
    }
    if (s_state == State::ACTIVE) {
        s_state = State::FROZEN;
        s_frozenRawValue = s_lastRawValue;
        Log::Mod("[CountdownTimer] FREEZE engaged at raw=%u remainingSec=%d "
                 "(experimental — rewriting 0x%08X each frame).",
                 (unsigned)s_frozenRawValue, s_remainingSec,
                 (uint32_t)TIMER_VAR724_ADDR);
        ScreenReader::Speak("Timer frozen.", true);
    } else { // FROZEN
        s_state = State::ACTIVE;
        Log::Mod("[CountdownTimer] FREEZE released at raw=%u remainingSec=%d.",
                 (unsigned)s_lastRawValue, s_remainingSec);
        s_frozenRawValue = 0;
        ScreenReader::Speak("Timer resumed.", true);
    }
}

} // namespace CountdownTimer
