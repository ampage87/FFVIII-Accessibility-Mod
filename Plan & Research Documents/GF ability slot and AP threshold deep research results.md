# FF8 per-GF ability table (slot -> ability ID -> AP to learn)

Sources: Hyne save editor (myst6re/hyne, `src/Data.cpp`: `apsTab[116]`, `innateAbilities[16][22]`) for AP cost + per-GF slot order; Doomtrain wiki (DarkShinryu/doomtrain.wiki) for the unified 0-115 ability-ID namespace and kernel.bin GF-section structure. Cross-checked vs Final Fantasy Wiki (Fandom) and Gamer Guides.

**Anchor validation: ALL 5 ANCHORS PASS** (Quezacotl SumMag+30% slot 2 / 140; Shiva VIT-J slot 8 / 50; Ifrit GFHP+10% slot 3 / 40; Siren SumMag+20% slot 4 / 70; Diablos GFHP+10% slot 0 / 40).

## Key answers

- **AP-to-learn is a property of the ABILITY, not the (GF,slot) pair.** Every GF that can learn ability X needs the same AP for it (e.g. GFHP+10% = 40 on both Ifrit and Diablos). A single `ability_ap_cost[116]` table is sufficient; the per-GF table only supplies the slot ORDER.

- The AP cost is **not** in kernel.bin's GF section (that section stores ability ID + an *unlocker* = GF-level / prerequisite-ability gate, not AP). The AP threshold is a fixed per-ability value (Hyne mirrors the engine's table).

- Each GF has **22** ability slots (indices 0-21), matching the save's `APs[24]` (22 used + 2 unused). The last three slots are always the command abilities Magic(20)/GF(21)/Draw(22).


## Per-GF slot tables


### 0 Quezacotl

| Slot | Ability | Unified ID (dec / hex) | AP to learn |
|---|---|---|---|
| 0 | SumMag+10% | 83 / 0x53 | 40 |
| 1 | SumMag+20% | 84 / 0x54 | 70 |
| 2 | SumMag+30% | 85 / 0x55 | 140 |
| 3 | GFHP+10% | 87 / 0x57 | 40 |
| 4 | GFHP+20% | 88 / 0x58 | 70 |
| 5 | Boost | 91 / 0x5B | 10 |
| 6 | T Mag-RF | 97 / 0x61 | 30 |
| 7 | Mid Mag-RF | 112 / 0x70 | 60 |
| 8 | HP-J | 1 / 0x01 | 50 |
| 9 | Mag+20% | 48 / 0x30 | 60 |
| 10 | Mag+40% | 49 / 0x31 | 120 |
| 11 | Elem-Atk-J | 10 / 0x0A | 160 |
| 12 | VIT-J | 3 / 0x03 | 50 |
| 13 | Elem-Def-J | 12 / 0x0C | 100 |
| 14 | Elem-Defx2 | 14 / 0x0E | 130 |
| 15 | Card | 25 / 0x19 | 40 |
| 16 | Card Mod | 115 / 0x73 | 80 |
| 17 | MAG-J | 4 / 0x04 | 50 |
| 18 | Item | 23 / 0x17 | 1 |
| 19 | Magic | 20 / 0x14 | 1 |
| 20 | GF | 21 / 0x15 | 1 |
| 21 | Draw | 22 / 0x16 | 1 |

### 1 Shiva

| Slot | Ability | Unified ID (dec / hex) | AP to learn |
|---|---|---|---|
| 0 | SumMag+10% | 83 / 0x53 | 40 |
| 1 | SumMag+20% | 84 / 0x54 | 70 |
| 2 | SumMag+30% | 85 / 0x55 | 140 |
| 3 | GFHP+10% | 87 / 0x57 | 40 |
| 4 | GFHP+20% | 88 / 0x58 | 70 |
| 5 | Item | 23 / 0x17 | 1 |
| 6 | Boost | 91 / 0x5B | 10 |
| 7 | IMag-RF | 98 / 0x62 | 30 |
| 8 | VIT-J | 3 / 0x03 | 50 |
| 9 | Vit+20% | 45 / 0x2D | 60 |
| 10 | Vit+40% | 46 / 0x2E | 120 |
| 11 | Spr+20% | 51 / 0x33 | 60 |
| 12 | Spr+40% | 52 / 0x34 | 120 |
| 13 | Elem-Def-J | 12 / 0x0C | 100 |
| 14 | Elem-Defx2 | 14 / 0x0E | 130 |
| 15 | STR-J | 2 / 0x02 | 50 |
| 16 | Elem-Atk-J | 10 / 0x0A | 160 |
| 17 | Doom | 26 / 0x1A | 60 |
| 18 | SPR-J | 5 / 0x05 | 50 |
| 19 | Magic | 20 / 0x14 | 1 |
| 20 | GF | 21 / 0x15 | 1 |
| 21 | Draw | 22 / 0x16 | 1 |

### 2 Ifrit

