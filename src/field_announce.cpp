// field_announce.cpp - Automatic field-name announcement on field load.
// See field_announce.h for design notes.

#include "field_announce.h"
#include "field_display_names.h"
#include "ff8_accessibility.h"
#include "ff8_addresses.h"
#include "mod_forward_decls.h"

#include <windows.h>
#include <cstdint>

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

// Last fieldId we announced. 0xFFFF = nothing announced yet.
static uint16_t s_lastAnnouncedFieldId = 0xFFFF;

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

    if (curId != s_observedFieldId) {
        // FieldId just changed. Start (or restart) the debounce timer.
        s_observedFieldId = curId;
        s_changeTick      = GetTickCount();
        s_pendingAnnounce = true;
        return;
    }

    if (!s_pendingAnnounce) return;

    if ((GetTickCount() - s_changeTick) < DEBOUNCE_MS) return;

    // Debounce elapsed and fieldId is stable. Decide whether to speak.
    s_pendingAnnounce = false;

    if (curId == s_lastAnnouncedFieldId) {
        // Already announced this field (e.g. mode flipped to battle and
        // back without a real field change). Don't repeat.
        return;
    }

    const char* name = FIELD_DISPLAY_NAMES[curId];
    if (!name || !*name) return;

    s_lastAnnouncedFieldId = curId;

    // Speak without interrupting. Field-name announcement is informative
    // but not critical -- if dialog or another announcement is currently
    // playing, queueing behind it is the polite behavior.
    ScreenReader::Speak(name, false);

    Log::Mod("FieldAnnounce: announced fieldId=0x%04X name='%s'",
             (unsigned)curId, name);
}

}  // namespace FieldAnnounce
