## v0.20.89

#80: **the beach exception was armed in one place and asked about in three.**

**BAT (v0.20.88).** *"Did a quick test just now and Shumi did not appear in the catalog."*

The log says it plainly:

```
[GARDEN] catalog: 24 destinations the Garden can reach, 15 hidden as out of range
```

**24, not 25**, and no `beach climb armed` line anywhere — because v0.20.88 set `s_gdBeachGoalIdx` in `Garden_StartDrive` and **nowhere else**. `Garden_BuildCatalog` runs the moment you board, long before any destination is chosen, and it called `Garden_CellReachable` directly. With the exception clear, a `beach_climb` berth is unreachable *by construction* — that is the entire point of it — so the catalog hid the destination it had just been given. The announce path had the same hole.

### The fix is a shape, not a patch

The exception is a property of **the berth**, not a mode the mod happens to be in. Asking `Garden_CellReachable(gp->park_x, gp->park_y)` is the wrong question about a `beach_climb` berth no matter who asks it, so that call is gone from every berth path:

```c
static bool Garden_BerthReachable(const GardenPark* gp)
{
    if (!gp || !gp->reachable) return false;
    const int saved = s_gdBeachGoalIdx;
    s_gdBeachGoalIdx = gp->beach_climb
                     ? GdIdx(GdRow(gp->park_y), GdCol(gp->park_x)) : -1;
    const bool ok = Garden_CellReachable(gp->park_x, gp->park_y);
    s_gdBeachGoalIdx = saved;
    return ok;
}
```

Arm, ask, restore. The catalog, the announce and the drive all go through it; a caller cannot get this wrong by forgetting.

### And a guard, because "I'll remember" is what produced the bug

`tests/garden_harness.cpp` now pins the semantics outright:

```
beach_climb semantics: raw=0 berth=1  OK (helper is required -- use Garden_BerthReachable)
```

A `beach_climb` berth **must** read unreachable through the raw call and reachable through the helper. If a future call site asks the raw question, that assertion is what catches it — instead of a destination quietly vanishing from a list.

### The first attempt did not build

```
world_map_state.inl(1025): error C4430: missing type specifier - int assumed
world_map_state.inl(1025): error C2143: syntax error: missing ',' before '*'
world_catalog.inl(258):    error C2664: cannot convert 'const GardenPark *' to 'const int'
world_map_announce.inl(30): error C2664: same
```

I put the forward declaration next to `Garden_CellReachable` — which sits **above** the `GardenPark` struct it names. It now lives below the struct, beside `Garden_ParkFor`.

**Worth knowing for next time:** `g++` passed both before and after. Each harness defines its own `GardenPark` before including the `.inl` files, so a declaration-order error *inside* `world_map_state.inl` is structurally invisible to them. The host harnesses check behaviour; only MSVC checks the real include order.

### Verification

* `tests/garden_harness.cpp`: **25 ok / 0 bad**, `beach_climb semantics` OK, `Shumi beach plan from Balamb: OK (360 waypoints)`.
* `tests/garden_aboard_test.cpp`: ALL CHECKS PASSED.
* **Parity**: WALK / PARK / FOOT / OPEN_E / OPEN_S / WATER / BEACH / CLEAR unchanged — this build touches no grid bit.
* Clean under `-Wall -Wextra`; every source file inside the 80 KB guard.

**NOT MSVC-built, NOT BAT'd.**
