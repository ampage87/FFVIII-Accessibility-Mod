// world_garden_probe.inl - Mobile Balamb Garden auto-drive (#80): the collision
// probes the EXECUTOR steers by.
//
// PART OF world_map.cpp -- TEXTUAL INCLUDE. Do NOT compile standalone.
// Included AFTER world_garden_plan.inl and BEFORE world_garden.inl.
//
// v0.20.94 SPLIT: world_garden.inl reached 80,357 bytes against the 81,920-byte
// CI hard fail, with 1.5 KB left and the Shumi beach survey still iterating.
// DEVNOTES says to split BEFORE the edit that needs the room rather than shave
// comments to fit, so the probes come out now: they read only the grid, nothing
// in them depends on the executor's state, and nothing else moves. Behaviour
// identical by construction; both garden harnesses cover them.
//
// The design rationale and the engine facts are in world_garden_grid.inl's
// header -- read that first.

// ============================================================================
// Collision probes -- the executor reads the real grid, never the plan.
// ============================================================================
// Forward declaration: GdProbe is defined in terms of GdLineClear below.
static bool GdLineClear(int32_t x, int32_t y, int32_t tx, int32_t ty);

// v0.20.57, from the v0.20.56 BAT: this used to compare the hull's cell to the
// cell one WHOLE PROBE LENGTH away in a single step, and call the pair blocked
// if they differed by the 200-unit gate. That is not what the gate means. A
// 640-unit probe spans 2.5 cells, so any gentle slope longer than the probe
// accumulates past 200 even though every individual cell-to-cell step is well
// under it -- and the continental shelf is exactly such a slope. Measured at
// the Centra wedge: per-cell steps 0/107/166/85/48/62/32/17, all legal; the
// 2.5-cell jump 299, "blocked". Open ocean read as a wall, the hull wall-
// followed the depth contour, and 15 of 15 "bow blocked" lines in that log
// reproduce exactly under the old rule.
//
// Marching the gate cell by cell is both correct and already implemented, so
// the probe is now simply "is the straight line to the probe point clear".
static bool GdProbe(int32_t x, int32_t y, int heading, int dist)
{
    const double a = (heading & 0xFFF) / 4096.0 * 6.283185307179586;
    const int32_t nx = x + (int32_t)(sin(a) * dist);
    const int32_t ny = y - (int32_t)(cos(a) * dist);
    return !GdLineClear(x, y, nx, ny);
}

// True iff the straight line from (x,y) to (tx,ty) is Garden-clear at 96-unit
// pitch, honouring the step gate cell to cell.
static bool GdLineClear(int32_t x, int32_t y, int32_t tx, int32_t ty)
{
    const double d = CalculateWrappedDistance(x, y, tx, ty);
    if (d < 1.0) return true;
    if (d > 20000.0) return false;                 // never trust a very long chord
    const int steps = (int)(d / 96.0) + 1;
    const double a = (TorusBearing(x, y, tx, ty) & 0xFFF) / 4096.0 * 6.283185307179586;
    int prevH = 0; bool havePrev = false;
    int pr = GdRow(y), pc = GdCol(x);
    for (int i = 1; i <= steps; i++) {
        const double s = d * i / steps;
        const int32_t qx = x + (int32_t)(sin(a) * s);
        const int32_t qy = y - (int32_t)(cos(a) * s);
        const int r = GdRow(qy), c = GdCol(qx);
        if (r < 0 || r >= GD_ROWS) return false;
        const int idx = GdIdx(r, c);
        // v0.20.95: a beach mouth is passable to the executor too, or the hull
        // stands on ground the engine allowed and refuses to drive across it.
        if (!(s_gdCls[idx] & GDC_WALK) && !GdBeachOpen(idx)) return false;
        if (havePrev && abs((int)s_gdH[idx] - prevH) >= GD_STEP_GATE) return false;
        // v0.20.61: and the cliff gate on every cell boundary the chord crosses.
        if (r != pr || c != pc) {
            // v0.20.69: wrap BOTH axes. dc has always wrapped; dr did not, so
            // a chord crossing the pole seam read as a 767-row jump and every
            // GdLineClear over the pole returned false -- the hull could plan a
            // route across the seam and then refuse to drive a single step of
            // it, which is the Centra-to-Shumi wedge.
            int dr = r - pr;
            int dc = c - pc;
            if (dr >  GD_ROWS / 2) dr -= GD_ROWS;
            if (dr < -GD_ROWS / 2) dr += GD_ROWS;
            if (dc >  GD_COLS / 2) dc -= GD_COLS;
            if (dc < -GD_COLS / 2) dc += GD_COLS;
            if (dr < -1 || dr > 1 || dc < -1 || dc > 1) return false;
            if (!GdStepOpen(pr, pc, dr, dc)) return false;
            pr = r; pc = c;
        }
        prevH = s_gdH[idx]; havePrev = true;
    }
    return true;
}

