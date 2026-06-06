# Field dialog system disassembly analysis

**Date:** 2026-05-08
**Source:** static disassembly of FF8_EN.exe (Steam 2013, FF8 1.2 US NV) + the mod's own runtime address-resolution log (`Logs/ff8_mod.log` from v0.15.2.1 BAT)
**Goal:** identify the exact mechanism the engine uses to decide whether a populated `ff8_win_obj` slot is rendered, so we can drive the engine renderer from a DLL hook (chase ASK overlay; future engine-rendered MES/ASK from the mod).

---

## Background — what v0.15.2.1 BAT proved

The `LogProxySlotSnapshot` polling logged a fully-populated proxy slot for 13 seconds straight. Every snapshot showed our writes preserved exactly — `state=0x0D`, `mode1=0x1000`, `trans=0x1000`, `geom=[0x50,0x0A,0xCC,0x5D]`, `t1=t2` non-NULL, `firstQ=1 lastQ=2 curQ` tracking, `cb1=cb2=NULL` — but the engine never rendered the slot. The conclusion at the time was that rendering must be bound to script-VM context. **That conclusion was partly wrong: the binding is to a set of byte bitmasks on the global game object, not to script-VM context.**

---

## Confirmed runtime addresses (Steam 2013, FF8 1.2 US NV)

Pulled from `Logs/ff8_mod.log` after a normal game launch. The mod resolves these dynamically via the same FFNx-style chain used everywhere else (see `src/ff8_addresses.cpp::Resolve`).

| Symbol | Address | How resolved |
|---|---|---|
| `pExecuteOpcodeTable` | `0x00B8DE94` | `update_field_entities + 0x65A` (absolute) |
| `pGameObjGlobal` | `0x00B8EE90` | `MOV ESI, [addr]` early in `main_loop` |
| `*pGameObjGlobal` (the actual game object) | `0x01CFE9B8` | dereferenced |
| `pWindowsArray` | `0x01D2B330` | `set_window_object + 0x11` (absolute) |
| `opcode_mesw`     [0x46] | `0x00528E40` | `pExecuteOpcodeTable[0x46]` |
| `opcode_mes`      [0x47] | `0x00528F20` | `pExecuteOpcodeTable[0x47]` |
| `opcode_messync`  [0x48] | `0x00529900` | `pExecuteOpcodeTable[0x48]` |
| `opcode_ask`      [0x4A] | `0x00529520` | `pExecuteOpcodeTable[0x4A]` |
| `opcode_winclose` [0x4C] | `0x00529B60` | `pExecuteOpcodeTable[0x4C]` |
| `opcode_amesw`    [0x64] | `0x00529020` | `pExecuteOpcodeTable[0x64]` |
| `opcode_ames`     [0x65] | `0x005291E0` | `pExecuteOpcodeTable[0x65]` |
| `opcode_aask`     [0x6F] | `0x005296C0` | `pExecuteOpcodeTable[0x6F]` ← note: **0x6F**, not 0x4B |
| `field_get_dialog_string` | `0x00530750` | `opcode_mes + 0x5D` (rel call) |
| `set_window_object` (MES variant) | `0x004A0410` | `opcode_mes + 0x66` (rel call) |
| `set_window_object_ASK` | `0x004A04E0` | called from opcode_ask + 0x57 |
| `sub_4A0620` (open transition) | `0x004A0620` | called from MES, ASK, AASK |
| `sub_49FD50` (set current dialog slot) | `0x0049FD50` | called from MES |
| `sub_49FD60` (get current dialog slot) | `0x0049FD60` | called from ASK |
| `pCurrentDialogSlot` (BYTE) | `0x01D2B51C` | written by `sub_49FD50` |
| `pAskAnswerBuffer` (DWORD) | `0x01D9CDC4` | written by ASK setup, polled for answer |
| `pFieldMessageTable` | `0x01CE4778` | base passed to `field_get_dialog_string` |

The script-VM dispatcher is at `0x0052A647` (inside the function at `0x0052A039`); the JSM bytecode parser is at `0x00530760`. None of those are needed for mod-driven dialog injection — just for understanding.

