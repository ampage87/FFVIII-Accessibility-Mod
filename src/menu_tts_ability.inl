// menu_tts_ability.inl — Main-menu Ability screen TTS (#42), v0.18.1 chapter
// Included from menu_tts.cpp AFTER menu_tts_gf.inl so it can reuse
// GcwAbilityNames(), NormalizeAbilityToGcw(), GetAbilityName(), ABILITY_NAMES,
// pMenuStateA, and the Log/ScreenReader/FieldDialog/FF8TextDecode facilities.
// Do not compile independently.
//
// The main-menu "Ability" screen (top-level cursor 5) is the "Use GF ability"
// ACTION screen — using a GF menu/command ability (the *-RF refine family plus
// Med LV Up / Card Mod) from the menu. It is NOT a GF-pick / category / AP-learn
// screen — that is the GF screen (#41, done). Confirmed offsets (BAT 2026-06-01,
// isolated SUBMON pass), all relative to pMenuStateA:
//   +0x1E8 == 14         : Ability screen active (gate; analog of GF==4 / Junc==17)
//   +0x22E               : phase — 3 = ability list ; ~19–21 = selected ability's
//                          refine item list (Build 2)
//   +0x258 (+0x257 page) : ability-list cursor (0-based). +0x258 confirmed toggling
//                          0<->1 in lockstep with the help-text swap between the two
//                          abilities; +0x257 read too in case longer lists paginate.
//   +0x2DF               : refine item-list cursor (Build 2)
//
// Build 1 (v0.18.1.0–.3): the ABILITY-LIST phase. Announce the highlighted
// ability NAME on cursor move; the "/" key reads its help; empty padded slots
// say "Empty Ability Slot". Build 2 (v0.18.1.4): the refine ITEM-LIST phase
// (+0x22E >= 19, cursor +0x2DF) — announce the source item's name + quantity on
// move (read from the savemap inventory), and "/" reads the refine preview
// ("N will refine into M <Magic>") parsed from the GCW.

// Confirmation logging (ability-list parse + item-list inventory/preview cross-
// check). Gate off once Build 2 is confirmed; never delete.
#define ABIL_DIAG 1

static const int ABIL_GATE_OFFSET   = 0x1E8;   // == 14 on the Ability screen
static const int ABIL_PHASE_OFFSET  = 0x22E;   // 3 = ability list
static const int ABIL_CURSOR_P1_OFF = 0x257;   // ability-list cursor (page 1 candidate)
static const int ABIL_CURSOR_P2_OFF = 0x258;   // ability-list cursor (confirmed)
static const int ABIL_GATE_VALUE    = 14;      // +0x1E8 value on this screen
static const int ABIL_PHASE_LIST    = 3;       // +0x22E value on the ability list
static const int ABIL_PHASE_ITEM_MIN = 19;     // +0x22E >= this = refine item list (Build 2)
static const int ABIL_ITEM_CURSOR_OFF = 0x2DF; // refine item-list cursor (0-based)

// Menu-usable ("Use GF ability") ability id block: the *-RF refine family (97–113)
// plus Med LV Up (114) and Card Mod (115). Restricting the GCW name match to this
// block avoids colliding with menu-item tokens (GF/Item/Magic/Card = ids 20–25)
// and battle-only command abilities, which matters because these names contain
// internal spaces ("I Mag-RF"). Anything outside this range that ever shows up on
// the screen will surface as an unmatched/short list under ABIL_DIAG.
static const int ABIL_MENU_ID_LO = 97;
static const int ABIL_MENU_ID_HI = 115;

static bool  s_abilActive  = false;   // on the Ability screen (top-level cursor 5)
static int   s_abilLastSel = -1;      // last announced ability-list index (dedupe)
static int   s_abilPrev257 = -1;
static int   s_abilPrev258 = -1;
static DWORD s_abilPoll    = 0;