| Slot | Ability | Unified ID (dec / hex) | AP to learn |
|---|---|---|---|
| 0 | SumMag+10% | 83 / 0x53 | 40 |
| 1 | SumMag+20% | 84 / 0x54 | 70 |
| 2 | SumMag+30% | 85 / 0x55 | 140 |
| 3 | GFHP+10% | 87 / 0x57 | 40 |
| 4 | GFHP+20% | 88 / 0x58 | 70 |
| 5 | GFHP+30% | 89 / 0x59 | 140 |
| 6 | Boost | 91 / 0x5B | 10 |
| 7 | FMag-RF | 99 / 0x63 | 30 |
| 8 | Ammo-RF | 107 / 0x6B | 30 |
| 9 | Str+20% | 42 / 0x2A | 60 |
| 10 | Str+40% | 43 / 0x2B | 120 |
| 11 | Elem-Atk-J | 10 / 0x0A | 160 |
| 12 | StrBonus | 66 / 0x42 | 100 |
| 13 | HP-J | 1 / 0x01 | 50 |
| 14 | Item | 23 / 0x17 | 1 |
| 15 | Elem-Def-J | 12 / 0x0C | 100 |
| 16 | Elem-Defx2 | 14 / 0x0E | 130 |
| 17 | MadRush | 27 / 0x1B | 60 |
| 18 | STR-J | 2 / 0x02 | 50 |
| 19 | Magic | 20 / 0x14 | 1 |
| 20 | GF | 21 / 0x15 | 1 |
| 21 | Draw | 22 / 0x16 | 1 |

### 3 Siren

| Slot | Ability | Unified ID (dec / hex) | AP to learn |
|---|---|---|---|
| 0 | GFHP+10% | 87 / 0x57 | 40 |
| 1 | GFHP+20% | 88 / 0x58 | 70 |
| 2 | Tool-RF | 108 / 0x6C | 30 |
| 3 | SumMag+10% | 83 / 0x53 | 40 |
| 4 | SumMag+20% | 84 / 0x54 | 70 |
| 5 | SumMag+30% | 85 / 0x55 | 140 |
| 6 | Boost | 91 / 0x5B | 10 |
| 7 | LMag-RF | 100 / 0x64 | 30 |
| 8 | STMed-RF | 106 / 0x6A | 30 |
| 9 | Mag+20% | 48 / 0x30 | 60 |
| 10 | Mag+40% | 49 / 0x31 | 120 |
| 11 | ST-Atk-J | 11 / 0x0B | 160 |
| 12 | MagBonus | 68 / 0x44 | 100 |
| 13 | Item | 23 / 0x17 | 1 |
| 14 | ST-Def-J | 13 / 0x0D | 100 |
| 15 | ST-Def-Jx2 | 16 / 0x10 | 130 |
| 16 | Treatment | 28 / 0x1C | 100 |
| 17 | Move-Find | 79 / 0x4F | 40 |
| 18 | MAG-J | 4 / 0x04 | 50 |
| 19 | Magic | 20 / 0x14 | 1 |
| 20 | GF | 21 / 0x15 | 1 |
| 21 | Draw | 22 / 0x16 | 1 |

### 4 Brothers

| Slot | Ability | Unified ID (dec / hex) | AP to learn |
|---|---|---|---|
| 0 | SumMag+10% | 83 / 0x53 | 40 |
| 1 | SumMag+20% | 84 / 0x54 | 70 |
| 2 | SumMag+30% | 85 / 0x55 | 140 |
| 3 | GFHP+10% | 87 / 0x57 | 40 |
| 4 | GFHP+20% | 88 / 0x58 | 70 |
| 5 | GFHP+30% | 89 / 0x59 | 140 |
| 6 | Boost | 91 / 0x5B | 10 |
| 7 | HP+20% | 39 / 0x27 | 60 |
| 8 | HP+40% | 40 / 0x28 | 120 |
| 9 | HP+80% | 41 / 0x29 | 240 |
| 10 | HPBonus | 65 / 0x41 | 100 |
| 11 | STR-J | 2 / 0x02 | 50 |
| 12 | Elem-Atk-J | 10 / 0x0A | 160 |
| 13 | SPR-J | 5 / 0x05 | 50 |
| 14 | Elem-Def-J | 12 / 0x0C | 100 |
| 15 | Item | 23 / 0x17 | 1 |
| 16 | Cover | 62 / 0x3E | 100 |
| 17 | Defend | 29 / 0x1D | 100 |
| 18 | HP-J | 1 / 0x01 | 50 |
| 19 | Magic | 20 / 0x14 | 1 |
| 20 | GF | 21 / 0x15 | 1 |
| 21 | Draw | 22 / 0x16 | 1 |

### 5 Diablos

