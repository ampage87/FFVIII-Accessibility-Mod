// menu_tts_item.inl — Item submenu TTS (use, rearrange, sort, battle flows)
// Included from menu_tts.cpp. Do not compile independently.
// v0.12.18: Extracted for readability.

static void SubmonStart(uint8_t submenuIdx)
{
    s_submonActive = true;
    s_submonSubmenu = submenuIdx;
    s_submonSnapValid = false;
    s_submonLastPoll = 0;
    s_submonTotalPolls = 0;
    memset(s_submonChangeCount, 0, sizeof(s_submonChangeCount));
    memset(s_submonMinVal, 0xFF, sizeof(s_submonMinVal));
    memset(s_submonMaxVal, 0, sizeof(s_submonMaxVal));
    memset(s_submonFirstVal, 0, sizeof(s_submonFirstVal));
    const char* name = GetMenuItemName(submenuIdx);
    Log::Menu("[SUBMON] === Started monitoring submenu %u (%s) ===",
               (unsigned)submenuIdx, name ? name : "?");
}

static void SubmonStop()
{
    if (!s_submonActive) return;
    s_submonActive = false;

    // Log summary: which offsets changed, how many times, value range
    const char* name = GetMenuItemName(s_submonSubmenu);
    Log::Menu("[SUBMON] === Summary for submenu %u (%s): %d polls ===",
               (unsigned)s_submonSubmenu, name ? name : "?", s_submonTotalPolls);

    // First pass: collect all changed offsets
    int changedCount = 0;
    for (int i = 0; i < SUBMON_REGION_SIZE; i++) {
        if (s_submonChangeCount[i] > 0 && !IsSubmonNoiseOffset(i))
            changedCount++;
    }
    Log::Menu("[SUBMON] %d offsets changed (excluding noise)", changedCount);

    // Log each changed offset, flagging likely cursors
    for (int i = 0; i < SUBMON_REGION_SIZE; i++) {
        if (s_submonChangeCount[i] == 0 || IsSubmonNoiseOffset(i)) continue;
        uint8_t minV = s_submonMinVal[i];
        uint8_t maxV = s_submonMaxVal[i];
        uint8_t firstV = s_submonFirstVal[i];
        int range = (int)maxV - (int)minV;
        int changes = s_submonChangeCount[i];

        // Flag as likely cursor if: small value range (0-30), changes > 1
        const char* flag = "";
        if (range >= 1 && range <= 30 && maxV <= 50 && changes >= 2)
            flag = " <<< CURSOR CANDIDATE";
        else if (range >= 1 && range <= 10 && changes >= 2)
            flag = " <<< POSSIBLE CURSOR";

        Log::Menu("[SUBMON]   +0x%03X: changes=%d first=%u min=%u max=%u range=%d%s",
                   i, changes, (unsigned)firstV, (unsigned)minV, (unsigned)maxV, range, flag);
    }

    Log::Menu("[SUBMON] === End summary ===");
}

static void SubmonPoll()
{
    if (!s_submonActive) return;

    DWORD now = GetTickCount();
    if (now - s_submonLastPoll < 150) return;  // poll every 150ms
    s_submonLastPoll = now;
    s_submonTotalPolls++;

    uint8_t* base = (uint8_t*)pMenuStateA;
    uint8_t cur[SUBMON_REGION_SIZE];
    __try {
        memcpy(cur, base, SUBMON_REGION_SIZE);
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        return;
    }

    if (!s_submonSnapValid) {
        memcpy(s_submonSnap, cur, SUBMON_REGION_SIZE);
        // Initialize first/min/max from initial snapshot
        for (int i = 0; i < SUBMON_REGION_SIZE; i++) {
            s_submonFirstVal[i] = cur[i];
            s_submonMinVal[i] = cur[i];
            s_submonMaxVal[i] = cur[i];
        }
        s_submonSnapValid = true;
        return;
    }

    // Compare and track changes
    for (int i = 0; i < SUBMON_REGION_SIZE; i++) {
        if (cur[i] != s_submonSnap[i]) {
            s_submonChangeCount[i]++;
            if (cur[i] < s_submonMinVal[i]) s_submonMinVal[i] = cur[i];
            if (cur[i] > s_submonMaxVal[i]) s_submonMaxVal[i] = cur[i];

            // Log individual changes for non-noise offsets (first 200 only to limit spam)
            if (!IsSubmonNoiseOffset(i) && s_submonChangeCount[i] <= 5) {
                Log::Menu("[SUBMON] +0x%03X: %u -> %u (change #%d)",
                           i, (unsigned)s_submonSnap[i], (unsigned)cur[i],
                           s_submonChangeCount[i]);
            }
            s_submonSnap[i] = cur[i];
        }
    }
}

