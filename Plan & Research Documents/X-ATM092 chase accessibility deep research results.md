# Final Fantasy VIII (Steam 2013, FF8_EN.exe + FFNx 1.23.x) — Dollet Chase / X-ATM092 Engine Internals: Research Report for Accessibility Mod

> Scope: Steam 2013 PC build only. PSX-only and Remaster-only behavior is flagged. Savemap offsets are normalized to the 76-byte (0x4C) header convention. JSM opcode IDs are normalized to the bit-31 encoding scheme used by Qhimm/Deling. Confidence is labeled per claim: **CONFIRMED** (primary source quoted), **INFERRED** (derived from opcode/architecture knowledge plus a confirmed adjacent fact), **UNVERIFIED** (could not be confirmed from available primary sources within the research budget — flagged as a runtime-diagnostic task).

---

## Executive Summary (per major question)

**Q1 — Chase field sequence.** The chase descends from the Communications Tower exit through a fixed sequence of mountain/bridge/town/beach fields ending at Lapin Beach. The engine-level "place ID" (savemap variable 84, "Last area visited") for the chase area transitions through Comm Tower (99) → Mountain Hideout (100) → Town Square (93) → Lapin Beach (94) (CONFIRMED from `ff8-speedruns/ff8-memory/locationId.md`). The internal short field names inside `field.fs` (the 4-8 char strings like `bdin*`, `bdtw*`) could not be confirmed against a primary source within the research budget — they must be read directly with Deling against the player's installation. Chase end is the transition from the bridge/town-square chain into the Lapin Beach field, where an FMV plays and X-ATM092 is destroyed by Quistis.

**Q2 — X-ATM092 entity and respawn timer (most critical).** X-ATM092's field entity is named **`kani`** (CONFIRMED — Fandom: *"X-ATM092's field entity reference in the game's data is kani from the Japanese word for crab (蟹, kani?)."*). Its slot index is **NOT canonical across fields** and must be discovered per-field via Deling. The chase progress is gated by a savemap state-machine variable: **byte var 530, the "Dollet state" bitmap** (CONFIRMED — Qhimm `FF8/Variables`: *"Byte | 530 | Dollet state (+2 crossed bridge, +4 elvoret finished, +16 xatm first knock out, +32 selphie waiting near tower, +18 cliff jump, -18 Enter tower, +192 enter pub to dodge xatm)"*). The post-knockdown rise is INFERRED to be a per-field script sequence of `BASEANIME`/`ANIME` (collapse) → `WAIT N` (opcode 0x3C) → `ANIME` (rise) → resume `MOVE` chase, with var-530 bits gating the kani entity's main loop. Field code runs at 30 fps, so any literal pushed via `PSHN_L` ahead of `WAIT` is N/30 seconds. The exact `WAIT` literal must be read from the kani entity's main method per chase field.

**Q3 — Engine-level ASK invocation.** `opcode_ask` is `execute_opcode_table[0x4A]` (CONFIRMED in FFNx `src/ff8_data.cpp`: *"ff8_externals.opcode_ask = common_externals.execute_opcode_table[0x4A];"*). The opcode table itself is resolved in FFNx via `common_externals.execute_opcode_table = (uint32_t*)get_absolute_value(common_externals.update_field_entities, 0x65A);` — so there is no hard-coded absolute EXE address in the open-source layer; the address must be resolved dynamically from the running process. The handler is **__cdecl**, takes the script-VM context implicitly (it pulls operands from the script stack), and pushes the user's selection back to the script. Practically, calling the engine `opcode_ask` directly from a dinput8.dll frame hook is fragile because it depends on script-VM state; the recommended approach is **script injection or proxy-window** via the existing `show_dialog` hook the mod already uses.

**Q4 — Chase-start trigger.** Chase begins in the Communications Tower exit area immediately after the post-collapse cutscene, signaled by Squall's "Run!" (or, in many script paths, Zell's "Let's get the hell outta here!" — the in-engine MES whose contents you can match in your existing show_dialog hook). The unambiguous engine-level signal is the bit transition in **savemap var 530**: bit `+16` ("xatm first knock out") flips on at Elvoret completion + first XATM knockdown. A frame-tick test of `(savemap[var530] & 0x10) != 0 && current_field_id ∈ {chase fields}` is more reliable than a string match on the dialog.

**Q5 — Auto-drive (stretch).** The engine offers `SET3` (0x1E, teleport-to-walkmesh-triangle), `MOVE` family (0x3E–0x42, 0x78), `MOVESYNC` (0x7D), `MOVECANCEL` (0x12B), `MOVEFLUSH` (0x148), `FOLLOWOFF/FOLLOWON` (0xAB/0xAC), and `SPLIT/JOIN` (0xE6/0x93) for cinematic motion. There is **no documented direct-position-override opcode that bypasses both the walkmesh and the move pipeline**; cinematic sequences use SET3-init + scripted MOVE/MOVEA chains.

**Q6 — Disassembly pointers.** Most addresses must be resolved dynamically through FFNx-style `get_absolute_value`/`get_relative_call` from a small set of anchor symbols (`update_field_entities`, `field_main_loop`, `pubintro_main_loop`, `opcode_effectplay2`). FFNx exposes the canonical anchor pattern.

---

## Q1 — Chase Field Sequence

### 1.1 Field IDs and short names — descent order

