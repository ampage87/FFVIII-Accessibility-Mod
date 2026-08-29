// field_nav_wallbias_disabled.inl -- THE WALL-AVOIDANCE BIAS, PARKED
//
// v0.118.0 (#centra). This block has been dead since v06.20 -- `if (false && ...)`
// -- and was kept in field_nav_autodrive.inl "for potential re-enabling with
// better narrow-space logic". It is kept for exactly that reason still; it has
// simply moved out of a file that has no room left.
//
// field_nav_autodrive.inl is ONE 1,344-line function and the 80 KB CI cap was
// 209 bytes away when v0.118.0 needed eight lines in it. Deleting live comments
// to fit is how a file loses the reasoning that makes it maintainable, and
// deleting this block would throw away work someone may want back, so the
// project's established answer applies instead: split it out (the same move
// that produced field_nav_battlepause.inl when field_navigation.cpp hit the
// same wall). The code below is byte-for-byte what was there, still disabled,
// still compiled, and still correct with respect to the v0.18.3.308 edge-vertex
// convention fix it received while parked.
//
// Included from field_nav_autodrive.inl at its original point in UpdateAutoDrive,
// after the dx/dz recompute and before the trigger-line proximity check, so the
// variables it reads (dx, dz, px, pz, s_walkmesh, s_playerEntityIdx) are the
// ones it always read.

    // v06.17: Wall-avoidance steering bias. DISABLED in v06.20 (pushed players
    // OUT of narrow corridors; corridor steering + recovery handles it better).
    // Retained for potential re-enabling with better narrow-space logic.
    if (false && s_walkmesh.valid) {
        uint16_t nowTri2 = 0xFFFF;
        {
            uint8_t* base2 = *reinterpret_cast<uint8_t**>(FF8Addresses::pFieldStateOthers);
            if (base2)
                nowTri2 = *(uint16_t*)(base2 + ENTITY_STRIDE * s_playerEntityIdx + 0x1FA);
        }
        if (nowTri2 != 0xFFFF && nowTri2 < (uint16_t)s_walkmesh.numTriangles) {
            const auto& tri = s_walkmesh.triangles[nowTri2];
            static const float WALL_BIAS_DIST = 40.0f;   // activate when within this distance
            static const float WALL_BIAS_STRENGTH = 0.25f; // blend factor (0=no bias, 1=full perpendicular)
            // v06.19: Check if corridor is narrow (walls on multiple edges).
            // If so, reduce bias to avoid ping-ponging between walls.
            int wallEdgeCount = 0;
            for (int ec = 0; ec < 3; ec++)
                if (tri.neighbor[ec] == 0xFFFF) wallEdgeCount++;
            float effectiveStrength = WALL_BIAS_STRENGTH;
            if (wallEdgeCount >= 2) effectiveStrength *= 0.3f; // very narrow, minimal bias
            for (int e = 0; e < 3; e++) {
                if (tri.neighbor[e] != 0xFFFF) continue; // not a wall edge
                // Wall edge e spans vertices e and (e+1)%3.
                // v0.18.3.308 (#113): corrected to the v0.17.9.14 convention while
                // this block was DISABLED, so re-enabling doesn't resurrect the bug.
                int wvi1 = tri.vertexIdx[e];
                int wvi2 = tri.vertexIdx[(e + 1) % 3];
                if (wvi1 >= s_walkmesh.numVertices || wvi2 >= s_walkmesh.numVertices) continue;
                float wx1 = (float)s_walkmesh.vertices[wvi1].x;
                float wy1 = (float)s_walkmesh.vertices[wvi1].y;
                float wx2 = (float)s_walkmesh.vertices[wvi2].x;
                float wy2 = (float)s_walkmesh.vertices[wvi2].y;
                // Distance from player to this edge (point-to-line-segment).
                float edx = wx2 - wx1, edy = wy2 - wy1;
                float edLenSq = edx*edx + edy*edy;
                if (edLenSq < 1.0f) continue;
                float t = ((px - wx1)*edx + (pz - wy1)*edy) / edLenSq;
                if (t < 0) t = 0; if (t > 1) t = 1;
                float closestX = wx1 + t * edx;
                float closestY = wy1 + t * edy;
                float wallDx = px - closestX;
                float wallDy = pz - closestY;
                float wallDist = sqrtf(wallDx*wallDx + wallDy*wallDy);
                if (wallDist < WALL_BIAS_DIST && wallDist > 0.1f) {
                    // Blend steering away from wall. Stronger when closer.
                    float factor = effectiveStrength * (1.0f - wallDist / WALL_BIAS_DIST);
                    float awayX = wallDx / wallDist; // unit vector away from wall
                    float awayY = wallDy / wallDist;
                    float steerMag = sqrtf(dx*dx + dz*dz);
                    dx = dx * (1.0f - factor) + awayX * factor * steerMag;
                    dz = dz * (1.0f - factor) + awayY * factor * steerMag;
                }
            }
        }
    }
