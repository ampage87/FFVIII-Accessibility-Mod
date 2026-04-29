// game_audio.cpp - Centralized game audio control for FF8 Accessibility Mod
//
// v0.09.22: Extracted from dinput8.cpp.
// v0.09.24: Direct nxAudioEngine.setMusicVolume call — bypasses FFNx hold flag.
// v0.14.45: Added SFX volume hook + audio-ducking toggle (F2 Phase 1).
// v0.14.46: SFX hook rewritten. v0.14.45 BAT showed F5/F6 had no audible
//   effect because TryInstallSfxHook returned silently every frame at the
//   `if (pFunc[0] != 0xE9) return;` check. FFNx only patches
//   sfx_set_master_volume when use_external_sfx=true in FFNx.toml; Aaron has
//   it false, so FFNx never replaces the function and the 0xE9 check failed
//   forever. Disassembly of sfx_set_master_volume at 0x0046A390 also showed
//   the function expects volume 0-100 (cmp eax, 0x64; jbe), NOT 0-127.
//   Fix: hook the game function directly with MinHook regardless of FFNx
//   state. Works for both use_external_sfx modes — MinHook trampolines the
//   first 5 bytes whether they're a normal prologue (FFNx didn't patch) or
//   a JMP (FFNx did patch). Either way we call the original via trampoline
//   with our scaled 0-100 value, and the original function stores to
//   *pMasterSfxVolume@0x01CD1794 then loops over channels to propagate the
//   new volume to in-flight SFX. No direct nxAudioEngine call needed.
//
// BGM Volume Architecture (v0.09.24):
//   The game's set_midi_volume is replaced by FFNx with set_music_volume_for_channel.
//   That function has a hold_volume_for_channel flag that silently blocks volume
//   changes during track loads. The game also never calls it during normal field play.
//
//   Our solution: at hook install time, scan FFNx's set_music_volume_for_channel
//   bytes to find the nxAudioEngine global address and the setMusicVolume method
//   address. Then call setMusicVolume DIRECTLY, bypassing the hold flag entirely.
//
//   FFNx's set_music_volume_for_channel does:
//     if (hold_volume_for_channel[channel]) return 1;  // WE BYPASS THIS
//     nxAudioEngine.setMusicVolume(volume / 127.0f, channel);
//
//   We extract nxAudioEngine ptr from: MOV ECX, imm32 (B9 xx xx xx xx)
//   We extract setMusicVolume addr from: CALL rel32 (E8 xx xx xx xx)
//   Then call it directly via __fastcall thiscall shim.
//
// SFX Volume Architecture (v0.14.46):
//   Hook game-side sfx_set_master_volume at 0x0046A390 directly with MinHook,
//   regardless of FFNx config. Volume range is 0-100. The function itself
//   stores to *pMasterSfxVolume@0x01CD1794 (verified at +0x46) then iterates
//   the channel array at 0x01cd0b00 (stride 0x24) calling per-channel update
//   functions to propagate the new master to currently-playing SFX. We call
//   the original via MinHook trampoline with our user-scaled value (0-100),
//   and rely on the function to do everything else.
//
//   Why not direct write to *pMasterSfxVolume? It would only affect NEW SFX;
//   in-flight ones (e.g. GF roar) keep their original mix-down volume until
//   they restart. The function call updates both.
//
//   Why not the v0.14.45 nxAudioEngine direct-call path? It only existed
//   when FFNx replaces the function (use_external_sfx=true). Hooking the
//   game function works for both true and false because MinHook trampolines
//   the first 5 bytes whether normal prologue or FFNx's E9 JMP.

#include "ff8_accessibility.h"
#include "ff8_addresses.h"
#include "game_audio.h"
#include "audio_ducker.h"
#include "minhook/include/MinHook.h"

// Forward declarations for cross-module namespaces (deleted from earlier and
// restored in v0.14.26 build recovery).
namespace Log { void Mod(const char* format, ...); }
namespace ScreenReader { bool Speak(const char* text, bool interrupt = false); bool IsSpeaking(); }
namespace Config { void Load(); int GetInt(const char* key, int defaultValue); void SetInt(const char* key, int value); }

