// menu_tts_gf.inl — Main-menu GF screen TTS (#41), v0.18.0 chapter
// Included from menu_tts.cpp AFTER menu_tts_diagnostics.inl so it can use
// SAVEMAP_BASE, DecodeNameToBuffer(), GetAbilityName(), pMenuStateA, the
// Log/ScreenReader/FieldDialog/FF8TextDecode facilities, and (if ever needed)
// IsSubmonNoiseOffset(). Do not compile independently.
//
// PRODUCTION GF-name TTS (#41) + diagnostics. AnnounceGFOnCursorChange() speaks
// the highlighted GF's name (from the savemap, keyed on the +0x253 cell index)
// on every cursor move. While GF_DIAG=1 a [GFDIAG] harness also runs, which,
// while the top-level menu cursor sits on GF (index 4) in mode 6:
//   1. Dumps the savemap GF block once per screen entry (16 records x 0x44,
//      base savemap+0x4C) — raw bytes + decoded name + an EXP candidate — so
//      the on-screen GF order can be correlated to savemap records.
//   2. Polls a narrow byte band of pMenuStateA (0x1C0..0x2C0, where the Item
//      and Junction cursors were found) every ~150 ms and logs any change —
//      this surfaces the GF list cursor as the player arrows through the list.
//   3. Logs the rendered GCW menu text on change — shows what the screen says.
// None of this requires an on-screen-timed keypress: the player just opens the
// menu, arrows onto GF, enters, and navigates. The generic SUBMON 4KB monitor
// also runs here (cursor stable on 4) as an independent cross-check.
//
// Flip GF_DIAG to 0 to compile the whole harness out once the offsets are
// confirmed; the dispatch scaffold (PollGFSubmenu/ResetGFSubmenuState) stays.

#define GF_DIAG 0

// v0.18.0.13 (#44): AP-readout feasibility probe, independent of the broad
// GF_DIAG harness. Fires on each Learn-list row move and logs the full decoded
// GCW (chunked), the raw GCW hex, and the displayed GF's savemap AP array so we
// can settle ONE question: do the AP numbers the game shows render as text
// (present in the GCW -> we scrape them) or as sprites (absent -> we use the
// kernel / deep-research AP-cost table)? RESOLVED v0.18.0.14: the AP numbers are
// NOT in the GCW (sprite-drawn), so we use the baked ability_ap_cost table.
// Gated off; the probe stays for future re-validation.
#define GF_AP_DIAG 0

// GF savemap block: 16 records of 0x44 bytes starting at savemap+0x4C.
// Leading 12 bytes are the GF name (FF8-encoded, +0x20 like other savemap
// names) — confirmed by VerifySavemapOffsets' GF[0] read. Remaining fields
// (exp / AP / learned-ability / kills) are to be mapped from this dump.
static const int GF_BLOCK_OFFSET = 0x4C;
static const int GF_RECORD_SIZE  = 0x44;   // 68 bytes
static const int GF_REC_COUNT    = 16;     // GF records in the savemap block

// Cursor-candidate band within pMenuStateA (covers the 0x22E..0x27F cluster
// where Item/Junction cursors lived, plus a margin on both sides).
static const int GF_BAND_LO = 0x1C0;
static const int GF_BAND_HI = 0x2C0;       // exclusive; 256 bytes

static bool    s_gfActive    = false;      // on the GF screen (top cursor == 4)
static bool    s_gfDumped    = false;      // GF block dumped this entry
static bool    s_gfSnapValid = false;      // band baseline captured
static DWORD   s_gfLastPoll  = 0;
static uint8_t s_gfBandSnap[GF_BAND_HI - GF_BAND_LO] = {};

// Production (#41): GF-list cursor announce state. +0x253 = canonical GF cell
// index 0..15 (confirmed v0.18.0). Announce on index CHANGE only;
// s_gfLastWasEmpty de-dupes runs of un-obtained cells.
static int  s_gfLastIdx      = -1;
static bool s_gfLastWasEmpty = false;

// Production (#41, v0.18.0.1): GF DETAIL screen number-key state.
// s_gfDetailActive is true while the GF detail stat panel is showing (and stays
// true on the Learn ability list, which keeps the panel visible on the left —
// per design, keys 1..7 remain active there). Detected from the GCW
// "Compatibility" label, throttled. While active, number keys 1..7 each read
// one field (Scan-screen model). No manual edge state needed: GetAsyncKeyState
// & 1 returns the press-since-last-poll bit, same as the M/G/T/L/R hotkeys.
static bool  s_gfDetailActive = false;
static DWORD s_gfPhasePoll     = 0;

// v0.18.0.6 / v0.18.0.7 detail-panel extras:
//  - s_gfDetailWasActive: edge-detect the panel appearing -> fire the entry
//    hint once (Scan-screen model).
//  - s_gfDetailIdx: the GF currently DISPLAYED on the detail panel. Q/R cycle
//    the displayed GF but do NOT move the grid cursor +0x253 (confirmed in the
//    v0.18.0.6 BAT: the detail probe kept reading GF[0] while the screen showed
//    Shiva/Ifrit/Siren/Diablos, and the displayed-GF index is not anywhere in
//    the 0x1C0..0x2C0 band). Instead we read the displayed GF out of the
//    rendered GCW: the panel header is always "<GFName>LVHP/Compatibility...",
//    so we match each obtained GF's savemap name against the GCW and take the
//    right-most "<name>LV" hit (freshest render). s_gfDetailIdx then drives
//    BOTH the number keys (so they read the displayed GF, not the entry GF) and
//    the Q/R auto-announce. s_gfDetailLastIdx de-dupes the announce.
static bool  s_gfDetailWasActive = false;
static DWORD s_gfDetailHintTick   = 0;
static int   s_gfDetailIdx        = -1;
static int   s_gfDetailLastIdx    = -1;

// Production (#41, v0.18.0.9): GF LEARN-LIST (ability-to-learn) announce state.
// The Learn list lives inside the same detail panel (s_gfDetailActive stays
// true). Cursor = pMenuStateA + 0x258, a 0-based index into the DISPLAYED list
// (confirmed in the v0.18.0.8 BAT: +0x258 tracked the help-text 1:1 across
// Siren's 8 rows). The displayed list is a filtered subset of the GF's kernel
// slots, so rather than reconstruct + replicate the engine's filter we read the
// real list straight out of the rendered GCW (the names sit between the help
// text and the "<GFName>LV" stat-panel header) and index it by +0x258. AP
// numbers (current/total) need the per-GF kernel table (#44) and come in v2;
// v1 announces name + learned/learning status + the help description.
// v0.18.0.10: the list is PAGINATED and NOT filtered (Shiva page 1 = 11 rows
// incl. Str-J/Magic/GF/Draw/Item; page 2 = 5 rows). The rendered GCW shows only
// the current page, and the highlight cursor lives in a DIFFERENT byte per page:
// page 1 (top) tracked +0x257, page 2 (scrolled) tracked +0x258 — both 0-based
// into the rendered page. So we read BOTH bytes and use whichever just changed
// and is in range, de-duping on the resolved ability id. A window log
// ([GFLEARN]) captures the full picture for a clean confirmation pass.
static int   s_gfLearnLastId  = -1;     // last announced ability id (dedupe)
static int   s_gfLearnPrev257 = -1;
static int   s_gfLearnPrev258 = -1;
static DWORD s_gfLearnPoll     = 0;

// v0.18.0.13 (#3): stored selection for the on-demand "/" help re-read. The
// Learn list already speaks "<name>, <status>. <desc>" as the cursor moves;
// this captures the row CURRENTLY under the cursor so the "/" key can re-read
// its help text at any time. This is deliberately distinct from detail key 5,
// which reads the ability being LEARNED. Refreshed on each row announce; valid
// only while the Learn list is up (cleared when the list closes / on reset) so
// "/" falls back to the normal help-bar reader everywhere else.
static bool s_gfLearnSelValid     = false;
static int  s_gfLearnSelId        = -1;    // unified id of the stored row (-1 = empty slot)
static char s_gfLearnSelName[64]  = {};
static char s_gfLearnSelDesc[192] = {};

