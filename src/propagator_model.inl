// propagator_model.inl -- the PURE model of the Ragnarok Propagator puzzle
// (field set `rg*`, disc 3).
//
// PART OF field_navigation.cpp -- TEXTUAL INCLUDE. Do NOT compile standalone.
// Compiled standalone by tests/propagator_compile.cpp.
//
// ============================================================================
// THE PUZZLE, AND WHAT A BLIND PLAYER CANNOT SEE (#112)
// ============================================================================
//
// `rgguest2` msg 2 states the rule in the game's own words: *"So basically, we
// have to kill them in pairs that have the same colors, right?"* Eight
// Propagators, four colours, two of each. Kill two of a colour in a row and
// both stay dead; kill one of a colour and then one of another, and the first
// comes back.
//
// Aaron: *"There is also the question of how the blind player will know which
// is which color."* The colour is the only cue the game gives, and it is
// purely visual.
//
// ============================================================================
// THE PAIRING IS IN THE BYTECODE, AND THAT IS WHAT ACTUALLY SOLVES IT
// ============================================================================
//
// Three field variables run the whole rule:
//
//     var[445] 0x01CFEB75  bit 0x04 = a kill is pending (unmatched)
//     var[446] 0x01CFEB76  eight death bits, one per Propagator
//     var[447] 0x01CFEB77  the pending kill's own bit
//
// Each Propagator's `alien0N::default` tests `var[446] & OWN` to decide whether
// it still exists, and after winning its battle tests `var[447] & PARTNER` --
// "was the last unmatched kill my partner?". If not, `var[446] &= ~var[447]`
// revives that one. So the pairing is a fixed bit-partner table, read straight
// out of eight scripts:
//
//     rgair1   0x01 <-> 0x80  rgguest2      BATTLE 819 / 816
//     rgroad1  0x02 <-> 0x20  rghang2       BATTLE 815 / 815
//     rgroad2  0x04 <-> 0x08  rgroad3       BATTLE  85 /  85
//     rghang1  0x10 <-> 0x40  rgexit1       BATTLE 814 / 817
//
// **The mod announces the partner's location as well as the colour**, and that
// is deliberate: the partner is proven from bytecode, the colour is a label on
// top of it. If a colour name were ever wrong the puzzle would still be
// solvable from what the mod says.
//
// THE COLOURS come from the battle scenes: `0x069 BATTLE(enc, flags)` gives an
// encounter id, `scene.out` (battle.fs entry 64) gives its enemy slots at
// record offset 0x38 as monsterId + 0x10, and the four Propagator monsters'
// palettes peak at 89 = (96,24,240) purple, 97 = (152,240,24) green,
// 98 = (240,152,24) amber/yellow, 111 = (248,16,16) red. Two of those are
// corroborated by an outside source without being taken from it: a published
// walkthrough puts the yellow pair at the airlock and the passenger cabin,
// which is exactly `rgair1` and `rgguest2` -- the pair the bit table gives.
// The field models agree too: each field's alien has five texture pages whose
// palettes are byte-identical within a pair.
//
// A TRAP WORTH NAMING: `rgroad3` has a NINTH alien, entity `alien01`, that is
// cutscene-only and can never be fought. The fightable one there is `alien02`.
// A catalog that announced both would send the player hunting a phantom.
//
// THE PUZZLE ENDS when `var[446] == 255`, tested in exactly one place --
// `rgroad1 :: lift::talk` dwords 102-105 -- which then opens the lift. The game
// also has its OWN mercy rule: `var[437] > 24` forces `var[446] = 255`, so 25
// battles of any kind solve it. That is the only game-provided skip and the mod
// does not need to invent one.

static const int      PG_VAR_PENDFLAG = 445;          // bit 0x04 = a kill is pending
static const int      PG_VAR_DEAD     = 446;          // eight death bits
static const int      PG_VAR_PENDBIT  = 447;          // which one is pending
static const uint32_t PG_ADDR_PENDFLAG = 0x01CFEB75u;
static const uint32_t PG_ADDR_DEAD     = 0x01CFEB76u;
static const uint32_t PG_ADDR_PENDBIT  = 0x01CFEB77u;
static const uint8_t  PG_PENDING_MASK  = 0x04;
static const uint8_t  PG_ALL_DEAD      = 0xFF;

