# Next Session Prompt: chapter selection / push decision

## Greeting

Start with `## Claude Says`. Read `DEVNOTES.md` and THIS file before any work.

## Where we are at session open

**v0.17.7.5.5 BAT'd clean.** The exit/interaction labeling chapter is feature-complete. Aaron confirmed the bgryo1_4 bed is now announced as an Interaction (no longer mislabeled as "Exit to B-Garden - Dormitory Double 4").

`FF8OPC_VERSION` = `0.17.7.5.5`. `CHANGELOG.md` top heading matches. GitHub HEAD is still v0.17.7.0 (`8b9299c2`); 11 build chapters of unpublished work locally.

## Two decisions pending Aaron's direction

### Decision 1: push the cumulative v0.17.7.1 → v0.17.7.5.5 series?

The work is push-ready as a coherent batch. Suggested commit-body framing:

> Catalog overhaul: walkmesh exclusion rule, MAPJUMP destField static resolver with addr-as-literal pattern for B-Garden hall+road grid, hasDialogReqTarget split for genuinely dual-purpose Lines, self-loop MAPJUMP detection for sleep transitions.

Aaron pushes via `Utilities/push_to_github.ps1`. Claude doesn't push. If Aaron says "push" he runs the utility; if he wants Claude to verify push readiness first, sanity-check that `FF8OPC_VERSION` matches `CHANGELOG.md`'s top `## v...` heading.

### Decision 2: which chapter next?

Three candidates documented in `DEVNOTES.md`. In likely-priority order:

1. **v0.17.7.6 — camera projection closed-loop calibration.**
   The bgroad_5 auto-drive direction confusion (degenerate .ca file, identity fallback wrong by 90 degrees). Aaron experienced this twice already in the .5.3 and .5.4 BATs. NAV-OBSERVE already captures the empirical truth (`measured=(1.000,0.000) predicted=(-0.000,1.000) DIVERGE=90deg`); we just need to feed that back into camRight/camDown when CA-derived 2D projection is degenerate. Substantial chapter — start with a design pass before coding. Likely touches: projection init in `field_nav_fieldscripts.inl`, drive direction injection (the `[drive-vec]` system), and possibly GPS cardinal computation. May also improve manual nav cardinal accuracy on other tilted-camera fields.

2. **v0.17.7.7 — SETLINE-position promotion + NPC ResolveFriendlyName.**
   Auto-drive needs accurate target positions when navigating to the new SCREEN_BOUND exits. The catalog already records SETLINE centers but the auto-drive may want a different anchor (e.g. the closest walkmesh point inside the line segment, not the midpoint). NPC `ResolveFriendlyName` is a separate workstream — replaces generic "NPC" labels with sym-based friendly names where possible.

3. **v0.17.7.8 — Shop/Card Game → NPC announce-layer collapse.**
   When a shop or card-game NPC exists, the catalog currently exposes both an NPC entry and the shop/game entry. Collapse to one.

## How to start each chapter

- **v0.17.7.6** — start with a written design proposal: how to detect degenerate CA + what to do when NAV-OBSERVE samples accumulate + safety guard rails (single-arrow-only, no recent direction changes, sanity-check vs walkmesh). Aaron signs off before code. Then implementation, then BAT on bgroad_5 specifically (the field that exposed the issue) plus a sanity pass on a known-good field like bghall_1 to confirm no regression.

- **v0.17.7.7 / .8** — both are small enough to propose directly. Skip the design pass; describe the change and proceed if Aaron approves.

## If Aaron opens with "BAT" anyway

A "BAT" without a preceding build change means Aaron tested something old. Ask which build is in his game directory (he can press `V` in-game to hear the version). If it's still v0.17.7.5.5, his BAT either:
- Surfaced something he forgot to report originally (re-read his message and probe), or
- He's BAT'ing his own changes outside Claude (unlikely but possible).

If it's older, the cumulative batch hasn't deployed yet. Walk him through what's pending.

## File-access reminder

**Mod files are on Windows.** Use `filesystem:`-prefixed MCP tools for paths under `C:/Users/ampag/...`. BAT logs are at `Logs/ff8_field.log` (Windows, reliable) or may appear as `/mnt/user-data/uploads/*ff8_field.log` (Linux container, requires `bash_tool` which may not always be available).

For mid-file targeted searches in large logs, use `filesystem:edit_file` with `dryRun=true` and a unique `oldText` anchor — the diff shows ~3 lines context on either side. Useful for grep-equivalent search when bash isn't available.

DO NOT call bare `create_file`, `str_replace`, `view`, or `bash_tool` for Windows paths — those operate on the Linux container's filesystem.

## Session checkpoint rule reminder

If a chapter starts this session: at every version bump and after every BAT, update `DEVNOTES.md` and rewrite this file. If only a push happens: update DEVNOTES to reflect new GitHub HEAD and the cumulative chapter set landed; rewrite this file to point at v0.17.7.6 (or whichever chapter Aaron picks).