// Reset on leaving the GF screen (or menu open/close).
static void ResetGFSubmenuState()
{
    s_gfActive    = false;
    s_gfDumped    = false;
    s_gfSnapValid = false;
    s_gfLastPoll  = 0;
    s_gfLastIdx      = -1;
    s_gfLastWasEmpty = false;
    s_gfDetailActive = false;
    s_gfPhasePoll    = 0;
    s_gfDetailWasActive = false;
    s_gfDetailHintTick  = 0;
    s_gfDetailIdx       = -1;
    s_gfDetailLastIdx   = -1;
    s_gfLearnLastId     = -1;
    s_gfLearnPrev257    = -1;
    s_gfLearnPrev258    = -1;
    s_gfLearnPoll       = 0;
    s_gfLearnSelValid   = false;
    s_gfLearnSelId      = -1;
    s_gfLearnSelName[0] = '\0';
    s_gfLearnSelDesc[0] = '\0';
}

#if GF_DIAG
// SEH-free per VerifySavemapOffsets/DumpMenuScreenData precedent (diagnostic;
// a crash here is acceptable and informative). Uses only char[] + sprintf +
// DecodeNameToBuffer (no std::string in this frame).
static void GFDiagDumpBlock()
{
    Log::Menu("[GFDIAG] === GF block dump (savemap+0x%02X, %d x 0x%02X) ===",
               GF_BLOCK_OFFSET, GF_REC_COUNT, GF_RECORD_SIZE);
    uint8_t* base = (uint8_t*)SAVEMAP_BASE + GF_BLOCK_OFFSET;
    for (int g = 0; g < GF_REC_COUNT; g++) {
        uint8_t* rec = base + g * GF_RECORD_SIZE;
        // Name: first 12 bytes, +0x20 adjusted like the other savemap names.
        uint8_t adj[12];
        for (int i = 0; i < 12; i++)
            adj[i] = (rec[i] >= 0x20) ? (rec[i] - 0x20) : rec[i];
        char nm[32] = {};
        DecodeNameToBuffer(adj, 12, nm, sizeof(nm));
        // Candidate EXP: uint32 right after the 12-byte name.
        uint32_t u32_0C = *(uint32_t*)(rec + 0x0C);
        // Full 68-byte raw record.
        char hex[3 * GF_RECORD_SIZE + 4] = {};
        int hp = 0;
        for (int i = 0; i < GF_RECORD_SIZE; i++)
            hp += sprintf(hex + hp, "%02X ", rec[i]);
        Log::Menu("[GFDIAG] GF[%2d] name='%s' u32@0x0C=%u", g, nm, u32_0C);
        Log::Menu("[GFDIAG]   raw=%s", hex);
    }
    Log::Menu("[GFDIAG] === end GF block dump ===");
}

// Minimal render-noise filter for the band (intentionally lighter than the
// generic IsSubmonNoiseOffset: keeps 0x1E8/0x1E9 etc. VISIBLE because those
// are submenu-state candidates here).
static bool GFDiagIsNoise(int off)
{
    return (off == 0x1CA || off == 0x1CE || off == 0x1E4 ||
            off == 0x1E5 || off == 0x1E6 || off == 0x1EA || off == 0x1FD);
}

