// field_missile_terminal.inl -- reading the Galbadia Missile Base terminal.
//
// Included from field_dialog.cpp after missile_terminal_model.inl. Polled from
// the top of PollWindows, BEFORE the window scan, so anything this speaks can be
// marked as already-spoken and the scan does not say it twice.
//
// Everything here is a read of a field variable the script writes. No hooks, no
// patches, nothing written back. The derivation is in missile_terminal_model.inl.
//
// WHAT THIS ADDS THAT THE DIALOGUE READER CANNOT
//
// The menu labels are drawn as ordinary message windows and **the AMES opcode
// hook already speaks every one of them** -- v0.40.0 said them here as well, on
// the theory that a readout which depends on another subsystem catching the
// window is a readout that works by luck. The 2026-08-20 BAT settled it: the
// log shows `[AMES] win[1] Speaking: "SET TARGET"`, `"CONFIRM EQUIPMENT"`,
// `"SIMULATION"`, `"EXIT"`, `"DATA UPLOAD"` for every cursor move, so the
// second copy was redundant -- and because AMES speaks with interrupt and this
// file queues, the copy was being discarded anyway. The labels are no longer
// spoken here; the cursors are still tracked, because they are what identifies
// the screen.
//
// What is left is everything with no text at all:
//
//   * the ratio bar -- a sprite position in `var[482]`, no number anywhere;
//   * the upload YES/NO -- two sprites lit by `upbyesno`, no window;
//   * the outcome of pressing confirm -- the ratio screen sets a flag or does
//     NOTHING AT ALL, on screen identically;
//   * **the boot screen, the simulation report and the upload progress bars**,
//     which are background layers with baked-in text and no window at all.
//
// THE THREE SILENT SCREENS (v0.40.1, #101)
//
// Aaron: *"A few screens had nothing announce... the initial bootup screen, the
// 'simulation', and the progress bars while data is uploading."* All three are
// drawn as sprites, so there is nothing to decode -- but all three are also
// FIXED. `control::equipment`'s two animation scripts settle it:
//
//   script 336 (simulation)  153 dwords, 0 branches, 0 variable reads
//   script 359 (upload)       96 dwords, 0 branches, 0 variable reads
//
// Not one conditional and not one variable between them, so neither screen can
// vary with anything the player did. Aaron's own capture confirms it from the
// other side: he ran the simulation with the error ratio at 100% and the report
// still read "Error Ratio 65~80%" -- it is a canned demo, not a calculation.
// That is what makes a fixed transcription honest here rather than a guess, and
// it is the only reason these strings are allowed to be literals.
//
// The strings below are transcribed from the F11 captures of that BAT. If the
// game ever shows something else on these screens, the mod would be reading out
// something that is not there -- so they are confined to this one field, and
// each is anchored to a signal that only occurs on its own screen.

static const char* const MISSILE_FIELD = "gmmoni1";

struct MissileState
{
    bool     active;
    int      screen;          // MissileTerminal::Screen
    int      mainCursor, subCursor, sharedCursor, ratio, flags;
    int      flagsOnScreenEntry;
    uint32_t lastRatioSpokeAt;
    bool     ratioMaxAnnounced;
    bool     slashWasDown;
    int      confirm;
    bool     uploadRunningSaid;
    bool     simRunningSaid;
    bool     simReportSaid;
    bool     cautionSaid;
};

// Transcribed from the 2026-08-20 F11 captures. Safe as literals only because
// the scripts that draw them have no branches and read no variables -- see the
// header. Confined to this one field.
static const char* const MISSILE_BOOT_TEXT =
    "System check. I D, okay. Bios, okay. Memory, okay. Network, okay. "
    "Safety unit, okay. Welcome to H T M S, the Hyper Technology Missile System.";
// SET TARGET is refused, and the refusal is a PICTURE (v0.42.0, #103).
//
// `settarget::targetmenu` cursor 0 + confirm is `REQ(seigyo, 344)`, and script
// 344 -- `seigyo::settargetselect` -- is forty-six dwords whose entire visible
// effect is
//
//     REQ(caution, 101 = error01)   ; raise the CAUTION layer
//     WAIT 90                       ; ninety frames
//     REQ(caution, 102 = erroroff)  ; take it down
//
// No window and no message: `gmmoni1.msd` has thirty-five strings and none of
// them is this one, because the text is baked into the background graphic. So
// the player picks the item the scene is named after -- the one thing the party
// came here to do -- and the game answers with a picture, silently. Transcribed
// from the 2026-08-20 F11 capture, the same standing as the boot screen and the
// simulation report: a layer with no branches behind it cannot say anything
// else. (The proper name is read off that capture and is the one part of this
// that is a transcription rather than a decode.)
static const char* const MISSILE_CAUTION_TEXT =
    "Caution. Authorization required. This control panel is only available to "
    "the System Administrator or Commander Okamoto. Enter the password now, or "
    "you can not use this system.";
