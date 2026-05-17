# Next Session Prompt: v0.16.3 field_archive_jsm.inl split

## Greeting

Start with `## Claude Says` per session ritual. Read `DEVNOTES.md` and THIS file before any work.

## Where we are

**GitHub HEAD = v0.16.1.4** (commit `5c08a1a`, pushed 2026-05-16 21:57:55, tagged `v0.16.1.4`). **Local tree = v0.16.2** (field_dialog.cpp split done, pending Aaron BAT + push).

The X-ATM092 chase auto-pilot chapter is **closed**: v0.16.1.4 BAT confirmed clean end-to-end progression through all four post-bridge fields (doopen2a -> dotown_3 -> dotown_2 -> dotown_1) with zero catches.

### v0.16.2 (this past session): field_dialog.cpp split

`src/field_dialog.cpp` (88 KB monolith) carved into a 3 KB slim parent + 8 `.inl` files. Pure mechanical split, no functional change. Include chain (dependency-ordered):

```
state -> helpers -> scan -> show_dialog -> opcodes -> diag -> menuname -> lifecycle
```

Largest new file: `field_dialog_lifecycle.inl` ~12 KB. Public-API tail (`IsActive`, `IsDialogOpen`, `GetMenuDrawTextCallCount`, `GetGetCharWidthCallCount`, `SnapshotGcwBuffer`) kept in the slim parent for visibility. `deploy.bat` unchanged.

### Status check at session open

If Aaron's first message is **"BAT"**: he's likely BATing v0.16.2 right now. Read `Logs/build_latest.log` tail first. Expected outcome: clean build, no functional change. If there's a build error, it's almost certainly one of:

- Missing forward declaration (something referenced before its `.inl` was included).
- Namespace mismatch (extra `namespace { }` inside an `.inl`).
- A static was missed during the move and is declared in two places.
- Static-initialization-order tangle on first dialog interaction.

If the build passes and Aaron reports dialog regression, walk `Logs/ff8_dialog.log` for `[DIALOG]`, `[GETSTR]`, `[SHOW_DIALOG-TEXT]`, `[MES]` / `[MESW]` / `[ASK]` markers and compare against any prior known-good run.

If Aaron's first message is **"Begin v0.16.3"** (or similar): proceed to v0.16.3 below.

## Next priority: v0.16.3 = field_archive_jsm.inl split

`src/field_archive_jsm.inl` is **91 KB**, the largest remaining source file over the 60 KB CI warn line.

### Important quirk

Unlike v0.16.0 / v0.16.1 / v0.16.2 (all of which were `.cpp` monoliths), `field_archive_jsm.inl` is **already an `.inl`**. It is textually included from `src/field_archive.cpp` inside the `namespace FieldArchive` block. The split strategy therefore differs:

**Option A** (simpler): produce a set of sub-`.inl` files (e.g. `field_archive_jsm_state.inl`, `field_archive_jsm_decoder.inl`, etc.), and have `field_archive_jsm.inl` become a slim file that just `#include`s the sub-files. This preserves the existing `field_archive.cpp` include line unchanged.

**Option B** (more invasive): rename `field_archive_jsm.inl` -> `field_archive_jsm_main.inl` or similar, and have `field_archive.cpp` include the new sub-`.inl` chain directly.

**Recommendation: Option A** unless `field_archive.cpp` itself has structural issues. Confirm with Aaron at session start.

### Recipe (Option A)

1. Read `src/field_archive.cpp` first to understand how it currently includes `field_archive_jsm.inl` and what other state lives at the `FieldArchive` namespace level. Confirm Option A is feasible (no surprise compile dependencies).
2. Read `src/field_archive_jsm.inl` end-to-end. Map functional groupings: typedefs/state, low-level binary decoding helpers, opcode-table data, opcode dispatch, per-opcode handlers, debug/diag.
3. Create `field_archive_jsm_state.inl` first (all statics + typedefs + constants).
4. Create the rest as the groupings suggest: `field_archive_jsm_decoder.inl`, `field_archive_jsm_opcodes.inl`, etc. Aim for 5-20 KB each.
5. Rewrite `field_archive_jsm.inl` as a slim shell: just `#include` chain of the new sub-`.inl` files. Keep the original comment block at the top for orientation.
6. **`deploy.bat` unchanged** -- only `field_archive.cpp` compiles; `.inl`s are textual.
7. **No functional change.** Aaron BATs: enter any field with NPCs, confirm dialog/scripts run normally.

