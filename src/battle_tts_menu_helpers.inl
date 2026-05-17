// battle_tts_menu_helpers.inl — Per-turn / per-submenu helper functions.
// Included from battle_tts_menu.inl after battle_tts_menu_lists.inl. Reads
// state declared in battle_tts_menu_state.inl and calls builders defined in
// battle_tts_menu_lists.inl. Consumed by PollTurnAndCommands in
// battle_tts_menu_poll.inl. Do not compile independently.
//
// v0.16.5: Extracted from the v0.16.4 monolithic battle_tts_menu.inl as
// part of the mechanical .inl split. No behavior change.


// v0.13.49: Shared submenu entry helper.
// Called from all detection paths (submenuMode, dword, subCursor) to ensure
// consistent state setup and list building regardless of which path triggers.
static void EnterSubmenu(uint8_t cmdId, const char* source)
{
    s_submenuCommandId = cmdId;
    s_inSubmenu = true;
    s_turnSubmenuCursor = 0xFF;  // force announce on next cursor read
    s_prevSubmenuMode = 0x01;    // so exit detection works
    s_pendingSubmenuEntry = false;
    Log::Battle("BattleTTS: [SUBMENU] Entry via %s: cmd 0x%02X (%s)",
               source, (unsigned)cmdId, GetCommandName(cmdId));
    // Build the appropriate list for this submenu type
    if (cmdId == 0x14 && !s_magicListBuilt) BuildMagicList(s_turnActiveCharId);
    if (cmdId == 0x15 && !s_gfListBuilt) BuildGFList(s_turnActiveCharId);
    if (cmdId == 0x17 && !s_itemListBuilt) BuildItemList();
    if (cmdId == 0x16 && !s_drawListBuilt) {
        s_lastDrawerPartySlot = s_turnActiveCharId;
        BuildDrawList();
    }
}

static const char* GetBattleCharName(uint8_t partySlot) {
    if (partySlot >= 3) return "???";
    __try {
        uint8_t charIdx = *(uint8_t*)(SAVEMAP_PARTY_FORMATION + partySlot);
        if (charIdx < 8) return CHAR_NAMES[charIdx];
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
    return "???";
}

static void BuildCharCommandList(uint8_t partySlot) {
    s_turnCharCommands[0] = 0x01; // Attack always first
    s_turnCharCommands[1] = 0x00;
    s_turnCharCommands[2] = 0x00;
    s_turnCharCommands[3] = 0x00;
    if (partySlot >= 3) return;
    
    __try {
        uint8_t charIdx = *(uint8_t*)(SAVEMAP_PARTY_FORMATION + partySlot);
        if (charIdx >= 8) return;
        
        uint8_t* charBase = (uint8_t*)(SAVEMAP_CHAR_DATA_BASE + charIdx * SAVEMAP_CHAR_STRIDE);
        __try {
            s_turnCharCommands[1] = charBase[SAVEMAP_CHAR_EQUIP_CMD + 0];
            s_turnCharCommands[2] = charBase[SAVEMAP_CHAR_EQUIP_CMD + 1];
            s_turnCharCommands[3] = charBase[SAVEMAP_CHAR_EQUIP_CMD + 2];
            Log::Battle("BattleTTS: [CMD] charIdx=%d cmds=[0x%02X,0x%02X,0x%02X] = [%s,%s,%s]",
                       (int)charIdx, s_turnCharCommands[1], s_turnCharCommands[2], s_turnCharCommands[3],
                       GetCommandName(s_turnCharCommands[1]),
                       GetCommandName(s_turnCharCommands[2]),
                       GetCommandName(s_turnCharCommands[3]));
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            Log::Battle("BattleTTS: [CMD] EXCEPTION reading cmds for charIdx=%d", (int)charIdx);
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
}
