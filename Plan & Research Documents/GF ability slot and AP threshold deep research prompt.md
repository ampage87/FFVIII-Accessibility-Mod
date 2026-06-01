# Deep Research Prompt: FF8 per-GF ability table (slot → ability ID → AP cost)

> Paste everything below the line into a fresh Claude session with web access.
> It is self-contained. Goal: one validated per-GF ability table that lets a mod
> map a GF's "currently learning" ability ID to (a) its slot index and (b) the
> AP required to learn it.

---

I'm building an accessibility mod for **Final Fantasy VIII** (Steam 2013 PC,
FF8_EN.exe). On the GF detail screen the game shows the ability a GF is currently
learning plus its AP progress, e.g. "SumMag+20% 36/70". I can already read the
GF's currently-learning **ability ID** and its accumulated-AP array from the save,
but I need static game data to interpret them.

## The data I need

For **each of the 16 Guardian Forces**, the ordered list of learnable ability
**slots**. For every slot I need:
- the **slot index** (0-based, matching the GF's `APs[]` array order in the save),
- the **unified ability ID** (FF8's single 0–115 ability namespace),
- the **AP required** to learn that ability ("AP to learn" / the AP threshold).

This is the per-GF GF ability data from `kernel.bin` (section 2, "Junctionable
GFs"; each GF entry is ~132 bytes with up to 21 four-byte ability slots, each slot
referencing an ability ID and an AP/unlocker threshold).

## Why I need it (mechanic)

In the save, each GF stores `APs[24]` = accumulated AP **per slot** (slot 0..21),
and a `learning` byte = the **ability ID** currently being learned. To display
"current / total AP" I must:
1. find the **slot** whose ability ID equals `learning` (needs the per-GF list),
2. read `APs[slot]` = current AP,
3. read that slot's **AP threshold** = total needed.

There is no shortcut: the learning ability is NOT always the last slot, and
earlier slots can hold AP while the learning slot is 0 (see Ifrit anchor below).

## Ground-truth anchors (any correct answer MUST reproduce these)

From my own save + on-screen readings:

| GF | Learning ability (unified ID) | Slot | Current AP (=APs[slot]) | Total AP (threshold) |
|----|-------------------------------|------|--------------------------|----------------------|
| Quezacotl | SumMag+30% (85) | 2 | 117 | **140** |
| Shiva | Vit-J (3) | 8 | 46 | **50** |
| Ifrit | GFHP+10% (87) | (a slot ≥3, AP=0) | 0 | **40** |
| Siren | SumMag+20% (84) | 4 | 36 | **70** |
| Diablos | GFHP+10% (87) | — | 0 | **40** |

So confirmed AP thresholds so far: **GFHP+10% = 40, Vit-J = 50, SumMag+20% = 70,
SumMag+30% = 140.** Also: Quezacotl's slots 0/1 hold AP 40/72 (earlier-learned
abilities) and slot 2 (117) is the one being learned; Ifrit's slots 0/1/2 hold
40/73/151 yet its learning ability (GFHP+10%) reads 0 — confirming the slot is
NOT derivable from the AP values.

## Also clarify

- Is the "AP to learn" a property of the **ability** (same cost wherever it
  appears) or of the **(GF, slot)** pair? If per-ability, also give a single
  ability-ID → AP-cost table.
- The unified ability ID list (0–115) for naming, if your source has it.

## Canonical GF order (use these indices)

0 Quezacotl, 1 Shiva, 2 Ifrit, 3 Siren, 4 Brothers, 5 Diablos, 6 Carbuncle,
7 Leviathan, 8 Pandemona, 9 Cerberus, 10 Alexander, 11 Doomtrain, 12 Bahamut,
13 Cactuar, 14 Tonberry, 15 Eden.

## Best sources (priority order)

1. **Doomtrain wiki** — `github.com/DarkShinryu/doomtrain.wiki` (clone it):
   `Junctionable-GFs.md` / per-GF pages list each GF's abilities, slot order, and
   AP. `Junctionable-Abilities.md` is the master ability-ID table.
2. **HobbitDur FF8ModdingWiki** — kernel.bin / Junctionable-GFs documentation
   (the per-GF ability lists with AP).
3. **Hyne** save editor source — `github.com/myst6re/hyne` (struct + any data).
4. Any kernel.bin GF-data dump on the qhimm forums.

Cross-check at least two sources. Prefer whatever reproduces my anchors above.

## What to return

1. For each of the 16 GFs: a slot-ordered table (slot index, ability name,
   unified ability ID, AP to learn).
2. Confirmation it reproduces all anchors (especially the four thresholds and the
   Quezacotl slot-2 / Shiva slot-8 / Siren slot-4 placements).
3. A compact, paste-ready C structure, e.g. per-GF arrays of `{ability_id, ap}`
   in slot order, plus (if AP is per-ability) a single `ability_ap_cost[116]`.
4. Confidence notes wherever a source was thin or sources disagreed.
