# Next Session Prompt — Chapter 3: GitHub push of the world-map saga (v0.14.91)

## Where we are

**Chapter 2 (auto-drive + animation-byte suppression) is BAT-confirmed complete.** v0.14.90.3 BAT (Tue 05/05 18:12, Chocobo drive Balamb-Garden→Balamb-Town across two pause/resume cycles) confirmed:

- Zero spurious `Vehicle change:` announcements during post-battle world-map re-entries.
- `[WM-ENTRY-DEBOUNCE]` log lines fire correctly: initial entry → `Snapshot baseline locomotion=0`; second re-entry → `Window expired with non-canonical locomotion=9; keeping s_lastVehicle=31` (graceful fallback).
- Real Chocobo mount mid-drive announced correctly (mode 31, single announcement).
- AD arrival fired with refined-coord capture (12861,-26829).
- No spurious BFS catalog rebuilds during the suppression windows.

The world-map regression-fix → BFS reachability filter → auto-drive → animation-byte suppression saga is functionally complete.

## Priority 1: GitHub push (v0.14.90.3)

GitHub `main` HEAD is at `0b06ab1` (v0.14.90.2, pushed Tue 2026-05-05 20:56 UTC). Local has only ONE version unpushed: **v0.14.90.3** (today's BAT-passed WM-ENTRY-DEBOUNCE suppression hotfix). Chapter 2 itself was already pushed in the v0.14.90.2 commit; v0.14.90.3 is just the animation-byte suppression patch on top.

**Verify exact backlog with `github:list_commits` before quoting any number** — the rule earned its keep on Tue 2026-05-05 when memory and DEVNOTES had v0.14.82 cached as HEAD.

Claude does NOT push to GitHub. Aaron has a small utility he uses for pushes himself — Claude's role is to provide the version and the consolidated commit description, Aaron handles the push.

Version: `0.14.90.3` (no header bump needed; already at this value and BAT-passed).

The v0.14.90.3 commit description is in the conversation history (and can be re-derived from DEVNOTES and the v0.14.90.3 BAT log if needed).

## Priority 2: Optional polish (after the push lands)

In rough priority order:

1. **Pre-battle byte noise.** v0.14.90.2 BAT log showed a single spurious `Vehicle change: Ship` ~1s BEFORE each battle entry. Smaller magnitude than the post-entry case (one announcement, not three) but still noise. Most-likely fix: gate `CheckVehicleChange` on `s_driveActive` (skip announce + rebuild when AD is running). Real vehicle actions during AD are vanishingly rare. Confirm pattern still present in v0.14.90.3 BAT log first.

2. **Deferred re-check after non-canonical window expiry.** The v0.14.90.3 BAT showed one `Window expired with non-canonical locomotion=9` line — the byte hadn't settled to canonical at exactly 3s. Current behavior keeps the prior `s_lastVehicle` (correct). Polish: add a 1-2s deferred re-check that polls again and updates `s_lastVehicle` if the byte settles to canonical in that window. Low priority because user-facing behavior is already correct.

3. **Vehicle-change-triggered catalog rebuild verification.** If Aaron mounts/dismounts mid-session without entering/exiting world map (e.g. Ship at FH dock), the catalog rebuilds via the v0.14.85.2/3 rule-class-change detection. Confirm by mounting a Ship in a future session.

4. **Locomotion enum reconciliation.** `GetVehicleName` in `world_map.cpp` currently uses both legacy (0/1/2/3/4) and research-doc (6/10/31/32) values. v0.14.90.3 BAT empirically confirmed Chocobo=31. Continue empirical reconciliation as gameplay surfaces other modes.

5. **DEVNOTES rotation.** DEVNOTES is at ~210KB (over the 10KB target). Move v0.14.50–v0.14.90.x Scan TTS chapter and v0.14.83–v0.14.90.3 world-map saga to `DEVNOTES_HISTORY.md` after Chapter 3 lands.

## Priority 3: Older deferred priorities (after world-map fully closes)

1. Persistent accessibility settings across play sessions.
2. Remove party members from field entity catalog.
3. X-ATM092 chase scene accessibility.
4. Walk-and-talk dialog gap (hardcoded engine path).

## Priority 4: GitHub issue #27 — SeeD Rank misreads as "No rank yet"

Unchanged. https://github.com/ampage87/FFVIII-Accessibility-Mod/issues/27 . Hypothesis: `FIELD_H_OFFSET = 0xF94` in `AnnounceSeedRank()` is a stacked-section-size computation with one wrong section size. Investigation deferred.

## Files in current state (v0.14.90.3 BAT-confirmed)

- `src/world_map.cpp` — v0.14.90.3 with `WM_ENTRY_DEBOUNCE_MS = 3000ms` suppression in `CheckVehicleChange`. BAT-PASSED.
- `src/ff8_accessibility.h` — version `0.14.90.3` (will bump to `0.14.91` for the GitHub push).
- `src/scan_tts.cpp` — v0.14.82 chance-based weakness tier (50% Vulnerable cutoff). Stable.
- `src/battle_tts_sprite.inl`, `src/battle_tts_screenshot.inl` — v0.14.79 popup-hook fixes intact.
- `src/battle_tts.h` — `BENT_STATUS_RESIST_BASE = 0x80` (BAT-validated since v0.14.77).
- `DEVNOTES.md` — top section reflects v0.14.90.3 BAT pass + Chapter 3 pivot.
- `NEXT_SESSION_PROMPT.md` — this file.
- GitHub: `main` HEAD = `0b06ab1` (v0.14.90.2, pushed Tue 2026-05-05). Local v0.14.90.3 = ONE version unpushed (the WM-ENTRY-DEBOUNCE suppression hotfix).

## Mandatory session-start ritual

Read `DEVNOTES.md` and this file before doing any work. `DEVNOTES_HISTORY.md` only when tracing past decisions. Update both files at every version bump and after every BAT result.

## Lessons from this chapter (carry-forward to memory pruning pass)

1. **Animation-residue byte noise can mimic real engine state for hundreds of milliseconds.** v0.14.90's 4-poll (~64ms) debounce only catches frame-scale transients. The world-map re-entry animation cycles canonical locomotion values for ~1s each — indistinguishable from a real player-initiated vehicle change purely on hold-time. The v0.14.90.3 fix uses 'we just entered the world map' as the discriminator; the only signal that genuinely separates animation from gameplay.
2. **Distance-based arrival sidesteps mode-register timing races.** v0.14.90 tried to read `pGameMode` at the moment of `IsOnWorldMap` flipping false; the register hadn't transitioned yet. v0.14.90.2 replaced it with `s_driveLastDist < 1500` at exit — robust to engine-internal state-machine timing because we're using AD's own tracked distance, not querying engine state at a race-prone moment.
3. **Refined-coord empirical capture works as designed.** v0.14.89's parallel `s_refinedX/Y/Has` table records the player's last-known position before world-map exit when the exit qualifies as arrival. v0.14.90.2/3 BAT confirmed: Balamb Town's catalog center is (13249,-26779) but the actual on-the-ground entry trigger fires at (12861,-26829), a small offset; second visit steers directly to the refined coord. Skipping the deep-research trigger-table investigation in favor of empirical capture proved the right tradeoff.
4. **Recovery-from-build-damage gotcha (v0.14.84 chapter).** When recovering a `.cpp` from an older GitHub HEAD, audit the lifecycle wiring (Install, Poll, Reset hook calls) AND audit feature parity against past BAT logs — the v0.14.31 recovery only restored what triggered linker errors. Documented in DEVNOTES_HISTORY.
5. **Non-canonical byte at window expiry is OK if we keep the prior baseline.** v0.14.90.3 BAT surfaced one `Window expired with non-canonical locomotion=9` case. The mechanism handled it correctly by keeping the prior `s_lastVehicle=31`. The lesson: when we don't know what a transient byte means, doing nothing (keep last-known-good) is almost always safer than committing the unknown.
