// ff8_accessibility.h - Core header for FF8 Original PC Accessibility Mod
#pragma once

#include <windows.h>
#include <cstdint>
#include <string>

// ================================================================
// FF8 Original PC Accessibility Mod version
// Increment on every build change
// ================================================================
#define FF8OPC_VERSION "0.13.61"  // GitHub catch-up: sessions 65–76 batch push, baseline for session 77 work
#define FF8OPC_VERSION_DATE "2026-04-17"

// ============================================================================
// FF8 Runtime Address Resolution
// See ff8_addresses.h / ff8_addresses.cpp for the resolver that computes
// addresses at runtime using the same offset-chain technique as FFNx.
// ============================================================================

#include "ff8_addresses.h"

// ============================================================================
// Persistent settings (v0.13.51)
// Back-end for SAPI voice/rate/volume, EWM toggle, BGM volume, etc.
// ============================================================================

#include "config.h"

// ============================================================================
// Screen Reader Interface (NVDA direct + SAPI fallback)
// nvdaControllerClient.dll is embedded as a resource and extracted at runtime.
// No external Tolk.dll or screen reader DLLs needed.
// ============================================================================

namespace ScreenReader {

bool Initialize(HMODULE hModule);
void Shutdown();
bool IsAvailable();

// Speak text, optionally interrupting current speech
bool Speak(const wchar_t* text, bool interrupt = true);

// Speak text (narrow string convenience wrapper)
bool Speak(const char* text, bool interrupt = true);

// Output to both speech and braille
bool Output(const wchar_t* text, bool interrupt = true);
bool Output(const char* text, bool interrupt = true);

// Silence current speech
bool Silence();

// v0.13.51: Returns true while SAPI (voice 1 or voice 2) is actively rendering
// audio. Used by BattleTTS EWM to hold ATB until damage TTS finishes.
bool IsSpeaking();

// Get detected screen reader name (empty if none)
std::string GetScreenReaderName();

// v04.25: SAPI voice cycling and rate control
void CycleVoice();      // Switch to next available SAPI voice (F1)
void IncreaseRate();    // Increase SAPI speech rate (F8)
void DecreaseRate();    // Decrease SAPI speech rate (F7)
void SetRate(long rate); // Set rate silently, no announcement (startup default)
void RepeatLast();      // Repeat last spoken dialog

// v05.13: SAPI volume control
void IncreaseVolume();  // Increase SAPI speech volume by 10% (F12)
void DecreaseVolume();  // Decrease SAPI speech volume by 10% (F11)

// v0.10.32: Channel 2 — independent SAPI voice for battle events/damage
// Plays on a separate ISpVoice instance so it overlaps with menu speech.
bool SpeakChannel2(const wchar_t* text, bool interrupt = false);
bool SpeakChannel2(const char* text, bool interrupt = false);

}  // namespace ScreenReader

// ============================================================================
// Title Screen Module
// ============================================================================

namespace TitleScreen {

void Initialize();
void Activate();    // Call when title screen is entered
void Deactivate();  // Call when title screen is exited
void Update();      // Called each frame/tick
void Shutdown();

}  // namespace TitleScreen

// ============================================================================
// FMV Audio Description Module
// ============================================================================

#include "fmv_audio_desc.h"

// ============================================================================
// FMV Skip Module
// ============================================================================

#include "fmv_skip.h"

// ============================================================================
// Field Dialog Module (v04.00)
// ============================================================================

#include "field_dialog.h"

// ============================================================================
// Field Navigation Module (v05.00)
// ============================================================================

#include "field_archive.h"
#include "field_navigation.h"

// ============================================================================
// Game Audio Module (v0.09.22)
// ============================================================================

#include "game_audio.h"

// ============================================================================
// World Map Module (v0.11.03)
// ============================================================================

#include "world_map.h"

// ============================================================================
// FF8 Text Decoder (v04.00)
// ============================================================================

#include "ff8_text_decode.h"

// ============================================================================
// Logging
// ============================================================================

namespace Log {

void Init(const char* gameLogFilename);
void Close();

// Backward-compatible general write — goes to ff8_mod.log without channel prefix.
// Use domain-specific functions below for new code.
void Write(const char* fmt, ...);

// Domain-specific log channels — each writes to its own file.
// Use these in new/refactored code.
void Mod(const char* fmt, ...);     // ff8_mod.log     — core init, modules, errors
void Battle(const char* fmt, ...);  // ff8_battle.log  — battle TTS, EWM, GF
void Field(const char* fmt, ...);   // ff8_field.log   — field nav, catalog, GPS
void World(const char* fmt, ...);   // ff8_world.log   — world map navigation
void Menu(const char* fmt, ...);    // ff8_menu.log    — menu TTS, junction, items
void Dialog(const char* fmt, ...);  // ff8_dialog.log  — field dialog hooks

}  // namespace Log

// ============================================================================
// Navigation Data Log (persistent, append-mode)
// Accumulates structured navigation data across game sessions for analysis.
// Separate from the debug log — this file is never truncated.
// ============================================================================

namespace NavLog {

void Init();                        // Open/create nav log in append mode
void Close();                       // Flush and close

// Session/field events
void SessionStart();                // Log session header with version + timestamp
void FieldLoad(const char* fieldName, int fieldId, int numTris, int numEntities,
               int numExits, int numEvents);

// Auto-drive lifecycle
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

// Coordinate mapping: logs a known 3D↔2D correspondence for camera research
void CoordSample(const char* fieldName, int triIdx,
                 float posX, float posY,   // entity 2D coords
                 float wx, float wy, float wz);  // walkmesh 3D coords (if available)

}  // namespace NavLog
