# Next Session Prompt — v0.14.93 trigger-data hardcode build

## Where we are

**v0.14.92 BAT-passed Tue 2026-05-05 20:30 with full Section 8 decoder success.** All 38 field-entry programs decoded, complete opcode table mapped, durable artifact at `Plan & Research Documents/wmsetus Section 8 decoded.md`. The Chapter 3 trigger-data saga has its decode key.

Now we ship the data: **v0.14.93** hardcodes the 38 decoded programs as a static C++ array AND adds Section 2 to the dump list so we capture the segment→region-byte map needed to actually use the data at runtime.

## v0.14.92 BAT findings recap

- Build clean (no errors). Dump fired at module init. 48-section table sizes match prediction exactly (Section 1=392b, Section 2=772b, Section 7=56b, Section 8=2652b).
- **Section 7 (56b)** = a 14-record region-ID permutation table: 12 entries of `1D 00 NN 3C` for NN ∈ {0,1,2,3,4,5,6,7,8,9,A,B} permuted, plus a special `1D 00 3E 38` entry plus null terminator. Likely maps continental regions to canonical IDs.
- **Section 8 (2652b)** = the field-entry bytecode. Layout: 38-entry u32 LE offset table (programs at 0x9C..0xA30) + null terminator at 0x94 + bytecode begins at 0x98.
- Each program has the structure: `0xFF06 <loc_id>` header → optional `0xFF02 <story_gte>` / `0xFF03 <story_lt>` gates → `0xFF04` (begin-condition) → one or more clauses of `(0xFF09 <vehicle>, 0xFF08 <region>)` ANDed via `0xFF0B` and ORed via `0xFF0A`/`0xFF0C`/`0xFF0D` → `0xFF16` (end-of-program). 6 unknown opcodes (`0xFF0F/10/11/12/20/21`) appear as additional condition flags in ~6 programs; their consistent operand patterns suggest 16-bit thresholds, but they're not blocking — most clauses don't use them.
- 38 programs ↔ 38 catalog entries — strong one-to-one correspondence hint.
- **No rectangle-bounds opcodes.** Fine-grained entry geometry comes from the region-ID system: each `0xFF08 <region>` operand identifies which segments trigger that field entry. Section 2 (4-byte header + 32×24 = 768-byte segment-region map) provides the segment→region-byte lookup. Multiple segments can share a region byte = same trigger area.

This means AD steering can work without knowing each location's name or wmField ID: for each catalog (X,Y), compute segment, read Section 2's region byte, find all segments sharing that region byte, steer to the closest one matching the player's vehicle and story state.

## v0.14.93 build plan

**Two changes to `src/world_map.cpp`:**

### Change 1: Extend dump to include Section 2

```cpp
static const int WMSETUS_DUMP_SECTIONS_1IDX[] = { 2, 7, 8 };
```

That's a one-line change. Section 2 is 772 bytes (4-byte header + 768-byte segment map = 32 segments × 24 rows × 1 byte) — adds about 49 log rows. The decoder reuses the existing `DumpTriggerSection` helper.

### Change 2: Hardcode the 38 programs as `s_triggerPrograms[]`

Add a new constants block in `world_map.cpp` (after the existing wmsetus constants block) defining the program structures and the 38-entry table. Schema:

```cpp
struct TriggerClause {
    uint16_t vehicle;       // 0x80=foot, 0x84=footAlt, 0x30=Garden, 0x31=Choco, 0x32=Rag, 0=any
    uint16_t region;        // segment region-byte to match (lookup in Section 2)
    uint16_t story_gte;     // 0 = no lower bound
    uint16_t story_lt;      // 0 = no upper bound (treated as ∞)
    uint16_t unk_flags;     // bit-encoded unk opcodes if any (0 for clean clauses)
};

struct TriggerProgram {
    uint16_t loc_id;        // wmField/location ID from 0xFF06
    uint16_t top_story_gte; // top-level story gate (0 = none)
    uint16_t top_story_lt;  // top-level story upper bound (0 = ∞)
    uint16_t top_vehicle;   // top-level vehicle restriction (0 = none)
    uint8_t  num_clauses;
    const TriggerClause* clauses;
};
```

