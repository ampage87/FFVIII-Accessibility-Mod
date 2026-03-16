// fmv_audio_desc.cpp - FMV Audio Description System (v03.00)
//
// Ported from FF8 Remastered Accessibility Mod (v31.30).
//
// Key change from Remaster: VTT files and mapping data are embedded as Win32
// resources in the DLL (via resources.rc). No external Audio Descriptions
// folder needed at runtime — everything is baked into dinput8.dll.
//
// Adaptations from Remaster:
//   - Resource loading: FindResource/LoadResource instead of filesystem scanning
//   - Movie detection: polls FmvSkip::GetCurrentAviName() instead of LastFilePath
//   - TTS output: ScreenReader::Speak replaces SpeechThread::QueueSpeak
//   - Logging: Log::Write replaces OutputDebug
//   - FMV end detection: FmvSkip calls StopPlayback() via CloseHandle hook
//
// WebVTT parser is a direct port — same format, same timing logic,
// but parses from a memory buffer instead of file I/O.

#include "ff8_accessibility.h"
#include "fmv_audio_desc.h"
#include "fmv_skip.h"
#include "resources.h"
#include <vector>
#include <string>
#include <map>
#include <sstream>
#include <algorithm>
#include <cctype>

namespace FmvAudioDesc
{
    // ---- VTT data structures ----

    struct Cue
    {
        double startTime;   // seconds
        double endTime;     // seconds
        std::string text;
    };

    struct VttTrack
    {
        std::string name;           // descriptive name (e.g. "ff8_intro_ad.vtt")
        std::vector<Cue> cues;
    };

    // Table mapping resource IDs to their VTT track names.
    // Add entries here when adding new audio description VTT resources.
    struct ResourceEntry
    {
        int resourceId;
        const char* vttName;    // lowercase, matches mapping.txt values
    };

    static const ResourceEntry g_resourceTable[] = {
        { IDR_VTT_INTRO,           "ff8_intro_ad.vtt" },
        { IDR_VTT_OPENING_CREDITS, "ff8_opening_credits_ad.vtt" },
    };
    static const int g_resourceTableCount = sizeof(g_resourceTable) / sizeof(g_resourceTable[0]);

    // ---- State ----

    static bool g_initialized = false;
    static HMODULE g_hModule = nullptr;

    // All loaded VTT tracks, keyed by VTT name (lowercase)
    static std::map<std::string, VttTrack> g_tracks;

    // AVI filename (just the basename, lowercase) -> VTT name (lowercase)
    static std::map<std::string, std::string> g_aviToVtt;

    // Current playback state
    static bool g_playing = false;
    static VttTrack* g_currentTrack = nullptr;
    static int g_nextCueIndex = 0;
    static LARGE_INTEGER g_startTime = {};
    static LARGE_INTEGER g_perfFreq = {};

    // Track which AVI we last started playback for (to detect transitions)
    static std::string g_lastStartedAvi;

    // ---- Helpers ----

