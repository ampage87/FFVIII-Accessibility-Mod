# Next Session Prompt: v0.17.7.6.2 BAT triage

## Greeting

Start with `## Claude Says`. Read `DEVNOTES.md` and THIS file before any work.

## Where we are at session open

**v0.17.7.6.2 is implemented locally, awaiting Aaron's BAT.** `FF8OPC_VERSION` = `0.17.7.6.2`. `CHANGELOG.md` top heading matches.

**What's in this build:** the calibration-needed gate. When Aaron triggers AD on a field where `s_camAxesSource == "identity"` (degenerate-CA fallback) and calibration hasn't applied yet:
- AD does not start
- TTS announces: *"Camera not yet calibrated. Press an arrow key briefly to calibrate, then try again."*
- Aaron walks an arrow briefly; observer collects 2 samples; `[NAV-CAL]` fires
- TTS announces: *"Camera calibrated."*
- Aaron retries AD; drives correctly

This is "Option A" from the v0.17.7.6.1 BAT discussion. It defers AD until calibration applied, instead of trying to make AD self-correct (which the v0.17.7.6 and .6.1 BATs both proved unreliable because the wrong direction can push the player into a wall, producing `moveDist=0`).

**Regression safety:** the gate fires only on `source=identity` fields with pending calibration. CA-valid fields are byte-for-byte identical to v0.17.7.6.1 = identical to v0.17.7.5.5. The TTS at `[NAV-CAL]` is purely additive.

GitHub HEAD still at v0.17.7.5.5 (`6abcb8f`). v0.17.7.6, .6.1, .6.2 all stay local until .6.2 BAT'd clean.

## Status check at session open

If Aaron opens with **"BAT"** or a log paste: triage v0.17.7.6.2.

If Aaron opens with a build error: read `Logs/build_latest.log` first. (The new code uses `strcmp` which is already in scope from v0.17.7.6.1 and `ScreenReader::Speak` which is widely used in handlekeys.inl — no new headers needed.)

If Aaron opens with a question or design discussion about the calibration UX: most likely scenario is he wants v0.17.7.6.3 (synthetic look-around at field load) if the walk-then-retry friction is too much. Discuss before coding — that one is more intrusive.

## BAT triage workflow

### Step 1: Confirm build clean

Read `Logs/build_latest.log`. Two-file change set; build should be clean.

### Step 2: Pull the field log + accessibility log

`Logs/ff8_field.log` for `[NAV-PROJ-INIT]`, `[drive] REFUSED`, `[NAV-CAL]`, `[NAV-OBSERVE]` lines.
`Logs/ff8_accessibility.log` (or `ff8_mod.log`) for the two TTS announcements.

Sanity check version: `FieldNavigation: Initialized v0.17.7.6.2`.

### Step 3: Primary test — bgroad_5 retry

The key sequence Aaron should observe (and the logs should confirm):

1. **Field load.** `[NAV-PROJ-INIT] WARNING field='bgroad_5' camera 2D projections degenerate ...` + `[NAV-PROJ-INIT] field='bgroad_5' ... source=identity`.
2. **Aaron presses backslash (AD trigger).**
3. `ff8_mod.log` shows TTS: `"Camera not yet calibrated. Press an arrow key briefly to calibrate, then try again."`
4. `ff8_field.log` shows: `FieldNavigation: F9 drive REFUSED (camera axes not yet calibrated: source=identity, pending empirical correction)`.
5. AD does NOT start. No `[drive] started toward` line. No `[drive-vec]` lines. No fake gamepad install.
6. **Aaron walks UP arrow for a few seconds.** Player moves.
7. `[NAV-OBSERVE] field='bgroad_5' axes=identity arrow=UP held=18ticks delta=(279,0) ... DIVERGE=90deg` — first sample.
8. After 1500ms throttle, second sample comes in. `[NAV-OBSERVE] ... arrow=UP delta=(...,0)`.
9. **`[NAV-CAL]` fires.** Expected values: `camRight (1.000,0.000)->(-0.000,-1.000) camDown (0.000,-1.000)->(-1.000,0.000) det=-1.000 source=empirical-corrected`.
10. `ff8_mod.log` shows TTS: `"Camera calibrated."`
11. **Aaron presses backslash again.** AD starts normally.
12. `[drive] started toward ...` line logged. `[drive-vec]` lines show the corrected axes producing correct direction injection.
13. Drive completes; Aaron arrives at dormitory.

