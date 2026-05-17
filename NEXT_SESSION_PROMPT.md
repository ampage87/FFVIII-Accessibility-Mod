# Next Session Prompt: v0.16.3 BAT triage, then v0.16.4 battle_tts_ewm.inl split

## Greeting

Start with `## Claude Says` per session ritual. Read `DEVNOTES.md` and THIS file before any work.

## Where we are

**GitHub HEAD = v0.16.2** (commit `7eb1ab1e`, pushed 2026-05-17 05:09:53 UTC, parent `5c08a1ae` = v0.16.1.4). **Local tree = v0.16.3, awaiting BAT.**

The X-ATM092 chase auto-pilot chapter is **closed**: v0.16.1.4 BAT confirmed clean end-to-end progression through all four post-bridge fields with zero catches.

### v0.16.3 (last session): field_archive_jsm.inl split, READY FOR BAT

`src/field_archive_jsm.inl` (91 KB monolith, over the 80 KB CI hard-fail) carved into a 2 KB slim shell + seven sub-`.inl` files. **Option B small refactor**, not pure mechanical:

1. Cross-pass `static` arrays inside `ScanJSMScripts()` (`s_methodMapjumps`, `s_entityReqs`, `s_entityPopms`, `s_initVarMaps`, `s_reqOpcodeCount`, `s_hasSetmodelInit`, `s_hasDialogAny`, `s_hasExtDispatchArr`) and their containing structs (`MethodMapjump`, `ReqCallInfo`, `EntityReqs`, `EntityPopms`, `VarWrite`, `EntityVarMap`) hoisted to namespace scope. Function-local `static` already has program lifetime so this is a visibility change only; explicit `memset` block at scan entry preserves the zero-on-entry contract.
2. Director DIAGNOSTIC + post-pass blocks extracted verbatim into a new `RunDirectorDetection()` helper. `ScanJSMScripts()` calls it as one line after the draw-point trigger cross-reference.

Behavior byte-for-byte identical to v0.16.2.

Include chain (dependency-ordered, included textually from the slim parent inside `namespace FieldArchive`):

```
state → constants → helpers → opnames → director → scan → dump
```

File sizes:
- `field_archive_jsm_state.inl` 4.4 KB
- `field_archive_jsm_constants.inl` 6.5 KB
- `field_archive_jsm_helpers.inl` 2.1 KB
- `field_archive_jsm_opnames.inl` 2.6 KB
- `field_archive_jsm_director.inl` 10.4 KB
- `field_archive_jsm_scan.inl` **63.3 KB** (just over 60 KB warn, well under 80 KB fail)
- `field_archive_jsm_dump.inl` 7.1 KB
- `field_archive_jsm.inl` (slim shell) 2.3 KB

`scan.inl` at 63 KB is in the watch zone. Further splitting would require breaking the per-entity opcode scan loop into sub-helpers — crosses from mechanical extraction into behavior-touching refactor. Deferred until there's a functional reason. CHANGELOG.md and DEVNOTES.md both call this out.

### Status check at session open

**If Aaron's first message is "BAT" or "Built and tested"**: triage the v0.16.3 BAT result:

1. Read `Logs/build_latest.log` tail first — confirm compile clean.
2. If compile errors: focus on those. Likely places for issues:
   - Forward declaration mismatch in `field_archive_jsm_state.inl` (signature of `RunDirectorDetection` must match the body in `director.inl`).
   - Namespace-scope statics colliding with something else in `field_archive.cpp` (unlikely — checked at split time, no conflicts).
   - Include order: `state.inl` must come first.
3. If compile clean: read `Logs/ff8_field.log` tail. Look for `[JSMScan]` lines (per-field scan summaries) and `[DIR-DIAG]` / `[DIRECTOR]` lines (Director detection). Compare against v0.16.2 baseline behavior — same fields should produce the same classification counts.
4. If runtime regressions show up (entities misclassified, Directors not detected, etc.): the most likely cause is a typo in the memset block or the helper call. Walk the diff carefully.

**If Aaron's first message is "Begin v0.16.4" or pushes ahead without BAT comment**: assume v0.16.3 BAT cleared (or Aaron will catch a regression later); proceed to the v0.16.4 plan below.

**If Aaron asks about a different regression**: read the relevant domain log first, not assumptions.

### v0.16.2 shipped (prior session)

`src/field_dialog.cpp` (88 KB monolith) → 3 KB slim parent + 8 `.inl` files. Pure mechanical. BAT clean 2026-05-16 23:00.

## Next priority: v0.16.4 = battle_tts_ewm.inl split

`src/battle_tts_ewm.inl` is **89.64 KB**, the largest remaining source file over the 60 KB warn line.

### Important note on type

Unlike v0.16.3, this is a **regular `.inl` textually included from `battle_tts.cpp`** — same pattern as v0.16.0/v0.16.1/v0.16.2's `.cpp` splits, except the parent is a `.cpp` that compiles and `battle_tts_ewm.inl` is one of several `.inl`s already living inside it. The split will produce sub-`.inl`s included from `battle_tts_ewm.inl`, identical structurally to what v0.16.3 just did for `field_archive_jsm.inl`.

### Recipe

