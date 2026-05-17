**Purpose & context**

Aaron is the sole developer of the FF8 Accessibility Mod — a `dinput8.dll` injection for Final Fantasy VIII Steam 2013 (App ID 39150, FF8_EN.exe + FFNx v1.23.x) that makes the game playable for blind players via Windows SAPI TTS, navigation assistance, and entity catalogs. Aaron is blind and uses NVDA as his screen reader, and is also the primary tester.

**Project root:** `C:/Users/ampag/OneDrive/Documents/FFVIII-Accessibility-Mod/FF8_OriginalPC_mod/`

GitHub: `ampage87/FFVIII-Accessibility-Mod`. **GitHub HEAD = v0.16.5.1** (commit `7c46239`, tag `v0.16.5.1`, pushed 2026-05-17 14:42 local). **Local tree = v0.16.5.2** (utility-only change adding a client-side mirror of the GitHub Actions CI checks; no mod code change). Refactor chapter (v0.16.0 → v0.16.5) is closed; v0.16.5.1 deferred-turn release fix shipped; v0.16.5.2 pending push.

---

## v0.16.5.2: local mirror of CI safety checks

Utility-only change. Mirrored both checks from `.github/workflows/safety-checks.yml` into a new Step 7c in `Utilities/push_to_github.ps1`, between the duplicate-commit refusal (Step 7b) and the cmd.exe invocation (Step 8). The CI runs server-side AFTER a push lands; if the size or SET3 check fails, the offending commit is already on `main` with a red X. Step 7c catches the same conditions client-side and refuses via the existing `Show-ErrorDialog -ShowViewLog $false` flow with a screen-reader-readable explanation.

Thresholds mirror the YAML exactly (60 KB warn, 80 KB fail, `src/*.{cpp,inl}` at depth 1; SET3 marker `SET3.*PERMANENTLY DISABLED` in `src/field_navigation.cpp`). Watch-zone files (60-80 KB) log to `Logs/push_diagnostic.log` as `[Step 7c] Watch zone ...` for passive trend monitoring without blocking.

Duplication between YAML and PS1 is intentional and acceptable: the local check needs to be fast and offline. Bidirectional pointer comments added in both files so future maintenance keeps them in sync.

DLL behavior is byte-for-byte identical to v0.16.5.1 except `Initialize()` logs `v0.16.5.2`. No BAT required; the verification is simply "does push_to_github.ps1 still succeed when all files are under threshold" (which they are post-v0.16.5).

---

## v0.16.5.1: 3-line fix for the deferred-turn release path (SHIPPED)

v0.16.5 BAT log review (battle 2, log lines 2942–3136) caught a latent bug: when Selphie's turn started on the exact frame Zell's Ifrit cast began animating, `PollTurnAndCommands` correctly identified the collision and stashed "Selphie's turn. Attack." in the deferred buffer with the `[TURN] Deferred (damage in flight): ...` log line. But the release path — `PollDeferredTurnAnnounce` defined in `battle_tts_menu_poll.inl` — was never wired into `Update()`. The stashed line sat in the buffer until battle end, then got silently wiped by `OnBattleEnter` state reset. dryRun grep probes across `battle_tts.cpp` and every sibling `.inl` confirmed no caller existed.

The bug has been latent since v0.13.52 introduced the deferred-turn feature (2026-02). v0.16.5's pure mechanical split did not introduce it; the function body and absent call site are byte-for-byte from v0.16.4 back to v0.13.52. The split exposed it by giving the BAT triage a specific marker (`[TURN] Deferred ...`) to grep for.

### Fix

`src/battle_tts.cpp::Update()`, immediately after the existing `PollHPChanges()` block:

```cpp
if (s_inBattle && s_initAnnounceDone && s_enemyAnnounceDone) {
    PollDeferredTurnAnnounce();
}
```

Guards match the surrounding poll calls. Placement after `PollHPChanges` matches the function's own header comment so `s_ewmHoldForDamageTTS` reflects this frame's HP signals before the release-decision runs. No change to the function body.

### Verification

Not reliably reproducible (one-frame collision window). Future battle log review will look for the pattern:

- Pre-fix: `[TURN] Deferred (damage in flight): ...` — nothing else.
- Post-fix: `[TURN] Deferred (damage in flight): ...` followed within ~5 seconds by either `[TURN] Deferred fired after <ms> ms: ...` (release succeeded) or `[TURN] Deferred cancelled (char N -> M, stale): ...` (active char advanced past the deferred-for character).

