// field_minigame_bgbtl_skip.inl -- v0.20.121 (#minigame-bgbtl)
//
// F9: make the fight unloseable and silence the soldier; F9 again to leave the
// scene entirely. Included from field_minigame_bgbtl.inl; part of the
// FieldNavigation namespace. Do not compile independently.
//
// Split out because field_minigame_bgbtl.inl reached 85 KB and the CI hard fail
// is 81,920 bytes per source file. Nothing here reads anything the rest of the
// module does not already own: the clock, the two HP pairs, and the REQ hook's
// veto helper.

// ------------------------------------------------------------- the Skip
//
// v0.20.111: THE FORGED TRANSITION IS GONE. It crashed. Aaron: "I tried using
// the F10 skip functionality, and it caused the game to crash when I entered
// G-Garden."
//
// Why it was always fragile: writing `push 675, 1019, 3384, 0, 128 ; MAPJUMP3`
// into the engine's transition block reproduces the BYTES the winning script
// writes but none of its STATE. director0::talk reaches that line having
// finished the fight, released its entities and set whatever flags ggback1
// expects. Firing the same jump from the middle of a live fight -- in the HOST
// field, where that script is not even loaded -- lands in Galbadia Garden with
// the previous scene half torn down. That it survived long enough to crash on
// arrival rather than immediately is the only surprising part.
//
// SO THE SKIP NOW USES THE GAME'S OWN PATH: set the foe's HP to zero and top the
// player up. The script then resolves the fight exactly as it does when the
// player wins on merit -- knockout, scene, transition, flags, all of it -- and
// there is nothing to desync because nothing has been forged. It is slower than
// a jump (the v0.20.110 BAT measured ~68 s from the foe reaching zero to the
// field change) but it is the real ending, and the win is announced immediately
// from HP either way.
//
// Worth knowing: the game's own Game Over menu also offers "Try again with
// HP+200", and its hint reads "When time is up, the one with the most HP is the
// winner" -- so this is a last resort, not the only way through.
// The skip is a MODE, not a write. v0.20.111 zeroed the foe's HP once and
// topped Squall up once, which the BAT showed is not enough by a long way: the
// foe kept attacking from 0 HP for seventy seconds and Squall was ground from
// 1000 down to -31 and lost the fight the mod had just announced as won.
//
// The fight ends on its TIMER, with most-HP-wins, or immediately if Squall hits
// zero. So the skip has to hold BOTH conditions until the timer runs out: foe
// pinned at zero, Squall pinned at full, every tick. Then the only outcome left
// is the one the player asked for, and the game plays its own ending -- no
// forged transition, which is what crashed in v0.20.110.
static uint32_t ReadClock(bool* ok)
{
    __try { *ok = true; return *(const uint32_t*)(VARBLOCK_BASE + FIGHT_CLOCK); }
    __except (EXCEPTION_EXECUTE_HANDLER) { *ok = false; return 0; }
}

