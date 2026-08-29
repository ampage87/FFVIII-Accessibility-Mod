// wm_distance_test.cpp -- distance as the player hears it, in one convention.
#include <cstdio>
#include <cstring>
#include "wm_distance_pure.inl"

static int bad = 0;
static void chk(bool ok, const char* w) { if (!ok) { printf("  BAD: %s\n", w); bad++; } }
static bool has(const char* h, const char* n) { return strstr(h, n) != nullptr; }

int main()
{
    printf("wm_distance_test\n");
    char b[64];

    // The walks from the 19:53 global tour, in the words he will now hear.
    WmSayDistance(916.0, b, sizeof b);
    chk(has(b, "0.9 kilometers") && !has(b, "unit"),
        "**the Shumi Village walk is still spoken in units** -- Aaron: \"'Units' "
        "doesn't really mean anything to a player\"");
    WmSayDistance(1201.0, b, sizeof b);
    chk(has(b, "1.2 kilometers"), "Edea's House reads wrong");
    WmSayDistance(641.0, b, sizeof b);
    chk(has(b, "0.6 kilometers"), "Deling City reads wrong");

    // One decimal, because whole kilometres are worse than the units they
    // replace at exactly the range a walk lives in.
    WmSayDistance(500.0, b, sizeof b);
    chk(has(b, "0.5 kilometers"),
        "**half a kilometre rounds to a whole one** -- \"1 kilometer\" for anything "
        "from 500 to 1499 units tells him less than the units did");
    WmSayDistance(1499.0, b, sizeof b);
    chk(has(b, "1.5 kilometers"), "1499 units reads wrong");

    // Below a tenth there is no number worth saying.
    WmSayDistance(42.0, b, sizeof b);
    chk(has(b, "less than") && !has(b, "0.0"),
        "**a 42-unit walk is announced as 0.0 kilometers** -- that is a number that "
        "says nothing");
    WmSayDistance(0.0, b, sizeof b);
    chk(has(b, "less than"), "zero reads wrong");

    // The catalog's own spelling, because it is the string he hears most often.
    WmSayDistance(25000.0, b, sizeof b);
    chk(has(b, "kilometers") && !has(b, "kilometres"),
        "**two spellings of the same word** -- the catalog has said \"25 kilometers "
        "away\" from the beginning and consistency with that beats a dictionary");

    // Defensive: a negative distance must not print a minus sign at a player.
    WmSayDistance(-5.0, b, sizeof b);
    chk(!has(b, "-"), "a negative distance is spoken with a minus sign");

    // Null-safe.
    WmSayDistance(100.0, nullptr, 0);
    chk(true, "unreachable");

    printf("wm_distance_test: %s (%d bad)\n", bad ? "FAILED" : "OK", bad);
    return bad ? 1 : 0;
}
