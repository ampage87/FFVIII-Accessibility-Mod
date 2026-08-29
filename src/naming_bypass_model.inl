// naming_bypass_model.inl -- WHICH MODE THE GAME RETURNS TO WHEN THE MOD SKIPS
// THE GF NAMING SCREEN (#naming-bypass).
//
// Included from battle_tts_victory.inl, and compiled standalone by
// tests/naming_bypass_test.cpp. Nothing here touches game memory.
//
// ============================================================================
// THE BUG THIS EXISTS TO FIX, IN AARON'S WORDS
// ============================================================================
//
// *"I went and played the battle against Jumbo Cactuar. I won the battle, but
// afterward the game seemed to glitch. I heard it say a field name like I was
// at Edea's House, but I could hear the world map music. The catalog would not
// populate, and even though I could hear world map music it would not let me
// save when I opened the menu."*
//
// The 2026-08-25 log has the whole thing in five lines:
//
//     22:42:13  [VICTORY-THREAD] Mode: 2 -> 3          <- world map into a battle
//     23:31:51  [NAME-BYPASS] New GF detected (idx=13), bypass armed
//     23:31:51  [NAME-BYPASS] Patched 0x00470AB9: 0B -> 01
//     23:32:08  [VICTORY-THREAD] Mode: 4 -> 1          <- FIELD, from the world map
//     23:32:08  [fieldload] id=505 name='ehenter2'     <- Edea's House 2
//
// v0.13.46 skips the naming screen by rewriting the immediate in the ONE
// instruction that puts the game into mode 11:
//
//     0x00470AB2  mov word ptr [0x1cd8fc6], 0xb     ; 0xb = MODE_11, the naming UI
//
// and it wrote a **constant 1 -- MODE_FIELD**. From a field battle that is
// right: the party goes back to the room it was standing in. From a WORLD MAP
// battle it is wrong, and Jumbo Cactuar is fought on the world map. The engine
// entered field mode with whatever field id was still in the slot -- 505,
// Edea's House 2, from an earlier visit -- and every symptom follows: the world
// map music kept playing because the world map was never properly left or
// re-entered, the catalog stayed empty because WorldMap::Update correctly saw
// that the game was not on the world map, and Save was refused because in a
// field you may only save at a save point.
//
// ============================================================================
// WHAT THE ENGINE DOES WHEN THE NAMING SCREEN IS ALLOWED TO RUN
// ============================================================================
//
// The block the patch sits in, disassembled from FF8_EN.exe:
//
//     00470A9D  mov  eax, 1
//     00470AA2  cmp  word [0x1ce0758], ax        ; <-- THE TEST
//     00470AA9  mov  word [0x1ce0758], 3
//     00470AB2  mov  word [0x1cd8fc6], 0xb       ; <-- the patched instruction
//     00470ABB  jne  0x470acb
//     00470AC0  mov  word [0x1cd2ed4], ax        ; equal   -> 1
//     00470ACE  mov  word [0x1cd2ed4], 2         ; not eq  -> 2
//
// and 0x1cd2ed4 is read in exactly one place in the whole executable -- the
// naming screen's exit:
//
//     00470C47  mov  dx, word [0x1cd2ed4]
//     00470C54  mov  word [0x1cd8fc6], dx        ; restore the game mode
//
// So the engine decides, before it opens the naming screen, which mode to
// return to afterwards, and it decides it with `word [0x1ce0758] == 1`: equal
// means FIELD, anything else means WORLD MAP. The same test appears again a
// hundred and fifty bytes later, at 0x00470C8D, choosing between mode 1 at
// 0x00470CD0 and mode 2 at 0x00470CE2. Two independent sites, one rule.
//
// **AND THE PATCH REPRODUCES THE POST-NAMING STATE EXACTLY.** The naming path
// sets `[0x1ce0758] = 3` at 0x00470AA9 and the exit at 0x00470C47 never touches
// it again, so a player who completes the naming screen reaches the world map
// with 0x1ce0758 == 3 -- which is precisely where our patched instruction
// leaves it. The only bytes we skip are 0x1cd2ed4 and 0x1cd2eec, and both are
// read only by the exit path we no longer take.
//
// ============================================================================
// SO WHY IS THE OBSERVED MODE THE PRIMARY SOURCE, AND NOT THAT TEST?
// ============================================================================
//
// Because the mod already knew the answer and threw it away. The victory
// thread polls the game mode at 30 Hz and logs every change; `Mode: 2 -> 3` at
// 22:42:13 is the battle starting, and the 2 is the world map. That is a direct
// observation of where the party was standing, it needs no reverse engineering,
// and it cannot be wrong about the thing it measures.
//
// The engine's own test is kept as the fallback -- for a battle already in
// progress when the mod loaded, where there is no observation to use -- and it
// is logged next to the observation on every bypass, so a BAT says whether the
// two ever disagree.
//
// If neither is available the mod DOES NOT PATCH. A naming screen Aaron has to
// navigate is a bad outcome; a save file that believes it is in a room the
// party has never entered is a worse one, and guessing is how you get the
// second. The mode-11 warning already announces "Naming screen appeared."
// ============================================================================

