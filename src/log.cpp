// log.cpp - Multi-channel file logger with auto-archiving
// v0.12.18 — Refactored from single-file to domain-specific log channels.
//
// Channels:
//   MOD    → ff8_mod.log     Core init, module loading, version info, errors
//   BATTLE → ff8_battle.log  Battle TTS, EWM, GF events
//   FIELD  → ff8_field.log   Field navigation, catalog, GPS, auto-drive
//   WORLD  → ff8_world.log   World map navigation
//   MENU   → ff8_menu.log    Menu TTS, junction, items, save screen
//   DIALOG → ff8_dialog.log  Field dialog hooks, MES/ASK events
//
// Each log overwrites on game launch. Before overwriting, the previous log
// is moved to Logs/archive/ with a timestamp from its last-modified time.
//
// Log::Write() remains for backward compatibility — writes to ff8_mod.log.
// Migrate calls to domain-specific functions (Log::Battle, Log::Field, etc.)
// during source file splits.

#include "ff8_accessibility.h"
#include "mod_forward_decls.h"
#include <cstdio>
#include <cstdarg>
#include <ctime>
#include <sys/stat.h>
#include <direct.h>

namespace Log {

// ---------------------------------------------------------------------------
// Channel definitions
// ---------------------------------------------------------------------------

enum ChannelIndex {
    CH_MOD = 0,
    CH_BATTLE,
    CH_FIELD,
    CH_WORLD,
    CH_MENU,
    CH_DIALOG,
    CH_COUNT
};

static const char* CHANNEL_FILENAMES[CH_COUNT] = {
    "ff8_mod.log",
    "ff8_battle.log",
    "ff8_field.log",
    "ff8_world.log",
    "ff8_menu.log",
    "ff8_dialog.log"
};

static const char* CHANNEL_PREFIXES[CH_COUNT] = {
    "MOD",
    "BATTLE",
    "FIELD",
    "WORLD",
    "MENU",
    "DIALOG"
};

// Two copies of each log: one in the game directory, one in the dev project
static FILE* s_gameLogs[CH_COUNT] = {};
static FILE* s_devLogs[CH_COUNT] = {};

// Dev copy base path
static const char* DEV_LOG_DIR =
    "C:\\Users\\ampag\\OneDrive\\Documents\\FFVIII-Accessibility-Mod"
    "\\FF8_OriginalPC_mod\\Logs\\";

static const char* DEV_ARCHIVE_DIR =
    "C:\\Users\\ampag\\OneDrive\\Documents\\FFVIII-Accessibility-Mod"
    "\\FF8_OriginalPC_mod\\Logs\\archive\\";

// Game directory base path (set during Init)
static char s_gameLogDir[MAX_PATH] = {};

// ---------------------------------------------------------------------------
// Archive helper — move old log to archive/ with timestamp
// ---------------------------------------------------------------------------

static void ArchiveOldLog(const char* logPath, const char* archiveDir, const char* filename)
{
    // Check if old log exists
    struct _stat st;
    if (_stat(logPath, &st) != 0)
        return;  // No old log to archive

    // Ensure archive directory exists
    _mkdir(archiveDir);

    // Get last-modified time for the archive filename
    struct tm local;
    localtime_s(&local, &st.st_mtime);

    // Build archive filename: ff8_battle_2026-04-06_14-30-22.log
    // Strip .log extension from filename, insert timestamp, re-add .log
    char baseName[256];
    strncpy_s(baseName, filename, sizeof(baseName));
    char* dot = strrchr(baseName, '.');
    if (dot) *dot = '\0';

    char archivePath[MAX_PATH];
    snprintf(archivePath, sizeof(archivePath),
        "%s%s_%04d-%02d-%02d_%02d-%02d-%02d.log",
        archiveDir, baseName,
        local.tm_year + 1900, local.tm_mon + 1, local.tm_mday,
        local.tm_hour, local.tm_min, local.tm_sec);

    // Move (rename) old log to archive
    // If rename fails (cross-device), fall back to copy+delete
    if (rename(logPath, archivePath) != 0) {
        // Cross-device: copy then delete
        FILE* src = nullptr;
        FILE* dst = nullptr;
        fopen_s(&src, logPath, "rb");
        fopen_s(&dst, archivePath, "wb");
        if (src && dst) {
            char buf[4096];
            size_t n;
            while ((n = fread(buf, 1, sizeof(buf), src)) > 0)
                fwrite(buf, 1, n, dst);
        }
        if (src) fclose(src);
        if (dst) fclose(dst);
        // Original will be overwritten by fopen "w" below
    }
}

// ---------------------------------------------------------------------------
// Internal write to a single file handle
// ---------------------------------------------------------------------------

static void WriteToFile(FILE* f, const char* prefix, const char* fmt, va_list args)
{
    if (!f) return;

    time_t now = time(nullptr);
    struct tm local;
    localtime_s(&local, &now);

    if (prefix && prefix[0]) {
        fprintf(f, "[%02d:%02d:%02d][%s] ",
            local.tm_hour, local.tm_min, local.tm_sec, prefix);
    } else {
        fprintf(f, "[%02d:%02d:%02d] ",
            local.tm_hour, local.tm_min, local.tm_sec);
    }

    vfprintf(f, fmt, args);
    fprintf(f, "\n");
    fflush(f);
}

// ---------------------------------------------------------------------------
// Write to a specific channel (both game + dev copies)
// ---------------------------------------------------------------------------

static void WriteChannel(ChannelIndex ch, const char* fmt, va_list args)
{
    const char* prefix = CHANNEL_PREFIXES[ch];

    if (s_gameLogs[ch]) {
        va_list copy;
        va_copy(copy, args);
        WriteToFile(s_gameLogs[ch], prefix, fmt, copy);
        va_end(copy);
    }

    if (s_devLogs[ch]) {
        va_list copy;
        va_copy(copy, args);
        WriteToFile(s_devLogs[ch], prefix, fmt, copy);
        va_end(copy);
    }
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

void Init(const char* gameLogFilename)
{
    // Extract game directory from the provided filename
    // (gameLogFilename is typically "ff8_accessibility.log" in the game dir)
    // We'll use the current working directory (game dir) as the base
    // and just open files by name there.
    // For the dev logs, we use the hardcoded DEV_LOG_DIR path.

    // Ensure dev archive directory exists
    _mkdir(DEV_ARCHIVE_DIR);

    for (int i = 0; i < CH_COUNT; i++) {
        const char* fn = CHANNEL_FILENAMES[i];

        // Archive old dev log before overwriting
        char devPath[MAX_PATH];
        snprintf(devPath, sizeof(devPath), "%s%s", DEV_LOG_DIR, fn);
        ArchiveOldLog(devPath, DEV_ARCHIVE_DIR, fn);

        // Open game directory log (simple filename = current working dir = game dir)
        fopen_s(&s_gameLogs[i], fn, "w");

        // Open dev directory log
        fopen_s(&s_devLogs[i], devPath, "w");
    }

    // Write init header to MOD channel
    time_t now = time(nullptr);
    struct tm local;
    localtime_s(&local, &now);
    
    Mod("=== FF8 Accessibility Mod v%s — Log opened %04d-%02d-%02d %02d:%02d:%02d ===",
        FF8OPC_VERSION,
        local.tm_year + 1900, local.tm_mon + 1, local.tm_mday,
        local.tm_hour, local.tm_min, local.tm_sec);
    
    Mod("Log channels: MOD, BATTLE, FIELD, WORLD, MENU, DIALOG");
}

void Close()
{
    Mod("=== Log closed ===");

    for (int i = 0; i < CH_COUNT; i++) {
        if (s_gameLogs[i]) { fclose(s_gameLogs[i]); s_gameLogs[i] = nullptr; }
        if (s_devLogs[i])  { fclose(s_devLogs[i]);  s_devLogs[i]  = nullptr; }
    }
}

// Backward-compatible Write() — goes to MOD channel without prefix
// (preserves existing log format for un-migrated calls)
void Write(const char* fmt, ...)
{
    if (!s_gameLogs[CH_MOD] && !s_devLogs[CH_MOD]) return;

    // Write to MOD channel without the [MOD] prefix for backward compat
    va_list args;

    if (s_gameLogs[CH_MOD]) {
        va_start(args, fmt);
        WriteToFile(s_gameLogs[CH_MOD], nullptr, fmt, args);
        va_end(args);
    }

    if (s_devLogs[CH_MOD]) {
        va_start(args, fmt);
        WriteToFile(s_devLogs[CH_MOD], nullptr, fmt, args);
        va_end(args);
    }
}

// Channel-specific functions
void Mod(const char* fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    WriteChannel(CH_MOD, fmt, args);
    va_end(args);
}

void Battle(const char* fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    WriteChannel(CH_BATTLE, fmt, args);
    va_end(args);
}

void Field(const char* fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    WriteChannel(CH_FIELD, fmt, args);
    va_end(args);
}

void World(const char* fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    WriteChannel(CH_WORLD, fmt, args);
    va_end(args);
}

void Menu(const char* fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    WriteChannel(CH_MENU, fmt, args);
    va_end(args);
}

void Dialog(const char* fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    WriteChannel(CH_DIALOG, fmt, args);
    va_end(args);
}

}  // namespace Log