// SEH-isolated band poller — NO C++ objects in this frame (C2712-safe).
static void GFDiagPollBand()
{
    __try {
        uint8_t* base = (uint8_t*)pMenuStateA;
        const int n = GF_BAND_HI - GF_BAND_LO;
        if (!s_gfSnapValid) {
            memcpy(s_gfBandSnap, base + GF_BAND_LO, n);
            s_gfSnapValid = true;
            Log::Menu("[GFDIAG] band baseline captured (+0x%03X..+0x%03X)",
                       GF_BAND_LO, GF_BAND_HI);
            return;
        }
        for (int i = 0; i < n; i++) {
            uint8_t cur = base[GF_BAND_LO + i];
            if (cur != s_gfBandSnap[i]) {
                int off = GF_BAND_LO + i;
                if (!GFDiagIsNoise(off))
                    Log::Menu("[GFDIAG] +0x%03X: %u -> %u", off,
                               (unsigned)s_gfBandSnap[i], (unsigned)cur);
                s_gfBandSnap[i] = cur;
            }
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
}

// Detail-screen value-discovery probe (one dump per detail entry).
//
// v0.18.0 breakthrough: the authoritative FFNx struct `savemap_ff8_character`
// (save_data.h) shows the four Compatibility numbers are NOT in the GF record
// and NOT in pMenuStateA — they live in each CHARACTER's struct as
// `gf_compat[16]` at char-offset +0x70 (u16 per GF, indexed by canonical GF id).
// The GF record itself (savemap_ff8_gf, 0x44 bytes) gives us HP (+0x12), exp
// (+0x0C), the learning-ability index (+0x40), and the per-ability AP array
// APs[24] (+0x24). So everything the detail screen shows is reachable from the
// savemap directly; level + next-level-EXP are computed from exp.
//
// This probe DUMPS those exact fields for the highlighted GF so we can confirm
// the model against the on-screen BAT values (Diablos: HP 730, EXP 4000,
// compat Squall648/Zell573/Quistis600/Selphie606, learning GFHP+10%, AP 0/40)
// before wiring the number keys. We resolve the four ACTIVE party members from
// the party array and read each one's gf_compat[gfIdx]. SEH-guarded, scalar
// only (C2712-safe). One dump per detail entry.
static bool  s_gfDetailProbed = false;
static DWORD s_gfDetailProbeTick = 0;
static void GFDiagProbeDetail()
{
    __try {
        uint8_t* sm = (uint8_t*)SAVEMAP_BASE;
        uint8_t* ms = (uint8_t*)pMenuStateA;
        int gfIdx = ms[0x253];   // diagnostic: independent grid-cursor read
        if (gfIdx < 0 || gfIdx >= GF_REC_COUNT) return;

        uint8_t* rec = sm + GF_BLOCK_OFFSET + gfIdx * GF_RECORD_SIZE;
        uint32_t exp   = *(uint32_t*)(rec + 0x0C);
        uint8_t  exists = rec[0x11];
        uint16_t hp    = *(uint16_t*)(rec + 0x12);
        uint8_t  learning = rec[0x40];
        // AP for the learning ability: APs[24] starts at +0x24, one byte each,
        // indexed by the same ability index the 'learning' field holds.
        uint8_t  apForLearning = (learning < 24) ? rec[0x24 + learning] : 0xFF;

        Log::Menu("[GFDIAG] === detail field probe (GF[%d]) ===", gfIdx);
        Log::Menu("[GFDIAG]   GF rec: exp=%u exists=%u HP=%u learning=0x%02X apForLearning=%u",
                   exp, (unsigned)exists, (unsigned)hp,
                   (unsigned)learning, (unsigned)apForLearning);
        // Full AP array so we can see the 0/40 etc. distribution vs the screen.
        char aphex[3 * 24 + 4] = {}; int ap = 0;
        for (int i = 0; i < 24; i++) ap += sprintf(aphex + ap, "%02X ", rec[0x24 + i]);
        Log::Menu("[GFDIAG]   GF APs[24]=%s", aphex);
        // complete_abilities[16] (learned-ability flags) for later list work.
        char cahex[3 * 16 + 4] = {}; int ca = 0;
        for (int i = 0; i < 16; i++) ca += sprintf(cahex + ca, "%02X ", rec[0x14 + i]);
        Log::Menu("[GFDIAG]   GF complete_abilities[16]=%s", cahex);

        // Compatibility: read gf_compat[gfIdx] (u16 @ char+0x70 + gfIdx*2) for
        // EVERY existing character slot 0..7 (the detail screen shows the fixed
        // roster Squall/Zell/Quistis/Selphie, not the active party). char+0x70
        // confirmed by summing savemap_ff8_character (total 0x98, exists@+0x94).
        // Goal this build: pair each slot's RAW gf_compat with the on-screen
        // DISPLAYED value (Diablos: Squall648 Zell573 Quistis600 Selphie606) to
        // solve the raw->display formula exactly (2 points were ambiguous; the
        // 4 here disqualify the wrong candidate).
        const int GF_COMPAT_OFF = 0x70;
        for (int slot = 0; slot < CHAR_COUNT; slot++) {
            uint8_t* ch = sm + CHARS_OFFSET + slot * CHAR_STRUCT_SIZE;
            uint8_t  exists = ch[CHR_EXISTS];
            uint8_t  model  = ch[CHR_MODEL_ID];
            uint16_t compat = *(uint16_t*)(ch + GF_COMPAT_OFF + gfIdx * 2);
            const char* nm = (model < 8) ? CHAR_NAMES[model] : "???";
            Log::Menu("[GFDIAG]   compat char[%d] exists=%u model=%u(%s) gf_compat[%d]=%u",
                       slot, (unsigned)exists, (unsigned)model, nm, gfIdx, (unsigned)compat);
        }
        Log::Menu("[GFDIAG] === end detail field probe ===");
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
}

// GCW capture — uses std::string, so NO __try here (mirrors CaptureMenuGcwText).
static void GFDiagCaptureGcw()
{
    static DWORD       s_gfLastGcw = 0;
    static std::string s_gfLastGcwText;
    DWORD now = GetTickCount();
    if (now - s_gfLastGcw < 400) return;
    s_gfLastGcw = now;
    uint8_t gcw[1024];
    int len = FieldDialog::SnapshotGcwBuffer(gcw, sizeof(gcw));
    if (len > 0) {
        std::string dec = FF8TextDecode::DecodeMenuText(gcw, len);
        if (!dec.empty() && dec != s_gfLastGcwText) {
            Log::Menu("[GFDIAG] GCW: \"%s\"", dec.c_str());
            s_gfLastGcwText = dec;
        }
        // Detail/Learn screen field probe. The GCW buffer concatenates several
        // stale render cycles, so "Select ability to learn" is often present even
        // on the detail screen — the old `!onLearn` gate wrongly blocked the
        // probe. Both the detail panel and the Learn list show the same stat
        // block (name + LV + HP/Compatibility), and the probe only READS the
        // savemap, so it's safe to fire on either. Trigger whenever the GF stat
        // panel is up ("Compatibility" present), throttled to once per 2 s so it
        // re-confirms without spamming. s_gfDetailProbed latches the first fire;
        // it re-arms when the panel goes away (back on the grid, no "Compatibility").
        bool hasCompat = (dec.find("Compatibility") != std::string::npos);
        if (hasCompat) {
            DWORD nowp = GetTickCount();
            if (!s_gfDetailProbed || (nowp - s_gfDetailProbeTick) >= 2000) {
                s_gfDetailProbed = true;
                s_gfDetailProbeTick = nowp;
                GFDiagProbeDetail();
            }
        } else {
            s_gfDetailProbed = false;   // back on the grid — re-arm for next entry
        }
    }
}
#endif // GF_DIAG

// --- Production GF-list announce (#41) ---------------------------------------
// +0x253 = canonical GF cell index 0..15 (the hand-cursor position in the GF
// grid; confirmed v0.18.0 — it tracked the HELP name 0=Quezacotl..5=Diablos and
// stays live the whole time the screen is open). +0x1E8==4 marks the GF
// subsystem active (analog of Junction=17 / Save=6). On a cell-index change we
// look up the savemap record at that index and speak its name (rec[+0x11]!=0 =
// obtained); un-obtained cells say "Empty", de-duped so a sweep across a run of
// empty cells doesn't repeat. Reading the name straight from the savemap is
// deterministic — no help-text scraping. Level is omitted for now (engine
// computes it from EXP; not stored, not in the HELP bar).

// SEH-guarded raw read — NO C++ objects in this frame (C2712-safe). Returns
// false unless the GF subsystem is active and the cell index is in range.
static bool GFReadCursor(int* outIdx, uint8_t nameRaw[12], bool* outObtained)
{
    __try {
        uint8_t* ms = (uint8_t*)pMenuStateA;
        if (ms == nullptr)        return false;
        if (ms[0x1E8] != 0x04)    return false;   // GF subsystem not active
        int idx = ms[0x253];
        if (idx >= GF_REC_COUNT)  return false;   // byte read, so idx >= 0
        uint8_t* rec = (uint8_t*)SAVEMAP_BASE + GF_BLOCK_OFFSET + idx * GF_RECORD_SIZE;
        *outObtained = (rec[0x11] != 0);
        for (int i = 0; i < 12; i++) nameRaw[i] = rec[i];
        *outIdx = idx;
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

// std::string here, so NO __try (mirrors GFDiagCaptureGcw). Announces on a
// cell-index change only.
static void AnnounceGFOnCursorChange()
{
    int     idx = -1;
    bool    obtained = false;
    uint8_t nameRaw[12] = {};

    if (!GFReadCursor(&idx, nameRaw, &obtained)) {
        s_gfLastIdx = -1;          // left the screen — re-announce on re-entry
        return;
    }
    if (idx == s_gfLastIdx) return;    // announce only when the cursor moves
    s_gfLastIdx = idx;

    if (obtained) {
        uint8_t adj[12];
        for (int i = 0; i < 12; i++)
            adj[i] = (nameRaw[i] >= 0x20) ? (uint8_t)(nameRaw[i] - 0x20) : nameRaw[i];
        char nm[32] = {};
        DecodeNameToBuffer(adj, 12, nm, sizeof(nm));
        if (nm[0] != '\0') {
            ScreenReader::Speak(nm, true);
            Log::Menu("[MenuTTS] GF[%d]: \"%s\"", idx, nm);
            s_gfLastWasEmpty = false;
        }
    } else if (!s_gfLastWasEmpty) {    // de-dupe a run of empty cells
        ScreenReader::Speak("Empty", true);
        Log::Menu("[MenuTTS] GF[%d]: empty", idx);
        s_gfLastWasEmpty = true;
    }
}

// --- Production GF DETAIL screen number keys (#41) ---------------------------
// Scan-screen model: while the detail panel is up, keys 1..7 each speak one
// field, all from the savemap (deterministic):
//   1 = Name
//   2 = HP (current of max)
//   3 = Level + who currently has this GF junctioned ("equipped by")
//   4 = EXP to next level, then total (Current) EXP
//   5 = Currently learning (ability)
//   6 = Compatibility, first three existing characters
//   7 = Compatibility, next three existing characters
// Keys 6/7 prefix the readout with "Compatibility:" so the numbers have
// context. When the panel first appears, an entry hint ("Press numbers 1
// through 7 for details") is spoken once (Scan-screen model). Q/R cycle the
// displayed GF on the panel; the new GF auto-announces, and the number keys
// follow it, via the displayed-GF index resolved from the GCW header (see
// MatchDetailGFIndex / UpdateGFDetailPhase) — Q/R does NOT move the grid cursor
// +0x253, so reading the GCW header is the only reliable signal.
// Compatibility display = (6000 - raw) / 5 (confirmed against
// Squall648/Zell573/Quistis600/Selphie606). Characters iterate in model order,
// existing-only (Irvine/Rinoa appear once recruited), split 3 + 3.
//
// HP (key 2): the stored HPs u16 (+0x12) equals the displayed MAX HP — confirmed
// across 5 GFs (730/2904/1593/1948/1421, each shown as cur==max while rested).
// For a rested GF current==max, so we report "HP X of X". (A damaged/KO'd GF in
// the menu is rare; the single stored field is the best available either way.)
//
// Level (key 3) + EXP-to-next (key 4): FF8 levels GFs at a FLAT per-level EXP
// cost — level = exp/cost + 1, next = cost - (exp % cost). Confirmed exactly
// from BAT screenshots: Quezacotl L23@11185, Shiva L40@19958, Ifrit L24@11930,
// Diablos L9@4000 (cost 500); Siren L26@10192 (cost 400). The cost is per-GF
// (most 500, some 400, Eden 1000), so GF_EXP_PER_LEVEL holds only EMPIRICALLY
// CONFIRMED costs; an uncalibrated GF (cost 0) omits the level (key 3) and gives
// only total EXP (key 4) — never a guessed level a blind player can't verify.
// Fill in each GF's cost from a detail screenshot as it's obtained.
// "Equipped by" (key 3): each existing character's junctioned-GF bitmask is a
// u16 at char +0x58 (bit = canonical GF id; FFNx savemap_ff8_character.gfs).
static const int GF_EXP_PER_LEVEL[GF_REC_COUNT] = {
    500,  //  0 Quezacotl  (confirmed L23 @ 11185)
    500,  //  1 Shiva      (confirmed L40 @ 19958)
    500,  //  2 Ifrit      (confirmed L24 @ 11930)
    400,  //  3 Siren      (confirmed L26 @ 10192)
    0,    //  4 Brothers   (not yet calibrated)
    500,  //  5 Diablos    (confirmed L9  @ 4000)
    0,    //  6 Carbuncle  (not yet calibrated)
    0,    //  7 Leviathan  (not yet calibrated)
    0,    //  8 Pandemona  (not yet calibrated)
    0,    //  9 Cerberus   (not yet calibrated)
    0,    // 10 Alexander  (not yet calibrated)
    0,    // 11 Doomtrain  (not yet calibrated)
    0,    // 12 Bahamut    (not yet calibrated)
    0,    // 13 Cactuar    (not yet calibrated)
    0,    // 14 Tonberry   (not yet calibrated)
    0,    // 15 Eden       (docs say 1000; confirm when obtained)
};

// --- AP-to-learn data (#44) --------------------------------------------------
// Required AP is a per-ABILITY constant (same cost on every GF that can learn
// it), indexed by unified ability id 0..115. Current AP is per-(GF,slot) in the
// savemap APs[24] (+0x24); to read an ability's current AP we map its id to the
// GF's slot via gf_ability_slots[gf][slot]==id, then read APs[slot]. Source:
// Hyne apsTab/innateAbilities; anchor-validated AND cross-checked against live
// BAT data (Quezacotl learning SumMag+30% id 85 -> slot 2 -> APs[2]=117 toward
// cost 140). See "Plan & Research Documents/GF ability slot and AP threshold
// deep research results.md". NOTE: Auto-Haste (id 73) cost is uncertain (Hyne
// 250 vs FF Wiki 150); it only appears on Cerberus (slot 6) -- verify in-game.
static const unsigned char ability_ap_cost[116] = {
      0,  50,  50,  50,  50,  50, 120, 200, 120, 200, 160, 160,
    100, 100, 130, 180, 130, 180, 150, 200,   1,   1,   1,   1,
      0,  40,  60,  60, 100, 100, 100, 200,  80, 200, 100, 100,
    100, 100,   0,  60, 120, 240,  60, 120, 240,  60, 120, 240,
     60, 120, 240,  60, 120, 240, 150, 200, 150, 200, 200, 200,
    200,   0, 100, 160, 200, 100, 100, 100, 100, 100, 250, 250,
    250, 250, 150, 250, 250,   0, 200,  40,  30, 100, 250,  40,
     70, 140, 200,  40,  70, 140, 200,  10, 150, 150, 150, 200,
    150,  30,  30,  30,  30,  30,  60,  30, 200,  30,  30,  30,
     30, 200,  30,  30,  60,  60, 120,  80
};

// Per-GF learnable abilities in SLOT ORDER (matches savemap APs[] index), value
// = unified ability id. NOT the on-screen display order (the menu sorts/filters
// the visible list); we only use this to map id -> slot for the current-AP read.
static const unsigned char gf_ability_slots[GF_REC_COUNT][22] = {
    {  83, 84, 85, 87, 88, 91, 97,112,  1, 48, 49, 10,  3, 12, 14, 25,115,  4, 23, 20, 21, 22 }, // 0 Quezacotl
    {  83, 84, 85, 87, 88, 23, 91, 98,  3, 45, 46, 51, 52, 12, 14,  2, 10, 26,  5, 20, 21, 22 }, // 1 Shiva
    {  83, 84, 85, 87, 88, 89, 91, 99,107, 42, 43, 10, 66,  1, 23, 12, 14, 27,  2, 20, 21, 22 }, // 2 Ifrit
    {  87, 88,108, 83, 84, 85, 91,100,106, 48, 49, 11, 68, 23, 13, 16, 28, 79,  4, 20, 21, 22 }, // 3 Siren
    {  83, 84, 85, 87, 88, 89, 91, 39, 40, 41, 65,  2, 10,  5, 12, 23, 62, 29,  1, 20, 21, 22 }, // 4 Brothers
    {  87, 88, 89,101,102,  1, 39, 40, 41,  4, 48, 49, 23,  8, 80, 81, 30, 58, 18, 20, 21, 22 }, // 5 Diablos
    {  87, 88, 89,105, 45, 46, 67,  1, 39, 40,  4, 11, 13, 16, 60, 72, 23, 18,  3, 20, 21, 22 }, // 6 Carbuncle
    {  87, 88, 89, 83, 84, 85, 91,103,110, 51, 52, 69,  5, 14,  4, 10, 74, 31, 23, 20, 21, 22 }, // 7 Leviathan
    {  83, 84, 85, 87, 88, 89, 91,  6, 54, 55, 42, 43,  2, 12, 14, 10, 63, 32, 23, 20, 21, 22 }, // 8 Pandemona
    {  87, 88, 89,  6, 54, 55, 73,  5, 13, 16, 17, 11, 18,  2, 78,  4, 75,  8, 23, 20, 21, 22 }, // 9 Cerberus
    {  87, 88, 89, 83, 84, 85, 91, 59,114, 51, 52,113, 18, 10, 14, 15,  5, 33, 23, 20, 21, 22 }, // 10 Alexander
    {  83, 84, 85, 86, 87, 88, 89, 90, 91, 71, 32, 30, 96,109, 15, 17, 23, 10, 11, 20, 21, 22 }, // 11 Doomtrain
    {  83, 84, 85, 86, 87, 88, 89, 90, 91, 58, 75, 70, 23, 82, 64, 44, 50,104, 19, 20, 21, 22 }, // 12 Bahamut
    {  87, 88, 89, 23,  7, 56, 75,  9, 57, 29, 74, 63, 65, 66, 67, 68, 69, 64, 36, 20, 21, 22 }, // 13 Cactuar
    {  83, 84, 85, 87, 88, 89, 91, 74, 64, 63, 57, 23, 56, 92, 93, 94, 95, 34, 35, 20, 21, 22 }, // 14 Tonberry
    {  83, 84, 85, 86, 87, 88, 89, 90, 91,111, 30, 27,  8,  6,  7, 57, 76, 37, 23, 20, 21, 22 }, // 15 Eden
};

// Required AP to learn ability `id` (0 if out of range / no cost).
static int GFAbilityApCost(uint8_t id) { return (id < 116) ? (int)ability_ap_cost[id] : 0; }

// Current AP accumulated toward ability `id` on GF `gfIdx`: map id -> slot via
// gf_ability_slots, then read the savemap APs[+0x24 + slot] byte. Returns true
// and sets *cur on success; false if the id isn't one of this GF's slots.
// Scalars only inside the __try (C2712-safe).
static bool GFReadAbilityAP(int gfIdx, uint8_t id, int* cur)
{
    if (gfIdx < 0 || gfIdx >= GF_REC_COUNT) return false;
    int slot = -1;
    for (int s = 0; s < 22; s++) if (gf_ability_slots[gfIdx][s] == id) { slot = s; break; }
    if (slot < 0) return false;
    __try {
        uint8_t* rec = (uint8_t*)SAVEMAP_BASE + GF_BLOCK_OFFSET + gfIdx * GF_RECORD_SIZE;
        *cur = (int)rec[0x24 + slot];
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}

// Resolve which GF the detail panel is CURRENTLY showing. Q/R cycle the
// displayed GF but do NOT move the grid cursor (+0x253), so we read the panel
// header out of the rendered GCW instead: it always contains
// "<GFName>LVHP/Compatibility...". We match each obtained GF's savemap name
// (same +0x20 decode as the grid) against the GCW and return the right-most
// "<name>LV" hit — the right-most copy is the freshest render, so this tracks
// Q/R. Returns -1 if no GF header is present (caller keeps the last value).
// std::string here, so NO __try (mirrors the other GCW helpers); raw savemap
// reads are safe while the menu is up (same precedent as GFDiagDumpBlock).
static int MatchDetailGFIndex(const std::string& dec)
{
    uint8_t* base = (uint8_t*)SAVEMAP_BASE + GF_BLOCK_OFFSET;
    int    bestIdx = -1;
    size_t bestPos = std::string::npos;
    for (int g = 0; g < GF_REC_COUNT; g++) {
        uint8_t* rec = base + g * GF_RECORD_SIZE;
        if (rec[0x11] == 0) continue;          // GF not obtained
        uint8_t adj[12];
        for (int i = 0; i < 12; i++)
            adj[i] = (rec[i] >= 0x20) ? (uint8_t)(rec[i] - 0x20) : rec[i];
        char nm[32] = {};
        DecodeNameToBuffer(adj, 12, nm, sizeof(nm));
        if (nm[0] == '\0') continue;
        size_t pos = dec.rfind(std::string(nm) + "LV");   // "<name>LVHP" header
        if (pos != std::string::npos &&
            (bestPos == std::string::npos || pos > bestPos)) {
            bestPos = pos;
            bestIdx = g;
        }
    }
    return bestIdx;
}

// Decode GF[idx]'s savemap name into out (same +0x20 decode as the grid).
static void DecodeGFName(int idx, char* out, int outSize)
{
    if (outSize > 0) out[0] = '\0';
    if (idx < 0 || idx >= GF_REC_COUNT) return;
    uint8_t* rec = (uint8_t*)SAVEMAP_BASE + GF_BLOCK_OFFSET + idx * GF_RECORD_SIZE;
    uint8_t adj[12];
    for (int i = 0; i < 12; i++)
        adj[i] = (rec[i] >= 0x20) ? (uint8_t)(rec[i] - 0x20) : rec[i];
    DecodeNameToBuffer(adj, 12, out, outSize);
}

// Phase detector (std::string -> no __try). Sets s_gfDetailActive when the GF
// stat panel (detail or Learn) is showing (GCW "Compatibility" label), resolves
// the DISPLAYED GF (s_gfDetailIdx) from the GCW header, fires the one-time entry
// hint, and auto-announces the GF when Q/R changes it. Throttled; on an empty
// GCW snapshot it keeps the last known state.
static void UpdateGFDetailPhase()
{
    DWORD now = GetTickCount();
    if (now - s_gfPhasePoll < 150) return;
    s_gfPhasePoll = now;
    uint8_t gcw[1024];
    int len = FieldDialog::SnapshotGcwBuffer(gcw, sizeof(gcw));
    if (len <= 0) return;
    std::string dec = FF8TextDecode::DecodeMenuText(gcw, len);
    if (dec.empty()) return;
    bool active = (dec.find("Compatibility") != std::string::npos);

    if (active) {
        int idx = MatchDetailGFIndex(dec);
        if (idx >= 0) s_gfDetailIdx = idx;     // sticky: keep last on a miss

        if (!s_gfDetailWasActive) {
            // Panel just appeared: speak the number-key hint once (Scan model).
            // 2 s cooldown guards GCW flicker; interrupt=false so it never clips
            // the GF name the grid just announced.
            if (now - s_gfDetailHintTick > 2000) {
                s_gfDetailHintTick = now;
                ScreenReader::Speak("Press numbers 1 through 7 for details.", false);
            }
        }
        // Announce the displayed GF when it changes. lastIdx < 0 means "not yet
        // primed" (covers the panel appearing a poll or two before the header
        // resolves) — prime silently so we never announce the GF we entered on.
        if (s_gfDetailIdx >= 0) {
            if (s_gfDetailLastIdx < 0) {
                s_gfDetailLastIdx = s_gfDetailIdx;          // prime, no announce
            } else if (s_gfDetailIdx != s_gfDetailLastIdx) {
                s_gfDetailLastIdx = s_gfDetailIdx;
                char nm[32] = {};
                DecodeGFName(s_gfDetailIdx, nm, sizeof(nm));
                if (nm[0]) {
                    ScreenReader::Speak(nm, true);
                    Log::Menu("[MenuTTS] GF detail switched to: \"%s\"", nm);
                }
            }
        }
    } else if (s_gfDetailWasActive) {
        s_gfDetailIdx     = -1;     // left the panel — reset
        s_gfDetailLastIdx = -1;
    }
    s_gfDetailWasActive = active;
    s_gfDetailActive    = active;
}

// Speak one detail field. Char[]/sprintf/Speak/GetAbilityName/DecodeNameToBuffer
// only (no std::string), so the raw reads and formatting share one __try frame
// (same pattern as AnnounceJuncCharSelect). C2712-safe.
static void SpeakGFDetailField(int key)
{
    __try {
        uint8_t* sm = (uint8_t*)SAVEMAP_BASE;
        uint8_t* ms = (uint8_t*)pMenuStateA;
        // On the detail panel, read the DISPLAYED GF (resolved from the GCW
        // header) rather than the grid cursor +0x253 — Q/R cycle the displayed
        // GF WITHOUT moving +0x253, so the keys must follow s_gfDetailIdx or
        // they'd report the GF the player entered on.
        int gfIdx = (s_gfDetailIdx >= 0) ? s_gfDetailIdx : ms[0x253];
        if (gfIdx < 0 || gfIdx >= GF_REC_COUNT) return;
        uint8_t* rec = sm + GF_BLOCK_OFFSET + gfIdx * GF_RECORD_SIZE;
        char buf[256] = {};

        switch (key) {
        case 1: {   // Name
            uint8_t adj[12];
            for (int i = 0; i < 12; i++)
                adj[i] = (rec[i] >= 0x20) ? (uint8_t)(rec[i] - 0x20) : rec[i];
            char nm[32] = {};
            DecodeNameToBuffer(adj, 12, nm, sizeof(nm));
            snprintf(buf, sizeof(buf), "%s", nm[0] ? nm : "Unknown");
            break;
        }
        case 2: {   // HP, current of max. Stored HPs (+0x12) == displayed max
                    // for a rested GF (confirmed across 5 GFs).
            uint16_t hp = *(uint16_t*)(rec + 0x12);
            snprintf(buf, sizeof(buf), "HP %u of %u", (unsigned)hp, (unsigned)hp);
            break;
        }
        case 3: {   // Level + who currently has this GF junctioned. Level from
                    // the flat per-level cost (uncalibrated GF omits it). The
                    // "equipped by" scan reads each existing character's
                    // junctioned-GF bitmask (u16 @ char +0x58, bit = GF id).
            uint32_t exp = *(uint32_t*)(rec + 0x0C);
            int cost = GF_EXP_PER_LEVEL[gfIdx];
            int pos = 0;
            if (cost > 0) {
                int level = (int)(exp / (uint32_t)cost) + 1;
                if (level > 100) level = 100;
                pos += snprintf(buf + pos, sizeof(buf) - pos, "Level %d, ", level);
            }
            int emitted = 0;
            for (int slot = 0; slot < CHAR_COUNT; slot++) {
                uint8_t* ch = sm + CHARS_OFFSET + slot * CHAR_STRUCT_SIZE;
                if (ch[CHR_EXISTS] == 0) continue;
                uint16_t gfs = *(uint16_t*)(ch + 0x58);
                if (((gfs >> gfIdx) & 1) == 0) continue;
                uint8_t model = ch[CHR_MODEL_ID];
                const char* cn = (model < 8) ? CHAR_NAMES[model] : "Unknown";
                pos += snprintf(buf + pos, sizeof(buf) - pos, "%s%s",
                                emitted ? ", " : "equipped by ", cn);
                emitted++;
            }
            if (emitted == 0)
                snprintf(buf + pos, sizeof(buf) - pos, "not equipped");
            break;
        }
        case 4: {   // EXP to next level first (more useful), then total EXP.
                    // Uncalibrated GF (cost 0) or max level: total EXP only.
            uint32_t exp = *(uint32_t*)(rec + 0x0C);
            int cost = GF_EXP_PER_LEVEL[gfIdx];
            int level = (cost > 0) ? ((int)(exp / (uint32_t)cost) + 1) : 0;
            if (cost > 0 && level < 100) {
                int next = cost - (int)(exp % (uint32_t)cost);
                snprintf(buf, sizeof(buf),
                         "EXP to next level %d, Current EXP %u",
                         next, (unsigned)exp);
            } else {
                snprintf(buf, sizeof(buf), "Current EXP %u", (unsigned)exp);
            }
            break;
        }
        case 5: {   // Currently learning ability + AP progress (current of
                    // required) from the #44 AP tables. learning id = rec[0x40];
                    // current = APs[slot-of-id], required = ability_ap_cost[id].
            uint8_t learn = rec[0x40];
            const char* an = GetAbilityName(learn);
            if (learn == 0 || an == nullptr) {
                snprintf(buf, sizeof(buf), "Learning nothing");
            } else {
                int cur = 0, req = GFAbilityApCost(learn);
                if (GFReadAbilityAP(gfIdx, learn, &cur) && req > 0) {
                    if (cur > req) cur = req;
                    snprintf(buf, sizeof(buf), "Learning %s, %d of %d AP", an, cur, req);
                } else {
                    snprintf(buf, sizeof(buf), "Learning %s", an);
                }
            }
            break;
        }
        case 6:     // Compatibility, first three existing characters
        case 7: {   // Compatibility, next three existing characters
            int start = (key == 6) ? 0 : 3;
            int seen = 0, emitted = 0, pos = 0;
            pos += snprintf(buf + pos, sizeof(buf) - pos, "Compatibility: ");
            for (int slot = 0; slot < CHAR_COUNT && emitted < 3; slot++) {
                uint8_t* ch = sm + CHARS_OFFSET + slot * CHAR_STRUCT_SIZE;
                if (ch[CHR_EXISTS] == 0) continue;     // not yet recruited
                if (seen++ < start) continue;          // skip the first group
                uint16_t raw = *(uint16_t*)(ch + 0x70 + gfIdx * 2);
                int disp = (6000 - (int)raw) / 5;
                if (disp < 0) disp = 0;
                uint8_t model = ch[CHR_MODEL_ID];
                const char* cn = (model < 8) ? CHAR_NAMES[model] : "Unknown";
                pos += snprintf(buf + pos, sizeof(buf) - pos, "%s%s %d",
                                emitted ? ", " : "", cn, disp);
                emitted++;
            }
            if (emitted == 0)
                snprintf(buf, sizeof(buf), "No more characters");
            break;
        }
        default: return;
        }

        if (buf[0]) {
            ScreenReader::Speak(buf, true);
            Log::Menu("[MenuTTS] GF detail key %d: \"%s\"", key, buf);
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        Log::Menu("[MenuTTS] Exception in SpeakGFDetailField");
    }
}

// --- Production GF LEARN-LIST (ability-to-learn) announce (#41, v1) ----------
// On the Learn list the player arrows through the GF's learnable abilities to
// pick the next one to study. We announce the highlighted row on each move:
// "<name>, <status>. <description>". The row index is pMenuStateA+0x258; the
// row's NAME comes from the rendered GCW (the displayed list, which is a
// filtered subset of the kernel slots — reading the screen avoids replicating
// the engine's filter); status (learned / now learning) comes from the savemap;
// the description is the GCW help text. AP progress is v2 (needs #44's table).

// SEH raw read of both Learn-list cursor-candidate bytes (+0x257 page 1,
// +0x258 page 2). No C++ objects (C2712-safe). Returns false on fault.
static bool GFReadLearnCursors(int* out257, int* out258)
{
    __try {
        uint8_t* ms = (uint8_t*)pMenuStateA;
        if (ms == nullptr) return false;
        *out257 = (int)ms[0x257];
        *out258 = (int)ms[0x258];
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}

#if GF_DIAG
// SEH raw read of a 16-byte window at pMenuStateA+0x250 (covers +0x253 grid,
// +0x257/+0x258 learn cursors, neighbours) for the confirmation-pass log.
static bool GFReadCursorWindow(uint8_t out[16])
{
    __try {
        uint8_t* ms = (uint8_t*)pMenuStateA;
        if (ms == nullptr) return false;
        for (int i = 0; i < 16; i++) out[i] = ms[0x250 + i];
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}
#endif

// SEH raw read of a row's learned/learning status from the displayed GF's
// savemap record: completeAbilities[16] bitmap (+0x14, bit = unified ability id)
// = learned; rec[+0x40] (the ability id being learned) == id = now learning.
// Scalars only (C2712-safe).
static void GFReadLearnStatus(int gfIdx, uint8_t abilityId,
                              bool* outLearned, bool* outLearning)
{
    *outLearned = false;
    *outLearning = false;
    __try {
        if (gfIdx < 0 || gfIdx >= GF_REC_COUNT) return;
        uint8_t* rec = (uint8_t*)SAVEMAP_BASE + GF_BLOCK_OFFSET + gfIdx * GF_RECORD_SIZE;
        if (abilityId < 128) {
            uint8_t byte = rec[0x14 + (abilityId >> 3)];
            *outLearned = ((byte >> (abilityId & 7)) & 1) != 0;
        }
        *outLearning = (rec[0x40] == abilityId);
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
}

#if GF_AP_DIAG
// SEH raw read of the displayed GF's per-slot AP array APs[24] (+0x24) plus the
// two learning-byte candidates (+0x40 = unified ability id, empirically; +0x41 =
// the slot-index the community docs cite). Scalars/char only (C2712-safe).
// Diagnostic-only (#44): correlates on-screen AP with savemap to decide the
// AP-readout source.
static bool GFReadApArray(int gfIdx, uint8_t out[24], uint8_t* learn40, uint8_t* learn41)
{
    *learn40 = 0; *learn41 = 0;
    __try {
        if (gfIdx < 0 || gfIdx >= GF_REC_COUNT) return false;
        uint8_t* rec = (uint8_t*)SAVEMAP_BASE + GF_BLOCK_OFFSET + gfIdx * GF_RECORD_SIZE;
        for (int i = 0; i < 24; i++) out[i] = rec[0x24 + i];
        *learn40 = rec[0x40];
        *learn41 = rec[0x41];
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}
#endif

// Convert a TTS ability name (ABILITY_NAMES form, e.g. "SumMag plus 10%") into
// the on-screen GCW form ("SumMag+10%"): the menu renders "+" where the TTS
// table spells " plus ", and drops the space in " x2"/" x3"/" x4" multipliers
// ("Elem-Def-J x2" -> "Elem-Def-Jx2", "Expend x2-1" -> "Expendx2-1"). std::string
// here, so this and its callers stay OUT of any __try frame.
static std::string NormalizeAbilityToGcw(const char* tts)
{
    std::string s(tts ? tts : "");
    size_t p;
    while ((p = s.find(" plus ")) != std::string::npos) s.replace(p, 6, "+");
    for (size_t i = 0; i + 2 < s.size(); ) {
        if (s[i] == ' ' && s[i + 1] == 'x' && s[i + 2] >= '0' && s[i + 2] <= '9')
            s.erase(i, 1);
        else
            i++;
    }
    return s;
}

// Lazily-built table of GCW-form ability names, parallel to ABILITY_NAMES.
static const std::vector<std::string>& GcwAbilityNames()
{
    static std::vector<std::string> v;
    if (v.empty()) {
        v.reserve(ABILITY_NAME_COUNT);
        for (int i = 0; i < ABILITY_NAME_COUNT; i++)
            v.push_back(NormalizeAbilityToGcw(ABILITY_NAMES[i]));
    }
    return v;
}

// Parse the displayed Learn list out of the decoded GCW. The list is the run of
// concatenated ability names ending right before the "<GFName>LV" stat-panel
// header; we walk it RIGHT-TO-LEFT, longest-match against the GCW-form names.
// Items concatenate with NO separating space, while the preceding help text is
// space-separated words — so we stop as soon as a candidate match would be
// immediately preceded by a space. That cleanly cuts the list off from the help
// text even when the help ends in an ability word (e.g. Boost's help "Boost GF":
// the trailing "GF" is space-preceded and rejected). Returns the row count and
// fills outIds in display order; *outListStart = decoded index where row 0
// begins (used to slice out the help description). std::string -> no __try.
static int ParseLearnList(const std::string& dec, const std::string& gfName,
                          uint8_t outIds[], int maxOut, size_t* outListStart)
{
    *outListStart = std::string::npos;
    size_t end = dec.rfind(gfName + "LV");        // freshest stat-panel header
    if (end == std::string::npos) return 0;

    const std::vector<std::string>& names = GcwAbilityNames();
    uint8_t tmp[64];
    int n = 0;
    size_t p = end;
    while (p > 0 && n < 64) {
        int    bestId  = -1;
        size_t bestLen = 0;
        for (int id = 0; id < ABILITY_NAME_COUNT; id++) {
            if (id == 0 || id == 24) continue;          // "None"/"Empty" placeholders
            const std::string& nm = names[id];
            size_t len = nm.size();
            if (len == 0 || len > p || len <= bestLen) continue;
            if (dec.compare(p - len, len, nm) != 0) continue;
            if (p - len > 0 && dec[p - len - 1] == ' ') continue;   // space-reject
            bestId  = id;
            bestLen = len;
        }
        if (bestId < 0) break;
        tmp[n++] = (uint8_t)bestId;
        p -= bestLen;
        *outListStart = p;
    }
    int count = (n < maxOut) ? n : maxOut;
    for (int i = 0; i < count; i++) outIds[i] = tmp[n - 1 - i];   // reverse -> display order
    return count;
}

// Phase handler: while the detail panel is up, detect the Learn list (a
// non-empty parsed row list) and announce the highlighted row on cursor change.
// std::string -> no __try (the raw reads are isolated in the SEH helpers above).
static void UpdateGFLearnPhase()
{
    DWORD now = GetTickCount();
    if (now - s_gfLearnPoll < 100) return;
    s_gfLearnPoll = now;

    uint8_t gcw[1024];
    int len = FieldDialog::SnapshotGcwBuffer(gcw, sizeof(gcw));
    if (len <= 0) return;
    std::string dec = FF8TextDecode::DecodeMenuText(gcw, len);
    if (dec.empty()) return;

    if (s_gfDetailIdx < 0) return;
    char gfn[32] = {};
    DecodeGFName(s_gfDetailIdx, gfn, sizeof(gfn));
    if (!gfn[0]) return;

    uint8_t ids[64];
    size_t  listStart = std::string::npos;
    int count = ParseLearnList(dec, std::string(gfn), ids, 64, &listStart);
    if (count <= 0) {                 // detail panel only — not on the Learn list
        s_gfLearnLastId  = -1;        // re-arm so re-entry re-announces
        s_gfLearnPrev257 = -1;
        s_gfLearnPrev258 = -1;
        s_gfLearnSelValid = false;   // (#3) off the list -> "/" uses the normal help bar
        return;
    }

    // Help description = GCW text between the menu-bar "Save" and row 0. Best
    // effort; omitted if it looks wrong (empty, too long, or the generic prompt).
    std::string desc;
    if (listStart != std::string::npos && listStart > 0) {
        size_t sp = dec.rfind("Save", listStart);
        if (sp != std::string::npos) {
            size_t ds = sp + 4;
            if (listStart > ds) desc = dec.substr(ds, listStart - ds);
        }
    }
    if (desc == "Select ability to learn" || desc.size() > 64) desc.clear();

    int cur257 = -1, cur258 = -1;
    if (!GFReadLearnCursors(&cur257, &cur258)) return;
    bool ch257 = (cur257 != s_gfLearnPrev257);
    bool ch258 = (cur258 != s_gfLearnPrev258);

#if GF_DIAG
    // Confirmation-pass window log: fire whenever the +0x250 window changes, so
    // one slow pass through BOTH pages pins the exact per-page cursor byte.
    {
        static uint8_t s_win[16];
        static bool    s_winValid = false;
        uint8_t win[16];
        if (GFReadCursorWindow(win)) {
            bool wch = !s_winValid;
            for (int i = 0; i < 16; i++) if (win[i] != s_win[i]) wch = true;
            if (wch) {
                memcpy(s_win, win, 16);
                s_winValid = true;
                char hex[64]; int hp = 0;
                for (int i = 0; i < 16; i++) hp += sprintf(hex + hp, "%02X ", win[i]);
                char lst[256]; int lp = 0;
                for (int i = 0; i < count && lp < 240; i++)
                    lp += snprintf(lst + lp, sizeof(lst) - lp, "%u%s",
                                   (unsigned)ids[i], (i + 1 < count) ? "," : "");
                Log::Menu("[GFLEARN] win+0x250=%s| 257=%d 258=%d count=%d ids=[%s] help=\"%s\"",
                           hex, cur257, cur258, count, lst, desc.c_str());
            }
        }
    }
#endif

#if GF_AP_DIAG
    // (#44) AP-readout feasibility probe. On each row move, dump the parsed list,
    // the displayed GF's savemap AP array + raw learning bytes, the raw GCW hex
    // (64 bytes/line), and the full decoded GCW (200 chars/line). If the AP
    // numbers appear in the decoded text we can scrape them; if they're absent
    // they're sprite-drawn and we fall back to the kernel / research AP table.
    if (ch257 || ch258 || s_gfLearnLastId < 0) {
        Log::Menu("[GFAP] --- gf=%d count=%d cur257=%d cur258=%d ---",
                  s_gfDetailIdx, count, cur257, cur258);
        { char lst[256]; int lp = 0;
          for (int i = 0; i < count && lp < 240; i++)
              lp += snprintf(lst + lp, sizeof(lst) - lp, "%u%s",
                             (unsigned)ids[i], (i + 1 < count) ? "," : "");
          Log::Menu("[GFAP] ids=[%s]", lst); }
        { uint8_t aps[24]; uint8_t l40 = 0, l41 = 0;
          if (GFReadApArray(s_gfDetailIdx, aps, &l40, &l41)) {
              char ah[3 * 24 + 8]; int ap = 0;
              for (int i = 0; i < 24; i++) ap += sprintf(ah + ap, "%02X ", aps[i]);
              Log::Menu("[GFAP] APs[24]=%s | learn+0x40=0x%02X +0x41=0x%02X",
                        ah, (unsigned)l40, (unsigned)l41);
          } }
        for (int base = 0; base < len; base += 64) {
            char hx[3 * 64 + 8]; int hp = 0;
            for (int i = base; i < base + 64 && i < len; i++)
                hp += sprintf(hx + hp, "%02X ", gcw[i]);
            Log::Menu("[GFAP] raw+%d: %s", base, hx);
        }
        for (size_t off = 0; off < dec.size(); off += 200)
            Log::Menu("[GFAP] gcw+%u: \"%.200s\"", (unsigned)off, dec.c_str() + off);
    }
#endif

    s_gfLearnPrev257 = cur257;
    s_gfLearnPrev258 = cur258;

    // Pick the active cursor and classify the row. Page 1 navigates via +0x257,
    // page 2 via +0x258 (whichever just changed is the live cursor). A value in
    // [0,count) is a real ability row; a value in [count,22) with blank help is
    // an EMPTY slot in the padded list area below the abilities (confirmed: Quez
    // page 2 = 4 rows but +0x258 ran 4..10 over the empty rows, all help="").
    // Dedupe so each distinct row speaks once: real -> id (0..115), empty -> 200+row.
    int idx = -1;        // real-ability row
    int emptyRow = -1;   // empty-slot row
    if      (ch257 && cur257 >= 0 && cur257 < count)  idx = cur257;
    else if (ch258 && cur258 >= 0 && cur258 < count)  idx = cur258;
    else if (ch257 && cur257 >= count && cur257 < 22) emptyRow = cur257;
    else if (ch258 && cur258 >= count && cur258 < 22) emptyRow = cur258;
    else if (s_gfLearnLastId < 0) {                   // fresh entry: nothing moved yet
        if (cur257 >= 0 && cur257 < count)      idx = cur257;
        else if (cur258 >= 0 && cur258 < count) idx = cur258;
    }

    if (idx >= 0) {
        uint8_t id = ids[idx];
        if ((int)id == s_gfLearnLastId) return;       // dedupe on resolved ability id
        s_gfLearnLastId = id;

        const char* nm = GetAbilityName(id);
        bool learned = false, learning = false;
        GFReadLearnStatus(s_gfDetailIdx, id, &learned, &learning);

        // (#44, v0.18.0.15) Streamlined row readout: name + AP only -- NO help
        // description on cursor move (the "/" key reads that on demand, to cut
        // repeats). Learned -> ", Learned"; otherwise always ", C out of R AP"
        // (one consistent format, even at 0 AP). C = savemap APs[slot-of-id]
        // (0 if untouched); R = ability_ap_cost[id]. The actively-learning
        // ability is held out of the displayed list, so its progress is read by
        // detail key 5, not here.
        char apbuf[64]; apbuf[0] = '\0';
        int req = GFAbilityApCost(id);
        if (learned) {
            snprintf(apbuf, sizeof(apbuf), ", Learned");
        } else if (req > 0) {
            int cur = 0;
            GFReadAbilityAP(s_gfDetailIdx, id, &cur);   // cur stays 0 on miss
            if (cur > req) cur = req;
            snprintf(apbuf, sizeof(apbuf), ", %d out of %d AP", cur, req);
        }
        (void)learning;   // status now conveyed by the AP progress itself

        char out[256];
        snprintf(out, sizeof(out), "%s%s", nm ? nm : "Unknown", apbuf);
        // (#3) Stash the row under the cursor for the on-demand "/" help re-read,
        // mirroring exactly what we announce here (name + help description).
        s_gfLearnSelValid = true;
        s_gfLearnSelId    = (int)id;
        snprintf(s_gfLearnSelName, sizeof(s_gfLearnSelName), "%s", nm ? nm : "Unknown");
        snprintf(s_gfLearnSelDesc, sizeof(s_gfLearnSelDesc), "%s", desc.c_str());
        ScreenReader::Speak(out, true);
        Log::Menu("[MenuTTS] GF learn row %d/%d: id=%u \"%s\"", idx, count, (unsigned)id, out);
    }
    else if (emptyRow >= 0 && desc.empty()) {         // empty slot (blank help confirms)
        int key = 200 + emptyRow;
        if (key == s_gfLearnLastId) return;           // dedupe per empty row
        s_gfLearnLastId = key;
        // (#3) Stash the empty slot for the "/" help re-read.
        s_gfLearnSelValid   = true;
        s_gfLearnSelId      = -1;
        snprintf(s_gfLearnSelName, sizeof(s_gfLearnSelName), "Empty Ability Slot");
        s_gfLearnSelDesc[0] = '\0';
        ScreenReader::Speak("Empty Ability Slot", true);
        Log::Menu("[MenuTTS] GF learn row %d/%d: empty slot", emptyRow, count);
    }
}

// (#3, v0.18.0.13; readout streamlined v0.18.0.15) On-demand re-read of the
// ability currently UNDER the Learn-list cursor. Bound to the "/" key in
// MenuTTS::Update(); deliberately distinct from detail key 5 (which reads the
// ability being LEARNED). Speaks JUST the help description captured as the
// cursor last moved (no name repeat -- the row announce already gave the name;
// falls back to the row name / "Empty Ability Slot" only when no description is
// available). Returns true
// when it spoke (Learn list up with a stored row) so the caller falls back to
// the normal help-bar reader on every other screen. Char[]/snprintf/Speak only.
static bool GFSpeakSelectedAbilityHelp()
{
    if (!s_gfDetailActive || !s_gfLearnSelValid) return false;
    char out[256];
    if (s_gfLearnSelDesc[0])
        snprintf(out, sizeof(out), "%s", s_gfLearnSelDesc);   // help text only, no name repeat
    else
        snprintf(out, sizeof(out), "%s", s_gfLearnSelName);   // fallback (e.g. empty slot)
    ScreenReader::Speak(out, true);
    Log::Menu("[MenuTTS] GF learn help (/): \"%s\"", out);
    return true;
}

// Dispatched from MenuTTS::Update() while mode==6 and top-level cursor == 4.
static void PollGFSubmenu()
{
    if (!s_gfActive) {
        s_gfActive    = true;
        s_gfDumped    = false;
        s_gfSnapValid = false;
        s_gfLastPoll  = 0;
        Log::Menu("[GFDIAG] entered GF screen (top-level cursor index 4)");
    }

#if GF_DIAG
    if (!s_gfDumped) {
        s_gfDumped = true;
        GFDiagDumpBlock();
    }
    DWORD now = GetTickCount();
    if (now - s_gfLastPoll >= 150) {
        s_gfLastPoll = now;
        GFDiagPollBand();
    }
    GFDiagCaptureGcw();
#endif

    // Production (#41): detail-phase first — sets s_gfDetailActive, resolves the
    // displayed GF (s_gfDetailIdx) from the GCW header, fires the entry hint, and
    // auto-announces the GF on a Q/R change. The grid name announce runs only
    // OFF the detail panel, so the two paths never double-announce.
    UpdateGFDetailPhase();
    if (!s_gfDetailActive)
        AnnounceGFOnCursorChange();

    // Production (#41): GF detail-screen number keys (1..7), gated to the panel.
    if (s_gfDetailActive) {
        // Learn-list (ability-to-learn) row announce, v1. Detects the Learn
        // list from the rendered GCW and speaks the highlighted row on a
        // +0x258 cursor change; no-op on the plain detail panel.
        UpdateGFLearnPhase();
        for (int k = 1; k <= 7; k++) {
            if (GetAsyncKeyState('0' + k) & 1)
                SpeakGFDetailField(k);
        }
    }
}
