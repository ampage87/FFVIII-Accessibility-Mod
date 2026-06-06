# FF8 FMV Scene Reference

This file documents the confirmed scene content of each FF8 FMV file (disc00_XXh.avi etc.)
for use when writing audio description VTT files.

Built from: direct frame verification (ffmpeg frame extraction + visual inspection) +
game walkthrough research (jegged.com, nightsolo.net, gamerguides.com, finalfantasy.fandom.com).

## Key Facts

- Files are named disc0X_YYh.avi (disc 0-3, sequential number, h = high-res)
- Files are roughly in narrative/game-order within each disc but not strictly so
- All original VTT descriptions in this project were AI-generated and WRONG for most files
  (descriptions were assigned to the wrong video files systematically)
- The h/l suffix: h = high res, l = low res (same scene, two quality versions)
- Frame rate is approximately 15fps for most files (so 300 frames ≈ 20 seconds)

## TTS Pacing Rule

At ~2.5 words/second TTS rate:
- 3 sec window = max ~7 words
- 5 sec window = max ~12 words
- 8 sec window = max ~20 words
- 10 sec window = max ~25 words
Always err on the shorter side — it's better to finish early than to be cut off.

## Timestamp Format

Short files (under ~1 min) use MM:SS.mmm format.
Longer files use HH:MM:SS.mmm format.
Both are valid WebVTT. Check the existing cues in each file to match the format used.

## Important: Original VTT Files Were Wrong

Every file reviewed in batch 1 (00h-09h) and batch 2 (10h-19h) had descriptions
for entirely the wrong scenes. The AI that originally generated them appears to have
assigned descriptions in the wrong order. Always verify with frame extraction before
trusting any existing description.

---

## DISC 0 — Confirmed Scene Index

### disc00_00h (~22s) ✅ DONE
Balamb Garden interior tour. Camera: corridor floor → elevated walkway → open
courtyard with trees and cadets → looking up at the ornate central hub against blue sky.
NOT an exterior shot. This is the first thing Squall sees after leaving the infirmary.

### disc00_01h (~13.5s) ✅ DONE (rewrite 2026-05-27, frame-verified)
Quistis arrives at the Garden infirmary to collect Squall. Sequence: angular
doorway close-up as she steps through → close-ups of her face (auburn hair in
instructor's ponytail, wire-frame glasses, navy SeeD instructor's blazer with
red trim and gold piping) → wide shot of the teal infirmary (angular desk
with retro round monitor, lamp, eye chart on wall) with Quistis in the
doorway → medium close-up of Quistis stepping in → SQUALL'S POV FROM THE BED:
his boot/leg in foreground, Quistis stands across the room with arms loose
at her sides, looking at him → close-up of Quistis: eyes lid lower → eyes
close, mouth opens slightly for a soft sigh of patient exasperation → eyes
open again, composed. NOT Dr. Kadowaki (original AD got this wrong); the
instructor's uniform belongs to Quistis. Squall is in the bed throughout,
not leaving.

### disc00_02h (~10s) ✅ DONE
Zell Dincht's introduction in the Garden main lobby. Zell approaches and leaps into a
fighting stance in front of the ornate Balamb emblem archway. Quistis visible walking
away in background at end.

### disc00_03h (~78s) ✅ DONE
Dollet SeeD Field Exam mission launch sequence. Confirmed frame content:
- Galbadian jets fly in formation past a large full moon (night sky, dark teal)
- Balamb Garden floating through night sky, hull glowing teal from below
- SeeD transport seaplane landing on calm water near Dollet coast (orange sunset)
- Inside transport: soldier reviews recon photograph of Dollet harbor/Communication Tower
- Beach landing: SeeD soldiers charge down lowered ramps into fire and explosions
- Aerial/birds-eye view of SeeD landing craft racing across dark water
- Two SeeD landing craft entering Dollet's stone harbor at water level (red sunset sky)

### disc00_04h (~14s) ✅ DONE
Selphie Tilmitt's introduction. Found on a rocky hillside near Dollet.
Confirmed: standing on boulders (pink/purple sunset sky) → leaping joyfully with
arms outstretched → crouching/landing among vegetation.

