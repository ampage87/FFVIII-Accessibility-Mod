// countdown_timer.cpp — Mission countdown timer accessibility implementation.
//
// See countdown_timer.h for the module overview and the research
// background. v0.15.13.2 update:
//
// v0.15.13.1's scanner found the live engine timer global at
// 0x01CFE92C. Cycle 11 of that BAT (21:50:40) showed exactly one
// R1 u32 candidate: cur=1711, old=1715, dec=4 over 4 sec, rate=1.00/s
// monotonic. The value 1711 is 28:31 remaining in seconds — perfectly
// consistent with a Dollet chase save loaded mid-run. The address is
// 0x8C bytes BELOW the game-object struct base 0x01CFE9B8, in an
// adjacent engine-globals allocation. v0.15.13.0's old Region 1
// (8 KB at 0x01CFE9B8) missed it because it started AT the game
// object; v0.15.13.1's expanded Region 1 (192 KB at 0x01CD0000) caught
// it.
//
// v0.15.13.2 changes:
//   - Reads now point at LIVE_TIMER_ADDR = 0x01CFE92C (uint16 — value
//     fits comfortably in 16 bits, so we don't risk clobbering high
//     bytes during Shift+T freeze).
//   - Scanner DISABLED via COUNTDOWN_SCAN_ENABLED=0 in the .inl,
//     freeing ~6 MB of static memory and per-frame CPU. The file is
//     kept for re-enabling on future similar diagnostic problems.
//   - Old TIMER_VAR724_ADDR (0x01CFEC8C, script-side snapshot) kept as
//     a named constant so the relationship is documented, but no
//     longer read.

#include "countdown_timer.h"
#include "ff8_accessibility.h"
#include "mod_forward_decls.h"

#include <windows.h>
#include <cstdint>
#include <cstdio>
#include <cstring>  // memcpy, memset (used by countdown_scan.inl when enabled)

namespace CountdownTimer {

// ============================================================================
// Memory addresses
// ============================================================================
//
// LIVE_TIMER_ADDR (0x01CFE92C) — the live engine countdown global,
// discovered by the v0.15.13.1 scanner. Decrements at exactly 1.0/sec,
// monotonically, stored in uint16/uint32-compatible form (low 16 bits
// hold the value, high 16 bits are zero for all reasonable timer
// durations since the max representable timer is 65535 seconds = 18h).
// We read as uint16 for safety: writes (Shift+T freeze) won't clobber
// any high bytes that may hold engine state we don't know about.
//
// VAR724_SNAPSHOT_ADDR (0x01CFEC8C) — the script-side snapshot of
// field var 724 ("Dollet mission time" per the wiki). Updated by the
// GETTIMER opcode (0x0A4) only when the field script explicitly calls
// it. The chase script doesn't call GETTIMER routinely, so this
// address stays at 0 the whole chase — that's why v0.15.12.0 / .13.0
// / .13.1 couldn't read the timer here. Kept as a named constant for
// documentation; no longer used at runtime.
//
// The legacy "+ 724 (decimal) = + 0x2D4" arithmetic stands: field-var
// stack base 0x01CFE9B8 + 0x2D4 = 0x01CFEC8C. That math was correct;
// it just wasn't the right ADDRESS to read.

static constexpr uintptr_t LIVE_TIMER_ADDR        = 0x01CFE92C;  // v0.15.13.2 — scanner-discovered
static constexpr uintptr_t VAR724_SNAPSHOT_ADDR   = 0x01CFEC8C;  // legacy — script-side, unused

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

// Last observed raw value read from LIVE_TIMER_ADDR.
static uint16_t s_lastRawValue = 0;

// Current remaining time IN SECONDS, derived from raw via units conversion.
// While FROZEN, this stays pinned to the value at freeze entry.
static int s_remainingSec = 0;

// Initial remaining time (seconds) at the start of the current session.
// Used to gate scheduled-announcement boundaries — we don't announce
// "25 minutes remaining" if the timer started below that mark (e.g. on
// a 10-minute Fire Cavern run, or if Aaron loads a save mid-chase
// already below 25:00).
static int s_initialSec = 0;

// While FROZEN, this is the raw value we keep writing back to memory
// each frame.
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
// Scanner (countdown_scan.inl). Forward-declared; bodies become no-ops
// when COUNTDOWN_SCAN_ENABLED=0 (set in the .inl). Kept around in case
// we need to scan again for a future timer / state variable.
// ============================================================================

namespace Scan {
    static void Initialize();
    static void Update(DWORD now);
}

// ============================================================================
// Memory access (SEH-wrapped reads/writes)
// ============================================================================

// Returns the raw uint16 at LIVE_TIMER_ADDR, or -1 on access fault.
static int ReadLiveTimerRaw()
{
    __try {
        return *reinterpret_cast<volatile uint16_t*>(LIVE_TIMER_ADDR);
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        return -1;
    }
}

// Writes a uint16 to LIVE_TIMER_ADDR. SEH-wrapped. Used by the freeze
// feature to pin the timer.
static void WriteLiveTimerRaw(uint16_t value)
{
    __try {
        *reinterpret_cast<volatile uint16_t*>(LIVE_TIMER_ADDR) = value;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        // ignore — log once at most via the caller's transition handling
    }
}

// ============================================================================
// Units classification
// ============================================================================

static Units ClassifyUnits(uint16_t raw)
{
    // Frames @ 30Hz: 30-min Dollet = 54000; range 15000-60000.
    if (raw >= 15000 && raw <= 60000) {
        return Units::FRAMES_30HZ;
    }
    // Seconds: 30-min Dollet = 1800; range 500-3000.
    // Aaron's v0.15.13.1 BAT confirmed live timer reads 1711 = 28:31,
    // squarely in this range.
    if (raw >= 500 && raw <= 3000) {
        return Units::SECONDS;
    }
    // Minutes: 5-60.
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
        Log::Mod("[CountdownTimer] Observed nonzero value %u but units "
                 "UNKNOWN (no classification matched: not minutes 5-60, "
                 "not seconds 500-3000, not frames 15000-60000). Staying "
                 "INACTIVE.", (unsigned)firstRaw);
        return;
    }

