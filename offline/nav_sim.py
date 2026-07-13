"""nav_sim.py — offline FF8 world-map navigation simulator with a faithful
CAMERA TRANSFORM engine model, a PlanPathGrid-style planner, and two executors.

Sources of truth:
  * offline/CAMERA_EXE_ANALYSIS.md §4 (input->movement), §5 (camera physics),
    §7 (wall slide), §8 (difference equations — implemented verbatim below),
    §9 (camera-write control spec).
  * offline/ff8_walkmesh.py — the validated walkmesh oracle (WMX).
  * offline/NAV_SIM_FINDINGS.md — 32u march / fine-grid route / exact-follow design.

COORDINATE CONVENTION (converted ONCE, here):
  The sim works entirely in the oracle's signed game coords (gx, gz):
    gx in [-131072, 131071]  (torus wrap mod 0x40000)
    gz in [ -98304,  98303]  (torus wrap mod 0x30000)
  The engine's raw Y (0x203EE84, dY = +cos(h)*spd) maps to gz = 0x48000 - Y,
  so in (gx, gz) a heading h (0..4095 au, 0x400 = 90 deg) moves
      dgx = sin(h * 2pi/4096) * spd
      dgz = -cos(h * 2pi/4096) * spd
  and the world bearing of a delta is  bearing = atan2(dgx, -dgz) * 4096/2pi.
  This matches the mod's TorusBearing (atan2(dx, -dy)) and its candidate-step
  builder (cy = py - cos(th)*32) in src/world_map_drive.inl.

ENGINE MODEL per frame (CAMERA_EXE_ANALYSIS §8, verbatim):
  desired = (cy + keyOff + bias//2) & 0xFFF
  snap heading if |wrapSigned(desired-h)| <= 0x100 else h += sign*0x200
  (large turn sets follow_inhibit); step spd=32 u/frame along h through the
  gate/slide; camera velocity ramp +-8, decay *3/4, halve near 180deg,
  clamp +-0x80, cy += cv >> 3 (arithmetic shift).

STEP GATE: candidate rejected iff dest has no ground, dest is not
  terrain-walkable (byte13 not in {32,33,34} — the ground-truth rule shared
  with routing), or |destH - curH| >= 200. The engine's own validator checks
  byte15 bit7 instead of terrain byte13; BOTH gates are evaluated every time
  and disagreements are counted per route (reported as gate_disagree).

WALL SLIDE (documented approximation of the validator's edge slide 0x53E7A0):
  when the primary step is rejected, we do NOT know the blocking edge's exact
  direction here, so the slide is approximated by probing headings
  h +- 0x100, +- 0x200, +- 0x300 (nearest first), taking the FIRST passing
  direction within 90 deg of h with magnitude spd*cos(offset) — a projection
  of the intended step onto the (quantized) wall tangent. +-0x400 (90 deg)
  has zero projected magnitude and therefore contributes no motion (skipped).
  If nothing passes, no movement that frame.

PLANNER (mirrors the mod's PlanPathGrid): 128u-cell A*, 8-neighbour, every
  edge sub-marched at 32u with no-ground / non-walkable / |dH|>=gate rejection
  via the oracle; bounding box = endpoints + margin, expanded on failure.
  DIVERGENCE from the mod: no road preference (the mod's s_roadFine road-cost
  layer has no counterpart in the raw wmx.obj data available offline).

EXECUTORS:
  (a) CURRENT-.200 model: per frame the driver computes knear from the REAL
      camera yaw register and probes the 8 keys in the mod's PORD staircase
      order (world_map_drive.inl ~1450-1500), feeding the chosen key into the
      full engine model (camera lag, turn rate and slide all apply).
  (b) CAMERA-WRITE model (§9): per frame the driver writes
      cy = bearing-to-waypoint - bias//2 and cv = 0, then holds UP; the engine
      converges h within <= 4 frames. Movement/gate/slide identical to (a).
  Waypoint advance is TIGHT: advance within 48u, lookahead 1 waypoint max.

REGION CAMERA LOCK: optional mode — between driver writes the engine pulls cy
  toward a forced yaw (0xC76D22 behaviour: +-0x20/frame, snap within 0x20).
  Modelled conservatively BEFORE the input transform, so executor (b) sees a
  max transient error of 0x20 (2.8 deg) per frame.

Pure stdlib. `python3 nav_sim.py [wmx.obj path] [--out results.json]` runs the
full 24-route validation matrix + robustness + a-vs-b comparison, writing
incremental JSON after every route.

================================ BAT .201 UPDATE ==============================
The v0.18.3.201 G-Garden->Dollet freeze (see offline/BAT201_ANALYSIS.md) was
replicated offline and the TRUE engine collision model was FITTED from the
observed motion (545 accepted walked steps + 886-frame d+0 freeze):

  FITTED ENGINE GATE -- a step along heading h is accepted iff
    dest  = pos + dir(h)*32  : ground exists, FIRST-CONTAINING poly has
                               byte15 bit7 = 1, and |H(dest)-H(pos)| < 200
    probe = pos + dir(h)*112 : ground exists, FIRST-CONTAINING poly bit7 = 1
  Probe failure is a HARD block: NO wall slide (matches the logged d+0 with
  walkable ground 32u ahead and to both sides). Fitted probe window from the
  log: [101, 126] u; 112 canonical. Replay parity: feeding the logged camera
  writes into this model reproduces the freeze 8.1 u from the logged point.

New switchable machinery (old classes above are unchanged):
  EngineSim2(gate="fitted"|"legacy")   engine with the fitted gate
  Planner2(mode="fitted"|"legacy")     F1 planner gate (directed edges,
                                       32u sub-march + 112u probe, goal relax)
                                       + learned-blocked overlay (F3)
  run_drive(...)                       camera-write executor with the mod's
                                       cursor logic: v.201 goalDist skip
                                       (cursor_skip=True) or F2 route-progress
                                       watchdog + F3 fan-out/breadcrumb/learn
  drive_mission(...)                   plan->drive->replan loop w/ overlay
  GridOracle / build_grids(...)        optional 32u precomputed grids (speed)
  python3 nav_sim.py --bat201          runs the BAT201 fix-suite + 24 matrix
==============================================================================
"""

import json
import math
import os
import sys
import time
import heapq

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import ff8_walkmesh
from ff8_walkmesh import WMX, wrap_gx, wrap_gz, OCEAN_TERRAIN

TAU = 2.0 * math.pi
SPD = 32                 # engine on-foot validator step, u/frame
STEP_GATE = 200          # executor gate (engine 0xC8) — routing may use 160
SQRT2 = 1.4142135623730951

# 8-way key vectors as the engine input filler builds them (§3):
# cardinals +-127, diagonals +-90; k counted clockwise from UP.
KEYVEC = {0: (0, 127), 1: (90, 90), 2: (127, 0), 3: (90, -90),
          4: (0, -127), 5: (-90, -90), 6: (-127, 0), 7: (-90, 90)}
PORD = (0, 1, -1, 2, -2, 3, -3, 4)   # the mod's nearest-first probe order


def wrap_signed(a):
    return ((a + 2048) & 0xFFF) - 2048


def au_sin(h):
    return math.sin(h * TAU / 4096.0)


def au_cos(h):
    return math.cos(h * TAU / 4096.0)


def bearing_au(dx, dz):
    """World bearing (au) of a game-coord delta; 0 = -gz, 0x400 = +gx."""
    return int(round(math.atan2(dx, -dz) * 4096.0 / TAU)) & 0xFFF


def tdist(ax, az, bx, bz):
    return math.hypot(wrap_gx(bx - ax), wrap_gz(bz - az))


def tbearing(ax, az, bx, bz):
    return bearing_au(wrap_gx(bx - ax), wrap_gz(bz - az))


# ---------------------------------------------------------------------- oracle

