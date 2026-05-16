# Next Session Prompt: v0.16.0.3 BAT confirm + v0.16.1 chase_auto_pilot.cpp split

## Greeting

Start with `## Claude Says` per session ritual. Read `DEVNOTES.md` and THIS file before any work.

## Where we are

**Local tree = v0.16.0.3** (single-file log-spam cleanup on top of v0.16.0.2 BAT pass). `world_map_segments.inl` now logs `[VEH-POS-FALLBACK]` only when `s_lastVehicle` transitions to a value the function hasn't logged before in this session. No time-based heartbeat — once logged, that vehicle byte stays silent. No functional change — just diagnostic noise reduction. Aaron should BAT briefly (any world-map session) to confirm the log no longer floods, then push the v0.16.0/.0.1/.0.2/.0.3 chain.

**GitHub HEAD = v0.15.13.2** (commit `3d6db2a`). The push utility expects `FF8OPC_VERSION = "0.16.0.3"` matches the top `## v0.16.0.3` CHANGELOG heading — both are aligned.

## v0.16.0.3 BAT plan (very short)

1. Build, restart FF8.
2. Spend ~1–2 minutes on the world map (foot or any vehicle works — the spam scenario was specifically `s_lastVehicle` latched to a non-foot value with vehicle savemap reading (0,0)).
3. Open `Logs/ff8_world.log` and confirm `[VEH-POS-FALLBACK]` appears at most a couple of times — one line per distinct `s_lastVehicle` value that hits the fallback, typically 0–3 total — instead of the per-poll flood that produced ~1800 lines in v0.16.0.2 BAT.
4. If the count looks reasonable, push v0.16.0/.0.1/.0.2/.0.3 sequence.

No TTS or AD behavior should differ from v0.16.0.2.

## v0.16.1 — split src/chase_auto_pilot.cpp (108 KB)

The world_map split is the template. The chase_auto_pilot.cpp file holds the X-ATM092 chase auto-pilot logic — the .inl-include pattern from v0.16.0 should apply directly.

**Suggested file structure** (subject to revision once we look at the actual file head + tail):

| Candidate `.inl` | Approximate scope |
|---|---|
| `chase_auto_pilot_state.inl` | All module statics (route state, phase tracker, ASK overlay flag, BAT diagnostic counters) |
| `chase_auto_pilot_route.inl` | Route table data and route lookup helpers |
| `chase_auto_pilot_engine.inl` | Per-frame Update logic that decides direction + injects DIJOYSTATE2 analog |
| `chase_auto_pilot_ask.inl` | chase_ask_overlay integration (committed-Auto handoff, deferred-open delay) |
| `chase_auto_pilot_diag.inl` | F12 diagnostic + per-second position logging |
| `chase_auto_pilot.cpp` (slim) | Header + namespace + `.inl` chain + public entry points |

**Workflow** (mirroring v0.16.0):