| Slot | Ability | Unified ID (dec / hex) | AP to learn |
|---|---|---|---|
| 0 | GFHP+10% | 87 / 0x57 | 40 |
| 1 | GFHP+20% | 88 / 0x58 | 70 |
| 2 | GFHP+30% | 89 / 0x59 | 140 |
| 3 | TimeMag-RF | 101 / 0x65 | 30 |
| 4 | STMag-RF | 102 / 0x66 | 60 |
| 5 | HP-J | 1 / 0x01 | 50 |
| 6 | HP+20% | 39 / 0x27 | 60 |
| 7 | HP+40% | 40 / 0x28 | 120 |
| 8 | HP+80% | 41 / 0x29 | 240 |
| 9 | MAG-J | 4 / 0x04 | 50 |
| 10 | Mag+20% | 48 / 0x30 | 60 |
| 11 | Mag+40% | 49 / 0x31 | 120 |
| 12 | Item | 23 / 0x17 | 1 |
| 13 | HIT-J | 8 / 0x08 | 120 |
| 14 | Enc-Half | 80 / 0x50 | 30 |
| 15 | Enc-None | 81 / 0x51 | 100 |
| 16 | Darkside | 30 / 0x1E | 100 |
| 17 | Mug | 58 / 0x3A | 200 |
| 18 | Abilityx3 | 18 / 0x12 | 150 |
| 19 | Magic | 20 / 0x14 | 1 |
| 20 | GF | 21 / 0x15 | 1 |
| 21 | Draw | 22 / 0x16 | 1 |

### 6 Carbuncle

| Slot | Ability | Unified ID (dec / hex) | AP to learn |
|---|---|---|---|
| 0 | GFHP+10% | 87 / 0x57 | 40 |
| 1 | GFHP+20% | 88 / 0x58 | 70 |
| 2 | GFHP+30% | 89 / 0x59 | 140 |
| 3 | RecovMed-RF | 105 / 0x69 | 30 |
| 4 | Vit+20% | 45 / 0x2D | 60 |
| 5 | Vit+40% | 46 / 0x2E | 120 |
| 6 | VitBonus | 67 / 0x43 | 100 |
| 7 | HP-J | 1 / 0x01 | 50 |
| 8 | HP+20% | 39 / 0x27 | 60 |
| 9 | HP+40% | 40 / 0x28 | 120 |
| 10 | MAG-J | 4 / 0x04 | 50 |
| 11 | ST-Atk-J | 11 / 0x0B | 160 |
| 12 | ST-Def-J | 13 / 0x0D | 100 |
| 13 | ST-Def-Jx2 | 16 / 0x10 | 130 |
| 14 | Counter | 60 / 0x3C | 200 |
| 15 | Auto-Reflect | 72 / 0x48 | 250 |
| 16 | Item | 23 / 0x17 | 1 |
| 17 | Abilityx3 | 18 / 0x12 | 150 |
| 18 | VIT-J | 3 / 0x03 | 50 |
| 19 | Magic | 20 / 0x14 | 1 |
| 20 | GF | 21 / 0x15 | 1 |
| 21 | Draw | 22 / 0x16 | 1 |

### 7 Leviathan

| Slot | Ability | Unified ID (dec / hex) | AP to learn |
|---|---|---|---|
| 0 | GFHP+10% | 87 / 0x57 | 40 |
| 1 | GFHP+20% | 88 / 0x58 | 70 |
| 2 | GFHP+30% | 89 / 0x59 | 140 |
| 3 | SumMag+10% | 83 / 0x53 | 40 |
| 4 | SumMag+20% | 84 / 0x54 | 70 |
| 5 | SumMag+30% | 85 / 0x55 | 140 |
| 6 | Boost | 91 / 0x5B | 10 |
| 7 | SuptMag-RF | 103 / 0x67 | 30 |
| 8 | GFRecovMed-RF | 110 / 0x6E | 30 |
| 9 | Spr+20% | 51 / 0x33 | 60 |
| 10 | Spr+40% | 52 / 0x34 | 120 |
| 11 | SprBonus | 69 / 0x45 | 100 |
| 12 | SPR-J | 5 / 0x05 | 50 |
| 13 | Elem-Defx2 | 14 / 0x0E | 130 |
| 14 | MAG-J | 4 / 0x04 | 50 |
| 15 | Elem-Atk-J | 10 / 0x0A | 160 |
| 16 | Auto Potion | 74 / 0x4A | 150 |
| 17 | Recover | 31 / 0x1F | 200 |
| 18 | Item | 23 / 0x17 | 1 |
| 19 | Magic | 20 / 0x14 | 1 |
| 20 | GF | 21 / 0x15 | 1 |
| 21 | Draw | 22 / 0x16 | 1 |

### 8 Pandemona

| Slot | Ability | Unified ID (dec / hex) | AP to learn |
|---|---|---|---|
| 0 | SumMag+10% | 83 / 0x53 | 40 |
| 1 | SumMag+20% | 84 / 0x54 | 70 |
| 2 | SumMag+30% | 85 / 0x55 | 140 |
| 3 | GFHP+10% | 87 / 0x57 | 40 |
| 4 | GFHP+20% | 88 / 0x58 | 70 |
| 5 | GFHP+30% | 89 / 0x59 | 140 |
| 6 | Boost | 91 / 0x5B | 10 |
| 7 | SPD-J | 6 / 0x06 | 120 |
| 8 | Spd+20% | 54 / 0x36 | 150 |
| 9 | Spd+40% | 55 / 0x37 | 200 |
| 10 | Str+20% | 42 / 0x2A | 60 |
| 11 | Str+40% | 43 / 0x2B | 120 |
| 12 | STR-J | 2 / 0x02 | 50 |
| 13 | Elem-Def-J | 12 / 0x0C | 100 |
| 14 | Elem-Defx2 | 14 / 0x0E | 130 |
| 15 | Elem-Atk-J | 10 / 0x0A | 160 |
| 16 | Initiative | 63 / 0x3F | 160 |
| 17 | Absorb | 32 / 0x20 | 80 |
| 18 | Item | 23 / 0x17 | 1 |
| 19 | Magic | 20 / 0x14 | 1 |
| 20 | GF | 21 / 0x15 | 1 |
| 21 | Draw | 22 / 0x16 | 1 |