static const char* const MISSILE_SIM_REPORT =
    "Simulation completed. Target, Balamb Garden. "
    "Accuracy rate, 20 to 30 percent. Error ratio, 65 to 80 percent.";

static MissileState s_mt = {};

// SEH-guarded reads. No object with a destructor in this frame (MSVC C2712 --
// tests/lint_seh.py).
static bool MissileReadVars(int* mainC, int* subC, int* sharedC, int* ratio, int* flags,
                            int* confirm)
{
    bool ok = false;
    __try {
        const uint8_t* base = (const uint8_t*)MissileTerminal::FIELD_VAR_BASE;
        *mainC   = *(volatile const uint8_t*)(base + MissileTerminal::VAR_MAIN_CURSOR);
        *subC    = *(volatile const uint8_t*)(base + MissileTerminal::VAR_SUB_CURSOR);
        *sharedC = *(volatile const uint8_t*)(base + MissileTerminal::VAR_SHARED_CUR);
        *ratio   = *(volatile const int16_t*)(base + MissileTerminal::VAR_RATIO);
        *flags   = *(volatile const uint8_t*)(base + MissileTerminal::VAR_FLAGS);
        *confirm = *(volatile const uint8_t*)(base + MissileTerminal::VAR_CONFIRM);
        ok = true;
    } __except (EXCEPTION_EXECUTE_HANDLER) { ok = false; }
    return ok;
}

// Speak, then tell the window scan it has already been said.
static void MissileSay(const char* text)
{
    if (!text || !*text) return;
    ScreenReader::Speak(text, false);
    Log::Dialog("FieldDialog: [MISSILE] \"%s\"", text);
}

// Has any window said this? Used to anchor the simulation report to the game's
// own "End simulation" prompt, which is the one thing on that screen that IS a
// window and therefore the one moment the report is certainly on display.
static bool MissileWindowSaid(const char* needle)
{
    for (int i = 0; i < MAX_WINDOWS; i++)
        if (s_winState[i].lastSpokenText.find(needle) != std::string::npos) return true;
    return false;
}

static void MissileSayRatio(int v, bool force)
{
    using namespace MissileTerminal;
    const uint32_t now = (uint32_t)GetTickCount();
    const bool atMax = RatioAtMax(v);
    // Holding the button walks 27 positions; a readout per position is noise.
    // The moment it becomes acceptable is never throttled, because that is the
    // one transition the scene turns on.
    if (!force && !(atMax && !s_mt.ratioMaxAnnounced) &&
        (uint32_t)(now - s_mt.lastRatioSpokeAt) < 250) return;

    char buf[96];
    if (atMax) snprintf(buf, sizeof(buf), "%d percent, maximum", RatioPercent(v));
    else       snprintf(buf, sizeof(buf), "%d percent", RatioPercent(v));
    ScreenReader::Speak(buf, false);
    Log::Dialog("FieldDialog: [MISSILE] ratio raw=%d steps=%d -> \"%s\"",
                v, RatioSteps(v), buf);
    s_mt.lastRatioSpokeAt = now;
    if (atMax) s_mt.ratioMaxAnnounced = true;
}

// Leaving the ratio or upload screen is the only honest place to report what
// confirming did, because both refusals are silent AND leave no transient the
// poller can be sure of catching. The flags are compared against what they were
// on entry, so a bit set on an earlier visit is not reported twice.
static void MissileReportOutcome(int leavingScreen, int flagsNow)
{
    using namespace MissileTerminal;
    const int gained = flagsNow & ~s_mt.flagsOnScreenEntry;

    if (leavingScreen == SCR_RATIO) {
        if (gained & FLAG_RATIO_SET) MissileSay("Error ratio set to maximum.");
        else if (!(flagsNow & FLAG_RATIO_SET))
            MissileSay("Error ratio not set. It has to be at maximum.");
    } else if (leavingScreen == SCR_UPLOAD) {
        if (gained & FLAG_UPLOADED) MissileSay("Coordinate data uploaded.");
        else if (!(flagsNow & FLAG_UPLOADED))
            MissileSay("Nothing uploaded. Set the error ratio to maximum first.");
    }
}

static void MissileEnterScreen(int screen, int flagsNow)
{
    if (s_mt.screen == screen) return;
    if (s_mt.screen != MissileTerminal::SCR_NONE) MissileReportOutcome(s_mt.screen, flagsNow);
    s_mt.screen = screen;
    s_mt.flagsOnScreenEntry = flagsNow;
    s_mt.ratioMaxAnnounced = false;
}

