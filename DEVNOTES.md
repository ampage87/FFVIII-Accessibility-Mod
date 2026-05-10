**Purpose & context**

Aaron is the sole developer of the FF8 Accessibility Mod -- a `dinput8.dll` injection for Final Fantasy VIII Steam 2013 (App ID 39150, FF8_EN.exe + FFNx v1.23.x) that makes the game playable for blind players via Windows SAPI text-to-speech, navigation assistance, entity catalogs. Aaron is blind himself and is the primary tester, using NVDA as his screen reader.

**Project root:** `C:/Users/ampag/OneDrive/Documents/FFVIII-Accessibility-Mod/FF8_OriginalPC_mod/`

GitHub: `ampage87/FFVIII-Accessibility-Mod`. **HEAD = v0.15.6.2** (commit `249d9e47`). **Local tree: v0.15.7.1 BAT-PASSED, ready to push.** Push covers v0.15.7 (answer detection ships) and v0.15.7.1 (premature commit fix) together since v0.15.7 was never pushed.

---

## v0.15.7.1 BAT result: SUCCESS

Phase 2B Test #1 in `doani1_2`, 10:12:28 -> 10:12:42:

```
10:12:28  opcode_ask returned 1
10:12:28  POST ASK ... slot[+0x2B]curQ=1 slot[+0x2C]aux=0 text1=0x64EAF020 (override=0x64EAF020)
10:12:28  v0.15.7 answer-detection armed for slot 2 (timeout 60000 ms)
10:12:28  v0.15.7 cursor-change slot=2 curQ 255->1 announce="Manual selected"
10:12:29  v0.15.7.1 active-state observed slot=2 t+422ms (state=0xD D2=0x04); commit gating now armed
          ... 14 seconds of cursor changes ...
10:12:33  v0.15.7 cursor-change slot=2 curQ 1->2 announce="Auto selected"
10:12:34  v0.15.7 cursor-change slot=2 curQ 2->3 announce="Original selected"
10:12:36  v0.15.7 cursor-change slot=2 curQ 3->1 announce="Manual selected"
10:12:38  v0.15.7 cursor-change slot=2 curQ 1->2 announce="Auto selected"
10:12:39  v0.15.7 cursor-change slot=2 curQ 2->3 announce="Original selected"
10:12:40  v0.15.7 cursor-change slot=2 curQ 3->1 announce="Manual selected"
10:12:41  v0.15.7 cursor-change slot=2 curQ 1->2 announce="Auto selected"
10:12:41  v0.15.7 cursor-change slot=2 curQ 2->3 announce="Original selected"
10:12:42  v0.15.7 commit reason=state left 0xD capturing answer=3
10:12:42  v0.15.7 announce="You chose Original"
```

End-to-end: dialog opens, gating arms after 422ms (matches the predicted ~450ms exactly), eight cursor changes announced cleanly, commit fires only on X press, final answer captured as 3 = Original.

`GetLastAnswer()` is now ready to drive v0.15.8's chase wiring.

### Architectural lessons captured

- **Slot state field at `+0x24` walks `0 -> 1 -> 0xD` over ~450 ms** after `opcode_ask` returns. Don't trust state checks until cursor-active state has been observed at least once.
- **`gameObj.D2` bit for the slot is set immediately** after `opcode_ask` returns (PRE D2=0x00 -> POST D2=0x04). Same belt-and-braces gating still applies for symmetry.
- **Cursor field is `slot+0x2B`** (current_choice_question), aux is at `slot+0x2C`. v0.15.5.1 had these crossed; v0.15.7 corrected.
- **Engine commit signal is "state leaves 0xD"** in this BAT (not "D2 bit clear"). Both are valid, but state is the faster/cleaner signal.

---

## Push plan

Both v0.15.7 and v0.15.7.1 will appear as separate commits. CHANGELOG.md has both entries (v0.15.7.1 on top, v0.15.7 below it). `Utilities/push_to_github.vbs` will push the top entry (v0.15.7.1) -- since v0.15.7 was never pushed, its CHANGELOG body will be included in v0.15.7.1's commit body's chronological history block.

Wait -- the push utility validates `## v0.15.7.1` matches `FF8OPC_VERSION "0.15.7.1"`, both confirmed. It uses everything between `## v0.15.7.1` and the next `## v` heading as the commit message body. So v0.15.7's body will NOT be in the v0.15.7.1 commit -- it'll just be in the local CHANGELOG.md file. That's fine; the v0.15.7.1 entry's "What v0.15.7 ships" section explains what was added.