struct Propagator {
    const char* field;        // internal field name
    const char* entity;       // the .sym entity that IS the monster
    uint8_t     bit;          // its bit in var[446]
    uint8_t     partnerBit;   // the bit its script accepts as a match
    const char* colour;       // spoken colour
    const char* place;        // spoken location, for naming the partner
    int         slot;         // its script-object slot, from the JSM. FALLBACK ONLY.
    int         model;        // its SETMODEL parameter -- the live block carries
                              // the same number at +0x218, and THAT is the join
                              // that survives the engine's entity compaction.
    bool        freezable;    // pin its MOVEMENT to zero? see PG_FREEZE
    bool        gate;         // require Confirm before it may touch him? see PG_GATE
};

// WHY freezable IS NOT SIMPLY TRUE (#112)
// --------------------------------------
// Aaron: *"We also need to prevent the Propagator from moving when the player
// and the Propagator are on the same field, otherwise the player is going to
// get caught while using navigation tools in the mod."* Right, and the freeze
// is real (see PG_OFF_SPEED_* below) -- but it must not be applied blindly:
//
//   * rgair1 and rgroad1 do not move AT ALL. Their scripts contain no move
//     opcode of any kind; they stand where SET3 put them. There is nothing to
//     hold still, so holding them is a write with no purpose.
//
//   * rgguest2 is not a patrol or a chase, it is a CUTSCENE. Its script runs
//     straight through -- fade, scripted approach, battle 816, the pair
//     bookkeeping -- and ends in MAPJUMP3. Freezing it would block that script
//     at its scripted move and strand the player in a room whose only exit is
//     the jump the script never reaches. The battle there is unavoidable by
//     design; the mod has no business making it unreachable instead.
//
// WHY gate IS A SEPARATE COLUMN, AND WHY IT IS NOT freezable (#112, v0.74.0)
// -------------------------------------------------------------------------
// Aaron, 12:30 BAT: *"Found another one that activated without me pressing X."*
// The battle was on rgroad1 -- the green one in the lift corridor, one of the
// two that do not move.
//
// FREEZING AND CONSENT ARE DIFFERENT PROBLEMS AND THIS TABLE HAD ONE COLUMN FOR
// BOTH. Not moving does not make a Propagator harmless: rgroad1 and rgair1 run
// the same three instructions the movers do --
//
//     0x013 0        ; target the party leader
//     0x05B          ; ISTOUCHING -> local 0
//     PSHL 0 / JMPZ  ; ...and two later, BATTLE 815 / 819
//
// -- sixty times a second, standing still, waiting for him to walk into them.
// The mod took "nothing to freeze" to mean "nothing to do", so a blind player
// crossing that corridor under navigation was grabbed exactly as before. A
// sighted player walks around them; that is the whole thing being restored.
//
// So `freezable` now means only "pin its movement", and `gate` means "refuse
// its contact test until he presses Confirm". Every Propagator he is meant to
// choose to fight is gated. Freezing is the extra thing five of them need.
//
// rgguest2 WAS the one that was gated by neither, and v0.75.0 gates it. Aaron:
// *"Let's try to freeze the one in the passenger compartment as well. I know
// that one is a bit different but it is jarring the way it works right now."*
//
// It is still not frozen -- freezable stays false, and for a better reason than
// the one v0.69.0 gave. Its script's wait after the charge is an eight-FRAME
// count, not a wait for the move to finish, so pinning the speed would stop the
// monster walking and the battle would arrive a fifth of a second later
// regardless. And it is still not gated by the contact veto: its script has no
// ISTOUCHING at all, because nothing about that fight depends on touching him.
//
// It is gated at the cutscene's FIRST INSTRUCTION instead, which is a different
// mechanism living in field_disc3_propagator.inl -- see PgHookedEvent. What the
// gate column means here is the part the model owns: this Propagator takes part
// in the reach cue and the Confirm release like every other one.

