// field_dialog_diag.inl -- diagnostic/instrumentation hooks.
//
// Contents:
//   - DispatchStub / DispatchStub_EDX: naked asm code caves that the patched
//     dispatch site at update_field_entities+0x657 redirects to. They log
//     the opcode index then call the real handler. Currently disabled in
//     Initialize() pending v0.09.08's infirmary-glitch investigation.
//   - PatchDispatchSite / UnpatchDispatchSite: install/remove the JMP at the
//     dispatch CALL site. Same disabled status.
//   - Hook_menu_draw_text_naked: zero-overhead call counter via naked asm.
//   - Hook_update_field_entities_naked: same pattern, counts script-interpreter
//     entries.
//   - Hook_get_character_width: per-glyph hook that accumulates raw FF8 char
//     codes; CheckGcwBuffer (called from PollWindows) decodes accumulated
//     chars on a 200ms idle gap. Currently disabled in PollWindows() because
//     it speaks garbled glyphs from the naming screen.
//   - DiagRawWindowDump: every-2-second raw hex dump of all 8 window slots.
//     Called from PollWindows for ongoing diagnostic visibility.

// ============================================================================
// Code cave for dispatch instrumentation.
// The original code at update_field_entities + 0x657 is:
//   FF 14 85 [table_addr]  = call dword ptr [eax*4 + table_addr]  (7 bytes)
// We replace those 7 bytes with: E9 [rel32] 90 90 = JMP our_cave + 2 NOPs
//
// Our cave: logs EAX (opcode index), does the original CALL, then JMPs back.
// Key constraint: the handler is __cdecl(int entityPtr). entityPtr is already
// on the stack when we arrive. We MUST NOT push anything before calling the
// handler, or it'll see the wrong parameter.
//
// We use static variables instead of the stack to save/restore registers.
// This is safe because script execution is single-threaded.
// ============================================================================

__declspec(naked) static void DispatchStub()
{
    __asm {
        // EAX = opcode index. Stack: [entityPtr, ...]
        // 1. Log to histogram
        pushfd
        cmp eax, 512
        jae stub_overflow
        lock inc dword ptr [s_opcodeHistogram + eax*4]
        jmp stub_docall
    stub_overflow:
        lock inc dword ptr [s_opcodeOverflow]
    stub_docall:
        popfd
        // 2. Compute handler address without touching the stack
        mov dword ptr [s_savedEdx], edx
        mov edx, dword ptr [s_opcodeTableAddr]
        mov edx, dword ptr [edx + eax*4]    // edx = handler function ptr
        mov dword ptr [s_handlerAddr], edx
        mov edx, dword ptr [s_savedEdx]     // restore EDX
        // 3. Call the handler. Stack is clean: [entityPtr, ...]
        //    CALL pushes our return addr, handler sees entityPtr at [esp+4]. Correct.
        call dword ptr [s_handlerAddr]
        // 4. Handler returned. JMP back to instruction after the patch site.
        jmp dword ptr [s_dispatchRetAddr]
    }
}

// Same as DispatchStub but opcode index is in EDX (FF 14 95 encoding)
__declspec(naked) static void DispatchStub_EDX()
{
    __asm {
        // EDX = opcode index. Stack: [entityPtr, ...]
        pushfd
        cmp edx, 512
        jae stub_edx_overflow
        lock inc dword ptr [s_opcodeHistogram + edx*4]
        jmp stub_edx_docall
    stub_edx_overflow:
        lock inc dword ptr [s_opcodeOverflow]
    stub_edx_docall:
        popfd
        // Compute handler address using EDX as index
        mov dword ptr [s_savedEdx], edx    // save EDX (it's both index AND might be clobbered)
        push eax                            // save EAX
        mov eax, dword ptr [s_opcodeTableAddr]
        mov eax, dword ptr [eax + edx*4]   // eax = handler function ptr
        mov dword ptr [s_handlerAddr], eax
        pop eax                             // restore EAX
        mov edx, dword ptr [s_savedEdx]     // restore EDX
        call dword ptr [s_handlerAddr]
        jmp dword ptr [s_dispatchRetAddr]
    }
}

