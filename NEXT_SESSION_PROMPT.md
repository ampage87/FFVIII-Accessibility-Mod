# Next Session Prompt: v0.16.5.1 ready to push, backlog open

## Greeting

Start with `## Claude Says` per session ritual. Read `DEVNOTES.md` and THIS file before any work.

## Where we are

**GitHub HEAD = v0.16.4** (commit `5d16179a`, pushed 2026-05-17 18:31 UTC). **Local tree = v0.16.5.1** (v0.16.5 BAT confirmed clean + 3-line follow-up patch for the deferred-turn release path; both pending push).

### v0.16.5 BAT result

Confirmed CLEAN. All menu paths exercised across two battles, every announcement fired as expected. Init: `Initialized v0.16.5 (EWM=ON, ATB=OK, GF=OK, FFNx=FAIL, PATCH=OK, BT=deferred)`. Highlights: Magic submenu via mode-byte path, GF submenu via fallback (mode 0x02→0x00 + phase 80) path, deferred GF cancel disambiguation (cancel suppressed on turn-end = confirm), Draw spell list with Stock/Cast + cursor nav + false-exit suppression + revisit-flag handling, target cycling across allies, v0.13.57 ATB exact-value restore preserved. **v0.16.4's open Ifrit-AD miss is RESOLVED** — played end-to-end this session (6 cues, ~23 s, all proper timestamps). Heartbeat diagnostic stays parked.

### v0.16.5.1: 3-line follow-up

v0.16.5 BAT log triage caught that `PollDeferredTurnAnnounce` (defined in `battle_tts_menu_poll.inl`) was never invoked from `Update()`. Latent dead-code path since v0.13.52 introduced the deferred-turn feature — not a v0.16.5 regression. Patched with three guarded lines in `battle_tts.cpp::Update()` after `PollHPChanges()`. Function body untouched.

Specific BAT evidence: line 2942–2943, Selphie's third turn in battle 2 started on the exact frame Zell's Ifrit began animating. `[TURN] Deferred (damage in flight): Selphie's turn. Attack.` logged correctly, but no `[TURN] Deferred fired ...` or `[TURN] Deferred cancelled ...` follow-up ever appeared — the stashed announce silently dropped at battle end. Aaron likely never heard "Selphie's turn. Attack." for that turn (and many similar collisions over the past ~32 versions).

### Push readiness

Both versions ready for one combined push. `FF8OPC_VERSION = "0.16.5.1"` matches the top heading in `CHANGELOG.md`. Run `Utilities/push_to_github.ps1`. The push will land both v0.16.5 (refactor) and v0.16.5.1 (follow-up patch) as one commit, with the v0.16.5.1 CHANGELOG section as the commit body (containing the v0.16.5.1 narrative + the v0.16.5 section below it).

## Status check at session open

**If Aaron's first message is "BAT"**: a build of v0.16.5.1 (or later) has been tested. Triage normally — read `Logs/build_latest.log` tail for compile errors, then `Logs/ff8_battle.log`. Specifically look for the v0.16.5.1 confirmation pattern: any `[TURN] Deferred (damage in flight): ...` line should be followed within ~5 seconds by either `[TURN] Deferred fired after <ms> ms: ...` (release succeeded) or `[TURN] Deferred cancelled (char N -> M, stale): ...` (active char advanced past the deferred-for character). Pre-fix, only the first line appeared.

**If Aaron names a backlog item**: jump to that. Refactor queue is empty; everything below is fair game.

**If Aaron asks about a regression**: read the relevant domain log first, not assumptions.

### Deferred-turn observation rule going forward

The one-frame trigger window means this collision can't be reliably reproduced. Aaron will keep an eye out for the post-fix pattern across future BAT runs. If `[TURN] Deferred fired ...` is observed at least once with damage-TTS-first / turn-announce-second audio ordering preserved, the fix is confirmed end-to-end. If `[TURN] Deferred (damage in flight)` ever appears WITHOUT a follow-up `fired` or `cancelled` line in the next ~5 seconds, that's a regression of the v0.16.5.1 fix (e.g. someone removed the `Update()` call).

## Refactor queue: EMPTY

v0.16.5 was the final size-split task. Every `src/*.cpp` and `src/*.inl` is now under the 80 KB hard fail. The chapter that started with v0.16.0's `world_map.cpp` carve is closed.

If a future file approaches the 60 KB warn line, the established `.inl` pattern is:
- Parent `.cpp` (or shell `.inl`) becomes a slim file with namespace block + `#include` chain
- `*_state.inl` (statics) included FIRST
- No header guards or namespace decls inside `.inl` files
- 5-20 KB per sub-`.inl` target; resplit if any approaches 60 KB
- `*_history.h` archive with `#if 0` wrapper holds removed legacy content when applicable
- Default to PURE mechanical splits — user-facing TTS paths in particular preserve every announcement exactly

The six reference splits: v0.16.0 (`world_map`), v0.16.1 (`chase_auto_pilot`), v0.16.2 (`field_dialog`), v0.16.3 (`field_archive_jsm` — small-refactor pattern with state hoist + helper extraction), v0.16.4 (`battle_tts_ewm`), v0.16.5 (`battle_tts_menu`).

