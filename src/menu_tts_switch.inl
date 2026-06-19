// menu_tts_switch.inl — Switch submenu TTS (#65). Included from menu_tts.cpp.
// Do not compile independently. No header guards / namespace (textual include).
//
// Discovery (v0.18.3.34 [SwitchDiag] BAT, 2026-06-12):
//   Subsystem byte  +0x1E8 == 10   identifies the Switch screen (Junction=17,
//                                  Status=5, Magic=3, Ability=14).
//   Focus byte      +0x22E:  12 = the two-option action bar
//                             2 = the member-select list
//   Action option   +0x25E:   0 = "Switch Member"   (help "Please make a party of 3")
//                             1 = "Junction Exchange" (help "Exchanges all that is junctioned")
//   The member list draws the 3 active members plus the highlighted candidate as
//   <Name>LVHP tokens after the help string; the 4th token is whoever the cursor
//   is on. We read those names from the rendered GCW text (speak-what's-shown),
//   which avoids the source/destination cursor bytes (still unmapped). The on-
//   screen LV/HP numbers bypass the menu-text pipeline, so we read them from
//   memory by char-id (v0.18.3.37), reusing AnnounceJuncCharSelect's proven path.
//
// Reuses CHAR_NAMES + ComputeCharLevel (defined earlier in the TU). No __try in
// PollSwitchSubmenu (std::string in scope — C2712); the raw stat reads live in
// the POD helper SwitchCharLevelHP, which has its own __try.

static bool        s_swSubActive   = false;
static uint8_t     s_swPrevFocus   = 0xFF;
static uint8_t     s_swPrevOption  = 0xFF;     // +0x25E on the action bar
static std::string s_swPrevCandidate;          // last announced member-list candidate
static std::string s_swPrevTrio;               // last announced active trio (swap detection)
static DWORD       s_swLastGcwMs   = 0;

static void ResetSwitchSubmenu()
{
    s_swSubActive = false;
    s_swPrevFocus = 0xFF;
    s_swPrevOption = 0xFF;
    s_swPrevCandidate.clear();
    s_swPrevTrio.clear();
}

// Level + HP for a character id (0-7), reusing AnnounceJuncCharSelect's reads:
// level from EXP (flat 1000/level), HP from the computed-stats slot when the
// char is in the live battle formation (savemap+0xAF0 -> 0x1CFF000), else from
// the menu HP array at pMenuStateA+0x71E (char-id indexed, covers benched).
// POD-only + SEH (no std::string here) so PollSwitchSubmenu stays C2712-safe.
static bool SwitchCharLevelHP(uint8_t charId, int& outLvl, uint16_t& outCur, uint16_t& outMax)
{
    bool ok = false;
    __try {
        uint8_t* sm = (uint8_t*)0x1CFDC5C;  // SAVEMAP_BASE
        uint32_t exp = *(uint32_t*)(sm + 0x48C + (int)charId * 0x98 + 0x04);
        outLvl = ComputeCharLevel(exp);

        uint16_t cur = 0, max = 0;
        uint8_t* party = sm + 0xAF0;
        for (int s = 0; s < 3; s++) {
            if (party[s] == charId) {
                uint8_t* cs = (uint8_t*)0x1CFF000 + s * 0x1D0;
                uint16_t csMax = *(uint16_t*)(cs + 0x174);
                if (csMax > 0 && csMax < 10000) { cur = *(uint16_t*)(cs + 0x172); max = csMax; }
                break;
            }
        }
        if (max == 0) {
            uint8_t* hp = (uint8_t*)pMenuStateA + 0x71E + (int)charId * 0x20;
            uint16_t dCur = *(uint16_t*)(hp + 0x00);
            uint16_t dMax = *(uint16_t*)(hp + 0x02);
            if (dMax > 0 && dMax < 10000) { cur = dCur; max = dMax; }
        }
        outCur = cur; outMax = max;
        ok = (max > 0);
    } __except(EXCEPTION_EXECUTE_HANDLER) { ok = false; }
    return ok;
}

