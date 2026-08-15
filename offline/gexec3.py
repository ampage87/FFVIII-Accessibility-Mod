"""Executor model matched to the cliff-aware v3 grid (see gsim3.py)."""
import sys, math
sys.path.insert(0, '/root/work')
from gsim3 import (plan, decimate, wdist, bearing, WALK, PKp, HP, g2cp, KP, RP,
                   edge_open, transition_ok, GATE, CLEAR)

STEER_DEADZONE = 320
STEER_FWD_CONE = 576
PIVOT_FRAMES   = 90      # v0.20.86: 1500 ms at 60 fps == GD_PIVOT_MS
ARRIVE = 288
LOOKAHEAD = 1536
FINAL = 1400
STALL_FRAMES = 210
STALL_ARM = 150
CLEAR_STALL_GRACE = 3     # v0.20.72: stalls with a clear path that we ride out
GUARD_MAX_DRIFT = 1000.0  # v0.20.80: a wall-follow may not carry the hull away
NEAR_GOAL_KEEP  = 3000.0  # v0.20.82: near the berth, keep the approach
FAN = (256, 512, 768, 1024, 1280, 1536, 1792, 2048)
PROBE_NEAR = 320
PROBE_FAR = 640
PROBE_TIGHT_NEAR = 320
PROBE_TIGHT_FAR = 320

# v0.20.72: THE GARDEN HAS INERTIA AND THIS MODEL DID NOT.
# Measured from Aaron's v0.20.71 BAT log, [GDTRACE] at 1 Hz from a standing
# start: 0 -> 923 -> 1841 -> 1940 units/second, then flat. That is a linear
# ramp of ~0.52 units/frame^2 to a cruise of ~32.3 units/frame, reached in
# about 62 frames. Every earlier version of this executor applied full speed
# on the first frame `wantUp` went true, which made a reverse FREE. In the
# game a reverse costs a full second of re-acceleration, and that is the
# entire mechanism behind the Centra Ruins freeze -- so no matrix built on
# the old model could ever have caught it.
ACCEL      = 0.52     # units/frame^2 while the throttle is down
DECEL      = 1.60     # units/frame^2 while it is not (brakes faster than it accelerates)
CRUISE     = 32.3     # units/frame
REV_SPEED  = 16.0     # reverse is slower, and it zeroes forward momentum


def cell(x, y):
    r, c = g2cp(x, y)
    return r, c % KP


def walkable(x, y):
    r, c = cell(x, y)
    return bool(WALK[r, c])


def park_at(x, y):
    r, c = cell(x, y)
    return bool(PKp[r, c])


def _cross(pr, pc, r, c):
    """Cell-to-cell transition legality, diagonals via either L route.

    v0.20.80: the C++ GdLineClear calls GdStepOpen here, which since v0.20.73
    includes GdTransitionOk -- the water->land beach restriction. This did not,
    so a chord that walks out of the sea onto land was legal offline and refused
    in game. Found by the replay harness, not by reasoning."""
    if pr == r and pc == c:
        return True
    dr = r - pr
    dc = c - pc
    # v0.20.69: wrap BOTH axes. dc has always wrapped; dr did not, so a chord
    # crossing the pole seam looked like a 767-row jump and every line_clear
    # over the pole returned False -- the hull could plan a route across the
    # seam and then refuse to drive a single step of it.
    if dr > RP // 2: dr -= RP
    if dr < -RP // 2: dr += RP
    if dc > KP // 2: dc -= KP
    if dc < -KP // 2: dc += KP
    if abs(dr) > 1 or abs(dc) > 1:
        return False
    if not transition_ok(pr % RP, pc % KP, r % RP, c % KP):
        return False
    if dr == 0 or dc == 0:
        return edge_open(pr, pc, r, c)
    if (WALK[pr % RP, c] and transition_ok(pr % RP, pc % KP, pr % RP, c)
            and transition_ok(pr % RP, c, r % RP, c)
            and edge_open(pr, pc, pr, c) and edge_open(pr, c, r, c)):
        return True
    if (WALK[r % RP, pc] and transition_ok(pr % RP, pc % KP, r % RP, pc % KP)
            and transition_ok(r % RP, pc % KP, r % RP, c)
            and edge_open(pr, pc, r, pc) and edge_open(r, pc, r, c)):
        return True
    return False


