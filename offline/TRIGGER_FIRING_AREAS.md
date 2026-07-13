# World-map field-entry triggers: FIRING AREAS (decoded 2026-07-02)

Feeds **v0.18.3.206**. Source evidence: full disassembly of `sub_546100` (all leaf
predicates + both dispatch tables extracted from FF8_EN.exe at 0x546CAC/0x546D3C and
0x546D74/0x546DA0), `sub_545EA0`/`sub_545F10`, `sub_553910`; raw re-dump of wmsetus.obj
Section 8 (from the v0.14.92 `[TRIGGER-DUMP]` hex in
`Logs/archive/ff8_world_2026-05-05_20-30-56.log`); wmx.obj poly scan via the faithful
oracle; v0.18.3.205 BAT log (`Logs/ff8_world.log`, session 21:44-21:51).

Machine-readable twin: sandbox `outputs/ff8/trigger_areas.json`
(+ `section8_programs_decoded.json`, `entryflag_tris.json`, `wmsetus_section8.bin`).

---

## 1. The complete firing condition (all unknowns decoded)

A world-map -> field entry fires on a frame iff **ALL** of:

1. **MASTER GATE (this was the missing fine condition):** the triangle the player is
   standing on has **poly byte14 bit 3 set**. `sub_545EA0` begins:
   `mov eax,[0x20409FC]` (current-tri record = raw 16-byte wmx poly) ;
   `test byte ptr [eax+0x0E], 8` ; **returns without scanning Section 8 if clear**.
   Entry-flag polys are hand-painted onto the mesh at each town's gate mouth.
   (`[0x20400AC]` = current block poly base; record stride 16 = the raw file poly, so
   +0x0D/+0x0E/+0x0F are exactly wmx bytes 13/14/15.)
2. **Segment (0xFF06):** `sub_553910(x,y)` == program operand, where
   `seg = ((y+0x48000) smod 0x30000 >> 13)*32 + ((x+0x60000) smod 0x40000 >> 13)`
   (8192u cells, row*32+col; x,y raw from `[0x203EE80/84]` — same values the mod logs).
   **The `.inl` `loc_id` IS this segment index** (not a "location id").
3. **Story window (0xFF02/0xFF03):** word at `[0x2036BDE/DF]` >= / < operand.
4. **Vehicle (0xFF09):** 0x80 foot (locomotion 0..9 or 0x80); 0x84 car family
   (0x20..0x28 or 0x84); 0x30 Garden; 0x31 Chocobo; 0x32 Ragnarok; also 0x81 (0..1),
   0x82 (8..9), 0x83 (0x10..0x16), 0x85 (0x22..0x28), 0x21 (==0x21).
   Bypassed entirely when `[0x2040A2C] != 0` (scripted move).
5. **Sub-segment rectangle — the former TRIG_UNK_0F/10/11/12:** with
   `xoff=(x+0x20000)&0x1FFF`, `yoff=(y+0x18000)&0x1FFF` (offset inside the 8192u
   segment):
   - `0xFF0F op` -> `xoff < op`  (was TRIG_UNK_0F; operand was discarded by the old decoder!)
   - `0xFF11 op` -> `xoff > op`  (was TRIG_UNK_11)
   - `0xFF10 op` -> `yoff < op`  (was TRIG_UNK_10)
   - `0xFF12 op` -> `yoff > op`  (was TRIG_UNK_12)

Then the matching clause's **`0xFF08` opcode is the ACTION, not a predicate**: its
operand is the destination id handed to `sub_544630` (0xFF08 dispatches to `return 0`
in sub_546100, which makes sub_545F10 return the pointer as the clause result).
**The old model's "region-byte == operand via Section 2" reading of 0xFF08 was wrong**;
the Section 2 region map is not part of the entry condition at all (the mod's live
`catRegion` matching mispairs programs with towns — e.g. it maps Timber to program 13,
which is actually Fire Cavern + Balamb-Garden-east, and Dollet to program 11, which is
actually Balamb Town).

