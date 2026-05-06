**Purpose & context**

Aaron is the sole developer of the FF8 Accessibility Mod — a `dinput8.dll` injection for Final Fantasy VIII Steam 2013 edition (App ID 39150, FF8_EN.exe + FFNx v1.23.x) that makes the game playable for blind players via Windows SAPI text-to-speech, navigation assistance, and entity catalogs. Aaron is blind himself and is the primary tester, using NVDA as his screen reader. The mod is open-source at `github.com/ampage87/FFVIII-Accessibility-Mod`.

**Project root:** `C:/Users/ampag/OneDrive/Documents/FFVIII-Accessibility-Mod/FF8_OriginalPC_mod/`

**Completed milestones (high-level):** Title screen TTS, FMV audio descriptions, field dialog TTS, world-map navigation with BFS terrain filtering and auto-drive (now functionally validated through v0.14.98 for early-game Balamb-area destinations), field navigation with entity catalog and A\* pathfinding, battle TTS (commands, sub-menus, EWM, GF prevention, victory, damage), Junction/save/load/menu TTS, Scan spell auto-announce + interactive UI, GF summon audio descriptions, multi-channel logging.

---

**Current build: v0.14.98 BAT-PASSED.** Ready to push v0.14.97 + v0.14.98 bundled to GitHub.

**v0.14.98 BAT result (2026-05-06 17:27–17:28):** Build clean. Drive to Balamb Town completed end-to-end with planner working correctly. Aaron was in a Car (vehicle type 2). Drive went through one random encounter (paused at 17:27:14 via `MODE_SWIRL`, resumed at 17:28:33), replanned cleanly, and arrived at Balamb Town with fieldId=0x006A (fieldName='bggate_1' this time vs 'bcgate_1' in the v0.14.96 BAT — either a fieldName pointer race or a real ID collision; non-blocking observation). Refined entry updated to (12896, -26711). All five expected log markers fired:

- Program 9's clauses now both PASS instead of SKIP
- Active region set has 18 entries including 0x07 (was 16 before fix)
- `[PLAN] closest active region 0x07 at seg(17,20) segDist=0`
- `[PLAN] Player already in goal segment` (empty-path case)
- `[DRIVE] Arrival via game-mode (mode=1 MODE_FIELD, ...)`

**Validation trail across the four-build sequence:**
- v0.14.96 BAT: deferred-arrival flow validated — drives complete via game-mode, encounters paused/resumed correctly, but planner declined for Balamb-area locations and drive fell back to catalog-center steering.
- v0.14.97 BAT: PLAN-DEBUG diagnostic confirmed the hypothesis exactly — program 9's `top_story=[290,0)` SKIP filtered out region 0x07, leaving the active set without it.
- v0.14.98 BAT: one-line fix (program 9's `top_story_gte` 290 → 0) restored region 0x07 to the active set; planner now picks it at `segDist=0`; drive completes correctly.

**Sweep-abort-on-drift change (carry-forward from v0.14.97):** Did not need to fire in this BAT because the drive completed before sweep was triggered. Will be exercised in future BATs where battles drift the player further from the target.

---

**Next:** push v0.14.97 + v0.14.98 bundled to GitHub. Commit description prepared in this session's chat for Aaron's `Utilities/push_to_github.bat`.

---

**Deferred queue (post-push, in priority order):**

1. **Audit other programs for the same disassembler scope error.** Trace looked clean for programs 10/13/14 (no top-level gates that conflict with clause-local windows). The remaining story-gated programs (8 ≥333, 16 ≥350, 21 ≥1600, 23–30 ≥1750, 24 ≥750, 32 ≥1750, 34 ≥900, plus 0/3/4 ≥750) are likely correctly gated for late-game content. We'll discover any additional scope errors when Aaron drives to a location and the planner declines unexpectedly — the PLAN-DEBUG trace makes diagnosis trivial now.
2. **Persistent accessibility settings** — refined-coord serialization first slice. Aaron has empirical refined coords for Balamb Town (12896, -26711), Balamb Garden (25368, -30226), and Fire Cavern from prior BATs.
3. **Remove party members from field entity catalog**.
4. **GitHub issue #27 (SeeD rank R key)**.
5. **X-ATM092 chase scene accessibility**.
6. **Walk-and-talk dialog gap** (lowest priority).
7. **Resolving the missing 26 region IDs** in v0.14.95 closest-active-region planner — long-term; many are correctly story-gated for late-game so the gap is smaller than originally feared.

---

**GitHub state:** `main` HEAD = `0982f572` (v0.14.96, pushed Wed 2026-05-06 22:55 UTC). Local at v0.14.98 (two versions ahead, BAT-passed). Push v0.14.97 + v0.14.98 bundled next.

**Minor observations from v0.14.98 BAT log (non-blocking):**
- `pCurrentFieldName` may be racy: returned `'bggate_1'` for fieldId=0x006A whereas v0.14.96 BAT returned `'bcgate_1'` for the same fieldId. The arrival itself was correct (player entered Balamb Town and the drive announced "Arrived at Balamb Town"), so this is cosmetic-log only. If we ever rely on fieldName for behavior decisions, address then.
- `[BFS]` and `[PLAN-DEBUG]` show different vehicle types in the same window (`vehicle type 0` from BFS vs `veh=2` from PLAN-DEBUG) — expected: `s_driveActive` blocks vehicle-byte noise during the drive (v0.14.94 hotfix), so the planner uses the pre-drive vehicle while BFS reads the live byte. Working as designed.