### 9 Cerberus

| Slot | Ability | Unified ID (dec / hex) | AP to learn |
|---|---|---|---|
| 0 | GFHP+10% | 87 / 0x57 | 40 |
| 1 | GFHP+20% | 88 / 0x58 | 70 |
| 2 | GFHP+30% | 89 / 0x59 | 140 |
| 3 | SPD-J | 6 / 0x06 | 120 |
| 4 | Spd+20% | 54 / 0x36 | 150 |
| 5 | Spd+40% | 55 / 0x37 | 200 |
| 6 | Auto-Haste | 73 / 0x49 | 250 |
| 7 | SPR-J | 5 / 0x05 | 50 |
| 8 | ST-Def-J | 13 / 0x0D | 100 |
| 9 | ST-Def-Jx2 | 16 / 0x10 | 130 |
| 10 | ST-Def-Jx4 | 17 / 0x11 | 180 |
| 11 | ST-Atk-J | 11 / 0x0B | 160 |
| 12 | Abilityx3 | 18 / 0x12 | 150 |
| 13 | STR-J | 2 / 0x02 | 50 |
| 14 | Alert | 78 / 0x4E | 200 |
| 15 | MAG-J | 4 / 0x04 | 50 |
| 16 | Expendx2-1 | 75 / 0x4B | 250 |
| 17 | HIT-J | 8 / 0x08 | 120 |
| 18 | Item | 23 / 0x17 | 1 |
| 19 | Magic | 20 / 0x14 | 1 |
| 20 | GF | 21 / 0x15 | 1 |
| 21 | Draw | 22 / 0x16 | 1 |

### 10 Alexander

| Slot | Ability | Unified ID (dec / hex) | AP to learn |
|---|---|---|---|
| 0 | GFHP+10% | 87 / 0x57 | 40 |
| 1 | GFHP+20% | 88 / 0x58 | 70 |
| 2 | GFHP+30% | 89 / 0x59 | 140 |
| 3 | SumMag+10% | 83 / 0x53 | 40 |
| 4 | SumMag+20% | 84 / 0x54 | 70 |
| 5 | SumMag+30% | 85 / 0x55 | 140 |
| 6 | Boost | 91 / 0x5B | 10 |
| 7 | MedData | 59 / 0x3B | 200 |
| 8 | MedLVUp | 114 / 0x72 | 120 |
| 9 | Spr+20% | 51 / 0x33 | 60 |
| 10 | Spr+40% | 52 / 0x34 | 120 |
| 11 | HighMag-RF | 113 / 0x71 | 60 |
| 12 | Abilityx3 | 18 / 0x12 | 150 |
| 13 | Elem-Atk-J | 10 / 0x0A | 160 |
| 14 | Elem-Defx2 | 14 / 0x0E | 130 |
| 15 | Elem-Defx4 | 15 / 0x0F | 180 |
| 16 | SPR-J | 5 / 0x05 | 50 |
| 17 | Revive | 33 / 0x21 | 200 |
| 18 | Item | 23 / 0x17 | 1 |
| 19 | Magic | 20 / 0x14 | 1 |
| 20 | GF | 21 / 0x15 | 1 |
| 21 | Draw | 22 / 0x16 | 1 |

### 11 Doomtrain

| Slot | Ability | Unified ID (dec / hex) | AP to learn |
|---|---|---|---|
| 0 | SumMag+10% | 83 / 0x53 | 40 |
| 1 | SumMag+20% | 84 / 0x54 | 70 |
| 2 | SumMag+30% | 85 / 0x55 | 140 |
| 3 | SumMag+40% | 86 / 0x56 | 200 |
| 4 | GFHP+10% | 87 / 0x57 | 40 |
| 5 | GFHP+20% | 88 / 0x58 | 70 |
| 6 | GFHP+30% | 89 / 0x59 | 140 |
| 7 | GFHP+40% | 90 / 0x5A | 200 |
| 8 | Boost | 91 / 0x5B | 10 |
| 9 | Auto-Shell | 71 / 0x47 | 250 |
| 10 | Absorb | 32 / 0x20 | 80 |
| 11 | Darkside | 30 / 0x1E | 100 |
| 12 | JunkShop | 96 / 0x60 | 150 |
| 13 | ForbidMed-RF | 109 / 0x6D | 200 |
| 14 | Elem-Defx4 | 15 / 0x0F | 180 |
| 15 | ST-Def-Jx4 | 17 / 0x11 | 180 |
| 16 | Item | 23 / 0x17 | 1 |
| 17 | Elem-Atk-J | 10 / 0x0A | 160 |
| 18 | ST-Atk-J | 11 / 0x0B | 160 |
| 19 | Magic | 20 / 0x14 | 1 |
| 20 | GF | 21 / 0x15 | 1 |
| 21 | Draw | 22 / 0x16 | 1 |

