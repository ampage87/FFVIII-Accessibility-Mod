// line_camera_pan_surface_model.inl -- WHEN A "CAMERA PAN" LINE IS ACTUALLY A LADDER
//
// v0.115.0 (#centra). Aaron, on the Centra Ruins: "look through the Centra
// Ruins fields to ensure all the entities needed to get around are properly
// reflected in the catalog."
//
// They were not, and the reason is one missing opcode family.
//
// A Line entity is classified by the opcodes its scripts contain
// (field_archive_jsm_classify.inl): dialog -> INTERACTIVE, MAPJUMP ->
// SCREEN_BOUND, BATTLE -> EVENT, BGDRAW/scroll -> CAMERA_PAN, and -- the
// silent default -- anything unrecognised -> CAMERA_PAN. The catalog hides
// CAMERA_PAN, correctly: a camera-pan line is a transparent marker that moves
// the viewport, not something a player walks to.
//
// Every ladder in the Centra Ruins landed in that silent default. `crroof1`'s
// lad0 and lad1, `crtower1`'s leftlad0/rightlad0/console0, `crtower2`'s
// leftlad0/1/2 and rightlad0, `crtower3`'s ladup0/laddw0/sw0, `crpower1`'s
// sw0 -- fourteen lines, and between them they are the ONLY way up, down or
// into the Centra Ruins puzzle. None of them appeared in the catalog. A blind
// player could stand on a ladder and never be told it was there.
//
// What they actually contain is this, identically, on `touch`:
//
//     PSHN_L 192 ; <0x06D> ; PSHL 0 ; PSHN_L 1 ; OPER == ; JPF
//     PSHN_L 1 ; PSHN_L <script> ; <0x019>
//
// -- a button test, then opcode 0x019. 0x014/0x015/0x016 are REQ/REQSW/REQEW,
// and 0x017/0x018/0x019 are the party-relative trio that follows them, PREQ /
// PREQSW / PREQEW: "run this script on the party member in slot N". The
// scanner's foundEventOp test lists REQ, REQSW and REQEW and stops there, so a
// line whose only dispatch is party-relative registers as containing nothing
// at all.
//
// The dispatched scripts say plainly what each line is. crtower1's leftlad0
// runs `squall::p0_ladup0`, which MAPJUMP3s to field 284 (crtower2) -- one
// floor up. crtower2's leftlad0 runs `squall::leftdw0`, which MAPJUMP3s to 283
// -- one floor down. crtower3's sw0 runs `squall::eye0`. crpower1's sw0 runs
// `squall::p0sw0` and sets var[359] |= 128. Square named every one of them.
//
// WHY NOT JUST RECLASSIFY. Adding the PREQ family to the classifier would move
// 95 CAMERA_PAN lines disc-wide into a visible type, and they are not all
// ladders: fhtown22's `blocker1`/`blocker2`, gmcont1's `CantGoNext`, and some
// three dozen `eventline*` are auto-firing story triggers that a player must
// never be sent to walk into. A single flag cannot tell those apart from a
// ladder, and guessing wrong here means steering a blind player into a cutscene
// he did not ask for.
//
// So the gate is the naming table, which is a per-symbol human decision and
// therefore cannot be wrong by accident. A CAMERA_PAN line surfaces if and only
// if FIELD_SCOPED_ENTITIES or ENTITY_DISPLAY_NAMES gives it a name. The blast
// radius is exactly the set of symbols someone has looked at and named -- today
// the fourteen Centra Ruins lines, plus any line whose SYM already had a name
// in the tables. Nothing else in the game moves.
//
// The naming tables are the same ones v0.113.0 taught the trigger-line path to
// consult, and `LineNameIsCurated` is the same predicate that decides whether a
// name outranks "Interaction N". This reuses both rather than inventing a third
// way to ask the question.

