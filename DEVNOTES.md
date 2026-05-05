**Purpose & context**

Aaron is the sole developer of the FF8 Accessibility Mod — a `dinput8.dll` injection for Final Fantasy VIII Steam 2013 edition (App ID 39150, FF8_EN.exe + FFNx v1.23.x) that makes the game playable for blind players via Windows SAPI text-to-speech, navigation assistance, and entity catalogs. Aaron is blind himself and is the primary tester, using NVDA as his screen reader. The mod is open-source at `github.com/ampage87/FFVIII-Accessibility-Mod`.

**Target platform:** FF8 Steam 2013 + FFNx v1.23.x (user installs separately). Mod builds as MSBuild .sln (Win32), outputs `dinput8.dll`. FFNx source at `github.com/julianxhokaxhiu/FFNx` is reference only (address offsets). Echo-S voice mod proves field dialog hooks work.

**Project root:** `C:/Users/ampag/OneDrive/Documents/FFVIII-Accessibility-Mod/FF8_OriginalPC_mod/`

**Completed milestones:**
- Title screen TTS (v02.00)
- FMV audio descriptions + skip (v03.00)
- Field dialog TTS at v04.36 — all MES/ASK/AMES/AASK/AMESW/RAMESW opcodes hooked; `show_dialog` hook for tutorials/thoughts; walk-and-talk gap remains (hardcoded engine path)
- World map navigation with BFS terrain filtering, auto-drive, location catalog
- Field navigation: entity catalog, GPS navigation, A\* pathfinding with walkmesh, camera-calibrated compass directions, SETLINE/SET3 runtime hooks, JSM scanner for interactive objects
- Battle TTS: command menus, sub-menus (Magic/GF/Draw/Item), EWM, GF fire prevention, victory screen (screenshot-based pipeline), damage/HP announcements (impact-time via sub_5068B0 render hook = production trigger; sub_48EF80 popup-create = diagnostic publisher only; anim-flag-fall = catch-all fallback)
- Junction menu TTS, save/load screen TTS, menu system TTS
- GF summon audio descriptions (v0.14.44) — 18 VTTs covering 16 junctioned GFs + Phoenix + Odin
- **SFX volume control + ducking-toggle scaffold + keyboard layout reshuffle (v0.14.45 + v0.14.46)**
- Multi-channel logging system (6 domain logs); `.inl` file splitting
- Full FF8_EN.exe disassembly reference at `Game Files/disassembly/`

---

**Current build: v0.14.82 — Vulnerable threshold relaxed 60% → 50%. PASSED BAT (Bite Bug Spirit=3, Glacial Eye Spirit=101, Caterchipillar Spirit=18 — all three announces matched predictions exactly; Bite Bug + Caterchipillar gained "Vulnerable to Sleep." second sentence as expected; Glacial Eye unchanged as Sleep at 32% still drops). The Scan TTS announce design is now fully stable. Pending: push v0.14.76 through v0.14.82 to GitHub (verify exact backlog via `github:list_commits` first).**

## v0.14.82 — 50% Vulnerable threshold (relaxed from 60%)

### What shipped

Single-line constant change in `src/scan_tts.cpp::FormatStatusWeaknesses`: `chance >= 60` → `chance >= 50`. Comment block updated to record the lesson. New tier boundaries:

- chance ≥ 95% → Highly vulnerable
- chance 50–94% → Vulnerable
- chance < 50% → silent

Version bumped 0.14.81 → 0.14.82 in `src/ff8_accessibility.h`.

### Why

v0.14.81 BAT validated the chance-based model perfectly (all five scans matched predictions exactly), but cross-reference against nightsolo canon revealed the 60% cutoff was too conservative. Three canon vulnerabilities were being dropped because they landed just below 60%:

- Bite Bug Sleep: chance ~56%
- Caterchipillar Sleep: chance ~53%
- Fastitocalon Darkness: chance ~56%

All three are listed as nightsolo canon vulnerabilities. They land on average more than half the time, so dropping them caused information loss.

Lowering threshold to 50% re-includes them while still correctly dropping the high-Spirit cases that motivated the chance-based model:

- Fastitocalon Sleep: chance 12% → still silent ✓ (the v0.14.80 bug that started this whole chain)
- Glacial Eye Sleep: chance 32% → still silent ✓

"Vulnerable" still has clear meaning: chance ≥ 50% means the status will land on average more than half the time. The boundary is now at the natural "more likely than not" line.

### v0.14.82 BAT plan

Aaron deploys via `deploy.vbs`, scans the same enemies as v0.14.81 BAT (or any subset). Press key 0 to verify the new tier boundaries.

### Predicted announcement changes from v0.14.81

Using same byte/Spirit data from v0.14.81 BAT log:

- **Fastitocalon** (Spirit=183): "Vulnerable to Death, Poison, Petrify, Darkness, Silence, Berserk, and Zombie." (Darkness added at chance 56%; Sleep at 12% stays silent.)
- **Glacial Eye** (Spirit=100): unchanged — "Vulnerable to Death, Poison, Petrify, Darkness, Silence, Berserk, and Zombie." (Sleep at 32% still silent.)
- **Caterchipillar** (Spirit=18): "Highly vulnerable to Death, Poison, Petrify, Darkness, Silence, Berserk, and Zombie. Vulnerable to Sleep." (Sleep added at chance 53%.)
- **Bite Bug** (Spirit=3 or 4): "Highly vulnerable to Death, Poison, Petrify, Darkness, Silence, Berserk, and Zombie. Vulnerable to Sleep." (Sleep added at chance 56–57%.)

Key 9 announcements unchanged from v0.14.80.

### Pass criteria

- Bite Bug and Caterchipillar both gain a "Vulnerable to Sleep." sentence.
- Fastitocalon adds Darkness to its list.
- Glacial Eye announce unchanged.
- Fastitocalon Sleep still does NOT announce.

### After BAT pass

1. Push v0.14.76 + v0.14.77 + v0.14.78 + v0.14.79 + v0.14.80 + v0.14.81 + v0.14.82 to GitHub (verify exact backlog via `github:list_commits` first).
2. Resume deferred priorities.
3. Optional polish per `NEXT_SESSION_PROMPT.md` Priority 2.

### If BAT fails

- Bite Bug Sleep doesn't announce: chance computation regression, or build cache stale. Verify `[SCAN-STAT]` log shows Sleep=50, `[SCAN-CACHE]` shows SPR=3 or 4, then check the version banner in the logs.
- Fastitocalon Sleep DOES announce: threshold not actually applied. Verify the `chance >= 50` constant in `FormatStatusWeaknesses`.
- Other discrepancies: cross-reference against the nightsolo table at https://www.nightsolo.net/games/ff8/part13.html

---

**Previous build: v0.14.81 — Status weakness tiering switched to chance-based (accounts for target Spirit). PASSED BAT (5 scans, all predictions matched log output exactly; cross-reference vs nightsolo canon revealed the 60% threshold was too conservative; fix shipped as v0.14.82).**

## v0.14.81 — chance-based weakness tiering accounting for Spirit

### Root cause

The v0.14.80 BAT exposed a real gap in our model. Aaron cast Sleep on Fastitocalon ~12 times after the announce said "Vulnerable to Sleep" (byte=50). Zero landed. Investigation: Fastitocalon's Spirit stat is **177** (one of the highest in the game by design — it's a magic-resistant fish enemy). Per the FF Wiki Magic page, direct status spell casts use:

```
Inflict % ≈ Magic/4 - Spirit/4 + 100 - byte
```

For Quistis at low level (Magic ~30) casting Sleep on Fastitocalon: `30/4 - 177/4 + 100 - 50 ≈ 13%`. So 12 attempts at 13% = (0.87)^12 ≈ 19% chance of zero lands. Bad luck but not impossible. The mod's announce was misleading: byte=50 alone implies ~50% inflict, but high target Spirit cuts that drastically.

### What shipped

`src/scan_tts.cpp::FormatStatusWeaknesses` rewritten to compute estimated direct-magic-cast inflict chance per status, then tier on chance instead of byte:

- chance ≥ 95% → "Highly vulnerable"
- chance 60–94% → "Vulnerable"
- chance < 60% → silent (drops out of weakness announce)

New helper `ComputeMagicCastChance(byte, spirit)` implements `max(0, min(100, 30/4 - spirit/4 + 100 - byte))` with `kAssumedMagic = 30` representing a typical low-mid game caster. High-level players with Magic 100+ will see actual inflict rates *higher* than the announce suggests — false-negative direction, which is safer than misleading false-positives.

Statuses with byte ≥ 100 still skip to `FormatStatusResistances` (key 9, Resists/Immune to tiers from v0.14.80) — unchanged.

Version bumped 0.14.80 → 0.14.81 in `src/ff8_accessibility.h`.

### Predicted announcement changes per v0.14.79 BAT bytes

Using integer arithmetic (C semantics: `30/4 = 7`, `spirit/4` floored):

**Bite Bug** (Spirit=3, `spirit/4=0`): chance = 107 − byte
- Death=0 → 107→100 → Highly vulnerable
- Poison=1 → 106→100 → Highly vulnerable
- Petrify=0 → 100 → Highly vulnerable
- Darkness=6 → 101→100 → Highly vulnerable
- Silence=0 → 100 → Highly vulnerable
- Berserk=3 → 104→100 → Highly vulnerable
- Zombie=1 → 100 → Highly vulnerable
- Sleep=50 → 57 → **silent** (just below 60%)

Announce: "Highly vulnerable to Death, Poison, Petrify, Darkness, Silence, Berserk, and Zombie." Sleep drops out at 57%. Note: this is the threshold-tuning trade-off — if BAT shows Sleep on Bite Bug actually lands frequently in practice, we lower to 50% in v0.14.82.

**Glacial Eye** (Spirit=100, `spirit/4=25`): chance = 82 − byte
- Death=1 → 81 → Vulnerable
- Poison=1 → 81 → Vulnerable
- Petrify=0 → 82 → Vulnerable
- Darkness=6 → 76 → Vulnerable
- Silence=0 → 82 → Vulnerable
- Berserk=0 → 82 → Vulnerable
- Zombie=1 → 81 → Vulnerable
- Sleep=50 → 32 → silent

Announce: "Vulnerable to Death, Poison, Petrify, Darkness, Silence, Berserk, and Zombie." (No Highly vulnerable tier; Sleep drops at 32%.)

**Fastitocalon** (Spirit=177, `spirit/4=44`): chance = 63 − byte
- Death=1 → 62 → Vulnerable
- Poison=1 → 62 → Vulnerable
- Petrify=1 → 62 → Vulnerable
- Darkness=6 → 57 → **silent** (just below 60%)
- Silence=0 → 63 → Vulnerable
- Berserk=0 → 63 → Vulnerable
- Zombie=1 → 62 → Vulnerable
- Sleep=50 → 13 → silent (this is the bug fix)

