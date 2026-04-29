// battle_tts_screenshot.inl — GL screenshot capture, sprite poll, popup-life diagnostics
//
// Included by battle_tts.cpp BEFORE battle_tts_victory.inl.
// CaptureScreenshot() is called by VictoryAutoCapture in victory.inl.
//
// Extracted from battle_tts.cpp v0.13.44 (session 63, purely mechanical split).
// v0.14.45: Removed F12-triggered victory step capture infrastructure
// (DiffMemorySnapshots, DumpVictoryStep, DumpVictoryScreenData,
// PollVictoryScreen, s_diffSnapPrev). Diagnostic complete.

// ============================================================================
// SwapBuffers hook — OpenGL screenshot capture
// ============================================================================
// v0.12.99: All GDI capture methods (PrintWindow/BitBlt/screen DC) return black
// with this game's OpenGL renderer. Only glReadPixels via SwapBuffers hook works.
// Requires gdiplus.lib + opengl32.lib (linked in deploy.bat).

typedef BOOL (WINAPI *SwapBuffers_t)(HDC);
static SwapBuffers_t s_origSwapBuffers = nullptr;
static bool s_swapHookInstalled = false;
static volatile bool s_captureRequested = false;
static char s_captureBasePath[512] = {};

// v0.13.75: forward-declare the sprite-record frame poller. The body is
// defined below (after DoGLCapture) and called from HookedSwapBuffers.
// Operates on state declared in battle_tts_sprite_spawn.inl — that file
// is included BEFORE this one in battle_tts.cpp, so its static symbols
// (SpriteRec, s_prevRecords, POPUP_* constants, screenshot counters) are
// in scope here.
static void PollPopupRecords();

// Called from hooked SwapBuffers — GL context is current, framebuffer is ready
static void DoGLCapture()
{
    // Get viewport dimensions
    GLint viewport[4];
    glGetIntegerv(GL_VIEWPORT, viewport);
    int w = viewport[2];
    int h = viewport[3];
    if (w <= 0 || h <= 0) {
        Log::Battle("BattleTTS: [VICTORY-SCREENSHOT] GL viewport %dx%d invalid", w, h);
        return;
    }
    
    int stride = ((w * 3 + 3) & ~3);  // 4-byte aligned row stride
    int dataSize = stride * h;
    uint8_t* pixels = (uint8_t*)malloc(dataSize);
    if (!pixels) return;
    
    // Read framebuffer (GL gives bottom-up, same as BMP)
    glPixelStorei(GL_PACK_ALIGNMENT, 4);
    glReadPixels(0, 0, w, h, GL_BGR_EXT, GL_UNSIGNED_BYTE, pixels);
    
    // Write BMP
    char bmpPath[512];
    snprintf(bmpPath, sizeof(bmpPath), "%s.bmp", s_captureBasePath);
    
    BITMAPFILEHEADER fh = {};
    fh.bfType = 0x4D42;
    fh.bfSize = sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER) + dataSize;
    fh.bfOffBits = sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER);
    
    BITMAPINFOHEADER bi = {};
    bi.biSize = sizeof(BITMAPINFOHEADER);
    bi.biWidth = w;
    bi.biHeight = h;  // positive = bottom-up (matches GL)
    bi.biPlanes = 1;
    bi.biBitCount = 24;
    bi.biCompression = BI_RGB;
    bi.biSizeImage = dataSize;
    
    FILE* f = fopen(bmpPath, "wb");
    bool ok = false;
    if (f) {
        fwrite(&fh, sizeof(fh), 1, f);
        fwrite(&bi, sizeof(bi), 1, f);
        fwrite(pixels, dataSize, 1, f);
        fclose(f);
        ok = true;
        
        // Convert BMP to PNG via GDI+
        if (s_gdiplusToken) {
            wchar_t wBmpPath[512], wPngPath[512];
            MultiByteToWideChar(CP_UTF8, 0, bmpPath, -1, wBmpPath, 512);
            char pngPath[512];
            snprintf(pngPath, sizeof(pngPath), "%s.png", s_captureBasePath);
            MultiByteToWideChar(CP_UTF8, 0, pngPath, -1, wPngPath, 512);
            
            Gdiplus::Bitmap* gdiBmp = Gdiplus::Bitmap::FromFile(wBmpPath);
            if (gdiBmp && gdiBmp->GetLastStatus() == Gdiplus::Ok) {
                CLSID pngClsid;
                CLSIDFromString(L"{557CF406-1A04-11D3-9A73-0000F81EF32E}", &pngClsid);
                gdiBmp->Save(wPngPath, &pngClsid, NULL);
                delete gdiBmp;
            }
        }
    } else {
        Log::Battle("BattleTTS: [VICTORY-SCREENSHOT] fopen FAILED: errno=%d path=%s", errno, bmpPath);
    }
    
    free(pixels);
    Log::Battle("BattleTTS: [VICTORY-SCREENSHOT] %s %dx%d -> %s",
               ok ? "Saved" : "FAILED", w, h, bmpPath);
}

// ============================================================================
// v0.13.75 frame-level poll + auto-screenshot
// ============================================================================
// Defined here (not in sprite_spawn.inl) because these functions need
// DoGLCapture() + s_captureBasePath which are static to this file. State
// they operate on (SpriteRec, s_prevRecords, POPUP_* constants, screenshot
// counters) is defined in battle_tts_sprite_spawn.inl, which is included
// BEFORE this file in battle_tts.cpp — so those symbols are in scope here.

// Band classifier for byte[3] lifetime countdown. Returns an integer band
// id so we can log only when bands change, not every frame a single-unit
// tick happens.
static int LifetimeBand(uint8_t lifetime)
{
    if (lifetime >= 0xC0) return 5;   // fresh (0xFF..0xC0)
    if (lifetime >= 0x80) return 4;   // early (0xBF..0x80)
    if (lifetime >= 0x40) return 3;   // middle (0x7F..0x40)
    if (lifetime >= 0x10) return 2;   // late   (0x3F..0x10)
    if (lifetime > 0x00)  return 1;   // fading (0x0F..0x01)
    return 0;                          // expired (0x00)
}

static void PollSpriteScreenshot(const char* tag, uint8_t slot, uint8_t text_id,
                                  uint16_t value)
{
    // Ensure screenshot directory exists on first fire. Idempotent.
    if (!s_pollScreenshotDirEnsured) {
        BOOL ok = CreateDirectoryA(KIND4_SCREENSHOT_DIR, NULL);
        DWORD err = ok ? 0 : GetLastError();
        if (!ok && err != ERROR_ALREADY_EXISTS) {
            Log::Battle("BattleTTS: [SPRITE-POLL] CreateDirectory %s FAILED (err=%u)",
                        KIND4_SCREENSHOT_DIR, err);
        }
        s_pollScreenshotDirEnsured = true;
    }

    SYSTEMTIME st;
    GetLocalTime(&st);
    snprintf(s_captureBasePath, sizeof(s_captureBasePath),
             "%s\\poll_%s_%02d%02d%02d_%03d_f%u_slot%u_kind%02X_val%u",
             KIND4_SCREENSHOT_DIR, tag,
             st.wHour, st.wMinute, st.wSecond, st.wMilliseconds,
             s_pollFrameCounter, slot, text_id, value);

    // We're already inside HookedSwapBuffers; GL context is current and
    // the back buffer holds the rendered frame that is about to swap.
    // Call DoGLCapture() directly — no flag dance needed.
    DoGLCapture();

    Log::Battle("BattleTTS: [SPRITE-POLL] Screenshot %s: %s",
                tag, s_captureBasePath);
}

