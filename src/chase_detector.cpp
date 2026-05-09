// chase_detector.cpp — Dollet / X-ATM092 chase scene state authority.
// See chase_detector.h for the public-facing design notes.
//
// v0.15.1: New module. v0.15.0 BAT data drives every key decision here:
//
//   Finding 1 (kani is a Background entity, not Other): we use the JSM
//     header's per-category counts plus the SYM name list to compute
//     kani's slot in pFieldStateBackgrounds correctly. The fallback to
//     pFieldStateOthers stays in case some chase variant moves kani.
//
//   Finding 3 (pCurrentFieldName lags pCurrentFieldId by 2-5 seconds):
//     we debounce field transitions. Every fieldId change starts a
//     2-second timer; while the timer is running, GetDebouncedFieldName()
//     returns the previous name (or "" if first transition of the
//     session). When the timer expires, we re-read pCurrentFieldName,
//     trust it, refresh kani-slot, and reset the per-field battle count.
//
// All accesses to game memory are SEH-guarded.

#include "chase_detector.h"
#include "ff8_accessibility.h"
#include "ff8_addresses.h"
#include "field_archive.h"
#include "mod_forward_decls.h"

#include <windows.h>
#include <cstdio>
#include <cstring>

namespace ChaseDetector {

// ============================================================================
// Constants
// ============================================================================

// Hard-coded chase field set, confirmed by v0.15.0 BAT. Match by name
// because field IDs are not stable across "session variants" — the same
// field name can appear under a different ID per playthrough or even per
// reload. domt3_2 added defensively in case it shows up in some variant
// of the mountain trail; if v0.15.1 BAT confirms it never appears, we
// can drop it in v0.15.2 without behavior change for any existing case.
//
// v0.15.2.10: domt1_1 added. v0.15.2.9 BAT log showed two battles in
// 'domt1_1' at 20:52:15 and 20:53:33 with chaseActive=0, both during
// the chase return run from the Comm Tower. Per Aaron's clarification
// ("no random encounters during the chase scene"), those were chase
// robot fights that escaped the kani pin entirely because the field
// wasn't in this set. domt1_1 is the entry/exit mountain field —
// traversed twice (going up = pre-chase random encounters; going down
// = chase). Including it activates the freeze on the second pass.
//
// v0.15.2.11: dotown_3, dotown_2, dotown_1 REMOVED. v0.15.2.10 BAT
// crashed/hung ~16 seconds after entering dotown_3 from doopen2a.
// Aaron's diagnosis: dotown_3's chase-end cutscene plays an animation
// where X-ATM092 (kani) walks across the town square and shorts out,
// driven by the dotown_3 kani entity in Backgrounds slot 1. With
// dotown_3 in CHASE_FIELD_NAMES, our chase_kani_freeze module kept
// tracking dotown_3's kani address and — if a mode 4→1 transition
// fired during the cutscene — would StartCapture and pin the
// dotown_3 kani's anim ID bytes (+0x150/+0x154/+0x1FA/+0x23F/+0x241),
// directly fighting the cutscene's animation script every frame.
// dotown_3 is the chase END field — there are no kani battles there;
// the chase is over. Same logic applies to dotown_2 and dotown_1
// (post-chase town fields). Removing all three from the chase set
// means: (1) ChaseDetector reports chase DEACTIVATED on entering
// dotown_3, (2) chase_kani_freeze.StartCapture won't fire there
// (IsInChaseField()=false), (3) any leftover pin state already
// deactivated cleanly via the field-change branch, (4) the dotown_3
// cutscene plays unimpeded.
static const char* CHASE_FIELD_NAMES[] = {
    "domt1_1",   // Mountain trail, entry/exit; second-pass = chase return
    "domt2_1",   // Mountain trail
    "domt3_2",   // Mountain trail (defensive include)
    "domt4_1",   // Mountain trail, post-tower exit; chase begins here
    "domt5_1",   // Mountain trail
    "doopen2a",  // Bridge field; chase ends on transition to dotown_3
    // v0.15.2.11: dotown_3/2/1 removed (chase-end cutscene fields).
};
static const int CHASE_FIELD_COUNT =
    (int)(sizeof(CHASE_FIELD_NAMES) / sizeof(CHASE_FIELD_NAMES[0]));

// Per finding 3: the field-name buffer lags fieldId. 2 seconds covers
// the 2-5 second observed range with comfortable margin. While the
// timer is running we treat the field as "in transition"; consumers
// see IsInChaseField() == false and GetCurrentFieldName() == "".
static const DWORD FIELD_NAME_DEBOUNCE_MS = 2000;

// SYM name buffer sizing — mirrors chase_diag.
static const int  MAX_SYM_NAMES = 64;

// Live entity array strides (from FFNx ff8.h).
static const int  STRIDE_BACKGROUND = 0x1B4;
static const int  STRIDE_OTHER      = 0x264;

// Game mode for battle (from ff8_addresses.h's GameMode enum).
// v0.15.2.1: Was 999 — wrong! v0.15.2 BAT showed mode=3 dominant
// during the chase battle (95 seconds straight, ent0 position frozen
// = Squall in battle screen). Battle mode in this Steam 2013 build
// is actually 3 at the field-mode polling resolution. Our edge
// detection 'prev != 999 && cur == 999' never fired, so the
// per-field battle counter stayed at 0 across kani contacts, which
// kept ChaseBattleFreeze passing through every kani battle
// (including subsequent ones we wanted to NO-OP). Result: robot
// kept getting up after each battle. Setting MODE_BATTLE_VAL = 3
// makes the edge detection fire on the first kani battle, and the
// freeze gate's count>=1 condition triggers on the second.
static const uint16_t MODE_BATTLE_VAL = 3;

// INI persistence — same file as the existing config.cpp Accessibility
// section, but a separate [Chase] section so the chase-specific keys
// don't show up in the commented Accessibility template.
static const char* INI_SECTION_CHASE   = "Chase";
static const char* INI_KEY_CHASE_MODE  = "chase_mode";

// ============================================================================
// State
// ============================================================================

static bool       s_initialized = false;

// Field tracking (debounced).
static uint16_t   s_lastFieldId         = 0xFFFF;
static DWORD      s_fieldChangeTick     = 0;     // GetTickCount() at last id change
static bool       s_debounceActive      = false; // true while waiting for name to settle
static char       s_debouncedFieldName[64] = "";

// Chase-active span (true between first chase-field entry and exit).
static bool       s_chaseActive = false;

// Per-field state.
static int        s_currentFieldBattleCount = 0;
static uint16_t   s_lastGameMode = 0xFFFF;       // for MODE_BATTLE edge detect

// Kani slot cache (per current field).
static KaniLocation s_kaniLoc        = { -1, -1, 0, "" };
// v0.15.2.8: parallel cache for battleyarou.
static KaniLocation s_battleyarouLoc = { -1, -1, 0, "" };

// SYM name buffer for the current field.
static char       s_symNames[MAX_SYM_NAMES][32];
static int        s_symNameCount = 0;

// Chase mode (mirrored to INI on change).
static Mode       s_chaseMode = MODE_MANUAL;

// ============================================================================
// Helpers
// ============================================================================

static bool IsChaseFieldName(const char* name) {
    if (!name || name[0] == '\0') return false;
    for (int i = 0; i < CHASE_FIELD_COUNT; i++) {
        if (_stricmp(name, CHASE_FIELD_NAMES[i]) == 0) return true;
    }
    return false;
}

// v0.15.2.8: Generic resolver. Searches s_symNames for the given target
// (case-insensitive), then uses FieldArchive::LoadJSMCounts to compute the
// runtime slot in either pFieldStateBackgrounds or pFieldStateOthers via:
//
//   doorsLines  = counts.doors + counts.lines
//   bgStart     = doorsLines
//   othersStart = doorsLines + counts.backgrounds
//
//   if symIdx >= othersStart  -> Others    slot = symIdx - othersStart
//   else if symIdx >= bgStart -> Backgrounds slot = symIdx - bgStart
//   else                      -> Doors/Lines (unexpected; bail)
//
// Logs each resolution outcome under a short tag for traceability.
// outLoc is fully reset before resolution; on failure outLoc.arraySlot
// remains -1.
static void ResolveEntityLocation(const char*    targetSym,
                                  const char*    fieldName,
                                  const char*    logTag,
                                  KaniLocation&  outLoc)
{
    outLoc.symIdx = -1;
    outLoc.arraySlot = -1;
    outLoc.arrayKind = 0;
    outLoc.symName[0] = '\0';

    if (!fieldName || fieldName[0] == '\0') return;
    if (s_symNameCount <= 0) return;

    // SYM-name match (case-insensitive — v0.15.0 BAT showed both
    // 'kani' and 'Kani' across different chase fields, and we expect the
    // same casing freedom for battleyarou).
    int symIdx = -1;
    for (int i = 0; i < s_symNameCount; i++) {
        if (_stricmp(s_symNames[i], targetSym) == 0) {
            symIdx = i;
            strncpy(outLoc.symName, s_symNames[i], sizeof(outLoc.symName) - 1);
            outLoc.symName[sizeof(outLoc.symName) - 1] = '\0';
            break;
        }
    }
    if (symIdx < 0) {
        Log::Field("ChaseDetector: field='%s' no %s in SYM (count=%d)",
                   fieldName, logTag, s_symNameCount);
        return;
    }
    outLoc.symIdx = symIdx;

    FieldArchive::JSMCounts counts = {};
    if (!FieldArchive::LoadJSMCounts(fieldName, counts)) {
        Log::Field("ChaseDetector: field='%s' LoadJSMCounts failed; "
                   "%s symIdx=%d but array unknown",
                   fieldName, logTag, symIdx);
        return;
    }

    int doorsLines  = counts.doors + counts.lines;
    int bgStart     = doorsLines;
    int othersStart = doorsLines + counts.backgrounds;

    if (symIdx >= othersStart) {
        outLoc.arraySlot = symIdx - othersStart;
        outLoc.arrayKind = 2;  // Others
        Log::Field("ChaseDetector: field='%s' %s symIdx=%d -> "
                   "Others slot %d (doors=%d lines=%d bgs=%d others=%d)",
                   fieldName, logTag, symIdx, outLoc.arraySlot,
                   counts.doors, counts.lines, counts.backgrounds, counts.others);
    } else if (symIdx >= bgStart) {
        outLoc.arraySlot = symIdx - bgStart;
        outLoc.arrayKind = 1;  // Backgrounds
        Log::Field("ChaseDetector: field='%s' %s symIdx=%d -> "
                   "Backgrounds slot %d (doors=%d lines=%d bgs=%d others=%d)",
                   fieldName, logTag, symIdx, outLoc.arraySlot,
                   counts.doors, counts.lines, counts.backgrounds, counts.others);
    } else {
        // SYM index is in the doors/lines region — unexpected.
        Log::Field("ChaseDetector: field='%s' %s symIdx=%d in Doors/Lines "
                   "(doors=%d lines=%d) — unexpected, leaving slot unset",
                   fieldName, logTag, symIdx, counts.doors, counts.lines);
    }
}

// Resolve kani's location in the new field. Thin wrapper around
// ResolveEntityLocation kept for source-level continuity (chase_diag and
// other callers reference the kani-specific path conceptually).
static void ResolveKaniLocation(const char* fieldName)
{
    ResolveEntityLocation("kani", fieldName, "kani", s_kaniLoc);
}

// v0.15.2.8: Resolve battleyarou's location in the new field. Same
// SYM/JSMCounts logic as kani. Resolves to runtime slot in Others or
// Backgrounds depending on JSM layout.
static void ResolveBattleyarouLocation(const char* fieldName)
{
    ResolveEntityLocation("battleyarou", fieldName, "battleyarou", s_battleyarouLoc);
}

// Called whenever the debounced field name settles to a new value. We
// reset per-field state and refresh the kani cache.
static void OnDebouncedFieldChange(const char* newFieldName)
{
    s_currentFieldBattleCount = 0;

    // Reload SYM names from the field archive.
    s_symNameCount = 0;
    if (newFieldName[0] != '\0') {
        FieldArchive::LoadSYMNames(newFieldName, s_symNames,
                                   MAX_SYM_NAMES, s_symNameCount);
    }

    ResolveKaniLocation(newFieldName);
    ResolveBattleyarouLocation(newFieldName);  // v0.15.2.8

    // Chase-active span tracking. We enter "active" on first chase-field
    // entry; we exit "active" when we land on a non-chase field.
    bool nowChase = IsChaseFieldName(newFieldName);
    if (nowChase && !s_chaseActive) {
        s_chaseActive = true;
        Log::Field("ChaseDetector: chase ACTIVATED on entry to '%s'", newFieldName);
    } else if (!nowChase && s_chaseActive) {
        s_chaseActive = false;
        Log::Field("ChaseDetector: chase DEACTIVATED on entry to '%s' "
                   "(non-chase field)", newFieldName);
    }
}

// ============================================================================
// Update path
// ============================================================================

static void PollFieldChange()
{
    uint16_t curId = FF8Addresses::pCurrentFieldId
                     ? *FF8Addresses::pCurrentFieldId : 0xFFFF;
    if (curId == 0xFFFF) return;

    if (curId != s_lastFieldId) {
        // Field id just changed. Start the debounce timer; don't trust
        // the name yet.
        s_lastFieldId      = curId;
        s_fieldChangeTick  = GetTickCount();
        s_debounceActive   = true;
        Log::Field("ChaseDetector: fieldId changed to 0x%04X — "
                   "starting %lu ms name-debounce", (unsigned)curId,
                   (unsigned long)FIELD_NAME_DEBOUNCE_MS);
        return;
    }

    if (s_debounceActive) {
        DWORD now = GetTickCount();
        if ((now - s_fieldChangeTick) >= FIELD_NAME_DEBOUNCE_MS) {
            // Timer expired — read the name and accept it.
            s_debounceActive = false;
            const char* nameNow = FF8Addresses::pCurrentFieldName
                                  ? FF8Addresses::pCurrentFieldName : "";
            // Defensive copy in case the buffer rotates under us.
            __try {
                strncpy(s_debouncedFieldName, nameNow,
                        sizeof(s_debouncedFieldName) - 1);
                s_debouncedFieldName[sizeof(s_debouncedFieldName) - 1] = '\0';
            } __except (EXCEPTION_EXECUTE_HANDLER) {
                s_debouncedFieldName[0] = '\0';
            }
            Log::Field("ChaseDetector: name debounce settled: id=0x%04X "
                       "name='%s' (chaseField=%d)",
                       (unsigned)s_lastFieldId, s_debouncedFieldName,
                       (int)IsChaseFieldName(s_debouncedFieldName));
            OnDebouncedFieldChange(s_debouncedFieldName);
        }
    }
}

static void PollGameMode()
{
    if (!FF8Addresses::pGameMode) return;
    uint16_t cur = 0xFFFF;
    __try { cur = *FF8Addresses::pGameMode; } __except (EXCEPTION_EXECUTE_HANDLER) { return; }

    // Edge: prev != BATTLE, cur == BATTLE — count as a battle entry.
    if (cur == MODE_BATTLE_VAL && s_lastGameMode != MODE_BATTLE_VAL) {
        s_currentFieldBattleCount++;
        Log::Field("ChaseDetector: battle entered (game-mode 0x%04X -> 0x%04X); "
                   "field='%s' chaseActive=%d count=%d",
                   (unsigned)s_lastGameMode, (unsigned)cur,
                   s_debouncedFieldName, (int)s_chaseActive,
                   s_currentFieldBattleCount);
    }
    s_lastGameMode = cur;
}

// ============================================================================
// INI persistence
// ============================================================================

static const char* GetIniPath()
{
    // Re-derive next to the DLL. Rather than threading config.cpp's path
    // through to here, we use the same convention: %DLL_DIR%/ff8_accessibility.ini.
    static char s_path[MAX_PATH] = {};
    if (s_path[0] != '\0') return s_path;

    char dllPath[MAX_PATH] = {};
    HMODULE hMod = NULL;
    GetModuleHandleExA(
        GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
        (LPCSTR)&GetIniPath, &hMod);
    GetModuleFileNameA(hMod, dllPath, sizeof(dllPath));
    char* lastSlash = strrchr(dllPath, '\\');
    if (lastSlash) *(lastSlash + 1) = '\0';
    else dllPath[0] = '\0';
    snprintf(s_path, sizeof(s_path), "%sff8_accessibility.ini", dllPath);
    return s_path;
}

static void LoadChaseModeFromIni()
{
    char buf[16] = {};
    GetPrivateProfileStringA(INI_SECTION_CHASE, INI_KEY_CHASE_MODE,
                              "manual", buf, sizeof(buf), GetIniPath());
    if (_stricmp(buf, "auto") == 0) {
        s_chaseMode = MODE_AUTO;
    } else {
        // Default for any unrecognized value — manual.
        s_chaseMode = MODE_MANUAL;
    }
    Log::Mod("ChaseDetector: loaded chase_mode='%s' from INI", ChaseModeName(s_chaseMode));
}

// ============================================================================
// Public API
// ============================================================================

void Initialize()
{
    if (s_initialized) return;
    s_initialized = true;

    s_lastFieldId             = 0xFFFF;
    s_fieldChangeTick         = 0;
    s_debounceActive          = false;
    s_debouncedFieldName[0]   = '\0';
    s_chaseActive             = false;
    s_currentFieldBattleCount = 0;
    s_lastGameMode            = 0xFFFF;
    s_kaniLoc                 = KaniLocation{ -1, -1, 0, "" };
    s_battleyarouLoc          = KaniLocation{ -1, -1, 0, "" };  // v0.15.2.8
    s_symNameCount            = 0;

    LoadChaseModeFromIni();

    Log::Mod("ChaseDetector: Initialized (chase_mode=%s, %d chase fields tracked).",
             ChaseModeName(s_chaseMode), CHASE_FIELD_COUNT);
}

void Shutdown()
{
    s_initialized = false;
}

void Update()
{
    if (!s_initialized) return;
    PollFieldChange();
    PollGameMode();
}

bool IsInChaseField()
{
    if (!s_initialized) return false;
    if (s_debounceActive) return false;          // still settling
    return IsChaseFieldName(s_debouncedFieldName);
}

const char* GetDebouncedFieldName()
{
    if (!s_initialized || s_debounceActive) return "";
    return s_debouncedFieldName;
}

bool IsChaseActive()
{
    return s_initialized && s_chaseActive;
}

int GetCurrentFieldBattleCount()
{
    return s_currentFieldBattleCount;
}

uintptr_t GetKaniEntityPtr()
{
    if (!s_initialized) return 0;
    if (s_kaniLoc.arraySlot < 0) return 0;
    if (s_kaniLoc.arrayKind == 0) return 0;

    uint8_t* base = nullptr;
    int stride = 0;
    if (s_kaniLoc.arrayKind == 1) {
        // Backgrounds.
        if (!FF8Addresses::pFieldStateBackgrounds) return 0;
        __try {
            base = *reinterpret_cast<uint8_t**>(FF8Addresses::pFieldStateBackgrounds);
        } __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
        stride = STRIDE_BACKGROUND;
    } else if (s_kaniLoc.arrayKind == 2) {
        // Others.
        if (!FF8Addresses::pFieldStateOthers) return 0;
        __try {
            base = *reinterpret_cast<uint8_t**>(FF8Addresses::pFieldStateOthers);
        } __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
        stride = STRIDE_OTHER;
    }
    if (!base) return 0;
    return (uintptr_t)(base + (size_t)stride * (size_t)s_kaniLoc.arraySlot);
}

bool IsKaniEntityPtr(uintptr_t entityPtr)
{
    if (!entityPtr) return false;
    uintptr_t kaniPtr = GetKaniEntityPtr();
    if (!kaniPtr) return false;
    return entityPtr == kaniPtr;
}

// v0.15.2.8: shared resolver used by both kani and battleyarou getters.
static uintptr_t ResolveLocPtr(const KaniLocation& loc)
{
    if (!s_initialized) return 0;
    if (loc.arraySlot < 0) return 0;
    if (loc.arrayKind == 0) return 0;

    uint8_t* base = nullptr;
    int stride = 0;
    if (loc.arrayKind == 1) {
        if (!FF8Addresses::pFieldStateBackgrounds) return 0;
        __try {
            base = *reinterpret_cast<uint8_t**>(FF8Addresses::pFieldStateBackgrounds);
        } __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
        stride = STRIDE_BACKGROUND;
    } else if (loc.arrayKind == 2) {
        if (!FF8Addresses::pFieldStateOthers) return 0;
        __try {
            base = *reinterpret_cast<uint8_t**>(FF8Addresses::pFieldStateOthers);
        } __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
        stride = STRIDE_OTHER;
    }
    if (!base) return 0;
    return (uintptr_t)(base + (size_t)stride * (size_t)loc.arraySlot);
}

uintptr_t GetBattleyarouEntityPtr()
{
    return ResolveLocPtr(s_battleyarouLoc);
}

bool IsBattleyarouEntityPtr(uintptr_t entityPtr)
{
    if (!entityPtr) return false;
    uintptr_t p = GetBattleyarouEntityPtr();
    if (!p) return false;
    return entityPtr == p;
}

Mode GetChaseMode()
{
    return s_chaseMode;
}

void SetChaseMode(Mode m)
{
    s_chaseMode = m;
    WritePrivateProfileStringA(INI_SECTION_CHASE, INI_KEY_CHASE_MODE,
                                ChaseModeName(m), GetIniPath());
    Log::Mod("ChaseDetector: chase_mode set to '%s' (persisted)", ChaseModeName(m));
}

const char* ChaseModeName(Mode m)
{
    return (m == MODE_AUTO) ? "auto" : "manual";
}

KaniLocation GetKaniLocation()
{
    return s_kaniLoc;
}

KaniLocation GetBattleyarouLocation()
{
    return s_battleyarouLoc;
}

// v0.15.2.9: SYM-name accessors. Read from the field-cached buffer.
const char* GetSymName(int idx)
{
    if (!s_initialized) return "?";
    if (idx < 0 || idx >= s_symNameCount) return "?";
    return s_symNames[idx];
}

int GetSymNameCount()
{
    return s_symNameCount;
}

}  // namespace ChaseDetector
