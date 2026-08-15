## v0.20.90

#80: **two things the .89 Shumi BAT said — and one of them was my message lying.**

**BAT (v0.20.89).** *"Drove to Shumi and the mod said I was 1km out, but I could clearly hear the Garden was still in the ocean. This leads me to think our coordinates for Shumi may be incorrect."*

### The message was wrong. The coordinate may not be.

```
[GARDEN] wall-follow drift at (4203,-82788) but only 1460 units from Shumi Village
```

That number is `goalDist` — **the distance to the berth** — printed next to the name of **the destination**. Everywhere else those two are within 3 km of each other and nobody notices. At Shumi the berth is a beach **12 km from the marker**, so the line announced "1,460 units from Shumi Village" with the hull at sea and ten kilometres from the village. Aaron heard the ocean and correctly concluded something was lying; it was this. It now reads *"1460 units from the Shumi Village berth (marker is 10488 away)"*.

**On the coordinate itself — checked properly, not dismissed.** Fisherman's Horizon cost six builds to exactly this kind of error, so:

| | |
|---|---|
| our marker (10362, −76967) | terrain 17, foot-walkable, **on the village's own foot landmass** |
| nearest wmsetus record | Chocobo Forest 2's, **4,082 units away** |
| unclaimed record 20 | (13000, −83977) — on the island, but on a **different foot landmass** from our marker |

So there *is* an unclaimed location record on that island, which is suggestive. But it sits on a landmass our marker is not on, so swapping to it would move the destination off the ground it belongs to. **Genuinely unresolved.** The way to settle it is the way FH was settled: walk in on foot and read where the field actually loads. One `[DRIVE] Arrival via game-mode` line pins it exactly.

### The beach: stop arguing with the model, ask the engine

The .89 attempt orbited for two minutes. One number explains it: **`gate=208` on every single sample** — the hull never left the water — with `aim=0` throughout.

`beach_climb` opened the **goal cell** to the planner, and the planner used it (360 waypoints). But the *executor* still has to see across the unmasked terrain-29 skirt to get there, and `GdLineClear` refuses every chord into it. So `aim` never came clear, the wall-follow engaged and never released, and the hull ran 1–2.6 km loops through four replans. **Opening one cell was never going to be enough — the crossing is the thing, and the model does not have it.**

Rather than guess at the crossing rule a fourth time, v0.20.90 asks the engine. Inside **`GD_BEACH_RUN_DIST` (2,000 units)** of a `beach_climb` berth the executor drives **straight at it** — bow probes suppressed, wall-follow suppressed, throttle down, steering only by shortest turn — for up to **9 seconds**, logging once a second:

```
[GDBEACH] t=3000ms pos=(3204,-82611) goal=612 hd=2317 off=88 mv=28/58 gate=208 cls=0x39 afloat
```

and ending in one of two lines:

* **`[GDBEACH] CLIMBED at (x,y) after N ms`** — the engine allows this shore, the model does not, and the model is what needs correcting.
* **`[GDBEACH] REFUSED at (x,y) after 9000 ms straight at the berth`** — the engine's own answer, with the hull stopped honestly rather than orbiting.

Aaron: *"If it runs into walls or something like that so be it, but we can at least use that data to survey the area around the village."* Either outcome is the first real data anyone has for this shore.

### Verification

* `tests/garden_harness.cpp`: **25 ok / 0 bad**, `beach_climb semantics: raw=0 berth=1`, `Shumi beach plan from Balamb: OK (360 waypoints)`.
* `tests/garden_aboard_test.cpp`: ALL CHECKS PASSED.
* **Parity** unchanged — no grid bit is touched.
* Clean under `-Wall -Wextra`; every source file inside the 80 KB guard (`world_garden.inl` 76,941).

**NOT MSVC-built, NOT BAT'd.**
