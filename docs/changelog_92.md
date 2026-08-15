## v0.20.92

#80: **the Shumi coordinate question is closed — and our coordinate is right.**

**BAT (v0.20.91).** Aaron drove the Garden to Chocobo Forest 2, stepped off, and walked the probe point.

```
[DRIVE] Start -> Shumi probe point at (13000,-83977), dist=3073 units (3 km)
[PLAN] GRID planner ok: cell(1124,135)->cell(1125,111) -> 25 fine cells
[YAWDRIVE] pos(13000,-83984) ... steer(13000,-83977) dist=7 idx=24/25
```

**Seven units from wmsetus location record 20.** He stood there for fifty seconds.

**Nothing loaded.** No field transition, no `ChaseDetector: fieldId changed`, no dialog. **Record 20 is not Shumi Village.**

So the catalog coordinate **(10362, −76967) stands**. Shumi Village is on the 7,034-cell island that carries the 53-polygon Garden beach, and that island simply has no world-map location record — the record-bearing island next door is Chocobo Forest 2's.

That also removes the last doubt about the beach: **the only Garden-landable shore in the area is on the same island as the marker**, which is what it should be if the marker is right.

### The file-side route was correct and produced nothing

Worth recording, because it was the right thing to try and it is now ruled out.

**`locID` in the trigger programs IS a field index** — validated on four anchors before drawing any conclusion from it: Winhill 652 → fields 643–674, Centra Ruins 279 → 276–286, Dollet 327 → 307–345, Esthar 407–466 → 402–487.

**Shumi's fields are 934–945. The highest locID in the whole 38-program table is 705.** Nothing points at Shumi — and that is not a decode gap. The trigger table is a *special-cases* table for vehicle- and story-gated entries; Balamb Town, Timber and Trabia Garden are absent from it too.

The field archive opened cleanly — `tmsand1.fs` and `tmgate1.fs` pulled by byte range with `dd` on the user's machine, LZS-decompressed to their exact catalogued sizes, and `tmsand1`'s 17-file sub-archive unpacked including its `.jsm` and its `.inf` gateways, which reference neighbouring Shumi field IDs. **But a field's gateway to the world map carries no world coordinates.** (`tmsand1` = "sand" — the beach field, matching the walkthrough.)

### Method note

**One walk answered in fifty seconds what the file archaeology could not.** When the question is *"what does the game do at this coordinate"*, the game is the cheapest instrument available — and the on-foot auto-drive turned a test Aaron could not have done by sight into a routine one.

### A real bug the probe exposed

**A destination with no field trigger has no arrival condition.** The drive jammed at 7 units and then announced *"Cannot reach the destination from here."* Only diagnostic destinations can hit this — every real one ends when its field loads — but expect it if another waypoint is ever added.

### Verification

* Probe entry removed; `tests/garden_harness.cpp` **25 ok / 0 bad**, `beach_climb semantics: raw=0 berth=1`.
* `tests/garden_aboard_test.cpp`: ALL CHECKS PASSED.
* Clean under `-Wall -Wextra`; every source file inside the 80 KB guard.

**NOT MSVC-built, NOT BAT'd.**
