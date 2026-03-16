// Tolk.h - Screen reader abstraction library
// Download Tolk from: https://github.com/ndarilek/tolk/releases
// Place Tolk.dll and Tolk.lib in the lib/ directory
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

// Initialize Tolk. Call before any other functions.
void __declspec(dllimport) Tolk_Load();

// Check if Tolk is loaded.
bool __declspec(dllimport) Tolk_IsLoaded();

// Unload Tolk. Call during cleanup.
void __declspec(dllimport) Tolk_Unload();

// Enable/disable SAPI fallback if no screen reader is active.
void __declspec(dllimport) Tolk_TrySAPI(bool trySAPI);

// Detect the currently active screen reader. Returns a string name or NULL.
const wchar_t* __declspec(dllimport) Tolk_DetectScreenReader();

// Check if the current screen reader supports speech/braille.
bool __declspec(dllimport) Tolk_HasSpeech();
bool __declspec(dllimport) Tolk_HasBraille();

// Output text via speech and braille (if available).
// If interrupt is true, stops current speech first.
bool __declspec(dllimport) Tolk_Output(const wchar_t* str, bool interrupt);

// Output text via speech only.
bool __declspec(dllimport) Tolk_Speak(const wchar_t* str, bool interrupt);

// Output text via braille only.
bool __declspec(dllimport) Tolk_Braille(const wchar_t* str);

// Silence current speech.
bool __declspec(dllimport) Tolk_Silence();

#ifdef __cplusplus
}
#endif
