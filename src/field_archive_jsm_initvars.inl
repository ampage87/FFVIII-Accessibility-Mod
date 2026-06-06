// field_archive_jsm_initvars.inl - public API for init-var write lookup.
// Included from field_archive_jsm.inl after scan.inl runs. Do not compile
// independently.
//
// v0.17.7.2: Exposes the s_initVarMaps[] table (populated during
// ScanJSMScripts) to callers outside FieldArchive namespace. Used by the
// MAPJUMP destination diagnostic in HookedFieldScriptsInit to find which
// init-script writes the destination field ID for an unresolved PSHM_W
// address.
//
// Both functions are read-only and safe to call any time after
// ScanJSMScripts has populated s_initVarMaps. They return 0 if no matches.

int LookupInitVarWrites(int16_t addr, InitVarWriter* outEntries, int maxEntries)
{
    int total = 0;
    int written = 0;
    // Walk all 128 entity slots. Each slot's s_initVarMaps[e] records up to
    // 64 (PUSH literal + POPM_W) pairs from that entity's init method. We
    // accept matches on either addr or (addr & 0xFFFF) since callers may
    // pass the low 16 bits of a marker.
    int16_t mask = addr;
    for (int e = 0; e < 128; e++) {
        const EntityVarMap& vm = s_initVarMaps[e];
        for (int w = 0; w < vm.count; w++) {
            // Match either the full address or the low-16-bit form.
            if ((int16_t)vm.writes[w].addr == mask) {
                total++;
                if (written < maxEntries && outEntries != nullptr) {
                    outEntries[written].entityIdx = e;
                    outEntries[written].value     = vm.writes[w].value;
                    written++;
                }
            }
        }
    }
    return total;
}

int EnumerateInitVars(InitVarTuple* outEntries, int maxEntries)
{
    int total = 0;
    int written = 0;
    for (int e = 0; e < 128; e++) {
        const EntityVarMap& vm = s_initVarMaps[e];
        for (int w = 0; w < vm.count; w++) {
            total++;
            if (written < maxEntries && outEntries != nullptr) {
                outEntries[written].entityIdx = e;
                outEntries[written].addr      = vm.writes[w].addr;
                outEntries[written].value     = vm.writes[w].value;
                written++;
            }
        }
    }
    return total;
}
