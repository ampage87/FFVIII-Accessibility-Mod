// esthar_pandora_model.inl -- the PURE model of the Esthar "board Lunatic
// Pandora" run (field set `ec*`, disc 3).
//
// PART OF field_navigation.cpp -- TEXTUAL INCLUDE. Do NOT compile standalone.
// Compiled standalone by tests/disc3_models_compile.cpp. Nothing here touches
// game memory, Windows or the screen reader.
//
// ============================================================================
// v0.57.0 (#110): REWRITTEN AFTER THE 2026-08-22 BAT. WHAT v0.55.0 GOT WRONG.
// ============================================================================
//
// Aaron reached the run and the mod told him he was standing on contact point 1
// with the window open. Nothing happened. Three separate faults, and the third
// is the one that made the other two unreadable.
//
// **1. THE MECHANISM IS A WALK-THROUGH LINE, NOT A PLACE TO STAND.**
//
// Boarding is a MAPJUMP to field 417/418/419 (ecenc1/2/3). There are exactly
// eight in the whole game -- an exhaustive scan of all 892 fields finds no
// others -- and six of them are `Linejump1::touch` / `Linejump2::touch`: the
// engine's `touch` event, which fires when the player WALKS THROUGH a trigger
// line. On every one of those entities `talk`, `push`, `across`, `touchOff` and
// `touchOn` are two-instruction stubs, so walking through the line is the only
// way to fire any of them.
//
// v0.55.0 told the player to WAIT. Waiting is the one thing that cannot work at
// a `touch` line. (It also named the wrong methods -- see the decode note
// below -- and attributed the trigger to lifters and to pushing the Director;
// none of that is on any boarding path.)
//
// The other two sites ARE stand-and-wait, and they are the ones worth aiming
// at: `eciway11 :: JumpTimer::defalut` and `ecoway3 :: TimerJump::defalut` are
// polling loops that fire by themselves the moment the window opens.
//
// **AND THE TWO KINDS COMPOSE.** Every line's miss-branch -- what happens when
// you cross it outside the window -- is an ordinary map transition to a
// neighbour, and in five of six cases that neighbour is a trigger for the SAME
// contact point. eccway11 and eccway21 both miss to **eciway11**, which then
// fires CP1 automatically. So "walk to eciway11 and wait" is correct whether
// the window is open when you cross or not: early, you land there and it fires
// later; open, you board on the way. That is the instruction this file exists
// to produce.
//
// **2. THE TIMER WAS READ FROM THE WRONG PLACE, AND IT FLOODED THE SPEECH.**
//
// v0.55.0 preferred `var[1024]`, falling back to the engine dword. var[1024] is
// the timer HUD mirror that `info::default` re-establishes EVERY FRAME, so
// reads catch it mid-update and return 0. In the BAT the clock alternated
// between the true value and zero on consecutive ticks, which flipped the
// target contact point 3 -> 1 -> 3 and fired the "point has passed" and
// "window open" announcements over and over: **six interrupting utterances in
// a single second, ~420 log lines a second, for the whole run.** That is also
// Aaron's *"it was breaking auto-drive somehow"* -- nothing else could be heard
// through it.
//
// The truth is one address. GETTIMER (opcode 0x0A4, handler 0x00521710) is
// `mov eax,[0x01CFE92C]`, and every one of the eight boarding gates reads the
// countdown that way. So does this file, and nothing else.
//
// **3. THE WINDOWS ARE NOT UNIFORM PER CONTACT POINT.**
//
// The display ladder in `eccway11 :: Disp::default` uses 720/900/300/600/180,
// and the briefing quotes 15:00-12:00, 10:00-5:00 and 3:00-0:00 -- but the
// GATES are per-site and three of them differ:
//
//     eciway11  CP1  t > 720 && t <= 900     (the automatic one)
//     eccway11  CP1  t > 720 && t <= 903
//     eccway21  CP1  t > 720 && t <= 903
//     eccway12  CP2  t > 300 && t <= 600
//     eccway41  CP2  t > 300 && t <= 600
//     eccway41  CP3  t > 0   && t <= 180
//     ecoway3   CP3  t > 0   && t <= 180     (the automatic one)
//     ecmall1   CP3  t > 0   && t <  180     <-- LT, one second narrower
//
// This file carries each site's own bound. The planning windows below are the
// intersection, so a "window is open" claim is true at every site of that point.
//
// ============================================================================
// THE GATES, AND WHICH ONES CAN BITE
// ============================================================================
//
//   var[672] == 19     the run is live. Tested by all eight. Nothing sets it
//                      back to 19, so the run is one-shot.
//   var[678] bit 0     CP1 already attempted -- blocks eciway11, eccway11 and
//                      eccway21. Set unconditionally by ecenc1's Director.
//   var[678] bit 1     CP2 already attempted -- blocks eccway12 and eccway41's
//                      CP2 line. Set unconditionally by ecenc2's Director.
//   var[678] bit 2     **NOT "CP3 used".** ecenc3 sets it only on the
//                      timer-expired exit, and NO gate anywhere reads it. The
//                      three CP3 triggers carry no var[678] test at all, so CP3
//                      can be retried.
//   var[1029] == 0     required by the two AUTOMATIC sites only, and failing it
//                      does not skip one tick -- it exits the polling loop for
//                      that whole visit. It is a per-field-load scratch byte:
//                      the engine zeroes var[1024..1279] in the field-script
//                      loader (0x0052C426, `mov edi,0x1CFEDB8; mov ecx,0x40;
//                      rep stosd`, the only such write in the image), so
//                      walking into the field clears it. It cannot be left dirty
//                      by talking to someone in a previous field.
//
// ============================================================================
// A DECODE NOTE THAT COST v0.55.0 ITS METHOD NAMES
// ============================================================================
//
// .jsm entry offsets carry a 0x8000 flag the engine masks off itself
// (0x0052BF98 / 0x0052C0A4, `and eax, 0x7fff`). Read unmasked, eccway21's
// entries[210..217] come out as 36247.. against a 6756-dword code array, seven
// methods get swallowed into their predecessor, and the CP1 trigger reads as
// `Edea::afkantei2` instead of `Linejump1::touch`. Every masked value lands on
// opcode 0x005, the SCRIPT marker. The offline loader masks now.

