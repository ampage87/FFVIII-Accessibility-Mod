// menu_tts_hotkeys.inl — Help bar, Gil, Time, Location, SeeD rank hotkeys
// Included from menu_tts.cpp. Do not compile independently.
// v0.12.18: Extracted for readability.

static void AnnounceHelpText()
{
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
    // SeeD rank is stored in field_h section of savemap.
    // field_h starts after: header(0x4C) + GFs(0x440) + chars(8*0x98=0x4C0)
    //   + shops(16*20=0x140) + limit_breaks(0x14) + items(0x198)
    // = 0x4C + 0x440 + 0x4C0 + 0x140 + 0x14 + 0x198 = 0xF94
    // field_h.seedExp is at field_h + 0x08 (uint16)
    // SeeD level = seedExp / 100 (approximate, game uses a lookup table)
    // For now, just report the raw seedExp value and compute approximate level.
    static const int FIELD_H_OFFSET = 0xF94;  // from savemap base
    static const int SEED_EXP_IN_FIELD_H = 0x08;  // uint16 within field_h
    uint16_t seedExp = *(uint16_t*)((uint8_t*)SAVEMAP_BASE + FIELD_H_OFFSET + SEED_EXP_IN_FIELD_H);
    if (seedExp == 0) {
        ScreenReader::Speak("No SeeD rank yet", true);
        Log::Menu("[MenuTTS] SeeD: no rank (seedExp=0)");
    } else {
        // SeeD level is roughly seedExp / 100, clamped 1-31
        int seedLvl = seedExp / 100;
        if (seedLvl < 1) seedLvl = 1;
        if (seedLvl > 31) seedLvl = 31;
        char buf[64];
        sprintf(buf, "SeeD Level %d", seedLvl);
        ScreenReader::Speak(buf, true);
        Log::Menu("[MenuTTS] SeeD: level %d (seedExp=%u)", seedLvl, (unsigned)seedExp);
    }
}

// ============================================================================
// Update — called every frame from AccessibilityThread
// ============================================================================
