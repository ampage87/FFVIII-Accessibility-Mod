// field_live_join.inl -- v0.62.0 (#123): live entity <-> script-object join.
//
// A statement-free header fragment #included by field_nav_helpers.inl (the real
// build) and by tests/catalog_harness.cpp, so the harness exercises the actual
// join rather than a restatement of it. Requires MAX_ENTITIES,
// MAX_JSM_ENTITIES, ENTITY_STRIDE, s_jsmEntities[] and s_jsmEntityCount to be
// in scope.

// ============================================================================
// v0.62.0 (#123): THE LIVE ENTITY <-> SCRIPT JOIN, DONE FROM THE MODEL.
//
// FindJSMByRuntimeSlot above rests on "live slot i is JSM group
// (nLines+nDoors+nBg+i)". field_scripts_init really does fill the script-object
// table that way -- but the array this catalog walks (pFieldStateOthers) is NOT
// that table. It holds only the entities the running scene actually
// instantiated, and it holds them compacted, so its index is not the slot.
//
// On the Lunar Base control room (sscont1) the live array read
//   ent0 model=0  ent1 model=5  ent2 model=6  ent3 model=6  ent4 model=8  ent5 model=9
// while the SCRIPT slots in order are
//   squall=0 squallsp=1 irvine=2 rinoa=3 selphie=4 quistis=5 man0=6 man1=6
//   oldlag=7 woman0=8 piet=9 elone=10 rinoasp=11
// -- so live ent5 is `piet`, and slot-indexing called it `quistis`. Aaron:
// "When I spoke to 'Quistis' in the catalog it turned out to be a different
// NPC." Across the BAT's field loads the live index equalled the slot in only
// 17 of 90 cases.
//
// SETMODEL's inline parameter is this entity's index into the field's
// chara.one model list, and the live block carries the same number at +0x218.
// It is assigned per entity by the script itself, so the compaction cannot
// disturb it. Replayed over every field load in the BAT log, model matching --
// with a static-position tiebreak and a same-base-name collapse for the 4.4%
// of model ids two entities share (soldier1/soldier2, quistis/quistis2) --
// resolved 138 of 138 placed entities, none ambiguous, none unmatched.
// ============================================================================
static int s_liveToJsm[MAX_ENTITIES];        // live index -> s_jsmEntities index
static int s_jsmToLive[MAX_JSM_ENTITIES];    // s_jsmEntities index -> live index
static bool s_liveJsmMapReady = false;

// "man0" and "man1" are the same person twice as far as a display name goes;
// "quistis" and "quistis2" are one character. Compare the names with any
// trailing digits removed so a shared model id between name-twins is not an
// ambiguity worth refusing to resolve.
static bool SameJsmBaseName(const char* a, const char* b)
{
    if (!a || !b) return false;
    size_t la = strlen(a), lb = strlen(b);
    while (la > 0 && a[la-1] >= '0' && a[la-1] <= '9') la--;
    while (lb > 0 && b[lb-1] >= '0' && b[lb-1] <= '9') lb--;
    if (la == 0 || la != lb) return false;
    return _strnicmp(a, b, la) == 0;
}

