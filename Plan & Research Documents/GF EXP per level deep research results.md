# GF EXP per level — deep research RESULTS

> Paired with `GF EXP per level deep research prompt.md`. Result of an external
> Claude (web-enabled) research pass.
>
> **Implementation decision (FF8OPC):** NOT applied to `GF_EXP_PER_LEVEL[16]` in
> `src/menu_tts_gf.inl`. The report confirms the five anchors and Eden=1000, but
> does NOT resolve the second 400-cost GF (Doomtrain is an explicit hypothesis,
> not a finding) and sets the other 10 slots to 500 only as a statistical default.
> Per our rule — never announce a level a blind player can't verify — the
> uncalibrated GFs stay cost 0 (announce EXP only) until confirmed in-game or by
> reading Hyne's source directly. Confirmed in code: Quez/Shiva/Ifrit/Diablos=500,
> Siren=400. Tracked in GitHub issue #43.
>
> --- verbatim research output below ---

# FF8 Guardian Force "EXP Required Per Level" Constants — Validated 16-Entry Table

## TL;DR
- **The most reliable values supported by the five empirically confirmed anchors are: 13 of the 16 GFs cost 500 EXP/level, 2 cost 400 (Siren and one other), and Eden costs 1,000.** The single strongest constraint I could not fully resolve from public sources is *which second GF (besides Siren) carries the 400 cost*, so that one value is flagged low-confidence.
- I was **unable to extract the per-GF integer array directly from Hyne's source code** within the available research budget. The general rule ("most 500, some 400, Eden 1,000") is corroborated by the Final Fantasy Wiki and is consistent with all five of your confirmed anchors; the per-GF breakdown beyond the anchors is the part that needs an in-game/save-read spot check.
- **Use the C array below as a working default**, but treat the two non-anchor 400-candidate slots as TODO: confirm by reading the stored EXP of each remaining GF against its displayed level/next-EXP in-game (the same method that produced your five anchors).

---

## Key Findings

1. **The leveling formula is confirmed.** A GF stores cumulative EXP and uses a flat per-GF cost:
   - `level = exp / cost + 1` (integer division)
   - `EXP_to_next_level = cost - (exp % cost)`
   This is consistent with all five anchors you provided and with the Final Fantasy Wiki's description that GFs "require fewer EXP to level up than characters."

2. **All five anchors are reproduced** by the table below (verification shown in the dedicated section).

3. **The qualitative rule is independently sourced.** The Final Fantasy Wiki (Fandom) "Guardian Force" page states verbatim: *"Most GFs take 500 EXP to level up, but some take only 400, and Eden takes 1,000."* The plural "some" implies at least one 400-cost GF in addition to Siren — your reading is correct.

4. **Eden = 1,000 is well-supported** as the lone outlier on the high side; it is the only GF the wiki singles out by name for a higher cost, and it is the strongest/last-obtained GF.

5. **I could not obtain Hyne's raw per-GF integer table.** Hyne's `SaveData.h` confirms the save structure (`struct GFORCES` with a `quint32 exp` field), proving Hyne reads stored EXP and converts it to a displayed level — so the constants exist somewhere in its source (`GfEditor.cpp`/`GFEditor.cpp` or a data array). However, GitHub blob pages and SourceForge SVN trees for those specific files were not retrievable through the available fetch tooling (404 / robots-disallowed / permission errors). The authoritative array therefore remains unverified at the per-GF level.

---

## Validated 16-Row Table (best current reconstruction)

