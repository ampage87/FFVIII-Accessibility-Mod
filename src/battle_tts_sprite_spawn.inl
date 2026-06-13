// battle_tts_sprite_spawn.inl — Real sprite system monitoring (v0.13.93 architectural pivot)
//
// v0.13.93: ARCHITECTURAL PIVOT from popup table to real sprite system
//
// SESSIONS 80-92 SUMMARY: 
// -----------------------
// Sessions 80-92 extensively investigated 0x01D280C4 popup table via sub_48E830 hooks.
// CONCLUSIVE FINDING: This table handles ACTION ANNOUNCEMENTS only ("Cast Fire", "Draw Sleep") 
// NOT floating damage numbers ("79", "149", "Miss"). Session 83 definitively identified 
// the real sprite system but it was never implemented.
//
// SESSION 83 KEY DISCOVERIES:
// ---------------------------
// 1. **sub_482C90 is the sprite slot allocator** managing 16 slots × 16 bytes at:
//    - 0x01D28C04 (metadata) + 0x01D28C44 (data)
//    - Each slot's first dword is a polymorphic update/render callback
//
// 2. **5 distinct callbacks identified:**
//    - 0x47E030: general task spawner
//    - 0x48ACD0: flag manager
//    - 0x48AC60: one-shot text render
//    - 0x48AC90: timed text render  
//    - 0x48E620: **complex per-frame update** ← PRIMARY DAMAGE CANDIDATE
//
// 3. **Popup table failure mode:** Life stayed at 0xFF for 8+ seconds, no meaningful
//    state transitions. This is because it tracks action announcements, not damage sprites.
//
// V0.13.93 NEW APPROACH:
// ----------------------
// 1. Hook sub_482C90 sprite allocator to detect sprite slot assignments
// 2. Monitor the real sprite pool at 0x01D28C04/0x01D28C44  
// 3. Track callback assignments, especially 0x48E620 for damage detection
// 4. Detect actual floating damage sprite lifecycle vs action announcements
// 5. Provide true damage sprite visibility detection for TTS timing
//
// This addresses the core issue: popup life never changed, timing remained inconsistent.
// The real sprite system should provide the sprite visibility signals we need.

// ============================================================================
// Real sprite system addresses (Session 83 discoveries)
// ============================================================================

static const uintptr_t SPRITE_POOL_METADATA = 0x01D28C04;  // 16 slots × 16 bytes metadata
static const uintptr_t SPRITE_POOL_DATA     = 0x01D28C44;  // 16 slots × 16 bytes data
static const size_t    SPRITE_SLOT_SIZE     = 16;
static const int       SPRITE_POOL_MAX      = 16;

// Session 83 callback addresses
static const uint32_t CALLBACK_GENERAL_TASK   = 0x47E030;  // general task spawner
static const uint32_t CALLBACK_FLAG_MANAGER   = 0x48ACD0;  // flag manager  
static const uint32_t CALLBACK_TEXT_ONESHOT   = 0x48AC60;  // one-shot text render
static const uint32_t CALLBACK_TEXT_TIMED     = 0x48AC90;  // timed text render
static const uint32_t CALLBACK_COMPLEX_UPDATE = 0x48E620;  // complex per-frame update ← DAMAGE CANDIDATE

// ============================================================================
// Hook signature & state: sub_482C90 sprite allocator (NEW v0.13.93)
// ============================================================================

typedef uint32_t (__cdecl *Sub482C90Func_t)(uint32_t, uint32_t, uint32_t, uint32_t);
static Sub482C90Func_t s_origSub482C90 = nullptr;
static bool s_sub482C90HookInstalled = false;
static const uint32_t SUB_482C90_ADDR = 0x00482C90;

