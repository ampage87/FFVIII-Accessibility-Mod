**Purpose & context**

Aaron is the sole developer of the FF8 Accessibility Mod — a `dinput8.dll` injection for Final Fantasy VIII Steam 2013 edition (App ID 39150, FF8_EN.exe + FFNx v1.23.x) that makes the game playable for blind players via Windows SAPI text-to-speech, navigation assistance, and entity catalogs. Aaron is blind himself and is the primary tester, using NVDA as his screen reader.

**Project root:** `C:/Users/ampag/OneDrive/Documents/FFVIII-Accessibility-Mod/FF8_OriginalPC_mod/`

GitHub: `ampage87/FFVIII-Accessibility-Mod`. `main` HEAD = `423e58a1` (v0.14.102 pushed). Local v0.14.103/.1/.2/.3 awaiting BAT pass before push.

---

**Current build: v0.14.104 IMPLEMENTED, ready to push.**

**v0.14.103.7 BAT PASSED ✓** — Aaron's report: "That worked well. Only glitch was I tried to use AD to drive to the Fire Cavern but couldn't get there. I think that is because there is a forest right in front of it and not because of a glitch with the mod." BAT log (Thu 2026-05-07 11:27-11:28): Balamb Garden announced foot-friendly=NO with sweep-abort 1/2 at ~13s (67% reduction from v0.14.103.5's 40s). Balamb Town announced foot-friendly=YES, drove all the way in via auto-dismount, MODE_FIELD arrival in ~9s. Both behaviors work as designed. Fire Cavern issue is real but unrelated to the foot-friendliness check — see GitHub issues filed for v0.15.x.

**v0.14.104 cleanup pass:** removes dead code now that v0.14.103.x line is validated. Strips: (1) [VEH-VERIFY] one-shot diagnostic dump in StartAutoDrive (savemap +0x125C offset confirmed in v0.14.103.3); (2) s_vehVerifyFired flag + Initialize reset; (3) per-frame frozen-position bounce-detection block in UpdateAutoDrive (cars oscillate, never freeze — the criterion never fired in practice); (4) s_carBounce* state and DRIVE_BOUNCE_FRAMES/DELTA/SAMPLE constants; (5) StopAutoDrive and Initialize resets for the dead state. KEEPS the live sweep-abort retry-counter mechanism (s_sweepAbortCount, DRIVE_BOUNCE_ABORT_THRESHOLD=2, s_destFootFriendly). Net: ~110 lines removed, 0 added. Functionally identical to v0.14.103.7.

**Push plan:** Aaron pushes v0.14.103/.1/.2/.3/.4/.5/.6/.7/.104 as a consolidated commit via Utilities/push_to_github.bat. Then Claude files the two Fire Cavern follow-up issues.

---

**v0.14.103.x BAT history**

- v0.14.103 BAT (21:41-21:43): foot AD to Balamb Town succeeded, 3-state terrain classifier loaded (185/10/573). Aaron clarified he DID also test car AD to Balamb Garden, but bounce-detection didn't fire because of `inCar` gate (locomotion byte stays at 6 in rental car).
- v0.14.103.1 (drop inCar gate, dump both candidate offsets): not BAT'd (rolled into v0.14.103.2).
- v0.14.103.2 BAT (22:11:38): build clean, [VEH-VERIFY] dump fired correctly at first drive start. AD drove car to Balamb Garden but bounce-detection STILL didn't fire. The 4-frame frozen-position criterion doesn't match real-world car-vs-wall behavior (oscillation, not freeze).

**Key empirical findings from v0.14.103.2 BAT [VEH-VERIFY] dump:**

```
foot DWORDs: X=16031, Y=-26948, Z=-538

Candidate A (+0x125C) WORKS:
  car_pos @ 0x01CFEEE8 = [16031, 0, -26948, ...]   ← exact match to foot DWORDs!
  char_pos @ 0x01CFEEB8 = [16149, 0, -26721, ...]  ← slightly different
  car_rent = 81  ← non-zero (rental car flag set)

Candidate B (+0x225C): all zeros (wrong region)
```

1. WM_WORLDMAP_OFFSET = 0x125C is correct (Candidate A).
2. **Savemap WORLDMAP struct stores positions at SAME scale as foot DWORDs**, NOT 20.12 fixed-point. The `WM_SAVEMAP_TO_DWORD_SCALE = 4096` was wrong.
3. car_rent flag at 0x01CFEF1A is a useful "is in rental car" signal (does not suffer locomotion-byte-stays-at-6 problem).

---

**v0.14.103.3 changes**

