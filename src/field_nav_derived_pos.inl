// field_nav_derived_pos.inl -- POSITIONS THE FIELD FILES DO NOT CARRY BUT THE
// FIELD'S OWN SCRIPT DOES (#derived-pos).
//
// Included from field_navigation.cpp before field_nav_fieldscripts.inl, and
// compiled standalone by tests/nav_derived_pos_test.cpp. Nothing here touches
// game memory.
//
// ============================================================================
// WHY THIS EXISTS
// ============================================================================
//
// Aaron, on the Deep Sea Research Center: *"There isn't anything in the catalog
// to direct the player to the light that they need to approach."*
//
// He is right, and the reason is that the light has no coordinate anywhere the
// mod normally looks. `sdcore1`'s scan reads:
//
//     ent1 cat=1 type=Interactive Line sym='BossBattle' pos=no(0,0,0 tri=0)
//
// `BossBattle` is the trigger line that starts the whole Bahamut scene, and it
// is the thing the player has to walk onto. Its geometry is in none of the
// three places a line's geometry can be: there is no `SETLINE` (0x039) anywhere
// in the field, all twelve `.inf` trigger zones are `0xFF` (empty), and it is
// not a gateway. So the catalog has nothing to steer to and says so.
//
// **THE SCRIPT KNOWS WHERE THE LIGHT IS, BECAUSE IT TURNS THE PARTY TO LOOK AT
// IT.** Every party member has a `hanno` method -- 反応, "reaction", the beat in
// the arrival cutscene where they notice the light -- and all five are the same
// three pushes:
//
//     Zell::hanno      PSHN_L 250 NEG ; PSHN_L 1161 NEG ; PSHN_L 550 ; OP_0x053
//     Selphie::hanno   ... identical ...
//     Quistis::hanno   ... identical ...
//     Rinoa::hanno     ... identical ...
//     Irvine::hanno    ... identical ...
//
// Five entities independently told to face **(-250, -1161, 550)**. That is not
// a coordinate anyone guessed: it is the game's own answer to "where is the
// thing they are staring at", and z=550 is the light hanging above the floor.
//
// And it is WALKABLE. Parsing `sdcore1.id` (284 triangles): (-250, -1161) falls
// inside triangle 232, at the far east end of a corridor whose west end holds
// the party's arrival point (-2646, -1200, triangle 149). Two and a half
// thousand units of corridor with the light at the end of it -- which is exactly
// the room Aaron has been walking.
//
// ============================================================================
// THE SHAPE OF THE FIX, AND WHY IT IS THIS SHAPE
// ============================================================================
//
// v0.12.17 already solved this class of problem once, for entities whose real
// interaction position lives in the `.inf` rather than in a `SET3`: the
// `[INF-TRIG-POS]` pass writes `posX/posY`, sets `hasPosition` and sets
// `hasNearbyInteractionZone` so the entity survives the junk-gate. This table
// feeds the SAME three fields through the SAME pass, for entities whose
// position is derivable from the script but is not in any file the mod parses.
//
// It is a table and not a special case in the Bahamut module on purpose: the
// module owns the puzzle, and where a thing IS belongs to the navigation layer
// that steers to it. The name comes from `ENTITY_DISPLAY_NAMES` the same way
// every other curated entity's does.
//
// A row is only ever added with the derivation written down. "The script turns
// five characters to face this point" is a derivation. Eyeballing a walkmesh is
// not.
// ============================================================================

struct NavDerivedPos {
    uint16_t    fieldId;   // the field this applies to -- NEVER apply by name alone
    const char* sym;       // the .sym entity name, matched case-insensitively
    int16_t     x;
    int16_t     y;
};

