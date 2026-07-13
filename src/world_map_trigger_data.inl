// world_map_trigger_data.inl - Static trigger-program data + logger
//
// PART OF world_map.cpp -- TEXTUAL INCLUDE. Do NOT compile standalone.
//
// Holds the 38 decoded field-entry trigger programs from wmsetus.obj
// Section 8. The data was originally hex-dumped at v0.14.92, decoded by
// Python (decode_wmsetus_section8.py) for v0.14.93, and embedded here as
// a static C++ array. Each program describes one possible field-entry
// destination keyed by location ID, with an optional top-level vehicle/
// story gate plus one or more clauses (each carrying its own vehicle
// predicate, region operand, story window, and unknown-flag bits).
//
// Layout (sized by trigger_data.inl):
//   - TRIG_VEH_*  / TRIG_UNK_*  scalar constants
//   - TriggerClause / TriggerProgram structs
//   - s_cl00 ... s_cl37 (38 individual clause arrays)
//   - s_triggerPrograms[] table -- the entry point read by AD's planner
//   - TRIGGER_PROGRAM_COUNT
//   - LogTriggerPrograms (init-time diagnostic dump)
//
// NOTE: s_triggerPrograms[].top_story_gte for entry 9 was changed from
// 290 to 0 in v0.14.98 (Balamb Town planner-decline fix). The original
// disassembler had put 0xFF02 0x0122 in the wrong scope; corrected here.

static const uint16_t TRIG_VEH_ANY        = 0x0000;
static const uint16_t TRIG_VEH_FOOT       = 0x0080;
static const uint16_t TRIG_VEH_FOOT_ALT   = 0x0084;
static const uint16_t TRIG_VEH_GARDEN     = 0x0030;
static const uint16_t TRIG_VEH_CHOCOBO    = 0x0031;
static const uint16_t TRIG_VEH_RAGNAROK   = 0x0032;

static const uint16_t TRIG_UNK_0F = 0x0001;
static const uint16_t TRIG_UNK_10 = 0x0002;
static const uint16_t TRIG_UNK_11 = 0x0004;
static const uint16_t TRIG_UNK_12 = 0x0008;
static const uint16_t TRIG_UNK_13 = 0x0010;
static const uint16_t TRIG_UNK_20 = 0x0020;
static const uint16_t TRIG_UNK_21 = 0x0040;

struct TriggerClause {
    uint16_t vehicle;     // TRIG_VEH_* constant; 0 = any (used when top-level vehicle is set)
    uint16_t region;      // segment region-byte to match against Section 2
    uint16_t story_gte;   // savemap-word lower bound (0 = none)
    uint16_t story_lt;    // savemap-word upper bound (0 = +inf)
    uint16_t unk_flags;   // bitmask of UNK opcodes present in original clause (TRIG_UNK_*)
};

struct TriggerProgram {
    uint16_t loc_id;          // wmField/location ID from 0xFF06
    uint16_t top_story_gte;   // top-level story gate lower bound (0 = none)
    uint16_t top_story_lt;    // top-level story gate upper bound (0 = +inf)
    uint16_t top_vehicle;     // top-level vehicle restriction (TRIG_VEH_*; 0 = none)
    uint8_t  num_clauses;     // count of entries in clauses[]
    const TriggerClause* clauses;
};

