// field_urgent_prompt.inl -- repeated prompts for scenes that kill you silently.
//
// Included from field_dialog.cpp; polled at the tail of FieldDialog::PollWindows().
//
// ============================================================================
// WHY (v0.39.0, #100 -- the D-District Prison escape)
// ============================================================================
//
// Aaron: *"During this FMV, Squall is shown hanging onto a ledge and the player
// must move Squall to the right in order to survive. Right now there is no way
// for a blind player to know they need to move right."*
//
// **It is not an interactive FMV.** All twenty references to the three pad masks
// in FF8_EN.exe (`0x01CE48B0` held, `0x01CE48B4`, `0x01CE48B8` newly pressed)
// live in the field module, the two JSM button opcodes and the script
// interpreter -- **none is in the movie player** at `0x0055A140`. And scanning
// all twenty-six `gp*` fields for the button opcodes (`0x6D`/`0x6E`) returns one
// hit, in `gpgmn2`, on the confirm button. So nothing is reading Left/Right but
// the engine's ordinary walking code: the player is walking a field while a
// movie plays behind it. That is why the fix is a spoken prompt and not an
// input hook -- there is no input to hook.
//
// **The BAT of 2026-08-20 settled the moment**, which the executable could not:
//
//   22:25:44  field 'gpexit2' loads
//   22:25:48  disc01_02h plays (audio description, 3 cues)
//   22:26:10  RAMESW: Rinoa -- "Squall!!!  Hold on!  Over here!  Hurry!"
//   22:26:10  disc01_03h starts                      <-- the window opens here
//   22:26:12  Aaron's screenshot: "when the player should start moving right"
//   22:26:40  disc01_03h ends
//   22:26:45  MAPJUMP from (3817,115) -> 'gppark1'   <-- he made it
//
// So the trigger is **field `gpexit2` + movie `disc01_03h`**, two facts the mod
// already has to hand, both logged, neither inferred.
//
// ============================================================================
// THE TWO RULES THIS FILE FOLLOWS
// ============================================================================
//
// **The cue arms on the movie and lives on the field.** `disc01_03h` ends five
// seconds before the map jump and Aaron was still walking; a cue that died with
// the movie would go quiet exactly when he still needed it. So the movie only
// ARMS the cue -- what keeps it alive is the field, and what ends it is leaving
// the field (which is what success looks like) or the cap.
//
// **It never talks over the game.** The prompt speaks with interrupt=false AND
// skips its slot entirely while anything else is speaking. The same BAT showed
// what the other way round sounds like: an audio-description cue fired one
// second after Rinoa's line with interrupt=true and cut her off mid-sentence.
// A prompt that stepped on the game's own words would be doing the same thing
// for the same reason.

// ---------------------------------------------------------------------------
// The model. No Windows, no game memory -- tests/urgent_cue_compile.cpp drives
// this half directly.
// ---------------------------------------------------------------------------

struct UrgentCue
{
    const char* field;        // field name that keeps the cue alive (required)
    const char* armAvi;       // AVI basename that arms it, or null = arm on entry
    uint32_t    startDelayMs; // silence after arming, so the scene's own line lands
    uint32_t    repeatMs;
    uint32_t    maxMs;        // give up; a cue that never stops is a cue that lies
    const char* text;
};

static const UrgentCue URGENT_CUES[] = {
    // D-District Prison escape. 2.5 s of head start lets Rinoa's line finish.
    { "gpexit2", "disc01_03h", 2500, 2500, 60000, "Move right" },
};
static const int URGENT_CUE_COUNT = (int)(sizeof(URGENT_CUES) / sizeof(URGENT_CUES[0]));

static bool UrgentSameName(const char* a, const char* b)
{
    if (!a || !b) return false;
    for (; *a && *b; a++, b++) {
        char ca = *a, cb = *b;
        if (ca >= 'A' && ca <= 'Z') ca = (char)(ca - 'A' + 'a');
        if (cb >= 'A' && cb <= 'Z') cb = (char)(cb - 'A' + 'a');
        if (ca != cb) return false;
    }
    return *a == '\0' && *b == '\0';
}

