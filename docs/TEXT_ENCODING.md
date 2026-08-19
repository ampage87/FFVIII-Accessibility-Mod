# FF8 text encoding — the expander, the word table, and the glyph grid

*v0.35.0 (#93). Everything here is read out of `FF8_EN.exe` and the shipped
data files; nothing is inferred from behaviour.*

---

## 1. The pipeline: a string is EXPANDED before it is DRAWN

The mod spent four builds looking for a `cmp al, 0x0E` near the glyph drawer and
never found one, because the drawer never sees a `0x0E`. The shop's own draw
function shows the whole chain:

| address | what it does |
|---|---|
| `0x004ED278` | `lea ecx, [esi + 0x20]` — the description pointer |
| `0x004ED2BC` | `mov [0x01D76AA0], ecx` — hand it to the window drawer |
| `0x004ED2C2` | `call 0x004B2900` — the framed-window renderer |
| `0x004ED530` | its per-line content callback, reached from `0x004B2900` |
| `0x004ED534` | `mov ecx, [0x01D76AA0]` — pick the pointer back up |
| `0x004ED56D` | `call 0x004B8B30` — **EXPAND into a 0x80-byte stack buffer** |
| `0x004ED58B` | `call 0x004BDE30` — draw the **expanded** bytes |

`sub_4B8B30(src, dst, maxLen)` is the shared window text expander. `sub_4BDE30`
is the glyph drawer, which handles only `0x02` (advance Y) and `0x05` (draw a
number), treats `0x19..0x1F` as two-byte glyph escapes, maps every byte `>= 0x20`
to `glyph = byte - 0x20`, and **terminates on anything `<= 0x18`**. So any control
code below `0x19` that survives to the drawer ends the line — which is why the
expansion has to happen first, and why it happens in one place for every screen
in the game.

### `sub_4B8B30`'s dispatch, byte by byte

```
0x004B8B65  mov cl, [ebp] / inc ebp      ; read the next source byte
0x004B8B71  mov [esi], cl / inc esi      ; store it, provisionally
0x004B8B74  cmp ecx, 0x19 / jl  <handle> ; codes below 0x19 are control codes
0x004B8B79  cmp ecx, 0xE8 / jl  <loop>   ; 0x19..0xE7 pass through verbatim
                                          ; >= 0xE8 expands to a 2-byte pair
0x004B8B81  ecx == 0/1/2/7  -> RETURN     ; these END the expansion
0x004B8BAD  ecx == 3        -> 0x4B9010 / 0x4B8E40 with (code<<8 | param)
0x004B8BFB  ecx == 4        -> the numeric insert (#77)
0x004B8C74  ecx == 0x0C     -> sub_47E970(param - 0x20)  location name
0x004B8C8A  ecx == 0x0D     -> sub_47EA30(param - 0x20)  other name
0x004B8CB8  ecx >= 0x0E     -> THE WORD TABLE (below)
0x004B8D21  anything else   -> copy the PARAM byte through as a literal
                               (this is the path 0x05/0x06/0x08/0x09/0x0A/0x0B
                                take, so all six CONSUME one parameter byte)
0x004B8D2D  0x10..0x1F      -> sub_49A860(code & 0x1F), NO parameter byte
0x004B8E04  >= 0xE8         -> two bytes from a pair table
```

---

## 2. `0x0E xx` — the substituted word

```
0x004B8CBD  mov dl, [ebp] / inc ebp       ; read + consume the param byte
0x004B8CC0  add ecx, -0xE                 ; group = code - 0x0E
0x004B8CC3  sub dl, 0x20                  ; idx   = param - 0x20
0x004B8CC7+ eax = group*8 - group; shl 5  ; entry = group*224 + idx
0x004B8CD6  mov ecx, [0x01D2B80C]         ; the table
0x004B8CE4  test ecx, ecx / je            ; null table   -> emit NOTHING
0x004B8CF1  movzx edx, word [ecx]         ; entry count (first word)
0x004B8CF4  cmp edx, eax / jle            ; out of range -> emit NOTHING
0x004B8D01  movzx esi, word [ecx+eax*2+2] ; offset, relative to the table start
0x004B8D06  add esi, ecx
0x004B8D0A  call 0x49A740 / 0x49A790      ; strcpy + strlen into dst
```

### Where the table comes from

`[0x01D2B80C]` is written at `0x004A1980`:

```
0x004A198A  mov eax, [0x00B6D060]
0x004A1998  mov [0x01D2B80C], eax
```

`[0x00B6D060]` is the load buffer that `0x0047D46C` fills, from a filename
assembled out of the string constant at `[0x00B81024]`:

```
0x00B81024: "namedic.bin"
```

**`namedic.bin` is `main.fs` entry 13** (`c:\ff8\data\eng\namedic.bin`), 408
bytes uncompressed, LZS-compressed in the archive. Note the `.fi` triple is
`{uncompressed size, offset, compression}` — read the other way it yields
plausible multi-megabyte files that decode into nothing.

### Layout, and the shipped English contents

```
u16  count                    ; 32
u16  offset[count]            ; relative to the start of the file
...  packed NUL-terminated FF8-encoded strings
```

| idx | param | word | | idx | param | word |
|---|---|---|---|---|---|---|
| 0 | `0x20` | Galbadia | | 16 | `0x30` | Dollet Station |
| 1 | `0x21` | Esthar | | 17 | `0x31` | Desert Prison Station |
| 2 | `0x22` | Balamb | | 18 | `0x32` | Lunar Gate |
| 3 | `0x23` | Dollet | | 19 | `0x33` | **Restores** |
| 4 | `0x24` | Timber | | 20 | `0x34` | **status** |
| 5 | `0x25` | Trabia | | 21 | `0x35` | **learns** |
| 6 | `0x26` | Centra | | 22 | `0x36` | **ability** |
| 7 | `0x27` | Fishermans Horizon | | 23 | `0x37` | **Magic** |
| 8 | `0x28` | East Academy | | 24 | `0x38` | **Refine** |
| 9 | `0x29` | Desert Prison | | 25 | `0x39` | **Junctions** |
| 10 | `0x2A` | Trabia Garden | | 26 | `0x3A` | **Raises** |
| 11 | `0x2B` | Lunar Base | | 27 | `0x3B` | **command** |
| 12 | `0x2C` | Shumi Village | | 28 | `0x3C` | **Magazine** |
| 13 | `0x2D` | Deling City | | 29 | `0x3D` | Ultimecia Castle |
| 14 | `0x2E` | Balamb Garden | | 30 | `0x3E` | **Garden** |
| 15 | `0x2F` | East Academy Station | | 31 | `0x3F` | **Deling** |

Group 1 (`0x0F`) would start at entry 224, so in the English file every `0x0F`
falls past the count and emits nothing — exactly as the engine does.

Entry 3, **Dollet**, is the word that v0.18.3.248 (#78) chased through the
field-dialog path. It is the same table; there was never a second one.

### The BAT line, resolved

v0.34.9 logged the Magic Scroll's description as ten raw bytes that spoke as
`"GF"`:

```
F0 20 0E 35 20 0E 37 20 0E 36
GF  _  learns _  Magic  _  ability
```

→ **"GF learns Magic ability"**. And v0.34.8's *" 1000 HP to GF"* — the fragment
that sounded complete and so slipped past a length-ratio test — is
`0E 33` + the rest: **"Restores 1000 HP to GF"**.

Decoding the shipped `kernel.bin` with this table resolves **all 80** of its
substituted strings with nothing dropped: *"Cures abnormal status and Magic
effects"*, *"Makes GF forget an ability"*, *"GF learns Elem-Defx4 ability"*.

---

## 3. The raw encoding IS the glyph grid, shifted by `0x20`

`sub_4BDE30` maps `glyph = byte - 0x20` for every byte `>= 0x20`. That single
line means the raw byte table and the sysfnt grid were never independent
descriptions — they are one table with an offset.

Checked against the two hand-built tables this mod carried: of the ~90 bytes
both defined, they agree on **every one** except seven slots where the Deling
grid is blank (`0x36`/`0x37`/`0x3E`/`0x3F` quotes, `0x3A` apostrophe) or blank
for a compression pair (`0xFA` "EC", `0xFD` "FE"). Those seven are an explicit
override list; everything else is derived.

The payoff is 57 bytes the raw table had **no entry for at all**, and which were
therefore silently dropped:

| bytes | what they are |
|---|---|
| `0x79`–`0xA7` | accented capitals and lower case (À, Ç, É, Î, Ñ, Ö, Ü, Œ, ß …) |
| `0xA9`, `0xAA` | `[` and `]` |
| `0xB5` | `;` |
| `0xB8` | `x` (multiplication) |
| `0xBC` | ` degrees` |
| `0xBF` | `-` |
| `0xC2` | `+/-` |
| `0xC9`–`0xCB` | `TM`, `<`, `>` |

`0xE8`–`0xFF` (the two-character compression pairs) come from the same grid at
glyph `0xC8`–`0xDF`, so the hand-written `switch` for them is gone too.

---

## 4. Which control codes consume a parameter byte

Straight off `sub_4B8B30`. This matters because a code that should consume one
and does not leaks its parameter into the sentence as a stray letter.

| code | parameter? | what the mod does |
|---|---|---|
| `0x00` | — | end of string |
| `0x01`, `0x02` | — | line break → a space |
| `0x03` | yes | character name (party table) |
| `0x04` | yes* | numeric insert; the FIELD path pre-expands it (#77). Elsewhere the number is genuinely lost, and is now **counted** as lost |
| `0x05`, `0x06` | yes | icon / colour — no text by design |
| `0x07` | **no** | the expander RETURNS on it (`0x004B8B9B`) before reading anything |
| `0x08`, `0x09` | yes | `0x004B8D21` reads one. **v0.35.0 fixed: these used to leak.** |
| `0x0A` | yes | value insert |
| `0x0B` | yes | choice-cursor marker — no text by design |
| `0x0C` | yes | location name (`sub_47E970`); the field path resolves it |
| `0x0D` | yes | other name (`sub_47EA30`). **v0.35.0 fixed: this used to leak.** |
| `0x0E`, `0x0F` | yes | **the word table** |
| `0x10`–`0x1F` | **no** | name codes via `sub_49A860(code & 0x1F)` (`0x004B8D2D`) |

\* `0x04` still emits the historical `". "` rather than nothing. Every field
dialog in the mod has been heard with it and changing it is a separate,
BAT-able decision — but the loss is no longer passed off as a whole sentence.

---

## 5. Knowing when the text is a fragment

`FF8TextDecode::Decode()` takes an optional `int* droppedOut`: the number of
bytes consumed that produced **no text** — an unresolved word, an unmapped
glyph, a name insert this decoder cannot expand. Codes that carry no text by
design (icons, colours, the choice-cursor marker, line breaks) are not counted.

Zero means "what you got back is the whole string". Anything else means a caller
reading it aloud is reading a fragment and must say so — which is what the shop's
`"Partial description: …"` marker is for.

`ShopCountDropped()` is **deleted**. It was a second, hard-coded copy of "what
the decoder throws away", and it stopped being true the moment the decoder
stopped throwing those bytes away. One implementation; the two cannot disagree.

---

## 5a. Verified in the game (BAT, 2026-08-19)

The table above was read out of `namedic.bin` before the build ever ran. The
item shop then said it back, word for word:

| v0.34.9 spoke | v0.35.0 spoke |
|---|---|
| `"GF"` | **"GF learns Magic ability"** |
| `" 1000 HP to GF"` | **"Restores 1000 HP to GF"** |
| `" HP to all GF"` | **"Restores HP to all GF"** |
| `" for dog lovers"` | **"Magazine for dog lovers"** |
| `"Makes GF forget an"` | **"Makes GF forget an ability"** |

Zero `[SHOP-TEXT]` lossy lines and zero `"Partial description:"` across the
whole run.

The shop is only where it was noticed. `FF8TextDecode::Decode` has **57 call
sites across 24 files** — every menu screen, the battle readers, the field
scan, save, junction, magic, cards, status — and all of them were dropping the
same bytes.

---

## 6. What is still open

* **`0x04` outside the field path.** The number is lost and the `". "` remains.
  It is counted, so it is at least audible as a gap.
* **`0x10`–`0x1F` name codes.** `sub_49A860` is not called yet; these are
  counted as lost. No real text observed using them.
* **`0x07` ends the expansion in the engine** but is a no-op in the decoder.
  Nothing has been seen to depend on it; changing it could truncate text that
  reads correctly today.
* **The `>= 0xE8` pair table is hard-coded**, not read from the engine's own
  copy at `0x01D2B80C + code*2`. It is verified against real text, but the same
  argument that moved the word table to a live read applies here eventually.

---

## 7. Where the code lives

| file | what it holds |
|---|---|
| `src/ff8_text_decode.cpp` | the derived glyph table, `ResolveWord()`, `Decode()` |
| `src/ff8_text_decode.h` | `Decode(..., int* droppedOut)`, `ResolveWord()`, `SetWordTableBase()` |
| `src/field_dialog_expand.inl` | pre-expands `0x04`/`0x0C`/`0x0D`/`0x0E` for field dialog; calls `ResolveWord()` |
| `src/menu_tts_shop.inl` | reads `droppedOut` to decide "description" vs "Partial description" |
| `tests/text_decode_compile.cpp` | the real decoder driven with the real 408 bytes of `namedic.bin` |