// v0.13.99: uncapped per-call log. The previous v0.13.93 first-N=20 +
// dedup'd-after pattern only emitted ~1 line per battle (the SEQ #1 entry
// at battle exit) which gave us no information about whether sub_482C90
// fires for damage sprites at all. v0.13.99 logs every call timestamped
// with full 16-byte slot data so we can post-hoc cross-reference call
// timestamps against the ROI shadow scanner's yellow-spike timestamps.
// If a COMPLEX_UPDATE callback (0x48E620) allocation lands within ~3
// frames of a yU>6 spike, that's our hook. If they correlate at popup-spawn
// time (~3s before yU spike) but not at the spike itself, the digit-render
// is happening inside sub_48E620's body and we hook the callback directly
// in v0.14.x. If neither correlation appears, invariant #18 is reconfirmed.
//
// Per-call log volume: a typical battle has many sprite allocations
// (hit splashes, status icons, action announces, damage). The log filters
// on mode==3 (battle only), and a 30-second damage-event-rich battle
// produces O(50-200) [SPRITE-ALLOC-V99] lines — still searchable by grep.
// No dedup or cap; we want the full trace for cross-correlation.
static int s_spriteAllocCallCountThisBattle = 0;

// ============================================================================
// Real sprite slot structure (Session 83 discoveries)
// ============================================================================

// Session 83: sprite pool structure at 0x01D28C04/0x01D28C44
// Each slot: 16 bytes, first dword is polymorphic callback pointer
struct RealSpriteSlot {
    uint32_t callback;      // polymorphic update/render function pointer
    uint32_t data[3];       // remaining 12 bytes of slot data
};

// ============================================================================
// Hook: sub_482C90 — real sprite slot allocator (NEW v0.13.93)
// ============================================================================

static uint32_t __cdecl HookedSub482C90(uint32_t arg0, uint32_t arg1,
                                         uint32_t arg2, uint32_t arg3)
{
    uintptr_t callerRA = (uintptr_t)_ReturnAddress();

    // Call original sprite allocator first.
    uint32_t result = s_origSub482C90(arg0, arg1, arg2, arg3);

    __try {
        uint16_t mode = 0;
        if (FF8Addresses::pGameMode) {
            __try { mode = *FF8Addresses::pGameMode; } __except(EXCEPTION_EXECUTE_HANDLER) {}
        }
        if (mode != 3) return result;  // Only monitor during battle

        DWORD nowMs = GetTickCount();
        uint8_t activeChar = 0xFF;
        __try { activeChar = *(uint8_t*)0x01D76844; } __except(EXCEPTION_EXECUTE_HANDLER) {}

        // Identify the most-recently-allocated slot. Heuristic: take the
        // highest slot index with a non-zero callback. The engine fills
        // slots in ascending order until full, then reuses the lowest free,
        // so the highest non-zero callback is almost always the one that
        // just got assigned. (For the rare reuse case we don't get the right
        // slot, but we still log the event timestamp, which is the primary
        // signal we need for cross-correlation.)
        uint32_t allocatedCallback = 0;
        int      allocatedSlot     = -1;
        uint32_t allocatedData[3]  = {};
        for (int slot = 0; slot < SPRITE_POOL_MAX; slot++) {
            __try {
                uint32_t* metaSlot = (uint32_t*)(SPRITE_POOL_METADATA + slot * SPRITE_SLOT_SIZE);
                uint32_t  cb       = metaSlot[0];
                if (cb != 0) {
                    allocatedCallback = cb;
                    allocatedSlot     = slot;
                    uint32_t* dataSlot = (uint32_t*)(SPRITE_POOL_DATA + slot * SPRITE_SLOT_SIZE);
                    allocatedData[0] = dataSlot[0];
                    allocatedData[1] = dataSlot[1];
                    allocatedData[2] = dataSlot[2];
                }
            } __except(EXCEPTION_EXECUTE_HANDLER) {}
        }

        const char* callbackName = "UNKNOWN";
        bool isDamageCandidate = false;
        if      (allocatedCallback == CALLBACK_GENERAL_TASK)   { callbackName = "GENERAL_TASK"; }
        else if (allocatedCallback == CALLBACK_FLAG_MANAGER)   { callbackName = "FLAG_MANAGER"; }
        else if (allocatedCallback == CALLBACK_TEXT_ONESHOT)   { callbackName = "TEXT_ONESHOT"; }
        else if (allocatedCallback == CALLBACK_TEXT_TIMED)     { callbackName = "TEXT_TIMED"; }
        else if (allocatedCallback == CALLBACK_COMPLEX_UPDATE) { callbackName = "COMPLEX_UPDATE"; isDamageCandidate = true; }

        s_spriteAllocCallCountThisBattle++;

        // v0.13.99: log EVERY call uncapped + timestamped with full slot data.
        // Cross-correlation with [ROI-LIVE-SHADOW] yellow-spike timestamps
        // happens in post-hoc analysis of the battle log. Tag suffix CUC =
        // "COMPLEX_UPDATE candidate" so it's grep-friendly.
        DiagLogBattle("BattleTTS: [SPRITE-ALLOC-V99] #%d tick=%u retaddr=0x%08X "
                    "args=[0x%X,0x%X,0x%X,0x%X] result=0x%X "
                    "slot=%d cb=0x%08X(%s) data=[0x%08X,0x%08X,0x%08X] activeChar=%u%s",
                    s_spriteAllocCallCountThisBattle,
                    (unsigned)nowMs, (uint32_t)callerRA,
                    arg0, arg1, arg2, arg3, result,
                    allocatedSlot, allocatedCallback, callbackName,
                    allocatedData[0], allocatedData[1], allocatedData[2],
                    (unsigned)activeChar,
                    isDamageCandidate ? " CUC" : "");
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        DiagLogBattle("BattleTTS: [SPRITE-ALLOC-V99] EXCEPTION in hook");
    }

    return result;
}

