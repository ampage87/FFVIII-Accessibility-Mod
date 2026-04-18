// battle_status.inl — Status ailment / buff detection + transition TTS.
// Included from battle_tts.cpp AFTER battle_tts_hp.inl (uses GetSlotName and
// the BATTLE_DAMAGE_ANIM_FLAG / s_pActiveCharId symbols from hp.inl).
// v0.13.62: Introduced for session 77 item 2.
// v0.13.63: Added animation-hold gating — transitions queue per slot and
//           flush when the damage animation flag clears (1→0), mirroring
//           the HP tracker's pattern. Fallback timeout covers buffs that
//           don't trigger the damage-number animation.
//
// Every frame, reads 5 status bytes (4 timed + 1 persist) for each of the 7
// battle slots, diffs them against a per-slot snapshot, and queues a short
// phrase on each bit flip. Phrases speak on:
//   (a) damage animation flag at 0x01D280C0 transitioning 1 → 0, or
//   (b) activeChar transitioning to a valid player slot (new-turn flush), or
//   (c) fallback timeout (STATUS_HOLD_TIMEOUT_MS) for buffs that don't
//       trigger the damage animation.
// Slot-0 baseline is captured at battle entry so pre-existing buffs don't
// spam on battle start. New-enemy slots that appear mid-battle are silently
// baselined on the first frame they read non-zero maxHP.

// ============================================================================
// Status table
// ============================================================================

struct StatusDef {
    uint8_t byteOffset;       // BENT_* offset within 0xD0 entity struct
    uint8_t mask;             // bit mask within that byte
    const char* shortName;    // log label
    const char* applyFmt;     // snprintf fmt with one %s for actor name
    const char* removeFmt;    // snprintf fmt with one %s for actor name
    bool announceTransition;  // false for KO (announced via HP tracker), and
                              // for Eject/Vit0 (unused in normal play)
};

// FF8 battle status layout (verified via existing BuildStatusString in hp.inl
// and FFNx community references). Bytes 0x00-0x03 hold timed statuses, byte
// 0x78 holds persistent statuses.
static const StatusDef STATUS_TABLE[] = {
    // --- Persistent (byte 0x78) ---
    { BENT_PERSIST_STATUS, 0x01, "KO",              "%s knocked out",          "%s revived",                  false },
    { BENT_PERSIST_STATUS, 0x02, "Poison",          "%s poisoned",             "%s no longer poisoned",       true  },
    { BENT_PERSIST_STATUS, 0x04, "Petrify",         "%s petrified",            "%s no longer petrified",      true  },
    { BENT_PERSIST_STATUS, 0x08, "Blind",           "%s blinded",              "%s no longer blinded",        true  },
    { BENT_PERSIST_STATUS, 0x10, "Silence",         "%s silenced",             "%s no longer silenced",       true  },
    { BENT_PERSIST_STATUS, 0x20, "Berserk",         "%s berserk",              "%s no longer berserk",        true  },
    { BENT_PERSIST_STATUS, 0x40, "Zombie",          "%s zombified",            "%s no longer zombified",      true  },

    // --- Timed byte 0 (0x00) ---
    { BENT_TIMED_STATUS_0, 0x01, "Sleep",           "%s asleep",               "%s awake",                    true  },
    { BENT_TIMED_STATUS_0, 0x02, "Haste",           "%s hasted",               "%s no longer hasted",         true  },
    { BENT_TIMED_STATUS_0, 0x04, "Slow",            "%s slowed",               "%s no longer slowed",         true  },
    { BENT_TIMED_STATUS_0, 0x08, "Stop",            "%s stopped",              "%s no longer stopped",        true  },
    { BENT_TIMED_STATUS_0, 0x10, "Regen",           "Regen on %s",             "Regen ended on %s",           true  },
    { BENT_TIMED_STATUS_0, 0x20, "Protect",         "Protect on %s",           "Protect ended on %s",         true  },
    { BENT_TIMED_STATUS_0, 0x40, "Shell",           "Shell on %s",             "Shell ended on %s",           true  },
    { BENT_TIMED_STATUS_0, 0x80, "Reflect",         "Reflect on %s",           "Reflect ended on %s",         true  },

    // --- Timed byte 1 (0x01) ---
    { BENT_TIMED_STATUS_1, 0x01, "Aura",            "Aura on %s",              "Aura ended on %s",            true  },
    { BENT_TIMED_STATUS_1, 0x02, "Curse",           "%s cursed",               "%s no longer cursed",         true  },
    { BENT_TIMED_STATUS_1, 0x04, "Doom",            "%s doomed",               "%s doom cancelled",           true  },
    { BENT_TIMED_STATUS_1, 0x08, "Invincible",      "%s invincible",           "Invincibility ended on %s",   true  },
    { BENT_TIMED_STATUS_1, 0x10, "Gradual Petrify", "%s turning to stone",     "Petrification halted on %s",  true  },
    { BENT_TIMED_STATUS_1, 0x20, "Float",           "Float on %s",             "Float ended on %s",           true  },
    { BENT_TIMED_STATUS_1, 0x40, "Confuse",         "%s confused",             "%s no longer confused",       true  },
    { BENT_TIMED_STATUS_1, 0x80, "Drain",           "%s drained",              "Drain ended on %s",           true  },

    // --- Timed byte 2 (0x02) ---
    { BENT_TIMED_STATUS_2, 0x01, "Eject",           "%s ejected",              "%s returned",                 false },
    { BENT_TIMED_STATUS_2, 0x02, "Double",          "Double on %s",            "Double ended on %s",          true  },
    { BENT_TIMED_STATUS_2, 0x04, "Triple",          "Triple on %s",            "Triple ended on %s",          true  },
    { BENT_TIMED_STATUS_2, 0x08, "Defend",          "%s defending",            "%s no longer defending",      true  },

    // --- Timed byte 3 (0x03) ---
    { BENT_TIMED_STATUS_3, 0x01, "Vit0",            "%s Vit0 on",              "%s Vit0 cleared",             false },
    { BENT_TIMED_STATUS_3, 0x02, "Angel Wing",      "Angel Wing on %s",        "Angel Wing ended on %s",      true  },
};
static const int STATUS_TABLE_SIZE = sizeof(STATUS_TABLE) / sizeof(STATUS_TABLE[0]);

