# v0.22.2 — the garbled help bar, and one name too many

**#81.** Two defects from the v0.22.1 BAT. **All now works as expected**, which
settles the direction correction: step 1 picks the receiver, step 2 the giver.

## The help bar was garbled throughout

`FF8TextDecode::DecodeMenuText` indexes its 224-entry glyph table with a **glyph
index** — because its existing caller hands it the GCW buffer, which the renderer
has already converted. `mngrp.bin` is one stage earlier and holds **text-stream
bytes**, and a text byte is `glyph + 0x20`. Every character came out 32 slots too
high.

From the real file, group 8 string 1 — bytes `59 71 63 20 6B 5F 65 67 61`:

| reading | result |
|---|---|
| indexed raw (v0.22.1) | `AaI'UEIOE` |
| minus 0x20 | `Use magic` |

The v0.22.1 comment claiming the bar and the scrape *"were always reading the
same encoding, just at different points in the pipeline"* was half right and
therefore wrong. Same **table**, different **offset** — and the offset was the
entire bug. Writing a confident sentence about a thing being equivalent is
apparently how I keep introducing these.

`MagicTextToGlyphs` now does the shift, and drops control codes along with the
parameter byte of a `0x0A`–`0x0E` variable (the game's own strings contain things
like `Can't carry {0A}5`). **`DecodeMenuText` itself is untouched** — its other
callers pass glyph indices and are correct.

## Exchange repeated the partner's name on every slot

The owner prefix now lives in the **phase header** — "Zell's magic" — spoken once
when you arrive in a panel, and never again on the per-slot line. The panel
cannot change while you move within it, so the name is news exactly once.

v0.22.1 put it on every line for a real reason (two lists on one screen, and
"Cure, quantity 47" is ambiguous between them). The header solves that without
the repetition.

## The new gate, and what it can actually prove

`tests/menu_sim.cpp` now decodes **three byte sequences lifted verbatim out of
`mngrp.bin`** — "Use magic" and both All step prompts — and asserts they come out
right. It also asserts that the **un-shifted** reading is *wrong*: a fixture that
decodes correctly either way would prove nothing at all.

`tests/menu_magic_compile.cpp`'s stub decoder is now the exact inverse of the
shift, and its header says plainly that it checks **control flow** — the pointer
read, the sentinels, the fault path, declining off-screen — and **not encoding**.
A stub cannot falsify its own convention; only the real bytes can.

## Gates

`menu_sim` OK (0 bad) · `menu_magic_compile` OK (0 bad) · `entryaim` ·
`trigwalk` · `trigseg` · `pathdecimate` · `vehsig` OK · `catalog_story` ·
`garden_aboard` 0 failures · `lint_seh` OK (89 files) · all four harnesses
compile.

## BAT

Press `/` on the action row, the spell list, the sort popup and both Exchange
panels — the text should now be the game's own words. Then walk Exchange and
check the partner is named once per panel rather than once per slot.
