// nav_log.cpp - Persistent navigation data logger
//
// Accumulates structured navigation data across game sessions.
// Uses APPEND mode so data is never lost between sessions.
// Output is tab-separated for easy parsing by Python/analysis tools.
//
// File: ff8_nav_data.log (in both game dir and dev dir)
//
// Line format: [timestamp] TYPE\tfield1\tfield2\t...
// Types:
//   SESSION    - New game session started
//   FIELD      - Field loaded
//   DRIVE_START - Auto-drive initiated
//   DRIVE_WP   - Waypoint reached during drive
//   DRIVE_SAMP - Periodic position sample during drive
//   DRIVE_REC  - Recovery phase triggered
//   DRIVE_END  - Auto-drive completed
//   COORD      - 3D↔2D coordinate sample (for camera research)

#include "ff8_accessibility.h"
#include <cstdio>
#include <cstdarg>
#include <ctime>
#include <cstring>

namespace NavLog {

static FILE* s_gameLog = nullptr;
static FILE* s_devLog = nullptr;

// Dev copy path - mirrors the debug log pattern
static const char* DEV_NAV_PATH =
    "C:\\Users\\ampag\\OneDrive\\Documents\\FFVIII-Accessibility-Mod"
    "\\FF8_OriginalPC_mod\\Logs\\ff8_nav_data.log";

static const char* GAME_NAV_FILENAME = "ff8_nav_data.log";

// Internal: write a pre-formatted line to both files
static void WriteBoth(const char* line)
{
    time_t now = time(nullptr);
    struct tm local;
    localtime_s(&local, &now);

    char prefix[32];
    snprintf(prefix, sizeof(prefix), "[%04d-%02d-%02d %02d:%02d:%02d] ",
             local.tm_year + 1900, local.tm_mon + 1, local.tm_mday,
             local.tm_hour, local.tm_min, local.tm_sec);

    if (s_gameLog) {
        fputs(prefix, s_gameLog);
        fputs(line, s_gameLog);
        fputc('\n', s_gameLog);
    }
    if (s_devLog) {
        fputs(prefix, s_devLog);
        fputs(line, s_devLog);
        fputc('\n', s_devLog);
    }
    // Periodic flush — avoid flushing every write for performance.
    // Flush every 5 seconds instead, and always on Close().
    static DWORD s_lastFlush = 0;
    DWORD tickNow = GetTickCount();
    if (tickNow - s_lastFlush > 5000) {
        if (s_gameLog) fflush(s_gameLog);
        if (s_devLog)  fflush(s_devLog);
        s_lastFlush = tickNow;
    }
}

// Internal: formatted write
static void WriteF(const char* fmt, ...)
{
    char buf[1024];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    WriteBoth(buf);
}

// ========================================================================
// Public API
// ========================================================================

void Init()
{
    // Append mode — never truncates
    fopen_s(&s_gameLog, GAME_NAV_FILENAME, "a");
    fopen_s(&s_devLog, DEV_NAV_PATH, "a");
}

void Close()
{
    if (s_gameLog) { fflush(s_gameLog); fclose(s_gameLog); s_gameLog = nullptr; }
    if (s_devLog)  { fflush(s_devLog);  fclose(s_devLog);  s_devLog = nullptr; }
}

void SessionStart()
{
    WriteBoth("SESSION\t" FF8OPC_VERSION "\t" FF8OPC_VERSION_DATE);
}

void FieldLoad(const char* fieldName, int fieldId, int numTris,
               int numEntities, int numExits, int numEvents)
{
    WriteF("FIELD\t%s\t%d\t%d\t%d\t%d\t%d",
           fieldName, fieldId, numTris, numEntities, numExits, numEvents);
}

void DriveStart(const char* fieldName, const char* targetName, const char* targetType,
                int startTri, float startX, float startY,
                int goalTri, float goalX, float goalY, float talkRadius,
                int astarTris, int waypointCount, bool usedFunnel)
{
    WriteF("DRIVE_START\t%s\t%s\t%s\t%d\t%.0f\t%.0f\t%d\t%.0f\t%.0f\t%.0f\t%d\t%d\t%s",
           fieldName, targetName, targetType,
           startTri, startX, startY,
           goalTri, goalX, goalY, talkRadius,
           astarTris, waypointCount,
           usedFunnel ? "funnel" : "centers");
}

void DriveWaypoint(int wpIndex, int wpTotal, float playerX, float playerY,
                   float distToTarget, int tick)
{
    WriteF("DRIVE_WP\t%d\t%d\t%.0f\t%.0f\t%.0f\t%d",
           wpIndex, wpTotal, playerX, playerY, distToTarget, tick);
}

void DriveSample(float playerX, float playerY, int playerTri,
                 float distToTarget, int wpIndex, int wpTotal, int tick)
{
    WriteF("DRIVE_SAMP\t%.0f\t%.0f\t%d\t%.0f\t%d\t%d\t%d",
           playerX, playerY, playerTri, distToTarget, wpIndex, wpTotal, tick);
}

void DriveRecovery(int phase, int playerTri, float playerX, float playerY,
                   float distToTarget)
{
    WriteF("DRIVE_REC\t%d\t%d\t%.0f\t%.0f\t%.0f",
           phase, playerTri, playerX, playerY, distToTarget);
}

void DriveEnd(const char* result, int totalTicks, float finalDist,
              int recoveryPhases, float startDist)
{
    WriteF("DRIVE_END\t%s\t%d\t%.0f\t%d\t%.0f",
           result, totalTicks, finalDist, recoveryPhases, startDist);
}

void CoordSample(const char* fieldName, int triIdx,
                 float posX, float posY,
                 float wx, float wy, float wz)
{
    WriteF("COORD\t%s\t%d\t%.1f\t%.1f\t%.1f\t%.1f\t%.1f",
           fieldName, triIdx, posX, posY, wx, wy, wz);
}

}  // namespace NavLog