// One row per fightable Propagator. Ordered by bit so the table reads as the
// four pairs it is.
// TWO COLUMNS FOR "WHICH ENTITY", AND ONLY ONE OF THEM IS TRUSTWORTHY.
//
// `slot` is the entity's index in the field's Others group, read out of the
// field's own JSM (fieldsim `group_to_slot`). It was the primary key until the
// 11:52 BAT, and rgroad3 is where it broke. Aaron: "the Propagator in Aisle 6
// still attacked without pressing confirm."
//
// THE LIVE ARRAY IS COMPACTED. It holds the entities the scene actually
// instantiated, so a script object with no model takes no place in it. Seven of
// these eight fields put the model-less `dic` AFTER the Propagator, so slot and
// live index happen to agree. rgroad3 puts `dic` at slot 2, BEFORE alien01 and
// alien02 -- so alien02's script slot is 4 while its live index is 3, and every
// hold in that room was pinning `dp01`, the draw point, to zero.
//
// The log said so plainly once the right question was asked of it: on rgroad3
// the mod reported "speed reads 0/0" -- true, and meaningless, because it was
// reading back its own write to a static object -- while the veto, which only
// fires on the real entity's ctx, reported 0 times in a room where every other
// Propagator field reported hundreds. A hold that is losing a race still fires.
//
// `model` is the SETMODEL parameter, which the live block carries at +0x218.
// The entity's own script assigns it, so compaction cannot disturb it, and it
// is the same key the catalog's live/script join already trusts. It is the
// primary key now; slot is the last resort, and PgResolveLive logs which one
// answered so a wrong row shows up in the next BAT rather than silently pinning
// the wrong entity for a whole scene.
static const Propagator PG_LIST[] = {
    { "rgair1",   "alien01", 0x01, 0x80, "yellow", "the airlock",         2, 2, false, true  },
    { "rgroad1",  "alien01", 0x02, 0x20, "green",  "the lift corridor",   3, 3, false, true  },
    { "rgroad2",  "alien01", 0x04, 0x08, "red",    "the middle corridor", 2, 2, true , true  },
    { "rgroad3",  "alien02", 0x08, 0x04, "red",    "the far corridor",    4, 3, true , true  },
    { "rghang1",  "alien01", 0x10, 0x40, "purple", "the hangar",          2, 2, true , true  },
    { "rghang2",  "alien01", 0x20, 0x02, "green",  "the far hangar",      9, 7, true , true  },
    { "rgexit1",  "alien01", 0x40, 0x10, "purple", "the exit passage",    2, 2, true , true  },
    { "rgguest2", "alien01", 0x80, 0x01, "yellow", "the passenger cabin", 2, 2, false, true  },
};
static const int PG_COUNT = (int)(sizeof(PG_LIST) / sizeof(PG_LIST[0]));

// The cutscene-only decoy in rgroad3 -- never fightable, never announced.
static const char* PG_DECOY_FIELD  = "rgroad3";
static const char* PG_DECOY_ENTITY = "alien01";

static const Propagator* PgForField(const char* field)
{
    if (!field) return nullptr;
    for (int i = 0; i < PG_COUNT; i++)
        if (_stricmp(PG_LIST[i].field, field) == 0) return &PG_LIST[i];
    return nullptr;
}
static const Propagator* PgForBit(uint8_t bit)
{
    for (int i = 0; i < PG_COUNT; i++)
        if (PG_LIST[i].bit == bit) return &PG_LIST[i];
    return nullptr;
}
static bool PgIsRagnarokField(const char* field)
{
    return field && (field[0] == 'r' || field[0] == 'R') &&
                    (field[1] == 'g' || field[1] == 'G');
}
// Every table row's partner must itself point back, and both must agree on the
// colour. Cheap enough to assert at runtime as well as in the probe.
static bool PgTableConsistent()
{
    uint8_t seen = 0;
    for (int i = 0; i < PG_COUNT; i++) {
        const Propagator* p = &PG_LIST[i];
        if (p->bit == 0 || (seen & p->bit)) return false;
        seen = (uint8_t)(seen | p->bit);
        const Propagator* q = PgForBit(p->partnerBit);
        if (!q || q->partnerBit != p->bit) return false;
        if (_stricmp(q->colour, p->colour) != 0) return false;
    }
    return seen == PG_ALL_DEAD;
}

