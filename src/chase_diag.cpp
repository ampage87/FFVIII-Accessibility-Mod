// chase_diag.cpp - Dollet / X-ATM092 chase scene diagnostic logger
//
// See chase_diag.h for design notes. This module is entirely self-contained:
// it does not hook into other modules' callsites. It reads game state via
// FF8Addresses convenience accessors plus a few absolute addresses confirmed
// in the deep research (var 84 = 0x01CFE9C0, var 530 = 0x01CFEB7E).
//
// All accesses to game memory are SEH-guarded (__try/__except) because the
// addresses point into the FF8 process's address space and may not be valid
// during early boot or during certain mode transitions.

#include "chase_diag.h"
#include "chase_detector.h"
#include "ff8_accessibility.h"
#include "ff8_addresses.h"
#include "field_archive.h"
#include "mod_forward_decls.h"

#include <windows.h>
#include <cstdio>
#include <cstring>
#include <cmath>

namespace ChaseDiag {

// ============================================================================
// Constants
// ============================================================================

// Savemap variable absolute addresses (confirmed by deep research, savemap
// base 0x01CFDC5C + 76-byte header convention; var area starts at base+0xD10).
static const uintptr_t ADDR_VAR_84  = 0x01CFE9C0;  // byte: place ID
static const uintptr_t ADDR_VAR_530 = 0x01CFEB7E;  // byte: Dollet state bitmap

// Field entity layout (mirrors field_navigation.cpp; redeclared here to keep
// the diag self-contained — no inter-module coupling).
static const int      MAX_ENTITIES         = 32;  // v0.18.3.283: mirrors field_navigation.cpp's widening (#85)
static const int      MAX_SYM_NAMES        = 64;
static const uint32_t ENTITY_STRIDE_OTHER  = 0x264;
static const uint32_t ENTITY_STRIDE_BG     = 0x1B4;  // v0.15.1: Backgrounds

// v0.15.1: Kani array kind. v0.15.0 BAT proved kani is always in
// pFieldStateBackgrounds across every chase field, but in case some
// variant moves it to pFieldStateOthers we track the kind dynamically.
enum KaniArrayKind {
    KANI_NONE        = 0,
    KANI_BACKGROUNDS = 1,
    KANI_OTHERS      = 2,
};

// Per-entity offsets (mirrors field_nav_catalog.inl).
static const int OFF_EXEC_FLAGS  = 0x160;
static const int OFF_FP_X        = 0x190;  // int32_t
static const int OFF_FP_Z        = 0x198;  // int32_t
static const int OFF_TRI_ID      = 0x1FA;  // uint16_t
static const int OFF_MODEL_ID    = 0x218;  // int16_t
static const int OFF_PUSH_ONOFF  = 0x249;
static const int OFF_TALK_ONOFF  = 0x24B;
static const int OFF_THRU_ONOFF  = 0x24C;
static const int OFF_SETPC       = 0x255;

// Heartbeat interval (5 seconds at 60 Hz tick driver = 300 ticks; we use
// wall clock instead for robustness against thread sleep variance).
static const DWORD HEARTBEAT_INTERVAL_MS = 5000;

// Kani-state poll cadence: every tick (the AccessibilityThread sleeps 16 ms
// between iterations, so this fires at ~60 Hz). Position deltas under this
// threshold are not logged — only meaningful movement.
static const int32_t KANI_POS_CHANGE_THRESHOLD = 8;  // FF8 walkmesh units

// Var 530 bit decoder. Strings come straight from Qhimm wiki documentation.
static const char* DecodeVar530Bit(uint8_t bit) {
    switch (bit) {
        case 0x02: return "+2 crossed bridge";
        case 0x04: return "+4 elvoret finished";
        case 0x10: return "+0x10 xatm first knock out";
        case 0x20: return "+0x20 selphie waiting near tower";
        case 0x40: return "+0x40 (unenumerated by Qhimm)";
        case 0x80: return "+0x80 (unenumerated by Qhimm)";
        case 0x01: return "+1 (unenumerated by Qhimm)";
        case 0x08: return "+8 (unenumerated by Qhimm)";
        default:   return "?";
    }
}

// Place ID decoder for the Dollet area (CONFIRMED from deep research).
static const char* DecodePlaceId(uint8_t placeId) {
    switch (placeId) {
        case 92:  return "Dollet (general)";
        case 93:  return "Dollet Town Square";
        case 94:  return "Dollet Lapin Beach (chase end)";
        case 99:  return "Dollet Comm Tower (chase pre-start)";
        case 100: return "Dollet Mountain Hideout (chase start)";
        default:  return "?";
    }
}

// ============================================================================
// State
// ============================================================================

static bool s_initialized = false;
static volatile bool s_enabled = false;

// Last-known values for change detection.
static uint8_t  s_lastVar530    = 0xFF;   // sentinel "uninitialized"
static uint8_t  s_lastVar84     = 0xFF;
static uint16_t s_lastFieldId   = 0xFFFF;
static DWORD    s_lastHeartbeat = 0;

// Kani tracking (per field). v0.15.1: now tracks WHICH array (Backgrounds
// or Others) holds kani, not just the slot index.
static int      s_kaniSlot      = -1;     // -1 if not present in current field
static int      s_kaniArrayKind = KANI_NONE;
static int32_t  s_kaniLastX     = 0;
static int32_t  s_kaniLastZ     = 0;
static uint8_t  s_kaniLastPush  = 0xFF;
static uint8_t  s_kaniLastTalk  = 0xFF;
static uint8_t  s_kaniLastThru  = 0xFF;
static uint16_t s_kaniLastTri   = 0xFFFF;

// SYM name buffer for the current field (refreshed on every field transition).
static char     s_symNames[MAX_SYM_NAMES][32];
static int      s_symNameCount = 0;

// ============================================================================
// Safe memory readers
// ============================================================================

static uint8_t SafeReadByte(uintptr_t addr, uint8_t fallback) {
    __try { return *reinterpret_cast<volatile uint8_t*>(addr); }
    __except (EXCEPTION_EXECUTE_HANDLER) { return fallback; }
}

static int32_t SafeReadInt32(uintptr_t addr, int32_t fallback) {
    __try { return *reinterpret_cast<volatile int32_t*>(addr); }
    __except (EXCEPTION_EXECUTE_HANDLER) { return fallback; }
}

static int16_t SafeReadInt16(uintptr_t addr, int16_t fallback) {
    __try { return *reinterpret_cast<volatile int16_t*>(addr); }
    __except (EXCEPTION_EXECUTE_HANDLER) { return fallback; }
}

static uint16_t SafeReadUInt16(uintptr_t addr, uint16_t fallback) {
    __try { return *reinterpret_cast<volatile uint16_t*>(addr); }
    __except (EXCEPTION_EXECUTE_HANDLER) { return fallback; }
}

// ============================================================================
// Field-load handling: full snapshot of field state, kani slot detection
// ============================================================================

// v0.15.1: Find kani in the current field's SYM names AND determine which
// runtime entity array (pFieldStateBackgrounds or pFieldStateOthers) holds
// it. v0.15.0 BAT proved the prior 'others-only' logic was wrong: kani is
// always a Background script entity in every chase field. The correct
// algorithm uses JSMCounts:
//
//   doorsLines  = doors + lines
//   bgStart     = doorsLines
//   othersStart = doorsLines + backgrounds
//
//   if kaniSymIdx >= othersStart  -> Others    slot = kaniSymIdx - othersStart
//   else if kaniSymIdx >= bgStart -> Backgrounds slot = kaniSymIdx - bgStart
//   else                          -> Doors/Lines (unexpected; bail)
//
// Sets s_kaniSlot and s_kaniArrayKind on success; both reset to -1/NONE if
// kani is not present or the lookup fails.
static void FindKaniLocation(const char* fieldName)
{
    s_kaniSlot      = -1;
    s_kaniArrayKind = KANI_NONE;

    if (!fieldName || fieldName[0] == '\0' || s_symNameCount <= 0) return;

    int symIdx = -1;
    for (int i = 0; i < s_symNameCount; i++) {
        if (_stricmp(s_symNames[i], "kani") == 0) {
            symIdx = i;
            break;
        }
    }
    if (symIdx < 0) return;

    FieldArchive::JSMCounts counts = {};
    if (!FieldArchive::LoadJSMCounts(fieldName, counts)) {
        Log::Field("ChaseDiag: field='%s' LoadJSMCounts failed; "
                   "kani symIdx=%d but array unknown",
                   fieldName, symIdx);
        return;
    }

    int doorsLines  = counts.doors + counts.lines;
    int bgStart     = doorsLines;
    int othersStart = doorsLines + counts.backgrounds;

    if (symIdx >= othersStart) {
        s_kaniSlot      = symIdx - othersStart;
        s_kaniArrayKind = KANI_OTHERS;
    } else if (symIdx >= bgStart) {
        s_kaniSlot      = symIdx - bgStart;
        s_kaniArrayKind = KANI_BACKGROUNDS;
    } else {
        Log::Field("ChaseDiag: field='%s' kani symIdx=%d falls in Doors/Lines "
                   "(doors=%d lines=%d) — unexpected, leaving slot unset",
                   fieldName, symIdx, counts.doors, counts.lines);
    }
}

static const char* KaniArrayName(int kind)
{
    switch (kind) {
        case KANI_BACKGROUNDS: return "Backgrounds";
        case KANI_OTHERS:      return "Others";
        default:               return "none";
    }
}

// Dump full SYM names list with the kani slot highlighted if found.
static void LogSymDump()
{
    Log::Field("ChaseDiag: [CHASE-DIAG-FIELD] SYM dump (%d names):",
               s_symNameCount);
    for (int i = 0; i < s_symNameCount; i++) {
        Log::Field("ChaseDiag: [CHASE-DIAG-FIELD]   sym[%d]='%s'%s",
                   i, s_symNames[i],
                   (_stricmp(s_symNames[i], "kani") == 0) ? "  <-- KANI" : "");
    }
    if (s_kaniSlot >= 0) {
        Log::Field("ChaseDiag: [CHASE-DIAG-FIELD] kani is at "
                   "%s slot %d in the runtime entity array",
                   KaniArrayName(s_kaniArrayKind), s_kaniSlot);
    } else {
        Log::Field("ChaseDiag: [CHASE-DIAG-FIELD] kani NOT present "
                   "in this field");
    }
}

// Walk the runtime Others entity array and dump all entity data. (We only
// dump Others because the existing field_navigation/field_nav_catalog.inl
// catalog focuses on Others entities; Backgrounds are script-only and
// don't typically have walkmesh positions worth dumping. Kani is the
// notable exception, but it's already covered by [CHASE-DIAG-KANI].)
static void LogEntityDump()
{
    if (!FF8Addresses::pFieldStateOthers || !FF8Addresses::pFieldStateOtherCount) {
        Log::Field("ChaseDiag: [CHASE-DIAG-FIELD] entity addresses unresolved");
        return;
    }

    __try {
        uint8_t entCount = *FF8Addresses::pFieldStateOtherCount;
        uint8_t* base = *reinterpret_cast<uint8_t**>(FF8Addresses::pFieldStateOthers);
        if (!base || entCount == 0) {
            Log::Field("ChaseDiag: [CHASE-DIAG-FIELD] no entities to dump "
                       "(base=0x%08X count=%d)",
                       (uint32_t)(uintptr_t)base, (int)entCount);
            return;
        }

        uint8_t lim = (entCount < MAX_ENTITIES) ? entCount : MAX_ENTITIES;
        Log::Field("ChaseDiag: [CHASE-DIAG-FIELD] entity array "
                   "(base=0x%08X count=%d):",
                   (uint32_t)(uintptr_t)base, (int)entCount);

        for (int i = 0; i < (int)lim; i++) {
            uint8_t* block = base + ENTITY_STRIDE_OTHER * i;
            int16_t  modelId   = *(int16_t*)(block + OFF_MODEL_ID);
            uint16_t triId     = *(uint16_t*)(block + OFF_TRI_ID);
            uint8_t  setpc     = *(block + OFF_SETPC);
            uint8_t  talk      = *(block + OFF_TALK_ONOFF);
            uint8_t  push      = *(block + OFF_PUSH_ONOFF);
            uint8_t  thru      = *(block + OFF_THRU_ONOFF);
            uint32_t exec      = *(uint32_t*)(block + OFF_EXEC_FLAGS);
            int32_t  fpX       = *(int32_t*)(block + OFF_FP_X);
            int32_t  fpZ       = *(int32_t*)(block + OFF_FP_Z);

            Log::Field("ChaseDiag: [CHASE-DIAG-FIELD]   ent%d "
                       "model=%d tri=0x%04X setpc=%d "
                       "talk=%d push=%d thru=%d "
                       "exec=0x%X fp=(%d,%d)",
                       i, (int)modelId, (unsigned)triId, (int)setpc,
                       (int)talk, (int)push, (int)thru,
                       exec, fpX, fpZ);
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        Log::Field("ChaseDiag: [CHASE-DIAG-FIELD] entity dump SEH");
    }
}

// Called whenever the field ID changes. Captures the full snapshot of the
// new field's state and looks up the kani slot for this field.
static void OnFieldChanged(uint16_t newFieldId)
{
    const char* fieldName =
        FF8Addresses::pCurrentFieldName ? FF8Addresses::pCurrentFieldName : "";
    uint8_t var84  = SafeReadByte(ADDR_VAR_84, 0);
    uint8_t var530 = SafeReadByte(ADDR_VAR_530, 0);

    Log::Field("ChaseDiag: [CHASE-DIAG-FIELD] === FIELD CHANGE ===");
    Log::Field("ChaseDiag: [CHASE-DIAG-FIELD] fieldId=0x%04X name='%s' "
               "place=%u (%s) var530=0x%02X",
               (unsigned)newFieldId, fieldName,
               (unsigned)var84, DecodePlaceId(var84),
               (unsigned)var530);

    // Reload SYM names from the field archive for this field.
    s_symNameCount = 0;
    if (fieldName[0] != '\0') {
        FieldArchive::LoadSYMNames(fieldName, s_symNames,
                                   MAX_SYM_NAMES, s_symNameCount);
    }

    // Find kani in the new field.
    FindKaniLocation(fieldName);
    LogSymDump();

    // Dump the runtime entity array.
    LogEntityDump();

    // Reset kani-tracking state for the new field.
    s_kaniLastX     = 0;
    s_kaniLastZ     = 0;
    s_kaniLastPush  = 0xFF;
    s_kaniLastTalk  = 0xFF;
    s_kaniLastThru  = 0xFF;
    s_kaniLastTri   = 0xFFFF;
}

// ============================================================================
// Per-frame polls: var 530, var 84, kani state
// ============================================================================

static void PollVar530()
{
    uint8_t cur = SafeReadByte(ADDR_VAR_530, s_lastVar530);
    if (cur == s_lastVar530) return;

    uint8_t prev = s_lastVar530;
    s_lastVar530 = cur;

    if (prev == 0xFF) {
        // First poll — establish baseline silently.
        Log::Field("ChaseDiag: [CHASE-DIAG-VAR530] baseline=0x%02X",
                   (unsigned)cur);
        return;
    }

    uint8_t setBits   = cur  & ~prev;
    uint8_t clearBits = prev & ~cur;

    char setStr[256] = {};
    char clrStr[256] = {};
    int  setLen = 0, clrLen = 0;

    for (int i = 0; i < 8; i++) {
        uint8_t mask = (uint8_t)(1u << i);
        if (setBits & mask) {
            int n = snprintf(setStr + setLen, sizeof(setStr) - setLen,
                             "%s%s", (setLen ? ", " : ""),
                             DecodeVar530Bit(mask));
            if (n > 0) setLen += n;
        }
        if (clearBits & mask) {
            int n = snprintf(clrStr + clrLen, sizeof(clrStr) - clrLen,
                             "%s%s", (clrLen ? ", " : ""),
                             DecodeVar530Bit(mask));
            if (n > 0) clrLen += n;
        }
    }

    Log::Field("ChaseDiag: [CHASE-DIAG-VAR530] 0x%02X -> 0x%02X "
               "(set: %s) (clr: %s)",
               (unsigned)prev, (unsigned)cur,
               setStr[0] ? setStr : "(none)",
               clrStr[0] ? clrStr : "(none)");
}

static void PollVar84()
{
    uint8_t cur = SafeReadByte(ADDR_VAR_84, s_lastVar84);
    if (cur == s_lastVar84) return;

    uint8_t prev = s_lastVar84;
    s_lastVar84 = cur;

    if (prev == 0xFF) {
        Log::Field("ChaseDiag: [CHASE-DIAG-PLACE] baseline=%u (%s)",
                   (unsigned)cur, DecodePlaceId(cur));
        return;
    }

    Log::Field("ChaseDiag: [CHASE-DIAG-PLACE] %u -> %u (%s -> %s)",
               (unsigned)prev, (unsigned)cur,
               DecodePlaceId(prev), DecodePlaceId(cur));
}

static void PollKani()
{
    if (s_kaniSlot < 0 || s_kaniArrayKind == KANI_NONE) return;

    // v0.15.1: pick the right runtime array based on which kind FindKaniLocation
    // resolved kani into. v0.15.0 BAT confirmed Backgrounds is the canonical
    // path for every chase field; the Others branch is defensive in case some
    // variant relocates kani.
    uint8_t** arrayPtr = nullptr;
    uint32_t  stride   = 0;
    if (s_kaniArrayKind == KANI_BACKGROUNDS) {
        if (!FF8Addresses::pFieldStateBackgrounds) return;
        arrayPtr = FF8Addresses::pFieldStateBackgrounds;
        stride   = ENTITY_STRIDE_BG;
    } else if (s_kaniArrayKind == KANI_OTHERS) {
        if (!FF8Addresses::pFieldStateOthers) return;
        arrayPtr = FF8Addresses::pFieldStateOthers;
        stride   = ENTITY_STRIDE_OTHER;
    } else {
        return;
    }

    __try {
        uint8_t* base = *arrayPtr;
        if (!base) return;
        uint8_t* block = base + stride * (uint32_t)s_kaniSlot;

        int32_t  fpX  = *(int32_t*)(block + OFF_FP_X);
        int32_t  fpZ  = *(int32_t*)(block + OFF_FP_Z);
        uint16_t tri  = *(uint16_t*)(block + OFF_TRI_ID);
        uint8_t  push = *(block + OFF_PUSH_ONOFF);
        uint8_t  talk = *(block + OFF_TALK_ONOFF);
        uint8_t  thru = *(block + OFF_THRU_ONOFF);

        // Compute position delta (Manhattan distance is fine here — we just
        // want to know if kani moved meaningfully since last log).
        int32_t dx = fpX - s_kaniLastX;
        int32_t dz = fpZ - s_kaniLastZ;
        int32_t deltaMag = (dx < 0 ? -dx : dx) + (dz < 0 ? -dz : dz);

        bool posChanged   = (s_kaniLastTri == 0xFFFF) ||
                            (deltaMag >= KANI_POS_CHANGE_THRESHOLD);
        bool triChanged   = (tri  != s_kaniLastTri);
        bool flagsChanged = (push != s_kaniLastPush) ||
                            (talk != s_kaniLastTalk) ||
                            (thru != s_kaniLastThru);

        if (!posChanged && !triChanged && !flagsChanged) return;

        Log::Field("ChaseDiag: [CHASE-DIAG-KANI] %s slot=%d "
                   "fp=(%d,%d) tri=0x%04X push=%d talk=%d thru=%d "
                   "(dx=%d dz=%d)",
                   KaniArrayName(s_kaniArrayKind), s_kaniSlot,
                   fpX, fpZ, (unsigned)tri,
                   (int)push, (int)talk, (int)thru,
                   dx, dz);

        s_kaniLastX    = fpX;
        s_kaniLastZ    = fpZ;
        s_kaniLastTri  = tri;
        s_kaniLastPush = push;
        s_kaniLastTalk = talk;
        s_kaniLastThru = thru;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        // Silent — kani array may be momentarily invalid during transitions.
    }
}

// ============================================================================
// Heartbeat: full state summary every 5 seconds
// ============================================================================

static void EmitHeartbeat()
{
    DWORD now = GetTickCount();
    if (s_lastHeartbeat != 0 &&
        (now - s_lastHeartbeat) < HEARTBEAT_INTERVAL_MS) return;
    s_lastHeartbeat = now;

    const char* fieldName =
        FF8Addresses::pCurrentFieldName ? FF8Addresses::pCurrentFieldName : "";
    uint16_t fieldId = FF8Addresses::pCurrentFieldId
                       ? *FF8Addresses::pCurrentFieldId : 0xFFFF;
    uint16_t mode    = FF8Addresses::GetCurrentMode();
    uint8_t  v84     = SafeReadByte(ADDR_VAR_84, 0);
    uint8_t  v530    = SafeReadByte(ADDR_VAR_530, 0);

    // Player position — try to read from entity 0 (the player setpc=0 entity
    // is detected dynamically in field_navigation, but for the diag we just
    // grab whichever entity is at slot 0; close enough for heartbeat).
    int32_t playerX = 0, playerZ = 0;
    if (FF8Addresses::pFieldStateOthers) {
        __try {
            uint8_t* base = *reinterpret_cast<uint8_t**>(FF8Addresses::pFieldStateOthers);
            if (base) {
                playerX = *(int32_t*)(base + OFF_FP_X);
                playerZ = *(int32_t*)(base + OFF_FP_Z);
            }
        } __except (EXCEPTION_EXECUTE_HANDLER) {}
    }

    Log::Field("ChaseDiag: [CHASE-DIAG-FRAME] field=0x%04X '%s' "
               "place=%u var530=0x%02X mode=%u "
               "ent0=(%d,%d) kaniSlot=%d",
               (unsigned)fieldId, fieldName,
               (unsigned)v84, (unsigned)v530, (unsigned)mode,
               playerX, playerZ, s_kaniSlot);
}

// ============================================================================
// Public API
// ============================================================================

void Initialize()
{
    if (s_initialized) return;
    s_initialized = true;

    // Reset baselines so the first Update() cycle establishes them rather
    // than burst-logging spurious changes.
    s_enabled       = false;
    s_lastVar530    = 0xFF;
    s_lastVar84     = 0xFF;
    s_lastFieldId   = 0xFFFF;
    s_lastHeartbeat = 0;
    s_kaniSlot      = -1;
    s_kaniArrayKind = KANI_NONE;
    s_symNameCount  = 0;

    Log::Mod("ChaseDiag: Initialized (disabled by default; F12 to toggle).");
}

void Shutdown()
{
    s_enabled = false;
    s_initialized = false;
}

void Toggle()
{
    s_enabled = !s_enabled;

    if (s_enabled) {
        // Snapshot current state so we don't burst-log a bunch of "changes"
        // on the next tick that are really just first-look readings.
        s_lastVar530    = SafeReadByte(ADDR_VAR_530, 0);
        s_lastVar84     = SafeReadByte(ADDR_VAR_84, 0);
        s_lastFieldId   = FF8Addresses::pCurrentFieldId
                          ? *FF8Addresses::pCurrentFieldId : 0xFFFF;
        s_lastHeartbeat = 0;  // force immediate heartbeat on next Update
        s_kaniSlot      = -1;
        s_kaniArrayKind = KANI_NONE;
        s_symNameCount  = 0;

        const char* fieldName = FF8Addresses::pCurrentFieldName
                                ? FF8Addresses::pCurrentFieldName : "";
        Log::Field("ChaseDiag: [CHASE-DIAG] === ENABLED === "
                   "field=0x%04X '%s' place=%u var530=0x%02X",
                   (unsigned)s_lastFieldId, fieldName,
                   (unsigned)s_lastVar84, (unsigned)s_lastVar530);
        Log::Mod("ChaseDiag: Enabled (F12 toggle).");
        ScreenReader::Speak("Chase diagnostic enabled.", true);

        // Also do a one-shot field snapshot for the field we're already in,
        // since OnFieldChanged would normally only fire on a transition.
        if (s_lastFieldId != 0xFFFF) {
            OnFieldChanged(s_lastFieldId);
        }
    } else {
        Log::Field("ChaseDiag: [CHASE-DIAG] === DISABLED ===");
        Log::Mod("ChaseDiag: Disabled (F12 toggle).");
        ScreenReader::Speak("Chase diagnostic disabled.", true);
    }
}

bool IsEnabled() { return s_enabled; }

// ============================================================================
// v0.15.1: ASK opcode snapshot logging
//
// Called from field_dialog's Hook_opcode_ask / Hook_opcode_aask after the
// original handler returns. When chase-diag is enabled, dumps all 8
// pWindowsArray slots in detail so we can capture the engine's actual
// mode1 / state / open_close / question fields for the v0.15.2 proxy-window
// template tuning.
//
// We log only when enabled because the dump is verbose (hex bytes per slot)
// and would otherwise spam the field log on every NPC dialog choice. The
// design intent is: turn chase-diag ON before the chase, talk to any NPC
// who offers a yes/no choice (or trigger any natural ASK), capture the
// snapshot, then enter the chase — chase_ask_overlay can use the captured
// values to populate its proxy slot in v0.15.2.
// ============================================================================

// ff8_win_obj layout (mirrors field_dialog.cpp v04.23).
static const size_t WIN_OBJ_SIZE              = 0x3C;
static const size_t WIN_OBJ_TEXT1_OFFSET      = 0x08;
static const size_t WIN_OBJ_TEXT2_OFFSET      = 0x0C;
static const size_t WIN_OBJ_WINID_OFFSET      = 0x18;
static const size_t WIN_OBJ_MODE1_OFFSET      = 0x1A;
static const size_t WIN_OBJ_OPEN_CLOSE_OFFSET = 0x1C;
static const size_t WIN_OBJ_STATE_OFFSET      = 0x24;
static const size_t WIN_OBJ_FIRST_Q_OFFSET    = 0x29;
static const size_t WIN_OBJ_LAST_Q_OFFSET     = 0x2A;
static const size_t WIN_OBJ_CUR_CHOICE_OFFSET = 0x2B;
static const size_t WIN_OBJ_FIELD30_OFFSET    = 0x30;
static const size_t WIN_OBJ_CALLBACK1_OFFSET  = 0x34;
static const size_t WIN_OBJ_CALLBACK2_OFFSET  = 0x38;
static const int    ASK_DIAG_MAX_WINDOWS      = 8;

void OnAskOpcodeFired(const char* opcodeLabel)
{
    if (!s_initialized || !s_enabled) return;
    if (!FF8Addresses::pWindowsArray) return;
    if (!opcodeLabel) opcodeLabel = "ASK";

    Log::Field("ChaseDiag: [CHASE-DIAG-ASK] === %s opcode fired — "
               "snapshotting all %d pWindowsArray slots ===",
               opcodeLabel, ASK_DIAG_MAX_WINDOWS);

    for (int i = 0; i < ASK_DIAG_MAX_WINDOWS; i++) {
        uint8_t* w = FF8Addresses::pWindowsArray + (i * WIN_OBJ_SIZE);
        __try {
            uint32_t state    = *(uint32_t*)(w + WIN_OBJ_STATE_OFFSET);
            uint16_t mode1    = *(uint16_t*)(w + WIN_OBJ_MODE1_OFFSET);
            int16_t  trans    = *(int16_t*)(w + WIN_OBJ_OPEN_CLOSE_OFFSET);
            uint8_t  winId    = *(uint8_t*)(w + WIN_OBJ_WINID_OFFSET);
            uint8_t  firstQ   = *(uint8_t*)(w + WIN_OBJ_FIRST_Q_OFFSET);
            uint8_t  lastQ    = *(uint8_t*)(w + WIN_OBJ_LAST_Q_OFFSET);
            uint8_t  curQ     = *(uint8_t*)(w + WIN_OBJ_CUR_CHOICE_OFFSET);
            uint16_t field30  = *(uint16_t*)(w + WIN_OBJ_FIELD30_OFFSET);
            uint32_t cb1      = *(uint32_t*)(w + WIN_OBJ_CALLBACK1_OFFSET);
            uint32_t cb2      = *(uint32_t*)(w + WIN_OBJ_CALLBACK2_OFFSET);
            uint32_t text1Ptr = *(uint32_t*)(w + WIN_OBJ_TEXT1_OFFSET);
            uint32_t text2Ptr = *(uint32_t*)(w + WIN_OBJ_TEXT2_OFFSET);

            Log::Field("ChaseDiag: [CHASE-DIAG-ASK]   slot[%d] state=0x%08X "
                       "mode1=0x%04X trans=%d winId=%u "
                       "firstQ=%u lastQ=%u curQ=%u field30=0x%04X "
                       "cb1=0x%08X cb2=0x%08X t1=0x%08X t2=0x%08X",
                       i, state, (unsigned)mode1, (int)trans, (unsigned)winId,
                       (unsigned)firstQ, (unsigned)lastQ, (unsigned)curQ,
                       (unsigned)field30, cb1, cb2, text1Ptr, text2Ptr);

            // Hex-dump the full 0x3C bytes for forensic inspection. Two
            // 16-byte rows + a 12-byte row.
            char row1[80] = {}, row2[80] = {}, row3[80] = {};
            int p1 = 0, p2 = 0, p3 = 0;
            for (int b = 0; b < 16; b++)
                p1 += snprintf(row1 + p1, sizeof(row1) - p1, "%02X ", w[b]);
            for (int b = 16; b < 32; b++)
                p2 += snprintf(row2 + p2, sizeof(row2) - p2, "%02X ", w[b]);
            for (int b = 32; b < 60; b++)
                p3 += snprintf(row3 + p3, sizeof(row3) - p3, "%02X ", w[b]);
            Log::Field("ChaseDiag: [CHASE-DIAG-ASK]     +00: %s", row1);
            Log::Field("ChaseDiag: [CHASE-DIAG-ASK]     +10: %s", row2);
            Log::Field("ChaseDiag: [CHASE-DIAG-ASK]     +20: %s", row3);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            Log::Field("ChaseDiag: [CHASE-DIAG-ASK]   slot[%d] SEH while reading", i);
        }
    }
}

void Update()
{
    if (!s_initialized || !s_enabled) return;

    // Field-change detection: read current_field_id and compare.
    uint16_t curFieldId = FF8Addresses::pCurrentFieldId
                          ? *FF8Addresses::pCurrentFieldId : 0xFFFF;
    if (curFieldId != s_lastFieldId && curFieldId != 0xFFFF) {
        s_lastFieldId = curFieldId;
        OnFieldChanged(curFieldId);
    }

    // Per-frame polls.
    PollVar530();
    PollVar84();
    PollKani();

    // Heartbeat (rate-limited).
    EmitHeartbeat();
}

}  // namespace ChaseDiag
