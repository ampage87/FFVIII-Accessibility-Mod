# Next Session Prompt: v0.17.7.0 BAT \u2192 v0.17.7.1 implementation

## Greeting

Start with `## Claude Says`. Read `DEVNOTES.md` and THIS file before any work.

## Where we are at session open

**GitHub HEAD = v0.17.6.2** (commit `a42d4aeb`). **Local tree = v0.17.7.0** \u2014 file split shipped locally, awaiting BAT before push.

v0.17.7.0 is a prerequisite, no-functional-change file split for the Track B entity-catalog overhaul. Two large blocks moved out of `field_nav_catalog.inl` (75.77 KB \u2192 53.82 KB):

- `field_nav_catalog_diag.inl` (9.29 KB) \u2014 ENTDIAG, BGDIAG, party-state, COORDDIAG one-shot dumps.
- `field_nav_catalog_lateres.inl` (11.19 KB) \u2014 LATE-RESOLVE, SET3-LATE-MATCH, STRUCT-POS late position fixups.

The v0.12.17 VARBLOCK-POS block (`if (false)` dead code, ~60 lines) is dropped during extraction. Git history at v0.17.6.2 preserves it.

Investigation phase of Track B is also complete. The four catalog failure modes and proposed fixes are documented in DEVNOTES.md and the full v0.17.7.x roadmap below.

## Session opener: BAT v0.17.7.0

This release is the highest-trust kind of change (mechanical split, no functional difference), but the BAT still matters because the helper call sites and the dropped VARBLOCK could in principle introduce subtle bugs.

### BAT recipe

1. Build. Confirm `Logs/build_latest.log` is clean \u2014 no compile errors from the new `#include` chain or helper visibility.
2. Load `bghall_1`. Press F9 several times to cycle the catalog.
3. Confirm the catalog populates with the same entries that v0.17.6.2 produced. Expected list (from v0.17.6.2 BAT): Save Point, Directory, NPC, Exit to B-Garden \u2014 Hall 8, Exit to B-Garden \u2014 Front Gate 5, Exit to B-Garden \u2014 Hall 4, and similar. The exact set depends on player position because of screen filtering, but the entries should match v0.17.6.2's behavior.
4. Press `\\` on a known-good target (Save Point or Hall 8 exit). Drive should behave identically to v0.17.6.2: `Driving.` \u2192 cardinal announcements \u2192 `Arrived.` (or field transition for cross-field exits).
5. Check `Logs/ff8_field.log` for:
   - `[party-state] formation = [u, u, u, u]` \u2014 fires once per field load (proves `DumpPartyStateOnce` runs).
   - `[COORDDIAG] === Coordinate space diagnostic ===` block \u2014 fires once per field (proves `DumpCoordDiagOnce` runs).
   - `[refresh] catalog: N entries ...` with `[refresh] cat0 ...`, `[refresh] cat1 ...` etc. \u2014 matches v0.17.6.2 catalog output.
   - No `[BGDIAG]` or `[ENTDIAG]` lines \u2014 those helpers are dormant (s_*Dumped initial true).
6. Optional: transition to fepic1 to confirm fepic1's broken behavior is still broken in the same way as v0.17.6.2 (since we haven't fixed anything yet). Expected: misclassified exits as `Interaction 1/2/3`, `Light 1 of 1` in catalog, `NPC 1 of 1` for the guard.

### If BAT is clean

Push v0.17.7.0 via `Utilities/push_to_github.ps1`, then proceed to v0.17.7.1.

### If BAT exposes a regression

Triage via `[refresh]` log diff against v0.17.6.2 BAT log. Likely suspects:
- Call order in `RefreshCatalog` differs from pre-split (e.g. LATE-RESOLVE running before some other block that depended on its output)
- A helper missed a static reference (compile error would catch this, but verify)
- The dropped VARBLOCK block had a side effect that wasn't actually dead

## v0.17.7.1 implementation plan (post-BAT)

Two combined fixes; one BAT cycle. Highest-impact pair from the v0.17.7.x roadmap.

### Fix 1: walkmesh exclusion rule

Drop entities from the catalog if BOTH:
- No talkradius: `talkonoff == 0` for runtime entities, OR no TALKRADIUS/TALKON opcode in the JSM scan for injected entities.
- Outside walkmesh: entity position not inside any walkmesh triangle.

