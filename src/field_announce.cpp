// field_announce.cpp - Automatic field-name announcement on field load.
// See field_announce.h for design notes.

#include "field_announce.h"
#include "field_display_names.h"
#include "ff8_accessibility.h"
#include "ff8_addresses.h"
#include "mod_forward_decls.h"

#include <windows.h>
#include <cstdint>
#include <cstdio>   // v0.18.3.301 (#90): snprintf for the floor announcement

namespace FieldAnnounce {

// ============================================================================
// Constants
// ============================================================================

// Debounce: hold the new fieldId stable for this long before announcing.
// 800ms is well past the typical 100-300ms loading flash for a normal
// field transition and short enough that it doesn't feel laggy.
static const DWORD DEBOUNCE_MS = 800;

// FIELD_DISPLAY_NAMES catalog size. Computed from the array literal in
// field_display_names.h (982 entries indexed by fieldId).
static const int FIELD_DISPLAY_NAMES_COUNT =
    (int)(sizeof(FIELD_DISPLAY_NAMES) / sizeof(FIELD_DISPLAY_NAMES[0]));

// ============================================================================
// State
// ============================================================================

// ============================================================================
// v0.18.3.301 (#90): D-District Prison floor announcement.
// ============================================================================
//
// The shaft changes floor WITHOUT changing fieldId -- the lift and the stairs
// both MAPJUMP 795 -> 795 -- so the fieldId-change trigger below never fires
// and 23 floor changes in the 2026-07-31 BAT announced NOTHING at all. On top
// of that the display name is an archive ordinal, not a place: Aaron heard
// "Galbadia D-District Prison 3" and "...5" alternating 13 times during a
// three-floor descent.
//
// The floor lives in field varblock 0x01B5 (437), found by the .298 probe and
// pinned by the .299 auto-capture: two screenshots read "Floor 6" while the
// varblock held 5, and "Floor 4" while it held 3. So the varblock is
// floor MINUS ONE and the announcement needs +1. (The engine's own display
// slot FIELD_VAR_TABLE_BASE[0x20] holds the true value, but it is only live
// while the Floor window renders; the varblock is readable continuously,
// which is what an announce-time read needs.)
//
// 0x031A/0x031B (gpbig1/gpbig1a) and 0x031C/0x031D (gpbig2/gpbig2a) are not
// separate rooms -- they are the LEFT and RIGHT halves of one circular
// walkway around the central shaft, per Aaron's description of the geometry,
// which the INF data corroborates exactly: each half carries two gateways at
// mirrored heights (y ~= +1800 and y ~= -1850), the top and bottom of the
// ring, and crossing between them changes no floor (10 crossings, 0 changes).
// So "Prison 3" / "Prison 5" carry no information a player can use, and are
// replaced outright by the side of the ring. Other shaft fields (cells,
// corridors) keep their name and gain the floor.
static const uintptr_t ANNOUNCE_VB_BASE  = 0x01CFE9B8;  // EXIT_VARBLOCK_BASE
static const unsigned  ANNOUNCE_VB_FLOOR = 0x01B5;      // holds floor - 1

static bool IsPrisonShaftField(uint16_t fid)
{
    return (fid >= 0x0319 && fid <= 0x032E) || fid == 0x03C5;
}

// Returns the 1-based floor, or -1 when not in the shaft / unreadable.
// SEH-guarded, no C++ objects (C2712).
static int ReadPrisonFloor(uint16_t fid)
{
    if (!IsPrisonShaftField(fid)) return -1;
    int v = -1;
    __try {
        v = (int)*(volatile uint8_t*)(ANNOUNCE_VB_BASE + ANNOUNCE_VB_FLOOR) + 1;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        v = -1;
    }
    return v;
}

// Last fieldId we announced. 0xFFFF = nothing announced yet.
static uint16_t s_lastAnnouncedFieldId = 0xFFFF;

// v0.18.3.301 (#90): the floor that accompanied it. Paired with the fieldId
// so a floor change on an UNCHANGED fieldId still announces -- without this
// the shaft stays silent, which is the whole bug.
static int s_lastAnnouncedFloor = -1;
static int s_observedFloor      = -1;

// v0.20.24: camera-zone index (0x1CE4906) is part of the on-screen identity, just
// like the prison floor. A multi-camera field (e.g. B-Garden Classroom 1 has 2
// cameras) switches this as the player crosses between its camera zones; when it
// changes we re-announce the field name + which camera, so a blind player knows the
// screen -- and therefore the catalog -- has changed. Per-field ordinals give a
// stable "camera 1 / camera 2" instead of the arbitrary raw zone byte.
static int     s_lastAnnouncedCamZone = -1;
static int     s_observedCamZone      = -1;
static uint8_t s_camZoneSeen[8]       = {};
static int     s_camZoneSeenCount     = 0;

// Last fieldId we observed (post-debounce). When the live fieldId
// differs from this, we treat it as a candidate change and start the
// debounce timer.
static uint16_t s_observedFieldId = 0xFFFF;

// Tick count when s_observedFieldId most recently changed. Once
// (now - s_changeTick) >= DEBOUNCE_MS and the live fieldId still
// matches s_observedFieldId, we announce.
static DWORD s_changeTick = 0;

// True while we're waiting for the debounce window to elapse.
static bool s_pendingAnnounce = false;

// ============================================================================
// Helpers
// ============================================================================

static uint16_t ReadFieldId()
{
    if (!FF8Addresses::pCurrentFieldId) return 0xFFFF;
    __try {
        return *FF8Addresses::pCurrentFieldId;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return 0xFFFF;
    }
}

// v0.20.24: live camera-zone index (0x1CE4906). -1 = unreadable. SEH-guarded,
// no C++ objects (C2712).
static int ReadCamZone()
{
    __try {
        return (int)*(volatile uint8_t*)(uintptr_t)0x01CE4906u;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return -1;
    }
}

// v0.20.25 DIAG (Aaron / camera-zone exits): static RE proved bgroom_1 (Classroom 1)
// NEVER calls the zone-set opcode 0x11C, so the zone byte 0x1CE4906 stays 0 there and
// the v0.20.24 "camera N" announce cannot fire in the classroom. The real front/back
// view is switched by 4 'jump' trigger lines via a REQ chain -- but WHICH engine byte
// records the active view is unknown. Watch a window of the camera-state cluster and
// log any byte that changes WITHIN a field (baseline re-taken on each field change, so
// only within-field front/back changes are logged). Pure observe-only.
static uint8_t  s_camWatchPrev[0x40] = {};
static uint16_t s_camWatchField      = 0xFFFF;
static bool     s_camWatchInit       = false;
static void WatchCameraCluster(uint16_t curId)
{
    __try {
        const uintptr_t BASE = 0x01CE4900u;
        if (curId != s_camWatchField) {          // new field: re-baseline silently
            s_camWatchField = curId;
            for (int i = 0; i < 0x40; i++)
                s_camWatchPrev[i] = *(volatile uint8_t*)(BASE + i);
            s_camWatchInit = true;
            return;
        }
        if (!s_camWatchInit) return;
        for (int i = 0; i < 0x40; i++) {
            uint8_t v = *(volatile uint8_t*)(BASE + i);
            if (v != s_camWatchPrev[i]) {
                Log::Mod("FieldAnnounce: [CAMBYTE] field=0x%04X 0x%08X: %u -> %u [v0.20.25 diag]",
                         (unsigned)curId, (unsigned)(BASE + i),
                         (unsigned)s_camWatchPrev[i], (unsigned)v);
                s_camWatchPrev[i] = v;
            }
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
}

// 1-based ordinal of a camera-zone id within the current field visit (a new
// ordinal is assigned the first time each zone id is seen). 0 if unreadable.
static int CamZoneOrdinal(int cam)
{
    if (cam < 0) return 0;
    for (int i = 0; i < s_camZoneSeenCount; i++)
        if (s_camZoneSeen[i] == (uint8_t)cam) return i + 1;
    if (s_camZoneSeenCount < 8) { s_camZoneSeen[s_camZoneSeenCount++] = (uint8_t)cam; return s_camZoneSeenCount; }
    return 0;
}

static bool IsAnnounceable(uint16_t fieldId)
{
    if (fieldId == 0xFFFF) return false;
    if (fieldId == 0)      return false;  // title screen / startup
    if ((int)fieldId >= FIELD_DISPLAY_NAMES_COUNT) return false;
    return true;
}

// ============================================================================
// Public API
// ============================================================================

void Initialize()
{
    s_lastAnnouncedFieldId = 0xFFFF;
    s_observedFieldId      = 0xFFFF;
    s_changeTick           = 0;
    s_pendingAnnounce      = false;

    Log::Mod("FieldAnnounce: Initialized (v0.15.2.14). Will announce "
             "the FIELD_DISPLAY_NAMES catalog name on each new field "
             "load (debounce=%lums, %d-entry catalog).",
             (unsigned long)DEBOUNCE_MS, FIELD_DISPLAY_NAMES_COUNT);
}

void Shutdown()
{
    s_lastAnnouncedFieldId = 0xFFFF;
    s_observedFieldId      = 0xFFFF;
    s_changeTick           = 0;
    s_pendingAnnounce      = false;
}

void Update()
{
    // Only meaningful on the field. Battle, menu, world map, etc. don't
    // count -- a field re-entry from those will be detected by the
    // fieldId change anyway when control returns.
    if (!FF8Addresses::IsOnField()) return;

    uint16_t curId = ReadFieldId();
    if (!IsAnnounceable(curId)) return;

    // WatchCameraCluster(curId);  // v0.20.25 diag: 0x1CE4906 confirmed as front/back byte; watch disabled

    // v0.18.3.301 (#90): the floor is part of the identity, not just the
    // fieldId. In the prison shaft the fieldId does NOT change between floors,
    // so watching it alone leaves every floor change silent.
    int curFloor = ReadPrisonFloor(curId);
    int curCam   = ReadCamZone();   // v0.20.24: camera-zone, part of the screen identity

    if (curId != s_observedFieldId || curFloor != s_observedFloor || curCam != s_observedCamZone) {
        // FieldId, floor, or camera-zone just changed. Start (or restart) the debounce timer.
        s_observedFieldId = curId;
        s_observedFloor   = curFloor;
        s_observedCamZone = curCam;
        s_changeTick      = GetTickCount();
        s_pendingAnnounce = true;
        return;
    }

    if (!s_pendingAnnounce) return;

    if ((GetTickCount() - s_changeTick) < DEBOUNCE_MS) return;

    // Debounce elapsed and fieldId is stable. Decide whether to speak.
    s_pendingAnnounce = false;

    if (curId == s_lastAnnouncedFieldId && curFloor == s_lastAnnouncedFloor && curCam == s_lastAnnouncedCamZone) {
        // Already announced this field AND floor (e.g. mode flipped to battle
        // and back without a real change). Don't repeat.
        return;
    }

    const char* name = FIELD_DISPLAY_NAMES[curId];
    if (!name || !*name) return;

    // v0.20.24: a genuine new field visit resets the per-field camera-zone ordinals.
    bool fieldChanged = (curId != s_lastAnnouncedFieldId);
    if (fieldChanged) s_camZoneSeenCount = 0;
    int camOrd = CamZoneOrdinal(curCam);

    s_lastAnnouncedFieldId = curId;
    s_lastAnnouncedFloor   = curFloor;
    s_lastAnnouncedCamZone = curCam;

    // v0.18.3.301 (#90): compose the prison shaft announcement. The ring
    // halves lose their archive ordinal entirely -- "Floor 4, left side" is
    // the pair of facts a player can actually navigate with, where "Galbadia
    // D-District Prison 3" is neither a floor nor a place. Everything else
    // keeps its name and gains the floor.
    char buf[160];
    const char* spoken = name;
    if (curFloor > 0) {
        const char* side = nullptr;
        if (curId == 0x031A || curId == 0x031B)      side = "left side";
        else if (curId == 0x031C || curId == 0x031D) side = "right side";
        if (side) snprintf(buf, sizeof(buf), "Floor %d, %s", curFloor, side);
        else      snprintf(buf, sizeof(buf), "%s, Floor %d", name, curFloor);
        spoken = buf;
    }
    else if (!fieldChanged && camOrd >= 1) {
        // v0.20.24 (Aaron): a pure camera-zone change within the same field.
        // Announce the field name + which camera view, so a blind player knows the
        // screen -- and the catalog -- has changed, mirroring a field-to-field move.
        snprintf(buf, sizeof(buf), "%s, camera %d", name, camOrd);
        spoken = buf;
        Log::Mod("FieldAnnounce: camera-zone change -> '%s' (zone idx=%d ordinal=%d) [v0.20.24]",
                 name, curCam, camOrd);
    }

    // Speak without interrupting. Field-name announcement is informative
    // but not critical -- if dialog or another announcement is currently
    // playing, queueing behind it is the polite behavior.
    ScreenReader::Speak(spoken, false);

    Log::Mod("FieldAnnounce: announced fieldId=0x%04X floor=%d name='%s' spoken='%s'",
             (unsigned)curId, curFloor, name, spoken);
}

}  // namespace FieldAnnounce
