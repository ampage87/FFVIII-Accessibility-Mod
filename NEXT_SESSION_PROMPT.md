# Next Session Prompt -- v0.15.8.1 ready to BAT

**Status:** v0.15.8.1 BUILT, ready to BAT. UX polish on top of v0.15.8 (which BAT'd SUCCESS but had a TTS-stacking glitch on commit). NOT pushed. GitHub HEAD = v0.15.7.1 (commit `67e5b74f`); v0.15.8 changes are local-only and will ship together with v0.15.8.1 in the same push when v0.15.8.1 SUCCESS.

If you're reading this in a fresh session:
1. Read this file + DEVNOTES.md.
2. Tail `Logs/ff8_dialog.log` for `announceCommit=0` line and `v0.15.8.1 commit announce suppressed by caller` line.
3. If no chase-ask lines appear despite triggering chase, check `Logs/build_latest.log` for build errors.

---

## What v0.15.8.1 ships

### chase_ask_overlay -- descriptive choice labels

`kChaseChoices` updated from `{"Manual", "Auto", "Original"}` to:

```cpp
static const char* kChaseChoices[] = {
    "Manual: one battle per field",
    "Auto: falls back to manual",
    "Original: falls back to manual"
};
```

Descriptions get spoken on cursor change during navigation (DialogInject's existing `"<name> selected"` announce path) and on dialog open (FieldDialog's `[ASK]` hook reads them from the encoded override buffer).

### chase_ask_overlay -- brief commit announces

`CommitChoice` trimmed from verbose multi-clause messages to single short lines:

- Manual -> `"Manual selected"`
- Auto -> `"Automatic selected"`
- Original -> `"Original selected"`
- default -> `"Manual selected"` (silent fallback)

`ChaseDetector::SetChaseMode(MODE_MANUAL)` routing unchanged.

### dialog_inject -- announceCommit param

`OpenAsk` gains `bool announceCommit = true` (sixth param). When false, `Update()`'s commit branch suppresses `"You chose <name>"` SAPI announce. chase_ask_overlay passes `false`; Phase2_TestAsk (Shift+F12) passes `true`. Default true preserves Phase2_TestAsk's diagnostic behavior.

### Infrastructure

- `PHASE2_NAME_CAP` 32 -> 64 (fits descriptive labels)
- Cursor-announce + commit-announce `msg` buffers 64 -> 128
- New static `s_phase2AnnounceCommit` set per OpenAskInternal call

## v0.15.8.1 BAT plan

1. Deploy via `deploy.vbs`.
2. Quit FF8 and re-launch.
3. Load a save just before a chase trigger.
4. Trigger the chase. Expect:
   - Squall's "Forget it!  Let's go!" plays normally.
   - After ~3 seconds, engine ASK renders.
   - FieldDialog [ASK] hook speaks: "Mode?. Selected: Manual: one battle per field. Auto: falls back to manual. Original: falls back to manual."
   - Initial cursor announce: "Manual: one battle per field selected".
   - Press **Down** -> "Auto: falls back to manual selected".
   - Press **Down** again -> "Original: falls back to manual selected".
   - Press **X** to commit -> single brief "Manual selected" / "Automatic selected" / "Original selected". **NO** "You chose <name>" announce.
5. Chase resumes promptly without TTS still rattling on.
6. Subsequent chase battles cap at 1 per field.
7. Send `Logs/ff8_dialog.log` and `Logs/ff8_field.log`.

## Expected log signature

```
[FIELD] ChaseAskOverlay: chase trigger MES detected: "Forget it!  Let's go!"; deferring ASK open by 3000 ms
[FIELD] ChaseAskOverlay: deferred-open timer expired; opening ASK now
[DLG-INJ] ===== OPEN-ASK TEST #N START =====
[DLG-INJ] v0.15.8 override text: "Mode?\nManual: one battle per field\nAuto: falls back to manual\nOriginal: falls back to manual" -> ~95 bytes encoded
[DLG-INJ] FIRING opcode_ask(...)...
[DLG-INJ] opcode_ask returned 1
[DLG-INJ] v0.15.7 answer-detection armed for slot 2 (timeout 60000 ms, announceCommit=0)
[FIELD] ChaseAskOverlay: chase ASK opened via DialogInject (slot=2, 3 choices, default cursor=1)
[DLG-INJ] v0.15.7 cursor-change slot=2 curQ 255->1 announce="Manual: one battle per field selected"
[DLG-INJ] v0.15.7.1 active-state observed slot=2 t+~450ms (state=0xD D2=0x04); commit gating now armed
            ... user navigates and presses X ...
[DLG-INJ] v0.15.7 commit reason=state left 0xD capturing answer=1
[DLG-INJ] v0.15.8.1 commit announce suppressed by caller (answer=1 name="Manual: one battle per field")
[FIELD] ChaseAskOverlay: DialogInject::GetLastAnswer returned 1; dispatching
[FIELD] ChaseAskOverlay: committed choice = 1 (Manual: one battle per field)
[FIELD] ChaseAskOverlay: chase ASK closed
```

## BAT outcomes

### SUCCESS

Chase ASK opens, descriptions spoken on navigation, single brief commit announce, NO "You chose" announce, chase resumes promptly. Move to **v0.15.9** (Auto = run-from-robot).

### DESCRIPTION TOO LONG

If the engine truncates a label visually or rejects it, look for crashes or odd behavior. With 95-byte encoded text we have plenty of room in the 256-byte override buffer. If problems, fall back to shorter labels with description split into a separate announce.

### REGRESSION

If all three TTS announces still stack (cursor + "You chose" + verbose), check:
- The suppression log line `v0.15.8.1 commit announce suppressed by caller` -- if missing, announceCommit gate isn't firing.
- The arm-time log line `announceCommit=0` -- if it's `=1`, chase_ask_overlay isn't passing `false`.
- CommitChoice strings -- look for `committed choice = N (Manual: one battle per field)` log; if it announces verbose, the trimmed strings didn't land.

### CRASH

Check `Logs/build_latest.log`. The 0.15.8.1 changes are minor (param plumbing + string changes); a crash would likely indicate a build error in the param signature mismatch.

---

## Workflow reminders (unchanged)

- Filesystem MCP for ALL Windows project files. Bash cannot reach Windows source.
- Every response begins with `## Claude Says`.
- CHANGELOG.md ASCII-only in commit body. Heading must match `FF8OPC_VERSION`.
- Aaron pushes via `Utilities/push_to_github.vbs` -- Claude never pushes.
- Build via `deploy.vbs` from project root.
- Version bumped in ONE place: `FF8OPC_VERSION` in `src/ff8_accessibility.h`.
- F12 alone = `Phase1_TestMes`; Shift+F12 = `Phase2_TestAsk`. Aaron will NOT toggle Shift+F12 during v0.15.8.1 BAT.
- FF8 confirm key is X (not Enter).

---

## State of the codebase

**v0.15.8.1 BUILT, NOT pushed. v0.15.7.1 (commit `67e5b74f`) is HEAD on GitHub. v0.15.8 changes are local-only -- they ship together with v0.15.8.1 when pushed.**

- `src/dialog_inject.h` -- v0.15.8.1 (announceCommit param + comment)
- `src/dialog_inject.cpp` -- v0.15.8.1 (PHASE2_NAME_CAP bump, msg bumps, s_phase2AnnounceCommit, OpenAskInternal new param, commit-branch gating + suppression log, arm-time log update, Phase2_TestAsk pass-through, public OpenAsk pass-through)
- `src/chase_ask_overlay.cpp` -- v0.15.8.1 (descriptive kChaseChoices, OpenAsk announceCommit=false, brief CommitChoice announces)
- `src/chase_ask_overlay.h` -- v0.15.8 (unchanged)
- `src/field_dialog.cpp` -- v0.15.6.2 (unchanged)
- `src/dinput8.cpp` -- v0.15.5 (unchanged)
- `src/ff8_accessibility.h` -- `FF8OPC_VERSION "0.15.8.1"` with full v0.15.8.1 + v0.15.8 + earlier comment trail
- `CHANGELOG.md` -- top entry `## v0.15.8.1` (push-quality)
- `DEVNOTES.md`, `NEXT_SESSION_PROMPT.md` -- this file

---

## Next: v0.15.9 (after v0.15.8.1 SUCCESS)

Implement Auto option = run-from-robot. The chase scene has a hardcoded X-ATM092 robot that triggers battles. Auto mode should attempt to flee or auto-navigate to the chase exit. Update `kChaseChoices[1]` to reflect implemented behavior (e.g. `"Auto: skip the chase battle"`).

Open questions (to resolve in v0.15.9 design):
- Does the chase have predetermined exits (which field to enter next)?
- Can field navigation auto-drive to the exit during chase?
- How does X-ATM092's pursuit AI interact with the player's auto-drive?

## Next: v0.15.10

Implement Original option = vanilla chase behavior with no battle-cap. Update `kChaseChoices[2]` to reflect implemented behavior (e.g. `"Original: vanilla chase, no battle cap"`). Selecting Original disables the chase-mod-active flag in `ChaseDetector` so battles fire as in vanilla FF8.

---

## Quick-start for next session

1. Read this file + DEVNOTES.md.
2. Tail `Logs/ff8_dialog.log` for `announceCommit=0` and `commit announce suppressed by caller` lines.
3. If chase ASK opens but commit announce regression: check the arm-time log and suppression log for diagnostic.
4. After v0.15.8.1 SUCCESS, push v0.15.8.1 (which carries v0.15.8 changes too) and plan v0.15.9 (Auto = run-from-robot).

---
