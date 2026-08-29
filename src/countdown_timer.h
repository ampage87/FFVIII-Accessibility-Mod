// countdown_timer.h — Mission countdown timer accessibility (Dollet, Fire Cavern, etc.)
//
// Module owner: per-frame Update polls FF8 memory for an active mission
// timer. When detected, the module fires scheduled TTS announcements at
// boundaries (initial detection, every 5 minutes, 1 minute, 30 seconds)
// so a blind player can hear the time tick down without sighted help.
// T-key reads the remaining time on demand; Shift+T toggles a freeze.
//
// v0.15.12.0 — first read+announce implementation. Built on the
// research finding that **field var 724 ("Dollet mission time", a Word)
// at runtime address 0x01CFECCC is the persistent script-side snapshot
// of the timer**, shared between Dollet and Fire Cavern (same engine
// countdown manager, opcodes SETTIMER 0x09C / DISPTIMER 0x09D / GETTIMER
// 0x0A4 / KILLTIMER 0x0B9). The LIVE engine-side global is a separate
// address that requires either an in-mod scanner or an opcode-handler
// trace to locate; we rely on the snapshot for now and use the BAT to
// discover empirically how often it updates during the chase. See
// "Plan & Research Documents/Dollet timer countdown deep research results.md".
//
// Freeze in v0.15.12.0 is EXPERIMENTAL: we rewrite 0x01CFECCC each
// frame to the captured value. If the snapshot IS the live global, this
// freezes everything (HUD + game-over event); if it's just a periodic
// copy, the scheduler will appear frozen (since it reads from this
// address) but the in-game timer keeps ticking. The BAT outcome
// decides the v0.15.13 follow-up.
//
// Public API:
//   Initialize()        — module setup, called once from AccessibilityThread
//   Shutdown()          — cleanup, called once at thread exit
//   Update()            — per-frame poll (~60Hz); detects timer
//                          start/end, fires scheduled announcements,
//                          applies freeze when engaged, polls T/Shift+T
//   IsActive()          — true while a countdown is detected and
//                          running (also true while frozen)
//   AnnounceRemaining() — speak the current remaining time (called by
//                          the internal T-key handler)
//   ToggleFreeze()      — toggle the freeze flag (called by the
//                          internal Shift+T handler)
//   SetHold()           — v0.63.1 (#111): a PROGRAMMATIC freeze request,
//                          for a module that needs the clock to stop while
//                          it holds the player still. Aaron, on the space
//                          rescue's Game Controls screen: "The controls
//                          should essentially pause everything until the
//                          player hits Enter to proceed." The hold uses the
//                          same rewrite-every-frame machinery Shift+T uses,
//                          but silently, and it SURVIVES DETECTION ORDER:
//                          the space scene's clock is not detected until
//                          about two seconds after the field loads, so a
//                          hold placed before then is remembered and applied
//                          the instant the timer goes ACTIVE.
//   IsHeldFrozen()      — true only while a SetHold(true) is actually
//                          pinning the value. A caller that is waiting on
//                          the clock being stopped must be able to tell the
//                          difference between "held" and "asked to hold a
//                          timer that does not exist yet".

#pragma once

namespace CountdownTimer {
    void Initialize();
    void Shutdown();
    void Update();
    bool IsActive();
    void AnnounceRemaining();
    void ToggleFreeze();
    void SetHold(bool on, const char* reason);
    bool IsHeldFrozen();
}
