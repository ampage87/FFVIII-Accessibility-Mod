// battle_countdown_model.inl -- THE DIGIT THAT FLOATS OVER A BATTLE ACTOR
//
// v0.108.0 (#megaflare). Aaron: "Can you go into the game exe and field files
// to see if you can pin down the exact function for displaying this countdown
// digit? We need to identify it so when it appears we can have the mod say
// 'Alert, Countdown #' where # is the digit shown on screen."
//
// It is pinned down. `sub_50A500` is the ONLY routine in FF8_EN.exe that draws
// a bare one- or two-digit number at a screen position taken from an actor's
// sprite -- the whole battle module blits glyphs through exactly one primitive
// (`sub_4A93D0`) and it has five call sites in the entire executable: three in
// the damage-popup renderer `sub_5069B0`, and two inside `sub_50A500`.
//
//     0x0050A500:  mov      eax, dword ptr [esp + 8]     ; the value, in FRAMES
//     0x0050A504:  cmp      eax, 0xfffffba9              ; -1111 -> draw nothing
//     0x0050A509:  je       0x50a5e8
//     0x0050A50F:  test     eax, eax
//     0x0050A511:  jge      0x50a515
//     0x0050A513:  xor      eax, eax                     ; negative draws as 0
//     0x0050A517:  lea      esi, [eax + 0xa]
//     0x0050A51A:  mov      eax, 0x88888889
//     0x0050A51F:  imul     esi
//     0x0050A52A:  sar      ecx, 4                       ; ecx = (value + 10) / 30
//     0x0050A53B:  cmp      ecx, 0xa
//     0x0050A53E:  jl       0x50a583                     ; under ten -> ONE digit
//
// So the number on screen is **(frames + 10) / 30** -- seconds, rounded -- and
// it is a single digit whenever that is under ten. Which is exactly Aaron's
// "just displayed as a single digit and very briefly".
//
// Its only caller is `sub_50A410`, which projects the actor's own sprite
// position, raises it 8 pixels, and draws up to two counters stacked above it:
//
//     0x0050A466:  test     ah, 0x20                          ; actorFlags & 0x2000
//     0x0050A478:  push     0xffffff                          ; WHITE
//     0x0050A494:  movsx    edx, word ptr [ecx + 0x1d27b7c]   ; entity + 0x64
//     0x0050A49D:  call     0x50a500
//     0x0050A4A5:  add      word ptr [esi + 0xa], -8          ; stack the next one
//     0x0050A4AA:  test     dword ptr [edi + 8], 0x80000      ; actorFlags & 0x80000
//     0x0050A4C2:  push     0xff                              ; BLUE
//     0x0050A4DA:  movsx    eax, word ptr [edx + 0x1d27b78]   ; entity + 0x60
//     0x0050A4E3:  call     0x50a500
//
// `0x01D27B18` is the battle entity array (stride 0xD0), and `BENT_STATUS_TIMERS`
// is +0x4C: fourteen int16 durations indexed by status bit, the array this mod
// already reads for Aura and already holds through TTS (v0.37.0, #95). So:
//
//     entity + 0x60 = 0x4C + 10*2 -> status bit 10
//     entity + 0x64 = 0x4C + 12*2 -> status bit 12
//
// and against the status table this project already has verified
// (battle_status.inl, BENT_TIMED_STATUS_1: 0x01 Aura = bit 8, 0x02 Curse,
// 0x04 Doom, 0x08 Invincible, 0x10 Gradual Petrify, 0x20 Float):
//
//     **bit 10 = Doom.  bit 12 = Gradual Petrify.**
//
// Confirmed a second way by what the ticker does when each expires --
// `0x004834B7 test bh,4` (bit 10) calls `sub_484720(entity, 5, 0)`, and
// `0x004834C5 test bh,0x10` (bit 12) does `or byte [entity+0x78], 4`, the
// Petrify bit. Those are the only two statuses in FF8 that put a number over
// anyone's head, and the engine draws no other bare digit anywhere.
//
// WHAT THIS DOES NOT SETTLE, and the honest statement of it: the 2026-08-26
// Bahamut log records NO Doom and NO Gradual Petrify on anybody. Either the
// mod's status diff lost it inside the ten-second animation hold, or the digit
// Aaron saw is a numbered TEXTURE inside the Mega Flare effect, which no text
// or number path in the executable would ever touch. This model reads the one
// thing the engine can draw; the screenshot burst added in v0.107.0 answers
// the other half in the same BAT.

