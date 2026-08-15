"""Garden auto-drive executor -- the reference model that is ported to C++.

Four behaviours, each fixing a wedge mode reproduced in simulation (and each
matching a failure signature in the 2026-07-31 BAT, issue H2):

  1. LOS-CLAMPED LOOKAHEAD. The aim point is the furthest waypoint inside the
     lookahead arc whose STRAIGHT line from the hull is Garden-clear. Without
     it the lookahead cuts corners across headlands into the coast.
  2. WALL-FOLLOW GUARD. When the forward beam is blocked the drive enters a
     latched wall-follow: it commits to the side whose heading fan clears
     first, keeps turning that way, and CREEPS FORWARD whenever the current
     heading is momentarily clear. It leaves the state only when the aim point
     is line-of-sight clear again.
     The mod's current rule (turn toward the target side, never move) produces
     a +-1 turn-step limit cycle against a wall -- the "moveDist=0 for 18 s"
     signature -- because the clear branch immediately steers back into it.
  3. STALL -> REVERSE BURST -> REPLAN, counted only on frames where forward
     motion was WANTED and denied. Pivot and wall-turn frames are legitimate
     no-motion frames and must not arm the detector.
  4. MONOTONIC POLYLINE CURSOR over a 40-waypoint window, so overshooting a
     waypoint cannot strand the cursor behind the hull.
"""
import sys, math
sys.path.insert(0, '.')
from gsim2 import plan, decimate, gok, wdist, bearing, GATE, probe_blocked

STEER_DEADZONE = 320       # ~28 deg -- inside this, drive straight
STEER_FWD_CONE = 576       # ~50 deg -- outside this, pivot without throttle
ARRIVE         = 384
LOOKAHEAD      = 1536
FINAL          = 1400
STALL_FRAMES   = 120       # ~2 s of denied forward motion
FAN = (256, 512, 768, 1024, 1280, 1536, 1792, 2048)


def los_clear(x, y, tx, ty, pitch=96):
    d = wdist(x, y, tx, ty)
    if d < 1:
        return True
    n = max(1, int(d / pitch))
    b = bearing(x, y, tx, ty) / 4096.0 * 2 * math.pi
    _, ph = gok(x, y)
    for i in range(1, n + 1):
        s = d * i / n
        qx = (x + math.sin(b) * s + 131072) % 262144 - 131072
        qy = (y - math.cos(b) * s + 98304) % 196608 - 98304
        ok, h = gok(qx, qy)
        if not ok or abs(h - ph) >= GATE:
            return False
        ph = h
    return True


def pick_side(x, y, hd, probe):
    """Scan symmetric heading offsets outward; commit to the side that clears
    first. Returns -1 (left) or +1 (right)."""
    for d in FAN:
        r = not probe_blocked(x, y, (hd + d) & 0xFFF, probe)
        l = not probe_blocked(x, y, (hd - d) & 0xFFF, probe)
        if r and not l:
            return 1
        if l and not r:
            return -1
        if r and l:
            return 1
    return 1


def run(sx, sy, heading, px, py, speed=64, turn=32, maxframes=120000,
        max_replans=12, trace=None):
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
    guardDir = 0
    guardOn = False
    guardFrames = 0
    guardHit = 0.0
    stall = 0
    bestGd = 1e18; bestF = 0
    replans = 0
    revs = 0
    f = 0
    near = speed * 4
    far = speed * 8
    while f < maxframes:
        f += 1
        n = len(wps)
        # ---- cursor
        best = idx; bd = wdist(x, y, *wps[idx])
        for j in range(idx + 1, min(idx + 41, n)):
            d = wdist(x, y, *wps[j])
            if d < bd:
                bd = d; best = j
        idx = best
        while idx < n - 1 and wdist(x, y, *wps[idx]) < ARRIVE:
            idx += 1
        gd = wdist(x, y, px, py)
        if gd < ARRIVE:
            return {"ok": True, "why": "arrived", "frames": f, "end": (x, y),
                    "replans": replans, "revs": revs}
        # ---- aim point (LOS-clamped)
        if gd < FINAL and los_clear(x, y, px, py):
            tx, ty = px, py
        else:
            ti = idx; acc = wdist(x, y, *wps[idx])
            while ti < n - 1 and acc < LOOKAHEAD:
                acc += wdist(*wps[ti], *wps[ti + 1]); ti += 1
            while ti > idx and not los_clear(x, y, *wps[ti]):
                ti -= 1
            tx, ty = wps[ti]
        tb = bearing(x, y, tx, ty)
        err = (tb - hd) & 0xFFF
        if err > 2048:
            err -= 4096
        off = abs(err)
        blocked = probe_blocked(x, y, hd, near) or probe_blocked(x, y, hd, far)
        aimClear = los_clear(x, y, tx, ty)
        # ---- guard state machine
        if guardOn:
            guardFrames += 1
            # Bug2 leave condition: quit the wall either when the aim is
            # line-of-sight clear again, OR simply when we are measurably
            # closer to the goal than we were when we hit the wall. Without
            # the second clause a wall-follow can orbit an island forever --
            # in cluttered terrain the aim is almost never LOS-clear.
            if not blocked and guardFrames > 8 and (aimClear or gd < guardHit - 256):
                guardOn = False; guardDir = 0; guardFrames = 0
        if not guardOn and blocked:
            guardOn = True; guardFrames = 0; guardHit = gd
            guardDir = pick_side(x, y, hd, far)
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
            ok, nh = gok(nx, ny); _, ch = gok(x, y)
            if ok and abs(nh - ch) < GATE:
                x, y = nx, ny; moved = True
        if trace is not None:
            trace.append((x, y, hd, guardOn))
        # ---- progress watchdog on REMAINING ROUTE LENGTH (not straight-line
        # distance: a legitimate route around a continent increases that).
        rem = wdist(x, y, *wps[idx]) + sum(
            wdist(*wps[i], *wps[i + 1]) for i in range(idx, n - 1))
        if rem < bestGd - 256:
            bestGd = rem; bestF = f
        forceReplan = (f - bestF) > 900
        # ---- stall -> reverse -> replan
        if moved:
            stall = 0
        elif wantUp:
            stall += 1
        else:
            stall += 0.25          # pivot/wall-turn frames count slowly
        if stall > STALL_FRAMES or forceReplan:
            stall = 0; revs += 1; bestGd = 1e18; bestF = f
            a = ((hd + 2048) & 0xFFF) / 4096.0 * 2 * math.pi
            for _ in range(12):
                nx = (x + math.sin(a) * speed + 131072) % 262144 - 131072
                ny = (y - math.cos(a) * speed + 98304) % 196608 - 98304
                ok, nh = gok(nx, ny); _, ch = gok(x, y)
                if not (ok and abs(nh - ch) < GATE):
                    break
                x, y = nx, ny; f += 1
            guardOn = False; guardDir = 0
            if replans >= max_replans:
                return {"ok": False, "why": "wedged", "frames": f, "end": (x, y),
                        "replans": replans, "revs": revs}
            # Escalate the clearance target on repeated failure: after two
            # replans insist on open water, which trades route length for a
            # corridor the hull can actually turn in.
            w = replan(x, y, 7 if replans < 2 else (12 if replans < 5 else 18))
            replans += 1
            if w is not None:
                wps = w; idx = 0
    return {"ok": False, "why": "timeout", "frames": f, "end": (x, y),
            "replans": replans, "revs": revs}