// 38 per-program clause arrays. The array index matches s_triggerPrograms[]
// index for cross-reference to the decoded artifact (which uses the same
// idx column). s_clNN means "clauses for program NN." When num_clauses == 0
// (program 18, the late-game Chocobo-only mobile destination with top-level
// vehicle restriction and no inner-clause requirements), the pointer is
// nullptr and there is no s_cl18 array.
static const TriggerClause s_cl00[] = {
    { TRIG_VEH_FOOT,    0x14, 0,    0,    0 },
    { TRIG_VEH_CHOCOBO, 0x14, 0,    0,    0 },
};
static const TriggerClause s_cl01[] = {
    { TRIG_VEH_FOOT,    0x21, 0,    0,    0 },
    { TRIG_VEH_CHOCOBO, 0x21, 0,    0,    0 },
};
static const TriggerClause s_cl02[] = {
    { TRIG_VEH_FOOT,    0x22, 0,    0,    0 },
    { TRIG_VEH_CHOCOBO, 0x22, 0,    0,    0 },
};
static const TriggerClause s_cl03[] = {
    { TRIG_VEH_FOOT,    0x13, 0,    0,    0 },
    { TRIG_VEH_CHOCOBO, 0x13, 0,    0,    0 },
};
static const TriggerClause s_cl04[] = {
    { TRIG_VEH_FOOT,    0x13, 0,    0,    TRIG_UNK_12 },
    { TRIG_VEH_CHOCOBO, 0x13, 0,    0,    TRIG_UNK_12 },
    { TRIG_VEH_FOOT,    0x23, 0,    0,    TRIG_UNK_10 },
    { TRIG_VEH_CHOCOBO, 0x23, 0,    0,    TRIG_UNK_10 },
};
static const TriggerClause s_cl05[] = {
    { TRIG_VEH_FOOT,    0x24, 0,    0,    0 },
    { TRIG_VEH_CHOCOBO, 0x24, 0,    0,    0 },
};
static const TriggerClause s_cl06[] = {
    { TRIG_VEH_FOOT, 0x09, 0,    0,    0 },
};
static const TriggerClause s_cl07[] = {
    { TRIG_VEH_FOOT,     0x03, 0,    0,    0 },
    { TRIG_VEH_FOOT_ALT, 0x03, 0,    0,    0 },
};
static const TriggerClause s_cl08[] = {
    { TRIG_VEH_FOOT,     0x08, 0,    0,    0 },
    { TRIG_VEH_FOOT_ALT, 0x08, 0,    0,    0 },
};
static const TriggerClause s_cl09[] = {
    // Heavy UNK flags on r=0x06; cleaner path on r=0x07.
    { TRIG_VEH_FOOT, 0x06, 0,    490,  TRIG_UNK_11 | TRIG_UNK_0F | TRIG_UNK_12 | TRIG_UNK_10 },
    { TRIG_VEH_FOOT, 0x07, 0,    3900, TRIG_UNK_12 },
};
static const TriggerClause s_cl10[] = {
    { TRIG_VEH_FOOT, 0x05, 290,  315,  0 },
};
static const TriggerClause s_cl11[] = {
    { TRIG_VEH_FOOT,     0x01, 0,    0,    0 },
    { TRIG_VEH_FOOT_ALT, 0x01, 0,    0,    0 },
};
static const TriggerClause s_cl12[] = {
    { TRIG_VEH_FOOT, 0x00, 0,    0,    0 },
};
static const TriggerClause s_cl13[] = {
    { TRIG_VEH_FOOT, 0x02, 0,    0,    TRIG_UNK_11 },
    { TRIG_VEH_FOOT, 0x00, 0,    570,  TRIG_UNK_0F },
};
static const TriggerClause s_cl14[] = {
    { TRIG_VEH_FOOT,    0x16, 0,    0,    TRIG_UNK_0F },
    { TRIG_VEH_CHOCOBO, 0x16, 0,    0,    TRIG_UNK_0F },
    { TRIG_VEH_FOOT,    0x17, 0,    0,    TRIG_UNK_11 },
    { TRIG_VEH_CHOCOBO, 0x17, 0,    0,    TRIG_UNK_11 },
};
static const TriggerClause s_cl15[] = {
    { TRIG_VEH_FOOT,     0x0B, 0,    0,    TRIG_UNK_12 },
    { TRIG_VEH_FOOT_ALT, 0x0B, 0,    0,    TRIG_UNK_12 },
};
static const TriggerClause s_cl16[] = {
    { TRIG_VEH_FOOT,     0x0A, 0,    0,    0 },
    { TRIG_VEH_FOOT_ALT, 0x0A, 0,    0,    0 },
};
static const TriggerClause s_cl17[] = {
    { TRIG_VEH_FOOT, 0x04, 0,    0,    0 },
};
// program 18 (locID 0x0172, story>=3900, topVeh=Chocobo): no inner clauses
// — the top-level vehicle restriction alone defines the trigger.
static const TriggerClause s_cl19[] = {
    { TRIG_VEH_ANY, 0x0D, 0,    3900, TRIG_UNK_20 },
};
static const TriggerClause s_cl20[] = {
    { TRIG_VEH_ANY, 0x0C, 0,    3900, 0 },
};
static const TriggerClause s_cl21[] = {
    { TRIG_VEH_FOOT, 0x18, 0,    3000, 0 },
    { TRIG_VEH_FOOT, 0x2E, 3000, 3900, 0 },
};
static const TriggerClause s_cl22[] = {
    { TRIG_VEH_FOOT, 0x19, 0,    0,    0 },
};
static const TriggerClause s_cl23[] = {
    { TRIG_VEH_FOOT, 0x1C, 0,    3000, 0 },
    { TRIG_VEH_FOOT, 0x39, 3000, 5000, 0 },
};
static const TriggerClause s_cl24[] = {
    { TRIG_VEH_FOOT, 0x0E, 0,    0,    TRIG_UNK_11 },
    { TRIG_VEH_FOOT, 0x0F, 0,    0,    TRIG_UNK_0F },
};
static const TriggerClause s_cl25[] = {
    // Multi-vehicle late-game evolution; most complex program in Section 8.
    { TRIG_VEH_RAGNAROK, 0x1B, 0,    3000, TRIG_UNK_0F | TRIG_UNK_20 },
    { TRIG_VEH_RAGNAROK, 0x44, 3000, 3900, TRIG_UNK_0F | TRIG_UNK_20 },
    { TRIG_VEH_CHOCOBO,  0x44, 3900, 0,    TRIG_UNK_0F },
    { TRIG_VEH_FOOT,     0x1A, 0,    3900, TRIG_UNK_11 },
    { TRIG_VEH_FOOT_ALT, 0x1A, 0,    3900, TRIG_UNK_11 },
};
static const TriggerClause s_cl26[] = {
    { TRIG_VEH_FOOT,     0x1A, 0,    3900, 0 },
    { TRIG_VEH_FOOT_ALT, 0x1A, 0,    3900, 0 },
};
static const TriggerClause s_cl27[] = {
    { TRIG_VEH_FOOT,     0x1A, 0,    3900, 0 },
    { TRIG_VEH_FOOT_ALT, 0x1A, 0,    3900, 0 },
};
static const TriggerClause s_cl28[] = {
    { TRIG_VEH_FOOT,     0x1A, 0,    3900, 0 },
    { TRIG_VEH_FOOT_ALT, 0x1A, 0,    3900, 0 },
};
static const TriggerClause s_cl29[] = {
    { TRIG_VEH_FOOT, 0x1E, 0,    3000, 0 },
    { TRIG_VEH_FOOT, 0x45, 3000, 0,    0 },
};
static const TriggerClause s_cl30[] = {
    { TRIG_VEH_FOOT, 0x1D, 0,    3000, 0 },
    { TRIG_VEH_FOOT, 0x2F, 0,    0,    0 },
};
static const TriggerClause s_cl31[] = {
    { TRIG_VEH_FOOT,    0x27, 0,    0,    0 },
    { TRIG_VEH_CHOCOBO, 0x27, 0,    0,    0 },
};
static const TriggerClause s_cl32[] = {
    // Esthar-candidate locID 0x01FA: 4 story-windowed regions
    // (region cycles 0x1F -> 0x30 -> 0x31 -> 0x1F across story 0..5000).
    { TRIG_VEH_FOOT, 0x1F, 0,    2500, 0 },
    { TRIG_VEH_FOOT, 0x30, 2500, 3000, 0 },
    { TRIG_VEH_FOOT, 0x31, 3000, 3900, 0 },
    { TRIG_VEH_FOOT, 0x1F, 3900, 5000, 0 },
};
static const TriggerClause s_cl33[] = {
    { TRIG_VEH_FOOT,    0x10, 0,    0,    0 },
    { TRIG_VEH_CHOCOBO, 0x10, 0,    0,    0 },
};
static const TriggerClause s_cl34[] = {
    { TRIG_VEH_FOOT,    0x12, 0,    0,    0 },
    { TRIG_VEH_CHOCOBO, 0x12, 0,    0,    0 },
};
static const TriggerClause s_cl35[] = {
    { TRIG_VEH_FOOT,    0x26, 0,    0,    0 },
    { TRIG_VEH_CHOCOBO, 0x26, 0,    0,    0 },
};
static const TriggerClause s_cl36[] = {
    { TRIG_VEH_FOOT,    0x25, 0,    0,    0 },
    { TRIG_VEH_CHOCOBO, 0x25, 0,    0,    0 },
};
static const TriggerClause s_cl37[] = {
    { TRIG_VEH_ANY, 0x15, 0,    0,    TRIG_UNK_20 },
};

