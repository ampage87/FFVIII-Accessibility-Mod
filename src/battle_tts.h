// battle_tts.h - Battle sequence TTS for blind players
//
// Detects battle mode (game mode 999), reads the 0xD0 entity array,
// announces enemies, turns, HP changes, commands, and results.
//
// v0.10.01: Module skeleton + battle entry/exit detection.
#pragma once

#include <windows.h>
#include <cstdint>

namespace BattleTTS {

void Initialize();
void Update();
void Shutdown();

// v0.10.112: Returns the name of the character who last initiated Draw.
// Uses engine byte 0x01D768D4 (executing character party slot).
const char* GetLastDrawerName();

// v0.12.52: Return the raw executing slot byte for diagnostics.
uint8_t GetDrawExecutingSlot();

// v0.12.52: Validate Draw character by diffing magic inventories.
void ValidateDrawCharacter(uint8_t claimedSlot);

// v0.14.51: Cancel any in-flight no-effect watchdog (and any already-queued
// no-effect announcement) for the given slot. Called by modules that have
// produced an authoritative TTS announcement which superseeds the watchdog's
// fallback. Currently used by ScanTTS::OnScanCast — Scan goes through the
// same sub_48E830 player-magic action-announce path that records the
// watchdog snapshot, but Scan deals no HP / status / display change, so
// without cancellation the watchdog would queue a spurious 'No effect on
// <target>' ~6 s after the cast.
void CancelNoEffectWatchdogForSlot(int slot);

// v0.14.65: Non-blocking screenshot request. Sets the GL capture flag and
// returns immediately; the next SwapBuffers call writes basePath.bmp +
// basePath.png. Unlike the internal CaptureScreenshot() which blocks for up
// to 160 ms waiting for the render thread, this variant is safe to call
// from inside MinHook callbacks running on the game thread (e.g. the
// sub_B687C0 hook in scan_tts.cpp) without freezing the game. Used by
// ScanTTS to auto-capture the rendered Scan UI window for visual
// validation against the in-memory entity stats.
//
// v0.14.65.3: optional frameDelay parameter defers the capture by N swap
// frames so the caller can wait for FF8's typewriter text rendering to
// finish drawing all values before the framebuffer is read. 0 (default)
// preserves the v0.14.65 behavior — capture fires on the next swap.
// Typical scan-UI value: 90 (≈1.5 s at 60 fps), enough to let the
// description and stat-value typewriter complete.
void RequestScreenshotAsync(const char* basePath, int frameDelay = 0);

// v0.14.65.2: Return the absolute path to the diagnostic screenshots
// directory. Lets other modules (currently ScanTTS) compose paths that
// land alongside the existing kind4_*, poll_NEW_*, popup_time_* etc.
// diagnostic captures rather than getting scattered across the FS based
// on whatever the current working directory happens to be. The directory
// itself is created on demand by the existing capture mechanisms (the
// first SpriteScreenshot or KIND4 capture per battle calls
// CreateDirectoryA on this path), so callers don't need to.
const char* GetScreenshotDir();

}  // namespace BattleTTS

// ============================================================================
// Battle system memory constants (FF8 Steam 2013 EN, no ASLR)
// Source: ff8-speedruns/ff8-memory + deep research results
// ============================================================================

// Entity array: 7 x 0xD0 structs (slots 0-2 = allies, 3-6 = enemies)
static const uint32_t BATTLE_ENTITY_ARRAY_BASE = 0x1D27B18;
static const uint32_t BATTLE_ENTITY_STRIDE     = 0xD0;  // 208 bytes per entity
static const int      BATTLE_ALLY_SLOTS        = 3;
static const int      BATTLE_ENEMY_SLOTS       = 4;
static const int      BATTLE_TOTAL_SLOTS       = 7;

