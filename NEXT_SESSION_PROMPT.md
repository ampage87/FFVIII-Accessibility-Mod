# Next Session Prompt — FF8 Accessibility Mod

## Current state at session start

Build: **v0.14.42 — BAT SUCCESS ✅** (15:26:14, session 72).

Aaron confirmed: "It worked! I heard all items in the slots as expected, and those items I used the item used matched what had been announced by TTS."

Bug A (items submenu ordering) is **resolved** after a 9-version journey from v0.14.34 through v0.14.42. Disassembly model is verified correct: in-battle items live in a 32-entry buffer at `0x1D28E78`, stride 5, cursor at `0x01D768EC` indexes directly. Arrangement at `0x1CFE77C` is id-indexed (`arrangement[id-1]` = position).

Verification log excerpt:

```
[ITEM-LIST] battle_buffer @ 0x01D28E78: 4 populated of 32 positions
  [0] id=1 qty=10 -> Potion
  [1] id=7 qty=1  -> Phoenix Down
  [2] id=16 qty=2 -> Remedy
  [9] id=9 qty=2  -> Elixir

[ITEM] cursor=9 -> Elixir x2 page3 item2 (id=9 src=battle_buffer)
```

## Top priority — v0.14.43 diagnostic cleanup

Now that v0.14.42 is verified, strip the diagnostic instrumentation:

1. **`src/battle_tts_menu.inl`**: Remove the `[ITEM-DUMP]` block (`DumpItemMenuState` function and its call from `BuildItemList`). The `[ITEM-LIST]` block stays — low overhead, one shot per submenu open, useful for regression checks.

2. **`src/battle_tts.cpp`**: Remove the `[BATTLESPEAK-DIAG]` instrumentation. (Was added during the items audio-purge investigation; no longer needed.)

3. **`src/screen_reader.cpp`**: Remove the `[SPEAK-DIAG]` instrumentation. (Same provenance.)

4. **F12** remains free for the next diagnostic build (no on-key hook currently bound in battle).

5. **Cancel** the deep research prompt that's still queued at `Plan & Research Documents/deep_research_battle_items_arrangement.md` if it hasn't completed — it's no longer needed. Aaron mentioned letting it finish as insurance, so coordinate with him on whether to delete the file or keep the output for archival reference.

After v0.14.43 is built, BAT to confirm nothing broke (items submenu still works, no missing log lines we still rely on).

## Backlog — work the user-facing list once cleanup is done

1. **Persistent accessibility settings** across play sessions (top user-facing priority — voice, speech vol, speech rate, EWM toggle, etc. should survive game restarts).
2. **Verify GF naming bypass** — Siren failed in earlier testing.
3. **Remove party members from entity catalog** in field navigation.
4. **X-ATMO92 chase scene accessibility**.
5. Bug 3 (Magic/GF auto-announce inconsistent).
6. Bug 4 (key 2 announced GF Shiva instead of Squall HP — stale `gfHpSubstitutionActive[1]` / `gfSummonedIdx[1]`).
7. Quistis Blue Magic ordering, Draw "???" reveal, independent SFX volume.
8. World map: vehicle-aware BFS, guided GPS mode, auto-announce location names via `world_dialog_assign_text_sub_543790`.

## GitHub push

~50+ builds remain unpushed since `v0.13.63` HEAD. v0.14.42 is a natural milestone — Aaron may want to push a single comprehensive commit covering build recovery → production trigger → sprite/spell hooks → item-submenu false-exit fix → items audio fix → items ordering rework (the v0.14.42 disassembly-driven solution) → damage timing fix.

## Required reading at session start

1. `DEVNOTES.md` (project root)
2. This file
3. `Logs/build_latest.log` tail — confirm any new build is clean
4. Domain log relevant to current task (`ff8_battle.log`, `ff8_field.log`, etc.)

## Workflow rules in effect

- **Filesystem MCP tools only** — never bash for project files
- **Update DEVNOTES + NEXT_SESSION_PROMPT** at every version bump and BAT
- **"BAT" = Built and Tested.** Check `Logs/build_latest.log` tail first, then domain log
- **Version bump in 1 location** — `FF8OPC_VERSION` in `src/ff8_accessibility.h`
- **Aaron is blind** — every response starts with `## Claude Says`
- **Don't declare a fix successful from log markers alone** — verify against Aaron's user-facing experience
- **NEVER re-enable the SET3 opcode hook (0x1E)** — hangs infirmary scene. CI guard active
- **F12 reserved for diagnostics** — search/remove old VK_F12 refs before adding new

## Memory addresses catalog (current as of v0.14.42)

- `BATTLE_CMD_CURSOR = 0x01D76843`
- `BATTLE_MENU_PHASE = 0x01D768D0` (dword: small phase OR function pointer when submenu open)
- `BATTLE_SUBMENU_CURSOR = 0x01D768EC`
- `BATTLE_ITEM_BUFFER_ADDR = 0x1D28E78` (32 × 5 bytes — **the truth source for in-battle items**)
- `BATTLE_ITEM_ARRANGE_ADDR = 0x1CFE77C` (32 bytes id-indexed: `arrangement[id-1]` = position)
- `ITEM_INVENTORY_ADDR = 0x1CFE79C` (198 × {id, qty}, the full inventory)
- Char struct base 0x1CFE0E8 stride 0x98; magic at +0x10; equip cmds at +0x50; junctioned GF mask at +0x58
- Savemap header is 76 bytes (0x4C); savemap base = 0x1CFE09C
