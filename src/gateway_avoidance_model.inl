// gateway_avoidance_model.inl -- A ROUTE THAT LEAVES THE FIELD IS NOT A ROUTE
//
// v0.132.0 (#shumi). Aaron, on the Shumi Village side quest: "When I tried to
// use auto-drive to get to the Moomba in Village 2 I kept getting moved back to
// Village 3."
//
// HE IS NOT DESCRIBING A DRIVE THAT WENT WRONG. HE IS DESCRIBING A DRIVE THAT
// DID EXACTLY WHAT IT PLANNED. The 12:35:01 run in tmmura1:
//
//     [drive] goal tri 3 from the entity's own placement       <- Munba2
//     [A*] Path found: 15 triangles, 14 waypoints, start=17 goal=3
//     [funnel] portal 11/15 L=(355,-2781) R=(355,-2781) tri 6->5
//     [funnel] portal 12/15 L=(-68,-2538) R=(362,-3054) tri 5->4
//     [funnel] portal 13/15 L=(-112,-2780) R=(-112,-2780) tri 4->3
//     ...
//     ChaseDetector: fieldId changed to 0x03B0
//     [drive] stopped: (silent -- field change or handoff)
//
// and the field's own INF record:
//
//     [INF-GW] gw[0] line=(36,-2401)->(594,-3077) destId=944 'Village 3'
//
// The Moomba is on triangle 3, in the far west of Village 2. The player started
// on triangle 17 in the east. **The only walkable route between them crosses the
// Village 3 gateway**, and A* neither knew nor cared: it routed straight through
// the doorway, the engine did what a doorway does, and Aaron arrived in Village
// 3 with the drive reporting nothing at all, because a field change is one of
// the two things `StopAutoDrive(nullptr)` means.
//
// WHY A* DID NOT KNOW. The avoidance in EdgeCrossesScreenBound walks
// s_capturedLines -- the lines the field's own scripts register with SETLINE --
// and treats a SCREEN_BOUND one as a wall so a path is planned around it. That
// is the right idea and it has been right since v05.92. But an INF gateway is
// not a SETLINE line. It is a boundary segment in the field's .inf record, the
// engine watches for the crossing itself, and it never appears in
// s_capturedLines at all. So half the doorways in the game were invisible to the
// pathfinder:
//
//     Village 2 has THREE exits and every one of them is an INF gateway.
//     Not one of them was a wall to A*.
//
// This is why the same field could produce a clean drive to a gateway (crossing
// the intended one) and an impossible drive to an NPC (crossing an unintended
// one) with no difference in the code path.
//
// THE FIX IS THE EXEMPTION, NOT THE WALL. Making gateways walls is one line. The
// part that has to be right is that the drive must still be able to USE one --
// `skipGatewayIdx` is the gateway the drive is deliberately heading for, and it
// stays crossable exactly as `skipTriggerIdx` already does for trigger lines.
// Without that, driving to any gateway would immediately fail to find a path to
// its own destination.
//
// AND IF THERE IS GENUINELY NO WAY ROUND, SAY SO. On some fields the target
// really is beyond a doorway -- FF8 lays out towns that way. A* will now report
// no path instead of silently walking the player out of the field. That is the
// better failure: "I can't get you there from here" is something a blind player
// can act on; being teleported to another map with no announcement is not.

// Should this gateway act as a wall for path planning?
//
// `idx` is the gateway being tested, `skipGatewayIdx` the one the drive is
// travelling to (-1 when the drive is not headed for a gateway at all). A
// gateway with no destination is still a wall: crossing it still changes the
// field, and the fact that the mod could not resolve where to is a reason for
// more caution, not less.
static bool GatewayIsPathBarrier(int idx, int skipGatewayIdx)
{
    if (idx < 0) return false;
    if (idx == skipGatewayIdx) return false;   // the one we are deliberately using
    return true;
}

// A degenerate gateway -- both endpoints at the same place, or at the origin --
// cannot be crossed and must not become a wall, because a zero-length segment
// sitting at (0,0) would fence off whatever part of the mesh happens to lie
// across the origin. Seven fields on the disc carry a gateway whose line was
// never filled in.
static bool GatewayLineIsUsable(int x1, int y1, int x2, int y2)
{
    if (x1 == x2 && y1 == y2) return false;
    if (x1 == 0 && y1 == 0 && x2 == 0 && y2 == 0) return false;
    return true;
}
