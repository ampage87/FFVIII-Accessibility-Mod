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

    // v0.18.3.301 (#90): the floor is part of the identity, not just the
    // fieldId. In the prison shaft the fieldId does NOT change between floors,
    // so watching it alone leaves every floor change silent.
    int curFloor = ReadPrisonFloor(curId);

    if (curId != s_observedFieldId || curFloor != s_observedFloor) {
        // FieldId or floor just changed. Start (or restart) the debounce timer.
        s_observedFieldId = curId;
        s_observedFloor   = curFloor;
        s_changeTick      = GetTickCount();
        s_pendingAnnounce = true;
        return;
    }

    if (!s_pendingAnnounce) return;

    if ((GetTickCount() - s_changeTick) < DEBOUNCE_MS) return;

    // Debounce elapsed and fieldId is stable. Decide whether to speak.
    s_pendingAnnounce = false;

    if (curId == s_lastAnnouncedFieldId && curFloor == s_lastAnnouncedFloor) {
        // Already announced this field AND floor (e.g. mode flipped to battle
        // and back without a real change). Don't repeat.
        return;
    }

    const char* name = FIELD_DISPLAY_NAMES[curId];
    if (!name || !*name) return;

    s_lastAnnouncedFieldId = curId;
    s_lastAnnouncedFloor   = curFloor;

    // v0.18.3.301 (#90): compose the prison shaft announcement. The ring
    // halves lose their archive ordinal entirely -- "Floor 4, left side" is
    // the pair of facts a player can actually navigate with, where "Galbadia
    // D-District Prison 3" is neither a floor nor a place. Everything else
    // keeps its name and gains the floor.
    char buf[128];
    const char* spoken = name;
    if (curFloor > 0) {
        const char* side = nullptr;
        if (curId == 0x031A || curId == 0x031B)      side = "left side";
        else if (curId == 0x031C || curId == 0x031D) side = "right side";
        if (side) snprintf(buf, sizeof(buf), "Floor %d, %s", curFloor, side);
        else      snprintf(buf, sizeof(buf), "%s, Floor %d", name, curFloor);
        spoken = buf;
    }

    // Speak without interrupting. Field-name announcement is informative
    // but not critical -- if dialog or another announcement is currently
    // playing, queueing behind it is the polite behavior.
    ScreenReader::Speak(spoken, false);

    Log::Mod("FieldAnnounce: announced fieldId=0x%04X floor=%d name='%s' spoken='%s'",
             (unsigned)curId, curFloor, name, spoken);
}

}  // namespace FieldAnnounce