### Step 4: Regression sanity

Confirm Aaron's standard fields behave the same as v0.17.7.5.5:
- bghall_1 auto-drive to any exit: AD starts immediately. No refusal. No `[NAV-CAL]`. Behavior identical.
- bg2f_2 auto-drive: identical.
- A field with valid CA in general: `[NAV-PROJ-INIT] ... source=ca-quantized` (the misleading log was fixed in .6.1). AD starts immediately.

### Step 5: Edge cases (if Aaron tests)

- **Walk first, then AD:** Calibration fires from manual walking; "Camera calibrated" plays; AD starts normally without ever hearing the refusal message. Working as designed.
- **Trigger AD multiple times rapidly before walking:** Each backslash press fires the refusal TTS again. Throttling at the SAPI/queue layer should prevent spam — verify it's not stacking up an annoying queue of identical announcements. If it does, add a debounce window on the refusal speak (note for follow-up, not blocking).
- **Cancel and restart AD on a CA-valid field after the gate fires once:** State should reset cleanly between fields via the field-load reset in fieldscripts.inl. No carry-over of `s_camAxesEmpiricalApplied` flag from a prior field.
- **Re-enter bgroad_5 after leaving:** Accumulator + lock flag both reset. Aaron must re-calibrate. (Acceptable: calibration is per field-load.)

## Reporting back to Aaron

**If everything works:**
1. Mark v0.17.7.6, .6.1, .6.2 all ✅ in DEVNOTES.
2. The three are push-ready as a coherent batch (calibration math + threshold + gate + UX messaging).
3. Suggest v0.17.7.7 (SETLINE-position promotion + NPC ResolveFriendlyName) as the next chapter.

**If the gate fires but TTS doesn't play:**
- Check `ff8_mod.log` for any `ScreenReader::Speak` errors.
- Verify `ff8_field.log` shows the `[drive] REFUSED` line (proves the gate fired).
- If logging fires but TTS doesn't, the ScreenReader pipeline has a queue issue — investigate that side.

**If TTS plays but AD doesn't refuse:**
- Check the strcmp comparison. Possible the gate logic is short-circuiting wrong way.
- Pull the `[NAV-PROJ-INIT]` line for the field — confirm it actually says `source=identity` at the time of AD trigger.

**If a regression surfaces** (working field starts misbehaving):
- Pull that field's `[NAV-PROJ-INIT]` line. Should say `source=ca-quantized`.
- The refusal gate must NOT fire on CA-valid fields. If it does, the strcmp condition is broken.

## File-access reminder

**Mod files are on Windows.** Use `filesystem:`-prefixed MCP tools. BAT logs at `Logs/ff8_*.log`. F11 screenshots at `Logs/screenshots/f11_HHMMSS_NNN.png` — use `filesystem:read_media_file` to view (load via `tool_search("read image media file")` if not already loaded).

For mid-file log searches, use `filesystem:edit_file` with `dryRun=true` and a unique `oldText` anchor.

## Session checkpoint rule reminder

After BAT triage:
1. Update DEVNOTES.md (mark .6.2 ✅ or note regressions).
2. Rewrite this NEXT_SESSION_PROMPT.md for whatever comes next.
3. If pushing: `Utilities/push_to_github.ps1`. Claude doesn't push. Aaron does. Utility checks version + CHANGELOG match before pushing.
