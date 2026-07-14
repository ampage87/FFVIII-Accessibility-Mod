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
#include "ff8_addresses.h"   // v0.18.3.237 (#75): IsOnField gate for the dismissal check

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

// v0.18.3.237 (#75) — TIMER_VISIBLE_FLAG_ADDR (0x01D2B813): the engine's own
// HUD-timer visibility flag, found via disassembly xref on LIVE_TIMER_ADDR
// after the .236 TIMERDIAG BAT proved no dismissal signal lives in the 96
// bytes around the counter (the raw global keeps decrementing after the
// Ifrit victory dismisses the display; the only nearby change was a battle
// counter at 0x01CFE934).
//   * 0x004A6CC0 = the engine's set-timer-visible(arg) — writes arg to
//     0x01D2B813 (and on enable snapshots the current timer byte).
//   * 0x004A6D40 = the MM:SS HUD renderer — FIRST instruction tests
//     0x01D2B813 and returns without drawing when 0; otherwise it reads
//     0x01CFE92C, clamps to 0x1797, divides by 60 and draws.
// So this byte IS "the timer is on screen" — the display-pipeline truth,
// per the project rule (hook/read the display state, never infer from
// upstream memory). ACTIVE is gated on it below; the check is applied only
// while on the field so any battle-HUD handoff can't false-dismiss.
static constexpr uintptr_t TIMER_VISIBLE_FLAG_ADDR = 0x01D2B813;

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

// v0.18.3.238 (#75): HUD-dismissal DEBOUNCE. The .237 BAT showed the engine
// blanks 0x01D2B813 for ~2 s on every battle->field return (dismiss at
// 21:04:21, flag back and re-announced 21:04:23 — one spurious "Timer
// detected" per random battle). Only a SUSTAINED zero means the sequence is
// over (post-Ifrit the flag stayed 0 permanently). Dismiss only after the
// flag has read 0 continuously for this long while on the field.
static DWORD s_visZeroSinceMs = 0;
static const DWORD HUD_DISMISS_DEBOUNCE_MS = 4000;

// v0.18.3.238 (#75): rate-limit for the activation-gate log line (the .237
// BAT logged "staying INACTIVE" every ~2 s for the whole post-Ifrit walk-out).
static DWORD s_actVisLogTick = 0;
static const DWORD ACT_VIS_LOG_INTERVAL_MS = 30000;

// v0.18.3.29 (#59): decrement-gated activation. While INACTIVE we watch the
// timer global and only ENTER ACTIVE once we've seen it cleanly step DOWN a
// couple of times -- the signature of a live 1/sec countdown. A static
// leftover value (e.g. the 79 the Timber timer holds after the mission
// completes) never decrements, so it no longer false-triggers an announcement
// and no longer floods the log (the old code called EnterActive every tick on
// any nonzero value). s_actPrevRaw is -1 when unseeded.
static int s_actPrevRaw    = -1;
static int s_actDecrements = 0;
static const int ACT_DECREMENTS_NEEDED = 2;  // ~2 seconds of confirmed countdown

// Diagnostic counters and timestamps.
static DWORD s_sessionStartTickMs = 0;
static DWORD s_lastLogTickMs      = 0;
static int   s_lastLoggedRaw      = -1;  // -1 = no log yet; suppresses repeat logs

// v0.18.3.236 (#75): timer-dismissal flag discovery diagnostic.
// The 2026-07-12 Ifrit run proved the engine global at 0x01CFE92C KEEPS
// DECREMENTING after the game dismisses the on-screen timer post-victory
// (raw 523 -> 516 across the victory screen), so neither the stall detector
// nor the zero check ever deactivates — T reports a phantom timer (#75).
// The dismissal state must live in a separate flag. This diagnostic dumps
// the 96 bytes around the timer global once per second while a timer is
// active (and for 60 s after activation ends) so one Fire Cavern BAT
// captures the byte that flips when the HUD timer is dismissed.
// Gate to 0 once the flag is identified.
// v0.18.3.237: FLAG FOUND (0x01D2B813, via disasm xref — the .236 BAT dump
// window contained no dismissal signal, only the battle counter at
// 0x01CFE934). Diagnostic gated OFF; code retained per the gating pattern.
#define TIMER_DISMISS_DIAG 0
#if TIMER_DISMISS_DIAG
static DWORD s_dismissDiagLastDumpMs = 0;
static DWORD s_dismissDiagTailUntil  = 0;   // keep dumping 60s past ACTIVE
static void DismissDiagDump(DWORD now)
{
    (void)now;
    static constexpr uintptr_t DIAG_BASE = 0x01CFE900;
    static constexpr int       DIAG_LEN  = 0x60;
    uint8_t buf[DIAG_LEN];
    __try {
        memcpy(buf, (const void*)DIAG_BASE, DIAG_LEN);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return;
    }
    for (int row = 0; row < DIAG_LEN; row += 32) {
        char line[3 * 32 + 16];
        int pos = 0;
        for (int i = 0; i < 32; i++) {
            pos += snprintf(line + pos, sizeof(line) - pos, "%02X ", buf[row + i]);
        }
        Log::Mod("[TIMERDIAG] 0x%08X: %s",
                 (uint32_t)(DIAG_BASE + row), line);
    }
}
#endif