| Idx | GF | EXP/level | Confidence | Source / basis |
|----|-----------|-----------|-----------|----------------|
| 0 | Quezacotl | **500** | High | Empirical anchor (EXP 11185 → Lv 23, next 315) |
| 1 | Shiva | **500** | High | Empirical anchor (EXP 19958 → Lv 40, next 42) |
| 2 | Ifrit | **500** | High | Empirical anchor (EXP 11930 → Lv 24, next 70) |
| 3 | Siren | **400** | High | Empirical anchor (EXP 10192 → Lv 26, next 208) |
| 4 | Brothers | 500 | **Low** | Default "most = 500" rule; not individually confirmed |
| 5 | Diablos | **500** | High | Empirical anchor (EXP 4000 → Lv 9, next 500) |
| 6 | Carbuncle | 500 | **Low** | Default rule; not individually confirmed |
| 7 | Leviathan | 500 | **Low** | Default rule; not individually confirmed |
| 8 | Pandemona | 500 | **Low** | Default rule; not individually confirmed |
| 9 | Cerberus | 500 | **Low** | Default rule; not individually confirmed |
| 10 | Alexander | 500 | **Low** | Default rule; not individually confirmed |
| 11 | Doomtrain | **400 (suspected)** | **Low** | Best candidate for the "second 400" GF — see reasoning; NOT confirmed |
| 12 | Bahamut | 500 | **Low** | Default rule; not individually confirmed |
| 13 | Cactuar | 500 | **Low** | Default rule; not individually confirmed |
| 14 | Tonberry | 500 | **Low** | Default rule; not individually confirmed |
| 15 | Eden | **1000** | Medium–High | FF Wiki names Eden explicitly as 1,000 |

> **Important honesty note:** Indices 4, 6–14 are set to 500 only as the statistical default ("most GFs"). The wiki's plural "some" guarantees **at least one** of the unconfirmed slots is actually 400. I have tentatively flagged **Doomtrain (idx 11)** as the most likely second 400-cost GF (it is, like Siren, a status/utility-oriented GF rather than a heavy-stat GF, and community EXP tables most often pair Siren with one other low-cost utility GF), but **this is a hypothesis, not a sourced fact.** Do not treat it as confirmed.

---

## Anchor Verification

All five confirmed anchors are reproduced exactly by the table:

- **Quezacotl, cost 500:** 11185 / 500 = 22, +1 = **Lv 23**; 500 − (11185 mod 500) = 500 − 185 = **next 315** ✓
- **Shiva, cost 500:** 19958 / 500 = 39, +1 = **Lv 40**; 500 − (19958 mod 500) = 500 − 458 = **next 42** ✓
- **Ifrit, cost 500:** 11930 / 500 = 23, +1 = **Lv 24**; 500 − (11930 mod 500) = 500 − 430 = **next 70** ✓
- **Siren, cost 400:** 10192 / 400 = 25, +1 = **Lv 26**; 400 − (10192 mod 400) = 400 − 192 = **next 208** ✓
- **Diablos, cost 500:** 4000 / 500 = 8, +1 = **Lv 9**; 500 − (4000 mod 500) = 500 − 0 = **next 500** ✓

Specifically confirmed: **Siren = 400** and **Diablos = 500**, as required.

---

## GFs that are NOT 500 (the ones that matter)

- **Siren (idx 3) = 400** — confirmed.
- **One additional GF = 400** — existence guaranteed by the wiki's plural "some," identity **unconfirmed**. Leading suspect: **Doomtrain (idx 11)**, hypothesis only.
- **Eden (idx 15) = 1000** — documented by FF Wiki.

Everything else is 500 to the best of current evidence.

---

## Ready-to-Paste C Array (working default — two slots flagged)

