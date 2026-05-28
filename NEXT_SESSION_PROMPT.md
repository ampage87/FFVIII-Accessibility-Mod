# Next Session Prompt: v0.17.8.17.7 Main-Menu (Junction char-select) dream-NAME follow-up BAT pending

## Greeting

Start every response with `## Claude Says`. Read `DEVNOTES.md` and THIS file before any work.

## Where we are at session open

**v0.17.8.17.7 staged, Main-Menu dream-NAME follow-up BAT pending.** Victory screen (v0.17.8.17.6) is VALIDATED. The Main Menu was NOT fixed by .17.6: reading that BAT's `ff8_menu.log` showed the wrong names came from `AnnounceJuncCharSelect` (Junction character-select), not `AnnounceMenuSummary` (the M-key summary .17.6 had patched, which Aaron never invoked -- `party slot` never logged). Same stale-formation-index root cause; the game's own GCW-rendered text correctly shows Ward/Laguna/Kiros, so the dream identity is present.

**.17.7 fix.** `AnnounceJuncCharSelect` (`menu_tts_junction.inl`) now reads the displayed char's `model_id` (+0x08 in `char-data[charIdx]`) and names Laguna/Kiros/Ward when it is 8/9/10, else falls back to the formation-index name table (unchanged for normal play). Its existing per-navigation log line now prints `modelId=` so the next dream BAT confirms the value. The `AnnounceMenuSummary` model_id fix from .17.6 is retained. Victory fix untouched.

**The one open assumption:** model_id at `char-data[formation[slot]]+0x08` is 8/9/10 in the field menu (strongly supported -- in a dream you ARE Laguna's party in the field, and victory EXP confirmed char-data[formation] holds dream data -- but not yet directly observed in mode-6). The `modelId=` log line resolves it on the next BAT either way; the fallback prevents any regression.

**Shared root cause (same family as the command fix).** Both subsystems read party member IDs from the savemap party formation, which during a dream holds the STALE regular field formation `[05 00 01]`, and feed those indices into a name table. The DATA they show (EXP/HP/level) is already correct, because it reads `char-data[formation[slot]]` — which the engine has loaded with the dream character's struct (same mechanism the v0.17.8.17.5 command fix relies on). Only the NAME used the wrong source.

**Victory screen fix (battle module — high confidence).** `GetCharNameById` already maps 8/9/10 → Laguna/Kiros/Ward; the victory code was feeding it the stale formation IDs from `VICTORY_PARTY_ADDR` (the same address as `SAVEMAP_PARTY_FORMATION`). The live dream identity is the battle `compStats` actor-kind at `+0x1C3` — the SAME source the validated in-battle name fix uses. New code in `battle_tts.cpp`:
  - `s_dreamSlotCharId[3]` + `s_isDreamBattle`, snapshotted per ally slot every frame while in battle (where actor-kind is validated valid) so the value is reliably readable by the separate victory thread at mode 4. Reset in `OnBattleEnter`. Logs `[DREAM-ID]` once on detection.
  - `GetVictoryCharName(slot, fallbackId)` → dream name when the slot snapshot is 8–10, else `GetCharNameById(fallbackId)`.
  - All 7 spoken EXP announce sites in `battle_tts_victory.inl` use it (Phase 1 all-same + grouped, in both the BTXT-hook and thread-fallback paths; the 3 Phase 2 level lines). Log-only lines left as-is. No-op for normal battles.

**Main Menu fix (no battle compStats — model_id based).** `AnnounceMenuSummary` (M key, in `menu_tts_diagnostics.inl`) read the formation index from savemap `+0xAF1` and named via `CHAR_NAMES[idx]`. It now reads the displayed character's own `model_id` (+0x08 in the loaded char struct at `char-data[idx]`) and names Laguna/Kiros/Ward when it is 8/9/10, falling back to the index table otherwise. In normal play `model_id == idx` for the 8 mains → behavior unchanged outside dreams (no regression). A `[MenuTTS] party slot N: formIdx=.. modelId=.. -> ..` log line records the actual model_id so the dream BAT confirms the mapping without a separate diagnostic.

**Confidence:** high for victory; medium-high for menu — the menu fix assumes the loaded dream char struct's `model_id` is 8/9/10 (well-supported by the field `setpc` convention and the command-fix evidence that the struct holds the dream char's data, but not yet directly observed). If the BAT log shows a different model_id, that line gives the exact value to remap; the fallback guarantees no regression either way.

## If Aaron has already run the BAT

Read `Logs/build_latest.log` (confirm v0.17.8.17.7 + success), then `Logs/ff8_menu.log`:
  - Find `[JuncTTS] CharSelect: NAME ... formation[i]=U modelId=V`. Expect NAME = Ward/Laguna/Kiros and V in 8/9/10 for the dream party.
    - **If V is 8/9/10 and the name is right** → menu fixed. Proceed to v0.17.8.17.8 cleanup.
    - **If V is NOT 8/9/10** (e.g. the model_id held a stale 0-7 or some other value) → the menu's dream identity is not in model_id. The log gives the exact V; if it is consistent per slot, map it. Otherwise the robust fallback is to read the game's own rendered names from the GCW buffer (the `[MenuGCW]` dumps show `...SaveWardLagunaKiros<location>` — the 3 names sit right after "Save" and before the location). Do NOT guess; use the logged value.
  - If Aaron also pressed M, check `[MenuTTS] party slot N ... modelId=V` similarly.
  - Zero-regression: confirm a normal (non-dream) Junction char-select still announces correct names.

## v0.17.8.17.8 cleanup (after validation)

