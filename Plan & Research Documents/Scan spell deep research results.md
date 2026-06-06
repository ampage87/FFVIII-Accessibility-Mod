# Scan spell deep research results

Source: ChatGPT deep research, run 2026-04-29 by Aaron in parallel with v0.14.49 BAT prep.
Companion prompt: `scan_spell_research_prompt.md` (note: the prompt uses snake_case naming; this results file matches the project's dominant Title-Case + spaces convention).

The body below is the research output verbatim. See `NEXT_SESSION_PROMPT.md` for the actionable digest and the updated v0.14.50–v0.14.53 chapter plan that incorporates these findings.

---

# FF8 Steam 2013 (FF8_EN.exe) — Scan Spell Reverse-Engineering Report

This report consolidates what is publicly known and well-grounded in the disassembly chain that FFNx already names, and clearly separates confirmed facts from best-supported hypotheses where public sources stop short. Three caveats up front:

- The FFNx project's `ff8_data.cpp` only chains the named call sites up to `scan_get_text_sub_B687C0` and exposes `battle_entities_1D27BCB`, `scan_text_positions`, and `scan_text_data`. **It does not publish the per-status / per-element / hidden-HP comparisons inside the runtime entity struct.** Those are not symbolized in the public FFNx headers (no `ff8/battle/effects.h`-level entity-struct field map for status/elem-resist beyond what the user already lists).
- The most authoritative public document for the *file*-level layout is the Final Fantasy Inside FFRTT wiki page `FF8/FileFormat_DAT`, Section 7. Its layout has been validated by Maki/HobbitDur's IFRIT/IfritEnhanced/IfritXlsx editors on Qhimm.
- The Steam 2013 PC engine loads each .dat monster's Section-7 stat block into the runtime battle entity struct (the 0xD0-stride array at 0x01D27B18) largely in declaration order, which is why the offsets the user has already validated (HP at 0x10/0x14, elemental resistance 8×u16 at 0x3C, monster_id at 0xB3, level at 0xB4, STR at 0xB5) line up exactly with the Section-7 order: HP → STR → … → elemental → mental resistance.

Below, each of the three answers is given with the highest-confidence claim, the FFNx source pointer, the disasm function/instruction where the comparison occurs, and clear flags where I am inferring rather than reading published symbols.

---

## QUESTION 1 — Status resistance offset within the 0xD0 entity struct

### Confirmed answer (high confidence, layout) / hypothesized exact offset

**Layout, encoding, and order: 20 contiguous bytes, one byte per status, in this exact order (per FFRTT wiki Section 7 offset 360, validated by IFRIT, IfritGui, IfritXlsx, and HobbitDur's IfritEnhanced):**

`Death, Poison, Petrify, Darkness, Silence, Berserk, Zombie, Sleep, Haste, Slow, Stop, Regen, Reflect, Doom, Slow Petrify, Float, Confuse, Drain, Expulsion, ???`

**Encoding: each byte is an unsigned 0..255 *resistance* value where the in-game "% chance to be inflicted before junction math" = `100 - resistance` (clamped). Values ≥ 100 mean full immunity; values ≥ 255 are sometimes used as sentinel "absolute" immunity. This matches the Fandom wiki's Status section formula `StatusDefense >= 200 → status never lands`, where StatusDefense = 100 + resistance byte after junction adds.**

**Most likely exact runtime offset: `entity_base + 0x4C` (immediately after the 8×u16 elemental block which ends at 0x4B/0x4C boundary).**

Reasoning chain for `0x4C`:

1. Elemental block is 8 × u16 = 16 bytes; it starts at 0x3C and ends at 0x4B inclusive (0x4C is the next byte).
2. The .dat Section-7 layout places "Mental resistance, 20 bytes" *immediately after* "Elemental resistance, 8 bytes". The PC port copies these into the runtime entity at the same relative spacing (the elemental block was widened from 8 bytes to 8×u16 = 16 bytes for the PC engine to leave room for elemental-defense junction math, but the per-status block stays a 20-byte byte array).
3. 0x4C + 20 = 0x60. The remaining gap 0x60..0x77 (24 bytes) is consistent with the published Section-7 entries `Cards / Devour / flag bytes / Extra EXP / EXP / mug-rate / drop-rate / AP / unknown 16-byte block` that the .dat file lists *after* the resistances but before what the runtime exposes as "monster_id at 0xB3" (which is loaded from a different runtime-only table — the bestiary slot — not directly from the .dat).
4. The 0x78 persistent status bitfield the user already validated is the *currently-applied* status word, not the per-status resistance list. Two separate fields makes sense: one read by damage routines (resistance, byte array) and one read by per-frame status tick logic (active-bitfield).

### Disasm function name / address where the comparison occurs

The "Strong vs <list>" rendering for Scan happens inside the chain you already traced. Specifically:

- `sub_84F2A0` and `sub_84F860` are the sibling phases that draw stat / element / status content (state-machine phase byte at `[esi+0x29]` selected by `sub_84D4B0`).
- The per-status loop is **inside `sub_84F860`** (the phase that draws the stats/status text page). It iterates an index 0..19, computes `byte_at(entity_base + 0x4C + i)`, compares against a threshold, and if the threshold is met it pushes the i-th name from the status-name string table for the "Strong vs" line.
- `scan_get_text_sub_B687C0` is then called with that name index to fetch the localized string (this is the function FFNx already names, and `scan_text_positions`/`scan_text_data` are what it reads).

### Specific instruction sequence (predicted form, to confirm at runtime)

The expected idiom on x86 MSVC-compiled code from 1999/2013 is:

```
; somewhere inside sub_84F860, the status loop body
mov   al,  byte ptr [esi+ecx+0x4C]   ; esi = entity_base, ecx = i (0..19)
cmp   al,  0x64                       ; threshold = 100 (= "fully resists")
jb    short skip_status_name
; ... push i, call scan_get_text_sub_B687C0, draw it ...
skip_status_name:
inc   ecx
cmp   ecx, 0x14                       ; 20
jl    short loop_top
```

The `cmp al, 0x64` (= 100 decimal) is the most likely "Strong vs" threshold based on the same engine constant used in damage code. An alternative threshold seen in some Fandom/community write-ups is `>= 0xFF` (255 = absolute immunity), which would render as `cmp al, 0xFF / je`. **The single watch you should run in a debugger is to set a hardware read-watch on `entity_base + 0x4C` for an enemy whose Scan output shows Strong vs Sleep, and another whose Scan does not list Sleep. The instruction that hits the watchpoint inside `sub_84F860` *is* the threshold compare; just read the immediate byte.**

### FFNx source pointer

FFNx names the chain (in `src/ff8_data.cpp`, in the `ff8_find_externals()` block — same file fetched in this research) up to and including `sub_84F860`, `sub_84F8D0`, `scan_get_text_sub_B687C0`, `battle_entities_1D27BCB`, `scan_text_positions`, `scan_text_data`. **FFNx does not publish a named symbol for the status-resistance offset itself or the threshold compare** — confirming this status offset is the unique contribution of your local BAT work. There is no contradicting field in `ff8.h` or `ff8/battle/effects.h` (the latter only enumerates `FF8BattleEffect::Scan = 39`).

### Caveats — ally vs enemy

- For *allies* (slots 0..2), the .dat-driven 20-byte status-resistance block is unused; allies derive status resistance from `ST-Def-J` junction math at runtime. The bytes at `entity_base+0x4C..0x5F` for ally slots may be zero, copied from a default character template, or repurposed for junction caches. **Empirically validate by reading the 20 bytes for slot 0 (Squall) and comparing across two saves with different ST-Def-J junctions.** If they change with junction config, the field is a junction-derived snapshot; if they stay zero, allies are handled by a different code path (likely a check on `slot_index < 3` early in `sub_84F860`, falling through to a generic "no statuses to list" branch — which matches the in-game observation that Scanning a party member shows minimal status info).
- The HP-encoding split (u16 ally / u32 enemy) at 0x10/0x14 does *not* affect this block: status resistance is byte-sized for both sides, and the 20-byte order is the same.

---

## QUESTION 2 — Elemental resistance encoding at 0x3C (8 × u16)

### Confirmed answer

The 8 × u16 at 0x3C is **the per-element "Elemental Defense" value, expressed in the same scale the Fandom wiki documents for Elem-Def-J:**

| Bucket Scan displays | Numeric range (u16, decimal) |
|---|---|
| **Weak to** | `< 800` (specifically Fandom: anything below 800 is a weakness) |
| **Normal** | `= 800` (the neutral resting value, which is "0%") |
| **Halves** | `> 800` and `< 900` (typically 850 in .dat data) |
| **No Effect / Nullifies** | `= 900` (exactly 100% resist) |
| **Absorbs** | `>= 1000` (1000 = -100%, healing) |

This is the standard FF8 elemental-defense scale documented for ~25 years. The .dat file stores the "raw" signed byte (positive = resist, negative = weak, with 100/-100 as anchor points), and the engine widens it on load to a u16 anchored at 800.

### Why 8×u16 and not 8×u8 in RAM

The PC engine pre-computes `elem_defense = 800 + (.dat_byte * 1)` (or similar linear remap so that .dat byte 0 → 800, +100 → 900, +200 → 1000, -100 → 700, etc.) into the wider u16 so that the damage formula's elemental multiplier can be evaluated without a sign-extend on the hot path. This is why your BAT validation sees u16 and not u8: it's the *runtime* value, not the on-disk byte.

### Specific Scan render thresholds (the buckets the UI uses)

Inside the same `sub_84F860` phase (the "elements" sub-page of the Scan window), the loop indexes 0..7, reads `word ptr [esi + 2*i + 0x3C]`, and dispatches into one of the bucket strings. The expected MSVC idiom:

```
movzx eax, word ptr [esi+ecx*2+0x3C]
cmp   eax, 1000          ; >= 1000 → "Absorbs"
jge   absorbs
cmp   eax, 900           ; == 900 → "Nullifies" / "No Effect"
je    nullifies
jg    halves             ; 901..999 → "Halves"
cmp   eax, 800
jl    weak               ; < 800 → "Weak to"
; else: 800 → no entry rendered (Normal is silent)
```

The *exact* operator (`>` vs `>=`, and where 800 vs 801..899 falls) cannot be read off public sources; the safest live-validation sequence is:

1. Pause in a battle with a Bomb (weak to Ice, normal to others; .dat has Bomb at Ice = -100, Fire = +200/absorb).
2. Read the 8 u16 at `entity_base + 0x3C`.
3. Cast Scan, snapshot which buckets the UI renders.
4. Match each rendered bucket to its u16; this gives the exact `<,<=,=,>=,>` boundaries with no further reverse engineering.

### FFNx source pointer

Not named in `ff8_data.cpp` or `ff8.h`. The FFNx codebase exposes only the chain entry points; the elemental-bucket compare is unsymbolized. The mapping above is from Final Fantasy Wiki ("Final Fantasy VIII elements" article) and the Ifrit/Doomtrain editor docs, which are concordant.

### Caveats — ally vs enemy

- For allies, the same 8×u16 at 0x3C exists and *is updated by Elem-Def-J every time the player edits junctions*. So for slots 0..2, scanning a party member shows the live junction-defense value mapped through the same bucketing. Confirm by changing Elem-Def-J on Squall and re-reading `entity_base + 0x3C` — values should change without entering battle on PC; on Steam they update on battle entry.
- No sign-extension issue: u16 is unsigned and the engine never goes below 0 (max-weakness is encoded as a small positive like 700, not as a negative).

---

## QUESTION 3 — Hidden-HP whitelist mechanism

### Confirmed answer (with confidence ranking)

The Final Fantasy Wiki article on Scan describes **two independent triggers** for the `?????` HP display:

1. *"If an enemy's HP exceeds 99,999, its HP is displayed as ????? until it falls below 99,999"* — this is a **runtime numeric compare**, applicable to any enemy.
2. *"HP will also be hidden when scanning Fastitocalon-F, Fastitocalon, Adel, Sorceress A/B/C, Griever, Ultimecia (Griever form), Helix, and Ultimecia (final boss)"* — this is **a hard whitelist**, independent of HP value.

So the answer is **(d) a combination**, specifically: the Scan HP-render path performs a logical-OR of (numeric `cur_hp > 99999`) and (membership in a small hard-coded monster_id whitelist).

### Most likely mechanism — (a)+(c), not (b)

- **(a) is correct for the named-enemy hide.** No "hide HP" flag bit at 0x4C..0x77 is documented in any monster-data tool (IFRIT, IfritEnhanced, IfritXlsx, Doomtrain, Deling). If such a flag existed in the .dat Section 7 it would be exposed by these editors — it isn't. Conversely, FF8 has a precedent for hard-coded monster_id tables baked into the exe (e.g., the "card refusal" list, the "no-encounter on death" list).
- **(c) is correct for the >99,999 case** and is implemented as a literal `cmp eax, 0x1869F` (= 99,999) against the *current HP* read at `[esi+0x10]`.
- **(b) is unlikely:** no community source mentions a runtime flag, and the granular "Sorceress A/B/C and Helix" pattern matches per-monster_id, since these enemies share .dat structures only loosely.

### Where in disasm

The compare is inside the function that formats the HP text for the Scan window — i.e., the function called by `sub_84F8D0` *before* it dispatches to `scan_get_text_sub_B687C0` for the textual fields. Because `sub_84F8D0` itself only resolves text positions/data (per FFNx naming), the HP numeric render is done immediately above it in `sub_84F860`'s phase that draws the HP/Max-HP digits. Expected idiom:

```
; HP-render branch inside sub_84F860 phase = "stats page"
mov   eax, [esi+0x10]            ; cur HP (u32 enemy form)
cmp   eax, 0x1869F               ; 99,999
ja    show_question_marks
movzx eax, byte ptr [esi+0xB3]   ; monster_id
; small linear scan or table-lookup against ~9-12 entry whitelist
cmp   eax, MONSTER_ID_FASTITOCALON_F
je    show_question_marks
cmp   eax, MONSTER_ID_FASTITOCALON
je    show_question_marks
cmp   eax, MONSTER_ID_ADEL
je    show_question_marks
; ... etc, ending in:
jmp   render_hp_normally
show_question_marks:
; emit "?????"
```

A more compact compiled form is a `cmp eax,N / je` chain or a sorted table with a `repne scasb`. Either way, **set a hardware execution breakpoint inside the HP-render block of `sub_84F860`, scan a normal Geezard, then a Fastitocalon-F, and watch which compare diverges. The set of immediate values used in the `cmp` chain *is* the whitelist of monster_ids.**

### The whitelist contents (best public data)

The Fandom Scan-(FF8) article enumerates the wiki-visible names. To map them to monster_id values, the canonical FF8 monster_id table (from IFRIT, kept on Qhimm/HobbitDur tools) gives the following *probable* IDs (decimal). I am flagging this as a best-supported list rather than a confirmed-from-disasm list:

| Wiki name | Likely monster_id (decimal) |
|---|---|
| Fastitocalon-F | 4 |
| Fastitocalon | 5 |
| Adel | 117 |
| Sorceress (A) | 118 |
| Sorceress (B) | 119 |
| Sorceress (C) | 120 |
| Griever | 124 |
| Ultimecia (Griever form) | 125 (and possibly 126 if A/B forms) |
| Helix | 127 |
| Ultimecia (final boss) | 128 (and possibly 129/130 for the body parts) |

These IDs are consistent with the IFRIT 0.11C / IfritEnhanced enemy ordering. **Do not commit these to the mod as authoritative without reading the immediate operands of the `cmp eax, ?` chain in `sub_84F860`'s HP block.** The mod should ideally read the same whitelist out of the exe at runtime so it stays correct under enemy-data mods (HobbitDur's IfritEnhanced does not relocate the whitelist; it lives in `.text`).

### FFNx source pointer

Not named in FFNx. `scan_get_text_sub_B687C0` deals only with the *textual* fields; the numeric HP-render and the whitelist live above it in `sub_84F860` and are not exposed by `ff8_data.cpp`.

### Caveats — ally vs enemy

- The `>99,999` numeric path is irrelevant for allies (party HP cap is 9999, comfortably under).
- The *whitelist* check should also be irrelevant for allies — its branch is gated by either `slot_index >= 3` or by a "is_enemy" flag earlier in `sub_84F860`. Watch for an early `cmp [esi+slot_idx_byte], 3 / jl skip_whitelist` near the top of the HP-render phase.
- The HP-encoding difference (u16 ally @0x10/0x14, u32 enemy) means the `cmp eax, 0x1869F` compare is only meaningful on the enemy path; the ally path reads `movzx eax, word ptr [esi+0x10]` and never exceeds 9999, so the >99,999 branch is effectively dead code for allies.

---

## Recommended live-memory validation steps for BlindGuyNW

For each of the three questions, the cleanest live-confirmation steps in your accessibility mod's debug build are:

1. **Status offset (Q1):**
   - Set a hardware read breakpoint on `entity_base + 0x4C` (DWORD).
   - Scan a Cactuar (high Death resistance, low everything else) and a Malboro (high statuses) on adjacent turns.
   - The instruction inside `sub_84F860` that fires is the per-status read; the immediate in the following `cmp` is the threshold (expected 0x64 = 100 or 0xFF = 255).
   - As secondary confirmation, dump 20 bytes from `entity_base + 0x4C..0x5F` for both enemies and verify the order matches the FFRTT wiki list (Death, Poison, Petrify, Darkness, ...).

2. **Elemental thresholds (Q2):**
   - Scan a Bomb (Fire absorb, Ice weak), a Blue Dragon (Ice absorb, Fire weak), a Geezard (mostly neutral).
   - Read 8 u16 at `entity_base + 0x3C`.
   - Tabulate which value maps to which Scan-UI bucket; bucket boundaries are then known exactly.

3. **Hidden-HP whitelist (Q3):**
   - Scan an arbitrary normal enemy first to anchor the breakpoint inside `sub_84F860`'s HP-render block.
   - Then Scan Fastitocalon-F (easily reachable on disc 1) — the diverging branch *is* the whitelist.
   - Read the immediates from the `cmp eax, IMM` chain; those are the canonical monster_ids.
   - Cross-check by scanning Adel — the same set of immediates should fire one of them.
   - Once you have the in-exe addresses of the immediates, you can read them at mod-load time so the mod auto-tracks any enemy-data mods that change monster_ids (e.g., Ragnarok mod).

---

## Source quality and reconciliation notes

- **FFNx canary `ff8_data.cpp` (master branch, GitHub julianxhokaxhiu/FFNx):** authoritative for the Scan call chain `sub_84D110 → … → sub_84F860 → sub_84F8D0 → scan_get_text_sub_B687C0` and for the data pointers `battle_entities_1D27BCB`, `scan_text_positions`, `scan_text_data`. **It does not publish entity-struct field offsets beyond what the user already has BAT-validated.** I attempted to fetch `ff8.h` and `ff8/battle/effects.h` directly; GitHub rendered-page fetches were blocked in this session, but the relevant lines from `ff8_data.cpp` are quoted above and confirm `FF8BattleEffect::Scan` and the chain.
- **FFRTT wiki `FF8/FileFormat_DAT` (Mirex, JWP, random_npc, myst6re, HobbitDur):** authoritative for Section 7's 20-byte status-resistance order at file-offset 360 and the 8-byte elemental-resistance order at file-offset 352. The runtime engine widens elemental from 8×u8 → 8×u16 (anchored at 800) but preserves status as 20×u8.
- **Fandom Scan (FF8) article:** authoritative for the named hidden-HP whitelist and the `>99,999` numeric rule.
- **Fandom FF8 elements article:** authoritative for the 800/900/1000 elemental-defense scale.
- **Fandom FF8 statuses article:** confirms the `StatusDefense >= 200 → immunity` rule, which together with `StatusDefense = 100 + .dat_byte` means a .dat byte of `>=100` produces full immunity — supporting the predicted Scan threshold of `cmp al, 0x64`.
- **Conflict to be aware of:** Some community discussions on GameFAQs and Eyes-on-FF describe the resistance bytes as "percentages" 0..100; others describe them as 0..255 with junction adds. The FFRTT wiki and IFRIT both treat the byte as 0..255 unsigned. Both views are reconcilable: the *display* the Scan UI computes is "Strong vs X" iff the byte is large enough that the damage path will refuse the status — i.e., `byte >= 100` after factoring junctions, which on a raw enemy is a single `cmp al, 0x64`.

The single biggest unknown that requires your live debugger to settle definitively is the exact threshold immediate in the Scan-UI status loop (0x64 vs 0xFF vs something else). Everything else above is either layout-derivable or already documented; the threshold is the one constant the public sources do not pin down.