### 12 Bahamut

| Slot | Ability | Unified ID (dec / hex) | AP to learn |
|---|---|---|---|
| 0 | SumMag+10% | 83 / 0x53 | 40 |
| 1 | SumMag+20% | 84 / 0x54 | 70 |
| 2 | SumMag+30% | 85 / 0x55 | 140 |
| 3 | SumMag+40% | 86 / 0x56 | 200 |
| 4 | GFHP+10% | 87 / 0x57 | 40 |
| 5 | GFHP+20% | 88 / 0x58 | 70 |
| 6 | GFHP+30% | 89 / 0x59 | 140 |
| 7 | GFHP+40% | 90 / 0x5A | 200 |
| 8 | Boost | 91 / 0x5B | 10 |
| 9 | Mug | 58 / 0x3A | 200 |
| 10 | Expendx2-1 | 75 / 0x4B | 250 |
| 11 | Auto-Protect | 70 / 0x46 | 250 |
| 12 | Item | 23 / 0x17 | 1 |
| 13 | RareItem | 82 / 0x52 | 250 |
| 14 | Move-HPUp | 64 / 0x40 | 200 |
| 15 | Str+60% | 44 / 0x2C | 240 |
| 16 | Mag+60% | 50 / 0x32 | 240 |
| 17 | ForbidMag-RF | 104 / 0x68 | 200 |
| 18 | Abilityx4 | 19 / 0x13 | 200 |
| 19 | Magic | 20 / 0x14 | 1 |
| 20 | GF | 21 / 0x15 | 1 |
| 21 | Draw | 22 / 0x16 | 1 |

### 13 Cactuar

| Slot | Ability | Unified ID (dec / hex) | AP to learn |
|---|---|---|---|
| 0 | GFHP+10% | 87 / 0x57 | 40 |
| 1 | GFHP+20% | 88 / 0x58 | 70 |
| 2 | GFHP+30% | 89 / 0x59 | 140 |
| 3 | Item | 23 / 0x17 | 1 |
| 4 | EVA-J | 7 / 0x07 | 200 |
| 5 | Eva+30% | 56 / 0x38 | 150 |
| 6 | Expendx2-1 | 75 / 0x4B | 250 |
| 7 | LUCK-J | 9 / 0x09 | 200 |
| 8 | Luck+50% | 57 / 0x39 | 200 |
| 9 | Defend | 29 / 0x1D | 100 |
| 10 | Auto Potion | 74 / 0x4A | 150 |
| 11 | Initiative | 63 / 0x3F | 160 |
| 12 | HPBonus | 65 / 0x41 | 100 |
| 13 | StrBonus | 66 / 0x42 | 100 |
| 14 | VitBonus | 67 / 0x43 | 100 |
| 15 | MagBonus | 68 / 0x44 | 100 |
| 16 | SprBonus | 69 / 0x45 | 100 |
| 17 | Move-HPUp | 64 / 0x40 | 200 |
| 18 | Kamikaze | 36 / 0x24 | 100 |
| 19 | Magic | 20 / 0x14 | 1 |
| 20 | GF | 21 / 0x15 | 1 |
| 21 | Draw | 22 / 0x16 | 1 |

### 14 Tonberry

| Slot | Ability | Unified ID (dec / hex) | AP to learn |
|---|---|---|---|
| 0 | SumMag+10% | 83 / 0x53 | 40 |
| 1 | SumMag+20% | 84 / 0x54 | 70 |
| 2 | SumMag+30% | 85 / 0x55 | 140 |
| 3 | GFHP+10% | 87 / 0x57 | 40 |
| 4 | GFHP+20% | 88 / 0x58 | 70 |
| 5 | GFHP+30% | 89 / 0x59 | 140 |
| 6 | Boost | 91 / 0x5B | 10 |
| 7 | Auto Potion | 74 / 0x4A | 150 |
| 8 | Move-HPUp | 64 / 0x40 | 200 |
| 9 | Initiative | 63 / 0x3F | 160 |
| 10 | Luck+50% | 57 / 0x39 | 200 |
| 11 | Item | 23 / 0x17 | 1 |
| 12 | Eva+30% | 56 / 0x38 | 150 |
| 13 | Haggle | 92 / 0x5C | 150 |
| 14 | Sell-High | 93 / 0x5D | 150 |
| 15 | Familiar | 94 / 0x5E | 150 |
| 16 | CallShop | 95 / 0x5F | 200 |
| 17 | LVDown | 34 / 0x22 | 100 |
| 18 | LVUp | 35 / 0x23 | 100 |
| 19 | Magic | 20 / 0x14 | 1 |
| 20 | GF | 21 / 0x15 | 1 |
| 21 | Draw | 22 / 0x16 | 1 |

