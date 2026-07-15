// field_dialog_expand.inl — v0.18.3.239 (#77): {Var} number-insert expansion
// Included from field_dialog.cpp AFTER field_dialog_helpers.inl (needs
// TrimDecoded) and BEFORE the scan / show_dialog / opcode .inl files (they
// call DecodeDialogWithExpansion). Do not compile independently.
//
// THE PROBLEM
// -----------
// Aaron's 2026-07-13 Tomb of the Unknown King run: picking up the student ID
// spoke "Student ID No. ." — the ID number (quest-critical, randomly generated
// per playthrough) was missing.
//
//   21:52:03 [GETSTR] dialogId=1 text=""Student ID No. .  ""
//   21:52:03 [AMESW] win[0] Speaking: ""Student ID No. .  ""
//
// The raw field message stores the number as control code 0x0A + param. The
// mod's decoder consumes those two bytes and emits nothing (ff8_text_decode.cpp
// DecodeByte, "Special value" branch), because the RAW message simply does not
// contain the number — the engine substitutes it at render time. Every
// {Var}-style numeric insert in field dialog is affected; the Tomb is just
// where it first becomes quest-blocking.
//
// THE ENGINE'S EXPANDER (disassembly sweep, 2026-07-13)
// -----------------------------------------------------
// `sub_4A3260(src, dst, a2..a6)` is FF8's message expander:
//
//   0x004A3266  mov al, [ecx] / inc ecx        ; walk src byte by byte
//   0x004A327B  cmp eax, 0xA                   ; the var-insert control code
//   0x004A327E  je  0x4A328D
//   0x004A3280  mov [dst], al / inc dst        ; ordinary char: copy through
//   0x004A328D  mov al, [ecx] / inc ecx        ; 0x0A: read the param byte
//   0x004A3297  add eax, -0x20                 ; param - 0x20 = case index
//   0x004A329E  cmp eax, 6 / ja ...            ; 7 substitution kinds
//   0x004A32AB  jmp [eax*4 + 0x4A33D8]         ; dispatch table
//     ...
//   0x004A334E  (number case) reads the value arg, formats the digits via
//               sub_4B87F0 / sub_4B8840, then 0x4A338A converts the ASCII
//               digits into FF8 font codes (adds the font base offset)
//   0x004A33A7  copies the produced bytes into dst
//   0x004A33C9  returns the dst end pointer
//
// So `dst` is the FULLY EXPANDED, FF8-encoded string the engine is about to
// render — numbers included. That is the display-pipeline truth, and per the
// standing project rule (hook the display pipeline, never infer from upstream
// memory) it is what TTS should speak.
//
// THE FIX
// -------
// Hook sub_4A3260. After the original runs, decode BOTH src and dst. If they
// differ, an expansion happened: cache src-pointer + decoded-src -> decoded-dst.
// Every dialog decode site then goes through DecodeDialogWithExpansion(), which
// decodes as before and, when the raw text matches a cached entry, substitutes
// the expanded text.
//
// SAFETY / NO-REGRESSION PROPERTY
// -------------------------------
// The substitution is opt-in per text: if the hook never fires, or the text has
// no 0x0A code (so src == dst and nothing is cached), DecodeDialogWithExpansion
// returns exactly what TrimDecoded(Decode(...)) returned before. Worst case is
// that the bug persists and the [TEXTEXPAND] log line tells us the hook didn't
// fire for the field path — it cannot break dialogs that work today.

// The engine's message expander. cdecl; the disassembly shows it reading up to
// arg6 ([esp+0x68] after the 0x4C-byte prologue), so we declare eight
// pass-through args — cdecl means the caller cleans the stack, so declaring
// more than the callee reads is harmless, while declaring FEWER would let the
// original read garbage for the substitution values.
// v0.18.3.242 (#77): both engine-expander hooks are RETIRED — neither is on the
// field-dialog path (proven: installed in .239/.240, never fired). The real
// mechanism is control code 0x04 + param, resolved below. Code retained behind
// this gate per the project's diagnostic-gating pattern.
#define FIELD_EXPAND_HOOKS_ENABLED 0