Announce: "Vulnerable to Death, Poison, Petrify, Silence, Berserk, and Zombie." Sleep correctly drops out (matches Aaron's experience).

Key 9 announcements unchanged from v0.14.80.

### v0.14.81 BAT plan

Aaron deploys via `deploy.vbs`, scans Fastitocalon. Press key 0 to verify the new chance-based tiering.

**Pass criteria:**

- Fastitocalon key 0 no longer mentions Sleep.
- Bite Bug and Glacial Eye announces match the predictions above (or are close enough that any deviation is explainable from the bytes).
- Optional bonus: cast Sleep on Bite Bug a handful of times. Should land ~57% of the time per our model. If it lands frequently, the 60% threshold is too strict and we lower to 50% in v0.14.82.

### After BAT pass

1. Push v0.14.76 + v0.14.77 + v0.14.78 + v0.14.79 + v0.14.80 + v0.14.81 to GitHub (verify exact backlog via `github:list_commits` first).
2. Resume deferred priorities.
3. Optional polish per `NEXT_SESSION_PROMPT.md` Priority 2.

### If BAT fails

- If Fastitocalon Sleep still announces: `ComputeMagicCastChance` not actually being called. Check `[SCAN-CACHE]` log shows Spirit=177, then verify the `if (b >= 100) continue;` early-skip didn't accidentally skip everything.
- If announce is empty ("No status weaknesses") for a monster that should have several: chance threshold is too aggressive. Lower from 60% → 50% (one-line edit).
- If announce includes Sleep on Fastitocalon despite our prediction: the math is wrong somewhere. Check `[SCAN-STAT]` shows Sleep=50 and `[SCAN-CACHE]` shows SPR=177 (snap.stats[3]).

### Implementation notes

- Helper uses `int` arithmetic; `30/4 = 7` (truncated), not 7.5. This makes the actual thresholds slightly tighter than my earlier verbal math suggested.
- The 60% "Vulnerable" cutoff was chosen conservatively at Aaron's request: "Vulnerable" should mean "will land most casts," not "might land sometimes." Bite Bug Sleep at 57% sits just below the cutoff. If BAT shows that's too strict, lowering to 50% would catch it.

---

**Previous build: v0.14.80 — Status resistances tiered (Resists / Immune to). PASSED BAT (Fastitocalon "Resists Slow, Stop, Doom, and Slow Petrify. Immune to Confuse." announced exactly as predicted). Aaron noted Fastitocalon Sleep didn't land in 12 casts despite "Vulnerable to Sleep" announce; root cause = high enemy Spirit, fixed in v0.14.81.**

## v0.14.80 — symmetric two-tier resistance announce

### What shipped

`src/scan_tts.cpp::FormatStatusResistances` now splits the 13 offensive ailments into two tiers based on raw resistance byte, mirroring the v0.14.78 weakness announce structure:

- `byte 100-199` → "Resists ..." (won't land via direct cast; defeatable with full junction stacking)
- `byte 200+` → "Immune to ..." (cannot be inflicted at all, even with 100 spells junctioned to ST-Atk-J)

Wording uses two separate sentences when both tiers populate: `"Resists Slow, Stop, Doom, and Slow Petrify. Immune to Confuse."` Single-tier cases use just the relevant sentence. Empty case unchanged: `"No status resistances."` `JoinNameList` helper unchanged.

Version bumped 0.14.79 → 0.14.80 in `src/ff8_accessibility.h`.

### Why tiered resistances

Driven by Aaron's design discussion after v0.14.79 BAT: weaknesses already use the "Highly vulnerable / Vulnerable" two-tier structure; resistances should match for a predictable mental model. Aaron's casting-focused use case (players will apply Scan findings to spell casts, not just junctioning) settled the threshold choice.

The FF8 inflict formula gives:
- Direct cast: `inflict % ≈ 100 - byte`
- Junction-stacked: `inflict % ≈ 200 - byte`

So byte=100 is the hard boundary where direct casts stop landing entirely (current "Resists" tier handles that correctly), and byte=200 is the hard boundary where even max junction stacking fails (new "Immune" tier handles that). The byte 100-199 range stays "Resists" because adding a "Mildly vulnerable" middle tier would mislead a casual player into wasting MP on direct casts that can't land.

### v0.14.80 BAT plan

Aaron deploys via `deploy.vbs`, scans Fastitocalon (or any monster with a known byte=200+ status), presses key 9 for resistances. Pass criteria:

- Key 9 announces both tiers in distinct sentences when both populate.
- Key 0 (weakness) announcement unchanged from v0.14.79.
- `[SCAN-STAT]` log line still present per scan.

Predicted announcements per the v0.14.79 BAT bytes:

- **Bite Bug** (no 200+ bytes): key 9 → "Resists Slow, Stop, Doom, Slow Petrify, and Confuse." *(unchanged from v0.14.79)*
- **Fastitocalon** (Confuse=255): key 9 → "Resists Slow, Stop, Doom, and Slow Petrify. Immune to Confuse." *(test case for the new tier)*
- **Glacial Eye** (no 200+ bytes): key 9 → "Resists Slow, Stop, Doom, Slow Petrify, and Confuse." *(unchanged)*

Fastitocalon is the meaningful BAT target — it's the only one of the three v0.14.79 enemies that has a byte≥200 to exercise the new tier.

### After BAT pass

1. Push v0.14.76 + v0.14.77 + v0.14.78 + v0.14.79 + v0.14.80 to GitHub. **Verify backlog size via `github:list_commits` before quoting.**
2. Move to deferred priorities (persistent settings, GF naming bypass verify, party-member field cleanup, X-ATM092 chase scene, etc.).
3. Optional polish per `NEXT_SESSION_PROMPT.md` Priority 2.

### If BAT fails

- If Fastitocalon Confuse still shows in "Resists" instead of "Immune to": the `b >= 200` check isn't matching. Verify `[SCAN-STAT]` log shows Confuse=255 and the order check `if (b >= 200)` runs before `else if (b >= 100)`.
- If both buckets show empty for a monster that previously announced "Resists": loop logic regression. Check `OFFENSIVE_AILMENT_COUNT` and the `OFFENSIVE_AILMENT_INDICES` iteration are unchanged.
- If announce reverts to v0.14.79 single-sentence wording: build cache stale. Check `Logs/build_latest.log` and the `[SCAN-TTS]` banner for version string.

---

**Previous build: v0.14.79 — Repeat-scan detection bug fix. PASSED BAT (Fastitocalon + Bite Bug + Glacial Eye in same play session, all detected with `[NOEFFECT-WATCH] Scan detected` and proper `[SCAN-CACHE]` capture). Cross-referenced status data against nightsolo canon walkthrough; data correct.**

## v0.14.79 — popup-hook condition relaxed to match BOTH text_id 0x02 and 0x06

### Root cause

The v0.14.78 BAT log showed Aaron's reported symptom: only the first scan in a battle was announced; subsequent scans were silent. Direct evidence in the log at 22:01:38:

```
[POPUP] retaddr=0x00485938 slot=0 text_id=0x06(6) value=0x32 extra1=0x9 extra2=0x8
```

The Scan-cast popup arrives with **text_id=0x06**, not 0x02. My v0.14.76 fix was based on a misread of the v0.14.75 BAT log — the changelog claimed "every Scan-cast popup arrives with text_id=0x02, NOT 0x06; text_id=0x06 never fires for any popup" — the v0.14.78 BAT log directly contradicts this.

With v0.14.78's `tid == 0x02` condition, the popup hook never fires for Scan casts. That broke the entire action-layer detection chain:

1. `s_lastScanCastTick` never gets set → `NoEffect_RecordSnapshot`'s scan branch never fires (zero `[NOEFFECT-WATCH] Scan detected` entries in log).
2. The only working detection is `PollBattleMagicId`'s 0→39 transition fallback — catches the FIRST scan in a battle but not subsequent ones because `magic_id` stays at 39.
3. Same bug on the screen-close side: `OnScanPopupDespawn` never fires (zero matches in log), leaving `s_scanScreenActiveSlot` stuck after every Scan UI close.

### What shipped

Defensive OR rather than just reverting to 0x06, because the v0.14.77 BAT (Grat + T-Rex) DID produce `[NOEFFECT-WATCH] Scan detected` entries, which means at least one of those casts had text_id=0x02. Different cast contexts (Magic-cast vs Draw-cast, full vs compacted view, different monsters, different battle states) may use different text_ids and we should be robust to all of them.

- `src/battle_tts_sprite.inl` popup-hook condition: `tid == 0x02` → `(tid == 0x02 || tid == 0x06)`.
- `src/battle_tts_screenshot.inl` screen-close condition: `prev.text_id == 0x02` → `(prev.text_id == 0x02 || prev.text_id == 0x06)`.
- Comment blocks in both files extended to record the v0.14.76 misread and the v0.14.78 BAT direct evidence so future-Claude doesn't re-introduce this.
- Version bumped to 0.14.79.

`value=0x32` (decimal 50, the spell-name display duration) plus the surrounding context (`sub_48E830` fires within milliseconds in `NoEffect_RecordSnapshot`'s window) provides enough specificity that false-positive Scan detection from non-Scan popups is unlikely. If we ever do see false positives, we can add a tighter retaddr-based filter (the Scan popup specifically came from `retaddr=0x00485938`).

### v0.14.79 BAT plan

Aaron deploys via `deploy.vbs`, scans any monster, then scans a SECOND monster in the same battle (or scans the same monster twice if multi-target unavailable).

**Pass criteria:**

- Both scans produce a `[NOEFFECT-WATCH] Scan detected` log line.
- Both scans produce a `[SCAN-CACHE]` capture log line.
- Both Scan UI windows produce an auto-announce when they open.
- Pressing key 0 in each Scan window fires the v0.14.78 tiered weakness announce correctly (the announce logic itself was correct in v0.14.78, but the second scan never reached it because the second scan was never detected at all).
- After Scan UI closes, number keys 1/2/3 revert to default ally HP behavior — confirms `OnScanPopupDespawn` fires.

### If BAT fails

- If second scan still silent, with no `[NOEFFECT-WATCH] Scan detected` in log: text_id is something other than 0x02 or 0x06. Look at the `[POPUP]` entries in the log to find the actual value. The popup hook's diagnostic logger is dedup'd by `(retaddr, slot, text_id)`, so we'll see exactly one `[POPUP]` entry per distinct popup signature.
- If both scans get caught but the second's announce sounds wrong: the issue is in the announce path (`OnScanCast` → `CaptureSnapshot` → `OnScanPopupSpawn`), not in detection. Check whether `s_pendingScanSlot` is being properly consumed and reset between scans.
- If `OnScanPopupDespawn` still doesn't fire: the screenshot.inl `prev.value == 50` part may also need adjustment. Check `[SPRITE-POLL] DESPAWN` log entries to see what `prev.value` actually is at close time.

### After BAT pass

1. Push v0.14.76 + v0.14.77 + v0.14.78 + v0.14.79 to GitHub. Verify exact backlog via `github:list_commits` before quoting (don't repeat the v0.14.72 "~80-build backlog" guess).
2. Resume deferred priorities (persistent settings, GF naming bypass verify, party-member field cleanup, X-ATM092 chase scene).
3. Optional polish: remove redundant Scan branch from `battle_tts_ewm.inl::PollBattleMagicId` (now genuinely redundant once popup hook reliably catches both first and repeat scans — it produces duplicate `[SCAN-CACHE]` log lines on the first scan in each battle).

---

**Previous build: v0.14.78 — Status weaknesses tiered. PARTIAL PASS: weakness tiering announces correctly on first scan; second scan in same battle was silent (root cause now known and fixed in v0.14.79).**

## v0.14.78 — tiered weakness threshold

### What shipped

`src/scan_tts.cpp::FormatStatusWeaknesses` now splits the 13 offensive ailments into two tiers based on raw resistance byte:

- `byte <= 5` → "Highly vulnerable to ..." (effectively guaranteed inflict, 95%+ before junction; the FF8 designers' filler-low values for auto-land ailments)
- `byte 6..50` → "Vulnerable to ..." (junction-amplifiable; 100 spells stacked to ST-Atk-J brings StatusAttack to 200, beating StatusDefense 100+50=150 by 50% per swing for the byte=50 "tutorial midpoint" ailments)
- `byte 51..99` → silent (partial resistance, not a meaningful weakness)
- `byte >= 100` → unchanged, still flows through `FormatStatusResistances` on key 9

Wording uses two separate sentences when both tiers populate: `"Highly vulnerable to A, B, and C. Vulnerable to D."` Single-tier cases use just the relevant sentence. Empty case unchanged: `"No status weaknesses."` `JoinNameList` helper unchanged — same Oxford-comma natural-language formatter shared with `FormatWeak` / `FormatResistances` / `FormatStatusResistances`.

Version bumped 0.14.77 → 0.14.78 in `src/ff8_accessibility.h` with full rationale comment.

### Why this is a deliberate design choice

Investigation of the FF8 Scan UI rendering chain in disassembly (sub_84F860 → sub_84F8D0 → sub_84FD90 → sub_49F0A0) confirmed that the **vanilla Scan UI does not display status weakness or resistance at all** — only stats and elemental affinities. The 20 status bytes exist solely for the inflict probability formula `% chance to inflict before junction = 100 - byte`. There is no game-side threshold to mirror because the game never displays this information. We are surfacing tactical data vanilla FF8 hides; the threshold is entirely our design choice.

The v0.14.77 BAT proved `byte == 0` was too strict. T-Rex's wiki-documented Sleep / Darkness / Death weaknesses sit at bytes 50 / 2 / 1 respectively, not 0, so v0.14.77 announced only the byte==0 ailments (Petrify / Silence / Berserk for both Grat and T-Rex) and missed the canonical FF8 vulnerabilities every player learns about from the Quistis tutorial.

The two-tier split surfaces the canon weaknesses while preserving the magnitude distinction that matters tactically. Death at byte=1 lands on every cast unaided; Sleep at byte=50 needs junction stacking. A blind player making junction decisions wants to know that difference — it's the difference between "cast Death directly" and "junction 100 Sleeps to ST-Atk-J first."

### v0.14.78 BAT plan

Aaron deploys via `deploy.vbs` and re-scans Grat / T-Rexaur (or any monster). On each Scan UI open, presses key 0 for weaknesses. Pass criteria:

- Key 0 announces both tiers in distinct sentences when both populate.
- `[SCAN-STAT]` log line still present (unchanged) so threshold tuning beyond v0.14.78 stays data-driven.
- Key 9 announcement unchanged from v0.14.77.

Predicted announcements per the v0.14.77 BAT bytes:

- **Grat** (Death=1 Poison=1 Petrify=1 Darkness=2 Silence=0 Berserk=0 Zombie=1 Sleep=50): key 0 → "Highly vulnerable to Death, Poison, Petrify, Darkness, Silence, Berserk, and Zombie. Vulnerable to Sleep."
- **T-Rexaur** (Death=1 Poison=1 Petrify=0 Darkness=2 Silence=0 Berserk=0 Zombie=1 Sleep=50): key 0 → "Highly vulnerable to Death, Poison, Petrify, Darkness, Silence, Berserk, and Zombie. Vulnerable to Sleep."

(Same shape because both monsters are wide-open offensively per FF8 design — the wiki's "Sleep / Darkness / Death" curated list is a strategic-value subset, not the raw resistance data. Surfacing the raw data with magnitude tiers gives the player better info than the wiki's curated subset.)

### After BAT pass

1. Push v0.14.76 + v0.14.77 + v0.14.78 to GitHub. **Verify backlog size via `github:list_commits` before quoting** — don't repeat the v0.14.72 mistake of guessing.
2. Move to deferred priorities (persistent settings, GF naming bypass verify, party-member field cleanup, X-ATM092 chase scene, etc.).
3. Optional polish: remove redundant Scan branch from `battle_tts_ewm.inl::PollBattleMagicId` (cosmetic log noise), update stale comment at top of `scan_tts.cpp` line 26 ("Fields 5..0 reply Not implemented yet." — obsolete since v0.14.74).

### If BAT fails

- If both Grat and T-Rex still announce only "Highly vulnerable" without the "Vulnerable" Sleep sentence: the byte=50 case isn't matching `b <= 50`. Check `else if (b <= 50)` clause didn't get mangled; verify `snap.statusRes[7]` actually reads 50 in the `[SCAN-STAT]` log.
- If announce contains only Sleep without the seven "Highly vulnerable" entries: tiers got swapped. Check ordering in the `if (hCount > 0)` / `if (rCount > 0)` blocks.
- If announce reverts to v0.14.77's flat "Weak to ..." wording: build cache stale, deployed dll is older. Check `Logs/build_latest.log` and the `[SCAN-TTS]` banner in the battle log.

---

**Previous build: v0.14.77 — Status resist offset corrected to +0x80; keys 9/0 wired. AWAITING BAT.**

## v0.14.77 — ship the +0x80 offset fix

Applied per the v0.14.76 BAT analysis below. Three changes total:

1. `BENT_STATUS_RESIST_BASE` already updated to `0x80` in `src/battle_tts.h` (BAT-validation comment block in place).
2. `src/scan_tts.cpp::SpeakField` case 9 re-enabled to call `FormatStatusResistances` (was 'Not implemented yet.' stub from v0.14.74.1).
3. `src/scan_tts.cpp::SpeakField` case 0 re-enabled to call `FormatStatusWeaknesses` (was 'Not implemented yet.' stub from v0.14.74.1).
4. `[SCAN-STRUCT]` 121-byte hex dump stripped from `CaptureSnapshot` (purpose served — actual offset found via the v0.14.76 BAT). `DumpRow` and `LogStructDump` helper functions also removed since they're now unreferenced. `[SCAN-CACHE]`, `[SCAN-ELEM]`, and `[SCAN-STAT]` log lines preserved — those remain useful for ongoing canon validation.
5. Version bumped to 0.14.77.

No new addresses, no schema changes, no new hooks. Pure wiring + cleanup.

### v0.14.77 BAT plan

Aaron deploys via `deploy.vbs` and casts Scan against any monster, then presses keys 9 and 0 while the Scan window is open.

**Pass criteria:**

- Key 9 (Status Resistances): announces 'Resists ...' or 'No status resistances' — should NOT mis-announce (no garbled status names; should be coherent given the +0x80 BAT-validated bytes).
- Key 0 (Status Weaknesses): announces 'Weak to ...' or 'No status weaknesses' — again should be coherent.
- `Logs/ff8_battle.log` should contain a `[SCAN-STAT]` line for each scan with the 20 raw byte values — this gives data-driven calibration if either announce sounds wrong.
- No `[SCAN-STRUCT]` lines in the log (those are stripped).

**Predicted outcomes per the v0.14.76 BAT samples (+0x80 = first byte of resist block):**

- **Grat** (Lv15, monsterId 0x1F): +0x80..+0x87 = `01 01 01 02 00 00 01 32`. Offensive-ailment indices [Death=0, Poison=1, Petrify=2, Darkness=3, Silence=4, Berserk=5, Zombie=6, Sleep=7]. Byte == 0 hits at idx 4 (Silence) and idx 5 (Berserk). Key 0 expected announce: 'Weak to Silence and Berserk.' Key 9 expected announce: 'No status resistances.' (all 13 offensive ailments are < 100 in this sample.)
- **T-Rexaur** (Lv21, monsterId 0x43): +0x80..+0x87 = `00 01 00 02 00 03 01 32`. Byte == 0 hits at idx 0 (Death), idx 2 (Petrify), idx 4 (Silence). Key 0 expected announce: 'Weak to Death, Petrify, and Silence.' Key 9 expected announce: 'No status resistances.'

**Calibration consideration:** T-Rex is famously also weak to Sleep, Darkness, and Poison per FF Wiki, but those bytes are 0x32/0x02/0x01 in the sample (small but non-zero). The current `byte == 0` threshold won't catch those. If Aaron wants the announce to match canon more closely we can relax the threshold to `byte < N` (e.g. N=10 or N=50) in v0.14.78+. The `[SCAN-STAT]` log line gives the data needed to pick the right N.

### If BAT fails

- If keys 9/0 announce nonsense statuses (random list): the +0x80 hypothesis is wrong despite the canon validation. Resume the disassembly hunt at `sub_84FD90` (phase 1 handler of `sub_84F860`).
- If keys 9/0 announce nothing at all ("No status..." for everything): formatters may not be wired correctly — verify `FormatStatusResistances` and `FormatStatusWeaknesses` are still calling `OFFENSIVE_AILMENT_INDICES` indexing into `snap.statusRes`.
- If announce still says 'Not implemented yet.': version didn't bump or build didn't pick up the case-body changes — check `Logs/build_latest.log` and the deployed dll's banner.

### After BAT pass

1. Push v0.14.76 + v0.14.77 to GitHub. Verify backlog size via `github:list_commits` before quoting.
2. Optional polish (v0.14.78+):
   - Threshold calibration for key 0 (relax `byte == 0` to `byte < N` based on T-Rex / Tonberry data).
   - Remove redundant Scan branch from `battle_tts_ewm.inl::PollBattleMagicId` (cosmetic log-noise cleanup).
   - Update stale comment at top of scan_tts.cpp line 26 ('Fields 5..0 reply Not implemented yet.') to reflect v0.14.74+ field bindings.
3. Move to deferred priorities (persistent settings, GF naming bypass verify, party-member field cleanup, etc.).

---

**Previous build: v0.14.76 — Scan-cast detection fix. BAT PASSED 2026-05-03 12:18.**

## v0.14.76 BAT result: clean PASS

Log: `Logs/ff8_battle.log` (433 KB, 2026-05-03 12:19:22 → 12:23:07). 2-Grat encounter, T-Rexaur action target. Confirmed signals:

- Action-layer fire @ 12:20:33 (Grat) and 12:22:23 (T-Rexaur) — popup-hook path now fires, no longer silent
- `sub_B687C0 fire #1` auto-announce @ 12:20:46 (Grat) and 12:22:31 (T-Rexaur)
- All 9 number keys exercised on T-Rexaur — fields 1–8 returned correct data, field 9 returned `'Not implemented yet.'` (correct stub per v0.14.74.1)
- Screen close @ 12:23:04: `[SCAN-TTS] Screen closed (slot=3); number keys revert to ally HP` — `OnScanPopupDespawn` fires correctly via the parallel `prev.text_id == 0x02` fix
- No `[SPELL-NOEFFECT]` watchdog spam this time

Duplicate `[SCAN-CACHE]/[SCAN-STRUCT]` block @ 12:20:34 from `PollBattleMagicId` fallback is the expected harmless cosmetic noise. Scheduled for v0.14.78+ cleanup (remove redundant Scan branch from `battle_tts_ewm.inl::PollBattleMagicId`), not blocking.

## v0.14.77 status-resist offset hunt — DEEP RESEARCH HYPOTHESIS INVALIDATED

The v0.14.76 BAT log included full `[SCAN-STRUCT]` dumps from both Grat (Lv15, monsterId 0x1F) and T-Rexaur (Lv21, monsterId 0x43). Comparing the two:

| Offset range | Grat | T-Rexaur | Verdict |
|---|---|---|---|
| +0x3C..+0x4B | `02 03 02 03 20 03 ...` | `20 03 8A 02 20 03 52 03 20 03 ...` | Element u16 — CORRECT (matches SCAN-ELEM) |
| **+0x4C..+0x6B** (32 bytes) | `A9 FB A9 FB ...` | `A9 FB A9 FB ...` | **IDENTICAL garbage — NOT status resist** |
| +0x6C..+0x73 | `18 FC 00 00 30 F8 00 00` | `00 00 00 00 3C F6 00 00` | differs (reward bytes?) |
| +0x80..+0x93 (20 bytes) | `01 01 01 02 00 00 01 32 FF 96 A0 FF FF FF FF 64 82 FF FF FF` | `00 01 00 02 00 03 01 32 AA 78 A0 78 78 8C 82 64 96 64 6E B4` | **PLAUSIBLE status resist** |
| +0xB3 / +0xB4 | 0x1F / 0x0F | 0x43 / 0x15 | monsterId / level — CORRECT |

The deep research doc (`Plan & Research Documents/Scan spell deep research results.md`) hypothesized `entity_base + 0x4C` as the status resist offset, BUT explicitly flagged this as needing live validation: "The single biggest unknown that requires your live debugger to settle definitively is the exact threshold immediate in the Scan-UI status loop." The two BAT samples now invalidate the 0x4C hypothesis — both monsters return identical alternating `A9 FB` bytes, which is uninitialized memory or a junction-defense scratch buffer that's the same for all enemies.

### Best candidate: +0x80 (20 bytes ending at +0x93)

Walking the +0x80..+0x93 window against the deep-research-confirmed status order [Death, Poison, Petrify, Darkness, Silence, Berserk, Zombie, Sleep, Haste, Slow, Stop, Regen, Reflect, Doom, SlowPetrify, Float, Confuse, Drain, Expulsion, ???]:

- **Sleep (idx 7) at +0x87**: BOTH = 0x32 (50). Per FF formula `100 - byte = chance to inflict`, Sleep is 50% chance on both — matches Quistis tutorial ("put Grats to sleep") and FF Wiki canon (T-Rexaur weak to Sleep). ✓
- **Death (idx 0) at +0x80**: Grat=0x01, T-Rex=0x00 — both highly vulnerable. Matches T-Rex canon. ✓
- **Berserk (idx 5) at +0x85**: Grat=0x00 (highly vulnerable), T-Rex=0x03. Matches Grat-vulnerable-to-Berserk canon. ✓
- **Haste (idx 8) at +0x88**: Grat=0xFF, T-Rex=0xAA. Both immune (positive status, expected). ✓
- **Pattern**: low values for negative statuses (idx 0-7), high values for positive statuses (idx 8-19). This is the canonical FF8 enemy resist profile.

Alternative window +0x88..+0x9B was considered but Grat shows Sleep=0x64 (100, exactly at immune threshold), inconsistent with Grat's known Sleep vulnerability.

### v0.14.77 decision: ship with +0x80, BAT-validate

Pivoted away from continuing the disassembly hunt. Rationale:

- The deep research's predicted threshold `cmp al, 0x64` does NOT exist anywhere in `0x00801000.asm` — so the deep research's threshold model is wrong, which weakens the case that its broader instruction predictions are reliable enough to find quickly.
- `mov word ptr [edx + 0x4c], ax` confirms +0x4C is u16 UI scratch (writing 2 bytes per frame), independent confirmation that 0x4C is wrong.
- The +0x88 alternative fails canon for both monsters (Grat Death=255 immune contradicts canon; Grat Sleep=100 immune contradicts Quistis tutorial).
- The +0x80 hypothesis matches FF Wiki canon for **every** validated status across **both** monsters: T-Rex weak to Death/Poison/Darkness/Sleep, Grat weak to Sleep/Silence/Berserk, both immune to all 12 positive statuses.
- Sleep=0x32 (50) appearing at the exact same byte position in two completely different monsters is structurally telling — engine writing 50 to status idx 7 of both, not coincidence. The 8-low-then-12-high distribution shape is the canonical FF8 monster resist profile.

Reading `sub_84FD90` exhaustively to find the loop would take 30+ more probe cycles. ROI is poor versus running 1 BAT.

### v0.14.77 actions

1. Update `BENT_STATUS_RESIST_BASE` from `0x4C` to `0x80` in `src/battle_tts.h`
2. Re-enable keys 9 (resist) and 0 (active statuses) in `src/scan_tts.cpp::SpeakField` — call `FormatStatusResistances` / `FormatStatusWeaknesses` (formatters already exist, just unwired per v0.14.74.1 stubs)
3. Strip the `[SCAN-STRUCT]` diagnostic dump from scan_tts.cpp (no longer needed)
4. Bump version to 0.14.77
5. BAT: Grat key 9 should announce something sensible (mostly positive statuses listed as resists; no canonical vulnerabilities listed as immune). If garbage, +0x80 is wrong and we resume disassembly at `sub_84FD90`.

### Disassembly findings — sub_84F860 is a phase DISPATCHER, not the loop home

Read `sub_84F860` directly from `Game Files/disassembly/FF8_EN_.text_0x00801000.asm`. Key finding: the deep research mis-described the function's role. `sub_84F860` is a 6-entry phase dispatcher, not a render function:

```
0x0084F860:  sub  esp, 0x18
             push esi
             mov  esi, [esp+0x20]   ; arg = scan_state struct
             push esi
0x0084F869:  mov  [esp+0x08], 0x84F8D0   ; phase 0
0x0084F871:  movsx eax, byte [esi+0x29] ; phase index byte
0x0084F875:  mov  [esp+0x0C], 0x84FD70   ; phase 1
0x0084F87D:  mov  [esp+0x10], 0x850650   ; phase 2
0x0084F885:  mov  [esp+0x14], 0x850690   ; phase 3
0x0084F88D:  mov  [esp+0x18], 0x8506B0   ; phase 4
0x0084F895:  mov  [esp+0x1C], 0x850740   ; phase 5
0x0084F89D:  call [esp + eax*4 + 8]      ; dispatch
0x0084F8A1:  mov  al, byte [esi+0x26]    ; flag check
0x0084F8A7:  inc  word [esi+0x24]        ; advance frame counter
0x0084F8CF:  ret
```

Function is only 0x70 bytes (~30 instructions). The status loop must be in one of the 6 phase subroutines:
- Phase 0 = `sub_84F8D0` — already read; uses `[ebx+0x2d]` as small dispatch byte; calls `scan_get_text_sub_B687C0` once. Looks like a category-by-category text-render dispatch.
- Phase 1 = `0x84FD70` (thin wrapper that just calls `sub_84FD90`) — `sub_84FD90` allocates 0x68 stack, substantial body. **Most likely status-loop home.**
- Phase 2 = `0x850650` — small (next function at 0x850690 means it's 0x40 bytes max)
- Phase 3 = `0x850690` — small (0x20 bytes)
- Phase 4 = `0x8506B0` — moderate (0x90 bytes)
- Phase 5 = `0x850740` — moderate

Note: `sub_84F8D0` is one of the FFNx-named functions per deep research, but it's the phase 0 handler, not the umbrella that contains the loop.

### Search patterns RULED OUT in `0x00801000.asm`

- `cmp al, 0x64` — **does not exist anywhere in this .asm file**. Deep research's predicted threshold (100 = immune) is wrong.
- `cmp al, 0x63` — does not exist.
- `cmp al, 0x65` — does not exist.
- `byte ptr [ebx + 0x4c]` — does not exist.
- `byte ptr [esi + 0x4c]` — only at `0x820DE5` (write, not read; in unrelated function). Not in scan UI region.

This CONFIRMS the BAT-data finding that 0x4C is wrong. The actual offset has not been located yet.

- `byte ptr [esi + 0x80]` first hit at `0x8C369A` (write of `0x36`, unrelated function). Need to check inside `sub_84FD90` and other phase functions specifically.
- `byte ptr [esi + 0x88]` — does not exist anywhere in this .asm file.
- `byte ptr [ebp + 0x4c]`, `byte ptr [edi + 0x4c]` — do not exist.

### Next session priorities

1. **Read `sub_84FD90` body** (starts at line 108039 of `FF8_EN_.text_0x00801000.asm`, allocates 0x68 stack so substantial). Use multi-anchor `edit_file` dryRun probes to map the function — anchors every ~50 lines covering 0x84FD90 to 0x850650.
2. Search inside it for: 20-iteration loop (`cmp REG, 0x14`), per-byte read with offset (`movsx`/`movzx`/`mov al`), and the threshold compare (NOT 0x64).
3. **Fallback**: check FFNx source `ff8_data.cpp` for any `scan_state` struct definitions or named field offsets that would tell us where the per-monster data is loaded from. FFNx may name the entity-struct fields explicitly even if the `cmp` immediate isn't symbolized.
4. **Fallback 2**: search inside `scan_get_text_sub_B687C0` (0x00B687C0, in `FF8_EN_.text_0x00B01000.asm`) — the per-status filter may live inside the text-fetch helper rather than the caller.
5. Once offset found: update `BENT_STATUS_RESIST_BASE` in `src/battle_tts.h`, re-enable keys 9/0 in `scan_tts.cpp::SpeakField`, strip `[SCAN-STRUCT]` diagnostic, bump to v0.14.77.

### Lesson for next pruning pass

The deep research's `cmp al, 0x64` threshold prediction was as confidently stated as the `0x4C` offset prediction, and both turned out to be wrong. **When deep research describes a low-level instruction sequence (not just an offset), every named immediate is also a hypothesis that needs disasm confirmation.** Don't grep for the predicted bytes and panic when they don't appear — that's evidence the encoding is different, not that the disassembly is corrupt.

## v0.14.76 GitHub push pending

Main HEAD is currently at v0.14.72 per recent_updates memory note (commit 337cf97a) OR at v0.14.75 per pre-compaction summary (commit a2bfc253). One is stale. Verify with `github:list_commits` BEFORE quoting backlog size — that's exactly the lesson from the recent_updates note that triggered this caution.

---

# v0.14.76 root cause and fix (kept for reference) — Scan-cast detection silent text_id mismatch



## v0.14.76 root cause: silent text_id mismatch dating to v0.14.55

v0.14.75 BAT log (`1777827377893_ff8_battle.log`, 4796 lines) reported as “T-Rexaur scan triggered in compacted view despite the config setting being changed.” That diagnosis was wrong — the log shows `[SCAN-HOOK] sub_B687C0 fire #1` firing for the T-Rexaur scan at line 4588, which compacted view skips entirely. The Config `Scan: Always` setting is working correctly. The actual bug is a separate code regression introduced in v0.14.55 and undetected for ~21 builds.

### Smoking gun

`battle_tts_sprite.inl::HookedPopupSprite` (the central popup-sprite dispatcher hook on `sub_48D200`) had this condition for capturing the Scan-cast moment:

```c
if (tid == 0x06 && (value & 0xFF) == 0x32) {
    InterlockedExchange(&s_lastScanCastTick, (LONG)GetTickCount());
}
```

**`text_id == 0x06` never matches any popup.** Empirical evidence from the v0.14.75 BAT log via `grep "text_id=0x"`: every Scan cast produces a popup with **`text_id=0x02`**, not 0x06. Across 4796 lines of battle log, `text_id=0x06` has zero matches anywhere. The popup-hook detection has been silently dead since v0.14.55.

Matching bug at `battle_tts_screenshot.inl::PollPopupRecords` line ~909:

```c
if (prev.text_id == 0x06 && prev.value == 50) {
    ::ScanTTS::OnScanPopupDespawn();
}
```

Same wrong constant. `OnScanPopupDespawn` has never fired since v0.14.59 — latent bug masked because Aaron hadn't pressed number keys 1–0 after closing a Scan UI in any past BAT.

### Why the bug was masked for 21 builds

`battle_tts_ewm.inl::PollBattleMagicId` has a parallel detection path: when `*battle_magic_id` transitions from non-39 to 39, it calls `ScanTTS::OnScanCast(slot, true)`. For first-of-battle scans this works — magic_id is typically 0 at battle entry, transitions 0→39 on the cast, the poll detects, OnScanCast fires with proper [SCAN-CACHE]/[SCAN-STRUCT] logging. That made every Grat/Bite Bug/Fastitocalon scan in informal testing look fine.

For REPEAT scans in the same battle the polling path is dead. Once magic_id is 39, it stays at 39 (the engine doesn't reset it between casts in the same battle). No transition = no fire. With the popup-hook primary path also broken, neither detection path fires for the second+ Scan in a battle.

T-Rexaur scan attempt at v0.14.75 BAT line 4480–4630 is the canonical bad case:

- 10:36:34 line 4481: `[POPUP] retaddr=0x00485938 slot=0 text_id=0x02(2) value=0x32` — popup fires (text_id 0x02 not 0x06, hook does nothing)
- 10:36:34 line 4485: `[NOEFFECT-WATCH] start slot=3 hp=16075` — sub_48E830 fires `NoEffect_RecordSnapshot`; reads `s_lastScanCastTick == 0`; skips Scan-detection branch; records normal watchdog
- 10:36:40 line 4566: `[NOEFFECT-Q] slot=3 kind=no-effect queued: No effect on T-Rexaur.`
- 10:36:43 line 4588: `[SCAN-HOOK] sub_B687C0 fire #1 slot=3 (window-open trigger — announcing now)`
- 10:36:43 line 4589: `[SCAN-TTS] OnScanPopupSpawn: no pending slot (action-layer didn't fire; possible Doomtrain edge case)`
- 10:36:50 line 4630: `[NOEFFECT-FLUSH] No effect on T-Rexaur.` — Aaron HEARS this announcement

From Aaron's POV: cast Scan on T-Rexaur, hear nothing for 16 seconds, then hear "No effect on T-Rexaur." Looks like compacted view ate the announcement; actually the popup hook ate it.

## v0.14.76 fix

Two one-token changes plus expanded comments documenting the v0.14.55 transcription error:

1. `src/battle_tts_sprite.inl::HookedPopupSprite` — `tid == 0x06` → `tid == 0x02`
2. `src/battle_tts_screenshot.inl::PollPopupRecords` — `prev.text_id == 0x06` → `prev.text_id == 0x02`
3. `src/ff8_accessibility.h` — version bumped to 0.14.76 with full changelog

File of every popup in the v0.14.75 BAT log, sorted by retaddr:

```
[POPUP] retaddr=0x00485938 ... text_id=0x01 value=0x0   — player physical attack
[POPUP] retaddr=0x00485938 ... text_id=0x02 value=0x32  — player magic cast (Scan, value=spell_id)
[POPUP] retaddr=0x00485938 ... text_id=0x03 value=0x41  — player limit-break / item
[POPUP] retaddr=0x00489FBA ... text_id=0x08 value=0x6C  — enemy/spell damage popup
```

The `tid == 0x02 && (value & 0xFF) == 0x32` filter uniquely identifies Scan casts because Scan is the only spell with ID 50 (0x32) in vanilla FF8.

## v0.14.76 BAT plan

Aaron deploys via `deploy.vbs`, then in Balamb Garden Training Center forces a multi-enemy battle that allows two Scan casts. Two practical scenarios:

- **2-Grat encounter**: Scan Grat 1 (verifies first-scan path still works), then Scan Grat 2 (verifies the popup-hook fix — same battle_magic_id=39 already set so polling fallback is dead, popup hook is the only path).
- **Grat → T-Rexaur**: scan Grat first, escape if needed, then re-engage to find T-Rexaur. Less reliable encounter-wise; the 2-Grat path is preferred.

Expected log evidence on success:
- Grat scan (first): `[NOEFFECT-WATCH] Scan detected (scanCastTick=...)` AND `[SCAN-CACHE]/[SCAN-ELEM]/[SCAN-STAT]/[SCAN-STRUCT]` lines fire from the popup-hook path. PollBattleMagicId may also detect transition 0→39 and produce a duplicate [SCAN-CACHE] block ~1 second later — this is harmless cosmetic noise (OnScanCast's per-slot 30 s lock from v0.14.57 prevents duplicate user-facing announcements).
- Grat 2 scan (repeat): `[NOEFFECT-WATCH] Scan detected` fires, [SCAN-CACHE] etc. fire ONCE (no PollBattleMagicId duplicate because no transition). NO `[SPELL-NOEFFECT]` queued. NO `[NOEFFECT-FLUSH] No effect on Grat`.
- After closing each Scan UI: `[SPRITE-POLL] DESPAWN ... kind=0x02 val=50` should be followed by an `OnScanPopupDespawn` call (no log line for that one but s_scanScreenActiveSlot will be cleared, observable by Aaron pressing number keys after Scan UI close — they should announce ally HP, not stale scan data).

If the BAT shows a duplicate [SCAN-CACHE] block on first-of-battle scans the noise is acceptable and we can clean it up in a future build by removing the Scan branch from PollBattleMagicId. Don't block on that.

## What this UNBLOCKS

The top priority from `NEXT_SESSION_PROMPT.md` was the BENT_STATUS_RESIST_BASE three-enemy diagnostic (Grat / T-Rexaur / Tonberry), blocked because we couldn't reliably get [SCAN-STRUCT] dumps for repeat-scan enemies in the same battle. v0.14.76 fixes that. After BAT pass, v0.14.77+ collects the three reference dumps and diffs them to find the correct status resist offset; re-enables keys 9/0 (FormatStatusResistances/FormatStatusWeaknesses currently stubbed to "Not implemented yet." per v0.14.74.1).

## Lessons identified (defer to memory pruning — memory is at 30/30)

1. **One detection path can silently mask another path's bugs.** v0.14.55's popup-hook code was wrong from day one; PollBattleMagicId's transition-based path caught first-of-battle scans, which is what informal testing happened to exercise. The bug went 21 builds without detection. Rule of thumb: when the architecture has multiple detection paths, every path needs its own test scenario that exercises that path alone.
2. **Trust log evidence over user theory when diagnosing.** Aaron's report mentioned compacted view; the log clearly showed sub_B687C0 firing (which compacted view skips), so the bug had to be elsewhere. Symptom report and root cause can diverge — always verify the user's diagnosis against the log before acting on it.

## Files modified

- `src/battle_tts_sprite.inl` (~25 lines: condition flipped + 19-line comment block extension explaining the v0.14.55 transcription error)
- `src/battle_tts_screenshot.inl` (~10 lines: condition flipped + 6-line comment block extension)
- `src/ff8_accessibility.h` (this version with full changelog)

## DEVNOTES rotation overdue

DEVNOTES.md is at ~156 KB, well over the 10 KB target from the session ritual rule. Multiple completed investigations (v0.14.45 ducking, v0.14.50–62 Scan TTS chapter, v0.14.65–70 scan UI render hooks, v0.14.71–72 BT-HOOK conflict, v0.14.74.x stale-data fixes) should be moved to `DEVNOTES_HISTORY.md`. Tracked in NEXT_SESSION_PROMPT.

---

**Previous build: v0.14.75 — Keybinding refactor. BAT PASSED. PUSHED to GitHub as commit `a2bfc253` (2026-05-03 16:12 UTC).**

## v0.14.75 GitHub push: COMPLETE

`main` HEAD advanced from `337cf97a` (v0.14.72) to `a2bfc253` (v0.14.75) in a single squashed commit covering the v0.14.73 → v0.14.75 range. Per-version detail (v0.14.73 / v0.14.73.1 / v0.14.74 / v0.14.74.1 / v0.14.74.2 / v0.14.74.3 / v0.14.74.4-diag / v0.14.75) preserved in `src/ff8_accessibility.h`'s changelog comments. Local and remote are in sync.

## v0.14.75 BAT result: PASS

Aaron deployed and tested all keyboard shortcuts — every binding behaves as expected:
- F11 fires the on-demand screenshot capture in any game state.
- M while the in-game menu is open speaks the party / Gil / time / location combined readout.
- F12 produces no output (reservation confirmed).
- All other hotkeys (V/G/T/L/R/`/F1–F10/etc.) work unchanged.

During testing Aaron flagged a separate latent bug: pressing **R** in the in-game menu always announces "No SeeD rank yet" even after he's earned a SeeD rank. This is unrelated to v0.14.75's changes — the keybinding works correctly, the savemap-read function is the issue. Filed as GitHub issue **#27** (https://github.com/ampage87/FFVIII-Accessibility-Mod/issues/27, labels: `bug`, `menu-tts`, `savemap-offsets`, `low-priority`). Hypothesis in the issue: `FIELD_H_OFFSET = 0xF94` in `src/menu_tts_hotkeys.inl::AnnounceSeedRank()` is computed by stacking section sizes (header + GFs + chars + shops + limit_breaks + items) and one of the section sizes is likely wrong, mirroring the SAVEMAP OFFSET CORRECTION lesson from earlier deep research work. Fix is deferred to a future session via runtime offset hunt or fresh deep research.

## v0.14.75 — keybinding ownership settled

Aaron asked which F11 functions could go and proposed moving the screenshot to F11 with F12 returning to its reserved-for-diag role. Source audit found four bindings on F11 in three files; three were research diagnostics for closed investigations, one was a user feature.

### REMOVED (3 diagnostics for closed investigations)

1. **`field_nav_handlekeys.inl` F11 — VISDIAG dump.** v05.69 research diagnostic that dumped candidate visibility-flag bytes (+0x188 / +0x1A0 / +0x21A / +0x240) for every model-bearing field entity. Used during the entity catalog build to find the SHOW/HIDE flag offset. Catalog is built and works — dump is dormant. ~50 lines of body deleted plus the `s_f11WasDown` static in `field_navigation.cpp`.

2. **`menu_tts.cpp` Shift+F11 — `StartMemoryMonitor`.** v0.08.22 research diagnostic that snapshotted 2 KB around `pMenuStateA` every 200 ms for 15 seconds, logging byte changes. Used to discover submenu cursor offsets (`SUBMENU_LIST_CURSOR_OFFSET=0x272`, `ITEM_FOCUS_STATE_OFFSET=0x22E`, `JUNC_FOCUS_OFFSET=0x22E`, etc.). All offsets now hardcoded — monitor is dormant. Call site removed; orphaned `PollMemoryMonitor()` polling call also removed (no caller of `StartMemoryMonitor` left, polling was a permanent no-op).

3. **`menu_tts.cpp` Ctrl+F11 — `DumpMenuScreenData`.** v0.08.17 research diagnostic that hex-dumped the savemap header / character structs / post-character region. Used to find the savemap base address (`SAVEMAP_BASE=0x1CFDC5C`) and verify offsets. All offsets now hardcoded — dump is dormant. Call site removed.

Function definitions for all three (`StartMemoryMonitor` / `PollMemoryMonitor` / `DumpMenuScreenData` / `LogSaveSubsystemChanges` etc.) remain in `menu_tts_diagnostics.inl` as harmless dead code in case a future investigation needs them. Only the call sites were removed.

### MOVED (1 user feature relocated)

4. **`menu_tts.cpp` plain F11 — `AnnounceMenuSummary`.** v0.08.20 USER FEATURE despite living in `_diagnostics.inl`. Speaks party composition + per-character HP and level + Gil + play time + location, the "where am I, what's my state" combined readout when the in-game menu is open. Complements the per-fact `G` / `T` / `L` / `R` hotkeys with a one-shot all-in-one. Aaron had not flagged it for removal — he expected to lose only diagnostics. **Moved to the `M` key**, gated on `isMenuMode` (game mode 6) so it only fires when the in-game menu is actually open. M was confirmed free across `dinput8.cpp` / `menu_tts.cpp` / `world_map.cpp` / `field_navigation.cpp` / `battle_tts.cpp` via dryRun probes before binding.

### MOVED (screenshot trigger to its permanent home)

5. **`dinput8.cpp` global F11 — on-demand screenshot capture.** Was F12 in v0.14.74.4-diag; now F11. Same `RequestScreenshotAsync` mechanism, same `BattleTTS::GetScreenshotDir()` path, same "Screenshot captured." speech confirmation. Only the keybinding and file-name prefix changed (`f12_<HHMMSS>_<MS>` → `f11_<HHMMSS>_<MS>`) plus the log tag (`[F12-SCREENSHOT]` → `[F11-SCREENSHOT]`). Works in any game state because the `SwapBuffers` hook installed by `BattleTTS::Initialize` is global.

### F12 RESERVED

F12 has no consumer in this build. Pressing it does nothing (correct). Per the F12 rule in userMemories: F12 is reserved exclusively for per-session diagnostic builds. Before adding ANY F12 handler in the future, search ALL source files for `VK_F12` / `F12` / `0x7B` references and remove old diagnostic code first. Only one F12 diagnostic active at a time. v0.14.75 brings F12 back to the clean state the rule requires.

### Files modified

- `src/dinput8.cpp` (~30 lines: F12 → F11 rename, comment block updated, closing comment now "F12 has no consumer in this build; reserved exclusively for per-session diagnostic builds.")
- `src/menu_tts.cpp` (~15 lines: F11 hotkey block replaced with simple M handler, `PollMemoryMonitor()` call removed)
- `src/field_nav_handlekeys.inl` (~50 lines deleted: VISDIAG dump body)
- `src/field_navigation.cpp` (1 line deleted: `s_f11WasDown` static + comment swap)
- `src/ff8_accessibility.h` (this version with full changelog)

### Behavior expectation

NO behavior regressions. Every user-facing feature still has a binding (just reshuffled); only diagnostic dead code was deleted. Aaron's BAT next session will verify:

- F11 in any game state (title / field / menu / battle / world map) → "Screenshot captured." + PNG appears at `Logs/screenshots/f11_<HHMMSS>_<MS>.png`.
- M while in the in-game menu (mode 6) → menu summary announces party / HP / Gil / time / location.
- F12 → nothing happens (reservation confirmed).
- All other hotkeys (G/T/L/R/V/F1-F10/`/O/1-9/0/H/`/`+/-/Backspace/`\`) unchanged from v0.14.74.x.

### After BAT — GitHub push

v0.14.75 ships ~6–7 builds to GitHub depending on what was already pushed (verify with `github:list_commits` first per the recent_updates lesson — "never quote a backlog size from memory"). The build chain since GitHub HEAD as of 2026-05-02 19:31 UTC commit 337cf97a (v0.14.72): v0.14.73, v0.14.73.1, v0.14.74, v0.14.74.1, v0.14.74.2, v0.14.74.3, v0.14.74.4-diag, v0.14.75.

---

**Previous build: v0.14.74.4-diag — F12 = on-demand screenshot. BAT PASSED. Compacted-view problem solved by config setting (no fallback code needed).**

## v0.14.74.4-diag BAT result: COMPACTED-VIEW SOLVED VIA IN-GAME CONFIG

Aaron BAT'd v0.14.74.4-diag at 23:04:16. F12 hotkey worked first try — captured the Config menu cleanly via the SwapBuffers hook + glReadPixels machinery, lands at `Logs/screenshots/f12_<HHMMSS>_<MS>.{bmp,png}`, no behavior regressions elsewhere.

**The screenshots revealed something I'd given up on prematurely earlier in the session: FF8's Config menu DOES have a Scan toggle.** Fourth row from top, between ATB and Camera Movement: `Scan: Once / Always`. I had said "vanilla FF8 PC config menu options known from project knowledge" listed Cursor / ATB / Battle Speed / Battle Message Speed / Sound / Music / Magic Order / Window Color and there's no Scan toggle — that was wrong. The Steam 2013 PC port's Config menu has different options than the original PC release I was remembering. The actual menu has: Controller / Cursor / ATB / **Scan** / Camera Movement / Battle speed / Battle message / Field message / Sound / Vibration function.

Aaron flipped Scan from "Once" to "Always" by pressing Down 3 times from the menu top + Right to toggle, then exited (auto-saved). Verified via second F12 capture: the values are now `Once` greyed / `Always` white — exact inverse of the pre-change capture. Setting persists on save.

With Scan: Always, the engine renders the FULL Scan UI on every cast regardless of how many times that monster_id has been scanned previously. sub_B687C0 fires every time. The v0.14.60 architecture (announce on first sub_B687C0 fire, activate s_scanScreenActiveSlot for number-key routing) works for every scan Aaron casts. The compacted-view skip path simply isn't triggered.

## Decision: NO mod-side compacted-view fallback code

v0.14.74.5 was pre-planned as `ScanTTS::OnScanCast(_, true)` ~800 ms timeout fallback for compacted view. **Cancelled.** Aaron has Scan: Always set in the in-game config; the compacted-view path is dead from his perspective. Writing fallback code we don't need adds maintenance surface for no user benefit.

If a future Aaron save somehow loses the Config setting (savemap corruption, fresh install, etc.), the worst case is he opens Config and re-enables Always. That's a one-time 30-second fix vs. permanent ~150 LOC of mod code with its own edge cases. Easy call.

## Lesson: ALWAYS check the in-game Config menu before writing workaround code

I should have asked Aaron to check the Config menu earlier in the v0.14.74 session, before committing to v0.14.60's sub_B687C0-gated architecture as the only path. The Config menu is the engine's own user-facing knob for this exact behavior; any time the engine has a behavior the mod wants to neutralize, the Config menu is the first place to look. Adding to memory: "Before writing engine-behavior workarounds, check FF8's Config menu for an existing toggle."

## F12 hotkey: keep or remove?

The F12 = on-demand screenshot capture has clear ongoing diagnostic value (any time Aaron sees something visually that I need to verify, F12 + read the PNG works), and it's zero cost when not pressed. But the F12 rule in userMemories says "F12 reserved exclusively for per-session diagnostic builds." Two options:

1. **Keep as permanent feature.** Move F12 = screenshot to the keyboard shortcut map in userMemories. F12 is no longer reserved-for-diag.
2. **Rip out for next non-diag build.** F12 stays free for future per-session diagnostics; if we need on-demand screenshots again, re-add the handler temporarily.

Deferring this decision to Aaron — see NEXT_SESSION_PROMPT.

## v0.14.74.3 BAT result: PRIMARY FIX VALIDATED (with new sub-issue)

Aaron tested twice. Both tests confirmed the cross-battle stale ENEMY NAME fix works as designed:

**Test 1 — World map outside Garden (full pass):** Bite Bug → escape → Fastitocalon. The Fastitocalon was correctly announced (no stale Bite Bug name leaked across the escape transition). Scan-cast on the Fastitocalon worked end-to-end: auto-announce fired with name + description, number-key prompt worked, all detail keys responded.

**Test 2 — Balamb Garden Training Center (mixed result):** Grat → escape → T-Rexaur. The T-Rexaur was correctly announced after the Grat-fight escape — same situation as the v0.14.74.3 root-cause BAT, but now the fingerprint-based defer-and-retry path worked exactly as designed. So the v0.14.74.2/3 chapter on cross-battle stale-data is closed.

BUT: the T-Rexaur Scan exposed a separate, previously-known issue. Scan only spoke the description, then auto-advanced without prompting for number keys and without waiting for confirm. The Bite Bug → Fastitocalon path (test 1) didn't trip this — only test 2's T-Rexaur did.

## Hypothesis: T-Rexaur Scan rendered in FF8's compacted view

FF8's Scan UI has two render paths the engine selects between based on whether the target's monster_id has been scanned in the current save/session:

1. **Full Scan UI** — full window opens, sub_B687C0 fires when the engine fetches text to render the window, the window stays open until the player dismisses it with a button press.
2. **Compacted view** — a brief description popup appears (no full window), engine auto-advances to the next turn after a short delay, sub_B687C0 does NOT fire.

The v0.14.60 architecture (current production) gates the user-facing auto-announce and number-key activation on sub_B687C0's first fire. Compacted view skips sub_B687C0 entirely. So in compacted view: action-layer (sub_48D200 popup hook) silently captures snapshot to s_scanCache[slot], but nothing speaks and number keys don't activate. Aaron hears engine TTS for the brief description blip (via show_dialog hook) and then the turn advances.

v0.14.54 BAT proved both sub_B687C0 and sub_84F860 skip compacted view. v0.14.55 moved to action-layer detection. v0.14.59+ UX redesign re-coupled the user-facing announce to sub_B687C0 because action-layer was firing 9 seconds before the visual window opened. So the compacted view fallback regressed during the v0.14.59+ redesign.

## Why test 1 didn't trip it but test 2 did

World-map encounters at the start of a save typically render full Scan view because monsters there haven't been scanned yet. Balamb Garden Training Center is reachable from a save where Aaron has likely already scanned T-Rexaurs (most-traveled save spot in early game). Compacted view triggers when the monster_id has already been scanned → stored in some session-local cache or savemap field.

Aaron asked whether FF8's Config menu has an option to disable compacted Scan view. He sent a screenshot of the menu but FF8's OpenGL framebuffer can't be captured by standard Windows tools (PrintScreen / GDI / snipping tool all return solid black for FF8 because the framebuffer isn't in the Windows compositor pipeline). The mod's own glReadPixels-via-SwapBuffers-hook capture mechanism IS the only way to capture FF8 frames, but until v0.14.74.4-diag it only fired at hard-coded triggers (kind4 sprite spawns, scan UI fire #1, etc.).

## v0.14.74.4-diag: F12 = on-demand screenshot capture

Added an F12 hotkey in `src/dinput8.cpp`'s AccessibilityThread main hotkey block. On press edge:

1. Builds an absolute path: `<BattleTTS::GetScreenshotDir()>/f12_<HHMMSS>_<MS>` (no extension).
2. Calls `BattleTTS::RequestScreenshotAsync(path)` with default 0 frame delay → sets the GL capture flag, returns immediately.
3. Logs `[F12-SCREENSHOT] Capture requested: '<path>.png'` to ff8_mod.log.
4. Speaks `"Screenshot captured."` via ScreenReader::Speak so Aaron knows the keypress registered.

The SwapBuffers hook installed by `BattleTTS::Initialize` is GLOBAL — it captures every present, not just battle ones. So F12 works in any game state: title, field, menus, Config menu, world map, battle.

### F12 rule compliance

Per userMemories: "F12 reserved exclusively for per-session diagnostic builds. Before hooking ANY new diagnostic to F12, search ALL source files for existing VK_F12 / F12 / 0x7B keycode references and REMOVE old diagnostic code first."

Searched all source files. Existing F12 state before this build:

- `src/scan_tts.cpp` — no F12 handler (already removed in some earlier cleanup; v0.14.66-diag's `PollDiagnosticKey` impl gone).
- `src/scan_tts.h` — no `PollDiagnosticKey` forward decl.
- `src/battle_tts_hp.inl` — no F12 / `PollDiagnosticKey` reference.
- `src/battle_tts.cpp` — HAD an orphaned `void PollDiagnosticKey();` forward decl + 5-line comment block in the ScanTTS namespace forward-decl block. Dead code. **REMOVED in this build.**
- `src/field_nav_handlekeys.inl` — single F12 reference is just a comment ("F12 reserved for future per-session diagnostic builds"). Left as-is.

Only hex literal `0x7B` in scan_tts.cpp was a `DumpRow(slot, base, 0x6C, 0x7B, ...)` offset in the SCAN-STRUCT diagnostic — not an F12 keycode reference. Confirmed via dryRun probe.

The "F12 is handled by FieldNavigation::HandleKeys()" comment in dinput8.cpp was updated to reflect F12's new ownership.

### Files touched

- `src/dinput8.cpp` — added F12 edge handler in AccessibilityThread main hotkey block (~30 lines including the long architectural comment)
- `src/battle_tts.cpp` — removed orphaned `void PollDiagnosticKey();` forward decl + comment block (~6 lines deleted)
- `src/ff8_accessibility.h` — bumped `FF8OPC_VERSION` to `"0.14.74.4-diag"` with comprehensive changelog comment
- `DEVNOTES.md` — this entry
- `NEXT_SESSION_PROMPT.md` — BAT plan

No behavior change for normal Aaron play — just adds the F12 hotkey.

### BAT plan

1. Aaron deploys `deploy.vbs` and launches the game.
2. Navigates to the Config menu (main menu → Config).
3. Presses F12. Hears "Screenshot captured." Confirms `[F12-SCREENSHOT]` line appears in `Logs/ff8_mod.log`.
4. Reports back. Claude reads the captured PNG via filesystem MCP tools to confirm or deny whether the Config menu has a Scan compacted-view toggle.

### Expected outcome

Vanilla FF8 PC Config menu options known from project knowledge: Cursor (Initial/Memory), ATB (Active/Wait), Battle Speed, Battle Message Speed, Sound (Stereo/Mono), Music, Magic Order, Window Color. None of those are a Scan toggle. FFNx doesn't add one either. So I expect the screenshot to confirm there's no toggle.

### After Config menu confirmation: SOLVED BY CONFIG

The Config menu has a `Scan: Once / Always` toggle. Aaron flipped to Always. Compacted view is no longer triggered. v0.14.74.5 compacted-view fallback CANCELLED — see top of DEVNOTES for the full reasoning.

---

**Current build: v0.14.74.3 — cross-battle stale enemy NAME bug fix. AWAITING BAT.**

## v0.14.74.3 closes the second stale-data vector exposed by v0.14.74.2 BAT

v0.14.74.2 BAT result was a partial win and partial discovery. **The primary magicId fix worked exactly as designed** — `[SCAN-TTS] Battle entry: cached current magicId=39 as prev` fired correctly on Battle 2's entry after Aaron escaped a Scan-cast Battle 1, suppressing what would otherwise have been a stale Scan announce. But during the same BAT, Aaron observed a NEW symptom: the T-Rexaur battle that followed his Grat-battle escape was announced as **"Battle! 2 Grats."** while the audible T-Rexaur roar made it obvious the announce was wrong.

### Root cause: enemy slot memory persists across escape transitions

`AnnounceBattleStart()` in `src/battle_tts.cpp` has a readiness gate that's just:

```cpp
uint16_t allyMaxHP = *(uint16_t*)(BATTLE_ENTITY_ARRAY_BASE + BENT_MAX_HP);
if (allyMaxHP == 0) { /* keep waiting */ }
else                { /* declare ready, build name string from live memory */ }
```

When Aaron escaped the Grats and immediately entered the T-Rexaur battle, the engine left battle 1's enemy data sitting in slots 3..6 (`HP=587/MaxHP=587/Lv=21 status=0x00` for both Grats) and didn't repopulate the slots until **~10 seconds later** when it finally wrote T-Rexaur stats. Squall's `allyMaxHP=901` survived the transition, so the readiness gate fired immediately at the 2-second min delay; `CountActiveEnemies()` returned 2 (stale Grat HP > 0); `BuildEnemyNameString` read stale memory and announced "2 Grats". Worse, `s_enemyAnnounceDone = true` was set, **disabling the second-pass safety net** that would have caught the late population.

Log evidence (1677-line ff8_battle.log uploaded by Aaron):

```
[19:47:48] === BATTLE ENTERED === (encounter ID: 59)
[19:47:48] [SCAN-TTS] Battle entry: cached current magicId=39 as prev   ← v0.14.74.2 fix WORKED
[19:47:50] Entity array ready after 2000ms (ally0 maxHP=901)
[19:47:50] slot3 ENEMY HP=587/587 Lv=21 status=0x00                   ← STALE GRAT DATA
[19:47:50] slot4 ENEMY HP=587/587 Lv=21 status=0x00                   ← STALE GRAT DATA
[19:47:50] [NAME-CACHE]   slot3 = "Grat 1" (base="Grat")
[19:47:50] [NAME-CACHE]   slot4 = "Grat 2" (base="Grat")
[19:47:50] Battle! 2 Grats.                                            ← ANNOUNCED WRONG
[19:47:58] s3=4240/12000(hp18883)                                      ← T-Rexaur HP finally appears (+10 s)
```

Same antipattern class as v0.14.74.2: trusting engine memory at battle entry before the engine has refreshed it. The v0.14.74.2 fix addressed the `*battle_magic_id` byte; this fix addresses the enemy slot HP/MaxHP/Lv/status block.

### Fix

**Snapshot enemy slot fingerprint at exit, require it to differ before declaring fresh.**

1. New struct `EnemySlotSnap { hp, maxHp, lvl, status }` and state `s_lastBattleEnemySnap[BATTLE_TOTAL_SLOTS - BATTLE_ALLY_SLOTS] + s_lastBattleEnemySnapValid` declared near the top of `namespace BattleTTS`.

2. New helper `EnemySlotsMatchLastBattleSnap()` returns true iff a snapshot exists AND every enemy slot's live HP+MaxHP+Lv+status matches the snapshot bit-for-bit. SEH-guarded.

3. `OnBattleExit()` captures the snapshot before clearing `s_inBattle`. New `[EXIT-SNAP]` diagnostic log line dumps the captured fingerprint per exit.

4. `AnnounceBattleStart()` — if `enemyCount > 0` AND fingerprint matches snapshot, treat as "enemies not ready": announce "Battle!" generically, **leave `s_enemyAnnounceDone = false`** so the second-pass loop will retry. New `[STALE-ENEMY]` diagnostic log line fires when this path is taken.

5. `Update()` second-pass loop — same protection. While `enemyCount > 0` AND fingerprint matches AND elapsed < `BATTLE_INIT_TIMEOUT_MS`, defer. Beyond timeout, fall through and announce (legitimate same-formation re-encounter case).

6. `BATTLE_INIT_TIMEOUT_MS` bumped from 10000 to 15000 ms because the engine took ~10 s to repopulate T-Rexaur data — right at the old timeout boundary, and we want comfortable slack for the fingerprint-mismatch detection to fire before timeout fallback kicks in.

### Why the design choices

- **Why fingerprint, not just "any slot has HP > 0"?** The bug case has `HP > 0` from stale data. We need to distinguish "engine repopulated" from "engine left old values in place."
- **Why HP+MaxHP+Lv+status and not monsterId?** The four fields are read via existing helpers and cover the most identifying memory. monsterId is at a different offset and would need a separate read; if all four of HP, MaxHP, Lv, status match exactly across a battle transition, the slot has not been touched.
- **Why not raise BATTLE_INIT_MIN_DELAY_MS instead?** Because for the normal case (engine zeroes slots and repopulates within 2 s), waiting longer would slow every battle's first announce. Fingerprint detection is zero-cost in the normal case (fingerprints differ immediately on first poll).
- **Why a 15 s timeout fallback?** Same-formation re-encounter is legitimate — walk into 2 Grats, kill them, walk into 2 Grats again. Without the fallback, that battle would silently never announce. 15 s is long enough for the engine to repopulate after even quick-re-encounter, short enough that the fallback doesn't make legitimate same-formation feel broken.

### Edge cases handled

- **First battle of session:** `s_lastBattleEnemySnapValid = false` → helper returns false → existing behavior preserved.
- **Victory exit:** Slots end with HP=0, status=0x01 (dead). Next battle has live enemies (HP>0) → fingerprints differ → no false delay.
- **Escape exit (the bug case):** Slots retain alive HP. New battle reuses same memory → fingerprints match → defer to second-pass.
- **Same-formation re-encounter:** Fingerprints match for the full 15 s, fallback announces (worst case: slight delay on legit same-formation).
- **SEH faults:** Snapshot capture and comparison both SEH-guarded so a fault doesn't crash; on fault, treat as fresh and let normal flow proceed.

### Downstream effect on other Battle 2 issues

Aaron's BAT also reported "No effect on Grat 1" being announced when Aaron Scanned the T-Rexaur (watchdog timed out at 19:48:18). This is a **downstream symptom** of the same root cause: at the time of the Scan cast, slot 3 had real T-Rexaur memory but the cached enemy name was "Grat 1" from stale memory. With v0.14.74.3, by the time the Scan window opens enemy data is fresh, name resolution is correct, and the watchdog cancel can fire on the right (slot, name) pair. Should self-heal.

### Files

- `src/battle_tts.cpp` — 5 edits totaling ~140 lines, all comment-heavy:
  - State declarations after `s_enemyAnnounceDone` (~50 lines including doc comment)
  - `EnemySlotsMatchLastBattleSnap()` helper after `CountActiveEnemies()` (~30 lines)
  - Snapshot capture in `OnBattleExit()` (~30 lines)
  - Stale-aware first-pass announce in `AnnounceBattleStart()` (~15 lines)
  - Stale-aware second-pass loop in `Update()` (~15 lines)
- `src/ff8_accessibility.h` — version bump.

### Test plan for BAT

Most reliable repro:

1. Walk into a random encounter in an area that has multiple formation types (e.g., near Balamb Garden's trabia field has Grats / Bite Bugs / T-Rexaurs / Caterchipillars).
2. (Optional) Cast Scan on an enemy in battle 1.
3. Escape battle 1.
4. Walk a few more steps until the next encounter.

Expected v0.14.74.3 behavior:

- At ~2 s into battle 2: "Battle!" generic announce (no enemy names).
- Up to ~15 s later: second-pass fires with the actual enemy name. Log: `[second-pass] T-Rexaur. (enemies appeared after initial announce)`.
- If you happen to re-encounter the same formation, expect a 10-15 s wait before the announce — the fingerprint-match fallback kicks in at timeout.

If you Scan the new battle's enemy after the second-pass fires, the Scan announce should now use the correct name (no more "Scanning Grat" for a T-Rexaur) and the watchdog "No effect" should NOT fire.

Diagnostic log lines to verify:

- `[EXIT-SNAP] Captured enemy fingerprint: ...` — should appear at every battle exit, dumping the slots' final state.
- `[STALE-ENEMY] Enemy fingerprint matches last battle exit — deferring enemy announce to second-pass` — should fire on the post-escape Battle 2 entry.
- `[second-pass] T-Rexaur. (enemies appeared after initial announce)` — should appear within ~10 s after the deferred first-pass.

If instead you see `[STALE-ENEMY]` fire and then `No enemies detected after 15000ms timeout`, that's the engine genuinely never refreshing — a different bug we'd need to investigate, but I don't expect that case from the BAT log evidence.

### Previous build:

## v0.14.74.2 closes the cross-battle stale-data leak

Aaron reported (last session, before context limit) that escaping from a battle could cause stale Scan data to appear in the subsequent battle. This session traced the root cause and shipped a two-line fix paired with one defensive line.

### Root cause

`PollBattleMagicId` in `battle_tts_ewm.inl` detects Scan / GF effect dispatches by watching `*battle_magic_id` for transitions:

```cpp
if (magicId != s_prevBattleMagicId) {
    if (IsGFEffectId(magicId)) { ... GfAudioDesc::OnGFAnimationStart(magicId); }
    else if (magicId == 39)    { ... ScanTTS::OnScanCast(slot, true); }
    s_prevBattleMagicId = magicId;
}
```

On battle entry, `OnBattleEnter` reset `s_prevBattleMagicId = -1`. But the engine's `*battle_magic_id` byte at `s_battleMagicIdAddr` (resolved once at hook install via `*(uint32_t*)(0x50AF20 + 0x3E)`) is NOT cleared by the engine on battle escape — the engine just exits to the world map / field with the byte holding whatever the last spell dispatched in the previous battle wrote. Common residual values:

| Previous-battle action | Residual `*battle_magic_id` |
|---|---|
| Player cast Scan | 39 |
| Player summoned Ifrit | 200 |
| Player summoned Shiva | 184 |
| ...etc., any GF | the GF's effect ID |

So the very next call to `PollBattleMagicId` in the new battle saw `prev=-1, cur=39` (or 200, etc.), interpreted it as a fresh transition, and fired the corresponding handler against the new battle's slot 0 entity — or against whatever target bitmask happened to be left over at `0x01D76884`. Most visible failure mode: Aaron casts Scan in Battle 1, escapes, enters Battle 2, hears a stale Scan announce in the first ~16 ms of Battle 2 against whatever entity the previous battle's bitmask resolved to. Same class for GF audio descriptions: summon Ifrit, escape, hear Ifrit's audio description replayed at the next battle's entry.

### Primary fix

`src/battle_tts.cpp::OnBattleEnter` — replaced `s_prevBattleMagicId = -1;` with a SEH-guarded read of `*s_battleMagicIdAddr` to cache the engine's current value at battle entry. With prev cached to the live value, `PollBattleMagicId` sees `prev == cur` and detects no transition. Once the engine eventually clears `*battle_magic_id` to a fresh value (typically 0) during battle setup, that transition is non-actionable (neither a GF effect ID nor 39).

The hook-install fallback path (`s_battleMagicIdAddr == 0`) preserves the v0.14.50..v0.14.74.1 behavior of resetting to -1; this only matters if `EWM_InstallBattleEffectHook` somehow hasn't run by the time of the first `OnBattleEnter`, which the existing install order in `OnBattleEnter` already guarantees.

New log line per battle entry: `[SCAN-TTS] Battle entry: cached current magicId=N as prev (suppresses stale transition fires)`. If N is non-zero on the first encounter after escape, that's exactly the value that would have caused a bogus announce pre-fix.

### Defense-in-depth fix

`src/battle_tts_sprite.inl::ResetSpriteSpawnState` — added `InterlockedExchange(&s_lastScanCastTick, 0)` to clear the popup-hook tick per battle. Without this, escaping within `SCAN_CAST_RECENT_MS` (1 sec) of casting Scan could leak the tick across the battle boundary and trigger a bogus action-layer Scan announce on the very first `sub_48E830` fire of the next battle. `NoEffect_RecordSnapshot`'s `InterlockedExchange` normally consumes the tick within the same frame the popup hook sets it, so the race window is microscopic, but the fix is one line and correctness > bytes. Pairs with the primary fix.

### Files

- `src/battle_tts.cpp` — ~30 lines added at the `s_prevBattleMagicId` reset site (one `__try` block + comment-heavy explanation since the bug is subtle).
- `src/battle_tts_sprite.inl` — 1 line of code + ~10 lines of comment in `ResetSpriteSpawnState`.
- `src/ff8_accessibility.h` — version bump.

### Test plan for BAT

1. Cast Scan on an enemy in any random encounter (e.g., a Bite Bug or Fastitocalon).
2. Escape immediately (no need to wait for victory).
3. Walk a few steps until the next random encounter triggers.
4. Listen for any unsolicited speech in the first ~1 second of Battle 2's entry. Pre-fix: occasional stale Scan/GF announce. Post-fix: silence (battle-start announce only).
5. Repeat with a GF summon in Battle 1 (Ifrit, Shiva, anything junctioned). Pre-fix: GF audio description plays at Battle 2 entry. Post-fix: silence.
6. Verify legitimate Scan in Battle 2 still works — cast Scan on an enemy, hear the auto-announce, press number keys 0-9, all keys still respond as expected.
7. Check `Logs/ff8_battle.log` for `[SCAN-TTS] Battle entry: cached current magicId=N as prev` per battle entry. N should usually be 0 at clean battle starts but may be 39 / 200 / etc. immediately after escaping a Scan / summon.

### Architectural notes from this session

- **`-1` sentinel + first-poll-fires-handlers is a recurring antipattern.** Whenever an edge-detect comparator is reset to a sentinel and the first read after reset can be ANY value (including handler-firing values), the handlers fire on phantom transitions. Same class as v0.13.51 `0x01D768D0` dword-vs-byte where reading a function pointer's low byte coincidentally matched an executing-phase value. Fix pattern: cache the live value at reset time so `prev == cur` yields no transition.
- **Engine memory persistence across battle escape vs victory.** This is the second time this session that engine state carrying across the battle boundary has been a bug source. Worth keeping in mind as a class of bugs going forward: any address that the mod observes via per-frame polling AND that the engine doesn't clear on mode 3 → mode 1/2/etc. transition is a candidate for stale-state bugs. Audit the field of similar pollers (HP / status / draw inventory / etc.) might surface more.
- **Why I had this half-wrong in the prior session.** My last message before context limit said `s_prevBattleMagicId is only ever initialized at hook install time — there's no per-battle reset.` That was wrong: `OnBattleEnter` does reset it (the `s_prevBattleMagicId = -1;` line). The actual bug is that the reset to -1 MAKES the bogus transition look real; the right fix is to reset to the live value, not to keep prev across battles.

## Remaining backlog

1. Persistent accessibility settings
2. GF naming bypass (Siren)
3. Remove party-member NPCs from field entity catalog
4. X-ATMO92 chase
5. ⚠️ v0.14.74.2 stale-data fix — SHIPPED, awaiting BAT
6. v0.14.74.1 [SCAN-STRUCT] diagnostic still pending its BAT against Grat / T-Rexaur / Tonberry. Aaron can BAT both at the same time — v0.14.74.2 doesn't touch the [SCAN-STRUCT] mechanism.
7. v0.14.74.3 (after offset confirmed): update BENT_STATUS_RESIST_BASE in battle_tts.h + re-enable keys 9/0
8. Optional polish after status keys land: 'Halves' verbose mode, status threshold refinement if `byte == 0` proves too strict
9. `kernel.bin` parsing for Blue Magic
10. ✅ GitHub push at v0.14.72 — will lag local by 5 builds (v0.14.73 + v0.14.73.1 + v0.14.74 + v0.14.74.1 + v0.14.74.2) until next push

---

**Previous build: v0.14.74.1 — diagnostic build for the broken keys 9/0 from v0.14.74. Adds [SCAN-STRUCT] log dumping 121 bytes of the entity struct per scan. Reverts keys 9/0 to 'Not implemented yet.' AWAITING BAT.**

## v0.14.74.1 ships the [SCAN-STRUCT] diagnostic

Two changes scoped to `src/scan_tts.cpp`:

1. **NEW `[SCAN-STRUCT]` diagnostic log** in `CaptureSnapshot`. Dumps 121 bytes of the entity struct from `+0x3C` through `+0xB4` every scan, split across 8 log lines (16 bytes per row except the last 9-byte row) for greppability. Range covers the elemental block anchor (`+0x3C..+0x4B`, validates dump alignment against the existing `[SCAN-ELEM]` u16 values), the unknown gap (`+0x4C..+0xB3`, contains the actual 20-byte status block somewhere), and the level anchor (`+0xB4`, validates alignment held all the way down). New helpers `DumpRow` (one row, 16 bytes max) and `LogStructDump` (8 calls under SEH guard).

2. **Keys 9 / 0 reverted to `"Not implemented yet."`** in the `SpeakField` switch so misleading status info doesn't reach Aaron during play. `FormatStatusResistances` and `FormatStatusWeaknesses` are still defined and unchanged — just unwired for now. v0.14.74.2 will rewire them after the offset is found.

Keys 1-8 are completely untouched and stay exactly as v0.14.74 (offensive/defensive stats, elemental resistances + weaknesses).

## How the BAT will pin down the offset

Scan three enemies with deliberately different status profiles in one battle (or three battles — the cache resets on `OnBattleEnter` but the diagnostic fires per scan), then diff the `[SCAN-STRUCT]` lines for each scan in `Logs/ff8_battle.log`. The offset is the position of a 20-byte run that satisfies all three of these patterns simultaneously:

| Enemy | Profile | Expected pattern in the 20-byte block |
|---|---|---|
| Grat | Vulnerable to Sleep / Silence / Berserk (Quistis Training Center tutorial canon) | 0x00 at indices 4 (Silence), 5 (Berserk), 7 (Sleep); high values elsewhere |
| T-Rexaur | Vulnerable to Sleep / Darkness / Death / Poison (FF8 wiki) | 0x00 at indices 0 (Death), 1 (Poison), 3 (Darkness), 7 (Sleep); high values elsewhere |
| Tonberry | No documented status weaknesses | High values across the entire 20-byte block, no zeros |

The offset where the 20-byte run satisfies ALL three patterns is the actual `BENT_STATUS_RESIST_BASE`. v0.14.74.2 will update the constant in `src/battle_tts.h` and re-enable keys 9/0 by removing the v0.14.74.1 stub comments.

## Why diagnostic-driven and not disassembly-driven

Plan B as originally formulated (find `cmp al, 0x64` in sub_84F860 phase handlers paired with `[esi+offset]`) didn't pan out:

1. **No `cmp al, 0x64` in any of the six phase handlers.** Searched `Game Files/disassembly/FF8_EN_.text_0x00801000.asm` lines 107601–108760 covering 0x84F8D0 through 0x850740 — the only constants compared in those handlers are 0x60, 0x9F, 0x800, 0x2000, and various memory addresses.
2. **The scan UI handlers don't read entity offsets in the suspect range at all.** The only register-relative offsets accessed in the phase handlers are `[esi+0x29]` (the phase counter), `[esi+0x50/54/58/60/70]` (a UI-internal scratch struct at global address `0x269aa78`, not the entity struct).
3. **The threshold check lives in the status APPLICATION path, not the scan UI rendering path.** When a status spell is cast, the inflict formula reads the resistance byte and rolls against it; the scan UI just displays pre-formatted strings via `sub_47EC70`. So the 'cmp 0x64' wouldn't appear inside the scan UI code even if FF8 used that exact threshold.

A disassembly hunt could still find it via a much wider search (status application code is in the magic-effect dispatcher, somewhere in the 0x47B000–0x49F000 range), but that's exploratory work without a clear target. The diagnostic-driven path lands the answer in one BAT round.

## Architectural lesson, captured

- **Even when the deep research has been validated for some claims, predictions it explicitly flags as needing runtime confirmation should be wrapped in diagnostic logs from day one.** The deep research called `+0x4C` a 'best-supported hypothesis' and recommended a debugger watchpoint to confirm. v0.14.74 added the `[SCAN-STAT]` log defensively for exactly this reason — same defensive pattern that saved v0.14.73.1's element scale. The combo (read the doc + log alongside the feature) caught the wrong offset in one BAT instead of shipping a confidently-broken status readout for a multi-build cycle. Memory edit #30 ('deep research docs first') still applies; this is a corollary: **predictions, not just published facts, get diagnostics**.
- **Disassembly Plan B's specific formulation was wrong but wasn't a wasted hunt.** We confirmed the scan UI doesn't read the entity struct directly in the suspect range — useful to know for any future investigation that needs to disambiguate scan-UI display logic from underlying engine data. Documented for posterity in this DEVNOTES entry.

## Remaining backlog

1. Persistent accessibility settings
2. GF naming bypass (Siren)
3. Remove party-member NPCs from field entity catalog
4. X-ATMO92 chase
5. ⚠️ v0.14.74.1 [SCAN-STRUCT] diagnostic — SHIPPED, awaiting BAT against Grat / T-Rexaur / Tonberry
6. v0.14.74.2 (after offset confirmed): update BENT_STATUS_RESIST_BASE in battle_tts.h + re-enable keys 9/0
7. Optional polish after status keys land: 'Halves' verbose mode, status threshold refinement if `byte == 0` proves too strict
8. `kernel.bin` parsing for Blue Magic
9. ✅ GitHub push at v0.14.72 — will lag local by 4 builds (v0.14.73 + v0.14.73.1 + v0.14.74 + v0.14.74.1) until next push

---

**Previous build: v0.14.74 — keys 5/6/7/8 PASSED BAT ✅, keys 9/0 FAILED ❌ (offset +0x4C is not status resistances). v0.14.74.1 plan ready, awaiting Aaron's choice of path.**

## v0.14.74 BAT result — partial PASS

### What worked (4/6 new things — keys 5/6/7/8) ✅

- **Key 5 Offensive Stats**: Strength/Magic/Speed/Hit readout. Concise.
- **Key 6 Defensive Stats**: Vitality/Spirit/Evasion/Luck readout. Concise.
- **Key 7 Resistances**: works (Grat had no resistances, announced "No elemental resistances." — correct)
- **Key 8 Weaknesses**: `"Weak against Fire and Ice."` for Grat — matches canonical FF8 Grat data
- `[SCAN-STAT]` diagnostic log line emits as designed
- The split-stats UX is the success Aaron wanted — one announce per role group instead of all 8 at once

### What failed (keys 9/0) ❌

The deep research's predicted offset `+0x4C` for the 20-byte status resistance block is **wrong**. The Grat scan revealed:

```
[SCAN-STAT] slot=3 raw u8=[Death=169 Poison=251 Petrify=169 Darkness=251 
   Silence=169 Berserk=251 Zombie=169 Sleep=251 Haste=169 Slow=251 
   Stop=169 Regen=251 Reflect=169 Doom=251 SlowPetrify=169 Float=251 
   Confuse=169 Drain=251 Expulsion=169 Unknown=251]
```

The alternating `169 / 251 / 169 / 251` pattern is too structural to be real game data, and it's inconsistent with canon: Grat's Sleep byte = 251 would mean fully immune, but Grat is the in-game source of Sleep magic and the Quistis Training Center tutorial puts Grats to sleep. The data at `+0x4C` is something else entirely — possibly sprite display flags, animation timer/frame counters, position data, or some other 16-byte-stride engine state.

Aaron's actual BAT announcements:
- Key 9: `"Resists Death, Poison, Petrify, Darkness, Silence, Berserk, Zombie, Sleep, Slow, Stop, Doom, Slow Petrify, and Confuse."` (every byte was ≥ 100, so every ailment triggered as resistance)
- Key 0: `"No status weaknesses."` (no byte was 0, so no weakness triggered)

The deep research itself flagged this as a runtime-validation requirement — it called `+0x4C` a "Most likely exact runtime offset" and explicitly recommended a debugger watchpoint to confirm. Our `[SCAN-STAT]` diagnostic just gave us that validation; the offset is wrong.

The actual 20-byte status block must live somewhere between `+0x4C` and `+0xB4` in the entity struct (since elements end at `+0x4B` and stats begin at `+0xB5`). PC port presumably inserted padding or other fields between elemental and status blocks, breaking the .dat-file relative-spacing assumption the deep research used.

## v0.14.74.1 plan (ready to ship next session, awaiting path choice)

All changes scoped to `src/scan_tts.cpp`:

1. **Revert keys 9/0 to `"Not implemented yet."`** so misleading status info doesn't ship to Aaron during play. Keys 1-8 stay exactly as v0.14.74.
2. **Keep `[SCAN-STAT]` log** for the `+0x4C` window (proven that offset is wrong, but the log mechanism is sound).
3. **TBD per Aaron's choice** — add a wider `[SCAN-STRUCT]` diagnostic dumping `entity+0x40..entity+0xB4` (116 bytes) for cross-enemy comparison, OR dig into the disassembly to find the actual offset directly.

### Path choice for next session

**Path A: Diagnostic-driven (faster).** Add `[SCAN-STRUCT]` log dumping 116 bytes per scan. Aaron BATs Grat (vulnerable to Sleep/Silence/Berserk), T-Rexaur (vulnerable to Sleep/Darkness/Death), and Tonberry (no documented status weaknesses). We diff the dumps to find where Grat and T-Rexaur have `0x00` bytes for their known weaknesses but Tonberry has high values — that reveals the actual block offset. One BAT round to confirm.

**Path B: Disassembly-driven (more authoritative).** Examine sub_84F8D0 (the phase-1 handler dispatched from sub_84F860) and its siblings (0x84FD70, 0x850650, 0x850690, 0x8506B0, 0x850740) to find a `cmp al, 0x64` pattern combined with a byte read from the entity struct. That pinpoints the offset directly, no BAT needed but takes more analysis time. sub_84F860 is at `Game Files/disassembly/FF8_EN_.text_0x00801000.asm`.

## Architectural notes from this session

- **The `[SCAN-STAT]` diagnostic log was the right call.** Same defensive pattern that saved us on v0.14.73.1's element scale — added the diagnostic alongside the feature, didn't have to ship a separate diagnostic build to investigate. Memory edit #30 ("deep research docs first") still applies, but a corollary: deep research predictions still need runtime validation when the doc itself flags uncertainty.
- **The deep research was right about everything ELSE.** The 20-byte order (Death/Poison/Petrify/.../Unknown) is the FF8 canonical order. The `byte == 0` weakness threshold and `byte >= 100` resistance threshold from the StatusDefense formula are likely correct — we just need to find where those 20 bytes actually live in the runtime entity struct.
- **Layout overhaul is solid.** Aaron's UX feedback drove the right changes: splitting stats by combat role, consolidating element resistances into one announce, removing the redundant active-statuses readout (already covered by target-cursor flow). Keep this as the v0.14.74.1 baseline; only the status data lookup is broken.

## Remaining backlog

1. Persistent accessibility settings
2. GF naming bypass (Siren)
3. Remove party-member NPCs from field entity catalog
4. X-ATMO92 chase
5. ⚠️ v0.14.74.1: revert keys 9/0 to "Not implemented yet." + diagnostic OR disasm-driven offset hunt (NEXT SESSION)
6. v0.14.74.2 (after offset confirmed): re-enable keys 9/0 with correct offset
7. Optional polish after status keys land: "Halves" verbose mode, status threshold refinement if `byte == 0` proves too strict
8. `kernel.bin` parsing for Blue Magic
9. ✅ GitHub push at v0.14.72 — will lag local by 3 builds (v0.14.73 + v0.14.73.1 + v0.14.74) until next push; v0.14.74.1 will add a 4th

---

**Previous build: v0.14.74 — Scan UI keyboard layout overhaul. AWAITING BAT.**

## v0.14.74 ships the keyboard redesign

Aaron requested a layout overhaul to break up the overwhelming 8-stat readout, consolidate the three element-resistance keys into one combined readout, and add Status Resistances + Status Weaknesses. New layout matches the in-game Scan UI's organization more naturally:

| Key | v0.14.73.1 | v0.14.74 |
|---|---|---|
| 1 | Name | Name (unchanged) |
| 2 | Description | Description (unchanged) |
| 3 | Level & Type | Level & Type (unchanged) |
| 4 | HP | HP (unchanged) |
| 5 | All 8 stats | **Offensive stats** (STR/MAG/SPD/HIT) |
| 6 | Weaknesses | **Defensive stats** (VIT/SPR/EVA/LCK) |
| 7 | Absorbs | **Resistances** (Halves/Strong/Nullify/Absorb combined) |
| 8 | Nullifies | **Weaknesses** (moved from key 6) |
| 9 | Status Res ("Not implemented yet.") | **Status Resistances** (offensive ailments only) |
| 0 | Active Statuses ("Not implemented yet.") | **Status Weaknesses** (offensive ailments only) |

Luck stays defensive (4-and-4 balance, applies to both sides). The active-statuses readout that used to live on key 0 is removed because the same info is announced via the target-cursor flow in `BuildStatusString` already — so 10 useful keys instead of 9 useful + 1 redundant.

## Element scale (preserved from v0.14.73.1)

| u16 range | Bucket | Key |
|---|---|---|
| `< 800` | Weak | 8 |
| `== 800` | Normal | (silent) |
| `> 800 && < 900` | Halves | 7 |
| `== 900` | Nullifies | 7 |
| `> 900 && < 1000` | Strongly resists | 7 |
| `>= 1000` | Absorbs | 7 |

The 901..999 "Strongly resists" range — left ambiguous and silent in v0.14.73.1 — is now folded into key 7 alongside the other resistance categories per Aaron's decision ("all of these represent varying levels of resistance").

Key 7 announces categories as separate sentences: "Halves Fire and Earth. Strongly resists Ice. Nullifies Thunder. Absorbs Water and Holy." Empty case: "No elemental resistances."

## Status scale (new in v0.14.74)

Reads 20 u8 bytes from `entity+0x4C` (BENT_STATUS_RESIST_BASE in `battle_tts.h`). Per the deep research's Question 1 + the FF Wiki StatusDefense formula:

| byte value | Bucket | Key |
|---|---|---|
| `== 0` | Weak to (fully vulnerable) | 0 |
| `1..99` | Partial resistance (silent) | — |
| `>= 100` | Strongly resists / immune | 9 |

**13-ailment filter** applied to BOTH keys 9 and 0:
- **Include:** Death, Poison, Petrify, Darkness, Silence, Berserk, Zombie, Sleep, Slow, Stop, Doom, Slow Petrify, Confuse
- **Exclude:** Haste, Regen, Reflect, Float (buffs), Drain, Expulsion, ??? (non-actionable)

Knowing an enemy "resists Haste" or "is weak to Reflect" isn't actionable for the player.

## Calibration anchor: T-Rexaur

T-Rexaur is famously vulnerable to Sleep, Darkness, Death, and Poison (canonical FF8 strategy: junction 100 Sleeps to ST-Atk-J in Training Center). Expected v0.14.74 BAT announcement: "Weak to Death, Poison, Darkness, and Sleep." if the standard FF8 data has those bytes at 0.

If T-Rexaur's Sleep byte is non-zero but small (50, 30, etc.), the `[SCAN-STAT]` log reveals it and we adjust the threshold to `byte < N`. The diagnostic is the same defensive pattern that saved us on element affinity — we get the right answer even if the threshold guess is wrong.

## What v0.14.74 changes (file-by-file)

**`src/battle_tts.h`**: New `BENT_STATUS_RESIST_BASE = 0x4C` constant with full encoding documentation in the comment.

**`src/scan_tts.cpp`** (~280 lines):
- New `ReadSlotStatusRes()` helper (SEH-guarded memcpy of 20 bytes from `entity_base + BENT_STATUS_RESIST_BASE`)
- `CaptureSnapshot` now calls `ReadSlotStatusRes` after `ReadSlotElements`
- New `[SCAN-STAT]` log line dumping all 20 raw status bytes per scan in deep-research order
- `FormatStats` replaced with `FormatOffensiveStats` (STR/MAG/SPD/HIT) and `FormatDefensiveStats` (VIT/SPR/EVA/LCK), each with its own "unavailable" fallback message
- `JoinElementList` refactored to `JoinNameList` parameterized over a name table (now used by both element and status formatters)
- `FormatAbsorb` + `FormatNullify` replaced by unified `FormatResistances` (4-bucket, sentence-separated)
- New `FormatStatusResistances` (key 9) and `FormatStatusWeaknesses` (key 0)
- New data: `STATUS_NAMES_20`, `OFFENSIVE_AILMENT_INDICES`, `OFFENSIVE_AILMENT_COUNT`
- `SpeakField` switch updated for new bindings (cases 5..0 all rewired)
- `BuildAutoAnnounce` comment block updated (spoken phrase unchanged)
- Element formatters comment block rewritten as v0.14.74 architecture doc

**`src/ff8_accessibility.h`**: `FF8OPC_VERSION` bumped to `0.14.74` with full changelog.

## What's preserved (working correctly)

- `[SCAN-ELEM]` log line (still emits; no change)
- ReadSlotElements, ReadSlotStats, ReadSlotName, ReadSlotHP, ReadSlotLevel, ReadSlotMonsterId (unchanged)
- BuildAutoAnnounce phrasing ("Press numbers 0 through 9 for details.")
- FormatLevel (key 3 type-label append from v0.14.69)
- FormatHP, all hidden-HP logic
- Type-label capture pipeline (HandleBattleText, sub_47EC70 forward from victory hook)
- Auto-announce on Scan UI open
- Cache lifecycle (OnBattleEnter clears, OnScanCast captures, OnScanPopupSpawn announces, OnScanPopupDespawn deactivates)
- Sub_B687C0 fire #1 trigger + 90-frame screenshot capture

## What to look for in BAT

1. **Fastitocalon** (already validated raw bytes) should announce on key 7: "Halves Fire. Absorbs Water."; on key 8: "Weak against Thunder and Earth."; on keys 9/0: depends on its actual statusRes bytes (Fastitocalon isn't documented as having specific status weaknesses, so possibly "No status weaknesses." / some immunities on key 9).
2. **T-Rexaur** is the calibration anchor for status weakness threshold. Expected key 0: "Weak to Death, Poison, Darkness, and Sleep." — if all four announce, the `byte == 0` threshold is right. If only some announce or if extras appear, the `[SCAN-STAT]` log reveals the actual byte values for refinement.
3. **Tonberry** (no documented status weaknesses) should announce on key 0: "No status weaknesses." — good null test.
4. **Stats split** — key 5 should now be ~half the length of v0.14.73.1's all-stats announce; key 6 the other half.
5. **No regressions** on keys 1-4 — Name, Description, Level (with type), HP all unchanged.
6. **`[SCAN-STAT]` log line** emits per scan with all 20 raw bytes for cross-validation.

## Architectural notes

- **Memory edit #30** ("deep research docs first") consulted for both the element scale (Question 2) and status resistance threshold (Question 1) before coding. The deep research doc had already documented `entity+0x4C`, the 20-byte order, and the `>= 100` threshold; v0.14.74 builds on those findings rather than guessing.
- **JoinNameList parameterization** lets the same Oxford-comma list builder serve both element and status formatters — saves ~30 lines of duplicate code.
- **`activeStatus` field** in ScanSnapshot remains declared but unused (was for the old key 0 active-statuses readout). Leaving it for now to avoid struct-layout ripple effects; can be removed in a future cleanup.

## Remaining backlog

1. Persistent accessibility settings
2. GF naming bypass (Siren)
3. Remove party-member NPCs from field entity catalog
4. X-ATMO92 chase
5. ⚠️ v0.14.74 keyboard layout overhaul — SHIPPED, awaiting BAT
6. Optional polish after BAT: status threshold refinement (if `byte == 0` is too strict), "Halves" verbose mode
7. `kernel.bin` parsing for Blue Magic
8. ✅ GitHub push at v0.14.72 — will lag local by 3 builds (v0.14.73 + v0.14.73.1 + v0.14.74) until next push

---

**Previous build: v0.14.73.1 — elemental affinity bucket scale corrected. AWAITING BAT.**

## v0.14.73 BAT FAILED ❌, v0.14.73.1 fixes the scale

v0.14.73 BAT against Fastitocalon (slot 3) announced "Weak against Fire, Ice, Thunder, Earth, Poison, Wind, Water, and Holy." — every element triggered the weak bucket. The `[SCAN-ELEM]` diagnostic log line revealed why:

```
[SCAN-ELEM] slot=3 raw u16=[Fi=820 Ic=800 Th=700 Ea=650 Po=800 Wi=800 Wa=1000 Ho=800]
              as i16=[Fi=820 Ic=800 Th=700 Ea=650 Po=800 Wi=800 Wa=1000 Ho=800]
```

Under v0.14.73's interpretation (`v > 100` = weak), every value was > 100 so every element triggered. v0.14.73's interpretation was a **guess** — a 100-anchored signed scale (`< 0` absorb, `== 0` nullify, `> 100` weak) based on FF7-style affinity conventions.

**The correct format was already documented** in `Plan & Research Documents/Scan spell deep research results.md` (2026-04-29). I should have read that BEFORE shipping v0.14.73, but didn't — the lesson is now recorded in the comment block above the formatters: when a Plan & Research Documents/ entry exists for a feature, consult it first.

## Correct FF8 elemental defense scale (8 × u16 at +0x3C, anchored at 800)

The PC engine pre-computes the .dat-file signed-byte affinity into a u16 anchored at 800:
- `.dat 0`     → `800` (normal)
- `.dat +100`  → `900` (nullify)
- `.dat +200`  → `1000` (absorb)
- `.dat -100`  → `700` (weak)
- `.dat -200`  → `600` (very weak)

The Scan UI renders five buckets:

| u16 range | Bucket | v0.14.73.1 announce key |
|---|---|---|
| `< 800` | Weak | Key 6 (FormatWeak) |
| `== 800` | Normal | (silent) |
| `> 800 && < 900` | Halves | (silent — no key assigned) |
| `== 900` | Nullifies | Key 8 (FormatNullify) |
| `> 900 && < 1000` | Strong resist | (silent) |
| `>= 1000` | Absorbs | Key 7 (FormatAbsorb) |

Fastitocalon's bytes decoded under this scale:
- Fire (820) → Halves (silent)
- Ice (800) → Normal (silent)
- Thunder (700) → **Weak**
- Earth (650) → **Weak**
- Poison (800) → Normal (silent)
- Wind (800) → Normal (silent)
- Water (1000) → **Absorbs** (water creature — makes perfect sense)
- Holy (800) → Normal (silent)

Expected v0.14.73.1 announcements for Fastitocalon: "Weak against Thunder and Earth." / "Absorbs Water." / "Nullifies no elements." — matching the canonical FF8 Fastitocalon profile.

## What v0.14.73.1 changes

Only the three formatters in `src/scan_tts.cpp`:

- `FormatWeak`: `uint16_t v = snap.elem[i]; if (v < 800)` (was `int16_t v; if (v > 100)`)
- `FormatAbsorb`: `if (v >= 1000)` (was `if (v < 0)`)
- `FormatNullify`: `if (v == 900)` (was `if (v == 0)`)

All three read u16 directly without the i16 cast — the actual values are always positive in the typical game range.

Comment block above the formatters rewritten to document the correct scale, the v0.14.73 mistake, and the deep-research-first lesson.

## What's preserved from v0.14.73 (working correctly)

- `ReadSlotElements()` helper — read 16 bytes from `entity_base + 0x3C` correctly.
- `[SCAN-ELEM]` diagnostic log line — worked exactly as designed; revealed the real format on first BAT.
- `JoinElementList` — Oxford comma list builder works correctly.
- `BuildAutoAnnounce` phrasing change ("Press numbers 0 through 9 for details.") — still in place, working.
- `SpeakField` switch routing for keys 6/7/8.
- `ELEMENT_NAMES[8]` array.

## What's intentionally silent (for now)

- **Halves** (800 < v < 900): no key assigned. Could add `FormatHalves` on a future key, OR merge into FormatWeak's announce as "Halves Fire" alongside the "Weak against" list.
- **Strong resist** (900 < v < 1000): the deep research left this 901..999 range slightly ambiguous. Treated as silent rather than mis-bucket.

If Aaron wants Halves announced, that's a polish pass after v0.14.73.1's BAT confirms the corrected scale.

## Architectural lesson recorded

**Always consult `Plan & Research Documents/` BEFORE picking an interpretation when one exists.** v0.14.73 shipped a guess when a deep research document with the correct answer was sitting in the project. The diagnostic log saved us — we got the right answer in one BAT instead of multiple — but we shouldn't have needed it. The fix: search Plan & Research Documents/ at the start of any feature work that touches a documented engine field.

## Remaining backlog

1. Persistent accessibility settings
2. GF naming bypass (Siren)
3. Remove party-member NPCs from field entity catalog
4. X-ATMO92 chase
5. ⚠️ v0.14.73.1 elemental affinity (corrected scale) — SHIPPED, awaiting BAT
6. v0.14.74 status resistances (key 9) — the SAME deep research doc above also documents this at `entity+0x4C`, 20 u8 bytes with threshold `< 100` for "Strong vs". Read THAT doc carefully before shipping v0.14.74.
7. v0.14.75 active statuses (key 0)
8. `kernel.bin` parsing for Blue Magic
9. ✅ GitHub push — DONE 2026-05-02 19:31 UTC at v0.14.72 (commit 337cf97a). Local will lag GitHub by 2 builds (v0.14.73 + v0.14.73.1) until the next push.

---

**Previous build: v0.14.73 — element affinity (keys 6/7/8) + auto-announce phrasing fix. BAT FAILED ❌ (wrong scale; corrected in v0.14.73.1).**

## What v0.14.73 ships

Three changes, all in `src/scan_tts.cpp`:

### 1. Element affinity feature (keys 6 / 7 / 8)

`ScanSnapshot::elem[8]` has been a declared-but-unpopulated field since v0.14.59. v0.14.73 finally wires it up. New `ReadSlotElements()` helper memcpys the 16-byte block at `entity_base + 0x3C` (BENT_ELEM_RESIST_BASE) into `snap.elem[8]`. SEH-guarded, mirrors the existing `ReadSlotStats()` pattern. CaptureSnapshot calls it alongside ReadSlotStats so every slot's affinity is cached at scan time.

Three new formatters — `FormatWeak`, `FormatAbsorb`, `FormatNullify` — each iterate `snap.elem[8]` cast as `int16_t` and pick out values per the FF8 community-standard scale:
- `v < 0` → absorb (heals when hit by this element)
- `v == 0` → immune / nullify
- `0 < v < 100` → resist (partial reduction — NOT announced; can be added later if Aaron wants)
- `v == 100` → normal damage (silent default)
- `v > 100` → weak

`JoinElementList` helper builds natural-language lists with Oxford commas: "Fire", "Fire and Ice", "Fire, Ice, and Thunder". Empty results say "No elemental weaknesses." / "Absorbs no elements." / "Nullifies no elements." so the response is never silent.

`SpeakField` switch cases 6/7/8 now call the new formatters (were 'Not implemented yet.'). Cases 9/0 still say 'Not implemented yet.' pending v0.14.74 (status resistances at +0x4C, 20 bytes) and v0.14.75 (active statuses at +0x78). Key routing in `PollHPCheckKeys` was already in place from v0.14.59 — no `battle_tts_hp.inl` change needed.

### 2. Diagnostic [SCAN-ELEM] log line

CaptureSnapshot now emits a `[SCAN-ELEM]` log line per scan dumping both raw u16 and i16-cast values for all 8 elements (Fire/Ice/Thunder/Earth/Poison/Wind/Water/Holy). If FormatWeak/Absorb/Nullify announce surprising results in BAT, the raw bytes reveal whether the i16 interpretation is wrong without another diagnostic round.

### 3. Auto-announce phrasing

`BuildAutoAnnounce` now says "Press numbers 0 through 9 for details." instead of "Press number keys 1 through 0 for details.". Same key bindings (1=Name, 2=Description, 3=Level, 4=HP, 5=Stats, 6=Weak, 7=Absorb, 8=Nullify, 9=StatusRes, 0=ActiveStatus). The phrase "1 through 0" is technically correct on a physical number row but reads awkwardly through SAPI; "0 through 9" is the natural ascending range.

## Expected v0.14.73 BAT outcomes

1. **Auto-announce phrasing fixed.** New scans should announce "Press numbers 0 through 9 for details." instead of the old phrase.
2. **Keys 6/7/8 produce real announcements.** Test enemies with known affinities:
   - Bite Bug (Fly Monster) → should announce "Weak against Wind." on key 6 (canonical Fly-type weakness)
   - Glacial Eye → should announce "Absorbs Ice." on key 7, "Weak against Fire." on key 6
   - Caterchipillar → "Weak against Fire." on key 6
   - Many enemies will have no entries (e.g. "No elemental weaknesses.") — that's normal for non-elemental monsters.
3. **`[SCAN-ELEM]` log line present** in `Logs/ff8_battle.log` for every scan, showing raw bytes for cross-validation.
4. **Cases 9/0 still say "Not implemented yet."** — those are v0.14.74 and v0.14.75 territory.

## If the BAT shows wrong announcements

The interpretation may need adjustment. The `[SCAN-ELEM]` log line will reveal the actual byte format — e.g., if all values are u8 packed into u16 slots, or if the scale differs from i16 percentage. Adjust `FormatWeak/Absorb/Nullify` interpretation in v0.14.73.1 based on observation.

## Next chapters

- v0.14.74: status resistances (key 9) — read 20 u8 at `entity+0x4C`
- v0.14.75: active statuses (key 0) — composite of TIMED_STATUS_0..3 + PERSIST_STATUS bitfields

## Remaining backlog

1. Persistent accessibility settings
2. GF naming bypass (Siren)
3. Remove party-member NPCs from field entity catalog
4. X-ATMO92 chase
5. ✅ v0.14.73 elemental affinity (keys 6/7/8) — SHIPPED, awaiting BAT
6. v0.14.74 status resistances (key 9)
7. v0.14.75 active statuses (key 0)
8. `kernel.bin` parsing for Blue Magic
9. ✅ GitHub push — DONE 2026-05-02 19:31 UTC. main HEAD is v0.14.72 (commit 337cf97a). Will lag local by 1 build until v0.14.73 BAT lands and a follow-up push.

---

**Previous build: v0.14.72 — sub_47EC70 hook conflict resolved. BAT PASSED ✅**

**v0.14.72 BAT log evidence (from 12:55:15 module init):**

```
[12:56:10] [BT-HOOK] sub_47EC70 @ 0x0047EC70: 8B 44 24 04 66 8B 04 45
[12:56:10] [BT-HOOK] sub_47EC70 hooked OK
...
[12:56:11] [BT-HOOK] 8/8 hooks installed
```

First 8 bytes of `sub_47EC70` now read the original function prologue (`mov eax, [esp+4]`) instead of `E9 ...` (JMP rel32). scan_tts.cpp is no longer claiming the address; victory's `InstallBattleTextHooks()` runs against a virgin function and succeeds. The previously-failing hook on `BT_ADDR1` (which was 7/8) is now 8/8. Aaron confirmed Scan TTS still works perfectly (Fly-types capture 'Fly Monster', type-less monsters fall back to plain Level), AND post-battle victory TTS phase announcements are functioning again for the first time since v0.14.68-diag. Aaron's previously-silent post-Scan victory screens now announce EXP/Items/GF/Ability as designed.

**The architectural lesson is now empirically confirmed.** Two MinHook installers on the same address silently fail — only the failure log line distinguishes the loser. Cross-module hooks must use a single canonical installer per address with forward-declared handler functions for cooperating modules. v0.14.72's `HandleBattleText` forward-call pattern is the template: any future module wanting to observe `sub_47EC70` / `sub_4B7210` / `sub_4A3EE0` / `sub_5348E0` / `sub_47EA30` / `sub_47EA90` / `sub_47E970` / `sub_47E710` (the 8 victory-text addresses) should add a public function called from the existing victory hook, NOT install its own MinHook.

---

**Why v0.14.72 existed.** v0.14.71 BAT log exposed an init-time hook conflict that had been silently breaking victory TTS phase detection since v0.14.68-diag. Two modules had been racing to install MinHook on `sub_47EC70`:

- `battle_tts_victory.inl::InstallBattleTextHooks()` — installs `HookedBtCandidate1` for victory phase routing (text_id 22/23/21/28/109/121/127). Has owned this address since v0.13.14.
- `scan_tts.cpp::InstallGetBattleTextHook()` — added in v0.14.68-diag for the type-label diagnostic. Promoted to v0.14.69 production.

Whichever installer ran SECOND silently failed with `MH_ERROR_ALREADY_CREATED` (status 3). v0.14.71's log:

```
[BT-HOOK] sub_47EC70 @ 0x0047EC70: E9 EB F4 A0 71 8B 04 45
[BT-HOOK] sub_47EC70 MH_CreateHook FAILED: 3
```

The `E9` opcode (JMP rel32) confirmed scan_tts.cpp's hook was already installed when victory's installer ran. **The victory hook lost.** Aaron confirmed in this session that he had noticed victory TTS not announcing post-Scan but hadn't reported it because we were focused on Scan itself.

**Resolution: Option C — single canonical hook, scan_tts.cpp becomes a passive observer.**

The victory module retains ownership of `sub_47EC70`. scan_tts.cpp's hook installer is removed. The victory hook gains a one-line forward call into a new public `ScanTTS::HandleBattleText(int textId, const char* result)`, which runs the same type-label capture logic v0.14.71 had — just called from a different entry point.

**Files touched (v0.14.72):**

- `src/scan_tts.h` — added `void HandleBattleText(int textId, const char* result)` to the public API in `namespace ScanTTS`.
- `src/scan_tts.cpp`:
  - Removed `BATTLE_GET_TEXT_ADDR` constant, `GetBattleTextFn` typedef, `s_originalGetBattleText` global, `s_getBattleTextHookInstalled` global.
  - Removed `InstallGetBattleTextHook()` function (~12 lines).
  - Converted `static const char* __cdecl HookedGetBattleText(int textId)` into `void HandleBattleText(int textId, const char* result)` — same body, but no longer calls the engine function (the caller did that already) and no longer returns the result.
  - Updated `Initialize()`: removed `InstallGetBattleTextHook()` call, replaced with explanatory comment, refreshed log message to `(v0.14.72: sub_47EC70 forwarded from victory hook, sub_B687C0 owned here)`.
  - Moved `TYPE_LABEL_MONSTER_TEXT_ID` constant up into the architecture comment block.
  - Doc comment above `HandleBattleText` rewritten to explain the v0.14.72 change.
- `src/battle_tts.cpp` — added `void HandleBattleText(int textId, const char* result);` forward decl to the existing `namespace ScanTTS` forward-decl block (matches the v0.14.66-diag pattern for `PollDiagnosticKey`).
- `src/battle_tts_victory.inl::HookedBtCandidate1` — added one line `::ScanTTS::HandleBattleText((int)a1, (const char*)result);` immediately after `s_origBt1` returns, BEFORE the existing `if (mode == 4 || mode == 5 || mode == 100)` block. The forward call must run regardless of game mode because Scan happens during normal battle (mode 3).
- `src/ff8_accessibility.h` — version bump.

**Behavior preserved.** v0.14.71's SCAN-TTS production behavior is unchanged. `GetScanFlightSlot()` wider gate still active. Per-scan reset in `OnScanCast` still defensive. Capture logic byte-for-byte identical — only the entry point changed.

**Performance.** The victory hook fires on every `sub_47EC70` invocation (~hundreds of calls per battle frame). `HandleBattleText` early-returns when no scan is in flight via `GetScanFlightSlot()` — two cheap atomic reads. Hot-path overhead is negligible.

**Next chapter: v0.14.73 elemental affinity (keys 6 / 7 / 8 for Weak / Absorb / Nullify).**

From v0.14.70-diag's BATTLE-TEXT-LITE captures during Fly-type scans we have direct evidence:
- text_id=38 returns `Weak against` header
- text_id=40 returns `has no effect` header (Glacial Eye scan, decoded as "has no effect")
- text_id=101 / 102 / 106 return 3-byte sequences `<\x05>Y` / `<\x05>Z` / `<\x05>b` — control-code-prefixed glyph indices for element symbols (Y/Z/b are FF8-encoded)

Approach for v0.14.73: read elemental affinity directly from the entity struct (`entity+0x3C`, 8 × u16 covering Fire, Ice, Thunder, Earth, Poison, Wind, Water, Holy per the existing `ScanSnapshot::elem[8]` field). Map u16 affinity values to weak/absorb/null per the FF8 affinity scale. Speak as e.g. "Weak against Fire and Ice. Absorbs Water. Nullifies Wind.".

No new hook needed — entity-struct reads already work. Pure data-formatting work.

**Remaining backlog (carried).**
1. Persistent accessibility settings
2. GF naming bypass (Siren)
3. Remove party-member NPCs from field entity catalog
4. X-ATMO92 chase
5. v0.14.73 elemental affinity (keys 6/7/8) — next up
6. v0.14.74 status resistances (key 9)
7. v0.14.75 active statuses (key 0)
8. `kernel.bin` parsing for Blue Magic
9. ✅ **GitHub push** — DONE 2026-05-02 19:31 UTC. main HEAD is now v0.14.72 (commit 337cf97a), in sync with local. Six versions consolidated into one cumulative commit. (Earlier notes that called this an "~85-build backlog" were wrong; actual gap was 6 versions: v0.14.66-diag, v0.14.68-diag, v0.14.69, v0.14.70-diag, v0.14.71, v0.14.72.)

---

**Previous build: v0.14.71 — PRODUCTION promotion of v0.14.70-diag. Type-label capture chapter CLOSED. AWAITING BAT.**

**v0.14.70-diag BAT result: MYSTERY SOLVED.** The Fastitocalon scan's auto-capture screenshot at `Logs/screenshots/scan_111219_884_slot3_Fastitocalon.png` is the smoking gun: **Fastitocalon's on-screen Scan UI has no type label**. The bottom of the screen shows just "LEVEL 6 HP ?????/?????" stretched across, with no "Fish Monster" rendered anywhere. The diagnostic confirmed the engine never calls `sub_47EC70(99)` followed by `sub_47EC70(36)` for Fastitocalon — the call sequence stops at text_id=33 (LEVEL) and never reaches the type-label fetch pair.

**Conclusion.** Not every FF8 enemy has a type label. Fly-types (Bite Bug, Glacial Eye, Buel) display "Fly Monster". Fastitocalon — and likely many other enemies — have no type classification in the engine's data. The mod's "Level 6." readout for type-less monsters is the correct mirror of what's on screen. Adding a synthetic "Fish Monster" announcement would invent information not present in-game.

**v0.14.70-diag call-sequence comparison (definitive):**

| Stage | Bite Bug (Fly-type, captures) | Fastitocalon (no type, silent) |
|-------|-------------------------------|--------------------------------|
| Pre-fire | text_id=12 "All enemies" | (none) |
| Header | 113, 11×8, 34, 11×3 | 113, 11×2, 34, 11×3 |
| Level | 33 "LEVEL" | 33 "LEVEL" |
| **Type** | **99 "Fly", 36 "Monster"** → captured | **— no calls —** |
| Element | 38, 102, 106 | (none) |

The engine genuinely takes a different render path for type-less monsters. Confirmed visually.

**v0.14.71 changes from v0.14.70-diag.**

Stripped (~30 lines removed):
- The `[BATTLE-TEXT-LITE]` per-call log block (16-byte hex+ASCII dump per `sub_47EC70` call during scan flight). Useful only for the diagnostic; for type-less monsters there's nothing meaningful to dump per call.
- The four explicit "why didn't we capture" failure-reason log lines (slot OOB, prefixLen=0, composed empty, already populated). For type-less monsters these would have been false-alarm diagnostics — the absence of `text_id=36` calls during scan flight is the correct null result, not a failure.

Kept:
- `GetScanFlightSlot()` helper and the wider gate (covers both action-layer and visible-UI phases). Strict superset of v0.14.69's `IsScreenActive()`-only gate — minor robustness improvement, no harm. Defensive against any future case where the engine might fetch the type label during cast animation.
- Per-scan reset of `s_lastTypePrefixBytes`/`Len` in `OnScanCast`. Defensive, guarantees clean buffer per scan.
- Core capture logic. Single-write per scan event, SEH-guarded, FF8TextDecode-based.
- The `[SCAN-TTS] Type label captured slot=N typeLabel='...'` success log line stays as production confirmation.

**Files touched (v0.14.71):**
- `src/scan_tts.cpp` — `HookedGetBattleText` body collapsed from ~80 lines back to ~25 lines; doc comment rewritten to reflect the v0.14.71 understanding (Fly-types capture, type-less monsters correctly silent). Hook install log message updated to say `[v0.14.71: type-label capture]`. ~30 net lines removed.
- `src/ff8_accessibility.h` — version bump.

**Expected v0.14.71 BAT outcome.**

- Fly-type scans (Bite Bug, Glacial Eye, Buel): `[SCAN-TTS] Type label captured slot=N typeLabel='Fly Monster'` fires once per scan; key 3 says "Level N, Fly Monster.".
- Type-less scans (Fastitocalon, T-Rexaur, Iguion, etc.): no type-label log line, key 3 says plain "Level N.".
- All other Scan UI features unchanged (key 1 name, key 2 description, key 4 HP, key 5 stats).
- Log volume reduction: a typical scan now logs ~3 lines vs. v0.14.70-diag's ~15–40 LITE lines per scan.

**Next chapter: v0.14.72 elemental affinity (keys 6 / 7 / 8).**

From v0.14.70-diag's BATTLE-TEXT-LITE captures during Fly-type scans we have direct evidence:
- text_id=38 returns `Weak against` header
- text_id=40 returns `has no effect` header (Glacial Eye scan, 14 bytes, decoded as "has no effect")
- text_id=101 / 102 / 106 return 3-byte sequences `<\x05>Y` / `<\x05>Z` / `<\x05>b` — control-code-prefixed glyph indices for element symbols (Y/Z/b are the FF8-encoded mappings)
- The engine renders the element symbols directly without going through a separate text fetch for the element name

Approach for v0.14.72: read the elemental affinity bytes directly from the entity struct (`entity+0x3C`, 8 × u16 covering Fire, Ice, Thunder, Earth, Poison, Wind, Water, Holy per the existing `ScanSnapshot::elem[8]` declaration). Map u16 affinity values to weak/absorb/null per the FF8 affinity scale (negative = absorb, 0 = null, positive = damage, very high positive = weak). Speak as e.g. "Weak against Fire and Ice. Absorbs Water. Nullifies Wind.".

No hook needed for v0.14.72 — the existing entity struct reads cover this. Pure data-formatting work.

**Remaining backlog (carried).**
1. Persistent accessibility settings
2. GF naming bypass (Siren)
3. Remove party-member NPCs from field entity catalog
4. X-ATMO92 chase
5. v0.14.72 elemental affinity (keys 6/7/8)
6. v0.14.73 status resistances (key 9)
7. v0.14.74 active statuses (key 0)
8. `kernel.bin` parsing for Blue Magic
9. **GitHub push** of ~85-build backlog (still unpushed)

---

**Previous build: v0.14.70-diag — diagnostic build to investigate Fastitocalon's missing type label. BAT result: MYSTERY SOLVED. Fastitocalon genuinely has no type label rendered on-screen; engine never calls `sub_47EC70(99)+sub_47EC70(36)` for type-less monsters. v0.14.71 promotes the wider gate + per-scan reset to production and strips the diagnostic logs.**

**v0.14.69 BAT result: PARTIAL WIN.**
- ✅ Bite Bug captured perfectly: `[SCAN-TTS] Type label captured slot=3 typeLabel='Fly Monster'` and key 3 said "Level 8, Fly Monster."
- ❌ Fastitocalon failed to capture: `Type label captured` line never fired during the 18-second open Scan UI window. Key 3 said just "Level 8.".
- ⚠️ Caterchipillar wasn't actually scanned (Aaron encountered the battle but didn't cast Scan).

**Hypothesis.** The Scan UI's render order varies by monster type. For Fly-type, the engine fetches the type label AFTER `sub_B687C0` fire #1, so v0.14.69's `IsScreenActive()`-gated hook caught it. For Fish-type, the type label probably gets fetched BEFORE fire #1 (during the cast animation), when `IsScreenActive()` is still false and the hook silently skips the bytes.

**v0.14.70-diag changes** (three coordinated, all in `HookedGetBattleText` plus `OnScanCast`):

1. **Broadened gate.** New helper `GetScanFlightSlot()` returns the active or pending slot, covering BOTH the action-layer phase (`s_pendingScanSlot >= 0`, set by `OnScanCast` at action-commit ~9 sec before fire #1) AND the visible Scan UI phase (`s_scanScreenActiveSlot >= 0`, set by fire #1). Replaces the old `IsScreenActive()` check inside `HookedGetBattleText`. (`IsScreenActive()` itself stays defined — still used by the keyboard router in `battle_tts_hp.inl`.)

2. **Per-scan reset in `OnScanCast`.** Clear `s_lastTypePrefixBytes`/`Len` at action-commit time so each new scan starts with a clean prefix-tracking buffer. Previously only `OnBattleEnter` reset it.

3. **Lightweight diagnostic.** During scan flight, every `sub_47EC70` call gets logged as `[BATTLE-TEXT-LITE] text_id=N (0xXX) bytes=B hex=[XX XX ...] ascii=|...| flight_slot=N`. 16 bytes max, single line, SEH-guarded. Plus when `text_id=36` fires inside the gate, log explicitly WHY it didn't capture (slot OOB, prefixLen=0, composed empty, already populated). Strip back out for v0.14.71 production once we understand the engine pattern.

**Files touched (v0.14.70-diag):**
- `src/scan_tts.cpp` — added `GetScanFlightSlot()` helper, replaced `HookedGetBattleText` body with wider gate + BATTLE-TEXT-LITE log + explicit failure-reason logging, added per-scan reset in `OnScanCast`. ~70 net lines added.
- `src/ff8_accessibility.h` — version bump to 0.14.70-diag.

**Expected v0.14.70-diag BAT outcome.**
- Cast Scan on enemies of varied types (Fastitocalon, Caterchipillar, Bite Bug, etc.).
- Look in `ff8_battle.log` for the `[BATTLE-TEXT-LITE]` sequence per scan. Expected pattern for a working capture: a sequence of varied text_ids with text_id=99 (or whatever the type prefix is) immediately followed by text_id=36 (the 'Monster' suffix). The line ordering tells us when each text_id fires relative to fire #1.
- For Bite Bug (Fly-type): expect Type label captured (preserved from v0.14.69 — wider gate is a strict superset).
- For Fastitocalon (Fish-type): if the broadened gate fixes it, expect `Type label captured slot=X typeLabel='Fish Monster'` and key 3 says "Level 8, Fish Monster.". If it still fails, the BATTLE-TEXT-LITE log will tell us whether text_id=36 fired at all and what the surrounding sequence looked like.
- For Caterchipillar: probably Beast/Insect/Earth type — same story, log will show.

**Branches after BAT lands.**
- *Both Fastitocalon and Caterchipillar work*: wider gate was the fix. Strip diagnostic for v0.14.71 production. Move on to v0.14.71 (elemental affinity, keys 6/7/8).
- *Fastitocalon works, Caterchipillar still fails (or vice versa)*: there's a third condition we haven't found. Examine the differing BATTLE-TEXT-LITE sequences for clues.
- *Both still fail*: text_id=36 isn't being called at all for those types — architectural rethink needed (maybe a separate UI-render hook, maybe a screenshot-OCR fallback, maybe a static type → monster_id table).

**Remaining backlog (carried).**
1. Persistent accessibility settings
2. GF naming bypass (Siren)
3. Remove party-member NPCs from field entity catalog
4. X-ATMO92 chase
5. v0.14.71+ continues Scan-data chapter: elemental affinity (keys 6/7/8), status resist (key 9), active statuses (key 0)
6. `kernel.bin` parsing for Blue Magic

---

**Previous build: v0.14.69 — PRODUCTION build wiring the Scan UI's monster-type label into the Level announcement (key 3). BAT result: PARTIAL WIN. Bite Bug worked ("Level 8, Fly Monster."), Fastitocalon failed (key 3 said "Level 8." — type label not captured). v0.14.70-diag investigates the gap.**

**v0.14.68-diag BAT result: DECISIVE WIN.** Glacial Eye scan produced 10 unique `[BATTLE-TEXT-DIAG]` entries during the open Scan window. After FF8 text decoding (uppercase encoded = decoded + 4, lowercase encoded = decoded - 2), the entries map cleanly to Scan UI labels:

| text_id | Decoded | Role |
|---------|---------|------|
| 33 (0x21) | `LEVEL` | Header label |
| 34 (0x22) | `HP` | Header label |
| 36 (0x24) | `Monster` | Universal type suffix |
| 38 (0x26) | `Weak against` | Element header |
| 40 (0x28) | `has no effect` | Element header |
| 99 (0x63) | `Fly` | Type prefix (varies per monster) |
| 113 (0x71) | `/////` | Underline glyph |
| 11 (0x0B) | (UI position list bytes) | Layout descriptor |
| 101 (0x65) | `<\x05>Y` | Element symbol |
| 102 (0x66) | `<\x05>Z` | Element symbol |

**The on-screen 'Fly Monster' label at the bottom-left of the Scan UI is rendered by TWO consecutive `sub_47EC70` calls** — first the type prefix (text_id varies per monster type, e.g. 99 returns 'Fly' for Glacial Eye), then `text_id=36` which always returns the universal 'Monster' suffix. Visually confirmed in `scan_001352_348_slot3_Glacial_Eye.png` (bottom-left renders 'Fly Monster' exactly).

**v0.14.69 implementation.** Mod stays monster-type-agnostic — we don't need to enumerate the prefix-text_id-to-name mapping. The engine itself does the lookup; we observe the result via the existing `sub_47EC70` hook. Algorithm in `HookedGetBattleText`:

1. While `IsScreenActive()` is true (a Scan UI session is open), every `sub_47EC70` call's returned bytes are snapshotted into `s_lastTypePrefixBytes` (32-byte buffer, SEH-guarded).
2. When `text_id == 36` ('Monster' suffix) fires, decode the snapshotted prior bytes via `FF8TextDecode::Decode`, compose `'{prefix} Monster'`, store in `s_scanCache[active_slot].typeLabel`. Single-write per scan event — we don't overwrite if `text_id=36` is refetched later for some other UI element.
3. `FormatLevel()` checks `snap.typeLabel` and appends if populated: "Level 14, Fly Monster." instead of "Level 14.".

**Auto-announce timing unchanged.** The auto-announce still fires at `sub_B687C0` fire #1 (the genuine "window opened visually" signal). The type label hasn't been fetched yet at that exact moment — by the time the user presses key 3 (typically several seconds after the Scan UI opens), the type label is captured and ready.

**Stripped from v0.14.66/67/68 diagnostic builds.** All gone:
- `PollDiagnosticKey()` (F12-gated SCAN-TYPE-DIAG dump)
- `DumpHexWindow` helper, the 10 candidate-base probes
- `BATTLE-TEXT-DIAG` hex+ASCII log line in `HookedGetBattleText`
- Forward decl in `scan_tts.h`
- Call site in `battle_tts_hp.inl` `PollHPCheckKeys`
- The `s_diagF12WasDown` static

F12 returns to its 'reserved for diagnostic builds only' status.

**Files touched (v0.14.69):**
- `src/scan_tts.cpp` — added `typeLabel[64]` field to `ScanSnapshot`; added `s_lastTypePrefixBytes`/`s_lastTypePrefixLen` tracking state; replaced `HookedGetBattleText` body (logging → type-label capture); added `SnapshotPrefixBytesSafe` and `ComposeTypeLabelToBuf` helpers; updated `FormatLevel` to include typeLabel; added typeLabel reset in `OnBattleEnter`; stripped the entire `PollDiagnosticKey`/`DumpHexWindow` block (~250 lines) at the file's end.
- `src/scan_tts.h` — removed `PollDiagnosticKey` forward decl and its long doc comment.
- `src/battle_tts_hp.inl` — removed the `::ScanTTS::PollDiagnosticKey()` call site at the top of `PollHPCheckKeys` and its surrounding comment.
- `src/ff8_accessibility.h` — version bump.

**Expected v0.14.69 BAT outcome.**
- Aaron casts Scan on any enemy (a Fly-type like Bite Bug or Glacial Eye is the easiest first test, but ANY monster works since the mod is type-agnostic).
- Scan UI opens, auto-announce fires as before: "<Name>. <Description>. Press number keys 1 through 0 for details."
- During the open window, the new hook captures the type label silently. Look in `ff8_battle.log` for one log line per scan: `[SCAN-TTS] Type label captured slot=N typeLabel='Fly Monster'`.
- Aaron presses key 3 — should hear "Level 14, Fly Monster." instead of just "Level 14.".
- For ally targets, no type label gets captured (the engine doesn't draw the type label for allies); key 3 should fall back to the plain "Level N." announcement.

**Remaining backlog (carried from v0.14.68-diag).**
1. Persistent accessibility settings across play sessions
2. GF naming screen bypass — Siren failed to bypass
3. Remove party member NPCs from field entity catalog
4. X-ATMO92 chase scene accessibility
5. v0.14.70+ continues Scan-data chapter: elemental affinity (keys 6/7/8), status resist (key 9), active statuses (key 0)
6. `kernel.bin` parsing for Blue Magic spell names

---

**Previous build: v0.14.68-diag — hook on sub_47EC70 (`get_battle_text`) to log every text_id requested during a Scan UI session, finding the type-label fetch by direct interception. BAT result: DECISIVE WIN, see v0.14.69 entry above.**

**v0.14.67-diag BAT result: HYPOTHESIS DISPROVEN, but with useful data.** The static table at `0x015D0B40` is NOT a 16-entry type-info table. All 16 entries (256 bytes) are byte-for-byte identical (`00 EE 00 02 80 FD 80 02 01 00 00 00 01 01 01 00`) across both Bite Bug and Fastitocalon dumps. The first dword (0x0200EE00) dereferences to UV-coordinate-style data, identical for both monsters. So `[edi+0xD] = 0x01` always for entries 0–15 — confirming `[edi+0xD]` is a static UI-layout flag, NOT a per-monster type byte. The `cmp al, bl` checks in `sub_84FD90` are layout switches, not type lookups. The 156-byte monster-info entry at `0x01D972C4 + monster_id*156` differs strongly between Bite Bug (full structured data) and Fastitocalon (mostly zeros), but probably because we're reading well past the valid table extent (monster_ids 0x25 and 0x2C are far beyond a typical 7-slot battle entity array). The `sub_84D410` input bytes (0x00 vs 0xA7) differ but the contextual evidence suggests this isn't the type byte either.

**Disassembly walk findings (sub_84FD90, ~250 of 540 instructions covered).** The first ~150 instructions are pure UI flag-checking: read static config words at `[0x269aac8..0x269aade]`, conditionally adjust UI element positions at `[esi+0x40..0x42]` and `[eax+0xc..0xe]` based on `[edi+0xC]` and `[edi+0xD]`. Around 0x00850150-0x008502E0 there's a function-call cluster that initially looked promising (4 calls to `sub_49F0A0` with arg2=2/3/?/1, results stored in static "UI element width" array `[0x269aaac..0x269aab2]`, then `cmp ax, 0x60`). But peeking into `sub_49F0A0` reveals it's NOT a text fetcher — it does cascading 196-byte-stride table lookups at `0x01D2B110/+0x18/+0xC2` followed by `(value - arg2) & 7` modular-7 arithmetic, which looks like geometric/positioning math (compass direction? sprite frame?), not text fetching. The actual type-label render is either in the remaining ~290 instructions of sub_84FD90 or in a different scan-UI phase function (sub_850650/sub_850690/sub_8506B0).

**v0.14.68-diag pivot: hook the engine text-fetch function directly.** Per Aaron's user memory, `sub_47EC70` is FF8's canonical `get_battle_text(int text_id)` function. First instructions confirm the signature: `const char* __cdecl get_battle_text(int text_id)`, with positions table at `0x01CF8B50` (u16 stride 2) and fallback string ptr `0x01CFF84C`. Every battle-context string — including the type label — should pass through this function. By installing a MinHook trampoline and logging every (text_id, returned_ptr) call while a Scan UI session is active, we identify the type-label text_id by:
1. Decoding the returned string in post-processing (FF8 text encoding — lowercase = ASCII - 2, uppercase = ASCII + 4)
2. Matching the decoded string to the on-screen "Fly Monster" / "Fish Monster" labels
3. Recording the text_id as the canonical lookup index for type names

**Implementation (v0.14.68-diag).** Mirrors the existing `sub_B687C0` hook pattern in `scan_tts.cpp`:
- New constant `BATTLE_GET_TEXT_ADDR = 0x0047EC70`, typedef `GetBattleTextFn`, static fn ptr `s_originalGetBattleText`, install flag.
- New `HookedGetBattleText(int textId)` callback that calls the original, then if `IsScreenActive()` logs `[BATTLE-TEXT-DIAG] text_id=N (0xX) ptr=0x... bytes=N hex=[XX XX ...] ascii=|...|` (first 32 bytes, SEH-guarded, stops at null terminator). Gating on `IsScreenActive()` keeps the diagnostic from spamming every battle frame's command-menu / status-popup / victory-text fetches.
- New `InstallGetBattleTextHook()` paralleling `InstallScanGetTextHook()`.
- Called from `Initialize()` alongside the existing scan hook.
- All v0.14.66/67 F12 probes retained — they're harmless when F12 isn't pressed and may still produce supplementary info.

**Files touched (v0.14.68-diag):**
- `src/scan_tts.cpp` — ~90 lines added near the existing `InstallScanGetTextHook` (new constants/typedef, `HookedGetBattleText` callback, `InstallGetBattleTextHook` installer); one-line addition in `Initialize()`.
- `src/ff8_accessibility.h` — version bump.
- All other modules unchanged.

**Expected v0.14.68-diag BAT outcome.**
- Aaron casts Scan on Bite Bug and Fastitocalon (3x F12 each is fine but the diagnostic auto-logs without F12 — just need the Scan UI active).
- For each scan, `ff8_battle.log` accumulates a sequence of `[BATTLE-TEXT-DIAG]` lines covering every battle-text fetch during the open window: monster name, description, type label, plus any other UI strings.
- We post-process the hex bytes to find the entry whose decoded text is "Fly Monster" (Bite Bug) or "Fish Monster" (Fastitocalon).
- That entry's `text_id` is the canonical type-label ID. The positions table at `0x01CF8B50` then gives us the offset for THIS monster's type label, but we still need to figure out which `text_id` corresponds to which monster (probably some helper does monster_id → type_text_id mapping).

**After v0.14.68-diag BAT lands.**
- v0.14.69 implements the type-label fetch using the discovered `text_id` and wires it into Level announcement ("Level 14 Fly Monster"). Production build, all diag stripped.
- Then v0.14.70+ continues the Scan-data chapter: elemental affinity (keys 6/7/8), status resist (key 9), active statuses (key 0).

---

**Previous build: v0.14.67-diag — dumped the static `0x015D0B40` table that disassembly suggested was a type-info table. Result: all 16 entries identical, `[edi+0xD]` = static UI-layout flag, hypothesis disproven (see v0.14.68 entry above).**

**v0.14.66-diag BAT result: PARTIAL.** F12 dumps captured cleanly (zero SEH exceptions, three repeats consistent) for Fastitocalon (monster_id=0x25, Lv10) and Bite Bug (monster_id=0x2C, Lv14). Diff conclusion: **the type field is NOT in the per-monster runtime entity struct.** Key diff outcomes:
- `+0xBD..+0xBF = 00 64 02` for BOTH monsters — fixed default, not type.
- Differences at `+0x90` and `+0x9D` are individual status resistances, not type categories.
- All 9 candidate-base 16-bit probes returned values that proved to be continuations of `scan_text_positions` (description offsets at different monster_ids), NOT a parallel type-positions table.
- All 6 candidate-base 8-bit probes returned bytes inside `scan_text_data` description text — not a single-byte type-index table.
- The "before scan_text_positions" dump decodes to UNRELATED game text ("arms and / status / change / attacks", "Galbadia Missile / Base security / soldiers" etc.) — some other text pool that just happens to live below scan_text_positions.

**Disassembly investigation found the actual type chain.** Reading `sub_84FD90` (Scan UI phase-1 main render) revealed:
```
sub_84FD90(scan_ui)            ; phase-1 render
  edi = sub_84EAF0(scan_ui)    ; returns ptr into a 16-byte-stride table
    │   reads byte at [scan_ui + 0x2D]
    │   passes to sub_84D410, gets remapped int
    └── returns 0x015D0B40 + index * 16   ★ STATIC TYPE-INFO TABLE
  reads byte at [edi + 0x0D]   ★ TYPE-CLASS BYTE
```
`sub_84D410` reads `[0x01D972C4 + arg*156]` and runs a long cmp/je remap chain on the result — i.e. multiple monster_ids fold into the same type class. So the type lives in a static table at VA `0x015D0B40`, accessed via a per-monster_id 156-byte struct array at `0x01D972C4`.

**v0.14.67-diag adds new probes** to `PollDiagnosticKey()`:
7. **256 bytes at `0x015D0B40`** (full assumed type-info table — 16 entries × 16 bytes).
8. **156 bytes at `0x01D972C4 + monster_id*156`** (the per-monster_id remap-table entry).
9. **Pointer-walk over the type-info table:** for each of the 16 entries, treat first 4 bytes as a candidate pointer; if it's in the .data range (0x00F6D000..0x02B9F000), dereference and dump 64 bytes — likely a type-name string (FF8-encoded).
10. **Single-line diff target:** the byte at `[0x01D972C4 + monster_id*156]` (sub_84D410's actual input read) called out explicitly so the diff between two enemies is one line.

**Files touched (v0.14.67-diag):**
- `src/scan_tts.cpp` — ~110 lines added at end of `PollDiagnosticKey()` before the closing separator. New static constants `TYPE_INFO_TABLE_BASE = 0x015D0B40`, `MONSTER_INFO_TABLE_BASE = 0x01D972C4`, `MONSTER_INFO_STRIDE = 156`.
- `src/ff8_accessibility.h` — version bump.
- All other v0.14.66-diag plumbing (F12 polling, SEH-guarded `DumpHexWindow`, etc.) unchanged.

**Expected v0.14.67-diag BAT outcome.**
- Aaron casts Scan on Bite Bug (`Fly Monster`), Fastitocalon (`Fish Monster` likely), and ideally one more (Caterchipillar or Geezard) for a third type.
- Each F12 press yields a complete dump set including the new probes 7–10.
- The pointer-walk in probe 9 should reveal one or two entries whose first dword decodes to FF8-encoded text matching "Fly Monster" / "Fish Monster" etc. once decrypted.
- Probe 10's single byte (`sub_84D410` input) should differ between Bite Bug and Fastitocalon if `[scan_ui+0x2D]` ends up being effectively monster_id-keyed.
- The byte at `[entry+0x0D]` (probe 9's per-entry read) for the type entry of each monster will identify the type-class id directly.

**After v0.14.67-diag BAT lands:**
- v0.14.68 wires the type label into Level announcement: "Level 14 Fly Monster" instead of just "Level 14". Production build, no diag remaining.
- Then the queue continues with v0.14.69 (elemental affinity keys 6/7/8), v0.14.70 (status resist key 9), v0.14.71 (active statuses key 0).

---

**Previous build: v0.14.66-diag — F12-gated diagnostic to find the 'Fly Monster' / 'Earth Monster' monster-type field. BAT PARTIAL (see v0.14.67 entry above).**

**v0.14.65.3 BAT result: PASS.** Frame-delay screenshot capture works perfectly. Stats announce was perfect ("Strength 12. Vitality 4. Magic 9. Spirit 4. Speed 5. Luck 0. Evasion 3. Hit 0." for Lv14 Bite Bug, matching `[SCAN-CACHE]` log). Screenshot at `Logs\screenshots\scan_201417_738_slot3_Bite_Bug.png` shows the **fully rendered Scan UI** for the first time, including the description "A bug monster that flies. Stay calm and attack precisely. It's not a very strong enemy." 

**v0.14.66-diag motivation.** The Bite Bug screenshot exposed a previously-unknown UI element: a `Fly Monster` text label rendered at the bottom-left of the Scan UI. This is the enemy's monster type/family — vanilla FF8 (Aaron confirmed no mods adding this). We don't currently announce it, and we don't yet know where it lives in memory.

**Investigation so far (cold reads from disassembly):**
- `sub_B687C0` (the description fetcher we already hook) confirmed: reads `entity[+0xB3]` as monster_id, indexes `scan_text_positions[0x01887474]` to get an offset, returns `scan_text_data[0x018875B4] + offset`. Decodes to the description text.
- `sub_84F860` is a 6-entry phase dispatcher reading `state[+0x29]`. Phase 0 calls sub_84F8D0 (description). Phase 1 calls sub_84FD70 → sub_84FD90 (main detail render). Phases 2-4 call sub_850650/sub_850690/sub_8506B0. Phase 5 = `ret`.
- FFNx canary `ff8.h` documents only `scan_text_positions` / `scan_text_data` for the Scan UI. No "monster type" / "family" / "category" table is named. Also has `get_card_name` / `card_name_positions` (Triple Triad cards) — but Bite Bug's card is "Bite Bug", not "Fly Monster", so cards are NOT it.
- `FF8_EN_strings.txt` confirms "Fly Monster", "Fly", "Monster" do NOT appear as plain ASCII anywhere in the EXE — must be encoded text in a data table.

**v0.14.66-diag approach.** F12-gated runtime memory dump. When Aaron presses F12 with a Scan window open, `ScanTTS::PollDiagnosticKey()` dumps to `ff8_battle.log` under the `[SCAN-TYPE-DIAG]` tag:
1. Full 0xD0-byte entity struct for the active scan slot — the type might be a single byte at one of the unknown-meaning offsets.
2. 0x80 bytes BEFORE `scan_text_positions` (0x01887474) — looking for a parallel positions table for the type strings.
3. Full `scan_text_positions` table (0x140 bytes for ~160 monster_id entries) for cross-reference.
4. First 0x100 bytes of `scan_text_data` (0x018875B4).
5. 16-bit reads at `id*2` from 9 candidate base addresses around `scan_text_positions ± 0x200`.
6. 8-bit reads at `id` from 6 candidate base addresses around `scan_text_data ± 0x200`.

All reads are SEH-guarded; the dump format includes ASCII gutters for spotting encoded text strings. Aaron BATs by casting Scan on **2-3 enemies of varied types** (Bite Bug = Fly Monster, plus a Geezard or Caterchipillar of a different type), pressing F12 in each Scan UI, then uploads `ff8_battle.log`. We diff the dumps; bytes that change with the type label correspond to the field we're looking for.

**Files touched (v0.14.66-diag):**
- `src/scan_tts.h` — added `PollDiagnosticKey()` to public API with full doc comment.
- `src/scan_tts.cpp` — added `s_diagF12WasDown`, static helper `DumpHexWindow()`, public `PollDiagnosticKey()` at end of file before namespace closer (~190 lines).
- `src/battle_tts.cpp` — added forward decl in `namespace ScanTTS { ... }` block.
- `src/battle_tts_hp.inl` — added one-line `::ScanTTS::PollDiagnosticKey()` call at top of `PollHPCheckKeys()`.
- `src/ff8_accessibility.h` — version bump.

**F12 verified free** across all source files (no existing `VK_F12` / `0x7B` handlers). No behavior change for normal play — F12 is silent unless the Scan window is open.

**Expected v0.14.66-diag BAT outcome.**
- Cast Scan on Bite Bug → window opens, auto-announce fires, screenshot captures.
- Press F12 while window is open → `ff8_battle.log` gets a `[SCAN-TYPE-DIAG]` block with all six dumps (full entity struct + ~0x300 bytes of table data + ~15 candidate-base probes).
- Repeat for a different enemy of a different type (e.g. Caterchipillar = Earth Monster, or Geezard = whatever its type is).
- Aaron uploads the log; we identify the type field by diffing.

---

**Previous build: v0.14.65.3 — frame-delay counter on async screenshots so FF8's typewriter rendering finishes before capture. BAT PASS.**

**v0.14.65.2 BAT result: PASS.** Path fix worked perfectly. Battle log line at 23:42:09 shows the absolute path: `[SCAN-CAPTURE] Auto-screenshot requested at fire #1 slot=3 path='C:\Users\ampag\OneDrive\Documents\FFVIII-Accessibility-Mod\FF8_OriginalPC_mod\Logs\screenshots\scan_234209_596_slot3_Fastitocalon.png'` paired with `[VICTORY-SCREENSHOT] Saved 640x480`. Claude reads the PNG directly from `Logs\screenshots\` — no manual copying needed. Stats announced "Strength 20. Vitality 132. Magic 56. Spirit 180. Speed 5. Luck 0. Evasion 6. Hit 0." for Lv14 Fastitocalon, matching `[SCAN-CACHE]` log exactly. The image content (same too-early render — labels visible, no numeric values, typewriter partial) is the same issue v0.14.65.1 had; addressing that now in v0.14.65.3.

**v0.14.65.3 root cause.** The async screenshot fires on the very next `SwapBuffers` after the request — ~16 ms at 60 fps. FF8's typewriter text rendering takes ~1–1.5 s to draw the description (~95 chars at ~1.5 chars/frame) plus another ~10–15 frames for stat values. So the captured framebuffer reliably catches stat labels (drawn instantly as a static layout) but only the first 4–8 chars of typewriter content.

**v0.14.65.3 fix.** Extended the existing async screenshot machinery with a frame-delay counter. New `static volatile int s_captureFrameDelay` alongside `s_captureRequested` in `battle_tts_screenshot.inl`. `HookedSwapBuffers` decrements the counter each frame and only calls `DoGLCapture()` when it reaches 0. `RequestScreenshotAsync` now takes an optional `frameDelay` parameter (default 0 preserves the v0.14.65 behavior; existing callers including the synchronous `CaptureScreenshot` path are unaffected because that path explicitly resets `s_captureFrameDelay = 0` to keep its 160 ms Sleep-and-poll contract). `scan_tts.cpp` passes 90 frames (≈1.5 s at 60 fps), comfortably above the ~63–80 frames needed for full description + stat values.

**Why 90 frames is safe:**
- Description "A fish that swims in the ground..." is ~95 chars at ~1.5 chars/frame = ~63 frames.
- Stat values render in ~10–15 more frames once the description completes.
- 90 frames ≈ 1.5 s gives ~12–17 frames of safety margin.
- User won't notice the visual delay (Aaron is blind — only the diagnostic capture is being deferred).
- BAT log shows ~6 s between scan UI open and first key press, so the delayed capture can't accidentally catch a different UI state.

**Files touched (v0.14.65.3):** `battle_tts_screenshot.inl` (`s_captureFrameDelay` declaration with v0.14.65.2 BAT motivation comment + `HookedSwapBuffers` counter check + `CaptureScreenshot` reset to 0), `battle_tts.h` (`RequestScreenshotAsync` gets default `frameDelay=0` parameter with docstring), `battle_tts.cpp` (impl reads `frameDelay` and clamps negatives to 0), `scan_tts.cpp` (call site passes 90 + log message updated), `ff8_accessibility.h` (version).

**Expected v0.14.65.3 BAT outcomes:**
- Cast Scan on a common enemy. Hear auto-announce + stats key 5 (unchanged).
- Screenshot lands at `Logs\screenshots\scan_<HHMMSS>_<MS>_slot<N>_<EnemyName>.{bmp,png}` ~1.5 s after fire #1.
- Battle log shows `[SCAN-CAPTURE]` line with `(90-frame delay ≈ 1.5 s)` suffix at fire #1, paired with the `[VICTORY-SCREENSHOT] Saved` line ~90 frames later.
- **Captured image shows the FULL scan UI: labels + all 6 numeric stat values + complete enemy name + complete description text.** This finally lets us visually validate the LCK/EVA/HIT positioning and confirm the stat-name mapping (STR→Strength, VIT→DEF, MAG→INT, SPR→SPI, SPD→DEX) is correct.

**Still pending after v0.14.65.3 BAT:**
- **GitHub push** of v0.13.83→v0.14.65.x backlog.
- **v0.14.66** — Elemental affinity (keys 6/7/8). 8×u16 at `entity+0x3C`.
- **v0.14.67** — Status resist (key 9). 20 bytes at `entity+0x4C`.
- **v0.14.68** — Active statuses (key 0). Status bitfield at `entity+0x78`.

**v0.14.65 chapter (retained for reference):**

**v0.14.65.1 BAT result: PASS (architecturally) but file landed outside Claude's read access.**
- The trigger fired correctly at fire #1: `[SCAN-CAPTURE] Auto-screenshot requested at fire #1 slot=3 path='Screenshots\scan_224316_252_slot3_Fastitocalon.png'` paired with `[VICTORY-SCREENSHOT] Saved 640x480`.
- Aaron manually copied the file out for upload. Image content validated:
  - **The Scan UI displays HP + 6 stats** (not 8) using abbreviations: `STR / DEF / INT / SPI / DEX / EVA`.
  - The on-screen labels differ from FF8's internal stat names: internal `VIT` displays as `DEF`, `MAG` as `INT`, `SPR` as `SPI`, `SPD` as `DEX`. `LCK` and `HIT` are internal-only and not displayed in the Scan UI.
  - My TTS uses the internal names ("Strength / Vitality / Magic / Spirit / Speed / Luck / Evasion / Hit") which is fine — extra info (LCK/HIT) is helpful, not harmful.
- **Capture timing is one tick too early.** Only labels visible; numeric stat values haven't started rendering yet (FF8 uses typewriter-style text rendering). Description column shows only `"Fast"` and `"A fi"` partial chars. The labels confirm layout but values aren't visible.
- **Stats announcement perfect again**: "Strength 20. Vitality 132. Magic 56. Spirit 180. Speed 5. Luck 0. Evasion 6. Hit 0." for Lv14 Fastitocalon, matching `[SCAN-CACHE]` log exactly.

**v0.14.65.1 root cause of file-location issue.** My code used a relative path `"Screenshots\\scan_..."` which Windows resolves against the FF8 process's CWD — the Steam install dir at `C:\Program Files (x86)\Steam\steamapps\common\FINAL FANTASY VIII\Screenshots\`. That's outside Claude's allowed directories, so even a successful capture is invisible until Aaron manually moves the file.

**v0.14.65.2 fix.** Added `BattleTTS::GetScreenshotDir()` public accessor that returns `KIND4_SCREENSHOT_DIR` (the absolute hardcoded path `C:\Users\ampag\OneDrive\Documents\FFVIII-Accessibility-Mod\FF8_OriginalPC_mod\Logs\screenshots`). `KIND4_SCREENSHOT_DIR` lives as a file-static const in `battle_tts_sprite.inl` so it's only visible within `battle_tts.cpp`'s translation unit — the accessor wraps it for cross-TU consumers. `scan_tts.cpp` now composes the absolute path via this accessor:

```cpp
snprintf(path, sizeof(path),
         "%s\\scan_%02d%02d%02d_%03d_slot%d_%s",
         BattleTTS::GetScreenshotDir(),
         wt.wHour, wt.wMinute, wt.wSecond, wt.wMilliseconds,
         slot, safeName);
```

The `CreateDirectoryA("Screenshots", NULL)` call was dropped since the existing kind4/sprite-poll capture mechanisms (which always fire first during a Scan event) ensure the directory exists.

**Files touched (v0.14.65.2):** `battle_tts.h` (`GetScreenshotDir()` declaration next to `RequestScreenshotAsync()`), `battle_tts.cpp` (`GetScreenshotDir()` implementation next to `RequestScreenshotAsync()`, returns `KIND4_SCREENSHOT_DIR` which is in scope after the `battle_tts_sprite.inl` include), `scan_tts.cpp` (replaced relative-path block with absolute-path block + dropped the now-redundant `CreateDirectoryA("Screenshots", NULL)` call), `ff8_accessibility.h` (version).

**Expected v0.14.65.2 BAT outcomes:**
- Cast Scan on a common enemy. Hear auto-announce + stats key 5 (unchanged from v0.14.65.1).
- Screenshot lands at `Logs\screenshots\scan_<HHMMSS>_<MS>_slot<N>_<EnemyName>.png` (and matching `.bmp`) — Claude can read it directly without Aaron copying anything.
- Battle log shows `[SCAN-CAPTURE]` line with the new absolute path.

**Still pending after v0.14.65.2 BAT:**
- **v0.14.65.3** — capture timing fix. Add a tick-based delay (~500-1000ms after fire #1) so the typewriter effect completes before SwapBuffers fires and we get a fully-rendered scan UI with all numeric values visible.
- **GitHub push** of v0.13.83→v0.14.65.x backlog.
- **v0.14.66** — Elemental affinity (keys 6/7/8).

**v0.14.65 chapter (retained for reference):**

**v0.14.65 implementation.** Two complementary changes bundled in this build:

### Part A: Stats wiring (key 5)

Reads the 8-byte stat block at `entity_base + 0xB5..0xBC` into `ScanSnapshot.stats[8]` during `CaptureSnapshot`, then formats it for spoken output on key 5 press during the open Scan window. Three additions to `scan_tts.cpp`:

1. **`ReadSlotStats(slot, outStats)` helper.** SEH-guarded `memcpy(outStats, base + BENT_STR, 8)`. Follows the existing `ReadSlotLevel` / `ReadSlotMonsterId` pattern.
2. **`CaptureSnapshot` populates `snap.stats`.** Single line: `ReadSlotStats(slot, snap.stats);`. The `[SCAN-CACHE]` log line is extended to include all 8 stat values: `stats=[STR=X VIT=Y MAG=Z SPR=W SPD=S LCK=L EVA=E HIT=H]`.
3. **`FormatStats` helper + `SpeakField` case 5.** Speaks all 8 stats as: `"Strength X. Vitality Y. Magic Z. Spirit W. Speed S. Luck L. Evasion E. Hit H."` Falls back to `"Stats unavailable."` if all 8 read as zero.

**RAM order note.** Validated `BENT_*` constants in `battle_tts.h` give runtime-RAM order **STR/VIT/MAG/SPR/SPD/LCK/EVA/HIT** — LCK at index 5 between SPD and EVA. Differs from FFRTT Section-7 .dat-file order (LUCK last). `battle_tts.h` is authoritative.

### Part B: Auto-screenshot capture for visual validation

**Why:** Aaron is blind and can't visually verify that the on-screen scan UI matches the announced/logged data. Adding an automatic screenshot at the moment the scan UI is fully rendered lets Claude do that comparison offline by reading the uploaded PNG.

**Architecture.** The internal `CaptureScreenshot()` in `battle_tts_screenshot.inl` blocks the calling thread for up to 160 ms (Sleep loop waiting for HookedSwapBuffers to consume the flag). Calling that from inside `HookedScanGetText` — a MinHook callback running on the game thread — would freeze the game for ~10 frames. So this build adds a non-blocking variant.

1. **`BattleTTS::RequestScreenshotAsync(basePath)`** — new public wrapper in `battle_tts.cpp`, declared in `battle_tts.h`. Sets `s_captureBasePath` + `s_captureRequested = true` and returns immediately. The next `HookedSwapBuffers` tick (within ~16 ms at 60 fps) picks up the flag and runs `DoGLCapture()` inline. Lives right next to `CancelNoEffectWatchdogForSlot` and follows the same "public-wrapper-into-static-inl-state" pattern.
2. **`HookedScanGetText` count==30 trigger.** ~500 ms after window-open the Scan UI is fully rendered. We trigger one capture per scan event at that point, using the cached `s_scanCache[slot].name` for filename context. Filename name-sanitization strips non-alnum (defensive against apostrophes in `Sorceress's`, dashes in `Fastitocalon-F`, etc.). Path: `Screenshots\scan_<HHMMSS>_<MS>_slot<N>_<safeName>.{bmp,png}` relative to FF8's working directory. `[SCAN-CAPTURE]` log line records the path.

**Files touched (full list):** `scan_tts.cpp` (Part A: `ReadSlotStats` + populate-stats + log extension + `FormatStats` + `SpeakField` case 5; Part B: `BattleTTS::RequestScreenshotAsync` forward decl + count==30 capture block in `HookedScanGetText`), `battle_tts.cpp` (Part B: `RequestScreenshotAsync` wrapper next to `CancelNoEffectWatchdogForSlot`), `battle_tts.h` (Part B: `RequestScreenshotAsync` public declaration), `ff8_accessibility.h` (version).

**Expected v0.14.65 BAT outcomes:**
- Cast Scan on Bite Bug (or any common enemy). Hear the auto-announce as before.
- While the scan window is open, **press 5**. Hear: *"Strength X. Vitality Y. Magic Z. Spirit W. Speed S. Luck L. Evasion E. Hit H."*
- ~500 ms after the window opened, a screenshot lands at `Screenshots\scan_<...>.png` in the FF8 install directory. Aaron uploads to Claude for cross-check.
- `[SCAN-CACHE]` log line shows `stats=[STR=... VIT=... ...]`. `[SCAN-CAPTURE]` log line shows the screenshot path.
- The on-screen scan UI's stat display (Strength / Vitality / Magic / Spirit / Speed) should match what the mod announces.
- Press 6/7/8/9/0 — still hear `"Not implemented yet."`.
- Press 1/2/3/4 — unchanged.

**Risk to validate:** the LCK at index 5 (between SPD and EVA) ordering. If announced Luck sounds wrong vs the screenshot, fix as v0.14.65.1.

**Next priorities (v0.14.66+):**
1. **GitHub push** of the v0.13.83→v0.14.65 backlog (~80 builds unpushed since v0.13.63).
2. **v0.14.66** — Elemental affinity (keys 6/7/8). 8×u16 at `entity+0x3C`. 6=Weaknesses (`<800`), 7=Absorbs (`≥1000`), 8=Nullifies (`=900`).
3. **v0.14.67** — Status resist (key 9). 20 bytes at `entity+0x4C`. Threshold `≥100` = "Strong vs."
4. **v0.14.68** — Active statuses (key 0). Status bitfield at `entity+0x78`. Reuse target-announce status decoder.

**v0.14.64 chapter (retained for reference):**

**v0.14.63 BAT result: ALL FIXES CONFIRMED.** Aaron reported: *"That worked perfectly! Battle dialogs are reading again and Scan worked as expected with no duplication."* The IsScreenActive()-based gate correctly suppresses only the rendered scan-window text duplicate while letting all other battle UI text (Cast Fire, Cast Cure, etc.) speak normally through Hook_show_dialog.

**v0.14.64 cleanup.** The SC1-PROBE and SC2-PROBE diagnostic instrumentation served their purpose (identified Hook_show_dialog as the source of the duplicate scan announce) and are now just adding log noise on every speech call. This build strips them and the now-unused `<intrin.h>` include for `_ReturnAddress()`.

**What stays in place from the v0.14.60-63 chapter:**
- **Single-channel SAPI mode** (v0.14.61): `s_pVoice2 = nullptr` in InitSAPI. Aaron prefers it; eliminates the simultaneous-overlap class of bugs entirely.
- **sub_B687C0 first-fire announce trigger** (v0.14.60): the architectural foundation that fires the scan auto-announce at window-open time, not action-commit time.
- **Per-scan reset of s_scanHookFireCount** (v0.14.60): so 'first fire' means 'first fire of THIS scan event,' not cumulative across all scans this battle.
- **IsScreenActive()-gated Hook_show_dialog suppression** (v0.14.63): suppress only when scan window is open; otherwise speak all battle UI text normally.
- **show_dialog still owned by field_dialog.cpp**: it's the universal text renderer, not really a 'field' hook. If we ever want to truly separate concerns (battle window text owned by BattleTTS module with its own dedup/style policy), that's a bigger refactor for later. For now, the IsScreenActive() gate gets us the right behavior with minimal code.

**Files touched:** `screen_reader.cpp` (3 deletions: SC1-PROBE block in Speak, SC2-PROBE block in SpeakChannel2, `<intrin.h>` include), `ff8_accessibility.h` (version).

**Expected v0.14.64 BAT outcomes:** identical audible behavior to v0.14.63 (scan announce works once, battle dialogs work, no duplicates) but with a much quieter `ff8_mod.log` — no `[SC1-PROBE]` / `[SC2-PROBE]` lines. Verify by casting Scan and confirming no audio regression vs v0.14.63.

**Scan TTS chapter closed.** With v0.14.64 the v0.14.50-64 Scan TTS arc is complete:
- v0.14.50: First slice (name + level + HP, hidden-HP soft fallback)
- v0.14.51: No-effect watchdog cancellation
- v0.14.52-58: Scan window detection iterations (sub_B687C0 hook, sub_84F860 hook, popup-spawn detection)
- v0.14.59: UX redesign — silent action-layer + screen-open auto-announce + interactive number keys 1..0
- v0.14.60: Architectural fix — announce on sub_B687C0 first-fire (window render time), not popup-spawn (action-commit time)
- v0.14.61: Single-channel SAPI mode + SC1-PROBE
- v0.14.62: Hook_show_dialog blanket-battle-mode gate (over-suppressed)
- v0.14.63: Hook_show_dialog narrowed to scan-active gate (correct)
- v0.14.64: Strip diagnostic probes (this build)

Keys 1..4 (Name/Description/Level/HP) are working. Keys 5..0 (Stats/Weak/Absorb/Nullify/StatusRes/ActiveStatus) still answer 'Not implemented yet.' That's the next chapter.

**Next priorities (v0.14.65+):**
1. **v0.14.65 — Stats (key 5).** Read STR/VIT/MAG/SPR/SPD/EVA/HIT/LUCK at `entity+0xB5..0xBC`. Snapshot already reserves `uint8_t stats[8]` in ScanSnapshot — just wire CaptureSnapshot to read them and FormatStats helper for SpeakField case 5.
2. **v0.14.66 — Elemental affinity (keys 6/7/8).** 8×u16 at `entity+0x3C` for Fire / Ice / Thunder / Earth / Poison / Wind / Water / Holy. Decode each as Weak / Resist / Absorb / Nullify per the standard FF8 affinity scale. Three keys split the output: 6 = Weaknesses, 7 = Absorbs, 8 = Nullifies.
3. **v0.14.67 — Status resist (key 9).** 20 bytes at `entity+0x4C` for status ailment resistances. Format as 'Resists Sleep, Stop, ...' or 'No status resistances.'
4. **v0.14.68 — Active statuses (key 0).** Status bitfield at `entity+0x78`. Format as 'Active: Poison, Slow, ...' or 'No active statuses.'
5. **Polish.** Hidden-HP whitelist (replace soft threshold), repeat-spam suppression, ally formatting tweaks.

Then back to the broader v0.14 priority queue: persistent accessibility settings across play sessions, GF naming screen bypass for Siren, party member NPCs in field nav, X-ATMO92 chase scene accessibility, kernel.bin Blue Magic spell name parsing.

**v0.14.63 chapter (retained for reference):**

**v0.14.62 BAT result.** Scan announce works perfectly with no duplications — Aaron confirmed. But the v0.14.62 blanket battle-mode suppression silenced too much: the **"Cast Fire"-style spell-cast banner** (the text that appears at the top of the screen when a character begins casting a spell) stopped announcing. That banner rides on `Hook_show_dialog` (the universal text renderer at `FF8Addresses::show_dialog_addr`), which v0.14.62 muzzled entirely in mode 3. Aaron wants it back — along with any other battle UI text the engine renders this way (mid-battle cutscene dialog, item-use announces, etc.) — with the duplicate suppressed only when the Scan UI is actually displaying the same content.

**v0.14.63 fix.** Replace the v0.14.62 gate `currentMode == 3` with `currentMode == 3 && ScanTTS::IsScreenActive()`. `ScanTTS::IsScreenActive()` returns true only between `OnScanPopupSpawn` (first sub_B687C0 fire — the moment the scan window actually renders on screen) and `OnScanPopupDespawn` (when the player dismisses it). During that period the rendered scan text would duplicate scan_tts.cpp's auto-announce, so we suppress. Outside that period — including all other battle moments — every battle UI text speaks normally through show_dialog: Cast Fire, Cast Cure, Cast Scan, GF summon banners, mid-battle cutscene dialog, anything else the engine routes through this path.

**Sequencing safety.** `HookedScanGetText` sets `s_scanScreenActiveSlot` via `InterlockedExchange` BEFORE returning the text to the engine, and `Hook_show_dialog` reads window text AFTER calling the original (which is what triggers sub_B687C0 internally). So by the time we check `IsScreenActive()` in show_dialog's speak path, the flag is already set on the very first scan render — meaning the first show_dialog call for the scan window correctly suppresses the duplicate. No race window.

**Cleanup from v0.14.62.** The `isDrawReceived` bypass flag is no longer needed (the new gate is more permissive) and was removed. The v0.10.112 "Received <item>" rewrite ("Received 4 Blizzards" → "Squall received 4 Blizzards") still applies and now always speaks in battle since it never coincides with scan being active.

**Architectural note.** Aaron's request was to "add support for those dialogs to the battle system." Two approaches: (a) keep show_dialog as the catcher and just narrow the gate (this commit), or (b) build a parallel battle-window text renderer in BattleTTS. Approach (a) achieves the same end result (Cast Fire announces, scan duplicate doesn't) with minimal code change, and the show_dialog hook is already the correct general text-renderer hook — it's not really a "field" hook, it's a "universal text renderer" hook that just happens to live in field_dialog.cpp. If we ever want to truly separate concerns later (e.g. battle window text owned by BattleTTS module with its own dedup/style policy), that's a bigger refactor we can do then.

**Files touched:** `field_dialog.cpp` (added `ScanTTS::IsScreenActive()` forward decl + replaced gate logic), `ff8_accessibility.h` (version).

**Expected v0.14.63 BAT outcomes:**
- **"Cast Fire" / "Cast Cure" / "Cast Scan" banner announces work again** — anytime a character commits a spell cast, the banner text speaks via show_dialog. New `[SHOW_DIALOG-SPEAK]` log entries with mode=3 confirm this.
- **Scan still works cleanly with no duplicate** — SC2-PROBE auto-announce fires once, SC1-PROBE entries for the scan window text sections show up in the log accompanied by `[SHOW_DIALOG-SUPPRESS] win[X] mode=3 scan-active` lines, and Aaron hears just the auto-announce.
- **Number keys 1..4 during open Scan window still work** — unaffected (ScanTTS::SpeakField).
- **"Received <item>" draw results still announce as "<Char> received <items>"** — unaffected.
- **Field dialog (NPCs, tutorials, thoughts) still works on the field** — unaffected (gate only fires in mode 3).

**What to listen for as a regression risk.** The v0.14.62→v0.14.63 change is strictly *more permissive* in battle (only suppresses when scan is active vs always). The risk is that some other battle UI text we want suppressed gets through. Examples to watch for: any duplicate during scan that might happen if `IsScreenActive()` flips to true slightly after the first show_dialog fire (the sequencing analysis above says this won't happen, but BAT will confirm). Also: GF summon name announces, Limit Break trigger banners, victory text during the brief mode 3→mode 4 transition window.

**Diagnostic state retained.** SC1-PROBE and SC2-PROBE remain active for v0.14.63 BAT verification. After Aaron confirms both scan AND "Cast Fire" work cleanly, strip both in v0.14.64. Single-channel SAPI mode also stays.

**v0.14.62 chapter (retained for reference):**

**v0.14.61 BAT result.** Aaron heard the duplicate twice but **serialized now instead of simultaneous** — single-channel SAPI mode worked exactly as predicted (no Voice 2 = no overlap possible). The SC1-PROBE then caught the offender red-handed at the moment of the Scan announce:

```
[21:01:31] [SC2-PROBE] caller=0x6DC6E261 text='Fastitocalon. A fish that swims in the ground...'  (scan_tts auto-announce, correct)
[21:01:31] [SC1-PROBE] caller=0x6DC65684 interrupt=0 text='A fish that swims in the ground...'      (OFFENDER #1 — description)
[21:01:31] [SC1-PROBE] caller=0x6DC65684 interrupt=0 text='Fastitocalon'                              (OFFENDER #2 — name)
[21:01:31] [SC1-PROBE] caller=0x6DC65684 interrupt=0 text='LEVEL 14 HP ?????/?????'                   (OFFENDER #3 — stats with hidden-HP markers)
```

Three separate Voice 1 calls with the three text sections of the FF8 Scan UI. Caller `0x6DC65684` is `ScreenReader::Speak(const char*, bool)` — the wrapper that re-encodes UTF-8 to wchar_t. The real caller is whoever called the char* overload.

**Source identified.** `Hook_show_dialog` in `src/field_dialog.cpp` (~line 888). Comment says it was hooked at v04.17 for MODE_TUTO coverage (Squall's internal thoughts), but it fires for ALL window text including battle UI. When the Scan window renders, the engine calls show_dialog for each text section and our hook decodes + speaks each as a separate utterance via `ScreenReader::Speak(decoded.c_str(), false)`.

Note that `field_dialog.cpp` already has explicit battle-mode logic for the v0.10.112 "Received <item>" draw-result rewrite ("Received 4 Blizzards" → "Squall received 4 Blizzards"). That path needs to keep working in battle. Everything else in mode 3 is now ScanTTS / BattleTTS territory.

**v0.14.62 fix.** In `Hook_show_dialog`'s speak block, gate on `currentMode != 3` (not battle). Exception: if the decoded text is the v0.10.112 "Received <...>" pattern, allow the speak even in battle. All other battle window text gets logged as `[SHOW_DIALOG-SUPPRESS]` and dropped silently. ScanTTS already owns scan-window announces via the v0.14.60 architecture (sub_B687C0 first-fire — the same engine call that triggers these text fetches).

**Diagnostic state retained.** SC1-PROBE and SC2-PROBE remain active for v0.14.62 BAT verification. After Aaron confirms the duplicate is gone, strip both in v0.14.63. Single-channel SAPI mode also stays — separate concern from the duplicate source, and Aaron has confirmed he prefers single-channel regardless.

**Files touched:** `field_dialog.cpp` (gate `Hook_show_dialog` speak path on `currentMode != 3` with isDrawReceived exception), `ff8_accessibility.h` (version).

**Expected v0.14.62 BAT outcomes:**
- Aaron should hear the Scan announce ONCE — just the SC2 auto-announce: "Fastitocalon. A fish that swims in the ground... Press number keys 1 through 0 for details."
- No more SC1-PROBE entries with scan content (name/description/stats) immediately after the SC2-PROBE auto-announce. The battle log should show new `[SHOW_DIALOG-SUPPRESS]` entries for those text sections instead.
- "Received <item>" draw results (e.g. "Squall received 4 Blizzards") still announce correctly during battle.
- Number-key queries (1..4) during the open Scan window still work (they go through ScanTTS::SpeakField, not field_dialog).
- Field dialog (NPCs, tutorials, thoughts) still works normally on the field.

**Risk to watch for in BAT:** any other battle UI text that previously relied on `Hook_show_dialog` will now go silent. Keep an ear out for any battle dialog/announcement that used to work but doesn't in v0.14.62. If something's missing, route it through BattleTTS explicitly rather than re-enabling the field_dialog hook for battle.

**v0.14.61 chapter (retained for reference):**

**Why v0.14.61.** v0.14.60 BAT showed Aaron still heard simultaneous duplicate speech during the Scan announce, sounding *disjointed and difficult to follow*. The [SC2-PROBE] log caught only ONE SpeakChannel2 call (caller=`0x6DC8E281`, the scan_tts.cpp auto-announce path), so the second voice MUST be coming through `ScreenReader::Speak()` — which routes to `s_pVoice` (voice 1) and was NOT being probed. Aaron noted he had thought we'd disabled the dual-channel audio system, but `s_pVoice2` was still being created unconditionally in `InitSAPI`.

**v0.14.61 changes (2):**

1. **Single-channel SAPI mode.** `InitSAPI` now leaves `s_pVoice2 = nullptr` and skips its `SpMMAudioOut` allocation. `SpeakChannel2` falls back to `s_pVoice` (voice 1), so all speech goes through one voice and SAPI's per-voice queue serializes everything — simultaneous overlap is mechanically impossible. **Trade-off:** `SpeakChannel2(text, interrupt=true)` now purges voice 1's queue, potentially cutting off menu/command speech mid-utterance. Aaron has confirmed he prefers this over overlapping speech.

2. **SC1-PROBE diagnostic.** Added to `ScreenReader::Speak(const wchar_t*, bool)` mirroring the SC2-PROBE pattern: logs caller `_ReturnAddress()` and 75-char text preview as `[SC1-PROBE] caller=0xXXXXXXXX interrupt=N text='...'` in `ff8_mod.log` every call. With single-channel mode active, simultaneous overlap can't occur, but the SC1-PROBE will tell us WHICH path was redundantly speaking the scan announce on voice 1 (and let us fix the duplicate at its source in v0.14.62).

**Files touched:** `screen_reader.cpp` (skip voice 2 creation + SC1-PROBE), `ff8_accessibility.h` (version).

**Expected v0.14.61 BAT outcomes:**
- Aaron should hear the announce ONCE, not overlapping with anything else. May still hear two announces sequentially if both Speak and SpeakChannel2 are being called for the same content (queued one after the other on the single voice).
- `Logs/ff8_mod.log` should show: ONE `[SC2-PROBE]` for the auto-announce at window-open time, AND POSSIBLY ONE OR MORE `[SC1-PROBE]` lines somewhere around the same time. The SC1-PROBE caller addresses + text content will identify the source(s) of voice 1 calls so we can target the duplicate at its real origin in v0.14.62.
- Voice 2 init log line should now read "SAPI voice 2 SKIPPED (v0.14.61 single-channel mode — SpeakChannel2 falls back to voice 1)" instead of "SAPI voice 2 (event channel) initialized."

**v0.14.60 chapter (retained for reference):**

**Why v0.14.60.** v0.14.59 BAT (2026-04-30 19:36) appeared to PASS in the battle log but Aaron heard the announce TWICE: once at start of cast animation (full content with "Press number keys") and once when the actual window appeared (just name + description, no "Press number keys"). Confirmed via `Logs/ff8_mod.log` AudioDucker which showed two distinct duck windows: 19:36:33-39 (~6s, first announce) and 19:36:42-47 (~5s, mystery second speech). The battle log only logged ONE `[SCAN-TTS] Auto-announce` line at 19:36:33; the second speech has no corresponding log line, so its source is unknown as of v0.14.59 BAT review. Note the SECOND duck starts at exactly 19:36:42 — the moment `[SCAN-HOOK] sub_B687C0 fire #1` fires, which is when the engine actually renders the Scan UI on screen.

**v0.14.60 architecture (5 changes).**

1. **Announce trigger MOVED from popup-spawn to sub_B687C0 first-fire.** v0.14.59 fired the announce on `[SPRITE-POLL] NEW kind=0x06 val=50` in `battle_tts_screenshot.inl` — that popup spawns at action-commit (~9 seconds before the window opens visually). v0.14.60 moves the trigger to `HookedScanGetText` count==1 in `scan_tts.cpp` — sub_B687C0 fires when the engine actually reads scan text for the UI render. This matches what the player sees on screen.
2. **Per-scan reset of `s_scanHookFireCount`.** `OnScanCast(_, true)` (action-layer) now sets the counter to 0 so the next sub_B687C0 fire is counted as #1 of THIS scan, not the cumulative count across all scans this battle. `OnBattleEnter` also resets it.
3. **`battle_tts_screenshot.inl` SPRITE-POLL NEW path no longer calls `OnScanPopupSpawn`.** The DESPAWN edge still calls `OnScanPopupDespawn` because the popup record stays alive for the full UI session and only despawns when the player dismisses the window.
4. **Cosmetic: Strip trailing periods from the description.** `BuildAutoAnnounce` now trims trailing `.` (and trailing whitespace) from `snap.description` before appending its own period, fixing the `"may be a shark.. Press number keys"` double-period in v0.14.59 BAT.
5. **Diagnostic: SC2-PROBE in `ScreenReader::SpeakChannel2`.** Logs every call with caller `_ReturnAddress()` and a 75-char text preview as `[SC2-PROBE] caller=0xXXXXXXXX interrupt=N text='...'` in `ff8_mod.log`. If v0.14.60 BAT still shows two AudioDucker duck windows during a Scan, the SC2-PROBE log lines will identify the second caller's return address (mappable back to source by subtracting the dinput8.dll load base from the caller address). Strip the probe in v0.14.61+ once root cause is found or the duplicate disappears.

**Files touched:** `scan_tts.h` (architecture comments), `scan_tts.cpp` (architecture + cosmetic), `battle_tts_screenshot.inl` (remove popup-spawn trigger), `screen_reader.cpp` (SC2-PROBE + `<intrin.h>` include), `ff8_accessibility.h` (version).

**Expected v0.14.60 BAT outcomes:**
- Aaron should hear the announce ONCE, when the actual Scan window opens (~9 seconds after pressing the cast confirm button), not at start of cast animation.
- Description should NOT have the double period.
- Battle log: `[SCAN-TTS] Action-layer fire (silent; pending announce on first sub_B687C0 fire)` at cast confirm → `[SCAN-HOOK] sub_B687C0 fire #1 slot=N (window-open trigger — announcing now)` at window open → `[SCAN-TTS] Auto-announce` immediately after.
- Mod log: ONE `[SC2-PROBE]` line for the auto-announce. If a SECOND `[SC2-PROBE]` line appears, the caller address is the smoking gun.
- Number keys 1..4 should still work (Name / Description / Level / HP). 5..0 still respond `Not implemented yet.`.
- After window despawn, 1/2/3 should revert to ally HP.

**If v0.14.60 BAT still has the duplicate**: the SC2-PROBE log identifies the second caller. Most likely candidates: a hook in `battle_tts_victory.inl` (BT1-BT8 — these only speak when game `mode == 4` per source review, but worth checking the SC2-PROBE caller against their hook addresses), or some path in field_dialog/menu_tts I haven't traced. Less likely: SAPI internal queue replay (Windows-level bug, no fix from us).

**v0.14.59 chapter design (retained for reference):** Architecture replaces v0.14.50–58's "announce-at-action-commit" with a clean two-stage UX: (1) the action-layer (popup hook in noeffect.inl, magicId==39 in ewm.inl) silently captures a per-slot ScanSnapshot {name, level, HP, monster_id, description} into `s_scanCache[slot]` and sets `s_pendingScanSlot` — NO speech. (2) One frame later, the existing `[SPRITE-POLL] NEW` emitter in screenshot.inl detects the `kind=0x06 val=50` popup record and calls `ScanTTS::OnScanPopupSpawn`, which speaks `<Name>. <Description>. Press number keys 1 through 0 for details.` on Channel 2 and sets `s_scanScreenActiveSlot`. While the screen is active, `PollHPCheckKeys` in battle_tts_hp.inl routes 1..0 to `ScanTTS::SpeakField` for live querying; outside the window 1/2/3 retain ally-HP behavior and 4..0 are silent no-ops. On `[SPRITE-POLL] DESPAWN`, `OnScanPopupDespawn` clears `s_scanScreenActiveSlot` and number keys revert. Snapshot cache is retained across despawn for re-scan support within the same battle. The 30-second action-layer lock from v0.14.57 is RETIRED (silent action-layer = no purpose); the sub_84F860 dispatcher hook from v0.14.54 is RETIRED (full-view-only — popup-spawn is universal across full and compacted views per v0.14.55+ analysis); the sub_B687C0 text-fetch hook stays installed as vestigial defense-in-depth, forwarding to a no-op for paths the action-layer doesn't see (Doomtrain Scan-effect etc.). Fields 5..0 (Stats / Weak / Absorb / Nullify / StatusRes / ActiveStatus) reply `Not implemented yet.` and land in v0.14.60..63.

**Recent chapter (v0.14.50 → v0.14.58) — closed.** Action-layer + dispatcher hooks landed announce timing right but description was never read and Aaron heard duplicates from the TARGET-ACTIVE redundancy. Chapter pivots to v0.14.59 UX redesign rather than further tuning.

**Recent chapter (v0.14.50 → v0.14.58) condensed:**
- v0.14.50 announcement first slice (Name + Level + HP). PASS, spurious 'No effect' watchdog discovered.
- v0.14.51–52 watchdog cancel + sub_B687C0 hook. Caught Draw-Cast / full-view only.
- v0.14.53–54 diagnostics + dispatcher hook on sub_84F860. Confirmed BOTH UI hooks miss compacted view.
- v0.14.55 action-layer detection via popup hook. Build FAIL: `.inl` namespace trap created `BattleTTS::ScanTTS::OnScanCast`.
- v0.14.56 namespace fix. BAT: 'No effect' gone, but two announcements per Scan.
- v0.14.57 30 s action-layer lock. C2668/C2572 fixes. BAT PASS three Scans → three announces.
- v0.14.59 UX redesign: silent action-layer + popup-spawn auto-announce + interactive 1..0. Keys 1..4 wired; 5..0 stub for v0.14.61..64. Lock + dispatcher hook retired. BAT 2026-04-30 19:36 reported PASS in battle log but Aaron heard the announce TWICE; AudioDucker pattern in mod log confirmed. Chapter pivots to v0.14.60.
- v0.14.60 architectural fix: announce trigger moved from popup-spawn (cast-commit, ~9s early) to sub_B687C0 first-fire (window-render). Per-scan reset of s_scanHookFireCount. Cosmetic: trailing-period strip in BuildAutoAnnounce. Diagnostic: SC2-PROBE in SpeakChannel2 to find the mystery second speaker if the duplicate persists. AWAITING BAT.

---

**Immediate next session priorities (in order):**

1. **v0.14.60 BAT review.** Cast Scan on an enemy. Listen for ONE announce when the window actually opens (not at start of cast animation). Check `ff8_battle.log` for the new flow: `[SCAN-TTS] Action-layer fire (silent; pending announce on first sub_B687C0 fire)` → `[SCAN-HOOK] sub_B687C0 fire #1 slot=N (window-open trigger — announcing now)` → `[SCAN-TTS] Auto-announce` (no double period). Check `ff8_mod.log` AudioDucker for ONE BeginDuck/EndDuck pair during the cast (not two), and ONE `[SC2-PROBE]` line from the auto-announce. If a SECOND `[SC2-PROBE]` line appears unexpectedly, capture its `caller=0xXXXXXXXX` value and find the dinput8.dll load base (e.g. via Process Explorer) to map back to source. Number keys 1..4 should still work and revert to ally HP after window close.
2. **If v0.14.60 BAT PASS — strip the SC2-PROBE diagnostic** (it's noisy in mod log) and proceed to v0.14.61.
3. **v0.14.61 — Stats (key 5).** Read STR/VIT/MAG/SPR/SPD/EVA/HIT/LUCK at `entity+0xB5..0xBC`. Speak the three FF8 displays as DEF/INT/DEX (= VIT, MAG, SPD).
4. **v0.14.62 — Elemental affinity (keys 6/7/8).** 8×u16 at `entity+0x3C`. Order Fire/Ice/Thunder/Earth/Poison/Wind/Water/Holy. Buckets `<800` Weak (key 6), `=900` Nullify (key 8), `≥1000` Absorb (key 7).
5. **v0.14.63 — Status resist (key 9).** 20 bytes at `entity+0x4C`. Order from deep research. Threshold `≥100 (0x64)` = "Strong vs". Validate via Cactuar (Death-resistant) and Malboro.
6. **v0.14.64 — Active statuses (key 0).** Read status bitfield at `entity+0x78`. Reuse target-announce status decoder.
7. **v0.14.65 — Polish.** Hidden-HP whitelist (Fastitocalon-F, Adel, Sorceress A/B/C, Griever, Helix, Ultimecia — read whitelist out of `cmp eax, ?` chain in sub_84F860 at mod load). Repeat-spam suppression. Ally formatting (skip descriptions for allies). Strip diagnostic `[SCAN-HOOK]` `[SCAN-DISP]` `[SCAN-DEDUP]` `[SCAN-LOCK]` logs. Resolve TARGET-ACTIVE redundant announce.
7. **Follow-up bug — TARGET-ACTIVE redundant announce.** Both `[TARGET] Entry` and `[TARGET-ACTIVE]` speak the same name within 1–6 s for every targeted spell. Gate active to skip if same (slot, status_mask) was announced as entry within 5 s. Touchpoint: `[TARGET-ACTIVE]` emitter in `battle_tts_helpers.inl`. Roll into v0.14.64 polish if time permits.
8. Persistent accessibility settings across play sessions.
9. Verify GF naming bypass — Siren failed in earlier testing.
10. Remove party members from entity catalog.
11. X-ATMO92 chase scene accessibility.
12. Boko Choco / Minimog / Moomba / Gilgamesh VTTs.
13. FF8 in-game config "Scan: Long/Short" forcing — investigate whether mod can flip the option to Long automatically.
14. Push v0.14.49+ to GitHub once Scan chapter is stable (~50 builds unpushed).

**Audio mixing chapter shipped (v0.14.45 → v0.14.48):** SFX volume control, full keyboard layout reshuffle, AudioDucker module with per-bus dB-based config and reference-counted BeginDuck/EndDuck, BAT-validated tuning at BGM -10 dB / SFX -15 dB / 800 ms hold. Pushed to `main` 2026-04-29 06:02 UTC at commit `afa0972`. Audio-mixing tuning playbook and per-channel SFX ducking escalation path retained in NEXT_SESSION_PROMPT.md history if future tuning needed.

---

**On the horizon**

- **GitHub issue #8 (independent SFX volume)** — resolved by v0.14.45/v0.14.46, push landed; close it on next issue triage.
- Boko Choco / Minimog / Moomba / Gilgamesh VTTs (extension of v0.14.44 GF AD)
- Per-GF AD timing tuning based on continued in-game listening
- World map GitHub issues: vehicle-aware BFS, guided GPS mode, auto-announce location names, TERRAIN-DIAG cleanup
- Battle command menu architecture (tabbed detection), cancel/back re-announce, Magic sub-menu scroll offset for >4 spells
- Draw menu "???" spell reveal issue
- Quistis Blue Magic spell-list ordering investigation
- Bug 3 from v0.14.31 BAT — Magic/GF submenu auto-announce inconsistent (may already self-resolve; retest first)
- Bug 4 from v0.14.31 BAT — number key 2 announced GF Shiva instead of Squall HP (edge case, lower urgency)

---

**Key learnings & principles**

**CRITICAL — bash vs filesystem MCP view mismatch:** When working on this project, bash sees `/C:/...` paths that look like the OneDrive folder but are actually a separate container-local filesystem. The `create_file` system tool writes there too. Files Aaron's build will see ONLY come from filesystem MCP `write_file` / `edit_file` at `C:/...` (no leading slash). DO NOT use `create_file` for project files. DO NOT use bash for project files. Use filesystem MCP exclusively.

**CRITICAL — SET3 hook permanently disabled:** NEVER re-enable the SET3 opcode hook (opcode 0x1E). ANY interception — MinHook, dispatch table patch, or minimal passthrough wrapper — hangs the infirmary scene (Dr. Kadowaki walk freeze). GitHub Actions CI check in `.github/workflows/safety-checks.yml` guards against accidental re-enablement.

**CRITICAL — FFNx-replacement detection is NOT universal (v0.14.46):** The BGM hook pattern (detect `0xE9` at game function entry → resolve FFNx target → MinHook the FFNx side) only works for functions FFNx unconditionally replaces. For functions FFNx replaces conditionally (e.g. `sfx_set_master_volume` only when `use_external_sfx=true`), the byte stays as the original game prologue and the detection returns silently. **Lesson: when hooking a game-side audio/render function, prefer MinHook on the game address directly.** MinHook trampolines either prologue (original or `E9 JMP`); calls through the trampoline reach whatever code is currently installed there. Sidesteps the FFNx-config dependency entirely.

**CRITICAL — sfx_set_master_volume volume range (v0.14.46):** Game function at `0x0046A390` expects volume **0–100, not 0–127**. Instruction `cmp eax, 0x64; jbe 0x46a3cc` rejects values >100 into a non-update error path. BGM (`set_midi_volume`) is 0–127. Don't reuse the 127 scaling.

**CRITICAL — MSVC name-mangling:** Forward declarations of namespaced functions across translation units MUST exactly match return type. MSVC encodes return type in the symbol name (`?Speak@ScreenReader@@YAX...` for void vs `YA_N...` for bool). A `void Speak` forward decl in one .cpp + `bool Speak` definition in another = unresolved external. When fixing linker errors involving cross-namespace forward decls, always grep for ALL inline decls of the function and unify them.

**CRITICAL — `.inl` files are included INSIDE `namespace BattleTTS {`** (v0.14.55 trap, fixed v0.14.56): cross-namespace forward declarations placed inside an `.inl` file resolve as nested. `namespace ScanTTS { void OnScanCast(int); }` written inside `battle_tts_noeffect.inl` becomes `BattleTTS::ScanTTS::OnScanCast` because the `.inl` is `#include`d inside `namespace BattleTTS {` in `battle_tts.cpp`. The linker error reads `unresolved external symbol "void __cdecl BattleTTS::ScanTTS::OnScanCast(int)"` — a different symbol than the `::ScanTTS::OnScanCast` defined in `scan_tts.cpp`. Cross-namespace forward decls must live in the parent `.cpp` BEFORE the `namespace BattleTTS {` opens.

**CRITICAL — default argument values can appear only ONCE per translation unit** (v0.14.57 C2572): when a header decl already provides `bool foo = false` and an in-file forward decl coexists in the same TU, the in-file decl must OMIT the default. The header version applies to all callers regardless. Same rule across multiple decls in the same TU: only the first may carry the default.

**CRITICAL — cdecl(byte) engine functions leave garbage in upper bits of ECX** (v0.14.57 BAT, fixed v0.14.58): when an engine call site does `mov cl, byte ptr [...]; push ecx; call func`, only the low byte of ECX is meaningful — the upper 24 bits are whatever was in ECX before. The called function typically `and eax, 0xFF` after reading `[esp+4]`, so the engine doesn't notice. MinHook callbacks declared `int slotIndex` see the full dword and pass garbage values like `0x648C5483` to downstream code. Always mask `slotIndex & 0xFF` before using as a slot index. Pattern observed at `sub_B687C0` call site `0x0084F958`. Audit any future cdecl(byte) hooks for this.

**CRITICAL — popup hook as action-layer cue** (v0.14.55+): `sub_48D200` (HookedPopupSprite) fires for every battle popup. Filter by `text_id == 0x06 && (value & 0xFF) == spell_id` to detect a specific spell cast at action-commit time — reliable across Magic-menu / Draw-Cast / Magic-Stock paths and view modes. v0.14.55 uses this for Scan (value=0x32 = ID 50). The popup hook writes a tick to a `volatile LONG` via `InterlockedExchange`; downstream consumers read-and-clear via `InterlockedExchange(&tick, 0)`. Pattern reusable for any spell that needs an action-layer cue independent of UI rendering.

**CRITICAL — Build recovery hook-install gotcha:** When rebuilding a .cpp file from an older GitHub HEAD and re-wiring newer .inl files into the include chain, ALSO audit `OnBattleEnter()` (and equivalent lifecycle entry points) for missing `*Install()` and `*Reset()` calls AND `Update()` for missing `Poll*()` calls. The .inl include alone is insufficient; the lifecycle wiring must be explicit. Audit checklist for every future build recovery: (a) every `Install*` function defined in any newly-wired .inl must have a corresponding call in the lifecycle entry; (b) every `Reset*` function must have a corresponding call in the reset block; (c) every `Poll*` function must have a corresponding call in `Update()`.

**Action ID at 0x01D27AE3 is NOT 0x16 for player magic:** The v0.13.83 noeffect.inl comment claimed `arg[1]==0x16 (magic action ID)` for the sub_48E830 hook gate. v0.14.34 BAT proved this WRONG: actual actionId for Sleep cast was 0x01. The 0x16 value in `[CMD] cmds=[0x14,0x15,0x16]` is the Draw command-menu index, NOT the action staging byte. Future filtering of sub_48E830 hits should NOT use 0x16 as a gate.

**SAVEMAP OFFSET CORRECTION:** Deep research assumes savemap header is 96 bytes (0x60). CONFIRMED header is 76 bytes (0x4C). All post-header offsets from deep research are 0x14 (20 bytes) too high. Subtract 0x14. Confirmed base: `0x1CFDC5C`. GFs at +0x4C, chars at +0x48C, Gil at +0x08 (header). Include this correction in all future deep research prompts about FF8 savemap/menu data.

**Interactive object positions:** PSHN_L literals in target entity init scripts (SETLINE/SET3/TALKRADIUS). SETLINE center override works for SETLINE-triggered entities. Shift-pattern fallback is ~494 units off. Director pattern is redundant dead code per deep research.

**Victory TTS:** MUST hook text renderer, NOT read memory addresses. Memory dumps all info at once — player blindly presses through multiple unannounced screens. Hook text pipeline to detect current victory phase, announce per-phase as each screen renders. Do NOT pivot to memory scanning.

**EWM design model:** Enhanced Wait Mode retrofits FF8 into sequential turn-based — only ONE action/menu occurs at a time. ATB still races normally; whoever fills first goes first (no advantage, same economy as vanilla). During ANY action, ALL other ATB freezes. Preserve: (1) first-to-fill acts first, (2) no skipped turns, (3) natural ally/enemy ratio.

**Damage announcement timing (v0.14.10):** Two parallel triggers wired into `PollHPChanges`. Production trigger is the sub_5068B0 render hook (impact-time, ~62ms after anim-up); fallback is the v0.13.90 anim-flag-fall trigger. Whichever fires first wins via `s_popupSpawnTriggered` flag. The render hook MUST be installed in `OnBattleEnter()` via `DmgRenderHook_Install()` — without it, only the anim-flag-fall fallback fires, producing the OLD ~13s-late timing.

**FFNx replaces ATB writes:** FFNx (not the original engine) writes GF loading counter values. The game's own code is a red herring — must hook FFNx's replacement function found by scanning for signature `B9 16 F0 CF 01 66 89 06`.

**Analog steering:** World-space headings must be projected onto calibrated camera axes (measured via `lX`/`lY` test injection at field start). Direct world-space mapping only works on axis-aligned camera fields.

**Walkmesh:** 47.5% of FF8 fields have disconnected walkmesh islands. FF8 uses inline vertex format (uint32 numTriangles, then N×24 bytes inline vertex data, then N×6 bytes neighbor data). Full walkmesh JSON at project root.

**Reusable diagnostic:** OpenGL screenshot capture. Only `glReadPixels` via SwapBuffers hook works — PrintWindow/BitBlt/screen DC all return black. See `HookedSwapBuffers/DoGLCapture/CaptureScreenshot` in `battle_tts.cpp`. Requires `gdiplus.lib+opengl32.lib`.

**Blue Magic auto-build (v0.14.22):** Auto-building scanner eliminates manual spell collection via signature matching + runtime address discovery. Preserves spell ID mappings (0x92="Laser Eye", 0xAA="Ultra Waves") to maintain proper UI ordering. Works with ANY Blue Magic spell Aaron learns, zero maintenance.

**Known issue:** JAWS intercepts game keys (arrows, Backspace) until user presses Insert+3 for passthrough. NVDA does not have this issue. Not a mod bug. Low priority.

---

**Approach & patterns**

**SESSION CHECKPOINT RULE:** To prevent progress loss when Claude session limits hit unexpectedly, update DEVNOTES.md and NEXT_SESSION_PROMPT.md at TWO checkpoints: (1) every time a new build version is bumped for Aaron to test, and (2) after every BAT (Built and Tested) result. Treat these updates as part of the version-bump and BAT workflows, not optional end-of-session work.

**Session startup ritual:** At the START of every new session, Claude MUST read both `DEVNOTES.md` and `NEXT_SESSION_PROMPT.md` using filesystem tools before doing any work. Read `DEVNOTES_HISTORY.md` only when tracing past decisions. Keep DEVNOTES under 10KB — move completed investigations to HISTORY.

**Build/test workflow:** Aaron says "BAT" = "Built and Tested." Claude should check `Logs/build_latest.log` tail for errors, then game log (`Logs/ff8_mod.log` or domain-specific: `ff8_field.log`, `ff8_battle.log`, `ff8_menu.log`, `ff8_world.log`, `ff8_dialog.log`) for runtime results. When a build error occurs, immediately read `Logs/build_latest.log` before attempting fixes.

**Default to writing code:** Once an approach is decided, write code directly. Avoid re-reading transcripts and re-summarizing instead of implementing — Aaron has explicitly corrected this pattern. If unsure between two approaches, pick the simpler one and commit; iterate from BAT results, not from speculation.

**Version bump — 1 location only:** `FF8OPC_VERSION` in `ff8_accessibility.h`. `field_navigation.cpp` and `battle_tts.cpp` headers say "See FF8OPC_VERSION" and their `Initialize()` logs use the macro via `%s` format. Format: `0.MM.BB` pre-production, `1.0.0` first public.

**Build system:** `deploy.vbs` in project root launches `src/deploy.ps1` which runs `src/deploy.bat`. All build scripts live in `src/` except the `.vbs` launcher. Update `src/deploy.bat` when adding/removing source files.

**Deep research protocol:** When source code, game files, and mod logs are insufficient, ask Aaron to perform deep research using ChatGPT. Claude provides the exact prompt. Save prompts to `Plan & Research Documents/`.

**Function key repurposing rule (generalized from F12 rule):** Before assigning a new behavior to F1–F12, grep ALL source files for existing `VK_F{n}` references and remove the stale ones. Diagnostics from old sessions hide in `.inl` files and survive long after DEVNOTES says "unused". Specific instances caught: v0.12.21 F2 "Director Varblock" diagnostic in `field_nav_handlekeys.inl`; v0.12.22 F12 POPM_W reset block in `field_nav_fieldscripts.inl`. Both removed in the v0.14.45 keyboard rebind. Search for ALL the symbols, not just the literal `VK_F{n}` — supporting state variables (e.g. `s_varWriteCount`) live elsewhere and break the build if their reset is missed.

**Mid-file .asm read:** When bash unavailable and .asm file too big for head/tail, use `filesystem:edit_file` with `dryRun=true`. Chain anchors using trailing lines from previous result.

**Stable catalog ordering:** Entity catalog order must be stable — only changes when entities appear/disappear, never reorders by distance. Blind players track visited entities by position.

---

**Tools & resources**

**CRITICAL — filesystem tools only for project files:** Mod files are on Windows. ALWAYS use filesystem MCP tools (`read_text_file`, `edit_file`, `write_file`, `search_files`, etc.) for ALL project file access. NEVER use bash for project files — bash runs in a separate Linux container that cannot access the Windows mod directory. Bash is only useful for text processing on tool results already in context.

**Key source files:**
- `src/ff8_accessibility.h` — version define
- `src/mod_forward_decls.h` — cross-module namespace forward declarations
- `src/field_navigation.cpp` + 13 `.inl` files (48KB core)
- `src/battle_tts.cpp` + 18 `.inl` files including helpers, diagnostics, hp, ewm, menu, sprite, status, noeffect, sprite_spawn, validate, dmgbp, dmg_popup_hook, dmg_read_bp, dmg_render_hook, spritepool, roi_calib, screenshot, victory
- `src/scan_tts.h` / `src/scan_tts.cpp` — Scan spell TTS (v0.14.50–57 chapter); `OnScanCast(slot, fromActionLayer)` + two-tier dedup (lock + quiet window); MinHooks on `sub_B687C0` (full-view text fetch) and `sub_84F860` (UI dispatcher)
- `src/menu_tts.cpp` + `.inl` files
- `src/game_audio.cpp` / `.h` — BGM + SFX + ducking-toggle
- `src/field_archive.cpp` / `field_archive_jsm.inl` — JSM scanner
- `src/dinput8.cpp` — main hook entry; keyboard input block

**Log files:** `Logs/build_latest.log`, `Logs/ff8_mod.log`, `Logs/ff8_field.log`, `Logs/ff8_battle.log`, `Logs/ff8_menu.log`, `Logs/ff8_world.log`, `Logs/ff8_dialog.log`. Auto-archived to `Logs/archive/` on next build start.

**Reference files in mod directory:**
- FFNx canary source: `FFNx-Steam-v1.23.0.182\Source Code\FFNx-canary\src\` (read-only, for address offsets and struct layouts)
- Game files: `Game Files\FINAL FANTASY VIII\`
- Full FF8_EN.exe disassembly: `Game Files/disassembly/`
- Walkmesh JSON: `ff8_walkmeshes.json` (project root, 17MB, all 894 fields)
- Session docs: `DEVNOTES.md`, `NEXT_SESSION_PROMPT.md`, `DEVNOTES_HISTORY.md` (project root)
- Research docs: `Plan & Research Documents/`
- Kernel extraction tools: `extract_kernel.ps1`, `kernel_analysis.txt` (project root)

**FFNx key hooks for dialog:** `opcode_mes` (0x47 in dispatch table), `field_get_dialog_string` (called from `opcode_mes+0x5D`), `set_window_object`, `ff8_win_obj` windows array, `opcode_ask` (0x4A), `world_dialog_assign_text_sub_543790`. These are in FFNx `src/ff8_data.cpp`.

**FFNx key hooks for audio:** BGM = `set_midi_volume` at game-side, FFNx unconditionally replaces with JMP to its `set_music_volume_for_channel` which calls `nxAudioEngine.setMusicVolume`. SFX = `sfx_set_master_volume` at `0x0046A390`, FFNx replaces conditionally on `use_external_sfx=true`. v0.14.46 hooks the game function directly with MinHook regardless of FFNx state — works for both `use_external_sfx` modes. Volume range: BGM 0–127, SFX 0–100.

**SFX address resolution chain:** `pExecuteOpcodeTable[0x21]` → +0x5F `sfx_play_to_current_playing_channel` → +0x35 `play_sfx_on_channel` → +0xA1 `sfx_set_volume`; `sfx_get_master_volume = sfx_set_volume - 0x10`; `sfx_set_master_volume = sfx_get_master_volume - 0xE0`; `pMasterSfxVolume` = absolute@+0x1 of `sfx_get_master_volume`. Resolved values: `pSfxSetMasterVolume = 0x0046A390`, `pMasterSfxVolume = 0x01CD1794`.

**Keyboard shortcuts (v0.14.46):** `` ` `` = repeat dialog/battle event | V = mod version | F1 = cycle voice | F2 = toggle audio ducking (Phase 1 announce-only) | F3/F4 = speech rate down/up | Shift+F3/F4 = speech volume down/up | F5/F6 = SFX volume down/up | F7/F8 = BGM volume down/up | F9/F10 = field nav | F11 = menu summary (Shift=monitor, Ctrl=dump) | F12 = diagnostic builds only | G/T/L/R = Gil/Time/Location/SeeD | `/` = help bar | O = EWM toggle | 1/2/3/H = battle HP check.

**GitHub:** `ampage87/FFVIII-Accessibility-Mod`, main branch. GitHub Sponsors enabled. Push utility at `Utilities/push_to_github.vbs`. ~50 builds unpushed (v0.13.63 → v0.14.46+).
