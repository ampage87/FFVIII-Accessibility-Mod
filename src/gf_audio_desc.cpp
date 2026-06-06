// gf_audio_desc.cpp - GF Summon Audio Description System (v0.14.44)
//
// Architecture mirrors fmv_audio_desc.cpp:
//   - VTT files embedded as RCDATA resources
//   - WebVTT parser builds Cue lists at init time
//   - Per-frame poll fires cues when their start time arrives
//
// Differences from FMV AD:
//   - Trigger is the in-engine GF effect ID (battle_magic_id), not an
//     AVI filename. PollBattleMagicId() in battle_tts_ewm.inl calls
//     OnGFAnimationStart(effectId) on the rising edge.
//   - End detection: OnFrame watches battle_magic_id; when it reverts
//     to a non-GF value (animation finished OR player pressed R1+L1 to
//     skip), we stop playback mid-cue.
//   - Output channel: Channel 2 (event voice) so it doesn't fight menu
//     navigation announces if EWM is off.

#include "ff8_accessibility.h"
#include "gf_audio_desc.h"
#include "resources.h"
#include <vector>
#include <string>
#include <map>
#include <sstream>
#include <algorithm>
#include <cctype>
#include <cstdio>

// Forward declarations for cross-module namespaces (matches fmv_audio_desc pattern)
namespace Log { void Mod(const char* format, ...); void Battle(const char* format, ...); }
namespace ScreenReader { bool SpeakChannel2(const char* text, bool interrupt = false); }

namespace GfAudioDesc
{
    // -- VTT data structures --

    struct Cue
    {
        double startTime;   // seconds
        double endTime;     // seconds
        std::string text;
    };

    struct VttTrack
    {
        std::string name;           // e.g. "gf_shiva.vtt"
        std::vector<Cue> cues;
    };

    // -- State --

    static bool g_initialized = false;
    static HMODULE g_hModule = nullptr;

    // effectId -> track. Effect IDs come from IsGFEffectId() in
    // battle_tts_ewm.inl.
    static std::map<int, VttTrack> g_tracks;

    // Current playback state
    static bool g_playing = false;
    static int g_currentEffectId = -1;
    static VttTrack* g_currentTrack = nullptr;
    static int g_nextCueIndex = 0;
    static LARGE_INTEGER g_startTime = {};
    static LARGE_INTEGER g_perfFreq = {};

    // -- Helpers (parser is a direct port from fmv_audio_desc) --

    static std::string Trim(const std::string& s)
    {
        size_t a = s.find_first_not_of(" \t\r\n");
        if (a == std::string::npos) return "";
        size_t b = s.find_last_not_of(" \t\r\n");
        return s.substr(a, b - a + 1);
    }

    static std::string LoadResourceString(HMODULE hModule, int resourceId)
    {
        HRSRC hRes = FindResourceA(hModule, MAKEINTRESOURCEA(resourceId), RT_RCDATA);
        if (!hRes) return "";
        HGLOBAL hData = LoadResource(hModule, hRes);
        if (!hData) return "";
        DWORD size = SizeofResource(hModule, hRes);
        const char* data = static_cast<const char*>(LockResource(hData));
        if (!data || size == 0) return "";

        // Skip UTF-8 BOM if present
        if (size >= 3 &&
            static_cast<unsigned char>(data[0]) == 0xEF &&
            static_cast<unsigned char>(data[1]) == 0xBB &&
            static_cast<unsigned char>(data[2]) == 0xBF) {
            data += 3; size -= 3;
        }
        return std::string(data, size);
    }

    static double ParseTimestamp(const std::string& ts)
    {
        int hh = 0, mm = 0, ss = 0, ms = 0;
        int colons = 0;
        for (char c : ts) if (c == ':') colons++;
        if (colons == 2) {
            if (sscanf(ts.c_str(), "%d:%d:%d.%d", &hh, &mm, &ss, &ms) < 3) return 0.0;
        } else if (colons == 1) {
            if (sscanf(ts.c_str(), "%d:%d.%d", &mm, &ss, &ms) < 2) return 0.0;
        } else {
            return 0.0;
        }
        return hh * 3600.0 + mm * 60.0 + ss + ms / 1000.0;
    }

