// world_rag_drive_clamp_body.inl -- the body of the invariant, split out so
// tests/rag_flight_clamp_test.cpp compiles the SHIPPED rule rather than a
// restatement of it. world_rag_drive.inl includes this; nothing else should.
static void RagFlightClampAim(int32_t px, int32_t py,
                              int32_t tgtX, int32_t tgtY,
                              int32_t* aimX, int32_t* aimY,
                              const LocationEntry* tears,
                              int32_t* pathLen)
{
    if (!aimX || !aimY) return;

    if (pathLen) *pathLen = 0;      // a flying drive has no route to follow

    *aimX = tgtX;
    *aimY = tgtY;

    if (tears != nullptr) {
        double ax = 0.0, ay = 0.0;
        if (RagNoFlyDetour((double)px, (double)py, (double)tgtX, (double)tgtY,
                           (double)tears->x, (double)tears->y,
                           RAG_NOFLY_RADIUS, &ax, &ay)) {
            *aimX = (int32_t)ax;
            *aimY = (int32_t)ay;
        }
    }
}
