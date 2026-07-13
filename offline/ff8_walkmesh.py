"""ff8_walkmesh.py — faithful offline port of FF8 (Steam 2013) world-map walking collision.

Rebuilt 2026-07-01 from offline/REQUIREMENTS.md (the faithful-port spec) after the
original session's code was lost. Pure stdlib; importable.

Ported engine functions (FF8_EN.exe):
  0x53DC70  locator          game coords -> block index + block-local point
  0x553E00  block loader     block index -> segment/sub-block pointer (no vertex transform)
  0x53EB80  find-poly        first containing triangle, WALKABLE-preferred (see REQUIREMENTS 1.3)
  0x402620  point-in-tri     2D X/Z sign test + planar (barycentric) height interpolation
  0x53E7A0  step validator   reject iff dest non-walkable (byte15 bit7) or |candH-curH| >= 200

Raw mesh: wmx.obj = 30,781,440 bytes = 835 segments x 0x9000. Segments 0-767 are the
ground grid (32 wide x 24 tall), 4x4 sub-blocks each -> 128x96 blocks of 2048 units.
Block: byte0=polyCount, byte1=vertCount, byte2=normalCount; polys at +4, 16 bytes each
(vertex idx @0,1,2; type tuple @13,14,15; foot-walkable iff byte15 & 0x80); then verts,
8 bytes each (word0=X, word1=HEIGHT up=negative, word2=Z runs 0..-2048, word3=pad).

File <-> game transform (REQUIREMENTS 2.1 — the file is row-mirrored vs engine indexing):
  engine block (blockCol,rowEng) -> fileCol = blockCol ; fileRow = 95 - rowEng
  in-block query point = (localX, localZmesh - 2048)
  vertex -> game: gx = fc*2048 + vx - 0x60000            (wrap into [-131072,131071])
                  gz = 0x48000 - ((95-fr)*2048 + vz + 2048)  (wrap into [-98304,98303])

Two walkability rules (both correct, different uses — see NAV_SIM_FINDINGS.md):
  * engine step VALIDATOR: poly byte15 bit7 (what 0x53E7A0 checks for foot, vehicle 0x80)
  * ROUTING connectivity:  terrain byte13 not in {32,33,34} (ocean). Terrain 29 "mountain"
    (54,297 polys) is walkable ground gated only by the |dH| >= 200 step rule; classifying
    by bit7 alone fragments the mesh (the Tears Point bug).

query() returns the first containing WALKABLE (bit7) poly's height, falling back to the
first containing poly (e.g. ocean) — validated 205/205 vs live [0x203FE30] traces,
mean err 0.59u / max 5.2u (float vs the engine's sar-8 fixed point).
"""

import struct
import heapq
from collections import deque

WMX_SIZE = 30781440          # 835 * 0x9000
SEG_SIZE = 0x9000
GROUND_SEGS = 768            # 32 x 24 ground segments; the rest are texture/aux
BLOCK_COLS = 128
BLOCK_ROWS = 96
BLOCK_UNITS = 2048
STEP_GATE = 200              # 0xC8: reject move iff |candH - curH| >= 200
OCEAN_TERRAIN = frozenset((32, 33, 34))   # terrain byte13 ocean types (non-walkable)
SQRT2 = 1.4142135623730951

GX_MIN, GX_MAX = -131072, 131071
GZ_MIN, GZ_MAX = -98304, 98303


def wrap_gx(gx):
    return (gx + 0x20000) % 0x40000 - 0x20000

def wrap_gz(gz):
    return (gz + 0x18000) % 0x30000 - 0x18000


