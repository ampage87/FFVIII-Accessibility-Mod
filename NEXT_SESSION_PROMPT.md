# Next Session Prompt — Status detail pages 1-3 (#54)

## Greeting & ritual

Start every response with `## Claude Says`. Read `DEVNOTES.md` and THIS file before any work; `DEVNOTES_HISTORY.md` only when tracing a past decision. Then `github:list_issues` for the live backlog and confirm HEAD with `github:list_commits`.

## Where we are at session open

**Status Limit Break chapter (#49) DONE & CLOSED — shipped through v0.18.2.33 (BAT-confirmed all chars); local tree v0.18.2.34 (gated diags). Confirm GitHub HEAD with `github:list_commits`.** Recently shipped:

- **v0.18.2.x menu chapter (closed #48)** — all main-menu character-select TTS: Junction reserve announce, "Rearrange party order" party-nav, Magic/Status char-select, active/reserve grouping cues. Detail in `CHANGELOG.md` v0.18.2.0–.23 + `DEVNOTES_HISTORY.md`.
- **v0.18.2.24 — community PR #26 (@djoleninja):** review/rewrite of all 21 disc-0 FMV audio descriptions, taken wholesale (merge conflict resolved), rebuilt + BAT-confirmed, pushed. PR merged and the thank-you comment to djoleninja is **posted** — that loop is closed, nothing outstanding.

## THIS SESSION — Status detail pages 1-3 (#54)

The Status DETAIL view has 4 pages cycled with L1/R1; page 4 (Limit Break) shipped in #49. **#54 = the remaining three:** (1) Stats & equipment, (2) Elemental & status resistances, (3) GF compatibility — identical in structure across characters, so one implementation covers everyone.

**Build on the existing `src/menu_tts_status.inl`** (dispatch `+0x1E8==5` / focus `+0x22E==3`, GCW snapshot/decode, SEH raw reads, `/`-key help re-read). Add page-1/2/3 handling alongside the limit-page path, each gated on page index **`+0x257`** (3 = limit confirmed; 1-3 likely 0/1/2 — **verify which value is which FIRST**).

**Discovery (no sighted step):** flip `ST_LIMIT_DIAG`→`true` in `menu_tts_status.inl` for `[STBAND]`/`[STLIMIT]` dumps; SUBMON harness (`menu_tts_diagnostics.inl`) for page/cursor bytes; read `ff8_menu.log`.

**DESIGN FINALIZED with Aaron (full spec in #54 comments).** Model: each page, on entry, speaks its name + enumerates its number-key shortcuts; keys speak labeled values (#44). **Page 1 (Character Statistics):** 0 overview (name/level/HP) · 1 experience · 2-7 Str/Vit/Mag/Spr/Spd/Luck · 8 Evade+Hit · 9 equipment (condensed enumeration on entry). **Page 2 (Elemental & Status):** 1 Elemental Attack · 2 Elemental Resistances · 3 Status Attack · 4 Status Resistances. Page 2 has 4 panels: elem-atk/st-atk = single icon+% (only render when junctioned), elem-def 8-grid, status-def ~13-grid. **Page 3 (GF Compatibility):** 0 = list all GFs + compat; diamond = junctioned to this char.

**NEXT ACTION = diagnostic build** (no design questions left): flip `ST_LIMIT_DIAG` / use SUBMON to locate the rendered value buffer(s) — the % and stat numbers are font-rendered TEXT, just in a different window than `SnapshotGcwBuffer` currently grabs (candidate computed-stats buffer `character_data_1CFE74C`; `ff8_win_obj` array at `0x01D2B330`). Page-1 stats are computed (incl. junction bonuses) — capture rendered, don't recompute. Element/status identities on page 2 are icon SPRITES → fixed-order tables (elements Fire/Ice/Thunder/Earth/Poison/Wind/Water/Holy; status-def ~13: Death/Poison/Petrify/Darkness/Silence/Berserk/Zombie/Sleep/Slow/Stop/Curse/Confuse/Drain — confirm order via kernel Section 31). Attack-panel element/status named from junctioned magic (char `+0x65` Elem-Atk / `+0x66` ST-Atk). Char record live base ~`SAVEMAP+0x48C` stride `0x98`; gf_compat char `+0x70` display `(6000-raw)/5` (verify vs Squall: Quez 504/Shiva 1000/Ifrit 289/Siren 528/Diablos 648). **#50** (Angelo gauges) deferred.

*(#49 original chapter plan — completed, kept for reference:)*

**Goal:** wire up the per-character **Status screen** options related to **Limit Breaks** for the characters that have them — Aaron specifically named **Squall, Zell, and Rinoa**. Per Aaron, **Zell's and Squall's limit breaks are effectively inaccessible to a blind player without these options being readable/adjustable**, so this is the priority of the chapter, not cosmetic.

**Build on what's done, don't re-derive:** the Status screen's *character-select* is already handled — subsystem `pMenuStateA+0x1E8 == 5`, char-select cursor `+0x1E9` → roster `+0x1DB`, focus `+0x22E` in {0,8} (v0.18.2.20–.23, shared `AnnounceJuncCharSelect`). The NEW surface is what appears **after** you confirm a character on the Status screen: that character's Status **detail** view, and within it the Limit Break option(s) that can be read and changed.

**Step 1 is discovery + existing-knowledge-first (do BEFORE coding):**
1. **Check `Plan & Research Documents/` and the disassembly FIRST** (per the standing rule — v0.14.73 shipped wrong data by skipping a research doc). Look for anything on Status-screen layout, limit-break flags/settings, and the per-character limit data. Use `project_knowledge_search` for the disassembly and filesystem tools for the research docs.
2. **Pin down the exact mechanic with Aaron before building** — confirm precisely what "options that can be adjusted" means per character (e.g. what Squall's vs Zell's vs Rinoa's option is, what values it toggles between, and whether it lives on the Status detail screen or an adjacent panel). Aaron's domain knowledge is ground truth here; ask him to describe one concrete example (which character, what the option is, what changing it does) and verify it empirically. Do NOT assume the FF8 mechanic — confirm it.
3. **Find the offsets with the SUBMON auto-monitor harness** (`menu_tts_diagnostics.inl`) — the same memory-diff path that found the Item/Junction/GF/Magic-Status offsets. It diffs memory as the cursor moves, so there is **no "press a key when X is on screen" step**: Aaron navigates the Status detail / limit option, the monitor logs which bytes near `pMenuStateA` change. Read the SUBMON output from `ff8_menu.log`. Unknowns to find: the Status-detail phase byte (analog of Junction `+0x22E`), the option/sub-cursor offset, and the value field for each adjustable limit option.

**Suggested implementation (mirror the GF/Ability chapters, which worked):**
- New `menu_tts_status.inl` (or extend the existing Status handling) — textual `#include` from `menu_tts.cpp`, statics-first `*_state` block, no header guards/namespace inside the `.inl`, keep under the 60 KB soft cap.
- Dispatch seam gated on `+0x1E8 == 5`, suppressed while other submenus are active; reset state on entry/exit.
- Announce the option name + its current value on cursor move, and announce the new value when it's changed. Never speak a raw number a blind player can't otherwise verify (the #44 rule) — give it meaning.
- File ONE GitHub issue (label menu-tts / accessibility) for the chapter at the start, close it against the shipping version at wrap-up. (Offer this to Aaron; he hadn't filed it yet as of last session.)

## Backlog — secondary, pick with Aaron if Status work stalls (live list: `github:list_issues`)

- **#30** menu_tts.cpp T-handler missing `!shift` gate (trivial). **#36** F9 auto-drive gateway `driveSkipTrigIdx` range guard. **#43** calibrate remaining GF per-level EXP costs (web research doable). **#33** Dollet timer doc.
- **#5** announce disabled/greyed menu items — related to Status if any limit option is conditionally unavailable.
- **#2** (high) speech-rate default offset; **#18/#19** battle Magic scroll + page/slot; **#20/#22** Draw "???" / wrong character; **#21** location names as numbers; #3, #6, #7, #9, #15, #25.
- Field/world verify items (#31, #34, #35, #27) need in-game traversal (flip `LINEDIAG_ENABLED`=1). Blocked/hold: **#45** (junction non-command abilities — no GF teaches one yet); #28/#29 (world-map planner); #37/#38/#39.

## FMV audio descriptions — carry-forward (from the PR #26 work)

- VTTs live in `Audio Descriptions/disc*_*h.vtt`, embedded as **RCDATA at build time** (`resources.rc`); parser is `src/fmv_audio_desc.cpp`. **Any VTT edit needs a rebuild to ship.** Parser skips `NOTE` blocks and tolerates minor timestamp typos (lenient `sscanf`); flag duplicate cue start-times (the later cue interrupts/drops the earlier — chain the start to the prior cue's end).
- Disc-2+ have paired `_ad.vtt`; only disc-0/1 + the intro / opening-credits VTTs were in PR #26's scope. @djoleninja may submit more passes — **when reviewing a future PR, verify merged file content with `github:get_file_contents`, not just "merge succeeded"** (the v0.18.2.24 stale-PR conflict-marker incident).

## Reusable reference — DO NOT re-derive

**Menu char-select offsets (v0.18.2, vs `pMenuStateA`):** `+0x1E8` subsystem (`0xFF` bare / `17` Junction / `14` Ability / `3` Magic / `5` Status); `+0x22E` focus (`0`/`8` = char-select); `+0x1E9` char-select cursor → roster `+0x1DB` (all members incl. reserves, `0xFF`-terminated); `+0x71E` per-character menu HP array (stride 0x20, cur +0 / max +2, covers benched); battle formation `savemap+0xAF0` = active-party test. SUBMON harness in `menu_tts_diagnostics.inl` (no sighted step).

**GF memory map (v0.18.0):** GF savemap record base `SAVEMAP_BASE + 0x4C`, stride `0x44`, 16 records canonical order: name `+0x00`, Current EXP u32 `+0x0C`, obtained `+0x11`, HP u16 `+0x12`, complete_abilities[16] `+0x14`, APs[24] `+0x24`, kills `+0x3C`, learning ability id `+0x40`. GF-list cursor `pMenuStateA+0x253`; gate `+0x1E8==4`. AP for an ability = `APs[slot-of-id]` (slot from `gf_ability_slots[gf]`, key off id not display order); cost `ability_ap_cost[id]`; helpers `GFAbilityApCost`/`GFReadAbilityAP` in `menu_tts_gf.inl`. GF level = flat per-level cost (`level = exp/cost + 1`); `GF_EXP_PER_LEVEL[16]` has only CONFIRMED costs (Quez/Shiva/Ifrit/Diablos 500, Siren 400; rest 0 = EXP only) — **#43** fills the rest.

**Character savemap (carry-forward, verify against research docs):** per-CHARACTER record holds stats + `gf_compat[16]` at char `+0x70` (u16 by GF id, display = `(6000 - raw)/5`). **Status/limit fields are NOT yet mapped — that's this chapter's discovery; check `Plan & Research Documents/` first.** Savemap header = 76 bytes (0x4C), community offsets run +0x14 too high — subtract 0x14. Base `0x1CFDC5C`.

**Status limit page (#49, SHIPPED v0.18.2.33 — `src/menu_tts_status.inl`):** page index `pMenuStateA+0x257==3`; detail focus `+0x22E==3`. **Toggles** (keyed off GCW help, announce name+on/off, re-announce on flip): Gunblade Auto `savemap+0xD1C & 0x01` (1=ON), Zell Duel-Auto `+0xD1C & 0x02` (set=ON), Renzokuken Indicator `+0xD1D & 0x80` (0=ON, INVERTED) — announces "disabled" on focus when Gunblade Auto on (greyed but help still drawn). `+0xD1C` shared auto bitfield (Irvine likely bit 0x04). **Per-char cursor byte (hardcoded):** Squall `+0x25F` (step1, leadingToggles4: cur 0-3 = 2 toggles x ON/OFF columns, 4+ = finishers), Zell `+0x260`, Quistis `+0x262` (auto-detect), Rinoa `+0x263`. **Read-only lists** via GCW longest-match parse; baked move descriptions on the toggle pages (Squall/Zell) because GCW help lags the cursor there, with a learned-row→full-table index map. **`ST_LIMIT_DIAG` flag (off)** in the `.inl` re-enables `[STBAND]`/`[STLIMIT]` to map Irvine's Shot page. **#50** = Angelo gauges (data ~`savemap+0xB16`, GF-style known/learning/points).

**SeeD savemap reference:** `.ff8` saves are FF7/FF8 LZSS-compressed (4-byte LE size header; N=4096 F=18 THRESHOLD=2, init-pos 0xFEE). Live offset X == decompressed-file offset `0x184 + X`. SeeD points `+0x0D6C` (u16, rank = points/100); salary count `+0x0CDE`; steps-since-pay `+0x0D64`; gameplay Gil `+0x0B08` (u32).

## Push-flow & build reminders

- **Aaron pushes via `Utilities/push_to_github.ps1` (double-click the `.vbs`); Claude NEVER pushes.** Utility refuses unless `CHANGELOG.md`'s top `## vX.Y.Z` heading matches `FF8OPC_VERSION`. **After a push, verify with `github:list_commits` AND a touched file's content — don't trust "I pushed" alone.** The v0.18.2.24 push didn't fire on the first attempt; `Logs/push_diagnostic.log` shows each `.ps1` run and `Logs/git_latest.log` shows the git result — read those to triage a non-landing push.
- **Version bump = one place:** `FF8OPC_VERSION` in `src/ff8_accessibility.h`, paired with a new top `CHANGELOG.md` entry. Deploy = `deploy.vbs` → `src/deploy.ps1` → `src/deploy.bat` (update `deploy.bat` only when adding/removing source files).
- **"BAT"** = read `Logs/build_latest.log` tail for version + success, then the relevant domain log (here `ff8_menu.log`). Read the full relevant log, not just the tail.
- **`filesystem:` tools for all Windows project files.** `edit_file` multi-edit is all-or-nothing (one stale `oldText` silently rejects the batch — read exact current text first; `dryRun:true` tests an anchor). A literal `$` in `newText` corrupts the file — use `write_file` for full rewrites with `$`. OneDrive can throw a transient EPERM on `edit_file` rename — retry once.
- **F12 = per-session diagnostic only** (one at a time; remove old before new). F-key handlers gate on `!(GetAsyncKeyState(VK_MENU) & 0x8000)`. **NEVER re-enable the SET3 hook (0x1E)** — hangs the infirmary scene (CI guard). Source size 60 KB soft / 80 KB hard (CI + push Step 7c); split via `.inl`.
- **DEVNOTES.md hard 10 KB cap** — long narratives go to `DEVNOTES_HISTORY.md`. Update DEVNOTES + this file at every version bump and after every BAT.

## Closed / verified — do NOT re-open

- **PR #26 / v0.18.2.24** — disc-0 FMV audio descriptions (djoleninja), merge conflict resolved, pushed + verified clean; thank-you comment posted.
- **v0.18.2.x menu chapter** — #10, #42, #46, #47, #48 closed; shipped through v0.18.2.23 (`34cbefc`).
- **GF submenus** — #41 + #44 closed, v0.18.0.15 (`9102689`); #40 dup of #41. **Ability "Use GF ability" refine flow** — #42 closed, v0.18.1.13 (`9cba532`).
- **Track A F9 auto-drive** — v0.17.9.17 (`808d4802`); **exit-destination interpreter** — v0.17.9.6 (`502516c3`) + v0.17.9.11 (`3478683`); **Chapter 5 SeeD rank/salary** — v0.17.9.1 (`5c3af6a5`). Diagnostics gated off.
- **deploy.bat "Version:" extraction** — verified correct (`/B` anchor); don't touch.
