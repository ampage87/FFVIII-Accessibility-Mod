# Next Session Prompt -- v0.15.5.3 ready to BAT

**Status:** v0.15.5.3 BUILT, ready to BAT. Two-character SAPI fix on top of v0.15.5.2. NOT pushed.

If you're reading this in a fresh session:
1. Read this file + DEVNOTES.md.
2. Tail `Logs/ff8_dialog.log` for `[DLG-INJ]` lines from the latest BAT.
3. If no Phase 2a lines, check `Logs/build_latest.log` for build errors.

---

## What v0.15.5.3 ships

Two character changes in `src/dialog_inject.cpp`:

```cpp
// In AnnouncePhase1Result and AnnouncePhase2Result:
-       ScreenReader::Speak(msg, true);   // was: interrupt
+       ScreenReader::Speak(msg, false);  // now: queue
```

Plus ~15 lines of new comment explaining the rationale. That's the entire change.

## Why

v0.15.5.2 BAT confirmed Phase 2a's cursor input is fully wired (`sub_49FD50(2): pCurrentDialogSlot 0xFF -> 0x02` in log; cursor-move SFX plays on arrow press during dialog). But Aaron only heard the diagnostic announcement, not the dialog text.

The FieldDialog `[ASK]` hook fires DURING `opcode_ask` and starts speaking the dialog text via SAPI (e.g., `"Selphie. 'Wanna go up?'. Selected: Go up. Stay"`). Then `opcode_ask` returns and our `AnnouncePhase2Result` calls `ScreenReader::Speak(msg, true)` -- the `true` interrupts in-flight speech. SAPI was halfway through speaking the dialog text when our diagnostic announcement preempted it.

With `interrupt=false`, the diagnostic announcement queues after the dialog text instead of cutting it off.

Phase 1's `AnnouncePhase1Result` gets the same fix proactively. The v0.15.4 BAT had the same race but Aaron didn't notice -- likely because Phase 1's MES dialog text and the diagnostic announcement are textually similar enough that the cut-off wasn't obvious.

## v0.15.5.3 BAT plan

1. Deploy via `deploy.vbs`.
2. **Quit FF8 and re-launch** -- clean restart for fresh slot state.
3. Load `doani1_2` save.
4. Press **F12 once**:
   - Hear FIRST: `"Selphie 'Wanna go up?' Go up Stay"` (elevator dialog from FieldDialog hook).
   - Hear THEN: `"Dialog inject phase one. Slot 1. Return code 3."` (queued diagnostic).
5. Wait for the announcements to finish. Wait 3 seconds for slot poll completion.
6. Press **Shift+F12 once**:
   - Hear FIRST: `"Selphie. 'Wanna go up?'. Selected: Go up. Stay"` (ASK with cursor on "Go up").
   - Hear THEN: `"Dialog inject phase two A. Slot 2. Return code 1."` (queued diagnostic).
7. Press arrows during the open ASK -- cursor SFX should still play (v0.15.5.2 fix preserved).
8. Wait 3 seconds for slot poll completion.
9. Quit, send `Logs/ff8_dialog.log`.

## BAT outcomes

### SUCCESS

Both phases speak the dialog text BEFORE the diagnostic announcement. Cursor SFX works on arrow press during open ASK. Phase 2a recipe FULLY proven. Move to **v0.15.6 Phase 2b**: FF8 text encoding for custom strings + answer detection + chase wiring.

### PARTIAL

Dialog text gets spoken but something else preempts it (another mod component? The FieldDialog hook itself using interrupt=true on its Speak call?). Investigate the SAPI event chain. Possibly need to delay `AnnouncePhase2Result` by a fixed time after `opcode_ask` returns.

### REGRESSION

Unrelated regression in Phase 1 or Phase 2a. Inspect log for differences from v0.15.5.2.

## Known-but-deferred (Phase 2b will resolve)

Aaron reported in the v0.15.5.2 BAT that arrows moved Squall AND cursor simultaneously. This is a known limitation: the standalone Phase 2a diagnostic doesn't suspend field input because we don't run the script-VM polling loop that normally blocks field movement during ASK. **Phase 2b chase wiring (v0.15.6) inherits `chase_ask_overlay`'s input gating** which already handles this for the existing TTS-only chase ASK. Not addressed in v0.15.5.3.

---

## v0.15.6 Phase 2b sketch (if v0.15.5.3 SUCCESS)

Goals:
1. Custom prompt + 3 options ("Manual / Auto / Original") via FF8 text encoding.
2. Answer detection (poll curQ; detect commit; report selected option via SAPI).
3. Wire into `chase_ask_overlay::OpenAsk` as primary path.

