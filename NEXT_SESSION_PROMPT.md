# Next Session Prompt: v0.17.5.2 BAT'd clean, post-push session

## Greeting

Start with `## Claude Says`. Read `DEVNOTES.md` and THIS file before any work.

## Where we are at session open

**GitHub HEAD = v0.17.5.2** (assuming Aaron pushed via `Utilities/push_to_github.ps1` after the v0.17.5.2 BAT). **Local tree = matches GitHub HEAD** unless Aaron has started new work.

If GitHub HEAD is still `v0.16.5.2`, Aaron hasn't pushed yet. The push utility is a one-line command on his end (`Utilities/push_to_github.ps1`); the CHANGELOG.md top heading already matches `FF8OPC_VERSION = 0.17.5.2` so the utility's guard check will pass. If Aaron asks, the BAT was clean -- safe to push.

## v0.17.5.2 BAT result summary

Aaron walked elevator -> directory -> classroom hallway -> elevator. Outcome: "directions seemed good throughout."

Quantitative wins from the log:

| Field | A* tris | Pre-prune wp | Post-prune wp | Sweeps |
|-------|---------|--------------|---------------|--------|
| bghall_1 | 11 | 11 | 5 | 7 |
| bg2f_2 | 46 | 46 | **10** | **37** |

bg2f_2's path this BAT was a different (longer) route than v0.17.5.1's, but the prune behaviour is consistent: ~4-5x reduction in waypoint count by removing waypoints within 50 units of the line through their neighbours. No autodrive velocity-stuck events; no compile warnings; DIVERGE numbers within expected residual (11° on bg2f_2 due to tilted-camera 2D projection, well within sector tolerance).

First announced cardinal on bg2f_2 this run was **"north"**, matching Aaron's mental model (path's first leg was -X direction = screen-north on bg2f_2's quantized axes). The "east when expected north" scenario from v0.17.5.1 wasn't reproduced this time because Aaron started from a different position -- the path's geometry was different.

## Open architectural question

v0.17.5.3 (option B hybrid announcement) was queued as "ship if pruning alone isn't enough." This BAT showed pruning + path-aware was good for the route walked. The "east when expected north" scenario from v0.17.5.1 may or may not still occur on the original-position path; we don't know because Aaron didn't reproduce that exact starting position this BAT.

**Recommendation:** defer v0.17.5.3 until Aaron hits the issue again in real play. If he reports it, ship B then. Otherwise the next session should move to the v0.16.5.2 BAT triage backlog (see below).

## v0.16.5.2 BAT triage backlog (resumable)

Issues from the v0.16.5.2 BAT that were deferred for the v0.17.x navigation accuracy work. Rough priority order:

1. **Remove party members from field entity catalog.** When the party has 2-3 active members, the non-leader members appear as NPCs in the F9/F10 cycle. They shouldn't -- party members aren't navigation targets. Fix: filter ent0..ent2 (party slots) from the catalog before populating the cycle list. Specific code location not yet investigated.

2. **Walk-and-talk dialog gap.** During scripted walk-and-talk sequences (Squall walks somewhere while NPCs speak), the dialog text doesn't always announce via TTS. Hardcoded engine path bypassing the normal dialog injection hook. Needs disassembly trace of which engine routine fires for these scripted dialogues.

3. **SeeD rank bug #27.** Hypothesis from earlier sessions: `FIELD_H_OFFSET = 0xF94` is wrong section size. Need to verify offset by reading the actual savemap section boundary. Disassembly references in `/mnt/project/FF8_EN_*.txt` should help locate the relevant struct.

4. **Refined-coord narrow-gate steering.** Some narrow corridors cause autodrive to oscillate. Possibly addressable by using a wider AGENT_RADIUS for path planning but a narrower one for steering target validation. Investigation needed.

5. **Fire Cavern #28 + planner-fallback #29.** Fire Cavern's world map navigation has a planner failure mode. Need to repro and trace.