// Collect up to maxN known character names (in order) in blob, with their
// char-ids (CHAR_NAMES is indexed by char-id). Scanning for the known names is
// robust against the "LVHP/" delimiter glyphs.
static int SwitchCollectNames(const std::string& blob, std::string* outNames, int* outIds, int maxN)
{
    int n = 0;
    size_t i = 0;
    while (i < blob.size() && n < maxN) {
        bool matched = false;
        for (int c = 0; c < 8; c++) {
            const char* nm = CHAR_NAMES[c];
            size_t len = strlen(nm);
            if (i + len <= blob.size() && blob.compare(i, len, nm) == 0) {
                outNames[n] = nm; outIds[n] = c; n++;
                i += len; matched = true; break;
            }
        }
        if (!matched) i++;
    }
    return n;
}

// Names (+ ids) after the LAST occurrence of [marker], capped before the next
// menu-bar word ("Ability"), which always follows the member list in the GCW.
static int SwitchParseMembers(const std::string& gcw, const char* marker,
                              std::string* outNames, int* outIds, int maxN)
{
    size_t m = gcw.rfind(marker);
    if (m == std::string::npos) return 0;
    size_t start = m + strlen(marker);
    size_t end = gcw.find("Ability", start);
    if (end == std::string::npos) end = gcw.size();
    return SwitchCollectNames(gcw.substr(start, end - start), outNames, outIds, maxN);
}

// "Name, active/reserve, Level N, HP X of Y." for the candidate at index idx.
// Active = the candidate is one of the 3 on-screen active members (names[0..2]).
// Falls back to "Name, active/reserve." if the LV/HP read fails.
static std::string SwitchCandidatePhrase(const std::string* names, const int* ids, int n, int idx)
{
    bool active = (idx < n) &&
                  (names[idx] == names[0] || names[idx] == names[1] || names[idx] == names[2]);
    std::string s = names[idx] + (active ? ", active" : ", reserve");
    int lvl = 0; uint16_t cur = 0, mx = 0;
    if (ids[idx] >= 0 && ids[idx] <= 7 && SwitchCharLevelHP((uint8_t)ids[idx], lvl, cur, mx)) {
        char buf[96];
        sprintf(buf, ", Level %d, HP %u of %u.", lvl, (unsigned)cur, (unsigned)mx);
        s += buf;
    } else {
        s += ".";
    }
    return s;
}

static void PollSwitchSubmenu()
{
    uint8_t* pmd = (uint8_t*)pMenuStateA;
    uint8_t focus  = pmd[0x22E];
    uint8_t option = pmd[0x25E];

    if (!s_swSubActive) {
        s_swSubActive = true;
        s_swPrevFocus = 0xFF;
        s_swPrevOption = 0xFF;
        s_swPrevCandidate.clear();
        s_swPrevTrio.clear();
    }

    // --- Action bar: the two options ---
    if (focus == 12) {
        if (option != s_swPrevOption || s_swPrevFocus != 12) {
            if (option == 1)
                ScreenReader::Speak("Junction Exchange. Exchanges all that is junctioned.", true);
            else
                ScreenReader::Speak("Switch Member. Please make a party of three.", true);
            s_swPrevOption = option;
        }
        // leaving a member list -> re-cue its context next time
        s_swPrevCandidate.clear();
        s_swPrevTrio.clear();
        s_swPrevFocus = focus;
        return;
    }

    // --- Member-select list ---
    if (focus == 2) {
        bool justEntered = (s_swPrevFocus != 2);
        DWORD now = GetTickCount();
        if (!justEntered && (now - s_swLastGcwMs < 100)) { s_swPrevFocus = focus; return; }
        s_swLastGcwMs = now;

        uint8_t gcw[2048];
        int len = FieldDialog::SnapshotGcwBuffer(gcw, sizeof(gcw));
        std::string text = (len > 0) ? FF8TextDecode::DecodeMenuText(gcw, len) : std::string();

        const char* marker = (option == 1) ? "Exchanges all that is junctioned" : "make a party of 3";
        std::string names[4];
        int ids[4] = { -1, -1, -1, -1 };
        int n = SwitchParseMembers(text, marker, names, ids, 4);

        std::string trio;
        if (n >= 3) trio = names[0] + ", " + names[1] + ", " + names[2];

        // On entering the list, announce context + the first candidate in one phrase.
        if (justEntered) {
            std::string s;
            if (option == 1)      s = "Junction Exchange. Choose a character. ";
            else if (n >= 3)      s = "Switch Member. Active party: " + trio + ". ";
            else                  s = "Switch Member. ";
            if (n >= 4) {
                s += SwitchCandidatePhrase(names, ids, n, 3);
                s_swPrevCandidate = names[3];
            }
            ScreenReader::Speak(s.c_str(), true);
            s_swPrevTrio = trio;
            s_swPrevFocus = focus;
            return;
        }

        // A completed swap changes the active trio — announce the new party.
        // Pin the candidate to the current slot so its echo does NOT interrupt the
        // "Party is now ..." line; it re-announces only on the next cursor move.
        if (n >= 3 && trio != s_swPrevTrio) {
            std::string s = "Party is now " + trio + ".";
            ScreenReader::Speak(s.c_str(), true);
            s_swPrevTrio = trio;
            if (n >= 4) s_swPrevCandidate = names[3];
        }
        // Otherwise announce the candidate under the cursor when it changes.
        else if (n >= 4 && names[3] != s_swPrevCandidate) {
            ScreenReader::Speak(SwitchCandidatePhrase(names, ids, n, 3).c_str(), true);
            s_swPrevCandidate = names[3];
        }
        s_swPrevFocus = focus;
        return;
    }

    // Transitional focus values — just track.
    s_swPrevFocus = focus;
}

