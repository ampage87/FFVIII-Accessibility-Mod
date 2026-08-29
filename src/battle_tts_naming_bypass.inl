// battle_tts_naming_bypass.inl -- the GF-naming-screen bypass (#naming-bypass).
//
// PART OF battle_tts.cpp -- TEXTUAL INCLUDE, inside namespace BattleTTS, before
// ResetVictoryTTS and the victory thread. Do NOT compile standalone.
//
// Split out of battle_tts_victory.inl in v0.99.0: that file reached 83,442 bytes
// and the CI hard fail is 81,920 per source file. The move is mechanical -- the
// state, the arm/deactivate logic and the code patch, lifted verbatim into three
// functions the victory thread calls.
//
// WHAT THIS DOES, AND THE BUG IT NO LONGER HAS. When a GF is drawn in battle the
// naming screen fires as mode 11 straight from the battle system, not through the
// MENUNAME field opcode, and a glyph grid is not something a blind player can
// navigate. v0.13.46 skips it by rewriting the immediate in the one instruction
// that writes mode 11 (0x00470AB2). It wrote a constant 1 -- MODE_FIELD -- and
// Jumbo Cactuar is fought on the WORLD MAP, so beating it put the game into field
// mode holding a stale field id. See naming_bypass_model.inl for the disassembly
// and for how the mode is chosen now.

// v0.13.46: Naming bypass state for battle-drawn GFs.
// v0.99.0 (#naming-bypass): the mode the bypass returns to is no longer the
// constant 1. naming_bypass_model.inl -- included by battle_tts.cpp at global
// scope, the same scope tests/naming_bypass_test.cpp compiles it in -- says why,
// and carries the disassembly it rests on.

// The last mode the party was actually standing in -- 1 field, 2 world map --
// sampled by the victory thread every poll. This is what the bypass returns to.
static int  s_preBattleHostMode  = -1;
// The value currently written into the patched immediate, so the patch can be
// re-aimed if the answer changes while the victory screen is up.
static uint8_t s_namingPatchedValue = 0;
static bool s_namingNoSourceLogged = false;

static bool s_namingBypassActive = false;
static bool s_namingBypassAnnounced = false;
static bool s_namingPatchApplied = false;
static uint8_t s_namingOrigBytes1[9] = {};  // stores 9 bytes of original MOV instruction
static uint8_t s_namingOrigBytes2[2] = {};  // unused, kept for compat
static const uint32_t NAMING_PATCH_ADDR1 = 0x00470AB2;  // THE mode-11 write instruction
static const uint32_t NAMING_PATCH_ADDR2 = 0x00470A72;  // unused, kept for compat

// ---------------------------------------------------------------------------
// Sampled every poll: where the party is actually standing. During a battle the
// mode is 3/4/5/11/100 and this is left alone, so at victory it still holds the
// mode the battle was entered FROM.
// ---------------------------------------------------------------------------
static void NamingBypassSampleMode(uint16_t mode)
{
    if (NamingBypassModel::NbIsHostMode((int)mode)) s_preBattleHostMode = (int)mode;
}