// ============================================================================
// v0.13.92 — Damage popup TIME-BASED capture diagnostic + per-frame record dump
// ============================================================================
// v0.13.91 BAT invalidated the byte[3]=lifetime-countdown premise:
//   - kind=0x01 (Quistis physical hit on Grat): byte[3]=0xFF for entire 2860ms
//     lifetime. 0/7 thresholds fired naturally.
//   - kind=0x02 (Squall Thunder on Grat): byte[3]=0xFF for entire 5719ms
//     lifetime. 0/7 thresholds fired naturally.
//   - kind=0x08 (Grat counter on Quistis): byte[3]=0x02 at spawn and locked
//     for entire 6781ms lifetime. All 7 thresholds fired same-frame at spawn
//     (life=0x02 ≤ every threshold value).
// byte[3] is NOT a countdown timer; it's a per-kind state/type field.
// Visual evidence (existing v0.13.91 screenshots): popup NEW = engine intent
// moment (animation just starting, no number on screen). Anim-done for
// kind=0x01 = number still visible (good). Anim-done for kind=0x02/0x08 =
// number already faded (late). Damage sprite first appears at some moment
// BETWEEN popup NEW and anim-done that varies per spell type.
//
// v0.13.92 STRATEGY: time-based capture + per-frame record dump.
//   1. At popup NEW for damage kinds, start time-based tracking.
//   2. Each frame, fire a screenshot if elapsedMs has crossed any of:
//      [250, 500, 1000, 1500, 2000, 2500, 3000, 4000,
//       4500, 5000, 5500, 6000] ms (v0.13.93 extended for magic spells).
//   3. Each frame while tracking, also LOG the full 20-byte record (no
//      screenshot — the [POPUP-TIME-DIAG] FRAME log lines let us scan for
//      ANY byte that changes mid-animation across the lifetime, even if
//      it's not byte[3]. If we find such a byte, v0.13.93 can hook on it
//      directly without the time-offset workaround.
//   4. On DESPAWN, log a summary line.
//
// FILENAME ENCODES (popup_time_HHMMSS_mmm_kindKK_valV_dmgD_slotS_+Nms.bmp):
//   kind  : popup byte[1] (damage type)
//   val   : popup byte[4-5] (engine value at spawn)
//   dmg   : BATTLE_DAMAGE_DISPLAY_ADDR (0x01D2834A) at spawn
//   slot  : popup byte[0] (target slot)
//   +Nms  : the time-offset gate that triggered this capture
//
// LOG FORMAT (NEW TAG):
//   [POPUP-TIME-DIAG] START tracking trackIdx=I kind=K val=V dmg=D slot=S
//                     spawnLife=L spawnRecord=[20 hex]
//   [POPUP-TIME-DIAG] FRAME trackIdx=I f=F +Nms record=[20 hex]
//                     (logged every frame while tracking)
//   [POPUP-TIME-DIAG] CAPTURE trackIdx=I gate=Nms life=L f=F record=[20 hex]
//                     (fires DoGLCapture at the corresponding time offset)
//   [POPUP-TIME-DIAG] END tracking trackIdx=I totalFrames=F totalMs=M
//                     gatesHit=G/N
// v0.13.93 adds 4 more gates (+4500/5000/5500/6000ms) to catch the
// magic-spell visible window beyond v0.13.92's +4000ms ceiling.
//
// REMOVAL: All v0.13.93 state and code is tagged with this comment block.
// v0.13.94 will either ship kind-specific timing using the empirical offset
// from v0.13.93's BAT, or fall back to v0.13.90's anim-flag-end timing.

static const uint32_t POPUP_TIME_DIAG_OFFSETS_MS[] = {
    250, 500, 1000, 1500, 2000, 2500, 3000, 4000,
    // v0.13.93: extended gates for magic-spell visible window. v0.13.92 BAT
    // showed kind=0x02 popup lives >4000ms with all 8 prior gates catching
    // pre-impact phases. The damage number must appear after +4000ms but
    // before anim-done (~5000-6000ms range for spells).
    4500, 5000, 5500, 6000
};
static const int POPUP_TIME_DIAG_OFFSET_COUNT =
    sizeof(POPUP_TIME_DIAG_OFFSETS_MS) / sizeof(POPUP_TIME_DIAG_OFFSETS_MS[0]);

// Hard cap on captures per battle. v0.13.94: 12 time-gates + 10 change
// shots per damage popup = up to 22 per popup. Typical 4-event BAT
// (1 magic + 1 physical + 2 enemy attacks): 22 + 22 + 22 + 22 = 88,
// but in practice CHANGE captures rarely fire >2-3x because the static
// regions don't transition often. Cap raised to 120 to avoid clipping.
static const int POPUP_LIFE_DIAG_SCREENSHOT_MAX = 120;
static int s_popupLifeDiagScreenshotCount = 0;

// v0.13.92 (deep research integration): hypothesized damage display state
// region. Per "Battle damage display trigger deep research results.md" §4,
// the region 0x01D28340-0x01D2835F may hold the renderer's display state
// machine, separate from the popup record table at 0x01D280C4 we already
// poll. The deep research's specific structural hypothesis (observed during
// a single 12-damage event):
//
//   +0x00 (u16)  popup-slot index / next free slot   observed 0x0000
//   +0x04 (u16)  popup type bitfield                 observed 0x0401
//   +0x08 (u16)  target entity slot index            observed 0x0003
//   +0x0A (u16)  damage value displayed              observed 0x000C = 12
//   +0x0E (u16)  display state / "visible" flag      observed 0x4000
//   +0x10 (u16)  fade/lifetime countdown             observed 0x00FF
//
// The deep research itself flagged this layout as a plausibility hypothesis
// (one observed event, derived from FF-engine conventions). v0.13.92 polls
// the full 32-byte region per frame so we can either confirm or refute.
// If the +0x0E flag transitions 0→non-zero mid-animation, that's exactly the
// "sprite first visible" signal we've been hunting and v0.13.93 can hook on
// it (or on whatever code writes it). If the region is static through the
// popup lifetime, the hypothesis is wrong and we fall back to time-offset.
static const uint32_t DAMAGE_DISPLAY_REGION_ADDR = 0x01D28340;
static const int DAMAGE_DISPLAY_REGION_SIZE = 128;  // v0.13.94: extended from 32 to catch state past the deep-research window

// v0.13.94: NEW entity buffer poll. Each popup record carries a pointer at
// bytes[12..15] (cur.entity_ptr in SpriteRec). v0.13.92/.93 confirmed that
// the popup record AND the disp region are both static throughout a popup's
// lifetime, but neither is the per-popup state machine — the entity buffer
// (which we've never polled) is the strongest remaining hypothesis. We
// snapshot 256 bytes starting at entity_ptr per frame and look for any byte
// that transitions during the popup lifetime. If a byte flips at "sprite
// first visible" we get a perfect hook target that auto-adapts to spell
// animation length — Thunder vs Fire vs Cure all share the entity buffer
// shape but their animation timing differs.
static const int POPUP_ENTITY_REGION_SIZE = 256;

// v0.13.94: separate cap for change-triggered screenshots. Distinct from the
// time-gate cap (POPUP_LIFE_DIAG_SCREENSHOT_MAX). 10 per popup is enough to
// see all distinct state transitions — we don't expect more than ~5 phase
// changes (cast / charge / impact / number-visible / fade) per spell.
static const int POPUP_CHANGE_SCREENSHOTS_PER_POPUP = 10;

// v0.13.94: per-region cap on full-hex diff log lines. Keeps log readable.
// After this many full-hex DIFF lines for a region, we still log per-byte
// delta lines and still fire screenshots, but skip the full-hex repeats.
static const int POPUP_DIFF_LOG_LINES_PER_REGION = 20;

struct PopupLifeDiagState {
    bool     tracking;
    uint8_t  spawnedKind;
    uint16_t spawnedVal;
    uint16_t spawnedDmg;
    uint8_t  spawnedSlot;
    uint32_t spawnFrame;
    DWORD    spawnTick;
    int      nextGateIdx;            // v0.13.92: index into POPUP_TIME_DIAG_OFFSETS_MS
    uint8_t  lastRecord[POPUP_RECORD_STRIDE];  // v0.13.92: previous frame's bytes
    bool     lastRecordValid;        // v0.13.92: false until first frame logged
    uint8_t  lastDispRegion[DAMAGE_DISPLAY_REGION_SIZE];  // v0.13.92: previous frame's 0x01D28340 region (128 bytes as of v0.13.94)
    bool     lastDispRegionValid;    // v0.13.92: false until first frame logged