### disc00_05h (~73s) ✅ DONE
Dollet Communication Tower interior and satellite dish activation.
Confirmed frames:
- Dense industrial interior: pipes, scaffolding, green-lit machinery
- Tower exterior: Galbadian aircraft parked on roof, large dish above
- Weapons bay: missiles in numbered racks (01, 02, 03) in dark chamber
- Looking up at antenna array: multiple radar dishes, pink/purple sky
- Camera racing up through cylindrical blue-lit interior shaft
- Dish mounting arm from the side
- Large satellite dish fully deployed, angled toward sky, sunset behind it

### disc00_06h (~2s) ✅ DONE
Brief transition. An ornate vintage car with circular headlights drives through
Dollet's cobblestone streets at night. "Dollet" signage visible on building.

### disc00_07h (~74s) ✅ DONE
X-ATM092 mechanical spider chase through Dollet's streets leading to beach evacuation.
Confirmed frames:
- t=0: Dollet main cobblestone street at evening through an archway (colorful shops, small car)
- t=30: X-ATM092 leg/claw slamming onto a wooden bridge/dock, orange smoky sky
- t=60: Very dark (near end of sequence)
Narrative: chase → bridge → beach → Quistis on machine gun on extraction boat →
X-ATM092 damaged and falls → landing craft pull away.

### disc00_08h (~22s) ✅ DONE
SeeD graduation night. Sequence:
- Balamb Garden's outer ring against starfield and large moon (exterior, night)
- Close-up of Squall in SeeD dress uniform, scar visible, gazing upward
- Rinoa in white halter dress, smiling in ballroom crowd
- Squall leaning against pillar in dress uniform, contemplative

### disc00_09h (~90s) ✅ DONE
Waltz for the Moon — the graduation ball dance sequence.
Rinoa approaches and takes Squall's hand → leads reluctant Squall onto dance floor →
awkward stumbling → he finds rhythm → graceful synchronized dancing → shooting stars
through open ceiling → final spin → standing together under starfield ceiling.
Note: The ballroom ceiling opens to show stars/shooting stars as the dance peaks.
Confirmed final frame (t=74): wide shot of ballroom from above, Squall and Rinoa
embracing in center under open star ceiling, other couples on stairs around them.

### disc00_10h (~26s) ✅ DONE
Timber train mission. The SeeD team's gray train (with red stripe) pursuing
President Deling's ornate dark green/gold/red presidential train.
Confirmed frames:
- Close-up of presidential train exterior with gold filigree racing through forest
- Gray SeeD train closing in from behind on adjacent track
- Both trains running parallel

### disc00_11h (~30s) ✅ DONE
Train coupling sequence + Timber arrival.
Confirmed frames:
- Both trains side by side, coupling arm extending between them
- Coupling mechanism connecting
- Orange locomotive passing by a large moon (NOTE: the party falls asleep on the
  train to Timber — this shot likely represents the Laguna dream sequence beginning)
- Arrival at Timber Station — platform structures visible
- Green train car slowing at platform

### disc00_12h (~2s) ✅ DONE
Very brief. Aerial/overhead view of a town (likely Timber). Very grainy/compressed.

### disc00_13h (~1s) ✅ DONE
Single shot: President Deling's ceremonial podium in Deling City. Ornate podium with
multiple microphones, flanked by Galbadian flags (black/white check on dark red) and
red curtains. Establishing shot before the speech.

### disc00_14h (~28s) ✅ DONE
Deling City parade approach to the ceremonial plaza.
Confirmed frames:
- Bright flash opening
- Aerial view of Edea's golden circular parade float rotating from above
  (gold and teal machinery, ornate)
- Approaching the Deling City arch from below/inside — gargoyle columns, amber light
Narrative: The parade procession arriving at the plaza where President Deling will give
his speech while Edea is introduced as Sorceress Ambassador.

### disc00_15h (~28s) ✅ DONE
Irvine Kinneas's introduction at Galbadia Garden.
Irvine is a sharpshooter (uses rifle/shotgun) who joins the party before Deling City.
Confirmed frames:
- Copper/reddish-brown curved railings blur against bright blue sky (Galbadia Garden)
- Butterfly landing on Irvine's outstretched finger (his iconic introduction moment)
- Galbadia Garden grounds in daytime
- Irvine in his cowboy hat lying in the grass, relaxed
- Irvine crouching/reaching toward the ground
- Galbadia Garden exterior with teal-lit architecture
- A figure in a wide-brim hat holding a rifle (likely Irvine from a different angle)
Note: Irvine has long reddish-brown hair and wears a brown cowboy hat and long coat.
The copper curved railings are the distinctive architectural feature of Galbadia Garden.