// ---------------------------------------------------------------------------
// Called on every game-mode CHANGE, from inside the victory thread.
// ---------------------------------------------------------------------------
static void NamingBypassOnModeChange(uint16_t prevMode, uint16_t mode)
{
    // v0.13.46: Auto-bypass naming screen for battle-drawn GFs.
    // When a GF is drawn during battle, the naming screen fires as mode 11
    // directly from the battle system — NOT through the MENUNAME field opcode.
    // Strategy: detect new GF during victory (mode 4), then continuously
    // clear the naming flag every frame so the naming screen never opens.
    // Also clear when mode 11 fires as a safety net.

    if (mode == 4 && prevMode != 4) {
        // Reset bypass state on victory entry
        s_namingBypassActive = false;
        s_namingBypassAnnounced = false;
        s_namingNoSourceLogged = false;
        // Check if a new GF was acquired during this battle
        if (s_preBattleGFSnapValid) {
            __try {
                for (int g = 0; g < 16; g++) {
                    uint8_t preBattleExists = s_preBattleGFStructs[g][0x11];
                    uint8_t* gfNow = (uint8_t*)(SAVEMAP_GF_BASE + g * SAVEMAP_GF_STRIDE);
                    uint8_t nowExists = gfNow[0x11];
                    if (preBattleExists == 0 && nowExists != 0) {
                        s_namingBypassActive = true;
                        Log::Battle("BattleTTS: [NAME-BYPASS] New GF detected (idx=%d), bypass armed", g);
                        break;
                    }
                }
            } __except(EXCEPTION_EXECUTE_HANDLER) {}
        }
    }

    // Mode 11 fallback: if the naming screen still appears despite flag clearing,
    // log it for diagnostic purposes.
    if (mode == 11 && (prevMode == 4 || prevMode == 100 || prevMode == 5)) {
        Log::Battle("BattleTTS: [NAME-BYPASS] WARNING: Mode 11 still appeared despite flag clearing!");
        if (!s_namingBypassAnnounced) {
            s_namingBypassAnnounced = true;
            ScreenReader::Speak("Naming screen appeared.", true);
        }
    }

    // Deactivate bypass when we leave victory/naming (mode 4→1 or 11→1)
    // v0.99.0: `mode == 2` as well -- a world-map battle now returns to the
    // world map, and this block used to be unreachable in that case.
    if (s_namingBypassActive && (prevMode == 11 || prevMode == 4) &&
        (mode == 1 || mode == 2)) {
        // Announce AFTER victory sequence completes
        if (!s_namingBypassAnnounced && s_preBattleGFSnapValid) {
            s_namingBypassAnnounced = true;
            static const char* GF_NM2[] = {
                "Quezacotl", "Shiva", "Ifrit", "Siren", "Brothers", "Diablos",
                "Carbuncle", "Leviathan", "Pandemona", "Cerberus", "Alexander",
                "Doomtrain", "Bahamut", "Cactuar", "Tonberry", "Eden"
            };
            for (int g = 0; g < 16; g++) {
                uint8_t pre = s_preBattleGFStructs[g][0x11];
                uint8_t* gfNow = (uint8_t*)(SAVEMAP_GF_BASE + g * SAVEMAP_GF_STRIDE);
                if (pre == 0 && gfNow[0x11] != 0) {
                    char gfName[64] = {};
                    DecodeFF8String(gfNow, gfName, sizeof(gfName));
                    if (gfName[0] == '\0') strncpy(gfName, GF_NM2[g], 63);
                    char buf3[128];
                    snprintf(buf3, sizeof(buf3), "GF %s acquired.", gfName);
                    ScreenReader::Speak(buf3, false);
                    Log::Battle("BattleTTS: [NAME-BYPASS] %s (idx=%d)", buf3, g);
                }
            }
        }
        s_namingBypassActive = false;
        Log::Battle("BattleTTS: [NAME-BYPASS] Bypass deactivated (mode %u->%u)", prevMode, mode);
    }
}

