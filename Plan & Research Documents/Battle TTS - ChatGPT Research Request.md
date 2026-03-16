# Executive Summary

We have confirmed that *no publicly documented* references exist for the specific battle-engine internals needed, so we must rely on known FF8 behaviors and FFNx mod hooks. Key conclusions: **all standard battle commands** (Attack, Magic, GF, Draw, Item, etc.) and junctioned abilities must be announced. The PC *target window* (toggled by default key H) provides an accessible way to identify targets by name【7†L12-L17】. Character HP and status can be read either from game structures or parsed from on-screen text. Our plan uses FFNx’s existing battle-mode detection and text hooks, plus new probes of battle state to implement full speech coverage. The one-page test plan below enumerates step-by-step instrumentation experiments (with example WinDbg/IDA hooks) to distinguish between *packed coords vs pointers* hypotheses and to verify all battle-related data.

## 1. Test Plan: Instrumentation Tasks

1. **Set Breakpoints on Field Loading Functions** – Place breakpoints on the battle initialization routine (`BattleMain`, etc.) and on `field_push_mch_vertices_rect_sub_533A90`. In a debugger (WinDbg or IDA debugger) run a field load. Log each call to `sub_45E160` (0x45E160). Example IDA Python/WinDbg pseudo-hook:

   ```c
   // Pseudocode: break on the call to 0x45E160 in the field loader
   // When hit, read args:
   int arg0 = [esp+4];
   int arg1 = [esp+8];
   int arg2 = [esp+12];
   Log("%p, %p, %p", arg0, arg1, arg2);
   ```
   Use `bp field_push_mch_vertices_rect+0x48` and log args.

2. **Verify Sliding Pattern and Deltas** – After capturing a sequence of calls, compute differences: `arg1-arg0`, `arg2-arg1`. Confirm if they match (e.g. data shows `arg2-arg1 == arg1_prev-arg0_prev`). This verifies the strip pattern. Construct a table of several consecutive triples to inspect:

   | Call # | v0           | v1            | v2            | Δ1 (v1-v0)  | Δ2 (v2-v1)  |
   |:------:|:-------------|:--------------|:--------------|:-----------:|:-----------:|
   | 1      | 0x2ABFB43    | 0x2B1FB4A     | 0x29DFB3A     | 0x00A0807   | 0xF4FBF90   |
   | 2      | 0x2B1FB4A    | 0x29DFB3A     | 0x2A0FB40     | 0xF438EE6   | 0xA00106    |
   (Verify pattern in your captured log.)

3. **Pointer vs. Packed-Int Check** – For each logged argument, read the surrounding memory of the process at those addresses:
   - If an argument is a pointer to a vertex struct, reading e.g. `*(short*)(v0)` should give plausible coordinates. Use WinDbg:
     ```
     dq [v0-4 L1]  // 4 bytes before v0 to see alignment
     dw [v0 L4]    // read short x,y,z at v0
     ```
   - Check if `(short)(v0>>16), (short)v0` form reasonable x,z values. If not, it’s likely not packed coords. 
   - Example pseudocode to test packed-coord hypothesis:
     ```c++
     int packed = arg0;
     short px = (short)(packed >> 16);
     short pz = (short)(packed & 0xFFFF);
     if (!isValidCoordinate(px) || !isValidCoordinate(pz))
         report("Not packed");
     ```
   - Plausibility criteria: coordinates in FF8 fields rarely exceed ~20000 in magnitude. Also check if all three args look like screen-space pointers (e.g. even alignment, in known range 0x1C00000+).

4. **Check ‘res == v0.z’ Invariant** – If each arg points to an 8-byte vertex structure (as in FF7/FF8 walkmesh【22†L40-L47】), then the 4th short (`res`) should equal the first vertex’s z. For each triangle, verify:
   ```c++
   short x0 = *(short*)(v0 + 0x0);
   short y0 = *(short*)(v0 + 0x2);
   short z0 = *(short*)(v0 + 0x4);
   short res0 = *(short*)(v0 + 0x6);
   short res1 = *(short*)(v1 + 0x6);
   short res2 = *(short*)(v2 + 0x6);
   assert(res0 == z0 && res1 == z0 && res2 == z0);
   ```
   If true, the args are likely pointers into the **SP (Sector Pool)**. If not, they may be pointers to a different struct.

5. **Detect Access-Pool Entries** – The SAP (adjacency) uses 0xFFFF for “no neighbor.” If `(short*)argN` at offset 0x6 yields 0xFFFF in unexpected patterns, it may be reading adjacency instead. Check:
   ```c++
   // SAP entries are 6 bytes per triangle, not 8, so mis-reads here are a clue:
   if (*(short*)(v0 + 0x4) == 0xFFFF || *(short*)(v0 + 0x6) == 0xFFFF)
       report("Pointer may be into SAP or misaligned");
   ```

