**Purpose & context**

Aaron is the sole developer of the FF8 Accessibility Mod -- a `dinput8.dll` injection for Final Fantasy VIII Steam 2013 (App ID 39150, FF8_EN.exe + FFNx v1.23.x) that makes the game playable for blind players via Windows SAPI text-to-speech, navigation assistance, entity catalogs. Aaron is blind himself and is the primary tester, using NVDA as his screen reader.

**Project root:** `C:/Users/ampag/OneDrive/Documents/FFVIII-Accessibility-Mod/FF8_OriginalPC_mod/`

GitHub: `ampage87/FFVIII-Accessibility-Mod`. Last pushed: **v0.15.4** (commit `41251c39`, 2026-05-09 23:47 UTC, parent `bc4d5358` v0.15.3). Local tree: **v0.15.5.3 BUILT, ready to BAT, NOT pushed.** v0.15.5 BAT'd PARTIAL, v0.15.5.1 BAT'd render-success-but-no-cursor-input, v0.15.5.2 BAT'd cursor-input-success-but-no-dialog-text-heard. None of v0.15.5/.5.1/.5.2/.5.3 pushed yet.

---

## Current build: v0.15.5.3 -- ready to BAT

Two-character SAPI fix on top of v0.15.5.2. v0.15.5.2 BAT confirmed cursor input fully wired (arrow SFX firing) but Aaron only heard the diagnostic announcement, not the dialog text. Root cause: `AnnouncePhase2Result` (and Phase 1's equivalent) called `Speak(msg, true)` where `true` = interrupt in-flight speech. The FieldDialog `[ASK]` hook had started speaking the dialog text inside `opcode_ask`, and our subsequent post-call diagnostic announcement preempted it. v0.15.5.3 changes both `AnnouncePhase1Result` and `AnnouncePhase2Result` to use `interrupt=false` so the diagnostic queues after the dialog text rather than cutting it off.

### v0.15.5.2 BAT recap (cursor input WORKED)

- `sub_49FD50(2): pCurrentDialogSlot 0xFF -> 0x02` confirmed in log.
- `opcode_ask returned 1`, slot 2 trans `0 -> 0x1000`, state `0xD`.
- `[ASK] win[2] Speaking: "Selphie. 'Wanna go up?'. Selected: Go up. Stay"` (Test #1) and `"Battle. Battle"` (Test #2 in different field).
- Aaron heard cursor-move SFX when pressing arrows during dialog.
- Aaron did NOT hear dialog text (preempted by diagnostic announcement).
- Aaron's character walked simultaneously with cursor moving.

### v0.15.5.3 fix

Two character changes in `src/dialog_inject.cpp`:

```cpp
-       ScreenReader::Speak(msg, true);   // interrupt
+       ScreenReader::Speak(msg, false);  // queue
```

In both `AnnouncePhase1Result` and `AnnouncePhase2Result`. Plus ~15 lines of new comment explaining the rationale.

Error-path Speak calls ("Dialog inject opcode address missing", "Dialog inject crashed") deliberately keep `interrupt=true` -- those are error scenarios where preemption is correct.

### v0.15.5.3 BAT plan

1. Deploy v0.15.5.3 (`deploy.vbs`).
2. **Quit FF8 and re-launch** -- clean restart for fresh slot 2.
3. Load `doani1_2` save again.
4. Press **F12 once** -- expect to hear FIRST `"Selphie 'Wanna go up?' Go up Stay"` (the elevator dialog), THEN queued `"Dialog inject phase one. Slot 1. Return code 3."`
5. Press **Shift+F12 once** -- expect to hear FIRST `"Selphie. 'Wanna go up?'. Selected: Go up. Stay"` (the ASK with cursor on Go up), THEN queued `"Dialog inject phase two A. Slot 2. Return code 1."`
6. Press arrows during the open ASK -- cursor SFX should still play (v0.15.5.2 fix preserved).
7. Wait 3 seconds for slot poll completion.
8. Quit, send `Logs/ff8_dialog.log`.

### v0.15.5.3 BAT outcomes

- **SUCCESS**: dialog text spoken before diagnostic announcement, BOTH phases (F12 and Shift+F12), cursor SFX still works on arrows. Phase 2a recipe FULLY proven for Phase 2b. Move to v0.15.6 Phase 2b.
- **PARTIAL**: dialog text gets spoken but is itself getting preempted by something else (e.g., another mod component). Investigate the SAPI event chain.
- **REGRESSION**: an unrelated regression in Phase 1 or 2a. Inspect log for differences from v0.15.5.2.

### Known-but-deferred (Phase 2b will resolve)

- Arrows moving Squall AND cursor simultaneously. Field input isn't suppressed because we don't run the script-VM polling loop that normally blocks field movement during ASK. `chase_ask_overlay` already handles input gating; v0.15.6 wiring will inherit that.

---

## Backlog (after v0.15.5.3 BAT)

If SUCCESS:
- v0.15.6: Phase 2b -- FF8 text encoding for custom strings + answer-detection polling + wiring into `chase_ask_overlay`.
  - Text encoder: trivial mapping (A-Z 0x45-0x5E, a-z 0x5F-0x78, digits 0-9 0x21-0x2A, space 0x20, ? 0x2F, line break 0x02, end 0x00).
  - Custom buffer composition: `Mode? \x02 Manual \x02 Auto \x02 Original \x00` with `firstQ=1 lastQ=3 curQ=1` (Manual default).
  - Path A: bypass `opcode_ask`, call `set_window_object_ASK` directly. Avoid the `ctx[+0x174]/[+0x175]` and answer-correlation machinery entirely. Just need: `set_window_object_ASK(slot, buf, firstQ, lastQ, curQ, aux)` + `sub_49FD50(slot)` + manually set `gameObj.D2/D3` bits + `sub_4A0620(slot)` for open transition.
  - Answer-detection: poll `slot+0x2B` (curQ) per frame; speak "Manual selected" / "Auto selected" / "Original selected" on changes; detect commit when `gameObj.D2` bit clears OR when state field transitions out of 0xD.
  - Chase wiring: replace `chase_ask_overlay::OpenAsk` body. Trigger logic stays.
- v0.15.7: "Auto" option = run-from-robot logic.
- v0.15.8: "Original" option = chase-mod-active flag gating.

If PARTIAL: iterate v0.15.5.3.

Standalone (any version):
- X-ATM092 battle-name fix.
- Generalized countdown-timer hook.

### Deferred priorities (unchanged)

- chase_diag::OnAskOpcodeFired snprintf size-tracking bug.
- Remove party members from entity catalog.
- SeeD rank bug #27, walk-and-talk dialog gap.
- X-ATM092 chase audio descriptions DURING the chase.
- Refined-coord narrow-gate steering.
- Fire Cavern entry (#28) + planner-fallback (#29).
- Cosmetic: rename `chase_kani_freeze` -> `chase_agent_pin`.
- v0.15.3.1 candidate: log CHASE-AGENT FINAL SUMMARY inside `DeactivateFreeze` BEFORE `ClearChaseAgent`.
