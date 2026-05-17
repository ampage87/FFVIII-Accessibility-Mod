// field_dialog_helpers.inl -- pointer validation, window-object accessors,
// text trimming, suffix detection, FNV-1a hash, MinHook detour helper.
//
// Included after state.inl. Provides the low-level utilities that scan.inl,
// show_dialog.inl, opcodes.inl, diag.inl, and lifecycle.inl all build on.

// ============================================================================
// Pointer validation
// ============================================================================

static bool IsValidTextPointer(const char* ptr)
{
    uintptr_t addr = (uintptr_t)ptr;
    // v04.18: lowered from 0x00A00000 to 0x00010000 to catch thought text
    // that may use lower-address buffers. ProbePointer() still provides safety.
    if (addr >= 0x00010000 && addr <= 0x30000000) return true;
    // v0.15.6.2: whitelist DialogInject's static override buffer. Our buffer
    // lives in the DLL data section above 0x30000000, outside the FF8
    // heap-range heuristic above. Without this whitelist, v0.15.6.1's
    // post-ASK pointer swap landed but ScanAndSpeakChoiceWindows silently
    // skipped the slot and Hook_show_dialog fell back to text_data2 (still
    // holding the engine's natural prompt). The buffer's location is stable
    // for the DLL's lifetime so range comparison is safe.
    const unsigned char* obStart = ::DialogInject::GetOverrideBufferStart();
    if (obStart != nullptr) {
        uintptr_t obStartAddr = (uintptr_t)obStart;
        uintptr_t obEndAddr   = obStartAddr + (uintptr_t)::DialogInject::GetOverrideBufferSize();
        if (addr >= obStartAddr && addr < obEndAddr) return true;
    }
    return false;
}

static bool ProbePointer(const char* ptr)
{
    if (!ptr) return false;
    __try {
        volatile uint8_t probe = *(const uint8_t*)ptr;
        (void)probe;
        return true;
    }
    __except(EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

// SEH probe must be in its own function -- MSVC can't mix __try with C++ objects
static bool ProbeGetstrResult(const char* ptr)
{
    if (!ptr) return false;
    __try {
        volatile uint8_t probe = *(const uint8_t*)ptr;
        (void)probe;
        return (probe != 0x00);  // Also reject empty strings
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

// ============================================================================
// Window helpers -- accessors into the 0x3C-byte window object struct
// ============================================================================

static uint8_t* GetWindowObj(int index)
{
    if (!FF8Addresses::pWindowsArray) return nullptr;
    return FF8Addresses::pWindowsArray + (index * WIN_OBJ_SIZE);
}

static char* GetWinText1(uint8_t* winObj)
{
    if (!winObj) return nullptr;
    return *(char**)(winObj + WIN_OBJ_TEXT1_OFFSET);
}

static char* GetWinText2(uint8_t* winObj)
{
    if (!winObj) return nullptr;
    return *(char**)(winObj + WIN_OBJ_TEXT2_OFFSET);
}

static int16_t GetWinOpenCloseTransition(uint8_t* winObj)
{
    if (!winObj) return 0;
    return *(int16_t*)(winObj + WIN_OBJ_OPEN_CLOSE_OFFSET);
}

// ============================================================================
// Suffix detection for multi-page dialog dedup
// ============================================================================

static bool IsSuffixOrSubstring(const std::string& fullText, const std::string& newText)
{
    if (fullText.empty() || newText.empty()) return false;
    if (newText.length() >= fullText.length()) return false;

    // Check if fullText ends with newText
    size_t pos = fullText.length() - newText.length();
    if (fullText.compare(pos, newText.length(), newText) == 0)
        return true;

    // Try stripping leading punctuation/whitespace
    size_t trimStart = newText.find_first_not_of(" .\"'");
    if (trimStart != std::string::npos && trimStart > 0) {
        std::string trimmed = newText.substr(trimStart);
        if (!trimmed.empty() && trimmed.length() < fullText.length()) {
            pos = fullText.length() - trimmed.length();
            if (fullText.compare(pos, trimmed.length(), trimmed) == 0)
                return true;
        }
    }

    // Check if fullText contains newText anywhere
    if (fullText.find(newText) != std::string::npos)
        return true;

    return false;
}

// ============================================================================
// Helper: trim whitespace and leading/trailing periods
// ============================================================================

static std::string TrimDecoded(const std::string& text)
{
    // v04.23: preserve ellipsis-only lines instead of discarding them
    size_t start = text.find_first_not_of(" .");
    size_t end = text.find_last_not_of(" .");
    if (start == std::string::npos) {
        // All dots/spaces -- check if there are meaningful dots
        size_t dotCount = 0;
        for (char c : text) if (c == '.') dotCount++;
        if (dotCount >= 3) return "(...)";
        return "";
    }
    return text.substr(start, end - start + 1);
}

// ============================================================================
// v04.23: FNV-1a hash for detecting in-place content changes
// ============================================================================

static uint32_t fnv1a_prefix(const uint8_t* p, size_t n)
{
    uint32_t h = 2166136261u;
    for (size_t i = 0; i < n; i++) h = (h ^ p[i]) * 16777619u;
    return h;
}

// ============================================================================
// MinHook detour creation helper
// ============================================================================

static bool CreateDetourHook(uint32_t targetAddr, OpcodeHandler_t newHandler,
                              OpcodeHandler_t* outOriginal, const char* label)
{
    if (targetAddr == 0) {
        Log::Dialog("FieldDialog: Cannot hook %s - address is null", label);
        return false;
    }

    MH_STATUS status = MH_CreateHook(
        (LPVOID)targetAddr, (LPVOID)newHandler, (LPVOID*)outOriginal);

    if (status != MH_OK) {
        Log::Dialog("FieldDialog: MH_CreateHook failed for %s at 0x%08X (status=%d)",
                   label, targetAddr, (int)status);
        return false;
    }

    Log::Dialog("FieldDialog: Hooked %s: target=0x%08X trampoline=0x%08X",
               label, targetAddr, (uint32_t)(uintptr_t)*outOriginal);
    return true;
}
