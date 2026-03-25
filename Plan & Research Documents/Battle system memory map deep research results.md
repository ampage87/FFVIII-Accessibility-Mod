# FF8 Steam 2013 battle system memory map

The runtime battle system uses **two separate memory regions** for combat data: a **0xD0-byte entity array** at `0x1D27B18` shared by allies and enemies, and the **0x1D0-byte computed stats block** at `0x1CFF000` for party characters only. This report maps both structures field-by-field with confirmed addresses for the English Steam 2013 build (`FF8_EN.exe`), drawn primarily from the ff8-speedruns/ff8-memory Cheat Engine tables, the Qhimm wiki (wiki.ffrtt.ru), the Doomtrain kernel.bin documentation, and the FFNx source code. All multi-byte values are **little-endian**. Addresses below are process virtual addresses (module base `0x400000` + offset).

Note on sources: community ff8-speedruns addresses are expressed as "FF8_EN.exe + offset" (module-relative). To convert, add `0x400000`. For example, `+0x1927B18` becomes process VA `0x1D27B18`. The user's confirmed addresses (e.g., `0x1CFF000`) are already process VAs.

---

## 1. The 0xD0 battle entity array is your primary runtime data source

**Source: ff8-speedruns/ff8-memory v1.4+ (Steam 2013 English) — HIGH confidence**

The battle engine maintains a contiguous array of **7 entity structs**, each **0xD0 (208) bytes**, starting at process VA **`0x1D27B18`**. This is the array that `battle_char_struct_dword_1D27B10` (a `BYTE**` pointer at `0x1D27B10`) points to.

| Slot | Role | Base Address (VA) | Module-Relative |
|------|------|-------------------|-----------------|
| 0 | Ally 1 | `0x1D27B18` | `+0x1927B18` |
| 1 | Ally 2 | `0x1D27BE8` | `+0x1927BE8` |
| 2 | Ally 3 | `0x1D27CB8` | `+0x1927CB8` |
| 3 | Enemy 1 | `0x1D27D88` | `+0x1927D88` |
| 4 | Enemy 2 | `0x1D27E58` | `+0x1927E58` |
| 5 | Enemy 3 | `0x1D27F28` | `+0x1927F28` |
| 6 | Enemy 4 | `0x1D27FF8` | `+0x1927FF8` |

Party slots 0–2 map directly to slots 0–2 in the computed stats block at `0x1CFF000`. Enemy slots accommodate up to 4 simultaneous enemies (the game supports up to 8 loaded via scene.out, but max 4 are visible/targetable at once, with AI scripts toggling visibility).

**Critical data type difference**: allies store HP and ATB as **uint16** while enemies use **uint32** at the same structural offsets. Your mod must branch on entity type when reading these fields.

### Complete 0xD0 entity struct layout

