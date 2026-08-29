// nav_observe_window_pure.inl -- when a NAV-OBSERVE sampling window is valid.
//
// A statement-free header fragment #included by field_nav_observe.inl (the real
// build) and by tests/nav_observe_window_test.cpp, so the probe exercises the
// actual rule rather than a restatement of it.
//
// WHY THIS IS ITS OWN FILE (v0.77.0)
// ----------------------------------
// A NAV-OBSERVE sample is a subtraction between two player positions taken a
// few hundred milliseconds apart, and a field transition moves the player by
// teleport. A window that spans one measures the doorway, not the arrow.
//
// The 20:25 log has two of these and they are the two worst readings in it:
// rgroad2 UP reported DIVERGE=177deg -- delta (81,-1841), the exact opposite of
// the prediction -- and rgexit1 LEFT reported 138deg, both timestamped on the
// second the field changed. Nothing was wrong with either room.
//
// It is not only a cosmetic problem with the log. On a field whose CA is
// missing or degenerate the observer's samples feed the empirical calibration
// that overwrites the movement axes for that field, so a bogus sample is a
// candidate for permanently steering him backwards in the one situation where
// he has no correct axes to fall back on. The consensus check makes that
// unlikely; walking through the same door repeatedly makes it less unlikely.
enum ObsWindow {
    OBS_WIN_SKIP = 0,        // not a single-arrow hold; no window at all
    OBS_WIN_ANCHOR,          // (re)start the window here and measure from now
    OBS_WIN_ACCUMULATE,      // the window is still valid; keep counting ticks
};

// arrowBits    -- how many arrows are held (diagonals are ambiguous, so only 1)
// arrowsChanged-- the arrow held is not the one held last tick
// fieldChanged -- the RAW field id moved since the window was anchored. Raw,
//                 not the debounced name: the name arrives seconds later, which
//                 is several samples too late to save any of them.
static ObsWindow ObsWindowStep(int arrowBits, bool arrowsChanged, bool fieldChanged)
{
    if (arrowBits != 1) return OBS_WIN_SKIP;
    if (arrowsChanged)  return OBS_WIN_ANCHOR;
    if (fieldChanged)   return OBS_WIN_ANCHOR;
    return OBS_WIN_ACCUMULATE;
}
