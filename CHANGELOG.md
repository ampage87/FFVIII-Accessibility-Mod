# FF8 Accessibility Mod — Changelog

Newest on top. Each entry begins with a `## vMAJOR.MINOR.BUILD` heading followed by a blank line and the commit message body. The push utility (`Utilities/push_to_github.ps1`) reads the top heading to determine the version being pushed and uses everything between that heading and the next `## v` heading as the commit message body.

The version in the top heading **must** match `FF8OPC_VERSION` in `src/ff8_accessibility.h`. The push utility refuses to push if they don't.

## v0.15.3

Single-pronged cleanup: remove the static kani+battleyarou pin from chase_kani_freeze; fix the deploy.bat "Version: World" regex bug.

The v0.15.2.15 BAT was a milestone success. Aaron played through the entire X-ATM092 chase end-to-end -- five mountain-trail fields, the bridge (doopen2a), Town Square (dotown_3), and out the chase-end Lapin Beach FMV with all eight audio descriptions playing cleanly across the 74-second cutscene. One battle per field, no crashes, no hangs, no robot-walking-around. Every chase field's CHASE-AGENT FINAL SUMMARY showed the dynamic agent pin holding the actual robot at zero or near-zero changed bytes. The doopen2a strcmp guard worked exactly as designed (one PASS log line, no [CHASE-AGENT] line, fieldId-flip deactivation fired correctly on the doopen2a -> dotown_3 handoff). The DEVNOTES decision criterion was satisfied: "if [CHASE-AGENT] resolves AND CHASE-AGENT FINAL SUMMARY shows few/zero byte changes AND freeze# count is low for that field, the agent pin is sufficient."

### What changes in v0.15.3

The static kani+battleyarou pin in `src/chase_kani_freeze.cpp` is removed. Across three v0.15.2.x BATs, the OTHERS-DIAG scanner consistently showed kani had at most 7 changed bytes and battleyarou had 0 -- both static pins were inert in every chase field tested, because the actual chase agents in those fields were rinoa-slot in domt5_1, director0 in doopen2a, and various robot-slots in the trail fields, NOT the kani or battleyarou symbols the static pin was targeting. They were dead code.

What goes:

- The `s_kaniPtr` / `s_strideBytes` / `s_arrayKind` / `s_haveFullSnapshot` / `s_fullSnapshot` / `s_initial` / `s_prev` / `s_byteFirstChangeLogged` state.
- The `s_battleyarouPtr` / `s_battleyarouStrideBytes` / `s_battleyarouArrayKind` / `s_battleyarouInitial` / `s_haveBattleyarouSnapshot` / `s_battleyarouSnapshot` state.
- `ReadKaniBlock`, `LogInitialSnapshot`, `LogChangeSummary`, `DiffAndLogFirstChanges` helpers.
- The kani INITIAL / snapshot / memcpy / FINAL blocks in `StartCapture`, `ApplyFreezePin`, `EndCapture`.
- The battleyarou INITIAL / snapshot / memcpy / FINAL blocks in the same three functions.
- The per-tick FIRST CHANGE diff loop and the MID-WINDOW heartbeat in `Update` (both anchored to the kani buffer).
- The kani-related cleanup lines in `DeactivateFreeze`.

What stays:

- The dynamic chase-agent pin (`RegisterChaseAgent` + agent INITIAL / snapshot / memcpy / FINAL SUMMARY blocks).
- The v0.15.2.14 fieldId-flip deactivation (`ReadCurrentFieldId` helper, `s_freezeFieldId` capture, raw-fieldId check before debounced-name check).
- The v0.15.2.9 OTHERS-DIAG diagnostic scanner (kept for future agent-resolution audits).
- The v0.15.2.3.1 capture trigger (`s_battleSeenRecently` mode 3->1 detection).
- The SEH probe pattern before agent writes.
- The `LogHexRow` helper (used for the AGENT-INIT log block).

### Net effect

`chase_kani_freeze.cpp` goes from ~700 lines to ~580 lines. The field log gets much quieter during chase battles -- no per-tick FIRST CHANGE spam, no MID-WINDOW heartbeat, no kani / battleyarou INITIAL or FINAL SUMMARY blocks. The CHASE-AGENT lines and the OTHERS-DIAG block remain. The per-frame cost in chase fields drops by two memcpys plus one ReadKaniBlock per Update tick.

`chase_kani_freeze.h`'s design comment is rewritten to document the v0.15.3 single-pronged design, with the v0.15.2.x history retained as a terse trail. The Initialize log line is updated to "v0.15.3 DYNAMIC AGENT PIN ONLY" wording.

### Cosmetic fix bundled in: deploy.bat "Version: World" regex bug

The v0.15.2.x deploy log lines all printed `Version: World` instead of the actual version string. Root cause: the `findstr /C:"FF8OPC_VERSION "` pattern in `src/deploy.bat` matched comment-trail lines in `ff8_accessibility.h` in addition to the actual `#define`. The `for /f` loop's last-iteration-wins behavior left `VERSION` set to token 3 of an unrelated comment line.

Fix: tighten the findstr to `/C:"#define FF8OPC_VERSION "` so only the actual `#define` line matches. Drop the now-redundant `^| findstr /V "DATE"` filter. The `%%~V` modifier strips surrounding quotes from `"0.15.3"` to give `VERSION=0.15.3` in the deploy log.

### Risk

Very low. The removed code paths only ran during chase-field battle exits, never wrote to entities outside their resolved kani/battleyarou pointers, and v0.15.2.15 BAT's OTHERS-DIAG already proved kani and battleyarou were inert -- removing inert pins changes nothing the engine observes.

### Predicted v0.15.3 BAT outcome

Identical chase behavior to v0.15.2.15: single-fight chase, robots stay down, dotown_3 transition succeeds, Lapin Beach FMV plays through to dotown_2. Field log is shorter and cleaner. Deploy log shows `Version: 0.15.3` instead of `Version: World`.

### Files changed

- `src/chase_kani_freeze.cpp` (rewritten, ~580 lines down from ~700)
- `src/chase_kani_freeze.h` (design comment rewrite)
- `src/ff8_accessibility.h` (version bump to 0.15.3 + new comment trail entry)
- `src/deploy.bat` (1 line: tighten findstr to `#define`-prefixed)
- `CHANGELOG.md` (this top entry)
- `DEVNOTES.md`, `NEXT_SESSION_PROMPT.md`

## v0.15.2.15

Surgical doopen2a fix on top of v0.15.2.14 -- skip dynamic chase-agent pin in doopen2a only

The v0.15.2.14 BAT confirmed that the new dynamic chase-agent pin works beautifully across the five mountain-trail chase fields (domt1_1 through domt5_1). Auto-announce field name fired correctly on every load. The robots stayed pinned. No crashes.

The failure was in doopen2a, the bridge field between the trail and dotown_3 (fieldId=0x014D; Aaron called it "town square" in his report, but the freeze actually happened one field upstream of dotown_3). After the chase battle in doopen2a resolved, the field froze before any transition. Music kept playing, no crash, but no dotown_3 entry was ever logged.

### Diagnosis

The v0.15.2.14 BAT log gives the answer. In doopen2a, the dynamic agent pin registered the BATTLE caller as `director0` (Others slot 4, symIdx 9) -- and the CHASE-AGENT FINAL SUMMARY showed 41 changed bytes in the t=0..1500ms grace period before our snapshot, scattered across the animation/position regions (+0x140-+0x147 X/Y, +0x190-+0x197, +0x1B5-+0x1BA, +0x1F6-+0x1FA, +0x206-+0x207). After t=1500ms, the pin held director0 frozen for the rest of the field session.

Cross-referencing with the v0.15.2.13 BAT: the BATTLE caller in doopen2a was logged at entityPtr=0x0188CA04, which v0.15.2.14 correctly resolved to director0. So in doopen2a the BATTLE caller and the chase-progress-tracker are the same entity. Pinning its full state for the rest of the field session prevents the chase-end script from advancing -- director0 presumably waits on a flag/counter byte at one of the offsets we're now overwriting every frame, so the transition to dotown_3 never fires.

The v0.15.2.10 deferred concern about director0 was prescient. domt1_1 through domt5_1 don't share this pattern: their BATTLE caller is the actual robot (the chase agent), not the field's progress director, so pinning it works.

### Fix

`src/chase_battle_freeze.cpp` Hook_opcode_battle PASS branch now captures the debounced field name and wraps the RegisterChaseAgent call in a strcmp:

```cpp
const char* fieldName = ChaseDetector::GetDebouncedFieldName();
if (fieldName != nullptr && std::strcmp(fieldName, "doopen2a") == 0) {
    Log::Field("[CBF] PASS in doopen2a -- skipping RegisterChaseAgent ...");
} else {
    ChaseKaniFreeze::RegisterChaseAgent((uintptr_t)entityPtr);
}
```

The BATTLE NO-OP gate (battleCount >= 1) is unchanged and carries the load in doopen2a -- that field has exactly one chase battle in the whole sequence, so capping at 1 is sufficient. Other chase fields keep the dynamic pin.

The static kani+battleyarou pin in chase_kani_freeze still runs in doopen2a, but it's inert there: per the v0.15.2.14 OTHERS-DIAG, kani had 7 changed bytes and battleyarou had 0, so the pin had nothing to hold.

Also added `<cstring>` include for `std::strcmp` and updated the Initialize log line to v0.15.2.15 wording ("skip register-agent in doopen2a only"). v0.15.2.14 dynamic agent pin design, tightened deactivation (raw fieldId check + SEH probe), and field_announce module are all UNCHANGED.

### What v0.15.2.15 BAT should show

- Each chase field except doopen2a: one `[CBF] PASS` line plus one `[CHASE-AGENT]` line (same as v0.15.2.14)
- doopen2a: one `[CBF] PASS in doopen2a -- skipping RegisterChaseAgent` line, NO `[CHASE-AGENT]` line for that field, no CHASE-AGENT FINAL SUMMARY block
- doopen2a kani+battleyarou FINAL SUMMARY shows changed_bytes=0 (those entities still dormant)
- A `KaniFreeze: FREEZE DEACTIVATED -- fieldId changed 0x014D -> 0x0158 (pre-debounce)` line shortly after the doopen2a battle ends
- dotown_3 cutscene plays through, chase-end FMV fires, control returns to dotown_2 or wherever the chase ends

If the freeze recurs even with the dynamic pin disabled in doopen2a, the cause is something else and we'll need a different angle (next candidates: was the static kani+battleyarou pin in earlier doopen2a entries causing trouble? does the BATTLE NO-OP itself break the chase-end script in doopen2a?).

### Files changed

- `src/chase_battle_freeze.cpp` (~30 lines: capture fieldName, wrap RegisterChaseAgent in strcmp guard, add `<cstring>` include, update Initialize log line)
- `src/chase_battle_freeze.h` (~20 lines: new design rationale paragraph at top documenting v0.15.2.15 doopen2a skip)
- `src/ff8_accessibility.h` (version bump to 0.15.2.15 + new comment trail entry)
- `CHANGELOG.md` (this entry)
- `DEVNOTES.md`, `NEXT_SESSION_PROMPT.md`

## v0.15.2.14

Dynamic chase-agent pin + tightened deactivation (dotown_3 crash fix) + auto-announce field name on field load

v0.15.2.13 BAT confirmed end-to-end traversal of the X-ATM092 chase but exposed two issues that this build addresses, plus an unrelated user-requested feature.

### Issue 1: dotown_3 crash recurred

v0.15.2.13 BAT crashed approximately 16 seconds after entering dotown_3 from doopen2a, the same pattern as the v0.15.2.10 BAT crash. v0.15.2.11's removal of dotown_3 from CHASE_FIELD_NAMES had stopped chase_kani_freeze from starting a new capture inside dotown_3, but it didn't fix the handoff. The actual mechanism: when fieldId flips from doopen2a (0x014D) to dotown_3 (0x0158), there is a roughly 2-second window before ChaseDetector's name-debounce settles on the new name. The v0.15.2.4 ApplyFreezePin design explicitly preserved the cached pointer through that window ("empty-string field name during the 2s name-debounce after fieldId flip does NOT deactivate -- we don't yet know the destination"). During those 2 seconds, ApplyFreezePin kept writing 0x21 to bytes at +0x150/+0x23F/+0x241 and 0x14 to +0x154/+0x1FA on the cached doopen2a kani pointer, plus a 292-byte memcpy of the full-state snapshot, on a memory region that had been freed and reallocated to dotown_3 entities. The cutscene crashed when it read the corrupted state.

v0.15.2.11 was pushed to GitHub but never demonstrably proven crash-free -- the v0.15.2.11 BAT didn't actually replay through dotown_3, v0.15.2.12 got stuck in domt5_1, and v0.15.2.13 was the first build since v0.15.2.10 to reach dotown_3.

### Issue 2: Pin was hitting the wrong entities

v0.15.2.13's BATTLE NO-OP suppressed combat correctly (6 PASS + 8 NO-OP across 14 chase BATTLE calls in 6 fields, exactly one PASS per field). But the actual chase agent in domt5_1 -- the rinoa-slot wearing kani's robot model (Others slot 3, model 12, 47 changed bytes in the v0.15.2.12 OTHERS-DIAG) -- was waking up and walking around silently while the BATTLE NO-OP suppressed combat. The kani+battleyarou pin from v0.15.2.7-.8 was holding two dormant entities perfectly still while the actual robot wandered loose. Aaron's design preference inverted: pin should keep the agent down on the ground (so it stays incapacitated visually too); BATTLE NO-OP becomes the safety net for cases where the pin misses.

### Fix 1: Dynamic chase-agent pin