---

## opcode_mes anatomy (0x00528F20)

Disassembly with annotations:

```
sub  esp, 8
push ebx
mov  ebx, [esp+0x10]              ; ebx = script_context (a.k.a. esi from main loop)
push esi
push edi
movsx eax, byte [ebx+0x184]       ; load script-VM stack pointer (signed byte)
mov  edi, [ebx+eax*4]             ; edi = stack[sp]   (likely the dialog_id arg)
mov  esi, [ebx+eax*4-4]           ; esi = stack[sp-1] (window slot index)
lea  eax, [ebx+eax*4]
call 0x49F5D0                     ; pop_script_args() → returns max valid args
cmp  esi, eax
jl   .ok
push 0x388                        ; assertion — bad slot index
push 0x4D
call 0x406A1A                     ; debug print
add  esp, 8

.ok:
mov  eax, [0xB8EE90]              ; pGameObjGlobal
mov  edx, 1
mov  ecx, esi                     ; ecx = slot index
shl  edx, cl                      ; edx = 1 << slot
test byte [eax+0xD3], dl          ; gameObj.window_active_mask & (1 << slot)?
je   .free
; slot busy → return 5 (wait, retry next frame)
pop  edi; pop esi
mov  eax, 5
pop  ebx; add esp, 8; ret

.free:
mov  ecx, [0x1CE4778]             ; field message table base
push edi                          ; arg2 = dialog_id
push ecx                          ; arg1 = msg_base
call 0x530750                     ; field_get_dialog_string → eax = decoded text ptr
mov  edi, eax
push edi                          ; text ptr
push esi                          ; slot index
call 0x4A0410                     ; set_window_object(slot, text)
push esi
call 0x4A0620                     ; sub_4A0620(slot) — open/animate
push esi
call 0x49FD50                     ; sub_49FD50(slot) — MARK AS CURRENT SLOT
mov  eax, [0xB8EE90]              ; pGameObjGlobal
mov  dl, 1
mov  ecx, esi
add  esp, 0x18
shl  dl, cl
mov  cl, [eax+0xD3]
or   cl, dl
mov  [eax+0xD3], cl               ; SET gameObj.window_active_mask bit
mov  cl, [eax+0xD4]
or   cl, dl
mov  dx, [esp+0xC]
mov  [eax+0xD4], cl               ; SET gameObj.mes_active_mask bit
; rest writes per-slot data into AUX TABLE at 0x01D9CF98
mov  cl, [ebx+0x184]
mov  ax, [esp+0xE]
add  cl, 0xFE                     ; decrement script SP by 2
shl  esi, 4                       ; esi = slot * 16
mov  [ebx+0x184], cl
mov  cx, [esp+0x10]
mov  [esi+0x1D9CFA0], edi         ; aux_table[slot].text = decoded_text
mov  [esi+0x1D9CF98], dx          ; aux_table[slot].field_0 (mes mode flag?)
mov  dx, [esp+0x12]
mov  [esi+0x1D9CF9A], ax          ; aux_table[slot].field_2
mov  [esi+0x1D9CF9C], cx          ; aux_table[slot].field_4
mov  [esi+0x1D9CF9E], dx          ; aux_table[slot].field_6
pop  edi; pop esi
mov  eax, 3                       ; return 3 = advance PC
pop  ebx; add esp, 8; ret
```

**Six-step recipe to make a MES dialog appear from outside the script VM:**

1. `set_window_object(slot, text_ptr)` — populates `pWindowsArray[slot]` with text and clears bookkeeping
2. `sub_4A0620(slot)` — kicks off the open animation (writes `[+0x16]=1`, `[+0x1E]=0x0200`, `[+0x2D]=0` on the slot)
3. `sub_49FD50(slot)` — writes `slot` into the byte at `0x01D2B51C`, the global "current dialog slot"
4. Set bit `(1 << slot)` in `gameObj[+0xD3]` ("any window active" mask)
5. Set bit `(1 << slot)` in `gameObj[+0xD4]` ("MES active" mask)
6. Optionally write the per-slot aux block at `0x01D9CF98 + slot*0x10` (mode flags + 6 bytes of context — mostly used for paging behaviour, not strictly required for a single-page MES)

