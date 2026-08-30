"""FF8 field-text decoder.

Derived from the Shumi Village .msd blocks by matching known English words:
`Wasjnrmp` -> `Sculptor` fixes uppercase at 0x45='A' and lowercase at 0x5F='a';
the punctuation below is read off the same messages.  Bytes with no mapping are
rendered as \\xNN so an unknown never silently becomes a plausible letter.
"""
PUNCT = {
    0x20: ' ',  0x2F: '!',  0x30: '?',
    0x3B: '.',  0x3C: ',',  0x3D: '-',
    0x3E: '"',  0x3F: '"',  0x43: "'",
    0x44: '"',  0x2C: '/',  0x2D: ':',
    0x2E: ';',  0x2B: '%',
}
for i in range(10):
    PUNCT[0x21 + i] = chr(ord('0') + i)

def ch(b):
    if 0x45 <= b <= 0x5E: return chr(ord('A') + b - 0x45)
    if 0x5F <= b <= 0x78: return chr(ord('a') + b - 0x5F)
    if b in PUNCT:        return PUNCT[b]
    return None

def decode(raw):
    out = []
    i = 0
    while i < len(raw):
        b = raw[i]
        if b == 0x00: break
        if b == 0x02: out.append('\n'); i += 1; continue
        if b == 0x01: out.append('\n\n'); i += 1; continue
        # 0x03..0x1F are control codes; several take one parameter byte.
        if b in (0x04, 0x05, 0x06, 0x07, 0x0E, 0x0F, 0x18, 0x19, 0x1A, 0x1B, 0x1C, 0x1D, 0x1E):
            i += 2; continue
        if b < 0x20:
            i += 1; continue
        c = ch(b)
        out.append(c if c is not None else '\\x%02X' % b)
        i += 1
    return ''.join(out)
