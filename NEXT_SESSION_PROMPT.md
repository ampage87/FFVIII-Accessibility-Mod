# Next Session Prompt — v0.15.2.11 ready to BAT

**Status:** v0.15.2.11 source changes complete. Awaiting Aaron's `deploy.vbs` and BAT.

## What changed since v0.15.2.10

v0.15.2.10 BAT crashed/hung ~16 seconds after entering `dotown_3` from `doopen2a`. Aaron diagnosed: `dotown_3`'s chase-end cutscene plays an animation where X-ATM092 (kani) walks across the town square, driven by the `dotown_3` kani entity in `Backgrounds` slot 1. With `dotown_3` in `CHASE_FIELD_NAMES`, our `chase_kani_freeze` module kept tracking that kani address. If a mode 4→1 transition fired during the cutscene, `StartCapture` would pin the cutscene kani's anim ID bytes (`+0x150/+0x154/+0x1FA/+0x23F/+0x241`), fighting the animation script every frame. v0.15.2.9 BAT survived by timing luck; v0.15.2.10 got unlucky.

**v0.15.2.11 fix:** remove `dotown_3`, `dotown_2`, and `dotown_1` from `CHASE_FIELD_NAMES[]`. These are post-chase cutscene fields. No kani battles fire there; the chase is over.

## v0.15.2.11 changes

- `src/chase_detector.cpp`: removed `"dotown_3"`, `"dotown_2"`, `"dotown_1"` from `CHASE_FIELD_NAMES[]`. Comment block extended.
- `src/ff8_accessibility.h`: `FF8OPC_VERSION` bumped to `"0.15.2.11"`.
- `CHANGELOG.md`: top entry added.
- `DEVNOTES.md`, `NEXT_SESSION_PROMPT.md`: updated.

## What's deferred to v0.15.2.12

**The `doopen2a` "second chase battle" issue.** v0.15.2.10 BAT showed kani+battleyarou pinned but battle still triggered. Clean OTHERS-DIAG at 22:03:34 in `doopen2a` identifies `director0` (31 changes/612, highest non-pinned) as the prime suspect. A `director0` pin is the obvious next move BUT carries its own risk: `director0` might be the chase-progress-tracker, in which case pinning it could break the chase-end cutscene we just unblocked. We want v0.15.2.11 to ship a clean win first, then evaluate `director0` separately.

## v0.15.2.10 BAT — other major data point worth remembering

**`domt5_1` clean OTHERS-DIAG (18 slots, in-field at 21:53:20):**

| Sym | Changes | |
|-----|---------|---|
| selphie2 | 73 | party member, highest |
| irvine | 64 | party member |
| rinoa | 47 | party member |
| zell2 | 31 | party member |
| kani | 5 | **pinned** |
| battleyarou | 0 | pinned (already dormant) |
| dic, plane1, onkyou, Garutyan, liti, gura, saidotoujou, Gakekuzure | 0 | **all static** |

**The previous "Director-is-the-chase-agent in `domt5_1`" hypothesis is refuted.** Every Director candidate shows zero changes. The active entities are all party members running normal chase-cutscene animations. The kani+battleyarou pin worked correctly in `domt5_1`.

## domt1_1 chase coverage — confirmed working in v0.15.2.10

```
[21:59:51] ChaseDetector: battle entered (game-mode 0x0001 -> 0x0003);
           field='domt1_1' chaseActive=1 count=1
```

`chaseActive=1` (was `0` in v0.15.2.9 BAT). v0.15.2.10's domt1_1 fix is doing its job.

## BAT plan

1. Aaron runs `deploy.vbs`. Verify `Logs/build_latest.log` shows a timestamp later than `Fri 05/08/2026 21:46:18.99`.
2. Reach the chase scene (Comm Tower top → mountain trail → Dollet town).
3. Look for in `Logs/ff8_field.log`:
   - `chase ACTIVATED on entry to 'domt4_1'` (or wherever chase begins for Aaron's path)
   - `chaseActive=1` on all kani battles in mountain/bridge fields
   - **`chase DEACTIVATED on entry to 'dotown_3' (non-chase field)`** — NEW for v0.15.2.11
   - **No crash. The cutscene plays. Aaron reaches Lapin Beach FMV.**
4. Aaron sends "BAT" — Claude reads field log for crash signatures + chase battle counts.

## Known gotchas

- The chase scene's only active engagement points are now mountain trail (`domt1_1`–`domt5_1`) and bridge (`doopen2a`). `dotown_x` is no longer chase-tracked.
- If crash persists: the cause is something else entirely. Need a fresh investigation angle.
- F12 reserved exclusively for diagnostic builds. Currently owned by `ChaseDiag::Toggle`.

## Workflow reminder

- **Never push.** Aaron runs `Utilities/push_to_github.vbs` after BAT validation.
- **Build error:** Read `Logs/build_latest.log` first, then domain log.
- **BAT workflow:** Read `Logs/build_latest.log` tail, then `Logs/ff8_field.log` (for chase work).
- **Filesystem MCP** for ALL Windows project files. Bash often unavailable.
- Every response begins with `## Claude Says`.

## Backlog (v0.15.3+)

- v0.15.2.12: `director0` pin in `doopen2a` to prevent the second chase battle.
- Re-enable engine-rendered chase ASK using the `gameObj+0xD2/0xD3` bitmask recipe.
- Fix `chase_diag::OnAskOpcodeFired` snprintf size-tracking bug.
- Delete orphan `src/chase_battle_freeze.{h,cpp}`.
- Fix the field-change-mid-capture diagnostic skip (BATTLEYAROU FINAL + OTHERS-DIAG FINAL get silently skipped when field changes during the 10s capture window — needs preserved-for-EndCapture state copies).
- Push v0.15.0–v0.15.2.x to GitHub once the chase scene is end-to-end stable.