    static bool ParseVttString(const std::string& content, VttTrack& track)
    {
        if (content.empty()) return false;
        track.cues.clear();

        std::istringstream stream(content);
        std::string line;
        enum { STATE_HEADER, STATE_SEEKING, STATE_TEXT } state = STATE_HEADER;
        Cue cur = {};

        while (std::getline(stream, line)) {
            line = Trim(line);
            switch (state) {
            case STATE_HEADER:
                if (line.empty() || line.find("WEBVTT") != std::string::npos
                    || line.find("NOTE") == 0) {
                    if (line.empty()) state = STATE_SEEKING;
                    continue;
                }
                state = STATE_SEEKING;
                // fallthrough
            case STATE_SEEKING:
                if (line.empty()) continue;
                if (line.find("NOTE") == 0) {
                    while (std::getline(stream, line)) {
                        line = Trim(line);
                        if (line.empty()) break;
                    }
                    continue;
                }
                if (line.find("-->") != std::string::npos) {
                    size_t arrow = line.find("-->");
                    std::string a = Trim(line.substr(0, arrow));
                    std::string b = Trim(line.substr(arrow + 3));
                    cur.startTime = ParseTimestamp(a);
                    cur.endTime = ParseTimestamp(b);
                    cur.text.clear();
                    state = STATE_TEXT;
                }
                break;
            case STATE_TEXT:
                if (line.empty()) {
                    if (!cur.text.empty()) track.cues.push_back(cur);
                    cur = {};
                    state = STATE_SEEKING;
                } else {
                    if (!cur.text.empty()) cur.text += " ";
                    cur.text += line;
                }
                break;
            }
        }
        if (state == STATE_TEXT && !cur.text.empty()) track.cues.push_back(cur);

        std::sort(track.cues.begin(), track.cues.end(),
            [](const Cue& a, const Cue& b) { return a.startTime < b.startTime; });
        return !track.cues.empty();
    }

    static double GetElapsedSeconds()
    {
        LARGE_INTEGER now;
        QueryPerformanceCounter(&now);
        return double(now.QuadPart - g_startTime.QuadPart) / double(g_perfFreq.QuadPart);
    }

    // ------------------------------------------------------------
    // GF effect ID -> VTT resource ID mapping
    // ------------------------------------------------------------

    struct GfEntry {
        int effectId;
        int resourceId;
        const char* name;
    };

    static const GfEntry g_gfTable[] = {
        // 16 junctioned GFs
        { 115, IDR_VTT_GF_BASE + 0,  "gf_quezacotl.vtt" },
        { 184, IDR_VTT_GF_BASE + 1,  "gf_shiva.vtt" },
        { 200, IDR_VTT_GF_BASE + 2,  "gf_ifrit.vtt" },
        {  94, IDR_VTT_GF_BASE + 3,  "gf_siren.vtt" },
        { 204, IDR_VTT_GF_BASE + 4,  "gf_brothers.vtt" },
        { 324, IDR_VTT_GF_BASE + 5,  "gf_diablos.vtt" },
        { 277, IDR_VTT_GF_BASE + 6,  "gf_carbuncle.vtt" },
        {   5, IDR_VTT_GF_BASE + 7,  "gf_leviathan.vtt" },
        { 290, IDR_VTT_GF_BASE + 8,  "gf_pandemona.vtt" },
        { 202, IDR_VTT_GF_BASE + 9,  "gf_cerberus.vtt" },
        { 203, IDR_VTT_GF_BASE + 10, "gf_alexander.vtt" },
        { 190, IDR_VTT_GF_BASE + 11, "gf_doomtrain.vtt" },
        { 201, IDR_VTT_GF_BASE + 12, "gf_bahamut.vtt" },
        { 198, IDR_VTT_GF_BASE + 13, "gf_cactuar.vtt" },
        {  89, IDR_VTT_GF_BASE + 14, "gf_tonberry.vtt" },
        { 205, IDR_VTT_GF_BASE + 15, "gf_eden.vtt" },
        // Special / item / auto summons
        { 139, IDR_VTT_GF_BASE + 16, "gf_phoenix.vtt" },
        { 186, IDR_VTT_GF_BASE + 17, "gf_odin.vtt" },
    };
    static const int g_gfTableCount = sizeof(g_gfTable) / sizeof(g_gfTable[0]);

    // Detect end-of-summon by polling battle_magic_id. Address is resolved
    // by battle_tts_ewm.inl during EWM_InstallBattleEffectHook(); we read
    // the same engine variable here.
    static const uint32_t BATTLE_EFFECT_FUNC_ADDR_LOCAL = 0x50AF20;
    static uint32_t s_battleMagicIdAddr = 0;