    // v0.13.94: entity buffer poll state. entityPtr captured at spawn from
    // cur.entity_ptr (popup record bytes[12..15]). May be 0 if the popup
    // type doesn't use one (we still poll record + disp in that case).
    uint32_t entityPtr;
    uint8_t  lastEntityRegion[POPUP_ENTITY_REGION_SIZE];
    bool     lastEntityRegionValid;

    // v0.13.94: per-region full-hex diff log counters (capped at
    // POPUP_DIFF_LOG_LINES_PER_REGION). Beyond cap, we still fire the
    // change screenshot and emit per-byte DIFF-* lines, but skip the
    // verbose full-hex DIFF lines to keep the log readable.
    int      recordDiffLogCount;
    int      dispDiffLogCount;
    int      entityDiffLogCount;

    // v0.13.94: change-triggered screenshot counter (capped at
    // POPUP_CHANGE_SCREENSHOTS_PER_POPUP).
    int      changeScreenshotsFired;
};
static PopupLifeDiagState s_popupLifeDiag[POPUP_TRACK_MAX] = {};

static bool IsDamagePopupKind(uint8_t kind)
{
    // Per session-80 record-layout discovery: kind=0x01 player-physical,
    // 0x02 magic-damage, 0x08 enemy-damage. v0.13.88 BAT confirmed all
    // three carry the actual rendered damage value at spawn.
    return (kind == 0x01 || kind == 0x02 || kind == 0x08);
}