class WMX:
    """Faithful oracle over the raw wmx.obj walkmesh."""

    def __init__(self, path):
        with open(path, "rb") as f:
            self.data = f.read()
        if len(self.data) != WMX_SIZE:
            raise ValueError("wmx.obj must be %d bytes, got %d" % (WMX_SIZE, len(self.data)))
        self._blocks = {}    # (fileCol, fileRow) -> (polys, verts)

    # ------------------------------------------------------------------ blocks

    def _block(self, file_col, file_row):
        """Port of 0x553E00 applied to FILE row/col (the 2.1 mirror is applied by
        the caller: fileRow = 95 - rowEng). Returns (polys, verts):
          polys: list of (i0, i1, i2, byte13, byte14, byte15)
          verts: flat list of int16, vertex i -> [4i]=X, [4i+1]=H, [4i+2]=Z, [4i+3]=pad
        """
        key = (file_col, file_row)
        blk = self._blocks.get(key)
        if blk is not None:
            return blk
        seg = (file_row >> 2) * 32 + (file_col >> 2)
        sub = (file_row & 3) * 4 + (file_col & 3)
        base = seg * SEG_SIZE
        off = struct.unpack_from("<I", self.data, base + 4 + sub * 4)[0]
        p = base + off
        d = self.data
        poly_count = d[p]
        vert_count = d[p + 1]
        pp = p + 4
        polys = []
        ap = polys.append
        for i in range(poly_count):
            q = pp + i * 16
            ap((d[q], d[q + 1], d[q + 2], d[q + 13], d[q + 14], d[q + 15]))
        vp = pp + poly_count * 16
        verts = list(struct.unpack_from("<%dh" % (vert_count * 4), d, vp))
        blk = (polys, verts)
        self._blocks[key] = blk
        return blk

    # ------------------------------------------------------------------ locator

    def locate(self, gx, gz):
        """Port of 0x53DC70: game coords -> (blockIndex, blockCol, rowEng, localX, localZmesh)."""
        mx = (int(gx) + 0x60000) % 0x40000
        block_col = mx // BLOCK_UNITS
        local_x = mx % BLOCK_UNITS
        mz = (0x48000 - int(gz)) % 0x30000
        row_eng = mz // BLOCK_UNITS
        local_zmesh = mz % BLOCK_UNITS
        return row_eng * 128 + block_col, block_col, row_eng, local_x, local_zmesh

    # ------------------------------------------------------------------ query

    @staticmethod
    def _interp(qx, qz, x0, h0, z0, x1, h1, z1, x2, h2, z2):
        """0x402620 height: planar (barycentric) interpolation; degenerate -> average."""
        den = (z1 - z2) * (x0 - x2) + (x2 - x1) * (z0 - z2)
        if den == 0:
            return (h0 + h1 + h2) / 3.0
        w0 = ((z1 - z2) * (qx - x2) + (x2 - x1) * (qz - z2)) / den
        w1 = ((z2 - z0) * (qx - x2) + (x0 - x2) * (qz - z2)) / den
        return w0 * h0 + w1 * h1 + (1.0 - w0 - w1) * h2

    def query(self, gx, gz, prefer="bit7"):
        """Port of 0x53EB80 + 0x402620.

        Returns (height, walkable_bit7, terrain_byte13, blockIndex, polyIndex).
        height is None (NO_GROUND) if no triangle contains the point.

        prefer='bit7'    : first containing FOOT-WALKABLE (byte15 bit7) poly, fallback
                           first containing — the engine-faithful standing surface.
        prefer='terrain' : first containing terrain-walkable (byte13 not ocean) poly,
                           fallback first containing — for routing connectivity.
        prefer='first'   : plain FIRST-CONTAINING in stored order — what the engine
                           step validator itself reads (BAT201 fitted model).
        """
        block_index, bc, row_eng, lx, lz = self.locate(gx, gz)
        polys, verts = self._block(bc, 95 - row_eng)
        qx = lx
        qz = lz - 2048
        first = -1
        for pi, (i0, i1, i2, b13, b14, b15) in enumerate(polys):
            j0 = i0 * 4; j1 = i1 * 4; j2 = i2 * 4
            x0 = verts[j0]; z0 = verts[j0 + 2]
            x1 = verts[j1]; z1 = verts[j1 + 2]
            x2 = verts[j2]; z2 = verts[j2 + 2]
            c0 = (x1 - x0) * (qz - z0) - (z1 - z0) * (qx - x0)
            c1 = (x2 - x1) * (qz - z1) - (z2 - z1) * (qx - x1)
            c2 = (x0 - x2) * (qz - z2) - (z0 - z2) * (qx - x2)
            if (c0 >= 0 and c1 >= 0 and c2 >= 0) or (c0 <= 0 and c1 <= 0 and c2 <= 0):
                if first < 0:
                    first = pi
                if prefer == "first":
                    ok = True          # FIRST-CONTAINING, stored order — the
                                       # engine validator's own selection
                                       # (BAT201 fitted model, 0x53E7A0)
                elif prefer == "terrain":
                    ok = b13 not in OCEAN_TERRAIN
                else:
                    ok = bool(b15 & 0x80)
                if ok:
                    h = self._interp(qx, qz, x0, verts[j0 + 1], z0,
                                     x1, verts[j1 + 1], z1, x2, verts[j2 + 1], z2)
                    return h, bool(b15 & 0x80), b13, block_index, pi
        if first >= 0:
            i0, i1, i2, b13, b14, b15 = polys[first]
            j0 = i0 * 4; j1 = i1 * 4; j2 = i2 * 4
            h = self._interp(qx, qz,
                             verts[j0], verts[j0 + 1], verts[j0 + 2],
                             verts[j1], verts[j1 + 1], verts[j1 + 2],
                             verts[j2], verts[j2 + 1], verts[j2 + 2])
            return h, bool(b15 & 0x80), b13, block_index, first
        return None, False, None, block_index, -1

    def ground_height(self, gx, gz, prefer="bit7"):
        """The oracle: interpolated standing height, or None (NO_GROUND)."""
        return self.query(gx, gz, prefer)[0]

    # ------------------------------------------------------------------ step gate

    def step_ok(self, frm, to):
        """Engine step validator (0x53E7A0), verbatim rule for foot (vehicle 0x80):
        the destination's poly must be foot-walkable (byte15 bit7) AND
        |candH - curH| < 200. frm/to are (gx, gz) tuples."""
        cur_h = self.ground_height(frm[0], frm[1])
        if cur_h is None:
            return False
        cand_h, walk7, _t, _b, _p = self.query(to[0], to[1])
        if cand_h is None or not walk7:
            return False
        return abs(cand_h - cur_h) < STEP_GATE

    def terrain_walkable(self, gx, gz):
        """Routing rule (NAV_SIM_FINDINGS): terrain byte13 not in {32,33,34}."""
        h, _w7, t, _b, _p = self.query(gx, gz, prefer="terrain")
        return h is not None and t not in OCEAN_TERRAIN

    # ------------------------------------------------------------------ stats

    def mesh_stats(self):
        """Counts over the 768 ground segments (12,288 blocks).
        Expected: total=473,193; bit7-walkable=101,703; terrain-walkable ~148k-158k."""
        total = walk7 = terrain_walk = 0
        terrain_hist = {}
        for fr in range(BLOCK_ROWS):
            for fc in range(BLOCK_COLS):
                polys, _ = self._block(fc, fr)
                total += len(polys)
                for (_a, _b, _c, b13, _b14, b15) in polys:
                    if b15 & 0x80:
                        walk7 += 1
                    if b13 not in OCEAN_TERRAIN:
                        terrain_walk += 1
                    terrain_hist[b13] = terrain_hist.get(b13, 0) + 1
        return {"total_polys": total, "foot_walkable_bit7": walk7,
                "terrain_walkable": terrain_walk, "terrain_hist": terrain_hist}

    # ------------------------------------------------------------------ router

    def _cell(self, cache, ix, iz, step):
        """Cached per-cell (height, enterable) using the ROUTING walkability rule."""
        key = (ix, iz)
        v = cache.get(key)
        if v is None:
            gx = wrap_gx(ix * step)
            gz = wrap_gz(iz * step)
            h, _w7, t, _b, _p = self.query(gx, gz, prefer="terrain")
            v = (h, h is not None and t not in OCEAN_TERRAIN)
            cache[key] = v
        return v

    def flood(self, start, step=128, targets=None, max_cells=None):
        """8-direction reachability flood from `start` (gx, gz) at `step` units.
        A neighbour is enterable iff terrain-walkable and |dH| < 200.

        targets: optional {name: (gx, gz)}; the flood stops early once every target
        has been reached (a target counts as reached within 1 cell of its snap).
        Returns (reached_cells_set, found_dict name->cells_seen_when_found)."""
        cache = {}
        s = (round(start[0] / step), round(start[1] / step))
        h0, ok = self._cell(cache, s[0], s[1], step)
        if not ok:
            return set(), {}
        tcells = {}
        if targets:
            for name, (tx, tz) in targets.items():
                cx, cz = round(tx / step), round(tz / step)
                for dx in (-1, 0, 1):
                    for dz in (-1, 0, 1):
                        tcells.setdefault((cx + dx, cz + dz), name)
        n_targets = len(set(tcells.values()))
        found = {}
        seen = {s}
        dq = deque((s,))
        while dq:
            ix, iz = dq.popleft()
            h, _ = cache[(ix, iz)]
            for dx in (-1, 0, 1):
                for dz in (-1, 0, 1):
                    if dx == 0 and dz == 0:
                        continue
                    n = (ix + dx, iz + dz)
                    if n in seen:
                        continue
                    nh, nok = self._cell(cache, n[0], n[1], step)
                    if not nok or abs(nh - h) >= STEP_GATE:
                        continue
                    seen.add(n)
                    dq.append(n)
                    name = tcells.get(n)
                    if name is not None and name not in found:
                        found[name] = len(seen)
                        if len(found) == n_targets:
                            return seen, found
            if max_cells is not None and len(seen) >= max_cells:
                break
        return seen, found

    def route(self, start, goal, step=128, margin=8192):
        """A* over the 8-direction step grid, limited to the start/goal bounding box
        plus `margin` game units (like the mod's GRID planner). Returns a list of
        (gx, gz) waypoints (start..goal cells) or None if unreachable in the box."""
        cache = {}
        s = (round(start[0] / step), round(start[1] / step))
        g = (round(goal[0] / step), round(goal[1] / step))
        _h, ok = self._cell(cache, s[0], s[1], step)
        if not ok:
            return None
        _h, ok = self._cell(cache, g[0], g[1], step)
        if not ok:
            return None
        m = max(1, margin // step)
        lo_x, hi_x = min(s[0], g[0]) - m, max(s[0], g[0]) + m
        lo_z, hi_z = min(s[1], g[1]) - m, max(s[1], g[1]) + m

        def heur(c):
            dx = abs(c[0] - g[0]); dz = abs(c[1] - g[1])
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
                return [(c[0] * step, c[1] * step) for c in path]
            base = gscore[cur]
            h, _ = cache[(cur[0], cur[1])]
            for dx in (-1, 0, 1):
                for dz in (-1, 0, 1):
                    if dx == 0 and dz == 0:
                        continue
                    n = (cur[0] + dx, cur[1] + dz)
                    if not (lo_x <= n[0] <= hi_x and lo_z <= n[1] <= hi_z):
                        continue
                    nh, nok = self._cell(cache, n[0], n[1], step)
                    if not nok or abs(nh - h) >= STEP_GATE:
                        continue
                    ng = base + (SQRT2 if dx and dz else 1.0)
                    if ng < gscore.get(n, 1e30):
                        gscore[n] = ng
                        came[n] = cur
                        tie += 1
                        heapq.heappush(open_heap, (ng + heur(n), tie, n))
        return None


# ---------------------------------------------------------------------- CLI

if __name__ == "__main__":
    import sys
    path = sys.argv[1] if len(sys.argv) > 1 else "wmx.obj"
    wm = WMX(path)
    st = wm.mesh_stats()
    print("total polys        :", st["total_polys"])
    print("foot-walkable bit7 :", st["foot_walkable_bit7"])
    print("terrain-walkable   :", st["terrain_walkable"])
    landmarks = {
        "Balamb Garden":      (24576, -29406),
        "Balamb Town":        (13249, -26779),
        "Dollet":             (-15639, -39437),
        "Timber":             (-22564, -4867),
        "Galbadia Garden":    (-37475, -26232),
        "Galbadia Station":   (-38394, -24803),
        "Fire Cavern":        (30326, -29221),
        "Lunar Gate":         (88021, 7865),
        "Sorceress Memorial": (81521, 11865),
        "Tears' Point":       (83021, 31865),
    }
    for name, (gx, gz) in landmarks.items():
        h, w7, t, bi, pi = wm.query(gx, gz)
        hs = "NO_GROUND" if h is None else "%.1f" % h
        print("%-20s (%7d,%7d)  h=%-9s bit7=%d terrain=%s block=%d poly=%d"
              % (name, gx, gz, hs, w7, t, bi, pi))