1. **Sweep-abort site** (line ~3705 in world_map.cpp) now declares bounce-arrived instead of resetting sweep state and looping forever. Announces "Arrived near \[Location\]. The entrance may require a manual approach." + StopAutoDrive(nullptr).
2. **WM_SAVEMAP_TO_DWORD_SCALE = 1** (was 4096). Empirically validated.
3. v0.14.103.2 per-frame bounce-detection block left in place as dead code (cleaned up in v0.14.104).

---

**v0.14.103.3 BAT plan**

1. Run `deploy.vbs`.
2. Check `Logs/build_latest.log`.
3. Launch game, rent a car in Balamb (or use save state from previous BATs), drive AD to Balamb Garden.
4. Expected sequence (~14-16 seconds total):
   - Drive starts cleanly
   - dist drops from ~8000 to ~970 (entered final approach)
   - 6 seconds: final-approach timeout
   - Sweep starts, walks for 1-3 seconds, drifts to dist 1500+
   - `[DRIVE-BOUNCE]` log line + TTS: "Arrived near Balamb Garden. The entrance may require a manual approach."
   - AD stops silently
5. Upload `Logs/ff8_world.log`.

---

**After successful BAT (v0.14.104 cleanup)**

- Strip [VEH-VERIFY] diagnostic dump
- Remove dead bounce-detection state variables (s_carBounce*) and constants (DRIVE_BOUNCE_*)
- Push consolidated v0.14.103.3 → v0.14.104 to GitHub

---

**On the horizon**