// v04.22: Patch the dispatch CALL site to redirect through DispatchStub.
static bool PatchDispatchSite()
{
    if (FF8Addresses::update_field_entities_addr == 0 || !FF8Addresses::pExecuteOpcodeTable)
        return false;

    s_dispatchAddr = FF8Addresses::update_field_entities_addr + 0x657;
    s_dispatchRetAddr = s_dispatchAddr + 7;  // instruction after the 7-byte CALL

    // Verify the instruction is FF 14 85 [4-byte addr]
    uint8_t* code = (uint8_t*)s_dispatchAddr;
    if (code[0] != 0xFF || code[1] != 0x14 || (code[2] != 0x85 && code[2] != 0x95)) {
        Log::Dialog("FieldDialog: [DISPATCH] Expected FF 14 85/95 at 0x%08X, got %02X %02X %02X",
                   s_dispatchAddr, code[0], code[1], code[2]);
        return false;
    }
    bool indexInEdx = (code[2] == 0x95);  // 0x85=EAX, 0x95=EDX
    Log::Dialog("FieldDialog: [DISPATCH] Opcode index register: %s", indexInEdx ? "EDX" : "EAX");

    // Read the table address from the instruction operand
    s_opcodeTableAddr = *(uint32_t*)(code + 3);
    Log::Dialog("FieldDialog: [DISPATCH] Found dispatch at 0x%08X, table=0x%08X",
               s_dispatchAddr, s_opcodeTableAddr);

    // Verify table matches what we resolved
    if (s_opcodeTableAddr != (uint32_t)FF8Addresses::pExecuteOpcodeTable) {
        Log::Dialog("FieldDialog: [DISPATCH] WARNING: table mismatch! Expected 0x%08X",
                   (uint32_t)FF8Addresses::pExecuteOpcodeTable);
        // Use the one from the instruction, not our resolved one
    }

    // Save original bytes
    memcpy(s_dispatchOrigBytes, code, 7);

    // Make writable
    DWORD oldProtect;
    if (!VirtualProtect((LPVOID)s_dispatchAddr, 7, PAGE_EXECUTE_READWRITE, &oldProtect)) {
        Log::Dialog("FieldDialog: [DISPATCH] VirtualProtect failed (err=%u)", GetLastError());
        return false;
    }

    // Write: E9 [rel32] 90 90  (JMP DispatchStub/DispatchStub_EDX + 2 NOPs)
    void* stubTarget = indexInEdx ? (void*)&DispatchStub_EDX : (void*)&DispatchStub;
    int32_t rel = (int32_t)((uint32_t)stubTarget - (s_dispatchAddr + 5));
    code[0] = 0xE9;  // JMP rel32
    *(int32_t*)(code + 1) = rel;
    code[5] = 0x90;  // NOP
    code[6] = 0x90;  // NOP

    // Restore protection
    VirtualProtect((LPVOID)s_dispatchAddr, 7, oldProtect, &oldProtect);

    s_dispatchPatched = true;
    Log::Dialog("FieldDialog: [DISPATCH] Patched! JMP to 0x%08X (%s stub), ret to 0x%08X",
               (uint32_t)stubTarget, indexInEdx ? "EDX" : "EAX", s_dispatchRetAddr);

    // Diagnostic: dump x86 bytes before the dispatch to understand how
    // the engine extracts the opcode index from JSM instruction data.
    // This covers the bytecode interpreter loop in update_field_entities.
    {
        uint32_t dumpStart = s_dispatchAddr - 128;
        // Restore original bytes temporarily for clean dump
        DWORD dp;
        VirtualProtect((LPVOID)s_dispatchAddr, 7, PAGE_EXECUTE_READWRITE, &dp);
        memcpy((void*)s_dispatchAddr, s_dispatchOrigBytes, 7);

        const uint8_t* p = (const uint8_t*)dumpStart;
        for (int row = 0; row < 9; row++) {
            char hexBuf[256];
            int hp = 0;
            uint32_t addr = dumpStart + row * 16;
            hp += snprintf(hexBuf + hp, 256 - hp, "%08X: ", addr);
            for (int b = 0; b < 16; b++)
                hp += snprintf(hexBuf + hp, 256 - hp, "%02X ", p[row * 16 + b]);
            Log::Dialog("FieldDialog: [X86DUMP] %s", hexBuf);
        }

        // Also dump the instruction decoder function at 0x00530760
        // (called from the dispatch site to extract opcode from raw dword)
        uint32_t decoderAddr = 0x00530760;
        const uint8_t* dp2 = (const uint8_t*)decoderAddr;
        Log::Dialog("FieldDialog: [X86DUMP] === Instruction decoder at 0x%08X ===", decoderAddr);
        for (int row = 0; row < 16; row++) {
            char hexBuf2[256];
            int hp2 = 0;
            uint32_t a2 = decoderAddr + row * 16;
            hp2 += snprintf(hexBuf2 + hp2, 256 - hp2, "%08X: ", a2);
            for (int b = 0; b < 16; b++)
                hp2 += snprintf(hexBuf2 + hp2, 256 - hp2, "%02X ", dp2[row * 16 + b]);
            Log::Dialog("FieldDialog: [X86DUMP] %s", hexBuf2);
        }

        // Re-apply patch
        code = (uint8_t*)s_dispatchAddr;
        code[0] = 0xE9;
        *(int32_t*)(code + 1) = rel;
        code[5] = 0x90;
        code[6] = 0x90;
        VirtualProtect((LPVOID)s_dispatchAddr, 7, dp, &dp);
    }

    return true;
}

