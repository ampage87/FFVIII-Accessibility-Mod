"""extract_wmx.py — extract the raw walkmesh archive wmx.obj from world.fs.

Usage:
    python3 extract_wmx.py <path/to/world.fs> <output/wmx.obj>

wmx.obj is stored UNCOMPRESSED in world.fs at FI entry 9 (offset 3,040,099,
size 30,781,440 = 835 segments x 0x9000). If the FI table (world.fi, 12-byte
entries: size, offset, compressed flag) is present next to world.fs it is
parsed and verified; otherwise the known offset/size are used directly.
"""

import os
import struct
import sys

WMX_SIZE = 30781440
WMX_OFFSET = 3040099
WMX_FI_INDEX = 9


def find_entry(fi_path):
    """Return (size, offset, comp) for the wmx.obj entry from world.fi."""
    data = open(fi_path, "rb").read()
    entries = [struct.unpack_from("<III", data, i * 12) for i in range(len(data) // 12)]
    # Prefer the known index if it matches; otherwise search by size.
    if len(entries) > WMX_FI_INDEX and entries[WMX_FI_INDEX][0] == WMX_SIZE:
        return entries[WMX_FI_INDEX]
    for e in entries:
        if e[0] == WMX_SIZE:
            return e
    raise SystemExit("no FI entry with size %d found in %s" % (WMX_SIZE, fi_path))


def main():
    if len(sys.argv) != 3:
        raise SystemExit(__doc__)
    fs_path, out_path = sys.argv[1], sys.argv[2]
    size, offset, comp = WMX_SIZE, WMX_OFFSET, 0
    fi_path = os.path.splitext(fs_path)[0] + ".fi"
    if os.path.exists(fi_path):
        size, offset, comp = find_entry(fi_path)
    if comp != 0:
        raise SystemExit("wmx.obj entry is marked compressed (%d); expected uncompressed" % comp)
    if size != WMX_SIZE:
        raise SystemExit("unexpected wmx.obj size %d (expected %d)" % (size, WMX_SIZE))
    with open(fs_path, "rb") as f:
        f.seek(offset)
        data = f.read(size)
    if len(data) != size:
        raise SystemExit("short read: got %d of %d bytes" % (len(data), size))
    with open(out_path, "wb") as f:
        f.write(data)
    print("extracted %d bytes -> %s (offset %d, uncompressed)" % (size, out_path, offset))


if __name__ == "__main__":
    main()