static const uintptr_t FF8_TEXT_EXPAND_ADDR = 0x004A3260;

typedef char* (__cdecl *ExpandTextFn)(const char* src, char* dst,
                                      uint32_t a2, uint32_t a3, uint32_t a4,
                                      uint32_t a5, uint32_t a6, uint32_t a7);
static ExpandTextFn s_origExpandText = nullptr;

// v0.18.3.240 (#77): THE FIELD EXPANDER. The .239 BAT proved sub_4A3260 is NOT
// on the field-dialog path — the hook installed and never fired once, while the
// Tomb ID still spoke as "Student ID No. .". A second sweep found the field
// renderer's own copy of the same logic:
//
//   sub_4D4A80(src, dst)                     ; esi=src [esp+0x6C], ebx=dst [esp+0x70]
//     0x004D4AFA  mov al,[esi] / inc esi     ; walk src
//     0x004D4B19  cmp eax, 0xA / jne ...     ; the var-insert control code
//     0x004D4B28  mov al,[esi] / inc esi     ; read the param byte
//     0x004D4B2B  add eax, -0x20             ; param-0x20 = case index
//     0x004D4B2E  cmp eax, 6 / ja  ...       ; 7 substitution kinds
//     0x004D4B33  jmp [eax*4 + 0x4D4D08]     ; dispatch table
//     0x004D4B3A/49/60  load the value from the engine GLOBALS
//                       0x1D7DAB0 / 0x1D7DAAC / 0x1D7DAA8   <-- the numbers
//     0x004D4B6E  sub_4B87F0 (value -> digits)
//     0x004D4B81  sub_4B8840 (field width / padding)
//     0x004D4BA1  digits -> FF8 font codes
//     0x004D4BCE  mov [ebx], al / inc ebx    ; write into dst
//
// Same (src, dst) shape as sub_4A3260 (only two stack args are ever read:
// [esp+0x6C] and [esp+0x70]), so the SAME hook body serves both. This one is
// called from a dozen sites in the field text-draw module — it is the field
// path's expander, and dst is the expanded string the field window renders.
static const uintptr_t FF8_FIELD_TEXT_EXPAND_ADDR = 0x004D4A80;
static ExpandTextFn s_origFieldExpandText = nullptr;

// Small ring cache of recent expansions. Field dialog rarely has more than one
// or two live windows, so eight entries is generous.
struct ExpandCacheEntry {
    const void* srcPtr;
    std::string srcDecoded;
    std::string expDecoded;
    DWORD       tick;
};

static const int   EXPAND_CACHE_SIZE   = 8;
static const DWORD EXPAND_CACHE_MAX_MS = 60000;  // stale entries are ignored

static ExpandCacheEntry s_expandCache[EXPAND_CACHE_SIZE] = {};
static int              s_expandCacheNext = 0;
static int              s_expandHookFires = 0;   // diagnostic counter

// Store one expansion. Overwrites the oldest slot; re-uses a slot that already
// holds the same source (a re-render of the same window is the common case).
static void ExpandCacheStore(const void* srcPtr,
                             const std::string& srcDecoded,
                             const std::string& expDecoded)
{
    EnterCriticalSection(&s_cs);
    int slot = -1;
    for (int i = 0; i < EXPAND_CACHE_SIZE; i++) {
        if (s_expandCache[i].srcPtr == srcPtr ||
            (!s_expandCache[i].srcDecoded.empty() &&
             s_expandCache[i].srcDecoded == srcDecoded)) {
            slot = i;
            break;
        }
    }
    if (slot < 0) {
        slot = s_expandCacheNext;
        s_expandCacheNext = (s_expandCacheNext + 1) % EXPAND_CACHE_SIZE;
    }
    s_expandCache[slot].srcPtr     = srcPtr;
    s_expandCache[slot].srcDecoded = srcDecoded;
    s_expandCache[slot].expDecoded = expDecoded;
    s_expandCache[slot].tick       = GetTickCount();
    LeaveCriticalSection(&s_cs);
}

