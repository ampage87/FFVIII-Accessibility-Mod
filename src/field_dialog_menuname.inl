// field_dialog_menuname.inl -- Hook_opcode_menuname.
//
// The naming-screen bypass. FF8 opens a character-naming UI when scripts
// trigger opcode_menuname (0x129); blind players can't navigate the glyph-
// grid UI, so we let the original handler do its essential side effects
// (savemap GF init, character-junction setup) but suppress the actual UI
// by clearing the naming-mode flags after it returns.
//
// We diff savemap GF "exists" flags before/after to detect when the engine
// just acquired a GF as part of the handler's switch-table writes. If so,
// we announce the GF name via TTS. We also announce the character name for
// non-GF calls (param 0-7 = character index).
//
// v04.35 ASM analysis revealed:
//   - 0x01CE490B = UI-open flag
//   - 0x47E480(charIdx) = GF junction init -- handled by the original
// v0.09.05: Full handler call restored (was previously bypassed entirely,
// which broke the infirmary scene by skipping character-junction init).
// We only suppress the UI flags, not the handler itself.

// v0.09.05: Restored FULL v04.35 bypass including enableGF calls.
// enableGF(charIdx) at 0x47E480 does essential character junction init --
// without it, subsequent scripts malfunction (infirmary scene breaks).
// The enableGF calls DO mark GFs 0-5 as obtained in savemap, even before
// the player earns them. This is handled at the TTS layer by reading the
// game's own displayed GF list rather than filtering by savemap exists flags.
static int __cdecl Hook_opcode_menuname(int entityPtr)
{
    const char* fieldName = FF8Addresses::pCurrentFieldName ?
                            FF8Addresses::pCurrentFieldName : "?";
    Log::Dialog("FieldDialog: [MENUNAME] Bypassing naming screen. field=%s entity=0x%08X",
               fieldName, (uint32_t)entityPtr);
    // v0.09.14: Smart bypass with correct parameter reading.
    // Disassembly of opcode_menuname shows: param = entityPtr[stackPtr * 4]
    // where stackPtr = byte at entityPtr+0x184. Stack is at START of struct.
    int param = -1;
    __try {
        uint8_t* ep = (uint8_t*)entityPtr;
        uint8_t sp = ep[0x184];
        param = *(int32_t*)(ep + sp * 4);
        Log::Dialog("FieldDialog: [MENUNAME] stackPtr=%u, param=%d (at +0x%02X)",
                   (unsigned)sp, param, sp * 4);
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        Log::Dialog("FieldDialog: [MENUNAME] SEH reading param");
    }

    // Character names (0-7) and GF names (8-23)
    static const char* s_charNames[] = {
        "Squall", "Zell", "Irvine", "Quistis", "Rinoa", "Selphie", "Seifer", "Edea"
    };
    static const char* s_gfNames[] = {
        "Quezacotl", "Shiva", "Ifrit", "Siren", "Brothers", "Diablos",
        "Carbuncle", "Leviathan", "Pandemona", "Cerberus", "Alexander",
        "Doomtrain", "Bahamut", "Cactuar", "Tonberry", "Eden"
    };

    // v0.09.19: Snapshot GF exists flags BEFORE calling original handler.
    // The original's switch table writes GF data to savemap. By diffing
    // before/after, we detect acquisitions at exactly the right moment
    // with zero polling cost during normal gameplay.
    static const uint32_t SAVEMAP_BASE_ADDR = 0x1CFDC5C;
    static const int GF_STRUCT_SIZE_LOCAL = 0x44;  // 68 bytes per GF
    static const int GF_COUNT_LOCAL = 16;
    uint8_t gfBefore[16] = {};
    {
        uint8_t* sm = (uint8_t*)SAVEMAP_BASE_ADDR;
        for (int g = 0; g < GF_COUNT_LOCAL; g++)
            gfBefore[g] = sm[0x4C + g * GF_STRUCT_SIZE_LOCAL + 0x11];
    }

    // Call original handler for ALL params -- it does essential init work
    // (switch table writes savemap data, GF assignments, character setup).
    // Then suppress the naming UI by clearing the trigger flags.
    Log::Dialog("FieldDialog: [MENUNAME] Calling original handler (param=%d)", param);
    int result = s_origMenuname(entityPtr);
    // Clear naming UI triggers before main loop sees them
    __try {
        *(uint8_t*)0x01CE4760 = 0;   // pMode0Phase - clear naming UI mode
        *(uint8_t*)0x01CE490B = 0;   // naming flag - clear
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        Log::Dialog("FieldDialog: [MENUNAME] SEH clearing UI flags");
    }

    // v0.09.19: Check for new GF acquisitions (exists flag 0->non-zero)
    bool gfAcquired = false;
    {
        static const char* GF_NAMES_LOCAL[] = {
            "Quezacotl", "Shiva", "Ifrit", "Siren", "Brothers", "Diablos",
            "Carbuncle", "Leviathan", "Pandemona", "Cerberus", "Alexander",
            "Doomtrain", "Bahamut", "Cactuar", "Tonberry", "Eden"
        };
        uint8_t* sm = (uint8_t*)SAVEMAP_BASE_ADDR;
        for (int g = 0; g < GF_COUNT_LOCAL; g++) {
            uint8_t after = sm[0x4C + g * GF_STRUCT_SIZE_LOCAL + 0x11];
            if (gfBefore[g] == 0 && after != 0) {
                gfAcquired = true;
                char buf[128];
                sprintf(buf, "GF %s acquired", GF_NAMES_LOCAL[g]);
                ScreenReader::Speak(buf, false);  // queue after any dialog
                Log::Dialog("FieldDialog: [MENUNAME] %s (idx=%d, flag 0x%02X)",
                           buf, g, (unsigned)after);
            }
        }
    }

    // v0.09.21: Announce character name only when no GF was acquired.
    // When the original handler writes GF data (e.g. Quistis/Rinoa at study panel),
    // the GF announcement is sufficient -- the character name is noise.
    // Squall (param 0) always announces because no GFs are given with him.
    if (param >= 0 && param <= 7 && !gfAcquired) {
        ScreenReader::Speak(s_charNames[param], false);
        Log::Dialog("FieldDialog: [MENUNAME] Character: %s", s_charNames[param]);
    } else if (param >= 0 && param <= 7) {
        Log::Dialog("FieldDialog: [MENUNAME] Character: %s (suppressed, GF acquired)", s_charNames[param]);
    }
    Log::Dialog("FieldDialog: [MENUNAME] UI suppressed, returning %d", result);
    return result;
}
