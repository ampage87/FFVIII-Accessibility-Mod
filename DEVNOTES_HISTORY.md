# DEVNOTES_HISTORY - FF8 Accessibility Mod Build History Archive
## All detailed build tables, investigation narratives, and per-version test results

> This file is the archaeological record. Consult ONLY when you need to understand
> WHY a past decision was made, or to trace the evolution of a specific feature.

---

## Session 65 (2026-04-26) — v0.14.23→v0.14.32 build recovery + damage-timing regression fix

### Context

A prior Sonnet session damaged the codebase across ~14 source files while attempting to add diagnostics to `battle_tts.cpp`. Aaron reported the v0.14.23 BAT failed at compile time. Investigation revealed catastrophic state:

- Helper functions (`BattleSpeak`, `BattleSpeakEvent`, `GetEntityHP`, `GetEntityMaxHP`, `GetEntityBlock`, `IsEntityKO`, `CountActiveEnemies`) had been removed from `battle_tts.cpp` but were still required by every `.inl` file.
- `s_ewmEnabled` was duplicated — declared in both `battle_tts.cpp` and `battle_tts_ewm.inl(948)`, causing C2374 redefinition error.
- 9 `.inl` files existed on disk but were not `#include`d anywhere: `battle_status.inl`, `battle_tts_dmgbp.inl`, `battle_tts_dmg_popup_hook.inl`, `battle_tts_dmg_read_bp.inl`, `battle_tts_dmg_render_hook.inl`, `battle_tts_roi_calib.inl`, `battle_tts_spritepool.inl`, `battle_tts_sprite_spawn.inl`, `battle_tts_validate.inl`. That's why `POPUP_RECORD_STRIDE`, `POPUP_TABLE_BASE`, `POPUP_TRACK_MAX`, `s_pollScreenshotDirEnsured`, `s_pollFrameCounter`, `Validate_AnnounceEvent`, `RoiCalib_OnSwapBuffers`, `DrainDeferredTextSpriteLog`, `TriggerImmediateHPFlush` were all undefined at compile time.

### v0.14.24 — Architectural recovery from GitHub HEAD v0.13.61

1. Restored helper functions to `battle_tts.cpp` before the `.inl` includes (matches GitHub HEAD v0.13.61 pattern).
2. Removed duplicate `s_ewmEnabled` from `battle_tts.cpp` (canonical declaration is in `battle_tts_ewm.inl`).
3. Reordered + expanded include chain: helpers → diagnostics → hp → ewm → menu → sprite → status → noeffect → sprite_spawn → validate → dmgbp → dmg_popup_hook → dmg_read_bp → dmg_render_hook → spritepool → roi_calib → screenshot → victory.
   - `noeffect` moved BEFORE `sprite` was reverted: `sprite.inl` calls `NoEffect_QueueAnnouncement`, but `noeffect.inl` also depends on sprite. Resolution: forward-declared `NoEffect_QueueAnnouncement` in `battle_tts.cpp`'s cross-.inl forward-decl block, then included `noeffect.inl` AFTER `battle_status.inl`.
   - `roi_calib` placed BEFORE `screenshot` despite roi_calib.inl's stale comment claiming "AFTER screenshot" — `HookedSwapBuffers` in screenshot.inl calls `RoiCalib_OnSwapBuffers()` with no forward declaration.

### v0.14.25–0.14.30 — Cross-module namespace forward-decl architectural fix

Multiple files (dinput8.cpp, ff8_addresses.cpp, screen_reader.cpp, log.cpp, config.cpp, title_screen.cpp) were missing namespace forward declarations for Log/ScreenReader/Config/NavLog after Sonnet's edits. Created `src/mod_forward_decls.h` with all cross-module forward decls and added `#include "mod_forward_decls.h"` to those 6 files. dinput8.cpp also got missing module includes (ff8_addresses.h, field_dialog.h, field_navigation.h, fmv_audio_desc.h, fmv_skip.h, game_audio.h, world_map.h) and an inline TitleScreen forward decl block (no title_screen.h exists).

### v0.14.31 — Linker error fixes

Fixed 5 linker errors (build now succeeds):
1. WorldMap::Update + Shutdown bodies were deleted from `world_map.cpp` by Sonnet — restored as thin wrappers (Update() calls Poll(), Shutdown() resets module state).
2. `dinput8.cpp` TitleScreen::Initialize forward decl was `bool` but title_screen.cpp defines `void` — fixed dinput8.cpp.
3. ScreenReader::Speak / SpeakChannel2 unresolved across 9 .obj files. **MSVC includes return type in name mangling** (`?Speak@ScreenReader@@YAX...` for void vs `YA_N...` for bool). Inline forward decls said `void Speak` but screen_reader.cpp defines `bool Speak`. Changed all 9 inline decls to `bool`: world_map.cpp, name_bypass.cpp, game_audio.cpp, fmv_skip.cpp, fmv_audio_desc.cpp, field_dialog.cpp, field_navigation.cpp, battle_tts.cpp, menu_tts.cpp.

### v0.14.31 BAT — Build OK, but 4 runtime regressions surfaced

1. **Damage announcement timing regression —** firing at animation END instead of impact-time. `Logs/ff8_battle.log` (timestamp 21:54:21–22:07:27) shows the explicit measurement: `[ROI-LIVE-SHADOW] event 23 SUMMARY: ... yellowLeadVsAnimFlag=13219ms`. The visible damage led the announcement by 13.2 seconds. ZERO `[DMG-RENDER]` or `[DMG-RENDER-HOOK] MH_OK` lines anywhere in the file. The announcements all said `trigger=anim-done` (the v0.13.90 fallback path) instead of `trigger=dmg-render` (the v0.14.10 production trigger).
2. Status effect spell miss/no-effect not announced (cast Blind on Biggs/Elvoret, no announcement).
3. Magic/GF submenu auto-announce inconsistent.
4. Number key 2 announced GF (Shiva) details instead of Squall's HP, no GF being summoned.

### v0.14.32 — Damage timing regression fix (bug 1 only)

**Root cause confirmed via log read:** the v0.14.24 build recovery rebuilt `battle_tts.cpp` from GitHub HEAD v0.13.61 (which predates the v0.14.4 / v0.14.10 damage hooks). The `.inl` files for the new hooks were correctly wired into the include chain, but the corresponding `Install()` and `Reset()` calls were never added to `OnBattleEnter()`. So the impact-time render hook on `sub_5068B0` was sitting in source code, fully written, but never installed at runtime. With no hook installed, `s_lastDmgRenderTick` stayed 0, the impact-time trigger in `PollHPChanges` never fired, and the announcement fell back to the v0.13.90 anim-flag-fall path — the OLD pre-v0.14.10 "announce at animation end" timing.

**Fix applied to `src/battle_tts.cpp` OnBattleEnter():**
- Added `DmgPopupHook_Reset()` and `DmgRenderHook_Reset()` to the reset block (after HP tracking reset).
- Added `DmgPopupHook_Install()` and `DmgRenderHook_Install()` to the install block (after `EWM_InstallBattleEffectHook`). Install functions self-guard on `s_dmgPopupHookInstalled` / `s_dmgRenderHookInstalled` flags so calling every battle is safe.

