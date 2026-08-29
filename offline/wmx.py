"""Offline replica of the mod's wmx.obj -> fine-grid rasterisation.

Mirrors world_map_segments.inl exactly: same segment/block walk, same vertex
frame (vwy = oy - lvy), same steepness = vertex-elevation spread, same class
rule. The one thing it ADDS is poly[0x0F] bit7, the engine's on-foot walkable
flag, so the two can be compared cell by cell.
"""
import struct, numpy as np

SEG_COLS, SEG_ROWS = 32, 24
SEGMENT_SIZE   = 36864
BLOCKS_PER_SEG = 16
BLOCK_HDR      = 4
POLY_SIZE      = 16
TERRAIN_OFF    = 0x0D
WALK_OFF       = 0x0F          # bit7 = engine on-foot walkable
MTN_STEEP_BLOCK = 256

SEG_LAND, SEG_FOREST, SEG_OCEAN, SEG_MOUNTAIN = 0, 1, 2, 3

FINE_COLS, FINE_ROWS = 256, 192
FINE_CELL = 1024                # 8192 / 8
# world X of fine col c: the mod's grid is indexed straight off segment space,
# world X = col*8192 + ... , i.e. fine col c covers X in [c*1024, (c+1)*1024).
# Game coords are that minus the half-world offset.
WORLD_OFF_X = 131072            # game X = raw X - 131072
WORLD_OFF_Y = 98304

