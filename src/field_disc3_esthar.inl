// field_disc3_esthar.inl -- #110, the Esthar Lunatic Pandora run.
// PART OF field_disc3.inl. Do NOT compile standalone.
//
// ============================================================================
// v0.57.0: REWRITTEN AFTER THE 2026-08-22 BAT
// ============================================================================
//
// Aaron reached the run. The mod told him he was standing on contact point 1
// with the window open, and nothing happened. Three faults; the account of what
// the game actually does is in esthar_pandora_model.inl. What changes here:
//
//   1. WHICH EXIT. One field per hop, named the way the CATALOG names that
//      exit ("Esthar - City 21"), so it can be found and walked to.
//   2. THE TRUTH ON ARRIVAL. v0.55.0 said "wait here" at eccway21, where
//      waiting is the one thing that cannot work: it is a walk-through `touch`
//      line. Only eciway11 and ecoway3 fire by themselves.
//   3. SILENCE THE REST OF THE TIME. v0.55.0 spoke six interrupting utterances
//      a second because the clock it read alternated with zero. Nothing else
//      the mod said could be heard through that, which is what Aaron reported
//      as auto-drive being broken.
//
// The route always aims at the contact point's stand-and-wait field where one
// exists. That is not merely the easiest target, it is the SAFEST: the trigger
// lines on the way there MISS to it. Cross eccway21's line early and you land
// in eciway11, where it fires by itself when the window opens; cross it during
// the window and you board on the spot. So "get to eciway11 and wait" is right
// whether the clock is ready or not -- which is the only kind of instruction a
// player can act on without watching a timer.
//
// CP2 has no automatic site. Its two lines are the single doorway between
// eccway12 and eccway41, and it fires walking through in either direction, so
// the player is routed to eccway12 and told to walk through to eccway41.