    s_units = units;
    s_initialSec = RawToSeconds(firstRaw, units);
    s_remainingSec = s_initialSec;
    s_lastRawValue = firstRaw;
    s_announcedMask = 0;
    s_sessionStartTickMs = GetTickCount();
    s_state = State::ACTIVE;

    // Pre-flag boundaries already past the session start.
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
        snprintf(msg, sizeof(msg), "Timer detected. %d minutes remaining.", mins);
    } else {
        snprintf(msg, sizeof(msg),
                 "Timer detected. %d minutes %d seconds remaining.", mins, secs);
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
    for (int i = 0; i < BOUNDARY_COUNT; i++) {
        uint32_t bit = (1u << i);
        if (s_announcedMask & bit) continue;
        if (s_remainingSec <= BOUNDARY_SEC[i]) {
            s_announcedMask |= bit;
            Log::Mod("[CountdownTimer] BOUNDARY %d seconds reached "
                     "(actual remaining=%d)", BOUNDARY_SEC[i], s_remainingSec);
            SpeakBoundary(BOUNDARY_SEC[i]);
            // Fire only one boundary per frame.
            break;
        }
    }
}

// ============================================================================
// Hotkey polling (T and Shift+T)
// ============================================================================

static void PollHotkeys()
{
    bool tkey  = (GetAsyncKeyState('T')      & 0x8000) != 0;
    bool shift = (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0;
    bool alt   = (GetAsyncKeyState(VK_MENU)  & 0x8000) != 0;

    bool tEdge = tkey && !s_tWas;
    s_tWas = tkey;

    if (!tEdge || alt) return;

    if (shift) {
        ToggleFreeze();
    } else if (IsActive()) {
        AnnounceRemaining();
    }
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
    Log::Mod("[CountdownTimer] Initialize v0.15.13.2: reading live engine "
             "timer at 0x%08X (uint16, scanner-discovered v0.15.13.1). "
             "T=announce, Shift+T=freeze (rewrite each frame). Boundaries: "
             "25/20/15/10/5:00, 1:00, 0:30. Script-side snapshot at "
             "0x%08X no longer read (stays at 0 during chase).",
             (uint32_t)LIVE_TIMER_ADDR, (uint32_t)VAR724_SNAPSHOT_ADDR);
    Scan::Initialize();  // no-op when COUNTDOWN_SCAN_ENABLED=0
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

    DWORD now = GetTickCount();

    int rawSigned = ReadLiveTimerRaw();
    if (rawSigned >= 0) {
        uint16_t raw = (uint16_t)rawSigned;

        // Log any change in raw value, rate-limited to 50ms.
        bool changed = ((int)raw != s_lastLoggedRaw);
        bool firstObservation = (s_lastLoggedRaw == -1);
        if ((changed && (now - s_lastLogTickMs) >= 50) || firstObservation) {
            Log::Mod("[CountdownTimer] live raw=%u (prev=%d) state=%d "
                     "tickMs=%lu", (unsigned)raw, s_lastLoggedRaw,
                     (int)s_state, (unsigned long)now);
            s_lastLogTickMs = now;
            s_lastLoggedRaw = (int)raw;
        }

        switch (s_state) {
            case State::INACTIVE:
                if (raw > 0) {
                    EnterActive(raw);
                }
                break;

            case State::ACTIVE: {
                if (raw == 0) {
                    EnterInactive("live timer=0");
                    break;
                }
                int newRemaining = RawToSeconds(raw, s_units);
                if (newRemaining != s_remainingSec) {
                    s_remainingSec = newRemaining;
                    s_lastRawValue = raw;
                    CheckScheduledAnnouncements();
                }
                break;
            }

            case State::FROZEN: {
                // Rewrite each frame to hold the displayed timer. The
                // engine is also writing to 0x01CFE92C every second to
                // decrement; our writes (on the faster mod-thread tick)
                // should overwrite the engine's decrements before they
                // visibly affect the HUD.
                WriteLiveTimerRaw(s_frozenRawValue);
                break;
            }
        }
    }

    Scan::Update(now);  // no-op when COUNTDOWN_SCAN_ENABLED=0
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
                 "(rewriting 0x%08X each frame).",
                 (unsigned)s_frozenRawValue, s_remainingSec,
                 (uint32_t)LIVE_TIMER_ADDR);
        ScreenReader::Speak("Timer frozen.", true);
    } else { // FROZEN
        s_state = State::ACTIVE;
        Log::Mod("[CountdownTimer] FREEZE released at raw=%u remainingSec=%d.",
                 (unsigned)s_lastRawValue, s_remainingSec);
        s_frozenRawValue = 0;
        ScreenReader::Speak("Timer resumed.", true);
    }
}

// Scanner — disabled in v0.15.13.2 via COUNTDOWN_SCAN_ENABLED=0 inside
// the .inl. Initialize / Update become empty stubs and the large static
// buffers are not allocated. File kept for re-enabling on future
// diagnostic problems.
#include "countdown_scan.inl"

} // namespace CountdownTimer
