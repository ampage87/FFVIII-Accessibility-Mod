// field_pause.inl -- THE FIELD FREEZE. One byte, and the whole field stops.
//
// PART OF field_navigation.cpp -- TEXTUAL INCLUDE. Do NOT compile standalone.
// Compiled and driven on the host by tests/field_pause_test.cpp.
//
// ============================================================================
// WHY
// ============================================================================
//
// Aaron, on the space rescue's Game Controls screen: *"Is there no way we can
// suppress the actual scene behind the Game Controls screen, that way when the
// player presses enter that is when Rinoa begins to move?"*
//
// v0.63.x could not, and the cost is in his logs twice. The screen stopped the
// mission CLOCK -- `CountdownTimer::SetHold` pinning 0x01CFE92C -- but the
// scene ran on underneath it, so a screen he read for forty-six seconds was
// forty-six seconds of an approach he never got to fly. Both times attempt 1
// was judged while he was still closing, and both times attempt 2, which gets
// no screen, was won comfortably.
//
// ============================================================================
// THE ENGINE HAS EXACTLY ONE PAUSE POINT, AND IT IS A REAL ONE
// ============================================================================
//
// field_main_loop (0x0046FEE0) calls field_main (0x00471F70) like this:
//
//     00470016  mov eax, [0x01CE4A68]   ; the field-teardown flag
//     0047001B  cmp eax, edi            ; edi = 0
//     0047001D  mov eax, [0x01CD2EBC]   ; the pause-MENU state
//     00470022  jne 0x47002F
//     00470024  cmp eax, edi
//     00470026  jne 0x470033            ; -> the pause-menu path
//     00470028  call 0x00471F70         ; <- field_main
//     0047002D  jmp 0x470075
//
// Two things follow. First, EVERYTHING AFTER THAT CALL RUNS REGARDLESS: the
// frame present at 0x4700E0..0x47017A, the sound, the input, the frame counter
// at 0x01D9CDCC. "field_main is not called" is a state the engine is designed
// to sit in -- it is precisely what its own pause menu does. Second, the call
// pushes NO ARGUMENTS, so a one-byte 0xC3 at the entry is a complete and
// stack-safe implementation of "do nothing".
//
// And field_main is the whole field: the script VM, entity movement, and the
// window drawing. So with it stopped, `rinoa::default` cannot reach the verdict
// at index 160, the 2700-frame approach move (opcode 0xF3 at 147, waited on by
// 0xF5 at 159 -- 2700 frames at 30fps IS the ninety seconds) cannot advance,
// the countdown global is not decremented, and Rinoa does not move. Enter is
// the moment the scene starts.
//
// ============================================================================
// WHAT IT COSTS, AND WHY THAT IS ACCEPTABLE HERE
// ============================================================================
//
// The Garden battle did exactly this until v0.20.123 and its note says plainly
// that it worked. It was retired for one reason: window rendering hangs off the
// same function -- field_main -> 0x00471010 -> 0x0052BC00 -> 0x004A0880 -- so a
// frozen field cannot redraw the Game Controls box.
//
// Aaron, asked directly: *"If we lose the drawn box so be it."* The box was
// always a courtesy to a sighted onlooker; the spoken brief is the deliverable
// and it comes off the mod's own thread, which the freeze does not touch. The
// module opens the box, lets a few field frames run so it is drawn at least
// once, and only then freezes -- a fifth of a second of scene instead of forty
// six seconds of it.
//
// ============================================================================
// THE RULES THIS FILE OBEYS
// ============================================================================
//
// A byte written over the game's main loop is the most dangerous thing in this
// mod, because a restore that does not happen leaves the field frozen with no
// way out but closing the game. So:
//
//   1. NEVER PATCH WHAT WE DID NOT RECOGNISE. The five bytes at the entry must
//      read A1 64 4A CE 01 -- `mov eax, [0x01CE4A64]`, field_main's own state
//      machine load. Anything else and Engage refuses and says so.
//   2. NEVER PATCH BLIND. The address comes from FF8Addresses' own resolution
//      chain (main_loop -> field_main_loop+0x148), not from a constant.
//   3. RESTORE FROM EVERY EXIT. Enter, F9, the backstop, leaving the field,
//      Shutdown -- all go through Release, and Release is idempotent.
//   4. A WATCHDOG THAT DOES NOT NEED THE CALLER. FieldPause::Watchdog() runs
//      from PollBattlePauseResume, which sits ABOVE Update()'s on-field early
//      returns and runs even in battle, and force-releases after
//      FP_MAX_FREEZE_MS. If the owning module dies, throws, or simply forgets,
//      the field comes back on its own.
//   5. RESTORE THE ORIGINAL BYTE, not a constant. Read it before patching.