static void BuildLiveJsmMap(uint8_t* base, int liveCount)
{
    for (int i = 0; i < MAX_ENTITIES; i++)     s_liveToJsm[i] = -1;
    for (int j = 0; j < MAX_JSM_ENTITIES; j++) s_jsmToLive[j] = -1;
    s_liveJsmMapReady = false;
    if (!base || liveCount <= 0 || s_jsmEntityCount <= 0) return;
    if (liveCount > MAX_ENTITIES) liveCount = MAX_ENTITIES;

    // Pass 1 -- the model key. One free candidate is the answer; several means
    // the nearest static SET3 position wins, and failing even that, the first
    // free candidate but only when every candidate is the same name bar a
    // trailing digit, so the announced name is right either way.
    //
    // (An earlier draft ran a separate "unambiguous model id" pass ahead of this
    // one. It was deleted because no mutation of it could be made to fail a
    // test: a lone free candidate falls through this loop to the same answer,
    // since a single candidate is trivially "all twins". A branch nothing can
    // distinguish is not a safeguard, it is furniture.)
    for (int i = 0; i < liveCount; i++) {
        if (s_liveToJsm[i] >= 0) continue;
        int16_t modelId = *(int16_t*)(base + ENTITY_STRIDE * i + 0x218);
        if (modelId < 0) continue;
        float lx = (float)(*(int32_t*)(base + ENTITY_STRIDE * i + 0x190) / 4096);
        float ly = (float)(*(int32_t*)(base + ENTITY_STRIDE * i + 0x194) / 4096);
        int best = -1, firstFree = -1, nFree = 0;
        float bestD = 0.0f;
        bool allTwins = true;
        const char* firstName = nullptr;
        for (int j = 0; j < s_jsmEntityCount; j++) {
            if (s_jsmEntities[j].jsmCategory != 3) continue;
            if (s_jsmEntities[j].modelParam != (int)modelId) continue;
            if (s_jsmToLive[j] >= 0) continue;
            if (firstFree < 0) { firstFree = j; firstName = s_jsmEntities[j].symName; }
            else if (!SameJsmBaseName(firstName, s_jsmEntities[j].symName)) allTwins = false;
            nFree++;
            if (!s_jsmEntities[j].hasPosition) continue;
            float dx = (float)s_jsmEntities[j].posX - lx;
            float dy = (float)s_jsmEntities[j].posY - ly;
            float d  = dx*dx + dy*dy;
            if (best < 0 || d < bestD) { best = j; bestD = d; }
        }
        int pick = -1;
        if (best >= 0 && bestD <= 64.0f*64.0f) pick = best;
        else if (nFree >= 1 && allTwins)       pick = firstFree;
        if (pick >= 0) { s_liveToJsm[i] = pick; s_jsmToLive[pick] = i; }
    }
    // Pass 2 -- entities the script never gave a model (save points, exits,
    // script-only objects). No model key, so the only honest evidence is that
    // the live block sits exactly where the script's own SET3 put it.
    for (int i = 0; i < liveCount; i++) {
        if (s_liveToJsm[i] >= 0) continue;
        int32_t fx = *(int32_t*)(base + ENTITY_STRIDE * i + 0x190);
        int32_t fy = *(int32_t*)(base + ENTITY_STRIDE * i + 0x194);
        if (fx == 0 && fy == 0) continue;
        int px = (int)(fx / 4096), py = (int)(fy / 4096);
        int hit = -1, nHit = 0;
        for (int j = 0; j < s_jsmEntityCount; j++) {
            if (s_jsmEntities[j].jsmCategory != 3) continue;
            if (s_jsmToLive[j] >= 0 || !s_jsmEntities[j].hasPosition) continue;
            if ((int)s_jsmEntities[j].posX != px || (int)s_jsmEntities[j].posY != py) continue;
            if (nHit == 0) hit = j;
            nHit++;
        }
        if (nHit == 1) { s_liveToJsm[i] = hit; s_jsmToLive[hit] = i; }
    }
    s_liveJsmMapReady = true;
}

// The identity of live entity `liveIdx`, or nullptr when nothing proved it.
// Nullptr means "no name and no script-derived type" -- which is the right
// answer when the evidence is absent, and a great deal better than the
// confident wrong name slot-indexing produced.
static const FieldArchive::JSMEntityInfo* FindJSMByLiveEntity(int liveIdx)
{
    if (!s_liveJsmMapReady || liveIdx < 0 || liveIdx >= MAX_ENTITIES) return nullptr;
    int j = s_liveToJsm[liveIdx];
    if (j < 0 || j >= s_jsmEntityCount) return nullptr;
    return &s_jsmEntities[j];
}

// The live entity running this script object, or -1 if it is not in the scene.
static int LiveIndexForJSM(const FieldArchive::JSMEntityInfo& je)
{
    if (!s_liveJsmMapReady) return -1;
    if (je.jsmIndex < 0) return -1;
    for (int j = 0; j < s_jsmEntityCount; j++)
        if (s_jsmEntities[j].jsmIndex == je.jsmIndex) return s_jsmToLive[j];
    return -1;
}