**This is the missing step that v0.15.0–0.15.2 never did:** none of the iterations set the gameObj bitmasks, so the engine's render loop ignored the slot.

---

## opcode_ask anatomy (0x00529520)

```
sub esp, 8; push ebx; push ebp; push esi
mov esi, [esp+0x18]               ; esi = script_context
push edi
movsx eax, byte [esi+0x184]       ; script SP

; Pull six args off the script stack: dialog_id, slot, firstQ, lastQ, curQ, ?
mov ecx, [esi+eax*4-8]            ; arg
mov edx, [esi+eax*4-0xC]
mov ebx, [esi+eax*4]
mov ebp, [esi+eax*4-4]
mov edi, [esi+eax*4-0x14]         ; slot index
mov ecx, [esi+eax*4-0x10]
... (saves args to local stack)

call 0x49F5D0                     ; pop args; check max
cmp edi, eax; jl .ok
... (assert)

.ok:
mov cl, [esi+0x174]               ; load script-VM "ASK pending" tracking byte
mov al, [esi+0x175]
mov edx, 1
shl edx, cl
test al, dl
je .answered                      ; bit clear → user already answered, finalize

; bit set → first call this frame, do the setup
mov eax, [0xB8EE90]               ; pGameObjGlobal
mov edx, 1
mov ecx, edi                      ; ecx = slot
shl edx, cl
test byte [eax+0xD2], dl          ; gameObj.ask_active_mask & (1 << slot)?
je .alloc                         ; bit clear → ASK not yet active → set it up
; bit set → already set up, return 5 (wait for answer)
pop edi; pop esi; pop ebp
mov eax, 5
pop ebx; add esp, 8; ret

.alloc:
call 0x49FD60                     ; get current dialog slot byte
mov ecx, [esp+0x1C]
mov edx, [esp+0x10]
push ebx
mov [0x1D9CDC4], eax              ; stash current dialog slot for later answer correlation
mov eax, [esp+0x18]
push ebp; push ecx
mov ecx, [0x1CE4778]              ; field message table base
push edx; push eax; push ecx
call 0x530750                     ; field_get_dialog_string
add esp, 8
push eax                          ; text ptr
push edi                          ; slot
call 0x4A04E0                     ; set_window_object_ASK(slot, text, ...choice args)
push edi
call 0x4A0620                     ; open transition
mov eax, [0xB8EE90]
mov dl, 1
mov ecx, edi
add esp, 0x1C
mov bl, [eax+0xD3]
mov word [esi+0x204], 0           ; clear script_context.ask_answer_word
shl dl, cl
pop edi; pop esi; pop ebp
or  bl, dl
mov [eax+0xD3], bl                ; SET window_active_mask bit
mov cl, [eax+0xD2]
or  cl, dl
pop ebx
mov [eax+0xD2], cl                ; SET ask_active_mask bit
mov eax, 1                        ; return 1 = wait for answer
add esp, 8; ret
```

So ASK is structurally the same as MES, with three differences:

1. **It does NOT call `sub_49FD50`** — i.e. it does not become the "current dialog slot". (MES does this so the renderer foregrounds it; ASK presumably co-renders alongside any open MES.)
2. **It uses `set_window_object_ASK` (0x4A04E0)** instead of the MES variant. The ASK helper takes additional params for choice ranges.
3. **It sets `gameObj+0xD2` bit instead of `+0xD4`.** Both still set `+0xD3`.

The "wait for answer" loop runs every frame: each call checks the script-VM context bit at `[esi+0x175]`. While the user is choosing, the bit stays set and ASK keeps returning 1 (= "stay parked on this opcode"). When the user picks, something else clears the bit; then on the next call ASK falls through to `.answered` (not disassembled here yet) which reads the answer code from `[0x1D9CDC4]`-related state and writes it to `[esi+0x204]` (script context) before advancing.

