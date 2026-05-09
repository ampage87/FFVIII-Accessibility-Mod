**Purpose & context**

Aaron is the sole developer of the FF8 Accessibility Mod -- a `dinput8.dll` injection for Final Fantasy VIII Steam 2013 edition (App ID 39150, FF8_EN.exe + FFNx v1.23.x) that makes the game playable for blind players via Windows SAPI text-to-speech, navigation assistance, and entity catalogs. Aaron is blind himself and is the primary tester, using NVDA as his screen reader.

**Project root:** `C:/Users/ampag/OneDrive/Documents/FFVIII-Accessibility-Mod/FF8_OriginalPC_mod/`

GitHub: `ampage87/FFVIII-Accessibility-Mod`. Last pushed: **v0.15.3** (commit `bc4d5358`, 2026-05-09 22:41 UTC). Local tree: **v0.15.4 BAT'd successfully, NOT pushed.**

---

## v0.15.4 BAT result: COMPLETE SUCCESS

Phase 1 is proven. Engine-rendered dialog injection via Path A works. Aaron pressed F12 in field `doani1_2` (Dollet Comm Tower top), and:

- `opcode_mes` returned 3 (advance/success).
- Dialog hook spoke the field's msg 0: "Selphie / "Wanna go up?" / Go up / Stay".
- Slot 1's `+0x1C` (open_close_transition) advanced from 0 -> 0x400 -> 0x1000 within 125 ms (2 frames).
- `+0x1E` velocity armed at 0x200, state machine progressed 0 -> 1 -> 7.
- `gameObj.D3/D4` bit set for slot 1 (allocation flags).
- `show_dialog` callback fired for slot 1, confirming the per-slot callback registration the v0.15.x bitmask attempts were missing.
- F11 screenshot confirmed: dialog box visually rendered, indistinguishable from a natural in-game MES.

Recipe is unambiguously proven. No tweaks needed for Phase 1. The bytes Aaron set in the phantom `script_context` (SP=2, msg_id at +0x8, slot at +0x4) are sufficient for `opcode_mes`.

### Useful incidental finding

The diagnostic logged: `WARNING: dispatch table opcode_mes=0x649E57F0 differs from cached=0x00528F20; using table.` That high address is FFNx's hook wrapper. The defensive "resolve from dispatch table at fire time, fall back to cached" pattern in `dialog_inject.cpp` routed correctly through FFNx -> our MinHook trampoline -> original. **Keep this pattern for `opcode_ask` in Phase 2** -- a hardcoded cached address would bypass FFNx and our own dialog hook.

---

## Next: v0.15.5 = Phase 2 (chase ASK via `opcode_ask`)

The infrastructure is identical to Phase 1; only the opcode and arg layout change.

### Disassembly cribbed from the research doc

```
opcode_ask (0x4A) at 0x00529520:
  movsx eax, byte [esi+0x184]       ; eax = SP
  mov ecx, [esi+eax*4-8]             ; arg
  mov edx, [esi+eax*4-0xC]
  mov ebx, [esi+eax*4]
  mov ebp, [esi+eax*4-4]
  mov edi, [esi+eax*4-0x14]          ; slot index
  mov ecx, [esi+eax*4-0x10]
  ...
  call 0x4A04E0                      ; set_window_object_ASK(slot, text, firstQ, lastQ, curQ_min, curQ_max, ...)
```

So `opcode_ask` pops 6 args off the stack. Layout (with SP=6):
- `stack[6]` = ctx[6*4] = ctx[0x18] -- arg pulled by ebx (probably text/msg_id)
- `stack[5]` = ctx[5*4] = ctx[0x14] -- arg pulled by ebp
- `stack[4]` = ctx[4*4] = ctx[0x10] -- arg pulled by ecx (second one)
- `stack[3]` = ctx[3*4] = ctx[0x0C] -- arg pulled by edx
- `stack[2]` = ctx[2*4] = ctx[0x08] -- arg pulled by ecx (first one, at -8)
- `stack[1]` = ctx[1*4] = ctx[0x04] -- arg pulled by edi (slot index, at -0x14 from sp*4)

The exact arg-to-meaning mapping needs one more disassembly read to nail down (which is msg_id, which is firstQ, which is lastQ, which are the two curQ values). Pull `set_window_object_ASK` at `0x004A04E0`'s entry sequence and trace which stack offset feeds which arg.

### Wait-for-answer mechanism