// Does `aviBasename` (e.g. "disc01_03h.avi", any case) name `stem`?
static bool UrgentAviIs(const char* aviBasename, const char* stem)
{
    if (!aviBasename || !stem) return false;
    size_t i = 0;
    for (; stem[i]; i++) {
        char c = aviBasename[i];
        if (!c) return false;
        if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
        char s = stem[i];
        if (s >= 'A' && s <= 'Z') s = (char)(s - 'A' + 'a');
        if (c != s) return false;
    }
    // Accept exactly the stem, or the stem followed by ".avi" and nothing
    // else. A looser "followed by a dot" test lets "disc01_03h.avix" through,
    // which is the kind of near-miss a cue table must not honour.
    if (aviBasename[i] == '\0') return true;
    const char* ext = aviBasename + i;
    return (ext[0] == '.' &&
            (ext[1] == 'a' || ext[1] == 'A') &&
            (ext[2] == 'v' || ext[2] == 'V') &&
            (ext[3] == 'i' || ext[3] == 'I') &&
            ext[4] == '\0');
}

// Which cue, if any, ARMS right now. -1 for none.
static int UrgentCueToArm(const char* field, const char* aviBasename)
{
    for (int i = 0; i < URGENT_CUE_COUNT; i++) {
        const UrgentCue& c = URGENT_CUES[i];
        if (!UrgentSameName(field, c.field)) continue;
        if (c.armAvi && !UrgentAviIs(aviBasename, c.armAvi)) continue;
        return i;
    }
    return -1;
}

struct UrgentState
{
    int      cue;          // -1 = idle
    uint32_t armedAt;
    uint32_t lastSpokeAt;  // 0 = never
    int      spokenCount;
};

// An armed cue survives the movie ending; only the field or the cap ends it.
static bool UrgentStillLive(const UrgentState& st, const char* field, uint32_t now)
{
    if (st.cue < 0) return false;
    const UrgentCue& c = URGENT_CUES[st.cue];
    if (!UrgentSameName(field, c.field)) return false;
    if ((uint32_t)(now - st.armedAt) >= c.maxMs) return false;
    return true;
}

// `busy` is "something else is speaking". The caller supplies it so the model
// stays testable; in the game it is ScreenReader::IsSpeaking().
static bool UrgentShouldSpeak(const UrgentState& st, uint32_t now, bool busy)
{
    if (st.cue < 0) return false;
    const UrgentCue& c = URGENT_CUES[st.cue];
    if ((uint32_t)(now - st.armedAt) < c.startDelayMs) return false;
    if (st.lastSpokeAt != 0 && (uint32_t)(now - st.lastSpokeAt) < c.repeatMs) return false;
    if (busy) return false;             // the game's own words win, always
    return true;
}

#if !defined(FF8_URGENT_CUE_HOST_TEST)
// ---------------------------------------------------------------------------
// The poller.
// ---------------------------------------------------------------------------

static UrgentState s_urgent = { -1, 0, 0, 0 };

static void PollUrgentPrompt()
{
    const char* field = FF8Addresses::pCurrentFieldName ? FF8Addresses::pCurrentFieldName : "";
    const uint32_t now = (uint32_t)GetTickCount();

    std::string avi = FmvSkip::GetCurrentAviName();   // "" between movies

    if (!UrgentStillLive(s_urgent, field, now)) {
        if (s_urgent.cue >= 0) {
            Log::Dialog("FieldDialog: [URGENT] '%s' ended after %u ms, %d spoken",
                        URGENT_CUES[s_urgent.cue].text,
                        (unsigned)(now - s_urgent.armedAt), s_urgent.spokenCount);
            s_urgent.cue = -1;
        }
        const int arm = UrgentCueToArm(field, avi.c_str());
        if (arm >= 0) {
            s_urgent.cue = arm;
            s_urgent.armedAt = now;
            s_urgent.lastSpokeAt = 0;
            s_urgent.spokenCount = 0;
            Log::Dialog("FieldDialog: [URGENT] armed '%s' on field=%s avi=%s",
                        URGENT_CUES[arm].text, field, avi.empty() ? "(none)" : avi.c_str());
        }
        return;
    }

    if (!UrgentShouldSpeak(s_urgent, now, ScreenReader::IsSpeaking())) return;

    ScreenReader::Speak(URGENT_CUES[s_urgent.cue].text, false);
    s_urgent.lastSpokeAt = now;
    s_urgent.spokenCount++;
    Log::Dialog("FieldDialog: [URGENT] \"%s\" (%d) at +%u ms",
                URGENT_CUES[s_urgent.cue].text, s_urgent.spokenCount,
                (unsigned)(now - s_urgent.armedAt));
}
#endif  // FF8_URGENT_CUE_HOST_TEST
