# Deep Research Prompt v2: FF8 Battle Floating-Sprite Render Path

**For use with ChatGPT Deep Research.** Paste the entire prompt below (from the horizontal rule down). This is a tighter, more-constrained reissue of v1 — v1 came back with a tangential explainer of FFNx's `effects.h` rather than the function address we need.

---

## The single question

In **Final Fantasy VIII Steam 2013 (FF8_EN.exe, app ID 39150)**, what is the **exact function address** (or a tight set of candidate addresses with ranked confidence) of the function that **creates/spawns/initializes** the floating battle sprites — damage numbers, "Miss" text, "No Effect" text, status icons — that appear above battlers during combat and animate upward/fade out?

I need a **function address in the 0x00400000–0x00B00000 range of FF8_EN.exe**. Not a general description of the battle engine. Not a summary of what a related function does. Not an FFNx-side abstraction. The **native FF8 function that writes the sprite into the battle sprite table or display list**.

## Constraints on your answer

1. **One line up top: `ANSWER: 0xXXXXXXXX — <one-sentence why>`.** If you can't commit to one, give up to three ranked candidates in the same format.
2. **Every candidate must be verifiable.** Cite: the source (FFNx `ff8_data.cpp`, Qhimm forum thread URL, MaKiPL/OpenVIII commit, IDA-style public disassembly), the byte pattern or sibling function it's identified by, and what FFNx uses it for if anything.
3. **If two sources disagree on an address, say so explicitly** and rank them.
4. **Do NOT spend paragraphs re-explaining what I've already ruled out.** The ruled-out list below is ruled out. Explaining `sub_4877F0`, `sub_487DF0`, or `sub_48D200` again is not useful.
5. **Do NOT answer with an FFNx C++ abstraction** (e.g. `ff8_externals.something`). I need the native FF8_EN.exe function those abstractions resolve to via `get_relative_call` / `get_absolute_value`. Walk the resolution pattern in `ff8_data.cpp` and give me the final native address.
6. **If the answer is "it's an opcode handler inside a bytecode VM"**, give the **opcode table address, the opcode byte, and the handler function address.** Say "opcode 0x23 at table 0x0048A0B8, handler at 0x004889C0", not "it's one of the opcode handlers in the sub_487DF0 VM."

## Context (read once, don't re-summarize in your answer)

I'm building an accessibility mod for blind players (a MinHook-based `dinput8.dll` shim). I need to hook the actual sprite-spawn path so the mod can announce Miss / damage / status results in sync with what sighted players see — not 200–2800ms ahead, which is where my current `sub_4877F0` dispatcher hook fires.

### Three sprite categories I need covered (same function or sibling functions is fine)

- **Floating damage numbers** — "77", "158", etc., world-space billboard over the battler
- **Floating result text** — "Miss", "No Effect", "Immune", "Reflect", "Absorb"
- **Small status icons** — sleep Zs, silence symbols, poison indicators

These are NOT the UI command menu, NOT the victory screen text, NOT the action-announce icon next to the active character. They're specifically the feedback sprites that animate upward and fade.

### What I have already conclusively ruled out via live testing

| Function | Address | Why ruled out |
|---|---|---|
| `sub_4877F0` | 0x004877F0 | Spell/attack **result dispatcher**. 9-case jump table at 0x487D58. Fires at result-decision time, doesn't itself create sprites. Timing is 200–2800ms ahead of sprite visibility. |
| `sub_487DF0` | 0x00487DF0 | Bytecode VM called by kind=4 of sub_4877F0. Opcode dispatch at 0x0048A0B8, ~61 entries. Statically contains a call to sub_48D200 at 0x004881D3 — that call site **never fires in live testing**, verified via `_ReturnAddress()` capture in the sub_48D200 hook. |
| `sub_48D200` | 0x0048D200 | Initially suspected as the popup dispatcher. In practice only handles **action-announce popups** (the icon next to the *active* character showing the pending action). Live observed sole caller retaddr: **0x00485938**. No floating damage/Miss/status sprites route through it. |
| `sub_47EC70` | 0x0047EC70 | Battle text-ID resolver for **victory screen / UI**. Wrong visual style. |
| `sub_483400` | 0x00483400 | Sprite spawner for **item pickup** popups. Not the battle floating-result path. |

