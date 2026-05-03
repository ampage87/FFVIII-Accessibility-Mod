# Next Session Prompt — v0.14.75 BAT'd. Push backlog + carry deferred work.

## Status

**v0.14.75 BAT PASSED.** Aaron deployed and verified all keyboard shortcuts work as expected:
- F11 → on-demand screenshot capture in any game state.
- M (in mode 6) → menu summary (party / HP / Gil / time / location).
- F12 → silent (reservation confirmed).
- All other hotkeys unchanged from v0.14.74.x.

Production build ready. F11/F12 keybinding ownership is settled.

## Priority 1: GitHub push of unpushed build chain

Per userMemories recent_updates: ALWAYS verify GitHub state with `github:list_commits` BEFORE quoting a backlog size. Do NOT trust earlier session claims.

Last known reference point: GitHub `main` HEAD was commit `337cf97a` = v0.14.72 (2026-05-02 19:31 UTC). Builds shipped since then (all BAT'd):

- v0.14.73 (element affinity scale fix attempt — wrong)
- v0.14.73.1 (corrected to FF8 800-anchored u16)
- v0.14.74 (8-key Scan layout overhaul)
- v0.14.74.1 (SCAN-STRUCT diagnostic; keys 9/0 reverted to stub)
- v0.14.74.2 (cross-battle stale magicId fix)
- v0.14.74.3 (cross-battle stale enemy NAME fix)
- v0.14.74.4-diag (F12 = on-demand screenshot)
- v0.14.75 (keybinding refactor: F11 = screenshot, M = menu summary, F12 freed)

After verifying the gap, push the chain. Commit messages should reference the corresponding DEVNOTES section entries for traceability.

## Priority 2: GitHub issue #27 — SeeD Rank misreads as "No rank yet"

Filed during the v0.14.75 BAT session. Pressing **R** in the in-game menu always announces "No SeeD rank yet" even after Aaron has earned a rank.

**URL:** https://github.com/ampage87/FFVIII-Accessibility-Mod/issues/27
**Labels:** `bug`, `menu-tts`, `savemap-offsets`, `low-priority`

Suspected cause: `FIELD_H_OFFSET = 0xF94` in `src/menu_tts_hotkeys.inl::AnnounceSeedRank()` is computed by stacking section sizes — likely one is off (similar to the SAVEMAP OFFSET CORRECTION lesson where ChatGPT's deep research repeatedly assumed a 96-byte header instead of the actual 76-byte one).

Fix approaches in the issue:
1. Add a temporary diagnostic that scans the savemap for a uint16 matching Aaron's known SeeD EXP value (he reads it off the in-game SeeD menu, we hunt for that exact value). Once located, hardcode.
2. ChatGPT deep research targeted at FF8 Steam 2013 savemap SeeD rank EXP offset, with the SAVEMAP OFFSET CORRECTION note in the prompt.
3. Verify the level-from-EXP table — `seedExp / 100` clamped 1-31 may not match the engine's actual rank curve (which uses a lookup).

Low priority — does not block any core gameplay system.

## Priority 3: v0.14.74.1 [SCAN-STRUCT] three-enemy BAT — longest-pending Scan task

Aaron scans Grat / T-Rexaur / Tonberry (canonical status profile differences) and uploads `Logs/ff8_battle.log`. Diff the `[SCAN-STRUCT]` log sections to find the 20-byte run where the patterns line up. Currently `BENT_STATUS_RESIST_BASE = 0x4C` is wrong (alternating 169/251 byte pattern proved it). Once correct offset is found, next build re-enables Scan keys 9 (Status Resistances) and 0 (Status Weaknesses).

After this lands, the Scan UX chapter is functionally complete (compacted-view solved by Config setting in v0.14.74.4-diag; auto-announce + 8 keys working; only status keys 9/0 remain).

## Other deferred priorities

1. Persistent accessibility settings across play sessions (TTS rate, volumes, EWM toggle, audio-ducking toggle, etc.).
2. Verify GF naming bypass (Siren failed in earlier testing).
3. Remove party members from field entity catalog.
4. X-ATMO92 chase scene accessibility.
5. Walk-and-talk dialog gap (hardcoded engine path).

## Mandatory session-start ritual

Read `DEVNOTES.md` and this file before doing any work. `DEVNOTES_HISTORY.md` only when tracing past decisions. Keep DEVNOTES under 10 KB; move completed investigations to HISTORY when they age out.

## Lessons accumulated this chapter (for next memory-pruning pass)

userMemories is at 30/30 capacity. Next time we have a slot:

1. **"Before writing engine-behavior workarounds, check FF8's Config menu for an existing toggle."** v0.14.74.4-diag's first F12 capture revealed a `Scan: Once / Always` toggle that closed the entire compacted-view chapter without writing fallback code. The Config menu is the engine's own user-facing knob for behavior toggles; it's the FIRST place to check before writing mod code to neutralize an engine behavior.

2. **"Verify backlog sizes via `github:list_commits` before quoting."** Earlier session notes stated "~80-build backlog" which was wildly wrong (actual gap was 6). Always check GitHub state live, never quote from memory.

3. **`menu_tts_diagnostics.inl` contains a USER FEATURE despite the filename** — `AnnounceMenuSummary` is the menu open summary, not a diagnostic. When auditing files for cleanup, don't trust filenames as authority; read the function purposes.

4. **Computed offsets that stack section sizes are fragile.** `AnnounceSeedRank`'s `FIELD_H_OFFSET = 0xF94` was built by adding header + GFs + chars + shops + limit_breaks + items. A miscount in any section produces a plausible-looking offset that reads from the wrong region. For any savemap field beyond the header, prefer offsets verified at runtime against a known live value rather than computed from section-size constants.

## Files in current state

- `src/dinput8.cpp` — F11 = on-demand screenshot.
- `src/menu_tts.cpp` — M = menu summary (in mode 6).
- `src/field_nav_handlekeys.inl` — VISDIAG body deleted.
- `src/field_navigation.cpp` — `s_f11WasDown` deleted.
- `src/ff8_accessibility.h` — version `0.14.75` with comprehensive changelog.
- `DEVNOTES.md` — top sections: v0.14.75 BAT result + v0.14.75 keybinding refactor; v0.14.74.4-diag below it.
- `NEXT_SESSION_PROMPT.md` — this file.
