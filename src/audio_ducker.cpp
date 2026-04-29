// audio_ducker.cpp - Implementation of AudioDucker module.
// See audio_ducker.h for design rationale.

#include "audio_ducker.h"
#include <windows.h>
#include <math.h>

// Forward decl for cross-module logger.
namespace Log { void Mod(const char* format, ...); }

namespace AudioDucker {

// ============================================================================
// Internal types and state
// ============================================================================

struct Bus {
    GetVolumeFn getVol;       // User-volume reader (returns 0..1)
    SetVolumeFn setVol;       // Effective-volume writer (takes 0..1)
    BusConfig   cfg;          // dB / ms config (immutable after Initialize)
    float       targetLinear; // cfg.depthDb converted to linear, cached
    float       envelope;     // Current envelope: 1.0 = no duck, target = full duck
    DWORD       holdUntilTick; // GetTickCount() value before which release is blocked
};

static Bus s_bgm = {};
static Bus s_sfx = {};
static int  s_refCount    = 0;     // Reference count (BeginDuck / EndDuck)
static bool s_enabled     = true;  // Master enable (F2 toggle in FF8 mod)
static bool s_initialized = false;

// ============================================================================
// dB / linear conversion
// ============================================================================

float LinearFromDb(float dB)
{
    return powf(10.0f, dB / 20.0f);
}

// ============================================================================
// Bus initialization
// ============================================================================

static void InitBus(Bus& b, GetVolumeFn get, SetVolumeFn set, const BusConfig& cfg)
{
    b.getVol         = get;
    b.setVol         = set;
    b.cfg            = cfg;
    b.targetLinear   = LinearFromDb(cfg.depthDb);
    b.envelope       = 1.0f;
    b.holdUntilTick  = 0;
}

// ============================================================================
// Public API — lifecycle
// ============================================================================

void Initialize(
    GetVolumeFn getBgm, SetVolumeFn setBgm, const BusConfig& bgmCfg,
    GetVolumeFn getSfx, SetVolumeFn setSfx, const BusConfig& sfxCfg)
{
    InitBus(s_bgm, getBgm, setBgm, bgmCfg);
    InitBus(s_sfx, getSfx, setSfx, sfxCfg);
    s_refCount    = 0;
    s_enabled     = true;
    s_initialized = true;
    Log::Mod("AudioDucker: Initialized. "
             "BGM target=%.1fdB(%.3f) atk=%.0fms rel=%.0fms hold=%.0fms; "
             "SFX target=%.1fdB(%.3f) atk=%.0fms rel=%.0fms hold=%.0fms.",
             bgmCfg.depthDb, s_bgm.targetLinear, bgmCfg.attackMs, bgmCfg.releaseMs, bgmCfg.holdMs,
             sfxCfg.depthDb, s_sfx.targetLinear, sfxCfg.attackMs, sfxCfg.releaseMs, sfxCfg.holdMs);
}

void Shutdown()
{
    if (!s_initialized) return;
    s_initialized   = false;
    s_refCount      = 0;
    s_bgm.envelope  = 1.0f;
    s_sfx.envelope  = 1.0f;
    Log::Mod("AudioDucker: Shutdown.");
}

void SetEnabled(bool enabled)
{
    s_enabled = enabled;
    if (!enabled) {
        // Drop refcount and clear hold so any in-progress duck releases
        // immediately at the configured release tau (no instant snap).
        s_refCount           = 0;
        s_bgm.holdUntilTick  = 0;
        s_sfx.holdUntilTick  = 0;
    }
}

bool IsEnabled() { return s_enabled; }

// ============================================================================
// Public API — trigger
// ============================================================================

void BeginDuck()
{
    if (!s_initialized || !s_enabled) return;
    int prev = s_refCount;
    s_refCount++;
    if (prev == 0) {
        DWORD now = GetTickCount();
        s_bgm.holdUntilTick = now + (DWORD)s_bgm.cfg.holdMs;
        s_sfx.holdUntilTick = now + (DWORD)s_sfx.cfg.holdMs;
        Log::Mod("AudioDucker: BeginDuck (count 0->1)");
    }
}

void EndDuck()
{
    if (!s_initialized) return;
    if (s_refCount <= 0) return;  // Defensive: don't underflow
    s_refCount--;
    if (s_refCount == 0) {
        Log::Mod("AudioDucker: EndDuck (count 1->0)");
    }
}

bool IsActive()
{
    if (!s_initialized) return false;
    return (s_bgm.envelope < 0.999f) || (s_sfx.envelope < 0.999f) || (s_refCount > 0);
}

// ============================================================================
// Envelope math
// ============================================================================

// One-pole filter step. Returns new envelope. Frame-rate independent: the
// coefficient is derived from dt and tau on every call so the audible
// time constant matches the configured ms regardless of tick rate.
static float OnePoleStep(float current, float target, float dtMs, float tauMs)
{
    if (tauMs <= 0.0f) return target;  // Degenerate: snap.
    float coef = 1.0f - expf(-dtMs / tauMs);
    return current + (target - current) * coef;
}

static void TickBus(Bus& b, float dtMs, bool ducking)
{
    if (!b.getVol || !b.setVol) return;

    DWORD now = GetTickCount();
    bool inHold = (now < b.holdUntilTick);

    if (ducking) {
        // Refcount > 0: pull toward duck depth at attack rate.
        b.envelope = OnePoleStep(b.envelope, b.targetLinear, dtMs, b.cfg.attackMs);
    } else if (inHold) {
        // Refcount 0 but hold not yet expired: freeze envelope. Bridges
        // micro-pauses inside SAPI chunk boundaries.
        // (No envelope change; still apply current envelope to user vol below.)
    } else {
        // Refcount 0 and hold expired: release toward 1.0.
        b.envelope = OnePoleStep(b.envelope, 1.0f, dtMs, b.cfg.releaseMs);
    }

    // Clamp.
    if (b.envelope < 0.0f) b.envelope = 0.0f;
    if (b.envelope > 1.0f) b.envelope = 1.0f;

    // Apply: read user volume now (so F5–F8 changes mid-duck track) and
    // write effective volume.
    float userVol = b.getVol();
    float out = userVol * b.envelope;
    if (out < 0.0f) out = 0.0f;
    if (out > 1.0f) out = 1.0f;
    b.setVol(out);
}

void Tick(float dtMs)
{
    if (!s_initialized) return;

    // Sanitize dt (handles first-frame bootstrap, stalls, window unfocus).
    if (dtMs <= 0.0f)  dtMs = 1.0f;
    if (dtMs > 250.0f) dtMs = 250.0f;

    if (!s_enabled) {
        // Disabled, but still drive release on any in-progress duck so
        // disable doesn't leave audio silenced. ducking=false drives
        // toward 1.0 at release rate.
        if (s_bgm.envelope < 1.0f) TickBus(s_bgm, dtMs, false);
        if (s_sfx.envelope < 1.0f) TickBus(s_sfx, dtMs, false);
        return;
    }

    bool ducking = (s_refCount > 0);
    TickBus(s_bgm, dtMs, ducking);
    TickBus(s_sfx, dtMs, ducking);
}

}  // namespace AudioDucker