### disc00_16h (~10s) ✅ DONE
Deling City Parade — descending to the ceremonial plaza at night.
Confirmed frames:
- Looking up a dark staircase (golden light from below)
- Looking down stairs at night toward the lit Deling City arch and teal fountain below
- Full view of the ceremonial plaza: ornate arch, glowing teal fountain,
  Galbadian banners (white/blue symbols), and Edea's pink parade float arriving

### disc00_17h (~59s) ✅ DONE
Deling City Parade — Edea on her float, then President Deling's speech.
Confirmed frames:
- t=0: Close-up of Edea's ornate spiral headdress from behind (dark backstage of float)
- t=20: Inside the float — Edea on a gold-trimmed throne framed by white drapes
- t=40: Close-up of Edea's face in profile — cold expression, full sorceress regalia,
  black feather collar, spiral/shell headpiece, face paint
- t=55: Wide shot — President Deling at podium, Deling City arch lit behind him,
  two large dark parade balloon-floats flanking scene, crowds filling plaza

### disc00_18h (~12s) ✅ DONE
Edea confronts Rinoa at the parade, then addresses the crowd at the podium.
Confirmed frames:
- t=0: Edea (full regalia) faces Rinoa (blue outfit) in close confrontation
- t=8: Close-up of Edea at the ceremonial podium with microphones, facing the crowd
Narrative: Rinoa sneaks to the presidential residence roof to try to cast a control
device on Edea, is caught. Edea then takes the podium to address the crowd.

### disc00_19h (~7s) ✅ DONE
Aerial views of the Deling City Parade and crowd.
Confirmed frames:
- Bird's-eye view down at Edea's circular parade platform from directly above
- High aerial shot of Deling City's grand boulevard densely packed with crowds,
  colored parade items visible in the street, green trees lining the road

### disc00_20h (~27s) ✅ DONE
Iguion gargoyle attack during parade. Stone gargoyle on Deling City arch comes alive
via Edea's magic — eye opens with lightning, green lizard creature (Iguion) bursts from
stone, two Iguions run through panicking crowd, climb wall toward Rinoa on rooftop.

### disc00_21h (~55s) ✅ DONE
Deling City parade celebration. SeeD team infiltrates through underground stone corridor
while Edea's float arrives at presidential residence. Dancers in plaid costumes, ornate
residence entrance with green-lit stained glass, Edea close-up with flames, aerial views
of plaza with braziers and magical displays, cheering crowds, fireworks.

### disc00_22h (~9s) ✅ DONE
Short parade continuation. Bird's-eye view of Edea's rotating float with cyan ring,
then Edea's hand casting golden magic, parade dancers below.

### disc00_23h (~37s) ✅ DONE
Parade procession. Overhead float shots, Seifer smirking on the float with gunblade,
Edea and Seifer silhouetted on float, stilt performers leading parade, float arrives
at presidential residence with clock tower and red banners.

### disc00_24h (~38s) ✅ DONE
Assassination setup. View through stone slit, clock tower reading 19:59:59, interior
carousel mechanism rising to roof at 20:00, float docked at residence entrance, dancers,
extreme Edea close-up (amber eyes, earrings), aerial sniper's-eye view of boulevard,
back inside stone passage.

### disc00_25h (~9s) ✅ DONE
Short transition. Ornate residence gates swing open revealing Edea on golden throne
amid flames and feathers. Edea's dark silhouette, then looking up through iron gate.

### disc00_26h (~7s) ✅ DONE
Brief transition. Rooftop carousel with automaton figures spins, then camera plunges
down to Edea gazing up from the float below.

### disc00_27h (~20s) ✅ DONE
Aftermath of assassination attempt. Irvine at sniper position behind rooftop parapet,
street brawl erupts (soldiers and civilians), sleek vintage car flees the scene,
car speeds away from residence with taillights glowing.

