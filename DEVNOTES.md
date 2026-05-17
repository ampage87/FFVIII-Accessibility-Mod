**Purpose & context**

Aaron is the sole developer of the FF8 Accessibility Mod — a `dinput8.dll` injection for Final Fantasy VIII Steam 2013 (App ID 39150, FF8_EN.exe + FFNx v1.23.x) that makes the game playable for blind players via Windows SAPI TTS, navigation assistance, and entity catalogs. Aaron is blind and uses NVDA as his screen reader, and is also the primary tester.

**Project root:** `C:/Users/ampag/OneDrive/Documents/FFVIII-Accessibility-Mod/FF8_OriginalPC_mod/`

GitHub: `ampage87/FFVIII-Accessibility-Mod`. **GitHub HEAD = v0.16.3** (commit `8a7a23d1`, pushed 2026-05-17 05:58:34 UTC). **Local tree at v0.16.4 awaiting BAT.**

---

## Current state: v0.16.4 battle_tts_ewm.inl split — LOCAL, awaiting BAT

The X-ATM092 chase auto-pilot chapter is **closed**. v0.16.1.4 BAT cleared doopen2a in ~5 seconds and progressed cleanly through the chase climax with zero `[CBF] PASS` catches.

**v0.16.4 written locally (not yet BAT'd):** `src/battle_tts_ewm.inl` (91.79 KB monolith, over the 80 KB CI hard-fail) carved into a 2.17 KB slim shell + **nine** sub-`.inl` files. **Pure mechanical** split — no behavior change. EWM is load-bearing for the turn-based retrofit, so every `__try` block, comment, and static was preserved verbatim; only locations moved.

Include chain (dependency-ordered, included textually from the slim parent inside `namespace BattleTTS`, which itself is included from `battle_tts.cpp`):

```
state → gf_patch → gf_effect → bp_diag → atb_hook → dispatch → ffnx → diag → update
```

`state.inl` must come first (declares every static). `update.inl` must come last (`EWM_UpdateBattle` calls helpers from `gf_patch.inl` and `diag.inl`). The `atb_hook.inl` file folds in the EWM lifecycle (`EWM_LoadConfig`, `EWM_SaveConfig`, `EWM_PollToggle`, `EWM_InstallHook`) because `EWM_InstallHook` installs `HookedATBUpdate` — they conceptually belong together.

File sizes: state 8.4 KB, gf_patch 8.9 KB, gf_effect 6.9 KB, bp_diag 17.1 KB, atb_hook 12.3 KB, dispatch 5.7 KB, ffnx 9.7 KB, diag 12.0 KB, update 13.8 KB, slim shell 2.2 KB. Largest sub-file is `bp_diag.inl` at 17.1 KB — comfortably under the 60 KB warn line. Total split is 96.8 KB vs original 91.8 KB; the ~5 KB overhead is per-file orientation comment headers explaining each module's role.

**`battle_tts.cpp` unchanged.** It still `#include`s `battle_tts_ewm.inl` exactly as before; the textual include simply expands into nine sub-files. `OnBattleEnter`, `Initialize`, `Shutdown` still reference statics declared in `state.inl` (e.g. `s_gfVEHHandle`, `s_gfSnapValid`, `s_gfAutoArmDone`, `s_tgtDiagStage`) — file-scope visibility carries across the textual-include boundary. `deploy.bat` unchanged.

### What Aaron's BAT will verify

Trigger any battle. Confirm:
- Build compiles (`Logs/build_latest.log` tail).
- EWM O-key toggle still works (announces "Enhanced Wait Mode on" / "off").
- ATB freezes during command menus.
- Turn order announces correctly.
- GF summons (Quezacotl, Shiva, Ifrit, etc.) still fire without the v0.10.91 GF-fire bug recurring.
- v0.13.58 per-slot turn counter still logs `[TURN-COUNT]` on each entity turn.

Any deviation from v0.16.3 behavior means a transcription error in the split — re-examine the affected sub-`.inl` against the v0.16.3 monolith via `git show 8a7a23d1:src/battle_tts_ewm.inl`.

**v0.16.3 shipped (commit `8a7a23d1`):** `src/field_archive_jsm.inl` split (91 KB → 2 KB slim shell + 7 sub-`.inl`). Strategy was Option B small refactor (state hoist + `RunDirectorDetection` helper extraction). BAT cleared on `bgryo1_1`. The full v0.16.3 narrative is in `DEVNOTES_HISTORY.md`.

The full v0.16.1.x chase chapter, v0.16.0.x world_map split + Parts B/C + Fire Cavern fixes, and other completed chapters are archived in **`DEVNOTES_HISTORY.md`**. Consult only if a regression surfaces.

### Diagnostic logging still in place

`ReadBattleyarouPosition` SEH-guarded helper in `chase_auto_pilot_io.inl`, `by=(X,Y) bydist=N` per-tick suffix on the `ChaseAutoPilot::tick` log paths, `_ReturnAddress()` capture on `[CBF] PASS` line in `chase_battle_freeze.cpp`. Baseline for any future chase regression — don't strip.

## Active refactor queue

After v0.16.4 BATs clean and pushes:

1. **v0.16.5**: split `src/battle_tts_menu.inl` (82 KB). **Final size-split task.** Pattern matches v0.16.4 (regular `.inl` shell + sub-`.inl` chain).

After v0.16.5 ships, every source file in the project is under the 80 KB CI hard fail (with `field_archive_jsm_scan.inl` at 63 KB an accepted watch-zone exception), the allowlist in `.github/workflows/safety-checks.yml` can be emptied, and the refactor chapter closes.

Completed: v0.16.0 (`world_map.cpp`), v0.16.1 (`chase_auto_pilot.cpp`), v0.16.2 (`field_dialog.cpp`), v0.16.3 (`field_archive_jsm.inl`), v0.16.4 (`battle_tts_ewm.inl`).

Pattern (established in v0.16.0, refined through v0.16.4): parent `.cpp` (or shell `.inl`) becomes a slim file with namespace block + `#include` chain of `.inl` files + tiny public-API tail (when applicable). `*_state.inl` (statics) included FIRST. No header guards or namespace decls inside `.inl` files. `*_history.h` archive with `#if 0` wrapper holds removed legacy content (when applicable). Aim for 5-20 KB per `.inl`; resplit if any approaches 60 KB.

## Backlog (priority order, after the refactor queue)

1. **POLL teardown garble** (carried from v0.16.2 BAT): the polling-thread fallback occasionally speaks fragments like `[Name80]kindrL` ~17 seconds after a dialog dismiss. Pre-existing behavior. Fix candidate: reject text containing unresolved `[…]` tokens in the POLL path.
2. **`menu_tts.cpp` T-handler `!shift` gate**. One-line cleanup.
3. **FieldAnnounce display-name catalog audit** in `src/field_display_names.h`. Wrong mappings for fieldIds 0x0134 / 0x0136. Verify Fire Cavern A mapping (fieldId 0x0088, engine `fieldName='bdview1'`, expected "Fire Cavern A") end-to-end.
4. **Field-name populate race** at Part B arrival check — diagnostic log only, audio fine.
5. **Deep-research doc updates**: `Plan & Research Documents/Dollet timer countdown deep research results.md` — wrong-math fix + LIVE TIMER FOUND appendix.

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
- **Arrival detection needs VERIFICATION, not just signal-presence.** v0.14.96 fixed encounter false-positives; v0.16.0 Part B fixed off-target-field false-positives; v0.16.0.2 fixed icon-vs-trigger false-negatives via two-tier cap.
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
- Every Claude response starts with `## Claude Says`.


---
