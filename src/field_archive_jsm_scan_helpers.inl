// field_archive_jsm_scan_helpers.inl -- helper functions for the JSM scanner.
// v0.20.49: extracted VERBATIM from the top of field_archive_jsm_scan.inl (EntityIsDebugLeftover,
// AdditemVarByte, ReadVarBank, DumpItemGateVars, and the DumpItemPickupScripts forward decl) to keep
// field_archive_jsm_scan.inl under the 80 KB CI hard fail. NO logic change -- a pure textual move.
// #included from field_archive_jsm_scan.inl at the point these definitions used to sit, so scope,
// ordering, and internal linkage are identical to the inline versions. Do not compile independently.

// v0.17.8.7: Detect debug / test-battle leftover entities that must NOT be
// promoted to INTERACTIVE_OBJECT (they surface as phantoms in the catalog --
// a navigable entry with nothing actually there). Two signals:
//   (a) SYM NAME == "cardgamemaster*". These are debug card-game scaffolding:
//       invisible (model=-1), appear as numbered copies (cardgamemaster,
//       cardgamemaster2, cardgamemaster3), reference test-battle fields, and do
//       nothing when reached (confirmed by BAT + an F11 screenshot of an empty
//       spot on bgroad_5). The real FF8 card challenges are launched from
//       visible CC-group NPCs via the CARDGAME opcode, not these entities. This
//       is the reliable signal -- it works regardless of whether the entity has
//       any init-var writes (on bghall_1 'cardgamemaster' has none) and is the
//       same name-scoped approach already used for 'camera' and party members.
//   (b) init-var (POPM_W) writes target a field named "testbl*". A secondary,
//       conservative signal for other debug entities; harmless if it never
//       fires. GetFieldNameById returns nullptr for out-of-range IDs.
static bool EntityIsDebugLeftover(int e, const char* sym)
{
    if (sym && _strnicmp(sym, "cardgamemaster", 14) == 0)
        return true;
    if (e >= 0 && e < 128) {
        for (int w = 0; w < s_initVarMaps[e].count; w++) {
            int32_t v = s_initVarMaps[e].writes[w].value;
            if (v < 0 || v > 0x7FFE) continue;  // skip non-field values + sentinels
            const char* nm = GetFieldNameById((uint16_t)v);
            if (nm && _strnicmp(nm, "testbl", 6) == 0)
                return true;
        }
    }
    return false;
}

// v0.19.x [ADDITEM-DRYRUN] SEH-guarded single-byte varblock read. Isolated into
// its own C-object-free function (like ShaftVarByte) so __try/__except never sits
// inside ScanJSMScripts, which holds std::vector/std::string (MSVC C2712). Reads
// EXIT_VARBLOCK_BASE + addr -- the same field-variable base the catalog's
// state-exclusion pass reads for hasStateGuard entities.
static unsigned AdditemVarByte(unsigned addr) {
    unsigned v = 0xFFFFu;
    __try { v = *(volatile uint8_t*)(uintptr_t)(0x01CFE9B8u + addr); }   // 0x01CFE9B8 = EXIT_VARBLOCK_BASE
    __except (EXCEPTION_EXECUTE_HANDLER) { v = 0xFFFFu; }
    return v;
}

// v0.19.x [ITEMGATE-VARS] SEH-safe bulk read of the persistent field/game var bank
// at EXIT_VARBLOCK_BASE (0x01CFE9B8). No C++ objects in the __try body (MSVC C2712-safe).
static bool ReadVarBank(uint8_t* out, int len) {
    __try {
        const volatile uint8_t* p = (const volatile uint8_t*)(uintptr_t)0x01CFE9B8u;
        for (int i = 0; i < len; i++) out[i] = p[i];
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

// v0.19.x [ITEMGATE-VARS] log-only: snapshot the var bank once per field-load in a
// compact, diffable hex format. Diffing two dorm loads (magazine uncollected vs
// collected) reveals the "collected" flag byte, which the catalog will then gate
// item pickups on. Log-only; changes nothing in the catalog.
static void DumpItemGateVars(const char* fieldName) {
    static char s_lastGateField[64] = {0};
    if (strncmp(s_lastGateField, fieldName, 63) == 0) return;   // once per field-load
    strncpy(s_lastGateField, fieldName, 63); s_lastGateField[63] = 0;
    static uint8_t vb[0x800];
    if (!ReadVarBank(vb, 0x800)) {
        Log::Field("FieldArchive: [ITEMGATE-VARS] '%s' READ FAULT", fieldName);
        return;
    }
    Log::Field("FieldArchive: [ITEMGATE-VARS] === '%s' varbank 0x01CFE9B8 +0x000..0x7FF (32 bytes/row) ===", fieldName);
    for (int row = 0; row < 0x800; row += 32) {
        char line[80]; int lp = 0;
        for (int c = 0; c < 32; c++) lp += snprintf(line + lp, sizeof(line) - lp, "%02X", vb[row + c]);
        Log::Field("FieldArchive: [ITEMGATE-VARS] +%04X %s", row, line);
    }
}

bool DumpItemPickupScripts(const char* fieldName);  // v0.19.x [ITEMDUMP] fwd decl (defined in dump.inl)