1. **Read the existing file head and tail first** to understand actual structure. Don't pick a split until reading the file.
2. **Create the .inl files** one at a time, copying the relevant sections out of `chase_auto_pilot.cpp`.
3. **state.inl always first** in include order. Any .inl that calls a function from another .inl must come AFTER the .inl that defines the function (compiler needs the definition visible in the same translation unit; forward decls won't help inside .inl includes).
4. **Slim parent `chase_auto_pilot.cpp`**: headers, namespace forward decls, `.inl` chain in dependency order, public entry points (`Initialize`, `Update`, `Shutdown` or whatever the chase module exposes).
5. **CI guard update**: remove `chase_auto_pilot.cpp` from the allowlist in `.github/workflows/safety-checks.yml`. After the split, no .inl should exceed 60 KB.
6. **Verify the v0.16.0 build-order rule still applies**: any .inl whose functions are called by a later .inl must come earlier in the include chain.
7. **Bump FF8OPC_VERSION** to `0.16.1`, prepend CHANGELOG.md entry, refresh DEVNOTES and NEXT_SESSION_PROMPT.

**BAT plan for v0.16.1**: Aaron loads a Dollet save just before/during the X-ATM092 chase. Verifies the chase auto-pilot still works (drives the party, handles the ASK overlay, returns control after the chase). No functional changes — pure refactor.

**Failure modes to watch:**
- Build error: read `Logs/build_latest.log` tail first. Most likely candidates are include-order mistakes (state.inl-first rule violated) or missing forward decls in the slim parent.
- Runtime regression in chase auto-pilot: probably a stale static-state reference. Compare before/after by `grep`ping for variable usage across the .inl files.
- OneDrive sync `EPERM` on first edit attempt: retry immediately.

## After v0.16.1 lands

Remaining size-split queue (each one removes an entry from the CI allowlist):
- **v0.16.2**: split `src/field_dialog.cpp` (88 KB).
- **v0.16.3**: split `src/field_archive_jsm.inl` (91 KB).
- **v0.16.4**: split `src/battle_tts_ewm.inl` (90 KB).
- **v0.16.5**: split `src/battle_tts_menu.inl` (82 KB).

Once all five v0.16.x splits are done, the CI guard becomes a meaningful gatekeeper (any new oversized file fails the build) rather than a tripwire for legacy files.

## Hard constraints (unchanged)

- **Filesystem MCP for all Windows project files.** Bash runs in a Linux container that can't reach the OneDrive mod directory. `str_replace`, `view`, `create_file` etc. from the bash toolset will silently fail or report "File not found" — use `filesystem:edit_file`, `filesystem:read_text_file`, `filesystem:write_file`.
- **Aaron pushes via `Utilities/push_to_github.ps1`**, Claude NEVER pushes. Push utility reads CHANGELOG top entry as commit body and version from `FF8OPC_VERSION`.
- **NEVER re-enable SET3 opcode hook (0x1E)** — CI guard.
- **F-key handlers gated** on `!(GetAsyncKeyState(VK_MENU) & 0x8000)`.
- **F12 reserved** for per-session diagnostics.
- **Source file size limits**: 60 KB warn, 80 KB fail (CI enforced). Split before substantive edits cross the warning line.
- **OneDrive sync EPERM**: retry immediately on first edit attempt. Sometimes a "File not found" error from `filesystem:edit_file` is actually a successful write masked by a stale-cache read — re-read the file before assuming the edit failed.
- **AUTO `[CBF]` battle-suppressor cap stays `INT_MAX`** — Aaron's 2026-05-13 directive.
- **`.inl` files are TEXTUAL INCLUDES**: no header guards inside, no namespace declarations inside, all `static` declarations preserved, `state.inl` always first in include chain.
- Every Claude response starts with `## Claude Says`.

## Quick reference — what to check first when this session opens

- [ ] Read `DEVNOTES.md` (current state + backlog sections).
- [ ] Read this `NEXT_SESSION_PROMPT.md` (you're already here).
- [ ] If Aaron is starting v0.16.1: read `src/chase_auto_pilot.cpp` head (first 200 lines) and tail (last 200 lines) to understand actual structure before proposing a split.
- [ ] If Aaron is doing more BAT runs first (e.g. exercising the planner-ineligible branch of Poll() replan-gate): wait for logs.
- [ ] If build failed: `Logs/build_latest.log` tail first.

## Open follow-ups from v0.16.0.2 BAT (lower priority, not blockers)

1. **VEH-POS-FALLBACK log spam**: ~1800 fallback lines per 7-minute session because `s_lastVehicle` latched to 33 (VEH_CAR) mid-session. Functionally correct but diagnostically noisy. Backlog item: rate-limit log line (every 10s or transition-only) AND/OR auto-snap `s_lastVehicle` back to 0 when foot DWORDs are valid and moving.
2. **`fieldName=''` race** on fast arrivals (Fire Cavern at dist=66, 547ms). Field-name pointer hasn't populated by the time Part B reads it. Either retry briefly or accept (fieldId is sufficient). Backlog.
3. **Fire Cavern A fieldId 0x0088** is new data for the FieldAnnounce display-name catalog audit backlog item. Confirm display name reads correctly.
