# Next Session Prompt: push v0.17.8.10, then bug #10 (Xu) model-id classifier

## Greeting

Start every response with `## Claude Says`. Read `DEVNOTES.md` and THIS file before any work.

## Where we are at session open

**v0.17.8.10 IS IN TREE, BAT-confirmed, ready to push.** `FF8OPC_VERSION` = `0.17.8.10`; CHANGELOG top heading matches. GitHub HEAD = v0.17.8.9 (`1c5f530e`, pushed + BAT-confirmed). Aaron pushes via `Utilities/push_to_github.ps1` — **Claude never pushes.** Diagnostic-only builds stay LOCAL.

v0.17.8.10 fixes **bug #9** (B-Garden hub missing its Hall 4 exit), BAT-confirmed 2026-05-27: Hall 4 now appears in the hub (`bghall_5`, "Hall 10"), Hall 6's exits unchanged. ONE change: the INF-gateway screen filter switched from the infinite-line `IsSeparatedByTriggerLine()` to a new bounded `SegmentsCross()` test, so a short screen-boundary line on a far edge no longer falsely "separates" a gateway on the opposite edge. Full root-cause in DEVNOTES (#9 entry) and CHANGELOG.

### What changed in v0.17.8.10 (files)

- `field_navigation.cpp` — new `static int Orient2D(...)` + `static bool SegmentsCross(...)` (proper bounded segment-vs-segment intersection), inserted right after `IsSeparatedByTriggerLine`, before the `field_nav_catalog.inl` include so the gateway block can call it.
- `field_nav_catalog.inl` — the INF-gateway screen filter now loops SCREEN_BOUND/UNKNOWN captured lines and drops a gateway only if the player->gateway SEGMENT crosses a line SEGMENT (was: `IsSeparatedByTriggerLine` infinite-line side test). Entity screen-filtering is UNCHANGED (still infinite-line). The v0.17.8.9-diag [gw-diag] logging was reverted.
- `field_archive_jsm_scan.inl` — the v0.17.8.9-diag [npc-class] dump was reverted (it had served its purpose for #10; see Step 2).
- `src/ff8_accessibility.h` — version `0.17.8.10`. `CHANGELOG.md` — v0.17.8.10 entry on top.

## Step 1: push v0.17.8.10

It is BAT-confirmed and the tree is clean (all diagnostics reverted). Aaron pushes via `Utilities/push_to_github.ps1`. After push, GitHub HEAD = v0.17.8.10.

## Step 2 (current chapter): bug #10 — Xu mislabeled "Interaction 3"

**What we know (this is solid):** Xu is a real walk-up-and-Confirm NPC. She is JSM `kanban2` (ent25, cat3, PSHM pos (4626,-3459)). The F11 screenshot `Logs/screenshots/f11_220226_490.png` shows her character model on the Hall 6 walkway; the dialog log shows her line. The "Director over-promotion / shu" hypothesis is dead.

**Why static signals alone failed (proven by the removed [npc-class] dump on bghall_3):** every cat-3 object is identical — `talkSetup=0` for EVERYTHING including Xu (her talk is runtime-dispatched via a bare 0x1C; she sets no static TALKRADIUS), and `setmodelInit=1` for Xu AND `water` AND `walllight`. So neither flag is a discriminator. The ONLY thing separating Xu from a sign/light/water is the MODEL she loads, which the JSM scan does not currently capture.

**Confirmed anchor:** the runtime classifier already labels live character models correctly. This run had a live `ent5 model=15` surfaced as "NPC" (cat1) right alongside kanban2's "Interaction 3" (cat7). Real character-model NPCs (model >= 10) classify fine at runtime; the gap is that at catalog-build time Xu is NOT an active runtime entity (she's Director-placed via PSHM, like signs/lights), so her model is never read.

**Plan (the model-id route — matches Aaron's "has a character model" intuition):**

1. **Capture SETMODEL's model-id operand statically.** In `field_archive_jsm_scan.inl`, where the scan already detects `opcode == JSM_OP_SETMODEL && m == 0` (sets `foundSetmodelInit`), also grab the operand pushed immediately before SETMODEL (the model index) and store it — add a field to `JSMEntityInfo` in `field_archive.h` (e.g. `int16_t setmodelId; // -1 = none`) or a parallel `s_setmodelId[128]` in `*_state.inl`.

2. **Diagnostic (read-only, LOCAL, no version bump):** dump per cat-3 entity its captured `setmodelId`, AND dump the runtime "others" array (index, SYM, runtime model-id, position) once on bghall_3. Two questions to answer:
   - Does `kanban2`'s SETMODEL load a character-range model (>= ~10, like ent5's 15), while `water` / `walllight` / signs load distinct prop ids? (Need a clean split.)
   - Is the runtime NPC `ent5 model=15` at the same position as `kanban2` (4626,-3459)? If yes, Xu is being surfaced TWICE (runtime "NPC" + JSM "Interaction 3") and the JSM injection should be deduped against the runtime entity. If ent5 is elsewhere, ent5 is a different student and Xu is specifically kanban2.