| Offset | Size | Type | Field | Notes |
|--------|------|------|-------|-------|
| **+0x00** | 1 | bitfield | **Timed status byte 1** | b0=Sleep, b1=Haste, b2=Slow, b3=Stop, b4=Regen, b5=Protect, b6=Shell, b7=Reflect |
| **+0x01** | 1 | bitfield | **Timed status byte 2** | b0=Aura, b1=Curse, b2=Doom, b3=Invincible, b4=Gradual Petrify, b5=Float, b6=Confuse |
| **+0x02** | 1 | bitfield | **Timed status byte 3** | b0=Eject(?), b1=Double, b2=Triple, b3=Defend, b6=Retribution(?) |
| **+0x03** | 1 | bitfield | **Timed status byte 4** | b0=Vit 0, b1=Angel Wing |
| +0x04 | 1 | uint8 | Battle status (general) | Purpose not fully documented |
| +0x05–0x07 | 3 | — | Padding/unknown | — |
| **+0x08** | 2/4 | uint16 (ally) / uint32 (enemy) | **Max ATB** | Maximum ATB gauge value |
| +0x0A | 2 | — | Padding (allies) | Enemies use 4 bytes at +0x08 |
| **+0x0C** | 2/4 | uint16 (ally) / uint32 (enemy) | **Current ATB** | When current ≥ max, character is ready |
| +0x0E | 2 | — | Padding (allies) | — |
| **+0x10** | 2/4 | uint16 (ally) / uint32 (enemy) | **Current HP** | — |
| +0x12 | 2 | — | Padding (allies) | — |
| **+0x14** | 2/4 | uint16 (ally) / uint32 (enemy) | **Max HP** | — |
| +0x16–0x3B | varies | — | Unknown/internal fields | May contain animation state, targeting info |
| **+0x3C** | 2 | uint16 | Elem resist: Fire | 500=weak, 800=neutral, 900=null, 1000=absorb |
| **+0x3E** | 2 | uint16 | Elem resist: Ice | Same scale |
| **+0x40** | 2 | uint16 | Elem resist: Thunder | Same scale |
| **+0x42** | 2 | uint16 | Elem resist: Earth | Same scale |
| **+0x44** | 2 | uint16 | Elem resist: Poison | Same scale |
| **+0x46** | 2 | uint16 | Elem resist: Wind | Same scale |
| **+0x48** | 2 | uint16 | Elem resist: Water | Same scale |
| **+0x4A** | 2 | uint16 | Elem resist: Holy | Same scale |
| **+0x4C** | 2 | uint16 | Sleep timer | 64425 (0xFBC9) = inactive sentinel |
| **+0x4E** | 2 | uint16 | Haste timer | Same sentinel convention |
| **+0x50** | 2 | uint16 | Slow timer | — |
| **+0x52** | 2 | uint16 | Stop timer | — |
| **+0x54** | 2 | uint16 | Regen timer | — |
| **+0x56** | 2 | uint16 | Protect timer | — |
| **+0x58** | 2 | uint16 | Shell timer | — |
| **+0x5A** | 2 | uint16 | Reflect timer | — |
| **+0x5C** | 2 | uint16 | Aura timer | — |
| **+0x5E** | 2 | uint16 | Curse timer | — |
| **+0x60** | 2 | uint16 | Doom timer | Counts down to KO |
| **+0x62** | 2 | uint16 | Hero/Invincible timer | — |
| **+0x64** | 2 | uint16 | Gradual Petrify timer | — |
| **+0x66** | 2 | uint16 | Float timer | — |
| +0x68 | 2 | uint16 | Unknown timer 1 | — |
| +0x6A | 2 | uint16 | Unknown timer 2 | — |
| +0x6C–0x77 | varies | — | Unknown | — |
| **+0x78** | 1 | bitfield | **Persistent status flags** | b0=KO, b1=Poison, b2=Petrify, b3=Darkness/Blind, b4=Silence, b5=Berserk, b6=Zombie |
| +0x79–0x7F | varies | — | Unknown | — |
| **+0x80** | 1 | uint8 | Last attacker | Battle position ID of last entity to hit this one |
| +0x81–0xB3 | varies | — | Unknown/internal | May include action state, command queue |
| **+0xB4** | 1 | uint8 | **Level** | Computed level at battle start |
| **+0xB5** | 1 | uint8 | **STR** | Computed (base + junction bonuses) |
| **+0xB6** | 1 | uint8 | **VIT** | — |
| **+0xB7** | 1 | uint8 | **MAG** | — |
| **+0xB8** | 1 | uint8 | **SPR** | — |
| **+0xB9** | 1 | uint8 | **SPD** | Determines ATB fill rate |
| **+0xBA** | 1 | uint8 | **LCK** | — |
| **+0xBB** | 1 | uint8 | **EVA** | — |
| **+0xBC** | 1 | uint8 | **HIT%** | — |
| +0xBD–0xC1 | varies | — | Unknown | — |
| **+0xC2** | 1 | uint8 | **Crisis Level** | 0–4; controls Limit Break availability |
| +0xC3–0xCF | varies | — | Unknown | — |

For your TTS mod, the **most useful fields per entity** are: Current HP (+0x10), Max HP (+0x14), Level (+0xB4), persistent statuses (+0x78), timed statuses (+0x00–0x03), ATB gauge (+0x0C vs +0x08), and Crisis Level (+0xC2).

---

## 2. The 0x1D0 computed stats block holds extended character data

**Base: `0x1CFF000`, stride: `0x1D0` (464 bytes), 3 party slots — confirmed by FFNx**

This larger structure contains the full computed character data including junction results, magic inventory, and equipped commands. Only your two confirmed fields are documented at specific offsets:

| Offset | Size | Type | Field | Confidence |
|--------|------|------|-------|------------|
| +0x172 | 2 | uint16 | Current HP | User-confirmed |
| +0x174 | 2 | uint16 | Max HP | User-confirmed |

FFNx treats this struct as opaque (`ff8_char_computed_stats`), and no public source fully maps all 464 bytes. However, based on the **savemap character struct** (152 bytes at savemap + 0x48C), we know the game stores per-character: base stats, magic inventory (32 slots × 2 bytes), 3 equipped commands, junction assignments, GF compatibility, and ability flags. The 0x1D0 struct likely expands this with computed totals (base + junction bonuses), runtime ATB state, and animation data.

**Savemap character data** (for reading persistent data like equipped commands and junctions) starts at process VA **`0x1CFE0E8`** (Squall, character index 0), with **0x98 (152) bytes** per character in order: Squall, Zell, Irvine, Quistis, Rinoa, Selphie, Seifer, Edea. Key savemap-relative offsets within each 0x98-byte character struct:

| Offset | Size | Field |
|--------|------|-------|
| +0x00 | 2 | Current HP |
| +0x02 | 2 | Max HP |
| +0x04 | 4 | Experience |
| +0x08 | 1 | Model ID |
| +0x09 | 1 | Weapon ID |
| +0x0A–0x0F | 6×1 | Base stats: STR, VIT, MAG, SPR, SPD, LCK |
| +0x10 | 64 | Magic inventory (32 slots: magic_id byte + qty byte) |
| **+0x50** | **3** | **Equipped commands** (3 junction command slot IDs) |
| +0x54 | 4 | Abilities (bitfield) |
| +0x58 | 2 | Junctioned GFs (16-bit bitmask, 1 bit per GF) |
| +0x5C–0x64 | 9×1 | Junction stat assignments: HP-J, STR-J, VIT-J, MAG-J, SPR-J, SPD-J, EVA-J, HIT-J, LCK-J |
| +0x65 | 1 | Elemental attack junction |
| +0x66 | 1 | Status attack junction |
| +0x67 | 4 | Elemental defense junction (4 slots) |
| +0x6B | 4 | Status defense junction (4 slots) |
| +0x70 | 32 | GF compatibility (16 GFs × 2 bytes each) |
| +0x94 | 1 | Exists flag |

**Character ID mapping**: The party formation array at savemap + 0xAF0 (user-confirmed) tells you which character IDs (0=Squall through 7=Edea) occupy the 3 active party slots. Read this to map entity array indices 0–2 to character names.

---

## 3. ATB fills from zero to max, with SPD as the primary driver

The ATB gauge lives inside the 0xD0 entity struct at **+0x08 (max)** and **+0x0C (current)**. For allies these are uint16; for enemies, uint32. A character's turn arrives when `current_ATB >= max_ATB`.

**ATB fill rate** is determined by the entity's SPD stat (+0xB9). Haste doubles the fill rate (bit 1 at +0x00), Slow halves it (bit 2 at +0x00), and Stop freezes it (bit 3 at +0x00). The **Battle Speed** config setting (adjustable in-game) scales the global tick rate but does not change max ATB values. In **Wait mode**, all ATB gauges pause when any player is in a sub-menu (Magic/Item/GF/Draw selection). In **Active mode**, gauges continue filling.

**For your TTS mod**: poll `current_ATB` against `max_ATB` each frame. When `current >= max` and the character is not KO'd (bit 0 of +0x78 is clear), announce "Squall's turn" (or equivalent). The `battle_current_active_character_id` byte pointer from FFNx can also confirm which character's menu is currently active.

There is **no separate "is_ready" flag** — readiness is purely `current_ATB >= max_ATB`. However, `battle_current_active_character_id` and `battle_new_active_character_id` (from FFNx, resolved from `sub_4BB840`) track which character's command menu is open, distinguishing "selecting" from "waiting."

---

## 4. Battle command menu state and command IDs

### Command IDs (from kernel.bin Section 0)

Each battle command is 8 bytes in kernel.bin. The IDs used in the character's equipped commands (savemap +0x50) and in the runtime menu are:

| ID | Command | ID | Command |
|----|---------|----|---------| 
| 0x01 | Attack | 0x02 | Magic |
| 0x03 | GF | 0x04 | Draw |
| 0x05 | Item | 0x06 | Card |
| 0x07 | Devour | 0x0F | MiniMog |
| 0x10 | Defend | 0x11 | Darkside |
| 0x12 | Recover | 0x13 | Absorb |
| 0x14 | Revive | 0x15 | LV Down |
| 0x16 | LV Up | 0x17 | Kamikaze |
| 0x18 | Expendx2-1 | 0x19 | Expendx3-1 |
| 0x1A | Mad Rush | 0x1B | Doom |
| 0x1C | Slot (Selphie) | 0x1D | Duel (Zell) |
| 0x1E | Shot (Irvine) | 0x1F | Blue Magic (Quistis) |
| 0x20 | Combine (Rinoa) | 0x21 | Renzokuken (Squall) |
| 0x24 | Mug | 0x26 | Treatment |

**Source: Doomtrain kernel.bin wiki — MEDIUM-HIGH confidence** (exact IDs for limit breaks and higher commands should be verified against the wiki at github.com/DarkShinryu/doomtrain/wiki/Battle-commands).

Each character's visible command menu in battle consists of **Attack** (always present) plus up to **3 junctioned commands** read from savemap character struct offset +0x50 (3 bytes). Limit Breaks replace the Attack command when Crisis Level > 0 and the trigger RNG check passes.

### Menu state tracking

**`battle_menu_state`** (resolved from `battle_pause_window_sub_4CD350 + 0x29`) tracks the current menu phase. **`battle_current_active_character_id`** (BYTE*) tells you which party slot's menu is open. The exact values of the menu state variable and sub-menu cursor positions are **not publicly documented** at specific memory offsets — this is a gap that requires IDA/Ghidra analysis of `battle_menu_sub_4A6660`, `battle_menu_sub_4A3D20`, and `battle_menu_sub_4A3EE0` from FFNx.

**Target byte flags** from kernel.bin command definitions indicate targeting behavior:
- 0x01: Cursor starts on enemy
- 0x02: Can target allies
- 0x04: Toggleable (switch enemies/allies)
- 0x08: Single-target
- 0x10: Multi-target
- 0x20: Random target
- 0x40: Can target dead units

---

## 5. Enemy data combines DAT files with runtime entity structs

Enemies share the **same 0xD0 entity struct** as allies (slots 3–6 in the array at `0x1D27D88` through `0x1D27FF8`). All fields — HP, stats, statuses, ATB, elemental resistances — are at the same offsets. The only differences are the uint32 vs uint16 data types for HP/ATB fields.

### Enemy names

Enemy names are stored as 24-byte FF8-encoded strings in the **monster DAT file** (c0m*XXX*.dat, Section 7, offset 0x00). At runtime, `battle_get_monster_name_sub_495100` retrieves names by reading from the loaded DAT data. The name is **not inline** in the entity struct — it is accessed via a pointer dereference through `battle_char_struct_dword_1D27B10`.

### How many enemies are active

The encounter data at scene.out offset 0x04–0x07 contains bitmasks for **visible**, **loaded**, **targetable**, and **count** enemies. At runtime, check entity HP > 0 and the targetable flag. The game supports up to 8 loaded enemies per encounter, but **max 4 are visible simultaneously** (exceeding this crashes the game). Boss fights with more use AI scripts to toggle visibility.

### Enemy level scaling

FF8 enemies scale to party level. The **scene.out level byte** (offset 0x78, 1 per enemy slot) controls scaling: **255** = standard (match average party level), **1–100** = fixed level, **101–199** = capped maximum, and special values (251=final bosses, 252=Ultimecia Castle, 254=scripted). The computed level appears at entity struct **+0xB4**.

### Enemy stats from DAT Section 7

The on-disk format uses 4-byte level-scaling formula parameters for HP, STR, VIT, MAG, SPR, SPD, EVA (offsets 24–51 in Section 7). At runtime, the engine resolves these into the single-byte computed stats at entity offsets +0xB5 through +0xBC.

---

## 6. Battle result data has confirmed addresses for EXP, AP, and prizes

**Source: ff8-speedruns/ff8-memory — HIGH confidence**

