# Next Session Prompt — FF8 Accessibility Mod

## Current state at session start

Build: **v0.14.72 — sub_47EC70 hook conflict resolved. BAT PASSED ✅ — ready for v0.14.73 chapter (elemental affinity).**

## What v0.14.72 fixed (now confirmed by BAT)

v0.14.71 BAT log exposed a silent init-time hook conflict that had been broken since v0.14.68-diag. Two modules were racing to install MinHook on `sub_47EC70`:
- `battle_tts_victory.inl::InstallBattleTextHooks()` (`HookedBtCandidate1`, owner since v0.13.14)
- `scan_tts.cpp::InstallGetBattleTextHook()` (added v0.14.68-diag)

Whichever installed second silently failed with `MH_ERROR_ALREADY_CREATED`. Victory's hook was the loser, so victory phase routing (text_id 22/23 → EXP, 21 → ITEMS, 28 → no items, 109 → GF_AP, 121 → GF_LEVELUP, 127 → ABILITY) hadn't run since v0.14.68-diag.

Resolution = Option C: SCAN-TTS removed its installer entirely and became a passive observer called from the victory hook. v0.14.72 BAT log confirms:

```
[BT-HOOK] sub_47EC70 @ 0x0047EC70: 8B 44 24 04 66 8B 04 45  (original prologue, NOT E9 ...)
[BT-HOOK] sub_47EC70 hooked OK
[BT-HOOK] 8/8 hooks installed                              (was 7/8 since v0.14.68-diag)
```

Aaron BAT'd: Scan still captures 'Fly Monster' for Fly-types and falls back correctly for type-less monsters; victory TTS phase announcements (EXP, Items, GF, Ability) are functioning post-Scan again.

## Architectural lesson recorded

MinHook silently fails when two installers race for the same address — only the failure log line distinguishes the loser. Cross-module hooks must use a single canonical installer per address with forward-declared handler functions for cooperating modules. v0.14.72's `HandleBattleText` forward-call pattern is the template: any future module wanting to observe `sub_47EC70` / `sub_4B7210` / `sub_4A3EE0` / `sub_5348E0` / `sub_47EA30` / `sub_47EA90` / `sub_47E970` / `sub_47E710` (the 8 victory-text addresses in `battle_tts_victory.inl`) should add a public function called from the existing victory hook, NOT install its own MinHook.

## Decision point at start of next session

Two natural paths forward:

**A) v0.14.73 elemental affinity (continue scan UI chapter).** Wires keys 6 / 7 / 8 to speak Weak / Absorb / Nullify based on the entity struct's `entity+0x3C` (8 × u16 covering Fire, Ice, Thunder, Earth, Poison, Wind, Water, Holy per the existing `ScanSnapshot::elem[8]` field). Map u16 affinity values to weak/absorb/null per the FF8 affinity scale. Speak as e.g. "Weak against Fire and Ice. Absorbs Water. Nullifies Wind.". From v0.14.70-diag's BATTLE-TEXT-LITE captures we have direct evidence: text_id=38 returns `Weak against` header, text_id=40 returns `has no effect` header, text_id=101 / 102 / 106 return 3-byte sequences (control-code-prefixed glyph indices for element symbols). No new hook needed — entity-struct reads already work. Pure data-formatting.

**B) GitHub push.** v0.14.72 is a stable architectural point. main HEAD is **v0.14.65.3**; local is **v0.14.72**. The actual gap is **~7 versions** (v0.14.66-diag, v0.14.67, v0.14.68-diag, v0.14.69, v0.14.70-diag, v0.14.71, v0.14.72) — verified via `github:list_commits` on 2026-05-02. (Earlier session notes that called this an "~85-build backlog" were wrong.) Pushing now would lock in v0.14.72's hook-conflict fix in the public history before feature work resumes.

Aaron's call.

## Files of interest at session start

- `src/scan_tts.h` — Scan TTS public API including v0.14.72's `HandleBattleText`
- `src/scan_tts.cpp` — ScanSnapshot struct, all field formatters (FormatLevel, FormatHP, FormatStats), `HandleBattleText`. New element formatter for v0.14.73 goes near `FormatStats`.
- `src/battle_tts_victory.inl` — owns sub_47EC70 hook (HookedBtCandidate1), forwards into ScanTTS::HandleBattleText
- `src/battle_tts_hp.inl` — PollHPCheckKeys, where number-key routing for keys 6/7/8 will go for v0.14.73
- `DEVNOTES.md` — full chapter history through v0.14.72 BAT PASSED

## Remaining backlog

1. Persistent accessibility settings
2. GF naming bypass (Siren)
3. Remove party-member NPCs from field entity catalog
4. X-ATMO92 chase
5. v0.14.73 elemental affinity (keys 6/7/8) — ready to start
6. v0.14.74 status resistances (key 9)
7. v0.14.75 active statuses (key 0)
8. `kernel.bin` parsing for Blue Magic
9. GitHub push — main HEAD is **v0.14.65.3**, local is **v0.14.72**; gap is **~7 versions** (verified 2026-05-02 via github:list_commits). Earlier notes claiming an "~85-build backlog" were wrong.

---

## Previous build: v0.14.71 — PRODUCTION promotion of v0.14.70-diag. Type-label capture chapter closed.

The Fastitocalon scan's auto-capture screenshot showed the on-screen Scan UI has no type label — just "LEVEL 6 HP ?????/?????" at the bottom. Diagnostic log confirmed the engine never calls `sub_47EC70(99)` followed by `sub_47EC70(36)` for Fastitocalon. Not every FF8 enemy has a type label. Fly-types display "Fly Monster"; Fastitocalon and many others have no type classification. The mod's "Level 6." readout for type-less monsters is the correct mirror of the on-screen UI.

v0.14.71 stripped diagnostic logs from v0.14.70-diag, kept the wider gate via `GetScanFlightSlot()` and per-scan reset in `OnScanCast`. v0.14.71 BAT showed Bite Bug captured "Fly Monster" perfectly, Fastitocalon correctly silent, zero `BATTLE-TEXT-LITE` lines (strip confirmed). But the BAT also surfaced the BT-HOOK conflict that v0.14.72 now resolves.

### v0.14.70-diag BAT result: mystery solved

The Fastitocalon scan's auto-capture screenshot at `Logs/screenshots/scan_111219_884_slot3_Fastitocalon.png` shows the on-screen Scan UI has **no type label** — just "LEVEL 6 HP ?????/?????" stretched across the bottom, with no "Fish Monster" text rendered anywhere. The diagnostic log confirmed the engine never calls `sub_47EC70(99)` followed by `sub_47EC70(36)` for Fastitocalon — the call sequence stops at text_id=33 (LEVEL).

Not every FF8 enemy has a type label. Fly-types display "Fly Monster"; Fastitocalon and many other enemies have no type classification. The mod's "Level 6." readout for type-less monsters is the correct mirror of the on-screen UI.

### v0.14.71 changes from v0.14.70-diag

**Stripped** (~30 lines removed):
- `[BATTLE-TEXT-LITE]` per-call hex+ASCII log block
- Four explicit "why didn't we capture" failure-reason logs (slot OOB, prefixLen=0, composed empty, already populated). For type-less monsters those would have been false alarms.

**Kept**:
- `GetScanFlightSlot()` helper and the wider gate (covers both action-layer and visible-UI phases). Strict superset of v0.14.69's narrow gate.
- Per-scan reset of `s_lastTypePrefixBytes`/`Len` in `OnScanCast`.
- Core capture logic with single-write per scan event.
- `[SCAN-TTS] Type label captured slot=N typeLabel='...'` success log line.

