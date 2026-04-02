# FF8 GF fire trigger: you've been watching the wrong address

**The GF summon timer at 0x01D27D94 is not Ally 0's ATB — it's Enemy 1's.** The battle entity stride is **0xD0** (208 bytes), not 0x1D0, and Current ATB sits at offset **+0x0C** within each entity. Your "entity+0x27C" calculation (0x01D27B18 + 0x27C = 0x01D27D94) actually spans **three full entities** and lands on Enemy 1's ATB: `3 × 0xD0 + 0x0C = 0x27C`. This single misidentification explains why capping that address at 6000/12000 had zero effect on GF firing — you were throttling the first enemy's turn gauge while the real GF countdown ticked elsewhere, undisturbed.

Furthermore, the GF charge timer is a **fundamentally separate mechanism** from the ATB system. ForteGSOmega's reverse-engineered FAQ proves the GF timer uses a completely different update formula — a countdown decremented by SpeedMod alone (1/2/3 for Slow/Normal/Haste), with **no dependency on the character's Speed stat**. The normal ATB increments by `(Spd + 30) × SpeedMod / 2`. This makes it impossible for both systems to share the same counter and code path. The fire-decision variable almost certainly lives in the **GF state region you already identified at 0x01D76860–0x01D769DF**, not in the entity array at all.

## The entity array is 0xD0, not 0x1D0

The ff8-speedruns/ff8-memory repository — the authoritative community memory map for FF8_EN.exe Steam 2013 — documents the battle entity array with **0xD0 byte stride**, consistently confirmed across all seven entity slots:

| Slot | Role | Base address | Current ATB (+0x0C) |
|------|------|-------------|-------------------|
| 0 | Ally 1 | 0x01D27B18 | **0x01D27B24** |
| 1 | Ally 2 | 0x01D27BE8 | 0x01D27BF4 |
| 2 | Ally 3 | 0x01D27CB8 | 0x01D27CC4 |
| 3 | Enemy 1 | 0x01D27D88 | **0x01D27D94** ← your monitored address |
| 4 | Enemy 2 | 0x01D27E58 | 0x01D27E64 |
| 5 | Enemy 3 | 0x01D27F28 | 0x01D27F34 |
| 6 | Enemy 4 | 0x01D27FF8 | 0x01D28004 |

Ally ATB fields are **2 bytes**; enemy ATB fields are **4 bytes**. No public FF8 documentation references a 0x1D0 entity stride or a +0x27C ATB offset anywhere — not in ff8-speedruns, FFNx source, the FFRTT wiki, FearLess Revolution CE tables, or any Qhimm forum post indexed by search engines. The 0x1D0 stride likely arose from conflating two different data structures: the live 0xD0-byte battle entity array at 0x01D27B18 and the ~0x1D0-byte character junction/save-data blocks near 0x01CFF000 (which store equipped spells, junction assignments, and stat modifiers — not live battle state).

Your hardware breakpoint data actually confirmed this layout. **Writer #5** at FFNx address 0x6E909F16 iterates with stride 0xD0 — you noted this yourself. All five writers to 0x01D27D94 were writing to Enemy 1's ATB as part of the normal per-entity ATB update loop.

## The GF timer is a dedicated countdown, not ATB reuse

ForteGSOmega's Battle Mechanics FAQ (PSX reverse engineering, Section 5.1) provides the exact GF timer formula, which differs from the ATB system in every respect:

**GF charge Duration (initial countdown value):**
```
Compatibility = 6000 - 5 × ShownCompatibility
Duration = Compatibility × BattleSpeed × 0.9143 / 32
```

**Decrement rule:** *"Duration is decreased by SpeedMod every tick and when it reaches 0, the GF is summoned."* SpeedMod is 1 (Slow), 2 (Normal), or 3 (Haste). At **15 ticks/second** with default BattleSpeed=3 and max compatibility (1000):

- Compatibility = 6000 - 5000 = **1000**
- Duration = 1000 × 3 × 0.9143 / 32 ≈ **85.7**
- Time to fire = 85.7 / (SpeedMod × 15) = 85.7 / 30 ≈ **2.86 seconds** ✓