| Field | Process VA | Module-Relative | Type |
|-------|-----------|-----------------|------|
| Battle result state | `0x1CFF6E7` | `+0x18FF6E7` | uint8 (2=escaped, 4=won) |
| **Ally 1 XP earned** | `0x1CFF574` | `+0x18FF574` | uint16 |
| **Ally 2 XP earned** | `0x1CFF576` | `+0x18FF576` | uint16 |
| **Ally 3 XP earned** | `0x1CFF578` | `+0x18FF578` | uint16 |
| Ally 1 XP earned (extra) | `0x1CFF57A` | `+0x18FF57A` | uint16 |
| Ally 2 XP earned (extra) | `0x1CFF57C` | `+0x18FF57C` | uint16 |
| Ally 3 XP earned (extra) | `0x1CFF57E` | `+0x18FF57E` | uint16 |
| **GF AP earned** | `0x1CFF5C0` | `+0x18FF5C0` | uint16 |
| GF XP per GF (×16) | `0x1CFF580`–`0x1CFF59E` | `+0x18FF580`–`+0x18FF59E` | uint16 each |
| **Prize #1 Item ID** | `0x1CFF5E0` | `+0x18FF5E0` | uint8 |
| Prize #1 Quantity | `0x1CFF5E1` | `+0x18FF5E1` | uint8 |
| Prize #2 ID | `0x1CFF5E2` | `+0x18FF5E2` | uint8 |
| Prize #2 Qty | `0x1CFF5E3` | `+0x18FF5E3` | uint8 |
| Prize #3 ID | `0x1CFF5E4` | `+0x18FF5E4` | uint8 |
| Prize #3 Qty | `0x1CFF5E5` | `+0x18FF5E5` | uint8 |
| Prize #4 ID | `0x1CFF5E6` | `+0x18FF5E6` | uint8 |
| Prize #4 Qty | `0x1CFF5E7` | `+0x18FF5E7` | uint8 |
| In post-battle screen | `0x1A78CA4` | `+0x1678CA4` | uint8 (bool) |

**Gil earned** is not stored separately in the battle result block — it is calculated from enemy base EXP values and added directly to the savemap Gil counter at `0x1CFE764` (module-relative `+0x18FE764`). The base EXP per enemy is in DAT Section 7 at offsets 256 (extra EXP, uint16) and 258 (EXP, uint16), with APs at offset 335 (uint8).

The **result screen phase** is tracked by `battle_check_won_sub_486500` and related functions, but no single address for the phase counter has been publicly documented. The `In post-battle screen` boolean at `0x1A78CA4` can tell your mod when to read and announce results.

---

## 7. Draw lists live in a separate memory region outside the entity array

**Source: ff8-speedruns/ff8-memory — HIGH confidence**

Enemy draw spells are stored in a **separate memory block** (not inside the 0xD0 entity struct). Each enemy has 4 draw slots with **4-byte spacing** between slots and a **0x47 stride** between enemies:

| Enemy | Draw Slot 1 (VA) | Draw Slot 2 | Draw Slot 3 | Draw Slot 4 |
|-------|-----------------|-------------|-------------|-------------|
| Enemy 1 | `0x1D28F18` | `0x1D28F1C` | `0x1D28F20` | `0x1D28F24` |
| Enemy 2 | `0x1D28F5F` | `0x1D28F63` | `0x1D28F67` | `0x1D28F6B` |
| Enemy 3 | `0x1D28FA6` | `0x1D28FAA` | `0x1D28FAE` | `0x1D28FB2` |
| Enemy 4 | `0x1D28FED` | `0x1D28FF1` | `0x1D28FF5` | `0x1D28FFF` |

Each slot is a **1-byte magic ID** (see magic ID list below). A value of **0x00** means empty/no draw. The draw list **is readable before the player uses Draw** — it is populated when the battle loads from the enemy DAT file data. This means your mod can proactively announce "Enemy has Fire, Blizzard, Cure available to draw."

### On-disk draw data (DAT Section 7)

Three level tiers, each 8 bytes (4 slots × 2 bytes: magic_id + unused_qty):
- Low level: offset 260 (0x104)
- Medium level: offset 268 (0x10C)
- High level: offset 276 (0x114)

The engine selects the tier based on the enemy's computed level vs thresholds at DAT offsets 244–245.

