# NEXT SESSION PROMPT — FF8 Accessibility Mod (Session 77)

## Current Build: v0.13.61 (GitHub-synced, ready for session 77 item 2)

## Session 77 Goals (4 items)

Aaron has picked four targets for this session. Work through them in order; GitHub sync should happen first so subsequent work commits cleanly on top of a synced tree.

### 1. Sync recent changes to GitHub

Sessions 75–76 produced v0.13.57 → v0.13.60 (cap→freeze ATB sandwich, dispatch hooks on `sub_483470`/`sub_482F80`, turn-counter diagnostic, format-string OOB fix). Everything is local — nothing pushed yet.

**Claude pushes directly via the GitHub MCP tools** — `github:push_files` for multi-file commits, `github:create_or_update_file` for single files. No local git required. Repo: `ampage87/FFVIII-Accessibility-Mod`, branch `main`.

**Workflow at session start:**

1. Call `github:list_commits` on `ampage87/FFVIII-Accessibility-Mod` main to find the last synced commit. Commit message + date tells us what's already up there.
2. Identify files changed since last push. Sessions 75–76 almost certainly touched:
   - `src/ff8_accessibility.h` (version bump — and confirm it matches v0.13.60)
   - `src/battle_tts.cpp` and/or `battle_tts_*.inl` (ATB sandwich, dispatch hooks, turn counter)
   - Possibly `DEVNOTES.md`, `DEVNOTES_HISTORY.md`, `NEXT_SESSION_PROMPT.md`
   - Ask Aaron if anything else changed that isn't obvious (new files, build config, etc.)
3. Read each file via `filesystem:read_text_file`, then batch them into one `github:push_files` call so they land as a single commit.
4. Skip: log files (`Logs/*.log`), per-session F12 diagnostics if still lingering, any build artifacts.

**Before pushing:** bump `FF8OPC_VERSION` in `ff8_accessibility.h` from `0.13.59` → `0.13.61` and update `FF8OPC_VERSION_DATE` to session 77's date. Aaron's decision: skip 0.13.60 in the header so everything is consistent going forward (session 77 starts coding on top of the sync commit anyway). Include the header bump in the sync push as a single atomic commit.

**Suggested commit message:**

```
v0.13.57–0.13.61: resolve damage/command-menu overlap, add turn counter

- v0.13.57: switch ATB sandwich from cap-at-max-1 to true freeze
  (eliminates converge-at-max-1 condition causing simultaneous dispatches)
- v0.13.58: per-slot turn counter (initially coupled to EWM_UpdateBattle)
- v0.13.59: split turn counter into lifecycle-hooked functions for
  reliable per-battle reset/summary
- v0.13.60: format-string OOB read fix (cosmetic, never shipped in header)
- v0.13.61: version-string catch-up; baseline for session 77 work

A/B tested across 10 G-Soldier battles: party:enemy turn ratio
2.25:1 (EWM ON) vs 2.33:1 (EWM OFF) — within 3.5%. Confirms EWM
has no measurable effect on turn economy.
```

### 2. Battle status-ailment detection & announcement

**Two behaviors required:**

- **(a) Transition announce** — when any party member or enemy gains a status, speak it on Channel 2 (e.g. "Squall poisoned", "Ifrit silenced"). Should also announce on *cure* ("Squall no longer poisoned") so the player knows when an Esuna/Remedy landed. Debounce so re-application of an already-active status doesn't re-announce.
- **(b) Status-on-read** — extend the existing `1`/`2`/`3` HP-check handler and the enemy-targeting announcement so the active status list is appended. Example: "Squall, 1450 of 2100 HP, poisoned and silenced."

**Status list (FF8 has 22 battle statuses, roughly):** Death, Petrify, Darkness (Blind), Poison, Petrifying, Slow, Stop, Confusion, Drain, Berserk, Float, Zombie, Curse, Doom, Low HP, Regen, Protect, Shell, Reflect, Aura, Haste, Sleep, Silence + some field-only ones. Exact list + bit order comes from kernel.bin.

**Research pointers (need to verify at session start):**

- Battle character struct: party actors live in the battle actor array. Status flags are typically a 32-bit or 64-bit field inside the actor struct. Look for FFNx references in `FFNx-Steam-v1.23.0.182/Source Code/FFNx-canary/src/ff8_data.cpp` — search for "status", "ailment", or hex constants around the known actor base.
- Known battle addresses from `ff8_accessibility.h`/`ff8_addresses.h`: activeChar at `0x01D76844`, menuPhase at `0x01D768D0`. The actor array base should be findable via cross-references from these.
- Disassembly lookups: grep `FF8_EN_callxrefs.txt` for sites that read/write suspected status-field offsets (often a `mov` to/from a word at `+0x...` inside a battle-tick function).
- Kernel.bin has status-name strings; if they're not easily reached we can hard-code a 22-entry status-name table in C++.

**Implementation approach:**

- Add `battle_status.inl` (new file) included from `battle_tts.cpp`.
- Per-frame polling: read status bitfield for each of 3 party slots + up to 8 enemy slots; cache last-known in `s_statusPrev[11]`; on any bit flip, queue an announcement.
- Gate announcements through Channel 2 (`SpeakChannel2`) with `interrupt=false` so they don't clobber active damage TTS.
- Extend `battle_tts_screenshot.inl` or wherever the `1`/`2`/`3` HP-check lives to call a new `FormatStatusList(slot)` that returns a comma-joined string of active statuses.
- Same `FormatStatusList` used for enemy-target announce (find the existing target-cursor hook in `battle_tts.cpp`).

