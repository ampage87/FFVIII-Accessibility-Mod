"""Emit src/menu_card_data.inl from FF8_EN.exe.

The card table and the card names are GENERATED, never typed. A 110-entry table
transcribed by hand is a hundred and ten chances to be quietly wrong about a
number a blind player cannot check against the screen.

  values: 0x00C74D00, stride 8 -> [0]=Top [1]=Bottom [2]=Left [3]=Right
                                  [4]=element bitmask [5]=AI rating
  names:  0x00C75074 -> u16 count, u16 offsets, FF8 text (glyph + 0x20)

Both proven in docs/CARD_MENU_FINDINGS.md, the values order twice over from the
two independent renderers' number-position tables (0x00C75B10, 0x00B88A68).
"""
import pefile, struct, sys

EXE = 'FF8_EN.exe'
VALUES, NAMES, N = 0x00C74D00, 0x00C75074, 110

ROWS = [
 [" ","0","1","2","3","4","5","6","7","8","9","%","/",":","!","?"],
 ["...","+","-","=","*","&","\"","\"","(",")",".",".",",","~","\"","\""],
 ["'","#","$","'","_","A","B","C","D","E","F","G","H","I","J","K"],
 ["L","M","N","O","P","Q","R","S","T","U","V","W","X","Y","Z","a"],
 ["b","c","d","e","f","g","h","i","j","k","l","m","n","o","p","q"],
 ["r","s","t","u","v","w","x","y","z","A","A","A","A","C","E","E"],
 ["E","E","I","I","I","I","N","O","O","O","O","U","U","U","U","O"],
 ["a","a","a","a","c","e","e","e","e","i","i","i","i","n","o","o"],
 ["o","o","u","u","u","u","o","s","!","?","<",">",".",".",".","."],
]
TBL = {0x20 + r*16 + c: ch for r, row in enumerate(ROWS) for c, ch in enumerate(row)}

pe = pefile.PE(EXE, fast_load=True)
ib = pe.OPTIONAL_HEADER.ImageBase
secs = [(s.VirtualAddress+ib, s.VirtualAddress+ib+max(s.Misc_VirtualSize, s.SizeOfRawData),
         s.PointerToRawData) for s in pe.sections]
data = open(EXE, 'rb').read()
def rd(va, n):
    for a, b, pr in secs:
        if a <= va < b:
            return data[pr+(va-a):pr+(va-a)+n]
    raise SystemExit("VA %08X not mapped" % va)

def text(va, cap=32):
    raw = rd(va, cap)
    out = []
    for b in raw:
        if b == 0:
            break
        out.append(TBL.get(b, '?'))
    return ''.join(out).strip()

vals = rd(VALUES, N*8)
nblk = rd(NAMES, 4 + N*2)
count = struct.unpack_from('<H', nblk, 0)[0]
if count != N:
    raise SystemExit("name table says %d entries, expected %d" % (count, N))
names = []
for i in range(N):
    off = struct.unpack_from('<H', nblk, 2 + i*2)[0]
    names.append(text(NAMES + off))

ELEM = ["Fire", "Ice", "Thunder", "Earth", "Poison", "Wind", "Water", "Holy"]

# The bottom info line the album draws under the card (0x004EFE30). For the 77
# common cards it is labelled "MONSTER" and names the monster that carries the
# card, or the pair that play it at levels 6-7 -- i.e. WHERE TO GET IT, which is
# the single most useful thing on the screen for a collector and the one thing a
# screen reader had no way to reach. Static table, so it is generated here.
#
# For the 33 rare cards the same line is labelled "AREA" and is DYNAMIC (it
# names whoever currently holds the card), so it is resolved at runtime instead
# -- see CardAreaLine in src/menu_tts_card.inl.
SRC_PTR = 0x00B96500
src_base = struct.unpack('<I', rd(SRC_PTR, 4))[0]
src_n = struct.unpack('<H', rd(src_base, 2))[0]
sources = []
for i in range(N):
    if i >= 77:
        sources.append("")
        continue
    if i >= src_n:
        sources.append("")
        continue
    off = struct.unpack_from('<H', rd(src_base + 2 + i*4, 4), 0)[0]
    sources.append(text(src_base + off, 64))
missing = [i for i in range(77) if not sources[i]]
if missing:
    raise SystemExit("no MONSTER line for common cards %s" % missing[:8])