// ============================================================================
// v0.08.29: Item submenu TTS — polls phase + cursor, announces actions/items
// ============================================================================
// Called every frame while in menu mode. Detects Item submenu via +0x234==5.
// Phase 0 = action menu (Use/Rearrange/Sort/Battle)
// Phase 1 = item list (reads inventory from savemap)

static void ResetItemSubmenuState()
{
    if (s_itemSubmenuActive) {
        Log::Menu("[MenuTTS] Item submenu exited");
    }
    s_itemSubmenuActive = false;
    s_prevItemCursor = 0xFF;
    s_prevActionCursor = 0xFF;
    s_prevFocusState = 0xFF;
    s_pendingActionCursor = 0xFF;
    s_pendingActionTime = 0;
    // v0.08.62: reset sub-flow state
    s_prevTargetCursor = 0xFF;
    s_prevTargetCharIdx = 0xFF;   // v0.18.2.7 (#10)
    s_prevTargetHP = 0xFFFF;      // v0.18.2.7 (#10)
    s_prevBattleItemCursor = 0xFF;
    s_inUseTargetMode = false;
    s_inRearrangeMode = false;
    s_inBattleMode = false;
    s_inBattleDestMode = false;
    s_battleSwapSrcPos = 0xFF;
    s_rearrangePrevFocus = 0;
    s_rearDestDiagValid = false;
    s_batDestDiagValid = false;
}

// v0.08.64: Live character HP readout for Use target.
// CHAR_DATA_BASE (0x1CFE74C) and CHAR_STRUCT_SIZE (0x98) defined above.
// Runtime character struct: +0x00=current_hp(u16), +0x02=max_hp(u16), +0x04=exp(u32), +0x08=model_id(u8)

// v0.08.63: Get active party member name by cursor position (0-based)
// Reads savemap party indices at +0xAF1 (3 bytes: char index 0-7, or 0xFF=empty)
static const char* GetPartyMemberName(uint8_t cursorPos)
{
    if (cursorPos >= 3) return nullptr;
    uint8_t* party = (uint8_t*)SAVEMAP_BASE + PARTY_INDICES_OFFSET;
    uint8_t charIdx = party[cursorPos];
    Log::Menu("[MenuTTS] GetPartyMemberName: pos=%u partyRaw=[%u,%u,%u] charIdx=%u",
               (unsigned)cursorPos,
               (unsigned)party[0], (unsigned)party[1], (unsigned)party[2],
               (unsigned)charIdx);
    // v0.17.8.17.7: dream-aware name (charIdx is a formation index; stale in dreams)
    return GetCharacterNameByPortrait(ResolveDreamAwareCharId(charIdx));
}

// ============================================================================
// v0.08.86: Status ailment decoding from savemap character status byte (+0x96)
// FF8 persistent status flags (survive outside battle, shown in menu):
//   Bit 0 (0x01): KO/Dead
//   Bit 1 (0x02): Poison
//   Bit 2 (0x04): Petrify
//   Bit 3 (0x08): Darkness/Blind
//   Bit 4 (0x10): Silence
//   Bit 5 (0x20): Berserk
//   Bit 6 (0x40): Zombie
//   Bit 7 (0x80): unknown/unused
// ============================================================================
static const char* STATUS_NAMES[] = {
    "KO",        // bit 0
    "Poison",    // bit 1
    "Petrify",   // bit 2
    "Blind",     // bit 3
    "Silence",   // bit 4
    "Berserk",   // bit 5
    "Zombie",    // bit 6
    nullptr      // bit 7 (unused)
};

// Decode status byte into a comma-separated string. Returns empty string if no ailments.
static int FormatStatusAilments(uint8_t status, char* buf, int bufSize)
{
    int pos = 0;
    for (int bit = 0; bit < 7; bit++) {
        if ((status & (1 << bit)) && STATUS_NAMES[bit]) {
            if (pos > 0 && pos < bufSize - 2)
                pos += sprintf(buf + pos, ", ");
            if (pos < bufSize - 16)
                pos += sprintf(buf + pos, "%s", STATUS_NAMES[bit]);
        }
    }
    if (pos < bufSize) buf[pos] = '\0';
    return pos;
}

// v0.08.86: Runtime computed stats array.
// FFNx: char_comp_stats_1CFF000, span of 3 (one per active party slot).
// struct ff8_char_computed_stats: curr_hp at +0x172, max_hp at +0x174.
// Struct size = 0x1D0 (464 bytes) based on FFNx definition.
static const uint32_t COMP_STATS_BASE = 0x1CFF000;
static const int COMP_STATS_CURHP_OFFSET = 0x172;
static const int COMP_STATS_MAXHP_OFFSET = 0x174;
static const int COMP_STATS_STRUCT_SIZE  = 0x1D0;  // 464 bytes per entry (3 entries for party)

