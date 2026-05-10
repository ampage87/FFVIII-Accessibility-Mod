# Next Session Prompt -- v0.15.7.1 BAT-PASSED, ready to push

**Status:** v0.15.7.1 BAT-PASSED. End-to-end answer detection works: gating triggered at t+422ms (predicted ~450ms), eight cursor announces fired cleanly across 14 seconds of arrow presses, commit fired only on X press, final answer captured as 3 = Original. Ready to push via `Utilities/push_to_github.vbs`. GitHub HEAD currently = v0.15.6.2 (commit `249d9e47`).

If you're reading this in a fresh session:
1. Read this file + DEVNOTES.md.
2. If GitHub HEAD is now `v0.15.7.1`: Aaron pushed -- proceed to v0.15.8 (chase_ask_overlay wiring).
3. If GitHub HEAD is still `v0.15.6.2`: Aaron hasn't pushed yet -- ask what's next.

---

## What v0.15.7.1 closed

The full Phase 2B answer-detection chain is now working:

| Step | Mechanism | Verified by |
|------|-----------|-------------|
| 1 | Inject `opcode_ask` with custom text | v0.15.6.2 BAT (Aaron heard "Mode?. Selected: Manual. Auto. Original") |
| 2 | Cursor input via `sub_49FD50(slot)` + arrow detection | v0.15.5.2 BAT (Aaron heard cursor SFX) |
| 3 | Per-frame poll of `slot+0x2B` for cursor changes -> SAPI announce | v0.15.7 BAT (cursor announces worked) |
| 4 | Gate commit on having observed cursor-active state | v0.15.7.1 BAT (no premature commit, fired only on X) |
| 5 | Capture final cursor as answer; expose via `GetLastAnswer()` | v0.15.7.1 BAT (`announce="You chose Original"`, answer=3) |

`GetLastAnswer()` is now ready to drive v0.15.8's chase wiring.

### v0.15.7.1 BAT log signature (Phase 2B Test #1, 10:12:28 -> 10:12:42)

```
opcode_ask returned 1
POST ASK ... slot[+0x2B]curQ=1 slot[+0x2C]aux=0 text1=0x64EAF020 (override=0x64EAF020)
v0.15.7 answer-detection armed for slot 2 (timeout 60000 ms)
v0.15.7 cursor-change slot=2 curQ 255->1 announce="Manual selected"
v0.15.7.1 active-state observed slot=2 t+422ms (state=0xD D2=0x04); commit gating now armed
                ... 14 seconds of cursor changes ...
v0.15.7 commit reason=state left 0xD capturing answer=3
v0.15.7 announce="You chose Original"
```

### Architectural lessons

- **Slot state walks `0 -> 1 -> 0xD` over ~450 ms** after `opcode_ask` returns. Gate state checks on having seen 0xD first.
- **`gameObj.D2` bit set immediately** after `opcode_ask` (PRE 0x00 -> POST 0x04). Same gating for symmetry.
- **`slot+0x2B` is curQ** (current_choice_question). v0.15.5.1 had it crossed with aux at 0x2C; v0.15.7 corrected.
- **Engine commit signal in v0.15.7.1 BAT was "state leaves 0xD"**. `D2 bit clear` is also valid; gating accepts either.

---

## Push plan

`Utilities/push_to_github.vbs` validates `## v0.15.7.1` matches `FF8OPC_VERSION "0.15.7.1"`. Both confirmed. Push utility takes the v0.15.7.1 entry as the commit body. v0.15.7's CHANGELOG entry stays in local CHANGELOG.md but won't appear as its own GitHub commit -- v0.15.7.1's BAT-diagnosis section documents the v0.15.7 design adequately. If Aaron specifically wants v0.15.7 as a separate commit, that's a manual two-step push (revert ff8_accessibility.h to "0.15.7", push, restore "0.15.7.1") -- not worth the complexity.

---

## v0.15.8 plan -- chase_ask_overlay wiring

Wire Phase 2B + answer detection into `chase_ask_overlay::OpenAsk` as the primary chase ASK path. Replaces v0.15.3's TTS-only overlay. Inherits chase_ask_overlay's existing input gating, which solves v0.15.7.1's deferred Squall-walking limitation.