### disc00_28h (~29s) ✅ DONE
Confrontation on parade float. Camera moves through gate onto float, Edea descends
from golden throne past braziers, Seifer grins (scarred face, lit by flame), Edea
stares forward cold and imperious — the moment before she strikes.

### disc00_29h (~50s) ✅ DONE
End of Disc 1 — ice javelin scene. Edea conjures ice magic (blue energy, javelin forms
above headdress), Gateway arch wide shot, Edea gazes down with contempt, ice javelin
strikes Squall (impaled, staggering), Rinoa reaches desperately over float edge,
extreme close-up of Squall's eye reflecting fire, eye slowly closes.

### disc00_30h (~3m12s) ✅ DONE
The New Game intro FMV (plays after selecting New Game from title screen).
Music: "Liberi Fatali" (orchestral choir).
Fully reviewed and corrected. Sequence:
- Ocean waves and beach at Balamb
- Rinoa in flower field, opens palm → glowing white feather lifts from her hand
- Feather rises through storm clouds to apex, transforms into a gunblade falling
- SQUARESOFT presents title card
- Rapid montage of characters amid feathers
- Edea and Rinoa flashes
- Rocky plateau: Seifer strides out, swings Hyperion, smirks; taunts Squall
- Squall charges; blades clash in sparks
- Edea's eyes / Rinoa watching intercut with duel
- Seifer casts fire spell; Squall blasted down, blood on face
- Seifer slashes Squall across the face as he rises (during montage of Rinoa/Edea)
- Squall counter-slashes Seifer's face — both now bear matching scars
- Squall and Rinoa reaching for each other → FF8 logo (their silhouettes embracing)

---

## DISC 1 (disc01_XXh) — Partial Review (00h–33h done)

### disc01_00h (~30s) ✅ DONE
D-District Prison interior. Dark industrial machinery with hazard stripes, then a vast
cylindrical shaft with teal-lit tiers. A transport capsule descends on cables into the
depths, headlights blazing, shrinking into the abyss.

### disc01_01h (~19s) ✅ DONE
D-District Prison exterior. Metal gangway with "TAKASHIN" and "CAUTION" labels, barren
desert over concrete wall, then the camera reveals three massive drill-shaped legs
standing in the sand — the prison is a walking structure.

### disc01_02h (~22s) ✅ DONE
D-District Prison exterior continued. Gangway to "GALBADIA PRISON CONTROL TOWER 01"
door, close-up of drill leg grinding into rocky desert, looking up at prison hull with
support beams and cables.

### disc01_03h (~30s) ✅ DONE
D-District Prison in motion. The walking prison strides across the desert. Drill legs
churn sand, support beams sway, dust clouds billow as the legs descend through the
wasteland.

### disc01_04h (~11s) ✅ DONE
Galbadia Missile Base. Chain-link fence with barbed wire, hangar, and watchtower in
desert. Missiles launch in formation, smoke trails streaking across blue sky.

### disc01_05h (~15s) ✅ DONE
Missile base destruction. Silo covers tilt open, then a massive explosion engulfs the
base. Mushroom cloud rises from the desert floor.

### disc01_06h (~7s) ✅ DONE
Desert settlement struck by missiles. Aerial view of a small town clustered around rock,
then a fireball engulfs it and a mushroom cloud rises.

### disc01_07h (~11s) ✅ DONE
Missiles in flight. Camera races through clouds, a single missile streaks past with
flame trail, then multiple missiles converge with crossing smoke trails.

### disc01_08h (~25s) ✅ DONE
Galbadian fighter squadron. Green/pink jets in formation with afterburners blazing,
streaking over the ocean. Close-up of a missile's red targeting sensor labeled
"GALBADIA DSMAC." The squadron races low over the waves toward a mountainous coast.

### disc01_09h (~15s) ✅ DONE
Garden MD level activation. Underground control room with circular console overlooking dark
shaft, instruments glow green, energy arcs, Garden main hall shakes (emblemed floor tilts),
then Fisherman's Horizon dock with mechanical tower and canvas sails.

### disc01_10h (~28s) ✅ DONE
Balamb Garden crashes into Fisherman's Horizon. Garden's golden emblem descends from sky,
hull impacts FH dock amid sparks, massive water spray. Then aerial view of Trabia Garden
lying in a smoking crater surrounded by green hills.

