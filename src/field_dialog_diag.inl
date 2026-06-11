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

// ============================================================================
// v0.18.3.0: Train minigame code-channel probe ([TRAINPROBE]) -- log-only.
//
// Goal (issue #56): find which render channel carries Rinoa's uncoupling
// code numbers. The mod already catches every text channel it knows about
// (opcode_mes, field_get_dialog_string, show_dialog, and the
// get_character_width glyph accumulator), yet the codes are never spoken --
// so they are almost certainly drawn by the fixed-width numeric-sprite
// routine (the HP/gil/timer font), bypassing all of them. This probe RULES
// OUT the text channels first: while on a field it dumps, UNFILTERED,
//   (1) the current field name + id on change (also seeds the train_detector
//       field set), and
//   (2) every window slot's decoded text (no min-length / garbled filter), and
//   (3) the raw + decoded get_character_width glyph buffer.
// If the code shows up in none of them, v0.18.3.1 hooks the numeric-sprite
// routine. If it shows up but is being filtered/deduped, the fix lives in
// code we already own.
//
// Discovery is done on the briefing-room PRACTICE code panel: no timer, no
// guards, repeatable. Auto-fires on field (no F12, no key, no SET3, no new
// engine hook). Pure logging -- no speech, no memory writes.
// ============================================================================

#define TRAIN_PROBE_DIAG 0   // set 0 to compile the probe out -- DONE: code is sprite-drawn, not in windows

static void TrainProbeDump()
{
#if TRAIN_PROBE_DIAG
    if (!FF8Addresses::IsOnField()) return;

    // Field name/id on change -- captures the briefing/train field names.
    static std::string s_lastProbeField;
    char fbuf[24];
    int fi = 0;
    const char* fn = FF8Addresses::pCurrentFieldName;
    if (fn) { for (; fi < 16 && fn[fi]; ++fi) fbuf[fi] = fn[fi]; }
    fbuf[fi] = 0;
    std::string curField(fbuf);
    if (curField != s_lastProbeField) {
        s_lastProbeField = curField;
        uint16_t fid = FF8Addresses::pCurrentFieldId ? *FF8Addresses::pCurrentFieldId : 0xFFFF;
        Log::Write("[TRAINCODE] field=\"%s\" id=0x%04X", curField.c_str(), fid);
    }

    if (!FF8Addresses::pWindowsArray) return;

    // Per-window RAW HEX + decoded, logged ONLY when a slot's bytes change.
    // Change-triggered (not time-throttled) so every ~5s code refresh in win[4]
    // is captured exactly once with its raw bytes, the stable legend window is
    // captured once, and idle frames produce no spam. The raw bytes are what we
    // need to map the code's button-icon glyphs to digits (#56/#57).
    static uint32_t s_slotHash[MAX_WINDOWS] = {0};
    for (int i = 0; i < MAX_WINDOWS; i++) {
        uint8_t* winObj = GetWindowObj(i);
        char* text1 = GetWinText1(winObj);
        if (!text1 || !IsValidTextPointer(text1) || !ProbePointer(text1)) {
            s_slotHash[i] = 0;
            continue;
        }
        // Copy up to 48 bytes (text1 already validated; FF8 text buffers are
        // large, so this stays in-bounds just like the 512-byte decode path).
        uint8_t raw[48];
        int n = 0;
        for (; n < 48; n++) {
            uint8_t b = ((const uint8_t*)text1)[n];
            raw[n] = b;
            if (b == 0x00) { n++; break; }   // include terminator, then stop
        }
        if (n == 0) { s_slotHash[i] = 0; continue; }
        uint32_t h = 2166136261u;            // FNV-1a for change detection
        for (int k = 0; k < n; k++) { h ^= raw[k]; h *= 16777619u; }
        if (h == s_slotHash[i]) continue;    // unchanged since last log -> skip
        s_slotHash[i] = h;

        char hexBuf[120];
        int hp = 0;
        int hexN = (n < 40) ? n : 40;
        for (int k = 0; k < hexN && hp < (int)sizeof(hexBuf) - 3; k++)
            hp += snprintf(hexBuf + hp, sizeof(hexBuf) - hp, "%02X", raw[k]);
        std::string decoded = FF8TextDecode::Decode(raw, n);
        uint32_t state = *(uint32_t*)(winObj + WIN_OBJ_STATE_OFFSET);
        Log::Write("[TRAINCODE] win[%d] st=%u raw=%s dec=\"%s\"",
                   i, state, hexBuf, decoded.c_str());
    }
#endif
}