### v0.14.32 BAT — Bug 1 fully resolved

Log evidence (timestamp 22:32:33–22:34:04):
- `[DMG-RENDER-HOOK] MH_OK — hooked sub_5068B0` install line present at battle entry
- `[DMG-POPUP-HOOK] sub_48EF80 hook @ 0x0048EF80 — MH_OK` install line present at battle entry
- `[DMG-RENDER] slot=0x01D28344 dmg=76 active=0x08 tick=35620828` fired during damage event
- `[HP-TRACK] Damage popup rendered via sub_5068B0 — impact-time trigger dmg=76 (was 62 ms after anim-up)`
- `[VALIDATE] kind=kill slot=3 value=76 trigger=dmg-render tts="Grat takes 76 damage. Defeated."`
- `[POPUP-SPAWN] Immediate HP flush completed - anim-flag bypass active` (fallback correctly suppressed)
- `[ROI-LIVE-SHADOW] event 4 SUMMARY: ... yellowLeadVsAnimFlag=-109ms` (announcement landed essentially synchronized with visible damage; compare v0.14.31's 13219ms gap)

Bugs 2, 3, 4 deferred to next session — likely same regression class (missing Install calls).

---

## Session 83 (2026-04-24) — v0.13.78 observational hook rework

### Static walk of sub_487DF0 VM opcode table + text render pipeline

With the Assembly_Files.zip extracted and in /home/claude/asm_files, did a comprehensive static walk answering Q1 from the session 82 handoff ("Is sub_495280 actually the right target?").

**Q1 answered definitively: sub_495280 is a TEXT writer, not a sprite spawner.** It writes null-terminated FF8-encoded byte strings into a 16-slot × 48-byte ring buffer at 0x01D29034. Confirmed from its 13-instruction body, plus the fact that the `ent` values seen in session 80's `[SPRITE-POLL] NEW` events (0x01D29034, 064, 094, 0C4 — 48-byte stride) are pointers STORED inside action-announce records (bytes[12-15] per v0.13.74 record layout), not the sprite records themselves.

### New discoveries in session 83

1. **sub_4952F0 is a SECONDARY text ring buffer writer** at 0x01D29334 (16 × 48 bytes, rotating counter at 0x01D2A279). Called from VM opcode handlers at 0x004884E8 and 0x0048854B (style byte 1 and 3 respectively).

2. **sub_495330 is an int-to-digit-string formatter** (5-digit, divide-by-10 with magic number 0xCCCCCCCD, strips leading zeros, looks up kernel.bin glyph codepoints via sub_47EC70(0xb)). 5 callers: 0x004883FC (status duration), 0x0048D5DF (action-announce MP cost inside sub_48D200), and three in an unknown function at 0x00490???.

3. **sub_482C90 is a sprite slot allocator** managing a linked-list pool of 16 slots × 16 bytes at 0x01D28C04 (metadata) + 0x01D28C44 (data). Each slot's first dword is a polymorphic update/render callback. 6 allocation sites register 5 distinct callbacks: 0x47E030 (general task spawner), 0x48ACD0 (flag manager), 0x48AC60 (one-shot text render), 0x48AC90 (timed text render), 0x48E620 (complex per-frame update).

4. **VM opcode 0x3C at 0x00489F30 is "apply damage to HP":** reads 2 bytes from script, sign-extends, adds to `battler[slot].field_18`. This is the ground-truth HP delta hook point — deferred to v0.13.79+ pending BAT3 results.

5. **VM opcode table decoded:** 61 entries at 0x0048A0B8. 39/61 decoded directly by re-encoding the garbled disassembly; remaining 22 identified by their exit addresses (jmp 0x487eba). Status-text composition opcode at 0x00488235 uses sub_47EAF0 (name lookup) + sub_47EC70 (kernel string) + sub_47E970 (text lookup) + sub_495210 × 3 (concat) + sub_495280 (ring write) + sub_47E220 (task 8 enqueue).

### Three hypotheses for floating damage NUMBER path

- **(A)** Damage numbers ARE in the 0x01D280C4 table with kind=0x01 (physical), renderer reads damage from HP-delta state rather than record bytes[4-5].
- **(B)** Damage numbers go through the unknown function at 0x00490??? that chains sub_495330 + sub_495280 + sub_48E5F0.
- **(C)** Damage numbers are rendered by the graphics pipeline directly from battler state — no gameplay hook catches them.

v0.13.78's observational hooks (sub_495280 + sub_4952F0 with retaddr capture) should resolve which of A/B/C is correct based on BAT3 log retaddrs.

### v0.13.78 implementation details

- Both hooks use the **deferred-log pattern**: hook body is SEH-free and Log-free, only does memory reads of static globals + fixed 24-byte raw-byte copy from the text arg. Atomically claims a ring-buffer slot via InterlockedIncrement, fills fields, _WriteBarrier, sets `valid=1`.
- Drainer runs from HookedSwapBuffers (battle_tts_screenshot.inl) where SEH and Log are safe. Drains up to the current claim, skipping invalid slots (left for next frame). Logs first-30 unconditionally, dedups thereafter by (writer, retaddr, first-8-bytes-of-text).
- Both hooks install from OnBattleEnter. State reset per battle via ResetSub495280HookState().

### Addresses added to the canonical list

0x00488235, 0x0048834C, 0x0048847A, 0x004884E8, 0x0048854B, 0x00489F30 (VM opcode handlers). 0x004952F0, 0x00495330 (text helpers). 0x01D28C04, 0x01D28C44 (sprite slot pool). 0x01D29334, 0x01D2A279 (secondary text ring).

---

## Session 82 (2026-04-23/24) — Minimal sub_495280 hook diagnostic

### v0.13.76 — First sub_495280 hook (hung)

Added MinHook on sub_495280 @ 0x00495280 with first-N unconditional context log + dedup. Hook decoded `raw=47 5F 71 72 20 57 6A 63 63 6E 00` → `"Cast Sleep"` correctly from retaddr=0x0048D772 (action-announce caller inside sub_48D200 family). Game thread tick rate then degraded to zero over ~2 seconds (GF-HOOK counter 60 → 11 → 0). Hung without crashing. Suspected cause: SEH setup/teardown inside hook body on arbitrary game threads, combined with Log::Battle from those threads.

### v0.13.77 — Minimal diagnostic hook (passed)

Shrank hook to `s_origSub495280(text); InterlockedIncrement(&s_sub495280CallCount);` — no SEH, no Log, no decoder, no context snapshot. Full Grat battle (including Sleep cast) played to victory with no hang. Confirmed MinHook trampoline for sub_495280 is safe; the hang was in our logging/SEH path.

Two open questions at session 82 close: (Q1) Is sub_495280 the right target? (Q2) What specifically caused the hang?

### Session 81 open question resolution (carried into 83)

Session 81 found sub_495280 via direct asm search for 0x01D29034 (observed `ent` value from session 80 SPRITE-POLL NEW events). The connection was real at the text-writer level, but session 83 reframed the meaning: sub_495280 is the TEXT writer used by multiple systems including action-announce text and VM status-text opcodes, not the floating-sprite spawner per se.

---

## Session 81 (2026-04-22) — sub_495280 discovery via direct asm search

### Definitive find

Direct search for `0x1d29034` in the asm file hit at 0x004952A3 inside sub_495280 (17 callers per callxrefs). Walking the function confirmed it's a text-writer into a 16-slot × 48-byte ring buffer at 0x01D29034. Slot counter at 0x01D2A278 (byte, self-wraps via AND 0xF on entry).

**sub_495280 full body (0x00495280–0x004952CC):**
```
0x00495280: mov  al, [0x1D2A278]      ; read slot counter (ring buffer idx)
0x00495285: push esi
0x00495286: and  al, 0xF                ; mask to 4 bits — 16-slot wrap
0x00495288: mov  esi, [esp+8]           ; esi = arg1 = source text ptr (null-terminated)
0x0049528C: mov  [0x1D2A278], al        ; normalize slot idx back
0x00495291: mov  eax, [0x1D2A278]
0x00495296: and  eax, 0xFF
0x0049529B: mov  dl, [esi]               ; first byte
0x0049529D: lea  eax, [eax + eax*2]     ; 3*slot
0x004952A0: shl  eax, 4                   ; 48*slot  ← 48-byte stride MATCH
0x004952A3: lea  eax, [eax + 0x1D29034] ; dest = 0x1D29034 + 48*slot  ← observed ent values
0x004952A9: mov  ecx, eax
0x004952AB: mov  [ecx], dl                ; write byte
0x004952AD–BB: loop: read, write, inc, loop until null terminator
0x004952BD: mov  cl, [0x1D2A278]
0x004952C3: pop  esi
0x004952C4: inc  cl                       ; advance slot counter
0x004952C6: mov  [0x1D2A278], cl         ; (wraps via 0xf mask on next entry)
0x004952CC: ret
```

Session 81 reframed by session 83: sub_495280 is TEXT, not sprite. Session 83 found sub_4952F0 as second text writer, plus the real sprite slot allocator sub_482C90 at 0x01D28C44.

### False leads ruled out in session 81

1. sub_487DF0 effect-VM — deep research confirmed battle-effect animation VM, not sprite spawner. Opcode table at 0x0048A0B8 handles palette/texture/SFX/vibration. Session 83 revisited and found TWO opcodes that DO write text (0x00488235 status text, 0x004884E8 / 0x0048854B simple text); one (0x0048834C path B) allocates a sprite slot via sub_482C90.
2. sub_485DC0 — walked prologue, found status-clear loop (iterates battler-struct status bytes at [slot*0xD0 + 0x1D27BC8], calls sub_486B40 for each, clears). Not a sprite spawner.
3. sub_483940 — started walking, found RNG-heavy path with internal jump table at 0x00483C40 (12 entries). Likely animation dispatcher, not spawner.

### Session 81 prep walk — function containing 0x0048594E

The player-action caller retaddr into sub_48E830. Partially mapped to `sub_485610` with inventory of its sibling calls: sub_47E080(5) (anim trigger), sub_483D60 (target mask), sub_485EC0 (resolver), sub_486DC0 OR sub_486E00 (outcome class), sub_485DC0 (unknown), sub_483940 (unknown), sub_4877F0 (spell result dispatch), sub_48E830 (action-announce commit), sub_486A10 (status-slot lookup).

**Session 83 revision:** sub_486DC0 and sub_486E00 turned out to be RANDOM TARGET PICKERS (not damage calculators as originally guessed). DEVNOTES "outcome-class A/B" annotation was wrong. The real damage calc is deeper in the VM.

### Second popup counter at 0x01D28DF0

Immediately after the call 0x48e830 at 0x00485949, the function increments a separate byte counter at 0x01D28DF0. Session 81 flagged this as a candidate "second sprite table." Session 83 walked all references to 0x01D28DF0 and found they're all internal to the action-announce system — no separate table. Confirmed dead-end.

---

## Session 80 (2026-04-19) — sub_48E830 confirmed action-announce-only

### Dual-hook BAT + frame-level sprite observer

Versions through session: v0.13.72 added sub_48E830 hook in new battle_tts_sprite_spawn.inl with `[SPRITE-SPAWN-SEQ]` logging. v0.13.73–.74 added SwapBuffers-based frame-level poll of the 0x01D280C4 record table with `[SPRITE-POLL] NEW/KIND/DESPAWN` transitions. v0.13.75 added adjacent-slot record dumps + auto-screenshots on NEW transitions.

### BAT matrix (single encounter, Grat x1, 16:26:46–16:28:50)

10 events captured via `[SPRITE-POLL] NEW`:

| # | Action | kind | val | style | ent | HP delta |
|---|---|---|---|---|---|---|
| 1 | Draw | 0x06 | 40 | 0x0B | 0x01D29034 | 0 |
| 2 | Draw-Cast Sleep | 0x06 | 40 | 0x0B | 0x01D29064 | 0 |
| 3 | Draw-Cast Silence | 0x06 | 41 | 0x0B | 0x01D29094 | 0 |
| 4 | Draw-Cast Berserk | 0x06 | 46 | 0x0B | 0x01D290C4 | 0 |
| 5 | Magic Berserk hit | 0x02 | 7 | 0x0B | 0x01CF907D | -185 |
| 6 | Berserk-Squall Atk hit | 0x01 | 0 | 0x0D | 0x00000000 | -159 |
| 7 | Quistis self-whip MISS | 0x01 | 0 | 0x0D | 0x00000000 | 0 |
| 8 | Berserk-Squall Atk hit | 0x01 | 0 | 0x0D | 0x00000000 | -118 |
| 9 | Enemy Grat atk | 0x08 | 2 | 0x0D | 0x00000000 | -70 |
| 10 | Quistis Atk kill | 0x01 | 0 | 0x0D | 0x00000000 | -98→168 |

### Definitive conclusions from session 80

1. sub_48E830 + sub_48D200 are confirmed paired-finalizer for action-announce popups only. Both fire 1:1 on every player action (12 calls each per battle). Neither handles the per-result floating damage/Miss/status-icon sprite.
2. The real sprite render path writes somewhere outside the 0x01D280C4 20-byte-stride table (or writes without incrementing 0x01D280C0). The frame-level poll sees only action-announce records, never a damage-number record.
3. Physical attack misses are NOT announced. Existing sub_4877F0 kind=4 a3=0x9 path catches spell/Draw-cast misses only. Aaron's Quistis self-whip-miss was silent in TTS.
4. Enemy damage flows through a different caller (retaddr=0x00489FC0) than player actions (retaddr=0x0048594E). Both route through sub_48E830, so both are action-announce-class.

### Screenshot system confirmed working (v0.13.74+)

SwapBuffers hook captures on every `[SPRITE-POLL] NEW` and every kind=4 dispatcher fire (400ms delayed, 10/battle cap). Session 80 produced 10 poll_NEW_*.png + 6 kind4_*.png in Logs/screenshots/. Hit/miss pair for visual comparison: poll_NEW_162823_746_...slot0_kind01_val0.png (Quistis self-miss) vs poll_NEW_162843_213_...slot0_kind01_val0.png (Quistis hit on Grat for 168).

### Cross-session revision: sub_486DC0 / sub_486E00 annotation

Session 80 prep walk annotated these as "outcome-class A/B calculators." Session 83 disassembly showed they are actually RANDOM TARGET PICKERS (for live-ally / live-enemy selection). Treat the old annotation as wrong.

---

## Sessions 75–76 (2026-04-17) — EWM Cap→Freeze + Principle Validation

### v0.13.57 (session 75) — Cap→Freeze + Damage-Anim Diagnostic

**Conceptual change.** Prior versions used a "cap at max-1" sandwich: entities advanced naturally during freeze windows but clamped at max-1. During long holds (GF summons, damage windows) everyone converged at 11999, erasing the natural ATB race. Changed sandwich to true freeze: restore ATB to exact pre-sandwich value, so entities stay at whatever value they had when freeze engaged. Same for GF loading counter.

**Files touched:**
- `src/battle_tts_ewm.inl` — removed cap-at-max-1 logic from POST-FREEZE for both ATB and GF loading counter; new `EWM_FormatATBSnapshot()` + `EWM_PollDiagnostics()` called each frame from `EWM_UpdateBattle`. New log tags: `[DMG-DIAG]`, `[ACT-DIAG]`, `[FRZ-DIAG]`, `[POST-REL]`.

**BAT result:** Aaron reported "I didn't notice any overlap, which was fantastic!" — the TTS-damage-overlap bug that sessions 69–74 chased is **resolved** by switching to freeze semantics + keeping the dispatch hooks from v0.13.55/56.

**Side observation:** Aaron noticed party getting 2–3 turns per enemy turn. Initially thought a bug, but analysis showed this is just 3v1 arithmetic + G-Soldier's slower speed. Both sides' ATB was correctly frozen during each other's actions (verified by ATB snapshots in log). Chose option 1: live with it (matches Wait mode economy).

### v0.13.58 (session 76) — Turn-Counter Diagnostic (first attempt)

**Goal:** Let Aaron A/B test EWM-on vs EWM-off turn ratios empirically to confirm freeze has zero impact on turn economy.

**Implementation:** `EWM_TrackTurnCount()` detects a turn start by watching each slot's ATB transition from high (>10000) to low (<2000). Called from inside `EWM_UpdateBattle`.

**Bug:** `EWM_UpdateBattle` is gated behind `s_initAnnounceDone && s_ewmEnabled && s_ewmHookInstalled`, so the `s_inBattle` transitions the tracker tried to detect weren't reliably visible. Reset-on-battle-start and summary-on-battle-end only fired on the first battle of a session. Counters ran continuously across all subsequent battles.

### v0.13.59 (session 76) — Turn-Counter Lifecycle Fix

**Architectural split.** Broke the tracker into three functions called from three different lifecycle points:
- `EWM_ResetTurnCount()` — called from `OnBattleEnter()` in `battle_tts.cpp`
- `EWM_TrackTurnCount()` — per-frame poll called from `BattleTTS::Update()` gated only on `s_inBattle`
- `EWM_LogTurnCountSummary()` — called from `OnBattleExit()` before `s_inBattle = false`

Each hook now fires exactly when it's supposed to regardless of EWM state or init progress.

**BAT result (10-battle A/B test):**

| | EWM ON | EWM OFF |
|---|---|---|
| Battles (with ≥1 enemy turn) | 3 of 5 | 4 of 4 |
| Party turns (aggregated) | 18 | 21 |
| Enemy turns (aggregated) | 8 | 9 |
| **Party:enemy ratio** | **2.25 : 1** | **2.33 : 1** |

Ratios within 3.5% of each other. 2v2 battles matched exactly (2.25:1 both). 1-enemy single-turn battles all landed at exactly 2.00:1 regardless of EWM state. **Conclusion:** EWM freeze has no measurable effect on turn economy. Principle validated empirically.

### v0.13.60 (session 76) — Turn-Summary Format Cosmetic Fix

**Bug found in BAT log:** Summary line printed `s7=257 (total=0)` — `s7` doesn't exist. `BATTLE_TOTAL_SLOTS=7` means enemy slots are s3–s6 (indices 3, 4, 5, 6). The format string included a spurious `s7` which was an out-of-bounds read of whatever happened to be in the adjacent memory. Ratio math was always correct (loop summed the right indices); only the per-slot display had garbage.

Fixed format string to print s3–s6 only. No behavior change; next battle logs will show clean summary. Ships silent — no further BAT needed.

### Key Learnings from Sessions 75–76

1. **The overlap bug's real root cause** was the cap's "converge at max-1" behavior, not a dispatch race. Dispatch hooks (v0.13.55/56) were necessary defense-in-depth but didn't solve it alone. Switching cap→freeze eliminated the simultaneous-tie-dispatch-at-release condition.

2. **EWM principle crystallized.** Memory #26 updated: "Enhanced Wait Mode retrofits FF8 into sequential turn-based — only ONE action/menu occurs at a time. ATB still races normally; whoever fills first goes first (no advantage, same turn economy as vanilla). During ANY action (menu, attack, GF, damage anim), ALL other ATB freezes. Gameplay-neutral — doesn't add turns or prevent enemy attacks, just sequences them for TTS. Preserve: (1) first-to-fill acts first, (2) no skipped turns, (3) natural ally/enemy ratio."

3. **Diagnostic lifecycle hooks beat inline state machines.** Version 0.13.58 tried to detect battle-enter/exit transitions inside the per-frame tick; v0.13.59 split the work across the battle lifecycle functions that already existed. Cleaner, more reliable, easier to read.

4. **Turn-counter detector is EWM-independent.** ATB high→low watch works whether the freeze sandwich is active or not, so A/B comparisons are apples-to-apples. Reusable for any future turn-economy question.

### v0.13.55–56 Outcome (retrospective)

Keep both dispatch hooks (`sub_483470` + `sub_482F80`) — they're doing their job at the dispatch layer even though the visible-overlap symptom was actually solved by cap→freeze at the ATB layer. The hooks provide defense-in-depth for edge cases (fast enemy attacks during transition-hold windows). Removing them is not planned.

---

## Sessions 69–74 (2026-04-16 → 2026-04-17) — Damage / Command-Menu Overlap Race

**7+ builds across 6 sessions.** Progressive attempts to stop enemy attacks from landing visually during the next player's command menu. Full per-session transcripts at `/mnt/transcripts/`; key decisions summarized:

### v0.13.51 (session 69) — Draw-submenu ATB cap hotfix
Unrelated to overlap bug but shipped same session. Three-edit fix: moved `s_inSubmenu` from menu.inl to hp.inl so ewm.inl can read it; extended `submenuOpen` check to `(menuPhaseDword >= 0x00400000) || s_inSubmenu`. Aaron BAT-confirmed 20:07:43: "It is working for suppressing ATB while moving around menus."

### v0.13.52 (sessions 70–71) — Dual-layer TTS ordering fix
**Layer 1** (ewm.inl): extended damage-TTS-hold block to cover five signals: `s_ewmHoldForDamageTTS`, `s_anyHpPending`, `s_damageAnimWasActive`, `*(uint8_t*)0x01D280C0 != 0`, `*(uint32_t*)0x01D27B00 != 0`. When any active AND `activeChar == 0xFF`, cap all slots with `excludeSlot=0xFF`.
**Layer 2** (menu.inl): added `s_deferredTurnBuf/Pending/Tick` state; turn-announce site defers if damage signals active; new `PollDeferredTurnAnnounce()` wired into `battle_tts.cpp::Update()` after `PollHPChanges`.
Result: TTS ordering correct, but enemy attack still lands visually during menu.

### v0.13.53 (session 72) — Post-turn grace + stale-deferral cancel
Added 1000 ms post-turn grace after activeChar transitions player→0xFF. `s_ewmPrevSeenActiveChar`, `s_ewmPostTurnGraceEnd`, `EWM_POST_TURN_GRACE_MS`. Also: `s_deferredTurnChar` tracks turn at defer time, cancels if activeChar changed. Dropped `ScreenReader::IsSpeaking()` gate in `PollDeferredTurnAnnounce` (caused 4-second-stale announcements during menu nav). Narrower race, still fails for GF-completion case.

### v0.13.54 (session 72) — Post-action cooldown
`EWM_POST_ACTION_COOLDOWN_MS = 500`, tracked via `s_ewmLastSignalTime`. `damageOrActionActive = postTurnGraceActive || anyActiveNow || postActionCooldown`. Narrows the window further but doesn't close it — fast actions where `[0x01D27B00]` flickers between 0 and 1 too briefly for mod thread to observe.

### v0.13.55 (session 73) — **Architectural shift**: MinHook on sub_483470
Cap-based approach abandoned in favor of direct dispatch intercept. `s_blockProcessReady = damageOrActionActive || (activeChar < 3)`. Hook returns early when flag set.
**Critical BAT result**: log showed `[DISPATCH] sub_483470: calls=6 blocks=6 block-flag=1` during bug window — hook caught every call, yet enemy attack still landed. Proved `sub_483470` is not the sole dispatch path.

### v0.13.56 (session 74) — Add sub_482F80 hook, awaiting BAT
Hypothesis: `sub_483470` + `sub_482F80` are a natural pair called back-to-back under the same engine gate. 483470 = queue/player-menu side, 482F80 = execution side. Added second MinHook, plus "passes" counter and always-on dispatch stats log for better diagnosis. See current DEVNOTES.md for full details.

### Cumulative key learnings from this saga
- Cap-based prevention has an unavoidable 16 ms race window at mod-thread poll boundaries
- `[0x01D27B00]` is not a persistent animation flag for fast actions
- Dispatch-layer hooks are the right intervention point (atomic, no race)
- Multi-function dispatch: FF8 appears to split queue processing and action execution across two functions
- `s_blockProcessReady` read is safe with `volatile bool` on x86 (no explicit synchronization needed)

---

## Session 67 (2026-04-16) — GF Submenu Fix + Code Cleanup + Draw Fix (v0.13.49→v0.13.50)

3 builds. Key changes:
1. **GF submenu detection build error**: `wasCommandMenu` undeclared after previous session's cleanup removed it. Fixed stray reference in `sm == 0x00` fallback block.
2. **Code cleanup — `EnterSubmenu()` helper**: Consolidated ~60 lines of duplicated list-building code across 4 detection paths (submenuMode, dword, subCursor, delayed entry) into single 20-line shared function. Removed dead variables: `s_submenuReentryNeeded`, `s_lastMenuPhaseForReentry`, `s_commandMenuPhase`, `s_prevMenuPhaseForSubmenu`. Removed 20-line commented-out phase fallback → 2-line note.
3. **Draw submenu false exit — FIXED**: `EnterSubmenu()` sets `s_prevSubmenuMode = 0x01`, and the submenuMode exit handler detects 0x01→0xFE as an exit. But Draw's internal phase transitions (target → spell list → Stock/Cast) briefly flip submenuMode to 0xFE, causing false exits. Fix: suppressed submenuMode exit when `s_submenuCommandId == 0x16` (Draw). Real Draw exits caught by cmdCursor change handler.
4. **EWM issues identified**: (a) Command menu interrupts damage TTS — need delay after attack animations. (b) Enemy attacks during menu navigation — ATB releasing during submenu transitions.

---

## Session 54 (2026-04-11) — Victory TTS phase detection + encoding fix (v0.13.24–26)

6 builds. Key breakthroughs:
1. **Fixed battle text encoding**: Was using kernel.bin encoding (0x02-0x1B=A-Z) which was WRONG. Battle text uses standard FF8 menu encoding (0x45-0x5E=A-Z, 0x5F-0x78=a-z). Same as enemy names in DecodeFF8String.
2. **Phase-to-text-ID mapping confirmed**: textID 22/23=EXP, 21=Items, 109=GF AP, 121=GF level-up, 127=ability learned. Smart BTXT logging (only log NEW/CHANGED text IDs) + F12 step markers mapped phases precisely.
3. **Phase-based victory TTS built**: BTXT hook sets phase flags → victory thread announces per-phase. GF AP ("GF received 2 AP.") confirmed working. Level-up detection via savemap EXP polling confirmed.
4. **sub_5348E0 NOT called during victory**: Hooked it (E9 JMP from FFNx), zero calls during mode 4. Victory code expands 0x0A control codes inline at 0x4A51F0.
5. **Memory-based TTS pivots corrected**: Aaron flagged twice that TTS must come from text renderer hooks, not memory address dumps. All-at-once memory reads leave blind players pressing through unannounced screens. Added to project memory #25.
6. **Victory screen layout documented from screenshots**: EXP shows Name/Lv/EXP Acquired/Current EXP/Next LEVEL. Items shown one-at-a-time. GF AP/Level-Up/Ability each in center box under "Raising GF" header.

## Session 53 (2026-04-11) — Battle text retrieval function identified (v0.13.14–22)

10 builds. BREAKTHROUGH: sub_47EC70 identified as get_battle_text(text_id) via disassembly. 261 call sites. Signature: looks up 16-bit offset at 0x1CF8B50+id*2, returns ptr to 0x1CF3E48+base+offset. 1774 calls during victory. Text IDs: 11 (per-frame graphics), 22/23 (one-time labels), 29/30/31 (per-frame character rows). Also identified sub_47EA30 (entity names), sub_5348E0 (ctrl code expansion), sub_4A37E0 (text display). tkmenu functions (4BD850/4BD630/4BD920) confirmed ZERO calls during battle — dead code.
> For current state, read `DEVNOTES.md` and `NEXT_SESSION_PROMPT.md`.
>
> **Versioning note**: Builds prior to v0.07.16 used the old format without the
> leading "0." (e.g. "v07.15" instead of "v0.07.15"). All versions in this file
> use the old format since that's what the logs and code said at the time.

---

## v07.xx: Menu TTS + Save Screen (2026-03-16)

### pMenuStateA Region Layout
- +0x00..+0x01: Transient input button flags (0x4000=UP, 0x1000=DOWN, 0x0010=confirm)
- +0x12..+0x19: Per-item animation/state flags (toggle 0x00↔0x40)
- +0x20: Active rendering callback index (per-frame oscillation — NOT cursor)
- +0x1CA: Rendering artifact. +0x1CE: Frame/tick counter.
- +0x1E6: **TOP-LEVEL CURSOR INDEX** (0-10)
- +0x1EC/+0x1ED: Submenu-related state

### Build Table v07.00–v07.15

| Build | Changes | Result |
|-------|---------|--------|
| v07.00 | MENUDIAG: menu_tts.cpp, dump pMenuStateA | pMenuStateA/B are input toggles |
| v07.01 | Per-frame WORD tracking at 20 offsets | +0x20 oscillates every frame |
| v07.02 | TTS using +0x20 as cursor | WRONG — repeats |
| v07.03 | Wide 512-byte scan, 500ms sampling | **FOUND CURSOR at +0x1E6** |
| v07.04 | Clean TTS using +0x1E6 | **WORKING** — all 11 items |
| v07.05 | Save slot entry diagnostic | Save screen leaves mode 6 |
| v07.06 | Global mode tracking | Title→Continue stays mode 1 |
| v07.07 | F12 2048-byte pMenuStateA scan | Only noise — cursor NOT here |
| v07.08 | F12 ff8_win_obj windows scan | Zero changes — not used |
| v07.09 | F12 MDT/GCW call counts | ~90-210 MDT/500ms confirmed |
| v07.10 | F12 GCW text capture | Hex captured, wrong encoding |
| v07.11 | Menu font decoder (sysfnt/Deling) | **CONFIRMED** — all text decodes |
| v07.12 | GCW text parsing "in use: Slot N" | Only fires after pressing X |
| v07.13 | Revised parsing, skip "Checking" | Same problem |
| v07.14 | GCW decode diagnostic | **KEY**: Text identical regardless of cursor |
| v07.15 | F12 4KB memory scan around pMenuStateA | Cursor NOT in pMenuStateA region |

### Save Screen Investigation Details (v07.06–v07.15)

**Save screen text rendering**: Uses standard menu_draw_text / get_character_width pipeline. GCW buffer fills to 1024 bytes/500ms with ~90-210 MDT calls per interval.

**GCW text patterns**: "LoadSlot 1FINAL FANTASY Slot 2FINAL FANTASY GAME FOLDER..." — repeats per frame, IDENTICAL regardless of cursor position.

**Memory scan results (v07.15)**: 4KB around pMenuStateA, 500ms intervals. Only 4 offsets changed: tick counter (-0x8), rendering toggles (+0x1FD, +0x1FE, +0x5E8). None correlated with arrow presses.

**Empirical glyph mapping** (v07.10): A=0x25, F=0x2A, I=0x2D, L=0x30, S=0x37, space=0x00.

---

## v06.xx: Field Navigation — Drive Reliability (2026-03-14 to 2026-03-15)

### Build Table v06.02–v06.22

| Build | Key Change | Result |
|-------|-----------|--------|
| v06.02 | Horizontal wall-parallel fix | Quistis+corridor reachable |
| v06.03 | A* trigger-line exemption for exits | Exits pathfinding works |
| v06.04 | Trigger exemption for events + preserve wp | Recovery doesn't lose waypoints |
| v06.05 | Trigger-safe recovery wiggle | Superseded by v06.06 |
| v06.06 | Edge-midpoint recovery replaces wiggle | Fires correctly, wall-stuck remains |
| v06.07 | Micro-nudge perpendicular to wall | Breaks wall contact, reveals oscillation |
| v06.08 | Overshoot detection + NavLog wiring | Fixes corridor oscillation |
| v06.09 | Stuck grace period + cancel threshold | Garden exploration test |
| v06.10 | CoordSample wired + progress detection | Recovery wp advance bug |
| v06.11 | wpIdx>2 guard attempt | Still too aggressive |
| v06.12 | genuineProgress gate | Fixed recovery escalation |
| v06.13 | PROJDIAG + camera analysis | Confirmed no projection needed |
| v06.14 | Per-field heading calibration | bg2f_1 NPC ARRIVED first time |
| v06.15 | Relaxed stuck thresh, remove arrow cancel | "Better than ever" |
| v06.16 | Simplified recovery pipeline | Clean re-path+nudge cycle |
| v06.17 | Corridor steering + wall bias | Per-tick edge midpoint target |
| v06.18 | Trigger proximity exemption | Target-side lines exempt |
| v06.19 | Narrow corridor bias reduction | Still oscillates |
| v06.20 | Wall bias disabled entirely | Corridors stable |
| v06.21 | Talk radius expansion (2.5×) | Elevator corridor NPC solved |
| v06.22 | Corridor steering trigger check | Final navigation build |

### MAJOR FINDING: Walkmesh = Entity Coordinates (2026-03-15)
.id walkmesh stores X,Y already in entity/screen space. No 3D→2D projection. Confirmed via CoordSample empirical data: mean diff=(14,34) std=(41,71). The .ca camera file is for background rendering only.

### v06.09 Garden Exploration Test Results
- Arrived: bggate_5 cdfield8, bggate_1 transitions (×2)
- Gave up: bg2f_1 NPC (×2), bggate_2 NPC (micro-oscillation)
- Open bugs: micro-oscillation (tri 126↔127), trigger-line crossing during drives

---

## v05.xx: Field Navigation — Entity Catalog + Pathfinding (2026-03-11 to 2026-03-14)

### Build Table v05.47–v05.96

| Build | Key Change | Result |
|-------|-----------|--------|
| v05.47 | SYM names + INF gateways | Working |
| v05.50 | pFieldStateBackgrounds | Working |
| v05.53 | Model-based character names | Quistis correct |
| v05.56 | SETLINE opcode hook | Coords at offset 0x188 |
| v05.61 | Y-axis fix (0x194 not 0x198) | Positions correct |
| v05.64 | A* walkmesh (FF7/PSX inline format) | Paths found, steering broken |
| v05.67 | Inverted heading mapping | First Arrived! |
| v05.70 | Screen filtering via SETLINE | 5 hidden back, 1 front |
| v05.76 | Heading: Y inverted, X direct | Left/right correct |
| v05.84 | Fake gamepad injection | Analog movement confirmed |
| v05.89 | get_key_state hook | **Analog steering works** |
| v05.94 | Vertex dedup + funnel fix | FindPortal succeeds |
| v05.96 | triB center cross product | Portal assignment fixed |

### Entity Classification (bgroom_1)
| Ent | Model | SYM | Actual | Catalog |
|-----|-------|-----|--------|---------|
| ent0 | -1 | Director | Controller | filtered |
| ent1 | 0 | Squall | Squall | player |
| ent2 | 8 | Squall_u | Quistis | "Quistis" |
| ent3-6 | 10-15 | various | NPCs | "NPC" |

### Screen Filtering Architecture
SETLINE trigger lines as screen boundaries. Cross product sign test. Layers: IsSeparatedByTriggerLine → screenFiltered[] → transition/event detection → gateway filtering → crossing detection.

---

## v05.97–v06.01: Navigation Pipeline Overhaul

| Build | Key Change |
|-------|-----------|
| v05.97 | Wall-parallel portal skip (epsilon=5.0) — too aggressive |
| v05.98 | Pre-skip nearby waypoints |
| v05.99 | Corridor-center steering bias — broke bgroom_1 |
| v06.00 | Tightened epsilon to 0.5 |
| v06.01 | New pipeline: wall-parallel skip + agent-radius shrinking + BFS island check |

---

## v0.07.93–v0.07.99: Interactive Objects, Exit Naming, INF Gateways

- **v0.07.99**: SET3 push stack diagnostic. `hasPshmCoords`/`pshmAddrX/Y/Z` added.
- **v0.07.98**: Interactive object classification infrastructure.
- **v0.07.97**: Model≥10 NPC fix (push-before-talk timing).
- **v0.07.95–96**: Exit naming, world map labels, redundant JSM exit suppression.
- **v0.07.93–94**: INF gateway parser (Deling format), dedup, catalog integration.

---

## Session 84 cont (2026-04-25) — v0.13.82 anim-flag edge fix + semantic miss correction

### Why this build existed

Two focused fixes based on BAT6 findings:

**1. Anim-flag screenshot was firing on the wrong edge.** v0.13.81 fired the audit screenshot on the 0→1 transition of `0x01D280C0`. BAT6 proved this was too early — the engine raises the anim flag when damage *computation* begins, before the damage-number sprite is composited. Verified by examining `sprite_animflag_172733_862_s3=-80.png` (Squall walking, no damage number visible). Fix: move `FireAnimFlagScreenshot()` from the 0→1 detection block to the three `FlushHPAnnouncements` branches (anim-done at 1→0 edge, anim-timeout, heal-timeout).

**2. "Miss on X" was semantically wrong for spell no-effect events.** BAT5/6 showed `sub_4877F0` kind=4 a3=0x9 fires for cases like Sleep-on-already-asleep and Silence-on-immune. The engine renders **nothing** on the target. We were saying "Miss on Grat" which doesn't match the visual. Fix: change TTS to "No effect on X" for that path. Keep `HookedPopupSprite` text_id=0xED saying "Miss" — that IS a real miss. New Validate kind: `no-effect` (was `miss-spell`). Log tag: `[SPELL-NOEFFECT]` (was `[SPELL-MISS]`). Physical miss kind stays `miss-popup` — unchanged.

---

## Session 85 (2026-04-25) — v0.13.83 timeout-based no-effect watchdog

v0.13.82 BAT exposed a third no-effect case the kind=4 hook misses entirely: **ally-already-status**. When a player casts a status spell on an ally who already has that status (e.g. Sleep on already-asleep ally), the engine short-circuits the spell-result path BEFORE calling `sub_4877F0`. The kind=4 hook never fires, no announcement plays. Verified at 15:23:56 in BAT log: `SPRITE-SPAWN-SEQ` fires for `sub_48E830`, `TEXT-SPRITE-SEQ` "Cast Sleep" fires, sprite despawns 8s later, NO `[SPELL-RESULT]`, NO `[STATUS-Q]`.

**Fix:** Hook the action-announce sprite spawn (`sub_48E830`, already hooked) for player magic casts (`retaddr=0x0048594E AND arg[1]==0x16`). Snapshot target HP at cast and start a 6-second per-frame watchdog. Activity indicators monitored: (a) HP delta, (b) status queue non-empty for target slot, (c) kind=4 hook already announced via `s_lastSpellMissAnnounceTick`. If ALL false at watchdog expiry → announce "No effect on <target>".

New file: `src/battle_tts_noeffect.inl` (~250 lines). Tunables: `NOEFFECT_WATCHDOG_MS=6000`, `NOEFFECT_KIND4_DEDUP_MS=8000`. Coordination: piggybacks on `s_lastSpellMissAnnounceTick[slot]` (the kind=4 path's dedup array) for both directions — reads to skip if kind=4 already spoke, writes to silence late kind=4 fires.

Scope (v1): player-cast magic only (`retaddr=0x0048594E`), single-target only. Enemy casts deferred (rarely produce no-effect). Multi-target Reflect/Wall scenarios deferred.

---

## Session 86 part 1 (2026-04-25) — v0.13.84/85 HEAL-DIAG diagnostic for heal-on-cap address hunt

Aaron asked: when Cure is cast on a damaged target and overcaps (e.g. Cure for 344 on 488/560 HP target heals to 560, displays "344" but only +72 HP delta), do we announce 344 or 72? Existing `FlushHPAnnouncements` uses `max(abs(delta), displayValue)` which would give 344 IF `0x01D2834A` holds heal values. Hypothesis untested.

**v0.13.84 diagnostic:** Added `HealDiag_DumpDisplayRegion(tag)` helper to `battle_tts_hp.inl` — dumps 32 bytes (16 uint16s) centered on `BATTLE_DAMAGE_DISPLAY_ADDR` at four call sites in `PollHPChanges`: anim-start (0→1), anim-done (1→0), anim-timeout, heal-timeout. All four sites lived inside `if (s_anyHpPending)` block.

**v0.13.84 BAT result:** Cure on full-HP Grat produced "No effect on Grat" (the v0.13.83 watchdog firing) but NO HEAL-DIAG output. Diagnostic was placed inside the same HP-delta-gated block we were investigating around — gating defeated the purpose for max-HP cap events.

**v0.13.85 fix:** Added ungated anim flag transition tracker (`s_healDiagPrevAnim`) at top of `PollHPChanges`. Fires HEAL-DIAG with tags `ungated-start` and `ungated-done` on every anim flag 0→1 and 1→0 transition regardless of HP delta state. Kept the four existing in-block calls.

**v0.13.85 BAT confirmed:** `0x01D2834A` holds heal values too. Cure on damaged Grat (488→560, delta+72): `[HEAL-DIAG] ungated-start: display@idx8=344`. HP-TRACK detected delta+72 BEFORE the watchdog could snapshot (engine pre-applies HP). FlushHPAnnouncements correctly used `max(72, 344)=344` and announced "Grat recovers 344 HP." — existing path worked.

But TWO new bugs surfaced from this BAT, requiring v0.13.86.

---

## Session 86 part 2 (2026-04-25) — v0.13.86 heal-on-cap announcement fix

v0.13.85 BAT exposed two bugs in the watchdog/heal interaction:

**Bug 1: Watchdog double-announces "no effect" after a successful heal-on-cap.** The engine pre-applies HP (488→560) BEFORE `sub_48E830` fires. `NoEffect_RecordSnapshot` captures `targetHpAtCast=560` (post-heal). Six seconds later, the watchdog reads HP=560, sees no change, fires "No effect on Grat" — even though FlushHPAnnouncements correctly said "Grat recovers 344 HP" earlier. Double-announce.

**Bug 2: Full-HP heal target gets nothing announced.** Cure on full-HP target. HP delta = 0, `s_anyHpPending` stays false, `FlushHPAnnouncements` not called for that slot. Watchdog sees no HP change, no status, fires "No effect on Grat." Aaron's principle: announce what's shown, not what HP did. Engine wrote 344 to displayValue — we should speak it.

**Fix design** (one cohesive set of changes, both bugs share the mechanism):

1. `battle_tts_hp.inl` adds `s_displayValuePrevFrame` (uint16_t, updated at end of every PollHPChanges call). Mod thread snapshot. Game thread reads this from `NoEffect_RecordSnapshot` to capture pre-action displayValue — since hooks fire BETWEEN mod thread polls, the value seen by the hook is from the LAST mod poll, which precedes the engine's same-frame pre-write.

2. `battle_tts_hp.inl` adds `s_lastFlushAnnounceTick[BATTLE_TOTAL_SLOTS]`. `FlushHPAnnouncements` stamps `s_lastFlushAnnounceTick[slot] = now` after each damage or heal announcement.

3. `battle_tts_noeffect.inl` adds `displayValueAtCast` field to `PendingSpellNoEffect` struct, captured via `s_displayValuePrevFrame` at `RecordSnapshot` time.

4. `battle_tts_noeffect.inl` `PollPendingSpellNoEffect` adds two new logic blocks:
   - **Step 1c (Bug 1 fix):** Check `s_lastFlushAnnounceTick[slot] >= castTick`. If a flush spoke for this slot during the watchdog window, mark `sawHpChange=true`. Suppresses no-effect.
   - **Step 4b (Bug 2 fix):** At expiry, if no other effect observed, read current displayValue. If `displayValue > 0 AND displayValue != displayValueAtCast`, announce "X recovers N HP." via `Validate_AnnounceEvent("heal-on-cap", ...)`.

**v0.13.86 BAT verified both fixes:**
- 21:09:39 Quistis Cure on Quistis (771→861, partial cap, display=345): `[VALIDATE] kind=heal slot=0 value=345 trigger=new-turn tts="Quistis recovers 345 HP."` (existing flush). Then 21:09:45: `[NOEFFECT-WATCH] slot=0 had effect (hp=1, status=0), no announce` (Bug 1 fix via Step 1c). No double-announce.
- 21:09:54 Squall Cure on full-HP Grat (display=297, HP delta=0): `[VALIDATE] kind=heal-on-cap slot=3 value=297 trigger=watchdog tts="Grat recovers 297 HP."` (Bug 2 fix via Step 4b; displayBase=345, current=297).
- Zero `[SPELL-NOEFFECT]` lines in entire log = no false positives.

Known limitation: same-value consecutive cap-heals (Cure 344 → Cure 344 on same target with no intervening action) won't fire — baseline equals current. Acceptable.

---

## Session 86 part 3 (2026-04-25) — v0.13.87 cleanup pass

With v0.13.86 verified working, removed the HEAL-DIAG diagnostic infrastructure that served its purpose in v0.13.84/85:
- `HealDiag_DumpDisplayRegion` helper function
- `s_healDiagPrevAnim` ungated tracker static
- Block in `PollHPChanges` that reads anim flag and calls HealDiag at transitions
- Four redundant in-block calls (anim-start, anim-done, anim-timeout, heal-timeout)

Retained the v0.13.86 fix logic intact:
- `s_displayValuePrevFrame` (still updated at end of PollHPChanges)
- `s_lastFlushAnnounceTick[BATTLE_TOTAL_SLOTS]` (still stamped by FlushHPAnnouncements)
- Watchdog Step (c) and Step 4b (battle_tts_noeffect.inl)

Also updated KNOWN LIMITATIONS comment block in `battle_tts_noeffect.inl` to remove the now-fixed "Cure on full-HP target" entry, replaced with note about same-value consecutive cap-heals being the new limitation.

No behavior change. v0.13.87 is purely a cleanup version.

---

## Sessions 88–94 (2026-04-25 to 04-26) — sprite-visibility detection: failed investigation

Fourteen-session arc (80–94) attempting to detect when the floating damage-number sprite becomes visible on screen. **Conclusion: not feasible with the approaches tried.** v0.13.90 animation-end timing remains the working baseline.

### v0.13.88 — text-writer hook call counter

Added `[TEXT-CTR]` diagnostic counting `sub_495280` and `sub_4952F0` invocations per damage event. Confirmed both fire during action-announce paths but their call counts and timing did not correlate with floating damage-number visibility. Hypothesis A (text writers handle floating numbers) **falsified**.

### v0.13.89 — no-effect timing queue

Moved no-effect / heal-on-cap announcements through `NoEffect_QueueAnnouncement` so they flush on anim flag 1→0 like status-apply does. Aaron BAT-confirmed: no-effect TTS now lands at the same moment status TTS lands. Codified as invariant #16.

### v0.13.90 — removed `HP_ANIM_TIMEOUT_MS` safety net

The 4-second `HP_ANIM_TIMEOUT_MS` was firing prematurely on long spell animations (Fire ~6s, Meteor, Ultima). Removed it entirely; PollHPChanges now trusts the 0x01D280C0 anim flag's natural 1→0 transition exclusively. BAT-confirmed working from 1.2s physical to 6s+ spells. Codified as invariant #17. **This is the current shipped damage-timing behavior.** `HP_HEAL_TIMEOUT_MS = 1500ms` retained for the no-anim fallback path only.

### v0.13.91–92 — popup-table wide-net diagnostics

Added `[POPUP-LIFE-DIAG]` and `[POPUP-TIME-DIAG]` casting a wider net on the 0x01D280C4 popup table. BAT showed the popup table only ever holds action-announce records ("Cast Fire", "Draw Sleep", etc.) — it is not the path floating damage numbers take. Sessions 80–92's popup-table thesis **falsified**.

### v0.13.93 — architectural pivot to sprite allocator (FAILED)

Removed all popup-table monitoring code. Hooked `sub_482C90` (identified in session 83 research as the sprite allocator) and watched for `CALLBACK_COMPLEX_UPDATE` (0x48E620) allocations as the damage-sprite signal.

BAT result: `[SPRITE-HOOK] Installed at 0x00482C90` succeeded, but the hook **never fires during damage events**. `[DISPATCH] sub_483470: calls=0 blocks=0 passes=0` across full battles. Animation logs continued to show `trigger=anim-done`. The session-83 identification of `sub_482C90` as the damage sprite allocator was either incomplete or wrong.

### v0.13.94 — failure analysis and decision

No new code. Documented exhaustive failure across all four investigated vectors:

1. Popup-table monitoring (sessions 80–92) — only captures action announcements
2. Text-writer hooks (session 78+, v0.13.88) — fire but don't correlate with sprite visibility
3. Sprite allocator hook (sessions 93–94) — installs but never fires
4. Alternative sprite tables (0x01D28DF0 etc., session 81/83) — all walked, all dead-ends

Decision: stop chasing sprite visibility through engine code paths. Session 95 will attempt graphics-pipeline interception (gl draw calls / framebuffer ROI diff) as a single time-boxed final attempt; if that fails, accept v0.13.90 as the final damage-timing solution and pivot to Limit Breaks, buff announcement, and GF audio descriptions.

Invariants #18 (sprite system pivot) and #19 (sprite detection not feasible via engine paths) recorded.

---

## Pre-v05: Dialog, FMV, Title Screen

### v04.36: Field Dialog TTS
All MES/ASK/AMES/AASK/AMESW/RAMESW opcodes hooked. show_dialog hook for tutorials/thoughts. Naming screen bypassed via enableGF() calls.

### v03.00: FMV Audio Descriptions + Skip
ReadFile EOF hook for FMV skip. WebVTT-timed audio descriptions via SAPI.

### v02.00: Title Screen TTS
Cursor tracking for New Game/Continue/Credits.