// ---------------------------------------------------------------------------
// Addresses and variables.
// ---------------------------------------------------------------------------
static const int      EP_VAR_MISSION = 672;          // == 19 while the run is live
static const int      EP_MISSION_LIVE = 19;
static const int      EP_VAR_USED    = 678;          // bit0 CP1 attempted, bit1 CP2
static const int      EP_VAR_BUSY    = 1029;         // must be 0 at the automatic sites
static const uint32_t EP_TIMER_ADDR  = 0x01CFE92Cu;  // GETTIMER's source, whole seconds

// Planning windows: the INTERSECTION of every site's own gate for that point,
// so "open" is true wherever the player happens to be standing.
static const int EP_CP1_LO = 720, EP_CP1_HI = 900;   // 12:00 .. 15:00
static const int EP_CP2_LO = 300, EP_CP2_HI = 600;   //  5:00 .. 10:00
static const int EP_CP3_LO = 0,   EP_CP3_HI = 180;   //  0:00 ..  3:00

// The three contact points, in the order the clock reaches them (t counts DOWN).
enum { EP_CP1 = 0, EP_CP2 = 1, EP_CP3 = 2, EP_CP_COUNT = 3 };

// The field each contact point is routed to. CP1 and CP3 have a site that fires
// by itself; CP2 has none, so it is routed to eccway12 and the player is told to
// walk through to eccway41 (the trigger is the single doorway between them and
// it works in either direction).
static const int EP_TARGET_FIELD[EP_CP_COUNT] = { 425, 404, 458 };  // eciway11, eccway12, ecoway3

static const char* EpPointPlace(int cp)
{
    switch (cp) {
        case EP_CP1: return "the centre of the city";
        case EP_CP2: return "where the two skyways cross";
        default:     return "north of the shopping mall";
    }
}