// The two status-timer slots the engine actually renders. Nothing else in the
// fourteen is ever drawn -- Haste and Protect tick down invisibly.
static const int BC_TIMER_DOOM            = 10;
static const int BC_TIMER_GRADUAL_PETRIFY = 12;

// 0xFBA9 read back through a signed 16-bit load. The engine's own "this status
// has no timer" sentinel, tested at 0x0050A504 before anything else.
static const int BC_NO_TIMER = -1111;

// The engine draws a counter when the slot is not the sentinel. A negative
// value is not the sentinel and does draw -- as 0 -- so it counts as live.
static bool BcTimerLive(int raw)
{
    return raw != BC_NO_TIMER;
}

// The number on screen, arithmetic for arithmetic with 0x0050A50F..0x0050A52A.
static int BcDigitFor(int raw)
{
    const int v = (raw < 0) ? 0 : raw;
    return (v + 10) / 30;
}

// True for the two slots above and nothing else, so a future caller cannot
// point this at Haste and expect a number.
static bool BcSlotIsDrawn(int bitIndex)
{
    return bitIndex == BC_TIMER_DOOM || bitIndex == BC_TIMER_GRADUAL_PETRIFY;
}

// THE ENGINE'S OWN DRAW GATE, AND WHY v0.110.0 HONOURS IT.
//
// v0.108.0 read the flag, logged it, and deliberately let the timer alone
// decide -- the reasoning being that a counter momentarily clipped off screen
// is still a counter the player needs. The 2026-08-27 BAT priced that
// reasoning:
//
//   [18:27:59] [COUNTDOWN] slot0 Doom raw=0 digit=0 actorFlags=00000000 drawn=0 -- "Alert, Countdown 0"
//   ... and thirteen more, every slot, both bits, in one frame
//
// At battle entry the timer array has not been written yet, so all fourteen
// int16 read back as 0 -- which is not the sentinel, so every one of them is
// "live", and every one of them draws as (0 + 10) / 30 = 0. Three battles in
// that session opened with the mod interrupting itself to say "Alert, Countdown
// 0". The engine drew none of them: actorFlags=00000000 on all fourteen.
//
// So the flag is not a fidelity nicety, it is the difference between a status
// and an uninitialised word. sub_50A410 tests it before it reads the timer at
// all, and a counter with the flag clear is not on screen by definition. The
// mod now tests exactly what the engine tests -- and logs, rate-limited, any
// timer that is live while the flag is clear, so a wrong actor base would show
// up as a stream of held lines rather than as silence.
static const bool BC_REQUIRE_ACTOR_FLAG = true;

static bool BcIsDrawn(bool live, bool flagsOk, unsigned actorFlags, unsigned wantFlag)
{
    if (!live) return false;
    if (!BC_REQUIRE_ACTOR_FLAG) return true;
    if (!flagsOk) return false;
    return (actorFlags & wantFlag) != 0;
}

// Speak on the first frame the counter exists, and again every time the number
// on screen changes -- which is once a second, because the drawn value is
// seconds. Never while it is absent, and never for a repeat of a number that
// is already standing.
static bool BcShouldAnnounce(bool live, int digit, bool wasLive, int lastDigit)
{
    if (!live) return false;
    if (!wasLive) return true;
    return digit != lastDigit;
}

// Aaron's words, verbatim: "have the mod say 'Alert, Countdown #' where # is
// the digit shown on screen."
static void BcAnnounceText(char* buf, size_t n, int digit)
{
    if (buf == nullptr || n == 0) return;
    if (digit < 0) digit = 0;
    snprintf(buf, n, "Alert, Countdown %d", digit);
}

// The name for the log line. Not spoken -- Aaron asked for the digit and only
// the digit, so the utterance stays four words long and lands before the next
// thing the battle wants to say.
static const char* BcSlotName(int bitIndex)
{
    if (bitIndex == BC_TIMER_DOOM) return "Doom";
    if (bitIndex == BC_TIMER_GRADUAL_PETRIFY) return "Gradual Petrify";
    return "?";
}
