// battle_tts_roi_calib.inl — ROI calibration auto-capture (v0.13.95)
//
// Included by battle_tts.cpp AFTER battle_tts_screenshot.inl. Provides
// auto-triggered framebuffer dumps for ROI / color-threshold calibration.
//
// Goal: capture the rendered frames around each damage event so the next
// instance of Claude can read them via filesystem:read_media_file and
// extract the exact pixel rectangle + RGB values for the floating damage
// number sprite. That lets us build an in-mod color-count trigger that
// fires on the same frame the number first becomes visible.
//
// Trigger: the damage anim flag at 0x01D280C0 (already polled by
// PollHPChanges as BATTLE_DAMAGE_ANIM_FLAG) transitions from 0 -> non-zero
// at attack start and back to 0 at sprite despawn. We start capturing on
// the 0->1 edge, stop 0.5s after the 1->0 edge.
//
// Output: per-event subfolder under Logs/screenshots/:
//   roi_calib_HHMMSS_mmm_evtNNN/
//     frame000.png ... frameNNN.png   downsampled RGB
//     meta.txt                        per-frame anim flag + dmg display
//
// Capture pacing: every 2nd SwapBuffers call (effective 30fps). Pre-buffer
// of 15 frames captures the lead-up before the anim flag rises. Active
// capture cap of 240 frames covers up to 8s of in-event time. 15-frame
// post-tail captures the despawn animation.
//
// Memory: at 480x270 RGB, ~389 KB / frame * 270 frames = ~105 MB peak.
// Allocated lazily (the GL viewport tells us source dimensions on first
// frame, downsample factor 4 means 1920x1080 -> 480x270, 1280x720 -> 320x180,
// etc.). Per-frame mallocs in HookedSwapBuffers, freed by the worker thread
// after PNG encode.
//
// Disabled by setting ROI_CALIBRATION_CAPTURE to 0 below.

// ============================================================================
// Build-time toggle
// ============================================================================
// Issue #62: defaults to the battle diagnostic-screenshot master switch
// (BATTLE_DIAG_SCREENSHOTS, declared in battle_tts.h, included by
// battle_tts.cpp before this file). With that flag off (the shipping
// default) RoiCalib_OnSwapBuffers compiles to the no-op stub below, so no
// roi_calib_* folders are written and the per-frame glReadPixels/downsample
// work is skipped entirely. Set BATTLE_DIAG_SCREENSHOTS to 1 to re-enable.
#ifndef ROI_CALIBRATION_CAPTURE
#define ROI_CALIBRATION_CAPTURE BATTLE_DIAG_SCREENSHOTS
#endif

#if ROI_CALIBRATION_CAPTURE

// ============================================================================
// Configuration constants
// ============================================================================

// Same hardcoded user path convention as KIND4_SCREENSHOT_DIR (battle_tts_sprite.inl).
static const char* ROI_CAPTURE_PARENT_DIR =
    "C:\\Users\\ampag\\OneDrive\\Documents\\FFVIII-Accessibility-Mod"
    "\\FF8_OriginalPC_mod\\Logs\\screenshots";

// Source -> downsampled ratio. 1920x1080 -> 480x270, 1280x720 -> 320x180.
// Nearest-neighbor preserves saturated colors better than box-filter for
// our color-count detection use case.
static const int ROI_DOWNSAMPLE_FACTOR = 4;

// Capture only every Nth SwapBuffers call. 2 = effective 30fps at 60fps source,
// which is plenty of temporal resolution for damage-sprite first-visible
// detection (frames last ~16.6ms; we want to localize visibility to ~33ms).
static const int ROI_CAPTURE_EVERY_N = 2;

// Frame counts. Pre + main + tail are kept in a single contiguous buffer
// once an event starts. Pre-buffer is a separate rolling ring before the
// event begins.
static const int ROI_PRE_FRAMES   = 15;     // ~0.5s lead-in
static const int ROI_MAIN_FRAMES  = 240;    // ~8s active cap
static const int ROI_TAIL_FRAMES  = 15;     // ~0.5s post-flag-fall
static const int ROI_TOTAL_FRAMES = ROI_PRE_FRAMES + ROI_MAIN_FRAMES + ROI_TAIL_FRAMES;

// Address constants — anim flag and damage display.
// Same addresses already polled by PollHPChanges in battle_tts_hp.inl, but
// read independently here from the game thread so the ROI capture state
// machine doesn't need cross-thread coordination with the mod poller.
static const uint32_t ROI_ANIM_FLAG_ADDR  = 0x01D280C0;
static const uint32_t ROI_DMG_DISPLAY_ADDR = 0x01D2834A;

// v0.13.99: per-frame memory snapshot region. Covers anim flag (0x01D280C0),
// popup table (0x01D280C4), displayDamage (0x01D2834A), and sprite pool
// metadata + data (0x01D28C04 / 0x01D28C44) with slack on either side. 4KB
// per frame * ~75 frames per event = ~300KB peak, trivial overhead. The
// memdiff at finalize compares the snapshot at the yU>6 spike frame F
// against F-1, F-3, F-10 to find addresses that transitioned at exactly F
// with stable pre/post — these are candidate "digit visible" engine fields.
static const uint32_t ROI_MEMSNAP_BASE_ADDR = 0x01D28000;
static const size_t   ROI_MEMSNAP_SIZE      = 4096;  // 4KB

// ============================================================================
// Per-frame metadata
// ============================================================================

struct RoiFrame {
    uint8_t* pixels;       // RGB (24-bit), row-major, top-down. Owned by this struct.
    uint8_t* memSnap;      // v0.13.99: 4KB snapshot of battle-state region. Owned by this struct.
    DWORD    tickMs;       // GetTickCount() at capture
    uint8_t  animFlag;     // *(uint8_t*)0x01D280C0 at capture
    uint16_t damageDisp;   // *(uint16_t*)0x01D2834A at capture
    uint8_t  phaseTag;     // 0 = pre, 1 = active, 2 = tail
    int      sourceW;      // GL viewport at capture
    int      sourceH;
};

// ============================================================================
// State (game thread owns; mod thread does not touch)
// ============================================================================

enum RoiState {
    ROI_IDLE = 0,
    ROI_CAPTURING = 1,
    ROI_TAILING = 2,
};

static RoiState s_roiState = ROI_IDLE;
static int s_roiCaptureSkipCounter = 0;
static uint8_t s_roiPrevAnimFlag = 0;
static int s_roiDownsampledW = 0;
static int s_roiDownsampledH = 0;
static bool s_roiSizeKnown = false;
static int s_roiEventSerial = 0;
static int s_roiTailRemaining = 0;
static DWORD s_roiEventStartTick = 0;
static SYSTEMTIME s_roiEventStartTime = {};

// Pre-buffer: rolling ring of RoiFrame*. Frames are owned by this ring
// until an event starts, at which point ownership transfers to the active
// buffer. Empty slots are nullptr.
static RoiFrame* s_roiPreRing[ROI_PRE_FRAMES] = {};
static int s_roiPreRingHead = 0;   // next slot to write
static int s_roiPreRingCount = 0;  // how many valid frames

// Active buffer: filled during ROI_CAPTURING + ROI_TAILING. Frames owned
// here until handed off to the worker thread on finalize.
static RoiFrame* s_roiActive[ROI_MAIN_FRAMES + ROI_TAIL_FRAMES] = {};
static int s_roiActiveCount = 0;

// Frame counter for the meta.txt (separate from skip counter — counts only
// frames actually captured).
static int s_roiFramesCapturedThisEvent = 0;

// Handoff payload to the worker thread. Allocated when we finalize, freed
// by the worker after writing.
struct RoiEventPayload {
    RoiFrame* frames[ROI_TOTAL_FRAMES];  // contiguous pre + active, nulls trailing
    int       frameCount;                 // valid count
    int       downW;
    int       downH;
    char      outDir[512];
    int       eventSerial;
    DWORD     eventStartTick;
};

