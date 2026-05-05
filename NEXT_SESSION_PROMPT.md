# Next Session Prompt — v0.14.82 PASSED BAT. Ready to push backlog to GitHub.

## Where we are

**v0.14.82 PASSED BAT cleanly.** Aaron scanned three enemies (Bite Bug Spirit=3, Glacial Eye Spirit=101, Caterchipillar Spirit=18) and all three announces matched predictions exactly:

- Bite Bug: "Highly vulnerable to Death, Poison, Petrify, Darkness, Silence, Berserk, and Zombie. **Vulnerable to Sleep.**"
- Caterchipillar: "Highly vulnerable to Death, Poison, Petrify, Darkness, Silence, Berserk, and Zombie. **Vulnerable to Sleep.**"
- Glacial Eye: "Vulnerable to Death, Poison, Petrify, Darkness, Silence, Berserk, and Zombie." (Sleep at 32% correctly silent)

Fastitocalon wasn't re-scanned this round but isn't affected by the threshold change — Sleep at 12% stays silent under both 60% and 50% cutoffs, and that case was already validated in v0.14.81 BAT.

**The Scan TTS announce design is now fully stable.** Five-build saga (v0.14.78 → v0.14.82) closed cleanly. The full progression:

- v0.14.78 introduced byte-only weakness tiering. PARTIAL pass (popup-detection bug exposed).
- v0.14.79 fixed popup-hook detection (defensive OR on text_id 0x02 || 0x06). PASSED.
- v0.14.80 added symmetric Resists / Immune to tiers on key 9. PASSED, but exposed Sleep-on-Fastitocalon design gap.
- v0.14.81 switched from byte-only to chance-based weakness tiering using `Magic/4 - Spirit/4 + 100 - byte` formula. PASSED, but 60% cutoff dropped three canon vulnerabilities.
- v0.14.82 relaxed cutoff 60% → 50% to capture them. PASSED. Done.

GitHub `main` HEAD is still v0.14.75 (`a2bfc253`). v0.14.76 + v0.14.77 + v0.14.78 + v0.14.79 + v0.14.80 + v0.14.81 + v0.14.82 are unpushed. **Verify exact backlog via `github:list_commits` before quoting.**

## Priority 1: Push v0.14.76 through v0.14.82 to GitHub