### Files touched (v0.14.71)

- `src/scan_tts.cpp` — `HookedGetBattleText` body collapsed from ~80 lines back to ~25 lines, doc comment rewritten to reflect the v0.14.71 understanding, install log message updated.
- `src/ff8_accessibility.h` — version bump.

### Expected v0.14.71 BAT outcome

- **Fly-type** scans (Bite Bug, Glacial Eye, Buel): `Type label captured slot=N typeLabel='Fly Monster'` fires once per scan; key 3 says "Level N, Fly Monster.".
- **Type-less** scans (Fastitocalon, T-Rexaur, Iguion, likely Caterchipillar): no type-label log line, key 3 says plain "Level N.". This is correct behavior, not a bug.
- Log volume drops back to ~3 lines per scan (vs. v0.14.70-diag's 15–40 LITE lines per scan).
- Other Scan UI features unchanged: key 1 name, key 2 description, key 4 HP, key 5 stats.

### After v0.14.71 BAT lands

Move to **v0.14.72 elemental affinity** (keys 6 / 7 / 8 for Weak / Absorb / Nullify).

From v0.14.70-diag's BATTLE-TEXT-LITE captures during Fly-type scans:
- `text_id=38` returns `Weak against` header
- `text_id=40` returns `has no effect` header (seen in Glacial Eye scan)
- `text_id=101 / 102 / 106` return 3-byte sequences `<\x05>Y` / `<\x05>Z` / `<\x05>b` — control-code-prefixed glyph indices for the element symbols

Approach: read the elemental affinity bytes directly from the entity struct (`entity+0x3C`, 8 × u16 covering Fire, Ice, Thunder, Earth, Poison, Wind, Water, Holy per the existing `ScanSnapshot::elem[8]` field). Map u16 affinity values to weak/absorb/null per the FF8 affinity scale. Speak as e.g. "Weak against Fire and Ice. Absorbs Water. Nullifies Wind.".

No new hook needed — entity-struct reads already work. Pure data-formatting work.

### Remaining backlog (carried)

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

## v0.14.71 verification points for the BAT

When Aaron sends "BAT" after deploying v0.14.71, look in `ff8_battle.log` for:

1. **Hook install confirmed**: `[SCAN-TTS] sub_47EC70 (get_battle_text) hook @ 0x0047EC70 — OK ... [v0.14.71: type-label capture]`
2. **Fly-type scan** (if Aaron encounters Bite Bug / Glacial Eye / Buel and casts Scan): one `Type label captured slot=N typeLabel='Fly Monster'` line per scan, no `BATTLE-TEXT-LITE` lines.
3. **Type-less scan** (Fastitocalon, etc.): no type-label log line at all, key 3 falls back to plain "Level N." — this is correct.
4. **No regressions**: Auto-announce still fires at fire #1, screenshot still captures at fire #1+90 frames, key 1/2/4/5 still work as before.

If the BAT shows clean Fly-type captures + clean type-less silence, v0.14.71 ships. Then close the type-label chapter and start v0.14.72 elemental affinity in the next session.

---

## Detailed approach for v0.14.71 (already implemented)

Minimal surgical strip. The hook still does the same fundamental work — snapshot the previous call's bytes when a non-36 text_id fires, decode and compose when text_id=36 fires — but with the diagnostic log scaffolding removed. Specifically:

1. **Removed the BATTLE-TEXT-LITE block** (~25 lines) that ran inside the gate before the text_id branching. With it gone, the hook is back to a fast hot-path passthrough for unrelated calls and a focused capture path for scan-flight calls.

2. **Collapsed the four-way failure-reason if/else** back into a single guarded `if`. The pre-conditions (slot in range, prefixLen > 0, typeLabel empty) are now combined into one boolean, and only the success path logs.

3. **Preserved the wider gate** (`GetScanFlightSlot()`) and **per-scan reset** in `OnScanCast`. These are not diagnostic — they're sound architecture worth keeping.

4. **Updated the doc comment** above `HookedGetBattleText` to explain both the type-labeled and type-less monster behaviors so future sessions don't re-discover the Fastitocalon question.

5. **Updated install log messages** to say `v0.14.71` instead of `v0.14.70-diag`.

The code now reflects the production understanding: the engine renders type labels for some monsters and not others, and the mod's job is to mirror what's on screen — not to invent type information for monsters that don't display one.

### v0.14.69 BAT result: PARTIAL WIN

- ✅ **Bite Bug**: `[SCAN-TTS] Type label captured slot=3 typeLabel='Fly Monster'` fired at the same second as the Auto-announce. Key 3 said "Level 8, Fly Monster." — perfect.
- ❌ **Fastitocalon**: 18-second open Scan UI, `Type label captured` line never fired. Key 3 said just "Level 8." with no type.
- ⚠️ **Caterchipillar**: encountered in battle, never actually Scanned (no action-layer fire in the log).

### Hypothesis the v0.14.70-diag tests

The Scan UI's render-order varies by monster type. For Fly-type, the engine fetches the type label AFTER `sub_B687C0` fire #1, so v0.14.69's `IsScreenActive()` gate caught it. For Fish-type, the type label probably gets fetched BEFORE fire #1 (during the cast animation), when `IsScreenActive()` is still false — the hook silently skipped the bytes.

The v0.14.70-diag fix:
1. **Broaden the gate** to cover BOTH action-layer phase AND visible-UI phase via new `GetScanFlightSlot()` helper.
2. **Per-scan reset** in `OnScanCast` so stale prefix bytes from a previous scan can't leak.
3. **Lightweight diagnostic** — log every `sub_47EC70` call during scan flight, plus explicit "why didn't we capture" logging when text_id=36 fires inside the gate but skips.

If the gate broadening is the fix, Fastitocalon will capture 'Fish Monster' and Caterchipillar will capture whatever its type is. If they still fail, the diagnostic log will tell us exactly what the engine's call pattern is for those types.

### Files touched (v0.14.70-diag)

- `src/scan_tts.cpp` (~70 net lines added):
  - New `GetScanFlightSlot()` helper at file scope inside `ScanTTS` namespace.
  - `HookedGetBattleText` rewritten: wider gate via `GetScanFlightSlot()` instead of `IsScreenActive()`, lightweight `[BATTLE-TEXT-LITE]` log line per call, explicit failure-reason logging when text_id=36 doesn't capture.
  - `OnScanCast` adds per-scan reset of `s_lastTypePrefixBytes`/`Len` after the existing `CaptureSnapshot` + `s_pendingScanSlot` writes.
  - Hook install log message updated to mention v0.14.70-diag.
- `src/ff8_accessibility.h` — version bump.

### Expected v0.14.70-diag BAT outcome

Cast Scan on enemies of varied types. The log will contain:

- One `[BATTLE-TEXT-LITE]` line per `sub_47EC70` call during scan flight — typically 8–12 calls per scan.
- Either a `[SCAN-TTS] Type label captured slot=X typeLabel='X Monster'` (success) or one of the new explicit failure lines:
  - `text_id=36 fired but flight slot OOB (-1)` — shouldn't happen given the gate ensures slot >= 0
  - `text_id=36 fired but s_lastTypePrefixLen=0` — means the engine called text_id=36 first thing, before any other call snapshotted a prefix
  - `text_id=36 fired but composed label is empty` — means the prefix decoded to nothing useful
  - `text_id=36 fired but typeLabel already captured` — means we caught it but the engine refetched

Key 3 verifies: "Level N, X Monster." if captured, plain "Level N." if not.

### Branches after BAT lands

- **Both Fastitocalon and Caterchipillar work**: wider gate was the fix. Strip the BATTLE-TEXT-LITE log + explicit failure-reason logs (preserve the broader gate + per-scan reset), bump to v0.14.71 production. Move on to elemental affinity (keys 6/7/8).
- **Fastitocalon works, Caterchipillar still fails (or vice versa)**: third condition we haven't found. Examine differing BATTLE-TEXT-LITE sequences.
- **Both still fail**: architectural rethink needed (different hook target, or screenshot-OCR fallback, or static monster_id → type-id table).

### After v0.14.70-diag BAT lands and the production fix is in (v0.14.71)

- v0.14.71 starts elemental affinity (keys 6/7/8 for Weak / Absorb / Nullify). Element symbols rendered via text_id=101/102 (returned `<\x05>Y` and `<\x05>Z` in v0.14.68-diag), suggesting control-code-prefixed glyph indices.
- v0.14.72 status resistances (key 9), v0.14.73 active statuses (key 0).
- Then carried backlog: persistent accessibility settings, GF naming bypass (Siren), party member NPC catalog cleanup, X-ATMO92 chase, kernel.bin parsing for Blue Magic.

---

## v0.14.69 BAT log evidence summary

From `Logs/ff8_battle.log` (10:28:02–10:36:03):

**Bite Bug scan @ 10:29:09 (success):**
```
[SCAN-HOOK] sub_B687C0 fire #1 slot=3 (window-open trigger — announcing now)
[SCAN-TTS] Auto-announce slot=3 msg='Bite Bug. A bug monster that flies. ...'
[SCAN-TTS] Type label captured slot=3 typeLabel='Fly Monster'
[SCAN-TTS] SpeakField slot=3 fieldId=3 msg='Level 8, Fly Monster.'  (key 3 press)
```

**Fastitocalon scan @ 10:30:38 (failure):**
```
[SCAN-HOOK] sub_B687C0 fire #1 slot=3 (window-open trigger — announcing now)
[SCAN-TTS] Auto-announce slot=3 msg='Fastitocalon. A fish that swims in the ground. ...'
(no Type label captured line during the 18-second scan window)
[SCAN-TTS] SpeakField slot=3 fieldId=3 msg='Level 8.'  (key 3 press, fallback)
[SCAN-TTS] Screen closed (slot=3); number keys revert to ally HP
```

The missing `Type label captured` line for Fastitocalon is the bug we're chasing.

---

## Detailed approach for v0.14.70-diag

The code change is minimal and surgical. The hook still does the same fundamental work — snapshot the previous call's bytes when a non-36 text_id fires, decode and compose when text_id=36 fires. The only differences from v0.14.69:

1. **Gate.** Instead of `if (IsScreenActive() && result != nullptr)`, we do `int slot = GetScanFlightSlot(); if (slot < 0 || result == nullptr) return result;` — wider, with the slot pre-resolved so we don't re-read it inside the text_id=36 branch.

2. **Diagnostic.** Inside the gate, before the text_id=36 vs other branching, we always log `[BATTLE-TEXT-LITE] text_id=N bytes=B hex=[...] ascii=|...| flight_slot=N`. SEH-guarded around the byte read so a bogus pointer logs `<SEH>` rather than crashing. 16-byte max read, stops at null.

3. **Failure-reason logs.** When `text_id=36` fires inside the gate, the original four-condition check (`slot in range && prefixLen > 0 && composed non-empty && typeLabel empty`) is split into four explicit `else if` branches each with its own log line. Silent skip becomes visible.

4. **Per-scan reset.** In `OnScanCast`, after the existing `CaptureSnapshot(targetSlot)` call (which already memsets the slot's typeLabel via `memset(&snap, 0, sizeof(snap))`) and after setting `s_pendingScanSlot`, we add `memset(s_lastTypePrefixBytes, 0, ...); s_lastTypePrefixLen = 0;`. Per-scan freshness.

Correctness invariants preserved: hook still SEH-guarded, the actual capture logic is unchanged, the production behavior for Bite Bug / Glacial Eye / any working case continues to work (wider gate is a strict superset of the narrower v0.14.69 gate).

### v0.14.68-diag BAT result: DECISIVE WIN

Glacial Eye scan during BAT produced 10 unique `[BATTLE-TEXT-DIAG]` entries during the open Scan window. Decoded via FF8 text encoding (uppercase encoded = decoded + 4, lowercase encoded = decoded - 2):

- `text_id=99 (0x63)` returns `'Fly'` — the type prefix for Fly-type monsters
- `text_id=36 (0x24)` returns `'Monster'` — the universal type suffix
- These two text_ids are fetched in IMMEDIATE succession during the Scan UI's render of the bottom-left type label
- Visually confirmed in `scan_001352_348_slot3_Glacial_Eye.png` — bottom-left renders 'Fly Monster' exactly

Other captured text_ids:
- 33 = `LEVEL`, 34 = `HP` (header labels)
- 38 = `Weak against`, 40 = `has no effect` (element headers)
- 113 = `/////` (underline glyph)
- 11 = UI position list, 101/102 = element symbol prefixes

### v0.14.69 architecture

Monster-type-agnostic capture via the existing `sub_47EC70` hook. We don't need to enumerate the prefix-text_id-to-name mapping — the engine itself does the lookup; we just observe the result.

Algorithm in `HookedGetBattleText`:
1. While `IsScreenActive()` is true (a Scan UI session is open), every `sub_47EC70` call's returned bytes are snapshotted into `s_lastTypePrefixBytes` (32-byte buffer, SEH-guarded)
2. When `text_id == 36` ('Monster' suffix) fires, decode the snapshotted prior bytes via `FF8TextDecode::Decode`, compose `'{prefix} Monster'`, store in `s_scanCache[active_slot].typeLabel`. Single-write per scan event.
3. `FormatLevel()` checks `snap.typeLabel` and appends if populated: "Level 14, Fly Monster." instead of "Level 14.".

Auto-announce timing unchanged — still fires at `sub_B687C0` fire #1. The type label hasn't been fetched yet at that exact moment, but by the time the user presses key 3 (typically several seconds later) the type label is captured and ready.

### v0.14.69 what's new

- New `typeLabel[64]` field on `ScanSnapshot` (cleared via existing `memset` in `CaptureSnapshot`).
- New `s_lastTypePrefixBytes`/`s_lastTypePrefixLen` tracking state (32-byte buffer + length counter).
- New helpers: `SnapshotPrefixBytesSafe` (SEH-guarded byte snapshot), `ComposeTypeLabelToBuf` (decodes via `FF8TextDecode::Decode`, trims, composes "{prefix} Monster").
- `HookedGetBattleText` body replaced: logging → type-label tracking. Hook still installed at the same address (`0x0047EC70`), still gated on `IsScreenActive()`.
- `FormatLevel` updated to include typeLabel when populated.
- `OnBattleEnter` resets the prefix tracking buffer.
- Stripped: `PollDiagnosticKey`, `DumpHexWindow`, all 10 candidate-base probes, `BATTLE-TEXT-DIAG` log line, `s_diagF12WasDown` static, the forward decl in `scan_tts.h`, the call site in `battle_tts_hp.inl`. F12 returns to 'reserved for diagnostic builds only' status.

Files touched: `src/scan_tts.cpp` (~80 net lines after strip+add), `src/scan_tts.h` (forward decl removed), `src/battle_tts_hp.inl` (call site removed), `src/ff8_accessibility.h` (version bump).

### Expected v0.14.69 BAT outcome

- Cast Scan on any enemy (Glacial Eye, Bite Bug, Fastitocalon, anything works — mod is type-agnostic).
- Auto-announce fires as before: "<Name>. <Description>. Press number keys 1 through 0 for details."
- During the open window, look for one log line in `ff8_battle.log`: `[SCAN-TTS] Type label captured slot=N typeLabel='Fly Monster'` (or 'Fish Monster' / 'Earth Monster' / etc. depending on the enemy).
- Press key 3 — should hear "Level 14, Fly Monster." (or whatever the captured type label is) instead of just "Level 14.".
- For ally targets, no type label gets captured; key 3 falls back to plain "Level N." — verify this still works.
- HP key (4), Stats key (5), Name key (1), Description key (2) should all behave exactly as before — v0.14.69 only touches FormatLevel.

### After v0.14.69 BAT lands

- v0.14.70 starts elemental affinity (keys 6/7/8 for Weak / Absorb / Nullify). The Scan UI shows element symbols at specific positions — we already know element symbols are rendered via `text_id=101`/`102` calls (returned `<\x05>Y` and `<\x05>Z` in the v0.14.68-diag BAT, suggesting control-code-prefixed glyph indices). Need to figure out the mapping from element symbol → ASCII element name.
- v0.14.71 status resistances (key 9)
- v0.14.72 active statuses (key 0)
- Then move on to other backlog: persistent accessibility settings, GF naming bypass (Siren), party member NPC catalog cleanup, X-ATMO92 chase, kernel.bin parsing.

---

## Detailed approach for v0.14.69

Stateful hook — the type label requires observing TWO consecutive `sub_47EC70` calls (prefix then 'Monster'), so we need a small piece of state between calls. The state is a 32-byte buffer holding the most recent call's returned bytes. When `text_id=36` fires, those bytes ARE the prefix — we decode and compose.

The gate condition is `IsScreenActive()` so the tracking work only happens during the narrow Scan UI window. Outside that window the hook is essentially a passthrough — just `s_originalGetBattleText(textId)` and return.

One-write-per-scan invariant: we only write to `s_scanCache[slot].typeLabel` if it's currently empty. This protects against the engine refetching `text_id=36` later in the Scan UI session for some other UI element — the type label captured the FIRST time text_id=36 fires is preserved.

Also important: `s_lastTypePrefixBytes` is reset on `OnBattleEnter` so a stale prefix from a previous battle can't leak into the first scan of a new battle. Within a battle, the buffer is overwritten with every non-`text_id=36` call so there's no cross-scan staleness concern within a battle.

### v0.14.67-diag BAT result: HYPOTHESIS DISPROVEN, but with useful narrowing

F12 dumps captured cleanly (3x for Bite Bug + 3x for Fastitocalon). Key findings:

- **The static table at `0x015D0B40` is NOT a 16-entry type-info table.** All 16 entries (256 bytes) are byte-for-byte identical (`00 EE 00 02 80 FD 80 02 01 00 00 00 01 01 01 00`) across BOTH monsters. The first dword `0x0200EE00` dereferences to UV-coordinate-style data, identical for both. So `[edi+0xD] = 0x01` always for entries 0–15 — confirming `[edi+0xD]` is a static UI-layout flag, NOT a per-monster type byte.
- **The `cmp al, bl` checks in `sub_84FD90` are layout switches**, not type lookups. The first ~150 instructions are pure UI flag-checking against `[0x269aac8..0x269aade]`.
- **The 156-byte `monster-info entry` at `0x01D972C4 + monster_id*156`** differs strongly between Bite Bug (full structured data starting `A7 90 A9 88...`) and Fastitocalon (mostly zeros for first 64 bytes), but probably because we're reading well past the valid table extent (monster_ids 0x25 and 0x2C are far beyond a typical 7-slot battle entity array).
- **`sub_84D410` input bytes** for the two monsters: 0x00 (Fast) vs 0xA7 (Bite). They differ but the contextual evidence suggests this isn't the canonical type byte either.

### Disassembly walk progress (sub_84FD90, ~250 of 540 instructions covered)

First ~150 instructions are pure UI flag-checking. Around 0x00850150-0x008502E0 there's a function-call cluster that initially looked promising (4 calls to `sub_49F0A0` with arg2=2/3/?/1, results stored in static "UI element width" array `[0x269aaac..0x269aab2]`, then `cmp ax, 0x60`). But peeking into `sub_49F0A0` reveals it's NOT a text fetcher — it does cascading 196-byte-stride table lookups at `0x01D2B110/+0x18/+0xC2` followed by `(value - arg2) & 7` modular-7 arithmetic, which looks like geometric/positioning math, not text fetching. The actual type-label render is either deeper in `sub_84FD90` (~290 more instructions) or in a different scan-UI phase function (`sub_850650`/`sub_850690`/`sub_8506B0`).

### v0.14.68-diag pivot: hook `sub_47EC70` (`get_battle_text`) directly

Per Aaron's user memory, `sub_47EC70` is FF8's canonical battle-text fetch. First instructions confirm signature: `const char* __cdecl get_battle_text(int text_id)` with positions table at `0x01CF8B50` (u16 stride 2) and fallback string ptr `0x01CFF84C`. **Every battle-context string — including the type label — should pass through this function.**

### v0.14.68-diag what's new

- New constant `BATTLE_GET_TEXT_ADDR = 0x0047EC70` in `scan_tts.cpp` near existing `SCAN_GET_TEXT_ADDR`.
- New typedef `GetBattleTextFn`, fn ptr `s_originalGetBattleText`, install flag.
- New `HookedGetBattleText(int textId)` callback: calls original, then if `IsScreenActive()` is true, builds a hex+ASCII dump of the first 32 bytes of the returned string (SEH-guarded, stops at null terminator) and logs:
  ```
  BattleTTS: [BATTLE-TEXT-DIAG] text_id=N (0xX) ptr=0x... bytes=N hex=[XX XX ...] ascii=|...|
  ```
- New `InstallGetBattleTextHook()` paralleling `InstallScanGetTextHook()`. MinHook trampoline.
- Called from `Initialize()` alongside the existing scan hook.
- All v0.14.66/67 F12 probes RETAINED — harmless when F12 not pressed and may produce supplementary info.

Files touched: `src/scan_tts.cpp` (~90 lines added), `src/ff8_accessibility.h` (version bump). No other modules affected.

### Expected v0.14.68-diag BAT outcome

- Aaron casts Scan on Bite Bug + Fastitocalon (no F12 needed — the new hook auto-logs every text fetch during the Scan UI window).
- For each scan, `ff8_battle.log` accumulates a sequence of `[BATTLE-TEXT-DIAG]` lines covering monster name, description, type label, and any other battle UI strings drawn during the open window. Probably 5–30 lines per scan.
- We post-process the hex bytes to find the entry whose decoded text is "Fly Monster" (Bite Bug) or "Fish Monster" (Fastitocalon). FF8 text encoding: lowercase = ASCII - 2, uppercase = ASCII + 4, space stays 0x20, 0x02 = section separator, 0x00 = terminator.
- That entry's `text_id` is the canonical type-label ID. Then we identify the engine path that maps `monster_id → type_text_id` (probably a lookup table somewhere).

### After v0.14.68-diag BAT lands

- v0.14.69 implements the type-label fetch using the discovered text_id and wires it into Level announcement: "Level 14 Fly Monster". Production build, all diag stripped.
- Then v0.14.70+ continues the Scan-data chapter: elemental affinity (keys 6/7/8), status resist (key 9), active statuses (key 0).

---

## Detailed approach

MinHook on `sub_47EC70` with passthrough. The hook callback is gated on `ScanTTS::IsScreenActive()` (returns true while a Scan UI window is open), so the diagnostic only fires during the narrow Scan window — outside that window `sub_47EC70` is also called for victory text, command menus, and status popups, and we don't want spam.

Hex dump format: 32 bytes max, stops at null terminator. SEH-guarded against bogus pointer returns. We don't decode FF8 text in-mod (the decoder lives in `ff8_text_decode.cpp` and calling it from inside the hook risks reentrancy if it ever logs); decoding happens in post-processing on Aaron's hex output.

### v0.14.66-diag BAT plan

1. Start a battle with **2-3 enemies of varied types**:
   - Bite Bug = "Fly Monster" (already validated in v0.14.65.3 BAT)
   - A Geezard or Caterchipillar of a different type (these are common Lv 1-10 area encounters, e.g. Balamb Garden surroundings)
2. Cast Scan on the first enemy. Wait for the auto-announce and the screenshot.
3. **Press F12 while the Scan window is still open.** Confirm `ff8_battle.log` gets a `[SCAN-TYPE-DIAG]` block.
4. Close the Scan window.
5. Cast Scan on a different enemy (different type). Press F12 again. Get a second dump.
6. Optional: a third enemy of yet another type.
7. Upload `ff8_battle.log` for analysis. Claude diffs the dumps to find the type field.

**Important**: F12 must be pressed *during* the Scan window's open period (after the auto-announce, before pressing Esc to close it). The dump is gated on `IsScreenActive()` returning true — if F12 fires outside that window, you'll see `[SCAN-TYPE-DIAG] F12 pressed but scan UI not active — ignoring`.

### Files touched (v0.14.66-diag)

- `src/scan_tts.h` — added `PollDiagnosticKey()` to public API with full doc comment.
- `src/scan_tts.cpp` — added `s_diagF12WasDown`, static helper `DumpHexWindow()`, public `PollDiagnosticKey()` at end of file before namespace closer (~190 lines).
- `src/battle_tts.cpp` — added forward decl in `namespace ScanTTS { ... }` block.
- `src/battle_tts_hp.inl` — added one-line `::ScanTTS::PollDiagnosticKey()` call at top of `PollHPCheckKeys()`.
- `src/ff8_accessibility.h` — version bump.

**F12 verified free** across all source files (no existing `VK_F12` / `0x7B` handlers). No behavior change for normal play — F12 is silent unless the Scan window is open.

### Path through next priorities

1. v0.14.66-diag BAT → analyze the dumps, identify the type field location.
2. **v0.14.66** (production) — Wire the discovered type field into the Level announcement (key 3): "Level 14 Fly Monster" instead of just "Level 14." Strip the diagnostic.
3. GitHub push the v0.13.83→v0.14.66 backlog (~80 builds).
4. **v0.14.67** — Elemental affinity (keys 6/7/8). 8×u16 at `entity+0x3C`. 6=Weaknesses (`<800`), 7=Absorbs (`≥1000`), 8=Nullifies (`=900`).
5. **v0.14.68** — Status resist (key 9). 20 bytes at `entity+0x4C`. Threshold `≥100` = "Strong vs."
6. **v0.14.69** — Active statuses (key 0). Status bitfield at `entity+0x78`.

---

## Required reading at session start

1. `DEVNOTES.md` (project root) — Current Build block has v0.14.66-diag status.
2. This file (top section).
3. `src/scan_tts.cpp` — ScanSnapshot struct, CaptureSnapshot, SpeakField, OnScanPopupSpawn, PollDiagnosticKey.
4. `src/scan_tts.h` — public API surface.
5. `Plan & Research Documents/Scan spell deep research results.md` for offsets and bucket boundaries (remember: subtract 0x14 from any post-header offsets in the research — SAVEMAP header is 76 bytes / 0x4C, not 96 bytes / 0x60).

---

## Workflow rules in effect

- **Filesystem MCP tools only** — never bash for project files. Bash sees `/C:/...` which is a separate container-local filesystem; the system `create_file` tool writes there. Real Windows files must use `filesystem:write_file` / `filesystem:edit_file` at `C:/...` (no leading slash).
- **Update DEVNOTES + NEXT_SESSION_PROMPT** at every version bump and BAT.
- **"BAT" = Built and Tested.** Check `Logs/build_latest.log` tail first, then domain log.
- **Version bump in 1 location** — `FF8OPC_VERSION` in `src/ff8_accessibility.h`.
- **Aaron is blind** — every response starts with `## Claude Says`.
- **Don't declare a fix successful from log markers alone** — verify against Aaron's user-facing experience.
- **NEVER re-enable the SET3 opcode hook (0x1E)** — hangs infirmary scene. CI guard active.
- **F12 reserved for diagnostics** — search/remove old VK_F12 refs before adding new. Search for supporting state variables too, not just the literal `VK_F{n}`.
- **When hooking a game-side audio/render function**, prefer MinHook on the game address directly rather than detecting an FFNx JMP — sidesteps the FFNx-config dependency that broke v0.14.45 SFX.
- **`.inl` files are included INSIDE `namespace BattleTTS {`.** Cross-namespace forward declarations (e.g. `namespace ScanTTS { ... }`) must be placed in the parent `.cpp` BEFORE the namespace opens, not inside the `.inl`. Putting them inside the `.inl` creates `BattleTTS::ScanTTS::Foo` — a different symbol than the global `::ScanTTS::Foo`.
- **Default arguments can appear in only ONE declaration per translation unit.** When an in-file forward decl coexists with a header decl that already provides the default, the in-file decl must OMIT the default. C2572.
- **Engine functions that take a `byte` arg via `push ecx` after `mov cl, ...`** leave the upper 24 bits of ECX as garbage. MinHook callbacks declared `int slotIndex` see the full dword. Always mask `& 0xFF` before using as a slot index.
- **Default to writing code** once an approach is decided. Avoid re-reading transcripts and re-summarizing instead of implementing. If torn between two approaches, pick simpler, commit, iterate from BAT results.
- **Action ID at 0x01D27AE3 is NOT 0x16 for player magic.** It's 0x01. The 0x16 value seen in `[CMD] cmds=[...]` is the Draw command-menu index, not the action staging byte.

---

## v0.14.59 chapter summary (retained for reference)

### Architecture summary (what changed from v0.14.58)

**Old (v0.14.50–58).** Action-layer detection (popup hook in noeffect.inl, magicId==39 in ewm.inl) called `OnScanCast(slot, true)` which announced `<Name>. Level X. HP Y of Z.` immediately at cast confirm and then armed a 30-second lock to suppress the dispatcher / text-fetch hooks that fire later when the Scan window actually opens. Description was never read; Aaron heard the announcement at cast-time when nothing was on screen yet, then the existing TARGET-ACTIVE redundant announce overlapped with it producing the perceived duplicate.

**New (v0.14.59).** Two-stage UX:

1. **Action-layer (silent).** `OnScanCast(slot, true)` populates `s_scanCache[slot]` with a `ScanSnapshot { name, level, curHP, maxHP, hpHidden, monsterId, description, hasDescription }` and sets `s_pendingScanSlot = slot`. **No speech.** Watchdog cancel still fires (Scan never causes HP/status/display change — watchdog would queue spurious 'No effect' otherwise).

2. **Screen-open (auto-announce).** The existing `[SPRITE-POLL] NEW` emitter in screenshot.inl now also calls `::ScanTTS::OnScanPopupSpawn()` when `cur.text_id == 0x06 && cur.value == 50`. That function consumes `s_pendingScanSlot` (atomic exchange to -1), reads the cached snapshot, speaks `<Name>. <Description>. Press number keys 1 through 0 for details.` on Channel 2, and sets `s_scanScreenActiveSlot = slot`. The popup-spawn signal is universal across full and compacted Scan views per v0.14.55+ analysis.

3. **Screen-close.** `[SPRITE-POLL] DESPAWN` for the same `kind=0x06 val=50` calls `::ScanTTS::OnScanPopupDespawn()` which clears `s_scanScreenActiveSlot`. Snapshot cache stays populated for re-scan support within the same battle.

4. **Number-key routing.** `PollHPCheckKeys` in battle_tts_hp.inl now polls all 11 keys (1–0 + H). When `::ScanTTS::IsScreenActive()` returns true, 1–0 route to `::ScanTTS::SpeakField(fieldId)`. Otherwise 1/2/3 retain their historical ally-HP behavior and 4–0 are silent no-ops. H always reads full party HP (unchanged).

### Field bindings

```
1 = Name             6 = Weaknesses          (v0.14.61)
2 = Description      7 = Absorbs             (v0.14.61)
3 = Level            8 = Nullifies           (v0.14.61)
4 = HP               9 = Status Resistances  (v0.14.62)
5 = Stats (v0.14.60) 0 = Active Statuses     (v0.14.63)
```

v0.14.59 wires 1–4. Keys 5–0 reply `Not implemented yet.` so silent failures are impossible to confuse with bugs.

### Hooks retired in v0.14.59

- **30-second action-layer lock** (v0.14.57): retired — silent action-layer means the lock's purpose is gone.
- **sub_84F860 dispatcher hook** (v0.14.54): retired — v0.14.55 BAT proved it's full-view-only; popup-spawn detection covers both views.

### Hook kept as vestigial

- **sub_B687C0 text-fetch hook** (v0.14.52): still installed in `Initialize()`, still masks `slotIndex & 0xFF` per v0.14.58. The callback still calls `OnScanCast(slot, false)`, which is now a no-op (the hook-caller path is `if (!fromActionLayer) return;`). Kept as defense-in-depth for paths the action-layer doesn't see (e.g. Doomtrain Scan-effect, where the spell-name popup may not fire). v0.14.64 polish may strip it.

### Cross-thread state (atomics)

- `s_pendingScanSlot` (LONG): action-layer writes, popup-spawn consumes via `InterlockedExchange`. -1 = none.
- `s_scanScreenActiveSlot` (LONG): popup-spawn writes, popup-despawn clears, `IsScreenActive` reads via `InterlockedCompareExchange`. -1 = no Scan window open.
- `s_scanHookFireCount` (LONG): vestigial hook diagnostic.

---

## BAT plan

### Expected user-facing behavior

1. Cast Scan on an enemy. **No announcement at cast confirm.** Wait for the Scan window to open on screen.
2. When the window opens, hear `<Name>. <Description>. Press number keys 1 through 0 for details.` with the description actually read out (this is the gap v0.14.59 fixes).
3. While the window is open:
   - Press `1` → hears `<Name>.`
   - Press `2` → hears `<Description>.`
   - Press `3` → hears `Level X.` or `Level unknown.`
   - Press `4` → hears `HP X of Y.` or `HP unknown.` (for hidden-HP enemies and any enemy whose maxHP exceeds 99,999)
   - Press `5`/`6`/`7`/`8`/`9`/`0` → hears `Not implemented yet.`
4. Close the Scan window (player presses cancel / it auto-closes).
5. With no Scan window open, press `1`/`2`/`3` → hears Squall/Zell/Selphie HP as before. Press `4`–`0` → silent no-op.
6. Re-scan the same target. Snapshot cache should be re-used, announcement plays again.
7. Cast Scan on an ally. Description segment should be omitted from the auto-announce. Pressing `2` during the window should reply `No description available.`

### Log validation

Review `Logs/ff8_battle.log` for these tags. The action-layer + popup-spawn pair MUST appear in this order for every Scan:

```
[SCAN-TTS] Action-layer fire slot=N (silent; pending announce on popup-spawn)
[SCAN-CACHE] Captured slot=N name='...' level=X curHP=Y maxHP=Z hpHidden=B monsterId=0xMM hasDesc=B
[SCAN-TTS] Auto-announce slot=N msg='...'
[SCAN-TTS] SpeakField slot=N fieldId=K msg='...'   (only if Aaron pressed a number key)
[SCAN-TTS] Screen closed (slot=N); number keys revert to ally HP
```

If `[SCAN-TTS] Action-layer fire` appears but `[SCAN-TTS] Auto-announce` does NOT, the popup-spawn detector didn't fire — likely the `kind=0x06 val=50` popup record didn't appear in the table for this cast path (Doomtrain edge case?). The vestigial sub_B687C0 path will then fire `[SCAN-HOOK] sub_B687C0 fire #N (vestigial — forwards to no-op)` lines but no announcement — that's the diagnostic signature for needing to wire the vestigial path back into a real announce path in v0.14.60+ (or convert it to call OnScanPopupSpawn directly when no popup record is present).

If the action-layer fire happens but the auto-announce uses an empty / generic name, check the `[SCAN-CACHE]` line for `name='Enemy N'` — means `ReadSlotName` failed and the fallback engaged. Cross-reference with the v0.14.50 BAT log: that path worked then, so a regression here would be unexpected.

### Edge cases to specifically test

1. **Scan a hidden-HP enemy** (e.g. Fastitocalon-F or any enemy with maxHP > 99,999 if reachable in the BAT save). HP field should announce `HP unknown.`
2. **Scan an ally.** Description should be omitted from auto-announce; pressing 2 says `No description available.` Pressing 4 still works (allies have visible HP).
3. **Press number keys outside the Scan window.** 1/2/3 → ally HP. 4–0 → silence.
4. **Re-scan same target.** Cache retained, announcement plays normally.
5. **Number keys during command menu navigation while Scan window is also open** (unlikely but possible if scan window stays open across turns) — expected: scan SpeakField wins because the keyboard handler checks `IsScreenActive()` first.

---

## After v0.14.60 BAT lands cleanly

Proceed with v0.14.61 — Stats key 5:

1. Strip the SC2-PROBE diagnostic from `ScreenReader::SpeakChannel2`.
2. Add `stats[8]` capture to `CaptureSnapshot` (read `entity+0xB5..0xBC`).
3. Wire `SpeakField(5)` to format `Defense X. Magic Y. Speed Z.` from `stats[1]` (VIT), `stats[2]` (MAG), `stats[4]` (SPD). Match FF8's display labels.

After v0.14.61 lands, continue to v0.14.62 (elemental) per DEVNOTES priority list.

Quick recap of the Scan-detection chapter (v0.14.49 → v0.14.58):

- v0.14.49 Draw-Stock no-effect suppression. PASS.
- v0.14.50 Name + Level + HP first slice. PASS announce; spurious 'No effect' watchdog discovered.
- v0.14.51 Watchdog cancel. FAIL: only worked for Draw-Cast.
- v0.14.52 sub_B687C0 hook for path-agnostic detection. FAIL: full view only.
- v0.14.53 Diagnostic build. Confirmed sub_B687C0 catches first cast only.
- v0.14.54 Added sub_84F860 dispatcher hook. FAIL: dispatcher also misses compacted view.
- v0.14.55 Action-layer detection via popup hook (text_id=0x06, value=0x32). Build FAIL LNK2019 (.inl namespace trap).
- v0.14.56 Namespace fix. PASS no-effect suppression; but two announcements per cast (action-commit + UI-open 10 s later, gap exceeded SCAN_REARM_QUIET_MS).
- v0.14.57 Two-tier dedup: 30 s per-slot lock when action-layer announces, hook callers check lock at function entry. Build errors fixed (C2668 stale forward decl, C2572 default-arg redefinition). PASS three Scans → three announces.
- v0.14.58 sub_B687C0 hook callback masks `slotIndex & 0xFF` to handle the engine call site at 0x0084F954 only setting CL. Lock verified by log; description never read; Aaron initiates UX redesign.

---

## Required reading at session start

1. `DEVNOTES.md` (project root).
2. This file.
3. `src/scan_tts.h` and `src/scan_tts.cpp` — current `OnScanCast` signature, lock state, and call paths.
4. `Plan & Research Documents/Scan spell deep research results.md` for offsets and bucket boundaries.

---

## Top priority — Scan UX redesign chapter

### Design intent

Replace the current "announce HP/Level at action-commit" behavior with a clean two-stage UX:

**Stage 1 — Cast happens (action-layer fires):**
- Mod silently captures the scan snapshot for the targeted slot (name, level, current/max HP, monster_id, stats, elemental u16 array, status resistance bytes, active status word). Snapshot is stored in a per-battle scan cache keyed by slot.
- No speech yet. The action-layer log entry stays as a trace marker, but `OnScanCast` no longer queues TTS.
- The 30 s lock is no longer needed because the UI hooks are repurposed (see Stage 2). Can be removed or kept as defense-in-depth — decide during implementation.

**Stage 2 — Scan window appears on screen:**
- Detect via the spell-name popup (text_id=0x06, value=0x32) being alive in the sprite-poll table, OR via the first sub_84F860 dispatcher fire for the slot. The popup is the more reliable signal because compacted view skips the dispatcher (v0.14.54 BAT proved this) but always spawns the popup. Use popup-alive as the primary "Scan UI is open" indicator; dispatcher fire as a defense-in-depth secondary.
- On the rising edge of "Scan UI open for slot N", speak: `"<Name>. <Description>. Press number keys 1 through 0 for details."` Channel 2, interrupt=true.
- Set a state flag `s_scanScreenActiveSlot = N`. Number-key handlers check this flag to decide between scan-data query and the existing ally HP behavior.

**Stage 3 — Scan window closes:**
- Detect via the spell-name popup despawn (slot=N kind=0x06 val=50 in `[SPRITE-POLL] DESPAWN`). Reset `s_scanScreenActiveSlot = -1`.
- Number keys revert to their existing ally HP behavior (1/2/3 for Squall/Zell/Selphie HP, etc.).

### Number-key bindings (active only while Scan window is open)

```
1 = Name
2 = Description
3 = Level
4 = HP / Max HP
5 = Stats — Defense, Magic, Speed (FF8 displays these as DEF / INT / DEX)
6 = Elemental Weaknesses — list of elements with bucket value < 800
7 = Elemental Absorbs — list of elements with bucket value >= 1000
8 = Elemental Nullifies / No Effect — list of elements with bucket value == 900
9 = Status Resistances / Immunities — list of statuses with resistance byte >= 100 ("Strong vs ...")
0 = Currently Active Statuses on the enemy — list of bits set in entity_base + 0x78 (Float, Sleep, Poison, etc.)
```

When the Scan window is NOT open, 1/2/3 retain their existing ally HP behavior. The mod consults `s_scanScreenActiveSlot` at the top of the dinput8 keyboard handler:

```cpp
if (s_scanScreenActiveSlot >= 0) {
    // route 1..0 to ScanTTS::SpeakField(slot, fieldId)
} else {
    // existing battle-HP / other handlers
}
```

### Suggested build numbering

Treat this as a multi-build chapter, similar to v0.14.50→58. Suggested split (refine during implementation):

- **v0.14.59 — Architecture refactor + Name/Description/Level/HP query.** Silent action-layer snapshot capture. Scan-screen-open/close detection via popup lifecycle. Auto-announce on screen-open: `"<Name>. <Description>. Press number keys 1 through 0 for details."` Number keys 1–4 query Name/Description/Level/HP. 5–0 stub-respond `"Not implemented yet."` to prevent silent failures. Existing 30 s lock retained or removed at implementer's discretion (the silent action-layer no longer announces, so the lock's purpose is gone).
- **v0.14.60 — Stats (key 5).** Read STR/VIT/MAG/SPR/SPD/EVA/HIT/LUCK at `entity_base + 0xB5..0xBC` (deep research mentions STR=0xB5; remaining offsets via FFRTT Section-7 order — needs in-RAM validation). Speak only the three FF8 displays as DEF/INT/DEX (= VIT, MAG, SPD respectively).
- **v0.14.61 — Elemental affinity (keys 6/7/8).** Read 8×u16 at `entity_base + 0x3C`. Order: Fire, Ice, Thunder, Earth, Poison, Wind, Water, Holy. Bucket boundaries: `< 800` Weak (key 6), `= 900` Nullify (key 8), `>= 1000` Absorb (key 7). Halves and Normal not announced.
- **v0.14.62 — Status resistance (key 9).** Read 20×u8 at `entity_base + 0x4C`. Order from deep research. Threshold `>= 100 (0x64)` = "Strong vs". Validate threshold via in-RAM check on Cactuar (Death-resistant) and Malboro.
- **v0.14.63 — Active statuses (key 0).** Read status bitfield at `entity_base + 0x78` (and possibly other offsets — there's a "timed" word and a "persistent" word per the existing code). Map bits to status names. Reuse the existing target-announce status decoder if possible (see `[TARGET] Entry announce: Bite Bug 1, Float` log line).
- **v0.14.64 — Polish.** Hidden-HP whitelist (Fastitocalon-F, Adel, Sorceress A/B/C, Griever, Helix, Ultimecia — read whitelist out of the `cmp eax, ?` chain in sub_84F860 at mod load), repeat-spam suppression on the auto-announce, ally formatting (descriptions skipped for slots < BATTLE_ALLY_SLOTS), strip diagnostic logs `[SCAN-HOOK]` `[SCAN-DISP]` `[SCAN-DEDUP]` `[SCAN-LOCK]`. Resolve TARGET-ACTIVE redundant announce (see follow-up bug below) — can roll into this build if time permits.

### Implementation notes

**Description lookup chain** (from deep research and existing entity offsets):
```cpp
uint8_t  monster_id = *(uint8_t*) (BATTLE_ENTITY_ARRAY_BASE + slot * BATTLE_ENTITY_STRIDE + 0xB3);
uint16_t pos        = *(uint16_t*)(0x01887474 + monster_id * 2);
const uint8_t* raw  = (const uint8_t*)(0x018875B4 + pos);
std::string desc    = FF8TextDecode::Decode(raw, 256);
```

**Snapshot struct** (suggested layout, in `scan_tts.cpp`):
```cpp
struct ScanSnapshot {
    bool     valid;
    int      slot;            // 0..6
    char     name[64];
    uint8_t  level;
    uint32_t curHP;
    uint32_t maxHP;
    uint8_t  stats[8];        // STR..LUCK at 0xB5..0xBC
    uint16_t elem[8];         // Fire..Holy at 0x3C..0x4B
    uint8_t  statusRes[20];   // 0x4C..0x5F
    uint32_t activeStatus;    // bitfield from 0x78
    char     description[256];
};
static ScanSnapshot s_scanCache[BATTLE_TOTAL_SLOTS];
```
Action-layer fire populates `s_scanCache[slot]`. UI-open trigger reads `s_scanCache[slot]` and speaks. Number-key handlers read `s_scanCache[s_scanScreenActiveSlot]`.

**Cache lifetime:** clear all entries on battle entry (`OnBattleEnter`). Persist across casts within a battle so re-querying a previously-scanned target works even after the target's scan window has closed and reopened. (Consideration for later: do we want number keys to query the *most recently scanned* target even after the window closes? Aaron said the keys revert to ally HP when the window closes — so deferring this — but the cache stays populated for re-scans.)

**Scan-screen-open detection:**
- Primary signal: presence of a `kind=0x06 val=50` entry in the sprite-poll table. The existing `[SPRITE-POLL] NEW i=N slot=M kind=0x06 val=50` and `[SPRITE-POLL] DESPAWN` log lines confirm the popup spans the entire Scan UI session in both full-view (cast 1 BAT log: NEW at 23:39:40, DESPAWN at 23:40:08) and compacted-view (cast 2: NEW at 23:40:15, DESPAWN at 23:40:33).
- Secondary signal: `s_scanDispatchFireCount` incrementing — only fires for full view, but useful as a sanity check.
- Open-edge: first frame the popup exists for `slot=M`. Close-edge: DESPAWN of that popup.

**FF8 config "Scan: Long/Short" toggle:** Aaron correctly recalled there is a Scan animation config option in FF8's in-game config menu. Long animation = full info, waits for input; short = quick, auto-continues. Forcing this to "Long" is not strictly necessary because we extract data from memory regardless of animation, but it might improve the player's perceived experience (longer window for the auto-announce to play, longer time to press number keys). Backlog item, not in this chapter.

**Keyboard collision:** keys 1/2/3 currently announce ally HP (Squall/Zell/Selphie). Resolution by Aaron's design: context-based switch in dinput8 keyboard handler — when `s_scanScreenActiveSlot >= 0`, route 1–0 to scan query; otherwise existing handlers. Tested with the open/close edge detection, the handoff should be invisible to the player.

---

## Carried bug — TARGET-ACTIVE redundant announce

Distinct from the Scan chapter and worth a follow-up. Every targeted spell (not just Scan) currently triggers two TTS events that speak the same target name within 1–6 seconds:

- `[TARGET] Entry announce: <name>, <status>` — when the cursor lands on the target.
- `[TARGET-ACTIVE] 0x9D 0->1: <name>, <status>` — when the user confirms / the action commits.

Diagnosed in v0.14.57 BAT log lines 224, 587, 872, 1137, 1357, 1609. Both lines speak the same string. On long entry→confirm gaps (5–6 s) the user perceives them as separate announcements; on short gaps (1–3 s) they run together and sound like a duplicate. v0.14.58 BAT (cast 1 entry at 00:19:29, active at 00:19:31, scan-tts at 00:19:32 — 2 s entry-to-active, 1 s active-to-cast) is what Aaron heard as the "duplicate immediately upon casting".

Fix candidate: gate `TARGET-ACTIVE` to skip if the same `(slot, status_mask)` was just announced as `TARGET Entry` within N seconds (suggest 5 s). Touchpoint: search for `[TARGET-ACTIVE]` log emitter in `battle_tts*.inl` (likely `battle_tts_helpers.inl` or a target-related .inl). Worth landing in v0.14.64 polish or as a standalone v0.14.65 if time permits.

---

## Carried backlog (after Scan chapter lands)

1. Persistent accessibility settings across play sessions (general — beyond the volume/duck keys which already persist via `ff8_accessibility.ini`).
2. Verify GF naming bypass — Siren failed in earlier testing.
3. Remove party members from entity catalog.
4. X-ATMO92 chase scene accessibility.
5. Boko Choco / Minimog / Moomba / Gilgamesh VTTs (extension of v0.14.44 GF AD).
6. Per-GF AD timing tuning based on continued in-game listening.
7. Bug 3 from v0.14.31 BAT — Magic/GF submenu auto-announce inconsistent (may already self-resolve; retest first).
8. Bug 4 from v0.14.31 BAT — number key 2 announced GF Shiva instead of Squall HP (edge case, lower urgency — and may interact with new Scan-screen number-key routing).
9. Quistis Blue Magic spell-list ordering investigation.
10. Draw menu "???" spell reveal issue.
11. World map: vehicle-aware BFS, guided GPS mode, auto-announce location names, TERRAIN-DIAG cleanup.
12. Battle command menu architecture (tabbed detection), cancel/back re-announce, Magic sub-menu scroll offset for >4 spells.
13. FF8 in-game config "Scan: Long/Short" forcing — investigate whether the mod can flip the option to Long automatically when active.
14. Push v0.14.49+ to GitHub once Scan chapter is stable (~50 builds unpushed).

---

## Workflow rules in effect

- **Filesystem MCP tools only** — never bash for project files. Bash sees `/C:/...` which is a separate container-local filesystem; the system `create_file` tool writes there. Real Windows files must use `filesystem:write_file` / `filesystem:edit_file` at `C:/...` (no leading slash).
- **Update DEVNOTES + NEXT_SESSION_PROMPT** at every version bump and BAT.
- **"BAT" = Built and Tested.** Check `Logs/build_latest.log` tail first, then domain log.
- **Version bump in 1 location** — `FF8OPC_VERSION` in `src/ff8_accessibility.h`.
- **Aaron is blind** — every response starts with `## Claude Says`.
- **Don't declare a fix successful from log markers alone** — verify against Aaron's user-facing experience.
- **NEVER re-enable the SET3 opcode hook (0x1E)** — hangs infirmary scene. CI guard active.
- **F12 reserved for diagnostics** — search/remove old VK_F12 refs before adding new. Search for supporting state variables too, not just the literal `VK_F{n}`.
- **When hooking a game-side audio/render function**, prefer MinHook on the game address directly rather than detecting an FFNx JMP — sidesteps the FFNx-config dependency that broke v0.14.45 SFX.
- **`.inl` files are included INSIDE `namespace BattleTTS {`.** Cross-namespace forward declarations (e.g. `namespace ScanTTS { ... }`) must be placed in the parent `.cpp` BEFORE the namespace opens, not inside the `.inl`. Putting them inside the `.inl` creates `BattleTTS::ScanTTS::Foo` — a different symbol than the global `::ScanTTS::Foo`. v0.14.55 hit this; v0.14.56 fixed it.
- **Default arguments can appear in only ONE declaration per translation unit.** When an in-file forward decl coexists with a header decl that already provides the default, the in-file decl must OMIT the default. C2572. v0.14.57 hit this.
- **Engine functions that take a `byte` arg via `push ecx` after `mov cl, ...`** leave the upper 24 bits of ECX as garbage. MinHook callbacks declared `int slotIndex` see the full dword. Always mask `& 0xFF` before using as a slot index. v0.14.58 fixed this for `sub_B687C0`.
- **Default to writing code** once an approach is decided. Avoid re-reading transcripts and re-summarizing instead of implementing. If torn between two approaches, pick simpler, commit, iterate from BAT results.
- **Action ID at 0x01D27AE3 is NOT 0x16 for player magic.** It's 0x01. The 0x16 value seen in `[CMD] cmds=[...]` is the Draw command-menu index, not the action staging byte. Don't use it as a no-effect-watchdog gate. (Documented in DEVNOTES under v0.14.34 BAT.)
