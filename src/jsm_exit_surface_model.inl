// jsm_exit_surface_model.inl -- WHICH SCRIPT-DERIVED MAP EXITS ARE REAL EXITS?
//
// v0.131.7 (#centra). The 22:17:00 catalog on crtower2, read in full:
//
//     cat5  JSM ent9  type=Exit name='Exit to Centra Ruins 8' pos=(1451,-383)
//     cat6  JSM ent11 type=Exit name='Exit to Centra Ruins 8' pos=(1451,-383)
//     cat7  JSM ent12 type=Exit name='Exit to Centra Ruins 8' pos=(1451,-383)
//     cat8  JSM ent17 type=Exit name='Exit to Centra Ruins 8' pos=(0,0)
//
// Four entries with the same name for one doorway, and Aaron pressed drive on
// the fourth four times:
//
//     [drive] REFUSED -- target validation failed: catIdx=8/11 entityIdx=-317
//
// **THE FIRST THREE ARE PARTY MEMBERS.** crtower2's MAPJUMP to field 283 lives
// in every character's own script -- squall, zell, irvine, rinoa, selphie and
// quistis each carry an identical copy at (1451,-383), because that is the code
// that moves whichever of them is walking. The three in the active party are
// filtered as party members; the three who are not in it are not, so an ABSENT
// character contributes a phantom exit. A character's script is not a doorway,
// whether or not that character happens to be along.
//
// **THE FOURTH HAS NO POSITION.** director0's MAPJUMP is real code and it is
// nowhere: pos=(0,0), no walkmesh triangle. The drive refuses it, correctly, so
// listing it offers the player something that cannot work -- and for a blind
// player an entry that refuses is worse than one that is absent, because the
// only way to discover it is broken is to try it.
//
// Disc-wide, across all 900 fields: 214 party-member exit entities on 124
// fields, and 465 with no position on 361 fields. crtower2 is not a special
// case; it is the one Aaron happened to stand in.
//
// NEITHER RULE CAN HIDE A REAL EXIT. A doorway the player can walk to has a
// position, and it is not stored inside a playable character.

// A script-derived exit needs somewhere to be. posTriangle == 0 is the engine's
// "not on the walkmesh" value, which is exactly what the drive's own target
// validation rejects.
static bool JsmExitHasPlacement(int posTriangle, int posX, int posY)
{
    if (posTriangle != 0) return true;
    return !(posX == 0 && posY == 0);
}

// isPartySym comes from IsPartyCharacterSym() in field_nav_helpers.inl -- the
// same list the catalog already trusts to filter party members out.
static bool JsmExitShouldSurface(bool isPartySym, int posTriangle, int posX, int posY)
{
    if (isPartySym) return false;
    return JsmExitHasPlacement(posTriangle, posX, posY);
}
