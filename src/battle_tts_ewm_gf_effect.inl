// battle_tts_ewm_gf_effect.inl — Battle effect dispatcher polling (GF anim fire + Scan).
// Included from battle_tts_ewm.inl. Do not compile independently.
// v0.16.4: Extracted from battle_tts_ewm.inl for size compliance.
//
// v0.12.48: battle_read_effect_sub_50AF20 is the engine's battle effect
// dispatcher. It reads battle_magic_id and calls the corresponding effect
// function from func_off_battle_effects_C81774. We resolve battle_magic_id's
// address from the hook entry's relative offset and then POLL it every frame
// from PollBattleMagicId (the original hook approach crashed due to unknown
// calling convention — see v0.12.49).
//
// Address: 0x50AF20 (from FFNx naming convention, confirmed via ff8_data.cpp)
// battle_magic_id address: resolved at runtime from *(uint32_t*)(0x50AF20 + 0x3E)

// Known GF effect IDs from FFNx ff8/battle/effects.h
static bool IsGFEffectId(int effectId)
{
    switch (effectId) {
        case 5:    // Leviathan
        case 89:   // Tonberry
        case 94:   // Siren
        case 95:   // Minimog
        case 96:   // BokoChocofire
        case 97:   // BokoChocoflare
        case 98:   // BokoChocometeor
        case 99:   // BokoChocobocle
        case 115:  // Quezacotl
        case 139:  // Phoenix
        case 184:  // Shiva
        case 186:  // Odin
        case 190:  // Doomtrain
        case 198:  // Cactuar
        case 200:  // Ifrit
        case 201:  // Bahamut
        case 202:  // Cerberus
        case 203:  // Alexander
        case 204:  // Brothers
        case 205:  // Eden
        case 277:  // Carbuncle
        case 290:  // Pandemona
        case 324:  // Diablos
        case 325:  // GilgameshZantetsukenReverse
        case 326:  // GilgameshZantetsuken
        case 327:  // GilgameshMasamune
        case 328:  // GilgameshExcaliber
        case 329:  // GilgameshExcalipoor
        case 337:  // Moomba
            return true;
        default:
            return false;
    }
}

// v0.12.49: Map GF effect ID to savemap GF index (0-15)
static int GFEffectIdToIndex(int effectId)
{
    switch (effectId) {
        case 115: return 0;   // Quezacotl
        case 184: return 1;   // Shiva
        case 200: return 2;   // Ifrit
        case 94:  return 3;   // Siren
        case 204: return 4;   // Brothers
        case 324: return 5;   // Diablos
        case 277: return 6;   // Carbuncle
        case 5:   return 7;   // Leviathan
        case 290: return 8;   // Pandemona
        case 202: return 9;   // Cerberus
        case 203: return 10;  // Alexander
        case 190: return 11;  // Doomtrain
        case 201: return 12;  // Bahamut
        case 198: return 13;  // Cactuar
        case 89:  return 14;  // Tonberry
        case 205: return 15;  // Eden
        default:  return -1;
    }
}

// Find which party slot (0-2) has a specific GF junctioned.
// Returns -1 if not found.
static int FindPartySlotForGF(int gfIdx)
{
    if (gfIdx < 0 || gfIdx > 15) return -1;
    __try {
        for (int slot = 0; slot < BATTLE_ALLY_SLOTS; slot++) {
            uint8_t charIdx = *(uint8_t*)(0x1CFE74C + slot);  // SAVEMAP_PARTY_FORMATION
            if (charIdx >= 8) continue;
            uint8_t* charBase = (uint8_t*)(0x1CFE0E8 + charIdx * 0x98);  // SAVEMAP_CHAR_DATA_BASE + stride
            uint16_t gfMask = *(uint16_t*)(charBase + 0x58);
            if (gfMask & (1 << gfIdx)) return slot;
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
    return -1;
}

static void PollBattleMagicId(void)
{
    if (s_battleMagicIdAddr == 0) return;
    __try {
        int magicId = *(int*)s_battleMagicIdAddr;
        if (magicId != s_prevBattleMagicId) {
            if (IsGFEffectId(magicId)) {
                int gfIdx = GFEffectIdToIndex(magicId);
                int slot = FindPartySlotForGF(gfIdx);
                if (slot >= 0 && slot < BATTLE_ALLY_SLOTS) {
                    s_gfAnimFired[slot] = true;
                    Log::Battle("BattleTTS: [GF-EFFECT] Animation detected: effectId=%d gfIdx=%d slot=%d",
                               magicId, gfIdx, slot);
                }
                // v0.14.44: kick off GF summon audio description playback.
                // GfAudioDesc handles its own re-entrancy guard and looks up
                // the matching VTT by effectId. Safe to call regardless of
                // whether the GF is junctioned to a party slot (e.g. Phoenix
                // from a Phoenix Pinion item, Odin auto-summon, Boko Choco).
                GfAudioDesc::OnGFAnimationStart(magicId);
            } else if (magicId == 39) {
                // v0.14.50: Scan spell. Effect ID 39 (FF8BattleEffect::Scan,
                // confirmed via FFNx canary ff8/battle/effects.h). Resolve the
                // current target from the standard battle target bitmask at
                // 0x01D76884 (the same source the target-selection TTS reads).
                // Single-bit -> single target; multi-bit / zero -> -1, which
                // ScanTTS::OnScanCast logs and skips. The bitmask is set
                // during target selection and committed before the action
                // dispatches, so by the time battle_magic_id transitions to
                // 39 it should still hold the chosen target.
                uint8_t mask = 0;
                __try { mask = *(uint8_t*)0x01D76884; } __except(EXCEPTION_EXECUTE_HANDLER) {}
                int slot = -1;
                if (mask != 0) {
                    // Reject multi-bit (all-target) bitmasks so we don't
                    // announce against an arbitrary slot. Scan only ever
                    // targets a single entity in normal play; a multi-bit
                    // mask here suggests we're catching the bitmask in a
                    // transient state and should defer to a future slice's
                    // tighter signal.
                    bool singleBit = ((mask & (mask - 1)) == 0);
                    if (singleBit) slot = BitmaskToSlot(mask);
                }
                Log::Battle("BattleTTS: [SCAN-TTS] Detected effect 39 (Scan), targetMask=0x%02X resolvedSlot=%d",
                           (unsigned)mask, slot);
                // v0.14.57: action-layer cue — owns the 30 s hook-suppression
                // window so the Scan UI's sub_84F860 / sub_B687C0 hooks (which
                // fire 5-15 s later when the window opens) don't re-announce.
                ScanTTS::OnScanCast(slot, /*fromActionLayer=*/true);
            }
            s_prevBattleMagicId = magicId;
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
}

static void EWM_InstallBattleEffectHook()
{
if (s_battleEffectHookInstalled) return;
__try {
    s_battleMagicIdAddr = *(uint32_t*)(BATTLE_EFFECT_FUNC_ADDR + 0x3E);
    s_battleEffectHookInstalled = true;
    s_prevBattleMagicId = -1;
Log::Battle("BattleTTS: [GF-EFFECT] Resolved battle_magic_id at 0x%08X (poll mode)",
           s_battleMagicIdAddr);
} __except(EXCEPTION_EXECUTE_HANDLER) {
Log::Battle("BattleTTS: [GF-EFFECT] EXCEPTION resolving battle_magic_id");
}
}
