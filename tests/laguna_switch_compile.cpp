// laguna_switch_compile.cpp -- the Laguna junction-party screen, checked against
// the bytes of FF8_EN.exe that define it.
//
//   g++ -std=c++17 -O0 -Isrc -o laguna_switch_compile tests/laguna_switch_compile.cpp
//
// Every offset this screen's reader uses is an operand inside one of the seven
// fragments below, so the probe DECODES them rather than restating the model.
// Retyping the offsets into a fixture would only prove that two files agree.
//
// v0.43.0 (#104).

#include <cstdio>
#include <cstdint>
#include <cstring>
#include <string>

namespace Log { static void Menu(const char*, ...) {} }

#include "laguna_switch_model.inl"

static int bad = 0;
static void check(bool ok, const char* what)
{
    if (!ok) { std::printf("  BAD: %s\n", what); bad++; }
}
static void checkStr(const char* got, const char* want, const char* what)
{
    if (std::strcmp(got, want) != 0) {
        std::printf("  BAD: %s -- got \"%s\", want \"%s\"\n", what, got, want);
        bad++;
    }
}

static uint32_t le32(const uint8_t* p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

// ===========================================================================
// PART 1 -- the chain from the field script to the module, in the engine's bytes
// ===========================================================================

// 0x004B3161: the menu-id switch.
//     8D 48 FF              lea ecx, [eax-1]
//     83 F9 1C              cmp ecx, 0x1C
//     0F 87 ...             ja  default
//     33 D2                 xor edx, edx
//     8A 91 E8 32 4B 00     mov dl, [ecx + 0x004B32E8]
static const uint8_t MENUID_SWITCH[] = {
    0x8D, 0x48, 0xFF, 0x83, 0xF9, 0x1C, 0x0F, 0x87, 0x29, 0x01, 0x00, 0x00,
    0x33, 0xD2, 0x8A, 0x91, 0xE8, 0x32, 0x4B, 0x00
};
// The index byte the table holds for menu id 0x16, and the target it selects.
static const uint8_t  MENUID_0x16_INDEX = 0x05;
static const uint32_t MENUID_JUMP_5     = 0x004B3190;

// 0x004B3190: what menu id 0x16 does.
//     6A 00 / E8 ..         push 0 ; call
//     6A 01 / E8 ..         push 1 ; call
//     8A 44 24 10           mov al, [esp+0x10]        <- the field's parameter
//     A2 99 55 D7 01        mov [0x01D75599], al      <- the SLOT MASK
//     E8 ..                 call
//     6A 00 / 6A 0D         push 0 ; push 0x0D        <- the SCREEN ID
//     E8 .. / 83 C4 10 / C3
static const uint8_t MENU_0x16_BODY[] = {
    0x6A, 0x00, 0xE8, 0x39, 0x8D, 0xFF, 0xFF, 0x6A, 0x01, 0xE8, 0x32, 0x8D,
    0xFF, 0xFF, 0x8A, 0x44, 0x24, 0x10, 0xA2, 0x99, 0x55, 0xD7, 0x01, 0xE8,
    0x64, 0x8D, 0xFF, 0xFF, 0x6A, 0x00, 0x6A, 0x0D, 0xE8, 0x7B, 0xA9, 0x00,
    0x00, 0x83, 0xC4, 0x10, 0xC3
};
// The pair table at 0x00B87ED8 is {creator, layer} per SCREEN id. Pair 13:
static const uint32_t SCREEN_13_CREATOR = 0x004E8A30;

// 0x004BE540: the module allocator, which is also the pool's definition.
//     BA C8 6B D7 01        mov edx, 0x01D76BC8        <- pool base
//     33 C0                 xor eax, eax
//     8A 4A 12              mov cl, [edx+0x12]         <- in-use flag
//     84 C9 / 74 0C
//     40                    inc eax
//     83 C2 78              add edx, 0x78              <- stride
//     83 F8 0A              cmp eax, 0x0A              <- ten slots
//     ...
//     A1 48 6B D7 01        mov eax, [0x01D76B48]      <- MRU head
//     ...
//     8B 44 24 08           mov eax, [esp+8]           (arg 1 = update fn)
//     8B 4C 24 0C           mov ecx, [esp+0xC]         (arg 2 = draw fn)
//     89 42 08              mov [edx+0x08], eax        <- UPDATE FN AT +0x08
//     89 4A 0C              mov [edx+0x0C], ecx
static const uint8_t ALLOCATOR[] = {
    0xBA, 0xC8, 0x6B, 0xD7, 0x01, 0x33, 0xC0, 0x8A, 0x4A, 0x12, 0x84, 0xC9,
    0x74, 0x0C, 0x40, 0x83, 0xC2, 0x78, 0x83, 0xF8, 0x0A, 0x7C, 0xF0, 0x33,
    0xC0, 0xC3, 0x85, 0xD2, 0xC6, 0x42, 0x12, 0x01, 0x75, 0x03, 0x33, 0xC0,
    0xC3, 0xA1, 0x48, 0x6B, 0xD7, 0x01, 0x57, 0x89, 0x02, 0xC7, 0x42, 0x04,
    0x48, 0x6B, 0xD7, 0x01, 0x89, 0x50, 0x04, 0x8D, 0x7A, 0x20, 0xB9, 0x16,
    0x00, 0x00, 0x00, 0x33, 0xC0, 0xF3, 0xAB, 0x8B, 0x44, 0x24, 0x08, 0x8B,
    0x4C, 0x24, 0x0C, 0x89, 0x42, 0x08, 0x89, 0x4A, 0x0C
};

// 0x004E8AE6: the creator laying out the slots and the grid.
//     8D 46 3B              lea eax, [esi+0x3B]        <- GRID base
//     83 C9 FF              or  ecx, -1
//     8B D0                 mov edx, eax
//     C6 46 38 00           mov byte [esi+0x38], 0     <- SLOT base, and SQUALL
//     88 5E 39              mov [esi+0x39], bl         (bl = 0xFF)
//     88 5E 3A              mov [esi+0x3A], bl
//     89 0A                 mov [edx], ecx
static const uint8_t CREATOR_INIT[] = {
    0x8D, 0x46, 0x3B, 0x83, 0xC9, 0xFF, 0x8B, 0xD0, 0xC6, 0x46, 0x38, 0x00,
    0x88, 0x5E, 0x39, 0x88, 0x5E, 0x3A, 0x89, 0x0A
};

// 0x004E8E4B: picking on the LEFT grid.
//     33 C0 / 33 C9
//     8A 46 34              mov al, [esi+0x34]         <- grid cursor
//     8A 4C 30 3B           mov cl, [eax+esi+0x3B]     <- grid[cursor]
static const uint8_t GRID_PICK[] = {
    0x33, 0xC0, 0x33, 0xC9, 0x8A, 0x46, 0x34, 0x8A, 0x4C, 0x30, 0x3B
};

// 0x004E8E5A: picking on the RIGHT slot column.
//     33 D2
//     8A 56 35              mov dl, [esi+0x35]         <- slot cursor
//     33 C0
//     8A 44 32 38           mov al, [edx+esi+0x38]     <- slots[cursor]
static const uint8_t SLOT_PICK[] = {
    0x33, 0xD2, 0x8A, 0x56, 0x35, 0x33, 0xC0, 0x8A, 0x44, 0x32, 0x38
};

// 0x004E8F88 -- state 4, the moment a pick is lifted. Four instructions, and
// they are the whole of "there are two cursors":
//     8A 4E 32   mov cl, [esi+0x32]      the live focus
//     8A 56 34   mov dl, [esi+0x34]      the live grid cursor
//     8A 46 35   mov al, [esi+0x35]      the live slot cursor
//     C6 46 43 03  mov byte [esi+0x43], 3
//     88 4E 33 / 88 56 36 / 88 46 37     COPIED into the second trio
//     66 C7 46 10 05 00                  state = 5
static const uint8_t STATE4_LIFT[] = {
    0x8A, 0x4E, 0x32, 0x8A, 0x56, 0x34, 0x8A, 0x46, 0x35, 0xC6, 0x46, 0x43,
    0x03, 0x88, 0x4E, 0x33, 0x88, 0x56, 0x36, 0x88, 0x46, 0x37, 0x66, 0xC7,
    0x46, 0x10, 0x05, 0x00
};
// 0x004E8FA4 -- state 5 reads the focus from +0x33 and the grid cursor from
// +0x36, not from +0x32 / +0x34.
static const uint8_t STATE5_READ[] = {
    0x8A, 0x46, 0x33, 0x84, 0xC0, 0x0F, 0x85, 0xBC, 0x00, 0x00, 0x00, 0x33,
    0xC0, 0x8A, 0x46, 0x36, 0x8B, 0xD8
};
// 0x004E9061 -- and writes the moved grid cursor back to +0x36.
static const uint8_t STATE5_WRITE_GRID[] = {
    0xC0, 0xE0, 0x02, 0x02, 0xC3, 0x88, 0x46, 0x36, 0xEB, 0x69, 0xE8
};
// 0x004E908A -- the slot half: +0x37 in, through the mask walker, +0x37 out.
static const uint8_t STATE5_SLOT[] = {
    0x33, 0xC9, 0x8A, 0x4E, 0x37, 0x51, 0x57, 0xE8, 0x0A, 0x2C, 0xFC, 0xFF,
    0x83, 0xC4, 0x0C, 0x88, 0x46, 0x37
};

// 0x004E9342 -- state 6 sets the mode and hands over to state 7, whose whole
// body walks +0x31 up and down the slot mask:
//     C6 46 43 02        mov byte [esi+0x43], 2       <- MODE 2
//     66 89 5E 10        mov word [esi+0x10], bx      (bx = 7)
//     E8 ..              call 0x004B2DB0              the slot mask
//     ...
//     8A 56 31           mov dl, [esi+0x31]           <- THE SECOND CURSOR
//     52 57 E8 ..        push / push / call 0x004ABCA0 (next set bit)
//     88 46 31           mov [esi+0x31], al           <- written back
static const uint8_t STATE6_MODE[] = {
    0xC6, 0x46, 0x43, 0x02, 0x66, 0x89, 0x5E, 0x10
};
static const uint8_t STATE7_MOVE[] = {
    0x33, 0xD2, 0x8A, 0x56, 0x31, 0x52, 0x57, 0xE8, 0x2B, 0x29, 0xFC, 0xFF,
    0x83, 0xC4, 0x0C, 0x88, 0x46, 0x31
};
// The call in the middle of that is the mask walker itself: E8 rel32 from
// 0x004E9370 lands on 0x004ABCA0, and a fixture that had invented the
// displacement would not survive this check.
static const uint32_t STATE7_MOVE_VA   = 0x004E9369;
static const uint32_t MASK_NEXT_SET_BIT = 0x004ABCA0;
// 0x004E94E8 -- the swap, which is what says which end is which:
//     8A 46 32     mov al, [esi+0x32]        the FROZEN focus
//     8A 46 31     mov al, [esi+0x31]        the DESTINATION slot
//     8A 56 34     mov dl, [esi+0x34]        the FROZEN grid cursor
//     8A 54 32 3B  mov dl, [edx+esi+0x3B]    the character in hand
//     8D 4C 30 38  lea ecx, [eax+esi+0x38]   &slots[destination]
static const uint8_t SWAP_BODY[] = {
    0x8A, 0x46, 0x32, 0x83, 0xC4, 0x04, 0x84, 0xC0, 0x75, 0x30, 0x33, 0xC0,
    0x33, 0xD2, 0x8A, 0x46, 0x31, 0x8A, 0x56, 0x34, 0x8A, 0x54, 0x32, 0x3B,
    0x8D, 0x4C, 0x30, 0x38
};

static bool findByte(const uint8_t* b, size_t n, const uint8_t* pat, size_t pn, size_t* at)
{
    for (size_t i = 0; i + pn <= n; i++)
        if (std::memcmp(b + i, pat, pn) == 0) { *at = i; return true; }
    return false;
}

static void EngineFragments()
{
    size_t at;

    // ---- the module pool -------------------------------------------------
    check(ALLOCATOR[0] == 0xBA && le32(ALLOCATOR + 1) == LSW_POOL_BASE,
          "**the allocator's pool base is the one the reader walks**");
    // add edx, imm8 -- the stride
    static const uint8_t ADD_EDX[] = { 0x83, 0xC2 };
    check(findByte(ALLOCATOR, sizeof(ALLOCATOR), ADD_EDX, 2, &at) &&
          ALLOCATOR[at + 2] == LSW_POOL_STRIDE,
          "the pool stride is decoded from `add edx, imm8`");
    static const uint8_t CMP_EAX[] = { 0x83, 0xF8 };
    check(findByte(ALLOCATOR, sizeof(ALLOCATOR), CMP_EAX, 2, &at),
          "the slot count is decoded from `cmp eax, imm8`");
    const int slots = ALLOCATOR[at + 2];
    check((uintptr_t)(LSW_POOL_BASE + slots * LSW_POOL_STRIDE) == LSW_POOL_END,
          "**pool end == base + count * stride** -- the bound the walk trusts");
    static const uint8_t MOV_EAX_ABS[] = { 0xA1 };
    check(findByte(ALLOCATOR, sizeof(ALLOCATOR), MOV_EAX_ABS, 1, &at) &&
          le32(ALLOCATOR + at + 1) == LSW_LIST_HEAD,
          "the MRU head is decoded from `mov eax, [imm32]`");
    // mov [edx+disp8], eax where eax was loaded from [esp+8] -- the first arg.
    static const uint8_t MOV_ARG1[] = { 0x8B, 0x44, 0x24, 0x08 };
    check(findByte(ALLOCATOR, sizeof(ALLOCATOR), MOV_ARG1, 4, &at),
          "the allocator loads its first argument");
    check(ALLOCATOR[at + 8] == 0x89 && ALLOCATOR[at + 9] == 0x42 &&
          ALLOCATOR[at + 10] == LSW_MOD_UPDATE_FN,
          "**the first argument -- the update function -- is stored at +0x08**, "
          "which is the field the walk matches on");

    // ---- the menu id -> screen id -> creator chain ------------------------
    check(MENUID_SWITCH[0] == 0x8D && MENUID_SWITCH[1] == 0x48 && MENUID_SWITCH[2] == 0xFF,
          "the menu-id switch biases by one (`lea ecx,[eax-1]`)");
    check(MENUID_SWITCH[3] == 0x83 && MENUID_SWITCH[4] == 0xF9 && MENUID_SWITCH[5] == 0x1C,
          "and covers ids 1..0x1D");
    check(0x16 - 1 <= 0x1C, "menu id 0x16 is inside that range");
    check(MENUID_0x16_INDEX == 5 && MENUID_JUMP_5 == 0x004B3190,
          "menu id 0x16 selects the handler at 0x004B3190");

    // mov [imm32], al -- the slot mask address, decoded from the handler.
    static const uint8_t MOV_ABS_AL[] = { 0xA2 };
    check(findByte(MENU_0x16_BODY, sizeof(MENU_0x16_BODY), MOV_ABS_AL, 1, &at) &&
          le32(MENU_0x16_BODY + at + 1) == LSW_SLOT_MASK_ADDR,
          "**the slot mask address is the byte that handler writes the field "
          "script's parameter into**");
    // The last `push imm8` before the call is the screen id.
    int screenId = -1;
    for (size_t i = 0; i + 1 < sizeof(MENU_0x16_BODY); i++)
        if (MENU_0x16_BODY[i] == 0x6A && MENU_0x16_BODY[i + 1] != 0x00) screenId = MENU_0x16_BODY[i + 1];
    check(screenId == 0x0D, "menu id 0x16 pushes screen id 0x0D");
    check(SCREEN_13_CREATOR == 0x004E8A30,
          "and pair 13 of the screen table is this module's creator");

    // ---- the offsets the reader uses --------------------------------------
    // lea eax, [esi+disp8] = 8D 46 disp8
    check(CREATOR_INIT[0] == 0x8D && CREATOR_INIT[1] == 0x46 &&
          CREATOR_INIT[2] == LSWO_GRID,
          "**the grid base is decoded from `lea eax,[esi+disp8]` in the creator**");
    // mov byte [esi+disp8], imm8 = C6 46 disp8 imm8
    static const uint8_t MOVB_ESI[] = { 0xC6, 0x46 };
    check(findByte(CREATOR_INIT, sizeof(CREATOR_INIT), MOVB_ESI, 2, &at) &&
          CREATOR_INIT[at + 2] == LSWO_SLOTS,
          "the slot base is decoded from `mov byte [esi+disp8], imm8`");
    check(CREATOR_INIT[at + 3] == 0,
          "**and the value it writes there is 0 -- Squall, preset into Laguna's "
          "slot by the creator itself**");
    check(CREATOR_INIT[12] == 0x88 && CREATOR_INIT[13] == 0x5E &&
          CREATOR_INIT[14] == LSWO_SLOTS + 1 &&
          CREATOR_INIT[15] == 0x88 && CREATOR_INIT[16] == 0x5E &&
          CREATOR_INIT[17] == LSWO_SLOTS + 2,
          "the other two slots are the next two bytes");

    check(GRID_PICK[4] == 0x8A && GRID_PICK[5] == 0x46 && GRID_PICK[6] == LSWO_GRID_CUR,
          "the grid cursor offset is decoded from `mov al,[esi+disp8]`");
    check(GRID_PICK[7] == 0x8A && GRID_PICK[8] == 0x4C && GRID_PICK[9] == 0x30 &&
          GRID_PICK[10] == LSWO_GRID,
          "**and the grid is indexed BY that cursor** (`mov cl,[eax+esi+disp8]`)");

    check(SLOT_PICK[2] == 0x8A && SLOT_PICK[3] == 0x56 && SLOT_PICK[4] == LSWO_SLOT_CUR,
          "the slot cursor offset is decoded from `mov dl,[esi+disp8]`");
    check(SLOT_PICK[7] == 0x8A && SLOT_PICK[8] == 0x44 && SLOT_PICK[9] == 0x32 &&
          SLOT_PICK[10] == LSWO_SLOTS,
          "**and the slots are indexed BY that cursor** (`mov al,[edx+esi+disp8]`)");
    check(LSWO_GRID == LSWO_SLOTS + LSW_SLOT_COUNT,
          "the grid starts immediately after the three slots");

    // ---- THE SECOND CURSOR -------------------------------------------------
    //
    // v0.43.0 read the first trio throughout and went silent for the whole
    // second half of every placement. These four fragments are why.
    check(STATE4_LIFT[0] == 0x8A && STATE4_LIFT[1] == 0x4E && STATE4_LIFT[2] == LSWO_FOCUS &&
          STATE4_LIFT[3] == 0x8A && STATE4_LIFT[4] == 0x56 && STATE4_LIFT[5] == LSWO_GRID_CUR &&
          STATE4_LIFT[6] == 0x8A && STATE4_LIFT[7] == 0x46 && STATE4_LIFT[8] == LSWO_SLOT_CUR,
          "state 4 reads the live cursor out of the first trio");
    check(STATE4_LIFT[9] == 0xC6 && STATE4_LIFT[10] == 0x46 &&
          STATE4_LIFT[11] == LSWO_PENDING && STATE4_LIFT[12] == 3,
          "sets +0x43 to 3");
    check(STATE4_LIFT[13] == 0x88 && STATE4_LIFT[14] == 0x4E && STATE4_LIFT[15] == LSWO_PEND_FOCUS &&
          STATE4_LIFT[16] == 0x88 && STATE4_LIFT[17] == 0x56 && STATE4_LIFT[18] == LSWO_PEND_GRID &&
          STATE4_LIFT[19] == 0x88 && STATE4_LIFT[20] == 0x46 && STATE4_LIFT[21] == LSWO_PEND_SLOT,
          "**and COPIES all three into the second trio**");
    check(STATE5_READ[0] == 0x8A && STATE5_READ[1] == 0x46 && STATE5_READ[2] == LSWO_PEND_FOCUS,
          "**state 5 then takes the focus from the SECOND trio**");
    check(STATE5_READ[13] == 0x8A && STATE5_READ[14] == 0x46 && STATE5_READ[15] == LSWO_PEND_GRID,
          "and the grid cursor from it");
    check(STATE5_WRITE_GRID[5] == 0x88 && STATE5_WRITE_GRID[6] == 0x46 &&
          STATE5_WRITE_GRID[7] == LSWO_PEND_GRID,
          "**writing the moved cursor back there** -- the first trio never moves again");
    check(STATE5_SLOT[2] == 0x8A && STATE5_SLOT[3] == 0x4E && STATE5_SLOT[4] == LSWO_PEND_SLOT &&
          STATE5_SLOT[15] == 0x88 && STATE5_SLOT[16] == 0x46 && STATE5_SLOT[17] == LSWO_PEND_SLOT,
          "and the slot half goes in and out of +0x37 the same way");

    // ---- MODE 2, which is the one this screen actually uses ---------------
    check(STATE6_MODE[0] == 0xC6 && STATE6_MODE[1] == 0x46 &&
          STATE6_MODE[2] == LSWO_PENDING && STATE6_MODE[3] == LSW_MODE_DEST,
          "**state 6 sets the mode byte to 2**, not to 3 -- v0.44.0 watched for 3 "
          "and this screen never produces it");
    check(STATE7_MOVE[2] == 0x8A && STATE7_MOVE[3] == 0x56 &&
          STATE7_MOVE[4] == LSWO_DEST_SLOT,
          "**state 7's cursor is +0x31**");
    check(STATE7_MOVE[15] == 0x88 && STATE7_MOVE[16] == 0x46 &&
          STATE7_MOVE[17] == LSWO_DEST_SLOT,
          "and it is written back there after the mask walk");
    {
        // E8 rel32 at offset 7 -- decode where the walk actually goes.
        const uint32_t site   = STATE7_MOVE_VA + 7;
        const uint32_t target = site + 5 + le32(STATE7_MOVE + 8);
        check(target == MASK_NEXT_SET_BIT,
              "**and the call between them is the mask walker** -- so +0x31 is a "
              "BIT INDEX in the slot mask, not an ordinal");
    }

    // The swap names both ends in six instructions.
    check(SWAP_BODY[0] == 0x8A && SWAP_BODY[1] == 0x46 && SWAP_BODY[2] == LSWO_FOCUS,
          "the swap branches on the FROZEN focus");
    check(SWAP_BODY[14] == 0x8A && SWAP_BODY[15] == 0x46 &&
          SWAP_BODY[16] == LSWO_DEST_SLOT,
          "**it takes the destination slot from +0x31**");
    check(SWAP_BODY[17] == 0x8A && SWAP_BODY[18] == 0x56 &&
          SWAP_BODY[19] == LSWO_GRID_CUR,
          "and the character in hand from the FROZEN grid cursor");
    check(SWAP_BODY[20] == 0x8A && SWAP_BODY[21] == 0x54 && SWAP_BODY[22] == 0x32 &&
          SWAP_BODY[23] == LSWO_GRID,
          "read out of the grid");
    check(SWAP_BODY[24] == 0x8D && SWAP_BODY[25] == 0x4C && SWAP_BODY[26] == 0x30 &&
          SWAP_BODY[27] == LSWO_SLOTS,
          "**and the destination is an index into the slot array** -- so +0x31 is "
          "a slot number and the browse trio is the pick");
}

// ===========================================================================
// PART 2 -- what it says
// ===========================================================================

static LagunaSwitchView Board()
{
    LagunaSwitchView v;
    std::memset(&v, 0, sizeof(v));
    v.slotMask  = 0x07;                       // all three slots exist
    v.availMask = 0x3F;
    v.slots[0] = 0; v.slots[1] = 0xFF; v.slots[2] = 0xFF;   // as the creator leaves it
    // Squall, Zell, Irvine, Quistis, Rinoa, Selphie available; two empty cells.
    const unsigned char g[8] = { 0, 1, 2, 3, 4, 5, 0xFF, 0xFF };
    std::memcpy(v.grid, g, sizeof(g));
    return v;
}

int main()
{
    EngineFragments();

    char buf[256];
    LagunaSwitchView v = Board();

    // ---- the board as the creator leaves it -------------------------------
    LswAllSlotsLine(v, buf, sizeof(buf));
    checkStr(buf, "Laguna, Squall. Kiros, nobody yet. Ward, nobody yet.",
             "the opening board");

    // ---- the left grid ----------------------------------------------------
    v.focus = 0; v.gridCursor = 1;
    LswCursorLine(v, buf, sizeof(buf));
    checkStr(buf, "Zell.", "an unassigned party member");

    v.gridCursor = 0;
    LswCursorLine(v, buf, sizeof(buf));
    checkStr(buf, "Squall, lending to Laguna.",
             "**a member who is already lending says so** -- the conflict is "
             "invisible on screen otherwise");

    v.gridCursor = 6;
    LswCursorLine(v, buf, sizeof(buf));
    checkStr(buf, "Empty.", "an empty grid cell");

    v.gridCursor = 4;                    // second row, first column
    LswCursorLine(v, buf, sizeof(buf));
    checkStr(buf, "Rinoa.", "the grid cursor is row*4 + column, not a list index");

    // ---- the right column -------------------------------------------------
    v.focus = 1; v.slotCursor = 1;
    LswCursorLine(v, buf, sizeof(buf));
    checkStr(buf, "Kiros, nobody yet. 2 of 3.", "an empty slot");

    v.slots[1] = 1;                      // Zell -> Kiros
    LswCursorLine(v, buf, sizeof(buf));
    checkStr(buf, "Kiros, Zell. 2 of 3.", "a filled slot");

    v.focus = 0; v.gridCursor = 1;
    LswCursorLine(v, buf, sizeof(buf));
    checkStr(buf, "Zell, lending to Kiros.", "and the grid now says so too");

    // ---- a dream with no Ward ---------------------------------------------
    //
    // The slot mask is the field script's own parameter, so a two-slot dream is
    // expressed by the mask, not by a missing character. Nothing may claim a
    // slot that does not exist.
    v.slotMask = 0x03;
    LswAllSlotsLine(v, buf, sizeof(buf));
    checkStr(buf, "Laguna, Squall. Kiros, Zell.",
             "**a slot outside the mask is not part of the board**");
    v.focus = 1; v.slotCursor = 1;
    LswCursorLine(v, buf, sizeof(buf));
    checkStr(buf, "Kiros, Zell. 2 of 2.", "and the count follows the mask");
    check(LswSlotCount(0x03) == 2 && LswSlotCount(0x07) == 3 && LswSlotCount(0x06) == 2,
          "the slot count is a popcount of the mask");
    check(LswSlotOrdinal(0x06, 2) == 2 && LswSlotOrdinal(0x06, 1) == 1 &&
          LswSlotOrdinal(0x06, 0) == 0,
          "**ordinals skip a slot the mask excludes** -- with Laguna locked, "
          "Kiros is the first of two");

    // ---- MODE 2: the second cursor this screen really uses -----------------
    v = Board();
    v.focus = 0; v.gridCursor = 1;                 // standing on Zell
    check(!LswPickHeld(v), "no pick held to begin with");

    v.pending = LSW_MODE_DEST; v.destSlot = 1;     // lifted, choosing a slot
    check(LswPickHeld(v), "**mode 2 IS a pick held**");
    check(LswHeldChar(v) == 1, "and Zell is the pick, from the frozen trio");
    check(LswLiveFocus(v) == 1 && LswLiveSlot(v) == 1,
          "**the live cursor is +0x31, on the slot column**");
    LswPendingLine(v, buf, sizeof(buf));
    checkStr(buf, "Zell picked up. Choose a slot.", "the lift");
    LswDestinationLine(v, buf, sizeof(buf));
    checkStr(buf, "Kiros, nobody yet. Put Zell here. 2 of 3.",
             "**the destination says what would happen** -- the line v0.43.0 and "
             "v0.44.0 both failed to say, for two different reasons");

    v.destSlot = 0;                                // moved up to Laguna's slot
    LswDestinationLine(v, buf, sizeof(buf));
    checkStr(buf, "Laguna, Squall. Put Zell here. 1 of 3.",
             "and on an occupied slot it names who comes back the other way");

    v.destSlot = 2;
    LswDestinationLine(v, buf, sizeof(buf));
    checkStr(buf, "Ward, nobody yet. Put Zell here. 3 of 3.", "on down the column");

    v.pending = LSW_MODE_BROWSE;
    check(!LswPickHeld(v) && LswLiveGrid(v) == 1 && LswLiveFocus(v) == 0,
          "and putting it down returns the live cursor to the browse trio");
    LswDestinationLine(v, buf, sizeof(buf));
    checkStr(buf, "", "with nothing to say about a destination");

    // ---- mode 3, the +0x2e variant -----------------------------------------
    //
    // The case v0.43.0 got wrong: a pick lifted off the grid, then carried to a
    // slot. The FROZEN trio names what is in hand; the SECOND trio is where the
    // player is.
    v = Board();
    v.focus = 0; v.gridCursor = 1;                 // standing on Zell
    check(!LswPickHeld(v), "no pick held to begin with");
    check(LswLiveGrid(v) == 1 && LswLiveFocus(v) == 0,
          "and the live cursor is the first trio");

    v = Board(); v.focus = 0; v.gridCursor = 1;
    v.pending = LSW_MODE_CARRY;                    // the other variant
    v.pendFocus = 0; v.pendGrid = 1; v.pendSlot = 0;
    LswPendingLine(v, buf, sizeof(buf));
    checkStr(buf, "Zell picked up. Choose a slot.", "the lift");
    check(LswHeldChar(v) == 1, "**the pick is read from the FROZEN trio**");

    v.pendFocus = 1; v.pendSlot = 1;               // carried to Kiros's slot
    check(LswLiveFocus(v) == 1 && LswLiveSlot(v) == 1,
          "**and the live cursor is now the SECOND trio**");
    LswCursorLine(v, buf, sizeof(buf));
    checkStr(buf, "Kiros, nobody yet. 2 of 3.",
             "which is the line v0.43.0 never said, because it was still "
             "watching a cursor the engine had stopped moving");
    check(LswHeldChar(v) == 1, "and Zell is still the thing in hand");

    v.pendSlot = 2;
    LswCursorLine(v, buf, sizeof(buf));
    checkStr(buf, "Ward, nobody yet. 3 of 3.", "moving on down the column");

    // And the other half of the same bug: a pick carried around the GRID, which
    // is how two party members are swapped with each other.
    v.pendFocus = 0; v.pendGrid = 3;               // Zell in hand, over Quistis
    check(LswLiveGrid(v) == 3,
          "**the grid cursor follows the second trio too** -- the frozen one is "
          "still sitting on Zell at cell 1");
    LswCursorLine(v, buf, sizeof(buf));
    checkStr(buf, "Quistis.", "so the cell being hovered is the one that is read");
    v.pendGrid = 0;
    LswCursorLine(v, buf, sizeof(buf));
    checkStr(buf, "Squall, lending to Laguna.",
             "and it still says what that person is already doing");
    check(LswHeldChar(v) == 1, "with Zell still in hand throughout");

    v.pending = LSW_MODE_BROWSE;                   // put down
    check(!LswPickHeld(v) && LswLiveGrid(v) == 1,
          "and when it is put down the live cursor is the first trio again");

    // ---- a pick lifted off a SLOT ------------------------------------------
    v = Board();
    v.slots[1] = 1;                                // Zell already lends to Kiros
    v.focus = 1; v.slotCursor = 1;                 // standing on that slot
    v.pending = LSW_MODE_DEST; v.destSlot = 2;
    LswPendingLine(v, buf, sizeof(buf));
    checkStr(buf, "Zell picked up. Choose a slot.",
             "a lift off a slot names who was in it");
    v.focus = 1; v.slotCursor = 2;                 // frozen on Ward's EMPTY slot
    LswPendingLine(v, buf, sizeof(buf));
    checkStr(buf, "", "**nothing is lifted off an empty slot**");

    // ---- who is where ------------------------------------------------------
    v = Board(); v.slots[1] = 1;
    check(LswSlotOfChar(v, 0) == 0, "Squall is in Laguna's slot");
    check(LswSlotOfChar(v, 1) == 1, "Zell is in Kiros's slot");
    check(LswSlotOfChar(v, 5) == -1, "Selphie is in none");
    check(LswSlotOfChar(v, 0xFF) == -1, "and 0xFF is not a character");

    checkStr(LswSlotLabel(0), "Laguna", "slot 0 is Laguna");
    checkStr(LswSlotLabel(1), "Kiros",  "slot 1 is Kiros");
    checkStr(LswSlotLabel(2), "Ward",   "slot 2 is Ward");
    check(LSW_SLOT_CHAR_ID[0] == 8 && LSW_SLOT_CHAR_ID[1] == 9 &&
          LSW_SLOT_CHAR_ID[2] == 10,
          "**and those are character ids 8, 9, 10** -- the three `bghoke_2` "
          "flags with 0x08C/0x08D immediately before opening this screen");

    std::printf(bad ? "laguna_switch_compile: FAILED (%d bad)\n"
                    : "laguna_switch_compile: OK (%d bad)\n", bad);
    return bad ? 1 : 0;
}