| # | Engine "place" ID (var 84 SETPLACE; CONFIRMED `ff8-speedruns/ff8-memory/locationId.md`) | Friendly description | Internal short name (in `field.fs` / `field.fl`) | Confidence |
|---|---|---|---|---|
| Pre-chase | 99 | Dollet — Comm Tower (exit screen where X-ATM092 first jumps down) | UNVERIFIED — read with Deling | CONFIRMED ID, UNVERIFIED short name |
| Chase 1 | 100 | Dollet — Mountain Hideout (mountain bend / cliff "where Selphie jumped") | UNVERIFIED | CONFIRMED ID, UNVERIFIED short name |
| Chase 2..n | (inside place 100 / 93) | Mountain trail screens → bridge | UNVERIFIED — multiple fields share place ID | UNVERIFIED |
| Chase end−1 | 93 | Dollet — Town Square (dog rescue, square dash) | UNVERIFIED | CONFIRMED ID, UNVERIFIED short name |
| Chase end | 94 | Dollet — Lapin Beach (FMV; Quistis with machine gun) | UNVERIFIED | CONFIRMED ID, UNVERIFIED short name |

> **Caveat — place ID vs. field ID**: variable 84 ("Last area visited") tracks the **place** family — a place can contain multiple fields. The engine's per-field numeric index (used by `MAPJUMP` 0x29 and stored in `common_externals.previous_field_id`) is a different table. The mapping from place→fields is not in any source I retrieved as a clean list and must be enumerated by walking through fields in Deling and watching var 84.

> **Action item for the mod (cheap to do at runtime)**: in your dinput8.dll hook, log `*common_externals.current_field_name` (a `char*` resolved by FFNx via `get_absolute_value(ff8_externals.opcode_effectplay2, 0x75)`) on every field transition. This will give you the canonical short names for every screen visited during the chase, removing the UNVERIFIED gap above with one playthrough.

### 1.2 Forward exit gateway

INFERRED from architecture: each chase field has one or more `INF`-archive gateways (entry/exit polygons; format documented at Qhimm `[INF] Field Gateways`) plus inline `MAPJUMP` (0x29) / `MAPJUMP3` (0x2A) / `MAPJUMPO` (0x5C) opcode calls inside entity scripts. The "forward" exit on each chase screen is the gateway whose `MAPJUMP` parameter points to the next field in the descent chain. The mod's pre-select-forward-exit feature in manual mode should:

1. On field load (hook frame 0 of new field), enumerate the field's `INF` gateways.
2. Identify each gateway's destination field ID.
3. The forward gateway is the one whose destination matches the next chase field in your runtime-discovered list.
4. Stretch goal: gently nudge the analog-stick virtual input toward that gateway's center coordinate.

### 1.3 Chase end signal

CONFIRMED-INFERRED: chase end is signaled when var 530 transitions to a state with the "reached beach" / completed-chase bit set (the Qhimm wiki var-530 description does not enumerate every bit but does enumerate +192 for "enter pub to dodge xatm", which is one of two terminal states). The other terminal state is reaching Lapin Beach (place ID 94), which triggers the destruction FMV. Practical detector: `(prev_field != 94) && (current_field == 94)` — i.e., the field-transition into the beach screen.

---

## Q2 — X-ATM092 Entity Behavior and Respawn Timer (MOST IMPORTANT)

### 2.1 Entity identity

CONFIRMED. Internal entity name: **`kani`** (Final Fantasy Wiki: *"X-ATM092's field entity reference in the game's data is kani from the Japanese word for crab (蟹, kani?). A sign in Insomnia in Final Fantasy XV reads 'Seven Flash X-ATM', a small reference to X-ATM092."*)

UNVERIFIED: the entity slot index in each chase field's entity group table. It is **not** canonical and must be discovered via Deling per field. Practical procedure for the mod author:

