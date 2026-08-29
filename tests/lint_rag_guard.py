#!/usr/bin/env python3
"""The forward-collision guard must be keyed on FLYING, not on having a landing.

v0.79.0 wrote `s_ragLanding == nullptr` here, and the 23:05 BAT ran the
controlled experiment inside one session:

  Sorceress Memorial   has a landing row -> guard OFF -> flew 53 km, arrived
  Mobile Balamb Garden no landing row    -> guard ON  -> position trace decays
                                            355, 339, 270, 209, 148, 88, 27, 6, 0
                                            and the drive gave up 53 km short

The airship has NO movement mask -- 0x53E6B0 falls through to `return 1` for
vehicle 0x32 -- so a fine-grid cell marked blocked ahead of it is scenery, not
an obstacle, whether or not the mod knows where that destination's landing is.
The three MOBILE destinations can never have a landing row at all, so keying the
guard on one permanently re-broke them.

This lint fails the build if that condition ever goes back to asking about the
landing row.
"""
import re
import sys

TARGET = 'src/world_map_drive_exec.inl'
GUARD  = re.compile(r'if\s*\(\s*!s_driveNavmeshPath\s*&&\s*([^)]*)\)')

def main():
    try:
        src = open(TARGET, encoding='utf-8').read()
    except OSError as e:
        print('lint_rag_guard: cannot read %s (%s)' % (TARGET, e))
        return 1
    hits = GUARD.findall(src)
    if not hits:
        print('lint_rag_guard: the forward-collision guard condition is GONE from %s.\n'
              '  Expected `if (!s_driveNavmeshPath && !s_ragFlying) {`.\n'
              '  If the guard moved, move this lint with it.' % TARGET)
        return 1
    bad = 0
    for cond in hits:
        c = cond.strip()
        if 's_ragLanding' in c:
            print('lint_rag_guard: %s\n'
                  '  the forward-collision guard is keyed on the LANDING ROW: `%s`\n'
                  '  Key it on FLIGHT instead: `!s_ragFlying`.\n'
                  '  A landing row says where to set down. It does not say whether the\n'
                  '  terrain ahead can be flown over -- and the three mobile destinations\n'
                  '  never have one, so this condition strands them.' % (TARGET, c))
            bad += 1
        elif '!s_ragFlying' not in c:
            print('lint_rag_guard: %s\n'
                  '  the forward-collision guard no longer excuses flight: `%s`\n'
                  '  Expected `!s_ragFlying` in the condition. The airship crosses ocean,\n'
                  '  mountain and forest alike; the guard makes it crab sideways instead\n'
                  '  of pressing forward, and it coasts to a halt.' % (TARGET, c))
            bad += 1
    # v0.84.0: and the two un-wedge triggers stay off for the Ragnarok.
    #
    # The 12:37 BAT fired thirteen reverse bursts at a ship that was never stuck
    # -- 0x53E6B0 falls through to `return 1` for vehicle 0x32, so no polygon is
    # closed to it and there is nothing to back out of. Each burst undid the
    # progress made, which kept net displacement small, which fired the next.
    WEDGE_SITES = [
        ('src/world_map_drive_exec.inl', 'hardWedge &&',
         'the hard-wedge reverse fast path'),
        ('src/world_map_drive.inl', '!s_drivePathWorld &&',
         'the stuck-check reverse burst'),
    ]
    for path, anchor, what in WEDGE_SITES:
        try:
            body = open(path).read()
        except OSError:
            print('lint_rag_guard: cannot read %s' % path)
            bad += 1
            continue
        i = body.find(anchor)
        if i < 0:
            print('lint_rag_guard: %s\n'
                  '  cannot find %s (anchor %r moved).\n'
                  '  This lint cannot see the guard it exists to protect; fix the\n'
                  '  anchor rather than deleting the check.' % (path, what, anchor))
            bad += 1
            continue
        window = body[i:i + 400]
        if 'RagWedgeAllowed(s_ragFlying)' not in window:
            print('lint_rag_guard: %s\n'
                  '  %s no longer excuses the Ragnarok.\n'
                  '  Expected `RagWedgeAllowed(s_ragFlying)` in the condition. The\n'
                  '  airship cannot wedge, so a burst can only undo the progress it\n'
                  '  has made -- and that keeps net displacement small, which fires\n'
                  '  the next burst. The 12:37 BAT looped thirteen times.' % (path, what))
            bad += 1

    # v0.87.0: and the planner never plans for a flying Ragnarok.
    #
    try:
        pl = open('src/world_map_planner2.inl').read()
    except OSError:
        print('lint_rag_guard: cannot read src/world_map_planner2.inl')
        bad += 1
        pl = ''
    if pl:
        if 'WmResolveVehicle' not in pl:
            print('lint_rag_guard: src/world_map_planner2.inl\n'
                  '  the planner resolves its vehicle without WmResolveVehicle.\n'
                  '  s_lastVehicle is the LOCOMOTION BYTE\'s verdict and it has never\n'
                  '  once seen the Ragnarok. Resolve through the shared predicate or\n'
                  '  the VEH_RAGNAROK bail below it never fires -- the 13:41 BAT spent\n'
                  '  ten seconds of frozen game planning a 302-cell WALKING route for\n'
                  '  an airship.')
            bad += 1
        if 'veh == VEH_RAGNAROK || s_ragFlying' not in pl:
            print('lint_rag_guard: src/world_map_planner2.inl\n'
                  '  the planner bail no longer checks s_ragFlying.\n'
                  '  That latch is set at StartAutoDrive from RagIsFlying(), which asks\n'
                  '  BOTH sources; it is what still knows the answer when the engine id\n'
                  '  read fails mid-drive. A flying drive must never plan -- the airship\n'
                  '  has nothing to route around, so the straight line IS the route.')
            bad += 1

    # v0.90.0: the airship's six controls stay six.
    #
    # SetDriveKeys presses the UP ARROW **and** the A key on one flag, because for
    # a car they are the same pedal. For the Ragnarok A is the THROTTLE and the up
    # arrow is the CLIMB, and the drive spent two builds unable to say one without
    # the other -- v0.88.0 concluded UP was the throttle, v0.89.0 concluded it was
    # the altitude, and both were half right.
    try:
        hp = open('src/world_map_drive_helpers.inl').read()
        ep = open('src/world_map_drive_exec.inl').read()
    except OSError:
        print('lint_rag_guard: cannot read the drive helper/executor')
        bad += 1
        hp = ep = ''
    if hp:
        i = hp.find('static void SetFlightKeys')
        if i < 0:
            print('lint_rag_guard: src/world_map_drive_helpers.inl\n'
                  '  SetFlightKeys is gone. The airship needs one flag per control:\n'
                  '  A forward, W back, UP climb, DOWN descend, LEFT/RIGHT turn.\n'
                  '  SetDriveKeys cannot express them -- it welds A to the up arrow.')
            bad += 1
        else:
            body = hp[i:i + 2000]
            for key, what in (("'A'", 'the A throttle'), ("VK_UP", 'the climb'),
                              ("VK_DOWN", 'the descent'), ("VK_LEFT", 'the left turn'),
                              ("VK_RIGHT", 'the right turn')):
                if key not in body:
                    print('lint_rag_guard: src/world_map_drive_helpers.inl\n'
                          '  SetFlightKeys no longer presses %s (%s).' % (key, what))
                    bad += 1
    if ep and 'SetFlightKeys(s_flyCmdFwd' not in ep:
        print('lint_rag_guard: src/world_map_drive_exec.inl\n'
              '  the executor no longer dispatches flight through SetFlightKeys.\n'
              '  Falling back to SetDriveKeys welds the throttle to the climb again.')
        bad += 1

    # v0.94.0: the firing-area escape never steers a flying Ragnarok.
    #
    # It exists so a walker or a car does not blunder into a world-map entry
    # trigger's firing area. An airship cannot trip one at all -- the game's own
    # evaluator prints "vehId=50 ... NO PROGRAM IS EVALUATED" every tick of every
    # flight. The 18:42 BAT: Aaron boarded beside Sorceress Memorial, inside its
    # firing area, and the escape spent a minute swinging the aim round that box
    # while the ship was already pointing dead at Esthar.
    ESC_SITES = [
        ('src/world_map_drive_helpers.inl', 'static void ArmFiringAreaEscape',
         'the escape arming', 'if (s_ragFlying) {'),
        ('src/world_map_drive.inl', 'if (s_driveEscapeActive && !s_ragFlying) {',
         'the escape steering site', 'if (s_driveEscapeActive && !s_ragFlying) {'),
    ]
    for path, anchor, what, need in ESC_SITES:
        try:
            body = open(path).read()
        except OSError:
            print('lint_rag_guard: cannot read %s' % path)
            bad += 1
            continue
        i = body.find(anchor)
        if i < 0:
            print('lint_rag_guard: %s\n'
                  '  cannot find %s (anchor %r moved).' % (path, what, anchor))
            bad += 1
            continue
        if need not in body[i:i + 3000]:
            print('lint_rag_guard: %s\n'
                  '  %s no longer excuses flight (expected %r).\n'
                  '  An airship cannot trip an entry trigger, and the escape moves the\n'
                  '  AIM -- the 18:42 BAT was told to turn away from a bearing it was\n'
                  '  already dead on, for a full minute.' % (path, what, need))
            bad += 1

    # v0.96.0: the blocked detector counts the flag flight actually sets.
    #
    # v0.90.0 split the airship's throttle off the car's welded pedal and moved it
    # to s_flyCmdFwd -- and left this counter reading wantUp, which the flight
    # branch sets to false outright. The detector counted zero on every flight for
    # six builds, so the 19:38 stall took eighteen seconds to say something useless
    # instead of six to say something he could act on.
    try:
        ex = open('src/world_map_drive_exec.inl').read()
    except OSError:
        print('lint_rag_guard: cannot read src/world_map_drive_exec.inl')
        bad += 1
        ex = ''
    if ex and 's_ragFlying && s_flyCmdFwd) s_ragGasTicks++' not in ex:
        print('lint_rag_guard: src/world_map_drive_exec.inl\n'
              '  the blocked detector no longer counts s_flyCmdFwd.\n'
              '  The flight branch sets wantUp = false, so a counter keyed on wantUp\n'
              '  counts zero forever and the detector never fires -- which is exactly\n'
              '  what happened between v0.90.0 and v0.96.0.')
        bad += 1

    # v0.95.0: the planner bail is the FIRST thing PlanDrivePath does, and the aim
    # invariant is asserted after every override.
    #
    # v0.87.0 put the Ragnarok bail thirty-five lines into PlanDrivePath, BELOW
    # RouteNetPlan and PlanPathGrid -- both of which plan and return true -- so it
    # never ran once. A guard that runs after the thing it guards is not a guard.
    try:
        pl2 = open('src/world_map_planner2.inl').read()
        dr  = open('src/world_map_drive.inl').read()
    except OSError:
        print('lint_rag_guard: cannot read the planner/drive')
        bad += 1
        pl2 = dr = ''
    if pl2:
        f = pl2.find('static bool PlanDrivePath(')
        g = pl2.find('veh == VEH_RAGNAROK || s_ragFlying', f)
        for call in ('RouteNetPlan(', 'PlanPathGrid('):
            c = pl2.find(call, f)
            if c >= 0 and g >= 0 and c < g:
                print('lint_rag_guard: src/world_map_planner2.inl\n'
                      '  %s runs BEFORE the Ragnarok bail in PlanDrivePath.\n'
                      '  That is v0.87.0 exactly: the bail was there and never ran once,\n'
                      '  because both planners return true before reaching it. The 18:58\n'
                      '  BAT was 151,552 A* expansions and a 302-cell WALKING path handed\n'
                      '  to an airship. The bail must be the FIRST statement.' % call)
                bad += 1
    if dr and 'RagFlightOwnsDrive()' not in dr:
        print('lint_rag_guard: src/world_map_drive.inl\n'
              '  the flight aim invariant is gone. Everything above it is machinery\n'
              '  for vehicles that touch the ground, and four of them have been caught\n'
              '  steering the airship in five builds. RagFlightClampAim asserts, once,\n'
              '  that a flying drive aims at its destination -- including the ones\n'
              '  nobody has found yet.')
        bad += 1

    if bad == 0:
        print('lint_rag_guard: OK (%d guard condition(s) + %d un-wedge site(s) + planner '
              '+ flight keys + %d escape site(s) checked)'
              % (len(hits), len(WEDGE_SITES), len(ESC_SITES)))
    return 1 if bad else 0

if __name__ == '__main__':
    sys.exit(main())