`opcode_ask` returns 1 (wait) on first call; the engine spins each frame on this until the user picks. We don't need to spin -- the engine handles the wait by replaying the opcode each frame. Our `Phase2_OpenAsk` returns immediately after the first call.

After fire, each `Update` tick:
- Read `pWindowsArray[slot] + 0x2B` (curQ -- engine writes the cursor position as the user navigates).
- Watch for `[ctx+0x174]` / `[ctx+0x175]` bit going clear (the "ASK pending" tracking byte, set by ASK on entry, cleared by the engine when the user commits).
- When committed, read `[ctx+0x204]` for the answer index.
- Close cleanly: clear `gameObj+0xD2` bit and slot state, OR call `opcode_winclose` similarly.

### Text encoding question

The chase ASK options ("Manual", "Auto", "Original") aren't in any field's MSD, so we can't use `field_get_dialog_string`. Two paths:

- **A**: Bypass the opcode entirely and call `set_window_object_ASK` directly with a pre-composed FF8-encoded buffer. The deep research recommended this. `set_window_object_ASK`'s disassembly (already in the research doc) shows it doesn't decode anything -- it just stores the text pointer.
- **B**: Inject the chase strings into the field's message table at runtime (patch + restore around the open).

A is cleaner. Use it.

### Wiring into chase_ask_overlay

`chase_ask_overlay::OpenAsk` currently does TTS-only. Replace its body with `DialogInject::Phase2_OpenAsk(prompt, options, default_idx)`. Existing trigger logic (Squall's "Forget it!  Let's go!" + chase field detection + 3-second deferral) stays intact. The TTS+keyboard fallback can be retained as belt-and-suspenders or removed once Phase 2 is BAT'd.

### Phase 2 deliverables

- `DialogInject::Phase2_OpenAsk(const char* prompt, const char* options[], int n_options, int default_idx) -> int` -- async; returns slot used, or -1 on fail.
- `DialogInject::PollAskAnswer(int slot) -> int` -- returns -1 if still pending, otherwise the chosen index. Called each tick by chase_ask_overlay's `Update`.
- `DialogInject::CloseAsk(int slot)` -- programmatic close.

Wire into `chase_ask_overlay::OpenAsk` as the primary path. Auto option still falls back to manual (v0.15.6 ships actual auto-run). Add Original option (v0.15.7 ships the chase-mod-active flag).

### Decision pending: push v0.15.4 first?

Two reasonable orders:
1. **Push v0.15.4 now**, then ship v0.15.5 separately. Benefits: Phase 1 is a clean milestone, easy to bisect against if Phase 2 introduces a regression.
2. **Bundle Phase 2 into v0.15.5 and push together.** Benefits: one commit, less GitHub churn. Cost: harder to bisect.

Aaron's call. v0.15.4 is shippable as-is -- a working diagnostic tool that proves the recipe.

---

### Backlog (after v0.15.4 / v0.15.5 chase ASK ships)

- v0.15.6: "Auto" option = run-from-robot logic. Field auto-drive plus flee-from-kani-symbol heading bias.
- v0.15.7: "Original" option = single chase-mod-active flag gating ChaseDetector freeze, ChaseBattleFreeze pin, chase_kani_freeze.
- Standalone (any version): X-ATM092 battle-name fix ("XATM 6" -> "XATM092") via hex-dump diagnostic.
- Standalone (any version): Generalized countdown-timer hook (SETTIMER/DISPTIMER/GETTIMER/KILLTIMER opcodes) with T / Shift+T context-sensitive accessibility. Aaron noted the Dollet timer "28:40" was visible in the F11 screenshot during this BAT -- confirms the timer is on screen as expected, useful for future hook-target acquisition.
- Fix `chase_diag::OnAskOpcodeFired` snprintf size-tracking bug.
- Remove party members from entity catalog (existing v0.14.108 filter incomplete in some fields).
- SeeD rank bug #27, walk-and-talk dialog gap.
- X-ATM092 chase audio descriptions DURING the chase (separate from the Lapin Beach FMV ADs).
- Refined-coord narrow-gate steering (Balamb Town entrance, etc.).
- Fire Cavern entry (#28) + planner-fallback (#29).
- Cosmetic: rename `chase_kani_freeze` module to `chase_agent_pin`.
- v0.15.3.1 candidate: log CHASE-AGENT FINAL SUMMARY inside `DeactivateFreeze` BEFORE `ClearChaseAgent`.
- Once chase is stable in production, archive obsolete diagnostic infrastructure.