### `battle_get_draw_magic_amount_48FD20(int, int, int)`

Based on the function's context in FFNx, the three parameters are most likely: **(1) drawing character's party slot index**, **(2) target enemy's entity index**, **(3) magic slot index (0–3)**. The function calculates how many copies of the spell are drawn based on the character's MAG stat, level, and draw resistance. **Confidence: MEDIUM** — inferred from calling patterns, not confirmed by disassembly.

---

## 8. Runtime status effects use a two-part system with separate timers

The runtime representation in the 0xD0 entity struct splits statuses into **persistent** (byte at +0x78) and **timed** (bytes at +0x00 through +0x03), each with their own bit layout. This differs from the kernel.bin file format (Statuses 0 + Statuses 1) used in spell/item definitions.

### Persistent status byte (+0x78) — survive battle, not timed

| Bit | Mask | Status |
|-----|------|--------|
| 0 | 0x01 | **KO (Death)** |
| 1 | 0x02 | Poison |
| 2 | 0x04 | Petrify |
| 3 | 0x08 | Darkness/Blind |
| 4 | 0x10 | Silence |
| 5 | 0x20 | Berserk |
| 6 | 0x40 | Zombie |

**KO detection**: bit 0 of +0x78 is the dead/alive flag. If set, the character is KO'd. This is your primary "is alive" check.

### Timed status bytes (+0x00 through +0x03) — temporary, with countdown timers

**Byte 0 (+0x00):**
| Bit | Mask | Status | Timer at |
|-----|------|--------|----------|
| 0 | 0x01 | Sleep | +0x4C |
| 1 | 0x02 | Haste | +0x4E |
| 2 | 0x04 | Slow | +0x50 |
| 3 | 0x08 | Stop | +0x52 |
| 4 | 0x10 | Regen | +0x54 |
| 5 | 0x20 | Protect | +0x56 |
| 6 | 0x40 | Shell | +0x58 |
| 7 | 0x80 | Reflect | +0x5A |

**Byte 1 (+0x01):**
| Bit | Mask | Status | Timer at |
|-----|------|--------|----------|
| 0 | 0x01 | Aura | +0x5C |
| 1 | 0x02 | Curse | +0x5E |
| 2 | 0x04 | Doom | +0x60 |
| 3 | 0x08 | Invincible | +0x62 |
| 4 | 0x10 | Gradual Petrify | +0x64 |
| 5 | 0x20 | Float | +0x66 |
| 6 | 0x40 | Confuse | — |

**Byte 2 (+0x02):**
| Bit | Mask | Status |
|-----|------|--------|
| 0 | 0x01 | Eject (unconfirmed) |
| 1 | 0x02 | Double |
| 2 | 0x04 | Triple |
| 3 | 0x08 | Defend |
| 6 | 0x40 | Retribution (unconfirmed) |

**Byte 3 (+0x03):**
| Bit | Mask | Status |
|-----|------|--------|
| 0 | 0x01 | Vit 0 (Meltdown) |
| 1 | 0x02 | Angel Wing |

**Timer sentinel value**: **64425 (0xFBC9)** means the timer is inactive. Any other value is a live countdown. For Doom, the timer counts down to 0, then KO is inflicted.

### Kernel.bin status bitmask format (for file data interpretation)

The kernel.bin uses **Statuses 0** (uint16) and **Statuses 1** (uint32) in magic/item/attack definitions. These have a **different bit ordering** from the runtime struct. The Statuses 0 bits 0–7 match the persistent byte order (Death, Poison, Petrify, Darkness, Silence, Berserk, Zombie, Sleep), with bits 8–15 continuing through the timed statuses. The exact bit mapping for bits 8–15 of Statuses 0 and all of Statuses 1 should be verified against the Doomtrain wiki page at `github.com/DarkShinryu/doomtrain/wiki/Statuses-0`.

---

## 9. Targeting, GF summons, limit breaks, and other battle subsystems

### Targeting system

When selecting a target, the cursor position is managed by the battle menu subsystem. No single public address for "current target index" has been documented. However, target **validity** is determined by the encounter's targetable-enemy bitmask (scene.out offset 0x06) and entity alive state (HP > 0 and KO bit clear). The kernel.bin **target byte** in each command's definition (see Section 4) determines whether the command targets enemies, allies, single, multi, or dead units.

