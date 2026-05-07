# Next Session Prompt — FF8 Accessibility Mod

## Status at handoff

**v0.14.108 is SHIPPED.** Commit `e9a60eb4` on `main` (Thu 2026-05-07 22:39 UTC). Behavioral-fingerprint follower filter works correctly across multiple fields and party compositions. Linear history on `main`: v0.14.104 → v0.14.106 → v0.14.108. v0.14.107 was BAT-failed and never pushed.

## What just happened (last session summary)

1. **v0.14.107 built and BAT-failed.** Filter cross-referenced canonical model→charId map against savemap formation at `0x01CFE74C`. On bggate_1 with party = Zell + Squall + Selphie (formation `[1, 0, 5, 255]`), the followers showed up as `ent1 model=2` and `ent2 model=4` (canonical Irvine/Rinoa, neither in the active party), so the savemap lookup correctly returned false and the filter no-opped. **Lesson: engine reuses model slots per-field**; the canonical map doesn't apply for catalog filtering.
2. **v0.14.108 designed, built, BAT-passed, and pushed.** Replaced the canonical-mapping filter with a behavioral fingerprint check inside the catalog qualification loop (modelId 0–9 + walk-through + no-talk + no-push). Parallel to the existing v0.12.12 push-only-skip. The v0.14.107 helpers (`IsCharacterInActiveParty`, `ModelIdToCharId`) and the `[party-state]` formation diagnostic remain in place — harmless, well-documented, may be useful later. BAT validated across two fields with two different party compositions; pushed as commit `e9a60eb4`.
3. **Push utility hardened.** First exercise of the new auto-flow `push_to_github.ps1` (rewritten earlier this session) failed silently — hidden PowerShell process, no dialog, no log update. Added phase-by-phase diagnostic logging to a fallback file (`Logs/push_diagnostic.log`) plus a top-level try/catch with a fallback MessageBox. Aaron's retry succeeded with a Success dialog. Instrumentation is permanent so further silent failures can be diagnosed immediately.

## v0.14.108 BAT evidence

bggate_1 with party `[1, 0, 5, 255]` (Squall + Zell + Selphie):
```
[party-state] formation = [1, 0, 5, 255]
[party-filter] ent1 model=2 filtered (visible walk-through, no interaction)
[party-filter] ent2 model=4 filtered (visible walk-through, no interaction)
[refresh] catalog: 4 entries (3 navigable, 1 new entities), player=ent0
[nav] cat1 ent-400 rank=1/3 'Exit to B-Garden - Front Gate 2, 1 of 3'
```
Catalog dropped from 6 entries (v0.14.107 BAT) to 4. First nav announce is now an actual exit, not a phantom NPC.

B-Garden Hall (probable bghall_1) with a different party including Quistis and Selphie:
```
[refresh] catalog: 9 entries (8 navigable, 4 new entities), player=ent0
[refresh]   cat1 ent4 model=18 type=NPC name='NPC'                     ← real NPC kept
[refresh]   cat2 ent5 model=20 type=NPC name='NPC'                     ← real NPC kept
[refresh]   cat3 ent7 model=24 type=Save Point name='Save Point'       ← save point preserved
[refresh]   cat4 JSM ent25 type=Object name='Directory'                 ← interactive kept
[party-filter] ent1 model=3 filtered (visible walk-through, no interaction)  ← Quistis follower
[party-filter] ent2 model=5 filtered (visible walk-through, no interaction)  ← Selphie follower
```

Filter correctly catches followers regardless of which model slot the field assigned (2/4 on bggate_1, 3/5 on bghall_1) while preserving real NPCs (model >= 10), save points (model 24, outside `< 10` guard), and interactive objects (JSM-injected). Comprehensive validation.

## Once Aaron pushes

v0.14.108 has been pushed (commit `e9a60eb4`). The deferred priority list:

1. **X-ATM092 chase scene accessibility.** Long-deferred. Timed visual sequence in Dollet — blind player has no sense of distance or direction to flee. Needs preliminary deep research on the chase's engine internals (the ATM092 entity behavior across screen transitions, the chase script structure) before any code changes.
2. **Walk-and-talk dialog gap.** Hardcoded engine path; longstanding. Field dialog TTS works for static dialogs but engine-driven walk-and-talk segments bypass the show_dialog hook. Needs investigation of the engine's walk-and-talk code path to find a different hook point.
3. SeeD rank bug (#27) — likely `FIELD_H_OFFSET = 0xF94` is wrong section size.
4. Refined-coord steering for narrow-gate locations (v0.15.x persistence work).
5. Fire Cavern entry trigger (#28) and planner-fallback (#29).

X-ATM092 and walk-and-talk are both preliminary — start with deep research/exploration before writing code. Aaron picks which one feels worth the effort.

## If unexpected regressions surface in further play

Most likely candidates if Aaron notices something off after the push:

- **Fire Cavern draw point** (model 9, party-character range): if play through Fire Cavern shows the draw point missing from the catalog, the filter is catching it. Verify `talkonoff` actually gets set on it before the catalog scan; if there's a timing race, may need to relax the filter (e.g., require `setpc != 0` as well, or whitelist model 9 specifically).
- **Real NPC missing from a specific field**: TALKRADIUS race — NPC has `throughonoff` set in init scripts, TALKRADIUS sets `talkonoff` later. The catalog refreshes every F9 press, so the next press should pick them up; if it consistently misses, the field uses a fully-static interaction model that needs a different fingerprint.
- **Save point missing on a specific field**: shouldn't happen given the BAT confirmed model 24 is preserved, but if it does, check if the field uses a non-24 model for its save point (unlikely but possible).

If any of these surfaces, paste the relevant log fragment and we'll diagnose. Don't guess.

## Open GitHub issues at session end (16 total)

Carryover: #2, #3, #5–10, #15, #18–22, #25, #26 (PR), #27, #28, #29.

## Persistent rules (do not break these)

- `## Claude Says` prefix on every response.
- Filesystem MCP for Windows project files; bash only for Linux container.
- Claude NEVER invokes the push utility. Aaron runs `Utilities/push_to_github.vbs` after BAT passes; the utility reads version + body automatically from `ff8_accessibility.h` and `CHANGELOG.md`.
- **Every version bump must be paired with a new top-of-file entry in `CHANGELOG.md`, written to push-quality.** The push utility validates the heading matches `FF8OPC_VERSION` and refuses to push otherwise.
- F12 reserved for diagnostic builds only.
- SET3 opcode hook is permanently disabled.
- BAT workflow: check `Logs/build_latest.log` first, then domain-specific log.
- Update DEVNOTES + NEXT_SESSION_PROMPT at every version bump and after every BAT.
- Always check `Plan & Research Documents/` AND past conversations BEFORE proposing new logic.
- Always call `github:list_commits` before quoting GitHub state.
- For "used to work before Sonnet regression": ALWAYS `conversation_search` BEFORE writing new logic.
- When BAT log seems to show absence of feature exercise, ASK Aaron rather than assuming.
- **FIELD ENTITY MODEL IDs ARE FIELD-LOCAL SLOT INDICES, NOT CANONICAL CHARACTER IDs**. Confirmed v0.14.107 BAT, validated by v0.14.108 BAT. Don't build filters on canonical model→charId mappings. Use behavioral fingerprints (interaction flags) when filtering by entity behavior.
- Locomotion byte at 0x02040A5E does NOT reliably indicate rental car state. Use `car_rent` flag at 0x01CFEF1A.
- Don't try to detect bouncing as frozen-position; cars oscillate.
- Verify engine-internals assumptions empirically before relying on them.
- Search log format strings by unique fragments, not bracket prefixes.
- Accessibility wording: prefer concrete action verbs over abstractions.
- `dryRun=true edit_file` works as grep substitute for finding code locations.
- When `oldText` matches only a partial line, the trailing fragment is preserved as-is — use full lines for replacements, or follow up with a cleanup edit.
- **filesystem:edit_file batches are atomic** — if any single edit's anchor doesn't match, the entire batch rolls back. For multi-region edits, prefer separate calls or use `write_file` for full-file rewrites.
- **Accessibility hotkeys must be gated against modifier-key combinations** (notably `Alt`).

## Reference: savemap layout (relevant slices)

- Base: `0x01CFDC5C`
- Header size: 76 bytes (0x4C). Subtract 0x14 from deep-research offsets that assume 96 bytes.
- Characters (8 × 0x98 bytes): `+0x48C` (Squall, Zell, Irvine, Quistis, Rinoa, Selphie, Seifer, Edea)
- GFs (16 × 0x44 bytes): `+0x4C`
- **Active party formation: `+0xAF0` = `0x01CFE74C` (4 bytes, charId 0–7 or 0xFF, not compacted)** — used by v0.14.107 helpers and the `[party-state]` diagnostic, NOT the v0.14.108 filter.
- Worldmap: `+0x125C`

## Reference: party-character canonical model→charId map (NOT used by the v0.14.108 filter)

| Model | CharId | Character |
|-------|--------|-----------|
| 0 | 0 | Squall |
| 1 | 1 | Zell |
| 2 | 2 | Irvine |
| 3 | 3 | Quistis (casual) |
| 4 | 4 | Rinoa |
| 5 | 5 | Selphie |
| 6 | 6 | Seifer |
| 7 | 7 | Edea |
| 8 | 3 | Quistis (uniform variant) |

Used by `ResolveNameByModelId` (naming) and the v0.14.107 helpers (still in field_nav_helpers.inl, available for future use). v0.14.108 BAT confirmed engine reuses model slots per-field, so this map cannot be relied on for catalog filtering.

## Reference: keyboard shortcuts (current as of v0.14.108)

`` ` `` = repeat | V = version | F1 = cycle voice | F2 = toggle ducking | F3/F4 = speech rate down/up | Shift+F3/F4 = speech volume down/up | F5/F6 = SFX volume down/up | F7/F8 = BGM volume down/up | F9/F10 = field nav | F11 = screenshot capture | F12 = diagnostic builds only | G/T/L/R = Gil/Time/Location/SeeD | `/` = help bar | O = EWM toggle | 1/2/3/H = battle HP | M = menu summary | `\` = world map auto-drive | A = gas | W = reverse. **All F-key accessibility handlers gated on `!alt`.**
