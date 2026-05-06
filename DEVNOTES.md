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
- Scan spell auto-announce + interactive number-key UI (v0.14.50 → v0.14.82) — all keys 1–0 wired, chance-based weakness tiering with Spirit accounting, BENT_STATUS_RESIST_BASE = 0x80
- GF summon audio descriptions (v0.14.44) — 18 VTTs covering 16 junctioned GFs + Phoenix + Odin
- SFX volume control + ducking-toggle scaffold + keyboard layout reshuffle (v0.14.45 + v0.14.46)
- World map BFS catalog reachability filter + auto-drive + animation-byte suppression (v0.14.83 → v0.14.90.3) — pushed to GitHub
- Multi-channel logging system (6 domain logs); `.inl` file splitting
- Full FF8_EN.exe disassembly reference at `Game Files/disassembly/`

---

**Current build: v0.14.92 BAT-passed Tue 2026-05-05 20:30. Section 8 (field-entry bytecode) FULLY DECODED. Ready to ship v0.14.93.** v0.14.90.3 BAT-passed Tue 2026-05-05 18:12 + pushed to GitHub Tue 2026-05-05 18:30 local at HEAD `683f1531`. v0.14.91 + v0.14.92 sit on top in local working tree (not yet pushed; will likely consolidate with v0.14.93 as a single GitHub commit once decoder hardcoded data ships).

**v0.14.92 BAT result (this session, after Stage 4 pivot):** Build clean (`build_latest.log` 20:30:09). Dump fired at module init successfully. 48-section table logged with sizes matching prediction exactly: Section 1=392b, Section 2=772b, Section 7=56b, Section 8=2652b. Section 7 hex (56b) revealed a 14-record table of `1D 00 NN 3C` patterns where NN is a 12-value permutation {00,0B,03,05,04,01,02,09,0A,07,08,06} + `1D 00 3E 38` special entry + null terminator — likely a region-ID permutation/mapping table. Section 8 hex (2652b) revealed: 38-entry u32 LE offset table (programs at 0x9C..0xA30) + 0x00 terminator at 0x94 + bytecode begins at 0x98 with the `0xFF01` global section-begin marker. Python disassembler in bash sandbox decoded ALL 38 PROGRAMS; full structured output saved as `Plan & Research Documents/wmsetus Section 8 decoded.md` (the durable artifact).

**Decoded architecture (full opcode table now confirmed):** Each program is a sequence of 4-byte u16 LE instruction pairs. `0xFF06 <loc_id>` = program header (operand is the wmField/location ID, range 0x0031..0x02C1). `0xFF02 <story_flag>` / `0xFF03 <story_flag>` = savemap word GTE / LT comparisons against `[0x2036bde/0x2036bdf]`. `0xFF04` = begin-condition (state machine entry). `0xFF09 <vehicle>` = locomotion match; operands are `0x80` (foot/Squall-lead), `0x84` (foot/alt-lead), `0x30` (Garden), `0x31` (Chocobo), `0x32` (Ragnarok). `0xFF08 <region>` = player's segment region-byte must equal operand; regions referenced span 0x00..0x45 (45 unique values, indexed via Section 2's 32×24 region-byte map). `0xFF0A`/`0xFF0C`/`0xFF0D` = OR-clause separators. `0xFF0B` = AND combinator. `0xFF05` = end-of-clause. `0xFF16` = end-of-program success terminator. `0xFF01` = next-program separator. `0xFF0F`/`0xFF10`/`0xFF11`/`0xFF12`/`0xFF20`/`0xFF21` = unknowns appearing as additional condition flags (consistent operand patterns suggest 16-bit thresholds; left opaque for v0.14.93, can be empirically decoded later via field-entry instrumentation).

**Critical insight:** NO rectangle-bounds opcodes exist. Fine-grained entry geometry comes entirely from the **region-ID system**. Section 2 (772 bytes = 4-byte header + 32×24 = 768-byte segment-region map) holds each segment's region byte. Each program's `0xFF08 <region>` operand identifies which segments trigger that field entry. Multiple segments can share a region byte = same trigger area. For Balamb Town, the gate-position segment has a unique region byte not shared with surrounding segments. For B-Garden, the region byte is shared across all approachable segments around the Garden = wide entry. **This is the mechanism that lets us solve narrow-entrance steering.** AD doesn't need to know the location's name or wmField ID — for each catalog (X,Y), compute the segment, read Section 2's region byte for that segment, find all segments sharing that region byte, steer toward the closest one with the right vehicle requirement.

