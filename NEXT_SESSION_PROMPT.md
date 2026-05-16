# Next Session Prompt: v0.15.11.0 push, then pick next backlog item

## Where we are

**Local tree = v0.15.11.0, BAT-confirmed 2026-05-15.** GitHub HEAD = v0.15.10.2 (commit `4ef5b61a`). The text-decoder unification work is complete and ready for Aaron to push via `Utilities/push_to_github.vbs`.

Aaron's BAT report: "Fought several battles and didn't notice any problems with how item drops, enemy names, and so forth were announced." Single full battle in `ff8_battle.log` 19:00:42-19:00:58 exercises every previously-suspect code path — item TTS, GF level-up, ability-learn, EXP Phase 1/2, glyph cache — and all decode cleanly through canonical `FF8TextDecode::Decode`. Smoking-gun line for the migration: `[ABILITY-NAME-HOOK] sub_47E710: a1=85(0x55) -> "SumMag+30%" hex=[57 73 6B 51 5F 65 31 24 21 2B]`. The hex stream contains 0x24 — preview would have mapped that to `'0'`; canonical correctly produces `"SumMag+30%"`. Full BAT evidence trail is in `DEVNOTES.md`.

## What Aaron does first

Run `Utilities/push_to_github.vbs`. The push utility reads `FF8OPC_VERSION` from `src/ff8_accessibility.h` (currently `0.15.11.0`) and the top entry of `CHANGELOG.md` (the v0.15.11.0 block), tags the commit, and pushes to `ampage87/FFVIII-Accessibility-Mod`. Claude NEVER pushes.

## After push: pick the next backlog item

The post-chase backlog is down to two items, both reasonable picks for the next session:

### Pick A: Generalized countdown-timer hook

The Dollet 30-min mission countdown is currently TTS'd via a chase-specific path. Generalize so future timed events (e.g. the chase, future scripted sequences) can register a countdown callback and get standardized announcements. Likely small-to-medium scope — touches the chase countdown plumbing plus probably a new module or section in `chase_detector.cpp` or `field_dialog.cpp`. Worth doing before the next timed-event scene appears so we don't have to invent the abstraction under pressure.

### Pick B: Remove party members from field entity catalog

Squall / Zell / Selphie currently appear as targetable entities in the field nav catalog (F9 path-finding target list and the `field_nav_catalog.inl` enumeration). Filter them out so the catalog only shows interactable / unique entities. Smaller scope — likely a single predicate in `field_nav_catalog.inl` or `field_nav_names.inl`, guarded against the case where a party member happens to *be* the interaction target (e.g. a script that triggers when you talk to a specific character).

### Deferred (don't pick without explicit Aaron direction)

- SeeD rank bug #27 (hypothesis: `FIELD_H_OFFSET = 0xF94` is wrong section size)
- Walk-and-talk dialog gap (hardcoded engine path)
- Refined-coord narrow-gate steering (#29)
- Fire Cavern entry (#28) + planner-fallback
- chase_diag::OnAskOpcodeFired snprintf bug
- `CHASE-AGENT FINAL SUMMARY` log regression (fix in DeactivateFreeze before clearing agent state)

## After the next pick is committed

Refresh `DEVNOTES.md` and `NEXT_SESSION_PROMPT.md` to reflect the new HEAD commit SHA from the push, plan and code the next backlog item, BAT, push.

## Hard constraints (unchanged)

- **Do NOT revert the AUTO `[CBF]` battle-suppressor cap to 0.** Aaron's 2026-05-13 directive stands. v0.15.9.11.3.6 BAT vindicated the input-layer fix.
- **Filesystem MCP for all Windows project files.** Bash runs in a Linux container that can't reach the OneDrive mod directory.
- **Aaron pushes via `Utilities/push_to_github.vbs`.** Claude NEVER pushes.
- **NEVER re-enable SET3 opcode hook (0x1E)** — CI guard in `.github/workflows/safety-checks.yml`.
- **F-key handlers gated** on `!(GetAsyncKeyState(VK_MENU) & 0x8000)` to prevent Alt+Fx interception.
- **F12 reserved** for per-session diagnostics — search source for existing F12 refs and REMOVE old code before re-binding.

## Session ritual reminder

Read `DEVNOTES.md` and this file at session start, before any work. Update both at every version bump AND after every BAT result. Every Claude response starts with `## Claude Says`.
