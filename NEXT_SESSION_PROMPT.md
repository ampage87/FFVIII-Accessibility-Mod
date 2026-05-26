# Next Session Prompt: v0.17.8.7 BAT triage + the runtime confirmation/disk layer

## Greeting

Start every response with `## Claude Says`. Read `DEVNOTES.md` and THIS file before any work.

## Where we are at session open

**v0.17.8.7 IS IN TREE, awaiting BAT.** `FF8OPC_VERSION` = `0.17.8.7`; CHANGELOG top heading matches. GitHub HEAD = v0.17.8.6 (`e415be44`, pushed and BAT-confirmed). Aaron pushes via `Utilities/push_to_github.ps1` — **Claude never pushes.** Diagnostic-only builds stay LOCAL (never pushed).

v0.17.8.7 adds ONE thing: a static **test-battle reference filter** that stops debug leftover entities (e.g. bgroad_5 `ent20 'cardgamemaster'`, the `cardmaster` phantom) from being promoted to INTERACTIVE_OBJECT and surfaced in the catalog. The larger runtime layer was deliberately NOT bundled so this filter can BAT in isolation.

### What changed in v0.17.8.7 (files)

- `field_archive_jsm_scan.inl` — new `static bool EntityRefsTestField(int e)` helper (just above `ScanJSMScripts`): true if any `s_initVarMaps[e].writes[].value` resolves via `GetFieldNameById` to a name starting with `testbl`. Guard added to the v0.07.98 INTERACTIVE_OBJECT promotion AND the v0.08.01 paired-entity inheritance promotion.
- `field_archive_jsm_state.inl` — forward declaration of `EntityRefsTestField` (next to the `RunDirectorDetection` forward decl), so the earlier-included `director.inl` can call it.
- `field_archive_jsm_director.inl` — same guard in the Director promotion loop (after the `camera` filter). **This one is essential:** the main-scan guard leaves the entity UNKNOWN, and without this the Director post-pass would re-promote it straight back.
- `src/ff8_accessibility.h` — version `0.17.8.7`. `CHANGELOG.md` — v0.17.8.7 entry on top.

## Step 1: BAT the filter

Ask Aaron to build, reload **bgroad_5** (B-Garden dormitory corridor), and cycle the catalog with F9.

PASS criteria:
- The `Card Player` / `cardmaster` entry is GONE. Catalog lists only the two real exits (Dormitory Double 1, Hall 10).
- Field log shows `ent20 'cardgamemaster' NOT promoted to INTERACTIVE_OBJECT: init-var writes reference a test-battle field` and no `[refresh] JSM-injected Card Player at (607,334)`.

Regression check (important): on a dormitory/classroom field with a real Director (bed, desk, wardrobe, the B-Garden Directory `dic`), those real interactables MUST still appear. If a real interactable vanished, an entity is coincidentally writing a `testbl*` field id — capture its `[JSMScan] ... NOT promoted` log line and tighten the helper (e.g. require the write to go to the field-jump var / require >1 testbl ref).

If PASS: mark v0.17.8.7 ✅ in DEVNOTES; Aaron pushes (`Utilities/push_to_github.ps1`). Then proceed to Step 2.

## Step 2 (next chapter): runtime dialog-confirmation + disk persistence

This is the larger piece Aaron approved and explicitly wants persisted across restarts. It is the general, principled answer to phantom/missed interactive objects, complementing the static filters (extDisp lines from v0.17.8.6, test-field suppression from v0.17.8.7).

**Goal.** An entity that actually fires MES/ASK/AMES/AASK at runtime is interactive, period. Record that fact, persist it to disk per-field, and re-apply it to the catalog on every load. Conversely, a surfaced object the player reaches that NEVER fires dialog can be demoted.

