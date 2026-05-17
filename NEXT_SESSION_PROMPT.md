# Next Session Prompt: v0.16.4 BAT triage OR v0.16.5 battle_tts_menu.inl split

## Greeting

Start with `## Claude Says` per session ritual. Read `DEVNOTES.md` and THIS file before any work.

## Where we are

**GitHub HEAD = v0.16.3** (commit `8a7a23d1`, pushed 2026-05-17 05:58:34 UTC). **Local tree = v0.16.4 awaiting BAT.**

The X-ATM092 chase auto-pilot chapter is **closed**: v0.16.1.4 BAT confirmed clean end-to-end progression with zero catches.

### v0.16.4 (last session): battle_tts_ewm.inl split WRITTEN, awaiting BAT

`src/battle_tts_ewm.inl` (91.79 KB monolith, over the 80 KB CI hard-fail) carved into a 2.17 KB slim shell + **nine** sub-`.inl` files. **Pure mechanical split** — no behavior change. EWM is load-bearing for the turn-based retrofit, so every `__try` block, comment, and static was preserved verbatim; only locations moved.

Include chain (dependency-ordered, included textually from the slim parent inside `namespace BattleTTS`):

```
state → gf_patch → gf_effect → bp_diag → atb_hook → dispatch → ffnx → diag → update
```

File sizes: state 8.4 KB, gf_patch 8.9 KB, gf_effect 6.9 KB, bp_diag 17.1 KB, atb_hook 12.3 KB, dispatch 5.7 KB, ffnx 9.7 KB, diag 12.0 KB, update 13.8 KB, slim shell 2.2 KB. Largest sub-file is `bp_diag.inl` at 17.1 KB — well under the 60 KB warn line.

`atb_hook.inl` folds in the EWM lifecycle (`EWM_LoadConfig`/`SaveConfig`/`PollToggle`/`InstallHook`) because `EWM_InstallHook` installs `HookedATBUpdate` — they belong together.

**`battle_tts.cpp` unchanged.** It still `#include`s `battle_tts_ewm.inl`. `OnBattleEnter`, `Initialize`, `Shutdown` still reference statics declared in `state.inl` (e.g. `s_gfVEHHandle`, `s_gfSnapValid`, `s_gfAutoArmDone`, `s_tgtDiagStage`) — file-scope visibility carries across the textual-include boundary. `deploy.bat` unchanged.

## Status check at session open

**If Aaron's first message is "BAT"** (the expected case): the v0.16.4 build has been built and tested.

Triage workflow:
1. Read `Logs/build_latest.log` tail for compile errors. If anything's wrong, it'll be a transcription mistake in the split — most likely a static referenced before declared (wrong include order) or a missing `__try`/`__except` brace from a copy-paste edge.
2. If build succeeds, read `Logs/ff8_battle.log` for the runtime BAT.
3. Verify:
   - `BattleTTS: [EWM] ATB hook @ 0x... — MH_OK` at battle entry.
   - EWM O-key toggle: pressing O announces "Enhanced Wait Mode on" / "off" and logs `BattleTTS: [EWM] Toggled: ...`.
   - Command menu opens → `BattleTTS: [EWM] ATB capped (new turn, char=N, phase=N)` line fires.
   - Player commits action → cap releases (`[EWM] ATB cap released`).
   - GF summons fire correctly (Quezacotl, Shiva, etc.) — the v0.10.91 GF-fire prevention is still in `gf_patch.inl` + `atb_hook.inl`, must not have regressed.
   - `[TURN-COUNT]` lines appear on each entity turn (v0.13.58-60 per-slot ATB counter in `diag.inl`).
4. If the BAT is clean, Aaron pushes v0.16.4 via `Utilities/push_to_github.ps1`. Move on to v0.16.5.
5. If the BAT shows any deviation from v0.16.3 behavior, diff the affected sub-`.inl` against the v0.16.3 monolith via `git show 8a7a23d1:src/battle_tts_ewm.inl` and fix the transcription error.

**If Aaron's first message is "Begin v0.16.5" or similar**: v0.16.4 has pushed cleanly. Proceed to the v0.16.5 plan below.

**If Aaron asks about a regression not related to v0.16.4**: read the relevant domain log first, not assumptions.

## Next priority (after v0.16.4 BATs clean): v0.16.5 = battle_tts_menu.inl split

`src/battle_tts_menu.inl` is **81.89 KB**, the last source file over the 60 KB warn line (excluding the accepted `field_archive_jsm_scan.inl` exception at 63 KB).

### Important note on type