static const NavDerivedPos NAV_DERIVED_POS[] = {
    // sdcore1 (846), the Deep Sea Research Center corridor. The blue light, from
    // the five identical `hanno` turn-to-face calls. Walkmesh triangle 232.
    // Displayed as "Blue Light" via ENTITY_DISPLAY_NAMES.
    { 846, "BossBattle", -250, -1161 },

    // tmsand1 (945), Shumi Village's Desert Village. `Search` is the SHADOW
    // STONE, one of the five the Sculptor sends you for, and it is the only one
    // of the five with no position anywhere the mod looks: it is not a line, it
    // has no SET3, and its .inf trigger slots are empty. `[JSMScan]` reads it as
    //
    //     ent9 cat=3 type=Unknown sym='Search' pos=no(0,0,0 tri=0) btn=00C0
    //
    // so the catalog has never had anything to offer, and a blind player has no
    // way at all to find it. The field's dialogue is unambiguous about what is
    // there -- msg 3 "More stones..." and msg 4 "Looks like the shadow stone."
    //
    // **THE SCRIPT STATES THE PLACE AS A BOUNDING BOX.** Search::method1 is a
    // polling loop: it loads the player's position (op 0x070), then gates on
    // four comparisons before it will accept the button press at 0x06D:
    //
    //     op_008 axis0 ; PSHN  581 ; JMP >    -- X >  581
    //     op_008 axis0 ; PSHN  767 ; JMP <    -- X <  767
    //     op_008 axis1 ; PSHN 1412 ; NEG ; < -- Y < -1412
    //     op_008 axis1 ; PSHN 1557 ; NEG ; > -- Y > -1557
    //
    // That rectangle is the game's own definition of "standing on the shadow
    // stone", and its centre is (674, -1485). Parsing tmsand1.id (69 triangles)
    // puts that point inside **triangle 42**, whose centroid is (700, -1478),
    // 27 units away -- so it is not merely walkable, it is the middle of a
    // triangle rather than an edge case.
    //
    // Only ONE spot in all thirteen Shumi fields works this way; the other four
    // stones are trigger lines with real SETLINE geometry, and they reach the
    // catalog on their own once the quest opens them.
    { 945, "Search", 674, -1485 },
};

static const int NAV_DERIVED_POS_COUNT =
    (int)(sizeof(NAV_DERIVED_POS) / sizeof(NAV_DERIVED_POS[0]));

// Case-insensitive ASCII compare, so the lookup is a pure function with no
// dependency on the CRT's locale behaviour (and so the probe compiles alone).
static bool NavDerivedSymEq(const char* a, const char* b)
{
    if (a == nullptr || b == nullptr) return false;
    while (*a && *b) {
        char ca = *a++, cb = *b++;
        if (ca >= 'A' && ca <= 'Z') ca = (char)(ca - 'A' + 'a');
        if (cb >= 'A' && cb <= 'Z') cb = (char)(cb - 'A' + 'a');
        if (ca != cb) return false;
    }
    return *a == '\0' && *b == '\0';
}

// Returns the row for this (field, entity) pair, or nullptr. The field id is
// part of the key and not a convenience: `BossBattle` is a plausible symbol name
// in any field with a boss in it, and a coordinate from the wrong room would put
// a catalog entry somewhere the player cannot walk.
static const NavDerivedPos* NavDerivedPosFor(uint16_t fieldId, const char* sym)
{
    if (sym == nullptr || sym[0] == '\0') return nullptr;
    for (int i = 0; i < NAV_DERIVED_POS_COUNT; i++) {
        if (NAV_DERIVED_POS[i].fieldId != fieldId) continue;
        if (NavDerivedSymEq(NAV_DERIVED_POS[i].sym, sym)) return &NAV_DERIVED_POS[i];
    }
    return nullptr;
}

// A derived position NEVER overwrites one the field's own data already carries.
// `SET3` and the `.inf` are the engine's own numbers; this table is a derivation
// standing in for numbers that are missing, and the day a field starts carrying
// them the file wins.
static bool NavDerivedShouldApply(bool entityAlreadyHasPosition,
                                  const NavDerivedPos* row)
{
    return row != nullptr && !entityAlreadyHasPosition;
}
