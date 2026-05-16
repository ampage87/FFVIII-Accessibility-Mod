// countdown_scan.inl — In-mod memory scanner for engine-globals values.
//
// HISTORY
// =======
// v0.15.13.0: scanner introduced. Region 1 = 8 KB at 0x01CFE9B8
//   (game-object struct only), Region 2 = 1 MB at 0x01D00000.
//   BAT showed scanner working mechanically but no candidate matched
//   the Dollet timer's expected value. Region 1 was too narrow.
// v0.15.13.1: Region 1 expanded to 192 KB at 0x01CD0000 (broader
//   engine-globals zone). Bounds widened: MAX_PLAUSIBLE_VAL 200000 ->
//   2000000, MAX_RATE_PER_SEC 200 -> 2000 (to admit ms-encoded
//   timers). Log-spam bug fixed (s_ringFullLogged gates the
//   "ring full" line to fire once).
//   BAT cycle 11 (21:50:40) found the timer:
//     [CountdownScan] R1 u32 #0 addr=0x01CFE92C u32 cur=1711 old=1715
//       dec=4 rate=1.00/s
//   Exactly 1.00/s monotonic decrement, value 1711 = 28:31 remaining
//   in seconds — perfectly consistent with a Dollet chase save loaded
//   mid-run. The address is 0x8C bytes below the game-object struct
//   base (which is why v0.15.13.0 missed it). The candidate only
//   showed in cycle 11 because the top-16 cap pushed it out of most
//   other cycles where 16+ faster candidates ranked higher; cycle 11
//   was calm enough for the slow timer to make the cut.
//
// v0.15.13.2 (THIS BUILD)
// =======================
// Scanner DISABLED via COUNTDOWN_SCAN_ENABLED = 0. The address is
// known; the scanner has served its purpose. Disabling frees ~6 MB of
// static buffer memory and the per-snapshot/analyze CPU cost.
//
// The file is intentionally kept (not deleted) so we can re-enable the
// scanner if a future engine global needs to be hunted the same way.
// To re-enable: change COUNTDOWN_SCAN_ENABLED to 1 below, optionally
// adjust REGION1_BASE / REGION1_BYTES / REGION2_BASE / REGION2_BYTES
// for the new target, and rebuild. The Initialize/Update API surface
// (called from countdown_timer.cpp) is unchanged across the toggle.

#define COUNTDOWN_SCAN_ENABLED 0

