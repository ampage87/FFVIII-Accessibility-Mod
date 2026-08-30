// camera_transition_model.inl -- A LINE THAT GOES SOMEWHERE IS NOT A CAMERA ANGLE
//
// v0.132.0 (#shumi). Aaron, on his first run at the Shumi Village side quest:
// "The actual elevator, used to enter/exit the village, was labeled a camera
// transition."
//
// HE IS DESCRIBING A RULE THAT HAS BEEN WRONG SINCE v0.20.29, ON 77 FIELDS.
//
// The rule identifies camera-view transition lines by looking for "jump" in the
// SYM name. FF8's own naming does follow that convention -- but so does every
// ordinary map exit the same authors wrote, because a map exit IS a jump. The
// name cannot separate them, and the disc says so without ambiguity. Across all
// 900 fields there are 148 trigger lines whose SYM name contains "jump":
//
//     95   carry a literal destination field id (0..981)   <- REAL EXITS
//     22   carry a PSHM marker (destination read at run time)  <- REAL EXITS
//     17   carry -2, the WORLDMAPJUMP sentinel              <- REAL EXITS
//     14   carry no destination at all                      <- camera transitions
//
// **EVERY LINE WITH A DESTINATION IS AN EXIT. EVERY LINE WITHOUT ONE IS A
// CAMERA TRANSITION. THE SPLIT IS EXACT -- 134 AND 14, NO OVERLAP.** And it is
// corroborated by the type the scanner independently assigns: all 14 true camera
// transitions come out as Interactive Line, Event Trigger or Camera Pan, and not
// one of them is a Screen Boundary; 93 of the 95 literal-destination lines are.
// Nor is there a self-referential case to worry about: zero of the 148 jump to
// the field they are standing in.
//
// v0.60.0 ALREADY FOUND HALF OF THIS. It carved out the 17 world-map lines after
// Aaron reported a Chocobo Forest whose only way out had vanished -- "in those
// cases there should still be an exit to the world map in the catalog" -- and
// the reasoning it wrote down was exactly right: a world-map exit is never a
// camera transition. What it did not do is notice that the argument never
// depended on the world map. It is the *destination* that makes a line an exit.
// A carve-out for one value of the destination left 95 field-to-field exits,
// Shumi Village's elevator among them, still labelled as camera angles.
//
// WHAT THE MISLABEL COSTS A BLIND PLAYER. The name is only half of it. A line
// tagged as a camera transition is also counted in the exit group -- Aaron's log
// reads `'Camera transition, 1 of 3'` -- so he is told the field has three exits
// and given a usable destination for two of them. The third is the way out of
// the village. He found it in the end by driving to the thing with the
// meaningless name and seeing where he ended up, which is exactly the kind of
// exploration the catalog exists to make unnecessary.
//
// THE 14 REAL ONES STILL WORK. bgroom_1's four `bgroom_N_jumpNN`, feclock2's
// left/right pair, cwwood4's in-field `Jump`, ecpway1a, ecmall1a, ecoway3a,
// doani1_1/2, cdfield2, bgmd2_6 -- all keep the label, because none of them goes
// anywhere.

// A destination that is present, whatever form it takes. -1 (and any other small
// negative that is not the world-map sentinel) means the scanner found no
// destination; everything else is a place this line leads to.
//
// The three positive forms are deliberately treated alike. A literal field id is
// certain. A PSHM marker is a destination the script reads out of a variable at
// run time -- the value is not known here, but its EXISTENCE is, and that is the
// whole question this file answers. -2 is WORLDMAPJUMP (opcode 0x10D, handler
// 0x00521820), which sets the transition mode at 0x01CE4760 to 7 where a
// field-to-field jump sets 1.
static bool CamXlineHasDestination(int destFieldId)
{
    if (destFieldId == -2) return true;                       // world map
    // A PSHM marker is 0x80000000 | addr with a 16-bit addr, so the whole family
    // lives in [0x80000000, 0x8000FFFF]. Testing bit 31 alone -- which is the
    // obvious way to write this and the way the first draft of this file did --
    // also catches every ordinary small negative, because -1 is 0xFFFFFFFF. The
    // scanner's "I found no destination" value would then read as a destination,
    // which is the exact inversion this file exists to prevent.
    const unsigned u = (unsigned)destFieldId;
    if (u >= 0x80000000u && u <= 0x8000FFFFu) return true;    // PSHM marker
    return destFieldId >= 0;                                  // literal field id
}

// True when this line really is a camera-view transition: its name says jump and
// it goes nowhere.
//
// `ownFieldId` guards a case the disc does not currently contain -- a jump line
// whose destination is the field it already stands on, which would be a screen
// change rather than a journey. Zero of the 148 do this today; the test is here
// so that if one ever appears it is named honestly rather than offered to a
// blind player as an exit that leads back to where he is standing. Pass -1 when
// the field id is not known and the check is skipped.
static bool CamXlineIsCameraTransition(bool nameHasJump, int destFieldId, int ownFieldId)
{
    if (!nameHasJump) return false;
    if (!CamXlineHasDestination(destFieldId)) return true;
    if (ownFieldId >= 0 && destFieldId == ownFieldId) return true;
    return false;
}

// The name test itself, kept here so the shipped path and the tests agree on it
// rather than each spelling out its own strstr pair.
static bool CamXlineNameHasJump(const char* sym)
{
    if (sym == 0 || sym[0] == '\0') return false;
    for (int i = 0; sym[i] != '\0' && i < 64; i++) {
        const char* p = sym + i;
        int k = 0;
        const char* want = "jump";
        while (want[k] != '\0') {
            char c = p[k];
            if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
            if (c != want[k]) break;
            k++;
        }
        if (want[k] == '\0') return true;
    }
    return false;
}