`src/chase_kani_freeze.h` exposes a new `RegisterChaseAgent(uintptr_t entityPtr)` entry point. `src/chase_battle_freeze.cpp` calls it from the PASS branch of Hook_opcode_battle, handing over the entity pointer that just made the BATTLE call. `RegisterChaseAgent` resolves the pointer to (arrayKind, slot, symIdx, symName) by reading `pFieldStateOthers` and `pFieldStateBackgrounds` bases under SEH, walking with the appropriate stride (0x264 / 0x1B4), and checking offset modulo + slot range. On success, logs a structured line for per-field identity audit:

```
[CHASE-AGENT] field='domt5_1' entityPtr=0x0188C5D8
              -> array=Others slot=3 symIdx=7 sym='rinoa'
              stride=0x264 header[0x00..0x10]: <16 hex bytes>
```

On failure (pointer outside both arrays, JSMCounts unavailable, etc.), logs `[CHASE-AGENT-UNRESOLVED]` with the reason -- pin stays inactive, BATTLE NO-OP carries the load.

The new agent state runs alongside kani+battleyarou. StartCapture snapshots the agent's INITIAL state (logged as `AGENT-INIT` hex dump). ApplyFreezePin takes the agent's full-state snapshot at SNAPSHOT_DELAY_MS=1500ms post-activation, then memcpy's it back over the agent's +0x140..stride region every frame. EndCapture logs CHASE-AGENT FINAL SUMMARY with changed_bytes count and per-byte deltas vs INITIAL.

Thread safety: RegisterChaseAgent runs on the game thread, ApplyFreezePin on the mod thread. Identity fields (stride, kind, slot, symName) are written first; s_chaseAgentPtr is written last (32-bit aligned, atomic on x86, store-store reordering forbidden), so the mod thread sees either uninitialized or fully populated state.

### Fix 2: Tightened deactivation

`src/chase_kani_freeze.cpp` ApplyFreezePin captures `pCurrentFieldId` at FREEZE ACTIVATED time (new state s_freezeFieldId) and reads it under SEH every frame. If the live fieldId differs from the captured value, it calls a new centralized `DeactivateFreeze(reason)` helper that clears all pin state (kani + battleyarou + agent + snapshot flags + freeze ticks) immediately -- before the 2-second name-debounce settles. The existing debounced-name check stays as a backup. Plus a one-byte SEH-guarded probe read before each entity write to catch torn pointers; on fault, skip the frame silently.

This fixes the dotown_3 crash without dotown_3 needing any special-case logic. The fieldId check fires the moment the engine flips it, before any reallocation can cross-contaminate.

### Fix 3: BATTLE NO-OP becomes the safety net

`src/chase_battle_freeze.h` and `.cpp` are repurposed from primary suppression (v0.15.2.13) to safety-net + agent identifier. The PASS branch now calls RegisterChaseAgent(entityPtr) as a side effect of the existing log line. The NO-OP gate (battleCount >= 1) is preserved unchanged as the fallback. In a healthy v0.15.2.14 run, the pin holds the agent on the ground and the NO-OP fires zero or rarely; the freeze# counter from the Shutdown summary becomes a real diagnostic (low = pin healthy, high = pin missing the agent and safety net carrying the load).

### Feature: auto-announce field name on field load

New module `src/field_announce.{h,cpp}` (~140 lines total). Polls `FF8Addresses::pCurrentFieldId` every Update tick under SEH; on fieldId change, starts an 800ms debounce timer; once the new fieldId is stable, looks up `FIELD_DISPLAY_NAMES[fieldId]` (the existing 982-entry catalog from `src/field_display_names.h`) and calls `ScreenReader::Speak(name, false)` -- queued, not interrupting, so dialog or in-flight TTS finishes first. Skip rules: fieldId == 0 (title screen), fieldId out of range, already-announced (no spam on battle/menu re-entry to the same field), not in MODE_FIELD. Wired into dinput8.cpp's main loop next to FieldNavigation::Update.

### Files changed

- `src/chase_kani_freeze.h` (rewritten ~70 lines)
- `src/chase_kani_freeze.cpp` (rewritten ~700 lines)
- `src/chase_battle_freeze.h` (rewritten ~50 lines)
- `src/chase_battle_freeze.cpp` (rewritten ~135 lines: include chase_kani_freeze.h, RegisterChaseAgent call in PASS branch)
- `src/field_announce.h` + `.cpp` (NEW)
- `src/dinput8.cpp` (~20 lines: include field_announce.h, FieldAnnounce::Initialize/Update/Shutdown wired in, ChaseKaniFreeze + ChaseBattleFreeze comment blocks rewritten)
- `src/deploy.bat` (1 line: field_announce.cpp added to compile list)
- `src/ff8_accessibility.h` (version bump)
- `CHANGELOG.md` (this entry)
- `DEVNOTES.md`, `NEXT_SESSION_PROMPT.md`

### What v0.15.2.14 BAT should show

For each chase field, exactly one `[CBF] PASS` line followed by exactly one `[CHASE-AGENT]` line resolving the BATTLE caller's entityPtr to a slot identity. Six fields total = six pairs. After the post-battle StartCapture in each field, a `KaniFreeze: CHASE-AGENT INITIAL snapshot` line and a `KaniFreeze: CHASE-AGENT full-state snapshot taken` line about 1500ms later. Subsequent collisions in the same field should NOT generate `[CBF] NO-OP` lines if the pin is working (the agent stays on the ground and never reaches BATTLE again); a low freeze# count at Shutdown indicates a healthy pin. CHASE-AGENT FINAL SUMMARY changed_bytes should be near zero per field.

On the doopen2a -> dotown_3 transition, expect a single `KaniFreeze: FREEZE DEACTIVATED -- fieldId changed 0x014D -> 0x0158 (pre-debounce)` line, then no further pin activity, no crash, dotown_3 cutscene plays through to credits.

Aaron should also hear field display names auto-spoken on every field load.

## v0.15.2.13

Flip `chase_battle_freeze` from passive observer to active BATTLE NO-OP based on v0.15.2.12 BAT data

The v0.15.2.12 BAT in `domt5_1` produced three `[CBF]` log lines, one
per chase battle, all paired one-to-one with `ChaseDetector battle
entered` events:

```
11:51:32  [CBF] chase BATTLE call #2 (total #3)  battleCount=0
          caller=other  entityPtr=0x0188C5D8
11:53:21  [CBF] chase BATTLE call #3 (total #4)  battleCount=1
          caller=other  entityPtr=0x0188C5D8
11:54:47  [CBF] chase BATTLE call #4 (total #5)  battleCount=2
          caller=other  entityPtr=0x0188C5D8
```

`opcode_battle` fires for every chase battle in `domt5_1`. The
v0.15.2.2 finding that opcode_battle was dead in `domt5_1` was wrong:
v0.15.1's pass-through logger sampled at every-50th-call frequency and
the freeze branch only logged kani-driven calls, so non-kani chase
BATTLE calls were essentially invisible. v0.15.2.12's per-call
chase-field logging caught what earlier builds missed.

### What entityPtr 0x0188C5D8 actually is

`othersBase(0x0188BEAC) + 3 * 0x264 = Others slot 3 = symIdx 7 =
'rinoa' SYM entry`. Same-window OTHERS-DIAG showed rinoa-slot had
`changed_bytes=47/612` (second-most-active entity in `domt5_1`); the
immediately-prior `[TALKRAD]` line for the same pointer showed
`model=12`, kani's robot model. The rinoa-slot is the actual chase
agent in `domt5_1`, wearing model 12. Not the kani-slot (slot 8, only
the 5 pinned bytes changed). Not the battleyarou-slot (slot 10, 0
changes).

v0.15.2.8/9/10 had been pinning the wrong two entities the whole time
in `domt5_1`. The pin worked perfectly but missed the chase agent.

### What v0.15.2.13 ships

A simple BATTLE-opcode NO-OP that doesn't depend on knowing which
entity is the chase agent. `chase_battle_freeze.cpp` reactivates the
freeze branch with gate:

```cpp
freeze = (mode == MODE_MANUAL && IsInChaseField()
          && GetCurrentFieldBattleCount() >= 1);
```

Returns `JSM_RC_ADVANCE = 3` without invoking the original handler
when the gate matches. **The caller-identity check (kani / battleyarou)
is dropped from the gate** -- it's still computed for log tagging only.
The first chase battle per field still passes through so the scripted
opening encounter fires; only the second-and-later calls are NO-OP'd.

Same mechanism v0.15.1 used successfully in `domt4_1`, just with a
broader gate.

### Implementation

**`src/chase_battle_freeze.h` (rewritten):**
- Documents the v0.15.2.13 design rationale and includes the v0.15.2.12
  BAT findings inline so future maintainers don't repeat the v0.15.2.2
  misinterpretation.
- Public API (`Initialize`, `Shutdown`) unchanged.

**`src/chase_battle_freeze.cpp` (rewritten):**
- `Hook_opcode_battle` re-enables the freeze branch with the
  caller-agnostic gate.
- New `s_freezeCount` global counts NO-OP'd calls for the Shutdown
  summary.
- Per-call log lines tagged `NO-OP` or `PASS` so the BAT can verify the
  freeze pattern at a glance.
- `Initialize` log line documents the active-freeze role.

**`src/dinput8.cpp`:**
- `ChaseBattleFreeze::Initialize` / `Shutdown` comment blocks updated to
  reflect ACTIVE FREEZE role.

### Predicted v0.15.2.13 BAT outcome

Enter `domt5_1`, fight battle #1 normally (`PASS` log line), walk
toward field exit, robot collides with Squall, opcode_battle fires but
is NO-OP'd (`NO-OP` log line). Squall continues walking, exits to next
chase field, pattern repeats. End result: traverse the entire chase
scene through the Lapin Beach FMV.

### Risk

Very low.

1. No entity bytes touched -- v0.15.2.11's `dotown_3` cutscene fix
   stays intact.
2. Returning JSM advance code without calling original is exactly what
   v0.15.1 did in `domt4_1` -- and that worked.
3. Aaron confirmed there are no random encounters during the chase
   scene, so the "cap at 1 battle per chase field" policy doesn't
   suppress legitimate non-chase battles.
4. The v0.15.2.12 BAT showed Aaron stuck in `domt5_1` with three
   battles and no forward progress -- this build directly addresses
   that failure mode.

### What this also resolves

The `doopen2a` second-chase-battle issue (deferred from v0.15.2.10 over
concerns about a `director0` pin breaking the chase-end logic) is
**solved by this fix without ever needing to pin `director0`**.
v0.15.2.13 caps `doopen2a` at one battle the same way it caps every
other chase field.

### What this does NOT change

- v0.15.2.11 dotown removal preserved.
- v0.15.2.10 `domt1_1` chase coverage preserved.
- v0.15.2.9 OTHERS-DIAG scanner preserved.
- v0.15.2.8 dual-entity (kani + battleyarou) pin preserved -- now
  defensive belt-and-suspenders, harmless when those entities aren't
  the chase agent and may still help in fields where kani-slot IS the
  agent (e.g. `domt4_1`).
- All earlier kani-pin layers preserved.

### Files changed

- `src/chase_battle_freeze.h` (rewritten)
- `src/chase_battle_freeze.cpp` (rewritten)
- `src/dinput8.cpp` (init / shutdown comment blocks)
- `src/ff8_accessibility.h` (version bump)
- `CHANGELOG.md` (this entry)
- `DEVNOTES.md`, `NEXT_SESSION_PROMPT.md`

## v0.15.2.12

Reactivate `chase_battle_freeze.cpp` as a passive opcode_battle observer (no behavior change, log only)

v0.15.2.10 BAT confirmed `doopen2a` fires a second chase battle even
with kani + battleyarou pinned. The clean OTHERS-DIAG at 22:03:34
flagged `director0` (31 changes / 612 bytes) as the prime non-pinned
suspect, but pinning a third entity carries real risk -- if `director0`
is the chase-progress-tracker, freezing it could re-break the chase-end
cutscene that v0.15.2.11 just unblocked, or stall progression in some
other way we can't predict from byte-change counts alone.

Before committing to a `director0` pin we want one piece of empirical
data: does the second-battle event in `doopen2a` go through
`opcode_battle` at all? If yes, the cleanest fix is a simple BATTLE
NO-OP hook (no entity bytes touched, no script-state corruption). If
no, we know the second battle takes a different code path -- the same
dead-code situation v0.15.2.2 BAT documented for `domt2_1` and
`domt5_1` -- and we move to entity-level work knowing the cheaper
strategy was never available.

### What v0.15.2.12 ships

**Reactivates the orphan `chase_battle_freeze.{h,cpp}` as a pure
passive observer.** The hook is installed on
`pExecuteOpcodeTable[0x69]` and ALWAYS forwards to the original
handler. No freeze, no NO-OP, no short-circuit. This is observation
only.

When `ChaseDetector::IsInChaseField()` is true, every BATTLE call
emits one log line:

```
[CBF] chase BATTLE call #N (total #M) field='X' mode=Y battleCount=Z
      caller=kani|battleyarou|other entityPtr=0xADDR
```

The `caller` tag uses `ChaseDetector::IsKaniEntityPtr` and
`IsBattleyarouEntityPtr` to identify whether the calling script entity
is one of the two we already track. For other callers, the raw
`entityPtr` value lets the BAT analyst correlate against the entity
addresses captured in OTHERS-DIAG.

Outside chase fields the hook is silent (no log spam from random
encounters or non-chase scripted battles).

### Three predicted BAT outcomes

**(A) FIRES-BOTH** -- two `[CBF]` lines in `doopen2a` (battleCount=0
for the first battle, battleCount=1 for the second). Strategy 1
(NO-OP the second+ call) is viable; v0.15.2.13 ships the active
freeze.