namespace GameAudio {

// ============================================================================
// Internal state — BGM
// ============================================================================

typedef uint32_t (__cdecl *FF8SetMusicVolumeForChannel_t)(int32_t channel, uint32_t volume);
static FF8SetMusicVolumeForChannel_t s_originalSetMusicVolumeForChannel = nullptr;
static void* s_hookedFuncAddr = nullptr;
static bool s_hookInstalled = false;

static float s_bgmVolume = 0.1f;  // User's desired BGM volume (0.0-1.0). Default 10%.

// Track the last volume the game requested per channel
static uint32_t s_lastGameVolume[2] = { 127, 127 };

// Periodic re-application timer
static DWORD s_lastReapplyTick = 0;
static const DWORD REAPPLY_INTERVAL_MS = 500;

// ============================================================================
// Internal state — SFX (v0.14.45)
// ============================================================================

// FFNx's replacement (when use_external_sfx=true) and game's native function
// (when use_external_sfx=false) both take a single uint32_t volume in 0-100
// range. Verified via FFNx ff8_sfx_set_master_volume source and game
// disassembly at 0x0046A390 (cmp eax, 0x64; jbe).
typedef void (__cdecl *FF8SfxSetMasterVolume_t)(uint32_t volume);
static FF8SfxSetMasterVolume_t s_originalSfxSetMasterVolume = nullptr;
static void* s_sfxHookedFuncAddr = nullptr;
static bool  s_sfxHookInstalled = false;

static float s_sfxVolume = 1.0f;  // User's desired SFX volume (0.0-1.0). Default 100%.
static uint32_t s_lastGameSfxVolume = 100;

// v0.14.46: nxAudioEngine direct-call path retired for SFX. The MinHook
// trampoline path works whether FFNx patched the function or not, and
// updates in-flight SFX via the game function's channel-iteration loop.

// ============================================================================
// Internal state — audio ducking (v0.14.45 Phase 1)
// ============================================================================

static bool s_duckEnabled = true;     // F2 toggle, persisted as `tts_duck_enabled`
static int  s_duckRatioPct = 30;      // 0-100 percent, persisted as `sfx_duck_ratio`

// ============================================================================
// Direct nxAudioEngine access (bypasses FFNx hold_volume_for_channel)
// ============================================================================

// NxAudioEngine::setMusicVolume(float volume, int channel, double time = 0)
// MSVC x86 __thiscall: ECX = this, stack = (float, int, double)
// We call via __fastcall shim: first arg -> ECX, second (dummy) -> EDX, rest on stack.
typedef void (__fastcall *SetMusicVolumeFn)(void* pThis, void* edx, float volume, int channel, double time);

// SoLoud::Soloud::fadeVolume(handle, float toVolume, double time)
// With time=0, this is equivalent to setVolume (immediate).
typedef void (__fastcall *SoLoudFadeVolumeFn)(void* pEngine, void* edx, uint32_t handle, float volume, double time);

static void*                s_nxAudioEngine = nullptr;      // Address of FFNx's global nxAudioEngine object
static SetMusicVolumeFn     s_fnSetMusicVolume = nullptr;    // Address of NxAudioEngine::setMusicVolume
static SoLoudFadeVolumeFn   s_fnSoLoudFadeVolume = nullptr;  // Address of SoLoud::Soloud::fadeVolume
static uint32_t             s_engineOffset = 0;              // Offset of _engine within nxAudioEngine
static uint32_t             s_streamHandleOffset = 0;        // Offset of _currentStream.handle within nxAudioEngine
static bool                 s_directCallAvailable = false;   // True if nxAudioEngine + setMusicVolume found
static bool                 s_fmvVolumeAvailable = false;    // True if SoLoud addresses found for FMV control

// Scan FFNx's set_music_volume_for_channel bytes to extract the nxAudioEngine
// pointer and setMusicVolume method address. Must be called BEFORE MinHook
// patches the function (MinHook overwrites the first 5+ bytes).
static void ExtractNxAudioEngineAddresses(void* ffnxFunc)
{
    __try {
        uint8_t* code = (uint8_t*)ffnxFunc;
        uint32_t funcBase = (uint32_t)(uintptr_t)ffnxFunc;

        // Dump first 128 bytes for diagnostic
        Log::Mod("GameAudio: [SCAN] FFNx set_music_volume_for_channel at 0x%08X, first 128 bytes:", funcBase);
        for (int row = 0; row < 8; row++) {
            int off = row * 16;
            Log::Mod("GameAudio: [SCAN] +%02X: %02X %02X %02X %02X %02X %02X %02X %02X  "
                       "%02X %02X %02X %02X %02X %02X %02X %02X",
                       off,
                       code[off+0],  code[off+1],  code[off+2],  code[off+3],
                       code[off+4],  code[off+5],  code[off+6],  code[off+7],
                       code[off+8],  code[off+9],  code[off+10], code[off+11],
                       code[off+12], code[off+13], code[off+14], code[off+15]);
        }

        // Scan for MOV ECX, imm32 (B9) followed by CALL rel32 (E8).
        // The MOV ECX loads the nxAudioEngine 'this' pointer.
        // The CALL invokes setMusicVolume.
        // There may be other B9 instructions (trace/debug), so we look for the
        // pattern where B9 is followed within ~30 bytes by E8 targeting FFNx code.
        uint32_t ffnxBase = funcBase & 0xFF000000;  // FFNx DLL base estimate (e.g. 0x64000000)

        for (int i = 0; i < 220; i++) {
            if (code[i] != 0xB9) continue;

            uint32_t candidateThis = *(uint32_t*)(code + i + 1);
            // nxAudioEngine is a global in FFNx's DLL data section or heap.
            // Validate: should be a plausible data address.
            if (candidateThis < 0x00400000 || candidateThis > 0x7F000000) continue;

            // Look for CALL rel32 within the next 40 bytes after MOV ECX
            for (int j = i + 5; j < i + 40 && j < 250; j++) {
                if (code[j] != 0xE8) continue;

                int32_t rel = *(int32_t*)(code + j + 1);
                uint32_t callTarget = funcBase + j + 5 + rel;

                // Validate: call target should be in a plausible code range
                // (same DLL as the function we're scanning)
                if (callTarget < (funcBase - 0x01000000) || callTarget > (funcBase + 0x01000000)) continue;

                // Found a valid MOV ECX, imm32 + CALL pattern
                Log::Mod("GameAudio: [SCAN] +%02X: MOV ECX, 0x%08X (nxAudioEngine candidate)", i, candidateThis);
                Log::Mod("GameAudio: [SCAN] +%02X: CALL 0x%08X (setMusicVolume candidate)", j, callTarget);

                s_nxAudioEngine = (void*)(uintptr_t)candidateThis;
                s_fnSetMusicVolume = (SetMusicVolumeFn)(uintptr_t)callTarget;
                s_directCallAvailable = true;

                Log::Mod("GameAudio: Direct nxAudioEngine access ENABLED: "
                           "nxAudioEngine=0x%08X setMusicVolume=0x%08X",
                           candidateThis, callTarget);
                return;
            }
        }

        Log::Mod("GameAudio: [SCAN] WARNING: Could not find nxAudioEngine pattern. "
                   "Falling back to original function call (subject to hold flag).");
    }
    __except(EXCEPTION_EXECUTE_HANDLER) {
        Log::Mod("GameAudio: [SCAN] Exception scanning FFNx function bytes.");
    }
}

// After finding nxAudioEngine + setMusicVolume, scan a wide range of FFNx code
// for ALL calls to nxAudioEngine methods. This catalogs every method address so
// we can identify setStreamMasterVolume for FMV volume control.
static void ScanAllNxAudioEngineMethods(uint32_t knownFuncAddr)
{
    if (!s_nxAudioEngine) return;

    // Build the 5-byte pattern: B9 <nxAudioEngine 4 bytes>
    uint8_t pattern[5];
    pattern[0] = 0xB9;
    uint32_t nxAddr = (uint32_t)(uintptr_t)s_nxAudioEngine;
    memcpy(pattern + 1, &nxAddr, 4);

    // Scan range: knownFuncAddr +/- 1MB (covers FFNx's code section)
    uint32_t scanStart = knownFuncAddr - 0x100000;
    uint32_t scanEnd   = knownFuncAddr + 0x100000;

    Log::Mod("GameAudio: [METHODS] Scanning 0x%08X-0x%08X for MOV ECX, 0x%08X (B9 %02X %02X %02X %02X)",
               scanStart, scanEnd, nxAddr, pattern[1], pattern[2], pattern[3], pattern[4]);

    // Collect unique call targets (static so stream trace can reference them)
    static const int MAX_TARGETS = 64;
    static uint32_t targets[MAX_TARGETS];
    static uint32_t callerOffsets[MAX_TARGETS];
    static int targetCount = 0;
    targetCount = 0;  // Reset for this scan

    __try {
        for (uint32_t addr = scanStart; addr < scanEnd - 50; addr++) {
            uint8_t* p = (uint8_t*)(uintptr_t)addr;

            // Match B9 <nxAudioEngine>
            if (p[0] != pattern[0] || p[1] != pattern[1] || p[2] != pattern[2] ||
                p[3] != pattern[3] || p[4] != pattern[4]) continue;

            // Found MOV ECX, nxAudioEngine — look for CALL within next 40 bytes
            for (int j = 5; j < 40; j++) {
                if (p[j] != 0xE8) continue;

                int32_t rel = *(int32_t*)(p + j + 1);
                uint32_t callTarget = addr + j + 5 + rel;

                // Validate range
                if (callTarget < scanStart || callTarget > scanEnd + 0x100000) continue;

                // Check if we already have this target
                bool dup = false;
                for (int k = 0; k < targetCount; k++) {
                    if (targets[k] == callTarget) { dup = true; break; }
                }

                if (!dup && targetCount < MAX_TARGETS) {
                    targets[targetCount] = callTarget;
                    callerOffsets[targetCount] = addr;
                    targetCount++;
                }
                break; // Only take the first CALL after each MOV ECX
            }
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        Log::Mod("GameAudio: [METHODS] Exception during scan (found %d so far)", targetCount);
    }

    Log::Mod("GameAudio: [METHODS] Found %d unique NxAudioEngine method targets:", targetCount);
    for (int i = 0; i < targetCount; i++) {
        const char* label = "";
        if (targets[i] == (uint32_t)(uintptr_t)s_fnSetMusicVolume) label = " <-- setMusicVolume (KNOWN)";
        Log::Mod("GameAudio: [METHODS]   #%02d: 0x%08X (caller at 0x%08X)%s",
                   i, targets[i], callerOffsets[i], label);
    }

    // --- Identify setStreamMasterVolume from stop_movie chain ---
    // FFNx replaces game's stop_movie. We trace: stop_movie (game) -> JMP to FFNx's
    // ff8_stop_movie -> calls ffmpeg_stop_movie -> calls nxAudioEngine.stopStream().
    // Then we use the method catalog to find setStreamMasterVolume nearby.
    //
    // game's stop_movie is at get_relative_call(update_movie_sample, 0x3E2).
    // update_movie_sample = get_relative_call(start_movie, 0x74)
    // start_movie = get_relative_call(opcode_movie, 0xC3)
    // opcode_movie = execute_opcode_table[0x4F]
    __try {
        uint32_t opcode_movie = FF8Addresses::pExecuteOpcodeTable[0x4F];
        uint32_t start_movie_game = 0;
        uint32_t update_movie_sample_game = 0;
        uint32_t stop_movie_game = 0;
        
        // Resolve start_movie from opcode_movie + 0xC3
        {
            uint8_t* c = (uint8_t*)(uintptr_t)opcode_movie;
            if (c[0xC3] == 0xE8) {
                int32_t r = *(int32_t*)(c + 0xC4);
                start_movie_game = opcode_movie + 0xC3 + 5 + r;
            }
        }
        if (start_movie_game) {
            uint8_t* c = (uint8_t*)(uintptr_t)start_movie_game;
            if (c[0x74] == 0xE8) {
                int32_t r = *(int32_t*)(c + 0x75);
                update_movie_sample_game = start_movie_game + 0x74 + 5 + r;
            }
        }
        if (update_movie_sample_game) {
            uint8_t* c = (uint8_t*)(uintptr_t)update_movie_sample_game;
            if (c[0x3E2] == 0xE8) {
                int32_t r = *(int32_t*)(c + 0x3E3);
                stop_movie_game = update_movie_sample_game + 0x3E2 + 5 + r;
            }
        }
        Log::Mod("GameAudio: [STREAM] opcode_movie=0x%08X start_movie=0x%08X update_movie_sample=0x%08X stop_movie=0x%08X",
                   opcode_movie, start_movie_game, update_movie_sample_game, stop_movie_game);

        // stop_movie should have been replaced by FFNx with a JMP (E9)
        if (stop_movie_game) {
            uint8_t* sm = (uint8_t*)(uintptr_t)stop_movie_game;
            if (sm[0] == 0xE9) {
                int32_t r = *(int32_t*)(sm + 1);
                uint32_t ff8StopMovie = stop_movie_game + 5 + r;
                Log::Mod("GameAudio: [STREAM] stop_movie JMP -> ff8_stop_movie at 0x%08X", ff8StopMovie);

                // Scan ff8_stop_movie for B9 <nxAudioEngine> + E8 pattern
                // ff8_stop_movie calls ffmpeg_stop_movie() which calls nxAudioEngine.stopStream()
                // But the nxAudioEngine call may be inside ffmpeg_stop_movie, not ff8_stop_movie directly.
                // First scan ff8_stop_movie for CALL instructions to find ffmpeg_stop_movie.
                uint8_t* fsm = (uint8_t*)(uintptr_t)ff8StopMovie;
                Log::Mod("GameAudio: [STREAM] ff8_stop_movie first 64 bytes:");
                for (int row2 = 0; row2 < 4; row2++) {
                    int off2 = row2 * 16;
                    Log::Mod("GameAudio: [STREAM] +%02X: %02X %02X %02X %02X %02X %02X %02X %02X  "
                               "%02X %02X %02X %02X %02X %02X %02X %02X",
                               off2,
                               fsm[off2+0],  fsm[off2+1],  fsm[off2+2],  fsm[off2+3],
                               fsm[off2+4],  fsm[off2+5],  fsm[off2+6],  fsm[off2+7],
                               fsm[off2+8],  fsm[off2+9],  fsm[off2+10], fsm[off2+11],
                               fsm[off2+12], fsm[off2+13], fsm[off2+14], fsm[off2+15]);
                }
                // Scan for CALL instructions and B9 pattern
                for (int k = 0; k < 200; k++) {
                    if (fsm[k] == 0xE8) {
                        int32_t r2 = *(int32_t*)(fsm + k + 1);
                        uint32_t callTgt = ff8StopMovie + k + 5 + r2;
                        Log::Mod("GameAudio: [STREAM] ff8_stop_movie+%02X: CALL 0x%08X", k, callTgt);
                        
                        // Check if the call target contains B9 <nxAudioEngine> (it's ffmpeg_stop_movie -> stopStream)
                        uint8_t* ct = (uint8_t*)(uintptr_t)callTgt;
                        for (int m = 0; m < 30; m++) {
                            if (ct[m] == pattern[0] && ct[m+1] == pattern[1] && ct[m+2] == pattern[2] &&
                                ct[m+3] == pattern[3] && ct[m+4] == pattern[4]) {
                                // Found nxAudioEngine in the called function — look for CALL after it
                                for (int n = m + 5; n < m + 30; n++) {
                                    if (ct[n] == 0xE8) {
                                        int32_t r3 = *(int32_t*)(ct + n + 1);
                                        uint32_t streamMethod = callTgt + n + 5 + r3;
                                        Log::Mod("GameAudio: [STREAM] Found stopStream candidate: 0x%08X (from ffmpeg func at 0x%08X+%02X)",
                                                   streamMethod, callTgt, n);
                                        
                                        // setStreamMasterVolume is declared 4 methods after stopStream in audio.h:
                                        // stopStream, pauseStream, resumeStream, isStreamPlaying, getStreamMasterVolume, setStreamMasterVolume
                                        // Find it in the method catalog by looking for the entry closest to but after stopStream
                                        // that has been seen called from the movie/stream context
                                        for (int t = 0; t < targetCount; t++) {
                                            if (targets[t] == streamMethod) {
                                                Log::Mod("GameAudio: [STREAM] stopStream confirmed as method #%02d", t);
                                            }
                                        }

                                        // Extract SoLoud offsets from stopStream bytes:
                                        //   +13: 8B 86 xx xx xx xx = MOV EAX,[ESI+offset] -> _currentStream.handle
                                        //   +19: 8D 4E xx          = LEA ECX,[ESI+xx] -> _engine offset
                                        //   +30: E8 xx xx xx xx    = CALL fadeVolume
                                        {
                                            uint8_t* ss = (uint8_t*)(uintptr_t)streamMethod;
                                            // Scan for MOV reg,[ESI+imm32] to find handle offset
                                            // and LEA ECX,[ESI+imm8] to find engine offset
                                            // and first CALL to find fadeVolume
                                            uint32_t foundHandleOff = 0, foundEngineOff = 0;
                                            uint32_t foundFadeVolume = 0;
                                            for (int s2 = 0; s2 < 80; s2++) {
                                                // MOV reg, [ESI + imm32]: 8B 86/8E/96/9E/A6/AE/BE xx xx xx xx
                                                if (ss[s2] == 0x8B && (ss[s2+1] & 0xC7) == 0x86 && !foundHandleOff) {
                                                    foundHandleOff = *(uint32_t*)(ss + s2 + 2);
                                                }
                                                // LEA ECX, [ESI + imm8]: 8D 4E xx
                                                if (ss[s2] == 0x8D && ss[s2+1] == 0x4E && !foundEngineOff) {
                                                    foundEngineOff = ss[s2+2];
                                                }
                                                // First CALL rel32
                                                if (ss[s2] == 0xE8 && !foundFadeVolume) {
                                                    int32_t r4 = *(int32_t*)(ss + s2 + 1);
                                                    foundFadeVolume = streamMethod + s2 + 5 + r4;
                                                }
                                            }
                                            if (foundHandleOff && foundEngineOff && foundFadeVolume) {
                                                s_streamHandleOffset = foundHandleOff;
                                                s_engineOffset = foundEngineOff;
                                                s_fnSoLoudFadeVolume = (SoLoudFadeVolumeFn)(uintptr_t)foundFadeVolume;
                                                s_fmvVolumeAvailable = true;
                                                Log::Mod("GameAudio: [STREAM] FMV volume ENABLED: "
                                                           "_engine=nxAE+0x%X, _currentStream.handle=nxAE+0x%X, fadeVolume=0x%08X",
                                                           s_engineOffset, s_streamHandleOffset, foundFadeVolume);
                                            } else {
                                                Log::Mod("GameAudio: [STREAM] FMV volume FAILED: "
                                                           "handleOff=0x%X engineOff=0x%X fadeVol=0x%08X",
                                                           foundHandleOff, foundEngineOff, foundFadeVolume);
                                            }
                                        }
                                        break;
                                    }
                                }
                                break;
                            }
                        }
                    }
                }
            }
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        Log::Mod("GameAudio: [STREAM] Exception during stop_movie trace");
    }
}

// ============================================================================
// v0.14.46: SFX hook — install MinHook on game-native sfx_set_master_volume
// ============================================================================
//
// (v0.14.45 had ExtractNxAudioEngineSfxAddresses to scan for FFNx's bridge
// bytes. Removed in v0.14.46 because we now hook the game function directly
// rather than FFNx's replacement, so we don't need nxAudioEngine ptr or
// setSFXMasterVolume method addr.)

// ============================================================================
// Hook: intercept ALL calls to set_music_volume_for_channel
// ============================================================================

static uint32_t __cdecl HookedSetMusicVolumeForChannel(int32_t channel, uint32_t volume)
{
    // Track whether the game wants this channel active or silent
    if (channel >= 0 && channel <= 1) {
        s_lastGameVolume[channel] = volume;
    }

    // If the game is setting volume to 0, pass through zero so tracks properly
    // stop/fade. Otherwise enforce our volume level.
    if (volume == 0) {
        if (s_directCallAvailable) {
            s_fnSetMusicVolume(s_nxAudioEngine, NULL, 0.0f, channel, 0.0);
            return 1;
        }
        if (s_originalSetMusicVolumeForChannel)
            return s_originalSetMusicVolumeForChannel(channel, 0);
        return 1;
    }

    // Non-zero: enforce our BGM volume
    if (s_directCallAvailable) {
        s_fnSetMusicVolume(s_nxAudioEngine, NULL, s_bgmVolume, channel, 0.0);
        return 1;
    }

    // Fallback: scale the game's value
    uint32_t scaled = (uint32_t)(volume * s_bgmVolume + 0.5f);
    if (scaled > 127) scaled = 127;
    if (s_originalSetMusicVolumeForChannel)
        return s_originalSetMusicVolumeForChannel(channel, scaled);
    return 1;
}

// ============================================================================
// Hook: intercept ALL calls to sfx_set_master_volume (v0.14.46)
// ============================================================================
//
// volume is 0-100 (game function rejects >100). When game calls this we
// remember the requested value as the game's intent and re-apply our
// user-scaled volume via the original. The original game function then
// stores to *pMasterSfxVolume and propagates to in-flight channels.

static void __cdecl HookedSfxSetMasterVolume(uint32_t volume)
{
    s_lastGameSfxVolume = volume;

    // If the game wants silence (volume==0), respect it.
    if (volume == 0) {
        if (s_originalSfxSetMasterVolume)
            s_originalSfxSetMasterVolume(0);
        return;
    }

    // Non-zero: scale the game's value by our user volume (both 0-100).
    // The game function compares against 0x64 (100) and rejects higher
    // values into a non-update error path, so cap at 100.
    uint32_t scaled = (uint32_t)(volume * s_sfxVolume + 0.5f);
    if (scaled > 100) scaled = 100;
    if (s_originalSfxSetMasterVolume)
        s_originalSfxSetMasterVolume(scaled);
}

// ============================================================================
// Apply current volume to both channels
// ============================================================================

static int s_reapplyCount = 0;

static void ReapplyVolume()
{
    if (!s_hookInstalled) return;

    for (int ch = 0; ch < 2; ch++) {
        if (s_directCallAvailable) {
            // Direct call — set our absolute volume, bypasses FFNx hold flag.
            // FFNx handles stops via nxAudioEngine.stopMusic(), not setMusicVolume,
            // so this won't interfere with intentional track stops.
            s_fnSetMusicVolume(s_nxAudioEngine, NULL, s_bgmVolume, ch, 0.0);
        } else if (s_originalSetMusicVolumeForChannel) {
            uint32_t scaled = (uint32_t)(127 * s_bgmVolume + 0.5f);
            if (scaled > 127) scaled = 127;
            s_originalSetMusicVolumeForChannel(ch, scaled);
        }
    }

    // Also apply to FMV stream audio via direct SoLoud fadeVolume(handle, vol, 0)
    if (s_fmvVolumeAvailable) {
        uint8_t* nxBase = (uint8_t*)s_nxAudioEngine;
        uint32_t streamHandle = *(uint32_t*)(nxBase + s_streamHandleOffset);
        // NXAUDIOENGINE_INVALID_HANDLE = 0xfffff000
        if (streamHandle != 0xfffff000 && streamHandle != 0) {
            void* engine = (void*)(nxBase + s_engineOffset);
            s_fnSoLoudFadeVolume(engine, NULL, streamHandle, s_bgmVolume, 0.0);
        }
    }

    // Log periodically
    s_reapplyCount++;
    if (s_reapplyCount % 20 == 1) {
        Log::Mod("GameAudio: [REAPPLY] #%d vol=%.0f%% gameVol=[%u,%u] direct=%s",
                   s_reapplyCount,
                   s_bgmVolume * 100.0f,
                   s_lastGameVolume[0], s_lastGameVolume[1],
                   s_directCallAvailable ? "YES" : "no");
    }
}

// v0.14.47: SFX reapply at user level. Periodic enforcement when ducker
// is NOT active. When ducking, AudioDucker calls ApplySfxVolume directly
// from its Tick.
static void ReapplySfxVolume()
{
    if (!s_sfxHookInstalled || !s_originalSfxSetMasterVolume) return;

    uint32_t scaled = (uint32_t)(100.0f * s_sfxVolume + 0.5f);
    if (scaled > 100) scaled = 100;
    s_originalSfxSetMasterVolume(scaled);
}

// ============================================================================
// v0.14.47: AudioDucker accessor functions
// ============================================================================
//
// Get callbacks return the user's intended volume (untouched by duck math).
// Apply callbacks write effective volume (user * envelope) to the audio
// backend via the same paths used by ReapplyVolume / ReapplySfxVolume,
// but with the gain as a parameter instead of reading s_bgmVolume / s_sfxVolume.
// Apply* are no-ops when their hook isn't installed yet (matches Reapply*).

float GetUserBgmVolume() { return s_bgmVolume; }
float GetUserSfxVolume() { return s_sfxVolume; }

void ApplyBgmVolume(float linearGain)
{
    if (!s_hookInstalled) return;
    if (linearGain < 0.0f) linearGain = 0.0f;
    if (linearGain > 1.0f) linearGain = 1.0f;

    for (int ch = 0; ch < 2; ch++) {
        if (s_directCallAvailable) {
            s_fnSetMusicVolume(s_nxAudioEngine, NULL, linearGain, ch, 0.0);
        } else if (s_originalSetMusicVolumeForChannel) {
            uint32_t scaled = (uint32_t)(127.0f * linearGain + 0.5f);
            if (scaled > 127) scaled = 127;
            s_originalSetMusicVolumeForChannel(ch, scaled);
        }
    }

    if (s_fmvVolumeAvailable) {
        uint8_t* nxBase = (uint8_t*)s_nxAudioEngine;
        uint32_t streamHandle = *(uint32_t*)(nxBase + s_streamHandleOffset);
        if (streamHandle != 0xfffff000 && streamHandle != 0) {
            void* engine = (void*)(nxBase + s_engineOffset);
            s_fnSoLoudFadeVolume(engine, NULL, streamHandle, linearGain, 0.0);
        }
    }
}

void ApplySfxVolume(float linearGain)
{
    if (!s_sfxHookInstalled || !s_originalSfxSetMasterVolume) return;
    if (linearGain < 0.0f) linearGain = 0.0f;
    if (linearGain > 1.0f) linearGain = 1.0f;

    uint32_t scaled = (uint32_t)(100.0f * linearGain + 0.5f);
    if (scaled > 100) scaled = 100;
    s_originalSfxSetMasterVolume(scaled);
}

// ============================================================================
// Deferred hook installation — BGM
// ============================================================================

static void TryInstallHook()
{
    if (s_hookInstalled) return;

    uint32_t funcAddr = FF8Addresses::pSetMidiVolume;
    if (funcAddr == 0) return;

    uint8_t* pFunc = (uint8_t*)funcAddr;

    __try {
        if (pFunc[0] != 0xE9) return;  // FFNx hasn't patched yet

        // Resolve FFNx's JMP target
        int32_t offset = *(int32_t*)(pFunc + 1);
        void* ffnxFunc = (void*)(pFunc + 5 + offset);
        s_hookedFuncAddr = ffnxFunc;
        Log::Mod("GameAudio: FFNx JMP detected at 0x%08X -> 0x%08X",
                   funcAddr, (uint32_t)(uintptr_t)ffnxFunc);

        // IMPORTANT: Extract nxAudioEngine addresses BEFORE MinHook patches the bytes
        ExtractNxAudioEngineAddresses(ffnxFunc);

        // Scan FFNx code for ALL nxAudioEngine method calls (for FMV volume etc.)
        if (s_directCallAvailable) {
            ScanAllNxAudioEngineMethods((uint32_t)(uintptr_t)ffnxFunc);
        }

        // Now install MinHook
        MH_STATUS st = MH_CreateHook(ffnxFunc, (LPVOID)HookedSetMusicVolumeForChannel,
                                      (LPVOID*)&s_originalSetMusicVolumeForChannel);
        Log::Mod("GameAudio: MH_CreateHook(0x%08X) = %s",
                   (uint32_t)(uintptr_t)ffnxFunc, MH_StatusToString(st));

        if (st == MH_OK) {
            MH_STATUS en = MH_EnableHook(ffnxFunc);
            Log::Mod("GameAudio: MH_EnableHook = %s", MH_StatusToString(en));
            if (en == MH_OK) {
                s_hookInstalled = true;
                s_lastReapplyTick = GetTickCount();
                Log::Mod("GameAudio: Hook installed. BGM volume %.0f%%. Direct=%s, FMV=%s.",
                           s_bgmVolume * 100.0f,
                           s_directCallAvailable ? "YES" : "NO (fallback)",
                           s_fmvVolumeAvailable ? "YES" : "NO");

                // Immediately apply volume
                ReapplyVolume();
            }
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        Log::Mod("GameAudio: Exception reading 0x%08X", funcAddr);
    }
}

// ============================================================================
// v0.14.45: Deferred hook installation — SFX
// ============================================================================

static void TryInstallSfxHook()
{
    if (s_sfxHookInstalled) return;

    uint32_t funcAddr = FF8Addresses::pSfxSetMasterVolume;
    if (funcAddr == 0) return;

    void* hookTarget = (void*)(uintptr_t)funcAddr;

    __try {
        // v0.14.46: Hook the game function at 0x0046A390 directly, regardless
        // of FFNx state. Works whether FFNx patched the prologue with E9 JMP
        // (use_external_sfx=true) or left the original prologue intact
        // (use_external_sfx=false) — MinHook trampolines either way. Calls
        // through the trampoline reach FFNx's wrapper if it patched, or the
        // game-native function if it didn't, and both store master volume
        // and propagate to in-flight channels.
        s_sfxHookedFuncAddr = hookTarget;
        uint8_t* pFunc = (uint8_t*)hookTarget;
        Log::Mod("GameAudio: SFX hooking 0x%08X (first bytes %02X %02X %02X %02X %02X, FFNx-patched=%s)",
                   funcAddr, pFunc[0], pFunc[1], pFunc[2], pFunc[3], pFunc[4],
                   pFunc[0] == 0xE9 ? "YES" : "no");

        MH_STATUS st = MH_CreateHook(hookTarget, (LPVOID)HookedSfxSetMasterVolume,
                                      (LPVOID*)&s_originalSfxSetMasterVolume);
        Log::Mod("GameAudio: SFX MH_CreateHook(0x%08X) = %s",
                   funcAddr, MH_StatusToString(st));

        if (st == MH_OK) {
            MH_STATUS en = MH_EnableHook(hookTarget);
            Log::Mod("GameAudio: SFX MH_EnableHook = %s", MH_StatusToString(en));
            if (en == MH_OK) {
                s_sfxHookInstalled = true;
                Log::Mod("GameAudio: SFX hook installed. SFX volume %.0f%%.",
                           s_sfxVolume * 100.0f);

                // Immediately apply current user volume.
                ReapplySfxVolume();
            }
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        Log::Mod("GameAudio: Exception installing SFX hook at 0x%08X", funcAddr);
    }
}

// ============================================================================
// Public interface
// ============================================================================

void Initialize()
{
    s_hookInstalled = false;
    s_originalSetMusicVolumeForChannel = nullptr;
    s_hookedFuncAddr = nullptr;
    s_nxAudioEngine = nullptr;
    s_fnSetMusicVolume = nullptr;
    s_fnSoLoudFadeVolume = nullptr;
    s_engineOffset = 0;
    s_streamHandleOffset = 0;
    s_directCallAvailable = false;
    s_fmvVolumeAvailable = false;

    // SFX hook state (v0.14.46)
    s_sfxHookInstalled = false;
    s_originalSfxSetMasterVolume = nullptr;
    s_sfxHookedFuncAddr = nullptr;
    s_lastGameSfxVolume = 100;

    // v0.13.51: Default speech rate is now loaded from ff8_accessibility.ini
    // inside ScreenReader::Initialize. Same pattern here for volumes.
    Config::Load();

    // BGM volume — stored as integer percent for hand-editability.
    int pct = Config::GetInt("game_volume", 10);
    if (pct < 0) pct = 0;
    if (pct > 100) pct = 100;
    s_bgmVolume = (float)pct / 100.0f;

    // v0.14.45: SFX volume — also stored as integer percent.
    int sfxPct = Config::GetInt("sfx_volume", 100);
    if (sfxPct < 0) sfxPct = 0;
    if (sfxPct > 100) sfxPct = 100;
    s_sfxVolume = (float)sfxPct / 100.0f;

    // v0.14.45: Audio ducking toggle and ratio.
    s_duckEnabled = Config::GetInt("tts_duck_enabled", 1) != 0;
    s_duckRatioPct = Config::GetInt("sfx_duck_ratio", 30);
    if (s_duckRatioPct < 0)   s_duckRatioPct = 0;
    if (s_duckRatioPct > 100) s_duckRatioPct = 100;

    s_lastGameVolume[0] = 127;
    s_lastGameVolume[1] = 127;
    s_lastReapplyTick = GetTickCount();
    s_reapplyCount = 0;
    Log::Mod("GameAudio: Initialized. BGM=%.0f%%, SFX=%.0f%%, duck=%s, duckRatio=%d%% (loaded from config).",
               s_bgmVolume * 100.0f,
               s_sfxVolume * 100.0f,
               s_duckEnabled ? "ON" : "OFF",
               s_duckRatioPct);

    // v0.14.47: Hand off the volume hooks to AudioDucker. Depths and times
    // are hardcoded constants for v0.14.47 — to tune, change these and
    // rebuild. (Config::GetInt uses GetPrivateProfileInt which clamps to
    // unsigned for negative defaults, so we can't trivially expose dB
    // depths via the existing Config layer. Promote to dedicated INI keys
    // when tuning becomes a frequent activity.)
    AudioDucker::BusConfig bgmCfg;
    bgmCfg.depthDb   = -10.0f;   // BGM ducks moderately (v0.14.48 tuning, was -15)
    bgmCfg.attackMs  = 100.0f;
    bgmCfg.releaseMs = 600.0f;
    bgmCfg.holdMs    = 800.0f;   // bridge menu-nav gaps (v0.14.48, was 200)

    AudioDucker::BusConfig sfxCfg;
    sfxCfg.depthDb   = -15.0f;   // SFX ducks deep — attack anim SFX masks TTS (v0.14.48, was -6)
    sfxCfg.attackMs  = 100.0f;
    sfxCfg.releaseMs = 600.0f;
    sfxCfg.holdMs    = 800.0f;   // bridge menu-nav gaps (v0.14.48, was 200)

    AudioDucker::Initialize(
        GetUserBgmVolume, ApplyBgmVolume, bgmCfg,
        GetUserSfxVolume, ApplySfxVolume, sfxCfg);
    AudioDucker::SetEnabled(s_duckEnabled);
}

void Update()
{
    TryInstallHook();
    TryInstallSfxHook();

    // v0.14.47: AudioDucker drive. Poll IsSpeaking() once per tick; on
    // false->true call BeginDuck, on true->false call EndDuck. Then Tick
    // the envelope. The periodic reapply below is gated on !IsActive
    // so the ducker owns volume writes while ducking.
    static bool s_wasSpeaking = false;
    static DWORD s_lastDuckTick = 0;
    DWORD nowDuck = GetTickCount();
    float dtMs;
    if (s_lastDuckTick == 0) dtMs = 16.0f;
    else                     dtMs = (float)(nowDuck - s_lastDuckTick);
    s_lastDuckTick = nowDuck;

    bool speaking = ScreenReader::IsSpeaking();
    if ( speaking && !s_wasSpeaking) AudioDucker::BeginDuck();
    if (!speaking &&  s_wasSpeaking) AudioDucker::EndDuck();
    s_wasSpeaking = speaking;

    AudioDucker::Tick(dtMs);

    if ((s_hookInstalled || s_sfxHookInstalled) && !AudioDucker::IsActive()) {
        DWORD now = GetTickCount();
        if (now - s_lastReapplyTick >= REAPPLY_INTERVAL_MS) {
            s_lastReapplyTick = now;
            if (s_hookInstalled) ReapplyVolume();
            if (s_sfxHookInstalled) ReapplySfxVolume();
        }
    }
}

void Shutdown()
{
    // v0.14.47: Stop ducker first so it doesn't write while we restore.
    AudioDucker::Shutdown();

    // Restore full volume before unhooking so audio doesn't stay quiet
    if (s_directCallAvailable) {
        for (int ch = 0; ch < 2; ch++) {
            s_fnSetMusicVolume(s_nxAudioEngine, NULL, 1.0f, ch, 0.0);
        }
    }
    if (s_fmvVolumeAvailable) {
        uint8_t* nxBase = (uint8_t*)s_nxAudioEngine;
        uint32_t streamHandle = *(uint32_t*)(nxBase + s_streamHandleOffset);
        if (streamHandle != 0xfffff000 && streamHandle != 0) {
            void* engine = (void*)(nxBase + s_engineOffset);
            s_fnSoLoudFadeVolume(engine, NULL, streamHandle, 1.0f, 0.0);
        }
    }
    if (s_sfxHookInstalled && s_originalSfxSetMasterVolume) {
        // Restore SFX master to 100 (max) before unhooking.
        s_originalSfxSetMasterVolume(100);
    }

    if (s_hookInstalled && s_hookedFuncAddr) {
        MH_DisableHook(s_hookedFuncAddr);
        Log::Mod("GameAudio: BGM hook disabled.");
    }
    if (s_sfxHookInstalled && s_sfxHookedFuncAddr) {
        MH_DisableHook(s_sfxHookedFuncAddr);
        Log::Mod("GameAudio: SFX hook disabled.");
    }
    s_hookInstalled = false;
    s_originalSetMusicVolumeForChannel = nullptr;
    s_hookedFuncAddr = nullptr;
    s_directCallAvailable = false;
    s_fmvVolumeAvailable = false;
    s_sfxHookInstalled = false;
    s_originalSfxSetMasterVolume = nullptr;
    s_sfxHookedFuncAddr = nullptr;
    Log::Mod("GameAudio: Shutdown complete.");
}

void VolumeDown()
{
    float newVol = s_bgmVolume - 0.1f;
    if (newVol < 0.0f) newVol = 0.0f;
    s_bgmVolume = newVol;

    Log::Mod("GameAudio: BGM volume -> %.0f%%", s_bgmVolume * 100.0f);

    if (s_hookInstalled) {
        if (!AudioDucker::IsActive()) ReapplyVolume();
    }

    Config::SetInt("game_volume", (int)(s_bgmVolume * 100.0f + 0.5f));

    char msg[48];
    snprintf(msg, sizeof(msg), "Music volume %d percent", (int)(s_bgmVolume * 100.0f + 0.5f));
    ScreenReader::Speak(msg, true);
}

void VolumeUp()
{
    float newVol = s_bgmVolume + 0.1f;
    if (newVol > 1.0f) newVol = 1.0f;
    s_bgmVolume = newVol;

    Log::Mod("GameAudio: BGM volume -> %.0f%%", s_bgmVolume * 100.0f);

    if (s_hookInstalled) {
        if (!AudioDucker::IsActive()) ReapplyVolume();
    }

    Config::SetInt("game_volume", (int)(s_bgmVolume * 100.0f + 0.5f));

    char msg[48];
    snprintf(msg, sizeof(msg), "Music volume %d percent", (int)(s_bgmVolume * 100.0f + 0.5f));
    ScreenReader::Speak(msg, true);
}

// ============================================================================
// v0.14.45: Public SFX volume controls (F5/F6)
// ============================================================================

void SfxVolumeDown()
{
    float newVol = s_sfxVolume - 0.1f;
    if (newVol < 0.0f) newVol = 0.0f;
    s_sfxVolume = newVol;

    Log::Mod("GameAudio: SFX volume -> %.0f%%", s_sfxVolume * 100.0f);

    if (s_sfxHookInstalled) {
        if (!AudioDucker::IsActive()) ReapplySfxVolume();
    }

    Config::SetInt("sfx_volume", (int)(s_sfxVolume * 100.0f + 0.5f));

    char msg[48];
    snprintf(msg, sizeof(msg), "SFX volume %d percent", (int)(s_sfxVolume * 100.0f + 0.5f));
    ScreenReader::Speak(msg, true);
}

void SfxVolumeUp()
{
    float newVol = s_sfxVolume + 0.1f;
    if (newVol > 1.0f) newVol = 1.0f;
    s_sfxVolume = newVol;

    Log::Mod("GameAudio: SFX volume -> %.0f%%", s_sfxVolume * 100.0f);

    if (s_sfxHookInstalled) {
        if (!AudioDucker::IsActive()) ReapplySfxVolume();
    }

    Config::SetInt("sfx_volume", (int)(s_sfxVolume * 100.0f + 0.5f));

    char msg[48];
    snprintf(msg, sizeof(msg), "SFX volume %d percent", (int)(s_sfxVolume * 100.0f + 0.5f));
    ScreenReader::Speak(msg, true);
}

// ============================================================================
// v0.14.45: Audio ducking toggle (F2)
// Phase 1 — persists the bool and announces. Phase 2 (v0.14.46) wires the
// IsSpeaking() polling that actually drops SFX volume during TTS.
// ============================================================================

void ToggleDucking()
{
    s_duckEnabled = !s_duckEnabled;
    AudioDucker::SetEnabled(s_duckEnabled);
    Log::Mod("GameAudio: Audio ducking -> %s", s_duckEnabled ? "ON" : "OFF");

    Config::SetInt("tts_duck_enabled", s_duckEnabled ? 1 : 0);

    ScreenReader::Speak(s_duckEnabled ? "Audio ducking on" : "Audio ducking off", true);
}

bool IsDuckingEnabled()
{
    return s_duckEnabled;
}

}  // namespace GameAudio
