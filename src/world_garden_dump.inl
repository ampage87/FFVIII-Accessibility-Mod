// world_garden_dump.inl - runtime world-map polygon dump (#80 diagnostic).
//
// PART OF world_map.cpp -- TEXTUAL INCLUDE. Do NOT compile standalone.
//
// WHY THIS EXISTS
//
// Aaron, after the v0.20.74 BAT: "Shumi Village is 100% reachable with Balamb
// Garden. It seems like your construct of the world map is incomplete. Can you
// go back to the static files to clarify, or is there a way we can do a diag
// BAT to dump the whole world map at runtime?"
//
// He is right and the second suggestion is the correct one. Every map fact this
// mod plans against has been derived offline from wmx.obj on disk, and the
// Shumi island comes out of that extraction with NO Garden mask bit on a single
// polygon -- which cannot be true of a place the Garden demonstrably reaches.
// Either the parse is wrong or the file is not what the engine actually loads,
// and no amount of re-reading the same file offline can tell which.
//
// So dump what the ENGINE hands the mod, at the point the mod already receives
// it, and diff that against the offline extraction. Whatever disagrees is the
// bug -- the same method that settled the executor divergence, applied to the
// map itself.
//
// FORMAT: little-endian, one 20-byte record per polygon, no padding.
//
//     int32 magic   'WMD1' on the first record only (see the header below)
//     ...
//     per polygon:
//       uint8  b13          terrain
//       uint8  b14
//       uint8  b15          vehicle masks
//       uint8  pad
//       int32  x0, z0       mesh-space vertex 0   (h in the low 16 bits of z? no --
//       int16  h0                                  kept separate, see below)
//
// Concretely each record is:
//       uint8 b13, b14, b15, flags
//       int32 x[3], z[3]  (mesh space)
//       int16 h[3]
//   = 4 + 24 + 6 = 34 bytes. At ~473k polygons that is ~16 MB, written once.
//
// SAFETY: gated off by default. It writes one file, allocates nothing, and runs
// only during the grid build that already walks every polygon.

// v0.20.75: ON for the diagnostic BAT Aaron asked for. Turn back to 0 afterwards --
// it writes ~16 MB every time the world map loads.
#define GARDEN_DUMP_ENABLED 0   // v0.20.76: done its job -- the dump proved the map data correct

#if GARDEN_DUMP_ENABLED

static FILE*  s_gdDumpFile  = nullptr;
static int    s_gdDumpCount = 0;

static const char* GD_DUMP_PATH =
    "C:\\Users\\ampag\\OneDrive\\Documents\\FFVIII-Accessibility-Mod"
    "\\FF8_OriginalPC_mod\\Logs\\worldmap_polys.bin";

static void Garden_DumpBegin()
{
    if (s_gdDumpFile) { fclose(s_gdDumpFile); s_gdDumpFile = nullptr; }
    s_gdDumpCount = 0;
    s_gdDumpFile = fopen(GD_DUMP_PATH, "wb");
    if (!s_gdDumpFile) {
        Log::World("WorldMap: [GDDUMP] could not open %s", GD_DUMP_PATH);
        return;
    }
    // 16-byte header: magic, record size, reserved.
    const char magic[8] = { 'W','M','D','U','M','P','0','1' };
    const int32_t recSize = 34;
    const int32_t reserved = 0;
    fwrite(magic, 1, 8, s_gdDumpFile);
    fwrite(&recSize, 4, 1, s_gdDumpFile);
    fwrite(&reserved, 4, 1, s_gdDumpFile);
    Log::World("WorldMap: [GDDUMP] writing %s", GD_DUMP_PATH);
}

// Called from Garden_FeedPoly with exactly what the engine handed the mod.
static void Garden_DumpPoly(const uint8_t* poly, const int32_t* vwx,
                            const int32_t* vwy, const int16_t* vwz, int vertCount)
{
    if (!s_gdDumpFile) return;
    const uint8_t i0 = poly[0], i1 = poly[1], i2 = poly[2];
    if (i0 >= vertCount || i1 >= vertCount || i2 >= vertCount) return;
    uint8_t hdr[4] = { poly[0x0D], poly[0x0E], poly[0x0F], 0 };
    const uint8_t idx[3] = { i0, i1, i2 };
    int32_t xs[3], zs[3];
    int16_t hs[3];
    for (int k = 0; k < 3; k++) {
        xs[k] = vwx[idx[k]];
        zs[k] = 196608 - vwy[idx[k]];      // mesh space, same reflection the grid uses
        hs[k] = vwz[idx[k]];
    }
    fwrite(hdr, 1, 4, s_gdDumpFile);
    fwrite(xs, 4, 3, s_gdDumpFile);
    fwrite(zs, 4, 3, s_gdDumpFile);
    fwrite(hs, 2, 3, s_gdDumpFile);
    s_gdDumpCount++;
}

static void Garden_DumpEnd()
{
    if (!s_gdDumpFile) return;
    fclose(s_gdDumpFile);
    s_gdDumpFile = nullptr;
    Log::World("WorldMap: [GDDUMP] wrote %d polygons to %s", s_gdDumpCount, GD_DUMP_PATH);
}

#else   // GARDEN_DUMP_ENABLED == 0

static void Garden_DumpBegin() {}
static void Garden_DumpPoly(const uint8_t*, const int32_t*, const int32_t*, const int16_t*, int) {}
static void Garden_DumpEnd() {}

#endif  // GARDEN_DUMP_ENABLED
