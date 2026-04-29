# Next Session Prompt — FF8 Accessibility Mod

## Current state at session start

Build: **v0.14.48 — audio ducking BAT PASSED. Audio mixing chapter complete.**

The full audio mixing system shipped over v0.14.45 → v0.14.48: SFX volume control, full keyboard layout reshuffle (F1 voice / F2 ducking-toggle / F3+F4 speech rate / Shift+F3+F4 speech vol / F5+F6 SFX vol / F7+F8 BGM vol), reusable AudioDucker module with per-bus depth/attack/release/hold config, reference-counted BeginDuck/EndDuck, smoothed one-pole envelope, dB-based config. Aaron BAT'd v0.14.48 with BGM -10 dB / SFX -15 dB / 800 ms hold and confirmed GF audio descriptions are now clearly audible during summon animations.

50+ builds remain unpushed to GitHub. Last push was `7845f0c` = v0.14.43.1. This session's priority is consolidating the backlog into a single comprehensive push.

---

## Top priority — GitHub push consolidation

Tag: `v0.14.48`. Use `Utilities/push_to_github.vbs` (the GUI wrapper). Commit message draft is at the end of this file in the **Push package** section.

After the push lands, close **GitHub issue #8** (independent SFX volume) — resolved by v0.14.45/v0.14.46.

---

## Tuning playbook (cheat sheet for future audio tweaks)

All four knobs live in `GameAudio::Initialize` in `src/game_audio.cpp`, in the `bgmCfg` and `sfxCfg` blocks:

```cpp
bgmCfg.depthDb   = -10.0f;   // depth: less negative = lighter duck
bgmCfg.attackMs  = 100.0f;   // higher = slower duck-down (gentler entry)
bgmCfg.releaseMs = 600.0f;   // higher = slower recovery (smoother exit)
bgmCfg.holdMs    = 800.0f;   // higher = bridges longer gaps in TTS
```

dB → linear cheat: -3 dB ≈ 70%, -6 dB ≈ 50%, -10 dB ≈ 32%, -12 dB ≈ 25%, -15 dB ≈ 18%, -18 dB ≈ 13%, -20 dB ≈ 10%.

**If a specific scene exposes a long in-flight SFX sample (GF roar etc.) masking TTS:** escalate to per-channel SFX ducking. The single master only updates new SFX cleanly; in-flight samples ride out the duck. The escalation path:

- `FF8Addresses::pSfxSetVolume` is already resolved (per-channel setter, game-side).
- Channel array at `0x01cd0b00` with stride `0x24` (verified in v0.14.46 disassembly walk).
- Implementation: inside `ApplySfxVolume(float)` in `src/game_audio.cpp`, after the existing master write, iterate active channels and call `pSfxSetVolume(channel, scaledVolume)` for each. The ducker doesn't need to know.

If future tuning becomes a frequent activity, promote depth/attack/release/hold to dedicated INI keys. The current obstacle is `Config::GetInt` uses `GetPrivateProfileInt` which doesn't handle negative values cleanly — a small wrapper that treats the int as a signed dB value (-30..0 range) would unblock that.

---

## After GitHub push lands

Carried backlog:

1. Persistent accessibility settings across play sessions (general — beyond the volume/duck keys, which already persist via `ff8_accessibility.ini`).
2. Verify GF naming bypass — Siren failed in earlier testing.
3. Remove party members from entity catalog.
4. X-ATMO92 chase scene accessibility.
5. Boko Choco / Minimog / Moomba / Gilgamesh VTTs (extension of v0.14.44 GF AD).
6. Per-GF AD timing tuning based on continued in-game listening.
7. Bug 3 from v0.14.31 BAT — Magic/GF submenu auto-announce inconsistent (may already self-resolve from v0.14.32+ timing fixes; retest first).
8. Bug 4 from v0.14.31 BAT — number key 2 announced GF Shiva instead of Squall HP (edge case, lower urgency).
9. Quistis Blue Magic spell-list ordering investigation.
10. Draw menu "???" spell reveal issue.
11. World map: vehicle-aware BFS, guided GPS mode, auto-announce location names, TERRAIN-DIAG cleanup.
12. Battle command menu architecture (tabbed detection), cancel/back re-announce, Magic sub-menu scroll offset for >4 spells.

---

## Required reading at session start

1. `DEVNOTES.md` (project root)
2. This file
3. `Logs/build_latest.log` tail — confirm any new build is clean
4. `src/audio_ducker.h` / `.cpp` for the module API if any audio work is on deck
5. `src/game_audio.cpp` `Initialize` block for current ducker constants

## Workflow rules in effect