// ============================================================================
// THE CATALOG NAME (#112)
// ============================================================================
//
// Aaron: *"we want the NPC in the catalog to say 'Red Propagator', 'Purple
// Propagator', etc."* That replaces every announcement this module used to
// make on walking into a room. It is also the right place for it: the colour is
// an attribute of the thing standing there, and the catalog is where the mod
// says what is standing there. The player finds them by looking, not by being
// told.
//
// One source of truth: the colours live in PG_LIST and nowhere else, so the
// name the catalog speaks and the name the pair logic reasons about cannot
// drift apart.
static bool PgCatalogName(const char* field, const char* sym, char* out, size_t n)
{
    if (!field || !sym || !out || n < 8) return false;
    const Propagator* p = PgForField(field);
    if (!p) return false;
    if (_stricmp(p->entity, sym) != 0) return false;
    char c0 = p->colour[0];
    if (c0 >= 'a' && c0 <= 'z') c0 = (char)(c0 - 'a' + 'A');
    snprintf(out, n, "%c%s Propagator", c0, p->colour + 1);
    return true;
}

// IS THIS ENTITY ONE OF THE EIGHT? Asked by the catalog's map-exit injector,
// which needs to know that the MAPJUMP in rgguest2's alien01 script is the tail
// of a forced battle rather than a doorway. See the v0.76.0 note there.
static bool PgIsPropagator(const char* field, const char* sym)
{
    if (!field || !sym) return false;
    const Propagator* p = PgForField(field);
    return p && _stricmp(p->entity, sym) == 0;
}

// THE THING THE ROOM IS FOR (#112, v0.76.0)
// ----------------------------------------
// Aaron: *"in the passenger compartment is the terminal you are supposed to
// interact with to hear the briefing on the Propagators, but the terminal is not
// appearing in the catalog."* v0.75.0 got it INTO the catalog -- the log shows
// it kept, every refresh -- and it was announced as "NPC", which is a name for
// nothing. A blind player hunting for a terminal hears "NPC" and walks past it;
// a sighted one sees a console on the wall and knows what it is at a glance.
//
// This table is the parity. It is field-scoped for the same reason the
// Propagator colours are: `comp` is a symbol, not a meaning, and what it means
// is decided by the room it is standing in.
struct PgNamedObject { const char* field; const char* entity; const char* name; };
static const PgNamedObject PG_OBJECTS[] = {
    { "rgguest2", "comp", "Terminal" },
};
static const int PG_OBJECT_COUNT = (int)(sizeof(PG_OBJECTS) / sizeof(PG_OBJECTS[0]));

static const char* PgObjectName(const char* field, const char* sym)
{
    if (!field || !sym) return nullptr;
    for (int i = 0; i < PG_OBJECT_COUNT; i++)
        if (_stricmp(PG_OBJECTS[i].field, field) == 0 &&
            _stricmp(PG_OBJECTS[i].entity, sym) == 0) return PG_OBJECTS[i].name;
    return nullptr;
}

// THE NINTH ALIEN. rgroad3 carries a second `alien01` that is cutscene-only:
// its default method is three words long -- label, one call, return -- and it
// can never be fought. A catalog that listed it would send the player across
// the ship to a Propagator that is not there.
static bool PgCatalogDrop(const char* field, const char* sym)
{
    if (!field || !sym) return false;
    if (_stricmp(field, PG_DECOY_FIELD) != 0) return false;
    if (_stricmp(sym, PG_DECOY_ENTITY) != 0) return false;
    // ...but only in the field where the fightable one is the OTHER entity.
    const Propagator* p = PgForField(field);
    return p && _stricmp(p->entity, sym) != 0;
}