- Open each chase field in Deling.
- Look for an entity labeled `kani` (label visible in Deling's entity tree).
- Record its zero-based group index.
- Ship those indices in a per-field config table in the mod.

### 2.2 What drives X-ATM092's position update

INFERRED from FF8 architecture (the Qhimm opcode reference and the FFRTT wiki):
- The kani's main method runs as one of the field-entity scripts under the engine's `update_field_entities` loop.
- Position updates are driven by `MOVE` (0x3E) / `MOVEA` (0x3F) / `CMOVE` (0x41) / `FMOVE` (0x42) calls inside the kani entity's main loop, optionally invoked from a director entity via `REQ` (0x14) / `REQSW` (0x15) / `REQEW` (0x16).
- Animation state changes use `BASEANIME` (0x2C), `ANIME` (0x2D), `ANIMEKEEP` (0x2E), `RANIMELOOP` (0x35) and synchronize with `ANIMESYNC` (0x44) / `ANIMESTOP` (0x45).

### 2.3 Contact-battle trigger

CONFIRMED — `SETLINE` (opcode **0x39**, "Sets the bounds of this line object (for its touchOn, touchOff, and across scripts). Lines are actually 3d hitboxes, not lines." — FFRTT `039_SETLINE`).

INFERRED chain: the kani entity carries a SETLINE 3-D hitbox that follows it. When the player crosses, the kani entity's `touchOn`/`across` script slot fires and calls `BATTLE` (opcode **0x69**, `ff8_externals.opcode_battle = common_externals.execute_opcode_table[0x69]` — CONFIRMED in FFNx `src/ff8_data.cpp`). The formation ID is the literal (`PSHN_L`) pushed before the `BATTLE` call.

UNVERIFIED: are chase formation IDs identical across all chase screens, or is each field's BATTLE call pushed with a unique formation? Almost certainly the same X-ATM092-only formation across all chase contacts (the boss data only defines one X-ATM092 chase formation), but this should be confirmed by reading the kani touchOn handler in any one chase field.

### 2.4 The respawn / rise timer — the central question

**CONFIRMED — chase progress lives in savemap byte 530**, the "Dollet state" bitmap (Qhimm `FF8/Variables`: *"Byte | 530 | Dollet state (+2 crossed bridge, +4 elvoret finished, +16 xatm first knock out, +32 selphie waiting near tower, +18 cliff jump, -18 Enter tower, +192 enter pub to dodge xatm)"*).

**76-byte-header normalization**: the save block starts at `0xD10` in the uncompressed PC save with a 96-byte-header convention (Qhimm: *"the save block starting at address 0xD10 on uncompressed PC saves... The varblock begins at 0x18fe9b8"* in en-US Steam/SE versions). Under your 76-byte (0x4C) convention, with savemap base `0x01CFDC5C`, the variable area's `var 530` lives at:
```
addr(var530) = 0x01CFDC5C + 0xD10 + 530 = 0x01CFDC5C + 0xF22 = 0x01CFEB7E
```
(Qhimm: *"In the en-US version of the original and SE releases (and likely most other versions), the varblock begins at 0x18fe9b8."* — this is the older `ff8.exe` build's address; for Steam 2013 the offset relative to your savemap base 0x01CFDC5C is what matters.)

**INFERRED — rise mechanism**: the per-field kani main loop is structured roughly as:

```
LABEL_START
    PSHM_B   var530
    PSHN_L   0x10           ; "first knockout" bit
    CAL      AND
    JPF      LABEL_NORMAL_PATROL    ; if not yet knocked out, normal patrol/lurk
    ; --- post-knockdown branch ---
    BASEANIME <collapsed_anim>      ; or ANIME with the collapsed pose
    PSHN_L   <duration_frames>      ; ← THE RISE-TIMER LITERAL
    WAIT                            ; opcode 0x3C
    ANIME    <rise_anim>
    ANIMESYNC                       ; 0x44
    ; clear/transition state, resume chase
    JMP      LABEL_CHASE_LOOP
LABEL_NORMAL_PATROL
    ...
LABEL_CHASE_LOOP
    MOVE/MOVEA/CMOVE   <toward party leader coords>
    ...
    JMP LABEL_START
```

UNVERIFIED: the literal `<duration_frames>` value pushed by `PSHN_L` immediately before `WAIT`. To find it: open the field in Deling, locate the kani entity, find its main method, search for the `WAIT` opcode in the post-knockdown branch and read the preceding `PSHN_L` literal. Field code runs at ~30 fps, so duration_seconds ≈ literal/30.

**Recommended freeze strategies for the mod, in order of preference:**

1. **Engine-level WAIT-extension hook (preferred — single hook, no script edit):** hook the engine's WAIT-tick decrement. The opcode_table entry for opcode 0x3C is the WAIT handler; resolve its address dynamically (it sits in `common_externals.execute_opcode_table[0x3C]`, same pattern FFNx uses). When the *currently executing entity* matches the kani's slot index AND the field is a chase field AND the mod is in manual mode AND the player has already had one battle in this field, intercept the per-frame countdown and never decrement it. The WAIT counter lives in the field-temporary "remaining ticks" slot of the kani's script-VM frame (each entity has its own VM frame; the WAIT counter is a member of that frame, near where the instruction pointer lives). Net effect: kani sits in collapsed state forever; chase-end condition (reaching beach or pub) still triggers when the player crosses the next field gateway, so no script breakage.

2. **Var-530-mediated short-circuit:** before the kani's main loop checks var 530, set a sentinel bit (use an unused high bit of var 530, or co-opt the `+128` bit observed in `+192 = enter pub to dodge`). Then in your script-aware logic, the kani's `JPF` will route into a "do nothing" branch. **Risk**: Qhimm's enumeration of var 530 is incomplete; using an unused bit may collide with story branching. Test by reading var 530 across an unmodified chase and recording every bit transition.

3. **BATTLE opcode interception:** hook `opcode_battle` (FFNx `ff8_externals.opcode_battle = common_externals.execute_opcode_table[0x69]`). When the caller is the kani entity's touchOn/across script and the mod is in "1 battle per field" mode and a battle has already happened this field, no-op the BATTLE call (return success, push 0/escape result back to script stack). This bypasses the rise timer entirely because no contact = no battle.

4. **MOVE-rate clamp:** hook the `MOVE` family handlers; when the entity is kani in a chase field in manual mode, set MSPEED to 0 (opcode 0x3D) or no-op the position update. This freezes kani in place visually without touching state vars.

**INFERRED but high-confidence**: strategies 1 and 3 will not break the chase-end condition because chase-end is gated on field transitions (reaching place ID 94 / Lapin Beach), not on kani's state. Strategy 2 is risky for unrelated story flags.

### 2.5 Chase-active flag

**CONFIRMED**: byte savemap var 530 carries chase-state bits including +16 ("xatm first knock out"). For runtime detection, the mod can read var 530 each frame from the savemap base + 0xD10 + 530.

**INFERRED secondary signals**: the `IDLOCK` (0x1F) / `IDUNLOCK` (0x20) opcodes likely lock player input during boss-jump-down cinematics; observing IDUNLOCK on the post-knockdown screen is another reliable chase-active onset signal.

---

## Q3 — Engine-level ASK invocation

### 3.1 The dispatch handler

**CONFIRMED** (FFNx `src/ff8_data.cpp`):
> `ff8_externals.opcode_ask = common_externals.execute_opcode_table[0x4A];`
> `ff8_externals.opcode_aask = common_externals.execute_opcode_table[0x6F];`
> `common_externals.execute_opcode_table = (uint32_t*)get_absolute_value(common_externals.update_field_entities, 0x65A);`

**Calling convention**: __cdecl in the FFNx hook surface. The opcode handlers in FF8 take a single pointer-to-script-VM-context argument and return an int status. The "parameters" of the ASK opcode are pulled by the handler from the script stack (which itself was populated by preceding `PSHN_L`/`PSHM_W` opcodes). 

**Prototype (FFNx-style)**: `int (*opcode_ask)(void* script_vm_context);`

There is no clean exposed prototype taking question-string and option-strings as direct arguments — the engine reads everything from the script-VM stack and the field's `MSD` (message) and `SYM` archives.

### 3.2 Prerequisites

**INFERRED** (architecture):
- A pre-existing `ff8_win_obj` slot (the active-windows array at `0x01D2B330`, struct size 0x3C) — same struct your show_dialog hook already touches.
- Game mode must be MODE_FIELD (so the field text resolver and field message archive are loaded).
- Calling from outside the script VM (e.g., a dinput8 frame hook) is **fragile**: the handler expects a valid current-entity context, instruction pointer, and stack — without those, behavior is undefined. Calling it as a one-shot can work if you also fake a script-VM context, but this is high risk.

**Bottom line**: do **not** call `opcode_ask` directly from a frame hook. Use the strategy in §3.5 instead.

### 3.3 How the selection is returned

**INFERRED**: the result is written back through the script-VM stack (the chosen index is pushed for the next opcode to pop) and also into a global selection slot. The FFNx codebase mentions `field_dialog_current_choice` as the canonical name for that slot. Resolving it: it is one of the `get_absolute_value` derivatives of `opcode_ask`, but the exact byte offset is not visible in the FFNx fragments retrieved.

**Diagnostic to confirm at runtime**: in your dinput8 hook, scratch-search the 256 bytes following `opcode_ask`'s entry point in IDA/Ghidra for absolute pointer references — the choice-result store is one of those, typically a `byte`/`int8_t`.

The `ff8_win_obj` callbacks at `+0x34` / `+0x38` — known from your existing show_dialog work — are the **input-event callbacks** for the window. For ASK windows, callback1 (`+0x34`) is invoked on Confirm; callback2 (`+0x38`) on Cancel. You can register your own callbacks, capture the selection there, and route it back to your mod's state.

### 3.4 Strings and buffers

**INFERRED**: ASK reads the question-message ID (the inline parameter in the opcode-encoding word, bits 15..0) and consults the field's `MSD` archive (the field's message file). The choice-min and choice-max are pushed onto the stack before the ASK call. To inject your own strings without touching the field's `.msg` archive:

1. Use FFNx's existing `replace_function` mechanism (you already use `field_get_dialog_string at 0x00530750`) to redirect the message-resolver call to your own buffer. When the kani entity is about to call MES/AMES/ASK and the message ID matches a sentinel you choose (e.g., 0xFE), substitute your custom string.
2. Or: write a transient string into a buffer the engine reads from before the ASK opcode executes, by intercepting the MSD-resolver function and providing a pointer to a heap-allocated UTF-8/Cp1252 buffer with the correct FF8 encoding.

### 3.5 Recommended approach for the mod — script-injection and proxy-window

Direct invocation is risky. Your mod already:
- Hooks `show_dialog(int32_t window_id, uint32_t state, int16_t a3)` at `sub_4A0C00 + 0x5F`.
- Manages an active-windows array.

Recommended pattern:
1. In `show_dialog`, when you detect the chase-start MES (the unambiguous signature of Squall's "Run!" + var-530 transition; see Q4), open your **own owned dialog window** (a fresh slot in the active-windows array or a wholly separate accessibility overlay).
2. Render your two-mode question via your existing TTS pipeline (NVDA-friendly, since the developer is the tester).
3. Read controller input directly from `ff8_externals.engine_gamepad_button_pressed` (CONFIRMED resolved via `get_absolute_value(has_keyboard_gamepad_input, 0x22)` in FFNx) — bypassing the script VM entirely.
4. Store the chosen mode in a mod-local variable.
5. Branch the rest of the chase behavior (auto-drive vs manual freeze) based on that variable.

This avoids re-entering the engine's ASK pipeline at all.

### 3.6 Community examples

I did not find a primary-source quote of any community mod (Echo-S, Tonberry/Roses-and-Wine HD, FFNx core, OpenVIII) **invoking ASK from outside the script VM**. FFNx hooks `opcode_ask`'s pointer for read access (so it can swap in custom translations), but does not synthesize new ASK calls. The pattern in §3.5 (own dialog overlay) is what Echo-S effectively does for voice cues — it observes and overlays, does not re-enter.

---

## Q4 — Chase-Start Trigger Detection

### 4.1 The "Run!" dialog

The script has Zell saying *"Let's get the hell outta here!"* (per the icybrian.com FF8 disc 1 script: *"Zell 'Let's get the hell outta here!' L2, R2 buttons to escape."*) at the close of the first X-ATM092 battle. After exiting the battle, the field reloads and there is typically a short MES from Squall ("Run!") triggering the chase. **Opcode**: `MES` (0x47) with a small inline message ID. **Field**: the post-tower "outside comm tower" / mountain field (place ID 99→100 transition).

UNVERIFIED: the exact message ID. Read it via Deling on the post-tower field.

### 4.2 Disambiguation in your show_dialog hook

Your `show_dialog(int32_t window_id, uint32_t state, int16_t a3)` hook gets the window_id and state. The `text_data1` at `+0x08` and `text_data2` at `+0x0C` of the `ff8_win_obj` (size 0x3C) are pointers to the resolved string. To detect "this is the chase-start moment" unambiguously:

```pseudo
on_show_dialog_called(win_obj):
    if (current_field_id is in CHASE_FIELDS_SET) AND
       (savemap[var530] & 0x10)  /* xatm first knock out */ AND
       (NOT mod_state.chase_start_already_handled):
        mod_state.chase_start_already_handled = true
        open_accessibility_ask_overlay()
```

This is more robust than string matching because:
- It survives translation/localization differences.
- It does not rely on the exact MES content.
- It survives mod-installed dialog tweaks.

### 4.3 Savemap signal — confirmed primary source

**CONFIRMED**: byte savemap var 530, bit 0x10 ("xatm first knock out"), at offset `0xD10 + 530 = 0xF22` from save block start, normalized to your 76-byte-header savemap base = `0x01CFDC5C + 0xF22 = 0x01CFEB7E`. Polling this every frame detects chase-start independently of dialog matching.

---

## Q5 — Auto-Drive Route (Stretch Goal)

### 5.1 Documented movement patterns

Speedrun and walkthrough community knowledge (Jegged.com, GameFAQs LVeitch guide, Gamer Guides, RPGSite):

| Screen | X-ATM092 behavior | Player routing tip |
|---|---|---|
| Mountain hideout (place 100) — the "Selphie cliff" screen | X-ATM092 enters from upper edge; if player delays, contact battle | Run **left** as fast as possible; do not jump down the cliff (Attitude penalty) |
| Mountain stairs / downward slope screen | Ground shake: if player runs, ground shakes and X-ATM catches up | **Walk** (Triangle / Square button), do not run |
| Mountain bridge | X-ATM **jumps over the party** when they reach a certain bridge X-coord | Run toward the far end; when you hear the jump SFX, **double back**; X-ATM jumps over again; turn forward and proceed |
| Town square (place 93) | X-ATM patrols, no jumps; constant motion required | Talk to the dog (interact) to shoo it (Attitude bonus); avoid the pub (Attitude penalty) |
| Pathway to beach | X-ATM closes in | Run straight |
| Lapin Beach (place 94) | FMV — chase ends, Quistis destroys X-ATM | n/a |

CONFIRMED quotes (Jegged.com): *"On the next screen (the one where Selphie jumped off the cliff), run immediately to the left as quickly as possible. If you delay at all, you will have to fight X-ATM092 again. The next screen is a pathway leading south. Walk, don't run, to the bottom of the screen. Continue running as quickly as possible on the next screen. On the bridge, run to the right, but wait until you hear X-ATM092 jump over you. Once it does, spin back around and run to the left."*

### 5.2 Pathing determinism

CONFIRMED-INFERRED from speedrun guides: X-ATM092's pathing is **partially deterministic** (entry edge and patrol pattern fixed per screen) but **partially player-position-reactive** in that some screens (mountain stairs) trigger ground-shake based on player's run-vs-walk state, and the bridge "jump over" has a positional trigger. Walkmesh is honored — X-ATM does not phase through scenery.

### 5.3 Scripted player movement mechanisms

**Available opcodes** (CONFIRMED from FFRTT opcode table):
- `SET3` (0x1E) — teleport entity to (X, Y, Z, walkmesh-triangle-id). Used heavily in cutscene init.
- `SET` (0x1D) — teleport without walkmesh constraint.
- `MOVE` (0x3E) — walk/run to (X, Y, Z) along walkmesh.
- `MOVEA` (0x3F) — walk to another entity's position.
- `PMOVEA` (0x40) — walk to player.
- `CMOVE` (0x41) — close-move variant.
- `FMOVE` (0x42) — forced move.
- `MOVESYNC` (0x7D) — wait for current MOVE to complete.
- `MOVECANCEL` (0x12B), `MOVEFLUSH` (0x148).
- `MSPEED` (0x3D) — set move speed.
- `JUMP` (0x23), `JUMP3` (0x24), `LADDERUP/DOWN` (0x25/0x26).
- `FOLLOWOFF` (0xAB) / `FOLLOWON` (0xAC) — toggle party-trail behavior.
- `SPLIT` (0xE6) / `JOIN` (0x93) — split/rejoin party for cinematics.
- `RUNENABLE` (0xF6) / `RUNDISABLE` (0xF7).
- `FACEDIR` family (0xFD–0x113) for orientation.

There is **no documented direct-position-override that bypasses both walkmesh and the move pipeline**. Cinematic sequences (parade, train carriage, SeeD ball dance) use `FOLLOWOFF` + `SET3` init + scripted `MOVE`/`MOVEA` chains.

### 5.4 Auto-drive recommendation

Given the tools above, the safest auto-drive approach is:

1. **Pre-record a per-field scripted route** as a list of `(target_x, target_y, target_z, walkmesh_tri)` waypoints to the forward gateway.
2. On entering a chase field in auto-drive mode, hook the field-frame update; for each frame:
   - Read the player leader's current position from the field-entity table.
   - If close to current waypoint, advance to next.
   - **Inject virtual analog stick input** toward (waypoint - leader_pos), magnitude 1.0 (run).
3. **Critical**: the analog-stick input is normally interpreted **camera-relative** (this is the bug the mod's existing walkmesh-based steering hits). Solution: read the field's camera matrix (resolved through `engine_setviewport_sub_45B4C0` and friends in FFNx — `ff8_externals.engine_setviewport_sub_45B4C0 = get_relative_call(ff8_externals.field_main_loop, 0x39)` per `src/ff8_data.cpp`), then **rotate the desired world-space movement vector by the inverse of the camera yaw** before injecting it as the analog stick value. This converts "I want to move +X in world space" to the camera-relative stick deflection the engine expects.
4. For ground-shake screens, override the run/walk state — push `RUNDISABLE` semantics or send the walk-button input.
5. For the bridge "jump-over" screen, the auto-drive must **double back when it hears the jump SFX or detects X-ATM's animation state change** — use the same kani entity-state observation as in Q2.

Cleaner alternative: **disable input entirely** in auto-drive and inject `MOVE` opcode equivalents via the same kani-style entity-script-state manipulation, but this is a deeper engine intervention.

---

## Q6 — Disassembly and Code-Pointer Specifics

### Anchor symbols (resolve dynamically à la FFNx)

| Symbol | Resolution path | Purpose |
|---|---|---|
| `update_field_entities` | FFNx anchor | Field-entity update loop; offset 0x65A → execute_opcode_table |
| `execute_opcode_table` | `update_field_entities + 0x65A` (absolute) | Array of all field opcode handlers |
| `opcode_ask` | `execute_opcode_table[0x4A]` | ASK handler (CONFIRMED, FFNx) |
| `opcode_aask` | `execute_opcode_table[0x6F]` | AASK (async) handler (CONFIRMED, FFNx) |
| `opcode_ames` | `execute_opcode_table[0x65]` | AMES handler |
| `opcode_amesw` | `execute_opcode_table[0x64]` | AMESW handler |
| `opcode_battle` | `execute_opcode_table[0x69]` | BATTLE handler (CONFIRMED, FFNx) |
| `opcode_setvibrate` | `execute_opcode_table[0xA1]` | SETVIBRATE; used as anchor for vibrate_data_field |
| `opcode_winclose` | `execute_opcode_table[0x4C]` | WINCLOSE |
| `opcode_movie` | `execute_opcode_table[0x4F]` | MOVIE (used for pre-beach FMV) |
| `opcode_tuto` | `execute_opcode_table[0x177]` | TUTO |
| `current_field_name` | `get_absolute_value(opcode_effectplay2, 0x75)` | char* of running field's short name (CONFIRMED, FFNx) |
| `previous_field_id` | `get_absolute_value(sub_470250, 0x13)` | WORD of previous field ID (CONFIRMED, FFNx) |
| `update_entities_call` | `update_field_entities + 0x657` | Entity-update call site |
| `engine_gamepad_button_pressed` | `get_absolute_value(has_keyboard_gamepad_input, 0x22)` | BYTE: current gamepad-button mask (CONFIRMED, FFNx) |
| `field_main_loop` | FFNx anchor | Main per-frame field loop |
| `engine_setviewport_sub_45B4C0` | `get_relative_call(field_main_loop, 0x39)` | Sets viewport; useful as anchor for camera state |
| `show_dialog` | `get_relative_call(sub_4A0C00, 0x5F)` | Dialog-open dispatcher (CONFIRMED, FFNx; mod already uses this) |

### JSM opcode IDs in the bit-31 scheme

All opcode IDs the mod uses in chase work, normalized:

| Mnemonic | Bit-31 ID | Notes |
|---|---|---|
| PSHN_L | 0x007 | Push literal (also represented bit31=0 in encoded word) |
| PSHM_W | 0x00C | Push var (word) |
| POPM_W | 0x00D | Pop to var (word) |
| PSHM_B | 0x00A | Push var (byte) — for var 530 reads |
| REQ | 0x014 |  |
| REQSW | 0x015 |  |
| REQEW | 0x016 |  |
| SET | 0x01D | Teleport (no walkmesh tri) |
| SET3 | 0x01E | Teleport with walkmesh tri |
| MAPJUMP | 0x029 |  |
| MAPJUMP3 | 0x02A |  |
| BASEANIME | 0x02C |  |
| ANIME | 0x02D |  |
| DISCJUMP | 0x038 |  |
| SETLINE | 0x039 | 3-D hitbox (X-ATM contact trigger) |
| WAIT | 0x03C | The rise-timer opcode |
| MSPEED | 0x03D |  |
| MOVE | 0x03E |  |
| ANIMESYNC | 0x044 |  |
| MES | 0x047 |  |
| MESW | 0x046 |  |
| MESSYNC | 0x048 |  |
| ASK | 0x04A |  |
| WINSIZE | 0x04B |  |
| WINCLOSE | 0x04C |  |
| MOVIE | 0x04F |  |
| MAPJUMPO | 0x05C |  |
| AMESW | 0x064 |  |
| AMES | 0x065 |  |
| BATTLE | 0x069 | The contact-battle opcode |
| BATTLEON | 0x06B |  |
| BATTLEOFF | 0x06C |  |
| AASK | 0x06F |  |
| MOVESYNC | 0x07D |  |
| WORLDMAPJUMP | 0x10D |  |
| RAMESW | 0x116 |  |
| TUTO | 0x177 |  |

### Savemap offsets (76-byte / 0x4C header normalization applied)

| Datum | Save-block offset | Absolute address (savemap base 0x01CFDC5C) | Size | Notes |
|---|---|---|---|---|
| (var area base) | 0xD10 | 0x01CFE96C | — | "save block starts at 0xD10" (Qhimm) |
| Var 84 — Last area visited (place ID) | 0xD10 + 84 = 0xD64 | 0x01CFE9C0 | byte | Tracks place IDs 92–100 etc. |
| Var 256 — Main story progress | 0xD10 + 256 = 0xE10 | 0x01CFEA6C | word | "getting main story progress (word 256, which is word 0x100 in hex)..." (Qhimm) |
| **Var 530 — Dollet state bitmap** | **0xD10 + 530 = 0xF22** | **0x01CFEB7E** | **byte** | **Bits: +2 crossed bridge, +4 elvoret done, +16 xatm 1st KO, +32 selphie waiting, +18 cliff jump, -18 enter tower, +192 enter pub** |
| Active party formation | (provided) +0xAF0 | 0x01CFE74C | — | Confirmed by you |
| Worldmap flag | (provided) +0x125C | — | — | Confirmed by you |
| car_rent flag | — | 0x01CFEF1A | byte | Confirmed by you |

> **Note on the 76-byte vs 96-byte header subtraction**: Qhimm sources frequently quote variable offsets relative to the save block start (0xD10). Those are header-agnostic — they are offsets *inside* the variable area and do not need ±0x14 adjustment. The 0x14 adjustment applies only when a community source quotes a *file* offset including the 96-byte header; in that case subtract 0x14 to convert to your 76-byte file-offset convention. The Q1 var-530 offset above uses save-block-relative arithmetic and so is already correct for both header conventions.

### FFNx source paths (v1.23.0.x)

- `src/ff8_data.cpp` — opcode table resolution, all `opcode_*` pointer inits (lines around the `execute_opcode_table[0x4A]` etc. references quoted above).
- `src/ff8.h` — common struct definitions and externals.
- `src/voice.cpp` — Echo-S voice integration; observes show_dialog hook (study this for the proxy-window pattern).
- `src/ff8/field/*` — field-specific hooks, including `field_get_dialog_string` replacement.
- `src/ff8_opengl.cpp` — graphics layer (not directly relevant).

---

## Consolidated Implementation Guidance

### (a) Detecting chase start

Use the **AND** of three signals, polled in your show_dialog hook + a frame hook:

```pseudo
chase_active = 
    (current_field_name in chase_field_set)  // discovered at runtime via current_field_name
    AND ((*(byte*)0x01CFEB7E) & 0x10) != 0   // var 530, bit 0x10, "xatm first knock out"
    AND mod_state.chase_done == false
```

Trigger the accessibility-mode ASK overlay the first time `chase_active` flips from false to true.

### (b) Invoking ASK

**Do not** call engine `opcode_ask` directly. Instead:
1. Open your **own** dialog window via the existing `show_dialog` hook surface (allocate a new ff8_win_obj slot or use a wholly external accessibility overlay).
2. Render the two-mode question through your TTS pipeline (NVDA-friendly strings already in your mod).
3. Read input directly from `engine_gamepad_button_pressed` (or your existing input hook).
4. Close the proxy window when the user confirms.

### (c) Reading the player's selection

Capture the choice in a mod-local variable (no engine state involved). Persist via the mod's own config file or a registry/dinput8-side store. There is no need to round-trip through `field_dialog_current_choice` or any savemap variable.

### (d) Freezing the respawn timer (manual mode)

**Recommended primary**: hook `opcode_battle` (`execute_opcode_table[0x69]`). When the calling entity is the kani slot in a chase field AND the mod has already counted one battle this field AND mode==manual, no-op the call: push 0 (escape) onto the script stack, return success. This caps the mode at 1 battle per field and entirely sidesteps the rise timer.

**Recommended secondary** (engine-pure freeze, no save state touched): hook `opcode_wait` (`execute_opcode_table[0x3C]`). When the calling entity is the kani in a chase field AND the post-knockdown branch is active AND mode==manual, simply do not decrement the WAIT counter — return as if 1 frame elapsed but treat it as 0. Net effect: kani sits collapsed forever; chase-end gateway transitions still fire.

**Avoid** modifying var 530 directly — its bits are not fully enumerated in the public docs, and side effects on later story flags (Selphie, Quistis, the pub option) are unknown.

### (e) Safe-pathing in auto-drive mode

1. Use `current_field_name` (resolved via FFNx anchor) to drive a per-field route table mapped to the field's `INF` gateways.
2. Per frame: compute world-space vector from leader to next waypoint; rotate by inverse camera yaw (read camera matrix near `engine_setviewport_sub_45B4C0`); inject as virtual analog stick.
3. For ground-shake screens, force walk (override RunEnable via input hook).
4. For the bridge "jump-over" screen, watch the kani entity's animation index — when it transitions to the jump animation, double back; when it transitions back to chase, resume forward.
5. Suppress kani contact via `opcode_battle` no-op (same as manual mode strategy d) — eliminates the "did I get caught?" RNG.
6. The `MAPJUMP` (0x29) call inside the forward gateway's touchOn script will fire when the leader crosses the gateway line; let the engine handle the field transition naturally.

---

## Unresolved Questions (Empirically Determine via Runtime Diagnostic Logging)

1. **Internal short field names** for each chase screen. Log `*current_field_name` on every field transition during a single chase playthrough. This produces the canonical 4–8-char names (e.g., `bdin1`, `bdtw1`, `bggate*`) without needing offline tooling. **Cost**: one playthrough.

2. **kani's entity slot index per chase field.** Open each chase field in Deling, look for the entity labeled `kani`, record its zero-based group index. Or, at runtime, walk the field-entity table and find the entity whose model matches the X-ATM092 model id; log its slot index. **Cost**: a few minutes of in-game observation per field with diagnostic logging.

3. **Exact `WAIT` literal pushed before the rise** — the duration of the post-knockdown collapsed state in frames. Find by Deling-disassembling kani's main method and reading the `PSHN_L` immediately preceding the `WAIT` in the post-knockdown branch. **Probable range**: 60–300 frames (2–10 seconds) based on observed in-game timing, but this is **UNVERIFIED** and should be measured.

4. **Chase formation IDs.** Read the `PSHN_L` literal pushed before each `BATTLE` (0x69) call in the kani touchOn handlers across chase fields. UNVERIFIED whether they're identical or per-field.

5. **`field_dialog_current_choice` exact address.** Disassemble `opcode_ask` in Ghidra/IDA; the choice-result write is one of the absolute store sites in the handler.

6. **Var 530 unused bits.** Read var 530 across an unmodified chase, log every transition, identify which bits Qhimm doesn't enumerate. This both confirms safety of using var 530 mediated freeze and reveals any chase-end-flag bit not in the published docs.

7. **Whether `ASK` (0x4A) vs `AASK` (0x6F)** is the better hook target if you ever decide to inject. AASK is the async variant; for an accessibility overlay it's synchronous (block field while user chooses), so 0x4A is the correct pattern to study.

---

## Caveats — PSX vs. PC vs. Remaster

- **All addresses, hooks, and the savemap base 0x01CFDC5C are Steam 2013 PC only.** PSX and Remaster have different memory layouts and (for Remaster) a different engine entirely.
- The 76-byte vs 96-byte savemap header convention only matters when consuming community sources that quote *file* offsets; offsets quoted relative to the save block start (`0xD10`) are header-agnostic and apply to both.
- The `kani` entity name and var 530 chase-state bitmap are **engine-data-level** and survive across PSX/PC/Remaster (the raw field scripts and savemap variable layout are largely shared). The opcode-handler addresses do not.
- The rise-timer mechanism (WAIT-based) is INFERRED to be identical across PSX/PC because the JSM scripts are the same data files. Remaster's reimplemented engine may handle WAIT differently, but the literal value pushed by `PSHN_L` is in the data, not the engine.
- The Final Fantasy Wiki note that the PSX *demo* version has X-ATM**082** (not 092) with the chase un-defeatable confirms this is a chase mechanic implemented in field scripts — the PSX retail and PC retail share that mechanic.
- The "2026 Re-Release" mentioned in current FFNx changelogs is a *separate* build from your Steam 2013 target; FFNx now supports both, but addresses differ.

---

## Confidence Summary

- **Primary-source CONFIRMED**: `kani` entity name; opcode IDs and execute_opcode_table resolution pattern; var 530 chase state bitmap and bits; FFNx hook anchors (show_dialog, current_field_name, previous_field_id, gamepad_button_pressed, field_main_loop, engine_setviewport); place IDs 92–100 for Dollet area; SETLINE semantics; SET3/MOVE semantics; speedrun-route per-screen tactics.
- **INFERRED with high confidence**: the rise-timer is a `WAIT`-opcode countdown inside the kani's main method (architecture-consistent; only the literal value is unmeasured); BATTLE-no-op cap-at-1 strategy is non-destructive (BATTLE is leaf-called from touchOn handlers); proxy-window approach is safer than reusing engine ASK.
- **UNVERIFIED — requires runtime diagnostic playthrough**: internal short field names per chase field; kani slot indices per chase field; exact WAIT literal; exact chase formation IDs; field_dialog_current_choice address; full enumeration of var 530 bits.

The unresolved items are all cheaply measurable with one diagnostic-logging build of the mod plus one playthrough — none of them require additional engine reverse-engineering. The build path forward is clear: build the proxy-window ASK + BATTLE-no-op skeleton today, instrument logging for the seven items above, and a single-session playthrough resolves all remaining gaps.
