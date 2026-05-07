**Purpose & context**

Aaron is the sole developer of the FF8 Accessibility Mod — a `dinput8.dll` injection for Final Fantasy VIII Steam 2013 edition (App ID 39150, FF8_EN.exe + FFNx v1.23.x) that makes the game playable for blind players via Windows SAPI text-to-speech, navigation assistance, and entity catalogs. Aaron is blind himself and is the primary tester, using NVDA as his screen reader.

**Project root:** `C:/Users/ampag/OneDrive/Documents/FFVIII-Accessibility-Mod/FF8_OriginalPC_mod/`

GitHub: `ampage87/FFVIII-Accessibility-Mod`. `main` HEAD = `560f725b` (v0.14.106 pushed Thu 2026-05-07 evening). v0.14.107 was built locally but its BAT exposed a flaw in the design; v0.14.108 supersedes it and is now built locally, awaiting BAT.

---

**Current build: v0.14.108 BAT-PASSED, awaiting Aaron's push.**

Filters party-member followers (Zell, Quistis, etc., when they're following Squall on the field) out of the F9 navigation catalog, using a **behavioral fingerprint** instead of a canonical model→charId map.

**Why v0.14.107 didn't work**

The v0.14.107 BAT on `bggate_1` (party = Zell + Squall + Selphie = formation `[1, 0, 5, 255]`) showed two follower entities in the catalog as `ent1 model=2` and `ent2 model=4`. In the canonical map those are Irvine and Rinoa, neither in the active party — so the savemap formation cross-reference correctly returned false and the filter no-opped. **Lesson: field entity model IDs are field-local slot indices, NOT canonical character IDs.** The engine reuses model slots per-field.

**The v0.14.108 fix**

Replace the canonical-mapping filter with a behavioral fingerprint that holds regardless of which model slot the field assigned:

- `i != s_playerEntityIdx` (not the player)
- `modelId >= 0 && modelId < 10` (visible character in party-character model range; excludes save points at model 24 and other non-character interactive objects)
- `throughonoff > 0` (player walks through them)
- `talkonoff == 0` (not talkable)
- `pushonoff == 0` (no collision)

That's the exact fingerprint of a follower: a visible character that the player walks through with no interaction. Same logic also catches non-interactive cutscene characters that walk through scenes — also correctly skipped since they're not navigation targets either.

**What changed (v0.14.108)**

- `src/field_nav_catalog.inl`: replaced the v0.14.107 standalone filter block (canonical model→charId + savemap cross-reference) with the behavioral fingerprint check inside the qualification loop. The filter logs `[party-filter] entN model=M filtered (visible walk-through, no interaction)`.
- `src/ff8_accessibility.h`: `FF8OPC_VERSION` bumped to `0.14.108` with full summary comment. v0.14.107 comment retained below.
- `CHANGELOG.md`: `## v0.14.108` entry at top, push-quality.
- v0.14.107 helpers (`IsCharacterInActiveParty`, `ModelIdToCharId`) and `[party-state]` diagnostic remain in place — harmless, well-documented, and may be useful for future party-aware features. Only the filter call site changed.

No new addresses, no new hooks, no `deploy.bat` changes.

**v0.14.108 BAT plan**

1. `deploy.vbs`.
2. Load a save with a 2- or 3-member party (post-Dollet — Squall + Zell + Selphie, etc.).
3. Walk into a populated field with NPCs (Garden hallway, classroom, town).
4. Press F9 to cycle the entity catalog. Confirm Zell / Quistis / Selphie / etc. are NO LONGER announced as NPCs.
5. Check `Logs/ff8_field.log`:
   - `[party-state] formation = [a, b, c, d]` once per field load (informational only now)
   - `[party-filter] entN model=M filtered (visible walk-through, no interaction)` for each follower skipped
   - The `[refresh]` summary should no longer include followers
6. **Save-point sanity check**: confirm the save point on this or any field still appears as `Save Point` in the catalog and is reachable. Aaron pre-confirmed this works as of v0.14.107; the v0.14.108 filter's `modelId < 10` guard preserves model 24 (save point crystal).
7. Edge cases:
   - **Solo Squall** (Fire Cavern): single-member party. Squall is the player, excluded by `i != s_playerEntityIdx`, no follower entities to filter.
   - **Fire Cavern draw point** (model 9): party-character model range, but should have `talkonoff > 0` (you can interact with it) — filter no-ops, draw point stays in catalog. Worth verifying.
   - **Cutscene character** crossing a scene non-interactively: WILL be filtered. That's intended behavior.

If BAT passes, run `Utilities/push_to_github.vbs`. The utility will read `0.14.108` from `ff8_accessibility.h` and the body from `CHANGELOG.md` automatically.

**Risks to watch**

- **TALKRADIUS race**: if a real NPC has `throughonoff` set in init scripts and TALKRADIUS sets `talkonoff` AFTER the catalog scan, the NPC is transiently filtered. The catalog refreshes on every F9 press, so the next press picks them up — but watch for any field where a known-talkable NPC is missing from the first F9 announcement.
- **Generic NPC throughonly cases (modelId >= 10)** keep their existing buggy `ENT_EXIT` classification. The v0.14.108 filter intentionally leaves them alone; they were broken before and still are. Can extend later if a specific case surfaces.
- **Fire Cavern draw point**: per `field_nav_catalog.inl` comment around line 700, "On Fire Cavern, the draw point entity uses model 9 (party character range)." If the draw point happens to also have `throughonoff > 0` and `talkonoff == 0` at scan time (unlikely — draw points need `talkonoff` to be activated), it would be filtered. Confirm during BAT.

---

**Open GitHub issues at session end (16 total):**
- #2, #3, #5–10, #15, #18–22, #25, #26 (PR), #27, #28, #29

---

**Key learnings & principles**

**Engine / data:**

- **FIELD ENTITY MODEL IDs ARE FIELD-LOCAL SLOT INDICES, NOT CANONICAL CHARACTER IDs**. Confirmed v0.14.107 BAT on bggate_1 with party [1, 0, 5, 255] showed followers at model=2 and model=4. The engine reuses model slots per-field. Don't build filters on top of canonical model→charId mappings.
- **Follower behavioral fingerprint**: visible (`modelId >= 0`), walk-through (`throughonoff > 0`), no talk (`talkonoff == 0`), no push (`pushonoff == 0`). v0.14.108 filters on this.
- **Save points**: model 24, always. `modelId < 10` guard in any character-filter logic preserves them.
- **Fire Cavern draw point**: model 9 (party-character range). Comment in field_nav_catalog.inl at v0.12.12 fallback documents this.
- **SAVEMAP HEADER = 76 bytes (0x4C)**. Subtract 0x14 from deep-research offsets that assume 96-byte header. Confirmed base: `0x01CFDC5C`.
- **Active party formation: savemap + 0xAF0** (`0x01CFE74C`), 4 bytes, charId 0–7 or 0xFF. NOT compacted (solo Squall is `[0xFF, 0x00, 0xFF, 0xFF]`). Same address used by Junction TTS, save block content TTS.
- **Canonical character IDs** (kept for ResolveNameByModelId / future menu-side features, NOT for catalog filtering): 0=Squall, 1=Zell, 2=Irvine, 3=Quistis(casual), 4=Rinoa, 5=Selphie, 6=Seifer, 7=Edea, 8=Quistis(uniform). Models 10+ are generic NPCs.
- **WORLDMAP struct at savemap+0x125C** (BAT-confirmed v0.14.103.2).
- **Savemap WORLDMAP positions at 1:1 scale with foot DWORDs** (BAT-confirmed v0.14.103.2).
- **Locomotion byte at 0x02040A5E does NOT reliably indicate rental car state**. Use `car_rent` flag at 0x01CFEF1A.
- **SET3 opcode hook PERMANENTLY DISABLED** — hangs the infirmary scene.
- **Foot DWORDs DO update during rental car drives**.
- **Car-vs-wall is OSCILLATION, not freeze**.
- **Victory TTS must hook text renderer, not read memory**.
- **Battle entity race condition**: `s_prevBattleMagicId` never resets on battle escape; capture snapshot after entity-ready check.

**Persistence layer (Config) — fully verified working as of v0.14.106:**

- `src/config.cpp` + `config.h`. INI at `<dll-dir>/ff8_accessibility.ini`, section `[Accessibility]`.
- Persisted: `speech_rate`, `speech_volume`, `speech_voice_id`, `game_volume` (BGM), `sfx_volume`, `tts_duck_enabled`, `sfx_duck_ratio`, `ewm_enabled`.
- v0.14.106 ships auto-generated commented INI template with legacy-upgrade path.
- **SAPI `SetVoice()` preserves rate/volume across voice changes** — v0.14.105's diagnostic harness proved this empirically.
- **`GetAsyncKeyState(VK_MENU)` checks Alt** — used to gate F-key handlers from firing during Alt+F4 close.

**Field entity catalog (current state, after v0.14.108):**

- `src/field_nav_catalog.inl::RefreshCatalog()` is the single rebuild path.
- v0.07.97: `pushonoff && modelId >= 10 → ENT_NPC` (visible NPC, talkonoff not yet set).
- v0.12.12: `pushonoff && modelId >= 0 → continue` (visible push-only entity = walking student, not interactable, skip).
- **v0.14.108: behavioral-fingerprint follower filter** — `modelId 0..9 + throughonoff > 0 + talkonoff == 0 + pushonoff == 0 → continue`.
- Player entity detected by `setpc == 0`; `s_playerEntityIdx` updated each refresh.
- v0.14.107 helpers (`IsCharacterInActiveParty`, `ModelIdToCharId`) remain in field_nav_helpers.inl for future use, NOT called from the catalog filter.

**Tooling / workflow:**

- **DEEP RESEARCH DOCS FIRST**: Search `Plan & Research Documents/` before writing engine-data interpretation code.
- **EXISTING KNOWLEDGE FIRST**: For "used to work before Sonnet regression" reports, run `conversation_search` BEFORE writing new logic.
- **Empirical verification before relying on engine-internals assumptions**. v0.14.107's failure was relying on the canonical model→charId mapping without testing. v0.14.108 ships with a behavioral fingerprint that's directly observable in the BAT log.
- **OneDrive EPERM on first edit**: Retry immediately.
- **Bash cannot reach Windows project files** — ever. Use filesystem MCP tools.
- **dryRun=true edit_file works as grep substitute**.

---

**Approach & patterns**

- **Session ritual**: Read `DEVNOTES.md` and `NEXT_SESSION_PROMPT.md` at start of every session.
- **SESSION CHECKPOINT RULE**: Update DEVNOTES + NEXT_SESSION_PROMPT at TWO checkpoints: (1) every version bump for testing, (2) after every BAT result.
- **BAT workflow**: Check `Logs/build_latest.log` tail for build errors, then domain-specific game log.
- **Build error**: Read `Logs/build_latest.log` tail before attempting fixes.
- **Version bump**: ONE location only — `FF8OPC_VERSION` in `src/ff8_accessibility.h`.
- **Build system**: `deploy.vbs` (root) → `src/deploy.ps1` → `src/deploy.bat`.
- **GitHub**: Claude NEVER invokes the push utility. Aaron runs `Utilities/push_to_github.vbs` after BAT passes. The utility reads `FF8OPC_VERSION` from `src/ff8_accessibility.h` and the commit body from the top entry of `CHANGELOG.md` automatically. **Every version bump in `ff8_accessibility.h` MUST be accompanied by a new top-of-file entry in `CHANGELOG.md`, written to push-quality.** The push utility validates that the two versions match and refuses to push otherwise. Before quoting any backlog size, call `github:list_commits`.

**F12 diagnostic key rule**: F12 is reserved exclusively for per-session diagnostic/debug builds.

**Keyboard shortcut map (current as of v0.14.108):**
`` ` `` = repeat dialog/battle event | `V` = mod version | `F1` = cycle voice | `F2` = toggle audio ducking | `F3`/`F4` = speech rate down/up | Shift+`F3`/`F4` = speech volume down/up | `F5`/`F6` = SFX volume down/up | `F7`/`F8` = BGM volume down/up | `F9`/`F10` = field nav | `F11` = on-demand screenshot capture | `F12` = DIAGNOSTIC BUILDS ONLY | `G/T/L/R` = Gil/Time/Location/SeeD | `/` = help bar | `O` = EWM toggle | `1/2/3/H` = battle HP check | `M` = menu summary | `\` = world map auto-drive | `A` = gas pedal (car only) | `W` = reverse (car only). **All F-key accessibility handlers gated on `!alt`** so Alt+combos (notably Alt+F4) don't fire them.

---

**Tools & resources**

- **Filesystem MCP tools**: All project file access. Never bash for project files.
- **FFNx canary source**: `FFNx-Steam-v1.23.0.182\Source Code\FFNx-canary\src\` — read-only reference.
- **Game files**: `Game Files\FINAL FANTASY VIII\`.
- **Full disassembly reference**: `Game Files/disassembly/`.
- **Plan & Research Documents/**: Deep research docs.
- **Community references**: nightsolo.net, finalfantasy.fandom.com, ff8-speedruns/ff8-memory, myst6re/deling, Qhimm Modding Wiki.
- **Reusable OpenGL screenshot capture**: Only `glReadPixels` via SwapBuffers hook works.
- **Known issue (not a mod bug)**: JAWS intercepts game keys until user presses Insert+3. NVDA unaffected.

---

**Recent work block:**
- v0.14.105 — Alt+F4 fix, all F-key accessibility handlers gated on `!alt`. BAT-passed and pushed.
- v0.14.106 — Strip diagnostic harness; add commented INI template with auto-upgrade. BAT-passed and pushed (HEAD `560f725b`).
- v0.14.107 — Party-member filter take 1 (canonical model→charId + savemap formation cross-reference). BUILT, BAT-failed (filter never matched on bggate_1 because the engine reuses model slots per-field). Superseded by v0.14.108 — never pushed.
- **v0.14.108** — Party-member filter take 2 (behavioral fingerprint). BUILT, BAT-passed Thu 2026-05-07 evening across multiple fields and party compositions. Followers correctly filtered, save points preserved, real NPCs kept, interactive objects kept. Awaiting Aaron's push via `Utilities/push_to_github.vbs`. ← we are here.

**v0.14.108 BAT evidence (for the record):**
- bggate_1 with party `[1, 0, 5, 255]` (Squall + Zell + Selphie): followers ent1 model=2 and ent2 model=4 filtered. Catalog dropped from 6 to 4 entries. First nav announce is now a real exit, not a phantom NPC.
- B-Garden Hall (probable bghall_1) with party including Quistis and Selphie: followers ent1 model=3 and ent2 model=5 filtered. Real NPCs at models 18 and 20 kept. **Save point at model 24 preserved** — confirms `modelId < 10` guard works. Directory interactive object (JSM ent25) kept.


---
