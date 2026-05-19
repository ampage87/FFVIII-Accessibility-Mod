# Next Session Prompt: v0.17.6.2 ready to push; v0.17.7.x — push-through gates + catalog labeling

## Greeting

Start with `## Claude Says`. Read `DEVNOTES.md` and THIS file before any work.

## Where we are at session open

**GitHub HEAD = v0.17.5.4** (commit `b54fa75`, tagged `v0.17.5.4`, pushed 2026-05-18 16:20:18 local). **Local tree = v0.17.6.2 (BAT'd 2026-05-18 18:32, rolled forward, ready to push).**

v0.17.6.2 BAT was a clean win on its stated goal. All four bghall_1 cross-field exits reach `Arrived.` with diagonal-kb wall-sliding through corridor turns. Drive times are 7-14 sec, matching manual nav. The F9 path-finding auto-drive is now correct for cross-field navigation in the B-Garden hub. See `DEVNOTES.md` "v0.17.6.2: BAT result" for the per-drive timings.

If Aaron hasn't pushed yet, his first move is `Utilities/push_to_github.ps1`. CHANGELOG.md v0.17.6.2 entry is at the top and matches `FF8OPC_VERSION`, so the push utility will accept it.

## Two new tracks exposed by v0.17.6.2 BAT

These are real-world friction issues the BAT exposed AFTER v0.17.6.2 solved its main problem. Both observed in fepic1 (B-Garden - Front Gate 5, fieldId=0x00A3) and similar fields.

### Track A: Push-through gate routing (fepic1 and likely other fields)

**What happens:** fepic1 has a scripted gate the player walks INTO at a specific point to trigger an animation/script that pushes them through to the south exit. The walkmesh treats the gate as a wall (because normally you can't walk through it), so A* can't find a path through. Auto-drive routes around the closest reachable point, oscillates, or walks the player into the wrong exit (back to Hall 1 in the v0.17.6.2 BAT).

**What the BAT log showed:**
- 18:34:47 drive to `Interaction 3` (southwest 6 steps): oscillated south/southwest for 32 seconds, no `Arrived.`
- 18:35:25 drive to `Interaction 2` (south 3 steps): same oscillation pattern
- 18:35:40 drive to `Interaction 1` (north 4 steps): walked into Hall 1 transition instead of completing

### Investigation plan for next session

1. **Find the push-through trigger.** Read fepic1's JSM script via `field_archive_jsm.inl` infrastructure or via project-knowledge disassembly search. Look for:
   - PUSHRADIUS entities (opcode 0x063 at `0x0051EE00`) and their script entry points
   - SETLINE entities (opcode 0x039 at `0x0051DC30`) that lead to JUMP/MOVA sequences
   - JUMP/MOVA opcodes that teleport the player (these are the "push" part)
   - Likely structure: a SETLINE in the middle of the corridor triggers a script that runs MOVA to push the player past the gate, then resumes control.

2. **Confirm the walkmesh geometry.** Read fepic1's walkmesh data — is there a true wall at the gate, or is there a small walkable triangle through it that A* is missing? If walkable, the issue is in A*/funnel. If walled, the issue is the absence of a routing concept for scripted teleports.

3. **Identify the player-side trigger zone.** Where does the player need to step to fire the push-through? Is it a SETLINE rectangle? A PUSHRADIUS center? A specific tile? This determines whether F9 can drive the player to it.

4. **Strategy decision.** Three candidates ordered by complexity:
   - **(a) Walkmesh patching at load time.** For known push-through fields, inject a walkable triangle through the gate connecting the two sides. A* then finds a normal path. Player walks through, the actual game-script PUSHRADIUS still fires at the right moment, scripted push plays out. (Risk: if the engine refuses to move the player into the patched triangle without the script firing first, the player still stalls.)
   - **(b) Two-stage A*.** Build a per-field "push-through node table". A* finds path to the trigger point, F9 drives there, engine fires the push, F9 detects player position has jumped and re-pathfinds from new position to original target.
   - **(c) Manual hint annotations.** For each push-through field, declare in code: "to reach exit X from spawn, first walk to SETLINE Y". F9 uses this hint instead of A* for cross-gate routing.

   (a) is cleanest if it works. (c) is least powerful but most predictable. (b) needs engine-side push detection.

5. **Other fields likely affected.** Any field with story-gated doors, scene-triggered exits, or vehicle-mediated transitions probably has the same pattern. Don't generalize the fix from a single field BAT — get fepic1 working, then check ffhill (final hill), dotown (Dollet town), and any field where vanilla play requires walking into something for a scene.

### Track B: Better entity catalog labels

**What happens:** fepic1 catalog shows `Interaction 1, 2, 3` and `Light 1 of 1` and `NPC 1 of 1`. Cafeteria 1 (fieldId=0x009A) shows `Son 1 of 1`. These are generic fall-through names with no semantic content.

**Why this matters even after push-through routing works:** Even if F9 can pathfind through gates, Aaron still needs to know WHICH entity is the gate trigger vs the guard NPC vs the chocobo. Without meaningful labels, he has to brute-force cycle and drive to each one.

### Investigation plan for next session

1. **Check what name sources exist today.** Look at `field_navigation.cpp` (and its `.inl` files) for the entity-labeling pipeline. Likely candidates already in use:
   - SYM strings from field archive (named in the field's script symbol table — "kani", "battleyarou", "laguna" all came from this)
   - INF gateway names (used for exits — works well)
   - Hardcoded per-field overrides
   - Fall-through to "Interaction N", "NPC N", "Light N"

2. **What's missing for fepic1?** The "Interaction 1/2/3" entries are probably field background/line entities without SYM names. Possible new sources:
   - **JSM script comments / labels.** Some entities have labels in the script. Check if `field_archive_jsm.inl` can extract these.
   - **PSHRADIUS / SETLINE script content heuristics.** If a SETLINE entity's triggered script contains a JUMP to the world map, label it "Gate to Outside". If it contains a MES (dialog) opcode with text mentioning a key NPC, use that.
   - **Manual per-field annotations.** For high-value story fields (B-Garden hub, Dollet, Galbadia), maintain a hand-written entity name override table keyed by `(fieldId, entityIndex)`.

3. **Quick wins.** "Son" in Cafeteria 1 is likely a SYM name that's exposed verbatim — should be re-mapped to "Cid's Son" or "Boy" or similar. Some SYM names are FFNx-internal and unhelpful as-is. Build a small SYM-to-display-name mapping table for the worst offenders.

4. **Defer the heavy lift if too much.** If JSM script-content heuristics turn out to be a multi-session effort, just ship the hand-written per-field name override table for the B-Garden hub fields. Aaron can extend it as new fields appear.

## v0.17.6.x backlog (now mostly retired)

v0.17.6.0/.1/.2 are all BAT'd successfully. The remaining v0.17.6.x candidates are deferred and may not be needed:
- v0.17.6.3: Re-enable corridor steering with `currentWpDist > 200.0f` gate. Only revisit if a long-corridor field overshoots without it.
- v0.17.6.4: Spatial triangle lookup fallback for stale engine triId. Only revisit if a drive fails with engine reporting wrong triangle.
- v0.17.6.5: Simplified recovery. Not needed if recovery already works (v0.17.6.1 BAT showed it does).
- v0.17.6.6: Funnel waypoint visibility validation. Not needed unless a wp falls inside geometry.

## v0.16.5.2 BAT triage backlog (still deferred)

1. Remove party members from field entity catalog
2. Walk-and-talk dialog gap (hardcoded engine path)
3. SeeD rank bug #27 (`FIELD_H_OFFSET = 0xF94` hypothesis)
4. Refined-coord narrow-gate steering
5. Fire Cavern #28 + planner-fallback #29
6. Per-world-map vehicle-aware BFS, guided GPS mode
7. Battle: Scan TTS keys 9/0 (status resist/active statuses)
8. Junction menu TTS
9. More victory screen polish

## Hard constraints (unchanged)

- **Filesystem MCP for all Windows project files.** Bash is Linux-container and can't see them.
- **Aaron pushes via `Utilities/push_to_github.ps1`**, Claude NEVER pushes.
- **NEVER re-enable SET3 opcode hook (0x1E).**
- **F-key handlers gated** on `!(GetAsyncKeyState(VK_MENU) & 0x8000)`.
- **F12 reserved** for per-session diagnostics only.
- **Source file size limits**: 60 KB warn, 80 KB fail. `field_nav_autodrive.inl` is ~73 KB after the v0.17.6.x diagnostic blocks — watch for next edits crossing 80 KB and consider splitting.
- **OneDrive sync EPERM**: retry immediately on first edit attempt.
- **AUTO `[CBF]` battle-suppressor cap stays `INT_MAX`**.
- **`.inl` files are TEXTUAL INCLUDES**: no header guards, no namespace declarations inside.
- **CHANGELOG.md top heading must match `FF8OPC_VERSION`** or the push utility refuses.
- **Navigation direction announcements are screen-relative, not world-relative.**
- **AUTO-DRIVE F9 path uses `s_camRight/Down` (v0.17.6.0), CHASE-DRIVE uses `s_driveCam*` (empirical, unchanged).** Manual-nav also uses `s_camRight/Down` — F9 shares this pair, so any future writes to `s_camRight/Down` must be safe for both manual nav and F9 auto-drive.
- **F9 corridor-level steering is OFF (v0.17.6.2, BAT-confirmed).** Funnel waypoints + FF8 wall-sliding are F9's only steering. Chase-drive has been on this regime since v0.15.9.2.3.

## Status check at session open

If Aaron's first message confirms a successful push of v0.17.6.2: acknowledge and pivot to Track A or B above (let Aaron choose which).

If Aaron's first message describes a new bug or different priority: pivot to that. v0.17.7.x can wait.

If Aaron's first message is "push-through" or "fepic1" or "front gate": start Track A investigation (read fepic1 JSM script via project knowledge search and/or `field_archive_jsm.inl`).

If Aaron's first message is "catalog" or "Interaction 1" or "labels": start Track B investigation.

## Classroom entity catalog (deferred, low priority)

Field name still confirmed as `bg2f_2`. Need from Aaron: F9 list contents (cycle through, get the two "interaction" names if they exist). Deferred until Aaron specifically wants to address it. This is a smaller version of Track B and would likely be solved together.
