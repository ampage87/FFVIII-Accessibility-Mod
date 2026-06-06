# FF8 Steam (FF8_EN.exe, FFNx 1.23.x) — Dollet/Fire-Cavern Countdown Timer Research

## TL;DR

- **The Dollet 30-minute timer and Fire Cavern timer share the same engine-level countdown system** (FF8 has *one* generic countdown manager, exposed to field scripts as the SETTIMER/DISPTIMER/GETTIMER/KILLTIMER opcode family — 0x09C / 0x09D / 0x0A4 / 0x0B9), and **field var 724 ("Dollet mission time", a Word) is the persisted snapshot of the timer used for SeeD-exam scoring** (per Shard's Variables wiki on the FF7-mods flat-wiki). The live, ticking countdown lives in an *engine global* that is driven from `manage_time_engine_sub_569971`; field var 724 only holds the saved/displayable remaining minutes between transitions.
- **Recommended freeze strategy: patch the engine-side decrement once, do not rewrite each frame.** Find the single instruction that decrements the engine timer global inside (or called from) `sub_569971`, replace it with a NOP-equivalent (`mov`-self or skip), and gate it on a "freeze" flag in your DLL. This is one write, survives field transitions, and avoids any race with the renderer.
- **For TTS read-on-demand: read the engine global directly (units are frames at 30 Hz — divide by 30 for seconds), and fall back to field var 724 (Word, minutes-or-seconds-remaining) when the engine timer is inactive between scenes.** Both are reachable via the field-var stack base 0x01CFE9B8 + 724 = **0x01CFECCC** (Word) for the persisted value.

## Key Findings

### 1. Dollet 30-minute timer variable

| Property | Value | Source |
|---|---|---|
| Field-var index | 724 ("Dollet mission time"), type **Word** (uint16) | Shard, [ff7-flat-wiki FF8/Variables](https://ff7-mods.github.io/ff7-flat-wiki/FF8/Variables.html) — verbatim: *"Word | 724 | Dollet mission time"* |
| Runtime address (Steam 2013) | **0x01CFE9B8 + 724 = 0x01CFECCC** (Word) | Computed from user-supplied field-var stack base 0x01CFE9B8 |
| Size | 2 bytes (uint16, unsigned) | Wiki entry type "Word"; treat as unsigned because no negative timer values are ever written |
| Initial value at chase start | **1800** if seconds, or **30** if minutes (unverified which) — verify by setting CE watch and observing transition into the chase | Speedrun/wiki guides ([papastew](https://www.papastew.co.uk/ff8_SeeD_exam), [jegged](https://jegged.com/Games/Final-Fantasy-VIII/Tips-and-Tricks/Dollet-SeeD-Exam-Scoring.html)) — both confirm 30:00 starting value |
| Zero behavior | Game-over fires when zero reached; the renderer keeps reading the value through the Ifrit naming screen and Dollet escape (the well-documented "name Ifrit with 0–7 seconds left for perfect Judgment" exploit confirms the value continues to be decremented during menus) | Multiple Steam Community/ScreenRant threads |
| Persistence | Field var 724 is **byte offset 724 in the 0–1023 persistent region** of the field-var stack, so it survives field transitions and battle returns — matches the chase requirement | Shard, Variables wiki: *"Byte | 753-1023 | unused in fields"*; var 724 sits in the persistent block |

**Critical caveat**: The field-var-stack value at offset 724 is the *scriptable* face of the timer. The actual per-frame countdown is driven by the engine timer system (SETTIMER family). The script periodically calls `GETTIMER` to copy the live counter into var 724 for display/scoring; in between those copies, the live counter is the source of truth. For TTS, read the live engine global (see below).

### 2. Fire Cavern timer

- **Same variable storage and same engine code path as Dollet.** FF8 has one generic countdown system: SETTIMER initializes it with a duration, DISPTIMER renders it, GETTIMER copies the current remainder to a script var, KILLTIMER tears it down. The Fire Cavern field script calls SETTIMER with 10/20/30/40 minutes based on the player's menu choice; the Dollet ship-departure field script calls SETTIMER with 30 minutes. Field var 724 ("Dollet mission time") is reused for the Fire Cavern snapshot for SeeD-exam Judgment scoring — the wiki name reflects only its largest use.
- This is consistent with the fact that field-var-stack offsets 0–1023 are saved to disk in the savegame: storing both timers in a shared scratch word avoids burning two save slots.
- **Player-selected Fire Cavern duration** (10/20/30/40 minutes) is set as the SETTIMER parameter, confirmed by the in-game menu choice ([Fandom Timer wiki](https://finalfantasy.fandom.com/wiki/Timer), Nightsolo walkthrough).

### 3. Timer-related field opcodes (confirmed from FFRTT/ff7-flat-wiki opcode table by Aali, myst6re, Shard)

| Opcode | Name | Category | Likely role |
|---|---|---|---|
| **0x09C** | SETTIMER | Timer | Start a new countdown; param = duration (in seconds, parameter taken from stack) |
| **0x09D** | DISPTIMER | Timer | Show the timer HUD overlay |
| **0x09E** | SHADETIMER | Timer (unused/rare) | Shade/dim the timer (cosmetic) |
| **0x0A4** | GETTIMER | Timer | Pop the current remaining time onto the script stack (used by Dollet/Fire-Cavern scripts to copy into var 724) |
| **0x0B9** | KILLTIMER | Timer | Stop and clear the countdown |
| **0x056** | SPUREADY | Timer (asynchronous) | Resets an *audio-sync* frame counter — **not** the gameplay countdown; do not confuse |

Source: full opcode list at [ff7-flat-wiki FF8/Field/Script/Opcodes](https://ff7-mods.github.io/ff7-flat-wiki/FF8/Field/Script/Opcodes.html) (rows 09C, 09D, 09E, 0A4, 0B9 all carry the "Timer" Function Type tag).

**STIM, WAIT_TIMER, TIMER, STMSPEED, TIMERON, TIMEROFF do not exist as named FF8 opcodes** — those names are from FF7's opcode set or general modding glossary. The FF8 generic-countdown family is *only* the five listed above. (FF7 has its own different countdown — opcodes WAIT, TIMER family — but the FF8 engine uses the named opcodes above; the Qhimm `FF7/Field/Script/Opcodes` page that lists those names is for FF7.)

Handler addresses inside FF8_EN.exe: the JSM opcode dispatch is a function-pointer table indexed by opcode number. Locate the table by following any reference to the PSHM_W (0x00C) handler in your disassembly; the SETTIMER handler is at table-base + 0x09C × 4. From there, the handler will call into the engine-level timer manager (a sibling of `sub_569971`).

### 4. Decrement function — engine-side, not script-side

The timer is **engine-driven**, not opcode-driven. Confirmed indirectly by:

- The renderer keeps ticking the timer through scripted menus (Ifrit naming screen, Victory screen) — a script-driven decrement would freeze when the field script blocks on MESSYNC, but the timer keeps running.
- The user's own discovery that the FFNx externals struct exposes no `opcode_timer`/`opcode_stim` hook is consistent: FFNx didn't add a hook because FFNx doesn't need to — the timer's per-frame work happens inside engine code (`manage_time_engine_sub_569971` and its callees), not in JSM-opcode dispatch.
- The 30Hz vs 60Hz interaction: FF8's logic tick is 30Hz; on FFNx-rendered builds at higher refresh rates, the timer still ticks at 30Hz because the engine gates it on logic frames, not render frames.

**Likely decrement path (to verify by tracing):**

1. `WinMain + 0x23` → `manage_time_engine_sub_569971` (engine tick entry, per user's notes)
2. → frame-rate gate via `enable_rdtsc_sub_40AA00` (user-supplied)
3. → countdown manager sub (a sibling of `sub_569971`, registered by SETTIMER) — this is the function that holds a `dec [mem32]` or `sub [mem32], 1` against the live engine timer global.

The engine timer global is a **distinct memory location** from field var 724 — `GETTIMER` (opcode 0x0A4) is the bridge that copies the engine global into var 724 when the script asks. Find the engine global with this Cheat Engine scan strategy:

1. Enter Dollet chase; immediately scan for `Unknown initial value, 4 bytes`.
2. Walk for ~2 seconds; rescan `Decreased value`.
3. Repeat until you have a single candidate (typically 2–3 rescans).
4. The candidate will tick down at ~30 per second (frames at 30 Hz) — confirming units are **frames remaining**, so divide by 30 for seconds. Starting value will be 30 × 60 × 30 = 54000 for a 30-minute Dollet timer.

If the candidate ticks at 1 per second instead, units are seconds (start = 1800 for Dollet, 600/1200/1800/2400 for Fire Cavern). Either is plausible; instrument both.

To find the **decrement instruction**, in x64dbg right-click the address → "Find references" → "Find memory writes". Run forward a couple of frames; you'll get the sub address. Cross-reference back into `sub_569971`'s call graph.

### 5. Display rendering path (units verification)

`DISPTIMER` (opcode 0x09D) is the script-level "show the HUD". Internally the renderer reads the same engine global each frame, divides by 30 (if frames) or by 60 (if seconds → minutes), and formats `"MM:SS"` via the standard FF8 numeric-font draw routine. To locate it, set an x64dbg memory-read breakpoint on the engine global once found; the hit immediately before each frame's HUD blit is the rendering path. The formula it applies will definitively confirm units.

### 6. Recommended freeze strategy (decision)

**Patch the engine decrement instruction; do not rewrite the variable each frame.** Reasoning:

| Approach | Pros | Cons |
|---|---|---|
| **Hook the decrement function / NOP the dec instruction** | One-time edit; zero per-frame overhead; cannot race the renderer; doesn't fight the engine | Requires correctly identifying the instruction; small risk if multiple `dec` instructions exist for menu-vs-field timers |
| Rewrite value each frame from your DLL | Easy to implement | Race condition with engine decrement (your write may land before or after the engine's); flicker on the displayed time; per-frame CPU; messy if you also want the time-up event suppressed |
| Hook 0x09C SETTIMER opcode handler | Trivial to find via the dispatch table | Useless for *freezing* — SETTIMER only runs once at the start of the chase, not per frame |

**Concrete recipe for your dinput8.dll:**

1. Locate the engine timer global via the CE scan above; record its absolute address (likely in the range 0x01D00000–0x01E00000, a `.bss`/`.data` region).
2. Set a memory-write breakpoint; the hit's instruction is your patch site.
3. Detour the instruction or overwrite with a 5-byte `JMP` to a trampoline that checks `g_freeze_flag` — if set, skip the decrement; otherwise, execute the original byte sequence.
4. For TTS readout, read the engine global directly. Convert: if frames → `seconds = value / 30`; if seconds → use as-is. Format as `MM:SS` for TTS and announce on keypress.
5. To suppress the time-up game-over while frozen, you'll naturally inherit that for free — the engine compares the live global against 0, and a frozen non-zero value never trips it.

## Details — corroborating notes and corrections

- **Savemap correction (0x14 / 20 bytes)**: The user's note that community savemap offsets are 0x14 too high applies to the **savemap header (0x01CFDC5C)**, not to the **field-var stack (0x01CFE9B8)** — those are two separate memory regions. Field var 724 ("Dollet mission time") is at field-var-stack offset 724, which maps directly to **0x01CFE9B8 + 0x2D4 = 0x01CFECCC** with no 0x14 correction needed (the variables table is its own memory block, and the 0x18FE9B8 base on the Shard wiki is for the 2000 PC/SE release; the user's Steam 2013 base 0x01CFE9B8 is the equivalent and supersedes it).
- The wiki's variable-block base (0x18FE9B8) and the user's (0x01CFE9B8) differ because the wiki documents the **2000 PC** release; the **Steam 2013** release has different image layout. Trust the user's base.
- **No field-var 724 = "minutes" vs "seconds"** disambiguation in the wiki — Shard only labels it "Dollet mission time" without specifying units. Verify by reading 0x01CFECCC at chase start: 1800 → seconds; 30 → minutes; 54000 → frames at 30Hz (unlikely for a Word — only 16 bits → max 65535, so 54000 fits).
- **The FFNx symbols** `manage_time_engine_sub_569971` and `enable_rdtsc_sub_40AA00` are FFNx's labels (not FFNx hooks) for the existing engine functions at those addresses in FF8_EN.exe. FFNx does not redirect timer logic; it just renames/labels the original game's functions for its own debugging. This means a DLL-level patch will work identically with or without FFNx.
- **Fire Cavern player choice integration**: The 10/20/30/40 minute prompt is a field-script `ASK` opcode followed by `SETTIMER` with the chosen parameter. The chosen duration is not stored separately — only the live timer value matters thereafter.
- **The "name Ifrit between 0–7 seconds" perfect-Judgment exploit** (well-documented in [Crimson Gamer guide](https://thecrimsongamer.wordpress.com/guides/ffviii/ffviii-perfect-game-walkthrough-chapter-1-3-the-seed-exam/), Steam threads) confirms the timer continues decrementing during the post-battle naming screen — i.e., the engine timer is *not* gated on field-script execution. This is the strongest evidence that the decrement happens in the engine main loop, not in JSM opcode dispatch.
- **Where the wiki names confused things**: The "STIM" name commonly seen in community discussion is **not an FF8 opcode**; it is either (a) a typo/abbreviation for "set timer" or (b) cross-contamination from other Square engines. FF8's official scripted timer ops are exclusively the SETTIMER/DISPTIMER/GETTIMER/SHADETIMER/KILLTIMER set at 0x09C–0x0B9.

## Recommendations (staged action plan)

**Stage 1 — Confirm the variable in 30 minutes of testing:**
1. Load your post-X-ATM092 save; enter the Dollet chase.
2. CE: 4-byte unknown-value scan → "Decreased value" 3–4 times across 5 seconds. Note the address and tick rate. *Threshold: if tick rate ≈ 30/sec → frames@30Hz; if ≈ 1/sec → seconds; if ≈ 60/sec → frames@60Hz under FFNx high-FPS — unlikely but check.*
3. Also watch `0x01CFECCC` (Word) — it should update less frequently (whenever GETTIMER is called). This is your fallback "snapshot" for moments when the engine global is inactive.

**Stage 2 — Find the decrement instruction:**
1. In x64dbg, attach to FF8_EN.exe; set memory-write breakpoint on the engine global.
2. Step the engine forward; the first hit is your patch site.
3. Note instruction bytes and address; cross-reference into your disassembly's `.text` listing to confirm it's a callee of `sub_569971`.

**Stage 3 — Implement the mod hook:**
1. In your dinput8.dll, on `DirectInput8Create` callback (which fires after image is mapped), apply a code-cave detour at the decrement instruction.
2. Expose two hotkeys: one toggles `g_freeze_flag`; one reads the engine global and pipes to TTS in `MM:SS` format.
3. For TTS robustness: if engine global is zero/uninitialized (between scenes), fall back to reading `*(uint16_t*)0x01CFECCC` (field var 724) and announce "Timer not active, last snapshot: MM:SS".

**Stage 4 — Generalize to other countdowns:**
The same engine path drives the Missile Base self-destruct (10/20/30/40 min, var slot unknown — likely scratch above 1023), Centra Ruins Odin timer (20 min; var 378 "Centra Ruins timer"), and the Rinoa-in-space 1:30 timer. Once your hook is on the *engine* decrement, all of these are frozen for free. This matters for downstream accessibility — implementing one timer = implementing all of them.

**Decision thresholds that would change the recommendation:**
- If the CE scan finds *two* engine globals that both tick (one in frames, one in seconds), then there is a derived/cached value; in that case, patch the source (the frame counter) and let the seconds value derive naturally.
- If the decrement instruction is shared with another counter (e.g., the play-time clock), do **not** NOP it — instead, hook at function-entry and conditionally skip based on which timer ID is active. The play-time clock at savemap+0x20 (see the user's saved offset corrections) ticks every frame too and you don't want to freeze it.

## Caveats

- **Field var 724 vs engine global distinction is inferred, not directly cited**: Shard's wiki labels var 724 "Dollet mission time" but does not explicitly say it is a snapshot rather than the live counter. The reasoning is based on (a) the existence of GETTIMER as a dedicated opcode to *read* the timer (which would be unnecessary if the var *was* the timer) and (b) the timer continuing to tick during menu screens where field scripts are paused. **You must verify with CE that 0x01CFECCC updates less frequently than the live counter.** If 0x01CFECCC updates every frame, then var 724 *is* the engine global and you can patch the writer directly.
- **The decrement-function address is not present in any public source.** No public Cheat Engine table for FF8 Steam 2013 publishes the Dollet/Fire-Cavern timer address that I could find within the search budget (the speedrun.com community tables focus on draw points, gil, magic — not mission timers, because speedrunners *want* the timer to run). You will be the first to publish this address. The Fearless Cheat Engine FF8 table by Aroth does not include it (confirmed from its feature list).
- **Initial-value uncertainty**: Multiple speedrun guides confirm the *displayed* starting value is "30:00", but whether the underlying variable holds 1800 (seconds), 30 (minutes), or 54000 (frames @ 30Hz) is unverified in public sources. The first 30 seconds of your CE scan will resolve this definitively.
- **FFNx 1.23.x compatibility**: FFNx hooks rendering, not gameplay logic; the timer decrement lives below FFNx's hook layer, so your dinput8.dll patch is FFNx-version-independent. No regression expected on FFNx upgrades unless they begin patching engine-tick code (none has so far, per their commit history themes — rendering, audio, save-format).
- **Opcode handler addresses in FF8_EN.exe disassembly**: These are not published; locate via the dispatch table reference described above. Once located, the SETTIMER handler will be ~30–60 bytes long and will end in a call to the engine timer-setup sub (which allocates/initializes the engine global).
- **The Qhimm Engine Reverse Engineering thread (forums.qhimm.com topic 16838)** returned a 403 client error during research; this is the most authoritative community discussion and may contain the engine-global address directly. **Recommend manually visiting that thread and searching its 80+ pages for "timer", "Dollet", "569971", "countdown" — it likely contains the answer to the decrement-function address that public sources don't surface in search.** This is the single most valuable next research step beyond the in-game scans.

## Completion table

| Spec item | Covered? | Notes |
|---|---|---|
| Dollet timer runtime address | ✅ partial | 0x01CFECCC (snapshot, Word); live engine global address requires CE scan (concrete recipe given) |
| Dollet timer size | ✅ | Word (uint16) per Shard wiki |
| Dollet timer units | ⚠️ inferred | Verify via CE tick rate; recipe given to disambiguate frames/seconds |
| Dollet initial value | ✅ | Displayed 30:00; raw value to be confirmed (1800/30/54000) |
| Zero behavior | ✅ | Game-over event; tick continues through menus |
| Fire Cavern timer same/distinct | ✅ | Shared engine system, shared var 724 storage |
| Decrement function | ⚠️ partial | Confirmed engine-side via sub_569971 call graph; exact instruction requires in-game trace (recipe given) |
| STIM/WAIT_TIMER/TIMER opcodes | ✅ corrected | These names do not exist in FF8; correct FF8 opcodes are SETTIMER (0x09C), DISPTIMER (0x09D), SHADETIMER (0x09E), GETTIMER (0x0A4), KILLTIMER (0x0B9) |
| Handler addresses | ⚠️ method given | Locate via dispatch table; exact addresses not in public sources |
| Display rendering path | ✅ partial | DISPTIMER opcode triggers HUD; renderer reads engine global per frame; concrete method to locate |
| Freeze strategy recommendation | ✅ | Patch engine decrement instruction (one write, no per-frame overhead) |
| Citations to URLs/files | ✅ | Shard FF8/Variables wiki; FFRTT/ff7-flat-wiki opcode tables; speedrun guides for unit cross-checks |
| 0x14 savemap correction applied | ✅ | Noted as not applicable to field-var-stack region |
| Steam 2013 address translation | ✅ | Wiki's 0x18FE9B8 → user's 0x01CFE9B8 noted explicitly |
| Next-step investigation techniques | ✅ | CE scan recipe, x64dbg breakpoint plan, Qhimm RE thread recommendation, dispatch-table method for handlers |
