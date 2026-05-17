// field_archive_jsm_helpers.inl — low-level JSM utility functions.
// Included from field_archive_jsm.inl. Do not compile independently.

// v0.07.64: Look up field ID by internal name (e.g. "bghall_1" → 165).
// Linear search through the FL-derived field name list.
int GetFieldIdByInternalName(const char* internalName)
{
    if (!internalName || !s_initialized) return -1;
    // s_fieldNames stores lowercase basenames indexed by field ID.
    // Do case-insensitive compare.
    for (size_t i = 0; i < s_fieldNames.size(); i++) {
        if (_stricmp(internalName, s_fieldNames[i].c_str()) == 0)
            return (int)i;
    }
    return -1;
}

// Byte-swap a 32-bit value from big-endian to little-endian.
// FF8 JSM script instructions are stored big-endian (PS1 heritage).
static uint32_t SwapBE32(uint32_t v)
{
    return ((v & 0xFF) << 24) | ((v & 0xFF00) << 8) |
           ((v >> 8) & 0xFF00) | ((v >> 24) & 0xFF);
}

// Decode a single 32-bit JSM instruction (already byte-swapped to native).
// Returns true if it's an opcode (bit 31 set), false if PSHN_L.
//
// Encoding (confirmed from myst6re's deling JsmExpression.cpp):
//   Bit 31 = 1: opcode. ID = bits 1-14 (shift right 1, mask 0x3FFF). Bit 0 = sub flag.
//   Bit 31 = 0: PSHN_L (push literal). Value = bits 0-30, sign-extended from bit 30.
//   All opcode parameters come from the stack — there are NO inline parameters in the word.
static bool DecodeJSMInstruction(uint32_t word, uint16_t& opcodeOut, uint16_t& paramOut, int32_t& pushValueOut)
{
    if (word & 0x80000000) {
        // Opcode: bits 1-14 = opcode ID, bit 0 = sub-opcode flag
        opcodeOut  = (uint16_t)((word >> 1) & 0x3FFF);
        paramOut   = (uint16_t)(word & 1);  // sub flag only
        pushValueOut = 0;
        return true;
    } else {
        // PSHN_L: bits 0-30 = literal value (signed)
        opcodeOut = 0;
        paramOut  = 0;
        // Sign-extend from 31 bits to 32.
        pushValueOut = (int32_t)(word & 0x7FFFFFFF);
        if (word & 0x40000000) pushValueOut |= (int32_t)0x80000000;  // sign bit
        return false;
    }
}