3. **If the model-id split is clean (pending the data):** classify a cat-3 INTERACTIVE_OBJECT whose `setmodelId` is in the character range as an NPC, so it surfaces as a person BEFORE the player reaches it (no caching, no runtime trigger needed — satisfies the find-before-approach rule). Also add the position-dedupe so an active Xu doesn't appear as both "NPC" and "Interaction 3".

**UX constraint (Aaron, firm):** detection MUST surface real NPCs BEFORE they are triggered. The static model-id is what makes that possible here; the runtime classifier is the confirmation/dedupe layer, not the primary find.

**Diagnostic discipline:** one change per BAT. F12 reserved for diagnostics (search/remove old `VK_F12` first). Remove the diagnostic before the #10 fix ships.

## Larger backlog piece (Aaron approved, deferred)

Runtime dialog-confirmation + DISK persistence — the general answer to Director over/under-promotion: the six dialog hooks call `NoteRuntimeDialogEntity(entityPtr)`, resolve to a stable per-field identity (line via `s_capturedLines[].entityAddr` + stride `0x1A0`, or others/bg array), persist to a per-field disk DB that survives restarts, and re-apply in `RefreshCatalog` to (a) catch objects the static proxy misses, (b) confirm static guesses, (c) demote reached-but-never-fired phantoms. Static pre-detection always leads; this is the refinement/persistence layer. Files: `field_dialog_opcodes.inl`, `field_navigation.h/.cpp`, `field_nav_catalog.inl`.

## Deferred backlog (unchanged)

Fire Cavern: #1 Quistis FMV premature; #7 Laguna dream field-nav broken (player=ent-1, `setpc==0` detection fails on gwgrass1); #8 Laguna dream battle announces real party not Laguna/Kiros/Ward. (#2/#3/#4/#5/#6 closed.) Steam 2013 savemap header = 76 bytes (0x4C), not 96 — community offsets need -0x14 (battle-side).

## Working rules (carry forward)

- **Mod files are on Windows** under `FF8_OriginalPC_mod/`. Use `filesystem:`-prefixed MCP tools. Bare bash/view hit a different Linux container — NOT OneDrive. Large `filesystem:read_text_file` results spill to `/mnt/user-data/tool_results/*.json` (Linux side) — extract with python/bash there.
- **Claude never pushes.** Aaron BATs, then pushes via `Utilities/push_to_github.ps1`. The utility refuses unless `FF8OPC_VERSION` == the top `## vX.Y.Z` heading in `CHANGELOG.md`. (GitHub MCP push tools may be visible in-session — do not use them.)
- **80,000-byte hard limit per source file** (CI-enforced). `field_archive_jsm_scan.inl` (~73.8 KB) and `field_nav_catalog.inl` (~72.6 KB) are the ones near the line — check sizes after edits. `field_nav_autodrive.inl` is pre-existing at 80,517 B (untouched).
- **`filesystem:edit_file` corrupts a file when the replacement text contains a literal dollar-sign** — it truncates and duplicates. Use hex `0x24` in source, or rewrite the whole file with `filesystem:write_file`. (OneDrive also occasionally throws a transient EPERM on edit_file rename — retry once.)
- Aaron is blind, uses NVDA. Give instructions that need no sighted spot-checking — he sends logs and F11 screenshots (`Logs/screenshots/f11_HHMMSS_mmm.png`, read with `filesystem:read_media_file`).
- F-keys: F5 repeat dialog; F9 nearest/cycle catalog; F10 player field+pos; F11 screenshot; F12 session diagnostics.

## Session checkpoint rule

After implementing a build: bump `FF8OPC_VERSION`, add the top `## vX.Y.Z` CHANGELOG entry, update `DEVNOTES.md`, rewrite this file. After BAT: mark the version confirmed in DEVNOTES; Aaron pushes. (DEVNOTES is over the 10 KB guideline — a `DEVNOTES_HISTORY.md` trim of the closed v0.17.8.7 chapter is overdue.)