// Helper macro keeps the program table compact while letting the compiler
// derive num_clauses from the array's actual length — no risk of the
// hand-written count getting out of sync with the array size.
#define WMS_NCLS(arr)  ((uint8_t)(sizeof(arr) / sizeof(arr[0])))

static const TriggerProgram s_triggerPrograms[] = {
    // [00] locID=0x0031 story>=750 — foot/Choco region 0x14
    { 0x0031,  750,    0, TRIG_VEH_ANY,      WMS_NCLS(s_cl00), s_cl00 },
    // [01] locID=0x0051 — foot/Choco region 0x21
    { 0x0051,    0,    0, TRIG_VEH_ANY,      WMS_NCLS(s_cl01), s_cl01 },
    // [02] locID=0x0091 — foot/Choco region 0x22
    { 0x0091,    0,    0, TRIG_VEH_ANY,      WMS_NCLS(s_cl02), s_cl02 },
    // [03] locID=0x0095 story>=750 — foot/Choco region 0x13
    { 0x0095,  750,    0, TRIG_VEH_ANY,      WMS_NCLS(s_cl03), s_cl03 },
    // [04] locID=0x0096 story>=750 — 4 clauses with UNK flags (regions 0x13/0x23)
    { 0x0096,  750,    0, TRIG_VEH_ANY,      WMS_NCLS(s_cl04), s_cl04 },
    // [05] locID=0x00DB — foot/Choco region 0x24
    { 0x00DB,    0,    0, TRIG_VEH_ANY,      WMS_NCLS(s_cl05), s_cl05 },
    // [06] locID=0x00EA — foot region 0x09
    { 0x00EA,    0,    0, TRIG_VEH_ANY,      WMS_NCLS(s_cl06), s_cl06 },
    // [07] locID=0x00EE story>=36 — foot/footAlt region 0x03
    { 0x00EE,   36,    0, TRIG_VEH_ANY,      WMS_NCLS(s_cl07), s_cl07 },
    // [08] locID=0x0108 story>=333 — foot/footAlt region 0x08
    { 0x0108,  333,    0, TRIG_VEH_ANY,      WMS_NCLS(s_cl08), s_cl08 },
    // [09] locID=0x010B (Balamb Town) — v0.14.98 fix: top_story_gte changed
    //      from 290 to 0. See world_map_history.h for v0.14.98 narrative.
    { 0x010B,    0,    0, TRIG_VEH_ANY,      WMS_NCLS(s_cl09), s_cl09 },
    // [10] locID=0x010C — foot region 0x05 [story 290..315]
    { 0x010C,    0,    0, TRIG_VEH_ANY,      WMS_NCLS(s_cl10), s_cl10 },
    // [11] locID=0x0111 — foot/footAlt region 0x01
    { 0x0111,    0,    0, TRIG_VEH_ANY,      WMS_NCLS(s_cl11), s_cl11 },
    // [12] locID=0x0112 story<570 — foot region 0x00
    { 0x0112,    0,  570, TRIG_VEH_ANY,      WMS_NCLS(s_cl12), s_cl12 },
    // [13] locID=0x0113 — foot region 0x02 (UNK_11) OR foot region 0x00 [<570] (UNK_0F)
    { 0x0113,    0,    0, TRIG_VEH_ANY,      WMS_NCLS(s_cl13), s_cl13 },
    // [14] locID=0x0117 — 4 clauses regions 0x16/0x17 with UNK flags
    { 0x0117,    0,    0, TRIG_VEH_ANY,      WMS_NCLS(s_cl14), s_cl14 },
    // [15] locID=0x0147 story 350..490 — foot/footAlt region 0x0B (UNK_12)
    { 0x0147,  350,  490, TRIG_VEH_ANY,      WMS_NCLS(s_cl15), s_cl15 },
    // [16] locID=0x0169 story>=350 — foot/footAlt region 0x0A
    { 0x0169,  350,    0, TRIG_VEH_ANY,      WMS_NCLS(s_cl16), s_cl16 },
    // [17] locID=0x016D story>=205 — foot region 0x04
    { 0x016D,  205,    0, TRIG_VEH_ANY,      WMS_NCLS(s_cl17), s_cl17 },
    // [18] locID=0x0172 story>=3900 topVeh=Chocobo — NO inner clauses (mobile destination)
    { 0x0172, 3900,    0, TRIG_VEH_CHOCOBO,  0,                 nullptr },
    // [19] locID=0x0172 story>=636 topVeh=Ragnarok — region 0x0D [<3900] +UNK_20
    { 0x0172,  636,    0, TRIG_VEH_RAGNAROK, WMS_NCLS(s_cl19), s_cl19 },
    // [20] locID=0x0172 story>=636 topVeh=Garden — region 0x0C [<3900]
    { 0x0172,  636,    0, TRIG_VEH_GARDEN,   WMS_NCLS(s_cl20), s_cl20 },
    // [21] locID=0x0175 story>=1600 — foot region 0x18 [<3000] OR 0x2E [3000..3900]
    { 0x0175, 1600,    0, TRIG_VEH_ANY,      WMS_NCLS(s_cl21), s_cl21 },
    // [22] locID=0x0176 — foot region 0x19
    { 0x0176,    0,    0, TRIG_VEH_ANY,      WMS_NCLS(s_cl22), s_cl22 },
    // [23] locID=0x017A story>=1750 — foot region 0x1C [<3000] OR 0x39 [3000..5000]
    { 0x017A, 1750,    0, TRIG_VEH_ANY,      WMS_NCLS(s_cl23), s_cl23 },
    // [24] locID=0x0189 story>=750 — foot regions 0x0E (UNK_11) / 0x0F (UNK_0F)
    { 0x0189,  750,    0, TRIG_VEH_ANY,      WMS_NCLS(s_cl24), s_cl24 },
    // [25] locID=0x0196 story>=1750 — 5 clauses, multi-vehicle late-game evolution
    { 0x0196, 1750,    0, TRIG_VEH_ANY,      WMS_NCLS(s_cl25), s_cl25 },
    // [26] locID=0x0197 story>=1750 — foot/footAlt region 0x1A [<3900]
    { 0x0197, 1750,    0, TRIG_VEH_ANY,      WMS_NCLS(s_cl26), s_cl26 },
    // [27] locID=0x01B6 story>=1750 — foot/footAlt region 0x1A [<3900]
    { 0x01B6, 1750,    0, TRIG_VEH_ANY,      WMS_NCLS(s_cl27), s_cl27 },
    // [28] locID=0x01B7 story>=1750 — foot/footAlt region 0x1A [<3900]
    { 0x01B7, 1750,    0, TRIG_VEH_ANY,      WMS_NCLS(s_cl28), s_cl28 },
    // [29] locID=0x01B9 story>=1750 — foot region 0x1E [<3000] OR 0x45 [3000..]
    { 0x01B9, 1750,    0, TRIG_VEH_ANY,      WMS_NCLS(s_cl29), s_cl29 },
    // [30] locID=0x01BB story>=1750 — foot region 0x1D [<3000] OR 0x2F
    { 0x01BB, 1750,    0, TRIG_VEH_ANY,      WMS_NCLS(s_cl30), s_cl30 },
    // [31] locID=0x01D2 — foot/Choco region 0x27
    { 0x01D2,    0,    0, TRIG_VEH_ANY,      WMS_NCLS(s_cl31), s_cl31 },
    // [32] locID=0x01FA story>=1750 — 4 story-windowed regions (Esthar candidate)
    { 0x01FA, 1750,    0, TRIG_VEH_ANY,      WMS_NCLS(s_cl32), s_cl32 },
    // [33] locID=0x0250 — foot/Choco region 0x10
    { 0x0250,    0,    0, TRIG_VEH_ANY,      WMS_NCLS(s_cl33), s_cl33 },
    // [34] locID=0x028C story>=900 — foot/Choco region 0x12
    { 0x028C,  900,    0, TRIG_VEH_ANY,      WMS_NCLS(s_cl34), s_cl34 },
    // [35] locID=0x028D — foot/Choco region 0x26
    { 0x028D,    0,    0, TRIG_VEH_ANY,      WMS_NCLS(s_cl35), s_cl35 },
    // [36] locID=0x02B5 — foot/Choco region 0x25
    { 0x02B5,    0,    0, TRIG_VEH_ANY,      WMS_NCLS(s_cl36), s_cl36 },
    // [37] locID=0x02C1 topVeh=Ragnarok — region 0x15 (UNK_20)
    { 0x02C1,    0,    0, TRIG_VEH_RAGNAROK, WMS_NCLS(s_cl37), s_cl37 },
};
static const int TRIGGER_PROGRAM_COUNT = sizeof(s_triggerPrograms) / sizeof(s_triggerPrograms[0]);

