# Deep Research Prompt: FF8 Guardian Force EXP-per-level table

> Paste everything below the line into a fresh Claude session that has web
> access. It is self-contained. The goal is one verified 16-entry table.

---

I need the **per-Guardian-Force "EXP required per level" constant** for all 16
GFs in **Final Fantasy VIII** (this is for the original/Steam 2013 PC release,
but the value is the same across releases). Please research authoritative
sources and return a single validated table.

## Background / what I already know (do not re-derive — use to VERIFY your answer)

FF8 levels each GF using a **flat, per-GF EXP cost**. For a GF with stored
total EXP `exp` and per-level cost `cost`:

```
level             = exp / cost + 1      (integer division)
EXP_to_next_level = cost - (exp % cost)
```

I have **empirically confirmed** five of the costs by reading a GF's stored EXP
out of the save and comparing to the level/next-EXP the game displays on the GF
detail screen. **Any correct source MUST reproduce all five of these exactly** —
treat them as ground-truth test cases and reject any source that disagrees:

| GF (canonical index) | Confirmed cost | Evidence (stored EXP -> displayed level / next) |
|---|---|---|
| 0  Quezacotl | **500** | EXP 11185 -> Lv 23, next 315 |
| 1  Shiva     | **500** | EXP 19958 -> Lv 40, next 42  |
| 2  Ifrit     | **500** | EXP 11930 -> Lv 24, next 70  |
| 3  Siren     | **400** | EXP 10192 -> Lv 26, next 208 |
| 5  Diablos   | **500** | EXP 4000  -> Lv 9,  next 500 |

Two more facts from secondary sources, to be confirmed by your research:
- **Eden** (index 15) is documented as needing **1000** EXP per level.
- The FF8 wiki states most GFs need 500 but **"some take only 400"** — the word
  *some* is plural, so there is almost certainly **at least one more 400-cost GF
  besides Siren**. Identifying every 400-cost GF is the most important part of
  this task; do not assume "all unlisted GFs are 500".

## Canonical GF order (use these exact indices in your answer)

0 Quezacotl, 1 Shiva, 2 Ifrit, 3 Siren, 4 Brothers, 5 Diablos, 6 Carbuncle,
7 Leviathan, 8 Pandemona, 9 Cerberus, 10 Alexander, 11 Doomtrain, 12 Bahamut,
13 Cactuar, 14 Tonberry, 15 Eden.

## Best sources to check (in priority order)

1. **Hyne** — myst6re's open-source FF8 save editor: `github.com/myst6re/hyne`.
   It converts a GF's stored EXP into a displayed level, so its source code must
   contain the per-GF EXP-per-level constants (look for a GF data table / an
   array of per-GF values, or the level-from-EXP routine). This is the most
   authoritative source because it mirrors the game's own arithmetic.
2. The **Final Fantasy Wiki (Fandom)** individual GF pages — each GF's infobox
   has historically listed an "EXP to level up" / "EXP" field. Collect the value
   from all 16 pages.
3. Any FF8 GF-data dump on the qhimm forums / GitHub (kernel/exe GF data tables).

Cross-check at least two independent sources where possible. If sources
disagree, prefer the one consistent with my five confirmed anchors.

## What to return

1. A **16-row table**: canonical index, GF name, EXP-per-level cost, and the
   source(s) you got it from.
2. An explicit note confirming the table reproduces all five anchors above
   (and Siren = 400, Diablos = 500 specifically).
3. A list of **which GFs are NOT 500** (the 400s and the 1000), since those are
   the ones that matter.
4. A ready-to-paste **C array literal** in canonical order, e.g.:
   ```c
   static const int GF_EXP_PER_LEVEL[16] = {
       /* 0 Quezacotl */ 500, /* 1 Shiva */ 500, /* 2 Ifrit */ 500,
       /* 3 Siren */ 400, /* 4 Brothers */ ???, /* 5 Diablos */ 500,
       /* ... */
   };
   ```
5. A confidence note per value where a source was weak or only one source was
   found, so I know which to spot-check in-game.
