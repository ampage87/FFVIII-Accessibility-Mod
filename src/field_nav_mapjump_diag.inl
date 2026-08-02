// field_nav_mapjump_diag.inl — v0.17.7.5.2
//
// Diagnostic dispatch table hooks on MAPJUMP / MAPJUMP3 / DISCJUMP /
// MAPJUMPO / WORLDMAPJUMP. v0.17.7.5 added VM stack args dump and engine
// RESULT dump. v0.17.7.5.1 added a contiguous varblock dump for 0x80-0xFF.
// v0.17.7.5.2 adds firing IP read from [ctx + 0x176] so the runtime trace
// can be cross-referenced against resolver per-instruction IPs.
//
// Why this exists:
//   v0.17.7.0..3 attempted static resolution of variable-dispatch destinations
//   (PSHM_W <addr>; MAPJUMP pattern) using the methodPshmAddrs scanner. After
//   widening the scanner in v0.17.7.3, BAT showed 0 found writers across ~220
//   captured writes covering 5 fields. Static analysis cannot tell us what
//   the values at those PSHM addresses ARE at run time. Aaron also corrected
//   that the apparent "Headmaster's Office 7" varblock win in v0.17.7.1.2 was
//   a misidentification — even the apparent success was bogus.
//
//   This build sidesteps the dead end: at the instant MAPJUMP-family opcodes
//   fire, log the varblock contents at every PSHM address any SCREEN_BOUND
//   line on the current field reads from, plus the player's current field /
//   triangle / entity for correlation. After ONE BAT cycle of Aaron walking
//   through known exits, the (field, line) -> destination mapping falls out
//   of the log directly. No static resolution, no scanner.
//
// Mechanism:
//   FF8 dispatches script opcodes through pExecuteOpcodeTable[opcode]. The
//   table is in .data (writable). We save the original function pointer for
//   each MAPJUMP variant, then overwrite the entry with our hook. The hook
//   logs + calls the original. Same dispatch table patch pattern as the
//   (disabled) SET3 hook in field_navigation.cpp — see Initialize().
//
//   IMPORTANT: this is DIFFERENT from the SET3 problem. SET3 (opcode 0x1E)
//   is hot-path enough that ANY interception hangs the infirmary cutscene.
//   MAPJUMP variants only fire on field transition — at most once every few
//   seconds during normal play — so the same patch technique is safe here.
//
// Output:
//   [MAPJUMP-HOOK] <opcode> field=<N> '<name>' inline_param=<P>
//   [MAPJUMP-HOOK]   player entity=<idx> tri=<T>
//   [MAPJUMP-HOOK]   varblock[0x00AA]=<v> [0x00AF]=<v> [0x00E2]=<v> ...
//
// Included from field_navigation.cpp inside namespace FieldNavigation, after
// field_nav_opcode_hooks.inl. Exposes one entry point: MapjumpDiag::Install().

