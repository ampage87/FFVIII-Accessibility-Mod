# Deep Research: FF8 Dollet Escape / X-ATM092 Chase Sequence — Engine Internals for Accessibility Mod

## Executive Context

I am building an accessibility mod for **Final Fantasy VIII (Steam 2013 PC edition, original release, NOT the Remaster)** — App ID 39150, executable `FF8_EN.exe`, used together with the FFNx graphics layer at version v1.23.x. The mod ships as a `dinput8.dll` proxy. Its goal is to make FF8 fully playable by blind players via Windows SAPI text-to-speech, audio descriptions of FMVs, an entity catalog with assisted navigation, battle TTS including a full Scan spell readout system, and persistent settings.

The mod currently delivers all of the following: title-screen TTS, FMV audio descriptions and skip, field-dialog TTS for every dialog opcode (`MES`, `ASK`, `AMES`, `AMESW`, `RAMESW`, `AASK`, plus `show_dialog`-based capture for `MODE_TUTO` / Squall's thoughts), battle TTS with Scan announcement, party-aware field navigation catalog with follower filtering, and persistent INI-based settings.

This research is needed for a long-deferred sequence that the current mod cannot handle: **the Dollet mission's escape and X-ATM092 chase scene.**

## The Chase Scene From a Sighted Player's Perspective

For background, here is the gameplay of the chase scene as a sighted player experiences it:

1. The party (Squall, Zell, and Selphie at this point in the story) reaches the top of the Dollet communications tower, restores its function as part of the SeeD field exam, and then leaves.
2. As the party exits the tower, the boss enemy **X-ATM092** (a spider-like Galbadian war machine, formation enemy) jumps down from the top of the tower. A boss battle begins immediately on field exit.
3. In this first battle, the party damages X-ATM092 until its HP threshold causes it to **collapse** (a scripted state, not a true KO). At that point, the player is prompted to escape the battle (it cannot be defeated outright in normal play).
4. After escaping, the party returns to the field and X-ATM092 is visible on the ground in a collapsed state.
5. The party walks down the mountain. Around a bend, X-ATM092 catches up. **Squall says "Run!"** in a standard `MES` dialog (confirmed standard, not a walk-and-talk segment). **The chase begins.**
6. From this point, X-ATM092 actively chases the player across a sequence of mountain field screens. Whenever it makes contact with the player, another battle begins. The player must again damage it to the collapse threshold and escape.
7. After each defeat during the chase, X-ATM092 collapses for **a very short time** before **getting up and resuming the chase**. This timer/cooldown mechanism is the key engine behavior we need to find and freeze.
8. The chase ends when the party reaches the bottom of the mountain at the beach, where Quistis is waiting with the rescue boat.

There are no random encounters during the descent — the only battles are X-ATM092 contact battles. A skilled sighted speedrunner can avoid most or all of these by knowing X-ATM092's movement patterns and routing around it.

## The Accessibility Problem

A blind player using the mod relies on the F9/F10 navigation catalog to perceive their environment and select an exit gateway. This takes time — bring up the catalog, listen to entries, select, walk to the chosen exit. During this time, X-ATM092 keeps closing distance, triggering battle after battle. After each escape the player is back in the field, has lost time, and has to navigate again from a possibly new position. The result is unwinnable without intervention from the mod.

## Our Two-Mode Design

The user has chosen to be presented with a choice **immediately after Squall says "Run!"** via an in-engine ASK dialog (so the existing dialog TTS hooks announce it for free):

- **Auto-drive mode.** The mod takes over movement and routes the party through every chase field along a pre-cached safe path that avoids all X-ATM092 contact. No battles occur. The player just listens and waits for arrival at the beach.
- **Manual mode.** The player drives the party themselves using the navigation catalog. The mod **freezes the X-ATM092 respawn / get-up timer** for the duration of the chase. This caps the worst case at one battle per field — once X-ATM092 has been defeated on a screen, it stays collapsed until the player crosses to the next field, at which point the next field's chase script naturally re-spawns the threat. This bounds the chase to at most N battles where N = number of chase fields, which is finite and predictable. The mod also pre-selects the forward exit gateway in the navigation catalog on each chase field so the player doesn't have to scroll to find it.

This document is the deep-research prompt covering everything we need to know about engine internals to implement either mode.

## What I Already Know (Mod Tooling and Prior Research)

This list exists so the researcher knows what I have at hand and doesn't waste effort re-deriving it:

- **JSM bytecode format is fully documented.** The mod has a working JSM scanner. Format: 32-bit fixed-width stack-based VM. Bit 31 = 0 → PSHN_L push literal (sign-extended from bit 30). Bit 31 = 1 → opcode in bits 30–16 (15 bits, 0x000–0x183), inline parameter in bits 15–0. Opcode IDs are known for all dialog opcodes (`MES` 0x47, `MESW` 0x46, `AMES` 0x65, `AMESW` 0x64, `ASK` 0x4A, `AASK` 0x6F, `RAMESW` 0x116), field transitions (`MAPJUMP` 0x29, `MAPJUMP3` 0x2A, `DISCJUMP` 0x38, `MAPJUMPO` 0x5C, `WORLDMAPJUMP` 0x10D), trigger lines (`SETLINE` 0x39), entity position (`SET3` 0x1E — note: hooking `SET3` is permanently disabled in the mod due to engine instability), entity scripts (`REQ` 0x14, `REQSW` 0x15, `REQEW` 0x16), memory access (`PSHM_W` 0x07/0x0C, `POPM_W` 0x08/0x0D), tutorial/thoughts (`TUTO` 0x177).
- **Dialog hooking is solid.** The mod hooks `show_dialog(int32_t window_id, uint32_t state, int16_t a3)` (resolved via the chain `sub_4A0880 + 0x33` → `sub_4A0C00 + 0x5F`), the dispatch-table entries for all the dialog opcodes listed above, and `set_window_object` (resolved from `opcode_mes + 0x66`). The `ff8_win_obj` struct is **0x3C bytes**, with `text_data1` at +0x08, `text_data2` at +0x0C, `win_id` at +0x18, `mode1` at +0x1A, `open_close_transition` at +0x1C, `state` at +0x24, ASK question fields at +0x29..0x2C, `field_30` at +0x30, callbacks at +0x34/0x38. The active windows array is at `0x01D2B330` (pWindowsArray). The `field_get_dialog_string` function exists at `0x00530750` but is **fully replaced by FFNx via `replace_function`**, so we cannot hook it directly — we hook `show_dialog` instead and pull text from `win->text_data1` after decoding.
- **Battle hooking is solid.** Battle entry/exit, magic ID, formation, and the battle entity struct (with `BENT_STATUS_RESIST_BASE` offset confirmed) are all hooked. We can pattern-match on enemy IDs in formations.
- **Savemap base = `0x01CFDC5C`. Savemap header = 76 bytes (0x4C), NOT 96 bytes (0x60).** Many community deep-research outputs assume the 96-byte header. **Subtract 0x14 from any post-header offset that assumes a 96-byte header.** Confirmed reliable offsets: characters at +0x48C (8 × 0x98), GFs at +0x4C (16 × 0x44), active party formation at +0xAF0 (`0x01CFE74C`, 4 bytes, charId 0–7 or 0xFF, NOT compacted — solo Squall is `[0xFF, 0x00, 0xFF, 0xFF]`), worldmap struct at +0x125C, `car_rent` flag at `0x01CFEF1A`.
- **FFNx canary v1.23.0.182 source is available locally** at `FFNx-Steam-v1.23.0.182\Source Code\FFNx-canary\src\`. Key files referenced: `ff8_data.cpp` (opcode dispatch resolution, hook installation), `ff8.h` (struct definitions), `voice.cpp` (Echo-S dialog interception including `MODE_TUTO` handling at `ff8_show_dialog`).
- **Field entity model IDs are field-local slot indices, not canonical character IDs.** Empirically confirmed by the v0.14.107/v0.14.108 follower-filter saga — on `bggate_1` with party `[Squall, Zell, Selphie]`, the followers showed up as model 2 and model 4, which would be canonical Irvine and Rinoa. Don't trust canonical model→character maps for filtering by party identity. Use behavioral fingerprints (`modelId 0..9` plus `throughonoff > 0` plus `talkonoff == 0` plus `pushonoff == 0` characterizes a follower) instead. This generalizes: **don't assume canonical entity-index mappings hold per-field; verify empirically.**
- **The mod has prior research on field transitions, INF gateways, JSM JMPFL exit destinations, walkmesh extraction, walkmesh camera transforms, the field navigation catalog, and PSHM_W shared-memory semantics.** Available on request — please assume I have these, but flag if your answer would benefit from cross-referencing them.
- **The auto-drive system has a known limitation.** The mod's existing walkmesh-based auto-drive (used elsewhere in the game) hits a camera-relative steering problem: analog stick input is interpreted relative to camera orientation, which varies per field, so a world-space delta-to-target maps to the wrong screen-space direction on camera-rotated fields. This is unsolved. For chase auto-drive, this means we may need an alternative position-control approach (direct position override, scripted analog-stick recordings calibrated per field, etc.) rather than reactive walkmesh steering.

## Constraints to Bake Into the Answer

When the researcher writes their answer, please respect these:

1. **Scope = Steam 2013 PC, FF8_EN.exe, FFNx v1.23.x.** Not the Remaster, not the PSX original. If a finding is PSX-only or Remaster-only, flag it explicitly and indicate whether the same mechanism survives in the PC original.
2. **All savemap offsets given by the researcher must be verified against the 76-byte (0x4C) header convention.** If a community source assumes a 96-byte (0x60) header, subtract 0x14 from cited offsets and explicitly note the correction was applied.
3. **JSM opcode IDs must use the bit-31 encoding (opcode in bits 30–16).** The Qhimm wiki uses this scheme. Some older docs and the Qhimm Modding Wiki fandom mirror use a different "high-byte" encoding — please normalize all reported opcode IDs to the bit-31 scheme.
4. **Don't assume canonical entity indices hold across fields.** When discussing X-ATM092's entity scripts, refer to the entity by behavioral fingerprint (which entity in a given field is the moving threat that triggers battles on contact) and provide field-by-field entity indices verified for that specific field. Generalizations like "X-ATM092 is always entity 4" should be treated with skepticism unless empirically verified.

## Research Questions

These are organized in priority order. Manual mode (questions 1–4 plus 6) is the primary deliverable; auto-drive (question 5) is a stretch goal that depends on results.

### 1. Chase field sequence (must-have)

**1.1.** What are the **field IDs** (numeric) and **field names** (the short names used in the game's field archive, like `bghall_1` for Balamb Garden hall 1) of the Dollet mountain chase fields, in **descent order from the comms tower exit to the beach landing where Quistis waits with the boat**? Please give the complete linear sequence of fields the player traverses during the chase. Include any fields that immediately precede or follow the chase if relevant (e.g., the field where X-ATM092 first jumps down, the beach field where the chase ends).

**1.2.** For each chase field, what is the **forward exit gateway** — the trigger line, edge transition, or `MAPJUMP` opcode that progresses the chase versus dead-ends or sends the player back uphill? If a field has only one logical forward exit, say so. If a field has multiple exits and only one is correct, identify which one and why. If exits are gated by walkmesh edge crossings rather than discrete entities, describe the geometric region the player needs to reach.

**1.3.** Which field marks **chase end** — the first field after which X-ATM092 no longer chases, where the chase script has clearly concluded? Is the end signaled by entering a specific field, by a specific dialog event, by a specific scripted cutscene, or by a savemap variable change?

### 2. X-ATM092 entity behavior and respawn timer (must-have)

**2.1.** Across the chase fields, **which JSM entity is X-ATM092**? Provide field-by-field entity indices (within the entity group table) and the entity's name/label if it has one in the JSM. Note: FF8 reuses entity slots per-field, so the index may differ from one field to the next.

**2.2.** What opcodes drive **X-ATM092's position update** during the chase? Is its movement implemented via opcodes called from its own entity's `main` method, via opcodes called from a director/controller entity, via a `REQ`/`REQSW`/`REQEW` cross-entity invocation, via a hardcoded engine code path, or some combination? If JSM-driven, list the opcode IDs and the rough script structure (loop body, conditionals, external state reads).

**2.3.** What triggers a **contact battle** — is it a `SETLINE` trigger line representing the contact zone, an explicit `BATTLE` opcode (0x69) called from X-ATM092's script when it reaches the player's tile, an engine-level proximity check, or something else? When X-ATM092 contacts the player, what's the formation ID of the battle that begins? Is it the same formation across the chase or different per-encounter?

**2.4.** **The respawn timer — this is the most important question.** After a contact battle is escaped, the field re-loads with X-ATM092 in a collapsed state. What mechanism causes it to **rise and resume the chase**? Possibilities to investigate:
- A field countdown opcode (something like `WAIT` or a tick-based timer in JSM script).
- A frame counter in a field-temporary memory variable, decremented each tick by an engine update or by the entity's own `main` script.
- An animation-state machine where the "rise" transition happens after the "collapsed" animation completes, with the animation duration being the effective timer.
- An AI state machine value the engine flips after a hardcoded duration.
- A savemap-variable countdown.

For the actual mechanism (not just speculation), please report:
- Where the duration value lives — script literal pushed by `PSHN_L`, savemap variable address, hardcoded engine constant, animation file frame count, or other.
- The exact value of the duration in frames or ticks (the game runs at ~30 fps for field code).
- A specific freeze-strategy recommendation: which memory write, which opcode invocation, or which engine-level update can the mod intercept to keep X-ATM092 in the collapsed state indefinitely without crashing the script or breaking the chase-end condition.

**2.5.** Is there a **chase-active flag** somewhere — a field-temporary variable, savemap byte, or engine global — that's set when the chase starts and cleared when the chase ends? If so, where? This would let the mod scope its respawn-timer freeze cleanly to the chase only.

### 3. Engine-level ASK invocation (must-have)

We want to display an in-engine ASK dialog ourselves at the moment Squall says "Run!" — our own question text, our own option strings — and read back the player's selection. The existing `show_dialog`-based dialog TTS hooks would announce the prompt automatically because they fire at the rendering layer. But we don't yet know how to invoke an ASK from outside the script VM.

**3.1.** What **engine function does the `opcode_ask` (0x4A) dispatch handler ultimately call** to render an ASK window and present options to the player? What's its address in `FF8_EN.exe`, its calling convention (`__cdecl` / `__stdcall` / `__fastcall` / `__thiscall`), and its full prototype?

**3.2.** What are the **prerequisites for calling that function** — does it require a pre-existing window object slot, a specific game mode (`MODE_FIELD`)? Can it be called outside the script VM (i.e., from our `dinput8.dll` hooks during a frame update), or does it depend on script-VM state (current entity, current method, instruction pointer) that only makes sense mid-script?

**3.3.** **How is the player's selection returned?** Possibilities:
- Synchronous return value from the function call.
- Stored in `field_dialog_current_choice` (referenced in FFNx's `ff8_externals` struct) — what's its address and how does it relate to ASK results?
- Stored in a savemap byte or field-temporary variable.
- Communicated via callback (the `ff8_win_obj` struct has `callback1`/`callback2` at the end — are these used for ASK option selection?).

What's the polling pattern — does the mod's frame update need to spin on a "selection complete" flag, or is there an event/callback we can subscribe to?

**3.4.** Where do the **question text and option strings** live during an ASK? In the JSM, `ASK` consumes message IDs that index into the field's message archive. If we want to inject our own strings, can we write them into a buffer the ASK function reads from? Can we provide the strings via pointer? Is there a sane way to inject strings into a transient ASK without modifying the field's `.msg`/`.fs`/`.fl`/`.fi` archive?

**3.5.** **Alternative: script-injection approach.** If direct invocation from outside the script VM is impractical, can we instead inject an ASK opcode into the running script flow at the moment Squall's "Run!" dialog finishes? This would mean either patching the JSM bytecode of the relevant chase-start field at load time, or manipulating script-VM state at runtime (instruction pointer, stack) to detour through our ASK injection. Pros and cons of this approach versus direct invocation.

**3.6.** **Are there community examples** — Echo-S, the Tonberry/Roses-and-Wine HD mods, FFNx itself, OpenVIII — of code that invokes an ASK or ASK-equivalent dialog from outside the FF8 script VM? Even a partial example would shortcut substantial reverse-engineering. Please cite specific files and line numbers if found.

### 4. Chase-start trigger detection (must-have)

**4.1.** The "Run!" dialog at the start of the chase — what's its **specific field, message ID, and opcode**? `MES` (`0x47`) or `AMES` (`0x65`)? What field does it occur in (likely the first chase field after the post-collapse cutscene)?

**4.2.** What **other dialog events occur immediately before and after** the "Run!" dialog? We need an unambiguous signature so the mod's `show_dialog` hook can detect "this is the chase-start moment" and trigger the ASK prompt at the right time.

**4.3.** Is there a **savemap variable, field-temporary variable, or game-state flag** that's set right when the chase begins, that we could check on every frame as a more reliable trigger than dialog matching?

### 5. Auto-drive route research (stretch goal)

If manual mode ships and we want to add auto-drive, we need a safe path through each chase field that avoids all X-ATM092 contact.

**5.1.** What are **X-ATM092's documented movement patterns** during the chase, per field? Speedrun community knowledge: which screens have X-ATM092 enter from a specific edge, which screens have it on a fixed patrol, which screens require timing? Please cite speedrun guides (any% / 100% routes for the Dollet escape) and community route notes if they exist.

**5.2.** Is X-ATM092's pathing **deterministic** (same every run) or **player-position-reactive** (it tracks the player)? If reactive, does it respect walkmesh boundaries or move in straight lines through the screen?

**5.3.** What **scripted player movement mechanisms** exist in the engine? Possibilities:
- A direct-position-override opcode or function (set party position to specific X/Y/Z).
- A "force walk" opcode that takes target coordinates and handles motion.
- A way to inject pre-recorded analog stick values frame-by-frame (TAS-style playback) and have them interpreted correctly per-field-camera.

The mod's existing reactive walkmesh auto-drive has a camera-relative steering bug, so a **direct-position-override or scripted-walk approach** is preferred here over reactive steering.

**5.4.** Are there **example fields elsewhere in FF8** where the engine drives the player on a scripted path (cutscene movement, the train sequence, the SeeD ball dance, the Galbadia parade)? If so, what's the mechanism? It may be reusable for chase auto-drive.

### 6. Disassembly and code-pointer specifics (must-have if available)

For all of the above, please provide as many of the following as you can:

- **`FF8_EN.exe` addresses** (RVA or absolute, both fine — the mod handles ASLR-free original PC builds with a known image base) of any engine functions referenced.
- **JSM opcode IDs** in the bit-31 encoding scheme.
- **Savemap variable offsets** with the 76-byte-header correction applied.
- **FFNx source file paths and approximate line numbers** for any cross-references.
- **Specific JSM file names** (`.jsm` archive entries) for the chase fields, plus the entity group structure for each.

## Format of Expected Answer

Please structure the answer as:

1. **Executive summary** — one paragraph per major question covering the headline finding.
2. **Section per major question** with:
   - The findings (with confidence level — **confirmed by source X**, **inferred from Y**, **speculative**).
   - The supporting evidence (file, line number, address, opcode trace, etc.).
   - Any caveats specific to PC vs. PSX vs. Remaster.
3. **Code/data tables** where appropriate (chase field sequence as a table, opcode IDs as a table, savemap offsets as a table).
4. **A consolidated implementation guidance section at the end** — given the findings, the recommended code-level approach for each of: (a) detecting chase start, (b) invoking the ASK, (c) reading the player's selection, (d) freezing the respawn timer in manual mode, (e) safe-pathing in auto-drive mode.
5. **An "unresolved questions" section** flagging anything we should plan to determine empirically via runtime diagnostic logging in the mod.

Please be **explicit about uncertainty**. If a finding rests on FFNx behavior plus inference about the underlying engine, say so. If a value is a guess based on similar boss patterns elsewhere in FF8, say so. The mod's primary tester is the developer (sole developer, blind, NVDA user, primary author) — false confidence in research findings costs us a build cycle and a save-and-reload, which is expensive.

## Reference Sources

Strong candidates for primary investigation, in rough priority order:

- **FFNx canary source** (GitHub: julianxhokaxhiu/FFNx) — `src/ff8_data.cpp`, `src/ff8.h`, `src/voice.cpp`, `src/ff8/field/*`, `src/ff8/battle/*`. The mod has a local copy of v1.23.0.182.
- **myst6re's Deling** (GitHub: myst6re/deling) — FF8 field script editor; has the most complete JSM opcode decoder in any open-source tool.
- **Qhimm Wiki** (wiki.ffrtt.ru) — `FF8/Field/Script/Opcodes`, `FF8/FileFormat`, `FF8/Variables`.
- **Qhimm Modding Wiki / fandom** (finalfantasy.fandom.com / forums.qhimm.com) — community threads on FF8 reverse engineering; search for "X-ATM092", "Dollet escape", "chase scene".
- **FF8 speedrun research** (GitHub: ff8-speedruns/ff8-memory) — memory address tables.
- **OpenVIII** (GitHub) — C# FF8 data parser; has independent JSM/field decoders for cross-validation.
- **Ifrit, Hyne, Cactuar** — additional FF8 field/save tools.
- **nightsolo.net/games/ff8/** — community walkthrough and enemy data.
- **Echo-S 8 / Tsunamods** — voice mod that depends on FFNx; may have ASK-related hook examples.
- **FF8 Any% / 100% speedrun guides** — for X-ATM092 chase routing patterns (question 5).

## Final Note for the Researcher

The chase has been a long-deferred priority for this mod precisely because it sits at the intersection of three engine subsystems we haven't yet had to deeply understand: scripted-NPC chase behavior, cross-field state continuity, and direct dialog-system invocation. A thorough answer here unlocks not just the Dollet chase but every future scripted timed-pursuit sequence in the game (there are several). Please err toward over-thoroughness — extra detail will be put to use.