- **Filesystem MCP tools only** — never bash for project files. Bash sees `/C:/...` which is a separate container-local filesystem; the system `create_file` tool writes there. Real Windows files must use `filesystem:write_file` / `filesystem:edit_file` at `C:/...` (no leading slash).
- **Update DEVNOTES + NEXT_SESSION_PROMPT** at every version bump and BAT.
- **"BAT" = Built and Tested.** Check `Logs/build_latest.log` tail first, then domain log.
- **Version bump in 1 location** — `FF8OPC_VERSION` in `src/ff8_accessibility.h`.
- **Aaron is blind** — every response starts with `## Claude Says`.
- **Don't declare a fix successful from log markers alone** — verify against Aaron's user-facing experience.
- **NEVER re-enable the SET3 opcode hook (0x1E)** — hangs infirmary scene. CI guard active.
- **F12 reserved for diagnostics** — search/remove old VK_F12 refs before adding new. Search for supporting state variables too, not just the literal `VK_F{n}`.
- **When hooking a game-side audio/render function**, prefer MinHook on the game address directly rather than detecting an FFNx JMP — sidesteps the FFNx-config dependency that broke v0.14.45 SFX (see DEVNOTES "FFNx-replacement detection is NOT universal").
- **Default to writing code** once an approach is decided. Avoid re-reading transcripts and re-summarizing instead of implementing. If torn between two approaches, pick simpler, commit, iterate from BAT results.

---

## Push package (drafted end of session 78)

**Tag:** `v0.14.48`

**Commit title:** `v0.14.48 — Audio mixing system, GF audio descriptions, battle/items submenu fixes, damage timing fix`

**Commit body:**

```
Comprehensive backlog push covering v0.13.63 → v0.14.48 (~50 internal builds).

AUDIO MIXING (v0.14.45 → v0.14.48)
- New AudioDucker module (src/audio_ducker.{h,cpp}): domain-agnostic, per-bus
  configurable depth/attack/release/hold, reference-counted BeginDuck/EndDuck,
  smoothed one-pole envelope with frame-rate-independent coefficient, dB-based
  config converted to linear gain internally.
- F2 toggles ducking on/off (persisted to ff8_accessibility.ini).
- BGM ducks -10 dB, SFX ducks -15 dB during TTS, with 800 ms hold to bridge
  menu-nav gaps. GF audio descriptions are now clearly audible during summon
  animations.
- F5/F6 SFX volume control (new); F7/F8 BGM volume control (moved from F3/F4).
- F3/F4 speech rate; Shift+F3/F4 speech volume (full keyboard layout reshuffle).
- SFX volume hook installs game-side at 0x0046A390 directly with MinHook,
  works regardless of FFNx use_external_sfx config.
- Resolves issue #8 (independent SFX volume).

GF AUDIO DESCRIPTIONS (v0.14.44)
- New module gf_audio_desc.{cpp,h} mirroring fmv_audio_desc architecture.
- 18 VTTs covering all 16 junctioned GFs (Quezacotl, Shiva, Ifrit, Siren,
  Brothers, Diablos, Carbuncle, Leviathan, Pandemona, Cerberus, Alexander,
  Doomtrain, Bahamut, Cactuar, Tonberry, Eden) plus Phoenix and Odin.
- Trigger fires from PollBattleMagicId() rising edge on battle_magic_id.
- Channel 2 playback, stops cleanly on revert (covers natural end + R1+L1
  skip).

BATTLE ITEMS SUBMENU (v0.14.35 → v0.14.42)
- Fixed filtered-mode data lookup: FF8 has separate inventory orderings for
  the all-items menu vs. the battle items menu, configured independently in
  the field menu. Reads from s_turnItemList[] for battle.
- Fixed visual page/slot announcement via boIdx tracking.
- Comprehensive items submenu architecture documented after deep research.

DAMAGE TIMING (v0.14.10 → v0.14.37)
- Production trigger restored to sub_5068B0 render hook (impact-time, ~62 ms
  after anim-up). Announces sync with visible damage sprite paint.
- Popup-create hook (sub_48EF80) demoted to diagnostic only — fires 1–5
  seconds before visible damage in v0.14.36 BAT.
- Anim-flag-fall trigger remains as catch-all fallback.

BUILD RECOVERY (v0.14.24 → v0.14.34)
- Comprehensive hook-install audit codified: every Install/Reset/Poll
  function in newly-wired .inl files needs explicit lifecycle wiring in
  OnBattleEnter/Update.
- 7 dev-only diagnostic functions identified as orphaned (dead code, no
  user impact).
- New mod_forward_decls.h to unify cross-module namespace declarations
  (MSVC name-mangling lesson).

OTHER
- Removed v0.12.21 stale F2 "Director Varblock" diagnostic from
  field_nav_handlekeys.inl during keyboard rebind.
- Removed v0.12.22 F12 POPM_W diagnostic state from field_navigation.cpp
  and field_nav_fieldscripts.inl.
- Generalized "Function key repurposing rule": before remapping any F-key,
  search ALL source files for VK_F{n} references AND supporting state
  variables. Stale diagnostics hide in .inl files.
- DEVNOTES + NEXT_SESSION_PROMPT trimmed; obsolete content moved to
  DEVNOTES_HISTORY.md as needed.
- GitHub push utility added at Utilities/push_to_github.vbs (session 73).
```