// ============================================================================
// LogTriggerPrograms — walks s_triggerPrograms[] at module init and emits one
// log line per program for runtime sanity-check that the embedded v0.14.93
// trigger data compiled correctly into the binary.
// ============================================================================
static void LogTriggerPrograms()
{
    int totalClauses = 0;
    for (int i = 0; i < TRIGGER_PROGRAM_COUNT; i++) {
        totalClauses += s_triggerPrograms[i].num_clauses;
    }
    Log::World("WorldMap: [TRIGGER-PROGRAMS] count=%d totalClauses=%d (sanity check that embedded data compiled)",
               TRIGGER_PROGRAM_COUNT, totalClauses);

    for (int i = 0; i < TRIGGER_PROGRAM_COUNT; i++) {
        const TriggerProgram& p = s_triggerPrograms[i];

        char gateBuf[40];
        if (p.top_story_gte == 0 && p.top_story_lt == 0) {
            snprintf(gateBuf, sizeof(gateBuf), "any");
        } else if (p.top_story_lt == 0) {
            snprintf(gateBuf, sizeof(gateBuf), "[%u,inf)", (unsigned)p.top_story_gte);
        } else if (p.top_story_gte == 0) {
            snprintf(gateBuf, sizeof(gateBuf), "[0,%u)", (unsigned)p.top_story_lt);
        } else {
            snprintf(gateBuf, sizeof(gateBuf), "[%u,%u)",
                     (unsigned)p.top_story_gte, (unsigned)p.top_story_lt);
        }

        char clausesBuf[768];
        clausesBuf[0] = '\0';
        size_t cpos = 0;

        if (p.num_clauses == 0 || p.clauses == nullptr) {
            snprintf(clausesBuf, sizeof(clausesBuf), "(none)");
        } else {
            for (uint8_t k = 0; k < p.num_clauses; k++) {
                const TriggerClause& c = p.clauses[k];

                char storyB[40];
                if (c.story_gte == 0 && c.story_lt == 0) {
                    storyB[0] = '\0';
                } else if (c.story_lt == 0) {
                    snprintf(storyB, sizeof(storyB), ",s=[%u,inf)", (unsigned)c.story_gte);
                } else if (c.story_gte == 0) {
                    snprintf(storyB, sizeof(storyB), ",s=[0,%u)", (unsigned)c.story_lt);
                } else {
                    snprintf(storyB, sizeof(storyB), ",s=[%u,%u)",
                             (unsigned)c.story_gte, (unsigned)c.story_lt);
                }

                char unkB[20];
                if (c.unk_flags == 0) {
                    unkB[0] = '\0';
                } else {
                    snprintf(unkB, sizeof(unkB), ",unk=0x%04X", (unsigned)c.unk_flags);
                }

                int written = snprintf(clausesBuf + cpos,
                                       sizeof(clausesBuf) - cpos,
                                       "%s(v=0x%02X,r=0x%02X%s%s)",
                                       (k == 0) ? "" : ",",
                                       (unsigned)c.vehicle, (unsigned)c.region,
                                       storyB, unkB);
                if (written < 0 || (size_t)written >= sizeof(clausesBuf) - cpos) {
                    break;
                }
                cpos += (size_t)written;
            }
        }

        Log::World("WorldMap: [TRIGGER-PROGRAMS] [%02d] loc=0x%04X storyGate=%s topVeh=0x%02X clauses=%u: [%s]",
                   i, (unsigned)p.loc_id, gateBuf, (unsigned)p.top_vehicle,
                   (unsigned)p.num_clauses, clausesBuf);
    }
}

