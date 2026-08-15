"""Garden executor model -- FIDELITY-MATCHED to src/world_garden.inl.

The v0.20.56 BAT caught a wedge the previous simulator could not reproduce, and
the reason was a methodology error on my part: gexec.py probed the 128-unit
engine grid on the theory that "finer is a stricter test". It is not a stricter
test, it is a DIFFERENT one. The shipped GdProbe / GdLineClear read the 256-unit
planner grid, so the simulation was exercising code the game does not run.

Everything here reads exactly what the C++ reads:
  * traversability + heights from the conservative 256u grid (GWp / HP)
  * the 200-unit step gate MARCHED at 96-unit pitch, never applied across a
    whole probe length in one jump (that jump was the Centra bug)
"""
import sys, math
sys.path.insert(0, '.')
from gsim2 import plan, decimate, wdist, bearing, CELL_GATE, GWp, HP, PKp, g2cp, KP, RP

STEER_DEADZONE = 320
STEER_FWD_CONE = 576
ARRIVE         = 288          # GD_ARRIVE_DIST
LOOKAHEAD      = 1536
FINAL          = 1400
STALL_FRAMES   = 210          # GD_STALL_MS 3500 at ~60fps
STALL_ARM      = 150          # GD_STALL_ARM_MS 2500
FAN = (256, 512, 768, 1024, 1280, 1536, 1792, 2048)
PROBE_NEAR = 320
PROBE_FAR  = 640


def cell(x, y):
    r, c = g2cp(x, y)
    return r, c % KP


def walkable(x, y):
    r, c = cell(x, y)
    return bool(GWp[r, c])


def park_at(x, y):
    r, c = cell(x, y)
    return bool(PKp[r, c])


def line_clear(x, y, tx, ty):
    """Port of GdLineClear: 96-unit pitch, step gate between CONSECUTIVE samples."""
    d = wdist(x, y, tx, ty)
    if d < 1.0:
        return True
    if d > 20000.0:
        return False
    steps = int(d / 96.0) + 1
    a = bearing(x, y, tx, ty) / 4096.0 * 2 * math.pi
    prev_h = None
    for i in range(1, steps + 1):
        s = d * i / steps
        qx = x + math.sin(a) * s
        qy = y - math.cos(a) * s
        r, c = cell(qx, qy)
        if not GWp[r, c]:
            return False
        h = int(HP[r, c])
        if prev_h is not None and abs(h - prev_h) >= CELL_GATE:
            return False
        prev_h = h
    return True


def probe(x, y, hd, dist):
    """Port of the FIXED GdProbe: blocked iff the line to the probe point is not clear."""
    a = (hd & 0xFFF) / 4096.0 * 2 * math.pi
    return not line_clear(x, y, x + math.sin(a) * dist, y - math.cos(a) * dist)


def probe_oldbug(x, y, hd, dist):
    """The v0.20.56 rule, kept so the regression can be demonstrated."""
    a = (hd & 0xFFF) / 4096.0 * 2 * math.pi
    nx = x + math.sin(a) * dist
    ny = y - math.cos(a) * dist
    r0, c0 = cell(x, y)
    r1, c1 = cell(nx, ny)
    if not GWp[r1, c1]:
        return True
    return abs(int(HP[r1, c1]) - int(HP[r0, c0])) >= 200


def pick_side(x, y, hd, pr, P):
    for d in FAN:
        rr = not P(x, y, (hd + d) & 0xFFF, pr)
        ll = not P(x, y, (hd - d) & 0xFFF, pr)
        if rr and not ll: return 1
        if ll and not rr: return -1
        if rr and ll:     return 1
    return 1


def run(sx, sy, heading, px, py, speed=64, turn=32, maxframes=200000,
        max_replans=12, buggy_probe=False, trace=None):
    P = probe_oldbug if buggy_probe else probe

    def replan(cx, cy, tgt=None):
        p = plan(cx, cy, px, py, clear_target=tgt)
        if p is None:
            return None
        w = decimate(p, 256)
        if wdist(w[-1][0], w[-1][1], px, py) > 1:
            w.append((px, py))
        return w

    wps = replan(sx, sy)
    if wps is None:
        return {"ok": False, "why": "planfail", "frames": 0, "end": (sx, sy),
                "replans": 0, "revs": 0}
    x, y, hd = float(sx), float(sy), heading
    idx = 0
    guardOn = False; guardDir = 0; guardFrames = 0; guardHit = 0.0
    stall = 0; bestRem = 1e18; bestF = 0
    replans = 0; revs = 0; f = 0; startF = 0
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
        if gd < ARRIVE or (onPark and replans > 0 and gd < ARRIVE + 256.0 * replans):
            return {"ok": True, "why": "arrived", "frames": f, "end": (x, y),
                    "replans": replans, "revs": revs, "gd": round(gd)}
        if gd < FINAL and line_clear(x, y, px, py):
            tx, ty = px, py
        else:
            ti = idx; acc = wdist(x, y, *wps[idx])
            while ti < n - 1 and acc < LOOKAHEAD:
                acc += wdist(*wps[ti], *wps[ti + 1]); ti += 1
            while ti > idx and not line_clear(x, y, *wps[ti]):
                ti -= 1
            tx, ty = wps[ti]
        tb = bearing(x, y, tx, ty)
        err = (tb - hd) & 0xFFF
        if err > 2048: err -= 4096
        off = abs(err)
        blocked = P(x, y, hd, PROBE_NEAR) or P(x, y, hd, PROBE_FAR)
        aimClear = line_clear(x, y, tx, ty)
        if guardOn:
            guardFrames += 1
            if not blocked and guardFrames > 8 and (aimClear or gd < guardHit - 256):
                guardOn = False; guardDir = 0; guardFrames = 0
        if not guardOn and blocked:
            guardOn = True; guardFrames = 0; guardHit = gd
            guardDir = pick_side(x, y, hd, PROBE_FAR, P)
        if guardOn:
            hd = (hd + guardDir * turn) & 0xFFF
            wantUp = not blocked
        else:
            if off > STEER_DEADZONE:
                hd = (hd + (min(turn, off) if err >= 0 else -min(turn, off))) & 0xFFF
            wantUp = off <= STEER_FWD_CONE
        moved = False
        if wantUp:
            a = hd / 4096.0 * 2 * math.pi
            nx = (x + math.sin(a) * speed + 131072) % 262144 - 131072
            ny = (y - math.cos(a) * speed + 98304) % 196608 - 98304
            if walkable(nx, ny) and line_clear(x, y, nx, ny):
                x, y = nx, ny; moved = True
        if trace is not None:
            trace.append((x, y, hd, guardOn))
        rem = wdist(x, y, *wps[idx]) + sum(
            wdist(*wps[i], *wps[i + 1]) for i in range(idx, n - 1))
        if rem < bestRem - 256:
            bestRem = rem; bestF = f
        forceReplan = (f - bestF) > 900
        if moved: stall = 0
        elif wantUp: stall += 1
        if (stall > STALL_FRAMES and (f - startF) > STALL_ARM) or forceReplan:
            stall = 0; revs += 1; bestRem = 1e18; bestF = f; startF = f
            a = ((hd + 2048) & 0xFFF) / 4096.0 * 2 * math.pi
            for _ in range(12):
                nx = (x + math.sin(a) * speed + 131072) % 262144 - 131072
                ny = (y - math.cos(a) * speed + 98304) % 196608 - 98304
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
                wps = w; idx = 0
    return {"ok": False, "why": "timeout", "frames": f, "end": (x, y),
            "replans": replans, "revs": revs, "gd": round(gd)}