// ============================================================================
// Install: sub_482C90 real sprite allocator hook (NEW v0.13.93)
// ============================================================================

static void InstallSub482C90Hook()
{
    if (s_sub482C90HookInstalled) return;

    __try {
        uint8_t* p = (uint8_t*)SUB_482C90_ADDR;
        char hx[50] = {};
        int hp = 0;
        for (int b = 0; b < 8; b++)
            hp += snprintf(hx + hp, sizeof(hx) - hp, "%02X ", p[b]);
        Log::Battle("BattleTTS: [SPRITE-HOOK] sub_482C90 @ 0x%08X: %s",
                    SUB_482C90_ADDR, hx);
    } __except(EXCEPTION_EXECUTE_HANDLER) {}

    MH_STATUS st = MH_CreateHook((void*)SUB_482C90_ADDR,
                                  (void*)&HookedSub482C90,
                                  (void**)&s_origSub482C90);
    if (st != MH_OK) {
        Log::Battle("BattleTTS: [SPRITE-HOOK] sub_482C90 MH_CreateHook FAILED: %d", (int)st);
        return;
    }
    st = MH_EnableHook((void*)SUB_482C90_ADDR);
    if (st != MH_OK) {
        Log::Battle("BattleTTS: [SPRITE-HOOK] sub_482C90 MH_EnableHook FAILED: %d", (int)st);
        return;
    }
    s_sub482C90HookInstalled = true;
    Log::Battle("BattleTTS: [SPRITE-HOOK] Installed at 0x%08X (sub_482C90 - real sprite allocator)",
                SUB_482C90_ADDR);
}

// ============================================================================
// Simplified reset and initialization (v0.13.93)
// ============================================================================

// Reset function called from main battle_tts.cpp on battle entry
static void ResetSpriteAllocatorState()
{
    // v0.13.99: reset per-battle uncapped call counter.
    s_spriteAllocCallCountThisBattle = 0;
    Log::Battle("BattleTTS: [SPRITE-HOOK] Reset sprite allocator state for new battle");
}

// Install function called from main battle_tts.cpp on battle initialization
static void InstallSpriteAllocatorHook()
{
    InstallSub482C90Hook();
    Log::Battle("BattleTTS: [SPRITE-HOOK] Sprite allocator hook installation complete");
}


