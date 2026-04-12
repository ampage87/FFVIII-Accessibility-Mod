// battle_tts_hp.inl — HP tracking, damage announce, target selection, HP check keys
// Included from battle_tts.cpp. Do not compile independently.
// v0.12.18: Extracted for readability.

// v0.12.48: Per-slot GF animation fired tracking.
// Set by HookedBattleEffect when a GF animation dispatches for a slot.
// Cleared by EWM_ClampGFState when a new GF starts loading for a slot.
// Used by AnnouncePartyMemberHP to stop showing GF HP after animation fires.
static volatile bool s_gfAnimFired[BATTLE_ALLY_SLOTS] = {};

// v0.12.46: Per-slot GF HP substitution tracking (DEPRECATED, kept for cleanup).
// Set when a character confirms a GF command (turn ends with GF selected).
// Cleared when that character gets their next turn.
// Engine flags (entity+0x7C, 0x01D76971) are unreliable — they stay stale forever.
static bool s_gfHpSubstitutionActive[BATTLE_ALLY_SLOTS] = {};
static uint8_t s_gfSummonedIdx[BATTLE_ALLY_SLOTS] = {0xFF, 0xFF, 0xFF}; // v0.12.83: which GF (savemap index) each slot is summoning

// v0.10.34: Damage/healing TTS — display-triggered announcements
// ============================================================================
// HP changes are tracked silently. Damage announcements are triggered when
// the damage display value (0x01D2834A) changes to a new non-zero number,
// meaning the engine is now showing the damage text on screen. This syncs
// TTS with the visual damage display instead of with the early HP computation.
//
// For healing (HP increase, no damage display), a fallback timeout fires.
//
// Address 0x01D2834A (uint16) holds displayed damage value (including overkill).
// Confirmed via v0.10.28 diagnostic.

static const uint32_t BATTLE_DAMAGE_DISPLAY_ADDR = 0x01D2834A; // uint16: last displayed damage value (holds permanently, not a timer)

// v0.10.47: Damage animation active flag at 0x01D280C0.
// Discovered via DSCAN diagnostic (v0.10.46): this byte is 01 when the engine
// is displaying damage numbers on screen, and transitions to 00 when the
// animation completes (~1.4 seconds after HP change). This is our trigger.
static const uint32_t BATTLE_DAMAGE_ANIM_FLAG = 0x01D280C0; // BYTE: 01=animating, 00=done

static uint32_t s_hpPrev[BATTLE_TOTAL_SLOTS] = {};      // previous HP per slot
static uint32_t s_hpMaxPrev[BATTLE_TOTAL_SLOTS] = {};   // previous maxHP (detect population)
static bool s_hpTrackingReady = false;                   // true after first frame of valid HP
static int32_t s_hpAccumDelta[BATTLE_TOTAL_SLOTS] = {};  // accumulated HP delta pending announce
static bool s_hpAccumPending[BATTLE_TOTAL_SLOTS] = {};   // true if accumulated delta awaits announce
static DWORD s_hpFirstPendingTime = 0;                   // GetTickCount when first HP change recorded
static bool s_anyHpPending = false;                      // any slot has pending HP changes
static const DWORD HP_HEAL_TIMEOUT_MS = 1500;            // v0.10.51: shorter heal timeout (no anim flag available)

// v0.10.47: Animation flag trigger — replaces display-to-zero approach.
// We watch BATTLE_DAMAGE_ANIM_FLAG: when it transitions from non-zero to zero,
// the damage number has finished displaying on screen. Flush announcements.
static bool s_damageAnimWasActive = false;               // true while anim flag is non-zero
static DWORD s_damageAnimStartTime = 0;                  // GetTickCount when anim flag first went non-zero
static const DWORD HP_ANIM_TIMEOUT_MS = 4000;            // safety net: max wait for anim flag to clear

// v0.10.42: New-turn flush — when active_char_id changes, the previous action's
// animation is guaranteed complete (engine wouldn't advance otherwise).
// Immediately flush any pending HP announcements on this edge.
static uint8_t s_hpTurnFlushLastChar = 0xFF;             // last active_char_id seen by HP tracker

// v0.10.42: New-turn flush — when active_char_id transitions, the previous
// action animation is guaranteed complete (engine wouldn't hand control to the
// next character otherwise). Immediately flush any pending HP announcements.
// This solves the EWM display-timer-frozen problem: the display countdown can't
// reach zero because EWM freezes the ATB function that drives it, but by the
// time the next turn starts the animation is visually done.
static uint8_t s_hpTrackLastActiveChar = 0xFF;           // track turn transitions for flush

// Get a name for any battle slot (allies 0-2, enemies 3-6)
// Uses the persistent name cache for enemies (survives KO).
static const char* GetSlotName(int slot, char* nameBuf, int bufSize)
{
    if (slot < BATTLE_ALLY_SLOTS) {
        return GetBattleCharName((uint8_t)slot);
    } else {
        int idx = slot - BATTLE_ALLY_SLOTS;
        if (idx >= 0 && idx < BATTLE_ENEMY_SLOTS && s_enemyNameCacheBuilt && s_enemyNameCache[idx][0] != '\0') {
            return s_enemyNameCache[idx];
        }
        // Fallback: try live read (might fail for KO'd enemies)
        if (GetEnemyName(slot, nameBuf, bufSize)) return nameBuf;
        snprintf(nameBuf, bufSize, "Enemy %d", slot - BATTLE_ALLY_SLOTS + 1);
        return nameBuf;
    }
}