def line_clear(x, y, tx, ty):
    d = wdist(x, y, tx, ty)
    if d < 1.0:
        return True
    if d > 20000.0:
        return False
    steps = int(d / 96.0) + 1
    a = bearing(x, y, tx, ty) / 4096.0 * 2 * math.pi
    pr, pc = cell(x, y)
    prevH = 0
    havePrev = False
    for i in range(1, steps + 1):
        s = d * i / steps
        qx = x + math.sin(a) * s
        qy = y - math.cos(a) * s
        r, c = cell(qx, qy)
        if not WALK[r, c]:
            return False
        # v0.20.80: the 800-unit ramp gate between CONSECUTIVE 96-unit samples
        # along the chord. world_garden.inl has had this since v0.20.57 and the
        # offline model never did, so every gentle-slope chord the game refused
        # read as clear here. This is the bulk of the 22% `aim` divergence the
        # replay harness found, and `aim` is what the wall-follow guard's
        # release condition hangs on.
        if havePrev and abs(int(HP[r, c]) - prevH) >= 800:
            return False
        if (r, c) != (pr, pc):
            if not _cross(pr, pc, r, c):
                return False
            pr, pc = r, c
        prevH = int(HP[r, c])
        havePrev = True
    return True


def probe(x, y, hd, dist):
    a = (hd & 0xFFF) / 4096.0 * 2 * math.pi
    return not line_clear(x, y, x + math.sin(a) * dist, y - math.cos(a) * dist)


# v0.20.71: the bow probes have to be shorter than the gap the hull is being
# asked to drive through. PROBE_FAR is 640 units = 2.5 cells; in a corridor of
# clearance 1 (a wall about 256 units either side) a 640-unit forward probe hits
# terrain on nearly every heading, so `blocked` is stuck true, the wall-follow
# guard can never release, and the hull orbits. That is exactly the Trabia ->
# Centra trace: 4,100 frames with guard=1 the whole way, clear=1..2, going
# nowhere. Same shape as the v0.20.68 lookahead collapse and the same remedy --
# when the corridor is tight, measure a shorter distance ahead.
def probes_for(x, y):
    r, c = cell(x, y)
    return (PROBE_NEAR, PROBE_FAR) if CLEAR[r, c] >= 3 else (PROBE_TIGHT_NEAR, PROBE_TIGHT_FAR)


def pick_side(x, y, hd, pr):
    for d in FAN:
        rr = not probe(x, y, (hd + d) & 0xFFF, pr)
        ll = not probe(x, y, (hd - d) & 0xFFF, pr)
        if rr and not ll: return 1
        if ll and not rr: return -1
        if rr and ll:     return 1
    return 1


