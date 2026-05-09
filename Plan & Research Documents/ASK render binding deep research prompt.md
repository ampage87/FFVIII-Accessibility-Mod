# ASK render binding — disassembly research

**Date:** 2026-05-08
**Source:** FF8_EN.exe (Steam 2013, App ID 39150) static disassembly. Image base 0x00400000, .text 0x00401000–0x00B69000.
**Goal:** identify the engine mechanism that determines whether a populated `ff8_win_obj` slot is rendered, so v0.15.3+ can either (a) trigger that mechanism from the mod, or (b) call the engine's dialog-setup function directly.

---

## Background (from prior BATs)

v0.15.2.1 BAT confirmed via `LogProxySlotSnapshot` polling (1 Hz, 13 seconds straight) that a fully-populated `ff8_win_obj` slot — with all captured-from-real-ASK template values — is **not** rendered when the population happens from a DLL hook outside the script-VM context. Every snapshot showed our writes preserved exactly; the engine never modified the slot until the next field-to-battle transition wiped it.

That ruled out every "we're missing a field" hypothesis. The conclusion was that rendering must be bound to script-VM context — only slots that an active script has parked on via `opcode_ask` / `opcode_aask` get rendered. This document traces the disassembly to confirm that hypothesis and identify a concrete v0.15.3 path.

---

## CONFIRMED: field script-VM dispatch table is at 0xb8de94

The script-VM main loop is in the function starting at **0x0052A039** (in `FF8_EN_.text_0x00501000.asm`). The opcode dispatch is at **0x0052A647**:

```
0x0052A629:  mov      edx, dword ptr [0x1d9cf50]   ; bytecode pointer table
0x0052A634:  lea      eax, [edx + ecx*4]            ; index by program counter
0x0052A637:  push     eax                            ; push current instruction ptr
0x0052A638:  call     0x530760                       ; sub_530760 = JSM bytecode parser
0x0052A63D:  mov      ecx, dword ptr [esp + 0x1c]   ; recover script param (low 24 bits)
0x0052A641:  mov      edx, dword ptr [esp + 0x20]   ; recover opcode byte (high byte)
0x0052A645:  push     ecx                            ; arg2 = bytecode parameter
0x0052A646:  push     esi                            ; arg1 = script context (current entity)
0x0052A647:  call     dword ptr [edx*4 + 0xb8de94]  ; ** DISPATCH **
0x0052A64E:  add      esp, 0x14
0x0052A651:  test     al, 4                          ; check return-code bits
```

`sub_530760` (in same file) reads a 32-bit JSM instruction word:

```
0x00530760:  mov      eax, dword ptr [esp + 4]       ; *instruction
0x00530764:  mov      ecx, dword ptr [eax]
0x00530766:  test     ecx, 0xff000000                ; high byte non-zero?
0x0053076C:  je       0x5307a2                       ; PUSH-immediate path
0x0053076E:  test     ecx, 0x800000                  ; signed-param flag bit
0x00530774:  mov      eax, ecx
0x00530776:  je       0x53078d                       ; unsigned param path
0x00530778:  mov      edx, dword ptr [esp + 8]       ; *out_opcode
0x0053077C:  or       eax, 0xff000000                ; sign-extend low 24 bits
0x00530781:  shr      ecx, 0x18                      ; ecx = opcode (high byte)
0x00530784:  mov      dword ptr [edx], ecx
0x00530786:  mov      ecx, dword ptr [esp + 0xc]     ; *out_param
0x0053078A:  mov      dword ptr [ecx], eax
0x0053078C:  ret
```

This matches the project's existing memory: "PC JSM instructions: native LE uint32, high byte = opcode, low 24 bits = signed parameter (NOT PSX format)."

### Derived addresses

Given dispatch table base **0xb8de94** and opcode index, each handler's function pointer lives at `0xb8de94 + N*4`:

