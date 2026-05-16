# Dollet 30-Minute Mission Timer (and General Countdown Timer System) — Deep Research Prompt

## For ChatGPT (Deep Research mode)

---

## Context

I am developing a screen-reader accessibility mod for **Final Fantasy VIII (Steam 2013 release, FF8_EN.exe + FFNx v1.23.x)**. The mod (a `dinput8.dll` injection) provides text-to-speech navigation and assistance for blind players. The X-ATM092 chase chapter (v0.15.0–v0.15.11.0) was just completed, end-to-end accessible. Next up is the **Dollet SeeD-exam 30-minute mission countdown timer**, which the player has zero way to perceive without sighted assistance — the timer is a small numeric overlay rendered on the field screen.

I need to:

1. **Announce the remaining time** via TTS on demand (hotkey `T`) and on schedule (every 5 minutes, then at 1 minute, then at 30 seconds).
2. **Freeze the countdown** on demand (hotkey `Shift+T`) so a blind player can navigate by audio without time pressure.

To do either, I need the memory address where the remaining time is stored, and (for the freeze) the function that decrements it.

The Dollet timer is the primary target. A **secondary** target is the Fire Cavern timer (player-selected duration 10/20/30/40 minutes, then countdown until escape). Confirming whether both timers share the same variable or use separate ones would tell me whether to build a generic countdown subsystem or a per-mission one.

---

## What I already know (confirmed via existing project research or FFNx source)

### Confirmed runtime addresses (Steam 2013, US, Build with FFNx)

| Address | Type | Meaning |
|---------|------|---------|
| `0x01CFDC5C` | base | Savemap base |
| `0x01CFE9B8` | base | Field-var stack (JSM-script variable block, PSHM_W / POPM_W targets) |
| Savemap header | 76 bytes (0x4C), **not** the 96 bytes (0x60) that some research docs assume |

### Field-var stack layout (from prior PSHM_W research)

- Bytes 0–1023 of the field-var stack are **persistent** game-state variables saved to disk
- Bytes ≥ 1024 are **temporary** per-field variables that reset on field transitions
- Known persistent variables include:
  - Gil at offset 72
  - Story progress at offset 256
  - SeeD rank as var 16 (signed word)
  - Car-rent flag at var 86 (signed byte)
  - Sub-story progression at var 528 (signed word)

The Dollet timer **persists across field transitions and battles** during the chase sequence, which rules out the temp region. It must live either in the persistent region of the field-var stack (offsets 0–1023) **or** in a dedicated engine global outside the field-var stack entirely.

### FFNx-known symbols (from `FFNx-canary/src/ff8_data.cpp` / `ff8.h`)

- `manage_time_engine_sub_569971` — the engine-level time-management hook (resolved as a relative call from WinMain + 0x23 in FFNx). Real-time decrement of the timer plausibly happens in this function or one of its callees.
- `enable_rdtsc_sub_40AA00` — RDTSC-related sub called from manage_time_engine.
- The opcodes FFNx hooks include `opcode_mes`, `opcode_ask`, `opcode_mapjump`, `opcode_pshm_w`, `opcode_popm_w`, `opcode_battle`, etc. — but **no `opcode_timer` or `opcode_stim`** is exposed in the externals struct. Either FFNx doesn't hook it (because it doesn't need to render the timer — FF8 renders it natively), or the timer is engine-driven without a dedicated opcode.

### Field-script opcodes the FF8 community has documented for timer manipulation

Various wiki references mention opcode names like `STIM`, `STMSPEED`, `WAIT_TIMER`, `TIMER`, `TIMERON`, `TIMEROFF` — but the exact opcode numbers and handler addresses for FF8_EN.exe Steam 2013 are not in our existing research docs. Confirming these would let us hook the opcode handler directly as one freeze option.

### Disassembly access

The full FF8_EN.exe disassembly is on disk at `Game Files/disassembly/` split into 8 `.text` `.asm` files by address range, plus a strings file and condensed indexes. Image base is `0x00400000`. The function at `0x00569971` (manage_time_engine) is in `FF8_EN_.text_0x00501000.asm`.

