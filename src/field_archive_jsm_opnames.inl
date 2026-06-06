// field_archive_jsm_opnames.inl — opcode-name lookup for script dump diagnostics.
// Included from field_archive_jsm.inl. Do not compile independently.

// v0.12.17: Opcode name table for script dump diagnostic
static const char* GetOpcodeName(uint16_t op) {
    switch (op) {
        case 0x001: return "JMP";
        case 0x002: return "JPF";
        case 0x003: return "JMPB";
        case 0x004: return "JMPF";
        case 0x005: return "LBL";
        case 0x006: return "RET";
        case 0x007: return "PSHM_W";
        case 0x008: return "POPM_W";
        case 0x009: return "PSHM_B";
        case 0x00A: return "PSHM_L";
        case 0x00B: return "POPM_L";
        case 0x00C: return "PSHSM_W";
        case 0x00D: return "PSHSM_B";
        case 0x00E: return "PSHAC";
        case 0x010: return "CAL";
        case 0x012: return "PSHN_L2";
        case 0x014: return "REQ";
        case 0x015: return "REQSW";
        case 0x016: return "REQEW";
        case 0x01A: return "UNUSE";
        case 0x01C: return "EXT_DISPATCH";
        case 0x01D: return "SET";
        case 0x01E: return "SET3";
        case 0x025: return "LADDERUP";
        case 0x026: return "LADDERDOWN";
        case 0x029: return "MAPJUMP";
        case 0x02A: return "MAPJUMP3";
        case 0x02B: return "SETMODEL";
        case 0x02C: return "BASEANIME";
        case 0x038: return "DISCJUMP";
        case 0x039: return "SETLINE";
        case 0x03E: return "MOVE";
        case 0x047: return "MES";
        case 0x04A: return "ASK";
        case 0x057: return "TALKON";
        case 0x058: return "TALKOFF";
        case 0x05C: return "MAPJUMPO";
        case 0x060: return "SHOW";
        case 0x061: return "HIDE";
        case 0x065: return "AMES";
        case 0x069: return "BATTLE";
        case 0x06F: return "AASK";
        case 0x099: return "BGDRAW";
        case 0x09A: return "BGOFF";
        case 0x0E5: return "USE";
        case 0x10A: return "SETCAMERA";
        case 0x10D: return "WORLDMAPJUMP";
        case 0x11B: return "MENUPHS";
        case 0x11E: return "MENUSHOP";
        case 0x125: return "ADDITEM";
        case 0x129: return "MENUNAME";
        case 0x12E: return "MENUSAVE";
        case 0x12F: return "SAVEENABLE";
        case 0x130: return "PHSENABLE";
        case 0x137: return "DRAWPOINT";
        case 0x13A: return "CARDGAME";
        case 0x142: return "DOORLINEOFF";
        case 0x143: return "DOORLINEON";
        case 0x14E: return "PARTICLEON";
        case 0x14F: return "PARTICLEOFF";
        case 0x155: return "SETDRAWPOINT";
        default:    return nullptr;  // unknown
    }
}