## Backlog (now active, roughly priority order)

1. **Ifrit / GF audio description miss diagnostic** (carried from v0.16.4 BAT, RESOLVED in v0.16.5 BAT but kept as a watch item). Only act if it recurs.
2. **POLL teardown garble** (carried from v0.16.2 BAT): polling-thread fallback occasionally speaks `[Name80]kindrL`-style fragments ~17s after dialog dismiss. Pre-existing. Fix candidate: reject unresolved `[…]` tokens in the POLL path.
3. **`menu_tts.cpp` T-handler `!shift` gate** — one-line cleanup.
4. **FieldAnnounce display-name catalog audit** in `src/field_display_names.h`. Wrong mappings for fieldIds 0x0134 / 0x0136. Verify Fire Cavern A mapping (fieldId 0x0088, engine `fieldName='bdview1'`, expected "Fire Cavern A") end-to-end.
5. **Field-name populate race** at Part B arrival check — diagnostic log only, audio fine.
6. **Deep-research doc updates**: `Plan & Research Documents/Dollet timer countdown deep research results.md` — wrong-math fix + LIVE TIMER FOUND appendix.
7. Remove party members from field entity catalog.
8. Walk-and-talk dialog gap (hardcoded engine path).
9. SeeD rank bug #27 (hypothesis: `FIELD_H_OFFSET = 0xF94` wrong section size).
10. Refined-coord narrow-gate steering.
11. Fire Cavern #28 + planner-fallback #29.
12. Per-world-map vehicle-aware BFS, guided GPS mode.
13. Battle: Scan TTS keys 9/0 (status resist/active statuses) — offset hunt deferred.
14. Future: Junction menu TTS, more victory screen polish.

## Hard constraints (unchanged)

- **Filesystem MCP for all Windows project files.** Bash runs in a Linux container that can't reach the OneDrive mod directory.
- **Aaron pushes via `Utilities/push_to_github.ps1`**, Claude NEVER pushes.
- **NEVER re-enable SET3 opcode hook (0x1E)** — CI guard in `.github/workflows/safety-checks.yml`.
- **F-key handlers gated** on `!(GetAsyncKeyState(VK_MENU) & 0x8000)`.
- **F12 reserved** for per-session diagnostics.
- **Source file size limits**: 60 KB warn, 80 KB fail (CI enforced).
- **OneDrive sync EPERM**: retry immediately on first edit attempt.
- **AUTO `[CBF]` battle-suppressor cap stays `INT_MAX`** — Aaron's 2026-05-13 directive.
- **`.inl` files are TEXTUAL INCLUDES**: no header guards, no namespace declarations inside, `state.inl` always first.
- **Push utility refuses to push if top CHANGELOG heading doesn't match `FF8OPC_VERSION`.**
- **Battle menu TTS is load-bearing** (v0.16.5). Every command, spell, GF name, item with qty, target selection, all-target announce, Stock/Cast, cancel-restore is user-facing. Pure mechanical splits only.
- **Functions defined but not called are a real failure mode** (v0.16.5.1 lesson). MSVC silently allows `static` unused functions; the compiler can't warn us. If a feature's log marker for the "work started" half appears but the "work finished" half is missing across multiple BAT runs, suspect a missing caller before suspecting a logic bug.
- Every Claude response starts with `## Claude Says`.

## Key lessons carried forward

1. **`ff8_nav_data.log` is the silent goldmine for spatial debugging.**
2. **Aaron's domain knowledge is ground truth, but his recipes need empirical verification.**
3. **Multiple catch sources on one field may not all be active.** Always verify the `[CBF] PASS` caller.
4. **Per-field problems require per-field analysis.**
5. **EWM is load-bearing.** Preserve "first-to-fill acts first, no skipped turns, natural ally/enemy ratio". Default to pure mechanical splits unless Aaron explicitly approves a refactor.
6. **Battle menu TTS is also load-bearing.** Same constraint.
7. **Pure mechanical splits avoid behavior regression risk.** v0.16.5 deliberately did NOT split `PollTurnAndCommands` into helpers (shared locals + outer SEH); v0.16.4 deliberately did NOT touch v0.13.57 ATB-restore semantics.
8. **Absence of an expected log line after a refactor doesn't automatically mean the refactor broke it.** If the install/resolution line for that subsystem still fires, the runtime path is structurally identical and the cause is likely environmental or intermittent. v0.16.4's Ifrit-AD miss — confirmed resolved in v0.16.5's BAT — is the canonical example.
9. **Refactor BAT log triage is a free dead-code audit.** v0.16.5's mechanical split exposed v0.13.52's missing `PollDeferredTurnAnnounce` caller; the new sub-`.inl` boundaries made the "this function is defined but never called" pattern grep-friendly. Future refactors should explicitly check that every public function in a new `.inl` has a caller somewhere in the parent translation unit.
10. **When editing markdown via `filesystem:edit_file`**: multi-edit calls in one invocation can collide if `oldText` of edit N appears more than once after edit N-1 runs. Prefer separate calls for each independent edit, or carefully verify that the intermediate state of the file is what the next `oldText` expects.