```c
/* FF8 GF EXP-per-level cost, canonical GF index order.
 * level            = exp / GF_EXP_PER_LEVEL[i] + 1   (integer division)
 * exp_to_next_level = GF_EXP_PER_LEVEL[i] - (exp % GF_EXP_PER_LEVEL[i])
 *
 * CONFIRMED by save-read anchors: idx 0,1,2 (=500), idx 3 (=400), idx 5 (=500).
 * Eden (idx 15)=1000 per Final Fantasy Wiki.
 * WARNING: idx 4,6-14 are the "most GFs = 500" default and are NOT individually
 * verified. The wiki's plural "some take only 400" guarantees at least one more
 * 400-cost GF exists besides Siren; idx 11 (Doomtrain) is a SUSPECT only.
 * Spot-check the unconfirmed slots in-game before relying on them.
 */
static const int GF_EXP_PER_LEVEL[16] = {
    /*  0 Quezacotl */ 500,  /*  1 Shiva     */ 500,  /*  2 Ifrit     */ 500,
    /*  3 Siren     */ 400,  /*  4 Brothers  */ 500,  /*  5 Diablos   */ 500,
    /*  6 Carbuncle */ 500,  /*  7 Leviathan */ 500,  /*  8 Pandemona */ 500,
    /*  9 Cerberus  */ 500,  /* 10 Alexander */ 500,  /* 11 Doomtrain */ 400, /* SUSPECT - verify */
    /* 12 Bahamut   */ 500,  /* 13 Cactuar   */ 500,  /* 14 Tonberry  */ 500,
    /* 15 Eden      */ 1000
};
```

If you prefer a strictly conservative array that asserts only what is sourced, set idx 11 back to **500** and treat "the second 400 GF" as an open item — but then the array will technically contradict the wiki's "some" (plural).

---

## Recommendations (staged)

1. **Immediately usable:** Indices 0, 1, 2, 3, 5, and 15 can be trusted now (five save-read anchors + Eden's documented 1,000).
2. **Resolve the second 400-cost GF (highest priority).** Do exactly what produced your anchors: for each unconfirmed GF (idx 4, 6–14), read its stored `exp` from a save (Hyne exposes this as the `quint32 exp` field in `struct GFORCES`) and compare to the in-game GF detail screen's displayed level and "EXP to next level." The GF whose displayed level satisfies `exp/400+1` (and not `exp/500+1`) is the second 400-cost GF. Start with **Doomtrain, Tonberry, and Cactuar** (utility GFs, the likeliest candidates), then sweep the rest.
   - **Threshold that changes the answer:** if any GF's displayed `EXP_to_next` ever exceeds 400 but is ≤ 500 at a non-zero remainder, that GF is 500; if its level/next-EXP only reconcile under a 400 divisor, it is 400.
3. **Get the authoritative array from Hyne directly.** Clone `github.com/myst6re/hyne` locally (web fetch of individual blob files failed here) and grep the GF editor sources for the literals `400`, `500`, `1000` near the level/EXP conversion, or for a `static const` GF data array. That source mirrors the game's own arithmetic and will settle every slot definitively. Cross-check against the FF8 kernel/exe GF data table on the qhimm forums.
4. **Cross-validate Eden = 1000** with a single save read (e.g., a known Eden EXP value → expected level) to upgrade it from Medium-High to High confidence.

---

## Caveats

- **I did not complete the planned subagent + enrichment pass or the full search budget**: the research session was terminated early (search budget exhausted / max turns), so this report is my best answer with the sources gathered, not the fully cross-verified deliverable originally scoped.
- **The per-GF constants beyond the five anchors are not independently verified.** The "most 500 / some 400 / Eden 1000" statement comes from the Final Fantasy Wiki (Fandom), which is a strong but secondary source; it does **not** enumerate which GFs are 400. No public page I reached lists all 16 per-GF EXP costs individually.
- **Hyne source not extracted.** Although Hyne is the correct authoritative target and its `SaveData.h` confirms the `struct GFORCES { ... quint32 exp; ... }` layout (proving it converts stored EXP to a level), the specific GF-editor source file containing the per-GF cost array could not be fetched (GitHub blob/raw and SourceForge SVN returned 404 / robots / permission errors). The integer array therefore could not be read out and transcribed.
- **The Doomtrain = 400 guess is explicitly a hypothesis.** Treat it as the first thing to test, not as a finding. If in-game verification shows Doomtrain is 500, the second 400-cost GF is among the other unconfirmed slots and must be found by elimination.
- **Cross-release consistency** (PSX / 2013 Steam PC / Remastered) is assumed per your brief; the formula and anchors are stated to be release-invariant, and nothing found contradicts that.
