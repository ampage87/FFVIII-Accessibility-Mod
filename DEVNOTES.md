**Purpose & context**

Aaron is the sole developer of the FF8 Accessibility Mod -- a `dinput8.dll` injection for Final Fantasy VIII Steam 2013 (App ID 39150, FF8_EN.exe + FFNx v1.23.x) that makes the game playable for blind players via Windows SAPI text-to-speech, navigation assistance, entity catalogs. Aaron is blind himself and is the primary tester, using NVDA as his screen reader.

**Project root:** `C:/Users/ampag/OneDrive/Documents/FFVIII-Accessibility-Mod/FF8_OriginalPC_mod/`

GitHub: `ampage87/FFVIII-Accessibility-Mod`. **HEAD = v0.15.7.1** (commit `67e5b74f`). **Local tree: v0.15.8.1 BUILT, ready to BAT, NOT pushed.** v0.15.8 was BAT-confirmed SUCCESS but not pushed since v0.15.8.1 is a polish patch on top.

---

## Current build: v0.15.8.1 -- ready to BAT

UX polish on top of v0.15.8 per Aaron's BAT feedback. v0.15.8 BAT was a clean SUCCESS end-to-end (dialog opened, cursor announces fired, X commit captured Manual at 11:05:06, chase proceeded with 1-battle-per-field cap holding through Selphie's rendezvous line) but Aaron noticed a glitch -- after pressing X, three TTS announces stacked up while the chase resumed:

1. "Manual selected" (cursor highlight just before commit)
2. "You chose Manual" (DialogInject's commit announce)
3. "Manual mode selected. One battle per chase field will be allowed." (chase_ask_overlay's verbose CommitChoice)

~10 seconds of speech. The chase doesn't wait. v0.15.8.1 trims this to a single brief line.

### What v0.15.8.1 ships

**chase_ask_overlay -- descriptive choice labels.** kChaseChoices changed from `{"Manual", "Auto", "Original"}` to:

- `"Manual: one battle per field"` -- states implemented behavior
- `"Auto: falls back to manual"` -- honest about current state until v0.15.9 implements
- `"Original: falls back to manual"` -- honest about current state until v0.15.10 implements

The descriptions get spoken on cursor change during navigation (DialogInject's existing `"<name> selected"` announce path) and on dialog open (FieldDialog's `[ASK]` hook reads them from the encoded override buffer). No new code path -- the labels carry the description.

**chase_ask_overlay -- brief commit announces.** CommitChoice trimmed from verbose multi-clause messages to single short lines per Aaron's exact phrasing:

- Manual -> `"Manual selected"`
- Auto -> `"Automatic selected"`
- Original -> `"Original selected"`
- default -> `"Manual selected"` (silent fallback, no "unknown choice" announce)

ChaseDetector::SetChaseMode(MODE_MANUAL) routing unchanged for all three options.

**dialog_inject -- announceCommit param.** OpenAsk gains a sixth parameter `bool announceCommit = true`. Plumbed through OpenAskInternal and stored in s_phase2AnnounceCommit. When false, Update()'s commit branch suppresses the generic `"You chose <name>"` SAPI announce (still logs `[DLG-INJ] v0.15.8.1 commit announce suppressed by caller`). chase_ask_overlay passes false; Phase2_TestAsk passes true (Shift+F12 has no other commit announce).

**Infrastructure.** PHASE2_NAME_CAP 32 -> 64 to fit descriptive labels. Cursor-announce and commit-announce msg buffers in Update() bumped 64 -> 128 so longer names + `" selected"` / `"You chose "` suffixes don't snprintf-truncate.

### v0.15.8.1 BAT plan

1. Deploy via `deploy.vbs`.
2. Quit FF8 and re-launch.
3. Load a save just before a chase trigger.
4. Trigger the chase. Expect:
   - Squall's "Forget it!  Let's go!" plays normally.
   - After ~3 seconds (TRIGGER_DELAY_MS), engine ASK renders.
   - FieldDialog [ASK] hook speaks: "Mode?. Selected: Manual: one battle per field. Auto: falls back to manual. Original: falls back to manual."
   - Initial cursor announce: "Manual: one battle per field selected".
   - Press **Down** -> "Auto: falls back to manual selected".
   - Press **Down** again -> "Original: falls back to manual selected".
   - Press **X** -> single brief "Manual selected" / "Automatic selected" / "Original selected". **NO** "You chose <name>" announce.
5. Verify chase resumes promptly without TTS still rattling on.
6. Verify subsequent chase battles cap at 1 per field.
7. Send `Logs/ff8_dialog.log` and `Logs/ff8_field.log`.

### Expected log signature

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

### v0.15.8.1 BAT outcomes

- **SUCCESS**: chase ASK opens, descriptions spoken on navigation, commit announces brief line, NO "You chose" announce, chase resumes promptly. Move to v0.15.9 (Auto = run-from-robot). Push v0.15.8.1 (which carries the v0.15.8 changes too since v0.15.8 was never pushed).
- **DESCRIPTION TOO LONG**: if the engine truncates a label visually or rejects it, fall back to shorter labels with description moved to a separate per-cursor-move announce. Watch for crashes from buffer overflow in EncodeFf8 / override buffer (256 bytes) -- with 95-byte encoded text we have lots of room but worth verifying.
- **REGRESSION**: if all three TTS announces still stack (cursor + "You chose" + verbose), check that announceCommit gate fires (look for the suppression log line) and that CommitChoice strings are the short ones.

### Known-but-deferred (unchanged from v0.15.8)

- Squall and party walking during open ASK.
- Number-key shortcuts (removed; arrows + X only).
- Phase2_TestAsk (Shift+F12) stays as standalone diagnostic. Aaron will NOT toggle Shift+F12 during BAT.

---

## v0.15.8 -- BAT-confirmed SUCCESS (NOT pushed; superseded locally by v0.15.8.1)

For the record: v0.15.8 dialog log evidence at 11:04:52-11:05:06:
- `[DLG-INJ] v0.15.7.1 active-state observed slot=2 t+578ms (state=0xD D2=0x04); commit gating now armed`
- Cursor moved through Manual/Auto/Original/Manual/Auto/Original/Auto/Manual multiple times, each announce fired correctly.
- `[DLG-INJ] v0.15.7 commit reason=state left 0xD capturing answer=1`
- `[DLG-INJ] v0.15.7 announce="You chose Manual"`
- Chase battles followed (Diamond Dust, Can't escape, Escaped, Selphie's "Hurry to the rendezvous!"), 1-per-field cap held.

Wiring path is proven. v0.15.8.1 is purely UX polish on the same path.

---

## Backlog

### After v0.15.8.1 SUCCESS

- **v0.15.9**: implement Auto = run-from-robot (X-ATM092 chase auto-flee). Update kChaseChoices[1] to "Auto: skip the chase battle" or similar.
- **v0.15.10**: implement Original = chase-mod-active flag (vanilla chase behavior). Update kChaseChoices[2] to "Original: vanilla chase, no battle cap" or similar.
- **v0.15.x cleanup**: remove Phase2_TestAsk + Shift+F12 hotkey + Phase 1 test once chase wiring is fully proven across multiple chase entries.
- **v0.15.x input gating**: address Squall-walking during ASK. Approaches: (1) DirectInput proxy filter while askOpen, (2) find engine's input-block flag via disassembly.

### Standalone (any version)

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