// Entity struct offsets (within each 0xD0 block)
static const uint32_t BENT_TIMED_STATUS_0   = 0x00;  // bitfield: Sleep/Haste/Slow/Stop/Regen/Protect/Shell/Reflect
static const uint32_t BENT_TIMED_STATUS_1   = 0x01;  // bitfield: Aura/Curse/Doom/Invincible/GradPetrify/Float/Confuse
static const uint32_t BENT_TIMED_STATUS_2   = 0x02;  // bitfield: Eject/Double/Triple/Defend
static const uint32_t BENT_TIMED_STATUS_3   = 0x03;  // bitfield: Vit0/AngelWing
static const uint32_t BENT_MAX_ATB          = 0x08;  // uint16 (ally) / uint32 (enemy)
static const uint32_t BENT_CUR_ATB          = 0x0C;  // uint16 (ally) / uint32 (enemy)
static const uint32_t BENT_CUR_HP           = 0x10;  // uint16 (ally) / uint32 (enemy)
static const uint32_t BENT_MAX_HP           = 0x14;  // uint16 (ally) / uint32 (enemy)
static const uint32_t BENT_ELEM_RESIST_BASE   = 0x3C;  // 8 x uint16 (Fire/Ice/Thunder/Earth/Poison/Wind/Water/Holy)
// v0.14.74: Status resistance block per `Plan & Research Documents/Scan spell deep research results.md`.
// 20 contiguous unsigned bytes immediately after the 8 x u16 elemental block (which ends at 0x4B).
// Order: Death, Poison, Petrify, Darkness, Silence, Berserk, Zombie, Sleep, Haste, Slow, Stop, Regen,
//        Reflect, Doom, Slow Petrify, Float, Confuse, Drain, Expulsion, ???
// Encoding: each byte adds to a 100 baseline to form StatusDefense in the inflict formula.
//   byte == 0   -> baseline (most vulnerable; "Weak to" candidate per the Scan UI)
//   byte >= 100 -> fully immune ("Strong vs" displayed by the Scan UI per the deep research's threshold)
static const uint32_t BENT_STATUS_RESIST_BASE = 0x4C;  // 20 x uint8 status resistances (see comment above)
static const uint32_t BENT_PERSIST_STATUS     = 0x78;  // bitfield: KO/Poison/Petrify/Blind/Silence/Berserk/Zombie
static const uint32_t BENT_LEVEL            = 0xB4;  // uint8
static const uint32_t BENT_STR              = 0xB5;  // uint8
static const uint32_t BENT_VIT              = 0xB6;  // uint8
static const uint32_t BENT_MAG              = 0xB7;  // uint8
static const uint32_t BENT_SPR              = 0xB8;  // uint8
static const uint32_t BENT_SPD              = 0xB9;  // uint8
static const uint32_t BENT_LCK              = 0xBA;  // uint8
static const uint32_t BENT_EVA              = 0xBB;  // uint8
static const uint32_t BENT_HIT              = 0xBC;  // uint8
static const uint32_t BENT_GF_SUMMON_FLAG   = 0x7C;  // uint16: non-zero when this character is summoning a GF
static const uint32_t BENT_CRISIS_LEVEL     = 0xC2;  // uint8 (0-4)

// Battle result addresses
static const uint32_t BATTLE_RESULT_STATE   = 0x1CFF6E7;  // uint8: 2=escaped, 4=won
static const uint32_t BATTLE_POST_SCREEN    = 0x1A78CA4;  // uint8: bool, true when in post-battle screen
static const uint32_t BATTLE_XP_ALLY1       = 0x1CFF574;  // uint16
static const uint32_t BATTLE_XP_ALLY2       = 0x1CFF576;  // uint16
static const uint32_t BATTLE_XP_ALLY3       = 0x1CFF578;  // uint16
static const uint32_t BATTLE_AP_EARNED      = 0x1CFF5C0;  // uint16
static const uint32_t BATTLE_PRIZE_BASE     = 0x1CFF5E0;  // 4 x {uint8 id, uint8 qty}
static const uint32_t BATTLE_ENCOUNTER_ID   = 0x1CFF6E0;  // uint16

// Draw spell slots (per enemy, 4 slots each, 0x47 stride between enemies)
static const uint32_t BATTLE_DRAW_BASE      = 0x1D28F18;
static const uint32_t BATTLE_DRAW_STRIDE    = 0x47;

// GF Boost gauge
static const uint32_t BATTLE_GF_BOOST       = 0x20DCEF0;  // uint8 (0-255)

// Zell Duel timer
static const uint32_t BATTLE_ZELL_TIMER     = 0x1D76750;  // uint16, countdown

// Battle char struct pointer (BYTE** — points to entity array)
static const uint32_t BATTLE_CHAR_STRUCT_PTR = 0x1D27B10;