### 15 Eden

| Slot | Ability | Unified ID (dec / hex) | AP to learn |
|---|---|---|---|
| 0 | SumMag+10% | 83 / 0x53 | 40 |
| 1 | SumMag+20% | 84 / 0x54 | 70 |
| 2 | SumMag+30% | 85 / 0x55 | 140 |
| 3 | SumMag+40% | 86 / 0x56 | 200 |
| 4 | GFHP+10% | 87 / 0x57 | 40 |
| 5 | GFHP+20% | 88 / 0x58 | 70 |
| 6 | GFHP+30% | 89 / 0x59 | 140 |
| 7 | GFHP+40% | 90 / 0x5A | 200 |
| 8 | Boost | 91 / 0x5B | 10 |
| 9 | GFAblMed-RF | 111 / 0x6F | 30 |
| 10 | Darkside | 30 / 0x1E | 100 |
| 11 | MadRush | 27 / 0x1B | 60 |
| 12 | HIT-J | 8 / 0x08 | 120 |
| 13 | SPD-J | 6 / 0x06 | 120 |
| 14 | EVA-J | 7 / 0x07 | 200 |
| 15 | Luck+50% | 57 / 0x39 | 200 |
| 16 | Expendx3-1 | 76 / 0x4C | 250 |
| 17 | Devour | 37 / 0x25 | 100 |
| 18 | Item | 23 / 0x17 | 1 |
| 19 | Magic | 20 / 0x14 | 1 |
| 20 | GF | 21 / 0x15 | 1 |
| 21 | Draw | 22 / 0x16 | 1 |

## ability_ap_cost[116] (unified ability ID -> AP)

| ID (dec/hex) | Ability | AP |
|---|---|---|
| 0 / 0x00 | None | 0 |
| 1 / 0x01 | HP-J | 50 |
| 2 / 0x02 | STR-J | 50 |
| 3 / 0x03 | VIT-J | 50 |
| 4 / 0x04 | MAG-J | 50 |
| 5 / 0x05 | SPR-J | 50 |
| 6 / 0x06 | SPD-J | 120 |
| 7 / 0x07 | EVA-J | 200 |
| 8 / 0x08 | HIT-J | 120 |
| 9 / 0x09 | LUCK-J | 200 |
| 10 / 0x0A | Elem-Atk-J | 160 |
| 11 / 0x0B | ST-Atk-J | 160 |
| 12 / 0x0C | Elem-Def-J | 100 |
| 13 / 0x0D | ST-Def-J | 100 |
| 14 / 0x0E | Elem-Defx2 | 130 |
| 15 / 0x0F | Elem-Defx4 | 180 |
| 16 / 0x10 | ST-Def-Jx2 | 130 |
| 17 / 0x11 | ST-Def-Jx4 | 180 |
| 18 / 0x12 | Abilityx3 | 150 |
| 19 / 0x13 | Abilityx4 | 200 |
| 20 / 0x14 | Magic | 1 |
| 21 / 0x15 | GF | 1 |
| 22 / 0x16 | Draw | 1 |
| 23 / 0x17 | Item | 1 |
| 24 / 0x18 | Empty* | 0 |
| 25 / 0x19 | Card | 40 |
| 26 / 0x1A | Doom | 60 |
| 27 / 0x1B | MadRush | 60 |
| 28 / 0x1C | Treatment | 100 |
| 29 / 0x1D | Defend | 100 |
| 30 / 0x1E | Darkside | 100 |
| 31 / 0x1F | Recover | 200 |
| 32 / 0x20 | Absorb | 80 |
| 33 / 0x21 | Revive | 200 |
| 34 / 0x22 | LVDown | 100 |
| 35 / 0x23 | LVUp | 100 |
| 36 / 0x24 | Kamikaze | 100 |
| 37 / 0x25 | Devour | 100 |
| 38 / 0x26 | MiniMog | 0 |
| 39 / 0x27 | HP+20% | 60 |
| 40 / 0x28 | HP+40% | 120 |
| 41 / 0x29 | HP+80% | 240 |
| 42 / 0x2A | Str+20% | 60 |
| 43 / 0x2B | Str+40% | 120 |
| 44 / 0x2C | Str+60% | 240 |
| 45 / 0x2D | Vit+20% | 60 |
| 46 / 0x2E | Vit+40% | 120 |
| 47 / 0x2F | Vit+60% | 240 |
| 48 / 0x30 | Mag+20% | 60 |
| 49 / 0x31 | Mag+40% | 120 |
| 50 / 0x32 | Mag+60% | 240 |
| 51 / 0x33 | Spr+20% | 60 |
| 52 / 0x34 | Spr+40% | 120 |
| 53 / 0x35 | Spr+60% | 240 |
| 54 / 0x36 | Spd+20% | 150 |
| 55 / 0x37 | Spd+40% | 200 |
| 56 / 0x38 | Eva+30% | 150 |
| 57 / 0x39 | Luck+50% | 200 |
| 58 / 0x3A | Mug | 200 |
| 59 / 0x3B | MedData | 200 |
| 60 / 0x3C | Counter | 200 |
| 61 / 0x3D | Return Damage | 0 |
| 62 / 0x3E | Cover | 100 |
| 63 / 0x3F | Initiative | 160 |
| 64 / 0x40 | Move-HPUp | 200 |
| 65 / 0x41 | HPBonus | 100 |
| 66 / 0x42 | StrBonus | 100 |
| 67 / 0x43 | VitBonus | 100 |
| 68 / 0x44 | MagBonus | 100 |
| 69 / 0x45 | SprBonus | 100 |
| 70 / 0x46 | Auto-Protect | 250 |
| 71 / 0x47 | Auto-Shell | 250 |
| 72 / 0x48 | Auto-Reflect | 250 |
| 73 / 0x49 | Auto-Haste *(see note)* | 250 |
| 74 / 0x4A | Auto Potion | 150 |
| 75 / 0x4B | Expendx2-1 | 250 |
| 76 / 0x4C | Expendx3-1 | 250 |
| 77 / 0x4D | Ribbon | 0 |
| 78 / 0x4E | Alert | 200 |
| 79 / 0x4F | Move-Find | 40 |
| 80 / 0x50 | Enc-Half | 30 |
| 81 / 0x51 | Enc-None | 100 |
| 82 / 0x52 | RareItem | 250 |
| 83 / 0x53 | SumMag+10% | 40 |
| 84 / 0x54 | SumMag+20% | 70 |
| 85 / 0x55 | SumMag+30% | 140 |
| 86 / 0x56 | SumMag+40% | 200 |
| 87 / 0x57 | GFHP+10% | 40 |
| 88 / 0x58 | GFHP+20% | 70 |
| 89 / 0x59 | GFHP+30% | 140 |
| 90 / 0x5A | GFHP+40% | 200 |
| 91 / 0x5B | Boost | 10 |
| 92 / 0x5C | Haggle | 150 |
| 93 / 0x5D | Sell-High | 150 |
| 94 / 0x5E | Familiar | 150 |
| 95 / 0x5F | CallShop | 200 |
| 96 / 0x60 | JunkShop | 150 |
| 97 / 0x61 | T Mag-RF | 30 |
| 98 / 0x62 | IMag-RF | 30 |
| 99 / 0x63 | FMag-RF | 30 |
| 100 / 0x64 | LMag-RF | 30 |
| 101 / 0x65 | TimeMag-RF | 30 |
| 102 / 0x66 | STMag-RF | 60 |
| 103 / 0x67 | SuptMag-RF | 30 |
| 104 / 0x68 | ForbidMag-RF | 200 |
| 105 / 0x69 | RecovMed-RF | 30 |
| 106 / 0x6A | STMed-RF | 30 |
| 107 / 0x6B | Ammo-RF | 30 |
| 108 / 0x6C | Tool-RF | 30 |
| 109 / 0x6D | ForbidMed-RF | 200 |
| 110 / 0x6E | GFRecovMed-RF | 30 |
| 111 / 0x6F | GFAblMed-RF | 30 |
| 112 / 0x70 | Mid Mag-RF | 60 |
| 113 / 0x71 | HighMag-RF | 60 |
| 114 / 0x72 | MedLVUp | 120 |
| 115 / 0x73 | Card Mod | 80 |