This matches your observed "~3–4 seconds at max compatibility." The critical architectural difference is that **the GF timer ignores the character's Speed stat entirely** — a character with 70 Speed summons at the same rate as one with 10 Speed, all else equal. The normal ATB increment of `(Spd + 30) × SpeedMod / 2` would produce wildly different fill times for these characters, proving the GF timer cannot share the ATB code path.

Additional evidence for separate storage: the FearLess Revolution CE table (t=1029) ships **"Max ATB Gauge" and "Quick Summons" as distinct cheat scripts**, implying different target addresses. The Remastered table (t=10172) lists separate "ATB Multiplier" and "G.F. HP and ATB modifiers" contributed by different authors — the contributor explicitly referred to "G.F. ATB" as its own set of addresses.

## Where the fire-decision variable actually lives

The GF state region at **0x01D76860–0x01D769DF** is by far the strongest candidate. You've already mapped several fields in this region:

| Address | Observed behavior | Likely role |
|---------|------------------|-------------|
| 0x01D76868 | Transitions 0→1→2→3→5 during loading | GF phase state machine |
| 0x01D7687F | Increments ~8–12 per 200ms poll | Animation frame counter or sub-timer |
| 0x01D76970 | Set to summoner's party slot | GF summoner index |
| 0x01D76971 | Set to 1 during GF loading | GF-active flag |
| 0x01D769D6 | Timer byte (freezing had no effect) | Display/cosmetic timer |

The **actual GF countdown timer** is a value in this region that starts at ~86 (at max compat, BS=3) and decrements by 2 (Normal SpeedMod) each tick. At 15 ticks/second over a 200ms poll window, that's 3 ticks × 2 = 6 units of decrement per poll. The counter at 0x01D7687F incrementing by 8–12 per poll doesn't match this pattern perfectly (it goes up, not down, and the magnitude is off), suggesting 0x01D7687F is an animation frame counter rather than the fire-decision timer. **The actual timer is likely a 2-byte value at a nearby offset in this 128-byte region that you haven't yet logged** — possibly between 0x01D76870 and 0x01D768CF, or in the GF struct portion at 0x01D76960–0x01D769CF.

The reason clamping state68 (0x01D76868) from 5→3 failed is almost certainly that **the fire code writes state=5 and dispatches the summon animation atomically within the same tick**. Your ATB hook at 0x004842B0 only intercepts the general ATB update function — the GF timer check runs in a separate code path called from `battle_main_loop`, so by the time your hook executes, the fire has already been dispatched.

## How to find the exact fire comparison

**Approach 1 — Hardware data breakpoints on the GF state region.** Set 4-byte hardware write breakpoints covering 0x01D76860–0x01D769DF systematically (you get 4 breakpoints at a time in x86, each covering 4 bytes). During GF loading, look for a value that:
- Gets initialized to a value near `(6000 - 5*compat) * BS * 0.9143 / 32`
- Decrements by exactly SpeedMod (2 under Normal status) every tick (~66ms at 15fps)
- Hits 0 at exactly the frame the summon animation triggers

When you find this decrementing value, the code performing the decrement and the subsequent `JLE`/`JZ` comparison to zero is your fire decision.

