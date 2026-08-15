import sys, math
sys.path.insert(0, '.')
from gsim2 import GWp, HP, g2cp, cp2g, KP, RP, GATE

# Replicate the SHIPPED C++ GdProbe exactly: it reads the 256u planner grid
# (s_gdCls / s_gdH) and applies the 200-unit step gate between the hull's
# CURRENT cell and the cell one whole probe-length away.
def gd_blocked_cpp(x, y, hd, dist):
    a = hd / 4096.0 * 2 * math.pi
    nx = x + math.sin(a) * dist
    ny = y - math.cos(a) * dist
    r0, c0 = g2cp(x, y)
    r1, c1 = g2cp(nx, ny)
    if not GWp[r1, c1 % KP]:
        return True, "not traversable"
    dh = abs(int(HP[r1, c1 % KP]) - int(HP[r0, c0 % KP]))
    return (dh >= GATE), "dh=%d" % dh

# the exact (position, heading) pairs the .56 BAT logged as "bow blocked"
cases = [(2902,36863,661),(2913,36840,461),(2974,36691,666),(3015,36635,726),
         (3092,36521,576),(3181,36387,721),(3274,36255,711),(3370,36125,1006),
         (3450,36022,546),(3547,35891,681),(3654,35744,862),(3731,35629,877),
         (3805,35521,519),(3853,35455,564),(3897,35386,452)]
print("%-22s %-8s %-28s %-28s" % ("hull", "hd", "near probe (320u)", "far probe (640u)"))
nblk = 0
for x, y, hd in cases:
    b1, w1 = gd_blocked_cpp(x, y, hd, 320)
    b2, w2 = gd_blocked_cpp(x, y, hd, 640)
    if b1 or b2: nblk += 1
    print("(%6d,%6d) %-8d %-8s %-19s %-8s %-19s" %
          (x, y, hd, "BLOCKED" if b1 else "clear", w1, "BLOCKED" if b2 else "clear", w2))
print("\n%d of %d reproduce the logged 'bow blocked'" % (nblk, len(cases)))

# is the underlying cell actually traversable? and what do the heights do?
print("\nheights on the 256 grid around the wedge (each cell 256u):")
r0, c0 = g2cp(3400, 36100)
for r in range(r0 - 5, r0 + 6):
    row = []
    for c in range(c0 - 8, c0 + 9):
        row.append("%6d%s" % (HP[r, c % KP], "G" if GWp[r, c % KP] else "."))
    print("  " + "".join(row))
