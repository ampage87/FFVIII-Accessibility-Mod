// mod_forward_decls.h - Centralized cross-module namespace forward declarations.
//
// Created v0.14.30 during build recovery. The mod's source layout has every
// .cpp file declare forward references for the cross-module namespaces it
// uses (Log, ScreenReader, Config, NavLog), since there is no per-namespace
// header. A prior session deleted those inline forward declarations from
// many files. Centralizing them here means: one include in every .cpp,
// no per-file maintenance, and any future damage of this kind only needs
// to be patched in one place.
//
// Definitions live in:
//   Log::*           -> log.cpp
//   ScreenReader::*  -> screen_reader.cpp
//   Config::*        -> config.cpp
//   NavLog::*        -> nav_log.cpp
//
// Including this header is also sufficient to satisfy *intra*-module
// forward references — e.g. log.cpp's Init() calls Log::Mod() which is
// defined later in the same file. Including this header at the top of
// log.cpp resolves the call to the declaration here.

#pragma once

#include <windows.h>
#include <cstddef>

// ============================================================================
// Log — multi-channel file logger (definitions in log.cpp)
// ============================================================================
namespace Log {
    void Init(const char* gameLogFilename);
    void Close();

    // Backward-compatible single-channel writer (goes to MOD channel)
    void Write(const char* format, ...);

    // Domain-specific channels
    void Mod(const char* format, ...);
    void Battle(const char* format, ...);
    void Field(const char* format, ...);
    void World(const char* format, ...);
    void Menu(const char* format, ...);
    void Dialog(const char* format, ...);
}

// ============================================================================
// ScreenReader — NVDA + SAPI speech output (definitions in screen_reader.cpp)
// ============================================================================
namespace ScreenReader {
    bool Initialize(HMODULE hModule);
    void Shutdown();
    bool IsAvailable();
    bool IsSpeaking();

    // Speech (queued by default, interrupt=true purges + speaks immediately)
    bool Speak(const char* text, bool interrupt = false);
    bool Speak(const wchar_t* text, bool interrupt = false);
    bool Output(const char* text, bool interrupt = false);
    bool Output(const wchar_t* text, bool interrupt = false);
    bool Silence();

    // Channel 2 — battle events / damage announcements (independent SAPI voice)
    bool SpeakChannel2(const char* text, bool interrupt = false);
    bool SpeakChannel2(const wchar_t* text, bool interrupt = false);

    // Voice / rate / volume controls (F1 / F7 / F8 / F5 / F6 hotkeys)
    void CycleVoice();
    void IncreaseRate();
    void DecreaseRate();
    void SetRate(long rate);
    void IncreaseVolume();
    void DecreaseVolume();
    void RepeatLast();
}

// ============================================================================
// Config — INI-backed persistent settings (definitions in config.cpp)
// ============================================================================
namespace Config {
    void Load();
    int  GetInt(const char* key, int defaultValue);
    void SetInt(const char* key, int value);
    bool GetString(const char* key, char* outBuf, size_t bufSize, const char* defaultValue);
    void SetString(const char* key, const char* value);
    const char* GetPath();
}

// ============================================================================
// NavLog — persistent navigation data logger (definitions in nav_log.cpp)
// ============================================================================
namespace NavLog {
    void Init();
    void Close();
    void SessionStart();
    void FieldLoad(const char* fieldName, int fieldId, int numTris,
                   int numEntities, int numExits, int numEvents);
    void DriveStart(const char* fieldName, const char* targetName, const char* targetType,
                    int startTri, float startX, float startY,
                    int goalTri, float goalX, float goalY, float talkRadius,
                    int astarTris, int waypointCount, bool usedFunnel);
    void DriveWaypoint(int wpIndex, int wpTotal, float playerX, float playerY,
                       float distToTarget, int tick);
    void DriveSample(float playerX, float playerY, int playerTri,
                     float distToTarget, int wpIndex, int wpTotal, int tick);
    void DriveRecovery(int phase, int playerTri, float playerX, float playerY,
                       float distToTarget);
    void DriveEnd(const char* result, int totalTicks, float finalDist,
                  int recoveryPhases, float startDist);
    void CoordSample(const char* fieldName, int triIdx, float posX, float posY,
                     float wx, float wy, float wz);
}