// v0.13.92: read 20 bytes from the popup record table for trackIdx into out[].
// Returns true on success, false on access violation. Used by FRAME and
// CAPTURE logging paths.
static bool ReadPopupRecord(int trackIdx, uint8_t out[POPUP_RECORD_STRIDE])
{
    bool ok = false;
    __try {
        const uint8_t* p = (const uint8_t*)(POPUP_TABLE_BASE +
                                              (uintptr_t)trackIdx * POPUP_RECORD_STRIDE);
        for (size_t i = 0; i < POPUP_RECORD_STRIDE; i++) out[i] = p[i];
        ok = true;
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
    return ok;
}

// v0.13.92: format a 20-byte popup record as space-separated hex into buf.
// buf must hold at least POPUP_RECORD_STRIDE*3 + 4 chars.
static void FormatPopupRecordHex(const uint8_t bytes[POPUP_RECORD_STRIDE],
                                   bool readOk, char* buf, size_t bufSize)
{
    if (!readOk) {
        snprintf(buf, bufSize, "READ_FAULT");
        return;
    }
    int hp = 0;
    for (size_t i = 0; i < POPUP_RECORD_STRIDE; i++) {
        hp += snprintf(buf + hp, bufSize - hp, "%02X ", bytes[i]);
    }
}

// v0.13.92: read 32 bytes from the hypothesized damage display state region
// at 0x01D28340. Returns true on success, false on access violation.
static bool ReadDamageDisplayRegion(uint8_t out[DAMAGE_DISPLAY_REGION_SIZE])
{
    bool ok = false;
    __try {
        const uint8_t* p = (const uint8_t*)DAMAGE_DISPLAY_REGION_ADDR;
        for (int i = 0; i < DAMAGE_DISPLAY_REGION_SIZE; i++) out[i] = p[i];
        ok = true;
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
    return ok;
}

// v0.13.92: format an arbitrary byte buffer as space-separated hex into buf.
// Used for the disp-region dump (32 bytes). buf must hold count*3 + 4 chars.
static void FormatHexBytes(const uint8_t* bytes, size_t count,
                            bool readOk, char* buf, size_t bufSize)
{
    if (!readOk) {
        snprintf(buf, bufSize, "READ_FAULT");
        return;
    }
    int hp = 0;
    for (size_t i = 0; i < count; i++) {
        hp += snprintf(buf + hp, bufSize - hp, "%02X ", bytes[i]);
    }
}

// v0.13.94: generic memory-block reader with SEH guard. Used for the
// entity buffer poll — entityPtr is variable per event and may be invalid
// if the popup type doesn't use one. Returns false on access violation;
// caller should treat that as "region unavailable this frame" and skip.
static bool ReadMemoryBlock(uint32_t addr, uint8_t* out, int size)
{
    if (addr == 0 || out == nullptr || size <= 0) return false;
    bool ok = false;
    __try {
        const uint8_t* p = (const uint8_t*)(uintptr_t)addr;
        for (int i = 0; i < size; i++) out[i] = p[i];
        ok = true;
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
    return ok;
}

// v0.13.94: log per-byte deltas between two snapshots. Cap output at 16
// changes per call so a wholesale-overwritten region doesn't flood the log.
// regionLabel goes into the [POPUP-TIME-DIAG] DIFF-<LABEL> tag; trackIdx,
// framesSinceSpawn, elapsedMs are passed through verbatim. Only emits a
// log line if at least one byte differs.
static int LogPerByteDeltas(const uint8_t* prev, const uint8_t* cur, int size,
                              const char* regionLabel, int trackIdx,
                              uint32_t framesSinceSpawn, DWORD elapsedMs)
{
    int changeCount = 0;
    char buf[1024] = {};
    int bp = 0;
    static const int MAX_DELTAS_PER_LINE = 16;
    for (int i = 0; i < size; i++) {
        if (prev[i] != cur[i]) {
            changeCount++;
            if (changeCount <= MAX_DELTAS_PER_LINE && bp < (int)sizeof(buf) - 32) {
                bp += snprintf(buf + bp, sizeof(buf) - bp,
                                "[+0x%02X: 0x%02X->0x%02X] ",
                                i, prev[i], cur[i]);
            }
        }
    }
    if (changeCount == 0) return 0;
    Log::Battle("BattleTTS: [POPUP-TIME-DIAG] DIFF-%s trackIdx=%d f=%u +%ums totalChanges=%d %s%s",
                regionLabel, trackIdx, framesSinceSpawn, (unsigned)elapsedMs,
                changeCount, buf,
                changeCount > MAX_DELTAS_PER_LINE ? "...(truncated)" : "");
    return changeCount;
}

// v0.13.94: fire DoGLCapture for the moment a region transitions. Filename
// encodes which region triggered it, the elapsed time, and core popup
// metadata so we can pair these with the FRAME/DIFF log lines after BAT.
// Capped at POPUP_CHANGE_SCREENSHOTS_PER_POPUP per popup.
static void FirePopupChangeCapture(int trackIdx, const char* reason,
                                     uint32_t curFrame, const SpriteRec& cur)
{
    PopupLifeDiagState& st = s_popupLifeDiag[trackIdx];
    if (st.changeScreenshotsFired >= POPUP_CHANGE_SCREENSHOTS_PER_POPUP) return;
    if (s_popupLifeDiagScreenshotCount >= POPUP_LIFE_DIAG_SCREENSHOT_MAX) return;
    st.changeScreenshotsFired++;
    s_popupLifeDiagScreenshotCount++;

    DWORD elapsedMs = GetTickCount() - st.spawnTick;
    uint32_t framesSinceSpawn = curFrame - st.spawnFrame;

    SYSTEMTIME wt;
    GetLocalTime(&wt);
    snprintf(s_captureBasePath, sizeof(s_captureBasePath),
             "%s\\popup_change_%02d%02d%02d_%03d_kind%02X_dmg%u_slot%u_+%ums_%s_n%d",
             KIND4_SCREENSHOT_DIR,
             wt.wHour, wt.wMinute, wt.wSecond, wt.wMilliseconds,
             st.spawnedKind, (unsigned)st.spawnedDmg, (unsigned)st.spawnedSlot,
             (unsigned)elapsedMs, reason, st.changeScreenshotsFired);

    DoGLCapture();

    Log::Battle("BattleTTS: [POPUP-TIME-DIAG] CHANGE trackIdx=%d region=%s f=%u +%ums n=%d/%d kind=0x%02X dmg=%u",
                trackIdx, reason, framesSinceSpawn, (unsigned)elapsedMs,
                st.changeScreenshotsFired, POPUP_CHANGE_SCREENSHOTS_PER_POPUP,
                st.spawnedKind, (unsigned)st.spawnedDmg);
}

static void FirePopupTimeDiagCapture(int trackIdx, uint32_t gateMs,
                                       uint32_t curFrame, const SpriteRec& cur)
{
    if (s_popupLifeDiagScreenshotCount >= POPUP_LIFE_DIAG_SCREENSHOT_MAX) return;
    s_popupLifeDiagScreenshotCount++;

    PopupLifeDiagState& st = s_popupLifeDiag[trackIdx];
    DWORD elapsedMs = GetTickCount() - st.spawnTick;
    uint32_t framesSinceSpawn = curFrame - st.spawnFrame;

    SYSTEMTIME wt;
    GetLocalTime(&wt);
    snprintf(s_captureBasePath, sizeof(s_captureBasePath),
             "%s\\popup_time_%02d%02d%02d_%03d_kind%02X_val%u_dmg%u_slot%u_+%ums",
             KIND4_SCREENSHOT_DIR,
             wt.wHour, wt.wMinute, wt.wSecond, wt.wMilliseconds,
             st.spawnedKind, (unsigned)st.spawnedVal, (unsigned)st.spawnedDmg,
             (unsigned)st.spawnedSlot,
             (unsigned)gateMs);

    // We're inside HookedSwapBuffers; GL context is current and back buffer
    // holds the about-to-swap frame. Fire DoGLCapture directly.
    DoGLCapture();

    // Dump the full 20-byte popup record at this exact moment.
    uint8_t bytes[POPUP_RECORD_STRIDE] = {};
    bool ok = ReadPopupRecord(trackIdx, bytes);
    char hex[POPUP_RECORD_STRIDE * 3 + 4] = {};
    FormatPopupRecordHex(bytes, ok, hex, sizeof(hex));

    Log::Battle("BattleTTS: [POPUP-TIME-DIAG] CAPTURE trackIdx=%d gate=%ums life=0x%02X "
                "f=%u elapsedMs=%u kind=0x%02X val=%u dmg=%u slot=%u record=[%s]",
                trackIdx, (unsigned)gateMs, (unsigned)cur.lifetime,
                framesSinceSpawn, (unsigned)elapsedMs,
                st.spawnedKind, (unsigned)st.spawnedVal, (unsigned)st.spawnedDmg,
                (unsigned)st.spawnedSlot, hex);

    // v0.13.92: also dump the disp-region (0x01D28340) at gate time. Same
    // moment as the screenshot — lets us correlate the two states with the
    // visual evidence.
    uint8_t dispBytes[DAMAGE_DISPLAY_REGION_SIZE] = {};
    bool dispOk = ReadDamageDisplayRegion(dispBytes);
    char dispHex[DAMAGE_DISPLAY_REGION_SIZE * 3 + 4] = {};
    FormatHexBytes(dispBytes, DAMAGE_DISPLAY_REGION_SIZE, dispOk, dispHex, sizeof(dispHex));
    Log::Battle("BattleTTS: [POPUP-TIME-DIAG] CAPTURE-DISP trackIdx=%d gate=%ums region=[%s]",
                trackIdx, (unsigned)gateMs, dispHex);
}

static void PopupLifeDiag_OnNew(int trackIdx, const SpriteRec& cur,
                                  uint16_t damageDisplay, uint32_t curFrame)
{
    if (!IsDamagePopupKind(cur.text_id)) return;
    PopupLifeDiagState& st = s_popupLifeDiag[trackIdx];
    st.tracking         = true;
    st.spawnedKind      = cur.text_id;
    st.spawnedVal       = cur.value;
    st.spawnedDmg       = damageDisplay;
    st.spawnedSlot      = cur.slot;
    st.spawnFrame       = curFrame;
    st.spawnTick        = GetTickCount();
    st.nextGateIdx      = 0;
    st.lastRecordValid  = false;
    st.lastDispRegionValid = false;

    // v0.13.94: capture entity pointer + snapshot 256-byte region.
    st.entityPtr        = cur.entity_ptr;
    st.lastEntityRegionValid = false;
    st.recordDiffLogCount  = 0;
    st.dispDiffLogCount    = 0;
    st.entityDiffLogCount  = 0;
    st.changeScreenshotsFired = 0;

    // Read spawn-frame record so the START line carries it for offline diff.
    uint8_t bytes[POPUP_RECORD_STRIDE] = {};
    bool ok = ReadPopupRecord(trackIdx, bytes);
    char hex[POPUP_RECORD_STRIDE * 3 + 4] = {};
    FormatPopupRecordHex(bytes, ok, hex, sizeof(hex));

    // Seed lastRecord with spawn-frame bytes so the FRAME diff path has
    // a baseline. The FRAME line itself fires next OnTick (one frame later).
    if (ok) {
        memcpy(st.lastRecord, bytes, POPUP_RECORD_STRIDE);
        st.lastRecordValid = true;
    }

    // v0.13.92: also read spawn-frame disp region (0x01D28340) for baseline.
    // v0.13.94: extended to 128 bytes.
    uint8_t dispBytes[DAMAGE_DISPLAY_REGION_SIZE] = {};
    bool dispOk = ReadDamageDisplayRegion(dispBytes);
    char dispHex[DAMAGE_DISPLAY_REGION_SIZE * 3 + 4] = {};
    FormatHexBytes(dispBytes, DAMAGE_DISPLAY_REGION_SIZE, dispOk, dispHex, sizeof(dispHex));
    if (dispOk) {
        memcpy(st.lastDispRegion, dispBytes, DAMAGE_DISPLAY_REGION_SIZE);
        st.lastDispRegionValid = true;
    }

    // v0.13.94: read spawn-frame entity region from cur.entity_ptr.
    // entityPtr may be 0 for popup types that don't use one — in that case
    // we just skip the region (lastEntityRegionValid stays false).
    uint8_t entBytes[POPUP_ENTITY_REGION_SIZE] = {};
    bool entOk = ReadMemoryBlock(st.entityPtr, entBytes, POPUP_ENTITY_REGION_SIZE);
    char entHex[POPUP_ENTITY_REGION_SIZE * 3 + 8] = {};
    FormatHexBytes(entBytes, POPUP_ENTITY_REGION_SIZE, entOk, entHex, sizeof(entHex));
    if (entOk) {
        memcpy(st.lastEntityRegion, entBytes, POPUP_ENTITY_REGION_SIZE);
        st.lastEntityRegionValid = true;
    }

    Log::Battle("BattleTTS: [POPUP-TIME-DIAG] START tracking trackIdx=%d kind=0x%02X val=%u "
                "dmg=%u slot=%u spawnLife=0x%02X spawnRecord=[%s]",
                trackIdx, st.spawnedKind, (unsigned)st.spawnedVal,
                (unsigned)st.spawnedDmg, (unsigned)st.spawnedSlot,
                (unsigned)cur.lifetime, hex);
    Log::Battle("BattleTTS: [POPUP-TIME-DIAG] START-DISP trackIdx=%d region=[%s]",
                trackIdx, dispHex);
    Log::Battle("BattleTTS: [POPUP-TIME-DIAG] START-ENT trackIdx=%d entityPtr=0x%08X readOk=%d region=[%s]",
                trackIdx, st.entityPtr, entOk ? 1 : 0,
                entOk ? entHex : "NULL_OR_FAULT");

    // Same-frame gate firing only if a 0ms gate exists (it doesn't — we
    // start at 250ms). Loop kept defensive in case gate list ever changes.
    while (st.nextGateIdx < POPUP_TIME_DIAG_OFFSET_COUNT &&
           POPUP_TIME_DIAG_OFFSETS_MS[st.nextGateIdx] == 0) {
        FirePopupTimeDiagCapture(trackIdx,
                                  POPUP_TIME_DIAG_OFFSETS_MS[st.nextGateIdx],
                                  curFrame, cur);
        st.nextGateIdx++;
    }
}

static void PopupLifeDiag_OnTick(int trackIdx, const SpriteRec& cur,
                                   uint32_t curFrame)
{
    PopupLifeDiagState& st = s_popupLifeDiag[trackIdx];
    if (!st.tracking) return;

    DWORD elapsedMs = GetTickCount() - st.spawnTick;
    uint32_t framesSinceSpawn = curFrame - st.spawnFrame;
    bool heartbeat = (framesSinceSpawn % 10 == 0);
    // v0.13.94: changeShotTrigger fires the CHANGE screenshot. Only flipped
    // for REC/DISP transitions — those regions were 100% static across all
    // v0.13.93 events, so any transition is almost certainly the smoking gun
    // we're hunting. The ENT region is likely to contain noisy fields (anim
    // timers, position interpolation) that change every frame; we still log
    // its per-byte deltas + heartbeat full-hex, but its changes don't burn
    // the screenshot budget.
    bool changeShotTrigger = false;

    // ----- Region 1: popup record (20 bytes at POPUP_TABLE_BASE+trackIdx*stride)
    uint8_t recBytes[POPUP_RECORD_STRIDE] = {};
    bool recOk = ReadPopupRecord(trackIdx, recBytes);
    if (recOk) {
        bool recChanged = false;
        if (st.lastRecordValid) {
            for (size_t i = 0; i < POPUP_RECORD_STRIDE; i++) {
                if (recBytes[i] != st.lastRecord[i]) { recChanged = true; break; }
            }
        } else {
            recChanged = true;
        }
        if (recChanged) {
            changeShotTrigger = true;
            // v0.13.94: per-byte delta logging — the smoking-gun signal.
            if (st.lastRecordValid) {
                LogPerByteDeltas(st.lastRecord, recBytes, POPUP_RECORD_STRIDE,
                                  "REC", trackIdx, framesSinceSpawn, elapsedMs);
            }
        }
        if (recChanged || heartbeat) {
            // Full-hex log: gated by per-region cap on diff lines (heartbeats
            // always log to give us periodic context).
            bool emitFullHex = heartbeat || st.recordDiffLogCount < POPUP_DIFF_LOG_LINES_PER_REGION;
            if (emitFullHex) {
                char hex[POPUP_RECORD_STRIDE * 3 + 4] = {};
                FormatPopupRecordHex(recBytes, true, hex, sizeof(hex));
                char tag = recChanged ? 'D' : 'H';
                Log::Battle("BattleTTS: [POPUP-TIME-DIAG] FRAME trackIdx=%d f=%u +%ums "
                            "life=0x%02X tag=%c record=[%s]",
                            trackIdx, framesSinceSpawn, (unsigned)elapsedMs,
                            (unsigned)cur.lifetime, tag, hex);
            }
            if (recChanged) st.recordDiffLogCount++;
            memcpy(st.lastRecord, recBytes, POPUP_RECORD_STRIDE);
            st.lastRecordValid = true;
        }
    }

    // ----- Region 2: damage display region (128 bytes at 0x01D28340)
    uint8_t dispBytes[DAMAGE_DISPLAY_REGION_SIZE] = {};
    bool dispOk = ReadDamageDisplayRegion(dispBytes);
    if (dispOk) {
        bool dispChanged = false;
        if (st.lastDispRegionValid) {
            for (int i = 0; i < DAMAGE_DISPLAY_REGION_SIZE; i++) {
                if (dispBytes[i] != st.lastDispRegion[i]) { dispChanged = true; break; }
            }
        } else {
            dispChanged = true;
        }
        if (dispChanged) {
            changeShotTrigger = true;
            if (st.lastDispRegionValid) {
                LogPerByteDeltas(st.lastDispRegion, dispBytes, DAMAGE_DISPLAY_REGION_SIZE,
                                  "DISP", trackIdx, framesSinceSpawn, elapsedMs);
            }
        }
        if (dispChanged || heartbeat) {
            bool emitFullHex = heartbeat || st.dispDiffLogCount < POPUP_DIFF_LOG_LINES_PER_REGION;
            if (emitFullHex) {
                char dispHex[DAMAGE_DISPLAY_REGION_SIZE * 3 + 4] = {};
                FormatHexBytes(dispBytes, DAMAGE_DISPLAY_REGION_SIZE, true, dispHex, sizeof(dispHex));
                char dispTag = dispChanged ? 'D' : 'H';
                Log::Battle("BattleTTS: [POPUP-TIME-DIAG] DISP trackIdx=%d f=%u +%ums tag=%c region=[%s]",
                            trackIdx, framesSinceSpawn, (unsigned)elapsedMs,
                            dispTag, dispHex);
            }
            if (dispChanged) st.dispDiffLogCount++;
            memcpy(st.lastDispRegion, dispBytes, DAMAGE_DISPLAY_REGION_SIZE);
            st.lastDispRegionValid = true;
        }
    }

    // ----- Region 3 (NEW v0.13.94): entity buffer at popup record's pointer-B
    // 256 bytes starting at st.entityPtr (captured at spawn from cur.entity_ptr).
    // The strongest hypothesis for where per-popup state lives. Skips entirely
    // if entityPtr was 0 at spawn (popup type doesn't use one).
    if (st.entityPtr != 0) {
        uint8_t entBytes[POPUP_ENTITY_REGION_SIZE] = {};
        bool entOk = ReadMemoryBlock(st.entityPtr, entBytes, POPUP_ENTITY_REGION_SIZE);
        if (entOk) {
            bool entChanged = false;
            if (st.lastEntityRegionValid) {
                for (int i = 0; i < POPUP_ENTITY_REGION_SIZE; i++) {
                    if (entBytes[i] != st.lastEntityRegion[i]) { entChanged = true; break; }
                }
            } else {
                entChanged = true;
            }
            if (entChanged) {
                // v0.13.94: per-byte delta logging only — entity changes don't
                // trigger CHANGE screenshots because the region is likely
                // noisy (anim timers etc.). Diff logs are still informative
                // for spotting which offsets stay stable vs which churn.
                if (st.lastEntityRegionValid) {
                    LogPerByteDeltas(st.lastEntityRegion, entBytes, POPUP_ENTITY_REGION_SIZE,
                                      "ENT", trackIdx, framesSinceSpawn, elapsedMs);
                }
            }
            if (entChanged || heartbeat) {
                bool emitFullHex = heartbeat || st.entityDiffLogCount < POPUP_DIFF_LOG_LINES_PER_REGION;
                if (emitFullHex) {
                    char entHex[POPUP_ENTITY_REGION_SIZE * 3 + 8] = {};
                    FormatHexBytes(entBytes, POPUP_ENTITY_REGION_SIZE, true, entHex, sizeof(entHex));
                    char entTag = entChanged ? 'D' : 'H';
                    Log::Battle("BattleTTS: [POPUP-TIME-DIAG] ENT trackIdx=%d f=%u +%ums tag=%c region=[%s]",
                                trackIdx, framesSinceSpawn, (unsigned)elapsedMs,
                                entTag, entHex);
                }
                if (entChanged) st.entityDiffLogCount++;
                memcpy(st.lastEntityRegion, entBytes, POPUP_ENTITY_REGION_SIZE);
                st.lastEntityRegionValid = true;
            }
        }
    }

    // v0.13.94: fire a CHANGE screenshot if a STATIC region (REC or DISP)
    // transitions this frame. Both regions were 100% static across all
    // v0.13.93 events, so any transition during a popup is the strongest
    // signal we have for state machine activity. ENT changes don't trigger
    // because that region is likely noisy. Capped at
    // POPUP_CHANGE_SCREENSHOTS_PER_POPUP per popup.
    if (changeShotTrigger) {
        FirePopupChangeCapture(trackIdx, "static", curFrame, cur);
    }

    // v0.13.92: time-gate captures. Fire DoGLCapture at each elapsed-ms
    // gate we cross. Loop handles the case where multiple gates fall
    // inside a single frame's elapsed-ms delta (rare at 60fps but possible
    // on a slow frame).
    while (st.nextGateIdx < POPUP_TIME_DIAG_OFFSET_COUNT &&
           elapsedMs >= POPUP_TIME_DIAG_OFFSETS_MS[st.nextGateIdx]) {
        FirePopupTimeDiagCapture(trackIdx,
                                  POPUP_TIME_DIAG_OFFSETS_MS[st.nextGateIdx],
                                  curFrame, cur);
        st.nextGateIdx++;
    }
}

static void PopupLifeDiag_OnDespawn(int trackIdx, const SpriteRec& prev,
                                      uint32_t curFrame)
{
    PopupLifeDiagState& st = s_popupLifeDiag[trackIdx];
    if (!st.tracking) return;
    DWORD elapsedMs = GetTickCount() - st.spawnTick;
    uint32_t totalFrames = curFrame - st.spawnFrame;
    Log::Battle("BattleTTS: [POPUP-TIME-DIAG] END tracking trackIdx=%d kind=0x%02X val=%u dmg=%u "
                "totalFrames=%u totalMs=%u gatesHit=%d/%d "
                "diffs=REC:%d/DISP:%d/ENT:%d changeShots=%d/%d entityPtr=0x%08X",
                trackIdx, st.spawnedKind, (unsigned)st.spawnedVal,
                (unsigned)st.spawnedDmg, totalFrames, (unsigned)elapsedMs,
                st.nextGateIdx, POPUP_TIME_DIAG_OFFSET_COUNT,
                st.recordDiffLogCount, st.dispDiffLogCount, st.entityDiffLogCount,
                st.changeScreenshotsFired, POPUP_CHANGE_SCREENSHOTS_PER_POPUP,
                st.entityPtr);
    st.tracking = false;
}

static void ResetPopupLifeDiagState()
{
    s_popupLifeDiagScreenshotCount = 0;
    memset(s_popupLifeDiag, 0, sizeof(s_popupLifeDiag));
}

static void PollPopupRecords()
{
    // Only poll when the game is in battle mode.
    uint16_t mode = 0;
    if (FF8Addresses::pGameMode) {
        __try { mode = *FF8Addresses::pGameMode; } __except(EXCEPTION_EXECUTE_HANDLER) {
            return;
        }
    }
    if (mode != 3) return;

    s_pollFrameCounter++;

    // Read count.
    uint8_t count = 0;
    __try {
        count = *(uint8_t*)POPUP_COUNT_ADDR;
    } __except(EXCEPTION_EXECUTE_HANDLER) { return; }

    // Cap at track max. If the game ever goes higher we'd just miss the
    // tail; in practice we've only ever seen count=1 during animations.
    if (count > POPUP_TRACK_MAX) count = POPUP_TRACK_MAX;

    // Read current frame's records.
    SpriteRec current[POPUP_TRACK_MAX] = {};
    for (uint8_t i = 0; i < count; i++) {
        __try {
            const uint8_t* p = (const uint8_t*)(POPUP_TABLE_BASE +
                                                 (uintptr_t)i * POPUP_RECORD_STRIDE);
            current[i].valid      = true;
            current[i].slot       = p[0];
            current[i].text_id    = p[1];
            current[i].style      = p[2];
            current[i].lifetime   = p[3];
            current[i].value      = *(const uint16_t*)(p + 4);
            current[i].secondary  = *(const uint16_t*)(p + 6);
            current[i].entity_ptr = *(const uint32_t*)(p + 12);
        } __except(EXCEPTION_EXECUTE_HANDLER) {
            current[i].valid = false;
        }
    }

    // Compare with previous frame's snapshot and log transitions.
    for (int i = 0; i < POPUP_TRACK_MAX; i++) {
        const SpriteRec& prev = s_prevRecords[i];
        const SpriteRec& cur  = current[i];

        if (!prev.valid && !cur.valid) continue;

        if (!prev.valid && cur.valid) {
            // v0.13.88: read BATTLE_DAMAGE_DISPLAY_ADDR (0x01D2834A) at the
            // exact moment the popup record spawns. Tests session 83
            // hypothesis A — if damage numbers ARE in this 0x01D280C4
            // table (kind=0x01/0x02/0x08) but the rendered value is sourced
            // from HP-delta state rather than record bytes[4-5], then `dmg`
            // here will match the eventually-announced damage and `val`
            // will be a different (often unrelated) value. Pure observation.
            uint16_t damageDisplay = 0;
            __try { damageDisplay = *(uint16_t*)BATTLE_DAMAGE_DISPLAY_ADDR; }
            __except(EXCEPTION_EXECUTE_HANDLER) {}
            Log::Battle("BattleTTS: [SPRITE-POLL] NEW i=%d slot=%u kind=0x%02X val=%u "
                        "life=0x%02X style=0x%02X sec=%u ent=0x%08X dmg=%u (f=%u)",
                        i, cur.slot, cur.text_id, cur.value,
                        cur.lifetime, cur.style, cur.secondary, cur.entity_ptr,
                        (unsigned)damageDisplay, s_pollFrameCounter);

            // v0.13.81: popup-signal screenshots are now the primary
            // validation artifact for non-damage events (action-announces,
            // spell-cast labels, miss text, status popups). Cap raised to 60
            // per battle in sprite_spawn.inl. Audit pairs these to
            // [VALIDATE] log lines by timestamp.
            if (s_pollScreenshotNewCount < POLL_SCREENSHOT_NEW_MAX) {
                s_pollScreenshotNewCount++;
                PollSpriteScreenshot("NEW", cur.slot, cur.text_id, cur.value);
            }

            // v0.13.92: IMMEDIATE popup-spawn HP announcement trigger.
            // Screenshot analysis showed damage numbers are visible at spawn time
            // and disappear rapidly. Trigger announcements immediately for damage
            // popups rather than waiting for animation end. This applies to ALL
            // battle actions (player physical, magic, enemy attacks).
            // CRITICAL: Only trigger on popups where val equals actual damage amount.
            // Action popups have val=7 or similar IDs, true damage sprites have val=dmg.
            // Read expected damage from the damage display array (0x1A78C88 base + 4*slot)
            uint32_t expectedDamage = 0;
            __try {
                expectedDamage = *(uint32_t*)(0x1A78C88 + cur.slot * 4);
            } __except(EXCEPTION_EXECUTE_HANDLER) {
                expectedDamage = 0;
            }
            if (IsDamagePopupKind(cur.text_id) && cur.value > 0 && cur.value == expectedDamage) {
                Log::Battle("BattleTTS: [SPRITE-POLL] TRUE DAMAGE SPRITE detected "
                            "(kind=0x%02X, slot=%u, val=%u=dmg) - attempting immediate HP flush",
                            cur.text_id, cur.slot, cur.value);
                TriggerImmediateHPFlush("true-damage-sprite");
            }

            // v0.13.92: damage popup time-based diagnostic. No-op for
            // non-damage kinds; for damage kinds (0x01/0x02/0x08) start
            // tracking so OnTick can fire screenshots at each elapsed-ms gate.
            PopupLifeDiag_OnNew(i, cur, damageDisplay, s_pollFrameCounter);
        } else if (prev.valid && !cur.valid) {
            Log::Battle("BattleTTS: [SPRITE-POLL] DESPAWN i=%d slot=%u kind=0x%02X val=%u (f=%u)",
                        i, prev.slot, prev.text_id, prev.value,
                        s_pollFrameCounter);
            // v0.13.92: end damage popup tracking, log summary.
            PopupLifeDiag_OnDespawn(i, prev, s_pollFrameCounter);
        } else {
            if (prev.text_id != cur.text_id) {
                // text_id overwrite mid-animation — the Miss-detection signal.
                Log::Battle("BattleTTS: [SPRITE-POLL] KIND i=%d slot=%u 0x%02X->0x%02X "
                            "val=%u life=0x%02X (f=%u)",
                            i, cur.slot, prev.text_id, cur.text_id,
                            cur.value, cur.lifetime, s_pollFrameCounter);

                if (s_pollScreenshotTextIdCount < POLL_SCREENSHOT_TEXTID_MAX) {
                    s_pollScreenshotTextIdCount++;
                    PollSpriteScreenshot("KIND", cur.slot, cur.text_id, cur.value);
                }
            }

            if (prev.value != cur.value) {
                Log::Battle("BattleTTS: [SPRITE-POLL] VALUE i=%d slot=%u kind=0x%02X "
                            "%u->%u life=0x%02X (f=%u)",
                            i, cur.slot, cur.text_id,
                            prev.value, cur.value, cur.lifetime,
                            s_pollFrameCounter);
            }

            int prevBand = LifetimeBand(prev.lifetime);
            int curBand  = LifetimeBand(cur.lifetime);
            if (prevBand != curBand) {
                Log::Battle("BattleTTS: [SPRITE-POLL] LIFE i=%d slot=%u kind=0x%02X "
                            "val=%u 0x%02X->0x%02X band=%d->%d (f=%u)",
                            i, cur.slot, cur.text_id, cur.value,
                            prev.lifetime, cur.lifetime, prevBand, curBand,
                            s_pollFrameCounter);
            }

            // v0.13.92: damage popup time-based diagnostic.
            // No-op for non-damage popups; for damage popups (0x01/0x02/0x08)
            // logs the 20-byte record (diff/heartbeat) and fires DoGLCapture
            // at each elapsed-ms gate (250/500/1000/.../4000).
            PopupLifeDiag_OnTick(i, cur, s_pollFrameCounter);
        }
    }

    // Commit current as previous for next frame.
    for (int i = 0; i < POPUP_TRACK_MAX; i++) {
        s_prevRecords[i] = current[i];
    }
}

// ============================================================================
// v0.14.0 — Sprite pool data per-frame poll (THE VISIBILITY TIMER HUNT)
// ============================================================================
// Session 100 disassembly grep found that all 5 sprite callbacks identified
// in session 83 share a single pattern: each reads [slot+8] uint16 as a
// per-frame countdown timer, decrements it, and fires an action when it
// reaches 0. The 16-byte slot data lives at 0x1D28C44 + slot*16:
//
//   sub_48AC60 (TEXT_ONESHOT):  immediate, calls sub_47E220 with style 0x80
//   sub_48AC90 (TEXT_TIMED):    cmp [+8],0 / je render-via-sub_47E220 / dec [+8]
//   sub_48ACD0 (FLAG_MANAGER):  cmp [+8],dx / je set-flag-at-0x1D280C2
//   sub_47E030 (GENERAL_TASK):  cmp [+8],0 / je task-via-sub_500df0 / dec [+8]
//   sub_48E620 (COMPLEX_UPDATE):entity-aware processing (savemap update)
//
// And [slot+0xF] byte transitions to 1 when the slot fires its action.
//
// HYPOTHESIS: damage digit popups allocate a slot in this pool, set [+8]
// to a frame countdown matching the attack animation length, and fire
// sub_47E220 (the digit render queue) when the timer hits 0. The 0→fire
// transition IS the long-hunted visible-frame moment.
//
// PRIOR INVESTIGATIONS MISSED IT BECAUSE:
//   1. v0.13.94 polled DISP region at 0x01D28340 (display-data table,
//      separate 24-byte stride), not the sprite pool at 0x1D28C44.
//   2. v0.13.99 memdiff DID cover 0x1D28C44 but used stable-pre/post
//      sampling: a counter ticking 60→59→58→...→0 doesn't pass that filter.
//   3. sub_482C90 hook (v0.13.93's chosen sprite allocator hook) never
//      fired for damage events — BAT showed 0–1 calls/battle. Damage
//      popups use a DIFFERENT allocator path that bypasses sub_482C90.
//
// THIS POLL: per SwapBuffers, snapshot 256 bytes from 0x1D28C44 and diff
// against previous frame. Log specifically:
//   - TIMER-START:   [+8] uint16 transitions 0 → non-zero (slot armed)
//   - TIMER-EXPIRED: [+8] transitions non-zero → 0 (the smoking gun!)
//   - DONE-FLAG:     [+0xF] byte transitions 0 → non-zero (rendered)
//   - DELTA:         non-timer/non-flag bytes change (e.g. callback set,
//                    context pointer set when slot allocated)
//   - HEARTBEAT:     every 60 frames, dump active-slot state for context
//
// Gated on anim flag at 0x1D280C0 to suppress logging outside damage
// events. Hard cap of 2000 lines/battle to prevent runaway growth if
// hypothesis is wrong about which slots are active.
//
// EXPECTED OUTCOMES:
//   BEST:  TIMER-EXPIRED line at the same frame as ROI [yU spike] line
//          → confirmed hook target. v0.14.x ships hook on sub_48AC90 entry,
//          checks args, fires announcement at expiry.
//   MIDDLE:TIMER-EXPIRED fires but at a different frame than yU spike
//          → timer is in this pool but for a different sprite (spell anim,
//          enemy reaction, etc.). Filter and try again.
//   WORST: pool stays static through damage events → damage uses a
//          separate render path. Search continues with broader memdiff.

static const uint32_t SPRITE_POOL_DATA_ADDR    = 0x01D28C44;
static const uint32_t SPRITE_POOL_ANIM_FLAG    = 0x01D280C0;
static const int      SPRITE_POOL_SLOT_COUNT   = 16;
static const int      SPRITE_POOL_SLOT_STRIDE  = 16;
static const int      SPRITE_POOL_TOTAL_SIZE   = SPRITE_POOL_SLOT_COUNT * SPRITE_POOL_SLOT_STRIDE; // 256
static const int      SPRITE_POOL_DELTA_LOG_CAP = 2000;
static const uint32_t SPRITE_POOL_HEARTBEAT_PERIOD = 60;

static uint8_t  s_spritePoolPrev[SPRITE_POOL_TOTAL_SIZE] = {};
static bool     s_spritePoolValid = false;
static uint32_t s_spritePoolFrameCounter = 0;
static int      s_spritePoolLogCount = 0;

static void FormatSpriteSlotHex(const uint8_t* slot, char* buf, size_t bufSize)
{
    int p = 0;
    for (int b = 0; b < SPRITE_POOL_SLOT_STRIDE && p < (int)bufSize - 4; b++) {
        p += snprintf(buf + p, bufSize - p, "%02X ", slot[b]);
    }
}

static void PollSpritePool()
{
    s_spritePoolFrameCounter++;

    // Read current snapshot with SEH guard. Battle memory may not be
    // mapped on the menu/title screens — skip silently in that case.
    uint8_t cur[SPRITE_POOL_TOTAL_SIZE];
    bool readOk = false;
    __try {
        memcpy(cur, (void*)(uintptr_t)SPRITE_POOL_DATA_ADDR, SPRITE_POOL_TOTAL_SIZE);
        readOk = true;
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
    if (!readOk) return;

    uint8_t animFlag = 0;
    __try { animFlag = *(uint8_t*)(uintptr_t)SPRITE_POOL_ANIM_FLAG; }
    __except(EXCEPTION_EXECUTE_HANDLER) {}

    // First-call seed: store baseline and exit. Avoids logging a giant
    // delta on the first frame against zero-initialized prev buffer.
    if (!s_spritePoolValid) {
        memcpy(s_spritePoolPrev, cur, SPRITE_POOL_TOTAL_SIZE);
        s_spritePoolValid = true;
        return;
    }

    bool gateActive = (animFlag != 0);
    bool capReached = (s_spritePoolLogCount >= SPRITE_POOL_DELTA_LOG_CAP);
    bool shouldLog  = gateActive && !capReached;

    if (shouldLog) {
        for (int s = 0; s < SPRITE_POOL_SLOT_COUNT; s++) {
            int base = s * SPRITE_POOL_SLOT_STRIDE;
            const uint8_t* prev = &s_spritePoolPrev[base];
            const uint8_t* curS = &cur[base];

            if (memcmp(prev, curS, SPRITE_POOL_SLOT_STRIDE) == 0) continue;

            // Read timer (uint16 LE at +8) and done-flag (byte at +0xF).
            uint16_t prevTimer = (uint16_t)(prev[8] | (prev[9] << 8));
            uint16_t curTimer  = (uint16_t)(curS[8] | (curS[9] << 8));
            uint8_t  prevFlag  = prev[15];
            uint8_t  curFlag   = curS[15];

            char prevHex[64] = {};
            char curHex[64]  = {};
            FormatSpriteSlotHex(prev, prevHex, sizeof(prevHex));
            FormatSpriteSlotHex(curS, curHex,  sizeof(curHex));

            // TIMER-START: 0 → non-zero (slot just armed).
            if (prevTimer == 0 && curTimer != 0) {
                Log::Battle("BattleTTS: [SPRITE-POOL-DIAG] TIMER-START slot=%d frame=%u animFlag=0x%02X "
                            "initial=0x%04X cb=0x%02X%02X%02X%02X ctx=0x%02X%02X%02X%02X cur=[%s]",
                            s, s_spritePoolFrameCounter, animFlag,
                            curTimer,
                            curS[3], curS[2], curS[1], curS[0],     // bytes 0..3 as LE dword (callback?)
                            curS[7], curS[6], curS[5], curS[4],     // bytes 4..7 as LE dword (context?)
                            curHex);
                s_spritePoolLogCount++;
            }

            // TIMER-EXPIRED: non-zero → 0 (THE SMOKING GUN MOMENT).
            if (prevTimer != 0 && curTimer == 0) {
                Log::Battle("BattleTTS: [SPRITE-POOL-DIAG] TIMER-EXPIRED slot=%d frame=%u animFlag=0x%02X "
                            "prevTimer=0x%04X cb=0x%02X%02X%02X%02X ctx=0x%02X%02X%02X%02X cur=[%s]",
                            s, s_spritePoolFrameCounter, animFlag,
                            prevTimer,
                            curS[3], curS[2], curS[1], curS[0],
                            curS[7], curS[6], curS[5], curS[4],
                            curHex);
                s_spritePoolLogCount++;
            }

            // DONE-FLAG: 0 → non-zero (slot just fired its action).
            if (prevFlag == 0 && curFlag != 0) {
                Log::Battle("BattleTTS: [SPRITE-POOL-DIAG] DONE-FLAG slot=%d frame=%u animFlag=0x%02X "
                            "flag=0x%02X timer=0x%04X cur=[%s]",
                            s, s_spritePoolFrameCounter, animFlag,
                            curFlag, curTimer, curHex);
                s_spritePoolLogCount++;
            }

            // DELTA on non-timer / non-flag bytes only. The timer ticks
            // every frame and would flood the log if included — it shows
            // up implicitly via TIMER-START / TIMER-EXPIRED transitions.
            // Bytes excluded from delta: +0x08, +0x09 (timer uint16),
            // +0x0F (done flag).
            char deltaStr[256] = {};
            int dp = 0;
            int changedNonTimer = 0;
            for (int b = 0; b < SPRITE_POOL_SLOT_STRIDE; b++) {
                if (b == 8 || b == 9 || b == 15) continue;  // skip timer + flag
                if (prev[b] != curS[b]) {
                    if (changedNonTimer < 8 && dp < (int)sizeof(deltaStr) - 32) {
                        dp += snprintf(deltaStr + dp, sizeof(deltaStr) - dp,
                                        "[+0x%02X: 0x%02X->0x%02X] ",
                                        b, prev[b], curS[b]);
                    }
                    changedNonTimer++;
                }
            }
            if (changedNonTimer > 0) {
                Log::Battle("BattleTTS: [SPRITE-POOL-DIAG] DELTA slot=%d frame=%u animFlag=0x%02X "
                            "timer=0x%04X flag=0x%02X n=%d %s%s",
                            s, s_spritePoolFrameCounter, animFlag,
                            curTimer, curFlag, changedNonTimer, deltaStr,
                            changedNonTimer > 8 ? "...(truncated)" : "");
                s_spritePoolLogCount++;
            }
        }

        // Periodic heartbeat: dump state of all slots that look "active"
        // (timer non-zero OR flag non-zero OR any non-zero byte). Cheap
        // way to see counters ticking down between transitions.
        if (s_spritePoolFrameCounter % SPRITE_POOL_HEARTBEAT_PERIOD == 0) {
            for (int s = 0; s < SPRITE_POOL_SLOT_COUNT; s++) {
                int base = s * SPRITE_POOL_SLOT_STRIDE;
                const uint8_t* curS = &cur[base];
                bool anyNonZero = false;
                for (int b = 0; b < SPRITE_POOL_SLOT_STRIDE; b++) {
                    if (curS[b] != 0) { anyNonZero = true; break; }
                }
                if (!anyNonZero) continue;

                uint16_t timer = (uint16_t)(curS[8] | (curS[9] << 8));
                char curHex[64] = {};
                FormatSpriteSlotHex(curS, curHex, sizeof(curHex));
                Log::Battle("BattleTTS: [SPRITE-POOL-DIAG] HEARTBEAT slot=%d frame=%u animFlag=0x%02X "
                            "timer=0x%04X flag=0x%02X cur=[%s]",
                            s, s_spritePoolFrameCounter, animFlag,
                            timer, curS[15], curHex);
                s_spritePoolLogCount++;
            }
        }
    }

    // Always update prev snapshot for next frame so transitions are
    // detected accurately even if logging is gated off.
    memcpy(s_spritePoolPrev, cur, SPRITE_POOL_TOTAL_SIZE);
}

static BOOL WINAPI HookedSwapBuffers(HDC hdc)
{
    if (s_captureRequested) {
        s_captureRequested = false;
        DoGLCapture();
    }

    // v0.14.0: sprite pool poll FIRST (before any other path can perturb
    // the read). The pool data at 0x1D28C44 is read-only from our side;
    // we just snapshot and diff. See block comment above PollSpritePool
    // for the full hypothesis and expected outcomes.
    PollSpritePool();

    // v0.13.95: ROI calibration auto-capture. Reads anim flag at
    // 0x01D280C0 directly each frame, drives the pre-buffer ring + active
    // event capture state machine, and spawns a worker thread on finalize.
    // No-op when ROI_CALIBRATION_CAPTURE is disabled. Runs first so its
    // glReadPixels happens before any other path's reads in case the GL
    // state mutates downstream.
    RoiCalib_OnSwapBuffers();

    // v0.13.75: frame-level poll of the popup sprite record table. Runs
    // after the existing flag-based capture so the two paths can't race
    // on s_captureBasePath. Poll may itself call DoGLCapture() inline to
    // snapshot sprite-lifecycle moments (capped per battle).
    PollPopupRecords();

    // v0.13.78: drain deferred text-sprite log filled by sub_495280 and
    // sub_4952F0 hooks. Runs here because this is a safe context (game's
    // render thread, SEH allowed, Log allowed) whereas the hook bodies
    // themselves fire on arbitrary game threads and must stay minimal.
    DrainDeferredTextSpriteLog();

    return s_origSwapBuffers(hdc);
}

static void InstallSwapBuffersHook()
{
    HMODULE hGdi32 = GetModuleHandleA("gdi32.dll");
    if (!hGdi32) {
        Log::Battle("BattleTTS: [SWAP-HOOK] gdi32.dll not found");
        return;
    }
    void* pSwap = (void*)GetProcAddress(hGdi32, "SwapBuffers");
    if (!pSwap) {
        Log::Battle("BattleTTS: [SWAP-HOOK] SwapBuffers not found");
        return;
    }
    MH_STATUS st = MH_CreateHook(pSwap, (void*)&HookedSwapBuffers, (void**)&s_origSwapBuffers);
    if (st != MH_OK) {
        Log::Battle("BattleTTS: [SWAP-HOOK] MH_CreateHook failed: %d", (int)st);
        return;
    }
    st = MH_EnableHook(pSwap);
    if (st != MH_OK) {
        Log::Battle("BattleTTS: [SWAP-HOOK] MH_EnableHook failed: %d", (int)st);
        return;
    }
    s_swapHookInstalled = true;
    Log::Battle("BattleTTS: [SWAP-HOOK] SwapBuffers hooked OK");
}

static void CaptureScreenshot(const char* basePath)
{
    // Set capture path and flag — actual capture happens in HookedSwapBuffers
    strncpy(s_captureBasePath, basePath, sizeof(s_captureBasePath) - 1);
    s_captureBasePath[sizeof(s_captureBasePath) - 1] = '\0';
    s_captureRequested = true;
    // Wait briefly for the render thread to process it
    for (int i = 0; i < 10 && s_captureRequested; i++) {
        Sleep(16);  // ~1 frame at 60fps
    }
    if (s_captureRequested) {
        Log::Battle("BattleTTS: [VICTORY-SCREENSHOT] Capture not processed after 160ms");
        s_captureRequested = false;
    }
}

// v0.14.45: Removed DumpVictoryScreenData, DumpVictoryStep, PollVictoryScreen
// and supporting state (savemap address aliases, GF_NAMES_STATIC). All were
// part of the F12-triggered victory step diagnostic, retired in v0.14.45.