namespace FieldPause {

// The signature. field_main opens by loading its own state machine.
static const uint8_t FP_SIG[5] = { 0xA1, 0x64, 0x4A, 0xCE, 0x01 };
static const uint8_t FP_RET    = 0xC3;

// Nothing may hold the field for longer than this, whatever it thinks it is
// doing. Ten minutes is far past any screen a player will sit on and far short
// of "the game is broken and he does not know why".
static const DWORD FP_MAX_FREEZE_MS = 600000;

static bool    s_engaged  = false;
static uint8_t s_original = 0;
static DWORD   s_since    = 0;
static char    s_why[64]  = "";

static uint32_t Addr() { return FF8Addresses::field_main_fn; }

// Does the entry read the way field_main's entry should? SEH-guarded, because a
// half-resolved address is exactly the case this has to survive.
static bool SignatureOk()
{
    const uint32_t a = Addr();
    if (!a) return false;
    __try {
        const volatile uint8_t* p = (const volatile uint8_t*)a;
        for (int i = 0; i < 5; i++) if (p[i] != FP_SIG[i]) return false;
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

// Can this build be frozen at all? Callers use it to CHOOSE A STRATEGY before
// committing to one -- the space rescue opens its screen differently depending
// on the answer -- so it must be answerable without patching anything.
//
// v0.64.1: AND IT MUST STAY TRUE ONCE WE HAVE FROZEN. The signature it looks
// for is the UNPATCHED entry, so between Engage and Release the byte reads 0xC3
// and the check fails on its own handiwork. Aaron's 2026-08-23 20:36:42 log is
// what that costs: he pressed slash while the field was frozen and the repeat
// read him the fallback wording -- "the mission clock is held ... but the scene
// itself is not" -- flatly contradicting the brief he had just been given.
static bool Available()
{
    if (s_engaged) return true;
    return Addr() != 0 && SignatureOk();
}

static bool IsEngaged() { return s_engaged; }

// Writes one byte at field_main's entry. Returns false and touches nothing if
// the page will not turn writable or the write faults.
static bool WriteEntry(uint8_t value, uint8_t* previous)
{
    const uint32_t a = Addr();
    if (!a) return false;
    DWORD old = 0;
    if (!VirtualProtect((LPVOID)a, 1, PAGE_EXECUTE_READWRITE, &old)) return false;
    bool ok = true;
    __try {
        volatile uint8_t* p = (volatile uint8_t*)a;
        if (previous) *previous = *p;
        *p = value;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { ok = false; }
    DWORD back = 0;
    VirtualProtect((LPVOID)a, 1, old, &back);
    return ok;
}

// field_main's own state machine, the byte it loads on its first instruction.
// 0-3 are the load path; the steady per-frame states are above that. Logged at
// Engage rather than gated on, because v0.64.1 is the first build that will
// report it and a gate on a number nobody has measured is a gate that can
// refuse for ever. See the v0.64.1 note in field_disc3_space.inl for what
// actually keeps the freeze out of the load path.
static const uintptr_t FP_STATE_ADDR = 0x01CE4A64u;
static int FieldMainState()
{
    __try { return (int)*(const volatile uint32_t*)FP_STATE_ADDR; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return -1; }
}

static bool Engage(const char* why)
{
    if (s_engaged) return true;
    if (!Available()) {
        Log::Field("FieldNavigation: [FIELDPAUSE] refused (%s): field_main=0x%08X "
                   "and the entry does not read A1 64 4A CE 01",
                   why ? why : "?", (unsigned)Addr());
        return false;
    }
    uint8_t prev = 0;
    if (!WriteEntry(FP_RET, &prev)) {
        Log::Field("FieldNavigation: [FIELDPAUSE] refused (%s): the write failed",
                   why ? why : "?");
        return false;
    }
    s_original = prev;
    s_engaged  = true;
    const DWORD now = GetTickCount();
    s_since = now ? now : 1;
    snprintf(s_why, sizeof s_why, "%s", why ? why : "?");
    Log::Field("FieldNavigation: [FIELDPAUSE] ENGAGED (%s): 0x%08X 0x%02X -> 0xC3 "
               "with field_main in state %d. The field is stopped: scripts, "
               "movement, the countdown, all of it.",
               s_why, (unsigned)Addr(), (unsigned)prev, FieldMainState());
    return true;
}

static void Release(const char* why)
{
    if (!s_engaged) return;
    // Put back what was there, not what we assume was there.
    const uint8_t restore = s_original ? s_original : FP_SIG[0];
    uint8_t now = 0;
    const bool ok = WriteEntry(restore, &now);
    s_engaged = false;
    Log::Field("FieldNavigation: [FIELDPAUSE] released (%s) after %u ms: "
               "0x%02X -> 0x%02X%s",
               why ? why : "?", (unsigned)(GetTickCount() - s_since),
               (unsigned)now, (unsigned)restore, ok ? "" : "  *** WRITE FAILED ***");
    s_since = 0;
    s_original = 0;
    s_why[0] = '\0';
}

// Called every mod tick from PollBattlePauseResume -- above Update()'s on-field
// early returns, and above the game-mode gate, so it runs even if the owning
// module has stopped being called at all. This is the guarantee that a bug in
// the caller cannot strand the player in a field that never advances.
static void Watchdog()
{
    if (!s_engaged) return;
    if (GetTickCount() - s_since < FP_MAX_FREEZE_MS) return;
    Log::Field("FieldNavigation: [FIELDPAUSE] WATCHDOG: held %u ms by '%s' -- "
               "past the %u ms ceiling. Releasing whatever the owner thinks.",
               (unsigned)(GetTickCount() - s_since), s_why,
               (unsigned)FP_MAX_FREEZE_MS);
    Release("watchdog");
}

} // namespace FieldPause
