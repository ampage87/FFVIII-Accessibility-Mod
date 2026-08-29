// world_map_wmslots.inl -- the eight WORLDMAP position slots, all of them.
//
// PART OF world_map.cpp -- TEXTUAL INCLUDE. Do NOT compile standalone.
// Included AFTER world_map_state.inl (for WM_SAVEMAP_BASE / WM_WORLDMAP_OFFSET
// and WmSafeReadBytes) and BEFORE world_map_segments.inl, which calls it.
//
// v0.52.0 (#109): FOUR OF THESE HAVE NEVER BEEN IDENTIFIED, AND ONE OF THEM
// MAY BE THE ANSWER.
//
// The struct is eight 12-byte records -- int32 X, int32 Y, int16 Z, int16
// rotation -- at savemap +0x125C. Four are known: char, ragnarok, bgu, car.
// WORLDMAP_CATALOG_STORY_WINDOWS.md records the other four as empty across all
// 41 saves, and re-checking them on 2026-08-21 against the three disc-3 saves
// found them empty there too.
//
// That was a curiosity until the White SeeD Ship BAT. The hull was driven onto
// the ship's own marker -- the log reads `dist to White SeeD Ship 5` -- and the
// screenshot is an empty bay. So the open question is which of these the ship
// is:
//
//   * an OBJECT with a live position, in which case one of these slots goes
//     live the moment it appears and the destination should read it the way
//     Mobile Balamb Garden reads bgu_pos -- which would also survive the ship
//     moving, and
//   * a FIXED coordinate that the story spawns, in which case they all stay
//     zero and the gate is a story value.
//
// Four unnamed records cost eight log lines to settle and nothing to guess, and
// the line prints on every world-map entry, so the next BAT answers it whether
// or not anyone remembers to look.
static void WmDumpWorldmapSlots()
{
    static const char* const WMSLOT_NAME[8] = {
        "char", "unnamed1", "ragnarok", "bgu", "car", "unnamed5", "unnamed6", "unnamed7"
    };
    for (int sl = 0; sl < 8; sl++) {
        const uintptr_t a8 = WM_SAVEMAP_BASE + WM_WORLDMAP_OFFSET + (uint32_t)sl * 12;
        int32_t sx = 0, sy = 0;
        int16_t sz = 0, sr = 0;
        const bool ok = WmSafeReadBytes(a8 + 0,  &sx, 4) &&
                        WmSafeReadBytes(a8 + 4,  &sy, 4) &&
                        WmSafeReadBytes(a8 + 8,  &sz, 2) &&
                        WmSafeReadBytes(a8 + 10, &sr, 2);
        if (!ok) {
            Log::World("WorldMap: [WMSLOTS] +0x%02X %-9s READ-FAULT at 0x%08X",
                       sl * 12, WMSLOT_NAME[sl], (uint32_t)a8);
            continue;
        }
        Log::World("WorldMap: [WMSLOTS] +0x%02X %-9s X=%d Y=%d Z=%d rot=%d%s",
                   sl * 12, WMSLOT_NAME[sl], (int)sx, (int)sy, (int)sz, (int)sr,
                   (sx || sy) ? "   <-- LIVE" : "");
    }
}
