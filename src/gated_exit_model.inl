// gated_exit_model.inl -- AN EXIT THAT CANNOT HAPPEN YET IS NOT AN EXIT
//
// v0.132.0 (#shumi). Aaron, on his first run at the Shumi Village side quest:
// "one exit was labeled 'Elevator' when it was the exit back to Residence 2."
//
// TWO SEPARATE THINGS WENT WRONG IN ONE ROOM AND HE MET THEM AS ONE SENTENCE.
// tmkobo2 -- the Shumi workshop, which the field-name table calls Residence 3 --
// put exactly one exit in his catalog: `Exit to Shumi Village - Elevator`. He
// drove to it twice. The log says what happened both times:
//
//     12:32:15  [drive] stopped: Arrived.        <- no field change, nothing
//     12:32:34  [drive] started ... [drive] stopped: Arrived.   <- same second
//
// He then picked the only other thing in the list, `Camera transition`, and that
// one WAS the way back to Residence 2 (camera_transition_model.inl covers that
// half). So the entry named for a destination did nothing, and the entry named
// for nothing was the destination.
//
// WHAT THE "ELEVATOR" ENTRY ACTUALLY IS. tmkobo2 entity 16 is `Munbamini` -- a
// mini Moomba, standing on triangle 54 at (-68,1055) next to `Munba` on the same
// triangle. Its talk method really does end in `MAPJUMPO (936, 0)`, and 936
// really is the elevator, so the scanner was not hallucinating. But the jump is
// buried behind three chained story gates:
//
//     JMP (607, 32) ; JMPB 15      <- skip 15 words unless var[607] == 32
//     JMP (607, 64) ; JMPB 15
//     JMP (623, 16) ; JMPB 79      <- skip 79 words unless var[623] == 16
//     ... 70-odd words of cutscene ...
//     MAPJUMPO (936, 0)
//
// This is the Moomba who walks you out of the village once the sculpture is
// finished. Before then, talking to him does nothing and walking onto him does
// nothing -- which is precisely what Aaron's two drives found.
//
// THE MOD ALREADY KNEW HOW TO SEE THIS AND LOOKED IN ONE PLACE ONLY. v0.62.3
// added exactly this check, for exactly this reason (sspod2's pod, offered as
// "Exit to Desert 1" before the story reached var[256] == 2556). But it decodes
// the guard at the METHOD'S FIRST WORD and asks whether the MAPJUMP falls inside
// that one guard's skip. A method whose exit is behind its third guard reads as
// ungated, because the first guard's skip of 15 words ends long before the jump.
// One gate or a chain of them is a scripting-style difference, not a semantic
// one, and the player cannot tell them apart.
//
// SO THE SEARCH FOLLOWS THE CHAIN. See the next block for why it is a chain walk
// and not a scan -- the difference between the two is 21 entities and 217.
//
// WHY NOT SIMPLY "ANY BRANCH BEFORE A MAPJUMP MEANS GATED". Because that is true
// of most real exits too -- a door checks the disc, the party, whether a cutscene
// is running. The 5-word form this decodes is specifically a savemap-variable
// comparison feeding a backward-skip, which is how FF8 writes "the story has not
// got here yet", and the catalog already knows how to evaluate one at run time
// (JsmGateSatisfied). A gated exit is not deleted -- it is held until its
// variable says it is real, the same treatment a gated trigger line gets.

// WHY A CHAIN AND NOT A SEARCH. The first cut of this walked every word of the
// method looking for any guard whose skip region contained the MAPJUMP. It
// works -- Munbamini comes out gated -- and it is wrong, because it also finds
// guards that have nothing to do with the exit. Measured over all 900 fields it
// put a gate on **217 entities**, among them bus stops, lifts, hotel counters
// and two dozen `director0` exits, and on one of them (ecenter2's `Lift`) it
// picked a different guard than the existing rule and changed the answer from
// var[1029]==3 to var[1029]==0. A gate withholds a catalog entry until its
// variable agrees; 217 of them is 217 chances to hide a door a blind player
// needs, in exchange for fixing one. That trade is the wrong way round.
//
// The structure Munbamini actually has is an if/else-if chain, and it is exact:
//
//     rel   0   LBL | PSHM 607 | PSHN 32 | JMP == | JMPB 15   -> lands on 19
//     rel  19       | PSHM 607 | PSHN 64 | JMP == | JMPB 15   -> lands on 37
//     rel  37       | PSHM 623 | PSHN 16 | JMP == | JMPB 79   -> lands on 119
//     rel ~117  MAPJUMPO (936, 0)
//
// Every link begins exactly where the previous link's skip lands. So the walk
// follows the chain instead of scanning: start at the method's leading guard,
// and while the current guard's skip target is itself the first word of another
// guard, step to it. That reproduces this case and cannot wander into a nested
// conditional that happens to span the jump.
//
// Disc-wide the chain walk gates **21 entities that were ungated before, loses
// none, and changes none.** All 21 are story transitions of exactly this shape:
// fourteen `Director`/`dic`/`kantoku`/`director0` exits, bgroom_5 and bgroom_6's
// `bgmover`, ecmview1's `Timer`, sslock1's `spacecloth`, and -- also in Shumi
// Village -- tmelder1's `Search` line on var[608]==64, the elder's house. Not one
// is an ordinary door.

// The index, relative to the method's first word, at which a guard's skip lands.
// A guard decoded at relative offset `at` with skip `skipTo` covers the words in
// (at, at + skipTo) -- anything in there executes only when the guard passes.
static bool GatedExitJumpIsInsideGuard(int guardAt, int guardSkipTo, int mapjumpRel)
{
    if (guardAt < 0 || guardSkipTo <= 0) return false;
    if (mapjumpRel <= guardAt) return false;          // the jump precedes the guard
    return mapjumpRel < guardAt + guardSkipTo;
}

// How far into the method it is worth looking for a guard. A guard that starts
// after the MAPJUMP cannot be guarding it, so the walk stops there; the bound is
// also what keeps this from being a whole-method rescan on every entity.
static int GatedExitScanLimit(int mapjumpRel, int methodWords)
{
    if (mapjumpRel < 0) return 0;
    int lim = mapjumpRel;
    if (lim > methodWords) lim = methodWords;
    if (lim < 0) lim = 0;
    return lim;
}

// How many links of an if/else-if chain to follow. Munbamini's is three. The cap
// exists so a malformed script cannot spin here, not because a longer chain is
// implausible.
static const int GATED_EXIT_MAX_CHAIN = 16;
