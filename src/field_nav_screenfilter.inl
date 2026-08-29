// field_nav_screenfilter.inl -- IsSeparatedByTriggerLine.
//
// Shared by the mod (field_navigation.cpp) and tests/catalog_harness.cpp, which
// used to keep its own copy of this function. v0.62.3 taught the real one to
// ignore a gated-shut line and the harness copy went on filtering, so the
// fixture written to prove the fix passed the fix and failed anyway. A rule the
// tests restate instead of running is not under test. Both sides now compile
// this file. Callers declare the skipTriggerIdx default themselves.
static bool IsSeparatedByTriggerLine(float px, float py, float ex, float ey, int skipTriggerIdx)
{
    for (int t = 0; t < s_capturedLineCount; t++) {
        if (!s_capturedLines[t].active) continue;
        if (s_capturedLines[t].gateClosed) continue;   // v0.62.3: inert, not a wall
        // v06.02: Skip the exempted trigger line (used when driving TO a screen transition).
        if (t == skipTriggerIdx) continue;
        // v0.07.82: Only screen-boundary lines act as separators.
        // Camera pans, event triggers, and unclassified lines are transparent.
        if (s_capturedLines[t].lineType != FieldArchive::JSM_ENT_LINE_SCREEN_BOUND &&
            s_capturedLines[t].lineType != FieldArchive::JSM_ENT_UNKNOWN)
            continue;
        float lx1 = (float)s_capturedLines[t].x1;
        float ly1 = (float)s_capturedLines[t].y1;
        float lx2 = (float)s_capturedLines[t].x2;
        float ly2 = (float)s_capturedLines[t].y2;
        // Cross product: (line_end - line_start) x (point - line_start)
        float ldx = lx2 - lx1;
        float ldy = ly2 - ly1;
        float crossPlayer = ldx * (py - ly1) - ldy * (px - lx1);
        float crossEntity = ldx * (ey - ly1) - ldy * (ex - lx1);
        // If signs differ, points are on opposite sides of the line.
        // Use a small deadzone to avoid filtering entities right on the line.
        if (crossPlayer * crossEntity < -1.0f) {
            return true;  // separated by this trigger line
        }
    }
    return false;  // same side of all trigger lines
}