// ============================================================================
// v0.13.93: Compatibility stubs for old text sprite hooks (DEPRECATED)
// ============================================================================

// These functions are called from main battle_tts.cpp and screenshot.inl
// but are no longer needed with the new sprite allocator approach.
// Kept as empty stubs to maintain compatibility.

static void TextSpriteHook_LogStats(void) {}
static void ResetTextSpriteHookCounters(void) {}
static bool TextSpriteHook_DrainLog() { return false; }
static void InstallSub495280Hook() {}
static void InstallSub4952F0Hook() {}
static void ResetSub495280HookState() {}
// ============================================================================
// Hook: sub_48E830 — action-announce commit (RESTORED v0.14.33, refined v0.14.34)
// ============================================================================
//
// Restored after the v0.13.93 architectural pivot deprecated this hook (in
// favor of sub_482C90 sprite-allocator monitoring) and the v0.14.24 build
// recovery did not re-include it. battle_tts_noeffect.inl depends on this
// hook calling NoEffect_RecordSnapshot to start its no-effect watchdog.
// Without it, status-spell miss/no-effect events (e.g. cast Blind on an
// already-blinded target) are never announced.
//
// Function signature confirmed via disassembly walk in session 65:
//   sub_48E830(uint32_t targetMask)         ; __cdecl, single 32-bit arg
//   - prologue at 0x0048E830: sub esp,0x10 / mov ecx,[esp+0x14]
//   - call site at 0x00485949: push eax / call 0x48e830 / add esp, 4
//
// v0.14.34 refinement — the historical noeffect.inl comment claimed
// 'arg[1]==0x16 (magic action ID)' but v0.14.33 BAT proved that filter was
// wrong: the [CMD] log line on the same battle showed 0x16 = Draw command,
// not Magic. With actionId==0x16 in the gate, the hook never fired the
// player-magic branch on a real Blind cast and the watchdog never started.
// Removing the actionId gate; we now snapshot on every player-path
// (retaddr=0x0048594E) hit. The watchdog's existing activity flags (HP
// delta, status queue, flush announce) filter out actions that genuinely
// had effects, so this is safe — physical hits, magic hits, and successful
// status applications all stay silent. Edge case: a physical attack that
// MISSES will now announce "No effect on X" via the watchdog (was silent
// before). Aaron has previously flagged silent physical misses as poor
// blind-player UX, so this is plausibly an improvement. Will iterate if
// objectionable. The actionId byte at 0x01D27AE3 is still read and logged
// for diagnostic visibility (so we can re-introduce a more accurate filter
// later if needed).

static const uint32_t SUB_48E830_ADDR              = 0x0048E830;
static const uint32_t SUB_48E830_PLAYER_RETADDR    = 0x0048594E;  // player magic-cast caller
static const uint32_t BATTLE_ACTION_ID_ADDR        = 0x01D27AE3;  // uint8 — action ID for current call (diagnostic only as of v0.14.34)

typedef void (__cdecl *Sub48E830Func_t)(uint32_t targetMask);
static Sub48E830Func_t s_origSub48E830 = nullptr;
static bool s_sub48E830HookInstalled = false;

static volatile LONG s_sub48E830HitCount        = 0;  // every entry, regardless of source
static volatile LONG s_sub48E830PlayerHitCount  = 0;  // retaddr matches player path

