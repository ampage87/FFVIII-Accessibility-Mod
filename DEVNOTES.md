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
- Battle TTS: command menus, sub-menus (Magic/GF/Draw/Item), EWM, GF fire prevention, victory screen (screenshot-based pipeline), damage/HP announcements (impact-time via sub_5068B0 render hook = Flag #2; sub_48EF80 popup-create = Flag #1 = diagnostic publisher only, never a trigger; anim-flag-fall = Flag #3 = catch-all fallback. v0.14.38 design.)
- Junction menu TTS, save/load screen TTS, menu system TTS
- Multi-channel logging system (6 domain logs); `.inl` file splitting
- Full FF8_EN.exe disassembly reference at `Game Files/disassembly/`

**Current build: v0.14.44 — GF summon audio descriptions, BAT PASSED (session 74)**

Aaron triggered Ifrit and Shiva in-game; both audio descriptions announced correctly. Trigger fires from `PollBattleMagicId()` in `battle_tts_ewm.inl` on the rising edge of `battle_magic_id` matching a known GF effect ID, cues stream on Channel 2, and playback stops cleanly when `battle_magic_id` reverts (covers natural end + R1+L1 skip). 18 VTTs cover the 16 junctioned GFs plus Phoenix and Odin.

Usability issue surfaced during BAT: GF SFX (Ifrit roar, Shiva ice shatter, etc.) is loud enough to mask the TTS. Aaron flagged this as the next priority — see NEXT_SESSION_PROMPT for the SFX volume + auto-duck plan.

Ready to push v0.14.44 to GitHub via Utilities/push_to_github.vbs whenever Aaron is ready.

**v0.14.43.1: GitHub catch-up complete.** Two commits behind us:
- `6d28211c` (v0.14.43): items submenu architectural fix + cumulative cleanup of ~50 backlog builds + diagnostic removal + Utilities/ folder.
- `af841fef` (v0.14.43.1): structural cleanup — removed stale `FF8_OriginalPC_mod/` subdirectory.

**Tooling: GitHub push utility built (session 73)** in `Utilities/` (gitignored): `push_to_github.bat` (worker with token-embedded URL auth, add → commit → pull --rebase → push → tag), `push_to_github.ps1` (Windows Forms GUI), `push_to_github.vbs` (silent launcher), `README.md` (setup notes). Token scrubbed from `Logs/git_latest.log` after every run. Two successful pushes this session validated the tool. Future pushes are one-click.

Non-trivial bug fixed during session: cmd.exe quote-stripping rule mangled the original `Start-Process -ArgumentList` invocation so the .bat never ran (no log was created, hence no error visibility). Fixed by switching to `[System.Diagnostics.ProcessStartInfo]` with explicit outer wrapping quotes around the `/c` command, plus pre-creating the log file in PowerShell so View Log always has something to show even on early failures.

**v0.14.43 cleanup (DONE in session 73):**

- ~~Strip `[ITEM-DUMP]` block in `src/battle_tts_menu.inl`~~ — done.
- ~~Strip `[BATTLESPEAK-DIAG]` from `src/battle_tts.cpp` and `[SPEAK-DIAG]` from `src/screen_reader.cpp`~~ — done.
- `[ITEM-LIST]` retained (low overhead, one shot per submenu open, useful for future regression checks).
- F12 stays free for the next diagnostic.
- Cancel deep research prompt at `Plan & Research Documents/deep_research_battle_items_arrangement.md` — no longer needed (Aaron may keep for archival).

---

v0.14.41's `HuntBattleItemInventory()` ran on two separate items-submenu opens (BAT 1: 4 items including Phoenix Down at 13:32:41; BAT 2: 3 items after Phoenix Down was used at 13:33:23). Across **both** runs:

- **Direct byte-pair scan**: ZERO matches in any of the 5 scan ranges. The packed `{id, qty}` sequence for the active battle items does not exist in compact form anywhere in 0x01CFE000–0x01D00000, 0x01D27000–0x01D2A000, 0x01D76000–0x01D78000, 0x01D8D000–0x01D8F000, or 0x01D90000–0x01D95000.
- **Index-sequence scan**: BAT 1 zero matches. BAT 2 one stride=2 match at 0x01CFE345 — almost certainly coincidence (char[3] + offset 0x95, surrounding bytes `00 00 03 8D 02 00 00 10 27 00...` don't form a coherent {invIdx, qty} struct; qty would have to be 0x8D=141 for Remedy which has actual qty 2).
- **Pool-pointer dereference**: Found 3 valid buffer pointers but they hold render-pipeline data (FF8 text glyphs, screen Y-coordinates, uint16 menu-option sequences with FFFF sentinels), NOT item content.
- **Display struct at 0x01D8DFF4** (the v0.10.105 documented location) is empty — `dsPopulated=false`, `[ITEM-LIST] Filtered mode` confirms.

**Aaron's BAT 2 navigation (the smoking gun):** cursor went 0 → 4 → 5 → 6 → 7 → 4 → 8 → 9 (right-arrow page jumps + down-arrow). At cursor=9 (visible page 3 slot 2 by `cursor/4` pagination), Aaron reports the engine renders **Elixir** on screen, while our code says "Empty page 3 item 2" because s_turnItemList only has 3 entries.

**Aaron's architectural clarification:** "You have two totally separate inventory orders — one for all items and one for battle items." The user customizes both independently via the field menu (Items > All Items vs Items > Battle). The All-items arrangement is what we currently read from 0x1CFE77C. The **Battle arrangement we have NOT located** is what the engine actually uses for the in-battle items submenu rendering.

This rules out every theory we had. The engine's battle items list is neither the full inventory, nor a battle_order-derived layout, nor a compact-non-empty list.

**v0.14.42 plan: deep research first, then build.** Per Aaron's protocol when source/logs/files are insufficient, we hand off to ChatGPT. The deep-research prompt is saved at `Plan & Research Documents/deep_research_battle_items_arrangement.md`. After ChatGPT replies, validate the claimed address against runtime memory, then write the v0.14.42 read path.

**v0.14.42 will also:**
- Strip the `[ITEM-HUNT]` diagnostic block (mission complete — proved the negative).
- Keep `[ITEM-DUMP]` until v0.14.42's read path is BAT-verified.
- Keep F12 reserved for next diagnostic.

---

**v0.14.41 architecture pivot — hunt for the compressed inventory:**

`HuntBattleItemInventory()` runs once per items submenu open, after `DumpItemMenuState()`. Logs to `ff8_battle.log` under `[ITEM-HUNT]`.

Three search modes:

1. **Direct {id, qty} byte-pair scan** at strides 2/4/8. Looks for the test battle's items (Phoenix=07 02, Potion=01 0A, Remedy=10 02) appearing contiguously in: battle-ui (0x01D76000–0x01D78000), display-struct region (0x01D8D000–0x01D8F000), battle-entity (0x01D27000–0x01D2A000), savemap (0x01CFE000–0x01D00000), post-display (0x01D90000–0x01D95000).
2. **Inventory-index sequence scan** at strides 1/2/4. For each turnItemList entry, finds its position in the full inventory (Phoenix=inv[1], Potion=inv[8], Remedy=inv[2] → bytes 01 08 02), then scans for that sequence. Catches the case where the engine's compact list stores indices, not direct {id,qty}.
3. **Pool-pointer dereference**. For every pool node, every uint32 at offset 0x14+ that points into 0x00400000–0x01F00000 is treated as a candidate buffer pointer; 32 bytes at the target are dumped. Catches the case where the controller allocates and stores a pointer to its compact inventory.

Each match logs address, stride, range tag, and 32 bytes of context. After Aaron's BAT we'll see exactly where the compressed inventory lives.

**v0.14.41 announce path — unchanged:** v0.14.40's boIdx logic is preserved. We don't refactor announces until we have ground truth on the address.

**Expected v0.14.41 BAT log markers:**

- One `[ITEM-HUNT]` block per items submenu open
- The hunt signature line shows our test items as `{07 02} {01 0A} {10 02}`
- The index signature line shows `01 08 02` (or whatever Aaron's inventory positions are)
- Successful matches report addresses we can plug into a v0.14.42 read path
- `Totals: direct=N index=N` summary at the end

**If hunt returns zero matches**, deep research will be required on the FF8 battle item display path in this FFNx build.

**v0.14.36 (previous build) BAT FAILED diagnosis:** v0.14.36 attempted Bug B fix by inverting primary/fallback ordering — sub_48EF80 (popup-create) primary, sub_5068B0 (render hook) fallback. The inversion was the regression. v0.14.36 BAT log showed popup-create fires 1–5 SECONDS before dmg-render in every event because sub_48EF80 fires at popup-data-struct creation, well before the damage sprite paints.

**Bug B (damage timing) — what the log actually shows:**

In the v0.14.36 BAT log (5-event multi-target battle), popup-create fires consistently 1–5 SECONDS before the corresponding dmg-render fires:

| Event | popup-create | dmg-render | gap |
|------:|:-------------|:-----------|----:|
| 2 (Biggs 62) | 22:10:29 | 22:10:30 | ~1s |
| 3 (Squall 47) | 22:10:30 | 22:10:33 | ~3s |
| 4 (Biggs 64) | 22:10:41 | 22:10:43 | ~2s |
| 5 (Biggs 114) | 22:10:50 | 22:10:55 | ~5s |
| 6 (Selphie 47) | 22:10:56 | 22:11:00 | ~4s |

With popup-create primary, every announce fires at ANIM-UP time — 1–5s BEFORE the visible damage sprite paints. The `age=0 ms` metric measures freshness-of-tick (signal published this poll), NOT sync-with-visual. The two `dmg_*_hook.inl` headers and DEVNOTES key learnings all warned this would happen: sub_5068B0 is the hook whose timestamp aligns with the visible-on-screen YELLOW ROI spike; sub_48EF80 fires at popup-data-struct creation, before the sprite is composited. The v0.14.4 reversal (also documented in the popup-hook header as 'never promote on partial verification') was for exactly this reason.

**Bug A (item submenu) — what the log actually shows:**

The `[ITEM]` log lines fire correctly for every cursor move with the right text (`Phoenix Down, quantity 2, page 1, item 1`, `Potion, quantity 10, page 1, item 2`, etc.). BattleSpeak is being called with the right buffer, PRIO_MENU, and interrupt=true. But Aaron only hears the cmd-nav 'Item' announce — no per-item speech reaches him. The announce code path (battle_tts_menu.inl ~line 1133, the `s_submenuCommandId == 0x17` block) is identical between GitHub HEAD v0.13.63 (which worked) and local v0.14.36 except for the v0.14.35 filtered-mode data fix (`s_turnItemList[sc]` vs `inv[sc*2]`). That change touches the data lookup, not the speech call. Something between BattleSpeak and SAPI audio is dropping these specific calls. Source review alone can't isolate it without runtime evidence.

**v0.14.35 Bug A data-lookup fix is retained** (filtered-mode reads from `s_turnItemList[sc]` — confirmed working by the `(src=turnItemList)` log tag firing for the right items). The v0.14.35 fix is correct as far as it goes; the v0.14.36 BAT only proves there's an additional, separate audio-output regression on top of it.

---

**v0.14.37 fix plan**

*Bug B (damage timing) — actual fix:*

In `src/battle_tts_hp.inl`, revert v0.14.36's primary/fallback inversion. Restore the v0.14.10 / v0.14.32 ordering: sub_5068B0 (render hook, impact-time, timestamp aligns with YELLOW ROI spike) is the production trigger; sub_48EF80 (popup-create, ANIM-UP time) goes back to diagnostic-only fallback. Keep the v0.14.36 `TRIGGER_FRESHNESS_MS = 500` freshness gate but apply it to the render-hook tick — this closes the v0.14.35 Ifrit-kill stale-tick concern (no-fire events leaving stale signal) without inverting the proven hook order. Keep the v0.14.35 consume-on-read also.

The v0.14.32 BAT recorded `yellowLeadVsAnimFlag=-109ms` with sub_5068B0 primary — announce essentially synchronized with visible damage. That's the target metric to verify in v0.14.37 BAT.

*Bug A (items not heard) — diagnostic in v0.14.37:*

Add targeted logging to capture runtime evidence of where the per-item BattleSpeak calls are being dropped between BattleSpeak entry and SAPI audio output. Two additions:

1. In `BattleSpeak` (src/battle_tts.cpp): log `[BATTLESPEAK-DIAG]` with text (truncated to 40 chars), prio, interrupt flag, `s_currentSpeakPriority` before-and-after, and which branch was taken (interrupt/priority-pass vs queue). Proves whether BattleSpeak entered at all and what state it saw.
2. In `ScreenReader::Speak(const char*, bool)` (src/screen_reader.cpp): log `[SPEAK-DIAG]` with backend, the SAPI HRESULT for both the purge call and the speak call, the NVDA `fn_speakText` and `fn_brailleMessage` return values. Proves whether SAPI/NVDA accepted the text and what they returned.

Fix in v0.14.38 follows from what the diagnostic shows: if BattleSpeak is being called with the right text but SAPI returns a failure HRESULT, the bug is in the SAPI/NVDA path (timing race between rapid interrupts, voice handle state, etc.). If BattleSpeak isn't being called at all, the bug is in the menu announce block's flow. If everything looks normal in the logs, the bug is downstream of SAPI's accept (e.g. WASAPI mixer state or a separate cancel firing after).

*Coordinated DEVNOTES + NEXT_SESSION_PROMPT update:*

This Step 3 plan replaces the earlier-this-session false 'BAT PASSED' edits in both files. The session checkpoint rule applies — these get updated again at the v0.14.37 version bump and again after Aaron's BAT.

**Known issue:** JAWS intercepts game keys (arrows, Backspace) until user presses Insert+3 for passthrough. NVDA does not have this issue. Not a mod bug. Low priority.

---

**Current state**

Session 67/68/69/70 — v0.14.40 implemented and ready for BAT. Pool-node approach confirmed broken in this FFNx build (40/40 NULL). v0.14.40 stops relying on it as primary, fixes the visual page/slot bug via boIdx tracking, and adds a comprehensive one-shot diagnostic dump. Once the BAT log includes the `[ITEM-DUMP]` block, we'll have ground truth on whether `battle_order[]` has gaps and where (if anywhere) the ITEM_HANDLER actually lives in pool memory in this build.

**v0.14.36 BAT failure diagnosis (2026-04-27 22:09–22:11, 5-event Biggs+Wedge battle):**

- Bug B: popup-create primary fires 1–5s before dmg-render in every event. Inversion of the proven v0.14.10/v0.14.32 ordering caused the regression. Aaron hears damage announces well before the visual sprite appears — confirmed the v0.14.4 reversal scenario applies regardless of the freshness gate.
- Bug A: cmd-nav 'Item' speaks correctly. Per-item announces fire `[ITEM]` log lines with correct text and call BattleSpeak with PRIO_MENU + interrupt=true. Aaron hears no per-item speech. Source diff against GitHub v0.13.63 (last working version) shows only the v0.14.35 data-lookup change in the announce path. Audio-output regression is real but source review alone cannot isolate the cause; runtime diagnostic is needed.

**Next session priorities (in order):**

1. **v0.14.40 BAT analysis.** Pull the `[ITEM-DUMP]` block from `ff8_battle.log`. Two questions to answer:
   - Does `battle_order[]` have gaps (FF bytes between non-FF entries) in Aaron's saved arrangement? If yes, the boIdx fix should already produce correct page/slot announces and the dump confirms the layout. If no, we need a different theory — maybe a separate "display order" array somewhere else.
   - Is `ITEM_HANDLER 0x4F81F0` reported FOUND anywhere in the pool dump? If yes, update `HANDLER_OFF_A`/`HANDLER_OFF_B` to the new offset and pool-node primary path comes back to life. If no, the FFNx build has a different handler entirely — use the code-pointer DWORDs in the dump to identify candidates.
2. **v0.14.41 — either new pool-node offset OR drop the pool-node path entirely.** Based on dump findings.
3. **v0.14.42 — strip remaining diagnostic logging** once items submenu fully working: remove `[BATTLESPEAK-DIAG]` from `src/battle_tts.cpp`, `[SPEAK-DIAG]` from `src/screen_reader.cpp`, `[ITEM-DUMP]` from `src/battle_tts_menu.inl`. Keep `[ITEM-LIST]` (useful for ongoing).
4. **GitHub push** (deferred until items submenu fully resolved). Single comprehensive commit v0.13.63→v0.14.42+ covering: build recovery + production trigger fix + sprite/spell hooks + Item submenu false-exit fix + items audio fix + items ordering fix + visual page/slot fix + damage timing fix.
5. Bug 3 from v0.14.31 BAT — Magic/GF submenu auto-announce inconsistent. May already be self-resolved by v0.14.32+ timing fixes; retest first.
6. Bug 4 from v0.14.31 BAT — Number key 2 announced GF (Shiva) details instead of Squall's HP. Stale `s_gfHpSubstitutionActive[1]` or `s_gfSummonedIdx[1]`. Edge case, lower urgency.
7. After bugs 3-4 confirmed: Persistent accessibility settings across play sessions. Quistis Blue Magic spell-list ordering investigation. Remove party members from entity catalog. X-ATMO92 chase scene accessibility.

**Build-recovery hook audit checklist (codified from v0.14.24→v0.14.34 saga):** When rebuilding a .cpp from older GitHub HEAD and re-wiring newer .inl files, ALWAYS audit:
  (a) every `Install*` defined in any wired .inl has a corresponding call in OnBattleEnter (or equivalent lifecycle entry)
  (b) every `Reset*` has a corresponding call in OnBattleEnter's reset block
  (c) every `Poll*` has a corresponding call in Update()
  (d) every `Hook*_Reset` (e.g. dedup state) has a corresponding call in OnBattleEnter's reset block

**Comprehensive wiring audit (2026-04-27, post-v0.14.34):** Performed full audit of all `Install*`/`Reset*`/`Poll*` functions across every battle/menu/field .inl file post-v0.14.34. Result: **NO additional user-facing regressions found.** The v0.14.32-v0.14.34 fixes brought all production code paths back online. Seven diagnostic-only functions remain orphaned, but their data was already used to build the production fixes they were investigating, so they're dead code rather than missing features:

  1. `Validate_Reset()` (validate.inl) — intentionally empty per v0.13.81 architecture (no per-battle state to reset)
  2. `Dmg_BP_Init/OnBattleEnter/Shutdown` (dmgbp.inl, v0.14.2) — HW write BP that found the damage display write site at sub_48EF80+0x59, used to build v0.14.4 popup hook
  3. `DmgRead_BP_Init/OnBattleEnter/Shutdown` (dmg_read_bp.inl, v0.14.6) — HW read BP that found impact-time renderer at sub_5068B0+0x74, used to build v0.14.8 render hook
  4. `PollSpritePoolDiag`/`PollAnimFlagRegion`/`PollDamageSlotDiag` (spritepool.inl, v0.14.0-v0.14.5) — sprite pool layout exploration, superseded by direct sub_5068B0 arg-pointer reading in v0.14.10
  5. `DmgRenderHook_PeriodicStats` (dmg_render_hook.inl) — 5-second periodic stats logger; hook itself is wired and working
  6. `DmgPopupHook_LogStats` (dmg_popup_hook.inl) — 5-second periodic stats logger; hook itself is wired and working

These are dev-only diagnostic infrastructure. None affect the player experience. Cleanup task (low priority): could be removed in a future tidy-up pass, or wired up if needed for future debugging.

**Audit limitations:** This audit catches missing call-site regressions (the v0.14.24 build-recovery class). It does NOT catch:
  (a) Internal logic regressions: if Sonnet mangled the body of a function rather than its wiring, this audit can't see it. Would require code review of each .inl, OR triggering the affected code path in playtesting.
  (b) Rare code paths: if a feature only runs in conditions Aaron hasn't tested (e.g. X-ATMO92 chase scene, specific NPC interactions, unusual battle states), regressions there would evade both this audit and Aaron's regular play.
  (c) Configuration drift: if the v0.14.x series introduced new config keys or tuning constants that Sonnet reverted, those wouldn't appear as missing-symbol regressions.

Bugs 3 and 4 from the v0.14.31 BAT may still be wiring issues that this audit missed, or they may be content regressions. Bug 3 in particular may already be self-resolved by the v0.14.32 + v0.14.34 fixes (both bug 1 and bug 2 were timing-related and could have indirectly disrupted submenu state machine timing). Worth retesting v0.14.34 before deeper investigation.

---

**On the horizon**

- Persistent settings storage across sessions
- Party member removal from entity catalog
- X-ATMO92 chase scene accessibility
- World map GitHub issues: vehicle-aware BFS, guided GPS mode, auto-announce location names, TERRAIN-DIAG cleanup
- Battle command menu architecture (tabbed detection), cancel/back re-announce, Magic sub-menu scroll offset for >4 spells
- Draw menu "???" spell reveal issue
- Independent SFX volume control (GitHub issue #8)

---

**Key learnings & principles**

**CRITICAL — bash vs filesystem MCP view mismatch:** When working on this project, bash sees `/C:/...` paths that look like the OneDrive folder but are actually a separate container-local filesystem. The `create_file` system tool writes there too. Files Aaron's build will see ONLY come from filesystem MCP `write_file` / `edit_file` at `C:/...` (no leading slash). DO NOT use `create_file` for project files. DO NOT use bash for project files. Use filesystem MCP exclusively. Diagnosed v0.14.44 session 74.

**CRITICAL — SET3 hook permanently disabled:** NEVER re-enable the SET3 opcode hook (opcode 0x1E). ANY interception — MinHook, dispatch table patch, or minimal passthrough wrapper — hangs the infirmary scene (Dr. Kadowaki walk freeze). GitHub Actions CI check in `.github/workflows/safety-checks.yml` guards against accidental re-enablement. Diagnosed v0.09.32–v0.09.40 via binary search.

**CRITICAL — MSVC name-mangling lesson (v0.14.31):** Forward declarations of namespaced functions across translation units MUST exactly match return type. MSVC encodes return type in the symbol name (`?Speak@ScreenReader@@YAX...` for void vs `YA_N...` for bool). A `void Speak` forward decl in one .cpp + `bool Speak` definition in another = unresolved external. When fixing linker errors involving cross-namespace forward decls, always grep for ALL inline decls of the function and unify them.

**CRITICAL — Build recovery hook-install gotcha (v0.14.24→v0.14.34):** When rebuilding a .cpp file from an older GitHub HEAD and re-wiring newer .inl files into the include chain, ALSO audit `OnBattleEnter()` (and equivalent lifecycle entry points) for missing `*Install()` and `*Reset()` calls AND `Update()` for missing `Poll*()` calls. The .inl include alone is insufficient; the lifecycle wiring must be explicit. The v0.14.24 rebuild left FIVE separate hook/reset functions un-wired across 4 .inl files: DmgRenderHook (bug 1), DmgPopupHook (bug 1 partner), Sub48E830Hook + ResetNoEffectState (bug 2 fallback path in noeffect.inl), and SpriteSpawnHook + SpellResultHook + PopupSpriteHook + ResetSpriteSpawnState + PollKind4Capture (bug 2 primary path in sprite.inl). Add this audit checklist to every future build recovery: (a) every `Install*` function defined in any newly-wired .inl must have a corresponding call in OnBattleEnter; (b) every `Reset*` function must have a corresponding call in the OnBattleEnter reset block; (c) every `Poll*` function must have a corresponding call in Update().

**Action ID at 0x01D27AE3 is NOT 0x16 for player magic (v0.14.34):** The v0.13.83 noeffect.inl comment claimed `arg[1]==0x16 (magic action ID)` for the sub_48E830 hook gate. v0.14.34 BAT proved this WRONG: actual actionId for Sleep cast was 0x01. The 0x16 value in `[CMD] cmds=[0x14,0x15,0x16]` is the Draw command-menu index, NOT the action staging byte. Future filtering of sub_48E830 hits should NOT use 0x16 as a gate. The current v0.14.34 implementation correctly removes the gate and relies on the watchdog's activity flags (HP delta, status queue, flush announce) to filter genuine effects from no-effect cases.

**SAVEMAP OFFSET CORRECTION:** Deep research assumes savemap header is 96 bytes (0x60). CONFIRMED header is 76 bytes (0x4C). All post-header offsets from deep research are 0x14 (20 bytes) too high. Subtract 0x14. Confirmed base: `0x1CFDC5C`. GFs at +0x4C, chars at +0x48C, Gil at +0x08 (header). Include this correction in all future deep research prompts about FF8 savemap/menu data.

**Interactive object positions:** PSHN_L literals in target entity init scripts (SETLINE/SET3/TALKRADIUS). SETLINE center override works for SETLINE-triggered entities. Shift-pattern fallback is ~494 units off. All 8 runtime approaches for beyond-window entities exhausted. Director pattern is redundant dead code per deep research.

**Victory TTS:** MUST hook text renderer, NOT read memory addresses. Memory dumps all info at once — player blindly presses through multiple unannounced screens. Hook text pipeline to detect current victory phase, announce per-phase as each screen renders. Do NOT pivot to memory scanning.

**EWM design model:** Enhanced Wait Mode retrofits FF8 into sequential turn-based — only ONE action/menu occurs at a time. ATB still races normally; whoever fills first goes first (no advantage, same economy as vanilla). During ANY action, ALL other ATB freezes. Preserve: (1) first-to-fill acts first, (2) no skipped turns, (3) natural ally/enemy ratio.

**Damage announcement timing (v0.14.10):** Two parallel triggers wired into `PollHPChanges`. Production trigger is the sub_5068B0 render hook (impact-time, ~62ms after anim-up); fallback is the v0.13.90 anim-flag-fall trigger. Whichever fires first wins via `s_popupSpawnTriggered` flag. The render hook MUST be installed in `OnBattleEnter()` via `DmgRenderHook_Install()` — without it, only the anim-flag-fall fallback fires, producing the OLD ~13s-late timing.

**FFNx replaces ATB writes:** FFNx (not the original engine) writes GF loading counter values. The game's own code is a red herring — must hook FFNx's replacement function found by scanning for signature `B9 16 F0 CF 01 66 89 06`.

**Analog steering:** World-space headings must be projected onto calibrated camera axes (measured via `lX`/`lY` test injection at field start). Direct world-space mapping only works on axis-aligned camera fields.

**Walkmesh:** 47.5% of FF8 fields have disconnected walkmesh islands. FF8 uses inline vertex format (uint32 numTriangles, then N×24 bytes inline vertex data, then N×6 bytes neighbor data). Full walkmesh JSON at project root.

**Reusable diagnostic:** OpenGL screenshot capture. Only `glReadPixels` via SwapBuffers hook works — PrintWindow/BitBlt/screen DC all return black. See `HookedSwapBuffers/DoGLCapture/CaptureScreenshot` in `battle_tts.cpp`. Requires `gdiplus.lib+opengl32.lib`.

**v0.14.22 Blue Magic breakthrough:** Auto-building scanner eliminates manual spell collection via signature matching + runtime address discovery. Preserves v0.14.20's spell ID mappings (0x92="Laser Eye", 0xAA="Ultra Waves") to maintain proper UI ordering. Revolutionary: works with ANY Blue Magic spell Aaron learns, zero maintenance.

---

**Approach & patterns**

**SESSION CHECKPOINT RULE:** To prevent progress loss when Claude session limits hit unexpectedly, update DEVNOTES.md and NEXT_SESSION_PROMPT.md at TWO checkpoints: (1) every time a new build version is bumped for Aaron to test, and (2) after every BAT (Built and Tested) result. Treat these updates as part of the version-bump and BAT workflows, not optional end-of-session work.

**Session startup ritual:** At the START of every new session, Claude MUST read both `DEVNOTES.md` and `NEXT_SESSION_PROMPT.md` using filesystem tools before doing any work. Read `DEVNOTES_HISTORY.md` only when tracing past decisions. Keep DEVNOTES under 10KB — move completed investigations to HISTORY.

**Build/test workflow:** Aaron says "BAT" = "Built and Tested." Claude should check `Logs/build_latest.log` tail for errors, then game log (`Logs/ff8_mod.log` or domain-specific: `ff8_field.log`, `ff8_battle.log`, `ff8_menu.log`, `ff8_world.log`, `ff8_dialog.log`) for runtime results. When a build error occurs, immediately read `Logs/build_latest.log` before attempting fixes.

**Default to writing code:** Once an approach is decided, write code directly. Avoid re-reading transcripts and re-summarizing instead of implementing — Aaron has explicitly corrected this pattern.

**Version bump — 1 location only:** `FF8OPC_VERSION` in `ff8_accessibility.h`. `field_navigation.cpp` and `battle_tts.cpp` headers say "See FF8OPC_VERSION" and their `Initialize()` logs use the macro via `%s` format. Format: `0.MM.BB` pre-production, `1.0.0` first public.

**Build system:** `deploy.vbs` in project root launches `src/deploy.ps1` which runs `src/deploy.bat`. All build scripts live in `src/` except the `.vbs` launcher. Update `src/deploy.bat` when adding/removing source files.

**Deep research protocol:** When source code, game files, and mod logs are insufficient, ask Aaron to perform deep research using ChatGPT. Claude provides the exact prompt.

**F12 diagnostic key rule:** F12 is reserved exclusively for per-session diagnostic/debug builds. BEFORE hooking any new diagnostic to F12, Claude MUST search all source files for existing `VK_F12` or F12 references and REMOVE any old diagnostic code bound to F12 first. Only one diagnostic ever active on F12 at a time.

**Mid-file .asm read:** When bash unavailable and .asm file too big for head/tail, use `filesystem:edit_file` with `dryRun=true`. Chain anchors using trailing lines from previous result.

**Stable catalog ordering:** Entity catalog order must be stable — only changes when entities appear/disappear, never reorders by distance. Blind players track visited entities by position.

---

**Tools & resources**

**CRITICAL — filesystem tools only for project files:** Mod files are on Windows. ALWAYS use filesystem MCP tools (`read_text_file`, `edit_file`, `write_file`, `search_files`, etc.) for ALL project file access. NEVER use bash for project files — bash runs in a separate Linux container that cannot access the Windows mod directory. Bash is only useful for text processing on tool results already in context.

**Key source files:**
- `src/ff8_accessibility.h` — version define
- `src/mod_forward_decls.h` — cross-module namespace forward declarations (created v0.14.30)
- `src/field_navigation.cpp` + 13 `.inl` files (48KB core)
- `src/battle_tts.cpp` + 18 `.inl` files including helpers, diagnostics, hp, ewm, menu, sprite, status, noeffect, sprite_spawn, validate, dmgbp, dmg_popup_hook, dmg_read_bp, dmg_render_hook, spritepool, roi_calib, screenshot, victory
- `src/menu_tts.cpp` + `.inl` files
- `src/field_archive.cpp` / `field_archive_jsm.inl` — JSM scanner
- `src/dinput8.cpp` — main hook entry

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

**Keyboard shortcuts (complete):** `` ` `` = repeat dialog/battle event | V = mod version | F1 = cycle voice | F3/F4 = game vol | F5/F6 = speech vol | F7/F8 = speech rate | F9/F10 = field nav | F11 = menu summary (Shift=monitor, Ctrl=dump) | F12 = diagnostic builds only | G/T/L/R = Gil/Time/Location/SeeD | `/` = help bar | O = EWM toggle | 1/2/3/H = battle HP check | F2 = unused

**GitHub:** `ampage87/FFVIII-Accessibility-Mod`, main branch. GitHub Sponsors enabled.