static void UnpatchDispatchSite()
{
    if (!s_dispatchPatched || s_dispatchAddr == 0) return;

    DWORD oldProtect;
    if (VirtualProtect((LPVOID)s_dispatchAddr, 7, PAGE_EXECUTE_READWRITE, &oldProtect)) {
        memcpy((void*)s_dispatchAddr, s_dispatchOrigBytes, 7);
        VirtualProtect((LPVOID)s_dispatchAddr, 7, oldProtect, &oldProtect);
    }
    s_dispatchPatched = false;
    Log::Dialog("FieldDialog: [DISPATCH] Unpatched.");
}

// ============================================================================
// v04.20: menu_draw_text naked hook
//
// Zero-overhead: just atomically increment a counter and jump to the
// original function. Preserves ALL registers, flags, and stack state.
// We use this to measure call rate during thought gaps.
// ============================================================================

__declspec(naked) static void Hook_menu_draw_text_naked()
{
    __asm {
        lock inc dword ptr [s_menuDrawTextCallCount]
        jmp dword ptr [s_origMenuDrawText_raw]
    }
}

// v04.22: update_field_entities naked hook -- counts script interpreter calls
__declspec(naked) static void Hook_update_field_entities_naked()
{
    __asm {
        lock inc dword ptr [s_ufeCallCount]
        jmp dword ptr [s_origUpdateFieldEntities_raw]
    }
}

// ============================================================================
// v04.20: get_character_width hook -- fires for EVERY glyph rendered.
// Accumulates FF8 char codes into a buffer. On a gap (no calls for 200ms),
// the poll thread decodes and speaks the accumulated text.
// This catches ALL rendered text regardless of code path.
// ============================================================================

static uint32_t __cdecl Hook_get_character_width(uint32_t charCode)
{
    InterlockedIncrement(&s_gcwCallCount);
    s_gcwLastCallTime = GetTickCount();

    // Accumulate char code (only printable range, skip control codes)
    // FF8 char codes: 0x00-0x19 = A-Z, 0x1A-0x33 = a-z, 0x34-0x3D = 0-9
    // 0x3E = space, 0x40+ = punctuation. 0x00 = terminator in strings but
    // get_character_width receives the actual code, not terminator.
    if (charCode <= 0xFF) {
        LONG pos = InterlockedIncrement(&s_gcwBufLen) - 1;
        if (pos < GCW_BUF_SIZE) {
            s_gcwBuf[pos] = (uint8_t)charCode;
        }
    }

    return s_origGetCharWidth(charCode);
}

