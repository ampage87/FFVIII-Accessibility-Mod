**Purpose & context**

Aaron is the sole developer of the FF8 Accessibility Mod — a `dinput8.dll` injection for Final Fantasy VIII Steam 2013 edition (App ID 39150, FF8_EN.exe + FFNx v1.23.x) that makes the game playable for blind players via Windows SAPI text-to-speech, navigation assistance, and entity catalogs. Aaron is blind himself and is the primary tester, using NVDA as his screen reader.

**Project root:** `C:/Users/ampag/OneDrive/Documents/FFVIII-Accessibility-Mod/FF8_OriginalPC_mod/`

GitHub: `ampage87/FFVIII-Accessibility-Mod`. v0.14.108 was the last shipped version. v0.15.0 through v0.15.2.11 are all built locally and **NOT pushed**.

---

**Current build: v0.15.2.11 — `dotown_x` removed from CHASE_FIELD_NAMES, ready to BAT**

v0.15.2.10 BAT (Fri 2026-05-08 21:46–22:05) crashed/hung ~16 seconds after entering `dotown_3` from `doopen2a`. Aaron diagnosed the cause: `dotown_3` plays a chase-end cutscene where X-ATM092 (kani) walks across the town square and shorts out — driven by the `dotown_3` kani entity in `Backgrounds` slot 1. With `dotown_3` in `CHASE_FIELD_NAMES`, our `chase_kani_freeze` module kept tracking `dotown_3`'s kani address. If a mode 4→1 transition fired during the cutscene, `StartCapture` would pin the cutscene kani's anim ID bytes, fighting the animation script every frame. v0.15.2.9 BAT survived this transition by timing luck; v0.15.2.10 got unlucky.

Fix: remove `dotown_3`, `dotown_2`, and `dotown_1` from `CHASE_FIELD_NAMES[]`. These are post-chase cutscene fields. No kani battles fire there. Removing them prevents `chase_kani_freeze` from ever engaging there.

### v0.15.2.11 changes

**`chase_detector.cpp`** (~25 lines): removed `"dotown_3"`, `"dotown_2"`, `"dotown_1"` from `CHASE_FIELD_NAMES[]`. Comment block extended to document the v0.15.2.10 crash and rationale.

### What's deferred

**The `doopen2a` "second chase battle" issue.** v0.15.2.10 BAT showed kani+battleyarou pinned but battle still triggered. Clean OTHERS-DIAG at 22:03:34 identifies `director0` (31 changes/612) as the prime non-pinned suspect. The `director0` pin is deferred to v0.15.2.12 because pinning it might break the chase-end cutscene we just unblocked (director0 might be the chase-progress-tracker). One issue at a time.

### Major hypothesis update from v0.15.2.10 BAT

**`domt5_1`'s chase agent is NOT a Director.** Clean OTHERS-DIAG (18 slots, in-field, post-battle, 21:53:20) showed every Director candidate (`plane1`, `dic`, `onkyou`, `Garutyan`, `liti`, `gura`, `saidotoujou`, `Gakekuzure`) at 0/612 changes. The active entities are all party members: `selphie2` (73), `irvine` (64), `rinoa` (47), `zell2` (31). But the kani+battleyarou pin worked in `domt5_1` (only one chase battle fired), so the chase IS triggered by kani contact there; the party member script activity is incidental (running animations + dialogue triggers during the chase cutscene).

### Modules in the build

**`chase_detector` (v0.15.2.11)** — single source of truth for chase state. Now tracks 6 chase fields: `domt1_1`, `domt2_1`, `domt3_2`, `domt4_1`, `domt5_1`, `doopen2a` (was 9 in v0.15.2.10).

**`chase_ask_overlay` (v0.15.1.2 + v0.15.2.2)** — chase-entry TTS+keyboard ASK.

**`chase_kani_freeze` (v0.15.2.9, DUAL-ENTITY PIN + ALL-OTHERS SCANNER)** — captures kani entity bytes for 10s after each chase-field battle exit; pins both kani and battleyarou full-state regions every frame after t=1500ms; snapshots+diffs all Others slots for diagnostic.

**`chase_diag` (v0.15.0+)** — F12 toggle. No-op when disabled.

---

**v0.15.2.11 BAT plan**

1. Aaron runs `deploy.vbs`. Verify `Logs/build_latest.log` timestamp later than `Fri 05/08/2026 21:46:18.99` (the v0.15.2.10 build).
2. Reach the chase scene (Comm Tower top → mountain trail → Dollet town).
3. Verify in `Logs/ff8_field.log`:
   - `ChaseDetector: chase ACTIVATED on entry to 'domt4_1'` (or similar — chase begins)
   - kani battles register `chaseActive=1` through all of `domt4_1`/`domt5_1`/`domt3_2`/`domt2_1`/`domt1_1`/`doopen2a`
   - `ChaseDetector: chase DEACTIVATED on entry to 'dotown_3' (non-chase field)` — NEW for v0.15.2.11
   - **No crash on entering `dotown_3`. The cutscene plays. Aaron reaches Lapin Beach FMV.**
4. Aaron sends "BAT" — Claude reads field log for crash signatures (none expected) + chase battle counts.

If crash persists: the cause is something else entirely; need a fresh investigation. Possible angles: a different module's interaction with `dotown_3`, an FFNx issue, or memory corruption from earlier captures.

If no crash: chase scene is now playable end-to-end. v0.15.2.12 can target the `doopen2a` second-battle annoyance.

---

**v0.15.3 backlog**
- v0.15.2.12: `director0` pin in `doopen2a` (prevent second chase battle there). Risk: might break the chase-end logic if director0 is the chase-progress-tracker.
- Re-enable engine-rendered chase ASK using the `gameObj+0xD2/0xD3` bitmask recipe.
- Fix `chase_diag::OnAskOpcodeFired` snprintf size-tracking bug.
- Delete orphan `src/chase_battle_freeze.{h,cpp}`.
- Cosmetic cleanup of `chase_kani_freeze.cpp` header comment block.
- Push accumulated v0.15.0–v0.15.2.x to GitHub via `Utilities/push_to_github.vbs` once the chase scene is BAT-validated end-to-end.
- Fix the field-change-mid-capture diagnostic skip: when field changes during the 10s capture window, BATTLEYAROU FINAL SUMMARY and OTHERS-DIAG FINAL blocks are silently skipped because the deactivation cleanup clears their state vars before EndCapture runs. Snapshot the snapshots into preserved-for-EndCapture copies so we always get full diagnostic output.