// ---------------------------------------------------------------------------
// The eight boarding sites.
// ---------------------------------------------------------------------------
enum EpSiteKind {
    EP_AUTO = 0,   // a polling loop: stand in the field and it fires
    EP_LINE = 1    // a `touch` trigger line: walk through it
};

struct EstharSite {
    const char* field;
    uint8_t     cp;          // EP_CP1 / EP_CP2 / EP_CP3
    uint8_t     kind;        // EP_AUTO / EP_LINE
    int         lo, hi;      // the site's OWN gate: t > lo && t <= hi ...
    bool        hiExclusive; // ...unless this, then t < hi (ecmall1 only)
    uint8_t     usedBit;     // var[678] bit that blocks it, 0 = none
    int         lineX, lineY;// midpoint of the trigger line (EP_LINE only)
    int         missField;   // where crossing outside the window puts you
};

// Line midpoints are the mean of the LINE opcode's two endpoints, read out of
// each entity's own init script.
static const EstharSite EP_SITES[] = {
    { "eciway11", EP_CP1, EP_AUTO, 720, 900, false, 0x01,      0,     0, 425 },
    { "ecoway3",  EP_CP3, EP_AUTO,   0, 180, false, 0x00,      0,     0, 458 },
    { "eccway11", EP_CP1, EP_LINE, 720, 903, false, 0x01,  -4755,   525, 425 },
    { "eccway21", EP_CP1, EP_LINE, 720, 903, false, 0x01,   3679,  1248, 425 },
    { "eccway12", EP_CP2, EP_LINE, 300, 600, false, 0x02,    466,  1172, 414 },
    { "eccway41", EP_CP2, EP_LINE, 300, 600, false, 0x02,  -5033, -4205, 404 },
    { "eccway41", EP_CP3, EP_LINE,   0, 180, false, 0x00,  -1714, -6966, 458 },
    { "ecmall1",  EP_CP3, EP_LINE,   0, 180, true,  0x00,   -566,  3057, 458 },
};
static const int EP_SITE_COUNT = (int)(sizeof(EP_SITES) / sizeof(EP_SITES[0]));

// ---------------------------------------------------------------------------
// The route table: for each city field, hops-to-go and the NEXT FIELD to head
// for, per contact point. Hops -1 means unreachable, 0 means you are there.
// GENERATED -- see offline/gen_esthar_tables.py.
// ---------------------------------------------------------------------------
struct EstharHopCell { int8_t hops; int16_t next; };
struct EstharHop { int16_t id; const char* field; EstharHopCell to[EP_CP_COUNT]; };

