# NEXT SESSION — FF8 Accessibility Mod

## ▶ STATUS: v0.18.3.238 LOCAL — BAT PASS, PUSH-READY. Playtest chapter #71–#76 COMPLETE.
**.238 BAT (21:34–21:42, Ifrit beaten 21:42:31): PASS.** Exactly ONE "Timer detected" (21:35:40, 40-min pick); zero re-announcements across the random battles (21:36:20, 21:37:10); dismissal at 21:42:40 "0x01D2B813=0 sustained" (9 s post-victory); INACTIVE held through quit while the raw global kept counting; gate log fired once (rate-limited). Known trade-off (by design, commented in #75): T pressed inside the 4 s debounce window right after the victory still speaks the last remaining time; a second later it's "No timer active." **Issues: #72/#73/#74/#75/#76 CLOSED with BAT evidence; #71 open LOW-PRIORITY** (only the hypothetical invisible non-formation scene actor / entity SHOW-HIDE flag discovery — no known field exhibits it). **Chapter push-ready: version==CHANGELOG top (0.18.3.238), no oversized files (field_navigation.cpp 80.0 KB — closest to the 81,920 cap; battle-pause split into `field_nav_battlepause.inl`). Aaron runs `Utilities/push_to_github.ps1`.** Next arcs when ready: #70 world-map walking umbrella, DEVNOTES still ~1 KB over its 10,240 cap (trim the .211 routenet block when that arc resumes).

---

_(Superseded pre-final-BAT status below.)_

## ▶ STATUS: v0.18.3.238 LOCAL — #75 core fix BAT-CONFIRMED on .237; .238 adds the dismissal DEBOUNCE (kills the per-battle "Timer detected" re-announcement), awaiting final BAT
**.237 BAT (21:02–21:10, Ifrit beaten 21:09:29):** timer went INACTIVE at 21:09:38 ("HUD timer dismissed") and STAYED inactive for the walk-out while the raw global kept counting — the phantom is dead. Two rough edges fixed in **.238 (countdown_timer.cpp only):** (1) the engine blanks 0x01D2B813 ~2 s on every battle→field return, so each random battle caused a dismiss + fresh "Timer detected. X remaining." (21:04:21/23, 21:04:54/56) — dismissal now requires the flag to read 0 for 4 s continuously on-field (`HudDismissedDebounced`, shared ACTIVE/FROZEN; off-field/visible/fault resets the clock); (2) the "staying INACTIVE" activation-gate log line spammed every ~2 s post-Ifrit — rate-limited to 30 s. **BAT .238:** one "Timer detected" per session only; T in-trial and in-battle unchanged; after the Ifrit victory T says "No timer active." with `ENTER INACTIVE ... 0x01D2B813=0 sustained`. Then #75 can close and the chapter is push-ready pending Aaron's push.

---

_(Superseded .237 status below — its BAT is done; results above.)_

## ▶ STATUS: v0.18.3.237 LOCAL — #75 TIMER-DISMISSAL FIX APPLIED (flag = engine byte 0x01D2B813), awaiting BAT; #72/#73/#74/#76 CLOSED on the .236 BAT
**CORRECTION to the block below:** the .236 BAT DID reach and beat Ifrit (fight 20:37:41–20:42:31, "GF Ifrit acquired" 20:42:39; the game was closed at 20:46 during the walk-out battles). #73/#74 are therefore verified against the original boss case, and the timer dismissal data WAS captured: nothing in the 96-byte window flips at dismissal — 0x01CFE934 (which ticks 4→5 at the victory) is a BATTLE COUNTER (1 at cavern entry, +1 per victory, 7 after the exit battles), and 0x01CFE950 stays 1 throughout, so neither is the flag.

**The real flag, from a disasm xref sweep on 0x01CFE92C:** engine fn 0x004A6CC0 = set-timer-visible(arg) → writes byte **0x01D2B813**; the MM:SS HUD renderer 0x004A6D40 tests that byte FIRST and skips drawing when 0 (then reads 0x01CFE92C, clamps 0x1797, /60, draws). Display-pipeline truth, per the standing rule. **.237 changes (countdown_timer.cpp):** ACTIVE/FROZEN → EnterInactive("HUD timer dismissed") when ON FIELD and 0x01D2B813==0 (field-gated; SEH fault ignored); activation additionally requires the flag nonzero; TIMER_DISMISS_DIAG back to 0. Carries the [SPELL-MISS-SKIP] "not 0x9" wording fix.

**BAT .237:** rebuild via deploy.vbs, confirm 0.18.3.237. Replay the cavern: (1) timer detected on entry as usual; (2) T during the trial announces remaining; (3) after the Ifrit victory, T must say **"No timer active."** and the log must show `ENTER INACTIVE ... HUD timer dismissed (0x01D2B813=0)`; (4) watch-item: any spurious re-"Timer detected" after mid-trial dialogs (would mean the engine clears the flag during dialogs — then the check needs a dialog gate). Issues: #72/#73/#74/#76 closed with BAT evidence; #71 open low-priority (invisible non-formation scene actor / SHOW-HIDE flag); #75 open pending this BAT.

---

_(The block below predates the correction — its "run ended before Ifrit" claim and the 0x01CFE950 candidate are WRONG; kept for the audit trail.)_

## ▶ STATUS: v0.18.3.236 LOCAL — BAT PASS on 5 of 6 (run ended before Ifrit); #75 flag candidate FOUND at 0x01CFE950
**BAT 2026-07-13 20:17–20:46 (run ended mid-battle before reaching Ifrit — finish-to-Ifrit still wanted):**
- **#72 PASS:** two mid-drive battles paused cleanly (20:44:45, 20:46:06 — "battle entered mid-drive -> pausing", fake gamepad removed at the edge); first resume completed end-to-end at 20:45:48 (target ent=-401 gw=1 re-found at catIdx=2, drive re-issued, Arrived at 20:45:57). Second resume was pending when the game was closed.
- **#73/#74 PASS:** ZERO phantom recovers/no-effect lines all session. Deferral worked (verdicts held 6–10 s while animating, then "had effect … no announce"); a3=0x8 now lands in [SPELL-MISS-SKIP]. Cosmetic fix applied post-BAT: that skip line said "bit3 clear" — reworded to "not 0x9" (log-text only, no behavior change; ships with the next build).
- **#71 PASS both phases:** pre-scene bg2f_2 Selphie filtered (inParty=1 — she is already in the formation before the run-in scene on this field, so the roster rule covered the pre-scene case too); post-join filtered on bg2f_2/bg2f_1; Fire Cavern escort Quistis filtered throughout.
- **#76 PASS:** "Exit to Fire Cavern 7" labels observed along the path.
- **#75 DATA:** [TIMERDIAG] capturing. While the timer is LIVE: dword at **0x01CFE950 = 01 00 00 00** (prime dismissed-flag candidate), 0x01CFE928 = up-counting elapsed-seconds dword, 0x01CFE92C = the known remaining-seconds timer, 0x01CFE934 = 7 (constant so far), 0x01CFE95C = 41 00 10 00. NEEDED: the dump ACROSS the Ifrit victory — if 0x01CFE950 flips to 0 when the HUD timer is dismissed, that's the gate; then wire EnterInactive("timer dismissed") on it and set TIMER_DISMISS_DIAG back to 0.

**NEXT BAT: finish the cavern — beat Ifrit, press T a few times after Victory, walk out, then send ff8_mod.log** (the [TIMERDIAG] lines through the victory are the deliverable; Blizzard-on-Ifrit also re-verifies #73 against the original boss case).

---

_(Superseded pre-BAT .236 status below.)_

## ▶ STATUS: v0.18.3.236 LOCAL — SIX PLAYTEST FIXES APPLIED, awaiting BAT (GitHub issues #71–#76)
**Session 2026-07-13 (Cowork).** Aaron played start→Ifrit on .235 and reported six bugs; all were confirmed in Logs/ff8_field.log, ff8_battle.log, ff8_mod.log, filed as GitHub **#71–#76**, and fixed/instrumented in **v0.18.3.236**. Full detail: CHANGELOG `## v0.18.3.236` + the issues.

**⚠ ISSUE-NUMBER CORRECTION:** older notes used "#71/#72" as labels for the world-map walking arc — those numbers were never created on GitHub. GitHub has now assigned **#71–#76 to the 2026-07-12 playtest bugs**; the world-map arc remains **#70**.

What changed (one BAT — a Fire Cavern replay — exercises everything):
- **#72 keyboard-dead-in-battle:** root cause = battles entered mid-F9-drive left the fake gamepad installed all battle (Update() early-returns off-field), so the held steer state masked real arrows. NEW `src/field_nav_battlepause.inl` (included from field_navigation.cpp before Update): stops the drive on the battle edge ("Auto-drive paused for battle."), saves the catalog target (entityIdx+gatewayIdx+fieldId), and ~1 s after the field returns re-selects the target and re-issues the drive through the normal HandleKeys start path (`s_driveResumeRequest`, synthetic backslash). Field-change across battle drops the resume; chase-drive exempt.
- **#73/#74 phantom "Ifrit recovers N HP" / "No effect":** battle_tts_noeffect.inl — watchdog verdict now DEFERRED while the damage anim flag is up (cap `NOEFFECT_WATCHDOG_MAX_MS`=20 s); queued verdicts are DROPPED at flush time if a real HP flush landed after queue (`[NOEFFECT-DROP]`); flush-side dedup blocks identical re-queues within 4 s (`[NOEFFECT-DEDUP]`). battle_tts_sprite.inl — kind=4 resist filter tightened to exact `a3==0x09` (Ifrit BAT proved 0x08 fires on ordinary elemental damage; other bit3 values now [SPELL-MISS-SKIP] + screenshot only).
- **#71 Selphie in catalog:** field_nav_catalog.inl party filter adds the IN-PARTY rule — setpc char in the active formation (savemap +0xAF0, `IsCharacterInActiveParty`) → excluded even when talkable. Verified against the .235 ggroom1 keep (recep log formation=[4,0,5]; kept-Quistis setpc=3 not in formation). REMAINING on #71: pre-scene INVISIBLE actor (bg2f_2 Selphie before the run-in, talk=1 thru=1, not in formation) still lists — needs the entity SHOW/HIDE flag (v05.69 VISDIAG never found it); next step is a per-session byte-flip diagnostic on bg2f_2 across the scene trigger.
- **#75 phantom timer after Ifrit:** engine global 0x01CFE92C keeps decrementing after the game dismisses the HUD timer (523→516 across Victory) — stall-deactivation never fires. `TIMER_DISMISS_DIAG=1` in countdown_timer.cpp dumps 0x01CFE900..0x5F once/sec as `[TIMERDIAG]` while a timer is live (+60 s tail). **BAT deliverable: the [TIMERDIAG] lines through the Ifrit victory** — find the byte that flips at dismissal, gate ACTIVE on it, then set the diag back to 0.
- **#76 Fire Cavern numbering:** field_display_names.h — bdenter1=1, bdin1..5=2..6, bdifrit1=7, bdview1="Fire Cavern Approach" (Aaron's choice).

**BAT .236:** rebuild via deploy.vbs, confirm 0.18.3.236. Load the pre-Fire-Cavern save. (1) F9-drive between rooms until a random battle interrupts — expect "Auto-drive paused for battle.", normal arrow keys IN battle, then "Driving." shortly after the battle ends; if the resume misfires, log reads `[drive] battle resume ...`. (2) Field names along the walk should read Fire Cavern Approach, then 1→7 ending at Ifrit. (3) Blizzard on Ifrit repeatedly: damage lines ONLY — no recovers/no-effect; `[NOEFFECT-DROP]`/`verdict deferred` lines in ff8_battle.log are the guards working. (4) After Victory, T will STILL announce a countdown (expected until the #75 flag lands) — collect the `[TIMERDIAG]` lines. (5) On the 2F walkway after Selphie joins, `-`/`=` through the catalog: she must be absent. Send Logs/ff8_battle.log + ff8_mod.log + ff8_field.log.

**Housekeeping:** DEVNOTES updated to .236 and trimmed (the .212–.225 chapter paragraphs moved to DEVNOTES_HISTORY.md) but still ~1 KB over the 10,240 cap — next session should trim the .211 routenet block once the world-map arc resumes. Note the .229–.235 NPC-catalog session did NOT update DEVNOTES/NEXT_SESSION_PROMPT (both were stale at .225/.217 at session open) — this file's older ▶ STATUS blocks below are from the .217 era.

---

_(Superseded .217 status below.)_

## ▶ STATUS: v0.18.3.217 LOCAL — AUTOMATED BAT LOOP LIVE (Claude drives the game); .211 east reroute VALIDATING; 2 nav bugs found+fixed mid-session
**Session 2026-07-11 evening (Cowork, fully automated BATs — Claude launches/plays/closes the game itself).** Infra shipped: **.212** logs open `_fsopen(_SH_DENYWR)` → live tailing while game runs (fopen_s denied all sharing). **.213–.215 autotest command channel** (`src/autotest_cmd.inl`, included from dinput8.cpp): mod polls `Logs/autotest_cmd.txt` for `KEY <NAME>[+N] [ms]` / `WAIT` / `SHOT` commands, acks as `[AUTOTEST]` in ff8_mod.log; **delivery = ChaseKeyboard DIK OVERLAY** (`SetOverlayKey`, OR-ed into every GetDeviceState read — OS SendInput NEVER reaches FF8's DirectInput buffer for direction keys, letters do; JAWS also eats arrows when resident). Screenshot vision = `SHOT` command / F11 (`Logs/screenshots/`). TTS lines in ff8_mod.log are the "ears".
**Nav bugs found by the loop, both fixed:** **.216** stale watchdog clocks across battle pause-resume (route-progress 4s stall + 40s give-up fired instantly on resume after any long battle → "Cannot reach the destination from here"; fix = `s_driveWatchdogGen` bumped on resume, keyed into both reseeds; VERIFIED live — resume after 107s battle continued cleanly). **.217** `GetWorldMapPosition_Active` foot-motion override (walking out of Dollet committed locomotion=33 VEH_CAR → all position reads returned stale savemap car_pos on the Balamb continent → 55km distances, ROUTENET declines, ocean-routing A* budget burn, bogus learned circle, "Balamb Garden" misidentification; fix = foot DWORDs moving ⇒ player IS the foot character, override stale vehicle byte, `[VEH-POS-OVERRIDE]`).
**Aaron's 8 test cases (Slot2Save1=outside GG, Slot1Save2=outside BG):** #1 GG→Dollet **PASS** (east chain GGEast→Yaulny→Hasberry→Dollet, entered Dollet, .211 topology confirmed live). #2 GG→Timber was ~1.6km out (route + both battle-resumes clean, chain GGEast↔Yaulny↔Timber ✓) when the game **hard-crashed at 22:06:05** (all log channels died same second, mid-battle, just after an overlay Z+C escape started; 2 prior identical escapes fine; treat as singleton unless it repeats). #3–#8 pending. Field-exit navigation works via field catalog (=/- cycle, \ drive, "Exit to World Map"). Escape macro: `KEY Z+C 25000 / WAIT / X ×3`.
**Loop mechanics:** launch exe via Run dialog; boot→worldmap macro = X 200, WAIT 3000, X 200, WAIT 2000, [DOWN for Slot2], WAIT, X, WAIT 4000, X. deploy.vbs between builds (game must be closed). Logs auto-archive on relaunch. Catalog keys =/-/\ are GetAsyncKeyState (OS hold_key works, game window must be frontmost for the tool); game keys go through the autotest channel.

_(Superseded .211 status below — BAT'd this session: east reroute works, Dollet arrival confirmed.)_

## ▶ STATUS: v0.18.3.211 APPLIED locally — PASS RETIRED, TIMBER REROUTED EAST (routenet data-only), awaiting BAT
LOCAL tree = **v0.18.3.211** (GitHub HEAD = v0.18.3.104; ~49 unpushed — Claude NEVER pushes). Authoritative state = DEVNOTES.md "CURRENT" + CHANGELOG.md `## v0.18.3.211` + **`offline/ROUTENET.md`** (rewritten this session — READ before any route work).

**.210 BAT (session 14:27:15):** the .210 transit fix itself WORKED — clean chain `Station→EastPlains→bypass→WestPlains→Timber`, zero field entries — but the **Galbadia pass is IMPASSABLE on foot live**: 26–29 km cycled from Timber riding the validated max-clearance z=−25088 polyline, zero crossings of the neck at x≈−44600. The corrected replica's 6–8u-margin warning was real. This is the scenario the network exists for: fix the MAP, not the executor.

**.211 changes (DATA only — `RouteNetPlan` runtime code untouched):** regenerated `src/world_map_routenet.inl` (now **13 nodes / 14 edges / 1,099 pts**) via updated `offline/gen_routenet.py`: (1) **pass edge REMOVED** (polyline archived under `"GG West Plains|Timber"` in `outputs/ff8/routenet/routenet_state.json`). (2) The fully-validated 50 km Dollet↔Timber corridor **split at two new junctions**: `Yaulny Plains` (−31345,−25449; NE of GG, the only mountain-belt crossing the replica+executor sim accept) and `Hasberry Plains` (−16452,−39492; Dollet mouth, OUTSIDE Dollet's firing bbox → Timber traffic passes Dollet without entering it; transit rule intact), plus a new validated 2 km `GG East Plains↔Yaulny Plains` link. (3) Also retired: `GG East Plains↔Dollet` (.205 gap track — root-caused: steep double-geometry skirt tris roof the massif-tip gap; the MRU cache serves the +200–400u skirt height → |ΔH|≥200 hard blocks; 8 repairs + 18 alternate lines all re-block) and the Timber–Dollet ROAD (checked per plan — its canyon neck ~(−30040,−20000) is a **~64u sliver**, worse than the pass; NOT shipped; the corridor parallels the road only on the Timber approach).

**Offline verification (this session): full matrix 20/20 ARRIVED — first-ever green board** (.209/.210 had 4 fails at the now-removed gap edge). GG→Timber 31.6 km/1,987 sim frames via `GGEast↔Yaulny + Yaulny↔Timber`; Station→Timber 34.3 km; Dollet→Timber 49.9 km via `Hasberry↔Dollet + Yaulny↔Hasberry + Yaulny↔Timber`; GG→Dollet 31.0 km. 6 long hauls use 1 learned-overlay resume (live = one mid-drive replan; normal). Table: `outputs/ff8/routenet/matrix.json` (.210 saved as `matrix_210.json`, state as `routenet_state_210.json`).

**Couldn't compile here — watch the build:** `world_map_routenet.inl` (regenerated data section: NODE_COUNT 13, EDGE_COUNT 14, PT_COUNT 1099; runtime section byte-identical to .210 apart from the #72 comment block), `offline/gen_routenet.py` (nodes/edges/seed_hints; now also carries the .210 bypass seed that previously lived only in the sandbox), version string. No other source touched.

**BAT .211:** REBUILD via deploy.vbs, confirm 0.18.3.211. **Test 1 (the retry): from near G-Garden / the Station, auto-drive to Timber in ONE go.** Log must read `[ROUTENET] plan ok ... via` ending `... GG East Plains<->Yaulny Plains Yaulny Plains<->Timber` — **NO pass, NO `GG West Plains<->Timber`** (that edge no longer exists). Expect the drive to head NE across open plains, north up the inland channel (x≈−32.6k), across the belt at z≈−15.7k→−13.1k, then the road-parallel approach south of Timber and the gate machinery (`[TRIGREADY]`/`[ENTRYMOW]`). This is ~32 km — several minutes of STEADY progress; a mid-drive `[ROUTENET] plan ok` replan is fine, a multi-minute grind at one spot is not. **Test 2: Dollet → Timber directly** — via chain `Hasberry Plains|Dollet + Yaulny Plains|Hasberry Plains + Yaulny Plains|Timber`, and the drive must NOT re-enter Dollet on the way out (Hasberry junction sits outside the trigger). **Test 3 (regression): any Balamb drive.** If Test 1 grinds, pull the `[CAMW]` positions: the belt crossing (x −32.5k→−30.1k, z −15.7k→−13.1k) is the one segment never driven live — send Logs/ff8_world.log and the replica will replay it from `routenet_state.json`. LOCAL; NOT pushed.

---

_(Superseded .210 status below — BAT'd: transit rule + bypass worked, pass proven impassable live → .211 reroute.)_

## ▶ STATUS: v0.18.3.210 APPLIED locally — ROUTENET HARD TRANSIT RULE + GG-plains bypass edge, awaiting BAT (SUPERSEDED — BAT'd)
LOCAL tree = **v0.18.3.210** (GitHub HEAD = v0.18.3.104; ~48 unpushed — Claude NEVER pushes). Authoritative state = DEVNOTES.md "CURRENT" + CHANGELOG.md `## v0.18.3.210` + `offline/ROUTENET.md`.

**.209 BAT (session 10:22:49):** the network itself WORKED (clean `[ROUTENET] plan ok` polylines, executor followed them) — but the route chooser was flawed: Dijkstra chained THROUGH **Galbadia Garden / Galbadia Station as intermediate nodes** (e.g. `via Galbadia Station<->GG East Plains, Galbadia Garden<->GG East Plains, Galbadia Garden<->GG West Plains, GG West Plains<->Timber`). Location nodes are entry AIM POINTS *inside* their decoded firing areas, so each transit drove into the ggview1/station field trigger: OFF-TARGET pauses at 10:23:24 (-35943,-27033), 10:24:03 (-36650,-26222), 10:24:29 (-38375,-24876); Aaron cancelled. The .209 "+8000u through-location penalty" only re-ranked routes — the GG-transit chain was still cheapest because the two Plains junctions had NO direct link (every 4↔5 path went via GG or Station).

**.210 changes (all in `src/world_map_routenet.inl`):** (1) **HARD transit rule** — the node Dijkstra relaxes edges out of a node only if it's a junction, or a seeded e1 endpoint whose walk-back ≤ `RN_START_MOUTH_MAX`=900u (start standing at that location's mouth); the e2 entry node must additionally be a junction, the drive target's own node (`RnIsTargetNode` ≤700u), or an at-mouth seed. Locations = start/destination ONLY, never waypoints; decline → grid fallback. (2) **NEW validated edge 4↔5 `GG East Plains<->GG West Plains`** (95 pts / 8912u; executor-sim arrived BOTH directions round 0, 242/243 frames; 0 firing-bbox clips at 8u segment sampling) — north around GG's area along the Station corridors, cut ≥84u south of the Station bbox. Appended at ptOffset 1660 so all .209 offsets are unchanged; `offline/gen_routenet.py` EDGES/seed_hint updated (a fresh `--emit` reorders it mid-table, same content). (3) **Straight local-hop foreign-area sweep** (`RnSegClipsForeignArea`, 64u sampling): a straight hop that would clip a non-endpoint firing area now falls through to the hop mini-A* (already foreign-area-aware) or declines → grid fallback.

**Offline verification (this session):** full 20-pair matrix re-run under the hard rule (`outputs/ff8/work/routenet_matrix.py`, updated to mirror it): **16/20 arrived — via chains, frames and endpoints byte-identical to the .209 baseline** (the 4 holdouts are the pre-existing Dollet-corridor sim-only capture cluster at ≈(-31.2k,-26.2k), unchanged, ROUTENET.md "Known limitations"). The .209 failure class sims clean end-to-end: EastPlains→bypass→WestPlains→Timber **arrived** (3030 frames, 1 recovery resume — normal), reverse **arrived** (2892 frames, 0 resumes). New-rule matrix = `outputs/ff8/routenet/matrix.json` (old saved as `matrix_209.json`).

**Couldn't compile here — watch the build:** world_map_routenet.inl only (data section: EDGE_COUNT 13, PT_COUNT 1755, appended edge row + 95 pts; runtime: RnIsTargetNode + RN_START_MOUTH_MAX + RnSegClipsForeignArea added, Dijkstra transit gate, entry eligibility, hop guard). ff8_accessibility.h version string. No other source touched.

**BAT .210:** REBUILD via deploy.vbs, confirm 0.18.3.210. **Test 1 (the .209 retry): from near G-Garden/Station, auto-drive to Timber in ONE go.** Log must read `[ROUTENET] plan ok ... via` with ONLY `GG East Plains`/`GG West Plains` mid-chain (a `Galbadia ...<->` leg is legal only as the very first leg when starting at that location's mouth), NO "OFF-TARGET field entry ... PAUSING" lines, then the pass crossing (steady westbound past x=-44600) and Timber's gate machinery. **Test 2: start right at the Station exit → Timber** (at-mouth seeded start). **Test 3 (regression): any Balamb drive.** If a plan logs `declined: no transit-legal network path`, that's the rule working — grid takes it; only worry if it happens from ON-network starts. Send Logs/ff8_world.log. LOCAL; NOT pushed.

---

_(Superseded .209 status below — BAT'd: network/executor good, Dijkstra transited GG/Station nodes and re-fired their fields → .210's hard transit rule + bypass edge.)_

## ▶ STATUS: v0.18.3.209 APPLIED locally — PRECOMPUTED VALIDATED ROUTE NETWORK (the structural pass fix), awaiting BAT (SUPERSEDED — BAT'd)
LOCAL tree = **v0.18.3.209** (GitHub HEAD = v0.18.3.104; ~47 unpushed — Claude NEVER pushes). Authoritative state = DEVNOTES.md "CURRENT" + CHANGELOG.md `## v0.18.3.209` + **`offline/ROUTENET.md`** (READ before any route/planner work).

**.208 BAT:** ground at the Galbadia pass AGAIN — char pinned 22s at x≈-44460 on the z=-25024 route row (88u from the ~1u sliver wall), zero westbound progress, fan/WPSKIP churn; G4 never engaged (it only runs outside recovery and the drive was in recovery the whole time there). Conclusion: **grid quantization is the disease** — the only reliable pass line (z≈-25086±8) lies BETWEEN 128u grid rows, so no grid plan can express it and no executor tweak can hold it.

**.209 changes (one coherent feature):** (1) offline engine replica corrected from the .208 evidence (`nav_sim.py EngineSimC(hatch="away")`: inside the sticky wall-foot zone only wall-AWAY steps ever succeed; the old model's crawl-through blind spot — which let .204–.208 validate offline — is gone; the .208 grind now replicates, and the max-clearance pass line still crosses both ways). (2) NEW **`src/world_map_routenet.inl`**: 11 nodes / 12 edges / 1,660 points — exact max-clearance polylines validated by the corrected replica + full executor sim BOTH directions (`offline/gen_routenet.py` regenerates; results in `outputs/ff8/routenet/`). Pass edge = z=-25088 through the neck; Dollet east corridor = the live .205 track. Two "Plains" junction nodes let Station↔Timber/Dollet bypass GG's firing area. (3) `PlanDrivePath` tries `RouteNetPlan` FIRST: hop ≤2048u onto the nearest edge vertex (straight or mini-A*, learned-block + foreign-area aware), node Dijkstra (+8000 through-location penalty), hop off to the target, splice into s_drivePathWX/WY; cap 4 network plans/drive; on ANY decline (logged `[ROUTENET] declined: ...`) grid A* runs exactly as before. Executor/entry machinery untouched. Offline matrix: 16/20 directed pairs arrive with 0 resumes (incl. all four Timber long-hauls, ~2,900 frames each); the 4 fails are ONE sim-only capture-aliasing cluster on the live-proven Dollet corridor (ROUTENET.md "Known limitations").

**Couldn't compile here — watch the build:** new `world_map_routenet.inl` (data + RouteNetPlan; included from world_map.cpp AFTER planner.inl), planner.inl (forward decl `static bool RouteNetPlan(int32_t,int32_t);` + try-first call in PlanDrivePath + dead `#if 0` block removed), version bump.

**BAT .209:** REBUILD via deploy.vbs, confirm 0.18.3.209. **Test 1 (flagship): near-G-Garden → Timber in ONE drive.** Log reads: `[ROUTENET] plan ok ... via Galbadia Garden<->GG West Plains GG West Plains<->Timber`; the drive goes SW past the Garden, then the pass — expect steady WESTBOUND progress past x=-44600 at z≈-25088 (this exact thing has never happened live), then the long north leg, then `[TRIGREADY]`/`[ENTRYMOW]` at Timber. **Test 2: Dollet → G-Garden (or Station)** — the live-proven east corridor; watch for a possible hiccup near (-31.2k,-26.2k) (recovery should clear it; it crossed at full speed in .205). **Test 3 (regression): any Balamb drive** (all three locations on-network). If the pass STILL grinds: pull the `[CAMW]` positions — if the char holds z≈-25086..-25090 and stalls anyway, the sticky-zone model needs live calibration (send the log; the offline replica + routenet state in outputs/ff8/routenet/ replay it); if the char is on z=-25024 again, check whether `[ROUTENET] plan ok` appeared at all (a decline means the grid planner ran — the log says why it declined). Send Logs/ff8_world.log. LOCAL; NOT pushed.

---

_(Superseded .208 status below — BAT'd: still ground at the pass; led to .209's structural fix.)_

## ▶ STATUS: v0.18.3.208 APPLIED locally — centerline sees the sliver (8u probes) + ABEAM waypoint advance, awaiting BAT (SUPERSEDED — BAT'd)
LOCAL tree = **v0.18.3.208** (GitHub HEAD = v0.18.3.104; ~46 unpushed — Claude NEVER pushes). Authoritative state = DEVNOTES.md "CURRENT" + CHANGELOG.md `## v0.18.3.208`.

**.207 BAT:** the G-Garden decoded-area avoidance WORKED (no field entries, no manual steering; fast plans) and the Timber drive crossed 13km — then ground at the Galbadia pass in the now fully-explained way: character in the north sticky lane at (-44500,-24946), route row z=-25024, proven passable centerline z≈-25086 — **no 128u grid row is safely centered in this ~196u corridor**, the executor must hold the line BETWEEN grid rows. Two blockers, both fixed in .208 (two small `world_map_drive.inl` edits): (1) **G4's side-clearance probes used the 32u cache and couldn't see the ~1u sliver wall** → centerline mode never engaged → probes upgraded to `FootBlocked8` (8u, cached, 20 lookups/frame). (2) **The 64u advance radius can never fire from the centerline** (~62u laterally off the waypoint row) → the character walked PAST waypoints with the cursor pinned (the ten-stall/[WPSKIP]-churn signature) → new ABEAM advance: also advance when ≤144u lateral AND the next waypoint is closer than the current one (height gate kept).

**BAT .208:** REBUILD via deploy.vbs, confirm 0.18.3.208. From near G-Garden, auto-drive to **Timber in one go**. Through the pass: expect the cursor to tick steadily (idx rising in [CAMW], no [WPSKIP] chains, G4 lateral offsets visible as small aim≠tgt differences), then the long north leg, then Timber's gate ([TRIGREADY] entryPoly→1 at entry; [ENTRYMOW] if needed). If the pass STILL grinds: pull [CAMW] positions — if the char holds z≈-25086 but stalls anyway, the corridor needs the sim replay (offline/nav_sim.py --bat203 has the exact pass scenario); if it's still in the north lane (z>-25000), the route row itself is north — check the planned ROUTEDUMP rows through x∈[-44700,-44540]. Send Logs/ff8_world.log. LOCAL; NOT pushed.

---

_(Superseded .207 status below — BAT'd; area avoidance confirmed, pass grind led to .208.)_

## ▶ STATUS: v0.18.3.207 APPLIED locally — decoded-area route avoidance + stall escalation, awaiting BAT (SUPERSEDED — BAT'd)
LOCAL tree = **v0.18.3.207** (GitHub HEAD = v0.18.3.104; ~45 unpushed — Claude NEVER pushes). Authoritative state = DEVNOTES.md "CURRENT" + CHANGELOG.md `## v0.18.3.207`.

**.206 BAT:** two failures. (1) Near-G-Garden→Timber: the drive entered the G-Garden field **4× in 40s** — the region fired 2990u from the learned-circle center (much bigger than the circle), and the circle logic's "start inside = exempt" rule let every RESUMED route re-cross the region (the trigger re-arms once you step off the entry polys; crossing was never safe). Aaron manually steered clear. (2) After repositioning, the Timber drive **ground 40s oscillating ~119u from waypoint 59** — never inside the 64u advance radius, the recovery fan always escaped somewhere (so fan-exhaust retreat/replan never fired), ten route-stalls in a row, ended by the 40s give-up. The .206 entry machinery (aim/mow/TRIGREADY) never got exercised at the Timber gate.

**.207 changes:** (1) **decoded-area route avoidance** — PlanPathGridM now penalizes (+4096/cell) any route cell inside a NON-TARGET decoded firing-area bbox (`s_entryAims`), NO start-inside exemption: plans that begin inside a region exit by the shortest path and stay out; learned circles remain only for undecoded locations. (2) **Stall escalation** in the F2 watchdog: 2 consecutive stalls at the same cursor → `[WPSKIP]` past the unreachable waypoint; 3 → forced retreat+replan (each stall already learned an obstacle via G1, so the replan differs). All .206 machinery unchanged.

**Couldn't compile here — watch the build:** planner.inl (eaAvoid array + decoded-area penalty in the neighbor loop — uses ENTRY_AIM_COUNT/s_entryAims/FindEntryAim from trigger_data.inl, included earlier), drive.inl (stall-escalation statics in the F2 block; sets s_camwRec=3 directly).

**BAT .207:** REBUILD via deploy.vbs, confirm 0.18.3.207. From near G-Garden, auto-drive to **Timber in ONE drive**: the route should skirt the Garden region on its own (no manual steering, no field entries), cross the long leg, and enter Timber via the .206 aim/mow at the gate wedge. Log reads: `[TRIGAVOID]`/plan lines (area avoidance engaged), NO "PAUSING drive" lines for G-Garden, `[WPSKIP]` at most occasionally, `[TRIGREADY]` entryPoly flipping to 1 at the moment of entry, `[ENTRYMOW]` if the trigger needs the sweep. If Timber entry STILL fails with inArea=1 + entryPoly=1 and no fire → story/vehicle gate (windows in offline/TRIGGER_FIRING_AREAS.md); if entryPoly stays 0 inside the area → the painted polys sit elsewhere in the wedge — send the log, the decoded polygon list in outputs/ff8/trigger_areas.json pinpoints them. LOCAL; NOT pushed.

---

_(Superseded .206 status below — BAT'd; see .207.)_

## ▶ STATUS: v0.18.3.206 APPLIED locally — decoded entry-trigger targeting (aim + area-mow), awaiting BAT (SUPERSEDED — BAT'd)
LOCAL tree = **v0.18.3.206** (GitHub HEAD = v0.18.3.104; ~44 unpushed — Claude NEVER pushes). Authoritative state = DEVNOTES.md "CURRENT" + CHANGELOG.md `## v0.18.3.206`.

**.205 BAT = MAJOR WIN:** G-Garden→Dollet drove AND ENTERED (the .201/.203 neck is beaten; hard-budgeted planning worked; no freeze). Dollet→Timber drove fine but the ENTRY never fired: the final-approach orbit swept radii 610–1330u around Timber's real firing patch, which lies entirely within 432u of the seed — the orbit had a hole exactly at the target.

**The trigger system is now FULLY decoded** (`offline/TRIGGER_FIRING_AREAS.md` + dated update in TRIGGER_MODEL.md — READ before any entry work): entry fires only while standing on a wmx poly with **byte14 bit 3** (hand-painted entry polys), in the program's 8192u segment, within sub-segment coordinate bounds (the ex-"unknown" predicate bits 0xFF0F–0xFF12), vehicle + story gates satisfied. Corrections: 0xFF08 = destination ACTION (not a region test); programs pair to destinations by SEGMENT INDEX (prog 17=Timber, 7=Dollet, 9=G-Garden/Station). Model validated: Dollet's .205 entry fired ON its decoded area edge; every failed Timber orbit position is outside its area; all known entrances sit inside theirs. Timber's area: a 314×512u wedge (-22685,-5120)(-22528,-5632)(-22371,-5120); the old seed IS inside it.

**.206 changes:** `s_entryAims[7]` decoded table (world_map_trigger_data.inl: name, aim point, area bbox, footOnly) — StartAutoDrive keeps an in-area proven target or retargets to the aim (`[ENTRYAIM]` logs); final-approach timeout now MOWS the firing area (serpentine waypoints inside the bbox, clipped 768u around the aim, driven by the normal executor; 2 passes opposite directions; `[ENTRYMOW]`) before falling back to the legacy sweep; `[TRIGREADY]` logs every 500ms in the approach zone: the ENGINE's own current-poly entry flag ([[0x20409FC]]+0x0E bit 3, SEH-read) + inside-area bit — one glance separates "right place, wrong poly" from "never reached the area". Foot-only destinations logged (Timber, Fire Cavern, B-Garden, G-Garden, Station).

**Couldn't compile here — watch the build:** world_map_trigger_data.inl (EntryAimInfo/s_entryAims/FindEntryAim at end), world_map_drive.inl (EngineOnEntryPoly near the SEH helpers; StartAutoDrive retarget hook; TRIGREADY + ENTRYMOW in the final-approach block; s_driveEntryAim/s_mowTried statics).

**BAT .206:** REBUILD via deploy.vbs, confirm 0.18.3.206. **Dollet→Timber** (the failure case): expect `[ENTRYAIM]` at start, drive to the gate wedge, and either an immediate entry or "Searching the entrance area" + a short back-and-forth INSIDE the gate mouth → enter Timber. Re-confirm Dollet entry; spot-check Balamb Town or Fire Cavern if convenient (same table). Log reads: `[TRIGREADY]` entryPoly should flip 0→1 at the moment of entry; if a mow pass completes without entry, check whether inArea=1 ever coincided with entryPoly=1 (if yes and no fire → story/vehicle gate; the foot-only flags and story windows are in TRIGGER_FIRING_AREAS.md). Send Logs/ff8_world.log. LOCAL; NOT pushed.

---

_(Superseded .205 status below — BAT'd: Dollet leg SUCCESS, Timber entry failure led to .206.)_

## ▶ STATUS: v0.18.3.205 APPLIED locally — .204 game-freeze fixed (hard-budgeted planning), awaiting BAT (SUPERSEDED — BAT'd)
LOCAL tree = **v0.18.3.205** (GitHub HEAD = v0.18.3.104; ~43 unpushed — Claude NEVER pushes). Authoritative state = DEVNOTES.md "CURRENT" + CHANGELOG.md `## v0.18.3.205`.

**.204 BAT = GAME FREEZE at drive start** (music on, input dead — game thread stuck inside the planner). Log: drive start 20:53:41, [TRIGAVOID] at :41 and a second at 20:54:26 with NO "GRID planner ok" between = the margin-176 plan ran **45s and FAILED**, then the margin-384 retry (4× the area) ran indefinitely. Root causes, both from .204: (1) FootBlockedCached quantization 32u→8u collapsed the cache hit rate ~16× (every probe = fresh mesh query); (2) the 8u fidelity as a HARD dest gate severed routes the .203 planner could find, forcing the giant fallback; (3) the near-wall ring recomputed ×8 per cell.

**.205 fixes (everything else from .204 kept):** A* hot path back on the fast 32u cache; new `FootBlocked8` (8u, sliver-catching) used ONLY by `SweptFootBlocked` (once-per-plan route validation + per-frame executor checks); near-wall ring memoized per cell; **hard 10s wall-clock budget for the whole planning call** (`s_planDeadline`; A* bails on an every-2048-expansions check, the wrapper stops escalating, accepts a validation-dirty plan rather than re-planning, and on total failure falls through to the legacy navmesh/road planners). Worst case from pressing auto-drive = a ≤10s hitch, NEVER a hang.

**Couldn't compile here — watch the build:** planner.inl only (FootBlockedCached reverted to 32u/2^18, new FootBlocked8 2^20, SweptFootBlocked → FootBlocked8, `nwc` per-cell memo, s_planDeadline checks in wrapper + A* loop).

**BAT .205:** REBUILD via deploy.vbs, confirm 0.18.3.205. Auto-drive G-Garden→Dollet: expect a hitch of up to ~10s, then the drive proceeds (the .204 expectations apply: slow nibbling near walls = NORMAL, ≤2 recovery replans per spot, arrival ~2.5–5 min). Then Dollet→Timber, Timber→G-Garden, G-Garden→Station. `[PLAN]` reads: "GRID planner ok" is the good path; "A* wall-clock bail" / "budget exhausted -- falling through" are the new safety valves (fine occasionally; if EVERY plan falls through, the grid planner never succeeds under the new costs — pull the log). `[CAMW-REC]`: overlay count should RISE when blocks happen. Send Logs/ff8_world.log. LOCAL; NOT pushed.

---

_(Superseded .204 status below — froze at drive start; its mechanics carry forward.)_

## ▶ STATUS: v0.18.3.204 APPLIED locally — stateful-gate acceptance: engine-truth learning + disciplined recovery, awaiting BAT (SUPERSEDED — froze; see .205)
LOCAL tree = **v0.18.3.204** (GitHub HEAD = v0.18.3.104; ~42 unpushed — Claude NEVER pushes). Authoritative state = DEVNOTES.md "CURRENT" + CHANGELOG.md `## v0.18.3.204`.

**.203 BAT:** trigger avoidance + pause/resume WORKED (zero field entries). The drive crossed 8km then ground at the .201 neck: engine block → fan escape → "waypoint bearing clear" → re-block within a second, dozens of cycles at bearings our oracle calls clear; almost nothing learned (oracle veto); identical 10-second replans; generic stuck-check replans interleaved. **Offline refit vs ALL evidence (1,217 accepted steps + 53 rejections; `offline/BAT203_ANALYSIS.md` — READ before touching planner/executor): no static collision model can fit — the engine's find-poly 8-entry MRU triangle cache is hit-tested in block-local coords without verifying the block, so recently-touched mountain triangles capture later queries (~half the rejections are inherently unpredictable).** Static facts recovered: a ~1u sliver wall at z=-24936 (invisible to point probes; swept 8u probes catch it), and the neck IS passable at centerline z≈-25086 (~98u clearance/side — the .203 route line ran 36–88u from walls, inside the engine's sticky zone).

**.204 = converge despite the stateful gate (offline: GG→Dollet arrives 0 replans; STILL arrives with the planner left on the wrong model; 20/20 matrix):** swept probes as lazy route validation (soft ×6 failing edges, `[PLAN] swept validation` log) + ×4 near-wall cost + 8u-quantized FootBlockedCached; **G1** learn on EVERY `[CAMW-REC] engine block` (NO oracle veto; 112/176/240 fence-thickening; prune valve `[NAVBLK-PRUNE]` releases cells that would island the goal); **G2** wall-follow hysteresis (8 consecutive swept-clear frames AND ≥64u travel to exit; quick re-block ⇒ same side, commitment ×2 → 512u) + 32-absolute-bearing fan, 2 cycles; **G3** replans need new learned cells (else fence inflation), wide-first margin, stuck-check suppressed during recovery, route-based give-up (128u/40s); **G4** centerline offset when side clearance <288u. Freeze window 40 frames.

**Couldn't compile here — watch the build:** planner.inl (SweptFootBlocked, 8u cache, PlanPathGrid wrapper rework: validation loop + prune valve + s_planWideFirst, IsSweptFail soft cost, ×4 near-wall, AddNavBlock returns bool), drive.inl (CamwLearnBlock, reworked freeze/fan/wall-follow/retreat states, G4 centerline, F2 40s give-up, stuck-check gate on s_camwRec), version/docs.

**BAT .204:** REBUILD via deploy.vbs, confirm 0.18.3.204. **G-Garden→Dollet** first. EXPECTED: one plan, no field entries, visibly SLOW "nibbling" near wall-adjacent stretches (engine sticky zone — normal), ≤2 recovery replans at the neck, arrival ~2.5–5 min. Then Dollet→Timber, Timber→G-Garden, G-Garden→Station. Log reads: `[CAMW-REC]` overlay count RISING on blocks (learning works), wall-follow exits after real travel (never instant), `[NAVBLK-PRUNE]`/`swept validation` informative. If the neck still defeats it: the learned fence should force the next plan onto a wide detour automatically — if instead it loops >3 replans at one spot, pull the [CAMW-REC] positions and the overlay contents from the log; the offline `--bat203` runner in offline/nav_sim.py replays scenarios. Send Logs/ff8_world.log. LOCAL; NOT pushed.

---

_(Superseded .203 status below — its trigger-region layer BAT-confirmed and carried forward.)_

## ▶ STATUS: v0.18.3.203 APPLIED locally — learned field-trigger footprints + pause-and-resume through fields, awaiting BAT (SUPERSEDED — BAT'd; see .204 above)
LOCAL tree = **v0.18.3.203** (GitHub HEAD = v0.18.3.104; ~41 unpushed — Claude NEVER pushes). Authoritative state = DEVNOTES.md "CURRENT" + CHANGELOG.md `## v0.18.3.203`.

**.202 BAT result:** steering/planner/recovery all healthy (trim ±4, clean route follow) — but the Dollet drive was eaten by **field TRIGGER REGIONS**: Galbadia Garden's 'ggview1' trigger fired at (-35939,-27200), **1815u from its entrance coord** (the whole plateau triggers), and the off-target stop killed the drive; the retry hit the Station region. Root insight: triggers are REGIONS, not points, and a trigger you spawn inside is DISARMED until you leave it (why G-Garden *exits* never re-trigger).

**.203 changes:** (1) **learned trigger circles** — off-target field entries during a drive record (location, observed radius+192) into `s_trigAvoid*`; `PlanPathGridM` soft-penalizes (+1536/cell) cells inside non-target circles, exempting the circle the start is inside (disarmed) and the destination's; `[TRIGAVOID]` logs seeding/learning/plan-use. (2) **Seeded** G-Garden r=2048 + Station r=1536 at init from the .202 observations. (3) **Off-target field entry = PAUSE, not stop** — announces "Entered X, not the destination. Return to the world map and I'll continue to Y."; the existing re-entry resume path (encounter machinery) replans + continues; stale pause (>5 min) cancels quietly. Session-scope learning; persistence of circles + executor-recovery awareness of circles = follow-ups if BATs demand.

**Couldn't compile here — watch the build:** state.inl (`s_trigAvoid*`, `s_drivePausedInField`), arrival.inl (learn+pause replaces the OFF-TARGET StopAutoDrive), world_map.cpp (init seeds + stale-pause check in the entry path), planner.inl (avoid-set filter + `trigPen`), drive.inl (pause-flag resets).

**BAT .203:** REBUILD via deploy.vbs, confirm 0.18.3.203. The four-location loop: **G-Garden→Dollet**, **Dollet→Timber**, **Timber→G-Garden**, **G-Garden→Station**. Expect: `[TRIGAVOID]` seed line at init; "plan avoids N circle(s)" on the Dollet plan; if terrain forces a crossing anyway, the pause announcement — walk back to the world map and listen for "Resuming drive to ..."; the resumed plan should exit the region and continue (check goalDist falling in `[CAMW]`). Failure reads: pause→resume loop at the same circle (radius too small — check the new [TRIGAVOID] learned radius), or a plan that can't find any route (soft penalty shouldn't cause this — grep `[PLAN]` fail lines). Send Logs/ff8_world.log. LOCAL; NOT pushed.

---

_(Superseded .202 status below.)_

## ▶ STATUS: v0.18.3.202 APPLIED locally — 112u-lookahead collision model + clearance planner + engine-block recovery, awaiting BAT (SUPERSEDED — BAT'd; see .203 above)
LOCAL tree = **v0.18.3.202** (GitHub HEAD = v0.18.3.104; ~40 unpushed — Claude NEVER pushes). Authoritative state = DEVNOTES.md "CURRENT" + CHANGELOG.md `## v0.18.3.202`.

**.201 BAT result:** camera-write steering CONFIRMED (trim ±9, mh tracks writes; G-Garden + Station arrivals) — but G-Garden→Dollet froze at (-44439,-25238): d+0 with UP held, **no wall slide**, then sterile identical replans. Offline replication (model-fit vs 545 walked steps + the frozen state; freeze reproduced to 8u) found the real engine rule and two compounding bugs — full evidence in **`offline/BAT201_ANALYSIS.md`** (read before touching planner/executor):

1. **The engine validates ~112u AHEAD of each 32u step** (fitted 101–126u): a probe landing on non-foot-walkable terrain (byte15 bit7=0 ≈ terrain 29 mountain / 32–34 ocean) is a HARD block, no slide. The .201 route was walkable at 32u but lacked CLEARANCE (first bad edge: idx 85, diagonal probing into the corridor's north wall).
2. The .186 **goalDist no-progress cursor skip** misread the horseshoe route's legitimate away-from-goal leg (goalDist rises for ~7km) and raced the cursor +4/1.5s → the char beelined 86u off the centerline into the wall's probe cone.

**.202 changes (all offline-proven; 20/24 validation pairs arrive, the 4 misses are all Tears' Point — see below):** clearance-aware planner (112u directional probe + terrain walkability at every 32u sub-point in PlanPathGrid via new `WorldFootBlockedAt`/`FootBlockedCached`; margin ladder 176→384 cells; goal relaxation to nearest reached cell ≤8 cells), goalDist skip DELETED → route-progress watchdog (distToWp + 128·cellsLeft; 4s stall → recovery), waypoint advance radius 192→64, and the **F3 engine-block recovery** in the camera-write executor: freeze detector (<8u over 20 frames) → bearing fan-out ±256/512/768/1024 (escape = real measured motion only) → wall-follow until the waypoint bearing's own 112u probe clears → breadcrumb retreat (stall-guarded; the gate is anisotropic so the own trail may not re-walk) → **learn the obstacle cell into the AddNavBlock overlay** (cap 96→256) → replan (budget 8→24; with the overlay every replan is genuinely different — the sterile loop is structurally dead). Offline, F2+F3 alone beat even the WRONG planner (S4 run) — that's the robustness margin for whatever the model still misses.

**Product finding:** under the fitted rule **Tears' Point is genuinely unreachable on foot** (its walkable pocket is disconnected; the only gap is a 1–2 cell zigzag the 112u probe can't thread — consistent with it being a vehicle destination). Excluded from on-foot validation; worth one live confirmation walk.

**Couldn't compile here — watch the build:** new `WorldFootBlockedAt` (world_map_navmesh.inl, after WorldGroundHeightLocal), `FootBlockedCached` + `PlanPathGridM` margin-ladder split + goal relaxation (world_map_planner.inl), F3 state machine + F2 watchdog + resets (world_map_drive.inl), constants (world_map_state.inl: DRIVE_MAX_REPLANS 24). Kill switches: `DRIVE_CAMWRITE=false` restores the .200 8-way executor entirely.

**BAT .202:** REBUILD via deploy.vbs, confirm 0.18.3.202. Drive the four-location loop: **G-Garden→Dollet** (the freeze), **Dollet→Timber**, **Timber→G-Garden**, **G-Garden→Station**. Reads: `[PLAN]` (the first Dollet plan may hitch a few seconds — bigger bbox + probes; note if it's bad), `[CAMW]` now has `aim`/`rec` (rec=0 normal, 1 fan-out, 2 wall-follow, 3 retreat), `[CAMW-REC]` narrates recoveries incl. learned blocks, `[DRIVE] route-progress stalled` replaces the old skip lines, `[WPADV]` unchanged. Success = all four legs arrive (recoveries are fine; sterile loops are not). If a leg fails: does `rec` cycle 1→3 repeatedly at one spot (fitted rule missed something — pull the [CAMW] positions there) or does the overlay count keep climbing with productive-looking replans that still fail (planner-side)? Send Logs/ff8_world.log. LOCAL; NOT pushed.

---

_(Superseded .201 status below.)_

## ▶ STATUS: v0.18.3.201 APPLIED locally — CAMERA-WRITE steering (exe-verified, sim 24/24), awaiting BAT (SUPERSEDED)
LOCAL tree = **v0.18.3.201** (GitHub HEAD = v0.18.3.104; ~39 unpushed — Claude NEVER pushes). Authoritative state = DEVNOTES.md "CURRENT" + CHANGELOG.md `## v0.18.3.201`.

**What .201 is:** the world-map camera system was fully reverse-engineered from FF8_EN.exe (pre-generated disassembly in `Game Files/disassembly/`) → **`offline/CAMERA_EXE_ANALYSIS.md`** — read it before touching steering. Engine fn `0x557A90` recomputes on-foot heading EVERY tick: `heading = camYaw(0x0203ED02) + key*512 + triBias(0x020409EC)/2`, then movement applies it. So the mod now **controls the camera**: each frame write camYaw = bearing-to-waypoint − bias/2 (+closed-loop trim), zero the camera follow velocity `0x0204DAE8`, hold UP — the engine aims the character itself (≤4 frames to any bearing), all native collision/slide/steps/encounters intact, and the camera faces the direction of travel. Region camera locks (`0x020409E4`→forced yaw `0x0C76D22`; the frozen 3556) are cleared per drive and restored on stop; scripted cams (`0x0203FD5C`) pause writes. Waypoint advance is now height-aware (|charH−wpH|<100) — fixes the "advanced then wedged" ramp jam. The .200 [CAMYAW] "inconsistency" was wall-slide poisoning the motion measurement; the register/formula were right.

**Offline tooling PERSISTED in `offline/`** (survives session loss now): `extract_wmx.py` (re-extract wmx.obj from world.fs), `ff8_walkmesh.py` (validated oracle: 473,193 polys, landmarks ≤0.1u), `nav_sim.py` (camera-faithful sim: engine heading/camera-velocity physics, step gate, wall slide, planner + both executors). Sim results `offline/SIM_CAMERA_RESULTS.md`: **all 24 directed validation routes arrive** (Timber/Dollet/G-Garden/G-Station, Balamb Garden/Fire Cavern/Balamb Town, Lunar Gate/Sorceress Memorial/Tears' Point), including the two live .200 wedges (G-Garden→Dollet, G-Garden→Timber); robust to 180°-wrong start yaw, forced-yaw lock, bias ±64. wmx.obj itself is NOT in the repo (30 MB) — re-run extract_wmx.py in a new sandbox.

**Couldn't compile here — watch the build:** new SEH helpers (ReadMemWord16/32, WriteMemWord16/32) + camw statics + the `else if (DRIVE_CAMWRITE)` branch in `world_map_drive.inl`; new addresses + `DRIVE_CAMWRITE` flag in `world_map_state.inl`. Kill switch: `DRIVE_CAMWRITE=false` restores the .200 8-way executor.

**BAT .201:** REBUILD via deploy.vbs, confirm 0.18.3.201. Drive (a) G-Garden→Dollet, (b) G-Garden→Timber (the .200 wedges), (c) re-confirm Timber→Dollet. Read `Logs/ff8_world.log` `[CAMW]` lines: `mh` (engine heading) should track `wrote` (±bias/2, ≤4 frames), `trim` should settle near a constant (a large stable trim = benign convention offset; a drifting trim = investigate), `lock=0` during the drive. `[WPADV]` lines show height-gated waypoint holds. If a drive still wedges: sim-proven next lever = breadcrumb recovery (retrace own trail after ~20 blocked frames — needed 9× on sim Dollet→Timber; not yet in the mod); also check the byte13-vs-bit7 walkability caveat in SIM_CAMERA_RESULTS.md. Send Logs/ff8_world.log. LOCAL; NOT pushed.

---

_(Superseded .149 status below — pre-dates the .150–.200 chain; see CHANGELOG.md for those builds.)_

## ▶ STATUS: v0.18.3.149 APPLIED locally — TANK-CONTROL steering, awaiting BAT (SUPERSEDED)
LOCAL tree = **v0.18.3.149** (GitHub HEAD = v0.18.3.104; ~38 unpushed — Claude NEVER pushes). Authoritative state = DEVNOTES.md "CURRENT" + CHANGELOG.md `## v0.18.3.149`.

**.148 BAT:** reached Dollet but Dollet->Timber wedged. The true-heading log showed why: at the wedge `yaw=613` vs `tgtBrg=2635` (target ~180deg BEHIND), and the 8-way sector switch mapped that to **DOWN** (`case 4`) -> reversed into a wall (`d+0 JAM`) instead of turning. World-map movement is **tank-style** (LEFT/RIGHT turn the heading, UP=forward, DOWN=reverse), so the 8-way screen-strafe switch was fundamentally wrong on top of the true heading.

**.149: replaced the on-foot 8-way switch with TANK control** (`world_map_drive.inl`): signed `err = targetBearing - heading`; if |err|>~34deg press LEFT/RIGHT to TURN toward the target, else press UP to walk. RIGHT raises heading (CW) -- **swap R<->L if a BAT shows wrong handedness** (one line, noted in code). Intended complete executor = collision-aware/road planner (.147) + true heading (.148) + tank steering (.149).

**Couldn't compile here** -- watch the build.

**BAT .149:** REBUILD via deploy.vbs, confirm 0.18.3.149. Drive Dollet->Timber (the .148 wedge) + the other trio pairs. Watch `[YAWDRIVE]`: should TURN to face the route (keys ---R / -L--) then walk (U---), NOT reverse into walls. If it turns the WRONG way at corners, swap RIGHT<->LEFT. Send Logs/ff8_world.log. LOCAL; NOT pushed.

---

_(Superseded .148 status below.)_

## ▶ STATUS: v0.18.3.148 APPLIED locally — EXECUTOR heading fix (true move heading), awaiting BAT
LOCAL tree = **v0.18.3.148** (GitHub HEAD = v0.18.3.104; ~38 unpushed — Claude NEVER pushes). Authoritative state = DEVNOTES.md "CURRENT" + CHANGELOG.md `## v0.18.3.148`.

**.147 BAT:** the collision-aware road-preferring planner ran fine (`[PLAN] GRID planner ok`) but Timber->G-Garden STILL wedged at the same ~7.6km canyon-rim spot -- confirming the blocker is the EXECUTOR steering, not the route. World-map movement is TANK-style: the character walks along its heading `0x0203FE52` (move builder `0x53DA20` reads it at `0x53eca5`); the camera yaw `0x0203ED02` only lerps toward it (`0x558592`, ~66deg lag after a warp). `GetWorldMapHeading` read the camera yaw, so the steering reference was ~90deg stale at the Galbadia exit -> drift -> wedge.

**.148: `WM_HEADING` 0x0203ED02 -> 0x0203FE52** (`world_map_state.inl`) so steering references the TRUE move heading. With .147's collision-aware + road-preferring planner, this is the intended complete fix for the trio drives. Couldn't compile here -- watch the build.

**BAT .148:** REBUILD via deploy.vbs, confirm 0.18.3.148. Drive Timber->Galbadia Garden (the wedge spot) + the other trio pairs; the character should now steer correctly at the Galbadia exit (no ~90deg drift). Re-confirm Dollet/Timber. If a drive still curves wrong at a turn, the next lever is tank turn-then-go steering (LEFT/RIGHT to face the waypoint, then UP) and/or writing 0x0203FE52 to aim directly. Send Logs/ff8_world.log. LOCAL; NOT pushed.

---

_(Superseded .147 status below.)_

## ▶ STATUS: v0.18.3.147 APPLIED locally — COLLISION-AWARE + ROAD-PREFERRING grid planner, awaiting BAT
LOCAL tree = **v0.18.3.147** (GitHub HEAD = v0.18.3.104; ~38 unpushed — Claude NEVER pushes). Authoritative state = DEVNOTES.md "CURRENT" + CHANGELOG.md `## v0.18.3.147`.

**Collision system located/analyzed/incorporated:** validator `0x53E7A0` steps **32u/frame** on foot and rejects a move iff `|candH-curH| >= 200` between consecutive 32u landings (find-poly `0x53EB80` + interp `0x402620`). **.147 upgraded `PlanPathGrid` (now PRIMARY):** each 128u edge is sub-marched at **32u** and rejected on `|dH|>=200`/no-ground (via `WorldGroundHeightLocal`) -> routes are proactively collision-free at the engine's own granularity; **road cells (`s_roadFine`) cost 1/3** so it follows the gentle reliable corridors. Offline (faithful 32u sim): Esthar trio fully walkable; Galbadia/Balamb routes follow roads, gentle (maxdH ~174-198).

**Couldn't compile here** -- watch the build (new edge-march loop in PlanPathGrid) and plan-time cost (A* + per-cell ground query; C++ should be sub-second; coarsen grid if it hitches).

**NEXT build (deferred to keep this attributable): EXECUTOR heading fix.** Steering still reads the laggy **camera yaw 0x0203ED02**; the true move heading is **0x0203FE52** (confirmed: move builder `0x53DA20` reads it at `0x53eca5`; camera lerps toward it at `0x558592`, ~66deg lag after a warp = the Galbadia drift). Point `GetWorldMapHeading` / steering at `0x0203FE52` (and optionally WRITE it to aim the character; the camera is cosmetic, irrelevant to a blind player). `WM_HEADING` is the `#define` to change (locate it; not in ff8_addresses.h). Verify the heading convention vs `TorusBearing` with a 1-frame diagnostic before trusting a write.

**BAT .147:** REBUILD via deploy.vbs, confirm 0.18.3.147. Drive the trio pairs; watch `[PLAN] GRID planner ok ... -> N fine cells` and whether routes follow roads / stop wedging on cross-country cliffs. Dollet/Timber should still work. Send Logs/ff8_world.log. LOCAL; NOT pushed.

---

_(Superseded .146 status below.)_

## ▶ STATUS: v0.18.3.146 APPLIED locally — grid planner REVERTED; real blocker = CAMERA/STEERING, awaiting BAT
LOCAL tree = **v0.18.3.146** (GitHub HEAD = v0.18.3.104; ~38 unpushed — Claude NEVER pushes). Authoritative state = DEVNOTES.md "CURRENT" + CHANGELOG.md `## v0.18.3.146`.

**.145 BAT:** the grid planner regressed (Dollet wedged 16km out; G-Garden still wedged). Proven offline that the persistent Galbadia-rim wedge is NOT terrain/routing -- the path through the stuck spot is gentle (max 65u per 40u step, well under the 200 gate). It's the rotated-camera STEERING bug from brief 5b: the character presses UP but moves ~90deg off (X-flipped), because the on-foot screen-relative steering assumes UP walks at the yaw bearing (0x0203ED02), which is wrong at camera-rotated spots like the Galbadia exit. The grid planner made it worse by routing cross-country into that area instead of following the road. **.146 disabled the grid planner** (`#if NAVMESH_DIAG && 0` around the PlanPathGrid call; function kept), restoring the .143 navmesh/road planner (known-good Dollet/Timber). **NEXT focus: the world-map camera->movement transform** -- determine how the engine maps world-map camera/yaw to walk direction (the Phase 1 camera item) and correct the screen-relative steering at rotated-camera locations. **BAT .146:** confirm 0.18.3.146; verify Dollet & Timber drives still work; G-Garden is expected to still wedge until the camera fix.

---

_(Superseded .145 status below.)_

## ▶ STATUS: v0.18.3.145 APPLIED locally — FAITHFUL GRID PLANNER (G-Garden routing), awaiting BAT
LOCAL tree = **v0.18.3.145** (GitHub HEAD = v0.18.3.104; ~38 unpushed — Claude NEVER pushes). Authoritative state = DEVNOTES.md "CURRENT" + CHANGELOG.md `## v0.18.3.145`.

**.144 BAT:** Timber->G-Garden STILL stalled — the navmesh A* kept routing into the -1305 deep pocket (waypoint -32256,-27136) even with `road-cell exemption kept 0`, so that pocket is bridged in by OTHER bridges and removing the exemption also hurt seam connectivity. **.145:** reverted the .144 exemption removal and replaced the path planner with a **faithful GRID PLANNER** (`PlanPathGrid`, `world_map_planner.inl`, now primary): 128u 8-neighbour A* on the real walkable surface using `WorldGroundHeightLocal` + the 200 step gate (the SAME rule the executor's STEPGUARD uses), emitting fine cells into s_drivePath; falls through to the old planners on failure. The navmesh stays as the height oracle only. Offline this routes all three trios correctly and routes the stuck point->G-Garden in 320 clean waypoints, avoiding the pocket. **Heads-up:** could not compile here — watch the build for errors in the new function, and watch plan-time cost (A* calls `WorldGroundHeightLocal` per cell, cached; coarsen the grid if drive-start/replan hitches). Timber ENTRY still open (marker >1500u from trigger; .144's widened sweep is a stopgap — durable fix = trigger-segment targeting, offline/TRIGGER_MODEL.md).

**BAT .145:** REBUILD via deploy.vbs, confirm 0.18.3.145. Drive Timber->Galbadia Garden — watch for `[PLAN] GRID planner ok: ... -> N fine cells` and confirm it routes around the canyon (no ~7km stall, no steering to -32256,-27136). Re-confirm Dollet and Timber drives still work. Send Logs/ff8_world.log. LOCAL; NOT pushed.

---

_(Superseded .144 status below.)_

## ▶ STATUS: v0.18.3.144 APPLIED locally — G-GARDEN ROUTING + TIMBER ENTRY fixes, awaiting BAT
LOCAL tree = **v0.18.3.144** (GitHub HEAD = v0.18.3.104; ~38 unpushed — Claude NEVER pushes). Authoritative state = DEVNOTES.md "CURRENT" + CHANGELOG.md `## v0.18.3.144`.

**.143 BAT result:** step-guard worked (`[STEPGUARD]` 287x, `[GROUNDH]` mean 1.5); Squall ENTERED Dollet and reached Timber. Two issues remained, both isolated offline: (1) **Timber→Galbadia-Garden stalled ~7km out** — the navmesh A* routed into a -1305 deep pocket (route waypoint 0 = (-32256,-27136), behind a 239u cliff, in a disconnected component), kept connected only by `NmEngineStepBlocked`'s road-cell exemption; STEPGUARD correctly refused the cliff so the drive wedged. **.144 removed the road-cell exemption** so the gate is the engine's uniform 200 everywhere (offline the Galbadia trio still connects; a faithful Timber→G-Garden route is 343 waypoints, all walkable). `ROADFLOOR` left alone (cost-only). (2) **Timber wouldn't enter** — the character reached the marker exactly but no field loaded; its marker is >1500u from the real entry trigger, so the sweep drifted out and bounced. **.144 widened the entrance search 1500→3000** (`DRIVE_FINAL_APPROACH_DIST*1.5 -> *3.0`); STEPGUARD keeps the wider spiral on land. Best-effort — the durable fix is capturing Timber's real entry coord (one-time on-foot entry + `[MAPJUMP-HOOK]`, like the Balamb Town / Fire Cavern refined defaults). Added `validate_route()` to offline/ (samples a route at 40u and flags >=200 cliffs/ocean).

**BAT .144:** REBUILD via deploy.vbs, confirm Version 0.18.3.144. From the Dollet save: (a) drive to Timber — does the wider sweep enter? If it bounces again, note the `[DRIVE-SWEEP]` drift distance/direction so we can seed Timber's real coord. (b) drive Timber→Galbadia Garden — it should route around the canyon instead of stalling ~7km out. Send Logs/ff8_world.log. Two edits this build (gate exemption + sweep radius) but logs separate them. LOCAL; NOT pushed.

---

_(Superseded .143 status below.)_

## ▶ STATUS: v0.18.3.143 APPLIED locally — DOLLET→TIMBER STEP-GUARD FIX, awaiting BAT
LOCAL tree = **v0.18.3.143** (GitHub HEAD = v0.18.3.104; ~38 unpushed — Claude NEVER pushes). Authoritative state = DEVNOTES.md "CURRENT" + CHANGELOG.md `## v0.18.3.143`.

**.142 BAT result:** the Y-flip height fix WORKED — `[GROUNDH]` diff collapsed to ~1 (mean 1.1 over 538 samples) and Squall reached Dollet. But Dollet→Timber wedged at the Dollet exit: the .138 DEEPGUARD now mis-fires on the corrected deep ground. It compares fine cells 1024u apart against `WM_CLIMB_STEP`=400, so the gradual road descending from the shallow coast (~-193) to the deep inland ground (~-595) reads as an unclimbable ledge — offline it false-blocked 80/304 steps of the (walkable, per-step dH<=83) route and redirected away from the only westward route, 1127× at c112,r57 → hard wedge (moved 0 units).

**.143 FIX (`world_map_drive.inl`, one change):** replaced DEEPGUARD with a faithful forward step-guard — probe ~128u ahead along each candidate 8-way sector via the now-validated `WorldGroundHeightLocal`, and block a sector only if the destination is no-ground/ocean OR |dH| >= 200 (the engine's own 0x53E7A0 gate). Offline: 0/304 false blocks on the walkable Dollet→Timber route, still rejects the sea. The on-foot executor previously had NO forward terrain check besides DEEPGUARD, so this also gives it real faithful wall/coast avoidance. **BAT .143:** from the Dollet save, auto-walk to Timber; watch `[STEPGUARD]` (should be rare, only at real cliffs/coast). If he reaches Timber, the Galbadia trio is closed in-game; then retire the DOLLET coast block / `[ROADFLOOR]` / road-bridge hacks one BAT at a time.

---

**Prior (.142) detail:**

The offline Cowork rebuild root-caused #70: the navmesh was **mirrored along Y inside every block**. `LoadTerrainGrid` placed vertices `vwy = oy + lvy`, but the wmx in-plane Y (word2 @vert+4) runs 0..-2048 while the height query (`NmGameToMeshY`) runs the OPPOSITE way — so every triangle sat ~one 2048u block off in Y and `WorldGroundHeightLocal`'s home block held the ADJACENT shallow coast (~-200) instead of the deep ground (~-595); the 200-step gate then severed the link (the Galbadia/Timber wedge). This is exactly the -201-vs-545 the .140/.141 BATs saw; the block-local query alone couldn't fix it because the home block held the misplaced geometry.

**FIX (.142, ONE change):** `vwy = oy - lvy` in `LoadTerrainGrid` (`world_map_segments.inl`). Replicating the mod's exact parser reproduces the same 157,416 triangles; with the flip the ported height matches the engine across ALL 205 `[GROUNDH]` samples (mean 0.6u, max ~5u, vs ~390u before), and offline the three trios (Galbadia/Balamb/Esthar) walk both ways. The .138 deep-trap guard, block-local query, and overlays are unchanged. Full disassembly + faithful port + analysis in `offline/` (REQUIREMENTS.md, VALIDATION.md, PATCH_phase5.md).

**BAT .142:** REBUILD via deploy.vbs, confirm Version 0.18.3.142. Load the Balamb save, walk from outside the Garden DOWN to the beach — the `[GROUNDH]` diff should now be ~0 (was ~344 stationary at the Garden). Then auto-walk to Dollet, and onward to Timber. Send `Logs/ff8_world.log`. If good, retire the hardcoded DOLLET coast block / `[ROADFLOOR]` clamp / road bridges one BAT at a time. LOCAL; NOT pushed. `NAVMESH_ROUTING 0` still reverts to the .99 fine-grid drive.

---

_(Older .140 pivot status below is SUPERSEDED — its open question "is our height query faithful?" is answered: the navmesh was Y-mirrored; fixed in .142.)_

## ▶ STATUS UPDATE: v0.18.3.140 BAT'd on BOTH continents -- the Stage 1 height query is NOT faithful -> PIVOT, pending Aaron's pick (A vs B)
The `[GROUNDH]` validation is DONE. The engine's own ground height (`0x0203FE30`) is GROUND TRUTH (on Balamb it climbs smoothly from ~-643 outside the Garden to ~0 at the beach), but OUR `WorldGroundHeight` query is NOT faithful across the map:
- **Galbadia start** looked OK (settled diff ~9) -- but that was a coincidentally-good flat patch.
- **Balamb, stationary by the Garden:** diff **344** at a dead stop (ourH -201 vs engine -545). Our tri 52942 (corners -172/-260/-239) is a spurious SHALLOW triangle amid good deep ones (neighbours -514..-560 matched the engine); point-location lands on it while the engine walks the real -545 ground. A large SETTLED diff rules out the .139 lag idea.
- **Balamb beach:** point-location returns NON-containing triangles (barycentric weights to -15.8/16.5 -- the point is nowhere near the returned tri), so `WorldGroundHeight` EXTRAPOLATES garbage (ourH -472 where the engine reads ~0). The coast isn't in our navmesh at all.

So our reconstructed navmesh has spurious shallow/overlapping triangles AND coastal coverage HOLES -- NOT a faithful height source, and Stage 2 cannot gate on it (it would misjudge every coast). The replicate-from-our-mesh path is the detour; the engine's own decoded height fn is ground truth.

**DECISION PENDING (Aaron picks; do NOT implement until he confirms):**
- **(A, RECOMMENDED) Call the engine's own ground-height fn `0x53EB80` -> `0x402620`** at the candidate (x,z) for the Stage 2 gate -- ground truth, no coverage holes, and literally the function the engine's own 200-step gate consults. FIRST build a tiny diagnostic that calls it at the PLAYER'S current position and compares to `0x0203FE30` (should match to ~1u): that confirms the calling convention + args AND that it has no side effects, before any gate uses it. Re-read 0x53EB80/0x402620 in the disassembly for the signature.
- **(B) Keep replicating + fix the navmesh** (add coastal coverage + strip the spurious shallow/overlapping triangles). Deeper and uncertain -- mesh fidelity is the exact thing the #70 saga has fought for ~40 builds.

**Cheap regardless of A/B:** guard `WorldGroundHeight` (`world_map_navmesh.inl`) to return `WGH_NO_GROUND` when the barycentric weights fall outside [0,1] (the point isn't actually inside the located triangle) -- stops the garbage extrapolation seen at the beach.

The .138 deep-trap guard STAYS. NAVMESH_ROUTING 0 reverts to the .99 fine-grid drive.

---
**(The block below is the pre-BAT .140 plan -- kept for the query/diagnostic detail; its "what .140 decides" branches are superseded by the PIVOT above.)**

## ▶ STATUS at open: v0.18.3.140 APPLIED locally (#70 collision-rule replication, Stage 1 — the height query is VALIDATED to WORK; .140 enriches the [GROUNDH] diagnostic to characterize a ~92u offset across terrain + both continents), AWAITING BAT
LOCAL tree = **v0.18.3.140** (GitHub HEAD = v0.18.3.95; ~45 builds unpushed — Claude NEVER pushes). **Authoritative state = DEVNOTES.md "CURRENT" + CHANGELOG.md `## v0.18.3.140`.**

#70 on-foot Dollet drive. The executor has NO collision check (the forward-guard is vehicle-only), so the 8-way walk noses into walls (the recurring Galbadia-exit wedge). We're REPLICATING the engine's on-foot rule (WMX_OBJ_FORMAT.md §12): for each candidate move, barycentric-interp the exact ground height under the destination, REJECT if |dH| from current >= 200 (0xC8); foot is exempt from the per-vehicle bits, so the 200-step is the ONLY thing stopping Squall. **3 stages: (1) the validated height query [.139/.140]; (2) gate the 8-way steering on the 200-step = universal wall avoidance; (3) replace the planner fine-grid walkability, retire .81 + WM_CLIMB_STEP, close #69.**

**.139 BAT = the query WORKS:** `WorldGroundHeight(gx,gy)` (`world_map_navmesh.inl`; = `Navmesh_FindTriangleGame` + barycentric interp from persistent corner heights `s_nmH0/1/2`) returned a valid triangle on every sample, and ourH (~-438) tracked the engine's LIVE ground height at `0x0203FE30` (~-530, the value its own 200-gate compares) within +/-5 — confirming the coordinate frame, file<->engine Z mirror, vertex layout, sign (UP=neg), and interpolation. The old coarse grid reads -165 here (off by 365), so this is a big accuracy gain. **OPEN: a ~92u offset (ours consistently LESS negative).** For the Stage 2 step gate (a DIFFERENCE, |ahead - here| >= 200) a CONSTANT offset cancels exactly; the risk is a TERRAIN-VARYING offset = we're point-locating a different surface triangle than the engine (our height changed ~2x faster than the engine's over the sampled positions — a hint we may be on a steeper tri). Couldn't tell: the drive WEDGED again at the Galbadia exit (c103,r66) so all samples are one small flat patch.

**.140 = READ-ONLY diagnostic enrichment (one BAT, no steering change):**
- `WorldGroundHeight` gains optional debug out-params (triangle index, 3 corner heights, barycentric weights); Stage 2 still calls it bare (no overhead).
- The `[GROUNDH]` probe MOVED from `world_map_drive.inl` (auto-drive only) to `Poll()` in `world_map.cpp` so it fires on EVERY world-map frame — logging while the player walks MANUALLY, not just during a wedged auto-drive. Now logs `tri + corners(h0,h1,h2) + bary + ourH/engineH/diff`. Gated on `GROUNDH_VALIDATE` (flip 0 to silence). `FF8OPC_VERSION` bumped + CHANGELOG `## v0.18.3.140`.

**When Aaron says "BAT":** read `Logs/build_latest.log` tail (confirm Version 0.18.3.140) then `Logs/ff8_world.log` (large + accumulates -> full read "too large" + stores to /mnt/user-data/tool_results/*.json; bash-grep that JSON, splitting on literal `\r\n`; the .140 run is at the tail). Aaron WALKS MANUALLY (no auto-drive needed — the probe fires every frame) around the Galbadia start over varied ground (flat, near the coast, slopes), THEN loads a BALAMB save and walks there too. KEY reads on the `[GROUNDH]` lines: **(a) does engineH fall WITHIN the triangle's `corners(h0,h1,h2)` range [min..max]?** Inside = we're on the right surface, the offset is a benign sampling/smoothing artifact that cancels in the gate. Outside (e.g. corners ~-440 but engineH -530) = we're point-locating the WRONG triangle — fix triangle selection before Stage 2. **(b) Is the `diff` ~constant across terrain AND across the two continents?** Constant -> cancels in the Stage 2 gate -> proceed. Varying -> triangle-selection problem.

**What .140 decides -> next (Aaron approves options first):**
- **engineH within corners + diff ~constant everywhere** -> Stage 1 VALIDATED -> **Stage 2**: wire the 200-step gate into the 8-way steering (reject any candidate sector whose ~190u-ahead step is >= 200; needs a spatial triangle index for the 8 queries/frame). Should clear the Galbadia wedge + let the forward trip reach Dollet (then the .138 deep-trap guard finally gets tested on the return).
- **engineH OUTSIDE corners (wrong triangle)** -> fix `Navmesh_FindTriangleGame` selection (overlapping/seam triangles — pick the top surface, or the navmesh is missing the engine's surface tri so feed an unfiltered height-mesh). Re-validate before Stage 2.
- **diff VARIES by region/terrain but engineH within corners** -> the engine smooths `0x0203FE30`, or our triangle differs per area; characterize the variance, then decide whether the step gate stays faithful (re-disasm 0x53EB80/0x402620 only if needed).

The .138 deep-trap guard STAYS (the one-way DESCENT the symmetric rule allows). NAVMESH_ROUTING 0 reverts to the .99 fine-grid drive. Do NOT re-apply old gates.

---
**⚠ Everything below (the .138 / .136 / ... chain) is SUPERSEDED — the .139 Stage-1 query + the .140 diagnostic enrichment are in the block above.**

## ▶ STATUS at open: v0.18.3.138 APPLIED locally (#70 forward Dollet drive WORKS; .138 reverts corner-safety + adds a DEEP-TRAP STEERING GUARD for the return drift), AWAITING ROUND-TRIP BAT
LOCAL tree = **v0.18.3.138** (GitHub HEAD = v0.18.3.95; ~43 builds unpushed — Claude NEVER pushes). **Authoritative state = DEVNOTES.md "CURRENT" + CHANGELOG.md `## v0.18.3.138`.**

#70 on-foot Dollet drive. The Dollet "bridge" is a shallow ROAD CAUSEWAY (railroad terr 28, fine row 55, c107->c112) over deep coast; the gap into town was the c112 column (c112,r56/r57, tagged road in .135). Forward Timber->Dollet now drives all the way into town.

**The story since .135:** .135 connected the causeway (forward SUCCESS). .136 added DIAGONAL CORNER-SAFETY (insert a road corner on a diagonal leg that grazes a deep cell) to fix the return drift -- but its trigger ("avoided corner non-road") was too loose, fired 7x on the benign inland road, reshaped the working route and REGRESSED the forward drive (wedged at the start). .137 GATED the insert on a real deep drop (> 2x climb limit); **the gate FIXED the forward trip** (corner-safety fired only the 4 deep corners; forward reaches Dollet again). **But the .137 RETURN still drifts, and the log proved it is the EXECUTOR, not the path:** the plan is correct, `[CORNERSAFE]` inserted the c112,r55 corner, and the steer target IS c112,r55 -- yet leaving Dollet the 8-way yaw walk slips c112,r57 -> c112,r56 -> WEST into deep-coast c111,r56 (floorZ -1374) while the yaw is still rotating, and traps there (can't climb the +874 back out). The corner is in the path and targeted; the executor drifts off it on the very first move. So path-reshaping CANNOT fix the return.

**.138 = two changes (the guard is the one new behaviour; the revert restores the .135 baseline the forward already worked from):**
- **(1) REVERTED the diagonal corner-safety** in `PlanDrivePathNavmesh` (`world_map_planner.inl`) -- it can't fix the return and the forward worked without it in .135, so it was adding risk for no benefit. Back to building the raw centroid polyline + rasterizing it.
- **(2) DEEP-TRAP STEERING GUARD** in `world_map_drive.inl` (the on-foot YAW 8-way steering). Walk-bearing for sector k = (heading + k*512). Before committing the chosen sector, find the player's ORTHOGONAL world-neighbours whose `s_elevFine` floor is > WM_CLIMB_STEP BELOW the player's cell (a one-way ledge he can't climb back from); if the chosen sector's walk bearing points within 90deg of such a neighbour, rotate the sector to the nearest one that doesn't. At the Dollet exit this turns the drifting NNW walk (toward the deep WEST cell c111,r56) into NNE, keeping the player on the c112 road column up to the causeway. No-op with no deep neighbour, so open-road / inland drives are unchanged. Logs `[DEEPGUARD]` (player elevation + which dirs were blocked) on a redirect. `FF8OPC_VERSION` bumped + CHANGELOG `## v0.18.3.138`.

**When Aaron says "BAT":** read `Logs/build_latest.log` tail (confirm Version 0.18.3.138) then `Logs/ff8_world.log` (large + accumulates across sessions -> full read "too large" + stores to /mnt/user-data/tool_results/*.json; bash-grep that JSON, splitting on the literal `\r\n`; the .138 run is at the tail). Aaron drives the ROUND TRIP: Galbadia save -> on foot to Dollet (should still arrive), enter, exit, back toward Timber. KEY reads: FORWARD still reaches Dollet (the guard is a no-op or keeps it off the coast -- either is fine); on the RETURN, `[DEEPGUARD]` should fire at/just after the Dollet exit and redirect the sector (e.g. 3->4); the player's `[YAWDRIVE]` position should walk UP the c112 column to the causeway WITHOUT entering c111,r56 / dropping to floorZ ~-1374; then head west toward Timber. Random battles interrupt + resume.

**What .138 decides -> next (Aaron approves options first):**
- **Round trip drives clean BOTH ways** -> #70 DONE; close it ONLY after BAT-confirmed both-way arrival + pushed, then retire the .81/.85/.127 Dollet hardcodes + the radius bridge + the [DOLLETBRIDGE] diag.
- **`[DEEPGUARD]` does NOT fire** -> read its `pElev`: the player's own cell (the tagged gap c112,r56) may read too DEEP for the relative drop check, so the drop to c111,r56 is < WM_CLIMB_STEP. Fix: compare the neighbour against a shallower reference (the path's current road cell, or an absolute deep-coast floor threshold) instead of the player's own cell.
- **`[DEEPGUARD]` fires but the player still drifts in** -> the guard redirected too late (already slipping) or the redirect sector still has a westward slip -> widen the guard (check 1-2 cells ahead, or also block diagonal-neighbour ledges), or bias the redirect further east.
- **Forward trip regresses** -> the guard is firing on the forward approach and steering it wrong -> tighten the trigger (it should be a no-op unless a true deep ledge is orthogonally adjacent).

NAVMESH_ROUTING 0 reverts to the .99 fine-grid drive. Do NOT re-apply old gates.

---
**⚠ Everything below (the .136 / .134 / .133 / ... chain) is SUPERSEDED — the .137 forward-fix + the .138 revert + deep-trap guard are in the block above.**

## ▶ STATUS (history): v0.18.3.136 — BAT'd (corner-safety over-fired, regressed forward; .137 gated it -> forward fixed, return still drifts; superseded by .138); see block above
LOCAL tree = **v0.18.3.136** (GitHub HEAD = v0.18.3.95; ~41 builds unpushed — Claude NEVER pushes). **Authoritative state = DEVNOTES.md "CURRENT" + CHANGELOG.md `## v0.18.3.136`.**

#70 on-foot Dollet drive. **.135 BAT = SUCCESS forward:** the corrected gap tags (c112,r56 + c112,r57, replacing the red-herring c113,r56) connected the row-55 ROAD CAUSEWAY into Dollet town; Timber->Dollet drove 25/25 waypoints on road, all shallow, into town. The Dollet "bridge" was never a terrain type -- it's that shallow railroad causeway (terr 28, fine row 55, c107->c112, floorZ ~-500) over the deep coast; the real gap was the 2-cell c112 column into town, which exceeded the 1536u radius bridge.

**But the return (Dollet->Timber) wedged ~immediately.** The plan is correct (46/50 on road, routes c112,r57->c112,r56->c111,r55 onto the causeway->west to Timber). The EXECUTOR drifts: the first leg out of Dollet is the DIAGONAL c112,r56->c111,r55 along the causeway edge, and the cell directly south of the target (c111,r56) is deep coast (floorZ -1374). The 8-way yaw steering didn't turn north hard enough across the diagonal, so the player slid one row south into c111,r56 -- a one-way ledge (descends in, can't climb the +874 back up to the road), JAM; the reverse-recovery just oscillated (back east toward Dollet, forward into the same deep cell, repeat). The forward trip dodges this by dropping straight DOWN the c112 column into Dollet (c112,r56->c112,r57, orthogonal).

**.136 fix (applied; do NOT re-apply): DIAGONAL CORNER SAFETY** in `PlanDrivePathNavmesh` (`world_map_planner.inl`). After the navmesh A* centroid polyline is built, walk it; where a leg steps diagonally between two cells (|dc|==1 and |dr|==1) and exactly ONE of the two orthogonal corner cells is road (the other a non-road deep cell), insert the ROAD corner as an intermediate waypoint -> the diagonal becomes an L through the road (c112,r56->c112,r55->c111,r55), both legs orthogonal + on-road so the steering can't graze the deep corner. No-op when both corners are road (already safe) or both non-road (a real bridge across non-road terrain) -- so it does nothing to the forward trip (orthogonal Dollet legs) or road-less continents. Logs `[CORNERSAFE]` per insertion. Keeps everything from .135 (the c112,r56/r57 gap tags, the .134 terr-12 overlay, the radius bridge, the [DOLLETBRIDGE] diag). The road overlay is consulted only to pick the safe corner of an already-routed path -- it never steers (Aaron's principle). `FF8OPC_VERSION` bumped + CHANGELOG `## v0.18.3.136`.

**When Aaron says "BAT":** read `Logs/build_latest.log` tail (confirm Version 0.18.3.136) then `Logs/ff8_world.log` (it's large + accumulates across sessions -> full read returns "too large" + stores to /mnt/user-data/tool_results/*.json; bash-grep that JSON, splitting on the literal `\r\n`). Aaron drives the ROUND TRIP: Galbadia save -> on foot to Dollet (should still arrive), enter Dollet, exit, then on foot back toward Timber. KEY reads: forward still reaches Dollet (`[ROADV:DRIVE]` mostly ROAD + arrival); on the RETURN, `[CORNERSAFE]` should log a road-corner insertion at the Dollet exit (diag c112,r56->c111,r55 -> insert c112,r55); the return `[YAWDRIVE]` should follow the causeway WEST without dropping to floorZ ~-1374 or wedging in c111,r56; and the drive should make real westward progress toward Timber instead of jamming ~immediately out of Dollet. Random battles interrupt + resume.

**What .136 decides -> next (Aaron approves options first):**
- **Round trip drives clean BOTH ways** -> #70 is DONE; close it ONLY after BAT-confirmed both-way arrival + pushed, then retire the .81/.85/.127 Dollet hardcodes + the radius bridge + the read-only [DOLLETBRIDGE] diagnostic.
- **Return still drifts into c111,r56** -> `[CORNERSAFE]` didn't fire for that leg (not detected as a 1-cell diagonal, or the corner wasn't road) -> read the `[CORNERSAFE]` + `[ROADV:DRIVE]` lines and extend the rule (e.g. handle 2-cell diagonal skips, or tag the drift corner).
- **Drive wedges somewhere NEW on the return** -> a different causeway-edge or executor issue -> diagnose from the new `[YAWDRIVE]` jam position.

NAVMESH_ROUTING 0 reverts to the .99 fine-grid drive. Do NOT re-apply old gates.

---
**⚠ Everything below (the .134 / .133 / .132 / ... chain) is SUPERSEDED — the .135 forward SUCCESS + the .136 corner-safety fix are in the block above.**

## ▶ STATUS (history): v0.18.3.134 — BAT'd (terr 12 exists but NOT near Dollet; the [DOLLETBRIDGE] corridor dump found the real road causeway + the c112 gap); superseded by .135/.136; see block above
LOCAL tree = **v0.18.3.134** (GitHub HEAD = v0.18.3.95; ~39 builds unpushed — Claude NEVER pushes). **Authoritative state = DEVNOTES.md "CURRENT" + CHANGELOG.md `## v0.18.3.134`.**

#70 on-foot Dollet drive. **ROOT CAUSE FOUND (Aaron's bridge insight, confirmed in code):** Dollet sits on the coast and you reach its town trigger across a short raised BRIDGE off the mainland road. The road overlay `s_roadFine` (built in `LoadTerrainGrid`, `world_map_segments.inl`) was rasterized from road/railroad polys ONLY (`terrain == 27 || terrain == 28`). FF8 has a distinct terrain type **12 for BRIDGES** (the `world_map_geometry.inl` comment names it: "type 28 road, type 12 bridge"). A bridge poly rasterizes into the fine grid as plain walkable land and is fed to the navmesh, but it was NEVER tagged into the road overlay — so all four Dollet-connection mechanisms (the navmesh road-bridge step 3.6, the gate's road-road exemption .128, the .85 walkable override, the .127 floor clamp) SKIPPED the bridge deck. It sat in the navmesh sharing edges only with the deep water beside it; the height step where it meets the mainland road was severed by the gate with no exemption → A* refused the severed bridge and dived the coast (the #70 ~4km wedge). The hand-tagged gap cell c113,r56 we kept fighting was the WRONG object (terrain 29, mountain, sitting BESIDE the bridge).

**.134 fix (applied; do NOT re-apply):** include terrain 12 in the road-overlay rasterize condition in `LoadTerrainGrid` (one line: `terrain == 27 || terrain == 28 || terrain == 12`). The bridge deck is now tagged road, so the road-bridge stitches it to the adjacent road triangles, the gate exemption KEEPS the tall bridge↔road and bridge↔Dollet steps, the .85 override forces the bridge cells walkable, and the .127 clamp shallows them — an all-road path across the bridge into Dollet that A* prefers over the coast dive. Low-risk + general (no-op where there's no bridge terrain; other coastal-town bridges get the same treatment — helps #67). Keeps .133's dense radius bridge + the .130 gap tag. `FF8OPC_VERSION` bumped + CHANGELOG `## v0.18.3.134`.

**.134 diagnostic (applied; read-only `[DOLLETBRIDGE]`, in `LoadTerrainGrid` after the navmesh build):** (1) the global terrain-type histogram (is terr 12 present? poly count?), (2) every terr-12 bridge triangle map-wide with its fine cell + floor + road-status, (3) every navmesh triangle in the Dollet road-end → town corridor (fine rows 54..61, cols 107..114) with terrain type + floor + road-status. After the fix the bridge cells should read road=1. Pure logging.

**When Aaron says "BAT":** read `Logs/build_latest.log` tail (confirm Version 0.18.3.134) then `Logs/ff8_world.log`. The `[DOLLETBRIDGE]` map is logged at world-map LOAD, so Aaron ENTERs the world map; he also drives on foot from a Galbadia save toward Dollet. KEY reads: `[DOLLETBRIDGE]` histogram (terr 12 present? how many polys?), the terr-12 bridge locator (are there bridge triangles next to Dollet, ~c109-113 r56-57?), the corridor dump (what terrain types tile the road-end → Dollet gap, and do they now read road=1?); `[ROADCON] SEVERANCE SUMMARY` (now 0 of ~14?); `[ROADV:DRIVE]` (route stop diving to -1414, stay on road?); and the live drive (`[YAWDRIVE]` — does it cross the bridge into Dollet?). F11 screenshots at `Logs/screenshots/` (read with `filesystem:read_media_file`; locate via `[F11-INDEX]`).

**What .134 decides → next (Aaron approves options first):**
- **terr 12 present, bridges next to Dollet now road=1, severance 0, drive reaches Dollet** → #70 likely DONE; close it ONLY after BAT-confirmed arrival + pushed, then retire the .81/.85/.130/.127 Dollet hardcodes + the road bridge.
- **terr 12 present but NOT near Dollet** → the Dollet bridge is a DIFFERENT terrain type; the corridor dump shows which → add that type to the overlay next.
- **terr 12 IS near Dollet but still severed** → the bridge triangles need the radius bridge to reach them (raise ROAD_BRIDGE), or they share no edge (proximity-link gap).
- Consider removing the .130 gap tag `s_roadFine[56][113]=1` once the bridge fix works (it tagged the wrong cell — terr-29 mountain, not the bridge).

NAVMESH_ROUTING 0 reverts to the .99 fine-grid drive. Do NOT re-apply old gates.

---
**⚠ Everything below (the .133 / .132 / .131 / ... chain) is SUPERSEDED — the .134 root-cause bridge-overlay fix is in the block above.**

## ▶ STATUS (history): v0.18.3.133 — reverted to .131 dense bridge + [GAPDIAG]; superseded by the .134 bridge-overlay fix; see block above
LOCAL tree = **v0.18.3.133** (GitHub HEAD = v0.18.3.95; ~38 builds unpushed — Claude NEVER pushes). **Authoritative state = DEVNOTES.md "CURRENT" + CHANGELOG.md `## v0.18.3.132` / `.133`.**

#70 on-foot Dollet drive. **.132 BAT result (REGRESSION):** the nearest-pair road bridge (one bridge per pair of 8-adjacent road cells, 1267 bridges) connected the road too LOOSELY. `[ROADCON]` navA* between road samples jumped back to the pre-bridge .130 values (17-20 triangles between samples vs .131's 4), and the sparse corridor FROZE the on-foot drive ~1-2 cells from spawn -- the route's funnel threaded a path the executor immediately wedged on (idx stuck at 0/28, pos unchanged, d+0 even on reverse bursts). It ALSO still left the synthetic gap cell c113,r56 SEVERED (road[0]) even with a 3072 cap -- so that cell's triangle is geometrically isolated, farther than 3072u from any road triangle in an 8-adjacent cell.

**.133 fix (applied; do NOT re-apply):** REVERTED `Navmesh_Build` step 3.6 (`world_map_navmesh.inl`) to .131's dense radius bridge verbatim (`ROAD_BRIDGE` back to 1536; bridge EVERY road-cell triangle pair whose fine cells are 8-adjacent and centroids within the radius). That gave the best result so far (smooth, quick drive to ~4km from Dollet). Added a read-only `[GAPDIAG]` block (step 3.6b): logs the gap cell c113,r56's 8 neighbours' road status, and for each triangle whose centroid lands in that cell, its globally-nearest OTHER road triangle (cell + floorZ + distance). No pairs added; no behavioural change beyond the revert. Keeps the .130 gap tag. `FF8OPC_VERSION` bumped + CHANGELOG `## v0.18.3.133`.

**When Aaron says "BAT":** read `Logs/build_latest.log` tail (confirm Version 0.18.3.133) then `Logs/ff8_world.log`. The `[GAPDIAG]` data is logged at world-map LOAD, so Aaron only needs to ENTER the world map; driving is optional (confirms the revert restored the smooth-to-4km drive). KEY reads: the `[GAPDIAG]` lines (which neighbour cells are road; the gap triangle's nearest road triangle cell + distance + floorZ), `[ROADCON] SEVERANCE` (should be back to 1 of ~14, the road body navA* back to ~4 tris between samples), and the drive (`[YAWDRIVE]` -- smooth to ~4km again?).

**What GAPDIAG decides → the next, DATA-DRIVEN gap-cell fix (Aaron approves options first):**
- If the gap triangle's nearest road triangle is in a cell 2 away → relax the bridge to 2-cell adjacency for this region, OR directly bridge the real road-end triangle to Dollet's tri#38414.
- If a road-side neighbour cell isn't road → tag a better in-between cell (e.g. c112,r56) than the synthetic c113,r56.
- If the gap is genuinely huge (>4000u) → PIVOT to executor-level road-follow (option B: when the navmesh route dives the coast near Dollet, steer along the s_roadFine overlay).

DENSE road bridges (radius all-pairs) are REQUIRED for a good executor drive; SPARSE bridges (nearest-pair, one per cell-pair) connect too loosely and FREEZE it. NAVMESH_ROUTING 0 reverts to the .99 fine-grid drive. Do NOT re-apply old gates.

---
**⚠ Everything below (the .132 / .131 / .130 / ... chain) is SUPERSEDED — the .132 BAT result + .133 revert are in the block above.**

## ▶ STATUS (history): v0.18.3.132 — BAT'd (FROZE the drive); reverted in .133; see block above
LOCAL tree = **v0.18.3.132** (GitHub HEAD = v0.18.3.95; ~37 builds unpushed — Claude NEVER pushes). **Authoritative state = DEVNOTES.md "CURRENT" + CHANGELOG.md `## v0.18.3.131` / `.132`.**

#70 on-foot Dollet drive. **.131 BAT result:** the radius-1536 road-cell bridge (option A) worked well for the road BODY — the Timber->Dollet oracle collapsed 208->62 tris (38 on road), the severance-walk navA* dropped to 4-5 tris between samples, and the live drive ran smooth and quick to ~4km out. But `[ROADCON]` severance stayed at 1 of ~14: the final hop road[3] fine(c110,r55) -> road[0] fine(c113,r56) was STILL SEVERED (navA* 53260 over 19 tris, minZ -1429). The synthetic gap cell c113,r56's triangle sits farther than 1536u from the nearest road triangle, so the fixed-radius bridge never reached it. With that one hop missing, A* still dived the coast at c108,r60 (-1414); the F11 screenshot shows Squall wedged at the BASE of the cliff while the road curves AROUND its foot. The radius bridge also added 798k pairs (268k gate-exempted) — redundant, partly steep.

**.132 fix (applied; do NOT re-apply):** replaced the radius-all-pairs road bridge with a NEAREST-PAIR bridge in `Navmesh_Build` step 3.6 (`world_map_navmesh.inl`). Road-cell triangles are grouped by fine cell; for every pair of 8-adjacent road cells, only their single closest triangle pair is bridged (within `ROAD_BRIDGE`, now a 3072u sanity cap). This GUARANTEES every adjacent road cell pair connects regardless of distance (so the gap cell connects to both the road body and Dollet's road cell), keeps each bridge as SHORT as possible (gate height-extrapolation stays accurate over short gaps; the bridge stays on the flat road, not up the cliff), and collapses ~798k pairs to ~one per cell-pair. Candidates still pass the gate; the .128 exemption keeps road-road moves. Keeps the .130 gap tag. `FF8OPC_VERSION` bumped + CHANGELOG `## v0.18.3.132`.

**When Aaron says "BAT":** read `Logs/build_latest.log` tail (confirm Version 0.18.3.132) then `Logs/ff8_world.log`. Aaron drives on foot from a Galbadia save toward Dollet. KEY reads: `[NMROADBRIDGE]` (bridge count — should now be ~thousands, not 798k), `[ROADCON] SEVERANCE SUMMARY` (should now be 0 of ~14 — the primary self-check), `[ROADV:DRIVE]` SUMMARY (route should stop diving to -1414 and stay on road), and the live drive (`[YAWDRIVE]` — does it follow the road around the cliff into Dollet?). The F11 screenshots live at `Logs/screenshots/` (read with `filesystem:read_media_file`; locate via the `[F11-INDEX]` block in the world log).

**What .132 decides → next:**
- **Severance 0, route stays on road, drive reaches Dollet** → #70 done; close it ONLY after BAT-confirmed arrival + pushed, then retire the .81/.85/.130 Dollet hardcodes + this bridge.
- **Severance still 1** → the gap cell c113,r56's nearest road cell is more than one cell away (the 8-adjacency found no road neighbour with a near-enough triangle) → tag the in-between cell, or also tag the catalog Dollet cell c112,r57 road, or RAISE ROAD_BRIDGE.
- **Route takes an off-road shortcut / drive wedges somewhere new** → a nearest pair bridged across a notch → LOWER ROAD_BRIDGE or add a max-step check on the bridge.

NAVMESH_ROUTING 0 reverts to the .99 fine-grid drive. Do NOT re-apply old gates.

---
**⚠ Everything below (the .131-applied block + the .130/.129/... chain) is SUPERSEDED — the .131 BAT result + .132 fix are in the block above.**

## ▶ STATUS (history): v0.18.3.131 — BAT'd; .132 (nearest-pair) applied; see block above
LOCAL tree = **v0.18.3.131** (GitHub HEAD = v0.18.3.95; ~36 builds unpushed — Claude NEVER pushes). **Authoritative state = DEVNOTES.md "CURRENT" + CHANGELOG.md `## v0.18.3.130` / `.131`.**

#70 on-foot Dollet drive — the .130 gap tag fixed the road OVERLAY (`[ROADCON]` reports the ribbon REACHES Dollet) but NOT the navmesh routing: the `[ROADV:DRIVE]` route stayed byte-identical to .129 (dives c108,r60 −500 → c109,r60 −1414 at idx 8) and the drive jammed at the same ~4.6km cliff. The `[ROADCON]` severance walk located the cause precisely — exactly ONE of ~14 road segments is severed, the coastal approach road[3] fine(c110,r55) → road[0] fine(c113,r56) (navmesh A* detours −1429 over 21 tris). Cell-tagging couldn't fix it: the road's triangles there share no edge and sit >400u apart, so the .105 proximity links (400u) never make a candidate pair, and the .128 exemption (keeps edge-adjacent road links) has no edge to keep.

**.131 fix (applied; option A; do NOT re-apply):** road-cell navmesh proximity bridge in `Navmesh_Build` step 3.6 (`world_map_navmesh.inl`). After the .105 proximity links and before the engine gate, for every pair of road-cell triangles whose fine cells are 8-ADJACENT and whose centroids are within `ROAD_BRIDGE` (1536u, ~1.5 fine cells), add a candidate connection. Natural extension of the .128 edge-adjacent road exemption to NEAR-adjacent road cells; cell-adjacency + the tight radius keep every bridge short and ON the shallow road surface (engine-walkable, never spans the −1429 coast trench); the candidates still pass through the gate, where the .128 exemption keeps the road-road moves. Road-only = safe. New `[NMROADBRIDGE]` log line counts the pairs added. Keeps the .130 gap tag (the bridge needs the gap cell tagged road). `FF8OPC_VERSION` bumped + CHANGELOG `## v0.18.3.131`.

**When Aaron says "BAT":** read `Logs/build_latest.log` tail (confirm Version 0.18.3.131) then `Logs/ff8_world.log`. Aaron drives on foot from a Galbadia save toward Dollet. KEY reads: `[NMROADBRIDGE]` (how many bridge pairs added — should be > 0), `[ROADCON] SEVERANCE SUMMARY` (should now be 0 of ~14), `[ROADV:DRIVE]` SUMMARY (route should stop diving to -1414, more ROAD waypoints), and the live drive (`[YAWDRIVE]` — does it follow the road past the old ~4.6km cliff into Dollet?). Random battles interrupt + resume.

**What .131 decides → next:**
- **Severance 0, route stays on road, drive reaches Dollet** → #70 done; close it ONLY after BAT-confirmed arrival + pushed, then retire the .81/.85/.130 Dollet hardcodes + this bridge.
- **Severance still 1 (last segment didn't bridge)** → the pinch road triangles are farther apart than ROAD_BRIDGE, or a cell between road[3] and road[0] isn't road-tagged → RAISE ROAD_BRIDGE (e.g. 2048) or also tag the catalog Dollet cell c112,r57 road.
- **Route takes an off-road shortcut / drive wedges somewhere new** → ROAD_BRIDGE too large (bridged a notch) → LOWER it or tighten the cell-adjacency.

NAVMESH_ROUTING 0 reverts to the .99 fine-grid drive. Do NOT re-apply old gates.

---
**⚠ Everything below (the .130-BAT'd block + the .129/.128/... chain) is SUPERSEDED — the .130 BAT result + .131 fix are in the block above.**

## ▶ STATUS (history): v0.18.3.130 — BAT'd; .131 (option A) applied; see block above
LOCAL tree = **v0.18.3.130** (GitHub HEAD = v0.18.3.95; ~35 builds unpushed — Claude NEVER pushes). The .130 `s_roadFine[56][113]=1` gap tag registered (`[ROADMAP]` 624 road cells, was 623) and `[ROADCON]` now reports the road ribbon REACHES Dollet (road-end fine c113,r56, 0 cells short). BUT the navmesh routing is UNCHANGED — the `[ROADV:DRIVE]` route is byte-identical to .129 (dives c108,r60 −500 → c109,r60 −1414 at idx 8), and the live drive jams at the same ~4.6km cliff (idx 4/9, `U--- JAM`). So the tag, while it registered, couldn't redirect A*.

**Root, pinpointed by the `[ROADCON]` severance walk:** exactly ONE of ~14 road segments is SEVERED — the final coastal approach to Dollet, road[3] fine(c110,r55,−638) → road[0] fine(c113,r56,−172), navmesh A* = 53422 over 21 tris minZ=−1429 (forced deep detour). 13/14 road segments are navmesh-connected; only the last coastal pinch resists. Cell-tagging can't fix it: tagging a CELL can't create triangle EDGE-adjacency where the mesh has none (the road's triangles there are 21 tris apart), so the .128 exemption (which only KEEPS edge-adjacent road links) has no edge to keep. A* keeps diving the coast (cheaper than the severed road loop, whose last hop detours −1429).

**NEXT — Aaron picks the approach, then Claude builds ONE change:**
- **(A, RECOMMENDED) road-cell navmesh proximity bridge:** in Navmesh_Build, after the normal build, where two road cells are cell-adjacent (8-conn) but their triangles aren't navmesh-connected, add a direct navmesh edge between their nearest triangles. Natural extension of the .128 exemption (which keeps edge-adjacent road links; this adds near-adjacent ones); road-only = safe; repairs the whole road corridor's connectivity. Re-run `[ROADCON]` — severance should drop to 0 and the route should stop diving.
- **(B) executor road-follow for the final approach:** when the drive nears Dollet and the navmesh path ahead dives deep, steer along the s_roadFine overlay (which DOES reach Dollet) instead of the navmesh path.

Keep the .130 tag — approach A needs the ribbon to reach Dollet. The 2 F11 shots (f11_115145, f11_115150) are the jam at the cliff. NAVMESH_ROUTING 0 reverts to .99. Do NOT re-apply old gates.

**3-identical watch:** .129 and .130 live drives are byte-identical (4.6km cliff jam) — that's 2. A third cell-tag tweak with the same outcome = pivot; but switching to the navmesh-edge bridge (A) IS the pivot, so it's clear of the rule.

---
**⚠ Everything below (the .130-awaiting-BAT block + the .129/.128/... chain) is SUPERSEDED — the .130 BAT result is folded into the block above.**

## ▶ STATUS (history): v0.18.3.130 — APPLIED + BAT'd; see block above
LOCAL tree = **v0.18.3.130** (GitHub HEAD = v0.18.3.95, c591803b; ~35 builds unpushed — Claude NEVER pushes). **Authoritative current state = DEVNOTES.md "CURRENT" + CHANGELOG.md `## v0.18.3.129` / `.130`.**

#70 on-foot Dollet drive — **the .129 LOS-clamp floor WORKED** (the coincident-waypoint oscillation is gone): on the .129 BAT the mid-drive re-plan's `[YAWDRIVE]` idx climbed cleanly 0→1→2→3→4 with a real cell ahead in the steer target (not pinned ~12u at the player), and the drive physically walked to ~4.6km from Dollet — the 3rd straight gain (20.5km → 5.3km → 4.6km). Routing + the executor oscillation are both handled.

**The .129 BAT surfaced the real final-approach blocker, and corrected a wrong .128 assumption.** The route dives off the shallow road straight onto the deep Dollet coast: route waypoint 7→8 is `c108,r60 floorZ=-500 ROAD` then `c109,r60 floorZ=-1414 off` — a **914-unit drop in one step**, far past the engine's 200u on-foot limit. The engine physically refuses it, so the player JAMS at the cliff edge (`U--- JAM`, d+4; the reverse-burst backs him WSW off the wall, then UP walks him right back). He never enters the coast — he can't. So the .128 "the coast is walkable because the gate allowed it" was WRONG: the gate's interpolated corner-height sampling let a cliff connection through that the engine blocks (same mean-of-corners unreliability, opposite direction from the severance the .128 exemption fixed).

**Why A* dives the cliff:** the road LOOPS west-then-north-then-east to approach Dollet from the north on shallow ground, but the .128 [ROADCON] gap profile showed the ribbon reaches the road-end at fine(c112,r55) and stops 2 cells short of Dollet's road cell fine(c113,r57). The lone untagged cell between them is fine(c113,r56) (terrain 29, road=0 -- walkable LAND, not ocean). With the loop disconnected, the cliff dive is A*'s only path to the coastal town despite its ~18000u vertical penalty.

**.130 fix (applied; do NOT re-apply):** in `world_map_segments.inl`, tag the gap cell `s_roadFine[56][113] = 1` (row 56, col 113) right before `Navmesh_Build()`, so the road-cell gate exemption keeps the road-end<->gap<->Dollet connections (8-connected), the .127 floor clamp shallows the cell, and the .85 [ROADMAP] override forces it walkable — completing an all-road path into Dollet that A* prefers over the cliff dive (the loop's shallow cost beats the coast cut's ~18000 vertical penalty), and the loop is engine-walkable so the executor can follow it in. Hardcoded one-off like the .81/.85 Dollet patches; retires with them once #70 is confirmed. `FF8OPC_VERSION` bumped + CHANGELOG `## v0.18.3.130`.

**When Aaron says "BAT":** read `Logs/build_latest.log` tail (confirm `Version 0.18.3.130`) then `Logs/ff8_world.log`. Aaron drives on foot from a Galbadia save toward Dollet. KEY reads: `[ROADCON]` — does the road ribbon now REACH Dollet (no longer "2 cells short")? `[ROADV:DRIVE]` SUMMARY — does the route stop diving to -1414 and stay on road (more `ROAD` waypoints)? And the drive itself — does it follow the road loop past the ~4.6km cliff into Dollet? Random battles interrupt + resume.

**What .130 decides → next step:**
- **Route follows the road into Dollet / drive reaches Dollet** → the gap was the blocker; iterate to arrival, then close #70 once the drive reaches Dollet (do NOT comment/close until BAT-confirmed arrival + pushed). Then retire the .81/.85/.130 Dollet hardcodes.
- **[ROADCON] still says 2 short, or the route still dives** → the gap cell's navmesh triangle isn't edge-adjacent (or proximity-linked) to BOTH the road-end and Dollet, so the exemption couldn't bridge it. Next: widen the tag to the adjacent `c112,r56` (verify it's not ocean first), or add an explicit road-to-road navmesh bridge across the road-end->Dollet pair. Aaron picks before Claude builds.

NAVMESH_ROUTING 0 reverts to the .99 fine-grid drive. Do NOT re-apply .117/.118/.119 gates, the floor-gate, or .120/.121/.122 changes.

---
**⚠ Everything below (the v0.18.3.129 block + the .128/.127/.126/.124/.122/.119/.116-era chain) is SUPERSEDED history — the .129 BAT result is folded into the .130 block above.**

## ▶ STATUS (history): v0.18.3.129 — BAT'd, oscillation fixed; see .130 block above
LOCAL tree = **v0.18.3.129** (GitHub HEAD = v0.18.3.95, c591803b; ~34 builds unpushed — Claude NEVER pushes). **Authoritative current state = DEVNOTES.md "CURRENT" + CHANGELOG.md `## v0.18.3.128` / `.129`.**

#70 on-foot Dollet drive — **.128 was the BREAKTHROUGH.** The road-cell gate exemption (exempt road↔road connections from the .120 `NmEngineStepBlocked` gate) reconnected the road FULLY: `[ROADCON] SEVERANCE 0 of 13` (was 2), road[11] now 17 tris min -657 (was a 78-tri -1567 dive), the Timber→Dollet oracle 109/208 + drive 63/89 on road both topping at -1414 not -1567 — and the live drive PHYSICALLY followed the road from 20.5km down to ~5km from Dollet (crossing the old road[11] canyon rim at fine c105,r63 cleanly), vs every prior build wedging at the canonical start. **The core routing problem is SOLVED.**

**The 5km stall is the EXECUTOR, not the route.** Entering Dollet's region triggered a mid-drive re-plan, and the steer target immediately collapsed onto the player's OWN cell (steer ~12u away, idx pinned 0/8) → targetBearing went to noise → the on-foot 8-way sector flipped (-D-R/--L-/-DL-/---R) in place until a random battle (MODE_SWIRL) paused the drive (it resumes on re-entry). Root cause (exactly the "narrower final-approach problem" the .110 revert comment flagged): `s_driveNavmeshPath` stays FALSE (the .110 revert kept the LOS clamp active because the 15km funnel corners need it), so `FineLineClearFootCar` runs on the navmesh path — and on the final approach EVERY forward straight-line crosses the .81 Dollet false-coast box (fine cols 104-111 rows 59-69, forced steep-mountain), so every line reads blocked and the clamp walked `wi` all the way back to `s_drivePathIdx`, the player's current cell. Steering at your own cell steers nowhere.

**.129 fix (applied; do NOT re-apply):** in `world_map_drive.inl`, floor the LOS-clamp walkback at `s_drivePathIdx + 1` (was `s_drivePathIdx`) so the steer target can never collapse onto the player himself. The next path cell is navmesh-adjacent by construction (A* connected idx→idx+1), so steering at it is always walkable even when the COARSE grid marks the straight line blocked — the drive staircases forward through the false-coast cells into Dollet. The .110 funnel-corner protection is intact for every cell beyond idx+1 (the clamp still walks far targets back to the nearest clear cell; it just never lands on the player). One behavioral change. `FF8OPC_VERSION` bumped + CHANGELOG `## v0.18.3.129`.

**When Aaron says "BAT":** read `Logs/build_latest.log` tail (confirm `Version 0.18.3.129`) then `Logs/ff8_world.log`. Aaron drives on foot from a Galbadia save toward Dollet. KEY reads: at the ~5km Dollet-region re-plan, does `[YAWDRIVE]` idx CLIMB past 0/N (steer target a real cell ahead, not pinned ~12u at the player), do the keys stop flipping in place, and does the drive push past 5km toward Dollet? Random battles interrupt + resume, so a clean run to arrival may need clearing/avoiding encounters on the final approach.

**What .129 decides → next step:**
- **Drive pushes past 5km / reaches or nears Dollet** → the over-clamp WAS the 5km blocker; iterate to arrival, then close #70 once the drive reaches Dollet (do NOT comment/close until BAT-confirmed arrival + pushed).
- **Drive still stalls on the final approach** → revisit the DEFERRED routing fix: the road approaches Dollet from the north (road-end fine c112,r55) but stops ~2 cells short (a 1-cell rasterization gap at c113,r56), so A*'s only direct way into the coastal town is across the deep coast; tag the gap cell as road (clamp + exemption + [ROADMAP] override then apply) so A* follows the road into Dollet instead of the coast. Aaron picks before Claude builds.

NAVMESH_ROUTING 0 reverts to the .99 fine-grid drive. Do NOT re-apply .117/.118/.119 gates, the floor-gate, or .120/.121/.122 changes.

---
**⚠ Everything below (the v0.18.3.128 block + the .127/.126/.124/.122/.119/.116-era chain) is SUPERSEDED history — the .128 BAT result is folded into the .129 block above.**

## ▶ STATUS (history): v0.18.3.128 — BAT'd, breakthrough; see .129 block above
LOCAL tree = **v0.18.3.128** (GitHub HEAD = v0.18.3.95, c591803b; ~33 builds unpushed — Claude NEVER pushes). **Authoritative current state = DEVNOTES.md "CURRENT" + CHANGELOG.md `## v0.18.3.127` / `.128`.**

#70 on-foot Dollet drive — the .120 connection GATE (not A* cost) is the binding constraint, confirmed by the .127 BAT. The road from Timber is 11/13 segments navmesh-connected and shallow; the whole break is ONE cell, road[11] fine(c105,r63), where a terr-14 navmesh triangle straddles the canyon rim (mean-of-corners floorZ artifact). **.127 clamped that cell's s_nmFloor to -500** and helped (severed 2->1, road[8] reconnected), but the oracle (80/232) and drive (36/115) routes were BYTE-IDENTICAL to the .123 baseline, still diving to -1567. Cost-correction alone did not redirect A*: road[11] stays severed because the rim(road[14])->crossing path detours 78 triangles through the -1567 canyon -- the GATE cut the direct link on corner-height grounds, and the canyon detour isn't a road cell so the clamp never touched it.

**.128 fix (applied; do NOT re-apply):** exempt road-cell connections from the gate. (1) `s_roadFine` DECLARATION moved from world_map_segments.inl to world_map_state.inl (still populated by RasterizeTriRoad) so the navmesh can read it. (2) In `NmEngineStepBlocked` (world_map_navmesh.inl), compute each triangle's centroid fine cell (game = mesh - NM_W/2, then WorldXToFineCol/Row); when the gate WOULD block a move (step >= 200) but BOTH centroids are road cells, KEEP the connection and count it (`s_nmRoadExempt`). The `[NAVMESH] built ...` log now also reports `gate dropped N connections, road-cell exemption kept M`. Keeps the .127 clamp. Ground-truth road, not steering; the exemption only ever KEEPS a road-to-road connection. `FF8OPC_VERSION` bumped + CHANGELOG `## v0.18.3.128`.

**Scope caveat:** the exemption reconnects adjacent road-cell triangle pairs, so its reach at the crossing depends on the road ribbon's triangles being edge-adjacent (or proximity-linked) there; a non-road triangle between two road cells could still pinch. The [ROADCON] severance walk re-runs at load, so the BAT shows directly whether road[11] connects.

**When Aaron says "BAT":** read `Logs/build_latest.log` tail (confirm `Version 0.18.3.128`) then `Logs/ff8_world.log`. Aaron should LOAD a Galbadia world-map save AND drive on foot toward Dollet. KEY reads: `[NAVMESH] ... road-cell exemption kept M` (did the exemption fire, and how many connections); `[ROADCON] SEVERANCE SUMMARY` (did road[11] reconnect -- severed -> 0?); `[ROADV:TIMBER-DOLLET]` + `[ROADV:DRIVE]` SUMMARY (does A*'s route finally stop diving to -1567 and track the road?); the drive outcome.

**What .128 decides -> next step:**
- **Route tracks the road / stops diving** -> the gate severance WAS the blocker; iterate/test the drive (#68 executor may surface next); then close #70 once the drive reaches Dollet.
- **road[11] still severed / route still dives** -> the road ribbon's triangles aren't edge-adjacent at the crossing (a non-road triangle pinches between them) -> next: widen the exemption (exempt a proximity-link between two road cells, or add a road-to-road bridge), keeping clamp + exemption. Aaron picks before Claude builds.

NAVMESH_ROUTING 0 reverts to the .99 fine-grid drive. Do NOT re-apply .117/.118/.119 gates, the floor-gate, or .120/.121/.122 changes.

---
**⚠ The v0.18.3.127 STATUS block below is SUPERSEDED by the .128 status above (kept as history).**

## ▶ STATUS at open: v0.18.3.127 APPLIED locally, AWAITING BAT
LOCAL tree = **v0.18.3.127** (GitHub HEAD = v0.18.3.95, c591803b; ~32 builds unpushed — Claude NEVER pushes). **Authoritative current state = DEVNOTES.md "CURRENT" + CHANGELOG.md `## v0.18.3.126` / `.127`.**

#70 on-foot Dollet drive — ROOT CAUSE FOUND, and .127 is the FIRST FIX (after .123-.126 read-only diagnostics). The .126 SEVERANCE walk pinned the entire blocker to ONE cell: the road from Timber is **11 of 13 segments navmesh-connected and shallow** (floorZ -231..-845), but road[11] at fine(c105,r63) reads floorZ **-1341** while its road neighbors are -231 and -552 — an isolated artifact. That cell sits on the eastern rim of the WEST canyon; its navmesh triangle (terrain 14) straddles the rim, so s_nmFloor (the MEAN of 3 corner heights — the .118 artifact) reads -1341 even though the walkable road there is shallow. That one cell severs 2 road segments AND makes A* unable to tell the shallow road from the real canyon bottom (-1567) next to it, so A* dives.

**.127 fix (applied; do NOT re-apply):** in `LoadTerrainGrid` (world_map_segments.inl) right after `Navmesh_Build()`, behind NAVMESH_DIAG — iterate every navmesh triangle; where its centroid's fine cell is a road cell (`s_roadFine`) AND `s_nmFloor < ROAD_FLOOR_DEEP (-1000)`, clamp `s_nmFloor` up to `ROAD_FLOOR_CLAMP (-500)`. Logs `[ROADFLOOR] clamped N road-cell triangles...`. Model correction (road = ground-truth walkable), not steering. `FF8OPC_VERSION` bumped + CHANGELOG `## v0.18.3.127`.

**CAVEAT (decides the next step):** the clamp corrects A*'s COST only. The .120 connection gate runs on per-corner heights DURING `Navmesh_Build`, before s_nmFloor is consulted, so a post-build clamp does NOT re-link a gate-severed crossing. The [ROADCON] severance walk re-runs at world-load on the clamped floorZ, so this BAT reveals whether cost-correction ALONE redirects A* off the canyon.

**When Aaron says "BAT":** read `Logs/build_latest.log` tail (confirm `Version 0.18.3.127`) then `Logs/ff8_world.log`. Aaron should LOAD a Galbadia world-map save AND drive on foot toward Dollet (the drive is now the real test). KEY reads: `[ROADFLOOR] clamped N` (clamp ran); `[ROADV:TIMBER-DOLLET]` + `[ROADV:DRIVE]` SUMMARY (does A*'s route stop diving to -1567 / stay on road more?); the drive outcome; `[ROADCON] SEVERANCE SUMMARY` (dropped from 2?).

**What .127 decides → next step:**
- **Route improves** (stops diving to -1567, more on road) → cost-correction sufficed; iterate/test the drive (#68 executor may surface next); tune ROAD_FLOOR_CLAMP if needed.
- **Route still dives** → the GATE severance is the binding constraint → next fix = exempt road-cell connections from the .120 gate (likely move `s_roadFine` to world_map_state.inl so navmesh.inl can see it; in `NmEngineStepBlocked` skip the block when both triangles' centroids are at road cells), keeping the clamp. Aaron picks before Claude builds.

NAVMESH_ROUTING 0 reverts to the .99 fine-grid drive. Do NOT re-apply .117/.118/.119 gates, the floor-gate, or .120/.121/.122 changes.

---
**⚠ The v0.18.3.126 STATUS block below is SUPERSEDED by the .127 status above (kept as history).**

## ▶ STATUS at open: v0.18.3.126 APPLIED locally, AWAITING BAT
LOCAL tree = **v0.18.3.126** (GitHub HEAD = v0.18.3.95, c591803b; ~31 builds unpushed — Claude NEVER pushes). **Authoritative current state = DEVNOTES.md "CURRENT" + CHANGELOG.md `## v0.18.3.124` / `.125` / `.126`.** Summary: #70 on-foot Dollet drive — a continuous shallow walkable ROAD provably exists where A* should route (Timber→Dollet, east col ~112), yet A* dives into the WEST canyon (col 100-102, floorZ -1567); .123 proved A* does NOT track the road, so routing (not the #68 executor) is the blocker. .125 corrected a wrong theory: the road overlay (`s_roadFine` = wmx terrain **27/28** only; **29=mountain**) is NOT broadly fragmented — it reaches within 2 cells of Dollet (the terrain-29 mountain road's cells also carry overlapping 27/28 polys, so road=1); only one untagged terrain-29 cell by Dollet breaks it. **.126 (awaiting BAT):** the SEVERANCE test runs on the real road — `RoadConnectivityDiag` walks the road ribbon Timber→road-end and per checkpoint logs navmesh floorZ+terrain + a short-range A* to prev, flagging SEVERED on no-path or a dive below floorZ -1000. BAT: confirm `Version 0.18.3.126`, LOAD a Galbadia save (`[ROADCON]` fires at load), read `[ROADCON] SEVERANCE SUMMARY`. SEVERED>0 → gate cut the road (recalibrate 200/190u, or exempt road-cell connections; Aaron picks before Claude builds); 0+shallow → A* cost issue.

---
**⚠ The v0.18.3.124 STATUS block below is SUPERSEDED by the .126 status above (kept as history).**

## ▶ STATUS at open: v0.18.3.124 APPLIED locally, AWAITING BAT
LOCAL tree = **v0.18.3.124** (GitHub HEAD = v0.18.3.95, commit c591803b; ~29 builds unpushed — Claude NEVER pushes). At session start read DEVNOTES.md + this file; the DEVNOTES "CURRENT" paragraph is the one-paragraph form of the below.

**#70 on-foot Dollet drive: the routing/walkability MODEL is the blocker, NOT the executor — confirmed by the .123 road-verification BAT.** The engine on-foot rule (a 200-unit/0xC8 interpolated ground-height STEP over one ~190u move; validator 0x53E7A0, barycentric interp 0x402620; full trace in `WMX_OBJ_FORMAT.md` §12) is replicated in the navmesh by the .120 engine-replica gate (`NmEngineStepBlocked`, gates CONNECTIONS not triangles), and Dollet + all Galbadia destinations are REACHABLE on that graph. `.121`/`.122` added a vertical cost penalty `NM_VERT_WEIGHT`×per-step |Δfloorz| (5→20, cost-shaping only, cannot disconnect).

**.122 BAT result:** at W=20 the route stays SHALLOW for the first ~25 waypoints, but the canyon is UNAVOIDABLE (Dollet is coastal and its own approach dips deep), and the drive wedges AT the canonical start (idx 0/35) — the #68 executor cutting the corner into a wall on GENTLE terrain (not the canyon). That looked like an executor problem, so Aaron called for a clean test of the routing first.

**.123 road-verification BAT — directive + verdict:** use the player-walkable Timber→Dollet road as a GROUND-TRUTH oracle (NOT a steering crutch — the navmesh A* never consults `s_roadFine`, so comparing A*'s route to the road is an independent test of the model). VERDICT NEGATIVE: A* does NOT track the road. The Timber→Dollet oracle (A* between the road's OWN endpoints) came back 80/232 waypoints on road, diving to floorZ -1567 — it starts on the road at Timber, then swings ~6km WEST off the direct line (fine col 100-102, X~-27k) into the deep canyon, then climbs back to Dollet. The actual drive route is 36/115 on road, same -1567 dive. So the MODEL routes through canyon terrain the real road avoids → routing, not the executor, is the blocker; fixing the executor would be wasted effort.

**Root-cause hypothesis (being confirmed by .124):** A* is optimal with an admissible horizontal heuristic, and the W=20 penalty makes that -1567 dive expensive (~2,600u of up-and-down × 20). For A* to STILL choose the canyon over a shallow road, the road corridor must be DISCONNECTED in the navmesh — the 200-step gate is reading the real walkable road's elevation changes (ramps) as cliffs and severing its connections, forcing the canyon detour. Fits the standing lesson that the centroid 200-step sample mis-judges ramped/steep terrain.

**.124 (applied, do NOT re-apply):** `RoadConnectivityDiag` (in `world_map_planner.inl`, called from `world_map.cpp` Initialize, behind NAVMESH_DIAG/NAVMESH_ROUTING) — read-only, fires once at world-load: (1) 8-connected BFS over the fine-grid road cells (`s_roadFine`) from Timber toward Dollet → the ordered road ribbon + a fine-grid continuity check (`[ROADCON] fine-grid road ribbon = N cells`, or "does NOT reach" if the road is broken in the fine grid); (2) walks the ribbon (decimated to ~16 checkpoints), logging each checkpoint's navmesh floorZ (does the ROAD itself stay shallow?) and running a short-range A* to the previous checkpoint, flagging SEVERED when there is no path or the sub-path dives below floorZ -1000 (A* had to leave the road between two adjacent road cells). The `[ROADCON] SUMMARY N of ~M road segments SEVERED` line is the headline. The road is never used to STEER — only to probe the mesh — so this stays inside the verify-not-crutch line. `FF8OPC_VERSION` bumped + CHANGELOG `## v0.18.3.124`.

**When Aaron says "BAT":** read `Logs/build_latest.log` tail (confirm `Version 0.18.3.124`) then `Logs/ff8_world.log`. The `[ROADCON]` lines fire at world-LOAD — Aaron only needs to LOAD a Galbadia world-map save (no drive needed; driving still re-emits the `[ROADV:DRIVE]` divergence). KEY reads: the ribbon length, the per-checkpoint floorZ profile (is the road's own navmesh shallow or deep?), and the `[ROADCON] SUMMARY` severed count.

**What .124 decides → next step:**
- **SEVERED > 0** (A* detours/dives between adjacent road cells) → the gate cut the walkable road → FIX THE GATE: either recalibrate the 200/0xC8 threshold or the ~190u `NM_STEP_DIST`, OR exempt road-cell connections from the gate (the road is ground-truth walkable). Aaron chooses direction before Claude builds either. (Framing: exempting known-walkable road cells from an over-aggressive gate corrects the walkability MODEL with ground truth — it is not "steering by the road," so it stays inside the verify-not-crutch line.)
- **0 SEVERED** (road IS navmesh-connected + shallow) → the divergence is instead a COST/floorZ problem in A*, a different fix. The per-checkpoint floorZ profile independently shows whether the road's own navmesh elevation is shallow (good) or a mesh-floorZ artifact.

NAVMESH_ROUTING 0 reverts to the .99 fine-grid drive. Do NOT re-apply .117/.118/.119 gates, the floor-gate, or .120/.121/.122 changes.

---
**⚠ The v0.18.3.122 STATUS block below is SUPERSEDED by the .124 status above (kept only as history).**

## ▶ STATUS at open: v0.18.3.122 APPLIED locally, AWAITING BAT
LOCAL tree = **v0.18.3.122** (GitHub HEAD still .104; 20+ builds unpushed — Claude NEVER pushes).

**#70 navmesh ROUTING is effectively SOLVED; the remaining blocker is the #68 EXECUTOR.** The engine on-foot walkability rule (a 200-unit/0xC8 interpolated ground-height STEP over one ~190u move; validator 0x53E7A0, barycentric interp 0x402620; full trace in `WMX_OBJ_FORMAT.md` §12) is now replicated DIRECTLY in the navmesh, and the .120 BAT confirmed it.

**.120 (engine-replica gate, BAT'd):** `world_map_navmesh.inl` keeps each triangle's 3 corner heights through Build; `NmInterpHeight` = the 0x402620 barycentric interp; `NmEngineStepBlocked` samples the exact interpolated height along every candidate connection (shared-edge / T-junction bridge / proximity link) at ~`NM_STEP_DIST`(190)u and DROPS the connection if any step's height change reaches `NM_HEIGHT_STEP`(200). Gates CONNECTIONS, not triangles. BAT result: 267,596 connections blocked, full 157,416-tri mesh kept, largest component 73,707, 35,236 reachable from the Galbadia start, **Dollet + Timber + Galbadia Garden + Deling + Tomb + Prison + Missile Base + Winhill all REACHABLE**, Balamb/other continents correctly not — matching the known-good .107 numbers. The gate is faithful and Dollet is reachable on a principled graph.

**But A* still dives into the box canyon** (`[NAVPATH]`: floorZ -679 → -1601 → climbs out to Dollet -264). Every step is under 200/190u, so the canyon is genuinely engine-WALKABLE (gentle winding descent). The F11 shows Squall wedged at the foot of the canyon WALL: the executor cuts straight across the gentle winding descent into the rock face (the 85-centroid path is collapsed to 24 coarse 1024u cells; the executor LOS clamp runs on that coarse grid, blind to a sub-1024u cliff).

**CORRECTION (Aaron caught this):** the .120 notes claimed the engine "lets the player switchback up a steep face" — that was an UNVERIFIED inference (the candidate-builder 0x56CD50 vector math was never decoded) and is WRONG; the game does NOT allow switchbacking up world-map mountains. It was only in the explanation, never the code. The true reason connection-gating beats .119's triangle-removal: it drops only the steep MOVES while keeping a triangle's gentle edges, so the mesh stays connected (.119 deleted whole triangles, 29%, disconnecting Dollet). Code comments fixed in .121. Real lesson: the 200-step centroid sample can PASS a steep stretch the player can't actually traverse — steep terrain is where the walkmesh model is least reliable.

**.121 (W=5) BAT result:** penalty ACTIVE but TOO WEAK. `[NAVMESH]` unchanged (Dollet REACHABLE). The connectivity A*->Dollet cost rose 30,259->57,748, the drive route shifted 85->90 tris, and Squall got ~1,000u further (wedge dist ~16,450 vs 17,443) — but the `[NAVPATH]` still DIVES to floorZ -1,567 and the executor wedges at the same canyon wall (F11). A hard floor-z gate is NOT the answer: the .114 gate (`s_nmAStarFloorMin`=-1050) disconnected Dollet, because Dollet is coastal and its own approach dips below the floor too. So the deep route may be UNAVOIDABLE; the canyon is engine-WALKABLE (.120), so this is likely a #68 EXECUTOR problem.

**.122 FIX (applied, do NOT re-apply):** raise `NM_VERT_WEIGHT` 5.0 -> 20.0 (one line in world_map_navmesh.inl). A 4x penalty is a DECISIVE test, not a blind tweak. Cost-shaping ONLY — cannot disconnect. `FF8OPC_VERSION` bumped + CHANGELOG `## v0.18.3.122` added.

**When Aaron says "BAT":** read `Logs/build_latest.log` tail (confirm `Version 0.18.3.122`) then `Logs/ff8_world.log`. `[NAVMESH]` stays UNCHANGED (Dollet REACHABLE). KEY read = the `[NAVPATH]` dump: if it now stays SHALLOW (no -1,567 dive; floorZ nearer the -264/-574 band) the penalty WON -> the drive should follow the flatter route and get further (then likely wedge on #68 further along). If it STILL dives to -1,567 at W=20, no shallow route exists -> the deep route is unavoidable -> PIVOT to the EXECUTOR fix: feed it the 90 navmesh centroids directly instead of the 29 coarse cells, follow them closely, and set `s_driveNavmeshPath` TRUE to drop the coarse-grid LOS clamp + #83 fwd-guard (safe now — .120's gate guarantees the route is engine-walkable). Regression-check Timber. NAVMESH_ROUTING 0 reverts to the .99 fine-grid drive.

---
**⚠ Everything from "**.118 BAT result (FAILED)**" down to the "## ★★★ THIS SESSION" header is SUPERSEDED by the status above (kept only as history). The "THIS SESSION" engine-rule background and the disassembly-tooling / standing-hazards sections further below remain valid reference.**

**.118 BAT result (FAILED):** the A* floor-step gate at 200 built clean and kept
Dollet reachable, but the on-foot drive wedged at the SAME box canyon. The
`[NAVPATH]` dump showed the route STILL dives in (floorZ -566 -> -1601), now via a
GENTLER descent where every step is <=197 (-197, -179, -163, -158) -- all under the
200 gate. A* just found a shallower-stepping way down the same wall.

**Root cause (confirmed in code):** `s_nmFloor` is the MEAN of a triangle's 3 corner
heights, so a cliff face reads as a gentle staircase and NO centroid-floor-step
threshold separates the canyon (<=197 steps) from the gentle corridor (176 neck) --
they overlap (exactly what .114 suspected; the full path proves it). Gating centroid
floor-steps is the wrong metric.

**.119 fix (already written, do NOT re-apply):** per-triangle SLOPE gate -- the
engine's actual collision quantity. The validator 0x53E7A0 refuses a move whose exact
ground height changes >=200 over one ~200u step, i.e. a surface gradient of
200/~200 = 1.0 (~45deg). `Navmesh_AddTriangle` (world_map_navmesh.inl) now computes
each triangle's slope from its 3 verts and DROPS triangles steeper than
`NM_STEEP_SLOPE`=1.0 (+ vertical/degenerate = walls), so the canyon walls are GONE
from the graph and A* must take the corridor. The .118 floor-step gate is REVERTED:
A* / flood / diag all pass ungated floor-step (`-1`) again (planner + navmesh.inl).
.117 seam gates (`NM_BRIDGE_ZTOL`/`NM_LINK_ZSTEP`=200) kept. `FF8OPC_VERSION` bumped
+ CHANGELOG `## v0.18.3.119` added. Safety: at the engine's threshold only triangles
the engine ALSO blocks are dropped -> can't sever a route the engine permits (the
.114/.116 over-exclusion failure mode).

**Why slope=1.0 (not extracted from 0x53D8A0):** the engine's ~200u step distance is
already established by prior RE and encoded in the code (`NM_STEP_LINK`=400 = "~2
engine steps"); 200/200 = 1.0. Reading 0x53D8A0 to pin it exactly stalled -- it
delegates candidate-building to sub_56CD50 (vector math) and no disassembler is
available this session. 1.0 is the principled seed; calibrate via BAT if needed
(canyon walls far steeper than the corridor -> wide valid window).

**When Aaron says "BAT":** read `Logs/build_latest.log` tail (confirm `Version
0.18.3.119`) then `Logs/ff8_world.log`. Read the `[NAVMESH]` line FIRST -- it now
reports a **steep-skipped count** (should be a large fraction = the cliffs) + whether
**Dollet** is REACHABLE on the slope-gated graph. If `not reachable`, the threshold
clipped the corridor -> RAISE `NM_STEEP_SLOPE` (e.g. 1.3) and stop. Then the drive:
the `[NAVPATH]` route should stay SHALLOW (no -1601 canyon dip) and bend through the
corridor, `[YAWDRIVE]` idx climb past 0, ideally arrival. If A* STILL dives into the
canyon, threshold too high -> LOWER (e.g. 0.7). If it ARRIVES -> retire the .81
false-coast hardcode + .85 road override, and close/comment #70. The "THIS SESSION"
section below is the engine-rule background (still current).

## ★★★ THIS SESSION — ON-FOOT WALKABILITY RULE DECODED (the #69 / .104 answer) ★★★
HEAD verified = **v0.18.3.104** (not .95). Pure-RE finding; the navmesh fix it
motivated is now APPLIED at **v0.18.3.119** (see STATUS above).

**The engine's world-map ON-FOOT walkability is a 200-unit (0xC8) interpolated
ground-height STEP GATE — NOT a data flag, NOT a ground-type, NOT terrain-29.**
Full writeup in `Plan & Research Documents/WMX_OBJ_FORMAT.md` §12. Decoded from
FF8_EN.exe:
- **0x53E7A0** = world-map movement/collision validator (9 callers). It loops over
  candidate neighbour positions (built by 0x53D8A0, stride 0x2C) and rejects any
  whose ground-height differs from the current height by `>= 0xC8 (200)`:
  `mov ecx,[ebp-0x18](candidate H); mov edx,[0x203fe30](current H); sub;abs; cmp eax,0xC8; jge reject`.
- **0x53EB80** finds the polygon under (X,Z) (block-getter 0x553E00 + point-in-tri
  0x402620); **0x402620** barycentrically interpolates the ground height from the
  triangle's 3 vertices and returns it.
- **0x53E730** (and an inlined copy in 0x53E7A0) is a *separate* per-VEHICLE
  ground-type passability check (car/garden/chocobo/ragnarok read specific bits of
  the poly `[13,14,15]` tuple). **FOOT (mode 0x80) falls through — exempt.** That
  is why the Dollet false-coast is "byte-indistinguishable" for foot: the bytes
  don't gate foot at all; only the 200u height step does. The false-coast is just
  a `>=200u` height ledge.

**This may break the .104 "geometry cannot separate them" impasse.** That verdict
was a height-RESOLUTION artifact: the mod gated at 400 (`WM_CLIMB_STEP`) / 300
(navmesh `NM_LINK_ZSTEP`) on COARSE heights (1024u cell averages / triangle
centroids), which blurred the `>=200` ledge and used the wrong threshold. The
engine uses **exactly 200 on exact barycentric-interpolated heights**.

**⇒ NEXT STEP (revised after this session's offline-validation attempt):**

OFFLINE GRID VALIDATION IS THE WRONG TOOL HERE — a fine grid can't faithfully
reproduce the engine's sampling, which is **per-block point-in-triangle** (0x53EB80
searches ONLY the player's current block via block-getter 0x553E00, not a global
triangle soup). An offline global grid manufactures fake >=200 steps at block
seams (adjacent blocks' surface triangles don't share vertices) and at cross-block
overlaps, so it fragments where the engine wouldn't. (Confirmed this session:
`/tmp/val200.py`, `/tmp/render_cliffs.py` — even with per-block footprint clipping
+ height-range filter, the corridor is too seam-fragmented to read a clean pass.)
This is the SAME reason prior offline attempts "couldn't separate" the route — not
because the 200 rule is wrong (the disassembly is unambiguous), but because the
offline mesh reconstruction is noisy at seams.

WHAT THE 200-GATE ACTUALLY EXPLAINS (the real bug): the 15km wedge is the
engine's own 200-gate firing. The mod's COARSE navmesh marks the false-coast
walkable, the planner routes a path across it, the executor steers there, and the
ENGINE's 200-step collision refuses the move → Squall oscillates (reverse-burst,
re-press, "Stuck check N/6") and wedges. So the durable fix is to make the mod's
planner enforce the SAME gate so it never routes into the ledge.

IMPLEMENTATION (small, in `world_map_navmesh.inl` — the navmesh ALREADY has the
right machinery): the v0.18.3.107 z-aware T-junction bridge already interpolates
exact edge-z and gates at NM_BRIDGE_ZTOL (a real per-edge height-step gate). The
fix is just to feed mountains back and align every gate to the engine's 200.
Exact change:
  (a) DELETE `if (terrain == 29) return;` in Navmesh_AddTriangle — feed mountains
      back; the road-pass IS terr-29, and the 200 height-step (not the terrain
      byte) separates the walkable pass from the impassable face. (This reverts
      the failed .116 mountain-exclusion; it also makes the failed .115
      steepness gate and the wrong .105 "poly[0x0E] bit7" filter both moot —
      0x0E bit7 is a per-VEHICLE ground-type bit; FOOT is exempt.)
  (b) NM_BRIDGE_ZTOL 100 -> 200 — bridge the gentle road-pass seams the 100
      cutoff was dropping; the false-coast's >=200 seam still won't bridge.
  (c) NM_LINK_ZSTEP 300 -> 200 — proximity links stop leaking the 200-300
      false-coast necks the engine refuses.
  (d) if A* uses NM_CLIMB_STEP as a per-edge gate, set it 400 -> 200 too.
Small delta on the already-offline-validated .107 feed-all approach, with
engine-correct thresholds. Retire the .81 false-coast hardcode (and the .85 road
override) and BAT. (the mod has the real loaded mesh, so it validates the rule far more
faithfully than any offline grid): plan Galbadia→Dollet on foot, confirm the route
bends through the gentle pass instead of wedging at the false-coast, and confirm
the .81 hardcode can be retired. Tune: the false-coast jump magnitude sets whether
200 exactly cuts it — if it still leaks, the gate is correct but the mod's
seam-edge height sampling (which collinear edge, which endpoints) needs to match
the engine's interpolation; if it over-cuts the pass, the seam-edge pairing is
mismatching non-adjacent edges.

Container scratch this session: `/tmp/val200.py` (grid flood-fill, fragments),
`/tmp/render_cliffs.py` (gradient render). Parser `/tmp/wmxlib.py`, mesh
`/tmp/world/wmx.obj`, disassembler `/tmp/ff8.py`. Registration (game→my-frame):
`wx=(((X+0x60000)%0x40000)-3072)%262144`, `wz=(196608-((0x48000-Z)%0x30000)-1024)%196608`.

## ★★ PRIOR SESSION — wmx.obj FORMAT FULLY CRACKED (issue #70 foundation) ★★
Offline RE session (no mod C++ written; offline-prototype gate work). Outcome:

**1. wmx.obj world-map mesh format is FULLY reverse-engineered and validated**
(0 errors / 473,193 polygons). Complete spec written to
**`Plan & Research Documents/WMX_OBJ_FORMAT.md`** — READ IT before any further
world-map work. Key facts: 835 segments × 0x9000; base map = file segments
0–767 in 32-wide row-major grid; block header = 4 bytes (P,V,N); poly = 16 B
(vert idx @0,1,2; **ground-type/region tuple @[13,14,15]**, ocean = `22 40 20`);
vertex = 8 B (X, pad, Z, height); **UP = negative Y, sea level = 0**, skirts at
−4096. Engine addrs nailed: locator **0x53DC70** (game→block, documented), loader
0x553E00, triangle-finder 0x53EB80, ground-type decoder **0x53E730**.

**2. Placement + registration solved.** File-order grid placement is correct but
Z-MIRRORED vs the engine locator frame (locator negates Z via `0x48000−Z`).
Transform engine→file: `col_mine=(col_eng−1)%128`, `row_mine=(95−row_eng)%96` →
15/15 catalog landmarks land on land. Landmark game-coords live in
`world_map_catalog.inl` `s_locations[]`.

**3. Connectivity verdict (the gate):**
- **Ocean-separation PASSES** (3 independent methods). At no-cliff-gate the
  Galbadia continent (Squall + Dollet + Timber + Galbadia Garden + Deling +
  Winhill) is ONE component; Balamb (Town + Garden + Fire Cavern) is a SEPARATE
  component across ocean. So Dollet IS reachable by land from Squall; Balamb is
  not. Foundation confirmed.
- **Walkable-pass-vs-impassable-face is NOT solvable by offline geometry.** Every
  realistic finite floor-step / height-step threshold FRAGMENTS the continent
  (Galbadia splits) — independently corroborating the findings-doc + .116
  conclusion that terr-29 "walkable road pass" vs "impassable mountain face"
  cannot be separated by slope/elevation/terrain-type. ⇒ **Option B is the right
  path; pure-geometry Path 1 cannot finish #70 alone.**

**4. (SUPERSEDED by THIS-SESSION section at top.)** The ground-type-decode idea
was tested and ANSWERED: on-foot walkability is NOT a ground-type code — it's the
200u interpolated-height-step gate (see top). Ground-type bits gate only
car/garden/chocobo/ragnarok, never foot.

Container scratch (ephemeral): `/tmp/wmxlib.py` (validated parser), `/tmp/world/
wmx.obj`, analysis scripts `verdict.py`/`prox.py`/`navmesh.py`/`raster2.py`,
`/tmp/ff8.py` (capstone disassembler for FF8_EN.exe). Durable knowledge is in
WMX_OBJ_FORMAT.md.

---

## SESSION RITUAL (do first)
1. Read DEVNOTES.md + this file.
2. `github:list_issues` (owner=ampage87, repo=FFVIII-Accessibility-Mod).
3. `github:list_commits` to verify HEAD.

## CURRENT STATE — v0.18.3.116 (LOCAL, behind NAVMESH_DIAG/NAVMESH_ROUTING, NOT pushed; GitHub HEAD = v0.18.3.95)
Active: **Issue #70 — world-map auto-drive to Dollet, on foot.**

### ★ STRATEGIC PIVOT — Option B locked (Aaron's call, this session)
Stop approximating world-map walkability from terrain-type/geometry. **READ THE ENGINE'S OWN WALKMESH** so the planner knows the true walls. Aaron chose B for scalability — it fixes every destination at once, no per-route tuning.

### Why the pivot (proven empirically over .105–.116)
On-foot world-map walkability needs ONE distinction that is NOT in any file at our resolution: **"walkable mountain path" vs "impassable mountain face."** Both are terrain-type **29 (MOUNTAINS)**.
- Aaron confirmed in-game: the **Timber→Dollet ROAD threads a real walkable pass THROUGH the mountains.** So on-foot Dollet IS reachable.
- Include all terr-29 → navmesh/fine-grid route OFF the road into impassable mountain faces → wedge at 15km.
- Exclude all terr-29 (.116) → **disconnects Dollet entirely** (the road pass reads as partly terr-29 at our cell/triangle resolution) → `navmesh A* NO path` → fine-grid fallback → SAME 15km wedge.
- The authoritative findings doc (*Plan & Research Documents/World Map Reachability Rework - offline wmx analysis findings.md*) already PROVED geometry (slope/elevation/within-triangle spread) cannot separate walkable from impassable terr-29. The .114 depth gate and .115 steepness filter both failed for exactly this reason.
- The .105 "byte 0x0E bit7 collision flag" was a MISREAD (that byte is shading/normals, not walkability).

### The 15km wedge (fine-grid drive — identical across BAT .114/.115/.116)
Player at game≈(-27800,-30400) ≈ fine(col100,row66), steering N to fine(100,64). Engine blocks N. The executor's ONLY stuck-recovery is "reverse-burst (DOWN) then re-press UP into the SAME wall" → oscillates forever ([DRIVE] Stuck check N/6). The fine-grid marks (100,65) walkable; the engine blocks it (a sub-cell road-vs-mountain wall the 1024u grid can't see). The `[RTWALK]` probe that tries to read the engine walkmesh (descBase=0x01E9FDCC, descStride=12, adjStride=32) returns garbage / coordinate-like values ([FAULT] on every poly) — that read is WRONG; 0x01E9FDCC is NOT the walkmesh descriptor table.

### .116 change (current on-disk)
`src/world_map_navmesh.inl`: `Navmesh_AddTriangle` now `if (terrain == 29) return;` (mountain exclusion), REPLACING the .115 `NM_STEEP_SPREAD` steepness block (removed) and the const (removed). The .114 depth gate `s_nmAStarFloorMin` stays dormant (INT32_MIN). Connectivity log text now "(ocean+mountains excluded, ungated)". This is CORRECT walkability but TOO COARSE — it cuts the road pass, so Dollet goes unreachable. **Decision pending:** keep .116 (authoritative, just coarse) or revert toward the .99 fine-grid behavior, depending on B. Leave as-is for now.

## OPTION B PLAN (next session — RE dive, fresh context)
GOAL: query "is this cell/edge walkable on foot" using the engine's own walkmesh data, then feed that into the planner (and/or executor). Then the road-through-mountains case and all future destinations just work.

- **STEP 1 — find the real walkmesh structure.** Reliable entry = the wmx.obj loader. The filename string `dat\wmx.obj;1` is at **0xC76274** (.data). Find `.asm` code that pushes/moves 0xC76274 → the wmx load path → trace where the parsed walkmesh base pointer is stored. (Do NOT trust 0x54B860 — see caveat.)
- **STEP 2 — identify the poly/cell format + the on-foot walkability field** (the byte/bit the engine tests to block a step).
- **STEP 3 — fix the `[RTWALK]` probe** (the RTWALK block in the world_map .inl files) to read the REAL structure. BAT: at the 15km wedge, log the player's cell + 4 neighbours' walkability; confirm the engine reports N blocked and (e.g.) NE/E open — proving we can read the truth.
- **STEP 4 — feed real walkability into the planner** (replace the terrain-type walkable test) and/or executor. Validate the full Timber→Dollet on-foot drive.

### 0x54B860 caveat (the shaky lead)
Per the navmesh header comment, 0x54B860 is the "move-validator" — but that came from the unreliable .105 analysis. This session I read its first ~10 instructions (`FF8_EN_.text_0x00501000.asm`, ~line 100782; function spans 0x54B860–0x54B900 = 160 bytes, 5 callers): they compute `|arg0->[+4] − arg3|` (abs-difference) — looks like a coordinate/wrap helper or a cell-lookup preamble, NOT an obvious walkmesh validator. Could not get its caller list (condensed callxrefs = counts only). Verify it or find the real validator via the wmx-loader route.

### Disassembly tooling
- On-disk `.asm`: OneDrive `Game Files/disassembly/FF8_EN_.text_0x00XXX000.asm` (1MB sections; image base 0x00400000; file for 0x0054xxxx is `..._0x00501000.asm`). Read via `filesystem:edit_file dryRun:true`, oldText=`"0x00XXXXXX:"` (WITH the colon, unique) → diff shows ~7 lines context AND the line number; chain forward by anchoring on the last-seen address. ~3-4 instructions per call.
- Container `/mnt/project/` has greppable copies: FF8_EN_functions.txt (8390 funcs w/ addr+file), callxrefs (COUNTS only), strings_condensed, sections, imports, exports. Use bash to grep these.
- FULL strings + FULL callxrefs (with caller lists) are ONLY in `Game Files/disassembly/` (OneDrive — not greppable in container; use dryRun/read_text_file).

### FALLBACK if B stalls — Option A (reactive executor)
When the walk JAMs toward the goal, **slide ALONG the wall** (try perpendicular directions, pick whichever the engine actually permits) instead of reverse-and-retry. Uses the engine's JAM as the walkability oracle — unblocks the drive without cracking the walkmesh. Less scalable than B. The executor's stuck-handler ("reverse un-wedge burst" / "Stuck check N/6") is in the world_map drive .inl.

## STANDING / HAZARDS
- Aaron = blind solo dev + sole tester (NVDA). ALL solutions automated/blind-accessible; NEVER suggest sighted help or "press a key when X appears."
- **BAT after a source edit RUNS THE OLD DLL** unless Aaron runs `deploy.vbs` to recompile. Always instruct rebuild + confirm `Version 0.18.3.XXX` in `Logs/build_latest.log`. Identical game-log timestamps across two BATs = no new run.
- Claude NEVER pushes; Aaron runs `Utilities/push_to_github.ps1`. Claude MAY create/update/comment GitHub issues.
- Version bump = `FF8OPC_VERSION` macro in `src/ff8_accessibility.h` (macro line has a giant inline comment → editing returns "Tool result too large" but SUCCEEDS; don't re-read to verify) + new top `## vX.Y.Z` in `CHANGELOG.md` (push utility validates heading == macro).
- F11 screenshots: 2 captured at the wedge this session (`f11_205650_362.png`, `f11_205657_747.png`) — NOT yet read (dir not located; check size first, PNGs >1MB unreadable, ~138-146KB OK). Could visually confirm the road-pass geometry.
- Use `filesystem:`-prefixed tools for OneDrive/Windows paths; bash only for `/mnt/project/` + container scratch.
- Every Claude prose response starts with `## Claude Says`.

## OTHER OPEN ISSUES (review at start via github:list_issues)
#68 executor steering (the wedge — common to navmesh + fine-grid). #69 geometry terrain-ID. #67 reachability rework (the passive terrain-logger calibration was its skipped BAT 2). #61 dialog spoken-"L". #50 Angelo gauges. #45 junction abilities (blocked). #37 source-size refactor. Backlog: Irvine Shot limit (needs Irvine in party in-game), #51-53 naming/Angelo, walk-and-talk dialog gap (hardcoded engine path, deferred).