Other decoded opcodes (present in engine, mostly unused in Section 8): `0xFF07`
fine-cell == operand (2048u cells, `((y+0x48000)%0x30000>>11)*128 + ((x+0x60000)%0x40000>>11)`);
`0xFF20` (TRIG_UNK_20) Ragnarok landing check (slope words 0x2040998.. within ±0x2D +
edge-trigger on per-frame ground-flag pair `[0x203FDE8]` indexed by `[0x20409BC]`);
`0xFF21` (TRIG_UNK_21) savemap `[0x20403A4]+0x6D` bit0 == operand (Garden-mobile flag);
`0xFF22` current-tri index == operand; `0xFF33` current-tri byte13 == operand; `0xFF32`
~byte14 bit3 == operand; `0xFF25` world-mode byte `[0x2036B70]` == operand; `0xFF27`
savemap bit test; `0xFF2F` random16 < operand; `0xFF17` no-entity-of-type-within-9e6;
`0xFF1A/1B/1C/1D` touched/facing entity type checks; `0xFF34/35/38/39` misc globals.
`0xFF01`=program separator, `0xFF04`=begin clauses, `0xFF0A/0C/0D`=clause separators,
`0xFF0B`=AND->action, `0xFF05`=end clause, `0xFF0E`=jump, `0xFF16`=end program.

### Alternate-mesh states

wmx.obj segments 768-834 are story-state replacements. Entry-flag polys for some
towns exist **only in the replacement** (Garden present vs absent):

- **extra 826 -> seg 0x010B** (Galbadia continent, row 8 col 11): Galbadia-Garden-present;
  223 walkable flagged polys (base has only the 23 G-Station ones).
