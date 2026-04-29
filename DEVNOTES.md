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

**Current build: v0.14.48 — audio ducking BAT PASSED. Audio mixing chapter complete.**

Aaron BAT'd v0.14.48. All three tuning tweaks landed cleanly: BGM at -10 dB ducks noticeably without feeling muted, SFX at -15 dB cuts through attack animation SFX, 800 ms hold bridges menu-nav gaps without bouncing. GF audio descriptions are now clearly audible during summon animations — the original problem that started the audio mixing thread is solved.

**Audio mixing chapter — what shipped (v0.14.45 → v0.14.48):**

- v0.14.45: SFX volume API resolved (game-side via opcode_effectplay2 chain), persistent INI keys, full keyboard layout reshuffle (F1 voice / F2 ducking toggle / F3+F4 speech rate / Shift+F3+F4 speech vol / F5+F6 SFX / F7+F8 BGM), removed v0.12.21 stale F2 diagnostic.
- v0.14.46: SFX hook fixed — install MinHook on game function at 0x0046A390 directly regardless of FFNx state. Volume range 0–100 (not 0–127). Discovered FFNx only patches `sfx_set_master_volume` when `use_external_sfx=true`.
- v0.14.47: AudioDucker module — per-bus config (depth/attack/release/hold), reference-counted BeginDuck/EndDuck, smoothed one-pole envelope with frame-rate-independent coefficient, dB-based config. F2 toggle wired to `AudioDucker::SetEnabled`. Polling adapter in `GameAudio::Update` drives Begin/End on `IsSpeaking()` edges. Existing 500 ms periodic Reapply gates on `!IsActive()`. F5–F8 mid-duck volume changes skip immediate Reapply.
- v0.14.48: Tuning constants — BGM -10 dB, SFX -15 dB, hold 800 ms.

GitHub HEAD before the audio mixing chapter was `7845f0c` = v0.14.44 (GF summon audio descriptions, pushed 2026-04-29 01:27). The audio mixing chapter (v0.14.45 → v0.14.48) is the next push. Push commit message is at the end of NEXT_SESSION_PROMPT.md.

---

**Immediate next session priorities (in order):**

1. **GitHub push of the ~50-build backlog** (v0.13.63 → v0.14.48). Single comprehensive commit. Audio mixing chapter is the cut-point. Use `Utilities/push_to_github.vbs`. Tag: `v0.14.48`. Close GitHub issue #8 (independent SFX volume) when push lands.
2. Persistent accessibility settings across play sessions (general — beyond just the volume/duck keys, which already persist via `ff8_accessibility.ini`).
3. Verify GF naming bypass — Siren failed in earlier testing.
4. Remove party members from entity catalog.
5. X-ATMO92 chase scene accessibility.
6. If audio mixing needs further tuning, start there. The cheat sheet is in NEXT_SESSION_PROMPT under "Tuning playbook". Per-channel SFX ducking (Option B) remains the escalation path if any specific scene exposes a long in-flight sample masking TTS.

---

**On the horizon**

- Independent SFX volume control GitHub issue #8 (resolved by v0.14.46 — should be closed when issues are next reviewed)
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
