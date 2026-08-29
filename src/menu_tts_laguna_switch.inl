// menu_tts_laguna_switch.inl -- reading the Laguna junction-party screen.
//
// Included from menu_tts.cpp after laguna_switch_model.inl. The derivation --
// field opcode, menu id, screen id, module, every offset -- is in that file.
//
// Everything here is a read of a module field. No hooks, no patches, nothing
// written back. The module is found by walking the menu module pool for the one
// whose update function is this screen's state machine, exactly the way
// menu_tts_magic.inl finds Magic, so the reader is live precisely when the
// screen is and is silent everywhere else. There is no game-mode gate, because
// this screen is opened from a field script and does not go through menu mode 6.
//
// WHAT IT SAYS AND WHY
//
// The screen has no text of its own to borrow: it is two grids of character
// faces with a cursor, and the only words on it are the help bar. So every line
// below is composed from module state, and the two things a sighted player gets
// for free are the two things it has to say out loud:
//
//   * WHO IS UNDER THE CURSOR, and -- on the left grid -- **who they are
//     already lending to**, because the whole task is dividing eight people
//     between two or three slots and a conflict is invisible otherwise;
//   * WHAT CHANGED, when an assignment lands, said as the slot that changed
//     rather than as "swapped", since a swap moves two slots at once and the
//     player needs both.
//
// `/` reads the whole board back: every slot, then where the cursor is.

static uint8_t* s_lswModule   = nullptr;
static bool     s_lswActive   = false;
static bool     s_lswSlashWas = false;
static LagunaSwitchView s_lswPrev;

static void ResetLagunaSwitch()
{
    s_lswModule = nullptr;
    s_lswActive = false;
    memset(&s_lswPrev, 0, sizeof(s_lswPrev));
}