// ============================================================================
// v0.18.3.2: Train code-apparatus JSM dump ([SCRIPT-DUMP]) -- static analysis
// aid for #56. On entering a Timber-train field (tiagit*), dump the code
// entities' decoded opcode streams so the 4 uncoupling-code digits' storage
// can be found statically: where they're generated (random 1-4), the POPM_W
// varblock addresses they're written to, and the draw-number opcode that
// renders them. The digits are sprite-drawn (NOT in any window text buffer --
// confirmed by exe disassembly of the field text engine), so the announcement
// must read them from the varblock; this dump finds the addresses.
// Fires once per field entry. Output -> ff8_field.log. Log-only.
// ============================================================================
#define TRAIN_JSM_DUMP_DIAG 0   // off again v0.18.3.7: apparatus entities have no POPM writes on tilink1; code located via [TRAINWIN] instead (#56)

static void TrainCodeJsmDump()
{
#if TRAIN_JSM_DUMP_DIAG
    if (!FF8Addresses::IsOnField()) return;
    const char* fn = FF8Addresses::pCurrentFieldName;
    if (!fn) return;
    bool isCodeField = (_strnicmp(fn, "tiagit", 6) == 0 ||
                        _strnicmp(fn, "titrain", 7) == 0 ||
                        _strnicmp(fn, "tilink", 6) == 0);
    if (!isCodeField) return;   // Timber-train code fields only

    static std::string s_lastJsmDumpField;
    std::string cur(fn);
    if (cur == s_lastJsmDumpField) return;          // once per field entry
    s_lastJsmDumpField = cur;

    Log::Field("FieldDialog: [SCRIPT-DUMP] TrainCodeJsmDump firing for field '%s' (#56)", cur.c_str());
    FieldArchive::DumpTrainCodeScripts(cur.c_str());
#endif
}

// ============================================================================
// v0.18.3.3: Train code-var runtime probe ([CODEVAR]) -- #56. The v0.18.3.2
// JSM dump located the 4 code digits in field-varblock vars 1026-1029 (Keykantoku
// writes each 1-4). They are written with the LONG memory op at consecutive
// indices, so the long bank's exact byte addressing is unconfirmed (raw
// byte-offset longs would overlap). This probe reads vars 1026-1029 under five
// candidate addressings from the varblock base (0x1CFE9B8 -- the same base the
// mapjump resolver uses for word vars) and logs the quad change-triggered (plus a
// ~2s heartbeat) so the right interpretation can be pinned against the known
// on-screen codes (1232 / 4331). Poll thread, tiagit* only. Log-only -> ff8_field.log.
// ============================================================================
#define TRAIN_CODEVAR_DIAG 0   // set 0 to compile the probe out -- DONE: confirmed byte@idx (v0.18.3.3 BAT)