// Stat counters
static int s_roiTotalEventsStarted = 0;
static int s_roiTotalEventsFinalized = 0;
static int s_roiTotalAllocFailures = 0;

// ============================================================================
// v0.13.97 SHADOW SCANNER — live in-mod histogram + shadow-fire logging
// ============================================================================
//
// Computes the same color histogram in real-time as the worker thread does
// post-hoc. Logs the would-fire frame/ms to ff8_battle.log when the threshold
// is first crossed within an active damage window. Does NOT change the
// announcement path — v0.13.90 anim-flag-falls trigger continues to fire as
// before. Purpose is validation: if the shadow log shows yellow/green spikes
// firing reliably on the same frame the visible damage number renders across
// many events (and a Miss case), v0.13.98 swaps to live primary trigger.
//
// Calibration (from v0.13.96 BAT, 4 events):
//   yellow upper-region (yU > 6)  -> damage number visible to enemy
//   green full-frame    (gF > 6)  -> heal number visible on ally
//   white               -> too noisy from UI / spell menu, not used yet
//   pre-buffer baseline noise: yU max=5, gF max=0 across all 4 events

// Thresholds derived from v0.13.96 calibration. Strict greater-than.
static const int ROI_SHADOW_YELLOW_UPPER_THRESHOLD = 6;
static const int ROI_SHADOW_GREEN_FULL_THRESHOLD   = 6;

// v0.13.98: green-commit window. Green crosses threshold first means "maybe
// heal" — but spell anim sparkles can also briefly cross gF>6 on damage events.
// Wait this long for yellow to follow before committing green as a heal.
// Calibrated from v0.13.97 BAT event 2: green fired at ms=2406, yellow at
// ms=4141 (delta 1735ms). 2500ms window catches that case. Heals in BAT had
// anim-flag-fall at >6500ms so commit fires before fallback.
static const DWORD ROI_SHADOW_GREEN_COMMIT_WINDOW_MS = 2500;

// Per-event shadow-fire state. Reset at StartEvent.
static bool     s_roiShadowYellowFired       = false;
static bool     s_roiShadowGreenFired        = false;
static int      s_roiShadowYellowFireFrame   = -1;
static int      s_roiShadowGreenFireFrame    = -1;
static DWORD    s_roiShadowYellowFireMs      = 0;
static DWORD    s_roiShadowGreenFireMs       = 0;
static uint16_t s_roiShadowYellowFireDmg     = 0;
static uint16_t s_roiShadowGreenFireDmg      = 0;
static int      s_roiShadowYellowFireYU      = 0;
static int      s_roiShadowGreenFireGF       = 0;

// v0.13.98: yellow-rejected log-once flag (for status-spell visuals where yU
// crosses but dmg=0). Avoids spamming the log on every frame of the visual.
static bool     s_roiShadowYellowRejectedLogged = false;

// v0.13.98: green pending state. Green crosses threshold but we delay the
// commit decision until either yellow follows (cancel) or commit window
// expires (fire as heal).
static int      s_roiShadowGreenPendingFrame = -1;
static DWORD    s_roiShadowGreenPendingMs    = 0;
static uint16_t s_roiShadowGreenPendingDmg   = 0;
static int      s_roiShadowGreenPendingGF    = 0;
static bool     s_roiShadowGreenCancelled    = false;

// Anim-flag-fell timestamp (set at BeginTail). Used in finalize summary to
// compare shadow-fire timing against the existing v0.13.90 trigger.
static DWORD    s_roiAnimFlagFellMs          = 0;
static bool     s_roiAnimFlagFellRecorded    = false;

// v0.13.99: per-event displayDamage transition tracking. Logs every change to
// 0x01D2834A during the active window. For physical attacks, v0.13.96 BAT
// showed displayDamage flips 0→value just 2 frames before the visible-frame
// moment — if that pattern holds, the write timestamp is a tight hook for
// physical at least. For magic, displayDamage is set at popup-spawn (~3s
// before visible) and stays constant, so the write isn't useful as a
// visibility trigger but the log still tells us the timing relationship.
static uint16_t s_roiPrevDmgDisp             = 0;
static int      s_roiDmgWriteCountThisEvent  = 0;

// v0.13.99: spike-frame index recorded by the shadow scanner so the memdiff
// at finalize knows which frame to use as F. Set to s_roiShadowYellowFireFrame
// after the shadow yellow fires; -1 if no spike happened during the event.

// ============================================================================
// Helpers — frame allocation, downsampling
// ============================================================================

static RoiFrame* RoiCalib_AllocFrame(int dW, int dH)
{
    RoiFrame* f = (RoiFrame*)calloc(1, sizeof(RoiFrame));
    if (!f) return nullptr;
    f->pixels = (uint8_t*)malloc((size_t)dW * dH * 3);
    if (!f->pixels) {
        free(f);
        return nullptr;
    }
    // v0.13.99: separate allocation for memSnap so frame layout stays
    // consistent with v0.13.95–98 even if snapshot is later disabled.
    f->memSnap = (uint8_t*)malloc(ROI_MEMSNAP_SIZE);
    if (!f->memSnap) {
        free(f->pixels);
        free(f);
        return nullptr;
    }
    return f;
}

static void RoiCalib_FreeFrame(RoiFrame* f)
{
    if (!f) return;
    if (f->pixels) free(f->pixels);
    if (f->memSnap) free(f->memSnap);
    free(f);
}

// v0.13.97: compute yellow-upper and green-full counts on a downsampled
// RGB buffer. Same predicates as the worker thread's post-hoc histogram so
// shadow-fire timing matches what we'd measure offline. ~19200 pixels at
// 160x120, called every 2nd SwapBuffers (30Hz effective) -> well under 1ms.
static void RoiCalib_ComputeLiveHistogram(const RoiFrame* f,
                                          int dW, int dH,
                                          int* yU_out, int* gF_out)
{
    int yU = 0;
    int gF = 0;
    int upperYStart = 10;
    int upperYEnd   = (dH * 2) / 3;
    for (int yy = 0; yy < dH; yy++) {
        const uint8_t* row = f->pixels + (size_t)yy * dW * 3;
        bool inUpper = (yy >= upperYStart && yy <= upperYEnd);
        for (int xx = 0; xx < dW; xx++) {
            uint8_t R = row[xx * 3 + 0];
            uint8_t G = row[xx * 3 + 1];
            uint8_t B = row[xx * 3 + 2];
            // green: R<120 && G>180 && B<120 (full frame; heals)
            if (R < 120 && G > 180 && B < 120) gF++;
            // yellow: R>200 && G>180 && B<100 (upper region only; damage)
            if (inUpper && R > 200 && G > 180 && B < 100) yU++;
        }
    }
    *yU_out = yU;
    *gF_out = gF;
}

