// world_map_keys.inl - Hotkey polling
//
// PART OF world_map.cpp -- TEXTUAL INCLUDE. Do NOT compile standalone.
//
// PollKeys reads `+/-/=` (cycle catalog), Backspace (announce bearing),
// `\` (start/cancel auto-drive). All F-key gating happens elsewhere
// (PollKeys handles only the world-map-specific hotkeys).

// ============================================================================
// Keyboard input polling (v0.14.83)
// ============================================================================
static void PollKeys()
{
    static bool s_minusWas = false;
    static bool s_plusWas  = false;
    static bool s_bkspWas  = false;
    static bool s_bslashWas = false;

    bool minus  = (GetAsyncKeyState(VK_OEM_MINUS) & 0x8000) != 0;
    bool plus   = (GetAsyncKeyState(VK_OEM_PLUS)  & 0x8000) != 0;
    bool bksp   = (GetAsyncKeyState(VK_BACK)      & 0x8000) != 0;
    bool bslash = (GetAsyncKeyState(VK_OEM_5)     & 0x8000) != 0;  // '\' key

    if (minus && !s_minusWas) {
        if (s_catalogBuilt && s_catalogCount > 0) {
            s_catalogIndex = (s_catalogIndex - 1 + s_catalogCount) % s_catalogCount;
            AnnounceLocation(s_catalogIndex);
            Log::World("WorldMap: [KEY] minus -> idx %d (%s)",
                       s_catalogIndex, s_catalog[s_catalogIndex].name);
        }
    }
    if (plus && !s_plusWas) {
        if (s_catalogBuilt && s_catalogCount > 0) {
            s_catalogIndex = (s_catalogIndex + 1) % s_catalogCount;
            AnnounceLocation(s_catalogIndex);
            Log::World("WorldMap: [KEY] plus -> idx %d (%s)",
                       s_catalogIndex, s_catalog[s_catalogIndex].name);
        }
    }
    if (bksp && !s_bkspWas) {
        AnnounceBearing();
        Log::World("WorldMap: [KEY] backspace bearing");
    }
    if (bslash && !s_bslashWas) {
        // v0.14.86: toggle auto-drive. If a drive is already running, cancel it;
        // otherwise start a drive toward the currently-selected catalog entry.
        // #80: while piloting the mobile Garden the backslash key drives the
        // GARDEN subsystem instead. It is a separate executor on a separate
        // grid; nothing in the foot/car drive is reached in this branch.
        if (Garden_IsAboard()) {
            Log::World("WorldMap: [KEY] backslash -> Garden toggle (idx %d)", s_catalogIndex);
            if (s_driveActive) StopAutoDrive(nullptr);   // never run both
            Garden_Toggle();
        } else if (s_driveActive) {
            StopAutoDrive("Cancelled.");
            Log::World("WorldMap: [KEY] backslash -> cancel");
        } else if (s_catalogBuilt && s_catalogCount > 0) {
            Log::World("WorldMap: [KEY] backslash -> start drive to idx %d (%s)",
                       s_catalogIndex, s_catalog[s_catalogIndex].name);
            StartAutoDrive(s_catalogIndex);
        } else {
            ScreenReader::Speak("No locations available.", true);
            Log::World("WorldMap: [KEY] backslash -> no catalog");
        }
    }

    s_minusWas  = minus;
    s_plusWas   = plus;
    s_bkspWas   = bksp;
    s_bslashWas = bslash;
}
