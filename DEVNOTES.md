**Purpose & context**

Aaron is the sole developer of the FF8 Accessibility Mod — a `dinput8.dll` injection for Final Fantasy VIII Steam 2013 (App ID 39150, FF8_EN.exe + FFNx v1.23.x) that makes the game playable for blind players via Windows SAPI TTS, navigation assistance, and entity catalogs. Aaron is blind and uses NVDA as his screen reader, and is also the primary tester.

**Project root:** `C:/Users/ampag/OneDrive/Documents/FFVIII-Accessibility-Mod/FF8_OriginalPC_mod/`

GitHub: `ampage87/FFVIII-Accessibility-Mod`. **GitHub HEAD = v0.16.1.4** (commit `5c08a1a`, pushed 2026-05-16 21:57:55, tagged `v0.16.1.4`). **Local tree = v0.16.2** (field_dialog.cpp split, pending BAT before push).

---

## Current state: chase chapter closed; v0.16.2 split complete (pending BAT)

The X-ATM092 chase auto-pilot is **complete**. v0.16.1.4 BAT cleared doopen2a in ~5 seconds and progressed cleanly through dotown_3 → dotown_2 → dotown_1 to the chase climax with zero `[CBF] PASS` catches.

**v0.16.2 mechanical refactor (this session):** `src/field_dialog.cpp` split 88 KB monolith → 3 KB slim parent + 8 `.inl` files. Include chain in dependency order: state → helpers → scan → show_dialog → opcodes → diag → menuname → lifecycle. No functional change. `deploy.bat` unchanged (only the parent `.cpp` compiles). Pending Aaron BAT before push.

The full v0.16.1.x narrative (refactor scope, chase chapter, BAT empirical numbers, findings, file layout, open question) is archived in **`DEVNOTES_HISTORY.md`** at the top. Consult it only if a chase regression surfaces.

Likewise the v0.16.0.x world_map.cpp split + Parts B/C + Fire Cavern fixes are archived there.

### Diagnostic logging still in place

`ReadBattleyarouPosition` SEH-guarded helper in `chase_auto_pilot_io.inl`, `by=(X,Y) bydist=N` per-tick suffix on the `ChaseAutoPilot::tick` log paths, `_ReturnAddress()` capture on `[CBF] PASS` line in `chase_battle_freeze.cpp`. Baseline for any future chase regression — don't strip.

## Active refactor queue

Three source files remaining over the 60 KB CI warn line, ordered by size:

1. **v0.16.2**: split `src/field_dialog.cpp` (88 KB). **DONE this session** (pending BAT + push). 8 `.inl` files + 3 KB slim parent. Largest new file: `field_dialog_lifecycle.inl` ~12 KB.
2. **v0.16.3**: split `src/field_archive_jsm.inl` (91 KB). **Next up.**
3. **v0.16.4**: split `src/battle_tts_ewm.inl` (90 KB).
4. **v0.16.5**: split `src/battle_tts_menu.inl` (82 KB).

Note on v0.16.3 quirk: `field_archive_jsm.inl` is *already* an `.inl` (textually included from `field_archive.cpp`). The split will likely produce a set of sub-`.inl`s included by it, rather than a slim-parent-`.cpp` pattern. Confirm strategy at session start.

Pattern (established in v0.16.0 world_map.cpp split, refined in v0.16.1 chase_auto_pilot.cpp split, applied again in v0.16.2 field_dialog.cpp split): parent `.cpp` becomes a slim file with namespace block + `#include` chain of `.inl` files + tiny public-API tail. `*_state.inl` (statics) included FIRST. No header guards or namespace decls inside `.inl` files. `*_history.h` archive with `#if 0` wrapper holds removed legacy content (when applicable). Aim for 5-20 KB per `.inl`; resplit if any approaches 60 KB.

## Backlog (priority order, after the refactor queue)

