# Sprite Render Path Deep Research — v2 Results

**Filed:** 2026-04-19 (Session 80 start)
**Corresponds to prompt:** `2026-04-18_sprite_render_path_research_prompt_v2.md`
**v1 results (tangential, FFNx effects.h):** `2026-04-18_sprite_render_path_research_results.md`

---

## Integration header (Claude, Session 80)

This is the v2 research pass. Key facts for interpreting it:

1. **Research predates Session 79's empirical findings.** It was generated before we disassembled `0x01D280C0`'s actual writers. It explicitly states "No public source names a writer of 0x01D280C0" — we know four now: `sub_48D1F0` (reset), two sites inside `sub_48E830`, and one inside `sub_48EE20`.

2. **Self-reported LOW confidence.** The v2 document admits its three candidates are triangulated from FFNx's externals graph plus address-locality reasoning, not from any source that actually names a sprite-spawn function.

3. **Candidates operate at a higher pipeline layer than the count-writers.** `sub_4AB450`, `sub_48D1A0`, and `sub_495100` are likely text/name formatters and higher-level dispatchers that *may* eventually call into `sub_48E830` / `sub_48EE20`. The `battle_get_monster_name` anchor in v2's primary candidate suggests these are action-announce-style paths (which embed monster names) rather than the floating damage-number / Miss sprite path (which doesn't need monster names).

4. **None of v2's candidates appear in `FF8_EN_callxrefs.txt`.** That file has a threshold of 3+ callers; v2's candidates all have fewer. Consistent with being narrow-purpose dispatchers on specific code paths.

5. **Session 80 hook code will recognize v2's candidate ranges in retaddr capture.** If `sub_48E830` or `sub_48EE20` fires with a retaddr falling in `[0x004AB450, 0x004AB600]` or `[0x0048D1A0, 0x0048D200]`, v2's chain is validated and we have a higher-level hook point with richer args. If retaddrs fall elsewhere, v2's chain is wrong and Session 79's count-writer level is the right hook level.

6. **v2's "Suggested BAT #1" (hardware write breakpoint via x64dbg) is not blind-accessible.** Its "BAT #2" (MinHook trampolines on `sub_4AB450` + `sub_48D1A0`) has been superseded by Session 79's more targeted hook plan on the actual count-writers. Its "BAT #3" (hook `sub_495100` and log retaddrs) remains a useful fallback if Session 80's dual-hook comes back inconclusive.

**Session 80 action:** Proceed with dual-hook of `sub_48E830` + `sub_48EE20` from `NEXT_SESSION_PROMPT.md`, with retaddr-range annotation logic added to recognize v2's candidate windows.

---

## Verbatim v2 research output

ANSWER: 0x004AB450 — battle result/message initializer reached via sub_47CCB0+0xA5F; immediately calls battle_get_monster_name_sub_495100 (which reads the battler struct at 0x01D27B10, the same region as the 0x01D280C0 flag), matching the signature of a floating result-text spawner.