### disc01_11h (~40s) ✅ DONE
Trabia Garden destruction and Galbadia Garden mobilization. Trabia smolders in distance
beyond green forest, view through cracked glass, Squall watches from behind (fur collar),
massive fire engulfs ruins, Garden dome against rocky cliff. Then Galbadia Garden flies
low over green hills with angular hull and glowing ring.

### disc01_12h (~22s) ✅ DONE
Battle of the Gardens — collision. Aerial sweep over green forest, Balamb Garden from
below with energy beams from golden ring, the two Gardens slam together with explosions
tearing across cracked hulls, wide shot of both tangled on a green plain.

### disc01_13h (~25s) ✅ DONE
Balamb Garden at rest — peaceful flyaround. White flash, then exterior deck with
landing pad, birds soaring past mountains, camera orbits ornate rooftop, seagulls
glide past dark hull, numbered hatches and blue-lit corridors.

### disc01_14h (~35s) ✅ DONE
Balamb Garden at rest — Rinoa on deck. White flash, birds over mountains, camera sweeps
landing pad, dark hull over green fields, Rinoa in blue cardigan (angel wings) with hair
streaming in wind, wide shot of Garden amid green plains with birds.

### disc01_15h (~8s) ✅ DONE
Balamb Garden over Balamb village. Quaint stone shop with iron signage on cobblestone
lane, camera tilts up to reveal Garden's dome towering above village rooftops.

### disc01_16h (~10s) ✅ DONE
Balamb Garden passes through Balamb harbor. Garden's hull looms over sailboat masts,
massive structure churns past waterfront iron railing with spray exploding upward.

### disc01_17h (~10s) ✅ DONE
Balamb Garden sailing across open ocean. Garden glides low over deep blue water,
pulls back to wide shot receding across vast empty sea.

### disc01_18h (~17s) ✅ DONE
White SeeD Ship sailing at sunset. Ornate sailing vessel with wooden deck and spiked prow
cuts through golden water, camera sweeps teal sails and cannon-mounted stern, wide shot
from behind with teal sails spread against orange sunset sky.

### disc01_19h (~29s) ✅ DONE
Garden crashes into FH — fisherman's perspective. FH's weathered tower with Garden behind,
a fisherman on a rusty pier under striped umbrella with windmills, startled by spray,
sits back down unfazed, then wide shot of massive wave engulfing the tiny pier.

### disc01_20h (~10s) ✅ DONE
Lunatic Pandora flyover at FH. Translucent energy sphere fills the sky, FH's massive
solar dish gleams below with windmills spinning around the perimeter.

### disc01_21h (~5s) ✅ DONE
Surveillance footage of Lunatic Pandora. Monitor displays scan-lined footage with
recording counter, massive dark structure fills the screen.

### disc01_22h (~8s) ✅ DONE
Deer in forest near Edea's orphanage (Centra). Spotted deer grazes in forest clearing
with mountains in distance, lifts head, stone building visible through trees.

### disc01_23h (~19s) ✅ DONE
Galbadia Garden approaches for the Battle of the Gardens. Dark red hull rises above
treetops blocking the sun, full view from below with copper ring glowing, then Balamb
Garden visible in distance as Galbadia races toward it with teal energy blazing.

### disc01_24h (~22s) ✅ DONE
Battle of the Gardens — Galbadian motorcycle assault. Close-up of helmeted soldier
(silver helm, blue scarf), motorcycle soldiers race forward, motorcycles launch from
Galbadia Garden's red hull flying across the gap, SeeD cadets in dark uniforms brace
against the assault on Garden walkway.

### disc01_25h (~20s) ✅ DONE
Rinoa dangles from Galbadia Garden. Two Gardens locked together (red hull against teal
ring), Rinoa clings to rough surface struggling, close-up of terrified face with green
forest far below.

### disc01_26h (~8s) ✅ DONE
Galbadia Garden separates from Balamb. Red hull grinds across courtyard with mountains
beyond, fortress pulls away with energy streaks across sky above damaged walkway.

### disc01_27h (~5s) ✅ DONE
Ragnarok lands at Esthar Airstation. Red dragon-shaped ship descends past turnstiles
(IN/OUT labels) onto landing pad.

