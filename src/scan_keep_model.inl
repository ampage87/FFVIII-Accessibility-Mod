// scan_keep_model.inl -- WHO SURVIVES THE RUNTIME ENTITY SCAN
//
// v0.132.1 (#shumi). Two decisions the catalog makes about every live entity in
// every field, both of which went wrong in the Shumi workshop in opposite
// directions: a person the player needed was dropped, and four fish he did not
// need were kept.
//
// ============================================================================
// THE ENGINE'S TALK AND PUSH FLAGS ARE INVERTED, AND THE SCAN READS THEM RAW
// ============================================================================
// From the opcode dispatch table at 0x00B8DE94:
//
//     0x057 TALKON   @ 0x0051EBB0   mov byte [eax+0x24B], 0
//     0x058 TALKOFF  @ 0x0051EBD0   mov byte [eax+0x24B], 1
//     0x059 PUSHON   @ 0x0051EBF0   mov byte [eax+0x249], 0
//     0x05A PUSHOFF  @ 0x0051EC10   mov byte [eax+0x249], 1
//     0x062 TALKRADIUS @ 0x0051EDD0 -> writes +0x1F8
//     0x063 PUSHRADIUS @ 0x0051EE00 -> writes +0x1F6
//
// **ZERO IS ON.** The scan tests `talkonoff > 0` for "talkable" and
// `pushonoff > 0` for "solid", and both are therefore backwards. The catalog
// knows this in one place -- there is a comment further down field_catalog.inl
// that says so in as many words -- and not in the place that decides who is
// kept.
//
// It has gone unnoticed because two other signals usually carry the answer: the
// static `hasTalkSetup` parsed out of the script, and a talk radius captured by
// the TALKRADIUS hook. This file does not flip the polarity. Flipping it would
// change the fate of entities in all 900 fields at once and that needs its own
// build and its own evidence. What it does is stop the inverted flag being the
// LAST word when a better signal is sitting in the same struct.
//
// ============================================================================
// THE SCULPTOR
// ============================================================================
// Aaron: "when I went to the workshop to talk to sculptor and continue the side
// quest, he was not shown in the catalog." His screenshot shows a Shumi in a
// green robe standing at the statue with his arm raised. tmkobo2's catalog held
// three entries: an exit, an unnamed "Interaction 1", and a draw point.
//
// The Shumi is `Shou`, live entity 4, and his script's first method is
//
//     SETMODEL 6 ; BASEANIME ; TALKRADIUS 130 ; PUSHOFF
//
// -- talk to me, walk through me. PUSHOFF writes 1 to +0x249, so `pushonoff > 0`
// was true, and with no talk radius credited to him he fell into the push-only
// branch and was discarded as "not interactable".
//
// **THE TALK RADIUS WAS CAPTURED AND FILED UNDER THE WRONG ENTITY.** The
// TALKRADIUS hook maps the executing entity's pointer back to an index by
// subtracting pFieldStateOthers and dividing by the stride, and in tmkobo2 that
// came out three slots high. The three Shumi set radii of 130 (Shou), 100
// (Otuki) and 100 (Tukurite) and are live entities 4, 5 and 6; the scan reported
// those exact three radii on entities 7, 8 and 9. Entity 9 is the Draw Point,
// which sets no talk radius at all and was credited with 100.
//
// So the fix is not to correct the hook's arithmetic -- it is to stop needing
// it. The radius lives at +0x1F8 in the entity's own block, and the scan is
// already holding that block. Read it there and it cannot be attributed to
// anybody else, whatever the hook does.

// A talk radius in the entity's own block is the strongest evidence there is
// that the player can talk to this thing: the script asked for one by name.
// `flagSaysTalk` is the raw +0x24B test the scan has always done, kept so this
// is a widening of the old answer and never a narrowing.
static bool ScanEntityIsTalkable(bool flagSaysTalk, bool jsmHasTalkSetup,
                                 unsigned liveTalkRadius, bool latchedTalkable,
                                 unsigned cachedTalkRadius)
{
    return flagSaysTalk || jsmHasTalkSetup || liveTalkRadius > 0 ||
           latchedTalkable || cachedTalkRadius > 0;
}

// The push-only drop discards a VISIBLE entity, which is the most expensive
// mistake this scan can make: a person standing in the room who is not in the
// catalog cannot be found by a blind player at all. It must never fire on
// something that asked for a talk radius.
static bool ScanDropAsPushOnly(bool pushFlagSet, int modelId, unsigned liveTalkRadius)
{
    if (!pushFlagSet) return false;
    if (modelId < 0) return false;        // invisible -- a different branch owns it
    if (modelId >= 10) return false;      // v0.07.97: generic character models are NPCs
    if (liveTalkRadius > 0) return false; // v0.132.1: it asked to be talked to
    return true;
}

// ============================================================================
// THE FISH
// ============================================================================
// Aaron: "I noticed multiple catalog entries titled 'fish' and don't know what
// these are. I tried interacting with one and nothing happened so I assume they
// are purely decorative."
//
// The Elder's pond holds four separate models -- Fish, Fish2, FishUp, FishUp2 --
// and between them their scripts contain no TALKRADIUS, no TALKON, no push
// radius and no message opcode. They are decoration, and v0.132.0 made them
// worse by naming all four "Fish" where two had previously been unnamed.
//
// ENTITY_SKIP_NAMES has held curated scenery since the 900-field survey, but it
// was only ever consulted for Background entities and JSM-injected objects. The
// fish arrive down the runtime NPC path, which never asked.
//
// WHY NOT A RULE INSTEAD OF A LIST. The obvious rule -- "placed, visible, and no
// interaction opcode of its own" -- describes **2640 entities across 621
// fields**, among them Shumi Village's own `Munba2`, `Munba3`, `Turi_jiji` and
// both save points, every one of which is real and is driven by a REQ from a
// line or a director. A rule that deletes the Master Fisherman to delete four
// fish is not a rule worth having.
//
// THE GUARDS ARE THE WHOLE DESIGN. IsBgControllerName() returns true for an
// EMPTY name, and on this path a failed model join leaves the symbol empty --
// in the Shumi workshop that is the entire cast. Calling it here unguarded
// would delete every entity whose name could not be resolved, which is the
// exact failure this build exists to fix. So: the name must be real, the entity
// must have no talk radius and no talk flag, and its type must be one that
// carries no navigational promise. Disc-wide 49 placed entities across 37 fields
// carry a skip-list name and the talk guard keeps the ones that turned out to be
// real -- bcmin2_1's `Urakata` reads as an NPC, gfcross2's as a Draw Point.
static bool ScanDropAsScenery(bool nameIsReal, bool nameIsCuratedScenery,
                              bool talkable, unsigned liveTalkRadius,
                              bool typeCarriesNoPromise)
{
    if (!nameIsReal) return false;              // an unresolved name proves nothing
    if (!nameIsCuratedScenery) return false;
    if (talkable) return false;
    if (liveTalkRadius > 0) return false;
    return typeCarriesNoPromise;
}