Confidence: LOW (no public source names the sprite-spawn function or a writer of 0x01D280C0; candidates are triangulated from FFNx's `ff8_data.cpp` externals graph plus the empirical 0x01D27xxx/0x01D28xxx battle-state locality).

Candidates (ranked):
1. 0x004AB450 — `ff8_externals.sub_4AB450`; resolved as `get_relative_call(sub_47CCB0, 0xA5F)`; at +0x40 it calls `battle_get_monster_name_sub_495100` which reads `battle_char_struct_dword_1D27B10` (a byte-offset sibling of the 0x01D280C0 flag) — source: https://github.com/julianxhokaxhiu/FFNx/blob/master/src/ff8_data.cpp
2. 0x00495100 — `ff8_externals.battle_get_monster_name_sub_495100`; callee of candidate #1; the `mov` at +0xF reads `[0x01D27B10]`, the exact battler-struct region the flag lives in — source: https://github.com/julianxhokaxhiu/FFNx/blob/master/src/ff8_data.cpp
3. 0x0048D1A0 — `ff8_externals.sub_48D1A0` (explicitly named in FFNx), the direct sibling of the ruled-out `sub_48D200` (action-announce). At +0x44 it calls `sub_4AD7D0`; by elimination this is the most likely "result popup" twin of the action-announce dispatcher — source: https://github.com/julianxhokaxhiu/FFNx/blob/master/src/ff8_data.cpp

Supporting evidence:
- FFNx resolves `ff8_externals.sub_4AB450 = get_relative_call(ff8_externals.sub_47CCB0, 0xA5F)` and immediately `ff8_externals.battle_get_monster_name_sub_495100 = get_relative_call(ff8_externals.sub_4AB450, 0x40)` — a formatter → monster-name-fetch chain typical of result-popup construction (ff8_data.cpp, master).
- `battle_char_struct_dword_1D27B10 = (BYTE**)get_absolute_value(battle_get_monster_name_sub_495100, 0xF)` — confirms sub_495100 indexes battle state at 0x01D27B10, 0x9B0 bytes before the flag at 0x01D280C0, i.e. inside the same battle-engine struct region.
- Battler array base 0x01D27B8C and flag 0x01D280C0 lie within ~0x534 bytes of 0x01D27B10; functions operating on 0x01D27Bxx almost always also touch 0x01D280xx.
- FFNx does NOT name any function with "popup"/"damage number"/"floating text" semantics for FF8 in any public file; all FF8 externals lists I inspected are limited to battle_enter / battle_main_loop / swirl / sm_battle_sound / monster_name / active_character_id / Leviathan opcode table and menu/dialog code.
- No Qhimm Hext patch, MaKiPL repo, DLPB/Hyne doc, or FFRTT wiki page publicly names a writer of 0x01D280C0 (verified by targeted searches for "1D280C0", "D280C0", and "battle popup"/"damage sprite" function addresses).
- Qhimm threads 18374 ("FF8 EXE — Debug/Empty Section & Battle Results section") and 16838 ("FF8 Engine reverse engineering") are the highest-value un-scraped sources but returned 403 to automated fetch; worth a manual read.

Writers of 0x01D280C0 (if found):
- NOT FOUND in any public source. Neither FFNx `ff8_data.cpp`, Qhimm-indexed posts, MaKiPL's OpenVIII / FF8_demaster / FF8-Rinoa-s-Toolset, DLPB2/Hyne, FFRTT wiki, nor any Hext patch corpus (Roses & Wine, SeeD Reborn, Tripod, Ragnarok, Lunar Cry, Rebirth Flame, Apocalypse) contains the string `1D280C0` or a named "damage popup spawner". This has to be confirmed via IDA/x64dbg memory-write breakpoint on the live process.

Open questions / what couldn't verify:
- Whether 0x01D280C0 is written by a standalone function or inline within a larger routine like `battle_main_loop` (0x47CCB0 region, per FFNx) — a write-breakpoint will distinguish these.
- Whether the floating result text is spawned by a dedicated function or is an opcode handler inside a separate VM (distinct from the ruled-out 0x48A0B8 table and the 0xB64C3C Leviathan table). No third battle VM table is publicly documented. If it exists, it would be reached via the battle-effect dispatch at 0xC81774 for non-magic effects — but that table was stated in the brief as reference only.
- sub_48D1A0 vs sub_48D200 — FFNx names both as externals but gives no semantic label for sub_48D1A0; the "sibling of ruled-out" argument is circumstantial.

Suggested BAT to disambiguate (do this first — it's definitive):
1. Set a hardware write-on-access breakpoint (DR0/DR1) on byte `0x01D280C0` in x64dbg or via VirtualProtectEx + VEH. Cast Fire on one enemy. On the first break, read EIP; step out to the function prologue (`push ebp; mov ebp, esp`). That address IS the answer. Expected range: the 0x004Axxxx–0x004Bxxxx band per the FFNx chain.
2. As a secondary BAT: place MinHook trampolines on 0x004AB450 and 0x0048D1A0 that log `GetTickCount`, the current value of `*(BYTE*)0x01D280C0`, and `_ReturnAddress()` on entry. Trigger one Miss and one hit. The function that fires synchronously with the 0→1 transition (same frame/tick) — and shows the flag as 0 on entry, 1 on exit — is the spawner. If neither fires at the right moment, the true target is a callee; inspect its CALL sites with IDA's xrefs.
3. Cross-check: hook 0x00495100 (battle_get_monster_name) and log retaddr on entry. Any retaddr that is NOT the already-known callers (from the monster-name UI resolver chain) AND fires in-window with a "Miss"/damage sprite is the floating-text formatter. That callsite's containing function is the sprite spawner.

Note on constraint compliance: per the brief, the ruled-out set (0x004877F0, 0x00487DF0, 0x0048D200, 0x0047EC70, 0x00483400) has been excluded; the three candidates above are NOT in that set and are NOT FFNx abstractions — they are the native FF8_EN.exe addresses the `ff8_externals.sub_*` names resolve to via the published `get_relative_call` / `get_absolute_value` chains in `src/ff8_data.cpp`. The "answer" field is provided under the explicit understanding that public reverse-engineering literature does not label this specific function; the user's write-breakpoint test above is the only authoritative disambiguator.
