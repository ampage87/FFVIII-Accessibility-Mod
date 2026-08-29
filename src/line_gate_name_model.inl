// line_gate_name_model.inl -- SAYING THAT A TERMINAL IS NOT READY
//
// v0.114.0 (#dsrc). Aaron, on Level 3 of the Deep Sea Research Center: "when I
// tried to interact with the steam room terminal nothing seemed to happen."
//
// Nothing did. `Tanme2::touch` guards everything it does behind
// `var[603] >= 2`, and behind two more tests after that. With the guard false
// the script runs four instructions and returns. A sighted player sees a panel
// that does not light up; a blind player hears silence, which is exactly what a
// broken mod sounds like.
//
// v0.62.2 reads this same guard shape on an EXIT and DROPS the exit while it is
// false. That is right for a door -- an exit you cannot take is not a route --
// and wrong for a terminal. A terminal that is shut is still a terminal, still
// the thing the player is looking for, and still where they will have to come
// back to. Taking it out of the catalog would replace "nothing happened" with
// "it isn't there", which is worse.
//
// So the entry stays and says so.
//
// ---------------------------------------------------------------------------
// AND IT IS NOT WIRED UP YET, ON PURPOSE. v0.114.0 decodes the gate and logs
// it; the name still reads plainly. What stopped it is the polarity.
//
// `JsmDecodeGate` finds the guard a method opens with and reports the condition
// under which the script FALLS THROUGH it. For an exit that is the same thing
// as "the exit works", which is why v0.62.2 can drop a shut one. For an
// interaction line it need not be. ddtower3's `Tanme2` is the counter-example
// and it is the very line this was written for:
//
//     PSHM_B var[603] ; PSHN_L 2 ; OPER >= ; JPF +27
//       true  (>= 2) -> falls through to fifteen instructions with no message,
//                       no REQ and no dialog: the "no" answer
//       false (<  2) -> jumps to the branch that REQs Squall and runs the
//                       Expending-4 sequence: the terminal actually working
//
// So for this line the decoded gate being TRUE means NOT ready, which is the
// opposite of what LineGateSuffixApplies() below assumes. Shipping it would
// have announced "Steam Room Terminal, not ready" at exactly the moment it was
// ready, and said nothing when it was not.
//
// The rule these functions encode is still the right rule once the polarity is
// known per line; what is missing is a way to tell which branch is the empty
// one. The `[LINE-GATE]` line logs the live value on every gated line, so a run
// through the Research Center gives the values for var[602], var[603] and
// var[615] at each terminal and settles it. Wire it up then, not before.
// ---------------------------------------------------------------------------

// Long enough to be unmistakable, short enough not to bury the name it follows.
// Read aloud this comes out as "Steam Room Terminal, not ready" -- the name
// first, because that is what the player is looking for in a list.
static const char* const LINE_GATE_SUFFIX = ", not ready";

// Compose the catalog name. `gateOpen` is the live evaluation; a line with no
// decodable gate is always open and always reads as its plain name, which is
// every line on the disc bar a handful.
static void LineGateName(char* out, size_t n, const char* base, bool gateOpen)
{
    if (out == nullptr || n == 0) return;
    out[0] = '\0';
    if (base == nullptr || base[0] == '\0') return;
    if (gateOpen) { snprintf(out, n, "%s", base); return; }
    snprintf(out, n, "%s%s", base, LINE_GATE_SUFFIX);
}

// Only a line the tables have NAMED gets the suffix.
//
// "Interaction 2, not ready" tells the player nothing they can act on -- they
// do not know what Interaction 2 is, so they cannot know whether to wait for it
// or ignore it -- and there are hundreds of anonymous lines on the disc whose
// guards this build has never looked at. A named line is one somebody has
// already made a deliberate judgement about, so the suffix goes there and
// nowhere else until a run says otherwise.
static bool LineGateSuffixApplies(bool hasCuratedName, bool hasGate, bool gateOpen)
{
    return hasCuratedName && hasGate && !gateOpen;
}