### disc01_28h (~8s) ✅ DONE
Ragnarok takes off from Esthar Airstation. Red ship rests on pad with turnstiles in
foreground, lifts off in burst of pink and violet energy.

### disc01_29h (~46s) ✅ DONE
Galbadian mech soldier assault on Balamb Garden. Transport ship hull from below,
mech suits in cargo bay, jetpack deployment over terrain, armored soldier close-up
(green scarf, helmet with red sensors, number 08), soldiers breach Garden's ring
corridor, rappel down teal hull, crash through classroom.

### disc01_30h (~8s) ✅ DONE
Battle of the Gardens — close-up of collision. Two Gardens locked together,
Balamb's teal dome crushed against Galbadia's red hull. Camera orbits upward
past damaged tiers glowing gold.

### disc01_31h (~9s) ✅ DONE
Battle of the Gardens — damage. Balamb Garden's dome ablaze, both Gardens
tangled with wreckage below, pink energy bursts at the collision point.

### disc01_32h (~22s) ✅ DONE
Battle of the Gardens — exterior to ground combat. Damaged Balamb hull with blue
emblem, aerial view of green forest, view from beneath tangled Gardens, then
massive melee inside Garden's pink-tiled courtyard with cyan magic and fire.

### disc01_33h (~79s) ✅ DONE
Battle of the Gardens — full montage. Camera traverses between locked Gardens
exterior (gap, Galbadia's red hull, Balamb's dome, golden ring structure),
figure on structural beam, Rinoa clinging to Galbadia Garden's hull near a mech
then gripping a cable, overhead ground combat, soldiers fighting on walkways
and grass with fire and explosions.

---

## DISC 2 (disc02_XXh) — Complete (00h–31h done)

### disc02_00h (~13s) ✅ DONE
Esthar city revealed. Futuristic arch with purple lights, then hexagonal energy
barrier shimmers over snowy mountains and salt flats, dissolves to reveal Esthar's
vast teal/cyan skyline stretching across the horizon.

### disc02_01h (~31s) ✅ DONE
Esthar city tour. Through the city gate arch, aerial plunge into dense teal
skyline with pink energy beams, green transport capsule racing on elevated rails,
hexagonal platform entering blue-lit transit tunnel, then darkness.

### disc02_02h (~7s) ✅ DONE
Esthar Airstation interior — departure side. Teal elevator platform with glowing
blue fixtures, RENT-A-CAR sign, SEE YOU AGAIN banner above exit corridor.

### disc02_03h (~7s) ✅ DONE
Esthar Airstation interior — arrival side. Dark tunnel approach, blue tunnel burst,
then station opens up with WELCOME TO ESTHAR banner and elevator platform.

### disc02_04h (~7s) ✅ DONE
Lunatic Pandora interior — energy core close-up. Massive crystal machine with
circuit-patterned surfaces, blue energy surging, orange sparks, radial arms,
holographic data streams.

### disc02_05h (~7s) ✅ DONE
Lunatic Pandora interior — wide view of core. Towering machine with circuit-etched
panels, radial petal arms, holographic screens orbiting, blue energy cascading.

### disc02_06h (~7s) ✅ DONE
Lunatic Pandora interior — shaft descent. Dark shaft with glowing blue pillars,
green-lit sensor on cable, cylindrical pod descending with green indicators.

### disc02_07h (~20s) ✅ DONE
Lunar Gate launch sequence. Gold/teal cockpit interior looking down launch rail,
TIN CAN SHOOTER machinery engages, blinding acceleration, pod streaks upward
trailing fire toward the enormous moon, Lunar Gate rail at dusk.

### disc02_08h (~5s) ✅ DONE
Launch pod ascending through atmosphere. Pod climbs away from planet trailing
fire, clouds and ocean visible below as it pierces the upper atmosphere.

### disc02_09h (~48s) ✅ DONE
Lunar Base flyaround and spacewalk. Close-up of Esthar orbital station with red
lights and ESTHAR markings, antenna arms in starfield, two white-suited figures
floating between station modules, hull close-ups with bronze panels and red trim,
full station receding with gold arms and green-lit windows.

