# World-map entry triggers + vehicle gating (offline findings)

## There are no entry "lines" — entry is segment + region + vehicle

The offline walkmesh has terrain only, no triggers. FF8 fires a world-map -> field
transition from a **bytecode program** per destination, not a crossed line segment.

- Data: `wmsetus.obj` **Section 8** = 38 field-entry programs (already decoded in
  `src/world_map_trigger_data.inl`); **Section 2** = a 32x24 segment **region-byte** map
  (runtime `[0x2040330]`).
- Engine: `sub_53FAC0` (world tick, only when on-foot/world-walk, `[0x2036B70]==0`) ->
  `sub_545EA0` (reads Section 8 at `[0x2040070]`) -> `sub_545F10` (OR/AND clause walker)
  -> `sub_546100` (leaf predicates) -> on match `sub_544630` loads the field.
- Predicates: `0xFF06` segment-index == operand (index = `sub_553910` = `col*32+row` from
  player X/Y at `0x203EE80/84`, 8192u segments); `0xFF08` region-byte == operand;
  `0xFF09` locomotion `[0x020409E0]` == vehicle; `0xFF02/03` story word >=/< operand.
- So the "trigger line" you cross is really the **boundary of the destination's segment /
  region**, on foot, with the right story flags.

## Vehicle gating: a location is car-enterable iff a matching clause has FOOT_ALT

`0xFF09` vehicle classes (from the handler disasm):
- **FOOT** = operand `0x80`, matches locomotion in `[0,9]` or `0x80` (walking).
- **FOOT_ALT** = operand `0x84`, matches locomotion in `[0x20,0x28]` or `0x84` (the **car
  family**).  CHOCOBO `0x31`, GARDEN `0x30`, RAGNAROK `0x32` are their own classes.

So: **enterable by car iff one of the destination's matching clauses uses FOOT_ALT (0x84);
otherwise foot-only.** Confirmed against the BAT region bytes + the decoded clauses:
- Dollet (region 0x01 -> program 11): `FOOT, FOOT_ALT`  => **foot + car** (matches reality).
- Timber (region 0x02 -> program 13): `FOOT, FOOT`      => **foot only** (matches reality).

Car-enterable regions in the decoded table (have a FOOT_ALT clause):
`0x01, 0x03, 0x08, 0x0A, 0x0B, 0x1A, 0x44`. All other town regions are foot/chocobo only.

**Caveat (do not hardcode by region byte alone):** a program also checks the segment-index
(`0xFF06`), so sharing a region byte with a FOOT_ALT program does not by itself make a town
car-enterable (e.g. the region-0x03 program is FOOT_ALT but that does not prove Galbadia
Garden is car-enterable — its real program may be a different, foot-only one). The reliable
determination evaluates the full program (segment-index + region + vehicle) at the target,
which the mod can do from its live Section 2 map + the embedded programs.

## Proposed auto-drive improvement (removes the blind sweep)

Today the auto-drive aims at a research-table **marker** coordinate and, when no field loads,
spirals a blind "searching for entrance" sweep (which bounced at Timber because the marker is
>1500u from the real trigger). With the trigger model we can target the trigger directly:

1. For the destination, find its trigger program (the mod already has them) and its trigger
   **segment** (from `0xFF06`) and **region byte** (from `0xFF08` + the Section 2 map).
2. Compute that segment's world-coordinate extent (inverse of `sub_553910`) intersected with
   walkable ground (the faithful walkmesh), giving the actual **entry cells**.
3. Route (faithful A*, `validate_route`-checked) to the nearest entry cell and walk straight
   across into the region. Entry fires on crossing — **no sweep needed**.