### Empirical timing evidence

A byte flag at **0x01D280C0** — I call it `BATTLE_DAMAGE_ANIM_FLAG` — transitions `0→1` when a result animation starts and `1→0` when it ends. Its lifetime correlates **1:1 with the visible sprite's lifetime**:

- Strike Raid limit-break hit: flag set, cleared after **266 ms**. OpenGL `glReadPixels` screenshot at 400 ms post-dispatch caught "77" clearly on screen.
- Quistis physical attack: flag set, cleared after **2875 ms**. 400 ms screenshot had nothing; "158" appeared near end of flag window.
- Sleep cast on sleeping target (resist): flag set, cleared after **~8 s**. No floating text visible in 400 ms screenshot.

Whatever function spawns the sprite **is almost certainly called around the 0→1 transition of 0x01D280C0**, or it writes that flag itself.

**High-value lead for you:** find the callers and writers of **0x01D280C0**. Every function in FF8_EN.exe that does `MOV [0x01D280C0], 1` or `MOV BYTE PTR ds:0x01D280C0, ...` is a candidate. Rank by proximity to sprite allocation.

### Other potentially-relevant addresses in FF8_EN.exe

| Address | Purpose |
|---|---|
| 0x01D27B8C | Battler struct array base, 208-byte stride × 7 |
| 0x01D27B00 | Active battler index (0xFF = none) |
| 0x01D99A68 | Current battle magic ID |
| 0x00C81774 | Battle effect dispatch table (from FFNx `effects.h`, GFs/magic main routines) |
| 0x00B64C3C | Leviathan per-effect opcode table (reference for the VM pattern) |

The battle effect dispatch at 0xC81774 is for **magic/GF animation playback** (Leviathan water, Quezacotl lightning, etc.), not for floating result text — but the VM-pattern it demonstrates may be reused for result-popup spawning. If the floating-sprite path turns out to be an opcode handler inside some VM, tell me which VM's opcode table.

## Sources to mine in priority order

1. **FFNx source — `julianxhokaxhiu/FFNx`**, especially `src/ff8_data.cpp`, `src/ff8/battle/*.cpp/h`, and `src/ff8/texture_packer*` (texture-override hooks may catch the sprite texture upload). Look for any symbol named `damage`, `popup`, `sprite`, `battle_text`, `result_text`, `miss`, `number`, `billboard`, `world_text`, `floating_text`.
2. **Qhimm forum** — `forums.qhimm.com` — search for "ff8 damage sprite", "ff8 miss popup", "ff8 battle number", "ff8 floating text".
3. **MaKiPL's GitHub** — `MaKiPL/OpenVIII-monogame` and predecessor repos. Also MaKiPL's Qhimm posts listing reversed function addresses.
4. **DLPB / Hyne project** — `DLPB2/Hyne` on GitHub, Qhimm old threads with IDB dumps.
5. **FF8 Hext patches** documented on Qhimm — they name specific function addresses.
6. **IDA / Ghidra public databases** for FF8 Steam 2013 — some mod communities share annotated IDBs. The function we need is probably named something like `spawn_battle_popup_sprite`, `battle_damage_text_init`, or `init_floating_number`.

## Specific empirical tests to suggest back to me

If you're unsure between two candidates, propose one concrete BAT I can run (e.g. "hook 0x00488A20 and log retaddr on entry; cast Fire on a single enemy and record the log line during the 266 ms flag window"). Don't invent facts — just help me narrow the candidate set.

## Output format

```
ANSWER: 0xXXXXXXXX — <one sentence>

Confidence: HIGH / MEDIUM / LOW

Candidates (ranked):
1. 0xXXXXXXXX — <one sentence> — source: <URL or file:line>
2. 0xXXXXXXXX — <one sentence> — source: <URL or file:line>
3. 0xXXXXXXXX — <one sentence> — source: <URL or file:line>

Supporting evidence:
- <bullet>
- <bullet>

Writers of 0x01D280C0 (if found):
- 0xXXXXXXXX — MOV [0x01D280C0], 1 at <offset within function>
- ...

Open questions / what you couldn't verify:
- <bullet>

Suggested BAT to disambiguate:
- <one concrete test I can run>
```

Keep the whole answer **under 1500 words**. Prose-heavy background explainers are not useful here — I want function addresses and sources.