// isCameraPan: the captured line's JSM type is JSM_ENT_LINE_CAMERA_PAN.
// curatedName: whatever LineDisplayName() returned for it (nullptr if the
// tables had nothing to say).
//
// The order matters. A non-camera-pan line is not this function's business at
// all -- it reaches the catalog through the paths that already existed, and
// answering "true" for one would double-emit it.
static bool CameraPanLineSurfaces(bool isCameraPan, const char* curatedName)
{
    if (!isCameraPan) return false;
    return curatedName != nullptr && curatedName[0] != '\0';
}

// ============================================================================
// v0.116.0 (#centra): A CONTROL THE ENGINE HAS SWITCHED OFF STILL EXISTS
// ============================================================================
//
// Aaron, on the first control panel in the Centra Ruins tower: "it wasn't shown
// in the catalog at first. I had to go to where I thought the control panel was
// and activate it myself. However, once I left and came back to the field the
// control panel was included in the subsequent visit. We need to ensure the
// control panel is always in the catalog."
//
// The mod was not wrong -- it was silent, which is worse. The 2026-08-27 log
// shows the engine switching that line off itself, at every load of crtower1:
//
//   [SETLINE] call#19 field=crtower1 ent=0x0188BB58 ... center=(1892,-15)
//   [LINEOFF] field=crtower1 ent=0x0188BB58
//
// on the visits at 17:03:00, 17:04:45, 17:05:34 and 17:08:49 -- and NOT on the
// visit at 17:11:43, the one after he threw the power switch in crpower1. Then
// LINEOFF again at 17:12:03, the moment he used it, and again on the way out at
// 17:31:35. `console0` is live in exactly one window: powered, and not yet
// used. Putting it in the catalog "always" would offer a control that does
// nothing on either side of that window.
//
// So: surface it, and SAY it is not live. This is the rule v0.114.0 wrote and
// then deliberately did not ship, because there the state had to be inferred
// from a decoded script guard whose polarity was ambiguous. Here nothing is
// inferred. LINEOFF is the engine's own statement that crossing this line does
// nothing, the mod hooks it directly (field_nav_opcode_hooks.inl), and the flag
// means one thing only.
//
// SCOPE, kept as tight as the surfacing rule it extends: only a line that is
// (a) a camera pan and (b) named in the tables. Every unnamed line the engine
// switches off stays hidden, which is nearly all of them -- LINEOFF is also how
// the game retires spent story triggers, and a catalog full of "Interaction 4,
// not active" would be worse than the silence this replaces.
static const char* const LCP_OFF_SUFFIX = ", not active";

// A named camera pan reaches the catalog whether the engine has it on or off.
static bool CameraPanLineSurfacesOffToo(bool isCameraPan, const char* curatedName)
{
    return CameraPanLineSurfaces(isCameraPan, curatedName);
}

// The suffix goes on only when there is a name to hang it from and the engine
// really has the line off. "Interaction 2, not active" tells a player nothing
// they can act on; a bare ", not active" is worse still.
static bool LineOffSuffixApplies(const char* curatedName, bool engineActive)
{
    if (engineActive) return false;
    return curatedName != nullptr && curatedName[0] != '\0';
}

// Compose the catalog name. Truncation keeps the base name rather than half a
// suffix -- a player who hears "Control Panel" and walks to a dead panel has
// lost a few seconds; one who hears "Control Pane, not a" has lost the entry.
static void LineOffDisplayName(char* buf, size_t n, const char* curatedName, bool engineActive)
{
    if (buf == nullptr || n == 0) return;
    buf[0] = '\0';
    if (curatedName == nullptr || curatedName[0] == '\0') return;
    const size_t baseLen = strlen(curatedName);
    const size_t sufLen  = strlen(LCP_OFF_SUFFIX);
    if (baseLen + 1 > n) {                       // the name alone will not fit
        size_t cut = n - 1;
        memcpy(buf, curatedName, cut);
        buf[cut] = '\0';
        return;
    }
    memcpy(buf, curatedName, baseLen + 1);
    if (!LineOffSuffixApplies(curatedName, engineActive)) return;
    if (baseLen + sufLen + 1 > n) return;        // no room: keep the bare name
    memcpy(buf + baseLen, LCP_OFF_SUFFIX, sufLen + 1);
}