// ---------------------------------------------------------------------------
// Called every poll: aim the patch while the bypass is armed, restore it after.
// ---------------------------------------------------------------------------
static void NamingBypassTick()
{
    // v0.13.46: Naming bypass via CODE PATCH.
    // Change the immediate value in the ONLY instruction that writes mode=11:
    //   0x00470AB2: mov word ptr [0x1cd8fc6], 0xb
    //   Encoding: 66 C7 05 [C6 8F CD 01] [0B] 00
    //   Patch byte at 0x00470AB9 from 0x0B to 0x01 (mode=field instead of naming)
    // This preserves the full control flow — mode transitions from 4 to 1 (field)
    // instead of 4 to 11 (naming screen).
    static const uint32_t MODE11_PATCH_BYTE = NamingBypassModel::NB_PATCH_ADDR;

    // v0.99.0 (#naming-bypass): THE IMMEDIATE IS NOT A CONSTANT. v0.13.46
    // wrote 1 (field) whatever the battle was entered from, and a world-map
    // battle -- Jumbo Cactuar is one -- came out of it in field mode holding
    // a stale field id. The value is now the mode the party was standing in,
    // and the engine's own post-naming test is the fallback and the
    // cross-check. Re-evaluated every poll so the byte is right at the
    // instant the instruction runs, not merely when the bypass was armed.
    if (s_namingBypassActive) {
        int rk = 0; bool rkOk = false;
        __try {
            rk = (int)(*(volatile const uint16_t*)
                       (uintptr_t)NamingBypassModel::NB_RETURN_KIND_ADDR);
            rkOk = true;
        } __except(EXCEPTION_EXECUTE_HANDLER) { rkOk = false; }

        int src = (int)NamingBypassModel::NB_SRC_NONE;
        const uint8_t want = NamingBypassModel::NbWantedMode(
            s_preBattleHostMode >= 0, s_preBattleHostMode, rkOk, rk, &src);

        if (src == (int)NamingBypassModel::NB_SRC_NONE) {
            if (!s_namingPatchApplied && !s_namingNoSourceLogged) {
                s_namingNoSourceLogged = true;
                Log::Battle("BattleTTS: [NAME-BYPASS] NOT patching -- no trustworthy "
                            "return mode (observed=%d engineRead=%d). The naming screen "
                            "will open rather than risk the wrong mode.",
                            s_preBattleHostMode, (int)rkOk);
            }
        } else if (!s_namingPatchApplied || want != s_namingPatchedValue) {
            if (NamingBypassModel::NbSourcesDisagree(
                    s_preBattleHostMode >= 0, s_preBattleHostMode, rkOk, rk)) {
                Log::Battle("BattleTTS: [NAME-BYPASS] SOURCES DISAGREE: observed mode %d, "
                            "engine returnKind %d (=> mode %u). Using the observed mode.",
                            s_preBattleHostMode, rk,
                            (unsigned)NamingBypassModel::NbModeFromReturnKind(rk));
            }
            DWORD oldProt;
            if (VirtualProtect((LPVOID)MODE11_PATCH_BYTE, 1, PAGE_EXECUTE_READWRITE, &oldProt)) {
                if (!s_namingPatchApplied) s_namingOrigBytes1[0] = *(uint8_t*)MODE11_PATCH_BYTE;
                *(uint8_t*)MODE11_PATCH_BYTE = want;
                VirtualProtect((LPVOID)MODE11_PATCH_BYTE, 1, oldProt, &oldProt);
                Log::Battle("BattleTTS: [NAME-BYPASS] Patched 0x%08X: %02X -> %02X "
                            "(return to %s; source=%s, observed=%d returnKind=%d)",
                            MODE11_PATCH_BYTE, s_namingOrigBytes1[0], want,
                            (want == NamingBypassModel::NB_MODE_WORLDMAP) ? "the world map" : "the field",
                            (src == (int)NamingBypassModel::NB_SRC_OBSERVED) ? "observed" : "engine",
                            s_preBattleHostMode, rkOk ? rk : -1);
                s_namingPatchApplied  = true;
                s_namingPatchedValue  = want;
            } else {
                Log::Battle("BattleTTS: [NAME-BYPASS] VirtualProtect FAILED (err=%u)", GetLastError());
                s_namingPatchApplied = true;   // do not spin on it
            }
        }
        // Announcement deferred to the mode 4 -> 1/2 transition (after victory).
    }

    // Restore patch when bypass deactivates
    if (s_namingPatchApplied && !s_namingBypassActive) {
        DWORD oldProt;
        if (VirtualProtect((LPVOID)MODE11_PATCH_BYTE, 1, PAGE_EXECUTE_READWRITE, &oldProt)) {
            *(uint8_t*)MODE11_PATCH_BYTE = s_namingOrigBytes1[0];
            VirtualProtect((LPVOID)MODE11_PATCH_BYTE, 1, oldProt, &oldProt);
        }
        s_namingPatchApplied = false;
        s_namingPatchedValue = 0;
        Log::Battle("BattleTTS: [NAME-BYPASS] Patch restored");
    
        // v0.13.47: Fire the bypass announcement here instead of the deactivation block.
        // The deactivation condition (s_namingBypassActive && prevMode==4 && mode==1) has a 
        // race/caching issue where s_namingBypassActive reads as false on the 4->1 transition.
        // This patch restore block is proven to fire correctly.
        if (!s_namingBypassAnnounced && s_preBattleGFSnapValid) {
            s_namingBypassAnnounced = true;
            static const char* GF_NM3[] = {
                "Quezacotl", "Shiva", "Ifrit", "Siren", "Brothers", "Diablos",
                "Carbuncle", "Leviathan", "Pandemona", "Cerberus", "Alexander",
                "Doomtrain", "Bahamut", "Cactuar", "Tonberry", "Eden"
            };
            for (int g = 0; g < 16; g++) {
                uint8_t pre = s_preBattleGFStructs[g][0x11];
                __try {
                    uint8_t* gfNow = (uint8_t*)(SAVEMAP_GF_BASE + g * SAVEMAP_GF_STRIDE);
                    if (pre == 0 && gfNow[0x11] != 0) {
                        char gfName[64] = {};
                        DecodeFF8String(gfNow, gfName, sizeof(gfName));
                        if (gfName[0] == '\0') strncpy(gfName, GF_NM3[g], 63);
                        char buf3[128];
                        snprintf(buf3, sizeof(buf3), "GF %s acquired.", gfName);
                        ScreenReader::Speak(buf3, false);
                        Log::Battle("BattleTTS: [NAME-BYPASS] %s (idx=%d)", buf3, g);
                    }
                } __except(EXCEPTION_EXECUTE_HANDLER) {}
            }
        }
    }
}
