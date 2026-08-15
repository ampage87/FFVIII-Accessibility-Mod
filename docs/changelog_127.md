## v0.20.127

#dialog: **the phantom is four phantoms, and a field change leaves exactly the
debris an FMV does.**

> *"There is a repeat of phantom dialogue after Squall finishes his speech.
> Squall returns to the headmaster's office lower level, which is empty, but
> Irvine's dialogue from before where he says to be CERTAIN there is nothing
> Squall can do to save Rinoa, inadvertently repeats. Irvine is not in the room
> and this is a phantom."* — Aaron

### The log, in six lines

```
15:39:56  [GETSTR] Irvine "...don't give up until you're CERTAIN that there's
                          nothing more you can do!"      <- the real line
...three and a half minutes later...
15:43:33  MAPJUMPO -> destField=255 (B-Garden Headmaster's Office 7)
15:43:35  [fieldload] id=255 name='bgsido5a'
15:43:35  [POLL] win[0] Speaking: "the attack.""
15:43:35  [POLL] win[1] Speaking: "l you're CERTAIN that there's nothing more you can do!""
15:43:35  [POLL] win[2] Speaking: "'s  nothing more you can do!""
15:43:35  [POLL] win[3] Speaking: "an do!""
```

**Four of them, in a single poll, the instant the empty office finished
loading.** Aaron heard the second because it is the longest and the most
recognisable; the other three are the same sentence chewed shorter.

And every one starts **mid-word** — `"l you're"` is the tail of *"until
you're"*, `"an do!"` the tail of *"can do!"*. That is the tell: the window
objects still point into the **previous field's** dialogue buffer, partway
through wherever its paging had reached. The engine does not clear them on a
field change, and the poller had no way to know the field had changed.

### The fix already existed, for a different transition

`PollWindows` has carried an FMV guard since v04.16: when a movie ends, snapshot
whatever the windows hold as "already spoken", then suppress and re-snapshot
while the pointers settle. A field change is the same event with a different
cause, and it gets the same treatment now — snapshot on the change, keep
absorbing for two seconds.

The shared loop is factored into `SnapshotWindowsAsSpoken()` rather than copied
a third time; the FMV paths now call it too, so the three transition guards can
never drift apart.

**Only the poller is held back.** The new field's own dialogue arrives through
the opcode hooks (`MES`, `AMESW`, `ASK`…), which are untouched — so nothing real
is delayed, and this cannot swallow a line the game actually shows.

### Verification

* A dedicated syntax-and-behaviour probe for the new helper: two windows holding
  the exact fragments from Aaron's log are both captured as already-spoken, the
  `show_dialog` per-window tracking is reset, and the critical section comes out
  balanced. `field_dialog.cpp` has no host harness, so this is the check that
  exists.
* `lint_seh` OK (88 files); `minigame_bgbtl_compile` 0 errors; `garden_harness`
  26 ok / 0 bad; `catalog_story_test` 0 failures; `garden_aboard_test` and
  `world_map_harness` pass.
* `field_navigation.cpp` untouched at 81,645.

**NOT MSVC-built, NOT BAT'd.**

### BAT

Play the run-up to the Garden battle: after Squall's speech, when he is back in
the empty Headmaster's Office lower level, **there should be silence** — no
Irvine, no fragments. Then the mini-game as before.

Grep `[POLL] field 0x` — it logs each field change it absorbs and what it
suppressed.