### disc02_10h (~8s) ✅ DONE
Lunar surface observation — monsters gathering. Monitor HUD displays cratered
lunar surface with dark splotches, zooms in to reveal crimson creatures covering
the terrain.

### disc02_11h (~21s) ✅ DONE
Ragnarok near the moon — Lunar Cry buildup. Dragon-shaped ship head close-up in
space, Ragnarok flies past monster-covered moon with red engine lights, HUD view
of moon surface swarming with red creatures.

### disc02_12h (~15s) ✅ DONE
Lunatic Pandora passes through Esthar. City at night with pink energy rails and
blue starburst, then daytime view of massive dark rectangular monolith rising
above teal buildings, dwarfing the skyline.

### disc02_13h (~40s) ✅ DONE
Adel sealing and Lunatic Pandora. Esthar platform at sunset with soldiers in
concentric rings, glowing pink energy containment cubes, red-haired figures in
circle (sealing ceremony), extreme close-up of Adel's blue-tinged face with
blank eyes and red hair, then Lunatic Pandora floating over red desert at sunset
with scorpion emblem.

### disc02_14h (~7s) ✅ DONE
Adel containment satellites destroyed. Angular satellites in orbit with pink energy
beams between panels, then explosion — pink fire, fragments tumbling, seal shattered.

### disc02_15h (~17s) ✅ DONE
Adel's containment tomb consumed by the Lunar Cry. Adel visible inside (blue skin,
red eyes, crown), spider-like containment structure drifts through crimson monster
swarm, gradually consumed until only empty starfield remains.

### disc02_16h (~15s) ✅ DONE
Lunar Base destruction during the Lunar Cry. Green-lit escape pod chamber interior,
station explodes with debris and fire against planet's horizon, station arm engulfed
by red creatures, escape pod launches through the crimson tide.

### disc02_17h (~6s) ✅ DONE
Rinoa adrift in space — close-up of her frightened face through spacesuit helmet visor.
Single sustained shot, stars behind her.

### disc02_18h (~6s) ✅ DONE
Rinoa adrift in space — variant with eyes downcast, more resigned expression.
Same framing as disc02_17h, slightly different emotional beat.

### disc02_19h (~11s) ✅ DONE
Rinoa losing consciousness in space. Eyes closing, head tilting forward.
Third variant of the Rinoa-in-space close-up sequence.

### disc02_20h (~18s) ✅ DONE
Squall rescues Rinoa in space. Extreme close-up of Rinoa's closed eye, Squall's
Griever necklace drifting through the starfield, Rinoa opens her eyes as Squall's
gloved hand reaches her visor, chain draped across the glass.

### disc02_21h (~19s) ✅ DONE
Post-rescue tenderness. Squall presses his helmet against Rinoa's to share air,
Rinoa's eyes closed peacefully, she opens her eyes and gazes at Squall with a
faint smile, camera pulls away to starfield.

### disc02_22h (~6s) ✅ DONE
Lunar Cry from orbit. Wide shot of the red torrent of creatures cascading from
the moon's surface into space. Single sustained shot.

### disc02_23h (~13s) ✅ DONE
Squall holds Rinoa in space, then the Ragnarok appears in the distance.
Dragon-shaped ship silhouetted against the planet's curve, approaching.

### disc02_24h (~40s) ✅ DONE
Ragnarok flyaround in space. Extended exterior showcase: planet horizon,
dragon-claw landing gear, ESTHAR hull markings, green cockpit windows,
engine pods with white thruster light, camera orbits crimson hull.

### disc02_25h (~2s) ✅ DONE
Very brief. Extreme close-up of Ragnarok's red hull with ESTHAR text.

### disc02_26h (~27s) ✅ DONE
Ragnarok interior — reunion. Propagator alien creature lurking in machinery,
Squall strides through bright white corridor, close-up of Squall (scar, chain)
in control room, Rinoa and Squall face each other and embrace tightly.

### disc02_27h (~9s) ✅ DONE
Ragnarok atmospheric re-entry. Dragon head plunges into green-gold clouds,
ship streaks through layers of cloud at blazing speed.

### disc02_28h (~25s) ✅ DONE
Ragnarok descends into Esthar with escort craft, then the Lunar Cry's red pillar
pours down from the moon through burning clouds above Esthar's towers.