For your TTS mod, you can determine valid targets by iterating the entity array and checking which enemy slots have HP > 0. Map entity slot indices 3–6 to enemy names via `battle_get_monster_name_sub_495100`.

### GF summon system

| Field | Address (VA) | Type | Notes |
|-------|-------------|------|-------|
| **GF Boost gauge** | `0x20DCEF0` | uint8 | 0–255; max effective 250 (= 250% damage) |
| Currently summoned GF | Not publicly documented | — | Check `battle_magic_id` (int*) during effect execution |

During a GF summon, the **summoning character's HP bar is replaced with the GF's HP**. GF HP is tracked in the savemap GF struct (+0x12, uint16 within the 68-byte GF block starting at savemap + 0x4C). The GF takes damage instead of the character until the animation completes. GF summon animation state can be detected by checking `func_off_battle_effects_C81774` — when a GF effect ID (e.g., Quezacotl=115, Shiva=184, Ifrit=200, Leviathan=5) is active in `battle_magic_id`.

### Limit break system

**Crisis Level** at entity struct **+0xC2** (uint8, 0–4) is the key field. It is recalculated each time a character's ATB fills. The formula:

`CrisisValue = DeathLimit + StatusLimit + FuryLimit + HPLimit`

If `CrisisValue >= (random(0..255) + 1)`, the Limit Break trigger becomes available. **Aura status** (timed byte 1, bit 0) dramatically increases CrisisValue regardless of HP, making Crisis Level 4 nearly guaranteed above 11% HP.

| Limit Break Subsystem | Address/Info |
|-----------------------|-------------|
| Crisis Level | Entity +0xC2 (uint8) |
| **Zell Duel timer** | `0x1D76750` (uint16, counts down) |
| Zell Duel durations | CL1=4.66s, CL2=6.66s, CL3=9.33s, CL4=12s |
| Selphie Slot spell | Determined by RNG + Crisis Level; no confirmed address |
| Renzokuken trigger bar | Not publicly documented; vibration hook at `0x4A29A0` |
| Renzokuken hit count | CL-dependent: CL1=4 hits, CL4=up to 8 hits |

### Battle dialog and text

Battle strings (enemy dialog, "Drew [Fire]", etc.) are loaded from two sources: **kernel.bin text sections** (for system messages like spell names) and **enemy DAT Section 8** (for enemy-specific battle dialog). At runtime, text passes through `battle_get_actor_name_sub_47EAF0` for actor names and `scan_get_text_sub_B687C0` for Scan text. Battle text uses FF8's proprietary string encoding.

The **battle text buffer** address is not publicly documented at a fixed location. The `scan_text_data` and `scan_text_positions` pointers (from FFNx) give access to Scan-specific text.

---

## 10. Encounter data maps encounter IDs to enemy composition

**Source: wiki.ffrtt.ru/FF8/BattleStructure — HIGH confidence**

The encounter ID at **`0x1CFF6E0`** (WORD) is an index into scene.out, a headerless file of **1024 × 128-byte** encounter blocks in battle.fs. The encounter can be read before battle fully initializes (the ID is set during the battle transition).

### Scene.out encounter struct (128 bytes)

| Offset | Size | Description |
|--------|------|-------------|
| 0x00 | 1 | Battle scenario (stage model index) |
| 0x01 | 1 | **Flags**: +1=no escape, +2=no fanfare, +4=countdown timer, +8=no XP/items, +16=current music, +32=preemptive, +64=back attack |
| 0x04 | 1 | Visible enemies (bitmask: bit 7=enemy 1, bit 0=enemy 8) |
| 0x05 | 1 | Loaded/active enemies (bitmask) |
| 0x06 | 1 | Targetable enemies (bitmask) |
| 0x07 | 1 | Enemy count (bitmask) |
| 0x08 | 48 | Enemy coordinates (8 × {int16 x, int16 y, int16 z}) |
| **0x38** | **8** | **Enemy IDs** (1 byte each; map to c0m{hex(ID+0x10)}.dat) |
| 0x78 | 8 | **Enemy levels** (1 byte each; 255=standard scaling) |

### Element bitmask (1 byte, used throughout kernel.bin)