1. Read `src/battle_tts.cpp` first to see how `battle_tts_ewm.inl` is currently included and what other `.inl`s are in play. Look for include order constraints.
2. Read `src/battle_tts_ewm.inl` end-to-end (use head/tail for the 90 KB if filesystem MCP truncates). Map functional groupings: typedefs/state, EWM mode toggle and lifecycle, ATB freeze/unfreeze logic, action queue, turn-order decision logic, debug/diag, anything else.
3. **Default to pure mechanical split** unless Aaron says otherwise. EWM is a load-bearing subsystem — Aaron's previous instruction was to preserve "first-to-fill acts first, no skipped turns, natural ally/enemy ratio". Don't refactor behavior under the guise of a split.
4. Create `battle_tts_ewm_state.inl` first (all statics + typedefs + constants). State always first.
5. Carve the rest as the groupings suggest. Aim for 5-25 KB per sub-`.inl`. Reasonable splits to consider: `_lifecycle`, `_atb_freeze`, `_action_queue`, `_turn_order`, `_diag`. Confirm with Aaron before committing to names.
6. Rewrite `battle_tts_ewm.inl` as a slim shell: `#include` chain of the new sub-`.inl`s. Keep the original orientation comment block at the top.
7. **`deploy.bat` unchanged** — only `battle_tts.cpp` compiles; `.inl`s are textual.
8. **No functional change.** Aaron BATs: trigger any battle, confirm EWM (O key toggle) still works, confirm turn order announces correctly, confirm ATB freezes during menus.

### Key gotchas (carried from v0.16.0–v0.16.3)

- `.inl` files: **NO header guards, NO namespace declarations inside.** They live inside `namespace BattleTTS` via the textual include from `battle_tts.cpp`.
- State `.inl` MUST be included FIRST.
- **Filesystem MCP for all Windows project files.** Bash runs in a Linux container that can't reach the OneDrive mod directory.
- **OneDrive sync EPERM rename errors**: retry immediately on first edit. Usually clears.
- Watch the 60 KB warn / 80 KB fail thresholds. If a sub-`.inl` approaches 60 KB, split further. (v0.16.3 `scan.inl` at 63 KB is an accepted exception, not a precedent.)
- Forward declarations for cross-`.inl` references go in `*_state.inl`.
- **Don't introduce comment/whitespace changes beyond what's necessary** (don't replace em-dashes with hyphens unless encoding requires it).

### Reference splits (the working models)

- **v0.16.0 world_map.cpp** — slim `.cpp` parent + sub-`.inl` chain.
- **v0.16.1 chase_auto_pilot.cpp** — 6.47 KB slim parent + 8 `.inl`: state (15.67), route (24.09), io (5.95), helpers (6.81), diag (5.23), bridge (7.06), engage (11.27), update (17.64).
- **v0.16.2 field_dialog.cpp** — 3 KB slim parent + 8 `.inl`: state (~11), helpers (~6), scan (~11), show_dialog (~11), opcodes (~13), diag (~15), menuname (~6), lifecycle (~14).
- **v0.16.3 field_archive_jsm.inl** (just shipped) — 2 KB slim shell + 7 `.inl` as listed above. Used small-refactor pattern (state hoist + helper extraction); v0.16.4 should default to pure mechanical unless Aaron approves something more invasive.

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
- Every Claude response starts with `## Claude Says`.

## Refactor queue after v0.16.4

- **v0.16.5**: split `src/battle_tts_menu.inl` (82 KB).

After v0.16.5 all source files are at or under the 80 KB hard fail (with only `field_archive_jsm_scan.inl` and possibly a few others in the watch zone), and the refactor chapter closes. Then the backlog below opens.

## Key lessons carried forward (from the chase chapter)

1. **`ff8_nav_data.log` is the silent goldmine for spatial debugging.** It logs every player triangle change as `[timestamp] COORD field tri X Y ...` regardless of auto-pilot state — including manual runs.
2. **Aaron's domain knowledge is ground truth, but his recipes need empirical verification.** Recipes point direction; position traces give magnitudes.
3. **Multiple catch sources on one field may not all be active.** Always verify the `[CBF] PASS` caller (`entityPtr=`) against the actual entity identity.
4. **Per-field problems require per-field analysis.** The robot's position resets at every field boundary.

## Backlog (after the size-split queue clears, v0.16.6+)

Roughly in priority order:

1. **POLL teardown garble** (carried from v0.16.2 BAT): polling-thread fallback occasionally speaks `[Name80]kindrL`-style fragments ~17s after dialog dismiss. Pre-existing behavior. Fix candidate: reject unresolved `[…]` tokens in the POLL path.
2. Remove party members from field entity catalog.
3. Walk-and-talk dialog gap (hardcoded engine path).
4. SeeD rank bug #27 (hypothesis: `FIELD_H_OFFSET = 0xF94` wrong section size).
5. Refined-coord narrow-gate steering.
6. Fire Cavern #28 + planner-fallback #29.
7. Per-world-map vehicle-aware BFS, guided GPS mode.
8. Battle: Scan TTS keys 9/0 (status resist/active statuses) — offset hunt deferred.
9. Future: Junction menu TTS, more victory screen polish.