6. **Per-world-map vehicle-aware BFS, guided GPS mode.** Bigger feature. World map navigation currently treats all terrain the same; should know which terrain the current vehicle can cross.

7. **Battle: Scan TTS keys 9/0 (status resist/active statuses)** -- offset hunt deferred. Need to find the right savemap offsets for these two fields.

8. **Junction menu TTS** -- future feature, not actively investigated.

9. **More victory screen polish** -- ongoing future work.

## Status check at session open

**If Aaron's first message is about the v0.16.5.2 BAT backlog**: pick the highest-priority item he wants to address. Start by re-reading the relevant code with `filesystem:read_text_file` rather than working from memory.

**If Aaron's first message is "let's do B / v0.17.5.3"**: he's hit the path-aware-vs-mental-model scenario again. Implement option B (hybrid announcement) in `field_nav_gps.inl::UpdateGPS`:
- Compute both `dirIdx` (toward waypoint, current behavior) and `finalDirIdx` (toward `(tx-px, ty-py)`).
- When they differ, speak both: "east, heading north, 6 steps" or similar phrasing.
- When they match, speak just one (current behavior).
- Same change in `StartGPS`.
- Keep path-aware steering intact (`AdvanceGpsWaypoint` still drives autodrive and the immediate `dirIdx`).
- New version v0.17.5.3, single CHANGELOG entry.

**If Aaron's first message is "BAT" without a v0.17.5.3 build between sessions**: that's a stale BAT message about v0.17.5.2 or something unexpected -- ask before assuming.

## Hard constraints (unchanged)

- **Filesystem MCP for all Windows project files.** Bash is Linux-container and can't see them.
- **Aaron pushes via `Utilities/push_to_github.ps1`**, Claude NEVER pushes.
- **NEVER re-enable SET3 opcode hook (0x1E).**
- **F-key handlers gated** on `!(GetAsyncKeyState(VK_MENU) & 0x8000)`.
- **F12 reserved** for per-session diagnostics only.
- **Source file size limits**: 60 KB warn, 80 KB fail. `field_nav_pathfinding.inl` is approaching the warn threshold after v0.17.5.2 -- if v0.17.5.3 adds significant code there, split to a new `.inl`.
- **OneDrive sync EPERM**: retry immediately on first edit attempt.
- **AUTO `[CBF]` battle-suppressor cap stays `INT_MAX`**.
- **`.inl` files are TEXTUAL INCLUDES**: no header guards, no namespace declarations inside.
- **CHANGELOG.md top heading must match `FF8OPC_VERSION`** or the push utility refuses.
- **Navigation direction announcements are screen-relative, not world-relative.**
- **AUTO-DRIVE uses `s_driveCam*` pair (v0.17.2), MANUAL-NAV uses `s_camRight/Down`.** Neither is written by anything other than the field-load handler.

## Notes for resumption

- The v0.17.x series shipped the navigation accuracy stack in three small steps: load-time 90 deg axis quantization (v0.17.5), GPS hysteresis (v0.17.5.1), and post-funnel collinear waypoint pruning (v0.17.5.2). All three working together. Architecture is in a good place.
- The `PruneCollinearWaypoints` function in `field_nav_pathfinding.inl` is the most recent change. 50-unit epsilon; conservative below FF8 wall thickness; first/last waypoints preserved; sweep-to-stable with 100-iteration safety cap.
- bg2f_2's tilted camera (d2len=0.130) is a known special case. v0.17.5 quantization handles it correctly within sector tolerance. If a future field has even more tilted axes and quantization fails, the observer log surfaces it as DIVERGE > 15°.
- For diagnostic walking on a specific field, F9/F10 cycle + GPS start is the established workflow. F11 captures a screenshot. F12 is reserved for per-session diagnostics.

## Classroom entity catalog (parallel track, still paused)

Still pending. Need from Aaron:
1. Field name confirm -- now corroborated as `bg2f_2` from v0.17.5.x BATs.
2. F9 list contents (cycle through, get the two "interaction" names if they exist).

This is low priority; can be deferred until Aaron specifically wants to address it.
