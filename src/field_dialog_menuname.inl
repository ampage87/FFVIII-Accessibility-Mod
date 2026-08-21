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
// we announce the GF name via TTS.
//
// v04.35 ASM analysis revealed:
//   - 0x01CE490B = UI-open flag
//   - 0x47E480(idx) = GF "obtained" flag -- handled by the original
// v0.09.05: Full handler call restored (was previously bypassed entirely,
// which broke the infirmary scene by skipping character-junction init).
// We only suppress the UI flags, not the handler itself.
//
// v0.38.1 (#98): WHAT THE PARAMETER MEANS. Eleven versions of this file read
// the script parameter as a party-member index and spoke
// {Squall, Zell, Irvine, Quistis, Rinoa, Selphie, Seifer, Edea}[param] for
// params 0..7, saying nothing at all for 8..20. The switch at 0x00521DA0 says
// otherwise: params 3..18 each `push <gf>` and `call 0x0047E480`, which is
// `savemap[0x4C + gf*0x44 + 0x11] |= 1` -- the GF obtained flag -- so those
// sixteen parameters are GF namings and the pushed value is the GF index. The
// classroom study panel, which names Quezacotl and Shiva, was therefore
// announcing "Quistis" and "Rinoa"; the old comment here even explained the
// symptom away as "Quistis/Rinoa at study panel" instead of checking it.
// The full derivation, including the jump table's two transposed pairs, is in
// field_menuname_model.inl and is decoded from engine bytes by
// tests/menuname_compile.cpp.

// v0.09.05: Restored FULL v04.35 bypass including enableGF calls.
// enableGF(gfIdx) at 0x47E480 marks GFs as obtained in savemap, even before
// the player earns them. This is handled at the TTS layer by reading the
// game's own displayed GF list rather than filtering by savemap exists flags.

// v0.38.1: the sixteen GF names, indexed exactly as 0x0047E480's argument.
static const char* s_menunameGfNames[16] = {
    "Quezacotl", "Shiva", "Ifrit", "Siren", "Brothers", "Diablos",
    "Carbuncle", "Leviathan", "Pandemona", "Cerberus", "Alexander",
    "Doomtrain", "Bahamut", "Cactuar", "Tonberry", "Eden"
};

// Speak the live savemap name behind a 0x03 name id, the same way every other
// screen in the mod resolves one (v0.38.0). Returns false when the decoder
// could not expand it, so the caller can log rather than speak a placeholder.
//
// Its own function on purpose: the hook below contains __try blocks and MSVC
// C2712 forbids any object with a destructor anywhere in such a function --
// std::string included, wherever it is declared (tests/lint_seh.py).
static bool MenunameSpeakNameId(int nameId)
{
    if (nameId <= 0 || nameId > 0xFF) return false;
    const uint8_t encoded[3] = { 0x03, (uint8_t)nameId, 0x00 };
    std::string name = FF8TextDecode::Decode(encoded, sizeof(encoded));
    if (name.empty() || name.find("[Name") != std::string::npos) {
        Log::Dialog("FieldDialog: [MENUNAME] name id 0x%02X did not resolve (\"%s\")",
                    nameId, name.c_str());
        return false;
    }
    ScreenReader::Speak(name.c_str(), false);
    Log::Dialog("FieldDialog: [MENUNAME] Naming %s (name id 0x%02X)", name.c_str(), nameId);
    return true;
}

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
        uint8_t* sm = (uint8_t*)SAVEMAP_BASE_ADDR;
        for (int g = 0; g < GF_COUNT_LOCAL; g++) {
            uint8_t after = sm[0x4C + g * GF_STRUCT_SIZE_LOCAL + 0x11];
            if (gfBefore[g] == 0 && after != 0) {
                gfAcquired = true;
                char buf[128];
                sprintf(buf, "GF %s acquired", s_menunameGfNames[g]);
                ScreenReader::Speak(buf, false);  // queue after any dialog
                Log::Dialog("FieldDialog: [MENUNAME] %s (idx=%d, flag 0x%02X)",
                           buf, g, (unsigned)after);
            }
        }
    }

    // v0.38.1: announce what is actually being named.
    const int gfIdx = FieldMenunameModel::MenunameGfIndex(param);
    const int nameId = FieldMenunameModel::MenunameNameId(param);
    Log::Dialog("FieldDialog: [MENUNAME] param=%d -> kind=0x%02X gf=%d nameId=0x%02X",
                param, FieldMenunameModel::MenunameKind(param), gfIdx, nameId);

    if (gfIdx >= 0 && gfIdx < GF_COUNT_LOCAL) {
        // A GF naming. When the flag diff already spoke, the GF name has been
        // said once and saying it again is noise.
        if (!gfAcquired) {
            ScreenReader::Speak(s_menunameGfNames[gfIdx], false);
            Log::Dialog("FieldDialog: [MENUNAME] Naming GF %s (index %d)",
                        s_menunameGfNames[gfIdx], gfIdx);
        } else {
            Log::Dialog("FieldDialog: [MENUNAME] Naming GF %s (index %d, suppressed -- already announced as acquired)",
                        s_menunameGfNames[gfIdx], gfIdx);
        }
    } else if (nameId != 0) {
        if (!MenunameSpeakNameId(nameId)) {
            Log::Dialog("FieldDialog: [MENUNAME] param=%d resolved to no name -- said nothing", param);
        }
    } else if (FieldMenunameModel::MenunameParamValid(param)) {
        // Params 19 and 20 are Boko and Griever in an order the executable does
        // not settle (the naming screen's destination lives in a menu.fs
        // overlay). Silence beats a coin flip; this log line is what identifies
        // them the first time one is hit.
        Log::Dialog("FieldDialog: [MENUNAME] param=%d is Boko or Griever -- mapping UNSETTLED, said nothing. "
                    "Note which name the game shows next and pin it down.", param);
    } else {
        Log::Dialog("FieldDialog: [MENUNAME] param=%d is outside the engine's 0..20 range -- said nothing", param);
    }

    Log::Dialog("FieldDialog: [MENUNAME] UI suppressed, returning %d", result);
    return result;
}
