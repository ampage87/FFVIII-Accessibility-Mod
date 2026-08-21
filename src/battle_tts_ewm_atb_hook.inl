// battle_tts_ewm_atb_hook.inl — HookedATBUpdate (ATB freeze sandwich) + EWM lifecycle.
// Included from battle_tts_ewm.inl. Do not compile independently.
// v0.16.4: Extracted from battle_tts_ewm.inl for size compliance.
//
// Contains:
//   - HookedATBUpdate: the core ATB-freeze sandwich (pre-cap save → call
//     original → post-cap restore). v0.13.57 changed semantics from
//     "cap at max-1" to "restore exact pre-sandwich value" so freezes
//     no longer converge entities at max-1 (which erased natural race
//     order during long freezes — see comment block in the function).
//   - EWM_LoadConfig / EWM_SaveConfig / EWM_PollToggle: the O-key toggle
//     and INI-backed persistence (v0.13.51 moved this from a dedicated
//     ewm_config.txt to the shared Config INI).
//   - EWM_InstallHook: MinHook on ATB_UPDATE_FUNC_ADDR pointing at
//     HookedATBUpdate.

static void __cdecl HookedATBUpdate(void)
{
    // v0.10.88: Initialize GF timer scan snapshot when GF loading starts
    if (!s_gfScanValid) {
        uint8_t gfAct = 0;
        __try { gfAct = *(uint8_t*)0x01D76971; } __except(EXCEPTION_EXECUTE_HANDLER) {}
        if (gfAct == 1) {
            memcpy(s_gfScanSnap, (uint8_t*)GF_SCAN_BASE, GF_SCAN_BYTES);
            s_gfScanValid = true;
            Log::Battle("BattleTTS: [GF-SCAN] Snapshot initialized (GF active)");
        }
    }
    
    // v0.10.85: RET code patch (belt-and-suspenders, kept alongside sticky hide).
    if (s_gfFirePatchReady) {
        if (s_ewmCapGF && !s_gfFirePatched) {
            uint8_t* p = (uint8_t*)GF_FIRE_PATCH_ADDR;
            if (*p == GF_FIRE_VALUE) {
                *p = GF_SAFE_VALUE;
                s_gfFirePatched = true;
            }
        } else if (!s_ewmCapGF && s_gfFirePatched) {
            uint8_t* p = (uint8_t*)GF_FIRE_PATCH_ADDR;
            *p = GF_FIRE_VALUE;
            s_gfFirePatched = false;
        }
    }
    
    // v0.10.87: Sticky/sandwich gfActive hide REMOVED (v0.10.88).
    // All flag-hiding approaches break ATB or menus. Need to find the
    // GF timer decrement function instead.
    
    // v0.10.69: Clamp GF state68 on the GAME THREAD (before battle loop reads it).
    // The mod thread clamp was too late — race condition with the game loop.
    if (s_ewmCapGF) {
        uint8_t st = *(uint8_t*)GF_STATE68_ADDR;
        if (st == GF_STATE_FIRE) {
            if (!s_gfState68Clamped) {
                s_gfSavedState68 = st;
                s_gfState68Clamped = true;
            }
            *(uint8_t*)GF_STATE68_ADDR = GF_STATE_SAFE;
        }
    } else if (s_gfState68Clamped) {
        // DON'T restore state68 to 5 (fire) — let the GF timer function
        // set it naturally when the timer reaches 0 after uncapping.
        // Just clear our tracking flag.
        s_gfState68Clamped = false;
    }
    
    // v0.10.79-82: Real GF timer pre-cap REMOVED (v0.10.88).
    // 0x01D27D94 was Enemy 1's ATB, not a GF timer. Stride is 0xD0, not 0x1D0.
    
    if (!s_ewmShouldCap) {
        s_originalATBUpdate();
        
        // v0.10.88: Per-frame GF timer scan on early-return path
        if (s_gfScanValid && s_gfScanLogCount < GF_SCAN_LOG_MAX) {
            uint8_t newSnap[GF_SCAN_BYTES];
            memcpy(newSnap, (uint8_t*)GF_SCAN_BASE, GF_SCAN_BYTES);
            for (int i = 0; i + 1 < GF_SCAN_BYTES; i += 2) {
                int16_t oldVal = *(int16_t*)(s_gfScanSnap + i);
                int16_t newVal = *(int16_t*)(newSnap + i);
                int16_t delta = newVal - oldVal;
                // Look for decrementing values (delta -1 to -4) with values in timer range
                if (delta >= -4 && delta <= -1 && oldVal > 0 && oldVal <= 500) {
                    Log::Battle("BattleTTS: [GF-SCAN] +0x%03X (0x%08X): %d -> %d (delta=%d)",
                               i, GF_SCAN_BASE + i, (int)oldVal, (int)newVal, (int)delta);
                    s_gfScanLogCount++;
                }
            }
            memcpy(s_gfScanSnap, newSnap, GF_SCAN_BYTES);
        }
        
        return;
    }
    
    uint8_t excludeSlot = s_ewmCapExcludeSlot;
    
    // --- PRE-CAP: save real ATB values, set to 0 ---
    uint32_t savedATB[BATTLE_TOTAL_SLOTS] = {};
    for (int slot = 0; slot < BATTLE_TOTAL_SLOTS; slot++) {
        if (slot == (int)excludeSlot) continue;
        
        uint8_t* base = (uint8_t*)(BATTLE_ENTITY_ARRAY_BASE + slot * BATTLE_ENTITY_STRIDE);
        
        if (slot < BATTLE_ALLY_SLOTS) {
            uint16_t* pCurATB = (uint16_t*)(base + BENT_CUR_ATB);
            savedATB[slot] = *pCurATB;
            *pCurATB = 0;
        } else {
            uint32_t* pCurATB = (uint32_t*)(base + BENT_CUR_ATB);
            savedATB[slot] = *pCurATB;
            *pCurATB = 0;
        }
    }
    
    // v0.10.95: PRE-CAP for GF loading counter — per-slot via entity+0x7C.
    // The ATB function also increments compStats[slot]+0x14 (GF loading gauge).
    // Same sandwich: save → zero → call → measure → restore+cap.
    // Now iterates all ally slots instead of just gfSlot.
    uint16_t savedGFLoad[BATTLE_ALLY_SLOTS] = {};
    uint16_t gfLoadMax[BATTLE_ALLY_SLOTS] = {};
    bool gfLoadActive[BATTLE_ALLY_SLOTS] = {};
    if (s_ewmCapGF) {
        for (int gs = 0; gs < BATTLE_ALLY_SLOTS; gs++) {
            uint8_t* ent = (uint8_t*)(BATTLE_ENTITY_ARRAY_BASE + gs * BATTLE_ENTITY_STRIDE);
            uint16_t gfFlag = *(uint16_t*)(ent + BENT_GF_SUMMON_FLAG);
            if (gfFlag != 0) {
                uint8_t* cs = (uint8_t*)(BATTLE_COMP_STATS_BASE + gs * BATTLE_COMP_STATS_STRIDE);
                uint16_t* pGFLoad = (uint16_t*)(cs + 0x14);
                savedGFLoad[gs] = *pGFLoad;
                gfLoadMax[gs] = *(uint16_t*)(cs + 0x16);
                *pGFLoad = 0;  // zero so original function increments from 0
                gfLoadActive[gs] = true;
            }
        }
    }
    
    // --- CALL ORIGINAL: ATB increments from 0, status timers run normally ---
    s_originalATBUpdate();
    
    // v0.13.57: POST-FREEZE for GF loading counter — restore to exact
    // pre-sandwich value (matching ATB freeze semantics). GF loading
    // only advances when freeze is released (i.e., during the active
    // player's GF-cast animation), never during menu/freeze windows.
    for (int gs = 0; gs < BATTLE_ALLY_SLOTS; gs++) {
        if (!gfLoadActive[gs]) continue;
        uint8_t* cs = (uint8_t*)(BATTLE_COMP_STATS_BASE + gs * BATTLE_COMP_STATS_STRIDE);
        uint16_t* pGFLoad = (uint16_t*)(cs + 0x14);
        *pGFLoad = savedGFLoad[gs];
    }
    
    // v0.10.82: Real GF timer post-cap REMOVED (v0.10.88) — was Enemy 1's ATB.
    
    // v0.13.57: POST-FREEZE — restore ATB to exact pre-sandwich value.
    // Previously (v0.13.56 and earlier) this was a "cap at max-1" sandwich
    // that ADDED the per-frame increment on top of savedATB, then clamped.
    // That preserved race order for entities below max-1 but CONVERGED
    // everyone at max-1 during long freezes (GF summons, damage windows),
    // erasing the natural ATB race — multiple entities would tie at 11999
    // and all dispatch simultaneously when the freeze released.
    //
    // Freeze semantics match Aaron's turn-based retrofit model: "the enemy's
    // ATB and other party members ATB are held in place" — held literally
    // means their value does not change. When the freeze releases, each
    // entity resumes from exactly where it was; whoever was closest to max
    // wins the natural race a few frames later (no ties created by the
    // mod).
    for (int slot = 0; slot < BATTLE_TOTAL_SLOTS; slot++) {
        if (slot == (int)excludeSlot) continue;
        
        uint8_t* base = (uint8_t*)(BATTLE_ENTITY_ARRAY_BASE + slot * BATTLE_ENTITY_STRIDE);
        
        if (slot < BATTLE_ALLY_SLOTS) {
            uint16_t* pCurATB = (uint16_t*)(base + BENT_CUR_ATB);
            *pCurATB = (uint16_t)savedATB[slot];
        } else {
            uint32_t* pCurATB = (uint32_t*)(base + BENT_CUR_ATB);
            *pCurATB = (uint32_t)savedATB[slot];
        }
    }

    // v0.10.95: Per-slot GF max inflation on the GAME THREAD.
    // Uses entity+0x7C per-character flag instead of global gfSlot.
    // Iterates all ally slots: any slot with entity+0x7C != 0 gets its
    // compStats+0x16 inflated to 0xFFFF to prevent the fire check from passing.
    if (s_ewmCapGF) {
        for (int gs = 0; gs < BATTLE_ALLY_SLOTS; gs++) {
            uint8_t* ent = (uint8_t*)(BATTLE_ENTITY_ARRAY_BASE + gs * BATTLE_ENTITY_STRIDE);
            uint16_t gfFlag = *(uint16_t*)(ent + BENT_GF_SUMMON_FLAG);
            if (gfFlag != 0) {
                uint8_t* cs2 = (uint8_t*)(BATTLE_COMP_STATS_BASE + gs * BATTLE_COMP_STATS_STRIDE);
                uint16_t* pMax = (uint16_t*)(cs2 + 0x16);
                uint16_t curMax = *pMax;
                if (curMax != 0xFFFF && curMax > 0) {
                    if (!s_gfMaxInflated[gs]) {
                        s_gfRealMax[gs] = curMax;
                        s_gfMaxInflated[gs] = true;
                    }
                    *pMax = 0xFFFF;
                }
            }
        }
    } else {
        // Cap released — restore real max for all inflated slots
        for (int gs = 0; gs < BATTLE_ALLY_SLOTS; gs++) {
            if (s_gfMaxInflated[gs]) {
                uint8_t* cs3 = (uint8_t*)(BATTLE_COMP_STATS_BASE + gs * BATTLE_COMP_STATS_STRIDE);
                uint16_t* pMax2 = (uint16_t*)(cs3 + 0x16);
                if (*pMax2 == 0xFFFF && s_gfRealMax[gs] > 0) {
                    *pMax2 = s_gfRealMax[gs];
                }
                s_gfMaxInflated[gs] = false;
                s_gfRealMax[gs] = 0;
            }
        }
    }
    
    // v0.10.87: Sticky gfActive zero REMOVED (v0.10.88).
    
    // v0.10.88: Per-frame GF timer scan on main sandwich path
    if (s_gfScanValid && s_gfScanLogCount < GF_SCAN_LOG_MAX) {
        uint8_t newSnap[GF_SCAN_BYTES];
        memcpy(newSnap, (uint8_t*)GF_SCAN_BASE, GF_SCAN_BYTES);
        for (int i = 0; i + 1 < GF_SCAN_BYTES; i += 2) {
            int16_t oldVal = *(int16_t*)(s_gfScanSnap + i);
            int16_t newVal = *(int16_t*)(newSnap + i);
            int16_t delta = newVal - oldVal;
            if (delta >= -4 && delta <= -1 && oldVal > 0 && oldVal <= 500) {
                Log::Battle("BattleTTS: [GF-SCAN] +0x%03X (0x%08X): %d -> %d (delta=%d)",
                           i, GF_SCAN_BASE + i, (int)oldVal, (int)newVal, (int)delta);
                s_gfScanLogCount++;
            }
        }
        memcpy(s_gfScanSnap, newSnap, GF_SCAN_BYTES);
    }
}