Remove the entire F12 Laguna diagnostic, then squash-push:
  - Delete `src/battle_tts_laguna_diag.inl` and `src/field_nav_laguna_diag.inl`.
  - Remove their `#include`s (battle_tts.cpp after victory.inl; field_navigation.cpp after directiondrive.inl).
  - Remove the `LagunaDiag()` wrappers + decls in battle_tts.cpp/.h and field_navigation.cpp/.h.
  - Remove the F12 dispatcher block in dinput8.cpp.
  - KEEP the new log lines: `[DREAM-ID]` (battle_tts.cpp), `[CMD] charIdx` (BuildCharCommandList), `[MenuTTS] party slot` (AnnounceMenuSummary), and `[JuncTTS] CharSelect ... modelId` (AnnounceJuncCharSelect) — all cheap and useful.
  - BAT for clean build + no behavior change.
  - Then Aaron runs `Utilities/push_to_github.ps1` to squash-push v0.17.8.17 .. .17.8 as ONE Chapter 2 commit. Confirm `github:list_commits` before/after. GitHub HEAD before push = `b6afa8cb`.

## BAT steps for Aaron (copy into the assistant's next message)

  1. Close FF8 if running. Rebuild via `deploy.vbs`. Confirm `Logs/build_latest.log` shows `Version 0.17.8.17.7` + "Build successful".
  2. In a Laguna dream, open the menu and go into Junction. Arrow across the 3 party members — confirm they announce as Ward/Laguna/Kiros (matching on-screen order), not Selphie/Squall/Zell. (Optionally press M for the summary too — also fixed.)
  3. Open a normal (non-dream) menu and Junction char-select — confirm names are still correct (zero-regression).
  4. Say "BAT" with what the Junction char-select announced in the dream.

## Other open work (NOT this session's focus)

- Chase-chapter carry-over (v0.15.9.8.3 bridge catch + v0.15.3.1 chase-agent summary log)
- Source-file refactor queue (only if approaching 80 KB)
- `DEVNOTES_HISTORY.md` trim
- Plan & Research Documents update (Dollet countdown doc)
- GitHub issue #27 (R key "No SeeD rank yet" — `FIELD_H_OFFSET=0xF94` hypothesis)

## Session-start ritual reminders

- Read `DEVNOTES.md` and THIS file at session start. `DEVNOTES_HISTORY.md` only when tracing past decisions.
- Filesystem MCP for all Windows project files. Bare `view`/`str_replace`/`create_file` reach the Linux container only.
- `filesystem:edit_file` corrupts files when the replacement contains a literal `$`. Use hex `0x24` in source, or `filesystem:write_file`.
- OneDrive transient EPERM on first `edit_file` rename — retry once.
- Aaron pushes via `Utilities/push_to_github.ps1`. Claude NEVER pushes. Utility refuses if CHANGELOG top heading != FF8OPC_VERSION.
- Diagnostics on F12 only.
- F-key handlers gated on `!(GetAsyncKeyState(VK_MENU) & 0x8000)`.
- Laguna-chapter multi-change exception still in force.
- **Minimize BAT cycles** — the dream is slow to reach. Leverage existing logs, disassembly, and FFNx source before asking for a BAT. A BAT to verify a fix or pin something down is OK; diagnostic-only round-trips are not.
- BAT response: read `Logs/build_latest.log` first, then domain logs.
- Update DEVNOTES + this file at every version bump AND after every BAT.

## Key carry-forward learnings

- **Dream-party data lives in the regular char-data array (CONFIRMED v0.17.8.17.5).** `char-data[SAVEMAP_PARTY_FORMATION[slot]]` IS the active dream character's struct: `commands[3]`@+0x50, `magics[32]`@+0x10, GF mask@+0x58, `exp`@+0x04, `model_id`@+0x08. The savemap formation (`SAVEMAP_PARTY_FORMATION` = `VICTORY_PARTY_ADDR` = 0x1CFE74C; menu reads it at savemap `+0xAF1`) holds the STALE regular field formation `[05 00 01]` during a dream — correct for INDEXING char-data, wrong as a NAME source.
- **Three dream-identity sources, by context:**
  - In battle / victory (battle module live): `compStats[slot]+0x1C3` actor-kind (8=Laguna, 9=Kiros, 10=Ward). compStats base 0x1CFF000, stride 0x1D0.
  - Main menu (no battle): the loaded char struct's `model_id` (+0x08) — assumed 8/9/10 for dream chars (BAT-confirming via the `[MenuTTS] party slot` log).
  - Field: `setpc` (field entity +0x255).
- **`GetCharNameById(id)`** (battle_tts.cpp): id<8→CHAR_NAMES, 8→Laguna, 9→Kiros, 10→Ward. Already correct — bugs were wrong INPUTS, not a missing mapping.
- **`GetVictoryCharName(slot, fallbackId)`** (battle_tts.cpp, v0.17.8.17.6): dream-aware victory name; reads `s_dreamSlotCharId[slot]` snapshot.
- **`GetCommandName`** maps ability IDs: Attack=0x01, Magic=0x14, GF=0x15, Draw=0x16, Item=0x17, Card=0x18, Devour=0x19, MiniMog=0x21, Defend=0x22, Recover=0x24, Absorb=0x25, Revive=0x26, LV Down=0x27, LV Up=0x28, Mug=0x36, Treatment=0x38. Unknown→"???".
- **Disassembly on disk:** `Game Files/disassembly/` (8 `.text_*.asm` + index txts; NOT in container, NOT content-greppable). FFNx canary C++ at `FFNx-Steam-v1.23.0.182/Source Code/FFNx-canary/src/` has named structs (`ff8/save_data.h`: `savemap_ff8_character` model_id@0x08, commands[3]@0x50, etc.).
- **`filesystem:edit_file` with `dryRun:true`** = content-grep substitute (empty diff on a no-op self-replace confirms the string exists in that file).