// ============================================================================
// Per-slot snapshot (5 bytes: timed0..3 then persist)
// ============================================================================

static uint8_t s_statusPrev[BATTLE_TOTAL_SLOTS][5] = {};
static bool    s_statusSlotInit[BATTLE_TOTAL_SLOTS] = {};

// Map BENT_ byte offset -> 0..4 snapshot index.
static inline int StatusByteIndex(uint8_t bentOffset)
{
    switch (bentOffset) {
        case BENT_TIMED_STATUS_0: return 0;
        case BENT_TIMED_STATUS_1: return 1;
        case BENT_TIMED_STATUS_2: return 2;
        case BENT_TIMED_STATUS_3: return 3;
        case BENT_PERSIST_STATUS: return 4;
    }
    return -1;
}

// Read all 5 status bytes for a slot. Returns false on exception / bad slot.
static bool ReadStatusBytes(int slot, uint8_t out[5])
{
    uint8_t* blk = GetEntityBlock(slot);
    if (!blk) return false;
    __try {
        out[0] = *(blk + BENT_TIMED_STATUS_0);
        out[1] = *(blk + BENT_TIMED_STATUS_1);
        out[2] = *(blk + BENT_TIMED_STATUS_2);
        out[3] = *(blk + BENT_TIMED_STATUS_3);
        out[4] = *(blk + BENT_PERSIST_STATUS);
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}

// ============================================================================
// v0.13.63: Pending transition queue + animation-hold flush
// ============================================================================

struct StatusPending {
    uint8_t statusIdx;  // index into STATUS_TABLE
    bool    isOn;       // true = apply, false = remove
};

static const int STATUS_QUEUE_CAPACITY = 8;  // per slot; more than enough per frame
static StatusPending s_statusQueue[BATTLE_TOTAL_SLOTS][STATUS_QUEUE_CAPACITY] = {};
static int           s_statusQueueCount[BATTLE_TOTAL_SLOTS] = {};
static bool          s_statusAnyPending = false;
static DWORD         s_statusFirstPendingTick = 0;

// Mirror the HP tracker's view of the damage animation flag. When the flag
// transitions 1 → 0, the on-screen damage number animation has finished and
// any pending status changes that landed with that attack should fire.
static bool s_statusAnimWasActive = false;

// For new-turn flush (copy of hp.inl pattern).
static uint8_t s_statusLastActiveChar = 0xFF;

// Safety timeout: buffs like Aura / Protect may not trigger the damage
// animation flag at all. If a transition has been pending for this long
// without a flag transition, flush anyway.
static const DWORD STATUS_HOLD_TIMEOUT_MS = 1500;

// Enqueue a single transition for a slot. Silently drops if the queue is
// full (shouldn't happen in practice — we've got room for 8 flips per
// slot per flush window).
static void EnqueueStatusTransition(int slot, int statusIdx, bool isOn)
{
    if (slot < 0 || slot >= BATTLE_TOTAL_SLOTS) return;
    int n = s_statusQueueCount[slot];
    if (n >= STATUS_QUEUE_CAPACITY) {
        Log::Battle("BattleTTS: [STATUS-Q] slot%d queue FULL, dropping %s %s",
                   slot, isOn ? "+" : "-", STATUS_TABLE[statusIdx].shortName);
        return;
    }
    s_statusQueue[slot][n].statusIdx = (uint8_t)statusIdx;
    s_statusQueue[slot][n].isOn = isOn;
    s_statusQueueCount[slot] = n + 1;

    if (!s_statusAnyPending) {
        s_statusAnyPending = true;
        s_statusFirstPendingTick = GetTickCount();
    }
    Log::Battle("BattleTTS: [STATUS-Q] slot%d queued %s %s (queue=%d)",
               slot, isOn ? "+" : "-", STATUS_TABLE[statusIdx].shortName, n + 1);
}

// Drain all pending transitions, speaking each as the proper apply/remove
// phrase. Called on: damage anim 1→0, new-turn transition, or timeout.
static void FlushStatusAnnouncements(const char* trigger)
{
    if (!s_statusAnyPending) return;

    char nameBuf[64];
    char phrase[160];

    for (int slot = 0; slot < BATTLE_TOTAL_SLOTS; slot++) {
        int n = s_statusQueueCount[slot];
        if (n == 0) continue;

        const char* name = GetSlotName(slot, nameBuf, sizeof(nameBuf));
        for (int i = 0; i < n; i++) {
            const StatusPending& p = s_statusQueue[slot][i];
            const StatusDef& def = STATUS_TABLE[p.statusIdx];
            const char* fmt = p.isOn ? def.applyFmt : def.removeFmt;
            snprintf(phrase, sizeof(phrase), fmt, name);
            BattleSpeak(phrase, PRIO_STATUS, false);
            Log::Battle("BattleTTS: [STATUS] slot%d %s %s: %s (trigger=%s)",
                       slot, p.isOn ? "+" : "-", def.shortName, phrase, trigger);
        }
        s_statusQueueCount[slot] = 0;
    }
    s_statusAnyPending = false;
    s_statusFirstPendingTick = 0;
}

// ============================================================================
// Baseline + per-frame poll
// ============================================================================

// Capture baselines at battle entry. Does NOT announce anything.
// Slots with maxHP==0 (not yet populated) are left un-init'd and will be
// baselined silently by PollStatusChanges when they go live.
static void InitStatusBaseline()
{
    memset(s_statusPrev, 0, sizeof(s_statusPrev));
    memset(s_statusSlotInit, 0, sizeof(s_statusSlotInit));
    memset(s_statusQueueCount, 0, sizeof(s_statusQueueCount));
    s_statusAnyPending = false;
    s_statusFirstPendingTick = 0;
    s_statusAnimWasActive = false;
    s_statusLastActiveChar = 0xFF;

    for (int slot = 0; slot < BATTLE_TOTAL_SLOTS; slot++) {
        if (GetEntityMaxHP(slot) == 0) continue;
        uint8_t bytes[5];
        if (!ReadStatusBytes(slot, bytes)) continue;
        memcpy(s_statusPrev[slot], bytes, 5);
        s_statusSlotInit[slot] = true;

        Log::Battle("BattleTTS: [STATUS-BASE] slot%d timed=[%02X %02X %02X %02X] persist=%02X",
                   slot, bytes[0], bytes[1], bytes[2], bytes[3], bytes[4]);
    }
}

// Per-frame poll. Detects transitions and queues them. Flushes on:
//   - damage animation flag 1→0 (primary trigger, sync with on-screen number)
//   - new-turn transition (activeChar 0xFF → 0-2, previous anim is finished)
//   - fallback timeout (buffs with no damage animation)
static void PollStatusChanges()
{
    // --- Step 1: Detect transitions, queue them ---
    for (int slot = 0; slot < BATTLE_TOTAL_SLOTS; slot++) {
        if (GetEntityMaxHP(slot) == 0) {
            // Empty — mark un-init'd so next populated frame baselines silently
            s_statusSlotInit[slot] = false;
            // Also drop any queued transitions for a slot that just vacated
            // (e.g. KO'd enemy despawned mid-battle) so we don't announce
            // stale changes against a nonexistent actor.
            s_statusQueueCount[slot] = 0;
            continue;
        }

        uint8_t cur[5];
        if (!ReadStatusBytes(slot, cur)) continue;

        if (!s_statusSlotInit[slot]) {
            // Mid-battle population (e.g. Elvoret after Biggs/Wedge dies).
            // Snapshot without announcing.
            memcpy(s_statusPrev[slot], cur, 5);
            s_statusSlotInit[slot] = true;
            Log::Battle("BattleTTS: [STATUS-BASE] slot%d (mid-battle) timed=[%02X %02X %02X %02X] persist=%02X",
                       slot, cur[0], cur[1], cur[2], cur[3], cur[4]);
            continue;
        }

        // Diff each tracked status bit against previous snapshot
        for (int si = 0; si < STATUS_TABLE_SIZE; si++) {
            const StatusDef& def = STATUS_TABLE[si];
            if (!def.announceTransition) continue;

            int byteIdx = StatusByteIndex(def.byteOffset);
            if (byteIdx < 0) continue;

            bool wasOn = (s_statusPrev[slot][byteIdx] & def.mask) != 0;
            bool isOn  = (cur[byteIdx] & def.mask) != 0;
            if (wasOn == isOn) continue;

            EnqueueStatusTransition(slot, si, isOn);
        }

        // Update snapshot immediately so we don't re-queue the same flip
        memcpy(s_statusPrev[slot], cur, 5);
    }

    // Recount pending after dropping any empty-slot queues above
    if (s_statusAnyPending) {
        bool stillPending = false;
        for (int i = 0; i < BATTLE_TOTAL_SLOTS; i++) {
            if (s_statusQueueCount[i] > 0) { stillPending = true; break; }
        }
        if (!stillPending) {
            s_statusAnyPending = false;
            s_statusFirstPendingTick = 0;
        }
    }

    if (!s_statusAnyPending) {
        // No pending transitions — reset anim tracking so the next flip starts fresh
        s_statusAnimWasActive = false;
        // Still track activeChar so the next new-turn transition fires correctly
        if (s_pActiveCharId) {
            __try { s_statusLastActiveChar = *s_pActiveCharId; }
            __except(EXCEPTION_EXECUTE_HANDLER) {}
        }
        return;
    }

    DWORD now = GetTickCount();

    // --- Step 2: New-turn flush ---
    // When activeChar transitions TO a valid player slot (0-2), the previous
    // action's animation is guaranteed complete. Matches the hp.inl pattern.
    {
        uint8_t curActiveChar = 0xFF;
        if (s_pActiveCharId) {
            __try { curActiveChar = *s_pActiveCharId; }
            __except(EXCEPTION_EXECUTE_HANDLER) {}
        }
        if (curActiveChar != s_statusLastActiveChar) {
            bool flushNow = (curActiveChar < 3 && s_statusLastActiveChar != curActiveChar);
            if (flushNow) {
                Log::Battle("BattleTTS: [STATUS] New-turn flush (char %u->%u)",
                           (unsigned)s_statusLastActiveChar, (unsigned)curActiveChar);
                s_statusAnimWasActive = false;
                FlushStatusAnnouncements("new-turn");
            }
            s_statusLastActiveChar = curActiveChar;
            if (!s_statusAnyPending) return;  // flushed, nothing more to do
        }
    }

    // --- Step 3: Damage animation flag 1→0 flush ---
    // Reads the same byte the HP tracker watches at 0x01D280C0.
    uint8_t animFlag = 0;
    __try { animFlag = *(uint8_t*)BATTLE_DAMAGE_ANIM_FLAG; }
    __except(EXCEPTION_EXECUTE_HANDLER) {}

    if (animFlag != 0 && !s_statusAnimWasActive) {
        s_statusAnimWasActive = true;
        Log::Battle("BattleTTS: [STATUS] Anim flag active, holding %d slots with pending",
                   s_statusAnyPending ? 1 : 0);
    }

    if (animFlag == 0 && s_statusAnimWasActive) {
        // Animation just finished — fire queued announcements
        s_statusAnimWasActive = false;
        Log::Battle("BattleTTS: [STATUS] Anim flag cleared, flushing pending");
        FlushStatusAnnouncements("anim-done");
        return;
    }

    // --- Step 4: Fallback timeout ---
    // Buffs like Aura / Protect / etc. may not trigger the damage animation
    // flag. Flush after STATUS_HOLD_TIMEOUT_MS to avoid indefinite deferrals.
    if (s_statusAnyPending &&
        (now - s_statusFirstPendingTick >= STATUS_HOLD_TIMEOUT_MS) &&
        animFlag == 0) {
        Log::Battle("BattleTTS: [STATUS] Hold timeout (%ums), flushing pending",
                   (unsigned)(now - s_statusFirstPendingTick));
        s_statusAnimWasActive = false;
        FlushStatusAnnouncements("timeout");
    }
}
