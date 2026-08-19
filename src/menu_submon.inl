// menu_submon.inl -- the SUBMENU MEMORY MONITOR.
//
// PART OF menu_tts.cpp (textual include). Split out of menu_tts_item.inl in
// v0.30.0 (#89) so that file can be compiled by a host probe: this block pulls
// in nine host globals and a name lookup that have nothing to do with the Item
// screen, and stubbing them to test the Item screen would have been nine
// statements about an interface instead of one test of the code.
//
// ---------------------------------------------------------------------------
// WHAT THIS IS, AND WHAT IT COST
//
// SUBMON snapshots a region of the menu state each frame and reports which
// bytes changed, how often, and over what range, flagging the small-range ones
// as "CURSOR CANDIDATE". Nearly every pMenuStateA-relative offset in this mod
// was found with it.
//
// **It finds bytes that correlate. It cannot tell you what they mean**, and the
// #88/#89 audits spent most of their length undoing that difference:
//
//   * the GF Learn cursor "+0x257 / +0x258" turned out to be `cursor[page]`,
//     an array indexed by a page byte SUBMON never suggested looking for;
//   * the Item use-target cursor was read as a position in a packed list when
//     the engine treats it as a bit index into a mask;
//   * the Switch screen's "unavailable" flag is still a hypothesis its own
//     comment admits to.
//
// Keep it -- it is genuinely useful for finding WHERE to look. But an offset it
// suggests is a lead, and the disassembly is the evidence. Nothing found here
// should reach a release without the state machine agreeing.
// ---------------------------------------------------------------------------

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

