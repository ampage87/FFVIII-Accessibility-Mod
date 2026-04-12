// battle_tts_screenshot.inl — GL screenshot capture, memory diff, victory step diagnostics
//
// Included by battle_tts.cpp BEFORE battle_tts_victory.inl.
// CaptureScreenshot() is called by VictoryAutoCapture in victory.inl.
//
// Extracted from battle_tts.cpp v0.13.44 (session 63, purely mechanical split).

// ============================================================================
// Memory diff snapshots (F12 step capture)
// ============================================================================

// On each F12 press, snapshot candidate regions and log only changed bytes.
static const int DIFF_REGION_COUNT = 1;
static struct {
    uint32_t addr;
    int size;
    const char* label;
} s_diffRegions[DIFF_REGION_COUNT] = {
    { 0x1CFF000, 0x81000, "battle_state_full" },  // 0x1CFF000-0x1D80000 (528KB)
};
static const int DIFF_TOTAL_SIZE = 0x81000;
static uint8_t s_diffSnapPrev[0x81000];
static bool s_diffSnapValid = false;

static void DiffMemorySnapshots(int step)
{
    uint8_t snapCur[sizeof(s_diffSnapPrev)];
    int offset = 0;
    
    // Read all regions into current snapshot
    for (int r = 0; r < DIFF_REGION_COUNT; r++) {
        __try {
            memcpy(snapCur + offset, (void*)(uintptr_t)s_diffRegions[r].addr, s_diffRegions[r].size);
        } __except(EXCEPTION_EXECUTE_HANDLER) {
            memset(snapCur + offset, 0xDE, s_diffRegions[r].size);  // poison on exception
        }
        offset += s_diffRegions[r].size;
    }
    
    if (!s_diffSnapValid) {
        // First snapshot — just store it
        memcpy(s_diffSnapPrev, snapCur, sizeof(s_diffSnapPrev));
        s_diffSnapValid = true;
        Log::Battle("BattleTTS: [VICTORY-DIFF] Step %d: baseline snapshot stored (%d bytes)", step, DIFF_TOTAL_SIZE);
        return;
    }
    
    // Compare and log changes (cap at 50 to avoid log flooding)
    int changeCount = 0;
    static const int MAX_LOG_CHANGES = 50;
    offset = 0;
    for (int r = 0; r < DIFF_REGION_COUNT; r++) {
        for (int b = 0; b < s_diffRegions[r].size; b++) {
            int idx = offset + b;
            if (snapCur[idx] != s_diffSnapPrev[idx]) {
                if (changeCount < MAX_LOG_CHANGES) {
                    uint32_t addr = s_diffRegions[r].addr + b;
                    Log::Battle("BattleTTS: [VICTORY-DIFF] Step %d: 0x%08X (%s+0x%05X) changed: 0x%02X -> 0x%02X",
                               step, addr, s_diffRegions[r].label, b,
                               s_diffSnapPrev[idx], snapCur[idx]);
                }
                changeCount++;
            }
        }
        offset += s_diffRegions[r].size;
    }
    
    Log::Battle("BattleTTS: [VICTORY-DIFF] Step %d: %d bytes changed", step, changeCount);
    
    // Store current as previous for next diff
    memcpy(s_diffSnapPrev, snapCur, sizeof(s_diffSnapPrev));
}

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

