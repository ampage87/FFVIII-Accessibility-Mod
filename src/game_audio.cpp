// game_audio.cpp - Centralized game audio control for FF8 Accessibility Mod
//
// v0.09.22: Extracted from dinput8.cpp.
// v0.09.24: Direct nxAudioEngine.setMusicVolume call — bypasses FFNx hold flag.
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

#include "ff8_accessibility.h"
#include "game_audio.h"
#include "minhook/include/MinHook.h"

namespace GameAudio {

// ============================================================================
// Internal state
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
        Log::Write("GameAudio: [SCAN] FFNx set_music_volume_for_channel at 0x%08X, first 128 bytes:", funcBase);
        for (int row = 0; row < 8; row++) {
            int off = row * 16;
            Log::Write("GameAudio: [SCAN] +%02X: %02X %02X %02X %02X %02X %02X %02X %02X  "
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
                Log::Write("GameAudio: [SCAN] +%02X: MOV ECX, 0x%08X (nxAudioEngine candidate)", i, candidateThis);
                Log::Write("GameAudio: [SCAN] +%02X: CALL 0x%08X (setMusicVolume candidate)", j, callTarget);

                s_nxAudioEngine = (void*)(uintptr_t)candidateThis;
                s_fnSetMusicVolume = (SetMusicVolumeFn)(uintptr_t)callTarget;
                s_directCallAvailable = true;

                Log::Write("GameAudio: Direct nxAudioEngine access ENABLED: "
                           "nxAudioEngine=0x%08X setMusicVolume=0x%08X",
                           candidateThis, callTarget);
                return;
            }
        }