# --- sanity gates: refuse to emit a table that fails its own cross-checks -----
#
# Nine cards whose published Top/Right/Bottom/Left every Triple Triad player
# knows, chosen so that most of them have four DISTINCT powers -- a table read
# with two positions transposed would still satisfy the arithmetic check below
# (it squares and sums all four), so the only way to catch a transposition is a
# card whose four numbers differ. All nine agree, which is what makes the byte
# order a fact rather than the subagent's reading of two icon-position tables.
KNOWN = {   # id: (top, right, bottom, left) -- as SPOKEN
    0:   (1,  4, 1,  5),   # Geezard
    1:   (5,  1, 1,  3),   # Funguar
    47:  (3, 10, 2,  1),   # PuPu
    84:  (9,  6, 2,  8),   # Ifrit
    83:  (6,  7, 4,  9),   # Shiva
    89:  (5, 10, 8,  3),   # Diablos
    94:  (9, 10, 4,  2),   # Alexander
    96:  (10, 8, 2,  6),   # Bahamut
    109: (10, 4, 6,  9),   # Squall
}
bad = 0
for cid, want in KNOWN.items():
    t, b_, l, r = vals[cid*8:cid*8+4]
    got = (t, r, b_, l)
    if got != want:
        print("MISMATCH: card %d (%s) reads T/R/B/L %s, published %s"
              % (cid, names[cid], got, want), file=sys.stderr)
        bad += 1

for i in range(N):
    t, b, l, r, el, ai = vals[i*8:i*8+6]
    if not (1 <= t <= 10 and 1 <= b <= 10 and 1 <= l <= 10 and 1 <= r <= 10):
        print("BAD values for card %d (%s): %d %d %d %d" % (i, names[i], t, b, l, r), file=sys.stderr)
        bad += 1
    if el and bin(el).count('1') != 1:
        print("BAD element for card %d (%s): 0x%02X" % (i, names[i], el), file=sys.stderr)
        bad += 1
    # the AI rating is (T^2+B^2+L^2+R^2)/2 for every card -- an independent check
    # that the four bytes really are the four powers and are in these positions.
    if ai != (t*t + b*b + l*l + r*r)//2:
        print("BAD rating for card %d (%s): %d vs %d" % (i, names[i], ai,
              (t*t+b*b+l*l+r*r)//2), file=sys.stderr)
        bad += 1
    if not names[i]:
        print("BAD empty name for card %d" % i, file=sys.stderr); bad += 1
if bad:
    raise SystemExit("%d problems -- refusing to emit" % bad)

out = []
w = out.append
w("// menu_card_data.inl -- GENERATED by offline/gen_cards.py. DO NOT EDIT.")
w("//")
w("// The 110 Triple Triad cards, lifted from FF8_EN.exe rather than typed:")
w("//   values 0x%08X, stride 8, [0]=Top [1]=Bottom [2]=Left [3]=Right [4]=element" % VALUES)
w("//   names  0x%08X, u16 count + u16 offsets + FF8 glyph text" % NAMES)
w("//")
w("// The generator refuses to emit unless every card passes three checks: all four")
w("// powers in 1..10, at most one element bit, and byte +5 (the game's own AI")
w("// rating) equal to (T^2+B^2+L^2+R^2)/2 -- which is an INDEPENDENT confirmation")
w("// that those four bytes really are the four powers, in those four positions.")
w("//")
w("// Spoken order is Top, Right, Bottom, Left (clockwise from the top), which is")
w("// Aaron's request and matches how the card is read on screen.")
w("")
w("#ifndef MENU_CARD_DATA_INCLUDED")
w("#define MENU_CARD_DATA_INCLUDED")
w("")
w("static const int CARD_COUNT = %d;" % N)
w("")
w("struct CardDef { const char* name; unsigned char top, right, bottom, left, elem; };")
w("static const CardDef CARD_DEFS[CARD_COUNT] = {")
for i in range(N):
    t, b, l, r, el = vals[i*8:i*8+5]
    e = ELEM[el.bit_length()-1] if el else "none"
    w('    { "%s", %2d, %2d, %2d, %2d, 0x%02X },   // %3d  %s' %
      (names[i].replace('"', '\\"'), t, r, b, l, el, i, e))
w("};")
w("")
w("// The \"MONSTER\" line under the card: which monster carries it, or the pair")
w("// that play it at levels 6-7. Empty for the 33 rare cards, whose line is the")
w("// dynamic \"AREA\" instead. From [0x%08X] = 0x%08X." % (SRC_PTR, src_base))
w("static const char* const CARD_SOURCES[CARD_COUNT] = {")
for i in range(N):
    w('    "%s",%s   // %3d %s' % (sources[i].replace('"', '\\"'),
                                   ' ' * max(0, 26 - len(sources[i])), i, names[i]))
w("};")
w("")
w("static const char* const CARD_ELEMENTS[8] = {")
w("    " + ", ".join('"%s"' % e for e in ELEM))
w("};")
w("")
w("#endif // MENU_CARD_DATA_INCLUDED")
open('src/menu_card_data.inl', 'w').write("\n".join(out) + "\n")
print("wrote src/menu_card_data.inl: %d cards, %d with an element" %
      (N, sum(1 for i in range(N) if vals[i*8+4])))
