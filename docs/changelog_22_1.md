# v0.22.1 — the four Magic bugs

**#81.** Use and Rearrange were reported clean. These are the other four.

## 1. "Magic list" on every page turn

Paging runs state 13 → 14/16 → 13. The poll recorded that transient state as the
current phase, so coming back to 13 looked like a fresh arrival and re-spoke the
header. It now keeps the last **spoken** phase and ignores transients, so the
header is said once — when focus actually lands in the list.

`menu_sim` drives the real sequence (land, then four page turns through state 16)
and asserts exactly one header and five slot lines.

## 2. "/" did nothing on the Magic screen

`AnnounceHelpText` scrapes the **rendered** text out of the GCW buffer and hunts
for a dash separator or a known prefix. The Magic bar has neither, so the key was
silently a no-op.

It never needed the scrape. The module caches a pointer to the exact string the
bar is drawing:

```
004F6996: push 0x4f70c0          ; the bar's content callback
004F699B: lea  edx, [esi + 0x24] ; ctx = &module[+0x24]
```

and the text getter `0x004BD630` does no copying — it returns a pointer straight
into the loaded `mngrp.bin`. So `+0x24` is raw NUL-terminated FF8 bytes in
long-lived data, written with a single aligned dword store, which makes it safe
to read from the mod's thread. `NULL`, `0x01D7714C` (the getter's own fallback)
and `0x01CFF84C` (the empty-string constant) all mean "no text".

Known and stated rather than hidden: the game does not refresh `+0x24` in target
select or in All's second step. The string sitting there is still the right one
for those screens — just not rewritten — so this reads slightly stale text, never
wrong text.

## 3. Exchange was entirely silent

| state | screen |
|---|---|
| 26 | your list |
| 28 | choose the partner |
| 44 | the partner's list |
| 52 / 55 | Give All · Take All · Split |
| 63 | the quantity split |

Both columns share the single per-character cursor array, with column B paging on
its own byte, so the partner's slot is `(cursor[B] & 3) + pageB * 4`.

**Each panel now names whose list it is.** Two lists on one screen, and "Cure,
quantity 47" spoken identically for both, is precisely the ambiguity that made
this flow unusable without sight.

The split reads **both** counters every time — "Cure, move 12, keep 35". The
screen shows two numbers that move in opposite directions; hearing only one tells
you nothing about which way the last keypress went.

## 4. All — and the direction is the reverse of what it looked like

You reported it as give-then-receive. **The code is the other way round, and the
game's own help bar agrees with the code.**

- State **97** picks the **receiver**; state **99** picks the **giver**.
- State 105 calls `0x004F5FA0(arg1 = +0x62, arg2 = +0x64)`. Inside, `0x4C2C70`
  **adds** each of arg1's spells to arg2 and `0x4C2D50` **removes** them from
  arg1. So the step-2 character loses and the step-1 character gains.
- Step 2's character mask is built as `available & ~(1 << +0x64)` — it excludes
  the step-1 choice, which only makes sense if step 1 already fixed a role.
- The action's own label is *"Take all magic **from** other members"*. The
  character whose screen you opened is the taker.

So the mod speaks the game's own wording verbatim: **"Select member to receive
magic"** on step 1 and **"Select member to transfer magic"** on step 2 — group 8
entries 12 and 13, the same strings the help bar draws. If that still reads
backwards in play, the two strings are the thing to check, because they are what
a sighted player is being shown.

The transfer itself now announces: *"All magic moved from Selphie to Quistis"*.
There is no Yes/No confirmation in this flow — the second confirm runs a
pre-flight check and either commits or opens a warning box (state 106), which is
also announced.

## The root cause of 3 and 4

v0.22.0 routed on `screenMode == 3` for "choosing the second character". **Both
flows use screen mode 3, and both use mode 4** — so Exchange and All were
literally indistinguishable, and neither could be narrated. Routing is now on the
state number, which for every screen here is reached from nowhere else in the
machine.

## A compiler error caught before the compiler

`lint_seh` flagged C2712 on the first draft of the help reader: MSVC rejects a
function that mixes `__try`/`__except` with anything needing object unwinding, and
the decode returns a `std::string`. The raw copy is now its own frame. That gate
paid for itself.

## Gates

`menu_sim` OK (0 bad) — new: the paging regression, both Exchange panels naming
their owner, column B's independent page, the two popups, the split's paired
counters, the All direction pinned in both steps and in the transfer line, and a
routing test proving screen mode alone cannot separate the two flows. Fuzz
extended to all eleven speaking phases.

`menu_magic_compile` OK (0 bad) — new: the help-bar reader against a real mapped
module, including both "no text" sentinels and declining off the Magic screen.

`entryaim` · `trigwalk` · `trigseg` · `pathdecimate` · `vehsig` OK ·
`catalog_story` · `garden_aboard` 0 failures · `lint_seh` OK (89 files) · all four
harnesses compile.

## BAT

- **Page through the spell list** — the header should be spoken once.
- **Press "/"** on the action row, the spell list and the sort popup.
- **Exchange**: pick a spell, pick a partner, pick their slot, then try Split.
- **All**: listen to which step says "receive" and which says "transfer", and
  confirm the transfer runs in that direction. If it does not, say so — the two
  strings come straight from the game and would then be mislabelled by it.
