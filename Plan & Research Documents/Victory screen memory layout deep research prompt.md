# Deep Research Prompt: FF8 Post-Battle Victory Screen Memory Layout

## Context

I'm building a DLL injection mod (dinput8.dll) that makes **Final Fantasy VIII (Steam 2013, FF8_EN.exe + FFNx v1.23.x)** accessible to blind players via TTS. Battle menu TTS is complete. Now I need to implement **post-battle victory screen TTS** — announcing EXP earned, AP earned, items received, GF level-ups, and abilities learned.

**Key facts about our build:**
- FF8_EN.exe Steam 2013, no ASLR, image base 0x00400000
- Savemap base: `0x1CFDC5C` (confirmed). **IMPORTANT**: Community/wiki docs assume 96-byte (0x60) savemap header. Our confirmed header is **76 bytes (0x4C)**. Subtract 0x14 from all post-header offsets found in community resources.
- Battle entity array: `0x1D27B18`, stride 0xD0, 7 slots (3 ally + 4 enemy)
- Computed stats block: `0x1CFF000`, stride 0x1D0, 3 party slots
- Game mode pointer value during active battle: 3
- Observed mode sequence on battle end: 3 → 5 → 100 → 4 (back to field/world)
- FFNx mode enum: FF8_MODE_SWIRL=3, FF8_MODE_AFTER_BATTLE=4, FF8_MODE_5=5, FF8_MODE_100=100, FF8_MODE_BATTLE=999

**Debug strings in the binary:**
- `0x00B81154`: "AP*10"
- `0x00B81160`: "EXP*10"  
- `0x00B80FF4`: "btitle.ovl" (battle title overlay — likely victory screen graphics)
- `0x00B884F8`: "pet_exp.bin" (GF EXP/AP data file)
- `0x00B88504`: "pet_exp.msg" (GF EXP/AP message strings)

## What I Need

### 1. Victory Screen Game Mode
Which raw game mode value(s) (the value at the game mode pointer) correspond to the **visible** victory screen where EXP/AP/items are being displayed? Is it mode 5, mode 100, or something else? How long does each mode persist? Is there a sub-state or phase counter within the victory screen?

### 2. Battle Results Memory Addresses
Where does FF8_EN.exe (Steam 2013) store the computed battle results during the victory screen? Specifically:

- **Total EXP earned** from the battle
- **Per-character EXP earned** (or is it just total ÷ alive characters?)
- **AP earned** from the battle
- **Items dropped/received** (item IDs + quantities)
- **Gil earned** (if any — I believe Gil comes from selling cards/items, not direct drops in FF8?)
- **Any "Level Up!" flag** per character

### 3. GF AP and Ability Learning
During the victory screen, the game shows AP going to each junctioned GF and may announce new abilities learned. Where is:

- **Per-GF AP progress** stored during the results screen?
- **Ability learning events** — is there a flag or callback when a GF learns a new ability from this battle's AP?
- The GF ability list and AP-to-learn values (kernel.bin section references OK)

### 4. Victory Screen Display Phases
The FF8 victory screen typically displays in stages:
1. "You Win" / fanfare
2. EXP distribution per character
3. AP distribution to GFs
4. Items received
5. (Optional) Level up / GF ability learned announcements

Is there a **phase counter or state byte** that tracks which part of the victory screen is currently being shown? What address?

### 5. Key Functions
What engine functions handle:
- Battle result calculation (EXP/AP computation from enemy data)
- Victory screen rendering/update loop
- Transitioning from battle to victory to field

Any function addresses or call chains would be very helpful.

## Sources to Prioritize
- **Qhimm wiki** (wiki.ffrtt.ru) — FF8 battle system, kernel.bin docs
- **ff8-speedruns/ff8-memory** Cheat Engine tables — confirmed addresses for Steam 2013
- **FFNx source code** (GitHub) — ff8_data.cpp, ff8.h mode enums
- **Myst6re's Makou Reactor / Hyne** source — may have save/battle result structures
- **Community modding forums** (Qhimm, Reddit r/FinalFantasy modding)
- **PSX docs** (may need +0x400000 adjustment for PC addresses)

## Format
Please provide:
1. Confirmed memory addresses (process VA format, e.g., `0x01D27B18`)
2. Struct layouts with offset tables
3. Confidence levels (confirmed/likely/speculative)
4. Source citations for each claim
