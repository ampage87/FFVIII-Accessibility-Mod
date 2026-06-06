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
    // v0.17.8.17.2: In Laguna dream battles the savemap party formation is
    // stale -- it still holds the regular field party (Squall/Zell/Selphie),
    // so reading it mis-names the dream party. The live battle identity lives
    // in the compStats actor-kind byte at +0x1C3. Confirmed by the
    // v0.17.8.17.1 BAT [LAGU-DIAG] block: slot0 kind=10 (Ward), slot1 kind=8
    // (Laguna), slot2 kind=9 (Kiros), matching sub_47EAF0's decoded names
    // exactly, while savemap.party read [05 00 01] (Selphie/Squall/Zell).
    // Actor-kinds 8/9/10 are Laguna/Kiros/Ward -- they appear ONLY in dream
    // sequences, so when we see one we know the savemap is stale and use the
    // actor-kind directly. For regular characters (actor-kind 0-7) we fall
    // through to the battle-tested savemap path below, guaranteeing zero
    // behavior change for normal battles.
    __try {
        uint8_t actorKind = *(uint8_t*)(BATTLE_COMP_STATS_BASE
                                        + partySlot * BATTLE_COMP_STATS_STRIDE + 0x1C3);
        if (actorKind == 8)  return "Laguna";
        if (actorKind == 9)  return "Kiros";
        if (actorKind == 10) return "Ward";
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
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

    // v0.17.8.17.5: Dream-party command list uses the SAME savemap path as
    // regular characters -- no special case. During a Laguna dream the engine
    // loads the dream party's data into the regular character-data array, and
    // SAVEMAP_PARTY_FORMATION[slot] indexes the active dream character's struct
    // within it. This is already proven by BuildMagicList (+0x10) and
    // BuildGFList (+0x58), which read this exact char struct and are validated
    // working in dreams; commands[3] lives at +0x50 in the same struct and is
    // stored in the mod's ability encoding (0x14 Magic, 0x15 GF, 0x16 Draw,
    // 0x17 Item) -- exactly what GetCommandName expects. The v0.17.8.17.4
    // [LIMIT-DIAG] dump for Laguna confirmed +0x50 = [14 15 17] = Magic, GF,
    // Item, matching her on-screen menu and reflecting a live Draw->Item
    // re-junction. (The v0.17.8.17.2..4 attempt to special-case dream chars by
    // parsing the compStats command table at +0x1C was abandoned: that table
    // has interleaved hidden entries and no reliable terminator, so the parser
    // over-ran into adjacent struct bytes that decoded as a phantom command --
    // the "4th command read Magic" bug. The savemap path avoids all of that.)
    //
    // NOTE: the NAME still needs the actor-kind override in GetBattleCharName,
    // because the char struct here does not carry the dream display name and
    // CHAR_NAMES[formation[slot]] would mis-name the dream party. Commands and
    // names are independent lookups.
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