Aaron will keep an eye out for the post-fix pattern across future BAT runs. If `[TURN] Deferred fired ...` is observed at least once with the damage-first / turn-second audio ordering preserved, the fix is confirmed end-to-end.

---

### v0.16.5 BAT confirmed clean (2026-05-17 13:18–13:25)

Build succeeded, runtime verified across two battles. Every menu announcement path exercised in the log:

- Battle 1: Squall Attack with status-appended single-enemy target (`Bite Bug, Float`). Selphie command nav (cursor=1 Magic), Magic submenu opens via the mode-byte path, 5-spell list builds correctly (`Cure x77, Fire x10, Blind x13, Esuna x8, Double x4`), cursor moves between spells announce, all-target via 0x9D 0→1 announces with status, Magic target cancel resets spell cursor and re-announces current spell.
- Battle 2: Zell command nav across Magic/GF, GF submenu opens via the v0.12.72 fallback (mode 0x02→0x00 + phase 80) path, Ifrit GF list builds, all-enemies announce. Deferred GF cancel: `0x9D 1->0` deferred for Ifrit, then turn-end confirms the GF — cancel suppressed, `[GF-HP-SUB] Enabled for slot 0 (...gfIdx=2 'Ifrit')`. Squall Draw turn: target Caterchipillar, draw list (Thunder, Cure), cursor nav, Stock/Cast prompt. Selphie Draw Cure + Cast with ally target cycling (Selphie→Squall→Zell). v0.13.50 false-exit suppression on Draw mode flips, v0.12.82 draw-list revisit flag both fired correctly.
- **v0.16.4's open Ifrit-AD question RESOLVED**: this BAT had `[GF-EFFECT] Animation detected: effectId=200 gfIdx=2 slot=0` followed immediately by `[GF-AD] Playback started: ... cues=6 duration~23.0s`, all 6 cues played at proper timestamps. v0.16.4's miss was intermittent engine timing, not refactor-related. Heartbeat diagnostic stays parked.
- EWM activity: 2 GF-PATCH cycles, 9 ATB caps, 18 FRZ-DIAG, 268 POST-REL, 14 each DMG/ACT-DIAG. POST-REL shows entities at non-converged values (`s0=360/12000 s1=8400/12000 s2=3240/12000`) — v0.13.57 exact-value restore preserved.
- Zero ERROR/EXCEPTION/CRITICAL/ASSERT lines. Three FAIL hits all the pre-existing v0.10.77 FFNx-GF hook miss.

The one finding from BAT triage — the deferred-turn release path missing its caller — was patched in v0.16.5.1 above.

## v0.16.5: battle_tts_menu.inl split (shipped local, pending push)

The size-split chapter is complete (pending BAT confirmation). v0.16.5 carved `src/battle_tts_menu.inl` (81.89 KB monolith) into a 1.05 KB slim shell + four sub-`.inl` files. **Pure mechanical split** — no behavior change. Battle menu TTS is user-facing and load-bearing for accessibility, so every announcement path (turn start/end, command cursor, submenu entry/exit across three detection mechanisms, Magic/GF/Item/Draw cursor announces, Stock/Cast prompts, all-target entry/cancel, deferred GF cancel, deferred turn TTS) is byte-for-byte identical to v0.16.4.

The `PollTurnAndCommands` function (~52 KB / ~700 lines) stays whole in `_poll.inl`. Its internal blocks share local state (`cmdCursorChangedThisFrame`, `subCursor`) and live inside one outer SEH guard; splitting them into helpers would have required scope restructuring and risked menu-TTS regression.

Include chain (dependency-ordered, textually included from the slim parent inside `namespace BattleTTS`, which is included from `battle_tts.cpp`):

```
state → lists → helpers → poll
```

`state.inl` first (declares every static and struct). `lists.inl` second (builders read state). `helpers.inl` third (calls list builders). `poll.inl` last (consumes everything above).

File sizes: state 16.2 KB, lists 9.6 KB, helpers 3.0 KB, poll 55.4 KB, slim shell 1.05 KB. Largest is `_poll.inl` at 55.4 KB — under the 60 KB warn line. Total footprint 85.3 KB vs original 81.9 KB; the ~3 KB overhead is per-file orientation headers.