Use `github:list_commits` to confirm the exact unpushed backlog before quoting numbers (don't repeat the v0.14.72 "~80-build backlog" guess error). Then push the seven local builds with their full changelog comment blocks (already embedded in `src/ff8_accessibility.h` history).

## Priority 2: Resume deferred priorities

In order from session memory:

1. **Persistent accessibility settings across play sessions** — TTS rate, volumes, EWM toggle, audio-ducking toggle, etc. This is the longest-running deferred priority. Likely the next major feature.
2. **Remove party members from field entity catalog**
3. **X-ATM092 chase scene accessibility**
4. **Walk-and-talk dialog gap** (hardcoded engine path)

## Priority 3: Optional polish (low priority)

- Remove redundant Scan branch from `battle_tts_ewm.inl::PollBattleMagicId` (genuinely redundant now that v0.14.79 popup hook reliably catches both first and repeat scans — produces duplicate `[SCAN-CACHE]` log noise on first scan in each battle).
- Update stale comment at top of scan_tts.cpp line 26 ("Fields 5..0 reply Not implemented yet.") to reflect v0.14.74+ field bindings.

## Priority 4: GitHub issue #27 — SeeD Rank misreads as "No rank yet"

Filed during the v0.14.75 BAT. https://github.com/ampage87/FFVIII-Accessibility-Mod/issues/27 — labels `bug` / `menu-tts` / `savemap-offsets` / `low-priority`. Suspect: `FIELD_H_OFFSET = 0xF94` in `AnnounceSeedRank()` is a stacked-section-size computation; likely off by 0x14 per the SAVEMAP OFFSET CORRECTION lesson.

## Priority 5: DEVNOTES rotation (overdue maintenance)

DEVNOTES.md continues to grow. Multiple completed investigations should be moved to `DEVNOTES_HISTORY.md`:

- v0.14.45 audio-ducking implementation
- v0.14.50–62 Scan TTS architecture chapter
- v0.14.65–70 scan UI render hooks + frame-delay screenshot machinery
- v0.14.71–72 BT-HOOK conflict resolution
- v0.14.74.x stale-data fingerprint fixes
- v0.14.75 keybinding refactor
- **v0.14.76–82 popup-hook + threshold-tiering saga** — NOW STABLE. Collapse the seven-build sequence into one summary paragraph: "Closed multi-build investigation into Scan status weakness/resistance announces. v0.14.76–79 fixed popup-hook detection (text_id 0x02 || 0x06 defensive OR). v0.14.80 added symmetric Resists/Immune to tiers on key 9. v0.14.81 switched weakness tiering from byte-only to chance-based using `Magic/4 - Spirit/4 + 100 - byte` (assumed Magic=30); thresholds 95% Highly vulnerable / 60% Vulnerable / silent. v0.14.82 relaxed Vulnerable to 50% after canon cross-reference. All five PASSED BAT. Lesson 4 in this file documents the Spirit-dependent inflict formula; Lesson 7 documents the 'natural more-likely-than-not boundary' threshold-tuning principle."

Rotation is a focused 30-minute task best done when not blocked. Recommended for the start of a quieter session.

## Files in current state

- `src/scan_tts.cpp` — `FormatStatusWeaknesses` is the v0.14.82 chance-based version with 50% Vulnerable cutoff and `ComputeMagicCastChance` helper. `FormatStatusResistances` is the v0.14.80 two-tier (Resists / Immune to). All BAT-validated.
- `src/battle_tts_sprite.inl`, `src/battle_tts_screenshot.inl` — v0.14.79 popup-hook fixes (defensive OR on text_id 0x02 || 0x06) intact.
- `src/battle_tts.h` — `BENT_STATUS_RESIST_BASE = 0x80` (BAT-validated since v0.14.77).
- `src/ff8_accessibility.h` — version `0.14.82` with full root-cause comment.
- `DEVNOTES.md` — top section: v0.14.82 PASSED summary. Below: v0.14.81 PASSED, v0.14.80 PASSED, v0.14.79 PASSED, etc. **Rotation overdue.**
- `NEXT_SESSION_PROMPT.md` — this file.
- GitHub: `main` HEAD = `a2bfc253` (v0.14.75); v0.14.76 through v0.14.82 unpushed.

## Lessons accumulated for next memory pruning pass

1. **Always verify popup signatures directly from a [POPUP] log entry** before changing popup-hook conditions (v0.14.79).
2. **The FF8 vanilla Scan UI does NOT display status weakness/resistance** — only stats and elemental affinities. Threshold tiering is entirely our design choice.
3. **Sleep=50 universal across early enemies is REAL FF8 design** — the Quistis junction tutorial baseline.
4. **Status inflict % depends on BOTH byte AND target Spirit (for magic casts) or Vitality (for ST-Atk-J).** Direct cast: `Magic/4 - Spirit/4 + 100 - byte` (StatusAttack fixed at 200, StatusDefense = 100 + byte).
5. **Trust direct log evidence over prior changelog claims** when they conflict.
6. **Test design assumptions in actual gameplay AND against community canon.** v0.14.81 log output matched predictions perfectly, but canon cross-reference exposed the threshold gap that v0.14.82 fixed.
7. **Conservative thresholds lose information.** When in doubt about a threshold for a player-facing classification, the natural "more likely than not" boundary (50%) is usually the right default.

## Mandatory session-start ritual

Read `DEVNOTES.md` and this file before doing any work. `DEVNOTES_HISTORY.md` only when tracing past decisions. Update both files at every version bump and after every BAT result.