    static std::string ToLower(const std::string& s)
    {
        std::string result = s;
        std::transform(result.begin(), result.end(), result.begin(),
            [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return result;
    }

    static std::string Trim(const std::string& s)
    {
        size_t start = s.find_first_not_of(" \t\r\n");
        if (start == std::string::npos)
            return "";
        size_t end = s.find_last_not_of(" \t\r\n");
        return s.substr(start, end - start + 1);
    }

    // Load a resource as a string. Returns empty string on failure.
    static std::string LoadResourceString(HMODULE hModule, int resourceId)
    {
        HRSRC hRes = FindResourceA(hModule, MAKEINTRESOURCEA(resourceId), RT_RCDATA);
        if (!hRes)
        {
            Log::Write("[FMV_AD] FindResource failed for ID %d (error %u)",
                resourceId, GetLastError());
            return "";
        }

        HGLOBAL hData = LoadResource(hModule, hRes);
        if (!hData)
        {
            Log::Write("[FMV_AD] LoadResource failed for ID %d", resourceId);
            return "";
        }

        DWORD size = SizeofResource(hModule, hRes);
        const char* data = static_cast<const char*>(LockResource(hData));
        if (!data || size == 0)
        {
            Log::Write("[FMV_AD] LockResource failed or empty for ID %d", resourceId);
            return "";
        }

        // Skip UTF-8 BOM if present
        if (size >= 3 &&
            static_cast<unsigned char>(data[0]) == 0xEF &&
            static_cast<unsigned char>(data[1]) == 0xBB &&
            static_cast<unsigned char>(data[2]) == 0xBF)
        {
            data += 3;
            size -= 3;
        }

        return std::string(data, size);
    }

    // Parse a VTT timestamp like "00:01:23.456" into seconds
    static double ParseTimestamp(const std::string& ts)
    {
        int hours = 0, minutes = 0, seconds = 0, millis = 0;

        int colonCount = 0;
        for (char c : ts)
            if (c == ':') colonCount++;

        if (colonCount == 2)
        {
            if (sscanf(ts.c_str(), "%d:%d:%d.%d", &hours, &minutes, &seconds, &millis) < 3)
                return 0.0;
        }
        else if (colonCount == 1)
        {
            if (sscanf(ts.c_str(), "%d:%d.%d", &minutes, &seconds, &millis) < 2)
                return 0.0;
        }
        else
        {
            return 0.0;
        }

        return static_cast<double>(hours) * 3600.0
             + static_cast<double>(minutes) * 60.0
             + static_cast<double>(seconds)
             + static_cast<double>(millis) / 1000.0;
    }

    // Parse VTT content from a string buffer into a track
    static bool ParseVttString(const std::string& content, VttTrack& track)
    {
        if (content.empty())
            return false;

        track.cues.clear();

        std::istringstream stream(content);
        std::string line;
        enum { STATE_HEADER, STATE_SEEKING, STATE_TEXT } state = STATE_HEADER;
        Cue currentCue = {};

        while (std::getline(stream, line))
        {
            line = Trim(line);

            switch (state)
            {
            case STATE_HEADER:
                if (line.empty() || line.find("WEBVTT") != std::string::npos
                    || line.find("NOTE") == 0)
                {
                    if (line.empty())
                        state = STATE_SEEKING;
                    continue;
                }
                state = STATE_SEEKING;
                // fallthrough

            case STATE_SEEKING:
                if (line.empty())
                    continue;

                // Skip NOTE blocks
                if (line.find("NOTE") == 0)
                {
                    while (std::getline(stream, line))
                    {
                        line = Trim(line);
                        if (line.empty())
                            break;
                    }
                    continue;
                }

                // Check if this is a timestamp line (contains "-->")
                if (line.find("-->") != std::string::npos)
                {
                    size_t arrowPos = line.find("-->");
                    std::string startStr = Trim(line.substr(0, arrowPos));
                    std::string endStr = Trim(line.substr(arrowPos + 3));

                    currentCue.startTime = ParseTimestamp(startStr);
                    currentCue.endTime = ParseTimestamp(endStr);
                    currentCue.text.clear();
                    state = STATE_TEXT;
                }
                break;

            case STATE_TEXT:
                if (line.empty())
                {
                    if (!currentCue.text.empty())
                    {
                        track.cues.push_back(currentCue);
                    }
                    currentCue = {};
                    state = STATE_SEEKING;
                }
                else
                {
                    if (!currentCue.text.empty())
                        currentCue.text += " ";
                    currentCue.text += line;
                }
                break;
            }
        }

        // Don't forget the last cue if content doesn't end with blank line
        if (state == STATE_TEXT && !currentCue.text.empty())
        {
            track.cues.push_back(currentCue);
        }

        // Sort cues by start time
        std::sort(track.cues.begin(), track.cues.end(),
            [](const Cue& a, const Cue& b) { return a.startTime < b.startTime; });

        return !track.cues.empty();
    }

    // Parse the mapping text (from embedded resource) that maps AVI filenames to VTT names
    static void ParseMappingString(const std::string& content)
    {
        if (content.empty())
        {
            Log::Write("[FMV_AD] Mapping resource is empty, using defaults only");
            return;
        }

        std::istringstream stream(content);
        std::string line;

        while (std::getline(stream, line))
        {
            line = Trim(line);
            if (line.empty() || line[0] == '#')
                continue;

            size_t eqPos = line.find('=');
            if (eqPos == std::string::npos)
                continue;

            std::string aviName = Trim(line.substr(0, eqPos));
            std::string vttName = Trim(line.substr(eqPos + 1));

            if (!aviName.empty() && !vttName.empty())
            {
                g_aviToVtt[ToLower(aviName)] = ToLower(vttName);
                Log::Write("[FMV_AD] Mapping: %s -> %s", aviName.c_str(), vttName.c_str());
            }
        }
    }

    // ---- Playback control ----

    static double GetElapsedSeconds()
    {
        LARGE_INTEGER now;
        QueryPerformanceCounter(&now);
        return static_cast<double>(now.QuadPart - g_startTime.QuadPart)
             / static_cast<double>(g_perfFreq.QuadPart);
    }

    static void StartPlayback(VttTrack* track, const std::string& aviName)
    {
        if (!track || track->cues.empty())
            return;

        g_playing = true;
        g_currentTrack = track;
        g_nextCueIndex = 0;
        g_lastStartedAvi = aviName;
        QueryPerformanceCounter(&g_startTime);

        Log::Write("[FMV_AD] Started playback: %s (%zu cues, %.1f seconds)",
            track->name.c_str(), track->cues.size(),
            track->cues.back().endTime);
    }

    // ---- Public API ----

    void Initialize(HMODULE dllModule)
    {
        if (g_initialized)
            return;

        g_hModule = dllModule;
        QueryPerformanceFrequency(&g_perfFreq);

        // Load VTT tracks from embedded resources
        for (int i = 0; i < g_resourceTableCount; i++)
        {
            const ResourceEntry& entry = g_resourceTable[i];
            std::string content = LoadResourceString(g_hModule, entry.resourceId);

            if (content.empty())
            {
                Log::Write("[FMV_AD] Failed to load resource %d (%s)",
                    entry.resourceId, entry.vttName);
                continue;
            }

            VttTrack track;
            track.name = entry.vttName;

            if (ParseVttString(content, track))
            {
                std::string key = ToLower(entry.vttName);
                Log::Write("[FMV_AD] Loaded %s: %zu cues (from resource %d)",
                    entry.vttName, track.cues.size(), entry.resourceId);
                g_tracks[key] = std::move(track);
            }
            else
            {
                Log::Write("[FMV_AD] Parsed 0 cues from %s", entry.vttName);
            }
        }

        // Load mapping from embedded resource
        std::string mappingContent = LoadResourceString(g_hModule, IDR_AD_MAPPING);
        ParseMappingString(mappingContent);

        // Apply default mappings for any AVI files not explicitly mapped.
        auto addDefault = [](const char* avi, const char* vtt) {
            std::string aviKey = ToLower(avi);
            if (g_aviToVtt.find(aviKey) == g_aviToVtt.end())
            {
                g_aviToVtt[aviKey] = ToLower(vtt);
                Log::Write("[FMV_AD] Default mapping: %s -> %s", avi, vtt);
            }
        };

        addDefault("disc00_30h.avi", "ff8_intro_ad.vtt");
        addDefault("disc00_23h.avi", "ff8_opening_credits_ad.vtt");

        g_initialized = true;
        Log::Write("[FMV_AD] Initialized: %zu tracks, %zu mappings (all embedded in DLL)",
            g_tracks.size(), g_aviToVtt.size());
    }

    void Shutdown()
    {
        StopPlayback();
        g_tracks.clear();
        g_aviToVtt.clear();
        g_lastStartedAvi.clear();
        g_hModule = nullptr;
        g_initialized = false;
        Log::Write("[FMV_AD] Shutdown.");
    }

    void StopPlayback()
    {
        if (g_playing)
        {
            Log::Write("[FMV_AD] Stopped playback at %.1f seconds (cue %d/%zu)",
                GetElapsedSeconds(), g_nextCueIndex,
                g_currentTrack ? g_currentTrack->cues.size() : 0);
        }
        g_playing = false;
        g_currentTrack = nullptr;
        g_nextCueIndex = 0;
    }

    void OnFrame()
    {
        if (!g_initialized)
            return;

        // ---- Detect new AVI playback via FmvSkip's handle tracking ----
        std::string currentAvi = FmvSkip::GetCurrentAviName();

        if (!currentAvi.empty() && currentAvi != g_lastStartedAvi)
        {
            Log::Write("[FMV_AD] AVI detected via FmvSkip: %s", currentAvi.c_str());

            if (g_playing)
                StopPlayback();

            auto it = g_aviToVtt.find(currentAvi);
            if (it != g_aviToVtt.end())
            {
                auto trackIt = g_tracks.find(it->second);
                if (trackIt != g_tracks.end())
                {
                    Log::Write("[FMV_AD] Matched %s -> %s",
                        currentAvi.c_str(), it->second.c_str());
                    StartPlayback(&trackIt->second, currentAvi);
                }
                else
                {
                    Log::Write("[FMV_AD] Mapping found but track not loaded: %s",
                        it->second.c_str());
                    g_lastStartedAvi = currentAvi;
                }
            }
            else
            {
                Log::Write("[FMV_AD] No mapping for: %s", currentAvi.c_str());
                g_lastStartedAvi = currentAvi;
            }
        }

        // Reset tracking when FMV stops
        if (!FmvSkip::IsMoviePlaying() && !g_lastStartedAvi.empty())
        {
            g_lastStartedAvi.clear();
        }

        // ---- Fire cues during playback ----
        if (!g_playing || !g_currentTrack)
            return;

        double elapsed = GetElapsedSeconds();

        if (g_nextCueIndex >= static_cast<int>(g_currentTrack->cues.size()))
        {
            double lastEnd = g_currentTrack->cues.back().endTime;
            if (elapsed > lastEnd + 2.0)
            {
                Log::Write("[FMV_AD] Playback complete (elapsed %.1f, last cue ended %.1f)",
                    elapsed, lastEnd);
                StopPlayback();
            }
            return;
        }

        while (g_nextCueIndex < static_cast<int>(g_currentTrack->cues.size()))
        {
            const Cue& cue = g_currentTrack->cues[g_nextCueIndex];

            if (elapsed >= cue.startTime)
            {
                Log::Write("[FMV_AD] [%.1fs] Cue %d: %s",
                    elapsed, g_nextCueIndex, cue.text.c_str());

                ScreenReader::Speak(cue.text.c_str(), true);

                g_nextCueIndex++;
            }
            else
            {
                break;
            }
        }
    }

}  // namespace FmvAudioDesc