// Walk the MRU list for the module whose update function is this screen's state
// machine. Bounded by the pool so a corrupt pointer cannot walk off into the
// process; capped at 12 hops so a cycle cannot hang the game thread.
static uint8_t* FindLagunaSwitchModule()
{
    __try {
        uint8_t* m = *(uint8_t* volatile*)LSW_LIST_HEAD;
        for (int i = 0; i < 12 && m; i++) {
            const uintptr_t a = (uintptr_t)m;
            if (a < LSW_POOL_BASE || a >= LSW_POOL_END) break;
            if ((a - LSW_POOL_BASE) % LSW_POOL_STRIDE != 0) break;
            if (*(uint32_t*)(m + LSW_MOD_UPDATE_FN) == LSW_UPDATE_FN) return m;
            m = *(uint8_t* volatile*)(m + LSW_MOD_NEXT);
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) { return nullptr; }
    return nullptr;
}

// POD + SEH (MSVC C2712 -- tests/lint_seh.py): no std::string in this frame.
static bool LswFillView(uint8_t* mod, LagunaSwitchView* v)
{
    bool ok = false;
    __try {
        v->state      = *(volatile uint16_t*)(mod + LSWO_STATE);
        v->availMask  = *(volatile uint16_t*)(mod + LSWO_AVAIL_MASK);
        v->focus      = *(volatile uint8_t*)(mod + LSWO_FOCUS);
        v->gridCursor = *(volatile uint8_t*)(mod + LSWO_GRID_CUR);
        v->slotCursor = *(volatile uint8_t*)(mod + LSWO_SLOT_CUR);
        v->pending    = *(volatile uint8_t*)(mod + LSWO_PENDING);
        v->destSlot   = *(volatile uint8_t*)(mod + LSWO_DEST_SLOT);
        v->page       = *(volatile uint8_t*)(mod + LSWO_PAGE);
        v->pendFocus  = *(volatile uint8_t*)(mod + LSWO_PEND_FOCUS);
        v->pendGrid   = *(volatile uint8_t*)(mod + LSWO_PEND_GRID);
        v->pendSlot   = *(volatile uint8_t*)(mod + LSWO_PEND_SLOT);
        for (int i = 0; i < LSW_SLOT_COUNT; i++)
            v->slots[i] = *(volatile uint8_t*)(mod + LSWO_SLOTS + i);
        for (int i = 0; i < LSW_GRID_CELLS; i++)
            v->grid[i] = *(volatile uint8_t*)(mod + LSWO_GRID + i);
        v->slotMask   = *(volatile uint8_t*)LSW_SLOT_MASK_ADDR;
        ok = true;
    } __except(EXCEPTION_EXECUTE_HANDLER) { ok = false; }
    return ok;
}

static void LswSay(const char* text, bool interrupt)
{
    if (!text || !*text) return;
    ScreenReader::Speak(text, interrupt);
    Log::Menu("[LagunaSwitch] \"%s\"", text);
}

static void LswSayCursor(bool interrupt)
{
    char line[192];
    LswCursorLine(s_lswPrev, line, sizeof(line));
    if (line[0]) LswSay(line, interrupt);
}

// "/" -- the whole board, then where you are.
static void LswSayEverything()
{
    char slots[256], cur[192], full[512];
    LswAllSlotsLine(s_lswPrev, slots, sizeof(slots));
    LswCursorLine(s_lswPrev, cur, sizeof(cur));
    if (LswPickHeld(s_lswPrev)) {
        char dest[224];
        LswDestinationLine(s_lswPrev, dest, sizeof(dest));
        snprintf(full, sizeof(full), "%s %s", slots, dest[0] ? dest : cur);
    } else {
        snprintf(full, sizeof(full), "%s Cursor on %s", slots, cur[0] ? cur : "nothing.");
    }
    LswSay(full, true);
}

static void PollLagunaSwitch()
{
    uint8_t* mod = FindLagunaSwitchModule();

    if (!mod) {
        if (s_lswActive) Log::Menu("[LagunaSwitch] screen closed");
        if (s_lswActive || s_lswModule) ResetLagunaSwitch();
        return;
    }

    LagunaSwitchView v;
    memset(&v, 0, sizeof(v));
    if (!LswFillView(mod, &v)) return;

    if (!s_lswActive) {
        s_lswActive = true;
        s_lswModule = mod;
        s_lswPrev   = v;
        Log::Menu("[LagunaSwitch] opened: module=0x%08X state=%d availMask=0x%02X "
                  "slotMask=0x%02X mode=%d dest=%d page=%d "
                  "slots=%02X,%02X,%02X grid=%02X %02X %02X %02X %02X %02X %02X %02X",
                  (unsigned)(uintptr_t)mod, v.state, v.availMask, v.slotMask,
                  v.pending, v.destSlot, v.page,
                  v.slots[0], v.slots[1], v.slots[2],
                  v.grid[0], v.grid[1], v.grid[2], v.grid[3],
                  v.grid[4], v.grid[5], v.grid[6], v.grid[7]);

        char slots[256], intro[512];
        LswAllSlotsLine(v, slots, sizeof(slots));
        snprintf(intro, sizeof(intro),
                 "Junction party. Choose who lends their junctions. %s "
                 "Directional buttons to move, confirm to pick up and place. "
                 "Press slash to hear the whole board.", slots);
        LswSay(intro, false);
        LswSayCursor(false);
        return;
    }

    // "/" -- bound only while this module is in the pool, so it cannot collide.
    const bool slashDown = (GetAsyncKeyState(VK_OEM_2) & 0x8000) != 0;
    if (slashDown && !s_lswSlashWas) { s_lswPrev = v; LswSayEverything(); }
    s_lswSlashWas = slashDown;

    // ---- an assignment landed --------------------------------------------
    //
    // Said as the slots that changed, not as "swapped": a swap moves two of
    // them and the player needs to hear both. Checked before the cursor so the
    // result of a press is heard before wherever the cursor came to rest.
    bool slotChanged = false;
    for (int i = 0; i < LSW_SLOT_COUNT; i++)
        if (v.slots[i] != s_lswPrev.slots[i]) slotChanged = true;

    if (slotChanged) {
        char msg[384] = "";
        for (int i = 0; i < LSW_SLOT_COUNT; i++) {
            if (v.slots[i] == s_lswPrev.slots[i]) continue;
            if (!LswSlotSelectable(v.slotMask, i)) continue;
            char one[96];
            LswSlotLine(v, i, one, sizeof(one));
            if (msg[0]) LswAppend(msg, sizeof(msg), " ");
            LswAppend(msg, sizeof(msg), one);
        }
        if (msg[0]) LswSay(msg, true);
    }

    // ---- a pick was lifted, or put down ------------------------------------
    const bool lifted = LswPickHeld(v) && !LswPickHeld(s_lswPrev);
    if (lifted) {
        char line[192];
        LswPendingLine(v, line, sizeof(line));
        if (line[0]) LswSay(line, !slotChanged);
    }

    // ---- the cursor moved --------------------------------------------------
    //
    // v0.44.0: through the LIVE cursor, which is the second trio while a pick is
    // held. v0.43.0 watched the first trio throughout, and the first trio FREEZES
    // the moment a pick is lifted -- so the whole second half of every placement
    // was silent. See laguna_switch_model.inl.
    const int liveFocus = LswLiveFocus(v),      prevFocus = LswLiveFocus(s_lswPrev);
    const int liveGrid  = LswLiveGrid(v),       prevGrid  = LswLiveGrid(s_lswPrev);
    const int liveSlot  = LswLiveSlot(v),       prevSlot  = LswLiveSlot(s_lswPrev);
    const bool moved = (liveFocus != prevFocus) ||
                       (liveFocus == 0 && liveGrid != prevGrid) ||
                       (liveFocus != 0 && liveSlot != prevSlot);

    s_lswPrev = v;

    // The lift itself changes which pair is live, which is not a move; say the
    // pick, then where the cursor now is, and do not let the two collide.
    if ((moved || lifted) && !slotChanged) {
        // With a pick in hand the useful line is the DESTINATION -- the slot
        // under the second cursor and who would come back the other way, since
        // this is a swap.
        if (LswPickHeld(s_lswPrev)) {
            char line[224];
            LswDestinationLine(s_lswPrev, line, sizeof(line));
            if (line[0]) LswSay(line, !lifted);
            else         LswSayCursor(!lifted);
        } else {
            LswSayCursor(!lifted);
        }
    }
}