**Notable program clusters from the decode:**
- Programs 18-20 share locID `0x0172` with three vehicle-specific paths (Chocobo/Ragnarok/Garden) — likely the mobile B-Garden destination.
- Programs 25-28 share region `0x1A` with foot+footAlt — the orphanage / Edea's House cluster (Selphie POV active).
- Program 32 (locID `0x01FA`) is heavily story-conditional: 4 story-windowed regions (0x1F → 0x30 → 0x31 → 0x1F) — likely Esthar City which has disc-3 story unlock.
- Program 37 (locID `0x02C1`) is Ragnarok-only via region 0x15 — likely Lunar Gate or alien ship.
- 38 programs ↔ 38 catalog entries (26 main + 7 chocobo + 4 alien + Fire Cavern) — strong hint of one-to-one correspondence.

**v0.14.93 plan (next build):** (a) extend `WMSETUS_DUMP_SECTIONS_1IDX[]` to include Section 2 so we capture the segment→region byte map; (b) hardcode the 38 programs as a static `s_triggerPrograms[]` C++ array embedded at module init time (38 entries × ~5-15 clauses each, total ~5KB of data — small); (c) ship the build for BAT to confirm Section 2 decode matches predictions. v0.14.94 wires the decoded data into AD targeting (use Section 2 region bytes to find equivalent-trigger segments; steer toward the nearest one matching the player's vehicle state and current story flags).

**Deferred to v0.14.94+ or beyond:** field-ID-to-name mapping (deep research lookup or empirical capture via field-entry instrumentation); UNK_0F/10/11/12/20/21 opcode interpretation (only matters for ~6 of 38 programs and even those programs have other unflagged paths AD can use as fallback); encounter-warning feature (Section 1 + Section 2 already dumped, can be a future side project).

**v0.14.93 will close the trigger-data Chapter 3 saga** — decoded geometry replaces sweep-search as the primary AD steering mechanism. Sweep-search demotes to fallback-only for locations with undecoded UNK opcodes or runtime ambiguity.

**v0.14.91 BAT result (Stage 3, this session, BEFORE Stage 4 pivot):** Build deployed cleanly, dump fired at module init, full `[TRIGGER-DUMP]` log captured. Section 17 = 33148 bytes, Section 18 = 19360 bytes. Analysis findings: Sections 17/18 contain `wm2field`-style destination data — 32-byte records with header `1E 00 XX XX 01 00 00 00` where XX XX duplicates wmField IDs 0x07-0x10, followed by 24 bytes that decode as 6 s32 values in field-walkmesh ±10000 range. Zero (X, Y) catalog matches at any encoding (s16/s32, LE/BE, paired/interleaved) with proximity ≤2000. The deep research's leading hypothesis (`worldmap_section17_position` + `worldmap_section18_position` from FFNx → trigger geometry) was wrong: those FFNx symbols name pointers the engine sets up, but Sections 17/18 are field-side destination data (probably the `wm2field` table mapping wmField IDs to walkmesh entry coords for fields like Galbadia Garden interior), not world-map triggers.

**Stage 4 pivot context (this session):** With Sections 17/18 disproved, Claude went straight to the FF8_EN.exe disassembly to find the real architecture. Aaron uploaded an `Assembly_Files.zip` of the 9 .asm files to bash so they could be searched directly. Hunt findings (full chain in `Game Files/disassembly/`):

- **Player position address `0x0203EE80`** has 82 references, all in `FF8_EN_.text_0x00501000.asm`. Cluster mapped to 36 functions; the high-density candidates were `sub_53FAC0` (world-map tick, 8 refs) and `sub_545EA0` (1 ref but in the most interesting context).
- **`sub_53FAC0`** is the world-map tick. Around offset `0x53FF6E` it gates on `[0x2036b70] == 0` (on-foot) and then calls `sub_545EA0` (field-entry trigger), then `sub_541C80` (encounter trigger) only if field-entry didn't fire. Field entry takes priority over encounter.
- **`sub_541C80`** is the random-encounter checker. Reads `[0x2040068]` (= wmsetus Section 1, 392 bytes) as a `(terrain_code, vehicle_state)` table, indexed via terrain code looked up from `[0x2040330]` (= wmsetus Section 2, 772 bytes = 4-byte header + 768-byte 32x24 region map). Has a step accumulator at `[0x2040a5c]` with threshold 0x100 — unmistakably encounter-rate code, not field-entry.
- **`sub_545EA0`** is the FIELD ENTRY trigger. Reads `[0x2040070]` (= wmsetus Section 8, 2652 bytes), calls `sub_545F10` to walk it, gets back a wmField ID, fires field transition via `sub_544630`. Section 8 is the data we want for AD.
- **`sub_545F10` + `sub_546100`** parse Section 8 as a BYTECODE PROGRAM with ~56 different opcodes (range 0xFF02..0xFF38) dispatched via a jump table at `[0x546cac]` indexed by `[opcode + 0x546d3c]`. Opcodes include rectangle-bounds checks (compare player coords against entry's u16 literals), savemap story-flag comparisons (e.g. byte at `[0x2036bde/0x2036bdf]`), AND/OR combinators, multi-stage matching states (modes 1-6), and a follow-link instruction (`0xFF0E`) that lets locations share sub-programs.
- **`sub_542DA0`** is the wmsetus section-pointer setup function. Reads `[i*4 + 0x1e9dc3c]` (the 48-entry section-offset header from wmsetus.obj loaded at base `0x1e9dc3c`), adds the base, stores absolute pointer to a fixed `.data` slot. The first 8 writes give us the section→runtime-pointer map: Section 1 (392b) → `[0x2040068]` encounter table; Section 2 (772b) → `[0x2040330]` terrain map; Section 3 (88b) → `[0x2040090]`; Section 4 (1348b) → `[0x2036be8]` encounter destinations; Section 5 (8b) → `[0x203ed40]`; Section 6 (68b) → `[0x2040080]`; Section 7 (56b) → `[0x2040074]` adjacent metadata; **Section 8 (2652b) → `[0x2040070]` FIELD ENTRY BYTECODE**.

**Why this matters for AD:** B-Garden's bytecode program is a wide-rectangle bounds check (entrable from any direction — the rectangle covers all approaches). Balamb Town's program is a tight rectangle just covering the gate (entrable only from the south). Catalog-center steering misses Balamb Town because the catalog X/Y is the town center, not the gate position. Once we decode Section 8, we know each location's exact entry rectangle and AD targets the rectangle center directly. Sweep-search demotes to fallback-only for locations whose programs use opcodes we haven't decoded yet.

**v0.14.92 build:** `src/world_map.cpp`'s existing `LoadTriggerZones()` function (kept the name for diff stability — it's still about triggers, just a different kind) gets a new `WMSETUS_DUMP_SECTIONS_1IDX[]` array driving which sections to dump. v0.14.91 dumped {17, 18}; v0.14.92 dumps {7, 8}. Section 7 (56 bytes) is the small adjacent section possibly serving as auxiliary metadata; Section 8 (2652 bytes) is the bytecode — ~170 log rows total, trivial cost. The 48-entry section table is still logged in full as a layout sanity-check. NO game behavior change — still purely diagnostic at module init. `WMSETUS_TRIGGER_SECT_A/B_IDX` constants removed in favor of the array. File-header `CURRENT STATE` block fully rewritten for the corrected architecture; constants block expanded with the section→runtime-pointer table from `sub_542DA0`.

**v0.14.92 BAT plan:** Aaron runs `deploy.vbs`, then launches the game once. The dump fires at module Initialize so just starting the game is enough. Aaron uploads `Logs/ff8_world.log` and Claude analyzes the `[TRIGGER-DUMP]` output for `sect07` and `sect08`. Next step is the Python disassembler: Claude writes ~150 lines in the bash sandbox using the opcode dispatch table mapped from `sub_546100` (jump table at `0x546cac` + index lookup at `0x546d3c`) plus the multi-mode parser state from `sub_545F10` (opcodes `0xFF01`/`0xFF04` start region-check / location-check; `0xFF05` is no-match terminator; `0xFF0A`-`0xFF0D` are state transitions; `0xFF0E` is follow-link; `0xFF16` is another sentinel; rest are condition predicates). Runs the disassembler against the dumped Section 7+8 bytes and emits per-location entry geometry (wmField ID, rectangle bounds, story-flag preconditions, opcodes that aren't yet decoded).

**v0.14.93+ followup:** hardcode the decoded data as a static `s_triggerData[]` array parallel to `s_locations[]`. Each entry holds: derived rectangle (X_min, X_max, Y_min, Y_max), wmField ID, optional story-flag predicate, vehicle-state requirement. v0.14.94 wires it into `StartAutoDrive`'s targeting (use rectangle center over catalog center when one exists) + arrival check (use rectangle bounds), demoting sweep-search to fallback only for locations whose programs use opcodes we haven't decoded. Future: encounter-warning feature can use the already-mapped Section 1 + Section 2 tables.

**Files touched in v0.14.92:** `src/world_map.cpp` (~80 lines: `WMSETUS_DUMP_SECTIONS_1IDX` + `WMSETUS_DUMP_COUNT` constants, removed `WMSETUS_TRIGGER_SECT_A/B_IDX`, file-header `CURRENT STATE` block fully rewritten for the corrected architecture, constants block expanded with the section→runtime-pointer table from `sub_542DA0`, `LoadTriggerZones` header comment updated, dump loop replaces the two hardcoded `safeDump` calls, `Initialize` call-site comment updated), `src/ff8_accessibility.h` (FF8OPC_VERSION 0.14.91 → 0.14.92 with full Stage-4 changelog).

The world-map auto-drive saga is now mid-Chapter 3 Stage 4: catalog-center steering (Chapter 1) + AD core (Chapter 2) shipped; trigger-zone Sections 17/18 hypothesis disproved (Stage 3 BAT, this session); field-entry Section 8 bytecode targeted next (Stage 4, this build).

**Next priority:** Ship v0.14.93. Two parallel changes: (1) extend `WMSETUS_DUMP_SECTIONS_1IDX[]` to `{2, 7, 8}` so Section 2 (the 32×24 region-byte map) gets captured; (2) hardcode the decoded 38-program data from `Plan & Research Documents/wmsetus Section 8 decoded.md` as a `static s_triggerPrograms[]` C++ array. After BAT, v0.14.94 wires the data into `StartAutoDrive` targeting and the arrival check. Polish punch-list (pre-battle Ship-noise debounce extension, deferred re-check after non-canonical window expiry, Ship-mount-at-FH-dock catalog rebuild verification, GitHub issue #27 SeeD rank) remains deferred until v0.14.94 ships.

**Per-version build narrative archive:** `DEVNOTES_HISTORY.md` carries topical chapter pointers to GitHub commit messages — `7c7afdf3` (Scan TTS v0.14.50→82), `aef75aac` (World Map regression fix + Chapter 1 v0.14.83→85.3), `0b06ab1` (Chapter 2 auto-drive v0.14.86→90.2), `683f1531` (Chapter 2 hotfix #6 v0.14.90.3). Older sessions (Session 65 down through Pre-v05) live verbatim in `DEVNOTES_HISTORY.md`.

---

**On the horizon**

- Boko Choco / Minimog / Moomba / Gilgamesh VTTs (extension of v0.14.44 GF AD)
- Per-GF AD timing tuning based on continued in-game listening
- Battle command menu architecture (tabbed detection), cancel/back re-announce, Magic sub-menu scroll offset for >4 spells
- Draw menu "???" spell reveal issue
- Quistis Blue Magic spell-list ordering investigation
- Persistent accessibility settings across play sessions (refined-coord serialization is a natural first slice)
- Remove party members from field entity catalog
- X-ATM092 chase scene accessibility
- Walk-and-talk dialog gap (hardcoded engine path, no hook point)
- GitHub issue #27 — SeeD Rank misreads as "No rank yet"; hypothesis: `FIELD_H_OFFSET = 0xF94` in `AnnounceSeedRank()` is a stacked-section-size computation with one wrong section size

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

**CRITICAL — MinHook installer conflict on a single address (v0.14.72):** Two modules attempting `MH_CreateHook` on the same address: the first wins, the second silently fails with `MH_ERROR_ALREADY_CREATED` — only the failure log line distinguishes the loser. Pattern observed at `sub_47EC70` (scan_tts.cpp vs battle_tts_victory.inl racing during init). Resolution: cooperating modules MUST share one canonical installer per address, with the owner module exposing a public forward-call function (e.g. `HandleBattleText(textId, result)` in victory.inl) that the other modules invoke as passive observers. Audit any future cross-module hook for this.

**CRITICAL — animation-residue byte noise mimics real engine state (v0.14.90.3):** Frame-scale debounce is insufficient when the noise values are themselves canonical AND held for hundreds of milliseconds. Locomotion byte at WM_LOCOMOTION cycles through canonical values (0/3/6) during world-map re-entry camera animation, each value held ~1s — passes any per-poll debounce as legitimate. Discriminator must be a different signal entirely (in this case, recency of world-map entry — `WM_ENTRY_DEBOUNCE_MS = 3000ms` time-gate). Non-canonical byte at window expiry: keep the prior baseline (doing nothing is safer than committing the unknown).

**CRITICAL — Build recovery hook-install gotcha:** When rebuilding a .cpp file from an older GitHub HEAD and re-wiring newer .inl files into the include chain, ALSO audit `OnBattleEnter()` (and equivalent lifecycle entry points) for missing `*Install()` and `*Reset()` calls AND `Update()` for missing `Poll*()` calls. The .inl include alone is insufficient; the lifecycle wiring must be explicit. Audit checklist for every future build recovery: (a) every `Install*` function defined in any newly-wired .inl must have a corresponding call in the lifecycle entry; (b) every `Reset*` function must have a corresponding call in the reset block; (c) every `Poll*` function must have a corresponding call in `Update()`. Also audit the module's PUBLIC SURFACE (what's declared in the .h) vs its ACTUAL CALLERS — orphaned-but-defined functions like `WorldMap::HandleKeyPress` (v0.14.31 recovery) survive linking but are silently dead.

**CRITICAL — Always consult `Plan & Research Documents/` BEFORE picking an interpretation when one exists (v0.14.73 lesson):** v0.14.73 shipped a guess for FF8's elemental defense scale (FF7-style buckets) when a deep research document with the correct answer (800-anchored u16) was sitting in the project. The diagnostic log saved us — we got the right answer in one BAT instead of multiple — but we shouldn't have needed it. Search Plan & Research Documents/ at the start of any feature work that touches a documented engine field.

**CRITICAL — Deep research can have the wrong leading hypothesis (v0.14.91 → v0.14.92 lesson):** A deep research document's leading hypothesis is still a hypothesis. The `World Map Entry Trigger Coordinates deep research results.md` named `worldmap_section17_position` + `worldmap_section18_position` from FFNx as the prime suspects for trigger geometry, and the dual-pointer setup matched the paired-section pattern seen elsewhere in wmset — plausible enough to ship a hex-dump build for. Sections 17/18 turned out to be `wm2field`-style field-walkmesh destination data, not world-map trigger geometry. The disassembly was the source of truth that the research couldn't access. Lesson: when a deep research hypothesis can be cheaply tested with a diagnostic build, ship it; but if the diagnostic disproves the hypothesis, go to the disassembly directly rather than seeking another round of deep research. Once the assembly tree is loaded into bash (`Assembly_Files.zip` → `/tmp/asm/`), grepping for the player position address and walking the call chain from the world-map tick is faster than another deep research turnaround.

**Action ID at 0x01D27AE3 is NOT 0x16 for player magic:** The v0.13.83 noeffect.inl comment claimed `arg[1]==0x16 (magic action ID)` for the sub_48E830 hook gate. v0.14.34 BAT proved this WRONG: actual actionId for Sleep cast was 0x01. The 0x16 value in `[CMD] cmds=[0x14,0x15,0x16]` is the Draw command-menu index, NOT the action staging byte. Future filtering of sub_48E830 hits should NOT use 0x16 as a gate.

**SAVEMAP OFFSET CORRECTION:** Deep research assumes savemap header is 96 bytes (0x60). CONFIRMED header is 76 bytes (0x4C). All post-header offsets from deep research are 0x14 (20 bytes) too high. Subtract 0x14. Confirmed base: `0x1CFDC5C`. GFs at +0x4C, chars at +0x48C, Gil at +0x08 (header). Include this correction in all future deep research prompts about FF8 savemap/menu data.

**Distance-based arrival sidesteps mode-register timing races (v0.14.90.2):** Don't try to read `pGameMode` at the moment of `IsOnWorldMap` flipping false — the register hasn't transitioned yet. Use a different signal entirely (in this case, `s_driveLastDist < 1500 at exit` — battles fire anywhere, field entries only happen near targets). Same lesson generalizes to any state-machine race where two signals from the engine appear to flip in lockstep but actually settle one or two frames apart.

**GitHub API write tools count as pushes (v0.14.90.3 lesson):** `github:create_or_update_file` and `github:push_files` write directly to GitHub without going through the user's local clone. Using them bypasses git's local→remote sync and the local clone diverges from origin without anyone realizing. Recovery requires `git fetch origin && git reset origin/main` (mixed reset preserves working tree). Rule: Claude NEVER pushes — Claude only provides version + commit description; Aaron's `Utilities/push_to_github.bat` (launched via `push_to_github.ps1`) does the actual push.

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

**Session startup ritual:** At the START of every new session, Claude MUST read both `DEVNOTES.md` and `NEXT_SESSION_PROMPT.md` using filesystem tools before doing any work. Read `DEVNOTES_HISTORY.md` only when tracing past decisions. Keep DEVNOTES under 25KB — move completed investigations to HISTORY.

**Build/test workflow:** Aaron says "BAT" = "Built and Tested." Claude should check `Logs/build_latest.log` tail for errors, then game log (`Logs/ff8_mod.log` or domain-specific: `ff8_field.log`, `ff8_battle.log`, `ff8_menu.log`, `ff8_world.log`, `ff8_dialog.log`) for runtime results. When a build error occurs, immediately read `Logs/build_latest.log` before attempting fixes.

**Default to writing code:** Once an approach is decided, write code directly. Avoid re-reading transcripts and re-summarizing instead of implementing — Aaron has explicitly corrected this pattern. If unsure between two approaches, pick the simpler one and commit; iterate from BAT results, not from speculation.

**Version bump — 1 location only:** `FF8OPC_VERSION` in `ff8_accessibility.h`. `field_navigation.cpp` and `battle_tts.cpp` headers say "See FF8OPC_VERSION" and their `Initialize()` logs use the macro via `%s` format. Format: `0.MM.BB` pre-production, `1.0.0` first public.

**Build system:** `deploy.vbs` in project root launches `src/deploy.ps1` which runs `src/deploy.bat`. All build scripts live in `src/` except the `.vbs` launcher. Update `src/deploy.bat` when adding/removing source files.

**Deep research protocol:** When source code, game files, and mod logs are insufficient, ask Aaron to perform deep research using ChatGPT. Claude provides the exact prompt. Save prompts to `Plan & Research Documents/`. Always search `Plan & Research Documents/` first BEFORE writing code that interprets any FF8 engine data field.

**GitHub push workflow:** Claude NEVER pushes to GitHub itself, including via `github:create_or_update_file` and `github:push_files` API tools. Claude provides (1) the version number to bump to and (2) a consolidated commit description. Aaron's utility (`Utilities/push_to_github.bat`, launched via `push_to_github.ps1`) does the actual push. ALWAYS call `github:list_commits` before quoting any backlog size or push state — never quote from memory.

**Function key repurposing rule (generalized from F12 rule):** Before assigning a new behavior to F1–F12, grep ALL source files for existing `VK_F{n}` references and remove the stale ones. Diagnostics from old sessions hide in `.inl` files and survive long after DEVNOTES says "unused". Specific instances caught: v0.12.21 F2 "Director Varblock" diagnostic in `field_nav_handlekeys.inl`; v0.12.22 F12 POPM_W reset block in `field_nav_fieldscripts.inl`. Both removed in the v0.14.45 keyboard rebind. Search for ALL the symbols, not just the literal `VK_F{n}` — supporting state variables (e.g. `s_varWriteCount`) live elsewhere and break the build if their reset is missed.

**Mid-file .asm read:** When bash unavailable and .asm file too big for head/tail, use `filesystem:edit_file` with `dryRun=true`. Chain anchors using trailing lines from previous result. Same pattern works for grep-style search of any large project file.

**Past-chat retrieval:** When a feature appears to have regressed or gone missing without an obvious commit, use `conversation_search` to find the original chat where it was implemented. The v0.14.86 auto-drive restoration was reconstructed by searching v0.11.05–v0.11.10 chats — the working baseline architecture lived only in past chats, not in any pushed commit.

**Stable catalog ordering:** Entity catalog order must be stable — only changes when entities appear/disappear, never reorders by distance. Blind players track visited entities by position.

**OneDrive sync EPERM:** OneDrive sync can cause `EPERM` rename errors on first `edit_file` attempt. Correct response: immediate retry, no other action.

---

**Tools & resources**

**CRITICAL — filesystem tools only for project files:** Mod files are on Windows. ALWAYS use filesystem MCP tools (`read_text_file`, `edit_file`, `write_file`, `search_files`, etc.) for ALL project file access. NEVER use bash for project files — bash runs in a separate Linux container that cannot access the Windows mod directory. Bash is only useful for text processing on tool results already in context.

**Key source files:**
- `src/ff8_accessibility.h` — version define (`FF8OPC_VERSION`)
- `src/mod_forward_decls.h` — cross-module namespace forward declarations
- `src/field_navigation.cpp` + 13 `.inl` files (48KB core)
- `src/battle_tts.cpp` + 18 `.inl` files including helpers, diagnostics, hp, ewm, menu, sprite, status, noeffect, sprite_spawn, validate, dmgbp, dmg_popup_hook, dmg_read_bp, dmg_render_hook, spritepool, roi_calib, screenshot, victory
- `src/scan_tts.h` / `src/scan_tts.cpp` — Scan spell TTS (v0.14.50–82 chapter); per-slot ScanSnapshot captured at cast time, all speech deferred to screen-open hook; passive observer of victory.inl's `sub_47EC70` hook via `HandleBattleText`
- `src/world_map.cpp` — world map BFS catalog + auto-drive (v0.14.83–v0.14.90.3 chapter)
- `src/menu_tts.cpp` + `.inl` files
- `src/game_audio.cpp` / `.h` — BGM + SFX + ducking-toggle
- `src/field_archive.cpp` / `field_archive_jsm.inl` — JSM scanner
- `src/dinput8.cpp` — main hook entry; keyboard input block

**Log files:** `Logs/build_latest.log`, `Logs/ff8_mod.log`, `Logs/ff8_field.log`, `Logs/ff8_battle.log`, `Logs/ff8_menu.log`, `Logs/ff8_world.log`, `Logs/ff8_dialog.log`, `Logs/git_latest.log`. Auto-archived to `Logs/archive/` on next build start.

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

**Keyboard shortcuts (v0.14.46):** `` ` `` = repeat dialog/battle event | V = mod version | F1 = cycle voice | F2 = toggle audio ducking (Phase 1 announce-only) | F3/F4 = speech rate down/up | Shift+F3/F4 = speech volume down/up | F5/F6 = SFX volume down/up | F7/F8 = BGM volume down/up | F9/F10 = field nav | F11 = menu summary (Shift=monitor, Ctrl=dump) | F12 = diagnostic builds only | G/T/L/R = Gil/Time/Location/SeeD | `/` = help bar | O = EWM toggle | 1/2/3/H = battle HP check | `\` = world map auto-drive (v0.14.86+).

**GitHub:** `ampage87/FFVIII-Accessibility-Mod`, main branch. GitHub Sponsors enabled. Push utility at `Utilities/push_to_github.bat` (launched via `push_to_github.ps1`). `main` HEAD = `683f1531` (v0.14.90.3, pushed Tue 2026-05-05 18:30 local); local in sync.
