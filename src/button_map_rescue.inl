// button_map_rescue.inl -- v0.25.1 (#84)
//
// A one-key recovery for FF8's in-game button remap (Shift+F9). PART OF dinput8.cpp --
// TEXTUAL INCLUDE. Do NOT compile standalone.
//
// ---------------------------------------------------------------------------
// WHY THIS EXISTS
//
// Aaron, testing the Config screen: *"I think I inadvertently switched the
// controls. I tried to switch the option back to Normal but it still doesn't
// seem correct. For example X is no longer Confirm, Z is."*
//
// The Config screen's Controller row has exactly two settings, and the second
// one -- Customize -- opens a screen where **Cancel does nothing** (the state-7
// handler never reads the Cancel bit) and where the Steam port arms a "press the
// button you want" rebinder. A blind player who wanders in there and presses
// keys to find the way out is remapping their controls with every press, and the
// only documented way back is a button whose keyboard equivalent is not written
// down anywhere in the game.
//
// That is a trap the mod put him within one keystroke of, so the mod carries the
// ladder out.
//
// WHAT THE GAME DOES, reproduced exactly:
//   * 0x004EE007 (the Customize screen's "defaults" action) writes
//       for (i = 0; i < 12; i++) [0x01CFE740 + i] = i + 1;
//       flags &= ~0x0080;
//     and then calls 0x004C3010.
//   * 0x004A2D60 -- the function every button read passes through -- returns the
//     input word COMPLETELY UNTOUCHED unless flag bit 0x0020 is set. Its first
//     two instructions are `test byte [0x1CFE73C], 0x20 / jne`. **So clearing
//     that bit is on its own sufficient to restore stock controls**, and this
//     rescue clears it as well as rewriting the map -- belt and braces, because
//     a player in this situation should not have to trust one of the two.
//
// 0x004C3010 is NOT called from here. It is the analog/stick refresh and the mod
// does not call into the game from its own thread on principle; the map and the
// flags are plain data the game re-reads every frame.
// ---------------------------------------------------------------------------

namespace ButtonMapRescue {

static const uintptr_t BMR_FLAGS   = 0x01CFE73C;   // u16 config flag word
static const uintptr_t BMR_MAP     = 0x01CFE740;   // u8[12] button assignment
static const unsigned  BMR_CUSTOM  = 0x0020;       // Controller = Customize
static const unsigned  BMR_SWAPLR  = 0x0080;       // L/R analog swapped

// True when the map is the identity the game ships with.
//
// **NOT static, unlike everything else here.** menu_tts.cpp needs it for the
// Config screen's Controller row, and that is a different translation unit --
// this file is included only by dinput8.cpp. v0.25.1 shipped it static and the
// MSVC build failed with C2653 on `ButtonMapRescue`, which neither host probe
// could catch: each of them STUBS the namespace it does not own, so both were
// perfectly happy. A stub is a statement about an interface, not evidence that
// the interface exists.
bool IsDefault()
{
    __try {
        const uint8_t* m = (const uint8_t*)BMR_MAP;
        for (int i = 0; i < 12; i++) if (m[i] != (uint8_t)(i + 1)) return false;
        return (*(const uint16_t*)BMR_FLAGS & (BMR_CUSTOM | BMR_SWAPLR)) == 0;
    } __except(EXCEPTION_EXECUTE_HANDLER) { return true; }
}

// Returns 0 = restored, 1 = already default, -1 = could not read/write.
static int Restore()
{
    __try {
        uint8_t*  m = (uint8_t*)BMR_MAP;
        uint16_t* f = (uint16_t*)BMR_FLAGS;
        bool wasDefault = true;
        for (int i = 0; i < 12; i++) if (m[i] != (uint8_t)(i + 1)) wasDefault = false;
        if ((*f & (BMR_CUSTOM | BMR_SWAPLR)) != 0) wasDefault = false;
        if (wasDefault) return 1;

        for (int i = 0; i < 12; i++) m[i] = (uint8_t)(i + 1);
        *f &= (uint16_t)~(BMR_CUSTOM | BMR_SWAPLR);
        return 0;
    } __except(EXCEPTION_EXECUTE_HANDLER) { return -1; }
}

// Log the map so a BAT can say what it actually was, not what it felt like.
static void LogMap(const char* when)
{
    __try {
        const uint8_t* m = (const uint8_t*)BMR_MAP;
        Log::Menu("[BTNMAP] %s flags=%04X map=[%u %u %u %u %u %u %u %u %u %u %u %u]",
                  when, (unsigned)*(const uint16_t*)BMR_FLAGS,
                  m[0], m[1], m[2], m[3], m[4], m[5], m[6], m[7], m[8], m[9], m[10], m[11]);
    } __except(EXCEPTION_EXECUTE_HANDLER) { }
}

// **Shift+F9. This was Alt+K in v0.25.1, and Alt+K never fired.**
//
// Aaron pressed it with a scrambled map and nothing happened -- no speech, and
// not one [BTNMAP] line in any log. That silence is the tell: the handler never
// ran, so none of the logic below was ever reached and the map was never the
// problem. This poll runs from the game's frame loop, and **holding Alt puts a
// Win32 window into menu-modal mode**, which can stall that loop for as long as
// the combo is held. The modifier chosen to make the shortcut feel deliberate
// was the one that guaranteed it could never be observed.
//
// Shift has no such behaviour, and the mod already reads it in this exact block
// for Shift+F3 / Shift+F4. Bare F9 was rejected: field_nav_handlekeys.inl binds
// it to the Garden battle SKIP, and while that binding is scoped to one field,
// this one is not -- an unscoped key that sometimes lands on top of a scoped one
// is a bug waiting for a BAT. F10 and Alt were rejected for the same reason as
// each other: both are Windows menu-activation keys.
//
// Alt+K is deliberately NOT kept. A shortcut that cannot fire is worse than no
// shortcut, because it answers "did you try the rescue" with a yes.
static void PollHotkey()
{
    static bool s_was = false;
    const bool shift = (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0;
    const bool f9    = (GetAsyncKeyState(VK_F9)    & 0x8000) != 0;
    const bool down  = shift && f9;
    if (down == s_was) return;
    s_was = down;
    if (!down) return;

    LogMap("before");
    const int r = Restore();
    LogMap("after");
    if (r == 0)      ScreenReader::Speak("Controls restored to default", true);
    else if (r == 1) ScreenReader::Speak("Controls are already at default", true);
    else             ScreenReader::Speak("Could not read the controller settings", true);
    Log::Menu("[BTNMAP] Shift+F9 rescue -> %s",
              r == 0 ? "restored" : (r == 1 ? "already default" : "failed"));
}

} // namespace ButtonMapRescue
