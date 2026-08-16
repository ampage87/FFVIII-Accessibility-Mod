// menu_tts_hotkeys.inl — Help bar, Gil, Time, Location, SeeD rank hotkeys
// Included from menu_tts.cpp. Do not compile independently.
// v0.12.18: Extracted for readability.

// v0.22.1 (#81): defined in menu_tts_magic.inl, which is included after this
// file. Returns true when it has spoken, i.e. when the Magic module is open.
static bool AnnounceMagicHelpText();

static void AnnounceHelpText()
{
    // v0.22.1 (#81): **the Magic screen's help bar was invisible to "/".** The
    // scrape below looks for a dash separator or a known prefix in the RENDERED
    // text, and the Magic bar matches neither -- so the key did nothing there.
    // It never needed the scrape: the Magic module holds a pointer to the very
    // string the bar is drawing, straight into the loaded mngrp.bin. Reading
    // that is exact rather than reconstructed, and it is the game's own wording.
    if (AnnounceMagicHelpText()) return;

    // Snapshot GCW buffer and decode
    uint8_t gcwBuf[1024];
    int gcwLen = FieldDialog::SnapshotGcwBuffer(gcwBuf, sizeof(gcwBuf));
    if (gcwLen <= 0) {
        ScreenReader::Speak("No help text", true);
        Log::Menu("[MenuTTS] HelpText: GCW buffer empty");
        return;
    }
    
    std::string decoded = FF8TextDecode::DecodeMenuText(gcwBuf, gcwLen);
    if (decoded.empty()) {
        ScreenReader::Speak("No help text", true);
        return;
    }
    
    // Strategy: The help bar text sits between a "--------" dash separator
    // and the first character name that follows it. In deep submenus the
    // render order is: [menu items][submenu items][dashes][HELP TEXT][char name].
    // On the top-level menu it's: [menu items][HELP TEXT][char name].
    // We try the dash strategy first (works in submenus), then fall back
    // to extracting after the static prefix (works on top-level).
    
    // Find the FIRST occurrence of the dash separator within one render cycle.
    // One cycle starts at the menu prefix; find prefix first to bound the search.
    const char* prefix = GetMenuItemsPrefix();
    size_t prefixPos = decoded.find(prefix);
    size_t cycleStart = (prefixPos != std::string::npos) ? prefixPos : 0;
    
    // Find next cycle to bound our search (don't read into repeated frames)
    size_t nextCycle = std::string::npos;
    if (prefixPos != std::string::npos) {
        nextCycle = decoded.find(prefix, prefixPos + strlen(prefix));
    }
    size_t searchEnd = (nextCycle != std::string::npos) ? nextCycle : decoded.size();
    
    // Look for dash separator within this cycle
    static const char* DASH_SEP = "----";
    size_t dashPos = decoded.find(DASH_SEP, cycleStart);
    
    size_t helpStart = std::string::npos;
    
    if (dashPos != std::string::npos && dashPos < searchEnd) {
        // Skip past the dashes to find where help text starts
        helpStart = dashPos;
        while (helpStart < searchEnd && decoded[helpStart] == '-')
            helpStart++;
        // v0.09.42: If the text right after dashes starts with a character name,
        // there's no help text (e.g. Ability Junction with empty slot).
        for (const char** m = HELP_END_MARKERS; *m != nullptr; m++) {
            size_t nameLen = strlen(*m);
            if (helpStart + nameLen <= searchEnd &&
                decoded.compare(helpStart, nameLen, *m) == 0) {
                ScreenReader::Speak("Choose Ability", true);
                Log::Menu("[MenuTTS] HelpText: empty slot (char name '%s' at dash end)", *m);
                return;
            }
        }
    } else if (prefixPos != std::string::npos) {
        // No dashes — top-level menu. Help text starts right after the prefix.
        helpStart = prefixPos + strlen(prefix);
    }
    
    if (helpStart == std::string::npos || helpStart >= searchEnd) {
        ScreenReader::Speak("No help text", true);
        Log::Menu("[MenuTTS] HelpText: no start found");
        return;
    }
    
    // Find where help text ends: first character name after helpStart
    size_t helpEnd = searchEnd;
    for (const char** m = HELP_END_MARKERS; *m != nullptr; m++) {
        size_t pos = decoded.find(*m, helpStart);
        if (pos != std::string::npos && pos >= helpStart && pos < helpEnd)
            helpEnd = pos;
    }
    
    // Also stop at next dash separator (if there's another one)
    size_t nextDash = decoded.find(DASH_SEP, helpStart);
    if (nextDash != std::string::npos && nextDash < helpEnd)
        helpEnd = nextDash;
    
    // Clamp length
    if (helpEnd <= helpStart)
        helpEnd = helpStart + 100;
    if (helpEnd > searchEnd)
        helpEnd = searchEnd;
    
    std::string helpText = decoded.substr(helpStart, helpEnd - helpStart);
    
    // Trim trailing spaces
    while (!helpText.empty() && helpText.back() == ' ')
        helpText.pop_back();
    
    if (helpText.empty()) {
        ScreenReader::Speak("No help text", true);
        Log::Menu("[MenuTTS] HelpText: extracted empty");
    } else {
        ScreenReader::Speak(helpText.c_str(), true);
        Log::Menu("[MenuTTS] HelpText: \"%s\"", helpText.c_str());
    }
}