// ============================================================================
// Forced party-select (#66) — the "make a party of 3" screen the story forces
// (e.g. Rinoa joining after the Fake President battle). v0.18.3.38/.39 discovery
// (BAT 2026-06-12) found it is the Switch Member screen run in GAME MODE 10 (not
// menu mode 6), so the shipped PollSwitchSubmenu gate (s_prevCursor==6 &&
// +0x1E8==10) never matched it. The GCW carries the same <Name>LVHP tokens
// (displayed active members, then the trailing highlighted candidate), and the
// savemap party +0xAF0 / EXP / the +0x71E HP array are all live, so this reuses
// the Switch helpers. Difference from the menu Switch: gate on game mode 10, and
// the active party may be fewer than 3 (you are adding members) — so the
// candidate is the LAST token and active/reserve comes from +0xAF0 (not a fixed
// names[0..2]).
// ============================================================================

static bool        s_fpsAnnActive = false;
static std::string s_fpsPrevOpt;       // "s"witch member / "j"unction exchange
static std::string s_fpsPrevCand;      // last announced candidate name
static std::string s_fpsPrevActive;    // last announced active-member list
static bool        s_fpsPrevPopup = false;
static int         s_fpsPrevFocus = -1;   // +0x1B6: 2=character grid, 0x0C=action bar

static void ResetForcedPartySelect()
{
    s_fpsAnnActive = false;
    s_fpsPrevOpt.clear();
    s_fpsPrevCand.clear();
    s_fpsPrevActive.clear();
    s_fpsPrevPopup = false;
    s_fpsPrevFocus = -1;
}

// POD + SEH: game mode (pGameMode is uint16_t*; forced select = 10).
static uint16_t ForcedPselGameMode()
{
    uint16_t m = 0xFFFF;
    __try { if (pGameMode) m = *(uint16_t*)pGameMode; } __except(EXCEPTION_EXECUTE_HANDLER) {}
    return m;
}