**Design (validated against the real code earlier):**
- The six dialog hooks in `field_dialog_opcodes.inl` (`Hook_opcode_mes/mesw/ask/ames/aask/amesw`, each `__cdecl Hook(int entityPtr)`) each call a new `FieldNavigation::NoteRuntimeDialogEntity(entityPtr)`.
- Resolve `entityPtr` to a stable identity: match against `s_capturedLines[].entityAddr` (a Line, e.g. the dorm bed) using the proven runtime stride `0x1A0`, or against the runtime others/background array range (`FF8Addresses::pFieldStateOthers`, ENTITY_STRIDE) for a bg/other entity. Stable key = fieldName + lineOrder (lines) / entity index (others).
- Persist the confirmed set to a DISK file (per-field; survives restart; becomes a shippable known-objects DB). Need to find the codebase's existing file-I/O pattern (look at how Logs/CameraFiles are written) and pick a path under the mod folder.
- `RefreshCatalog` (`field_nav_catalog.inl`) re-applies the set each build: surface confirmed entities as Interactions at their SETLINE-center / struct position.
- Purposes: (a) catch interactive objects the static extDisp proxy misses, (b) confirm/label static guesses, (c) DEMOTE static phantoms the player reaches that never fire dialog (general catch-all for non-debug story-dormant NPCs).

**UX constraint (Aaron, firm):** detection MUST surface real objects BEFORE they are triggered — runtime-only is useless for FINDING things. So static pre-detection always leads; this runtime layer is the refinement/persistence/pruning layer.

**Files:** `field_dialog_opcodes.inl`, `field_navigation.h` (decl), `field_navigation.cpp` (registry + `NoteRuntimeDialogEntity` + re-apply at RefreshCatalog start + disk read/write), `field_nav_catalog.inl` (surface confirmed entities).

Also consider, when convenient (alt root-cause for the whole phantom class): gate `ResolveStructPositions` (`field_nav_catalog_lateres.inl`) against the live `*pFieldStateOtherCount` instead of the hardcoded `sIdx >= 31`, so beyond-active-window stale struct positions don't surface phantoms generally. Verify first that it doesn't regress legit beyond-window save/draw points (e.g. Fire Cavern `drpoint`).

## Deferred backlog (unchanged)

Fire Cavern: #1 Quistis FMV premature; #7 Laguna dream field-nav broken (player=ent-1, `setpc==0` detection fails on gwgrass1); #8 Laguna dream battle announces real party not Laguna/Kiros/Ward. (#2/#3/#4/#5/#6 closed.) Steam 2013 savemap header = 76 bytes (0x4C), not 96 — community offsets need −0x14 (battle-side).

## Working rules (carry forward)

- **Mod files are on Windows** under `FF8_OriginalPC_mod/`. Use `filesystem:`-prefixed MCP tools. Bare bash/view hit a different Linux container — NOT OneDrive. Aaron's uploaded logs land at `/mnt/user-data/uploads/` (Linux side) — read those with bash/view, not filesystem MCP.
- **Claude never pushes.** Aaron BATs, then pushes via `Utilities/push_to_github.ps1`. The utility refuses to push unless `FF8OPC_VERSION` == the top `## vX.Y.Z` heading in `CHANGELOG.md`.
- **80 KB hard limit per source file.** `field_archive_jsm_scan.inl` is the big one — check its size after edits.
- **`filesystem:edit_file` corrupts a file when the replacement text contains a literal dollar-sign** — it truncates and duplicates. Use the hex literal `0x24` in source, or rewrite the whole file with `filesystem:write_file`. (OneDrive also occasionally throws a transient EPERM on edit_file rename — just retry once.)
- Aaron is blind, uses NVDA. Give instructions that need no sighted spot-checking — he sends logs and F11 screenshots (`Logs/screenshots/f11_HHMMSS_mmm.png`, read with `filesystem:read_media_file`) and Claude reads them.
- F-keys: F5 repeat dialog; F9 nearest/cycle catalog; F10 player field+pos; F11 screenshot; F12 session diagnostics.

## Session checkpoint rule

After implementing a build: bump `FF8OPC_VERSION`, add the top `## vX.Y.Z` CHANGELOG entry, update `DEVNOTES.md`, rewrite this file for the BAT-triage step. After BAT: mark the version ✅ in DEVNOTES; Aaron pushes.