class Oracle:
    """8u-quantized cache over WMX.query (both prefer modes). One WMX instance
    is shared across planner + executors + all routes."""

    def __init__(self, wmx):
        self.wmx = wmx
        self._t = {}     # (kx,kz) -> (height|None, terrain_ok)
        self._b = {}     # (kx,kz) -> (height|None, bit7_ok)

    def terr(self, gx, gz):
        """(height, walkable) under the ROUTING/ground-truth rule (byte13)."""
        k = (int(gx // 8), int(gz // 8))
        v = self._t.get(k)
        if v is None:
            h, _w7, t, _bi, _pi = self.wmx.query(wrap_gx(k[0] * 8), wrap_gz(k[1] * 8),
                                                 prefer="terrain")
            v = (h, h is not None and t not in OCEAN_TERRAIN)
            self._t[k] = v
        return v

    def bit7(self, gx, gz):
        """(height, walkable) under the engine validator's byte15-bit7 rule."""
        k = (int(gx // 8), int(gz // 8))
        v = self._b.get(k)
        if v is None:
            h, w7, _t, _bi, _pi = self.wmx.query(wrap_gx(k[0] * 8), wrap_gz(k[1] * 8),
                                                 prefer="bit7")
            v = (h, h is not None and w7)
            self._b[k] = v
        return v

    def first(self, gx, gz):
        """(height, walkable_bit7) of the FIRST-CONTAINING poly in stored
        order -- the engine validator's own selection (BAT201 fitted model).
        8u-quantized cache like terr()/bit7()."""
        try:
            cache = self._first_cache
        except AttributeError:
            cache = self._first_cache = {}
        k = (int(gx // 8), int(gz // 8))
        v = cache.get(k)
        if v is None:
            wm = self.wmx
            px, pz = wrap_gx(k[0] * 8), wrap_gz(k[1] * 8)
            bi, bc, row_eng, lx, lz = wm.locate(px, pz)
            polys, verts = wm._block(bc, 95 - row_eng)
            qx, qz = lx, lz - 2048
            v = (None, False)
            for (i0, i1, i2, b13, b14, b15) in polys:
                j0, j1, j2 = i0 * 4, i1 * 4, i2 * 4
                x0, z0 = verts[j0], verts[j0 + 2]
                x1, z1 = verts[j1], verts[j1 + 2]
                x2, z2 = verts[j2], verts[j2 + 2]
                c0 = (x1 - x0) * (qz - z0) - (z1 - z0) * (qx - x0)
                c1 = (x2 - x1) * (qz - z1) - (z2 - z1) * (qx - x1)
                c2 = (x0 - x2) * (qz - z2) - (z0 - z2) * (qx - x2)
                if ((c0 >= 0 and c1 >= 0 and c2 >= 0)
                        or (c0 <= 0 and c1 <= 0 and c2 <= 0)):
                    h = wm._interp(qx, qz, x0, verts[j0 + 1], z0,
                                   x1, verts[j1 + 1], z1, x2, verts[j2 + 1], z2)
                    v = (h, bool(b15 & 0x80))
                    break
            cache[k] = v
        return v


# ---------------------------------------------------------------------- engine

class EngineSim:
    """Per-frame engine model: input -> heading -> movement (gate+slide) ->
    camera follow physics. CAMERA_EXE_ANALYSIS §8 difference equations."""

    def __init__(self, oracle, x, z, h=0, cy=0, bias=0, lock_yaw=None):
        self.o = oracle
        self.x = int(x)
        self.z = int(z)
        self.h = h & 0xFFF
        self.cy = cy & 0xFFF
        self.cv = 0
        self.bias = bias          # per-triangle heading bias hook (0 default)
        self.lock_yaw = lock_yaw  # region camera lock (forced yaw) or None
        # instrumentation
        self.slides = 0           # frames moved via slide approximation
        self.blocked = 0          # frames with no movement at all
        self.turn_frames = 0      # frames spent in the 0x200/frame turn
        self.max_dh = 0.0         # max |dH| over ACCEPTED steps
        self.gate_disagree = 0    # gate evaluations where byte13 vs bit7 differ

    # -- step gate: (ok_under_terrain_rule, dh). Also evaluates the bit7 rule
    #    and counts disagreements.
    def _gate(self, fx, fz, tx, tz):
        ch, cok = self.o.terr(fx, fz)
        th, tok = self.o.terr(tx, tz)
        ok_t = (ch is not None and th is not None and tok
                and abs(th - ch) < STEP_GATE)
        cb, _cbok = self.o.bit7(fx, fz)
        tb, bok = self.o.bit7(tx, tz)
        ok_b = (cb is not None and tb is not None and bok
                and abs(tb - cb) < STEP_GATE)
        if ok_t != ok_b:
            self.gate_disagree += 1
        dh = abs(th - ch) if (ch is not None and th is not None) else 0.0
        return ok_t, dh

    def _move(self, h):
        """Step SPD along h; on rejection, slide approximation (see module doc)."""
        for off in (0, 0x100, -0x100, 0x200, -0x200, 0x300, -0x300):
            mag = SPD * au_cos(off)
            hh = (h + off) & 0xFFF
            dx = int(round(au_sin(hh) * mag))
            dz = int(round(-au_cos(hh) * mag))
            nx = wrap_gx(self.x + dx)
            nz = wrap_gz(self.z + dz)
            ok, dh = self._gate(self.x, self.z, nx, nz)
            if ok:
                self.x, self.z = nx, nz
                if dh > self.max_dh:
                    self.max_dh = dh
                if off:
                    self.slides += 1
                return True
        self.blocked += 1
        return False

    def frame(self, ix, iz, rot=0, cam_write=None):
        """One world-map frame. (ix, iz) = 8-way/analog key vector; rot = manual
        camera rotate axis; cam_write = driver camera-yaw write (executor b)."""
        if cam_write is not None:                 # §9: write cy, kill velocity
            self.cy = cam_write & 0xFFF
            self.cv = 0
        if self.lock_yaw is not None:
            # arrival controller (0x558F17..0x558F7A): +-0x20/frame toward the
            # forced yaw, snap within 0x20 — applied between driver writes,
            # conservatively BEFORE the input transform.
            d = wrap_signed(self.lock_yaw - self.cy)
            if abs(d) <= 0x20:
                self.cy = self.lock_yaw & 0xFFF
            else:
                self.cy = (self.cy + (0x20 if d > 0 else -0x20)) & 0xFFF

        move_input = (ix != 0 or iz != 0)
        inhibit = not move_input
        if move_input:
            off = int(round(math.atan2(ix, iz) * 4096.0 / TAU)) & 0xFFF
            desired = (self.cy + off + self.bias // 2) & 0xFFF
            d = wrap_signed(desired - self.h)
            if abs(d) <= 0x100:
                self.h = desired
            else:
                self.h = (self.h + (0x200 if d > 0 else -0x200)) & 0xFFF
                inhibit = True
                self.turn_frames += 1
            self._move(self.h)

        # camera follow physics (§5/§8); suppressed while region-locked
        if self.lock_yaw is None:
            d2 = wrap_signed(self.h - self.cy)
            cv = self.cv
            if rot > 0:
                cv -= 8
            elif rot < 0:
                cv += 8
            elif inhibit:
                cv = 0 if abs(cv) < 8 else cv * 3 // 4
            elif abs(d2) < 0x80:
                cv = cv * 3 // 4
            elif 0x800 - abs(d2) < 0x80:
                cv >>= 1
            else:
                cv += 8 if d2 >= 0x80 else -8
            cv = max(-0x80, min(0x80, cv))
            self.cv = cv
            self.cy = (self.cy + (cv >> 3)) & 0xFFF
        else:
            self.cv = 0


# ---------------------------------------------------------------------- planner

class Planner:
    """PlanPathGrid mirror: 128u-cell A*, 8-neighbour, 32u edge sub-march.
    NOTE divergence: no s_roadFine road preference (no road data offline)."""

    def __init__(self, oracle, cell=128, gate=STEP_GATE, sub=32):
        self.o = oracle
        self.cell = cell
        self.gate = gate
        self.sub = sub
        self._edges = {}   # undirected (cellA, cellB) -> bool

    def _center(self, c):
        return (c[0] * self.cell, c[1] * self.cell)

    def _enterable(self, c):
        _h, ok = self.o.terr(*self._center(c))
        return ok

    def _snap(self, p):
        c0 = (int(round(p[0] / self.cell)), int(round(p[1] / self.cell)))
        if self._enterable(c0):
            return c0
        for r in range(1, 9):
            best, bd = None, None
            for dx in range(-r, r + 1):
                for dz in range(-r, r + 1):
                    if max(abs(dx), abs(dz)) != r:
                        continue
                    c = (c0[0] + dx, c0[1] + dz)
                    if self._enterable(c):
                        cx, cz = self._center(c)
                        d = (cx - p[0]) ** 2 + (cz - p[1]) ** 2
                        if bd is None or d < bd:
                            bd, best = d, c
            if best is not None:
                return best
        return None

    def _edge_ok(self, a, b):
        key = (a, b) if a <= b else (b, a)
        v = self._edges.get(key)
        if v is not None:
            return v
        ax, az = self._center(a)
        bx, bz = self._center(b)
        n = max(1, int(math.ceil(math.hypot(bx - ax, bz - az) / self.sub)))
        ph, ok = self.o.terr(ax, az)
        good = ok and ph is not None
        if good:
            for i in range(1, n + 1):
                x = ax + (bx - ax) * i / n
                z = az + (bz - az) * i / n
                h, ok = self.o.terr(x, z)
                if not ok or h is None or abs(h - ph) >= self.gate:
                    good = False
                    break
                ph = h
        self._edges[key] = good
        return good

    def plan(self, start, goal, margin=6144):
        """A* start->goal; returns waypoint list (fine cell centers + exact
        goal appended) or None. Bounding box = endpoints + margin."""
        s = self._snap(start)
        g = self._snap(goal)
        if s is None or g is None:
            return None
        m = max(1, margin // self.cell)
        lo_x, hi_x = min(s[0], g[0]) - m, max(s[0], g[0]) + m
        lo_z, hi_z = min(s[1], g[1]) - m, max(s[1], g[1]) + m

        def heur(c):
            dx = abs(c[0] - g[0])
            dz = abs(c[1] - g[1])
            return (dx + dz) + (SQRT2 - 2.0) * min(dx, dz)

        open_heap = [(heur(s), 0, s)]
        gscore = {s: 0.0}
        came = {}
        tie = 0
        while open_heap:
            _f, _t, cur = heapq.heappop(open_heap)
            if cur == g:
                path = [cur]
                while cur in came:
                    cur = came[cur]
                    path.append(cur)
                path.reverse()
                wps = [self._center(c) for c in path]
                wps.append((int(goal[0]), int(goal[1])))
                return wps
            base = gscore[cur]
            for dx in (-1, 0, 1):
                for dz in (-1, 0, 1):
                    if dx == 0 and dz == 0:
                        continue
                    n = (cur[0] + dx, cur[1] + dz)
                    if not (lo_x <= n[0] <= hi_x and lo_z <= n[1] <= hi_z):
                        continue
                    if not self._edge_ok(cur, n):
                        continue
                    ng = base + (SQRT2 if dx and dz else 1.0)
                    if ng < gscore.get(n, 1e30):
                        gscore[n] = ng
                        came[n] = cur
                        tie += 1
                        heapq.heappush(open_heap, (ng + heur(n), tie, n))
        return None


# -------------------------------------------------------------------- executors

def steer_a(sim, oracle, wp):
    """Executor (a): the mod's .200 8-way steering — knear from the camera-yaw
    register + PORD staircase oracle probe (world_map_drive.inl ~1450-1500)."""
    D = tbearing(sim.x, sim.z, wp[0], wp[1])
    theta = sim.cy                       # register read (hand = +1)
    rel = (D - theta) & 0xFFF
    knear = ((rel + 256) // 512) % 8
    cur_h, _ok = oracle.terr(sim.x, sim.z)
    d0 = tdist(sim.x, sim.z, wp[0], wp[1])
    best_k, best_gain, got, stair_k = knear, -1e18, False, -1
    for po in PORD:
        k = (knear + po) % 8
        wb = (theta + k * 512) & 0xFFF
        cx = sim.x + int(round(au_sin(wb) * 32.0))
        cz = sim.z - int(round(au_cos(wb) * 32.0))
        ch, ok = oracle.terr(cx, cz)
        if ch is None or not ok:
            continue
        if cur_h is not None and abs(ch - cur_h) >= STEP_GATE:
            continue
        g = d0 - tdist(cx, cz, wp[0], wp[1])
        if g > best_gain:
            best_gain, best_k, got = g, k, True
        if stair_k < 0 and g > 4.0:      # nearest-to-path progressing key
            stair_k = k
        if po == 0 and g > 4.0:          # calibrated key already advances
            break
    return stair_k if stair_k >= 0 else (best_k if got else knear)


def run_follow(oracle, wps, start, target, mode="b", h0=None, cy0=None,
               bias=0, lock=None, advance_r=48, arrive_r=400, cap=None):
    """Drive the engine model along waypoints. Tight advance (48u), lookahead
    1 waypoint max. Returns a result dict.

    FINDINGS incorporated (both feed the mod implementation):
      * HEIGHT-AWARE ADVANCE — the 2D-only radius test can mark a waypoint
        'reached' while the character stands ~200u BELOW it (the validated
        climb runs along the polyline; a cliff flanks it laterally). A
        waypoint only counts reached when within advance_r AND |charH - wpH|
        < 100, so the executor keeps steering at the climb instead of aiming
        past it into the cliff face (the Timber->Dollet wedge).
      * BREADCRUMB RECOVERY — the |dH|<200 gate and walkability are symmetric,
        so the character's own trail is always re-walkable. If no movement
        happens for 20 consecutive frames, retrace recorded breadcrumbs
        (dropped every >=16u of travel) until a direct 32u probe toward the
        current waypoint passes the gate again, then resume. Escapes marginal
        pockets that planner-level gates cannot fully exclude."""
    pathlen = tdist(start[0], start[1], wps[0][0], wps[0][1])
    for i in range(len(wps) - 1):
        pathlen += tdist(wps[i][0], wps[i][1], wps[i + 1][0], wps[i + 1][1])
    if cap is None:
        cap = int(pathlen / SPD * 6) + 3000
    b0 = tbearing(start[0], start[1], wps[0][0], wps[0][1])
    sim = EngineSim(oracle, start[0], start[1],
                    h=(h0 if h0 is not None else b0),
                    cy=(cy0 if cy0 is not None else b0),
                    bias=bias, lock_yaw=lock)

    def reached(i):
        if tdist(sim.x, sim.z, *wps[i]) > advance_r:
            return False
        wh, _ = oracle.terr(*wps[i])
        ch, _ = oracle.terr(sim.x, sim.z)
        return wh is None or ch is None or abs(ch - wh) < 100

    def probe_ok(wp):
        bb = tbearing(sim.x, sim.z, wp[0], wp[1])
        nx = wrap_gx(sim.x + int(round(au_sin(bb) * SPD)))
        nz = wrap_gz(sim.z - int(round(au_cos(bb) * SPD)))
        ok, _dh = sim._gate(sim.x, sim.z, nx, nz)
        return ok

    wi = 0
    hist = []
    crumbs = []
    frames = 0
    arrived = False
    wedge = False
    recovering = False
    recoveries = 0
    blocked_streak = 0
    best_dwp = 1e18            # progress watchdog: dist to current waypoint
    best_wi = -1
    stale = 0
    while frames < cap:
        if tdist(sim.x, sim.z, target[0], target[1]) <= arrive_r:
            arrived = True
            break
        while wi < len(wps) - 1 and reached(wi):
            wi += 1
        if wi < len(wps) - 1 and reached(wi + 1):
            wi += 1                       # lookahead: 1 waypoint max
        if wi == len(wps) - 1 and reached(wi):
            arrived = True                # final waypoint reached
            break
        wp = wps[wi]
        # progress watchdog: sliding oscillation never fully blocks a frame,
        # so also enter recovery when distance-to-waypoint stops improving.
        dwp = tdist(sim.x, sim.z, wp[0], wp[1])
        if wi != best_wi:
            best_wi, best_dwp, stale = wi, dwp, 0
        elif dwp < best_dwp - 1.0:
            best_dwp, stale = dwp, 0
        else:
            stale += 1
            if not recovering and stale >= 150:
                recovering = True
                recoveries += 1
                stale = 0
        if recovering:
            if probe_ok(wp):
                recovering = False        # path to the waypoint is open again
            else:
                while crumbs and tdist(sim.x, sim.z, *crumbs[-1]) < SPD:
                    crumbs.pop()
                wp = crumbs[-1] if crumbs else wps[max(0, wi - 1)]
        pre = (sim.x, sim.z)
        if mode == "b":
            bb = tbearing(sim.x, sim.z, wp[0], wp[1])
            sim.frame(0, 127, cam_write=(bb - sim.bias // 2) & 0xFFF)
        else:
            k = steer_a(sim, oracle, wp)
            sim.frame(*KEYVEC[k])
        if (sim.x, sim.z) == pre:
            blocked_streak += 1
            if not recovering and blocked_streak >= 20:
                recovering = True
                recoveries += 1
                blocked_streak = 0
        else:
            blocked_streak = 0
            if not crumbs or tdist(sim.x, sim.z, *crumbs[-1]) >= 16:
                crumbs.append((sim.x, sim.z))
        hist.append((sim.x, sim.z))
        frames += 1
        if frames >= 300:
            ox, oz = hist[-300]
            if tdist(sim.x, sim.z, ox, oz) < 16:   # no net progress, 300 frames
                wedge = True
                break
    return {
        "frames": frames,
        "arrived": arrived,
        "wedge": wedge,
        "final_dist": round(tdist(sim.x, sim.z, target[0], target[1]), 1),
        "max_dh": round(sim.max_dh, 1),
        "slides": sim.slides,
        "blocked": sim.blocked,
        "turn_frames": sim.turn_frames,
        "gate_disagree": sim.gate_disagree,
        "recoveries": recoveries,
        "wp_reached": wi,
        "wp_total": len(wps),
    }


# ---------------------------------------------------------------------- matrix

LOCATIONS = {
    "Timber": (-22564, -4867),
    "Dollet": (-15639, -39437),
    "Galbadia Garden": (-37475, -26232),
    "Galbadia Station": (-38394, -24803),
    "Balamb Garden": (24576, -29406),
    "Fire Cavern": (30326, -29221),
    "Balamb Town": (13249, -26779),
    "Lunar Gate": (88021, 7865),
    "Sorceress Memorial": (81521, 11865),
    "Tears' Point": (83021, 31865),
}

GROUPS = {
    "Galbadia": ["Timber", "Dollet", "Galbadia Garden", "Galbadia Station"],
    "Balamb": ["Balamb Garden", "Fire Cavern", "Balamb Town"],
    "Esthar": ["Lunar Gate", "Sorceress Memorial", "Tears' Point"],
}

# (routing gate, bbox margin) escalation ladder; last entry retried with the
# executor started from the exact first waypoint.
CONFIGS = [(200, 6144), (200, 12288), (160, 12288), (160, 24576)]


def log(msg):
    print("[%s] %s" % (time.strftime("%H:%M:%S"), msg), flush=True)


def save(results, out_path):
    tmp = out_path + ".tmp"
    with open(tmp, "w") as f:
        json.dump(results, f, indent=1)
    os.replace(tmp, out_path)


def _parse_cfg(tag):
    gate, margin = 200, 6144
    for part in tag.split(","):
        if part.startswith("gate="):
            gate = int(part[5:])
        elif part.startswith("margin="):
            margin = int(part[7:])
    return gate, margin


def run_all(wmx_path, out_path):
    """Runs the full validation matrix + a-vs-b + robustness. RESUMABLE: if
    out_path already holds results, completed entries are skipped, so the run
    can be driven in bounded foreground chunks (sandbox background processes
    get reaped)."""
    log("loading WMX from %s" % wmx_path)
    oracle = Oracle(WMX(wmx_path))
    planners = {}

    def planner(gate):
        p = planners.get(gate)
        if p is None:
            p = Planner(oracle, gate=gate)
            planners[gate] = p
        return p

    results = {
        "meta": {
            "date": time.strftime("%Y-%m-%d %H:%M:%S"),
            "spd": SPD, "exec_gate": STEP_GATE, "cell": 128, "sub_march": 32,
            "advance_r": 48, "arrive_r": 400,
            "notes": "gate=terrain byte13 (routing ground truth); bit7 gate "
                     "evaluated in parallel, disagreements counted. No road "
                     "preference (s_roadFine divergence).",
        },
        "matrix": [], "a_vs_b": [], "robustness": [],
    }
    if os.path.exists(out_path):
        try:
            with open(out_path) as f:
                prev = json.load(f)
            if "matrix" in prev:
                for k in ("a_vs_b", "robustness"):
                    prev.setdefault(k, [])
                results = prev
                log("resuming: %d matrix entries already present"
                    % len(prev["matrix"]))
        except Exception as e:
            log("could not resume from %s (%s); starting fresh" % (out_path, e))

    done_matrix = {e["route"]: e for e in results["matrix"]}
    plans = {}   # (a, b) -> (wps, config)

    def get_plan(a, b):
        key = (a, b)
        if key in plans:
            return plans[key]
        e = done_matrix.get("%s -> %s" % (a, b))
        tag = e.get("config") if e else None
        gate, margin = _parse_cfg(tag) if tag else (200, 6144)
        wps = planner(gate).plan(LOCATIONS[a], LOCATIONS[b], margin=margin)
        plans[key] = (wps, tag or "gate=%d,margin=%d" % (gate, margin))
        return plans[key]

    n_ok = sum(1 for e in results["matrix"] if e.get("arrived"))
    total = 0
    for group, names in GROUPS.items():
        for a in names:
            for b in names:
                if a == b:
                    continue
                total += 1
                rname = "%s -> %s" % (a, b)
                if rname in done_matrix:
                    continue
                start, target = LOCATIONS[a], LOCATIONS[b]
                entry = {"group": group, "route": rname}
                res = None
                tried = []
                attempts = [(g, m, False) for (g, m) in CONFIGS]
                attempts.append((CONFIGS[-1][0], CONFIGS[-1][1], True))
                for gate, margin, from_wp0 in attempts:
                    t0 = time.time()
                    wps = planner(gate).plan(start, target, margin=margin)
                    pms = int((time.time() - t0) * 1000)
                    tag = "gate=%d,margin=%d%s" % (gate, margin,
                                                   ",from_wp0" if from_wp0 else "")
                    if wps is None:
                        tried.append(tag + ":no-plan")
                        continue
                    s0 = wps[0] if from_wp0 else start
                    r = run_follow(oracle, wps, s0, target, mode="b")
                    if r["arrived"]:
                        entry.update({"waypoints": len(wps), "plan_ms": pms,
                                      "config": tag})
                        entry.update(r)
                        plans[(a, b)] = (wps, tag)
                        res = r
                        n_ok += 1
                        break
                    tried.append(tag + ":%s d=%d" %
                                 ("wedge" if r["wedge"] else "timeout",
                                  r["final_dist"]))
                if res is None:
                    entry["arrived"] = False
                    entry["attempts"] = tried
                    log("FAIL  %-45s %s" % (entry["route"], tried))
                else:
                    if tried:
                        entry["earlier_attempts"] = tried
                    log("OK    %-45s wps=%-4d frames=%-5d slides=%-3d maxdh=%-5s "
                        "disagree=%d %s"
                        % (entry["route"], entry["waypoints"], res["frames"],
                           res["slides"], res["max_dh"], res["gate_disagree"],
                           entry["config"]))
                results["matrix"].append(entry)
                done_matrix[rname] = entry
                save(results, out_path)
    log("matrix done: %d/%d arrived" % (n_ok, total))

    # ---- executor a-vs-b comparison under the live wedge conditions --------
    ab_done = {(e["route"], e["executor"]) for e in results["a_vs_b"]}
    for dest in ("Dollet", "Timber"):
        rname = "Galbadia Garden -> %s" % dest
        if all((rname, m) in ab_done for m in ("a", "b")):
            continue
        wps, tag = get_plan("Galbadia Garden", dest)
        if wps is None:
            continue
        start, target = LOCATIONS["Galbadia Garden"], LOCATIONS[dest]
        for mode in ("a", "b"):
            if (rname, mode) in ab_done:
                continue
            r = run_follow(oracle, wps, start, target, mode=mode,
                           h0=3556, cy0=3556)
            e = {"route": rname, "executor": mode,
                 "start_cy": 3556, "start_h": 3556, "plan": tag}
            e.update(r)
            results["a_vs_b"].append(e)
            log("A/B   %-30s exec=%s arrived=%s frames=%d slides=%d wedge=%s "
                "turn_frames=%d d=%d"
                % (e["route"], mode, r["arrived"], r["frames"], r["slides"],
                   r["wedge"], r["turn_frames"], r["final_dist"]))
            save(results, out_path)

    # ---- robustness: (b) under adverse initial/lock/bias conditions --------
    rb_done = {(e["route"], e["case"]) for e in results["robustness"]}
    rb_routes = [("Galbadia Garden", "Dollet"),
                 ("Balamb Garden", "Fire Cavern"),
                 ("Lunar Gate", "Tears' Point")]
    for a, b in rb_routes:
        rname = "%s -> %s" % (a, b)
        wps, tag = get_plan(a, b)
        if wps is None:
            continue
        start, target = LOCATIONS[a], LOCATIONS[b]
        b0 = tbearing(start[0], start[1], wps[0][0], wps[0][1])
        cases = [
            ("cy_180_wrong", dict(h0=(b0 + 2048) & 0xFFF, cy0=(b0 + 2048) & 0xFFF)),
            ("region_lock_3556", dict(lock=3556)),
            ("bias_+64", dict(bias=64)),
            ("bias_-64", dict(bias=-64)),
        ]
        for name, kw in cases:
            if (rname, name) in rb_done:
                continue
            r = run_follow(oracle, wps, start, target, mode="b", **kw)
            e = {"route": rname, "case": name}
            e.update(r)
            results["robustness"].append(e)
            log("ROBUST %-38s %-18s arrived=%s frames=%d slides=%d d=%d"
                % (e["route"], name, r["arrived"], r["frames"], r["slides"],
                   r["final_dist"]))
            save(results, out_path)

    results["meta"]["matrix_arrived"] = n_ok
    results["meta"]["matrix_total"] = total
    save(results, out_path)
    log("ALL DONE -> %s" % out_path)
    return results


# =============================================================================
# BAT201 SECTION -- fitted engine gate + F1/F2/F3 fix suite
# (old behaviors above unchanged; see module docstring and BAT201_ANALYSIS.md)
# =============================================================================

PROBE_D = 112

# ---------------------------------------------------------------- engine
class EngineSim2(EngineSim):
    """EngineSim with switchable collision gate: 'legacy' (terrain@32 + slide
    fan) or 'fitted' (bit7 first-containing @32 dest + @112 probe, hard block)."""

    def __init__(self, oracle, x, z, h=0, cy=0, bias=0, lock_yaw=None,
                 gate="fitted"):
        super().__init__(oracle, x, z, h=h, cy=cy, bias=bias, lock_yaw=lock_yaw)
        self.gate_mode = gate
        self.last_block = None      # probe/dest point of last hard-blocked step
        self.block_cells = set()    # every 128u cell that hard-blocked a step

    def step_ok_fitted(self, fx, fz, h):
        dxu, dzu = au_sin(h), -au_cos(h)
        tx = wrap_gx(fx + int(round(dxu * SPD)))
        tz = wrap_gz(fz + int(round(dzu * SPD)))
        ch, _cw = self.o.first(fx, fz)
        th, tw = self.o.first(tx, tz)
        if ch is None or th is None or not tw or abs(th - ch) >= STEP_GATE:
            self.last_block = (tx, tz)
            self._learn_cell(tx, tz)
            return False, tx, tz, 0.0
        px = wrap_gx(fx + int(round(dxu * PROBE_D)))
        pz = wrap_gz(fz + int(round(dzu * PROBE_D)))
        ph, pw = self.o.first(px, pz)
        if ph is None or not pw:
            self.last_block = (px, pz)      # the blocking geometry (probe hit)
            self._learn_cell(px, pz)
            return False, tx, tz, abs(th - ch)
        return True, tx, tz, abs(th - ch)

    def _learn_cell(self, bx, bz):
        """Record the 128u cell of a blocking point for the learned overlay --
        but ONLY if the cell center itself is non-walkable (mountain/no-ground).
        Blocks whose cell centers are walkable (cliff fringes, narrow-pass
        walls hit obliquely) are DIRECTIONAL artefacts; fencing them poisons
        genuinely passable corridors for the planner."""
        c = (int(round(bx / 128.0)), int(round(bz / 128.0)))
        if c in self.block_cells:
            return
        h, w = self.o.first(c[0] * 128, c[1] * 128)
        if h is None or not w:
            self.block_cells.add(c)

    def _move(self, h):
        if self.gate_mode != "fitted":
            return super()._move(h)
        ok, tx, tz, dh = self.step_ok_fitted(self.x, self.z, h)
        if ok:
            self.x, self.z = tx, tz
            if dh > self.max_dh:
                self.max_dh = dh
            return True
        self.blocked += 1
        return False

# ---------------------------------------------------------------- planner
class Planner2(Planner):
    """Planner with switchable gate:
      'legacy' : the mod's v.201 gate — terrain byte13 @32u sub-march (what
                 approved the fatal corridor).
      'fitted' : F1 — bit7 first-containing @32u sub-march PLUS the 112u
                 engine probe along the edge direction from every sub-point.
    overlay: set of 128u cells with infinite cost (F3 learned engine-blocks)."""

    def __init__(self, oracle, cell=128, gate=STEP_GATE, sub=32,
                 mode="fitted", overlay=None, clearance_penalty=None):
        super().__init__(oracle, cell=cell, gate=gate, sub=sub)
        self.mode = mode
        self.overlay = overlay if overlay is not None else set()
        self._edges_f = {}
        # BAT203: soft cost multiplier for LOW-CLEARANCE cells (nonwalk within
        # ~96u of the center). Wall-hugging routes put the executor inside the
        # engine's sticky wall-foot zone; prefer centerlines.
        self.clearance_penalty = clearance_penalty
        self._clr = {}
        self.banned = set()      # BAT203: directed cell-edges failing the
                                 # swept refit gate (finer than cell fencing)

    def _low_clearance(self, c):
        v = self._clr.get(c)
        if v is None:
            cx, cz = self._center(c)
            v = False
            for hh in range(0, 4096, 256):
                dxu, dzu = au_sin(hh), -au_cos(hh)
                for t in (32, 64, 96):
                    ph, pw = self.o.first(cx + dxu * t, cz + dzu * t)
                    if ph is None or not pw:
                        v = True
                        break
                if v:
                    break
            self._clr[c] = v
        return v

    def _enterable(self, c):
        if c in self.overlay:
            return False
        if self.mode == "legacy":
            return super()._enterable(c)
        h, ok = self.o.first(*self._center(c))
        return ok

    def _edge_ok(self, a, b):
        if a in self.overlay or b in self.overlay:
            return False
        if self.mode == "legacy":
            return super()._edge_ok(a, b)
        key = (a, b)                      # DIRECTED: the engine gate is
        v = self._edges_f.get(key)        # anisotropic (112u lookahead along
        if v is not None:                 # the travel heading only)
            return v
        good = self._edge_ok_fitted(a, b)
        self._edges_f[key] = good
        return good

    GOAL_RELAX_CELLS = 8      # accept ending within 8 cells (1km) of the goal

    def plan(self, start, goal, margin=6144):
        """A* like the parent, but if the exact goal cell is unreachable
        (dead-end pockets: the 112u probe cannot 'enter' wall-adjacent cells),
        returns the path to the reachable cell CLOSEST to the goal, provided it
        is within GOAL_RELAX_CELLS. The executor's arrive radius (400u) plus
        the engine's own freeze-at-~112u handle the final approach."""
        import heapq as _hq
        s = self._snap(start)
        g = self._snap(goal)
        if s is None or g is None:
            return None
        m = max(1, margin // self.cell)
        lo_x, hi_x = min(s[0], g[0]) - m, max(s[0], g[0]) + m
        lo_z, hi_z = min(s[1], g[1]) - m, max(s[1], g[1]) + m

        def heur(c):
            dx = abs(c[0] - g[0]); dz = abs(c[1] - g[1])
            return (dx + dz) + (SQRT2 - 2.0) * min(dx, dz)

        open_heap = [(heur(s), 0, s)]
        gscore = {s: 0.0}
        came = {}
        tie = 0
        best, best_h = s, heur(s)
        while open_heap:
            _f, _t, cur = _hq.heappop(open_heap)
            if cur == g:
                best = cur
                break
            h = heur(cur)
            if h < best_h:
                best, best_h = cur, h
            base = gscore[cur]
            for dx in (-1, 0, 1):
                for dz in (-1, 0, 1):
                    if dx == 0 and dz == 0:
                        continue
                    n = (cur[0] + dx, cur[1] + dz)
                    if not (lo_x <= n[0] <= hi_x and lo_z <= n[1] <= hi_z):
                        continue
                    if not self._edge_ok(cur, n):
                        continue
                    step_c = (SQRT2 if dx and dz else 1.0)
                    if self.clearance_penalty and self._low_clearance(n):
                        step_c *= self.clearance_penalty
                    if (cur, n) in self.banned:
                        step_c *= 6.0     # swept-gate-failing edge: avoid
                                          # unless the pass is forced
                    ng = base + step_c
                    if ng < gscore.get(n, 1e30):
                        gscore[n] = ng
                        came[n] = cur
                        tie += 1
                        _hq.heappush(open_heap, (ng + heur(n), tie, n))
        if best != g and best_h > self.GOAL_RELAX_CELLS:
            return None
        cur = best
        path = [cur]
        while cur in came:
            cur = came[cur]
            path.append(cur)
        path.reverse()
        wps = [self._center(c) for c in path]
        wps.append((int(goal[0]), int(goal[1])))
        return wps

    def _edge_ok_fitted(self, a, b):
        ax, az = self._center(a)
        bx, bz = self._center(b)
        dx, dz = bx - ax, bz - az
        L = math.hypot(dx, dz)
        ux, uz = dx / L, dz / L
        n = max(1, int(math.ceil(L / self.sub)))
        ph, ok = self.o.first(ax, az)
        if not ok or ph is None:
            return False
        for i in range(1, n + 1):
            x = ax + dx * i / n
            z = az + dz * i / n
            h, ok = self.o.first(x, z)
            if not ok or h is None or abs(h - ph) >= self.gate:
                return False
            ph = h
            hh, ww = self.o.first(x + ux * PROBE_D, z + uz * PROBE_D)
            if hh is None or not ww:
                return False
        return True

# ---------------------------------------------------------------- executor
# Sim tick = one engine movement tick (~30 Hz). Time windows in ticks:
T_SKIP = 45          # mod: no-progress skip window 1.5s
T_STUCK = 90         # mod: stuck-check window 3s
T_F2 = 120           # F2: route-progress window ~4s
T_F3_RESUME = 150    # F3: progress must resume within ~5s after a fan escape
FAN_OFFS = (256, -256, 512, -512, 768, -768, 1024, -1024)
FAN_HOLD = 15

def run_drive(oracle, wps, start, target, h0=None, cy0=None, gate="fitted",
              cursor_skip=False, f2=False, f3=False,
              advance_r=192, arrive_r=400, cap=40000, trace=None):
    """Camera-write executor with the MOD's cursor logic, switchable watchdogs.

    cursor_skip: v.201 behavior — goalDist no-progress (>=150u in T_SKIP) ->
                 cursor += 4 (the racing bug).
    f2:          route-progress watchdog — remaining = distToWp + 128*(len-1-idx);
                 no >=64u improvement in T_F2 -> BLOCKED (return for replan, or
                 hand to F3 if enabled). Never skips the cursor.
    f3:          engine-block recovery — UP held, net displacement <8u over 20
                 ticks -> bearing fan-out; if no fan direction moves (or progress
                 does not resume), retreat along breadcrumbs and report the
                 blocked 128u cell for the learned overlay.

    Returns dict with status: 'arrived' | 'blocked' (F2/F3 gave up; includes
    learned cell + retreat position) | 'stuck' (mod stuck-check fired; caller
    replans like v.201) | 'timeout'.
    """
    b0 = tbearing(start[0], start[1], wps[0][0], wps[0][1])
    cls = EngineSimC if gate == "cache" else EngineSim2
    sim = cls(oracle, start[0], start[1],
              h=(h0 if h0 is not None else b0),
              cy=(cy0 if cy0 is not None else b0), gate=gate)
    idx = 0
    L = len(wps)
    frames = 0
    events = []
    ring = []                 # last 20 positions (engine-block detect)
    crumbs = [(sim.x, sim.z)] # breadcrumbs every >=64u
    # mod goalDist watchdog
    best_goal = tdist(sim.x, sim.z, target[0], target[1])
    skip_timer = 0
    # mod stuck check (3s window, 2 consecutive -> stuck)
    stuck_ref = (sim.x, sim.z)
    stuck_timer = 0
    stuck_count = 0
    # F2
    def remaining():
        return tdist(sim.x, sim.z, wps[idx][0], wps[idx][1]) + 128.0 * (L - 1 - idx)
    best_rem = remaining()
    f2_timer = 0
    # F3 state
    f3_state = "normal"       # normal | fan | verify | retreat
    fan_i = 0
    fan_hold = 0
    fan_base = 0
    verify_timer = 0
    verify_rem = 0.0
    retreat_left = 0
    retreat_stall = 0
    learned = None
    freeze_pos = None
    freezes = 0

    while frames < cap:
        if tdist(sim.x, sim.z, target[0], target[1]) <= arrive_r:
            return dict(status="arrived", frames=frames, idx=idx, events=events,
                        pos=(sim.x, sim.z), blocked_frames=sim.blocked,
                        block_cells=sim.block_cells)
        # pursuit advance: 192u + height gate
        while idx < L - 1:
            d = tdist(sim.x, sim.z, wps[idx][0], wps[idx][1])
            if d >= advance_r:
                break
            wh, _ = oracle.first(*wps[idx])
            ch, _ = oracle.first(sim.x, sim.z)
            if wh is not None and ch is not None and abs(ch - wh) >= 100:
                break
            idx += 1
        wp = wps[idx]
        # ---- desired bearing
        des = tbearing(sim.x, sim.z, wp[0], wp[1])
        aim = des
        # ---- F3 state machine overrides aim
        if f3_state in ("fan", "follow"):
            aim = (fan_base + FAN_OFFS[fan_i]) & 0xFFF
        elif f3_state == "retreat":
            if crumbs and retreat_left > 0:
                tgt_c = crumbs[-1]
                if tdist(sim.x, sim.z, tgt_c[0], tgt_c[1]) < 40:
                    crumbs.pop()
                    retreat_left -= 1
                    if not crumbs or retreat_left == 0:
                        return dict(status="blocked", frames=frames, idx=idx,
                                    events=events, pos=(sim.x, sim.z),
                                    learned=learned, freeze_pos=freeze_pos,
                                    blocked_frames=sim.blocked,
                                    block_cells=sim.block_cells)
                if crumbs:
                    aim = tbearing(sim.x, sim.z, crumbs[-1][0], crumbs[-1][1])
            else:
                return dict(status="blocked", frames=frames, idx=idx,
                            events=events, pos=(sim.x, sim.z), learned=learned,
                            freeze_pos=freeze_pos, blocked_frames=sim.blocked,
                            block_cells=sim.block_cells)
        # ---- frame
        pre = (sim.x, sim.z)
        sim.frame(0, 127, cam_write=(aim - sim.bias // 2) & 0xFFF)
        moved = (sim.x, sim.z) != pre
        frames += 1
        if trace is not None and frames % 4 == 0:
            trace.append((sim.x, sim.z, idx))
        ring.append((sim.x, sim.z))
        if len(ring) > 20:
            ring.pop(0)
        if f3_state == "retreat":
            # the fitted gate is ANISOTROPIC (112u lookahead): the trail back
            # is not guaranteed re-walkable. If the retreat itself stalls,
            # end the leg -- the learned cells make the replan different.
            retreat_stall = 0 if moved else retreat_stall + 1
            if retreat_stall >= 10:
                return dict(status="blocked", frames=frames, idx=idx,
                            events=events + [("retreat_stall", frames)],
                            pos=(sim.x, sim.z), learned=learned,
                            freeze_pos=freeze_pos, blocked_frames=sim.blocked,
                            block_cells=sim.block_cells)
        if moved and (not crumbs or
                      tdist(sim.x, sim.z, crumbs[-1][0], crumbs[-1][1]) >= 64):
            crumbs.append((sim.x, sim.z))
            if len(crumbs) > 400:
                crumbs.pop(0)
        net20 = (tdist(sim.x, sim.z, ring[0][0], ring[0][1])
                 if len(ring) == 20 else 999.0)

        # ---- mod stuck check (replicates v.201 replan trigger)
        stuck_timer += 1
        if stuck_timer >= T_STUCK:
            movedw = tdist(sim.x, sim.z, stuck_ref[0], stuck_ref[1])
            stuck_ref = (sim.x, sim.z)
            stuck_timer = 0
            if movedw < 32:
                stuck_count += 1
                events.append(("stuck_check", frames, stuck_count))
                if stuck_count >= 2 and not (f2 or f3):
                    return dict(status="stuck", frames=frames, idx=idx,
                                events=events, pos=(sim.x, sim.z),
                                blocked_frames=sim.blocked,
                                block_cells=sim.block_cells)
            else:
                stuck_count = 0

        # ---- v.201 goalDist no-progress cursor skip
        if cursor_skip:
            gd = tdist(sim.x, sim.z, target[0], target[1])
            if gd < best_goal - 150:
                best_goal = gd
                skip_timer = 0
            else:
                skip_timer += 1
                if skip_timer >= T_SKIP:
                    idx = min(idx + 4, L - 1)
                    skip_timer = 0
                    events.append(("skip", frames, idx, int(gd)))

        # ---- F2 route-progress watchdog
        f2_blocked = False
        if f2:
            rem = remaining()
            if rem < best_rem - 64:
                best_rem = rem
                f2_timer = 0
            else:
                f2_timer += 1
                if f2_timer >= T_F2:
                    f2_timer = 0
                    f2_blocked = True

        # ---- F3 transitions
        if f3:
            if f3_state == "normal":
                if net20 < 8 or f2_blocked:
                    freezes += 1
                    freeze_pos = (sim.x, sim.z)
                    # learn the BLOCKING CELL: the engine probe hit (~112u ahead),
                    # i.e. the actual obstacle geometry, not the walkable fringe
                    # the character stands on.
                    if net20 < 8 and sim.last_block is not None:
                        bx, bz = sim.last_block
                        learned = (int(round(bx / 128.0)), int(round(bz / 128.0)))
                    else:
                        learned = None
                    fan_base = des
                    fan_i = 0
                    fan_hold = 0
                    if freezes > 8:          # thrashing: fence what we learned
                        f3_state = "retreat"
                        retreat_left = 10
                        events.append(("freeze_budget", frames, freeze_pos))
                    else:
                        f3_state = "fan"
                        events.append(("freeze", frames, freeze_pos, des))
            elif f3_state == "fan":
                fan_hold += 1
                if moved:
                    f3_state = "follow"      # wall-follow along the fan bearing
                    verify_timer = 0
                    events.append(("fan_escape", frames, FAN_OFFS[fan_i]))
                elif fan_hold >= FAN_HOLD:
                    fan_i += 1
                    fan_hold = 0
                    if fan_i >= len(FAN_OFFS):
                        f3_state = "retreat"
                        retreat_left = 10
                        events.append(("retreat", frames))
            elif f3_state == "follow":
                verify_timer += 1
                # resume toward the waypoint only when its bearing PASSES the
                # engine gate again (classic wall-following exit condition)
                ok_des, _tx, _tz, _dh = sim.step_ok_fitted(sim.x, sim.z, des)
                if ok_des:
                    f3_state = "normal"
                    best_rem = min(best_rem, remaining())
                    f2_timer = 0
                    events.append(("resumed", frames))
                elif not moved:              # fan bearing blocked too: next fan
                    fan_i += 1
                    fan_hold = 0
                    if fan_i >= len(FAN_OFFS):
                        f3_state = "retreat"
                        retreat_left = 10
                        events.append(("retreat", frames))
                    else:
                        f3_state = "fan"
                elif verify_timer >= 240:    # can't get around: give up, retreat
                    f3_state = "retreat"
                    retreat_left = 10
                    events.append(("retreat", frames))
        elif f2 and f2_blocked:
            return dict(status="blocked", frames=frames, idx=idx, events=events,
                        pos=(sim.x, sim.z), learned=None,
                        freeze_pos=(sim.x, sim.z), blocked_frames=sim.blocked,
                        block_cells=sim.block_cells)
    return dict(status="timeout", frames=frames, idx=idx, events=events,
                pos=(sim.x, sim.z), blocked_frames=sim.blocked,
                block_cells=sim.block_cells)


def drive_mission(oracle, start, goal, planner_mode="fitted",
                  cursor_skip=False, f2=True, f3=True, gate="fitted",
                  margin=12288, max_replans=12, cap=40000, route0=None,
                  log=None, overlay0=None, budget=None):
    """Full mission loop: plan -> drive -> (blocked/stuck -> replan) -> ...
    F3 learned cells accumulate in an overlay shared across replans."""
    overlay = set(overlay0) if overlay0 else set()
    pl = Planner2(oracle, mode=planner_mode, overlay=overlay)
    pos = (int(start[0]), int(start[1]))
    h0 = cy0 = None
    replans = 0
    total_frames = 0
    legs = []
    route_sigs = []
    t_start = time.time()
    while replans <= max_replans:
        if budget is not None and time.time() - t_start > budget:
            return dict(arrived=False, reason="budget", replans=replans,
                        legs=legs, overlay=sorted(overlay),
                        frames=total_frames, pos=pos)
        if route0 is not None and replans == 0:
            wps = [tuple(p) for p in route0]
        else:
            wps = None
            m = margin
            while wps is None and m <= 49152:
                wps = pl.plan(pos, goal, margin=m)
                m *= 2
        if wps is None:
            return dict(arrived=False, reason="no-plan", replans=replans,
                        legs=legs, overlay=sorted(overlay),
                        frames=total_frames, pos=pos)
        sig = (len(wps), wps[len(wps)//2], wps[-1])
        route_sigs.append(sig)
        r = run_drive(oracle, wps, pos, goal, h0=h0, cy0=cy0, gate=gate,
                      cursor_skip=cursor_skip, f2=f2, f3=f3, cap=cap,
                      advance_r=(192 if cursor_skip else 64))
        total_frames += r["frames"]
        legs.append(dict(status=r["status"], frames=r["frames"],
                         wps=len(wps), idx=r["idx"],
                         events=[e[:2] + tuple(str(x) for x in e[2:]) for e in r["events"]][:40],
                         end=r["pos"]))
        if log:
            log("  leg %d: %s frames=%d wps=%d idx=%d end=%s learned=%s"
                % (replans, r["status"], r["frames"], len(wps), r["idx"],
                   r["pos"], r.get("learned")))
        if r["status"] == "arrived":
            return dict(arrived=True, replans=replans, frames=total_frames,
                        legs=legs, overlay=sorted(overlay), pos=r["pos"])
        if r["status"] in ("blocked", "stuck", "timeout"):
            new_cells = {tuple(c) for c in r.get("block_cells", ())} - overlay
            overlay |= new_cells
            L = tuple(r["learned"]) if r.get("learned") else None
            if new_cells or L is None:
                pass
            else:
                # repeat freeze on an already-fenced cell: inflate the fence
                for ring in (1, 2, 3):
                    added = False
                    for dx in range(-ring, ring + 1):
                        for dz in range(-ring, ring + 1):
                            c = (L[0] + dx, L[1] + dz)
                            if c not in overlay:
                                overlay.add(c)
                                added = True
                    if added:
                        break
        pos = r["pos"]
        h0 = cy0 = None
        replans += 1
    return dict(arrived=False, reason="max-replans", replans=replans,
                legs=legs, overlay=sorted(overlay), frames=total_frames,
                pos=pos)

# ---------------------------------------------------------------- grid oracle
from array import array as _array

class GridOracle(Oracle):
    """Oracle whose first() reads precomputed 32u regional grids (fast path);
    falls back to the live WMX scan outside the regions."""
    REGIONS = {
     'galbadia': (-58000, -46000, -6000, 2000),
     'balamb':   (6000, -36000, 38000, -18000),
     'esthar':   (74000, 0, 96000, 38000),
    }
    STEP = 32
    NOG = -32768

    def __init__(self, wmx, grid_dir=None):
        super().__init__(wmx)
        self.grids = {}
        self.tgrids = {}
        if grid_dir is None:
            return
        for name, (x0, z0, x1, z1) in self.REGIONS.items():
            cols = (x1 - x0) // self.STEP + 1
            rows = (z1 - z0) // self.STEP + 1
            try:
                walk = open('%s/grid_%s_walk.bin' % (grid_dir, name), 'rb').read()
                h = _array('h'); h.frombytes(open('%s/grid_%s_h.bin' % (grid_dir, name), 'rb').read())
            except OSError:
                continue
            if len(walk) != cols * rows:
                continue
            self.grids[name] = (x0, z0, x1, z1, cols, rows, walk, h)
            try:
                tw = open('%s/grid_%s_terrwalk.bin' % (grid_dir, name), 'rb').read()
                th = _array('h'); th.frombytes(open('%s/grid_%s_terrh.bin' % (grid_dir, name), 'rb').read())
                if len(tw) == cols * rows:
                    self.tgrids[name] = (x0, z0, x1, z1, cols, rows, tw, th)
            except OSError:
                pass

    def terr(self, gx, gz):
        for (x0, z0, x1, z1, cols, rows, walk, h) in self.tgrids.values():
            if x0 <= gx <= x1 and z0 <= gz <= z1:
                c = int((gx - x0) + 16) // self.STEP
                r = int((gz - z0) + 16) // self.STEP
                if c >= cols: c = cols - 1
                if r >= rows: r = rows - 1
                i = r * cols + c
                hh = h[i]
                return (None if hh == self.NOG else float(hh)), bool(walk[i])
        return super().terr(gx, gz)

    def first(self, gx, gz):
        for (x0, z0, x1, z1, cols, rows, walk, h) in self.grids.values():
            if x0 <= gx <= x1 and z0 <= gz <= z1:
                c = int((gx - x0) + 16) // self.STEP
                r = int((gz - z0) + 16) // self.STEP
                if c >= cols: c = cols - 1
                if r >= rows: r = rows - 1
                i = r * cols + c
                hh = h[i]
                return (None if hh == self.NOG else float(hh)), bool(walk[i])
        return Oracle.first(self, gx, gz)


def build_grids(wmx, grid_dir, budget=None):
    """Precompute the 32u regional grids used by GridOracle (resumable: call
    repeatedly with a time budget until it returns True)."""
    import time as _t
    t0 = _t.time()
    for name, (x0, z0, x1, z1) in GridOracle.REGIONS.items():
        step = GridOracle.STEP
        cols = (x1 - x0) // step + 1
        rows = (z1 - z0) // step + 1
        for kind in ("", "terr"):
            wpath = '%s/grid_%s_%swalk.bin' % (grid_dir, name, kind)
            hpath = '%s/grid_%s_%sh.bin' % (grid_dir, name, kind)
            ppath = '%s/grid_%s_%s.progress' % (grid_dir, name, kind)
            done = int(open(ppath).read()) if os.path.exists(ppath) else 0
            if done >= rows:
                continue
            wf = open(wpath, 'ab'); hf = open(hpath, 'ab')
            from array import array as _arr
            r = done
            while r < rows:
                wrow = bytearray(cols); hrow = _arr('h')
                z = z0 + r * step
                for c in range(cols):
                    x = x0 + c * step
                    if kind == "terr":
                        h, _w7, t, _bi, _pi = wmx.query(x, z, prefer="terrain")
                        w = h is not None and t not in OCEAN_TERRAIN
                    else:
                        bi, bc, row_eng, lx, lz = wmx.locate(x, z)
                        polys, verts = wmx._block(bc, 95 - row_eng)
                        qx, qz = lx, lz - 2048
                        h, w = None, False
                        for (i0, i1, i2, b13, b14, b15) in polys:
                            j0, j1, j2 = i0*4, i1*4, i2*4
                            x0v, z0v = verts[j0], verts[j0+2]
                            x1v, z1v = verts[j1], verts[j1+2]
                            x2v, z2v = verts[j2], verts[j2+2]
                            c0 = (x1v-x0v)*(qz-z0v)-(z1v-z0v)*(qx-x0v)
                            c1 = (x2v-x1v)*(qz-z1v)-(z2v-z1v)*(qx-x1v)
                            c2 = (x0v-x2v)*(qz-z2v)-(z0v-z2v)*(qx-x2v)
                            if ((c0>=0 and c1>=0 and c2>=0) or (c0<=0 and c1<=0 and c2<=0)):
                                h = wmx._interp(qx, qz, x0v, verts[j0+1], z0v,
                                                x1v, verts[j1+1], z1v, x2v, verts[j2+1], z2v)
                                w = bool(b15 & 0x80)
                                break
                    wrow[c] = 1 if w else 0
                    hrow.append(-32768 if h is None else max(-32000, min(32000, int(round(h)))))
                wf.write(bytes(wrow)); hf.write(hrow.tobytes())
                r += 1
                if budget is not None and _t.time() - t0 > budget:
                    break
            wf.close(); hf.close()
            open(ppath, 'w').write(str(r))
            if budget is not None and _t.time() - t0 > budget:
                return False
    return True


def run_bat201(wmx_path, out_path, grid_dir=None, budget=None):
    """BAT201 fix-suite scenarios + 24-pair matrix under F1+F2+F3. Resumable
    (per-entry incremental JSON; per-mission state files next to out_path)."""
    wmx = WMX(wmx_path)
    if grid_dir:
        os.makedirs(grid_dir, exist_ok=True)
        if not build_grids(wmx, grid_dir, budget=budget):
            log("grid build hit budget; call again to resume")
            return None
        oracle = GridOracle(wmx, grid_dir)
    else:
        oracle = Oracle(wmx)
    res = {'fix_scenarios': [], 'matrix': [],
           'meta': {'probe_d': PROBE_D,
                    'gate': 'first-containing bit7 @32 dest (+|dH|<200) AND @112 probe; '
                            'probe-fail = hard block (no slide)'}}
    if os.path.exists(out_path):
        try:
            res = json.load(open(out_path))
        except Exception:
            pass

    def save():
        json.dump(res, open(out_path + '.tmp', 'w'), indent=1, default=list)
        os.replace(out_path + '.tmp', out_path)

    t0 = time.time()
    GG = LOCATIONS['Galbadia Garden']; DO = LOCATIONS['Dollet']
    state_dir = os.path.dirname(os.path.abspath(out_path))

    def run_resumable(key, start, goal, **kw):
        stf = os.path.join(state_dir, 'b201state_%s.json' % key.replace(' ', '_').replace('>', ''))
        st = json.load(open(stf)) if os.path.exists(stf) else None
        s0 = tuple(st['pos']) if st else start
        ov = {tuple(c) for c in st['overlay']} if st else set()
        left = None if budget is None else budget - (time.time() - t0)
        if left is not None and left < 5:
            return None
        r = drive_mission(oracle, s0, goal, overlay0=ov, budget=left, **kw)
        pr = st['replans'] if st else 0
        pf = st['frames'] if st else 0
        if r.get('reason') == 'budget':
            json.dump(dict(pos=list(r['pos']), overlay=[list(c) for c in r['overlay']],
                           replans=pr + r['replans'], frames=pf + r['frames']), open(stf, 'w'))
            return 'partial'
        outv = dict(arrived=r['arrived'], replans=pr + r['replans'], frames=pf + r['frames'],
                    end=list(r['pos']), overlay_n=len(r['overlay']), reason=r.get('reason'))
        if os.path.exists(stf):
            os.remove(stf)
        return outv

    SCEN = [
        ('S0_baseline_v201', dict(planner_mode='legacy', cursor_skip=True, f2=False, f3=False, max_replans=25, cap=6000)),
        ('S1_F1_only', dict(planner_mode='fitted', cursor_skip=True, f2=False, f3=False, max_replans=25, cap=6000)),
        ('S2_F2_only', dict(planner_mode='legacy', cursor_skip=False, f2=True, f3=False, max_replans=100, cap=6000)),
        ('S3_F3_only', dict(planner_mode='legacy', cursor_skip=True, f2=False, f3=True, max_replans=300, cap=6000)),
        ('S4_F2F3_no_F1', dict(planner_mode='legacy', cursor_skip=False, f2=True, f3=True, max_replans=300, cap=6000)),
        ('S5_F1F2F3', dict(planner_mode='fitted', cursor_skip=False, f2=True, f3=True, max_replans=12, cap=40000)),
    ]
    done = {e['name'] for e in res['fix_scenarios']}
    for name, kw in SCEN:
        if name in done:
            continue
        outv = run_resumable(name, GG, DO, gate='fitted', margin=12288, **kw)
        if outv is None or outv == 'partial':
            save(); return res
        outv['name'] = name
        res['fix_scenarios'].append(outv); save()
        log("SCEN %-16s arrived=%s replans=%d frames=%d" % (name, outv['arrived'], outv['replans'], outv['frames']))
    done_m = {e['route'] for e in res['matrix']}
    for group, names in GROUPS.items():
        for a in names:
            for b in names:
                if a == b:
                    continue
                rname = "%s -> %s" % (a, b)
                if rname in done_m:
                    continue
                outv = run_resumable(rname, LOCATIONS[a], LOCATIONS[b], planner_mode='fitted',
                                     cursor_skip=False, f2=True, f3=True, gate='fitted',
                                     margin=12288, max_replans=12, cap=40000)
                if outv is None or outv == 'partial':
                    save(); return res
                outv.update(group=group, route=rname)
                res['matrix'].append(outv); save()
                log("MATRIX %-42s arrived=%s replans=%d frames=%d" % (rname, outv['arrived'], outv['replans'], outv['frames']))
    res['meta']['matrix_arrived'] = sum(1 for e in res['matrix'] if e['arrived'])
    save()
    log("bat201 done: %d/%d matrix arrived" % (res['meta']['matrix_arrived'], len(res['matrix'])))
    return res

# =============================================================================
# BAT203 SECTION -- stateful cache-capture engine + G1-G4 global fix suite
# (BAT201 fitted model FALSIFIED at the .203 corner: 27/53 logged rejections
#  are unexplainable by ANY static point/ray/cone/swept gate while 1217 logged
#  accepted steps constrain every widening. Mechanism: 0x53EB80 find-poly's
#  8-entry MRU triangle cache is hit-tested in BLOCK-LOCAL coordinates without
#  verifying the block, so a recently-touched far/mountain triangle whose local
#  footprint contains the query point hijacks the answer ("capture"). See
#  BAT203_ANALYSIS.md.)
# =============================================================================

LOOK_D = 112          # engine per-frame look-ahead ground query (the .201 "probe")
CAM_D = 500           # camera orbit distance BEHIND the character (its ground
                      # probe is the main cache poisoner: fanning the aim
                      # sweeps the camera through nearby walls)
CACHE_N = 8           # find-poly MRU cache size (REQUIREMENTS 1.3)
SWEPT_PITCH = 8       # swept-gate sampling pitch (catches paper-thin slivers)
ZONE_R = 90           # sticky wall-foot zone radius (nonwalk within ZONE_R of
                      # the character => stateful wedge; calibrated: the .203
                      # route line ran 88u from the z=-24936 sliver and ground,
                      # the .201 freeze sat 86u off the north wall)
ZONE_ESCAPE = 23      # deterministic escape hatch: 1 accepted step per 23
                      # in-zone attempts whose base gate passes (log-calibrated
                      # escape statistics: single-step escapes every ~0.5-2 s)
# G2 upgrade: FULL-CIRCLE fan. The .203 fan stopped at +-1024; a character
# wedged in a wall pocket whose only exit is behind him never found it.
def fan_bearings(des):
    """Full-circle ABSOLUTE fan: 32 bearings at 128au pitch ordered by
    deviation from the desired course. (The .203 fan used 8 relative offsets
    up to +-1024: a character wedged in a wall pocket whose only exit cone
    (~20 deg) sits between offsets never found it.)"""
    return sorted(range(0, 4096, 128),
                  key=lambda b: abs(wrap_signed(b - des)))


class EngineSimC(EngineSim2):
    """Cache-capture engine (the BAT203 refit). Faithful find-poly with the
    8-entry MRU triangle cache; cache hits are containment tests in BLOCK-LOCAL
    coordinates with NO block check. Per frame the engine issues ground queries
      Q0 far look  (camera look target, FAR_D along the written yaw)
      Q1 standing  (character position)
      Q2 look      (LOOK_D along heading -- the distance BAT201 fitted as 112)
      Q3 validator (step destination, 32u)
    and gates the step on Q3: byte15 bit7 AND |H(Q3)-H(Q1)| < 200. Rejection is
    a hard block (no slide). A poisoned cache rejects steps into open ground --
    reproducing the .203 corner wedge; eviction dynamics produce the sporadic
    single-step escapes in the log."""

    def __init__(self, oracle, x, z, h=0, cy=0, bias=0, lock_yaw=None,
                 gate="cache", hatch="away"):
        super().__init__(oracle, x, z, h=h, cy=cy, bias=bias,
                         lock_yaw=lock_yaw, gate=gate)
        # BAT208 refit: hatch="away" -- inside the sticky wall-foot zone the
        # deterministic escape hatch only grants steps that MOVE AWAY from the
        # non-walkable geometry (zone depth strictly increases). The .208 BAT
        # proved the old unconditional hatch ("free") is too generous: live,
        # 22 s of camera-write steering W along the z=-25024 route line (88u
        # from the sliver wall) produced ZERO net progress (x pinned at
        # -44515..-44446), while fan escapes AWAY from the wall (N) did move.
        # "free" reproduced a slow westward crawl the real engine never gave.
        self.hatch = hatch
        self.cache = []          # MRU list of (key, x0,z0,x1,z1,x2,z2, hverts, b13, b15)
        self.captures = 0        # queries answered by a WRONG-block cached tri
        self.block_events = []   # (x, z, heading) of hard-blocked frames
        self._zc = 0             # in-zone blocked-attempt counter
        self._prox_cache = {}    # 8u cell -> in wall-foot zone?

    # ---- faithful find-poly + MRU capture cache
    def _fp(self, gx, gz):
        wm = self.o.wmx
        gx = wrap_gx(int(round(gx))); gz = wrap_gz(int(round(gz)))
        bi, bc, row_eng, lx, lz = wm.locate(gx, gz)
        qx, qz = lx, lz - 2048
        # cache first: BLOCK-LOCAL containment, block identity NOT verified
        for i, e in enumerate(self.cache):
            key, x0, z0, x1, z1, x2, z2, h0, h1, h2, b13, b15 = e
            c0 = (x1 - x0) * (qz - z0) - (z1 - z0) * (qx - x0)
            c1 = (x2 - x1) * (qz - z1) - (z2 - z1) * (qx - x1)
            c2 = (x0 - x2) * (qz - z2) - (z0 - z2) * (qx - x2)
            if ((c0 >= 0 and c1 >= 0 and c2 >= 0)
                    or (c0 <= 0 and c1 <= 0 and c2 <= 0)):
                if key[0] != (bc, row_eng):
                    self.captures += 1
                if i:
                    self.cache.insert(0, self.cache.pop(i))
                h = WMX._interp(qx, qz, x0, h0, z0, x1, h1, z1, x2, h2, z2)
                return h, bool(b15 & 0x80)
        # miss: walkable-PREFERRED stored-order scan of the correct block
        # (0x53EB80 per REQUIREMENTS 1.3: where a non-walkable overlay marker
        # shares the footprint of real terrain, the engine stands on -- and
        # validates against -- the walkable surface; the sliver crossing and
        # the mountain-skirt 'speckle' in the .203 log confirm it for the
        # validator too). Statefulness comes from the MRU cache ABOVE, which
        # answers with whatever containing triangle was touched most recently.
        polys, verts = wm._block(bc, 95 - row_eng)
        first = None
        hit = None
        for pi, (i0, i1, i2, b13, b14, b15) in enumerate(polys):
            j0, j1, j2 = i0 * 4, i1 * 4, i2 * 4
            x0, z0 = verts[j0], verts[j0 + 2]
            x1, z1 = verts[j1], verts[j1 + 2]
            x2, z2 = verts[j2], verts[j2 + 2]
            c0 = (x1 - x0) * (qz - z0) - (z1 - z0) * (qx - x0)
            c1 = (x2 - x1) * (qz - z1) - (z2 - z1) * (qx - x1)
            c2 = (x0 - x2) * (qz - z2) - (z0 - z2) * (qx - x2)
            if ((c0 >= 0 and c1 >= 0 and c2 >= 0)
                    or (c0 <= 0 and c1 <= 0 and c2 <= 0)):
                e = (((bc, row_eng), pi), x0, z0, x1, z1, x2, z2,
                     verts[j0 + 1], verts[j1 + 1], verts[j2 + 1], b13, b15)
                if first is None:
                    first = e
                if b15 & 0x80:
                    hit = e
                    break
        e = hit if hit is not None else first
        if e is None:
            return None, False
        key = e[0]
        for i, c in enumerate(self.cache):
            if c[0] == key:
                del self.cache[i]
                break
        self.cache.insert(0, e)
        del self.cache[CACHE_N:]
        h = WMX._interp(qx, qz, e[1], e[7], e[2], e[3], e[8], e[4],
                        e[5], e[9], e[6])
        return h, bool(e[11] & 0x80)

    def _zone_depth(self, x=None, z=None):
        """Distance to the nearest nonwalk first-containing geometry within
        ZONE_R+8 (16 swept rays at 8u pitch, raw un-gridded oracle); returns
        ZONE_R+8 when clear. 8u-cell memoised."""
        if x is None:
            x, z = self.x, self.z
        k = (int(x) // 8, int(z) // 8)
        v = self._prox_cache.get(k)
        if v is None:
            v = ZONE_R + 8
            for hh in range(0, 4096, 256):
                dxu, dzu = au_sin(hh), -au_cos(hh)
                t = 8
                while t < v:
                    ph, pw = Oracle.first(self.o, x + dxu * t, z + dzu * t)
                    if ph is None or not pw:
                        v = t
                        break
                    t += 8
            self._prox_cache[k] = v
        return v

    def _in_zone(self):
        return self._zone_depth() <= ZONE_R

    def _move(self, h):
        if self.gate_mode != "cache":
            return super()._move(h)
        dxu, dzu = au_sin(h), -au_cos(h)
        # Q0 camera ground probe: the camera rides up to CAM_D behind the
        # character (opposite the camera yaw), with the boom CLAMPED at the
        # first terrain hit. Near walls the clamped probe still touches the
        # wall-face triangles -- poisoning the find-poly cache -- but it does
        # not teleport deep inside massifs.
        bx, bz = -au_sin(self.cy), au_cos(self.cy)
        t = 32
        while t < CAM_D:
            ph, pw = Oracle.first(self.o, self.x + bx * t, self.z + bz * t)
            if ph is None or not pw:
                t -= 32          # clamp on the NEAR side of the hit
                break
            t += 32
        t = min(t, CAM_D)
        if t >= 32:
            self._fp(self.x + bx * t, self.z + bz * t)
        # Q1 standing
        ch, _cw = self._fp(self.x, self.z)
        # Q2 look-ahead (the BAT201 "112u probe")
        lxp = self.x + dxu * LOOK_D
        lzp = self.z + dzu * LOOK_D
        lh, lw = self._fp(lxp, lzp)
        # Q3 validator dest
        tx = wrap_gx(self.x + int(round(dxu * SPD)))
        tz = wrap_gz(self.z + int(round(dzu * SPD)))
        th, tw = self._fp(tx, tz)
        ok = (ch is not None and th is not None and tw
              and abs(th - ch) < STEP_GATE and lh is not None and lw)
        if ok and self._in_zone():
            # sticky wall-foot wedge: statefully reject even gate-clear steps
            ok = False
        if not ok:
            # deterministic ESCAPE HATCH (models the sporadic cache evictions
            # seen in the .203 log): every ZONE_ESCAPE-th rejected attempt the
            # MRU cache is flushed and the gate re-evaluated on the raw mesh.
            # BAT208 (hatch="away"): a zone-rejected step is only granted if
            # it strictly INCREASES the distance to the offending geometry --
            # walking parallel to a wall inside the zone never progresses
            # (matches the .208 pinned-x grind); walking out of the zone does
            # (matches the live fan escapes away from the wall).
            self._zc += 1
            if self._zc % ZONE_ESCAPE == 0:
                self.cache = []
                ch2, _ = self._fp(self.x, self.z)
                lh2, lw2 = self._fp(lxp, lzp)
                th2, tw2 = self._fp(tx, tz)
                ok = (ch2 is not None and th2 is not None and tw2
                      and abs(th2 - ch2) < STEP_GATE
                      and lh2 is not None and lw2)
                if (ok and self.hatch == "away"
                        and self._zone_depth() <= ZONE_R
                        and self._zone_depth(tx, tz) <= self._zone_depth()):
                    ok = False
        if ok:
            self.x, self.z = tx, tz
            dh = abs(th - ch)
            if dh > self.max_dh:
                self.max_dh = dh
            return True
        self.blocked += 1
        self.block_events.append((self.x, self.z, h))
        # what to report as the blocking point: the look point if it is truly
        # non-walkable, else the dest (matches the mod's CAMW-REC bookkeeping)
        if lh is None or not lw:
            self.last_block = (int(lxp), int(lzp))
        else:
            self.last_block = (tx, tz)
        self._learn_cell(*self.last_block)
        return False


# ---------------------------------------------------------------- swept gate
def swept_clear(oracle, x, z, h, dist=LOOK_D, pitch=SWEPT_PITCH):
    """e112: first-containing bit7 sampled every `pitch` u along [pitch,dist].
    The BAT203 refit ENVELOPE gate: same 1/1217 false-rejection as the .201
    point probe but catches paper-thin sliver walls the point probe stepped
    over. (27/53 logged rejections remain stateful -- G1 learning covers them.)"""
    dxu, dzu = au_sin(h), -au_cos(h)
    t = pitch
    while t <= dist:
        hh, ww = Oracle.first(oracle, x + dxu * t, z + dzu * t)
        if hh is None or not ww:
            return False
        t += pitch
    return True


def perp_clearance(oracle, x, z, h, side, learned=None, maxd=160, pitch=16):
    """Distance to the first blocked sample perpendicular to heading h
    (side=+1 right, -1 left). Learned overlay cells count as blocked."""
    hp = (h + side * 0x400) & 0xFFF          # perpendicular bearing
    px, pz = au_sin(hp), -au_cos(hp)
    t = pitch
    while t <= maxd:
        sx, sz = x + px * t, z + pz * t
        hh, ww = Oracle.first(oracle, sx, sz)
        if hh is None or not ww:
            return t
        if learned and (int(round(sx / 128.0)), int(round(sz / 128.0))) in learned:
            return t
        t += pitch
    return maxd + pitch


# ---------------------------------------------------------------- G-executor
def run_drive3(oracle, wps, start, target, h0=None, cy0=None, engine="cache",
               g1=True, g2=True, g4=True, mod_probe="swept",
               advance_r=64, arrive_r=400, cap=40000, trace=None,
               learn_cap=12, overlay=None, hatch="away"):
    """BAT203 executor. .203 semantics when g1=g2=g4=False+mod_probe='point':
    fan/wall-follow with immediate exit on first clear probe, learning only on
    fan-exhaust with the oracle-veto. G-suite:
      G1 engine-truth learning: EVERY engine-block event learns the 128u cell
         ~112u along the blocked bearing (never within 96u of the character,
         dedup, <= learn_cap per leg), trusting the ENGINE over the oracle.
      G2 wall-follow hysteresis: exit only after the desired bearing's probe
         stays clear >= 8 consecutive ticks AND >= commit distance travelled
         since the follow began; re-block within 2 s of resuming doubles the
         commitment (64->128->256->512) and resumes the SAME side without
         re-fanning; cap -> retreat.
      G4 centerline discipline: inside corridors narrower than 2*LOOK_D+64 the
         aim is offset by half the left/right clearance imbalance."""
    b0 = tbearing(start[0], start[1], wps[0][0], wps[0][1])
    cls = EngineSimC if engine == "cache" else EngineSim2
    kw = dict(hatch=hatch) if engine == "cache" else {}
    sim = cls(oracle, start[0], start[1],
              h=(h0 if h0 is not None else b0),
              cy=(cy0 if cy0 is not None else b0),
              gate=("cache" if engine == "cache" else engine), **kw)
    idx = 0
    L = len(wps)
    frames = 0
    events = []
    ring = []
    crumbs = [(sim.x, sim.z)]
    learned = set()          # G1 cells learned this leg
    ext_overlay = overlay if overlay is not None else set()

    def probe_ok(h):
        if mod_probe == "swept":
            return swept_clear(oracle, sim.x, sim.z, h)
        ok, _tx, _tz, _dh = sim.step_ok_fitted(sim.x, sim.z, h)
        return ok

    def g1_learn(h):
        if not g1 or len(learned) >= learn_cap:
            return
        dxu, dzu = au_sin(h), -au_cos(h)
        for t in (112, 176, 240):
            bx, bz = sim.x + dxu * t, sim.z + dzu * t
            c = (int(round(bx / 128.0)), int(round(bz / 128.0)))
            # never fence the character itself (cell quantization can pull
            # the 112u point's cell center inside 96u -- step further out)
            if tdist(c[0] * 128, c[1] * 128, sim.x, sim.z) < 96:
                continue
            if c not in learned and c not in ext_overlay:
                learned.add(c)
            return

    def remaining():
        return tdist(sim.x, sim.z, wps[idx][0], wps[idx][1]) + 128.0 * (L - 1 - idx)
    best_rem = remaining()
    f2_timer = 0
    f2_stall_idx = -1
    f2_stall_n = 0
    stuck_timer = 0
    stuck_ref = (sim.x, sim.z)
    stuck_count = 0
    # leg-level progress watchdog (ends the leg -> replan). Much longer than
    # F2: in-zone crawling is progress, not a stall.
    best_leg = best_rem
    leg_timer = 0

    st = "normal"            # normal | fan | follow | retreat
    fan_list = fan_bearings(0)
    fan_i = 0
    fan_hold = 0
    fan_base = 0
    fan_cycles = 0
    follow_off = 0           # the fan offset we are following
    follow_start = None      # position where the follow began
    commit = 64              # G2 commitment distance
    clear_run = 0            # G2 consecutive clear-probe ticks
    resume_frame = -10**9    # frame when we last resumed normal steering
    verify_timer = 0
    retreat_left = 0
    retreat_stall = 0
    freezes = 0
    freeze_pos = None

    def ret(status):
        return dict(status=status, frames=frames, idx=idx, events=events,
                    pos=(sim.x, sim.z), learned=learned,
                    blocked_frames=sim.blocked,
                    block_cells=getattr(sim, 'block_cells', set()),
                    captures=getattr(sim, 'captures', 0))

    while frames < cap:
        if tdist(sim.x, sim.z, target[0], target[1]) <= arrive_r:
            return ret("arrived")
        while idx < L - 1:
            d = tdist(sim.x, sim.z, wps[idx][0], wps[idx][1])
            adv = d < advance_r
            # v0.18.3.208 ABEAM advance: also advance when laterally near
            # (<=144u) AND the next waypoint is already closer than the
            # current one (walking a line parallel to the waypoint row).
            if not adv and d <= 144:
                dn = tdist(sim.x, sim.z, wps[idx + 1][0], wps[idx + 1][1])
                adv = dn < d
            if not adv:
                break
            wh, _ = oracle.first(*wps[idx])
            chh, _ = oracle.first(sim.x, sim.z)
            if wh is not None and chh is not None and abs(chh - wh) >= 100:
                break
            idx += 1
        wp = wps[idx]
        des = tbearing(sim.x, sim.z, wp[0], wp[1])
        aim = des
        # G4 centerline
        if g4 and st == "normal":
            dl = perp_clearance(oracle, sim.x, sim.z, des, -1, learned | ext_overlay)
            dr = perp_clearance(oracle, sim.x, sim.z, des, +1, learned | ext_overlay)
            if dl + dr < 2 * LOOK_D + 64:
                shift = (dr - dl) / 2.0
                shift = max(-96.0, min(96.0, shift))
                ph = (des + 0x400) & 0xFFF
                ax = wp[0] + au_sin(ph) * shift
                az = wp[1] - au_cos(ph) * shift
                aim = tbearing(sim.x, sim.z, ax, az)
        if st in ("fan", "follow"):
            aim = fan_list[fan_i]
        elif st == "retreat":
            if crumbs and retreat_left > 0:
                if tdist(sim.x, sim.z, crumbs[-1][0], crumbs[-1][1]) < 40:
                    crumbs.pop()
                    retreat_left -= 1
                    if not crumbs or retreat_left == 0:
                        return ret("blocked")
                if crumbs:
                    aim = tbearing(sim.x, sim.z, crumbs[-1][0], crumbs[-1][1])
            else:
                return ret("blocked")
        pre = (sim.x, sim.z)
        sim.frame(0, 127, cam_write=(aim - sim.bias // 2) & 0xFFF)
        moved = (sim.x, sim.z) != pre
        frames += 1
        if trace is not None and frames % 4 == 0:
            trace.append((sim.x, sim.z, idx))
        ring.append((sim.x, sim.z))
        if len(ring) > 40:
            ring.pop(0)
        if st == "retreat":
            retreat_stall = 0 if moved else retreat_stall + 1
            if retreat_stall >= 10:
                events.append(("retreat_stall", frames))
                return ret("blocked")
        if moved and (not crumbs or
                      tdist(sim.x, sim.z, crumbs[-1][0], crumbs[-1][1]) >= 64):
            crumbs.append((sim.x, sim.z))
            if len(crumbs) > 400:
                crumbs.pop(0)
        net20 = (tdist(sim.x, sim.z, ring[0][0], ring[0][1])
                 if len(ring) == 40 else 999.0)
        # 40-tick window: the sticky-zone crawl moves ~1 step / 23 ticks --
        # a 20-tick window false-fires the freeze detector mid-crawl

        stuck_timer += 1
        if stuck_timer >= T_STUCK:
            movedw = tdist(sim.x, sim.z, stuck_ref[0], stuck_ref[1])
            stuck_ref = (sim.x, sim.z)
            stuck_timer = 0
            if movedw < 32:
                stuck_count += 1
                events.append(("stuck_check", frames, stuck_count))
            else:
                stuck_count = 0

        f2_blocked = False
        rem = remaining()
        if rem < best_rem - 64:
            best_rem = rem
            f2_timer = 0
        else:
            f2_timer += 1
            if f2_timer >= T_F2:
                f2_timer = 0
                f2_blocked = True
                # v0.18.3.207 stall escalation (executor parity): 2 consecutive
                # F2 stalls at one cursor -> skip the unreachable waypoint;
                # 3 -> forced retreat (the mod then replans with new knowledge).
                if idx == f2_stall_idx:
                    f2_stall_n += 1
                else:
                    f2_stall_idx, f2_stall_n = idx, 1
                if f2_stall_n == 2 and idx < L - 1:
                    idx += 1
                    events.append(("wpskip", frames, idx))
                elif f2_stall_n >= 3:
                    f2_stall_n = 0
                    f2_stall_idx = -1
                    st = "retreat"
                    retreat_left = min(len(crumbs), 6)
                    events.append(("forced_retreat", frames))

        if st == "normal":
            if net20 < 8 or f2_blocked:
                freezes += 1
                freeze_pos = (sim.x, sim.z)
                if net20 < 8:
                    g1_learn(sim.h)          # G1: trust the engine, learn NOW
                # G2: re-block shortly after a resume -> same side, no re-fan
                if g2 and frames - resume_frame <= 60 and follow_start is not None:
                    commit = min(commit * 2, 512)
                    st = "follow"
                    follow_start = (sim.x, sim.z)
                    clear_run = 0
                    events.append(("re_follow", frames, commit))
                else:
                    fan_base = des
                    fan_list = fan_bearings(des)
                    fan_i = 0
                    fan_hold = 0
                    fan_cycles = 0
                    if freezes > 8:
                        st = "retreat"
                        retreat_left = 5
                        events.append(("freeze_budget", frames, freeze_pos))
                    else:
                        st = "fan"
                        events.append(("freeze", frames, freeze_pos, des))
        elif st == "fan":
            fan_hold += 1
            if moved:
                st = "follow"
                follow_off = fan_list[fan_i]
                follow_start = (sim.x, sim.z)
                clear_run = 0
                verify_timer = 0
                events.append(("fan_escape", frames, fan_list[fan_i]))
            elif fan_hold >= FAN_HOLD:
                g1_learn(fan_list[fan_i])       # G1: blocked fan ray
                fan_i += 1
                fan_hold = 0
                if fan_i >= len(fan_list):
                    fan_i = 0
                    fan_cycles += 1
                    if fan_cycles >= 2:
                        st = "retreat"
                        retreat_left = 5
                        events.append(("fan_exhaust", frames))
        elif st == "follow":
            verify_timer += 1
            trav = (tdist(sim.x, sim.z, follow_start[0], follow_start[1])
                    if follow_start else 0.0)
            okd = probe_ok(des)
            clear_run = clear_run + 1 if okd else 0
            exit_ok = (clear_run >= 8 and trav >= commit) if g2 else okd
            if exit_ok:
                st = "normal"
                resume_frame = frames
                best_rem = min(best_rem, remaining())
                f2_timer = 0
                events.append(("resumed", frames, int(trav)))
            elif not moved:
                g1_learn(sim.h)
                fan_i += 1 if not g2 else 0
                if g2:
                    # committed follow hit a wall: fan from here, same episode
                    fan_base = des
                    fan_list = fan_bearings(des)
                    fan_hold = 0
                    st = "fan"
                else:
                    fan_hold = 0
                    if fan_i >= len(fan_list):
                        st = "retreat"
                        retreat_left = 5
                        events.append(("retreat", frames))
                    else:
                        st = "fan"
            elif verify_timer >= 240:
                st = "retreat"
                retreat_left = 5
                events.append(("retreat", frames))
        # leg ends ONLY on a genuine long-horizon stall: the route-remaining
        # watermark has not improved >=128u across 1200 ticks (~40 s). F2 and
        # the freeze machinery keep recovering inside the leg meanwhile.
        rem2 = remaining()
        if rem2 < best_leg - 128:
            best_leg = rem2
            leg_timer = 0
        else:
            leg_timer += 1
            if leg_timer >= 1200:
                return ret("blocked")
    return ret("timeout")


# ---------------------------------------------------------------- G-mission
def validate_route_swept(oracle, wps):
    """Refit-truth (e112 swept) lazy validation of a planned polyline: returns
    the DIRECTED cell-edges whose segment fails 8u swept sampling (paper-thin
    slivers, corner cuts). Edge-granular: fencing whole cells seals 1.5-cell
    passes the pathfinder legitimately needs."""
    bad = []
    for a, b in zip(wps, wps[1:]):
        d = tdist(a[0], a[1], b[0], b[1])
        if d < 1:
            continue
        n = max(1, int(d // SWEPT_PITCH))
        for i in range(0, n + 1):
            t = min(d, i * SWEPT_PITCH)
            x = a[0] + (b[0] - a[0]) * t / d
            z = a[1] + (b[1] - a[1]) * t / d
            hh, ww = Oracle.first(oracle, x, z)
            if hh is None or not ww:
                ca = (int(round(a[0] / 128.0)), int(round(a[1] / 128.0)))
                cb = (int(round(b[0] / 128.0)), int(round(b[1] / 128.0)))
                bad.append((ca, cb))
                break
    return bad


def drive_mission3(oracle, start, goal, engine="cache", g1=True, g2=True,
                   g3=True, g4=True, mod_probe="swept", refit_validate=True,
                   margin=12288, max_replans=16, cap=40000, route0=None,
                   log=None, overlay0=None, budget=None, trace=None):
    """BAT203 mission loop with G3 replan discipline:
      * recovery replans happen ONLY when >= 1 new cell was learned since the
        last plan (otherwise the learned fence is inflated instead);
      * recovery replans start directly at the WIDE margin (49152);
      * the generic stuck-check never replans while recovery is active (the
        executor owns recovery; only 'blocked' returns trigger replanning).
    refit_validate: planner routes are lazily validated with the swept gate
    (slivers fenced before driving them)."""
    overlay = set(overlay0) if overlay0 else set()
    pl = Planner2(oracle, mode="fitted", overlay=overlay,
                  clearance_penalty=4.0)
    pos = (int(start[0]), int(start[1]))
    replans = 0
    total_frames = 0
    legs = []
    t0 = time.time()
    recovery = False

    def oracle_sane(c):
        # judged with the PLANNER's own oracle view (grid fast path): a cell
        # the planner would otherwise use
        return oracle.first(c[0] * 128, c[1] * 128)[1]

    def plan_ladder(p0):
        w = None
        m = 24576 if (g3 and recovery) else margin
        while w is None and m <= 49152:
            w = pl.plan(p0, goal, margin=m)
            m *= 2
        if w is None:
            # safety valve 1: un-fence ORACLE-SANE learned cells near the
            # character (engine-truth cells can wall the character in or
            # fence a forced corridor like the 196u pinch)
            near = {c for c in overlay if oracle_sane(c)
                    and tdist(c[0] * 128, c[1] * 128, p0[0], p0[1]) < 1536}
            if near:
                overlay.difference_update(near)
                w = pl.plan(p0, goal, margin=49152)
        if w is None:
            # safety valve 2: un-fence ALL oracle-sane cells
            sane = {c for c in overlay if oracle_sane(c)}
            if sane:
                overlay.difference_update(sane)
                w = pl.plan(p0, goal, margin=49152)
        if w is None:
            # safety valve 3: drop the ENTIRE learned overlay -- a wrong fence
            # must never turn a reachable goal into no-plan (the walk itself
            # re-learns what still matters)
            if overlay:
                overlay.clear()
                w = pl.plan(p0, goal, margin=49152)
        return w

    while replans <= max_replans:
        if budget is not None and time.time() - t0 > budget:
            return dict(arrived=False, reason="budget", replans=replans,
                        legs=legs, overlay=sorted(overlay),
                        frames=total_frames, pos=pos)
        if route0 is not None and replans == 0:
            wps = [tuple(p) for p in route0]
        else:
            wps = plan_ladder(pos)
        if wps is None:
            return dict(arrived=False, reason="no-plan", replans=replans,
                        legs=legs, overlay=sorted(overlay),
                        frames=total_frames, pos=pos)
        if refit_validate and not (route0 is not None and replans == 0):
            for _ in range(8):
                bad = [e for e in validate_route_swept(oracle, wps)
                       if e not in pl.banned]
                if not bad:
                    break
                pl.banned.update(bad)
                wps = plan_ladder(pos)
                if wps is None:
                    return dict(arrived=False, reason="no-plan-swept",
                                replans=replans, legs=legs,
                                overlay=sorted(overlay),
                                frames=total_frames, pos=pos)
        r = run_drive3(oracle, wps, pos, goal, engine=engine, g1=g1, g2=g2,
                       g4=g4, mod_probe=mod_probe, cap=cap, overlay=overlay,
                       trace=trace)
        total_frames += r["frames"]
        legs.append(dict(status=r["status"], frames=r["frames"], wps=len(wps),
                         idx=r["idx"], end=r["pos"],
                         learned=sorted(r["learned"]),
                         events=[e[:2] + tuple(str(x) for x in e[2:])
                                 for e in r["events"]][:30]))
        if log:
            log("  leg %d: %s frames=%d wps=%d idx=%d end=%s learned=%d cap=%d"
                % (replans, r["status"], r["frames"], len(wps), r["idx"],
                   r["pos"], len(r["learned"]), r.get("captures", 0)))
        if r["status"] == "arrived":
            return dict(arrived=True, replans=replans, frames=total_frames,
                        legs=legs, overlay=sorted(overlay), pos=r["pos"])
        new_cells = set(r["learned"]) - overlay
        if engine != "cache":
            new_cells |= {tuple(c) for c in r.get("block_cells", ())} - overlay
        overlay |= new_cells
        recovery = True
        if g3 and not new_cells:
            # nothing new learned: inflate the fence around the last freeze
            # instead of replanning the same route (kills sterile replans)
            lp = r.get("pos")
            c = (int(round(lp[0] / 128.0)), int(round(lp[1] / 128.0)))
            for ring in (1, 2):
                added = False
                for dx in range(-ring, ring + 1):
                    for dz in range(-ring, ring + 1):
                        cc = (c[0] + dx, c[1] + dz)
                        if cc not in overlay and \
                           tdist(cc[0] * 128, cc[1] * 128, lp[0], lp[1]) >= 96:
                            overlay.add(cc)
                            added = True
                if added:
                    break
        pos = r["pos"]
        replans += 1
    return dict(arrived=False, reason="max-replans", replans=replans,
                legs=legs, overlay=sorted(overlay), frames=total_frames,
                pos=pos)


# ---------------------------------------------------------------- replication
ROUTE203_START = (-38135, -28352)     # .203 post-re-entry replan position
NECK203 = (-44490, -24990)            # the .203 grinding cluster


def replicate203(oracle, route, log=None, cap=12000):
    """.203 replica: cache engine + the v.203 executor (point probe, immediate
    wall-follow exit, fan-exhaust-only oracle-veto learning = run_drive f2+f3).
    Expected: grinding loop near NECK203, minimal net progress, sterile replans."""
    out = drive_mission(oracle, ROUTE203_START, LOCATIONS['Dollet'],
                        planner_mode="fitted", cursor_skip=False, f2=True,
                        f3=True, gate="cache", margin=12288, max_replans=6,
                        cap=cap, route0=route, log=log)
    # analyze: blocks near the neck?
    return out


# ---------------------------------------------------------------- neck geometry
def neck_geometry(oracle):
    """Exact corridor geometry at the .203 neck (first-containing bit7,
    UNQUANTIZED -- the sliver is thinner than the oracle's 8u cache)."""
    wm = oracle.wmx

    def fx(x, z):
        bi, bc, row_eng, lx, lz = wm.locate(wrap_gx(int(x)), wrap_gz(int(z)))
        polys, verts = wm._block(bc, 95 - row_eng)
        qx, qz = lx, lz - 2048
        for (i0, i1, i2, b13, b14, b15) in polys:
            j0, j1, j2 = i0 * 4, i1 * 4, i2 * 4
            x0, z0 = verts[j0], verts[j0 + 2]
            x1, z1 = verts[j1], verts[j1 + 2]
            x2, z2 = verts[j2], verts[j2 + 2]
            c0 = (x1 - x0) * (qz - z0) - (z1 - z0) * (qx - x0)
            c1 = (x2 - x1) * (qz - z1) - (z2 - z1) * (qx - x1)
            c2 = (x0 - x2) * (qz - z2) - (z0 - z2) * (qx - x2)
            if ((c0 >= 0 and c1 >= 0 and c2 >= 0)
                    or (c0 <= 0 and c1 <= 0 and c2 <= 0)):
                return bool(b15 & 0x80)
        return False
    out = {}
    sliv = {}
    for x in (-44530, -44490, -44450, -44300, -44000, -43500):
        z0 = z1 = None
        for z in range(-24950, -24900):
            if not fx(x, z):
                if z0 is None:
                    z0 = z
                z1 = z
        sliv[str(x)] = (z0, z1)
    out['sliver_z_band_by_x'] = sliv
    lanes = {}
    for x in (-44608, -44490, -44352, -44224):
        z = -25024
        while fx(x, z) and z > -26200:
            z -= 4
        north = z + 4
        z = -25024
        while fx(x, z) and z < -24200:
            z += 4
        south = z - 4
        lanes[str(x)] = dict(north_wall=north, south_wall=south,
                             width=south - north)
    out['north_lane_by_x'] = lanes
    return out


# ---------------------------------------------------------------- bat203 runner
def run_bat203(wmx_path, out_path, grid_dir=None, budget=None, route203=None):
    """BAT203: replication + G-suite scenarios + validation matrix under the
    cache-capture engine. Resumable (incremental JSON)."""
    wmx = WMX(wmx_path)
    oracle = GridOracle(wmx, grid_dir) if grid_dir else Oracle(wmx)
    res = {'meta': {'engine': 'cache-capture (EngineSimC)',
                    'look_d': LOOK_D, 'cam_d': CAM_D, 'cache_n': CACHE_N,
                    'zone_r': ZONE_R, 'zone_escape': ZONE_ESCAPE,
                    'swept_pitch': SWEPT_PITCH},
           'scenarios': [], 'matrix': []}
    if os.path.exists(out_path):
        try:
            res = json.load(open(out_path))
        except Exception:
            pass

    def save():
        json.dump(res, open(out_path + '.tmp', 'w'), indent=1, default=list)
        os.replace(out_path + '.tmp', out_path)

    t0 = time.time()

    def left():
        return None if budget is None else budget - (time.time() - t0)

    if 'neck' not in res:
        res['neck'] = neck_geometry(oracle)
        save()

    route = None
    if route203 and os.path.exists(route203):
        rd = json.load(open(route203))
        route = [tuple(p) for p in (rd if isinstance(rd, list) else rd['pts'])]

    state_dir = os.path.dirname(os.path.abspath(out_path))

    def run_resumable(key, start, goal, **kw):
        """drive_mission3 in budget-bounded chunks with pos/overlay state
        persisted next to out_path (sandbox background jobs get reaped)."""
        stf = os.path.join(state_dir,
                           'b203state_%s.json' % key.replace(' ', '_').replace('>', ''))
        st = json.load(open(stf)) if os.path.exists(stf) else None
        s0 = tuple(st['pos']) if st else start
        ov = {tuple(c) for c in st['overlay']} if st else set()
        lb = None if budget is None else left()
        if lb is not None and lb < 8:
            return None
        r = drive_mission3(oracle, s0, goal, overlay0=ov, budget=lb, **kw)
        pr = st['replans'] if st else 0
        pf = st['frames'] if st else 0
        if r.get('reason') == 'budget':
            json.dump(dict(pos=list(r['pos']),
                           overlay=[list(c) for c in r['overlay']],
                           replans=pr + r['replans'], frames=pf + r['frames']),
                      open(stf, 'w'))
            return 'partial'
        outv = dict(arrived=r['arrived'], replans=pr + r['replans'],
                    frames=pf + r['frames'], end=list(r['pos']),
                    overlay_n=len(r['overlay']), reason=r.get('reason'))
        if os.path.exists(stf):
            os.remove(stf)
        return outv

    done = {e['name'] for e in res['scenarios']}
    GG = LOCATIONS['Galbadia Garden']; DO = LOCATIONS['Dollet']
    SCEN = []
    if route:
        SCEN.append(('R203_replica', 'replica', {}))
    SCEN += [
        ('A_i_full_suite_refit_planner', 'mission',
         dict(g1=True, g2=True, g3=True, g4=True, mod_probe='swept',
              refit_validate=True)),
        ('A_ii_full_suite_OLD_planner_gate', 'mission',
         dict(g1=True, g2=True, g3=True, g4=True, mod_probe='point',
              refit_validate=False)),
        ('A_g1_only', 'mission',
         dict(g1=True, g2=False, g3=False, g4=False, mod_probe='point',
              refit_validate=False)),
        ('A_no_g1', 'mission',
         dict(g1=False, g2=True, g3=True, g4=True, mod_probe='swept',
              refit_validate=True)),
    ]
    for name, kind, kw in SCEN:
        if name in done:
            continue
        if left() is not None and left() < 10:
            save(); return res
        if kind == 'replica':
            r = replicate203(oracle, route, log=log, cap=9000)
            blocks = []
            for lg in r['legs']:
                for e in lg['events']:
                    if e[0] == 'freeze':
                        blocks.append(list(e))
            outv = dict(name=name, arrived=r['arrived'], replans=r['replans'],
                        frames=r['frames'], end=list(r['pos']),
                        freezes=len(blocks), first_freezes=blocks[:6],
                        near_neck=bool(tdist(r['pos'][0], r['pos'][1],
                                             NECK203[0], NECK203[1]) < 2000))
        else:
            outv = run_resumable(name, ROUTE203_START, DO, engine='cache',
                                 cap=60000, max_replans=40, log=log, **kw)
            if outv is None or outv == 'partial':
                save(); return res
            outv['name'] = name
        res['scenarios'].append(outv); save()
        log("SCEN %-36s arrived=%s replans=%s frames=%s"
            % (name, outv.get('arrived'), outv.get('replans'),
               outv.get('frames')))

    MATRIX_GROUPS = {
        'Galbadia': ['Timber', 'Dollet', 'Galbadia Garden', 'Galbadia Station'],
        'Balamb': ['Balamb Garden', 'Fire Cavern', 'Balamb Town'],
        'Esthar': ['Lunar Gate', 'Sorceress Memorial'],
    }
    done_m = {e['route'] for e in res['matrix']}
    for group, names in MATRIX_GROUPS.items():
        for a in names:
            for b in names:
                if a == b:
                    continue
                rname = "%s -> %s" % (a, b)
                if rname in done_m:
                    continue
                if left() is not None and left() < 10:
                    save(); return res
                outv = run_resumable(rname, LOCATIONS[a], LOCATIONS[b],
                                     engine='cache', g1=True, g2=True,
                                     g3=True, g4=True, mod_probe='swept',
                                     refit_validate=True, cap=60000,
                                     max_replans=40, log=log)
                if outv is None or outv == 'partial':
                    save(); return res
                outv.update(route=rname, group=group)
                res['matrix'].append(outv); save()
                log("MATRIX %-42s arrived=%s replans=%d frames=%d"
                    % (rname, outv['arrived'], outv['replans'], outv['frames']))
    res['meta']['matrix_arrived'] = sum(1 for e in res['matrix'] if e['arrived'])
    save()
    log("bat203 done: %d/%d matrix arrived"
        % (res['meta']['matrix_arrived'], len(res['matrix'])))
    return res


if __name__ == "__main__":
    args = sys.argv[1:]
    out = "sim_results.json"
    bat201 = "--bat201" in args
    bat203 = "--bat203" in args
    if bat201:
        args.remove("--bat201")
        out = "bat201_results.json"
    route203 = None
    if bat203:
        args.remove("--bat203")
        out = "bat203_results.json"
    if "--route203" in args:
        i = args.index("--route203")
        route203 = args[i + 1]
        del args[i:i + 2]
    grid_dir = None
    if "--grids" in args:
        i = args.index("--grids")
        grid_dir = args[i + 1]
        del args[i:i + 2]
    budget = None
    if "--budget" in args:
        i = args.index("--budget")
        budget = float(args[i + 1])
        del args[i:i + 2]
    if "--out" in args:
        i = args.index("--out")
        out = args[i + 1]
        del args[i:i + 2]
    wmx = args[0] if args else None
    if wmx is None:
        for cand in ("/sessions/gifted-relaxed-mendel/mnt/outputs/ff8/wmx.obj",
                     os.path.join(os.path.dirname(os.path.abspath(__file__)), "wmx.obj"),
                     "wmx.obj"):
            if os.path.exists(cand):
                wmx = cand
                break
    if wmx is None or not os.path.exists(wmx):
        print("wmx.obj not found; pass its path as the first argument")
        sys.exit(2)
    if bat203:
        run_bat203(wmx, out, grid_dir=grid_dir, budget=budget,
                   route203=route203)
    elif bat201:
        run_bat201(wmx, out, grid_dir=grid_dir, budget=budget)
    else:
        run_all(wmx, out)