**(B) FIRES-FIRST-ONLY** -- one `[CBF]` line for the first battle,
none for the second that `ChaseDetector` still observes via
game-mode 1 -> 3 transition. Same dead-code pattern as `domt2_1`
and `domt5_1`. Move to Strategy 2 (targeted byte pin on the specific
`director0` offsets that change) or Strategy 3 (full `director0`
freeze, riskiest).

**(C) FIRES-NEITHER** -- no `[CBF]` lines at all but `ChaseDetector`
still reports battles. Confirms `opcode_battle` is fully dead in
`doopen2a`. Same conclusion as B.

### Implementation

**`src/chase_battle_freeze.h` (rewritten ~30 lines):**
- Updated header comment block to describe the v0.15.2.12 passive
  observer role and the three predicted outcomes.
- Public API (`Initialize`, `Shutdown`) unchanged.

**`src/chase_battle_freeze.cpp` (rewritten ~140 lines):**
- Removed the freeze branch entirely. `Hook_opcode_battle` now
  computes chase-field state purely for logging and unconditionally
  forwards to `s_origBattle(entityPtr)`.
- Per-call logging inside chase fields includes both kani and
  battleyarou caller-pointer matches via the existing
  `ChaseDetector::Is*EntityPtr` helpers (battleyarou support added
  in v0.15.2.8 was not present in v0.15.1).
- Removed periodic pass-through summary lines (every-50th sampling
  no longer needed -- we want EVERY chase-field call).
- `Initialize` log line updated to v0.15.2.12 wording.
- `Shutdown` log line now reports both total opcode_battle calls and
  the chase-field subset.

**`src/deploy.bat` (1 line):**
- `chase_battle_freeze.cpp` added back to the cl.exe compile list,
  immediately after `chase_kani_freeze.cpp`.

**`src/dinput8.cpp` (~12 lines):**
- `#include "chase_battle_freeze.h"` added.
- `ChaseBattleFreeze::Initialize()` called after
  `ChaseKaniFreeze::Initialize()`. ChaseDetector is already
  initialized earlier in the chain so its kani / battleyarou queries
  return valid data when the hook fires.
- `ChaseBattleFreeze::Shutdown()` called before
  `ChaseKaniFreeze::Shutdown()` (reverse order).

### What this does NOT change

- v0.15.2.11 dotown removal preserved.
- v0.15.2.10 `domt1_1` chase coverage preserved.
- v0.15.2.9 OTHERS-DIAG scanner preserved.
- v0.15.2.8 dual-entity (kani + battleyarou) pin preserved.
- All earlier kani-pin layers preserved.
- `ChaseAskOverlay`, `ChaseDiag`, `ChaseDetector` unchanged.

### Risk

Zero. The hook does not modify engine behavior in any case. The only
output is log lines. If for some reason the hook fails to install
(MinHook error, address resolution failure), the build still runs
identically to v0.15.2.11 -- the failure path logs an error and
leaves the engine untouched.

### v0.15.2.12 BAT plan

Drive Squall through the chase scene focusing on `doopen2a`. Whatever
outcome we observe (A, B, or C above) directly determines v0.15.2.13's
strategy.

### Files changed

- `src/chase_battle_freeze.h` (rewritten)
- `src/chase_battle_freeze.cpp` (rewritten)
- `src/deploy.bat` (compile list)
- `src/dinput8.cpp` (init / shutdown wiring)
- `src/ff8_accessibility.h` (version bump)
- `CHANGELOG.md` (this entry)
- `DEVNOTES.md`, `NEXT_SESSION_PROMPT.md`

## v0.15.2.11

Remove `dotown_3`/`dotown_2`/`dotown_1` from `CHASE_FIELD_NAMES[]`

v0.15.2.10 BAT crashed/hung ~16 seconds after entering `dotown_3` from
`doopen2a` (Fri 2026-05-08 22:05:13–22:05:31). Aaron's diagnosis:
`dotown_3`'s chase-end cutscene plays an animation where X-ATM092
walks across the town square and shorts out, driven by the `dotown_3`
kani entity in `Backgrounds` slot 1. With `dotown_3` in
`CHASE_FIELD_NAMES`, our `chase_kani_freeze` module kept tracking
`dotown_3`'s kani address and — if a mode 4→1 transition fired during
the cutscene — would `StartCapture` and pin the kani's anim ID bytes
(`+0x150`/`+0x154`/`+0x1FA`/`+0x23F`/`+0x241`), directly fighting the
cutscene's animation script every frame. v0.15.2.9 BAT didn't crash on
this transition because timing happened to skip the `StartCapture`
trigger; v0.15.2.10 got unlucky.

### Implementation

**`chase_detector.cpp` (~5 lines deleted, ~20 lines of comment):**
- Removed `"dotown_3"`, `"dotown_2"`, `"dotown_1"` from
  `CHASE_FIELD_NAMES[]`.
- These are post-chase town fields where the chase-end cutscene plays.
  No kani battles fire there; the chase is over.
- Comment block extended explaining the v0.15.2.10 crash and the
  rationale for removing all three fields together.

### Behavioral change

- `ChaseDetector::IsInChaseField()` returns `false` on entry to
  `dotown_3` instead of returning `true` until reaching Lapin Beach.
- `chase_kani_freeze::StartCapture` won't fire in `dotown_3`/`dotown_2`/
  `dotown_1` (no chase battle fires there anyway, but the field-tracking
  no longer attempts to engage).
- `chase_ask_overlay::s_askFiredThisChase` flag stays true since the
  ASK plays at chase START in `domt4_1` (long before reaching `dotown_3`).
  No re-fire risk.
- The `dotown_3` cutscene plays unimpeded.

### What this does NOT fix

**The `doopen2a` "second chase battle" issue.** v0.15.2.10 BAT showed
kani+battleyarou pinned but battle still triggered (capture #2 at
22:03:34 ended cleanly, then ANOTHER chase battle fired before capture #3
at 22:05:05). The clean OTHERS-DIAG at 22:03:34 identifies `director0`
(31 changes/612) as the prime non-pinned suspect.

A `director0` pin is **deferred to v0.15.2.12** for a separate reason:
`director0` might be the chase-progress-tracker, in which case pinning
it could break the same chase-end cutscene we just unblocked. We want
v0.15.2.11 to ship a clean win first, then evaluate `director0`
separately.

### v0.15.2.10 BAT — other major data point

**`domt5_1` clean OTHERS-DIAG (18 slots, in-field, post-battle):**

| Sym | Changes | |
|-----|---------|---|
| selphie2 | 73 | party member, highest |
| irvine | 64 | party member |
| rinoa | 47 | party member |
| zell2 | 31 | party member |
| kani | 5 | pinned |
| battleyarou | 0 | pinned (already dormant) |
| dic, plane1, onkyou, Garutyan, liti, gura, saidotoujou, Gakekuzure | 0 | **all static** |

**The previous "Director-is-the-chase-agent in `domt5_1`" hypothesis is
refuted.** Every Director candidate shows zero changes. The active
entities are all party members running their normal chase-cutscene
animations (Selphie/Irvine running alongside Squall, dialogue triggers).
The kani+battleyarou pin worked correctly in `domt5_1` (only one chase
battle fired, no second-battle issue), so the chase IS triggered by kani
contact in `domt5_1`; the party member script activity is incidental.

### domt1_1 chase coverage — confirmed working

```
[21:59:51] ChaseDetector: battle entered (game-mode 0x0001 -> 0x0003);
           field='domt1_1' chaseActive=1 count=1
```

`chaseActive=1` (was `0` in v0.15.2.9 BAT). The kani pin now activates
in `domt1_1` battles. v0.15.2.10's domt1_1 fix is doing its job.

### Risk

Minimal. The change is a deletion of three array entries plus a comment
update. The chase scene's only active engagement points are the mountain
trail and bridge fields (`domt1_1` through `doopen2a`); `dotown_x` was
never functionally in scope.

### Files changed

- `src/chase_detector.cpp` (~25 lines: 3 entries removed, comment block extended)
- `src/ff8_accessibility.h` (version bump)
- `CHANGELOG.md` (this entry)
- `DEVNOTES.md`, `NEXT_SESSION_PROMPT.md`

## v0.15.2.10

Add `domt1_1` to the chase-field set

v0.15.2.9 BAT (Fri 2026-05-08 20:40–20:58) revealed Aaron fought 4
battles during the chase return run. Two of them — in `domt1_1` at
20:52:15 and 20:53:33 — logged with `chaseActive=0` because
`domt1_1` was missing from `CHASE_FIELD_NAMES[]`. Per Aaron's
clarification "no random encounters during the chase scene," those
two battles were chase-robot fights that bypassed the kani pin
entirely. v0.15.2.10 fixes the omission.

### Implementation

**`chase_detector.cpp` (~5 lines):**
- Added `"domt1_1"` to `CHASE_FIELD_NAMES[]`.
- Reordered the array to ascending mountain-trail numbering for
  readability: `domt1_1 → domt2_1 → domt3_2 → domt4_1 → domt5_1`,
  then bridge/town fields.
- Comment trail extended with the v0.15.2.9 BAT findings that
  motivated the addition.

### What this does NOT fix

The `doopen2a` OTHERS-DIAG capture in v0.15.2.9 BAT was contaminated
by the field transition `doopen2a → dotown_3` firing during the 10s
window. kani's full state went `0xFF → 0x00` across many bytes from
engine teardown, inflating `director0` (144), `director1` (122),
`dog` (117), `jumptotown0` (111), and `g_hei2` (105) byte counts —
but these are deallocation artefacts, not active script behavior.
The data tells us nothing about chase-agent identity in `doopen2a`.

`domt5_1` — the field where v0.15.2.8 "Still getting up" was
reported — was NOT visited in this BAT, so the all-Others scanner
data for that field is still pending.

### BAT plan

1. Drive Squall through the chase scene starting from the comm tower.
2. Confirm `chaseActive=1` when battles fire in `domt1_1`
   (FREEZE ACTIVATED log line, KaniFreeze pin engaged).
3. **Specifically reach `domt5_1`** — that's the unsolved field.
4. The OTHERS-DIAG output for `domt5_1` is the next decision point.

### Risk

Zero. Adding a field name to a string array activates more chase
logic; v0.14.x and earlier behavior in non-chase contexts unchanged.
All v0.15.2.9 diagnostic infrastructure preserved unchanged.

### Files changed

- `src/chase_detector.cpp` (~20 lines: array entry + comment block)
- `src/ff8_accessibility.h` (version bump)
- `CHANGELOG.md` (this entry)
- `DEVNOTES.md`, `NEXT_SESSION_PROMPT.md`

## v0.15.2.9

All-Others diagnostic scanner

v0.15.2.8 BAT in `domt5_1` confirmed both kani (slot 8) AND battleyarou
(slot 10) are dormant: kani INITIAL has substantial state but FINAL
SUMMARY `changed_bytes=0/612`, battleyarou INITIAL is almost entirely
zero (just `+0x020=0x09`) and FINAL `0/612`. Yet battle still triggered
(count=3 at 20:21:32). Both pins working perfectly, neither entity is
the chase agent. v0.15.2.9 ships a diagnostic that scans EVERY Others
slot in the field to find what entity is actually running scripts during
the chase window.

### Implementation

**`chase_detector.h`/`cpp` (~20 lines):**
- New public functions `GetSymName(int idx)` and `GetSymNameCount()`.
  Read from the existing `s_symNames` cache populated by
  `FieldArchive::LoadSYMNames` in `OnDebouncedFieldChange`.

**`chase_kani_freeze.cpp` (~120 lines):**
- New `#include "field_archive.h"` for `FieldArchive::LoadJSMCounts`.
- New state: `MAX_OTHERS = 32`, `s_othersCountSnapshot`,
  `s_othersBaseSnapshot`, `s_othersStartSymIdx`,
  `s_othersInitial[32][612]` (~20KB).
- `StartCapture` (after kani+battleyarou capture): calls
  `LoadJSMCounts` for the current field, computes
  `othersStartSymIdx = doors+lines+bgs`, dereferences
  `pFieldStateOthers`, snapshots up to `min(others, MAX_OTHERS)` slots
  via SEH-guarded `memcpy`. Logs `OTHERS-DIAG snapshot taken`.
- `EndCapture` (after kani+battleyarou FINAL summaries): for each
  snapshotted slot, SEH-reads current state, computes
  `changed_bytes` count vs `s_othersInitial[slot]`, looks up sym name
  via `ChaseDetector::GetSymName(othersStartSymIdx + slot)`. Logs
  `OTHERS-DIAG slot=N symIdx=K sym='X' changed_bytes=K/612` for every
  slot, plus an overall `N/M slots had byte changes` summary line.
- Field-change deactivation, `Initialize`, `Shutdown` reset the new
  state vars. `Initialize` log line updated to v0.15.2.9 wording.

### What changes for the player

Nothing. The kani+battleyarou pins from v0.15.2.8 are unchanged.
`domt4_1` should still work identically. `domt5_1` will still trigger
the chase battle — we're collecting data, not fixing yet.

### Predicted BAT outcomes

- **ENTITY-DRIVEN-CASE:** one or two slots show high `changed_bytes`
  counts (50-200 bytes). Slot with most changes is the chase agent;
  v0.15.2.10 generalizes the pin to cover it.