// Look up an expansion by raw pointer first (exact, cheapest) and then by an
// exact decoded-source match (covers the case where the engine hands the
// window a copy of the message rather than the original pointer).
static bool ExpandCacheLookup(const void* srcPtr,
                              const std::string& srcDecoded,
                              std::string& out)
{
    if (srcDecoded.empty()) return false;
    bool found = false;
    DWORD now = GetTickCount();
    EnterCriticalSection(&s_cs);
    for (int pass = 0; pass < 2 && !found; pass++) {
        for (int i = 0; i < EXPAND_CACHE_SIZE; i++) {
            const ExpandCacheEntry& e = s_expandCache[i];
            if (e.expDecoded.empty()) continue;
            if (e.tick == 0 || (now - e.tick) > EXPAND_CACHE_MAX_MS) continue;
            bool hit = (pass == 0) ? (srcPtr != nullptr && e.srcPtr == srcPtr)
                                   : (e.srcDecoded == srcDecoded);
            if (hit) {
                out = e.expDecoded;
                found = true;
                break;
            }
        }
    }
    LeaveCriticalSection(&s_cs);
    return found;
}

// SEH-guarded raw copy of an engine text buffer into a plain byte array.
// MSVC forbids __try in any function that needs C++ object unwinding (error
// C2712), and both FF8TextDecode::Decode and std::string do — so the SEH lives
// HERE, in a function with no C++ objects at all, and the decoding happens in
// the caller on the safe local copy. Returns false on an access fault or a
// null/empty buffer.
static bool SafeCopyEngineText(const void* p, uint8_t* out, size_t outSize)
{
    if (p == nullptr || out == nullptr || outSize == 0) return false;
    bool ok = false;
    __try {
        const uint8_t* s = (const uint8_t*)p;
        size_t i = 0;
        for (; i < outSize - 1; i++) {
            uint8_t b = s[i];
            out[i] = b;
            if (b == 0x00) break;
        }
        out[(i < outSize - 1) ? i : (outSize - 1)] = 0x00;
        ok = (out[0] != 0x00);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        ok = false;
    }
    return ok;
}

// Shared post-processing for both expanders: snapshot src + dst, decode them,
// and cache the pair when an expansion actually happened.
static void CaptureExpansion(const char* src, const char* dst, const char* tag)
{
    // Snapshot both buffers behind SEH first (no C++ objects in that call),
    // then decode the copies here where objects are allowed.
    uint8_t srcBuf[512];
    uint8_t dstBuf[512];
    if (!SafeCopyEngineText(src, srcBuf, sizeof(srcBuf))) return;
    if (!SafeCopyEngineText(dst, dstBuf, sizeof(dstBuf))) return;

    std::string srcDecoded = TrimDecoded(FF8TextDecode::Decode(srcBuf, sizeof(srcBuf)));
    std::string expDecoded = TrimDecoded(FF8TextDecode::Decode(dstBuf, sizeof(dstBuf)));

    // Only cache genuine expansions. When the message has no 0x0A code the
    // engine copies it verbatim and the two decode identically — nothing to
    // substitute, and caching it would be pure overhead.
    if (expDecoded.empty() || expDecoded == srcDecoded) return;

    ExpandCacheStore(src, srcDecoded, expDecoded);
    if (s_expandHookFires < 20) {
        s_expandHookFires++;
        Log::Dialog("FieldDialog: [TEXTEXPAND] %s src=0x%08X \"%s\" -> \"%s\"",
                    tag, (uint32_t)(uintptr_t)src,
                    srcDecoded.c_str(), expDecoded.c_str());
    }
}

// Hook A: sub_4A3260 (menu/other paths). Kept — harmless, and it may be the
// expander for non-field text that also carries numeric inserts.
static char* __cdecl Hook_expand_text(const char* src, char* dst,
                                      uint32_t a2, uint32_t a3, uint32_t a4,
                                      uint32_t a5, uint32_t a6, uint32_t a7)
{
    char* result = s_origExpandText(src, dst, a2, a3, a4, a5, a6, a7);
    CaptureExpansion(src, dst, "menu");
    return result;
}