**Approach 2 — Breakpoint on the battle effects table.** The GF summon effect function pointers live in a table at **0xC81774**, indexed by a `FF8BattleEffect` enum (Quezacotl, Shiva, Ifrit, etc., as defined in FFNx's `src/ff8/battle/effects.h`). Set a hardware **read** breakpoint on the table entry for whatever GF you're testing. The moment the game reads this function pointer to call the summon effect, you're at the instant of fire. Walk the call stack backward to find the comparison that triggered the dispatch.

**Approach 3 — Trace from battle_main_loop downward.** `battle_main_loop` is resolved from `main_loop + 0x340`. Set a conditional breakpoint on this function (break only when 0x01D76971 == 1, i.e., GF is loading) and single-step through its call chain. The GF timer check is one of the subroutines in this call tree — separate from the ATB update at 0x004842B0. Look for a function that reads from the 0x01D768xx range, decrements a value, and branches on the result.

**Approach 4 — Conditional breakpoint on state68 transition to 5.** Set a hardware write breakpoint on 0x01D76868, conditional on the new value being 5. When it fires, examine the instruction writing 5 and the surrounding code. The preceding comparison (which decided "timer expired, transition to fire state") will be within a few instructions or the calling function.

## The complete GF summon code path (reconstructed)

Based on all evidence, the vanilla engine's GF summon path works as follows:

1. **Player selects GF command** → game sets GF-active flag (0x01D76971=1), stores summoner slot (0x01D76970), initializes GF timer to `(6000 - 5*compat) * BattleSpeed * 0.9143 / 32`, sets state68 to initial phase (0 or 1).

2. **Each battle tick** (~15/sec), `battle_main_loop` calls:
   - The ATB update subroutine (0x004842B0) — iterates entities at 0xD0 stride, updates +0x0C
   - A **separate GF timer subroutine** — checks if GF is active, decrements the GF countdown by SpeedMod, compares against 0

3. **When GF timer reaches 0** → the GF timer subroutine writes state68=5 (fire), then dispatches to the summon effect handler via the function pointer table at 0xC81774[gf_id]. This writes state and dispatches **atomically in the same function call**, which is why external clamping of state68 arrives too late.

4. **Summon animation plays** → the effect handler (e.g., 0x6C3640 for Quezacotl, 0xB586F0 for Leviathan) runs the animation state machine, handles Boost input, triggers vibration via 0x4A29A0.

5. **Animation completes** → damage calculation, effect application, return to normal battle flow.

The GF timer display function at 0x004B0500 runs independently of all this, reading compStats+0x14 (a display copy) for the visual gauge. FFNx's rendering path writes to compStats+0x14 from its own interpolation — this is why capping that value had no gameplay effect.

## Practical next steps for your mod

Your Enhanced Wait Mode mod should **not** try to freeze the GF timer by capping a display variable or an unrelated entity's ATB. Instead:

- **Find the fire-decision comparison** using Approach 1 or 2 above. Once you have the instruction address, hook or NOP it during Enhanced Wait Mode.
- **Hook the GF timer decrement function** (not the ATB function at 0x004842B0). This is a separate function in `battle_main_loop`'s call chain that specifically handles the 0x01D768xx region. Make it skip the decrement when your mod's wait mode is active.
- **Set the actual ally ATB addresses correctly.** Ally 0's Current ATB is at **0x01D27B24**, not 0x01D27D94. If you need to freeze ally ATB during wait mode, target the right addresses.
- **The 0xD0-stride entity loop** (confirmed by FFNx writer #5 and ff8-speedruns data) is the correct iteration pattern. Any hooks on the ATB update function should use this stride, with 7 entities (3 allies + 4 enemies).

The OpenFF8 project at github.com/Extapathy/OpenFF8 contains a `memory.h` with `ff8vars` and `ff8funcs` struct definitions that likely maps additional battle fields — it requires cloning the repo directly (the file isn't indexed by search engines). The FearLess Revolution CE table at thread t=1029 contains a "Quick Summons" script whose assembly code would reveal the exact timer address — download the 2MB .CT file and open it in Cheat Engine to inspect the script.

## Conclusion

Ten failed approaches all stem from one root cause: **the 0x1D0 stride / +0x27C offset was wrong**, and every experiment built on it tested an irrelevant address (Enemy 1's ATB). The GF timer is architecturally separate from ATB — it's a SpeedMod-only countdown stored in the GF state region around 0x01D76860, with a fire decision embedded in a subroutine distinct from the ATB updater at 0x004842B0. The most efficient path to the exact fire instruction is a hardware write breakpoint sweep of the 0x01D76860–0x01D769DF region, filtering for a value that decrements by exactly SpeedMod per tick and reaches zero at the moment of summon execution. Once identified, a MinHook on that decrement function — not the ATB function — will give your Enhanced Wait Mode clean control over GF fire timing.
