# Catalog Revamp — working plan + build log

Durable doc on the device. Complements the "Entity catalog revamp deep research
results" reference with an executable sequence. Picked up after the Caraway's
Mansion arc (v0.20.12–.16), whose live-state work (story-gate, gateway-enable,
controller skip-list, co-located dedup) is the first concrete piece of this.

## Thesis (one sentence)
The catalog over-trusts the STATIC JSM scan (what an entity *is*) and under-uses
LIVE runtime state (whether it is usable *right now*), so it lists things that
exist in the field data but aren't real to the player: invisible controllers,
hidden/deactivated entities, story-locked exits, disabled interactions, and
duplicate representations of one thing. The revamp = make a live-state check a
first-class, uniform step of catalog assembly, across every field.

## Live-state signals (the spine)
| Signal | Meaning | Source | Status in tree |
|---|---|---|---|
| Line active | trigger line on/off | LINEON/LINEOFF -> s_capturedLines[].active | tracked |
| Talk enabled | can press X to talk | talkonoff 0x24B; talk_radius 0x1F8 | partly (radius) |
| Visible | SHOW/HIDE flag | +0x160 bit3 (HIDE); opc 0x60/0x61 | DONE (v0.18.3.269, filtered) |
| Active | UNUSE/USE state | execution_flags 0x160 | NOT read live (Step 1.2) |
| Gateway enabled | INF gateway armed | enable word +0x10 (0xFFFF=off) | read via [GW-LOCK] |
| Story gate | scripted exit gate | interpreter reason (1 = gate-false) | done for exits (v0.20.12) |
| Controller | invisible dispatcher | REQ-heavy / director / skip-list | partial (IsBgControllerName) |

Highest-leverage gap: SHOW/HIDE + UNUSE/USE are not read live. Those are the
general engine "this entity isn't there right now" mechanisms — Step 1.2.

## Workstream 1 — live-state filtering
- 1.0  Reference "expected sets" (below) — the acceptance oracle.
- 1.1  Observe-only [CAT-AUDIT] diagnostic — SHIPPED v0.20.19 (log-only). One line
       per kept entry per path, with the live signals in scope. No behaviour change.
- 1.2  Wire the two unread signals (SHOW/HIDE visibility + execution_flags 0x160),
       SEH-guarded, still log-only. Completes every candidate's live picture.
- 1.3  Unified gate CatalogEntryIsLiveNow(candidate); route every path through it,
       one signal at a time (controllers -> invisible/UNUSE -> talk-disabled ->
       exits already gated). Err toward KEEP; every DROP logs its reason.
- 1.4  Co-located dedup: collapse an entity's multiple representations (glass was
       line + object + controller at one spot) into the most-interactable one.

## Reference "expected sets" (Step 1.0 oracle — Aaron to confirm during BAT)
For each field, what the catalog SHOULD contain, per state. Fill from BAT.
- glwater3  (sewer gate room)   — canonical BLOAT example. Expected: <gates/exits + save?>; junk to remove: <water controller, etc.>
- bghall_1  (B-Garden Hall 1)   — has the REAL Directory that must NOT drop. Expected: exits + Directory + students.
- glfurin1  (Caraway Mansion 1) — now correct; REGRESSION TRIPWIRE. Expected (post-briefing): Exit to Mansion 6 (real gateway) + no strays; puzzle glass only when sealed.
- <shop/save field>             — expected: Save Point, shop, exits.
- <draw-point field>            — expected: Draw Point + exits.