// ============================================================================
// v0.10.35: Party HP & Status check keys (1/2/3 = individual, H = full party)
// ============================================================================

static bool s_hpKey1WasDown = false;
static bool s_hpKey2WasDown = false;
static bool s_hpKey3WasDown = false;
static bool s_hpKeyHWasDown = false;

// Build a status effect string from the entity's persistent + timed status bytes.
// Returns the number of effects found. Empty string if none.
#define APPEND_STATUS(name_str) do { \
    if (count > 0 && pos < bufSize - 2) pos += snprintf(buf + pos, bufSize - pos, ", "); \
    pos += snprintf(buf + pos, bufSize - pos, "%s", name_str); \
    count++; \
} while(0)

static int BuildStatusString(int slot, char* buf, int bufSize)
{
    buf[0] = '\0';
    uint8_t* blk = GetEntityBlock(slot);
    if (!blk) return 0;
    
    int count = 0;
    int pos = 0;
    
    __try {
        uint8_t persist = *(blk + BENT_PERSIST_STATUS);
        uint8_t timed0  = *(blk + BENT_TIMED_STATUS_0);
        uint8_t timed1  = *(blk + BENT_TIMED_STATUS_1);
        uint8_t timed2  = *(blk + BENT_TIMED_STATUS_2);
        uint8_t timed3  = *(blk + BENT_TIMED_STATUS_3);
        
        // Persistent statuses (0x78)
        if (persist & 0x01) APPEND_STATUS("KO");
        if (persist & 0x02) APPEND_STATUS("Poison");
        if (persist & 0x04) APPEND_STATUS("Petrify");
        if (persist & 0x08) APPEND_STATUS("Blind");
        if (persist & 0x10) APPEND_STATUS("Silence");
        if (persist & 0x20) APPEND_STATUS("Berserk");
        if (persist & 0x40) APPEND_STATUS("Zombie");
        
        // Timed statuses (0x00)
        if (timed0 & 0x01) APPEND_STATUS("Sleep");
        if (timed0 & 0x02) APPEND_STATUS("Haste");
        if (timed0 & 0x04) APPEND_STATUS("Slow");
        if (timed0 & 0x08) APPEND_STATUS("Stop");
        if (timed0 & 0x10) APPEND_STATUS("Regen");
        if (timed0 & 0x20) APPEND_STATUS("Protect");
        if (timed0 & 0x40) APPEND_STATUS("Shell");
        if (timed0 & 0x80) APPEND_STATUS("Reflect");
        
        // Timed statuses (0x01)
        if (timed1 & 0x01) APPEND_STATUS("Aura");
        if (timed1 & 0x02) APPEND_STATUS("Curse");
        if (timed1 & 0x04) APPEND_STATUS("Doom");
        if (timed1 & 0x10) APPEND_STATUS("Gradual Petrify");
        if (timed1 & 0x20) APPEND_STATUS("Float");
        if (timed1 & 0x40) APPEND_STATUS("Confuse");
        
        // Timed statuses (0x02)
        if (timed2 & 0x04) APPEND_STATUS("Double");
        if (timed2 & 0x08) APPEND_STATUS("Triple");
        
        // Timed statuses (0x03)
        if (timed3 & 0x02) APPEND_STATUS("Angel Wing");
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
    
    return count;
}

#undef APPEND_STATUS

// ============================================================================
// v0.10.93: GF summoning state helpers
// ============================================================================
// When a GF is being summoned, it takes damage in place of the character.
// The GF's HP is displayed overlaid on the character's HP bar.
// Address 0x01D76998 holds a pointer to the active GF's savemap struct.
// GF savemap struct: name at +0x00 (12 bytes FF8-encoded), HP at +0x12 (uint16).

static const uint32_t GF_ACTIVE_SAVEMAP_PTR = 0x01D76998;  // uint32 pointer to GF savemap struct
static const uint32_t GF_SLOT_ADDR          = 0x01D76970;  // int8: which party slot summoned
static const uint32_t GF_ACTIVE_FLAG_ADDR   = 0x01D76971;  // uint8: 1 during loading+animation
static const uint32_t GF_DISPLAY_TIMER_ADDR = 0x01D769D6;  // uint8: countdown, 0=animation phase

// Check if a party slot is currently summoning a GF.
// v0.10.95: Uses per-character entity+0x7C flag instead of global gfSlot.
static bool IsSlotSummoningGF(int partySlot)
{
    if (partySlot < 0 || partySlot >= BATTLE_ALLY_SLOTS) return false;
    __try {
        uint8_t* ent = (uint8_t*)(BATTLE_ENTITY_ARRAY_BASE + partySlot * BATTLE_ENTITY_STRIDE);
        uint16_t gfFlag = *(uint16_t*)(ent + BENT_GF_SUMMON_FLAG);
        return (gfFlag != 0);
    } __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}

// Check if a GF animation is playing (active flag set, timer at 0).
// v0.10.93 fix: If our code patch is blocking the fire, the animation ISN'T
// playing — the GF is loaded but waiting to fire. Only return true when the
// fire has actually happened (patch restored) and the animation is running.
// We check the actual byte at 0x004B04B4 rather than s_gfFirePatched (declared later).
static bool IsGFAnimationPlaying(void)
{
    // If our EWM code patch is active (byte == 0xC3 RET), fire is blocked — no animation yet
    __try {
        uint8_t patchByte = *(uint8_t*)0x004B04B4;
        if (patchByte == 0xC3) return false;  // fire blocked by our patch
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
    
    __try {
        uint8_t gfActive = *(uint8_t*)GF_ACTIVE_FLAG_ADDR;
        if (gfActive != 1) return false;
        uint8_t timer = *(uint8_t*)GF_DISPLAY_TIMER_ADDR;
        return (timer == 0);
    } __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}

// Savemap GF section: 16 GFs, each 0x44 bytes, starting at 0x1CFDCA8.
// Name at +0x00 (12 bytes, FF8-encoded). HP at +0x12 (uint16). Exists at +0x11.
static const uint32_t SAVEMAP_GF_BASE   = 0x1CFDCA8;
static const uint32_t SAVEMAP_GF_STRIDE = 0x44;

// Read the summoning GF's name and HP for a specific party slot.
// v0.10.95: Takes partySlot parameter instead of reading global gfSlot.
// Approach 1: Runtime pointer (only valid when this slot matches gfSlot during fire/animation)
// Approach 2: Character junction lookup — find which GFs are junctioned to this character,
//             then pick the first one with HP > 0. Works reliably during loading phase.
static bool GetActiveGFInfo(int partySlot, char* nameOut, int nameMax, uint16_t* hpOut)
{
    nameOut[0] = '\0';
    *hpOut = 0;
    if (partySlot < 0 || partySlot >= BATTLE_ALLY_SLOTS) return false;
    
    // v0.12.83: Direct lookup using saved GF index from submenu selection.
    // This is authoritative — it records exactly which GF the player selected.
    if (s_gfSummonedIdx[partySlot] < 16) {
        __try {
            uint8_t* gfBase = (uint8_t*)(SAVEMAP_GF_BASE + s_gfSummonedIdx[partySlot] * SAVEMAP_GF_STRIDE);
            DecodeFF8String(gfBase, nameOut, nameMax);
            *hpOut = *(uint16_t*)(gfBase + 0x12);
            if (nameOut[0] != '\0') {
                Log::Battle("BattleTTS: [HP-CHECK] GF direct lookup: slot=%d gfIdx=%d name='%s' hp=%u",
                           partySlot, (int)s_gfSummonedIdx[partySlot], nameOut, (unsigned)*hpOut);
                return true;
            }
        } __except(EXCEPTION_EXECUTE_HANDLER) {}
    }
    
    // Fallback: Runtime pointer (only valid during fire/animation phase AND for gfSlot)
    __try {
        int8_t gfSlot = *(int8_t*)GF_SLOT_ADDR;
        if (gfSlot == partySlot) {
            uint32_t gfPtr = *(uint32_t*)GF_ACTIVE_SAVEMAP_PTR;
            if (gfPtr >= 0x01000000 && gfPtr <= 0x7FFFFFFF) {
                uint8_t* gfBase = (uint8_t*)(uintptr_t)gfPtr;
                __try {
                    DecodeFF8String(gfBase + 0x00, nameOut, nameMax);
                    *hpOut = *(uint16_t*)(gfBase + 0x12);
                    if (nameOut[0] != '\0') return true;
                } __except(EXCEPTION_EXECUTE_HANDLER) {}
            }
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
    
    // Approach 2: Character junction lookup using the specified partySlot
    __try {
        uint8_t charIdx = *(uint8_t*)(0x1CFE74C + partySlot);  // SAVEMAP_PARTY_FORMATION
        if (charIdx >= 8) return false;
        
        uint8_t* charBase = (uint8_t*)(0x1CFE0E8 + charIdx * 0x98);  // SAVEMAP_CHAR_DATA_BASE
        uint16_t gfMask = *(uint16_t*)(charBase + 0x58);
        
        for (int gfIdx = 0; gfIdx < 16; gfIdx++) {
            if (!(gfMask & (1 << gfIdx))) continue;
            
            uint8_t* gfBase = (uint8_t*)(SAVEMAP_GF_BASE + gfIdx * SAVEMAP_GF_STRIDE);
            uint16_t hp = *(uint16_t*)(gfBase + 0x12);
            if (hp > 0) {
                DecodeFF8String(gfBase, nameOut, nameMax);
                *hpOut = hp;
                Log::Battle("BattleTTS: [HP-CHECK] GF junction lookup: slot=%d charIdx=%d gfIdx=%d name='%s' hp=%u mask=0x%04X",
                           partySlot, (int)charIdx, gfIdx, nameOut, (unsigned)hp, (unsigned)gfMask);
                return (nameOut[0] != '\0');
            }
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
    
    return false;
}

// Announce HP + status for a single party slot (0-2). Uses Channel 1 (interrupts menu speech).
// v0.10.93: If the slot is summoning a GF, announce GF name + HP instead.
static void AnnouncePartyMemberHP(int partySlot)
{
    if (partySlot < 0 || partySlot >= BATTLE_ALLY_SLOTS) return;
    
    // v0.10.95: Check if this slot is summoning a GF (per-character flag)
    // v0.12.48: entity+0x7C for per-slot detection, s_gfAnimFired for clearing.
    // Show GF HP from when entity+0x7C is set (loading starts) until the
    // battle effect dispatcher fires the GF animation (s_gfAnimFired set).
    if (IsSlotSummoningGF(partySlot) && !s_gfAnimFired[partySlot]) {
        char gfName[64];
        uint16_t gfHP = 0;
        if (GetActiveGFInfo(partySlot, gfName, sizeof(gfName), &gfHP)) {
            char buf[256];
            snprintf(buf, sizeof(buf), "%s: %u HP.", gfName, (unsigned)gfHP);
            BattleSpeakEvent(buf);
            Log::Battle("BattleTTS: [HP-CHECK] GF substitution: slot%d -> %s", partySlot, buf);
            return;
        }
    }
    
    uint32_t maxHP = GetEntityMaxHP(partySlot);
    if (maxHP == 0) {
        // Slot not populated
        char buf[64];
        snprintf(buf, sizeof(buf), "Party slot %d: empty.", partySlot + 1);
        BattleSpeakEvent(buf);  // v0.10.47: Ch2 queued
        return;
    }
    
    const char* name = GetBattleCharName((uint8_t)partySlot);
    uint32_t curHP = GetEntityHP(partySlot);
    
    char statusBuf[256];
    int statusCount = BuildStatusString(partySlot, statusBuf, sizeof(statusBuf));
    
    char buf[384];
    if (statusCount > 0) {
        snprintf(buf, sizeof(buf), "%s: %u of %u HP. %s.", name, curHP, maxHP, statusBuf);
    } else {
        snprintf(buf, sizeof(buf), "%s: %u of %u HP.", name, curHP, maxHP);
    }
    BattleSpeakEvent(buf);  // v0.10.47: Ch2 queued
    Log::Battle("BattleTTS: [HP-CHECK] %s", buf);
}

// Announce HP for all active party members. v0.10.44: Ch2 event.
static void AnnounceFullPartyHP()
{
    char buf[512];
    int pos = 0;
    int memberCount = 0;
    
    for (int slot = 0; slot < BATTLE_ALLY_SLOTS; slot++) {
        uint32_t maxHP = GetEntityMaxHP(slot);
        if (maxHP == 0) continue;
        
        const char* name = GetBattleCharName((uint8_t)slot);
        uint32_t curHP = GetEntityHP(slot);
        
        if (memberCount > 0 && pos < (int)sizeof(buf) - 2)
            pos += snprintf(buf + pos, sizeof(buf) - pos, ". ");
        
        pos += snprintf(buf + pos, sizeof(buf) - pos, "%s %u of %u", name, curHP, maxHP);
        memberCount++;
    }
    
    if (memberCount == 0) {
        BattleSpeakEvent("No party members.");
        return;
    }
    
    if (pos < (int)sizeof(buf) - 1) buf[pos++] = '.';
    buf[pos] = '\0';
    
    BattleSpeakEvent(buf);  // v0.10.47: Ch2 queued
    Log::Battle("BattleTTS: [HP-CHECK] Party: %s", buf);
}

static void PollHPCheckKeys()
{
    bool key1 = (GetAsyncKeyState('1') & 0x8000) != 0;
    bool key2 = (GetAsyncKeyState('2') & 0x8000) != 0;
    bool key3 = (GetAsyncKeyState('3') & 0x8000) != 0;
    bool keyH = (GetAsyncKeyState('H') & 0x8000) != 0;
    
    if (key1 && !s_hpKey1WasDown) AnnouncePartyMemberHP(0);
    if (key2 && !s_hpKey2WasDown) AnnouncePartyMemberHP(1);
    if (key3 && !s_hpKey3WasDown) AnnouncePartyMemberHP(2);
    if (keyH && !s_hpKeyHWasDown) AnnounceFullPartyHP();
    
    s_hpKey1WasDown = key1;
    s_hpKey2WasDown = key2;
    s_hpKey3WasDown = key3;
    s_hpKeyHWasDown = keyH;
}

// ============================================================================
// v0.10.97: Target selection TTS — function body (after GetSlotName)
// ============================================================================
static void PollTargetSelection()
{
    if (!s_pActiveCharId) return;
    uint8_t activeChar = 0xFF;
    __try { activeChar = *s_pActiveCharId; } __except(EXCEPTION_EXECUTE_HANDLER) { return; }
    if (activeChar >= 3) {
        // No turn active — reset
        if (s_inTargetSelect) {
            s_inTargetSelect = false;
            Log::Battle("BattleTTS: [TARGET] Exited target select (turn ended)");
        }
        s_lastTargetBitmask = 0;
        return;
    }
    
    uint8_t tgtMask = 0;
    uint8_t tgtScope = 0;
    __try { tgtMask = *(uint8_t*)BATTLE_TARGET_BITMASK; } __except(EXCEPTION_EXECUTE_HANDLER) { return; }
    __try { tgtScope = *(uint8_t*)BATTLE_TARGET_SCOPE; } __except(EXCEPTION_EXECUTE_HANDLER) {}
    
    // Re-announce if either the bitmask OR the scope changed
    // (scope change = player toggled single/all targeting on same cursor position)
    if (tgtMask == s_lastTargetBitmask && tgtScope == s_lastTargetScope) return;
    
    if (tgtMask == 0) {
        s_lastTargetBitmask = tgtMask;
        s_lastTargetScope = tgtScope;
        if (s_inTargetSelect) {
            s_inTargetSelect = false;
            Log::Battle("BattleTTS: [TARGET] Exited target select (mask=0)");
        }
        return;
    }
    
    // Debounce rapid changes — do NOT update tracking vars here.
    DWORD now = GetTickCount();
    if (now - s_targetLastAnnounceTick < TARGET_DEBOUNCE_MS) return;
    s_targetLastAnnounceTick = now;
    s_lastTargetBitmask = tgtMask;
    s_lastTargetScope = tgtScope;
    
    // v0.10.100: Use scope byte at 0x01D76883 to detect all-target vs single-target.
    // Observed values: 3 = single target (Attack), 1 = all enemies (GF).
    // When scope != 3, the action targets all entities on that side.
    // v0.10.112: Also require multi-bit mask for all-target. Draw uses scope=1
    // with a single-bit mask, which should announce the single enemy name.
    bool isAllTarget = (tgtScope != 3 && tgtScope != 0 && CountBits(tgtMask) > 1);
    
    char buf[128];
    
    if (isAllTarget) {
        // All-target: determine which side based on the cursor bitmask
        int slot = BitmaskToSlot(tgtMask);
        if (slot >= BATTLE_ALLY_SLOTS) {
            snprintf(buf, sizeof(buf), "All enemies");
        } else {
            snprintf(buf, sizeof(buf), "All allies");
        }
    } else if (CountBits(tgtMask) == 1) {
        // Single target
        int slot = BitmaskToSlot(tgtMask);
        if (slot < 0) return;
        char nameBuf[64];
        const char* name = GetSlotName(slot, nameBuf, sizeof(nameBuf));
        snprintf(buf, sizeof(buf), "%s", name);
    } else {
        // Multi-bit bitmask (fallback for multi-target with scope=3)
        bool hasAllies = (tgtMask & 0x07) != 0;
        bool hasEnemies = (tgtMask & 0x78) != 0;
        if (hasEnemies && !hasAllies) {
            snprintf(buf, sizeof(buf), "All enemies");
        } else if (hasAllies && !hasEnemies) {
            snprintf(buf, sizeof(buf), "All allies");
        } else {
            snprintf(buf, sizeof(buf), "All targets");
        }
    }
    
    BattleSpeak(buf, PRIO_MENU, true);
    
    if (!s_inTargetSelect) {
        s_inTargetSelect = true;
        Log::Battle("BattleTTS: [TARGET] Entered target select: mask=0x%02X scope=%u -> %s", (unsigned)tgtMask, (unsigned)tgtScope, buf);
    } else {
        Log::Battle("BattleTTS: [TARGET] Target changed: mask=0x%02X scope=%u -> %s", (unsigned)tgtMask, (unsigned)tgtScope, buf);
    }
}

// Flush all pending HP change announcements (damage and healing).
// v0.10.40: Back to Channel 2 (independent SAPI voice for damage/events).
// Each slot with accumulated HP changes gets its own individual announcement.
// Per-slot deltas ensure multi-target attacks announce each target separately.
static void FlushHPAnnouncements(const char* trigger)
{
    // v0.10.47: Read the engine's displayed damage value for overkill correction.
    // 0x01D2834A holds the actual damage number shown on screen, which includes
    // overkill (damage exceeding remaining HP). Our HP delta is capped at remaining HP.
    // Use max(abs(delta), displayValue) to announce the correct number.
    uint16_t displayVal = 0;
    __try { displayVal = *(uint16_t*)BATTLE_DAMAGE_DISPLAY_ADDR; } __except(EXCEPTION_EXECUTE_HANDLER) {}
    
    for (int slot = 0; slot < BATTLE_TOTAL_SLOTS; slot++) {
        if (!s_hpAccumPending[slot]) continue;
        
        int32_t accum = s_hpAccumDelta[slot];
        s_hpAccumDelta[slot] = 0;
        s_hpAccumPending[slot] = false;
        
        if (accum == 0) continue;  // net zero
        
        char nameBuf[64];
        const char* name = GetSlotName(slot, nameBuf, sizeof(nameBuf));
        char buf[256];
        
        if (accum < 0) {
            // Damage — use max(abs(delta), displayValue) for overkill correction
            uint32_t deltaDmg = (uint32_t)(-accum);
            uint32_t dmg = deltaDmg;
            if ((uint32_t)displayVal > dmg) {
                dmg = (uint32_t)displayVal;
            }
            uint32_t nowHP = GetEntityHP(slot);
            bool ko = (nowHP == 0);
            
            if (ko) {
                snprintf(buf, sizeof(buf), "%s takes %u damage. Defeated.", name, dmg);
            } else {
                snprintf(buf, sizeof(buf), "%s takes %u damage.", name, dmg);
            }
            BattleSpeakEvent(buf);
            Log::Battle("BattleTTS: [HP-TRACK] %s (slot%d, hpDelta=%d, display=%u, used=%u, hp=%u/%u, trigger=%s)",
                       buf, slot, accum, (unsigned)displayVal, dmg,
                       GetEntityHP(slot), GetEntityMaxHP(slot), trigger);
        } else {
            // Healing — use max(delta, displayValue) for overcapped heals
            uint32_t healAmt = (uint32_t)accum;
            if ((uint32_t)displayVal > healAmt) {
                healAmt = (uint32_t)displayVal;
            }
            snprintf(buf, sizeof(buf), "%s recovers %u HP.", name, healAmt);
            BattleSpeakEvent(buf);
            Log::Battle("BattleTTS: [HP-TRACK] %s (slot%d, delta=+%d, display=%u, used=%u, hp=%u/%u, trigger=%s)",
                       buf, slot, accum, (unsigned)displayVal, healAmt,
                       GetEntityHP(slot), GetEntityMaxHP(slot), trigger);
        }
    }
    s_anyHpPending = false;
}

static void PollHPChanges()
{
    if (!s_initAnnounceDone || !s_enemyAnnounceDone) return;
    
    // First call: populate baseline HP values, don't announce
    if (!s_hpTrackingReady) {
        bool anyValid = false;
        for (int i = 0; i < BATTLE_TOTAL_SLOTS; i++) {
            s_hpPrev[i] = GetEntityHP(i);
            s_hpMaxPrev[i] = GetEntityMaxHP(i);
            s_hpAccumDelta[i] = 0;
            s_hpAccumPending[i] = false;
            if (s_hpMaxPrev[i] > 0) anyValid = true;
        }
        if (anyValid) {
            s_hpTrackingReady = true;
            s_anyHpPending = false;
            s_damageAnimWasActive = false;
            Log::Battle("BattleTTS: [HP-TRACK] Baseline captured");
        }
        return;
    }
    
    DWORD now = GetTickCount();
    
    // --- Step 1: Detect HP changes silently (don't announce yet) ---
    for (int slot = 0; slot < BATTLE_TOTAL_SLOTS; slot++) {
        uint32_t curHP = GetEntityHP(slot);
        uint32_t curMaxHP = GetEntityMaxHP(slot);
        uint32_t prevHP = s_hpPrev[slot];
        
        if (s_hpMaxPrev[slot] == 0 && curMaxHP > 0) {
            s_hpPrev[slot] = curHP;
            s_hpMaxPrev[slot] = curMaxHP;
            continue;
        }
        if (curMaxHP == 0) {
            s_hpPrev[slot] = 0;
            s_hpMaxPrev[slot] = 0;
            continue;
        }
        
        if (curHP != prevHP) {
            int32_t delta = (int32_t)curHP - (int32_t)prevHP;
            s_hpAccumDelta[slot] += delta;
            s_hpAccumPending[slot] = true;
            if (!s_anyHpPending) {
                s_anyHpPending = true;
                s_hpFirstPendingTime = now;
            }
            Log::Battle("BattleTTS: [HP-TRACK] slot%d HP %u->%u delta=%d (silent, awaiting display)",
                       slot, prevHP, curHP, delta);
            s_hpPrev[slot] = curHP;
        }
        s_hpMaxPrev[slot] = curMaxHP;
    }
    
    // --- Step 1b: New-turn flush (v0.10.42, fixed v0.10.43) ---
    // When active_char_id transitions TO a valid player slot (0-2), the previous
    // action's animation is guaranteed complete — the engine wouldn't hand control
    // to a new character while an animation is still playing.
    //
    // CRITICAL: Do NOT flush when transitioning TO 0xFF. That transition means
    // the player just confirmed a command — HP changes were computed instantly
    // but the attack animation is ABOUT TO START, not finished.
    //
    // Valid flush transitions:
    //   0xFF → 0/1/2  (enemy attacks done, next player turn starting)
    //   0/1/2 → different 0/1/2  (immediate turn handoff, previous anim done)
    // Invalid (do not flush):
    //   0/1/2 → 0xFF  (command confirmed, animation starting)
    {
        uint8_t curActiveChar = 0xFF;
        if (s_pActiveCharId) {
            __try { curActiveChar = *s_pActiveCharId; } __except(EXCEPTION_EXECUTE_HANDLER) {}
        }
        if (curActiveChar != s_hpTrackLastActiveChar) {
            // Only flush when transitioning TO a valid player slot
            bool flushNow = (curActiveChar < 3 && s_hpTrackLastActiveChar != curActiveChar);
            if (flushNow && s_anyHpPending) {
                Log::Battle("BattleTTS: [HP-TRACK] New turn flush (char %u->%u)",
                           (unsigned)s_hpTrackLastActiveChar, (unsigned)curActiveChar);
                s_damageAnimWasActive = false;
                FlushHPAnnouncements("new-turn");
            }
            s_hpTrackLastActiveChar = curActiveChar;
        }
    }
    
    // --- Step 2: Animation flag trigger (v0.10.47) ---
    // Watch 0x01D280C0: the engine sets this to 01 when damage numbers are being
    // displayed on screen, and clears it to 00 when the animation completes.
    // When it transitions from non-zero to zero, flush pending announcements.
    // This replaces the broken display-to-zero approach (0x01D2834A never clears).
    if (s_anyHpPending) {
        uint8_t animFlag = 0;
        __try { animFlag = *(uint8_t*)BATTLE_DAMAGE_ANIM_FLAG; } __except(EXCEPTION_EXECUTE_HANDLER) {}
        bool healTimeout = (now - s_hpFirstPendingTime >= HP_HEAL_TIMEOUT_MS);
        
        if (animFlag != 0 && !s_damageAnimWasActive) {
            // Flag just went active — damage number appearing on screen
            s_damageAnimWasActive = true;
            s_damageAnimStartTime = now;
            Log::Battle("BattleTTS: [HP-TRACK] Anim flag active (0x%08X=%u)",
                       BATTLE_DAMAGE_ANIM_FLAG, (unsigned)animFlag);
        }
        
        if (animFlag == 0 && s_damageAnimWasActive) {
            // Flag cleared — damage number animation finished, announce now!
            s_damageAnimWasActive = false;
            Log::Battle("BattleTTS: [HP-TRACK] Anim flag cleared after %ums",
                       (unsigned)(now - s_damageAnimStartTime));
            FlushHPAnnouncements("anim-done");
        } else if (s_damageAnimWasActive && (now - s_damageAnimStartTime >= HP_ANIM_TIMEOUT_MS)) {
            // Safety net: flag stuck non-zero too long
            s_damageAnimWasActive = false;
            FlushHPAnnouncements("anim-timeout");
        } else if (!s_damageAnimWasActive && healTimeout) {
            // Fallback for healing (no damage display) or if flag never activated
            FlushHPAnnouncements("heal-timeout");
        }
    } else {
        // No pending HP changes — reset tracking
        s_damageAnimWasActive = false;
    }
}

// ============================================================================
// v0.10.25/38/55: Enhanced Wait Mode (EWM) — ATB capping
// ============================================================================
// Prevents actions from triggering while the player is choosing a command,
// without altering the Speed-based turn economy.
//
// v0.10.55: ATB CAPPING replaces the previous full-freeze approach.
// Instead of skipping the ATB update function entirely (which gave players
// extra turns by removing decision-time costs), the hook now:
//   1. Always calls the original ATB update function (gauges fill normally)
//   2. After it returns, caps every entity's ATB at max-1 (except the
//      active deciding character, whose ATB is already at max)
// This means:
//   - Nobody can trigger a new action while the player reads TTS output
//   - ATB fills at correct Speed-based rates → turn ratios are preserved
//   - When the cap lifts, the entity closest to max triggers next (within 1 tick)
//   - No gameplay advantage from slow decision-making
//
// Hook target: ATB update function at 0x004842B0 (MinHook intercept).
// Discovery: v0.10.36-37 via hardware write BP on entity ATB.
// The function handles status timer decrements AND ATB increments
// for all 7 entity slots (loop with add esi,0xD0 stride).
//
// The hook runs on the GAME THREAD (called from the battle main loop).
// s_ewmShouldCap / s_ewmCapExcludeSlot are set by our mod thread
// (Update/PollToggle) and read by the hook.
//
// Toggle: "O" key (works in all game modes, not just battle).
// Persistence: ewm_config.txt in mod root ("1"=on, "0"=off). Default: on.

static const uint32_t ATB_UPDATE_FUNC_ADDR = 0x004842B0;  // confirmed function entry

typedef void (__cdecl *ATBUpdateFn)(void);
static ATBUpdateFn s_originalATBUpdate = nullptr;  // trampoline to original function
static bool s_ewmHookInstalled = false;            // true after MinHook setup

// Volatile flags: set by mod thread, read by game thread hook.
// When s_ewmShouldCap is true, ATB runs normally but all entities (except
// the deciding character) are capped at ATB_max - 1, preventing any new
// turn from triggering. This preserves Speed-based turn ratios while
// giving the player time to read TTS output without being attacked.
static volatile bool s_ewmShouldCap = false;
static volatile uint8_t s_ewmCapExcludeSlot = 0xFF;  // slot to exclude from capping (the deciding character)
// ============================================================================
// v0.10.64: GF timer function hook (MinHook sandwich)
// ============================================================================
// GF loading countdown at 0x01D769D6 is driven by a dedicated function at
// 0x004B0500. Discovery: v0.10.63 hardware write BP on 0x01D769D6 → write
// instruction at 0x004B063B (EIP=0x004B063F), function entry scan found
// SUB ESP,14 / PUSH EBX/EBP/ESI/EDI / MOV EBP,0x01D76971 at 0x004B0500.
//
// When EWM cap is active (player is deciding), we skip the GF timer function
// entirely. The countdown freezes in place and can't reach 0 (fire).
// When the cap releases, the function runs normally and the timer resumes.
// This prevents risk-free GF loading during decision windows.

static const uint32_t GF_TIMER_FUNC_ADDR = 0x004B0500;  // confirmed function entry

// v0.10.78-80: 0x1D0 stride constants REMOVED (v0.10.88).
// Deep research round 2 proved: 0x01D27D94 = Enemy 1 ATB (3*0xD0+0x0C), NOT a GF timer.
// The GF timer is a separate SpeedMod-only countdown in the 0x01D768xx region.

typedef void (__cdecl *GFTimerFn)(void);
static GFTimerFn s_originalGFTimerUpdate = nullptr;  // trampoline to original
static bool s_gfTimerHookInstalled = false;

static volatile bool s_ewmCapGF = false;  // true while player is deciding

// Forward declarations (defined in FFNx hook section below)
static bool s_ffnxGFHookInstalled;
static volatile LONG s_ffnxHookCallCount;

// v0.10.95: Per-slot GF max inflation state (used by HookedATBUpdate on game thread)
// Indexed by party slot (0-2). Each slot independently tracks whether its
// compStats+0x16 has been inflated to 0xFFFF to prevent GF fire.
static bool s_gfMaxInflated[BATTLE_ALLY_SLOTS] = {};    // true while +0x16 is set to 0xFFFF
static uint16_t s_gfRealMax[BATTLE_ALLY_SLOTS] = {};     // saved real max value to restore later

// v0.10.81: GF active flag hiding
static bool s_gfFlagHidden = false;
static uint8_t s_gfSavedSlot = 0xFF;
// v0.10.86: Sticky hide — once set, stays true until player executes a command.
// This prevents the flickering that broke ATB in v0.10.81.
static bool s_gfStickyHidden = false;

// v0.10.85: CODE PATCH — RET at the fire handler entry.
// v0.10.84 proved: patching the immediate value (5→3) still fires because the
// fire setup code at 0x004B04BE+ executes regardless of what state68 is set to.
// Fix: patch the OPCODE at 0x004B04B4 from C7 (MOV) to C3 (RET). This makes
// the state machine handler return immediately before any fire setup code runs.
// The state machine stays in state 3 (loading) until we unpatch.
// When cap releases, restore C7 and the GF fires on the next state machine tick.
static const uint32_t GF_FIRE_PATCH_ADDR = 0x004B04B4; // opcode byte of MOV [state68], 5
static const uint8_t  GF_FIRE_VALUE     = 0xC7;        // original: C7 = MOV opcode
static const uint8_t  GF_SAFE_VALUE     = 0xC3;        // patched: C3 = RET (skip fire setup)
static bool s_gfFirePatched = false;                    // true while byte is patched to RET
static bool s_gfFirePatchReady = false;                 // true after VirtualProtect succeeded

// v0.10.89: GF effect function pointer table.
// Deep research says 0xC81774 holds a table of function pointers, one per GF.
// When the engine is about to fire a GF summon, it reads table[gfId] to get
// the effect function, then calls it. A hardware READ BP on the relevant entry
// will catch the exact code path that dispatches the fire.
// Standard GF ordering: Quezacotl=0, Shiva=1, Ifrit=2, Siren=3, ...
static const uint32_t GF_EFFECT_TABLE_BASE = 0xC81774;
static const int GF_EFFECT_TABLE_ENTRIES = 16;
static const int GF_EFFECT_ENTRY_SIZE = 4;  // uint32 function pointers

// v0.10.88: Per-frame GF timer scan.
// Runs inside HookedATBUpdate (game thread) to catch the GF countdown decrement.
// Scans 0x01D76860-0x01D769DF as 2-byte values, logging any that decrement
// by 1-4 per call (matches SpeedMod 1-3 at ~60 calls/sec = ~15 game ticks/sec).
static const uint32_t GF_SCAN_BASE = 0x01D76860;
static const int GF_SCAN_BYTES = 384;  // covers 0x01D76860-0x01D769DF
static uint8_t s_gfScanSnap[384] = {};  // previous frame's bytes
static bool s_gfScanValid = false;      // true after first snapshot
static int s_gfScanLogCount = 0;        // limit logging
static const int GF_SCAN_LOG_MAX = 200; // max log lines per battle

// v0.10.65: Diagnostic counters for GF timer hook
static volatile LONG s_gfHookCallCount = 0;      // total calls to hooked function
static volatile LONG s_gfHookSkipCount = 0;      // calls skipped (cap active)
static volatile LONG s_gfHookPassCount = 0;      // calls passed through (cap inactive)
static DWORD s_gfHookLastLogTick = 0;

// GF state machine byte at 0x01D76868 controls GF readiness.
// Value 5 = "GF ready to fire". The battle loop reads this independently
// of the hooked function at 0x004B0500 and triggers the GF animation.
// We must clamp state68 to a non-fire value while capped.
static const uint32_t GF_STATE68_ADDR = 0x01D76868;
static const uint8_t  GF_STATE_FIRE   = 5;   // state value that triggers fire
static const uint8_t  GF_STATE_SAFE   = 3;   // safe "loading" state value
static uint8_t s_gfSavedState68 = 0xFF;       // real state68 value (saved when we clamp)
static bool s_gfState68Clamped = false;        // true while we're holding state68 at safe value

