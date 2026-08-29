// wm_distance_pure.inl -- distance as the player hears it.
//
// A statement-free header fragment #included by the world-map announcers (the
// real build) and by tests/wm_distance_test.cpp.
//
// Aaron: "'Units' doesn't really mean anything to a player. Let's ensure that
// all auto-drive and navigation functionality references 'km' instead of units.
// e.g. when auto-drive says to land it should say the distance in km instead of
// units. This is also consistent with the catalog which uses km."
//
// He is right, and the codebase already half-agreed with him. The catalog has
// said "Esthar City. 25 kilometers away." since the beginning, and v0.20.88 made
// the Garden switch to kilometres past five of them for exactly this reason --
// its changelog: "one hundred and twenty-two hundred units" is not a distance
// anyone can hold in their head. What it never did was finish the job: below
// five kilometres, five separate announcers went on saying "9 hundred units",
// which is the same complaint at a smaller scale. A player has no idea how far a
// unit is, and no reason to learn.
//
// ONE CONVENTION, ONE PLACE, so the five cannot drift apart again. 1000 units to
// the kilometre, the divisor world_map_announce.inl has used for the catalog all
// along, and "kilometers" the spelling it uses -- consistency with the string he
// hears most often beats consistency with a dictionary.
static const double WM_UNITS_PER_KM = 1000.0;

// One decimal, because a walk is usually under two kilometres and "1 kilometer"
// for anything from 500 to 1499 units is worse than the units it replaces.
// Below a tenth of a kilometre there is no number worth saying.
static void WmSayDistance(double units, char* out, size_t n)
{
    if (out == nullptr || n == 0) return;
    // No clamp for negatives: any negative distance is below the floor below and
    // comes out as "less than a tenth", so a minus sign can never reach him. A
    // clamp here would be a second guard for a case the first already covers, and
    // an untested one -- the mutant that removed it could not be killed.
    const double km = units / WM_UNITS_PER_KM;
    if (km < 0.1) snprintf(out, n, "less than a tenth of a kilometer");
    else          snprintf(out, n, "%.1f kilometers", km);
    out[n - 1] = '\0';
}
