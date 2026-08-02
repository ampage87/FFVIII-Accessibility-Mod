# Sewer gates (#85) — offline static analysis of all 6 sewer fields

**Date:** 2026-07-19
**Tool:** one-off offline analysis binary built by compiling the mod's actual
`field_archive.cpp` + `field_archive_jsm_*.inl` for Linux (not shipped, not part
of the mod — scratch tooling, source not checked in). Parses the real
`field.fi/fl/fs` archive from `Game Files/FINAL FANTASY VIII/Data/lang-en/`
and runs the same `ScanJSMScripts` classifier the mod uses at runtime, with
zero live game process involved.

## Why this was needed

Prior diagnosis (GitHub #85, CHANGELOG `v0.18.3.285`) proved that `glwater3`'s
gate entities (`saku1`-`saku6`, `water`, `hasigo`) sit beyond the engine's
active-tracking window (`pFieldStateOthers` only holds `otherCount=9` live
slots but the field declares 17 "Other" entities) — so **no live memory read,
of any kind, can ever recover their position** while the player is far enough
away in the walkmesh. Aaron asked whether the same gate pattern repeats
throughout the sewer maze, and asked for a static (file-based) survey of every
sewer field instead of another BAT round.

## Method note: two crash-causing issues fixed in the offline build only

These only affected the throwaway Linux tool, not the shipped mod:

1. `__try`/`__except(EXCEPTION_EXECUTE_HANDLER)` (SEH, Windows-only) doesn't
   exist on Linux; textually mapped to real `try`/`catch(...)`.
2. `field_archive_jsm_mapjump_resolver.inl`'s forward interpreter reads a
   **hardcoded live-process address** (`EXIT_VARBLOCK_BASE = 0x01CFE9B8`, the
   field varblock) for `MAPJUMP`/`MAP_EXIT` destination resolution. With no
   real game process, dereferencing that pointer segfaults. Fixed for the
   offline tool by `mmap`-ing a zero-filled page range over that exact address
   range — the interpreter then legitimately sees "no live value" (matching
   its own designed fallback for an early-lifecycle field) instead of
   crashing. This has **no bearing on the shipped mod**, which always runs
   inside the real game process where that address is valid.

Both fields ran clean afterward; all 6 sewer fields fully scanned.

## Field census — internal name → display name

| Internal   | Display name          | Sewer # |
|------------|------------------------|---------|
| glfuryb1   | Deling City - Sewer 1  | 1 |
| glwater1   | Deling City - Sewer 2  | 2 |
| glwater2   | Deling City - Sewer 3  | 3 |
| glwater3   | Deling City - Sewer 4  | 4 (previously confirmed real gate/valve/ladder room) |
| glwater4   | Deling City - Sewer 5  | 5 |
| glwater5   | Deling City - Sewer 6  | 6 |

## Key finding #1: glwater1 (Sewer 2) has NO gate ("saku") entities at all

Its "Other" entities are just `ward`, `ladline0`-`ladline4`, `water` — no
`sakuN` entity exists in this field's JSM/SYM data. This independently
confirms the earlier field-misidentification finding from the previous BAT
round (the `.283`/`.284` work targeted glwater1 based on a stale prior-session
assumption; the real gate mechanism only exists in glwater2-5).

## Key finding #2: the real gate maze spans 4 fields (Sewer 3-6), 26 gates total

| Field (Sewer #) | saku-gate count | gates with a captured walkmesh triangle | gates with a direct literal position |
|---|---|---|---|
| glwater2 (3) | 4 (saku1-4) | 3/4 (saku2,3,4 → tri 63) | 2/4 (saku2, saku4 — real X/Y/Z, no PSHM at all) |
| glwater3 (4) | 6 (saku1-6) | 3/6 (saku1→tri294, saku2/3→tri58) | 0/6 |
| glwater4 (5) | 8 (saku1-8) | 6/8 (tri 57/58/60/294) | 0/8 |
| glwater5 (6) | 8 (saku1-8) | 3/8 (saku6→tri60, saku7→tri294, saku8→tri293) | 0/8 |
| **Total** | **26** | **15/26 (58%)** | **2/26** |

("Gate" here means a `sakuN`-named Other-category entity — the actual movable
gate/valve mechanisms the player opens to route water. `ladlineN`/`water`/
`hasigo`/`ward` are companion entities: ladder-climb triggers, the water-level
controller, and the ladder-out point, present in most fields alongside the
gates.)

## Key finding #3: the walkmesh-triangle discovery is the actionable path forward

For every entity where the classifier captured a `tri` id, that triangle
number was read from the entity's own init-script `SET3` opcode **at JSM
parse time — no live memory involved whatsoever.** Even when the X/Y/Z
operands of that same `SET3` call are PSHM_W (runtime-variable) markers
instead of literals — meaning the *exact* live coordinate genuinely can't be
known without the entity being in the active window — the walkmesh triangle
the entity stands on is still a real, static fact baked into the field's own
walkmesh (`.id` file), independent of the engine's live-tracking limitation.

`field_archive.h`'s `WalkmeshTriangle` struct already stores a pre-computed
`centerX, centerY` (screen-space centroid) for every triangle, and
`LoadWalkmesh()` is existing, working code. So for the 15/26 gates with a
captured triangle, we can derive an **approximate but always-available**
catalog position — `walkmesh.triangles[tri].centerX/centerY` — entirely from
static field data, with zero dependency on the active-tracking window. This
sidesteps the root cause completely rather than working around it.

The 2 gates with a full literal position (glwater2's saku2, saku4) don't even
need the triangle lookup — their exact SET3 coordinates were captured
directly.

## Open gap: the other 42% (11/26 gates, tri=0)

These entities' own init script never calls `SET3` at all. Several are typed
`Director` (an established pattern in this codebase: an invisible dispatcher
entity with no model/position of its own, whose job is to `REQ`-trigger
*other* entities). `hasigo` (the ladder-out point) and `saku4`/`saku5`/`saku6`
in glwater3 fall in this bucket, along with roughly a third to a half of the
`sakuN` gates in glwater4/5.

For these, a triangle-centroid position isn't available from their own
script. Two options, neither explored yet in code:
1. Follow the `Director`'s `REQ` targets (the codebase already has a working
   REQ-following pass, used for the dormitory-bed pattern) to see if any
   linked entity DOES carry a triangle/position that could be borrowed.
2. Accept "no position, but still listed" for this subset — surface them in
   the catalog as a named, selectable entry (so the player at least learns
   *how many* gates exist and their names/order) without map/auto-drive
   coordinates, which is strictly better than total invisibility (today's
   state) even though it doesn't support click-to-navigate.

## Addendum (same day): the 42% gap was mostly a scanner bug, not a game-design fact

Aaron asked a sharp question after the first pass: "Is it possible you could
only extract 17 because only 17 can be opened / interacted with? There are
some gates that you cannot open." That's a completely reasonable read of an
88%→58% drop, so it was checked directly by dumping the FULL raw opcode
stream (every method, not just init) for all 26 `sakuN` entities via
`DumpEntityScript`.

**Answer: mostly no — it was a bug in the classifier's scan scope.** The
production `ScanJSMScripts` only looks for `SET3` in an entity's **init
method (method 0)**. Several sewer gates set their own position in a
**later** method instead — almost certainly the toggle/open method, invoked
on demand via `REQ` from a controller entity rather than at field-load time.
Once all methods are checked, not just method 0:

- **22/26** `sakuN` entities have a real, directly-owned `SET3` triangle.
- **1/26** (`glwater2`'s `saku1`) turned out to not be a gate at all — its
  script is a `MAPJUMP3`-based exit (the sewer stairwell out), just reusing
  the `sakuN` naming convention. This is exactly the "SYM names are
  unreliable as identity hints" trap already flagged in `DEVNOTES.md` — the
  original name-pattern census conflated it with the real gates.
- **3/26** (all in `glwater3`: `saku4`, `saku5`, `saku6`) remain genuinely
  unresolved. `saku4` is a pure-logic `Director` (no `SETMODEL`, no model at
  all — it dispatches `REQ` calls to the other gates' toggle methods, so it
  correctly has no position because it isn't a physical object). `saku5` and
  `saku6` DO call `SETMODEL`+`BASEANIME` (real, visible, toggleable models)
  but never call `SET3` anywhere in their own script — still an open
  question, not yet explained.

So of the 25 entities that are actually gates, **22 (88%) have a discoverable
static position** once the scan covers every method — not 58%. The fix
requires widening `ScanJSMScripts`'s SET3 search from "init method only" to
"all methods," which is a separate, smaller bug from the triangle-centroid
proposal below and should be fixed first since it feeds directly into it.

## Proposed next step

Build the triangle-centroid fallback as a new, narrowly-scoped position
source in the catalog pipeline (alongside the existing SETLINE / live-entity
/ INF-gateway sources), gated to `jsmCategory==3` entities with a captured
`posTriangle` but `hasPosition==false` and no PSHM live-read available. This
covers 17/26 gates immediately (15 triangle-only + 2 already-literal) across
all 4 real gate fields, with the remaining 9 tracked as a known-scoped
follow-up (REQ-following or list-without-position).
