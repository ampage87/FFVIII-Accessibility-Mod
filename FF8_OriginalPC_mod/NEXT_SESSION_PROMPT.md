# NEXT SESSION PROMPT — v0.13.45

## Source File Split: COMPLETE (session 63)

battle_tts.cpp split from ~100KB into:
- battle_tts.cpp (~26KB): core framework, entity helpers, enter/exit, init/update/shutdown
- battle_tts_screenshot.inl (~19KB): GL screenshot, memory diff, victory step diagnostics
- battle_tts_victory.inl (~67KB): all 8 BT hooks, phase state machine, GF/ability tables, victory thread

Include order: helpers → diagnostics → hp → ewm → menu → screenshot → victory

No logic changes — purely mechanical extraction. Build verified v0.13.45.

---

## Priorities for next session

1. **Persistent accessibility settings** — SAPI voice, speech rate, music volume, and other player preferences should persist across play sessions. Currently only the EWM toggle persists. Need to save/load settings from a config file in the game directory.

2. **Verify character naming bypass** — Siren GF did not auto-bypass as expected. Investigate why the naming screen bypass isn't working for GF names (may be a regression from the recent refactoring).

3. **Remove party members from entity catalog** — The field entity catalog currently includes 2 NPCs that are the player's own party members following them. These aren't interactable and clutter the catalog. Filter them out.

4. **X-ATMO92 chase scene accessibility** — Develop a way to make this timed chase sequence accessible. Proposed approach: ensure X-ATMO92 doesn't resume movement after a random encounter until the player enters a new field screen, giving blind players time to navigate without the visual pressure of the chase.

---

## Remaining open items (lower priority)

- INF gateway destination direction accuracy
- bghall_1 catalog regression (noted session 48)
- Interaction zone naming refinements
- Submenu mode 0x00 ambiguity

---

## Build/test workflow
1. Edit with filesystem MCP tools (not bash)
2. Aaron runs deploy.vbs
3. "BAT" = read build_latest.log tail, then domain log
4. Version in 1 place: FF8OPC_VERSION in src/ff8_accessibility.h