- **NO-ENTITY-CASE:** all Others slots show `changed_bytes=0/612`. The
  chase battle is not entity-driven; it likely comes from a SETLINE
  trigger zone (`LineEvent=1` in `domt5_1`'s JSMScan). v0.15.2.10 would
  hook the SETLINE entity's Line script or an alternate battle-start
  function to intercept the trigger.
- **MIXED-CASE:** a few slots show small (1-10 byte) changes — likely
  heartbeat/idle counters. No standout slot. Same path as NO-ENTITY-CASE.

## v0.15.2.8

Dual-entity pin (kani + battleyarou)

v0.15.2.7 BAT in `domt5_1` produced FINAL SUMMARY `changed_bytes=0/612`
over the full 10-second window — zero byte changes anywhere in kani's
entity — but the chase battle still triggered (count=3 at 19:47:33).
MID-WINDOW heartbeat at t=5000ms confirmed `diff_this_tick=0
total_changed_so_far=0`. The resolved kani entity is dead code in this
field. Aaron clarified there are no random encounters during the chase
scene, so the battle MUST come from another entity.

### Hypothesis: `battleyarou` is the universal chase agent

The SYM and JSMScan output for `domt5_1` shows `ent14 cat=3 type=Interactive
Object sym='battleyarou' pos=no(0,0,0 tri=0) param=-1`. Translation:
"battle guy" in Japanese, runtime-driven (no static position), Interactive
Object (collision-eligible). Same 3-method JSM signature as kani in BOTH
`domt4_1` and `domt5_1`. `kani` and `battleyarou` may be a pair: `kani`
is the visible "down" model and `battleyarou` is the active collision
agent. v0.15.2.7 happened to work in `domt4_1` because some chain of
effects, but in `domt5_1` only `battleyarou` is alive.

### Strategy

Pin BOTH entities every frame. If either is the active chase agent in
any field, the pin catches it.

### Implementation

**`chase_detector` (h + cpp, ~90 lines):**
- Refactored `ResolveKaniLocation` into a generic
  `ResolveEntityLocation(targetSym, fieldName, logTag, outLoc)` helper.
  `ResolveKaniLocation` is now a one-line wrapper. New
  `ResolveBattleyarouLocation` is the parallel wrapper for `"battleyarou"`.
- New `ResolveLocPtr` static helper used by both `Get*EntityPtr()`
  functions to walk `pFieldStateBackgrounds`/`pFieldStateOthers` via the
  same JSM-counts arithmetic kani uses.
- New public functions: `GetBattleyarouEntityPtr()`,
  `IsBattleyarouEntityPtr()`, `GetBattleyarouLocation()`. Same
  signatures and semantics as the kani equivalents.
- New static `s_battleyarouLoc`, populated in `OnDebouncedFieldChange`
  alongside `s_kaniLoc` on every field transition. Reset in `Initialize`.

**`chase_kani_freeze.cpp` (~150 lines):**
- New state vars: `s_battleyarouPtr`, `s_battleyarouStrideBytes`,
  `s_battleyarouArrayKind`, `s_battleyarouInitial[612]`,
  `s_haveBattleyarouSnapshot`, `s_battleyarouSnapshot[612]`.
- `StartCapture`: after the existing kani INITIAL hex dump, resolves
  battleyarou via `GetBattleyarouEntityPtr`/`GetBattleyarouLocation`,
  captures INITIAL state via SEH-guarded `memcpy`, logs `BATTLEYAROU
  INITIAL snapshot` hex dump (`BYOU-INIT` label, 16 bytes per row). If
  battleyarou is absent (`symIdx<0`), logs single-line skip and continues
  — battleyarou pin is inert for that field, kani-only behavior identical
  to v0.15.2.7.
- `ApplyFreezePin`: NEW second SEH-guarded block after the existing kani
  pin block. Snapshots battleyarou at the same `t = SNAPSHOT_DELAY_MS
  = 1500ms` moment, `memcpy`s back every frame thereafter. NO
  belt-and-suspenders byte writes (kani's `0x21` and `0x14` magic values
  are kani-specific; applying them blindly to battleyarou could corrupt
  its state). Snapshot-only is safer.
- `EndCapture`: parallel BATTLEYAROU FINAL SUMMARY block —
  byte-by-byte comparison against `s_battleyarouInitial`, lists every
  changed byte with delta. Per-tick FIRST CHANGE diff is NOT done for
  battleyarou (would double the log volume); INITIAL/FINAL pair is
  enough.
- Field-change deactivation clears all new battleyarou state in
  addition to the v0.15.2.7 kani state.
- `Initialize` and `Shutdown` reset all new state vars. `Initialize` log
  line updated to v0.15.2.8 wording.

### Predicted BAT outcomes

- **GOOD:** in `domt5_1`, `BATTLEYAROU INITIAL` shows non-zero state,
  `BATTLEYAROU FINAL SUMMARY` shows few-to-zero changed bytes (pin held),
  no second chase battle, no audible kani movement. Hypothesis confirmed.
- **PARTIAL:** `BATTLEYAROU INITIAL` non-zero but `FINAL` shows many
  changes — our pin isn't winning the write race. Investigate hook
  ordering or alternate offsets.
- **WRONG-CANDIDATE:** `BATTLEYAROU INITIAL` is all zeros (battleyarou
  also dead in this field). Battle still triggers. v0.15.2.9 will try
  the next candidate (`plane1` Director, `dic`, `onkyou`, `gura`, or
  `saidotoujou` — all Interactive Objects in `domt5_1`'s JSMScan).

### Risk

Pinning battleyarou could break field functionality if battleyarou is
involved in scripted events outside the chase. Mitigation: the pin only
activates after a chase-field battle exit (StartCapture trigger), not on
initial field entry, and clears on field change. Pre-battle behavior
(any cutscene triggers) is unaffected.

## v0.15.2.7

Brute-force full-state pin per Aaron's design pivot

v0.15.2.6 BAT in `domt4_1` prevented kani collision (no battle) but
Aaron heard kani's running animation playing in place — the +0x150 anim
ID pin doesn't actually drive rendered animation. The engine reads other
unpinned bytes for playback. Aaron's design preference: "keep the robot
from getting up, rather than locking it in place when it does."

### Strategy

Drop the surgical pin sets. At t=1500ms post-`FREEZE ACTIVATED` (well past
Phase A re-init's last write at t=765ms, well before Phase C wakeup at
t=5375ms), snapshot kani's full post-header state region
(`+0x140`..stride). Then `memcpy` that snapshot back over every frame.
Every byte the engine would otherwise modify to drive wakeup — anim
playback drivers, AI state, position, collision flags, sub-state mirrors,
all the unidentified bytes — stays at "down and settled" forever.

Header bytes (`+0x000`..`+0x028`) are NOT pinned so the engine's
heartbeat/frame counter at `+0x028` still ticks. This preserves whatever
life-detection logic the engine uses while freezing all wakeup state.

For `domt5_1` (where the v0.15.2.6 BAT showed the resolved kani entity
had all-zero bytes), the brute-force pin will snapshot zeros and pin
zeros — effectively a no-op. If the chase battle still triggers there,
it confirms kani isn't the chase agent in that field.

### Subsumed and preserved

- v0.15.2.6's three-region position pin is subsumed (those regions are
  inside `+0x140`..stride and now pinned by the full snapshot).
- The five sub-state byte writes from v0.15.2.5 (`+0x150`/`+0x23F`/
  `+0x241`=0x21, `+0x154`/`+0x1FA`=0x14) are preserved as belt-and-
  suspenders during the `t < 1500ms` grace period. Once the snapshot
  kicks in, the memcpy overwrites them with the same values.

### Implementation

`src/chase_kani_freeze.cpp` (~120 lines):

1. Removed v0.15.2.6 position-pin state vars (`s_havePinnedPosition`,
   `s_pinnedPos_140/190/1B4`).
2. New state vars: `s_haveFullSnapshot` (bool), `s_fullSnapshot[612]`,
   `s_freezeStartTick` (DWORD).
3. New constants: `SNAPSHOT_DELAY_MS = 1500`, `SNAPSHOT_OFFSET_START =
   0x140`.
4. `StartCapture` sets `s_freezeStartTick = GetTickCount()` if
   `!s_haveFullSnapshot` (only on first activation per field).
5. `ApplyFreezePin`:
   - If `!s_haveFullSnapshot && elapsed >= SNAPSHOT_DELAY_MS`: SEH-guarded
     `memcpy` from `kani+0x140` (length = `stride - 0x140` bytes) into
     `s_fullSnapshot`, set flag, log.
   - If `s_haveFullSnapshot`: `memcpy` from `s_fullSnapshot` to
     `kani+0x140`.
6. Field-change deactivation clears `s_haveFullSnapshot` AND
   `s_freezeStartTick` so the next chase field gets a fresh post-Phase-A
   snapshot.
7. `Initialize`/`Shutdown` reset the new state.
8. `FREEZE ACTIVATED` and `Initialize` log lines updated.
9. Header comment trail extended with v0.15.2.7 design pivot rationale.

### Risk

Pinning ~292 bytes including unidentified ones could trip unexpected
engine behavior. Mitigation: header (heartbeat) excluded; if
engine-life-detection logic uses post-header bytes too, kani might
appear "hung" to the engine — which is what we want anyway, since he
should be incapacitated for the entire chase scene. The chase exits via
the Lapin Beach FMV which is position-independent, so a frozen kani can't
break progression.

### Predicted outcomes for next BAT

- **GOOD:** no battle, no audible kani movement in `domt4_1`. FINAL
  SUMMARY shows only `+0x028` heartbeat changed (and possibly a couple
  of header-byte transients).
- **PARTIAL:** no battle but some other audible artifact (e.g., a
  one-shot sound triggered before our snapshot kicks in, or a sound
  played from a separate audio source not gated by entity state).
- **FAILED:** battle still triggers — means kani isn't the chase entity
  in this field. For `domt5_1` especially, expect this; the all-zero
  snapshot tells us the resolved entity is dead. Next investigation:
  identify the actual chase entity (likely `battleyarou`, which has the
  same 3-method signature as kani in both fields).

## v0.15.2.6

Layer position pin on top of v0.15.2.5's full sub-state pin

v0.15.2.5 BAT in `domt4_1` proved the full sub-state pin held perfectly
but kani still triggered battles #2 and #3. Diagnosis: kani has TWO
independent wakeup paths, and the AI/movement subsystem doesn't read
any of the bytes we've pinned so far.

### v0.15.2.5 BAT findings

- **Pin held:** all five pinned bytes (`+0x150`, `+0x154`, `+0x1FA`,
  `+0x23F`, `+0x241`) ABSENT from FINAL SUMMARY in capture #1. v0.15.2.5
  had 17 changed bytes vs v0.15.2.4's 19 — the two-byte difference is
  exactly `+0x154` and `+0x1FA`, the new pins this build added.
- **Engine never decremented `+0x154`:** capture #2 INITIAL `+0x154` =
  `0x14` (vs v0.15.2.4 BAT's `0x0C`). The pin successfully prevented the
  cross-battle persistence we saw last time.
- **Phase C still happened.** Position bytes `+0x140-+0x148`,
  `+0x190-+0x199`, `+0x1B5-+0x1BD` drift at t=~5400ms regardless of
  whether `+0x154` is pinned. Battle #2 fired at 18:53:43 (7s into
  capture #1, count=2). Battle #3 at 18:54:53 (count=3).

### Diagnosis: timer-driven AI is decoupled from sub-state

Kani has at least two parallel wakeup mechanisms:

1. Sub-state countdown via `+0x154` / `+0x1FA` — controls some
   animation transitions. Pinned by v0.15.2.5.
2. AI/movement subsystem — timer-driven, fires at Phase C onset
   regardless of sub-state value. Moves kani's coordinates toward Squall.
   Collision detection on those coordinates triggers a new battle.

The AI doesn't gate movement on `+0x154`. So pinning `+0x154` doesn't
stop movement.

### v0.15.2.6 changes

`src/chase_kani_freeze.cpp` (~50 lines):

1. New state vars `s_havePinnedPosition` (bool) and three 12-byte
   buffers `s_pinnedPos_140`, `s_pinnedPos_190`, `s_pinnedPos_1B4`.
2. `StartCapture` snapshots three position regions on FIRST freeze
   activation per field: `+0x140-+0x14B` (X/Y/Z dwords),
   `+0x190-+0x19B` (second copy), `+0x1B4-+0x1BF` (third copy).
   SEH-guarded read with diagnostic log line on success and on read
   failure. Subsequent captures in the same field do NOT re-snapshot.
3. `ApplyFreezePin` writes the three buffers back via `memcpy` every
   frame after the existing five byte writes, gated on
   `s_havePinnedPosition`.
4. Field-change deactivation in `ApplyFreezePin` clears
   `s_havePinnedPosition` so the next chase field gets a fresh
   snapshot.
5. `Initialize` and `Shutdown` reset the new state.
6. `FREEZE ACTIVATED` and `Initialize` log lines updated to mention
   the position pin.

Why snapshot ONLY on first activation: capture #2 INITIAL position is
kani's chase-end position from prior battle. We want capture #1's
INITIAL spawn position (-995, 3562, 230 in `domt4_1`).

### Risk

Pinning kani's coordinates may produce a visual rendering glitch (kani
standing still in the chase scene). Functionally safe — the chase scene
ends via the Lapin Beach FMV, which is position-independent (scripted
event on Squall reaching the field exit). Aaron is blind so the visual
glitch doesn't affect UX.

### Predicted outcomes for next BAT

- **GOOD:** all eight pinned regions ABSENT from FINAL SUMMARY (or
  position bytes appear with `delta=0` because we wrote back same value).
  Kani stays at spawn, no second/third battle. Push v0.15.2.6.
- **PARTIAL:** position pin holds but other AI state (e.g., velocity,
  collision flag, target reference) drives a battle some other way.
  Diagnostic FINAL SUMMARY will show what new bytes the engine touched.
- **FAILED-PIN:** position bytes STILL change in FINAL despite memcpy.
  Engine is winning the write race or writing to a different memory
  location for the rendered position.

## v0.15.2.5

Layer +0x154 and +0x1FA sub-state pins on top of v0.15.2.4's anim-ID trio

v0.15.2.4 BAT in `domt4_1` had a partial-success outcome. The pin held
perfectly on the three animation-ID bytes — across two consecutive
captures the FINAL SUMMARY confirmed `+0x150`, `+0x23F`, and `+0x241`
were never modified — but kani still woke up and triggered battles #2
and #3 in rapid succession.

### Diagnosis: the anim-ID trio is purely a RENDERING pin

The `+0x150` byte controls which pose the model displays. Pinning it
makes kani render as "down" but does not affect the engine's AI or
movement logic. The actual wakeup is driven by:

- `+0x154` (dword LSB): sub-state countdown, `0x14` → `0x0C`
- `+0x1FA` (byte): sub-state mirror, `0x14` → `0x0C`

The engine decrements the `+0x154` LSB from `0x14` over ~5 seconds.
Once it crosses some threshold the AI starts moving kani toward
Squall, and collision detection triggers a new battle regardless of
what the rendered pose is.

### Cross-battle persistence

Capture #2 INITIAL `+0x154` = `0x0C`. The engine PERSISTED the
post-wakeup sub-state value across the battle/field reload — only
`+0x150` was reset to `0x21` by the engine's normal init pass. That's
why each successive battle in the v0.15.2.4 BAT woke up faster than
the last: the countdown never had a chance to reset.

### v0.15.2.5 changes

`src/chase_kani_freeze.cpp` (~25 lines):

1. `ApplyFreezePin()` now writes `0x14` to bytes `+0x154` and `+0x1FA`
   every frame in addition to the existing `0x21` writes to `+0x150`,
   `+0x23F`, `+0x241`.
2. Header comment trail and inline pin-section comment updated with
   v0.15.2.4 BAT findings and v0.15.2.5 rationale.
3. `FREEZE ACTIVATED` log line updated to mention all five pinned bytes.
4. `Initialize` log line updated to v0.15.2.5.

Byte-level writes are correct here — the `+0x154` dword's upper three
bytes were already `0` in INITIAL, and the FIRST CHANGE log only ever
showed the LSB changing.

### Predicted outcomes for next BAT

- **GOOD:** All five pinned bytes ABSENT from FINAL SUMMARY, kani stays
  down, no second/third battle. Push v0.15.2.5 to GitHub.
- **PARTIAL:** Sub-state pinned (absent from FINAL) but position bytes
  `+0x140-+0x148`, `+0x190-+0x199`, `+0x1B5-+0x1BD` still drift. AI
  is moving kani's coordinates despite the full sub-state pin. v0.15.2.6
  layers on a position pin using the INITIAL position as the target.
- **FAILED-PIN:** `+0x154` STILL changes despite the pin. Engine wins
  the write race. Need to investigate timing or hook a different write
  site.

## v0.15.2.4

Add per-frame FREEZE on top of the v0.15.2.3.1 diagnostic capture

v0.15.2.3.1 BAT in `domt4_1` (capture started 18:00:11 on `mode 4->1`,
ran the full 10s window) revealed a clean three-phase pattern matching
Aaron's UX description.

### Three-phase wakeup pattern

- **Phase A (0–700ms):** field re-init burst as kani spawns into the
  field after battle. Many bytes update once.
- **Phase B (700–5300ms, ~4.6s):** QUIET PERIOD — kani is visibly on
  the ground. The mid-window heartbeat at t=5000ms shows 18 bytes
  changed total, only 1 diff that tick.
- **Phase C (5300ms onward):** wakeup burst. Animation flip, position
  fields start updating rapidly. Battle #2 fired at 18:00:18 — only
  7 seconds after capture start — confirming kani fully woke up and
  caught Squall inside our window.

### Wakeup-control bytes (Phase C onset, t=5593–5640ms)

Five bytes flip in a tight 50ms window and stay at the new value
through end-of-capture (per FINAL SUMMARY):

| Offset | Initial | Final | Pattern |
|--------|---------|-------|---------|
| +0x150 (dword) | 0x00000021 | 0x00000011 | animation ID, "down" → "running" |
| +0x154 (dword) | 0x00000014 | 0x0000000C | paired sub-state |
| +0x23F (byte) | 0x21 | 0x11 | anim shadow register A |
| +0x241 (byte) | 0x21 | 0x11 | anim shadow register B |
| +0x1FA (byte) | 0x14 | 0x0C | sub-state mirror |

Position bytes confirm the wakeup is real: kani moved from world
position (-995, 3562, 230) to (-548, 2615, 231) — about 947 units
toward Squall during Phase C.

### v0.15.2.4 changes

`src/chase_kani_freeze.cpp` (~50 lines):

1. Header comment block updated with v0.15.2.3.1 → v0.15.2.4 trail.
2. New state vars `s_freezeActive` and `s_freezeFieldName[64]`.
3. `StartCapture` now sets `s_freezeActive = true` and saves
   `ChaseDetector::GetDebouncedFieldName()` into `s_freezeFieldName`
   as the freeze-anchor.
4. New helper `ApplyFreezePin()` called every `Update()` after the
   capture-trigger logic:
   - If `!s_freezeActive` → no-op.
   - If current debounced field name is non-empty AND different from
     `s_freezeFieldName` → deactivate freeze and log.
   - Otherwise re-resolve kani via `ChaseDetector::GetKaniEntityPtr()`
     each frame and SEH-write `0x21` to bytes at +0x150, +0x23F, +0x241.
   - Empty-string field name during the 2s name-debounce after a
     `fieldId` flip does NOT deactivate (we don't yet know the
     destination field).
5. `Initialize`/`Shutdown` clear the new state.

### Conservative first attempt

Only the three animation-ID bytes are pinned in this build. The
+0x154 dword and +0x1FA byte sub-state mirrors are left free — if
the next BAT shows kani still wakes up, v0.15.2.5 layers them on.

The diagnostic capture continues to run alongside the freeze so the
next BAT's FINAL SUMMARY can verify pin compliance: +0x150, +0x23F,
+0x241 should be ABSENT from the changed-bytes list if the freeze
is working. If kani still moves and triggers a second battle despite
the pin, the diagnostic will show what other bytes the engine wrote
at the wakeup moment to drive it.

## v0.15.2.3.1

Fix v0.15.2.3 capture trigger — fired during the wrong game-mode window

v0.15.2.3 BAT (`domt4_1`, `count=1` chase-mode kani battle ending at
17:38:31) showed the capture started cleanly on the mode 3->non-3
edge but logged ZERO byte changes across the entire 5-second window.

### Root cause

The post-battle game-mode sequence is:

- mode 3 (battle)
- mode 5 (fade-to-field transition, ~6 seconds, **engine pauses ALL
  entity updates** — the entity blocks are frozen)
- mode 1 (active field, entity state machines resume)

v0.15.2.3 triggered on `prev==3 && cur!=3`, which fires the moment
battle ends — i.e., entry into mode 5. The capture window therefore
landed entirely in the dead transition phase and saw no activity.

Evidence in the BAT log:

- 17:38:31 — capture started (mode 3->5)
- 17:38:36 — capture complete, 0 changes
- 17:38:37 — `FieldNavigation: [PSHM_W-HOOK] Init done` (field
  re-initializing, mode 5->1 transition completing) — **one second
  after our window closed**
- 17:38:44 — second kani battle (`count=2`)

The wakeup completed within ~7 seconds of mode 1 starting, entirely
outside our v0.15.2.3 capture window.

### Fix

`src/chase_kani_freeze.cpp`:

1. New `MODE_FIELD_VAL = 1` constant.
2. New `s_battleSeenRecently` flag, set true when game mode reads
   `MODE_BATTLE_VAL = 3`, cleared after a capture starts.
3. Trigger condition rewritten: capture fires on the first frame of
   `MODE_FIELD_VAL` with `s_battleSeenRecently` true, i.e., the
   moment the engine returns to active field mode following any
   battle. Catches `3->5->1`, `3->1`, `3->4->1` etc. uniformly.
   Re-arms after each capture so subsequent kani battles in the
   same chase session each get their own capture.
4. `CAPTURE_DURATION_MS` bumped 5000 -> 10000 ms.
5. `MID_SUMMARY_AT_MS` bumped 2500 -> 5000 ms.

### Same scope as v0.15.2.3

Diagnostic only. v0.15.2.4 still installs the actual freeze hook
based on the (now correctly-captured) wakeup-byte data.

### Files

- `src/chase_kani_freeze.cpp` (~40 lines)
- `src/ff8_accessibility.h` (this version)
- `CHANGELOG.md` (this entry)
- `DEVNOTES.md`, `NEXT_SESSION_PROMPT.md` updated

## v0.15.2.3

DIAGNOSTIC build for chase robot freeze redesign — capture kani's wakeup byte

v0.15.2.2 BAT (domt2_1, kani battle exit at 16:53:57) proved the v0.15.1
`ChaseBattleFreeze` approach is not viable in two of the chase fields.
`ChaseDetector` saw both battles via game-mode 1→3 polling (count=1 then
count=2), but the field log shows ZERO `ChaseBattleFreeze: NO-OP` or
`ChaseBattleFreeze: pass-through` lines. `opcode_battle` is dead code for
these fields — they enter battle via a different path (random-encounter
step counter or scripted encounter). The v0.15.2.1 robot fix worked in
domt4_1 only because that's the one chase field where the kani battle
goes through `opcode_battle`.

### Design pivot — hook the wakeup, not the battle

Aaron clarified the desired UX: post-battle, when the field re-appears,
kani is visibly on the ground for several seconds before the engine's
wakeup animation/AI-state runs and the robot stands back up to resume
chasing. The cleaner design is to hook that wakeup transition — which
is the SAME state machine across every chase field — and pin kani at
"incapacitated" until the field changes, instead of trying to intercept
battles at all.

This sidesteps the entire domt2_1/domt5_1 battle-entry mystery. It also
means the robot stays down for the duration of the party being on that
field (the goal Aaron stated), with natural reset on field unload
because entity state is reinitialized when a new field loads.

### v0.15.2.3 ships a diagnostic

New module `src/chase_kani_freeze.{h,cpp}` (~330 lines). On every
game-mode 3→non-3 transition (battle exit) while `ChaseDetector` reports
we're in a chase field with kani's entity address resolved, snapshot
kani's full entity block (stride `0x264` for Others, `0x1B4` for
Backgrounds) and continuously diff for 5 seconds.