### v0.15.8 design

**1. New public API in dialog_inject:**

```cpp
namespace DialogInject {
    // Generalizes Phase2_TestAsk with caller-supplied prompt and choices.
    // Encodes prompt + choices into the override buffer via EncodeFf8,
    // fires opcode_ask with firstQ=1, lastQ=numChoices, curQ=defaultCursor,
    // arms answer detection. Returns true if the call returned 1
    // (wait-for-answer success), false otherwise.
    //
    // After this returns true, callers should poll GetLastAnswer() per
    // frame; non--1 means the user committed and the result is ready.
    bool OpenAsk(const char* prompt,
                 const char* const* choices,
                 int numChoices,
                 int defaultCursor,    // 1-based, in [1, numChoices]
                 int slot);            // typically 2

    // Resets s_phase2LastAnswer to -1. Callers should call this before
    // OpenAsk if they want to differentiate "no commit yet" from "stale
    // answer from previous ASK".
    void ResetLastAnswer();
}
```

**2. dialog_inject.cpp refactor:**

Factor `Phase2_TestAsk()`'s body into `OpenAskInternal(prompt, choices, numChoices, defaultCursor, slot)` and call it from both `Phase2_TestAsk()` (hardcoded "Mode? / Manual / Auto / Original" with slot=2, defaultCursor=1) and the new public `OpenAsk()`. Existing test path stays for diagnostics (still on Shift+F12).

The override-buffer encoder needs to handle variable choice count: encode prompt + `\n` + each choice + `\n`, terminate with `\0`. The choice-count `numChoices` becomes `lastQ`; default cursor maps to `curQ`.

Also need an `OptionNameAt(int curQ)` indirection -- can't keep the hardcoded `CurQToOptionName` switch since OpenAsk callers supply arbitrary choice strings. Pass the choice list through to Update() via state, OR have OpenAsk callers register their own announcer callback. Simpler: store the choice strings in a small static array when OpenAsk fires, look up by index in Update().

**3. chase_ask_overlay rewire:**

Locate where v0.15.3's TTS-only overlay opens (somewhere near the existing `OnDialogText` trigger that matches Squall's "Forget it!  Let's go!"). Replace the open path with:

```cpp
const char* choices[] = { "Manual", "Auto", "Original" };
DialogInject::ResetLastAnswer();
if (DialogInject::OpenAsk("Mode?", choices, 3, 1, 2)) {
    s_chaseAskWaiting = true;
    // chase_ask_overlay's existing input-gating flag stays set; will be
    // cleared in OnAnswer() below.
}
```

Then in chase_ask_overlay's per-frame update:

```cpp
if (s_chaseAskWaiting) {
    int answer = DialogInject::GetLastAnswer();
    if (answer != -1) {
        s_chaseAskWaiting = false;
        OnAnswer(answer);  // 1=Manual, 2=Auto, 3=Original
    }
}
```

`OnAnswer(int)` dispatches: Manual -> normal play, Auto -> mark for v0.15.9 run-from-robot, Original -> mark for v0.15.10 chase-mod-active flag. v0.15.8 itself can announce "You chose <X>" plus "(not yet implemented)" for Auto/Original.

**4. Input gating coordination:**

chase_ask_overlay already gates input during its overlay's lifetime. Need to verify that flag stays set across the entire ASK -- from OpenAsk fire to OnAnswer dispatch. May require an explicit hold-down period if the flag clears too early.

### v0.15.8 BAT plan

1. Reach the chase trigger field (where Squall says "Forget it!  Let's go!"). Aaron will know the field name -- I think it's `enter1` based on past sessions.
2. Trigger the chase. Expect:
   - Squall's "Forget it!" line speaks normally.
   - Chase ASK opens automatically: "Mode?. Selected: Manual. Auto. Original" + "Manual selected".
   - **Squall does NOT walk while the ASK is open** (chase_ask_overlay gating).
3. Press arrows -- "Auto selected" / "Original selected" announce as in v0.15.7.1.
4. Press X -- "You chose <X>" announces; ASK closes; Squall regains control.
5. Verify the chosen mode took effect (or for v0.15.8: announces it for v0.15.9/.10).