// ============================================================================
// v0.18.3.206: DECODED ENTRY FIRING AREAS (offline/TRIGGER_FIRING_AREAS.md).
// The trigger system is now FULLY decoded from the exe + wmsetus Section 8:
// a field entry fires only while standing on a wmx poly with byte14 bit 3 set
// (hand-painted entry polys at each gate mouth), inside the program's 8192u
// segment, within the clause's sub-segment coordinate bounds, with the vehicle
// and story gates satisfied. The former "unknown" predicate bits were the
// sub-segment bounds; 0xFF08 is the destination ACTION, not a region test.
// This table holds, per catalog destination, a VALIDATED aim point inside the
// firing area plus the area's bbox: aim = drive-target override; bbox = the
// final-approach mow zone (replaces the blind orbit, which at Timber circled
// radius 610-1330u around a firing patch that lies entirely within 432u --
// the orbit had a hole exactly where the target was). Validated against the
// .205 log: Dollet's successful entry fired ON its area edge; every failed
// Timber orbit position lies OUTSIDE its area.
struct EntryAimInfo {
    const char* name;         // catalog name (matched against s_driveTargetName)
    int32_t     aimX, aimY;   // validated point inside the firing area
    int32_t     x0, x1;       // area bbox x [min,max]
    int32_t     y0, y1;       // area bbox y [min,max]
    bool        footOnly;     // no FOOT_ALT clause: cannot be entered by car
};
static const EntryAimInfo s_entryAims[] = {
    { "Timber",           -22580,  -5291, -22685, -22371,  -5632,  -5120, true  },
    { "Dollet",           -14513, -39119, -15409, -13516, -39951, -38175, false },
    { "Balamb Town",       12560, -26800,  12288,  12884, -26896, -26624, false },
    { "Balamb Garden",     24304, -30300,  23552,  25410, -31370, -29696, true  },
    { "Fire Cavern",       30239, -29528,  30112,  30394, -29750, -29192, true  },
    { "Galbadia Garden",  -36895, -27082, -37764, -35964, -28036, -26236, true  },
    { "Galbadia Station", -38914, -24767, -39426, -38398, -24936, -24682, true  },
};
static const int ENTRY_AIM_COUNT = (int)(sizeof(s_entryAims) / sizeof(s_entryAims[0]));
static int FindEntryAim(const char* name)
{
    for (int i = 0; i < ENTRY_AIM_COUNT; i++)
        if (strcmp(s_entryAims[i].name, name) == 0) return i;
    return -1;
}
