# Next Session Prompt — FF8 Accessibility Mod

## Current state at session start

Build: **v0.14.65.3 — frame-delay counter on async screenshots. AWAITING BAT.** Five files touched: `battle_tts_screenshot.inl` (added `s_captureFrameDelay` state + `HookedSwapBuffers` counter check + `CaptureScreenshot` reset to 0), `battle_tts.h` (added `frameDelay=0` default parameter to `RequestScreenshotAsync`), `battle_tts.cpp` (updated impl with negative-clamp), `scan_tts.cpp` (call site passes 90 frames ≈ 1.5 s), `ff8_accessibility.h` version bumped to 0.14.65.3.

### v0.14.65.2 BAT result: PASS

- Path fix worked perfectly. Battle log line at 23:42:09 shows the absolute path: `[SCAN-CAPTURE] Auto-screenshot requested at fire #1 slot=3 path='C:\Users\ampag\...\Logs\screenshots\scan_234209_596_slot3_Fastitocalon.png'` paired with `[VICTORY-SCREENSHOT] Saved 640x480`. Claude reads the PNG directly from `Logs\screenshots\` — no manual copying.
- Stats announce: "Strength 20. Vitality 132. Magic 56. Spirit 180. Speed 5. Luck 0. Evasion 6. Hit 0." for Lv14 Fastitocalon, matching `[SCAN-CACHE]` log exactly.
- Image content same too-early render as v0.14.65.1 (labels visible, no values, typewriter partial). That's the issue v0.14.65.3 addresses.

### v0.14.65.3 root cause + fix

The async screenshot fires on the very next `SwapBuffers` after the request — ~16 ms at 60 fps. FF8's typewriter takes ~1–1.5 s to draw the description (~95 chars at ~1.5 chars/frame) plus another ~10–15 frames for stat values. So the captured framebuffer reliably catches stat labels (drawn instantly as a static layout) but only the first 4–8 chars of typewriter content.

**Fix:** add a frame-delay counter `s_captureFrameDelay` next to the existing `s_captureRequested` flag. `HookedSwapBuffers` decrements it each frame and only calls `DoGLCapture()` when it hits 0. `RequestScreenshotAsync` gains an optional `frameDelay` parameter (default 0 preserves prior behavior). `scan_tts.cpp` passes 90 frames ≈ 1.5 s, comfortably above the ~63–80 frames needed for full content. The synchronous `CaptureScreenshot()` path explicitly resets the counter to 0 so its existing 160 ms Sleep-and-poll contract is preserved.

### v0.14.65.3 BAT plan

1. Cast Scan on Bite Bug or any common enemy. Hear auto-announce + stats key 5 (unchanged).
2. Screenshot lands at `Logs\screenshots\scan_<HHMMSS>_<MS>_slot<N>_<EnemyName>.{bmp,png}` ~1.5 s after fire #1.
3. Battle log shows `[SCAN-CAPTURE]` line with `(90-frame delay ≈ 1.5 s)` suffix at fire #1, paired with `[VICTORY-SCREENSHOT] Saved` ~90 frames later.
4. **Captured image shows the FULL scan UI: labels + all 6 numeric stat values + complete enemy name + complete description text.** This is the validation we need — confirms LCK/EVA/HIT positioning and the stat-name mapping (STR→Strength, VIT→DEF, MAG→INT, SPR→SPI, SPD→DEX).

### Path through next priorities

1. v0.14.65.3 BAT → if PASS, GitHub push the v0.13.83→v0.14.65.3 backlog.
2. **v0.14.66** — Elemental affinity (keys 6/7/8). 8×u16 at `entity+0x3C`. 6=Weaknesses (`<800`), 7=Absorbs (`≥1000`), 8=Nullifies (`=900`).
3. **v0.14.67** — Status resist (key 9). 20 bytes at `entity+0x4C`. Threshold `≥100` = "Strong vs."
4. **v0.14.68** — Active statuses (key 0). Status bitfield at `entity+0x78`.

### What changed in scan_tts.cpp (Part A: stats)

All three changes are localized to existing patterns:

```cpp
// New helper near other ReadSlot* helpers:
static bool ReadSlotStats(int slot, uint8_t outStats[8]) {
    // ... SEH-guarded memcpy(outStats, base + BENT_STR, 8) ...
}

// In CaptureSnapshot, after monster_id/description block:
ReadSlotStats(slot, snap.stats);
// ... and [SCAN-CACHE] log line extended with all 8 stat values

// New helper near FormatHP:
static void FormatStats(const ScanSnapshot& snap, char* out, int outSize) {
    // ... 'Strength X. Vitality Y. Magic Z. ...' or 'Stats unavailable.'
}