**For mod-driven ASK** (without script-VM hooks) the recipe is:

1. `set_window_object_ASK(slot, text, firstQ, lastQ, curQ, ...)` — see below for full signature
2. `sub_4A0620(slot)`
3. Set bit `(1 << slot)` in `gameObj[+0xD3]` and `gameObj[+0xD2]`
4. Each frame: read current cursor from window slot's `+0x2B` (curQ); read confirm/cancel from engine input state. When user commits, clear the gameObj bits and the slot's state byte.

---

## set_window_object (MES variant, 0x004A0410)

```
mov  eax, [esp+4]                 ; arg1 = slot
push ebx
push esi
xor  ebx, ebx
lea  eax, [eax+eax*2]             ; eax = slot * 3
lea  eax, [eax+eax*4]             ; eax = slot * 15
lea  esi, [eax*4 + 0x1D2B330]     ; esi = pWindowsArray + slot * 60   ← stride 0x3C confirmed
mov  eax, [esp+0x10]              ; arg2 = text_ptr
push esi
mov  byte [esi+0x28], bl          ; clear +0x28 (state low byte?)
mov  [esi+0x08], eax              ; text_data1
mov  [esi+0x0C], eax              ; text_data2 (same buffer for MES)
mov  word [esi+0x12], bx          ; clear +0x12
mov  al, [esi+0x17]               ; nybble swap on the byte at +0x17 (color/style?)
and  al, 0x0F
or   al, 0x70
mov  cl, al
and  cl, 0xF0
shr  al, 4
or   cl, al
mov  [esi+0x17], cl
mov  [esi+0x24], ebx              ; clear state (+0x24)
call 0x4B9290                     ; (sub_4B9290 — not yet disassembled, presumably calls
                                  ; the text-measurement / line-break path)
mov  al, 0xFF
add  esp, 4
mov  [esi+0x29], al               ; firstQ = 0xFF
mov  [esi+0x2A], al               ; lastQ = 0xFF
mov  [esi+0x19], bl               ; clear +0x19
pop  esi; pop ebx; ret
```

Note: this writes ONLY `+0x08, +0x0C, +0x12, +0x17, +0x24, +0x28, +0x29, +0x2A, +0x19`. It does NOT write geometry (+0x00..+0x07), mode1 (+0x1A), open_close_transition (+0x1C), state-during-open (+0x24 stays 0), callbacks (+0x34/+0x38), or the choice fields (+0x2B, +0x2C). Geometry is preserved from prior writes; mode1/transition are written by `sub_4A0620`; state advances on its own once the bitmasks are set.

## set_window_object_ASK (0x004A04E0) — selected fields

```
0x004A04E0:  mov eax, [esp+0x14]      ; arg5 = curQ?
0x004A04E4:  mov ecx, [esp+0xC]       ; arg2 = firstQ?
... (clamp curQ to [firstQ, lastQ])
0x004A04F8:  mov eax, [esp+4]         ; arg1 = slot
0x004A050E:  lea esi, [edi + 0x1D2B330]  ; esi = slot base
0x004A0515:  mov [esi+0x28], bl       ; clear
0x004A0518:  mov [esi+0x08], eax      ; text_data1
0x004A051B:  mov [esi+0x0C], eax      ; text_data2
0x004A051E:  mov word [esi+0x12], bx
... nybble swap at +0x17 same as MES
0x004A0536:  mov [esi+0x24], ebx      ; clear state
0x004A0539:  call 0x4B9290
0x004A0548:  mov al, 0xFF
0x004A054B:  mov [esi+0x29], al       ; firstQ = 0xFF (placeholder)
0x004A054E:  mov [esi+0x2A], al       ; lastQ = 0xFF
0x004A0551:  mov al, [esp+0x1C]       ; pull real firstQ
0x004A0555:  mov [esi+0x19], bl
0x004A0558:  mov [esi+0x29], dl       ; firstQ = real value
0x004A055B:  mov dl, [esp+0x20]
0x004A055F:  mov [esi+0x2A], al       ; lastQ = real value
0x004A0562:  mov [esi+0x2C], cl       ; +0x2C — second cursor field?
0x004A0565:  mov [edi+0x1D2B35B], dl  ; ASK-specific aux byte
0x004A056B:  pop edi; pop esi; pop ebx; ret
```