## Standing principles (scar tissue)
1. Observe-only diagnostic BEFORE any filtering change.
2. Err toward showing too much — losing a real entry is the worst failure (#88).
3. Every DROP logs a reason, so a BAT catches an over-drop in one cycle.
4. All live memory reads SEH-guarded.
5. Validate against the reference expected-sets, not vibes.
6. A category can empty mid-visit (lines re-capture after a battle, #88) — compare
   against the FIRST field-entry block, not a post-battle one.

## Build log
### v0.20.19 — Step 1.1 [CAT-AUDIT] (SHIPPED, pending BAT)
Added CatAudit() helper (field_catalog.inl, before InjectTriggerLineExits) + a
scoped-brace audit call before all six KEEP push-sites: setline-exit (trigExit),
trigger-event (evEntry), interaction (intEntry), object (jsmEntry), mapexit
(mapExit), gateway (gwExit). Emits:
  [CAT-AUDIT] field=<f> path=<p> type=<T> name='<n>' pos=(x,y) sym='<s>' ctrl=<0/1> | <detail>
Pure logging (no branch/continue/mutation); catalog contents byte-identical;
reads only mod-side data (harness-safe). Switch off via s_catAudit=false.
BAT: visit the 5 reference fields, open the catalog on each, send ff8_field.log,
grep [CAT-AUDIT]. Then we fill the expected-sets above and design 1.2/1.3.

### v0.20.19 BAT findings (2026-08-08, B-Garden) — three confirmed issues
Aaron ran B-Garden, cycled the catalog on several fields. [CAT-AUDIT] pinned all three:

**(1) Elevator corridor bg2f_1 — position/identity collapse.** 5 JSM objects (elevator,
dic=Directory, l1, panpi1, seito) ALL inject at the identical fallback coord (-4,-2619)
because none resolves to its real runtime position. Live talk-flag cannot separate real
from junk: the REAL Directory (dic) reads talkon=0 in BOTH index conventions, same as the
modelless controller l1 (director pattern). RT-INTERACT (v0.19.9) shows the compact-vs-flat
oir convention is still unsettled (elevator: compact oir=9 model=9 correct, flat model=-1;
panpi1: flat oir=11 model=10 correct, compact model=-1 — neither convention is universally
right). => root cause = unresolved JSM-object -> live-Others-entity mapping; it blocks both
correct positions AND reliable live-state. KEYSTONE for the revamp. (task #83)

**(2)+(3) Classroom bgroom_1 = "Classroom 1" (id 232) — camera-zone problems.**
The classroom spans multiple field files (Classroom 1=232, Classroom 3=234, ...). bgroom_1
has TWO SCREEN_BOUND lines:
  - line5 'SelphieDummy' -> 139 (2F Hallway 3), paired center (1418,-3444) => surfaced as
    "Exit to 2F Hallway 3". This is the room's real exit, at the FAR camera zone.
  - line6 'Selphie'      -> 234 (Classroom 3), paired center (969,376). This is the CAMERA-PAN
    into the next section of the same logical room.
BUG A (mislabel): ent6 'Selphie' is the classroom scene DIRECTOR (REQ-TARGET dispatches to
  Quistis/Student1/2/4; doorcont REQs Selphie). Because line6 REQs dialog, the dual-purpose
  rule (field_catalog.inl ~1186, hasDialogReqTarget) drops its EXIT and shows it only as a
  nameless "Interaction" at (969,376). So the "continue to the next section" transition is
  never offered as a named target -- the opposite of what a blind player needs.
BUG B (leak): the far hallway exit (139) is announced across the camera boundary; the
  "path crosses screen-bound line6" straight-line filter is fragile (hid it 1/26 refreshes).
Aaron's spec: "In the back, you should hear the transition to the FRONT (the way to the next
  section), NOT the hallway exit located in the front -- a sighted player can't see that exit
  until they walk to the front." Mirror the sighted camera reveal.
Available signal: the mod ALREADY reads the live camera-zone index [0x1CE4906]
  (field_nav_observe.inl / field_nav_helpers.inl, for movement rotation). So current-zone is
  known; the gap is (i) tagging each entry's zone and (ii) surfacing pan-transitions.

Fix direction (task #84), two parts:
  A. Surface a same-logical-room camera-pan SCREEN_BOUND transition as a navigable target
     (named by dest / "continue"), even when dual-purpose. Discriminate a camera-pan (sibling
     room, e.g. Classroom 3 shares the classroom walkmesh family with Classroom 1) from a real
     different-area exit and from dorm-bed-style furniture, so we don't reopen the fepic1/
     dorm-bed regressions the dual-purpose rule guards against.
  B. Hide far-camera-zone entries reliably using the live zone index / a robust reachability
     test (flood-fill walkmesh treating screen-bound lines as walls), replacing the fragile
     straight-line crossing test. Err toward KEEP for exits (#88).
Next step: a camera-zone diagnostic (log [0x1CE4906] per refresh + which side of each
  screen-bound line each entry falls) to validate zone-tagging BEFORE filtering.

### v0.20.20 BAT (2026-08-08) — surfacing works; far-zone HIDING is the fragile straight-line filter
Aaron: "I did hear the transition between front and back of the classroom [v0.20.20 works].
However, I still heard interactive objects and the exit from the front while in the back."

Diagnosis from the fresh log (bgroom_1, 201 refreshes; whole classroom is ONE field with
internal camera zones, id 232):
- The existing far-zone filter is `SegmentsCross(player, entity, screenBoundLine)` -- a
  STRAIGHT-LINE test: hide an entry if the segment player->entity crosses a screen-bound
  line (line5=hallway 139 @ (1418,-3444); line6=Classroom-3 234 @ (969,376)). It lives in
  ONLY 3 of 6 inject paths: InjectTriggerLineExits (1371), InjectInteractionLines (1760),
  InjectGatewayExits (2866). It is ABSENT from InjectJsmSpecials (objects) and InjectMapExits.
- It is FRAGILE: from some back positions the player->front-entry segment misses the boundary
  segment (passes around its endpoints), so the front entry leaks. Confirmed: the hallway exit
  (139) was KEPT in 26 refreshes; in one sampled refresh (player (1153,410), back) it correctly
  filtered line5 + all front interactions + the INF hallway gateway -- so it works from SOME
  positions, leaks from others. Classic straight-line-vs-segment failure.
- The "interactive objects" Aaron hears are the scene-character SETLINE interactions (line0-7:
  Director/Squall/Zell/Selphie) and/or runtime NPCs -- paths that are unfiltered or leak.

FIX (next build): replace the straight-line test with a TOPOLOGICAL zone-membership test, applied
UNIFORMLY across all entry paths (trigline, interaction, gateway, object, mapexit, + runtime NPCs):
  IsEntryInPlayerZone(x,y):
    1. player triangle T0 = *(uint16_t*)(entityBase + STRIDE*playerIdx + 0x1FA)  [SEH-guarded]
    2. BFS over WalkmeshTriangle.neighbor[0..2] from T0; do NOT step across an edge whose
       center-to-neighbor-center segment crosses an ACTIVE screen-bound line segment (treat
       screen-bound lines as walls). Mark reachable triangle set.
    3. entry's triangle = nearest-centroid triangle to (x,y); KEEP if reachable, else HIDE.
  Building blocks CONFIRMED present: WalkmeshTriangle{neighbor[3],centerX/Y,vertices} (field_archive.h:386),
  s_walkmesh, SegmentsCross, player-tri @0x1FA (field_nav_autodrive.inl pattern).
  SAFETY (#88): bias to KEEP -- if walkmesh invalid / player-tri unknown / entry-tri ambiguous,
  do NOT hide. Every HIDE logs its reason so a BAT catches an over-hide in one cycle. Never hide
  a camera-zone-transition exit (the way forward) or the current zone's own exits.
  Validate: g++ harness compile + 33-fixture green (no-regression) + BAT (behaviour). General:
  fixes every multi-camera-zone field, not just the classroom.

### v0.20.21 — topological camera-zone filter (BUILT, pending BAT)
Implemented the far-zone HIDING half. ComputePlayerZoneReachability() (field_catalog.inl, runs
once per RefreshCatalog): reads player live triangle (entity+0x1FA), BFS over
WalkmeshTriangle.neighbor[] refusing to cross an active SCREEN_BOUND segment (camera boundary =
wall), marks reachable set. Guard at ALL 6 push sites: hide an entry only if its triangle is
unreachable. Lines use 3-point test (center+both endpoints, keep if ANY in-zone) so the boundary
camera-pan transition is never hidden; a line wholly in another zone (far hallway exit) is.
FAIL-SAFE (#88): no walkmesh / unknown player-tri / oversize / unplaceable => KEEP. Kept the old
straight-line filter as a 2nd layer. Log-loud + a per-refresh "zone-filter active: player tri=N,
M/K reachable" confirmation. Self-contained ZONE_MAX_TRI (harness lacks MAX_TRI_ID). Validated:
g++ harness compile clean + 33/33 fixtures green (offline-neutral: fixtures set no live player
triangle -> s_zoneValid=false). General across all multi-camera-zone fields. BAT next.
Follow-up idea: add a 2-zone harness fixture to lock the flood-fill behaviour (currently only the
BAT exercises the active path).

### v0.20.22 — classroom polish (BUILT, pending BAT)
Fixed the two v0.20.21 BAT regressions + hardened exits:
- NUMBERING: interactions numbered only AFTER surviving filters (was numbered before -> "Interaction
  6 of 4" gaps when the zone filter dropped some). Provisional label in the else-block; real number
  assigned just before the push.
- ZONE-BOUNDARY EXITS: flood-fill now records s_zoneBoundaryLine[] (SCREEN_BOUND lines that wall the
  player's zone). A trigline-exit is kept if reachable OR it bounds the zone -> the camera-pan
  transition shows from BOTH zones; a far exit lying wholly in another zone stays hidden.
- EXITS NEVER ZONE-FILTERED: map-exits + gateways guards removed; with the boundary rule, no reachable
  exit is ever hidden (#88).
- SAVE POINTS exempt from zone filter. Interactions/objects/events still filter.
Compiles clean (g++), 33/33 harness green.
STILL OPEN (bgroom_1 is an opening-cutscene field, dense with scene machinery -- Director/doorcont/
scene-actor lines Squall_u/Zell_u/Selphie_u; entities 'door' ent18 has bogus tri=62925 so it isn't
placed):
  - Co-located dedup: hallway exit (SelphieDummy line5->139) + its "door" interaction should be ONE
    exit. Needs interaction-near-exit dedup that distinguishes "door that IS the exit" from a real
    interaction merely near an exit. (WS1.4)
  - Naming: Squall's-desk / scene interactions read as generic "Interaction N". (WS2)
  - Squall's desk visibility: v0.20.22 should show it when the player is in its zone + numbering
    fixed; BAT will confirm whether it now appears or is a detection gap.

### v0.20.23 — REVERT v0.20.20 (desk mislabel) [BUILT, pending BAT]
v0.20.22 BAT (Aaron): "Exit to Classroom 3" is actually SQUALL'S DESK. bgroom_1 line6 'Selphie'
is SCREEN_BOUND dest=234 BUT REQs Quistis/Students (hasDialogReqTarget=1) -> dialog-gated
interaction (press interact -> "...what a pain" ASK; walking in MAPJUMPs to bgroom_4=Classroom 3
as a scene consequence). The real hallway exit line5 'SelphieDummy' dest=139 has
hasDialogReqTarget=0 -> correctly an Exit. v0.20.20 keyed on "different-field dest" and wrongly
promoted the desk. FIX: removed IsCameraZoneTransitionLine + its 3 uses; dialog-REQ SCREEN_BOUND
lines are Interactions again (original rule). Zone filter (v0.20.21/22) untouched -- offline
survey confirms bgroom_1 has 2 camera zones (CAMERA_ANGLES.csv: "bgroom_1,2,...").
LESSON: I built v0.20.20-22 on a wrong model of this cutscene field. Verify entity FUNCTION
(hasDialogReqTarget, REQ-TARGET, what interact does) before labeling a SCREEN_BOUND line an exit.
OPEN: (a) the two "generic interactions in the back = walkways to the front?" -- identify the
lines/field (maybe bgroom_4/Classroom 3, entities=4). (b) name desk/scene interactions (WS2).

### v0.20.24 — camera-zone announcement + static-analysis answer [BUILT, pending BAT]
Aaron Q1 (static analysis): are the two generic back-of-classroom interactions the front/back
camera transitions? ANSWER = NO. offline/CAMERA_ANGLES.csv: "bgroom_1,2,..." = Classroom 1 has 2
cameras; all its captured lines (line0-7) are scene-actor trigger zones (Director/Squall/Zell/
Selphie) -- none is a camera-transition line. The front/back switch is the engine per-zone index
[0x1CE4906], NOT a scripted line. So nothing to relabel; announce the change instead.
Aaron Q2 (feature): field_announce.cpp now treats [0x1CE4906] as part of the screen identity
(like the prison floor). On a zone change with the field id unchanged, re-announces "<field name>,
camera <N>" (N = stable per-visit ordinal), debounced 800ms. Single-camera fields never fire.
SEH-guarded; logs [FieldAnnounce] camera-zone change. field_announce.cpp is a Windows TU -> NOT
harness-testable; coded to match its existing patterns.
BAT will also SETTLE: does bgroom_1 front/back fire the camera announcement (internal 2-camera
field) or is the front/back actually the bgroom_1<->bgroom_4 field change (already announced)?
Also bundles v0.20.23 (Squall's desk = interaction, not "Exit to Classroom 3").
OPEN: name desk/scene interactions descriptively (WS2, hard in a cutscene field).

### v0.20.24 BAT — camera-transition-as-exit: NO static source (design decision needed)
Aaron BAT: desk=interaction OK; camera-zone announce fires between front/back OK; BUT wants the
front<->back camera transition in the CATALOG as an EXIT (currently nothing / a generic interaction).
Investigated whether the boundary is statically derivable:
  - LineCamPan=0 in bgroom_1 -> NO scripted camera-pan line to re-type.
  - .id walkmesh is EXACTLY 10984 bytes = 4 + 366*30 (flat FF7-style tri+access) -> NO per-camera
    block / zone data in the walkmesh the mod parses.
  - type-13 lines are LINE_INTERACTIVE (scene actors), not transitions.
=> The camera zone [0x1CE4906] is computed by the ENGINE from player position at runtime; there is
   no static entity/line/geometry to convert into a catalog exit.
ONLY viable path = RUNTIME learning: the mod already reads the live zone index (drives the v0.20.24
announce). Record the crossing point (player pos + from/to zone) at the moment the zone flips, then
synthesize a navigable "Passage to camera N" exit at that point in the catalog for the relevant zone.
INHERENT CAVEAT: the exit appears AFTER the first crossing (first cross guided by the audio announce;
thereafter it is a catalog exit for the round trip). No way around it without static boundary data,
which FF8 does not put in the readable field files. Cross-module (observe/announce detect + record;
catalog synthesizes). Design + label ("Passage to camera N"?) pending Aaron's OK before building.


---

# Camera-Zone Transition Investigation (bgroom_1 / Classroom 1) — v0.20.25

Deep static RE of `bgroom_1.jsm` + `FF8_EN.exe` dispatch table + INF + CAMERA_ANGLES.csv.
This OVERTURNS the v0.20.24 premise (which assumed the front/back split is the engine
zone byte `0x1CE4906`). It is not.

## 1. The zone byte 0x1CE4906 is NOT the classroom front/back mechanism
- Only 3 instructions write `0x1CE4906`: field-init `0x00471FE7` (=0), the JSM opcode
  `0x11C` handler `0x005218E0` (pops value & 3), and reset `0x0052BE30` (=0).
- `bgroom_1` script data contains **zero** pushes of `0x11C` (opcode 0x11C is never
  invoked). So `0x1CE4906` stays 0 the whole time in the classroom.
- => the v0.20.24 "camera N" announce (reads 0x1CE4906) cannot fire in bgroom_1.
  Its earlier "confirmation" was mistaken.

## 2. What actually switches front/back
- `bgroom_1` has 2 cameras (CAMERA_ANGLES.csv: `bgroom_1,2,-62.49`).
- Front/back are two camera VIEWS switched by 4 walkmesh trigger LINE entities:
  `bgroom_1_jump01`, `bgroom_1_jump02`, `bgroom_2_jump01`, `bgroom_2_jump02`.
- Their trigger method REQ-chains into `cameraman` (methods room2open/room2close),
  which repositions the actors (characters' set1to2_/set2to1_ methods) and drives
  the camera. `SETCAMERA` (0x10A) itself appears only in the `cut` cutscene entity.
- Opcode 0x19 seen on the jump lines is a clamp/cast VM primitive (handler 0x51C990
  -> 0x51C9C0), NOT a camera op. Red herring.

## 3. RELIABLE static identification of camera transitions
- `jump`-named LINE entities are FF8's convention for free-roam camera transitions.
- Survey of ALL 45 multi-camera fields (identifying lines by accross/lineon method
  signature): 7 fields have jump-named lines: bgroom_1 (x4), doani1_1, doani1_2,
  glclock1, glclub1 (stagejump), glwitch1, glwitch3. The other 38 multicam fields
  have NO free-roam transition lines (their extra cameras are cutscene-only).
- So: a LINE entity whose name contains "jump" == a free-roam camera-view transition.

## 4. Entity naming/category BUG in the mod (affects bgroom_1 and any model-first field)
- `field_archive_jsm_scan.inl` maps `symIdx = e - countDoors`, assuming the SYM name
  table is in the SAME order as the JSM group table. It is NOT.
- JSM group table order = Door, Line, Bg, Other (categorization by index is correct).
- SYM name table order (bgroom_1) = Other/models(0-20), Line(21-28), Bg(29-35).
- Robust fix: identify SYM line entities by method signature (accross/touch/lineon),
  then map them positionally to the group Line block [countDoors .. +countLines).
  Verified: group Line 0-7 == SYM lines in same order.
- Consequence today: bgroom_1's real lines (group idx 0-7 = jump01/02, bgroom_2_jump01/02,
  door01, to_corridor, Cliant, BritinBoard) are mislabeled with model names
  (Director/Squall/...). The reported "hallway exit SelphieDummy dest=139" is actually
  `to_corridor` (MAPJUMP3 -> field 139). "Squall's desk / Selphie" mislabels likewise.

## 5. Geometry source (open)
- The mod captures line geometry ONLY via the SETLINE (0x39) opcode hook, reading
  entity+0x188. `bgroom_1` uses NO SETLINE, so the hook never fires -> the classroom's
  lines have NO captured geometry today (explains the vague classroom catalog).
- The jump lines set geometry in their `default` method via arithmetic expressions
  (opcodes 0x01=sub, 0x03=div, 0x04=mod over pushed coords) -- too fragile to re-derive
  statically. Endpoints for jump01 ~ (1610,416)-(1230,403); to_corridor ~
  (1496,3460)-(1341,3429) [matches its reported center 1418,-3444].
- Preferred: read each line entity's endpoints from ENGINE MEMORY at runtime (all lines,
  not just SETLINE ones). Needs the line-entity array base + the coord offset confirmed
  for non-SETLINE lines. SETLINE handler 0x525B60 stores <<12 fixed-point coords at
  entity 0x1AC..0x1C8; the mod's hook reads int16 at 0x188 (works for SETLINE fields).
  Which offset holds bgroom_1's jump-line coords is unconfirmed -> needs a BAT.

## 6. v0.20.25 build (observe-only)
- `[CAMBYTE]` watch in field_announce.cpp: logs any byte in 0x1CE4900..0x1CE4940 that
  changes within a field (baseline re-taken per field). BAT: cross front<->back in the
  classroom; the byte that flips at the crossing IS the active-view index. That byte
  fixes the announcement AND becomes the runtime front/back signal for the catalog.

## 7. Plan after the BAT identifies the byte
1. Point the camera announcement at the correct byte (so "camera N" works in bgroom_1).
2. Fix the SYM<->group line naming (signature-based positional map).
3. Detect camera-transition lines = LINE entity whose (corrected) name contains "jump".
4. Capture their geometry at runtime (engine memory) since SETLINE is absent.
5. Emit them as catalog EXITS, filtered by the existing zone-reachability so only the
   transition reachable from the player's current view is listed (back -> shows the
   transition to front, and vice versa).


---

# CORRECTIONS from the v0.20.25 BAT (supersede parts of the v0.20.25 section above)

The v0.20.25 `[CAMBYTE]` BAT (Aaron, field 0x00E8=bgroom_1) overturned two of my
static conclusions:

1. **0x1CE4906 DOES change front/back.** In the classroom it flips together with
   0x1CE4908 (56<->64) and 0x1CE4909 (0<->1) on every crossing, in a clean
   alternating pattern. My static "only 3 writers, 0x11C never fires" reasoning was
   wrong -- the zone-set reaches 0x1CE4906 via dynamic dispatch (the ext-opcode index
   is not a literal push, so my literal scan missed it). => 0x1CE4906 IS a reliable
   runtime front/back signal, and the v0.20.24 "camera N" announcement works.

2. **All 8 bgroom_1 lines ARE captured with geometry.** The SETLINE (0x39) hook DOES
   fire here (calls #14-21), even though the JSM has no high-byte-0x39 opcode -- the
   line coords are computed by the arithmetic in the default method and SETLINE is
   dispatched dynamically. Captured geometry (SETLINE order == group Line order):
     #14 bgroom_1_jump01 (1610,-416)-(1230,-403)   #18 door01 (1500,-3363)-(1337,-3341)
     #15 bgroom_1_jump02 (690,-533)-(475,-533)      #19 to_corridor (1496,-3460)-(1341,-3429) [hallway exit]
     #16 bgroom_2_jump01 (1655,-2175)-(687,-2014)   #20 Cliant (741,377)-(1198,375)
     #17 bgroom_2_jump02 (-102,-1996)-(-241,-1996)  #21 BritinBoard (1664,-3304)-(1664,-3171)

## The real root cause (confirmed by the mod's own [JSMScan] log)
SYM name order != JSM group order. The mod's log shows grp[0] methods=8 sym='Director'
but Director has only 3 methods (its count is wrong for the assigned name); grp[16]
methods=20 sym='Student4' but Student4 has 7 and Squall has 20. So symIdx=e-countDoors
mislabels everything in model-first fields. Group order = Door,Line,Bg,Other (category
by index is CORRECT); SYM order (bgroom_1) = Others(0-20),Lines(21-28),Bgs(29-35).
The real lines are group idx 0-7 (jump01/02, bgroom_2_jump01/02, door01, to_corridor,
Cliant, BritinBoard) -- mislabeled Director/Squall/...; the real names bgroom_1_jump01..
landed on Other entities (grp[21]=Selphie got named bgroom_1_jump01).

## v0.20.26 fix (shipped)
LoadSYMCategories() categorizes each SYM entity by method signature and the scanner
remaps names per-category (guarded by count-match; SYM==group fields unchanged).
Logs [SYMREMAP]/[SYMCAT]/[CAMXLINE]. Announcement already works (0x1CE4906).

## Remaining (next build)
Surface the 4 jump lines as catalog EXITS (they are already captured with geometry) and
use them as zone-boundary walls in ComputePlayerZoneReachability so the front hallway
exit (to_corridor) is hidden while the player is in the back. Detection = captured line
whose corrected name contains "jump".


## v0.20.30 - v0.20.33 (shipped, bgroom_1 classroom finished)
Aaron BAT'd the classroom to completion. State as of v0.20.33:
- Camera transitions (jump lines) surface as "Camera transition" EXITS, zone-filtered,
  and wall the zone BFS. Front-only hallway exit confirmed correct by Aaron.
- SYM naming remap correct (remap=1); phantom scene-actors dropped (no-live-slot rule,
  v0.20.31); Squall's desk labeled "Desk" (v0.20.32, numbering-pass exemption).
- v0.20.33: DOOR-OPEN TRIGGER CONSOLIDATION. In InjectInteractionLines, an interactive
  line whose corrected SYM name contains "door" AND that lies within 400 world units of a
  non-camera-transition SCREEN_BOUND exit line is dropped from the catalog. It is the
  door-open animation trigger, which fires automatically as the player walks toward the
  exit, so a separate "Interaction" entry is redundant. The exit is untouched. Dropped
  BEFORE the interaction-numbering pass so it never burns an "Interaction N" slot.
  bgroom_1: door01 (1418,-3352) vs to_corridor exit (1418,-3444) = 92 units -> suppressed.
  Rule is deliberately name+colocation (both required): name-only could hit an unrelated
  line, proximity-only could suppress a legit interaction near a doorway. Generalizes to
  door01/door02 triggers game-wide; a non-"door"-named trigger just stays listed (safe
  failure, never over-suppression). Sym lookup = s_jsmEntities[j] where jsmCategory==1 &&
  jsmIndex==s_jsmDoors+t (same map used by lineCurated/lineIsSave). Log tag [v0.20.33].


## v0.20.34 (shipped) -- classroom bulletin board labeled; signpost naming
Aaron expected 3 signs on bgroom_1's north wall; only 1 was catalogued ("Interaction 1").
DEFINITIVE TRACE (extracted bgroom_1 from field.fs, validated extractor against the known
36-entity .sym):
- ONE sign entity: BritinBoard (JSM line entity 7, ~1664,-3171..-3304). Its m2 reads var1026
  and 5-way branches to notices (msg IDs ~99..104): Tardiness / Magic-use / Garden Festival /
  cafeteria hot dogs / uniforms. NO player-position branch -> one interaction spot, one notice
  fixed by var1026 at the current story point (Aaron always saw "uniforms"; 2 screenshots agree).
- Scanned every entity's script for those message IDs: only BritinBoard displays them (NPC hits
  are dialogue; Bg entities cut/redlight/greenlight/doorlight/door/student1/student2 display NO
  messages -> the other wall panels are background art, non-interactive).
- CONCLUSION: mod was correct (1 sign, surfaced). Gap was naming only.
FIX: IsSignpostName() (field_catalog.inl, above InjectInteractionLines) -> curated label
"Notice Board" via lineCurated (same path as Cliant->Desk). Tight token set (substrings
britin/bulletin/noticeboard/signboard/billboard/signpost; exact board/sign/notice/kanban/keijiban)
to avoid keyboard/design/signal false hits. Naming only; exempt from the interaction-numbering pass.
GLOBAL follow-up options (not yet done): (a) survey all field .sym files to catalog sign entities
and curate labels; (b) pin the FIELD message opcode in the JSM so any interaction whose script
displays text auto-labels as Sign/Notice (robust, name-independent) -- needs a disassembly pass
(dispatch-table notes in the repo are battle-effect VMs, not the field MES opcode).
Signpost LINES are already surfaced as interactions everywhere -> not being missed; only unnamed.


## WS1 RESUMED (v0.20.35) — glwater3 target; SHOW/HIDE already done; field opcode table found
Target field: **glwater3** (sewer gate room), the canonical bloat example. Aaron has a save at the sewer entrance -> clean repeatable BAT.

FINDINGS re-grounding the plan:
- Live-state table CORRECTION: "Visible | SHOW/HIDE" is **DONE**, not pending. +0x160 **bit 3 (0x08) = HIDE**
  (SHOW opcode 0x60 @0x0051EAD0 `and ecx,~8`; HIDE opcode 0x61 @0x0051EB40 `or ecx,8`). HIDDEN-ENTITY
  filter (v0.18.3.269 #71) already drops bit-3 entities. Remaining live signal = **UNUSE/USE (active)** --
  a DIFFERENT +0x160 bit, still to decode.
- **FIELD opcode dispatch table base = 0x00B8DE94** (validated [0x57]=TALKON [0x58]=TALKOFF). handler(op) =
  *(0x00B8DE94 + op*4). Findable now for USE/UNUSE AND for issue #116's field MES opcode (mes/mesw/ames/amesw
  handlers 0x00528F20 / 0x00528E40 / 0x005291E0 / 0x00529020 -> find their indices in this table).
- glwater3 expected-set (from field.fs extract; JSM D=0 L=8 B=8 O=17, 33 entities):
  * REAL (expected in catalog): `hasigo` ladder (climb via ladline0-7), `drpoint` draw point, exits, and the
    `saku1..6` gates IF interactive; `book` (examinable?).
  * BLOAT (expected OUT): `water` (bg controller), `director0`, `ct_lf`/`ct_rt`/`ct_rt2`/`rt_up` (controllers),
    party/dream actors squall/zell/irvine/rinoa/selphie/quistis/laguna/kiros/ward (party-filtered).
  * Aaron to confirm the exact real-vs-bloat split during the BAT.

v0.20.35 (shipped, log-only): object [CAT-AUDIT] now logs each live other's `flags@0x160` (HIDE bit broken
out), `model@0x218`, `triangle@0x1FA`, `slot`. Bounds-checked, no SEH. Observe-first input for Step 1.3.

NEXT (Step 1.3): BAT glwater3 -> read [CAT-AUDIT] `live[...]` per surviving object -> identify which signal
gates each bloat entry -> decode UNUSE/USE bit (dispatch table 0x00B8DE94 if not obvious from flags) ->
build unified `CatalogEntryIsLiveNow(candidate)`; route every path through it (err toward KEEP, log every DROP).


## glwater3 (sewer) BAT -- v0.20.36 (Aaron): far-side + gate issues
BAT result: "a lot less messy" but 3 issues. Empirical data from the v0.20.35 audit + before/after F11 shots.
1. FAR-SIDE LADDER: 'hasigomodel' had live hide=1 (flags@0x160=0x1008280A bit3) but leaked because the
   HIDDEN-ENTITY filter (v0.18.3.269, +0x160 bit3) ran only on the entity-scan path, not JSM injection.
   FIX v0.20.36: apply the hide check in the JSM-injection phantom filter. GLOBAL. Reappears on SHOW.
2. DRAW POINT PHANTOM: real 'drpoint' (ent28) dropped (no live slot); the v0.12.12 no-position draw-point
   fallback then reclassified party/dream NPC ent3 (model 7) as "Draw Point" at a wrong reachable-looking
   spot. TODO: scope the fallback (don't fabricate a draw point from a character-model NPC / when the real
   dp isn't present).
3. REACHABILITY WINDOW: ComputePlayerZoneReachability (walkmesh-tri BFS, 36/235 reachable) is CORRECT and
   stable mid-visit; it's only OFF during the field-entry window (playerTri +0x1FA not ready yet), which is
   when far-side junk flashes in. Fix A's hide check covers the ladder there. TODO for other far-side
   entities: defer catalog until s_zoneValid, or seed playerTri earlier.
4. OPENABLE GATE (issue #3, located via F11 timing): scripted iron gate on the path to the Sewer 2 exit,
   opened by an action AT the gate (13s field-nav silence = scripted seq; waterwheel spins as mechanism).
   NOT a captured line, NOT a talk entity -- saku1..6 are animation-only bg; ct_lf/ct_rt/ct_rt2/rt_up are
   hidden controllers writing water/gate vars 337/339/340. TO SURFACE only-openable gates: identify the
   gate-state var + gate position (deep, location-specific). Open question for Aaron: does he press action
   AT the gate, or trigger it elsewhere (lever/waterwheel)? Field-nav log is silent during the scripted open.


## GATE MECHANISM CRACKED (glwater3, Aaron confirmed press-action-at-gate)
From [REQ-TARGET] + [MODELSIG] + [SET3-DIAG] in the BAT log:
- The visible gates are backgrounds `saku3`/`saku4` (trivial scripts: SHOW/HIDE animation only).
- Hidden CONTROLLERS drive them and sit AT the gates: `ct_lf` (tri 83) m2 REQ-> `saku3`; `ct_rt` (tri 138)
  m2 REQ-> `saku4`; `ct_rt2` (tri 147) m2 REQ-> `saku4`; `rt_up` (tri 181). They write water/gate vars
  337/339 and branch on `var 340`. (`book` is SET3-gated on var 340 == 9 -- var 340 is the section's
  water/gate STATE.)
- MODELSIG: ct_* have static talkon=0 / setline=0 (why the scan misses them) but ct_rt2 has pushrad63=1;
  the player opens the gate by press-action at the gate, so the interaction is enabled at RUNTIME
  (talkonoff/pushonoff at +0x24B/+0x249), not statically -- the observe gap.
- The mod CAN read the state var: `ReadVarBank()` (field_archive_jsm_scan.inl:59) and FIELD_VAR_TABLE_BASE
  + param*4 (field_dialog_expand.inl). So "openable NOW" = read var 340 and compare.

PLAN to surface only-openable gates (location-pattern, likely generalizes across glwater*):
1. OBSERVE (next build): log, per refresh, the runtime talkonoff/pushonoff + position of the ct_*/saku
   gate entities, and the live value of var 340 (337/339). BAT: Aaron approaches + opens a gate -> see
   which controller's interaction is enabled at the gate and which var value = openable.
2. SURFACE: emit a "Gate" catalog entry at the openable controller's position, gated on the var condition
   (err toward showing; drop logs the var it gated on). Reachability + hide filters already apply.
PENDING: confirm via the observe BAT before surfacing (don't guess the openable var value).


## MOVE-FIND / hidden draw+save points (Aaron insight, glwater3 draw point)
Aaron: the glwater3 "Draw Point" may not be VISIBLE to a sighted player -- FF8's Move-Find (Siren, party-wide
junction ability) is what reveals HIDDEN field draw points AND concealed save points. Confirmed via FF Wiki:
Move-Find uncovers hidden field draw/save points; without it they are invisible (world-map draw points stay
invisible regardless).
- glwater3 SPECIFIC: the surfaced "Draw Point" is the PHANTOM (fallback reclassified char-model ent3); the
  REAL drpoint (ent28) is already dropped (no live slot). So killing the phantom resolves glwater3's draw
  point -- Move-Find is not the cause HERE, but the insight is a GLOBAL rule for other fields.
- DESIGN DECISION (Aaron's call): for a genuinely HIDDEN draw/save point, do we
  (A) STRICT PARITY: surface it ONLY when Move-Find is equipped (matches what a sighted player sees), or
  (B) ACCESSIBILITY AID: always surface it (the catalog IS the blind player's sight; the visual sparkle is
      replaced by the catalog entry)?
  Mod thesis ("show what's real to the player right now") leans A. Pending Aaron.
- IMPLEMENTATION if A (global): (1) detect the draw/save point's HIDDEN flag in field data (verify format --
  FF8 draw-point struct / field section); (2) read whether any party member has Move-Find equipped (savemap
  ability check -- feasible, mod already reads game state). Gate hidden points on (2); visible points always show.


## DECISION: Move-Find = STRICT PARITY (Aaron)
Hidden draw/save points surface ONLY when Move-Find is equipped (match sighted play). Visible points always show.

KEY HYPOTHESIS to verify before writing Move-Find code:
- FF8 likely implements "hidden until Move-Find" by having the draw/save-point ENTITY set the engine HIDE
  flag (+0x160 bit3) when Move-Find is NOT equipped, and clear it when it is. If so, PARITY IS ALREADY FREE:
  v0.20.36's hide-filter (now on the entity-scan AND JSM-injection paths) drops the point when hidden and
  shows it when Move-Find reveals it. NO explicit equipped-check needed.
- VERIFY: examine a hidden-draw-point field's draw-point entity script for a Move-Find flag check that
  sets/clears HIDE (or SHOW/HIDE opcode 0x60/0x61). Good test field = one with a KNOWN hidden draw point,
  BAT once with Move-Find off, once on, read the [CAT-AUDIT] live[flags@160 hide=] for the draw point.
- IF FF8 uses a SEPARATE Move-Find flag (draw point reads it without touching HIDE): add explicit gate ->
  read party equipped-abilities for Move-Find (ability id; savemap ability list), suppress hidden dp/sp unless present.

NEXT BUILD (two items):
1. Phantom-draw-point fallback fix (v0.12.12 no-position fallback grabbing a scene character in glwater3).
   Needs: understand why 'saku4' classifies as ENT_DRAW_POINT + why the fallback picks char-model ent3;
   cleanest guard likely = don't fabricate a draw point when the real dp entity isn't live-present, and/or
   never reclassify an active party/scene character model. (Careful: Fire Cavern dp legitimately uses model 9.)
2. Move-Find parity: verify hide-flag hypothesis (above); implement only if not already covered by hide-filter.


## v0.20.37 (glwater3 BAT) -- phantom Draw Point fixed; v0.20.36 confirmed
- v0.20.36 CONFIRMED: hide-filter on JSM injection drops far-side ladder (hasigomodel) + hidden 'book'
  (both flags@0x160 bit3=1). Ladder issue #1a resolved.
- PHANTOM DRAW POINT root cause: gate 'saku4' (cat=2 background, ent12) is DRAWPOINT-typed via a DRAWPOINT
  opcode in its gate-controller script -> jt=ENT_DRAW_POINT, hasPosition=0 -> the v0.12.12 no-position
  fallback relabeled scene char ent3 (model 7) "Draw Point" at a wrong reachable spot. Real 'drpoint'
  (ent28, cat=3) is correctly dropped (no live slot).
  FIX v0.20.37: no-position draw-point fallback now SKIPS cat=2 backgrounds (real positionless draw points
  are cat=3 others). Phantom gone; no draw point shows in glwater3 now (correct -- real one not present/reachable).
STILL OPEN: (a) openable-gate surfacing (ct_* REQ saku, var 340 gate -- needs observe pass);
  (b) Move-Find parity verify (hide-flag hypothesis).


## v0.20.38 (glwater3 BAT) -- unreachable item pickup dropped; far-side cleanup COMPLETE
- v0.20.37 confirmed: phantom draw point gone. But it had MASKED a real entry: ent3 is a genuine hidden
  ITEM PICKUP (isItemPickup, tri 175, far side) -- surfaced as "Item 1", unreachable.
- ROOT: the walkmesh zone-reachability filter runs on the trigger/interaction/event/JSM-object paths but
  NOT the runtime-entity path (fresh[] adds at ~4157/4166). So an unreachable runtime pickup/NPC survives.
- FIX v0.20.38 (surgical): in the item-pickup relabel pass, drop a pickup whose triangleId is not in
  s_zoneReachable (zone-valid). Scoped to ITEM PICKUPS (must-walk-to) -- deliberately NOT all runtime
  entities, because a conversational NPC can be talkable across an unreachable gap and a plain
  walkmesh-reachability test would over-drop it (violates principle #2). Item returns when zone grows.
- FAR-SIDE TRIO now all handled: ladder (hide, v0.20.36), draw point (phantom, v0.20.37), item (reach, v0.20.38).
- FUTURE (general runtime-entity reachability for NON-item entities): needs a talk-range-aware test
  (keep if within talk_radius of a reachable triangle) before filtering talkable NPCs. Deferred until a
  real case appears.


## GATE MECHANISM -- STATIC DECODE (glwater3 JSM, opcodes confirmed vs FF8 field-script ref)
Opcodes: 0x0A PSHM_B (push var byte), 0x0B POPM_B (pop var byte), 0x0C PSHM_W (push var word), 0x01 CAL
(param 6==, 12=AND, 13=OR), 0x02 JMP, 0x03 JPF, 0x07 PSHN_L, 0x14 REQ, 0x17 PREQ, 0x1E SET3 (pos+tri),
0x2B SETMODEL, 0x1C HALT, 0x1F IDLOCK.

MECHANISM:
- director0 m1 = state RENDERER (no player input). Branches on var340 in {0,3,9} = the section macro
  puzzle-state (runtime: glwater3 var340=9 later / 0 at start; glwater1 var340=10). Within each var340
  branch it reads BITS of var337/338/339 and REQs (op14) each gate's open/closed visual.
- var337/338/339 = GATE-OPEN BITMASKS (one bit per gate).
- Controllers sit AT their gate (SET3 tri): ct_lf=83, ct_rt=138, ct_rt2=147, rt_up=181 (world coords in the
  op07 triple before SET3). Each toggles ONE bit when operated (op0A var, op07 mask, op01(12) AND-test,
  op01(13) OR-set, op0B var):
    ct_lf  -> var337 bit2 (mask 4)    [guard var340==0]
    ct_rt  -> var337 bit7 (mask 128)  [no var340 guard]
    ct_rt2 -> var339 bit0 (mask 1)    [guard var340==0]
    rt_up  -> var339 bit1 (mask 2)    [guard var340==0]
- saku3/saku4 (the visible gates) are animation-only (SHOW/HIDE via HALT dispatch); REQ'd by the controllers/director.
- Controllers have NO TALKON -> the player's press-action is NOT handled by the controller directly; the
  director/engine routes it (unresolved: exact action->toggle path). Last BAT (start save, var340=0,
  var337=1, var339=64) showed NO bit flip on the reported open -> either that gate's openable condition
  wasn't met, or the player wasn't at a controller, or a different trigger path. v0.20.40 whole-bank observe
  will confirm the exact bit on a real open.

SURFACING PLAN (buildable now): "Gate" entry at each controller's tri-centroid, shown when OPENABLE =
(var340 == controller's guard value) AND (gate bit currently CLEAR). Reachability/hide-filtered. Generalizes
across glwater* by the ct_*/rt_* + var337/339/340 signature (extract each field's controller table).
Sources: FFRTT wiki FF8/Field/Script/Opcodes; qhimm-modding wiki.


## GATE PIVOT -- gates ALREADY implemented (#85), regressed to position-less; re-anchor on controllers
DISCOVERY (from the ff8_accessibility.h version history + current BAT log): the sewer gates were built in
the #85 arc (v0.18.3.285-297): saku1/2/3 promoted to Interactive Objects, surfaced as "Gate 1/2/3",
STATE-GATED on varblock 0x0154 (var340) with BITMASK semantics (glwater2: saku2==25/saku3==13/saku4==16;
glwater3: saku3==3 + ladder pair 9/0). My v0.20.39/40 observe + static decode independently rediscovered the
SAME mechanism (var340 = section state; var337/339 = gate bits) -- consistent, not wasted.
WHY GATES ARE MISSING NOW: current BAT [PUZZLE-DIAG] shows saku1..6 hasPos=0 pos=(0,0) tri=0 -- the saku have
NO resolvable position in this save state, so they cannot be catalogued. The saku's OWN scripts are trivial
(empty init / HALT-dispatch, NO SET3). Their positions in #85 were borrowed from elsewhere (struct/live).
KEY RECONCILE: the CONTROLLERS carry the real SET3 positions -- ct_lf tri=83, ct_rt=138, ct_rt2=147, rt_up=181
(op1E/SET3) -- and REQ the saku visuals + toggle var337/339 bits (guarded by var340). So the reliable gate
ANCHOR = the controller triangles (centroids), NOT the position-less saku.
FIX PLAN (v0.20.41): surface a "Gate" at each controller's tri-centroid, shown when its var340 condition holds
AND its gate bit is clear (openable), reachability-filtered. Controller posX/posY may be 0 (SET3 X/Y were lit 0)
-> use the walkmesh triangle CENTROID for position, not posX/posY. STATE-GROUP (#85) currently evaluates only
book/hasigomodel/drpoint -- the saku/controllers aren't flowing through it now, which is the regression to fix.
NOTE: this supersedes the from-scratch framing; reuse #85's var340 state model + the controller positions.


## GATE RESOLUTION -- v0.20.41 (glwater3), verified by direct JSM disassembly
CORRECTION TO EARLIER "GATE MECHANISM -- STATIC DECODE": that section used a WRONG opcode table
(0x0A=PSHM_B, 0x07=PSHN_L, JPF=0x03 ...). It is SUPERSEDED. The mod's own authoritative table lives in
src/field_archive_jsm_constants.inl: SET=0x1D, SET3=0x1E, SETMODEL=0x2B, SETLINE=0x39, REQ=0x14,
REQSW=0x15, TALKON=0x57, MES=0x47, SHOW=0x60, HIDE=0x61, RET=0x04; pushes PSHM_W=0x07, PSHM_B=0x09,
PSHM_L=0x0A, PSHSM_W=0x0C; JPF=0x02; 0x1C = extended-dispatch prefix (pops ext opcode off the stack).
The wrong table is what produced the "no bit flip" dead end last cycle.

EXTRACTOR (works, on device): /tmp/ff8_gate.py extracts glwater3.jsm/.sym from the nested archive
(field.fi/fl/fs -> per-field glwater3.fi/fl/fs -> inner files). LZSS: 4096 ring, start 0xFEE, flag LSB-first
(1=literal,0=2B backref off=b1|((b2&0xF0)<<4) len=(b2&0xF)+3), compressed entries have a 4-byte length header.
/tmp/ff8_dis.py disassembles by raw group index. Validated: glwater3.jsm header (Door0/Line8/Bg8/Other17 =
33 entities, posFirst=74, posScripts=480) matches the runtime PUZZLE-DIAG entity list exactly.

WHAT THE GATE IS (glwater3). The gate is operated at a CONTROLLER entity, NOT the saku visual. Four
controllers, each with its own model + SET3 triangle, each REQ-firing a saku visual:
  ct_lf  tri 83  -> saku3   (THE blocking gate to Sewer 2; = the tri-83 spot Aaron confirmed in #85 .289/.290)
  ct_rt  tri 138 -> saku4   (saku4 is the Director/coordinator, not a visible gate -> ct_rt may be a valve)
  ct_rt2 tri 147 -> saku4   (pushrad63=1)
  rt_up  tri 181 -> saku5
director0 (ent29) REQs saku1..6 in state-dependent branches = the per-state renderer.
saku3 method0 decoded: SETMODEL; PSHM_L 340 (var340); PSHM_W 3; 0x01 param=6 (compare); 0x03 (branch);
SET3(place) ... UNUSE(remove). So each saku tests var340 to decide present/absent. (Operator 6 of 0x01 is
still not disambiguated == vs & -- did NOT need it; see below.)
The saku visuals have NO reliable runtime position (out of the engine's 9-slot active window; live reads 0),
which is why the #85 "Gate 1/2/3" surfacing went invisible. The CONTROLLERS are the reliable anchor:
ct_lf resolves to tri 83 via the #85 TRI-CENTROID fallback (SET3 X/Y are lit 0 so only the triangle is real).

THE REAL REGRESSION (both filters post-date #85, both in the JSM-object injection path in field_catalog.inl):
  (1) v0.20.0 junk-gate (~2181): drops a marker-positioned Object with no interaction zone, no own
      interaction, AND no curated name. Controllers had no curated name -> dropped.
  (2) v0.20.31 scene-actor phantom (~2224): drops any cat-3 Object whose LIVE slot reads tri=0/pos=0.
      An out-of-window gate reads exactly that, though its STATIC SET3 tri (83) is real -> dropped.
Reachability (ZoneReachablePoint ~2511) was never the problem; it runs AFTER both and is what we WANT.

FIX v0.20.41 (two edits, both feed the existing reachability filter):
  (a) entity_classifications.h: ct_lf/ct_rt/ct_rt2/rt_up -> "Gate" (clears junk-gate; same as saku="Gate N").
  (b) field_catalog.inl phantom filter: EXEMPT (jsmNamedObject && je.hasPosition && je.posTriangle != 0)
      from the live-0 drop. Verified safe: ResolveTriangleCentroidPositions sets hasPosition + KEEPS
      posTriangle (83) for ct_lf; Selphie/Quistis are not curated gates and have no static SET3 so they
      still drop. Adds a [refresh] "KEPT despite live tri=0" log line.
NET at sewer-start (player tri 77, 36/235 reachable): ct_lf (tri 83) surfaces as "Gate"; ct_rt/ct_rt2/rt_up
(tri 138/147/181, far side) reachability-dropped; saku unchanged. Exactly the one openable blocking gate.

OPENABILITY MODEL: reachability is the working proxy (can only open a gate you can reach; the water level that
gates progress also gates walkable triangles). NOT yet handled: a gate reachable but ALREADY open -> the
var340 test on its saku (decoded above) is the future signal to hide it. Generalises across the 4 gate fields
by the ct_*/rt_* + curated-name pattern; labels beyond ct_lf (gate vs valve) to confirm as they become reachable.
STATUS: static-analysis + edits done, NOT MSVC/BAT-d. Awaiting Aaron's sewer-start BAT.


## SEWER MAZE -- offline simulation of all 6 fields (v0.20.42)
TOOLING (durable, in this folder): sewer_maze_extractor.py (imports extract_walkmeshes.py's proven
.id/.ca parser; pulls walkmesh+JSM+SYM for glfuryb1/glwater1-5, computes walkmesh connected components)
and sewer_maze_island_sim.py (maps gates + observed player positions to islands per field).

MAZE TOPOLOGY (field IDs + ->glwitch1 confirmed from BAT-log MAPJUMP fires):
  glwater1(762) -> glwater2(763) -> glwater3(764) -> glwater4(765) -> glwater5(766) -> glwitch1(767=Gateway)
  (glfuryb1 = Sewer 1 precedes; glwater1=Sewer 2 ... glwater5=Sewer 6.)

ISLAND STRUCTURE (why strict reachability is correct): every field is several DISCONNECTED walkmesh
components; the player enters one and the waterwheel(suisha)/gates bridge to the rest. Reachability BFS
already gates per-island, so a gate cannot surface until its island is reached. Gate->island map:
  glwater2: saku2 tri69, saku3 tri48, saku4 tri96 -- all island 0 (main). (curated "Gate N" already)
  glwater3 (4 islands): ct_lf tri83 = START island 1 (the blocking gate to Sewer 2, 36 reachable);
           ct_rt tri138 / ct_rt2 tri147 / rt_up tri181 = MAIN island 0 (across the water).
  glwater4 (5 islands): ct_lf_dw tri59 + lf_up tri73 = island 1; ct_rt_dw/ct_rt_dw2 tri194 + rt_up tri199
           = island 2; ct_lt_up tri165 = island 0.
  glwater5 (5 islands): ct_rt_up tri146 island 0 (+ ct_rt_dw). Waterwheel 'suisha' in glwater4/5.
  glwater1: seigyo (control).
Every gate lands on a real, player-visited island (verified vs the BAT log's per-visit reachable sets),
so the maze is followable island-by-island.

TWO ROOT CAUSES of the v0.20.41 "hit or miss" (both from the fresh BAT log):
  (1) scene-actor filter's oirP>=ocnt drop fired BEFORE the v0.20.41 tri=0 exemption. Sewer gates are
      ALWAYS out of the active window (ocnt = nearest ~8-9), and ocnt fluctuates 8<->9 -> gate appeared
      only in some scenes. FIX v0.20.42: exempt curated static gates from the oirP>=ocnt drop too
      (skip the live-slot read there -- no slot exists), guarded by `if(!keptOOW)`.
  (2) controllers have DIFFERENT names per field; v0.20.41 only curated glwater3's. FIX: enumerated the
      COMPLETE control set from every glwater*.sym -> "Gate": ct_lf_dw, lf_up, ct_rt_dw, ct_lt_up,
      ct_rt_dw2, ct_rt_up (glwater4/5), seigyo (glwater1). (glwater2 = saku, already curated.)
OPEN ITEMS: (a) a gate reachable-but-already-open isn't hidden yet (out-of-window has no live HIDE slot;
future: var340 state on its saku). (b) glwater4 ct_lt_up island 0 wasn't visited in this BAT -- confirm it
is reached in the solution. (c) suisha/waterwheel surfaces as an existing Line interaction (Aaron confirmed
in glwater3); verify it lists in glwater4/5. STATUS: edits done, NOT MSVC/BAT-d.


## SEWER BAT v0.20.42 -> v0.20.43 fixes (glwater4 missing gates + phantoms)
Diagnosed from the fresh BAT log (F11 placemarker 19:49:22 = glwater4, player tri223) + the island sim:
- MISSING GATES: the two gates on the player's island (ct_lf_dw own tri59, lf_up own tri73, both
  walkmesh-reachable from tri223) were dropped by the v0.20.36 HIDE filter reading an ALIASED live slot
  (STRUCT-POS: "struct tri=199 disagrees with own SET3 tri=59"; the aliased flag 0x1008200A = a hidden
  party member, same as quistis). The position passes already guard this aliasing; the HIDE read did not.
  FIX v0.20.43: duplicate-slot guard on the HIDE read -- trust +0x160 bit3 only when slot tri == own SET3 tri.
- PHANTOM LADDERS: the out-of-window exemptions used jsmNamedObject (ANY curated), which kept hasigomodel
  ("Ladder", state-dependent). FIX: scope the exemptions to jsmIsGate (display name starts with "Gate" =
  the always-present controls). hasigomodel now only lists when its own live slot is present.
- LADDER-AS-ITEM: isItemPickup keyed on a non-init savemap write; a sewer ladder writes its own used-flag
  in a non-init method, so ladline entities got relabeled "Item N". FIX (classify.inl): exclude foundLadder
  from isItemPickup (a LADDER is never a collectible; no glwater3 entity has a real ADDITEM).
- DRAW POINTS: deferred to issue #117 (Move-Find parity needs the equipped-ability read).
STATUS v0.20.43: edits done, NOT MSVC/BAT-d.


## SEWER BAT v0.20.43 -> v0.20.44: maze NAVIGABLE; ladder-as-item fixed; post-battle filed
Aaron: full maze navigable with gates+interactions announced (core goal met). Residuals:
- LADDER-AS-ITEM (fixed v0.20.44): the culprit is 'hasigomodel' (shortcut-ladder MODEL), curated "Ladder",
  posTri 175. It has NO LADDER opcode (model + non-init state-flag write) so v0.20.43's foundLadder guard
  missed it, and it stayed isItemPickup. FIX: the item relabel now inspects the matched JSM entity -- a
  generic runtime "NPC" sitting on a CURATED entity adopts that name ("Ladder") instead of "Item". Only
  touches generic "NPC" entries.
- POST-BATTLE LOSS (issue #118): SETLINE trigger-line geometry is captured once on entry, memset on re-init,
  NOT rebuilt after a battle -> LINE-PAIR captured=0 (glwater3 8->0, glwater2 6->0) -> exits vanish. Wrong
  "Presidential Residence" name is the same lifecycle event. Fix options in #118 (preserve/restore captures,
  or read live line-entity structs). Separate field-lifecycle subsystem.
- PHANTOMS: down to the draw points (issue #117, Move-Find parity).
STATUS v0.20.44: edits done, NOT MSVC/BAT-d.


## POST-BATTLE EXIT LOSS -- fix v0.20.45 (issue #118)
Trigger lines are captured by the SETLINE hook on field entry into s_capturedLines (CapturedTriggerLine[32]),
cleared on every HookedFieldScriptsInit (field_nav_fieldscripts.inl:55 memset). Post-battle the engine re-inits
field scripts (clearing) but does NOT re-fire SETLINE -> [LINE-PAIR] captured=0 -> exits vanish (glwater3 8->0,
glwater2 6->0). FIX (RefreshCatalog, field_catalog.inl ~3798, before ComputePlayerZoneReachability@4274 + exit
injection): back up the last non-empty capture tagged by *pCurrentFieldId; restore it if a same-field refresh
finds count==0. pCurrentFieldId is battle-stable (the battle-pause/resume machinery in field_nav_battlepause.inl
keys on it). Dedupe-by-entity-address (stable per field) prevents doubling if SETLINE ever re-fires. The
"Presidential Residence" post-battle mislabel appears in the log only as a real exit dest / transition, not a
wrong field name (name derives from pCurrentFieldId); likely the empty-exit fallback, should clear with the
restore -- re-check in BAT. STATUS: edits done, NOT MSVC/BAT-d.

## DRAW POINTS -- proper cataloging v0.20.46 (issue #117), from exe RE
RE (FF8_EN.exe): dispatch table 0x00B8DE94; [0x155] SETDRAWPOINT -> 0x00523030 reads the drpoint entity's
own pos (0x190/0x194) and calls createDrawPoint 0x00474750, which writes world X/Y to 0x01CDC620/0x01CDC622
and sets render-enable byte 0x01CE0750 (the visible sparkle). No Move-Find gate in the sparkle renderer
(0x00475170); visibility == whether SETDRAWPOINT ran. A draw point's SET3 + SETDRAWPOINT are in ONE
conditional block: glwater3 drpoint is STATE-GATED on var340 (`PSHM_L 340; PSHM_W 3; CAL; JPF -> SET3(54);
SETDRAWPOINT`), glroad1 is unconditional. Archive scan: 30+ fields have a drpoint, ALL exactly 1 per field.
FIX: IsDrawPointLivePresent(px,py) reads render-enable + sparkle pos; surface a draw point only when the
sparkle is active AND at this entity (position match rejects stale cross-field sparkle; SEH, fail-open).
True parity (shows iff the game draws the sparkle) -> auto-handles state-gating + any script Move-Find gate.
Non-drpoint fields have no drpoint entity so the gate never runs -> structurally no phantom. [drawpt] log
line each refresh for BAT tuning. RE tooling: capstone/pefile in cloud /home/claude/re/ff8.exe. STATUS: NOT BAT'd.

---

## v0.20.47 — Draw-point Move-Find parity (verified RE, 2026-08)

Aaron's requirement: a hidden draw point must appear in the catalog only when Move-Find is equipped, exactly as it becomes visible to a sighted player. v0.20.46 gated on the sparkle render-enable byte, which turned out to be wrong. Full disassembly-verified chain (FF8_EN.exe, base 0x400000):

**Render decision.** Renderer `0x00475170` loops the 8 sparkle particle slots and draws a slot's graphic only when `state==2 || (state==1 && cfg==1)`, where `state` = byte[0x01CE0750+slot] and `cfg` = word[0x01CDBFEA]. `test al,al / jbe` skips a slot whose state==0.

**`state` is a flicker counter, not a presence flag.** `createDrawPoint 0x00474750` writes `0x01010101` to 0x01CE0750/0x01CE0754 (all 8 slots = state 1) and stores the entity world X/Y/Z (`entityX>>12`, i.e. `/4096`) into every slot's position field (0x01CDC620/622/624, stride 0x18). The renderer increments a per-slot animation counter and resets state→0 when it overflows; `state_retrigger 0x00474970` sets state→1 whenever it reads 0. So state oscillates 0/1 as the sparkle pulses and is **never 2** for a draw point. => the render decision reduces to **`cfg==1`**. (This is why v0.20.46's `state!=0` gate was both flickery and blind to Move-Find.)

**`cfg = param | drawFlag`.** SETDRAWPOINT handler `0x00523030`: pops the script param to G+0xF1, calls createDrawPoint unconditionally, then `cl=G[0xF1]` (param), `dl=G[0x58]` (drawFlag), `or ecx,edx`, `configDrawPoint(param|drawFlag)`. `configDrawPoint 0x004747A0` just stores its arg to word[0x01CDBFEA]. VISIBLE draw points push `param=1` (cfg always 1); HIDDEN push `param=0` (verified bgeat1a) so `cfg = drawFlag`.

**`drawFlag` (G+0x58) IS Move-Find, recomputed live.** G is the field-script global block, `mov [0xB8EE90],0x1CFE9B8` => G=0x01CFE9B8, so G+0x58 = 0x01CFEA10. It is written only at `0x0052B7FD` (`mov [ecx+0x58],al`), and that same function (`0x0052B7EC`) runs each tick: `al = (byte[0x01CFF6D8] >> 4) & 1`, and immediately (if G[0xF0]!=0, i.e. a draw point is active) re-calls configDrawPoint with `G[0xF1] | G[0x58]` — so **cfg is refreshed every tick and follows Move-Find live**, not just latched at field entry. byte[0x01CFF6D8] is the field-ability bitfield, assembled at `0x0049565C` by OR-ing a per-ability flag (table 0x01CF7F2D, stride 8) across the party's 4 equipped abilities in the field-ability id range 0x4E–0x52. Bit 4 (0x10) of it = Move-Find.

**cfg is single-writer/single-reader.** `.text` scan: word[0x01CDBFEA] is written only by configDrawPoint (0x004747A5) and read only by the renderer (0x004751D0). Reading it in the catalog is therefore exact parity with the game's own render gate.

**Position units.** Sparkle X/Y = `entityX>>12`. Every catalog `posX/posY` source is the same `>>12`/`/4096` world unit: live-struct LATE-RESOLVE (field_catalog.inl:586), STRUCT-POS (:747), fieldscripts (:571), and SET3 capture (field_nav_opcode_hooks.inl:275, `fpX/4096`); triangle centroid (:949) is walkmesh world units, comparable within the 300 tolerance. So the `|sparkle−entity|<300` match is units-correct: ~0 for the active field's own draw point, large for a stale sparkle from a previous field.

**Implementation (IsDrawPointLivePresent, field_catalog.inl ~2082).** `present = (cfg==1) && (dx<300) && (dy<300)`, SEH-guarded, fail-open. Gate applied only to `ENT_DRAW_POINT` entries (InjectJsmSpecials ~2136), so non-drpoint fields structurally cannot phantom. Net: visible → always shown; hidden → shown iff Move-Find equipped; state/Move-Find-gated & un-run → absent. `[drawpt]` log prints state/cfg/sparkle/entity/d each refresh for BAT tuning.

**Residual (BAT-observable via the log):** the 300-unit tolerance could in principle admit a stale cross-field sparkle whose old world coords coincidentally land within 300 of this field's drpoint entity; unlikely (independent per-field coordinate spaces) and visible in the log as a large-but-<300 `d=`.

---

## v0.20.48 — Draw points: use the sparkle position, not the entity position (BAT fix)

v0.20.47 BAT (Aaron): two fields with visible draw points, neither appeared. The [drawpt] log was decisive and vindicated the cfg RE:
- `otokun01`: state=1 cfg=1 sparkle=(-1695,-3948) entity=(-1260,-1526) d=(435,2422) -> absent
- `zells`:    state=1 cfg=1 sparkle=(1495,1261)  entity=(0,0)        d=(1495,1261) -> absent

So cfg==1 correctly fired (the game WAS drawing the sparkle), but the v0.20.47 position MATCH (|sparkle-entity|<300) rejected it: the drpoint entity's *catalog* position is unreliable — `otokun01` resolved 435/2422 world-units from the sparkle, `zells` never resolved (0,0). The drpoint entity is a script-only object; the catalog's SET3/live-struct/centroid resolution (built for NPCs) does not reliably locate it.

**Ground truth = the sparkle.** createDrawPoint writes the drpoint entity's own position (`entityX>>12`) to 0x01CDC620/622; that IS where the visible draw point sits. So stop trying to *match* the entity position — *adopt* the sparkle position.

**New IsDrawPointLivePresent(sym, &outX,&outY,&outTri):**
1. presence = `cfg==1` (unchanged; full Move-Find parity — cfg = SETDRAWPOINT_param | Move-Find, recomputed live per the v0.20.47 RE).
2. stale rejection = the sparkle must lie on THIS field's walkmesh: `IsInsideWalkmesh(sparkle)` OR within 600u of the nearest triangle (`NearestWalkTriangle`). A stale sparkle left by a previous field whose SETDRAWPOINT did not run (the state-gated glwater3 case) sits at the previous field's coords — off this walkmesh — so it is rejected here instead of by a fragile entity match. Fail-open if no walkmesh (trust cfg alone).
3. on success, output sparkle (x,y) and its triangle.

**Gate site (InjectJsmSpecials):** on present, overwrite `s_jsmEntities[j].posX/posY/hasPosition/posTriangle` with the sparkle values, so every downstream position-dependent branch (and the EntityInfo, which derives position from triangleId) places the draw point at the real spot.

**Two supporting changes:**
- The legacy "no interactive entity near the JSM draw point -> reclassify the nearest NPC as the Draw Point" heuristic (a workaround for the old wrong positions) is capped to an NPC within 300u of the true sparkle. Since it only ran when nothing was within 300u, the cap makes it inert: a standalone sparkle now injects a standalone Draw Point at its own position.
- Draw points are exempt from the camera-zone reachability filter: cfg==1 + on-mesh already equals "a sighted player sees this sparkle."

**state==2 note:** contrary to the v0.20.47 assumption that state is only 0/1, a separate particle routine (0x00474872) does set state=2, and the renderer draws unconditionally on state==2. That is a transient draw-burst mode, not the steady sparkle (createDrawPoint sets state=1), so the catalog gates on cfg==1 and only logs state.

---

## v0.20.49 — Balamb Hotel: phantom 2nd save point + suppressed magazine (v0.20.44 relabel over-reach)

BAT of v0.20.48 (draw points all working) surfaced two Balamb Hotel regressions. Log dive (bchtr_1):
- `[dedup] curated-name relabel: ent6 tri=45 'NPC'->'Save Point' [v0.20.44]` -- a party member (Quistis, a cutscene runtime NPC) reading the save point's position (fp -> 355,-336, tri 45) got renamed "Save Point" -> catalog showed TWO save points (real SETLINE saveline0 + this phantom).
- The magazine (`ent12 'Buki1'`, MODELSIG pickup=1 nonInitWr=1 dialog=0 setmodel=1 -> isItemPickup) sits at tri 27, which ALSO holds the curated `'Irvine'` interactive object. The v0.20.44 loop matched 'Irvine' first (`ent4 tri=27 'NPC'->'Irvine'`), adopted it, and broke before ever checking Buki1's isItemPickup -> the magazine never surfaced.

**Both are one bug.** v0.20.44's curated-name relabel adopted the name of ANY JSM entity sharing the NPC's triangle, and did it BEFORE the item-pickup check. It was added narrowly (the glwater3 'hasigomodel' ladder is an isItemPickup false-positive that must read "Ladder"), but as written it let any curated non-item (a save point, an 'Irvine' object, a gate) hijack a coincident runtime NPC and, worse, pre-empt a real item pickup on the same triangle.

**Regression timing confirms it:** the 'Irvine'/'Save Point' adoption IS v0.20.44. Before v0.20.44 this pass went straight to the isItemPickup check, so the magazine read "Item" and no NPC-on-the-save-triangle became a save point.

**Fix (field_catalog.inl ~3556):** gate the whole loop on `isItemPickup` -- only an item-pickup JSM entity may claim a generic runtime NPC. A non-pickup entity can no longer hijack it. Curated-name adoption is kept ONLY for a pickup that is itself a curated non-item (the ladder). Net: hotel shows ONE save point + the magazine as "Item"; ladder ("Ladder") and the working bccent_1 item ("Item 1") unchanged. Save points themselves were never broken -- no save-system deep dive was needed; the relabel over-reached.
