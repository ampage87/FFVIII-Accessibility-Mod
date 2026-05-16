**Purpose & context**

Aaron is the sole developer of the FF8 Accessibility Mod — a `dinput8.dll` injection for Final Fantasy VIII Steam 2013 (App ID 39150, FF8_EN.exe + FFNx v1.23.x) that makes the game playable for blind players via Windows SAPI TTS, navigation assistance, and entity catalogs. Aaron is blind and uses NVDA as his screen reader, and is also the primary tester.

**Project root:** `C:/Users/ampag/OneDrive/Documents/FFVIII-Accessibility-Mod/FF8_OriginalPC_mod/`

GitHub: `ampage87/FFVIII-Accessibility-Mod`. **HEAD = v0.15.10.2** (pushed 2026-05-15, commit `4ef5b61a`). **Local tree = v0.15.11.0, BAT-confirmed 2026-05-15, ready to push.**

---

## Current state: v0.15.11.0 BAT clean, awaiting Aaron's push

Aaron BAT'd v0.15.11.0 across multiple battles 2026-05-15 ~19:00 — no regressions across item drops, enemy names, GF level-up, or ability-learn TTS. Single battle in `ff8_battle.log` (19:00:42-19:00:58) hit every previously-suspect code path cleanly:

- **`[BT5-EA30] mode=4 a1=154 -> "Fish Fin"`** + **`[BT6-EA90] ... -> "Fish's fin"`** + **`[VICTORY-TTS] Item (EA30): Received 1 Fish Fin. Description: Fish's fin`** — item name + description + announce TTS all decode and compose cleanly through canonical.
- **`[ABILITY-NAME-HOOK] sub_47E710: a1=85(0x55) -> "SumMag+30%" hex=[57 73 6B 51 5F 65 31 24 21 2B]`** — confirms the inline ability decoder migration in HookedBtCandidate8 works. The hex stream contains 0x24, which the retired preview decoder would have mapped to `'0'`; canonical correctly recognizes the byte position and produces the intended `"SumMag+30%"`.
- **`[GF-NAME-HOOK] sub_47E970: a1=0x42 -> "Ifrit"`** + **`[VICTORY-TTS] Ability: GF Ifrit learned SumMag+30%.`** — GF name capture + composed ability TTS coherent.
- **`[BTXT] #1 ... -> " 0123456789ABCDEF:/',"`** — the engine's glyph-cache string passed through canonical with the correct digit mapping; this would have been garbled by preview's off-by-3 digit range.
- **`[VICTORY-TTS] EXP Phase 1: Zell and Selphie received 144 EXP. Squall received 0 EXP.`** + **`EXP Phase 2: Zell has 856 EXP to reach level 20...`** — EXP path unchanged, strstr-based capture still firing.
- No `{XX}` unknown-byte markers in any `[BTXT]` / `[VAREXP]` log line.

Aaron's BAT report: "Fought several battles and didn't notice any problems with how item drops, enemy names, and so forth were announced." Ready for push via `Utilities/push_to_github.vbs`.

After push: refresh DEVNOTES.md / NEXT_SESSION_PROMPT.md to idle-state, then Aaron picks the next backlog item.

---

## Recently shipped

### v0.15.11.0 (BAT-confirmed 2026-05-15 19:00, ready for push)

Finish unifying the FF8 text decoders. Three coordinated changes (full detail in CHANGELOG.md):

