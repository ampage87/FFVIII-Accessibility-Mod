**Purpose & context**

Aaron is the sole developer of the FF8 Accessibility Mod -- a `dinput8.dll` injection for Final Fantasy VIII Steam 2013 (App ID 39150, FF8_EN.exe + FFNx v1.23.x) that makes the game playable for blind players via Windows SAPI text-to-speech, navigation assistance, entity catalogs. Aaron is blind himself and is the primary tester, using NVDA as his screen reader.

**Project root:** `C:/Users/ampag/OneDrive/Documents/FFVIII-Accessibility-Mod/FF8_OriginalPC_mod/`

GitHub: `ampage87/FFVIII-Accessibility-Mod`. **HEAD = v0.15.5.3** (commit `c58d993a`). **Local tree: v0.15.6.2 BAT-PASSED, ready to push.** Aaron heard "Mode?. Selected: Manual. Auto. Original" through the engine ASK render path. Push via `Utilities/push_to_github.vbs` when ready.

---

## v0.15.6.2 BAT result: SUCCESS

Phase 2B Test #1 in `doani1_2` at 00:59:30:

```
PRE  ASK gameObj.D2=0x00              clean slot 2
FIRING opcode_ask(0x65325610)
sub_49FD50(2): pCurrentDialogSlot 0xFF -> 0x02
v0.15.6.1 SetOverride active for slot 2
[POST-ASK-OVERRIDE] Patched slot[2]+0x08: 0x16C50E98 -> 0x64EAF020
[ASK] win[2] Parsed 3 choices (firstQ=1 lastQ=3 curChoice=1)
[ASK] win[2] Speaking: "Mode?. Selected: Manual. Auto. Original"
opcode_ask returned 1
POST ASK ... text1=0x64EAF020 (override=0x64EAF020)
[SHOW_DIALOG-TEXT] win[2] ... text="Mode? Manual Auto Original"   no [T2]
[SHOW_DIALOG-TEXT] win[2] (already spoken by opcode hook)         dedup caught it
```

State machine progressed `0 -> 1 -> 0xD` (ASK active with cursor). The slot held open through all 28 polls. `cursor=Manual` matched our default (`TEST_ASK_CUR_Q=1`). show_dialog's text1-vs-text2 logic resolved to text1 (our buffer) because `IsValidTextPointer` accepted it via the new whitelist. Dedup against `ws.lastSpokenText` / `lastRawText` suppressed the duplicate show_dialog speak.

End-to-end mod-driven dialog rendering with custom FF8-encoded text, working under FFNx, surviving the `field_get_dialog_string` bypass, reaching SAPI through the existing `[ASK]` choice handler. Closes the v0.15.0 -> v0.15.6.2 arc.

### Architectural lessons captured

- **FFNx's `replace_call` bypass.** v0.15.6's `Hook_field_get_dialog_string` was a dead address under FFNx. FFNx rewrites the engine's internal `CALL` instruction operand to point at FFNx's own implementation, so MinHook on the engine's entry point never fires. Confirmed via "zero `[GETSTR-RAW]` lines despite unconditional first-10-calls logging" in the v0.15.6 BAT log. Future dialog hooks should target the slot itself (post-`opcode_*`) rather than the text-fetch function.
- **`IsValidTextPointer` is FF8-heap-tuned.** The `0x00010000-0x30000000` range filters spurious pointers but rejects mod-DLL data section addresses (typical at `~0x60000000-0x80000000` under FFNx). Mod-injected buffers need either a whitelist or a relocation strategy. v0.15.6.2 chose targeted whitelisting via stable-address accessors.
- **Post-ASK slot patching is robust to FFNx versions.** It doesn't depend on FFNx's internal addresses or function names, only on the slot layout (`slot+0x08` = text_data1) which is engine-defined and stable.

---

## Push plan

`Utilities/push_to_github.vbs` will validate `CHANGELOG.md` top heading (`## v0.15.6.2`) matches `FF8OPC_VERSION` ("0.15.6.2"). The CHANGELOG entry is push-quality (full BAT diagnosis, fix mechanism, predicted vs actual outcome, files changed, risk).

Optional: Aaron may want to bundle v0.15.6.2 with v0.15.7 (answer detection) into one push since v0.15.6.2 alone doesn't yet detect which option the user chose. But each version is independently complete and can be pushed separately.

---

## Next: v0.15.7 -- answer detection

DialogInject's `Update()` polls per frame while a Phase 2B ASK is open. On cursor changes, speak which option is now selected. On commit, speak "You chose X" and store the answer for v0.15.8's chase wiring.

### v0.15.7 design

State additions in dialog_inject.cpp:

```cpp
static bool   s_phase2Active   = false;   // true while injected ASK is open
static int    s_phase2Slot     = -1;
static uint8_t s_phase2LastCurQ = 0xFF;   // last value seen at slot+0x2B
static int    s_phase2LastAnswer = -1;    // committed answer (-1 = not yet)
```

`Phase2_TestAsk` end (after firing opcode_ask):

```cpp
s_phase2Active   = true;
s_phase2Slot     = TEST_SLOT_ASK;
s_phase2LastCurQ = 0xFF;   // forces first-poll announce
s_phase2LastAnswer = -1;
```

Update() loop (alongside or replacing existing slot poll):

```cpp
if (s_phase2Active) {
    uint8_t curQ = ReadSlotByte(s_phase2Slot, 0x2B);  // NOT 0x2C
    uint8_t askMask = ReadGameObjMask(GAMEOBJ_ASK_MASK_OFFSET);
    bool slotBitClear = (askMask & (1 << s_phase2Slot)) == 0;
    uint32_t state = ReadSlotState(s_phase2Slot);

    // Cursor-change announce
    if (curQ != s_phase2LastCurQ && curQ >= 1 && curQ <= 3) {
        const char* names[] = { "Manual", "Auto", "Original" };
        char msg[64];
        snprintf(msg, sizeof(msg), "%s selected", names[curQ - 1]);
        ScreenReader::Speak(msg, false);  // queue
        s_phase2LastCurQ = curQ;
    }

    // Commit detection
    if (slotBitClear || state != 0xD) {
        s_phase2LastAnswer = (int)s_phase2LastCurQ;
        char msg[64];
        snprintf(msg, sizeof(msg), "You chose %s", names_indexed_by_lastCurQ);
        ScreenReader::Speak(msg, false);
        s_phase2Active = false;
    }
}
```

Public accessor:

```cpp
int GetLastAnswer();   // returns 1=Manual, 2=Auto, 3=Original, -1=none
```

### v0.15.7 BAT plan

1. Deploy via `deploy.vbs`.
2. Quit FF8 and re-launch.
3. Load any save in field mode.
4. Press **Shift+F12** -- hear "Mode?. Selected: Manual. Auto. Original" + diagnostic.
5. Press **Down** arrow -- hear "Auto selected" + cursor SFX.
6. Press **Down** again -- hear "Original selected" + cursor SFX.
7. Press **Up** -- hear "Auto selected" + cursor SFX.
8. Press **Enter** (or whatever the engine's commit key is) -- hear "You chose Auto".
9. Send `Logs/ff8_dialog.log`.

### v0.15.7 outcomes

- **SUCCESS**: each cursor move produces an "X selected" announce within ~100ms; commit produces "You chose X".
- **NO CURSOR ANNOUNCE**: slot+0x2B isn't being read correctly (off-by-one offset?). Add `slot[+0x2B]` to the existing slot poll diagnostic line to verify.
- **DOUBLE ANNOUNCE**: cursor poll fires before SAPI finishes the previous announce (interrupt=false should queue; if not, debounce by 50ms).
- **NO COMMIT DETECTION**: slot bit not clearing on Enter, or state staying at 0xD. Investigate which mechanism the engine uses to mark ASK as "answered".

### Risk

Very low. Pure read of slot bytes + SAPI calls. No engine state writes. No new hooks.

---

## Backlog (after v0.15.7 BAT)

### Sequence after v0.15.7 SUCCESS

- **v0.15.8**: Wire Phase 2B + answer detection into `chase_ask_overlay::OpenAsk` as the primary chase ASK path. Replaces v0.15.3's TTS-only overlay. Inherits chase_ask_overlay's input gating (resolves "arrows move Squall AND cursor simultaneously").
- **v0.15.9**: "Auto" option = run-from-robot logic.
- **v0.15.10**: "Original" option = chase-mod-active flag gating.

### Standalone (any version)

- X-ATM092 battle-name fix.
- Generalized countdown-timer hook.
- **Cosmetic cleanup (v0.15.x or 0.16):** remove the dead `Hook_field_get_dialog_string` override branch from field_dialog.cpp now that v0.15.6.2 SUCCESS confirms post-ASK patching is the correct path. Branch is harmless under FFNx (never fires) but adds noise.
- **Cosmetic cleanup:** dialog_inject.cpp's POST-ASK readback uses `slot+0x2C` for curQ but the actual curQ is at `slot+0x2B` (per FFNx ff8.h and field_dialog's working code). The 0x2C read returns the aux byte. Functionally harmless but the log line is misleading. Fix when convenient.
- **Cosmetic:** deploy.bat "Version: SINGLE-PRONGED" regex regression (introduced post-v0.15.3, the comment-trail line for v0.15.3 contains "SINGLE-PRONGED" and `findstr` picks it up). Tighten the regex.

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
