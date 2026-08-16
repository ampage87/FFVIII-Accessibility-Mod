## v0.21.1

#79: **Edea's House was 9.6 km from its marker, and it is the Shumi failure
repeating almost to the kilometre.**

> *"Auto-Drive says searching for entrance, then it sounds like it is going back
> and forth in front of the location. Also tried to randomly walk into it but
> was unsuccessful."* — Aaron

---

### First, v0.21.0 passed

```
22:01:54  [VEHSIG] verdict VETOED: motion sided with mh in 23 of 24, but the
          engine's vehicleId=0 says ON FOOT -- staying on the foot steering law
22:04:54  [VEHSIG] verdict VETOED: ... 22 of 24 ... vehicleId=0 says ON FOOT
```

No "Vehicle detected." was spoken and the foot law kept the drive. **The last
twenty distance samples are `dist=5` and `dist=26`** — the back-and-forth Aaron
heard is the drive standing *on* the marker, not failing to reach it.

Worth recording honestly: **the turn guard alone did not suppress those
verdicts** (23 of 24 frames still voted vehicle). The engine-id veto carried it.
That is the belt-and-braces design working as intended, but it means the physics
fallback would still misfire on foot if the id ever became unreadable.

### The marker had nothing under it

Every named place the world map *draws* sits on a patch of texture page 8, and a
correct marker sits a few hundred units outside it. Audited against the whole
catalog first, because a method is only worth trusting if it reproduces the cases
already known good:

```
Fire Cavern 241u   Shumi 349u   Galbadia Station 502u   Tomb 540u
Chocobo Forests 619-734u   Centra Ruins 741u   Winhill 881u
Timber 900u   Balamb Town 1002u   Trabia 1396u   Dollet 1397u
```

Nineteen markers land in that band. **Edea's House was 7,110 units from its
nearest patch — and that patch is a 32-poly one already claimed by Chocobo
Forest 7 at 728u.** It had no settlement of its own anywhere near it.

The whole southern half of the map contains exactly **four** page-8 patches:

| polys | centre | claimed by |
|---|---|---|
| 291 | (6145, 55295) | Centra Ruins, 741u |
| 32 | (−21005, 69632) | Chocobo Forest 7, 728u |
| 32 | (44018, 75776) | Chocobo Forest 6, 684u |
| **103** | **(−29583, 70090)** | **nothing** |

103 polys is settlement-sized (Winhill 248, Shumi 248, Tomb 64), the structure
measures **800 × 900 units**, and it carries **terrain 29** — the same signature
Shumi Village has, and the terrain the planner's own comment already calls out as
*"entered via terrain-29 polygon trigger, not wmsetus script event"*. Its apron
is terrain 29 on all four faces; the **east** face has the most of it (7 polys at
h −346..−188), and east is the side you arrive from after landing the Garden.

**The marker is now that apron: (−28950, 70090).**

### The story gate was a red herring, and I said otherwise

The trigger table has program [32], field 506 `ehhana1` — *Edea's House, Flower
Field* — gated `story >= 1750`, and the live story word reads **912**. That
looked like an answer. It was not one.

> *"Edea's House is now open at this point in the game. It becomes reachable at
> the start of disc 3, which is where I am at now."* — Aaron

He is right, and the terrain-29 finding explains the contradiction: **the
orphanage is entered by walking onto its polygons, exactly as Shumi is**, so
program [32] is a later scripted visit and has nothing to do with this one. A
gate that fails does not mean the door is locked when the door is not that gate.

### Other markers the audit flagged

The audit is a lead generator, not a verdict — Fisherman's Horizon is 25 km from
any page-8 patch and is *correct*, because it is a platform in open ocean, and
Cactuar Island and the Missile Base are the same kind of case. But several
disc-3 destinations are about to matter and have never been driven to:

```
Sorceress Memorial   3,713u        Esthar City          9,018u
White SeeD Ship      4,203u        Lunar Gate           8,321u
Chocobo Forest 3     4,315u        Lunatic Pandora Lab  6,740u
Tears' Point         6,036u        Island C. to Heaven  6,290u
```

None is touched here. They are recorded so the next one that misbehaves starts
from evidence rather than from a fresh investigation.

### Verification

* `catalog_story_test` 0 failures — the only host test that compiles
  `world_catalog.inl`; `vehsig_test` OK (0 bad); `lint_seh` OK (88 files);
  `minigame_bgbtl_compile` 0 errors / 0 bad; `garden_harness` 26 ok / 0 bad;
  `garden_aboard_test` and `world_map_harness` pass
* Coordinate change only — no executable logic touched

**NOT MSVC-built.**

### BAT

Auto-drive to Edea's House. It is about 10 km west-north-west of where the mod
used to send you, so expect a longer drive.

1. **It should arrive and a field should load** — the orphanage, not a sweep.
2. If it sweeps again, that is still useful: grep `[DRIVE] Manual field entry`
   and `arming entry capture`. The moment any field loads within 3,000 units of
   the marker the mod captures the true coordinate itself, which is exactly how
   Galbadia Garden's was pinned.
3. If nothing loads anywhere near it, tell me and I will go after the terrain-29
   polygon list directly rather than the texture map.