// v0.13.51: EWM toggle persistence moved to the shared Config INI.
// Legacy ewm_config.txt (single "1"/"0" byte) is imported by Config::Load on
// first run and deleted, so existing installs preserve their setting.
static void EWM_LoadConfig()
{
    if (s_ewmConfigLoaded) return;
    s_ewmConfigLoaded = true;
    Config::Load();
    s_ewmEnabled = (Config::GetInt("ewm_enabled", 1) != 0);
    Log::Battle("BattleTTS: [EWM] Config loaded: ewm_enabled=%d (from %s)",
               (int)s_ewmEnabled, Config::GetPath());
}

static void EWM_SaveConfig()
{
    Config::SetInt("ewm_enabled", s_ewmEnabled ? 1 : 0);
}

static void EWM_PollToggle()
{
    bool oDown = (GetAsyncKeyState('O') & 0x8000) != 0;
    bool oPressed = oDown && !s_ewmOKeyWasDown;
    s_ewmOKeyWasDown = oDown;
    if (!oPressed) return;
    s_ewmEnabled = !s_ewmEnabled;
    EWM_SaveConfig();
    // If disabling, immediately release cap
    if (!s_ewmEnabled) {
        EWM_SetFreeze(false);
        s_ewmFreezing = false;
        s_ewmCapExcludeSlot = 0xFF;
        s_ewmCapGF = false;
    }
    const char* msg = s_ewmEnabled ? "Enhanced Wait Mode on" : "Enhanced Wait Mode off";
    ScreenReader::Speak(msg, true);
    Log::Battle("BattleTTS: [EWM] Toggled: %s", msg);
}

// Install the MinHook on the ATB update function.
// Called once from Initialize().
static void EWM_InstallHook()
{
    if (s_ewmHookInstalled) return;
    MH_STATUS st = MH_CreateHook(
        (LPVOID)(uintptr_t)ATB_UPDATE_FUNC_ADDR,
        (LPVOID)HookedATBUpdate,
        (LPVOID*)&s_originalATBUpdate);
    if (st == MH_OK) {
        st = MH_EnableHook((LPVOID)(uintptr_t)ATB_UPDATE_FUNC_ADDR);
    }
    s_ewmHookInstalled = (st == MH_OK);
    Log::Battle("BattleTTS: [EWM] ATB hook @ 0x%08X — %s (trampoline=0x%08X)",
               ATB_UPDATE_FUNC_ADDR, MH_StatusToString(st),
               (uint32_t)(uintptr_t)s_originalATBUpdate);
}