// Hook B: sub_4D4A80 — the FIELD expander (v0.18.3.240, the one the .239 BAT
// proved we actually needed). Same (src, dst) signature.
static char* __cdecl Hook_field_expand_text(const char* src, char* dst,
                                            uint32_t a2, uint32_t a3, uint32_t a4,
                                            uint32_t a5, uint32_t a6, uint32_t a7)
{
    char* result = s_origFieldExpandText(src, dst, a2, a3, a4, a5, a6, a7);
    CaptureExpansion(src, dst, "field");
    return result;
}

// ============================================================================
// v0.18.3.240 (#77): resolve {Var} inserts OURSELVES, at decode time.
// ============================================================================
//
// Why not just use the hook's cache? TIMING. The mod speaks from the dialog
// OPCODE hook (AMESW/MES), which runs BEFORE the renderer ever touches the
// text — so at first-speak the cache is necessarily empty and the number would
// still be missing (it would only appear on a later repeat). The .239 BAT log
// shows exactly that ordering: [GETSTR] -> [AMESW] Speaking -> [SHOW_DIALOG].
//
// The field expander (sub_4D4A80, disassembled above) reads its substitution
// values from fixed engine globals — it has no other source — so we can resolve
// them at decode time and be immune to the ordering. The mapping comes straight
// from the engine's own dispatch table at 0x4D4D08 (read out of FF8_EN.exe):
//
//   index (param - 0x20)   engine case      source
//     0                    0x4D4B60         dword [0x1D7DAA8]   number
//     1                    0x4D4B3A         dword [0x1D7DAB0]   number
//     2                    0x4D4B49         dword [0x1D7DAAC]   number
//     3, 4, 5              0x4D4B76         (no value loaded — emit nothing)
//     6                    0x4D4B59         string [0x1D7EABC]  FF8-encoded name
//
// The engine formats the number to a padded field and then strips the leading
// pad bytes (0x4D4B8D), so the visible result is the plain decimal value — that
// is what we emit. Digits are written as FF8 font codes ('0'..'9' = 0x21..0x2A)
// so the existing decoder renders them normally.
//
// This resolution is deliberately scoped to the FIELD-DIALOG decode path only.
// It must NOT go into the shared FF8TextDecode::DecodeByte: the menu/battle
// expander (sub_4A3260) takes its values from stack ARGUMENTS with a different
// case table, so resolving 0x0A against the field globals there would splice
// wrong numbers into menu and battle text.

// ============================================================================
// v0.18.3.242 (#77): THE ACTUAL MECHANISM — control code 0x04, not 0x0A.
// ============================================================================
//
// The .241 [GETSTR-HEX] dump settled it. The Tomb message is 19 bytes:
//
//   3F  57 72 73 62 63 6C 72  20  4D 48  20  52 6D 3B  20  04  20  3E
//   "   S  t  u  d  e  n  t   _   I  D   _   N  o  .   _  [04][20] "
//
// There is NO 0x0A anywhere. The placeholder is **0x04 followed by param 0x20**.
// Our decoder maps 0x04 to "page break" and emits ". " WITHOUT consuming the
// param — which is precisely why the line came out as "Student ID No. .  "
// (the phantom "." IS the missing number, and the orphaned 0x20 decoded as a
// space). Both earlier expander theories (sub_4A3260 / sub_4D4A80) were the
// wrong tree entirely: neither hook ever fired.
//
// The real chain, from the window text processor:
//   sub_4B9170  0x004B9216: codes 0x02-0x0F each CONSUME A PARAM BYTE.
//   sub_4B8B30  0x004B8BFB: `cmp ecx, 4` -> the code-4 case; reads the param,
//               and at 0x004B8C2C calls the resolver with (4<<8 | param).
//   sub_4B8E40  THE RESOLVER:
//                 0x004B8E56  param must be in [0x20 .. 0x27]  (8 slots)
//                 0x004B8E72  mov ecx, dword ptr [eax*4 + 0x1D2B4B0]  <-- VALUE
//                 0x004B8E8F+ divide-by-powers-of-ten loop -> digit chars,
//                             base glyph from [0x1D76610]; leading zeros are
//                             stripped at 0x004B8EB6.
//
// So the value is simply:  *(uint32_t*)(0x1D2B4B0 + param * 4)
// and for the Tomb ID (param 0x20) that is the dword at 0x1D2B530.
//
// We emit the decimal digits as FF8 font codes ('0'..'9' == 0x21..0x2A), which
// is exactly what the engine's digit loop produces, so the existing decoder
// renders them normally. snprintf("%u") gives us the same leading-zero
// suppression the engine does.
static const uintptr_t FIELD_VAR_TABLE_BASE = 0x01D2B4B0;  // dword[param]
static const uint8_t   FIELD_VAR_PARAM_MIN  = 0x20;
static const uint8_t   FIELD_VAR_PARAM_MAX  = 0x27;

