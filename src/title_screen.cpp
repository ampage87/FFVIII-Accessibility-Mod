// title_screen.cpp - Title screen menu accessibility
//
// FF8 title screen has 3 options: New Game (0), Continue (1), Credits (2).
// The game defaults cursor to Continue (index 1).
//
// v02.00: Production build. Reads cursor position directly from game memory
//         at pTitleCursorPos (pMenuStateA + 0x1F6), discovered via diag6b
//         memory sweep. No more keyboard tracking or guessing.

#include "ff8_accessibility.h"

namespace TitleScreen {

// Title menu items indexed by cursor value
static const wchar_t* MENU_ITEMS[] = {
    L"New Game",
    L"Continue",
    L"Credits"
};
static const int MENU_ITEM_COUNT = 3;

static bool s_active = false;
static bool s_announced = false;
static int  s_lastCursor = -1;  // tracks previous cursor for change detection

void Initialize()
{
    s_active = false;
    s_announced = false;
    s_lastCursor = -1;
    Log::Write("TitleScreen: Module initialized (v02.00 - memory read).");
}

void Activate()
{
    if (!s_active) {
        s_active = true;
        s_announced = false;
        s_lastCursor = -1;
        Log::Write("TitleScreen: Activated.");
    }
}

void Deactivate()
{
    if (s_active) {
        s_active = false;
        s_announced = false;
        s_lastCursor = -1;
        Log::Write("TitleScreen: Deactivated.");
    }
}

void Update()
{
    if (!s_active)
        return;

    // Read cursor position from game memory
    uint8_t cursorRaw = FF8Addresses::GetTitleCursorPos();
    int cursor = (int)cursorRaw;

    // Validate range (should be 0-2)
    if (cursor < 0 || cursor >= MENU_ITEM_COUNT) {
        // Invalid or unresolved — skip this frame
        return;
    }

    // First frame on title screen: announce it
    if (!s_announced) {
        wchar_t msg[128];
        wsprintfW(msg, L"Title Screen. %s", MENU_ITEMS[cursor]);
        ScreenReader::Output(msg, true);
        Log::Write("TitleScreen: Announced title screen, cursor at %d (%ls).",
                   cursor, MENU_ITEMS[cursor]);
        s_announced = true;
        s_lastCursor = cursor;
        return;
    }

    // Detect cursor movement
    if (cursor != s_lastCursor) {
        Log::Write("TitleScreen: Cursor moved %d -> %d (%ls)",
                   s_lastCursor, cursor, MENU_ITEMS[cursor]);
        ScreenReader::Output(MENU_ITEMS[cursor], true);
        s_lastCursor = cursor;
    }

    // F5 = re-announce current position
    if (GetAsyncKeyState(VK_F5) & 1) {
        Log::Write("TitleScreen: F5 re-announce, cursor at %d", cursor);
        wchar_t msg[64];
        wsprintfW(msg, L"Cursor on: %s", MENU_ITEMS[cursor]);
        ScreenReader::Output(msg, true);
    }
}

void Shutdown()
{
    s_active = false;
    Log::Write("TitleScreen: Shutdown.");
}

}  // namespace TitleScreen