// v0.18.3.30 (#59): stall-based deactivation. A running countdown changes
// ~1/sec; if the ACTIVE value stops changing for STALL_TIMEOUT_MS, the timed
// sequence has ended (mission complete) WITHOUT the global reaching 0 -- it
// just freezes at whatever was left (the Timber train leaves ~79). We then
// deactivate so the mod stops reporting a phantom running timer. This fixes
// the long-standing "timer still counting after the sequence ended" glitch
// across all timers. FROZEN is unaffected (the user holds that on purpose).
static DWORD s_lastDecrementTickMs = 0;
static const int STALL_TIMEOUT_MS = 8000;  // ~8s with no change => sequence ended

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

// v0.18.3.237 (#75): reads the engine HUD-timer visibility byte at
// 0x01D2B813. Returns -1 on access fault (treated as "unknown" — callers
// must not deactivate on a fault).
static int ReadTimerVisibleFlag()
{
    __try {
        return *reinterpret_cast<volatile uint8_t*>(TIMER_VISIBLE_FLAG_ADDR);
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        return -1;
    }
}

// v0.18.3.238 (#75): debounced dismissal test shared by ACTIVE and FROZEN.
// Returns true only when the HUD flag has read 0 continuously for
// HUD_DISMISS_DEBOUNCE_MS while on the field. Off-field ticks, visible
// reads, and read faults (-1) all reset the zero-clock, so the ~2 s
// battle-return blank never dismisses.
static bool HudDismissedDebounced(DWORD now)
{
    if (!FF8Addresses::IsOnField()) { s_visZeroSinceMs = 0; return false; }
    if (ReadTimerVisibleFlag() != 0) { s_visZeroSinceMs = 0; return false; }
    if (s_visZeroSinceMs == 0) { s_visZeroSinceMs = now; return false; }
    return (now - s_visZeroSinceMs) >= HUD_DISMISS_DEBOUNCE_MS;
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
    // This global holds SECONDS for every timed sequence that uses it
    // (Dollet confirmed 1711 = 28:31; Fire Cavern and the Timber train
    // share the same on-screen MM:SS timer and the same global), so we
    // treat essentially the whole low range as seconds. This is what lets
    // the Timber 5-minute (300s) timer and its final 0:30 / 0:79 values
    // classify -- the old 500-3000 floor (sized for Dollet) left the train
    // timer below the floor, so it never activated (#59).
    //
    // The old MINUTES 5-60 branch was removed: a raw of, say, 30 at this
    // address is 30 SECONDS (a final countdown), never 30 minutes -- that
    // branch would have mis-scaled the train's last minute by 60x had it
    // ever activated. Units are latched once at EnterActive anyway, so an
    // already-active SECONDS timer stays SECONDS all the way down to 0.
    if (raw >= 1 && raw <= 14999) {
        return Units::SECONDS;
    }
    // Frames @ 30Hz kept as a conservative fallback for any sequence that
    // might store a frame count (30-min @ 30Hz = 54000; range 15000-60000).
    if (raw >= 15000 && raw <= 60000) {
        return Units::FRAMES_30HZ;
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
        Log::Mod("[CountdownTimer] value %u unclassifiable; staying INACTIVE.",
                 (unsigned)firstRaw);
        return;
    }

    s_units = units;
    s_initialSec = RawToSeconds(firstRaw, units);
    s_remainingSec = s_initialSec;
    s_lastRawValue = firstRaw;
    s_announcedMask = 0;
    s_sessionStartTickMs = GetTickCount();
    s_lastDecrementTickMs = GetTickCount();
    s_visZeroSinceMs = 0;   // v0.18.3.238: fresh debounce clock per session
    s_actVisLogTick  = 0;
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
    s_actPrevRaw = -1;
    s_actDecrements = 0;
    s_lastDecrementTickMs = 0;
    s_visZeroSinceMs = 0;   // v0.18.3.238
    s_actVisLogTick  = 0;
    s_sessionStartTickMs = 0;
    s_lastLogTickMs = 0;
    s_lastLoggedRaw = -1;
    Log::Mod("[CountdownTimer] Initialize v0.18.3.30: reading live engine "
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

#if TIMER_DISMISS_DIAG
        // v0.18.3.236 (#75): while a timer session is live (and for 60 s
        // after it ends) dump the surrounding globals once per second to
        // find the HUD-dismissal flag.
        if (s_state != State::INACTIVE) {
            s_dismissDiagTailUntil = now + 60000;
        }
        if ((s_state != State::INACTIVE ||
             (s_dismissDiagTailUntil != 0 && now < s_dismissDiagTailUntil)) &&
            (now - s_dismissDiagLastDumpMs) >= 1000) {
            s_dismissDiagLastDumpMs = now;
            DismissDiagDump(now);
        }
#endif

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
            case State::INACTIVE: {
                // Decrement-gated activation (#59). Only a value that is
                // actively counting down should start the timer; a static
                // leftover must be ignored.
                if (raw == 0) {
                    s_actPrevRaw = -1;
                    s_actDecrements = 0;
                    break;
                }
                if (s_actPrevRaw < 0) {
                    // First sighting -- seed, don't trust it yet.
                    s_actPrevRaw = (int)raw;
                    s_actDecrements = 0;
                    break;
                }
                int delta = s_actPrevRaw - (int)raw;
                if (delta == 0) {
                    // Unchanged this tick. A real countdown only steps once
                    // per second, so the vast majority of ticks land here.
                    break;
                }
                if (delta > 0 && delta <= 3) {
                    // Clean small step-down: the live-countdown signature.
                    s_actPrevRaw = (int)raw;
                    s_actDecrements++;
                    if (s_actDecrements >= ACT_DECREMENTS_NEEDED) {
                        // v0.18.3.237 (#75): also require the engine's HUD
                        // visibility flag — the global keeps decrementing
                        // after a dismissed trial (Ifrit), and a save/reload
                        // could otherwise re-activate on that phantom count.
                        // vis==-1 (read fault) is treated as unknown and
                        // does NOT block activation.
                        int vis = ReadTimerVisibleFlag();
                        if (vis == 0) {
                            // v0.18.3.238: rate-limited — this fired every
                            // ~2 s for the whole post-Ifrit walk-out in the
                            // .237 BAT log.
                            if (s_actVisLogTick == 0 ||
                                (now - s_actVisLogTick) >= ACT_VIS_LOG_INTERVAL_MS) {
                                s_actVisLogTick = now;
                                Log::Mod("[CountdownTimer] decrement signature met "
                                         "but HUD flag 0x%08X=0 (timer not displayed) "
                                         "-- staying INACTIVE (log rate-limited to %us)",
                                         (uint32_t)TIMER_VISIBLE_FLAG_ADDR,
                                         (unsigned)(ACT_VIS_LOG_INTERVAL_MS / 1000));
                            }
                            s_actDecrements = 0;
                            break;
                        }
                        EnterActive(raw);
                        s_actPrevRaw = -1;
                        s_actDecrements = 0;
                    }
                    break;
                }
                // Jumped up, or a large discontinuity (timer reset, a
                // different sequence, or a stale write). Resync and require
                // fresh decrements before trusting it.
                s_actPrevRaw = (int)raw;
                s_actDecrements = 0;
                break;
            }

            case State::ACTIVE: {
                if (raw == 0) {
                    EnterInactive("live timer=0");
                    break;
                }
                // v0.18.3.237 (#75): the engine dismisses the HUD timer when
                // the timed sequence ends (Ifrit victory) but keeps
                // decrementing the raw global, so neither the zero check nor
                // the stall detector ever fires and T reports a phantom
                // timer. The renderer's own gate byte (0x01D2B813, see the
                // address block comment) is the truth: 0 = not on screen.
                // v0.18.3.238: DEBOUNCED — the .237 BAT showed a ~2 s flag
                // blank on every battle->field return, which caused a
                // dismiss + "Timer detected" re-announce per random battle.
                // Only a sustained (4 s on-field) zero dismisses now.
                if (HudDismissedDebounced(now)) {
                    EnterInactive("HUD timer dismissed (0x01D2B813=0 sustained)");
                    break;
                }
                int newRemaining = RawToSeconds(raw, s_units);
                if (newRemaining != s_remainingSec) {
                    s_remainingSec = newRemaining;
                    s_lastRawValue = raw;
                    s_lastDecrementTickMs = now;   // activity -- reset stall watch
                    CheckScheduledAnnouncements();
                } else if ((now - s_lastDecrementTickMs) >= (DWORD)STALL_TIMEOUT_MS) {
                    // Value unchanged for STALL_TIMEOUT_MS. A live countdown
                    // steps ~1/sec, so a stall this long means the timed
                    // sequence ended without the global hitting 0 (it froze
                    // at the leftover value). Stop reporting a phantom timer.
                    // (If a sequence legitimately pauses the global for this
                    // long, e.g. a long battle, it simply re-detects when the
                    // countdown resumes.)
                    EnterInactive("timer stalled -- sequence ended");
                }
                break;
            }

            case State::FROZEN: {
                // v0.18.3.237 (#75): if the engine dismisses the HUD while
                // frozen, stop pinning its memory and release the freeze —
                // the sequence is over; rewriting the global forever would
                // fight the engine for no visible timer.
                // v0.18.3.238: debounced, same as ACTIVE.
                if (HudDismissedDebounced(now)) {
                    EnterInactive("HUD timer dismissed while frozen (0x01D2B813=0 sustained)");
                    break;
                }
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