namespace MapjumpDiag {

// FF8 field VM varblock base. Verified by FFNx ff8_data.cpp:
//   ff8_externals.field_vars_stack_1CFE9B8 = get_absolute_value(opcode_pshm_w, 0x1E);
// FFNx names it `field_vars_stack_1CFE9B8` exactly because the address is
// 0x01CFE9B8 on every released version of the binary. PSHM_W and POPM_W read
// and write here using a byte offset (the parameter is a signed int16 address).
static const uintptr_t VARBLOCK_BASE = 0x01CFE9B8;

// v0.17.7.5: Engine's "transition request" global block at 0x01CE4760.
// Per FF8_EN.exe disassembly of opcode_mapjump @ 0x00521A20 and
// opcode_mapjump3 @ 0x00521AC0:
//   0x01CE4760  BYTE  transition_type (1 = MAPJUMP/MAPJUMP3, 6 = MAPJUMPO, ...)
//   0x01CE4762  WORD  destFieldId          <-- the destination field
//   0x01CE4764  WORD  arg2 (X)
//   0x01CE4766  WORD  arg3 (Y)
//   0x01CE4768  WORD  arg4 (Z, MAPJUMP3 only)
//   0x01CE476C  WORD  inline_param         (from opcode word bits 0-15)
//   0x01CE476E  WORD  topOfStack (last-pushed, first-popped)
// We read these AFTER chaining to the original handler to capture what the
// engine resolved for this transition.
static const uintptr_t TRANS_DEST_FIELD = 0x01CE4762;
static const uintptr_t TRANS_TYPE_BYTE  = 0x01CE4760;

// v0.17.7.5: VM context layout, from opcode_mapjump3 disassembly.
//   ctx + 0x184  BYTE  VM stack pointer (signed -- movsx in engine code)
//   ctx + sp*4   WORD  top of stack (each slot is 4 bytes wide; only the
//                      low 16 bits hold meaningful data for MAPJUMP args)
// Reading the args BEFORE chaining lets us cross-check the static resolver
// (MapjumpResolver::Run in field_archive_jsm_mapjump_resolver.inl) against
// ground truth from the actual engine VM state at fire time.
// v0.17.7.5.2: VM context layout extended -- the IP of the firing opcode.
//   ctx + 0x176  WORD  current instruction pointer (dword index into the
//                      current method's bytecode buffer)
// Per the dispatcher disassembly at 0x0052A621:  mov cx, [esi + 0x176]
// followed by the dispatch call, the IP at hook entry is the IP of THIS
// opcode. It's incremented post-dispatch at 0x0052A675 (`inc word ptr
// [esi + 0x176]`), so reading it before chaining gives the firing IP.
static const ptrdiff_t VMCTX_SP_OFFSET = 0x184;
static const ptrdiff_t VMCTX_IP_OFFSET = 0x176;

// Opcode handler prototype per FFNx:
//   ff8_externals.opcode_popm_w = (int(*)(void*, int))execute_opcode_table[0x0D];
// All opcode handlers share the same __cdecl (void* ctx, int param) signature.
typedef int (__cdecl *OpcodeFunc_t)(void* ctx, int param);

// Saved original function pointers — used for chaining and Shutdown() restore.
static uint32_t s_origMapjump      = 0;
static uint32_t s_origMapjump3     = 0;
static uint32_t s_origDiscjump     = 0;
static uint32_t s_origMapjumpo     = 0;
static uint32_t s_origWorldmapjump = 0;

static bool s_installed = false;

// PSHM addresses we've seen SCREEN_BOUND lines read from in v0.17.7.x BAT
// logs (bghall_1/3, bgroad_3, bgmon_1, etc). Updated when scanner finds new
// ones in future builds. Keeping this list small and explicit beats dumping
// the entire 4KB varblock — the log lines stay readable.
static const uint16_t kRelevantAddrs[] = {
    0x0000, 0x0002, 0x00AA, 0x00AF, 0x00E2, 0x00E6, 0x01DF, 0x01F6, 0x023A, 0x0401
};
static const size_t kNumAddrs = sizeof(kRelevantAddrs) / sizeof(kRelevantAddrs[0]);

// Single per-hook logger. opcodeName is a literal from the call site so it's
// safe to take by pointer and pass straight to printf-style Log::Field.
static void LogMapjumpFired(const char* opcodeName, int inlineParam)
{
    __try {
        uint16_t fieldId = 0xFFFF;
        const char* fieldName = "?";
        if (FF8Addresses::pCurrentFieldId != nullptr) {
            fieldId = *FF8Addresses::pCurrentFieldId;
        }
        if (FF8Addresses::pCurrentFieldName != nullptr) {
            fieldName = FF8Addresses::pCurrentFieldName;
        }

        Log::Field("FieldNavigation: [MAPJUMP-HOOK] %s fired on field=%u '%s' inline_param=%d (0x%X)",
                   opcodeName, (unsigned)fieldId, fieldName, inlineParam, inlineParam);

        // Player entity + triangle, for cross-checking which SCREEN_BOUND line
        // the player was crossing when MAPJUMP fired. Correlate with the
        // SETLINE coords captured statically in s_capturedLines[].
        int playerIdx = FF8Addresses::GetPlayerEntityIndex();
        if (playerIdx >= 0) {
            uint16_t tri = FF8Addresses::GetEntityTriangleId(playerIdx);
            // v0.18.3.301 (#91 R1): player XY at fire time. The triangle alone
            // needs a walkmesh lookup to interpret; the coordinate can be
            // compared directly against the captured line centres.
            //
            // This is the missing bit for labelling the prison stairs. gpbig1a
            // carries TWO self-destination lines -- line1 at (-2150,-197) and
            // line2 at (-2275,269), only ~470 units apart on the west wall --
            // and one is up while the other is down. Which is which cannot be
            // read out of the static data: gateway Z is (0,0) everywhere
            // (.298), and the nav_data trail only records TRIANGLE changes, so
            // it went quiet 13-49 s before each jump and never captured the
            // crossing itself. Logging the position AT the fire, next to the
            // floor the jump produces, pins it in one ordinary descent.
            float pjx = 0.0f, pjy = 0.0f;
            bool gotPj = GetEntityPos(playerIdx, pjx, pjy);
            if (gotPj) {
                Log::Field("FieldNavigation: [MAPJUMP-HOOK]   player entity=%d tri=%u "
                           "pos=(%.0f,%.0f)",
                           playerIdx, (unsigned)tri, pjx, pjy);
            } else {
                Log::Field("FieldNavigation: [MAPJUMP-HOOK]   player entity=%d tri=%u "
                           "pos=unresolved",
                           playerIdx, (unsigned)tri);
            }
        } else {
            Log::Field("FieldNavigation: [MAPJUMP-HOOK]   player entity unresolved");
        }

        // Varblock dump. PSHM_W reads a 16-bit word at byte offset `addr` from
        // VARBLOCK_BASE. We mirror that addressing here: cast base to uint8_t*
        // and read uint16_t at (base + addr).
        const uint8_t* vb = (const uint8_t*)VARBLOCK_BASE;
        char line1[256] = {};
        char line2[256] = {};
        int o1 = 0;
        int o2 = 0;
        for (size_t i = 0; i < kNumAddrs; i++) {
            uint16_t addr = kRelevantAddrs[i];
            uint16_t val = *(const uint16_t*)(vb + addr);
            char* buf = (i < kNumAddrs / 2) ? line1 : line2;
            int* off  = (i < kNumAddrs / 2) ? &o1   : &o2;
            int n = snprintf(buf + *off, 256 - *off, "[0x%04X]=%u(0x%04X) ",
                             (unsigned)addr, (unsigned)val, (unsigned)val);
            if (n > 0) *off += n;
        }
        Log::Field("FieldNavigation: [MAPJUMP-HOOK]   varblock %s", line1);
        if (o2 > 0) {
            Log::Field("FieldNavigation: [MAPJUMP-HOOK]   varblock %s", line2);
        }

        // v0.17.7.5.1: Contiguous varblock dump for the 0x80-0xFF range.
        // The static resolver's recently-picked PSHM addresses concentrate in
        // 0x00A5-0x00E6 (e.g. bghall_2 squallsd@0xA5, bghall_5 selphie@0xE0,
        // bghall_5 irvine@0xAA, bghall_3 quistis@0xE2). Dumping the full
        // 0x80-0xFF window at MAPJUMP fire time shows whether the destField
        // value lives at one of these slots at fire time (timing question)
        // or whether it's somewhere else entirely (addressing question).
        //
        // Split into 4 lines of 16 entries each so each log line stays
        // under ~256 chars and individual entries remain readable.
        for (int chunk = 0; chunk < 4; chunk++) {
            uint16_t base = (uint16_t)(0x80 + chunk * 32);
            char wide[300] = {};
            int  woff = 0;
            for (int i = 0; i < 16 && woff < 280; i++) {
                uint16_t addr = (uint16_t)(base + i * 2);
                uint16_t val = *(const uint16_t*)(vb + addr);
                if (val != 0) {
                    woff += snprintf(wide + woff, 300 - woff,
                                     "[0x%04X]=%u(0x%04X) ",
                                     (unsigned)addr, (unsigned)val, (unsigned)val);
                }
            }
            if (woff > 0) {
                Log::Field("FieldNavigation: [MAPJUMP-HOOK]   varblock(0x%04X-0x%04X) %s",
                           (unsigned)base, (unsigned)(base + 30), wide);
            }
        }

        // v0.18.3.298 (#90): D-District Prison floor-variable hunt. LOGGING ONLY.
        //
        // The prison shaft re-uses two archives for many floors: 23 of the
        // transitions in the 2026-07-31 BAT were 795 -> 795, i.e. the lift
        // changed floor without changing fieldId, so FieldAnnounce::Update()
        // bailed on `curId == s_lastAnnouncedFieldId` and Aaron heard nothing
        // at all. Before anything can announce "Floor N" we need to know which
        // variable N lives in.
        //
        // Candidates carried over from the log analysis:
        //   0x01A5 (421) -- the SET3 anchor every shaft entity positions from
        //                   (saveline0 / s_light / door all PSHM it)
        //   0x01B4 (436) -- the prison-wide state var the #85 [STATE-GUARD]
        //                   captures use; observed 1 / 13 / 29, so probably a
        //                   bitmask rather than a floor index, but sample it
        //   0x01B5 (437) -- its neighbour, never sampled
        // The existing 0x80-0xFE window above already covers 0x00C8 / 0x00D4,
        // which correlated loosely with inline_param and then broke after
        // 15:07 -- included here only so the two windows can be diffed.
        //
        // BYTE values, not words: the #85 state code compares the live BYTE
        // ("[STATE-GROUP] ... live byte=1"), and 0x01A5 is an odd address the
        // word-stepped loops above would skip entirely.
        //
        // Gated to the prison so this is silent everywhere else in the game.
        if ((fieldId >= 0x0319 && fieldId <= 0x032E) || fieldId == 0x03C5) {
            Log::Field("FieldNavigation: [FLOOR-PROBE] field=%u '%s' "
                       "[0x01A5]=%u [0x01B4]=%u [0x01B5]=%u (bytes) "
                       "[0x01A4]w=%u [0x01B4]w=%u",
                       (unsigned)fieldId, fieldName,
                       (unsigned)*(const uint8_t*)(vb + 0x01A5),
                       (unsigned)*(const uint8_t*)(vb + 0x01B4),
                       (unsigned)*(const uint8_t*)(vb + 0x01B5),
                       (unsigned)*(const uint16_t*)(vb + 0x01A4),
                       (unsigned)*(const uint16_t*)(vb + 0x01B4));

            // Contiguous byte sweep either side of the candidates, non-zero
            // only, so a floor counter sitting at an address we did not guess
            // still shows up. Two lines of 32 bytes.
            for (int chunk = 0; chunk < 2; chunk++) {
                uint16_t fbase = (uint16_t)(0x0190 + chunk * 32);
                char pbuf[300] = {};
                int  poff = 0;
                for (int i = 0; i < 32 && poff < 280; i++) {
                    uint16_t addr = (uint16_t)(fbase + i);
                    uint8_t  val  = *(const uint8_t*)(vb + addr);
                    if (val != 0) {
                        poff += snprintf(pbuf + poff, 300 - poff,
                                         "[0x%04X]=%u ", (unsigned)addr, (unsigned)val);
                    }
                }
                if (poff > 0) {
                    Log::Field("FieldNavigation: [FLOOR-PROBE]   bytes(0x%04X-0x%04X) %s",
                               (unsigned)fbase, (unsigned)(fbase + 31), pbuf);
                }
            }
        }
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        // A bad address in pCurrentFieldName or VARBLOCK_BASE shouldn't take
        // the game down. Swallow and continue to chain into the original.
        Log::Field("FieldNavigation: [MAPJUMP-HOOK] exception in logger (op=%s code=0x%08X)",
                   opcodeName, GetExceptionCode());
    }
}

// Per-opcode hook stubs. Each one logs then chains to the saved original
// function pointer. __cdecl matches the call site at update_field_entities
// + 0x657:  call dword ptr [edx*4 + 0xb8de94]  ;  add esp, 0x14  (caller cleanup)
//
// v0.17.7.5: Each stub now ALSO reads the 5 (or 4) VM stack args from ctx
// before chaining, and reads the resolved destField global after chaining.
// This produces the validation data we need to confirm the static resolver
// in MapjumpResolver matches the engine's actual behavior.
static void LogMapjumpVmStack(const char* opcodeName, void* ctx, int argCount)
{
    __try {
        if (ctx == nullptr) return;
        const uint8_t* base = (const uint8_t*)ctx;
        // v0.17.7.5.2: Firing IP. Read BEFORE the stack pointer so the
        // log ordering matches the engine's own sequence (decode-then-dispatch).
        // The IP is a dword index into the current method's bytecode buffer;
        // to match a resolver's absolute scriptData index we'd need the
        // method's start IP, but inline_param already pairs uniquely with
        // a single bytecode word, so absolute matching isn't required for
        // the immediate triage.
        uint16_t firingIp = *(const uint16_t*)(base + VMCTX_IP_OFFSET);
        Log::Field("FieldNavigation: [MAPJUMP-HOOK]   %s firing IP=%u (0x%04X)",
                   opcodeName, (unsigned)firingIp, (unsigned)firingIp);
        // sp is a SIGNED byte per the disassembly's movsx; we read it as
        // int8_t and let normal int promotion handle negative values (which
        // would indicate a stack underflow in the engine itself -- we'd
        // still see something useful in the log if that happens).
        int sp = (int)*(const int8_t*)(base + VMCTX_SP_OFFSET);
        char line[256] = {};
        int  off = 0;
        off += snprintf(line + off, 256 - off,
                        "sp=%d [", sp);
        for (int i = 0; i < argCount && off < 220; i++) {
            // arg index i counts from the top: i=0 is top-of-stack
            // (last-pushed), i=argCount-1 is the deepest (destField).
            int slot = sp - i;
            const uint8_t* slotPtr = base + (intptr_t)slot * 4;
            uint16_t v = 0;
            __try {
                v = *(const uint16_t*)slotPtr;
            }
            __except (EXCEPTION_EXECUTE_HANDLER) {
                v = 0xDEAD;
            }
            const char* tag = "";
            if (i == argCount - 1) tag = "=destField";
            else if (i == 0)       tag = "=top";
            off += snprintf(line + off, 256 - off,
                            "%s[%d]=%u%s",
                            i == 0 ? "" : " ",
                            slot, (unsigned)v, tag);
        }
        snprintf(line + off, 256 - off, "]");
        Log::Field("FieldNavigation: [MAPJUMP-HOOK]   %s VM stack: %s",
                   opcodeName, line);
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        Log::Field("FieldNavigation: [MAPJUMP-HOOK]   %s VM stack read FAILED (code=0x%08X)",
                   opcodeName, GetExceptionCode());
    }
}

static void LogMapjumpResult(const char* opcodeName)
{
    __try {
        uint8_t  ttype = *(const uint8_t*)TRANS_TYPE_BYTE;
        uint16_t dest  = *(const uint16_t*)TRANS_DEST_FIELD;
        const char* destName = nullptr;
        if (dest < FIELD_DISPLAY_NAMES_COUNT)
            destName = FIELD_DISPLAY_NAMES[dest];
        Log::Field("FieldNavigation: [MAPJUMP-HOOK]   %s engine RESULT: "
                   "transition_type=%u destField=%u (%s)",
                   opcodeName, (unsigned)ttype, (unsigned)dest,
                   destName ? destName : "?");
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        Log::Field("FieldNavigation: [MAPJUMP-HOOK]   %s engine RESULT read FAILED (code=0x%08X)",
                   opcodeName, GetExceptionCode());
    }
}

static int __cdecl HookedMapjump(void* ctx, int param) {
    LogMapjumpFired("MAPJUMP", param);
    LogMapjumpVmStack("MAPJUMP", ctx, 4);
    int r = ((OpcodeFunc_t)s_origMapjump)(ctx, param);
    LogMapjumpResult("MAPJUMP");
    return r;
}
static int __cdecl HookedMapjump3(void* ctx, int param) {
    LogMapjumpFired("MAPJUMP3", param);
    LogMapjumpVmStack("MAPJUMP3", ctx, 5);
    int r = ((OpcodeFunc_t)s_origMapjump3)(ctx, param);
    LogMapjumpResult("MAPJUMP3");
    return r;
}
static int __cdecl HookedDiscjump(void* ctx, int param) {
    LogMapjumpFired("DISCJUMP", param);
    LogMapjumpVmStack("DISCJUMP", ctx, 5);
    int r = ((OpcodeFunc_t)s_origDiscjump)(ctx, param);
    LogMapjumpResult("DISCJUMP");
    return r;
}
static int __cdecl HookedMapjumpo(void* ctx, int param) {
    LogMapjumpFired("MAPJUMPO", param);
    LogMapjumpVmStack("MAPJUMPO", ctx, 4);
    int r = ((OpcodeFunc_t)s_origMapjumpo)(ctx, param);
    LogMapjumpResult("MAPJUMPO");
    return r;
}
static int __cdecl HookedWorldmapjump(void* ctx, int param) {
    LogMapjumpFired("WORLDMAPJUMP", param);
    // WORLDMAPJUMP arg count is unverified; log 4 args defensively.
    LogMapjumpVmStack("WORLDMAPJUMP", ctx, 4);
    int r = ((OpcodeFunc_t)s_origWorldmapjump)(ctx, param);
    LogMapjumpResult("WORLDMAPJUMP");
    return r;
}

// Install the five hooks by overwriting their entries in pExecuteOpcodeTable.
// Returns true if at least one hook was installed.
//
// Called from FieldNavigation::Initialize() AFTER FF8Addresses::Resolve() has
// populated pExecuteOpcodeTable. Safe to call multiple times (early return on
// re-entry via s_installed).
static bool Install()
{
    if (s_installed) {
        Log::Field("FieldNavigation: [MAPJUMP-HOOK] Install() called twice — ignoring.");
        return true;
    }

    if (FF8Addresses::pExecuteOpcodeTable == nullptr) {
        Log::Field("FieldNavigation: [MAPJUMP-HOOK] cannot install — opcode table not resolved.");
        return false;
    }

    uint32_t* table = FF8Addresses::pExecuteOpcodeTable;

    // Snapshot originals. These match FF8Addresses::opcode_mapjump etc., but
    // we read directly from the table to be unambiguous about what we're
    // saving and to keep this code self-contained for restore-on-Shutdown.
    s_origMapjump      = table[0x29];
    s_origMapjump3     = table[0x2A];
    s_origDiscjump     = table[0x38];
    s_origMapjumpo     = table[0x5C];
    s_origWorldmapjump = table[0x10D];

    Log::Field("FieldNavigation: [MAPJUMP-HOOK] installing dispatch table hooks");
    Log::Field("FieldNavigation: [MAPJUMP-HOOK]   table base = 0x%08X", (uint32_t)(uintptr_t)table);
    Log::Field("FieldNavigation: [MAPJUMP-HOOK]   orig MAPJUMP      [0x029] = 0x%08X -> 0x%08X",
               s_origMapjump,      (uint32_t)(uintptr_t)&HookedMapjump);
    Log::Field("FieldNavigation: [MAPJUMP-HOOK]   orig MAPJUMP3     [0x02A] = 0x%08X -> 0x%08X",
               s_origMapjump3,     (uint32_t)(uintptr_t)&HookedMapjump3);
    Log::Field("FieldNavigation: [MAPJUMP-HOOK]   orig DISCJUMP     [0x038] = 0x%08X -> 0x%08X",
               s_origDiscjump,     (uint32_t)(uintptr_t)&HookedDiscjump);
    Log::Field("FieldNavigation: [MAPJUMP-HOOK]   orig MAPJUMPO     [0x05C] = 0x%08X -> 0x%08X",
               s_origMapjumpo,     (uint32_t)(uintptr_t)&HookedMapjumpo);
    Log::Field("FieldNavigation: [MAPJUMP-HOOK]   orig WORLDMAPJUMP [0x10D] = 0x%08X -> 0x%08X",
               s_origWorldmapjump, (uint32_t)(uintptr_t)&HookedWorldmapjump);

    // Patch each entry under VirtualProtect. .data is normally already RW on
    // this PE but VP-wrap is cheap insurance and is the pattern the existing
    // (disabled) SET3 path uses. We patch entries one at a time so a failure
    // on entry N leaves entries 0..N-1 cleanly installed.
    struct PatchSlot { uint32_t idx; uint32_t newVal; const char* name; };
    PatchSlot slots[] = {
        { 0x29,  (uint32_t)(uintptr_t)&HookedMapjump,      "MAPJUMP"      },
        { 0x2A,  (uint32_t)(uintptr_t)&HookedMapjump3,     "MAPJUMP3"     },
        { 0x38,  (uint32_t)(uintptr_t)&HookedDiscjump,     "DISCJUMP"     },
        { 0x5C,  (uint32_t)(uintptr_t)&HookedMapjumpo,     "MAPJUMPO"     },
        { 0x10D, (uint32_t)(uintptr_t)&HookedWorldmapjump, "WORLDMAPJUMP" },
    };

    int installed = 0;
    for (size_t i = 0; i < sizeof(slots) / sizeof(slots[0]); i++) {
        uint32_t* entry = &table[slots[i].idx];
        DWORD oldProtect = 0;
        if (VirtualProtect(entry, sizeof(uint32_t), PAGE_READWRITE, &oldProtect)) {
            *entry = slots[i].newVal;
            DWORD restore = 0;
            VirtualProtect(entry, sizeof(uint32_t), oldProtect, &restore);
            installed++;
        } else {
            Log::Field("FieldNavigation: [MAPJUMP-HOOK]   VirtualProtect FAILED for %s @ 0x%08X (err=%lu)",
                       slots[i].name, (uint32_t)(uintptr_t)entry, GetLastError());
        }
    }

    s_installed = (installed > 0);
    Log::Field("FieldNavigation: [MAPJUMP-HOOK] %d / %d hooks installed (installed=%s)",
               installed, (int)(sizeof(slots) / sizeof(slots[0])),
               s_installed ? "true" : "false");
    return s_installed;
}

// Restore original dispatch table entries. Called from FieldNavigation::Shutdown().
// Idempotent; no-op if Install() never ran or already shut down.
static void Restore()
{
    if (!s_installed) return;
    if (FF8Addresses::pExecuteOpcodeTable == nullptr) {
        s_installed = false;
        return;
    }

    uint32_t* table = FF8Addresses::pExecuteOpcodeTable;
    struct RestoreSlot { uint32_t idx; uint32_t origVal; };
    RestoreSlot slots[] = {
        { 0x29,  s_origMapjump      },
        { 0x2A,  s_origMapjump3     },
        { 0x38,  s_origDiscjump     },
        { 0x5C,  s_origMapjumpo     },
        { 0x10D, s_origWorldmapjump },
    };

    for (size_t i = 0; i < sizeof(slots) / sizeof(slots[0]); i++) {
        if (slots[i].origVal == 0) continue;
        uint32_t* entry = &table[slots[i].idx];
        DWORD oldProtect = 0;
        if (VirtualProtect(entry, sizeof(uint32_t), PAGE_READWRITE, &oldProtect)) {
            *entry = slots[i].origVal;
            DWORD restore = 0;
            VirtualProtect(entry, sizeof(uint32_t), oldProtect, &restore);
        }
    }

    s_installed = false;
    Log::Field("FieldNavigation: [MAPJUMP-HOOK] dispatch table entries restored.");
}

}  // namespace MapjumpDiag
