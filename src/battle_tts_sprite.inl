// battle_tts_sprite.inl — Battle event sprite hook for Miss/Hit detection
//
// Included by battle_tts.cpp AFTER battle_tts_menu.inl (depends on
// GetBattleCharName from menu.inl and GetEnemyName from helpers.inl).
//
// v0.13.64 (session 77 item 5 research + speculative implementation):
//
// Hooks sub_483400 — the battle event sprite text spawner. Every time a
// battle result renders a sprite over an entity (Miss, damage number,
// status change, critical, immune, etc.), it goes through this function.
//
// Signature (verified from disassembly): sub_483400(slot, text_id, extra, value)
//   - slot:     target entity slot (0..BATTLE_TOTAL_SLOTS)
//   - text_id:  battle text table ID (what sprite/text to show)
//   - extra:    secondary numeric param (context-dependent)
//   - value:    tertiary numeric param
//
// Static analysis in session 77 identified this text ID cluster:
//   0xED (237) — miss/no-effect (zero-damage branch of sub_485220)
//                CANDIDATE for Miss sprite
//   0xEE (238) — hit/effect (non-zero damage branch of sub_485220)
//   0xEF (239) — unknown (at sub_4853C2, with ecx param)
//   0xF0 (240) — damage/numeric display (via sub_482E60 wrapper, 3-arg)
//   0xF1 (241) — signed-value stat change (uses 0xFFFC = -4 constant)
//
// This build does BOTH diagnostic logging AND speculative announcement:
//   - Logs every (slot, text_id) pair the first time it fires per battle
//     with tag [SPRITE] — reveals the real IDs for Miss/hit/crit/etc.
//   - Announces "Miss on {target}" when text_id == 0xED fires.
//
// If 0xED is correct, the BAT confirms it and we keep as-is.
// If 0xED is wrong, the [SPRITE] log shows the correct ID from the first
// missed attack, and v0.13.65 patches the constant.
//
// Testing hint: easiest way to force a miss is to cast Blind on an enemy
// and let them attack you. Physical attacks from blinded enemies miss ~80%.

// ============================================================================
// Hook state
// ============================================================================

// Forward declaration from battle_tts_screenshot.inl (which is #included into
// battle_tts.cpp AFTER this file). Used by v0.13.69's PollKind4Capture.
static void CaptureScreenshot(const char* basePath);

// Forward declarations for v0.13.69 kind=4 screenshot subsystem (defined
// lower in this file, but called from HookedSpellResultDispatch above them).
static void SchedulePendingKind4Capture(uint32_t slot, uint32_t a3, bool isAnnounceBranch);
static void PollKind4Capture();

typedef uint32_t (__cdecl *SpriteSpawnFunc_t)(uint32_t, uint32_t, uint32_t, uint32_t);
static SpriteSpawnFunc_t s_origSpriteSpawn = nullptr;
static bool s_spriteSpawnHookInstalled = false;
static const uint32_t SPRITE_SPAWN_ADDR = 0x00483400;

// Per-slot Miss dedup — prevents flooding if the sprite re-fires across
// multiple frames for the same event. 500ms window is well below any
// reasonable attack cadence but long enough to suppress multi-frame spam.
static DWORD s_lastMissAnnounceTick[BATTLE_TOTAL_SLOTS] = {};
static const DWORD MISS_ANNOUNCE_DEDUP_MS = 500;

// v0.14.55: Scan-cast detection at the action layer. The popup hook
// (HookedPopupSprite, sub_48D200) fires for every battle popup sprite,
// including the spell-name popup that spawns at action-commit time. For
// Scan specifically, the popup arrives with text_id=0x02 and value=0x32
// (the latter is the spell ID 50 = Scan). This fires for ALL Scan casts
// regardless of view path — critically, including the compacted view
// shown on repeat Scans of the same target in a battle, which v0.14.54
// BAT proved skips both sub_B687C0 AND sub_84F860 entirely. We capture
// the tick when the Scan-name popup fires; battle_tts_noeffect.inl reads
// this in NoEffect_RecordSnapshot to short-circuit the watchdog (Scan
// produces no observable change) and invoke ScanTTS::OnScanCast directly
// for the announcement. Because the popup hook fires BEFORE sub_48E830
// (the action staging that records the watchdog snapshot), the tick is
// always fresh by the time NoEffect_RecordSnapshot reads it.
//
// v0.14.76: text_id constant CORRECTED from 0x06 to 0x02. v0.14.55's
// 0x06 was a transcription error (likely confused with the kind=0x06
// reference in screenshot.inl's old screen-close comment). Empirical
// evidence from v0.14.75 BAT log: every Scan cast produces a sub_48D200
// popup with text_id=0x02 and value=0x32; text_id=0x06 NEVER fires for
// any popup (zero matches across the entire log). The wrong constant
// meant s_lastScanCastTick was never set, NoEffect_RecordSnapshot's
// scan-detection branch never fired, and the action-layer ScanTTS
// detection path was completely dead. PollBattleMagicId's transition-
// based fallback caught FIRST scans in a battle (when battle_magic_id
// transitioned 0→39) but missed REPEAT scans (battle_magic_id stays at
// 39 across multiple casts in the same battle, no transition to detect).
// Result: a repeat Scan in a battle produced NO Scan announcement AND
// a spurious 'No effect on <target>' from the watchdog 10 seconds later.
// With the correct text_id, the popup hook reliably catches every Scan
// cast (first or repeat) and the action-layer path becomes the primary
// detection mechanism — PollBattleMagicId's Scan branch becomes a
// no-op fallback for first-scans only.
static volatile LONG s_lastScanCastTick = 0;
static const DWORD   SCAN_CAST_RECENT_MS = 1000;