// What "/" reads back: where you are and what it is set to.
static void MissileSayCurrent()
{
    using namespace MissileTerminal;
    char buf[192];
    switch (s_mt.screen) {
        case SCR_MAIN:
            snprintf(buf, sizeof(buf), "Main menu. %s.",
                     MainCursorValid(s_mt.mainCursor) ? MAIN_LABELS[s_mt.mainCursor] : "Unknown");
            break;
        case SCR_TARGET:
            snprintf(buf, sizeof(buf), "Set target. %s.",
                     SubCursorValid(s_mt.subCursor) ? SUB_LABELS[s_mt.subCursor] : "Unknown");
            break;
        case SCR_RATIO:
            snprintf(buf, sizeof(buf), "Error ratio, %d percent%s. Right to increase, left to decrease.",
                     RatioPercent(s_mt.ratio), RatioAtMax(s_mt.ratio) ? ", maximum" : "");
            break;
        case SCR_UPLOAD:
            snprintf(buf, sizeof(buf), "Upload coordinate data? %s.",
                     UploadLabel(s_mt.sharedCursor));
            break;
        case SCR_EQUIP:
            snprintf(buf, sizeof(buf), "Confirm equipment, item %d of 5.", s_mt.sharedCursor + 1);
            break;
        default:
            snprintf(buf, sizeof(buf), "Missile control terminal.");
            break;
    }
    const int f = s_mt.flags;
    char full[256];
    snprintf(full, sizeof(full), "%s Ratio %s, data %s.", buf,
             (f & FLAG_RATIO_SET) ? "set" : "not set",
             (f & FLAG_UPLOADED)  ? "uploaded" : "not uploaded");
    ScreenReader::Speak(full, true);
    Log::Dialog("FieldDialog: [MISSILE] readout: %s", full);
}