Log output:

- `KaniFreeze: ===== CAPTURE STARTED =====` plus trigger context
- `KaniFreeze: INITIAL +0x000:` — full entity block hex dump (~32 lines)
- Per-byte first-change events:
  `KaniFreeze: t=1234ms tick=42 +0x07A: FIRST CHANGE 0x00 -> 0x01`
- `KaniFreeze: MID-WINDOW heartbeat t=2500ms ...` — visibility pulse
- `KaniFreeze: FINAL SUMMARY ...` — every changed byte with
  initial-vs-final values and signed delta
- `KaniFreeze: ===== CAPTURE COMPLETE =====`

The expected pattern in the BAT log: kani is on the ground for several
seconds = mostly-quiet log with maybe one byte (an animation tick or
AI-state counter) incrementing slowly, then a sharp inflection where
ONE byte (or small word/dword) flips at the moment kani stands up, then
a flood of position-byte changes per tick as kani begins moving.

v0.15.2.4 will pin the inflection byte at its initial value, freezing
kani in the on-ground state until field exit.

### Other changes

- `src/chase_battle_freeze.cpp` removed from build (`deploy.bat` compile
  line replaced with `chase_kani_freeze.cpp`). The `.h` and `.cpp`
  source files remain on disk as orphans — v0.15.2.4 cleanup may delete
  them after the new approach is BAT-validated.
- `src/dinput8.cpp`: include swapped `chase_battle_freeze.h` →
  `chase_kani_freeze.h`, `Initialize`/`Shutdown` calls updated, new
  `ChaseKaniFreeze::Update()` call added to per-tick chain (after
  `ChaseAskOverlay::Update()` so `ChaseDetector`'s kani address has
  been refreshed for the same tick).

### Preserved

- v0.15.1.2 timing fix (3-second deferred `OpenAsk` so Squall's
  chase-trigger line plays first)
- v0.15.2.1 `MODE_BATTLE_VAL=3` fix in `chase_detector.cpp` —
  unrelated to wakeup, still needed for `ChaseDetector`'s per-field
  battle counter and for `ChaseKaniFreeze`'s battle-exit edge detection
- v0.15.2.2 `chase_ask_overlay` TTS+keyboard-only path

### Files

- `src/chase_kani_freeze.h` (NEW, ~30 lines)
- `src/chase_kani_freeze.cpp` (NEW, ~300 lines)
- `src/dinput8.cpp` (~10 lines: include + Initialize/Update/Shutdown wiring)
- `src/deploy.bat` (1 line: compile-list filename swap)
- `src/ff8_accessibility.h` (this version)
- `CHANGELOG.md` (this entry)
- `DEVNOTES.md`, `NEXT_SESSION_PROMPT.md` updated

## v0.15.2.2

Drop engine-rendered chase ASK proxy slot (TTS+keyboard only)

v0.15.2.1 BAT shipped two diagnostic findings that close the
engine-ASK chapter for v0.15.x.

### Finding 1 — the engine doesn't render externally-populated slots

The `LogProxySlotSnapshot` polling logged our proxy slot's state
every second for 13 seconds after a fully-correct
`PopulateProxySlot` (captured-from-real-ASK template values:
`state=0x0D`, `mode1=0x1000`, `trans=0x1000`, `geom=[0x50, 0x0A,
0xCC, 0x5D]`, `t1=t2=non-NULL`, `firstQ=1 lastQ=2 curQ` tracking,
`cb1=cb2=NULL`).