4. Vehicle handling (player's current locomotion = `[0x020409E0]`; car family = 0x20-0x28):
   - **On foot** -> drive in normally (cross the trigger).
   - **In a car AND the destination has a FOOT_ALT clause** (car-enterable) -> drive in by
     car (the car locomotion satisfies the FOOT_ALT clause, so crossing the trigger segment
     loads the field while mounted -- no dismount needed).
   - **In a car AND the destination is foot-only** (no FOOT_ALT clause) -> do NOT approach
     the trigger; **stop auto-drive ~1-2 km out**, announce "<Location> cannot be entered by
     car -- please get out and enter on foot," and end the drive cleanly (no sweep, no
     wedging against an entry the car can't trigger).

This makes entry deterministic (drive onto the real trigger segment) instead of hoping the
marker is close, and it is fully offline-checkable: the entry cells and the route to them can
be validated against the walkmesh before any BAT. Implementation touches the auto-drive target
selection (use trigger-segment cells, not the marker) and can retire the sweep once confirmed.

---

## 2026-07-02 — Unknowns decoded; two corrections to the model above (see TRIGGER_FIRING_AREAS.md)

Full disassembly of `sub_546100` (dispatch tables at 0x546CAC/0x546D3C, vehicle sub-table
0x546D74/0x546DA0) plus a raw re-decode of Section 8 settles everything:

**CORRECTION 1 — `0xFF08` is the ACTION, not a region predicate.** Its operand is the
destination id passed to `sub_544630`. The Section 2 region map at `[0x2040330]` is NOT part
of the entry condition. Consequently the "region byte -> program" pairing above mislabels
programs (prog 11 = Balamb Town, not Dollet; prog 7 = Dollet; prog 13 = Fire Cavern +
B-Garden-east, not Timber; prog 17 = Timber; prog 9 = G-Garden/G-Station, not Balamb Town).
Correct pairing: **program `loc_id` (0xFF06 operand) == player segment index** from
`sub_553910` = `((y+0x48000) smod 0x30000 >>13)*32 + ((x+0x60000) smod 0x40000 >>13)`.

**CORRECTION 2 — the trigger is NOT the whole segment.** `sub_545EA0`'s first instruction
gates on the CURRENT TRIANGLE: `test byte ptr [[0x20409FC]]+0x0E, 8` — entry only ever fires
while standing on a wmx poly with **byte14 bit 3** set (hand-painted "entry" polys at each
gate mouth; Timber's whole segment contains just TWO walkable ones).

**Unknown predicates decoded** (operands were discarded by the v0.14.93 decoder — they are
sub-segment coordinate bounds, `xoff=(x+0x20000)&0x1FFF`, `yoff=(y+0x18000)&0x1FFF`):
`0xFF0F: xoff<op` · `0xFF10: yoff<op` · `0xFF11: xoff>op` · `0xFF12: yoff>op` ·
`0xFF07: 2048u fine-cell == op (unused in Section 8)` · `0xFF20: Ragnarok landing check
(slope ±0x2D + ground-flag edge)` · `0xFF21: savemap +0x6D bit0 (Garden-mobile)` ·
`0xFF22: tri index == op` · `0xFF32/33: current-tri byte14bit3 / byte13 == op`.
Vehicle operands beyond the four above: 0x81 (loc 0..1), 0x82 (8..9), 0x83 (0x10..0x16),
0x85 (0x22..0x28); `[0x2040A2C]!=0` bypasses the vehicle check.

wmx segments 768-834 are story-state replacements and carry the entry polys for
Garden-present states (826 -> seg 0x010B G-Garden; 827/828 -> segs 0x0112/0x0113 B-Garden).

Validated against the .205 BAT: Dollet fired exactly on its area's west edge; Timber failed
because the drive never touched its 314x512u patch (stall 391u west, then an orbit of radius
610-1330 around a point whose whole patch lies within 432u — inside the orbit's hole), with
story (315>=205) and vehicle (foot) both satisfied. Aim points + per-location firing areas:
`offline/TRIGGER_FIRING_AREAS.md` and sandbox `outputs/ff8/trigger_areas.json`.