        Log::Write("GameAudio: [SCAN] WARNING: Could not find nxAudioEngine pattern. "
                   "Falling back to original function call (subject to hold flag).");
    }
    __except(EXCEPTION_EXECUTE_HANDLER) {
        Log::Write("GameAudio: [SCAN] Exception scanning FFNx function bytes.");
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

    Log::Write("GameAudio: [METHODS] Scanning 0x%08X-0x%08X for MOV ECX, 0x%08X (B9 %02X %02X %02X %02X)",
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
        Log::Write("GameAudio: [METHODS] Exception during scan (found %d so far)", targetCount);
    }

    Log::Write("GameAudio: [METHODS] Found %d unique NxAudioEngine method targets:", targetCount);
    for (int i = 0; i < targetCount; i++) {
        const char* label = "";
        if (targets[i] == (uint32_t)(uintptr_t)s_fnSetMusicVolume) label = " <-- setMusicVolume (KNOWN)";
        Log::Write("GameAudio: [METHODS]   #%02d: 0x%08X (caller at 0x%08X)%s",
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
        Log::Write("GameAudio: [STREAM] opcode_movie=0x%08X start_movie=0x%08X update_movie_sample=0x%08X stop_movie=0x%08X",
                   opcode_movie, start_movie_game, update_movie_sample_game, stop_movie_game);

        // stop_movie should have been replaced by FFNx with a JMP (E9)
        if (stop_movie_game) {
            uint8_t* sm = (uint8_t*)(uintptr_t)stop_movie_game;
            if (sm[0] == 0xE9) {
                int32_t r = *(int32_t*)(sm + 1);
                uint32_t ff8StopMovie = stop_movie_game + 5 + r;
                Log::Write("GameAudio: [STREAM] stop_movie JMP -> ff8_stop_movie at 0x%08X", ff8StopMovie);

                // Scan ff8_stop_movie for B9 <nxAudioEngine> + E8 pattern
                // ff8_stop_movie calls ffmpeg_stop_movie() which calls nxAudioEngine.stopStream()
                // But the nxAudioEngine call may be inside ffmpeg_stop_movie, not ff8_stop_movie directly.
                // First scan ff8_stop_movie for CALL instructions to find ffmpeg_stop_movie.
                uint8_t* fsm = (uint8_t*)(uintptr_t)ff8StopMovie;
                Log::Write("GameAudio: [STREAM] ff8_stop_movie first 64 bytes:");
                for (int row2 = 0; row2 < 4; row2++) {
                    int off2 = row2 * 16;
                    Log::Write("GameAudio: [STREAM] +%02X: %02X %02X %02X %02X %02X %02X %02X %02X  "
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
                        Log::Write("GameAudio: [STREAM] ff8_stop_movie+%02X: CALL 0x%08X", k, callTgt);
                        
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
                                        Log::Write("GameAudio: [STREAM] Found stopStream candidate: 0x%08X (from ffmpeg func at 0x%08X+%02X)",
                                                   streamMethod, callTgt, n);
                                        
                                        // setStreamMasterVolume is declared 4 methods after stopStream in audio.h:
                                        // stopStream, pauseStream, resumeStream, isStreamPlaying, getStreamMasterVolume, setStreamMasterVolume
                                        // Find it in the method catalog by looking for the entry closest to but after stopStream
                                        // that has been seen called from the movie/stream context
                                        for (int t = 0; t < targetCount; t++) {
                                            if (targets[t] == streamMethod) {
                                                Log::Write("GameAudio: [STREAM] stopStream confirmed as method #%02d", t);
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
                                                Log::Write("GameAudio: [STREAM] FMV volume ENABLED: "
                                                           "_engine=nxAE+0x%X, _currentStream.handle=nxAE+0x%X, fadeVolume=0x%08X",
                                                           s_engineOffset, s_streamHandleOffset, foundFadeVolume);
                                            } else {
                                                Log::Write("GameAudio: [STREAM] FMV volume FAILED: "
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
        Log::Write("GameAudio: [STREAM] Exception during stop_movie trace");
    }
}

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
        Log::Write("GameAudio: [REAPPLY] #%d vol=%.0f%% gameVol=[%u,%u] direct=%s",
                   s_reapplyCount,
                   s_bgmVolume * 100.0f,
                   s_lastGameVolume[0], s_lastGameVolume[1],
                   s_directCallAvailable ? "YES" : "no");
    }
}

// ============================================================================
// Deferred hook installation
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
        Log::Write("GameAudio: FFNx JMP detected at 0x%08X -> 0x%08X",
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
        Log::Write("GameAudio: MH_CreateHook(0x%08X) = %s",
                   (uint32_t)(uintptr_t)ffnxFunc, MH_StatusToString(st));

        if (st == MH_OK) {
            MH_STATUS en = MH_EnableHook(ffnxFunc);
            Log::Write("GameAudio: MH_EnableHook = %s", MH_StatusToString(en));
            if (en == MH_OK) {
                s_hookInstalled = true;
                s_lastReapplyTick = GetTickCount();
                Log::Write("GameAudio: Hook installed. BGM volume %.0f%%. Direct=%s, FMV=%s.",
                           s_bgmVolume * 100.0f,
                           s_directCallAvailable ? "YES" : "NO (fallback)",
                           s_fmvVolumeAvailable ? "YES" : "NO");

                // Immediately apply volume
                ReapplyVolume();
            }
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        Log::Write("GameAudio: Exception reading 0x%08X", funcAddr);
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
    s_bgmVolume = 0.1f;
    s_lastGameVolume[0] = 127;
    s_lastGameVolume[1] = 127;
    s_lastReapplyTick = GetTickCount();
    s_reapplyCount = 0;
    Log::Write("GameAudio: Initialized. Default BGM volume %.0f%%.", s_bgmVolume * 100.0f);
}

void Update()
{
    TryInstallHook();

    if (s_hookInstalled) {
        DWORD now = GetTickCount();
        if (now - s_lastReapplyTick >= REAPPLY_INTERVAL_MS) {
            s_lastReapplyTick = now;
            ReapplyVolume();
        }
    }
}

void Shutdown()
{
    // Restore full volume before unhooking so music doesn't stay quiet
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

    if (s_hookInstalled && s_hookedFuncAddr) {
        MH_DisableHook(s_hookedFuncAddr);
        Log::Write("GameAudio: Hook disabled.");
    }
    s_hookInstalled = false;
    s_originalSetMusicVolumeForChannel = nullptr;
    s_hookedFuncAddr = nullptr;
    s_directCallAvailable = false;
    s_fmvVolumeAvailable = false;
    Log::Write("GameAudio: Shutdown complete.");
}

void VolumeDown()
{
    float newVol = s_bgmVolume - 0.1f;
    if (newVol < 0.0f) newVol = 0.0f;
    s_bgmVolume = newVol;

    Log::Write("GameAudio: BGM volume -> %.0f%%", s_bgmVolume * 100.0f);

    if (s_hookInstalled) ReapplyVolume();

    char msg[48];
    snprintf(msg, sizeof(msg), "Music volume %d percent", (int)(s_bgmVolume * 100.0f + 0.5f));
    ScreenReader::Speak(msg, true);
}

void VolumeUp()
{
    float newVol = s_bgmVolume + 0.1f;
    if (newVol > 1.0f) newVol = 1.0f;
    s_bgmVolume = newVol;

    Log::Write("GameAudio: BGM volume -> %.0f%%", s_bgmVolume * 100.0f);

    if (s_hookInstalled) ReapplyVolume();

    char msg[48];
    snprintf(msg, sizeof(msg), "Music volume %d percent", (int)(s_bgmVolume * 100.0f + 0.5f));
    ScreenReader::Speak(msg, true);
}

}  // namespace GameAudio