**`battle_tts.cpp` unchanged.** Many statics declared in `_state.inl` are referenced by `OnBattleEnter`'s reset block in `battle_tts.cpp` itself (`s_turnActiveCharId`, `s_turnCmdCursor`, `s_inSubmenu`, `s_turnSubmenuCursor`, `s_submenuCommandId`, all the `s_*ListBuilt` flags, all the Draw cursor trackers, etc.); `CHAR_NAMES[]` is read by `GetCharNameById` in the shared-victory section. File-scope `static` visibility carries across the textual-include boundary, identical to the v0.16.4 pattern. `deploy.bat` unchanged.

### CI allowlist emptied

`.github/workflows/safety-checks.yml` allowlist had four stale entries (`field_dialog.cpp`/`field_archive_jsm.inl`/`battle_tts_ewm.inl`/`battle_tts_menu.inl` for v0.16.2/3/4/5 respectively). All four files are now slim 1–3 KB shells. Allowlist replaced with a documentation comment noting v0.16.5's emptying. Only `field_archive_jsm_scan.inl` at 63 KB remains in the watch zone (accepted exception — warns, does not fail).

### v0.16.5 BAT expectations

Build should succeed cleanly. Runtime BAT should confirm every battle-menu announcement path:

- Battle start: enemy name + "Battle!" announce
- Turn start: "[Char]'s turn. [Cmd]." with deferral if damage in flight
- Command cursor navigation: Attack / equipped commands / Limit Break toggle
- Submenu entry: Magic spell with quantity, GF name, Item with page/slot, Draw target+spell
- Submenu cursor navigation in each of the four submenus
- Target selection: single enemy/ally, "All enemies"/"All allies"/"All targets" with status appended for enemy targets
- Target cancel: returns to command menu announce; GF target cancel deferred 150ms then announced; Magic/Draw cancels reset cursors without false re-announces
- Draw Stock/Cast prompt
- All-target entry via 0x01D7689D 0→1 (GF target path)
- Deferred turn announcement firing after damage clears or 5s timeout
- v0.13.57 ATB exact-value restore preserved (no regression from menu split since `_poll.inl` doesn't touch EWM)

If any of those misfires, look at `battle_tts_menu_state.inl` first — the most likely failure mode would be a missing static or a struct definition out of order. The poll path is structurally identical to v0.16.4 byte-for-byte; behavior regressions there would be surprising.

### Active refactor queue

**EMPTY.** v0.16.5 was the final size-split task. Every `src/*.cpp` and `src/*.inl` is now under the 80 KB hard fail.

Completed: v0.16.0 (`world_map.cpp`), v0.16.1 (`chase_auto_pilot.cpp`), v0.16.2 (`field_dialog.cpp`), v0.16.3 (`field_archive_jsm.inl`), v0.16.4 (`battle_tts_ewm.inl`), v0.16.5 (`battle_tts_menu.inl`).

Pattern (established v0.16.0, refined through v0.16.5): parent `.cpp` (or shell `.inl`) becomes a slim file with namespace block + `#include` chain of `.inl` files + tiny public-API tail when applicable. `*_state.inl` (statics) included FIRST. No header guards or namespace decls inside `.inl` files. `*_history.h` archive with `#if 0` wrapper holds removed legacy content when applicable. Aim for 5-20 KB per `.inl`; resplit if any approaches 60 KB. Cross-`.inl` statics go in `*_state.inl`. Default to PURE mechanical split unless explicit approval to refactor logic — user-facing TTS paths in particular preserve every announcement exactly.

The v0.16.4 narrative (EWM split details, BAT result, Ifrit AD miss diagnostic candidate) has moved to `DEVNOTES_HISTORY.md`.

## Backlog (priority order, now active after the refactor queue cleared)

1. **Ifrit / GF audio description miss diagnostic** (carried from v0.16.4 BAT): if it recurs in v0.16.5's BAT or any later session, add 1-second `[GF-EFFECT-POLL] magicId=N prev=M` heartbeat to `PollBattleMagicId` in `src/battle_tts_ewm_gf_effect.inl` to capture what values the engine writes to `0x01D99A68` during a GF cast.
2. **POLL teardown garble** (carried from v0.16.2 BAT): the polling-thread fallback occasionally speaks fragments like `[Name80]kindrL` ~17 seconds after a dialog dismiss. Pre-existing behavior. Fix candidate: reject text containing unresolved `[…]` tokens in the POLL path.
3. **`menu_tts.cpp` T-handler `!shift` gate**. One-line cleanup.
4. **FieldAnnounce display-name catalog audit** in `src/field_display_names.h`. Wrong mappings for fieldIds 0x0134 / 0x0136. Verify Fire Cavern A mapping (fieldId 0x0088, engine `fieldName='bdview1'`, expected "Fire Cavern A") end-to-end.
5. **Field-name populate race** at Part B arrival check — diagnostic log only, audio fine.
6. **Deep-research doc updates**: `Plan & Research Documents/Dollet timer countdown deep research results.md` — wrong-math fix + LIVE TIMER FOUND appendix.

### Future (deferred)

- **Refined-coord persistence** (JSON or %APPDATA% store so BAT-captured coords survive sessions, replacing per-destination hardcodes).
- **Other geometric-trigger destinations**: as v0.16.0.2's two-tier cap catches them on first arrival, add their refined coords to the Initialize() hardcode chain.
- **Engine-write hook for cleaner countdown freeze** (cosmetic ±1-sec flicker; not urgent).

### Deferred (don't pick without Aaron's direction)

- SeeD rank bug #27
- Walk-and-talk dialog gap
- Refined-coord narrow-gate steering (#29)
- `chase_diag::OnAskOpcodeFired` snprintf bug

**Do NOT revert AUTO battle-suppressor cap to 0.** Aaron's 2026-05-13 directive.

---

## Catalog of known fieldIds for geometric-trigger destinations

- **Fire Cavern A** (approach field, world-map trigger): `fieldId=0x0088`, engine `fieldName='bdview1'`. Trigger position ≈ (30260, -29221).
- **Balamb Town gate** (planner destination, not geometric): `fieldId=0x006A`, fieldName=`bcgate_1`. Trigger position ≈ (12894, -26776).

---

## Session ritual & rules

- Read **`DEVNOTES.md`** and **`NEXT_SESSION_PROMPT.md`** at session start.
- Update both at every version bump AND after every BAT result.
- **Filesystem MCP for all Windows project files.** Bash runs in a Linux container that can't reach the OneDrive mod directory.
- **Aaron pushes via `Utilities/push_to_github.ps1`**, Claude NEVER pushes.
- F-key handlers gated on `!(GetAsyncKeyState(VK_MENU) & 0x8000)`.
- F12 reserved for per-session diagnostics.
- **NEVER re-enable SET3 hook (0x1E)** — CI guard.
- DEVNOTES under 10 KB. When this file approaches the limit, move completed-chapter material to `DEVNOTES_HISTORY.md`.
- `deploy.bat` version-extract regex requires `/B` anchor (v0.15.10.1).
- **`.inl` textual-include pattern** for source splitting; no `deploy.bat` change needed (only the parent `.cpp` is compiled).
- **Inline-changelog accretion is dead** (retired v0.15.12.0). Canonical changelog is `CHANGELOG.md`.
- **F11 screenshots are gold for BAT context.**
- **Diagnostic-feature gating pattern**: gate behind `#define X 0` instead of deleting.
- **Source file size limits (v0.16.0 CI guard)**: 60 KB soft warning, 80 KB hard fail. Split before substantive edits cross the warning line.
- **Arrival detection needs VERIFICATION, not just signal-presence.**
- **Empirical-data capture (refined coords) needs the underlying decision VALIDATED before storage.**
- **Geometric-trigger vs script-trigger destinations need different navigation strategies.**
- **When "fixing" a planner decline, don't substitute a different region — that's the v0.14.95 mistake.**
- **Mid-drive replan must honor the same planner-eligibility gate as initial Start.**
- **Two-stage destination entry** (Fire Cavern, possibly other major dungeons): the world-map terrain trigger drops the player into an approach field, not the destination interior.
- **GitHub commit history is authoritative for "when did X change" questions.**
- **`ff8_nav_data.log` is the silent goldmine for spatial debugging.**
- **Aaron's domain knowledge is ground truth, but his recipes need empirical verification.**
- **Multiple catch sources on one field may not all be active.** Always verify the `[CBF] PASS` caller (`entityPtr=`) against the actual entity identity.
- **EWM is load-bearing.** Preserve "first-to-fill acts first, no skipped turns, natural ally/enemy ratio". Default to pure mechanical splits unless Aaron explicitly approves a refactor.
- **Battle menu TTS is also load-bearing** (v0.16.5). Every command, spell name, GF name, item with qty, target selection, all-target announce, Stock/Cast, cancel-restore is user-facing. Pure mechanical splits only.
- **Verifying user-facing features after a refactor requires comparing against a known-working baseline log.** Absence of an expected log line doesn't automatically mean the refactor broke it — it might be intermittent. If something looks suspicious, look at the install/resolution path first; if that fired, the runtime path is structurally identical.
- Every Claude response starts with `## Claude Says`.


---
