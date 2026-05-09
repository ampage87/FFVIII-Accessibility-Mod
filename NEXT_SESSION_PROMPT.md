# Next Session Prompt -- v0.15.4 BAT'd, Phase 2 (chase ASK) is the next ship

**Status:** v0.15.4 BAT'd successfully on 2026-05-09 17:35-17:36. Phase 1 of engine-rendered dialog injection works exactly as designed. NOT pushed yet -- Aaron to decide whether to push v0.15.4 first or bundle with v0.15.5 (Phase 2).

---

## What v0.15.4 BAT proved

Aaron pressed F12 once in field `doani1_2` (Dollet Comm Tower top, fieldId 0x136). Every metric green:

- `opcode_mes(&phantom_ctx)` returned 3.
- Existing dialog hook fired and SAPI spoke: `Selphie "Wanna go up?" Go up Stay`.
- `pWindowsArray[1] + 0x1C` advanced 0 -> 0x400 (+15ms) -> 0x1000 (+125ms) and held.
- `+0x1E` velocity 0x200 armed by engine on entry.
- State machine 0 -> 1 -> 7.
- `gameObj.D3 = 0x02`, `D4 = 0x02` (bit 1 set for slot 1 -- as the opcode does for any natural MES).
- `show_dialog` callback fired for slot 1 -- the per-slot callback registration v0.15.x worried about happens automatically through the opcode path.
- F11 screenshot at 17:36:00 confirmed: dialog visually rendered, looks like any natural FF8 MES.

Phantom context layout that worked:
- 0x300-byte zero-init buffer
- `ctx[0x184] = 2` (SP)
- `ctx[0x08] = 0` (msg_id, since SP=2 means top of stack is at +SP*4 = +8)
- `ctx[0x04] = 1` (slot index, at +(SP-1)*4 = +4)

That's the entire Phase 1 recipe, and it's enough for `opcode_mes`.

---

## v0.15.5 = Phase 2: chase ASK via `opcode_ask`

The plan and disassembly are written up in DEVNOTES.md. Summary:

- New API in `dialog_inject.cpp`:
  - `Phase2_OpenAsk(prompt_text, options[], n_options, default_idx) -> slot`
  - `PollAskAnswer(slot) -> answer_idx | -1`
  - `CloseAsk(slot)`
- `opcode_ask` (dispatch index 0x4A) at `0x00529520`, pulls 6 args off the script stack. SP=6 in the phantom context.
- Returns 1 (wait) on first call; engine spins each frame. We DON'T spin -- engine replays automatically.
- Each tick poll `pWindowsArray[slot] + 0x2B` (curQ) for navigation feedback.
- Detect commit via `[ctx+0x174/0x175]` ASK-pending bit clear; read answer at `[ctx+0x204]`.
- Use `set_window_object_ASK` directly (call its address, not via opcode) for custom prompt/option strings -- bypasses `field_get_dialog_string` and lets us pass FF8-encoded buffers we compose ourselves. Cleaner than patching the field message table.

Wire into `chase_ask_overlay::OpenAsk` as the primary path; existing trigger logic stays intact.

Three options in the chase ASK:
- "Manual" (default cursor)
- "Auto" (still falls back to manual until v0.15.6)
- "Original" (still no-op until v0.15.7)

### Outstanding research before coding Phase 2

Two questions to nail down by reading more of the disassembly:

1. **opcode_ask arg-to-meaning mapping.** The opcode pops 6 args; we need to know which is msg_id, which is firstQ/lastQ, which are the two curQ values, which is slot. The research doc has the assembly but doesn't fully decode the order. Read `set_window_object_ASK` at `0x004A04E0` to see which args feed into which output offsets, then trace back through `opcode_ask` to map the script-stack positions.

2. **FF8 text encoding for custom strings.** "Manual", "Auto", "Original" need to be in the FF8 character encoding (high-bit-set extension chars for special tokens, etc.). The mod already has `ff8_text_decode.cpp` for the reverse direction (FF8 -> ASCII). Check whether there's an encode helper already; if not, write one (or hand-craft the encoded bytes for those three short strings, since they're all printable ASCII and in FF8's encoding probably just map to 0x20-0x7F directly).

Once those are answered, Phase 2 is a copy of Phase 1 with different opcode and arg layout.

---

## Workflow reminders (unchanged)

- Filesystem MCP for ALL Windows project files. Bash cannot reach Windows source.
- Every response begins with `## Claude Says`.
- CHANGELOG.md ASCII-only in commit body. Heading must match `FF8OPC_VERSION` exactly. Push utility refuses if mismatched.
- Aaron pushes via `Utilities/push_to_github.vbs` -- Claude never pushes.
- Build via `deploy.vbs` from project root -> reads `src/deploy.ps1` -> `src/deploy.bat`.
- Version is bumped in ONE place: `FF8OPC_VERSION` in `src/ff8_accessibility.h`.
- Read DEVNOTES.md and this file at start of every session.
- F12 = `DialogInject::Phase1_TestMes` (replaces v0.15.0's `ChaseDiag::Toggle`). Per the F12 rule, only one diagnostic active on F12 at a time.
- For Phase 2: keep the "resolve from dispatch table at fire time, fall back to cached" pattern. The v0.15.4 BAT log showed the table value (FFNx wrapper) differed from cached; routing through the table preserves the FFNx hook chain and our own dialog hook.

---

## State of the codebase

- `src/dialog_inject.h` -- v0.15.4 (~80 lines)
- `src/dialog_inject.cpp` -- v0.15.4 (~280 lines), Phase 1 only
- `src/dinput8.cpp` -- v0.15.4 (DialogInject wired, F12 swap)
- `src/deploy.bat` -- v0.15.4 (dialog_inject.cpp in compile list)
- `src/ff8_accessibility.h` -- `FF8OPC_VERSION "0.15.4"` with v0.15.4 + v0.15.3 trail
- `src/chase_kani_freeze.cpp` -- v0.15.3 design unchanged
- `src/chase_ask_overlay.cpp` -- v0.15.2.2 design (TTS+keyboard only) unchanged; v0.15.5 will swap its body to use the Phase 2 API
- `CHANGELOG.md` -- top entry `## v0.15.4`
- `DEVNOTES.md` -- post-BAT state, Phase 2 plan
- `NEXT_SESSION_PROMPT.md` -- this file

---

## Quick-start for next session

1. Read this file + DEVNOTES.md.
2. Confirm with Aaron whether v0.15.4 was pushed standalone or v0.15.5 is being bundled.
3. Read `Plan & Research Documents/Field dialog system disassembly analysis.md` for the `opcode_ask` and `set_window_object_ASK` disassembly.
4. Pull the FF8_EN.exe asm at `0x00529520` (opcode_ask) and `0x004A04E0` (set_window_object_ASK) to confirm the arg-to-meaning mapping.
5. Decide the text-encoding path: pre-encode the three option strings as FF8 byte sequences (likely just ASCII passthrough since they're all `[A-Za-z]`), or write a small encoder.
6. Implement `Phase2_OpenAsk`, `PollAskAnswer`, `CloseAsk` in `dialog_inject.cpp`.
7. Wire into `chase_ask_overlay::OpenAsk`.
8. Bump version, update CHANGELOG, DEVNOTES, NEXT_SESSION_PROMPT.
9. Hand to Aaron for BAT.