namespace Esthar {

static bool  s_live          = false;
static int   s_lastPoint     = -1;
static bool  s_lastOpen      = false;
static int   s_lastMin       = -1;
static char  s_lastField[32] = "";
static DWORD s_lastSpokeAt   = 0;
static bool  s_clockStarted  = false;

// Nothing may interrupt more often than this. The BAT's failure was six
// interrupting utterances inside one second; a floor makes that structurally
// impossible even if some future state flaps. "/" bypasses it -- being asked a
// question is not flapping.
static const DWORD EP_MIN_GAP_MS = 1500;

static void Reset()
{
    s_live = false; s_lastPoint = -1; s_lastOpen = false; s_lastMin = -1;
    s_lastField[0] = '\0'; s_lastSpokeAt = 0; s_clockStarted = false;
}

// ---------------------------------------------------------------------------
// Reading the run.
// ---------------------------------------------------------------------------
static bool RunLive()
{
    uint16_t m = 0;
    if (!D3ReadU16(D3VarAddr(EP_VAR_MISSION), &m)) return false;   // var[672] is a WORD
    return m == EP_MISSION_LIVE;
}

// THE CLOCK, FROM ONE PLACE ONLY.
//
// v0.55.0 preferred var[1024] and fell back to this. var[1024] is the HUD
// mirror `info::default` re-establishes every frame, so reads land mid-update
// and come back 0 -- which flipped the target contact point back and forth and
// produced the flood. GETTIMER (opcode 0x0A4, handler 0x00521710) is
// `mov eax,[0x01CFE92C]`, and that is what all eight boarding gates compare
// against. So it is what this reads, and there is no fallback: an unreadable
// clock means say nothing, not guess.
static int Seconds()
{
    int32_t t = 0;
    if (!D3ReadI32(EP_TIMER_ADDR, &t)) return -1;
    if (t < 0 || t > 1300) return -1;          // outside the 20:01 run
    // v0.57.1: zero BEFORE `ecmview1 :: Timelimit::default` has run its
    // SETTIMER 1201 is "not started", not "no time left". The 2026-08-22 BAT
    // opened with `t=0 point=3 open=0` in ecmview1 and announced the LAST
    // contact point as the target, because a stopped clock and an expired one
    // read the same. Once a non-zero value has been seen the run is under way
    // and a genuine zero means exactly what it says.
    if (t == 0 && !s_clockStarted) return -1;
    if (t > 0) s_clockStarted = true;
    return t;
}

static uint8_t UsedBits()
{
    uint8_t b = 0;
    return D3ReadU8(D3VarAddr(EP_VAR_USED), &b) ? b : 0;
}

// The catalog's own words for a field, so "take the exit to X" names the thing
// the player hears when they cycle the catalog onto that exit.
static const char* DisplayOfId(int id)
{
    if (id >= 0 && id < FIELD_DISPLAY_NAMES_COUNT && FIELD_DISPLAY_NAMES[id])
        return FIELD_DISPLAY_NAMES[id];
    const char* f = EpFieldName(id);
    return f ? f : "the next area";
}
static const char* DisplayOf(const char* internalField)
{
    if (!internalField) return "the next area";
    for (int i = 0; i < EP_HOP_COUNT; i++)
        if (_stricmp(EP_HOPS[i].field, internalField) == 0)
            return DisplayOfId(EP_HOPS[i].id);
    return internalField;
}

// ---------------------------------------------------------------------------
// The floor applies to EVERYTHING this module says, not just to interrupting
// speech. A non-interrupting utterance every frame is not politer than an
// interrupting one -- it queues, and the queue is what the player then has to
// sit through. The BAT's six-a-second was interrupting; the first version of
// this floor only covered that case and let a flapping field emit 600 queued
// lines in ten seconds instead, which the probe caught.
//
// "/" bypasses by zeroing s_lastSpokeAt before it calls Announce: being asked a
// question is not flapping.
static void EpSay(const char* text, bool interrupt)
{
    const DWORD now = GetTickCount();
    if (s_lastSpokeAt && (now - s_lastSpokeAt) < EP_MIN_GAP_MS) return;
    s_lastSpokeAt = now;
    D3Say("ESTHAR", text, interrupt);
}

// ---------------------------------------------------------------------------
// What to do where you are standing.
// ---------------------------------------------------------------------------
static void ArrivalLine(const EstharSite* site, int t, char* out, size_t n)
{
    const uint8_t used = UsedBits();
    if (site->usedBit && (used & site->usedBit)) {
        snprintf(out, n, "Contact point %d has already been used and will not open again.",
                 site->cp + 1);
        return;
    }
    const bool open = EpSiteOpen(site, t);
    if (site->kind == EP_AUTO) {
        if (open)
            snprintf(out, n, "Contact point %d. Stay exactly where you are -- it takes you "
                             "aboard on its own.", site->cp + 1);
        else {
            char u[48]; EpClock(EpSecondsUntilOpen(site->cp, t), u, sizeof(u));
            snprintf(out, n, "Contact point %d. Wait right here. It opens in %s and takes you "
                             "aboard on its own -- you do not have to do anything.",
                     site->cp + 1, u);
        }
        return;
    }
    // A walk-through line. It is in the catalog as "Contact point N" -- the mod
    // forces it there during the run, because the zone filter had been dropping
    // exactly this entry (see field_catalog.inl) -- so the player can select it
    // and let the GPS walk them onto it.
    //
    // Whether the clock matters here depends on where crossing early puts you.
    // For CP1 and CP3 it puts you on the site that boards you by itself, so the
    // answer is simply GO. Only CP2 needs timing.
    if (EpMissIsAuto(site)) {
        if (open)
            snprintf(out, n, "Contact point %d is open. Head for Contact point %d in the catalog "
                             "and cross it -- that takes you aboard.",
                     site->cp + 1, site->cp + 1);
        else
            snprintf(out, n, "Contact point %d. Head for Contact point %d in the catalog and "
                             "cross it -- you can go now. If you are early it puts you where it "
                             "happens by itself, so you do not have to watch the clock.",
                     site->cp + 1, site->cp + 1);
        return;
    }
    const char* dest = DisplayOfId(site->missField);
    if (open)
        snprintf(out, n, "Contact point %d is open. Head for Contact point %d in the catalog and "
                         "cross it now -- that takes you aboard.", site->cp + 1, site->cp + 1);
    else {
        char u[48]; EpClock(EpSecondsUntilOpen(site->cp, t), u, sizeof(u));
        snprintf(out, n, "Contact point %d. Wait for it to open in %s, then cross Contact point "
                         "%d in the catalog. Crossing early only moves you to %s, and you can "
                         "cross straight back.", site->cp + 1, u, site->cp + 1, dest);
    }
}

static void Announce(int t, bool interrupt)
{
    const int cp = EpTargetPoint(t);
    const char* nm = D3FieldName();
    char clock[64]; EpClock(t, clock, sizeof(clock));
    char line[460];

    int hops = -1; const char* next = nullptr;
    const bool known = EpRoute(nm, cp, &hops, &next);
    const EstharSite* here = EpSiteAt(nm, cp);

    if (here) {
        char what[320];
        ArrivalLine(here, t, what, sizeof(what));
        snprintf(line, sizeof(line), "%s left. %s", clock, what);
    } else if (known && hops == 0) {
        snprintf(line, sizeof(line), "%s left. Contact point %d, %s. You are in the right place.",
                 clock, cp + 1, EpPointPlace(cp));
    } else if (known && hops > 0 && next) {
        char untilTxt[64];
        if (EpWindowOpen(cp, t)) snprintf(untilTxt, sizeof(untilTxt), "open now");
        else { char u[48]; EpClock(EpSecondsUntilOpen(cp, t), u, sizeof(u));
               snprintf(untilTxt, sizeof(untilTxt), "opens in %s", u); }
        snprintf(line, sizeof(line),
                 "%s left. Contact point %d, %s, %s. %d exit%s to go. Take the exit to %s.",
                 clock, cp + 1, EpPointPlace(cp), untilTxt,
                 hops, hops == 1 ? "" : "s", DisplayOf(next));
    } else {
        snprintf(line, sizeof(line),
                 "%s left. Contact point %d, %s. I have no route from here.",
                 clock, cp + 1, EpPointPlace(cp));
    }
    EpSay(line, interrupt);
}

// ---------------------------------------------------------------------------
// NAME ONLY, as v0.56.0 established: the ec field ids are derived, none of this
// is reflex-timed, and a wrong derived range would put Pandora chatter into an
// unrelated scene rather than merely staying quiet.
static bool InCity() { return EpIsCityField(D3FieldName()); }

static void Update(bool slash)
{
    if (!InCity() || !RunLive()) {
        if (s_live) { Log::Field("FieldNavigation: [ESTHAR] run over or left the city"); Reset(); }
        return;
    }

    const int t = Seconds();
    if (t < 0) return;                       // unreadable clock: say nothing

    const int  cp   = EpTargetPoint(t);
    const bool open = EpWindowOpen(cp, t);
    const char* nm  = D3FieldName();

    if (!s_live) {
        s_live = true;
        s_lastPoint = cp; s_lastOpen = open; s_lastMin = t / 60;
        s_lastField[0] = '\0';
        Log::Field("FieldNavigation: [ESTHAR] run live: field '%s' id %u t=%d point=%d open=%d "
                   "tables=%s", nm, (unsigned)D3FieldId(), t, cp + 1, (int)open,
                   EpTablesConsistent() ? "consistent" : "**INCONSISTENT**");
        EpSay("Lunatic Pandora run. Slash for the clock, the contact point, and which exit to take.",
            true);
    }

    const bool movedField = (nm && *nm && _stricmp(nm, s_lastField) != 0);
    if (movedField) {
        strncpy(s_lastField, nm, sizeof(s_lastField) - 1);
        s_lastField[sizeof(s_lastField) - 1] = '\0';
        int hops = -1; const char* next = nullptr;
        EpRoute(nm, cp, &hops, &next);
        const EstharSite* site = EpSiteAt(nm, cp);
        Log::Field("FieldNavigation: [ESTHAR] entered '%s' (id %u) t=%d point=%d open=%d "
                   "hops=%d next='%s' site=%s used=0x%02X",
                   nm, (unsigned)D3FieldId(), t, cp + 1, (int)open, hops, next ? next : "-",
                   site ? (site->kind == EP_AUTO ? "AUTO" : "LINE") : "-", UsedBits());
    }

    // ASKED FIRST. "/" is an explicit request and must always be answered, so
    // it is handled before the transition branches -- one of which can be
    // throttled by the floor and then `return`, swallowing the question. (Found
    // by the probe: a slash immediately after a contact-point transition got
    // nothing back.)
    if (slash) {
        s_lastSpokeAt = 0;
        Announce(t, true);
        s_lastPoint = cp; s_lastOpen = open; s_lastMin = t / 60;
        return;
    }

    // The transitions worth interrupting for, and nothing else. v0.55.0 fired
    // all of these several times a second because the clock flapped; it cannot
    // flap now, and EpSay() carries a floor besides.
    if (cp != s_lastPoint) {
        char b[220];
        snprintf(b, sizeof(b), "Contact point %d has passed. Now heading for point %d, %s.",
                 s_lastPoint + 1, cp + 1, EpPointPlace(cp));
        EpSay(b, true);
        s_lastPoint = cp; s_lastOpen = open;
        Announce(t, false);
        return;
    }
    if (open && !s_lastOpen) { s_lastOpen = open; Announce(t, true); return; }
    s_lastOpen = open;

    if (movedField) { Announce(t, false); return; }

    const int mins = t / 60;
    if (mins != s_lastMin) {
        if (s_lastMin >= 0 && t <= 300 && (t % 60) == 0) {
            char b[64];
            snprintf(b, sizeof(b), "%d minute%s.", mins, mins == 1 ? "" : "s");
            EpSay(b, false);
        }
        s_lastMin = mins;
    }

}

} // namespace Esthar
