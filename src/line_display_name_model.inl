// line_display_name_model.inl -- NAMING A TRIGGER LINE FROM ITS SYMBOL
//
// v0.113.0 (#dsrc). Aaron, on Level 3 of the Deep Sea Research Center: "Got to
// floor 3 in the DSRC but couldn't seem to find the control panel to open the
// steam room."
//
// It was in the catalog. Both terminals on that floor were, and had been all
// along -- his 2026-08-27 log lists them as
//
//     [refresh]  cat1 TRIGGER line0 center=(2273,95)  name='Interaction 1'
//     [refresh]  cat2 TRIGGER line1 center=(1090,-166) name='Interaction 2'
//
// and the GPS shows him walking to Interaction 1 and stopping there. Nothing
// told him the other one was the steam room control, because a trigger line is
// named "Interaction N" and nothing else.
//
// v0.112.0 added "Terminal", "Steam Room Terminal" and "Level Terminal" to the
// two naming tables and they had no effect whatsoever, because the tables were
// never consulted for a LINE. InjectInteractionLines knew exactly two special
// cases, both hard-coded in place: `Cliant` -> "Desk" and IsSignpostName() ->
// "Notice Board". Every other symbol fell through to the number.
//
// So: ask the tables. The field-scoped one first, because it exists precisely
// to say that one symbol means different things in different rooms; then the
// SYM table, which is what "friendly display name for TTS" was always for.
//
// HOW MUCH THIS MOVES, counted rather than guessed. Across the 866 fields in
// tests/jsm_scan_golden.txt there are exactly **18 interactive lines whose
// symbol has a display name**, and every one of them reads better for it:
//
//     Tanma x4, Tanme x2, Tanme2, Tanmatu  -> Terminal
//     evl1 x2, Lift                        -> Elevator
//     ladder x2                            -> Ladder
//     betunikun, naidarokun                -> Student
//     door                                 -> Door
//     Kaisetu                              -> Information Panel
//     BossBattle                           -> Blue Light
//
// A line the tables have nothing to say about keeps its number, which is the
// overwhelming majority of them.

// The order is the whole rule: field+sym beats sym, and a field-scoped row that
// says "drop this" (a null display) is not a name and must not be treated as
// one -- the catalog's own drop path handles those, and reaching this function
// means the line already survived it.
static const char* LineDisplayName(const char* fieldScoped, const char* symScoped)
{
    if (fieldScoped != nullptr && fieldScoped[0] != '\0') return fieldScoped;
    if (symScoped   != nullptr && symScoped[0]   != '\0') return symScoped;
    return nullptr;
}

// A curated name -- from anywhere -- outranks the solo-interaction guess and
// stops the renumbering pass from turning it back into "Interaction N". The
// two callers that already worked this way (`Cliant`, the signposts) keep
// behaving exactly as they did; this just says so in one place.
static bool LineNameIsCurated(const char* name)
{
    return name != nullptr && name[0] != '\0';
}