def game_to_fine(gx, gy):
    return int((gx + WORLD_OFF_X) // FINE_CELL), int((gy + WORLD_OFF_Y) // FINE_CELL)
def fine_to_game(c, r):
    return c * FINE_CELL - WORLD_OFF_X + FINE_CELL // 2, r * FINE_CELL - WORLD_OFF_Y + FINE_CELL // 2


class Grid:
    __slots__ = ('cls', 'steep', 'elev', 'walk', 'nonwalk', 'road')
    def __init__(self):
        self.cls     = np.full((FINE_ROWS, FINE_COLS), SEG_OCEAN, np.uint8)
        self.steep   = np.zeros((FINE_ROWS, FINE_COLS), np.uint16)
        self.elev    = np.zeros((FINE_ROWS, FINE_COLS), np.int16)
        self.walk    = np.zeros((FINE_ROWS, FINE_COLS), np.uint32)  # bit7-set polys covering the cell
        self.nonwalk = np.zeros((FINE_ROWS, FINE_COLS), np.uint32)  # bit7-clear, non-ocean
        self.road    = np.zeros((FINE_ROWS, FINE_COLS), np.uint8)


def _raster(g, ax, ay, bx, by, cx, cy, cls, steep, elev, bit7):
    """RasterizeTriFine: every fine cell whose CENTRE lies in the triangle."""
    minx, maxx = min(ax, bx, cx), max(ax, bx, cx)
    miny, maxy = min(ay, by, cy), max(ay, by, cy)
    c0 = max(0, int(minx // FINE_CELL)); c1 = min(FINE_COLS - 1, int(maxx // FINE_CELL))
    r0 = max(0, int(miny // FINE_CELL)); r1 = min(FINE_ROWS - 1, int(maxy // FINE_CELL))
    if c1 < c0 or r1 < r0: return
    for r in range(r0, r1 + 1):
        py = r * FINE_CELL + FINE_CELL // 2
        for c in range(c0, c1 + 1):
            px = c * FINE_CELL + FINE_CELL // 2
            d1 = (px - bx) * (ay - by) - (ax - bx) * (py - by)
            d2 = (px - cx) * (by - cy) - (bx - cx) * (py - cy)
            d3 = (px - ax) * (cy - ay) - (cx - ax) * (py - ay)
            neg = (d1 < 0) or (d2 < 0) or (d3 < 0)
            pos = (d1 > 0) or (d2 > 0) or (d3 > 0)
            if neg and pos: continue
            if g.cls[r][c] != SEG_OCEAN:      # first non-ocean polygon wins
                if bit7: g.walk[r][c]    += 1
                else:    g.nonwalk[r][c] += 1
                continue
            g.cls[r][c]   = cls
            g.steep[r][c] = steep
            g.elev[r][c]  = elev
            if bit7: g.walk[r][c]    += 1
            else:    g.nonwalk[r][c] += 1


def _raster_road(g, ax, ay, bx, by, cx, cy):
    """RasterizeTriRoad: the three vertex cells and the centroid cell
    unconditionally, plus every cell whose centre is inside -- so a road ribbon
    narrower than a cell still registers."""
    for vx, vy in ((ax,ay),(bx,by),(cx,cy),((ax+bx+cx)//3,(ay+by+cy)//3)):
        gx, gy = vx // FINE_CELL, vy // FINE_CELL
        if 0 <= gx < FINE_COLS and 0 <= gy < FINE_ROWS: g.road[gy][gx] = 1
    c0 = max(0, min(ax,bx,cx)//FINE_CELL); c1 = min(FINE_COLS-1, max(ax,bx,cx)//FINE_CELL)
    r0 = max(0, min(ay,by,cy)//FINE_CELL); r1 = min(FINE_ROWS-1, max(ay,by,cy)//FINE_CELL)
    for r in range(r0, r1+1):
        py = r*FINE_CELL + FINE_CELL//2
        for c in range(c0, c1+1):
            px = c*FINE_CELL + FINE_CELL//2
            d1 = (px-bx)*(ay-by) - (ax-bx)*(py-by)
            d2 = (px-cx)*(by-cy) - (bx-cx)*(py-cy)
            d3 = (px-ax)*(cy-ay) - (cx-ax)*(py-ay)
            if not (((d1<0)or(d2<0)or(d3<0)) and ((d1>0)or(d2>0)or(d3>0))):
                g.road[r][c] = 1


def build(path='/root/work/wmx.obj'):
    data = open(path, 'rb').read()
    g = Grid()
    nseg = len(data) // SEGMENT_SIZE
    for seg in range(min(nseg, SEG_COLS * SEG_ROWS)):
        row, col = seg // SEG_COLS, seg % SEG_COLS
        base = seg * SEGMENT_SIZE
        for b in range(BLOCKS_PER_SEG):
            off = struct.unpack_from('<I', data, base + 4 + b * 4)[0]
            if off == 0 or off + BLOCK_HDR > SEGMENT_SIZE: continue
            bb = base + off
            polyCount, vertCount = data[bb], data[bb + 1]
            polyEnd = off + BLOCK_HDR + polyCount * POLY_SIZE
            if polyEnd > SEGMENT_SIZE: continue
            vertBase = bb + BLOCK_HDR + polyCount * POLY_SIZE
            if polyEnd + vertCount * 8 > SEGMENT_SIZE: continue
            bRow, bCol = b // 4, b % 4
            ox = col * 8192 + bCol * 2048
            oy = row * 8192 + bRow * 2048
            vwx = [0] * vertCount; vwy = [0] * vertCount; vwz = [0] * vertCount
            for v in range(vertCount):
                lvx, lvz, lvy = struct.unpack_from('<hhh', data, vertBase + v * 8)
                vwx[v] = ox + lvx
                vwz[v] = lvz
                vwy[v] = oy - lvy
            for p in range(polyCount):
                pb = bb + BLOCK_HDR + p * POLY_SIZE
                terrain = data[pb + TERRAIN_OFF]
                bit7    = (data[pb + WALK_OFF] & 0x80) != 0
                if 32 <= terrain <= 34: continue          # ocean
                cls = SEG_MOUNTAIN if terrain == 29 else (SEG_FOREST if terrain <= 5 else SEG_LAND)
                i0, i1, i2 = data[pb], data[pb + 1], data[pb + 2]
                if i0 >= vertCount or i1 >= vertCount or i2 >= vertCount: continue
                e = (vwz[i0], vwz[i1], vwz[i2])
                steep = max(e) - min(e)
                elev  = sum(e) // 3
                _raster(g, vwx[i0], vwy[i0], vwx[i1], vwy[i1], vwx[i2], vwy[i2],
                        cls, steep, elev, bit7)
                if terrain in (27, 28, 12):
                    _raster_road(g, vwx[i0], vwy[i0], vwx[i1], vwy[i1], vwx[i2], vwy[i2])
    return g


def walkable_current(g):
    """The mod's SHIPPED rule: class + steepness only."""
    return ~((g.cls == SEG_OCEAN) | ((g.cls == SEG_MOUNTAIN) & (g.steep > MTN_STEEP_BLOCK)))


# ---------------------------------------------------------------------------
# The three post-rasterisation corrections the mod applies, in the mod's order.
# ---------------------------------------------------------------------------
DOLLET_COAST = (-24576, -17408, -37888, -27648)   # x0, x1, y0, y1 (game coords)

def _fine_col(gx): return (gx + WORLD_OFF_X) // FINE_CELL
def _fine_row(gy): return (gy + WORLD_OFF_Y) // FINE_CELL

def apply_corrections(g):
    """Dollet false-coast patch, then the road-walkable override -- the order
    world_map_segments.inl uses (patch first, override second, so the road wins)."""
    # --- Dollet false-coast no-walk patch (v0.18.3.81) ---
    x0, x1, y0, y1 = DOLLET_COAST
    c0, c1 = _fine_col(x0), _fine_col(x1)
    r0, r1 = _fine_row(y0), _fine_row(y1)
    if c0 > c1: c0, c1 = c1, c0
    if r0 > r1: r0, r1 = r1, r0
    patched = 0
    for r in range(max(0, r0), min(FINE_ROWS - 1, r1) + 1):
        for c in range(max(0, c0), min(FINE_COLS - 1, c1) + 1):
            g.cls[r][c] = SEG_MOUNTAIN
            g.steep[r][c] = 0xFFFF
            patched += 1
    # --- the two hardcoded Dollet bridge road cells (v0.18.3.135) ---
    g.road[56][112] = 1
    g.road[57][112] = 1
    # --- road-walkable override (v0.18.3.85) ---
    forced = 0; recovered = 0
    for r in range(FINE_ROWS):
        for c in range(FINE_COLS):
            if not g.road[r][c]: continue
            was_blocked = (g.cls[r][c] == SEG_OCEAN) or \
                          (g.cls[r][c] == SEG_MOUNTAIN and g.steep[r][c] > MTN_STEEP_BLOCK)
            g.cls[r][c] = SEG_LAND
            g.steep[r][c] = 0
            forced += 1
            if was_blocked: recovered += 1
    return patched, forced, recovered


def clearance(walk):
    """The mod's clearance field: CHEBYSHEV (8-neighbour) distance in cells from
    every walkable cell to the nearest blocked one, multi-source BFS seeded with
    the blocked cells at 0, capped at 254."""
    import collections
    clr = np.full((FINE_ROWS, FINE_COLS), 255, np.int32)
    q = collections.deque()
    for r in range(FINE_ROWS):
        for c in range(FINE_COLS):
            if not walk[r][c]:
                clr[r][c] = 0
                q.append((r, c))
    while q:
        r, c = q.popleft()
        if clr[r][c] >= 254: continue
        nd = clr[r][c] + 1
        for dr in (-1, 0, 1):
            for dc in (-1, 0, 1):
                if dr == 0 and dc == 0: continue
                nr, nc = r + dr, c + dc
                if not (0 <= nr < FINE_ROWS and 0 <= nc < FINE_COLS): continue
                if clr[nr][nc] > nd:
                    clr[nr][nc] = nd
                    q.append((nr, nc))
    return clr