// POD + SEH: is char-id currently in the LIVE working active party? The savemap
// (+0xAF0) only commits on exit, so during the operation we read the in-progress
// 3 active slots at pMenuStateA+0x1EA (0xFF = empty). +0x1EC == slot 3 was
// confirmed by a placement swap (+0x1EC 05->01 = Selphie->Zell).
static bool ForcedPselIsActive(int id)
{
    bool active = false;
    __try {
        uint8_t* a = (uint8_t*)pMenuStateA + 0x1EA;
        for (int s = 0; s < 3; s++) { if (a[s] == id) { active = true; break; } }
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
    return active;
}

// POD + SEH: the menu's per-char state byte at pMenuStateA+0x1DB (id-indexed). On
// the forced screen, non-zero appears to mark a grayed-out / un-addable reserve
// (e.g. Quistis before Timber: +0x1DB[3]=3, available chars=0). HYPOTHESIS — the
// .41 BAT confirms by ear; the re-enabled probe logs the full array for backup.
static uint8_t ForcedPselMenuFlag(int id)
{
    uint8_t v = 0;
    __try { if (id >= 0 && id <= 7) v = *((uint8_t*)pMenuStateA + 0x1DB + id); } __except(EXCEPTION_EXECUTE_HANDLER) {}
    return v;
}

// "Name, active/reserve[, unavailable], Level N, HP X of Y." (active from +0xAF0;
// LV/HP via the shared SwitchCharLevelHP). Falls back to "Name, active/reserve."
static std::string ForcedPselCandidatePhrase(const std::string& name, int id, bool unavailable)
{
    std::string s = name + (ForcedPselIsActive(id) ? ", active" : ", reserve");
    if (unavailable) s += ", unavailable";
    int lvl = 0; uint16_t cur = 0, mx = 0;
    if (id >= 0 && id <= 7 && SwitchCharLevelHP((uint8_t)id, lvl, cur, mx)) {
        char buf[96];
        sprintf(buf, ", Level %d, HP %u of %u.", lvl, (unsigned)cur, (unsigned)mx);
        s += buf;
    } else {
        s += ".";
    }
    return s;
}

// POD + SEH: the LIVE working active party — 3 slot char-ids at pMenuStateA+0x1EA
// (0xFF = empty). This is the in-progress arrangement the screen shows; unlike
// savemap +0xAF0 it updates immediately as members are placed (savemap commits
// only on exit). +0x1EC == slot 3 confirmed by a placement swap; +0x1ED begins
// the reserves.
static void ForcedPselActiveSlots(int slots[3])
{
    slots[0] = slots[1] = slots[2] = 0xFF;
    __try {
        uint8_t* a = (uint8_t*)pMenuStateA + 0x1EA;
        slots[0] = a[0]; slots[1] = a[1]; slots[2] = a[2];
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
}

// POD + SEH: the menu cursor — column (+0x1E7: 0=active/left, 1=reserve/right),
// active-slot index (+0x1E8), reserve index (+0x1E9). These keep updating even
// when the GCW detail text freezes (e.g. after a member is placed), so the active
// column is announced from memory rather than the (sometimes stale) GCW token.
static void ForcedPselCursor(int& col, int& activeIdx, int& reserveIdx)
{
    col = -1; activeIdx = -1; reserveIdx = -1;
    __try {
        uint8_t* pmd = (uint8_t*)pMenuStateA;
        col        = pmd[0x1E7];
        activeIdx  = pmd[0x1E8];
        reserveIdx = pmd[0x1E9];
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
}

// POD + SEH: the action option at pMenuStateA+0x1E6 in game mode 10
// (0 = Switch Member, 1 = Junction Exchange). Authoritative and always current
// (the GCW marker text can lag a frame on a toggle). Confirmed by three clean
// 0<->1 flips that tracked the help text in the v0.18.3.44 BAT trace. Returns
// -1 on an unreadable/unexpected value so the caller can fall back to the GCW.
static int ForcedPselOption()
{
    int v = -1;
    __try { v = *((uint8_t*)pMenuStateA + 0x1E6); } __except(EXCEPTION_EXECUTE_HANDLER) {}
    return (v == 0 || v == 1) ? v : -1;
}

// POD + SEH: the live working RESERVE char-id at reserve grid index rIdx, read
// from pMenuStateA+0x1ED (reserves run contiguously after the 3 active slots at
// +0x1EA). The reserve portrait grid is 2 rows of 4 (indices 0..7); the cursor
// index +0x1E9 maps 1:1 onto this array (idx 0 = Zell, idx 1 = Quistis in the
// BAT). Returns -1 for an empty slot (0xFF), an out-of-range id, or oob index.
static int ForcedPselReserveId(int rIdx)
{
    int v = -1;
    __try {
        if (rIdx >= 0 && rIdx <= 7) {
            uint8_t b = *((uint8_t*)pMenuStateA + 0x1ED + rIdx);
            if (b <= 7) v = b;
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
    return v;
}

// POD + SEH: the action-bar-vs-character FOCUS byte at pMenuStateA+0x1B6 in game
// mode 10. 0x02 = the character grid is focused (the on-screen hand is on a
// character); 0x0C = the action bar is focused (hand up on Switch Member /
// Junction Exchange, no character cursor drawn; 0x0B is a transient while moving
// up). Found by the v0.18.3.46 isolated-motion BAT — it sits just below the old
// diagnostic window (+0x1C0), which is why earlier traces missed it.
static int ForcedPselFocus()
{
    int v = -1;
    __try { v = *((uint8_t*)pMenuStateA + 0x1B6); } __except(EXCEPTION_EXECUTE_HANDLER) {}
    return v;
}

static void PollForcedPartySelect()
{
    if (ForcedPselGameMode() != 10) { if (s_fpsAnnActive) ResetForcedPartySelect(); return; }
    bool justEntered = !s_fpsAnnActive;

    // --- Memory-first state (reliable even when the GCW detail text freezes) ---
    int col = -1, aIdx = -1, rIdx = -1;
    ForcedPselCursor(col, aIdx, rIdx);

    // Action-bar-vs-character focus (+0x1B6): 2 = on the character grid, 0x0C = on
    // the action bar (0x0B transient). When focus drops from the bar back onto a
    // character the on-screen cursor "appears" on it, so we announce that character
    // then (handled below) — picking an option and landing on a character reads it.
    int focus = ForcedPselFocus();
    bool onBar           = (focus == 0x0B || focus == 0x0C);
    bool returnedFromBar = (focus == 2 && (s_fpsPrevFocus == 0x0B || s_fpsPrevFocus == 0x0C));
    bool movedToBar      = (onBar && s_fpsPrevFocus == 2);
    s_fpsPrevFocus = focus;   // update now so early-return paths keep it current

    int slots[3];
    ForcedPselActiveSlots(slots);
    int K = 0;
    std::string activeList;
    for (int s = 0; s < 3; s++) {
        if (slots[s] >= 0 && slots[s] <= 7) {
            if (!activeList.empty()) activeList += ", ";
            activeList += CHAR_NAMES[slots[s]];
            K++;
        }
    }

    // --- GCW: used for the option line, the popup, and reserve names ---
    uint8_t gcw[2048];
    int len = FieldDialog::SnapshotGcwBuffer(gcw, sizeof(gcw));
    std::string text = (len > 0) ? FF8TextDecode::DecodeMenuText(gcw, len) : std::string();

    // "The party has not been set" popup (only meaningful on a populated frame).
    if (!text.empty() && text.find("party has not been set") != std::string::npos) {
        if (!s_fpsPrevPopup) { ScreenReader::Speak("The party has not been set.", true); s_fpsPrevPopup = true; }
        return;
    }
    if (!text.empty()) s_fpsPrevPopup = false;

    // Which top option is active. From a populated GCW; otherwise keep the last.
    bool hasMarker = !text.empty() &&
        (text.find("make a party of 3") != std::string::npos ||
         text.find("Exchanges all that is junctioned") != std::string::npos);
    if (justEntered && !hasMarker) return;   // wait for the screen text before the first announce

    // Action option from +0x1E6 (0=Switch Member, 1=Junction Exchange) — the
    // authoritative byte in game mode 10, always current. Fall back to the GCW
    // marker, then to the last known option, only if the byte is unreadable.
    int opt6 = ForcedPselOption();
    bool junction;
    if (opt6 == 0 || opt6 == 1) junction = (opt6 == 1);
    else if (hasMarker)         junction = (text.find("Exchanges all that is junctioned") != std::string::npos);
    else                        junction = (s_fpsPrevOpt == "j");
    const char* marker = junction ? "Exchanges all that is junctioned" : "make a party of 3";

    // Reserve name from the GCW detail token (names[K]); reserve column only,
    // best-effort (stale if the detail text is frozen — reserve memory map is TODO).
    std::string names[6];
    int ids[6] = { -1, -1, -1, -1, -1, -1 };
    int n = text.empty() ? 0 : SwitchParseMembers(text, marker, names, ids, 6);
    bool haveGcwCand = (K >= 0 && K < 6 && n > K);
    std::string gcwCand = haveGcwCand ? names[K] : std::string();
    int gcwCandId = haveGcwCand ? ids[K] : -1;

    // --- Highlighted entry, cursor-driven ---
    std::string hKey, hPhrase;
    bool haveH = false;
    if (col == 0) {                              // active column (left)
        if (aIdx >= 0 && aIdx < 3) {
            int id = slots[aIdx];                // the live working slot
            if (id >= 0 && id <= 7) {            // a filled active slot
                char kb[16]; sprintf(kb, "a%d", id);
                hKey = kb;
                hPhrase = ForcedPselCandidatePhrase(CHAR_NAMES[id], id, false);
                haveH = true;
            } else {                             // an empty active slot
                char kb[16]; sprintf(kb, "aempty%d", aIdx);
                hKey = kb;
                hPhrase = "Empty Party Slot.";
                haveH = true;
            }
        }
    } else if (col == 1) {                       // reserve column (right)
        int rid = ForcedPselReserveId(rIdx);     // live working reserve id (-1 if empty)
        if (haveGcwCand) {                       // GCW shows a candidate (proven path)
            bool unavail = !junction && (ForcedPselMenuFlag(gcwCandId) != 0);
            char kb[16]; sprintf(kb, "r%d", gcwCandId);
            hKey = kb;
            hPhrase = ForcedPselCandidatePhrase(gcwCand, gcwCandId, unavail);
            haveH = true;
        } else if (rid >= 0 && rid <= 7) {       // GCW token absent but a member is here (frozen detail text)
            bool unavail = !junction && (ForcedPselMenuFlag(rid) != 0);
            char kb[16]; sprintf(kb, "r%d", rid);
            hKey = kb;
            hPhrase = ForcedPselCandidatePhrase(CHAR_NAMES[rid], rid, unavail);
            haveH = true;
        } else if (rIdx >= 0) {                  // confirmed empty reserve slot
            char kb[24]; sprintf(kb, "rempty%d", rIdx);
            hKey = kb;
            hPhrase = "Empty Reserve Slot.";
            haveH = true;
        }
    }

    // --- Entry ---
    if (justEntered) {
        s_fpsAnnActive = true;
        std::string s = junction
            ? std::string("Junction Exchange. Exchanges all that is junctioned. ")
            : std::string("Select party members. Please make a party of three. ");
        if (!junction && !activeList.empty()) s += "Active party: " + activeList + ". ";
        if (haveH) s += hPhrase;
        ScreenReader::Speak(s.c_str(), true);
        s_fpsPrevOpt = junction ? "j" : "s";
        s_fpsPrevActive = activeList;
        s_fpsPrevCand = hKey;
        return;
    }

    // --- Option toggle (Switch Member <-> Junction Exchange) ---
    // Speak ONLY the option name here, never the focused character. Toggling
    // between the two options keeps the same character highlighted (the cursor
    // col/idx don't move — confirmed in the v0.18.3.45 trace), so reading the
    // character on every flip is noise. The character is announced by the
    // cursor-move branch when you actually move onto/among characters; the
    // cursor state is re-synced here so that branch doesn't double-fire.
    std::string optKey = junction ? "j" : "s";
    if (optKey != s_fpsPrevOpt) {
        ScreenReader::Speak(junction ? "Junction Exchange." : "Switch Member.", true);
        s_fpsPrevOpt = optKey;
        s_fpsPrevActive = activeList;
        s_fpsPrevCand = hKey;
        return;
    }

    // --- Active party changed (a member was placed/swapped) ---
    if (activeList != s_fpsPrevActive) {
        if (!activeList.empty()) ScreenReader::Speak(("Party is now " + activeList + ".").c_str(), true);
        s_fpsPrevActive = activeList;
        s_fpsPrevCand = hKey;
        return;
    }

    // --- Moved onto the action bar: announce the current option ---
    // The character cursor disappears when focus goes up to the bar, so cue the
    // action item now highlighted (mirror of the reverse cue below).
    if (movedToBar) {
        ScreenReader::Speak(junction ? "Junction Exchange." : "Switch Member.", true);
        s_fpsPrevOpt = junction ? "j" : "s";
        s_fpsPrevCand = hKey;
        return;
    }

    // --- Returned from the action bar onto a character ---
    // While on the bar the character cursor is hidden but its value persists, so
    // dropping back onto a character makes the on-screen cursor reappear. Announce
    // that character (even if it's the same slot we left), which is what the player
    // wants after picking Switch Member / Junction Exchange.
    if (returnedFromBar) {
        if (haveH) ScreenReader::Speak(hPhrase.c_str(), true);
        s_fpsPrevCand = hKey;
        return;
    }

    // --- Cursor moved to a new entry ---
    if (haveH && hKey != s_fpsPrevCand) {
        ScreenReader::Speak(hPhrase.c_str(), true);
        s_fpsPrevCand = hKey;
    }
}