// ============================================================================
// v0.08.21: Individual info hotkeys (G/T/R/L) for menu mode
// ============================================================================
static void AnnounceGil()
{
    uint32_t gil = *(uint32_t*)((uint8_t*)SAVEMAP_BASE + HDR_GIL);
    char buf[64];
    sprintf(buf, "%u Gil", gil);
    ScreenReader::Speak(buf, true);
    Log::Menu("[MenuTTS] Gil: %u", gil);
}

static void AnnouncePlayTime()
{
    // v0.08.27: Use live game timer at +0x0CCC instead of stale header at +0x0C.
    // Header time only syncs at save/load; live timer ticks every second.
    static const int LIVE_TIME_OFFSET = 0x0CCC;
    uint32_t timeSec = *(uint32_t*)((uint8_t*)SAVEMAP_BASE + LIVE_TIME_OFFSET);
    int hours = timeSec / 3600;
    int mins  = (timeSec % 3600) / 60;
    int secs  = timeSec % 60;
    char buf[64];
    if (hours > 0)
        sprintf(buf, "Play time: %d hours, %d minutes, %d seconds", hours, mins, secs);
    else
        sprintf(buf, "Play time: %d minutes, %d seconds", mins, secs);
    ScreenReader::Speak(buf, true);
    Log::Menu("[MenuTTS] Time: %u sec (%d:%02d:%02d)", timeSec, hours, mins, secs);
}

static void AnnounceLocation()
{
    uint16_t locId = *(uint16_t*)((uint8_t*)SAVEMAP_BASE + HDR_LOCATION_ID);
    const char* locName = GetLocationNameById(locId);
    char buf[128];
    if (locName)
        sprintf(buf, "%s", locName);
    else
        sprintf(buf, "Location %u", (unsigned)locId);
    ScreenReader::Speak(buf, true);
    Log::Menu("[MenuTTS] Location: %s (id=%u)", buf, (unsigned)locId);
}

static void AnnounceSeedRank()
{
    // SeeD rank is derived from SeeD points (a.k.a. SeeD experience) stored at
    // live savemap +0x0D6C (uint16). Each rank is 100 points, so rank =
    // points / 100. Ranks run 1..30 then "A" (the 31st). A non-SeeD / fresh game
    // reads below 100 (no rank).
    //
    // Offset confirmed empirically (Chapter 5, v0.17.9.0) by diffing three
    // decompressed .ff8 saves, anchored to the live savemap via Squall HP/EXP +
    // Gil + location (live offset == decompressed-file offset - 0x184):
    //   - pre-SeeD save:                  points = 500  (the documented base of
    //       the initial-rank formula, present before the Dollet exam grades it)
    //   - SeeD Rank 3 save, pre-salary:   points = 392  (392 / 100 = 3)
    //   - same save right after a salary: points = 383  (-9 = lose 10 per payment
    //       + 1 from a kill; matches the documented -10 SeeD-points-per-pay decay)
    // 392/100 = 3 matched the observed 7400->8900 Gil (= 1500 = Rank 3) salary.
    //
    // The previous read (+0xF94 + 0x08 = +0xF9C) was derived by summing section
    // sizes in a comment and never measured. That offset lands in the field-
    // variable block and reads dead zeros in every save examined, which is why
    // this hotkey always reported "No SeeD rank yet" regardless of actual rank.
    static const int SEED_POINTS_OFFSET    = 0x0D6C;  // uint16, live savemap-relative
    static const int SEED_SALARY_COUNT_OFF = 0x0CDE;  // uint16: salary payments received (0 until first pay)
    uint16_t seedPoints  = *(uint16_t*)((uint8_t*)SAVEMAP_BASE + SEED_POINTS_OFFSET);
    uint16_t salaryCount = *(uint16_t*)((uint8_t*)SAVEMAP_BASE + SEED_SALARY_COUNT_OFF);
    int rank = seedPoints / 100;  // each rank = 100 points
    // Pre-SeeD gate: before the Dollet field exam grades the player, the points
    // pool sits at exactly the formula base (500) and no salary has ever been
    // paid. Pre-promotion rank modifiers (e.g. showing the gunblade) are
    // DEFERRED -- applied at graduation, not to the live value -- so points
    // stays exactly 500 until promotion. Confirmed: the pre-SeeD save reads
    // points=500, salaryCount=0; both SeeD saves read salaryCount>0. So treat
    // (points == 500 && salaryCount == 0) as "not a SeeD yet" and avoid
    // announcing a false "Rank 5". A paid SeeD that happens to sit at exactly
    // 500 points still announces Rank 5 correctly (salaryCount > 0).
    if ((seedPoints == 500 && salaryCount == 0) || rank < 1) {
        ScreenReader::Speak("No SeeD rank yet", true);
        Log::Menu("[MenuTTS] SeeD: no rank (points=%u salaryCount=%u)",
                  (unsigned)seedPoints, (unsigned)salaryCount);
    } else if (rank >= 31) {
        // Rank 30 is the highest numbered rank; 3100+ points is Rank A.
        ScreenReader::Speak("SeeD Rank A", true);
        Log::Menu("[MenuTTS] SeeD: Rank A (points=%u)", (unsigned)seedPoints);
    } else {
        char buf[64];
        sprintf(buf, "SeeD Rank %d", rank);
        ScreenReader::Speak(buf, true);
        Log::Menu("[MenuTTS] SeeD: Rank %d (points=%u)", rank, (unsigned)seedPoints);
    }
}

// ============================================================================
// Update — called every frame from AccessibilityThread
// ============================================================================
