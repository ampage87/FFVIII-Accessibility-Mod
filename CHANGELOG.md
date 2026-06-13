# FF8 Accessibility Mod — Changelog

Newest on top. Each entry begins with a `## vMAJOR.MINOR.BUILD` heading followed by a blank line and the commit message body. The push utility (`Utilities/push_to_github.ps1`) reads the top heading to determine the version being pushed and uses everything between that heading and the next `## v` heading as the commit message body.

The version in the top heading **must** match `FF8OPC_VERSION` in `src/ff8_accessibility.h`. The push utility refuses to push if they don't.

Older entries (pre-v0.15.12.0) are preserved in `CHANGELOG_HISTORY.md`.

## v0.18.3.32

Battle: silence the rest of the leftover damage-number diagnostics -- the victory screenshot (#62) and the per-frame battle-log flood (#63). LOCAL build.

Two loose ends from the same concluded damage-number investigation, both pure
diagnostic with no effect on gameplay or the spoken accessibility output.

#62 (victory screenshot): on every victory the mod still wrote a
Logs/victory_auto_1_pose.bmp/.png pair (~900 KB). It doesn't route through any
of the five capture paths v0.18.3.31 gated, so it survived. It overwrites
itself each battle rather than piling up, but by the "only the F11 screenshots
you take on purpose are wanted" rule it should be off too. VictoryAutoCapture is
now gated behind the same BATTLE_DIAG_SCREENSHOTS switch.

#63 (battle-log flood): the same investigation left heavy diagnostic logging on
the live battle path. Any battle with damage or healing filled the battle log
with per-frame hex dumps -- the worst being [POPUP-TIME-DIAG] (a full record /
display-region / entity-region dump every frame for the life of every damage
popup) and [SPRITE-POOL-DIAG] (a per-frame sprite-pool diff), plus per-event
lines from [SPRITE-POLL], [SPRITE-ALLOC-V99], [DMG-RENDER], [DMG-POPUP-CREATE]
and their periodic STATS summaries.

A new master switch, BATTLE_DIAG_LOGGING in battle_tts.h (default off), now
governs all of it, alongside a DiagLogBattle helper that compiles to the real
logger when the flag is on and to nothing when it's off. The two per-frame
offenders have their whole diagnostic bodies gated (so the wasted per-frame work
is skipped, not just the log line); the per-event lines route through
DiagLogBattle. The popup-lifetime body is gated on either flag, so turning the
screenshots back on for a future investigation still gives you its time-gate
captures.

Nothing load-bearing was touched. The hooks and publishers that drive the live
features keep running: the sub_5068B0 impact-time render-tick publish (the
damage-announce trigger), the immediate HP-flush trigger and scan-window-close
detector inside the popup poll, and the damage-popup InterlockedExchange
publishes. As always, the code stays in place behind the flags -- gate, don't
delete -- so a one-line change re-enables the full diagnostic stream.

Not yet pushed.

## v0.18.3.31

Battle: stop flooding Logs/screenshots with diagnostic captures every battle (#62). LOCAL build.

Apart from the F11 screenshots you take on purpose, every battle was dumping
hundreds of extra captures into Logs/screenshots, plus large per-event
subfolders that each held up to ~270 images. A play session with random
encounters bloated the folder fast.

These were all left over from the old battle damage-number reading work. That
investigation finished long ago (the damage announce ships through the
impact-time render hook), but the auto-capture diagnostics that supported it
were never switched off. They sit on the live battle render path and fire up to
their per-battle limits on every encounter, with the limits resetting each
battle, so the growth never stopped. Five separate capture paths were involved:
the sprite poll (poll_NEW_*/poll_KIND_*), the damage-popup lifetime captures
(popup_time_*/popup_change_*), the spell-result kind=4 captures (kind4_*), the
per-HP-flush audit capture (sprite_animflag_*), and the ROI calibration folders
(roi_calib_*, the biggest contributor).

All five are now governed by a single master switch, BATTLE_DIAG_SCREENSHOTS in
battle_tts.h, which defaults to off. The capture code stays in place behind the
flag so a future damage-render investigation can turn it back on with a one-line
change. Every capture was pure diagnostic audit; the live accessibility
features (HP/damage announce, scan-window detection, no-effect/miss announce)
run on separate paths and are unchanged. The F11 manual screenshot and the
victory screenshot do not route through any of the gated functions, so they
still work exactly as before.

Note: the same old investigation also leaves heavy per-frame diagnostic logging
in the battle log; that is a separate, lower-priority cleanup, out of scope here.

## v0.18.3.30

Timer: stop reporting a phantom timer after a timed sequence ends (#59). LOCAL build.

After the train mission finished, the mod still acted as if the timer were
running. The engine doesn't reset its timer to zero when a mission completes --
it just freezes it at whatever was left (the train leaves about 79 seconds), and
the module only deactivated when the value reached zero. So it sat there
"active" indefinitely. This was the same root cause behind the equivalent
glitch on the Dollet and Fire Cavern timers.

The module now also deactivates when the timer stops changing. A live countdown
ticks about once a second, so if the value holds steady for several seconds the
sequence has clearly ended, and the timer switches off on its own. A frozen
timer (Shift+T) is left alone -- that's held on purpose. If some sequence ever
legitimately pauses its timer for a long stretch, the timer simply re-detects
when the countdown resumes.

Not pushed.

## v0.18.3.29

Timber train: countdown timer now announces (#59). LOCAL build.

The train hijack gives you 5 minutes to finish the seven procedures, but that
timer never spoke. The countdown module (from the Dollet / Fire Cavern work)
reads the live engine timer, but its "is this seconds?" check was sized for
Dollet's half-hour clock -- it only accepted 500-3000. The train's 300 seconds
fell below that, so the timer was classified as unknown and never started.

Two changes fix it. First, the seconds range is widened: that engine global
holds seconds for every timed sequence that uses it, so anything from 1 up to
14999 is now read as seconds (the train's 5:00, and its final 0:30, included).
Second, the timer now only starts once it has actually been seen counting
down a couple of times. Previously any leftover value sitting at that address
(the train leaves a 79 there after the mission ends) was treated as a live
timer every single frame -- which both risked a bogus announcement and was the
source of the multi-megabyte log spam. A static value never decrements, so it
is now correctly ignored.

Behavior once active is unchanged from Dollet: you hear "Timer detected, about
5 minutes remaining" when it starts, automatic cues at 1 minute and 30 seconds,
T announces the time remaining on demand, and Shift+T freezes it.

Note: the train timer living at this address is a strong inference (it is the
shared timer global, and the leftover 79 = 1:19 is a believable "time left at
mission completion"), but it is confirmed by this BAT, not yet hard-proven. If
it does not announce on the live train, the fallback is to re-enable the
address scanner (as was done originally for Dollet) to locate the train's
timer. A quick Dollet regression check is also worthwhile since the activation
logic changed.

Not pushed.

## v0.18.3.28

Timber code entry: fix Rinoa's "L L L L" instruction (#60 / #57). LOCAL build.

During the uncoupling briefing, Rinoa explains the keypad with a line like
"...if I relay the code 3124, you'll push [four buttons], in that order." In
the original those four buttons are directional sprites; on PC they decode to
a meaningless "L L L L", so the spoken version gave you no idea which keys she
meant.

That line is now rewritten on the fly. For the example code 3124 it reads
"...you'll push A, D, X, W, in that order" (3 is A, 1 is D, 2 is X, 4 is W),
and a short reminder is tacked on: you can press the slash key anytime to hear
the key layout. Only that one line is touched -- the match is on the unique
four-L run, and the single "L" that shows up as a line break in other dialogue
is left alone.

Not pushed.

## v0.18.3.27

Timber code entry: announce the key layout (#60 / #57). LOCAL build.

The uncoupling codes are read aloud as numbers ("Code. two, two, three,
two"), but until now there was no spoken way to learn which keys those
numbers map to. Pressing "/" while at a code panel -- either the briefing-
room practice model or the live moving train -- now says:

"Code entry keys. 1 is D, 2 is X, 3 is A, 4 is W."

The order is number-first on purpose: it matches the order the code itself is
read out, so when you hear "two" you can look up "2 is X" directly.

This reuses the same "/" key that reads the help bar inside menus; there's no
clash because the menu handler only runs in menu mode, and code entry happens
on the field. Press once per reminder. Not pushed.

## v0.18.3.26

Timber Skip: confirmed working + label note (#60). LOCAL build.

Skip is confirmed end to end: choosing it drops you into the Forest Owls' base
with the post-mission story intact -- the dummy-President conversation starts
exactly as it would after a real run.

One nice side effect: because the captured state came from a run that included
a few failed attempts, replaying it lowers your SeeD rank by one. Rather than
scrub that out, we're keeping it as an intentional, predictable cost of
skipping the challenge. The Skip option's description now says so, so you know
the trade-off before you pick it.

Label text only -- no behavior change from v0.18.3.25. Not pushed.

## v0.18.3.25

Timber Skip: real bypass built (#60). LOCAL build.

The Skip option now actually skips. Picking it at the start of the train mission
reproduces the persistent state a successful uncouple leaves behind, then warps
you to the room the win returns you to (the Forest Owls' base), so the story
picks up exactly where it would after a clean run -- no roof traversal, no code
entry, no guards.

How it works (`ExecuteSkipBypass` in `train_mode_ask_overlay.cpp`): it writes the
23-byte persistent savemap delta captured from a real winning run (the step and
clock counters from that capture are left out, since you didn't take those
steps), then fills the engine's own field-transition request block with the
exact values the winning map-jump produced and arms it. The engine's field loop
picks up the request and performs the load -- the same path a real win takes, so
no engine function is called directly. The warp is deferred about 400 ms after
you confirm the choice so it doesn't collide with the dialog closing.

The before/after capture diagnostic from v0.18.3.24 is switched off now that we
have the data (the code stays behind its flag for future reuse).

Testing note: this writes story state and moves you between fields, so test it
with a backup save handy. If the post-mission scene doesn't play correctly, the
captured delta needs refining -- reload and report what happened. Not pushed.

## v0.18.3.24

Timber Skip: discovery capture (#60). LOCAL build, logging-only.

Groundwork for the real Skip (bypass the train). Before we can warp the player
to the end safely, we need to know exactly what persistent state a *successful*
run leaves behind, so we don't skip past a story flag and break later events.

This adds a diagnostic (behind `TRAIN_SKIP_CAPTURE_DIAG`, on) inside the train
overlay. On a normal run it snapshots the savemap (base `0x1CFDC5C`, 4 KB) at
mission start (`tiyane1`) and again at the field a successful uncouple returns
the player to, then logs the byte-level diff to `ff8_field.log` under
`[SKIP-CAP]`. It reuses the overlay's existing field-change detection and the
guarded snapshot reads nothing it writes -- no engine state is touched.

Next: read the `[SKIP-CAP]` delta from one clean winning run, identify the
story/progression flag(s) among the gametime/step churn, then implement Skip
(set those flags + MAPJUMP to the captured return field) and switch the Skip
choice off its interim Freeze fallback. Not pushed.

## v0.18.3.23

Timber train: in-engine guard-mode picker (#60). LOCAL build.

The player now chooses how the mod handles the train guards through a real
in-game dialog, right as the mission begins, instead of editing the INI. New
module `train_mode_ask_overlay`, built on the same proven pipeline as the
Dollet chase ASK: DialogInject renders the prompt and detects the answer; the
overlay owns the trigger and the dispatch.

The three options, default on the first:

- **Manual** -- guards patrol; the uncoupling codes and per-guard proximity
  cues are announced.
- **Freeze** -- guards are held in place; the player just enters the codes.
- **Skip** -- bypass the train scene. The bypass itself isn't built yet, so
  for now Skip falls back to Freeze (announced clearly on selection) rather
  than leaving the guards live with no help.

Trigger, pinned from a capture run: Watts' "Are you ready, sir!?" prompt fires
in the Forest Owls' base room (tiagit1); answering "Yeah" jumps straight to the
first train-roof field (tiyane1, "Timber - Train 3"), where Rinoa says "Squall,
over here!". The overlay matches that line, gated to tiyane1 and fired once per
mission, then opens the ASK. It re-arms whenever the player returns to the
briefing room, so a reload or replay prompts again.

Wiring: forwarded the decoded dialog text from the show_dialog hook (beside the
chase forward, field mode only); added Initialize/Update/Shutdown to the mod
lifecycle in dinput8.cpp; added the new source file to the build list. The
choice persists via `FieldDialog::SetTrainGuardMode()`. Not yet pushed.

## v0.18.3.22

Timber guard modes -- relabel to the player-facing scheme (#60 prep). LOCAL build, behavior-preserving per INI value.

The in-engine mode picker we're about to build needs the three options named
the way a player will read them, so the guard-mode enum is relabeled:

- **Manual** (0) -- guards move; the code announce and per-guard proximity
  cues are on. This is the old "Original" path, and it's now the **default**.
- **Freeze** (1) -- guards held in place; the player only enters codes. This is
  the old "Manual" path.
- **Skip** (2) -- bypass the train scene (not yet built).

The fully-vanilla option is gone on purpose: with no code announce it leaves a
blind player stuck, so it has no place in this mod.

The numeric INI values did not change -- 0 has always meant guards-plus-cue, 1
frozen, 2 skip -- so any existing `train_guard_mode` line keeps its exact
behavior. Only the names and the default moved (default flipped from 1 to 0).
Under the hood: the `TGM_*` enum members were renamed in `field_dialog.h`;
`GuardOriginalCue` -> `GuardManualCue` (gates `TGM_MANUAL`); `GuardManualFreeze`
-> `GuardFreezePin` (gates `TGM_FREEZE`); and the default, bounds checks, and
`[TRAINMODE]` log strings were updated to match. No new runtime behavior; this
is the groundwork for the ASK picker. Not yet pushed.

## v0.18.3.21

Timber guard chapter -- diagnostics-off cleanup (#58). LOCAL build, no behavior change.

With both assisted guard modes confirmed -- Manual at v0.18.3.14 and Original at
v0.18.3.20 -- the discovery diagnostics that were flooding the field log are no
longer needed. Turned off `GUARD_RECON_DIAG` (the `[GUARDPOS]` per-guard
position stream) and `GUARD_VAR_DIAG` (the `[GUARDVAR]` round/var stream, and
with it the `[GUARDFREEZE]` once-a-second Manual confirmation). Both stay behind
their flags for a one-line re-enable if a future guard investigation needs them.

Nothing about the features changed: Manual still pins the patrol switch to keep
the guards frozen, and the Original-mode `[GUARDCUE]` line -- which only emits on
a per-guard level change, not a flood -- is left on as the feature's own trace.
Not yet pushed.

## v0.18.3.20

Timber guard **Original mode** -- identify guards by the Y axis, not distance (#58). LOCAL build.

The F11 screenshots settled the question. When Squall drops to the code panel he
is at the bottom of the screen, on the lower coupling, while the party stay up
on the roof. So a party member can be standing horizontally right above Squall
-- near in a straight line -- yet far from him on the depth axis. Every previous
attempt judged proximity by Euclidean distance, which can't tell those apart;
that's why the party kept leaking through.

This build judges proximity on the Y axis alone: |entity.Y - player.Y|. The
guards patrol the interior corridor, so their Y sweeps straight through Squall's
(down to about 90, the width of the corridor) -- while the party sit on the roof
at a Y offset of roughly 1360 or more and stay there. A guard is announced as
its Y closes on Squall's (approaching at 960, close at 480, clear past 1152);
anything beyond 1152 on the Y axis, including the entire roof party, is ignored
and never gets a label. The field's axes are rotated, so this Y axis is what
reads as horizontal on screen -- which is why earlier notes called the guard
motion "vertical."

The motion gate stays on as a secondary guard against a static prop sharing the
lane, the per-guard labels and silent-recede behavior are unchanged, and
`[GUARDCUE]` now logs the Y offset alongside distance and position. Not yet pushed.

## v0.18.3.19

Timber guard **Original mode** -- fix the "four guards" on the first car (#58). LOCAL build.

The live-motion gate from v0.18.3.18 worked during code entry -- the party,
parked motionless on top of the car, were correctly silent -- yet the BAT still
announced four guards. The position log made it plain: the two real guards
patrol the rail at X about -1315, sweeping continuously, while two party members
sit dead still at X -1221 (the same rail the player stands on), distance 1364
and 1924, never moving and never coming near. The catch was at scene start: the
party walk *into* those positions, so for a second or two they are moving, and
the label was handed out to any mover on its first active tick -- so the party
took "Guard 1" and "Guard 2," then settled, leaving the two real guards as
"Guard 3" and "Guard 4."

Two fixes. A guard label is now assigned only once an entity actually approaches
within the warning distance, not merely moves -- the party walk to a far spot
and stay 1300-plus units away the whole time, so they never earn a label. And
the label is released the moment an entity settles, then re-taken as the lowest
free slot, so the two real patrollers reliably come out as Guard 1 and Guard 2
even if a straggler briefly held a number. The motion gate, distances
(480/960/1152), per-guard cues, and pos logging are unchanged. Not yet pushed.

## v0.18.3.18

Timber guard **Original mode** -- tell guards from party by live motion (#58). LOCAL build.

The third BAT was much closer: timing felt about right, but two issues remained.
First, the first car still announced more than two guards. The v0.18.3.17
"ever far" gate assumed followers stay near the player -- but they don't: the
party (Zell, Selphie, Rinoa) ride on top of the train, far from Squall down at
the panel, so they sailed past the distance gate. The win-cutscene log showed
the same failure mode from the static dial NPC, which became a phantom "Guard 3"
the moment it slid across the screen and then closed on the walking player.

The fix drops distance-based discrimination entirely in favor of **live motion**.
A real guard patrols the corridor without stopping, so it will have moved within
the last 1.2 seconds; a party member who has taken position on the roof, or a
static NPC, has not. Only an entity moving *right now* earns a guard label and a
cue, and a guard that stops is retired with a "clear". This is field-independent
-- no hard-coded coordinates -- and matches the layout: guards inside and moving,
party up top and still.

Second, the gap between "approaching" and "close" was too short. Approaching
widens from 832 to 960 units (close stays 480), giving roughly a second more
between the first warning and the urgent one at the observed sweep speed.

The per-guard labels, silent close->approaching recede, and close-only interrupt
are unchanged. `[GUARDCUE]` now also logs each entity's position, so any future
tuning against the on-screen layout has the coordinates on hand. Not yet pushed.

## v0.18.3.17

Timber guard **Original mode** -- fix false guards and widen the lead (#58). LOCAL build.

The second BAT surfaced two linked problems. On the first code car the cue
announced more than two guards: the party followers (Zell, Selphie, Rinoa)
shuffle to keep up with you, which tripped the "is it moving?" test, and the
static dial NPC jostles once in the cutscene and did the same -- all of them
then sat right next to you and read as "close". That false chatter also buried
the real guard's earlier warning, which is why the advance notice still felt
short even after v0.18.3.16 widened the distances.

**Guard-vs-follower discriminator.** A real guard sweeps the corridor and is
seen far from the player at least once; followers and static NPCs never are
(they stay close). `GuardOriginalCue()` now requires one sighting beyond
`GUARD_FAR` (900 units) before an entity earns a guard label and a cue. In the
BAT the real guards swept out past 1500-2000 units, while the followers and the
dial NPC never left the player's side -- so only the two real guards qualify,
and they renumber correctly as Guard 1 and Guard 2.

**More lead time.** Cue distances widened again: close 480, approaching 832,
clear 1024 (hysteresis band 832..1024). At the observed ~125 unit/sec sweep the
first "approaching" now lands roughly 7 seconds before a catch. Removing the
false-guard chatter should make that warning actually audible in time.

Per-guard labels, the silent close->approaching recede, and the close-only
interrupt are unchanged. `GUARD_FAR` and the three cue distances are named
constants at the top of `GuardOriginalCue()`. Not yet pushed.

## v0.18.3.16

Timber guard **Original mode** tuning from the first live BAT (#58). LOCAL build.

The v0.18.3.15 cue worked, but the `tilink2` run exposed two problems: the
warning came too late (only ~2 seconds between "Guard close" and the catch --
not enough to finish entering the code and step away), and with two guards
sweeping, a single "Guard close" didn't say *which* one. Both are addressed.

**Per-guard announcements.** Each moving guard now carries its own proximity
level and a stable numeric label, so cues are specific: "Guard 1 approaching",
"Guard 2 close", "Guard 1 clear". Labels are assigned 1, 2, ... the first time
each entity is seen moving and reset per field, so each code car numbers its
guards from 1. The static dist-206 entity from the BAT (talk radius 128 but
never moving) is still correctly ignored -- only entities that actually patrol
get a label and a cue.

**Wider, later-tunable thresholds.** Measured sweep speed was ~125 units/sec,
so the old 192/320 thresholds gave almost no lead. New distances:

- <= 384 -> "Guard N close" (interrupts; catch at the ~128 talk radius is near)
- <= 704 -> "Guard N approaching" (~6 seconds of lead at the observed speed)
- >= 832 -> "Guard N clear" (hysteresis band 704..832 prevents boundary chatter)

A guard stepping back from "close" to the "approaching" band is now silent --
announcing "approaching" while a guard is leaving would be misleading -- and
only the urgent "close" interrupts speech; "approaching" and "clear" queue.

Manual mode and every non-`tilink` field are unaffected. Thresholds are plain
named constants at the top of `GuardOriginalCue()`, easy to tune further. Not
yet pushed.

## v0.18.3.15

Timber guard **Original mode** -- audio guard-proximity cue for the train
hijack (#58). LOCAL build.

Manual mode (v0.18.3.14) freezes the guards entirely. Original mode is the
opposite end: the guards patrol exactly as in vanilla, and the mod gives the
blind player the spatial information a sighted player reads off the screen --
how near the nearest sweeping guard is -- so they can play the real minigame,
stepping away from the code panel when a guard closes in and returning when it
is clear.

When `train_guard_mode` is Original, `GuardOriginalCue()`
(`field_nav_observe.inl`, called each tick from the field observer) finds the
nearest *moving*, visible guard (talk radius 128; the static code panel is
talk 1 and is ignored) and announces three escalating levels as its distance
to the player crosses fixed thresholds, with a hysteresis band so it does not
chatter at a boundary:

- distance >= 480 -> "Guard clear"
- distance <= 320 -> "Guard approaching"
- distance <= 192 -> "Guard close" (interrupts speech; a catch at the ~128
  talk radius is imminent)

Mover detection mirrors the recon: an entity counts as a guard once it drifts
more than 20 units from where it was first seen on the field, which keeps the
static scenery -- and, while the player holds still at the panel, the party
followers -- from tripping the cue. The cue is gated by the mode, not a
diagnostic flag, so it is completely inert unless Original is selected (the
default remains Manual). It reads only; its sole effects are the spoken cue
and a `[GUARDCUE]` log line on each level change.

The 192/320/480 thresholds are a first cut tied to the 128 talk radius
(1.5x / 2.5x / 3.75x) and are expected to be tuned after testing against how
close the guards actually sweep in un-frozen play.

The guard mode was promoted from a private cache in `field_dialog` to a shared
accessor so the cue (which lives in `field_navigation`) can read it and the
upcoming in-engine mode ASK can set it:

- `field_dialog.h` now declares `enum TrainGuardModeVal { TGM_ORIGINAL,
  TGM_MANUAL, TGM_SKIP }` plus `GetTrainGuardMode()` / `SetTrainGuardMode(int)`.
- The cache moved from a function-local static to a file-scope static so
  `SetTrainGuardMode()` can update the live mode (and persist it to the INI)
  without an INI round-trip -- ready for the ASK to call when the player picks
  a mode in-game.
- `TrainGuardMode()` now writes the resolved value back to the INI on first
  read, so the `train_guard_mode` key always exists as an editable line
  (previously the default was applied but never materialized in the file, so
  there was nothing to edit when selecting a mode by hand).

No behavior change for Manual or for any non-`tilink` field. Not yet pushed.

## v0.18.3.14

Timber guard **Manual mode** -- first real accessibility mode for the train
hijack (#58). LOCAL build.

The v0.18.3.13 `[GUARDVAR]` run proved the guard-patrol switch: field var 1040
holds 1 while the guards patrol and drops to 0 when they idle, and the catch
fires as a moving guard enters its talk radius (~128) of the player. The code
validators gate on a different var (1042 = round active), not 1040.

Manual mode pins var 1040 to 0 on `tilink*` every poll, so the guards stay
frozen and can never reach the player, while code entry and the win proceed
normally. A blind player can now work the uncoupling code at their own pace
without a guard sweep they can't see ending the round.

- New `GuardManualFreeze()` (`field_dialog_lifecycle.inl`, from `PollWindows`):
  on `tilink*`, writes 0 to the single byte at varblock 0x1CFE9B8 + 1040 each
  poll. SEH-guarded; logs a throttled `[GUARDFREEZE]` confirmation once per
  second. The varblock is byte-indexed, so only the one byte is written --
  never a wider store that would clobber the adjacent round/entry vars.
- New `train_guard_mode` setting in `ff8_accessibility.ini` `[Accessibility]`:
  0 = Original (vanilla; audio cue to come), 1 = Manual (freeze), 2 = Skip
  (not yet implemented). Default = Manual. Read once and cached. Eventually an
  in-game mode prompt (#60) will set this; for now it's INI-selectable.
- Turned off the guard JSM dump diagnostic (`GUARD_JSM_DUMP_DIAG` 1 -> 0) now
  that entities 0-27 are fully mapped; retained behind the `#define`. The
  `[GUARDVAR]` and `[GUARDPOS]` runtime logs stay on to confirm the freeze
  holds and to watch the round still reach a win.

No opcode 0x4A (ASK) exists in any `tilink1` entity (0-27 dumped), confirming
the fail dialog is not a field script there -- it lives on the post-catch
destination. Skip mode still needs the success/win flag pattern and will be
built next.

## v0.18.3.13

Guard machine mapped + runtime state logger (#58). LOCAL diagnostic build.

The v0.18.3.12 narrowed dump (entities 18-30) was read for 18-25 and cracked
the Timber train-hijack minigame's structure:

- The patrol guards are `blind2` (entity 19, SETMODEL 5) and `blind3` (entity
  20, SETMODEL 6) -- invisible `Other` entities that walk the corridor (spawn
  near (-1315,-505) and (-1312,93)). These are the recon's moving "model-5/6"
  guards, NOT the GalHei sprites (which are static `Background` scenery). Their
  walk loop is gated on field var 1040 and never reads the player's position.
- `blind4` (entity 21) is the minigame master: its init zeroes the entered-code
  slots 1024-1027, the code-entry var 1043, and the patrol var 1040, then wakes
  the helpers; one method is the fail/restart path (MAPJUMP3 back into field 902
  = tilink1 itself, spawn picked by savemap 724); another is the success path
  (MAPJUMP3 out to field 925).
- `blind5`/`blind6`/`blind7` (22/23/24) are per-digit validators (gated var
  1042, reading 1024-1026); `blind8` (25) is the code display (reads the live
  code digits 1029-1032 and draws them).
- Key switches: var 1040 = guard patrol on/off; var 1042 = round active.

No opcode 0x4A (ASK) appears in entities 0-25, so the fail dialog ("Rinoa:
What happened!?") lives in the still-unread `blind9`/`blind10` (26/27) or is
requested during the method-4 reload.

This build:
- Re-narrows `DumpGuardScripts` to entities 26-30 (`blind9`, `blind10`,
  `hatch`, `point`, `light`) so the last unread suspects land alone at the top
  of the log, to find the fail-ASK and any player-distance check.
- Adds `GuardVarLog()` (`GUARD_VAR_DIAG`, called from `PollWindows`): on
  `tilink*`, once per ~500 ms, logs the live byte values of vars 1040, 1042,
  1028, 1038, 1039, 1043, 1044, plus the entered-code slots 1024-1027 and the
  target code 1029-1032, as `[GUARDVAR]` in `ff8_field.log`. Paired with the
  still-on `[GUARDPOS]` recon, a real run (enter code, then get caught, then
  succeed) reveals which value of var 1040 freezes the guards (Manual mode) and
  the success/fail flag pattern (Auto/Skip mode). SEH-guarded, log-only.

Three accessibility modes (refined with Aaron): Auto/Skip = bypass the
minigame via the success path; Manual = player enters the code with guards
frozen; Original = player enters the code with an audio cue for guards
approaching/receding.

## v0.18.3.12

Guard catch hunt (#58). LOCAL diagnostic build.

Static analysis of the v0.18.3.11 all-entity `tilink1` dump cleared entities
0-17 -- the party/NPC sprites, the train-shake (`TrainSindou`) and
`hatchcont`/`view` camera controllers (var 1044 = down/up state), and the
`Noriuturiline1` code-entry line (var 1043, savemap var 724). None of them
holds a guard-vs-Squall proximity check or the fail-ASK ("Rinoa: What
happened!? Squall!!!"). The remaining suspects are entities 18-30: the ten
`blind*` invisible entities plus `hatch`, `point`, and `light`.

`DumpGuardScripts()` now starts its dump loop at entity 18 instead of 0, so
those 13 entities land at the TOP of `ff8_field.log` for a single cheap read
(the earlier 0-17 dumps pushed them into an unreadable mid-log zone).

Diagnostic-only; no gameplay/TTS behaviour change. The dump fires once on the
first field load of the session, so any field reached after launch triggers
it -- no need to navigate to the train.

## v0.18.3.11

Guard catch hunt (#58). LOCAL diagnostic build.

v0.18.3.10's dump proved the catch logic is NOT in GalHei1/GalHei2 (decorative
background sprites), TrainSindou (train-rumble controller), or point (scenery
orchestrator). But the guards DO catch Squall if they get too close during
code entry -- the detection just lives in another entity (or an engine-level
proximity check). Two changes to find it:

1. `DumpGuardScripts()` now dumps ALL 31 `tilink1` entities, not just the four
   name-matched ones, so the proximity/distance check + catch trigger (MAPJUMP
   to a caught field / REQEW-to-caught / fail-flag POPM) can be located. Prime
   suspects: the `blind1`-`blind10` invisible entities and the train-movement
   entities (Downyarukun/Upyarukun). Still fires once per session from any
   field, landing at the top of the log.
2. `GUARD_RECON_DIAG` re-enabled so a deliberate get-caught run logs each
   guard's position and distance-to-player every ~400 ms -- this captures the
   catch threshold (distance at which detection fires) and any position reset.
   The existing MAPJUMP / field-load / dialog hooks capture what the catch
   does. The static dump is at the top of the log and the catch is at the tail,
   so neither buries the other.

Log-only; no behavior change.

## v0.18.3.10

Guard-awareness discovery, retrieval fix (#58). LOCAL diagnostic build.

v0.18.3.9 dumped the guard scripts correctly, but the build also re-enabled the
`[GUARDPOS]` recon flood, and the `tilink1` visit was followed by a return to
the briefing area with F9-navigation logging -- so the one-shot dump ended up
buried in the middle of a 572 KB log, unreachable without grep.

Fix: `GuardJsmDump()` now reads `tilink1`'s script archive **by name**, so it
fires from ANY field (including the briefing-room save) -- no traversal to the
live coupling field needed -- once per game session on the first valid field.
The `[SCRIPT-DUMP]` block lands near the TOP of the session log, readable with
`head`. `GUARD_RECON_DIAG` turned back off (the v0.18.3.7 first-pass patrol map
is enough; the flood was what buried the dump). DumpGuardScripts retries until
the field archive is ready, then dumps GalHei1/GalHei2 + TrainSindou + point.
Log-only; no behavior change.

## v0.18.3.9

Guard-awareness discovery (#58). LOCAL diagnostic build, not for push.

First discovery build for the train guards, following the v0.18.3.7 `[GUARDPOS]`
recon (ents 5/6 = GalHei1/GalHei2 patrol X~-1315, Y~[-505,1640], ~150 u/s; a
guard passed within 71 units of the stationary player at the device with no
catch, so the catch fires during the crossing, not at the panel). To find the
catch mechanic this build adds a static JSM dump of the guard + controller
scripts on `tilink1`:

- `FieldArchive::DumpGuardScripts()` (field_archive_jsm_dump.inl) dumps the SYM
  entities named `galhei*` (GalHei1/GalHei2) plus the prime controller
  candidates `TrainSindou` and `point`, via the existing `DumpEntityScript`.
- `GuardJsmDump()` (gated `GUARD_JSM_DUMP_DIAG 1`, field_dialog_diag.inl) fires
  once on `tilink1` entry from `PollWindows` -> `[SCRIPT-DUMP]` in ff8_field.log.
- `GUARD_RECON_DIAG` re-enabled so the same run refreshes the `[GUARDPOS]`
  patrol data.

Reading targets in the dump: the patrol MOVE loop, the line-of-sight/proximity
check against the player, and what a "spotted" event triggers (likely a MAPJUMP
to a caught field or a fail-flag `POPM`). Log-only; no behavior change.

## v0.18.3.8

#56 DONE -- cleanup after the real-train code announce was BAT-confirmed.

v0.18.3.7 BAT (2026-06-09) confirmed the announce reads the real-train code
correctly: `[TRAINCODE-SAY] (2221)` matched `[TRAINWIN]` 1029-1032 = 2,2,2,1 and
`(4334)` matched 4,3,3,4, and Aaron entered the spoken codes and uncoupled the
car. The code-announce feature (1026-1029 on `tiagit*`, 1029-1032 on `tilink*`)
is complete. This version just turns the discovery diagnostics off:
`TRAIN_FIELD_SCAN_DIAG` (`[TRAINWIN]`/`[TRAINSCAN]`/`[TRAINFIELD]`) and
`GUARD_RECON_DIAG` (`[GUARDPOS]`) both -> 0. All diag code is retained behind
its `#define` for one-line re-enable. No behavior change to the shipping
features.

Guard recon first pass (seeds #58): the two patrollers on `tilink1` are ent5
(GalHei1) and ent6 (GalHei2), sweeping a single corridor at X ~= -1315, Y from
~ -505 to ~ +1640, ~150 units/sec, in opposite phase. talk=128/push=48 (same as
all entities, so detection is script logic, not radius). A guard passed within
71 units of the stationary player at the device with no catch -- the catch is
during the crossing, not at the panel. Recon posted to #58.

## v0.18.3.7

Announce the real-train uncoupling code (#56). LOCAL, awaiting functional BAT.

The v0.18.3.6 `[TRAINWIN]` map located the real-train code on `tilink1`: vars
**1029-1032** held a stable 4-digit 1-4 code for ~5s then cycled cleanly
(`2421` -> `2433` -> `4122`), while 1026-1029 (the practice location) read
`[1 1 1 x]`. The 1029-1032 window was the most stable of the candidates
(1030-1033 wobbled within a hold; 1028 was empty), so the real train uses the
practice apparatus's varblock shifted +3. The apparatus entities themselves
(`Angoyarukun`/`Keykantoku`/`Keyjokantoku`) have no `POPM` writes on `tilink1`
(only reads + dispatch), so the JSM dump couldn't pin it -- the runtime map did.

`TrainCodeAnnounce` now selects the code location per field: 1026-1029 on
`tiagit*` (practice, confirmed), 1029-1032 on `tilink*` (real train). Gate
re-widened to `tiagit*`/`tilink*`. `SETTLE_POLLS` 3 -> 5 (~500ms) to skip the
real train's noisier mid-rewrite transients. JSM dump turned back off;
`[TRAINWIN]`/`[TRAINSCAN]` field scan kept on as a safety net. Functional BAT:
at the `tilink1` panel, the spoken code should uncouple a car when entered.

Also bundles **guard-patrol recon** for #58 (`[GUARDPOS]`, `GUARD_RECON_DIAG`
in `field_nav_observe.inl`, called from `ObserveArrowResponse`): on `tilink1`,
logs each visible moving entity's live world position, distance to the player,
and talk/push radius (~400ms throttle, movers only). Lets the one functional
BAT verify the code AND map the guards' (GalHei1/GalHei2) patrol range, speed,
and radii in a single run.

## v0.18.3.6

Locate the real-train code vars (#56). LOCAL diagnostic, not for push.

v0.18.3.5 BAT confirmed the real code-entry field is **`tilink1`** (id 902;
field-load trail tiyane1/2/3 -> titrain1 -> tilink1), whose SYM carries the
same apparatus as the practice panel (`Angoyarukun`/`Keykantoku`/`Keyjokantoku`).
The widened gate fired and announced there -- BUT the 1026-1029 reads were
`[1 1 1 3]` / `[1 1 1 4]`: three pinned 1s with only the last digit moving. The
practice panel never produced three identical leading digits, so 1026-1029 is
NOT the live code on `tilink1`; the digits sit at different var indices.

This build finds them: `TrainFieldScan` now dumps a 64-var window (1008..1071)
on `tilink*`/`titrain*` as a "digit if 1-4, else dot" map, logged only when the
1-4 pattern changes (`[TRAINWIN]`, column i = var 1008+i) so the four bytes that
cycle together show up by column. The JSM dump (`TRAIN_JSM_DUMP_DIAG`) is
re-enabled and its gate widened to `tilink*`/`titrain*` as a cross-check
(the same tool that pinned 1026-1029 on the practice field). `TrainCodeAnnounce`
is reverted to `tiagit*` only so it won't speak an unverified code under the
train timer; it will be re-widened with the correct indices once located.

## v0.18.3.5

Real-train code-field discovery (#56). LOCAL diagnostic, not for push.

The v0.18.3.4 announcement works on the briefing-room practice panel (tiagit5)
but not on the actual moving-train code-entry: `TrainCodeAnnounce()` is gated
to `tiagit*`, and the real train uses a different field name, so it bailed out
before reading anything (field navigation isn't name-gated, which is why nav
worked there). This build adds `TrainFieldScan()` in field_dialog_diag.inl
(gated `TRAIN_FIELD_SCAN_DIAG`), called from PollWindows on every field: it
logs the field name+id on change (`[TRAINFIELD]`) and, whenever bytes
0x1CFE9B8 + 1026..1029 all read 1-4, logs that quad on change (`[TRAINSCAN]`).

Goal: learn the real train code-entry field name and confirm whether the code
lives at the same varblock spot there. If `[TRAINSCAN]` cycles a 1-4 code on
the train field, the code is at 1026-1029 there too; if not, the train field
stores the code elsewhere and gets its own JSM dump.

This build also **widens `TrainCodeAnnounce`'s field gate** from `tiagit*` to
also cover `titrain*` / `tilink*` -- the real train code-entry fields
identified from the v0.18.3.4 field-load trail (tiyane1/2/3 -> titrain1 ->
tilink1; "link" = the car coupling). Since the train uncoupling is the same
minigame as the practice, it very likely reuses the same apparatus/vars, so
the code should now announce on the train this build; `[TRAINSCAN]` confirms
which field carries it and that it reads at 1026-1029.

## v0.18.3.4

Timber-train uncoupling-code announcement (#56) -- first working accessibility feature for the train minigame.

The v0.18.3.3 `[CODEVAR]` probe confirmed the four code digits are stored as
bytes at field-varblock `0x1CFE9B8 + 1026..1029` (values 1-4), read left to
right. This build adds `TrainCodeAnnounce()` in field_dialog_lifecycle.inl,
called from PollWindows: on a `tiagit*` field it reads those four bytes each
poll and, when a new code settles (the same quad held for a few consecutive
polls -- enough to skip the sub-second mixed states while the script rewrites
the four bytes one at a time), speaks it as four words via ScreenReader::Speak in
assertive/interrupt mode (aria-live "assertive" -- it purges queued/in-progress
field TTS so a fresh, time-sensitive code never waits), e.g. "Code. four, three, three, one." It announces once per settled code, so as
the practice panel cycles (~5s) the current code is read out each time it
changes. SEH-guarded; logs `[TRAINCODE-SAY]` to ff8_field.log.

The three discovery diagnostics from the prior builds are now gated off
(retained): `TRAIN_PROBE_DIAG` (window-text rule-out), `TRAIN_JSM_DUMP_DIAG`
(code-apparatus JSM dump), and `TRAIN_CODEVAR_DIAG` (varblock-addressing
probe). Still local; not pushed (the wider Timber chapter -- #57 key layout,
#58 guards, #59 timer -- is ongoing).

## v0.18.3.3

Runtime varblock probe for the Timber-train code digits (#56). LOCAL diagnostic, not for push.

The v0.18.3.2 `[SCRIPT-DUMP]` JSM dump located the four code digits in field-
varblock vars 1026-1029: on field `tiagit5`, Keykantoku (ent13) `method[4]`
buckets the raw randomized code and writes the human digit 1-4 into each via
`POPM_L`; Angoyarukun (ent12) copies each into scratch var 1030 and `REQEW`s
Keykantoku to draw it; Keyjokantoku (ent14) draws the sprites and runs the
randomizer (opcode 0x21) + validation. Because the digits are written with the
LONG memory op at consecutive indices (raw byte-offset longs would overlap),
the long bank's exact byte addressing isn't certain from the static dump.

This build adds `TrainCodeVarProbe()` in field_dialog_diag.inl (gated `#define
TRAIN_CODEVAR_DIAG 1`), called from PollWindows after the JSM dump. On any
`tiagit*` field it reads vars 1026-1029 under five candidate addressings from
the varblock base 0x1CFE9B8 (the same base the mapjump resolver uses for word
vars): byte / word / long at base+idx, word at base+idx*2, and long at
base+idx*4. Output is tagged `[CODEVAR]` in ff8_field.log, logged whenever the
combined value set changes plus a ~2s heartbeat so a screenshot always lines up
with a recent line. SEH-guarded; log-only, no speech or writes. BAT on the
practice panel + one F11 to match the quad against the known codes (1232 /
4331) and pin the correct interpretation, then implement the #56 announcement.

## v0.18.3.2

Timber-train code-apparatus JSM dump (#56). LOCAL diagnostic build, not for push.

Static disassembly of FF8_EN.exe established the code-display mechanism
conclusively: the field/dialog text engine (measure pass 0x4a0d10, draw pass
0x4a1200) has NO inline "insert number from a variable" control code -- FF8
pre-flattens dynamic numbers into literal glyph bytes before drawing. Since
win[4]'s buffer is byte-frozen while the on-screen code changes, the yellow
code digits are NOT in any window text buffer; they are drawn by a separate
number-sprite routine reading the 4 values from the field varblock. The exe
holds the engine, not the per-field code identity (that lives in the tiagit5
field script), so the exact varblock addresses are found by reading the JSM.

This build adds `FieldArchive::DumpTrainCodeScripts()` (in
field_archive_jsm_dump.inl), which dumps the decoded opcode stream of the
code-apparatus entities (SYM names ango*/key* -- Angoyarukun + Key*
supervisors, falling back to all 'Other' entities if no name match) via the
existing DumpEntityScript decoder. It is triggered once per field entry by
`TrainCodeJsmDump()` in field_dialog_diag.inl (gated `#define
TRAIN_JSM_DUMP_DIAG 1`), called from PollWindows when the current field name
starts with "tiagit". Output is tagged `[SCRIPT-DUMP]` in ff8_field.log. From
the dump we read, statically: the random-code generation (random 1-4 x4), the
POPM_W varblock addresses the 4 digits are stored at, and the draw-number
opcode -- which yields the exact addresses the announcement (#56) will read.
Log-only; no speech, no memory writes, no new engine hook.

## v0.18.3.1

Timber train code-channel probe refined to capture live code bytes (#56).
LOCAL diagnostic build, not for push.

The v0.18.3.0 BAT settled the key question: Rinoa's uncoupling code rides the
normal AMES/window text channel, not a hidden numeric-sprite channel. During
the briefing-room practice (field `tiagit5`) the code apparatus occupies window
slots: win[4] = the code ("The code is..." followed by four button-icon glyphs
that decode to punctuation `. + - =` and the whole line is rejected by
IsGarbledText), win[5] = status ("Enter the code" / "Code input error", speaks
fine), win[3] = "Press [icon] to quit", win[2] = a stable "4 3 1   2" (likely
the keypad legend). So #56 needs no new engine hook -- the fix is ours: detect
the code context, bypass the garble filter, and translate the four icon bytes
to a spoken digit/button sequence.

The one missing piece is the raw bytes of a LIVE code (v0.18.3.0 logged decoded
text per window plus the glyph buffer, and its 2s raw dump never coincided with
a live code, so the icon-byte -> digit mapping isn't yet derivable). This build
reworks `TrainProbeDump` to log per-window RAW HEX + decoded, change-triggered
(FNV-1a per slot) so each ~5s code refresh in win[4] is captured exactly once
with its bytes and idle frames produce no spam. Tagged `[TRAINCODE]`, written
via Log::Write to `ff8_mod.log`. Still pure logging -- no speech, no writes, no
new hook, no F11/F12 (auto-fires on field).

## v0.18.3.0

Timber train hijack minigame — accessibility chapter begins (#60). Code-entry
code-channel rule-out probe (#56). LOCAL diagnostic build, not for push.

The Timber train uncoupling minigame is being made playable for blind players
as three assistance modes (Auto / Manual / Original), mirroring the X-ATM092
chase. Filed the chapter as #60 (umbrella) with #56 (announce Rinoa's code
numbers), #57 (code-entry key layout), #58 (guard awareness), #59 (timer).

First target is the code-entry mechanic (#56), mappable on the briefing-room
PRACTICE code panel — no timer, no guards, repeatable. The mod already catches
every text channel it knows about (opcode_mes, field_get_dialog_string,
show_dialog, and the get_character_width glyph accumulator), yet Rinoa's code
numbers are never spoken, which points to the code being drawn by the
fixed-width numeric-sprite routine (HP/gil/timer font) that bypasses all of
them.

This build adds `TrainProbeDump()` in `field_dialog_diag.inl` (gated behind
`#define TRAIN_PROBE_DIAG 1`), called from `PollWindows`. While on a field it
logs, unfiltered: (1) the current field name + id on change (also seeds the
train_detector field set), (2) every window slot's decoded text with no
min-length/garbled filter, and (3) the raw + decoded get_character_width glyph
buffer. Pure logging — no speech, no memory writes, no new engine hook, no F12
(auto-fires on field). If the practice code appears in none of these channels,
v0.18.3.1 hooks the numeric-sprite routine; if it appears but is being
filtered/deduped, the fix lives in code we already own.

## v0.18.2.50

Status Page 2 key 3 (Status Attack) — implemented with name + percent (#54).
Completes the status detail submenu; ready to ship Pages 1/2/3 together.

The percent turned out to be readable after all. ST-Atk-J inflicts status at 1%
per spell stocked (max 100), so the displayed percent is simply the stock count
of the junctioned spell (confirmed via Final Fantasy Wiki, Neoseeker, GameFAQs).
The "66" found earlier in the magic-inventory list wasn't a coincidence — Squall
had 66 Berserk stocked, and 66 stock = 66%. (The three kernel-scanner attempts
were the wrong tool: this value was never in a kernel table we needed to find.)

Key 3 now reads the junctioned ST-Atk magic id (savemap char+0x66 = FFNx
j_atk_mtl), names the spell from a magic-id table validated against our own data
(id 7 = Thunder, id 46 = Berserk both match), and reads the percent as that
spell's stock count from the per-character magic inventory (FFNx
savemap_ff8_character.magics[32] at char+0x10; each u16 = id in the low byte,
quantity in the high byte). Speaks e.g. "Status attack, Berserk, 66 percent";
logs magicId + stock for verification. It names the junctioned spell (not the
inflicted status) to stay exact and self-verifiable rather than ship an
unsourced spell->status mapping. ST_PAGE2_DIAG flipped off (discovery done);
ST_MAGSCAN stays shelved.

## v0.18.2.49

Status Page 2 key 3 — shelved the kernel Magic-table scanner, pivoted back to the
computed-slot diff method (#54, LOCAL/diagnostic only).

Three scanner variants (v1–v3, .46–.48) each false-matched or found nothing: v1
hit a UI vertex buffer, v2 a curve/lookup table, and v3's strict 48-entry
u16-id==index run found no match anywhere, with the fallback only turning up
.text code bytes. Conclusion: the in-memory magic data isn't a flat, id-indexed
0x3C array we can fingerprint by scanning. The anchor itself was also suspect —
the "66" came from the status-DEFENSE reading, not status-attack.

Set ST_MAGSCAN=false (scanner code kept, gated off) and re-enabled ST_PAGE2_DIAG
to dump the computed-stats slot on Page 2. The status-attack data is almost
certainly cached in that slot (the battle engine needs it to inflict status),
as a status bitmask + percent near the already-solved elem-attack fields
(+0x1C4/+0x1C5). The plan: capture the slot with Berserk on ST-Atk-J, locate the
Byte(s) holding the Berserk status bit + its percent, and confirm by diffing
against a different status spell — the same empirical method that solved
elem-attack (Thunder/Water/Ice) and the resistances. No shipping changes.

## v0.18.2.48

Status Page 2 key 3 — kernel Magic-table scanner v3 (#54, LOCAL/diagnostic only).

The v0.18.2.47 scanner hit a second false positive (base 0x00D497D4, a curve /
lookup table of smooth ramp bytes): its two-entry id check accepted a u8-at-+0x04
match, and a single coincidental byte plus stray anchor bytes were enough.
Replaced with a full 48-entry run requiring the u16 id field at +0x04 to equal
the entry index (0-based, with a 1-based fallback) — a coincidence across 48
consecutive u16==index values at 0x3C stride is unique to the real Magic table,
and both prior false positives are rejected on entry 0. The value-byte anchors
(Thunder 0x2B, Berserk 0x42) are now informational (logged, not required) so a
surprise in the stored values can't reject the real table. Fallback unchanged.
Still one-shot, SEH-guarded, log-only, gated to Page 2; no shipping changes.

## v0.18.2.47

Status Page 2 key 3 — kernel Magic-table scanner v2 (#54, LOCAL/diagnostic only).

The v0.18.2.46 scanner returned a false positive (base 0x00CDB728, a UI vertex
buffer in the exe data section: 20-byte records of rising screen coords ending
in an `80 80 80 24` color, which tripped the loose name-offset-monotone
heuristic and happened to contain a stray 0x42). Because the scan stops at the
first hit, that junk match blocked the real table. Replaced the heuristic with a
strict, self-verifying match: at a candidate base the per-entry magic-id field
(tried at +0x04/+0x00/+0x02 u16 and +0x04 u8) must equal the entry index at BOTH
anchor entries (7 = Thunder, 46 = Berserk) AND those entries must carry their
known value bytes (0x2B and 0x42). If the strict pass finds nothing, a fallback
lists up to six bases that merely carry both anchor bytes (with their +0x04 id
reads and e7/e46 dumps) for manual identification. Read window widened to cover
entry 46 fully (NEED = 47 entries). Still one-shot, SEH-guarded, log-only, gated
to Page 2; no shipping behavior changes.

## v0.18.2.46

Status Page 2 key 3 (Status Attack) groundwork — kernel Magic-data discovery
diagnostic (#54, LOCAL/diagnostic only).

Key 3's status name + percent come from the kernel.bin Magic table, which isn't
cached in the computed-stats buffer and isn't exposed as a base pointer by FFNx
(its named magic loaders are the battle effect/animation data, not the stat
table). Added `PollStatusMagScan` (behind `ST_MAGSCAN`, gated to Page 2): a
one-shot scan that walks committed/readable memory via VirtualQuery for a
0x3C-stride array whose per-entry magic-id field counts up, confirmed by the
content anchor that entry 46 contains 0x42 (Berserk's status-attack value 66
from the prior BAT). On a hit it logs the base and dumps entries 0/1/7/27/46 in
full so the real field offsets (status-attack byte, status bitmask) can be read
rather than assumed. SEH-guarded, log-only, no speech/writes; no change to any
shipping behavior. Once the base + offsets are confirmed, key 3 will read the
kernel entry directly (status from the bitmask, percent from the status-attack
byte) and `ST_MAGSCAN` flips off.

## v0.18.2.45

Status Page 2 Elemental Resistance (key 2) — resist / immune / absorb wording (#54).

The elemental-defense reading now interprets the FF8 elem-def value (word - 800)
as three cases instead of a flat "resist N percent":

- value below 100  -> "<Element> resists N percent" (N = value)
- value exactly 100 -> "<Element> immune"
- value above 100   -> "<Element> absorbs M percent" where M = value - 100

So Squall's Blizzaga-on-Elem-Def-J (Ice word 920 = value 120) now speaks
"Ice absorbs 20 percent" instead of the misleading "Ice resist 120 percent".
Verified against the v0.18.2.44 BAT log ([STPAGE2] key=2 read 120). Negative
values still read "<Element> weak N percent". Status Resistance (key 4) is
unchanged — status effects can be resisted or made immune but never absorbed.

## v0.18.2.44

Status Page 2 (Elemental & Status) TTS — implemented (#54). Completes the status
detail submenu (Pages 1/2/3). Discovery diagnostic flipped off.

Five `[ST2DIAG]` BATs located every Page-2 array inside the computed-stats slot
(0x1CFF000 + slot*0x1D0) and confirmed each scale against on-screen values:

- Key 1 Elemental Attack: element bitmask +0x1C4 (Fire 0x01 / Ice 0x02 / Thunder
  0x04 / Earth 0x08 / Poison 0x10 / Wind 0x20 / Water 0x40 / Holy 0x80) + percent
  +0x1C5. Verified Thunder 43%, Water 100%, Ice 80%.
- Key 2 Elemental Resistance: 8 little-endian words +0x194, percent = word - 800
  (neutral 800). Verified Blizzara->Elem-Def gives Ice word 824 = 24% on screen.
  Spoken as resist / weak. Element order Fire/Ice/Thunder/Earth/Poison/Wind/Water/Holy.
- Key 4 Status Resistance: 13 bytes +0x1A4, percent = byte - 100 (neutral 100).
  Verified across 0% / 4% / 66% data points. Status order Death/Poison/Petrify/
  Darkness/Silence/Berserk/Zombie/Sleep/Slow/Stop/Curse/Confuse/Drain.
- Key 3 Status Attack: reads the junctioned ST-Atk magic id (savemap char+0x66).
  Its status name + percent are NOT cached in the computed buffer, so v1 reports
  presence only and logs the magic id; a magic->status table is the pending
  refinement (does not block Pages 1/2/3 shipping together).

Entering Page 2 announces "Elemental and Status" + the four key hints; each key
speaks labeled values (rule #44). `ST_PAGE2_DIAG` set false; the `[ST2DIAG]`
diagnostic is retained behind the flag for the later Status-Attack table work.
No new source files (all in `menu_tts_status.inl`; the dispatch in `menu_tts.cpp`
already calls `PollStatusDetailPages` / `StatusDetailHotkeys`).

## v0.18.2.43

Status Page 2 (Elemental & Status) discovery diagnostic (#54) — LOCAL build, not
for release (diagnostic flag is on).

- Added `[ST2DIAG]`, gated to Page 2 (focus +0x22E==3, page +0x257==1). For the
  viewed character it logs the savemap junction inputs (char+0x65..0x6E: Elem-Atk,
  ST-Atk, 4x Elem-Def, 4x ST-Def magic IDs) and a hex dump of the computed-stats
  slot at 0x1CFF000 (non-zero 16-byte rows across the full 0x1D0 struct).
- Purpose: the displayed Page-2 percentages bypass the GCW buffer and FFNx leaves
  the elem/status arrays unnamed inside `ff8_char_computed_stats.unk1[370]`. Pairing
  the dump with a Page-2 F11 screenshot lets the per-element / per-status arrays and
  their scale be located by correlation (the same method that resolved Page 1).
- Log-only, SEH-isolated, no speech/writes, no GCW snapshot. `ST_PAGE2_DIAG` flips
  off before the Page-2 implementation ships.

## v0.18.2.42

Hardened the Page-1 weapon parser's command skip-list (#54).

- The weapon is read from the GCW panel by skipping the command box that
  precedes it. The skip-list is now the complete, verified set of FF8 command
  abilities (18 total + innate Attack): Absorb, Card, Darkside, Defend, Devour,
  Doom, Draw, GF, Item, Kamikaze, LV Down, LV Up, Mad Rush, Magic, MiniMog,
  Recover, Revive, Treatment.
- Removed three entries that are not command abilities: Mug (not in FF8) and
  Med Data / Junk Shop (those are Menu abilities, never in the battle command
  box).
- Added both spacing/hyphen variants for the multi-word names whose exact
  in-game rendering is uncertain (Mad Rush / Mad-Rush, LV Up / LV-Up, etc.), so
  an unusual command sitting before the weapon can't leak into the readout.

## v0.18.2.41

Status Page 1 key 9 — equipped weapon (#54).

- Page-1 key 9 now announces the equipped weapon, e.g. "Weapon, Revolver" /
  "Weapon, Metal Knuckle". FF8 has no armor or accessories, so the weapon is the
  whole of equipment.
- The name is read from the game's own rendered text in the page-1 GCW panel
  (so it always matches the screen), not a hand-rolled weapon-ID table. The
  command box between the labels and the weapon is junction-dependent, so the
  parser isolates one panel between two menu-bar repeats and skips the command
  tokens after the innate "Attack", taking the trailing run as the weapon.
- Page-1 entry hint updated to include "9 weapon".

## v0.18.2.40

Status Page 1 experience now matches the screen (#54).

- Key 1 (experience) previously spoke only the running total. The status
  screen also shows "Next LEVEL" — the EXP remaining to the next level — which
  the F11 screenshot of Squall's page confirmed (Current EXP 19690, Next LEVEL
  310). Key 1 now speaks both, e.g. "Experience 19690. 310 to next level."
- FF8 characters take a flat 1000 EXP per level, so to-next = level×1000 − EXP
  (20×1000 − 19690 = 310, an exact match to the screen). At level 100 it says
  "maximum level" instead of a remaining figure.

## v0.18.2.39

F11 screenshot index — makes manual captures discoverable from the logs.

- Each F11 press now writes an inline `[F11-SHOT]` marker into the log channel
  matching the current game mode (menu shot → `ff8_menu.log`, battle → battle,
  field → field, world → world, else mod), so the screenshot record sits next
  to the on-screen context it captured. Grep `[F11-SHOT]` to jump straight to a
  capture point.
- After each press it re-appends a cumulative `[F11-INDEX]` block (sequence #,
  filename, timestamp, channel) to that same log, so a tail read taken right
  after a screenshot burst surfaces the full list and the filenames — the latest
  block is always the complete one.
- A final consolidated `[F11-INDEX]` is written to `ff8_mod.log` at shutdown for
  whole-log reads.
- Filenames recorded as `.png` basenames under `Logs\screenshots` (the capture
  writes both `.bmp` and `.png`). The previous single mod-log line is kept as a
  `[F11-SCREENSHOT]` trail with the sequence number and active channel.

## v0.18.2.38

Status detail pages 1 & 3 (#54) confirmed; discovery diagnostics off.

- BAT (Squall + Zell) confirmed Page 1 (Character Statistics) and Page 3 (GF
  Compatibility) read the correct character on both. The viewed-character
  resolution is verified: the roster index and the independent HP cross-check
  agree (Zell = index 1), and the computed buffer puts the viewed character in
  slot 0 regardless of party position (viewing Zell: comp[0] = Zell's HP 866,
  with Squall in comp[1]). The HP-match slot picker handles it either way.
- Turned `ST_DETAIL_DIAG` off now that the slot/character question is settled
  — the `[STDETAIL]`/`[STCALC]` discovery dumps no longer spam the menu log.
  The production `[STPAGE]`/`[STPAGE1]`/`[STPAGE3]` lines remain for ongoing
  verification.
- Known cosmetic: Siren's GF name decodes with a stray trailing glyph
  ("SirenA"); affects `DecodeGFName` (so the GF grid too). Noted for cleanup.
- Compatibility values confirmed on a 0–1000 display scale; Zell's Shiva 0 /
  Ifrit 1000 is genuine per-character affinity data, not a misread.

## v0.18.2.37

Status detail pages 1 & 3 (#54) — Character Statistics + GF Compatibility TTS.

- Page 1 (Character Statistics): number-key hotkeys read each labeled field from
  the computed-stats buffer `ff8_char_computed_stats` (0x1CFF000, stride 0x1D0).
  Key 0 overview (name, level, HP current of max), 1 experience, 2-7
  Strength/Vitality/Magic/Spirit/Speed/Luck, 8 Evade and Hit. Stat-field offsets
  were resolved by disassembling `compute_char_stats_sub_495960` (curHP +0x172,
  maxHP +0x174, level +0x1B8, STR +0x1BB, VIT +0x1BC, MAG +0x1BD, SPR +0x1BE,
  SPD +0x1BF, LUCK +0x1C0, EVA +0x1C1, HIT +0x1C2) and cross-corroborated by the
  junction-bonus factor array order. Equipment (key 9) deferred.
- Page 3 (GF Compatibility): key 0 enumerates obtained GFs with this character's
  compatibility value and a "junctioned" marker, reading the savemap directly
  (reusing the GF module's verified model: obtained = GF rec[+0x11]; name via
  DecodeGFName; compatibility = (6000 - char[+0x70 + gf*2]) / 5; junctioned-to-
  this-char = char[+0x58] bit gf). No GCW glyph needed for the marker.
- On entering either page the mod announces the page name, the viewed character,
  and the available number keys. Pages are static (no in-game cursor), so reads
  are mod-side number-key hotkeys gated to the page (cursor==3, focus==3,
  +0x257 == 0 or 2). Per rule #44 every value is spoken with its label.
- Viewed character resolved from the char-select roster (+0x1DB indexed by
  +0x1E9); the computed slot is matched by the character's current HP and falls
  back to slot 0 (the confirmed viewed-char slot). A `[STPAGE]` log records the
  roster index, an HP-match cross-check, and the resolved slot so a benched /
  non-leader character (e.g. Zell) confirms the slot mapping next test.
- `ST_DETAIL_DIAG` discovery diagnostics (`[STDETAIL]`/`[STCALC]`) left enabled
  this build for that confirmation; they flip off once #54 fully ships.

## v0.18.2.36

Status detail pages 1-3 (#54) — computed-stats buffer hunt (LOCAL, not for release).

- Added `[STCALC]`, a second log-only discovery diagnostic in
  `menu_tts_status.inl` (under the same `ST_DETAIL_DIAG` flag). On any Status
  detail page it dumps, once per page/character change: the 8 savemap base
  character records (stored HP/maxHP + pre-junction base stats, as ground
  truth) and the three slots of the computed-stats buffer
  `char_comp_stats_1CFF000` (`ff8_char_computed_stats[3]`, stride 0x1D0;
  curr_hp +0x172, max_hp +0x174, stat_multiplier +0x1B8), plus the char-select
  cursor hint. All reads SEH-isolated; speaks/writes nothing; makes no GCW
  snapshot, so it cannot interfere with `PollStatusLimit`.
- Purpose (existing-knowledge-first, confirmed against FFNx `ff8_data.cpp` /
  `ff8.h`): the v0.18.2.35 BAT proved the page numbers bypass the GCW text
  pipeline, and FFNx identifies `0x1CFF000` as the battle/active computed-stats
  buffer (not `character_data_1CFE74C`, which is the base/working records). This
  build answers the go/no-go: does a `0x1CFF000` slot hold the viewed
  character's computed HP while a Status page is open, and which slot maps to
  the viewed character? If yes, page-1 stats for active members read straight
  from this buffer; a follow-up maps the stat-field offsets via a junction-diff.
- No behavior change for players; diagnostic only. `ST_DETAIL_DIAG` flips to
  false when #54 ships.

## v0.18.2.35

Status detail pages 1-3 (#54) — discovery diagnostic build (LOCAL, not for release).

- Added `[STDETAIL]`, a log-only discovery diagnostic in `menu_tts_status.inl`
  behind a new `ST_DETAIL_DIAG` compile-time flag (on for this build). It fires
  on the Status detail view for the three non-limit pages (detail focus
  +0x22E==3, page +0x257 != 3), logging the page byte, the cursor band
  (+0x25F..+0x264), and the decoded GCW text on any page/band/text change. All
  reads are SEH-isolated; it speaks nothing and writes nothing.
- Purpose: map which +0x257 value is which page (P1 stats / P2 resistances /
  P3 GF compatibility) and determine whether the font-rendered stat numbers and
  percentages reach the GCW (get_character_width) buffer at all — or whether a
  separate number-draw routine bypasses it, in which case the next step is to
  locate the computed-stats render buffer (the base-stat struct at
  character_data 0x1CFE74C is NOT it: page 1 shows junction-inclusive computed
  values, not the struct's base stats).
- Wired `PollStatusDetailDiag()` into the existing `sub==5` Status dispatch in
  menu_tts.cpp, after `PollStatusLimit()`. On the limit page the diag early-
  returns so only `PollStatusLimit` drains the GCW buffer (no double drain); on
  pages 1-3 `PollStatusLimit` early-returns before touching the buffer, so the
  diag is the only consumer.
- No production behavior change: with `ST_DETAIL_DIAG` off this is inert.

BAT: menu > Status > pick a character > on the detail view, cycle pages with
L1/R1, pausing a moment on each (pages 1-3 are static info displays — no cursor,
so there is nothing to arrow through). Then read `ff8_menu.log` and grep
`[STDETAIL]` — report the page byte per page and whether the `text=` field
contains the stat numbers / percentages or only the labels. The band is expected
to stay constant (confirming there is no per-page cursor).

## v0.18.2.34

Status Limit Break page TTS (#49) confirmed complete on Squall, Zell, Quistis,
Rinoa, Selphie.

- Restored the `[STBAND]` and `[STLIMIT]` diagnostics behind a new
  `ST_LIMIT_DIAG` compile-time flag (off by default), generalized to log the
  active character. Flip it to true to map a new character's limit page (e.g.
  Irvine's Shot limit).

## v0.18.2.33

Status Limit Break page TTS (#49) — BAT #6, Squall silent slot fixed.

- Fixed the silent slot on Squall's page (down from Rough Divide / the cell next
  to it announced nothing). The `[STBAND]` dump confirmed only +0x25F moves, and
  it advances by 1 across the finishers (cur 4-7 = Rough Divide / Fated Circle /
  Blasting Zone / Lion Heart) while the two toggles occupy cur 0-3. The config
  had step=2, which mapped cur=4 and cur=5 to the same row index, so cur=5 was
  silently deduped against Rough Divide. Changed Squall to step=1,
  leadingToggles=4 (moveIdx = cursor - 4); cur=5 now reads as an empty Fated
  Circle slot.
- Removed the temporary `[STBAND]` and `[STLIMIT]` diagnostics now that the
  chapter is complete.

## v0.18.2.32

Status Limit Break page TTS (#49) — BAT #5.

- Fixed the first ability after a toggle being swallowed (Zell: moving from
  Duel-Auto down to Punch Rush announced nothing until you moved again). The
  toggle path returns early, so the cursor-band snapshot wasn't maintained while
  on a toggle; the move onto the adjacent list row then registered no change. We
  now remember that we just left a toggle and force the arriving row to announce.
- Added a temporary `[STBAND]` diagnostic on Squall (full cursor-band dump on any
  change) to identify which byte the silent slot between the Renzokuken Indicator
  and Rough Divide uses — that test wasn't captured in the previous run's log.
  Removed once #49 closes.

## v0.18.2.31

Status Limit Break page TTS (#49) — BAT #4 fixes.

- Zell move descriptions now correct. The baked-description lookup was indexed by
  the displayed row number, but with only some Duel moves learned the displayed
  list is compressed (Burning Rave is the 5th learned row but the 7th move in the
  full table), so it returned the wrong move's description. Each displayed row is
  now mapped back to its full-list index, so Burning Rave correctly reads "Damage
  all enemies."
- Renzokuken Indicator now announces "disabled" when focused while Gunblade Auto
  is on. The F11 screenshot confirmed the row is greyed out but still carries its
  "Set Renzokuken Indicator" help, so it was being read as a normal on/off toggle
  (or going silent when the help buffer lagged) — the "unannounced slot next to
  Rough Divide" Aaron heard. The toggle path now special-cases it, and the
  per-character cursor byte below makes the lagged-buffer path catch it too.
- Cursor detection is now pinned to each character's known band byte (Squall
  +0x25F, Zell +0x260, Rinoa +0x263; Quistis still auto-detects) instead of
  "whichever byte moved," which was grabbing a flickering highlight byte on
  Squall's page and mis-resolving the focused row.

## v0.18.2.30

Status Limit Break page TTS (#49) — BAT #3 fixes for all three flagged items.

- Move descriptions read on "/" (and on arrow) are now correct on Squall and
  Zell. Root cause: on the two toggle-bearing pages the GCW help text lags the
  cursor by one row and never refreshes while the cursor is still — confirmed in
  the log, where Burning Rave read "Damage one enemy" the whole time it was
  focused and only flipped to "Damage all enemies" after moving off it. Those two
  pages now announce a baked target description (Burning Rave and Fated Circle =
  all enemies, every other shown move = one enemy). Rinoa and Quistis have no
  toggle and read the live GCW help correctly, so they're left on the live text.
- Renzokuken Indicator "disabled" is now announced at the moment Gunblade Auto is
  turned on ("Gunblade Auto, on. Renzokuken Indicator disabled"), since the
  disabled row itself can't be focused so it never announced on its own.

Still open: Rinoa's currently-learning ability, per-ability learn %, and the
"Won't learn anything. OK? Yes/No" confirmation dialog (its GCW string is now
confirmed) — all tracked under #50.

## v0.18.2.29

Status Limit Break page TTS (#49) — BAT #2 follow-ups.

- Selphie's Slot limit page now announces "Slot limit. No options to adjust."
  once on entry (confirmed from the GCW: her limit page renders only the stat
  panel — nothing selectable between "Save" and the header), instead of silence.
- Fixed the temporary GCW log flooding the menu log: the previous
  ST_LIMIT_GCW_LOG dumped on every poll because the GCW buffer rotates each
  frame so its dedup never matched, which buried the per-character pages under
  thousands of lines. Removed it; replaced with a single clean [STLIMIT] line
  per cursor move that logs the focused row's cursor value and help-region text.

Still open from BAT #2, to finalize from the clean [STLIMIT] log on the next
pass: the move descriptions read on "/" (Zell reported "Damage 1" and Squall's
Rough Divide read nothing — need to see the exact GCW help bytes per row to fix
the extraction), and the Renzokuken Indicator "disabled" announce (need the
actual Squall cursor value when Gunblade Auto is on — the current branch keys on
cursor +0x25F==2). Quistis and the Rinoa/Zell/Squall move-name readouts are
good. Rinoa's currently-learning ability, per-ability learn %, and the "Won't
learn anything" confirmation dialog are tracked under #50.

## v0.18.2.28

Status Limit Break page TTS (#49) — BAT #1 follow-ups from the first feature
build. The toggles and the Squall/Zell/Quistis move-name readouts worked; this
round addresses four observations:

- Renzokuken Indicator disabled state: when Gunblade Auto is on the game stops
  drawing that row's help text, so the help-keyed path went silent on it.
  Detect the row by cursor instead (Squall row +0x25F==2) and announce
  "Renzokuken Indicator, disabled" while Gunblade Auto is on.
- Empty slots: unlearned move/finisher rows past the learned ones now announce
  "Empty slot" (cursor row index >= the learned count, within the move table).
- Description on "/": each focused move now stores its GCW help text, and the
  "/" key reads it back via StatusLimitSpeakSelectedHelp(), wired into the same
  on-demand help chain as the GF Learn list / Ability / Junction.
- Rinoa read nothing: her Angelo list is preceded by a bare "Angelo" command
  label that stopped the forward-parse before the ability names. Added "Angelo"
  to the skip tokens so the parse continues into Angelo Rush / Angelo Cannon /
  etc.

Selphie's Slot limit has no learnable list, so she stays silent by design (not
in the per-character table). A temporary [STLIMIT-GCW] log (ST_LIMIT_GCW_LOG,
remove next) dumps the decoded GCW on the limit page so this BAT confirms Rinoa
parses and shows whether Selphie's page has anything worth reading. Rinoa's
Angelo learn-% gauges remain tracked as #50.

## v0.18.2.27

Status screen Limit Break page TTS (#49) — first feature build of the limit-break
chapter, replacing the local STATDIAG diagnostic with production announcements on
the per-character Status detail view's limit page (Status subsystem +0x1E8==5,
detail focus +0x22E==3, page index +0x257==3).

Toggles — the headline accessibility win, since these auto-mode / indicator
options are what let a blind player land Squall's Renzokuken and Zell's Duel
(which otherwise need unseeable visual timing). Detected from the GCW help text
(so no dependence on the per-character row cursor), with on/off read from the
savemap and re-announced whenever the bit flips:
- Gunblade Auto: savemap+0x0D1C & 0x01 (1 = on)
- Zell Duel-Auto: savemap+0x0D1C & 0x02 (set = on)
- Renzokuken Indicator: savemap+0x0D1D & 0x80 (0 = on, inverted)

Read-only limit-move names — the Renzokuken finishers / Duel moves / Blue Magic /
Angelo abilities are parsed out of the rendered GCW by longest-match against the
per-character name table (the GF Learn-list technique) and indexed by the row
cursor (band +0x25F..+0x264, using whichever byte moved; Squall steps by 2 with
two leading toggle rows, the others step by 1). New file menu_tts_status.inl,
textually included after menu_tts_ability.inl; the local [STATDIAG] savemap
watcher and its call site are removed.

To confirm on this BAT: toggle polarity (one-line flip if any reads backwards),
the exact on-screen move spellings (longest-match needs them byte-for-byte), and
the per-character cursor step / leading-toggle counts. Selphie's Slot page has no
readable content; Rinoa's Angelo learn-% gauges are tracked separately as #50.

## v0.18.2.26

LOCAL DIAGNOSTIC BUILD (issue #49, not for push) — widen the Status-screen
savemap watcher to the full savemap.

The v0.18.2.25 BAT confirmed the watcher fires correctly (init line logged on
Status entry), but toggling Gunblade Auto / Renzokuken Indicator on Squall's
limit page produced no [STATDIAG] change lines — even though the switch was
audibly applied. The state byte is therefore outside the 512-byte window
[+0x0A80..+0x0C80) that build watched (and outside SUBMON's 4 KB at pMenuStateA),
so the config-block guess was wrong; the byte may live lower, e.g. in Squall's
character record.

This build widens `PollStatusSavemapDiag` to diff the entire documented savemap
(+0x0000..+0x1400, 5120 bytes) while the Status subsystem is active, skipping
only the live play-time counter (+0xCCC, 4 bytes) so it doesn't spam. Same BAT:
open Squall's Status, page to the limit screen, flip Gunblade Auto and Renzokuken
Indicator back and forth a few times each; the byte that flips in lockstep is the
state field. If the full-savemap diff still catches nothing, the setting is a
runtime global outside the per-save savemap and we'll switch tactics (disassembly
or a wider RAM scan).

## v0.18.2.25

LOCAL DIAGNOSTIC BUILD (issue #49, not for push) — Status-screen savemap window
watcher to locate the persistent state byte for Squall's limit-page toggles.

The 12:25 BAT mapped Squall's Status page 4: two ON/OFF options (Gunblade Auto,
Renzokuken Indicator) plus the learned finisher rows (Rough Divide), with page
index at pMenuStateA+0x257 (limit page = 3) and the limit-page row cursor at
+0x25F (0 = Gunblade Auto, 2 = Renzokuken Indicator, 4 = Rough Divide). What's
still unknown is where the ON/OFF state is stored — GCW can't tell ON from OFF
(it emits both strings), and SUBMON only diffs 4 KB at pMenuStateA, never the
savemap, where a persistent setting must live.

This build adds `PollStatusSavemapDiag` in `menu_tts_diagnostics.inl`, called from
the menu poll whenever the Status subsystem is active (+0x1E8 == 5). It diffs a
512-byte savemap window [+0x0A80 .. +0x0C80) — config block, active party,
Griever, Gil, limit-break region, item battle-order, start of item inventory —
and logs every byte that changes as `[STATDIAG] savemap+0xNNN: x -> y`. The
window stops short of the play-time counter (+0xCCC) to avoid per-frame tick
noise. No F12, no key; resets its snapshot on leaving Status.

BAT: open Squall's Status, page to the limit screen, then toggle Gunblade Auto
OFF->ON->OFF and Renzokuken Indicator ON->OFF->ON; the byte that flips in lockstep
is the state field. Remove this watcher once the offset is pinned (#49).

## v0.18.2.24

FMV audio descriptions — incorporate the community review/edit pass (PR #26,
thanks to @djoleninja) for the disc 0 FMVs.

PR #26 rewrote and corrected the AI-generated descriptions for disc00_00h through
disc00_20h (21 clips): fixing mis-identified scenes (01h is the Quistis infirmary
intro, not Dr. Kadowaki; 06h is the X-ATM092 crushing a car, not a car driving)
and adding beat-by-beat detail to the longer sequences (the Dollet launch 03h, the
comm-tower activation 05h, the X-ATM092 chase 07h, the Timber train mission
10h/11h, and the Edea parade 17h).

The GitHub merge collided with this repo's own later AD revision of the same files
and committed Git conflict markers into five of them (01h, 03h, 07h, 10h, 14h)
while silently reverting the other 16 to the repo-side text. This commit resolves
that by taking the PR author's descriptions wholesale, per maintainer decision,
for all 21 files, with the conflict markers removed.

Small cleanups applied on top of the PR text:
- 07h: "Quill" -> "Quistis"; tidied "one of the extraction ship ," to "extraction
  ships,".
- 03h: a duplicate cue start (two cues at 00:48.000) would have made the second
  interrupt the first and drop a line; the second cue now starts at 00:51.000.
- 05h / 14h / 17h: repaired malformed timestamps (a stray space inside a
  timestamp, a double space before the arrow) that the lenient parser tolerated
  but were still wrong.
- 09h / 14h: removed the contributor's notes-to-maintainer left inside the NOTE
  header blocks (the parser skips NOTE blocks, so they never reached players).

Content-only change: the VTTs are embedded as RCDATA at build time, so this needs
a rebuild for the new descriptions to ship. No code changed.


## v0.18.2.23

Shortened the party-group cue wording to "Active Party" / "Reserve Party" (dropped
the trailing "Start"), which reads more naturally. No other change.


## v0.18.2.22

Active/reserve party grouping cue on the Junction, Magic, and Status character-
select screens.

Sighted players see the three active battle members' slots set apart from the
reserve slots; that distinction was lost for screen-reader players. The character-
select now announces a fieldset/legend-style cue — "Active Party Start" or
"Reserve Party Start" — the first time the cursor lands in a group and whenever it
crosses into the other group, prepended to the member readout in the same
utterance (a separate announcement would be interrupted away). Start cues only,
no end cues, to keep it terse. A character counts as active when it is in the
battle formation (`savemap+0xAF0`); the roster lists the active members first,
then reserves. Implemented in the shared `AnnounceJuncCharSelect` behind an opt-in
flag, so all three character-select screens get it while the main-menu Rearrange
panel (active members only) is unaffected; the group state resets on each
(re)entry so every visit re-cues the starting group.


## v0.18.2.21

Magic and Status main-menu commands now read the party member during their
character-select step.

Magic and Status (like Junction) have an intermediate "select party member" step
before their per-character screen; the other commands go straight in. That step
was silent. The v0.18.2.20 probe confirmed both reuse Junction's character-select
screen: Magic = subsystem `+0x1E8 == 3`, Status = `+0x1E8 == 5`, both with focus
`+0x22E == 0` and the cursor at `+0x1E9` indexing the roster at `+0x1DB` (the
cursor ranges over reserve members too, not just the three active leads). A small
poller in `MenuTTS::Update()` now announces the member under the cursor via the
shared `AnnounceJuncCharSelect` ("Name, Level N, HP X of Y", reserves and empty
slots handled) whenever Magic or Status is in its character-select phase. The
focus gate confines this to char-select; the per-character spell/status screen
has a different focus and is unaffected. No overlap with the main-menu party
panel (gated on `+0x1E8 == 0xFF`) or Junction (`+0x1E8 == 17`). The `[MagStatDiag]`
diagnostic from v0.18.2.20 is removed.



## v0.18.2.19

Main-menu "Rearrange party order" — party panel now reachable from every command.

The party-panel announce worked when approached from the Junction command but
was silent from Item (and other commands): the block was gated by the
per-submenu `!s_*Active` flags, and `s_itemSubmenuActive`/`s_gfActive` get set
just by hovering the Item/GF command in the list, blocking the party panel
from every command except Junction (whose `s_juncActive` is only set when the
junction subsystem is genuinely open).

The gate is now the bare-main-menu indicator `+0x1E8 == 0xFF` (any open submenu
changes it — junction = 17, ability = 14, etc.), which is the correct semantic
and independent of which command is highlighted. Panel detection still uses the
region flag `+0x1B6` (`0x0F` source-select / `0x10` destination-select); the
redundant `+0x1EC` check was dropped.


## v0.18.2.18

Main-menu "Rearrange party order" — command column re-announced on the way back.

Moving from the command column onto the party panel announced the member fine
(v0.18.2.16/.17), but moving back the other way — from the party panel to the
command column — was silent until an up/down press, because the top cursor
`+0x1E6` doesn't change while the cursor sits on the party panel, so
`PollMenuCursor` saw nothing to announce when you landed back on the same
command.

`MenuTTS::Update()` now detects leaving the party panel (party sub-mode
returning to 0) and resets `s_prevCursor` to `0xFF`, the same "announce on next
poll" sentinel used on menu-open, so the next `PollMenuCursor` re-announces the
command under the cursor.


## v0.18.2.17

Main-menu "Rearrange party order" — destination cursor now announced.

Reordering uses two cursors: a source-select cursor picks a member, then a
destination-select cursor picks the slot to swap it into. v0.18.2.16 announced
the source cursor but the destination cursor was silent. BAT showed both
sub-modes live on the party panel (`+0x1EC == 0x07`) and are told apart by
`+0x1B6`: source-select is `0x0F` with cursor `+0x1D6`, destination-select is
`0x10` with cursor `+0x1D7` (while `+0x1D6` stays locked on the source). Both
cursors are 0/1/2 over `roster[0..2]`.

`MenuTTS::Update()` now tracks the sub-mode and announces the member under
whichever cursor is active (reusing `AnnounceJuncCharSelect`), with a one-time
"Choose destination" spoken cue when entering destination-select so the mode
switch — invisible without sight — is audible.


## v0.18.2.16

Main-menu "Rearrange party order" — party members now announced.

From the bare main menu you can move the cursor onto the party panel (help bar
"Rearrange party order") to reorder the three active members; previously nothing
was announced as the cursor moved across them. The v0.18.2.15 `[PartyDiag]` BAT
identified the state: the slot cursor is `+0x1D6` (0/1/2 over the three active
members), and the cursor is on the party panel when `+0x1B6 == 0x0F` and
`+0x1EC == 0x07` (the command column has `+0x1B6 == 3`). The roster is live at
`+0x1DB` on this screen, so slot N is `roster[N]`.

`MenuTTS::Update()` now, when no submenu is active and the party-panel flags are
set, announces the member under the cursor on entry and on each slot change by
reusing `AnnounceJuncCharSelect` (roster-indexed; "Name, Level N, HP X of Y",
live HP since the three are battle members). The `[PartyDiag]` diagnostic has
been removed.


## v0.18.2.15

Main-menu "Rearrange party order" — diagnostic for the party cursor on the bare main menu.

From the main menu, before selecting any command (Junction/Item/etc.), the cursor
can be moved onto the party panel — the help bar reads "Rearrange party order" —
but nothing is announced as the cursor moves across the members. The v0.18.2.14 BAT
log showed `+0x1D6` cycling 0/1/2 (the slot cursor) while the top-menu cursor
`+0x1E6` stays put and `+0x1E8` is not 17, but `+0x1D6` is also 0 on the command
column, so a separate mode flag is needed to know when the cursor is on the party.

This adds an auto (no-key) `[PartyDiag]` log in `MenuTTS::Update()` (only when no
submenu is active) that logs, on a change of `+0x1D6`/`+0x1E6`/`+0x1E8`/`+0x1F1`, a
`+0x1D0..0x1FF` window plus reference bytes (`+0x01E`/`+0x020`/`+0x1B6`/`+0x1D3`) and
the battle formation, so a BAT (open menu, move onto the party, across members, off
again) reveals the party-slot cursor and the mode flag. Diagnostic only — removed
once the fix lands.


## v0.18.2.14

Junction character-select — reserve (available, non-party) characters now announced (Task 4).

The v0.18.2.13 `[JCharDiag]` BAT proved the char-select cursor (`+0x1E9`) indexes
the roster array at `pMenuStateA+0x1DB` directly: cursor 0/1/2/3 -> roster
`[1,0,5,3]` = Zell/Squall/Selphie/Quistis. Cursor 0-2 happen to equal
`formation[0-2]`, but the reserve (Quistis) is only in the roster — the battle
formation is `[1,0,5,FF]` — which is why the old code (formation-indexed, bailing
past slot 2) left her silent.

`AnnounceJuncCharSelect` and `GetJuncSelectedCharIdx` now source the character
from the roster (`+0x1DB`) and accept cursor 0-7, so reserves are announced like
the party. HP resolution: battle members still come from the computed-stats array
(live), reserves from the menu's per-character HP display array at
`pMenuStateA+0x71E` (the #47 benched-capable source), with the savemap struct as a
final fallback. Empty roster slots (e.g. cursor past the last character) say
"Empty". The `[JCharDiag]` diagnostic has been removed.


## v0.18.2.13

Junction character-select — diagnostic for reserve (available, non-party) character announcement (Task 4).

Selecting "Junction" then choosing which character to junction shows the current
party in three "STATUS" boxes (announced fine) and any reserve character (e.g.
Quistis) in a larger box below (silent). `AnnounceJuncCharSelect` maps the cursor
through the 3-slot battle formation (`savemap+0xAF0`) and bails past slot 2, and
the reserve isn't in that array at all — it only appears in the full roster array
at `pMenuStateA+0x1DB` (raw formation-then-reserve order, `[1,0,5,3]`).

BAT v0.18.2.12 showed moving onto Quistis produced no char-select line and did not
drive the party cursor `+0x1E9` to 3 — only render byte `+0x020` changed — so the
reserve selection is encoded in an as-yet-unknown offset. This build adds an auto
`[JCharDiag]` logger (no key) on the char-select that records, on any change, the
candidate cursor offsets (`+0x1E9`, `+0x01E`, `+0x020`, `+0x022`, `+0x1D3`) plus
the roster (`+0x1DB`) and formation (`+0xAF0`). BAT: enter Junction, move the
cursor Zell -> Squall -> Selphie -> Quistis -> back. The log will reveal which
offset tracks the reserve selection and how it maps to characters, feeding the
fix in the next build.


## v0.18.2.12

Item > Use target — benched-member max HP now announced (issue #47); diagnostic removed.

The v0.18.2.11 `[ItemDiag2]` diagnostic located the source: the menu keeps a
per-character HP display array at `pMenuStateA + 0x71E`, stride `0x20`, indexed by
character index, with curHP at +0 and maxHP at +2 — and it covers every available
character, including benched ones the 3-slot computed-stats array (`0x1CFF000`)
doesn't. BAT v0.18.2.11: Squall(0)=336/916, Zell(1)=64/585,
Quistis(3, benched)=861/861, Selphie(5)=385/482.

`GetCharacterHP` now reads max HP from that array, confirming the entry belongs to
the character by requiring its curHP field to match the savemap curHP before
trusting its maxHP. The old computed-stats / header path remains as a fallback.
Current HP still comes from the savemap (live on item use, per #10). Net effect:
benched members now announce "HP X of Y" like the battle party. The `[ItemDiag2]`
diagnostic has been removed.

BAT: menu > Item > Use target; move onto Quistis (benched) — expect "Quistis, HP
861 of 861" (full) rather than "HP 861"; confirm the battle members still read
correctly. If good, #47 closes.

## v0.18.2.11

Item > Use target — DIAGNOSTIC for benched-member max HP (issue #47).

After the #46 roster fix, benched (available but not-in-battle) members like Quistis
announce current HP but no max HP, because max HP for the listed members comes from
the computed-stats array at `0x1CFF000`, which only covers the 3 battle slots
(formation-indexed). This build adds an auto `[ItemDiag2]` (no key, Use-target only)
that logs, on each cursor move, for the selected character: savemap cur/max HP; the
first 8 computed-stat slots (to see whether the array is really span-3 or actually
holds entries for all available characters); and a bounded scan of `pMenuStateA`
for the character's current HP (to spot a menu display struct that pairs cur+max).
No behaviour change beyond logging.

BAT: menu > Item > Use target; move the cursor onto Quistis (the benched member)
and across the others, then back out. Read `Logs/ff8_menu.log` for `[ItemDiag2]`
lines — they reveal where the menu keeps max HP for benched characters, so the fix
can read it directly. #47 stays open.

## v0.18.2.10

Item > Use target — full-roster party list (issue #46); diagnostics removed.

The v0.18.2.9 diagnostic pinned the source. The Use-target list reads from an
0xFF-terminated roster array of char indices at `pMenuStateA +0x1DB` (BAT:
`[1,0,5,3,FF…]` = Zell, Squall, Selphie, Quistis) and the screen renders it sorted
by character index. The previous code read the 3-member battle formation at
`+0xAF0`, so a 4th roster member (Quistis) was missing — which also shifted slots
2 and 3 (the BAT "Selphie" at slot 3 was really Quistis, and the Unknown 4th was
really Selphie; a Potion errored on slot 3 because Quistis was at full HP).

`GetPartyCharAtVisualPos` now collects the roster from `+0x1DB` (stopping at 0xFF),
sorts by character index, and maps the cursor — yielding Squall, Zell, Quistis,
Selphie (0,1,3,5), matching the screen. It falls back to the battle formation only
if the roster array is unreadable (a safety net). NB: the Use screen lists every
*available* (joined) character, not the 3-member battle party — here that's 4
(Rinoa and Irvine haven't joined yet) — and the read grows as characters join.
The HP-after-use fix from v0.18.2.9 is retained (curHP from savemap, maxHP from
computed stats), confirmed working for Squall and Zell. All `[ItemDiag]`
diagnostics are removed.

BAT: menu > Item > Use target — expect Squall, Zell, Quistis, Selphie named
correctly across the four slots, a Potion accepted on a wounded one with the new
HP spoken, and no "Unknown". Closes #10 and #46 on confirm.

## v0.18.2.9

Item > Use target — HP-after-use FIX (issue #10) + roster diagnostic (issue #46).

The v0.18.2.8 diagnostic settled the HP question: after a Potion on Squall, the
savemap curHP read 536 (live, +200) while the computed-stats curHP stayed 336
(stale until the Item screen is rebuilt). `GetCharacterHP` was overriding curHP
with the stale computed value. Fix: in `GetCharacterHP`, keep **curHP from the
savemap** (it updates live on an in-menu item use) and take only **maxHP from the
computed-stats array** (FF8 derives max HP at runtime; the savemap char struct
stores 0 for it). This also makes the v0.18.2.7 live re-announce work — the
Use-target poll now sees the HP change and re-speaks it after each use.

Roster (#46) is now understood but not yet fixed here. The GCW render order proved
the Use screen lists the **full party roster sorted by char index** (Squall, Zell,
Quistis, Selphie = 0,1,3,5), not the 3-member battle formation we read from
`+0xAF0`. The roster set {0,1,3,5} was found in `pMenuStateA` (`+0x1DB`, `+0x6FA`).
To avoid regressing the common 3-member case, this build does not yet switch the
source; it widens the `[ItemDiag]` capture to dump the `+0x1DB`/`+0x6FA` array
windows (length/terminator) and log those arrays against the live cursor on each
move, so the roster fix can be implemented safely next.

BAT: menu > Item > Use a Potion on a wounded member and stay on them — expect the
new HP spoken right after the use (issue #10). Then move the cursor across all
four members and back out. `[ItemDiag]` lines (read from the log) will show the
`+0x1DB` array structure for the #46 fix. #10 closes on confirm; #46 stays open.

## v0.18.2.8

Item > Use target — DIAGNOSTIC for two BAT findings on v0.18.2.7 (issue #10 + Task 3).

The v0.18.2.7 HP-on-use fix did not work, and BAT surfaced a second, separate bug:

1. HP still stale after a Potion. The fix re-reads HP via `GetCharacterHP`, which
   prefers the computed-stats array (0x1CFF000); that array evidently is not
   updated by an in-menu item use, so there is no change to detect (it only
   re-syncs when the Item screen is re-entered).
2. A 4-member Use list (Squall, Zell, Selphie, Quistis) reads the 4th as Unknown.
   The list is built from the 3-member battle formation at +0xAF0, but the Use
   screen lists the full party roster, so the 4th member isn't in our source.

This build adds an auto-running `[ItemDiag]` (no key) active only on the Use-target
screen. On entry it scans pMenuStateA and the savemap for a 4-byte run that is a
permutation of {0,1,3,5} (the present roster char indices) to locate the real list
array and its on-screen order, and dumps the cursor/formation windows. Every 400 ms
it logs savemap vs computed-stats HP for Squall and the current target, so using a
Potion reveals which source updates live in the menu. No behaviour change beyond
logging; the v0.18.2.7 announce path stays (inert until the HP source is fixed).

BAT: menu > Item > Use a Potion on Squall; wait ~2 s; scroll across all four
members; back out. Then read `Logs/ff8_menu.log` for `[ItemDiag]` lines — the
roster run offset(s) and the HP source that changed after the Potion. #10 stays
open.

## v0.18.2.7

Item > Use target — live HP re-announce after using an item (issue #10, Task 3).

On the Use-target party list, the only thing that triggered a re-announce was the
cursor moving to a different character. Using an item keeps the cursor on the same
target, so even though the character's HP updates, nothing re-read or re-spoke it
until the player backed out of the target list and came back — the player heard
stale HP after every potion.

The Use-target poll now reads the selected character's current HP every frame and
re-announces (name + HP + any status) when it changes from the value last spoken,
in addition to the existing cursor-move announce. The change is gated to the same
character under the cursor (so it can't fire off a stale comparison), and the HP
baseline is captured on entry to the target list and reset whenever the item
submenu or a sub-flow resets, so a fresh selection never produces a phantom
announce. Fully automated — no key press needed; works for single or repeated uses.

BAT: menu > Item > Use a healing item (Potion etc.) > select a wounded party
member and confirm one or more uses without moving off them. Expect the new HP to
be spoken after each use (e.g. "Squall, HP 580 of 580"). Moving between members
still announces as before. Log: `[MenuTTS] Use target cursor N: charIdx=.. hp=X/Y
... (HP changed)` on each use. Closes #10 on confirm.

## v0.18.2.6

Junction Auto submenu — reliable "junctioned automatically" confirmation (Task 1).

The v0.18.2.5 BAT located the game's auto-junction routine (0x004BE790) via the
hardware write breakpoint and, with Aaron's confirm/cancel labelling, settled the
behaviour: the routine runs on a CONFIRM of an Auto option — even a no-op confirm
that changes no spells — and does NOT run on a cancel. The breakpoint stayed
correctly silent on all three cancels, so it's a clean confirm-vs-cancel signal,
not a stale one.

This promotes that breakpoint from a diagnostic into the live detector. The VEH
is now lean: when the write to the working junction byte fires while the Auto
submenu is focused (+0x22E==11), it sets a flag; the per-fire logging, register/
stack capture, and hit cap are removed. The Auto submenu's apply resolution now
reads that flag instead of the v0.18.2.3 magic-changed snapshot: routine ran =>
speak "Junctioned automatically for <Attack/Magic/Defense>"; routine did not run
=> it was a cancel, stay silent. The flag is cleared on each entry to the Auto
submenu so every confirm/cancel cycle is judged fresh. The snapshot (and its
+0x6C0 window) is gone.

Net effect: the confirmation now fires on every confirm, including confirming an
option that doesn't change the current junction — fixing the old "silent on a
no-op confirm, reads as broken" problem — while still staying silent on cancel.

BAT: Junction > a character > action menu > Auto, confirm an option (expect
"Junctioned automatically for ..."); re-open Auto and confirm an option that
changes nothing (expect the same announcement); open Auto and cancel out (expect
silence, then the action-menu item). Log: `[JuncTTS] AutoApplied: ... (confirm
detected)` on confirms, `[JuncTTS] AutoMenu cancelled (auto-junction routine did
not run)` on cancels.

## v0.18.2.5

Junction Auto submenu — DIAGNOSTIC build (find the auto-junction routine).

The v0.18.2.4 BAT settled the open question: across a full Auto session (opening
the submenu, arrowing the options, confirming Attack — a real change — confirming
Defense, plus a cancel) there were ZERO `[JuncBtnDiag]` lines, meaning the engine's
`pEngineInputConfirmedButtons` bitmask stayed 0 on every menu press. The menu
module reads input through a separate path, so the button-bitmask approach is out.

This build pivots to the routine itself, as requested. On a confirm the auto-
junction routine writes the menu's working junction array at pMenuStateA+0x6C2
(the v0.18.2.4 log caught 0x6C2 0->18, 0x6C4 32->0 on the Attack apply). Since
pMenuStateA is a fixed address, that write lands at a fixed location, so we set a
1-byte hardware WRITE breakpoint there (DR3; DR0/1/2 are the battle BPs) armed
while the Junction menu is open. When it fires — inside the auto-junction routine
— the VEH logs `[JuncAutoBP]` with EIP, registers, the value written, the focus
value, the code bytes around EIP, and the FF8-.text stack return addresses. The
return addresses give the routine entry to MinHook in the follow-up build. This
is the same hardware-breakpoint technique that pinned down the battle damage-popup
writer. The v0.18.2.4 button diagnostic is removed; the v0.18.2.3 snapshot announce
is left in place as interim until the routine hook replaces it.

BAT: open Junction > a character > action menu > Auto, confirm an option (e.g.
Attack), then re-open Auto and confirm again (ideally one that changes nothing,
to see whether the routine writes on a no-op), and try a Cancel. The `[JuncAutoBP]`
lines will show where the write came from. If no `[JuncAutoBP]` lines appear, the
BP didn't catch the writer (we'll widen to the +0x6C4 byte or revisit thread
arming).

## v0.18.2.4

Junction Auto submenu — DIAGNOSTIC build (no behavior change to the announce).

The v0.18.2.3 snapshot approach (announce only if junctioned magic changed) was
too fragile: it goes silent whenever the auto result is unchanged (cancel, or a
re-confirm of an already-optimal junction), which reads as broken. The right
signal is the CONFIRM action itself — the OK button press that triggers the auto-
junction — which happens on every confirm regardless of outcome.

The engine exposes an edge-triggered "buttons pressed this frame" bitmask
(`pEngineInputConfirmedButtons`, already resolved and used on the field). This
build logs that bitmask on every button edge while the Junction menu is open
(`[JuncBtnDiag]` lines in ff8_menu.log) to determine two unknowns: whether the
menu module updates that global at all, and which bit is OK (confirm) vs Cancel.
The existing announce logic is unchanged for this build.

BAT: open Junction > a character > action menu > Auto, then press OK on one
option, Cancel (X) on another, and a few arrow presses. The `[JuncBtnDiag]` lines
will show the confirmed-button bitmask for each, with the focus value. If the
bitmask stays 0x00000000 in the menu, the menu uses a separate input path and
we'll fall back to hooking the auto-junction routine via a write breakpoint on
the fixed junction working address (pMenuStateA+0x6C2).

## v0.18.2.3

Junction Auto submenu — fixes the two v0.18.2.2 BAT bugs (confirmation worked, but).

Bug 1 (something spoke just before the confirmation): the "force re-announce on
return to character select" reset was firing on the transient char-select hop and
un-muting the character name, so "Zell…" leaked out right before the confirmation.
The char-select hop is now fully muted while an Auto resolution is pending, and
that reset is gated off while pending, so only the confirmation is heard.

Bug 2 (cancel falsely announced "Junctioned…"): confirm and cancel leave the Auto
submenu by the IDENTICAL focus path (11 -> 8 -> 3), so the path can't tell them
apart — the v0.18.2.1 "confirm = goes through char select" theory was wrong. The
only reliable signal is whether the junction actually changed. The mod now
snapshots the live junction data (window at +0x6C0, covering the junctioned
magic-id array at +0x6C2/+0x6C4) on entering the Auto submenu and compares it when
the submenu closes: changed => speak "Junctioned automatically for <option>";
unchanged => stay silent. This makes a cancel silent AND a re-confirm of an
already-optimal auto silent (a true no-op), and only speaks on a real change.

If a real apply ever fails to announce, the snapshot window may be too small; if a
cancel ever announces, it may be catching a volatile byte — either way the window
(JUNC_AUTO_SNAP_OFF/LEN in `menu_tts_junction.inl`) is the knob to adjust.

## v0.18.2.2

Junction Auto submenu — spoken "applied" confirmation on confirm.

The v0.18.2.1 BAT (three confirms) showed the confirm/cancel discriminator: when
an Auto option is CONFIRMED the junction focus `+0x22E` goes 11 -> 8 (a transient
character-select hop) -> 3 (the action menu re-opens), and the junctioned-magic
panel updates; a CANCEL pops straight from 11 -> 3 with no char-select hop. So
prev-focus 11 landing on focus 0/8 means "applied". On that transition the mod
now sets a pending flag (capturing the chosen option) and mutes the transient
character re-announce; when the action menu settles (focus 3) it speaks
"Junctioned automatically for Attack/Magic/Defense" in place of that one action
re-announce, so the confirmation isn't cut off. Cancel (11 -> 3) sets nothing and
behaves exactly as before.

Pending caveat for next BAT: the confirm/cancel split is well-evidenced for
confirm (3/3) but the cancel path (X out of the Auto submenu) wasn't captured, so
verify a cancel does NOT speak the confirmation. Code in `menu_tts_junction.inl`.

## v0.18.2.1

Junction Auto submenu — option readout (Atk / Mag / Def).

Confirming "Auto" from the Junction action menu opens a three-option submenu
that auto-junctions a character's magic to optimize a stat. It previously read
nothing. SUBMON (BAT 2026-06-02, captured automatically as Aaron moved between
the options) showed the submenu settles at junction focus `+0x22E == 11` and
stays there throughout, with the option cursor at `+0x26A` (0/1/2) tracking the
GCW help line exactly ("Junction magic to up Str / Mag / HP"). A new `focus == 11`
branch in `PollJunctionSubmenu` (mirroring the action-menu branch) now announces
the terse option name on entry and on each cursor move ("Attack" / "Magic" /
"Defense"); the "/" key reads that option's help via a new `JunctionAutoSpeakHelp()`
spliced into the existing GF/Ability "/" fallthrough chain in `MenuTTS::Update()`. focus 11
was also added to the handled set so it stops logging as Unhandled, and the
per-phase cursor reset clears the Auto cursor on exit.

Still open for this submenu: the spoken "applied" confirmation when an option is
actually confirmed — the confirm transition wasn't captured this BAT (Aaron only
moved between options), so it's deferred to the next build once the post-confirm
state is logged. Code in `menu_tts_junction.inl` + the one-line `/` chain edit in `menu_tts.cpp`.

## v0.18.2.0

Refine quantity screen — added orienting context (0.18.2.x chapter open).

The "Use GF ability" refine quantity selector previously read out as a bare
number (e.g. "1, 20 Waters") with nothing telling the player what the screen was
for. On the first announce after the selector comes up it now prepends an
orienting phrase that also spells out the format — "Select quantity to refine. 1,
makes 20 Waters" — while subsequent count moves stay terse ("1, 20 Waters") so
scrolling the amount is still fast. "First entry" is detected with no new state
(`s_abilQtyLast < 0`, which the recipient picker already re-arms), so backing out
to the picker and re-entering quantity re-speaks the orientation. The log line
now records `first` and the computed `total`, and continues to record `owned`
(+0x2E4) so a later build can add the selectable maximum once it's confirmed
against the panel. `PollRefineQuantity` in `menu_tts_ability.inl`; no offset or
state changes elsewhere.

## v0.18.1.13

Ability screen (#42) refine flow — recipient magic stock now announced.

The v0.18.1.12 BAT confirmed the savemap character-magic layout and pinned the
result spell id: **Water = spell id 10** (Quistis's array held `{10, 60}` for her
panel value of 60, Selphie `{10, 40}`, etc.). The recipe pointer was just preview
text and there were no `0x0C` spell-insertion codes, so the result magic is mapped
from its name instead.

The character picker now speaks, e.g., "Squall" immediately and then "has 100
Waters" after the same 400 ms beat the refinable tag uses. The count is read from
the recipient's savemap magic array (`SAVEMAP_BASE + 0x048C + charId*152 + 0x10`,
32 x {spell_id, qty}) by scanning for the result spell id; the id is resolved from
the stashed result-magic name via a spell-name table (ids 1-40 — elemental, GF-
tier, healing, support — with Water confirmed in-game). A magic not in the table
falls back to the plain name (no count) rather than risk announcing a wrong
number; the status-magic ids (41+) get added once confirmed the same way.

Diagnostics retired (`RECIP_STOCK_DIAG` off). This build carries everything since
the recipient picker landed (Builds 2b/3/4 + all the polish fixes).

## v0.18.1.12

Ability screen (#42) refine flow — stock-locator retargeted at the savemap.

The v0.18.1.11 BAT (with the four character screenshots) confirmed the recipient
Water counts — Squall 100, Zell 60, Quistis 40, Selphie 20 — but the menu-struct
window was byte-identical across recipients, so the panel reads each character's
magic stock from the savemap, not the menu state. Per the submenu-layout research
(offsets corrected by -0x14 for the confirmed 0x4C header), the character structs
start at `SAVEMAP_BASE+0x048C`, 152 bytes each, indexed by character id, with
`Magics[32]` (32 x {spell_id, qty}) at struct `+0x10`.

This build retargets `RECIP_STOCK_DIAG` to dump, per recipient: (a) that
character's 64-byte magic array from the savemap (confirms the layout and reveals
the result spell id — the slot whose qty matches the known panel value), (b) the
engine refine recipe at `*(+0x2BE)`, and (c) the GCW `0x0C` spell-insertion codes
— the latter two as id sources so the next build can scan the array for the
result magic and announce "<name>, has N <Magic>" with the 400 ms beat.

## v0.18.1.11

Ability screen (#42) refine flow — fixes for the two v0.18.1.10 BAT findings.

1. **Double-announced recipient names fixed.** The picker-vs-quantity decision
   was leaning on the "Number to refine" text in the GCW, which flickers in and
   out frame-to-frame; that made the sub-phase flap between picker and quantity
   and re-speak names. Routing is now purely memory-based: `+0x2E9` (255 = item
   list, 0 = recipient flow) and `+0x2E7` (1 = quantity, 0 = character picker) —
   both frame-stable, so no flap. (`+0x2E4`/owned can't be used here: it lingers
   non-zero after backing out of quantity.)
2. **Broken "already has 100" reverted.** `+0x2E6` turned out to be a transient
   post-refine value (0 for every hovered recipient), so it never fired. Removed;
   the picker is back to a clean name announce.

New per-recipient design ("<name>, has N <Magic>" with the 400 ms beat) needs the
actual stock byte first, which isn't in `2E0..2EB`. A wider locator dump
(`RECIP_STOCK_DIAG`: `0x2C0..0x33F` on each recipient change, `[RECIPDIAG]` lines)
is in this build to find it; once located, the next build wires up the stock
follow-up and gates the dump off.

## v0.18.1.10

Ability screen (#42) — two refine-flow polish fixes on top of Builds 3/4.

1. **No more stray "Squall" on entering the item list.** The recipient/quantity
   routing is now gated on having actually browsed an item first
   (`s_abilItemLastCur >= 0`), so the one-frame character-picker blip at item-list
   entry is suppressed (you can only reach the picker after selecting an item).
2. **Maxed recipient announced.** When you land on a character who already holds
   the magic's cap, the picker says e.g. "Zell, already has 100" — read from
   `+0x2E6` (the recipient's current stock). The game's own "Already has 100" popup
   renders in a separate window the menu buffer can't see (hence its inconsistent
   capture), so this proactive read is the dependable signal.

The recipient handler temporarily logs `+0x2E6` plus a small byte window
(`2E0..2EB`) so the BAT can confirm the stock byte tracks the *hovered* recipient
(not only the last-refined one); that probe window gets gated off once verified.

## v0.18.1.9

Ability screen (#42) Builds 3 + 4 — the refine flow's last two steps now speak.
After you pick a refinable item, the **character picker** announces each recipient
as you move (Squall / Zell / Quistis / …), read from the FF8 character id at
`+0x2DE`. After picking a recipient, the **quantity selector** announces the
number to refine plus the running spell total as you change it — e.g. "5, 100
Waters" (count × per-item yield, taken from the refine preview). The diagnostic is
gated off (`REFINE_FLOW_DIAG` 0); this build is the shippable refine-flow milestone
(carries Build 2 item list, 2b refinable tag, 3 recipient, 4 quantity).

Mechanics: `+0x22E` stays 21 across all three sub-screens, so the sub-phase is
routed on markers instead — quantity = the unique "Number to refine" text (fallback
`+0x2E4` owned ≠ 0), character picker = `+0x2E9` == 0, item list = `+0x2E9` == 255.
Quantity counter = `+0x2E5` (1..`+0x2E4` max). New `AbilReadRefineSub`,
`ParseRefineYield`, `PollRefineCharPicker`, `PollRefineQuantity` in
`menu_tts_ability.inl`; the per-item yield is stashed from the clean preview
(item list / character picker) before the quantity popup muddies the text.

Known limits (verify next): total = count × out / in is exact for the common
1-input recipes (Fish Fin 1→20); multi-input recipes (X→Y, X>1) need a check.
Recipient names for renameable characters (Squall/Rinoa) use FF8 defaults until a
savemap-name read is added.

## v0.18.1.8

Ability screen (#42) — DIAGNOSTIC build (refine-flow sub-phase map). No behavior
change to shipped features (Build 2b still active). Adds `REFINE_FLOW_DIAG` and a
`RefineFlowDiag()` that logs the phase byte (`+0x22E`) plus the candidate
recipient / quantity cursor bytes (`+0x2DE`/`+0x2E0`, `+0x2DF`, `+0x2E5`/`+0x2E4`/
`+0x2E7`, `+0x2E1`/`+0x2E3`/`+0x2E9`) on any change, across the whole flow. One
pass (item list -> pick item -> character picker -> pick -> quantity selector)
maps every sub-phase's `+0x22E` value and confirms which byte is each live cursor,
so Builds 3 (recipient announce) and 4 (quantity announce) can gate correctly.
Not for push; gate `REFINE_FLOW_DIAG` to 0 once mapped.

## v0.18.1.7

Ability screen (#42) Build 2b — per-item "Refinable / Cannot be refined" tag, and
the refinable-flag diagnostics gated off. When the refine item list is open, each
item still announces "name, quantity" the instant the cursor lands; then, after a
short dwell (~400 ms) on a real item, the mod reads the engine's refine-result
pointer (`+0x2BE`) and speaks "Refinable" or "Cannot be refined" as a second clip
(queued, so it never cuts off the name). The brief pause also doubles as a
scan-vs-detail beat for the player.

Why a dwell: two rounds of full menu-struct dumps showed there is no synchronous
per-item refinable flag — `+0x2BE` is the only refinability signal and it
populates a few frames after the cursor lands (and clears when leaving a
refinable item, so a settled non-zero read means "refinable" with no false
positives). The rendered preview is unreliable per-move (stale text bleeds across
items), so it stays limited to the dwelling `/` reader.

- `ABIL_DIAG` flipped to 0; `AbilDumpMenuWindow` wrapped under it (diagnostic
  only, retained, gated). New `AbilReadRefinePtr` + settle logic in
  `PollAbilityItemList`. The `[MenuTTS] Refine status` log line is always on for
  verification.
- Carries the previously-confirmed Build 2 (item name+qty + `/` preview). Empty
  slots get no status.

## v0.18.1.6

Ability screen (#42) Build 2b — DIAGNOSTIC build #2 (refinable-flag hunt, widened).
No behavior change. The v0.18.1.5 window (`+0x200..+0x2FF`) analysis showed NO
per-item refinable flag there — the refine pointer at `+0x2BE` is set lazily
(reads 0 at cursor-move time for some refinable items) and there's no static
per-row array/bitmask in that range. This build widens `AbilDumpMenuWindow` to
dump the FULL menu struct `+0x000..+0x3FF` (two 512-byte `[ABILDIAG-WIN]` lines)
on each item-cursor move, to rule a persisted flag in or out anywhere in the
struct (static per-row array or per-cursor byte).

- To capture: open I Mag-RF item list, arrow slowly through every item.
- If no flag turns up here either, fall back to baking FF8's refine-recipe data
  (ability id -> refinable source-item set), which the previews already reveal.
- `ABIL_DIAG` stays 1. Not for push.

## v0.18.1.5

Ability screen (#42) Build 2b — DIAGNOSTIC build (refinable-flag discovery). No
behavior change; adds logging only. The rendered refine preview is too laggy to
tag each item "Refinable / Cannot be refined" reliably as the cursor moves, so
this build hunts for the engine's own per-item refinable flag in the dynamic
menu-state struct (the refine display list assembles there at `~+0x296`).

- `AbilDumpMenuWindow` hex-dumps `pMenuState +0x200..+0x2FF` as `[ABILDIAG-WIN]`,
  twice per item (the instant the cursor lands, then ~80 ms later) so a flag can
  be located and its timing checked (synchronous vs lazily computed).
- To capture: open a refine ability's item list (e.g. I Mag-RF), arrow slowly
  through every item. Diff the window across items of known refinability
  (M-Stone Piece / Magic Stone refinable; Potion / Tent / Cottage / G-Returner
  not) to find the flag, then implement the tag from it.
- `ABIL_DIAG` stays 1. Not for push.

## v0.18.1.4

Ability screen (#42) Build 2 — refine ITEM-LIST phase (first pass). When a refine
ability is selected and the source-item list opens (`+0x22E >= 19`), the
highlighted item now reads on cursor (`+0x2DF`) move, and `/` reads the refine
preview.

- Item name + quantity announce on move, read from the savemap inventory
  (`AbilReadInvSlot`, same source as the Item submenu). Empty/over-range slots
  say "Empty".
- `/` reads the refine preview ("N will refine into M <Magic>"), parsed from the
  GCW (`ParseRefinePreview`); says "No refine information" when the highlighted
  item can't be refined.
- `PollAbilitySubmenu` now branches on `+0x22E`: ability list (==3) vs item list
  (>=19); the item handler is `PollAbilityItemList`.
- `ABIL_DIAG` back to 1 for this build: `[ABILDIAG-ITEM]` logs cursor + savemap
  id/qty/name + the GCW so the BAT can confirm the item-list-to-inventory
  mapping (the working assumption is that the list is the full inventory in
  order). Flips to 0 once confirmed.
- Deferred to Build 2b: greyed/non-refinable state detection; pagination for long
  item lists.

## v0.18.1.3

Ability screen (#42) Build 1 — ability-list phase COMPLETE; diagnostics gated off.
Confirmed in-game: highlighted ability name on cursor move, `/` reads the help
description, and empty padded slots announce "Empty Ability Slot" (with `/`
repeating it). `ABIL_DIAG` flipped to 0 (kept in place, never deleted). No
behavior change from v0.18.1.2 — this is the clean, log-quiet build.

Next: Build 2 — the refine item-list phase (`+0x22E ~19–21`, cursor `+0x2DF`):
item name + quantity + greyed/eligible state on move, `/` for refine info.

## v0.18.1.2

Ability screen (#42) Build 1 — empty-slot handling. The ability list pads with
focusable blank rows below the real abilities (BAT: cursor `+0x258` ran 0–10 with
count=2, help="" on every empty row). `PollAbilitySubmenu` now classifies the
focused row: cursor `< count` is a real ability; `count..63` is an empty slot.

- An empty slot announces "Empty Ability Slot" on focus (deduped per row, so each
  slot speaks once as it's entered), and the `/` key repeats "Empty Ability
  Slot" while one is focused (its stashed help is blank, so the on-demand reader
  falls back to the slot name).
- This completes Build 1 (ability-list phase): name on move, `/` for help, empty
  slots. `ABIL_DIAG` stays 1 for this confirmation BAT; flips to 0 next.

## v0.18.1.1

Ability screen (#42) Build 1 fix — ability-list parse. The v0.18.1.0 BAT showed
the dispatch, gate (`+0x1E8==14`), phase (`+0x22E==3`), and cursor (`+0x258`) all
working, but `[ABILDIAG]` logged `count=0` on every poll: the parser anchored on
`rfind("Junction")`, and on this screen the menu bar scrolls so the GCW renders
"GFAbilitySwitchCardConfigTutorialSave..." — Junction/Item/Magic/Status scrolled
off — so the anchor was never found.

- `ParseAbilityList` no longer uses any menu-token anchor. It forward-scans the
  decoded GCW for the longest contiguous run of menu-ability names (ids 97–115),
  which appear only in the list, and keeps the rightmost-longest run. Help text
  is still sliced from the preceding "Save".
- `ABIL_DIAG` stays 1 for this BAT to confirm the parse now yields the list
  (expect `count=2 ids=[98,108]`); flip to 0 before push.

## v0.18.1.0

Main-menu Ability screen TTS (#42), Build 1 — the ability list. The Ability
screen (top-level cursor 5) is the "Use GF ability" action screen (the *-RF
refine family etc.), confirmed by the SUBMON discovery pass + 3 F11 shots; it is
NOT a GF/AP/learn screen (that's the GF screen, #41). New `menu_tts_ability.inl`
(textual include from `menu_tts.cpp`, after `menu_tts_gf.inl`) plus a
`PollAbilitySubmenu()` dispatch gated on top-level cursor 5.

- Gate `pMenuStateA+0x1E8 == 14`; ability-list phase `+0x22E == 3`; list cursor
  `+0x258` (with `+0x257` read as a pagination fallback). Offsets confirmed in
  the 2026-06-01 isolated SUBMON pass.
- On a cursor move the highlighted ability NAME is announced (name only, per
  request). The ability list is read from the rendered GCW (right-to-left
  longest-match against the menu-ability id block 97–115, anchored on the next
  "Junction" menu cycle), reusing the GF module's `GcwAbilityNames()`.
- The `/` key reads that ability's help description on demand
  (`AbilitySpeakSelectedHelp()`, chained after the GF learn-list `/` handler);
  it returns false off this screen so the normal help bar still works elsewhere.
- `ABIL_DIAG` logging is ON for this build to confirm the GCW parse against the
  BAT (logs phase / +0x257 / +0x258 / parsed ids / help); flip to 0 before push.
- Build 2 will add the refine item-list phase (`+0x22E ~19–21`, cursor `+0x2DF`).

## v0.18.0.15

GF learn-list readout polish (#44):
- Cursor-move readout no longer reads the help description — just the ability
  name + AP (cuts repeats). Learned abilities read "&lt;name&gt;, Learned".
- AP now always uses one format, "C out of R AP", including at 0 ("0 out of 60
  AP") for consistency across learned-progress and untouched abilities.
- The `/` key now reads ONLY the help description (no name repeat); falls back
  to the row name / "Empty Ability Slot" when there's no description.

## v0.18.0.14

**AP readout (#44) implemented.** The v0.18.0.13 probe confirmed the on-screen AP
numbers are sprite-drawn (absent from the GCW text), so AP is now computed from
a baked table instead of scraped:
- `ability_ap_cost[116]` — required AP per unified ability id (per-ability
  constant), and `gf_ability_slots[16][22]` — each GF's slot order, used only to
  map an ability id to its savemap `APs[+0x24]` slot. From the Hyne-sourced deep
  research (anchor-validated), cross-checked against live BAT data: Quezacotl
  learning SumMag+30% (id 85) -> slot 2 -> `APs[2]=117` toward cost 140.
- **Learn-list rows** now carry AP: learning -> "now learning, C of R AP";
  learned -> "learned"; otherwise the cost to learn -> "R AP".
- **Detail key 5** now reads "Learning <name>, C of R AP" (current of required).
- `GF_AP_DIAG` gated to 0 (its question is answered; probe retained).
- Known caveat: Auto-Haste (id 73) cost is uncertain (Hyne 250 vs FF Wiki 150);
  Cerberus-only, flagged in-code for in-game verification.

The display list is sorted by ability id and is a subset of the 22 slots, but
all AP lookups key off the ability id (cost) or map id->slot (current), so the
on-screen order is irrelevant.

## v0.18.0.13

GF screen (#41) enhancement + AP-readout probe (#44). Holding the push until the
whole GF submenu is finished; this is an iterative BAT build.
- **`/` key re-reads the selected ability's help (#41 enhancement).** On the
  ability-to-learn list, `/` now speaks the help/description of the ability
  currently under the cursor ("<name>. <help text>"), distinct from detail key 5
  (which reads the ability being *learned*). Implemented as a single override at
  the existing `/` dispatch in `MenuTTS::Update`: `GFSpeakSelectedAbilityHelp()`
  returns true and speaks when the learn list is up, otherwise the normal
  help-bar reader runs unchanged. The selected row's name + description are
  captured as the cursor moves (mirrors what was just announced); empty slots
  read "Empty Ability Slot".
- **AP-readout feasibility probe `GF_AP_DIAG` (#44).** New diagnostic toggle
  (independent of the broad `GF_DIAG`, which stays 0). On each learn-list row
  move it logs the parsed ability list, the displayed GF's savemap AP array
  (`APs[24]` @ +0x24) with the raw learning bytes (+0x40 / +0x41), the raw GCW
  hex, and the full decoded GCW. Purpose: settle whether the on-screen AP
  numbers render as text (scrapeable from the GCW) or as sprites (absent ->
  use the kernel / deep-research AP-cost table). Gated off once decided.

## v0.18.0.12

**Ship #41 — main-menu GF screen TTS.** The empty-slot announce (v0.18.0.11)
is BAT-confirmed (Shiva: 5 real rows speak with learned/now-learning status,
the 6 empty rows below say "Empty Ability Slot"). The full GF screen is now
complete and verified: GF-list name announce, detail-panel keys 1..7, entry
hint, Q/R displayed-GF cycling, and the paginated ability-to-learn list with
its per-page cursor (`+0x257` page 1 / `+0x258` page 2), learned/now-learning
status, descriptions, and empty slots.
- `GF_DIAG` flipped to **0** — the whole `[GFDIAG]` / `[GFLEARN]` harness
  (savemap dump, byte-band poll, GCW logging, window dump) compiles out. The
  dispatch scaffold (`PollGFSubmenu` / `ResetGFSubmenuState`) and all
  production TTS stay. Per convention the diagnostic code is gated off, not
  deleted.
- No functional change to the TTS itself vs v0.18.0.11; this is the clean
  diagnostics-off build for release.

## v0.18.0.11

GF Learn list (#41): **announce empty ability slots.** The v0.18.0.10 BAT
confirmed the dual-byte cursor works across both pages (Quezacotl page 1 = 11
rows via `+0x257`, page 2 = 4 rows via `+0x258`, all names/status/descriptions
correct, and "now learning" verified against `rec[+0x40]`). The remaining gap:
the list area pads each page to a fixed height, so a short final page leaves
empty rows below the abilities (Quez page 2 has 4 abilities but the cursor ran
0..10 over 7 empty rows), and the cursor can land on them — those rows were
silent, which felt broken.
- Empty rows are now detected (active cursor in `[count, 22)` with blank help —
  empty rows render no name and clear the help window) and announced as
  "Empty Ability Slot", de-duped per row so each empty speaks once as you pass
  over it.
- `GF_DIAG` / `[GFLEARN]` window log still on for this pass.

## v0.18.0.10

GF Learn list (#41): **handle pagination + the per-page cursor byte.** The
v0.18.0.9 BAT showed page-2 rows announcing perfectly (names, learned status,
descriptions, and the "Boost GF" space-reject all correct) but page-1 rows
silent. The log explained both:
- The ability-to-learn list is **paginated and NOT filtered** — Shiva page 1 is
  11 rows (Str-J, Vit-J, Spr-J, Magic, GF, Draw, Item, Doom, Spr+20%,
  SumMag+10%, SumMag+20%), page 2 is 5 rows (SumMag+30%, GFHP+10%, GFHP+20%,
  Boost, I Mag-RF). (The earlier Siren "8 rows" was a single page.) The parser
  already reads whichever page is rendered correctly.
- The highlight cursor lives in a **different byte per page**: page 1 (top)
  tracked `+0x257`, page 2 (scrolled) tracked `+0x258`, both 0-based into the
  rendered page. v0.18.0.9 only read `+0x258`, so page 1 was silent.
- Now reads BOTH bytes and announces off whichever just changed and is in range
  (page 1 -> +0x257, page 2 -> +0x258), de-duping on the resolved ability id.
- Added a `[GFLEARN]` window log (16 bytes at `+0x250` + parsed page rows +
  help text, on any change) to confirm the per-page cursor model in one pass and
  check it generalizes to 3+ page GFs (e.g. Eden). `GF_DIAG` stays on.

## v0.18.0.9

GF Learn list (#41) **v1**: the ability-to-learn screen is now navigable. On the
Learn list (which keeps the detail panel up, so `s_gfDetailActive` stays true),
arrowing through the rows now speaks the highlighted ability as
"<name>, <status>. <description>" — e.g. "SumMag+10%, learned. Raises SumMag
damage by 10%" or "Boost, now learning. Boost GF".
- Row cursor = `pMenuStateA + 0x258` (0-based index into the displayed list;
  confirmed in the v0.18.0.8 BAT, where it tracked the help text 1:1 across
  Siren's 8 rows).
- The displayed list is a filtered subset of the GF's kernel slots, so rather
  than reconstruct it and replicate the engine's filter, v1 reads the real list
  straight out of the rendered GCW: the row names sit between the help text and
  the `<GFName>LV` stat-panel header. `ParseLearnList` walks that run
  right-to-left, longest-match against the GCW-form ability names, and stops at
  the first space-preceded candidate — list items concatenate with no space
  while help words are space-separated, so this cleanly cuts the list off from
  the help text even when the help ends in an ability word (Boost's "Boost GF").
- Status (learned / now learning) is read from the savemap: `completeAbilities`
  bit (+0x14) and the learning-ability id (+0x40). The description is the GCW
  help text (best-effort slice).
- **Not yet:** AP progress ("X of Y AP") per row — needs the per-GF kernel
  ability table (#44), shared with detail key 5 and the #42 Ability screen.
  Lands in v2.
- GF_DIAG still on; logs the parsed row list once per Learn-list entry for
  verification.

## v0.18.0.8

GF detail screen (#41): **finish the Q/R fix** — number keys now follow the
displayed GF. v0.18.0.7 wired the displayed-index read (`s_gfDetailIdx`) into
the wrong function: the edit matched the identical `gfIdx = ms[0x253]` line in
the diagnostic `GFDiagProbeDetail` instead of the production `SpeakGFDetailField`.
Result: the Q/R name announce worked, but keys 1–7 still read the GF the player
entered on (same level/HP/etc. regardless of Q/R). Now `SpeakGFDetailField`
reads `s_gfDetailIdx`, so all fields update with the displayed GF; the
diagnostic probe is back to its independent grid-cursor read.

## v0.18.0.7

GF detail screen (#41): **fix Q/R GF switching** (v0.18.0.6 item 3 regressed).
BAT proved Q/R cycle the displayed GF but do NOT move the grid cursor `+0x253`,
and the displayed-GF index is nowhere in the polled 0x1C0..0x2C0 band — so both
the v0.18.0.6 auto-announce AND the number keys were reading the GF the player
*entered* on, not the one on screen.
- The displayed GF is now resolved from the GCW panel header, which always reads
  `<GFName>LVHP/Compatibility...` and does follow Q/R. `MatchDetailGFIndex`
  matches each obtained GF's savemap name against the GCW and takes the
  right-most (freshest) hit.
- That resolved index now drives **both** the Q/R auto-announce **and** number
  keys 1–7, so the keys report the GF actually on screen. Announce is primed on
  entry (no re-speak of the entry GF) and de-duped on index change.
- Removed the `+0x25E` rendered-name approach from v0.18.0.6 (it didn't track
  the switch).

## v0.18.0.6

GF detail screen (#41): three polish items.
- **Entry hint:** when the detail panel first appears, speaks "Press numbers 1
  through 7 for details" once (Scan-screen model), so the player knows the
  number keys are available. 2 s cooldown guards against GCW flicker;
  interrupt=false so it never clips the GF name.
- **Keys 6/7** now prefix the readout with "Compatibility:" so the numbers have
  context (e.g. "Compatibility: Squall 503, Zell 515, Quistis 782").
- **Q/R GF change:** Q and R cycle the displayed GF on the detail panel; the new
  GF's name now auto-announces. Detected from the rendered name the engine
  writes to `pMenuStateA+0x25E` (same +0x20 encoding as the savemap name) —
  independent of whichever cursor index Q/R drives — primed on entry so it
  doesn't re-speak the GF you came in on. On the detail panel this replaces the
  grid's index-based announce so the two never double-fire.

## v0.18.0.5

GF detail screen (#41): **number keys remapped to 1..7.** Level moved off the
EXP key onto its own key, paired with a new "equipped by" readout; EXP key now
leads with the more useful EXP-to-next value.
- **3** = Level + who currently has the GF junctioned (e.g. "Level 9, equipped
  by Squall", or "Level 9, not equipped"). "Equipped by" reads each existing
  character's junctioned-GF bitmask (u16 @ char +0x58, bit = canonical GF id;
  FFNx `savemap_ff8_character.gfs`). Uncalibrated GF omits the level.
- **4** = EXP, now "EXP to next level X, Current EXP Y" (to-next first).
- **5** = Currently learning ability (was 4).
- **6 / 7** = Compatibility first three / next three (were 5 / 6).

## v0.18.0.4

GF detail screen (#41): **key 3 EXP phrasing fix** for screen-reader clarity.
Was "Level 9, Current EXP 4000, 500 to next level" — the two numbers ran
together with no label between them. Now "Level 9, Current EXP 4000, EXP to next
level 500", so each value is preceded by its own label. No logic change.

## v0.18.0.3

GF detail screen (#41): **keys 2 and 3 completed** — max HP, level, and EXP-to-
next now announced. The v0.18.0.2 wide search proved these aren't in
`pMenuStateA` (0 hits across all GFs), so they're solved instead from data +
formula, confirmed against five GFs' detail screenshots:
- **Max HP**: the stored GF `HPs` u16 (+0x12) equals the displayed max HP
  (730/2904/1593/1948/1421, each cur==max while rested). Key 2 now says
  "HP X of X".
- **Level / EXP-to-next**: FF8 levels GFs at a FLAT per-level EXP cost —
  level = exp/cost + 1, next = cost - (exp % cost). Verified exactly: Quezacotl
  L23@11185, Shiva L40@19958, Ifrit L24@11930, Diablos L9@4000 (cost 500);
  **Siren L26@10192 (cost 400)** — proof the cost is per-GF, and that a flat
  500-for-all would have misreported Siren as L21. Key 3 now says
  "Level N, Current EXP X, Y to next level".

`GF_EXP_PER_LEVEL[16]` holds only EMPIRICALLY CONFIRMED per-GF costs (the five
above); every other GF is 0 = "not yet calibrated", and an uncalibrated GF
announces EXP only — never a guessed level a blind player can't verify. Each
GF's cost is filled in from a detail screenshot as it's obtained (Eden is
documented as 1000 but left uncalibrated until confirmed in-game). The spent
Diablos-only wide-search probe was removed (it proved its negative); the rest
of the `GF_DIAG` record/compat harness stays for the upcoming Learn-list work.

## v0.18.0.2

GF detail screen (#41): **discovery probe for the three engine-computed values**
(GF level, max HP, EXP-to-next) that keys 2/3 don't yet announce. These aren't
in the savemap and aren't in `pMenuStateA+0..0x800`. FF8 levels GFs at a flat
per-level EXP cost (most 500, some 400, Eden 1000) — verified: Diablos 4000 EXP
÷ 500 + 1 = level 9, exact-multiple → "Next LEVEL" 500, matching the screen — but
the exact 400-EXP GF list isn't reliably documented, so rather than risk a wrong
level a blind player can't catch, this build locates the engine's already-
computed values directly. The `GFDiagProbeDetail` harness gains a Diablos-only
(gfIdx==5) wide sweep of `pMenuStateA 0..0x2000` logging every u16 == 730 or
500 with 16 bytes of context, to find the menu's GF display struct (500 is the
unique anchor). No production behaviour change; `GF_DIAG` stays 1. Next build
wires keys 2/3 (and the max-HP half of the HP line) to the found offsets, then
gates `GF_DIAG` off to ship #41.

## v0.18.0.1

GF main-menu screen TTS (#41): **GF list name announce + GF detail-screen
number keys**. First user-facing GF build of the chapter (the v0.18.0 entry
below was the discovery-only harness). Versioning going forward: 0.18.0.x = GF
submenu, 0.18.1.x = Ability submenu, 0.18.3+ = other menus.

GF list (the portrait grid): the highlighted GF's name is spoken on cursor
move, index-driven from `pMenuStateA + 0x253` (canonical GF cell 0..15), gated
on `+0x1E8 == 4` (GF subsystem active). Names come straight from the savemap GF
record; un-obtained cells say "Empty" (de-duped across a run). No help-text
scraping, no timed keypress — solo-testable.

GF detail screen (after selecting a GF): Scan-style number keys, each reading
one field on demand, gated to the detail/Learn panel so they can't fire on the
grid or elsewhere:
- 1 = name
- 2 = HP (current)
- 3 = current EXP
- 4 = currently-learning ability
- 5 = compatibility, first three existing characters
- 6 = compatibility, next three existing characters

All values read directly from the savemap (deterministic). The GF record layout
was confirmed against the Diablos detail screen: name `+0x00`, current EXP u32
`+0x0C` (4000), obtained flag `+0x11`, current HP u16 `+0x12` (730), learning
ability id `+0x40`. Compatibility lives in each character's struct
(`gf_compat[16]` at char `+0x70`, indexed by GF id); the menu display value is
`(6000 - raw) / 5`, confirmed exactly against Squall 648 / Zell 573 /
Quistis 600 / Selphie 606. Characters iterate in model order, existing-only, so
the roster grows from four to six as Rinoa/Irvine are recruited.

Deliberately deferred (engine-COMPUTED, not in the savemap, so a wrong value
can't be visually verified by a blind player): GF level, max HP, EXP-to-next
("Next LEVEL"), and the static AP-required for the learning ability. These need
their live computed-display location found and are the one remaining discovery
item for the detail screen; keys 2/3/4 announce their savemap-backed portion
until then.

Implementation: `src/menu_tts_gf.inl` gains `UpdateGFDetailPhase()` (GCW
"Compatibility" label -> `s_gfDetailActive`, throttled) and `SpeakGFDetailField()`
(one `__try` frame, char[]/sprintf/Speak only per the C2712 rule). The `GF_DIAG`
harness stays on one more build as a safety net; flip to 0 to ship #41.

## v0.18.0

Chapter opener + first GF build: **Main-menu GF screen discovery diagnostic**
(#41). Track A (auto-drive) closed at v0.17.9.17; this starts the GF + Ability
main-menu TTS chapter.

This build adds the GF dispatch scaffold and a discovery-only harness; there is
no spoken GF TTS yet. New `src/menu_tts_gf.inl` (textual `#include` from
`menu_tts.cpp`, after `menu_tts_hotkeys.inl`), with `PollGFSubmenu()` /
`ResetGFSubmenuState()` dispatched from the `isMenuMode` block on top-level
cursor index 4 (mirrors the Junction dispatch; suppressed while the item
submenu is active; reset on menu open and on leaving GF).

While the menu cursor sits on GF (mode 6, index 4) the `[GFDIAG]` harness, gated
behind `#define GF_DIAG 1`:
- dumps the savemap GF block once per screen entry (16 records x 0x44 at
  savemap+0x4C) as raw bytes + decoded name + a uint32 EXP candidate at +0x0C,
  to correlate on-screen GF order with savemap records;
- polls a narrow pMenuStateA band (0x1C0..0x2C0, where the Item/Junction
  cursors were found) every ~150 ms and logs byte changes, surfacing the GF
  list cursor as the player navigates;
- logs the rendered GCW menu text on change.
No on-screen-timed keypress is needed (solo-testable); the generic SUBMON 4KB
monitor also runs here as an independent cross-check. SEH/C2712-safe: the
__try band poller and the std::string GCW capture are isolated into separate
functions per the existing menu-TTS pattern.

Next build will read `ff8_menu.log` `[GFDIAG]` output to fix the GF list cursor
offset and the GF record field layout, then begin the production announce path
(GF name + level + junctioned-to, then per-GF detail). Flip `GF_DIAG` to 0 to
compile the harness out; the dispatch scaffold stays.

## v0.17.9.17

Track A complete — diagnostics gated off for push. This is the push build for
all three Track A auto-drive fixes (Steps 1+2+3), which are each BAT-passed:

- **Step 1 (v0.17.9.14):** `FindPortal` / `GetSharedEdgeLength` /
  `EdgeMidpointPath` read the walkmesh neighbour edge as the (edge, edge+1)
  vertex pair the FF8 .id format actually stores (was the wrong (edge+1,
  edge+2) pair, which emitted wall edges as funnel portals and wedged
  auto-drive on narrow/rounded fields). Full Dollet chase = 0 catches.
- **Step 2 (v0.17.9.15–.16):** F9 path-finding auto-drive uses a LOCAL bounded
  `EdgeCrossesScreenBound` test in the A* screen-bound avoidance instead of the
  global infinite-line `IsSeparatedByTriggerLine`, so a screen-bound line near
  the spawn no longer fences off the far side of a field (Balamb Hotel
  bcsaka_1). Gated on `s_chaseDriveActive` so the chase keeps the byte-identical
  global test (0 catches preserved).
- **Step 3 (v0.17.9.16.2):** bggate_6 front-gate turnstile slot selection via
  the new `ComputeAStarPathVia` two-segment A*; the F9 drive forces the correct
  one-way lane's mid-band triangle when the route crosses the turnstile.
  F9-only, bggate_6-only.

Diagnostics gated off (no behaviour change, all compiled out of the shipped
DLL): `FEPIC1_GATE_DIAG` set to 0 (removes the [GATEDIAG] dump and the
[TTRACE] turnstile tracer); the per-field [LINEDIAG] captured-trigger-line
dump moved behind a new `LINEDIAG_ENABLED` toggle (set to 0). Both are a
one-line flip to re-enable for future field-trigger / exit-label diagnosis.

## v0.17.9.16.2

Track A Step 3 — bggate_6 front-gate TURNSTILE auto-drive fix (LOCAL; still
carries the `FEPIC1_GATE_DIAG`/`[TTRACE]`/`[LINEDIAG]` diagnostics, strip before
push).

The front gate is a closed walkmesh loop with two offset one-way lanes — WEST
lane (X≈-1312) = IN, up to the B-Garden Hall; EAST lane (X≈-1093) = OUT, down
the gate path — and the turnstile collision separating them is not in the
walkmesh. Plain A* therefore picked the geometrically shorter lane, which is
the wrong (one-way, blocked) lane, and the party wedged. The `[TTRACE]` manual
walk confirmed the lanes: IN = straight up X=-1312; OUT = right, then down
X=-1093, each crossing both interaction-line "bars" (Y=-428 'squalls',
Y=-907 'squall').

Fix: a new `ComputeAStarPathVia(start, via, goal, ...)` runs A* in two segments
and stitches the triangle corridors so the funnel threads a forced "via"
triangle. The F9 drive setup (`field_nav_handlekeys.inl`) detects bggate_6
(field 0x00A3) and, when the route crosses the turnstile band (midline
Y=-667 between the two bars), forces the via triangle in the correct lane's
mid-band chosen by travel direction: goal north of start -> WEST lane
(via ≈(-1312,-532)); goal south -> EAST lane (via ≈(-1093,-586)). On a via-path
failure it falls back to plain A*. Strictly F9-only and bggate_6-only — every
other field, the chase, and all non-crossing drives are byte-identical
(`viaTri` stays -1 -> the original `ComputeAStarPath` call).

## v0.17.9.16.1

LOCAL DIAGNOSTIC build (NOT for push). Adds a `[TTRACE]` turnstile path tracer
for Track A Step 3 (`field_nav_diagnostics.inl`, gated under the existing
`FEPIC1_GATE_DIAG` flag, called per-tick from the gate-diag block in
`Update()`). Behaviour-neutral -- logging only.

Purpose: capture the player's MANUAL walk-through path at the `bggate_6`
front-gate turnstiles so the auto-drive thread-the-needle fix can be built from
real coordinates. Step 3 is diagnosed as a turnstile collision (`ent22
'doorbig'`): A* finds a path and the exemption resolves correctly, but the
party jams at wide walkmesh portals (13->148 going out, 157->156 going in)
because the turnstile's collision isn't in the walkmesh. The two Interactive
Lines ('squall' = line[0], 'squalls' = line[1]) are positioned exactly where
the manual maneuvers aim (line[0] is right-then-down from the exit jam; line[1]
is straight-up from the in jam), so they are the natural thread targets.

The tracer fires only in `bggate_6` (field 0x00A3) and only while auto-drive is
OFF (we want the hand-walked path, not the AD attempts). It logs `[TTRACE]
pos=(x,y) tri=N` at ~10 Hz while moving, and `[TTRACE] CROSSED line[i] ...` the
instant the player's side of any captured line flips -- showing which
interaction line is threaded for each turnstile and where.

Strip `FEPIC1_GATE_DIAG` (which now also removes `[TTRACE]`) and the `[LINEDIAG]`
block before any push.

## v0.17.9.16

Track A Step 2, chase-safety fix: scope the v0.17.9.15 avoidance-localization to
F9 path-finding only; chase-drive keeps the original global test. Still carries
the `[LINEDIAG]` + `FEPIC1_GATE_DIAG` diagnostics -- STRIP before push. LOCAL
build for a chase-first re-BAT.

Why: the v0.17.9.15 chase BAT REGRESSED -- the robot caught the party. The
avoidance localization is shared nav-core (StartChaseDrive also calls
ComputeAStarPath), and `doopen2a` (the Dollet town-square chase field) has two
SCREEN_BOUND lines (confirmed by the `[LINEDIAG]` dump: dest 321 + 344). The
local bounded test is strictly more permissive than the global infinite-line
test, so on doopen2a A* opened a different route and the X-ATM092 robot caught
up. The chase is tuned around the global test (v0.17.9.14 = 0 catches); the
hotel needs the local test. They conflict only because both went through the
same code path.

Fix: in ComputeAStarPath's neighbour-avoidance, branch on s_chaseDriveActive --
chase-drive uses `IsSeparatedByTriggerLine(startCX,startCY,nb...)` (the exact
v0.17.9.14 behaviour, byte-identical, so the chase route is unchanged and
should return to 0 catches), F9 uses `EdgeCrossesScreenBound(curX,curY,nb...)`
(the Step-2 Balamb Hotel fix). This mirrors the existing s_chaseDriveActive
splits elsewhere in nav-core (the funnel prune, calibration). One branch added;
no other logic changed.

v0.17.9.15 BAT context: the hotel half WORKED -- F9 auto-drive now steers
around `bcsaka_1` (the Step-2 fix is correct). The front gate still fails (the
FF8 door-collision = Step 3) and a Garden-exit NPC trigger blocked further
testing; both are separate from this fix.

Gate order unchanged: re-BAT the full Dollet chase FIRST (must be 0 catches),
then re-confirm the hotel.

## v0.17.9.15

Track A Step 2 (Balamb Hotel `bcsaka_1` auto-drive): make the A* screen-bound
trigger-line avoidance LOCAL. Carries forward the v0.17.9.14 FindPortal fix and
the v0.17.9.14.1 `[LINEDIAG]` + `FEPIC1_GATE_DIAG` diagnostics -- STRIP all the
diagnostics before any push. LOCAL build for a chase-first BAT.

Root cause: `ComputeAStarPath`'s neighbour-avoidance rejected any neighbour
whose centre was on the far side of an active screen-bound line as tested by
`IsSeparatedByTriggerLine` -- an INFINITE-line side split measured from the
START triangle's centre. On `bcsaka_1` the party spawns beside the harbour
screen-bound exit (call#41, the only `Screen Boundary` line on the field; the
other two captured lines are `Interactive Line`s the filter already ignores).
That exit's short segment sits up at the harbour, off the route to the Town
Square, but its infinite extension cuts the whole field, so every far-west
neighbour was rejected and A* returned "No path from tri 13 to 196" -> 0
waypoints -> auto-drive never moved (the symptom Aaron reported).

Fix: add `EdgeCrossesScreenBound(ax,ay,bx,by,skip)` (field_navigation.cpp, next
to `SegmentsCross`; forward-declared in field_nav_pathfinding.inl) and call it
in place of `IsSeparatedByTriggerLine` in the A* neighbour loop, testing the
ACTUAL traversal edge (current->neighbour centre) against each screen-bound
line's FINITE segment via the bounded `SegmentsCross`. Same SCREEN_BOUND/UNKNOWN
filter and skipTriggerIdx exemption as before; `IsSeparatedByTriggerLine` itself
is unchanged and still used by the recovery-wiggle projection and the
line-of-sight pre-pass. One call-site swap + one new helper; no other logic
touched.

Proven offline before BAT: the real `ComputeAStarPath` compiled against
`bcsaka_1`'s real walkmesh + its three captured lines (line types taken from
the live v0.17.9.14.1 `[LINEDIAG]` dump) gives "No path" under the old global
test and the full 65-triangle route under the new local test.

nav-core is SHARED with the X-ATM092 Dollet chase (StartChaseDrive also calls
ComputeAStarPath), and the offline guards do NOT exercise this path (the C++
harness stubs the trigger-line functions; the Python guard only checks portal
correctness). So the real safety gate is the in-game chase: BAT the full Dollet
chase FIRST and confirm 0 catches before the hotel BAT.

## v0.17.9.14.1

LOCAL DIAGNOSTIC build (NOT for push). Adds a greppable `[LINEDIAG]` per-
captured-line dump at field load (`field_nav_fieldscripts.inl`, right after the
existing "lineType assigned" summary): for each SETLINE it logs the assigned
lineType, destFieldId, hasExtDispatch, and the mapped JSM entity (index, sym,
category). Purpose: confirm the classification + MAPJUMP destination of the
three bcsaka_1 (Balamb Hotel exterior) screen-bound trigger lines so the Step-2
A* avoidance/exemption fix can key on destFieldId (e.g. don't fence off a
section line whose destination is the current field or the auto-drive target's
own destination).

Why: the offline proof this session (real ComputeAStarPath compiled against
bcsaka_1's real walkmesh + the 3 captured lines) DISPROVED the planned Step-2
"make avoidance local" one-liner as sufficient. Current code reproduces the live
"No path" exactly; the local edge test alone still fails; only the local test
PLUS exempting the mid-route section line (call#42) reaches the town-square exit.
The live exemption picks the wrong line: a gateway target's entityIdx (<= -400)
falls through the `<= -200` trigger-exempt branch and computes a bogus
skipTriggerIdx of 201 that matches no captured line, so nothing is exempted.

Carries forward the v0.17.9.14 Track A FindPortal fix and the FEPIC1_GATE_DIAG
diagnostic -- STRIP this `[LINEDIAG]` block AND FEPIC1_GATE_DIAG before any push.

## v0.17.9.14

Track A fix (Step 1): correct the walkmesh neighbour-edge vertex pair in the
field nav core. STILL CONTAINS the local `FEPIC1_GATE_DIAG` diagnostic from
v0.17.9.12/.13 -- STRIP BEFORE PUSH.

Root cause: for a triangle's neighbour on edge `e`, the FF8 .id walkmesh stores
that neighbour across edge (vertex[e], vertex[(e+1)%3]). Three functions in
`field_nav_pathfinding.inl` instead read the (vertex[(e+1)%3], vertex[(e+2)%3])
pair -- rotated one vertex off -- so they named the WRONG edge. On rectilinear /
narrow fields the mis-named edge is a wall, so the funnel emitted wall edges as
portals and the wall-parallel COLLAPSE slammed a waypoint onto the wall, wedging
auto-drive (the B-Garden front gate turnstile, B-Garden Hall 6, Balamb Hotel
exterior). AGENT_RADIUS + FF8 wall-slide + stuck-recovery masked it everywhere
else, which is why it presented only on narrow/rounded gates.

Fix: use the (vertex[edge], vertex[(edge+1)%3]) pair in all three sites --
`FindPortal`, `GetSharedEdgeLength`, and `EdgeMidpointPath`. One-vertex rotation,
no other logic changed.

Validated offline before BAT by the Step-0 harness (`tests/chase_harness.cpp`)
compiling this exact nav core against the real Dollet walkmeshes: the fix is
NEUTRAL on the chase fallback field domt2_1 (its spawn sits in a 42-triangle
walkmesh island with no A* path to the goal under both old and new code -- that
field clears by direct chase-drive steering, not the funnel) and IMPROVES
dotown_3 (funnel 30 waypoints with 5 out-of-mesh -> 6 waypoints with 0
out-of-mesh; closest approach to the inert robot slot 895 -> 803, both far
outside catch range). bggate_6's logged (-1686,-553) wall-hug is gone under the
fix.

Test order per the chase-risk plan: BAT the X-ATM092 Dollet chase FIRST (must
stay 0 catches) before the gate fields. The chase clears today via hacks tuned
on the old portals (COLLAPSE, protected waypoints, prune-skip), so the chase is
the regression to watch.

## v0.17.9.13

LOCAL diagnostic build (Track A, front-gate push-through). STRIP BEFORE PUSH.

Fixes the arm condition of the v0.17.9.12 `[GATEDIAG]` dump. v0.17.9.12 armed on
`_stricmp(fieldName, "fepic1")`, but the live engine name of the B-Garden front
gate (fieldId 0x00A3, display "B-Garden - Front Gate 5") is `bggate_6` — the
Track A notes' "fepic1" engine string was wrong (the fieldId was right). The
dump therefore never fired on the BAT. Now armed on the authoritative fieldId
(`fieldId == 0x00A3`, or name `bggate_6`), and the `[GATEDIAG]` log strings say
bggate_6. Still gated behind `#define FEPIC1_GATE_DIAG` (token kept; it is a
throwaway local that gets stripped). No production behaviour change.

Field facts confirmed from the v0.17.9.12 BAT's standard logging (bggate_6,
159 walkmesh triangles): exits resolve via the working `[MAPJUMP-RES]`
interpreter — `squallsd` SCREEN_BOUND -> 165 (B-Garden Hall 1, north/back in);
`zell`/`zells` SCREEN_BOUND -> 162 (south/out of Garden, the OUT turnstile);
`squall`/`squalls` are interactive Lines (turnstile interaction). Still missing
the decisive connectivity/reachability datum, which only `[GATEDIAG]` produces
— hence this re-fire.

## v0.17.9.12

LOCAL diagnostic build (Track A, fepic1 push-through gate). STRIP BEFORE PUSH.

Adds a one-shot `[GATEDIAG]` walkmesh/reachability dump that auto-fires ~1.5s
after entering field `fepic1` (B-Garden Front Gate 5), gated behind
`#define FEPIC1_GATE_DIAG` in `field_navigation.cpp`. Armed in
`HookedFieldScriptsInit` when the field is fepic1; fired once from `Update()`
after a short settle delay (no keypress required). Behaviour-neutral: the
production nav/drive path is untouched; the build only adds logging.

Purpose: decide why F9 auto-drive can't route through fepic1's scripted gate.
The dump distinguishes three hypotheses — (a) TRUE WALL: the south exit's
triangle is in a different connected component from the player's spawn
triangle; (b) MISSED/NARROW TRIANGLE: connected but A*'s `MIN_EDGE_WIDTH`
gate refuses the only portal; (c) TRIGGER-LINE BLOCK: connected but a
SCREEN_BOUND trigger line crosses the only portal so A*'s
`IsSeparatedByTriggerLine` refuses it.

Logs (all tagged `[GATEDIAG]`): walkmesh vertex/triangle counts + connected-
component labeling (island count and per-island sizes); player spawn pos +
triangle + component; each INF gateway and each captured SCREEN_BOUND trigger
line with its nearest triangle, component, raw reachability
(`AreTrianglesConnected` from spawn), and trigger-line separation; the INF
proximity trigger zones (the push-through trigger candidates) with entity
index / interaction type / endpoints; positioned + SETLINE JSM entities
(type, position, talk-setup) to identify the gate trigger entity; and a full
per-triangle dump (center, 3 vertices, 3 neighbors with WALL marks, component)
for offline geometry reconstruction.

No production behaviour change; no version-gated push expected from this build.

## v0.17.9.11

Exit interpreter: fix the JPF (conditional-jump) target offset, generalizing
correct exit-destination resolution to flag-gated multi-case (switch-on-
game_moment) exits beyond the dorm SCREEN_BOUND fields.

In `field_archive_jsm_mapjump_resolver.inl`, `InterpretExitMethod`: a taken JPF
(0x03) now jumps to `ip + param` (k=0), not `ip + 1 + param` (k=1). JMP (0x02)
is unchanged at k=1. The off-by-one was invisible until a JPF was actually
taken: the dorm exits' gate falls through (condition true), so their target was
never used and they still resolved correctly (228 / 245). Multi-case exits
(e.g. the bgryo2_1 `l1` switch on var[0x100] = game_moment) compile as a chain
of `reload var[0x100]; push <threshold>; CAL EQ; JPF <next-case>` blocks; with
k=1 each JPF skipped the per-case reload, so after the first compare the stack
ran dry and the interpreter bailed (`CAL/JPF-underflow`) and fell back to the
wrong addr-as-literal label. The bgryo2_1 `l1` [EXIT-DISASM] proved k=0: every
taken JPF must land on the reload at the start of the next case, else those
reloads are unreachable dead code (each prior case ends with `JMP -> RET`).

Validated against the live `[MAPJUMP-HOOK]` engine oracle: bcport_2 'Director'
now resolves `[INTERP] -> 120` (Balamb Harbor 1), matching the engine's actual
MAPJUMP3 destField=120 on crossing, and replacing the confirmed-wrong fallback
label 121. No regression: bgryo2_1 squalls still 228, bgroad_5 squalls still
245, bcport_1 Director still 125; bgryo2_1 `l1` now walks its full switch to a
clean RET with no map jump at game_moment=205 (correctly inactive — a story-
forced door; the squalls boundary 228 is the real navigable exit). No SEH /
crashes across the dorm + Dollet/Balamb chase regression.

The v0.17.9.7-.10 diagnostic instrumentation ([EXIT-TRACE]/[EXIT-OPSEQ]/
[EXIT-DISASM]) is retained behind `#define EXIT_TRACE_DIAG 0` (off; flip to 1
to re-probe a field). Production resolve path is unchanged by the flag.

## v0.17.9.6

Bug 4 (dormitory/corridor exit-destination mislabeling) — FIX PROMOTED. The
forward concrete JSM exit interpreter, validated log-only as `[SHADOW]` in
v0.17.9.5 against the live `[MAPJUMP-HOOK]` engine oracle (bgryo2_1 gate-true
fall-through -> 228; bgroad_5 gate-false JPF-taken -> 245; both correct where
the old addr-as-literal labeling gave 174 / 237), is now the authoritative
destField resolver for SCREEN_BOUND lines.

In `field_archive_jsm_mapjump_resolver.inl`: the validated interpreter is
renamed `ShadowInterpretMethod` -> `InterpretExitMethod` (body unchanged) and
wrapped in a leaf SEH guard `SafeInterpretExitMethod` (returns -1 on a wild
varblock read so `Run` stays free of `__try`). `MapjumpResolver::Run` now, for
each `JSM_ENT_LINE_SCREEN_BOUND` entity, interprets the first method containing
a MAPJUMP-family op and sets `info.param` to the concrete destField it returns
(logged ` [INTERP]`). The old abstract `ResolveMapjumpDest` is kept only as a
fallback (` [LITERAL fallback]` / ` [VARBLOCK fallback]`) for methods the
interpreter can't complete (RET/underflow/unmodeled). Because the interpreter
returns a plain positive literal (masked to 16 bits, never bit31), it flows
straight through the existing literal path in `HookedFieldScriptsInit` — no
downstream change. This runs on ALL fields now (the diagnostic field allow-list
is gone), so this BAT is also the multi-field regression.

Diagnostics stripped: `[BC-DUMP]` (`DumpLineBytecode`/`BcDumpGated`/
`BcIsLineType`), `[MAPJUMP-CTX]` (`DumpBytecodeContext`), and `[SHADOW]`
(`ShadowValidateExits`) from the resolver; both `[OPDUMP]` blocks
(`DumpOpcodeHandlers` + its `Install()` call) from `field_nav_mapjump_diag.inl`.
The `[MAPJUMP-HOOK]` live engine hooks are retained (low-volume transition
oracle). No source files added/removed; `deploy.bat` unchanged.

Expected BAT: on a fresh gated-field load, `[MAPJUMP-RES] ... (SCREEN_BOUND):
param 0x... -> 0x000000E4 [INTERP]` (228) for bgryo2_1 and `-> 0x000000F5
[INTERP]` (245) for bgroad_5, and the catalog exit labels read "Exit to
B-Garden - Hallway 5" / "...Dormitory Single 1" with no `[PSHM-DEST]`
addr-as-literal mislabel. Still open (separate BAT): the dropped real-door
`MAP_EXIT 'l1'` in bgryo2_1 (ent15, param=-2147483648).

## v0.17.9.5

LOCAL DIAGNOSTIC (do not ship) — bug 4, step 4: the forward concrete JSM exit
interpreter, landed FIRST as a log-only shadow validator. Adds `[SHADOW]`
(`ShadowInterpretMethod` + `ShadowValidateExits`) to
`field_archive_jsm_mapjump_resolver.inl`, called from `MapjumpResolver::Run`
after the `[BC-DUMP]`, gated to {bgryo2_1, bgroad_5, bghall_5}. It does NOT
touch `info.param` — no behavior change yet.

Unlike the abstract resolver (which resets the operand stack at every basic
block because it can't choose a branch), this interpreter FOLLOWS control flow
with one continuous stack and reads the live field varblock, so it computes the
exact destField the engine will use. Implements the opcode model locked in
v0.17.9.3/.4 + the deep-research cross-check: `0x00`/`0x07` push literal/
immediate; `0x0A`/`0x0C`/`0x11` varblock byte/word-unsigned/word-signed reads
at `0x01CFE9B8+param`; `0x0B`/`0x0D` pop (no write in shadow); `0x01` CAL
(pop2/push1, operator table 6=EQ…9=LS…, 5=NEG/F=NOT unary); `0x02` JMP, `0x03`
JPF, target = ip+1+param (k=1); `0x05` LBL no-op; `0x06` RET stop; `0x2A`/`0x38`
MAPJUMP3/DISCJUMP -> stack[-5]; `0x29`/`0x5C` MAPJUMP/MAPJUMPO -> stack[-4].
SEH-guarded; 200k-step cap.

Validation target for the BAT: `[SHADOW]` must log **interpDest=228** for
bgryo2_1's exit line, and **237 (pre-SeeD) / 245 (post-SeeD)** for bgroad_5 —
matched against the live `[MAPJUMP-HOOK]` engine truth. Once confirmed, a
follow-up build wires the interpreter into the catalog labeling path (replacing
addr-as-literal), fixes the dropped `MAP_EXIT 'l1'`, and strips ALL diagnostics.
Strip before any push.

## v0.17.9.4

LOCAL DIAGNOSTIC (do not ship) — bug 4, step 3. Extends `[OPDUMP]` in
`field_nav_mapjump_diag.inl` to also dump the opcode-0x01 secondary operator
table at `0x00B8DE4C` (entries 0x00..0x11, address + 96 raw bytes each).

The v0.17.9.3 `[OPDUMP]` + capstone nailed the JSM opcode model and corrected
the mod's mislabeled opcode names: `0x01` is a binary-op/compare (param selects
the operator via the `0x00B8DE4C` sub-table; pops 2, pushes 1), `0x02` is the
unconditional JMP (IP += param), `0x03` is JPF (pop; if zero, IP += param),
`0x07` pushes a sign-extended immediate, `0x0C`/`0x0A` push a varblock
word/byte at `0x01CFE9B8 + param`, `0x0B`/`0x0D` pop to varblock byte/word.
This solved the control-flow puzzle: the dorm exit gate is
`push varblock[0x100]; push <imm>; compare(op N); JPF` — fall through to the
first MAPJUMP3 when true. The last unknown is which relations operators 6 and 9
are; this dump captures their handlers for offline disassembly. Passive, fires
once at hook install; no behavior change. Strip before any push.

## v0.17.9.3

LOCAL DIAGNOSTIC (do not ship) — bug 4 (dormitory/corridor exit mislabeling),
step 2 toward the sandbox interpreter. Adds a one-shot `[OPDUMP]` to
`field_nav_mapjump_diag.inl` (`DumpOpcodeHandlers`, called once from
`MapjumpDiag::Install`). It logs the JSM opcode dispatch-table handler address
for opcodes 0x00..0x40 and the first 160 raw bytes of the key handlers
(0x01/0x02/0x03 jumps, 0x05/0x06, 0x07/0x08/0x0A/0x0B/0x0C/0x0D push/pop,
0x1C ext-dispatch), read read-only from the loaded `.text` under SEH.

Purpose: the v0.17.9.2 `[BC-DUMP]` BAT proved the engine fires the FIRST of
squalls m7's three MAPJUMP3s (-> 228 Hallway 5) but a forward trace under the
current "`0x01` = unconditional JMP" model skips it, so the opcode model is
wrong and the interpreter can't be built on it. The dispatch table that maps
opcodes to handlers lives in `.data` (no `.asm` dump), and the on-disk
disassembly is only readable through a ~3-line peephole, so this dumps the
handler bytes to the log instead; they get disassembled offline with capstone
to pin down the true semantics of the jump/push/pop opcodes (esp. whether
`0x01` is a conditional compare-and-branch). Passive, fires once at hook
install; no behavior change. Strip before any push.

## v0.17.9.2

LOCAL DIAGNOSTIC (do not ship) — bug 4 (dormitory/corridor exit mislabeling),
step 1 of the sandbox-interpreter fix. Adds a `[BC-DUMP]` full-method bytecode
disassembler to `field_archive_jsm_mapjump_resolver.inl`, gated to a small
field allow-list (bgryo2_1, bgroad_5, bghall_5). For every Door/Line entity in
those fields it dumps each method's instructions: IP, raw dword, decoded
opcode (high-byte model), signed low-24 param, absolute jump targets for
JMP/JPF/JMPB, immediate-vs-varAddr annotation for the PSHM/push family, and a
destField marker on MAPJUMP/MAPJUMP3/DISCJUMP/MAPJUMPO. Basic-block starts
(jump targets) are flagged with `*`.

Purpose: the walk-through BAT proved the exit destination is NOT a single
varblock value — it is a hardcoded immediate operand of whichever MAPJUMP3 the
script branches to (bgryo2_1 'squalls' carries three: ->228 Hallway 5, ->231,
->174 Hall 10), selected at runtime by flag-gated control flow. Engine ground
truth this run: destField=228 (Hallway 5), while addr-as-literal mislabels it
174 (Hall 10). The current static resolver fails because (a) it resets its
stack at every jump target so it never follows the taken branch, and (b) it
treats the push opcode's operand as a varblock address, ignoring the bank
field (bank 0 = immediate). This dump exposes the real branch structure so a
forward concrete interpreter (next build) can reproduce the engine's
destination by following branches with live variable reads — no caching, no
manual traversal. Acceptance test for the interpreter: bgryo2_1 must resolve
to 228 against current memory. Passive log only; no behavior change; strip
before any push.

## v0.17.9.1

Chapter 5 COMPLETE — SeeD rank R-key fix + automatic SeeD salary announcement
(GitHub issue #27 + the related salary-announcement gap). Both surfaces
BAT-confirmed; diagnostics removed; push-ready.

Surface 1 — R key now reports the real SeeD rank. `AnnounceSeedRank()`
(`menu_tts_hotkeys.inl`) reads SeeD points at savemap +0x0D6C (uint16) and
announces rank = points/100 ("SeeD Rank N"; "SeeD Rank A" at 3100+). This
replaces the old +0xF9C read, which landed on dead zeros and always produced
"No SeeD rank yet" (the issue #27 bug). Pre-SeeD gate: announces "No SeeD rank
 yet" when points == 500 && salaryCount == 0 (salaryCount = uint16 at +0x0CDE),
since pre-Dollet the pool sits at the base 500 with no salary paid.

Surface 2 — automatic TTS when the SeeD salary is paid. The salary window
flashes the rank and gil on screen and clears itself with no input, so a blind
player previously got only the chime. `PollSeedSalary()` (`dinput8.cpp`, polled
each non-title frame) detects a payment by its one-frame memory signature:
steps-since-pay (+0x0D64) resets from near the ~24,575 threshold to ~0 (drop
>10000), gameplay gil (+0x0B08) increases, and SeeD points (+0x0D6C) fall by a
small conduct amount. That triple is unique to a payment — it excludes save
loads (arbitrary jumps / large points deltas) and battle gil (no steps reset,
no points change). On a hit it speaks rank + amount + direction:
- same rank: "SeeD salary. Rank N. <amount> gil."
- higher:    "SeeD salary. Promoted to Rank N. <amount> gil."
- lower:     "SeeD salary. Dropped to Rank N. <amount> gil."
(Rank 31 = "Rank A".) Rank is read live (points/100), the amount is the actual
gil delta (correct even across a rank boundary), and direction compares the
rank before vs after. One `[SALARY] PAY:` line is logged per payment.

Investigation note (kept for the record): the salary-payment counter at +0x0CDE
is NOT synced to the chime — a BAT with the salary window confirmed on screen
logged the counter unchanged; it only ticks over later (next save). The earlier
counter-tick detector (v0.17.9.0.2) was therefore replaced by the change-logger
(v0.17.9.0.3) that pinned the gil-up + steps-reset trigger, then the wired
announcement (v0.17.9.0.4), and finally this build which strips the verification
heartbeat. The promoted/dropped wordings share the exact code path as the
BAT-confirmed same-rank line (only the verb/branch differs) and will surface in
normal play; the amount and rank are ground-truth regardless of direction.

BAT-confirmed: R announces "SeeD Rank 3" on a SeeD save and "No SeeD rank yet"
pre-SeeD; a world-map salary spoke "SeeD salary. Rank 3. 1500 gil." with
`[SALARY] PAY: rank 3->3 | amount 1500 gil | points 392->383 | steps 24575->0`.

## v0.17.9.0.4

Chapter 5 / Surface 2 — automatic SeeD salary announcement WIRED (verification build).

The v0.17.9.0.3 change-logger BAT pinned the real-time trigger. At the salary
chime, in a single frame: gil (+0x0B08) jumped 7400->8900 (+1500 = the rank-3
table value), SeeD points (+0x0D6C) dropped 392->383, and steps-since-pay
(+0x0D64) reset from 24494 to 0 — while the counter (+0x0CDE) stayed at 14
through close. So the counter never was the trigger; the gil-up + steps-reset
pair (plus a small points drop) is, and it fires in real time with the window.

`PollSalaryDiag` is replaced by `PollSeedSalary` (dinput8.cpp, each non-title
frame). It detects a payment when, in one poll: steps dropped by >10000 (reset
from near the ~24,575 threshold), gil increased, and points dropped by 0..100
(small conduct change). That triple excludes save loads (arbitrary jumps, large
points delta) and battle gil (no steps reset, no points change). On a hit it
speaks rank + amount + direction via SAPI:
- same rank: "SeeD salary. Rank N. <amount> gil."
- higher:    "SeeD salary. Promoted to Rank N. <amount> gil."
- lower:     "SeeD salary. Dropped to Rank N. <amount> gil."
(Rank 31 says "Rank A".) Rank = points/100 after the drop; amount = gil delta
(ground truth, so it stays correct even across a rank boundary); direction =
rank-after vs rank-before. It also logs one `[SALARY] PAY: ...` line per payment.

The `[SALARYDIAG] HB` ~1s heartbeat is kept as a verification aid for this BAT
and will be removed before the chapter is pushed (Build 4 = strip HB + squash).

Test: load save20, travel on the world map until the salary chime — you should
hear "SeeD salary. Rank 3. 1500 gil." Send the `[SALARY] PAY:` line. Not pushed.

## v0.17.9.0.3

Chapter 5 / Surface 2 investigation build #2 (LOCAL).

The v0.17.9.0.2 BAT disproved the counter-tick approach: a screenshot taken
while the SeeD salary window was on screen ("S-Lv. 3 / 1500G", rank 3 = 1500
gil) showed the counter at +0x0CDE still reading 14 across every poll until the
game closed -- no `[SALARYDIAG] PAY:` ever fired. So +0x0CDE is NOT synced to
the chime/window; it ticks over later (window dismiss or next save). The
file-diff that originally showed 14->15 just captured a post-dismiss state.

Reworked PollSalaryDiag from a counter-tick detector into a change-logger over
all four salary-related savemap fields, so the next BAT reveals which field
actually moves at the chime (and in what frame order):

1. Emits `[SALARYDIAG] CHANGE ...` the instant counter (+0x0CDE), gil (+0x0B08),
or points (+0x0D6C) changes, each line carrying the full context of the other
fields + stepsSincePay (+0x0D64) so the ordering around the chime is visible.
2. Emits a `[SALARYDIAG] HB ...` heartbeat ~once per second while the party is
moving (gated on stepsSincePay changing) so we can watch the step counter climb
toward the ~24,575 payment threshold and confirm the poll is reading correctly.
3. Primes silently on first observation; logs `[SALARYDIAG] primed: ...` once.

Test protocol: load save20, travel on the world map until you hear the salary
chime (the window shows and clears on its own -- no button press), then keep
moving ~10 seconds so the logger captures anything that updates during/after the
window, and close. Send all `[SALARYDIAG]` lines. The CHANGE lines will pin
which field (gil and/or points) moves at window-appear vs. which lags, which
decides the real-time trigger for the Surface 2 TTS. LOCAL diagnostic -- not for
push; ships no salary TTS yet.

## v0.17.9.0.2

Chapter 5 housekeeping + Surface 2 investigation build (LOCAL).

1. Removed the Surface 1 F12 SeeD-membership diagnostic from dinput8.cpp now
that the pre-SeeD gate is BAT-confirmed (F12 is free again). Surface 1's code
(rank read at +0x0D6C, points==500 && salaryCount==0 gate) is unchanged and now
in push-clean form.

2. Added a LOCAL automatic SeeD-salary detector (PollSalaryDiag in dinput8.cpp,
called each non-title frame). A payment increments the counter at +0x0CDE by 1,
adds rank-based gil to +0x0B08, and drops points at +0x0D6C by 10; when the
counter ticks up by exactly 1 it logs `[SALARYDIAG] PAY:` to ff8_mod.log with
the gil delta (= amount received), points/rank before-and-after, an instant
direction (rank vs the pre-decrement rank), a vs-last-pay direction (rank vs the
rank at the previous payment), and the salary-table expectation for the new
rank. Counter jumps of !=1 are treated as save loads and rebaseline silently.

Rationale: the salary window text is in a runtime archive (not the exe) and the
values render as number sprites, so memory is the reliable source for rank, gil
and direction. Unlike the victory screen (multi-phase, player presses through
several screens, so a memory dump desyncs), salary is a single instantaneous
event, so detecting the counter tick and announcing IS synced to the display --
the same pattern as the existing level-up / item-received auto-announcements.

Test: walk (or drive/chocobo on the world map) until a salary is paid; send the
`[SALARYDIAG] PAY:` line(s) from ff8_mod.log. The data confirms the gil delta
and pins the exact rank-direction semantics for the Surface 2 TTS (next build).
This build ships no salary TTS yet and is LOCAL (diagnostic) -- not for push.

## v0.17.9.0.1

Chapter 5 (Surface 1) follow-up to v0.17.9.0: add the pre-SeeD gate so R stops
announcing a false "Rank 5" before the Dollet exam, plus a LOCAL F12 diagnostic
to validate the gate and catch edge cases. (Includes the LOCAL-ONLY F12 hook, so
this build is not for push as-is; the diagnostic comes out before the chapter is
pushed, per the F12-reserved rule.)

Gate (`AnnounceSeedRank` in menu_tts_hotkeys.inl): before the field exam grades
the player, the SeeD points pool sits at exactly the formula base (500) and no
salary has been paid; pre-promotion rank modifiers are deferred to graduation,
so the live value stays exactly 500 until promotion. R now treats
(points == 500 && salaryCount == 0) as "No SeeD rank yet". A paid SeeD that
happens to sit at exactly 500 points (Rank 5) still announces correctly because
salaryCount > 0. salaryCount is the salary-payment counter at +0x0CDE (0 in the
pre-SeeD save, 14 then 15 across one payment in the SeeD saves).

F12 diagnostic (dinput8.cpp, !alt-gated, edge-triggered) dumps to ff8_mod.log
under `[SEEDDIAG]`: context (locId/fieldId/mode/Gil), points at +0x0D6C with
computed rank, salary count (+0x0CDE), steps-since-pay (+0x0D64), save count,
and a labelled hex window of +0x0CD8..+0x0E70 (SeeD struct + start of the
field-variable block). Test: press F12 once on a pre-SeeD save and once on a
SeeD save; compare the two blocks to confirm the gate fields and, if a cleaner
story "became a SeeD" flag is wanted later, to spot it.

BAT: (1) R on a SeeD save -> correct "SeeD Rank N" (cross-check vs salary table);
(2) R on the pre-SeeD save -> "No SeeD rank yet"; (3) F12 on both saves -> send
the two `[SEEDDIAG]` blocks from ff8_mod.log.

## v0.17.9.0

Chapter 5 (Surface 1): fix the R hotkey, which always announced "No SeeD rank
yet" regardless of actual rank (GitHub issue #27).

Root cause: AnnounceSeedRank() read a uint16 at savemap +0xF94 + 0x08 (= +0xF9C).
That 0xF94 was derived by summing section sizes in a code comment and was never
measured; it lands in the field-variable block and reads dead zeros in every
save examined, so the "value == 0 => no rank" branch always fired.

Fix: read SeeD points (SeeD experience) at the measured offset +0x0D6C (uint16)
and announce rank = points / 100 (each rank = 100 points). 1..30 announce as
"SeeD Rank N"; 3100+ points announce as "SeeD Rank A" (the 31st rank); below 100
still says "No SeeD rank yet". Wording changed from "SeeD Level" to "SeeD Rank"
to match in-game terminology (Aaron's call: R announces rank only).

Offset confirmed empirically by diffing three of Aaron's .ff8 saves. The .ff8
files are FF7/FF8 LZSS-compressed (4-byte LE size header, then the stream); once
decompressed they carry the original-PC slot layout, and the savemap maps to
live memory as live_offset == decompressed_file_offset - 0x184 (anchored on
Squall HP/EXP + Gil + location, all matching). Across the saves: a pre-SeeD save
reads points = 500 (exactly the documented base of the initial-rank formula
before the Dollet exam grades it); a Rank 3 save reads 392 (392/100 = 3); the
same save right after one salary reads 383 (-9 = lose 10 per payment + 1 from a
kill, matching the documented -10-per-pay decay). 392/100 = 3 matched the
observed 7400->8900 Gil salary (1500 = Rank 3) exactly. A neighbouring value at
+0x0D62 that happened to read 3 was rejected: it is the high word of the u32
total-step counter at +0x0D60, not an independent rank field.

Known minor edge: between the point pool initializing (~500) and the Dollet exam
formally promoting Squall, R will announce a provisional rank (e.g. "Rank 5")
rather than "No SeeD rank yet", because the game stores the base 500 before
grading and there is no separately-confirmed membership flag yet. This window is
brief and pre-Dollet; revisit with a membership-flag diagnostic if it matters.

BAT: open the in-game menu (mode 6) and press R as a SeeD; confirm it speaks the
correct "SeeD Rank N" (cross-check against the salary amount via the table) and
logs `[MenuTTS] SeeD: Rank N (points=...)` in Logs/ff8_menu.log.

## v0.17.8.20

Refactor (zero behavior change): split field_nav_autodrive.inl to relieve size
pressure. The file had reached 79.83 KB — ~170 bytes under the 80 KB hard cap the
CI mirror enforces at push time — so any logic edit in UpdateAutoDrive would have
pushed it over and forced another emergency comment trim (the Chapter 4 push
hiccup that motivated this).

Extracted into two new files, both included immediately before
field_nav_autodrive.inl in field_navigation.cpp (helpers before calib before
autodrive, so the call graph resolves top-down):
  - field_nav_autodrive_helpers.inl (12.5 KB): InjectKey, ReleaseAllDirections,
    SetHeldDirections, SetAnalogFromVector, StopAutoDrive — the analog/keyboard
    injection primitives and the drive-stop lifecycle.
  - field_nav_autodrive_calib.inl (7.6 KB): the CALIB phase statics plus a new
    RunCalibration() holding the heading-calibration state machine that used to
    sit inline at the top of UpdateAutoDrive. The call site is now
    `if (RunCalibration()) return;`, faithfully replicating the two inline
    `return;`s (true = a calibration tick was consumed, caller returns; false =
    idle/done, fall through to normal nav). Only structural change: phase 2's
    `if` became an `else if` so both phases fall to one trailing `return true;`.

field_nav_autodrive.inl now holds only UpdateAutoDrive and is 62.9 KB (~17 KB
under the cap). No logic changed; the v0.15.9.11.3.4 InjectKey comment and the
v0.17.8.19.4 gateway pass-through block are untouched. The calib statics moved
out of autodrive.inl into the new calib file but keep the same compilation-order
position, so directiondrive/handlekeys/fieldscripts (included later) still see
them.

BAT: F9 path-finding on any field (confirm `[CALIB] phase 1 done` / `phase 2
done` still fire) plus an X-ATM092 chase auto-pilot run from before the Lapin
Beach FMV (confirm 0 catches and that `[chase-drive] STARTED`,
`[funnel-prune] skipped for chase-drive`, and `chase-drive gateway pass-through`
all still fire).

## v0.17.8.19.4

Chapter 4 ACTUAL fix: chase-drive gateway pass-through. The previous three
builds (v0.17.8.19.1, .2, .3) were all chasing a wrong diagnosis (prune
over-trimming the path). The v0.17.8.19.3 BAT proved the prune theory was
wrong: with the prune fully skipped and the full 32-waypoint funnel output
used, the party still got caught at the south exit of domt2_1. Trajectory
analysis from the v0.17.8.19.3 log revealed the real bug: party reaches
the gateway midpoint and **oscillates around it without ever crossing
the gateway line**, getting caught while wobbling in place.

**v0.17.8.19.3 BAT evidence (2026-05-28 19:12:50-19:13:06).**
  - `[funnel-prune] skipped for chase-drive (32 waypoints kept; ...)`
    confirmed -- the prune-skip is now wired correctly.
  - Path: 32-waypoint dense funnel from `(520,1391)` to `(-93,-3414)`.
    Party traversed cleanly in ~15 seconds with no stuck behavior.
  - Per-tick steering (gateway line is at Y=-3414, target (-93,-3414)):
      t=720: pp=(-27,-3283), 131N of gateway, analog lX=67 lY=979 (S)
      t=750: pp=(-80,-3401),  13N of gateway, analog lX=-248 lY=993 (DL)
      t=780: pp=(-110,-3407),  7N of gateway, analog lX=989 lY=-273 (**UR -- NE!**)
      t=870: pp=(-82,-3404), 10N of gateway, analog lX=-293 lY=986 (DL)
      t=900: pp=(-113,-3410), 4N of gateway, analog lX=989 lY=-276 (**UR**)
  - Party stuck in a ~30x6-unit box at the gateway approach, Y never
    going more negative than -3410. Battleyarou (TALKRAD ~500) caught
    the wobbling party.

**Root cause diagnosis.** `UpdateAutoDrive` has v0.15.9.2.15 "offset 300
units past line" logic in the `gotCrossLine` block:
```
if (gotCrossLine) {
    // ... compute heading toward cross-line ...
    tx += (dx/dirLen) * 300.0f;   // push target 300 units past the line
    tz += (dz/dirLen) * 300.0f;
    dx = tx - px;  dz = tz - pz;
}
```
This correctly offsets `tx, tz, dx, dz` past the gateway. **But later in
the same function**, the waypoint chain-advance block OVERWRITES `dx, dz`:
```
float steerX = tx, steerY = tz;
if (s_waypointCount > 0 && s_waypointIdx < s_waypointCount) {
    // chain-advance (never advances past final waypoint)
    steerX = s_waypoints[s_waypointIdx][0];   // <-- gateway CENTER point
    steerY = s_waypoints[s_waypointIdx][1];
}
dx = steerX - px;  // <-- OVERWRITES the offset-adjusted dx
SetAnalogFromVector(dx, dz);
```
The funnel's last waypoint sits at the gateway midpoint (passed in to
FunnelPath as the goal), so once `s_waypointIdx == s_waypointCount-1`,
`steerX/steerY` lock onto the gateway center, the offset is silently
discarded, and the analog aims **at** the gateway rather than
**through** it. Engine wall-sliding then produces the UR-direction
wobble once the party gets pinned at the corner of the goal triangle.

**Fix shape (`src/field_nav_autodrive.inl`).** Single ~25-line block
inserted directly after the waypoint chain-advance sets `vecWpRawX/Y`,
gated on `s_chaseDriveActive && s_driveCrossLineActive &&
s_waypointIdx == s_waypointCount-1`:
```
float toGwX = (float)s_chaseDriveTargetX - px;
float toGwY = (float)s_chaseDriveTargetY - pz;
float toGwLen = sqrtf(toGwX*toGwX + toGwY*toGwY);
if (toGwLen > 1.0f) {
    steerX = (float)s_chaseDriveTargetX + (toGwX/toGwLen) * 300.0f;
    steerY = (float)s_chaseDriveTargetY + (toGwY/toGwLen) * 300.0f;
}
```
Now the analog points 300 units PAST the gateway along the
player->gateway direction. The party walks straight through. Arrival is
still owned by the cross-product sign-flip check in the `gotCrossLine`
block above (which fires the instant the player's Y crosses the line),
so we don't blow past the destination on the other side.

**Scope.**
  - F9 path-finding: unaffected (s_chaseDriveActive guard).
  - Direction-drive: unaffected (different code path entirely).
  - Chase-drive without crossing line: unaffected (none currently exist;
    s_driveCrossLineActive guard).
  - Chase-drive with crossing line but not on last waypoint: unaffected.
  - The corridor-steering block below this point is disabled for
    chase-drive (existing `false &&` guard from v0.15.9.2.3 + v0.17.6.2),
    so the override flows directly to `SetAnalogFromVector`.
  - `vecWpRawX/Y` is intentionally NOT updated -- the `[drive-vec]` log
    keeps showing the raw funnel waypoint so we can compare aim vs
    override.

**Why the previous three builds were not the actual fix (but are kept):**
  - v0.17.8.19.1 (protected wall-parallel midpoint): Real fix for a real
    bug -- v0.17.5.2's `PruneCollinearWaypoints` was deleting v0.16.1.2's
    COLLAPSE'd doorway waypoint at (-13,-1508), which made some chase
    paths thread the wrong side of the door geometry. The protection
    mechanism prevents that from regressing. Not the cause of THIS bug.
  - v0.17.8.19.2 (skip prune entirely for chase-drive): Reasonable design
    -- the TTS micro-corner motivation for the prune doesn't apply to
    chase (chase is silent), and chase benefits from dense waypoints for
    tight corridor navigation. Not the cause of THIS bug.
  - v0.17.8.19.3 (flag-set ordering): Made v0.17.8.19.2 actually engage
    by setting `s_chaseDriveActive=true` before `FunnelPath` runs. The
    prune-skip gate now works as intended. Not the cause of THIS bug.

All three are kept as defensive infrastructure: protected waypoints +
skipped prune produce a denser, more robust chase path that the
gateway pass-through logic then steers through cleanly. The combination
is correct; the previous three builds individually didn't solve the
failure mode because the failure was in the pass-through logic, not in
the path quality.

**Expected BAT outcome.**
  - Chase reaches Lapin Beach FMV (`disc00_07h.avi`) with 0 catches.
  - No `[CBF] PASS chase BATTLE` lines anywhere in the run.
  - New log line: `[drive] chase-drive gateway pass-through: player=(...)
    gw=(-93,-3414) toGwLen=... -> steer overridden to (...) (300 units
    past gateway)` fires when the party gets within range of the south
    exit of `domt2_1`.
  - Party traverses the gateway in a single pass, no oscillation.

**Modified files:**
  - `src/field_nav_autodrive.inl` (gateway pass-through block in
    `UpdateAutoDrive` after waypoint chain-advance)
  - `src/ff8_accessibility.h` (version bump)
  - `CHANGELOG.md` (this entry)
  - `DEVNOTES.md` (Chapter 4 status update)

## v0.17.8.19.3

Chapter 4 fix-the-fix: move `s_chaseDriveActive = true` BEFORE the path
computation block in `StartChaseDrive`. v0.17.8.19.2's prune gate worked
but never fired during chase-drive because the flag was set near the
bottom of `StartChaseDrive` -- AFTER `ComputeAStarPath` / `FunnelPath`
had already run. Same `[CBF] PASS` outcome at the south exit; same
8-waypoint post-prune path; identical [funnel-prune] log line as
v0.17.8.19.1.

**v0.17.8.19.2 BAT evidence (2026-05-28 18:55:13-18:55:26).**
  - `[funnel-prune] removed 24 collinear waypoints (eps=50 units, 25 sweeps,
    2 protected wall-parallel midpoints preserved)` -- IDENTICAL to
    v0.17.8.19.1. The prune ran, removing 24 waypoints. The new
    `[funnel-prune] skipped for chase-drive` log line that v0.17.8.19.2
    was supposed to add NEVER FIRED, because `s_chaseDriveActive` was
    `false` at the time `FunnelPath` ran.
  - Subsequent log sequence confirms the ordering:
      line 1610: `[funnel-prune] removed 24` (FunnelPath finished)
      line 1611: `35 triangles -> 8 waypoints (post-prune...)` (funnel summary)
      line 1612: `[chase-drive] A*+funnel: 8 waypoints from tri 71 to 6`
      line 1613: `[chase-drive] STARTED tgt=(-93,-3414) walk=0 ...` -- the
        STARTED line is logged at the END of `StartChaseDrive`, AFTER the
        flag was set. By the time the chase-drive STARTED log fires, the
        path is already computed and pruned.
  - Same wedge at (-64,-1562) for 5 seconds (because the path is the same
    8 sparse waypoints we had in v0.17.8.19.1), velocity-stuck recoveries
    skipped wp 5->6->7, party crawled to (-110,-3407), `[CBF] PASS` fired
    with battleyarou as caller at 18:55:26.

**Diagnosis.** `StartChaseDrive` in `src/field_nav_directiondrive.inl`
performed work in this order:
  1. Mutex / prereq checks (s_driveActive, s_directionDriveActive, dialog,
     field, player entity, position read).
  2. Drive state init (s_driveActive=true, stuck counters, position seed).
  3. Calibration setup, fake gamepad install, crossing-line setup.
  4. **Path computation: ComputeAStarPath + FunnelPath.** <-- prune runs
     here, sees `s_chaseDriveActive == false`, runs the prune unmodified.
  5. Walk modifier setup.
  6. `s_chaseDriveTargetX/Y` cached, `s_chaseDriveActive = true` set, final
     `[chase-drive] STARTED` log fires.

Step 6's flag-set was too late for step 4's prune gate.

**Fix shape (`src/field_nav_directiondrive.inl`).** Move steps 6's three
statements (`s_chaseDriveTargetX = targetX`, `s_chaseDriveTargetY = targetY`,
`s_chaseDriveActive = true`) up to right after step 2's drive state init,
before step 3's calibration setup. Replace the old set-site with a comment
pointing at the new location. No new flag, no logic change beyond ordering.

**Safety analysis.**
  - The mutex check at the top of `StartChaseDrive` (refuses if another
    drive is active) is unchanged; we only reach the new set-site if that
    check passed. No risk of setting the flag while another owner thinks
    it has the drive.
  - No failure path between the new set-site and the original (now-removed)
    set-site clears the flag. The pre-v0.17.8.19.3 code path always reached
    the bottom `s_chaseDriveActive = true` line regardless of walkmesh or
    A* failure -- if the walkmesh was invalid, the code logged and fell
    through; if A* failed, same. So pre-v0.17.8.19.3 ALSO had no failure
    path that left the flag false after a successful mutex check. The new
    ordering preserves this property.
  - `UpdateAutoDrive`'s chase-drive branches read `s_chaseDriveActive` only
    after `StartChaseDrive` returns and the next `Update()` tick fires.
    Same thread (chase_auto_pilot's `Update` -> `StartChaseDrive`, and
    `FieldNavigation::Update` -> `UpdateAutoDrive`; both called from the
    mod thread), no race.
  - The recovery re-paths in `UpdateAutoDrive`'s wiggle-completion block
    already call `FunnelPath` with `s_chaseDriveActive=true` (because they
    run during an active chase-drive), so the v0.17.8.19.2 prune gate has
    always worked correctly for them. Only the initial `StartChaseDrive`
    call was exposed. This fix doesn't change recovery behavior.

**Expected BAT outcome.** Identical to v0.17.8.19.2's stated expectations:
chase reaches Lapin Beach with 0 catches, no `[CBF] PASS chase BATTLE`
lines anywhere, and now the `[funnel-prune] skipped for chase-drive (N
waypoints kept; ...)` log line ACTUALLY fires (with N >= 30 on domt2_1
instead of the 8 we got with the prune running).

**Modified files:**
  - `src/field_nav_directiondrive.inl` (move chase-drive flag set earlier)
  - `src/ff8_accessibility.h` (version bump)
  - `CHANGELOG.md` (this entry)

## v0.17.8.19.2

Chapter 4 follow-up: skip `PruneCollinearWaypoints` entirely when chase-drive
owns the wheel. The v0.17.8.19.1 protected-waypoint mechanism preserved the
doorway-threading COLLAPSE midpoint correctly, but the v0.17.8.19.1 BAT showed
the prune was also deleting too many OTHER intermediate funnel waypoints --
the party threaded the doorway at (-13,-1508) and then immediately wedged
at (-64,-1562) heading to the next surviving waypoint (-306,-1919). Same
`[CBF] PASS` outcome at the south exit -- different proximate cause.

**v0.17.8.19.1 BAT evidence (2026-05-28 18:30:48-18:31:02).**
  - `[funnel] COLLAPSE wall-parallel portal 23 ... wp=(-13,-1508)` fired as
    expected.
  - `[funnel-prune] removed 24 collinear waypoints (eps=50 units, 25 sweeps,
    2 protected wall-parallel midpoints preserved)` -- the new "2 protected"
    counter confirmed the v0.17.8.19.1 protection mechanism engaged. (The
    "2" is the number of times the protected waypoint was encountered as
    a near-collinear candidate during sweeps, not the count of distinct
    protected waypoints. Only one COLLAPSE log line fired, so there's one
    distinct protected midpoint at (-13,-1508).)
  - `[drive-vec]` line 1636: `wp 4/8 reached (dist=37)` while heading toward
    `(-13,-1508)`. Protected waypoint REACHED. v0.16.1.2 fix restored.
  - But: next `wpRaw=(-306,-1919)` from t=300 onward. Player at (4,-1475)
    -> (-64,-1562), then STUCK at (-64,-1562) from t=330 through t=420 (5
    seconds), velocity-stuck recoveries skipped wp 5->6->7. Player reached
    (-110,-3407) by t=780; `[CBF] PASS` fired with battleyarou as caller.

**Diagnosis.** The funnel emitted 32 waypoints; prune kept 8. Even with
(-13,-1508) preserved, the remaining 7 waypoints are too sparse to traverse
the full domt2_1 corridor. Between the doorway exit (-13,-1508) and the
south-corridor turn point (-306,-1919) the original 32-waypoint path had
several intermediate steering points the funnel emitted at portal-vertex
alternations -- all of which were near-collinear with their neighbors (the
whole point of SSFA's funnel-pull behavior) and got deleted by the prune.

**Fix shape.** Skip the prune entirely when `s_chaseDriveActive == true`.
v0.17.5.2's prune was introduced because each waypoint advance triggers a
TTS announcement during F9 navigation; on bg2f_2's classroom hallway the
funnel emitted hundreds of micro-corner waypoints and the TTS was rattling
through cardinal direction changes. Chase auto-pilot is silent (no per-
waypoint TTS), so the TTS spam motivation doesn't apply, and chase needs
the path density to navigate long Dollet corridors. v0.16.1.4 ran clean
with 0 catches on the full unpruned funnel output.

**Code change (`src/field_nav_pathfinding.inl`).** Single conditional in
FunnelPath right before the prune call:
  - `if (!s_chaseDriveActive)` gates the `PruneCollinearWaypoints` call.
  - The else branch logs `[funnel-prune] skipped for chase-drive` with the
    waypoint count so chase BATs surface the skip action.

Not touched:
  - `PruneCollinearWaypoints` itself (with the v0.17.8.19.1 protection check).
    F9 navigation still calls it and benefits from the TTS micro-corner fix
    AND the v0.17.8.19.1 protection (if a player F9-paths to an NPC behind
    a wall-parallel doorway, the doorway midpoint survives).
  - The v0.17.8.19.1 protected-waypoint mechanism in FunnelPath (still
    populates s_protectedWaypointPos during COLLAPSE; if FunnelPath is
    invoked for F9 to a target behind a wall-parallel portal, the
    protection engages).

**Expected BAT outcome.** Identical to v0.17.8.19.1's stated expectations:
chase reaches Lapin Beach with 0 catches, no `[CBF] PASS chase BATTLE`
lines anywhere. The new `[funnel-prune] skipped for chase-drive` log line
should appear once per chase field (each FunnelPath invocation). Chase-
field waypoints will be denser; the per-tick `[drive-vec]` log will show
more frequent `wp N/M reached` advances reflecting the un-pruned 30+ wp
path instead of 8.

**Modified files:**
  - `src/field_nav_pathfinding.inl` (FunnelPath: chase-drive skip + log)
  - `src/field_navigation.cpp` (move `s_chaseDriveActive` + sibling state
    declarations above the `field_nav_pathfinding.inl` include so the new
    chase-drive gate compiles -- the first build attempt errored out with
    `error C2065: 's_chaseDriveActive': undeclared identifier` because
    pathfinding.inl is included earlier than the original declaration spot)
  - `src/ff8_accessibility.h` (version bump)
  - `CHANGELOG.md` (this entry)

## v0.17.8.19.1

Chapter 4 (chase regression): restore X-ATM092 chase auto-pilot to 0-catch behavior
on `domt2_1` by protecting wall-parallel-portal COLLAPSE waypoints from
`PruneCollinearWaypoints`. The Aaron BAT 2026-05-28 (built on v0.17.8.18.1)
showed battleyarou catching the party at the south exit of `domt2_1` --
identical to the pre-v0.16.1.2 symptom this code was supposed to have fixed.

**Root cause.** v0.17.5.2 added `PruneCollinearWaypoints` to collapse micro-
corner waypoints emitted by SSFA on zigzag walkmesh corridors (motivated by
TTS spam on the bg2f_2 classroom hallway). The prune runs at the bottom of
`FunnelPath` and removes any waypoint whose perpendicular distance from the
line through its neighbors is below `PRUNE_PERP_EPSILON = 50.0f`. Geometrically
that's the right call for ordinary funnel output -- but the wall-parallel-
portal COLLAPSE waypoints emitted by v0.16.1.2 are constraint-forced doorway
midpoints that sit ON the corridor line by construction (a doorway through a
wall IS a near-collinear point). v0.17.5.2's prune deleted them, defeating
the v0.16.1.2 fix and reproducing the v0.16.1.1 stuck-at-wall behavior.

The BAT log on `domt2_1` made the regression explicit:
  - Line 1573: `[funnel] COLLAPSE wall-parallel portal 23 dX=0.0 dY=278.0
    L=(-42,-1638) R=(-42,-1360) -> wp=(-13,-1508) tri 26->27` -- COLLAPSE
    fired correctly, producing the (-13,-1508) constraint midpoint.
  - Line 1595: `[funnel-prune] removed 24 collinear waypoints (eps=50 units,
    25 sweeps)` -- prune deleted 24 of 32 waypoints, including (-13,-1508).
  - Lines 1604-1629 (per-tick [drive-vec] log): the final 8 surviving
    waypoints contained NO (-13,-1508). wp 4 was at (-64,-1658), essentially
    on the L endpoint of the portal (at x=-42 wall corner).
  - 17:36:09-13: party stuck at (-31,-1588) for ~4 seconds, velocity-stuck
    recoveries skipped wp 4->5->6->7. Y=-1588 sits between the L=-1638 and
    R=-1360 portal endpoints; the party slid along the x=-42 wall instead of
    threading the doorway.
  - 17:36:16: `[CBF] PASS chase BATTLE` (cap=INT_MAX in AUTO mode is the
    v0.15.9.9 verification setting, intentionally letting through any battle
    the auto-pilot fails to avoid). caller entityPtr=0x0188C284 = battleyarou
    (TALKRAD=500, set at field load on line 1543).

The pre-v0.16.1.2 narrative in `field_nav_pathfinding.inl` describes this
exact failure mode verbatim: "on domt2_1, the wall-parallel portal between
tri 26 and tri 27 at x=-42 (L=(-42,-1638), R=(-42,-1360)) is the ONLY exit
from tri 26 going south. Skipping it left the player with no aim point
inside the doorway, so they slid along the x=-42 wall accumulating
moveDist=160 over 2s with zero net displacement, then froze entirely
(moveDist=0) for another 2s before velocity-stuck recovery advanced
wp 23 -> 24 -> 25 -> 26 over 5 seconds total."

**Fix.** New protected-waypoint mechanism in `field_nav_pathfinding.inl`:
  - `s_protectedWaypointPos[MAX_CORRIDOR][2]` + `s_protectedWaypointCount`
    static arrays at file scope. Hold the positions of any waypoints the
    prune must not delete.
  - `FunnelPath` resets the counter to 0 at the top of every call (fresh
    protection list per re-path).
  - The wall-parallel-portal COLLAPSE branch records the post-AGENT_RADIUS-
    shift midpoint `(mx, my)` into the array right where it sets
    `portals[numPortals].lx = mx; ...; portals[numPortals].rx = mx`.
  - `PruneCollinearWaypoints` checks each candidate-for-prune against the
    protected list (squared-distance vs `PROTECTED_WP_EPSILON = 1.0f`).
    Matches are skipped with `continue` so they survive into the final
    waypoint list. The summary log line now reports both `removed` and
    `protected wall-parallel midpoints preserved` so BATs surface the
    protection action.

No other prune callers exist (`PruneCollinearWaypoints` is invoked only at
the bottom of `FunnelPath`), so the protection state is well-scoped. Fields
without wall-parallel portals see no change in behavior (counter stays 0,
prune runs unchanged). Fields with them (domt2_1 south exit, doopen2a, and
any other field where COLLAPSE was the right call in v0.16.1.2) regain the
doorway-threading aim point.

The v0.15.9.9 `cap=INT_MAX` AUTO-mode setting in `chase_battle_freeze.cpp`
is CORRECT and stays unchanged. It's the chase-catch verification cap; the
fact that it let a battle through this BAT is exactly the signal it was
designed to produce when the auto-pilot has a real hole. Now that the hole
is identified and the fix shipped, the next BAT should show 0 `[CBF] PASS`
lines on the entire chase.

**Expected BAT outcome.** Load a save before X-ATM092 chase, pick Auto, run
through to Lapin Beach FMV. Field log should show:
  - `[funnel] COLLAPSE wall-parallel portal ...` for `domt2_1` as before.
  - `[funnel-prune] removed N collinear waypoints (eps=50 units, M sweeps,
    1 protected wall-parallel midpoints preserved)` -- the new "1 protected"
    is the new diagnostic that confirms (-13,-1508) survived.
  - No `[CBF] PASS chase BATTLE` lines on any chase field.
  - Party reaches Lapin Beach (`disc00_07h.avi`) with 0 catches.

If a `[CBF] PASS` still fires, something else is wrong and we open a new
diagnostic build. If 0 catches but the chase still feels off, refine in
v0.17.8.19.2 and beyond.

**Modified files:**
  - `src/field_nav_pathfinding.inl` (s_protectedWaypointPos array, COLLAPSE
    recording, PruneCollinearWaypoints protection check, FunnelPath reset)
  - `src/ff8_accessibility.h` (version bump)
  - `CHANGELOG.md` (this entry)

## v0.17.8.18.1

Chapter 3: Scan-on-allies fix. BAT-confirmed 2026-05-28: scanning Squall in
a regular battle announced the full canonical description ("Uses a sword
called a gunblade. Special skill is Renzokuken, using the gunblade. Silent,
and a bit cold."). `[SCAN-CACHE]` log captured `monsterId=0x00 hasDesc=1`
for Squall, confirming the `+0xB3` lookup chain is universal across allies
and enemies. Zero-regression check passed: Bite Bug enemy scan worked
identically (`monsterId=0x2C hasDesc=1`, full description played).

Before the fix, casting Scan on any ally (regular party or Laguna dream
party) announced "No description available." Aaron confirmed FF8 ships
canonical Scan descriptions for ALL playable characters: Squall, Zell,
Irvine, Quistis, Rinoa, Selphie, Seifer, Edea, Laguna, Kiros, Ward. The
mod has been silently dropping the description on every ally Scan since
the feature shipped -- not just dream party.

**Root cause.** Two code sites in `src/scan_tts.cpp` enforced an `isAlly`
guard based on a wrong architectural assumption. The inline comment read:
"Allies don't have a meaningful entry (the byte at 0xB3 is whatever the
engine has loaded there -- usually 0 or stale data); the auto-announce omits
the description for ally targets." Both claims wrong. The game DOES have
descriptions for every playable character, and the engine reads them via
the same `+0xB3 -> monster_id -> SCAN_TEXT_POSITIONS -> SCAN_TEXT_DATA`
chain it uses for enemies. The in-game Scan UI renders them on screen --
proof that the lookup chain works for allies.

**Fix.** Lift both guards. The existing safety filters in
`ResolveDescriptionSafe` (`monsterId == 0xFF` sentinel, `pos == 0xFFFF`
sentinel, `pos >= 0x4000` garbage filter, empty-decode filter) already
handle any genuinely-invalid byte, so attempting the lookup for an ally
is no worse than v0.17.8.17.8 behavior in any failure mode. In the
success case (the norm per Aaron's confirmation), the description gets
announced.

  - `CaptureSnapshot` (around the monsterId / description read): removed
    `!snap.isAlly` from the lookup gate. Same `+0xB3` read, same
    `ResolveDescriptionSafe` call, applied unconditionally.
  - `BuildAutoAnnounce`: removed `!snap.isAlly` from the description-
    append check. `if (snap.hasDescription)` alone now gates the
    auto-announce description.
  - `SpeakField` case 2 (key `2` description query): collapsed the
    ally branch into the generic `snap.hasDescription ? description :
    "No description available."` path.
  - Architectural comment block at the top of the file rewritten to
    document the actual universal-lookup behavior.

**Zero-regression observation.** The `[SCAN-CACHE]` log line already prints
`monsterId=` and `hasDesc=` for the captured slot regardless of ally/enemy,
so the BAT log surfaces whether the ally lookup succeeded without any new
diagnostic. If a particular ally slot turns out to have a stale +0xB3 byte
(`hasDesc=0` in the log), the fallback message is the exact same
"No description available." as v0.17.8.17.8 -- no behavior worse than
shipped.

## v0.17.8.17.8

Chapter 2 cleanup. Closes the Laguna chapter by retiring the F12 Phase 1
diagnostic infrastructure. No behavior change for normal gameplay -- the dream
fixes from v0.17.8.17.1 .. .17.7 remain in place and validated.

**Removed:**
  - `src/battle_tts_laguna_diag.inl` (battle-mode diag: sub_47EAF0 / compStats /
    savemap.party dump). Stub left containing only a `git rm` notice.
  - `src/field_nav_laguna_diag.inl` (field-mode diag: Others/Background entity
    dump with player-detection signals). Stub left containing only a `git rm`
    notice.
  - `BattleTTS::LagunaDiag()` wrapper + declaration (battle_tts.cpp / .h).
  - `FieldNavigation::LagunaDiag()` wrapper + declaration (field_navigation.cpp
    / .h).
  - F12 dispatcher block in `src/dinput8.cpp` (game-mode routed between battle
    and field diag). The F12 key is once again reserved for the next session's
    diagnostic, with no active binding.
  - The two corresponding `#include` lines (battle_tts.cpp after victory.inl,
    field_navigation.cpp after directiondrive.inl).

**Kept (the useful artifacts from this chapter):**
  - `[DREAM-ID]` log line in battle_tts.cpp (one-shot dream-party detection).
  - `[CMD] charIdx` log line in BuildCharCommandList.
  - `[MenuTTS] party slot ... modelId=` in AnnounceMenuSummary.
  - `[JuncTTS] CharSelect ... modelId=` in AnnounceJuncCharSelect.
  These are cheap, fire only on already-logged paths, and document the
  observed dream-identity values for future reference.

## v0.17.8.17.7

Chapter 2 bug #8 dream-party names: Main Menu fix + a PROACTIVE AUDIT of every
character-naming site, so the dream-name issue stops surfacing one screen at a
time. The v0.17.8.17.6 Victory-screen fix validated; the Main Menu did not,
because the wrong names came from a different function than the one .17.6
patched -- which prompted auditing the whole codebase for the same pattern.

**The shared root cause (whole bundle).** A subsystem has a party FORMATION /
character index and names the character by indexing a name table with it.
During a Laguna dream the savemap formation holds the STALE regular party
(e.g. [5,0,1] = Selphie/Squall/Zell), so the NAME is wrong even though the
DATA is right (the engine loads the dream character's struct -- incl. model_id
at +0x08 = 8/9/10 -- into char-data[idx]).

**New shared resolver.** `ResolveDreamAwareCharId(charIdx)` (menu_tts_diagnostics.inl)
converts a formation index to the correct id: returns model_id when it names a
dream member (8/9/10), else the index (identical to normal play, where
model_id == idx -> zero regression). Feed its result to the existing
GetCharacterNameByPortrait()/id->name mappers. All new menu naming routes
through it.

**Menu sites fixed (the audit's hits):**
  - `AnnounceJuncCharSelect` (Junction character select) -- the actual source
    of the reported Main Menu bug; the M-key summary .17.6 patched was never
    invoked in that BAT. Now model_id-aware.
  - `AnnounceMenuSummary` (M-key summary) -- refactored from the .17.6 inline
    block to the shared resolver + GetCharacterNameByPortrait (also fixes the
    prior 8-name table ceiling).
  - `menu_tts_item.inl`: item Use-target announce (entry + cursor) and
    `GetPartyMemberName` -- named the target party member via the formation
    index; now dream-aware. HP/level reads stay keyed on the index (correct --
    dream data lives there).
  - `menu_tts_junction.inl` GF-list owner display ("GF on <name>") -- the
    FindGfOwner index is now mapped through model_id so a GF on a dream member
    reads Laguna/Kiros/Ward.

**Verified already dream-safe (no change needed):**
  - All in-battle ally naming -- turn announce, target selection, HP keys
    (1/2/3/H), command menu, victory EXP, and the Draw "drawer" -- routes
    through `GetBattleCharName` (compStats actor-kind +0x1C3) or
    `GetVictoryCharName`, both dream-aware. The battle `CHAR_NAMES[8]` table is
    only reached as the actor-kind fallback for regular characters.
  - Scan TTS -- operates on enemy data only; no party naming.
  - Battle status ailment TTS -- announces ailments, no character name table.
  - Menu help bar (/ key) and save-screen party list -- read the game's own
    rendered text (GCW) or save-file portrait IDs, not the live formation.

**Known follow-up (NOT fixed -- needs observation, not a guess):** the FIELD
entity catalog's `ResolveNameByModelId` (field_nav_names.inl) uses a separate,
partly-unconfirmed FIELD model-ID convention (field 8=Quistis-uniform, 9=
"Laguna?") that differs from the savemap convention. Naming dream party
followers in the field would need the dream field model IDs captured first;
guessing risks regressing the confirmed field models 0-8. Logged for a future
dream-field catalog observation.

Each fixed menu site logs its decode (`[JuncTTS] CharSelect ... modelId=`,
`[MenuTTS] party slot ... modelId=`) so a dream BAT confirms the model_id
assumption holds without a separate diagnostic.

## v0.17.8.17.6

Chapter 2 bug #8 dream-party NAMES on the Victory screen and Main Menu. During
a Laguna dream both announced the regular party (Selphie/Squall/Zell) instead
of Laguna/Kiros/Ward. These are separate subsystems from the in-battle command
menu, so the in-battle actor-kind name fix never reached them.

**Shared root cause.** Both read party member IDs from the savemap party
formation, which during a dream holds the STALE regular field formation (e.g.
`[05 00 01]`), and feed those indices into a name table. The character DATA
they display (EXP, HP, level) is already correct, because that is read from
`char-data[formation[slot]]`, which the engine has loaded with the dream
character's struct -- the same mechanism the v0.17.8.17.5 command fix relies
on. Only the NAME lookup used the wrong (stale-index) source.

**Victory screen fix.** The live dream identity is the battle `compStats`
actor-kind byte at `+0x1C3` (8=Laguna, 9=Kiros, 10=Ward) -- the same source
the validated in-battle name fix uses. It is snapshotted per ally slot every
frame while in battle (where it is validated valid) into `s_dreamSlotCharId[3]`
/ `s_isDreamBattle`, so it is reliably available to the victory thread at mode
4 (right after the battle, same battle module). A new `GetVictoryCharName(slot,
fallbackId)` helper returns the dream name when the slot's snapshot is 8-10 and
otherwise falls through to the existing id->name mapping. All seven spoken EXP
announce sites (Phase 1 all-same + grouped, in both the BTXT-hook and
thread-fallback paths, and the three Phase 2 level lines) now use it. Reset in
`OnBattleEnter`; no-op for normal battles (kinds 0-7 -> snapshot stays false).

**Main Menu fix.** `AnnounceMenuSummary` (M key) read the formation index from
savemap `+0xAF1` and named via `CHAR_NAMES[idx]`. It now reads the displayed
character's own `model_id` (+0x08 in the loaded char struct) and names
Laguna/Kiros/Ward when it is 8/9/10, falling back to the index table otherwise.
In normal play `model_id == idx` for the 8 main characters, so behavior is
unchanged outside dreams. A `[MenuTTS] party slot N: formIdx=.. modelId=.. ->
..` log line records the actual `model_id` so a dream BAT confirms the mapping
without a separate diagnostic.

The command menu fix (v0.17.8.17.5) remains validated; this build only adds the
Victory/Main-Menu name handling and does not touch the command path.

## v0.17.8.17.5

Chapter 2 bug #8 command menu, fixed properly. The Laguna dream command menu
now reads out every option correctly, sourced from the same savemap character
struct that the (already-working) Magic and GF submenus use, so it tracks the
player's junctions live.

**Root cause.** v0.17.8.17.2 abandoned the normal savemap `commands[3]` path
for dream characters and special-cased them by parsing the in-battle compStats
command table at `compStats[slot]+0x1C`. That table interleaves hidden/disabled
entries among the visible ones and has no reliable end marker, so the parser
ran past the real list into adjacent struct bytes that happened to decode as a
valid command -- producing the "4th command reads Magic" bug and failing to
track a Draw->Item re-junction.

**Why the savemap path is correct for dreams.** During a Laguna dream the
engine loads the dream party's data into the regular character-data array, and
`SAVEMAP_PARTY_FORMATION[slot]` indexes the active dream character's struct
within it. `BuildMagicList` (+0x10) and `BuildGFList` (+0x58) already rely on
this and are validated working in dreams. `commands[3]` lives at +0x50 in that
same struct, stored in the mod's ability encoding (0x14 Magic, 0x15 GF, 0x16
Draw, 0x17 Item) -- exactly what `GetCommandName` expects. The v0.17.8.17.4
battle log confirms it: the `[LIMIT-DIAG]` dump for Laguna's turn read +0x50 =
`14 15 17` (Magic, GF, Item) -- matching her on-screen menu and reflecting the
live Draw->Item swap -- and its magics region was byte-identical to the magic
list the mod read out, proving it is the same struct the command path reads.

`BuildCharCommandList` now uses the ordinary savemap `commands[3]` path for all
characters (the dream-specific compStats parser is removed). The actor-kind
name override in `GetBattleCharName` is unchanged -- names still need it,
because the char struct here does not carry the dream display name; commands
and names are independent lookups.

The F12 `[LAGU-CMD]`/`[LAGU-DIAG]` diagnostic stays in for this validation
build; it is removed in the v0.17.8.17.6 cleanup.

**Known separate issue (not addressed here):** the Victory screen and Main
Menu still show the regular party names (Squall/Zell/Selphie) during dreams.
Those are different text subsystems (tkbtl victory render / tkmenu) that the
in-battle name fix never covered; tracked as its own follow-up.

**Modified files:**
  - `src/battle_tts_menu_helpers.inl` (dream command list now uses savemap path)
  - `src/ff8_accessibility.h` (version bump)
  - `CHANGELOG.md` (this entry)

## v0.17.8.17.4

Chapter 2 bug #8: the Laguna dream battle command menu now reads out every
option, sourced from the engine's LIVE per-character command list so it
reflects whatever the player has junctioned (the dream party is fully
junctionable, so the command set is dynamic and must never be hard-coded).

**What broke.** v0.17.8.17.2 left `s_turnCharCommands` at `[Attack, 0, 0, 0]`
for dream characters, so the cursor announcer in `battle_tts_menu_poll.inl`
(which speaks `GetCommandName(s_turnCharCommands[cursor])`) read 0x00 for
cursors 1-3 and said nothing.

**The source.** Decoded from the v0.17.8.17.3 `[LAGU-CMD]` dump,
cross-referenced against an F11 screenshot of Ward's menu:
  - The live command list sits at `compStats[slot] + 0x1C`, 4-byte entries
    `{ u16 param, u8 cmdId, u8 flags }`. The engine's own command-window
    pointer at `0x01D76834` references this region.
  - `cmdId` is battle-command encoding; the ability id `GetCommandName`
    expects is `cmdId + 0x12` (Magic 0x02->0x14, GF 0x03->0x15,
    Draw 0x04->0x16, ...). `cmdId 0x01` is Attack (slot 0).
  - `flags` bit `0x02` set means the command is present but hidden/disabled
    (not shown in the player menu).
  - The list ends at `cmdId 0x00`, or at the first entry whose `cmdId+0x12`
    isn't a known command (i.e. we've walked into adjacent struct data).
  Ward decodes to `[Attack, Magic, GF, Draw]` exactly matching the
  screenshot, with Card (0x06) and LV Up (0x16) present-but-hidden.

`BuildCharCommandList` now walks that live list for dream characters
(actor-kind 8/9/10), collecting up to three shown, non-Attack commands into
slots 1-3. Regular battles (actor-kind 0-7) keep the unchanged savemap path,
so normal play is unaffected. The decoded result is logged per turn as
`[CMD] dream-party slot N ... cmds=[Attack ...]` for BAT verification.

The F12 `[LAGU-CMD]`/`[LAGU-DIAG]` diagnostic stays in for this validation
build; it is removed in the v0.17.8.17.5 cleanup.

**Modified files:**
  - `src/battle_tts_menu_helpers.inl` (live command-list parser for dream party)
  - `src/ff8_accessibility.h` (version bump)
  - `CHANGELOG.md` (this entry)

## v0.17.8.17.3

Chapter 2 bug #8 follow-up: command-menu source diagnostic. The
v0.17.8.17.2 BAT confirmed the dream party is now correctly NAMED
(Laguna/Kiros/Ward) and field navigation works, but surfaced a
secondary issue: while moving through the battle command menu during a
Laguna dream battle, most command options don't read out.

**Cause.** The command-menu cursor announcer in `battle_tts_menu_poll.inl`
reads `s_turnCharCommands[cursor]` and speaks `GetCommandName(...)`.
v0.17.8.17.2's `BuildCharCommandList` returns early for dream characters
(actor-kind 8/9/10), leaving `s_turnCharCommands = [Attack, 0, 0, 0]`.
So cursor 0 says "Attack" but cursors 1-3 read `0x00` and announce
nothing useful. The earlier assumption that the live cursor announcer
would cover the rest was wrong -- that announcer IS this code, and it
depends on `s_turnCharCommands` being populated.

**Why a diagnostic and not a fix.** The dream party's command list isn't
in the savemap (the savemap character struct only covers the 8 permanent
characters, IDs 0-7), and none of the compStats offsets sampled by the
v0.17.8.17.1 `[LAGU-DIAG]` dump contained the command IDs. Hard-coding
the commands is fragile -- FF8 has several Laguna dream sequences that
may use different command sets. So this build extends the F12 battle
diagnostic to locate the engine's runtime command source precisely:
  - active char id + command cursor + current `s_turnCharCommands[]`
  - FULL compStats slab (0x000..0x1CF) for each ally slot
  - battle menu region 0x01D76800..0x01D768FF (the structure the command
    cursor at 0x01D76843 indexes)
All under the `[LAGU-CMD]` tag, appended to the existing `[LAGU-DIAG]`
dump. v0.17.8.17.4 will read the located source and populate
`s_turnCharCommands` for dream characters.

The v0.17.8.17.2 name fix is unchanged and still in effect.

**Modified files:**
  - `src/battle_tts_laguna_diag.inl` (command-menu source hunt added to the battle diagnostic)
  - `src/ff8_accessibility.h` (version bump)
  - `CHANGELOG.md` (this entry)

## v0.17.8.17.2

Chapter 2 bug #8 fix: Laguna dream battles now announce the correct party
(Laguna/Kiros/Ward) instead of the regular party (Squall/Zell/Selphie).

**Root cause (confirmed by the v0.17.8.17.1 BAT `[LAGU-DIAG]` block).**
`GetBattleCharName` and `BuildCharCommandList` in
`battle_tts_menu_helpers.inl` both read the savemap party formation at
`SAVEMAP_PARTY_FORMATION` (`0x1CFE74C`). During a dream battle that array
is stale -- it still holds the regular field party. The BAT dump showed
`savemap.party = [05 00 01]` (Selphie/Squall/Zell) while the live battle
slots were Ward/Laguna/Kiros. So the mod announced "Zell's turn" for a
slot that was actually Kiros, etc. The live, correct identity is the
compStats actor-kind byte at `+0x1C3`: the BAT showed slot0=10 (Ward),
slot1=8 (Laguna), slot2=9 (Kiros), matching `sub_47EAF0`'s decoded name
strings exactly.

**Fix.** Both functions now read `compStats[slot]:0x1C3` first. When the
actor-kind is 8, 9, or 10 (Laguna/Kiros/Ward -- IDs that appear ONLY in
dream sequences), the function uses the dream identity directly:
  - `GetBattleCharName` returns "Laguna"/"Kiros"/"Ward".
  - `BuildCharCommandList` leaves the command list at just "Attack"
    (the dream party has no savemap character-data entry to source
    equipped commands from; the live cursor-navigation announcer covers
    the rest of the menu as the player moves through it).
For any other actor-kind (the 8 permanent characters, IDs 0-7) both
functions fall through to the original savemap path completely unchanged,
so normal battles have zero behavior change. The gate is safe because
actor-kinds 8/9/10 are exclusively the dream characters -- a regular
battle never produces them.

This closes Chapter 2's bug #8. Combined with the v0.17.8.17.1 bug #7 fix
(field navigation on Laguna fields) the Laguna bundle is functionally
complete. The F12 Laguna diagnostic (`battle_tts_laguna_diag.inl` /
`field_nav_laguna_diag.inl` and their wiring) is left in place for this
validation BAT and should be removed in a cleanup pass before the
Chapter 2 squash-push.

**Modified files:**
  - `src/battle_tts_menu_helpers.inl` (`GetBattleCharName` + `BuildCharCommandList` dream-party gate)
  - `src/ff8_accessibility.h` (version bump)
  - `CHANGELOG.md` (this entry)

## v0.17.8.17.1

Chapter 2 (Laguna bundle) Phase 1+2 stacked patch. The v0.17.8.17 BAT
revealed two bugs in v0.17.8.17 itself plus confirmed the bug #7 root
cause, so this build folds the diagnostic dispatch fix together with
the bug #7 fix. The bug #8 diagnostic stays in -- the same Laguna BAT
that validates bug #7 will capture the still-missing battle-side data
for the v0.17.8.17.2 bug #8 fix.

**Fix 1: F12 dispatch (`src/dinput8.cpp`).** The v0.17.8.17 dispatcher
branched on `mode == 999`, expecting that to be the active-battle mode.
The v0.17.8.17 BAT proved otherwise: during a battle on `gwgrass1` with
continuous battle-engine activity in `ff8_battle.log` (turn announces,
ATB caps, GF-HOOK, EWM-DIAG), the F12 press at 20:28:13 routed to the
*field* diagnostic, not the battle diagnostic -- because `*pGameMode`
at that moment was 3, not 999. `BattleTTS::Update` itself checks
`mode == 3` for battle detection, and that path works in normal
gameplay (HP tracking, turn announces, etc. all fire correctly). The
`MODE_BATTLE = 999` enum in `ff8_addresses.h` is mislabeled or used
for a different layer; the active-battle game-mode value is 3 (what
the enum calls `MODE_SWIRL`, which is actually held the entire battle,
not just the entry animation). The dispatcher now uses `mode == 3`
and the battle diagnostic will fire when F12 is pressed during a
battle.

**Fix 2: Player detection on Laguna fields (`src/field_nav_fieldscripts.inl`).**
The at-field-load player-detection loop in `HookedFieldScriptsInit`
checked `setpc == 0`, expecting that to identify Squall. The
v0.17.8.17 BAT `[LAGU-FLD]` block on `gwgrass1` proved `setpc` is
the *character ID*, not a boolean: ent0 had `setpc=8` (Laguna), ent1
`setpc=9` (Kiros), ent2 `setpc=10` (Ward), ent3/4 `setpc=254` (NPC
sentinel). On regular fields Squall has `setpc=0` and the old check
worked by accident -- ID 0 is one valid character ID among 11. The
fix accepts any `setpc < 11` (covering all 11 playable characters:
Squall=0 through Ward=10) and rejects the NPC sentinel 254. Regular
fields still work because Squall=0 satisfies the bound; Laguna fields
now work because ent0=8 also satisfies it.

**Diagnostic stays in.** The `[LAGU-DIAG]` / `[LAGU-FLD]` blocks from
v0.17.8.17 are unchanged. Re-running the Laguna BAT with this build
should: (1) demonstrate field navigation works correctly on
`gwgrass1` (bug #7 fix validation), and (2) capture the battle-side
dump in `ff8_battle.log` when F12 is pressed during the dream
battle (bug #8 data collection that v0.17.8.17 missed). The next
build (v0.17.8.17.2) ships the bug #8 fix from that data.

**Modified files:**
  - `src/dinput8.cpp` (dispatch fix: `mode == 999` -> `mode == 3`)
  - `src/field_nav_fieldscripts.inl` (player detection: `setpc == 0` -> `setpc < 11`)
  - `src/ff8_accessibility.h` (version bump)
  - `CHANGELOG.md` (this entry)

## v0.17.8.17

Chapter 2 (Laguna bundle, bugs #7 + #8) Phase 1 diagnostic build. No fixes
in this build -- data collection only. F12 is the trigger; it dispatches
by game mode:

  - In battle (mode 999) -> `BattleTTS::LagunaDiag()` writes a one-shot
    `[LAGU-DIAG]` block to `ff8_battle.log`.
  - Anywhere else -> `FieldNavigation::LagunaDiag()` writes a one-shot
    `[LAGU-FLD]` block to `ff8_field.log`.

The previous F12 binding (`DialogInject::Phase1_TestMes` /
`Phase2_TestAsk` from v0.17.7.x) is removed per the F12 diagnostic-key
rule. The DialogInject test functions themselves remain in source --
they just aren't keyboard-triggered anymore.

**Battle dump (bug #8).** For each ally slot 0..2:
  - Calls `sub_47EAF0(slot)` (the engine's actor-name function) and logs
    both the raw pointer and the FF8-decoded string. Disassembly of
    `sub_47EAF0` shows it reads `compStats[slot]:0x1C3` (an "actor kind"
    byte), then either returns a fallback string for kind 0/4 or indexes
    a 36-byte-stride table at `0x1CF75EC` to compute the final string
    pointer. If this returns Laguna/Kiros/Ward during a dream battle, the
    v0.17.8.17.1 fix is to route `GetBattleCharName` through this function
    instead of the savemap-side party-formation read.
  - Logs `compStats[slot]:0x1C3` explicitly (actor-kind byte).
  - Hex-dumps `compStats[slot][0x00..0x1F]` (early common fields),
    `[0x40..0x5F]` (savemap mirrors equipped commands at +0x50; if
    compStats follows the same layout the dream-party command IDs land
    here), and `[0x1B0..0x1CF]` (context around the actor-kind byte).
  - Logs HP from compStats (+0x172/+0x174) vs the entity array as a
    sanity-check that the slab is the correct slot.
  - Logs `savemap.party[0..3]` at `0x1CFE74C` so we can confirm the
    savemap-side formation still holds the regular party during a dream
    sequence (the root cause of `GetBattleCharName` returning wrong
    names from `battle_tts_menu_helpers.inl`).

**Field dump (bug #7).** Logs the current `fieldId` then iterates every
entity in the field's Others array (`pFieldStateOthers`,
`pFieldStateOtherCount`, stride `0x264`) and Background array
(`pFieldStateBackgrounds`, stride `0x1B4`). Per entity logs the signals
the player-detection heuristic depends on: `model_id` (+0x218),
`triangle_id` (+0x1FA), `setpc` (+0x255), `talkonoff` (+0x24B),
`pushonoff` (+0x249), `throughonoff` (+0x24C), `execFlags` (+0x160),
field position (+0x190/0x198) and simulated position (+0x20/0x28). On
gwgrass1 the dump should reveal whether the player entity has
`setpc != 0` (matching the current heuristic) or a different discriminator
(triggering bug #7).

**Pre-build research (Phase 0) carried in this commit.** Disassembly of
`sub_47EAF0`. Confirmation that `SAVEMAP_PARTY_FORMATION` (`0x1CFE74C`)
is the savemap-side regular party, separate from `party_other[4]` later
in the savemap struct (FFNx `save_data.h`). Identification of
`savemap_ff8_battle.special_flags` bit 0 = `dream` discriminator.
Review of `battle_tts_menu_helpers.inl::GetBattleCharName` and
`BuildCharCommandList`, both of which currently read
`SAVEMAP_PARTY_FORMATION` and gate on `charIdx < 8` -- the dual mechanism
behind bug #8 (wrong source + missing IDs 8/9/10 for Laguna/Kiros/Ward).

**New source files (textual includes; no `deploy.bat` change required):**
  - `src/battle_tts_laguna_diag.inl`
  - `src/field_nav_laguna_diag.inl`

**Modified files:**
  - `src/battle_tts.cpp` (include + public `LagunaDiag()` wrapper)
  - `src/battle_tts.h` (public declaration)
  - `src/field_navigation.cpp` (include + public `LagunaDiag()` wrapper)
  - `src/field_navigation.h` (public declaration)
  - `src/dinput8.cpp` (F12 dispatcher; old DialogInject binding removed)
  - `src/ff8_accessibility.h` (version bump)
  - `CHANGELOG.md` (this entry)

## v0.17.8.16.1

Audio descriptions: rewrite the Quistis-infirmary FMV (`disc00_01h.vtt`)
based on frame-verified scene content. The original AD was wrong on both
identity and action.

**What the original AD said.** Four cues that described "Squall's perspective
as he leaves the infirmary," the room itself, "Dr. Kadowaki stands in the
infirmary in her instructor uniform," and a close-up of "Dr. Kadowaki --
auburn hair, glasses, speaking." Both character attribution and the
direction of the action were wrong.

**What the FMV actually shows.** Frame extraction via `ffmpeg` (27 frames
at 0.5 s intervals, 1280x896 VP8 source) confirms the scene is Quistis
arriving at the infirmary to collect Squall after his training injury.
Sequence: angular doorway close-up as she steps through; close-ups of her
face and the SeeD instructor's uniform (navy blazer with red trim and
gold piping); wide shot of the teal infirmary; medium close-up of Quistis;
then a critical reverse to Squall's POV from the bed (his boot/leg in
foreground, Quistis standing across the room watching him); close-up of
Quistis as her eyes lower and close, a soft sigh of patient exasperation,
then eyes open again, composed. No Dr. Kadowaki at any point in this FMV.
Squall is in the bed throughout, not leaving.

**Why the original AD was wrong.** The auburn hair plus glasses on a
character in an "instructor uniform" was misattributed to Dr. Kadowaki
(who is the infirmary doctor but does not wear an instructor's uniform).
The instructor's uniform actually belongs to Quistis -- she is a SeeD
instructor. The original AD also flipped the direction of the scene: it
framed Squall as leaving the infirmary when in fact he is the patient in
the bed being collected.

**New cues (29 words across 13.5 s, ~2.15 wps -- well under the project's
2.5 wps TTS pacing ceiling).** Each cue stays under the 7-word/3-sec rule
from `FMV_SCENE_REFERENCE.md`:

  - `00:00.000 --> 00:03.500` -- "Quistis enters the infirmary, instructor's uniform, glasses."
  - `00:03.500 --> 00:07.000` -- "Teal room -- desk, retro monitor, eye chart."
  - `00:07.000 --> 00:10.000` -- "From Squall's bed, Quistis watches him."
  - `00:10.000 --> 00:13.500` -- "Eyes closed, she sighs softly -- gently exasperated."

The `FMV_SCENE_REFERENCE.md` entry for `disc00_01h` is also corrected to
match the frame-verified content, so future AD authors don't repeat the
misidentification.

**Why this is a content-only patch on top of v0.17.8.16.** The v0.17.8.16
engine cue-clock fix BAT-confirmed that the AD now plays in sync with FMV
playback. That sync revealed Aaron could now actually evaluate the AD
content -- previously the 22-second pre-roll obscured how wrong it was.
The VTT files are embedded into `dinput8.dll` via Win32 resources
(`resources.rc`), so the rebuild picks up the new content automatically;
no source-code changes in this patch.

## v0.17.8.16

FMV audio descriptions: fix the 22-second pre-roll on the Quistis infirmary
FMV (Fire Cavern bug list bug #1). Cue clock is now driven by engine
playback state, not by AVI file-handle open/close.

**Symptom Aaron reported (Fire Cavern playthrough).** The audio description
for Quistis's Infirmary FMV started speaking the scene about 22 seconds
BEFORE the engine actually began playback of the FMV. All cues were
offset by the same gap, so the AD narrative was permanently ~22s ahead
of what was happening visually/audibly in the game.

**Root cause.** `FmvAudioDesc::OnFrame` was starting the cue timer the
moment `FmvSkip::GetCurrentAviName()` returned a non-empty new AVI name.
That name becomes available as soon as the AVI file handle is opened via
`CreateFileA`/`CreateFileW` -- but the engine can open the AVI handle
long before it actually begins playback. On the infirmary FMV that gap
is 22 seconds. Cue startTimes are absolute offsets within the FMV
(WebVTT format), so a cue at startTime=0 fired immediately on handle
open, then a cue at startTime=5 fired five seconds later (still pre-
playback), and so on -- the entire AD ran end-to-end before the engine
ever started rendering the video.

The disc04 (Square logo) intro hid the bug in normal play: the handle-
open and engine-start are milliseconds apart there, so the offset isn't
noticeable. The infirmary scene exposes it because the engine pre-loads
the AVI and waits on some other event (likely the surrounding dialog /
fade-in sequence) before actually playing.

**Fix (`fmv_audio_desc.cpp`).** Replace the wall-clock-from-StartPlayback
elapsed time with a cue-clock accumulator (`g_engineActiveSeconds`) that
only advances on frames where the engine reports it is playing. The
source of truth for engine-playing state is `FF8Addresses::IsMoviePlaying()`,
which reads `movie_object + 0x4C4A8` (the engine's own `movie_is_playing`
flag) -- the exact same address `FmvSkip` cross-references in its
`Game movie state changed: PLAYING` log line. Two changes wire it up:

  1. **Gate StartPlayback on engine-playing.** In `OnFrame`, the
     "new AVI detected" block now only enters StartPlayback if the
     engine is actually playing. If the engine hasn't yet begun, log
     `[FMV_AD] AVI handle open: <name> -- waiting for engine playback`
     once and defer. When the engine starts playing on a subsequent
     frame, the same condition triggers and StartPlayback runs.
  2. **Accumulator-based elapsed time.** In the cue-firing section,
     compute a per-frame delta from `QueryPerformanceCounter` and add
     it to `g_engineActiveSeconds` only when the engine is currently
     playing. If the engine pauses mid-FMV (the original "STOP/PLAY"
     scenario from the v0.16.5.2 BAT report), the accumulator freezes;
     when it resumes, the accumulator picks up where it left off. Edge
     transitions in either direction log once so the BAT trace shows
     when pauses happen. `GetElapsedSeconds()` now returns the
     accumulator instead of wall-clock-since-g_startTime; cue firing
     compares cue startTimes against it directly.

The wall-clock `g_startTime` is still recorded in StartPlayback for
legacy compatibility with any code path that might read it, but it no
longer drives cue timing. Reset paths in `StartPlayback`/`StopPlayback`
bring the accumulator and edge-tracking flags back to a clean baseline
so the next FMV starts from zero.

**Expected behaviour on next BAT (infirmary FMV).** Aaron triggers the
Quistis infirmary scene. `ff8_mod.log` should show:

  - `[FMV_AD] AVI handle open: <infirmary AVI name> -- waiting for engine playback`
    (one line, at the moment the file handle opens)
  - ~22 seconds of no further `[FMV_AD]` activity
  - `[FMV_AD] AVI detected via FmvSkip: <name> (engine confirmed playing)`
  - `[FMV_AD] Matched ... -> ...`
  - `[FMV_AD] Started playback: ...`
  - Cues fire in step with what Aaron hears in the FMV.

Also unblocks the broader "STOP/PLAY" pause/resume pattern on any other
FMV where the engine pauses playback mid-stream: the accumulator handles
that case for free.

## v0.17.8.15.1

Field navigation: two follow-on fixes from the v0.17.8.15 BAT, both in the
label + announce path for the new JSM-behavior-signal NPC relabel.

**Symptom Aaron heard:** on bghall_3, kanban2 announced as `"NPC 2, 1 of 0"`.
The NPC relabel had worked (Xu is now correctly typed NPC, not Interaction)
but the number was wrong ("NPC 2" with no "NPC 1" anywhere) and the suffix
was nonsensical ("1 of 0").

**Bug A -- dedupe counter inflated by friendly-named NPCs.** The v0.17.8.15
relabel counter looped `if (newCatalog[c].type == ENT_NPC) n++` to pick the
next sequential number. But friendly-named NPCs like Cid and Quistis are ALSO
typed `ENT_NPC` -- they're just announced by their friendly name, not as
"NPC N". On bghall_3 the catalog already had at least one such friendly-named
ENT_NPC entry (one of cat0-cat4, runtime entity surviving party-filter), so
the count started at 1 and kanban2 became "NPC 2" with no "NPC 1" ever
heard. Fix: count only entries whose name already matches the `"NPC %d"`
prefix (the generic relabel sequence). Friendly-named NPCs are excluded;
the first raw-SYM NPC relabel is `"NPC 1"`. (`field_nav_catalog_dedupe.inl`)

**Bug B -- announce sameType test missed JSM-injected NPCs and Interactions.**
In `AnnounceCurrentTarget()` the `typeNum`/`typeTotal` cascades had:
```
else if (strcmp(typeLabel, "NPC") == 0 && ce.entityIdx >= 0 && ...)
```
This was the legacy heuristic from before type classification was reliable:
"any runtime entity (entityIdx >= 0) is an NPC". But JSM-injected NPC
relabels (entityIdx <= -300) failed this test, so kanban2 didn't count
itself in either typeNum or typeTotal. `if (typeNum == 0) typeNum = 1`
defaulted it to 1, typeTotal stayed 0, label became "NPC 2 1 of 0". The
same pre-existing bug affected `ENT_INTERACTION` -- there was NO typeLabel
branch for it at all, and no sameType clause matched it, so JSM-injected
Interaction relabels also got "1 of 0" (this was the watch-list item flagged
from v0.17.8.13/.14's `"Interaction 3 1 of 0"`). Three fixes in
`field_nav_announce.inl`:

  1. Add `else if (catEnt.type == ENT_INTERACTION) typeLabel = "Interaction";`
     to the typeLabel cascade so JSM-injected interactions get a proper
     typeLabel instead of defaulting to "Entity".
  2. Update the NPC sameType clause in BOTH the typeNum and typeTotal loops:
     `ce.entityIdx >= 0` becomes `(ce.entityIdx >= 0) || (ce.type == ENT_NPC
     || ce.type == ENT_BG_NPC)`. Legacy runtime path preserved; new
     type-based clause catches JSM-injected NPCs. Disjoint conditions, no
     double-counting.
  3. Add Interaction sameType clause to both loops: `strcmp(typeLabel,
     "Interaction") == 0 && ce.type == ENT_INTERACTION`. Generic
     "Interaction N" relabels now self-count.

**Expected post-fix announce on bghall_3:**
  - kanban2 (cat6): `"NPC 1 1 of 1"` (only raw-SYM NPC; runtime ENT_NPCs are
    announced by their friendly name with their own count).
  - line3 / line4: `"Interaction 1 1 of 2"` / `"Interaction 2 2 of 2"`
    (unchanged for trigger-line interactions -- their typeLabel was already
    `"Event"` and that path is untouched; this fix only adds new coverage,
    doesn't change existing paths).
  - Friendly-named runtime ENT_NPCs (if any in the catalog): now counted in
    the same group as kanban2 for typeTotal. E.g. if Cid is in the catalog,
    Cid announces as `"Cid 1 of 2"` and kanban2 as `"NPC 1 2 of 2"`.

**Regression safety.** The dedupe counter change is local to the NPC branch
and affects only the numbering of the new `"NPC N"` label sequence; the
Interaction fall-through branch's counter is unchanged (it was already
correct -- trigger lines ARE named `"Interaction N"` so type-counting
works for them). The announce changes ADD matching clauses without removing
any existing ones; legacy runtime-NPC counting is preserved verbatim.

## v0.17.8.15

Field navigation: full revert of the v0.17.8.11 - v0.17.8.14 chara.one
cross-reference chain + clean replacement using JSM behavior signals.

**Why the revert.** Aaron took a screenshot of bghall_3 at the spot the
v0.17.8.14 BAT had concluded was a signpost (kanban2, the only remaining
"Interaction 3" entry on B-Garden Hall 6). The screenshot showed Xu visibly
standing there as a character model in front of Squall, with the dialog
box reading `Xu "Hey, Squall, heard you got your first mission already!"`.
There is no signpost. The internal SYM name "kanban2" is just a misleading
leftover identifier in the field data.

This disproved the v0.17.8.13 BAT's central conclusion ("kanban2 IS a
sign"). It also disproved the deeper assumption underneath v0.17.8.11-.14:
that the chara.one classifier could distinguish NPCs from props by reading
the model archive header. The classifier was returning `isChar=0` for
kanban2's model slot (p048), but p048 IS Xu's character model on this
field. The classifier was wrong, and -- more fundamentally -- the file-
level "is this a character model" question was the wrong mechanism
entirely. What matters for the player is the entity's gameplay role: does
the player walk up to it and press Confirm (NPC), or walk across it (line
trigger / interaction)?

**The clean fix.** That gameplay distinction is already in the data we
scan, no new mechanism needed:

  - Line entities (`jsmCategory == 1`) are walk-across triggers (signposts,
    beds, save lines, screen boundaries). They surface as TRIGGER entries
    in the catalog and were already labeled "Interaction N".
  - Other-category entities (`jsmCategory == 3`) with a SETMODEL opcode
    in their init method are by construction "someone standing somewhere":
    the entity loads a 3D model at field load and the player walks up to
    interact. That's an NPC, regardless of whether the model file is a
    'd'-prefix or 'p'-prefix.
  - Other-category entities without SETMODEL-init (script-only Directors,
    invisible dispatchers) stay as "Interaction N".

Replaces the `info.setmodelSlot` (int) field on `JSMEntityInfo` with
`info.hasSetmodelInit` (bool). Populated directly from the existing
`foundSetmodelInit` local variable in the JSM scanner (no new opcode
parsing). Used in `field_nav_catalog_dedupe.inl`: when a raw-SYM JSM-
injected ENT_OBJECT is being relabeled, branch on `jsmCategory == 3 &&
hasSetmodelInit` to choose between "NPC N" (set type ENT_NPC) and
"Interaction N" (set type ENT_INTERACTION).

**Per Aaron's directive, NPC labels never expose SYM names.** SYM names
are unreliable internal identifiers; the catalog only ever announces
generic "NPC N" plus the existing " X of Y" suffix the announce code
appends based on per-type counts. Friendly names (Cid, Quistis) and named
specials (Save Point, Draw Point, Shop, Card Game) continue to be applied
via their own paths and are not affected.

**What got reverted, file by file.**

  - `src/field_charaone_parse.h` / `.cpp` -- stubbed to comment-only
    placeholders explaining their removal. No longer compiled (dropped
    from `src/deploy.bat`). Files retained as stubs so the next
    housekeeping pass can delete them outright; nothing references them
    any more.
  - `src/ff8_addresses.h` -- removed the v0.17.8.11 chara.one block:
    `load_field_models_addr`, `chara_one_set_data_start_addr`,
    `pCharaOneDataStart`, `chara_one_read_file_addr` and the
    `HasCharaOneDataStart()` / `HasCharaOneReadFile()` inlines.
  - `src/ff8_addresses.cpp` -- removed the four corresponding variable
    definitions and the chara.one resolution `__try` block inside
    `Resolve()` (the `read_field_data` + 0xF0F / +0x15F / +0xAFF chain
    and the SEH fallback path).
  - `src/dinput8.cpp` -- removed `#include "field_charaone_parse.h"` and
    the two `FieldCharaOneParse::Initialize()` / `Shutdown()` calls in
    `AccessibilityThread`. Replaced with v0.17.8.15 explainer comments.
  - `src/field_navigation.cpp` -- removed the chara.one include there too.
  - `src/deploy.bat` -- removed `field_charaone_parse.cpp` from the
    compile list.
  - `src/field_archive.h` -- replaced `int setmodelSlot;` on
    `JSMEntityInfo` with `bool hasSetmodelInit;`.
  - `src/field_archive_jsm_scan.inl` -- removed the
    `int setmodelSlotInit = -1;` local, the v0.17.8.14 inline-opcParam
    capture block inside the `JSM_OP_SETMODEL && m == 0` branch (kept
    `foundSetmodelInit = true` -- v0.12.20, pre-existing), and replaced
    `info.setmodelSlot = setmodelSlotInit` with
    `info.hasSetmodelInit = foundSetmodelInit`. The persistent
    `s_hasSetmodelInit[]` array used by the Director-detection post-pass
    is unchanged; this just adds an export to the per-entity struct.
  - `src/field_nav_catalog_dedupe.inl` -- removed the v0.17.8.11 chara.one
    NPC-override block and the v0.17.8.12 `[NPC-skip]` diagnostic, and
    inserted the new behavior-signal block above (jsmCategory + SETMODEL).

**BAT expectation on bghall_3.**

  - line3 -> Interaction 1 (unchanged)
  - line4 -> Interaction 2 (unchanged)
  - kanban2 -> NPC 1 (was: Interaction 3)
  - F9 nav-cycle says "NPC 1, 1 of 1" (or whatever the type-counted suffix
    becomes) at kanban2's position
  - No `[NPC-skip]` lines anywhere (diagnostic removed with v0.17.8.12)
  - No `[JSMScan] SETMODEL-init` lines anywhere (removed in v0.17.8.13)
  - No chara.one parse / hook log lines (the entire module is gone)

Open pre-existing issue not addressed here: in the v0.17.8.13/.14 BAT,
the announce code reported `"Interaction 3 1 of 0"` for the JSM-injected
entry -- the "of 0" suffix is wrong. After v0.17.8.15 the kanban2 entry
should speak as "NPC 1, 1 of 1" but if the same counter bug appears (e.g.
"NPC 1, 1 of 0"), that's the next thing to investigate in field_navigation.cpp's
announce path. Flagged for follow-up only if observed.

## v0.17.8.14

Field navigation: bug-#10 mechanism fix. v0.17.8.13's SETMODEL-init
diagnostic resolved the long-standing question of how SETMODEL encodes
its chara.one slot index: **inline in the opcode word's low 24 bits,
not on the script VM stack.** The BAT log showed `pushCount=0
stk=(empty)` for every single SETMODEL-init firing across four fields
(bgryo2_1 12 slots, bgroad_5 16 slots, bghall_5 17 slots, bghall_3 18
slots), with `opcParam` matching the chara.one slot ordering 1:1 in
every case. The v0.17.8.11 stack-based capture was looking in the
wrong place and silently failed for every entity.

Fix: read `opcParam` instead of `pushStack[pushCount-1]` (same
range/sentinel guards: `setmodelSlotInit < 0 && opcParam >= 0 &&
opcParam < 64`). The v0.17.8.13 diagnostic block is removed in the
same commit, so the file's net change is a small reduction. Two
distinct entities can share a slot (e.g. bghall_5 ent25 'l2' and
ent26 'l3' both `opcParam=0x0A`); the existing `setmodelSlotInit < 0`
gate means the first SETMODEL-init in init wins per-entity, which is
the correct semantics.

Downstream effect: `info.setmodelSlot` is now populated correctly for
every entity that has a SETMODEL in its init method. The dedupe NPC
override from v0.17.8.11 ("if the SYM-named object loads a character
model slot, label it NPC") finally has data to act on. The
v0.17.8.12 `[NPC-skip]` diagnostic in the fallthrough path stays in
place for one more BAT cycle so the next session can see steady-state
behavior; if quiet on success, it will be removed in v0.17.8.15.

**Important user-facing caveat:** The v0.17.8.13 BAT also revealed
that kanban2 on bghall_3 -- the entity Aaron experiences as Xu --
loads chara.one slot 13 = `p048`, which the classifier correctly
identifies as a PROP (signpost). So after this build, kanban2 will
have `setmodelSlot=13`, `isChar=0`, and the NPC override correctly
won't fire. kanban2 will continue to label as "Interaction 3" -- the
mechanism is now correct, but kanban2 itself is genuinely a bulletin
board. Xu (`ent13 'shu'`, loading character slot 1 = d000) is a
separate character entity whose dialog is summoned by story script
when the player examines kanban2. Changing kanban2's label requires a
different approach (REQ-chain analysis, or prop-name labeling) and is
queued for a future build.

**BAT expectation:** the previously-spammy `[JSMScan] SETMODEL-init
...` lines are gone (diagnostic removed). The `[NPC-skip]
'kanban2': fid=170 setmodelSlot=13 fieldParsed=1 isChar=0` line now
shows the captured slot number (was -1). On other fields where a
raw-SYM character entity is positioned and visible, the dedupe path
should now relabel it to NPC via the v0.17.8.11 path. kanban2 itself
stays "Interaction 3".

## v0.17.8.13

Field navigation: bug-#10 diagnostic. v0.17.8.12's Mch=Char fix to
`IsCharacterModel` was correct but couldn't take effect -- the v0.17.8.12
NPC-skip diagnostic in the dedupe fallthrough reported
`setmodelSlot=-1 fieldParsed=1 isChar=0` for kanban2 on every catalog
refresh. The chara.one parse landed (fieldParsed=1) and the classifier
is now correctly inclusive, but the JSM scanner's SETMODEL operand
capture never wrote a slot into `JSMEntityInfo::setmodelSlot`. The
v0.17.8.11 capture rejects values that are absent (pushCount==0), PSHM
markers (0x8000xxxx), or outside `[0, 64)` -- and we don't yet know
which gate is firing for kanban2 (or whether SETMODEL pops from stack
at all vs. takes an inline arg in the opcode word).

This build adds one diagnostic log line at the SETMODEL-init detection
in `field_archive_jsm_scan.inl` that dumps `opcParam` (the inline arg)
and the last few stack values whenever SETMODEL fires in init for an
entity whose setmodelSlot capture has not yet succeeded. The BAT after
this build will show, for kanban2 and every other character entity, the
exact encoding shape -- e.g. a stack-top `0x00000003` (literal slot 3,
should pass capture) vs. `0x80000004` (PSHM marker rejected) vs.
`(empty)` (inline arg path via `opcParam`).

v0.17.8.14 will use the dump to write the correct capture. No behavioral
changes in v0.17.8.13 -- diagnostic only.

## v0.17.8.12

Field navigation: bug-#10 follow-on. The v0.17.8.11 chara.one classifier
hooked correctly and parsed every field's archive, but on bghall_3 (B-Garden
Hall 6) Xu still announced as "Interaction 3". The first BAT's per-slot log
lines (`field=170 slot=K name='...' isMch=N textures=M -> CHARACTER/MCH/prop`)
revealed the misclassification:

- Every visited field's slots 0-N classified as `MCH` (the `flag>>24 == 0xD0`
  branch). Their names had the canonical FF8 disc-character pattern:
  `d000`, `d002`, `d005`, `d009`, ... These are the field's loaded character
  models -- party plus named story NPCs (Xu, Quistis, Edea, Cid).
- The remaining slots had `p0xx` names and classified as `prop`.
- The texture-count branch (intended discriminator) never fired in a way
  that mattered: every non-MCH entry read exactly `textures=1` and dropped
  out as prop.

The `0xD0` flag is FF8's "character model" marker, not "party MCH". All
d-prefix entries are people; the Mch/Char split in `IsCharacterModel` was
the wrong axis. The fix is one line: `IsCharacterModel` now returns true
for `ClassChar` OR `ClassMch`. The enum keeps both names for log clarity
(per-slot lines still distinguish them), but for the NPC-override question
they're equivalent answers.

If this build still announces "Interaction 3" on bghall_3, a second
fallthrough condition is in play: either the JSM scanner's SETMODEL slot
operand capture isn't firing (kanban2's `setmodelSlot` stays -1), or the
chara.one parse hadn't landed before the catalog refresh. A one-line
diagnostic was added to the dedupe fallthrough path that logs
`setmodelSlot`, `fieldParsed`, and `isChar` whenever the v0.17.8.11 NPC
override is skipped -- the next BAT will show exactly which condition
failed and v0.17.8.13 can target it precisely. (Removed once the override
is firing reliably.)

The textures=1 misread of the inline texture table remains -- the prop
branch terminates after one iteration because the next u32 reads as the
`0xFFFFFFFF` terminator. It doesn't matter for classification anymore
(d-prefix entries take the 0xD0/Mch path that never reads textures), so
the parser walk is left as-is rather than churning more code in this
fix. If a non-d-prefix character model ever surfaces in a field, the
threshold logic can be revisited then.

## v0.17.8.11

Field navigation: script-injected NPCs whose interactivity is dispatched via
REQ from another entity now announce as "NPC" instead of a generic
"Interaction N". The canonical case is Xu on bghall_3 (B-Garden, Hall 6),
whose visible entity (kanban2) is a signboard-style proxy with no own dialog
opcodes; the actual Xu interaction is fired by a REQ from kanban2 to another
entity. The catalog had no way to tell whether such a JSM-injected raw-SYM
object was a person or a prop, and was labeling every such entry
"Interaction N".

### How the new signal works

FF8 field scripts run SETMODEL in their init method to load a 3D model from
the field's `chara.one` archive. The model itself is what tells a person
apart from a prop: character models hold an inline texture-offset table
with many entries (FF8 NPCs use multiple body-part + animation-frame
textures); props have at most a few. Reading the slot's classification
from the archive gives a one-bit person-or-prop answer for every entity
that called SETMODEL.

Getting at that archive at the right moment was the difficult part. The
in-game pointer `chara_one_data_start` is a moving cursor that the engine
advances through the file as it streams model bodies and textures; by the
time the catalog refreshes, the cursor is mid-stream, not at the index --
a v0.17.8.10-diag probe (read-only, since reverted) parsed garbage from
it and ruled this approach out. FFNx itself only parses the archive ONCE
per field, via a `replace_call` patch on the call site of
`chara_one_read_file` inside `load_field_models`, capturing the buffer at
the moment of the header read.

The new `FieldCharaOneParse` module installs a MinHook on the entry point
of `chara_one_read_file`, in parallel with FFNx's call-site replacement
(the two hook mechanisms touch different bytes, so they coexist). On every
invocation our hook calls through to the original, then sniffs the
post-read buffer: if the first u32 is a small model count (0 < n <= 32)
and the field hasn't been parsed yet, the buffer is the header read and
we walk the per-entry records exactly as FFNx does (count, offset,
section_size, flag, optional size-echo skip, MCH branch for `flag>>24 ==
0xD0` or inline texture table terminated by `0xFFFFFFFF`, name-and-trailer
skip). Each slot is tagged Char / Prop / MCH and stored in a per-field
classification table; MCH-tagged slots are party characters that the
upstream party filter handles separately. Pass-through wrapper, never
modifies the read.

The character-vs-prop split uses a texture-count threshold of 6: the
threshold was set conservatively high so the first BAT cycle won't
false-positive props as NPCs; per-slot lines are logged at parse time so
the number can be retuned from observed data if real NPCs slip through.

### Wiring

The JSM scanner already detected SETMODEL in init scripts via
`foundSetmodelInit` (a bool) -- this version also captures the slot
operand from top-of-stack at the same opcode site, rejecting PSHM markers
and out-of-range values, and surfaces it on `JSMEntityInfo::setmodelSlot`
(-1 when no SETMODEL was found). The catalog's raw-SYM relabel pass in
`field_nav_catalog_dedupe.inl` runs the NPC check first: if
`FieldCharaOneParse::IsCharacterModel(currentFieldId, setmodelSlot)`
returns true, the entry becomes "NPC" / `ENT_NPC`; otherwise it falls
through to the existing "Interaction N" relabel. `IsCharacterModel` is
gated internally on `IsFieldParsed`, so before the chara.one read has
landed for the field, the call returns false and the entry stays
"Interaction N" -- which is what we want for the very first F9 after a
field transition; subsequent refreshes pick up the NPC label.

Address chain (US Steam, resolved at startup, SEH-wrapped):
`read_field_data + 0xF0F` -> `load_field_models`,
`load_field_models + 0x15F` -> `chara_one_read_file`. JP/non-US builds
disable the feature cleanly (the catalog falls back to "Interaction N"
for everything, which is the prior behavior).

### Why not name the NPC

This change deliberately does NOT try to identify Xu specifically. The
4-byte name field in chara.one is truncated (`cardgamemaster` becomes
`card`) and not useful as a friendly label. The goal here is solely to
stop calling people "Interactions"; identifying them by name remains
out of scope.

## v0.17.8.10

Field navigation: the B-Garden hub (bghall_5, "Hall 10") now lists its exit to
Hall 4. The INF-gateway exit was being silently dropped by a faulty screen
filter; the fix makes the gateway visibility test geometrically correct.

### The bug

The hub's only path to Hall 4 (bghall_2, field 168) is an INF gateway, with no
SETLINE trigger. The catalog's gateway screen filter called
`IsSeparatedByTriggerLine()`, which does an INFINITE-line side test: it asks
which side of a line's infinite extension each point falls on. A BAT capture
(read-only [gw-diag] logging, since removed) showed the Hall 4 gateway
(center -4572,3777, far west) was "separated" from the player by line9 -- the
Hall 6 doorway exit, a short SCREEN_BOUND segment on the far EAST edge
(x in [4206,5042]). Extended to infinity that nearly-horizontal line passes
between the two points because the gateway's Y (3777) sits almost exactly on
the line's extension, so the gateway was filtered on every refresh. The same
single-gateway pipeline surfaces Hall 6's gateway to Hall 10 fine -- no short
edge-line happens to be collinear with it -- which is what made this
field-specific.

### The fix (`field_navigation.cpp`, `field_nav_catalog.inl`)

Added `SegmentsCross()` -- a proper bounded segment-vs-segment intersection
(orientation test). The INF-gateway screen filter now skips a gateway only when
the player->gateway SEGMENT actually crosses a screen-boundary line SEGMENT,
not its infinite extension. A gateway is a real exit you walk to; it is on a
different screen only if the path to it genuinely crosses a boundary. Entity
screen-filtering is unchanged -- it still uses the infinite-line
`IsSeparatedByTriggerLine()`; only the gateway test moved to the bounded test,
keeping the blast radius minimal.

## v0.17.8.9

Field save-point detection: the B-Garden hub hallway (bghall_1, "Hall 1") save
point now reads "Save Point" instead of "Interaction 1". Also a size refactor of
the two impacted .inl files to restore headroom under the 80,000-byte ceiling.

### Save point (the fix)

bghall_1's save spot is the trigger line `ent5 'selphie'` (SETLINE center
-700,-8593). A LOCAL one-shot script dump (added, used, and now removed)
proved the static signal: selphie's own bytecode literally pushes the
save-enable opcode CONSTANTS -- PUSH 303 (0x12F SAVEENABLE) and PUSH 304 (0x130
PHSENABLE), in two methods. It dispatches the save through a runtime-supplied
0x1C (empty-stack, like the dorm bed), so the opcode-resolved foundSaveenable
never fires; and neither save-POINT entity can carry the label (savePoint has
PSHM-only X/Y so it never gets a position, and saveline0 is a REQ-chain
controller with a MAPJUMP that classifies it MAP_EXIT). The sibling control line
`ent4 'zells'` has none of these constants -- a clean discriminator.

Fix (`field_archive_jsm_scan.inl`): the per-instruction literal-push branch now
notes literal pushes of MENUSAVE (0x12E) / SAVEENABLE (0x12F) / PHSENABLE
(0x130). Save-line signal-(a) gained an `ownSaveConst` term -- true when MENUSAVE
appears alone, or SAVEENABLE and PHSENABLE both appear -- which sets isSaveLine
and forces the line LINE_INTERACTIVE so the catalog relabels it "Save Point" at
the line's own SETLINE center (where auto-drive already arrives). The tight
two-constant rule, scoped to Line entities, keeps non-line entities and the
control line `zells` unaffected; no proximity/heuristic guess is used.

### Size refactor (no behavior change)

Both impacted files were within ~300 bytes of the 80,000-byte CI ceiling, so the
fix could not land without first reclaiming space:
  - `field_archive_jsm_scan.inl` 79,696 -> 75,590 bytes: removed the LOCAL
    bghall_1 dump diagnostic (its job is done) and four long-disabled `if
    (false)` diagnostic blocks (POPM_W-address dump, PSHM_W-coords summary,
    per-method MAPJUMP dump, INF-gateway dump). All dead code, recoverable from
    git. `DumpEntityScript` itself (field_archive_jsm_dump.inl) is kept.
  - `field_nav_catalog.inl` 79,718 -> 74,387 bytes: the v0.17.8.8 object/line
    dedupe + raw-SYM relabel pass moved verbatim into a new statement-fragment
    file `field_nav_catalog_dedupe.inl`, `#include`d inline at the same point in
    RefreshCatalog (it still operates on the local newCatalog[]/newCount, so
    behavior is byte-identical).

### Expected BAT outcome

Enter bghall_1 (Hall 1, off the dorm corridor): the save point now reads "Save
Point" at (-700,-8593) -- previously "Interaction 1". Regression checks: the dorm
(bgryo2_1) Save Point still works; the bghall_2/bghall_3 kanban dedupe/relabel
still works; no false "Save Point" appears on other fields. Field log shows
`save-line(own): ... isSaveLine=1 (resolved=0 const=1)` for selphie.

## v0.17.8.8

Field navigation catalog: filter duplicate entries where the same physical
interactable is surfaced twice. Reported by Aaron on bghall_2 (a B-Garden hub
hallway), where a signboard appeared as BOTH "Interaction 2" and "Kanban1".

### Root cause

The signboard `ent23 'kanban1'` is a real interactive object, so it reaches the
catalog two independent ways: the JSM scanner classifies it INTERACTIVE_OBJECT
and the injection block adds it as an object entry ("Kanban1" -- the raw SYM,
capitalized, since there is no friendly-name mapping for it), AND its walk-on
trigger line is classified INTERACTIVE and added as "Interaction 2". Both sit at
the same coordinates (-3886,-5070)/(-3886,-5069). Nothing in `RefreshCatalog`
reconciled an object against a coincident line, so both showed.

### Fix: general object/line dedupe

New pass at the end of catalog assembly (`field_nav_catalog.inl`, before the
change-detect/commit step): when a JSM-injected object (sentinel <= -300) and an
interactive trigger LINE (sentinel -200..-299, type ENT_INTERACTION) resolve to
the same position (within 128 world units), they are treated as one physical
interactable and only one entry is kept. Which one follows the more-informative
name:
  - A named object -- a special type (Save/Draw/Shop/Card) or one that resolved
    to a friendly name like "Directory" -- is kept; the coincident line is
    dropped.
  - An object whose only name is its raw (capitalized) SYM, like "Kanban1", is
    dropped in favour of the line, because "Interaction N" is the term the
    player already understands. (Per Aaron: that name is sufficient.)
Exits are never touched here -- they already dedupe by destination in the INF
gateway / MAP_EXIT blocks. Drops log `[dedup] dropped ...` so the filtering is
visible in the field log.

### Why this is safe

The match is tight (128 units; the kanban overlap is ~1 unit, real distinct
interactables are hundreds of units apart) and scoped to the object-vs-
interaction-line class only. NPCs (positive entity indices) and exits are not
considered, so no real navigation target is lost. The informative-name rule
protects meaningfully-labelled objects: a Directory or Save Point that ever
coincides with a trigger line keeps its specific label rather than being reduced
to "Interaction".

### Expected BAT outcome

Reload bghall_2 and cycle the catalog with F9: the signboard appears once, as
"Interaction 2" -- no separate "Kanban1" entry. The field log shows `[dedup]
dropped JSM 'Kanban1' ... duplicate of Interaction line1`. Regression check on
bghall_1: the Directory and the save-point Interaction remain (they sit far
apart, so the dedupe does not fire), and the dorm bed/save point/exit are
unchanged.

*BAT-confirmed:* on bghall_2 ("Hall 4") the duplicate Kanban1 was removed.

### Also: raw-SYM object relabel

The dedupe only fires when an object is coincident with a trigger line. On
bghall_3 ("Hall 6") the `kanban2` signboard is a STANDALONE interactive object
(nearest line ~696 units away), so it survived dedupe and surfaced as "Kanban2"
-- exposing the raw internal symbol. A post-dedupe pass now relabels any
surviving JSM object whose only name is its raw SYM to a generic "Interaction N"
(numbering continued from existing interactions), so internal symbols like
"Kanban2" never reach the player. It runs AFTER the dedupe so the raw name is
still intact for the dedupe's own raw-SYM test (Hall 4 behavior preserved).
Friendly-named objects (Directory) and named specials (Save/Draw/Shop/Card) are
unaffected. Only positioned objects reach the catalog, so the relabeled entry
stays navigable. Logs `[dedup] relabeled raw-SYM object ...`.

### Not in this build

(none — the Hall 1 save-point label is addressed below.)

### Also in v0.17.8.8: save-point label via script association

The B-Garden Hall 1 (bghall_1) save point read as a generic "Interaction 1"
rather than "Save Point" — a regression introduced when interactive object/line
detection (bed, signposts) was added. Root cause: the scanner DOES detect the
'savePoint' entity as a Save Point (it finds MENUSAVE), but that entity's X and Y
are both PSHM runtime variables, so it never resolves a navigable position and is
never injected as a standalone Save Point. The save spot reaches the catalog only
via its co-located trigger line, which the Line-classification block labels a
generic Interaction.

Fix (script association, per the mechanism Aaron identified): a Line is now
flagged `isSaveLine` when EITHER (a) its own script invokes the save menu
(MENUSAVE/SAVEENABLE/PHSENABLE) — the type cascade detects this but the Line
block used to discard it — OR (b) it REQs an entity that is a Save Point (type
SAVE_POINT, or a save*/svpt SYM name, covering a 'saveline' that was classified
MAP_EXIT because its script also has a MAPJUMP). The catalog then surfaces that
line as "Save Point" (type ENT_SAVE_POINT) at its own SETLINE-center position
instead of "Interaction N" — no save-point positioning required. Cannot mislabel:
it fires only on a provable save signal in the script.

A one-shot `[save-wiring]` log reports, per save-point entity, whether it has a
position and how many save-lines the field flagged — so the BAT is conclusive
even if a given field wires its save differently (e.g. pure proximity with no
line link), revealing exactly what to extend.

### BAT result (save point) — did NOT fire; root cause confirmed

Reloaded bghall_1. The `[save-wiring]` log reads: `ent27 'savePoint'
hasPosition=0 hasPshmCoords=1 -- field has 0 save-line(s) flagged`. So both
signals correctly did not fire, and the surrounding log shows why this field
cannot be handled statically:
  - `savePoint` has no navigable position (X and Y are both PSHM runtime
    variables AND its runtime entity-struct slot reads zero -- it is an
    invisible script entity, unlike the kanban which positions fine via the
    struct read). So it can never be injected as a standalone Save Point.
  - No line statically REQs it. Across bghall_1/bghall_3 the scanner reports
    `reqResolved=0` for nearly every entity and the resolver logs `stack
    underflow ... unresolved` -- the static abstract stack loses the REQ
    targets, so `selphie`'s save line ("Interaction 1" at -700,-8593) cannot be
    linked to `savePoint` even though it almost certainly REQs it.

The save-line detection is KEPT (it is correct and will fire on any field whose
save line has its own MENUSAVE or a resolvable REQ to a save point), but
bghall_1 has neither. Labeling it requires one of: (1) extending the
MapjumpResolver-style basic-block re-walk to also capture REQ targets (a real
scanner improvement, also helps dual-purpose lines), or (2) a focused local
diagnostic that dumps `savePoint`'s full runtime others-struct to see if its
position lives at a non-standard offset, or (3) runtime detection when the save
menu actually opens. The player can still reach and use the save spot as
"Interaction 1"; only the label is missing. No proximity/heuristic guess will be
shipped -- a wrong "Save Point" label would mislead a blind player.

## v0.17.8.7

Field navigation catalog: two related correctness fixes, both surfaced during
the v0.17.8.6 BAT on the B-Garden hub fields.

## Fix 1 — phantom `cardmaster` / `Card Player`

A phantom interactive object was read out in the B-Garden corridors and Main
Hall but nothing was there when the player reached it. Pre-existing bug, NOT a
regression from v0.17.8.6.

### Root cause

The phantom is the `cardgamemaster` family of entities (`cardgamemaster`,
`cardgamemaster2`, `cardgamemaster3`). They are debug card-game scaffolding:
invisible (`model=-1`), appear as numbered copies, sit among other debug/dummy
entities (`dammy`, `synkun`, `seito*`), reference test-battle fields (`testbl2`,
`testbl8`, `testbl14`, `test5`), and do nothing when reached -- confirmed by an
F11 screenshot of an empty spot on bgroad_5. The real FF8 card challenges are
launched from visible CC-group NPCs via the CARDGAME opcode, not these entities.

They reach the catalog through the INTERACTIVE_OBJECT promotion plus a resolved
position (SET3 shift-pattern or runtime LATE-RESOLVE). On bghall_1 the
entity is promoted specifically by the Director post-pass, which aggressively
promotes any cat-3 entity with extended-dispatch when a Director is present.

### Fix: name-scoped debug-leftover filter

New `EntityIsDebugLeftover(e, sym)` helper (`field_archive_jsm_scan.inl`,
forward-declared in `field_archive_jsm_state.inl`): true when the entity SYM
name begins with `cardgamemaster`, or (secondary) when its init-var writes
reference a `testbl*` field. The name signal is the reliable one -- it works
regardless of whether the entity has any init-var writes (on bghall_1
`cardgamemaster` has none) and mirrors the existing name-scoped filters for
`camera` and party members. The guard is applied in all three INTERACTIVE_OBJECT
promotion paths so the entity cannot be re-promoted after being skipped:
  1. the main-scan direct promotion (dialog/extDispatch + position),
  2. the paired-entity coordinate-inheritance promotion, and
  3. the Director post-pass promotion (`field_archive_jsm_director.inl`).
Skips log `NOT promoted to INTERACTIVE_OBJECT` / `[DIRECTOR] skipped ... debug
leftover`, so the suppression is visible in the field log.

### Why this is safe

`cardgamemaster*` is debug scaffolding, not a real interactable, on every field
observed. The filter is scoped to the INTERACTIVE_OBJECT promotion only -- it
does not touch Line classification, save/draw/shop/card detection, MAP_EXIT
detection, or the runtime entity loop. Real interactables (the B-Garden
Directory `igyous1`, dorm beds, examinable objects like `water`) are not named
`cardgamemaster` and are unaffected.

## Fix 2 — interactive lines double-listed ("Event" + "Interaction"), and the Directory vanishing

Reported in the same BAT: B-Garden Main Hall pathway signs (now surfaced thanks
to the v0.17.8.6 extended-dispatch Line rule) appeared to flicker between
"Event" and "Interaction" in the catalog, and the Directory stopped appearing.

Root cause is one vestigial code path in `RefreshCatalog`
(`field_nav_catalog.inl`). The legacy "Event" block skips Lines of type
CAMERA_PAN / EVENT / SCREEN_BOUND / UNKNOWN -- which, after v0.12.12 removed its
original UNKNOWN-only purpose, left `LINE_INTERACTIVE` as the ONLY type it still
emitted. The "Interaction" block immediately below emits that same
`LINE_INTERACTIVE` line again (identical `-200-t` sentinel). So every interactive
Line was injected twice -- once as "Event" (catalog type `ENT_OBJECT`) and once as
"Interaction N" (`ENT_INTERACTION`) -- and cycling landed on both, reading as a
flicker. The bogus `ENT_OBJECT` "Event" entry then tripped the JSM-injection
block's `alreadyInCatalog` test (`type == ENT_OBJECT`), causing it to skip the
real Directory (`igyous1`, also `ENT_OBJECT`). The v0.17.8.6 rule only made the
problem visible by classifying more Lines as `LINE_INTERACTIVE`.

Fix: the "Event" block now also skips `LINE_INTERACTIVE`. This makes the block
emit nothing (its UNKNOWN-only purpose was already gone), so interactive Lines
surface exactly once -- as "Interaction N" -- and the `ENT_OBJECT` collision that
hid the Directory disappears. No legitimate entry is lost.

## Expected BAT outcome

Reload bghall_1 (B-Garden Main Hall) and/or bgroad_5 (dormitory corridor) and
cycle the catalog with F9. Expected: (1) no `Card Player` / `cardmaster` entry;
(2) each pathway sign appears ONCE, as "Interaction N" -- no "Event" duplicate,
no flicker between Event and Interaction; (3) the Directory appears again. Real
entries (Directory, exits, save point, dorm bed) remain. The field log should
show `cardgamemaster' NOT promoted to INTERACTIVE_OBJECT` / `[DIRECTOR] skipped
... debug leftover`, a single `cat... TRIGGER line... name='Interaction 1'` per
sign (no paired `name='Event'`), and `JSM-injected Directory ... sym='igyous1'`.

### Not in this build (staged follow-up)

The runtime dialog-confirmation + disk-persistence layer (catch objects the
static proxy misses; confirm/label static guesses; demote phantoms that never
fire dialog when reached; persist a known-objects DB across restarts) is the
larger next piece and is the general answer to the Director's over-promotion;
it is intentionally not bundled here.

## v0.17.8.6

Fix: the B-Garden dormitory bed (field bgryo2_1) now appears in the navigation
catalog, and the duplicate/dead second "Exit" is gone. Rolls up the v0.17.8.5.x
diagnostic chain into a fix and removes all of that diagnostic logging.

### What the diagnostics settled

The runtime entity-ID probe (v0.17.8.5.2) was decisive: the bed is **ent0
'squall'**, the trigger Line, at SETLINE center (-50,496). Its "I should get
some rest" AASK prompt fires in ent0's own script, reached through a *bare
0x1C ext-dispatch* whose sub-opcode index is supplied at runtime (logged
"0x1C EMPTY STACK: ent=0 method=1"). The opcode constants are correct
(verified against the engine's dispatch table in ff8_addresses.h: AASK=0x6F);
the encoding is high-byte. The static scanner simply cannot resolve a
runtime-supplied 0x1C to a dialog opcode, so `foundDialogOp` is false for the
bed and it was classified `LINE_EVENT` -- which the catalog hides. That is why
it was missing, and no amount of better static decoding can see it.

### Fix 1 — pre-detect runtime-0x1C interactive Lines (so blind players can FIND them)

Line classification (`field_archive_jsm_scan.inl`) now routes a Line whose own
0x1C usage (`extDisp`) is NOT a screen-exit (mapjump), battle trigger, or
camera-scroll to `LINE_INTERACTIVE`, surfaced by the existing catalog Block 3
at its SETLINE center. This is the same `extDisp` interactivity proxy the
cat2/3 INTERACTIVE_OBJECT promotion already uses for background objects (e.g.
the Directory) -- Lines were the asymmetric gap. Crucially this works at field
load, BEFORE the player crosses the line, which runtime detection alone cannot
do. Dialog-op beds (foundDialogOp) are unaffected; exits, battle lines, and
camera pans keep their classification.

Known tradeoff: a Line whose 0x1C only drives a sound/particle effect (no
dialog) can surface as a phantom "Interaction" on some fields. The v0.17.8.7
runtime dialog-confirmation + disk-persistence layer is intended to prune and
label these (an object that actually fires MES/ASK when reached is confirmed;
one that never does can be demoted), and to persist confirmed interactions
across restarts as a shippable known-objects database.

### Fix 2 — suppress the dead duplicate exit

The catalog's JSM MAP_EXIT injection now drops an exit that has neither a
navigable position nor a resolvable/world-map destination (`field_nav_catalog.inl`).
bgryo2_1 ent15 'l1' is a positionless MAP_EXIT with an unresolved runtime-var
destination (param=INT_MIN); with no INF gateways on the field the existing
gateway-suppression never fired, so it injected a bare second "Exit". The real
exit (ent1 'squalls' -> Hallway) is unaffected.

### Cleanup

Removed the `[dorm-diag]` scan block and the `[ASK-ENTITY]`/`[AASK-ENTITY]`
dialog logging added across v0.17.8.5.x.

## v0.17.8.5.2

DIAGNOSTIC build (no behavior change), iteration 3 of the bgryo2_1 dorm
investigation. Runtime entity-ID probe.

### What v0.17.8.5.1 settled and what it didn't

The widened dump confirmed the dialog log: the dorm bed fires an **AASK**
("Squall 'I should get some rest.'" / Rest / Don't rest). It also nailed the
root cause of `dialog=0` everywhere: on this field the dialog opcodes are
dispatched through the `0x1C` ext-dispatch prefix (the histogram tops out at
`0x34`; everything higher, dialog included, is a `0x1C` sub-opcode), so the
scanner's high-byte-only dialog check never sees MES/ASK/AMES/AASK and records
`extDisp=1` instead.

What it did NOT settle: WHICH entity owns the AASK. `ent0 'squall'` is the
scene controller (its method[2] writes the post-event destination var to Hotel
111 / 2F Hallway 139 / Classroom 232 / Secret Area 248 / Dorm Double 239), but
its dumped methods contain no `0x1C` dispatch at all -- the `PUSH 111 (0x6F)`
instructions there are writing the field-id 111 (Hotel) into a variable, not
dispatching AASK (0x6F). The AASK is reached elsewhere (possibly via ent0's
`REQEW`, or in an entity not yet pinned). Two static red herrings (the earlier
fallback-heuristic hit, now the 111 coincidence) make clear that hand-decoding
the branch-heavy bytecode is too error-prone to identify the entity reliably.

### Change (decisive, minimal)

Log the executing field-script entity pointer in the existing `opcode_aask`
and `opcode_ask` hooks (`field_dialog_opcodes.inl`). These hooks already
receive the entity pointer (`int entityPtr`). The JSM entity index resolves as
`(entityPtr - line0SetlinePtr) / 0x1A0`: the runtime entity stride is `0x1A0`
(proven by the three `[SETLINE]` line pointers `0x0188B818 / 0x0188B9B8 /
0x0188BB58`, exactly `0x1A0` apart), and lines are entities 0/1/2, so the
`[SETLINE] call#1` pointer (idx>>8==0) is the entity-0 base.

### Expected BAT outcome

Reload bgryo2_1 and interact with the bed, then send `Logs/ff8_dialog.log`
(tiny -- contains `[AASK-ENTITY] entityPtr=0x...`) and the head of
`Logs/ff8_field.log` (for the `[SETLINE]` base pointer). That pins the bed
entity with certainty. v0.17.8.6 then: (1) teach the scanner to resolve
`0x1C`-dispatched MES/ASK/AMES/AASK so the bed entity gets real `dialog=1`;
(2) surface it in the catalog with a position (line center if it's a line, or
SET3/paired coords otherwise); (3) suppress the positionless `l1` exit; and
remove all dorm diagnostics.

## v0.17.8.5.1

DIAGNOSTIC build (no behavior change), iteration 2 of the bgryo2_1 dorm
investigation.

### What the v0.17.8.5 run proved

- There are ZERO MES/ASK opcodes anywhere on the field (every entity
  `dialog=0`). A text "rest?" prompt is therefore not how the bed works here.
- The only Interactive Object, `ent19 'suit'`, is a cutscene controller, not a
  bed: its script has no position-setting opcode and just fires `REQ`/`REQEW`
  at the `'zell'`/`'zells'` background animation methods (global methods
  130-141) -- it orchestrates the SeeD-uniform-change sequence. It is
  misclassified as an Interactive Object (ext-dispatch + REQ) but is
  positionless, so it never reaches the catalog anyway.
- The duplicate exit is confirmed: `ent15 'l1'` is a positionless MAP_EXIT with
  an unresolved runtime-variable destination (`param=-2147483648`); the real
  navigable exit is the positioned `'squalls'` SCREEN_BOUND -> Hall 10.

### Why iteration 2

Aaron confirms the bed IS interactable at this story point, so a trigger exists
in one of the Other entities whose script the v0.17.8.5 dump did not cover
(it only dumped Lines, Backgrounds, and Interactive Objects). The flag summary
flags `ent12 'hon'` (the only mystery Other with activity: 2 REQ ops and a SET3
position that failed to resolve, tri=229) and `ent11 'kigaeyarou'` (the
change-clothes handler) as the prime suspects. The bed most likely REQs the
`'suit'` cutscene rather than showing text -- consistent with `dialog=0`.

### Change

Widen the `[dorm-diag]` script dump from "Lines + Backgrounds + Interactive
Objects" to "Lines + Backgrounds + every Other with reqOps<=30" -- i.e. dump
every entity EXCEPT the 95-op `'l1'` dispatcher (still excluded; its param/pos
in the summary already characterize it). This surfaces `'hon'`, `'kigaeyarou'`,
`'dic'`, etc. so the bed's trigger entity, its REQ target, and its position
handling become visible.

### Expected BAT outcome

Reload bgryo2_1 and send `Logs/ff8_field.log`. The widened dump identifies the
bed trigger; combined with Aaron's note on what interacting with the bed does
in-game (text / clothes-change / fade), the v0.17.8.6 fix then surfaces the bed
and suppresses the positionless `'l1'` exit, removing this diagnostic block.

## v0.17.8.5

DIAGNOSTIC build (no behavior change). Investigates two pre-existing catalog
gaps on bgryo2_1 (Squall's B-Garden dormitory, the SeeD-uniform scene) reported
by Aaron after the v0.17.8.4 camera fix: the bed interaction is missing, and a
second, positionless "Exit" is listed alongside the real Hall 10 exit. Expected
catalog is 1 interaction (bed) + 1 save point + 1 exit; actual is save point +
two exits, no bed. Both gaps predate the recent builds -- not regressions.

### What the JSM scan already tells us

- Every Others entity on this field has `dialog=0` (no MES/ASK). The room's
  interaction is not a normal dialog entity. The scan logs
  `REQ-interact: Line ent0 'squall' REQs interactive entity ->
  hasDialogReqTarget=1`, but that fired via the FALLBACK heuristic (unresolved
  REQ opcodes + some Interactive Object exists on the field), so we do NOT yet
  know ent0's real target. ent0 'squall' is classified `JSM_ENT_LINE_EVENT`,
  and the catalog deliberately skips Event lines (v0.12.12), so if the bed is
  this line it is dropped.
- `ent19 'suit'` is classified Interactive Object but is unpositioned (PSHM
  coords inherited from the unresolved 'camera'), so it never reaches the
  catalog either. It is a second candidate for the "missing interaction".
- The extra exit is `ent15 'l1'`: a MAP_EXIT with no captured position (0,0)
  and `param=-2147483648` (0x80000000, an unresolved varblock marker, not a
  field id). Its destination is runtime-variable (initVars list Dormitory
  Double 3, Master Room 5, Hall 3, etc.). The real exit is the positioned
  'squalls' SCREEN_BOUND -> Hall 10. bgryo2_1 has 0 INF gateways, so the
  catalog's gateway-based exit suppression never applies and the positionless
  'l1' is injected as a generic "Exit".

### Diagnostic added

A field-gated `[dorm-diag]` block at the end of `ScanJSMScripts()`
(`field_archive_jsm_scan.inl`), active only on bgryo2_1. It logs:

1. A per-entity flag summary for every scanned entity: category, type,
   position, `dialog` / `extDisp` / `reqOps` flags, resolved REQ targets,
   `dialogReqTarget`, and `param`.
2. The full decoded script (via `DumpEntityScript`) of every Line (cat=1),
   Background (cat=2), and Interactive Object on the field. This shows whether
   `ent0 'squall'` does an ASK/MES (a real bed/rest prompt), a MAPJUMP (an
   exit), or only camera/sound (a cutscene trigger), and what `'suit'` is.

The big `'l1'` MAP_EXIT dispatcher (95 REQ opcodes) is intentionally NOT dumped
to avoid log truncation; the summary line's `param`/`pos` already confirm it is
positionless with an unresolved destination, which is enough to design the exit
suppression in the follow-up fix.

### Expected BAT outcome

Reload bgryo2_1 (no navigation needed -- the diagnostic fires at field load) and
send `Logs/ff8_field.log`. The `[dorm-diag]` summary plus the Line/Background/
Interactive-Object script dumps will identify which entity is the bed and how
it triggers its prompt, and confirm the `'l1'` exit is a positionless duplicate.
The v0.17.8.6 fix then surfaces the bed (a small, targeted line/object
classification change) and suppresses the positionless unresolved exit, and this
diagnostic block is removed.

## v0.17.8.4

Field navigation catalog: a non-interactive camera-control entity was listed
as a navigable "Camera" object (reported on bgryo2_1, the B-Garden dormitory).
Reaching it does nothing -- it is a scene-camera script, not a player object.

### Root cause

The Director-detection post-pass in `field_archive_jsm_director.inl` promotes a
script entity to `JSM_ENT_INTERACTIVE_OBJECT` when it has dialog OR extended
dispatch (`!s_hasDialogAny[tgt] && !s_hasExtDispatchArr[tgt]` -> continue, else
promote). The `0x1C` extended-dispatch opcode is a catch-all -- camera moves,
sound, particle effects -- not only player interaction. On bgryo2_1 the scan
recorded `ent18 'camera' ... dialog=0 extDisp=1`: no dialog at all, but it
drives the scene camera via `0x1C`, so it slipped through the OR, was promoted,
and was injected into the catalog as a "Camera" (JSM entity 18, beyond the
7-entity runtime window, so the promotion is its only path into the catalog).

### Fix

A name-scoped guard in the promotion loop, alongside the existing
party-character filter: skip targets whose SYM begins with `camera` (covers
`camera` and `cameraman`). Camera entities are conventionally named, have no
dialog, and are never navigation targets. Kept name-scoped rather than
tightening the promotion criterion to require dialog, because that broader
change could drop genuine no-dialog interactables (levers, switches) on other
fields; the dormitory/classroom Director heuristic depends on the looser rule.

### Regression safety

- Only camera-named targets are affected; every other promotion path is
  unchanged.
- Real interactables (bed, desk, wardrobe, sign) are not named `camera`, so
  they still promote normally.
- The guard sits in the promotion loop only; it does not touch runtime-entity
  classification, save/draw point detection, or exit handling.

### Expected BAT outcome

Reload bgryo2_1 (B-Garden dormitory) and cycle the catalog with F9. The `Camera`
entry should be gone; the catalog should list only the Hall 10 exit, the save
point, and the second exit. The scan log should no longer show
`[DIRECTOR]   promoted ent18 'camera' ... -> Interactive Object`. Regression
check: on a dormitory/classroom field with a real Director (bed/desk/wardrobe),
those interactables should still appear.

## v0.17.8.3

Fire Cavern playthrough bug #4: a party member is announced as an NPC in the
field navigation catalog. Root cause confirmed via the v0.17.8.2 diagnostic.

### Root cause

The party-member filter in `field_nav_catalog.inl` (v0.14.108) skipped an
entity only when it was a FOLLOWING party member: a visible character model
(0-9) with `throughonoff > 0` (player walks through it) and no talk/push. In
scripted scenes -- a dormitory, the Laguna dream intro -- party members are
placed as STATIC actors with no interaction flags at all
(`talk==push==thru==0`), so the `throughonoff>0` test missed them and they
were classified as `NPC`. The v0.17.8.2 diagnostic confirmed this on bgryo2_1
(B-Garden dormitory, formation [1,0,5,255]): `ent1 model=3` and `ent2 model=5`,
all interaction flags zero, both became catalog NPCs.

### Why not just drop the throughonoff requirement

Some draw points reuse a party-character model with all interaction flags zero
-- the Fire Cavern `drpoint` uses model 9. Blanket-filtering every model-0-9
zero-flag entity would delete those before the downstream JSM draw-point
reclassification can find them. The gwgrass1 (Laguna dream) log shows the same
hazard: a no-position `drpoint` whose fallback grabs the nearest catalog NPC.

### Fix

The discriminator is the entity SYM NAME, which is reliable and field-data
sourced. Party members are named after the character
(`squall`/`zell`/`laguna`/...); draw points are named `drpoint`/`dp*`, save
points `savePoint`/`saveline`, exits `l1` etc. -- none match. New helper
`IsPartyCharacterSym()` prefix-matches a SYM against the known playable /
party-swap character bases (squall, zell, selphie, quistis, rinoa, irvine,
laguna, kiros, ward, seifer, edea -- covering the Laguna dream party). The
filter now skips an entity with no talk/push interaction when EITHER it is a
visible character model (0-9) the player walks through (`throughonoff>0`,
following party member, model-based -- also catches followers whose SYM didn't
resolve) OR its SYM is a party-character name (placed scene actor, ANY model
and ANY walk-through state). The name branch is model-independent on purpose:
bgryo2_1 `ent5 'selphie'` uses model 11 (a full NPC model) with walk-through,
so a model<10 rule would have missed it -- only the name catches it.
Non-character zero-flag entities (draw points) are deliberately kept so their
reclassification still runs.

### Regression safety

- Following party members (Training Center, bgmon_2/bgmon_5): still caught by
  the model-based `throughonoff>0` branch -- unchanged.
- Real NPCs / save points (model 24) / exits: never named after a party
  character, so the name branch never touches them.
- Talkable scene characters (talk>0): `noInteract` is false, so NOT filtered;
  they stay navigable.
- Real exits are added via the trigger-line / gateway path, not the runtime
  entity, so filtering a character-named runtime actor never removes an exit
  (verified on bgryo2_1: the 'squalls' screen-boundary exit still appears in
  the catalog after the runtime ent1 'squalls' actor is filtered).
- Fire Cavern model-9 draw point (sym='drpoint'): not a character name, so NOT
  filtered; draw-point reclassification still works.
- If a SYM name fails to resolve, the name branch simply doesn't fire (no
  regression) -- the entity stays an NPC as before.

### Confirmed (BAT)

BAT on bgryo2_1 (B-Garden dormitory, formation [1,0,5,255]): all six party
entities now filter as `named party member` -- `squalls` (model 3), `squallsd`
(model 5), `zell` / `zells` / `selphies` (model -1), and `selphie` (model 11,
the high-model case the first model<10 attempt missed). Zero misses. The
catalog contains only real targets (Hall 10 exit, save point, camera, second
exit) and navigation is intact -- auto-drive to the camera logged `Arrived`
and GPS reported `In range`, so nothing needed was removed.

### Diagnostic removed

The `[party-filter-miss]` diagnostic from v0.17.8.2 has been removed now that
the fix is confirmed. The permanent `[party-filter]` log (which records each
filtered party member) is kept. Draw-point safety holds by construction, not
by observation: `drpoint` is not a character name and has thru=0, so neither
the follower branch nor the named-party branch can touch it.

### Not addressed here (logged for follow-up)

Two separate bugs surfaced in the first Laguna dream (gwgrass1) and are NOT
part of this change:
- Field navigation is fully broken in the Laguna dream: the player entity is
  not detected (`player=ent-1`), so auto-drive refuses every target
  (`player_pos_known=0`). Distinct root cause (the `setpc==0` player-detection
  heuristic) -- own chapter.
- Battle TTS announces the real party (Squall/Zell/Selphie) instead of the
  dream party (Laguna/Kiros/Ward), because the savemap formation still holds
  the real party during the dream. Battle-side, separate chapter.

## v0.17.8.2

DIAGNOSTIC build for Fire Cavern bug #4 (party member announced as NPC in
2-member parties on bdin2/bdin3). Not a fix -- adds one targeted log line
to pin the mechanism, to be removed once the real fix lands. Safe to push
or to keep local; the only behavioral change is an extra `[party-filter-miss]`
log line on fields that have a visible party-character-model entity which
the existing party filter did not catch.

### Background

The party-member filter in `field_nav_catalog.inl` (v0.14.108) skips an
entity from the navigation catalog when ALL of these hold:
  modelId in [0,9] (visible character model), throughonoff > 0 (walk-
  through), talkonoff == 0 (not talkable), pushonoff == 0 (no collision).
The follower-as-NPC bug means a 2-member party's follower fails one of
those four conditions on bdin2/bdin3, so it survives the filter and gets
classified as an NPC. The existing `[party-filter]` log only records
entities it DID filter -- it says nothing about a follower it missed, so
the mechanism (which flag is off) is currently unknown.

### Change

Added a `[party-filter-miss]` diagnostic immediately after the filter
block: for any non-player entity with a party-character model (0-9) that
survived the filter, log its talk/push/thru flags, triangle, and field
position. This fires only on the suspect case -- real NPCs use model >= 10
and never hit it -- so there is no log spam on NPC-heavy fields.

### Expected BAT outcome

Load bdin2/bdin3 with a 2-member party and press F9 to build the catalog.
The field log should show one `[party-filter-miss]` line for the follower,
revealing which flag (talk/push/thru) is set unexpectedly. That pins the
fix for the next build (e.g. relax the filter to also skip a model-0-9
walk-through entity that is pushable-but-not-talkable, if push is the
culprit). No user-facing behavior change otherwise.

## v0.17.8.1.1

Fire Cavern playthrough bug #3: garbage read out by TTS after a tutorial
scene completes. (v0.17.8.0 closed bugs #5 and #6; #1 Quistis FMV race
and #4 party-as-NPC remain deferred.)

This entry covers the whole tutorial-garbage fix. An initial filter was
built as v0.17.8.1 and BAT'd, but it let a longer garbage string through;
v0.17.8.1.1 strengthens the heuristic. v0.17.8.1 was never pushed, so the
two are folded into one shipped entry.

### Root cause

After a tutorial overlay tears down ([TUTO] mode 10 -> 1), FF8 leaves
stale bytes in window slot 0's text region. The accessibility poll loop
(`PollWindows` -> `ScanAndSpeakAllWindows("POLL")` in
`field_dialog_scan.inl`) then decodes those bytes as if they were real
dialog and speaks them. The decoder is not at fault -- every byte decodes
legally; the bytes themselves are garbage. Confirmed in Aaron's
2026-05-18 and 2026-05-21 dialog logs:

  [POLL] win[0] Speaking: ",e 3in*retone3 e~HP~B:All08E%~!/..."        (short)
  [POLL] win[0] Speaking: "q...1& 3,e 3in*retone3 ... meuiquymuy"      (~400 chars)

### Fix: IsGarbledText() heuristic filter

A new `IsGarbledText()` in `field_dialog_scan.inl` runs on each decoded
buffer before it is spoken, in both the live poll path
(`ScanAndSpeakAllWindows`) and the deferred path (`CheckPendingTexts`).
When it fires, the buffer is still recorded as "spoken" (window state +
pending queue) so the same garbage is not re-detected on later poll
ticks; only the `ScreenReader::Speak` call is suppressed. Rejections log
as `[POLL] win[N] REJECTED garbled: ...` / `[GETSTR-DEFERRED] ... REJECTED
garbled: ...`.

Signals (computed over the decoded string, only when length >= 8):
- letter<->digit transitions with no separator ("3in", "retone3", "08E")
- lowercase->uppercase transitions, i.e. random mid-word capitalization
  ("wlNVFEC", "RJtVPNR", "FNdV")
- unusual-punctuation density (* % # USD + = & _ /)
- letter ratio
- "[NameXX]" literal (decoder's marker for an out-of-range name byte)

Strong standalone triggers (any one rejects): >= 5 lower->upper
transitions, >= 4 letter-digit transitions, > 15% unusual punctuation, or
< 30% letters. Weaker signals (>= 2 lower->upper transitions, >= 2
letter-digit transitions, > 8% unusual punctuation, < 45% letters)
require any two to agree. "[NameXX]" rejects immediately.

### Why v0.17.8.1 failed and v0.17.8.1.1 fixes it

The initial v0.17.8.1 thresholds were tuned on the short canonical
sample, which trips on punctuation density and low letter ratio. The
~400-char tutorial buffer has a long letter-heavy tail that dilutes both
of those below threshold, leaving only the letter-digit-transition signal
-- and the old "require 2 signals" rule then ignored a lone signal, so
the string was spoken. v0.17.8.1.1 adds the lowercase->uppercase
transition counter (the most reliable discriminator: real dialog
capitalizes only at word starts or in all-caps words like "SeeD", so it
has 0-2; this garbage had 15+) and promotes the two transition counters
to strong standalone triggers. The long string now rejects on the
letter-digit-transition strong trigger; the short sample still rejects on
the weaker-signal pair.

### Regression safety

The filter only suppresses speech; it never alters decoded text or game
state. The strong thresholds sit far above what well-formed English
dialog of any length produces (verified against the same log: lines like
"Zell's Limit Break settings can also be changed in the Status screen",
"If Duel-Auto", and "...12 members from Squads A through D..." have zero
letter-digit and zero stray lower->upper transitions because FF8
space-separates numbers and capitalizes only at word starts). If a legit
line is ever suppressed, the BAT log names it via the REJECTED line and
the thresholds can be relaxed.

### Known remaining minor case

Very short stale blips (e.g. a 4-char `"HebL"` seen once in the
2026-05-21 log) fall under the 8-char minimum the filter needs to judge
reliably, so they are not caught. Lowering that bound risks false
positives on legit short fragments ("HP", "GF", "OK"), so it is left as-is
for now and revisited only if these short blips prove common.

### Tooling note

`filesystem:edit_file` corrupts a file when the replacement text contains
a literal dollar-sign character (it truncates and duplicates the original
content). The punctuation classifier therefore uses the hex literal 0x24
rather than the dollar-sign character. If a dollar sign is unavoidable in
future edits, use `filesystem:write_file` to rewrite the whole file
instead of `edit_file`.

### Expected BAT outcome

- Trigger a tutorial to its [TUTO] mode 10 -> 1 teardown (e.g. Zell's
  Duel limit-break tutorial in bghall_6). The dialog log should show
  `[POLL] win[0] REJECTED garbled: ...` instead of `Speaking: ...` for
  the stale buffer, and NVDA should stay silent through it.
- Regression: normal field dialog across NPCs still spoken correctly --
  the surrounding Quistis / Seifer / Cid conversation in the same scene
  should read normally, and legit tutorial lines ("Zell's Limit Break
  settings...", "If Duel-Auto") should still be announced.

## v0.17.8.0

Fire Cavern playthrough bug list (Aaron's 2026-05-18 report) — chapter open.
v0.17.7.6.2 closed item #2 (manual field nav direction lag). This build
addresses items #5 (GF breakpoint diagnostic spam) and #6 (damage not
announced when a character is summoning and the GF takes damage in place)
together in one build per Aaron's request — both are GF-related and the
fixes don't interact. Item #3 (tutorial TTS garbage) ships separately in a
follow-up build. Items #1 (Quistis FMV race) and #4 (party-as-NPC in
two-member parties) remain deferred.

### Bug #5: GF-BP diagnostic spam

The v0.10.91 GF dispatch hunt diagnostic (`GF_BP_AutoArm` in
`battle_tts_ewm_bp_diag.inl`) arms a hardware write/read breakpoint on
the GF display timer at 0x01D769D6 when timer<=3, then captures up to
GF_BP_MAX_HITS VEH events with full register + GF-state dumps. The
investigation it supported closed in v0.10.91 when the GF fire dispatch
function entry was identified and hooked. The auto-arm path was never
removed — it still fires every GF cast, floods the battle log with 350+
`[GF-BP] #N ACCESS` lines in a fraction of a second.

**Fix:** Gate the auto-arm behind `#define GF_BP_AUTOARM_DIAG 0` in
`battle_tts_ewm_bp_diag.inl`. Wrapped the single call site in
`battle_tts.cpp` (Update() loop) with `#if GF_BP_AUTOARM_DIAG ... #endif`.
The VEH handler (`GF_BP_VectoredHandler`), `GF_BP_ArmAllThreads`, and
`GF_ScanForFunctionEntry` remain compiled in so the diagnostic can be
re-armed by flipping the define to 1 without restoring removed code.

### Bug #6: GF damage missing during GF-HP-SUB window

Two defects compounded:

1. **`PollGFSummonState()` was dead code.** Defined in
   `battle_tts_hp.inl` since v0.13.47, never called from `Update()`. The
   v0.16.5.2 BAT analysis noted "HP-TRACK doesn't watch the GF HP
   address while GF-HP-SUB is active" — the polling logic existed but
   wasn't wired in. Confirmed via exhaustive search across all
   `battle_tts*.inl` files; the only references are the function
   definition itself and two comments in `menu_poll.inl` /
   `battle_tts_hp.inl` describing what it would do if called. Wired into
   `Update()` adjacent to `PollHPChanges()` with matching
   `s_inBattle && s_initAnnounceDone && s_enemyAnnounceDone` guards.

2. **Predicate mismatch with `AnnouncePartyMemberHP`.** Once running,
   `PollGFSummonState` gated GF HP tracking on `nowSummoning` alone
   (`IsSlotSummoningGF` reads entity+0x7C). That engine flag is
   unreliable during the HP-SUB window per the comment on
   `s_gfHpSubstitutionActive`'s declaration: "Engine flags (entity+0x7C,
   0x01D76971) are unreliable — they stay stale forever." The manual
   HP-check-key path (`AnnouncePartyMemberHP`, 1/2/3 keys) has used
   `IsSlotSummoningGF(slot) || s_gfHpSubstitutionActive[slot]` since
   v0.13.48 and correctly shows GF HP across the full HP-SUB window.
   `PollGFSummonState` now uses the same OR pattern via a `hpSubActive`
   local: damage-announce path matches HP-check-key path.

`s_gfHpSubstitutionActive[slot]` is set definitively in
`battle_tts_menu_poll.inl` when the player confirms a GF command (turn
ends with `submenuCommandId == 0x15`); cleared on the next turn for that
character or on battle exit. It covers the entire interval from command
confirm through GF animation fire, regardless of the engine flag's state.
Combined with the wiring fix, GF HP changes during the substitution
window now produce `[GF-SUMMON] <gfName> takes N damage.` log lines + TTS
announcements.

### Regression safety

**Bug #5:** Pure compile-out. Without the auto-arm call site, the BP is
never set, the VEH never fires, no behavior change other than the
absence of the `[GF-BP]` log spam. All other GF systems (timer hook,
state clamp, code patch at 0x004B04B4, FFNx hook) are untouched.

**Bug #6:** Two changes, both additive in the announce direction:
- New call to `PollGFSummonState()` from Update() runs an existing
  function that was previously dead code; it only emits TTS in cases
  where damage was previously missed entirely. Cannot suppress existing
  announcements because it writes to its own state (`s_gfHpPrev`,
  `s_gfHpTracking`) that no other code reads.
- Predicate widening from `nowSummoning` to
  `nowSummoning || s_gfHpSubstitutionActive[slot]` can only enable HP
  tracking in additional states; it never disables tracking that the
  pre-v0.17.8.0 code path enabled. `s_gfAnimFired[gs]` gating is
  unchanged — announcements still stop at animation fire (visual parity
  with the engine).

### Expected BAT outcome

- Enter a battle, summon any GF, let an enemy attack while the GF is
  loading. Battle log should NOT show any `[GF-BP] #N ACCESS` lines for
  the cast; should show `[GF-SUMMON] HP baseline for slot N: <gfName>
  M HP` at HP-SUB window open, followed by `[GF-SUMMON] <gfName> takes K
  damage.` lines as the enemy hits. TTS should announce the GF damage in
  the player's voice.
- Regression sanity: non-GF damage (Squall hit by Bite Bug) still
  announces via `PollHPChanges` -> `FlushHPAnnouncements` -> `[HP-TRACK]`
  with no change.
- Carry-over: v0.17.7.6.2's "Camera not yet calibrated" + "Camera
  calibrated" announcements on degenerate-CA fields still fire correctly.

## v0.17.7.6.2

Follow-up to v0.17.7.6.1 BAT on bgroad_5. The calibration math + threshold
reduction both worked correctly: when Aaron walked manually, `[NAV-CAL]`
fired after just 2 samples (down from 3). BUT auto-drive still failed to
reach the dormitory because AD's wrong-direction injection (kb=R east)
pushed the player straight into a wall.

### Root cause of the v0.17.7.6.1 failure

The v0.17.7.6.1 design assumed AD's own injected keys would seed the
calibration. That's true when AD's wrong-direction movement still
produces measurable forward progress. It's NOT true when the wrong
direction is blocked by a wall:

- 22:58:58 Aaron triggers AD to dormitory exit
- 22:58:59-22:59:01 AD injects `kb=R lX=995 lY=93` (strong east)
- 22:59:00 `[drive] tick=120 ... moveDist=0` -- **player not moving**
- Observer's 50-unit movement gate filters out all samples
- Calibration cannot fire from AD-injected presses
- AD thrashes through recovery phases and eventually gives up
- Aaron walks manually (UP, LEFT, UP) -- THESE moves produce real
  movement because Aaron presses keys he knows align with the visible
  hallway geometry
- 22:59:15 `[NAV-CAL]` fires after 2 UP samples

The v0.17.7.6.1 catch-22 mutated into a new shape: AD pushes into wall
-> no movement -> no samples -> no calibration -> AD continues into wall.

### Fix (Option A: block AD until calibrated)

When Aaron presses the AD trigger (backslash) on a field where
`s_camAxesSource == "identity"` and `!s_camAxesEmpiricalApplied`, AD
does NOT start. Instead, TTS announces:

> *"Camera not yet calibrated. Press an arrow key briefly to calibrate,
> then try again."*

Aaron walks a few steps with arrow keys (which DO produce movement
because he's pressing keys aligned with visible geometry). The observer
samples it. After 2 samples agree, `[NAV-CAL]` fires and TTS announces:

> *"Camera calibrated."*

Aaron retries AD. With corrected axes, AD now works normally.

### Changes

1. **`field_nav_handlekeys.inl`** -- new refusal case in the AD-start
   chain, between the dialog-open check and the target-validation block.
   Fires when `strcmp(s_camAxesSource, "identity") == 0 &&
   !s_camAxesEmpiricalApplied`. Speaks the calibration-needed message and
   logs `[drive] REFUSED (camera axes not yet calibrated: source=identity,
   pending empirical correction)`.

2. **`field_nav_observe.inl`** -- `ObsApplyEmpirical` adds a single
   `ScreenReader::Speak("Camera calibrated.")` call at the end, after
   the existing `[NAV-CAL]` log line. Fires once per field load (the
   `s_camAxesEmpiricalApplied` lock prevents repeats).

### Regression safety

The AD refusal gate is gated on `s_camAxesSource == "identity"`. On
CA-valid fields the source is `"ca-quantized"` and the strcmp returns
non-zero, so AD starts as before -- byte-for-byte identical to v0.17.7.6.1
and earlier on those fields. After calibration applies on a degenerate
field, source becomes `"empirical-corrected"`; the gate stops firing.

The TTS announcement at `[NAV-CAL]` is additive. No existing logic
changes; just a new Speak call.

v0.17.7.6.1's other changes are unchanged in this build: the two-tier AD
gate in the observer still allows sampling during AD on degenerate-CA
fields (in case Aaron triggers AD before reading the TTS prompt and AD
then produces movement somehow), and `EMPIRICAL_MIN_SAMPLES` remains at
2 (the threshold reduction was independently useful).

### Expected BAT outcome

**Primary (bgroad_5):**
- Aaron enters bgroad_5. `[NAV-PROJ-INIT] source=identity` logged.
- Aaron triggers AD (backslash) immediately.
- TTS announces "Camera not yet calibrated. Press an arrow key briefly
  to calibrate, then try again."
- AD does not start. `[drive] REFUSED` line logged.
- Aaron presses UP for ~1 second.
- Observer samples (1 UP). Throttle waits.
- Aaron continues UP, second sample fires.
- `[NAV-CAL]` fires. TTS announces "Camera calibrated."
- Aaron triggers AD again.
- AD starts with corrected axes; goes the right direction; reaches
  dormitory.

**Regression sanity:**
- bghall_1, bg2f_2, other CA-valid fields: no behavioral change. AD on
  those fields starts immediately as before.
- bgroad_5 entered, Aaron walks before triggering AD: same as above
  but the refusal message never fires (calibration applied first).

### Open question for later

If the calibration-walk-then-retry-AD UX becomes annoying after Aaron
lives with it, v0.17.7.6.3 could add a one-shot synthetic look-around at
field load on degenerate-CA fields (inject a brief UP arrow before the
player has a chance to act). Deferred unless Aaron asks for it.

## v0.17.7.6.1

Follow-up to v0.17.7.6 BAT on bgroad_5. The empirical calibration math
worked correctly when it fired (the `[NAV-CAL]` line at 22:12:59 produced
`camRight=(0.000,-1.000) camDown=(-1.000,0.000) det=-1.000`, which matches
the .ca file's axis2=+X and the screenshot's visual hallway orientation),
but the calibration didn't fire until **80 seconds** after field entry --
long enough for Aaron to receive wrong-direction guidance ("east 12 steps"
when he needed to go north), trigger auto-drive that went the wrong way
for 53 seconds, disengage AD, try manual GPS, give up on guidance, and
finally walk manually until the observer caught enough samples.

### Root cause

The v0.17.6's NAV-OBSERVE has an existing gate that suppresses sampling
during any auto-drive (so AD's synthetic key injection doesn't pollute the
diagnostic log). v0.17.7.6 kept that gate but used the observer's samples
as calibration input. On a degenerate-CA field that's a catch-22:

1. Field loads with identity-fallback axes (wrong by 90 degrees on bgroad_5).
2. Aaron triggers AD; AD projects through identity axes; goes wrong direction.
3. Observer is suppressed (AD-gate); no samples accumulate.
4. Calibration can't fire; AD continues wrong direction.
5. Aaron disengages AD; observer starts sampling; calibration fires after
   ~5 seconds of clean manual walking.
6. By then Aaron has walked into walls, tried manual GPS (also wrong), and
   become understandably frustrated.

F11 screenshot taken at 22:12:54 confirmed visually: the hallway runs
into the screen (vanishing point at center-back), so screen-up = north
is the right direction; "east 12 steps" would have walked Aaron sideways
into a pillar.

### Fix

Three small changes:

1. **`field_nav_observe.inl`** -- two-tier auto-drive gate. Chase drive
   continues to fully suppress the observer (it has its own empirical
   calibration loop). Regular auto-drive ALSO suppresses the observer
   EXCEPT when `s_camAxesSource == "identity"` and the empirical
   correction is still pending. In that specific case, the observer runs
   during AD; AD's own keyboard injection seeds the calibration the same
   way Aaron's hand would (GetAsyncKeyState reflects SendInput-injected
   state). Once the correction applies (`s_camAxesEmpiricalApplied`
   becomes true and `s_camAxesSource` becomes `"empirical-corrected"`),
   the gate returns to its original v0.17.7.6 behavior.

2. **`field_nav_observe.inl`** -- `EMPIRICAL_MIN_SAMPLES` lowered from 3
   to 2. With the existing per-sample filters (single-arrow only, 18-tick
   hold, 100-unit movement floor, 10-degree consensus threshold) two
   samples already give high confidence: v0.17.7.6 BAT showed all three
   bgroad_5 measurements landed at exactly `(1.000, 0.000)`, well within
   threshold. Two-sample consensus halves time-to-correction (~3 seconds
   instead of ~5 under throttle).

3. **`field_nav_fieldscripts.inl`** -- the `[NAV-PROJ-INIT]` log line
   hardcoded `source=ca-quantized` regardless of which branch of the
   CA-load block ran. On bgroad_5 (degenerate CA) the code correctly set
   `s_camAxesSource = "identity"` but the log line still wrote
   `source=ca-quantized`, making BAT debugging harder than necessary.
   Read the actual `s_camAxesSource` value into the log line instead.

### Regression safety

The `degenerateCaPendingCal` gate is the only behavior change that
affects non-bgroad_5 fields. It's gated on `s_camAxesSource == "identity"`
-- only fields where the CA file's 2D projection is degenerate (`d2len <
0.001f` in the existing check). On those fields, AD was already failing
(v0.17.7.6 BAT proved this). On all other fields (CA valid, source =
"ca-quantized" or "empirical-corrected"), the AD-gate behaves identically
to v0.17.7.6: observer suppressed during AD, no change to AD direction
injection, no change to GPS, no change to anything else.

The sample-count change (3 -> 2) does affect any field that triggers
empirical calibration. The risk profile: a 2-sample consensus is
somewhat weaker than 3-sample. Mitigations: each sample requires 18-tick
hold + 100-unit movement + single-arrow + 10-degree agreement with mean.
A single anomalous sample (player on a moving platform, scripted move)
would need a SECOND anomalous sample landing within 10 degrees of the
first to apply a wrong correction. The probability of two such samples
in sequence is very low; one-off scripted movements are rare on entry
to a degenerate-CA field that the player will then navigate manually.

### Expected BAT outcome

**Primary test (bgroad_5 retry):**
- Aaron enters bgroad_5.
- `[NAV-PROJ-INIT]` log now reads `source=identity` (was wrongly
  `ca-quantized`).
- Aaron triggers AD to dormitory exit. AD starts going wrong direction.
- Within ~3 seconds (2 NAV-OBSERVE samples), `[NAV-CAL]` fires from
  AD's own injected key state.
- AD re-projects on next tick with corrected axes; starts going right
  direction.
- Total wrong-direction time: ~3 seconds (versus 80 in v0.17.7.6).
- AD completes successfully.

**Regression sanity:**
- bghall_1 / bg2f_2 / other CA-valid fields: no behavioral change.
  Observer still suppressed during AD; AD still uses CA-quantized axes;
  no `[NAV-CAL]` fires.
- bgroad_5 entered, AD not triggered, Aaron walks manually: same as
  v0.17.7.6 (observer samples normally, calibration fires after 2
  samples instead of 3).
- Log files: cleaner `source=...` reporting in `[NAV-PROJ-INIT]` lines.

## v0.17.7.6

Closed-loop empirical camera-axes calibration. Fixes auto-drive direction
confusion on fields where the .ca file's 2D projection is degenerate (the
identity-fallback path). Surfaced by v0.17.7.5.4 BAT on bgroad_5 (Hallway 5),
whose `.ca` has `axis1=(0,0,-4096)` -- entirely in the depth axis -- producing
`d2len=0.000` in the existing degenerate-CA check, after which the mod fell
back to identity defaults that were wrong by 90 degrees vs. the engine's
actual screen-to-world mapping.

NAV-OBSERVE on bgroad_5 had been logging the divergence for several BAT
cycles (`arrow=UP delta=(279,0) measured=(1.000,0.000) predicted=(-0.000,1.000)
DIVERGE=90deg`) but the data was only ever logged, never fed back. This
build closes the loop.

### Mechanism

When the observer fires a NAV-OBSERVE sample AND `s_camAxesSource ==
"identity"` AND a correction hasn't already been applied this field load,
the normalized measured direction is pushed into a per-arrow ring buffer.
When the last 3 samples for the same arrow agree on a mean direction
within 10 degrees, the consensus measurement is mapped to the camera axis
the arrow corresponds to (UP/DOWN -> camDown, LEFT/RIGHT -> camRight),
quantized to nearest 90-degree world cardinal, and the orthogonal axis is
derived via the existing v0.17.5 rotation rule (`camDown = (camRight.y,
-camRight.x)`, det = -1).

Both the manual-nav pair (`s_camRightX/Y`, `s_camDownX/Y`) and the auto-drive
private pair (`s_driveCamRightX/Y`, `s_driveCamDownX/Y`) are overwritten so
all consumers (auto-drive direction injection, GPS cardinal computation,
FormatNavComponents Backspace announce) pick up the corrected values.
`s_camAxesSource` is set to `"empirical-corrected"` and
`s_camAxesEmpiricalApplied` locks the correction to one-shot per field load.

### Regression safety

Aaron's mandate: do not regress auto-drive on fields where it already works.

The entire calibration path is gated on `s_camAxesSource == "identity"`. On
any field where the CA file's 2D projection was non-degenerate, the CA-load
block in `field_nav_fieldscripts.inl` set `s_camAxesSource = "ca-quantized"`,
and the observer's calibration block (line ~302 in `field_nav_observe.inl`)
is never entered. Auto-drive, GPS, and manual nav behavior on those fields
is byte-for-byte identical to v0.17.7.5.5.

Additional safety layers:
- 3-sample consensus requirement (single bad measurement won't fire).
- 10-degree agreement threshold (mixed-direction samples won't fire).
- 100-unit minimum delta magnitude (stricter than the upstream
  OBS_MOVE_THRESHOLD=50; rejects wall-stuck noise).
- Single-arrow-only gate (already enforced by the observer's existing
  diagonal-rejection logic; defensive `ObsArrowToIdx` in the new path).
- Skipped entirely while any auto-drive or chase-drive is active
  (existing observer gate).
- One-shot lock per field load (`s_camAxesEmpiricalApplied`).

### Files touched

- `src/ff8_accessibility.h` -- version bump (1 line).
- `src/field_navigation.cpp` -- new state declarations (struct
  NavObsSample + 3 arrays/flags + 2 constants), ~25 lines plus comment.
- `src/field_nav_observe.inl` -- 3 helper functions (`ObsArrowToIdx`,
  `ObsPushSample`, `ObsCheckConsensus`, `ObsApplyEmpirical`), 3 tuning
  constants, and a ~15-line gate block at the end of `ObsLogSample` that
  feeds samples into the helpers. ~170 lines added.
- `src/field_nav_fieldscripts.inl` -- 3 lines added to the reset block
  (memset the buffer and counts, reset the applied flag).

No changes to the CA parser, the catalog, auto-drive's direction injection,
or GPS cardinal logic.

### Expected BAT results

**Primary test (bgroad_5):**
- Aaron enters bgroad_5 (Hallway 5).
- Walks a few steps (3+ steady single-arrow presses, ~5 seconds).
- The log accumulates `[NAV-OBSERVE]` lines with `axes=identity
  DIVERGE=90deg`.
- After the 3rd consistent sample, a `[NAV-CAL]` line fires with
  `EMPIRICAL CORRECTION APPLIED: camRight (1.000,0.000)->(0.000,-1.000)
  camDown (0.000,-1.000)->(-1.000,0.000) source=empirical-corrected`.
- Subsequent NAV-OBSERVE lines show `axes=empirical-corrected DIVERGE=0deg`.
- Triggering auto-drive to the dormitory exit completes successfully
  (no 90-degree direction error).
- Manual nav cardinals also come out right (they read the same axes).

**Regression sanity (must pass unchanged):**
- bghall_1: CA non-degenerate (axes are world-cardinal). No NAV-CAL
  lines fire. Auto-drive behavior identical to v0.17.7.5.5.
- bg2f_2: Same as above (det-correction from v0.17.4 still applies; no
  empirical correction triggered).
- Any field where Aaron was happy with auto-drive in .5.5: no change.

**Edge cases:**
- bgroad_5 entered, auto-drive triggered immediately (no observations
  yet): correction not applied, drive misbehaves same as v0.17.7.5.5.
  No hang or crash.
- bgroad_5 entered, only diagonal walking: diagonals filtered by
  observer; no consensus; no correction.
- Player leaves bgroad_5 and re-enters: `HookedFieldScriptsInit` resets
  the accumulator and the applied flag; correction re-applies on the
  next valid sample run.

### What's still deferred

- v0.17.7.7: SETLINE-position promotion + NPC `ResolveFriendlyName`.
- v0.17.7.8: Shop/Card Game -> NPC announce-layer collapse.
- v0.17.7.9 (optional): SYM override layer for residual leaks.
- Open design question: should the empirical correction also fire on
  non-degenerate CA fields where NAV-OBSERVE shows persistent divergence?
  Currently NO (riskier; defer until a concrete case appears).

## v0.17.7.5.5

Fix bgryo1_4 bed labeled as "Exit to B-Garden - Dormitory Double 4" — BAT'd
regression from v0.17.7.5.4. The bed is a Line whose MAPJUMP destination
resolves to the same field id the player is on (field 240 -> field 240).
Labeling it as "Exit to <the room I'm in>" is nonsense; Aaron correctly
identified it should be an Interaction.

### Diagnosis

bgryo1_4 ent0 'squall' is classified SCREEN_BOUND with destFieldId=240 by the
resolver. v0.17.7.5.4's `hasDialogReqTarget` check passed (the bed Line
doesn't itself REQ a dialog-bearing entity -- the "Sleep?" prompt is handled
by the Director entity 'seed' via different code paths). So Block 1 emitted
the Exit label.

Root cause: catalog had no self-loop detection. A MAPJUMP whose destFieldId
matches the current fieldId is an in-place state transition (sleep, fade-to-
next-day, similar), not a navigational exit. The engine's behavior on
self-loop MAPJUMPs is "reload this field, advancing some state."

### Fix

Catalog block 1 (Exits): add an early-continue when
`s_capturedLines[t].destFieldId == *FF8Addresses::pCurrentFieldId`.

Catalog block 2 (Interactions): extend the SCREEN_BOUND dual-purpose
acceptance check to ALSO accept self-loop lines (mirror condition: a line
rejected by block 1 should be picked up by block 2 to ensure it appears
somewhere in the catalog).

No scanner changes; no struct changes. The decision is made at catalog-build
time using the existing `destFieldId` + the runtime `pCurrentFieldId` pointer.
Single-file change in `src/field_nav_catalog.inl`.

### Expected BAT results

- **bgryo1_4 bed** (the BAT'd regression): catalog now shows
  `cat<n> TRIGGER line0 ... name='Interaction 1'`
  instead of `name='Exit to B-Garden - Dormitory Double 4'`.
- **Other v0.17.7.5.4 wins** (bgroad_5 dormitory exit, bghall_* exits): no
  change. Those lines have destFieldId pointing to DIFFERENT fields, so the
  self-loop check doesn't trip.
- **Genuine dual-purpose Lines** (dorm beds detected via REQ-following's
  `hasDialogReqTarget`): no change. They were already handled in block 2.
- **Any field with a SCREEN_BOUND self-loop Line that ISN'T a sleep
  transition** (hypothetical, none known): the Line will be relabeled as
  Interaction instead of Exit. This is at worst slightly imprecise; the
  previous "Exit to <same room>" label was unambiguously wrong.

### Known issue, NOT fixed in this build

v0.17.7.5.4 BAT also surfaced a longstanding camera-projection bug on
bgroad_5 (Hallway 5). The .ca file for that field has axis1=(0,0,-4096) --
entirely in the depth axis -- producing a degenerate 2D projection. The mod
falls back to identity camera axes, but the engine's actual screen-up
direction is world +X (confirmed by NAV-OBSERVE: arrow=UP produced
delta=(279,0), measured=(1.000,0.000), DIVERGE=90deg from prediction).

Consequence: on bgroad_5 the auto-drive system injects the wrong keyboard
direction (e.g. presses RIGHT when the player needs UP), so the player
gets pushed perpendicular to the target and either gets stuck against a
wall or transitions through the wrong exit. Manual GPS cardinal directions
are also affected -- the cardinal is correct ("east") but Aaron's arrow-key
mapping interpretation has to be learned empirically per field.

Fix is a closed-loop calibration: use NAV-OBSERVE empirical measurements to
overwrite camRight/camDown when the CA-derived 2D projection is degenerate.
Queued for v0.17.7.6 as its own chapter (touches projection init,
auto-drive direction injection, and possibly GPS cardinal computation).

## v0.17.7.5.4

Fix bgroad_5 (Hallway 5) catalog regression revealed by v0.17.7.5.3 BAT. Aaron heard
the dormitory exit announced as "Interaction 1" instead of "Exit to Dormitory Double 1".
All other v0.17.7.5.3 predicted resolutions confirmed working (27 PSHM-DEST entries
all matched expected fields). This build addresses one remaining false-positive in the
catalog labeling pipeline.

### Diagnosis

The `[MAPJUMP-RES]` resolver correctly identified bgroad_5 ent1 'squalls' as
SCREEN_BOUND with destFieldId=237 (Dormitory Double 1). But the catalog Block 1
("Exit to ..." labeling) skipped the line because `hasExtDispatch=true`. Block 2
("Interaction N" labeling) then picked it up and labeled it "Interaction 1".

Root cause: `hasExtDispatch` is set by two distinct mechanisms in the JSM scanner,
and the catalog conflated them:

1. **Own 0x1C extended-dispatch use** (set in opcode scan, very common): fires for
   any extended opcode -- sound effects, particle effects, animation triggers,
   anything dispatched via 0x1C with a PSHM_W or empty stack. bgroad_5 squalls
   has this from non-dialog effects.

2. **REQ to dialog-bearing entity** (set in REQ-following post-pass): the genuine
   "dual-purpose dialog-mediated exit" pattern. The Line REQs a Background that
   shows MES/ASK dialog; the MAPJUMP fires as a consequence of the player
   choosing yes. Example: dormitory beds ("Sleep?" -> next-day field).

The catalog used mechanism #1's signal to decide "this is dual-purpose". For
bgroad_5 squalls (mechanism #1 only, no dialog REQ), this incorrectly suppressed
the Exit label.

### The fix

Split the conflated flag into two:
- Keep `hasExtDispatch` (own 0x1C usage) -- unchanged semantics.
- Add `hasDialogReqTarget` -- set ONLY by REQ-following when REQ target has
  dialog or extended dispatch.

Catalog Blocks 1 and 2 now use `hasDialogReqTarget` for the dual-purpose check.
Lines with own 0x1C usage but no dialog REQ (the common case, including
bgroad_5 squalls) flow through Block 1 as Exits. True dual-purpose lines
(dorm beds) still flow through Block 2 as Interactions.

### Files touched

- `src/ff8_accessibility.h`: version 0.17.7.5.3 -> 0.17.7.5.4
- `src/field_archive.h`: add `bool hasDialogReqTarget;` to `JSMEntityInfo`
- `src/field_archive_jsm_scan.inl`: in REQ-following post-pass, drop the
  `if (hasExtDispatch) continue;` early-exit so REQ-following runs for ALL
  Line entities. Set `hasDialogReqTarget=true` (new flag) instead of
  `hasExtDispatch=true`. Log message updated accordingly.
- `src/field_nav_pathfinding.inl`: add `bool hasDialogReqTarget;` to
  `CapturedTriggerLine`.
- `src/field_nav_fieldscripts.inl`: reset and copy `hasDialogReqTarget` in
  the lineType assignment block.
- `src/field_nav_catalog.inl`: Block 1 (Exits) skip condition changed from
  `hasExtDispatch` to `hasDialogReqTarget`. Block 2 (Interactions) dual-purpose
  acceptance check also changed from `hasExtDispatch` to `hasDialogReqTarget`.
  Both comments updated with the v0.17.7.5.4 split rationale.

### Expected BAT results

- **bgroad_5 squalls** (the BAT'd regression): catalog now shows
  `cat4 TRIGGER line1 center=(3765,10) name='Exit to B-Garden - Dormitory Double 1'`
  instead of `name='Interaction 1'`.
- **Other v0.17.7.5.3 wins** (bghall_1 / bghall_2 / bghall_5 / bgroad_1 exits): no
  change -- those lines never had `hasExtDispatch=true` to begin with, so the
  catalog Block 1 path was already labeling them correctly.
- **Dormitory beds and similar genuine dual-purpose Lines**: no behavior change.
  REQ-following still sets `hasDialogReqTarget=true` for them (their REQ target
  has dialog), Block 1 still suppresses the Exit, Block 2 still labels them as
  Interactions.
- **REQ-interact log lines** now read `-> hasDialogReqTarget=1` instead of
  `-> hasExtDispatch=1`. Should appear for fewer lines overall because the
  scanner no longer skips lines where `hasExtDispatch` was already set by own
  0x1C usage.

### Risk

Low. The change makes the catalog's dual-purpose detection more precise. Worst
case: a hypothetical Line that uses extended dispatch internally AND is genuinely
dialog-mediated WITHOUT REQing a dialog entity. No known field exhibits this
pattern -- dorm-bed dialogs go via REQ, not via the Line's own 0x1C dispatch.

## v0.17.7.5.3

The v0.17.7.5.2 BAT confirmed the addr-as-literal pattern across 8 fires. Implementing the fix.

### What the .5.2 BAT showed

Two engine fires captured:
- bghall_2 -> 185 (Quad 4), inline_param=0x0188 (392), engine firing IP=1722
- bghall_5 -> 224 (Hallway 1), inline_param=0x0039 (57), engine firing IP=2705

Cross-referenced via inline_param to resolver entries:
- bghall_2 zell m7 ip=2639/2646/2653 all have inline_param=0x0188; resolver picked VARBLOCK 0x00B9 (= 185)
- bghall_5 selphie m7 ip=4348/4355 both have inline_param=0x0039; resolver picked VARBLOCK 0x00E0 (= 224)

Bytecode context dumps for selphie's two MAPJUMP3 instructions:

```
ip=4348 ctx: -7=JMP+9 -6=JMPB-8 -5=PSHM_W 0xE0 -4=PSHM_W 0x194 -3=PSHM_W 0x000
              -2=PSHM_W 0xFFFFFF00 -1=PSHM_W 0xC0  *+0=MAPJUMP3 0x39  +1=JPF+7

ip=4355 ctx: -7=MAPJUMP3 0x39 -6=JPF+7 -5=PSHM_W 0x9A -4=PSHM_W 0x248
              -3=PSHM_W 0xFFFFF844 -2=PSHM_W 0x000 -1=PSHM_W 0x74  *+0=MAPJUMP3 0x39  +1=op 0x06
```

VM stack at hook entry (bghall_5 fire): `sp=12 [12]=192 [11]=65280 [10]=0 [9]=404 [8]=224=destField`.

The five VM stack values match the five PSHM_W param values *exactly in decimal*:

| Bytecode | Param (hex) | Param (decimal) | VM stack | Match |
|---|---|---|---|---|
| PSHM_W 0xE0   | 0xE0      | 224  | [8]=224   | yes |
| PSHM_W 0x194  | 0x194     | 404  | [9]=404   | yes |
| PSHM_W 0x000  | 0x000     | 0    | [10]=0    | yes |
| PSHM_W 0xFFFFFF00 | 0xFF00 (low16) | 65280 | [11]=65280 | yes |
| PSHM_W 0xC0   | 0xC0      | 192  | [12]=192  | yes |

The varblock dump at the same instant shows `varblock[0x00E0]=255`. Not 224. So PSHM_W did NOT read the varblock value of 255 -- it produced 224. Either:

1. **Pattern is intentional self-documenting bytecode.** B-Garden's script authors chose pshmAddr = destField for readability. The PSHM_W instruction reads varblock[addr], but the varblock at byte-offset = destField holds value = destField at method-7 execution time (some setup we haven't located populates it between field-load and the line's MAPJUMP3 firing).
2. **Pattern is coincidental.** Engine populates varblock[X] = X for some init range; PSHM_W's value happens to equal its address.

Either way the runtime behavior is consistent: `destField == pshmAddr in decimal`.

### Why this didn't show up earlier

v0.17.7.x had tried reading varblock at field-load time (in the `[PSHM-DEST]` resolver). That fails because the varblock-population step happens later. Results were:
- Varblock=0 -> kept marker -> line stayed bare in catalog
- Varblock=255 (random stale value) -> coincidentally close to the field-id range -> line labeled as field 255 (Headmaster's Office 7), which was wrong (actual was 185 for bghall_2 zell)

The v0.17.7.5.2 contiguous varblock 0x80-0xFF dump showed that varblock entries at SCREEN_BOUND-line PSHM addresses do NOT hold the destField at field-load. The match between VM stack and PSHM param can only be explained by the pattern described above.

### The fix

Single change in `field_nav_fieldscripts.inl`, the `[PSHM-DEST]` block:

**Before** (~24 lines): read `*(int16_t*)(0x1CFE9B8 + pshmAddr)` and use that as resolvedId.

**After** (~20 lines including comments): treat `pshmAddr` (the low 16 bits of the marker) directly as the destField. No varblock read.

```cpp
if (pshmAddr > 0 && pshmAddr < FIELD_DISPLAY_NAMES_COUNT) {
    rawParam = (int)pshmAddr;
}
```

Net: roughly the same line count. Removed the `__try` block (no longer reading memory). Added an extensive comment explaining the empirical pattern.

### Expected catalog changes (predicted from resolver output)

| Field | Line | Old label | New label |
|---|---|---|---|
| bghall_1 | squalls, squallsd, zell | bare "Exit" | "Exit to Hall 11" (x3) [needs BAT confirmation] |
| bghall_2 | squallsd | bare | "Exit to Hall 1" |
| bghall_2 | zell | "Exit to Headmaster's Office 7" (wrong) | "Exit to Quad 4" (correct, BAT'd) |
| bghall_2 | zells | bare | "Exit to Hallway 4" (BAT'd) |
| bghall_5 | selphie | bare | "Exit to Hallway 1" (BAT'd) |
| bghall_5 | irvine | bare | "Exit to Hall 6" (BAT'd) |
| bghall_5 | zell | bare | "Exit to Hallway 5" |
| bghall_5 | zells | bare | "Exit to Hallway 2" |
| bgroad_1 | squall | bare | "Exit to Cafeteria 1" (BAT'd) |
| bgkote_3 | (lines) | varies | varies (mixed -- bgkote_3 had different resolver picks) |

### Risks

1. **Non-B-Garden fields**: any field using PSHM_W where the address is NOT the destField in decimal will mislabel. No such case is currently known; will surface during catalog testing on later areas.
2. **bghall_1**: 3 SCREEN_BOUND lines all pick `addr=0x00AF=175`. After this fix they all label as "Exit to Hall 11". If wrong, we'll see it; revisit then.
3. **Fallback removed**: lines where the field-load varblock read previously gave a coincidentally-correct value (none confirmed) lose that path. Acceptable risk -- the addr-as-literal interpretation is empirically more reliable.

### Files

- `src/ff8_accessibility.h` -- version `0.17.7.5.2` -> `0.17.7.5.3`
- `src/field_nav_fieldscripts.inl` -- `[PSHM-DEST]` block replaces varblock read with addr-as-literal; ~3 KB added (mostly comments). File size: 68.09 KB (in watch zone, well under 80 KB hard limit).
- `CHANGELOG.md`, `DEVNOTES.md`, `NEXT_SESSION_PROMPT.md` -- updated

No new files. `deploy.bat` does NOT need updating (only one source touched, already listed).

### BAT recipe

1. Build and launch. Verify version line `v0.17.7.5.3` in startup log.
2. Walk to bghall_2 and bghall_5. Cycle through their catalogs (F9/F10) on each field and check the SCREEN_BOUND line labels:
   - bghall_2: should announce "Exit to Hall 1", "Exit to Quad 4", "Exit to Hallway 4" for the three exits
   - bghall_5: should announce "Exit to Hallway 1", "Exit to Hallway 2", "Exit to Hallway 5", "Exit to Hall 6"
3. (Optional) Walk through bghall_1, bgroad_1 and any other reachable B-Garden hall fields to spot-check labels and catch the bghall_1 case if it's wrong.
4. Send `Logs/ff8_field.log`.

### Followups (not blocking)

- **v0.17.7.6 (next planned)**: wire confirmed destField into catalog labeling for SETLINE-position promotion + NPC `ResolveFriendlyName`. This makes the new destField info actually surface in the catalog announce path. (The .5.3 fix produces correct destFieldId values; the consumer pipeline still needs to integrate them at the catalog labeling step.)
- **bghall_1 BAT**: needed to confirm or refute the "all 3 lines -> Hall 11" labeling.
- **Engine firing IP mystery (deferred)**: in the .5.2 BAT, engine firing IP=2705 did not equal selphie m7 ip=4348 (difference 1643). This was not needed for the fix (inline_param + bytecode-context cross-reference was sufficient), but the IP mismatch is unexplained. Possible: engine maintains a separate bytecode copy with different base. Not blocking; leaving as an open question.

## v0.17.7.5.2

Diagnostic-only build to break the deadlock that v0.17.7.5.1 surfaced. v0.17.7.5.1 BAT confirmed two facts that together rule out both prior hypotheses:

1. **No non-SCREEN_BOUND entity fires MAPJUMP.** Widening the resolver to scan every entity type produced no LITERAL resolutions outside the four SCREEN_BOUND lines. The 4 traversed-line scans went from 5 MAPJUMP3 instructions to 9 (each line has 2-3 MAPJUMP3s in method 7), but only SCREEN_BOUND lines have them. 35 of the 39 entities on bghall_5 have zero MAPJUMP-family instructions.
2. **No varblock value in 0x80-0xFF matches the engine destField.** For bghall_5 -> 224, the contiguous dump shows varblock[0x00E0]=255 and varblock[0x00E2]=255 at fire time, no value=224 anywhere in the dumped range. The engine pushes destField=224 from somewhere, but not from where my resolver predicted.

### Inferred bytecode structure

Every SCREEN_BOUND line has TWO adjacent MAPJUMP3 instructions ~7 IP slots apart, with consistent shape across all four lines on each field:

| Field | Line | MAPJUMP3 #1 (resolver picks) | MAPJUMP3 #2 (resolver underflows) |
|---|---|---|---|
| bghall_2 | squallsd | ip=2562 VARBLOCK 0x00A5 | ip=2569 sp=4 need 5 |
| bghall_2 | zells    | ip=2714 VARBLOCK 0x00E3 | ip=2721 sp=4 need 5 |
| bghall_5 | selphie  | ip=4348 VARBLOCK 0x00E0 | ip=4355 sp=4 need 5 |
| bghall_5 | irvine   | ip=4760 VARBLOCK 0x00AA | ip=4767 sp=4 need 5 |

Ground truth from the bghall_5 -> 224 fire: varblock[0x00E0]=255 at fire time but engine destField=224. The first MAPJUMP3 (the one my resolver picked) therefore CANNOT be firing -- if it were, the destField on the VM stack would be 255. The second MAPJUMP3 (the one that underflows) must be firing, with a LITERAL destField push of 224.

The pattern: each line carries two destField encodings -- a varblock-driven path (used during scripted story scenes that override default destinations) and a literal-driven default. For Aaron's Disc 1 playthrough, the game-state flag is clear, so the literal branch runs. The address-equals-destination-in-decimal pattern is not coincidence: the two encodings represent the SAME logical destination (`PSHM_W 0x00E0` reads varblock at byte offset 224, and `PSHN_L 224` pushes 224 directly -- both equal 0xE0 in hex).

The v0.17.7.5.1 LITERAL-preference policy is the right policy. It just doesn't trigger because the second MAPJUMP3 underflows at sp=4 (one push missing) before the LITERAL push can land on the abstract stack.

### Why the underflow happens

One push opcode in the 6-slot gap between the two MAPJUMP3s isn't modeled in `GetStackEffect`. bghall_5's opcode histogram lists 0x11 (16x), 0x13 (4x), 0x1F (12x), 0x32 (21x) as candidates not covered by my resolver's table OR by the existing forward scanner's switch (both have the same `default: break` blind spot).

The existing forward scanner has worked anyway for position extraction (SET3) because that only needs the most recent 4 pushes -- those always fit in the 8-deep cap and aren't affected by older truncated entries. MAPJUMP3 fails specifically because the destField is the 5th-from-top push (the deepest), which is exactly what an under-modeled stack effect clobbers.

### Pre-build assembly verification

Before implementing the diagnostics, verified from FF8_EN.exe disassembly:

- `[esi + 0x176]` is the IP at hook entry. Dispatcher at 0x0052A621 reads it before decoding the bytecode word, increments at 0x0052A675 after dispatch returns. Reading it at hook entry safely gives the firing opcode's IP.
- `opcode_pshm_w` at `0x0051CB30` confirmed to read `*(uint16_t*)(0x01CFE9B8 + inline_param)` and run through a no-op saturation. No literal-passthrough trick.
- Dispatch table at `0x00B8DE94` is in `.data` (28 MB section), not accessible from my `.text`-only disassembly dumps. Cannot directly identify handlers for opcodes 0x11/0x13/0x1F/0x32 without another BAT cycle. The function index doesn't help either: handlers are indirect-call-only via the dispatch table, so they don't appear as direct call targets.

### Three diagnostics in this build

**Resolver: inline_param on every per-instruction log line.** Each `[MAPJUMP-RES]` log now includes `inline_param=0x%04X` so resolver entries can be matched to runtime MAPJUMP-HOOK fires by inline_param. (The hook already logs inline_param; both sides finally speak the same language.)

**Resolver: bytecode context dump per scanned MAPJUMP3.** New `[MAPJUMP-CTX]` log line for every scanned MAPJUMP-family instruction, 9 dwords wide (ip-7 through ip+1). Lets us decode the basic-block structure between MAPJUMP3 #1 and #2 by hand and identify the missing push opcode without further BAT cycles after this one.

**Hook: firing IP read from `[ctx + 0x176]` at hook entry.** New `firing IP=N (0x%04X)` log line in the MAPJUMP-HOOK output. The IP is method-relative (not absolute scriptData index), so direct comparison with resolver's `m7 ip=4348` needs the method's start IP; but inline_param uniquely identifies the bytecode word, so pairing by inline_param is sufficient for triage. The IP is logged anyway for safety -- if inline_params collide we can still differentiate.

No catalog change. No code path change other than logging.

### Files

- `src/ff8_accessibility.h` -- version `0.17.7.5.1` -> `0.17.7.5.2`
- `src/field_archive_jsm_mapjump_resolver.inl` -- header bumped, `DumpBytecodeContext` helper added, inline_param appended to all per-instruction log formats, `Run()` calls the dump for each scanned MAPJUMP
- `src/field_nav_mapjump_diag.inl` -- header bumped, `VMCTX_IP_OFFSET = 0x176` constant added, `LogMapjumpVmStack` reads and logs the firing IP before the stack dump
- `CHANGELOG.md`, `DEVNOTES.md`, `NEXT_SESSION_PROMPT.md` -- updated

`deploy.bat` does NOT need updating (.inl files are textual includes).

### BAT recipe

1. Build and launch. Sanity check `[MAPJUMP-HOOK] 5 / 5 hooks installed (installed=true)` in startup log.
2. Walk same paths as v0.17.7.5.1 (bghall_5 wing exits, bghall_2 hub exits). One traversal per line is enough.
3. Send `Logs/ff8_field.log`.

### What I'll do with the log

1. Match the engine fires' inline_param (e.g. `inline_param=57` for bghall_5 -> 224) to a specific `[MAPJUMP-RES]` per-instruction entry on the same field. Confirm which of the two MAPJUMP3s in selphie's m7 the engine actually fires.
2. Read the `[MAPJUMP-CTX]` bytecode dump for that firing IP. Decode the 9 dwords by hand using the opcode format spec (high byte = opcode, low 24 bits = param; high byte == 0 means literal push). The dwords leading up to the firing MAPJUMP3 give us the 5 pushes the engine actually executes, including whatever opcode my resolver is failing to model.
3. v0.17.7.5.3 will be a simple `GetStackEffect` table extension once we know which opcode is missing -- probably 5-10 lines of code.

### Two safety properties

- Build with no game-state-mutating changes. The resolver runs at JSM scan time (post field load), the hook runs in the existing dispatch path. Both already worked in v0.17.7.5.1; this build only adds log output.
- No new files, no new entries to `deploy.bat`. `.inl` files are textually included from existing `.cpp` parents.

## v0.17.7.5.1

Iterative refinement of v0.17.7.5 based on the BAT. The static resolver fires and replaces the forward scanner's wrong markers with new ones, but exits are still mostly unlabeled because the resolver's new markers (e.g. bghall_5 irvine -> PSHM_W 0x00AA) read 0 from the varblock at field load AND at MAPJUMP3 fire time -- yet the engine's actual destField is 170. The resolver is therefore identifying MAPJUMP3 instructions that aren't the ones the engine actually fires for those traversals.

This build widens both the resolver and the hook diagnostic to disambiguate.

### What v0.17.7.5 BAT revealed

- Resolver fired correctly on 9 field loads, ~95 [MAPJUMP-RES] entries total.
- Engine RESULT (ground truth from `*(uint16_t*)0x01CE4762` after handler return) for four traversals:
  - bghall_2 -> 165 (B-Garden Hall 1)
  - bghall_2 -> 227 (B-Garden Hallway 4)
  - bghall_5 -> 224 (B-Garden Hallway 1)
  - bghall_5 -> 170 (B-Garden Hall 6)
- VM stack `[8]=destField` slot matched the engine RESULT in all 4 traversals (the disassembly trace of opcode_mapjump3 at `0x00521AC0` is correct).
- Resolver-picked PSHM addresses for the four bghall_5 SCREEN_BOUND lines: zell@0xE4, zells@0xE1, selphie@0xE0, irvine@0xAA. None of those varblock slots hold the engine's actual destFields at fire time -- e.g. varblock[0x00AA] = 0 at fire time for the bghall_5 -> 170 transition, but engine destField = 170.
- Disassembly of `opcode_pshm_w` at `0x0051CB30` confirms PSHM_W really reads `*(uint16_t*)(0x01CFE9B8 + inline_param)` and passes through a no-op saturation. There's no special-case where it pushes the address as a literal.

Conclusion: the engine fires MAPJUMP3 from entities OTHER than the four SCREEN_BOUND lines my resolver scanned. Likely candidates: Event Trigger lines (bghall_5 ent6/7/8: selphies/quistis/rinoa) classified as JSM_ENT_LINE_EVENT, Background scripts, or Other entities dispatched via REQ from a Line.

### Changes in v0.17.7.5.1

**Resolver widening**: drops the `info.type == JSM_ENT_LINE_SCREEN_BOUND` filter. The resolver now walks the bytecode of every entity, logging `[MAPJUMP-RES]` for every MAPJUMP-family instruction found. `info.param` is still ONLY overwritten for SCREEN_BOUND lines (the downstream `[PSHM-DEST]` resolver consumes that field for catalog labeling); other entities produce diagnostic log lines only.

**LITERAL preference**: when an entity has multiple MAPJUMP-family instructions resolving to a mix of LITERAL and VARBLOCK, the resolver now prefers the LITERAL. A LITERAL is statically self-contained (no varblock lookup needed) and trumps a VARBLOCK marker that might read garbage at field-load time. Previously the resolver took the first valid resolution regardless of kind.

**Contiguous varblock dump 0x80-0xFF**: the MAPJUMP-HOOK now logs an additional set of varblock entries covering the 0x80-0xFF range in four lines of 16 entries each (only non-zero entries are shown to keep log volume sane). This range covers all of the resolver's recently-picked PSHM addresses, so we can finally see what's at varblock[0x00A5], [0x00AA], [0x00E0], [0x00E3], etc. at MAPJUMP3 fire time.

### Two distinguishing outcomes for the BAT

**Outcome A** -- the firing entity is non-SCREEN_BOUND. The new `[MAPJUMP-RES]` lines will show extra MAPJUMP-family instructions in EVENT trigger lines (entity type "Event Trigger") or other entity classifications, with LITERAL destFields matching the engine's actual destinations (165, 224, 227, 170). Fix in v0.17.7.5.2: widen the classification logic so those entities are surfaced as exits.

**Outcome B** -- the firing entity IS a SCREEN_BOUND line, but the varblock at the resolver's address holds the right value at fire time (just not at field load). The new contiguous dump will show value=170 at some varblock address near 0x00AA at the bghall_5 -> 170 fire, value=224 at some address near 0x00E0, etc. Fix in v0.17.7.5.2: capture varblock values at FIRST fire time and use those for subsequent labeling (session cache pattern).

Most likely outcome A based on the v0.17.7.5 data (the address-decimal-equals-destination pattern is striking but unlikely to hold across all entity types if checked).

### Files

- `src/ff8_accessibility.h` -- version `0.17.7.5` -> `0.17.7.5.1`
- `src/field_archive_jsm_mapjump_resolver.inl` -- drop SCREEN_BOUND filter, add LITERAL preference, distinguish param-update vs diagnostic-only log lines
- `src/field_nav_mapjump_diag.inl` -- header bumped, contiguous varblock 0x80-0xFF dump added (4 log lines, non-zero entries only)
- `CHANGELOG.md` -- this entry
- `DEVNOTES.md` / `NEXT_SESSION_PROMPT.md` -- updated

### BAT recipe

1. Build and launch.
2. Same walks as v0.17.7.5 (bghall_5 wings, bghall_2 hub exits, etc.).
3. Send `Logs/ff8_field.log`.

### What to look for in the log

**At field load** (e.g. bghall_5):
- Look for `[MAPJUMP-RES] ... (Event Trigger): would-be param 0x000000AA [LITERAL]` (or similar) on `selphies`, `quistis`, `rinoa`. If present, those are the firing entities and the LITERAL value is the destFieldId.
- Look for `[MAPJUMP-RES] ... (Background): ...` lines too -- some fields may dispatch via background scripts.

**At MAPJUMP3 fire time**:
- The new `varblock(0x0080-0x00AE) ... varblock(0x00E0-0x010E)` lines show all non-zero values in those ranges. Cross-reference: `engine RESULT: destField=170` -- does varblock[some_addr_near_0xAA] hold 170 at this moment? If yes, outcome B; we have a timing problem.

### Known not-fixed

- Catalog still shows bare `Exit` for SCREEN_BOUND lines. We don't ship catalog changes until BAT confirms which entity actually fires AND we have a confirmed-correct destField for it.
- All Track B carry-over items remain deferred.

## v0.17.7.5

First static-resolution build for SCREEN_BOUND line destinations. Adds the MapjumpResolver pass that re-walks each line's bytecode method-by-method with proper basic-block awareness, and extends the v0.17.7.4 MAPJUMP-HOOK to read the engine's actual VM stack args and resolved destField global so the resolver's output can be cross-checked against ground truth.

No catalog change yet -- the resolver writes its result into `JSMEntityInfo::param` (replacing the forward scanner's incorrect marker) and the existing downstream `[PSHM-DEST]` path in `HookedFieldScriptsInit` consumes it as before. We turn the resolver's output ON for catalog labeling AFTER the validation log confirms predicted == actual on the bghall_5 / bghall_3 wing exits Aaron walked in the v0.17.7.4 BAT.

### What the v0.17.7.4 BAT revealed

Aaron walked B-Garden halls. MAPJUMP3 fired twice (bghall_5 -> bghall_3 with inline_param=175, bghall_3 -> bghall_1 with inline_param=111). The hook captured varblock state at fire time -- but the destination wasn't in any of the 10 PSHM addresses the scanner had reported. The scanner-reported source for the zell line said "destField from PSHM_W 0x0002", and varblock[0x0002] = 14381 (0x382D) at fire time, while the actual destination was 170. There is no plausible arithmetic that maps 14381 to 170, so the scanner is just looking at the wrong push entirely.

### Why the existing forward scanner picks the wrong push

Disassembly of `opcode_mapjump3` @ `0x00521AC0` confirms the destField is the deepest of the 5 args MAPJUMP3 pops -- i.e. `pushStack[pushCount - 5]` per the scanner's own indexing. The indexing is correct in principle. Two specific bugs in `field_archive_jsm_scan.inl` desynchronize the simulated stack from the real VM:

1. **No stack reset at basic block boundaries.** Jumps (`0x01` JMP, `0x03` JMPB, `0x04` JMPF) hit `break;` without resetting `pushCount`. Pushes accumulate across labels and the 8-deep `pushStack` cap truncates older entries.
2. **Most opcodes have unmodeled stack effects.** The scanner explicitly models PSHM_W, POPM_W/L, JPF, REQ family, and control-flow opcodes. Everything else is `default: break;` -- stack untouched. bghall_5's opcode histogram shows 135x 0x14, 35x 0x1A, 31x 0x1E, 21x 0x32, 19x 0x2B / 0x2C, etc., all unmodeled.

By the time the scanner reaches MAPJUMP3 at the end of a walk-on method, `pushCount` is meaningless and the slot it reports as destField is just whatever happened to be at `pushCount - 5` at that moment -- a stale PSHM_W from a branch-condition check, not the destField push.

### The new resolver

`src/field_archive_jsm_mapjump_resolver.inl` -- runs as a follow-up pass over each SCREEN_BOUND line entity after `RunDirectorDetection`. For each MAPJUMP / MAPJUMP3 / DISCJUMP / MAPJUMPO instruction in the entity's methods:

1. Pre-builds the set of jump-target IPs within the method (LBL instructions and computed targets of JMP / JPF / JMPB).
2. Forward-walks the bytecode with a 32-slot abstract stack.
3. Resets the stack at every jump target so only pushes that reach MAPJUMP3 through its own basic block count.
4. Tracks abstract values as one of LITERAL, VARBLOCK (with address), or UNKNOWN. PSHM_W / PSHM_B / PSHM_L / PSHSM_W / PSHSM_B are all tracked as VARBLOCK refs (downstream `[PSHM-DEST]` resolves them all the same way).
5. At the MAPJUMP instruction, inspects the deepest of the top-N stack values. If LITERAL -> the literal IS the destField. If VARBLOCK -> stores `0x80000000 | addr` as the marker. If UNKNOWN -> logs as unresolved and leaves `info.param` alone (the forward scanner's existing value stays).

Opcode stack effects are confirmed for MAPJUMP / MAPJUMP3 / DISCJUMP / MAPJUMPO / SET / SET3 / SETLINE / REQ family / JPF / extended dispatch / push-pop memory ops. Less-common opcodes default to {0, 0} (no stack effect) -- safer than guessing wrong; the resolver logs an UNKNOWN result in that case rather than silently producing garbage.

The resolver rewrites `JSMEntityInfo::param` in place for resolved entities. The existing `[PSHM-DEST]` resolver in `HookedFieldScriptsInit` (at field load, after varblock is populated) reads `info.param`, recognizes bit-31 PSHM markers, and reads `*(int16_t*)(0x1CFE9B8 + addr)` to convert the marker to a literal field ID. The resolver's output flows through that same path -- it just puts the CORRECT marker in `info.param` instead of the wrong one.

### Hook enhancements for validation

`src/field_nav_mapjump_diag.inl` -- each Hooked* stub now ALSO:

1. Reads the VM stack pointer at `ctx + 0x184` and dumps the 5 (or 4) args the engine is about to pop. Each slot is logged with its index, value, and a tag identifying the top and the destField slot.
2. After chaining to the original handler, reads `*(uint16_t*)0x01CE4762` (the engine's resolved destField global, written by `opcode_mapjump3` per disassembly) and logs both that value and the corresponding field name from `FIELD_DISPLAY_NAMES`.

This gives us three independent ground-truth signals per traversal:

- What the static resolver predicted at field load (`[MAPJUMP-RES]` lines from the new pass)
- What the engine actually saw on the VM stack at fire time (`[MAPJUMP-HOOK] ... VM stack:` lines)
- What the engine wrote into the transition globals after popping (`[MAPJUMP-HOOK] ... engine RESULT:` lines)

All three should agree on the destField. If they do, the resolver is correct and v0.17.7.6 can wire its output into the catalog. If they don't, the discrepancy tells us which case the resolver missed (unmodeled opcode, missed branch, varblock not yet populated, etc.).

### Files

- `src/ff8_accessibility.h` -- version `0.17.7.4` -> `0.17.7.5`
- `src/field_archive_jsm_mapjump_resolver.inl` -- NEW, MapjumpResolver namespace + Run() entry point
- `src/field_archive_jsm.inl` -- include the new resolver .inl AFTER director.inl and BEFORE scan.inl
- `src/field_archive_jsm_scan.inl` -- call `MapjumpResolver::Run(...)` after `RunDirectorDetection(...)` returns
- `src/field_nav_mapjump_diag.inl` -- header bumped to v0.17.7.5, two new helpers (`LogMapjumpVmStack`, `LogMapjumpResult`), all five Hooked* stubs updated to log around the original call
- `CHANGELOG.md` -- this entry
- `DEVNOTES.md` / `NEXT_SESSION_PROMPT.md` -- updated

### BAT recipe

1. Build and launch.
2. Walk through the same B-Garden hall transitions from v0.17.7.4 (bghall_5 wing -> bghall_3 -> bghall_1 / bghall_2, then continue exploring).
3. Send `Logs/ff8_field.log`.

### Expected log shape

**At field load** (one of these per resolved line, plus a summary):

```
[MAPJUMP-RES] bghall_5 ent3 'zell' m? ip=?: LITERAL destField=170
[MAPJUMP-RES] bghall_5 ent3 'zell': param 0x80000002 -> 0x000000AA
[MAPJUMP-RES] bghall_5 summary: N MAPJUMP instructions scanned, R entities resolved, U unresolved
```

(literal 170 = bghall_3's field ID; the example is illustrative -- actual values come from BAT)

**At MAPJUMP3 fire time** (every transition):

```
[MAPJUMP-HOOK] MAPJUMP3 fired on field=... inline_param=175
[MAPJUMP-HOOK]   player entity=I tri=T
[MAPJUMP-HOOK]   varblock [0x0000]=... [0x0002]=... ...
[MAPJUMP-HOOK]   MAPJUMP3 VM stack: sp=N [[N]=top=triId [N-1]=Z [N-2]=Y [N-3]=X [N-4]=destField]
[MAPJUMP-HOOK]   MAPJUMP3 engine RESULT: transition_type=1 destField=170 (bghall_3)
```

**Validation:** `[MAPJUMP-RES]` LITERAL destField (or VARBLOCK + addr resolved via varblock) should match `[MAPJUMP-HOOK] ... engine RESULT: destField=...` for the line Aaron crossed.

### Known not-fixed

- Catalog still shows bare `Exit` for SCREEN_BOUND lines. The resolver's output flows through `info.param` but until BAT confirms predicted == actual we don't trust it for labeling.
- All other deferred items from v0.17.7.4 remain deferred (`deploy.bat` cosmetic regression, walk-and-talk dialog gap, SeeD rank #27, Fire Cavern #28, planner-fallback #29).

## v0.17.7.4

Diagnostic-only build. Installs runtime hooks on the five MAPJUMP-family opcodes (MAPJUMP, MAPJUMP3, DISCJUMP, MAPJUMPO, WORLDMAPJUMP) to capture varblock state at the exact moment a field transition fires. Sidesteps the static-resolution dead end documented in v0.17.7.3 BAT: across ~220 captured POPM_W writes covering 5 fields, ZERO targeted the unresolved PSHM addresses (0x00AF, 0x01F6, 0x023A, 0x00E6, etc.) that SCREEN_BOUND lines read from. The destinations exist somewhere the JSM scanner can't see; this build asks the engine directly.

No catalog change in this build. The deliverable is the BAT log.

### Two corrections that shaped this build

Aaron flagged two interpretation errors carried over from v0.17.7.1.2 / v0.17.7.2:

1. **Dormant SCREEN_BOUND Lines are NOT meaningless.** They ARE valid exits with story-state-dependent destinations. Early-game routes through intermediate hallway fields; late-game routes direct. The v0.17.7.3 filter plan that would have stripped these from the catalog is REJECTED.

2. **The bghall_3 "Headmaster's Office 7" varblock-resolve in v0.17.7.1.2 was a misidentification.** The runtime read at addr=0x00E2 returned value 255 and the resolver labeled that as field 255 (Headmaster's Office 7), but Aaron never visited the Headmaster's Office during that BAT (it's on the 3rd floor; BAT was 1st-floor only). The value 255 was either a stale/uninitialized varblock slot or a story flag, NOT a destination field ID. Even the one apparent success of the runtime varblock approach was bogus, which means the timing AND the addressing in v0.17.7.1.2's resolver might both be wrong. This new diagnostic captures ground truth instead of guessing.

### How the hook works

`FF8Addresses::pExecuteOpcodeTable[N]` holds the dispatch function pointer for opcode N. The table lives in `.data` (writable). At `Initialize()` we save the original pointers for opcodes 0x29/0x2A/0x38/0x5C/0x10D and overwrite each entry with our own hook stub. The stub:

1. Reads `*FF8Addresses::pCurrentFieldId` and `pCurrentFieldName` for the source field.
2. Calls `GetPlayerEntityIndex()` + `GetEntityTriangleId()` for the player's walkmesh triangle, so we can correlate the firing with one specific SCREEN_BOUND line's SETLINE coordinates after the fact.
3. Reads `uint16_t` values from `0x01CFE9B8 + addr` for 10 known relevant PSHM addresses (0x0000, 0x0002, 0x00AA, 0x00AF, 0x00E2, 0x00E6, 0x01DF, 0x01F6, 0x023A, 0x0401) -- the union of every SCREEN_BOUND PSHM address seen in v0.17.7.x BAT logs.
4. Calls the saved original opcode function so the transition still proceeds.

Dispatch table addresses confirmed from FFNx source + disassembly cross-reference:
- `update_field_entities` = `0x00529FF0` (call site at `sub_4767B0 + 0x14E`)
- `execute_opcode_table` = `0x00B8DE94` (dispatch instruction at `update_field_entities + 0x657`)
- `field_vars_stack_1CFE9B8` = `0x01CFE9B8` (FFNx names the global after its address)
- Opcode handler signature = `int __cdecl (void* vmCtx, int param)` (from FFNx's `opcode_popm_w` typedef)

### Why not just hook the VM stack to read the destination directly

Would need to disassemble `opcode_pshm_w` / `opcode_mapjump` to learn the VM context struct layout, which adds risk for a build whose entire point is to get one round of ground-truth logging out. Reading the varblock at all 10 known PSHM addresses gives us the same information (the destination IS in one of those slots at MAPJUMP fire time) without touching VM internals.

### SET3 safety note

The one-line memory note about SET3 (opcode 0x1E) being permanently disabled because ANY interception hangs the infirmary cutscene applies SPECIFICALLY to SET3 -- not a general "don't patch the dispatch table" rule. SET3 is hot-path enough that even a pass-through wrapper triggers a script-interpreter race. MAPJUMP-family opcodes only fire on field transition (at most every few seconds), and the SET3 wrapper investigation in v0.09.32-40 never implicated 0x29 / 0x2A / 0x38 / 0x5C / 0x10D. The same dispatch-table-patch pattern (with `VirtualProtect` wrap, mirroring the disabled SET3 code path that's already in `Initialize()`) is safe for these.

### Files

- `src/ff8_accessibility.h` -- version `0.17.7.3` -> `0.17.7.4`
- `src/ff8_addresses.h` -- declarations for `opcode_mapjump` / `opcode_mapjump3` / `opcode_discjump` / `opcode_mapjumpo` / `opcode_worldmapjump`
- `src/ff8_addresses.cpp` -- storage and resolution from `pExecuteOpcodeTable[N]` at startup
- `src/field_nav_mapjump_diag.inl` -- NEW, dispatch-table patch + 5 hook stubs + `Install()` / `Restore()`
- `src/field_navigation.cpp` -- `#include` the new .inl, call `MapjumpDiag::Install()` from `Initialize()` and `MapjumpDiag::Restore()` from `Shutdown()`
- `CHANGELOG.md` -- this entry
- `DEVNOTES.md` / `NEXT_SESSION_PROMPT.md` -- updated

### BAT recipe

1. Build and launch.
2. Walk through any field transitions Aaron is investigating. B-Garden halls are the highest-value targets (bghall_1 -> wings, bghall_2/3/5 -> hub, ...). Any exit firing a MAPJUMP triggers a log entry.
3. Send `Logs/ff8_field.log`.

**Expected log shape per traversal:**

```
[MAPJUMP-HOOK] MAPJUMP fired on field=N 'fieldname' inline_param=P (0xPP)
[MAPJUMP-HOOK]   player entity=I tri=T
[MAPJUMP-HOOK]   varblock [0x0000]=V(0x...) [0x0002]=V(0x...) ...
[MAPJUMP-HOOK]   varblock [0x01F6]=V(0x...) [0x023A]=V(0x...) ...
```

For each traversal, the destination field ID is in ONE of those 10 varblock slots. Cross-reference the line crossing direction + player triangle against the `s_capturedLines[]` SETLINE coordinates (already logged at field load) to identify WHICH line fired, then read that line's preferred PSHM address from the existing scan output. The intersection gives us `(field, lineIdx) -> destination` in one BAT cycle.

**Sanity check:** at minimum, MAPJUMPs from bghall_1 INF-gateway exits (which v0.17.4.x already labels correctly) should show varblock destinations matching the known gateway destFieldIds. If they don't, the varblock base or read width is wrong and v0.17.7.5 fixes the addressing before resolving anything.

### Known not-fixed

- All wing-exit labels still show as bare `Exit` in this build. The catalog isn't touched; this is by design.
- Dorm bed Interaction, static-text leaks like `Kanban2 1 of 1`, and the `deploy.bat` "Version: SINGLE-PRONGED" cosmetic regression are all deferred to Track B v0.17.7.5+.

## v0.17.7.3

Diagnostic-only follow-up to v0.17.7.2. Two surgical changes: drop the `m == 0` gate on the JSM scanner's POPM_W capture so writes from all methods (not just init) get recorded, and gate the fourth `DumpEntityScript` call site that slipped through v0.17.7.2 (the promote-targets-without-position branch in `RunDirectorDetection`). Same `[MAPJUMP-RESOLVE]` and `[INITVARS-SUMMARY]` diagnostic blocks fire as in v0.17.7.2 -- this build's deliverable is the BAT log, NOT a catalog-visible change.

### Why widen the scanner

v0.17.7.2 BAT confirmed across all four B-Garden hall fields that the unresolved SCREEN_BOUND lines read from varblock addresses (0x00AF, 0x01F6, 0x023A, 0x00E6, 0x0002) which NO entity writes to in its init method. Field-wide init writes go to a completely different set of addresses (0x0000, 0x0405, 0x0410, 0x040F, 0x0406, 0x0407, 0x01DF, 0x0401). Zero overlap. The exception was bghall_3's `quistis` line at addr=0x00E2, which the runtime varblock read happened to resolve to field 255 (Headmaster's Office 7) -- proving destinations DO end up in the varblock, just not via init writes.

Most likely explanation: the Line entities write the destination to their own varblock address in their own walk-on / interaction method (not the m=0 init), then PSHM_W + MAPJUMP to use it. The current scanner doesn't see those writes because it only records POPM_W when `m == 0`.

The v0.17.7.3 widening removes that filter. The same `[INITVARS-SUMMARY]` block now logs every literal-PUSH + POPM_W pair across every entity's bytecode, not just method 0. If the unresolved addresses (0x01F6, 0x023A, etc.) now appear as writers with plausible field-ID values, v0.17.7.4 ships a five-line per-entity resolver. If they still don't appear, destinations live outside script bytecode (savemap, engine state) and we need a runtime opcode_mapjump hook instead.

### Why the fourth dump gate

v0.17.7.2 gated three `DumpEntityScript` call sites in `field_archive_jsm_director.inl` and `field_nav_fieldscripts.inl`, but missed the one in `RunDirectorDetection`'s promote-targets-without-position loop (`if (!outEntities[tc].hasPosition) DumpEntityScript(fieldName, tgt);`). bghall_1's BAT log showed `seito4` getting dumped from this site even with the other three gates active. Same `#ifdef FF8OPC_VERBOSE_JSM` wrap. The post-init block completed cleanly anyway in v0.17.7.2 BAT, but tightening this for cleaner logs.

### Per-entity buffer cap

`s_initVarMaps[entityIdx].count` is capped at 64 writes per entity. v0.17.7.2 BAT showed at most ~9 writes per entity from m=0 only; even with all-method widening, individual entities are unlikely to exceed 64. If a chatty entity hits the cap, the diagnostic will be incomplete but won't crash.

### Files

- `src/ff8_accessibility.h` -- version 0.17.7.2 -> 0.17.7.3
- `src/field_archive_jsm_scan.inl` -- drop `m == 0` from the POPM_W capture condition
- `src/field_archive_jsm_director.inl` -- gate the fourth `DumpEntityScript` site
- `CHANGELOG.md` -- this entry
- `DEVNOTES.md`, `NEXT_SESSION_PROMPT.md` -- updated

### BAT recipe

Same as v0.17.7.2 -- load any B-Garden hall (bghall_1 / bghall_2 / bghall_3 / bghall_5), wait at field load, send `Logs/ff8_field.log` for inspection.

**Expected outcomes:**

- **Path A (hypothesis confirmed):** `[INITVARS-SUMMARY]` for bghall_3 now shows an entity writing value 255 to addr 0x00E2 (matching the runtime varblock read that succeeded in v0.17.7.2). `[MAPJUMP-RESOLVE]` for bghall_2/3/5 SCREEN_BOUND lines now reports `K init writers` with plausible field-ID values instead of `NO init writers found`. v0.17.7.4 ships a 5-line per-entity resolver: for each unresolved line, look up `LookupInitVarWrites(addr)` filtered to the line's own jsmIndex, adopt the writer's value if it's in [1, 982].

- **Path B (hypothesis rejected):** Even with all-method widening, no entity writes to the unresolved addresses. Destinations live in savemap state populated by prior gameplay (parent field's init when player entered bghall_2 set the address; engine global state; new-game template). v0.17.7.4 ships a runtime opcode_mapjump hook + session cache: snapshot destinations on first traversal, label them on subsequent visits. Drawback: first visit shows bare `Exit`, second visit shows full destination.

- **Path C (partial):** Some lines resolve via the new writes, others don't. v0.17.7.4 ships Path A for the working cases plus an explicit `[PSHM-UNRESOLVED]` log marker for the rest, queueing them for the runtime hook in v0.17.7.5.

### Known not-fixed

- B-Garden hall WING exits (bghall_2/3/5) still show as bare `Exit`. Hub bghall_1 still labels its 4 INF gateway exits correctly.
- Dorm bed Interaction still missing (separate bug, deferred).
- Static text/signs like `Kanban2 1 of 1` still leak (Track B v0.17.7.5+).
- `Logs/build_latest.log` may still trigger the `deploy.bat` "Version: SINGLE-PRONGED" cosmetic regression (backlog).

## v0.17.7.2

Diagnostic-only build. v0.17.7.1.2 BAT confirmed two things at once: B-Garden hall exits still show as bare `Exit`, and `bghall_1`'s `field_scripts_init` post-init logging block never completed in the BAT log (Aaron transitioned fields ~37 seconds after entering, but `[PSHM-DEST]` lines for the hall exits, the `[fieldload] lineType assigned` summary, and everything after it were absent). Root cause for the log gap: `RunDirectorDetection` was unconditionally calling `DumpEntityScript` for every Background entity AND the Director itself, once per Director detected. `bghall_1` has three Directors (displight, cornerlight, sidelight) and six Backgrounds; one of those Backgrounds is `displight` with 34 methods covering ~3000 dwords. The combined dump volume blew up the field log past the point where init could finish before the player moved to another field, masking diagnostic output for everything that runs AFTER the JSM scan.

The broader v0.17.7.x problem -- the runtime PSHM varblock read at `0x1CFE9B8 + addr` returns zero/garbage at the lifecycle point where `field_scripts_init` runs the post-init resolution block, so the marker stays unresolved and the catalog falls back to bare `Exit` -- is unsolved. v0.17.7.2 explicitly does NOT attempt another runtime read. Instead it adds enough STATIC instrumentation to confirm or rule out a specific hypothesis before any resolver code is written: **field-exit destinations live in init-method literal-PUSH + POPM_W pairs that the JSM scanner already captures in `s_initVarMaps[]`**.

If that hypothesis holds, the v0.17.7.3 resolver is a five-line cross-reference: for each `JSM_ENT_LINE_SCREEN_BOUND` line whose `param` is a bit31 marker, ask `LookupInitVarWrites(marker & 0xFFFF)` for matching writers; if exactly one entity wrote a sensible field ID, that's the resolved destination. If the hypothesis fails (zero writers for the unresolved addresses), the destinations live in story-dispatch methods (m > 0) reachable from init via REQ chains, and the scanner has to be widened first. Either way, v0.17.7.2's BAT log tells us which path we're on -- and avoids shipping another build that *looks* right in code review but produces no labels at runtime.

### Fix 1 -- Gate the Director script-dumps behind `FF8OPC_VERBOSE_JSM`

Three call sites were unconditionally calling `DumpEntityScript`:

- `field_archive_jsm_director.inl` per-Background loop: dumps every BG entity once per Director detected. On `bghall_1` that's 6 backgrounds * 3 directors = 18 full dumps. `displight` (a Background, despite being a Director on the Others side) has 34 methods of ~88 dwords each.
- `field_archive_jsm_director.inl` per-Director self-dump: dumps the Director's own script after detection. Same `displight`-class entities, three times per field.
- `field_nav_fieldscripts.inl` Event-Trigger/Unknown Line loop: dumps every non-CAMERA_PAN, non-SCREEN_BOUND Line entity. On dormitory fields these reference long Background scripts.

All three now wrap their `DumpEntityScript` calls in `#ifdef FF8OPC_VERBOSE_JSM`. The build script doesn't define the symbol, so production runs skip the dumps entirely. The `[DIR-DIAG]` per-entity flag lines and the `[DIRECTOR] Detected` summary line stay in place -- those are tiny and informative.

### Fix 2 -- `LookupInitVarWrites()` + `EnumerateInitVars()` public API

Added to `field_archive.h` and implemented in a new `field_archive_jsm_initvars.inl` (textually included from `field_archive_jsm.inl` after `scan.inl`). Both functions are read-only walkers of `s_initVarMaps[128]` populated during `ScanJSMScripts`. The first answers "which entities write to this varblock address during init?"; the second enumerates all init writes across the field. New structs `InitVarWriter { entityIdx, value }` and `InitVarTuple { entityIdx, addr, value }` expose results to callers.

Neither function changes data. They exist only so the diagnostic block in `HookedFieldScriptsInit` can ask the question without poking at `FieldArchive`'s internal statics.

### Fix 3 -- `[MAPJUMP-RESOLVE]` + `[INITVARS-SUMMARY]` diagnostic blocks

Inserted in `field_nav_fieldscripts.inl` immediately after the `[fieldload] lineType assigned` summary log line. Two stages:

1. **Per-unresolved-line lookup**: for each captured line where `lineType == JSM_ENT_LINE_SCREEN_BOUND` AND `destFieldId & 0x80000000 != 0` (still a bit31 marker because v0.17.7.1.2's varblock read failed), extract `addr = destFieldId & 0xFFFF`, call `LookupInitVarWrites(addr)`, and log every writer found with `(entityIdx, symName, value)` and -- if `value` is a plausible field ID -- the resolved display name. If `totalWriters == 0`, log `NO init writers found` so we know to expand the scan.

2. **Field-wide summary**: call `EnumerateInitVars()` for the whole field, log up to 256 init writes annotated with `(entityIdx, symName, addr, value, optional displayName if value is a field ID)`. This shows the full landscape so we can spot patterns even when the per-line lookup misses (wrong addr convention, shifted/masked addresses, etc).

Neither block changes catalog behavior. Production runs will still see bare `Exit` labels until v0.17.7.3. The BAT log is the deliverable.

### Files

- `src/ff8_accessibility.h` -- version 0.17.7.1.2 -> 0.17.7.2
- `src/field_archive_jsm_director.inl` -- gate per-BG and per-Director self-dump behind `FF8OPC_VERBOSE_JSM`
- `src/field_nav_fieldscripts.inl` -- gate Event-Trigger/Unknown Line dump behind `FF8OPC_VERBOSE_JSM`; add `[MAPJUMP-RESOLVE]` + `[INITVARS-SUMMARY]` diagnostic blocks
- `src/field_archive.h` -- declare `InitVarWriter`, `InitVarTuple`, `LookupInitVarWrites`, `EnumerateInitVars`
- `src/field_archive_jsm_initvars.inl` -- NEW: implement the two lookup functions
- `src/field_archive_jsm.inl` -- `#include` the new .inl after `scan.inl`
- `CHANGELOG.md` -- this entry
- `DEVNOTES.md`, `NEXT_SESSION_PROMPT.md` -- updated

### BAT recipe

1. **bghall_1 (any B-Garden hall field)**. Load the field. Wait for ~5 seconds AT THE LOAD SCREEN (don't move) so the field log captures the init blocks. Press F10 to confirm field name.
2. **Quick exit verification**: press `-` / `=` to cycle to each exit. Aaron will hear the catalog labels (still bare `Exit` -- this build does not fix labels; resolver lives in v0.17.7.3).
3. **Log inspection (Claude)**: look for `[MAPJUMP-RESOLVE]` and `[INITVARS-SUMMARY]` lines for `bghall_1`. Expected outcomes:
   - **Hypothesis confirmed**: `[MAPJUMP-RESOLVE]` shows each unresolved line's `addr=0xXX` matched by exactly one writer with a plausible value (e.g. "ent7 'saveline_init' value=170 -> Cafeteria"). v0.17.7.3 ships the cross-reference resolver.
   - **Hypothesis rejected**: `[MAPJUMP-RESOLVE]` shows `NO init writers found` for the hall exit addresses. v0.17.7.3 widens the scanner to capture POPM_W writes from method 1+ reachable from init via REQ chain.
   - **Partial**: some lines resolve, some don't. v0.17.7.3 ships the resolver for the cases that work and a TODO for the others.
4. Confirm `[DIR-DIAG]` lines still appear for `bghall_1` (proves Director detection still runs) and that NO `[SCRIPT-DUMP]` log spam appears (proves the gate works). The `[fieldload] lineType assigned` summary and `[NAV-PROJ-INIT]` block MUST appear -- if they're missing, the build still has a log-explosion problem we haven't diagnosed.

### Known not-fixed

- B-Garden hall exits still show as bare `Exit` (resolver deferred to v0.17.7.3).
- Dorm bed Interaction still missing (deferred -- separate issue, addressed after the hall-exits resolver lands).
- `Logs/build_latest.log` may still trigger `deploy.bat` "Version: SINGLE-PRONGED" cosmetic regression from v0.15.3 (backlog).

## v0.17.7.1.2

Second hotfix on the v0.17.7.1 BAT findings. v0.17.7.1.1's two fixes both missed: the dorm bed `hasTalkSetup`-based JSM-scanner gating didn't classify the bed as INTERACTIVE (it uses REQ-to-Background for dialog, not its own MES/ASK), and the INF gateway proximity match couldn't recover B-Garden hall exit destinations because the INF binary data holds vestigial PSX placeholders (the v0.07.95 comment explicitly warned about this). v0.17.7.1.2 attacks both with the right signals.

### Fix 1 — Per-line `hasExtDispatch` discriminator (dorm bed)

The dorm bed Line's own script has only `SETLINE + REQ(bedBackground)`; the `MES`/`ASK` opcodes live in the bed Background entity. Static JSM scanning finds `foundDialogOp=false` for the Line, so dialog-wins-first reclassification (v0.17.7.1.1) sends the bed to `JSM_ENT_LINE_SCREEN_BOUND` via the MAPJUMP-to-next-day branch. In the v0.12.24 era this was masked by the field-wide `fieldHasInteractiveObjects` demote (which v0.17.7.1 correctly removed for fepic1).

The per-line equivalent of that demote was already present: the v0.12.24 REQ-following pass sets `info.hasExtDispatch=true` on Line entities that REQ entities containing dialog or 0x1C ext-dispatch. The flag propagates to `s_capturedLines[t].hasExtDispatch` in `field_nav_fieldscripts.inl`. It just wasn't consumed in `field_nav_catalog.inl`.

Now it is. Two surgical changes in `field_nav_catalog.inl`:

- **SETLINE-Exit injection**: skip SCREEN_BOUND lines with `hasExtDispatch=true`. These are dual-purpose (exit-via-interaction); showing them as Exits is misleading and the destination name (next-day field) is uninformative anyway.
- **SETLINE-Interaction injection**: accept SCREEN_BOUND lines with `hasExtDispatch=true` as Interactions, alongside the existing LINE_INTERACTIVE classification.

fepic1's three exit Lines have `hasExtDispatch=false` (they don't REQ interactive entities), so they pass through the Exit block as before. Per-line signal replaces field-wide signal with no false-positives.

### Fix 2 — PSHM varblock resolution (B-Garden hall exits)

B-Garden hall MAPJUMPs use runtime memory variables for the destination field ID. The script emits `MAPJUMP <PSHM_W varAddr>`, the engine pushes the value at `varblock[varAddr]` onto the VM stack, then MAPJUMP pops it as the destination. The destination table is populated during field init, but the static JSM scanner sees only the PSHM marker (`0x80000000 | varAddr`).

v0.17.7.1.1's INF-gateway-proximity fallback failed because B-Garden's INF gateways hold vestigial PSX placeholder `destFieldId` values — confirmed by the v0.07.95 comment that originally introduced INF-gateway support. The runtime varblock is the only authoritative source for these destinations.

Resolution in `field_nav_fieldscripts.inl` at the destFieldId copy block, which runs AFTER `s_originalFieldScriptsInit` has populated the varblock. For each Line classified as `JSM_ENT_LINE_SCREEN_BOUND`:

```c
if ((unsigned)rawParam & 0x80000000u) {
    uint16_t pshmAddr = (uint16_t)(rawParam & 0xFFFF);
    int16_t resolvedId = *(int16_t*)(0x1CFE9B8 + pshmAddr);  // varblock base hardcoded for Steam 2013 en-US
    if (resolvedId > 0 && resolvedId < FIELD_DISPLAY_NAMES_COUNT)
        rawParam = (int)resolvedId;
}
s_capturedLines[t].destFieldId = rawParam;
```

New log line `[PSHM-DEST] lineN (jsmM 'symName') marker=0xXXXXXXXX addr=0xXXXX -> field N (DisplayName)` confirms a successful resolution. If the varblock read produces an out-of-range value, the marker is kept and the catalog still falls back to bare `Exit` (no regression vs v0.17.7.1.1).

The v0.17.7.1.1 INF-gateway-proximity block in `field_nav_catalog.inl` stays in place as a secondary fallback for any field whose destinations aren't PSHM-resolvable but DO have meaningful INF data (e.g. older or simpler fields where INF was kept current).

PSHSM_W (opcode 0x0C, special memory) also produces the same marker via the scanner's shared branch. We read from the regular varblock base only; if a SCREEN_BOUND uses PSHSM_W and reads from special memory, the varblock fetch lands on the wrong region, produces out-of-range, and the catalog falls back. Acceptable for first BAT — likely rare on exit Lines.

### Files

- `src/ff8_accessibility.h` — version 0.17.7.1.1 → 0.17.7.1.2
- `src/field_nav_catalog.inl` — SETLINE-Exit skip on hasExtDispatch (already shipped in .1.1's partial implementation); SETLINE-Interaction accepts SCREEN_BOUND+hasExtDispatch
- `src/field_nav_fieldscripts.inl` — PSHM marker resolution for SCREEN_BOUND `destFieldId` reads from `0x1CFE9B8 + addr`
- `CHANGELOG.md` — this entry
- `DEVNOTES.md`, `NEXT_SESSION_PROMPT.md` — updated

### BAT recipe

1. **bghall_1**. Catalog now shows `Exit to Cafeteria` (or display-name equivalent), `Exit to Dormitories`, `Exit to Parking Lot` instead of bare `Exit`. `Light 1 of 1` still absent. `\` drive to each exit still works.
2. **Any dorm field** (bgryo1_1 / bgmaki1_1 etc.). Bed appears in catalog as `Interaction 1` (or higher N if multiple Lines). Drive to it with `\`; auto-drive lands on the SETLINE center. The bed's SCREEN_BOUND exit-to-next-day is NOT listed separately.
3. **fepic1**. Three `Exit to ...` entries still present. No regression — fepic1's exit Lines have `hasExtDispatch=false`.
4. **bggate_6**. Disconnected-island guard still in catalog.

Log lines to grep in `Logs/ff8_field.log`:

- `[PSHM-DEST] lineN ... -> field N (Name)` — successful PSHM resolution. Should fire 3+ times on bghall_1.
- `[PSHM-DEST] lineN ... -> varblock=N (out of range, keeping marker)` — PSHM read returned a nonsense value. Indicates the varblock at that address isn't a field ID, or the variable hasn't been initialised yet. Falls back to `Exit`.
- `[walkmesh-excl] JSM ent%d ...` from v0.17.7.1 should still fire for the bghall_1 light.

If bghall_1 exits still show as bare `Exit` after this fix, the next diagnostic is to confirm whether `[PSHM-DEST]` lines fire at all (varblock content empty/wrong at this lifecycle point), or whether they fire with out-of-range values (varblock address scheme different than assumed). v0.17.7.1.3 fallback would be a runtime opcode_mapjump hook that snapshots the resolved field ID on first traversal and caches it session-long, indexed by (sourceFieldId, lineIdx).

## v0.17.7.1.1

Hotfix on top of v0.17.7.1's BAT findings. Two fixes:

### Fix 1 — Dorm bed Interaction regression

v0.17.7.1 over-engineered the JSM scanner. The real fepic1 fix was the catalog's `fieldHasInteractiveObjects` field-wide demote removal; I *also* changed the JSM scanner to require TALKRADIUS/TALKON for `JSM_ENT_LINE_INTERACTIVE`. Dorm beds turn out not to use TALKRADIUS — they use SETLINE + dialog opcodes, and the engine fires the "Sleep?" prompt when the player crosses the line. With the new gating they classified as SCREEN_BOUND or EVENT and stopped appearing as `Interaction N` in the catalog.

Reverted the Line classification priority back to v0.12.24-era "dialog wins first":

1. dialog opcodes → `JSM_ENT_LINE_INTERACTIVE`  *(covers walk-across SETLINE+MES beds AND press-confirm TALKRADIUS signs)*
2. MAPJUMP without dialog → `JSM_ENT_LINE_SCREEN_BOUND`  *(pure screen exits like fepic1's three)*
3. battle/event without dialog → `JSM_ENT_LINE_EVENT`
4. BGDRAW/SCROLL only → `JSM_ENT_LINE_CAMERA_PAN`
5. nothing recognisable → `JSM_ENT_LINE_CAMERA_PAN`

fepic1's exit Lines have no dialog, so they still classify as SCREEN_BOUND → "Exit to ...". The catalog's field-wide demote removal (v0.17.7.1) stays in place; that's what actually fixed fepic1. The `hasTalkSetup` field on `JSMEntityInfo` is still populated and remains available for future fixes that need to distinguish confirm-press interactions from walk-across triggers, just no longer used to gate classification.

### Fix 2 — Robust exit destination recovery

Bghall_1 BAT exposed a pre-existing issue that v0.17.7.1 made visible: when a SETLINE SCREEN_BOUND Line's MAPJUMP destination is sourced from PSHM_W (runtime memory variable, common for B-Garden hall exits to Cafeteria / Dormitories / Parking Lot), the static JSM scan can't resolve the destination field ID. The captured trigger line's `destFieldId` ends up as a negative marker or out-of-range value, and the catalog label falls back to bare `"Exit"`.

Pre-v0.17.7.1, the v0.12.24 field-wide demote was masking this — those exits were mislabeled as `Interaction 1/2/3` so the missing destination didn't matter. Now they correctly show as exits but the destinations are missing.

Fix: when the captured SETLINE's `destFieldId` is unresolvable (`< 0` or `>= FIELD_DISPLAY_NAMES_COUNT`, not equal to the World-Map sentinel `-2`), match the SETLINE center to the nearest INF gateway by spatial proximity and inherit that gateway's `destFieldId`. INF gateway destFieldIds are static binary data in the `.inf` file at offset `+18` of each gateway record (loaded by `LoadINFGateways`) and reliable when present. Threshold: 1000 world units — SETLINE trigger lines and INF gateway lines for the same physical exit are typically co-located at the screen boundary (usually within ~200 units); 1000 gives generous margin without risking cross-matching.

The dedup-against-existing-exit check in the v0.07.94 INF-gateway-injection block runs after this and catches the duplicate via display-name `strcmp` (both paths now use the same `FIELD_DISPLAY_NAMES` table), so the INF gateway won't add a redundant entry once we've recovered the destId here. World-map destinations (`-2` sentinel) are preserved and resolve correctly through their existing branch.

New log line: `[refresh] SETLINE lineN center=... destId=N unresolvable -> matched INF gateway G destId=N (dist=N) -- recovering`. Useful for confirming the proximity match worked.

### Files

- `src/ff8_accessibility.h` — version 0.17.7.1 → 0.17.7.1.1
- `src/field_archive_jsm_scan.inl` — reverted Line classification refactor to dialog-wins-first; `foundTalkradius` tracker and `info.hasTalkSetup` writeback retained as harmless and future-useful
- `src/field_nav_catalog.inl` — INF gateway proximity recovery for unresolvable SETLINE destinations
- `CHANGELOG.md` — this entry
- `DEVNOTES.md`, `NEXT_SESSION_PROMPT.md` — updated

### BAT recipe

1. **bghall_1**. Catalog now shows `Exit to Cafeteria`, `Exit to Dormitories`, `Exit to Parking Lot` (or the equivalent display names from `FIELD_DISPLAY_NAMES`) instead of bare `Exit` entries. `\` drive to each exit still works.
2. **Any dorm field** (e.g. bgryo1_1). The bed appears in catalog as `Interaction 1` (or higher N if there are multiple Lines). Drive to it with `\`; auto-drive lands on the SETLINE center.
3. **fepic1**. Three `Exit to ...` entries still present; the v0.17.7.1 fix isn't regressed.
4. **bghall_1 (light regression)**. `Light 1 of 1` still absent from catalog (v0.17.7.1's walkmesh exclusion stays).

If an exit on bghall_1 still shows as bare `Exit`, grep `Logs/ff8_field.log` for `SETLINE lineN center=... destId=N unresolvable` to see whether the INF gateway proximity matched. "matched INF gateway" → destination recovered; "no INF gateway within 1000 units" → the bghall_1 INF doesn't have a static destFieldId for that exit and we need a different recovery mechanism (likely a runtime opcode_mapjump hook to snapshot the PSHM_W-resolved destination right before transition).

## v0.17.7.1

First substantive Track B fix: walkmesh exclusion rule plus per-line exit/interaction/event discriminator. Kills the `Light 1 of 1` regression on fepic1 and restores correct exit labels there. Two combined fixes, one BAT cycle. v0.17.7.0's file split (`field_nav_catalog.inl` 75.77 KB → 53.82 KB) was the prerequisite; this entry adds ~3 KB of code there (final size 56.77 KB, still comfortably under the 60 KB warn line).

### Fix 1 — Walkmesh exclusion rule

New `IsInsideWalkmesh(float x, float y)` helper in `field_nav_pathfinding.inl`. Standard sign-of-cross-product point-in-triangle across `s_walkmesh.tris` — cheap (the walkmesh is already loaded for A*) and conservative (returns false / keeps entity if the walkmesh isn't loaded).

Two call sites in `field_nav_catalog.inl`:

- **Runtime classification loop** (after party-filter). Drops entities with `talkonoff == 0 && pushonoff == 0` whose runtime position lies outside any walkmesh triangle. Either condition alone keeps the entity — a guard with `talkonoff > 0` who happens to stand on a disconnected island (bggate_6's tri=87) stays, an over-the-railing NPC stays, but the lights/particle emitters/decorative scenery that were leaking into the catalog via JSM Interactive Object promotion drop. The v0.12.08 reachability filter (removed in v0.12.09 because bggate_6's guard sat on tri=87 while the player stood on tri=22 — disconnected within one screen) does **not** recur here because the OR-with-talk preserves that guard.
- **JSM Interactive Object injection** (the path the lights take). Drops `JSM_ENT_INTERACTIVE_OBJECT` injections that have no `hasTalkSetup` AND sit off-mesh. Save Points, Draw Points, Shops, Card Games, and MAP_EXITs are explicitly excluded from this filter — those are always valuable navigation targets regardless of mesh position.

Entities at position `(0, 0)` skip the walkmesh test (placeholder for not-yet-placed) so the engine has time to place them before they get culled.

### Fix 2 — Per-line exit/interaction/event discriminator

Replaces the v0.12.24 `fieldHasInteractiveObjects` field-wide demote (which converted *every* SCREEN_BOUND Line into an Interaction on any field that contained at least one Interactive Object — wrong for fepic1 where the three exit Lines and the Interactive Objects are separate entities) with a per-line classification driven by the JSM scanner.

New opcode constant `JSM_OP_TALKRADIUS = 0x056`. New field `bool hasTalkSetup` on `JSMEntityInfo`, set to `foundTalkradius || foundTalkon` after the scan. JSM scanner Line classification now prioritises in this order (matches Aaron's 2026-05-18 taxonomy):

1. dialog + TALKRADIUS/TALKON → `JSM_ENT_LINE_INTERACTIVE`  *(player confirms — dormitory bed)*
2. MAPJUMP → `JSM_ENT_LINE_SCREEN_BOUND`  *(exit; dialog if any is cutscene)*
3. dialog/battle/event without talk → `JSM_ENT_LINE_EVENT`  *(walk-through fires it)*
4. BGDRAW/SCROLL only → `JSM_ENT_LINE_CAMERA_PAN`
5. nothing recognisable → `JSM_ENT_LINE_CAMERA_PAN`

The dual-purpose dormitory case (bgryo1_4 'squall' = MAPJUMP + dialog + TALK setup) still classifies as INTERACTIVE because rule 1 wins over rule 2. fepic1's three exit Lines (MAPJUMP only, no dialog, no talk setup) stay SCREEN_BOUND and reach the catalog as Exits.

The SETLINE-injection layer in `RefreshCatalog` now trusts `lineType` directly:

- LINE_SCREEN_BOUND → "Exit to <field>" (the v0.12.24 demote and its `fieldHasInteractiveObjects` lookup are deleted).
- LINE_INTERACTIVE → "Interaction N" (the v0.12.24 dual-purpose promote-SCREEN_BOUND path is deleted, because dual-purpose Lines now classify as LINE_INTERACTIVE directly in the JSM scanner).

### Files

- `src/ff8_accessibility.h` — version 0.17.7.0 → 0.17.7.1
- `src/field_archive_jsm_constants.inl` — new `JSM_OP_TALKRADIUS = 0x056`
- `src/field_archive.h` — new `JSMEntityInfo::hasTalkSetup`
- `src/field_archive_jsm_scan.inl` — `foundTalkradius` tracker, Line classification refactor, `info.hasTalkSetup` writeback
- `src/field_nav_pathfinding.inl` — `IsInsideWalkmesh` helper (~30 lines, +2.48 KB)
- `src/field_nav_catalog.inl` — runtime walkmesh exclusion, JSM-injection walkmesh exclusion (Interactive Object only), removed v0.12.24 field-wide demote in both SETLINE-exit and LINE_INTERACTIVE blocks
- `CHANGELOG.md` — this entry
- `DEVNOTES.md`, `NEXT_SESSION_PROMPT.md` — updated

### File sizes after edits

- `field_nav_catalog.inl`: 53.82 → **56.77 KB** (+2.95 KB; 23 KB headroom under the 80 KB hard fail)
- `field_archive_jsm_scan.inl`: 63.32 → **64.62 KB** (+1.30 KB; warn-zone but 15 KB headroom)
- `field_nav_pathfinding.inl`: 39.70 → **42.18 KB** (+2.48 KB)
- `field_archive.h`: 12.62 → **13.09 KB** (+0.47 KB)
- `field_archive_jsm_constants.inl`: 6.52 → **6.61 KB** (+0.09 KB)

### BAT recipe

1. **bghall_1**. Confirm `Light 1 of 1` is **NOT** in the catalog (the JSM-injection walkmesh exclusion killed it). Confirm Save Point, Directory, NPCs, exits all still present and selectable. Drive to Save Point with `\` and confirm the auto-drive still completes successfully.
2. **fepic1** (Front Gate 5). Confirm the catalog shows `Exit to ...` for the three exits instead of `Interaction 1/2/3`. The push-through gate (Track A) is still broken, but the labels are now correct — that's the win for this BAT.
3. **bggate_6** (disconnected-walkmesh island regression check). Confirm the guard still appears in the catalog (because guard has `talkonoff > 0`, so the runtime walkmesh-exclusion's OR-with-talk preserves him). This is the v0.12.08 regression test.
4. **bgroom_1** or **Cafeteria 1** (no-regression check). Walk to a sign or interactive object on a dormitory-style field. The sign won't be in the catalog yet — that's v0.17.7.2's SETLINE-position promotion. Confirm the v0.17.7.1 changes haven't broken any existing interactive surface that *was* in the catalog at v0.17.6.2.

`Logs/ff8_field.log` will show `[walkmesh-excl] ent... -- excluded` lines for filtered entities and `[refresh]` catalog dumps reflecting the cleaned-up labels.

## v0.17.7.0

Prerequisite file split for the upcoming Track B (entity-catalog overhaul) chapter. No functional change. `field_nav_catalog.inl` was 75.77 KB at v0.17.6.2 — only 4 KB under the 80 KB CI hard fail. The substantive Track B fixes (walkmesh exclusion, per-line exit discriminator, SETLINE-position promotion, NPC `ResolveFriendlyName` routing) will add ~5 KB to this file, which would trip CI. This release moves two large blocks out into dedicated helper files so the next four point releases have room to land.

### What moved, where, why

Two new sub-`.inl` files included from `field_navigation.cpp` *before* `field_nav_catalog.inl` so their static helpers are visible to `RefreshCatalog`:

- **`field_nav_catalog_diag.inl`** (9.29 KB) — four one-shot diagnostic dumps. `DumpEntityDiagOnce(base, lim)` (ENTDIAG, dormant since v05.58), `DumpBgDiagOnce(lim)` (BGDIAG, dormant since v05.58), `DumpPartyStateOnce()` (party-state, fires once per field), `DumpCoordDiagOnce(base, lim)` (COORDDIAG, fires once per field). Each helper no-ops on subsequent calls via the existing `s_*Dumped` flags.

- **`field_nav_catalog_lateres.inl`** (11.19 KB) — three late-position-resolution passes: `ResolveLatePositions()` (LATE-RESOLVE: read runtime entity struct for entities with `hasPshmCoords` but no `hasPosition`), `MatchSet3LateCaptures()` (SET3-LATE-MATCH: overlay accumulated SET3 captures), `ResolveStructPositions()` (STRUCT-POS: direct entity-struct read for PSHM entities, catches entities beyond the active window). Called in the same order they ran inline pre-split.

The v0.12.17 VARBLOCK-POS block (~60 lines of unreachable code gated `if (false)`) is dropped during extraction. It corrupted positions when last enabled and was permanently disabled. Git history at v0.17.6.2 preserves the source if anyone needs to revisit varblock as a future resolution path.

### Sizes

- `field_nav_catalog.inl`: 75.77 KB → **53.82 KB** (-21.95 KB). Now comfortably under the 60 KB warn line with ~26 KB headroom under the 80 KB hard fail.
- Combined catalog footprint (slim + diag + lateres): 74.30 KB. ~1.5 KB net reduction from dropping VARBLOCK.

### Files

- `src/ff8_accessibility.h` — version 0.17.6.2 → 0.17.7.0
- `src/field_nav_catalog_diag.inl` — NEW (extracted)
- `src/field_nav_catalog_lateres.inl` — NEW (extracted)
- `src/field_nav_catalog.inl` — slim, calls helpers (75.77 KB → 53.82 KB)
- `src/field_navigation.cpp` — `#include` chain extended with the two new files
- `CHANGELOG.md` — this entry
- `DEVNOTES.md`, `NEXT_SESSION_PROMPT.md` — updated

### What's NOT touched

Every classification, screen-filter, trigger-line, JSM-injection, INF-gateway-dedup, commit, and selection-restore block stays inline in `RefreshCatalog` byte-for-byte. The same applies to the party-filter, model-24 save-point detection, draw-point consolidation, and entity-type-table classification. `RefreshCatalog`'s outer `__try/__except` wrapper is unchanged. `deploy.bat` unchanged (only the parent `.cpp` compiles; new `.inl`s are textual includes).

The substantive Track B fixes ship in v0.17.7.1 and later.

### BAT recipe

Load bghall_1. Press F9 several times to cycle the catalog. Confirm:
- Catalog populates with the same entries that v0.17.6.2 produced (Directory, Hall exits, Save Point, NPC, etc. — whatever was there pre-split).
- F9/F10 cycling, Backspace direction announce, `\` auto-drive all behave the same as v0.17.6.2.
- `Logs/ff8_field.log` shows `[party-state] formation = ...` and `[COORDDIAG]` lines firing once per field (proving the diag helpers still run).
- `Logs/build_latest.log` clean — no compile errors from the new `#include` chain or the helper function visibility.

If the catalog populates differently from v0.17.6.2, the split has changed behavior somewhere and needs triage before any substantive fixes land.

## v0.17.6.2

Disables F9 path-finding auto-drive's corridor-level steering. The v0.17.6.1 BAT [drive-vec] log on bghall_1 Save Point exposed corridor steering directly fighting the drive-start pre-skip block, wedging the player against geometry for hundreds of ticks. Funnel waypoints alone (manual nav's BAT-proven primitive) are now F9 auto-drive's only steering source.

### What [drive-vec] showed

The Save Point drive started cleanly. Pre-skip correctly bumped past wp 0 (only 58 units from player; both wp 0 and the corridor edge midpoint between tri 358 and tri 71 are the funnel-collapsed point `(-626,-8215)`, which is essentially the player's current location). For one tick at t=30, the analog reflected the real target:

```
t=30  corOverride=0  corSteer=(-700,-8593)  finalDelta=(-132,-375)  lX=-332 lY=943  kb=DL
```

That's south-west toward the save point at `(-700,-8593)` -- the same direction manual nav uses for its "south, 2 steps" announcement at this position. But starting at t=60, the corridor-level steering block kicked in (its `s_driveTotalTicks >= 30` gate had just opened) and overrode `steerX/Y` to the shared-edge midpoint `(-626,-8215)`:

```
t=60   corOverride=1  corSteer=(-626,-8215)  finalDelta=(-57.8, 2.6)  lX=-999 lY=-44  kb=L
t=90,120,150,180,210,240...  pp=(-568,-8218)  lX=-999 lY=-44  kb=L  moveDist=0
```

The corridor steering re-introduced the exact point that pre-skip had discarded. The analog flipped from `lX=-332 lY=943` (south-west diagonal) to `lX=-999 lY=-44` (pure west). The keyboard collapsed from `kb=DL` (diagonal) to `kb=L` (single direction). The player pressed pure LEFT into a wall and didn't move for hundreds of ticks. Recovery fired, re-pathed, corridor steering picked the same point again, player wedged again. Repeat until `Gave up. Distance remaining: 555.`

### Why manual nav doesn't have this problem

Manual nav at the same position computes the analog directly from `(target - player) * camAxes` and presses arrow keys for the dominant axes. From `(-568,-8218)` toward save point `(-700,-8593)` the dominant axes are both LEFT and DOWN (the delta is `(-132,-375)`), so the keyboard fires `DL` diagonal. FF8's wall-sliding then handles the corridor turn -- the player walks south-west, slides along the west wall, and naturally tracks the corridor through tri 358 -> 71 -> 70 -> 8 to the save point.

Manual nav has been correct on the first announcement across bghall_1, bghall_4, bg2f_1, bg2f_2, bgroom_1 since v0.17.5 with no corridor steering. F9 auto-drive's separate corridor steering pipeline was the source of the failure, not the funnel or the analog projection (v0.17.6.0 confirmed those are correct).

### The fix

`field_nav_autodrive.inl` line ~635: the corridor-level steering condition is wrapped with `false &&`, matching the pattern v06.20 used to disable wall-avoidance. The entire block stays in place with the original v06.17/v0.15.9.2.3 rationale plus a new v0.17.6.2 block explaining why it's off and what to flip if a future field regresses without it.

Chase-drive is unaffected; it has skipped this block since v0.15.9.2.3.

Other things v0.17.6.1 added that stay in place because they're correct:
- Recovery counter reset on tri advance (worked exactly as designed -- the v0.17.6.1 BAT log shows `[drive] recovery counter reset: tri 358 -> 359 (player advanced along corridor; phase was 6)` firing at the right moment).
- `MAX_RECOVERY_PHASES` 12 -> 30 (safety net, didn't fire in v0.17.6.1 BAT; drive ended via DRIVE_MAX_TICKS instead).
- `[drive-vec]` per-tick diagnostic log (this is how we found the bug; staying on for v0.17.6.2 BAT in case a different failure pattern emerges).

### Files

- `src/ff8_accessibility.h` -- version 0.17.6.1 -> 0.17.6.2
- `src/field_nav_autodrive.inl` -- corridor-level steering block gated with `false &&` and new v0.17.6.2 rationale comment
- `CHANGELOG.md` -- this entry

## v0.17.6.1

Follow-up triage of the v0.17.6.0 BAT. The re-engineered F9 auto-drive proved mechanically correct on bghall_1 (no CALIB, .ca-quantized axes, mathematically correct analog projection, kb/analog agreement), but Aaron's BAT reported "failed on most entities I tried" -- three of four drive attempts ended in `Stuck. Distance remaining: <N>.` Only the JSM-injected Directory (closest target, in the main hallway) reached Arrived. Root-cause analysis traced the failures to the recovery counter, not the axis pipeline.

### Recovery counter inflates across triangle boundaries

The Save Point drive made genuine progress through five corridor triangles (367 -> 366 -> 363 -> 362 -> 359), but each triangle escape needed 2-3 recovery cycles (re-path + perpendicular nudge), and `s_driveWigglePhase` only resets when the player advances past funnel waypoint index 3 -- which never happened because the path kept re-pathing back to waypoint 0 after each recovery re-path. The global counter inflated to 12 and `MAX_RECOVERY_PHASES` killed the drive while the player was still making real progress toward the save point. The two long-range exit drives (Hall 8 at dist 4753 remaining, Front Gate 5 at dist 3291 remaining) hit the same wall earlier in their corridors.

v0.17.6.1 adds a new reset signal: when the recovery block fires and the player's walkmesh triangle has changed since the previous recovery cycle, that's genuine corridor progress and `s_driveWigglePhase` resets to 0. Each new triangle along the corridor earns a fresh recovery budget. The new `s_lastRecoveryTri` state variable is initialized to `0xFFFF` at drive start in `field_nav_handlekeys.inl` so the first recovery on a fresh drive doesn't see a stale tri from a prior drive on the same field.

`MAX_RECOVERY_PHASES` is also bumped from 12 to 30 as a safety net. With the tri-advance reset working, 30 is only reached when the player genuinely cannot escape a single triangle -- the v0.17.6.0 Save Point case would have run with phase max ~3 per triangle (the highest seen between resets on bghall_1) and never gotten anywhere near 30. The new ceiling is designed to fire only on "this triangle is permanently unreachable" cases, not on slow corridor traversals.

### Per-tick steering pipeline diagnostic ([drive-vec])

The v0.17.6.0 BAT log showed `lX=-840 lY=-542` for multiple consecutive 120-tick log windows even as the player oscillated between two positions, which made it hard to tell whether the analog projection itself was wrong or just stuck on a stale waypoint. The existing `[drive] tick=` line fires every 2 seconds and only shows post-projection state.

v0.17.6.1 adds a `[drive-vec]` log that fires every 30 ticks (~0.5 s) and shows the intermediate values at each stage of the steering pipeline:

```
[drive-vec] t=N tri=T pp=(px,pz) wpRaw=(wx,wy) corOverride=0|1 corSteer=(sx,sy) trigRedir=0|1 finalDelta=(dx,dz) lX=lx lY=ly kb=mask wig=W phase=P
```

- `wpRaw` is the chosen funnel waypoint (or final target) before corridor steering runs.
- `corOverride/corSteer` says whether corridor steering replaced the waypoint with a shared-edge midpoint, and what midpoint it picked.
- `trigRedir/finalDelta` says whether the trigger-line proximity check rewrote `dx/dz` parallel to a nearby line, and the final `dx/dz` going into `SetAnalogFromVector`.
- `lX/lY` are the analog values after camera projection.
- `kb` is the heading bitmask derived from `lX/lY` (post v0.17.6.0 unified logic).
- `wig/phase` are the recovery counters.

When v0.17.6.1 BAT data comes back and a drive still gets stuck, the per-tick log shows exactly which stage broke. Three new tracking variables (`vecWpRawX/Y`, `vecCorridorOverrode`, `vecTrigRedirected`) record stage outputs as the existing pipeline runs; they cost essentially nothing per tick and the log itself is gated by `s_driveTotalTicks % 30 == 0`. To turn the log off after triage is complete, raise `DRIVE_VEC_LOG_INTERVAL` to a large number.

### Files

- `src/ff8_accessibility.h` -- version 0.17.6.0 -> 0.17.6.1
- `src/field_navigation.cpp` -- `MAX_RECOVERY_PHASES` 12 -> 30, new `s_lastRecoveryTri` state
- `src/field_nav_handlekeys.inl` -- reset `s_lastRecoveryTri` at drive start
- `src/field_nav_autodrive.inl` -- recovery block tri-advance reset, three pipeline tracking flags, [drive-vec] log emit
- `CHANGELOG.md` -- this entry

## v0.17.6.0

Re-engineers F9 path-finding auto-drive to share manual nav's load-time-quantized camera axes, splits draw-point arrival from save-point arrival, and adds INF gateway crossing detection. First of a staged v0.17.6.x series that re-bases auto-drive on manual nav's BAT-proven primitives.

### Three changes, one BAT

**1. F9 auto-drive uses the .ca-quantized axes manual nav uses.**

Manual nav has been correct on the first announcement across bghall_1, bghall_4, bg2f_1, bg2f_2, and bgroom_1 since v0.17.5 thanks to load-time 90-degree quantization of the .ca-file axes. F9 auto-drive was still running the v06.14 empirical CALIB pipeline that injects `lX=+1000` for 24 ticks, then `lY=+1000` for 24 ticks, measures the resulting walkmesh delta, and writes `s_driveCamRight/Down`. That loop predates the quantization work and has a known failure mode (the bghall_1 BAT bug from NEXT_SESSION_PROMPT): when phase 1 fails because the player is wedged against geometry, the default `(1,0)` axes are kept and steering uses wrong axes on rotated cameras.

v0.17.6.0 wires `SetAnalogFromVector` to read `s_camRight/Down` (manual nav's quantized pair) when F9 owns the drive, and `s_driveCamRight/Down` (the empirical pair) when chase-drive owns it. F9's handlekeys block no longer initiates CALIB -- it sets `s_calibPhase = 3` (skip-state) unconditionally. The auto-drive starts moving the moment the player presses backslash, with no warmup phase and no CALIB-can-fail edge case.

Chase-drive is deliberately untouched: per its design doc Finding #10, empirical calibration is the verified-working axis source on rotated-camera chase fields (e.g., domt5_1 where `camRight ~= (0,1)`). Future unification can swap chase to the quantized axes once F9 with quantization proves stable in production (chase doc Finding #28: parallel implementations have already cost five wasted BAT cycles, so they shouldn't stay parallel forever -- but we don't risk regressing chase auto-pilot while validating F9).

**2. Draw points arrive within talk radius. Save points stay walk-onto.**

The handlekeys arrival-distance block previously conflated save points and draw points under a single 30-unit walk-onto rule. Per Aaron's spec, draw points should behave like NPCs and interactive objects -- arrive when the player is within interaction distance, not when they're standing on top of the marker.

The new split:
- `ENT_SAVE_POINT` -> arriveDist = 30.0f (walk-onto, unchanged). The save crystal only activates when the player's model overlaps it.
- Runtime-entity targets (`entityIdx >= 0`) including NPCs, Objects, and reclassified-NPC Draw Points -> read engine-set talkRadius, clamp to 60-unit floor. Logged target type for diagnostic clarity.
- `ENT_DRAW_POINT` with no runtime entity slot (JSM-injected, `entityIdx <= -300`, e.g., Fire Cavern 'drpoint') -> arriveDist = 120.0f. Matches GPS_ARRIVE_DIST's default for non-entity targets and gives the player room to press X without inching onto the exact marker.

**3. INF exit gateways auto-cross like trigger lines.**

Trigger-line targets (`entityIdx <= -200`) already had cross-product sign-flip arrival detection plus a 300-unit overshoot offset on the steer target -- when the player crosses the line, the drive announces Arrived and the engine fires the screen transition naturally. INF gateway targets (`entityIdx <= -400`) used plain `dist < arriveDist` arrival, which stopped the drive 300 units short of the gateway and left the player to walk through manually.

v0.17.6.0 wires gateway targets through the same crossing-detection state chase-drive uses (`s_driveCrossLine*`, `s_driveCrossLineActive`):

- At drive start, handlekeys finds the raw INF gateway in `s_gateways[]` whose `destFieldId` matches the dedup-catalog entry AND is nearest to the player, and seeds its line endpoints into `s_driveCrossLine*`.
- `UpdateAutoDrive`'s crossing block, which previously gated on `s_chaseDriveActive && s_driveCrossLineActive`, now gates on just `s_driveCrossLineActive`. Chase-drive and F9 gateway both flow through the same code path.
- F9 trigger-line targets keep using the existing `s_capturedLines[]` lookup branch; handlekeys doesn't seed `s_driveCrossLine*` for them.

The dedup catalog can cover 1..N raw gateways with the same destination field; we pick the nearest as the crossing line. If the player crosses a different raw gateway in the same group, the engine still fires the transition and `"Player position lost."` ends the drive when the field reloads -- functionally equivalent for the user.

### Files changed

- `src/ff8_accessibility.h` -- version bump (0.17.6.0)
- `src/field_nav_autodrive.inl`:
  - `SetAnalogFromVector` -- branch axis source on `s_chaseDriveActive`. Updated documentation block.
  - `UpdateAutoDrive` crossing block -- condition widened to `s_driveCrossLineActive` for both chase and F9 gateway.
- `src/field_nav_handlekeys.inl`:
  - F9 drive-start CALIB block -- replaced with unconditional `s_calibPhase = 3`.
  - Arrival-distance block -- split save points from draw points; runtime-entity draw points fall through to talkRadius path; JSM-injected draw points get a 120-unit default.
  - Trigger crossing block -- added gateway-crossing setup (find nearest raw gateway with matching destFieldId, seed `s_driveCrossLine*`, compute `s_driveTrigCrossStart`).
- `CHANGELOG.md`, `DEVNOTES.md`, `NEXT_SESSION_PROMPT.md`

### What's NOT touched

- Chase-drive (`StartChaseDrive`, `IsChaseDriveActive`, the v0.15.9.2.x logic) keeps its empirical CALIB pipeline. `s_driveCamRight/Down` still exist and are still written by CALIB phase 1/2 when chase-drive starts and `s_calibPending` is true.
- The `s_camCalibrated` flag and `s_calibPending` reset in `HookedFieldScriptsInit` -- chase-drive depends on them.
- Recovery / wiggle phase machine (deferred to v0.17.6.2).
- Engine triangle-ID corridor steering (deferred to v0.17.6.1; the stale-triId class of failures is the next big-ticket item).
- New per-tick `[drive-vec]` diagnostic (deferred to v0.17.6.3).
- Manual nav, GPS, funnel pruning, hysteresis -- all v0.17.5.x work stays as shipped.

### BAT verification

Load save in bghall_1. Cycle F9 to an exit (e.g., Hall 6). Press `\`.

Expect:
1. NO `[CALIB] phase 1` or `phase 2` log lines for F9. The first `[drive] tick=` line should show the correct screen-relative direction immediately.
2. `[drive] gateway target -> crossing line (...)->(...) crossStart=... rawIdx=N destFieldId=M` log line at drive start.
3. As the player approaches the exit, `[drive] stopped: Arrived.` fires when they physically cross the gateway line (cross-product sign-flip), not 300 units short.
4. Field reloads naturally to the destination.

Also BAT: cycle F9 to a Draw Point and `\`. The drive should stop within ~120 units (or talkRadius if a runtime entity), not walk on top of the marker. Save Points still walk on top (unchanged).

If cardinals or steering are still wrong on any rotated-camera field, the `[NAV-PROJ-INIT] quantization` line at field load tells us the camera axes the drive is using, and the per-tick `[drive] tick=` line shows the resulting lX/lY values. Both should match what manual nav uses for direction announcements on the same field.

## v0.17.5.4

Fixes the World Map polling stuck-at-startup bug exposed by v0.17.5.3's TTS audit trail.

### What v0.17.5.3 revealed

Aaron's BAT log showed two TTS messages firing in the same second when pressing `\` on bghall_1:

```
[15:59:37] [TTS] "Driving."
[15:59:37] [TTS] "No locations available." (interrupt)
```

The FieldNavigation autodrive started successfully ("Driving"). Immediately afterward, the World Map module's key handler also responded to `\`, found its catalog empty (correctly, since there's no world map data when on a field), and announced "No locations available" with interrupt=true -- clobbering the "Driving" announcement.

### Root cause

World Map's `IsOnWorldMap()` in `world_map_segments.inl` only checked `WM_SCENE_FLAG`. At application boot before any scene has loaded, that memory location reads 0 (zero-initialized memory), and `(scene == 0)` returned true. The `Poll()` function then declared "Entered world map" at boot, set `s_onWorldMap=true`, and never reset because the exit detector only fires on a true->false transition of `IsOnWorldMap()`. `PollKeys()` then ran every tick on every screen (including fields), responding to `\` with the "No locations available" announcement.

The world log carried a long-standing diagnostic warning that surfaced exactly this:

```
[WORLD] WorldMap: Entered world map
[WORLD] WorldMap: Warning - On world map but game mode is 0 (expected 2)
```

The warning was observing the disagreement between scene flag and game mode but only logging it. v0.17.5.4 uses game mode as part of the decision.

### The fix

`IsOnWorldMap()` now requires BOTH signals to agree:

1. `FF8Addresses::pGameMode` must be resolved AND its value must equal `MODE_WORLDMAP` (= 2).
2. THEN the scene flag at `WM_SCENE_FLAG` must read 0.

If either check fails, the function returns false. Both signals are wrapped in SEH (`__try`/`__except`) since they read raw process memory.

### Files changed

- `src/ff8_accessibility.h` -- version bump (0.17.5.4)
- `src/world_map_segments.inl` -- `IsOnWorldMap()` rewritten to require both scene flag AND game mode

### What's NOT touched

FieldNavigation autodrive's underlying steering issue on bghall_1 (separate bug, see below). Funnel pruning (v0.17.5.2). Quantization (v0.17.5). Hysteresis (v0.17.5.1). TTS audit logging (v0.17.5.3, retained and proving its worth).

### Separate bug exposed (deferred)

The BAT log also shows that even after the spurious "No locations available" TTS, FieldNavigation autodrive RAN but FAILED to reach its target. At drive start (15:59:37) dist=3899 from target. After 21 seconds of running (15:59:58) dist=3726 -- only 173 units of forward progress. Player position went from (137,-7634) to (134,-7461). Multiple recovery cycles and re-paths in the log. Drive stopped with `[drive] stopped: Stuck. Distance remaining: 3726`.

Analysis of the final tick shows the steering math is producing contradictory inputs: at player=(134,-7461), steer target=(452,-7722), delta is (+318 east, -261 south). With autodrive's calibrated axes `driveCamRight=(1,0) driveCamDown=(0,-1)`, the correct keyboard direction is RIGHT+DOWN. The log instead shows `kb=U lX=1000 lY=0` -- pressing UP on the keyboard and pushing analog right only. The vertical axis is inverted for autodrive on this field.

This is a separate, longstanding autodrive issue independent of v0.17.5.4's world-map fix. Manual GPS works fine on bghall_1 because manual nav and autodrive use SEPARATE axis pairs (`s_camRight/Down` vs `s_driveCamRight/Down`, v0.17.2 split). Tackling this needs a dedicated investigation of autodrive's steering pipeline and is queued for a future version.

### BAT recipe

Launch the game and reach a field (any field). Press `\` to start autodrive on a catalog entity.

Expect:
- `ff8_world.log` should NOT show "Entered world map" at boot. The world map module should only declare entry when the player actually reaches the world map.
- `ff8_mod.log` should show `[TTS] "Driving."` (or "Target not yet located.") in isolation -- no follow-up `[TTS] "No locations available."` interrupt.
- `\` on the world map (when the player is actually on it) should still work normally.

## v0.17.5.3

Diagnostic logging for autodrive validation failures and an audit trail of every TTS utterance. No behavior changes; this build is a step toward fixing the "Target not yet located" autodrive refusal Aaron hit on bghall_1.

### Motivation

After the v0.17.5.2 push, Aaron tried autodrive on bghall_1 by selecting an entity from the catalog and pressing `\`. The mod spoke something like "location not available" (Aaron's paraphrase) and refused to start driving. There was no record in any log of:

1. What was actually spoken (the message Aaron heard).
2. Which catalog target was selected at the moment of the refusal.
3. Why the validation failed (was the player position unknown? the target position? was the catalog index out of bounds?).

v0.17.5.3 adds both logs so the next BAT will expose the cause without needing further investigation.

### What ships

**1. `ScreenReader::Speak` -- TTS audit trail.** Every call to `Speak(text, interrupt)` now writes a `[TTS] "<text>"` line to `ff8_mod.log` before the text reaches SAPI/NVDA. Empty-string "silence" purge calls are skipped (they produce no audio). The narrow-ASCII transliteration is one WideCharToMultiByte hop and bounded at 511 chars; overhead is negligible at the speech cadence the player actually experiences. The same logging applies whether the call site uses `Speak`, `Output`, `RepeatLast`, or any other variant -- all funnel into this one wide-char path.

This is a permanent diagnostic. It pays for itself every time a player reports "the mod said something weird" -- we get the literal string without needing to reproduce the scenario.

**2. Autodrive validation-fail logging.** In `field_nav_handlekeys.inl`, the `else` branch that fires "Target not yet located" now logs a full context line before speaking. The line reports:

- Current field name
- Selected catalog index (and total catalog size)
- Target's `entityIdx` and `gatewayIdx`
- Target type and name
- Whether `GetEntityPos` succeeded for the player
- Whether `GetEntityPos` succeeded for the target (only meaningful when entityIdx >= 0)
- Player's own entity index

This tells us in one log line which of the validation gates failed.

### Files changed

- `src/ff8_accessibility.h` -- version bump (0.17.5.3)
- `src/screen_reader.cpp` -- `[TTS]` audit logging in `Speak(const wchar_t*, bool)`
- `src/field_nav_handlekeys.inl` -- `[drive] REFUSED` diagnostic in the validation-fail branch

### What's NOT touched

Funnel pruning (v0.17.5.2). Quantization (v0.17.5). Hysteresis (v0.17.5.1). The validation logic itself -- this is observation only, no fix. The actual fix for whatever the next BAT exposes will ship as v0.17.5.4 or v0.17.6.

### BAT recipe

Repeat the autodrive attempt that failed on bghall_1:
1. Load to bghall_1 (or any field).
2. Cycle to an entity in the F9/F10 catalog (especially an NPC, which is the suspected failure case).
3. Press `\` to start autodrive.
4. If you hear "Target not yet located" or anything else other than "Driving", note which entity was selected.

Check `ff8_mod.log` for `[TTS] "..."` -- that's the exact utterance you heard.
Check `ff8_field.log` for `[drive] REFUSED -- target validation failed: ...` -- that's the why.

### Likely cause (hypothesis, BAT will confirm)

GetEntityPos for NPCs returns false until the engine's `set_current_triangle` callback fires for them. That callback runs the first time the entity's init script puts it on a triangle. For NPCs whose init scripts haven't executed yet at autodrive-press time, `target_pos_known=0` is the expected log signal. Fix (deferred to v0.17.5.4): fall back to the JSM-extracted SETPOS coordinates already captured for those NPCs.

## v0.17.5.2

Funnel waypoint pruning. Reduces SSFA micro-corner waypoint noise on walkmesh corridors that have many small triangle turns. Quantization architecture (v0.17.5) and announcement hysteresis (v0.17.5.1) ship unchanged.

### Motivation

Aaron's v0.17.5.1 BAT on bg2f_2 (classroom hallway) revealed that the SSFA funnel was producing 13 waypoints for a 1600-unit path. Tracing the SSFA against the corridor's actual portal geometry showed that the algorithm was correctly identifying every walkmesh triangle's portal-vertex alternation as a turn point -- but most of those "turns" don't represent real bends from the player's perspective.

Perpendicular-distance check of each waypoint against the line through its neighbors:

| Waypoint | Off line | Real corner? |
|----------|----------|--------------|
| wp 1 | 220 units off wp 0->wp 4 | YES (real bend) |
| wp 2 | 6 units off wp 1->wp 4 | no (collinear-ish) |
| wp 3 | 24 units off wp 1->wp 4 | no |
| wp 4 | 30 units off wp 1->wp 5 | no |

With hysteresis filtering brief sector flips (v0.17.5.1), each of these waypoint advances still produced a cardinal-change announcement, since they each spanned >500ms of walking. So on this corridor Aaron heard "east, northeast, north, northeast, north, northwest, north..." instead of "east, north" (the two macroscopic legs).

### What ships

New function `PruneCollinearWaypoints` in `field_nav_pathfinding.inl`, called at the end of `FunnelPath` after the funnel produces its waypoint list. Algorithm:

1. For each interior waypoint B (with neighbors A and C), compute perpendicular distance from B to the segment AC.
2. If perpDist < `PRUNE_PERP_EPSILON` (50 units), remove B from the list.
3. Repeat the sweep until no more removable waypoints (sweep-to-stable).

First and last waypoints are preserved. Iteration is capped at 100 sweeps as a safety bound.

50-unit epsilon was chosen conservatively below typical FF8 wall thickness (~100+ units). Combined with the existing `AGENT_RADIUS = 30` portal shrinking (waypoints are already pulled 30 units inward from walls), worst-case post-prune wall clearance is ~80 units -- still well within walkable space. Real corners (like wp 1 above at 220 units off) are never touched.

### Properties

- **Reduces micro-corner cardinal changes.** Aaron hears one announcement per real corner, not one per walkmesh-triangle bend.
- **Preserves object avoidance.** Pruning eps is below wall thickness, so corners that actually route around walls/objects (where the perp distance is large) survive.
- **Affects both GPS and autodrive.** Both read the same `s_waypoints` array from FunnelPath. Autodrive benefits from fewer waypoints to navigate too, and its existing stuck-recovery handles any wall-grazing.
- **Reversible.** If a future field surfaces a real corner under 50 units of perp distance, lowering the constant or skipping the prune for that field is a one-line change.

### Files changed

- `src/ff8_accessibility.h` -- version bump (0.17.5.2)
- `src/field_nav_pathfinding.inl` -- `PruneCollinearWaypoints` function added; `FunnelPath` calls it before logging; log line now reports both pre- and post-prune counts as `[funnel] N triangles -> M waypoints (post-prune; pre-prune=K, was J centers)`

### What's NOT touched

v0.17.5 quantization (working). v0.17.5.1 announcement hysteresis (working). The SSFA funnel itself (its waypoint output is still optimal in the geometric sense; pruning just discards waypoints that don't represent meaningful turns). EdgeMidpointPath fallback (used by autodrive when funnel fails). v0.16.5.2 BAT triage backlog.

### Known remaining gap (for v0.17.5.3 or later)

Even with pruning, the FIRST cardinal on bg2f_2 will still be "east" because the path genuinely starts by going east before bending north (the corridor curves around the central pillar visible in Aaron's BAT screenshot). Aaron's mental model focuses on the final destination, which is north of the start. Resolving this fully will require hybrid announcement (option B from session discussion): announce both the immediate cardinal AND the final-target cardinal, e.g. "east, heading north, 6 steps". This is queued for v0.17.5.3 if Aaron's BAT shows the pruning alone isn't enough.

### BAT recipe

Repeat the v0.17.5.1 BAT path (elevator -> classroom hallway -> classroom -> dorm). Watch for:

1. `[funnel] N triangles -> M waypoints (post-prune; pre-prune=K, was J centers)` lines should show K >> M on corridors with zigzag triangles. bg2f_2's path that was 13 waypoints should drop to 4-5.
2. `[funnel-prune] removed N collinear waypoints (eps=50 units, K sweeps)` lines confirm pruning fired.
3. Aaron's qualitative: fewer cardinal-change announcements per journey. Each one should correspond to a real bend in the path.

## v0.17.5.1

GPS announcement hysteresis. Quantization architecture from v0.17.5 ships unchanged; this point release fixes the TTS rattle Aaron reported in the v0.17.5 BAT.

### Motivation

Aaron's v0.17.5 BAT: quantization worked on 4 of 5 fields (cardinals correct from the first announcement, no warmup needed). Two issues on bg2f_1 (the C-shaped classroom hallway):

1. **TTS rattle.** "The direction / distance was constantly rattling off and spamming the TTS." The v0.17.0 GPS cadence fires on every cardinal sector boundary crossing AND every step-count change. Near a sector boundary the cardinal can flip between two adjacent values for a tick or two; every flip fired an announcement. Step counts decrement frequently as the player walks. The existing 3-second throttle (`GPS_ANNOUNCE_INTERVAL_FAR`) only gated step-only changes -- direction changes always broke through. Result: 1-2 announcements per second on long walks.
2. **"South when I needed north" on bg2f_1.** Aaron's spatial description (enter at bottom point of a C-shape, door at top opposite side, considerably north and west of entry) combined with the quantized axes (RIGHT->world-north, DOWN->world-east) means a real "south" leg would push the player world-east -- away from the door. So this was almost certainly a transient sector flip during the rattle, not a sustained wrong cardinal.

Aaron's spec for the fix: "TTS only announces the direction when it changes, e.g. it says to go north so I keep going north until it eventually says east then I go east."

### What ships

In `field_nav_gps.inl::UpdateGPS`, the v0.17.0 sector-change-driven cadence and the v0.17.1 waypoint-force are replaced with **cardinal-change-only with hysteresis**:

- Announcements fire ONLY when the cardinal changes from `s_gpsLastDirIdx` to something different, AND the new value has held steady for `GPS_DIR_HYSTERESIS_MS = 500ms`.
- Step-count changes never fire on their own.
- Waypoint advances never fire on their own (if the cardinal happens to match across two waypoint legs, the conceptual handoff is silent -- Aaron just keeps walking the same direction).
- Nearby/in-range one-shot announcements are unchanged.

Mechanism: two new statics `s_gpsPendingDirIdx` and `s_gpsPendingDirSince`. When the computed cardinal differs from the last spoken one, it becomes a candidate. If a new candidate appears (different from previous candidate), its timer resets. Only when the candidate has held its value for 500ms is it promoted to `s_gpsLastDirIdx` and announced. Brief sector flips never get past the hysteresis.

### Properties

- **Eliminates the rattle structurally.** Sector boundary jitter can no longer fire because brief flips reset the candidate timer rather than promote.
- **Resolves the bg2f_1 transient.** Whatever caused the one-off "south" announcement (likely a corner-waypoint geometry quirk in the funnel) can no longer reach the screen reader unless it persists for half a second.
- **Matches Aaron's spec exactly.** Silence while walking in a constant direction, announce only when the direction genuinely changes.
- **No effect on the architecture.** v0.17.5 quantization is untouched. The fix is purely in the announcement gate.

### Edge cases the fix handles

- **Long straight walks.** Cardinal stays constant for minutes -> total silence (except Nearby/in-range at the end). Matches Aaron's spec.
- **Genuine corner turn.** Cardinal changes from A to B and B holds steady -> announced after 500ms. Adds ~half a step of delay to corner announcements; acceptable.
- **Wander back out of nearby zone.** Re-prime pending so the next direction change still requires hysteresis confirmation rather than firing immediately.
- **GPS started mid-flight on a stable bearing.** `StartGPS` primes both `s_gpsLastDirIdx` and `s_gpsPendingDirIdx` to the initial cardinal, so `UpdateGPS` doesn't spuriously fire on the first tick.

### Files changed

- `src/ff8_accessibility.h` -- version bump
- `src/field_nav_gps.inl` -- new statics `s_gpsPendingDirIdx`/`s_gpsPendingDirSince`/`GPS_DIR_HYSTERESIS_MS`; reset in `StopGPS`; prime in `StartGPS`; cadence block in `UpdateGPS` rewritten

### What's NOT touched

v0.17.5 quantization (working). v0.17.4 det convention check. v0.17.1 path-aware path building (waypoints still drive the steering target; they just no longer trigger announcements on their own). v0.17.0 ComputeScreenDirIndex math. v0.16.5.2 BAT triage backlog.

### BAT recipe

Walk the same path as v0.17.5 (elevator -> classroom hallway -> classroom -> dorm). Expect:

1. **Drastically fewer `[GPS] Update` lines in the log.** Each one's tagged `hysteresis=ok` and `dirChanged=1`.
2. **No back-to-back announcements** for step-count changes.
3. **bg2f_1 hallway**: cardinals should be "north" along the up-leg, "west" along the cross-leg (or "northwest" near the bend), no spurious "south". If a spurious south DOES sneak through, it means whatever produced it was stable for >500ms, which is a separate issue from rattle and we look at the .ca data.
4. **Aaron's qualitative report.** Silence while walking in a stable direction. Announcement at each genuine cardinal change. Same architecture-level correctness as v0.17.5.

## v0.17.5

Replaces v0.17.4's passive movement-driven calibration with a **load-time 90-degree quantization** of the CA-derived camera axes. Same end result as a perfect calibration on the four well-behaved fields, deterministic on field load, and zero state machine.

### Motivation

Aaron asked the right question after the v0.17.4 BAT: "is it really necessary to have this calibration? It seems like the ideal solution would be for the mod to automatically calibrate each field based on the field's unique data upon field load, and not when the character moves." Looking back at the v0.17.3 BAT clean samples through that lens, the engine's actual arrow -> world direction was world-axis-aligned on every tested field:

| Field | CA camRight angle | Engine RIGHT direction | World cardinal |
|-------|-------------------|------------------------|----------------|
| bghall_1 | 7.8 deg | (1, 0) | 0 deg (snap from 7.8) |
| bghall_4 | 23.8 deg | (1, 0) | 0 deg (snap from 23.8) |
| bg2f_1 | 65.4 deg | (0, 1) | 90 deg (snap from 65.4) |
| bg2f_2 (det-fixed) | 60.5 deg | ~(-0.19, 0.98) | 90 deg (5-11 deg residual) |
| bgroom_1 | -62.5 deg | (0, -1) | -90 deg (snap from -62.5) |

The CA value rounds to the engine's actual direction in every case. The engine appears to use a 90-degree-quantized form of its camera matrix when mapping DIJOYSTATE2 lX/lY to walkmesh delta. So we can do exactly that at load time and skip the entire observation-based calibration loop.

bg2f_2 (classroom) has a 5-11 deg residual after quantization. That's well within the 22.5 deg cardinal sector tolerance, and the v0.17.4 BAT proved bg2f_2 navigates correctly via the det fix alone with the residual baked in.

### What ships

The det convention check from v0.17.4 stays unchanged. v0.17.5 adds **one quantization block** in `HookedFieldScriptsInit` after the det fix:

1. Compute `angleR = atan2f(camRight.y, camRight.x)`.
2. Snap to nearest 90 deg: `snappedR = roundf(angleR / (PI/2)) * (PI/2)`.
3. Regenerate camRight as a unit vector at the snapped angle.
4. Derive camDown from camRight via the rotation `(x, y) -> (y, -x)` which is R(-90 deg) and exactly the det = -1 screen-down convention (independent quantization of camDown could break orthogonality near 45 deg boundaries, so we don't do that).
5. Mirror the quantized pair to `s_driveCam*` so the auto-drive private pair starts from the same baseline.
6. Source tag becomes `"ca-quantized"` and the load-time log includes both the original CA angle and the snapped angle.

The `[NAV-OBSERVE]` log from v0.17.3 stays in place as pure diagnostic. The observer now compares engine measured direction against the QUANTIZED prediction, so a future field where the engine doesn't match 90-deg snap will surface as DIVERGE > ~12 deg in the log.

### What got ripped out

From v0.17.4 and v0.17.5-pre:

- `ObsCalibrateAxes()` function and its call from `ObserveArrowResponse`.
- `s_fieldCalibratedManual` static flag and its reset at field load.
- Observer hold-state reset at field load (no longer needed without the cal logic).
- Include order change that put observe.inl before fieldscripts.inl (observe.inl is back at the end where it was in v0.17.3).
- The v0.17.5 filter constants (`OBS_CALIB_AXIS_ALIGN_MAX`, `OBS_CALIB_MAX_ROT_DEG`, etc.).

The v0.17.4 BAT-induced TTS rattle is structurally impossible now: cardinals are computed from axes that never change after field load.

### Properties

- **Deterministic.** Same .ca file -> same axes. No timing windows, no "walk for 500ms before cardinals are right."
- **No state machine.** No flag, no observer dependency for correctness.
- **No regressions.** Filter retest against the v0.17.4 BAT data shows: bghall_1 -> camRight=(1,0) (matches engine exactly), bg2f_1 -> camRight=(0,1) (matches), bg2f_2 -> camRight=(0,1) (matches engine within 11 deg), bghall_4 -> camRight=(1,0) (matches), bgroom_1 -> camRight=(0,-1) (matches).
- **Observer becomes pure diagnostic.** If a future field violates the 90-deg-quantized model, DIVERGE > ~12 deg in `[NAV-OBSERVE]` shows it and we revisit.

### Edge cases the math handles

- 45 deg boundary (e.g., camRight at exactly 45 deg): `roundf(0.5)` rounds away from zero, so the snap is consistent. camDown is derived from camRight rather than independently quantized, so the 90 deg relationship is always preserved.
- Floating-point residuals from `cosf(pi/2)` returning ~6e-8 instead of 0 are clamped to 0 so the log reads cleanly.
- Both det conventions (-1 raw + det-corrected, +1 -> negate camDown to get det = -1, then quantize) end up at the same standard right-handed quantized axes.

### Files changed

- `src/ff8_accessibility.h` -- version bump
- `src/field_navigation.cpp` -- comment block above `s_camRight/Down` rewritten to describe the v0.17.5 architecture; removed `s_fieldCalibratedManual` static; restored original observe.inl include position (after diagnostics.inl)
- `src/field_nav_observe.inl` -- removed `ObsCalibrateAxes` function, call site, and v0.17.4/.5 filter constants; restored v0.17.3 "purely observational" comment block
- `src/field_nav_fieldscripts.inl` -- removed observer/lock reset block; added quantization step inside the CA-init branch after the det fix; updated source tag to `"ca-quantized"`

### What's NOT touched

v0.17.4 det convention check (proven correct and necessary for bg2f_2 and any other left-handed CA fields). v0.17.0.1 2D normalization. v0.17.2 state separation between manual-nav and auto-drive axis pairs. v0.17.1 path-aware GPS. v0.17.3 observer logging (now diagnostic-only, same as it was originally). Auto-drive's empirical calibration (separate state pair, unaffected). v0.16.5.2 BAT triage backlog.

### BAT recipe

Repeat the v0.17.4 BAT pass. Expect:

1. `[NAV-PROJ-INIT] det-correction` line for bg2f_2 only.
2. `[NAV-PROJ-INIT] quantization: camRight pre=(...) angle=(...) -> snap=(...) -> camRight=(...) camDown=(...)` line for every field that successfully loads its .ca file.
3. `[NAV-PROJ-INIT] field=... source=ca-quantized` summary line.
4. **NO `[NAV-CALIB-AUTO]` lines anywhere.** That function is gone.
5. `[NAV-OBSERVE]` lines as before (still throttled to 1.5s), now showing DIVERGE against quantized prediction. Expect DIVERGE near 0 on every field except bg2f_2 where DIVERGE will be 5-11 deg (within sector tolerance, no action needed).
6. **Aaron's qualitative report.** All five fields should navigate correctly from the first cardinal announcement after entering. No "walk a bit before cardinals work," no TTS rattle.

## v0.17.4

The fix the v0.17.3 BAT diagnosed. v0.17.3's observer logged the world-space response to single-arrow key presses across `bghall_1`, `bghall_4` (elevator field), `bg2f_1` (2nd-floor hall), `bg2f_2` (classroom), and `bgroom_1` (dorm). Two findings:

**Finding 1 — the engine's screen-to-world transform is a uniform rotation per field, not a per-arrow thing.** On `bgroom_1`, all 14 axis-aligned clean samples agreed on -27.5° (CW), stdev 0.49°. On `bg2f_1`, all 8 clean samples agreed on +24.6° (CCW), stdev 0.0°. `bghall_1` had -7.8° (small enough Aaron didn't notice), `bghall_4` had -23.8° (Aaron reports works perfectly — borderline but the elevator corridor's geometry tolerates it). Meaning: a single clean observation per field is enough to determine the rotation matrix that maps CA-derived axes to the engine's actual axes. The chase auto-pilot's empirical calibration has been doing this all along; the manual-nav pair never adopted the technique.

**Finding 2 — `bg2f_2` (classroom) had `det(camRight, camDown) = +1.0`, left-handed CA axes.** All other fields in the BAT had `det = -1.0`. With det=+1, the 2D projection of `.ca` axis1 ends up pointing world-UP instead of the standard world-DOWN convention. The mod's prediction for UP/DOWN arrows comes out exactly opposite of the world direction Aaron actually moves — "had to go opposite the instructions" matches the math precisely. With axis1 negated to force `det=-1`, the residual rotation for `bg2f_2` becomes ~+30-40° (in line with the other tilted-camera fields, varying with noise from Aaron walking through curves).

The two paired fixes ship together because they target the same end-to-end outcome (cardinals match Aaron's walking direction) and the diagnostic distinguishes them in the log:

### Fix 1: det convention check at CA load

In `field_nav_fieldscripts.inl` after the v0.17.0.1 2D normalization, compute `det = camRight.x*camDown.y - camRight.y*camDown.x`. If positive, negate `camDown` to force `det=-1`. Logs a new `[NAV-PROJ-INIT] det-correction` line when this fires.

The negation propagates to the drive-private pair (auto-drive starts from CA values), but auto-drive's empirical calibration overwrites those on first run, so chase auto-pilot is unaffected. The only visible behavior change is that fields with det=+1 (rare, e.g. `bg2f_2`) now project to UP/DOWN cardinals using the corrected direction.

### Fix 2: passive self-correcting calibration

In `field_nav_observe.inl`, a new `ObsCalibrateAxes()` function fires from the per-tick observer when:

- Single arrow held for >= 30 ticks (~500ms; stricter than the diagnostic log's 18 ticks so the engine has settled into a stable motion direction).
- Measured movement delta >= 100 world units (twice the diagnostic threshold).
- `dot(predicted, measured) >= 0.5` (within a 60° cone of the current axes — rejects samples where Aaron walked through a wall or a curve and the engine's actual direction was deflected, which v0.17.3 BAT showed as 95-100° outliers).

When all three pass: compute `theta = atan2(cross(predicted, measured), dot(predicted, measured))` and rotate BOTH `s_camRightX/Y` and `s_camDownX/Y` by `theta` using the standard 2D rotation matrix. Update `s_camAxesSource` to `"calibrated"`. Log a `[NAV-CALIB-AUTO]` line with the old axes, new axes, and rotation magnitude.

Uniform rotation across the four arrows (Finding 1) means a single clean observation calibrates the field for all subsequent cardinal computations. If the camera changes mid-field (unlikely but possible on multi-section fields), the next clean observation re-calibrates. Wall-deflection samples are filtered out by the 60° cone, so axes don't wobble on noisy data.

The diagnostic `[NAV-OBSERVE]` log from v0.17.3 stays in place at the lower 18-tick / 50-unit / no-cone thresholds so the next BAT can still surface residual divergence patterns if they exist.

### What Aaron should experience

On entering a field, the first GPS cardinal still uses CA-derived axes (possibly off by 0-40°). When he holds an arrow to walk in that announced direction for ~500ms, calibration fires and rotates the axes to match the engine. The next GPS update announces a corrected cardinal. Total mismatch window: about half a second.

For `bghall_1` (small -7.8° rotation), Aaron likely won't notice anything different — cardinals stayed within the 22.5° sector tolerance even before calibration. For `bg2f_1`, `bg2f_2`, and `bgroom_1`, the mismatch window is the only time wrong cardinals appear; after that, cardinals are accurate. For `bghall_4` (elevator, -23.8° rotation, Aaron reported works perfectly), behavior should also improve slightly though Aaron was already comfortable with it.

### Files changed

- `src/ff8_accessibility.h` — version bump
- `src/field_nav_fieldscripts.inl` — det convention check after CA 2D normalization
- `src/field_nav_observe.inl` — new `ObsCalibrateAxes()` function plus `ObsCalibrateAxes(...)` call from `ObserveArrowResponse()`; comment block at top updated to reflect that the observer now writes state
- `src/field_navigation.cpp` — comment block above `s_camRight/Down` updated to document v0.17.4 observer writes

### What's NOT touched

Auto-drive's empirical calibration code in `field_nav_autodrive.inl` (proven correct, never read s_camRight/Down anyway). v0.17.2 state separation (preserved verbatim — drive pair and manual pair stay independent except for the field-load mirror). Chase auto-pilot config tables. v0.17.1 path-aware GPS. v0.17.0.1 2D normalization. The v0.16.5.2 BAT triage backlog. Classroom entity catalog under-population (parallel track — still needs Aaron's field-name lookup and F9 list).

### BAT recipe

Repeat the v0.17.3 BAT pass: load save in `bghall_1`, walk through the same fields holding each cardinal for 2-3 seconds. Watch for new `[NAV-CALIB-AUTO]` log lines. Each field should produce one `[NAV-CALIB-AUTO]` line within seconds of the first eligible arrow hold; subsequent `[NAV-PROJ] start` lines for GPS sessions on that field should show `axes=calibrated` and updated `camRight`/`camDown` values.

Qualitative test: GPS-guide to a target on `bg2f_1` and `bg2f_2`. Initial cardinal announcement may briefly point the wrong way; after walking a step or two, subsequent announcements should be correct. End-to-end navigation should succeed without Aaron having to go opposite the instructions.

## v0.17.3

Diagnostic-only build. No behavior change. Adds a passive observer that logs the empirical world-space response to single-arrow key presses alongside the .ca-derived prediction, so the next BAT log shows directly whether the CA file values match the engine's actual screen-to-world projection on the fields where manual navigation has been giving wrong cardinals.

Background from the v0.17.2 BAT: Aaron loaded a save in `bghall_1` (Balamb Garden hallway), walked to the classroom. The `[NAV-PROJ]` line for the `bg2f_1` GPS session showed `axes=ca-file` (state separation working as designed: calibration can no longer leak into manual nav). But cardinals on `bg2f_1` and the classroom were still wrong, while the field outside the elevator -- previously the worst case -- now navigates correctly. That distribution rules out calibration corruption (hypothesis A) and confirms CA-vs-engine mismatch (hypothesis B): some fields' .ca data happens to align with the engine's actual projection, others don't, and which is which can't be derived from CA values alone.

The chase auto-pilot's empirical calibration in `field_nav_autodrive.inl` has always produced correct axes because it injects analog input and measures the resulting walkmesh delta -- it doesn't trust .ca, it observes reality. v0.17.3 does the same observation passively (no input injection) using Aaron's actual keypresses as the test signal. Each time he holds a single arrow for long enough to produce measurable movement (>= 18 ticks held, >= 50 world-unit delta), the observer logs a comparison line:

```
[NAV-OBSERVE] field='bg2f_1' axes=ca-file arrow=RIGHT held=22ticks delta=(120,-50)
              measured=(0.92,-0.38) predicted=(0.417,0.909)
              DIVERGE=68deg | camRight=(0.417,0.909) camDown=(0.909,-0.417)
```

The `DIVERGE` angle is the diagnostic core: 0 deg means CA values match the engine's actual projection for this arrow direction, 180 deg means exact opposite (both components signed wrong), 90 deg suggests axes swapped (axis0 and axis1 reversed in the .ca file's labeling convention), and any other angle is something more complex that needs the data to explain. Across multiple samples per field, the divergence pattern surfaces the transformation needed.

### How the observer self-gates

The sample is invalid if any of these is true, so the observer skips logging:

- An auto-drive (F9 path-finding or chase auto-pilot) is running -- synthetic key injection would pollute the measurement.
- The player entity hasn't been detected yet.
- A dialog is open -- engine ignores movement input.
- More than one arrow is held -- a diagonal press averages two camera-axis directions, ambiguous for clean per-axis comparison.
- Less than 18 ticks (~300ms at 60fps) of held time -- engine hasn't settled into movement.
- Less than 50 world units of measured delta -- noise rather than signal.
- Less than 1.5 seconds since the last sample -- throttle so a continuous hold doesn't flood the log.

With those gates, Aaron walking around naturally produces one log line per arrow direction per second or so. A test pass that walks a couple of seconds in each cardinal direction on each field of interest fills the log with the data needed to design v0.17.4.

### Why GetAsyncKeyState instead of the engine's keyboard buffer

The observer reads arrow state via `GetAsyncKeyState(VK_UP/DOWN/LEFT/RIGHT)` rather than the engine's keyboard buffer at `*0x01CD02D8`. Two reasons. First, GetAsyncKeyState reflects Aaron's physical key presses regardless of any in-engine remapping (FF8 lets the player remap keys; Aaron almost certainly hasn't, but the diagnostic shouldn't depend on that assumption). Second, the engine's keyboard buffer is written to by chase_keyboard during chase Auto and by FFNx during normal play; reading from the OS layer instead of the engine layer keeps the observer self-contained and removes a dependency on hook timing that doesn't matter here (the observer gates on auto-drive being inactive anyway, so synthetic injection isn't a confounder).

### Files changed

- `src/ff8_accessibility.h` -- version bump
- `src/field_nav_observe.inl` -- NEW file, ~150 lines, contains the observer state, helpers, and `ObserveArrowResponse()` function called from `Update()`.
- `src/field_navigation.cpp` -- include the new .inl after `field_nav_diagnostics.inl` (so it can see all helper functions from earlier includes) and add `ObserveArrowResponse();` to `Update()` right after `UpdateGPS();`.

### Not touched, deliberately

Projection math in `field_nav_gps.inl::ComputeScreenDirIndex` (still uses CA-derived `s_camRight/Down` -- v0.17.4 will know how to transform those once we have the BAT data). `field_nav_autodrive.inl` empirical calibration (proven correct, untouched). Chase auto-pilot config. v0.17.2 state separation (proven working). v0.17.1 path-aware buffer + advance logic. v0.17.0.1 CA 2D normalization. The v0.16.5.2 BAT triage backlog. The classroom entity catalog under-population (separate track).

### BAT recipe

Load a save in `bghall_1`, walk slowly in each cardinal direction (north / east / south / west) for at least 2-3 seconds, holding only one arrow at a time. Transition to `bg2f_1` and do the same. Transition into the classroom (the field that's been giving the worst cardinals) and do the same. The field log will contain `[NAV-OBSERVE]` lines with field name, arrow direction, measured vs predicted world direction, and divergence angle. Aaron can also include the elevator-side field that v0.17.2 BAT confirmed works correctly -- that gives a known-good baseline for what the divergence looks like when CA is correct.

Ideal sample count: 4 cardinals per field, across 3-4 fields, total ~12-16 lines. Plus whatever incidental samples happen during normal walking. Throttling ensures the log doesn't explode even on long sessions.

What the v0.17.4 fix looks like depends on what the data shows. If divergence is consistent per field (e.g. always 0 deg on field A, always 180 deg on field B, always 90 deg on field C), the fix is a per-field transformation table or a single geometric flip applied conditionally on a tractable .ca header value. If divergence varies within a single field (different per camera section), the fix needs to be runtime: read the engine's current camera state at the moment of projection rather than the static .ca snapshot. The observer data resolves the ambiguity in one BAT cycle.

## v0.17.2

Follow-up to v0.17.1 BAT triage. v0.17.1 added path-aware GPS (A* + funnel waypoints) and the BAT log confirmed the path-aware logic works correctly — two GPS sessions on the test field showed the funnel producing 11 and 38 waypoints respectively, the waypoint advance routine fired through the sequence, and the overshoot-detection branch caught three sub-arrive-distance passes. The new logic is sound. But the announced cardinals didn't match the direction Aaron actually had to walk, and the BAT log surfaced the cause: the `[NAV-PROJ]` lines at GPS-start time recorded camera axes `(0.493,0.870)`/`(-0.871,0.492)`, which are NOT the CA-derived axes that fieldscripts.inl logged for that same session at field load (`bghall_1` first load: `(0.991,0.135)`/`(0.134,-0.991)`). Something had overwritten the manual-nav camera axes between field load and the GPS test.

The most likely culprit is the auto-drive empirical calibration in `field_nav_autodrive.inl`. Phases 1 and 2 of that calibration inject `lX=+1000`/`lY=+1000`, measure the resulting walkmesh-delta direction, and write the normalized result back into `s_camRightX/Y` and `s_camDownX/Y` — the same statics that GPS and `FormatNavComponents` read for screen-relative projection. The calibration was originally added in v06.14 to give the chase auto-pilot's analog steering correct screen-to-world conversion, but it shares state with manual-nav by virtue of writing to the same module-level statics. That shared state never mattered before v0.17.0 because manual-nav didn't actually consume `s_camRight/Down` for cardinal labels — only the chase code did. v0.17.0 changed that: it wired CA-derived values into the same statics and pointed manual-nav at them. The wiring works at field load, but if calibration runs at any point afterward — even on a different field, even briefly — its writes leak into manual-nav's projection for the rest of the session or until the next field load.

A secondary possibility, raised by the chase auto-pilot lessons document, is that empirical calibration and `.ca`-derived axes don't produce identical results on every field. The chase doc's "What chase-drive proved works" list is explicit: empirical calibration produces correct axes (verified across multiple BATs on multiple rotated-camera chase fields). The CA-derived equivalent is unverified at that level of rigor — Finding #10 mentions it as a future option, not a proven equivalent. If CA-axes diverge from what the engine actually uses for screen projection on some fields, manual-nav cardinals will be wrong on those fields regardless of whether calibration runs.

v0.17.2 distinguishes the two hypotheses by splitting the camera-axes state pair, so manual-nav and auto-drive consume axes from independent sources that can no longer cross-contaminate:

- **`s_camRightX/Y, s_camDownX/Y`** is now the MANUAL-NAV pair. Set once by `HookedFieldScriptsInit` at field load — from the `.ca` file via v0.17.0.1's 2D-normalization path, or identity defaults if the `.ca` is absent / degenerate. Never written by auto-drive. Read by `field_nav_gps.inl::ComputeScreenDirIndex`, `field_nav_gps.inl`'s `[NAV-PROJ]` log lines, and `field_nav_helpers.inl::FormatNavComponents`.
- **`s_driveCamRightX/Y, s_driveCamDownX/Y`** is a new AUTO-DRIVE PRIVATE pair. Mirrors the manual-nav pair at field load (so auto-drive starts from CA-derived values on the first drive of each field), then overwritten by the calibration's phase 1 / phase 2 writes. Read only by `field_nav_autodrive.inl::SetAnalogFromVector` (and through it by chase auto-pilot + F9 path-finding's analog steering).

With this split, the next BAT log answers the question definitively. If manual-nav cardinals are now correct on `bghall_1` end-to-end, hypothesis A (calibration corrupting manual-nav) was the cause and the fix is complete. If the cardinals are still wrong on the same field, hypothesis B (CA-derived axes diverge from the engine's actual projection) is in play and v0.17.3 will need a deeper fix — either auto-drive-style empirical calibration on GPS start, or reading the engine's runtime camera matrix from memory rather than the file-load `.ca` snapshot.

Diagnostic logging added so the BAT log tells us which case we're in without needing further log forensics. The `[NAV-PROJ] start` line at `StartGPS` now includes the current field name and a new `axes=` tag that reads either `ca-file` (CA loaded and 2D-normalized successfully) or `identity` (CA absent, degenerate, or fell through to fallback). New static `s_camAxesSource` in `field_navigation.cpp` tracks this; `field_nav_fieldscripts.inl` sets it at every field load. With the source tag in every NAV-PROJ line, a v0.17.2 BAT log showing two GPS sessions on `bghall_1` will show the same `axes=ca-file` and the same camera-axis values on both — confirming the manual-nav pair is stable through the session. If the values still differ, the difference is now provably a real `.ca`-vs-engine mismatch and points to v0.17.3 work; calibration is no longer a possible culprit.

Chase auto-pilot is deliberately untouched. The empirical-calibration code path in `field_nav_autodrive.inl` still runs (phases 1, 2, fallback-perpendicular, and the `[CALIB]` log lines all retained); the only change is that it writes into the `s_driveCam*` pair instead of `s_cam*`. The chase doc's verified-working behavior on `domt4_1`, `domt3_2`, `domt5_1`, `dotown_2`, `dotown_1`, etc. is preserved bit-for-bit, since auto-drive's `SetAnalogFromVector` now reads from the same drive-private pair that calibration writes. The mirror-at-field-load step ensures the first drive on each field starts from CA-derived values rather than the previous field's calibration residue — a small improvement, but a side effect of the state split, not its purpose.

v0.17.1's path-aware logic is also untouched. `BuildGpsPath` and `AdvanceGpsWaypoint` still work the way the v0.17.1 BAT confirmed; the only thing changing in `field_nav_gps.inl` is the `[NAV-PROJ] start` log format (added two fields: `field='%s'` and `axes=%s`). Behavior outside the diagnostic logging is byte-for-byte identical.

Files changed: `src/ff8_accessibility.h` (version), `src/field_navigation.cpp` (new state pair + source tag), `src/field_nav_fieldscripts.inl` (reset + CA-load both pairs, set source tag), `src/field_nav_autodrive.inl` (calibration phases + `SetAnalogFromVector` rename `s_cam*` → `s_driveCam*`), `src/field_nav_gps.inl` (NAV-PROJ start log adds field + axes fields).

Not touched, deliberately: chase auto-pilot config tables, `field_nav_pathfinding.inl` (A* + funnel reused unchanged), `field_nav_helpers.inl::FormatNavComponents` (already reads `s_camRight/Down` which is now manual-nav-pinned), the v0.17.1 path-aware buffer + advance logic (proven working last BAT, no reason to risk regression), the v0.16.5.2 BAT triage backlog (FMV STOP/PLAY race, POLL tutorial garble, formation party-member filter, GF-BP diagnostic gating, HP-TRACK during GF-HP-SUB), and the classroom entity catalog under-population reported in v0.17.0.1.

BAT recipe: load `bghall_1` again (or any tilted-camera field). The new `[NAV-PROJ] start` log line will include `field='bghall_1'` and `axes=ca-file`. Walk around the field exercising both manual nav (Backspace/F9) and at least one auto-drive (F9 list cycle then start drive, or chase-trigger if convenient). If manual-nav GPS produces correct cardinals throughout AND a subsequent BAT shows the camera-axis values matching the field's CA load every time GPS announces, the state-separation fix is sufficient. If cardinals are still wrong with `axes=ca-file` in the NAV-PROJ logs, the BAT log will show the same axis values across multiple announces — proving the issue is CA-vs-engine divergence, not calibration corruption, and v0.17.3 needs to address the projection itself.

## v0.17.1

Path-aware GPS direction. v0.17.0/0.17.0.1 made the cardinal genuinely screen-relative on any camera, but the announced cardinal was still the straight-line bearing from player to final destination. On a curved hallway, that bearing cuts through walls. v0.17.0.1 BAT confirmed this on `bghall_1` (Balamb Garden hallway, C-shaped): going classroom → elevator corridor the bearing happened to align with the walkable direction at every step, so the cardinal was correct; going the other way, the bearing pointed through the inside of the curve and the announced cardinal was wrong because the player needed to follow the bend, not aim through it.

v0.17.1 runs A* on the walkmesh from the player's triangle to the target's triangle, smooths the corridor with the existing funnel algorithm (`FunnelPath` in `field_nav_pathfinding.inl`), and announces the cardinal toward the NEXT waypoint rather than the final destination. The funnel produces a small number of turn-point waypoints; on a straight path it produces a single waypoint at the destination, so behavior degrades cleanly to v0.17.0.1's straight-line direction. On a curved path it produces one waypoint per major bend, and the GPS announces the leg-by-leg direction the player needs to walk.

The A* + funnel infrastructure already exists in `field_nav_pathfinding.inl` — chase auto-pilot has been using it since v0.15.9, and F9 path-finding auto-drive uses it from `UpdateAutoDrive`. v0.17.1 calls it from `StartGPS` and copies the result into a GPS-private buffer (`s_gpsWaypoints[]`) so an active auto-drive's path isn't disturbed. The save/restore mechanism in `BuildGpsPath` snapshots the shared `s_waypoints[]`, `s_waypointCount`, `s_waypointIdx`, `s_usingFunnel`, `s_corridor[]`, `s_corridorCount`, and `s_wpMinDist` before A* runs and restores them after the funnel result is copied. If no auto-drive is running, the save/restore is effectively a state-clear and has no observable effect; if auto-drive IS running, its waypoint sequence and progress index are preserved across the GPS path build.

Waypoint advance uses two conditions, evaluated each `UpdateGPS` tick: (a) the player gets within `GPS_WP_ARRIVE_DIST` (200 units) of the current waypoint, or (b) the player overshoots — they got reasonably close (under `GPS_WP_OVERSHOOT_CLOSE` = 300 units) and the distance is now growing again, indicating they passed the waypoint at an angle. Either condition advances to the next waypoint. The 200-unit threshold is generous compared to auto-drive's 60-unit `FUNNEL_ARRIVE_DIST` because the player walking themselves doesn't need precise turn points — the threshold's job is to advance the announced direction BEFORE the player reaches the corner, so they have time to plan their turn. The overshoot detection mirrors auto-drive's same-named feature for the same reason.

The announcement cadence from v0.17.0 still applies: silent in the nearby/in-range zone, fires on cardinal sector change or step-count change outside it, with a 3-second minimum interval that direction changes break through. v0.17.1 adds one new break-through condition: a waypoint advance forces an announcement even when the cardinal happens to match the previous one. This matters for L-shaped paths where two legs both run, say, east — without the forced announcement, the player would never get a corner-handoff signal. The forced announcement uses the same cardinal text but fires immediately on advance regardless of the time-interval floor.

Fallback behavior is unchanged from v0.17.0.1 whenever A* can't run: walkmesh not loaded, target not on the walkmesh, player and target on disconnected walkmesh islands, or A* iteration limit exhausted. In all those cases `BuildGpsPath` returns false, `s_gpsUseWaypoints` stays false, and `UpdateGPS` aims straight at the final destination as before. The `[NAV-PATH]` log lines record which fallback path fired, so a future BAT where path-aware misbehaves can be diagnosed by reading the log without re-running.

Diagnostic logging additions: `[NAV-PATH]` lines at `BuildGpsPath` covering walkmesh availability, start/goal triangle indices, A* success/failure, funnel waypoint count, and the first 6 waypoint positions; `[NAV-PATH] wp N/M reached` on each advance with the reached/overshoot reason; the existing `[NAV-PROJ] start` / `[NAV-PROJ] update` lines now include `wp=I/N` and a `steer=(x,y)` field separate from `target=(x,y)` so the path-aware behavior is fully traceable.

Trigger-line targets get special handling: when the GPS target's catalog entry is a trigger line (`entityIdx <= -200` and `> -300`), `BuildGpsPath` passes the trigger index as `skipTriggerIdx` to `ComputeAStarPath` so A* can route through that specific trigger line (it's the goal). Without this, A* would refuse to enter the destination triangle because crossing the trigger line is forbidden by default. Runtime-entity targets (`entityIdx >= 0`) pass their entity index as `targetEntityIdx` so A*'s push-radius blackout doesn't block the goal triangle.

Files changed: `src/ff8_accessibility.h` (version), `src/field_nav_gps.inl` (path-aware state, `BuildGpsPath` helper, `AdvanceGpsWaypoint` helper, `StartGPS`/`UpdateGPS`/`StopGPS` integration with path-aware mode, expanded diagnostics).

Not touched, deliberately: `field_nav_pathfinding.inl` (A* + funnel implementation is reused as-is), `field_nav_autodrive.inl` (auto-drive's waypoint state save/restore is handled at the GPS-call boundary, not by changing auto-drive), `field_navigation.cpp` GPS_DIR_NAMES (the cardinal vocabulary is the same), the v0.16.5.2 BAT triage's other backlog bugs (FMV STOP/PLAY race, POLL tutorial garble, formation-based party-as-NPC filter, GF-BP diagnostic spam, missing damage announce during GF-HP-SUB), and the classroom entity catalog under-population reported in the v0.17.0.1 BAT (separate diagnosis track once we have the field name and the F9 list).

BAT recipe: load `bghall_1` (or any curved-corridor field). Cycle F9 to a target on the opposite side of the curve, press Backspace to start GPS guidance. The initial announcement should be the cardinal toward the first turn point, not the final destination. Walking along the corridor should produce a `[NAV-PATH] wp 0/N reached` log line and a fresh direction announcement at each bend. Going the reverse direction — same field, opposite endpoints — should now produce correct directions; that was the v0.17.0.1 BAT failure case. On straight fields (`bgroom_1`, default-camera open areas), behavior should be indistinguishable from v0.17.0.1.

## v0.17.0.1

Follow-up to v0.17.0 BAT triage. v0.17.0 confirmed the orientation infrastructure in principle — most fields improved — but two specific fields (`bghall_1` Balamb Garden hallway and the classroom outside it) still produced wrong cardinals. The BAT logs surfaced the cause immediately: `bghall_1`'s `[NAV-PROJ-INIT]` line showed `camRight=(0.991,0.135) camDown=(0.044,-0.330)`. camRight's 2D magnitude is 1.0; camDown's is only 0.333. The asymmetry biased every `atan2(sD, sR)` toward east/west and produced the wrong cardinal even when the screen-direction was unambiguously north or south.

Root cause was a missing normalization step in the v0.17.0 CA-to-`s_camRight/Down` wiring. The `.ca` file stores camera axes as **3D unit vectors** (int16 fixed-point /4096). For a tilted camera — where the rendering camera looks forward+down at the floor instead of straight down — most of axis1's magnitude lies in the Z (depth) component; the XY projection is short. v0.17.0 divided by 4096 and used the raw XY components, which left `s_camDown` at sub-unit-length on tilted-camera fields. Walkmesh deltas have Z=0, so dotting them against a short-XY-vector produced screen-space deltas with asymmetric scale: the screen-right projection was at full scale while screen-down was at fractional scale. `atan2(sD, sR)` reads ratios, so the asymmetry rotates the apparent angle toward the larger axis's direction.

The chase auto-pilot's empirical calibration in `field_nav_autodrive.inl` doesn't have this problem because it measures the resulting walkmesh-delta direction and normalizes by `cdist` (the magnitude of the delta). The result is always a unit-length 2D vector matching the engine's actual analog-to-walkmesh transform. v0.17.0.1 mirrors that: it normalizes axis0's and axis1's 2D projections to unit length before writing into `s_camRight/Down`.

The geometric justification matches the chase calibration's empirical observation. When the engine reads analog input `(lX, lY)` and converts it to walkmesh movement, it follows the camera axes' 2D projection as a *direction* — the player's walking speed is constrained to the engine's pace, not the axis vector's magnitude. So the walkmesh direction of "press right arrow" is the unit-length 2D projection of axis0, regardless of how much of axis0's 3D magnitude lies along Z. Same for camDown and axis1.

Default-camera fields produced correct cardinals on v0.17.0 because their axis0 and axis1 already have near-zero Z components (the 2D magnitude was ~1.0 already, so normalization is a no-op). The bug only surfaced on tilted-camera fields where axis1's Z component is large — a class of fields that includes the Balamb Garden interiors (`bghall_1`, `bgcls_1`/classroom, and likely most of the indoor environments with non-default camera framings).

Two extra `[NAV-PROJ-INIT]` log fields: `source=ca-file-normalized` (was `ca-file`) so a v0.17.0 build vs v0.17.0.1 build is unambiguous from the log; and a per-field `raw-2D r2len=N d2len=M` line showing the pre-normalization 2D magnitudes. `d2len` near 1.0 means a flat (default) camera; `d2len` significantly less than 1.0 means a tilted camera. This makes "is this a tilted-camera field?" a one-line lookup in the field log.

Also adds a degenerate-projection safety check: if both `r2len` and `d2len` are essentially zero, the camera is looking straight down a single world axis and the screen-projection is undefined for direction labels. v0.17.0.1 keeps identity defaults in that case and logs a warning rather than dividing by ~0 and producing NaN.

Files changed: `src/ff8_accessibility.h` (version), `src/field_nav_fieldscripts.inl` (CA wiring now normalizes 2D projections, extra diagnostic log lines, degenerate-camera guard).

Not touched: `src/field_nav_gps.inl` (the projection math was correct in v0.17.0 — only the inputs to it were wrong), `src/field_navigation.cpp` `GPS_DIR_NAMES` cardinals (unchanged from v0.17.0), the chase auto-pilot's empirical calibration path (also unchanged — it normalizes correctly already).

BAT recipe: load any tilted-camera field (`bghall_1` is the canonical example). The `[NAV-PROJ-INIT]` line should now show `camDown` with 2D magnitude ~1.0 (e.g. `(0.132,-0.991)` for `bghall_1`) instead of `(0.044,-0.330)`. Cardinals announced during GPS guidance should match arrow keys. The `raw-2D` line records the pre-normalization magnitudes so the tilt-or-flat status is logged independently.

## v0.17.0

Field navigation, Bug 2 from the v0.16.5.2 BAT triage: manual GPS direction announcements were correct on some fields and inverted on others ("left" when the player actually needed to press right). Root cause was a coordinate-system mismatch: the GPS direction code computed the world-space bearing from raw entity coordinates and labeled it with screen-relative names, which works only on default-camera fields. On any field with a rotated camera — e.g. the Mountain Hideout chase fields, where the chase auto-pilot's empirical calibration found `camRight ≈ (0.04, 0.99)` instead of identity — the labeled direction did not match the arrow key the player needed to press.

The fix has two parts. First, the `.ca` (camera) file is already parsed at field load into the `CameraAxes` struct, but the parsed result was never being wired into the projection axes (`s_camRightX/Y`, `s_camDownX/Y`) that the direction code actually reads. Those projection axes were only ever populated by the chase auto-pilot's empirical calibration, which runs lazily on the first auto-drive of a field — i.e. on chase fields and nowhere else. The rest of the time they sat at their identity defaults. `field_nav_fieldscripts.inl` now derives the screen-right and screen-down vectors from `s_cameraAxes.axis0` and `axis1` (int16 fixed-point, normalized by /4096) and writes them into `s_camRightX/Y/DownX/DownY` immediately after `LoadCameraAxes()` returns. A `[NAV-PROJ-INIT]` log line records the derived axes and a 2D determinant per field load; degenerate cameras (|det| < 0.1) get a warning line. The chase auto-pilot's empirical calibration is untouched and will overwrite the CA-derived values on chase fields when it runs, which is fine because the two methods converge on the same axes for static cameras.

Second, `field_nav_gps.inl` no longer uses the world-space `atan2(dx, dy)`. The new `ComputeScreenDirIndex` projects the walkmesh delta through the now-correctly-populated camera axes (`screenRight = dx*camRightX + dy*camRightY`, `screenDown = dx*camDownX + dy*camDownY`) before classifying into one of eight 45° cardinal sectors. The cardinal vocabulary (north, northeast, east, southeast, south, southwest, west, northwest) replaces the old `up / up right / right / ...` labels; cardinals were chosen because they map unambiguously to arrow keys (north = up arrow, east = right, south = down, west = left) and are the canonical terminology already used by the chase auto-pilot. Aaron confirmed this convention before the rewrite.

GPS announcement cadence also changes. The old loop spoke a direction every 3 seconds while distance > 500 units, every 1.5 seconds while distance was in 200–500, and every 0.8 seconds while distance was under 200 — producing announcement bursts on long stretches where nothing meaningful was changing, and continuous spam in the final approach where the `Nearby` and `In range` one-shots already cover the player. New rule: in the nearby/in-range zone (distance ≤ `s_gpsNearbyDist`) GPS Updates are silent entirely, leaving messaging to the existing one-shots. Outside that zone, an Update fires only when (a) the cardinal sector changes — i.e. the player crosses into a new 45° wedge — or (b) the step count changes AND at least `GPS_ANNOUNCE_INTERVAL_FAR` (3 s) has elapsed since the last announcement. Direction changes break through the minimum-interval floor immediately; step-count-only changes wait it out so the player doesn't hear "11 steps… 10 steps… 9 steps…" in rapid succession on a long approach. State for this is two new statics in `field_nav_gps.inl`: `s_gpsLastDirIdx` (last announced cardinal index) and `s_gpsLastStepsAnn` (last announced step count), both reset by `StartGPS()` / `StopGPS()`.

A `[NAV-PROJ]` log line is added to both `StartGPS` (initial announcement) and the `UpdateGPS` periodic announce path. Each line records player position, target position, walkmesh delta, projected screen delta, and the chosen cardinal label. Combined with the `[NAV-PROJ-INIT]` line at field load, this gives full traceability for any direction the mod announces — if a future BAT exposes a direction that feels wrong, the log line shows exactly which step disagrees with reality. The diagnostic also serves the Bug 2 verification recipe: walk to a known target on a field where pre-v0.17.0 direction was inverted, confirm the announced cardinal matches the arrow key needed, and verify the `[NAV-PROJ]` math against what the player observes.

Scope of this version is the orientation layer only — GPS direction announcements (Backspace-triggered guided nav, F9-list-cycle-then-Backspace, F10 player-position-and-named-destination). Path-aware direction (the second half of Bug 2: "dir=up for 4000 units then suddenly up-left in the final 6 seconds" on bdin3, where the destination is correct but the player needs to follow a bend in the corridor) is queued for v0.17.1 and will reuse the existing A*+funnel infrastructure to target the next funnel waypoint instead of the final destination. Auto-drive integration (replacing the chase auto-pilot's parallel empirical calibration with the same CA-derived axes) is queued for v0.17.2+. v0.17.0 ships orientation alone so a single BAT cycle confirms or refutes the camera-projection approach in isolation before path-aware complexity goes on top — the v0.15.9 chase auto-pilot work hammered home that one-change-per-BAT cycle is the only way to attribute regressions cleanly.

Files changed: `src/ff8_accessibility.h` (version), `src/field_navigation.cpp` (`GPS_DIR_NAMES` array — cardinal vocabulary + comment update), `src/field_nav_fieldscripts.inl` (CA → `s_camRight/Down` wiring at field load, `[NAV-PROJ-INIT]` log), `src/field_nav_gps.inl` (full rewrite of `ComputeScreenDirIndex`, new sector-change cadence in `UpdateGPS`, `[NAV-PROJ]` diagnostic, new state `s_gpsLastDirIdx` / `s_gpsLastStepsAnn`).

Not touched, deliberately: the chase auto-pilot's empirical calibration path (`s_calibPending`, `s_camCalibrated`); the F9/F10 component-readout `FormatNavComponents` (it already uses `s_camRight/Down`, so it inherits the fix automatically); the post-v0.16.5.2 BAT triage's other five bugs (FMV STOP/PLAY race, POLL tutorial garble, formation-based party-member-as-NPC filter, GF-BP diagnostic gating, HP-TRACK during GF-HP-SUB) which remain backlog.

## v0.16.5.2

Defense-in-depth utility change — no mod code change. The DLL behavior is byte-for-byte identical to v0.16.5.1 except `Initialize()` will log `Initialized v0.16.5.2 ...` instead of `v0.16.5.1`.

Mirror the two checks in `.github/workflows/safety-checks.yml` locally in `Utilities/push_to_github.ps1` as new Step 7c, between the duplicate-commit refusal (Step 7b) and the session-header / cmd.exe invocation (Step 8). The CI workflow runs server-side AFTER a push lands — if it fails, the offending commit is already on `main` with a red X next to it, and the only notifications are an email to the committer and (next session) a Claude `github:list_commits` check. The push utility's own success dialog never reflects CI results, so a screen-reader user could come away believing a push succeeded when in fact CI was about to flag it. Step 7c closes that gap by running the same two checks locally and refusing the push (via the existing `Show-ErrorDialog` flow) if either would fail on the server.

### Checks added

- **SET3 hook marker** (mirrors CI job `check-set3-hook`): greps `src/field_navigation.cpp` for `SET3.*PERMANENTLY DISABLED`. The marker is a comment near the disabled SET3 hook block that documents the v0.09.32–v0.09.40 diagnosis showing ANY interception of the SET3 opcode handler hangs the infirmary scene (Dr. Kadowaki walk-to-phone freeze). If the marker is missing, the utility refuses with a screen-reader-readable error dialog naming the file and explaining the consequence.
- **Source file size** (mirrors CI job `source-file-size-check`): walks `src/*.{cpp,inl}` at depth 1 (matches the CI's `find src -maxdepth 1 \( -name '*.cpp' -o -name '*.inl' \)`), checks each file's byte size. Files > 60 KB log a WARN line to `Logs/push_diagnostic.log` as informational (matching CI's warn-but-don't-fail behavior). Files > 80 KB cause refusal with a dialog listing every offending filename and KB size, plus a pointer to the v0.16.0–v0.16.5 splits in CHANGELOG.md as the template for splitting.

### Thresholds (constants in both files)

- `WARN_BYTES = 60 * 1024 = 61440`
- `FAIL_BYTES = 80 * 1024 = 81920`

The duplication between `safety-checks.yml` and `push_to_github.ps1` is intentional and acceptable. The check needs to be fast and offline (no GitHub API round-trip), and the thresholds have been stable since v0.16.0. A header comment in each file points at the other so future maintenance keeps both in sync.

### What this catches vs. what it doesn't

Catches:
- A future source edit that crosses 80 KB without anyone noticing during the edit session.
- Accidentally removing the SET3 marker (e.g. during a refactor that touches `field_navigation.cpp`).
- Cases where Claude or another tool grew a file but didn't trigger a split.

Doesn't catch:
- Files in subdirectories of `src/` (CI also doesn't — only depth 1).
- `.h` files growing large (CI also doesn't — documented exception for `ff8_accessibility_history.h` and `field_display_names.h`).
- Server-side checks added in the future to `safety-checks.yml` that aren't also mirrored here (manual sync required).
- Bypass via direct `git push` from a terminal (the .ps1 is the only path that runs this check; CI is still the authoritative server-side backstop).

### Watch zone

Files in the 60–80 KB range log to `Logs/push_diagnostic.log` as `[Step 7c] Watch zone (60-80 KB, informational): ...`. This gives a passive trail of which files are creeping toward the limit, useful for spotting growth trends across multiple pushes without having to actively monitor. Current watch zone after v0.16.5: `field_archive_jsm_scan.inl` (63 KB, accepted exception), plus several `battle_tts_*.inl` files near the line.

### Future maintenance

If new safety checks are added to `safety-checks.yml` (e.g. a guard against inline-changelog accretion in source headers, or a check for forbidden imports), mirror them as additional sub-blocks under Step 7c. Each check should:
1. Run its detection logic.
2. Append a descriptive failure message to `$ciFailReasons` on failure (don't `exit 1` immediately — collect all reasons so the user sees them in one dialog).
3. Log PASS/FAIL/WARN to `Write-Diag` for the diagnostic trail.

The push utility now treats itself as the canonical client-side enforcement point; CI remains the authoritative server-side backstop in case the utility is ever bypassed.

## v0.16.5.1

Three-line fix wiring `PollDeferredTurnAnnounce()` into `battle_tts.cpp::Update()` after the existing `PollHPChanges()` call. Latent dead-code bug since v0.13.52 (2026-02 timeframe): the deferred turn-announce release function was defined in what is now `battle_tts_menu_poll.inl` but was never invoked anywhere. Whenever a character's ATB filled on the exact frame an enemy attack landed (or a teammate's GF / spell animation was still resolving), `PollTurnAndCommands` would correctly identify the collision, stash "X's turn. <Cmd>." in `s_deferredTurnBuf`, log `[TURN] Deferred (damage in flight): ...`, and set `s_deferredTurnPending = true`. The release path that was supposed to drain that buffer once the damage TTS cleared (or hit the 5-second safety timeout, or cancel on stale activeChar) was simply never called per-frame. The stashed line sat in the buffer until battle end, then got silently wiped when the next battle's `OnBattleEnter` reset state.

Discovered in the v0.16.5 BAT log triage: Selphie's third turn in battle 2 (timestamp 13:24:18, log line 2942) started on the exact frame Zell's Ifrit cast began animating. The defer line `[TURN] Deferred (damage in flight): Selphie's turn. Attack. (tts=0 hp=0 anim=0 engAnim=1)` appeared correctly, Ifrit's GF audio description played end-to-end (~23 s, all 6 cues), the battle continued, and ended ~78 s later — with no `[TURN] Deferred fired ...` or `[TURN] Deferred cancelled ...` log line ever appearing. Grep + dryRun probes across `battle_tts.cpp`, every `battle_tts_*.inl`, `battle_tts_hp.inl`, and `battle_tts_helpers.inl` confirmed no caller existed.

The v0.16.5 split was pure mechanical, so both the function body and the absent call site are byte-for-byte from v0.16.4 and back through v0.13.52. The split did not introduce the bug — it exposed it by giving the BAT triage a clear marker to look for.

### The fix

`src/battle_tts.cpp`, right after the existing `PollHPChanges()` block:

```cpp
if (s_inBattle && s_initAnnounceDone && s_enemyAnnounceDone) {
    PollDeferredTurnAnnounce();
}
```

Guards match the surrounding poll calls (battle active, init done, enemies announced). Placement after `PollHPChanges` matches the function's own header comment so `s_ewmHoldForDamageTTS` reflects this frame's HP signals before the release-decision is made. No change to the function body in `battle_tts_menu_poll.inl`.

### Reproducibility / verification

The trigger window is one frame wide — a teammate's ATB has to fill on the exact frame an attack lands or a GF animation kicks off. Not reliably reproducible on demand. Future battle log review will look for `[TURN] Deferred (damage in flight): ...` followed by `[TURN] Deferred fired after <ms> ms: ...` (success) or `[TURN] Deferred cancelled (char N -> M, stale): ...` (turn already advanced). Pre-fix, only the first line would appear; post-fix, one of the latter two should always follow within ~5 seconds.

### Impact

From the player's perspective: whenever this collision happened (probably a handful of times per dungeon run with junctioned GFs in play), the spoken "<Character>'s turn. <Command>." line was silently dropped, requiring the player to figure out whose menu was open via cursor probing or HP key inspection. With this fix, the line speaks within milliseconds of the damage window clearing, with `PRIO_TURN` interrupting anything lower-priority that might be queued.

## v0.16.5

Pure mechanical split of `src/battle_tts_menu.inl` (81.89 KB monolith — over the 80 KB CI hard-fail line) into a slim 1.05 KB shell plus four sub-`.inl` modules. No behavioral change; turn announcements, command-menu navigation, target selection, all four submenus (Magic / GF / Item / Draw), Stock/Cast prompts, all-target entry/cancel, deferred GF cancel, and the v0.13.52 deferred-turn TTS are byte-for-byte identical to v0.16.4.

This was the FINAL size-split task. With v0.16.5 shipped, every `src/*.cpp` and `src/*.inl` file is under the 80 KB CI hard-fail line. The CI allowlist in `.github/workflows/safety-checks.yml` is now empty. `field_archive_jsm_scan.inl` remains in the watch zone at 63 KB as an accepted exception (will warn but not fail).

Battle menu TTS is user-facing and load-bearing for accessibility (every command, every spell, every submenu cursor move must announce correctly), so this split deliberately did NOT touch the `PollTurnAndCommands` function body. Internal blocks of that ~52 KB function share local variables (`cmdCursorChangedThisFrame`, `subCursor`) and live inside one outer SEH guard — splitting them into separate helper functions would have required scope restructuring that risks regressing menu announcements. The block stays whole in `_poll.inl`.

### New files

- `src/battle_tts_menu_state.inl` (16.2 KB) — all constants (`BATTLE_CMD_CURSOR`, `BATTLE_MENU_PHASE`, `BATTLE_SUBMENU_CURSOR`, `SAVEMAP_*`, `BATTLE_ITEM_*`, `DRAW_*`), name tables (`CHAR_NAMES[8]`, `MAGIC_NAMES[57]`, GF fallback names), free lookups (`GetCommandName`, `GetMagicName`, `GetDrawEntryName`), struct definitions (`MagicEntry`, `GFEntry`, `BattleItemEntry`, `DrawEntry`, `MagicSlot`), all module statics (sub-menu state, deferred-turn TTS, Magic/GF/Item/Draw list state, magic-snapshot state, turn-tracking state). Hoisted to the head of the chain so every subsequent sub-`.inl` sees these declarations.
- `src/battle_tts_menu_lists.inl` (9.6 KB) — the per-turn list builders: `BuildMagicList`, `BuildGFList`, `BuildItemList` (plus `ReadBattleItemEntry` and `GetItemVisualPos` helpers), `BuildDrawList`, and the v0.12.52 Draw-validation pair `SnapshotAllMagicInventories` / `DiffMagicInventories`. All read state from `_state.inl`; consumed by `EnterSubmenu`, `PollTurnAndCommands`, and (for `DiffMagicInventories`) the dialog-injection "Received" path.
- `src/battle_tts_menu_helpers.inl` (3.0 KB) — the per-frame helpers: `EnterSubmenu` (the v0.13.49 shared entry helper called from all three detection paths — submenu mode, dword detection, subCursor change), `GetBattleCharName` (savemap-driven char-name lookup, distinct from `GetSlotName` for the battle entity array), `BuildCharCommandList` (reads savemap `+0x50` equipped-command IDs into `s_turnCharCommands[]`).
- `src/battle_tts_menu_poll.inl` (55.4 KB) — `PollTurnAndCommands` (the per-frame menu state machine: turn-start/end detection, char→char GF HP substitution arming, command cursor navigation, submenu debounce, target-phase entry/exit via `BATTLE_MENU_PHASE`, submenu entry via `0x01D768EB` mode byte, submenu entry via `BATTLE_MENU_PHASE` dword function-pointer detection, subCursor announcement routing per submenu type, v0.10.112 delayed submenu entry, Draw-specific cursor + Stock/Cast polling, v0.12.72 deferred GF cancel, v0.12.66 all-target entry/cancel via `0x01D7689D`) plus `PollDeferredTurnAnnounce` (the v0.13.52-53 deferred turn TTS release path).

### Include chain

Dependency-ordered, included textually from the slim `battle_tts_menu.inl` parent (which is itself included from `battle_tts.cpp` inside `namespace BattleTTS`):

```
state → lists → helpers → poll
```

`state.inl` must come first (declares every static and every struct). `lists.inl` reads state, defines builders. `helpers.inl` reads state, calls list builders. `poll.inl` consumes everything above.

### Statics also referenced by `battle_tts.cpp`

The `OnBattleEnter` reset block in `battle_tts.cpp` writes to many statics that now live in `battle_tts_menu_state.inl`: `s_turnActiveCharId`, `s_turnCmdCursor`, `s_turnCharCommands[]`, `s_inSubmenu` (declared in `battle_tts_hp.inl` since v0.13.51, modified from menu code), `s_turnSubmenuCursor`, `s_submenuCommandId`, `s_magicListBuilt`, `s_turnMagicCount`, `s_gfListBuilt`, `s_turnGFCount`, `s_itemListBuilt`, `s_turnItemCount`, `s_drawListBuilt`, `s_turnDrawCount`, `s_drawTargetSlot`, `s_drawCursorPrev`, `s_drawStockCastPrev`, `s_lastDrawerPartySlot`, `s_drawLastMenuPhase`, `s_pendingSubmenuEntry`, `s_pendingSubmenuTick`, `s_submenuDebouncing`, `s_submenuDebounceTick`, `s_limitBreakActive` (declared elsewhere), `s_lastLimitToggle` (declared elsewhere). `CHAR_NAMES[]` is read by `GetCharNameById` in the shared-victory section. All of these work because the `.inl` chain is textually included inside `battle_tts.cpp` BEFORE `OnBattleEnter` — file-scope `static` visibility carries across the include boundary, identical to the v0.16.4 pattern.

### CI status

Largest sub-file is `_poll.inl` at 55.4 KB — under the 60 KB warn line. The other three sub-files (state 16.2, lists 9.6, helpers 3.0) are comfortably small. Slim parent is 1.05 KB. Total split footprint is 85.3 KB versus the original 81.9 KB; the ~3 KB overhead is per-file orientation comment headers explaining each module's purpose and dependency position.

`battle_tts.cpp` is unchanged — it still `#include`s `battle_tts_menu.inl` exactly as before; the content beneath that include simply expanded into the four sub-files. `deploy.bat` is unchanged (`.inl` files are textually included, only the parent `.cpp` compiles).

### Allowlist emptied

The `.github/workflows/safety-checks.yml` allowlist that protected the queued v0.16.x splits is now empty. The four entries (`field_dialog.cpp` for v0.16.2, `field_archive_jsm.inl` for v0.16.3, `battle_tts_ewm.inl` for v0.16.4, `battle_tts_menu.inl` for v0.16.5) were all stale — the corresponding files have all been carved into slim 1–3 KB shells. Removing them closes the refactor chapter that began with v0.16.0's `world_map.cpp` split.

## v0.16.4

Pure mechanical split of `src/battle_tts_ewm.inl` (91.79 KB monolith — over the 80 KB CI hard-fail line) into a slim 2.17 KB shell plus nine sub-`.inl` modules. No behavioral change; the EWM freeze, GF fire prevention, dispatch hooks, FFNx hook, and per-frame state machine are byte-for-byte identical to v0.16.3. Pattern matches the v0.16.0 (`world_map.cpp`), v0.16.1 (`chase_auto_pilot.cpp`), v0.16.2 (`field_dialog.cpp`), and v0.16.3 (`field_archive_jsm.inl`) splits.

EWM is the load-bearing core of the turn-based retrofit ("first-to-fill acts first, no skipped turns, natural ally/enemy ratio"), so this split deliberately did NOT touch the v0.13.57 ATB-restore semantics or any of the dispatch / cooldown / grace-period logic. Every static, comment, and `__try` block is preserved verbatim; only locations moved.

### New files

- `src/battle_tts_ewm_state.inl` (8.4 KB) — all module statics, typedefs, structs, constants. Hoisted to file head so every sub-`.inl` can reference them. Includes the `TargetDiagSnapshot` struct, all `s_ewm*` lifecycle flags, dispatch hook counters (`s_processReadyCalls/Blocks/Passes`, `s_actionExecuteCalls/Blocks/Passes`), diagnostic state (`s_diagPrevDamageAnim`, `s_prevSlotATB[]`, `s_slotTurnCount[]`), and the two function-pointer typedefs (`ProcessReadyFn`, `ActionExecuteFn`, `FFNxBattleUpdateFn`, `BattleEffectFn`).
- `src/battle_tts_ewm_gf_patch.inl` (8.9 KB) — GF fire prevention layer: `HookedGFTimerUpdate`, `EWM_ClampGFState` (the three-layer prevention: 0x004B04B4 MOV→RET code patch + state68 clamp + timer function skip), `EWM_RestoreGFPatch`, `GF_LogHookStats`, `GF_PollStateChanges`, `EWM_InstallGFHook`.
- `src/battle_tts_ewm_gf_effect.inl` (6.9 KB) — battle_magic_id polling (v0.12.48-49) for GF animation fire detection and Scan effect handling: `IsGFEffectId`, `GFEffectIdToIndex`, `FindPartySlotForGF`, `PollBattleMagicId` (handles GF anim fire AND Scan effect ID 39 with target bitmask resolution), `EWM_InstallBattleEffectHook`.
- `src/battle_tts_ewm_bp_diag.inl` (17.1 KB) — hardware breakpoint diagnostic (DR0 VEH + ToolHelp32 thread enumeration), target-selection diff diagnostic, and function entry scanner: `GF_BP_VectoredHandler`, `GF_BP_ArmAllThreads`, `GF_BP_AutoArm` (display-timer ≤3 arm window), `TgtDiag_TakeSnapshot`, `GF_BP_PollKey` (no-op since v0.11.01), `GF_ScanForFunctionEntry`.
- `src/battle_tts_ewm_atb_hook.inl` (12.3 KB) — `HookedATBUpdate` (the core ATB freeze sandwich with v0.13.57 exact-value restore semantics) plus EWM lifecycle/toggle: `EWM_LoadConfig`, `EWM_SaveConfig`, `EWM_PollToggle` (O-key toggle), `EWM_InstallHook`. The lifecycle functions live with the ATB hook because EWM_InstallHook is the installer for HookedATBUpdate and the toggle gates its behavior.
- `src/battle_tts_ewm_dispatch.inl` (5.7 KB) — v0.13.55/56 dispatch-layer hooks (sub_483470 + sub_482F80): `HookedProcessReady`, `HookedActionExecute`, `EWM_InstallProcessReadyHook`, `EWM_InstallActionExecuteHook`, `EWM_LogDispatchStats`.
- `src/battle_tts_ewm_ffnx.inl` (9.7 KB) — v0.10.77 FFNx GF loading counter hook: `HookedFFNxBattleUpdate` (the GF loading counter cap-at-max-1 sandwich), `FindFFNxModuleBase` (via the E9 JMP at set_midi_volume 0x0046BB40), `ScanModuleForSignature` (sig: `B9 16 F0 CF 01 66 89 06`), `ScanAllModulesForSignature`, `FindFunctionEntry` (backward CC/90/C3 padding scan), `EWM_InstallFFNxGFHook`.
- `src/battle_tts_ewm_diag.inl` (12.0 KB) — diagnostic helpers: `EWM_IsExecutingPhase` (phases 14/21/23/33/34), `EWM_FormatATBSnapshot`, `EWM_PollDiagnostics` (v0.13.57 transition logger for [0x01D280C0]/[0x01D27B00]/s_ewmShouldCap + post-release trace window), `EWM_ResetTurnCount`, `EWM_LogTurnCountSummary`, `EWM_TrackTurnCount` (v0.13.58-60 per-slot ATB high→low turn counter), `EWM_DiagLogATB`.
- `src/battle_tts_ewm_update.inl` (13.8 KB) — `EWM_UpdateBattle`, the per-frame freeze state machine. Calls helpers from `gf_patch.inl` (`EWM_ClampGFState`, `EWM_RestoreGFPatch`) and `diag.inl` (`EWM_PollDiagnostics`, `EWM_IsExecutingPhase`, `EWM_DiagLogATB`), so must come last in the include chain.

### Include chain

Dependency-ordered, included textually from the slim parent (which is itself included from `battle_tts.cpp` inside `namespace BattleTTS`):

```
state → gf_patch → gf_effect → bp_diag → atb_hook → dispatch → ffnx → diag → update
```

`state.inl` must come first (declares every static). `update.inl` must come last (calls helpers from `gf_patch` and `diag`). Everything between is independent and could be reordered; the chosen order groups related concerns (GF prevention → diagnostics → core hook → dispatch → FFNx → diag helpers → state machine).

### Statics also referenced by `battle_tts.cpp`

A few statics declared in `state.inl` are also referenced by `battle_tts.cpp` itself (e.g. `OnBattleEnter` resets `s_gfSnapValid`, `s_gfSnapLastTick`, `s_gfAutoArmLastActive`, `s_gfAutoArmDone`, `s_tgtDiagStage`; `Initialize` and `Shutdown` reference `s_gfVEHHandle` for the AddVectoredExceptionHandler / RemoveVectoredExceptionHandler pair). These work because the `.inl` chain is textually included inside `battle_tts.cpp` BEFORE the functions that use them — file-scope `static` visibility carries across the include boundary.

### CI status

Largest sub-file is `bp_diag.inl` at 17.1 KB — comfortably under the 60 KB warn line. Smallest is the slim parent at 2.17 KB. Total split is 96.83 KB versus the original 91.79 KB; the ~5 KB overhead is per-file orientation comment headers explaining each module's purpose.

`battle_tts.cpp` is unchanged — it still `#include`s `battle_tts_ewm.inl` exactly as before; the `.inl` content beneath that include simply expanded into the nine sub-files. `deploy.bat` is unchanged (`.inl` files are textually included, only the parent `.cpp` compiles).

### Remaining size-split work

After v0.16.4 ships, the size-split sequence is one file from done: v0.16.5 splits `src/battle_tts_menu.inl` (82 KB). With that, every source file in the project is under the 80 KB CI hard fail, and the allowlist in `.github/workflows/safety-checks.yml` can be emptied.

## v0.16.3

Split `src/field_archive_jsm.inl` (91 KB monolith, over the 80 KB CI hard-fail line) into a slim 2 KB shell plus seven sub-`.inl` modules. The JSM scanner pipeline was the last source file over the size limit; with this split, the field-archive subsystem stays under the 80 KB cap.

### Strategy

This was a small-refactor split rather than a pure mechanical one. Two changes:

1. **State hoist.** The cross-pass `static` arrays inside `ScanJSMScripts()` (`s_methodMapjumps`, `s_entityReqs`, `s_entityPopms`, `s_initVarMaps`, `s_reqOpcodeCount`, `s_hasSetmodelInit`, `s_hasDialogAny`, `s_hasExtDispatchArr`) and their containing struct definitions (`MethodMapjump`, `ReqCallInfo`, `EntityReqs`, `EntityPopms`, `VarWrite`, `EntityVarMap`) were promoted from function-local to namespace scope so the Director post-pass can share them. Function-local `static` already has program lifetime; the move is visibility-only. The explicit `memset` block at scan entry remains and preserves the zero-on-entry contract identically.
2. **Director helper extraction.** The DIAGNOSTIC + Director-detection-post-pass blocks (originally a v0.12.20 addition with its own bounded scope) were extracted verbatim into a new `RunDirectorDetection()` helper. `ScanJSMScripts()` now calls it as a single line after the draw-point trigger cross-reference completes.

Behavior is byte-for-byte identical to v0.16.2.

### New files

- `src/field_archive_jsm_state.inl` (4.4 KB) — hoisted struct decls, size constants (`MAX_METHOD_MAPJUMPS`, `MAX_PSHM_PER_METHOD`, etc.), the eight cross-pass `static` arrays, and the `RunDirectorDetection` forward declaration.
- `src/field_archive_jsm_constants.inl` (6.5 KB) — `JSM_OP_*` opcode ID constants and `JSMEntityTypeName()` lookup.
- `src/field_archive_jsm_helpers.inl` (2.1 KB) — `GetFieldIdByInternalName`, `SwapBE32`, `DecodeJSMInstruction`.
- `src/field_archive_jsm_opnames.inl` (2.6 KB) — `GetOpcodeName()` lookup for the script-dump diagnostic.
- `src/field_archive_jsm_director.inl` (10.4 KB) — `RunDirectorDetection()` post-pass: the `[DIR-DIAG]` log emitter and the Director identification + dispatch-target promotion logic, including the party-character SYM filter.
- `src/field_archive_jsm_scan.inl` (63.3 KB) — `ScanJSMScripts()` main body with the Director block replaced by a single helper call and the consolidated memset block referencing the namespace-scope arrays.
- `src/field_archive_jsm_dump.inl` (7.1 KB) — `DumpEntityScript()` diagnostic.

### Include chain

Dependency-ordered, included textually from the slim parent inside `namespace FieldArchive` (which is itself included from `field_archive.cpp`):

```
state → constants → helpers → opnames → director → scan → dump
```

`state.inl` must come first because it declares the cross-pass arrays and the helper forward decl; everything else depends on those. `director.inl` precedes `scan.inl` so the helper body acts as its own declaration when `scan.inl` calls it.

### CI status

`scan.inl` lands at 63.3 KB — just over the 60 KB warn line but well under the 80 KB hard fail. The 91 KB parent monolith is gone; the largest single piece of the JSM scanner is now ~70% of the CI hard limit. The dense per-entity opcode-scan loop is what keeps `scan.inl` chunky; further splitting would require breaking the loop into sub-helpers, which crosses the line from mechanical extraction into behavior-touching refactor. Holding off until there's a functional reason to revisit.

`field_archive.cpp` is unchanged — it still `#include`s `field_archive_jsm.inl` exactly as before; the `.inl` content beneath that include simply expanded into the seven sub-files. Public-API surface (`JSMEntityTypeName`, `ScanJSMScripts`, `DumpEntityScript` declared in `field_archive.h`) is identical.

### Why

v0.15-era debugging on the X-ATM092 chase repeatedly touched both the JSM scanner (for Background-entity classification fixes) and the Director detection logic (for dormitory-field interactive-object promotion). With both living in a 91 KB monolith, surgical edits to either side required scrolling through the other. The split lets future Director-detection work happen in a 10 KB file and leaves `scan.inl` focused on the per-entity opcode pass.

## v0.16.2

Pure mechanical split of `src/field_dialog.cpp` (88 KB monolith → 3 KB slim parent + 8 `.inl` files). No functional change. Pattern matches the v0.16.0 (`world_map.cpp`) and v0.16.1 (`chase_auto_pilot.cpp`) splits.

### New files

- `src/field_dialog_state.inl` — typedefs, all module-static state, struct definitions (`WindowState`, `PendingText`), window-object layout constants, FMV-poll state, show_dialog dedup state, and the `MarkPendingAsSpoken` forward declaration.
- `src/field_dialog_helpers.inl` — pointer validation (`IsValidTextPointer`, `ProbePointer`, `ProbeGetstrResult`), window accessors (`GetWindowObj`, `GetWinText1/2`, `GetWinOpenCloseTransition`), text helpers (`TrimDecoded`, `IsSuffixOrSubstring`, `fnv1a_prefix`), `CreateDetourHook`.
- `src/field_dialog_scan.inl` — the central TTS-speak path: `ScanAndSpeakAllWindows`, `ScanAndSpeakChoiceWindows`, `MarkPendingAsSpoken`, `CheckPendingTexts`.
- `src/field_dialog_show_dialog.inl` — `Hook_show_dialog` with OOR diagnostic, FNV-1a hash dedup, scan-active suppression, chase overlay forward, battle drawer-name decoration.
- `src/field_dialog_opcodes.inl` — opcode hooks (mes/mesw/ask/ames/aask/amesw), diagnostic opcode hooks (tuto/mesmode/ramesw), `Hook_field_get_dialog_string` with the DialogInject override path, and `RepeatLastDialog`.
- `src/field_dialog_diag.inl` — dispatch instrumentation (`DispatchStub`, `DispatchStub_EDX`, `PatchDispatchSite`, `UnpatchDispatchSite`), naked counter hooks, `Hook_get_character_width` + `CheckGcwBuffer`, `DiagRawWindowDump`.
- `src/field_dialog_menuname.inl` — `Hook_opcode_menuname` with GF-diff-on-acquire detection and naming-screen UI suppression.
- `src/field_dialog_lifecycle.inl` — `Initialize`, `Shutdown`, `PollWindows` (FMV-aware polling fallback).

### Include chain

Dependency-ordered, included textually from the slim parent inside `namespace FieldDialog`:

```
state → helpers → scan → show_dialog → opcodes → diag → menuname → lifecycle
```

The parent retains the tiny public-API tail (`IsActive`, `IsDialogOpen`, `GetMenuDrawTextCallCount`, `GetGetCharWidthCallCount`, `SnapshotGcwBuffer`) for visibility — everything else is in the `.inl` chain. Build script (`src/deploy.bat`) unchanged: `.inl` files are textually included, only the parent `.cpp` compiles.

### CI guard

60 KB warn / 80 KB hard-fail thresholds (`.github/workflows/safety-checks.yml`) respected. Largest new file is `field_dialog_lifecycle.inl` at ~12 KB; all others under 25 KB. The 88 KB monolith no longer trips the limit.

### Why

Readability + future-proofing. The v0.16.x refactor sequence is carving every source file over 60 KB into focused `.inl` modules so single-area edits stop touching half the dialog system. `field_dialog.cpp` was the second-largest remaining offender after the v0.16.1 chase split.

## v0.16.1.4

Corrects the doopen2a auto-pilot route based on Aaron's manual chase BAT (2026-05-16 21:10:38-21:10:47), which successfully cleared Town Square 5 in 9 seconds total with 0 catches. The `ff8_nav_data.log` COORD trace captured every triangle change of the manual run and is the source of truth for the new threshold.

### Manual run trace

```
t=0       (-974, -166)   spawn          tri 52
t=0+      (-856, -450)                  tri 51
t=1       (-783, -669)                  tri 49
t=1.5     (-629, -891)   MAX EAST       tri 46
t=2       (-662, -1351)                 tri 97
t=2       (-725, -1559)                 tri 96
t=2       (-750, -1805)                 tri 94
t=2       (-753, -1836)                 tri 89
t=2       (-777, -2082)                 tri 88
t=2       (-780, -2113)                 tri 85
t=3       (-807, -2391)                 tri 37
t=3       (-825, -2576)                 tri 33
t=3       (-871, -3038)                 tri 14
t=3       (-874, -3069)                 tri 79
t=4       (-940, -3293)                 tri 148
t=4       (-964, -3313)                 tri 23
t=4       (-1068, -3542) EXIT TRIGGER   tri 151
```

Shape: SE briefly (~1.5s, max east X=-629), then SOUTH along the western corridor with natural west drift. Exit triggered in the SW corner around `(-1068, -3542)`. Aaron's TALKRAD log also showed battleyarou's catch radius expanded from 500 to 700 at the 7-second mark (`[TALKRAD] CHANGED @0x1F8: 500 -> 700` at 21:10:45, context `@21E 0->2 @244 0->3`) -- the chase mechanic is "outrun an expanding catch radius", not "avoid a fixed circle."

### Critical correction: kani has no active proximity catch on doopen2a

Aaron's path passes within **162 units** of kani at `(-685, -2284)` (at position `(-807, -2391)`) and is not caught. The pre-v0.16.1.4 commentary that attributed v0.16.1.2's t=3 catch to kani was wrong. That catch's caller in the `[CBF] PASS` log was always `entityPtr=0x0188CA04` (battleyarou). Battleyarou fired BATTLE in v0.16.1.2 from 1447 units away -- probably velocity- or motion-vector-based rather than pure proximity. The exact mechanism remains unidentified, but the empirical fact is that Aaron's east-first / west-corridor route avoids it while a direct west-wall A* path triggers it.

Battleyarou's *static* TALKRAD=500 around its JSM init position `(0, -744)` is a real proximity catch, confirmed by v0.16.1.3 BAT (auto-pilot at `(-446, -821)`, 453 units from `(0, -744)`, caught at t=1). Aaron's max-east excursion to `(-629, -891)` was 646 units from `(0, -744)` -- 146-unit margin.

### Fix

`src/chase_auto_pilot_route.inl`: SE-stage threshold in `kStages_doopen2a[]` tightened from `Y < -1500` to `Y < -631`. The new threshold ends stage 0 at approximately `(-629, -631)` -- same X as Aaron's max-east excursion, 639 units from battleyarou's catch center (139-unit margin). Stage 1 (pure south) then drifts west naturally at -76/sec from the camDown vector `(-0.097, -0.995)`, walking the party along the western corridor to the SW screen-boundary trigger at approximately `(-905, -3447)` after ~3.6 more seconds. Total field time: ~4.25 seconds, well under the 7-second TALKRAD expansion.

### Walk vs run

Unchanged: `walk=false` on both stages. Aaron's manual run was at running pace; the slower observed rate compared to the auto-pilot's top speed is from walkmesh constraints and analog thumb angle, not a forced walk modifier.

### Expected v0.16.1.4 BAT signature

- Auto-pilot ENGAGED on doopen2a, MODE_STAGED_DIRECTION, stage 0/2 SE.
- `tick=60` log line approximately at `pos=(-629, -631)` or thereabouts, with `bydist >= 639`.
- Stage transition to S after Y crosses -631.
- Field transition to dotown_3 at approximately t=4-5 seconds.
- No `[CBF] PASS` BATTLE call on doopen2a.

### v0.16.1.3 reverted in spirit

The MODE_STAGED_DIRECTION mode is kept; only the SE-stage threshold changes. v0.16.1.3's threshold of `Y < -1500` was based on the false kani-proximity model and is replaced with the empirically-derived `Y < -631`.

## v0.16.1.3

Functional fix for the X-ATM092 chase catch on doopen2a (Town Square 5). The v0.16.1.1 diagnostic and v0.16.1.2 funnel-collapse BATs both reached BATTLE at ~3 seconds on doopen2a regardless of upstream timing, which finally clicked into place after Aaron's 2026-05-16 confirmation that the catch IS proximity-based ("that is how the chase scene works -- when the robot catches you then you end up in a fight") and that the field is not difficult for a sighted player following his recipe: "first have to go southeast (down and right) several steps, then due south to the exit gateway."

### Root cause

The v0.15.9.8 doopen2a config used `MODE_TARGET (-952, -3800)`, aiming at a SETLINE south trigger center the v0.15.9.7.8 fallback-mis-selection comment described as the chase exit. The A* + funnel pipeline routed the party along the WEST wall of the field (per v0.16.1.2 BAT portal data: portal 0 `L=(-1022,-99) R=(-769,53)` through portal 19 `L=(-1171,-2693) R=(-1180,-2525)`, X range ~-769 to -1180). The party's actual path held X around -800 to -900 throughout the south leg.

Kani sits at `(-685, -2284)` mid-field with TALKRAD set to 500 on field load (per the `[TALKRAD] CHANGED @0x1F8: 128 -> 500` log line that fires on every doopen2a engage). The west-wall path crossed within 165 units of kani's X column when the party reached kani's Y band -- well inside the 500-unit catch radius. The chase script's proximity check fired BATTLE at ~3 seconds reliably across multiple BATs. The v0.16.1.1 diagnostic captured the closing pattern in the new `kdist` per-tick log: 1837 -> 1061 -> caught, with `bydist` (party-to-origin distance for the UNUSE'd battleyarou entity) increasing in lockstep as the party moved away from world origin -- confirming kani, not battleyarou, as the proximity source.

### Why the v0.16.1.2 funnel-COLLAPSE didn't help

The v0.16.1.2 BAT confirmed Fix B fired correctly on domt2_1 (`[funnel] COLLAPSE wall-parallel portal 23 ... -> wp=(-13,-1508) tri 26->27`) and the party reached the new collapsed waypoint cleanly. But the 5-second stuck on domt2_1 at `(8, -1602)` persisted unchanged. That stuck is the scripted X-ATM092 landing animation Aaron confirmed plays on domt2_1 ("the field with the robot-jump-down animation, right before the bridge"). It is immutable game cinematic, not a pathing bug, and doesn't affect doopen2a timing because the robot's position resets at each field boundary. v0.16.1.2 was a clean swing-and-miss against the actual problem; the funnel-collapse code stays in for other walkmesh cases (no regression risk shown), but it's not what fixes the chase.

### Fix

New `MODE_STAGED_DIRECTION` config for doopen2a in `src/chase_auto_pilot_route.inl`, modelled on the domt5_1 stage table. Two stages match Aaron's recipe:

- Stage 0: `dirX=+1, dirY=+1` (south-east), running, active while `Y > -1500`. Several steps of SE motion push the party east of `X = -185` (kani's TALKRAD radius east boundary) before reaching kani's Y line. At doopen2a's calibrated camera (`camRight=(0.927,-0.376)`, `camDown=(-0.097,-0.995)`), SE input produces approximately `+407` east and `-672` south per second of world motion. Clearing `dX = 789` east (from spawn `X=-974` to safe `X=-185`) takes ~1.94 seconds, by which point `dY = -1303` south -- the threshold is set to `Y < -1500` with a ~90-unit safety margin.
- Stage 1: `dirX=0, dirY=+1` (pure south), running, active while `Y <= -1500`. Pure south to cross the exit gateway at `Y=-3414` (Screen Boundary line, X range `[-497, 311]` per the v0.16.1.2 BAT `gateway crossing line (-497,-3414)->(311,-3414)` log). Party X is held at the value reached during stage 0 (~-185 or further east), keeping kdist >= 500 throughout the south leg.

The exit-gateway target also corrects a long-standing misidentification of the chase exit. The v0.15.9.8 comment treated the south SETLINE at `Y=-3703` (center `(-952, -3703)`, X range `-1091` to `-814`) as the chase exit. That line is in the west of the field and was the target the A* path was aimed at. The actual chase exit per the BAT-logged `gateway crossing line` is at `Y=-3414` with `X` range `[-497, 311]`, in the center-east of the field -- exactly where the SE -> S route ends up.

### Diagnostic logging from v0.16.1.1 remains in place

- `ReadBattleyarouPosition` helper + ` by=(X,Y) bydist=N` per-tick log suffix in `chase_auto_pilot_update.inl`.
- `_ReturnAddress()` capture on the `[CBF] PASS` line in `chase_battle_freeze.cpp`.

Future chase regressions will surface in these logs without re-shipping diagnostic code.

### Expected v0.16.1.3 BAT signature

- doopen2a transit: ~5 seconds (1.94s SE + ~3s S), 0 catches.
- Per-tick log shows party moving south-east during stage 0 with `pos.X` increasing from ~-974 toward 0 (or close), then southbound with `pos.X` held near -100 to -200.
- `kdist` minimum ~545 (party at X=-185, kani at X=-685, same Y -- never closer).
- No `[CBF] PASS` line on doopen2a; field transitions to dotown_3 via the gateway crossing line at Y=-3414.

If the chase still catches:
- Check the BAT log for the per-tick `pos.X` trajectory during stage 0. If party X stays under -400 (didn't move east enough), the camera-mapping math is off and the threshold needs lowering (more negative Y to allow more SE travel time).
- If party moves east correctly but kdist still drops below 500 in stage 1, kani's TALKRAD is wider than 500 or her tracked position differs from the BAT-logged value. Re-derive thresholds.
- If party reaches the exit but the chase doesn't transition out, the gateway crossing line is on a different Screen Boundary than expected. Dump the `squall` / `zell` SETLINE entries to see which one corresponds to the south gateway.

## v0.16.1.2

Functional fix for the deterministic doopen2a catch identified by the v0.16.1.1 diagnostic BAT. The catch was not proximity-based (battleyarou reads as static at `(0,0)` across all chase fields per the new `by=(X,Y) bydist=N` per-tick log) and not a non-script controller (no FFNx hook detour; the chase script just runs out of session-budget). Total chase time domt5_1->BATTLE = 51 seconds, of which 5 seconds are eaten by a stuck on domt2_1 at `(3, -1603)`. Removing that 5 seconds gives doopen2a enough headroom for the south trigger to fire before the chase script does.

### Root cause

The SSFA (Simple Stupid Funnel Algorithm) in `src/field_nav_pathfinding.inl::FunnelPath` includes a wall-parallel portal optimization added in v06.01: portals whose endpoints lie on the same vertical line (`absDX < WALL_PARALLEL_EPSILON && absDY > 10 * epsilon`) are skipped via `continue` before being added to the portal list. The justification, validated against all 894 game walkmeshes offline, is that these portals "run ALONG a wall, not across the walkable corridor."

That reasoning holds for the bg2f_1 case the heuristic was tuned against (a long open corridor whose left/right inner walls happen to be exposed as wall-parallel portals between adjacent corridor triangles). It fails for tight chase fields like domt2_1, where the wall-parallel portal IS the corridor: tri 26 -> tri 27 has exactly one shared edge, a vertical doorway at `x=-42` spanning `y=-1638` to `y=-1360`. Skipping the portal removed the only aim point inside the doorway. The player at `(3, -1603)` saw a steer vector pointing to wp 23 at `(-64, -1658)` -- mostly south, slightly west -- which the camera projection (`camRight=(0.860,-0.510)`, `camDown=(-0.619,-0.785)`) converted to analog dominated by south. Player walked south into the `x=-42` wall, slid east-west along it for 2 seconds (`moveDist=160` with zero net displacement), then froze entirely (`moveDist=0`) for another 2 seconds before velocity-stuck recovery advanced wp 23 -> 24 -> 25 -> 26 over a total of 5 seconds. Each waypoint skip took ~1 second because each new waypoint also lived on the far side of the same wall.

### Fix B (default behavior)

When a wall-parallel portal is detected, emit a single "forced waypoint" at the portal midpoint shrunk inward by `AGENT_RADIUS` (30 units) toward triB's center, rather than `continue`-ing past it. The SSFA treats `L == R` as a pass-through constraint, so the funnel produces a waypoint exactly at the doorway and the player aims through it. New log line: `[funnel] COLLAPSE wall-parallel portal N dX=... dY=... L=(...) R=(...) -> wp=(...) tri A->B`. Summary log on field load: `[funnel] N wall-parallel portals processed (SKIP if SKIP_WALL_PARALLEL_LEGACY else COLLAPSE; v0.16.1.2 default = COLLAPSE)`.

### Fix A (fallback toggle)

A `static const bool SKIP_WALL_PARALLEL_LEGACY = false` inside the wall-parallel branch restores the v0.16.1.1 `continue` behavior globally when flipped to `true`. Intended as a one-line + rebuild mitigation if Fix B turns out to regress on bg2f_1 or other long-corridor fields where the original SKIP was correct. If a per-field toggle becomes necessary, we lift the constant to a route-config field instead. The toggle's legacy path emits `[funnel] SKIP wall-parallel portal N (LEGACY)` so BAT logs distinguish the modes.

### Diagnostic logging retained from v0.16.1.1

- `ReadBattleyarouPosition` and the ` by=(X,Y) bydist=N` suffix on ChaseAutoPilot per-tick logs stay in place. Useful for confirming battleyarou continues to read as `(0,0)` on chase fields (proximity catch falsified) and for diagnosing future chase regressions.
- `_ReturnAddress()` capture on the `[CBF] PASS` line stays. Useful for tracing future BATTLE invocations through the FFNx hook chain.

### Expected v0.16.1.2 BAT signature

**Chase clears cleanly**:
- `[funnel] COLLAPSE wall-parallel portal N` appears once during the domt2_1 chase-drive (between A* and the chase-drive STARTED log).
- domt2_1 transit time drops from ~14s to ~9s (the 5s stuck at `(3, -1603)` is gone).
- Total chase time domt5_1 -> doopen2a south trigger arrives well under 51s.
- No `[CBF] PASS` line on doopen2a; the chase ends with a field transition to dotown_3 (or whatever follows).

If chase still catches:
- Check the BAT log for `[funnel] COLLAPSE` lines to confirm Fix B fired.
- If COLLAPSE fired but domt2_1 transit is still 14s, the wall-parallel portal wasn't the bottleneck and we need to revisit (memory scan for chase timer or other approaches).
- If COLLAPSE did NOT fire (the wall-parallel detection missed the portal), the threshold values need tightening.

If bg2f_1 or other fields regress:
- Flip `SKIP_WALL_PARALLEL_LEGACY` to `true` in `field_nav_pathfinding.inl::FunnelPath` and rebuild. This restores the v0.16.1.1 behavior pending a per-field toggle.

## v0.16.1.1

Diagnostic build investigating the reproducible X-ATM092 catch on doopen2a (Town Square 5) discovered in the v0.16.1 BAT. The catch fires ~4 seconds after entering doopen2a regardless of party progress -- party position at BATTLE time (-853, -1266) is still ~2500 units short of the target trigger at (-952, -3800). Two consecutive BATs (same save state) both caught the party in the square; the regression is deterministic, not marginal.

Three small additions, all pure diagnostic logging -- no behavior changes:

### (1) `ReadBattleyarouPosition` in `src/chase_auto_pilot_io.inl`

New SEH-guarded helper mirroring `ReadKaniPosition` but targeting `ChaseDetector::GetBattleyarouEntityPtr()`. battleyarou (Others slot 6 on doopen2a) is the BATTLE caller per the v0.16.1 `[CBF]` log line (`entityPtr=0x0188CA04 caller=other`). Its method[4] is a 51-instruction movement loop (SET3 at dword 990 with PSHM_W params 7, -744, 0 -- the spawn position -- and a chain of waypoint constants 442/765/500/724/1494/756) and its TALKRAD jumps from 128 to 500 at field load. The question this read answers: is battleyarou actively closing on the party (proximity catch within TALKRAD=500), or is its position roughly static while a session-timer fires BATTLE regardless of geometry?

### (2) Per-tick log adds ` by=(X,Y) bydist=N` in `src/chase_auto_pilot_update.inl`

All four ChaseAutoPilot tick log paths (DIRECTION-with-pos, DIRECTION-without-pos, TARGET-with-pos, TARGET-without-pos) gain the battleyarou suffix alongside the existing kani suffix. Format mirrors the kani fragment exactly: ` by=(X,Y) bydist=N` when both reads succeed, ` by=(X,Y) bydist=?` when only battleyarou resolves, ` by=UNRESOLVED` when battleyarou's slot pointer is null on this field. Slots into the v0.15.9.11.3.7 delta-zero suppression cleanly because the suffix is appended after `kaniBuf` in the same `Log::Field` call -- no new log gate.

### (3) `_ReturnAddress()` capture in `src/chase_battle_freeze.cpp`

`Hook_opcode_battle` reads MSVC's `_ReturnAddress()` intrinsic on its very first line (before any other code so no inlining shuffles the captured value) and appends ` retAddr=0x%08X` to the existing `[CBF] PASS` log line. The captured address is the engine instruction immediately after the call site that invoked opcode_battle (0x69). With this we can map the BATTLE invocation back to the engine function that fired it -- battleyarou's script body, an EXT_DISPATCH handler, or some other dispatch path the v0.16.1 BAT's opcode histogram (which topped out at 0x35 with no 0x66 BATTLE opcode visible) didn't surface. New include: `<intrin.h>`.

### Expected v0.16.1.1 BAT signatures

**Proximity hypothesis confirmed:** on doopen2a, `bydist` starts ~1165 (battleyarou spawn at (0,-744), party at (-974,-105)) and decreases each tick as battleyarou's script moves it south, dropping below 500 right before the `[CBF] PASS` line fires. The `retAddr` lands inside battleyarou's script execution -- whatever engine function actually runs the JSM bytecode.

**Timer hypothesis confirmed:** `bydist` stays large (>500) throughout the engagement; `[CBF] PASS` fires with `bydist=800+`. The `retAddr` lands in a non-script engine function (e.g. a scene-state controller) that fires BATTLE on its own schedule.

Either result narrows the next fix substantially: proximity wants a faster auto-pilot path through doopen2a (MODE_DIRECTION south for instant engagement, no CALIB delay) and possibly a battleyarou-position-aware steering bias; timer wants us to either save time on earlier fields (bridge transit was 14s in the v0.16.1 BAT -- on the high end) or to intercept the timer-arming opcode on chase entry.

## v0.16.1

Pure-refactor split of `src/chase_auto_pilot.cpp` (108 KB, 1402 lines — second-largest non-history source file after `ff8_accessibility_history.h`). No behavioral changes. Removes `chase_auto_pilot.cpp` from the CI source-file-size-check allowlist.

The v0.15.9.x narrative comment header (every chase auto-pilot iteration from v0.15.9 through v0.15.9.11.3.7, walking through MODE_DIRECTION, MODE_TARGET, MODE_STAGED_DIRECTION, MODE_BRIDGE_DANCE, per-field configs, BAT findings, and the v0.15.9.11.3 synthetic-keyboard hookup) is pulled into a new `chase_auto_pilot_history.h` with an `#if 0` wrapper, mirroring the `world_map_history.h` archive pattern.

File layout after the split:

- `chase_auto_pilot.cpp` (slim parent) — system includes, namespace forward decls, namespace block, `.inl` chain in dependency order, public API: `Initialize`, `Shutdown`, `IsEngaged`. The big `Update` function lives in `chase_auto_pilot_update.inl` and is wired in via the textual include.
- `chase_auto_pilot_history.h` — pulled-out v0.15.9.x narrative, NOT in build path.
- `chase_auto_pilot_state.inl` — enums (`FieldDriveMode`, `BridgeDanceState`), structs (`FieldStage`, `FieldConfig`), all `s_*` module-static state, bridge-dance `kBridge*` thresholds, `ENTITY_STRIDE_OTHERS`. First in include chain.
- `chase_auto_pilot_route.inl` — `kStages_domt5_1[]` and `kFieldConfigs[]` with their rationale comments.
- `chase_auto_pilot_io.inl` — `ReadSquallPosition`, `ReadKaniPosition` (both SEH-guarded), `DistSquared`, `IntSqrt`.
- `chase_auto_pilot_helpers.inl` — `IsDirectionLikeMode`, `PickStageIdx`, `LookupConfig`, `BuildFallbackConfig`, `DirectionName`.
- `chase_auto_pilot_diag.inl` — `LogChaseActiveDiagnostic` (currently retired/early-returns; preserved for future camera-orientation research).
- `chase_auto_pilot_bridge.inl` — `UpdateBridgeDance` state machine (domt1_1 EAST/WEST kani-leap dance).
- `chase_auto_pilot_engage.inl` — `Engage`, `Disengage`.
- `chase_auto_pilot_update.inl` — the big per-tick `Update` function with the per-second diagnostic and delta-zero suppression.

Include order in the slim parent: state → route → io → helpers → diag → bridge → engage → update. State first per the v0.16.0 rule; each later file's functions reference only definitions from earlier files.

Largest .inl after the split is `chase_auto_pilot_update.inl` at roughly 22 KB — well clear of the 60 KB soft warning and 80 KB hard fail thresholds enforced by `.github/workflows/safety-checks.yml`. The allowlist entry for `src/chase_auto_pilot.cpp` is removed in the same diff.

BAT plan: load a Dollet save just before/during the X-ATM092 chase. Verify the auto-pilot still engages at the chase-ASK answer, drives the party across `domt4_1 / domt3_2 / domt5_1 / domt1_1 / doopen2a / dotown_2 / dotown_1` per their respective configs, and disengages cleanly at field exits. No log lines or behavior should differ from v0.16.0.3.

## v0.16.0.3

Log-spam cleanup follow-up from v0.16.0.2 BAT. The `[VEH-POS-FALLBACK]` diagnostic added in v0.16.0.1 (when `GetWorldMapPosition_Active` declines to overwrite foot DWORDs with a (0,0) vehicle read) was firing on every world-map poll while `s_lastVehicle` stayed latched to a non-foot value. In Aaron's v0.16.0.2 BAT, `s_lastVehicle=33 (VEH_CAR)` latched mid-session and the fallback log line fired roughly 1800 times in a 7-minute session, dominating `Logs/ff8_world.log` and making post-BAT analysis painful.

**Fix.** `world_map_segments.inl` — the fallback log branch now uses a function-local `s_fbLastLoggedVehicle` static and logs only when the current `s_lastVehicle` differs from the last-logged value. The functional guard (only overwrite foot DWORDs when `vx != 0 || vy != 0`) is unchanged.

Rationale for transition-only (no time-based heartbeat): the guard is silent and self-correcting; the diagnostic exists only as a forensic trail of which vehicle byte values reached the fallback. Once a given vehicle value has been logged once, additional heartbeats add noise without adding forensic value. A future bug that depends on the fallback firing repeatedly without a vehicle change would need its own targeted diagnostic.

No functional change to AD behavior; only diagnostic log frequency reduced.

BAT plan: any session that exercises the world map. Expect at most one `[VEH-POS-FALLBACK]` line per distinct `s_lastVehicle` value that triggers the fallback (typically 0–3 total per session), instead of the per-poll flood.

## v0.16.0.2

Three-part fix from the v0.16.0.1 BAT, which revealed that Fire Cavern is a two-stage entry: the world-map trigger drops the player into the "Fire Cavern A" approach field (a path field leading to the cavern interior), not directly into the cavern. The trigger geometry for this approach field sits ~6.5k units southwest of the icon at (36864,-28672), well outside the 2500-unit Part B cap. Aaron correctly observed in the BAT that landing in Fire Cavern A is a success; the mod was wrongly treating it as off-target.

### Fix 1 — Poll() replan-path now honors planner-eligibility

**Symptom.** Fire Cavern drive at 14:37:46 in the v0.16.0.1 BAT log started correctly with `planned=0` (simple-coord steering, per Part C). A random encounter at 14:37:51 paused it. On world-map re-entry at 14:39:00, the log shows `[DRIVE] Resumed after world-map re-entry`, and immediately after, the next `Awaiting arrival decision` line reports `planned=1`. The Poll()'s replan path had called `PlanDrivePath(rx, ry)` unconditionally, the closest-active-region fallback fired (Fire Cavern's segment (20,20) region=0x0C has no foot clause, so the fallback walked active regions and picked seg(18,20) which belongs to Balamb Town's region 0x07), and the simple-coord drive was converted into a misrouted planner drive.

**Root cause.** Part C correctly gates `StartAutoDrive` on `s_destPlannerEligible[locIdx]`, but `Poll()`'s mid-drive replan code at re-entry was added in v0.14.88 (well before Part C existed) and calls `PlanDrivePath` without the same gate.

**Fix.** `world_map_state.inl` adds `static bool s_drivePlannerEligible = true;`. `world_map_drive.inl`'s `StartAutoDrive` sets it from the same locIdx-based decision Part C uses, and `StopAutoDrive` resets it to `true`. `world_map.cpp`'s Poll() replan block now wraps `PlanDrivePath(rx, ry)` with `if (s_drivePlannerEligible) { ... } else { log + keep simple-coord }`. Planner-ineligible drives now stay simple-coord through encounter-resume cycles.

### Fix 2 — Part B two-tier distance cap

**Symptom.** Same BAT, 14:39:11: the misrouted-then-corrected drive exits the world map at lastPos=(30326,-29221), MODE_FIELD fires, Part B refuses arrival because `dist=6561 > 2500 max`. But that position is exactly where the Fire Cavern A approach-field trigger sits — the off-target stop was actually a successful arrival.

**Root cause.** Part B's 2500-unit cap assumes the destination's catalog point is trigger-aligned (true for refined coords captured from prior BAT, true for planner-eligible destinations whose icons sit at script-event positions). Geometric-trigger destinations (Fire Cavern, early-game Balamb Garden, likely Centra Ruins / Tomb / Cactuar Island / Shumi / Edea's House) have icons placed for visual centering, with terrain triggers thousands of units away. The 2500 cap is correct for them once a refined coord is captured but wrong on first arrival.

**Fix.** `world_map_arrival.inl` adds `DRIVE_ARRIVAL_MAX_DIST_GEOMETRIC = 8000.0`. The MODE_FIELD branch and the timeout-fallback distance branch both choose between the two caps via `s_drivePlannerEligible ? 2500 : 8000`. OFF-TARGET log lines now include the tier label (`planner-eligible` or `geometric-trigger`) for diagnostic clarity. The refined-coord capture in the success branch already exists; it now runs for geometric-trigger arrivals in the 2500–8000 zone, capturing the actual trigger position. Subsequent drives target the refined coord and dist drops to near zero, falling back inside the strict 2500 cap.

This is self-correcting and data-driven: every new geometric-trigger destination Aaron visits will be refined on first arrival, with no per-destination hardcode required. The wider cap is a safety net only for unrefined destinations, not a permanent relaxation.

### Fix 3 — Hardcoded Fire Cavern refined-coord baseline

In `world_map.cpp`'s `Initialize()`, the `s_refinedHas[i]` default-population loop now sets Fire Cavern's refined position to `(30326, -29221)` alongside the existing Balamb Town hardcode at `(12896, -26711)`. This eliminates the first-drive 4-second round-trip through the wider-cap arrival path for Fire Cavern specifically. On a fresh install or after savedata reset, the first Fire Cavern drive will compute dist near zero at arrival and use the strict cap immediately.

The loop's `break;` after Balamb Town was removed so both names are checked on a single pass; the else-if chain ensures only one match per location.

### BAT plan for v0.16.0.2

1. Build v0.16.0.2, restart FF8.
2. Stand on world map on foot. Select Fire Cavern, press `\`. Expect `[INIT] Refined entry default: Fire Cavern (30326,-29221)` already logged at module init.
3. Expected drive log: `[DRIVE] Geometric-trigger destination Fire Cavern (locIdx=37, planner-ineligible) -- using simple-coord steering`. **NO `[PLAN-DEBUG]` walk.**
4. After arrival in Fire Cavern A, expect `[DRIVE] Arrival via game-mode (mode=1 MODE_FIELD, fieldId=0x????, fieldName='?????', target=Fire Cavern, dist=<low>, ...)` — `dist` should be small because the refined coord is now the target.
5. If a random encounter interrupts mid-drive: on resume, expect `[DRIVE] Planner-ineligible destination -- keeping simple-coord steering, not replanning`. The previous bug would have shown `[PLAN-DEBUG]` here.
6. Select Balamb Town, press `\`. Expect normal `[PLAN-DEBUG]` walk and planner arrival (unchanged behavior).
7. Pull `Logs/ff8_world.log` + `Logs/ff8_mod.log` and the field's fieldName/fieldId from the arrival line so the DEVNOTES catalog of geometric-trigger destinations can grow.

## v0.16.0.1

Two follow-up fixes from the v0.16.0 BAT. Both surfaced in `Logs/ff8_world.log` from Aaron's first run; both have known repros and small surgical patches.

### Fix 1 — "Position unavailable" after exiting a field (the bug Aaron hit)

**Symptom.** After exiting a location back to the world map, pressing `\` to start auto-drive spoke "Position unavailable. Try again." After a random encounter the announcement disappeared and AD worked normally.

**Root cause.** In the BAT log at 14:09:55:
```
[WM-ENTRY-DEBOUNCE] Snapshot baseline locomotion=37 (was 0, suppressed 3000ms of byte noise)
```
The 3-second WM-ENTRY-DEBOUNCE committed `s_lastVehicle = 37` (mode 0x25, in the 32-40 `VEH_CAR` range) for a player who never owned a car. `GetWorldMapPosition_Active` saw `VEH_CAR`, dispatched to `WM_CAR_POS_ADDR`, read the savemap `car_pos` struct which holds `(0,0)` (vehicle never owned, never maintained), and **unconditionally overwrote the perfectly valid foot DWORD position** with `(0,0)`. `StartAutoDrive` then aborted via the `if (px == 0 && py == 0)` guard with "Position unavailable. Try again." The random encounter cycle eventually settled `s_lastVehicle` to mode 0 (foot), and AD started working.

**Fix.** `world_map_segments.inl` — inside the `__try` block in `GetWorldMapPosition_Active`, guard the vehicle-pos overwrite with `if (vx != 0 || vy != 0)`. `(0,0)` from a vehicle savemap struct is a sentinel meaning "vehicle not owned / not maintained," and the foot DWORDs (already populated by the initial `GetWorldMapPosition` call) are the more reliable fallback. The `else` branch logs `[VEH-POS-FALLBACK]` with the tag, `s_lastVehicle`, vehicle-type name, and the retained foot coords so any recurrence is visible in `ff8_world.log` without needing a fresh diagnostic build.

### Fix 2 — Part C indexed the wrong eligibility array (uncovered while diagnosing Fix 1)

The same BAT log showed the Fire Cavern drive at 14:07:15 walking all 38 planner programs and producing the closest-active-region fallback toward seg(18,20), exactly the case Part C was meant to short-circuit. Part B caught the off-target arrival at 14:07:23, but the planner walk shouldn't have fired at all.

**Root cause.** `world_map_drive.inl`'s Part C gate read `s_destPlannerEligible[catIdx]` where `catIdx` is into `s_catalog[]` (the BFS-filtered, distance-sorted, vehicle-aware catalog — 4 entries during the failing drive), but `s_destPlannerEligible[]` is indexed by `s_locations[]` (the 38-entry master table populated by `ComputePlannerEligibility`). For Fire Cavern at catIdx=2, the gate read `s_destPlannerEligible[2]` = **Dollet's** eligibility (master idx 2 = YES) and ran the planner anyway.

**Fix.** `world_map_drive.inl` — `StartAutoDrive` already calls `FindLocationIndexByTargetCoords(dest.x, dest.y)` to look up `locIdx` (master-table position) for the refined-coord check a few lines earlier. Reuse that variable: `s_destPlannerEligible[locIdx]` is the right index. The fallback log now reports `locIdx` for direct correlation with the `[INIT] Planner-eligibility:` lines.

### Verification path for the next BAT

1. **"Position unavailable" gone.** Exit any field on foot, immediately press `\` on the world map. Should announce the destination and start driving. `[VEH-POS-FALLBACK]` lines in `ff8_world.log` confirm the new guard catching the stale-vehicle case; their absence means the locomotion byte stayed clean this run.
2. **Fire Cavern uses simple-coord steering.** Stand on the world map on foot, select Fire Cavern, press `\`. Expect a new log line: `[DRIVE] Geometric-trigger destination Fire Cavern (locIdx=37, planner-ineligible) -- using simple-coord steering`. **No** `[PLAN-DEBUG]` walk follows. UpdateAutoDrive steers by bearing toward the catalog center until either arrival (capped by Part B at 2500 units) or sweep-abort.
3. **Balamb Town still uses the planner.** Select Balamb Town, press `\`. Expect the existing `[PLAN-DEBUG]` walk to run and produce a real path. Part B and the new locIdx gate together should keep planner-eligible destinations working exactly as in v0.16.0.

Fire Cavern refined-coord capture is on the BAT punch list for v0.16.0.1: stand on the world map on foot, drive into Fire Cavern via simple-coord steering, the on-arrival log line `[DRIVE] Captured refined entry for Fire Cavern at (X,Y)` is what we want.

## v0.16.0

Refactor + safety net for the world-map auto-drive system. The 222 KB / 4452-line `src/world_map.cpp` monolith has been split into 10 focused files, two new behavioral safety nets were added (Part B and Part C), and a CI guard was added to keep source-file size bounded going forward. No new features for the user beyond the AD safety improvements; the bulk of the diff is structural.

### What v0.15.13.2 BAT exposed (the bug behind Parts B / C)

A Fire Cavern auto-drive routed the player into Balamb Garden's gate field (`bggate_1`) instead. The v0.14.95 closest-active-region fallback in `MatchProgramForCatalog` was the culprit: Fire Cavern's catalog at (36864, -28672) maps to segment region 0x0C, and the only program that names 0x0C is program 20 with `top_vehicle=Garden`. On foot with no Garden owned, that clause filters out, the catalog's own region falls out of the active set, and the closest-active-region search picked an unrelated active region — routing the player toward Balamb Garden's gate. Worse, the v0.14.96 deferred-arrival path then captured the misrouted entry coord into `s_refinedX/Y[bggate_1]`, poisoning subsequent drives to Balamb Garden until a fresh session cleared the in-memory table.

Root diagnosis: some world-map destinations are **planner destinations** (entered via a wmsetus.obj Section 8 trigger zone, well represented by the A* planner) and some are **geometric-trigger destinations** (entered via a terrain-29 polygon trigger on the world map mesh, no wmsetus script event at all). Fire Cavern is the canonical geometric-trigger destination. The A* planner cannot represent these — there's no foot clause to match — so its closest-active-region fallback misroutes drives toward unrelated destinations. Pre-Sonnet builds solved this with v0.11.11-era simple-coord steering (catalog-center, bearing-based) which is bounded and predictable.

### Part B — off-target distance cap on arrival

`world_map_arrival.inl` adds `DRIVE_ARRIVAL_MAX_DIST = 2500.0` and applies it at two anchor points in `ResolveDeferredArrival`:

1. Top of the `MODE_FIELD` branch: when the game settles into a field but the player's last-known world-map position is more than 2500 units from `s_driveTarget`, refuse to capture a refined coord, refuse to declare arrival, log `[DRIVE] OFF-TARGET stop (dist=X.X > 2500.0 max ...)`, and stop AD with a spoken "Entered field but X units from target; not arrival." The Fire-Cavern-into-bggate_1 case fails this check on every retry — it would have stopped cleanly instead of poisoning the refined table.
2. Inside the timeout-fallback exit-distance branch: same check, defensive. `DRIVE_ARRIVED_ON_EXIT_DIST` (1500) is already below `DRIVE_ARRIVAL_MAX_DIST` (2500), so the check is structurally redundant today, but the explicit guard preserves the contract if a future build raises `DRIVE_ARRIVED_ON_EXIT_DIST`.

### Part C — planner-eligibility gate in `StartAutoDrive`

`world_map_drive.inl`'s `StartAutoDrive` no longer calls `PlanDrivePath` unconditionally. It now checks `s_destPlannerEligible[catIdx]` first:

- **Eligible destination**: call `PlanDrivePath(px, py)` exactly as before. A* runs, planner takes over.
- **Ineligible destination**: skip the planner entirely. Log `[DRIVE] Geometric-trigger destination (planner-ineligible), using simple-coord steering`. Clear `s_drivePathLen / Idx / Planned / GoalSegCount`. `UpdateAutoDrive`'s non-planner branch (catalog-center steering with bearing-based final approach) handles the rest.

### `ComputePlannerEligibility` — the helper that decides which is which

`world_map_planner.inl` gains `ComputePlannerEligibility()`, called once near the end of `Initialize` (after `LoadTriggerZones` so `s_segmentRegionMap` is populated, after the catalog is registered so `s_locations[]` is valid). It walks every catalog entry, maps `(x, y)` to a segment region byte, and scans `s_triggerPrograms[]` looking for at least one clause that names that region with a foot vehicle code (`TRIG_VEH_FOOT = 0x80` or `TRIG_VEH_FOOT_ALT = 0x84`). Result lands in `s_destPlannerEligible[LOCATION_COUNT]`. Logs one `[INIT] Planner-eligibility:` line per catalog entry plus a count summary. Defaults all flags to false if `s_segmentRegionLoaded` is false — safer than over-marking.

Predicted classifications (verify in v0.16.0 BAT init log):

- Balamb Town (region 0x07): **YES** (program 9 clause 1: foot, 0x07, story [0..3900)).
- Balamb Garden (region 0x0C): **NO** (only program 20 names 0x0C; top_vehicle=Garden).
- Fire Cavern (region 0x0C): **NO** (same as Balamb Garden — both at seg(20-ish, 19-ish), both region 0x0C, no foot clause).
- Most named-town destinations should be YES.
- Most chocobo forests should be YES.
- Alien Ship sites are likely NO (they're terrain triggers).

### File split — what moved where

| File | Size | Contains |
|---|---|---|
| `src/world_map_history.h` | 17.76 KB | Narrative archive of v0.14.31 through v0.15.13.2. Pulled out of the build. v0.14.102 narrative preserved verbatim; older blocks condensed with a `git show v0.15.13.2:src/world_map.cpp \| head -609` pointer. |
| `src/world_map_state.inl` | 29.78 KB | Enums (`VehicleType`, `SegTerrainClass`), structs (`LocationEntry`, `TriggerClause`, `TriggerProgram`), all `static` state arrays sized to `MAX_LOCATIONS = 64`, including the new `s_destPlannerEligible[MAX_LOCATIONS]`. Constants for the world torus, wmx.obj, wmsetus.obj, AD lifecycle. |
| `src/world_map_segments.inl` | 34.35 KB | Coord readers, pure math, vehicle classifier, archive I/O, `LoadTerrainGrid`, `DumpTriggerSection`, `LoadTriggerZones`. |
| `src/world_map_trigger_data.inl` | 17.72 KB | 38 decoded wmsetus.obj Section 8 trigger programs, `s_triggerPrograms[]`, `TRIGGER_PROGRAM_COUNT`, `LogTriggerPrograms`. |
| `src/world_map_catalog.inl` | 13.57 KB | `s_locations[]` data with `LOCATION_COUNT`, `static_assert(LOCATION_COUNT <= MAX_LOCATIONS)`, BFS reachability, distance-sorted catalog builder, vehicle-state tracker. |
| `src/world_map_announce.inl` | 2.81 KB | `AnnounceLocation`, `AnnounceBearing`. |
| `src/world_map_planner.inl` | ~30 KB | `IsLocationFootFriendly`, story/vehicle predicates, `MatchProgramForCatalog`, `CollectGoalSegments`, `IsGoalSegment`, `WrapManhattan`, `HeuristicToGoals`, `PlanPath`, `PlanDrivePath`, **new** `ComputePlannerEligibility`. |
| `src/world_map_arrival.inl` | ~11 KB | `ResolveDeferredArrival` with `DRIVE_ARRIVAL_MAX_DIST` and the two Part B distance checks. |
| `src/world_map_drive.inl` | ~28 KB | `PressKey`, `ReleaseKey`, `ReleaseAllDriveKeys`, `SetDriveKeys`, `StopAutoDrive`, `StartAutoDrive` (with Part C eligibility gate), `StartSweep`, `UpdateAutoDrive`. |
| `src/world_map_keys.inl` | 2.59 KB | `PollKeys` (catalog cycle, bearing, AD toggle). |
| `src/world_map.cpp` | ~10 KB | Slim parent. Headers, namespace forward decls, the 9-deep `.inl` include chain inside `namespace WorldMap { ... }`, plus `Initialize` (with the new `ComputePlannerEligibility()` call), `Update`, `Shutdown`, `Poll`. |

`.inl` includes are textual — no header guards inside, all `static` declarations preserved, `state.inl` is always included first so types/state are visible to every later file. The `LocationEntry` struct moved into `state.inl` so state arrays can reference it; `s_locations[]` data and `LOCATION_COUNT` stay in `catalog.inl`. `MAX_LOCATIONS = 64` in `state.inl` decouples state-array sizing from the catalog size; a `static_assert` in `catalog.inl` keeps them honest.

### CI guard — source-file-size check

`.github/workflows/safety-checks.yml` gains a `source-file-size-check` job that scans `src/*.cpp` and `src/*.inl` at push time. Soft warning at 60 KB, hard fail at 80 KB. The check is needed because the 222 KB world_map.cpp got that way precisely because there was no enforcement — every "just add one more changelog block" was locally cheap and globally ruinous.

Existing oversized files are temporarily allowlisted so this build can push without already requiring follow-up refactors: `chase_auto_pilot.cpp` (108 KB), `field_archive_jsm.inl` (91 KB), `battle_tts_ewm.inl` (90 KB), `field_dialog.cpp` (88 KB), `battle_tts_menu.inl` (82 KB). Each of these is queued for its own v0.16.x split — the world_map split is the template.

### BAT plan

1. Build, restart FF8 to clear in-memory poisoned Fire Cavern refined-coord.
2. Check `Logs/ff8_world.log` for `[INIT] Planner-eligibility:` block. Expect Fire Cavern=NO, Balamb Garden=NO, Balamb Town=YES, plus a count summary.
3. **Fire Cavern AD test**: select Fire Cavern, press `\`. Log should show `[DRIVE] Geometric-trigger destination (planner-ineligible), using simple-coord steering`. Character moves east toward Fire Cavern. Arrives. Capture refined coord from `[DRIVE] Captured refined entry for Fire Cavern at (X,Y)` for v0.16.0.1 hardcoding.
4. **Balamb Town AD regression**: planner-eligible path executes normally, drive arrives.
5. **Balamb Garden AD**: geometric-trigger steering instead of accidental terrain crossing.
6. **Off-target stop test (Part B safety net)**: if a drive ever enters the wrong field >2500 units from target, look for `[DRIVE] OFF-TARGET stop`. Should fire on the wrong-field scenarios that v0.15.13.2 silently passed.
7. World-map keyboard nav regression: `+`, `-`, Backspace, `\` all still work.

Upload `Logs/ff8_world.log` + `Logs/ff8_mod.log`.

### Files

- DELETED (effectively, by overwrite): old monolithic `src/world_map.cpp` (222 KB) — content migrated to the .inl files.
- NEW: `src/world_map_history.h`, `src/world_map_state.inl`, `src/world_map_segments.inl`, `src/world_map_trigger_data.inl`, `src/world_map_catalog.inl`, `src/world_map_announce.inl`, `src/world_map_planner.inl`, `src/world_map_arrival.inl`, `src/world_map_drive.inl`, `src/world_map_keys.inl`.
- REPLACED: `src/world_map.cpp` (slim parent, ~10 KB).
- MODIFIED: `src/ff8_accessibility.h` (version 0.16.0).
- MODIFIED: `.github/workflows/safety-checks.yml` (source-file-size CI guard, allowlist for already-oversized files pending later splits).
- MODIFIED: `CHANGELOG.md` (this entry).
- MODIFIED: `DEVNOTES.md`, `NEXT_SESSION_PROMPT.md`.

### Deferred

- v0.16.1: split `src/chase_auto_pilot.cpp` (108 KB) using the same `.inl` pattern.
- v0.16.2: split `src/field_dialog.cpp` (88 KB).
- v0.16.3: split `src/field_archive_jsm.inl` (91 KB).
- v0.16.4: split `src/battle_tts_ewm.inl` (90 KB).
- v0.16.5: split `src/battle_tts_menu.inl` (82 KB).
- v0.16.0.1 hardcoded refined-coord baseline for Fire Cavern and Balamb Garden, captured from the v0.16.0 BAT logs (Part D from the planning conversation — was always intended to follow the BAT result, not ship blind).

## v0.15.13.2

Live timer reads now point at the address the v0.15.13.1 scanner discovered. The scanner has served its purpose and is disabled in this build, freeing ~6 MB of static memory and the per-frame snapshot/analyze CPU cost.

### v0.15.13.1 BAT — scanner found the timer

Cycle 11 of the v0.15.13.1 BAT (21:50:40) surfaced a single, unmistakable candidate in the R1 u32 list:

```
[CountdownScan] R1 u32 #0 addr=0x01CFE92C u32 cur=1711 old=1715 dec=4 rate=1.00/s
```

Perfect 1.00/second monotonic decrement, value 1711 = 28 minutes 31 seconds remaining — squarely consistent with a Dollet chase save loaded mid-run (chase starts at 1800 sec; 89 seconds elapsed by cycle 11). The address `0x01CFE92C` is `0x8C` bytes BELOW the game-object struct base `0x01CFE9B8`, in an adjacent engine-globals allocation. That's why v0.15.13.0's old Region 1 (8 KB starting AT the game object) missed it — the v0.15.13.1 expansion to `0x01CD0000 + 192 KB` was what surfaced it.

The candidate only appeared in cycle 11 because the top-16 cap pushed it out of most other cycles where 16+ faster-changing candidates ranked higher (entity-state churn at rate ~25/s during gameplay dominated the rankings). Cycle 11 was unusually calm — only 1 R1 u32 entry made it through the value-range and rate filters — letting our slow timer (dec=4, rate=1.00/s) take that lone slot.

This is the kind of find that justifies wider scan regions and accepting more noise in the ranked output: a low-rate, single-instance, perfectly-monotonic candidate in an otherwise quiet region is exactly the signature of a real countdown timer.

### Changes in `src/countdown_timer.cpp`

- New constant `LIVE_TIMER_ADDR = 0x01CFE92C` (the discovered address). Read as uint16 — value fits comfortably in 16 bits since the max representable timer is 65535 seconds = ~18 hours, well above any chase duration, and reading uint16 means Shift+T freeze writes won't clobber any unknown high-byte engine state.
- Old `TIMER_VAR724_ADDR = 0x01CFEC8C` renamed to `VAR724_SNAPSHOT_ADDR`, kept as a documented constant but no longer read. The script-side snapshot stays at 0 during the chase because the chase script doesn't call GETTIMER routinely; only `LIVE_TIMER_ADDR` updates.
- `ReadVar724Raw` / `WriteVar724Raw` renamed to `ReadLiveTimerRaw` / `WriteLiveTimerRaw`. All call sites updated.
- Log tag updated from `var724 raw=N` to `live raw=N` to make the new source obvious in the log.
- Initial announcement reworded "Timer started" → "Timer detected" since the player may be loading mid-chase rather than at the SETTIMER moment.
- Comment block rewritten to capture the v0.15.13.0/.1/.2 history and the rationale for picking uint16 over uint32.

### Changes in `src/countdown_scan.inl`

Scanner gated behind `#define COUNTDOWN_SCAN_ENABLED 0` at the top of the file. When disabled:

- The large static buffers (`s_region1Buf`, `s_region2Buf` — ~6 MB total) are not declared.
- `Initialize` becomes a one-line log saying "DISABLED (v0.15.13.2). Set `COUNTDOWN_SCAN_ENABLED=1` to re-enable."
- `Update` is an empty no-op.
- Full scanner implementation preserved inside the `#if` block so a future session can flip the flag to re-hunt for a different engine global without rewriting from scratch.

This is a deliberate pattern: when a diagnostic feature has served its purpose, gate the heavy work behind a flag rather than deleting the code. The file keeps documenting how scanning was done, and the next time we need to find an engine global, the only change is the flag and (optionally) the region addresses.

### What the next BAT verifies

Aaron loads the Dollet comm-tower save. The mod log should now show:

- `[CountdownTimer] Initialize v0.15.13.2: reading live engine timer at 0x01CFE92C ...`
- `[CountdownScan] DISABLED (v0.15.13.2). ...` (and nothing else from the scanner).
- Shortly after fieldload: `[CountdownTimer] live raw=NNNN (prev=-1) state=0 tickMs=...` (the first observation).
- Then `[CountdownTimer] ENTER ACTIVE: rawValue=NNNN units=SECONDS initialSec=NNNN (NNmNNs) ...`
- TTS announcement: "Timer detected. NN minutes NN seconds remaining."
- As the chase progresses: `[CountdownTimer] BOUNDARY 1500 seconds reached ...` etc. at 25:00, 20:00, 15:00, 10:00, 5:00, 1:00, 0:30.
- Pressing T at any point: "NN minutes NN seconds remaining."
- Pressing Shift+T: "Timer frozen." Then on-screen timer stops advancing (or flickers between current and frozen value at HUD refresh rate). Pressing Shift+T again: "Timer resumed."

Static memory should drop by ~6 MB (verifiable indirectly via taskmgr if Aaron cares to check). No `[CountdownScan]` lines beyond the disabled announcement.

### Failure modes to watch for

- **No `[CountdownTimer] live raw=NNNN` after fieldload**: read may have faulted on 0x01CFE92C. SEH should catch this gracefully; log would be empty rather than crashing. Could mean the address isn't always mapped before fieldload finishes initializing the engine state. Mitigation: read attempts run every frame, so it'd start working once the page maps. If it never maps, the scanner finding was a false positive (unlikely given the exact 1.00/s signature).
- **`live raw=0` throughout**: the address holds zero. Could mean the timer hasn't started yet for this save, or 0x01CFE92C is actually a per-save-slot offset rather than a global. Aaron would confirm by watching the on-screen HUD.
- **Units misclassified**: if the live address holds a value outside our three ranges (5-60, 500-3000, 15000-60000), the classifier returns UNKNOWN and the state machine stays INACTIVE. The "Observed nonzero value N but units UNKNOWN" log line will tell us which range to add. (Aaron's BAT had value 1711 which is in SECONDS range — should be fine.)
- **Shift+T freeze doesn't visually freeze the timer**: the engine writes to 0x01CFE92C more aggressively than our mod thread can rewrite. If this happens, we have the read working but freeze remains unreliable; that's an acceptable trade-off — read-and-announce is the primary feature. Could be addressed in v0.15.14 by hooking the engine's write instead of polling.

### Files

- MODIFIED: `src/countdown_timer.cpp` (live timer address, renames, log tags, comments)
- MODIFIED: `src/countdown_scan.inl` (compile flag gating heavy work; full implementation preserved)
- MODIFIED: `src/ff8_accessibility.h` (version 0.15.13.2)
- MODIFIED: `CHANGELOG.md` (this entry)
- MODIFIED: `DEVNOTES.md`, `NEXT_SESSION_PROMPT.md`

### Deferred to later builds

- `menu_tts.cpp` T-handler `!shift` gate. Theoretical conflict only.
- Deep-research doc comment fix (still says `0x01CFECCC`).
- `field_display_names.h` audit (fieldIds 0x0134 / 0x0136 mislabeled).
- v0.15.14.0 candidate work: hook the engine write to 0x01CFE92C for more reliable freeze; or add value-range "spotlight" pass to the scanner so future address hunts surface slow timers even amid faster-changing neighbors.

## v0.15.13.1

Region expansion for the in-mod timer scanner, after the v0.15.13.0 BAT showed the scanner working mechanically but failing to surface a candidate matching the visibly-active Dollet 30-minute countdown.

### v0.15.13.0 BAT findings (why this build exists)

Aaron loaded a save in the Dollet comm tower (post-Elvoret, timer actively counting down) and captured an F11 screenshot at 21:24:47 showing `28:19` on the timer HUD. The mod log confirms the scanner ran correctly: `Initialize: armed`, `First snapshot done at slot 0: Region 1 2/2 pages mapped, Region 2 256/256 pages mapped`, 11 analysis cycles. No SEH faults; both regions fully mapped.

But none of the candidates surfaced over those 11 cycles match any plausible encoding of "28:19 remaining":

- SECONDS encoding expected cur ≈ 1699 — no candidate near that value
- MINUTES encoding expected cur ≈ 28 or 29 — no candidate in that range
- FRAMES@30Hz encoding expected cur ≈ 50970 — no candidate near that
- MS encoding expected cur ≈ 1,699,000 — filtered out by v0.15.13.0's `MAX_PLAUSIBLE_VAL = 200,000`

The actual candidates were either (a) very-fast-changing animation counters during field load (cycle 7's 16 entries at `0x01DC67xx-0x01DC68xx` with rate ~115/s and cur values 60-75 — entity state during the comm-tower-interior load), (b) menu-state byte-boundary artifacts (uint16 reads spanning a byte where the low byte changed, looking like dec=256 in uint16), or (c) the recurring `0x01D2B106 dec=32 rate=8/s` counter (constant pattern, probably an audio/input system tick).

Diagnosis: the chase timer global lives in a region the v0.15.13.0 scanner did not cover. The address-resolution log lists many engine globals — `pCurrentFieldId = 0x01CD2FC0`, `pCurrentFieldName = 0x01CD2DB0`, `pMode0Phase = 0x01CE4760`, `pMode0InitFlag = 0x01CE0758`, `pMasterSfxVolume = 0x01CD1794`, `_mode = 0x01CD8FC6`, `pKeyboardState = 0x01CD02D8`, `pEngineInputValidButtons = 0x01CD01F8` — all in the `0x01CD0000-0x01D00000` range, which is exactly the 192 KB gap between v0.15.13.0's Region 1 (8 KB at the game-object struct base `0x01CFE9B8`) and Region 2 (`0x01D00000-0x01E00000`). The chase timer is almost certainly in that neighborhood.

### Changes

**Region 1 expanded**: from 8 KB at `0x01CFE9B8` to **192 KB at `0x01CD0000`** (covers the broader engine-globals zone). The game-object struct at `0x01CFE9B8` is now at offset `0x2E9B8` inside this expanded region. Page count grows from 2 to 48. Static buffer grows from 40 KB to 960 KB.

**`MAX_PLAUSIBLE_VAL` raised** from 200,000 to **2,000,000**. Admits ms-encoded 30-minute timers (1,800,000 ms at chase start).

**`MAX_RATE_PER_SEC` raised** from 200 to **2000**. Admits ms-encoded decrements at ~1000/sec.

**Bug fix: "Ring is now full" log spam.** v0.15.13.0 fired this line every snapshot tick (every second) after `s_snapshotsTaken` saturated at `SNAPSHOT_COUNT`. v0.15.13.1 adds `s_ringFullLogged` boolean so the line fires exactly once on the transition from 4→5 snapshots.

### Memory cost

Static buffers grow from 5.04 MB to ~5.96 MB total. Per-snapshot CPU cost grows proportionally with region 1 size (now scans 48 pages instead of 2, but region 2's 256 pages dominate anyway). Per-analyze CPU cost grows: 192 KB has ~98k uint16 + ~49k uint32 candidates, plus region 2's ~786k. Inner loop is still tight; should land in the 50-100 ms range at 5-second cadence.

### What the next BAT will tell us

Aaron loads the same comm-tower save and plays for ~10-15 seconds (enough for the ring to fill plus one analyze cycle). The `[CountdownScan]` log shows the candidate dumps. Expected outcomes:

- **Clean find in R1**: the timer global is in the newly-scanned engine-globals area. Look for a candidate whose `cur` value matches the on-screen timer value at the screenshot moment (1699 for seconds, 50970 for frames@30Hz, 28 or 29 for minutes, ~1.7M for ms). Then v0.15.13.2 hardcodes that address.
- **Multiple plausible candidates**: several addresses look timer-shaped. v0.15.13.2 adds a value-range "spotlight" pass that filters per encoding so the right one is easier to pick out.
- **Still no candidate**: the timer is either outside both regions or encoded in a way our filters miss. v0.15.14.0 pivots to Path B — hook the DISPTIMER opcode (`0x09D`) at JSM dispatch and read whatever memory address the engine reads to render the HUD value.

### Files

- MODIFIED: `src/countdown_scan.inl` (region 1 expansion, bound widening, log-spam fix)
- MODIFIED: `src/ff8_accessibility.h` (version bump to 0.15.13.1)
- MODIFIED: `CHANGELOG.md` (this entry)
- MODIFIED: `DEVNOTES.md`, `NEXT_SESSION_PROMPT.md`

No other source changes. `src/countdown_timer.cpp` is unchanged from v0.15.13.0.

## v0.15.13.0

In-mod memory scanner for the live FF8 mission timer global. After Aaron clarified that his v0.15.12.0 BAT save was post-Elvoret in the Dollet comm tower — i.e. the 30-minute escape countdown was actively running and visibly decrementing on screen — we know the script-side snapshot at `0x01CFEC8C` does not mirror the live timer. The `[CountdownTimer]` log lines from that BAT showed `var724 raw=0` throughout, conclusively. v0.15.13.0 adds a scanner that hunts for the address that actually does decrement, so v0.15.13.1 can repoint reads to it.

### New module: `src/countdown_scan.inl`

A textually-included `.inl` (no `deploy.bat` change needed; the .inl reaches the build through the existing `countdown_timer.cpp` compilation unit) that:

- Snapshots two regions of process memory into its own buffers every 1 second:
  - **Region 1** at `0x01CFE9B8 + 8 KB`. Covers the game-object struct in case the field-var stack lives at some offset inside it rather than at offset 0. Cheap (2 pages).
  - **Region 2** at `0x01D00000 .. 0x01E00000`. The 1 MB engine-globals zone the deep-research doc identified as the most plausible home of the live engine countdown global. 256 pages.
- Maintains a 5-snapshot ring → 4-second time window of history.
- Each `SEH-wrapped` per-page read; pages that fault on first read are marked invalid and skipped from then on.
- Analyses every aligned `uint16` and `uint32` inside the regions whose values across the 5 snapshots are: all nonzero, all not `0xFFFF` / `0xFFFFFFFF` (FF8 unset sentinels), monotonically non-increasing, with total decrement > 0, with current value < 200000 (filters out pointer-like values), and with per-second rate in [0.10, 200.0] (admits seconds-level, frames@30Hz-level, and even minutes-level timers that happen to tick during the window, while rejecting random data noise and large counters).
- Logs the top-16 candidates per region per width every 5 seconds to `ff8_mod.log` under tag `[CountdownScan]`, sorted by total decrement. Each line includes address, current value, oldest value, total decrement, and per-second rate. From these Aaron can identify the Dollet timer by matching expected values (~1800 if SECONDS, ~30 if MINUTES, ~54000 if FRAMES_30HZ) at the start of his BAT session and seeing them drop steadily.
- Always-on for v0.15.13.0 — no field-id gating. Aaron loads any save with an active timer, plays for ~5 seconds to fill the ring, then for ~5 more seconds to see the first analyse cycle. v0.15.13.1+ may add gating once the address is known.

Memory cost: ~5 MB static (snapshot ring) + ~40 KB (region 1 ring). Per-frame cost: dominated by `Scan::Update` which mostly short-circuits on the snapshot/log-interval checks; per-snapshot is ~256 SEH-wrapped 4 KB memcpys (~1-2 ms total); per-analyse is the inner loop over ~786k candidate addresses (~50-100 ms). The analyse cost lands in a 5-second cadence so the per-second amortised cost is small.

### Wired into `src/countdown_timer.cpp`

Three changes there:

1. `#include <cstring>` added at the top (the `.inl` uses `memcpy` and `memset`).
2. Forward declaration of the `Scan` sub-namespace's `Initialize` and `Update(DWORD)` so the calls from `CountdownTimer::Initialize` and `CountdownTimer::Update` resolve before the `.inl` definition.
3. `Scan::Initialize()` added at end of `CountdownTimer::Initialize()`; `Scan::Update(GetTickCount())` added unconditionally at end of `CountdownTimer::Update()` (runs regardless of whether the var724 read faulted, since the scanner has its own per-page fault handling). The `#include "countdown_scan.inl"` sits at the bottom of the `CountdownTimer` namespace block so the definitions land in `CountdownTimer::Scan::*`.

Existing var724 logic is unchanged in behavior — still reads `0x01CFEC8C`, still runs the state machine, still polls T and Shift+T. The scanner is purely additive diagnostic for this build.

### Cosmetic cleanup also in this commit

The deep-research doc and the original `countdown_timer.cpp` comments both said "0x01CFE9B8 + 724 = 0x01CFECCC" — that's wrong math. 724 decimal = 0x2D4 hex, and 0xE9B8 + 0x2D4 = `0x01CFEC8C`. The C++ code computed the correct value at compile time (via `FIELD_VAR_STACK_BASE + 724`), so the binary was right; only the comments were misleading. Corrected throughout `src/countdown_timer.cpp` (header block + the BAT-result comment that explains what we learned in v0.15.12.0). The deep-research doc still has the original wrong math; that's a documentation cleanup task tracked in backlog.

### Expected BAT outcome

Aaron loads a save with the Dollet timer active (the same save shape he used for the v0.15.12.0 BAT). The mod log shows the existing `[CountdownTimer]` lines (`Initialize`, `var724 raw=0` once, no further changes because the snapshot doesn't update — same as v0.15.12.0). New: `[CountdownScan] Initialize: armed.` near startup; `[CountdownScan] First snapshot done at slot 0: Region 1 N/2 pages mapped, Region 2 N/256 pages mapped.` after ~1 second; `[CountdownScan] Ring is now full (5 snapshots). Analysis will begin on the next scheduled log tick.` after ~5 seconds; then every 5 seconds a `=== Scan cycle #N ===` block listing the top candidates per region/width.

Three possible BAT outcomes (in increasing severity of follow-up needed):

- **Clean find.** The Dollet timer appears at the top of `R2 u16` or `R2 u32` (or `R1` if the field-var stack really is inside the game-object struct) with the expected value (~1800 / ~30 / ~54000 at fresh chase start, decreasing). v0.15.13.1 hardcodes that address and the timer reads work. Aaron reports which address + width + initial value.
- **Multiple plausible candidates.** Several addresses show timer-like behavior but it's not obvious which is the real Dollet timer. v0.15.13.1 adds a tighter filter (e.g. value must match a known starting duration within tolerance at chase start) or adds a longer ring window (covers more time so minute-level timers stand out more).
- **No clean candidate.** The scanner finds many addresses but none match expected values, OR finds nothing because Region 2 is mostly unmapped. v0.15.14 falls back to Path B (SETTIMER opcode hook at JSM dispatch slot 0x9C): hook the script-VM dispatch table at the SETTIMER index, capture the duration parameter when the chase script calls it, simulate locally off `GetTickCount`. Doesn't give freeze, but gives reliable read-and-announce.

### Files

- NEW: `src/countdown_scan.inl`
- MODIFIED: `src/countdown_timer.cpp` (scanner wiring, comment corrections)
- MODIFIED: `src/ff8_accessibility.h` (version bump to 0.15.13.0)
- MODIFIED: `CHANGELOG.md` (this entry)
- MODIFIED: `DEVNOTES.md`, `NEXT_SESSION_PROMPT.md`

### Deferred (post-scanner-find or post-Path-B)

- `menu_tts.cpp` T-handler `!shift` gate. Theoretical conflict only.
- `Plan & Research Documents/Dollet timer countdown deep research results.md` comment fix (still has the 0x01CFECCC typo).
- `src/field_display_names.h` audit (wrong mappings for fieldIds 0x0134 / 0x0136 in the Dollet comm tower area, surfaced by the v0.15.12.0 BAT interpretation cycle).

## v0.15.12.0

First implementation of mission countdown timer accessibility, plus a structural cleanup that retired two project files which had grown past the size at which Claude (or any editor with a bounded buffer) could safely round-trip them. The two changes ship together because the cleanup is what made the version bump for the countdown work even possible.

### Countdown timer module (NEW)

New `src/countdown_timer.h` / `src/countdown_timer.cpp` targeting the Dollet 30-minute mission timer and, by virtue of FF8 having a single generic countdown system shared across all timed events, also Fire Cavern (10/20/30/40 min), Missile Base, Centra Ruins Odin, and Rinoa-in-space.

Reads field var 724 ("Dollet mission time", uint16) at `0x01CFECCC` each frame, SEH-wrapped. State machine: INACTIVE / ACTIVE / FROZEN. Units classifier (FRAMES_30HZ 15000-60000, SECONDS 500-3000, MINUTES 5-60) — rejects values outside these ranges as noise so the classifier can't latch onto an unrelated word at the same address. Scheduler fires TTS at 25:00 / 20:00 / 15:00 / 10:00 / 5:00 / 1:00 / 0:30 boundaries; boundaries above the session's initial value are pre-flagged so Fire Cavern's shorter durations don't fire stale "25 minutes remaining" announcements. T key (gated on `IsActive() && !shift && !alt`) announces remaining time on demand. Shift+T (gated on `shift && !alt`) toggles an experimental freeze that rewrites `0x01CFECCC` each frame to the captured value. Comprehensive `Log::Mod` diagnostic logging on every value change (rate-limited 50 ms), state transition, hotkey press, and units-detection decision.

Wired into `src/dinput8.cpp` (`#include "countdown_timer.h"` plus `Initialize` / `Update` / `Shutdown` calls in the existing module-init / main-loop / cleanup sections) and `src/deploy.bat` (added `countdown_timer.cpp` to the cl.exe compile list).

Research saved at `Plan & Research Documents/Dollet timer countdown deep research results.md`. Key findings: timer opcode family is SETTIMER 0x09C, DISPTIMER 0x09D, SHADETIMER 0x09E, GETTIMER 0x0A4, KILLTIMER 0x0B9 (STIM / WAIT_TIMER / TIMER do NOT exist in FF8 — those names come from FF7's opcode set and contaminated some wiki references). Field-var-stack base on Steam 2013 is `0x01CFE9B8`, and var 724 lands at `+0x2D4 = 0x01CFECCC`. The 0x14 savemap correction does NOT apply to the field-var stack — those are two separate memory regions. The script-side snapshot at `0x01CFECCC` is updated by GETTIMER (opcode 0x0A4) when the field script calls it; the actual per-frame engine timer lives at a separate address in the `0x01D00000-0x01E00000` range that is not in any public source.

### BAT result: Case C — snapshot never observed positive

Aaron triggered the Dollet chase. T key did not announce, and Shift+T spoke "No timer to freeze," which means `IsActive()` returned false the whole time — the countdown module never saw a value in `0x01CFECCC` that the classifier accepted. Either the snapshot stays at zero during the chase (which would mean the field script never calls GETTIMER to refresh it), or it holds a value outside our classifier ranges. The `[CountdownTimer]` log lines in `ff8_mod.log` from a chase session will disambiguate; that diagnostic data is what v0.15.13 needs to design the next attempt.

This is the worst-case branch of the BAT decision tree documented in DEVNOTES, and it's a clear signal that v0.15.13 has to find the live engine global rather than relying on the script-side snapshot. Since Aaron is blind and can't use Cheat Engine or x64dbg to find the address externally, the v0.15.13 path is one of:

- In-mod memory scanner that runs during the chase: snapshot a candidate region every second, diff against previous snapshot, surface addresses whose uint16 / uint32 values decrement monotonically at ~30 Hz or ~1 Hz.
- SETTIMER opcode hook (0x09C in the JSM dispatch table) that captures the duration parameter at chase start, then simulates the countdown locally in the mod off a GetTickCount baseline.

The current snapshot read + Shift+T rewrite path stays in place either way as a complementary diagnostic.

### Structural cleanup — file slimming

Two files had grown past the size at which Claude could safely round-trip them through a single full-file rewrite (the only edit mode available with the filesystem MCP toolset in the current session — no `edit_file`):

- `src/ff8_accessibility.h` was **421.80 KB**, almost all of it a single line-12 comment that contained the inline-changelog chain accreted across roughly 80 versions of the project. The header itself only needed to provide `#pragma once`, three system includes, and the `FF8OPC_VERSION` macro — everything else was historical accretion.
- `CHANGELOG.md` was **488.25 KB**, with entries since project start prepended one by one. The push utility only reads the top entry, so the size was load-bearing nowhere.

Cleanup:

- `src/ff8_accessibility.h` moved to `src/ff8_accessibility_history.h` (NOT included by the build — nothing references it; the rename preserves the full inline-changelog history off the build path). New slim `src/ff8_accessibility.h` written with the header guard, the three system includes, a pointer comment to the history file and to CHANGELOG.md, and the `FF8OPC_VERSION` macro at v0.15.12.0 with no trailing comment.
- `CHANGELOG.md` moved to `CHANGELOG_HISTORY.md` (preserves all pre-v0.15.12.0 entries). New slim `CHANGELOG.md` written with the file header explaining the format + push-utility contract, this v0.15.12.0 entry on top, and a pointer to `CHANGELOG_HISTORY.md` for older content. Future versions get prepended here as normal.

`deploy.bat`'s version-extract regex (`findstr /B /C:"#define FF8OPC_VERSION "`) still resolves cleanly to "0.15.12.0" since the new header has exactly one matching line at column 0 with no historical mentions to compete with. (The history file is not in the build's includepath traversal, but even if it were, the regex pattern starts at column 0 and all the historical mentions inside it are inside comment lines starting with `// `, which the `/B` anchor correctly excludes.)

### Files

- NEW: `src/countdown_timer.h`
- NEW: `src/countdown_timer.cpp`
- NEW: `src/ff8_accessibility_history.h` (renamed from `src/ff8_accessibility.h`, preserved off the build path)
- NEW: `CHANGELOG_HISTORY.md` (renamed from `CHANGELOG.md`)
- MODIFIED: `src/dinput8.cpp` (countdown_timer wiring; some pre-existing v0.15.9.11.3.x historical-rationale comment blocks compressed to short summaries during the rewrite to fit within Claude's response budget for a 720-line file — the full historical comments are preserved at GitHub HEAD `8d29ee61` if a future session needs to restore them)
- MODIFIED: `src/deploy.bat` (added `countdown_timer.cpp` to the cl.exe compile list)
- REPLACED: `src/ff8_accessibility.h` (slim version, v0.15.12.0 macro only)
- REPLACED: `CHANGELOG.md` (slim version, this entry on top)
- MODIFIED: `DEVNOTES.md`, `NEXT_SESSION_PROMPT.md`, `Plan & Research Documents/Dollet timer countdown deep research results.md`

### Deferred to v0.15.13

- In-mod scanner for the live engine timer global, OR SETTIMER opcode hook for start-event simulation (Case C remediation per BAT result above).
- `menu_tts.cpp` T-handler `!shift` gate so Shift+T doesn't fire both `AnnouncePlayTime` and `CountdownTimer::ToggleFreeze` in menu mode 6. Theoretical conflict only — player can't realistically open the menu during the Dollet chase — but worth fixing.