| Bit | Element | Bit | Element |
|-----|---------|-----|---------|
| 0 (0x01) | Fire | 4 (0x10) | Poison |
| 1 (0x02) | Ice | 5 (0x20) | Wind |
| 2 (0x04) | Thunder | 6 (0x40) | Water |
| 3 (0x08) | Earth | 7 (0x80) | Holy |

### Elemental resistance scale (uint16, in entity struct +0x3C–0x4A)

**500** = weak (takes extra damage), **800** = neutral, **900** = nullified, **1000** = absorbed. Values between indicate partial resistance.

---

## 11. Additional confirmed global addresses

| Description | Process VA | Module-Relative | Type |
|-------------|-----------|-----------------|------|
| Savemap base | `0x1CFDC5C` | `+0x18FDC5C` | — |
| Current Gil | `0x1CFE764` | `+0x18FE764` | uint32 |
| Battles won | `0x1CFE9CC` | `+0x18FE9CC` | uint16 |
| Battles escaped | `0x1CFE9D2` | `+0x18FE9D2` | uint16 |
| Total enemies killed | `0x1CFE9FC` | `+0x18FE9FC` | uint32 |
| Story progress | `0x1CFEAB8` | `+0x18FEAB8` | uint16 |
| Play time (seconds) | `0x1CFE928` | `+0x18FE928` | uint32 |
| Item inventory base | `0x1CFE79C` | `+0x18FE79C` | 198 × {uint8 id, uint8 qty} |
| Encounter count | `0x1CDBFEC` | `+0x18DBFEC` | uint16 |
| In-menu flag | `0x1D76358` | `+0x1976358` | uint8 |
| In-FMV flag | `0x20DA7A0` | `+0x1C9A7A0` | uint8 |
| Ult. Castle seals | `0x1CFF6E8` | `+0x18FF6E8` | bitfield (b0=Item, b1=Magic, b2=GF, b3=Draw, b4=CmdAbility, b5=Limit, b6=Resurrect, b7=Save) |
| Field variable block | `0x18FE9B8` | — | Base for PSHM/POPM opcodes |

---

## Key gaps and recommended next steps

Several areas remain undocumented in public sources and will require your own IDA/Ghidra analysis of `FF8_EN.exe`:

- **Full 0x1D0 computed stats struct layout** — Only +0x172/+0x174 (HP) are confirmed. The character ID, equipped commands at runtime, and junction data within this struct need disassembly of `compute_char_stats_sub_495960`.
- **Battle menu cursor position** — The current command selection, sub-menu cursor (which spell/item/GF is highlighted), and target cursor index are managed by the `battle_menu_sub_4A6660` family of functions. Hook these to announce menu navigation.
- **Battle result screen phase** — No single phase counter address is documented. Monitor `In post-battle screen` (`0x1A78CA4`) and read the XP/AP/prize fields when it becomes true.
- **Entity struct bytes +0x05–0x07, +0x16–0x3B, +0x6C–0x77, +0x81–0xB3, +0xBD–0xC1, +0xC3–0xCF** — These unknown ranges likely contain animation state, action state (idle/attacking/casting/defending), current command being executed, and targeting data. Tracing `battle_ai_opcode_sub_487DF0` and the battle main loop will reveal these.
- **GF HP during summon** — The runtime GF HP display during summoning likely uses the savemap GF struct HP field at savemap + 0x4C + (GF_index × 0x44) + 0x12, but this needs confirmation.

The **ff8-speedruns/ff8-memory** Cheat Engine .CT files (available at github.com/ff8-speedruns/ff8-memory/releases) contain pointer chains that may resolve some of these gaps. Additionally, the **OpenFF8** project by Extapathy (github.com/Extapathy/OpenFF8) defines `ff8vars` and `ff8funcs` structs in `memory.h` that may contain additional battle field mappings.

### Savemap header offset note

Your confirmed 76-byte (0x4C) header places GFs at savemap + 0x4C and characters at savemap + 0x48C. The wiki documents GFs at +0x60 and characters at +0x4A0 (assuming a 96-byte header). The **correction factor is -0x14** (subtract 20 bytes from all wiki-documented post-header offsets). All addresses in this report use either your confirmed values or absolute process VAs from ff8-speedruns, so no correction is needed for the data presented here.