1. **`menu_tts.cpp` T-handler `!shift` gate**. One-line cleanup.
2. **FieldAnnounce display-name catalog audit** in `src/field_display_names.h`. Wrong mappings for fieldIds 0x0134 / 0x0136. Verify Fire Cavern A mapping (fieldId 0x0088, engine `fieldName='bdview1'`, expected "Fire Cavern A") end-to-end.
3. **Field-name populate race** at Part B arrival check — DIAGNOSTIC LOG ONLY, audio is fine. v0.16.0.2 BAT caught the snapshot at `fieldName=''` for a 7-second drive; v0.16.0.3 BAT caught it at `fieldName='bdview1'` once the race resolved. Backlog action: either retry briefly in Part B before logging, or accept (fieldId is sufficient).
4. **Deep-research doc updates**: `Plan & Research Documents/Dollet timer countdown deep research results.md` — wrong-math fix + LIVE TIMER FOUND appendix.

### Future (deferred)

- **Refined-coord persistence** (JSON or %APPDATA% store so BAT-captured coords survive sessions, replacing per-destination hardcodes).
- **Other geometric-trigger destinations**: as v0.16.0.2's two-tier cap catches them on first arrival, add their refined coords to the Initialize() hardcode chain. Candidates: Centra Ruins, Tomb of the Unknown King, Cactuar Island, Shumi Village, Edea's House, Chocobo Forest entrances.
- **Engine-write hook for cleaner countdown freeze** (cosmetic ±1-sec flicker; not urgent).

### Deferred (don't pick without Aaron's direction)

- SeeD rank bug #27
- Walk-and-talk dialog gap
- Refined-coord narrow-gate steering (#29)
- `chase_diag::OnAskOpcodeFired` snprintf bug

**Do NOT revert AUTO battle-suppressor cap to 0.** Aaron's 2026-05-13 directive.

---

## Catalog of known fieldIds for geometric-trigger destinations

- **Fire Cavern A** (approach field, world-map trigger): `fieldId=0x0088`, engine `fieldName='bdview1'`. Trigger position ≈ (30260, -29221), ~6.5k units southwest of catalog icon (36864, -28672). v0.16.0.3 BAT captured the fieldName once the populate race resolved.
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
- **Empirical-data capture (refined coords) needs the underlying decision VALIDATED before storage.** Bad decisions self-reinforce otherwise (the Fire-Cavern-into-bggate_1 cascade).
- **Geometric-trigger vs script-trigger destinations need different navigation strategies.** The wmsetus planner only handles script-trigger destinations; geometric-trigger destinations (Fire Cavern, early-game Balamb Garden, likely Centra Ruins / Tomb / Cactuar Island) use simple-coord steering + engine terrain trigger via Part C, with the wider Part B cap on first arrival.
- **When "fixing" a planner decline, don't substitute a different region — that's the v0.14.95 mistake.** If the data says there's no scripted path, the right answer is fall back to non-planner logic, not invent a route the data doesn't support.
- **Mid-drive replan must honor the same planner-eligibility gate as initial Start.**
- **Two-stage destination entry** (Fire Cavern, possibly other major dungeons): the world-map terrain trigger drops the player into an approach field, not the destination interior. The refined coord for these destinations is the approach-field trigger position, several thousand units offset from the icon.
- **GitHub commit history is authoritative for "when did X change" questions.** Memory and DEVNOTES can drift; `list_commits` queries reveal exact regression points.
- **`ff8_nav_data.log` is the silent goldmine for spatial debugging.** Per-triangle player position logged regardless of auto-pilot state — including manual runs. Use whenever Aaron's manual play is the ground truth.
- **Aaron's domain knowledge is ground truth, but his recipes need empirical verification.** Recipes point direction; position traces give magnitudes.
- **Multiple catch sources on one field may not all be active.** Always verify the `[CBF] PASS` caller (`entityPtr=`) against the actual entity identity, not just the field's JSMScan listing.
- Every Claude response starts with `## Claude Says`.
