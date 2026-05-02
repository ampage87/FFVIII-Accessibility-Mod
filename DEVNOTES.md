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

**Current build: v0.14.65.3 — frame-delay counter on async screenshots so FF8's typewriter rendering finishes before capture. AWAITING BAT.**

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