// In SpeakField switch:
case 5: FormatStats(snap, msg, sizeof(msg)); break;
```

### What changed (Part B: auto-screenshot)

**battle_tts.cpp** — new public wrapper next to `CancelNoEffectWatchdogForSlot`:
```cpp
void RequestScreenshotAsync(const char* basePath) {
    if (!basePath || !basePath[0]) return;
    strncpy(s_captureBasePath, basePath, sizeof(s_captureBasePath) - 1);
    s_captureBasePath[sizeof(s_captureBasePath) - 1] = '\0';
    s_captureRequested = true;
    // No Sleep loop — caller doesn't wait. HookedSwapBuffers picks it up.
}
```

**battle_tts.h** — declaration in `namespace BattleTTS`:
```cpp
void RequestScreenshotAsync(const char* basePath);
```

**scan_tts.cpp HookedScanGetText** — new `else if (count == 30)` branch builds the path with sanitized name and calls `BattleTTS::RequestScreenshotAsync(path)`. One capture per scan event.

### RAM order note (critical)

The validated `BENT_*` constants in `battle_tts.h` give the runtime-RAM order as **STR / VIT / MAG / SPR / SPD / LCK / EVA / HIT** — LCK at index 5 between SPD and EVA. Differs from FFRTT Section-7 .dat-file order (LUCK last). `battle_tts.h` is authoritative since these defines have been used for HP/level reads throughout the codebase. Reading 8 bytes from base+0xB5 gives `stats[]` in `[STR, VIT, MAG, SPR, SPD, LCK, EVA, HIT]` order; FormatStats prints in that same order so the indices match. If the screenshot reveals this is wrong, fix as v0.14.65.1 by reordering `FormatStats`.

### v0.14.60-64 chapter recap (the Scan duplicate-announce hunt is closed)

- **v0.14.60** moved the announce trigger from popup-spawn (action-commit time) to sub_B687C0 first-fire (window-render time). Scan-text-displayed = announce. Aaron BAT: still heard duplicate, simultaneous, disjointed.
- **v0.14.61** disabled dual-channel SAPI (`s_pVoice2 = nullptr` in InitSAPI) and added SC1-PROBE to Speak() to find what was speaking on Voice 1 alongside the SC2 auto-announce. Aaron BAT: duplicate became serialized instead of simultaneous (single-channel works), and SC1-PROBE caught three Voice 1 calls speaking the rendered scan text section-by-section.
- **v0.14.62** identified `Hook_show_dialog` in `field_dialog.cpp` as the source (it was decoding any new window text and calling Speak; for the Scan UI it fired three times — name, description, stats). Gated speak path on `currentMode != 3` (suppressed all battle text). Aaron BAT: scan duplicate fixed, but "Cast Fire" / "Cast <spell>" banner stopped announcing.
- **v0.14.63** narrowed the gate to `currentMode == 3 && ScanTTS::IsScreenActive()`. ScanTTS::IsScreenActive() is true only between OnScanPopupSpawn (window opens) and OnScanPopupDespawn (window closes). Aaron BAT: ALL FIXES CONFIRMED — *"Battle dialogs are reading again and Scan worked as expected with no duplication."*
- **v0.14.64 (this build)** strips the SC1-PROBE / SC2-PROBE diagnostics now that the chapter is closed.

### What's in place from the chapter (do not touch)

- **Single-channel SAPI mode** (v0.14.61) — `s_pVoice2 = nullptr` in `InitSAPI()`. Aaron prefers it; eliminates the simultaneous-overlap class of bugs entirely. SpeakChannel2 falls back to s_pVoice and SAPI's per-voice queue serializes everything.
- **sub_B687C0 first-fire announce trigger** (v0.14.60) — `HookedScanGetText` count==1 calls `OnScanPopupSpawn`, which speaks the auto-announce. Per-scan reset of `s_scanHookFireCount` in `OnScanCast(_, true)` so 'first fire' means 'first fire of THIS scan event.'
- **screenshot.inl SPRITE-POLL DESPAWN** kept for OnScanPopupDespawn close-edge detection. NEW path no longer triggers announce.
- **IsScreenActive()-gated Hook_show_dialog suppression** (v0.14.63) — in `field_dialog.cpp`'s show_dialog hook speak path, when `currentMode == 3 && ScanTTS::IsScreenActive()`, log `[SHOW_DIALOG-SUPPRESS]` and don't speak. Otherwise speak normally. The v0.10.112 "Received <item>" rewrite still applies above the gate.
- **`namespace ScanTTS { bool IsScreenActive(); }` forward decl** in `field_dialog.cpp` near the top, alongside the other namespace forward decls.

### Sanity-check BAT plan for v0.14.64

This is a no-behavior-change build — only stripping diagnostic log lines. Aaron should:

1. Cast Scan on an enemy. Hear the auto-announce ONCE: `<Name>. <Description>. Press number keys 1 through 0 for details.`
2. Cast Fire (or any spell). Hear the "Cast Fire" banner announce.
3. Confirm `Logs/ff8_mod.log` has NO `[SC1-PROBE]` or `[SC2-PROBE]` lines anymore.
4. Confirm `Logs/build_latest.log` shows clean build with no warnings about removed `<intrin.h>`.

If all four check out, v0.14.64 lands and we proceed to v0.14.65.

---

## Next chapter — Scan field queries 5..0

With the duplicate-announce hunt closed, finish wiring the remaining number-key queries during the open Scan window. Keys 1..4 (Name/Description/Level/HP) work; keys 5..0 currently say `"Not implemented yet."` Plan:

### v0.14.65 — Stats (key 5)

**What:** When the Scan UI is open and the player presses 5, speak the enemy's stat block.

**Where the data lives:** `entity_base + 0xB5..0xBC` for STR/VIT/MAG/SPR/SPD/EVA/HIT/LUCK (deep research mentions STR=0xB5; remaining offsets follow FFRTT Section-7 order — needs in-RAM validation against a known enemy). The `ScanSnapshot` struct in `scan_tts.cpp` already reserves `uint8_t stats[8]` for this; just wire `CaptureSnapshot` to read those 8 bytes and add a `FormatStats` helper for the SpeakField case 5 branch.

**Suggested format:** Match FF8's display naming — the game shows STR as Strength, VIT as Defense, MAG as Magic, SPR as Magic Defense, SPD as Speed, EVA as Evasion, HIT as Hit rate, LUCK as Luck. Aaron's call on whether to read all 8 or just the most useful subset (e.g. "Strength X. Defense Y. Magic Z. Speed W."). Default suggestion: read all 8 in a structured sentence.

**Implementation steps:**
1. In `CaptureSnapshot` in `scan_tts.cpp`, after the existing field reads, add an SEH-guarded `memcpy(snap.stats, base + 0xB5, 8)` (or 8 individual byte reads to be safe).
2. Log the captured stats in the `[SCAN-CACHE]` line for diagnostic verification.
3. In `SpeakField`, replace the case 5 stub with a `FormatStats(snap, msg, sizeof(msg))` call that builds the announce string.
4. BAT: cast Scan on Bite Bug, press 5, verify the stats match the in-game scan window display.

### v0.14.66 — Elemental affinity (keys 6/7/8)

**Where the data lives:** 8×u16 at `entity_base + 0x3C`. Order: Fire, Ice, Thunder, Earth, Poison, Wind, Water, Holy.

**Bucket boundaries:** `< 800` Weak (key 6), `= 900` Nullify (key 8), `≥ 1000` Absorb (key 7). Halves and Normal not announced.

**Suggested format:**
- Key 6: "Weak to Fire and Ice." / "No elemental weaknesses."
- Key 7: "Absorbs Holy." / "No elemental absorbs."
- Key 8: "Nullifies Wind." / "No elemental nullifications."

### v0.14.67 — Status resist (key 9)

**Where the data lives:** 20 bytes at `entity_base + 0x4C`. Status order from deep research (Death/Poison/Petrify/Darkness/Silence/Berserk/Zombie/Sleep/Slow/Stop/Curse/Doom/Confuse/Drain/Eject/Float/Mini/Petrifying/Vit0/...). Threshold `≥ 100 (0x64)` = "Strong vs". Validate via Cactuar (Death-resistant) and Malboro.

**Suggested format:** "Strong against Sleep, Confuse, and Petrify." / "No notable status resistances."

### v0.14.68 — Active statuses (key 0)

**Where the data lives:** Status bitfield at `entity_base + 0x78` (and possibly 0x7C for the persistent bits — check `BENT_PERSIST_STATUS` in battle_tts.h for the existing offset). Reuse the target-announce status decoder if possible (the `[TARGET] Entry announce: Bite Bug 1, Float` log line shows a working decoder is already in `battle_tts_helpers.inl` somewhere).

**Suggested format:** "Currently affected by Poison and Slow." / "No active statuses."

### v0.14.69 — Polish

- Hidden-HP whitelist (replace `HIDDEN_HP_SOFT_THRESHOLD` of 99,999 with the authoritative whitelist read from sub_84F860's cmp-chain: Fastitocalon-F, Adel, Sorceress A/B/C, Griever, Helix, Ultimecia).
- Repeat-spam suppression on the auto-announce (if Aaron re-scans the same target within N seconds).
- Ally formatting tweaks based on continued listening.
- Strip remaining diagnostic logs (`[SCAN-HOOK]` / `[SCAN-CACHE]` / `[SCAN-DEDUP]`) once stable.
- Resolve TARGET-ACTIVE redundant announce (carried bug — see below). Could fold here or stand alone as v0.14.70.

---

## Required reading at session start

1. `DEVNOTES.md` (project root) — Current Build block has v0.14.64 status.
2. This file (top section).
3. `src/scan_tts.cpp` — ScanSnapshot struct, CaptureSnapshot, SpeakField, OnScanPopupSpawn.
4. `src/scan_tts.h` — public API surface.
5. `Plan & Research Documents/Scan spell deep research results.md` for offsets and bucket boundaries (remember: subtract 0x14 from any post-header offsets in the research — SAVEMAP header is 76 bytes / 0x4C, not 96 bytes / 0x60).

---

## Other items to consider for an upcoming session

- **Push to GitHub.** The repo at `ampage87/FFVIII-Accessibility-Mod` is at v0.13.63; everything from v0.13.83 onward is local only. ~80 builds unpushed. Worth a clean commit + push between chapters — maybe after v0.14.64 BAT lands as a clean stopping point, or after v0.14.65 once the next chapter is in motion.
- **Carried bug — TARGET-ACTIVE redundant announce.** See section below. Plausible to fold into v0.14.69 polish.

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