static void TrainCodeVarProbe()
{
#if TRAIN_CODEVAR_DIAG
    if (!FF8Addresses::IsOnField()) return;
    const char* fn = FF8Addresses::pCurrentFieldName;
    if (!fn) return;
    if (_strnicmp(fn, "tiagit", 6) != 0) return;   // Timber train fields only

    const uint32_t base = 0x1CFE9B8;               // FFNx field_vars_stack (Steam 2013 en-US)
    static const int idx[4] = { 1026, 1027, 1028, 1029 };

    int b[4]  = {}, w[4]  = {}, l[4]  = {};        // byte / word / long @ base+idx
    int w2[4] = {}, l4[4] = {};                    // word @ base+idx*2, long @ base+idx*4
    bool okRead = true;
    __try {
        for (int i = 0; i < 4; i++) {
            b[i]  = *(uint8_t*)(base + idx[i]);
            w[i]  = *(int16_t*)(base + idx[i]);
            l[i]  = *(int32_t*)(base + idx[i]);
            w2[i] = *(int16_t*)(base + idx[i] * 2);
            l4[i] = *(int32_t*)(base + idx[i] * 4);
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        okRead = false;
    }
    if (!okRead) {
        static bool s_faultLogged = false;
        if (!s_faultLogged) {
            Log::Field("FieldDialog: [CODEVAR] read fault at base=0x%08X (varblock not mapped?)", base);
            s_faultLogged = true;
        }
        return;
    }

    // Change-trigger over all candidates, with a ~2s heartbeat so a screenshot
    // always lines up with a recent line even if the values are momentarily static.
    uint32_t h = 2166136261u;
    int vals[20]; int n = 0;
    for (int i = 0; i < 4; i++) { vals[n++]=b[i]; vals[n++]=w[i]; vals[n++]=l[i]; vals[n++]=w2[i]; vals[n++]=l4[i]; }
    for (int i = 0; i < n; i++) { h ^= (uint32_t)vals[i]; h *= 16777619u; }

    static uint32_t s_lastHash = 0;
    static DWORD    s_lastLog  = 0;
    DWORD now = GetTickCount();
    if (h == s_lastHash && (now - s_lastLog) < 2000) return;
    s_lastHash = h;
    s_lastLog  = now;

    Log::Field("FieldDialog: [CODEVAR] %s base=0x%08X idx=1026-1029  "
               "byte@idx=[%d %d %d %d]  word@idx=[%d %d %d %d]  long@idx=[%d %d %d %d]  "
               "word@idx*2=[%d %d %d %d]  long@idx*4=[%d %d %d %d]",
               fn, base,
               b[0],b[1],b[2],b[3],
               w[0],w[1],w[2],w[3],
               l[0],l[1],l[2],l[3],
               w2[0],w2[1],w2[2],w2[3],
               l4[0],l4[1],l4[2],l4[3]);
#endif
}

// ============================================================================
// v0.18.3.5: Real-train code-field discovery ([TRAINFIELD]/[TRAINSCAN]) -- #56.
//
// The v0.18.3.4 announcement works on the briefing-room practice panel
// (tiagit5) but NOT on the actual moving-train code-entry, because
// TrainCodeAnnounce() is gated to `tiagit*` and the real train uses a
// different field name. This pass finds (a) that field name and (b) whether
// the code is stored at the SAME varblock spot (0x1CFE9B8 + 1026..1029) there.
//
// On EVERY field it logs the field name+id on change ([TRAINFIELD]); and, when
// bytes 1026..1029 all read 1-4, logs that quad on change ([TRAINSCAN]). On
// the real train code-entry field we expect to see [TRAINFIELD] give its name
// and [TRAINSCAN] cycle a 1-4 code (~5s) just like the practice panel -- in
// which case the fix is simply widening the announce gate to that field. If
// [TRAINSCAN] does NOT cycle there, the train field stores the code elsewhere
// and needs its own JSM dump (re-enable TRAIN_JSM_DUMP_DIAG + widen its gate).
// Log-only -> ff8_field.log. No speech, no writes.
// ============================================================================
#define TRAIN_FIELD_SCAN_DIAG 0   // off v0.18.3.8: real-train code located at tilink1 1029-1032 + announce BAT-confirmed working; [TRAINWIN]/[TRAINSCAN] served their purpose (#56)

#if TRAIN_FIELD_SCAN_DIAG
// Destructor-free SEH wrapper for raw varblock byte reads. __try cannot share a
// function with C++ objects that require unwinding (std::string) -- MSVC C2712 --
// so the guarded reads live here. Returns true and fills out[0..count-1].
static bool TrainScan_ReadBytes(unsigned firstIdx, int count, int* out)
{
    __try {
        for (int i = 0; i < count; i++) out[i] = *(uint8_t*)(0x1CFE9B8 + firstIdx + i);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}
#endif

static void TrainFieldScan()
{
#if TRAIN_FIELD_SCAN_DIAG
    if (!FF8Addresses::IsOnField()) return;
    const char* fn = FF8Addresses::pCurrentFieldName;
    if (!fn) return;
    std::string cur(fn);

    // (1) Field name + id on change -- captures the train code-entry field name.
    static std::string s_lastField;
    if (cur != s_lastField) {
        s_lastField = cur;
        uint16_t fid = FF8Addresses::pCurrentFieldId ? *FF8Addresses::pCurrentFieldId : 0xFFFF;
        Log::Field("FieldDialog: [TRAINFIELD] field=\"%s\" id=0x%04X", cur.c_str(), (unsigned)fid);
    }

    // (2) v0.18.3.6: on the real train code fields (tilink*/titrain*), the
    // practice's 1026-1029 read [1 1 1 x] -- NOT a live code (the practice never
    // produced three identical leading digits) -- so the digits live at different
    // indices here. Dump a 64-var window (1008..1071) as a 1-4-else-dot map,
    // logged only when the 1-4 pattern changes, so the four bytes that cycle
    // together can be located by column. Tag [TRAINWIN]; column i = var 1008+i.
    if (_strnicmp(fn, "tilink", 6) == 0 || _strnicmp(fn, "titrain", 7) == 0) {
        int win[64] = {};
        if (TrainScan_ReadBytes(1008, 64, win)) {
            char map[72];
            uint32_t h = 2166136261u;
            for (int i = 0; i < 64; i++) {
                char c = (win[i] >= 1 && win[i] <= 4) ? (char)('0' + win[i]) : '.';
                map[i] = c;
                h ^= (uint32_t)c; h *= 16777619u;
            }
            map[64] = 0;
            static uint32_t s_winHash = 0;
            static std::string s_winField;
            if (h != s_winHash || cur != s_winField) {
                s_winHash = h; s_winField = cur;
                Log::Field("FieldDialog: [TRAINWIN] field=\"%s\" vars 1008..1071 (1-4 else .): %s",
                           cur.c_str(), map);
            }
        }
    }

    // (3) Reference read of the practice code vars 1026-1029 (confirmed on tiagit5).
    int d[4] = {};
    if (!TrainScan_ReadBytes(1026, 4, d)) return;
    for (int i = 0; i < 4; i++) if (d[i] < 1 || d[i] > 4) return;   // not a code here

    int code = d[0] * 1000 + d[1] * 100 + d[2] * 10 + d[3];
    static int         s_lastCode = -1;
    static std::string s_lastCodeField;
    if (code == s_lastCode && cur == s_lastCodeField) return;       // change-triggered
    s_lastCode = code; s_lastCodeField = cur;
    Log::Field("FieldDialog: [TRAINSCAN] field=\"%s\" code@1026=[%d %d %d %d]",
               cur.c_str(), d[0], d[1], d[2], d[3]);
#endif
}

// ============================================================================
// v0.18.3.9/.10: Timber-train guard JSM dump ([SCRIPT-DUMP]) -- #58. Dumps
// tilink1's GalHei1/GalHei2 (ents 5/6) + the controller candidates
// (TrainSindou, point) via FieldArchive::DumpGuardScripts, so the patrol MOVE
// loop + the line-of-sight/proximity check + the "spotted -> restart" trigger
// (MAPJUMP/fail-flag) can be read statically.
//   v0.18.3.10: reads tilink1's archive BY NAME, so it fires from ANY field
//   (e.g. the briefing-room save) -- no need to traverse to the live coupling
//   field -- once per game session on the first valid field. Lands near the
//   TOP of the session log (read with `head`), not buried under runtime spam.
// Poll thread. Log-only -> ff8_field.log.
// ============================================================================
#define GUARD_JSM_DUMP_DIAG 0   // #58: entities 0-27 fully mapped (v0.18.3.13); dump retained behind this gate for one-line re-enable

static void GuardJsmDump()
{
#if GUARD_JSM_DUMP_DIAG
    if (!FF8Addresses::IsOnField()) return;
    const char* fn = FF8Addresses::pCurrentFieldName;
    if (!fn || !fn[0]) return;

    static bool s_guardDumpDone = false;
    if (s_guardDumpDone) return;                     // once per game session
    // Dump tilink1's guard scripts from the field ARCHIVE (by name); works from
    // any field. Only mark done on success so it retries until the archive is
    // ready (DumpGuardScripts logs its own success/fail lines).
    if (FieldArchive::DumpGuardScripts("tilink1"))
        s_guardDumpDone = true;
#endif
}
