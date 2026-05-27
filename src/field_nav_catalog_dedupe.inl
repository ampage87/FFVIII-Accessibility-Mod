// field_nav_catalog_dedupe.inl — v0.17.8.8 object/line dedupe + raw-SYM relabel.
// Extracted from RefreshCatalog() in field_nav_catalog.inl (v0.17.8.9) to keep
// that file under the size ceiling. This is NOT a standalone function: it is a
// statement fragment #included inline at ONE point inside RefreshCatalog's
// __try block, so it sees the local newCatalog[]/newCount and the file-scope
// catalog state (s_jsmEntities, s_jsmEntityCount, s_capturedLines,
// s_capturedLineCount, EntityTypeName, ENT_*, MAX_CATALOG, _stricmp, Log).
// Behavior is byte-identical to the previous inline block. Do not compile
// independently; do not include anywhere except that single call site.

// v0.17.8.8: General duplicate filter for object/line overlaps.
// A JSM-injected object (sentinel <= -300) coincident with an
// interactive trigger LINE (sentinel -200..-299, type ENT_INTERACTION)
// is the same physical interactable surfaced twice (entity + its walk-on
// line). Reported on bghall_2: the 'kanban' sign showed as both
// "Interaction 2" and "Kanban1". Rule: keep the more informative entry.
// A named object -- a special type (Save/Draw/Shop/Card) or a friendly
// name like "Directory" -- beats the generic line (drop the line); a
// raw-SYM object like "Kanban1" loses to "Interaction N" (drop the
// object). Exits are untouched (they dedupe by destination elsewhere).
{
    const float DUP_DIST = 128.0f;
    bool dupRemoved[MAX_CATALOG] = {};
    for (int a = 0; a < newCount; a++) {
        if (dupRemoved[a]) continue;
        if (newCatalog[a].entityIdx > -300) continue;   // A must be JSM-injected
        if (newCatalog[a].type == ENT_EXIT) continue;   // never dedupe exits here
        int ja = -(newCatalog[a].entityIdx + 300);
        if (ja < 0 || ja >= s_jsmEntityCount) continue;
        float ax = (float)s_jsmEntities[ja].posX;
        float ay = (float)s_jsmEntities[ja].posY;
        for (int b = 0; b < newCount; b++) {
            if (b == a || dupRemoved[b]) continue;
            if (newCatalog[b].type != ENT_INTERACTION) continue;
            if (newCatalog[b].entityIdx > -200 || newCatalog[b].entityIdx <= -300)
                continue;                                // B must be a trigger line
            int tb = -(newCatalog[b].entityIdx + 200);
            if (tb < 0 || tb >= s_capturedLineCount) continue;
            float bx = (float)(s_capturedLines[tb].x1 + s_capturedLines[tb].x2) / 2.0f;
            float by = (float)(s_capturedLines[tb].y1 + s_capturedLines[tb].y2) / 2.0f;
            float ddx = ax - bx, ddy = ay - by;
            if (ddx*ddx + ddy*ddy > DUP_DIST*DUP_DIST) continue;
            // Same spot. Is the object's name just its raw SYM?
            bool objIsRawSym = false;
            if (newCatalog[a].type == ENT_OBJECT) {
                const char* sym = s_jsmEntities[ja].symName;
                objIsRawSym = (sym[0] != '\0' &&
                               _stricmp(newCatalog[a].name, sym) == 0);
            }
            if (objIsRawSym) {
                dupRemoved[a] = true;   // keep the line ("Interaction N")
                Log::Field("FieldNavigation: [dedup] dropped JSM '%s' (%.0f,%.0f): "
                           "duplicate of Interaction line%d at (%.0f,%.0f) [v0.17.8.8]",
                           newCatalog[a].name, ax, ay, tb, bx, by);
                break;                  // A is gone; stop scanning lines for it
            } else {
                dupRemoved[b] = true;   // keep the named object
                Log::Field("FieldNavigation: [dedup] dropped Interaction line%d (%.0f,%.0f): "
                           "duplicate of JSM '%s' (%s) at (%.0f,%.0f) [v0.17.8.8]",
                           tb, bx, by, newCatalog[a].name,
                           EntityTypeName(newCatalog[a].type), ax, ay);
            }
        }
    }
    int dw = 0;
    for (int r = 0; r < newCount; r++) {
        if (!dupRemoved[r]) {
            if (dw != r) newCatalog[dw] = newCatalog[r];
            dw++;
        }
    }
    newCount = dw;

    // v0.17.8.8: Relabel any surviving raw-SYM interactive object as a
    // generic "Interaction N". A standalone JSM object whose only name
    // is its internal SYM (e.g. bghall_3 'Kanban2', a signboard with no
    // friendly mapping and no coincident line to dedupe against) must
    // not expose that symbol to the player -- per Aaron, "Interaction"
    // is the familiar term. Friendly-named objects (Directory) and named
    // specials (Save/Draw/Shop/Card) are not ENT_OBJECT-with-raw-SYM, so
    // they keep their labels. Runs AFTER dedupe so the objIsRawSym test
    // above still sees the raw name (preserving the Hall 4 dedupe). Only
    // positioned objects reach the catalog, so every relabel stays
    // navigable. Numbering continues from existing Interactions.
    for (int a = 0; a < newCount; a++) {
        if (newCatalog[a].entityIdx > -300) continue;   // JSM-injected only
        if (newCatalog[a].type != ENT_OBJECT) continue;
        int ja = -(newCatalog[a].entityIdx + 300);
        if (ja < 0 || ja >= s_jsmEntityCount) continue;
        const char* sym = s_jsmEntities[ja].symName;
        if (sym[0] == '\0' || _stricmp(newCatalog[a].name, sym) != 0) continue;
        int n = 0;
        for (int c = 0; c < newCount; c++)
            if (newCatalog[c].type == ENT_INTERACTION) n++;
        n++;
        snprintf(newCatalog[a].name, sizeof(newCatalog[a].name), "Interaction %d", n);
        newCatalog[a].type = ENT_INTERACTION;
        Log::Field("FieldNavigation: [dedup] relabeled raw-SYM object '%s' -> "
                   "Interaction %d [v0.17.8.8]", sym, n);
    }
}
