// battle_burst_capture_model.inl -- a run of screenshots on a fixed cadence
//
// v0.107.0 (#megaflare).
//
// The existing async capture (RequestScreenshotAsync) is a single shot with an
// optional frame delay: one flag, one path, last request wins. That is right
// for the Scan UI, which is a still frame the mod knows the moment of. It is
// no use at all for an ANIMATION, where what has to be answered is "what is on
// screen across these ten seconds", and where the mod has exactly one trigger
// (the ability name appearing) at the START of the window.
//
// So: arm once, and let the frame hook take N shots spaced `interval` frames
// apart. The first shot is the frame the burst is stepped on -- no lead-in --
// because the trigger is already late by however long the ability text took to
// reach the window.
//
// The whole point of pulling it out into a model is that "did it fire on the
// right frames" is a question about arithmetic, and arithmetic is cheaper to
// check here than in a boss fight Aaron has to lose again to re-run.

struct BurstCapture {
    int remaining;   // shots still to take
    int countdown;   // frames to skip before the next shot
    int interval;    // frames between shots
    int index;       // shots already taken -- the number in the filename
};

static void BurstReset(BurstCapture* b)
{
    if (b == nullptr) return;
    b->remaining = 0;
    b->countdown = 0;
    b->interval  = 0;
    b->index     = 0;
}

static bool BurstIsRunning(const BurstCapture* b)
{
    return b != nullptr && b->remaining > 0;
}

// Arm a burst. Refuses while one is already running, so a trigger that fires
// twice in the same second (the ability name is written to the window more
// than once) does not restart the run and lose the tail of the animation.
// An interval below 1 would fire every frame forever; 0 shots is a no-op.
static bool BurstArm(BurstCapture* b, int shots, int interval)
{
    if (b == nullptr) return false;
    if (BurstIsRunning(b)) return false;
    if (shots < 1 || interval < 1) return false;
    b->remaining = shots;
    b->interval  = interval;
    b->countdown = 0;      // the next step fires
    b->index     = 0;
    return true;
}

// Call once per presented frame. Returns true on the frames that should
// capture. `b->index` names the shot and is advanced after the answer, so the
// first capture is shot 0.
static bool BurstStep(BurstCapture* b)
{
    if (!BurstIsRunning(b)) return false;
    if (b->countdown > 0) { b->countdown--; return false; }
    b->countdown = b->interval - 1;
    b->remaining--;
    b->index++;
    return true;
}

// The shot number the caller should put in the filename for the capture
// BurstStep just authorised. Kept separate from `index` so the call site
// cannot be tempted to read the field before or after the advance and get a
// different answer depending on which.
static int BurstShotNumber(const BurstCapture* b)
{
    return (b == nullptr) ? 0 : b->index - 1;
}

// ---------------------------------------------------------------------------
// v0.110.0 (#megaflare): WHAT COUNTS AS THE ABILITY-NAME BOX
// ---------------------------------------------------------------------------
// v0.107.0 armed the burst on `strstr(text, "Mega Flare")`. The 2026-08-27 BAT
// showed what that actually matches:
//
//   [18:53:46] [BURST] armed on "Called the King of GF; its Mega Flare ignores
//              all defense. Gives assistance freely to those who show their
//              power" -- 10 shots, 54 frames apart
//
// That is the SCAN DESCRIPTION. Ten screenshots and the whole twenty-second
// timer sweep were spent on the scan window, minutes before Bahamut could have
// cast anything, and both were spent by the time the fight mattered.
//
// The ability-name box holds the name and nothing else -- "Mega Flare",
// "Diamond Dust", "Hell Fire", "Dark Messenger" are what that same log's
// [BTL-WIN-RAW] lines show, at 12 to 14 bytes. So: match the WHOLE box, not a
// substring of it. FF8 pads its window strings, so leading and trailing spaces
// come off first -- the same padding that hid the v0.106.0 off-by-one.
static bool BurstTextTriggers(const char* text, const char* name)
{
    if (text == nullptr || name == nullptr || name[0] == '\0') return false;
    size_t b = 0;
    while (text[b] == ' ') b++;
    size_t e = b;
    while (text[e] != '\0') e++;
    while (e > b && text[e - 1] == ' ') e--;
    size_t n = 0;
    while (name[n] != '\0') n++;
    if (e - b != n) return false;
    for (size_t i = 0; i < n; i++) {
        if (text[b + i] != name[i]) return false;
    }
    return true;
}
