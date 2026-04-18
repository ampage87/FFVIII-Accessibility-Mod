// config.h - Persistent accessibility settings via Windows INI file.
// v0.13.51: Introduced to persist speech voice/rate/volume, game BGM volume,
// EWM toggle, and anything else we want to remember across play sessions.
//
// File location: <dll-dir>/ff8_accessibility.ini
// Section: [Accessibility]
//
// Legacy migration: on first load, if ewm_config.txt exists alongside the DLL,
// its value is imported into the new INI and the old file is deleted.

#pragma once

#include <windows.h>
#include <cstddef>

namespace Config {

// Load the INI file (idempotent). Also performs legacy migration on first call.
// Safe to call multiple times — actual I/O only happens once.
void Load();

// Read an integer value. Returns defaultValue if the key is absent.
int  GetInt(const char* key, int defaultValue);

// Write an integer value. Flushed to disk immediately.
void SetInt(const char* key, int value);

// Read a string value into buf (NUL-terminated). Returns true if the key exists
// and is non-empty. If absent, buf receives defaultValue (may be NULL).
bool GetString(const char* key, char* buf, size_t bufSize, const char* defaultValue);

// Write a string value. Flushed to disk immediately. value may be NULL (stored as empty).
void SetString(const char* key, const char* value);

// Returns the absolute path to the INI file (for logging).
const char* GetPath();

}  // namespace Config