Same as v0.16.4: `battle_tts_menu.inl` is a **regular `.inl` textually included from `battle_tts.cpp`**. The split will produce sub-`.inl`s included from `battle_tts_menu.inl`, structurally identical to what v0.16.4 just did for `battle_tts_ewm.inl`.

### Recipe

1. Read `src/battle_tts.cpp` to see where `battle_tts_menu.inl` sits in the include chain (it comes AFTER `battle_tts_ewm.inl` — the orphan section header at the end of `update.inl` calls out the boundary: "Turn announcement + Command menu TTS").
2. Read `src/battle_tts_menu.inl` end-to-end (use head/tail for the 82 KB if filesystem MCP truncates). Map functional groupings: command menu state, turn announcement, target selection, sub-menus (Magic/GF/Item/Draw), Limit Break handling, anything else.
3. **Default to pure mechanical split** unless Aaron says otherwise. Battle TTS is user-facing — preserve every announcement exactly.
4. Create `battle_tts_menu_state.inl` first (all statics + typedefs + constants).
5. Carve the rest as the groupings suggest. Aim for 5-20 KB per sub-`.inl`. Reasonable splits: `_turn_announce`, `_command_menu`, `_target_select`, `_submenus` (or per-submenu if any one is large), `_limit_break`, `_diag`. Confirm names with Aaron before committing.
6. Rewrite `battle_tts_menu.inl` as a slim shell: `#include` chain. Keep original orientation comment block.
7. **`deploy.bat` unchanged** — only `battle_tts.cpp` compiles.
8. **No functional change.** BAT: trigger battles, confirm turn-start announcements, command menu navigation (arrow keys), target selection (with multi-target arrows), submenu navigation (Magic, GF, Item, Draw), Limit Break announcement.

### Key gotchas (carried from v0.16.0-v0.16.4)

- `.inl` files: **NO header guards, NO namespace declarations inside.** They live inside `namespace BattleTTS` via the textual include from `battle_tts.cpp`.
- State `.inl` MUST be included FIRST.
- **Filesystem MCP for all Windows project files.** Bash runs in a Linux container that can't reach the OneDrive mod directory.
- **OneDrive sync EPERM rename errors**: retry immediately on first edit. Usually clears.
- Watch the 60 KB warn / 80 KB fail thresholds.
- Cross-`.inl` statics go in `*_state.inl`.
- **Don't introduce comment/whitespace changes beyond what's necessary.**
- **Statics in `state.inl` that are referenced by `battle_tts.cpp` itself** (OnBattleEnter, Initialize, Shutdown resets) remain visible across the textual-include boundary — no special handling needed, just verify they're still file-scope `static`.

### Reference splits (the working models)

- **v0.16.0 world_map.cpp** — slim `.cpp` parent + sub-`.inl` chain.
- **v0.16.1 chase_auto_pilot.cpp** — 6.47 KB slim parent + 8 `.inl`.
- **v0.16.2 field_dialog.cpp** — 3 KB slim parent + 8 `.inl`.
- **v0.16.3 field_archive_jsm.inl** — 2 KB slim shell + 7 `.inl`. Small-refactor pattern (state hoist + helper extraction).
- **v0.16.4 battle_tts_ewm.inl** (just shipped) — 2.17 KB slim shell + 9 `.inl`. Pure mechanical. Closest reference model for v0.16.5 since they're sibling `.inl`s in the same parent (`battle_tts.cpp`).

## Hard constraints (unchanged)

- **Filesystem MCP for all Windows project files.**
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

## After v0.16.5

The refactor chapter closes. The allowlist in `.github/workflows/safety-checks.yml` can be emptied (only `field_archive_jsm_scan.inl` at 63 KB remains in the watch zone; accepted exception). Then the backlog below opens.

## Key lessons carried forward

1. **`ff8_nav_data.log` is the silent goldmine for spatial debugging.**
2. **Aaron's domain knowledge is ground truth, but his recipes need empirical verification.**
3. **Multiple catch sources on one field may not all be active.** Always verify the `[CBF] PASS` caller.
4. **Per-field problems require per-field analysis.**
5. **EWM is load-bearing.** Preserve "first-to-fill acts first, no skipped turns, natural ally/enemy ratio". Default to pure mechanical splits unless Aaron explicitly approves a refactor.
6. **Pure mechanical splits avoid behavior regression risk.** v0.16.4 deliberately did NOT touch v0.13.57 ATB-restore semantics or v0.13.55-56 dispatch hooks — they're load-bearing for the EWM contract.

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