---

## What I need

### 1. The remaining-time variable for the Dollet 30-minute timer

- **Absolute runtime address** (preferred), or relative offset from a documented base such as the field-var stack (`0x1CFE9B8`) or the savemap (`0x1CFDC5C`).
- **Byte size**: uint8 / uint16 / uint32.
- **Units**: seconds remaining (likely starts at 1800), frames remaining at 30 or 60 Hz, ticks, or a split representation (e.g. separate minutes and seconds fields).
- **Signedness**: signed or unsigned.
- **Initial value** at start of the Dollet chase sequence.
- **Behavior at zero**: does the variable underflow, clamp to zero, or get cleared when the time-up event fires?

### 2. The decrement function

- Address of the function (or instruction) that decrements the timer each tick.
- Whether the decrement happens in:
  - The engine time manager (`manage_time_engine_sub_569971` or one of its callees), or
  - A field-script opcode handler (e.g., a `TIMER` or `STIM` opcode dispatcher), or
  - A dedicated countdown tick routine separate from both.
- A short trace of how the decrement reaches the variable would be ideal — useful for choosing between "hook the function" and "rewrite the value each frame" as freeze strategies.

### 3. The Fire Cavern timer (secondary data point)

- Same questions: address, size, units, decrement-function location.
- **Most important question:** is it the same variable / same function as Dollet (generic countdown system), or distinct (per-mission system)?

### 4. The timer-related field opcodes

- Confirmed opcode numbers (in the JSM dispatch table) for `STIM`, `WAIT_TIMER`, `TIMER`, or whatever the canonical opcodes for setting and reading the countdown timer are in FF8_EN.exe.
- Handler addresses in the FF8_EN.exe disassembly.

### 5. The display-rendering path

- Where the timer's display string (e.g. "29:47") is built and drawn each frame. Not strictly required for the read/freeze work, but useful for confirming the variable's units (the renderer's read of the variable will reveal the units).

---

## Savemap header correction (apply if quoting savemap offsets)

Public FF8 savemap research commonly assumes a **96-byte (0x60) header**. The actual header in the Steam 2013 PC version is **76 bytes (0x4C)**. All post-header research offsets are **0x14 too high** — subtract 0x14 from any savemap-relative offset before using it. Savemap base in memory: `0x01CFDC5C`.

This correction does **not** apply to the field-var stack at `0x01CFE9B8` (those are byte offsets within the var block, not savemap offsets).

---

## Sources to prioritize

- **Qhimm forums** — search for posts on the Dollet timer, FF8 countdown system, mission timer, SeeD exam timer.
- **Cheat Engine tables** for FF8 Steam published on speedrun.com, GitHub, or the `ff8-speedruns/ff8-memory` repository. Speedrunners always scan for mission timers — the Dollet address is almost certainly in one of these tables.
- **Makou Reactor** field-script editor documentation — for the `STIM` / `TIMER` opcode definitions and parameter formats.
- **Maki-Chan's FF8 hex notes** (community memory map).
- **Deling / hobbitdur FF8 modding wiki** — opcode reference.
- **FFNx commit history** — any commit that touched timer rendering, countdown, or HUD overlays.
- **Hyne save editor source** (`SaveData.h` etc.) — timer fields in the save file would point to the corresponding savemap memory location.

---

## Output format requested

A short structured summary with:

1. **Dollet timer**: runtime address, size, units, initial value, decrement-function address.
2. **Fire Cavern timer**: same fields, plus a note on whether it shares storage with Dollet.
3. **Timer opcodes**: opcode numbers and handler addresses for STIM / TIMER / WAIT_TIMER family.
4. **Recommended freeze strategy**: based on the decrement-function structure, advise whether hooking the function (with a no-op or pass-through) or overwriting the variable each frame is the cleaner intervention.

Each fact should be **cited** to its source (URL, file path within a publicly available repo, forum thread, or wiki page).
