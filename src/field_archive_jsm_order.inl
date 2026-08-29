// field_archive_jsm_order.inl -- the engine's own script-object ordering.
// Included from field_archive_jsm.inl BEFORE field_archive_jsm_scan.inl.
// Do not compile independently.
//
// WHERE THIS COMES FROM
// ---------------------
// field_scripts_init (FF8_EN.exe 0x0052BC00) builds the field's script objects
// in FOUR consecutive loops. Reading them settles three orderings this codebase
// had been guessing at since v0.07.73:
//
//   0x0052BDAC  cl=[jsm+1] -> nLines   dl=[jsm+0] -> nDoors
//               cl=[jsm+3] -> nOthers  dl=[jsm+2] -> nBackgrounds
//               ecx = jsm+8            -> the group-word array pointer
//
//   loop 1 @0x0052BF32  count=nLines        exec tag 0x20000000  stride 0x1A0
//   loop 2 @0x0052C02A  count=nDoors        exec tag 0x40000000  stride 0x18C
//   loop 3 @0x0052C13B  count=nBackgrounds  exec tag 0x80001000  stride 0x1B4
//   loop 4 @0x0052C270  count=nOthers       exec tag 0x10080002  stride 0x264
//
// Each loop walks the SAME running group pointer (0x01D9CDEC) forward, so:
//
//   (1) THE GROUP-WORD ARRAY IS ORDERED  Lines, Doors, Backgrounds, Others.
//       Not "Door, Line, Bg, Other" -- that assumption mis-typed the first
//       min(nD,nL) groups of every field that has both doors and lines (79 of
//       them), handing real trigger Lines to the Door branch and vice versa.
//
// Each loop also writes its object pointer into the shared table at 0x01D9D020,
// at an index built from the counts:
//       lines        -> [nOthers + i]                 (0x0052BF44)
//       doors        -> [nOthers + nLines + nBg + i]  (0x0052C050)
//       backgrounds  -> [nOthers + nLines + i]        (0x0052C155)
//       others       -> [i]                           (0x0052C279, ebp = table)
//
//   (2) THE RUNTIME TABLE IS ORDERED  Others, Lines, Backgrounds, Doors.
//       REQ/REQSW/REQEW index this table directly with their INLINE param
//       (0x0051CD8D: `mov ecx, [eax*4 + 0x01D9D020]`), so a REQ operand is a
//       runtime SLOT, never a group index.
//
//   (3) THE SYM BARE-NAME LIST IS ORDERED  Others, Lines, Backgrounds -- doors
//       omitted -- with each block in ascending code order (group `start`).
//       Verified against all 849 fields that ship a SYM: 848 agree exactly and
//       the one holdout (cwwood6) swaps two Others names between itself and its
//       own method section. The old `symIdx = e - countDoors` assumed the SYM
//       ran Lines, Backgrounds, Others, which is a different order entirely; on
//       ecoway1 (Esthar) that put EVERY one of 35 names on the wrong entity.
//
// Only the Background block is ever permuted relative to code order (48 fields),
// which is why ranking by `start` is done per category rather than assumed.

// Max groups the scanner supports (matches the scanner's own 128-entry arrays).
static const int JSM_ORDER_MAX = 128;

struct JSMOrderMap {
    int     doors, lines, backgrounds, others, total;
    bool    valid;
    int8_t  cat[JSM_ORDER_MAX];          // 0=Door 1=Line 2=Background 3=Other
    int16_t symIdx[JSM_ORDER_MAX];       // group -> SYM bare index, -1 for doors
    int16_t slot[JSM_ORDER_MAX];         // group -> runtime script-object slot
    int16_t groupOfSlot[JSM_ORDER_MAX];  // runtime slot -> group, -1 if none
};

// Build the map from the JSM header + group-word array.
// groupStart[e] is the group word's label field (bits 7-15), i.e. the entity's
// first index into the script entry-point table -- its position in code order.
static void BuildJSMOrderMap(int nDoors, int nLines, int nBg, int nOthers,
                             int total, const int* groupStart, JSMOrderMap& o)
{
    o.doors = nDoors; o.lines = nLines; o.backgrounds = nBg; o.others = nOthers;
    o.total = total;
    o.valid = false;
    for (int i = 0; i < JSM_ORDER_MAX; i++) {
        o.cat[i] = -1; o.symIdx[i] = -1; o.slot[i] = -1; o.groupOfSlot[i] = -1;
    }
    if (total <= 0 || total > JSM_ORDER_MAX) return;
    if (nDoors < 0 || nLines < 0 || nBg < 0 || nOthers < 0) return;
    if (nDoors + nLines + nBg + nOthers != total) return;

    // (1) category by group index, in the engine's L, D, B, O order.
    const int lineBase = 0;
    const int doorBase = nLines;
    const int bgBase   = nLines + nDoors;
    const int othBase  = nLines + nDoors + nBg;
    for (int g = 0; g < total; g++) {
        if      (g < doorBase) o.cat[g] = 1;   // Line
        else if (g < bgBase)   o.cat[g] = 0;   // Door
        else if (g < othBase)  o.cat[g] = 2;   // Background
        else                   o.cat[g] = 3;   // Other
    }

    // (2) runtime slot: Others, Lines, Backgrounds, Doors.
    for (int g = 0; g < total; g++) {
        int s;
        switch (o.cat[g]) {
            case 3:  s = g - othBase; break;                       // Other
            case 1:  s = nOthers + (g - lineBase); break;          // Line
            case 2:  s = nOthers + nLines + (g - bgBase); break;   // Background
            default: s = nOthers + nLines + nBg + (g - doorBase); break;  // Door
        }
        o.slot[g] = (int16_t)s;
        if (s >= 0 && s < JSM_ORDER_MAX) o.groupOfSlot[s] = (int16_t)g;
    }

    // (3) SYM bare index: Others, Lines, Backgrounds; within a category, rank by
    // ascending code start. Doors are absent from the SYM (-1).
    const int symBase[4] = { -1, nOthers, nOthers + nLines, 0 };  // by cat code
    for (int g = 0; g < total; g++) {
        int c = o.cat[g];
        if (c == 0) { o.symIdx[g] = -1; continue; }
        int rank = 0;
        for (int h = 0; h < total; h++) {
            if (h == g || o.cat[h] != c) continue;
            if (groupStart[h] < groupStart[g] ||
                (groupStart[h] == groupStart[g] && h < g)) rank++;
        }
        o.symIdx[g] = (int16_t)(symBase[c] + rank);
    }
    o.valid = true;
}