Deferred priorities (post-v0.14.104):
1. Persistent accessibility settings across play sessions
2. Remove party members from field entity catalog
3. X-ATM092 chase scene accessibility
4. Walk-and-talk dialog gap
5. SeeD rank bug (#27) investigation
6. Refined-coord steering for narrow-gate locations (foot AD couldn't find Balamb Garden's foot entry in v0.14.103 BAT)

---

**Key learnings & principles**

**Engine / data:**

- **SAVEMAP HEADER = 76 bytes (0x4C)**. Subtract 0x14 from deep-research offsets that assume 96-byte header. Confirmed base: `0x01CFDC5C`. GFs at +0x4C, chars at +0x48C, Gil at +0x08 (header).
- **WORLDMAP struct at savemap+0x125C** (BAT-confirmed v0.14.103.2). Stores: char_pos[6], ragnarok_pos[6], bgu_pos[6], car_pos[6] at offsets 0x00/0x18/0x24/0x30. Each is uint16[X, Z, Y, ...]. car_rent flag at +0x62.
- **Savemap WORLDMAP positions at 1:1 scale with foot DWORDs** (BAT-confirmed v0.14.103.2). NOT 20.12 fixed-point.
- **Locomotion byte at 0x02040A5E does NOT reliably indicate rental car state** — stays at 6 (Selphie foot) even in rental car. Garden=48 and Ragnarok=50 work but rentals don't. **Use car_rent flag at 0x01CFEF1A as alternative signal.**
- **SET3 opcode hook PERMANENTLY DISABLED** — hangs the infirmary scene.
- **Foot DWORDs DO update during rental car drives** — they don't freeze (contrary to deep research). Garden/Ragnarok may be different (have dedicated vehicle modes).
- **Car-vs-wall is OSCILLATION, not freeze** — distance bounces between 970 and 1554 continuously when stuck against an invisible wall. Detection logic must use higher-level signals (like sweep-abort), not per-sample frozen-position counters.
- **Victory TTS must hook text renderer, not read memory**.
- **FF8 element affinity**: 800-anchored u16 scale. Always check `Plan & Research Documents/` BEFORE interpreting any engine data field.
- **Battle entity race condition**: `s_prevBattleMagicId` never resets on battle escape; `[SCAN-CACHE]` fires at same millisecond as `OnBattleEnter`. Fix: capture snapshot after entity-ready check.

**Tooling / workflow:**

- **DEEP RESEARCH DOCS FIRST**: Before writing code that interprets any FF8 engine data field, search `Plan & Research Documents/` for an existing deep research entry.
- **EXISTING KNOWLEDGE FIRST**: For regressions where Aaron mentions 'used to work before Sonnet regression', run `conversation_search` BEFORE writing new logic.
- **Cross-check arithmetic against authoritative target addresses, not summary descriptions.** When deep research natural-language summary disagrees with stated target addresses, trust the concrete examples. (v0.14.103.1 hit this: "+0x1270 minus 0x14 = 0x125C" was a typo; the stated target addresses implied "+0x225C". BAT confirmed +0x125C was actually right anyway, but the summary text was internally inconsistent.)
- **Diagnostic dumps must fire when the data they're dumping is actually loaded** (v0.14.103 [VEH-VERIFY] hit this: ran at module init when savemap was uninitialized, all reads returned 0). Always gate on a "data is ready" precondition.
- **When BAT log seems to show absence of feature exercise, ASK Aaron rather than assuming.** I misread v0.14.103 BAT as "didn't test car AD" because log showed `vehicle type 0` — but Aaron clarified he DID test it. Locomotion byte staying at 6 explained the misleading log.
- **dryRun=true as grep substitute**: `filesystem:edit_file` with `dryRun: true` and probe string — confirms file/location without making changes. Use to locate function boundaries in large `.inl`/`.cpp` files. **Search log file format strings using unique fragments** ("returning to normal") rather than the visible bracket prefix ("[DRIVE-SWEEP]") since prefix may not be a literal in the format string.
- **OneDrive EPERM on first edit**: Retry immediately without other action.
- **Bash cannot reach Windows project files** — ever. All project file access via filesystem MCP tools only.
- **nightsolo tables more reliable** than FF Wiki for per-enemy status vulnerability data.

---

**Approach & patterns**

- **Session ritual**: Read `DEVNOTES.md` and `NEXT_SESSION_PROMPT.md` at start of every session.
- **SESSION CHECKPOINT RULE**: Update DEVNOTES + NEXT_SESSION_PROMPT at TWO checkpoints: (1) every time a version is bumped for Aaron to test, and (2) after every BAT result.
- **BAT workflow**: When Aaron says "BAT" — check `Logs/build_latest.log` tail for build errors, then domain-specific game log (e.g., `Logs/ff8_world.log`).
- **Build error**: Immediately read `Logs/build_latest.log` tail before attempting fixes.
- **Version bump**: ONE location only — `FF8OPC_VERSION` in `src/ff8_accessibility.h`.
- **Build system**: `deploy.vbs` (root) → `src/deploy.ps1` → `src/deploy.bat`.
- **GitHub**: Claude NEVER pushes. Aaron uses `Utilities/push_to_github.bat`. Before quoting any backlog size, call `github:list_commits` — never quote from memory. Provide Aaron: (1) version to bump to, (2) consolidated commit description.
- **Deep research escalation**: When source code, game files, and logs are insufficient, ask Aaron to run deep research via ChatGPT.
- **Source structure**: `battle_tts.cpp` and `field_navigation.cpp` are split into `.inl` textual-include sections. world_map.cpp is monolithic.
- **Large file navigation**: Use `head`/`tail`/`view_range` parameters; for mid-file reads use `dryRun=true` edit probe.

**F12 diagnostic key rule**: F12 is reserved exclusively for per-session diagnostic/debug builds. BEFORE adding any new diagnostic to F12, search all source files for existing `VK_F12` references and remove old diagnostic code first.

**Keyboard shortcut map (complete):**
`` ` `` = repeat dialog/battle event | `V` = mod version | `F1` = cycle voice | `F3/F4` = game vol | `F5/F6` = speech vol | `F7/F8` = speech rate | `F9/F10` = field nav | `F11` = menu summary | `F12` = DIAGNOSTIC BUILDS ONLY | `G/T/L/R` = Gil/Time/Location/SeeD | `/` = help bar | `O` = EWM toggle | `1/2/3/H` = battle HP check | `\` = world map auto-drive | `A` = gas pedal (car only, scan 0x1E NOT extended) | `W` = reverse (car only, scan 0x11 NOT extended)

---

**Tools & resources**

- **Filesystem MCP tools**: All project file access. Never bash for project files.
- **FFNx canary source**: `FFNx-Steam-v1.23.0.182\Source Code\FFNx-canary\src\` — read-only reference for address offsets and struct layouts.
- **Game files**: `Game Files\FINAL FANTASY VIII\` (includes `Data\lang-en\` with field.fi/fl/fs archives, FF8_EN.exe).
- **Full disassembly reference**: `Game Files/disassembly/` — 8 project knowledge files + 9 on-disk `.asm` files (~98MB, 2.76M instructions).
- **Plan & Research Documents/**: Deep research docs. Always check here before interpreting engine data fields.
- **Community references**: nightsolo.net enemy tables, finalfantasy.fandom.com, ff8-speedruns/ff8-memory, myst6re/deling, Qhimm Modding Wiki.
- **Reusable OpenGL screenshot capture**: Only `glReadPixels` via SwapBuffers hook works.
- **Known issue (not a mod bug)**: JAWS intercepts game keys until user presses Insert+3. NVDA unaffected.

---

**Closed issues this session:** #16, #17, #23, #24 (4 issues). Open count: 14. Remaining: #2, #3, #5-10, #15, #18-22, #25, #26 (PR), #27.