namespace Scan {

#if COUNTDOWN_SCAN_ENABLED

// ---------------------------------------------------------------------------
// Configuration (live when COUNTDOWN_SCAN_ENABLED = 1)
// ---------------------------------------------------------------------------

static constexpr uintptr_t REGION1_BASE       = 0x01CD0000;
static constexpr size_t    REGION1_BYTES      = 192 * 1024;  // 196608 = 0x30000
static constexpr uintptr_t REGION2_BASE       = 0x01D00000;
static constexpr size_t    REGION2_BYTES      = 1024 * 1024;
static constexpr size_t    PAGE_BYTES         = 4096;
static constexpr int       SNAPSHOT_COUNT     = 5;
static constexpr DWORD     SNAPSHOT_INTERVAL_MS = 1000;
static constexpr DWORD     LOG_INTERVAL_MS    = 5000;
static constexpr int       TOP_N              = 16;
static constexpr double    MIN_RATE_PER_SEC   = 0.10;
static constexpr double    MAX_RATE_PER_SEC   = 2000.0;
static constexpr uint32_t  MAX_PLAUSIBLE_VAL  = 2000000;
static constexpr size_t    R1_PAGES = REGION1_BYTES / PAGE_BYTES;  // 48
static constexpr size_t    R2_PAGES = REGION2_BYTES / PAGE_BYTES;  // 256
static constexpr double WINDOW_SEC =
    (double)(SNAPSHOT_COUNT - 1) * (double)SNAPSHOT_INTERVAL_MS / 1000.0;

// ---------------------------------------------------------------------------
// State
// ---------------------------------------------------------------------------

static uint8_t s_region1Buf[SNAPSHOT_COUNT][REGION1_BYTES];
static uint8_t s_region2Buf[SNAPSHOT_COUNT][REGION2_BYTES];
static bool    s_region1PageValid[R1_PAGES];
static bool    s_region2PageValid[R2_PAGES];
static int     s_snapshotsTaken     = 0;
static int     s_writeSlot          = 0;
static DWORD   s_lastSnapshotTickMs = 0;
static DWORD   s_lastLogTickMs      = 0;
static int     s_scanCycleCount     = 0;
static bool    s_ringFullLogged     = false;

struct Candidate {
    uintptr_t addr;
    uint32_t  current;
    uint32_t  oldest;
    int       totalDec;
    uint8_t   width;
};

static Candidate s_topR1_u16[TOP_N];
static int       s_topR1_u16_count = 0;
static Candidate s_topR1_u32[TOP_N];
static int       s_topR1_u32_count = 0;
static Candidate s_topR2_u16[TOP_N];
static int       s_topR2_u16_count = 0;
static Candidate s_topR2_u32[TOP_N];
static int       s_topR2_u32_count = 0;

static void TopNInsert(Candidate* top, int& count, const Candidate& cand) {
    if (count < TOP_N) {
        top[count++] = cand;
    } else if (cand.totalDec > top[TOP_N - 1].totalDec) {
        top[TOP_N - 1] = cand;
    } else {
        return;
    }
    for (int i = count - 1; i > 0; i--) {
        if (top[i].totalDec > top[i - 1].totalDec) {
            Candidate tmp = top[i];
            top[i] = top[i - 1];
            top[i - 1] = tmp;
        } else {
            break;
        }
    }
}

static void ResetTopN() {
    s_topR1_u16_count = 0;
    s_topR1_u32_count = 0;
    s_topR2_u16_count = 0;
    s_topR2_u32_count = 0;
}

static bool TryCopyPage(void* dst, const void* src, size_t bytes) {
    __try {
        memcpy(dst, src, bytes);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

static void TakeRegionSnapshot(uintptr_t base,
                               uint8_t* dst,
                               bool* pageValid,
                               size_t pageCount) {
    for (size_t p = 0; p < pageCount; p++) {
        if (!pageValid[p]) continue;
        const uint8_t* srcPage = reinterpret_cast<const uint8_t*>(base + p * PAGE_BYTES);
        uint8_t* dstPage = dst + p * PAGE_BYTES;
        if (!TryCopyPage(dstPage, srcPage, PAGE_BYTES)) {
            pageValid[p] = false;
            memset(dstPage, 0, PAGE_BYTES);
        }
    }
}

static void AnalyzeRegionU16(uintptr_t base,
                              const uint8_t* snapshotBuf,
                              size_t regionBytes,
                              const bool* pageValid,
                              size_t pageCount,
                              Candidate* topList,
                              int& topListCount) {
    int slotOrder[SNAPSHOT_COUNT];
    for (int i = 0; i < SNAPSHOT_COUNT; i++) {
        slotOrder[i] = (s_writeSlot + i) % SNAPSHOT_COUNT;
    }

    for (size_t off = 0; off + 2 <= regionBytes; off += 2) {
        size_t page = off / PAGE_BYTES;
        if (page < pageCount && !pageValid[page]) continue;

        uint16_t vals[SNAPSHOT_COUNT];
        for (int s = 0; s < SNAPSHOT_COUNT; s++) {
            vals[s] = *reinterpret_cast<const uint16_t*>(
                snapshotBuf + slotOrder[s] * regionBytes + off);
        }

        if (vals[0] == 0 || vals[SNAPSHOT_COUNT - 1] == 0) continue;
        bool allNonzero = true;
        for (int s = 1; s < SNAPSHOT_COUNT - 1; s++) {
            if (vals[s] == 0) { allNonzero = false; break; }
        }
        if (!allNonzero) continue;
        if (vals[0] == 0xFFFF || vals[SNAPSHOT_COUNT - 1] == 0xFFFF) continue;

        bool monotonic = true;
        for (int s = 0; s < SNAPSHOT_COUNT - 1; s++) {
            if (vals[s + 1] > vals[s]) { monotonic = false; break; }
        }
        if (!monotonic) continue;

        int totalDec = (int)vals[0] - (int)vals[SNAPSHOT_COUNT - 1];
        if (totalDec <= 0) continue;

        double ratePerSec = (double)totalDec / WINDOW_SEC;
        if (ratePerSec < MIN_RATE_PER_SEC || ratePerSec > MAX_RATE_PER_SEC) continue;

        Candidate c;
        c.addr     = base + off;
        c.current  = vals[SNAPSHOT_COUNT - 1];
        c.oldest   = vals[0];
        c.totalDec = totalDec;
        c.width    = 2;
        TopNInsert(topList, topListCount, c);
    }
}

static void AnalyzeRegionU32(uintptr_t base,
                              const uint8_t* snapshotBuf,
                              size_t regionBytes,
                              const bool* pageValid,
                              size_t pageCount,
                              Candidate* topList,
                              int& topListCount) {
    int slotOrder[SNAPSHOT_COUNT];
    for (int i = 0; i < SNAPSHOT_COUNT; i++) {
        slotOrder[i] = (s_writeSlot + i) % SNAPSHOT_COUNT;
    }

    for (size_t off = 0; off + 4 <= regionBytes; off += 4) {
        size_t page = off / PAGE_BYTES;
        if (page < pageCount && !pageValid[page]) continue;

        uint32_t vals[SNAPSHOT_COUNT];
        for (int s = 0; s < SNAPSHOT_COUNT; s++) {
            vals[s] = *reinterpret_cast<const uint32_t*>(
                snapshotBuf + slotOrder[s] * regionBytes + off);
        }

        if (vals[0] == 0 || vals[SNAPSHOT_COUNT - 1] == 0) continue;
        if (vals[0] == 0xFFFFFFFF || vals[SNAPSHOT_COUNT - 1] == 0xFFFFFFFF) continue;
        if (vals[0] > MAX_PLAUSIBLE_VAL) continue;
        if (vals[SNAPSHOT_COUNT - 1] > MAX_PLAUSIBLE_VAL) continue;

        bool allNonzero = true;
        for (int s = 1; s < SNAPSHOT_COUNT - 1; s++) {
            if (vals[s] == 0) { allNonzero = false; break; }
        }
        if (!allNonzero) continue;

        bool monotonic = true;
        for (int s = 0; s < SNAPSHOT_COUNT - 1; s++) {
            if (vals[s + 1] > vals[s]) { monotonic = false; break; }
        }
        if (!monotonic) continue;

        int totalDec = (int)((int64_t)vals[0] - (int64_t)vals[SNAPSHOT_COUNT - 1]);
        if (totalDec <= 0) continue;

        double ratePerSec = (double)totalDec / WINDOW_SEC;
        if (ratePerSec < MIN_RATE_PER_SEC || ratePerSec > MAX_RATE_PER_SEC) continue;

        Candidate c;
        c.addr     = base + off;
        c.current  = vals[SNAPSHOT_COUNT - 1];
        c.oldest   = vals[0];
        c.totalDec = totalDec;
        c.width    = 4;
        TopNInsert(topList, topListCount, c);
    }
}

static void LogTopList(const char* tag, const Candidate* top, int count) {
    if (count == 0) {
        Log::Mod("[CountdownScan] %s: no candidates this cycle "
                 "(no aligned uint16/uint32 values decremented "
                 "monotonically at a plausible timer rate).", tag);
        return;
    }
    Log::Mod("[CountdownScan] %s: %d candidate(s) this cycle, "
             "sorted by total decrement (rate-per-second computed over "
             "%.1fs window):", tag, count, WINDOW_SEC);
    for (int i = 0; i < count; i++) {
        const Candidate& c = top[i];
        double ratePerSec = (double)c.totalDec / WINDOW_SEC;
        Log::Mod("[CountdownScan] %s #%d addr=0x%08X u%u cur=%u old=%u "
                 "dec=%d rate=%.2f/s",
                 tag, i, (uint32_t)c.addr, (unsigned)(c.width * 8),
                 (unsigned)c.current, (unsigned)c.oldest, c.totalDec,
                 ratePerSec);
    }
}

static void AnalyzeAndLog() {
    ResetTopN();
    AnalyzeRegionU16(REGION1_BASE,
                     reinterpret_cast<const uint8_t*>(s_region1Buf),
                     REGION1_BYTES, s_region1PageValid, R1_PAGES,
                     s_topR1_u16, s_topR1_u16_count);
    AnalyzeRegionU32(REGION1_BASE,
                     reinterpret_cast<const uint8_t*>(s_region1Buf),
                     REGION1_BYTES, s_region1PageValid, R1_PAGES,
                     s_topR1_u32, s_topR1_u32_count);
    AnalyzeRegionU16(REGION2_BASE,
                     reinterpret_cast<const uint8_t*>(s_region2Buf),
                     REGION2_BYTES, s_region2PageValid, R2_PAGES,
                     s_topR2_u16, s_topR2_u16_count);
    AnalyzeRegionU32(REGION2_BASE,
                     reinterpret_cast<const uint8_t*>(s_region2Buf),
                     REGION2_BYTES, s_region2PageValid, R2_PAGES,
                     s_topR2_u32, s_topR2_u32_count);

    s_scanCycleCount++;
    Log::Mod("[CountdownScan] === Scan cycle #%d ===", s_scanCycleCount);
    LogTopList("R1 u16", s_topR1_u16, s_topR1_u16_count);
    LogTopList("R1 u32", s_topR1_u32, s_topR1_u32_count);
    LogTopList("R2 u16", s_topR2_u16, s_topR2_u16_count);
    LogTopList("R2 u32", s_topR2_u32, s_topR2_u32_count);
    Log::Mod("[CountdownScan] === End of cycle #%d ===", s_scanCycleCount);
}

static void Initialize() {
    memset(s_region1Buf, 0, sizeof(s_region1Buf));
    memset(s_region2Buf, 0, sizeof(s_region2Buf));
    for (size_t i = 0; i < R1_PAGES; i++) s_region1PageValid[i] = true;
    for (size_t i = 0; i < R2_PAGES; i++) s_region2PageValid[i] = true;
    s_snapshotsTaken     = 0;
    s_writeSlot          = 0;
    s_lastSnapshotTickMs = 0;
    s_lastLogTickMs      = 0;
    s_scanCycleCount     = 0;
    s_ringFullLogged     = false;
    ResetTopN();
    Log::Mod("[CountdownScan] Initialize: armed. Region 1: 0x%08X+%u "
             "(%u pages). Region 2: 0x%08X+%u (%u pages). Snapshot every "
             "%ums; log top %d per region/width every %ums.",
             (uint32_t)REGION1_BASE, (unsigned)REGION1_BYTES, (unsigned)R1_PAGES,
             (uint32_t)REGION2_BASE, (unsigned)REGION2_BYTES, (unsigned)R2_PAGES,
             (unsigned)SNAPSHOT_INTERVAL_MS, TOP_N,
             (unsigned)LOG_INTERVAL_MS);
}

static void Update(DWORD now) {
    bool dueForSnapshot = (s_lastSnapshotTickMs == 0) ||
                          ((now - s_lastSnapshotTickMs) >= SNAPSHOT_INTERVAL_MS);
    if (dueForSnapshot) {
        TakeRegionSnapshot(REGION1_BASE, s_region1Buf[s_writeSlot],
                           s_region1PageValid, R1_PAGES);
        TakeRegionSnapshot(REGION2_BASE, s_region2Buf[s_writeSlot],
                           s_region2PageValid, R2_PAGES);
        int prevWriteSlot = s_writeSlot;
        s_writeSlot = (s_writeSlot + 1) % SNAPSHOT_COUNT;
        if (s_snapshotsTaken < SNAPSHOT_COUNT) s_snapshotsTaken++;
        s_lastSnapshotTickMs = now;
        if (s_snapshotsTaken == 1) {
            int r1Valid = 0, r2Valid = 0;
            for (size_t i = 0; i < R1_PAGES; i++) if (s_region1PageValid[i]) r1Valid++;
            for (size_t i = 0; i < R2_PAGES; i++) if (s_region2PageValid[i]) r2Valid++;
            Log::Mod("[CountdownScan] First snapshot done at slot %d: "
                     "Region 1 %d/%u pages mapped, Region 2 %d/%u pages mapped.",
                     prevWriteSlot, r1Valid, (unsigned)R1_PAGES,
                     r2Valid, (unsigned)R2_PAGES);
        } else if (s_snapshotsTaken == SNAPSHOT_COUNT && !s_ringFullLogged) {
            s_ringFullLogged = true;
            Log::Mod("[CountdownScan] Ring is now full (%d snapshots). "
                     "Analysis will begin on the next scheduled log tick.",
                     SNAPSHOT_COUNT);
        }
    }

    if (s_snapshotsTaken < SNAPSHOT_COUNT) return;
    bool dueForLog = (s_lastLogTickMs == 0) ||
                     ((now - s_lastLogTickMs) >= LOG_INTERVAL_MS);
    if (!dueForLog) return;

    AnalyzeAndLog();
    s_lastLogTickMs = now;
}

#else  // COUNTDOWN_SCAN_ENABLED == 0

// ---------------------------------------------------------------------------
// Disabled stubs (v0.15.13.2)
// ---------------------------------------------------------------------------
//
// The live engine timer global was identified at 0x01CFE92C by the
// v0.15.13.1 BAT. The scanner has served its purpose; disabling it
// here saves ~6 MB of static memory and the per-frame snapshot/analyze
// cost. The full implementation above is preserved in the
// #if COUNTDOWN_SCAN_ENABLED block — toggle the flag at the top of
// this file to re-enable.

static void Initialize() {
    Log::Mod("[CountdownScan] DISABLED (v0.15.13.2). Set "
             "COUNTDOWN_SCAN_ENABLED=1 in countdown_scan.inl to "
             "re-enable for future address hunts.");
}

static void Update(DWORD /*now*/) {
    // no-op
}

#endif  // COUNTDOWN_SCAN_ENABLED

} // namespace Scan