| Opcode | Index | Handler ptr address | Notes |
|--------|-------|---------------------|-------|
| `opcode_mes`     | 0x47 | `[0xb8df90]` | Show MES dialog (text-only) |
| `opcode_ask`     | 0x4A | `[0xb8df9c]` | Show ASK dialog (text + multi-choice) |
| `opcode_aask`    | 0x4B | `[0xb8dfa0]` | Variant ASK |
| `opcode_battle`  | 0x69 | `[0xb8e038]` | Battle entry — **already used by mod** for ChaseBattleFreeze hook |

The mod already reads `[0xb8e038]` at runtime to install the ChaseBattleFreeze MinHook. The same mechanism can read the ASK handler addresses.

### Other dispatch tables nearby

- `0xb8de4c` — separate dispatch table called from `0x0051C4B9` only. 0x48 bytes (18 entries) before the main opcode table. Possibly a subroutine table or inner-AASK choice table. Worth investigating later.
- `0xb8e078` — referenced as immediate value loaded into an object's `+0x18` field at `0x006B65E7` and `0x006B6DB8`. Could be a sister table or pointer-to-table reference; not directly dispatched.

---

## Handler calling convention

From the dispatch site, every handler is invoked with:

- **arg1 (esi)** — script context pointer. The script-VM main loop holds this in `esi` throughout. The struct has fields at `[esi+0x160]` (state flags), `[esi+0x170]` (current opcode area), `[esi+0x174]/[esi+0x175]` (flag/bitmask bytes), `[esi+0x176]` (program counter as word), `[esi+0x248]` (state byte). Used for control-flow tracking.
- **arg2 (ecx)** — bytecode parameter (signed 24-bit from JSM instruction).

Returns **al** as a status bitfield:
- `al & 0x01` — used in branching (purpose TBD)
- `al & 0x02` — advance the program counter (`inc word ptr [esi+0x176]`)
- `al & 0x04` — clear opcode flag bit at `[esi+0x175]`

**ChaseBattleFreeze returns 3 (= bits `0x02 | 0x01`) to NO-OP a battle**; that matches "advance PC past battle, set unknown flag" — exactly what we want when the script tries to enter a battle we want to skip. Same return pattern would apply to a synthesized ASK invocation.

---

## What the disassembly does NOT reveal directly

The handler **addresses** are stored in the .data section, written there at process startup by the PE loader from the static initializers in the binary. The available disassembly dumps only `.text`, so we can see the dispatch site (which uses the table indirectly) but not the table contents themselves. To get the handler addresses we need either:

1. **A runtime dump** — read `[0xb8de94 + N*4]` from inside the running mod. This is the path that worked for ChaseBattleFreeze and is the cheapest first step.
2. **A `.data` dump** — extract bytes from `0xb8de94..0xb8e0XX` from the live process or from the on-disk PE image. More invasive than option 1; only useful if option 1 isn't enough.

Option 1 is recommended for v0.15.3.

---

## What we still need to learn

The disassembly tells us the dispatch architecture but not what `opcode_ask` actually does once invoked. Specifically:

1. **Does `opcode_ask` populate an `ff8_win_obj` slot directly, or does it call into a shared "set window" helper?**

   FFNx canary's source memory mentions a `set_window_object` function — likely a shared helper that both `opcode_mes` and `opcode_ask` call after extracting their parameters. If so, `set_window_object` is the function the mod can call directly without needing a synthesized script context.

2. **What state does `opcode_ask` write *outside* the slot itself?**

   The 13-second SLOT-SNAP proves the engine isn't rendering based on `ff8_win_obj.state == 0x0D` alone. There is some other state — most likely a "current dialog slot" pointer or a script-context flag — that the renderer reads to decide which slot is active. Finding that state is the answer to the v0.15.x puzzle.

3. **Does the renderer read script-VM context, or a global "active window" variable?**

   If the renderer reads `[esi+...]` from the active script context, we cannot easily generate dialogs from outside a script. If the renderer reads a global like `pCurrentDialogSlot`, we can write to that global from the mod and the render will fire.

---

## v0.15.3 recommended approach (two phases)

### Phase 1 — runtime dump diagnostic (small, ~20 lines of code)