// ============================================================================
// v0.18.3.245 (#78): NAME / LOCATION inserts — control codes 0x0C and 0x0D.
// ============================================================================
//
// The .244 BAT reproduced the bug in Xu's Dollet briefing — e.g. the line the
// mod spoke as:  "  has been under attack by the G-Army since about 72 hours
// ago."  The subject noun "Dollet" is cleanly gone (its trailing space kept),
// which is the exact fingerprint of a silently-dropped INLINE INSERT, the
// name/location sibling of the #77 number bug.
//
// In the same field text processor sub_4B8B30 that handles the 0x04 number
// case, the two adjacent cases are the name inserts:
//   0x004B8C74  code 0x0C: param -> call sub_47E970(param-0x20) -> FF8 string
//   0x004B8C8A  code 0x0D: param -> call sub_47EA30(param-0x20) -> FF8 string
// Each resolver just indexes a static table and returns a pointer to an
// FF8-encoded string (location names for 0x0C, other names for 0x0D). Our
// decoder currently drops both (0x0C consumes its param + emits nothing;
// 0x0D-0x1F emit nothing) -> the name vanishes.
//
// We resolve by calling the ENGINE'S OWN resolver via a function pointer,
// rather than re-deriving the table math. This is the #77 lesson applied: the
// three failed builds all came from reimplementing engine logic from the
// disassembly; calling the game's own function cannot get the table math wrong,
// and it automatically tracks whatever the engine currently has loaded. The
// resolvers are pure table lookups (read-only, no locks, no side effects), so
// calling them from the mod's decode path is safe; the call is SEH-guarded.
typedef char* (__cdecl *NameResolverFn)(int index);
static const uintptr_t FIELD_NAME_RESOLVER_0C = 0x0047E970;  // location names
static const uintptr_t FIELD_NAME_RESOLVER_0D = 0x0047EA30;  // other names

// ============================================================================
// v0.18.3.248 (#78): THE ACTUAL DOLLET MECHANISM — control code 0x0E.
// ============================================================================
//
// The .247 [VE-HEX] dump settled it (third time this pattern has paid off).
// Every dropped word in Xu's briefing is the SAME two bytes, e.g.:
//
//   "Xu " [0E 23] has been under attack by the G-Army ..."   -> "Dollet"
//   "...battle, [0E 23] abandoned their position..."          -> "Dollet"
//   "...is the [0E 23] Dukedom Parliament."                   -> "Dollet"
//   "...mopping up the [0E 23] troops..."                     -> "Dollet"
//
// NOT 0x04 (numbers, #77) and NOT 0x0C/0x0D (the .245 resolvers, which stay
// in but simply never match this code). The engine handler is the tail case
// of the same window text processor sub_4B8B30:
//
//   0x004B8CB8  cmp ecx, 0xE / jl ...        ; codes >= 0x0E land here
//   0x004B8CBD  mov dl, [ebp] / inc ebp      ; read + consume the param byte
//   0x004B8CC0  add ecx, -0xE                ; group = code - 0x0E
//   0x004B8CC3  sub dl, 0x20                 ; idx = (uint8)(param - 0x20)
//   0x004B8CC7+ eax = group*8 - group        ; group*7
//   0x004B8CDC  shl eax, 5                   ; group*224
//   0x004B8CDF  add eax, edx                 ; entry = group*224 + idx
//   0x004B8CD6  mov ecx, [0x1D2B80C]         ; TABLE pointer (runtime-loaded)
//   0x004B8CE4  test ecx, ecx / je -> skip   ; null table: emit NOTHING
//   0x004B8CF1  movzx edx, word [ecx]        ; entry count (first word)
//   0x004B8CF4  cmp edx, eax / jle -> skip   ; out of range: emit NOTHING
//   0x004B8D01  movzx esi, word [ecx+eax*2+2]; offset word (rel. table start)
//   0x004B8D06  add esi, ecx                 ; -> FF8-encoded string
//   0x004B8D0A  call 0x49a740 (copy) / 0x49a790 (strlen) ; splice into dst
//
// So the blob at [0x01D2B80C] is:  uint16 count; uint16 offsets[count]; then
// packed FF8 strings — a runtime-loaded shared-text table ("Dollet" is group 0
// entry 3 during the SeeD-exam chapter). Unlike 0x0C/0x0D there is NO
// standalone engine resolver to call — the lookup is inlined — but unlike #77
// there is also no math to re-derive from theory: the layout above is read
// straight off the executable, and the read below replicates it byte for byte,
// including the two silent-skip cases (null table / entry >= count), so a
// failed lookup emits exactly what the engine emits: nothing.
static const uintptr_t FIELD_DEFERRED_TABLE_PTR = 0x01D2B80C;

