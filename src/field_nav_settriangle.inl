// field_nav_settriangle.inl — HookedSetCurrentTriangle + entity center tracking
// Included from field_navigation.cpp. Do not compile independently.
// v0.12.18: Extracted from field_navigation.cpp for readability.

static void __cdecl HookedSetCurrentTriangle(int a1, int a2, int a3)
{
    // Call original first — game behaviour unchanged.
    if (s_originalSetCurrentTriangle)
        s_originalSetCurrentTriangle(a1, a2, a3);

    // Read vertex coords from the three pointer arguments.
    int16_t x0=0,y0=0,z0=0, x1=0,y1=0,z1=0, x2=0,y2=0,z2=0;
    bool ok = ReadVertexCoords((uintptr_t)(unsigned)a1, x0, y0, z0)
           && ReadVertexCoords((uintptr_t)(unsigned)a2, x1, y1, z1)
           && ReadVertexCoords((uintptr_t)(unsigned)a3, x2, y2, z2);

    if (ok && FF8Addresses::HasFieldStateArrays()) {
        float cx = (x0 + x1 + x2) / 3.0f;
        // v05.61: Use Y (screen-vertical) instead of Z (depth) for triangle center.
        float cy = (y0 + y1 + y2) / 3.0f;

        // Identify which entity just moved by comparing triIds against our
        // shadow copy (s_hookPrevTri).  Accept only if exactly ONE entity
        // changed — that entity's new triId receives the spatial centre
        // computed from the vertex arguments.  If multiple entities appear
        // to have changed simultaneously (e.g. after a hookPrevTri reset or
        // a threading edge case) we update the shadows but do not store a
        // centre, since we cannot reliably attribute which triId the vertices
        // belong to.  The key insight: storing centre keyed by triId (not
        // entity index) means the lookup in GetEntityPos is always spatially
        // correct — a triangle has exactly one world position regardless of
        // which entity is standing on it.
        __try {
            uint8_t* base = reinterpret_cast<uint8_t*>(
                *reinterpret_cast<uint32_t*>(FF8Addresses::pFieldStateOthers));
            uint8_t count = *FF8Addresses::pFieldStateOtherCount;
            if (base && count > 0) {
                uint8_t  lim         = (count < MAX_ENTITIES) ? count : (uint8_t)MAX_ENTITIES;
                int      changeCount = 0;
                int      changedIdx  = -1;
                uint16_t newTriId    = 0;
                for (int i = 0; i < (int)lim; i++) {
                    int16_t  model  = *(int16_t*)(base + ENTITY_STRIDE * i + 0x218);
                    if (model < 0) continue;
                    uint16_t curTri = *(uint16_t*)(base + ENTITY_STRIDE * i + 0x1FA);
                    if (curTri != s_hookPrevTri[i]) {
                        changeCount++;
                        changedIdx      = i;
                        newTriId        = curTri;
                        s_hookPrevTri[i] = curTri;  // always keep shadow current
                    }
                }
                if (changeCount == 1 && newTriId > 0 && newTriId < MAX_TRI_ID) {
                    s_triCenter[newTriId].cx    = cx;
                    s_triCenter[newTriId].cz    = cy;
                    s_triCenter[newTriId].valid = true;
                    // Also update legacy per-entity cache used by AnnounceCurrentTarget
                    // (refreshed from triCenter map in Update() every 500ms).
                    s_entityCenters[changedIdx].cx    = cx;
                    s_entityCenters[changedIdx].cz    = cy;
                    s_entityCenters[changedIdx].valid = true;

                    // v06.13: CoordSample — Approach B: shared-edge midpoint.
                    // When the player crosses from prevTri to newTriId, they're
                    // at or near the shared edge between the two triangles.
                    // The 3D midpoint of that shared edge is a much tighter
                    // constraint than the triangle center (~30 unit error vs ~100).
                    // Also logs the triangle center as fallback (prevTri=0 at field load).
                    if (changedIdx == s_playerEntityIdx && s_walkmesh.valid &&
                        newTriId < (uint16_t)s_walkmesh.numTriangles) {
                        // 2D entity position from fixed-point coords.
                        uint8_t* pBlock = base + ENTITY_STRIDE * changedIdx;
                        int32_t fpX = *(int32_t*)(pBlock + 0x190);
                        int32_t fpY = *(int32_t*)(pBlock + 0x194);
                        float ent2dX = (float)(fpX / 4096);
                        float ent2dY = (float)(fpY / 4096);
                        const char* fld = FF8Addresses::pCurrentFieldName
                                          ? FF8Addresses::pCurrentFieldName : "?";
                        // Try to find the shared edge between prevTri and newTriId.
                        uint16_t prevTri = s_coordPrevPlayerTri;
                        float wx = 0, wy = 0, wz = 0;
                        bool usedEdge = false;
                        if (prevTri != 0 && prevTri != 0xFFFF &&
                            prevTri < (uint16_t)s_walkmesh.numTriangles) {
                            const auto& tOld = s_walkmesh.triangles[prevTri];
                            // Find which edge of prevTri is shared with newTriId.
                            for (int e = 0; e < 3; e++) {
                                if (tOld.neighbor[e] == newTriId) {
                                    // Shared edge: vertices e and (e+1)%3 of prevTri.
                                    // v0.18.3.308 (#113): was the old (e+1, e+2) pair,
                                    // i.e. the wrong segment (see v0.17.9.14 FindPortal
                                    // fix). Diagnostic-only consumer: COORD samples in
                                    // ff8_nav_data.log logged the wrong crossed-edge
                                    // midpoint; behavior unchanged.
                                    int ea = tOld.vertexIdx[e];
                                    int eb = tOld.vertexIdx[(e + 1) % 3];
                                    if (ea < s_walkmesh.numVertices &&
                                        eb < s_walkmesh.numVertices) {
                                        wx = (s_walkmesh.vertices[ea].x +
                                              s_walkmesh.vertices[eb].x) / 2.0f;
                                        wy = (s_walkmesh.vertices[ea].y +
                                              s_walkmesh.vertices[eb].y) / 2.0f;
                                        wz = (s_walkmesh.vertices[ea].z +
                                              s_walkmesh.vertices[eb].z) / 2.0f;
                                        usedEdge = true;
                                    }
                                    break;
                                }
                            }
                        }
                        if (!usedEdge) {
                            // Fallback: use triangle center (field load, first sample, etc.)
                            const auto& triNew = s_walkmesh.triangles[newTriId];
                            int vi0 = triNew.vertexIdx[0];
                            int vi1 = triNew.vertexIdx[1];
                            int vi2 = triNew.vertexIdx[2];
                            if (vi0 < s_walkmesh.numVertices &&
                                vi1 < s_walkmesh.numVertices &&
                                vi2 < s_walkmesh.numVertices) {
                                wx = (s_walkmesh.vertices[vi0].x +
                                      s_walkmesh.vertices[vi1].x +
                                      s_walkmesh.vertices[vi2].x) / 3.0f;
                                wy = (s_walkmesh.vertices[vi0].y +
                                      s_walkmesh.vertices[vi1].y +
                                      s_walkmesh.vertices[vi2].y) / 3.0f;
                                wz = (s_walkmesh.vertices[vi0].z +
                                      s_walkmesh.vertices[vi1].z +
                                      s_walkmesh.vertices[vi2].z) / 3.0f;
                            }
                        }
                        NavLog::CoordSample(fld, (int)newTriId,
                                            ent2dX, ent2dY,
                                            wx, wy, wz);
                        s_coordPrevPlayerTri = newTriId;
                    }
                }
                // changeCount > 1: shadows updated above; centre not stored
            }
        }
        __except (EXCEPTION_EXECUTE_HANDLER) { /* non-critical */ }
    }

    // Diagnostic logging — first 30 calls per field only.
    if (s_setTriCallCount < SET_TRI_LOG_MAX) {
        s_setTriCallCount++;
        if (ok) {
            Log::Field("FieldNavigation: [set_tri] #%d "
                       "v0=(%d,%d,%d) v1=(%d,%d,%d) v2=(%d,%d,%d) center=(%.0f,%.0f)",
                       s_setTriCallCount,
                       x0, y0, z0, x1, y1, z1, x2, y2, z2,
                       (x0+x1+x2)/3.0f, (z0+z1+z2)/3.0f);
        } else {
            Log::Field("FieldNavigation: [set_tri] #%d args=(0x%08X,0x%08X,0x%08X) BAD vertex ptrs",
                       s_setTriCallCount, (unsigned)a1, (unsigned)a2, (unsigned)a3);
        }
    }
}