// ============================================================================
// HOLDING THEM STILL (#112)
// ============================================================================
//
// The mechanism is the engine's own per-entity move speed. From the field
// update pass at 0x004790E7:
//
//     0x004790F1  cmp byte [ent+0x23C], 1     ; is this entity moving?
//     0x00479130  call 0x479C60               ; ...then walk one step
//     0x00479F5F  movsx eax, word [ent+0x1FE] ; and the step is
//     0x00479FA3  add  edi, edx               ;   unit(facing) * speed >> 8
//
// Write 0 into [ent+0x1FE] and the step vector is zero: the entity does not
// translate. The arrival test at 0x004799FF compares the remaining distance
// against speed squared, so it cannot fire spuriously either -- the move never
// completes, the script blocks on its move opcode at 0x0052359F, and therefore
// never reaches the contact test or the BATTLE after it. Nothing is lied to:
// +0x23C stays 1, the walkmesh triangle at +0x1FA stays valid, facing and
// animation keep updating, and restoring the word resumes the move exactly
// where it stopped. [ent+0x200] is the persistent walk speed the script's own
// speed opcode (0x03D) writes and every move opcode copies into +0x1FE, so both
// are written -- that way it does not matter whether the mod's tick lands
// before or after the script's.
//
// WHY IT IS NOT A PERMANENT FREEZE. In four of these five fields the ONLY path
// to a battle is the Propagator's own charge, so a Propagator held still
// forever is a Propagator that can never be fought -- and the puzzle needs all
// eight killed to open the lift. The hold is therefore a BUBBLE: held while the
// player is far enough away that any movement would be an ambush, released once
// the player has deliberately walked into arm's reach. Hysteresis on the two
// radii, because a Propagator flickering between held and free on the boundary
// would be worse than either.
static const uint32_t PG_OFF_SPEED_CUR  = 0x1FE;   // speed of the move in flight
static const uint32_t PG_OFF_SPEED_WALK = 0x200;   // persistent walk speed
static const uint32_t PG_OFF_POS_X      = 0x190;   // 20.12 fixed point
static const uint32_t PG_OFF_POS_Y      = 0x194;
static const uint32_t PG_OFF_TRIANGLE   = 0x1FA;
// TWO STEPS, and the player's own key. Aaron, after the first run where one of
// them caught him at a doorway: *"could we make it so the monster is held in
// place until the player gets within 2 steps from the monster and presses X /
// the Confirm key? That would prevent the player from being chased at all and
// essentially let them walk up to the enemy to start the battle."*
//
// So the bubble is gone. A Propagator is held from the moment the mod can see
// it until the player is close enough to touch it AND says so. Nothing about
// where he stands releases it on its own, which is the whole point: the 23:04
// run had one waiting by the door he came through, and a distance rule cannot
// tell "he walked up to me" from "he walked in".
//
// The mod's own navigation calls about 255 units a step (the GPS logs
// "dist=255 steps=1"), so two steps is a shade over 500. 520 is generous enough
// that he does not have to stand on the thing and tight enough that Confirm
// pressed across the room does nothing.
static const long     PG_REACH_UNITS    = 520;     // "within two steps"

enum PgHold {
    PG_HOLD_NONE = 0,   // not our business -- leave the entity alone
    PG_HOLD_ON,         // refuse its contact test (and pin the speed, if freezable)
    PG_HOLD_OFF,        // let go: he has chosen to fight it
    PG_HOLD_DEAD,       // let go silently: it is dead, there is nobody to tell
};

// The whole policy, as arithmetic, so it can be checked without a Ragnarok.
//   dead       -- var[446] & this one's bit
//   posKnown   -- did we get both the player's position and the entity's?
//   dist       -- units between them, meaningless when !posKnown
//   holding    -- what we did last tick
//   confirmHit -- the Confirm key went down THIS tick (edge, not held)
//   engaged    -- he has already released this one; it is his fight now
//
// NOTE what is NOT here any more: distance alone can no longer release it, and
// nothing re-holds a Propagator once released. Both are deliberate. A monster
// that re-froze after being let go would be a monster that can never be fought,
// and a monster released by proximity is a monster that ambushes anyone who
// walks in through the wrong door.
static PgHold PgHoldDecide(const Propagator* p, uint8_t dead,
                           bool posKnown, long dist, bool holding,
                           bool confirmHit, bool engaged)
{
    // GATE, not freezable. A Propagator that cannot move can still grab him;
    // rgroad1 did, at 12:31:41, standing perfectly still the whole time.
    if (!p || !p->gate)      return PG_HOLD_NONE;
    if (engaged)             return PG_HOLD_NONE;   // his fight now; hands off
    // A DEAD ONE IS RELEASED WITHOUT A WORD. "Released." is the answer to a
    // key he pressed; a monster that just died did not ask him anything, and
    // saying it there is the mod narrating its own bookkeeping.
    if (dead & p->bit)       return holding ? PG_HOLD_DEAD : PG_HOLD_NONE;
    // HE HAS TO REACH IT AND SAY SO. Either half alone is not consent: standing
    // next to one is where the navigation leaves him, and Confirm is a key he
    // presses at scenery all day.
    if (holding && confirmHit && posKnown && dist <= PG_REACH_UNITS)
        return PG_HOLD_OFF;
    return PG_HOLD_ON;      // otherwise held, whether or not we can see it
}

