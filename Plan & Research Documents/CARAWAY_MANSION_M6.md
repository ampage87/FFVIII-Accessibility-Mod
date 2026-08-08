# Caraway's Mansion — Mansion 6 door & the phantom exits

STATUS: root cause CONFIRMED (BATs 2026-08-06 20:34 and 21:59). Fix designed, NOT yet built.
Lineage: Caraway glfurin diag = old issues #82/#83. Current tree: v0.20.11 (diagnostics only).

## The room is duplicated — same physical room, several field files
Walkmesh hash 86c159b437c88545 (202 triangles) is shared by glfurin1 / glfurin4 / glfurin5;
the mod's [duproom] logic treats them as copies of one room.
Field id -> mod label ("Mansion N" = id - 720):
  721 glfurin1 = "Mansion 1"      724 glfurin2 = "Mansion 4"
  722 glfurin4 = "Mansion 2"      725 (—)      = "Mansion 5"
  723 glfurin5 = "Mansion 3"      726 glfury1  = "Mansion 6" (the actual M6 room / hall)

## KEY FACT (Aaron-confirmed 2026-08-06): the field variant IS the M6 door state
  M2 = glfurin4 = BRIEFING START = M6 door LOCKED   (you cannot go out to M6)
  M1 = glfurin1 = POST-BRIEFING  = M6 door UNLOCKED (you walk out to M6)
Log progression: enter hall glfury1(726) -> briefing in glfurin4 (M2, locked) -> after the
briefing the room is glfurin1 (M1, unlocked) -> player bounces M1 <-> glfury1(726), i.e. using
the now-open M6 door.  (NOTE: this was briefly recorded backwards; THIS is the correct mapping.)

## The live INF-gateway ENABLE word is the signal — and it matches the variant
Engine crossing check skips a slot when enable(+0x10)==0xFFFF or fieldId(+0x12)==0x7FFF
(mod source field_catalog.inl:3096; dumped live by [GW-LOCK]).
  glfurin1 (M1, unlocked): slot0 enable=0x004F fid=726 ARMED    (M6 usable)
                           slot1 enable=0xFFFF fid=725 DISABLED  (M5 locked)
                           slot2 enable=0xFFFF fid=724 DISABLED  (M4 locked)
  glfurin4 (M2, locked):   all 12 slots disabled (no M6 gateway at all)
Rule: enable=real value => usable; enable=0xFFFF => present-but-locked (the phantom state);
enable=0x0000 & fid=0x7FFF => empty slot. This is the "present but unavailable" signal.

## The actual bugs (what Aaron hears)
The mod does NOT list the armed M6 gateway directly; the scripted-door path drives the catalog
and never consults the gateway enable word:
1. M1 fake "Mansion 6": ent0 door (center -592,247; real dest 725=M5, whose gateway is DISABLED)
   won't resolve -> recovery heuristic (field_catalog.inl:1345, 1000u) borrows the nearest gateway
   label = the armed M6 (dest 726 at -862,-428, 727u away) -> announced "Exit to Mansion 6" at the
   WRONG door. (M6 SHOULD show in M1, but via the real gateway, not this mislabel.)
2. M1 stray "Exit": ent1 (146,500; dest 724=M4, gateway DISABLED) leaks as a generic Exit
   == Aaron's scene-2/3 "another generic exit that shouldn't be".