Every single SLOT-SNAP from `[22:27:21]` through `[22:27:34]`
showed our writes preserved EXACTLY. The engine never modified
our slot — it just IGNORED slot 1 in its render pass for the
entire 13 seconds the ASK was 'open'. At `[22:27:34]` the kani
collision triggered a battle, and the field-to-battle transition
wiped the slot to defaults (`geom=[0x40, 0x40, 0x80, 0x80]`,
`t1=t2=NULL`, `state=0`, `firstQ=lastQ=0xFF`).

This rules out every 'we're missing a field' hypothesis. Conclusion:
the engine's render loop doesn't iterate `pWindowsArray` looking
for `state=0x0D` dialogs. **Render is bound to script-VM context—only slots that an active script has parked on via `opcode_ask` /
`opcode_aask` get rendered.** Replicating that binding from a DLL
hook would require disassembly work to locate the script-VM's
'current dialog slot' reference — a v0.15.3+ investigation,
outside v0.15.2.x's scope.

v0.15.2.x ships with the chase ASK as TTS+keyboard only. **The
engine-rendered visual is an open feature gap, not a closed item.**
It remains on the backlog to revisit once the script-VM binding
mechanism is understood, or once a different rendering path
(e.g. hooking the renderer directly) becomes tractable. The five
iterations (v0.15.0–0.15.2.1) of engine-ASK debugging produced
solid documentation of the `ff8_win_obj` layout and confirmed the
script-VM binding constraint; that investment isn't lost. The
decision to defer is about engineering tractability for the
v0.15.x line, not about declaring the audio-only path
sufficient on its own.

### Finding 2 — the v0.15.2.1 robot fix WORKS in domt4_1

Field log shows:

```
[22:29:57] ChaseBattleFreeze: NO-OP kani BATTLE in 'domt4_1' (battleCount=1, freeze#1) — returning 3
[22:30:04] ChaseBattleFreeze: NO-OP kani BATTLE in 'domt4_1' (battleCount=1, freeze#2) — returning 3
```

These NO-OPs are the 'interruption / delay' Aaron heard between
battles. The freeze gate fires correctly: first kani contact in
domt4_1 plays out (count goes 0 → 1), subsequent contacts NO-OP
(opcode_battle returns advance-code 3 without calling original).

The kani's bounce-back animation runs on its own AI timeline
independent of `opcode_battle`, so the entity still 'gets up'
visually — a sighted player would see the kani animate even
though the battle screen was suppressed. That's a separate issue
for v0.15.3 (suppress the wakeup animation, or hook a
lower-level battle-entry function that catches both code paths).

### Finding 3 — ChaseBattleFreeze doesn't fire in domt5_1 (deferred)

Log shows two domt5_1 battles at `[22:32:43]` and `[22:34:17]`,
both detected by `ChaseDetector::PollGameMode` (mode 1 → 3) but
zero `pass-through` or `NO-OP` log lines from `ChaseBattleFreeze`.
`opcode_battle` isn't being called for those battles. They use
a different code path — likely random-encounter step counter or
scripted encounter, not kani-collision-triggered `opcode_battle`.
Deferred to v0.15.3 investigation. Possible v0.15.3 fixes:
hook the lower-level battle-entry function (the one actually
triggering mode 1 → 3) instead of `opcode_battle`.

### v0.15.2.2 changes

One function modified: `chase_ask_overlay.cpp::OpenAsk`. Replace
the `FindFreeWindowSlot` + `PopulateProxySlot` block with
`s_proxySlotIdx = -1` plus a one-line log and a 30-line comment
explaining the rationale. Helper functions remain in source as
unreferenced statics (MSVC C4505 is off at default `/W3`,
compiles clean) so a future version can re-enable proxy-slot
rendering if the script-VM binding mechanism is ever discovered:

- `FindFreeWindowSlot`
- `PopulateProxySlot`
- `ReleaseProxySlot`
- `LogProxySlotSnapshot`
- `SyncEngineCursor`

All downstream paths gated on `s_proxySlotIdx >= 0` are now
inert: the SLOT-SNAP polling in `Update()` early-returns inside
`LogProxySlotSnapshot`; `SyncEngineCursor` early-returns;
`ReleaseProxySlot` in `CloseAsk` early-returns. No log spam, no
behavior change in the TTS+keyboard path.

### What v0.15.2.2 BAT will verify