If Aaron wants v0.15.7 to appear as its own commit on GitHub: would need to manually push v0.15.7 first (revert ff8_accessibility.h locally, push, restore). Not worth it -- v0.15.7.1's commit body documents the v0.15.7 design via the BAT-diagnosis section.

---

## Next: v0.15.8 -- chase_ask_overlay wiring

Wire Phase 2B + answer detection into `chase_ask_overlay::OpenAsk` as the primary chase ASK path. Replaces v0.15.3's TTS-only overlay. Inherits chase_ask_overlay's existing input gating, which solves v0.15.7.1's deferred Squall-walking limitation.

### v0.15.8 design

**New API in dialog_inject:**

```cpp
// Generalizes Phase2_TestAsk with caller-supplied prompt and choices.
// Encodes the prompt + choices into the override buffer using EncodeFf8,
// fires opcode_ask with the right firstQ/lastQ/curQ for the choice count,
// arms answer detection, returns true if successfully opened.
bool OpenAsk(const char* prompt,
             const char** choices,
             int numChoices,
             int defaultCursor,    // 1-based, in [1, numChoices]
             int slot);            // typically 2 to leave 0/1 for engine
```

**chase_ask_overlay rewire:**

1. Replace v0.15.3's TTS-only overlay open path with `DialogInject::OpenAsk("Mode?", {"Manual", "Auto", "Original"}, 3, 1, 2)`.
2. After OpenAsk returns true, poll `DialogInject::GetLastAnswer()` per frame (could piggyback on existing chase_ask_overlay update).
3. When `GetLastAnswer() != -1`, treat as the chosen mode and dispatch (Manual = no auto-drive, Auto = run-from-robot, Original = chase-mod-active flag).
4. chase_ask_overlay's existing input-gating flag (which suppresses field input during the overlay's lifetime) covers our injected ASK as a side effect -- Squall stops walking.
5. Preserve the existing trigger logic (matches Squall's "Forget it!  Let's go!" via show_dialog hook).

### Risk

Medium. The biggest unknown is the input-gating coordination -- chase_ask_overlay sets some flag/state that suppresses input during its overlay; we need to verify that flag stays set while DialogInject's ASK is open and clears only after the user commits. May require adding a short hold-down period after `GetLastAnswer()` returns non--1, to avoid the gate releasing before the engine has fully closed the slot.

Minor unknown: chase_ask_overlay was designed assuming TTS-only (no actual engine dialog). It might have logic like "after announcing the ASK, always proceed in N seconds" that needs deletion since we now have a real engine ASK with its own timing.

### v0.15.8 BAT plan

1. Trigger the chase scene in `enter1` (or wherever Squall says "Forget it!  Let's go!").
2. Hear the chase ASK open: "Mode?. Selected: Manual. Auto. Original" + "Manual selected".
3. Verify Squall does NOT walk during the ASK.
4. Make a selection with arrows + X.
5. Verify the chosen mode dispatches correctly (Manual = no auto-drive proceeds, Auto = ?, Original = ?).

(Note: v0.15.9/.10 will implement Auto/Original dispatch logic. v0.15.8 just wires the ASK; selecting Auto or Original should announce "You chose X" but v0.15.8 itself won't have those modes implemented yet.)

---

## Backlog

### After v0.15.8 SUCCESS

- **v0.15.9**: "Auto" option = run-from-robot logic.
- **v0.15.10**: "Original" option = chase-mod-active flag gating.

### Standalone (any version)

- **v0.15.7.2** (optional, low priority now): suppress field input during standalone Phase2_TestAsk via dinput8 proxy. Once v0.15.8 ships, the standalone test isn't user-facing -- skip.
- X-ATM092 battle-name fix.
- Generalized countdown-timer hook.
- Cleanup: remove the dead `Hook_field_get_dialog_string` override branch in field_dialog.cpp now that v0.15.6.2 SUCCESS confirms post-ASK patching is the correct path.
- Cosmetic: deploy.bat "Version: SINGLE-PRONGED" regex regression.

### Deferred priorities (unchanged across recent sessions)

- chase_diag::OnAskOpcodeFired snprintf size-tracking bug.
- Remove party members from entity catalog.
- SeeD rank bug #27, walk-and-talk dialog gap.
- X-ATM092 chase audio descriptions DURING the chase.
- Refined-coord narrow-gate steering.
- Fire Cavern entry (#28) + planner-fallback (#29).
- Cosmetic: rename `chase_kani_freeze` -> `chase_agent_pin`.
- v0.15.3.1 candidate: log CHASE-AGENT FINAL SUMMARY inside `DeactivateFreeze` BEFORE `ClearChaseAgent`.

---
