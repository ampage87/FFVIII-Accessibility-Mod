# v0.23.1 — #82: the status-attack row was reading half a mask

Aaron's v0.23.0 BAT:

> 1. Junctioning to ST-Atk doesn't seem to announce any value. Regardless of
>    which spell I selected for it I heard ST-Atk would have no effect.
> 2. As I moved through the page of spells to junction I heard repetitive
>    announcement on each page.

---

## 1. The status-attack mask is assembled, not stored

I read the mask as a u16 at block `+0x1B4` and the percentage as a u16 at
`+0x1B6`. Both wrong, and wrong in the way that produces **silence** rather than
a wrong number — which is why the row presented as unhooked.

`0x004E0FA0` builds the 13-bit mask from two places:

```
mask  = block[0x1B4] & 0x7F                  statuses 0..6
if (block[0x18C] & 0x0001) mask |= 1 << 7    Sleep
if (block[0x18C] & 0x0004) mask |= 1 << 8    Slow
if (block[0x18C] & 0x0008) mask |= 1 << 9    Stop
if (block[0x18C] & 0x0200) mask |= 1 << 10   Curse
if (block[0x18C] & 0x4000) mask |= 1 << 11   Confuse
if (block[0x18C] & 0x8000) mask |= 1 << 12   Drain
```

That is FF8's real two-word status bitfield folded into the thirteen entries the
junction screen shows. **Six of the thirteen live entirely in the second word —
Sleep among them**, which is about the most likely thing anyone junctions to
ST-Atk. Reading `+0x1B4` alone returns zero for all six.

The percentage is also offset: `0x004E0C7D` does `sub eax, 0x64` on `+0x1B6`, so
100 means "nothing". The elemental attack percentage next door is absolute
(`0x004E12C3` uses `+0x1C5` raw). That asymmetry is the game's, not a slip.

New `JuncAssembleStatusMask()` in the model does exactly what the game does, and
`menu_sim` pins every one of the thirteen bits individually plus the requirement
that all thirteen are reachable — a partial mask is the failure mode here, and a
spot-check of two bits would have passed on the broken version.

**Third independent confirmation of the status ordering, for free.** kernel.bin's
per-spell ST-Atk mask (`entry + 0x26`) gives exactly one spell per bit, and each
one is the spell of that name: Death→0, Bio→1, Break→2, Blind→3, Silence→4,
Berserk→5, Zombie→6, Sleep→7, Slow→8, Stop→9, Confuse→11, Drain→12, and
Pain→1,3,4. The ordering now agrees from three unrelated directions.

## 2. "No effect here" on every spell was correct, and still wrong to say

`0x004C2E50(spellId, slot)` reads the spell's kernel entry, and for ST-Atk that
is the status mask at `entry + 0x26`. **Only thirteen spells in the entire game
have a non-zero one** — one per junctionable status. So on ST-Atk almost
everything a player is carrying genuinely does nothing.

Correct, but hearing it spell after spell sounds exactly like a hook that has
given up, which is how Aaron read it. It is now said **once, on arrival**:

```
Choose magic for Status attack. None of your magic affects this row
```

The per-spell qualifier stays for the mixed case. An empty inventory does not
trigger it — that would blame the row for the player having no magic.

## 3. The two attack rows previewed as "no change"

The preview's delta walk only covered the defence arrays, so on Elem-Atk and
ST-Atk — where what the row would *become* is the entire question — every
candidate reported doing nothing. An attack row is a set plus one percentage,
not a per-entry table, so there is no useful delta: the line now says the
resulting set.

```
Sleep, quantity 30, Sleep 30 percent
```

## 4. The page-turn repeat

Paging the spell list is **59 → 60 → 59** (state 60 at `0x004DED18` is page-left,
62 is page-right). The poll recorded the transient state, so returning to 59
looked like a fresh arrival and the header was spoken again.

**The Magic submenu had this exact bug in v0.22.1** — "Magic list" on every page
turn — and the fix is the same: remember the last state *spoken in*, never the
last state seen. Passing through 60 now leaves every remembered value untouched.

The dedup key also carries the cursor positions now, so a page turn onto an
identically-worded entry ("Empty", or the same spell held twice) still speaks.
Only the words are spoken.

`menu_junction_compile` replays the real 59 → 60 → 59 chain seven times and
asserts zero repeated headers and seven spoken lines — **replaying the
transition rather than constructing its endpoints**, which is the rule the last
three of these bugs have earned.

---

## Verified

- `menu_sim: OK (0 bad)` — all thirteen status bits individually, the
  reachability of the whole set, the `+0x1B4` bit-7 exclusion, the offset
  percentage, a Sleep junction end to end, both attack-row previews, and the
  four header cases including the empty-inventory one.
- `menu_junction_compile: OK (0 bad)` — the seven-page paging replay, and the
  status-attack read through both halves of the mask at the real offsets.
- `menu_magic_compile: OK (0 bad)`; `lint_seh: OK (90 files)`; entryaim /
  trigwalk / trigseg / pathdecimate / vehsig OK; catalog_story and
  garden_aboard 0 failures; all four harnesses compile.