## Paste-ready C

```c
/* Unified ability AP-to-learn cost, indexed by ability ID (0-115).
   Source: Hyne apsTab[116]; validated against in-game anchors.
   NOTE: id 73 (Auto-Haste): Hyne=250, Final Fantasy Wiki=150 (multiple pages).
         Only appears in Cerberus slot 6. Verify against a save if encountered. */
static const unsigned char ability_ap_cost[116] = {
      0,  50,  50,  50,  50,  50, 120, 200, 120, 200, 160, 160,
    100, 100, 130, 180, 130, 180, 150, 200,   1,   1,   1,   1,
      0,  40,  60,  60, 100, 100, 100, 200,  80, 200, 100, 100,
    100, 100,   0,  60, 120, 240,  60, 120, 240,  60, 120, 240,
     60, 120, 240,  60, 120, 240, 150, 200, 150, 200, 200, 200,
    200,   0, 100, 160, 200, 100, 100, 100, 100, 100, 250, 250,
    250, 250, 150, 250, 250,   0, 200,  40,  30, 100, 250,  40,
     70, 140, 200,  40,  70, 140, 200,  10, 150, 150, 150, 200,
    150,  30,  30,  30,  30,  30,  60,  30, 200,  30,  30,  30,
     30, 200,  30,  30,  60,  60, 120,  80
};

/* Per-GF learnable abilities in SLOT ORDER (matches save APs[] index).
   16 GFs x 22 slots; value = unified ability ID. Source: Hyne innateAbilities. */
static const unsigned char gf_ability_slots[16][22] = {
    {  83,  84,  85,  87,  88,  91,  97, 112,   1,  48,  49,  10,   3,  12,  14,  25, 115,   4,  23,  20,  21,  22 },  /* 0 Quezacotl */
    {  83,  84,  85,  87,  88,  23,  91,  98,   3,  45,  46,  51,  52,  12,  14,   2,  10,  26,   5,  20,  21,  22 },  /* 1 Shiva */
    {  83,  84,  85,  87,  88,  89,  91,  99, 107,  42,  43,  10,  66,   1,  23,  12,  14,  27,   2,  20,  21,  22 },  /* 2 Ifrit */
    {  87,  88, 108,  83,  84,  85,  91, 100, 106,  48,  49,  11,  68,  23,  13,  16,  28,  79,   4,  20,  21,  22 },  /* 3 Siren */
    {  83,  84,  85,  87,  88,  89,  91,  39,  40,  41,  65,   2,  10,   5,  12,  23,  62,  29,   1,  20,  21,  22 },  /* 4 Brothers */
    {  87,  88,  89, 101, 102,   1,  39,  40,  41,   4,  48,  49,  23,   8,  80,  81,  30,  58,  18,  20,  21,  22 },  /* 5 Diablos */
    {  87,  88,  89, 105,  45,  46,  67,   1,  39,  40,   4,  11,  13,  16,  60,  72,  23,  18,   3,  20,  21,  22 },  /* 6 Carbuncle */
    {  87,  88,  89,  83,  84,  85,  91, 103, 110,  51,  52,  69,   5,  14,   4,  10,  74,  31,  23,  20,  21,  22 },  /* 7 Leviathan */
    {  83,  84,  85,  87,  88,  89,  91,   6,  54,  55,  42,  43,   2,  12,  14,  10,  63,  32,  23,  20,  21,  22 },  /* 8 Pandemona */
    {  87,  88,  89,   6,  54,  55,  73,   5,  13,  16,  17,  11,  18,   2,  78,   4,  75,   8,  23,  20,  21,  22 },  /* 9 Cerberus */
    {  87,  88,  89,  83,  84,  85,  91,  59, 114,  51,  52, 113,  18,  10,  14,  15,   5,  33,  23,  20,  21,  22 },  /* 10 Alexander */
    {  83,  84,  85,  86,  87,  88,  89,  90,  91,  71,  32,  30,  96, 109,  15,  17,  23,  10,  11,  20,  21,  22 },  /* 11 Doomtrain */
    {  83,  84,  85,  86,  87,  88,  89,  90,  91,  58,  75,  70,  23,  82,  64,  44,  50, 104,  19,  20,  21,  22 },  /* 12 Bahamut */
    {  87,  88,  89,  23,   7,  56,  75,   9,  57,  29,  74,  63,  65,  66,  67,  68,  69,  64,  36,  20,  21,  22 },  /* 13 Cactuar */
    {  83,  84,  85,  87,  88,  89,  91,  74,  64,  63,  57,  23,  56,  92,  93,  94,  95,  34,  35,  20,  21,  22 },  /* 14 Tonberry */
    {  83,  84,  85,  86,  87,  88,  89,  90,  91, 111,  30,  27,   8,   6,   7,  57,  76,  37,  23,  20,  21,  22 },  /* 15 Eden */
};

/* Resolve current/total AP for a GF's currently-learning ability.
   gf_idx 0-15, learning = ability ID from save, aps = save APs[] (>=22 bytes).
   Returns slot index (0-21) or -1 if not found; *cur/*tot get current/needed AP. */
static int ff8_gf_learning_progress(int gf_idx, unsigned char learning,
                                    const unsigned char *aps_save,
                                    int *cur, int *tot) {
    for (int s = 0; s < 22; ++s) {
        if (gf_ability_slots[gf_idx][s] == learning) {
            if (cur) *cur = aps_save[s];
            if (tot) *tot = ability_ap_cost[learning];
            return s;
        }
    }
    return -1;
}
```

