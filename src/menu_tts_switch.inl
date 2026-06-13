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