1. **Canonical augmentation** in `src/ff8_text_decode.cpp` + `.h` — added 0xFA `"EC"` and 0xFD `"FE"` compression sequences (gaps relative to the preview's v0.13.46 sysfnt.bin table); changed 0x0E (icon code) from `return 0` to `return 1` so the icon ID byte is consumed silently instead of leaking into the next decoded char. Preview's small Fire/Magic icon-name translation table is not preserved (it covered only 2 of many icons; cleaner to lose icon names entirely).
2. **Migrated all 7 `DecodeFF8TextPreview` call sites** in `src/battle_tts_victory.inl` to `DecodeFF8String`: BT1 `[BTXT]` diagnostic + EXP-text strstr capture (pure ASCII, decoded identically); BT4 `[VAREXP]` diagnostic; BT5 fallback (dropped — canonical handles every case preview did); BT5 first + second item-description decodes (player-facing); BT6 entity-name capture; BT7 fallback (dropped).
3. **Replaced the 13-line inline ability decoder in `HookedBtCandidate8`** with a single `DecodeFF8String` call. The inline's character table agreed with canonical on `0x21-0x2A` / `0x2B` / `0x31` (the bytes ability names actually use) but disagreed on `0x2E` / `0x2F` / `0x30` — bytes that don't appear in real ability-name encodings (the engine uses `0x32` for ability hyphens, both decoders agree), so the disagreement was dead code and migration is strict cleanup.

Also: deleted the `DecodeFF8TextPreview` function definition; refreshed the v0.15.10.0 comment block in `battle_tts_helpers.inl` to record completion; rewrote the file-header comment block at the top of `battle_tts_victory.inl`. Canonical `FF8TextDecode::Decode` is now the single source of truth for FF8 text decoding across the entire mod.

BAT evidence (2026-05-15 18:49 build, 19:00 first battle):
- `build_latest.log` top: `Building FF8 Original PC Accessibility Mod Version 0.15.11.0`. Deployment Complete block: `Version: 0.15.11.0`.
- Item path: `[BT5-EA30] a1=154 -> "Fish Fin"` + `[BT6-EA90] a1=154 -> "Fish's fin"` + `[VICTORY-TTS] Received 1 Fish Fin. Description: Fish's fin`.
- Ability path: `[ABILITY-NAME-HOOK] sub_47E710: a1=85 -> "SumMag+30%"` (the migrated inline decoder, exercising 0x24 in the hex stream which preview would have mangled) + `[GF-NAME-HOOK] sub_47E970: a1=0x42 -> "Ifrit"` + `[VICTORY-TTS] Ability: GF Ifrit learned SumMag+30%.`.
- No `{XX}` markers in any `[BTXT]` / `[VAREXP]` line. EXP Phase 1/2 announcements unchanged. Glyph cache string `" 0123456789ABCDEF:/',"` decodes correctly.

### v0.15.10.2 (pushed 2026-05-15, BAT-confirmed, commit `4ef5b61a`)

Three-item cleanup pass: vestigial `WALK_REPRESS_PERIOD` constants removed from `field_nav_directiondrive.inl`; stale `FF8OPC_VERSION_DATE` macro removed (was reading "2026-05-07" while date was 2026-05-15); BridgeDance per-sample log + `LogBridgeDiagnostic` function dead-body removed from `chase_auto_pilot.cpp`. Pure dead-code/dead-data removal, no runtime behavior change. Caught one missed reference in `nav_log.cpp` on first BAT attempt (the macro was used in a TSV writer, not just a banner); C2065 compile error pointed straight at it, one-line fix and re-BAT clean.

### v0.15.10.1 (pushed 2026-05-15, BAT-confirmed)

`deploy.bat` regex fix: add `/B` (begin-of-line anchor) to findstr so historical `#define FF8OPC_VERSION` mentions in comments stop false-positively matching. Closes the "Version: SINGLE-PRONGED" regression that had been in every build log since v0.15.3.

### v0.15.10.0 (pushed 2026-05-15, BAT-confirmed)

First post-chase backlog work. Retired the v0.10.08 standalone decoder (`DecodeFF8Char` deleted; `DecodeFF8String` rewritten as a thin SEH-safe wrapper around canonical `FF8TextDecode::Decode`). Fixed the off-by-0x03 digit-range bug that made battle TTS announce "X-ATM092" as "X-ATM?6?". Five battle-module call sites kept working unchanged because the public function signature was preserved. v0.15.11.0 (above) completes the unification by retiring the remaining two local decoders.

### Chase chapter (closed v0.15.9.11.3.9, pushed 2026-05-15)

v0.15.0 through v0.15.9.11.3.9 closed the X-ATM092 chase scene accessibility chapter. Empirical 11→0 catch reduction across the full route. Architecture summary: four coordinated keyboard hooks block physical key presses from reaching FF8 during chase Auto (DirectInputCreateA chain, GetDeviceState vtable detour, GetAsyncKeyState MinHook, WndProc subclass). Per-field auto-pilot configs in `chase_auto_pilot.cpp::kFieldConfigs[]`. AUTO battle-suppressor cap stays `INT_MAX` per Aaron's directive (fix the input layer, don't band-aid the catch).

---

## Backlog (in rough priority order)

1. **Generalized countdown-timer hook** — Dollet 30-min countdown is TTS'd via a chase-specific path; generalize for future timers.
2. **Remove party members from field entity catalog** — Squall/Zell/Selphie appear as targetable entities; filter them out.

**Do NOT revert AUTO battle-suppressor cap to 0.** Aaron's 2026-05-13 directive: the fix is the input layer, not the band-aid. v0.15.9.11.3.6 BAT vindicates the call.

### Deferred (don't pick without explicit Aaron direction)

- SeeD rank bug #27 (hypothesis: `FIELD_H_OFFSET = 0xF94` is wrong section size)
- Walk-and-talk dialog gap (hardcoded engine path)
- Refined-coord narrow-gate steering (#29)
- Fire Cavern entry (#28) + planner-fallback
- chase_diag::OnAskOpcodeFired snprintf bug
- `CHASE-AGENT FINAL SUMMARY` log regression (fix in DeactivateFreeze before clearing agent state)

---

## Session ritual & rules

- Read **`DEVNOTES.md`** and **`NEXT_SESSION_PROMPT.md`** at start of every session
- Update both at TWO checkpoints: every version bump AND after every BAT result
- **Filesystem MCP for all Windows project files** — bash runs in a Linux container that can't reach the Windows mod directory
- **Aaron pushes via `Utilities/push_to_github.vbs`**, Claude NEVER pushes
- **Build/BAT cycle**: Aaron runs `deploy.vbs`. "BAT" = built and tested → read `Logs/build_latest.log` tail then domain log (field/mod/dialog/battle/menu/world)
- F-key handlers gated on `!(GetAsyncKeyState(VK_MENU) & 0x8000)` to prevent Alt+Fx interception
- F12 reserved for per-session diagnostics (search source for existing F12 refs first and REMOVE before re-binding)
- **NEVER re-enable SET3 hook** (CI guard in `.github/workflows/safety-checks.yml`)
- DEVNOTES under 10KB — move older history to DEVNOTES_HISTORY.md
- `deploy.bat` version-extract regex requires `/B` anchor (v0.15.10.1) — without it, historical `#define FF8OPC_VERSION` mentions in comments cause findstr to match the wrong line.

---