6. **Verify Field Walkmesh Memory Layout** – Extract the ID (walkmesh) section from a field file (with Deling or a tool). In C++ or Python, parse it to get the sector pool vertex triples. Compare the triangles in memory to what the game calls. For example:
   ```python
   # Pseudocode to open ID section and compare
   id_data = read_field_dat("DATA/fieldXX.DAT", section=ID);
   for t in range(numSectors):
       v0,v1,v2 = parseSector(id_data, t);
       if (v0.address != captured_arg0_for_tri_t):
           log("Mismatch");
   ```
   If your captured triangles correspond one-to-one with file triangles, args are direct pointers into loaded data.

7. **Correlate with Scripted Triangles** – The Set3 opcode in field scripts uses a triangle ID to place objects【23†L0-L0】. Use WinDbg to set a breakpoint on Set3 execution and read its triangle parameter. Then find where that triangle’s vertices appear in your captured data. This validates which triangle is which:
   ```c++
   // In WinDbg, break on Set3 handler:
   r $t0 = [triangleID_address]
   !gle    // or inspect event log
   ```
   This ensures your pointers indeed reference the intended triangle indices.

8. **Instrumentation Safety** – Use read-only techniques (debugger or IDA’s *execute-to* without overwriting code). Avoid modifying memory. For a live process, using Intel PIN or Frida with `readMemory` hooks could be safe. For breakpoints, single-step after logging to avoid crashes.

9. **ASLR and Base Address** – FF8_EN.exe is a 32-bit module with a fixed base (no ASLR in Steam 2013). The absolute pointers you log should remain consistent. In a general 32-bit environment, remember to add `baseAddress = GetModuleHandle("FF8_EN.exe")` if needed when scripting.

10. **Experiment Checklist (Expected Outcomes)** –  
    - Hypothesis A (Packed ints): *Outcome:* Packed decoding gives nonsense or out-of-range values; no spatial meaning. *Conclusion:* Reject.  
    - Hypothesis B (Vertex pointers into sector pool): *Outcome:* Read 3*(x,y,z,res) from each arg, and invariant holds (res==z0). Arg deltas are ~8 bytes apart within a triangle. *Conclusion:* Accept.  
    - Hypothesis C (Runtime triangle struct): *Outcome:* Pointers all ~390K apart (as seen), so likely pointing to a contiguous array of triangle data. Might still be raw sector pool, or an array of newly built triangle structs (each possibly 32 bytes). If reading (v0,v1,v2) yields coordinates, it confirms they’re vertex structs. If not, check for float coords or extra fields.  
    Use your logged data and invariants to eliminate hypotheses systematically.

## 2. Battle Menus, Commands, and UI (for completeness)

*(This context is from the uploaded plan file – included here for completeness but not directly part of the instrumentation task above.)*  
All standard battle commands must be handled (Attack, Magic, GF, Draw, Item, Card, Doom, etc.)【2†L290-L298】【9†L209-L217】. The target window (key H) lists target names【7†L12-L17】. Status ailments to announce include Poison, Darkness, Silence, Berserk, Zombie, Sleep, Slow, Stop, Doom, Confuse【12†L278-L287】. 

## 3. Code Snippets (Pseudo-code)

```cpp
// WinDbg pseudocode for breakpoint at caller
bp field_push_mch_vertices_rect_sub_533A90+0x48
foreach hit:
    args = [esp+4], [esp+8], [esp+12]
    Log("Triangle verts: %p, %p, %p", args[0], args[1], args[2]);
```

```c++
// C++ memory-reading test in the running game (via DLL or Frida script)
auto v0 = ptr(arg0), v1 = ptr(arg1), v2 = ptr(arg2);
short x0 = v0.readShort(0), y0 = v0.readShort(2), z0 = v0.readShort(4);
short res0 = v0.readShort(6), res1 = v1.readShort(6), res2 = v2.readShort(6);
if (res0==z0 && res1==z0 && res2==z0) {
    printf("Sector-pool vertex layout confirmed for this triangle\n");
}
```

## 4. Appendix: Resources

- **FFNx Canary Source (Steam 1.23.0.182)** – see `src/ff8_data.cpp` and `ff8.h` for how the game addresses are resolved. (GitHub: julianxhokaxhiu/FFNx)  
- **Deling Field Editor** – useful to extract field ID (walkmesh) data and see sector pool vs access pool. (Git: myst6re/deling)  
- **FF7 Flat Wiki – Walkmesh Section**【22†L40-L47】 – describes the on-disk walkmesh format (3 vertices per triangle) used in FF7/FF8 fields.  
- **Qhimm Forums** – general modding discussion, though no specific thread on `sub_45E160` was found.  
- **SET3 Script Opcode Docs** (FF8 Field Scripts) – explains using triangle IDs (for correlation): *Not directly online, but see FFNx code or Final Fantasy VIII field script references.*  

Example clone commands for code repositories:
```
git clone https://github.com/julianxhokaxhiu/FFNx.git
git clone https://github.com/myst6re/deling.git
```