Add to `chase_diag.cpp` (or a new `dialog_diag.cpp`) a one-shot logging routine that runs once at mod initialization (or behind an Alt-keypress to keep it opt-in):

```cpp
void DumpOpcodeHandlers() {
    constexpr uintptr_t TABLE = 0xb8de94;
    static const struct { uint8_t op; const char* name; } interesting[] = {
        {0x47, "opcode_mes"},
        {0x4A, "opcode_ask"},
        {0x4B, "opcode_aask"},
        {0x69, "opcode_battle"},
    };
    for (auto& e : interesting) {
        uintptr_t handler = *(uintptr_t*)(TABLE + e.op * 4);
        Log::Field("[disasm] %s (0x%02X) handler @ 0x%08X", e.name, e.op, handler);
    }
}
```

Aaron BATs once. The log gives us four real handler addresses. We then look those up in the on-disk disassembly to read the handlers' code.

### Phase 2 — trace the handler

Once we have the address of (e.g.) `opcode_ask`, we read its disassembly to identify:

1. The "set window" helper it calls (likely shared with `opcode_mes`).
2. Any global state it touches outside the `ff8_win_obj` slot.

That answers the rendering-binding question definitively. Three plausible outcomes:

- **Outcome A — it's a callable helper.** `opcode_ask` reads bytecode params and calls `set_window_object(slot, text_id, mode, choices...)`. We can call that helper directly from the mod with synthesized parameters. **Cleanest path; most likely outcome.**
- **Outcome B — it writes a global "active dialog" pointer.** `opcode_ask` writes `&pWindowsArray[slot]` (or a slot index) into a global the renderer reads. We populate the slot AND the global from the mod. Slightly messier; requires finding the global.
- **Outcome C — the renderer is bound to the script-VM context.** `opcode_ask` only sets `[esi+something]` flags; the renderer walks active script contexts and renders their dialogs. This would mean we cannot render arbitrary dialogs without spawning a script-VM context, and would push the whole feature out of v0.15.x scope. Unlikely but possible.

Phase 2 is a single follow-up session once Phase 1's diagnostic ships and BATs.

---

## Future-use scenarios

This work is reusable for several upcoming features beyond the chase:

1. **Custom save-confirm dialogs** — when Aaron triggers a save action that needs confirmation, the mod can show a styled engine dialog instead of TTS-only.
2. **Auto-drive route-choice dialogs** — when v0.15.3+ ships auto-drive with multiple route options, present them as engine ASK.
3. **Settings menu shortcuts** — F1-styled audio settings could surface via engine dialog for sighted-spectator visibility.
4. **In-game error / status notifications** — "GF junctioned successfully", "Item full inventory", etc., without taking over the screen.

The Phase 2 disassembly findings will be the foundational knowledge for all of these.

---

## Anchors recap (for future sessions)

| Address | Symbol | Notes |
|---------|--------|-------|
| 0x0052A039 | `script_vm_main_loop` | Function start; main JSM dispatcher |
| 0x0052A647 | dispatch site | `call dword ptr [edx*4 + 0xb8de94]` |
| 0x00530760 | `parse_jsm_instruction` | Reads 32-bit JSM word, splits opcode/param |
| 0x00543790 | `world_dialog_assign_text` | World-map dialog setup (separate system, table at 0xc761a0, stride 0x10 — NOT field) |
| 0xb8de94 | `pExecuteOpcodeTable` | Field script-VM opcode dispatch table |
| 0xb8de4c | other dispatch table | 18 entries before main; called from 0x0051C4B9 |
| 0x1d9cf50 | bytecode pointer table | Loaded into `edx` at dispatch site |
| 0x1ce477c | (loaded into ebp) | Used in main loop prologue |
| 0xc761a0 | world dialog text array | World-map only; stride 0x10 |
| 0x20400c4 | world dialog text-data table | World-map only |

Recommend caching these in `Plan & Research Documents/ASK render binding deep research.md` (project naming convention) for future-session lookup.