## Confidence notes

- **High confidence** on AP costs and slot order for every ability involved in the five anchors and for all stat-junction / stat+% / SumMag / GFHP / RF / Boost abilities (internally consistent, anchor-validated, and cross-checked).

- **Auto-Haste (ID 73):** sole source conflict. Hyne = 250 AP; Final Fantasy Wiki states 150 AP on multiple pages while agreeing with Hyne that Auto-Protect/Shell/Reflect = 250. This looks like a fill-down error in Hyne. Recommend treating Auto-Haste as **150** unless your own save proves otherwise. Impact is tiny: ID 73 appears only in Cerberus (GF 9), slot 6.

- **Slot count 21 vs 22:** the Doomtrain kernel.bin GF-section doc lists 21 ability entries; Hyne, the save's `APs[24]`, and Gamer Guides all say 22. The 22 figure governs the `APs[]` index mapping and is used here. The discrepancy only concerns the trailing command slots and does not affect any learnable-ability placement.

- GF identities: Hyne uses internal/working names in its source comments (Golgotha=Quezacotl, Ondine=Siren, Taurus=Brothers, Nosferatu=Diablos, Ahuri=Carbuncle, Zephyr=Pandemona, Helltrain=Doomtrain, Pampa=Cactuar, Tomberry=Tonberry, orbital=Eden). Order matches the canonical 0-15 indexing exactly.
