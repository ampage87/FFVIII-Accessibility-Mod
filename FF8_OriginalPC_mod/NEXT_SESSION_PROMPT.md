# NEXT SESSION PROMPT — v0.13.45

## Source File Split: COMPLETE (session 63)

battle_tts.cpp split from ~100KB into:
- battle_tts.cpp (~26KB): core framework, entity helpers, enter/exit, init/update/shutdown
- battle_tts_screenshot.inl (~19KB): GL screenshot, memory diff, victory step diagnostics
- battle_tts_victory.inl (~67KB): all 8 BT hooks, phase state machine, GF/ability tables, victory thread

Include order: helpers → diagnostics → hp → ewm → menu → screenshot → victory

No logic changes — purely mechanical extraction. Build verified v0.13.45.

---

## Remaining open items

1. **INF gateway destination direction accuracy** — PS1-era vestigial data, destinations unreliable on many PC fields
2. **bghall_1 catalog regression** — noted session 48, not yet investigated
3. **Interaction zone naming refinements** — polish naming of interactive objects
4. **Submenu mode 0x00 ambiguity** — mode byte 0x00 can mean "not in submenu" or "magic submenu"

---

## Build/test workflow
1. Edit with filesystem MCP tools (not bash)
2. Aaron runs deploy.vbs
3. "BAT" = read build_latest.log tail, then domain log
4. Version in 1 place: FF8OPC_VERSION in src/ff8_accessibility.h