    static int ReadBattleMagicId()
    {
        if (s_battleMagicIdAddr == 0) {
            __try {
                s_battleMagicIdAddr =
                    *(uint32_t*)(BATTLE_EFFECT_FUNC_ADDR_LOCAL + 0x3E);
            } __except(EXCEPTION_EXECUTE_HANDLER) {
                return -1;
            }
        }
        if (s_battleMagicIdAddr == 0) return -1;
        __try {
            return *(int*)s_battleMagicIdAddr;
        } __except(EXCEPTION_EXECUTE_HANDLER) {
            return -1;
        }
    }

    static void StartPlaybackInternal(int effectId, VttTrack* track)
    {
        if (!track || track->cues.empty()) return;
        g_playing = true;
        g_currentEffectId = effectId;
        g_currentTrack = track;
        g_nextCueIndex = 0;
        QueryPerformanceCounter(&g_startTime);
        Log::Battle("[GF-AD] Playback started: effectId=%d track=%s cues=%zu duration~%.1fs",
                    effectId, track->name.c_str(), track->cues.size(),
                    track->cues.back().endTime);
    }

    // -- Public API --

    void Initialize(HMODULE dllModule)
    {
        if (g_initialized) return;
        g_hModule = dllModule;
        QueryPerformanceFrequency(&g_perfFreq);

        int loaded = 0;
        for (int i = 0; i < g_gfTableCount; i++) {
            const GfEntry& e = g_gfTable[i];
            std::string content = LoadResourceString(g_hModule, e.resourceId);
            if (content.empty()) {
                Log::Mod("[GF-AD] Resource %d (%s) missing or empty",
                         e.resourceId, e.name);
                continue;
            }
            VttTrack t;
            t.name = e.name;
            if (!ParseVttString(content, t) || t.cues.empty()) {
                Log::Mod("[GF-AD] Parse failed for %s (resource %d)",
                         e.name, e.resourceId);
                continue;
            }
            g_tracks[e.effectId] = std::move(t);
            loaded++;
            Log::Mod("[GF-AD] Loaded effectId=%d %s: %zu cues",
                     e.effectId, e.name, g_tracks[e.effectId].cues.size());
        }

        g_initialized = true;
        Log::Mod("[GF-AD] Initialized: %d/%d GF VTTs loaded.",
                 loaded, g_gfTableCount);
    }

    void Shutdown()
    {
        StopPlayback();
        g_tracks.clear();
        g_hModule = nullptr;
        g_initialized = false;
        Log::Mod("[GF-AD] Shutdown.");
    }

    void StopPlayback()
    {
        if (g_playing) {
            Log::Battle("[GF-AD] Playback stopped at %.1fs (cue %d/%zu) effectId=%d",
                        GetElapsedSeconds(), g_nextCueIndex,
                        g_currentTrack ? g_currentTrack->cues.size() : 0,
                        g_currentEffectId);
        }
        g_playing = false;
        g_currentEffectId = -1;
        g_currentTrack = nullptr;
        g_nextCueIndex = 0;
    }

    void OnGFAnimationStart(int effectId)
    {
        if (!g_initialized) return;
        if (g_playing) {
            Log::Battle("[GF-AD] OnGFAnimationStart effectId=%d ignored (already playing effectId=%d)",
                        effectId, g_currentEffectId);
            return;
        }
        auto it = g_tracks.find(effectId);
        if (it == g_tracks.end()) {
            Log::Battle("[GF-AD] OnGFAnimationStart effectId=%d: no VTT registered",
                        effectId);
            return;
        }
        StartPlaybackInternal(effectId, &it->second);
    }

    void OnFrame()
    {
        if (!g_initialized || !g_playing || !g_currentTrack) return;

        // -- End-of-summon detection --
        // If battle_magic_id is no longer the GF effect we're playing for,
        // the engine has either finished the animation or the player skipped.
        int curMagicId = ReadBattleMagicId();
        if (curMagicId != g_currentEffectId) {
            Log::Battle("[GF-AD] battle_magic_id changed (%d -> %d), stopping playback",
                        g_currentEffectId, curMagicId);
            StopPlayback();
            return;
        }

        // -- Fire cues whose start time has arrived --
        double elapsed = GetElapsedSeconds();
        if (g_nextCueIndex >= (int)g_currentTrack->cues.size()) {
            return;
        }
        while (g_nextCueIndex < (int)g_currentTrack->cues.size()) {
            const Cue& cue = g_currentTrack->cues[g_nextCueIndex];
            if (elapsed < cue.startTime) break;
            Log::Battle("[GF-AD] [%.1fs] Cue %d: %s",
                        elapsed, g_nextCueIndex, cue.text.c_str());
            ScreenReader::SpeakChannel2(cue.text.c_str(), true);
            g_nextCueIndex++;
        }
    }

}  // namespace GfAudioDesc