def run(sx, sy, heading, px, py, speed=64, turn=32, maxframes=200000,
        max_replans=12, trace=None):
    def replan(cx, cy, tgt=None):
        p = plan(cx, cy, px, py, clear_target=tgt)
        if p is None:
            return None
        w = decimate(p, 256)
        if wdist(w[-1][0], w[-1][1], px, py) > 1:
            w.append((px, py))
        return w

    def suffix(w):
        # v0.20.67 harness speed-up: the per-frame `rem` was O(len(wps)), which
        # at 380 waypoints x thousands of frames made the matrix untestable.
        # Precompute suffix lengths once per plan. Model-neutral.
        su = [0.0] * len(w)
        for i in range(len(w) - 2, -1, -1):
            su[i] = su[i + 1] + wdist(*w[i], *w[i + 1])
        return su

    wps = replan(sx, sy)
    if wps is None:
        return {"ok": False, "why": "planfail", "frames": 0, "end": (sx, sy),
                "replans": 0, "revs": 0}
    suf = suffix(wps)
    x, y, hd = float(sx), float(sy), heading
    spd = 0.0                      # v0.20.72: carried speed, units/frame
    idx = 0
    guardOn = False; guardDir = 0; guardFrames = 0; guardHit = 0.0; lastGuardDir = 0; guardCycles = 0; guardMinFrames = 0
    stall = 0; bestRem = 1e18; bestF = 0
    replans = 0; revs = 0; f = 0; startF = 0
    lastMoveX, lastMoveY, lastMoveF = sx, sy, 0   # v0.20.86 pivot escape
    throttleF = 0; clearStalls = 0        # v0.20.72, mirrors world_garden.inl
    gd = wdist(x, y, px, py)
    while f < maxframes:
        f += 1
        n = len(wps)
        best = idx; bd = wdist(x, y, *wps[idx])
        for j in range(idx + 1, min(idx + 41, n)):
            d = wdist(x, y, *wps[j])
            if d < bd: bd = d; best = j
        idx = best
        while idx < n - 1 and wdist(x, y, *wps[idx]) < ARRIVE:
            idx += 1
        gd = wdist(x, y, px, py)
        onPark = park_at(x, y)
        # v0.20.80: mirrors GD_ARRIVE_MAX_EXTRA -- the per-replan widening is
        # capped, so twelve replans cannot turn "arrived" into 3.4 km away.
        arriveExtra = min(256.0 * replans, 512.0)
        if gd < ARRIVE or (onPark and replans > 0 and gd < ARRIVE + arriveExtra):
            return {"ok": True, "why": "arrived", "frames": f, "end": (x, y),
                    "replans": replans, "revs": revs, "gd": round(gd)}
        if gd < FINAL and line_clear(x, y, px, py):
            tx, ty = px, py
        else:
            # v0.20.68: SHORE THREADING. A beach is one or two cells wide -- there
            # are only 8565 climbable water-to-land entries on the whole map --
            # and a 1536-unit lookahead lets the hull cut the corner and arrive
            # at the land cell across a boundary that is NOT climbable, which is
            # the engine refusing and the hull stalling at the water's edge.
            # Close to land, follow the route waypoint by waypoint instead.
            r_, c_ = cell(x, y)
            look = LOOKAHEAD if CLEAR[r_, c_] >= 3 else 256.0
            ti = idx; acc = wdist(x, y, *wps[idx])
            while ti < n - 1 and acc < look:
                acc += wdist(*wps[ti], *wps[ti + 1]); ti += 1
            while ti > idx and not line_clear(x, y, *wps[ti]):
                ti -= 1
            tx, ty = wps[ti]
        tb = bearing(x, y, tx, ty)
        err = (tb - hd) & 0xFFF
        if err > 2048: err -= 4096
        off = abs(err)
        pnear, pfar = probes_for(x, y)
        blocked = probe(x, y, hd, pnear) or probe(x, y, hd, pfar)
        aimClear = line_clear(x, y, tx, ty)
        if guardOn:
            guardFrames += 1
            held = guardFrames < guardMinFrames
            if (not held) and (not blocked) and guardFrames > 8 and (aimClear or gd < guardHit - 256):
                guardOn = False; guardDir = 0; guardFrames = 0
            elif guardFrames > 900:
                guardOn = False; guardDir = 0; guardFrames = 0
        if not guardOn and blocked:
            noProgress = guardHit > 0 and gd > guardHit - 128
            guardCycles = guardCycles + 1 if noProgress else 0
            guardOn = True; guardFrames = 0; guardMinFrames = 0
            # v0.20.71: the SIDE choice keeps the full-length fan. The two
            # probes answer different questions -- "is something immediately in
            # my way?" has to be measured at less than the corridor width, but
            # "which way round is it?" has to be measured far enough to see past
            # it. Shortening this one to 256 was what wedged Galbadia Station:
            # at that range every bearing in the fan reads clear, so the fan
            # always returned +1 and the hull committed to the wrong side.
            side = pick_side(x, y, hd, PROBE_FAR)
            if guardCycles >= 3 and lastGuardDir:
                side = -lastGuardDir; guardMinFrames = 180; guardCycles = 0
            guardDir = side; lastGuardDir = side; guardHit = gd
        if guardOn:
            hd = (hd + guardDir * turn) & 0xFFF
            wantUp = not blocked
        else:
            if off > STEER_DEADZONE:
                hd = (hd + (min(turn, off) if err >= 0 else -min(turn, off))) & 0xFFF
            wantUp = off <= STEER_FWD_CONE
            # v0.20.86: mirrors the pivot-deadlock escape in world_garden.inl.
            #
            # NOTE THE MODEL GAP THIS EXPOSES, because it is why two BATs hit the
            # same wedge and no offline run ever did: the line above turns the
            # hull whether or not the move succeeds, and THE ENGINE DOES NOT --
            # rotation is applied as part of a move, so a refused step leaves the
            # heading exactly where it was. In the game that closes the loop
            # (turn -> no move -> no turn); here it cannot. Mirroring the fix
            # keeps the two sides honest about behaviour; the coupling itself is
            # still unmodelled and this class of wedge stays invisible offline.
            if (not wantUp and not blocked and (f - lastMoveF) > PIVOT_FRAMES
                    and (f - startF) > STALL_ARM):
                wantUp = True
        moved = False
        # v0.20.72: accelerate toward the caller's cruise speed rather than
        # snapping to it. `speed` is now the CEILING, not the per-frame step.
        cruise = min(CRUISE, float(speed))
        if wantUp:
            spd = min(cruise, spd + ACCEL)
        else:
            spd = max(0.0, spd - DECEL)
        if wantUp and spd > 0.0:
            a = hd / 4096.0 * 2 * math.pi
            nx = (x + math.sin(a) * spd + 131072) % 262144 - 131072
            ny = (y - math.cos(a) * spd + 98304) % 196608 - 98304
            if walkable(nx, ny) and line_clear(x, y, nx, ny):
                x, y = nx, ny; moved = True
            else:
                spd = 0.0          # a refused move kills momentum outright
        if trace is not None:
            trace.append((x, y, hd, guardOn))
        rem = wdist(x, y, *wps[idx]) + suf[idx]
        if rem < bestRem - 256:
            bestRem = rem; bestF = f
        forceReplan = (f - bestF) > 900
        if moved:
            if wdist(x, y, lastMoveX, lastMoveY) >= 48.0:
                lastMoveX, lastMoveY, lastMoveF = x, y, f
        if moved: stall = 0
        elif wantUp: stall += 1
        stalledNow = (stall > STALL_FRAMES and (f - startF) > STALL_ARM
                      and (f - throttleF) > STALL_ARM)
        # v0.20.80: mirrors GD_GUARD_MAX_DRIFT in world_garden.inl.
        guardDrift = guardOn and guardHit > 0 and gd > guardHit + GUARD_MAX_DRIFT
        if guardDrift:
            guardOn = False; guardDir = 0; guardFrames = 0; guardHit = 0.0
            # v0.20.82: mirrors GD_NEAR_GOAL_KEEP -- close to the berth, drop the
            # guard but keep the route rather than reversing the approach away.
            if not stalledNow and gd < NEAR_GOAL_KEEP:
                guardDrift = False
                stall = 0; throttleF = f
                continue
        # v0.20.72: a stall with nothing blocking and a clear aim is not an
        # obstruction -- it is a hull that has not finished accelerating.
        # Reversing there throws the momentum away and guarantees it never
        # gets going. Hold the throttle and re-arm instead.
        if stalledNow and not blocked and aimClear and clearStalls < CLEAR_STALL_GRACE:
            clearStalls += 1
            stall = 0; throttleF = f
            continue
        if stalledNow or forceReplan or guardDrift:
            stall = 0; revs += 1; bestRem = 1e18; bestF = f; startF = f
            lastMoveX, lastMoveY, lastMoveF = x, y, f
            throttleF = f; clearStalls = 0
            a = ((hd + 2048) & 0xFFF) / 4096.0 * 2 * math.pi
            spd = 0.0              # v0.20.72: reversing zeroes forward momentum,
                                   # which is what makes the stall/reverse cycle
                                   # self-defeating in the real game.
            for _ in range(12):
                nx = (x + math.sin(a) * REV_SPEED + 131072) % 262144 - 131072
                ny = (y - math.cos(a) * REV_SPEED + 98304) % 196608 - 98304
                if not (walkable(nx, ny) and line_clear(x, y, nx, ny)):
                    break
                x, y = nx, ny; f += 1
            guardOn = False; guardDir = 0
            if replans >= max_replans:
                if onPark and gd < 6000.0:
                    return {"ok": True, "why": "fallback berth", "frames": f,
                            "end": (x, y), "replans": replans, "revs": revs, "gd": round(gd)}
                return {"ok": False, "why": "wedged", "frames": f, "end": (x, y),
                        "replans": replans, "revs": revs, "gd": round(gd)}
            w = replan(x, y, 7 if replans < 2 else (12 if replans < 5 else 18))
            replans += 1
            if w is not None:
                wps = w; idx = 0; suf = suffix(wps)
    return {"ok": False, "why": "timeout", "frames": f, "end": (x, y),
            "replans": replans, "revs": revs, "gd": round(gd)}
