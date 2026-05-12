# Next Session Prompt -- v0.15.9.9.1 READY-TO-BAT (duplicate "Let's go!" fix)

**Build state:** v0.15.9.9.1 implemented 2026-05-12, awaiting build. One-line predicate added to `src/field_dialog.cpp::ScanAndSpeakChoiceWindows` to skip windows whose `firstQ`/`lastQ` is `0xFF` (FF8's "no ASK fields set" sentinel).

**HEAD on GitHub:** v0.15.9.8.3 (pushed 2026-05-12). Local tree is two versions ahead at v0.15.9.9.1 awaiting BAT. After this BAT succeeds, Aaron pushes v0.15.9.9 + v0.15.9.9.1 in a single cumulative push to GitHub.

## Read first

1. `DEVNOTES.md` -- current state (v0.15.9.9.1 READY-TO-BAT section at top, plus v0.15.9.9 BAT success preserved for context).
2. This file -- BAT plan and verification markers.

## What just landed (v0.15.9.9.1)

Targeted fix for the duplicate Squall "Let's go!" line that v0.15.9.9 BAT identified as the one remaining issue.

### Root cause from Logs/ff8_dialog.log at 10:27:54-57

The chase-trigger MES fires once correctly through AMESW (win[0]). Three seconds later, when `chase_ask_overlay` opens the chase ASK in slot 2, `ScanAndSpeakChoiceWindows` iterates over ALL 8 dialog window slots. Slot 0 still holds Squall's "Let's go!" with `firstQ=0xFF lastQ=0xFF` (FF8's "no ASK fields set" sentinel). The existing skip predicates `if (firstQ == 0 && lastQ == 0) continue;` and `if (lastQ < firstQ) continue;` correctly catch the all-zeros sentinel and inverted ranges but miss the `(0xFF, 0xFF)` case. Slot 0 got decoded as a 0-choice dialog with non-empty prompt, and the prompt was spoken as a duplicate.

The v0.15.9.9 prompt change only affected slot 2; the duplicate was always coming from slot 0, which is why the new explainer prompt couldn't have eliminated it.

### Fix

`src/field_dialog.cpp` around line 681, added after the existing sentinel checks:

```cpp
if (firstQ == 0xFF || lastQ == 0xFF) continue;
```

`0xFF` is unambiguously a sentinel (FF8 dialogs cap at ~16 choices, so neither `firstQ` nor `lastQ` can be `0xFF` in a real ASK). Comments document the v0.15.9.9 BAT-traced rationale so future-Claude understands why the predicate is there.

## BAT plan for v0.15.9.9.1

1. `deploy.vbs` -> `src/deploy.ps1` -> `src/deploy.bat`.
2. Launch FF8, load the save near chase start.
3. Trigger the chase. Listen for the sequence:
   - Squall's "Forget it! Let's go!" line speaks ONCE via AMESW.
   - 3 seconds elapse (chase_ask_overlay's deferred-open timer).
   - The new ASK prompt reads: "X-ATM092 is heading right for you. How do you want to run?"
   - The three option labels read on cursor navigation, NO duplicate Squall line in between.
4. Choose **Auto** at the ASK.
5. Confirm chase completes hands-off with 0 battles (same as v0.15.9.9).

### Verification markers in Logs/ff8_dialog.log

**Expected sequence after fix:**

```
FieldDialog: [AMESW] win[0] Speaking: "Squall "Forget it!  Let's go!""
... 3s elapse ...
[DLG-INJ] v0.15.7 cursor-change slot=2 curQ 255->1 announce="Manual: ... selected"
```

**No longer expected (was the duplicate in v0.15.9.9):**

```
FieldDialog: [ASK] win[0] Parsed 0 choices (firstQ=255 lastQ=255 curChoice=0)
FieldDialog: [ASK] win[0] Speaking: "Squall "Forget it! Let's go!""
```

If the `[ASK] win[0] Parsed 0 choices` line is gone from the log, the fix worked. If it's still there, the predicate didn't catch the case and we need to investigate further.

### Outcomes

- **Best (duplicate eliminated):** Aaron hears Squall's line once, then the new ASK prompt + option labels cleanly. Aaron pushes v0.15.9.9 + v0.15.9.9.1 cumulatively to GitHub.
- **Partial (duplicate still present but log shows slot 0 skipped):** The duplicate is coming from a different code path. Next candidates: `Hook_show_dialog`'s slot-paint re-read or `DialogInject::OpenAsk`'s render path. v0.15.9.9.2 traces those.
- **Worst (something we didn't anticipate):** Revert the predicate in v0.15.9.9.2 and re-diagnose.

## Outstanding chase-scene items (after v0.15.9.9.1 BAT)

2. **v0.15.9.10 -- Original = MODE_ORIGINAL** (Aaron's item #2). Add `MODE_ORIGINAL = 2` to `ChaseDetector::Mode`. `chase_battle_freeze` short-circuits at top of hook for MODE_ORIGINAL (no PASS log, no NO-OP, no agent register, full pass-through to original). `chase_kani_freeze` similarly short-circuits. `chase_ask_overlay::CommitChoice` routes `ANSWER_ORIGINAL` to MODE_ORIGINAL instead of MODE_MANUAL. Vanilla-engine chase for users who want it.
3. **v0.15.9.11 -- Keyboard suppressor during Auto chase** (Aaron's item #3). Extend `HookedGetKeyState` in `field_nav_input_hooks.inl` to zero W (0x11), ESC (0x01), and FF8 confirm/cancel/menu scancodes when `ChaseAutoPilot::IsEngaged() && ChaseDetector::GetChaseMode() == MODE_AUTO`. Accessibility hotkeys (read via `GetAsyncKeyState` in mod-owned code) bypass the keyboard buffer entirely so they're unaffected.
4. **Aaron pushes v0.15.9.9 + v0.15.9.9.1 to GitHub** after BAT success. Push utility validates CHANGELOG heading matches `FF8OPC_VERSION`; refuses if mismatched. With v0.15.9.9.1 as the top heading, the push will create one cumulative commit from v0.15.9.8.3 to v0.15.9.9.1.