static void PollMissileTerminal()
{
    using namespace MissileTerminal;

    const char* field = FF8Addresses::pCurrentFieldName ? FF8Addresses::pCurrentFieldName : "";
    const bool onField = UrgentSameName(field, MISSILE_FIELD);

    if (!onField) {
        if (s_mt.active) {
            Log::Dialog("FieldDialog: [MISSILE] left the terminal field");
            memset(&s_mt, 0, sizeof(s_mt));
        }
        return;
    }

    int mainC = 0, subC = 0, sharedC = 0, ratio = 0, flags = 0, confirm = 0;
    if (!MissileReadVars(&mainC, &subC, &sharedC, &ratio, &flags, &confirm)) return;

    if (!s_mt.active) {
        memset(&s_mt, 0, sizeof(s_mt));
        s_mt.active = true;
        s_mt.mainCursor = mainC; s_mt.subCursor = subC; s_mt.sharedCursor = sharedC;
        s_mt.ratio = ratio; s_mt.flags = flags;
        s_mt.screen = SCR_NONE;
        Log::Dialog("FieldDialog: [MISSILE] entered gmmoni1: main=%d sub=%d shared=%d ratio=%d flags=0x%02X",
                    mainC, subC, sharedC, ratio, flags);
        MissileSay("Missile control terminal.");
        MissileSay(MISSILE_BOOT_TEXT);
        MissileSay("Up and down to change, confirm to select. "
                   "Press slash to hear the current setting.");
        return;
    }

    // "/" -- read back where you are. Bound only while this field is loaded, so
    // it cannot collide with anything else.
    const bool slashDown = (GetAsyncKeyState(VK_OEM_2) & 0x8000) != 0;
    if (slashDown && !s_mt.slashWasDown) MissileSayCurrent();
    s_mt.slashWasDown = slashDown;

    // The screen as it stood BEFORE anything this frame changed it. The
    // caution below has to know that the SET TARGET submenu was already open
    // when confirm was pressed -- the same press is how you get INTO that
    // submenu from the main menu, and a level check made after the transition
    // cannot tell the two apart.
    const int  screenBefore   = s_mt.screen;
    const bool confirmRising  = (confirm != 0) && (s_mt.confirm == 0);

    // Whichever cursor moved identifies the screen: only the live screen's own
    // loop writes its cursor.
    // The labels themselves are the AMES hook's job -- see the header. These
    // two blocks exist to know WHICH SCREEN is open, which is what everything
    // below depends on.
    if (mainC != s_mt.mainCursor) {
        s_mt.mainCursor = mainC;
        MissileEnterScreen(SCR_MAIN, flags);
        s_mt.simRunningSaid = false;
        s_mt.simReportSaid  = false;
    }
    if (subC != s_mt.subCursor) {
        s_mt.subCursor = subC;
        MissileEnterScreen(SCR_TARGET, flags);
    }
    if (sharedC != s_mt.sharedCursor) {
        const int which = SharedCursorScreen(sharedC, s_mt.mainCursor, s_mt.subCursor);
        s_mt.sharedCursor = sharedC;
        MissileEnterScreen(which, flags);
        if (which == SCR_UPLOAD) {
            if (UploadCursorValid(sharedC)) MissileSay(UploadLabel(sharedC));
        } else if (sharedC == 4) {
            MissileSay("Exit");
        }
        // Equipment items 0..3 draw their own text; the dialogue reader has it.
    }
    // Entering the SET TARGET submenu. `Director::default` cursor 0 + confirm
    // runs `REQ(settarget, targetmenu)`, and the submenu opens with its own
    // cursor already at 0 -- so if the player confirms SET TARGET straight away
    // without moving it, no cursor ever changes and the cursor blocks above
    // never learn which screen is open.
    // SCR_NONE counts as the main menu here: that is what is on screen when
    // the field loads, and a player who confirms SET TARGET without moving any
    // cursor at all would otherwise never leave it.
    if ((screenBefore == SCR_MAIN || screenBefore == SCR_NONE) &&
        s_mt.mainCursor == 0 && confirmRising)
        MissileEnterScreen(SCR_TARGET, flags);

    if (ratio != s_mt.ratio) {
        const bool firstMove = (s_mt.screen != SCR_RATIO);
        s_mt.ratio = ratio;
        MissileEnterScreen(SCR_RATIO, flags);
        MissileSayRatio(ratio, firstMove);
    }

    // ---- the upload progress bars -----------------------------------------
    //
    // `control::uploader`'s Yes branch is
    //     WINCLOSE / REQ(seigyo, 359) / if (var[484] & 1) var[484] |= 2 / var[1029] = 0
    // and that REQ blocks for the whole animation, so **`var[1029]` stays 1 for
    // its entire duration** -- roughly twenty-five seconds in the BAT, during
    // which the screen showed three unit bars filling and the mod said nothing.
    // It is the one persistent signal either animation offers.
    if (s_mt.screen == SCR_UPLOAD && confirm && !s_mt.uploadRunningSaid &&
        s_mt.sharedCursor == 1) {
        s_mt.uploadRunningSaid = true;
        MissileSay("Uploading coordinate data to three units. Please wait.");
    }
    if (!confirm) s_mt.uploadRunningSaid = false;

    // ---- the simulation ---------------------------------------------------
    //
    // Here `var[1029]` is cleared before the REQ rather than after, so it is
    // only a start signal, not a duration. The REPORT is anchored to the game's
    // own "End simulation" prompt instead: that prompt is the one thing on the
    // screen that is a real window, so it is the one moment the report is
    // certainly on display.
    if (s_mt.screen == SCR_MAIN && s_mt.mainCursor == 2) {
        // The same confirm both STARTS the run and DISMISSES it at the end, so
        // the end prompt being absent is what separates the two.
        const bool endPromptUp = MissileWindowSaid("End simulation");
        if (confirm && !endPromptUp && !s_mt.simRunningSaid) {
            s_mt.simRunningSaid = true;
            s_mt.simReportSaid  = false;      // a fresh run
            MissileSay("Running simulation. Please wait.");
        }
        if (!confirm) s_mt.simRunningSaid = false;
        if (endPromptUp && !s_mt.simReportSaid) {
            s_mt.simReportSaid = true;
            MissileSay(MISSILE_SIM_REPORT);
        }
    }
    // ---- SET TARGET says no, and says it in a picture --------------------
    //
    // Keyed on the RISING EDGE of confirm with the submenu already open. The
    // level itself is no good here: `settarget::targetmenu` clears `var[1029]`
    // AFTER the blocking REQ rather than before it (the main menu does the
    // opposite), so confirm stays 1 for the whole time the layer is up -- eight
    // seconds in the BAT -- and a level test would fire on every frame of it.
    if (screenBefore == SCR_TARGET && s_mt.subCursor == 0 && confirmRising &&
        !s_mt.cautionSaid) {
        s_mt.cautionSaid = true;
        MissileSay(MISSILE_CAUTION_TEXT);
    }
    if (!confirm) s_mt.cautionSaid = false;

    if (confirm != s_mt.confirm) {
        Log::Dialog("FieldDialog: [MISSILE] confirm %d -> %d on screen %d",
                    s_mt.confirm, confirm, s_mt.screen);
        s_mt.confirm = confirm;
    }

    if (flags != s_mt.flags) {
        Log::Dialog("FieldDialog: [MISSILE] flags 0x%02X -> 0x%02X", s_mt.flags, flags);
        s_mt.flags = flags;
    }
}