// Is he close enough that Confirm would start the fight? Used for the one cue
// the module still volunteers -- see the comment on it.
static bool PgInReach(bool posKnown, long dist)
{
    return posKnown && dist <= PG_REACH_UNITS;
}

// What to say about the state of the hunt. `dead` is var[446], `pendBit` is
// var[447], `pending` is var[445] & 0x04.
// `withPlace` is the difference between answering a question and volunteering
// an answer. Aaron: *"We also don't want to proactively inform the player where
// to find it's pair."* So the location is spoken only when he asks for it with
// the help key; everything the mod says on its own says the RULE and stops.
static void PgStatusLine(uint8_t dead, uint8_t pendBit, bool pending,
                         char* out, size_t n, bool withPlace)
{
    if (dead == PG_ALL_DEAD) { snprintf(out, n, "All eight are down. The lift is open."); return; }
    int left = 0;
    for (int i = 0; i < PG_COUNT; i++) if (!(dead & PG_LIST[i].bit)) left++;
    if (pending) {
        const Propagator* p = PgForBit(pendBit);
        const Propagator* q = p ? PgForBit(p->partnerBit) : nullptr;
        if (p && q) {
            if (withPlace) {
                snprintf(out, n, "%d left. A %s one is down and unmatched -- kill the other %s "
                                 "one, in %s, next, or the first comes back.",
                         left, p->colour, q->colour, q->place);
            } else {
                snprintf(out, n, "%d left. A %s one is down and unmatched -- the next kill "
                                 "must be the other %s one, or the first comes back.",
                         left, p->colour, q->colour);
            }
            return;
        }
    }
    snprintf(out, n, "%d left. No kill is pending, so any one may be killed next.", left);
}

// What to say about the Propagator standing in this field.
static void PgHereLine(const Propagator* p, uint8_t dead, uint8_t pendBit, bool pending,
                       char* out, size_t n)
{
    if (!p) { snprintf(out, n, "No Propagator here."); return; }
    if (dead & p->bit) { snprintf(out, n, "The %s Propagator here is already down.", p->colour); return; }
    const Propagator* q = PgForBit(p->partnerBit);
    const bool partnerDown = q && (dead & q->bit);
    // MASK, not equality -- because that is what the script does:
    //     PSHM_B var[447] ; PSHN_L <partnerBit> ; OPER AND ; PSHN_L 0 ; OPER EQ
    // var[447] only ever holds a single bit today, so the two agree; matching
    // the game's own test costs nothing and cannot drift away from it.
    const bool matchesPending = pending && ((pendBit & p->partnerBit) != 0);
    if (matchesPending) {
        snprintf(out, n, "%s Propagator. This is the match for the pending kill -- "
                         "killing it now finishes the pair.", p->colour);
    } else if (pending) {
        const Propagator* pend = PgForBit(pendBit);
        snprintf(out, n, "%s Propagator. Do NOT kill it yet: a %s one is unmatched and "
                         "would come back. Its pair is in %s.",
                 p->colour, pend ? pend->colour : "another", q ? q->place : "another room");
    } else if (partnerDown) {
        snprintf(out, n, "%s Propagator. Its pair is already down and matched.", p->colour);
    } else {
        snprintf(out, n, "%s Propagator. Its pair is in %s.", p->colour, q ? q->place : "another room");
    }
}
