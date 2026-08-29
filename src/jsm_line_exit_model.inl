// jsm_line_exit_model.inl -- WHEN A TRIGGER LINE IS A DOOR
//
// v0.111.0 (#dsrc). The rule the REQ-follow(line) pass in
// field_archive_jsm_linepass.inl applies, pulled out so it can be argued with
// in a test rather than in a boss corridor.
//
// Aaron, descending the Deep Sea Research Center: "The hatchways down are
// either not showing up in the catalog or auto-drive is not approaching them
// correctly." Not one of the five tower floors offered a way down, because
// every one of them is built the same way: a LINE entity named `Sitahe`
// (shita-e, "downward") whose touch script REQs a method belonging to the party
// leader, and the MAPJUMP lives in THAT method. The catalog found the jump,
// attributed it to Squall, and dropped it -- rightly, on its own terms, because
// a party member is not a door.
//
// The door is the line.

// FF8 has 900-odd fields; the display-name table in field_display_names.h is
// the authority and nothing on the disc is near this. The bound exists because
// a REQ-follow can land on a method whose "destination" is an unresolved
// variable marker -- the disc-wide scanner diff threw up 0x8001FFFE more than
// once -- and an exit to field -2147418114 is worse than no exit at all.
static const int JSM_FIELD_ID_MAX = 1000;

static bool JsmDestIsPlausibleField(int dest)
{
    return dest >= 0 && dest < JSM_FIELD_ID_MAX;
}

// The two line types the catalog has nothing to say about, and only those.
//
// An INTERACTIVE line is something the player talks to first and foremost, and
// retyping one into a door loses the conversation -- the same argument the
// category-3 rule makes about NPCs. A SAVE line is already doing a job. A line
// that is ALREADY a screen boundary carries its own MAPJUMP and needs nothing
// from this pass.
//
// `hasDialogReqTarget` is the reason this predicate takes four arguments
// instead of two: the pass that sets it runs after the category-3 REQ-follow,
// so a rule folded into that loop fires too early to see it. The disc-wide
// scanner diff caught exactly that -- nine lines (`Eventline`, `mapjumpline`,
// `Jumpline1` on three fields) promoted to doors that the dialog pass would
// have made interactive.
static bool JsmLineExitEligible(int jsmCategory, int lineType, int typeLineEvent,
                                int typeLineCameraPan, bool hasDialogReqTarget,
                                bool isSaveLine)
{
    if (jsmCategory != 1) return false;
    if (lineType != typeLineEvent && lineType != typeLineCameraPan) return false;
    if (hasDialogReqTarget) return false;
    if (isSaveLine) return false;
    return true;
}

// Which destination wins when the entity that OWNS the MAPJUMP already carries
// one of its own.
//
// v0.62.1's rule is that the owner's answer is authoritative, because only
// MapjumpResolver::Run knows a destination that came out of the variable block.
// That holds -- with one exception the DSRC found. ddtower1's `Director` and
// `Hantei` are both typed Map Exit with **param 0**: the resolver's answer for a
// destination it could not follow. Field 0 is `wm00`, so taking it sent
// ddtower1's only way back to the Research Center core through the
// world-map-staging filter, which deleted it. The MAPJUMP3 inside the method
// says 847 in plain literals, and 847 is right.
//
// So: the owner wins unless it is offering zero over a real literal.
static bool JsmOwnerDestWins(int ownerDest, int methodDest)
{
    if (!JsmDestIsPlausibleField(ownerDest)) return false;
    if (ownerDest == 0 && methodDest != 0) return false;
    return true;
}
