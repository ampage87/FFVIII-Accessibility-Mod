## v0.20.109

#minigame-bgbtl: **build fix — and the lint that should have caught it.**

```
field_minigame_bgbtl.inl(780): error C2712:
    Cannot use __try in functions that require object unwinding
```

### One line

v0.20.108 put this inside the `__try` in `FreezeWatchdog`:

```cpp
const std::string avi = FmvSkip::GetCurrentAviName();
```

`std::string` needs unwinding, and MSVC refuses SEH in any function that does.

Split into a small `IsGardenBattleAvi()` helper with no SEH — **which it never
wanted anyway.** It reads `FmvSkip`'s own state through a normal C++ call, not
engine memory through a raw pointer, so there was nothing there for a structured
handler to catch. `FreezeWatchdog` keeps the heartbeat and the AVI latch and
loses the `__try` entirely.

### The real lesson is the gap, not the typo

**The host harnesses compile with g++, where `__try` is a macro that expands to
`if(1)`.** This entire class of error is invisible to every pre-build check the
mod has.

That is the same shape as the v0.20.89 declaration-order bug: MSVC-only, and
therefore only ever caught by Aaron's build. Twice is a pattern, so it gets a
check.

### `tests/lint_seh.py`

Walks `src/`, finds every function containing `__try`, and flags any that also
contains a type needing unwinding.

**The rule MSVC applies is per-function, not per-block** — an object declared
*outside* the `__try` still triggers C2712 — so the lint scans whole
brace-balanced function bodies rather than just the guarded region.

Verified both ways:

```
$ python3 tests/lint_seh.py /tmp/lintcheck
  C2712 RISK  bad.inl:1  Example() uses __try and 'std::string'
  lint_seh: 1 problem(s) -- MSVC will reject these

$ python3 tests/lint_seh.py src
  lint_seh: scanned 85 file(s) containing __try
  lint_seh: OK
```

It reports the exact v0.20.108 construct, and it reports the shipped tree clean
across all 85 files that use `__try`. **Run it before any build that touches an
SEH block.**

### Verification

* `lint_seh` OK — 85 files scanned, 0 problems.
* `tests/minigame_bgbtl_compile.cpp`: **0 errors**, with all v0.20.108 behaviour
  intact — the ten-minute no-cap check, the FMV arm, the unrelated-movie
  negative, once-per-attempt, the Game Over re-arm, the AVI latch, patch/restore
  on every exit path, the HP replay, the legend filter.
* `garden_harness` 26 ok / 0 bad; `catalog_story_test` 13 checks / 0 failures;
  `garden_aboard_test` and `world_map_harness` pass.

**No behaviour change from v0.20.108.** The pause still has no time limit, the
guard thread still watches the heartbeat, and **the v0.20.108 BAT stands exactly
as written.**

`field_navigation.cpp` unchanged at 81,587 bytes — 333 from the hard fail.
**SPLIT BEFORE THE NEXT EDIT.**