// Per-battle diagnostic dedup — log each distinct (slot, text_id) pair
// only the first time it fires. Keeps the log readable while still
// revealing every new ID as it appears.
static const int SPRITE_LOG_DEDUP_MAX = 32;
static struct { uint32_t slot; uint32_t text_id; } s_spriteLogDedup[SPRITE_LOG_DEDUP_MAX] = {};
static int s_spriteLogDedupCount = 0;

// ============================================================================
// Hook function
// ============================================================================

static uint32_t __cdecl HookedSpriteSpawn(uint32_t target_slot, uint32_t text_id,
                                           uint32_t extra, uint32_t value)
{
    // Always forward to the real function first — never interfere with
    // the engine's sprite pipeline.
    uint32_t result = s_origSpriteSpawn(target_slot, text_id, extra, value);

    __try {
        uint16_t mode = 0;
        if (FF8Addresses::pGameMode) {
            __try { mode = *FF8Addresses::pGameMode; } __except(EXCEPTION_EXECUTE_HANDLER) {}
        }

        if (mode != 3) {
            // Only active during battle — victory/menu sprite calls are
            // out of scope for this hook.
            return result;
        }

        // --- Diagnostic log only (v0.13.66) ---
        // Miss announcement is now handled by the sub_48D200 hook below,
        // which is the central popup sprite dispatcher and catches the
        // same event regardless of whether it came through the item or
        // spell path. Keeping this hook as a pure diagnostic lets us still
        // see item-specific spawn events if they ever diverge from the
        // universal path.
        bool alreadyLogged = false;
        for (int i = 0; i < s_spriteLogDedupCount; i++) {
            if (s_spriteLogDedup[i].slot == target_slot &&
                s_spriteLogDedup[i].text_id == text_id) {
                alreadyLogged = true;
                break;
            }
        }
        if (!alreadyLogged && s_spriteLogDedupCount < SPRITE_LOG_DEDUP_MAX) {
            s_spriteLogDedup[s_spriteLogDedupCount].slot = target_slot;
            s_spriteLogDedup[s_spriteLogDedupCount].text_id = text_id;
            s_spriteLogDedupCount++;
            Log::Battle("BattleTTS: [SPRITE] slot=%u text_id=0x%X(%u) extra=0x%X value=0x%X",
                        target_slot, text_id, text_id, extra, value);
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        Log::Battle("BattleTTS: [SPRITE] EXCEPTION in hook");
    }

    return result;
}

// ============================================================================
// Install / reset
// ============================================================================

static void InstallSpriteSpawnHook()
{
    if (s_spriteSpawnHookInstalled) return;

    __try {
        uint8_t* p = (uint8_t*)SPRITE_SPAWN_ADDR;
        char hx[50] = {};
        int hp = 0;
        for (int b = 0; b < 8; b++)
            hp += snprintf(hx + hp, sizeof(hx) - hp, "%02X ", p[b]);
        Log::Battle("BattleTTS: [SPRITE-HOOK] sub_483400 @ 0x%08X: %s",
                    SPRITE_SPAWN_ADDR, hx);
    } __except(EXCEPTION_EXECUTE_HANDLER) {}

    MH_STATUS st = MH_CreateHook((void*)SPRITE_SPAWN_ADDR,
                                  (void*)&HookedSpriteSpawn,
                                  (void**)&s_origSpriteSpawn);
    if (st != MH_OK) {
        Log::Battle("BattleTTS: [SPRITE-HOOK] MH_CreateHook FAILED: %d", (int)st);
        return;
    }
    st = MH_EnableHook((void*)SPRITE_SPAWN_ADDR);
    if (st != MH_OK) {
        Log::Battle("BattleTTS: [SPRITE-HOOK] MH_EnableHook FAILED: %d", (int)st);
        return;
    }
    s_spriteSpawnHookInstalled = true;
    Log::Battle("BattleTTS: [SPRITE-HOOK] Installed at 0x%08X (sub_483400)",
                SPRITE_SPAWN_ADDR);
}

// ============================================================================
// v0.13.65: Spell result dispatcher hook (sub_4877F0)
// ============================================================================
//
// Complementary to sub_483400. BAT of v0.13.64 with a Sleep-on-already-asleep
// test showed that sub_483400 never fired during spell casts — that function
// is the ITEM event sprite pipeline, not the spell pipeline.
//
// Session 78 static analysis identified sub_4877F0 as the SPELL result
// dispatcher entry point. It takes (slot, result_kind) and has a 9-case
// jump table at 0x487D58 that branches on result_kind. Each case corresponds
// to a different spell outcome (hit / miss / immune / reflected / absorbed /
// etc.) and ends up setting the damage-anim flag and typically calling
// sub_487DF0 (the spell sprite spawner).
//
// v0.13.65: diagnostic only. Logs every (slot, result_kind) pair during
// battle mode. One BAT comparing a successful cast vs a no-effect cast
// will reveal which result_kind value(s) correspond to "miss" / "no effect".
// Once identified, v0.13.66 wires the announcement.
//
// ----------------------------------------------------------------------------
// v0.13.67: kind=4 = Miss/resist — DISASSEMBLY-CONFIRMED.
// ----------------------------------------------------------------------------
//
// Session 77 completed a full read of sub_4877F0's 9-case jump table via
// chained filesystem:edit_file dryRun calls (see DEVNOTES for the technique).
// Complete kind-to-semantics map:
//
//   kind=0 (0x0048785A) — action setup: sub_47E080(5) helper + load state.
//   kind=1 (0x00487886) — action mid-calc: same prologue as kind=0.
//   kind=2 (0x004878C0) — hit confirmed: complex damage/status branching,
//                         eventually pushes damage number sprite (0xF0) via
//                         the common site at 0x487D42 → sub_483400.
//   kind=3 (0x00487B90) — calls sub_482E60(slot, 0xD, slot_mask|0x4000)
//                         — specific event type 0xD.
//   kind=4 (0x00487BF3) — UNIQUE: skips the flag-check prologue AND the
//                         sub_47E080(5) helper call. Only case that routes
//                         to sub_487DF0 directly with minimal args.
//                         Generic "spawn feedback sprite" path — used by
//                         BOTH normal attacks AND resists. The body reads
//                         only ebp (enemy struct) and slot; it does NOT
//                         read a3. The outcome meaning is carried by the
//                         caller via a3 (see v0.13.68 note below).
//                         BAT observations:
//                           a3=0x0 — normal physical attack (NOT a miss);
//                                    false positive if announced.
//                           a3=0x9 — Sleep-on-sleeping & Silence-on-immune;
//                                    genuine no-effect.
//                         v0.13.68 gates announcement on (a3 & 0x8) != 0.
//   kind=5 (0x00487C0D) — helper + push(0xF6, 0x2B, slot_mask) → sub_483400.
//   kind=6 (0x00487C37) — helper + push(0, 4, slot_mask) → sub_483400.
//   kind=7 (0x00487C5E) — helper + conditional on global 0x1d28e14 byte.
//   kind=8 (0x00487D22) — helper + reads last-damage globals → sub_483400
//                         with 0xF0 (damage number sprite).
//
// Dispatcher at 0x00487853: cmp edx, 8 / ja 0x487d52 / jmp [edx*4 + 0x487d58]
// — kinds > 8 short-circuit to the common exit without sprite spawn.
//
// Conclusion: announce "Miss on {target}" when kind=4 fires. Complements the
// sub_48D200 text_id=0xED path (which catches physical-attack Miss via a
// different pipeline). The two announcements use independent per-slot dedup
// so they never silence each other.

typedef uint32_t (__cdecl *SpellResultFunc_t)(uint32_t, uint32_t, uint32_t, uint32_t);
static SpellResultFunc_t s_origSpellResult = nullptr;
static bool s_spellResultHookInstalled = false;
static const uint32_t SPELL_RESULT_ADDR = 0x004877F0;

// Per-battle dedup for spell result logging
static const int SPELL_LOG_DEDUP_MAX = 16;
static struct { uint32_t slot; uint32_t kind; } s_spellLogDedup[SPELL_LOG_DEDUP_MAX] = {};
static int s_spellLogDedupCount = 0;

// v0.13.67: per-slot Miss/resist announcement dedup for kind=4 path
static DWORD s_lastSpellMissAnnounceTick[BATTLE_TOTAL_SLOTS] = {};
static const DWORD SPELL_MISS_DEDUP_MS = 500;

static uint32_t __cdecl HookedSpellResultDispatch(uint32_t slot, uint32_t result_kind,
                                                    uint32_t a3, uint32_t a4)
{
    // Always forward first — never interfere with the engine's spell pipeline.
    uint32_t result = s_origSpellResult(slot, result_kind, a3, a4);

    __try {
        uint16_t mode = 0;
        if (FF8Addresses::pGameMode) {
            __try { mode = *FF8Addresses::pGameMode; } __except(EXCEPTION_EXECUTE_HANDLER) {}
        }

        if (mode != 3) return result;

        // Dedup log: first occurrence of each (slot, kind) per battle
        bool alreadyLogged = false;
        for (int i = 0; i < s_spellLogDedupCount; i++) {
            if (s_spellLogDedup[i].slot == slot &&
                s_spellLogDedup[i].kind == result_kind) {
                alreadyLogged = true;
                break;
            }
        }
        if (!alreadyLogged && s_spellLogDedupCount < SPELL_LOG_DEDUP_MAX) {
            s_spellLogDedup[s_spellLogDedupCount].slot = slot;
            s_spellLogDedup[s_spellLogDedupCount].kind = result_kind;
            s_spellLogDedupCount++;
            Log::Battle("BattleTTS: [SPELL-RESULT] slot=%u kind=%u(0x%X) a3=0x%X a4=0x%X",
                        slot, result_kind, result_kind, a3, a4);
        }

        // v0.13.67 → v0.13.68 → v0.13.70: kind=4 gated on upper-nibble-zero
        // AND bit 3 set.
        //
        // Evolution of the filter:
        //
        //   v0.13.67 — `result_kind == 4` alone. BAT revealed kind=4 fires for
        //   normal physical attacks too, not just resists.
        //
        //   v0.13.68 — added `(a3 & 0x8) != 0`. Observed data: a3=0x0 for
        //   physical attacks (skipped correctly), a3=0x9 for Silence/Sleep
        //   resist (announced correctly).
        //
        //   v0.13.69 — added auto-screenshot diagnostic to visually confirm
        //   what the engine renders for each kind=4 event.
        //
        //   v0.13.70 — screenshot BAT revealed a3=0xFD (bit 3 set!) fires for
        //   damage hits from limit break / Strike Raid attacks, with the
        //   engine clearly rendering a damage NUMBER (106, 114) on screen.
        //   So `(a3 & 0x8) != 0` was still letting false positives through.
        //   The discriminator is the UPPER NIBBLE of a3:
        //     a3=0x09 (upper=0) — real resist/no-effect
        //     a3=0xFD (upper=0xF) — damage hit
        //   Tighter filter: (a3 & 0xF0) == 0 && (a3 & 0x8) != 0.
        //
        // All observed a3 values through v0.13.70:
        //   a3=0x00 — physical attack (SKIP: bit 3 clear)
        //   a3=0x06 — target selection menu context (SKIP: bit 3 clear)
        //   a3=0x09 — Silence-immune / Sleep-on-sleeping (ANNOUNCE: real miss)
        //   a3=0xFD — Strike Raid / limit break damage hit (SKIP: upper nibble set)
        //
        //   v0.18.3.236 (#74) — a3=0x08 observed in Aaron's 2026-07-12 Ifrit
        //   fight firing for ordinary Blizzard DAMAGE hits (engine rendered a
        //   damage number, HP dropped, damage TTS spoke). The old
        //   upper-nibble-zero + bit3 filter accepted 0x08 and announced a
        //   false "No effect on Ifrit". a3=0x09 remains the ONLY value ever
        //   BAT-confirmed as a genuine resist/no-effect, so the filter is now
        //   exact-match. Any new bit3-set value logs via [SPELL-MISS-SKIP] +
        //   screenshot capture for review instead of announcing.
        //
        // Keep the screenshot diagnostic enabled so any new a3 pattern is
        // captured for review. Also keep [SPELL-MISS-SKIP] logging for any
        // SKIP branch cases so we can spot new patterns in the log.
        if (result_kind == 4 && slot < BATTLE_TOTAL_SLOTS) {
            bool isResist = (a3 == 0x9);  // v0.18.3.236 (#74): exact-match, was upper-nibble-zero + bit3

            // v0.13.69: schedule an auto-screenshot for BOTH branches so we
            // can visually confirm what the engine renders. Filename encodes
            // which branch it belonged to.
            SchedulePendingKind4Capture(slot, a3, isResist);

            if (!isResist) {
                // kind=4 fired but bit 3 clear — this is the normal
                // sprite-setup path (physical attacks, etc.), NOT a miss.
                // Log once per (slot, a3) per battle so we can audit edge
                // cases without flooding the log.
                static const int SKIP_LOG_DEDUP_MAX = 16;
                static struct { uint32_t slot; uint32_t a3; } s_skipLogDedup[SKIP_LOG_DEDUP_MAX] = {};
                static int s_skipLogDedupCount = 0;
                bool alreadySkipLogged = false;
                for (int i = 0; i < s_skipLogDedupCount; i++) {
                    if (s_skipLogDedup[i].slot == slot && s_skipLogDedup[i].a3 == a3) {
                        alreadySkipLogged = true;
                        break;
                    }
                }
                if (!alreadySkipLogged && s_skipLogDedupCount < SKIP_LOG_DEDUP_MAX) {
                    s_skipLogDedup[s_skipLogDedupCount].slot = slot;
                    s_skipLogDedup[s_skipLogDedupCount].a3 = a3;
                    s_skipLogDedupCount++;
                    // v0.18.3.236 (#74): wording updated — the filter is now
                    // exact a3==0x9, so skipped values include bit3-set ones
                    // like 0x8 (ordinary elemental damage).
                    Log::Battle("BattleTTS: [SPELL-MISS-SKIP] slot=%u kind=4 a3=0x%X "
                                "(not 0x9 — not a miss; normal sprite path)",
                                slot, a3);
                }
            } else {
                // a3 bit 3 set — genuine no-effect/resist. Announce.
                //
                // v0.13.82: Say "No effect on X" rather than "Miss on X".
                // BAT5/BAT6 proved this code path fires for cases like
                // Sleep-on-already-asleep, Silence-on-immune, etc. — where
                // the engine actually renders NOTHING on the target
                // (unlike a physical Miss, which writes text_id=0xED into
                // an existing popup record and shows "Miss" text on screen).
                // "No effect" matches what a sighted player experiences:
                // the spell completes its cast animation and nothing
                // happens to the target. Physical-miss announcements from
                // HookedPopupSprite still say "Miss" since the engine
                // genuinely shows Miss text for those.
                DWORD now = GetTickCount();
                if (now - s_lastSpellMissAnnounceTick[slot] > SPELL_MISS_DEDUP_MS) {
                    s_lastSpellMissAnnounceTick[slot] = now;

                    char name[64] = {};
                    if (slot < BATTLE_ALLY_SLOTS) {
                        const char* charName = GetBattleCharName((int)slot);
                        if (charName) strncpy(name, charName, sizeof(name) - 1);
                    } else {
                        GetEnemyName((int)slot, name, sizeof(name));
                    }

                    char buf[128];
                    if (name[0])
                        snprintf(buf, sizeof(buf), "No effect on %s.", name);
                    else
                        snprintf(buf, sizeof(buf), "No effect.");

                    // v0.13.79: Validate BEFORE speaking. Non-blocking
                    // enqueue — safe to call from game-thread hook body.
                    // v0.13.82: kind changed to "no-effect" to match TTS.
                    // v0.13.89: Route through NoEffect_QueueAnnouncement
                    // instead of speaking directly. The kind=4 hook fires
                    // at engine-decision time (BEFORE the cast animation
                    // begins) — speaking immediately means the player hears
                    // "No effect on X" before any visible effect on screen.
                    // The queue holds the announcement until the damage
                    // anim flag transitions 1->0 (animation finished),
                    // matching the proven status-apply timing pattern.
                    NoEffect_QueueAnnouncement((int)slot, 0, buf, "no-effect");
                    Log::Battle("BattleTTS: [SPELL-NOEFFECT] %s queued (slot=%u kind=4 a3=0x%X)",
                                buf, slot, a3);
                }
            }
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        Log::Battle("BattleTTS: [SPELL-RESULT] EXCEPTION in hook");
    }

    return result;
}

static void InstallSpellResultHook()
{
    if (s_spellResultHookInstalled) return;

    __try {
        uint8_t* p = (uint8_t*)SPELL_RESULT_ADDR;
        char hx[50] = {};
        int hp = 0;
        for (int b = 0; b < 8; b++)
            hp += snprintf(hx + hp, sizeof(hx) - hp, "%02X ", p[b]);
        Log::Battle("BattleTTS: [SPELL-HOOK] sub_4877F0 @ 0x%08X: %s",
                    SPELL_RESULT_ADDR, hx);
    } __except(EXCEPTION_EXECUTE_HANDLER) {}

    MH_STATUS st = MH_CreateHook((void*)SPELL_RESULT_ADDR,
                                  (void*)&HookedSpellResultDispatch,
                                  (void**)&s_origSpellResult);
    if (st != MH_OK) {
        Log::Battle("BattleTTS: [SPELL-HOOK] MH_CreateHook FAILED: %d", (int)st);
        return;
    }
    st = MH_EnableHook((void*)SPELL_RESULT_ADDR);
    if (st != MH_OK) {
        Log::Battle("BattleTTS: [SPELL-HOOK] MH_EnableHook FAILED: %d", (int)st);
        return;
    }
    s_spellResultHookInstalled = true;
    Log::Battle("BattleTTS: [SPELL-HOOK] Installed at 0x%08X (sub_4877F0)",
                SPELL_RESULT_ADDR);
}

// Reset per-battle state (called from OnBattleEnter).
// Declared forward here, defined at the bottom of this file once all the
// per-hook static variables (from v0.13.64, .65, .66) are in scope.
static void ResetSpriteSpawnState();

// ============================================================================
// v0.13.66: Central battle popup sprite dispatcher hook (sub_48D200)
// ============================================================================
//
// sub_48D200 is the single convergence point for every battle popup sprite
// in the game. Static analysis proved:
//
//   - 12 callers across the battle code range (0x483xxx..0x48Axxx)
//   - Both item path (sub_483400 → sub_48D200) and spell path
//     (sub_4877F0 → sub_487DF0 → sub_48D200) funnel through it
//   - Has a jump table at 0x48E420 indexed by (text_id - 2), bounds-checked
//     against 0xFC — handles text IDs 2 through 0xFE
//   - Writes to the damage-animation slot tracking at 0x1D280C0/C4
//
// __cdecl signature (7 dword args, caller cleanup 0x1C bytes):
//   sub_48D200(int slot,        // arg1 — target slot (0..6)
//              int text_id,     // arg2 — popup sprite type (low byte used)
//              int value,       // arg3 — primary value (damage amount, etc.)
//              int extra1,      // arg4
//              int slot_dup,    // arg5 — often same as arg1
//              int extra2,      // arg6
//              int reserved)    // arg7
//
// Known text_ids from static analysis (session 77):
//   0xED (237) — Miss
//   0xEE (238) — Hit (may be generic contact, may not need its own announce)
//   0xEF (239) — unknown (related to stat modification path)
//   0xF0 (240) — damage amount
//   0xF1 (241) — stat change
//   Others (0x02..0xEC, 0xF2..0xFE) — to be catalogued from BAT logs
//
// v0.13.66 ships only the Miss announcement. All other text_ids are logged
// (first occurrence per (slot, text_id) per battle) so subsequent sessions
// can identify them and add announcements incrementally.

typedef uint32_t (__cdecl *PopupSpriteFunc_t)(uint32_t, uint32_t, uint32_t, uint32_t,
                                                uint32_t, uint32_t, uint32_t);
static PopupSpriteFunc_t s_origPopupSprite = nullptr;
static bool s_popupSpriteHookInstalled = false;
static const uint32_t POPUP_SPRITE_ADDR = 0x0048D200;

// v0.37.1 (#95): text_id 0xED is the limit-break TRIGGER popup, not the miss
// sprite v0.13.71 took it for. Named here so the next reader meets the evidence
// (the block in the popup hook below) before the number.
static const uint8_t POPUP_TID_LIMIT_SHOT = 0xED;
static DWORD s_lastLimitShotLogTick = 0;

// Per-battle diagnostic dedup for text_ids.
// v0.13.71: expanded to include caller return address so we can distinguish
// which call site originated each sprite (e.g. sub_487DF0's feedback path at
// 0x004881D8 vs other upstream callers). This is the key diagnostic for
// identifying which (retaddr, text_id) combo fires at the actual sprite
// display moment — independent of the variable-timing kind=4 dispatch above.
static const int POPUP_LOG_DEDUP_MAX = 256;
static struct { uint32_t retaddr; uint32_t slot; uint32_t text_id; } s_popupLogDedup[POPUP_LOG_DEDUP_MAX] = {};
static int s_popupLogDedupCount = 0;

static uint32_t __cdecl HookedPopupSprite(uint32_t slot, uint32_t text_id,
                                            uint32_t value, uint32_t extra1,
                                            uint32_t slot_dup, uint32_t extra2,
                                            uint32_t reserved)
{
    // v0.13.71: capture the caller's return address BEFORE calling the
    // original function — after the call, the stack frame may be disturbed.
    // _ReturnAddress() is an MSVC intrinsic; in this context it returns the
    // address that our hook will return to, which is the instruction right
    // after the game's `call 0x48d200`. For example, a call from
    // sub_487DF0 at 0x004881D3 gives retaddr = 0x004881D8.
    uintptr_t callerRA = (uintptr_t)_ReturnAddress();

    // Always forward first — never interfere with the engine's popup pipeline.
    uint32_t result = s_origPopupSprite(slot, text_id, value, extra1,
                                         slot_dup, extra2, reserved);

    __try {
        uint16_t mode = 0;
        if (FF8Addresses::pGameMode) {
            __try { mode = *FF8Addresses::pGameMode; } __except(EXCEPTION_EXECUTE_HANDLER) {}
        }
        if (mode != 3) return result;

        // Only the low byte of text_id is the actual sprite type; callers
        // sometimes pack other bits into the upper bytes.
        uint32_t tid = text_id & 0xFF;

        // v0.14.55 / v0.14.76: Capture Scan-cast moment for the action-layer
        // detection path. text_id=0x02 is the spell-name popup that
        // appears above the caster as the action commits; value carries
        // the spell ID. Spell ID 50 (0x32) is Scan. Updates every fire,
        // BEFORE the dedup-for-logging check below — logging is
        // throttled per-battle but the timestamp must update on every
        // Scan cast, including repeats. Atomic to be safe even though
        // hook is on game thread (mod thread polls reads it). v0.14.76
        // corrected the text_id from 0x06 (never fires) to 0x02 (the
        // actual spell-name popup) — see the long comment block at
        // s_lastScanCastTick declaration above for the full context.
        // v0.14.79: Defensive match on BOTH text_id 0x02 AND 0x06 with
        // value=0x32. v0.14.55 used 0x06 and worked for first-of-battle
        // scans only (PollBattleMagicId's 0->39 transition caught those
        // even when the popup hook didn't, masking the bug). v0.14.76
        // changed to 0x02 based on a misread of the v0.14.75 BAT log
        // (claimed "0x06 never fires" but the v0.14.78 BAT direct
        // evidence shows the Bite Bug Scan-cast popup is exactly
        // text_id=0x06 value=0x32 at retaddr=0x00485938). v0.14.78 BAT
        // showed that with `tid == 0x02`, NO scan was caught by this
        // hook — only the first scan got announced (via the EWM
        // PollBattleMagicId fallback's 0->39 transition); subsequent
        // scans in the same battle had magic_id stuck at 39, no
        // transition, no detection, and a spurious "No effect" 10s
        // later from the watchdog. The screen-close detection in
        // screenshot.inl had the same bug, leaving s_scanScreenActiveSlot
        // stuck after every Scan UI close. v0.14.79 matches both values
        // so we're robust to whichever text_id the engine uses across
        // contexts (Magic-cast, Draw-cast, different monsters, etc.).
        // If both 0x02 and 0x06 turn out to fire frequently for non-
        // Scan popups, we may need a tighter retaddr-based filter, but
        // value=0x32 (decimal 50, the spell-name display duration) is
        // already a strong filter on its own.
        if ((tid == 0x02 || tid == 0x06) && (value & 0xFF) == 0x32) {
            InterlockedExchange(&s_lastScanCastTick, (LONG)GetTickCount());
        }

        // --- Diagnostic: log first occurrence of each (retaddr, slot, text_id) triple ---
        // v0.13.71: dedup now keyed on retaddr too so we see EVERY distinct
        // call site. Identifying which retaddr corresponds to the actual
        // "Miss sprite displayed" render is the whole point of this BAT.
        bool alreadyLogged = false;
        for (int i = 0; i < s_popupLogDedupCount; i++) {
            if (s_popupLogDedup[i].retaddr == callerRA &&
                s_popupLogDedup[i].slot == slot &&
                s_popupLogDedup[i].text_id == tid) {
                alreadyLogged = true;
                break;
            }
        }
        if (!alreadyLogged && s_popupLogDedupCount < POPUP_LOG_DEDUP_MAX) {
            s_popupLogDedup[s_popupLogDedupCount].retaddr = (uint32_t)callerRA;
            s_popupLogDedup[s_popupLogDedupCount].slot = slot;
            s_popupLogDedup[s_popupLogDedupCount].text_id = tid;
            s_popupLogDedupCount++;
            Log::Battle("BattleTTS: [POPUP] retaddr=0x%08X slot=%u text_id=0x%02X(%u) "
                        "value=0x%X extra1=0x%X extra2=0x%X",
                        (uint32_t)callerRA, slot, tid, tid, value, extra1, extra2);
        }

        // ====================================================================
        // v0.37.1 (#95): text_id 0xED IS NOT "MISS". IT IS A LIMIT-BREAK SHOT.
        // ====================================================================
        //
        // v0.13.71 adopted `tid == 0xED` as the miss sprite and announced
        // "Miss on <slot>". The 2026-08-20 BAT put eight of those on Irvine
        // while every one of his Shot rounds was landing:
        //
        //   [POPUP] retaddr=0x0048341F slot=0 text_id=0xED value=0x0
        //   [POPUP-MISS] Miss on Irvine. (slot=0)
        //   [HP-TRACK] Thrustaevis takes 104 damage.
        //
        // Six shots, six damage events, six "Miss"es, then two more after the
        // enemy was already dead.
        //
        // A byte-scan of the whole image for a pushed 0xED settles it. In the
        // battle code there is **exactly one producer**: `0x00485242`, inside
        //
        //   00485220  eax = arg0 / test eax, eax / jne 0x48526E
        //   00485242  push 0xED            <- arg0 == 0 branch
        //   00485289  push 0xEE            <- arg0 != 0 branch
        //
        // and `0x00485220` is called from exactly two places, `0x004ADA9C` and
        // `0x004ADB20`, both inside the state machine on `[0x01D7675A]` that
        // reads the input bits and plays a sound on each press -- the
        // trigger-driven part of a limit break. **So this popup cannot ever
        // have been a miss.** It fires once per trigger pull.
        //
        // The announcement is DELETED rather than re-pointed at another id.
        // The real miss text id is NOT KNOWN: the damage path builds its
        // popups from a queued record (`0x00485933` passes `[esi+1]` as the
        // text id), so it cannot be enumerated from the call site, and
        // substituting a guess here is how the wrong id survived eleven
        // versions in the first place. The diagnostic below now carries the
        // return address on every 0xED, so the next BAT that contains a REAL
        // miss identifies the true one from the log instead of from a theory.
        //
        // Nothing is lost by removing it: since 0xED only ever came from the
        // limit-break trigger, no real miss has ever been announced by it.
        if (tid == POPUP_TID_LIMIT_SHOT) {
            DWORD now = GetTickCount();
            if (now - s_lastLimitShotLogTick > 250) {
                s_lastLimitShotLogTick = now;
                Log::Battle("BattleTTS: [POPUP-SHOT] limit-break trigger popup "
                            "(text_id=0x%02X slot=%u retaddr=0x%08X) -- NOT a miss, "
                            "see the block in battle_tts_sprite.inl",
                            tid, slot, (uint32_t)callerRA);
            }
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        Log::Battle("BattleTTS: [POPUP] EXCEPTION in hook");
    }

    return result;
}

static void InstallPopupSpriteHook()
{
    if (s_popupSpriteHookInstalled) return;

    __try {
        uint8_t* p = (uint8_t*)POPUP_SPRITE_ADDR;
        char hx[50] = {};
        int hp = 0;
        for (int b = 0; b < 8; b++)
            hp += snprintf(hx + hp, sizeof(hx) - hp, "%02X ", p[b]);
        Log::Battle("BattleTTS: [POPUP-HOOK] sub_48D200 @ 0x%08X: %s",
                    POPUP_SPRITE_ADDR, hx);
    } __except(EXCEPTION_EXECUTE_HANDLER) {}

    MH_STATUS st = MH_CreateHook((void*)POPUP_SPRITE_ADDR,
                                  (void*)&HookedPopupSprite,
                                  (void**)&s_origPopupSprite);
    if (st != MH_OK) {
        Log::Battle("BattleTTS: [POPUP-HOOK] MH_CreateHook FAILED: %d", (int)st);
        return;
    }
    st = MH_EnableHook((void*)POPUP_SPRITE_ADDR);
    if (st != MH_OK) {
        Log::Battle("BattleTTS: [POPUP-HOOK] MH_EnableHook FAILED: %d", (int)st);
        return;
    }
    s_popupSpriteHookInstalled = true;
    Log::Battle("BattleTTS: [POPUP-HOOK] Installed at 0x%08X (sub_48D200)",
                POPUP_SPRITE_ADDR);
}

// ============================================================================
// v0.13.69: kind=4 auto-screenshot diagnostic
// ============================================================================
//
// Aaron's question about v0.13.68: "does kind=4 really mean the engine
// displays Miss?" — the answer requires VISUAL confirmation, not just
// disassembly reasoning. This subsystem schedules a screenshot ~400ms
// after each kind=4 event so we can see exactly what the engine renders
// in both branches:
//
//   a3 bit 3 set   — what we announce as "Miss on {target}"
//                     Expected: engine shows "Miss", "No Effect", or similar.
//   a3 bit 3 clear — what we silently skip (normal sprite path)
//                     Expected: engine shows damage number or nothing special.
//
// Files go to Logs\screenshots\ with filename encoding branch, timestamp,
// slot, and a3 so they can be cross-referenced against the battle log.
// Capped at 10 per battle so the directory doesn't balloon.

static const DWORD KIND4_CAPTURE_DELAY_MS = 400;
static const int KIND4_CAPTURE_LIMIT_PER_BATTLE = 10;
static const char* KIND4_SCREENSHOT_DIR =
    "C:\\Users\\ampag\\OneDrive\\Documents\\FFVIII-Accessibility-Mod"
    "\\FF8_OriginalPC_mod\\Logs\\screenshots";

static bool s_kind4CapturePending = false;
static DWORD s_kind4CaptureFireTick = 0;
static char s_kind4CaptureBasePath[512] = {};
static int s_kind4CaptureCount = 0;
static bool s_kind4CaptureDirEnsured = false;

static void SchedulePendingKind4Capture(uint32_t slot, uint32_t a3, bool isAnnounceBranch)
{
#if !BATTLE_DIAG_SCREENSHOTS
    // Issue #62: diagnostic capture disabled. Nothing scheduled, so
    // PollKind4Capture() early-returns and no kind4_* file is written.
    // The no-effect/miss announcements run on separate paths and are unaffected.
    (void)slot; (void)a3; (void)isAnnounceBranch;
    return;
#else
    if (s_kind4CaptureCount >= KIND4_CAPTURE_LIMIT_PER_BATTLE) return;
    // Don't overwrite a pending capture that hasn't fired yet.
    if (s_kind4CapturePending) return;

    SYSTEMTIME st;
    GetLocalTime(&st);

    const char* label = isAnnounceBranch ? "MISS" : "SKIP";

    // Filename: kind4_<LABEL>_HHMMSS_mmm_slotN_a3_0xHEX
    // Leading LABEL so directory sorts group the two branches for comparison.
    snprintf(s_kind4CaptureBasePath, sizeof(s_kind4CaptureBasePath),
             "%s\\kind4_%s_%02d%02d%02d_%03d_slot%u_a3_0x%X",
             KIND4_SCREENSHOT_DIR, label,
             st.wHour, st.wMinute, st.wSecond, st.wMilliseconds,
             slot, a3);

    s_kind4CaptureFireTick = GetTickCount() + KIND4_CAPTURE_DELAY_MS;
    s_kind4CapturePending = true;

    Log::Battle("BattleTTS: [KIND4-CAP] Scheduled %s capture (+%ums) for slot=%u a3=0x%X",
                label, KIND4_CAPTURE_DELAY_MS, slot, a3);
#endif
}

// Called from the main Update() loop once per frame.
static void PollKind4Capture()
{
    if (!s_kind4CapturePending) return;

    DWORD now = GetTickCount();
    if ((int32_t)(now - s_kind4CaptureFireTick) < 0) return;

    // Ensure the screenshots/ subdirectory exists on first fire.
    if (!s_kind4CaptureDirEnsured) {
        BOOL ok = CreateDirectoryA(KIND4_SCREENSHOT_DIR, NULL);
        DWORD err = ok ? 0 : GetLastError();
        Log::Battle("BattleTTS: [KIND4-CAP] CreateDirectory %s: %s (err=%u)",
                    KIND4_SCREENSHOT_DIR,
                    (ok || err == ERROR_ALREADY_EXISTS) ? "OK" : "FAILED", err);
        s_kind4CaptureDirEnsured = true;
    }

    // Fire the capture. CaptureScreenshot (battle_tts_screenshot.inl) sets
    // s_captureRequested + path, then waits up to 160ms for the SwapBuffers
    // hook to consume it. The existing victory screenshot path uses the same
    // API and works reliably from Update() context.
    CaptureScreenshot(s_kind4CaptureBasePath);
    s_kind4CaptureCount++;
    s_kind4CapturePending = false;

    Log::Battle("BattleTTS: [KIND4-CAP] Fired capture #%d of %d: %s",
                s_kind4CaptureCount, KIND4_CAPTURE_LIMIT_PER_BATTLE,
                s_kind4CaptureBasePath);
}

// ============================================================================
// Reset (defined here because it touches state owned by all four hook
// sections above — sub_483400 (v0.13.64), sub_4877F0 (v0.13.65 / .67 / .68),
// sub_48D200 (v0.13.66), and the v0.13.69 kind=4 screenshot subsystem).
// ============================================================================

static void ResetSpriteSpawnState()
{
    // v0.13.64: sub_483400 dedup
    memset(s_lastMissAnnounceTick, 0, sizeof(s_lastMissAnnounceTick));
    s_spriteLogDedupCount = 0;
    memset(s_spriteLogDedup, 0, sizeof(s_spriteLogDedup));
    // v0.13.65: sub_4877F0 dedup
    s_spellLogDedupCount = 0;
    memset(s_spellLogDedup, 0, sizeof(s_spellLogDedup));
    // v0.13.66 + v0.13.71: sub_48D200 popup hook state
    // (v0.13.71 expanded dedup to (retaddr, slot, text_id) triple, memset is
    // sufficient since sizeof(s_popupLogDedup) includes the retaddr field now)
    s_popupLogDedupCount = 0;
    memset(s_popupLogDedup, 0, sizeof(s_popupLogDedup));
    s_lastLimitShotLogTick = 0;   // v0.37.1
    // v0.13.67: sub_4877F0 kind=4 Miss dedup
    memset(s_lastSpellMissAnnounceTick, 0, sizeof(s_lastSpellMissAnnounceTick));
    // v0.14.74.2: Scan-cast popup tick. Defense-in-depth — without this
    // reset, escaping from a battle within SCAN_CAST_RECENT_MS (1 sec)
    // of casting Scan could leak the tick across the battle boundary
    // and trigger a bogus action-layer Scan announce on the very first
    // sub_48E830 fire of the next battle. NoEffect_RecordSnapshot's
    // InterlockedExchange normally consumes this tick within the same
    // frame the popup hook sets it, but the fix is one line and the
    // race window is real, so we clear it per battle. Pairs with the
    // primary v0.14.74.2 fix in OnBattleEnter for s_prevBattleMagicId.
    InterlockedExchange(&s_lastScanCastTick, 0);
    // v0.13.69: kind=4 screenshot state — reset per battle so the 10-capture
    // cap refreshes each encounter.
    s_kind4CapturePending = false;
    s_kind4CaptureCount = 0;
    s_kind4CaptureFireTick = 0;
    s_kind4CaptureBasePath[0] = '\0';
    // Note: s_kind4CaptureDirEnsured intentionally NOT reset — the directory
    // only needs to be created once per process lifetime.
}

