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
    // generic "Interaction N" or "NPC N". A standalone JSM object whose only
    // name is its internal SYM (e.g. bghall_3 'Kanban2') must not expose that
    // symbol to the player -- SYM names are unreliable internal identifiers
    // that don't always reflect what an entity actually is (kanban2 looks
    // like a signpost name but the entity IS Xu, a character, on bghall_3).
    // Friendly-named objects (Directory) and named specials (Save/Draw/Shop/
    // Card) are not ENT_OBJECT-with-raw-SYM, so they keep their labels.
    // Runs AFTER dedupe so the objIsRawSym test above still sees the raw
    // name (preserving the Hall 4 dedupe). Only positioned objects reach the
    // catalog, so every relabel stays navigable. Numbering for each label
    // type continues from existing entries of that type.
    //
    // v0.17.8.15: NPC vs Interaction discriminator. Replaces the v0.17.8.11
    // chara.one cross-reference, which was reverted after the bghall_3
    // screenshot proved kanban2 IS Xu standing in the world (not a signpost),
    // and the chara.one classifier had misclassified her model p048 as a
    // prop. The classifier was the wrong mechanism entirely -- what matters
    // for the player is whether the entity stands in the world and is talked
    // to (NPC) vs. is a walk-across line trigger (Interaction). The behavior
    // signal:
    //   jsmCategory == 3 (Other) AND hasSetmodelInit  -> "NPC N"
    //   everything else (Background, no SETMODEL, etc.) -> "Interaction N"
    //
    // The signal is grounded in observable game behavior, not file-level
    // classification: an Other-category entity that loads a 3D model in init
    // is by construction "someone standing somewhere" -- whether the model
    // file is conventionally a 'd'-prefix character or a 'p'-prefix prop
    // doesn't matter, because both are used for characters across the game.
    // Validated against bghall_3:
    //   line3 (cat 1, Line)         -> Interaction 1  (signpost, walk-across)
    //   line4 (cat 1, Line)         -> Interaction 2  (signpost, walk-across)
    //   ent25 kanban2 (cat 3, SETMODEL=1) -> NPC 1   (Xu, walk-up + Confirm)
    //
    // Per Aaron's directive, NPC labels are pure "NPC N" -- no SYM-derived
    // names ever exposed to the player. The nav-cycle code adds the
    // " X of Y" suffix at announce time based on how many NPCs are on the
    // current field.
    for (int a = 0; a < newCount; a++) {
        if (newCatalog[a].entityIdx > -300) continue;   // JSM-injected only
        if (newCatalog[a].type != ENT_OBJECT) continue;
        int ja = -(newCatalog[a].entityIdx + 300);
        if (ja < 0 || ja >= s_jsmEntityCount) continue;
        const char* sym = s_jsmEntities[ja].symName;
        if (sym[0] == '\0' || _stricmp(newCatalog[a].name, sym) != 0) continue;

        // v0.17.8.15: NPC discriminator. Other-category entity (cat 3) with
        // a SETMODEL in its init method is by definition a positioned, model-
        // bearing entity the player walks up to and Confirms -- i.e. an NPC.
        bool isNpcCandidate = (s_jsmEntities[ja].jsmCategory == 3 &&
                               s_jsmEntities[ja].hasSetmodelInit);

        if (isNpcCandidate) {
            // v0.17.8.15.1: Count entries already named "NPC N" (the generic
            // relabel sequence), NOT all ENT_NPC entries. Friendly-named NPCs
            // (Cid, Quistis, etc.) are also typed ENT_NPC but are announced
            // by their actual name -- counting them inflated kanban2's number
            // to "NPC 2" on bghall_3 even though it was the first/only raw-SYM
            // NPC relabel (Aaron never heard an "NPC 1" because the catalog's
            // other ENT_NPC announced as e.g. "Cid"). Match the "NPC %d"
            // prefix only.
            int n = 0;
            for (int c = 0; c < newCount; c++) {
                const char* nm = newCatalog[c].name;
                if (strncmp(nm, "NPC ", 4) == 0 && nm[4] >= '0' && nm[4] <= '9')
                    n++;
            }
            n++;
            snprintf(newCatalog[a].name, sizeof(newCatalog[a].name), "NPC %d", n);
            newCatalog[a].type = ENT_NPC;
            Log::Field("FieldNavigation: [dedup] relabeled raw-SYM object '%s' -> "
                       "NPC %d (Other + SETMODEL-init) [v0.17.8.15.1]", sym, n);
            continue;
        }

        // Fall-through: not an NPC by the behavior signal. Generic
        // "Interaction N" -- background script object, Other with no model,
        // etc. Numbering continues from existing ENT_INTERACTION entries.
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
