"""FF8 archive LZSS + fi/fl index reader (used by extract_fields.py)."""
import struct

def lzs(data, expected=0):
    out = bytearray(); ring = bytearray(4096); r = 0xFEE
    i = 0; n = len(data)
    while i < n:
        flags = data[i]; i += 1
        for b in range(8):
            if i >= n: break
            if flags & (1 << b):
                c = data[i]; i += 1
                out.append(c); ring[r] = c; r = (r + 1) & 0xFFF
            else:
                if i + 1 >= n: break
                b1 = data[i]; b2 = data[i+1]; i += 2
                off = b1 | ((b2 & 0xF0) << 4); ln = (b2 & 0x0F) + 3
                for k in range(ln):
                    c = ring[(off + k) & 0xFFF]
                    out.append(c); ring[r] = c; r = (r + 1) & 0xFFF
        if expected and len(out) >= expected: break
    return bytes(out[:expected]) if expected else bytes(out)

def load_index(fi_bytes, fl_bytes):
    """.fi is 12 bytes per entry: uncompressed size, offset, compression type."""
    names = [x.strip() for x in fl_bytes.decode('latin1').replace('\r\n', '\n').split('\n') if x.strip()]
    ents = [struct.unpack_from('<III', fi_bytes, i * 12) for i in range(len(fi_bytes) // 12)]
    return names, ents