Then `s_triggerPrograms[38]` is a const array, with each program's clauses table also const. Total data size: ~38 × (16 bytes header + average 4 clauses × 12 bytes) ≈ 2.4 KB embedded in `dinput8.dll`'s read-only data section. Trivial.

Source data lives in `Plan & Research Documents/wmsetus Section 8 decoded.md` — copy from the program-by-program decode table. Each clause maps directly: `(vehicle, region [story-range])` → `{vehicle, region, story_gte, story_lt, 0}`.

This build is **still diagnostic-only** — `s_triggerPrograms[]` is defined but not yet wired into `StartAutoDrive`. v0.14.94 does the wire-up. Reason for splitting: keep v0.14.93 narrow enough that BAT cleanly verifies (a) Section 2 dumps successfully and matches our prediction (4-byte header + 768-byte segment map, range of region bytes 0..0x45), (b) the embedded `s_triggerPrograms[]` data compiles correctly and can be log-walked at module init for sanity-check.

**Add a `[TRIGGER-PROGRAMS]` log block at module init** that walks `s_triggerPrograms[]` and prints a one-line summary per program — gives Aaron a runtime confirmation that the decoded data is in the binary as expected.

## v0.14.93 BAT plan

Aaron's steps (identical to v0.14.91/92):
1. Run `deploy.vbs`.
2. Check `build_latest.log` tail.
3. Launch game, reach title screen, quit.
4. Upload `Logs/ff8_world.log`.

What Claude verifies in the log:
- `[TRIGGER-DUMP] sect02 begin (size=772)` block appears with non-trivial bytes after the 4-byte header.
- `[TRIGGER-DUMP] sect07` and `sect08` blocks unchanged from v0.14.92.
- `[TRIGGER-PROGRAMS]` block shows 38 programs with location IDs matching the decoded reference (0x0031, 0x0051, 0x0091, … 0x02C1).

After BAT-pass, Claude (a) cross-references Section 2's region bytes against our 38 catalog X/Y values to confirm each catalog location's segment maps to a region byte that appears in `s_triggerPrograms[]`, (b) writes the v0.14.94 design doc for the AD targeting integration.

## v0.14.94 design preview (after v0.14.93 BAT)

`StartAutoDrive` gains a region-byte-aware targeting path:

1. For the catalog target (X, Y), compute segment via existing `WorldXToSegCol`/`Row`.
2. Look up `s_segmentRegionMap[segCol][segRow]` (loaded from Section 2 at module init).
3. For each `s_triggerPrograms[i]` whose clauses include this region byte AND whose vehicle matches the player's current locomotion AND whose story-flag gates are satisfied, that program is a valid entry to this location.
4. Find ALL segments in `s_segmentRegionMap` with the matching region byte (= equivalent trigger zones).
5. Steer toward the closest such segment; once on it, walking forward triggers the entry.

For multi-program locations (e.g. mobile B-Garden with 3 vehicle-specific paths), the player's current vehicle selects which program applies. For locations with story-windowed regions (e.g. Esthar City: 0x1F → 0x30 → 0x31 → 0x1F across story states), the current savemap word at `[0x2036bde/0x2036bdf]` selects the active region.

The arrival check uses the same logic: when the world map exits to a field, the player's last-known segment had some region byte; if that region byte appears in any program for the current AD target, AD records arrival. This replaces the v0.14.90.2 distance-based heuristic with a deterministic mechanism.