// (/) on-demand help: the ability currently under the list cursor. Valid only
// while the ability list is up (cleared off it) so "/" falls back to the normal
// help-bar reader everywhere else.
static bool s_abilSelValid     = false;
static char s_abilSelName[64]  = {};
static char s_abilSelDesc[192] = {};

// (Build 2) Refine item-list phase state: cursor dedupe + the refine preview
// stashed for the "/" reader (valid only while the item list is up).
static int   s_abilItemLastCur    = -1;
static DWORD s_abilItemPoll       = 0;
static bool  s_abilItemSelValid   = false;
static char  s_abilItemRefine[192] = {};

static void ResetAbilitySubmenuState()
{
    s_abilActive  = false;
    s_abilLastSel = -1;
    s_abilPrev257 = -1;
    s_abilPrev258 = -1;
    s_abilPoll    = 0;
    s_abilSelValid    = false;
    s_abilSelName[0]  = '\0';
    s_abilSelDesc[0]  = '\0';
    s_abilItemLastCur   = -1;
    s_abilItemPoll      = 0;
    s_abilItemSelValid  = false;
    s_abilItemRefine[0] = '\0';
}

// SEH raw read of the gate / phase / cursor bytes. No C++ objects (C2712-safe).
static bool AbilReadState(int* gate, int* phase, int* cur257, int* cur258)
{
    __try {
        uint8_t* ms = (uint8_t*)pMenuStateA;
        if (ms == nullptr) return false;
        *gate   = (int)ms[ABIL_GATE_OFFSET];
        *phase  = (int)ms[ABIL_PHASE_OFFSET];
        *cur257 = (int)ms[ABIL_CURSOR_P1_OFF];
        *cur258 = (int)ms[ABIL_CURSOR_P2_OFF];
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}

// Parse the displayed ability list out of the decoded GCW. The menu-ability
// names (ids 97–115) appear ONLY in the ability list — never in the menu bar
// ("GF"/"Item"/"Magic"/"Card" are ids 20–25, excluded) nor in the help text —
// so the list is simply the longest contiguous run of those names in the
// buffer. We FORWARD-SCAN and keep the rightmost-longest run; no reliance on a
// menu-token anchor (the menu bar scrolls — when the cursor sits on Ability the
// bar renders "GFAbilitySwitchCardConfigTutorialSave...", with Junction/Item/
// Magic/Status scrolled off, so an earlier "Junction"-anchored parse found
// nothing). Fills outIds in display order; *outListStart = decoded index where
// row 0 begins (used to slice the preceding help text). std::string -> no __try.
static int ParseAbilityList(const std::string& dec, uint8_t outIds[], int maxOut,
                            size_t* outListStart)
{
    *outListStart = std::string::npos;
    const std::vector<std::string>& names = GcwAbilityNames();

    uint8_t best[64]; int bestN = 0; size_t bestStart = std::string::npos;
    size_t i = 0;
    while (i < dec.size()) {
        // Longest menu-ability name matching at i (decides whether a run starts).
        int    mid = -1; size_t mlen = 0;
        for (int id = ABIL_MENU_ID_LO; id <= ABIL_MENU_ID_HI; id++) {
            const std::string& nm = names[id];
            size_t len = nm.size();
            if (len == 0 || len <= mlen || i + len > dec.size()) continue;
            if (dec.compare(i, len, nm) == 0) { mid = id; mlen = len; }
        }
        if (mid < 0) { i++; continue; }
        // Extend a run of back-to-back ability names from here.
        uint8_t run[64]; int rn = 0; size_t runStart = i;
        size_t j = i;
        while (j < dec.size() && rn < 64) {
            int    rid = -1; size_t rlen = 0;
            for (int id = ABIL_MENU_ID_LO; id <= ABIL_MENU_ID_HI; id++) {
                const std::string& nm = names[id];
                size_t len = nm.size();
                if (len == 0 || len <= rlen || j + len > dec.size()) continue;
                if (dec.compare(j, len, nm) == 0) { rid = id; rlen = len; }
            }
            if (rid < 0) break;
            run[rn++] = (uint8_t)rid;
            j += rlen;
        }
        if (rn >= bestN) {                 // rightmost run of the max length wins
            bestN = rn; bestStart = runStart;
            for (int k = 0; k < rn; k++) best[k] = run[k];
        }
        i = (j > i) ? j : i + 1;
    }
    if (bestN <= 0) return 0;
    *outListStart = bestStart;
    int count = (bestN < maxOut) ? bestN : maxOut;
    for (int k = 0; k < count; k++) outIds[k] = best[k];
    return count;
}

// SEH read of the refine item-list cursor (+0x2DF). C2712-safe.
static bool AbilReadItemCursor(int* cur)
{
    __try {
        uint8_t* ms = (uint8_t*)pMenuStateA;
        if (ms == nullptr) return false;
        *cur = (int)ms[ABIL_ITEM_CURSOR_OFF];
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}

// SEH read of savemap inventory slot `idx` (item id + quantity). The refine
// source-item list appears to be the full inventory in order; the ABIL_DIAG
// cross-check logs the GCW so the BAT can confirm this mapping. C2712-safe.
static bool AbilReadInvSlot(int idx, uint8_t* id, uint8_t* qty)
{
    __try {
        uint8_t* inv = (uint8_t*)SAVEMAP_BASE + ITEM_INVENTORY_OFFSET;
        *id  = inv[idx * 2];
        *qty = inv[idx * 2 + 1];
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}

// Extract the refine preview ("N will refine into M <Magic>") from the decoded
// GCW for the "/" reader. It sits between the selected ability name and the
// party panel; anchor on "will refine into", walk back over the leading
// "<count> " digits, and stop at the first party-name marker. Empty when the
// highlighted item can't be refined (no preview rendered). std::string -> no __try.
static std::string ParseRefinePreview(const std::string& dec)
{
    size_t w = dec.rfind("will refine into");
    if (w == std::string::npos) return std::string();
    size_t s = w;
    while (s > 0 && dec[s - 1] == ' ') s--;
    while (s > 0 && dec[s - 1] >= '0' && dec[s - 1] <= '9') s--;
    size_t e = dec.size();
    for (const char** m = HELP_END_MARKERS; *m; m++) {
        size_t p = dec.find(*m, w);
        if (p != std::string::npos && p < e) e = p;
    }
    if (e <= s) return std::string();
    std::string out = dec.substr(s, e - s);
    while (!out.empty() && out.back() == ' ') out.pop_back();
    return out;
}

// Refine item-list phase handler (Build 2). Announces the highlighted source
// item's name + quantity (from the savemap inventory) on +0x2DF move; stashes
// the refine preview for "/". std::string here -> the raw reads are isolated in
// the SEH helpers above.
static void PollAbilityItemList()
{
    DWORD now = GetTickCount();
    if (now - s_abilItemPoll < 80) return;
    s_abilItemPoll = now;

    int cur = -1;
    if (!AbilReadItemCursor(&cur)) return;
    if (cur < 0 || cur >= 198) { s_abilItemSelValid = false; return; }

    // Re-parse the refine preview each poll so the "/" reader tracks the cursor.
    uint8_t gcw[1024];
    int len = FieldDialog::SnapshotGcwBuffer(gcw, sizeof(gcw));
    std::string dec = (len > 0) ? FF8TextDecode::DecodeMenuText(gcw, len) : std::string();
    std::string preview = dec.empty() ? std::string() : ParseRefinePreview(dec);
    s_abilItemSelValid = true;
    snprintf(s_abilItemRefine, sizeof(s_abilItemRefine), "%s", preview.c_str());

    uint8_t id = 0, qty = 0;
    bool ok = AbilReadInvSlot(cur, &id, &qty);

#if ABIL_DIAG
    if (cur != s_abilItemLastCur) {
        const char* dn = (ok && id) ? GetItemName(id) : "(none)";
        Log::Menu("[ABILDIAG-ITEM] cur=%d invId=%u qty=%u name=\"%s\" preview=\"%s\"",
                  cur, (unsigned)id, (unsigned)qty, dn ? dn : "?", s_abilItemRefine);
        Log::Menu("[ABILDIAG-ITEM] gcw=\"%.300s\"", dec.c_str());
    }
#endif

    if (cur == s_abilItemLastCur) return;   // announce only on a move
    s_abilItemLastCur = cur;

    char out[256];
    if (!ok || id == 0) {
        snprintf(out, sizeof(out), "Empty");
    } else {
        const char* nm = GetItemName(id);
        if (nm) snprintf(out, sizeof(out), "%s, %u", nm, (unsigned)qty);
        else    snprintf(out, sizeof(out), "Item %u, %u", (unsigned)id, (unsigned)qty);
    }
    ScreenReader::Speak(out, true);
    Log::Menu("[MenuTTS] Refine item %d: id=%u qty=%u -> \"%s\"",
              cur, (unsigned)id, (unsigned)qty, out);
}

// Dispatched from MenuTTS::Update() while mode==6 and top-level cursor == 5.
// Branches on +0x22E: ability-list phase (==3) vs refine item-list phase (>=19).
static void PollAbilitySubmenu()
{
    if (!s_abilActive) {
        s_abilActive = true;
        Log::Menu("[ABILITY] entered Ability screen (top-level cursor 5)");
    }

    int gate = -1, phase = -1, c257 = -1, c258 = -1;
    if (!AbilReadState(&gate, &phase, &c257, &c258)) return;
    if (gate != ABIL_GATE_VALUE) return;          // not actually on the Ability screen

    if (phase != ABIL_PHASE_LIST) {
        if (phase >= ABIL_PHASE_ITEM_MIN) {
            // Drilled into a selected ability's refine source-item list (Build 2).
            // Re-arm the ability list for when we return; "/" now reads the refine
            // preview (set inside PollAbilityItemList), not the ability help.
            s_abilSelValid = false;
            s_abilLastSel  = -1;
            s_abilPrev257  = -1;
            s_abilPrev258  = -1;
            PollAbilityItemList();
            return;
        }
        // Transitioning between phases / leaving — nothing to announce.
        s_abilSelValid     = false;
        s_abilItemSelValid = false;
        s_abilLastSel     = -1;
        s_abilPrev257     = -1;
        s_abilPrev258     = -1;
        s_abilItemLastCur = -1;
        return;
    }

    // On the ability list: the refine item list isn't up, so "/" uses the ability
    // help (not the refine preview).
    s_abilItemSelValid = false;
    s_abilItemLastCur  = -1;

    DWORD now = GetTickCount();
    if (now - s_abilPoll < 80) return;
    s_abilPoll = now;

    uint8_t gcw[1024];
    int len = FieldDialog::SnapshotGcwBuffer(gcw, sizeof(gcw));
    if (len <= 0) return;
    std::string dec = FF8TextDecode::DecodeMenuText(gcw, len);
    if (dec.empty()) return;

    uint8_t ids[64];
    size_t  listStart = std::string::npos;
    int count = ParseAbilityList(dec, ids, 64, &listStart);

    // Help description = the text between the menu bar's trailing "Save" and the
    // first ability row (e.g. "Refine Water/Ice Magic from an item").
    std::string desc;
    if (listStart != std::string::npos && listStart > 0) {
        size_t sp = dec.rfind("Save", listStart);
        if (sp != std::string::npos) {
            size_t ds = sp + 4;
            if (listStart > ds) desc = dec.substr(ds, listStart - ds);
        }
    }

    bool ch257 = (c257 != s_abilPrev257);
    bool ch258 = (c258 != s_abilPrev258);

#if ABIL_DIAG
    if (ch257 || ch258 || s_abilLastSel < 0) {
        char lst[256]; int lp = 0;
        for (int i = 0; i < count && lp < 240; i++)
            lp += snprintf(lst + lp, sizeof(lst) - lp, "%u%s",
                           (unsigned)ids[i], (i + 1 < count) ? "," : "");
        Log::Menu("[ABILDIAG] phase=%d 257=%d 258=%d count=%d ids=[%s] help=\"%s\"",
                  phase, c257, c258, count, lst, desc.c_str());
    }
#endif

    s_abilPrev257 = c257;
    s_abilPrev258 = c258;

    if (count <= 0) return;   // parse miss (captured under ABIL_DIAG); nothing to say

    // Resolve the active cursor value: whichever byte just moved (prefer the
    // confirmed +0x258), else the current +0x258 when nothing has been announced
    // yet (fresh entry; +0x258 is true on the first poll because prev == -1).
    int cur = -1;
    if      (ch258) cur = c258;
    else if (ch257) cur = c257;
    else if (s_abilLastSel < 0) cur = c258;
    if (cur < 0) return;

    // Classify the focused row. 0..count-1 = a real ability; count..63 = an empty
    // slot (the list area pads with focusable blank rows below the abilities —
    // confirmed in the BAT reaching index 10 with count=2, help=""). >=64 is noise.
    // Dedupe key: real row -> its index; empty row -> 200 + index (so each empty
    // slot announces once as it's entered, and never collides with a real row).
    int  key;
    bool emptySlot;
    if      (cur < count) { key = cur;       emptySlot = false; }
    else if (cur < 64)    { key = 200 + cur; emptySlot = true;  }
    else return;

    if (key == s_abilLastSel) return;     // announce only on a real change
    s_abilLastSel = key;

    if (!emptySlot) {
        uint8_t id = ids[cur];
        const char* nm = GetAbilityName(id);
        // Stash for the "/" on-demand help re-read (help text; name as fallback).
        s_abilSelValid = true;
        snprintf(s_abilSelName, sizeof(s_abilSelName), "%s", nm ? nm : "Unknown");
        snprintf(s_abilSelDesc, sizeof(s_abilSelDesc), "%s", desc.c_str());
        ScreenReader::Speak(nm ? nm : "Unknown", true);
        Log::Menu("[MenuTTS] Ability row %d/%d: id=%u \"%s\"",
                  cur, count, (unsigned)id, nm ? nm : "Unknown");
    } else {
        // Empty slot: announce it, and stash it so "/" repeats "Empty Ability
        // Slot" (AbilitySpeakSelectedHelp falls back to the name when desc empty).
        s_abilSelValid = true;
        snprintf(s_abilSelName, sizeof(s_abilSelName), "Empty Ability Slot");
        s_abilSelDesc[0] = '\0';
        ScreenReader::Speak("Empty Ability Slot", true);
        Log::Menu("[MenuTTS] Ability row %d/%d: empty slot", cur, count);
    }
}

// (/) On-demand help for the ability under the list cursor. Bound after the GF
// handler in MenuTTS::Update(); returns true when it spoke so every other screen
// falls back to the normal help bar. Speaks the help description only (no name
// repeat — the row announce already gave the name); falls back to the name when
// no description was captured. Char[]/snprintf/Speak only.
static bool AbilitySpeakSelectedHelp()
{
    if (!s_abilActive) return false;
    // Refine item list up: "/" reads the refine preview ("N will refine into M X").
    if (s_abilItemSelValid) {
        char out[256];
        snprintf(out, sizeof(out), "%s",
                 s_abilItemRefine[0] ? s_abilItemRefine : "No refine information");
        ScreenReader::Speak(out, true);
        Log::Menu("[MenuTTS] Refine info (/): \"%s\"", out);
        return true;
    }
    if (!s_abilSelValid) return false;
    char out[256];
    if (s_abilSelDesc[0])
        snprintf(out, sizeof(out), "%s", s_abilSelDesc);
    else
        snprintf(out, sizeof(out), "%s", s_abilSelName);
    ScreenReader::Speak(out, true);
    Log::Menu("[MenuTTS] Ability help (/): \"%s\"", out);
    return true;
}