So ASK takes (slot, text, firstQ, lastQ, curQ_clamp_min, curQ_clamp_max, ?). Note: this writes `+0x29` (firstQ) and `+0x2A` (lastQ) as captured in the v0.15.1.2 snapshot. The `+0x2B` (curQ) is set by the engine input handler later, not in this helper.

## sub_4A0620 (open transition variants)

There are four nearly-identical "open" functions in a row. They differ only in what value they write to `[+0x1E]`:

| Address | `[+0x16]` | `[+0x1E]` | `[+0x2D]` | Comment |
|---|---|---|---|---|
| `0x4A0620` | 1 | 0x0200 | 0 | Fast open (used by MES + ASK) |
| `0x4A0640` | 1 | 0x1000 | 0 | Slow open (matches captured `mode1=0x1000`) |
| `0x4A0660` | (unchanged) | 0xFE00 | (unchanged) | Close (write only `[+0x1E]`) |
| `0x4A0680` | (unchanged) | 0xF000 | (unchanged) | Slow close |

So the engine has parametric open/close transitions. The captured ASK had `+0x1A=0x1000`, `+0x1C=0x1000` — those are `mode1` and `open_close_transition`, which appear to be set by `set_window_object_ASK + 0x4B9290` rather than by `sub_4A0620` directly. The disassembly above doesn't fully cover that — `sub_4B9290` needs a follow-up trace.

## sub_49FD50 / sub_49FD60 (current-dialog-slot getter/setter)

Trivially simple:

```
0x0049FD50:  mov al, [esp+4]              ; al = slot
0x0049FD54:  mov byte [0x1D2B51C], al     ; pCurrentDialogSlot = slot
0x0049FD59:  ret

0x0049FD60:  movsx eax, byte [0x1D2B51C]  ; return pCurrentDialogSlot (sign-extended)
0x0049FD67:  ret
```

`pCurrentDialogSlot @ 0x01D2B51C` is a single-byte global. The renderer foregrounds whatever slot is currently set here; ASK reads it to correlate the answer back to the dialog the user is responding to.

---

## v0.15.3+ implementation plan

Given everything above, mod-driven engine-rendered dialogs are tractable in v0.15.3.

### Phase 1 — engine MES from the mod (verifies the recipe)

Add to `chase_diag.cpp` (or a new `dialog_inject.cpp`) a helper:

```cpp
namespace DialogInject {

// Show a static text dialog in the engine. text_buf must persist for the
// dialog's lifetime. Returns the slot number used, or -1 if no slot free.
int ShowMes(const char* text_buf);

// Programmatic close — clears the gameObj bits + slot state.
void CloseMes(int slot);

}
```