// v0.18.2.12 (#47): the menu also keeps a per-CHARACTER HP display array that
// covers every AVAILABLE character — including benched ones the 3-slot
// computed-stats array misses. pMenuStateA + 0x71E + charIdx*0x20: curHP at +0,
// maxHP at +2. BAT v0.18.2.11: Squall(0)=336/916, Zell(1)=64/585,
// Quistis(3,benched)=861/861, Selphie(5)=385/482 — all matching the battle
// members' computed stats.
static const int MENU_HP_ARRAY_OFFSET = 0x71E;  // pMenuStateA-relative
static const int MENU_HP_ARRAY_STRIDE = 0x20;
static const int MENU_HP_CUR_OFFSET   = 0x00;
static const int MENU_HP_MAX_OFFSET   = 0x02;

// v0.08.86: Get character HP + status.
// Primary source: computed stats at 0x1CFF000 (live, updates on item use).
// Fallback: savemap character section for curHP, header for lead maxHP.
static bool GetCharacterHP(uint8_t charIdx, uint16_t& curHP, uint16_t& maxHP)
{
    if (charIdx > 7) return false;
    __try {
        // Read from savemap as baseline
        uint8_t* smChar = (uint8_t*)SAVEMAP_BASE + CHARS_OFFSET + charIdx * CHAR_STRUCT_SIZE;
        curHP = *(uint16_t*)(smChar + CHR_CURR_HP);
        maxHP = *(uint16_t*)(smChar + CHR_MAX_HP);

        // v0.18.2.12 (#47): prefer the menu's per-character HP display array, which
        // covers ALL available characters — including benched ones. Confirm the entry
        // belongs to this character by requiring its curHP field to match the savemap
        // curHP, then take maxHP from it. (FF8 derives max HP at runtime; the savemap
        // char struct doesn't store it — BAT showed savemap maxHP=0.)
        bool gotMax = false;
        if (pMenuStateA && charIdx <= 10) {
            uint8_t* disp = (uint8_t*)pMenuStateA + MENU_HP_ARRAY_OFFSET + charIdx * MENU_HP_ARRAY_STRIDE;
            uint16_t dispCur = *(uint16_t*)(disp + MENU_HP_CUR_OFFSET);
            uint16_t dispMax = *(uint16_t*)(disp + MENU_HP_MAX_OFFSET);
            if (dispMax > 0 && dispMax < 10000 && dispCur == curHP) {
                maxHP = dispMax;
                gotMax = true;
            }
        }

        // Fallback: computed-stats array for the 3 battle slots (formation-indexed).
        // Keep curHP from the SAVEMAP: it updates live on an in-menu item use, whereas
        // computed curHP is stale until the Item screen is rebuilt (BAT #10: after a
        // Potion, savemap=536, computed=336).
        if (!gotMax) {
            uint8_t* formation = (uint8_t*)SAVEMAP_BASE + 0xAF0;
            int partySlot = -1;
            for (int i = 0; i < 4; i++) {
                if (formation[i] == charIdx) { partySlot = i; break; }
            }
            if (partySlot >= 0 && partySlot < 3) {
                uint8_t* cs = (uint8_t*)COMP_STATS_BASE + partySlot * COMP_STATS_STRUCT_SIZE;
                uint16_t csHP = *(uint16_t*)(cs + COMP_STATS_CURHP_OFFSET);
                uint16_t csMax = *(uint16_t*)(cs + COMP_STATS_MAXHP_OFFSET);
                if (csMax > 0 && csMax < 10000) {
                    maxHP = csMax;
                    if (curHP == 0 && csHP > 0) curHP = csHP;  // safety if savemap unreadable
                }
            } else if (maxHP == 0 && formation[0] == charIdx) {
                maxHP = *(uint16_t*)((uint8_t*)SAVEMAP_BASE + HDR_CHAR1_MAX_HP);
            }
        }

        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

// v0.08.86: Get character status byte from savemap
static uint8_t GetCharacterStatus(uint8_t charIdx)
{
    if (charIdx > 7) return 0;
    __try {
        uint8_t* smChar = (uint8_t*)SAVEMAP_BASE + CHARS_OFFSET + charIdx * CHAR_STRUCT_SIZE;
        return *(smChar + CHR_STATUS);
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        return 0;
    }
}

// v0.08.86: Build full party member announcement string.
// Format: "Name, HP X of Y" or "Name, HP X" (if no maxHP)
// Appends status ailments if present: ", Poison, Blind"
// Appends "KO" for dead characters (curHP=0).
static int FormatPartyMemberAnnouncement(uint8_t charIdx, const char* name,
                                          bool isUseEntry, char* buf, int bufSize)
{
    uint16_t curHP = 0, maxHP = 0;
    bool hasHP = GetCharacterHP(charIdx, curHP, maxHP);
    uint8_t status = GetCharacterStatus(charIdx);
    
    int pos = 0;
    if (isUseEntry)
        pos += sprintf(buf + pos, "Use on ");
    
    pos += sprintf(buf + pos, "%s", name);
    
    if (hasHP) {
        if (maxHP > 0)
            pos += sprintf(buf + pos, ", HP %u of %u", (unsigned)curHP, (unsigned)maxHP);
        else
            pos += sprintf(buf + pos, ", HP %u", (unsigned)curHP);
    }
    
    // Append status ailments
    if (status != 0) {
        char statusBuf[128] = {};
        int slen = FormatStatusAilments(status, statusBuf, sizeof(statusBuf));
        if (slen > 0)
            pos += sprintf(buf + pos, ", %s", statusBuf);
    } else if (hasHP && curHP == 0) {
        // No status flags but HP is 0 — character is KO
        pos += sprintf(buf + pos, ", KO");
    }
    
    return pos;
}

// v0.18.2.10 (#46): party member char index by cursor position (0-based).
// The Use-target screen lists the FULL party roster, not just the 3 battle
// members. The roster is an 0xFF-terminated array of char indices at pMenuStateA
// +0x1DB (BAT: [1,0,5,3,FF...] = Zell,Squall,Selphie,Quistis), and the screen
// renders it SORTED BY CHARACTER INDEX (BAT via potion errors: cursor 0/1/2/3 =
// Squall/Zell/Quistis/Selphie = idx 0,1,3,5). Collect the roster, sort, map the
// cursor. Fall back to the battle formation (+0xAF0) only if the roster array is
// empty/unreadable (a degraded safety net). NB: the Use screen lists every
// AVAILABLE (joined) character, not the battle party — benched members included,
// and it grows as characters join (e.g. Rinoa/Irvine later).
static uint8_t GetPartyCharAtVisualPos(uint8_t cursorPos)
{
    static const int ROSTER_OFFSET          = 0x1DB;  // pMenuStateA-relative, 0xFF-terminated
    static const int PARTY_FORMATION_OFFSET = 0xAF0;  // savemap battle formation (fallback)

    uint8_t members[12];
    int count = 0;

    // Primary: the menu roster list (every available character)
    if (pMenuStateA) {
        uint8_t* roster = (uint8_t*)pMenuStateA + ROSTER_OFFSET;
        for (int i = 0; i < 11; i++) {
            uint8_t c = roster[i];
            if (c == 0xFF) break;
            if (c <= 10 && count < 11) members[count++] = c;
        }
    }
    bool usedRoster = (count > 0);

    // Fallback: battle formation, if the roster list was empty/unreadable
    if (!usedRoster) {
        uint8_t* formation = (uint8_t*)SAVEMAP_BASE + PARTY_FORMATION_OFFSET;
        for (int i = 0; i < 4; i++)
            if (formation[i] != 0xFF && formation[i] <= 10 && count < 11)
                members[count++] = formation[i];
    }

    // Sort by character index (the screen's display order)
    for (int i = 1; i < count; i++) {
        uint8_t key = members[i];
        int j = i - 1;
        while (j >= 0 && members[j] > key) { members[j + 1] = members[j]; j--; }
        members[j + 1] = key;
    }

    Log::Menu("[MenuTTS] GetPartyCharAtVisualPos: pos=%u src=%s count=%d sorted=[%u %u %u %u %u %u]",
               (unsigned)cursorPos, usedRoster ? "roster" : "formation", count,
               count > 0 ? (unsigned)members[0] : 255u,
               count > 1 ? (unsigned)members[1] : 255u,
               count > 2 ? (unsigned)members[2] : 255u,
               count > 3 ? (unsigned)members[3] : 255u,
               count > 4 ? (unsigned)members[4] : 255u,
               count > 5 ? (unsigned)members[5] : 255u);

    if (cursorPos < count) return members[cursorPos];
    return 0xFF;
}

// SEH-protected: reads submenu offsets and announces changes.
// v0.08.60: Primary detection via +0x22E focus state (3=action menu, 5=items list).
//   On 5→3: items→action transition, announce current action option.
//   On *→5: action→items transition, announce current item.
//   Debounced +0x27F for left/right action cursor changes.
//   +0x272 item list cursor changes announced immediately when focus==5.

static void AnnounceItemAtCursor(uint8_t cursor)
{
    uint8_t* inv = (uint8_t*)SAVEMAP_BASE + ITEM_INVENTORY_OFFSET;
    uint8_t itemId  = inv[cursor * 2];
    uint8_t itemQty = inv[cursor * 2 + 1];
    char buf[256];

    if (itemId == 0) {
        sprintf(buf, "Empty");
    } else {
        const char* name = GetItemName(itemId);
        if (name)
            sprintf(buf, "%s, %u", name, (unsigned)itemQty);
        else
            sprintf(buf, "Item %u, %u", (unsigned)itemId, (unsigned)itemQty);
    }
    ScreenReader::Speak(buf, true);
    Log::Menu("[MenuTTS] Item list cursor %u: id=%u qty=%u -> \"%s\"",
               (unsigned)cursor, (unsigned)itemId, (unsigned)itemQty, buf);
}

// v0.08.68: Battle item announcement with page/item position.
// Format: "Name, quantity N, page P, item I" or "Empty, page P, item I"
// Battle screen shows 4 items per page.
static const int BATTLE_ITEMS_PER_PAGE = 4;

// v0.08.85: battle_order working buffer code removed (v0.08.68–v0.08.83).
// The battle arrangement screen has its own display struct at 0x1D8DFF4
// containing {item_id, quantity} pairs that reflect the actual screen content,
// including filtering and live swap updates. We read that directly now.

// v0.08.84: Battle arrangement display struct at 0x1D8DFF4.
// Format: savemap_ff8_item pairs {item_id, quantity} at 2 bytes per slot.
// Engine builds this filtered list from battle_order on screen open.
// qty=0 means empty slot. This is what the screen actually shows.
static const uint32_t BATTLE_DISPLAY_STRUCT = 0x1D8DFF4;

static void AnnounceBattleItemAtCursor(uint8_t cursor)
{
    // v0.08.84: Read from the engine's display struct at 0x1D8DFF4.
    // This is the filtered/ordered list that the battle arrangement screen renders.
    uint8_t itemId = 0, itemQty = 0;
    __try {
        uint8_t* disp = (uint8_t*)BATTLE_DISPLAY_STRUCT;
        itemId  = disp[cursor * 2];
        itemQty = disp[cursor * 2 + 1];
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        Log::Menu("[MenuTTS] Battle display struct read failed at cursor %u", (unsigned)cursor);
    }
    
    int page = (cursor / BATTLE_ITEMS_PER_PAGE) + 1;
    int itemNum = (cursor % BATTLE_ITEMS_PER_PAGE) + 1;
    char buf[256];

    if (itemQty == 0) {
        sprintf(buf, "Empty, page %d, item %d", page, itemNum);
    } else {
        const char* name = GetItemName(itemId);
        if (name)
            sprintf(buf, "%s, quantity %u, page %d, item %d", name, (unsigned)itemQty, page, itemNum);
        else
            sprintf(buf, "Item %u, quantity %u, page %d, item %d", (unsigned)itemId, (unsigned)itemQty, page, itemNum);
    }
    ScreenReader::Speak(buf, true);
    Log::Menu("[MenuTTS] Battle cursor %u: disp id=%u qty=%u page=%d item=%d -> \"%s\"",
               (unsigned)cursor, (unsigned)itemId, (unsigned)itemQty, page, itemNum, buf);
}

static void PollItemSubmenu()
{
    if (!pMenuStateA) return;
    // Only poll when top-level cursor is on Item (index 1)
    if (s_prevCursor != 1) {
        if (s_itemSubmenuActive) ResetItemSubmenuState();
        return;
    }

    __try {
        uint8_t* base = (uint8_t*)pMenuStateA;
        uint8_t focusState = *(base + ITEM_FOCUS_STATE_OFFSET);  // 3=action, 5=items, 14=use target, etc.
        uint8_t actionCur  = *(base + SUBMENU_ACTION_CURSOR_OFFSET);
        uint8_t listCur    = *(base + SUBMENU_LIST_CURSOR_OFFSET);

        // Submenu just became active (top-level cursor landed on Item)
        if (!s_itemSubmenuActive) {
            s_itemSubmenuActive = true;
            s_prevItemCursor = 0xFF;
            s_prevActionCursor = actionCur;
            s_prevFocusState = focusState;
            s_pendingActionCursor = 0xFF;
            s_pendingActionTime = 0;
            s_prevTargetCursor = 0xFF;
            s_prevTargetCharIdx = 0xFF;   // v0.18.2.7 (#10)
            s_prevTargetHP = 0xFFFF;      // v0.18.2.7 (#10)
            s_prevBattleItemCursor = 0xFF;
            s_inUseTargetMode = false;
            s_inRearrangeMode = false;
            s_inBattleMode = false;
            s_inBattleDestMode = false;
            s_battleSwapSrcPos = 0xFF;
            s_rearrangePrevFocus = 0;
            Log::Menu("[MenuTTS] Item submenu entered (focus=%u actionCur=%u listCur=%u)",
                       (unsigned)focusState, (unsigned)actionCur, (unsigned)listCur);
        }

        // === FOCUS STATE TRANSITIONS (+0x22E) ===
        // Core transitions: 5↔3 via intermediates (5>2>3, 3>4>5).
        // Sub-flow focus values: 14=Use target, ~97=Rearrange, ~30=Battle, 79=Sort flash.
        if (focusState != s_prevFocusState) {
            Log::Menu("[MenuTTS] Item focus: %u -> %u (actionCur=%u listCur=%u)",
                       (unsigned)s_prevFocusState, (unsigned)focusState,
                       (unsigned)actionCur, (unsigned)listCur);

            // --- Arriving at Action menu (focus==3) ---
            if (focusState == 3 && s_prevFocusState != 3 && s_prevFocusState != 0xFF) {
                // v0.08.63: Check if returning from Sort (focus 79->3, actionCur resets to 0)
                bool fromSort = (s_prevFocusState == 79);
                if (fromSort) {
                    ScreenReader::Speak("Items sorted", true);
                    Log::Menu("[MenuTTS] Sort executed (focus 79->3)");
                }
                // Exiting sub-flow modes
                if (s_inUseTargetMode) {
                    s_inUseTargetMode = false;
                    Log::Menu("[MenuTTS] Use target mode exited");
                }
                if (s_inRearrangeMode) {
                    s_inRearrangeMode = false;
                    Log::Menu("[MenuTTS] Rearrange mode exited");
                }
                if (s_inBattleMode) {
                    s_inBattleMode = false;
                    Log::Menu("[MenuTTS] Battle mode exited");
                }
                // Announce current action option
                // v0.08.64: After Sort, queue the action name with interrupt=false
                // so user hears "Items sorted" then "Use" in sequence.
                if (actionCur < ITEM_ACTION_COUNT) {
                    ScreenReader::Speak(ITEM_ACTION_NAMES[actionCur], !fromSort);
                    Log::Menu("[MenuTTS] Item action (focus->3) cursor %u: %s%s",
                               (unsigned)actionCur, ITEM_ACTION_NAMES[actionCur],
                               fromSort ? " (queued after sort)" : "");
                }
                s_prevActionCursor = actionCur;
                s_pendingActionTime = 0;
            }
            // --- Arriving at Items list (focus==5) ---
            else if (focusState == 5 && s_prevFocusState != 5 && s_prevFocusState != 0xFF) {
                // Exiting sub-flow modes (cancel from Use target returns via 14->4->5)
                if (s_inUseTargetMode) {
                    s_inUseTargetMode = false;
                    Log::Menu("[MenuTTS] Use target mode exited (back to items)");
                }
                AnnounceItemAtCursor(listCur);
                s_prevItemCursor = listCur;
            }
            // --- v0.08.86: Arriving at Use target selection (focus==14) ---
            else if (focusState == 14 && !s_inUseTargetMode) {
                s_inUseTargetMode = true;
                uint8_t targetCur = *(base + ITEM_TARGET_CURSOR_OFFSET);
                s_prevTargetCursor = targetCur;
                uint8_t charIdx = GetPartyCharAtVisualPos(targetCur);
                const char* name = GetCharacterNameByPortrait(ResolveDreamAwareCharId(charIdx));  // v0.17.8.17.7 dream-aware
                char buf[256];
                if (name) {
                    FormatPartyMemberAnnouncement(charIdx, name, true, buf, sizeof(buf));
                } else {
                    sprintf(buf, "Use on party member %u", (unsigned)targetCur + 1);
                }
                ScreenReader::Speak(buf, true);
                Log::Menu("[MenuTTS] Use target entered: cursor %u charIdx=%u -> \"%s\"",
                           (unsigned)targetCur, (unsigned)charIdx, buf);
                // v0.18.2.7 (#10): baseline the target HP so the live-HP poll below
                // only re-announces on an actual change from using an item.
                {
                    uint16_t initCur = 0, initMax = 0;
                    GetCharacterHP(charIdx, initCur, initMax);
                    s_prevTargetCharIdx = charIdx;
                    s_prevTargetHP = initCur;
                }
            }
            // --- v0.08.64: Rearrange mode detection (focus stabilizes ~97) ---
            else if (focusState >= 94 && focusState <= 100 && !s_inRearrangeMode) {
                s_inRearrangeMode = true;
                s_rearrangePrevFocus = focusState;
                s_prevItemCursor = listCur;
                s_prevTargetCursor = 0xFF;  // reset dest cursor for when focus hits 99
                AnnounceItemAtCursor(listCur);
                Log::Menu("[MenuTTS] Rearrange mode entered (focus=%u listCur=%u)",
                           (unsigned)focusState, (unsigned)listCur);
            }
            // --- v0.08.70: Battle mode detection (focus stabilizes ~30) ---
            else if (focusState >= 26 && focusState <= 35 && !s_inBattleMode) {
                s_inBattleMode = true;
                s_inBattleDestMode = false;
                uint8_t battleCur = *(base + BATTLE_ITEM_CURSOR_OFFSET);
                s_prevBattleItemCursor = battleCur;
                AnnounceBattleItemAtCursor(battleCur);
                Log::Menu("[MenuTTS] Battle mode entered (focus=%u battleCur=%u)",
                           (unsigned)focusState, (unsigned)battleCur);

            }
            // --- v0.08.77: Battle destination entered (focus==36) ---
            else if (focusState == 36 && s_inBattleMode && s_prevFocusState != 36) {
                s_inBattleDestMode = true;
                s_battleSwapSrcPos = s_prevBattleItemCursor;  // remember source for swap
                uint8_t batDestCur = *(base + 0x286);
                s_prevBattleItemCursor = batDestCur;
                AnnounceBattleItemAtCursor(batDestCur);
                Log::Menu("[MenuTTS] Battle dest entered (focus=36 srcPos=%u destCur=%u)",
                           (unsigned)s_battleSwapSrcPos, (unsigned)batDestCur);
            }

            // --- v0.08.79: Battle swap detection (returning from dest to source) ---
            if (s_inBattleMode && s_inBattleDestMode && focusState >= 26 && focusState <= 35) {
                // Was in dest mode (focus==36), now back to source (~30) = swap completed
                uint8_t destPos = s_prevBattleItemCursor;  // last dest cursor position
                s_inBattleDestMode = false;
                // v0.08.79: No manual swap tracking needed — live buffer is authoritative
                ScreenReader::Speak("Swapped", true);
                Log::Menu("[MenuTTS] Battle swap completed: pos %u <-> %u",
                           (unsigned)s_battleSwapSrcPos, (unsigned)destPos);
                s_battleSwapSrcPos = 0xFF;
                uint8_t battleCur = *(base + BATTLE_ITEM_CURSOR_OFFSET);
                s_prevBattleItemCursor = battleCur;
                AnnounceBattleItemAtCursor(battleCur);
            }

            // --- v0.08.64: Rearrange swap detection (focus 99->97 = swap completed) ---
            if (s_inRearrangeMode && s_rearrangePrevFocus == 99 && focusState == 97) {
                ScreenReader::Speak("Swapped", true);
                Log::Menu("[MenuTTS] Rearrange swap completed (99->97)");
                // Re-announce item at cursor after swap
                AnnounceItemAtCursor(listCur);
                s_prevItemCursor = listCur;
                s_prevTargetCursor = 0xFF;  // reset dest cursor for next source→dest cycle
            }
            if (s_inRearrangeMode) {
                s_rearrangePrevFocus = focusState;
            }

            s_prevFocusState = focusState;
        }

        // === ACTION CURSOR: debounced left/right (only when at action menu focus==3) ===
        if (focusState == 3) {
            if (actionCur != s_prevActionCursor) {
                s_pendingActionCursor = actionCur;
                s_pendingActionTime = GetTickCount();
                s_prevActionCursor = actionCur;
            }
            if (s_pendingActionTime != 0) {
                DWORD now = GetTickCount();
                if (actionCur == s_pendingActionCursor &&
                    (now - s_pendingActionTime) >= 200) {
                    if (s_pendingActionCursor < ITEM_ACTION_COUNT) {
                        ScreenReader::Speak(ITEM_ACTION_NAMES[s_pendingActionCursor], true);
                        Log::Menu("[MenuTTS] Item action (debounced) cursor %u: %s",
                                   (unsigned)s_pendingActionCursor,
                                   ITEM_ACTION_NAMES[s_pendingActionCursor]);
                    }
                    s_pendingActionTime = 0;
                } else if (actionCur != s_pendingActionCursor) {
                    s_pendingActionCursor = actionCur;
                    s_pendingActionTime = now;
                }
            }
        } else {
            s_pendingActionTime = 0;
            s_prevActionCursor = actionCur;
        }

        // === ITEM LIST CURSOR: immediate announce when items list has focus ===
        if (focusState == 5) {
            if (listCur != s_prevItemCursor) {
                AnnounceItemAtCursor(listCur);
                s_prevItemCursor = listCur;
            }
        }

        // === v0.08.86: USE TARGET CURSOR (+0x276 party member selection) ===
        // v0.18.2.7 (#10): also re-announce when the selected target's HP changes
        // from using an item. The cursor stays on the same character through a use,
        // so the cursor-move check alone never re-reads the (now updated) HP.
        if (s_inUseTargetMode) {
            uint8_t targetCur = *(base + ITEM_TARGET_CURSOR_OFFSET);
            uint8_t charIdx = GetPartyCharAtVisualPos(targetCur);
            uint16_t curHP = 0, maxHP = 0;
            GetCharacterHP(charIdx, curHP, maxHP);
            bool cursorMoved = (targetCur != s_prevTargetCursor);
            bool hpChanged   = (!cursorMoved && charIdx == s_prevTargetCharIdx &&
                                s_prevTargetHP != 0xFFFF && curHP != s_prevTargetHP);
            if (cursorMoved || hpChanged) {
                const char* name = GetCharacterNameByPortrait(ResolveDreamAwareCharId(charIdx));  // v0.17.8.17.7 dream-aware
                char buf[256];
                if (name) {
                    FormatPartyMemberAnnouncement(charIdx, name, false, buf, sizeof(buf));
                } else {
                    sprintf(buf, "Party member %u", (unsigned)targetCur + 1);
                }
                ScreenReader::Speak(buf, true);
                Log::Menu("[MenuTTS] Use target cursor %u: charIdx=%u hp=%u/%u -> \"%s\"%s",
                           (unsigned)targetCur, (unsigned)charIdx,
                           (unsigned)curHP, (unsigned)maxHP, buf,
                           hpChanged ? " (HP changed)" : "");
                s_prevTargetCursor = targetCur;
                s_prevTargetCharIdx = charIdx;
                s_prevTargetHP = curHP;
            }
        }

        // === v0.08.63: REARRANGE ITEM CURSOR (reuse +0x272 during rearrange source, focus ~97) ===
        if (s_inRearrangeMode && focusState == 97) {
            if (listCur != s_prevItemCursor) {
                AnnounceItemAtCursor(listCur);
                s_prevItemCursor = listCur;
            }
        }

        // === v0.08.64: REARRANGE DESTINATION CURSOR (+0x276 during focus==99) ===
        if (s_inRearrangeMode && focusState == 99) {
            uint8_t destCur = *(base + ITEM_TARGET_CURSOR_OFFSET);  // +0x276 reused for destination
            if (destCur != s_prevTargetCursor) {
                AnnounceItemAtCursor(destCur);
                Log::Menu("[MenuTTS] Rearrange dest cursor %u", (unsigned)destCur);
                s_prevTargetCursor = destCur;
            }
        }

        // === v0.08.68: BATTLE ITEM CURSOR (+0x285 for source browsing) ===
        if (s_inBattleMode && focusState >= 26 && focusState <= 35) {
            uint8_t battleCur = *(base + BATTLE_ITEM_CURSOR_OFFSET);
            if (battleCur != s_prevBattleItemCursor) {
                AnnounceBattleItemAtCursor(battleCur);
                s_prevBattleItemCursor = battleCur;
            }
        }

        // === v0.08.68: BATTLE DESTINATION CURSOR (+0x286 during focus==36) ===
        static const int BATTLE_DEST_CURSOR_OFFSET = 0x286;
        if (s_inBattleMode && focusState == 36) {
            uint8_t batDestCur = *(base + BATTLE_DEST_CURSOR_OFFSET);
            if (batDestCur != s_prevBattleItemCursor) {
                AnnounceBattleItemAtCursor(batDestCur);
                s_prevBattleItemCursor = batDestCur;
            }
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        Log::Menu("[MenuTTS] SEH exception in PollItemSubmenu");
    }
}

// ============================================================================
// v0.09.41: Help bar text extraction from GCW buffer
// ============================================================================
// The GCW buffer captures all text rendered each frame. One render cycle:
//   [menu items][help text][character name(s)][location]
// The static menu items prefix is constant: "JunctionItemMagic...Save".
// After "Save", the help text runs until the first party member name.

// Build the static prefix from MENU_ITEMS[] (computed once)
static const char* GetMenuItemsPrefix()
{
    static char s_prefix[128] = {};
    static bool s_built = false;
    if (!s_built) {
        int pos = 0;
        for (int i = 0; i < MENU_ITEMS_COUNT && pos < 120; i++)
            pos += sprintf(s_prefix + pos, "%s", MENU_ITEMS[i]);
        s_built = true;
    }
    return s_prefix;
}

// All possible character names to search for as help text end markers
static const char* HELP_END_MARKERS[] = {
    "Squall", "Zell", "Irvine", "Quistis", "Rinoa", "Selphie",
    "Seifer", "Edea", "Laguna", "Kiros", "Ward",
    nullptr
};