static const EstharHop EP_HOPS[] = {
    { 402, "eccway11", { {1,425}, {1,404}, {3,404} } },   // Esthar - City 1
    { 404, "eccway12", { {2,408}, {0,0}, {2,414} } },   // Esthar - City 3
    { 406, "eccway13", { {5,424}, {5,424}, {5,424} } },   // Esthar - City 5
    { 407, "eccway14", { {6,406}, {6,406}, {6,406} } },   // Esthar - City 6
    { 408, "eccway21", { {1,425}, {1,404}, {3,404} } },   // Esthar - City 7
    { 409, "eccway22", { {7,438}, {7,438}, {7,438} } },   // Esthar - City 8
    { 411, "eccway31", { {3,427}, {3,427}, {5,446} } },   // Esthar - City 10
    { 412, "eccway32", { {9,465}, {9,465}, {9,465} } },   // Esthar - City 11
    { 414, "eccway41", { {3,404}, {1,404}, {1,458} } },   // Esthar - City 13
    { 415, "eccway42", { {7,459}, {7,459}, {7,459} } },   // Esthar - City 14
    { 417, "ecenc1", { {1,425}, {3,425}, {5,425} } },   // Lunatic Pandora Approaching 1
    { 418, "ecenc2", { {3,404}, {1,404}, {3,404} } },   // Lunatic Pandora Approaching 2
    { 419, "ecenc3", { {5,458}, {3,458}, {1,458} } },   // Lunatic Pandora Approaching 3
    { 420, "ecenter1", { {2,402}, {2,402}, {2,431} } },   // Esthar - City 16
    { 422, "ecenter2", { {3,420}, {3,420}, {3,420} } },   // Esthar - City 18
    { 424, "ecenter3", { {4,422}, {4,422}, {4,422} } },   // Esthar - City 20
    { 425, "eciway11", { {0,0}, {2,402}, {4,402} } },   // Esthar - City 21
    { 427, "eciway12", { {2,408}, {2,408}, {4,408} } },   // Esthar - City 23
    { 429, "eciway13", { {6,406}, {6,406}, {6,406} } },   // Esthar - City 25
    { 430, "eciway14", { {8,409}, {8,409}, {8,409} } },   // Esthar - City 26
    { 431, "ecmall1", { {3,420}, {3,420}, {1,458} } },   // Esthar - City 27
    { 432, "ecmall1a", { {5,424}, {5,424}, {5,424} } },   // Esthar - City 28
    { 434, "ecmview1", { {3,437}, {3,437}, {5,437} } },   // Esthar - Odine's Laboratory 1
    { 435, "ecmview2", { {7,438}, {7,438}, {7,438} } },   // Esthar - Odine's Laboratory 2
    { 437, "ecmway1", { {2,408}, {2,408}, {4,408} } },   // Esthar - City 31
    { 438, "ecmway1a", { {6,453}, {6,453}, {6,453} } },   // Esthar - City 32
    { 440, "ecopen1", { {3,446}, {2,443}, {3,443} } },   // Esthar - City 34
    { 441, "ecopen1a", { {8,444}, {8,444}, {8,444} } },   // Esthar - City 35
    { 443, "ecopen2", { {3,404}, {1,404}, {2,414} } },   // Esthar - City 37
    { 444, "ecopen2a", { {7,407}, {7,407}, {7,407} } },   // Esthar - City 38
    { 446, "ecopen3", { {2,408}, {2,408}, {4,440} } },   // Esthar - City 40
    { 447, "ecopen3a", { {8,409}, {8,409}, {8,409} } },   // Esthar - City 41
    { 449, "ecopen4", { {4,440}, {3,440}, {4,440} } },   // Esthar - Presidential Palace 1
    { 450, "ecopen4a", { {9,441}, {9,441}, {9,441} } },   // Esthar - Presidential Palace 2
    { 452, "ecoway1", { {3,437}, {3,420}, {3,420} } },   // Esthar - City 44
    { 453, "ecoway1a", { {5,424}, {5,424}, {5,424} } },   // Esthar - City 45
    { 455, "ecoway2", { {3,437}, {3,437}, {5,437} } },   // Esthar - City 47
    { 456, "ecoway2a", { {7,438}, {7,438}, {7,438} } },   // Esthar - City 48
    { 458, "ecoway3", { {4,414}, {2,414}, {0,0} } },   // Esthar - City 50
    { 459, "ecoway3a", { {6,432}, {6,432}, {6,432} } },   // Esthar - City 51
    { 461, "ecpview1", { {5,464}, {5,464}, {7,464} } },   // Esthar - Airstation
    { 462, "ecpview2", { {9,465}, {9,465}, {9,465} } },   // Esthar - City 53
    { 464, "ecpway1", { {4,411}, {4,411}, {6,411} } },   // Esthar - City 55
    { 465, "ecpway1a", { {8,456}, {8,456}, {8,456} } },   // Esthar - City 56
    { 467, "ectake1", { {6,468}, {5,468}, {6,468} } },   // Esthar - City 58
    { 468, "ectake2", { {5,469}, {4,469}, {5,469} } },   // Esthar - City 59
    { 469, "ectake3", { {4,440}, {3,440}, {4,440} } },   // Esthar - City 60
};
static const int EP_HOP_COUNT = (int)(sizeof(EP_HOPS) / sizeof(EP_HOPS[0]));