`ShowMes` does the six-step recipe above. The text buffer needs to be in the FF8 dialog encoding (or a plain ASCII buffer if `field_get_dialog_string` is bypassed by passing a pre-decoded pointer; `set_window_object` doesn't actually decode anything, it just stores the pointer).

For test bind: F12-diag mode + a hotkey to fire a fixed test message. If the dialog renders in-engine, the recipe is confirmed and we can move to ASK.

### Phase 2 — engine ASK from the mod (replaces the deferred chase ASK feature gap)

Add `ShowAsk(text_buf, num_choices, default_choice) → choice_index`. Internally:

1. `set_window_object_ASK(slot, text, 1, num_choices, default_choice, default_choice, …)`
2. `sub_4A0620(slot)`
3. Set bits in `gameObj+0xD2`, `+0xD3`
4. Each frame in `Update()`: read current cursor from `pWindowsArray[slot] + 0x2B`. Update via mod-driven Up/Down/Enter as today. On commit, clear the gameObj bits, set slot state to 0, return the cursor value.

This re-enables the engine-rendered chase ASK with full visual cursor — the open feature gap from v0.15.2.2.

### Phase 3 — generalize to other scenarios

The same primitive supports:
- Custom save-confirm dialogs
- Auto-drive route-choice prompts
- In-game status notifications (item full, GF junctioned, …)
- Settings shortcuts surfaced as engine dialogs for sighted-spectator visibility

---

## What we still don't fully understand

- **`sub_4B9290`** — called by both `set_window_object` variants right after writing the text pointers. Probably text measurement / paginate. If the dialog renders garbled or with wrong size, this is the suspect. Worth tracing in a follow-up.
- **The exact contents of the per-slot aux table at `0x01D9CF98`.** opcode_mes writes 6 bytes per slot here — likely flags for paging behaviour, MES mode (talk vs sysmsg), and timing. Probably zeroable without harm for one-page test dialogs.
- **The "answered" branch of opcode_ask** (instructions starting at the `je .answered` target) reads the answer back to the script context. We don't need it for mod-driven ASK because we can read `pWindowsArray[slot]+0x2B` (the cursor) directly when the user presses Enter, but we'd need to trace it to understand what state ASK leaves behind on close.
- **`sub_4A0620` vs `sub_4A0640`.** The captured ASK had open-transition `0x1000`, suggesting ASK ought to call `4A0640` not `4A0620`. But the disassembly shows `opcode_ask` calling `4A0620`. Either the captured value comes from elsewhere or my reading is off. Worth verifying with a fresh capture once Phase 1 is in.

---

## Anchors recap

| Address | Symbol | File | Purpose |
|---|---|---|---|
| 0x00528F20 | `opcode_mes`           | 0x501000.asm | MES handler |
| 0x00529520 | `opcode_ask`           | 0x501000.asm | ASK handler |
| 0x005296C0 | `opcode_aask`          | 0x501000.asm | AASK handler (index 0x6F) |
| 0x004A0410 | `set_window_object`    | 0x401000.asm | MES window setup |
| 0x004A04E0 | `set_window_object_ASK`| 0x401000.asm | ASK window setup |
| 0x004A0620 | `sub_4A0620`           | 0x401000.asm | Open transition (fast) |
| 0x004A0640 | `sub_4A0640`           | 0x401000.asm | Open transition (slow) |
| 0x004A0660 | `sub_4A0660`           | 0x401000.asm | Close transition (fast) |
| 0x004A0680 | `sub_4A0680`           | 0x401000.asm | Close transition (slow) |
| 0x0049FD50 | `set_current_dialog_slot` | 0x401000.asm | Write `pCurrentDialogSlot` |
| 0x0049FD60 | `get_current_dialog_slot` | 0x401000.asm | Read `pCurrentDialogSlot` |
| 0x00530750 | `field_get_dialog_string` | 0x501000.asm | Decode dialog text by id |
| 0x004B9290 | `sub_4B9290`           | 0x401000.asm | Text measurement (TBD) |
| 0x0052A039 | `script_vm_main_loop`  | 0x501000.asm | Field VM dispatcher (info only) |
| 0x0052A647 | dispatch site          | 0x501000.asm | `call [edx*4 + 0xB8DE94]` |
| 0x00B8DE94 | `pExecuteOpcodeTable`  | .data | Field opcode dispatch table (256 entries) |
| 0x00B8EE90 | `pGameObjGlobal`       | .data | Holds pointer to game object |
| 0x01CFE9B8 | game object base       | .bss | `*pGameObjGlobal` at runtime |
| `+0xD2`    | gameObj.ask_active_mask  | offset | per-slot bitmask |
| `+0xD3`    | gameObj.window_active_mask | offset | per-slot bitmask (renderer reads this) |
| `+0xD4`    | gameObj.mes_active_mask  | offset | per-slot bitmask |
| 0x01D2B330 | `pWindowsArray`        | .bss | 8 × 0x3C-byte slots |
| 0x01D2B51C | `pCurrentDialogSlot`   | .bss | byte: foreground MES slot |
| 0x01D9CDC4 | `pAskCorrelation`      | .bss | DWORD: ASK answer correlation |
| 0x01D9CF98 | aux table              | .bss | 8 × 0x10-byte per-slot blocks |
| 0x01CE4778 | `pFieldMessageTable`   | .bss | base for `field_get_dialog_string` |

---

## Follow-up findings (2026-05-08, after the initial doc was written)

Further disassembly of `show_dialog` (`0x0049FEB0`) and the window-system initializer `sub_4A0880` (`0x004A0880`) refined and partially **corrected** the hypothesis above.

### Correction — the gameObj bitmasks are NOT the render trigger

Grepping the disassembly for byte accesses to `[reg+0xD2]`, `[reg+0xD3]`, `[reg+0xD4]` (the bitmask offsets) returned ~80 hits, **all in `FF8_EN_.text_0x00501000.asm`**. That file is where the field-opcode handlers live. The renderer files (`0x401000.asm` and the graphics-heavy higher-address files) have ZERO byte accesses to these offsets.

What the bitmasks actually are: **per-slot allocation flags that the script-VM opcodes use to refuse double-allocation.** opcode_mes tests `gameObj+0xD3` bit before allocating; opcode_ask tests `gameObj+0xD2`. If the bit is set, the opcode returns 5 ("wait") instead of overwriting an in-use slot. opcode_winclose clears the bits. Setting these bits from a mod won't make a slot render — it'll just make subsequent opcode_mes/ask calls in the SAME slot return early.

### What show_dialog (the actual per-frame renderer) does

```
show_dialog(slot):
    edi = (signed) word [slot*0x3C + 0x1E]   ; transition velocity
    eax = (signed) word [slot*0x3C + 0x1C]   ; current open_close_transition
    edi += eax                               ; advance
    edi = clamp(edi, 0, 0x1000)
    [slot+0x1C] = edi                        ; write back

    if edi == 0:                             ; fully closed
        [slot+0x16] = 0
        return

    draw_box(...)                            ; draws the bordered window box

    if pCurrentDialogSlot != slot:           ; foreground check
        skip text drawing
    else if [slot+0x08] == 0:                ; text_data1 NULL
        skip text drawing
    else if [slot+0x1C] < 0x1000:            ; not fully open
        skip text drawing
    else:
        eax = [slot+0x24]                    ; state field
        if eax > 0x11:
            return
        jump [eax*4 + 0x4A034C]              ; dispatch state-machine handler
```

So the rendering decision tree is:

1. `slot.open_close_transition (+0x1C)` advances each frame by `slot.velocity (+0x1E)`, clamped to `[0, 0x1000]`.
2. If transition > 0, the window box is drawn (background + border, no text).
3. Text is drawn only if `pCurrentDialogSlot @ 0x01D2B51C` == this slot AND transition is fully open AND `text_data1` is non-null.
4. Once text is being drawn, the `state` field at `+0x24` selects an entry in a 0x12-entry state machine at `0x004A034C` (states 0..0x11). State 0 zero-inits per-slot draw counters and advances to state 1.

### `+0x1E` velocity values written by the open/close transition functions

| Address | `[+0x16]` | `[+0x1E]` velocity | Effect |
|---|---|---|---|
| `0x4A0620` | 1 | `+0x0200` | Open at 0x200/frame (8 frames to fully open) |
| `0x4A0640` | 1 | `+0x1000` | Open immediately (1 frame) |
| `0x4A0660` | (unchanged) | `-0x0200` | Close at 0x200/frame |
| `0x4A0680` | (unchanged) | `-0x1000` | Close immediately |

opcode_mes calls `0x4A0620` (slow open). opcode_ask also calls `0x4A0620`. The captured ASK in v0.15.1.2 had `+0x1C=0x1000` (steady-state, fully open) and `+0x1A=0x1000` (`mode1` field, possibly the "target transition" or animation type); the velocity at `+0x1E` was outside the captured snapshot range.

### Why v0.15.x's slot was never rendered — the actual reason

The runtime log shows `show_dialog` is at `0x0049FEB0`. Grepping the disassembly for `call 0x49feb0` returns **zero matches.** show_dialog is not called by any direct CALL instruction. It is **registered as a per-slot callback** via the `sub_4B6210` / `sub_4B6230` registry family in `sub_4A0880` (the window-system initializer):

```
sub_4A0880:                                  ; one-time window-system init
    [pCurrentDialogSlot] = 0xFF              ; "no foreground slot"
    ...
    push 0x4A09A0                            ; per-frame handler #1
    push 0
    call 0x4B6230                            ; register for slot 0
    push 0x4A0C00                            ; per-frame handler #2 (looks like input/state)
    push 0
    call 0x4B6210                            ; register for slot 0
    ... continues for other slots ...
```

So each slot needs to have its drawer callback REGISTERED with the engine. v0.15.x wrote bytes into `pWindowsArray[1]` but never registered slot 1 with the engine's window-system registry, which is why show_dialog was never invoked for that slot.

### Updated v0.15.3 plan (revised)

The engine is more elaborate than yesterday's six-step recipe assumed. The right primitive isn't "populate the slot manually + set bitmasks" — it's **"call the engine's own opcode_mes / opcode_ask functions with a synthesized script context."** Both functions:

1. Pull args off the script-VM stack (we'd need to fake this)
2. Call `set_window_object` / `set_window_object_ASK` (already understood)
3. Call `sub_4A0620` (already understood)
4. Call `sub_49FD50` for MES (already understood)
5. Set the gameObj bitmask bits (we now know these are allocation flags, not render triggers)

The missing piece in v0.15.x is that the **slot registration in the engine's window-system registry** (via `sub_4A0880` at startup) only covers slots that the engine knows about. Slots populated externally aren't part of the registry, so their `show_dialog` callback never fires.

**Two viable v0.15.3 paths:**

**Path A — call the engine's opcode handlers with synthesized context.** Build a fake `script_context` struct with:
- `[+0x184]` = script SP byte (set to 1)
- `[+0x180+0]` = arg0 (slot index, 1)
- `[+0x180+4]` = arg1 (dialog_id)
- Other fields as needed
Then `call opcode_mes(&fake_ctx)` directly. The engine sees a normal opcode invocation, all callbacks register, dialog renders. **Cleanest path; biggest unknowns are the script_context layout (probably already documented in `Plan & Research Documents/PSHM_W*.md`) and the bytecode-stack args.**

**Path B — hook the engine's per-frame dispatcher (`sub_4A0880`'s registered callbacks) to expose registration directly.** More invasive but doesn't require synthesizing a script context. We'd register an extra slot (#1, since slot 0 is the main dialog) and supply our own `show_dialog`-equivalent callback.

Path A is the lower-risk start since it reuses the engine's own setup logic verbatim. Phase 1 of v0.15.3 should be a tiny test: build a fake script context, call `opcode_mes(&ctx)` once, see if a test dialog appears. If yes, the recipe is solid and we move to ASK (Path A) for v0.15.3 chase ASK.

### Anchors added by this follow-up

| Address | Symbol | Purpose |
|---|---|---|
| 0x004A034C | dialog state machine table | 18 entries, indexed by `slot.state` (+0x24) |
| 0x004A0880 | `init_window_system` | Called once at startup; registers per-slot callbacks |
| 0x004A09A0 | window callback handler #1 | Registered by `sub_4A0880` (likely text drawer) |
| 0x004A0C00 | window callback handler #2 | Registered by `sub_4A0880` (likely state advancer) |
| 0x004B6210 | callback registry insert (variant 1) | Used to install per-slot handlers |
| 0x004B6230 | callback registry insert (variant 2) | Used to install per-slot handlers |
| 0x01D76608 | draw-state counter | Incremented by 0x80 per frame in render path |
| `slot+0x1E` | open_close velocity (signed word) | `+0x1C` += this each frame, clamped 0..0x1000 |
| `slot+0x1C` | open_close transition (current) | 0=closed, 0x1000=fully open |
| `slot+0x16` | (cleared to 0 when fully closed) | possibly "render-active" flag |