### 3. GF summon audio descriptions

Mirror the existing FMV audio-description pipeline (shipped in v03.00) for GF summons. Each of FF8's 16 GFs has a distinctive animation — a blind player currently hears nothing meaningful during the entire summon sequence.

**Trigger detection:** `s_gfAnimFired` already latches when a GF summon starts. `s_gfHpTracking` already tracks which GF. Both are in `battle_tts.cpp`. The description should fire once per summon, at animation start.

**GF roster (16):** Quezacotl, Shiva, Ifrit, Siren, Brothers (Sacred + Minotaur), Diablos, Carbuncle, Leviathan, Pandemona, Cerberus, Alexander, Doomtrain, Bahamut, Cactuar, Tonberry, Eden.

**Approach decision to confirm with Aaron at session start:**

- **Option A (fast):** TTS descriptions — Aaron writes one paragraph per GF (e.g. Shiva: "A woman of ice materializes, raises her arms, and a cascade of icicles rains down on the target."), stored in a C++ `const` map, spoken via `SpeakChannel2`. Shippable in one session. Iteratable later.
- **Option B (polished):** Pre-recorded audio files played via DirectSound or SAPI-to-WAV pipeline. More work; better production value.
- Recommend starting with Option A, then upgrading individual GFs to recorded audio if/when Aaron wants.

**Implementation scaffold:**

- Add `gf_descriptions.inl` with a `GF_ID → description` map.
- In the existing GF-summon detection path, after `s_gfAnimFired` latches, call `SpeakChannel2(GetGFDescription(gfId), /*interrupt=*/true)`.
- Provide a user setting to toggle off (some players may not want it after the first view). Reuse the `Config::` API that handles the EWM toggle.

### 4. Scan spell formatted output

Scan currently spills the raw enemy-info text through the existing dialog hook. That text is a wall of fragments ("Weak: Fire,Ice Absorb: Lightning …") and not navigable or well-structured for a screen reader.

**Target output format** (to confirm with Aaron):

> "Scan: Ruby Dragon. Level 48. 20480 of 20480 HP. 1500 of 1500 MP. Weak to ice. Absorbs fire. Halves lightning. Immune to earth, water, poison. Status immunities: sleep, stop, silence, slow, berserk, confusion. Vulnerable to: blind, curse. Note: a dragon that lives in the Island Closest to Hell."

**Research pointers:**

- The Scan screen is a specific battle sub-menu. Find its opening function via the existing battle-menu dispatch (same area that handles Magic/GF/Item submenus — these live around `0x1D768D0` function-ptr territory).
- Enemy struct has `u8 elem_weak`, `u8 elem_absorb`, `u8 elem_immune`, `u8 elem_halve` bitfields (8 elements: Fire, Ice, Thunder, Earth, Poison, Wind, Water, Holy). Status affinity similarly bitfielded.
- Enemy Note text: sourced from kernel.bin or the battle scene file. May share a codepath with the existing dialog hook — worth checking whether the note text flows through `set_window_object` already.
- FFNx `ff8_data.cpp` likely has the enemy-struct offsets documented near its scan-related hooks.

**Implementation approach:**

- Hook the Scan window open (not the existing generic dialog read — that fires too early and in wrong context).
- Read the targeted enemy's struct directly; build the output string from raw affinity bitfields rather than parsing the rendered on-screen text.
- Suppress or replace the raw-text read-out when formatted version fires, so the player doesn't hear both.

**Nice-to-have:** a key to re-speak the last Scan result (mirroring the `` ` `` dialog-repeat key).

## Active EWM Subsystem (do not break)

- ATB freeze sandwich in `HookedATBUpdate` — POST-FREEZE restores to exact pre-sandwich value
- Dispatch hooks on `sub_483470` and `sub_482F80` — block during damageOrActionActive or activeChar<3
- Post-turn grace (1000 ms) + post-action cooldown (500 ms) — bridges game-thread/mod-thread race windows
- Turn-count diagnostic — EWM-independent, logs per-battle summary
- Damage-anim transition diagnostic — `[DMG-DIAG]`, `[FRZ-DIAG]`, `[POST-REL]` log tags

All stable after v0.13.59. Changes to any of these need to preserve the invariants in `DEVNOTES.md`.

## Session Ritual Reminder

Read `DEVNOTES.md` and this file before any work. Consult `DEVNOTES_HISTORY.md` only when tracing past decisions. Keep `DEVNOTES.md` under 10 KB.

**BAT protocol:** When Aaron says "BAT," immediately read `Logs/build_latest.log` tail for compiler errors, then the relevant domain log (`ff8_battle.log` for items 2/3/4) for runtime results.

**File access:** ALWAYS use filesystem MCP tools for project files. Bash runs in a Linux container and cannot reach the Windows source tree.

**F12 rule:** Reserved exclusively for per-session diagnostic builds. Before adding any new F12 diagnostic this session, search for existing `VK_F12` references and remove old diagnostic code first.

**Version:** session 77 starts at `0.13.61` post-sync (see item 1). Bump the minor on the first real code change after sync (`0.13.62` for item 2 opener).

**Copyright / IP note:** Any Scan "Note" text displayed in-game is Square Enix IP. Describe/paraphrase in tooling or TTS when possible; speaking the exact on-screen text verbatim to the player is fine (they'd see it otherwise) but the audio-description scripts Aaron writes for GFs should be original wording, not quoted from official materials.