static BOOL WINAPI HookedSwapBuffers(HDC hdc)
{
    if (s_captureRequested) {
        s_captureRequested = false;
        DoGLCapture();
    }
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

// ============================================================================
// Victory screen diagnostic dump (savemap cross-reference)
// ============================================================================

// Savemap addresses for cross-reference (some already defined in other .inl files)
static const uint32_t SAVEMAP_CHAR_BASE = 0x1CFE0E8;   // Squall
// SAVEMAP_CHAR_STRIDE already defined in battle_tts_menu.inl
// SAVEMAP_GF_BASE already defined in battle_tts_hp.inl
// SAVEMAP_GF_STRIDE already defined in battle_tts_hp.inl
static const uint32_t SAVEMAP_PARTY_ADDR = 0x1CFE74C;   // party composition
static const uint32_t SAVEMAP_ITEMS_ADDR = 0x1CFE79C;   // item array (198x2)
static const uint32_t SAVEMAP_VICTORY_COUNT = 0x1CFE934; // victory counter

static const char* GF_NAMES_STATIC[] = {
    "Quetzalcoatl", "Shiva", "Ifrit", "Siren", "Brothers", "Diablos",
    "Carbuncle", "Leviathan", "Pandemonia", "Cerberus", "Alexander",
    "Doomtrain", "Bahamut", "Cactuar", "Tonberry", "Eden"
};

static void DumpVictoryScreenData()
{
    Log::Battle("BattleTTS: [VICTORY-DIAG] === VICTORY SCREEN MEMORY DUMP ===");
    
    // 1. Dump party composition
    __try {
        uint8_t* party = (uint8_t*)SAVEMAP_PARTY_ADDR;
        Log::Battle("BattleTTS: [VICTORY-DIAG] Party: [%u, %u, %u, %u]",
                   party[0], party[1], party[2], party[3]);
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        Log::Battle("BattleTTS: [VICTORY-DIAG] Party read EXCEPTION");
    }
    
    // 2. Dump per-character EXP from savemap
    __try {
        for (int c = 0; c < 8; c++) {
            uint8_t* ch = (uint8_t*)(SAVEMAP_CHAR_BASE + c * SAVEMAP_CHAR_STRIDE);
            uint32_t exp = *(uint32_t*)(ch + 0x04);
            uint16_t curHP = *(uint16_t*)(ch + 0x00);
            uint16_t maxHP = *(uint16_t*)(ch + 0x02);
            uint8_t exists = *(ch + 0x94);
            if (exists || exp > 0) {
                Log::Battle("BattleTTS: [VICTORY-DIAG] Char%d: EXP=%u HP=%u/%u exists=%u",
                           c, exp, curHP, maxHP, exists);
            }
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        Log::Battle("BattleTTS: [VICTORY-DIAG] Char EXP read EXCEPTION");
    }
    
    // 3. Dump victory count
    __try {
        uint32_t vicCount = *(uint32_t*)SAVEMAP_VICTORY_COUNT;
        Log::Battle("BattleTTS: [VICTORY-DIAG] Victory count: %u", vicCount);
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
    
    // 4. Dump GF EXP + completed abilities for junctioned GFs
    __try {
        for (int g = 0; g < 16; g++) {
            uint8_t* gf = (uint8_t*)(SAVEMAP_GF_BASE + g * SAVEMAP_GF_STRIDE);
            uint8_t existsFlag = *(gf + 0x11);
            if (!existsFlag) continue;
            uint32_t gfExp = *(uint32_t*)(gf + 0x0C);
            uint8_t learningIdx = *(gf + 0x41);
            uint8_t apProgress = *(gf + 0x24 + learningIdx);
            Log::Battle("BattleTTS: [VICTORY-DIAG] GF%d(%s): EXP=%u learnIdx=%u apProg=%u",
                       g, GF_NAMES_STATIC[g], gfExp, learningIdx, apProgress);
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        Log::Battle("BattleTTS: [VICTORY-DIAG] GF read EXCEPTION");
    }
    
    // 5. Dump candidate transient buffer region: 0x1CFF570 through 0x1D00000
    __try {
        Log::Battle("BattleTTS: [VICTORY-DIAG] --- Region 0x1CFF570-0x1CFF700 (400 bytes) ---");
        uint8_t* base = (uint8_t*)0x1CFF570;
        for (int off = 0; off < 400; off += 16) {
            char hex[80] = {};
            int p = 0;
            for (int b = 0; b < 16 && off + b < 400; b++)
                p += snprintf(hex + p, sizeof(hex) - p, "%02X ", base[off + b]);
            char vals[80] = {};
            int vp = 0;
            for (int w = 0; w < 8 && (off + w*2 + 1) < 400; w++) {
                uint16_t v = *(uint16_t*)(base + off + w*2);
                vp += snprintf(vals + vp, sizeof(vals) - vp, "%5u ", v);
            }
            Log::Battle("BattleTTS: [VICTORY-DIAG] +%03X: %s | %s", off, hex, vals);
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        Log::Battle("BattleTTS: [VICTORY-DIAG] Region 0x1CFF570 read EXCEPTION");
    }
    
    // 6. Also dump 0x1CFF6D0-0x1CFF780 -- around encounter_id (0x1CFF6E0)
    __try {
        Log::Battle("BattleTTS: [VICTORY-DIAG] --- Region 0x1CFF6D0-0x1CFF780 (176 bytes) ---");
        uint8_t* base = (uint8_t*)0x1CFF6D0;
        for (int off = 0; off < 176; off += 16) {
            char hex[80] = {};
            int p = 0;
            for (int b = 0; b < 16 && off + b < 176; b++)
                p += snprintf(hex + p, sizeof(hex) - p, "%02X ", base[off + b]);
            char vals[80] = {};
            int vp = 0;
            for (int w = 0; w < 8 && (off + w*2 + 1) < 176; w++) {
                uint16_t v = *(uint16_t*)(base + off + w*2);
                vp += snprintf(vals + vp, sizeof(vals) - vp, "%5u ", v);
            }
            Log::Battle("BattleTTS: [VICTORY-DIAG] +%03X: %s | %s",
                       0x1CFF6D0 - 0x1CFF570 + off, hex, vals);
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        Log::Battle("BattleTTS: [VICTORY-DIAG] Region 0x1CFF6D0 read EXCEPTION");
    }
    
    // 7. Wider sweep: 0x1D28800-0x1D28F00 (battle_vars area from OpenFF8)
    __try {
        Log::Battle("BattleTTS: [VICTORY-DIAG] --- Region 0x1D28D80-0x1D28E20 (160 bytes, battle_vars) ---");
        uint8_t* base = (uint8_t*)0x1D28D80;
        for (int off = 0; off < 160; off += 16) {
            char hex[80] = {};
            int p = 0;
            for (int b = 0; b < 16; b++)
                p += snprintf(hex + p, sizeof(hex) - p, "%02X ", base[off + b]);
            char vals[80] = {};
            int vp = 0;
            for (int w = 0; w < 8; w++) {
                uint16_t v = *(uint16_t*)(base + off + w*2);
                vp += snprintf(vals + vp, sizeof(vals) - vp, "%5u ", v);
            }
            Log::Battle("BattleTTS: [VICTORY-DIAG] 0x%08X: %s | %s",
                       (uint32_t)(uintptr_t)(base + off), hex, vals);
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        Log::Battle("BattleTTS: [VICTORY-DIAG] battle_vars region EXCEPTION");
    }
    
    // 8. Dump items array (first 20 slots) for item drop cross-reference
    __try {
        Log::Battle("BattleTTS: [VICTORY-DIAG] --- Items array (first 20 slots) ---");
        uint8_t* items = (uint8_t*)SAVEMAP_ITEMS_ADDR;
        for (int i = 0; i < 20; i++) {
            uint8_t id = items[i*2];
            uint8_t qty = items[i*2 + 1];
            if (id > 0) {
                Log::Battle("BattleTTS: [VICTORY-DIAG] Item[%d]: id=%u qty=%u", i, id, qty);
            }
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
    
    Log::Battle("BattleTTS: [VICTORY-DIAG] === END DUMP ===");
}

// ============================================================================
// Victory step capture (F12-triggered diagnostic)
// ============================================================================

static void DumpVictoryStep(int step)
{
    Log::Battle("BattleTTS: [VICTORY-STEP] === STEP %d CAPTURE ===", step);
    
    // Capture screenshot
    char screenshotPath[512];
    snprintf(screenshotPath, sizeof(screenshotPath),
             "C:\\Users\\ampag\\OneDrive\\Documents\\FFVIII-Accessibility-Mod"
             "\\FF8_OriginalPC_mod\\Logs\\victory_step_%d", step);
    CaptureScreenshot(screenshotPath);
    
    // v0.12.95: Memory diff between steps
    DiffMemorySnapshots(step);
    
    // v0.13.28: Log savemap EXP + focused victory buffer for EXP countdown tracking
    __try {
        uint8_t party[4] = {};
        memcpy(party, (void*)VICTORY_PARTY_ADDR, 4);
        for (int ps = 0; ps < 3; ps++) {
            if (party[ps] >= 11) continue;
            uint8_t* ch = (uint8_t*)(0x1CFE0E8 + party[ps] * 0x98);
            uint32_t curExp = *(uint32_t*)(ch + 0x04);
            uint32_t preExp = s_preBattleExpSnapValid ? s_preBattleExpAll[party[ps]] : 0;
            Log::Battle("BattleTTS: [VICTORY-STEP] Step %d: %s savemap_EXP=%u pre=%u delta=%d",
                       step, GetCharNameById(party[ps]), curExp, preExp, (int)(curExp - preExp));
        }
        // Focused hex dump of VICTORY_EXP_BASE region (64 bytes)
        Log::Battle("BattleTTS: [VICTORY-STEP] Step %d: EXP region 0x%08X:", step, VICTORY_EXP_BASE);
        uint8_t* expBase = (uint8_t*)VICTORY_EXP_BASE;
        for (int off = 0; off < 64; off += 16) {
            char hx[80] = {};
            int hp = 0;
            for (int b = 0; b < 16; b++)
                hp += snprintf(hx + hp, sizeof(hx) - hp, "%02X ", expBase[off + b]);
            char vals[80] = {};
            int vp = 0;
            for (int w = 0; w < 8; w++) {
                uint16_t v = *(uint16_t*)(expBase + off + w * 2);
                vp += snprintf(vals + vp, sizeof(vals) - vp, "%5u ", v);
            }
            Log::Battle("BattleTTS: [VICTORY-STEP]   +%02X: %s| %s", off, hx, vals);
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
    
    // Also dump a wider view of the 0x1CFF570 region with uint32 interpretation
    __try {
        Log::Battle("BattleTTS: [VICTORY-STEP] --- uint32 view of 0x1CFF570-0x1CFF5F0 ---");
        uint32_t* base32 = (uint32_t*)0x1CFF570;
        for (int i = 0; i < 32; i += 4) {
            Log::Battle("BattleTTS: [VICTORY-STEP] +%03X: %10u %10u %10u %10u",
                       i * 4, base32[i], base32[i+1], base32[i+2], base32[i+3]);
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
    
    Log::Battle("BattleTTS: [VICTORY-STEP] === END STEP %d ===", step);
}

// ============================================================================
// Victory screen polling (mode 5 auto-dump + F12 step capture)
// ============================================================================

static void PollVictoryScreen()
{
    // Auto-dump on first entry (after 1s delay)
    if (!s_victoryDumpDone) {
        DWORD elapsed = GetTickCount() - s_victoryEntryTime;
        if (elapsed >= 1000) {
            s_victoryDumpDone = true;
            Log::Battle("BattleTTS: [VICTORY] Auto-dump at mode 5 + %ums", elapsed);
            DumpVictoryStep(0);  // step 0 = auto-capture on entry
        }
    }
    
    // F12 step capture
    bool f12Down = (GetAsyncKeyState(VK_F12) & 0x8000) != 0;
    bool f12Pressed = f12Down && !s_victoryF12WasDown;
    s_victoryF12WasDown = f12Down;
    
    if (f12Pressed) {
        s_victoryStepCount++;
        Log::Battle("BattleTTS: [VICTORY] F12 pressed -- capturing step %d", s_victoryStepCount);
        DumpVictoryStep(s_victoryStepCount);
        
        // Announce step number so Aaron knows it captured
        char buf[64];
        snprintf(buf, sizeof(buf), "Step %d captured.", s_victoryStepCount);
        ScreenReader::Speak(buf, true);
    }
}