1. Chase trigger MES `"Forget it!  Let's go!"` plays via NVDA.
2. After the 3-second deferred-open delay, the chase ASK prompt
   is spoken (TTS-only — no engine dialog box, that's intended).
3. Up/Down toggles between Auto-drive and Manual with TTS
   announcements, Enter commits, 1/2 number-key shortcuts work.
4. Selection persists to `ff8_accessibility.ini` under `[Chase]`.
5. First kani contact in domt4_1 fires a normal battle. Subsequent
   contacts in same field NO-OP (audible as 'no battle music
   transition, kani bounces off').
6. Field log no longer shows the 1Hz SLOT-SNAP spam.
7. Lapin Beach FMV ends the chase cleanly; INI choice survives.

### v0.15.1.2 timing fix preserved

OnDialogText still defers OpenAsk by 3 seconds via
`s_triggerPending` / `s_triggerTimestamp` so Squall's line plays
first. Update() polls per-tick and calls OpenAsk when delay
elapses.

### v0.15.2.1 robot fix preserved

`MODE_BATTLE_VAL = 3` in `chase_detector.cpp` still applies. Edge
detection on game-mode 1 → 3 increments `s_currentFieldBattleCount`
on each kani-triggered battle in chase fields.

### Files changed

- `src/chase_ask_overlay.cpp` — ~30 lines: OpenAsk proxy
  allocation block replaced with disable + 30-line rationale
  comment.
- `src/ff8_accessibility.h` — version bumped 0.15.2.1 → 0.15.2.2,
  comment trail extended.
- `CHANGELOG.md` — this entry.
- `DEVNOTES.md`, `NEXT_SESSION_PROMPT.md` — ready-to-BAT state,
  v0.15.3 backlog updated with the domt5_1 finding.

## v0.15.2.1

Fix robot getting up after battle, plus diagnostic snapshot for the engine ASK rendering issue

v0.15.2 BAT surfaced two issues:

1. **Robot still gets up after a brief pause after battle.** ChaseBattleFreeze
   was passing through every kani battle even after the first.
2. **Engine-rendered ASK dialog still doesn't appear.** The proxy slot
   was successfully populated (`PopulateProxySlot` returned true,
   `ASK opened (proxySlot=1)`) but no dialog box rendered visually.

### Fix #1 — ROBOT (high-confidence one-line fix)

`chase_detector.cpp` had `MODE_BATTLE_VAL = 999`. v0.15.2 BAT showed
the game mode goes to `3` during chase battles and stays there for
95 seconds (with Squall's field-position frozen, classic 'Squall is
on the battle screen' pattern). Battle mode in this Steam 2013 build
is `3` at the field-mode polling resolution — not 999.

The edge detection `prev != 999 && cur == 999` never fired, so
`s_currentFieldBattleCount` stayed at 0 across kani contacts. The
freeze gate's `count >= 1` condition was never satisfied. Result:
every kani battle passed through, including the second-and-later
battles we wanted to NO-OP.

Fix: `MODE_BATTLE_VAL = 3`. Edge detection fires on first kani
battle (count goes to 1), freeze gate triggers on second
(`NO-OP kani BATTLE`).

Field log will now show:

```
ChaseDetector: battle entered (game-mode 0x0001 -> 0x0003); field='domt4_1' chaseActive=1 count=1
[… first battle plays out …]
ChaseBattleFreeze: NO-OP kani BATTLE  (mode=manual inChase=1 kaniCaller=1 count=1)
```

### Fix #2 — ASK rendering: geometry writes + diagnostic snapshot

v0.15.2 BAT log shows `proxy slot 1 populated v0.15.2 (textBuf=0x6DCA3B10,
mode1=0x1000, state=0x0000000D, trans=0x1000, firstQ=1, lastQ=2,
curQ=2, callbacks=NULL)` — our writes succeeded. But the engine
didn't render anything.

Looking at the captured slot[0] from a real engine ASK earlier:
bytes 0x00–0x07 contained `50 00 0A 00 CC 00 5D 00` (4 × uint16 LE
= 0x0050, 0x000A, 0x00CC, 0x005D — likely x, y, w, h or similar
dialog box geometry). v0.15.2 left those zero, which may have caused
the engine to cull the slot from the render pass (zero-sized
window).

**v0.15.2.1 changes:**

1. New `WIN_OBJ_GEOM0_OFFSET = 0x00` constant and
   `TEMPLATE_GEOM[4] = { 0x0050, 0x000A, 0x00CC, 0x005D }` —
   captured values written by `PopulateProxySlot`.
2. New `LogProxySlotSnapshot(label)` diagnostic helper. Dumps the
   full state of our proxy slot to the field log: geometry, both
   text pointers, win_id, mode1, trans, state, firstQ/lastQ/curQ,
   both callbacks. Called once right after `PopulateProxySlot`
   (label `"post-populate"`) and again every 1 second while the
   ASK is open (label `"while-open"`).

The snapshot data tells us which scenario we're in:
- **State stays at 0x0D over time** → engine ignores our slot
  entirely. Our slot is missing something needed to be a render
  target (perhaps slot 0 is special, or the script VM needs to
  be parked on this slot).
- **State resets to 0 or fields get rewritten** → engine touches
  the slot but invalidates it (bad fields).
- **Engine writes new values to fields we didn't set** → we'd
  see them in the snapshot, learn the missing pieces.

### What v0.15.2.1 BAT will verify

1. **Robot fix.** Second kani battle in same chase field NO-OPs.
   Field log shows `count=1` after first battle and the freeze
   line on second contact.
2. **Engine ASK dialog box appears** (if geometry fix is the
   issue) OR snapshot data narrows down what's missing.

### Files changed

- `src/chase_detector.cpp` — ~10 lines: `MODE_BATTLE_VAL` constant
  changed to 3, with comment explaining why.
- `src/chase_ask_overlay.cpp` — ~80 lines:
  `WIN_OBJ_GEOM0_OFFSET` constant, `TEMPLATE_GEOM[4]` constant,
  geometry writes in `PopulateProxySlot`, log line update,
  `LogProxySlotSnapshot` helper, post-populate snapshot call in
  `OpenAsk`, while-open snapshot call in `Update`,
  `s_lastProxySnapshotTick` state var.
- `src/ff8_accessibility.h` — version bumped, comment trail
  extended.
- `CHANGELOG.md` — this entry.
- `DEVNOTES.md`, `NEXT_SESSION_PROMPT.md` — ready-to-BAT state.

## v0.15.2

Re-enable engine-rendered chase ASK dialog using captured template values

v0.15.1.2 BAT included a quick chase-diag ASK snapshot capture. Aaron
triggered an in-game 3-option ASK with chase-diag (F12) enabled, and
`OnAskOpcodeFired` dumped the full state of slot[0]. The captured
values let us re-enable the engine-rendered window that v0.15.1.1
had to disable.

### What we learned from the snapshot

```
slot[0] when AASK is active:
  state    = 0x0000000D    (was guessing 0x07)
  mode1    = 0x00001000     (was guessing 0x0005)
  trans    = 0x00001000     (open_close_transition; was 0)
  cb1      = 0x00000000     NULL!
  cb2      = 0x00000000     NULL!
  firstQ=2, lastQ=4, curQ=2 (3-option ASK in capture; ours is 1/2/1)
  t1, t2   = both populated, ~75 bytes apart in same buffer
```

**v0.15.1's lockup root cause is now confirmed.** The engine doesn't
use the `+0x34` / `+0x38` callbacks for ASK windows at all — they're
NULL in real ASKs. v0.15.1's mistake was setting them to function
pointers, which the engine apparently interpreted as some other
dispatch target (per-frame render tick?) and invoked at ~180 Hz.

### What v0.15.2 does

1. **Updates `chase_ask_overlay.cpp` template constants** to match
   captured values:
   - `TEMPLATE_MODE1 = 0x1000` (was 0x0005)
   - `TEMPLATE_STATE = 0x0000000D` (was 0x07)
   - new `TEMPLATE_TRANS = 0x1000` constant
2. **Updates `PopulateProxySlot`** to write the captured values:
   - `text2 = textBuf` (was nullptr; real ASKs have both populated)
   - `open_close_transition = 0x1000` (was 0; that probably hid the
     window even after state was set to active)
   - **callbacks set to 0** (was function pointers — the bug)
3. **Re-enables proxy-slot allocation in `OpenAsk()`** (undoes
   v0.15.1.1's `s_proxySlotIdx = -1` hardcode).
4. **Adds `SyncEngineCursor()`** — PollKeys writes
   `s_currentHighlight + 1` to the engine's `curQ` field after each
   Up/Down keypress, so the engine-rendered cursor visually tracks
   the selection. Hybrid input: engine renders, our PollKeys still
   owns the input mechanism. On commit, `CommitChoice` → `CloseAsk`
   → `ReleaseProxySlot` releases the slot via `state = 0`.

v0.15.1.2's timing fix is preserved unchanged: `OnDialogText` still
defers `OpenAsk` by 3 seconds via `s_triggerPending` /
`s_triggerTimestamp` so Squall's `"Forget it!  Let's go!"` plays
through first.

### What v0.15.2 BAT will verify

1. Aaron hears Squall's chase-trigger line first.
2. Chase-mode ASK appears as **engine-rendered window** AFTER the
   3-second delay. Field log shows `proxy slot %d populated v0.15.2
   (textBuf=..., mode1=0x1000, state=0x0000000D, trans=0x1000,
   firstQ=1, lastQ=2, curQ=2, callbacks=NULL)`.
3. Up/Down/Enter and 1/2 still work correctly with TTS
   announcements.
4. Engine cursor visually tracks selection (sighted spectators
   only — not strictly necessary since Aaron is blind).
5. `chase_battle_freeze` still caps battles at one per chase field.
6. Lapin Beach FMV ends chase cleanly. ASK once-per-chase flag
   clears.
7. **No callback spam.** Field log should NOT contain repeating
   `ConfirmCallback fired` / `CancelCallback fired` lines (those
   functions remain in source as unreferenced statics but are no
   longer assigned to slot fields).

### Risks

- **Engine input race.** If the engine ALSO consumes our key
  events (because it sees `state=0x0D` as 'active ASK awaiting
  input'), our PollKeys handler may double-fire. Symptom: Up/Down
  counting twice per press in the field log. If observed, v0.15.2.1
  will need to either suppress engine input on our slot or
  detect-and-ignore.
- **Encoded text rendering.** Our `EncodeChar` table is best-effort
  for FF8 text encoding. The rendered window may show garbled text
  on screen. This doesn't affect Aaron's experience (TTS speaks
  the right thing); refining the table is a v0.15.3+ polish item.

### Known minor issue (defer to v0.15.3)

The `chase_diag` ASK snapshot's `+10:` hex dump row showed only
`"000"` instead of the full 16 bytes — `snprintf` size-tracking
bug in `chase_diag.cpp` where `p2`/`p3` accumulators go negative
when `row3` overflows, causing UB in subsequent calls. The
high-level fields all captured cleanly so this didn't block
v0.15.2.

### Files changed

- `src/chase_ask_overlay.cpp` — ~50 lines: constants update,
  `PopulateProxySlot` rewrite, `OpenAsk` re-enable, new
  `SyncEngineCursor` helper, PollKeys integration.
- `src/ff8_accessibility.h` — version bumped, comment trail
  extended.
- `CHANGELOG.md` — this entry.
- `DEVNOTES.md`, `NEXT_SESSION_PROMPT.md` — ready-to-BAT state.

## v0.15.1.2

Fix chase ASK timing so Squall's chase-trigger line plays first

v0.15.1.1 fixed the lockup but introduced a timing problem: Aaron heard
the chase-mode prompt but never heard Squall's `"Forget it!  Let's go!"`.

### Root cause

`ChaseAskOverlay::OnDialogText` was called synchronously from inside
`field_dialog`'s `Hook_show_dialog`, BEFORE field_dialog's own NVDA
speak path got to announce Squall's line. Our `SpeakInitialPrompt`
called `ScreenReader::Speak(…, /*interrupt=*/true)`, which clobbered
the SAPI queue. Aaron then advanced the dialog past Squall's line
while picking an option from our prompt.

### The fix

Defer the open by 3 seconds. `OnDialogText` no longer calls `OpenAsk`
synchronously; it sets `s_triggerPending = true` and stores
`s_triggerTimestamp = GetTickCount() + TRIGGER_DELAY_MS` (3000 ms).
`Update()` polls per-tick and calls `OpenAsk` once the delay elapses,
with a re-check of `IsInChaseField()` and `!s_askFiredThisChase` so
the ASK doesn't open if the chase ended during the delay window.

3 seconds covers a 5-word line at any reasonable TTS rate. The
default NVDA pace renders `"Forget it!  Let's go!"` in roughly 2
seconds; the extra second is buffer for slower rates and the dialog
box's letter-by-letter display.

### Engine-rendered ASK — still deferred to v0.15.2

Aaron also reported the ASK fired as TTS only — no in-game dialog
box. v0.15.1.1 explicitly disabled the proxy-slot population in
`OpenAsk()` to prevent the v0.15.1 callback feedback loop. The
proxy-window code is preserved verbatim but won't be re-enabled
until we have real engine-set values for `mode1` (currently 0x05
guess), `state` (currently 0x07 guess), and the callback ABI at
`+0x34` / `+0x38`.

`ChaseDiag::OnAskOpcodeFired` (added in v0.15.1) is already wired and
dumps full snapshots of all 8 `ff8_win_obj` slots whenever
`opcode_ask` or `opcode_aask` fires AND chase-diag is enabled (F12).
**To unlock v0.15.2 engine ASK, capture a snapshot:**

1. Turn chase-diag ON (F12) before any natural in-game ASK.
2. Trigger any opcode_ask: vendor `Buy / Cancel`, NPC yes/no
   question, save-point's save-yes-no, Cid's `"Wanna become a SeeD?"`.
3. Send the resulting `Logs/ff8_field.log` (or just the
   `[CHASE-DIAG-ASK]` block).

v0.15.2 will hardcode the captured values into `TEMPLATE_MODE1` /
`TEMPLATE_STATE` and replace the stub `ConfirmCallback` /
`CancelCallback` with engine-aware no-ops or an ABI-correct handler
based on what we observe.

### What v0.15.1.2 BAT will verify

1. **Aaron hears Squall's line.** `"Forget it!  Let's go!"` plays
   through NVDA before our chase-mode prompt starts.
2. **The chase-mode prompt fires after the 3-second delay.** Field
   log shows `chase trigger MES detected ... deferring ASK open by
   3000 ms` followed 3 seconds later by `deferred-open timer
   expired; opening ASK now`.
3. **The rest still works** — chase mode persists, `chase_battle_freeze`
   no-ops second-and-later kani battles, Lapin Beach FMV plays.

### Files changed

- `src/chase_ask_overlay.cpp` — new state vars (`s_triggerPending`,
  `s_triggerTimestamp`), constant (`TRIGGER_DELAY_MS = 3000`),
  rewrite of `OnDialogText` to defer instead of open, addition to
  `Update()` for the deferred-open timer + chase-end cancel.
- `src/ff8_accessibility.h` — version bumped, comment trail
  extended.
- `CHANGELOG.md` — this entry.
- `DEVNOTES.md`, `NEXT_SESSION_PROMPT.md` — updated to ready-to-BAT
  state.

## v0.15.1.1

Hotfix for v0.15.1 BAT lockup: disable the engine-rendered proxy window in the chase ASK overlay

The v0.15.1 BAT showed the chase trigger working perfectly — ChaseDetector
activated, the kani slot resolved correctly (`Backgrounds slot 4`), the
`"Forget it!  Let's go!"` MES was detected, and the ASK opened. But the
proxy-window experiment locked the game.

### Root cause

When `OpenAsk()` populated a free `pWindowsArray` slot with `state=0x07`
and registered our `ConfirmCallback` / `CancelCallback` at offsets `+0x34`
and `+0x38`, the engine's window state machine started dispatching to
our slot every frame. `ConfirmCallback` and `CancelCallback` fired in a
repeating "Confirm, Cancel, Cancel" pattern at roughly 180 calls per
second, burning every frame in callback-stub returns. From the player's
perspective the game appeared frozen.

Aaron eventually completed the choice (`committed choice = Auto-drive`
at 21:01:03) because the TTS+keyboard path kept working alongside the
spam, but the engine-window approach was the lock cause.

The educated-guess template values (`mode1=0x05`, `state=0x07`) were
documented in v0.15.1 as needing v0.15.2 tuning via chase_diag's new
ASK snapshot logging. The lockup confirms we don't yet understand the
callback ABI either.

### The fix

Disable the proxy-slot allocation entirely in `OpenAsk()`. Pure
TTS+keyboard. Aaron is blind, so the engine-rendered window was a
sighted-player nicety, not a functional requirement. The TTS path was
already proven by the v0.15.1 BAT to work correctly even amid the
callback spam.

All proxy-window code is preserved verbatim:
- `FindFreeWindowSlot`
- `PopulateProxySlot`
- `ConfirmCallback` / `CancelCallback`
- `ReleaseProxySlot` (which already early-returns on `s_proxySlotIdx<0`)
- `EncodeChar` / `EncodeAskOptions`
- `TEMPLATE_MODE1` / `TEMPLATE_STATE` constants

v0.15.2 will re-enable this code once chase_diag's `OnAskOpcodeFired`
snapshot logging captures real engine-set template values from a
natural in-game ASK and we understand the callback ABI by observing
the engine's invocation pattern on legitimate dialogs.

### What v0.15.1.1 BAT will verify

1. **No more lockup.** When the chase ASK opens, the game continues
   running normally. Aaron hears the prompt, picks an option with
   Up/Down/Enter or 1/2, and the game proceeds.
2. **The rest of v0.15.1 still works.** ChaseDetector debounce, kani
   slot resolution (Backgrounds slot 4), `ChaseBattleFreeze` no-op of
   second-and-later kani BATTLE calls, and clean chase-end on
   transition to Lapin Beach.
3. **Optional:** With chase-diag ON (F12) before the chase, trigger
   any natural NPC ASK to confirm the snapshot logging fires and
   captures the engine's real `ff8_win_obj` template values for
   v0.15.2.

### Files changed

- `src/chase_ask_overlay.cpp` — the proxy-allocation block in
  `OpenAsk` is replaced by `s_proxySlotIdx = -1` plus an explanatory
  log line. Surrounded by a comment block explaining the rationale
  and pointing at v0.15.2.
- `src/ff8_accessibility.h` — version bumped, comment trail extended
  with the v0.15.1.1 narrative.
- `CHANGELOG.md` — this entry.
- `DEVNOTES.md`, `NEXT_SESSION_PROMPT.md` — updated to ready-to-BAT
  state.

## v0.15.1

First functional chase-mode build: manual mode caps Dollet/X-ATM092 chase battles at one per field

v0.15.0 was the diagnostic-only foundation. v0.15.1 is the first build that
actually changes chase behavior. Three new modules ship together as one
coordinated feature; v0.15.0's chase-diag is also fixed and extended.

### v0.15.0 BAT findings that shape this build

The v0.15.0 BAT (full chase playthrough — pre-tower battle, escape, all
five X-ATM092 chase battles, final Lapin Beach FMV) produced three findings
that invalidated parts of the original plan:

1. **Kani is in `pFieldStateBackgrounds`, not `pFieldStateOthers`.** Every
   chase field had `kani` in SYM but always BEFORE the v0.15.0 computed
   `othersStart = symNameCount - otherCount` index. v0.15.1 fixes this by
   routing through `FieldArchive::LoadJSMCounts` (already present in the
   code base) and tracking which array (Backgrounds or Others) holds kani
   in a new `s_kaniArrayKind` state.

2. **Var 84 / var 530 read 0x00 throughout the chase** at the documented
   savemap-on-disk addresses. Live engine state uses a different base.
   Until the live var area is found, **chase detection uses field-name
   matching, not var 530 polling.**

3. **`pCurrentFieldName` lags `pCurrentFieldId` by 2-5 seconds.** v0.15.1
   debounces field transitions: every fieldId change starts a 2-second
   timer; consumers see no field name until the timer expires.

### What ships

- **`chase_detector` (new)** — single source of truth for chase-scene state.
  Hard-coded chase-field name set (`domt4_1`, `domt5_1`, `domt3_2`,
  `domt2_1`, `doopen2a`, `dotown_3`, `dotown_2`, `dotown_1`). 2-second
  debounce on field name. Per-field battle counter via game-mode edge
  detection (`MODE != BATTLE` → `MODE == BATTLE` ⇒ +1). Resolves kani's
  runtime block address through `LoadJSMCounts` + SYM lookup, supporting
  both Backgrounds and Others arrays. Chase mode (`manual` / `auto`)
  persists to `ff8_accessibility.ini` under a new `[Chase]` section.

- **`chase_ask_overlay` (new)** — fires when Squall says
  `"Forget it!  Let's go!"` (the exact MES, with the double-space, confirmed
  by v0.15.0 dialog log capture; not `"Run!"` as the deep research
  suggested). Two-path hybrid:

  - *Engine-rendered window:* allocates a free slot in `pWindowsArray`,
    populates it with two options (`Auto-drive`, `Manual`), registers
    callbacks at the `+0x34` / `+0x38` offsets the engine reads.
    Template values (`mode1=0x05`, `state=0x07`) are educated-guess
    defaults; v0.15.2 will tune them with real values from the new ASK
    snapshot logging.
  - *TTS + keyboard:* always works, regardless of whether the engine
    renders the slot. ScreenReader speaks the prompt; up/down cycle the
    highlight (TTS announces new option each press); Enter commits.
    Number keys 1 and 2 also work as direct shortcuts. All keys are
    Alt-gated per the v0.14.105 convention.

  Auto-drive isn't shipped in v0.15.1 — selecting it announces
  `"Auto-drive is not yet implemented. Falling back to manual."` and
  stores `manual` to the INI. The selection slot is wired so v0.15.2+
  auto-drive arrives as a behavior change without overlay rework.

- **`chase_battle_freeze` (new)** — MinHook detour on
  `pExecuteOpcodeTable[0x69]` (opcode_battle). Gates:

      mode == manual
   && IsInChaseField()
   && IsKaniEntityPtr(entityPtr)
   && GetCurrentFieldBattleCount() >= 1
   ⇒ return JSM-VM advance code 3 without calling original

  All other cases pass through to `s_origBattle`. The kani check is
  pointer equality between the hook's `entityPtr` parameter and
  `ChaseDetector::GetKaniEntityPtr()` (which returns
  `backgroundsBase + 0x1B4 * kaniSlot` or `othersBase + 0x264 * kaniSlot`
  per `s_kaniArrayKind`). Result: the chase is capped at one battle per
  field, the post-knockdown WAIT timer is sidestepped entirely.

  Risk noted: if BATTLE pushes a return value to its script-VM stack
  (we haven't disassembled it to confirm), post-BATTLE script paths
  might misbehave. The v0.15.1 BAT will surface this if it happens.

### chase_diag fixes and extensions

- `FindKaniSlot` (broken — assumed Others-only, computed wrong
  `othersStart`) replaced with `FindKaniLocation` using `LoadJSMCounts`.
- New `s_kaniArrayKind` state; `PollKani` now picks the correct stride
  (0x1B4 Backgrounds, 0x264 Others) per kind.
- New `ChaseDiag::OnAskOpcodeFired(opcodeLabel)` — when chase-diag is
  enabled (F12), dumps all 8 `ff8_win_obj` slots in detail (state,
  mode1, open_close_transition, win_id, firstQ/lastQ/curQ, field30,
  callbacks, text pointers) plus a 60-byte hex dump per slot. Called
  from `field_dialog.cpp`'s `Hook_opcode_ask` / `Hook_opcode_aask`.
  Workflow: turn chase-diag ON before the chase, trigger any natural
  in-game ASK (NPC question, vendor `buy / cancel`) to capture template
  values, then enter the chase. v0.15.2 will hardcode the captured
  values into `TEMPLATE_MODE1` / `TEMPLATE_STATE`.
- No-op when chase-diag is disabled — doesn't spam the field log on
  normal NPC dialogs.

### What the v0.15.1 BAT will verify

1. Chase-field name set is complete (any chase field not in our
   hardcoded list?).
2. Field-name debounce dodges the v0.15.0 stale-name false positives.
3. The chase ASK opens at chase entry (Squall's MES detected), NVDA
   reads the prompt and options, selection routes correctly, INI
   persists.
4. (Best-effort) The engine renders our allocated window slot. If a
   natural ASK fires before chase, captured template values yield a
   clean visible ASK; otherwise the TTS + keyboard fallback handles
   the choice and v0.15.2 tunes the engine path.
5. `chase_battle_freeze` correctly no-ops the second-and-later kani
   BATTLE call per chase field. Existing v0.15.0 chase-diag still
   captures cleanly with the kani slot fix.
6. Chase still ends correctly (Lapin Beach FMV plays) when the kani
   battles after the first are no-op'd.

## v0.15.0

Dollet / X-ATM092 chase scene work begins: F12-toggleable diagnostic logger

This ships only the diagnostic infrastructure. The actual chase accessibility
feature (in-engine ASK overlay + manual-mode `opcode_battle` no-op +
auto-drive option) lands in v0.15.1+ once we have the playthrough data.

The deep research on the chase (saved at `Plan & Research Documents/X-ATM092
chase accessibility deep research results.md`) confirmed five things that
shape the implementation:

1. **Var 530 at absolute address 0x01CFEB7E** is the Dollet state bitmap.
   Bit 0x10 ("xatm first knock out") flips on at the chase start. We don't
   need broad memory monitoring — one byte gives us the chase-active signal.
2. **Var 84 at 0x01CFE9C0** is the place ID. Dollet places are 99 (Comm
   Tower), 100 (Mountain Hideout), 93 (Town Square), 94 (Lapin Beach).
3. **X-ATM092's field entity is named `kani`** in SYM data (Japanese for
   crab — the boss is a giant crab-shaped war machine). The mod can find
   its slot per field by string match against the SYM names already loaded
   by the field archive.
4. **`opcode_battle` (0x69)** is the recommended freeze hook for manual
   mode: when calling entity is kani in a chase field AND we've already had
   one battle this field, no-op the BATTLE call. This caps the chase at
   one battle per field and entirely sidesteps the post-knockdown WAIT
   timer. Modifying var 530 directly is unsafe (bits not fully enumerated
   in public docs).
5. **Engine `opcode_ask` is fragile to call from outside the script VM.**
   Use a proxy-window pattern via the existing `show_dialog` hook surface
   instead: allocate our own ff8_win_obj slot, populate it, register
   confirm/cancel callbacks at the +0x34 / +0x38 offsets the engine
   already exposes. From the player's perspective it's a real in-engine
   ASK dialog; from the engine's perspective it's a real win_obj in the
   active windows array; we just don't go through the script-VM ASK
   handler.

v0.15.0 instruments the engine state we still need to verify empirically
before building the feature: chase field short names in order, kani's
slot index per chase field, the rise-timer duration, the chase formation
ID(s), and the full var 530 bit-transition history.

New module `src/chase_diag.{h,cpp}` adds an F12-toggleable diagnostic.
When toggled on (TTS announces "Chase diagnostic enabled"), six log
streams are emitted to the appropriate domain logs:

- `[CHASE-DIAG-FIELD]` on every field transition — field ID, short name
  from `pCurrentFieldName`, place ID (var 84), var 530 current value,
  full per-entity dump (model, flags, position, exec flags), full SYM
  names list with the kani slot in the runtime entity array highlighted
  if found.
- `[CHASE-DIAG-VAR530]` per-frame poll at 0x01CFEB7E. Logs only on
  change. The bit deltas are decoded into human-readable phrases
  ("+0x10 xatm first knock out", etc.) per the Qhimm wiki documentation.
- `[CHASE-DIAG-PLACE]` per-frame poll of var 84 at 0x01CFE9C0. Logs only
  on change with the place name decoded for Dollet places 92–100.
- `[CHASE-DIAG-KANI]` when kani is detected in the current field via
  SYM name match. Logs whenever its position, walkmesh triangle, or
  push/talk/through flags change. Timestamps on these log lines let us
  measure the rise-timer duration empirically (collapse → resume).
- `[CHASE-DIAG-FRAME]` heartbeat every 5 seconds with field name,
  player position, var 530, place ID, game mode, and current kani slot.

F12 reservation rule honored: searched all source files for `VK_F12` /
`0x7B` before adding the handler. None found — F12 has been free since
v0.14.75 promoted the screenshot binding to F11 and stripped the F12
code. The new F12 handler is alt-gated alongside every other F-key
handler per the v0.14.105 lesson, so Alt+F4 doesn't accidentally fire
the toggle.

The module is purely additive: no behavior change when the diagnostic is
off (the default), and no inter-module hook insertions when on. Game
state is read via FF8Addresses convenience accessors (`pCurrentFieldId`,
`pCurrentFieldName`, `pFieldStateOthers`, `pFieldStateOtherCount`) plus
three confirmed absolute addresses for the savemap variables. Memory
access is SEH-guarded throughout because the addresses point into the
FF8 process's address space and may be transiently invalid during
mode/field transitions.

The v0.14.108 module set is unchanged in v0.15.0 — the follower
behavioral-fingerprint filter, the persistence layer, the screenshot
feature, the existing battle/dialog/scan/world-map/menu TTS modules all
ship as-is.

FILES: `src/chase_diag.h` (new), `src/chase_diag.cpp` (new),
`src/dinput8.cpp` (Initialize/Update/Shutdown calls + F12 handler +
alt-gating + comment block update), `src/deploy.bat` (chase_diag.cpp
added to compile list), `src/ff8_accessibility.h` (version bump).

QUEUED with this version: the previous session's push-utility hardening
(`Utilities/push_to_github.ps1` Step 7b duplicate-version refusal,
phase-by-phase logging to `Logs/push_diagnostic.log`, non-modal progress
dialog, success-message enrichment with HEAD shortstat) plus the
DEVNOTES and NEXT_SESSION_PROMPT updates from v0.14.108. These ride
along because v0.14.108 push happened with the changes uncommitted; the
utility's new Step 7b would refuse a same-version repush, so the
bundling is intentional.

BAT plan: load the pre-tower-drop save. Press F12 (TTS: "Chase
diagnostic enabled"). Walk through the X-ATM092 drop cutscene. Engage
the first battle, damage to collapse, escape. Walk through the
post-battle field, trigger Squall's "Run!". Enter the first chase
field. Stand still — let X-ATM092 catch up. Engage the chase battle,
escape. **Stand still on the same field through one or two respawn
cycles** — this is the critical capture. Press F12 to disable. Send
the four logs (`ff8_field.log`, `ff8_battle.log`, `ff8_dialog.log`,
`ff8_mod.log`).

## v0.14.108

Filter party members from the field entity catalog (take 2)

When the player has 2+ active party members, the followers (Zell, Quistis,
etc.) appear in the field navigation catalog because they have throughonoff
set (you walk through them so they don't block) but no talkonoff/pushonoff.
The existing classification chain falls through to ENT_EXIT, which is
wrong — they're not exits, they're invisible-to-interaction party
members. Pressing X on them does nothing. They're noise that pads out
the F9 cycle list.

v0.14.107 attempted a savemap-aware filter that cross-referenced the
entity's model ID against the active party formation at savemap+0xAF0
(0x01CFE74C). That assumed canonical model→charId mapping (0=Squall,
1=Zell, 2=Irvine, etc.). BAT on bggate_1 with a Squall + Zell + Selphie
party (formation [1,0,5,255]) revealed the assumption is wrong: the
followers showed up as model 2 and model 4 — canonical Irvine and Rinoa
IDs, neither in this party. The savemap cross-reference correctly
returned false, the filter no-op'd, and the followers stayed in the
catalog. The engine reuses model slots per-field; the canonical mapping
is an averaged truth that doesn't hold per-field.

v0.14.108 replaces the model→charId filter with a behavioral fingerprint
that catches followers regardless of which model slot the field assigned
them:

  modelId in [0, 9]   visible character (party-character model range)
  throughonoff  > 0   player walks through them
  talkonoff    == 0   not talkable
  pushonoff    == 0   no collision

The modelId < 10 guard excludes save points (model 24) and other non-
character interactive objects with throughonoff. Save point detection
runs later in RefreshCatalog() via the modelId == 24 check, so save
points qualify and reach the JSM-based reclassification regardless.

This filter also catches non-interactive cutscene characters that walk
through scenes — actually correct behavior since they're not navigation
targets either.

Known trade-offs:
- A real NPC whose script sets throughonoff before TALKRADIUS sets
  talkonoff would be transiently filtered. The catalog refreshes on
  every F9 press, so the next press picks them up. Acceptable race.
- Followers using model 10+ (generic NPC range) wouldn't be caught.
  Empirically followers seem to land in 0–9, but if a future BAT shows
  otherwise this can be relaxed.

The v0.14.107 helpers (IsCharacterInActiveParty, ModelIdToCharId) and
the per-field [party-state] formation diagnostic stay in place. They're
harmless, well-documented, and may be useful for future party-aware
features. Only the filter call site changes.

Files: src/field_nav_catalog.inl (replace v0.14.107 filter block with
the behavioral fingerprint, ~12 lines), src/ff8_accessibility.h
(version bump). v0.14.107 helpers in field_nav_helpers.inl and the
[party-state] diagnostic UNCHANGED.

## v0.14.107

Filter party members from the field entity catalog

When the player has 2+ active party members, the followers (Zell, Quistis,
etc.) appear in the field navigation catalog as if they were interactable
NPCs because they have talkonoff set. Pressing X on them does nothing —
they're noise that pads out the F9 cycle list. v0.14.107 filters them out
using a savemap-aware check.

Approach: in RefreshCatalog(), for each non-player entity, if its model
ID maps to a character ID currently in the active party formation, skip
it. The model→charId map is straightforward (model 0=Squall, 1=Zell,
2=Irvine, 3=Quistis, 4=Rinoa, 5=Selphie, 6=Seifer, 7=Edea, with model 8
Quistis-uniform also mapping to charId 3); the formation array lives at
savemap+0xAF0 (absolute 0x01CFE74C), four bytes, each a charId 0–7 or
0xFF for empty. Same address used by Junction TTS and save block content
TTS — confirmed reliable across many BATs.

Why cross-reference savemap formation rather than filtering on model ID
alone: the engine reuses model slots across scenes. A model 7 in early-
game might be a generic background character, not Edea. The April 2026
entity-classification thread confirmed this empirically. Only filter
when the model corresponds to a character actually in the active party
right now.

Edge cases handled by design:
- Solo Squall (Fire Cavern, formation [0xFF, 0x00, 0xFF, 0xFF]) — Squall
  is the player and excluded by the `i != s_playerEntityIdx` guard, so
  nothing filters.
- Pre-recruitment cutscene NPCs sharing a party-character model — the
  formation byte for that charId is still 0xFF, IsCharacterInActiveParty
  returns false, filter no-ops, NPC stays in catalog.
- Quistis as classroom instructor (model 8) — only filtered when she's
  in the active party; otherwise kept.

Adds two helpers in field_nav_helpers.inl: IsCharacterInActiveParty
(charId) reads the 4-byte formation array under SEH; ModelIdToCharId
(modelId) maps 0–7 directly and 8→3 with the Quistis-uniform special
case. New per-field [party-state] diagnostic logs the formation array
contents once per field load; new [party-filter] line per filtered
entity for verification.

Files: src/field_nav_helpers.inl (two new helpers near top, ~50 lines
including documentation), src/field_nav_catalog.inl (filter check before
classification + one-shot per-field diagnostic, ~50 lines), src/field_
navigation.cpp (1 line: new s_partyDiagDumped flag declaration), src/
field_nav_fieldscripts.inl (1 line: reset s_partyDiagDumped = false on
field load), src/ff8_accessibility.h (version bump).

## v0.14.106

v0.14.105 + v0.14.106: Fix speech rate persistence + INI template

v0.14.105: Speech rate (and other accessibility settings) were creeping up
by 1 each session because Alt+F4 close was firing the F4 IncreaseRate()
handler on the way out. Every F-key accessibility handler (F1-F8, F11) is
now gated on !alt (GetAsyncKeyState(VK_MENU)) so Alt+combos no-op. Also
added a one-shot LogActualSAPIState diagnostic to confirm the fix; BAT
verified persistence is sound and SAPI preserves rate/volume across voice
changes.

v0.14.106: Strip the diagnostic harness now that the fix is verified. Add
a human-readable commented template to ff8_accessibility.ini explaining
each setting, its range, and the in-game shortcut. Existing INIs auto-
upgrade on next launch (preserving all values). New helpers in config.cpp:
HasTemplateMarker(), EnsureTemplate().