static void __cdecl HookedSub48E830(uint32_t targetMask)
{
    // Capture caller return address before anything clobbers it.
    uintptr_t retAddr = (uintptr_t)_ReturnAddress();

    // Read action ID from engine global BEFORE running original. The
    // original's prologue reads the same byte at 0x0048E839; reading after
    // would race with any modification the function makes mid-body. Kept
    // even though we no longer gate on it, because the value is useful in
    // the [SPRITE-SPAWN-SEQ] log for diagnosing future filter refinements.
    uint8_t actionId = 0xFF;
    __try { actionId = *(uint8_t*)BATTLE_ACTION_ID_ADDR; } __except(EXCEPTION_EXECUTE_HANDLER) {}

    // Always pass through to preserve game logic.
    s_origSub48E830(targetMask);

    InterlockedIncrement(&s_sub48E830HitCount);

    // Player path only — enemy actions (retaddr=0x00489FC0) are out of scope
    // per noeffect.inl's v1 design notes (rare cases produce no observable
    // change so the watchdog rarely fires anyway).
    if (retAddr == SUB_48E830_PLAYER_RETADDR) {
        InterlockedIncrement(&s_sub48E830PlayerHitCount);
        Log::Battle("BattleTTS: [SPRITE-SPAWN-SEQ] sub_48E830 player-action "
                    "targetMask=0x%X actionId=0x%02X retaddr=0x%08X (snapshot)",
                    targetMask, (unsigned)actionId, (uint32_t)retAddr);
        NoEffect_RecordSnapshot(targetMask);
    }
}

static void InstallSub48E830Hook()
{
    if (s_sub48E830HookInstalled) return;

    MH_STATUS st = MH_CreateHook((LPVOID)SUB_48E830_ADDR,
                                  (LPVOID)&HookedSub48E830,
                                  (LPVOID*)&s_origSub48E830);
    if (st != MH_OK) {
        Log::Battle("BattleTTS: [SPRITE-SPAWN-HOOK] sub_48E830 MH_CreateHook FAILED: %d", (int)st);
        return;
    }
    st = MH_EnableHook((LPVOID)SUB_48E830_ADDR);
    if (st != MH_OK) {
        Log::Battle("BattleTTS: [SPRITE-SPAWN-HOOK] sub_48E830 MH_EnableHook FAILED: %d", (int)st);
        return;
    }
    s_sub48E830HookInstalled = true;
    Log::Battle("BattleTTS: [SPRITE-SPAWN-HOOK] sub_48E830 hook @ 0x%08X \u2014 MH_OK "
                "(trampoline=0x%08X)",
                SUB_48E830_ADDR, (uint32_t)(uintptr_t)s_origSub48E830);
}

static void Sub48E830Hook_Reset()
{
    InterlockedExchange(&s_sub48E830HitCount,       0);
    InterlockedExchange(&s_sub48E830PlayerHitCount, 0);
}
static void ResetSpriteSpawnCountWriterState() { ResetSpriteAllocatorState(); }  // Redirect to new function

// ============================================================================
// v0.13.93: Old popup table constants (compatibility stubs for screenshot.inl)
// ============================================================================

// These constants are referenced by screenshot.inl but are no longer used
// in the new sprite allocator approach. Kept as stubs to maintain compilation.
static bool s_pollScreenshotDirEnsured = false;
static uint32_t s_pollFrameCounter = 0;
static const uint32_t POPUP_RECORD_STRIDE = 20;
static const int POPUP_TRACK_MAX = 16;
static const uint32_t POPUP_TABLE_BASE = 0x01D280C4;
static const uint32_t POPUP_COUNT_ADDR = 0x01D280C0;

// Additional constants for screenshot.inl compatibility
static const int POLL_SCREENSHOT_NEW_MAX = 60;
static const int POLL_SCREENSHOT_TEXTID_MAX = 20;
static int s_pollScreenshotNewCount = 0;
static int s_pollScreenshotTextIdCount = 0;

// Old sprite record structure for screenshot.inl compatibility
struct SpriteRec {
    bool valid;
    uint8_t slot;
    uint8_t text_id;
    uint8_t style;
    uint8_t lifetime;
    uint16_t value;
    uint16_t secondary;
    uint32_t entity_ptr;
};
static SpriteRec s_prevRecords[POPUP_TRACK_MAX] = {};

// Function stub that's only needed if screenshot.inl doesn't provide its own implementation
static void DrainDeferredTextSpriteLog() {
    // v0.13.93: Stub for old text sprite draining
    // No longer needed with new sprite allocator approach
}
