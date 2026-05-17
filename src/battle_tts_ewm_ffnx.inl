// battle_tts_ewm_ffnx.inl — FFNx GF loading counter hook (signature scan + MinHook).
// Included from battle_tts_ewm.inl. Do not compile independently.
// v0.16.4: Extracted from battle_tts_ewm.inl for size compliance.
//
// ============================================================================
// v0.10.77: FFNx GF loading counter hook
// ============================================================================
// FFNx (not the vanilla engine) writes to compStats[slot]+0x14 (master GF loading
// counter). Confirmed v0.10.76 via hardware write BP: all writes come from FFNx
// DLL space. Our ATB hook sandwich on +0x14 had no effect because FFNx overwrites
// the value on a separate code path.
//
// Strategy: find FFNx's module at runtime via the JMP at set_midi_volume (0x0046BB40),
// scan for the signature B9 16 F0 CF 01 66 89 06, walk backward to find the
// function entry, and MinHook it with the same cap-at-max-1 sandwich.
// ============================================================================

// s_ffnxGFHookInstalled defined in battle_tts.cpp main statics block.
// s_ffnxHookCallCount defined in battle_tts.cpp main statics block.

static void __cdecl HookedFFNxBattleUpdate(void)
{
    InterlockedIncrement(&s_ffnxHookCallCount);

    // If not capping GF, or no GF is loading, just call through
    if (!s_ewmCapGF) {
        s_originalFFNxBattleUpdate();
        return;
    }

    // Check if a GF is actively loading
    uint8_t gfActive = 0;
    int8_t gfSlot = -1;
    __try { gfActive = *(uint8_t*)0x01D76971; } __except(EXCEPTION_EXECUTE_HANDLER) {}
    __try { gfSlot = *(int8_t*)0x01D76970; } __except(EXCEPTION_EXECUTE_HANDLER) {}

    if (gfActive != 1 || gfSlot < 0 || gfSlot >= BATTLE_ALLY_SLOTS) {
        // No GF loading — call through unmodified
        s_originalFFNxBattleUpdate();
        return;
    }

    // Sandwich: save compStats[gfSlot]+0x14, call original, restore+cap at max-1
    uint8_t* cs = (uint8_t*)(BATTLE_COMP_STATS_BASE + gfSlot * BATTLE_COMP_STATS_STRIDE);
    uint16_t* pGFLoad = (uint16_t*)(cs + 0x14);
    uint16_t savedLoad = *pGFLoad;
    uint16_t gfMax = *(uint16_t*)(cs + 0x16);

    s_originalFFNxBattleUpdate();

    // After the call, FFNx may have incremented +0x14.
    // Compute the new value and cap at max-1.
    uint16_t newLoad = *pGFLoad;
    if (gfMax > 1 && newLoad >= gfMax) {
        *pGFLoad = gfMax - 1;  // cap: prevent GF from firing
    }
}

// Find FFNx module base by following the E9 JMP at set_midi_volume (0x0046BB40).
// Returns 0 on failure.
static uint32_t FindFFNxModuleBase(void)
{
    __try {
        uint8_t* pSetMidi = (uint8_t*)0x0046BB40;
        if (*pSetMidi != 0xE9) {
            Log::Battle("BattleTTS: [FFNx-GF] set_midi_volume @0x0046BB40 is not a JMP (byte=0x%02X)",
                       (unsigned)*pSetMidi);
            return 0;
        }
        // E9 rel32: target = addr + 5 + *(int32_t*)(addr+1)
        int32_t rel = *(int32_t*)(pSetMidi + 1);
        uint32_t target = 0x0046BB40 + 5 + rel;
        Log::Battle("BattleTTS: [FFNx-GF] set_midi_volume JMP target = 0x%08X", target);

        // Use VirtualQuery to find the allocation base (= module base)
        MEMORY_BASIC_INFORMATION mbi = {};
        if (VirtualQuery((LPCVOID)target, &mbi, sizeof(mbi)) == 0) {
            Log::Battle("BattleTTS: [FFNx-GF] VirtualQuery failed for 0x%08X", target);
            return 0;
        }
        uint32_t moduleBase = (uint32_t)(uintptr_t)mbi.AllocationBase;
        Log::Battle("BattleTTS: [FFNx-GF] FFNx module base = 0x%08X", moduleBase);
        return moduleBase;
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        Log::Battle("BattleTTS: [FFNx-GF] EXCEPTION resolving FFNx module base");
        return 0;
    }
}

// Scan a single module for the GF loading writer signature.
// Signature: B9 16 F0 CF 01 66 89 06 = MOV ECX,0x01CFF016; MOV [ESI],AX
// Returns the address of the first byte of the match, or 0.
static uint32_t ScanModuleForSignature(uint32_t moduleBase)
{
    __try {
        IMAGE_DOS_HEADER* dos = (IMAGE_DOS_HEADER*)moduleBase;
        if (dos->e_magic != IMAGE_DOS_SIGNATURE) return 0;
        IMAGE_NT_HEADERS* nt = (IMAGE_NT_HEADERS*)(moduleBase + dos->e_lfanew);
        if (nt->Signature != IMAGE_NT_SIGNATURE) return 0;
        uint32_t moduleSize = nt->OptionalHeader.SizeOfImage;

        static const uint8_t sig[] = { 0xB9, 0x16, 0xF0, 0xCF, 0x01, 0x66, 0x89, 0x06 };
        static const int sigLen = sizeof(sig);

        uint8_t* base = (uint8_t*)moduleBase;
        for (uint32_t i = 0; i + sigLen <= moduleSize; i++) {
            bool match = true;
            for (int j = 0; j < sigLen; j++) {
                if (base[i + j] != sig[j]) { match = false; break; }
            }
            if (match) {
                uint32_t addr = moduleBase + i;
                Log::Battle("BattleTTS: [FFNx-GF] Signature found at 0x%08X in module 0x%08X (size=0x%X)",
                           addr, moduleBase, moduleSize);
                return addr;
            }
        }
        return 0;
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        return 0;
    }
}