Sweep-search demotes to fallback-only for: locations whose programs use `UNK_*` opcodes that block the simple region-byte match (rare — only programs 4, 9, 13, 14, 19, 24, 25, 32 of 38 have any UNK flags), OR runtime ambiguity (player's segment maps to a region byte not in any reachable program).

## Files in current state

- `src/world_map.cpp` — v0.14.92 with `WMSETUS_DUMP_SECTIONS_1IDX[] = {7, 8}` and the configurable dump loop. Polish work cleanly absent.
- `src/ff8_accessibility.h` — version `0.14.92` with full Stage-4 changelog.
- `Plan & Research Documents/wmsetus Section 8 decoded.md` — **NEW** — full disassembly artifact. Authoritative reference for v0.14.93 hardcoding.
- DEVNOTES.md — current-state section reflects v0.14.92 BAT-pass + v0.14.93 plan.
- NEXT_SESSION_PROMPT.md — this file.
- GitHub: `main` HEAD = `683f1531` (v0.14.90.3). v0.14.91 + v0.14.92 + v0.14.93 will likely consolidate into one push when v0.14.93 BAT lands cleanly.

## Mandatory session-start ritual

Read DEVNOTES.md and this file before any work. **Then read `Plan & Research Documents/wmsetus Section 8 decoded.md`** — that's the source-of-truth for the 38 programs being hardcoded. DEVNOTES_HISTORY.md only when tracing past decisions.

## Commit description for Aaron's push utility (after v0.14.93 BAT-pass)

```
v0.14.91 + v0.14.92 + v0.14.93

Chapter 3 trigger-data: hypothesis pivot + Section 8 decode + hardcoded
programs

v0.14.91 (Stage 3): Adds LoadTriggerZones() in src/world_map.cpp which
clones LoadTerrainGrid's archive-reader pattern but reads world.fi
entry 10 (wmsetus.obj) instead of entry 9 (wmx.obj). Hex-dumps a
configurable set of sections to ff8_world.log under [TRIGGER-DUMP].
v0.14.91 dumped Sections 17/18 per the deep research's leading
hypothesis. BAT result: Sections 17/18 are wm2field-style field-
walkmesh destination data (32-byte records, 6 s32 fields ±10000),
NOT world-map trigger geometry. The deep research hypothesis was
wrong.

v0.14.92 (Stage 4 pivot): Disassembly of FF8_EN.exe identified the
real architecture. World-map tick at sub_53FAC0 calls sub_545EA0 each
frame on foot. sub_545EA0 reads wmsetus Section 8 (2652b, resolved
at runtime as [0x2040070] by sub_542DA0's 8th iteration). Section 8
is a BYTECODE PROGRAM with ~56 opcodes (range 0xFF02..0xFF38)
dispatched via jump table at sub_546100. v0.14.92 dumps Sections 7
+ 8 to ff8_world.log. BAT result: Section 8 fully decoded by Python
disassembler in bash sandbox. 38 programs, full opcode table mapped,
artifact saved to Plan & Research Documents/wmsetus Section 8
decoded.md.

v0.14.93 (Stage 4 deliverable): Embeds the decoded data as a static
s_triggerPrograms[] C++ array in src/world_map.cpp (38 entries,
~2.4KB total). Also extends WMSETUS_DUMP_SECTIONS_1IDX[] to {2, 7, 8}
to capture Section 2 (the 32×24 segment-region byte map needed for
runtime AD steering). Adds [TRIGGER-PROGRAMS] init-time log block
walking s_triggerPrograms[] for sanity-check.

Architecture for AD: each program defines (vehicle, region) clauses
where region = byte in Section 2's segment map. For each catalog
(X,Y), compute segment, read region byte, find all segments sharing
that byte (= equivalent trigger zones), steer to closest. v0.14.94
wires this into StartAutoDrive's targeting + arrival check, demoting
sweep-search to fallback-only for locations with UNK opcodes or
runtime ambiguity.

NO game behavior change in any of these three builds. AD's existing
catalog-center steering + sweep-search fallback continues to work
unchanged. The new code adds diagnostic logging at module init plus
the embedded data table.

VALIDATION
- v0.14.91 BAT 2026-05-05 ~18:30: dumped Sections 17/18 successfully;
  analysis proved hypothesis wrong.
- v0.14.92 BAT 2026-05-05 20:30: dumped Sections 7/8; full disassembly
  succeeded; all 38 programs decoded.
- v0.14.93 BAT [DATE]: [Fill in once Aaron has BAT'd]

LESSONS

- Deep research can have the wrong leading hypothesis. The
  worldmap_section17_position / worldmap_section18_position pointers
  named in FFNx source matched the paired-section pattern seen in
  wmset and were plausible enough to ship a hex-dump build for. They
  turned out to point at field-side destination data. Lesson: when
  a hypothesis can be cheaply tested with a diagnostic build, ship
  it; but if disproved, go to the disassembly directly rather than
  seeking another round of deep research.
- Loading the assembly tree into bash (Assembly_Files.zip → /tmp/asm)
  unlocks fast call-chain walking. Grep for the player position
  address, group by containing function, walk the cluster from the
  world-map tick.
- Configurable section-list arrays (WMSETUS_DUMP_SECTIONS_1IDX) beat
  hardcoded constants when iteration is expected. v0.14.92's refactor
  in v0.14.93 was a one-line addition.
- Bytecode dispatch tables at known addresses (sub_546100 jump table
  at 0x546cac, indexed via byte translation table at 0x546d3c) make
  opcode-set discovery tractable: range-check the opcode, look up the
  byte translation, dispatch. The opcode set we observed (FF01..FF21
  with gaps) maps cleanly onto sub_545F10's multi-mode parser states.

FILES

v0.14.91:
- src/world_map.cpp — ~190 lines: WMSETUS_FL_INDEX + 5 related
  constants, DumpTriggerSection helper + LoadTriggerZones function,
  Initialize call site, file-header CURRENT STATE block.
- src/ff8_accessibility.h — FF8OPC_VERSION 0.14.90.3 → 0.14.91.

v0.14.92:
- src/world_map.cpp — ~80 lines: WMSETUS_DUMP_SECTIONS_1IDX +
  WMSETUS_DUMP_COUNT constants, removed WMSETUS_TRIGGER_SECT_A/B_IDX,
  file-header CURRENT STATE rewritten, constants block expanded with
  section→runtime-pointer map, dump loop, Initialize comment.
- src/ff8_accessibility.h — FF8OPC_VERSION 0.14.91 → 0.14.92.

v0.14.93:
- src/world_map.cpp — extended dump list to {2, 7, 8}; new
  s_triggerPrograms[] static const array (38 entries, ~2.4KB);
  [TRIGGER-PROGRAMS] init log block; file-header CURRENT STATE
  updated for v0.14.93.
- src/ff8_accessibility.h — FF8OPC_VERSION 0.14.92 → 0.14.93.
- Plan & Research Documents/wmsetus Section 8 decoded.md — NEW
  authoritative reference for the embedded data.

NO new addresses, NO new hooks, NO schema changes, NO build script
changes in any of these builds. v0.14.90.3 WM_ENTRY_DEBOUNCE behavior
unchanged.
```

After Aaron pushes via his utility, Claude verifies via `github:list_commits` and updates DEVNOTES + this file.

## Subsequent work after v0.14.94 lands

- **Pre-battle Ship-noise polish** (the deferred v0.14.91 work). Needs Chocobo-mid-drive validation.
- **Field-ID-to-name mapping** for the 38 programs. Two paths: (a) deep research lookup of FF8 field ID → field name (via Deling, Maki, or wiki), (b) empirical capture: instrument `sub_545EA0`'s return path to log `[FIELD-ENTRY]` events; Aaron's natural play activates each entry trigger over a session and we backfill the mapping. Either way, the result lets `s_triggerPrograms[]` carry display names so AD announces e.g. "Approaching Balamb Town" derived from the matched program rather than the catalog.
- **UNK_0F/10/11/12/20/21 opcode interpretation.** Only matters for ~6 of 38 programs; can be deferred or empirically decoded against runtime BAT logs of actual entry events.
- **Encounter-warning feature** (Section 1 + Section 2 already dumped). Logs upcoming encounter region and hostile encounter table when entering a high-rate region. Future, low priority.
- **Persistent accessibility settings** across play sessions (refined-coord serialization first slice).
- **Remove party members from field entity catalog.**
- **X-ATM092 chase scene accessibility.**
- **Walk-and-talk dialog gap** — hardcoded engine path, no hook point.
- **GitHub issue #27 — SeeD Rank misreads.** Hypothesis: `FIELD_H_OFFSET = 0xF94` is a stacked-section-size computation with one wrong section size. Investigation requires deep research.