namespace NamingBypassModel {

// The instruction and the byte inside it.
static const uint32_t NB_PATCH_INSN_ADDR = 0x00470AB2u;  // mov word [0x1cd8fc6], imm16
static const uint32_t NB_PATCH_ADDR      = 0x00470AB9u;  // the imm16's low byte
static const uint8_t  NB_ORIGINAL_IMM    = 0x0Bu;        // MODE_11 -- the naming UI

// The two modes a battle can return to. These are FF8Addresses::MODE_FIELD and
// MODE_WORLDMAP; spelled out here so the pure model compiles alone.
static const uint8_t NB_MODE_FIELD    = 1u;
static const uint8_t NB_MODE_WORLDMAP = 2u;

// The word the engine tests at 0x00470AA2 and again at 0x00470C8D.
static const uint32_t NB_RETURN_KIND_ADDR  = 0x01CE0758u;
static const int      NB_RETURN_KIND_FIELD = 1;

static uint8_t NbModeFromReturnKind(int returnKind)
{
    return (returnKind == NB_RETURN_KIND_FIELD) ? NB_MODE_FIELD : NB_MODE_WORLDMAP;
}

// A mode the party can actually be standing in when a battle starts. Mode 6 is
// the menu -- the 2026-08-25 log goes 2 -> 6 -> 2 -> 3 on the way into the
// Cactuar fight -- and 3/4/5/11/100 are the battle and its aftermath. Only 1
// and 2 are places a battle can return to.
static bool NbIsHostMode(int mode)
{
    return mode == (int)NB_MODE_FIELD || mode == (int)NB_MODE_WORLDMAP;
}

// Where the chosen mode came from, so the log can say.
enum NbSource {
    NB_SRC_NONE     = 0,   // nothing to go on -- DO NOT PATCH
    NB_SRC_OBSERVED = 1,   // the mode the party was in when the battle started
    NB_SRC_ENGINE   = 2    // the engine's own post-naming test
};

// Returns the mode to write into the immediate, or 0 with *srcOut = NB_SRC_NONE
// when the mod has nothing trustworthy to write and must leave the game alone.
static uint8_t NbWantedMode(bool haveObserved, int observedMode,
                            bool engineReadOk, int returnKind,
                            int* srcOut)
{
    if (haveObserved && NbIsHostMode(observedMode)) {
        if (srcOut) *srcOut = (int)NB_SRC_OBSERVED;
        return (uint8_t)observedMode;
    }
    if (engineReadOk) {
        if (srcOut) *srcOut = (int)NB_SRC_ENGINE;
        return NbModeFromReturnKind(returnKind);
    }
    if (srcOut) *srcOut = (int)NB_SRC_NONE;
    return 0u;
}

// True when the two sources disagree and both were available -- worth a log
// line, because it is the only way we would ever learn that the engine test is
// not what this file claims it is.
static bool NbSourcesDisagree(bool haveObserved, int observedMode,
                              bool engineReadOk, int returnKind)
{
    if (!haveObserved || !NbIsHostMode(observedMode) || !engineReadOk) return false;
    return (uint8_t)observedMode != NbModeFromReturnKind(returnKind);
}

}  // namespace NamingBypassModel
