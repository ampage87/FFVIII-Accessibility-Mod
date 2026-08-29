#!/usr/bin/env python3
"""lint_dialog_page -- a window's text pointer is one PAGE, and every reader
must say so.

WHY THIS EXISTS
---------------
Aaron, on the Ragnarok passenger-compartment terminal: *"it seemed to repeat
itself, like it was loading parts of the message repeatedly."*

ff8_win_obj + 0x08 is not "the message". It is the start of the CURRENT PAGE
inside the message, and the engine advances it every time the player presses
Confirm on a page break. Decoding from it to the string terminator yields this
page AND EVERY PAGE AFTER IT, so a fourteen-page report is read out from page
one, then again from page two, then again from page three -- each utterance cut
off mid-word at the decoder's limit, which is why no containment test could see
it.

v0.70.0 fixed that. In one of the three places that read the pointer. The other
two -- ScanAndSpeakAllWindows, which serves both the poll and the AMESW/RAMESW
opcode hooks -- were the ones actually speaking, and the terminal repeated
itself exactly as before. The fix was right and the coverage was not, and
nothing in the build could tell the difference.

So this is the thing that can: any decode of a window text pointer has to go
through DecodeDialogPage, which applies the page limit itself. A new call site
that reaches for DecodeDialogWithExpansion with a window pointer fails here
rather than in a BAT.
"""
import re, sys, pathlib

ROOT = pathlib.Path(__file__).resolve().parent.parent
SRC = ROOT / "src"

# The names the codebase uses for a pointer that came out of a window object.
WINDOW_PTRS = ("text1", "text2", "textPtr")

# DecodeDialogPage is allowed to call it -- that is where the limit is applied.
EXEMPT_FILE_FUNC = ("field_dialog_expand.inl", "DecodeDialogPage")

bad = []
for path in sorted(SRC.glob("field_dialog*.inl")) + sorted(SRC.glob("field_dialog*.cpp")):
    text = path.read_text(encoding="utf-8", errors="ignore")
    for n, line in enumerate(text.split("\n"), 1):
        stripped = line.strip()
        if stripped.startswith("//") or stripped.startswith("*"):
            continue
        m = re.search(r"DecodeDialogWithExpansion\s*\(\s*([A-Za-z_][A-Za-z0-9_]*)", line)
        if not m:
            continue
        arg = m.group(1)
        if arg not in WINDOW_PTRS:
            continue
        if path.name == EXEMPT_FILE_FUNC[0]:
            continue
        bad.append((path.name, n, arg, stripped))

if bad:
    print("lint_dialog_page: FAIL")
    for name, n, arg, line in bad:
        print("  %s:%d decodes a window pointer without the page limit" % (name, n))
        print("     %s" % line)
        print("     -> use DecodeDialogPage(%s, ...) instead" % arg)
    print("")
    print("  ff8_win_obj+0x08 is the CURRENT PAGE, not the message. Decoding past")
    print("  the 0x01 page break speaks this page and every page after it, which")
    print("  is what read the Ragnarok terminal out four times.")
    sys.exit(1)

print("lint_dialog_page  OK")
