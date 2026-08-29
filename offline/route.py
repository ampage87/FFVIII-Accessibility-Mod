"""The mod's GRID planner edge rule, offline, on the parity-proven grid.

Edge (cr,cc)->(nr,nc) is allowed iff both cells are IsFineTraversable for the
vehicle AND, unless the destination is a road cell, |elev[a]-elev[b]| <=
WM_CLIMB_STEP (400). That is world_map_planner.inl's rule verbatim.
"""
import heapq, sys
sys.path.insert(0, '/root/work/wmsim')
import wmx, numpy as np, pickle

WM_CLIMB_STEP = 400
VEH_FOOT, VEH_CHOCOBO, VEH_CAR, VEH_GARDEN, VEH_RAGNAROK = range(5)

_d = pickle.load(open('/root/work/wmsim/grid.pkl', 'rb'))
class G: pass
g = G()
for k, v in _d.items(): setattr(g, k, v)

def traversable(r, c, veh=VEH_FOOT):
    if veh in (VEH_GARDEN, VEH_RAGNAROK): return True
    cl = g.cls[r][c]
    if cl == wmx.SEG_OCEAN: return False
    if cl == wmx.SEG_FOREST and veh == VEH_CAR: return False
    if cl == wmx.SEG_MOUNTAIN and g.steep[r][c] > wmx.MTN_STEEP_BLOCK: return False
    return True

def edge_ok(cr, cc, nr, nc, veh=VEH_FOOT):
    if not traversable(nr, nc, veh): return False
    if g.road[nr][nc]: return True
    return abs(int(g.elev[cr][cc]) - int(g.elev[nr][nc])) <= WM_CLIMB_STEP

N4 = ((-1,0),(1,0),(0,-1),(0,1))
def _wrap(r, c): return r % wmx.FINE_ROWS, c % wmx.FINE_COLS

def plan(gx0, gy0, gx1, gy1, veh=VEH_FOOT):
    """Dijkstra on the fine grid, 4-neighbour with torus wrap, exactly as
    world_map_planner.inl does it. Returns (path, cost) when the target cell is
    reached, or (None, reason). The mod also falls back to the nearest reachable
    cell; nearest_reachable() below reports that separately so a "planned" route
    that stops 20 km short cannot be mistaken for an arrival."""
    c0, r0 = wmx.game_to_fine(gx0, gy0)
    c1, r1 = wmx.game_to_fine(gx1, gy1)
    if not traversable(r0, c0, veh): return None, f"start cell ({c0},{r0}) not traversable"
    if not traversable(r1, c1, veh): return None, f"goal cell ({c1},{r1}) not traversable"
    openq = [(0.0, r0, c0)]
    best = {(r0,c0): 0.0}; came = {}
    while openq:
        cost, r, c = heapq.heappop(openq)
        if (r,c) == (r1,c1):
            path = [(r,c)]
            while (r,c) in came: r,c = came[(r,c)]; path.append((r,c))
            return path[::-1], cost
        if cost > best.get((r,c), 1e18): continue
        for dr, dc in N4:
            nr, nc = _wrap(r+dr, c+dc)
            if not edge_ok(r, c, nr, nc, veh): continue
            ncost = cost + 1.0
            if ncost < best.get((nr,nc), 1e18):
                best[(nr,nc)] = ncost; came[(nr,nc)] = (r,c)
                heapq.heappush(openq, (ncost, nr, nc))
    return None, "no path"

def reachable_set(gx, gy, veh=VEH_FOOT):
    """Flood fill of everything walkable from a point, under the same edge rule."""
    c0, r0 = wmx.game_to_fine(gx, gy)
    if not traversable(r0, c0, veh): return set()
    seen = {(r0,c0)}; stack = [(r0,c0)]
    while stack:
        r, c = stack.pop()
        for dr, dc in N4:
            nr, nc = _wrap(r+dr, c+dc)
            if (nr,nc) in seen: continue
            if not edge_ok(r, c, nr, nc, veh): continue
            seen.add((nr,nc)); stack.append((nr,nc))
    return seen


def nearest_reachable(gx0, gy0, gx1, gy1, veh=VEH_FOOT):
    """How close to the goal the planner can actually get -- the mod's
    bestR/bestC fallback. Returns (cells_short, game_units_short)."""
    c1, r1 = wmx.game_to_fine(gx1, gy1)
    reach = reachable_set(gx0, gy0, veh)
    if not reach: return None, None
    bd = min(abs(r-r1) + abs(c-c1) for r, c in reach)
    return bd, bd * wmx.FINE_CELL