3. M2: mod correctly shows no M6 (drops rinoa->726 via duproom, log line "JSM exit 'rinoa'
   dest=726 dropped"). Currently correct.

## Fix plan — one signal: the live gateway enable word
A. Cross-check every scripted door / SCREEN_BOUND exit against the live INF gateway for its
   resolved dest; if that slot is DISABLED (0xFFFF) -> SUPPRESS the scripted exit.
   Kills ent0's M5 mislabel-source AND ent1's M4 stray. M4/M5 return automatically when the
   puzzle-solve flips their gateways armed.
B. List the ARMED M6 gateway (dest 726) directly at its own line center (-862,-428) so M1 still
   announces a real, correctly-placed "Exit to Mansion 6".
C. Net: M1 => one real exit (M6 @ -862,-428), no strays. M2 => no M6 (locked).

## OPEN — verify in a during-puzzle BAT
Scene 5 (puzzle, Quistis trapped, in glfurin1): M6 should NOT show while trapped. Does the LIVE
M6 gateway slot go to 0xFFFF during the trap? If yes, rule (A/B) via the live read suppresses it
automatically. If it stays armed, we need the puzzle-trapped flag on top. Capture [GW-LOCK] M6
slot state during the puzzle to decide.

## Provenance
Logs/ff8_field.log tags: [GW-LOCK], [duproom], [MAPJUMP-RES], [M6-DOOR]. Disassembler (correct
opcode model): device /tmp/disC.py. NEVER push (Aaron pushes via Utilities/push_to_github.ps1).

# =====================================================================
# IMPLEMENTATION DESIGN (added after full code trace) — the reason==1 fix
# =====================================================================
## The three DISTINCT locking mechanisms in this room (don't conflate them)
1. INF gateway enable word (+0x10=0xFFFF): gates M6. Armed in glfurin1(M1), absent in glfurin4(M2).
   Surfaced by InjectGatewayExits (field_catalog.inl ~2385). v0.20.2 retired the stale-gw rule,
   so every LOADED (armed) gateway is listed. M6 in M1 rides this path -- LEAVE IT.
2. Story-progress MAPJUMP gate (special[0x100] > 376): gates the M4/M5 scripted doors (ent0->725,
   ent1->724). The interpreter InterpretExitMethod EVALUATES it. progress<=376 -> RET before any
   MAPJUMP -> returns -1 with InterpTrace.reason==1. THIS is the clean "locked" signal.
3. Trigger-line active flag (LINEON/LINEOFF -> s_capturedLines[].active): already tracked & already
   skipped by the catalog. Not the M4/M5 mechanism (their lines stay active).

## WHY the M6 phantom appears (M1): ent0 (locked M5 door, marker dest) hits the SETLINE recovery
block (field_catalog.inl ~1346), borrows the nearest ARMED gateway label = M6 (727u), and that
"Exit to Mansion 6" then DEDUPES AWAY the real M6 gateway (InjectGatewayExits dedup by name). So M6
shows at ent0's wrong spot (-592,247) and the real gateway (-862,-428) is hidden. Suppress ent0 ->
the mislabel goes AND the real gateway surfaces at the right place. Two birds.

## THE FIX (gate suppression on interpreter reason==1 -- avoids the #86 underflow regression):
reason codes (resolver l.313): 0=reached MAPJUMP | 1=RET(gate-false=LOCKED) | 2=underflow(real exit,
e.g. glprein1 trapdoor -- must NOT suppress) | 3=off-range | 4=stepcap | 5=CAL/JPF underflow |
6=tainted dest. ONLY reason==1 means story-locked. Gate on it exactly.

Edit points (all DEVICE tree; NEVER push):
A. field_archive_jsm_state.inl (~l.43 struct): add `bool exitLocked=false;` to the JSM entity info.
   + captured-line struct (grep the struct with .active/.destFieldId/.lineOrder): add `bool exitLocked`.
B. field_archive_jsm_mapjump_resolver.inl Run() (~l.616): pass an InterpTrace to
   SafeInterpretExitMethod; if interpDest<0 && tr.reason==1 && type in {SCREEN_BOUND,MAP_EXIT}
   -> info.exitLocked=true (log [MAPJUMP-RES] "... LOCKED (story gate false, reason=1)").
C. field_nav_fieldscripts_linetypes.inl (~l.313, `s_capturedLines[t].destFieldId = rawParam;`):
   also `s_capturedLines[t].exitLocked = s_jsmEntities[jsmIdx].exitLocked;`.
D. field_catalog.inl SETLINE path (~l.1116 loop / before recovery ~l.1346): 
   `if (s_capturedLines[t].exitLocked) { log+continue; }` -- suppress, no recovery, no entry.
E. field_catalog.inl mapexit path (~l.2320): `if (je.exitLocked) continue;` (carry the flag onto je).

Result: M1 -> real M6 gateway @(-862,-428) only; ent0/ent1 (locked M5/M4) gone. M2 -> unchanged
(duproom). Puzzle-trap M6: still open -- if M6 shown there is ALSO ent0's mislabel, this fix removes
it; if it's the raw armed gateway, need during-puzzle [GW-LOCK] to see if the trap disarms it.
VERIFY: catalog harness (compiles field_catalog.inl) for D/E; g++ -fsyntax-only for B/C; brace balance.
Bump FF8OPC_VERSION == CHANGELOG top heading. Then Aaron BATs the full mansion sequence.

# =====================================================================
# IMPLEMENTED v0.20.12 (LOCAL, NOT pushed, NOT MSVC-built, NOT BAT'd)
# =====================================================================
Applied the reason==1 fix via a resolver sentinel (avoided struct changes):
- field_archive.h: static const int32_t EXIT_LOCKED_MARKER = 0x8000FFFE.
- resolver Run(): when interpDest<0, re-runs the interpreter with an InterpTrace to read
  reason; logs "[MAPJUMP-RES] ... interp no-dest reason=N"; if reason==1 (RET/gate-false)
  for SCREEN_BOUND/MAP_EXIT -> info.param = EXIT_LOCKED_MARKER + "[SUPPRESS]" log.
- field_catalog.inl SETLINE path (~1352): drop line whose destFieldId == EXIT_LOCKED_MARKER
  BEFORE the gateway-recovery (kills ent0 mislabel-source).
- field_catalog.inl MAP_EXIT path (~2331): drop je.param == EXIT_LOCKED_MARKER.
VERIFIED: catalog harness GOLDEN MATCH (33 fixtures); braces balanced (resolver 132/132,
catalog 379/379, header 12/12); version 0.20.12 == CHANGELOG top. v0.20.11 diagnostics
([M6-DOOR]/[GW-LOCK]/[PUZZLE-GATE]) left ACTIVE -- they capture the during-puzzle state.
BAT: full mansion sequence. Expect: M2(glfurin4) no M6; M1(glfurin1) M6 once at the real
door, no pre-puzzle M4/M5 strays; after puzzle solve M4/M5 appear. grep [MAPJUMP-RES] reason=
+ LOCKED, and the during-puzzle [GW-LOCK] M6 slot answers the last open question.

# =====================================================================
# v0.20.13 (LOCAL, NOT pushed/BAT'd) -- M6 phantom during the TRAP
# =====================================================================
BAT of v0.20.12 confirmed: pre-puzzle M4/M5 strays + M6 mislabel GONE. Remaining (Aaron):
 (2) trapped-in-M1 (glass puzzle) still announces M6 -- it's the raw ARMED gateway, not ent0.
 (1) glass/puzzle objects catalogued in M1/M2/M3 but only interactive once trapped.
 (3) low pri: M3 Rinoa-re-entry trigger reads as "exit" not "interaction".

## THE TRAP FLAG (found by full-varbank diff, PUZZLE-GATE dumps)
Story var 0x0F2 @ 0x01CFE9B8: 0x25 in explore phase (M6 door OPEN), 0x16 from trap onward
(M6 door LOCKED, gateway still armed=0x004F). Full 0x000-0x7FF diff of M6-open(23:19,glfurin1)
vs trapped(23:26): 21 bytes differ, but only SEVEN are stable phase flags (same value across ALL
later locked visits while progress[0x100] drifts): 0x069(00->02) 0x0C4(01->FF) 0x0EA(00->FF)
0x0EC(00->FF) 0x0EE(00->FF) 0x0F2(25->16) 0x105(00->02). 0x0F2 chosen as representative.
Confirmed across all 4 variants: glfurin4@354=0x25, glfurin1@357=0x25 (open); glfurin5@364=0x16,
glfurin1@366/382=0x16 (locked).

## v0.20.13 FIX (issue 2 only)
field_catalog.inl: CarawayMansionSealed() = (field starts "glfurin") && live byte 0x0F2==0x16
(SEH-guarded). InjectGatewayExits drops the dest-726 gateway when sealed. Gated on the KNOWN
value 0x16 (not !=0x25) so a mis-read never hides a real exit. Harness GOLDEN, braces 383/383,
version==changelog. glfury1 (M6 room itself) is NOT glfurin* -> untouched.

## DEFERRED (do AFTER this BAT validates the 0x0F2 gate):
 Issue 1 (glass): SAME flag, INVERSE gate -- suppress glfurin* JSM puzzle objects when NOT sealed
   (0x0F2 != 0x16). RISK: hiding them DURING the trap would block the puzzle; gating "suppress when
   != 0x16" never fires at 0x16, so safe. Objects = 'cup'(Glass)+'kakusi'(Object) etc, all mansion
   JSM objects are puzzle objects (no legit always-objects seen). Validate flag on M6 first.
 Issue 3 (M3 Rinoa exit): classify the glfurin5 Rinoa-re-entry trigger as interaction not exit. Low.

# =====================================================================
# v0.20.14 (LOCAL, NOT pushed/BAT'd) -- corrected trigger + glass gate
# =====================================================================
v0.20.13 BAT (Aaron, 3 F11 screenshots): M6 dropped TOO EARLY. 0x0F2 flips at MISSION start
(Quistis takes team, progress~364), not PUZZLE start, and even FLICKERED within progress 366
(log: M6 suppressed@00:20 then listed@00:21:33, same field/progress). SS2=M6 wrongly dropped
@progress 366; SS3=puzzle @progress 382, M6 correctly gone + M4/5 + glass ENABLED.

## THE REAL SIGNAL: progress[0x100] > 376 (0x178) -- the SAME gate M4/5 use.
One event = puzzle activates: M6 locks, M4/5 unlock, glass becomes interactive. All at
progress>376. Confirmed: 357/366 (<=376) M6 shown; 382 (>376) M6 gone + M4/5 + glass live.
0x0F2 was ~18 progress-units early AND unstable -> abandoned. Monotonic progress is stable.

## v0.20.14 FIX (both main issues, one signal)
CarawayMansionSealed() now: glfurin* && live word (0x01CFE9B8+0x100) > 376. Two consumers:
 - Issue 2 (M6): InjectGatewayExits drops dest-726 gateway when sealed. Now correct.
 - Issue 1 (glass): InjectJsmSpecials suppresses mansion JSM objects (cup/kakusi) when NOT
   sealed (explicit glfurin* test -- helper is false off-mansion, so !sealed alone would drop
   objects everywhere). Fires only on the not-sealed side -> never hides during the puzzle.
Forward decl of CarawayMansionSealed added before InjectJsmSpecials (defn is later).
VERIFIED: harness GOLDEN, compiles clean, braces 384/384, version==changelog.
Remaining: issue 3 (M3 Rinoa trigger = exit not interaction, LOW pri, task #78).

# =====================================================================
# v0.20.15 (LOCAL, NOT pushed/BAT'd) -- the two remaining glass issues
# =====================================================================
v0.20.14 BAT: M6 correct. But (A) "Glass" still shows pre-puzzle, and (B) during the puzzle a
duplicate "Object" sits on the "Glass". Three co-located entities at (-694,-254):
  ent15 'cup'  -> displayed "Glass" (the interactive object; gated OK by v0.20.14)
  ent14 'kakusi' -> displayed "Object" (the puzzle CONTROLLER, REQ-dispatches ~12 entities
                    like director1)  <-- issue B duplicate
  glass-shelf INTERACTION LINE (line2) -> displayed "Glass" via InjectInteractionLines
                    (a DIFFERENT path than the object gate)  <-- issue A pre-puzzle leak
Pre-puzzle: only the interaction line surfaces (objects gated). During puzzle: cup + kakusi
surface (line does not). So A = gate the interaction line; B = drop the kakusi controller.

## v0.20.15 FIX
 A: InjectInteractionLines -- suppress a glfurin* ENT_INTERACTION line when !CarawayMansionSealed()
    (progress<=376). Save Points never gated; fires only on not-sealed side. Forward decl of
    CarawayMansionSealed moved before InjectTriggerLineExits so this earlier path sees it.
 B: added 'kakusi' to ENTITY_SKIP_NAMES (entity_classifications.h) -> IsBgControllerName drops it
    everywhere (like director1). Only 'cup'/Glass remains during the puzzle.
VERIFIED: harness GOLDEN, compiles clean, braces 385/385, version==changelog.
Remaining: issue 3 (M3 Rinoa trigger = exit not interaction, LOW, task #78).

# =====================================================================
# v0.20.16 (LOCAL, NOT pushed/BAT'd) -- M3 Rinoa door label (was issue 3)
# =====================================================================
Aaron reframed issue 3: the M3 (glfurin5) generic "Exit" is the door Quistis heads for to leave
to Mansion 6, which springs the "Rinoa runs back in" cutscene. He wants it labeled "Exit to
Mansion 6" so a blind player heads for it with the same lack of forewarning as a sighted player
(NOT reclassified as an interaction).
Mechanism: glfurin5 ent0 (SCREEN_BOUND) MAPJUMP dest is a runtime var -> interp reason=5
(underflow) -> kept VARBLOCK marker 0x800002D6; low word 0x2D6=726=Mansion 6. glfurin5 has no
local INF gateway so recovery couldn't name it -> bare "Exit".
FIX: SETLINE-exit recovery block adopts marker addr as dest when addr==726 && glfurin* ->
labels "Exit to ...Mansion 6". LABEL-ONLY: trigger-line path applies NO duproom, so it stays
visible (verified 1109-1460). Scoped mansion+726: M1 gateway (diff path), M4/5 locked doors
(EXIT_LOCKED_MARKER, diff addr), M2 rinoa (resolved MAP_EXIT, duproom-dropped, diff path) all
untouched. Harness GOLDEN, braces 386/386, version==changelog.
=== CARAWAY MANSION ARC COMPLETE (v0.20.12 - v0.20.16, all BAT-confirmed except v0.20.16). ===

# v0.20.17 (LOCAL, NOT pushed) -- investigation diagnostics OFF. [PUZZLE-GATE] + [M6-DOOR]
# gated behind s_mansionInvestigationDiag=false (functions kept, one-line re-enable).
# [GW-LOCK] + the fix's [MAPJUMP-RES]/SUPPRESSED logs KEPT. Harness GOLDEN. ARC DONE.

# v0.20.18 (LOCAL, NOT pushed) -- push unblock (NOT mansion logic). The push utility's local CI
# mirror hard-failed: field_navigation.cpp was 82,527 B, 607 over the 80 KB (81920) ceiling.
# NB the mansion work is all in the size-EXEMPT field_catalog.inl; field_navigation.cpp was never
# touched by it -- the overage was pre-existing creep. Removed the dead v0.08.23 descriptor-table
# poll probe (if(false)) + v0.08.24 PSHM_W dump stubs (statics stay used in field_nav_diagnostics.inl,
# no orphans). Now 81,233 B (687 under). SET3 'PERMANENTLY DISABLED' marker preserved. Longer-term
# field_navigation.cpp split = issue #37 (still open). Optional tidiness: extract Caraway code from
# field_catalog.inl into a caraway.inl (Aaron's idea; findability only, does NOT affect the size gate).
