// log.cpp - Dual file logger
// Writes to both the game directory and the project directory.
// The project directory path is hardcoded for development convenience
// and should be removed for production/distribution builds.

#include "ff8_accessibility.h"
#include <cstdio>
#include <cstdarg>
#include <ctime>

namespace Log {

static FILE* s_gameLog = nullptr;
static FILE* s_devLog = nullptr;

// Dev copy path - remove for production builds
static const char* DEV_LOG_PATH = 
    "C:\\Users\\ampag\\OneDrive\\Documents\\FFVIII-Accessibility-Mod"
    "\\FF8_OriginalPC_mod\\Logs\\ff8_accessibility.log";

static void WriteToFile(FILE* f, const char* fmt, va_list args)
{
    if (!f) return;
    
    time_t now = time(nullptr);
    struct tm local;
    localtime_s(&local, &now);
    fprintf(f, "[%02d:%02d:%02d] ",
        local.tm_hour, local.tm_min, local.tm_sec);
    
    vfprintf(f, fmt, args);
    fprintf(f, "\n");
    fflush(f);
}

void Init(const char* filename)
{
    // Open game directory log
    fopen_s(&s_gameLog, filename, "w");
    
    // Open dev directory log
    fopen_s(&s_devLog, DEV_LOG_PATH, "w");
}

void Write(const char* fmt, ...)
{
    va_list args;
    
    if (s_gameLog) {
        va_start(args, fmt);
        WriteToFile(s_gameLog, fmt, args);
        va_end(args);
    }
    
    if (s_devLog) {
        va_start(args, fmt);
        WriteToFile(s_devLog, fmt, args);
        va_end(args);
    }
}

void Close()
{
    if (s_gameLog || s_devLog) {
        Write("=== Log closed ===");
    }
    if (s_gameLog) {
        fclose(s_gameLog);
        s_gameLog = nullptr;
    }
    if (s_devLog) {
        fclose(s_devLog);
        s_devLog = nullptr;
    }
}

}  // namespace Log