Either condition alone keeps the entity. A talkable entity off-mesh stays (player interacts from on-mesh). A reachable scenery entity without talkradius is dropped (player can't do anything with it).

**New helper:** `IsInsideWalkmesh(float x, float y)` in `field_nav_helpers.inl` or `field_nav_pathfinding.inl`. Barycentric point-in-triangle across `s_walkmesh.tris`. ~30 lines. Walkmesh is already loaded for A* so the data structure is hot.

**Call sites:**
- In `field_nav_catalog.inl` runtime classification loop (after the party-filter, before the qualify check): add `if (talkonoff == 0 && pushonoff == 0 && !IsInsideWalkmesh(fpX, fpY)) continue;`.
- In the JSM-injection passes (interactive object + MAP_EXIT + draw/save point fallbacks): wrap injection with the same check, reading `je.posX/Y`.

**Risk to verify in BAT:** disconnected walkmesh islands. The v0.12.08 reachability filter was REMOVED in v0.12.09 because it false-positived on bggate_6 (guard on tri=87, player on tri=22). The new rule is safer because OR-with-talkradius keeps the guard, but BAT bggate_6 specifically to confirm.

### Fix 2: per-line exit/interaction/event discriminator

In `field_nav_catalog.inl`, replace the v0.12.24 `fieldHasInteractiveObjects` field-wide demote with per-line classification:

```
SCREEN_BOUND line:
  destFieldId > 0  \u2192 ENT_EXIT  "Exit to <field>" (or "Exit" if destFieldId out of range)
  destFieldId <= 0 \u2192 fall through to non-SCREEN_BOUND classification

Non-SCREEN_BOUND line (or SCREEN_BOUND with no MAPJUMP):
  TALKRADIUS or TALKON in script              \u2192 ENT_INTERACTION (current "Interaction N")
  MES/ASK/BATTLE/SHOW/HIDE/MOVE/REQ in script \u2192 ENT_OBJECT  (current "Event N")
  only BGDRAW/SCROLL                          \u2192 drop (camera-pan-only line)
```

Requires the JSM scanner to track TALKRADIUS/TALKON usage per Line entity. The scanner currently tracks `foundDialogOp`, `foundEventOp`, `foundBgdraw`, `foundScroll`, `foundBattle`, but NOT TALKRADIUS/TALKON. Add a new tracker `foundTalkSetup = (opcode == JSM_OP_TALKRADIUS || opcode == JSM_OP_TALKON)` and propagate via `info.hasTalkSetup` (new field in `JSMEntityInfo`).

Then in `JSM_ENT_LINE_INTERACTIVE` classification path (currently fires on `foundDialogOp` regardless of TALKRADIUS), gate on `foundTalkSetup`. Lines with MES but no TALKRADIUS reclassify to `JSM_ENT_LINE_EVENT`.

**Files changed:**
- `src/field_archive.h` \u2014 add `bool hasTalkSetup` to `JSMEntityInfo`
- `src/field_archive_jsm_scan.inl` \u2014 track TALKRADIUS/TALKON, set `info.hasTalkSetup`, gate `JSM_ENT_LINE_INTERACTIVE` on it
- `src/field_nav_catalog.inl` \u2014 swap field-wide `fieldHasInteractiveObjects` demote for per-line discriminator using `s_capturedLines[t].destFieldId` and the new line classification

**Notable size watch:** `field_archive_jsm_scan.inl` is 63.32 KB at v0.17.7.0. Adding `foundTalkSetup` tracking + the gate is maybe 10-20 lines, well under the 80 KB hard fail. But the file is in the warn zone; if v0.17.7.2's SETLINE-position promotion + this push the file over 60 KB, an `_inline` style split (similar to v0.16.x) will be needed before v0.17.7.2.

### BAT recipe (v0.17.7.1)

After build:
1. Load `bghall_1`. Confirm `Light 1 of 1` is NOT in catalog (exclusion rule killed it). Confirm Save Point, Directory, NPCs, exits all still present. Drive to Save Point with `\\`, confirm it still works.
2. Transition to fepic1 (Front Gate 5). Confirm catalog shows `Exit to ...` for the three exits instead of `Interaction 1/2/3`. The push-through gate (Track A) is still broken, but the catalog at least labels things correctly.
3. Transition to bggate_6 (or another disconnected-island field). Confirm the guard still appears in catalog (because guard has talkradius). This is the v0.12.08 regression test \u2014 verifying the new rule doesn't reproduce the old failure.
4. Walk to a sign or interactive object on bgroom_1 or Cafeteria 1. Sign won't be in catalog yet (v0.17.7.2 fixes that), but confirm the v0.17.7.1 changes haven't broken any existing interactive surface.

### What's NOT in v0.17.7.1

- SETLINE-positioned signs (v0.17.7.2)
- Runtime NPC `ResolveFriendlyName` (v0.17.7.2)
- Shop/Card Game \u2192 NPC announce-layer collapse (v0.17.7.3)
- SYM overrides like `Son \u2192 Cid's son` (v0.17.7.4 if needed)

## v0.17.7.x roadmap (after .1)

### v0.17.7.2 \u2014 SETLINE position promotion + NPC friendly names

**SETLINE position writeback in JSM scanner.** In `field_archive_jsm_scan.inl`, at the SETLINE capture block, write the center coordinates back to `info.posX/Y` when the entity has no other position:

```cpp
if (slAllLit) {
    setlineX1 = ...; setlineY1 = ...;  // existing capture
    if (!info.hasPosition && !info.hasPshmCoords) {
        info.posX = (setlineX1 + setlineX2) / 2;
        info.posY = (setlineY1 + setlineY2) / 2;
        info.posZ = 0;
        info.posTriangle = 0;
        info.hasPosition = true;
    }
    foundSetline = true;
}
```

This makes signs (background entities with MES + SETLINE) pass the `JSM_ENT_INTERACTIVE_OBJECT` promotion gate. The v0.17.7.1 walkmesh exclusion still applies, so SETLINEs that aren't on the walkmesh still drop.

**Runtime NPC `ResolveFriendlyName`.** In `field_nav_catalog.inl` runtime classification loop, after the existing SYM type table check, if `entName` is still the literal `"NPC"`:

```cpp
if (strcmp(entName, "NPC") == 0 && symIdx >= 0 && symIdx < s_symNameCount) {
    static char friendlyBuf[48];
    ResolveFriendlyName(s_symNames[symIdx], friendlyBuf, sizeof(friendlyBuf));
    if (friendlyBuf[0] != '\\0') entName = friendlyBuf;
}
```

Storage lifetime: `entName` is `const char*` pointing to string literals; for the friendly-name case we need a static or per-iteration buffer. Verify storage strategy when implementing.

This unlocks the 148-entry `ENTITY_DISPLAY_NAMES` map. Squall, Quistis, Cid, Boy, Student, Soldier, etc. all become proper names instead of "NPC N".

### v0.17.7.3 \u2014 Shop/Card Game \u2192 NPC announce-layer collapse

In `field_nav_announce.inl` typeLabel mapping, change:
```cpp
else if (catEnt.type == ENT_SHOP)        typeLabel = "Shop";
else if (catEnt.type == ENT_CARD_GAME)   typeLabel = "Card Game";
```
to:
```cpp
else if (catEnt.type == ENT_SHOP)        typeLabel = "NPC";
else if (catEnt.type == ENT_CARD_GAME)   typeLabel = "NPC";
```

Also update the same-type-counting loops (currently match on `Shop`/`Card Game`; they fold into the NPC count). Internal `ENT_SHOP`/`ENT_CARD_GAME` enum values stay for diagnostic clarity; user only ever hears "NPC".

### v0.17.7.4 (optional) \u2014 SYM override layer

For residual leaks like `Son 1 of 1`. Strategy decision: new manual override array checked first, OR extend the auto-generated `field_entity_survey.json` + regenerate. Decide at session open. Skip if the v0.17.7.2 NPC `ResolveFriendlyName` change happens to fix `Son` naturally (depends on whether the entity routes through the runtime NPC path or the JSM injection path).

## Hard constraints (unchanged)

- **Filesystem MCP for all Windows project files.** Bash is Linux-container.
- **Aaron pushes via `Utilities/push_to_github.ps1`**, Claude NEVER pushes.
- **NEVER re-enable SET3 opcode hook (0x1E).**
- **F-key handlers gated** on `!(GetAsyncKeyState(VK_MENU) & 0x8000)`.
- **F12 reserved** for per-session diagnostics only.
- **Source file size limits**: 60 KB warn, 80 KB fail. CI guard + client-side mirror in `Utilities/push_to_github.ps1` Step 7c.
- **OneDrive sync EPERM**: retry immediately on first edit attempt.
- **AUTO `[CBF]` battle-suppressor cap stays `INT_MAX`**.
- **`.inl` files are TEXTUAL INCLUDES**: no header guards, no namespace declarations inside.
- **CHANGELOG.md top heading must match `FF8OPC_VERSION`** or push utility refuses.
- **Navigation direction announcements are screen-relative.** AUTO-DRIVE F9 uses `s_camRight/Down` (v0.17.6.0). CHASE-DRIVE uses `s_driveCam*` (empirical, unchanged).
- **F9 corridor-level steering is OFF** (v0.17.6.2, BAT-confirmed). Funnel waypoints + FF8 wall-sliding are F9's only steering.

## Backlog

### Track A: push-through gate routing \u2014 deferred until Track B done

Multiple within-field drives in fepic1 failed because the walkmesh treats the gate as a wall; A* can't path through. Three candidate fixes documented in `DEVNOTES_HISTORY.md`'s v0.17.6.2 entry. Strategy decision is the first step when Aaron returns to it.

### v0.16.5.2 BAT triage backlog (still deferred)

Remove party members from field entity catalog (likely solved as side-effect of Track B item 1, since followers have no talkradius and the new exclusion rule kills them); walk-and-talk dialog gap; SeeD rank bug #27; refined-coord narrow-gate steering; Fire Cavern #28 + planner-fallback #29; per-world-map vehicle-aware BFS; battle Scan TTS keys 9/0; Junction menu TTS; more victory screen polish.

### v0.17.6.x candidates (mostly retired)

v0.17.6.3 (re-enable corridor steering with `currentWpDist > 200.0f` gate); v0.17.6.4 (spatial triangle lookup fallback for stale engine triId); v0.17.6.5/.6 (simplified recovery / funnel waypoint visibility validation). Only revisit if specific symptoms surface.

## Status check at session open

If Aaron opens with a BAT result: triage that first.

If Aaron opens with "let's start v0.17.7.1" (or similar): jump straight into the v0.17.7.1 implementation plan above. The investigation phase is complete; no re-reading of catalog source needed unless something has drifted.

If Aaron opens with a different priority: pivot.