// The skip does two things, in this order, and then stops interfering.
//
//   1. Pin the HP -- foe at zero, player at full -- so that when the script
//      reads them at clock 580 it picks the player.
//   2. Push the clock to 579 once we are in bgbtl_1, so that moment arrives
//      NOW instead of after the full round.
//
// Then it lets go. The ending between 580 and 1057 IS the rescue scene Aaron
// asked for, and holding either value through it would only get in the way.
static void SkipTick()
{
    if (!s_skipActive) return;
    const uint16_t fid = FF8Addresses::pCurrentFieldId
                       ? *FF8Addresses::pCurrentFieldId : 0xFFFF;
    bool ok = false;
    const uint32_t clock = ReadClock(&ok);

    // -----------------------------------------------------------------------
    // v0.20.120: **THE MOD MUST NEVER WRITE VAR 80.** v0.20.119 did, and the
    // 2026-08-15 13:21 log shows the cost in one column:
    //
    //     13:21:16  F10 -- clock 115 -> 580 written
    //     13:21:17  ... 82 SECONDS WITH NOT ONE REQ EVENT ...
    //     13:22:38  the script wakes up mid-fight and the punches restart
    //
    // Aaron: "I could no longer hear any of the background sounds of the Garden
    // battle, it was like the background animation was completely gone."
    //
    // Var 80 is the CURRENT MOVIE'S FRAME NUMBER, and field 152's whole fight
    // plays over disc01_33h.avi -- one movie, ~105 s, whose last stretch IS the
    // Rinoa rescue. Writing 580 into it teleported director0 to its resolution
    // while the movie was still at frame 117: it released the fight entities
    // (`op76` x4, which is why the scene went silent), set the rescue
    // choreography sixty seconds early, and then the movie's own counter
    // reasserted itself and dragged the ladder back into the fight.
    //
    // There is no seek. The fight and the rescue are the same movie, so the
    // skip cannot shorten it -- it can only make it harmless. What it CAN do,
    // and what Aaron actually asked for, is stop the punching. The soldier's
    // driver gates every attack on Squall's health:
    //
    //     gal0::g0_fall0   ...  RDVARSW 354 ; PUSHI 0 ; EXPR 7 (GT) ; JPF 13
    //
    // So the skip parks Squall's health at ZERO, which silences the driver
    // without touching a timer, and restores it to full before the clock
    // reaches the resolution at 580, so `foeHP < squallHP` still picks him.
    // Every value written here is one the game writes itself.
    // v0.20.121: **SQUALL IS PINNED AT FULL, NEVER AT ZERO.** .120 parked him at
    // zero to silence the driver and the 13:46 log shows what that costs: F10,
    // then the Game Over screen four seconds after field 152 loaded. The
    // silencing now happens at the REQ (see SkipVetoLabel); the health values
    // are only ever the ones the game itself would write.
    __try {
        const int sMax = *(const int16_t*)(VARBLOCK_BASE + HP_SQUALL_MAX);
        *(int16_t*)(VARBLOCK_BASE + HP_FOE_CUR) = 0;
        if (sMax > 0) *(int16_t*)(VARBLOCK_BASE + HP_SQUALL_CUR) = (int16_t)sMax;
        // ...and the bars a watcher can see, which are a separate thing.
        SetHpGauge(GAUGE_FOE, 0);
        if (sMax > 0) SetHpGauge(GAUGE_SQUALL, (int16_t)sMax);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        s_skipActive = false;
        Log::Field("FieldNavigation: [BGBTL] SKIP hold failed -- disengaged");
        return;
    }

    // THE HOST PHASE ENDS WITH THE MOVIE, AND ONLY WITH THE MOVIE. bg2f_31's
    // ladder gates on var 80 >= 1 and >= 9 and nothing else; the resolution
    // lives in bgbtl_1's director0, so the fight cannot be ended here. Ending
    // disc01_32h.avi is what moves the scene on, and it costs nothing -- that
    // movie is the fight, not the rescue.
    if (fid != FIELD_MINIGAME && !s_reached152) {
        if (!s_fmvSkipAsked) {
            s_fmvSkipAsked = true;
            const bool asked = FmvSkip::RequestSkip();
            Log::Field("FieldNavigation: [BGBTL] SKIP: host phase -- FMV skip %s "
                       "(only the movie ends this phase)",
                       asked ? "requested" : "declined, no movie playing");
        }
        return;
    }
    if (fid == FIELD_MINIGAME) s_reached152 = true;
}

// v0.20.124: THE DOUBLE-TAP IS GONE. Aaron: "Let's get rid of the double-tap
// F9 skip option. The single-tap option seems to work well and ensures the
// player hears the full FMV." It did work -- the 2026-08-15 14:33 log has it
// requested at 14:33:14 and out at field 675 by 14:33:18 -- but it existed to
// save a wait that only mattered when the fight was still dangerous, and it
// cut the rescue scene to do it. One press, one behaviour, no way to skip past
// the thing the skip exists to reach.

static bool SkipToVictory()
{
    // The fight is driven by the field script, which is exactly what the freeze
    // stops -- so skipping from inside the briefing must thaw first.
    EndBriefing("skip requested", false);

    if (s_skipActive) {
        ScreenReader::Speak("Already skipping.", true);
        return true;
    }
    s_skipActive   = true;
    s_wonAnnounced = true;      // the skip IS the win; do not repeat it later
    SkipTick();
    Log::Field("FieldNavigation: [BGBTL] SKIP engaged -- foe at 0, Squall pinned at "
               "full, the soldier's attacks vetoed at the REQ");
    // Say what it actually does, and what it cannot do. The fight and the
    // rescue are ONE movie, so the scene still takes its time -- what changes
    // is that nothing can hurt Squall and the soldier stops swinging. A player
    // told "skipping" who then waits ninety seconds deserves to know why.
    ScreenReader::Speak("Skipping. You win. The soldier stops and the rescue "
                        "scene plays out.", true);
    return true;
}

