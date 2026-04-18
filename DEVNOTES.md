# DEVNOTES — FF8 Accessibility Mod

## Current Build: v0.13.61 (GitHub catch-up baseline, session 77 starting point)

### Status

**EWM damage/command-menu overlap bug: RESOLVED (v0.13.57).** Switching the ATB sandwich from "cap at max-1" to true freeze eliminated the converge-at-max-1 condition that was causing simultaneous dispatches at freeze release. Dispatch hooks on `sub_483470` and `sub_482F80` (v0.13.55/56) remain in place as defense-in-depth.

**EWM principle empirically validated (v0.13.59).** 10-battle A/B test against G-Soldier encounters showed party:enemy turn ratios of 2.25:1 (EWM ON) vs 2.33:1 (EWM OFF) — within 3.5% of each other. 2v2 battles matched exactly at 2.25:1. Conclusion: freeze sequences actions without perturbing the natural race. No gameplay advantage, no gameplay disadvantage.

**Turn-counter diagnostic is now reliable** and ships enabled. Each battle logs a clean bracket: `=== Battle START (EWM=ON/OFF) ===` at enter, per-turn detection lines during, `=== Battle END === / Party: ... / Enemies: ... / Ratio ...` at exit. Works whether EWM is on or off. Aaron plans to keep an eye on the ratios during ongoing play.

### Active Systems

| Subsystem | Status | Notes |
|---|---|---|
| ATB freeze sandwich (HookedATBUpdate) | ✅ | POST-FREEZE restores to exact pre-sandwich value |
| Dispatch hook: `sub_483470` | ✅ | Blocks dispatch during damageOrActionActive or activeChar<3 |
| Dispatch hook: `sub_482F80` | ✅ | Same block condition; execution-side pair |
| Post-turn grace (1000 ms) | ✅ | Bridges player-action-end → next-turn race |
| Post-action cooldown (500 ms) | ✅ | Bridges signal-clear → mod-thread-poll race |
| Turn-count diagnostic | ✅ | EWM-independent; logs per-battle summary |
| Damage-anim transition diagnostic | ✅ | `[DMG-DIAG]`, `[FRZ-DIAG]`, `[POST-REL]` log tags |

### Next Session Priorities (Session 77)

Aaron has picked four targets for session 77:

1. **GitHub sync** — commit & push all v0.13.57–0.13.60 changes (sessions 75–76) to `ampage87/FFVIII-Accessibility-Mod` main via `github:push_files` (multi-file single-commit) and `filesystem:read_text_file` for each source.
2. **Battle status-ailment detection & announcement** — (a) detect & announce when a status is applied to any party member or enemy; (b) append current statuses to the HP readout on `1`/`2`/`3` and when an enemy is targeted.
3. **GF summon audio descriptions** — mirror the FMV audio-description pipeline (v03.00) for each GF summon animation. Use `s_gfAnimFired` as the trigger gate.
4. **Scan spell formatted output** — intercept the Scan display, parse the enemy data (Level, HP, elemental affinities, status affinities, note), and speak a clean structured readout instead of the current raw text dump.

Deferred from prior priority queue (pick up after session 77): persistent settings, GF naming bypass, party-member NPC filter, X-ATMO92 chase, per-character GF state, GF-acquired timing race, SFX volume split, greyed menu-item TTS, GitHub issues #6/#7.

EWM subsystem is stable and not on the table unless a regression surfaces.

### Critical Addresses (unchanged)

| Address | Purpose |
|---------|---------|
| 0x00483470 | `sub_483470` — turn dispatch (hooked) |
| 0x00482F80 | `sub_482F80` — action execute (hooked) |
| 0x00483EB0 | `sub_483EB0` — entity-ATB-topped-out handler (not hooked) |
| 0x004842B0 | ATB update function (hooked, freeze sandwich) |
| 0x01D27B00 | engine "action in progress" flag |
| 0x01D280C0 | BATTLE_DAMAGE_ANIM_FLAG (byte) |
| 0x01D76844 | activeChar (0–2 = player slot, 0xFF = no one) |
| 0x01D768D0 | menuPhase dword (dual-purpose: phase int OR function ptr) |

### Key Invariants (do not break)

1. **NEVER re-enable the SET3 opcode hook (opcode 0x1E)** — hangs infirmary scene. CI check in `.github/workflows/safety-checks.yml`.
2. **Victory TTS must hook the text renderer, not read memory** — memory-scan dumps everything at once; blind player presses through unannounced screens.
3. **Submenu detection on confirm, not highlight** — task pool scan and `s_pendingSubmenuEntry` fire on cursor highlight (rejected approaches).
4. **Savemap header is 76 bytes (0x4C)**, not 96 — ChatGPT deep research assumes 96. Subtract 0x14 from any research offsets.
5. **`EWM_TrackTurnCount` must stay EWM-independent** — detector watches ATB high→low transitions directly, works regardless of freeze state.

### Previous Sessions

<details>
<summary>Sessions 75–76 (v0.13.57→v0.13.60) — Cap→Freeze + Turn-Counter Diagnostic</summary>

Full details archived in DEVNOTES_HISTORY.md. Summary: switched ATB cap-at-max-1 sandwich to true freeze (v0.13.57) which eliminated the TTS-damage-overlap bug. Added per-slot turn counter (v0.13.58) initially coupled to EWM_UpdateBattle; split into lifecycle-hooked functions (v0.13.59) so reset/summary fire reliably on every battle. Fixed format string OOB read (v0.13.60). A/B test: EWM has no measurable effect on turn economy (2.25:1 vs 2.33:1 across 10 battles).
</details>

<details>
<summary>Sessions 69–74 (v0.13.51→v0.13.56) — Damage/command-menu overlap investigation</summary>

Full transcripts at `/mnt/transcripts/` (journal.txt for catalog). Progressive narrowing through 6 sessions ended with dual MinHook on `sub_483470` + `sub_482F80`. Session 75's cap→freeze change turned out to be what actually fixed the visible-overlap symptom; the dispatch hooks remain as defense-in-depth.
</details>

<details>
<summary>Session 67 (v0.13.49→v0.13.50) — GF Submenu Fix + Code Cleanup + Draw Fix</summary>

Fixed 100% of GF submenu detection misses by removing `wasCommandMenu` requirement (root cause: `0x1D768EB` is a party slot index, not a flag). Consolidated 4 detection paths into single `EnterSubmenu()` helper. Suppressed Draw-specific false submenu exits.
</details>


---
