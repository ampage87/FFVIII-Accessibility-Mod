# Next Session Prompt -- v0.15.9.10 READY-TO-BAT (MODE_ORIGINAL)

**Build state:** v0.15.9.10 implemented 2026-05-12, awaiting build. Aaron's chase-scene item #2 complete: the third ASK option ("Original") is now a real third chase mode that bypasses all mod chase machinery and lets the vanilla FF8 chase play out unmodified.

**HEAD on GitHub:** v0.15.9.9.1 (pushed 2026-05-12). Local tree is one version ahead at v0.15.9.10 awaiting BAT.

## Read first

1. `DEVNOTES.md` -- current state (v0.15.9.10 READY-TO-BAT section at top, plus v0.15.9.9.1 BAT success preserved for context).
2. This file -- BAT plan and verification markers.

## What just landed (v0.15.9.10)

Five-file touch implementing MODE_ORIGINAL as a real third chase mode:

### 1. `src/chase_detector.h`

Added `MODE_ORIGINAL = 2` to the `Mode` enum with rationale comments. Backward-compatible (MODE_MANUAL=0 and MODE_AUTO=1 unchanged).

### 2. `src/chase_detector.cpp`

- `LoadChaseModeFromIni`: accepts `"original"` string -> `MODE_ORIGINAL`.
- `ChaseModeName`: switch statement returns `"original"` for `MODE_ORIGINAL` (previously a ternary; rewritten for three cases).

### 3. `src/chase_battle_freeze.cpp::Hook_opcode_battle`

Short-circuit at top of the `if (inChase)` branch. When `mode == MODE_ORIGINAL`, returns `s_origBattle(entityPtr)` immediately with no chase-counter increment, no `[CBF] PASS`/`NO-OP` log, no `RegisterChaseAgent` call. Vanilla FF8 chase battles fire as Square shipped them. Reordered so `mode` is read before `s_chaseCallCount` is incremented (the short-circuit needs to happen before any chase counter touches). Initialize log message updated.

### 4. `src/chase_kani_freeze.cpp`

- `Update`: short-circuit before reading `pGameMode`. If `s_freezeActive` is true from a previous mode (e.g. user changed mode mid-chase via INI), `DeactivateFreeze` runs cleanly so no stale pin keeps writing.
- `RegisterChaseAgent`: short-circuit immediately after the `!entityPtr` early return -- belt-and-suspenders in case any future code path calls it bypassing chase_battle_freeze.

### 5. `src/chase_ask_overlay.cpp::CommitChoice`

`ANSWER_ORIGINAL` branch now sets `ChaseDetector::MODE_ORIGINAL` instead of `MODE_MANUAL`. Comment updated to describe the new behavior.

### Not changed

`chase_auto_pilot` -- its engagement gate already requires `mode == MODE_AUTO`, so MODE_ORIGINAL naturally fails the gate and the auto-pilot stays disengaged. Verified via inspection: line 1933, `bool autoMode = (ChaseDetector::GetChaseMode() == ChaseDetector::MODE_AUTO);`.

## BAT plan for v0.15.9.10

Aaron's BAT cycle:

1. `deploy.vbs` -> `src/deploy.ps1` -> `src/deploy.bat`.
2. Launch FF8. Listen for the startup log via mod loader; if Aaron previously committed Auto from v0.15.9.9.1's BAT, the INI will say `chase_mode=auto` and a fresh chase would default to Auto. That's fine -- the ASK will fire again and Aaron picks Original this time.
3. Load the save near chase start.
4. Trigger the chase. Listen for:
   - Squall's "Forget it! Let's go!" line ONCE via AMESW.
   - 3 seconds elapse.
   - New ASK prompt: "X-ATM092 is heading right for you. How do you want to run?"
   - Three option labels read out as cursor navigates.
5. **Choose Original.** Listen for: "Original selected".
6. Experience the vanilla chase: walk west on the trail, get caught by chase battles, robot pursues, ground shakes, etc.
7. Optional persistence check: after the chase, exit FF8 and inspect `ff8_accessibility.ini` -- should show `chase_mode=original` under `[Chase]` section.

### Verification markers in Logs/ff8_field.log

**At mod load (startup):**

```
ChaseBattleFreeze: Initialized v0.15.9.10 (... MODE_ORIGINAL short-circuits before any chase logic so vanilla chase plays out unmodified ...)
ChaseDetector: loaded chase_mode='<previous>' from INI
```

**After ASK commits Original:**

```
ChaseAskOverlay: committed choice = 3 (Original: vanilla chase, no mod help)
ChaseDetector: chase_mode set to 'original' (persisted)
```

**On chase ACTIVATED:**

```
ChaseDetector: chase ACTIVATED on entry to 'domt4_1' (mode=original, baseline calls=N freezes=0)
```

**Throughout the chase fields:**

- **No `[CBF]` lines** (PASS or NO-OP). The short-circuit returns `s_origBattle` before any logging.
- **No `KaniFreeze:` lines** (no FREEZE ACTIVATED, no CAPTURE STARTED, no CHASE-AGENT). The Update short-circuit bails before any of that work runs.
- **No `ChaseAutoPilot: ENGAGED` lines.** Auto-pilot's engagement gate naturally skips MODE_ORIGINAL.

**Battles WILL fire** and appear in `Logs/ff8_battle.log` as normal battle activity (BTL_START, battle TTS, battle command menus, etc.). The party WILL get caught by X-ATM092 -- that's the vanilla behavior, which is what Original mode delivers.

### Outcomes

- **Best (mode works cleanly):** Aaron experiences the vanilla chase end-to-end. No mod chase machinery activates. Log markers as above. Push v0.15.9.10 and move to item #3.
- **Acceptable (chase works but some log noise):** A path was missed. Aaron sees the vanilla behavior, but the log has stray `[CBF]` or `KaniFreeze:` lines. Diagnose which path, add a short-circuit in v0.15.9.10.1.
- **Worst (mode doesn't behave as vanilla):** Something unexpected. E.g. battles get suppressed despite the short-circuit, or auto-pilot engages. Revert the ASK routing to MODE_MANUAL in v0.15.9.10.1 to restore working behavior, then diagnose.

## After v0.15.9.10 BAT

1. **Push v0.15.9.10 to GitHub.** Aaron runs `Utilities/push_to_github.vbs`.
2. **v0.15.9.11 -- Keyboard suppressor during Auto chase** (Aaron's item #3). Last item in the chase scene work. Extend `HookedGetKeyState` in `field_nav_input_hooks.inl` to zero W (0x11), ESC (0x01), and FF8 confirm/cancel/menu scancodes when `ChaseAutoPilot::IsEngaged() && ChaseDetector::GetChaseMode() == MODE_AUTO`. Accessibility hotkeys bypass the keyboard buffer (read via `GetAsyncKeyState` in mod-owned code) so they're unaffected. Predicate is narrowly scoped: only fires during engaged Auto chase; Manual/Original chases are untouched; F9 path-finding and world-map AD are untouched.

## Mid-chase mode swap edge case (already handled)

If Aaron edits `ff8_accessibility.ini` mid-chase to flip mode (which is admittedly unusual), `chase_kani_freeze::Update`'s MODE_ORIGINAL short-circuit calls `DeactivateFreeze("chase mode switched to MODE_ORIGINAL")` to release any active freeze cleanly. Documented in the implementation comments. Not part of the BAT plan but worth knowing.