// Look up a 0x0E-family deferred-text entry and copy its FF8 bytes into out.
// Fully SEH-guarded, no C++ objects (C2712). Returns bytes written; 0 means
// "emit nothing", which is also the engine's behavior for a missing entry.
static size_t ResolveDeferredText(uint8_t code, uint8_t param,
                                  uint8_t* out, size_t outSize)
{
    size_t written = 0;
    __try {
        uintptr_t table = *(volatile uintptr_t*)FIELD_DEFERRED_TABLE_PTR;
        if (table != 0) {
            uint32_t entry = (uint32_t)(code - 0x0E) * 224u
                           + (uint32_t)(uint8_t)(param - 0x20);
            uint16_t count = *(volatile uint16_t*)table;
            if (entry < count) {
                uint16_t off = *(volatile uint16_t*)(table + 2 + entry * 2);
                const uint8_t* s = (const uint8_t*)(table + off);
                for (; written < outSize - 1; written++) {
                    uint8_t sb = s[written];
                    if (sb == 0x00) break;
                    out[written] = sb;
                }
            }
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        written = 0;
    }
    if (written < outSize) out[written] = 0x00;
    return written;
}

// SEH-guarded dword read. No C++ objects (C2712).
static bool SafeReadDword(uintptr_t addr, uint32_t* out)
{
    bool ok = false;
    __try {
        *out = *(volatile uint32_t*)addr;
        ok = true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        ok = false;
    }
    return ok;
}

// Call the engine's name resolver and copy the returned FF8 string into out.
// SEH-guarded (no C++ objects here — C2712). Returns bytes written, 0 on any
// fault or empty result.
static size_t ResolveEngineName(uintptr_t resolverAddr, int index,
                                uint8_t* out, size_t outSize)
{
    size_t written = 0;
    __try {
        NameResolverFn fn = (NameResolverFn)resolverAddr;
        const char* s = fn(index);
        if (s != nullptr) {
            const uint8_t* p = (const uint8_t*)s;
            for (; written < outSize - 1; written++) {
                uint8_t b = p[written];
                if (b == 0x00) break;
                out[written] = b;
            }
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        written = 0;
    }
    if (written < outSize) out[written] = 0x00;
    return written;
}

// Rewrite a raw FF8 field message, replacing each 0x04+param numeric insert
// with the engine's current value, encoded as FF8 font bytes. Everything else
// is copied through untouched. Returns the number of substitutions made.
//
// Critically, this runs BEFORE FF8TextDecode::Decode, so the decoder never sees
// the 0x04 and never emits its bogus ". " for it — the decoder itself stays
// untouched, which keeps menu and battle text (whose 0x04 semantics we have NOT
// verified) exactly as they are today.
static int FieldExpandRawVars(const uint8_t* src, size_t srcLen,
                              uint8_t* out, size_t outSize)
{
    int subs = 0;
    size_t o = 0;
    for (size_t i = 0; i < srcLen && o + 1 < outSize; i++) {
        uint8_t b = src[i];
        if (b == 0x00) break;

        // --- 0x04 + param: numeric insert (sub_4B8E40, #77) ---
        if (b == 0x04 && i + 1 < srcLen) {
            uint8_t param = src[++i];
            if (param < FIELD_VAR_PARAM_MIN || param > FIELD_VAR_PARAM_MAX) {
                continue;  // out of range: engine renders nothing, param consumed
            }
            uint32_t value = 0;
            if (!SafeReadDword(FIELD_VAR_TABLE_BASE + (uintptr_t)param * 4, &value)) {
                continue;
            }
            char digits[16];
            int n = snprintf(digits, sizeof(digits), "%u", (unsigned)value);
            for (int k = 0; k < n && o + 1 < outSize; k++) {
                out[o++] = (uint8_t)(0x21 + (digits[k] - '0'));  // FF8 '0'..'9'
            }
            subs++;
            continue;
        }

        // --- 0x0C / 0x0D + param: name / location insert (#78) ---
        // The engine's reader (sub_4B9170 @0x004B9216) consumes a param byte
        // for every code 0x02-0x0F, and sub_4B8B30 resolves 0x0C via sub_47E970
        // and 0x0D via sub_47EA30 with (param - 0x20). We call those directly.
        if ((b == 0x0C || b == 0x0D) && i + 1 < srcLen) {
            uint8_t param = src[++i];
            uintptr_t resolver = (b == 0x0C) ? FIELD_NAME_RESOLVER_0C
                                             : FIELD_NAME_RESOLVER_0D;
            uint8_t nameBuf[128];
            size_t n = ResolveEngineName(resolver, (int)param - 0x20,
                                         nameBuf, sizeof(nameBuf));
            for (size_t k = 0; k < n && o + 1 < outSize; k++) {
                out[o++] = nameBuf[k];   // already FF8-encoded; copy through
            }
            // n==0 -> engine had no string for this slot; emit nothing (the
            // param is still consumed, matching the engine's reader).
            subs++;
            continue;
        }

        // --- 0x0E (+ any higher code) + param: deferred shared-text insert ---
        // (#78, v0.18.3.248 — the Dollet mechanism; disassembly block above.)
        // The engine's tail case at 0x4B8CB8 handles every code >= 0x0E with a
        // group stride of 224 entries, so 0x0F etc. resolve through the same
        // table read. Codes 0x10+ never reach sub_4B8B30's chain in practice,
        // but if one did, the bounds check makes it a silent no-op — identical
        // to the engine.
        if (b >= 0x0E && b <= 0x0F && i + 1 < srcLen) {
            uint8_t param = src[++i];
            uint8_t nameBuf[128];
            size_t n = ResolveDeferredText(b, param, nameBuf, sizeof(nameBuf));
            for (size_t k = 0; k < n && o + 1 < outSize; k++) {
                out[o++] = nameBuf[k];   // already FF8-encoded; copy through
            }
            subs++;
            continue;
        }

        out[o++] = b;
    }
    out[o] = 0x00;
    return subs;
}

// Decode a raw dialog string with {Var} inserts resolved. Drop-in replacement
// for TrimDecoded(FF8TextDecode::Decode(raw, 512)) at every field-dialog decode
// site.
//
// Order of preference:
//   1. Our own expansion (deterministic, works on the FIRST speak).
//   2. The hook's captured engine output, if for some reason we substituted
//      nothing but the engine did (belt and braces; also our cross-check).
//   3. The plain decode, exactly as before .239 (no-regression fallback).
static std::string DecodeDialogWithExpansion(const void* raw,
                                             size_t maxBytes = 512)
{
    if (raw == nullptr) return std::string();
    // Callers already probe the pointer, but the copy is SEH-guarded anyway so
    // a text buffer that goes away mid-scan can't fault us.
    uint8_t buf[512];
    size_t cap = (maxBytes < sizeof(buf)) ? maxBytes : sizeof(buf);
    if (!SafeCopyEngineText(raw, buf, cap)) return std::string();

    // v0.18.3.245 (#78): raw-byte capture ON THE SCAN PATH. The .244 hex diag
    // lived in the GETSTR hook, but Xu's briefing comes through the AMESW
    // window-scan path, so it never fired. This dumps the raw bytes of any
    // message that contains an insert control code (0x04/0x0C/0x0D) here, where
    // the briefing actually flows — so this BAT confirms the exact code+param
    // even if the 0x0C/0x0D fix below already made it speak. Gate off once #78
    // is verified.
    // v0.18.3.246 (#78): UNCONDITIONAL raw-byte dump. The .245 build only
    // logged when a 0x04/0x0C/0x0D byte was present — and it fired for NEITHER
    // Dollet line, so the placeholder is none of those. Two wrong code guesses
    // in a row (#77 rule) => stop guessing, dump EVERY message's bytes and read
    // the code sitting where "Dollet" belongs. Capped so it can't flood; load a
    // save at/just before Xu's briefing so its lines fall in the first N.
    // v0.18.3.249: #78 VERIFIED on the .248 BAT (all four Dollet lines spoke;
    // [VE-HEX] showed 0E 23 beside each) -- diagnostic gated back off per the
    // project pattern. Flip to 1 if another silently-dropped insert appears.
#define VAR_EXPAND_HEX_DIAG 0
#if VAR_EXPAND_HEX_DIAG
    {
        size_t nlen = 0;
        while (nlen < cap && buf[nlen] != 0x00) nlen++;
        // v0.18.3.247: dedup by a SET of already-logged texts, not just the
        // last one. The .246 build kept only the previous text, so the two
        // windows the scanner alternates between (Xu / Quistis) each looked
        // "new" every tick and burned the whole 40-cap before the briefing
        // reached the Dollet lines. A set logs each DISTINCT message exactly
        // once, so the cap counts distinct messages as intended.
        static std::set<std::string> s_veHexSeen;
        if (nlen >= 4 && s_veHexSeen.size() < 80) {
            std::string plain = TrimDecoded(FF8TextDecode::Decode(buf, nlen));
            if (s_veHexSeen.find(plain) == s_veHexSeen.end()) {
                s_veHexSeen.insert(plain);
                std::string hex = FF8TextDecode::HexDump(buf, nlen);
                Log::Dialog("FieldDialog: [VE-HEX] len=%u text=\"%s\" bytes=%s",
                            (unsigned)nlen, plain.c_str(), hex.c_str());
            }
        }
    }
#endif

    uint8_t expBuf[640];
    int subs = FieldExpandRawVars(buf, cap, expBuf, sizeof(expBuf));

    if (subs > 0) {
        std::string expanded = TrimDecoded(
            FF8TextDecode::Decode(expBuf, sizeof(expBuf)));
        if (!expanded.empty()) {
            // v0.18.3.243: log once per distinct expansion. The window scanner
            // re-decodes the live text roughly once a second for as long as the
            // dialog is open, which flooded the .242 log with ~30 identical
            // [VAR-EXPAND] lines for one message. Speech was never affected
            // (the dedup upstream is on the decoded string), this is log noise
            // only.
            static std::string s_lastVarExpandLogged;
            if (expanded != s_lastVarExpandLogged) {
                s_lastVarExpandLogged = expanded;
                std::string plain = TrimDecoded(FF8TextDecode::Decode(buf, cap));
                Log::Dialog("FieldDialog: [VAR-EXPAND] %d insert(s): \"%s\" -> \"%s\"",
                            subs, plain.c_str(), expanded.c_str());
            }
            return expanded;
        }
    }

    std::string decoded = TrimDecoded(FF8TextDecode::Decode(buf, cap));

    std::string expanded;
    if (ExpandCacheLookup(raw, decoded, expanded)) {
        Log::Dialog("FieldDialog: [TEXTEXPAND-USE] \"%s\" -> \"%s\"",
                    decoded.c_str(), expanded.c_str());
        return expanded;
    }
    return decoded;
}