// Read framebuffer + downsample to the supplied frame's pixel buffer.
// Returns true on success. Reads from current GL context (must be called
// from the game thread inside HookedSwapBuffers).
static bool RoiCalib_ReadDownsample(RoiFrame* dst)
{
    GLint viewport[4];
    glGetIntegerv(GL_VIEWPORT, viewport);
    int srcW = viewport[2];
    int srcH = viewport[3];
    if (srcW <= 0 || srcH <= 0) return false;

    int dW = s_roiDownsampledW;
    int dH = s_roiDownsampledH;
    if (dW <= 0 || dH <= 0) return false;

    // Read full frame to a scratch buffer. ~6 MB at 1920x1080. We pay this
    // cost every Nth frame; could optimize later by using glReadPixels with
    // a stride-skip but that requires extensions and isn't portable across
    // FFNx renderer paths.
    int srcStride = ((srcW * 3 + 3) & ~3);
    size_t srcSize = (size_t)srcStride * srcH;
    uint8_t* src = (uint8_t*)malloc(srcSize);
    if (!src) return false;
    glPixelStorei(GL_PACK_ALIGNMENT, 4);
    glReadPixels(0, 0, srcW, srcH, GL_BGR_EXT, GL_UNSIGNED_BYTE, src);

    // Nearest-neighbor downsample with vertical flip (GL is bottom-up).
    // Output is top-down RGB so the saved PNG renders correctly without
    // post-processing.
    int factor = ROI_DOWNSAMPLE_FACTOR;
    for (int dy = 0; dy < dH; dy++) {
        // GL row sy_gl is bottom-up; flip to top-down for output.
        int sy_gl = (srcH - 1) - (dy * factor);
        if (sy_gl < 0) sy_gl = 0;
        if (sy_gl >= srcH) sy_gl = srcH - 1;
        const uint8_t* srcRow = src + (size_t)sy_gl * srcStride;
        uint8_t* dstRow = dst->pixels + (size_t)dy * dW * 3;
        for (int dx = 0; dx < dW; dx++) {
            int sx = dx * factor;
            if (sx >= srcW) sx = srcW - 1;
            const uint8_t* srcPx = srcRow + (size_t)sx * 3;
            // GL_BGR_EXT -> RGB swap
            dstRow[dx * 3 + 0] = srcPx[2];
            dstRow[dx * 3 + 1] = srcPx[1];
            dstRow[dx * 3 + 2] = srcPx[0];
        }
    }
    free(src);

    dst->tickMs = GetTickCount();
    dst->sourceW = srcW;
    dst->sourceH = srcH;
    __try {
        dst->animFlag = *(uint8_t*)ROI_ANIM_FLAG_ADDR;
        dst->damageDisp = *(uint16_t*)ROI_DMG_DISPLAY_ADDR;
        // v0.13.99: snapshot the battle-state region for later memdiff.
        // memcpy is safe under SEH — if the page is unreadable we'll fall
        // through to the except handler and zero everything.
        if (dst->memSnap) {
            memcpy(dst->memSnap, (const void*)ROI_MEMSNAP_BASE_ADDR, ROI_MEMSNAP_SIZE);
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        dst->animFlag = 0;
        dst->damageDisp = 0;
        if (dst->memSnap) memset(dst->memSnap, 0, ROI_MEMSNAP_SIZE);
    }
    return true;
}

// One-time discovery of downsampled dimensions. Called from
// RoiCalib_OnSwapBuffers on the first capture per process.
static void RoiCalib_LearnSize(int srcW, int srcH)
{
    if (s_roiSizeKnown) return;
    int factor = ROI_DOWNSAMPLE_FACTOR;
    int dW = srcW / factor;
    int dH = srcH / factor;
    if (dW <= 0 || dH <= 0) return;
    s_roiDownsampledW = dW;
    s_roiDownsampledH = dH;
    s_roiSizeKnown = true;
    Log::Battle("BattleTTS: [ROI-CALIB] Source %dx%d -> downsampled %dx%d "
                "(factor=%d, ~%dKB/frame, ~%dMB peak pool)",
                srcW, srcH, dW, dH, factor,
                (dW * dH * 3) / 1024,
                ((dW * dH * 3) * ROI_TOTAL_FRAMES) / (1024 * 1024));
}

// ============================================================================
// Worker thread — encode frames as PNG, write meta.txt, free everything
// ============================================================================

static DWORD WINAPI RoiCalib_WorkerThreadFunc(LPVOID lpParam)
{
    RoiEventPayload* p = (RoiEventPayload*)lpParam;
    if (!p) return 1;

    // Create the per-event subfolder.
    BOOL ok = CreateDirectoryA(p->outDir, NULL);
    DWORD err = ok ? 0 : GetLastError();
    if (!ok && err != ERROR_ALREADY_EXISTS) {
        // Try parent dir creation as a fallback (in case Logs/screenshots/ is missing)
        CreateDirectoryA(ROI_CAPTURE_PARENT_DIR, NULL);
        ok = CreateDirectoryA(p->outDir, NULL);
        err = ok ? 0 : GetLastError();
    }
    if (!ok && err != ERROR_ALREADY_EXISTS) {
        Log::Battle("BattleTTS: [ROI-CALIB] Worker: CreateDirectory FAILED dir='%s' err=%u",
                    p->outDir, err);
        // Still try to free buffers, just skip the writes.
        for (int i = 0; i < p->frameCount; i++) RoiCalib_FreeFrame(p->frames[i]);
        free(p);
        return 1;
    }

    CLSID pngClsid;
    HRESULT hrCls = CLSIDFromString(L"{557CF406-1A04-11D3-9A73-0000F81EF32E}", &pngClsid);
    if (FAILED(hrCls)) {
        Log::Battle("BattleTTS: [ROI-CALIB] Worker: CLSIDFromString FAILED hr=0x%08X", (uint32_t)hrCls);
        for (int i = 0; i < p->frameCount; i++) RoiCalib_FreeFrame(p->frames[i]);
        free(p);
        return 1;
    }

    // Open meta.txt for streaming writes.
    char metaPath[600];
    snprintf(metaPath, sizeof(metaPath), "%s\\meta.txt", p->outDir);
    FILE* meta = fopen(metaPath, "w");
    if (meta) {
        fprintf(meta, "# ROI calibration event %d\n", p->eventSerial);
        fprintf(meta, "# downsampled_size %dx%d\n", p->downW, p->downH);
        fprintf(meta, "# event_start_tick_ms %u\n", (unsigned)p->eventStartTick);
        fprintf(meta, "# frames_captured %d\n", p->frameCount);
        fprintf(meta, "#\n");
        fprintf(meta, "# v0.13.96: per-frame color histogram for ROI calibration.\n");
        fprintf(meta, "# Pixels are RGB in [0..255]; counts are pixels matching the color predicate.\n");
        fprintf(meta, "# upper region = y in [10, %d] (top ~2/3 of frame, excludes status bar at bottom).\n",
                (p->downH * 2) / 3);
        fprintf(meta, "# yellow = R>200 && G>180 && B<100  (damage-to-enemy color)\n");
        fprintf(meta, "# white  = R>240 && G>240 && B>240  (damage-to-ally / Miss text color)\n");
        fprintf(meta, "# green  = R<120 && G>180 && B<120  (heal text color)\n");
        fprintf(meta, "# orange = R>220 && G>120 && G<200 && B<100  (loose damage; may also catch enemy sprites)\n");
        fprintf(meta, "#\n");
        fprintf(meta, "# columns: frameIdx msFromEventStart animFlag damageDisplay phaseTag srcW srcH "
                     "|| yU wU gU oU yF wF gF oF\n");
        fprintf(meta, "# (U=upper region, F=full frame; y=yellow w=white g=green o=orange)\n");
        fprintf(meta, "# phaseTag: 0=pre, 1=active, 2=tail\n");
    } else {
        Log::Battle("BattleTTS: [ROI-CALIB] Worker: meta.txt fopen FAILED errno=%d", errno);
    }

    // First-spike trackers per (color, region). Threshold = 5 pixels.
    static const int SPIKE_THRESHOLD = 5;
    int firstYellowUpper = -1, firstWhiteUpper = -1, firstGreenUpper = -1, firstOrangeUpper = -1;
    int firstYellowFull  = -1, firstWhiteFull  = -1, firstGreenFull  = -1, firstOrangeFull  = -1;
    DWORD firstYellowUpperMs = 0, firstWhiteUpperMs = 0, firstGreenUpperMs = 0, firstOrangeUpperMs = 0;
    DWORD firstYellowFullMs = 0,  firstWhiteFullMs = 0,  firstGreenFullMs = 0,  firstOrangeFullMs = 0;

    int written = 0;
    int writeFailed = 0;
    int upperYStart = 10;
    int upperYEnd   = (p->downH * 2) / 3;  // exclude bottom 1/3 (status / menus)

    for (int i = 0; i < p->frameCount; i++) {
        RoiFrame* f = p->frames[i];
        if (!f || !f->pixels) continue;

        int dW = p->downW;
        int dH = p->downH;

        // ---- v0.13.96: per-frame color histogram on the RGB buffer ----
        int yU = 0, wU = 0, gU = 0, oU = 0;
        int yF = 0, wF = 0, gF = 0, oF = 0;
        for (int yy = 0; yy < dH; yy++) {
            const uint8_t* row = f->pixels + (size_t)yy * dW * 3;
            bool inUpper = (yy >= upperYStart && yy <= upperYEnd);
            for (int xx = 0; xx < dW; xx++) {
                uint8_t R = row[xx * 3 + 0];
                uint8_t G = row[xx * 3 + 1];
                uint8_t B = row[xx * 3 + 2];
                bool yel = (R > 200 && G > 180 && B < 100);
                bool whi = (R > 240 && G > 240 && B > 240);
                bool grn = (R < 120 && G > 180 && B < 120);
                bool org = (R > 220 && G > 120 && G < 200 && B < 100);
                if (yel) yF++;
                if (whi) wF++;
                if (grn) gF++;
                if (org) oF++;
                if (inUpper) {
                    if (yel) yU++;
                    if (whi) wU++;
                    if (grn) gU++;
                    if (org) oU++;
                }
            }
        }

        // First-spike tracking. Use msFromEventStart for the spike timestamp.
        DWORD relMs = (f->tickMs >= p->eventStartTick) ? (f->tickMs - p->eventStartTick) : 0;
        if (firstYellowUpper < 0 && yU > SPIKE_THRESHOLD) { firstYellowUpper = i; firstYellowUpperMs = relMs; }
        if (firstWhiteUpper  < 0 && wU > SPIKE_THRESHOLD) { firstWhiteUpper  = i; firstWhiteUpperMs  = relMs; }
        if (firstGreenUpper  < 0 && gU > SPIKE_THRESHOLD) { firstGreenUpper  = i; firstGreenUpperMs  = relMs; }
        if (firstOrangeUpper < 0 && oU > SPIKE_THRESHOLD) { firstOrangeUpper = i; firstOrangeUpperMs = relMs; }
        if (firstYellowFull  < 0 && yF > SPIKE_THRESHOLD) { firstYellowFull  = i; firstYellowFullMs  = relMs; }
        if (firstWhiteFull   < 0 && wF > SPIKE_THRESHOLD) { firstWhiteFull   = i; firstWhiteFullMs   = relMs; }
        if (firstGreenFull   < 0 && gF > SPIKE_THRESHOLD) { firstGreenFull   = i; firstGreenFullMs   = relMs; }
        if (firstOrangeFull  < 0 && oF > SPIKE_THRESHOLD) { firstOrangeFull  = i; firstOrangeFullMs  = relMs; }

        // ---- PNG encode ----
        // GDI+ Bitmap from raw pixel data. PixelFormat24bppRGB expects BGR
        // byte order in memory (GDI+ historical quirk). Our buffer is RGB
        // because we BGR->RGB swapped during downsample, so we need to swap
        // back here. Build a BGR copy on the stack? Simpler: use a wrapper
        // that swaps inline. Fastest: just re-swap the existing buffer in
        // place since we're done with it after this anyway.
        int stride = ((dW * 3 + 3) & ~3);
        // GDI+ requires stride alignment. Allocate a properly-strided BGR
        // buffer for the bitmap to consume.
        uint8_t* gdiBuf = (uint8_t*)malloc((size_t)stride * dH);
        if (!gdiBuf) { writeFailed++;
        } else {
            for (int y = 0; y < dH; y++) {
                const uint8_t* srcRow = f->pixels + (size_t)y * dW * 3;
                uint8_t* dstRow = gdiBuf + (size_t)y * stride;
                for (int x = 0; x < dW; x++) {
                    // RGB -> BGR
                    dstRow[x * 3 + 0] = srcRow[x * 3 + 2];
                    dstRow[x * 3 + 1] = srcRow[x * 3 + 1];
                    dstRow[x * 3 + 2] = srcRow[x * 3 + 0];
                }
            }

            Gdiplus::Bitmap* bmp = new Gdiplus::Bitmap(
                dW, dH, stride, PixelFormat24bppRGB, gdiBuf);
            if (bmp && bmp->GetLastStatus() == Gdiplus::Ok) {
                wchar_t wPath[700];
                char path[600];
                snprintf(path, sizeof(path), "%s\\frame%03d.png", p->outDir, i);
                MultiByteToWideChar(CP_UTF8, 0, path, -1, wPath, 700);
                Gdiplus::Status s = bmp->Save(wPath, &pngClsid, NULL);
                if (s == Gdiplus::Ok) {
                    written++;
                } else {
                    writeFailed++;
                }
            } else {
                writeFailed++;
            }
            if (bmp) delete bmp;
            free(gdiBuf);
        }

        // Write meta line for this frame.
        if (meta) {
            fprintf(meta,
                    "%d %u %u %u %u %d %d || %d %d %d %d %d %d %d %d\n",
                    i,
                    (unsigned)relMs,
                    (unsigned)f->animFlag,
                    (unsigned)f->damageDisp,
                    (unsigned)f->phaseTag,
                    f->sourceW, f->sourceH,
                    yU, wU, gU, oU,
                    yF, wF, gF, oF);
        }
    }

    // ---- v0.13.96: per-event spike summary at the bottom of meta.txt ----
    if (meta) {
        fprintf(meta, "\n# === v0.13.96 first-spike summary (count > %d in upper region) ===\n",
                SPIKE_THRESHOLD);
        fprintf(meta, "# first_yellow_upper_spike: frame=%d msFromStart=%u\n",
                firstYellowUpper, (unsigned)firstYellowUpperMs);
        fprintf(meta, "# first_white_upper_spike : frame=%d msFromStart=%u\n",
                firstWhiteUpper,  (unsigned)firstWhiteUpperMs);
        fprintf(meta, "# first_green_upper_spike : frame=%d msFromStart=%u\n",
                firstGreenUpper,  (unsigned)firstGreenUpperMs);
        fprintf(meta, "# first_orange_upper_spike: frame=%d msFromStart=%u\n",
                firstOrangeUpper, (unsigned)firstOrangeUpperMs);
        fprintf(meta, "# first_yellow_full_spike : frame=%d msFromStart=%u\n",
                firstYellowFull,  (unsigned)firstYellowFullMs);
        fprintf(meta, "# first_white_full_spike  : frame=%d msFromStart=%u\n",
                firstWhiteFull,   (unsigned)firstWhiteFullMs);
        fprintf(meta, "# first_green_full_spike  : frame=%d msFromStart=%u\n",
                firstGreenFull,   (unsigned)firstGreenFullMs);
        fprintf(meta, "# first_orange_full_spike : frame=%d msFromStart=%u\n",
                firstOrangeFull,  (unsigned)firstOrangeFullMs);
        fclose(meta);
    }

    Log::Battle("BattleTTS: [ROI-CALIB] Worker: event %d wrote %d/%d frames "
                "(failed=%d) firstYellowUpper=frame%d/%ums dir='%s'",
                p->eventSerial, written, p->frameCount, writeFailed,
                firstYellowUpper, (unsigned)firstYellowUpperMs, p->outDir);

    // Free all frame data.
    for (int i = 0; i < p->frameCount; i++) RoiCalib_FreeFrame(p->frames[i]);
    free(p);
    return 0;
}

// ============================================================================
// State machine
// ============================================================================

static void RoiCalib_StartEvent(uint8_t firstAnimFlagValue)
{
    // Build per-event output dir using the time the event started.
    GetLocalTime(&s_roiEventStartTime);
    s_roiEventStartTick = GetTickCount();
    s_roiEventSerial++;
    s_roiTotalEventsStarted++;

    s_roiState = ROI_CAPTURING;
    s_roiActiveCount = 0;
    s_roiTailRemaining = ROI_TAIL_FRAMES;
    s_roiFramesCapturedThisEvent = 0;

    // v0.13.97 SHADOW SCANNER: reset shadow-fire state for this event.
    s_roiShadowYellowFired       = false;
    s_roiShadowGreenFired        = false;
    s_roiShadowYellowFireFrame   = -1;
    s_roiShadowGreenFireFrame    = -1;
    s_roiShadowYellowFireMs      = 0;
    s_roiShadowGreenFireMs       = 0;
    s_roiShadowYellowFireDmg     = 0;
    s_roiShadowGreenFireDmg      = 0;
    s_roiShadowYellowFireYU      = 0;
    s_roiShadowGreenFireGF       = 0;
    // v0.13.98: reset gating state.
    s_roiShadowYellowRejectedLogged = false;
    s_roiShadowGreenPendingFrame    = -1;
    s_roiShadowGreenPendingMs       = 0;
    s_roiShadowGreenPendingDmg      = 0;
    s_roiShadowGreenPendingGF       = 0;
    s_roiShadowGreenCancelled       = false;
    s_roiAnimFlagFellMs          = 0;
    s_roiAnimFlagFellRecorded    = false;

    // v0.13.99: reset per-event displayDamage tracking. Capture current value
    // as baseline so we only log subsequent changes within this event.
    __try {
        s_roiPrevDmgDisp = *(uint16_t*)ROI_DMG_DISPLAY_ADDR;
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        s_roiPrevDmgDisp = 0;
    }
    s_roiDmgWriteCountThisEvent = 0;

    // Drain pre-buffer into active buffer in temporal order.
    // Ring layout: oldest is at (head - count) mod size.
    int n = s_roiPreRingCount;
    int idx = (s_roiPreRingHead - n + ROI_PRE_FRAMES) % ROI_PRE_FRAMES;
    int copied = 0;
    for (int i = 0; i < n && s_roiActiveCount < (ROI_MAIN_FRAMES + ROI_TAIL_FRAMES); i++) {
        RoiFrame* f = s_roiPreRing[idx];
        s_roiPreRing[idx] = nullptr;  // transfer ownership
        if (f) {
            f->phaseTag = 0;  // pre
            s_roiActive[s_roiActiveCount++] = f;
            s_roiFramesCapturedThisEvent++;
            copied++;
        }
        idx = (idx + 1) % ROI_PRE_FRAMES;
    }
    s_roiPreRingHead = 0;
    s_roiPreRingCount = 0;

    Log::Battle("BattleTTS: [ROI-CALIB] Event %d STARTED "
                "(prebuf=%d, animFlag=0x%02X, tick=%u)",
                s_roiEventSerial, copied,
                (unsigned)firstAnimFlagValue,
                (unsigned)s_roiEventStartTick);
}

static void RoiCalib_BeginTail()
{
    if (s_roiState != ROI_CAPTURING) return;
    s_roiState = ROI_TAILING;

    // v0.13.97 SHADOW SCANNER: record anim-flag-fall ms for finalize comparison.
    if (!s_roiAnimFlagFellRecorded) {
        DWORD now = GetTickCount();
        s_roiAnimFlagFellMs = (now >= s_roiEventStartTick) ? (now - s_roiEventStartTick) : 0;
        s_roiAnimFlagFellRecorded = true;
    }

    Log::Battle("BattleTTS: [ROI-CALIB] Event %d entering TAIL "
                "(activeCount=%d, tailFrames=%d)",
                s_roiEventSerial, s_roiActiveCount, ROI_TAIL_FRAMES);
}

// v0.13.99: at finalize, identify the yellow-spike frame F and diff its
// memory snapshot against earlier frames to find addresses that transitioned
// at exactly F with stable pre/post. The output is a compact log of candidate
// "digit visible" engine fields. Caller passes the spike frame index in
// s_roiActive[]; if it's invalid (-1 or out of range), function logs
// "no spike" and returns.
//
// Stability test:
//   pre  = values at F-10, F-5, F-1 are all equal -> V1
//   post = values at F, F+1, F+3 are all equal -> V2  (or just F if no later frames)
//   transition = V1 != V2
// Reports (offset, V1, V2) for every byte that satisfies all three. Caps at
// MEMDIFF_MAX_REPORT to avoid log flooding; logs total candidate count.
static void RoiCalib_DoMemDiff(int spikeFrameIdx)
{
    static const int MEMDIFF_MAX_REPORT = 80;

    if (spikeFrameIdx < 0) {
        Log::Battle("BattleTTS: [ROI-MEMDIFF] event %d: no yellow spike this event, skipping memdiff",
                    s_roiEventSerial);
        return;
    }
    if (spikeFrameIdx >= s_roiActiveCount) {
        Log::Battle("BattleTTS: [ROI-MEMDIFF] event %d: spikeFrameIdx=%d out of range (active=%d)",
                    s_roiEventSerial, spikeFrameIdx, s_roiActiveCount);
        return;
    }
    if (!s_roiActive[spikeFrameIdx] || !s_roiActive[spikeFrameIdx]->memSnap) {
        Log::Battle("BattleTTS: [ROI-MEMDIFF] event %d: spike frame %d has null snapshot",
                    s_roiEventSerial, spikeFrameIdx);
        return;
    }

    // Frame indices we want to sample. Out-of-range → -1 sentinel; the byte
    // loop substitutes the spike-frame value for missing pre-samples and
    // skips post-stability checks if any post-sample is missing.
    int idxF   = spikeFrameIdx;
    int idxM10 = spikeFrameIdx - 10;
    int idxM5  = spikeFrameIdx - 5;
    int idxM1  = spikeFrameIdx - 1;
    int idxP1  = spikeFrameIdx + 1;
    int idxP3  = spikeFrameIdx + 3;
    if (idxM10 < 0) idxM10 = -1;
    if (idxM5  < 0) idxM5  = -1;
    if (idxM1  < 0) idxM1  = -1;
    if (idxP1  >= s_roiActiveCount) idxP1 = -1;
    if (idxP3  >= s_roiActiveCount) idxP3 = -1;

    auto getSnap = [](int idx) -> const uint8_t* {
        if (idx < 0 || idx >= s_roiActiveCount) return nullptr;
        if (!s_roiActive[idx]) return nullptr;
        return s_roiActive[idx]->memSnap;
    };
    const uint8_t* sF   = getSnap(idxF);
    const uint8_t* sM10 = getSnap(idxM10);
    const uint8_t* sM5  = getSnap(idxM5);
    const uint8_t* sM1  = getSnap(idxM1);
    const uint8_t* sP1  = getSnap(idxP1);
    const uint8_t* sP3  = getSnap(idxP3);

    if (!sF) return;  // already logged above; defensive

    Log::Battle("BattleTTS: [ROI-MEMDIFF] event %d START spikeFrame=%d "
                "sampleFrames F=%d/M1=%d/M5=%d/M10=%d/P1=%d/P3=%d "
                "region=0x%08X..0x%08X size=%u",
                s_roiEventSerial, spikeFrameIdx,
                idxF, idxM1, idxM5, idxM10, idxP1, idxP3,
                ROI_MEMSNAP_BASE_ADDR,
                (uint32_t)(ROI_MEMSNAP_BASE_ADDR + ROI_MEMSNAP_SIZE),
                (unsigned)ROI_MEMSNAP_SIZE);

    int totalCandidates = 0;
    int reported = 0;
    for (size_t off = 0; off < ROI_MEMSNAP_SIZE; off++) {
        uint8_t vF = sF[off];

        // Pre-stability: every available pre-sample must equal each other.
        // If we have at least one pre-sample, use it as baseline V1; else
        // fall through to comparing only F vs post (less robust but still
        // useful at event start where no F-5/F-10 exist).
        uint8_t vPre = 0;
        bool havePre = false;
        bool preStable = true;
        if (sM10) { vPre = sM10[off]; havePre = true; }
        if (sM5)  {
            uint8_t v = sM5[off];
            if (havePre && v != vPre) preStable = false;
            else { vPre = v; havePre = true; }
        }
        if (sM1) {
            uint8_t v = sM1[off];
            if (havePre && v != vPre) preStable = false;
            else { vPre = v; havePre = true; }
        }
        if (!havePre || !preStable) continue;

        // Post-stability: F, F+1, F+3 must all equal each other (call V2).
        // Tolerate missing P1/P3 (event ended early) but require F itself.
        uint8_t vPost = vF;
        if (sP1 && sP1[off] != vPost) continue;
        if (sP3 && sP3[off] != vPost) continue;

        // Transition: V1 != V2.
        if (vPre == vPost) continue;

        totalCandidates++;
        if (reported < MEMDIFF_MAX_REPORT) {
            reported++;
            Log::Battle("BattleTTS: [ROI-MEMDIFF] event %d "
                        "addr=0x%08X (off=+0x%03X) pre=0x%02X(%u) post=0x%02X(%u) "
                        "deltaSigned=%d",
                        s_roiEventSerial,
                        (uint32_t)(ROI_MEMSNAP_BASE_ADDR + off),
                        (unsigned)off,
                        (unsigned)vPre, (unsigned)vPre,
                        (unsigned)vPost, (unsigned)vPost,
                        (int)vPost - (int)vPre);
        }
    }

    Log::Battle("BattleTTS: [ROI-MEMDIFF] event %d END candidates=%d reported=%d (cap=%d)",
                s_roiEventSerial, totalCandidates, reported, MEMDIFF_MAX_REPORT);
}

static void RoiCalib_FinalizeEvent()
{
    if (s_roiActiveCount == 0) {
        s_roiState = ROI_IDLE;
        return;
    }

    // v0.13.99: run memdiff BEFORE handing frames off to the worker thread.
    // The diff reads from s_roiActive[]; once we transfer ownership those
    // pointers are nulled. The shadow scanner's yellow-fire frame is our
    // F (the visible-frame moment we want to characterize).
    RoiCalib_DoMemDiff(s_roiShadowYellowFireFrame);

    RoiEventPayload* p = (RoiEventPayload*)calloc(1, sizeof(RoiEventPayload));
    if (!p) {
        Log::Battle("BattleTTS: [ROI-CALIB] Finalize: payload alloc FAILED, dropping event");
        for (int i = 0; i < s_roiActiveCount; i++) {
            RoiCalib_FreeFrame(s_roiActive[i]);
            s_roiActive[i] = nullptr;
        }
        s_roiActiveCount = 0;
        s_roiState = ROI_IDLE;
        return;
    }

    p->frameCount = s_roiActiveCount;
    p->downW = s_roiDownsampledW;
    p->downH = s_roiDownsampledH;
    p->eventSerial = s_roiEventSerial;
    p->eventStartTick = s_roiEventStartTick;
    snprintf(p->outDir, sizeof(p->outDir),
             "%s\\roi_calib_%02d%02d%02d_%03d_evt%03d",
             ROI_CAPTURE_PARENT_DIR,
             s_roiEventStartTime.wHour, s_roiEventStartTime.wMinute,
             s_roiEventStartTime.wSecond, s_roiEventStartTime.wMilliseconds,
             s_roiEventSerial);

    // Transfer ownership of frame pointers to the payload.
    for (int i = 0; i < s_roiActiveCount && i < ROI_TOTAL_FRAMES; i++) {
        p->frames[i] = s_roiActive[i];
        s_roiActive[i] = nullptr;
    }
    s_roiActiveCount = 0;
    s_roiState = ROI_IDLE;

    // Spawn fire-and-forget worker. Worker frees the payload + frames.
    HANDLE h = CreateThread(NULL, 0, RoiCalib_WorkerThreadFunc, p, 0, NULL);
    if (h) {
        CloseHandle(h);  // we don't need to wait
        s_roiTotalEventsFinalized++;
        Log::Battle("BattleTTS: [ROI-CALIB] Event %d FINALIZED handed to worker "
                    "(frames=%d, dir='%s', total events finalized=%d)",
                    p->eventSerial, p->frameCount, p->outDir,
                    s_roiTotalEventsFinalized);
    } else {
        Log::Battle("BattleTTS: [ROI-CALIB] CreateThread FAILED err=%u, freeing inline",
                    GetLastError());
        for (int i = 0; i < p->frameCount; i++) RoiCalib_FreeFrame(p->frames[i]);
        free(p);
    }

    // v0.13.97 SHADOW SCANNER: per-event comparison summary. Compares
    // shadow-fire timing (yellow + green) against the v0.13.90 trigger
    // moment (anim-flag fall). Negative delta = shadow would fire EARLIER
    // than current trigger, which is the timing improvement we're after.
    //
    // v0.13.98: also commit any still-pending green at event end. If green
    // was pending and event ended without a yellow follow-up, that's a real
    // heal that the commit-window timer didn't get to fire (e.g., short
    // event with anim-flag falling before commit window expired).
    if (!s_roiShadowGreenFired && !s_roiShadowGreenCancelled &&
        s_roiShadowGreenPendingFrame >= 0)
    {
        s_roiShadowGreenFired = true;
        s_roiShadowGreenFireFrame = s_roiShadowGreenPendingFrame;
        s_roiShadowGreenFireMs = s_roiShadowGreenPendingMs;
        s_roiShadowGreenFireDmg = s_roiShadowGreenPendingDmg;
        s_roiShadowGreenFireGF = s_roiShadowGreenPendingGF;
        Log::Battle("BattleTTS: [ROI-LIVE-SHADOW] event %d GREEN-COMMITTED-AT-EVENT-END "
                    "(pending since frame=%d ms=%u gF=%d dmg=%u, no yellow follow-up "
                    "— commit-window did not expire before event ended)",
                    s_roiEventSerial,
                    s_roiShadowGreenPendingFrame,
                    (unsigned)s_roiShadowGreenPendingMs,
                    s_roiShadowGreenPendingGF,
                    (unsigned)s_roiShadowGreenPendingDmg);
    }

    int yelLeadMs = (s_roiShadowYellowFired && s_roiAnimFlagFellRecorded)
                        ? (int)s_roiAnimFlagFellMs - (int)s_roiShadowYellowFireMs
                        : 0;
    int grnLeadMs = (s_roiShadowGreenFired && s_roiAnimFlagFellRecorded)
                        ? (int)s_roiAnimFlagFellMs - (int)s_roiShadowGreenFireMs
                        : 0;
    Log::Battle("BattleTTS: [ROI-LIVE-SHADOW] event %d SUMMARY: "
                "yellow={fired=%d frame=%d ms=%u yU=%d dmg=%u rejectedDmg0=%d} "
                "green={fired=%d frame=%d ms=%u gF=%d dmg=%u cancelled=%d pendingFrame=%d} "
                "animFlagFell={recorded=%d ms=%u} "
                "yellowLeadVsAnimFlag=%dms greenLeadVsAnimFlag=%dms",
                s_roiEventSerial,
                s_roiShadowYellowFired ? 1 : 0,
                s_roiShadowYellowFireFrame,
                (unsigned)s_roiShadowYellowFireMs,
                s_roiShadowYellowFireYU,
                (unsigned)s_roiShadowYellowFireDmg,
                s_roiShadowYellowRejectedLogged ? 1 : 0,
                s_roiShadowGreenFired ? 1 : 0,
                s_roiShadowGreenFireFrame,
                (unsigned)s_roiShadowGreenFireMs,
                s_roiShadowGreenFireGF,
                (unsigned)s_roiShadowGreenFireDmg,
                s_roiShadowGreenCancelled ? 1 : 0,
                s_roiShadowGreenPendingFrame,
                s_roiAnimFlagFellRecorded ? 1 : 0,
                (unsigned)s_roiAnimFlagFellMs,
                yelLeadMs, grnLeadMs);
}

// ============================================================================
// Public entry — called per-frame from HookedSwapBuffers (game thread)
// ============================================================================

static void RoiCalib_OnSwapBuffers()
{
    // Bail unless we're in a battle. The anim flag at 0x01D280C0 is only
    // meaningful during battle mode; reading it outside battle could pick
    // up unrelated bytes. (Use BattleTTS:: namespace's s_inBattle which
    // tracks game mode == 3 transitions.)
    if (!s_inBattle) {
        // Force-finalize any in-flight event on battle exit (loss, flee, etc.)
        if (s_roiState != ROI_IDLE && s_roiActiveCount > 0) {
            Log::Battle("BattleTTS: [ROI-CALIB] Battle exit mid-event, finalizing partial");
            RoiCalib_FinalizeEvent();
        }
        // Drain pre-buffer too — they're stale outside battle.
        for (int i = 0; i < ROI_PRE_FRAMES; i++) {
            RoiCalib_FreeFrame(s_roiPreRing[i]);
            s_roiPreRing[i] = nullptr;
        }
        s_roiPreRingHead = 0;
        s_roiPreRingCount = 0;
        return;
    }

    // Subsample.
    s_roiCaptureSkipCounter++;
    if (s_roiCaptureSkipCounter < ROI_CAPTURE_EVERY_N) return;
    s_roiCaptureSkipCounter = 0;

    // Discover downsampled dimensions on first frame.
    if (!s_roiSizeKnown) {
        GLint vp[4];
        glGetIntegerv(GL_VIEWPORT, vp);
        if (vp[2] > 0 && vp[3] > 0) RoiCalib_LearnSize(vp[2], vp[3]);
        if (!s_roiSizeKnown) return;  // can't capture without sizes
    }

    // Read anim flag.
    uint8_t curAnim = 0;
    __try { curAnim = *(uint8_t*)ROI_ANIM_FLAG_ADDR; }
    __except(EXCEPTION_EXECUTE_HANDLER) { curAnim = 0; }

    // Allocate this frame's storage. If alloc fails, skip the frame but
    // keep the state machine alive.
    RoiFrame* f = RoiCalib_AllocFrame(s_roiDownsampledW, s_roiDownsampledH);
    if (!f) {
        s_roiTotalAllocFailures++;
        if (s_roiTotalAllocFailures % 30 == 1) {
            Log::Battle("BattleTTS: [ROI-CALIB] alloc fail #%d (heap pressure?)",
                        s_roiTotalAllocFailures);
        }
        s_roiPrevAnimFlag = curAnim;
        return;
    }
    if (!RoiCalib_ReadDownsample(f)) {
        RoiCalib_FreeFrame(f);
        s_roiPrevAnimFlag = curAnim;
        return;
    }

    // v0.13.97 SHADOW SCANNER: compute live histogram on this frame BEFORE
    // the state machine runs. Cheap (~<1ms). We use the values after the
    // state machine has placed the frame, when we know the current phase.
    int liveYU = 0;
    int liveGF = 0;
    RoiCalib_ComputeLiveHistogram(f, s_roiDownsampledW, s_roiDownsampledH,
                                  &liveYU, &liveGF);
    uint16_t curDmgDisp = f->damageDisp;

    // v0.13.99: displayDamage transition tracking. Log every value change at
    // 0x01D2834A during an active event so we can correlate the write moment
    // against the yellow shadow-fire moment in post-hoc analysis. Sample taken
    // BEFORE the state-machine push so the frame index in the log matches
    // s_roiFramesCapturedThisEvent at the next-pushed slot. Only log during
    // active capture window (CAPTURING or TAILING).
    if ((s_roiState == ROI_CAPTURING || s_roiState == ROI_TAILING) &&
        curDmgDisp != s_roiPrevDmgDisp)
    {
        DWORD nowMs = GetTickCount();
        DWORD relMs = (nowMs >= s_roiEventStartTick) ? (nowMs - s_roiEventStartTick) : 0;
        s_roiDmgWriteCountThisEvent++;
        Log::Battle("BattleTTS: [DMG-WRITE] event %d frame=%d ms=%u "
                    "oldDmg=%u newDmg=%u (write #%d this event)",
                    s_roiEventSerial,
                    s_roiFramesCapturedThisEvent,
                    (unsigned)relMs,
                    (unsigned)s_roiPrevDmgDisp,
                    (unsigned)curDmgDisp,
                    s_roiDmgWriteCountThisEvent);
    }
    s_roiPrevDmgDisp = curDmgDisp;

    // ---- State machine ----
    bool flagRose = (s_roiPrevAnimFlag == 0 && curAnim != 0);
    bool flagFell = (s_roiPrevAnimFlag != 0 && curAnim == 0);

    if (s_roiState == ROI_IDLE) {
        if (flagRose) {
            RoiCalib_StartEvent(curAnim);
            // Place the trigger frame as the first active frame.
            f->phaseTag = 1;
            if (s_roiActiveCount < (ROI_MAIN_FRAMES + ROI_TAIL_FRAMES)) {
                s_roiActive[s_roiActiveCount++] = f;
                s_roiFramesCapturedThisEvent++;
                f = nullptr;
            } else {
                RoiCalib_FreeFrame(f);
                f = nullptr;
            }
        } else {
            // Add to pre-ring.
            f->phaseTag = 0;
            int slot = s_roiPreRingHead;
            RoiCalib_FreeFrame(s_roiPreRing[slot]);  // overwrite oldest
            s_roiPreRing[slot] = f;
            s_roiPreRingHead = (slot + 1) % ROI_PRE_FRAMES;
            if (s_roiPreRingCount < ROI_PRE_FRAMES) s_roiPreRingCount++;
            f = nullptr;
        }
    } else if (s_roiState == ROI_CAPTURING) {
        // Always append to active buffer until cap reached.
        if (s_roiActiveCount < ROI_MAIN_FRAMES) {
            f->phaseTag = 1;
            s_roiActive[s_roiActiveCount++] = f;
            s_roiFramesCapturedThisEvent++;
            f = nullptr;
        } else {
            // Cap reached — drop the frame but stay in CAPTURING. We rely
            // on the anim flag falling to exit; for very long animations
            // we'll simply have a fixed-window capture.
            RoiCalib_FreeFrame(f);
            f = nullptr;
            static int s_capLogCount = 0;
            if (s_capLogCount < 3) {
                s_capLogCount++;
                Log::Battle("BattleTTS: [ROI-CALIB] Event %d hit main cap (%d), "
                            "still in CAPTURING — frames past cap dropped",
                            s_roiEventSerial, ROI_MAIN_FRAMES);
            }
        }
        if (flagFell) {
            RoiCalib_BeginTail();
        }
    } else if (s_roiState == ROI_TAILING) {
        // Append tail frames.
        if (s_roiActiveCount < (ROI_MAIN_FRAMES + ROI_TAIL_FRAMES) &&
            s_roiTailRemaining > 0) {
            f->phaseTag = 2;
            s_roiActive[s_roiActiveCount++] = f;
            s_roiFramesCapturedThisEvent++;
            s_roiTailRemaining--;
            f = nullptr;
        } else {
            RoiCalib_FreeFrame(f);
            f = nullptr;
        }
        if (s_roiTailRemaining <= 0) {
            RoiCalib_FinalizeEvent();
        } else if (flagRose) {
            // Rare: anim flag bounced back up during tail. Treat the tail
            // as preface for a new event by finalizing current and starting
            // fresh. Simplest defensive handling.
            Log::Battle("BattleTTS: [ROI-CALIB] Event %d: anim flag rose during tail — "
                        "finalizing and chaining",
                        s_roiEventSerial);
            RoiCalib_FinalizeEvent();
            RoiCalib_StartEvent(curAnim);
        }
    }

    // If we didn't take ownership (any branch above that fell through), free.
    if (f) RoiCalib_FreeFrame(f);

    // v0.13.97 SHADOW SCANNER: evaluate would-fire conditions on this frame.
    // We only fire during the active damage window. ROI_CAPTURING means anim
    // flag is currently 1 (true active). We also check ROI_TAILING for the
    // edge case where the threshold first crosses on the very frame the anim
    // flag fell — that's still a valid shadow-fire (would have fired at the
    // same time as v0.13.90, lead delta = 0).
    //
    // v0.13.98 GATING: yellow gated on `dmg>0` (rejects status-spell visuals
    // like Sleep where yU crosses but no damage number renders). Green uses a
    // pending→commit pattern: green crosses gates -> mark pending -> wait
    // ROI_SHADOW_GREEN_COMMIT_WINDOW_MS for yellow to follow. If yellow
    // follows, cancel green (damage event with green spell anim). If timer
    // expires without yellow, commit green as a heal.
    if (s_roiState == ROI_CAPTURING || s_roiState == ROI_TAILING) {
        DWORD nowMs = GetTickCount();
        DWORD relMs = (nowMs >= s_roiEventStartTick) ? (nowMs - s_roiEventStartTick) : 0;
        // Frame index in the active buffer: just-pushed frame is at
        // s_roiFramesCapturedThisEvent - 1 (we incremented after push).
        int curFrameIdx = s_roiFramesCapturedThisEvent > 0 ? s_roiFramesCapturedThisEvent - 1 : 0;

        // ----- YELLOW gate: dmg>0 required to fire -----
        if (!s_roiShadowYellowFired && liveYU > ROI_SHADOW_YELLOW_UPPER_THRESHOLD) {
            if (curDmgDisp > 0) {
                s_roiShadowYellowFired = true;
                s_roiShadowYellowFireFrame = curFrameIdx;
                s_roiShadowYellowFireMs = relMs;
                s_roiShadowYellowFireDmg = curDmgDisp;
                s_roiShadowYellowFireYU = liveYU;
                Log::Battle("BattleTTS: [ROI-LIVE-SHADOW] event %d YELLOW spike "
                            "frame=%d ms=%u yU=%d dmg=%u (would-fire — damage to enemy)",
                            s_roiEventSerial, curFrameIdx,
                            (unsigned)relMs, liveYU, (unsigned)curDmgDisp);

                // v0.13.98: cancel pending green (damage event with green spell anim).
                if (s_roiShadowGreenPendingFrame >= 0 && !s_roiShadowGreenFired) {
                    Log::Battle("BattleTTS: [ROI-LIVE-SHADOW] event %d GREEN-CANCELLED "
                                "(yellow followed pending green at frame=%d ms=%u gF=%d "
                                "— was a damage event with green spell anim, not a heal)",
                                s_roiEventSerial,
                                s_roiShadowGreenPendingFrame,
                                (unsigned)s_roiShadowGreenPendingMs,
                                s_roiShadowGreenPendingGF);
                    s_roiShadowGreenCancelled = true;
                    s_roiShadowGreenPendingFrame = -1;
                }
            } else if (!s_roiShadowYellowRejectedLogged) {
                // v0.13.98: yU crossed but dmg=0 — likely status-spell visual
                // (Sleep Z's, Confuse swirl, Berserk red flash, etc.). Log
                // once per event to avoid spam, do NOT set fired flag (allow
                // a real damage number to fire later if dmg becomes >0).
                s_roiShadowYellowRejectedLogged = true;
                Log::Battle("BattleTTS: [ROI-LIVE-SHADOW] event %d YELLOW-REJECTED "
                            "frame=%d ms=%u yU=%d dmg=0 (status-spell visual? "
                            "gating on dmg>0; allowing later real damage to fire)",
                            s_roiEventSerial, curFrameIdx,
                            (unsigned)relMs, liveYU);
            }
        }

        // ----- GREEN gate: dmg>0 required, then pending→commit/cancel -----
        if (!s_roiShadowGreenFired && !s_roiShadowGreenCancelled) {
            // Check for commit time (pending was set, window has elapsed).
            if (s_roiShadowGreenPendingFrame >= 0 &&
                relMs >= s_roiShadowGreenPendingMs + ROI_SHADOW_GREEN_COMMIT_WINDOW_MS)
            {
                s_roiShadowGreenFired = true;
                s_roiShadowGreenFireFrame = s_roiShadowGreenPendingFrame;
                s_roiShadowGreenFireMs = s_roiShadowGreenPendingMs;
                s_roiShadowGreenFireDmg = s_roiShadowGreenPendingDmg;
                s_roiShadowGreenFireGF = s_roiShadowGreenPendingGF;
                Log::Battle("BattleTTS: [ROI-LIVE-SHADOW] event %d GREEN-COMMITTED "
                            "(pending-since frame=%d ms=%u gF=%d dmg=%u, "
                            "committed at relMs=%u, no yellow within %ums — heal on ally)",
                            s_roiEventSerial,
                            s_roiShadowGreenPendingFrame,
                            (unsigned)s_roiShadowGreenPendingMs,
                            s_roiShadowGreenPendingGF,
                            (unsigned)s_roiShadowGreenPendingDmg,
                            (unsigned)relMs,
                            (unsigned)ROI_SHADOW_GREEN_COMMIT_WINDOW_MS);
                s_roiShadowGreenPendingFrame = -1;
            }
            // Otherwise check for new pending (gF crossed, dmg>0, not
            // already pending, not cancelled by yellow).
            else if (s_roiShadowGreenPendingFrame < 0 &&
                     liveGF > ROI_SHADOW_GREEN_FULL_THRESHOLD &&
                     curDmgDisp > 0)
            {
                s_roiShadowGreenPendingFrame = curFrameIdx;
                s_roiShadowGreenPendingMs    = relMs;
                s_roiShadowGreenPendingDmg   = curDmgDisp;
                s_roiShadowGreenPendingGF    = liveGF;
                Log::Battle("BattleTTS: [ROI-LIVE-SHADOW] event %d GREEN-PENDING "
                            "frame=%d ms=%u gF=%d dmg=%u (waiting %ums for yellow follow-up)",
                            s_roiEventSerial, curFrameIdx,
                            (unsigned)relMs, liveGF, (unsigned)curDmgDisp,
                            (unsigned)ROI_SHADOW_GREEN_COMMIT_WINDOW_MS);
            }
        }
    }

    s_roiPrevAnimFlag = curAnim;
}

// Called from BattleTTS::Initialize after GdiplusStartup.
static void RoiCalib_InitOnce()
{
    Log::Battle("BattleTTS: [ROI-CALIB] Subsystem ENABLED. "
                "every-N=%d, pre=%d, main=%d, tail=%d, downsample=1/%d",
                ROI_CAPTURE_EVERY_N,
                ROI_PRE_FRAMES, ROI_MAIN_FRAMES, ROI_TAIL_FRAMES,
                ROI_DOWNSAMPLE_FACTOR);
}

// Called from OnBattleEnter.
static void RoiCalib_OnBattleEnter()
{
    s_roiCaptureSkipCounter = 0;
    s_roiPrevAnimFlag = 0;
    // Don't drain pre-ring here; OnSwapBuffers will detect !s_inBattle on
    // exit and clean up. This keeps the entry path simple.
}

#else  // ROI_CALIBRATION_CAPTURE disabled

static void RoiCalib_OnSwapBuffers() {}
static void RoiCalib_InitOnce() {}
static void RoiCalib_OnBattleEnter() {}

#endif  // ROI_CALIBRATION_CAPTURE