### disc02_29h (~26s) ✅ DONE
Ragnarok punches through energy barrier with shield, Esthar's turrets fire at
monster swarm amid explosions and hexagonal shields, Ragnarok crashes down
among rubble with dragon claws gripping shattered stone.

### disc02_30h (~42s) ✅ DONE
Full Lunar Cry surface impact. Red mass descends through clouds, sky turns solid
red over Esthar with Lunatic Pandora overhead, golden streaks rain between green
mountains, blinding explosion at impact site, energy burst inside Lunatic Pandora.

### disc02_31h (~66s) ✅ DONE
Lunar Cry montage. Star streaks, Propagator alien extreme close-up (red eye,
pink flesh), swarming pink/purple horde, orbital view of red stream striking
planet, moon and planet aftermath, more monster chaos, stream subsides.

---

## DISC 3 (disc03_XXh) — Complete (00h–04h, 06h done; 05h has no AVI)

### disc03_00h (~26s) ✅ DONE
Adel junction scene inside Lunatic Pandora. Adel hovers in golden energy vortex,
Seifer holds Rinoa before her. Close-up of Adel (red hair, pale face, dark markings,
jeweled crown). Adel's massive clawed hands seize Rinoa to junction with her power.

### disc03_01h (~84s) ✅ DONE
Time compression FMV. Golden vortex portal opens, vast organic form descends through
clouds, objects/figures tumble through teal void, reality melts into reflective blobs,
bright blue ocean, underwater tropical fish, white energy pillar through orange sky,
thousands of birds scatter across pink sky, blinding particle burst.

### disc03_02h (~19s) ✅ DONE
Ultimecia's Castle establishing shot. Gothic castle emerges from dark fog, gargoyles
and bat-wing architecture against enormous moon, camera sweeps past stone walls and
lit windows, castle floats on skeletal foundation with chains dangling into the void.

### disc03_03h (~15s) ✅ DONE
Ultimecia's throne room. Red light pulses in darkness, then ornate golden architecture
revealed (red velvet, filigree, jeweled panels). Wide shot: golden throne hovering
above teal energy between dark columns with open sky beyond.

### disc03_04h (~4m50s) ✅ DONE
Post-Ultimecia time compression wandering. Squall's close-up in hazy void, then
he staggers alone across a vast desolate desert under swirling distorted sky.
Rinoa waits against teal clouds. Squall reaches the flower field at sunset and
finds Rinoa among the petals. Time-distorted flashbacks of Edea receiving power
at the orphanage. Ghostly architecture phases through time. Memories fracture.
Light floods everything white. A single white feather drifts against a pure white sky.

### disc03_05h — DOES NOT EXIST
No AVI file exists for this index. Only a .cam file is present in the game folder.
No audio description needed.

### disc03_06h (~7m53s) ✅ DONE
Full ending FMV and credits. White light fades. Squall lies unconscious on rock.
Rinoa cradles him desperately. Cut to Seifer fishing peacefully by calm water.
Squall recovers — open hand holds Rinoa's ring in sunlight. Squall and Rinoa
embrace under the night sky; close-up of Squall smiling for the first time.
Laguna stands in a green field in casual clothes, smiling warmly. Credits roll
with red/white text on the right; a small camcorder window on the left shows
celebration footage at Balamb Garden — friends cheering, party scenes. Near
the end, Rinoa steps onto a balcony at night through the viewfinder, battery
low. Fade to black.

---

## DISC 4 (disc04_XXh)

### disc04_00h (~9s) ✅ DONE
Publisher end card. Black screen with centered white text:
"Published by Square Electronic Arts L.L.C."

---

## Special Files

### ff8_intro_ad.vtt — ✅ DONE
The New Game intro FMV (disc00_30h.avi). See disc00_30h entry above.

### ff8_opening_credits_ad.vtt — ✅ DONE
The opening credits FMV that plays before the title screen.
Shows black-and-white still portrait shots of characters intercut with white-text
credits on black, ending with the FF8 title logo on white.
Characters visible: Squall near chain-link fence, Edea striding forward,
a man in wide-brim hat, an ornate armored vehicle, a gunblade in a case,
a young woman in bright lights, a martial artist, an emblem, two armored figures,
Rinoa in a flower field with white feathers.
