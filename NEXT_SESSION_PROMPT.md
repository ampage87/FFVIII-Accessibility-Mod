# Next Session Prompt -- v0.15.6.2 BAT-PASSED, ready to push

**Status:** v0.15.6.2 BAT-PASSED. Aaron heard "Mode?. Selected: Manual. Auto. Original" through the engine ASK render path. Ready to push via `Utilities/push_to_github.vbs`. GitHub HEAD currently = v0.15.5.3 (commit `c58d993a`).

If you're reading this in a fresh session:
1. Read this file + DEVNOTES.md.
2. If GitHub HEAD is now `v0.15.6.2`: Aaron pushed -- proceed to v0.15.7 (answer detection).
3. If GitHub HEAD is still `v0.15.5.3`: Aaron hasn't pushed yet -- ask what's next.

---

## What v0.15.6.2 closed

The v0.15.0 -> v0.15.6.2 arc shipped end-to-end mod-driven dialog rendering with custom FF8-encoded text:

| Version | Approach | Outcome |
|---------|----------|---------|
| v0.15.0-v0.15.2.1 | Populate ff8_win_obj slot directly | Engine ignored slot (per-slot callback registration via `sub_4A0880` happens at startup; externally-populated slots aren't in the registry) |
| v0.15.4 | Synthesize phantom script_context, call opcode_mes(&ctx) | Phase 1 SUCCESS. Engine renders natural msg 0 |
| v0.15.5/.5.1/.5.2/.5.3 | Same recipe via opcode_ask | Phase 2a SUCCESS. Cursor input wired (sub_49FD50). SAPI race fixed (queue not interrupt) |
| v0.15.6 | Inject custom text via Hook_field_get_dialog_string override | FAILED. FFNx replace_call bypassed our hook (zero `[GETSTR-RAW]` lines despite unconditional logging) |
| v0.15.6.1 | Post-ASK slot+0x08 patching in Hook_opcode_ask | Pointer swap landed but `IsValidTextPointer` rejected our DLL-data-section address |
| v0.15.6.2 | Whitelist override buffer's exact range in `IsValidTextPointer` | SUCCESS. End-to-end working |

### v0.15.6.2 BAT log signature (Phase 2B Test #1, 00:59:30)

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

State machine progressed `0 -> 1 -> 0xD` cleanly. Slot held open through all 28 polls.

### Key architectural lessons (capture for HISTORY)

1. **FFNx's `replace_call` bypass.** FFNx rewrites engine-internal CALL operands to point at FFNx's own functions. MinHook on the engine's entry point never fires. Future dialog hooks should target slot state post-`opcode_*` rather than the text-fetch function. Smoking gun: zero `[GETSTR-RAW]` lines despite unconditional first-10-calls logging.
2. **`IsValidTextPointer` is FF8-heap-tuned.** The `0x00010000-0x30000000` range filters spurious pointers but rejects mod-DLL data-section addresses (typical at `~0x60000000-0x80000000` under FFNx). Mod-injected buffers need either a whitelist or relocation. v0.15.6.2 chose targeted whitelisting via stable-address accessors.
3. **Post-ASK slot patching is FFNx-version-robust.** Doesn't depend on FFNx's internal addresses or function names, only on the engine-defined slot layout.

---

## Push plan

`Utilities/push_to_github.vbs` validates `CHANGELOG.md` top heading (`## v0.15.6.2`) matches `FF8OPC_VERSION` ("0.15.6.2"). Both match. CHANGELOG entry is push-quality with full BAT diagnosis, fix mechanism, and risk assessment.

Aaron's choice: push v0.15.6.2 alone, or bundle with v0.15.7 (answer detection) for a single push. Either way the next BAT is v0.15.7.

---

## v0.15.7 plan -- answer detection

DialogInject's `Update()` polls per frame while a Phase 2B ASK is open. On cursor changes, speak which option is now selected. On commit, speak "You chose X" and store the answer for v0.15.8's chase wiring.

### v0.15.7 design

State additions in dialog_inject.cpp:

```cpp
static bool   s_phase2Active   = false;
static int    s_phase2Slot     = -1;
static uint8_t s_phase2LastCurQ = 0xFF;
static int    s_phase2LastAnswer = -1;
```

`Phase2_TestAsk` end (after firing opcode_ask):

```cpp
s_phase2Active   = true;
s_phase2Slot     = TEST_SLOT_ASK;
s_phase2LastCurQ = 0xFF;       // forces first-poll announce
s_phase2LastAnswer = -1;
```

Update() loop (alongside or replacing existing 3-second slot poll):

```cpp
if (s_phase2Active) {
    uint8_t curQ = ReadSlotByte(s_phase2Slot, 0x2B);   // NOTE: 0x2B not 0x2C
    uint8_t askMask = ReadGameObjMask(GAMEOBJ_ASK_MASK_OFFSET);
    bool slotBitClear = (askMask & (1 << s_phase2Slot)) == 0;
    uint32_t state = ReadSlotState(s_phase2Slot);

    // Cursor-change announce
    if (curQ != s_phase2LastCurQ && curQ >= 1 && curQ <= 3) {
        const char* names[] = { "Manual", "Auto", "Original" };
        char msg[64];
        snprintf(msg, sizeof(msg), "%s selected", names[curQ - 1]);
        ScreenReader::Speak(msg, false);
        s_phase2LastCurQ = curQ;
    }

    // Commit detection
    if (slotBitClear || state != 0xD) {
        s_phase2LastAnswer = (int)s_phase2LastCurQ;
        if (s_phase2LastAnswer >= 1 && s_phase2LastAnswer <= 3) {
            const char* names[] = { "Manual", "Auto", "Original" };
            char msg[64];
            snprintf(msg, sizeof(msg), "You chose %s", names[s_phase2LastAnswer - 1]);
            ScreenReader::Speak(msg, false);
        }
        s_phase2Active = false;
    }
}
```

Public accessor (header):

```cpp
int GetLastAnswer();   // returns 1=Manual, 2=Auto, 3=Original, -1=none
```

### Important: slot+0x2B is curQ, NOT slot+0x2C

dialog_inject.cpp's existing POST-ASK readback uses `slot+0x2C` for curQ -- that's the AUX byte, not curQ. The actual curQ is at `slot+0x2B` (per FFNx's ff8.h and per field_dialog's working choice handler which has been correctly reading curChoice at 0x2B since v04.03). The v0.15.5.1 BAT comment that established the stack-arg map got the byte offsets crossed in the comment. v0.15.7 must read 0x2B for cursor changes.

(Cosmetic followup: fix dialog_inject.cpp's POST-ASK log line to read 0x2B and label it correctly. Or leave as a known quirk and document.)

### v0.15.7 BAT plan

1. Deploy via `deploy.vbs`.
2. Quit FF8 and re-launch.
3. Load any save in field mode.
4. Press **Shift+F12** -- hear "Mode?. Selected: Manual. Auto. Original" + diagnostic. Cursor on Manual.
5. Press **Down** arrow -- hear "Auto selected" + FF8 cursor SFX.
6. Press **Down** again -- hear "Original selected" + cursor SFX.
7. Press **Up** -- hear "Auto selected" + cursor SFX.
8. Press **Enter** (or whatever the engine's commit key is for ASK) -- hear "You chose Auto".
9. Send `Logs/ff8_dialog.log`.

### v0.15.7 outcomes

- **SUCCESS**: each cursor move produces "X selected" within ~100ms; commit produces "You chose X". Move to v0.15.8 chase wiring.
- **NO CURSOR ANNOUNCE**: `slot+0x2B` not changing on arrows? Add slot+0x2B to the existing slot poll diagnostic to confirm. Could mean the engine writes curQ to a different offset, or our 0x2B reads are racing with the engine's writes.
- **DOUBLE/STUTTERING ANNOUNCE**: cursor poll firing too fast. Debounce by ~100ms.
- **NO COMMIT DETECTION**: slot bit not clearing on Enter, or state staying at 0xD. Investigate which mechanism the engine uses to mark ASK as "answered" -- could be `gameObj.D2 & (1<<slot)` clearing, OR `state != 0xD`, OR neither (engine pushes the answer back to the script-VM which we don't run).
- **WRONG ANSWER**: cursor poll captured stale curQ at commit time. Use `s_phase2LastCurQ` directly rather than re-reading at commit moment.

### Risk

Very low. Pure read of slot bytes + SAPI calls. No engine state writes. No new hooks.

---

## Workflow reminders (unchanged)

- Filesystem MCP for ALL Windows project files. Bash cannot reach Windows source.
- Every response begins with `## Claude Says`.
- CHANGELOG.md ASCII-only in commit body. Heading must match `FF8OPC_VERSION` exactly. Push utility refuses if mismatched.
- Aaron pushes via `Utilities/push_to_github.vbs` -- Claude never pushes.
- Build via `deploy.vbs` from project root.
- Version is bumped in ONE place: `FF8OPC_VERSION` in `src/ff8_accessibility.h`.
- F12 alone = `Phase1_TestMes`; Shift+F12 = `Phase2_TestAsk`.

---

## State of the codebase

**v0.15.6.2 BAT-PASSED, ready to push. v0.15.5.3 (commit `c58d993a`) is HEAD on GitHub.**

- `src/dialog_inject.h` -- v0.15.6.2 (design rationale documenting v0.15.6 BAT failure, v0.15.6.1 patch landing without TTS pickup, v0.15.6.2 whitelist fix; `GetOverrideSlot`/`GetOverrideBufferStart`/`GetOverrideBufferSize` decls)
- `src/dialog_inject.cpp` -- v0.15.6.2 (override state, EncodeFf8 utility, SetOverride/ClearOverride/GetOverride*, Phase2_TestAsk with full v0.15.5.x mechanics + v0.15.6.1 SetOverride pre-call)
- `src/field_dialog.cpp` -- v0.15.6.2 (forward-decl block exposes DialogInject namespace; `Hook_opcode_ask` post-ASK patch block; `IsValidTextPointer` whitelist clause for override buffer; v0.15.6 dead-hook override branch left in `Hook_field_get_dialog_string` as documentation)
- `src/dinput8.cpp` -- v0.15.5 (unchanged)
- `src/deploy.bat` -- unchanged from v0.15.4
- `src/ff8_accessibility.h` -- `FF8OPC_VERSION "0.15.6.2"` with full comment trail
- `CHANGELOG.md` -- top entry `## v0.15.6.2` (push-quality body)
- `DEVNOTES.md` -- post-BAT, ready-to-push, v0.15.7 plan
- `NEXT_SESSION_PROMPT.md` -- this file

---

## Quick-start for next session

1. Read this file + DEVNOTES.md.
2. If Aaron has pushed v0.15.6.2: implement v0.15.7 per the design above. Five files: dialog_inject.{h,cpp}, ff8_accessibility.h, CHANGELOG.md, DEVNOTES.md/NEXT_SESSION_PROMPT.md.
3. If Aaron hasn't pushed yet: ask what to work on. Likely v0.15.7 implementation in parallel for bundled push.

## v0.15.7 implementation checklist (next session ready-to-go)

- [ ] dialog_inject.h: add `int GetLastAnswer();` decl; document v0.15.7 design.
- [ ] dialog_inject.cpp: add s_phase2Active/Slot/LastCurQ/LastAnswer state; add poll block in Update(); set state in Phase2_TestAsk after opcode_ask returns; clear in Shutdown.
- [ ] dialog_inject.cpp: fix POST-ASK readback to read slot+0x2B (curQ) not slot+0x2C (aux). Optional cosmetic but consistent with v0.15.7 changes.
- [ ] ff8_accessibility.h: bump to 0.15.7, add comment trail entry.
- [ ] CHANGELOG.md: prepend v0.15.7 entry.
- [ ] DEVNOTES.md, NEXT_SESSION_PROMPT.md: refresh for v0.15.7 ready-to-BAT.