### Key gotchas (carried from v0.16.0/0.16.1/0.16.2)

- **`.inl` files: NO header guards, NO namespace declarations inside.** They live inside the parent's namespace via textual include.
- **State `.inl` MUST be included first** inside the namespace block. All other `.inl` files reference the statics declared there.
- **Don't use bash for project file access.** Filesystem MCP only. Bash runs in a separate Linux container that can't see the OneDrive mod directory.
- **OneDrive sync EPERM rename errors**: retry immediately on first edit. Usually clears.
- **Watch the 60 KB warn / 80 KB fail thresholds.** Each new sub-`.inl` should land in the 5-20 KB range. If one approaches 60 KB, split further.
- **Forward declarations** for cross-`.inl` references go in `*_state.inl` (the file included first).
- **Don't introduce textual changes to comments beyond what's necessary** (e.g. don't replace em-dashes with hyphens unless required for encoding).

### Reference splits (the working models)

- **v0.16.0 world_map.cpp split** -- v0.16.0 commit chain. Slim `.cpp` parent + sub-`.inl` chain.
- **v0.16.1 chase_auto_pilot.cpp split** -- 6.47 KB slim parent + 8 `.inl` files: state (15.67), route (21.01), io (5.0), helpers (6.81), diag (5.23), bridge (7.06), engage (11.27), update (17.0).
- **v0.16.2 field_dialog.cpp split** (this past session) -- 3 KB slim parent + 8 `.inl`: state (~5 KB), helpers (~5 KB), scan (~10 KB), show_dialog (~11 KB), opcodes (~9 KB), diag (~12 KB), menuname (~6 KB), lifecycle (~12 KB).

## Hard constraints (unchanged)

- **Filesystem MCP for all Windows project files.** Bash runs in a Linux container that can't reach the OneDrive mod directory.
- **Aaron pushes via `Utilities/push_to_github.ps1`**, Claude NEVER pushes.
- **NEVER re-enable SET3 opcode hook (0x1E)** -- CI guard in `.github/workflows/safety-checks.yml`.
- **F-key handlers gated** on `!(GetAsyncKeyState(VK_MENU) & 0x8000)`.
- **F12 reserved** for per-session diagnostics.
- **Source file size limits**: 60 KB warn, 80 KB fail (CI enforced).
- **OneDrive sync EPERM**: retry immediately on first edit attempt.
- **AUTO `[CBF]` battle-suppressor cap stays `INT_MAX`** -- Aaron's 2026-05-13 directive.
- **`.inl` files are TEXTUAL INCLUDES**: no header guards, no namespace declarations inside, `state.inl` always first.
- Every Claude response starts with `## Claude Says`.

## Refactor queue after v0.16.3

- **v0.16.4**: split `src/battle_tts_ewm.inl` (90 KB).
- **v0.16.5**: split `src/battle_tts_menu.inl` (82 KB).

After v0.16.5 all source files are under the 60 KB warn line, and the refactor chapter closes. Then the backlog below opens.

## Key lessons carried forward (from the chase chapter)

1. **`ff8_nav_data.log` is the silent goldmine for spatial debugging.** It logs every player triangle change as `[timestamp] COORD field tri X Y ...` regardless of auto-pilot state -- including manual runs.
2. **Aaron's domain knowledge is ground truth, but his recipes need empirical verification.** Recipes point direction; position traces give magnitudes.
3. **Multiple catch sources on one field may not all be active.** Always verify the `[CBF] PASS` caller (`entityPtr=`) against the actual entity identity.
4. **Per-field problems require per-field analysis.** The robot's position resets at every field boundary.

## Backlog (after the size-split queue clears, v0.16.6+)

Roughly in priority order:

1. Remove party members from field entity catalog.
2. Walk-and-talk dialog gap (hardcoded engine path).
3. SeeD rank bug #27 (hypothesis: `FIELD_H_OFFSET = 0xF94` wrong section size).
4. Refined-coord narrow-gate steering.
5. Fire Cavern #28 + planner-fallback #29.
6. Per-world-map vehicle-aware BFS, guided GPS mode.
7. Battle: Scan TTS keys 9/0 (status resist/active statuses) -- offset hunt deferred.
8. Future: Junction menu TTS, more victory screen polish.