// Scan ALL loaded modules in the process for the signature.
// The GF loading writer may be in a DLL loaded by FFNx, not FFNx.dll itself.
static uint32_t ScanAllModulesForSignature(void)
{
    HANDLE hProc = GetCurrentProcess();
    HMODULE modules[512];
    DWORD cbNeeded = 0;
    if (!EnumProcessModules(hProc, modules, sizeof(modules), &cbNeeded)) {
        Log::Battle("BattleTTS: [FFNx-GF] EnumProcessModules failed (err=%u)", GetLastError());
        return 0;
    }
    int count = cbNeeded / sizeof(HMODULE);
    Log::Battle("BattleTTS: [FFNx-GF] Scanning %d loaded modules for signature...", count);

    for (int i = 0; i < count; i++) {
        uint32_t base = (uint32_t)(uintptr_t)modules[i];
        uint32_t result = ScanModuleForSignature(base);
        if (result != 0) return result;
    }
    Log::Battle("BattleTTS: [FFNx-GF] Signature not found in any loaded module");
    return 0;
}

// Walk backward from sigAddr to find the function entry point.
// Looks for CC/90 inter-function padding (MSVC pattern).
static uint32_t FindFunctionEntry(uint32_t sigAddr)
{
    __try {
        uint8_t* p = (uint8_t*)sigAddr;
        // Scan backward up to 0x400 bytes
        for (int i = 1; i < 0x400; i++) {
            uint8_t b = p[-i];
            if (b == 0xCC || b == 0x90) {
                // Found padding — the function entry is the first non-padding byte after this
                // Continue backward through the padding
                int padStart = i;
                while (padStart < 0x400 && (p[-padStart] == 0xCC || p[-padStart] == 0x90))
                    padStart++;
                // Now p[-padStart] is non-padding (end of previous function).
                // The entry is at p[-(padStart-1)] = first padding byte... no.
                // Actually: p[-i] is the first padding byte we found (closest to sig).
                // Walk backward through padding. The function entry is the byte AFTER
                // the last padding byte (closest to our code).
                uint32_t entry = sigAddr - i + 1;
                // But we need to continue backward past ALL padding
                int j = i;
                while (j < 0x400) {
                    uint8_t prev = p[-j];
                    if (prev != 0xCC && prev != 0x90) break;
                    j++;
                }
                entry = sigAddr - j + 1;
                Log::Battle("BattleTTS: [FFNx-GF] Function entry at 0x%08X (sig-0x%X, padding at sig-0x%X)",
                           entry, (sigAddr - entry), i);
                return entry;
            }
            // Also check for RET (C3) which ends the previous function
            if (b == 0xC3) {
                uint32_t entry = sigAddr - i + 1;
                Log::Battle("BattleTTS: [FFNx-GF] Function entry at 0x%08X (after RET at sig-0x%X)",
                           entry, i);
                return entry;
            }
        }
        Log::Battle("BattleTTS: [FFNx-GF] Could not find function entry (no padding/RET in 0x400 bytes)");
        return 0;
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        Log::Battle("BattleTTS: [FFNx-GF] EXCEPTION scanning for function entry");
        return 0;
    }
}

static void EWM_InstallFFNxGFHook(void)
{
    if (s_ffnxGFHookInstalled) return;

    // Step 1+2: Scan all loaded modules for signature
    // The writer may be in a DLL loaded by FFNx, not FFNx.dll itself.
    uint32_t sigAddr = ScanAllModulesForSignature();
    if (sigAddr == 0) {
        Log::Battle("BattleTTS: [FFNx-GF] Signature scan failed — GF timer hook skipped");
        return;
    }

    // Step 3: Find function entry
    uint32_t funcAddr = FindFunctionEntry(sigAddr);
    if (funcAddr == 0) {
        Log::Battle("BattleTTS: [FFNx-GF] Function entry not found — GF timer hook skipped");
        return;
    }
    s_ffnxGFFuncAddr = funcAddr;

    // Dump first 32 bytes of the function for diagnostic
    __try {
        uint8_t* code = (uint8_t*)funcAddr;
        char hex[200] = {};
        int p = 0;
        for (int b = 0; b < 32; b++)
            p += snprintf(hex + p, sizeof(hex) - p, "%02X ", code[b]);
        Log::Battle("BattleTTS: [FFNx-GF] Function code[0..31]: %s", hex);
    } __except(EXCEPTION_EXECUTE_HANDLER) {}

    // Step 4: MinHook it
    MH_STATUS st = MH_CreateHook(
        (LPVOID)(uintptr_t)funcAddr,
        (LPVOID)HookedFFNxBattleUpdate,
        (LPVOID*)&s_originalFFNxBattleUpdate);
    if (st == MH_OK) {
        st = MH_EnableHook((LPVOID)(uintptr_t)funcAddr);
    }
    s_ffnxGFHookInstalled = (st == MH_OK);
    Log::Battle("BattleTTS: [FFNx-GF] MinHook @ 0x%08X — %s (trampoline=0x%08X)",
               funcAddr, MH_StatusToString(st),
               (uint32_t)(uintptr_t)s_originalFFNxBattleUpdate);
}