- **extra 827 -> seg 0x0112**, **extra 828 -> seg 0x0113**: Balamb-Garden-present
  (base 0x0112 has ZERO flagged polys; base 0x0113 has only Fire Cavern's 8).
- Programs with no walkable flagged polys in base mesh (need alt state): idx 12
  (B-Garden W), 15 (0x0147 Missile Base), 18/19/20 (0x0172 mobile-Garden dest),
  22 (0x0176 FH), 37 (0x02C1 Ragnarok-only).

---

## 2. Ground-truth validation (v0.18.3.205 session, story=315, on foot)

| Check | Result |
|---|---|
| Dollet fired at lastPos (-15418,-39462), fieldId 0x013D | Dollet area west edge x=-15409 at that y; (-15400,-39450) is inside — capture is 1 frame past the crossing. **PASS** |
| Pre-fire stall (-15639,-39437) for ~6 s without firing | Outside area (230u west). **PASS** |
| Timber seed (-22564,-5507) | **INSIDE** the Timber patch — the old capture was good. |
| Every logged position in both Timber attempts (21:48:16-51, 21:50:41-48) | Zero positions inside the patch bbox — grep for `p(-22[3-6]xx,-5[1-6]xx)` over the whole log: **0 hits**. The drive pinned at (-23076,-5535) (391u west of the patch; wall) and the sweep orbited the seed at radius 610-1330 — but the whole patch lies within 432u of the seed, **inside the orbit's hole**. **Geometric miss, not gating** (story 315 >= 205, on foot). |
| G-Garden ggview1 (-37475,-26232) | rect y edge is -26236; 4u past (capture lag). In alt-826 flagged polys. **PASS** |
| G-Station (-38394,-24803) | 4-6u past area east edge (capture lag). **PASS** |
| Balamb bcgate (12894,-26776) | 10u past area east edge (capture lag). **PASS** |
| Fire Cavern bdview1 (30260,-29221) | 30u past area north edge (capture = approach-field marker). **PASS** |

Note the systematic +4..30u overshoot: the mod captures `lastPos` a frame or two
*after* the transition begins, so captured "entries" sit just OUTSIDE the true area on
the approach side. Never orbit a captured point — walk THROUGH it toward the area.

---

## 3. TRUE ENTRANCE AIM DATA (v0.18.3.206 targeting table)

Coordinates are raw `[0x203EE80/84]` world units (the mod's logging convention).
`aim` = interior point of the walkable entry-flag area (safe steering target);
`bbox` = [xmin,xmax,ymin,ymax] of the firing area (walkable ∩ flagged ∩ clause rect).

```c
// { name, aimX, aimY, xmin, xmax, ymin, ymax, footOnly, storyGte, storyLt }
{ "Timber",          -22580,  -5291, -22685, -22371,  -5632,  -5120, 1,  205,    0 },
{ "Dollet",          -14513, -39119, -15409, -13516, -39951, -38175, 0,   36,    0 },
{ "Balamb Town",      12560, -26800,  12288,  12884, -26896, -26624, 0,    0,    0 },
{ "Balamb Garden",    24304, -30300,  23552,  25410, -31370, -29696, 1,    0,  570 },
{ "Fire Cavern",      30239, -29528,  30112,  30394, -29750, -29192, 1,    0,    0 },
{ "Galbadia Garden", -36895, -27082, -37764, -35964, -28036, -26236, 1,  290,  490 },
{ "Galbadia Station",-38914, -24767, -39426, -38398, -24936, -24682, 1,  290, 3900 },
```

Per-location notes:

- **Timber** (prog 17, ACT 0x04): the ONLY walkable entry polys in seg 0x016D are two
  triangles: `(-22685,-5120) (-22528,-5632) (-22528,-5120)` and
  `(-22528,-5120) (-22528,-5632) (-22371,-5120)` — a 314x512u wedge. The road ends in
  the 1024u cell (c105,r91) just NORTH of it; best approach: reach (-22580,-4900) on
  the road, then walk SOUTH ~250u into the patch. Foot only (no 0x84 clause) —
  announce "Timber cannot be entered by car."
- **Dollet** (prog 7, ACT 0x03): huge area (219 polys), foot or car. Any point of the
  bbox interior works; from the west road use (-15222,-39465).
- **Balamb Town** (prog 11, ACT 0x01): small 7-poly gate mouth, foot or car.
- **Balamb Garden** (progs 12 + 13-clause-2, ACT 0x00): patch straddles the
  col18/col19 segment boundary at x=24576 (west part = whole-seg-0x0112 program, east
  part = seg 0x0113 with xoff<0x800). Foot only; **closes at story>=570**.
- **Fire Cavern** (prog 13 clause 1, ACT 0x02): flagged polys clipped by xoff>0x15A0
  (x>30112). Foot only. Loads the bdview1 approach field.
- **Galbadia Garden** (prog 9 clause A, ACT 0x06): firing area = flagged polys (alt
  mesh) ∩ rect x(-37764,-35964) y(-28036,-26236). Foot only; **story window
  [290,490) — outside it announce that G-Garden cannot be entered now**.
- **Galbadia Station** (prog 9 clause B, ACT 0x07): flagged polys ∩ y>-25768
  (yoff>7000). Foot only; story < 3900.

---

## 4. v0.18.3.206 implementation recommendation

1. **Retire the blind spiral.** Target the `aim` point; on "final approach", switch to
   *area-walk*: while no field loads, walk a lawn-mower pattern INSIDE `bbox`
   (clamped to the area polygon for Timber), never leaving it. The .205 orbit failed
   precisely because it circled OUTSIDE a patch smaller than its minimum radius.
2. **Live fire-readiness check** (the mod already reads all of these): predicted-fire =
   `(curTriByte14 & 8)` (read `[[0x20409FC]]+0x0E`) `&& seg==prog.seg && storyWindow
   && vehicleClass && xoff/yoff rect`. Log `[TRIGREADY]` each frame during approach;
   if predicted-fire is ever true while the game stays on the world map, the model
   needs revisiting (it should be impossible).
3. **Pre-drive gates, announced before moving:** (a) vehicle — foot-only destinations
   (Timber, Fire Cavern, B-Garden, G-Garden, G-Station) while in the car: stop 1-2 km
   out and say "<X> cannot be entered by car — please get out and enter on foot";
   (b) story — G-Garden [290,490), B-Garden [0,570), G-Station [290,3900), Dollet
   [36,∞), Timber [205,∞): if outside, say "<X> cannot be entered right now."
4. **Fix the program<->location pairing:** match by **segment index == program
   loc_id** (and clause rect for multi-destination segments 0x010B/0x0113/0x0117/
   0x0189/0x0096), NOT by Section-2 region byte. Keep the `.inl` clause data but add
   the four rect operands (they were dropped: e.g. prog 9 clause A carries
   xoff∈(3196,4996), yoff∈(4732,6532)).
5. **Refined-entry captures:** treat as *evidence of the area*, not as the target —
   they land a few units outside it (capture lag). Snap any capture to the nearest
   point of the decoded area before use.