// ---------------------------------------------------------------------------
// Queries.
// ---------------------------------------------------------------------------
static const EstharHop* EpHopRow(const char* field)
{
    if (!field || !*field) return nullptr;
    for (int i = 0; i < EP_HOP_COUNT; i++)
        if (_stricmp(EP_HOPS[i].field, field) == 0) return &EP_HOPS[i];
    return nullptr;
}

static bool EpIsCityField(const char* field) { return EpHopRow(field) != nullptr; }

static const char* EpFieldName(int id)
{
    for (int i = 0; i < EP_HOP_COUNT; i++)
        if (EP_HOPS[i].id == id) return EP_HOPS[i].field;
    return nullptr;
}

// Which contact point to be heading for at time t. The clock counts DOWN, so
// CP1's window (900..720) comes first. A point whose window has closed is
// finished; the answer is the next one that has not.
static int EpTargetPoint(int t)
{
    if (t > EP_CP1_LO) return EP_CP1;      // before or inside CP1's window
    if (t > EP_CP2_LO) return EP_CP2;
    return EP_CP3;
}

// Is that point's window open right now?
static bool EpWindowOpen(int cp, int t)
{
    switch (cp) {
        case EP_CP1: return t > EP_CP1_LO && t <= EP_CP1_HI;
        case EP_CP2: return t > EP_CP2_LO && t <= EP_CP2_HI;
        default:     return t > EP_CP3_LO && t <= EP_CP3_HI;
    }
}

// Seconds until that point's window opens (0 if it is open or already past).
static int EpSecondsUntilOpen(int cp, int t)
{
    const int hi = (cp == EP_CP1) ? EP_CP1_HI : (cp == EP_CP2) ? EP_CP2_HI : EP_CP3_HI;
    return (t > hi) ? (t - hi) : 0;
}

// Seconds until it shuts (0 if not open).
static int EpSecondsUntilShut(int cp, int t)
{
    if (!EpWindowOpen(cp, t)) return 0;
    const int lo = (cp == EP_CP1) ? EP_CP1_LO : (cp == EP_CP2) ? EP_CP2_LO : EP_CP3_LO;
    return t - lo;
}

// The site the player is standing in for a given contact point, or null.
static const EstharSite* EpSiteAt(const char* field, int cp)
{
    if (!field || !*field) return nullptr;
    for (int i = 0; i < EP_SITE_COUNT; i++)
        if (EP_SITES[i].cp == (uint8_t)cp && _stricmp(EP_SITES[i].field, field) == 0)
            return &EP_SITES[i];
    return nullptr;
}

// This site's own gate, which is not always the planning window.
static bool EpSiteOpen(const EstharSite* s, int t)
{
    if (!s) return false;
    return (t > s->lo) && (s->hiExclusive ? (t < s->hi) : (t <= s->hi));
}

// Route: hops to the target field for `cp`, and the next field to head for.
// Returns false when the field is not in the table.
static bool EpRoute(const char* field, int cp, int* outHops, const char** outNext)
{
    const EstharHop* r = EpHopRow(field);
    if (!r || cp < 0 || cp >= EP_CP_COUNT) return false;
    const EstharHopCell& c = r->to[cp];
    if (outHops) *outHops = c.hops;
    if (outNext) *outNext = (c.hops > 0) ? EpFieldName(c.next) : nullptr;
    return true;
}

// mm:ss in words, for the clock readout.
static void EpClock(int secs, char* out, size_t n)
{
    if (secs < 0) secs = 0;
    const int m = secs / 60, s = secs % 60;
    if (m == 0) snprintf(out, n, "%d second%s", s, s == 1 ? "" : "s");
    else        snprintf(out, n, "%d minute%s %d second%s",
                         m, m == 1 ? "" : "s", s, s == 1 ? "" : "s");
}