### Risk

Medium. Two unknowns to verify:
1. **Input gating coordination.** Does chase_ask_overlay's existing flag stay set across the engine ASK's full lifetime?
2. **chase_ask_overlay's existing logic assumed TTS-only.** Any "auto-proceed after N seconds" logic needs deletion.

Once cleared, the v0.15.7.1 detection path is proven and the wiring is mechanical.

---

## Workflow reminders (unchanged)

- Filesystem MCP for ALL Windows project files. Bash cannot reach Windows source.
- Every response begins with `## Claude Says`.
- CHANGELOG.md ASCII-only in commit body. Heading must match `FF8OPC_VERSION` exactly.
- Aaron pushes via `Utilities/push_to_github.vbs` -- Claude never pushes.
- Build via `deploy.vbs` from project root.
- Version bumped in ONE place: `FF8OPC_VERSION` in `src/ff8_accessibility.h`.
- F12 alone = `Phase1_TestMes`; Shift+F12 = `Phase2_TestAsk`.
- FF8 confirm key is X (not Enter).

---

## State of the codebase

**v0.15.7.1 BAT-PASSED, ready to push. v0.15.6.2 (commit `249d9e47`) is HEAD on GitHub.**

- `src/dialog_inject.h` -- v0.15.7.1 (design rationale through v0.15.7.1, GetLastAnswer decl)
- `src/dialog_inject.cpp` -- v0.15.7.1 (state vars, helpers, Update() answer-detection block with gating, Phase2_TestAsk arms detection, Shutdown disarms, POST-ASK readback fixed)
- `src/field_dialog.cpp` -- v0.15.6.2 (unchanged; whitelist + post-ASK patch in place)
- `src/dinput8.cpp` -- v0.15.5 (unchanged)
- `src/deploy.bat` -- unchanged from v0.15.4
- `src/ff8_accessibility.h` -- `FF8OPC_VERSION "0.15.7.1"` with full comment trail
- `CHANGELOG.md` -- top entry `## v0.15.7.1`, `## v0.15.7` below it (push-quality)
- `DEVNOTES.md`, `NEXT_SESSION_PROMPT.md` -- post-BAT, ready-to-push, v0.15.8 plan

---

## Quick-start for next session

1. Read this file + DEVNOTES.md.
2. Verify GitHub HEAD: if v0.15.7.1 there, work v0.15.8. If still v0.15.6.2, ask Aaron about push status.
3. For v0.15.8, locate chase_ask_overlay.cpp/.h and read its current OpenAsk logic + input gating flag. Then implement OpenAsk in dialog_inject and rewire chase_ask_overlay to call it.

## v0.15.8 implementation checklist (next session ready-to-go)

- [ ] Read chase_ask_overlay.cpp/.h to understand existing TTS-only overlay logic + input gating flag.
- [ ] dialog_inject.h: add `OpenAsk` decl, `ResetLastAnswer` decl. Document choice-array ownership (caller retains).
- [ ] dialog_inject.cpp: add `s_phase2Choices[]` static array (max 8 entries, 32 bytes each), `s_phase2NumChoices`. `EncodeFf8WithChoices(prompt, choices, n, outBuf)` helper. Factor `Phase2_TestAsk` body into `OpenAskInternal(prompt, choices, numChoices, defaultCursor, slot)`. Update CurQToOptionName to look up s_phase2Choices instead of hardcoded.
- [ ] Update `s_phase2Choices` from OpenAsk callers; clear on Shutdown.
- [ ] chase_ask_overlay.cpp: replace TTS-only OpenAsk body with DialogInject::OpenAsk + GetLastAnswer poll. Add `s_chaseAskWaiting` state. Wire `OnAnswer()` dispatch.
- [ ] Verify chase_ask_overlay's input gating flag stays set across the full engine ASK lifetime. Add hold-down if needed.
- [ ] ff8_accessibility.h: bump to 0.15.8, add comment trail entry.
- [ ] CHANGELOG.md: prepend v0.15.8 entry.
- [ ] DEVNOTES.md, NEXT_SESSION_PROMPT.md: refresh for v0.15.8 ready-to-BAT.
