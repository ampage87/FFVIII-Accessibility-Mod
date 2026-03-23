# FF8 battle order copy function: what public RE reveals and where to look next

**No public FF8 reverse engineering resource documents the exact copy-in or copy-out functions for the Item→Battle working buffer.** After exhaustive search across every major FF8 RE project — FFNx, OpenFF8, Hyne, OpenVIII, FFRTT wiki, ff8-speedruns memory maps, Qhimm forums, and the new FF8ModdingWiki — the item menu controller internals at addresses 0x4F81F0, 0x4AD0C0, 0x4F80EB, and 0x4D7B3C remain entirely undocumented. The FF8 RE community has concentrated on graphics, audio, field scripts, save formats, and battle rendering; the menu subsystem's copy/swap machinery is virgin territory. However, this research confirms the savemap layout, identifies the definitive struct definition, and provides concrete strategies to locate the copy function via debugging.

## Key Findings

### ITEMS struct is exactly 428 bytes (confirmed by Hyne's SaveData.h)
- battle_order[32] at savemap+0x0B20 (after -0x14 header correction)
- items[198] (2 bytes each) at savemap+0x0B40
- Total 428 bytes copied to working buffer

### 0x4F81F0 controller allocates 0x2B4 (692) bytes on stack
- 428-byte ITEMS struct fits within this, leaving 264 bytes for locals
- Working buffer at ~0x0019FF1C confirmed within expected stack range

### Swap Paradox Resolution
The swap function 0x4AD0C0 writes hardcoded savemap addresses but savemap doesn't change during rearrangement. Most likely explanation: **swaps during active Battle arrangement are performed inline within the 0x4F81F0 jump table states** using simple mov/xchg on the stack buffer, NOT through 0x4AD0C0.

### Debugger Strategy (if needed later)
- Read BP on 0x1CFE77C → catches copy-in on menu entry
- Write BP on 0x0019FF1C → catches inline swaps
- Write BP on 0x1CFE77C → catches copy-out on exit

## Status
Our v0.08.81 VirtualQuery-based multi-region scan successfully finds the working buffer at 0x0019FF1C with inventory cross-validation. Live pointer reads are working. No need to locate the exact copy function unless the live-pointer approach proves unreliable.