// ---------------------------------------------------------------------------
// v0.57.1 (#110): CAN THIS LINE BE CROSSED EARLY WITHOUT COST?
//
// Aaron, after the 2026-08-22 BAT: *"The instructions said something about
// having to cross at a certain time, but that is not correct -- for all three
// contact points if you get to the right point and just wait it will trigger."*
//
// He is right about two of the three, and the scripts say exactly which two.
// A line's miss-branch -- where crossing outside the window puts you -- is a
// neighbouring field, and for the CP1 and CP3 lines that neighbour is the
// stand-and-wait site for the SAME contact point:
//
//     eccway11 CP1 --miss--> eciway11   (polls, boards by itself)
//     eccway21 CP1 --miss--> eciway11
//     eccway41 CP3 --miss--> ecoway3    (polls, boards by itself)
//     ecmall1  CP3 --miss--> ecoway3
//
// So for CP1 and CP3 the honest instruction is **go now, whatever the clock
// says**: cross during the window and you board on the spot, cross early and
// you land on the site that boards you when it opens. Either way you end up
// aboard without watching a timer, which is what Aaron experienced.
//
// CP2 is the exception and it is a real one. Its two lines are the single
// doorway between eccway12 and eccway41, and each misses to the OTHER --
// neither of which polls. Crossing early there just walks you back and forth.
// v0.57.0 told the player to time every crossing; that was wrong for two
// points out of three and needlessly hard for a blind player.
static bool EpMissIsAuto(const EstharSite* s)
{
    if (!s || s->kind != EP_LINE) return false;
    const char* miss = EpFieldName(s->missField);
    if (!miss) return false;
    const EstharSite* m = EpSiteAt(miss, s->cp);
    return m && m->kind == EP_AUTO;
}

// ---------------------------------------------------------------------------
// v0.57.1 (#110): is this camera-transition line a contact-point trigger?
//
// The catalog asks. Six of the eight boarding sites are camera lines, and the
// v0.20.29 zone filter drops a camera line that is not a boundary of the
// player's own zone -- which in eccway21 dropped the only thing the player
// needed to reach. Matching is by FIELD plus the line's midpoint, against the
// midpoints read out of each entity's own LINE opcode; the runtime capture and
// the offline extraction agree to within one unit on all six, so the tolerance
// below is slack rather than fudge.
//
// The caller must ALSO satisfy itself that the run is live -- this answers
// "is this the contact-point line", not "does it matter right now".
static const int EP_LINE_MATCH_UNITS = 64;

static const EstharSite* EpContactLineAt(const char* field, int cx, int cy)
{
    if (!field || !*field) return nullptr;
    for (int i = 0; i < EP_SITE_COUNT; i++) {
        const EstharSite& s = EP_SITES[i];
        if (s.kind != EP_LINE) continue;
        if (_stricmp(s.field, field) != 0) continue;
        const int dx = cx - s.lineX, dy = cy - s.lineY;
        if (dx > -EP_LINE_MATCH_UNITS && dx < EP_LINE_MATCH_UNITS &&
            dy > -EP_LINE_MATCH_UNITS && dy < EP_LINE_MATCH_UNITS)
            return &s;
    }
    return nullptr;
}

// The whole table is generated, so a transcription slip would be silent.
// Checked by tests/disc3_models_compile.cpp against the shipped .jsm bytes.
static bool EpTablesConsistent()
{
    for (int i = 0; i < EP_SITE_COUNT; i++) {
        const EstharSite& s = EP_SITES[i];
        if (s.cp >= EP_CP_COUNT) return false;
        if (s.lo >= s.hi) return false;
        if (!EpIsCityField(s.field)) return false;
        if (!EpFieldName(s.missField)) return false;
        if (s.kind == EP_LINE && s.lineX == 0 && s.lineY == 0) return false;
    }
    for (int cp = 0; cp < EP_CP_COUNT; cp++) {
        const char* tf = EpFieldName(EP_TARGET_FIELD[cp]);
        if (!tf) return false;
        int hops = -1;
        if (!EpRoute(tf, cp, &hops, nullptr) || hops != 0) return false;
    }
    for (int i = 0; i < EP_HOP_COUNT; i++)
        for (int cp = 0; cp < EP_CP_COUNT; cp++) {
            const EstharHopCell& c = EP_HOPS[i].to[cp];
            if (c.hops > 0 && !EpFieldName(c.next)) return false;
        }
    return true;
}