**Path A (recommended)**: bypass `opcode_ask` entirely. Direct calls:
```cpp
// 1. Compose FF8-encoded buffer:
//    "Mode?" \x02 "Manual" \x02 "Auto" \x02 "Original" \x00
// 2. set_window_object_ASK(slot, buf, firstQ=1, lastQ=3, curQ=defaultIdx, aux=0)
// 3. sub_49FD50(slot)            -- enable arrow input
// 4. sub_4A0620(slot)            -- open transition (need to find/resolve address)
// 5. gameObj.D2 |= (1<<slot)     -- mark slot as ASK-active
// 6. gameObj.D3 |= (1<<slot)     -- mark window-active
```

Per-frame Update() polling:
- Read `slot+0x2B` (curQ); if changed, speak "Manual selected" / "Auto selected" / "Original selected".
- Detect commit: watch for `gameObj.D2 & (1<<slot) == 0` (engine cleared on Enter) OR `state` field transition out of 0xD.
- On commit, read final curQ as the answer; report to chase_ask_overlay caller.

This avoids `opcode_ask`'s state machine entirely. Simpler, more controllable. Field-input gating handled by `chase_ask_overlay`.

FF8 text encoding (already in `ff8_text_decode.cpp`, just need to invert):
- A-Z: 0x45-0x5E. a-z: 0x5F-0x78. 0-9: 0x21-0x2A. Space: 0x20. ?: 0x2F. Newline: 0x02. End: 0x00.

Need to resolve `sub_4A0620` address (open-transition function). Hardcode similarly to `sub_49FD50`.

---

## Workflow reminders (unchanged)

- Filesystem MCP for ALL Windows project files. Bash cannot reach Windows source.
- Every response begins with `## Claude Says`.
- CHANGELOG.md ASCII-only in commit body. Heading must match `FF8OPC_VERSION` exactly. Push utility refuses if mismatched.
- Aaron pushes via `Utilities/push_to_github.vbs` -- Claude never pushes.
- Build via `deploy.vbs` from project root.
- Version is bumped in ONE place: `FF8OPC_VERSION` in `src/ff8_accessibility.h`.
- Read DEVNOTES.md and this file at start of every session.
- F12 alone = `Phase1_TestMes`; Shift+F12 = `Phase2_TestAsk`.

---

## State of the codebase

**v0.15.5.3 BUILT, NOT pushed. v0.15.4 (commit `41251c39`) remains HEAD on GitHub. v0.15.5/.5.1/.5.2/.5.3 all BAT'd locally but never pushed.**

- `src/dialog_inject.h` -- v0.15.5 (unchanged since)
- `src/dialog_inject.cpp` -- v0.15.5.3 (2 char changes + comments; v0.15.5.1 ctx-bytes fix and v0.15.5.2 sub_49FD50 call preserved)
- `src/dinput8.cpp` -- v0.15.5 (unchanged since)
- `src/deploy.bat` -- unchanged from v0.15.4
- `src/ff8_accessibility.h` -- `FF8OPC_VERSION "0.15.5.3"` with full comment trail
- `CHANGELOG.md` -- top entry `## v0.15.5.3`
- `DEVNOTES.md` -- post-v0.15.5.3-build state, BAT plan, Phase 2b sketch
- `NEXT_SESSION_PROMPT.md` -- this file

---

## Quick-start for next session (after v0.15.5.3 BAT)

1. Read this file + DEVNOTES.md.
2. Tail `Logs/ff8_dialog.log` for `[DLG-INJ]` lines.
3. Look for `[ASK]` / `[MES]` Speaking lines AROUND the BAT timestamps -- those should now play before the queued diagnostic announcement.
4. If Aaron reports hearing dialog text first then the diagnostic announcement -> SUCCESS, ship v0.15.6 Phase 2b.
5. If still preempted -> PARTIAL. Investigate SAPI scheduling.

## Dispute notes

If at any point Aaron disputes v0.15.5.x's success, point him to the `Logs/ff8_dialog.log` lines:
- `FieldDialog: [ASK] win[2] Speaking: ...` -- proves the engine populated slot 2 and our hook saw the text.
- `sub_49FD50(2): pCurrentDialogSlot 0xFF -> 0x02` -- proves the cursor-input fix landed.
- `opcode_ask returned 1`, slot 2 trans `0 -> 0x1000`, state `0xD` -- proves the state machine progressed.

The log evidence is solid. The remaining concerns are all UX (SAPI scheduling, field-input gating) -- mechanical fixes, not design problems.