// Called from PollWindows -- check if accumulated chars form speakable text
static void CheckGcwBuffer()
{
    DWORD lastCall = s_gcwLastCallTime;
    if (lastCall == 0) return;

    DWORD elapsed = GetTickCount() - lastCall;
    LONG bufLen = s_gcwBufLen;

    // Wait for a gap: no new chars for 200ms and buffer has content
    if (elapsed < 200 || bufLen == 0) return;

    // Snapshot and reset buffer
    int len = (bufLen < GCW_BUF_SIZE) ? (int)bufLen : GCW_BUF_SIZE;
    uint8_t localBuf[GCW_BUF_SIZE];
    memcpy(localBuf, s_gcwBuf, len);
    InterlockedExchange(&s_gcwBufLen, 0);

    // Decode using our FF8 text decoder
    std::string decoded = TrimDecoded(FF8TextDecode::Decode(localBuf, len));
    if (decoded.empty() || (int)decoded.length() < MIN_TEXT_LENGTH) return;

    // Dedup: skip if same as last GCW spoken or any window state
    if (decoded == s_gcwLastSpoken) return;
    for (int w = 0; w < MAX_WINDOWS; w++) {
        if (decoded == s_winState[w].lastSpokenText ||
            decoded == s_winState[w].lastRawText) return;
        if (IsSuffixOrSubstring(s_winState[w].lastSpokenText, decoded) ||
            IsSuffixOrSubstring(s_winState[w].lastRawText, decoded)) return;
    }

    s_gcwLastSpoken = decoded;
    Log::Dialog("FieldDialog: [GCW-SPEAK] %d chars -> \"%s\"", len, decoded.c_str());
    ScreenReader::Speak(decoded.c_str(), false);
}

// ============================================================================
// Periodic raw dump of ALL window slots (every ~2 seconds)
// Logs pointer, first 8 hex bytes, and state for EVERY window regardless
// of validation. This catches text that our pointer checks might filter out.
// ============================================================================

static void DiagRawWindowDump()
{
    DWORD now = GetTickCount();
    if ((now - s_lastRawDumpTime) < 2000) return;
    s_lastRawDumpTime = now;

    // Check if any window has anything interesting (non-zero text pointer)
    bool anyInteresting = false;
    for (int i = 0; i < MAX_WINDOWS; i++) {
        uint8_t* winObj = GetWindowObj(i);
        char* text1 = GetWinText1(winObj);
        if (text1 != nullptr) { anyInteresting = true; break; }
    }
    if (!anyInteresting) return;

    char buf[2048];
    int pos = 0;
    pos += snprintf(buf + pos, sizeof(buf) - pos, "[RAWDUMP]");

    for (int i = 0; i < MAX_WINDOWS; i++) {
        uint8_t* winObj = GetWindowObj(i);
        char* text1 = GetWinText1(winObj);
        uint32_t state = *(uint32_t*)(winObj + WIN_OBJ_STATE_OFFSET);

        if (!text1 && state == 0) continue;  // truly empty

        uintptr_t addr = (uintptr_t)text1;
        if (!text1) {
            pos += snprintf(buf + pos, sizeof(buf) - pos,
                            " [%d NULL st=%u]", i, state);
            continue;
        }

        // Try to read first 8 bytes with SEH (even if pointer looks bad)
        uint8_t raw[8] = {0};
        bool readable = false;
        __try {
            for (int j = 0; j < 8; j++) raw[j] = ((uint8_t*)text1)[j];
            readable = true;
        } __except(EXCEPTION_EXECUTE_HANDLER) {}

        // v04.23: also show text_data2 pointer and open_close_transition
        char* text2 = GetWinText2(winObj);
        int16_t transition = GetWinOpenCloseTransition(winObj);

        if (readable) {
            pos += snprintf(buf + pos, sizeof(buf) - pos,
                            " [%d t1=%08X t2=%08X st=%u tr=%d hex=%02X%02X%02X%02X%02X%02X%02X%02X]",
                            i, (uint32_t)addr, (uint32_t)(uintptr_t)text2,
                            state, (int)transition,
                            raw[0], raw[1], raw[2], raw[3],
                            raw[4], raw[5], raw[6], raw[7]);
        } else {
            pos += snprintf(buf + pos, sizeof(buf) - pos,
                            " [%d t1=%08X t2=%08X st=%u tr=%d UNREADABLE]",
                            i, (uint32_t)addr, (uint32_t)(uintptr_t)text2,
                            state, (int)transition);
        }
    }

    Log::Write("%s", buf);
}
