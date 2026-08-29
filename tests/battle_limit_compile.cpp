// battle_limit_compile.cpp -- v0.36.0 (#94)
//
// Drives src/battle_tts_limit.inl against the engine's REAL addresses.
//
//   g++ -std=c++17 -O0 -Isrc -o tests_out/battle_limit_compile
//       tests/battle_limit_compile.cpp src/ff8_text_decode.cpp
//
// WHY THIS EXISTS
//
// v0.14.13 shipped a limit-submenu announce that said "Blue Magic" and stopped,
// because Quistis had exactly one Blue Magic spell at the time and ONE ROW
// CANNOT REVEAL A CURSOR. The reader stalled there for twenty-two builds. A
// probe that plants sixteen rows and walks them settles in a second what no
// amount of staring at a one-row BAT log could.
//
// THE INTERESTING PART: this probe maps EXECUTABLE PAGES at the engine's own
// resolver addresses and writes real machine code into them. The reader calls
// 0x004C7CD0 for the list base and the installed name/description resolvers for
// the strings -- it does not multiply out `charIdx * 464` or index a kernel
// section itself -- so the only honest way to test it is to let it make those
// calls. If someone later "simplifies" the reader by re-deriving the table
// math, these stubs stop being called and the assertions still pass; that is
// why the base-address stub also RECORDS that it was called.

#include <cstdio>
#include <cstdarg>
#include <cstring>
#include <cstdint>
#include <cstddef>
#include <string>
#include <vector>
#include <sys/mman.h>

typedef unsigned long  DWORD;
typedef unsigned short WORD;
typedef unsigned char  BYTE;

#undef __try
#undef __except
#define __try if (1)
#define __except(x) else
#define EXCEPTION_EXECUTE_HANDLER 1
#define __cdecl        // host build: the engine ABI is x86-64 SysV here

#define VK_OEM_2 0xBF
static int g_keyDown = 0;
static short GetAsyncKeyState(int vk) { return (short)((vk == VK_OEM_2 && g_keyDown) ? 0x8000 : 0); }

namespace Log { void Battle(const char*, ...) {} }

// --- the speech sink --------------------------------------------------------
static char g_spoken[512];
static int  g_speakCount = 0;
enum SpeechPriority { PRIO_CRITICAL = 0, PRIO_TURN, PRIO_MENU, PRIO_ACTION,
                      PRIO_HP, PRIO_STATUS, PRIO_INFO };
static void BattleSpeak(const char* text, SpeechPriority, bool = false)
{ snprintf(g_spoken, sizeof(g_spoken), "%s", text ? text : ""); g_speakCount++; }

// --- the REAL decoder, not a stub (v0.35.0 rule) ----------------------------
#include "ff8_text_decode.h"
static int DecodeFF8String(const uint8_t* src, char* dst, int maxLen)
{
    dst[0] = '\0';
    if (!src) return 0;
    std::string s = FF8TextDecode::Decode(src, 256);
    snprintf(dst, maxLen, "%s", s.c_str());
    return (int)strlen(dst);
}

static const char* MAGIC_NAMES_STUB[] = { "None", "Fire", "Fira", "Firaga", "Blizzard" };
static const char* GetMagicName(uint8_t id)
{
    if (id == 0x33) return "Full-Cure";
    if (id < sizeof(MAGIC_NAMES_STUB) / sizeof(MAGIC_NAMES_STUB[0])) return MAGIC_NAMES_STUB[id];
    return "???";
}

#include "battle_limit_model.inl"
#include "battle_tts_limit.inl"

static int bad = 0;
static void check(bool ok, const char* what)
{ if (!ok) { bad++; printf("  BAD: %s\n", what); } }
static void checkStr(const char* got, const char* want, const char* what)
{
    if (strcmp(got, want) != 0) {
        bad++;
        printf("  BAD: %s\n        got  \"%s\"\n        want \"%s\"\n", what, got, want);
    }
}

// ---------------------------------------------------------------------------
// Memory
// ---------------------------------------------------------------------------
static void* MapAt(uintptr_t addr, size_t len, int prot)
{
    const uintptr_t pg = addr & ~(uintptr_t)0xFFF;
    const size_t    sz = ((addr + len) - pg + 0xFFF) & ~(size_t)0xFFF;
    void* p = mmap((void*)pg, sz, prot, MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0);
    return (p == MAP_FAILED) ? nullptr : p;
}

// String banks the fake resolvers hand back: 64 entries of 32 bytes each.
static const uintptr_t BANK_NAME = 0x00460000;   // one bank per resolver, so a
static const uintptr_t BANK_DESC = 0x00462000;   // reader that confused the two
static const uintptr_t BANK_CMD  = 0x00464000;   // would be caught
static const uintptr_t BANK_SLOT = 0x00466000;
// v0.38.3 (#99): kind 0 gets banks of its own so a resolver mix-up between it
// and kinds 1-3 shows up as a wrong string rather than a coincidence.
static const uintptr_t BANK_NAME0 = 0x00468000;
static const uintptr_t BANK_DESC0 = 0x0046A000;
static const int       BANK_ENTRY = 32;

static uint8_t Enc(char c)
{
    if (c == ' ')             return 0x20;
    if (c >= '0' && c <= '9') return (uint8_t)(0x21 + (c - '0'));
    if (c >= 'A' && c <= 'Z') return (uint8_t)(0x45 + (c - 'A'));
    if (c >= 'a' && c <= 'z') return (uint8_t)(0x5F + (c - 'a'));
    switch (c) {
        case '-': return 0x32; case '.': return 0x3B; case ',': return 0x3C;
        case '?': return 0x2F; case '!': return 0x2E; case '\'': return 0x43;  // v0.117.0
        default:  return 0x20;
    }
}
// Plant bytes VERBATIM -- for strings copied out of kernel.bin, where
// re-encoding from ASCII would quietly drop the >= 0xE8 pair codes the real
// descriptions are full of and test the probe's encoder instead of the mod's
// decoder.
static void PutBankRaw(uintptr_t bank, int index, const uint8_t* b, size_t n)
{
    uint8_t* p = (uint8_t*)(bank + (uintptr_t)index * BANK_ENTRY);
    if (n > BANK_ENTRY - 1) n = BANK_ENTRY - 1;
    memcpy(p, b, n);
    p[n] = 0;
}

static void PutBank(uintptr_t bank, int index, const char* s)
{
    uint8_t* p = (uint8_t*)(bank + (uintptr_t)index * BANK_ENTRY);
    size_t i = 0;
    for (; s[i] && i < BANK_ENTRY - 1; i++) p[i] = Enc(s[i]);
    p[i] = 0;
}

// --- machine code for the engine's resolvers --------------------------------
// System V x86-64: first integer argument in EDI, return value in RAX.
//
// The call counter lives in the MAPPED LOW REGION, not in this binary. A
// rip-relative write from 0x004C7CD0 to a PIE image at 0x5555_5555_xxxx does
// not fit in a signed 32-bit displacement, which is a segfault rather than a
// failed assertion -- worth writing down, because the next probe that needs an
// executable stub at a low fixed address will hit it too.
static const uintptr_t BASEFN_COUNTER = 0x0046F000;
static uint32_t BaseFnCalls() { return *(volatile uint32_t*)BASEFN_COUNTER; }
static void     BaseFnCallsReset() { *(volatile uint32_t*)BASEFN_COUNTER = 0; }

//   inc dword [rip+d32] | mov eax, edi | imul eax, eax, 464 | add eax, 0x01CFF032 | ret
static void WriteBaseFnStub(uintptr_t at)
{
    uint8_t code[] = {
        0xFF, 0x05, 0,0,0,0,                // inc dword [rip+disp32]
        0x89, 0xF8,                         // mov eax, edi
        0x69, 0xC0, 0xD0, 0x01, 0x00, 0x00, // imul eax, eax, 464
        0x05, 0x32, 0xF0, 0xCF, 0x01,       // add eax, 0x01CFF032
        0xC3                                // ret
    };
    const int32_t disp = (int32_t)((int64_t)BASEFN_COUNTER - (int64_t)(at + 6));
    memcpy(code + 2, &disp, 4);
    memcpy((void*)at, code, sizeof(code));
}

//   mov eax, edi | shl eax, 5 | add eax, bank | ret
static void WriteBankFnStub(uintptr_t at, uintptr_t bank)
{
    uint8_t code[] = {
        0x89, 0xF8,                 // mov eax, edi
        0xC1, 0xE0, 0x05,           // shl eax, 5
        0x05, 0,0,0,0,              // add eax, imm32
        0xC3                        // ret
    };
    const uint32_t b = (uint32_t)bank;
    memcpy(code + 6, &b, 4);
    memcpy((void*)at, code, sizeof(code));
}

// ---------------------------------------------------------------------------
static uint8_t* SCRATCH = nullptr;      // 0x01D768D0 window
static uint8_t* USED    = nullptr;      // 0x01D76904
static uint8_t* LIST    = nullptr;      // 0x01CFF032

static void W32(uintptr_t a, uint32_t v) { *(uint32_t*)a = v; }
static void W8 (uintptr_t a, uint8_t  v) { *(uint8_t*)a  = v; }

static void ResetReader()
{
    s_limInList = false; s_limLastCursor = -1; s_limLastKind = -1;
    s_limRowDesc[0] = '\0';
    s_limSlotOpen = false; s_limSlotCursor = -1; s_limSlotMagic = -1;
    s_limSlotTimes = -1; s_limSlotDesc[0] = '\0';
    s_limSlashWasDown = false;
    g_spoken[0] = '\0'; g_speakCount = 0;
}

// Lay out one limit list for a character: count rows of {id, stock, 0,0, flags}
struct Row { uint8_t id, stock, flags; };
static uint8_t* ListFor(int slot)
{ return (uint8_t*)(0x01CFF032 + (uintptr_t)slot * 464); }

static void BuildList(int slot, const Row* rows, int n)
{
    uint8_t* base = ListFor(slot);
    memset(base, 0, 5 * 64);
    for (int i = 0; i < n; i++) {
        base[i * 5 + 0] = rows[i].id;
        base[i * 5 + 1] = rows[i].stock;
        base[i * 5 + 4] = rows[i].flags;
    }
    memset(USED, 0, 64);
}

// The party formation the mod maps a slot through: slot -> savemap charIdx.
// The 2026-08-19 BAT had Irvine in slot 0 and Selphie in slot 2, and the
// v0.36.0 reader compared the SLOT against a CHARACTER INDEX.
static void SetFormation(int s0, int s1, int s2)
{
    uint8_t* f = (uint8_t*)(uintptr_t)LIMIT_PARTY_FORMATION;
    f[0] = (uint8_t)s0; f[1] = (uint8_t)s1; f[2] = (uint8_t)s2;
}

static void OpenList(int kind, int slot, int count, int cursor, int cmdId)
{
    W32(LIM_BASEFN, LIM_LIST_BASEFN_VALUE);
    W32(LIM_NAMEFN, LIMIT_NAME_FN[kind]);
    W32(LIM_DESCFN, LIMIT_DESC_FN[kind]);
    W8 (LIM_KIND,    (uint8_t)kind);
    W8 (LIM_SLOT,    (uint8_t)slot);
    W8 (LIM_COUNT,   (uint8_t)count);
    W8 (LIM_COLUMNS, 4);
    W8 (LIM_CMDID,   (uint8_t)cmdId);
    W8 (LIM_COLSET,  0);
    W8 (LIM_VISIBLE, (uint8_t)((count < 4) ? count : 4));
    W8 (LIM_LASTROW, (uint8_t)(count - 1));
    W8 (LIM_PAGES,   (uint8_t)((count + 3) / 4));
    W8 (LIM_CURSOR,  (uint8_t)cursor);
}

int main()
{
    printf("battle_tts_limit.inl compiles\n");

    // ---- the pure model, first -------------------------------------------
    check(LimitKindFromNameFn(0x0047E650) == LIMIT_KIND_BLUE, "0x47E650 is Quistis's kind");
    check(LimitKindFromNameFn(0x0047EA30) == LIMIT_KIND_AMMO, "0x47EA30 is the item kind");
    check(LimitKindFromNameFn(0x0047E4F0) == LIMIT_KIND_COMB, "0x47E4F0 is Rinoa's kind");
    check(LimitKindFromNameFn(0x0047E6B0) == LIMIT_KIND_TEMP, "0x47E6B0 is the temp-character kind");
    check(LimitKindFromNameFn(0x004C7CD0) == -1, "the base callback is not a name resolver");
    check(LimitKindFromNameFn(0) == -1, "zero is not a kind");
    check(LimitKindAgrees(1, 0x0047E650), "kind byte 1 agrees with its resolver");
    check(!LimitKindAgrees(1, 0x0047EA30),
          "**a kind byte that disagrees with the installed resolver is refused**");
    check(!LimitKindAgrees(4, 0x0047E650), "a kind byte past the table is refused");

    check(LimitRowRemaining(4, 7, 2) == 5, "a 4-column list subtracts what is already queued");
    check(LimitRowRemaining(1, 7, 2) == 7, "a 1-column list does not (0x004FE2A2)");
    check(LimitRowRemaining(3, 7, 2) == 7, "nor does a 3-column one");
    check(LimitRowSelectable(1, 0), "one left and no flag is choosable");
    check(!LimitRowSelectable(0, 0), "nothing left is not");
    check(!LimitRowSelectable(5, 0x02), "and bit 1 of the flag byte disables a row outright");

    check(SlotOptionEntryOffset(0) == 5 && SlotOptionEntryOffset(1) == 10,
          "the Slot's options are list entries 1 and 2");
    check(SlotOptionLabelIndex(66) == 66, "the LABEL is the entry id (row drawer, 0x004C7A92)");
    check(SlotOptionHelpIndex(66) == 68, "the EXPLANATION is two further on (help line, 0x004C7538)");
    check(SlotOptionLabelIndex(67) == 67 && SlotOptionHelpIndex(67) == 69,
          "and the same for Do over / Turn the slot again");
    checkStr(LimitTimesWord(1), "time", "one cast is a time, not times");
    checkStr(LimitTimesWord(3), "times", "three casts are times");
    check(!SlotSpellReady(0, 3) && !SlotSpellReady(0x33, 0) && SlotSpellReady(0x33, 3),
          "a half-written roll is not ready");
    check(SlotPhaseValid(0x0A) && !SlotPhaseValid(0x0B), "the phase table has eleven entries");
    check(LimitRowUsesSavemapName(LIMIT_KIND_COMB, 0),
          "Rinoa's first row is named from the savemap (0x0047E4F0 index 0)");
    check(!LimitRowUsesSavemapName(LIMIT_KIND_COMB, 1) &&
          !LimitRowUsesSavemapName(LIMIT_KIND_BLUE, 0),
          "and nothing else is");

    // ---- memory ------------------------------------------------------------
    check(MapAt(0x00460000, 0x10000, PROT_READ | PROT_WRITE) != nullptr, "string banks mapped");
    check(MapAt(0x00470000, 0x60000, PROT_READ | PROT_WRITE | PROT_EXEC) != nullptr,
          "resolver pages mapped executable");
    check(MapAt(0x01CF0000, 0x90000, PROT_READ | PROT_WRITE) != nullptr, "battle memory mapped");
    if (bad) { printf("battle_limit_compile: FAILED (mapping) %d\n", bad); return 1; }

    SCRATCH = (uint8_t*)0x01D768D0;
    USED    = (uint8_t*)(uintptr_t)LIM_USED;
    LIST    = (uint8_t*)0x01CFF032;
    memset((void*)0x01D76800, 0, 0x200);
    SetFormation(LIMIT_CHAR_IRVINE, LIMIT_CHAR_SQUALL, LIMIT_CHAR_SELPHIE);

    WriteBaseFnStub(0x004C7CD0);
    WriteBankFnStub(0x0047E650, BANK_NAME);   // kind 1 name
    WriteBankFnStub(0x0047E680, BANK_DESC);   // kind 1 desc
    WriteBankFnStub(0x0047EA30, BANK_NAME);   // kind 3 name (items)
    WriteBankFnStub(0x0047EA90, BANK_DESC);   // kind 3 desc
    WriteBankFnStub(0x0047E4F0, BANK_NAME);   // kind 2 name
    WriteBankFnStub(0x004952D0, BANK_DESC);   // kind 2 desc  -- outside the 0x47xxxx page
    WriteBankFnStub(0x0047EBD0, BANK_CMD);    // battle command names
    WriteBankFnStub(0x0047EC70, BANK_SLOT);   // the Slot's string table

    WriteBankFnStub(0x0047E6B0, BANK_NAME0);  // kind 0 name -- Seifer/Edea/Laguna
    WriteBankFnStub(0x0047E6E0, BANK_DESC0);  // kind 0 desc

    PutBank(BANK_CMD,  5, "Renzokuken");
    PutBank(BANK_CMD, 11, "Duel");
    PutBank(BANK_CMD, 14, "Shot");
    PutBank(BANK_CMD, 15, "Blue Magic");
    PutBank(BANK_CMD, 16, "Slot");
    PutBank(BANK_CMD, 17, "Fire Cross");
    PutBank(BANK_CMD, 18, "Sorcery");
    PutBank(BANK_CMD, 19, "Combine");
    PutBank(BANK_CMD, 20, "Limit");

    // ---- kind 0, in FF8's own bytes ---------------------------------------
    //
    // Not typed out: these are kernel.bin section 18 entries 0 and 1, whose
    // name/description offsets (0x0000/0x0009 and 0x0018/0x0023) resolve into
    // section 48 at file offset 0x89C0. Searching kernel.bin for the encoded
    // form of each name lands on the same five addresses, so the record table
    // and the text block agree independently. The descriptions carry the
    // >= 0xE8 pair codes, which is the point of planting bytes rather than
    // ASCII: "Damage all enemies" is eighteen characters out of fourteen bytes.
    static const uint8_t K0_NAME_SEIFER[] =
        { 0x52, 0x6D, 0x20, 0x51, 0x63, 0x70, 0x61, 0x77 };
    static const uint8_t K0_DESC_SEIFER[] =
        { 0x48, 0x5F, 0x6B, 0xFF, 0xE9, 0x5F, 0xEF, 0x20, 0x63, 0xEA, 0x6B, 0x67, 0x63, 0x71 };
    static const uint8_t K0_NAME_EDEA[] =
        { 0x4D, 0x61, 0x63, 0x20, 0x57, 0x72, 0x70, 0x67, 0x69, 0x63 };
    static const uint8_t K0_DESC_EDEA[] =
        { 0x48, 0x5F, 0x6B, 0xFF, 0xE9, 0xF5, 0xE9, 0x63, 0xEA, 0x6B, 0x77 };
    // **The indices here are LITERAL 0 and 1 -- kernel section 18's own record
    // order -- not LIMIT_KIND0_SEIFER / _EDEA.** Planting the bank through the
    // same constants the rows are built from would make the fixture agree with
    // the model by construction: swap the two constants and every assertion
    // still passes. With literals, swapping them makes Seifer's list say "Ice
    // Strike" and the probe fails, which is the only version of this test worth
    // having.
    PutBankRaw(BANK_NAME0, 0, K0_NAME_SEIFER, sizeof(K0_NAME_SEIFER));
    PutBankRaw(BANK_DESC0, 0, K0_DESC_SEIFER, sizeof(K0_DESC_SEIFER));
    PutBankRaw(BANK_NAME0, 1, K0_NAME_EDEA,   sizeof(K0_NAME_EDEA));
    PutBankRaw(BANK_DESC0, 1, K0_DESC_EDEA,   sizeof(K0_DESC_EDEA));

    // ---- the slot -> character mapping, which v0.36.0 did not do at all ---
    check(LimitPartySlotValid(0) && LimitPartySlotValid(2) && !LimitPartySlotValid(3),
          "party slots are 0..2");
    check(!LimitPartySlotValid(LIMIT_CHAR_SELPHIE),
          "**Selphie's character index is not a valid party slot** -- which is why "
          "v0.36.0's `slot == 5` test could never pass");
    check(LimitCharOfSlot(2) == LIMIT_CHAR_SELPHIE, "slot 2 resolves to Selphie");
    check(LimitCharOfSlot(0) == LIMIT_CHAR_IRVINE, "slot 0 resolves to Irvine");
    check(LimitCharOfSlot(7) == -1, "an impossible slot resolves to nobody");

    // ---- QUISTIS: sixteen Blue Magic rows, the case that stalled v0.14.13 --
    {
        ResetReader();
        static const char* BLUE[16] = {
            "Laser Eye", "Ultra Waves", "Electrocute", "LV Death", "Degenerator",
            "Aqua Breath", "Micro Missiles", "Acid", "Gatling Gun", "Fire Breath",
            "Bad Breath", "White Wind", "Homing Laser", "Mighty Guard", "Ray-Bomb",
            "Shockwave Pulsar"
        };
        Row rows[16];
        for (int i = 0; i < 16; i++) {
            rows[i].id = (uint8_t)i; rows[i].stock = 1; rows[i].flags = 0;
            PutBank(BANK_NAME, i, BLUE[i]);
        }
        PutBank(BANK_DESC, 15, "Major damage to all enemies");
        BuildList(0, rows, 16);   // party slot 0

        OpenList(LIMIT_KIND_BLUE, 0, 16, 0, 15);
        BaseFnCallsReset();
        PollLimitMenus();
        checkStr(g_spoken, "Blue Magic. Laser Eye",
                 "the submenu opens with the game's own command name and the first row");
        check(BaseFnCalls() > 0,
              "**the reader asked the ENGINE for the list base** rather than multiplying it out");

        // Walk to the sixteenth row. v0.14.13 could not have distinguished any
        // of these from each other -- there was only ever one row on screen.
        W8(LIM_CURSOR, 15);
        PollLimitMenus();
        checkStr(g_spoken, "Shockwave Pulsar", "row 16 names row 16, not row 1");

        W8(LIM_CURSOR, 11);
        PollLimitMenus();
        checkStr(g_spoken, "White Wind", "and the cursor is read where it actually is");

        // "/" reads the description of the row the cursor is on.
        W8(LIM_CURSOR, 15);
        PollLimitMenus();
        g_spoken[0] = '\0';
        g_keyDown = 1; PollLimitMenus(); g_keyDown = 0;
        checkStr(g_spoken, "Major damage to all enemies", "\"/\" reads the row's own description");

        // No repeat without a cursor move.
        const int before = g_speakCount;
        PollLimitMenus();
        check(g_speakCount == before, "a still cursor says nothing again");
    }

    // ---- IRVINE: ammunition, with counts and a spent row -------------------
    {
        ResetReader();
        Row rows[4] = { {1, 30, 0}, {2, 8, 0}, {3, 0, 0}, {4, 12, 0x02} };
        PutBank(BANK_NAME, 1, "Normal Ammo");
        PutBank(BANK_NAME, 2, "Shotgun Ammo");
        PutBank(BANK_NAME, 3, "Dark Ammo");
        PutBank(BANK_NAME, 4, "Pulse Ammo");
        PutBank(BANK_DESC, 2, "Damage all enemies");
        BuildList(0, rows, 4);    // Irvine was slot 0 in the BAT

        OpenList(LIMIT_KIND_AMMO, 0, 4, 0, 14);
        PollLimitMenus();
        checkStr(g_spoken, "Shot. Normal Ammo, 30 left",
                 "ammunition reads its count -- the one kind whose stock is spent");

        W8(LIM_CURSOR, 2);
        PollLimitMenus();
        checkStr(g_spoken, "Dark Ammo, 0 left, not available",
                 "**a row you cannot pick SAYS so** rather than reading like the others");

        W8(LIM_CURSOR, 3);
        PollLimitMenus();
        checkStr(g_spoken, "Pulse Ammo, 12 left, not available",
                 "and so does one the engine has disabled despite a stock");

        // The queued-this-turn subtraction: two Shotgun rounds already spent.
        USED[1] = 2;
        W8(LIM_CURSOR, 1);
        PollLimitMenus();
        checkStr(g_spoken, "Shotgun Ammo, 6 left",
                 "what is already queued this turn is taken off the count");
        USED[1] = 0;
    }

    // ---- RINOA: two options, no sub-list ----------------------------------
    {
        ResetReader();
        Row rows[2] = { {0, 1, 0}, {1, 1, 0} };
        PutBank(BANK_NAME, 0, "Angelo");
        PutBank(BANK_NAME, 1, "Angel Wing");
        PutBank(BANK_DESC, 1, "Use Angel Wing");
        BuildList(2, rows, 2);    // party slot 2

        OpenList(LIMIT_KIND_COMB, 2, 2, 0, 19);
        PollLimitMenus();
        checkStr(g_spoken, "Combine. Angelo", "Rinoa's first option is the dog, by name");
        W8(LIM_CURSOR, 1);
        PollLimitMenus();
        checkStr(g_spoken, "Angel Wing", "and the second is Angel Wing");
        check(g_speakCount == 2, "two options, two utterances -- there is no third");
    }

    // ---- the identification refuses everything it should ------------------
    {
        ResetReader();
        Row rows[2] = { {0, 1, 0}, {1, 1, 0} };
        BuildList(2, rows, 2);    // party slot 2
        OpenList(LIMIT_KIND_COMB, 2, 2, 0, 19);

        // The scratch block is a union. Any of these means it is holding
        // something else, and reading it would name a row out of another menu.
        W32(LIM_BASEFN, 0x00000020);           // the command menu's own value
        PollLimitMenus();
        check(g_speakCount == 0, "without the base-callback signature, nothing is read");

        W32(LIM_BASEFN, LIM_LIST_BASEFN_VALUE);
        W8(LIM_KIND, 3);                        // stale kind, resolver still kind 2
        PollLimitMenus();
        check(g_speakCount == 0,
              "**a kind byte that disagrees with the resolver is refused** -- one field is not enough");

        // **v0.37.2: a cursor outside the list is not a failed identification.**
        // The 2026-08-20 log caught 0x01D768EC reading 5 with five rows as the
        // window closed. v0.36 refused the whole view, which tore the session
        // down, so the next readable frame re-announced the TITLE instead of
        // the row -- the player hears "Blue Magic. Laser Eye" again in the
        // middle of walking the list. Identification and cursor validity are
        // different questions.
        ResetReader();
        W8(LIM_KIND, LIMIT_KIND_COMB);
        W8(LIM_CURSOR, 5);                      // past the end
        PollLimitMenus();
        check(g_speakCount == 1, "an out-of-range cursor still opens the menu");
        checkStr(g_spoken, "Combine", "with the title alone -- no row is invented");

        W8(LIM_CURSOR, 1);                      // back in range
        PollLimitMenus();
        check(g_speakCount == 2, "and the next real row speaks");
        checkStr(g_spoken, "Angel Wing",
                 "**as a ROW, not as another title** -- the session survived");

        // v0.37.3: a cell that exists on the page but holds nothing SAYS so.
        // The list is a column of four per page; with 2 rows and 1 page,
        // cursors 2 and 3 are cells on screen with nothing in them. Silence
        // there is indistinguishable from the mod having broken, and it is
        // where the stray "Electrocute" was filling the gap.
        W8(LIM_PAGES, 1);
        W8(LIM_CURSOR, 3);
        PollLimitMenus();
        checkStr(g_spoken, "Empty", "an empty cell on the page reads as Empty");
        W8(LIM_CURSOR, 9);                      // past every page
        PollLimitMenus();
        check(strcmp(g_spoken, "Empty") == 0 && g_speakCount == 3,
              "but a cursor past the last page invents nothing");

        ResetReader();
        W8(LIM_CURSOR, 0);
        W8(LIM_COUNT, 0);
        PollLimitMenus();
        check(g_speakCount == 0, "an empty list IS refused -- there is nothing to identify");
        W8(LIM_COUNT, 2);
    }

    // ---- SELPHIE'S SLOT ----------------------------------------------------
    {
        ResetReader();
        memset((void*)0x01D76800, 0, 0x200);
        uint8_t* base = ListFor(2);   // Selphie was party slot 2 in the BAT
        memset(base, 0, 5 * 8);
        // **THE FIXTURE IS THE GAME'S DATA, NOT THIS FILE'S.** Kernel section 30
        // holds 66 "Cast" / 67 "Do over" with their explanations two further on
        // at 68 / 69, and the 2026-08-19 BAT showed the list holds 67 at entry 1
        // and 66 at entry 2 -- the player picked cursor 1 and the popup carried
        // value=0x33 (Full-Cure), so cursor 1 IS Cast.
        //
        // v0.36.1's fixture planted 64/65 and asserted 66/67: numbers chosen to
        // match the code. It passed while the game announced every option's
        // description in place of its name.
        base[1 * 5] = 67;   // cursor 0 -> "Do over"
        base[2 * 5] = 66;   // cursor 1 -> "Cast"
        PutBank(BANK_SLOT, 66, "Cast");
        PutBank(BANK_SLOT, 67, "Do over");
        PutBank(BANK_SLOT, 68, "Use indicated magic");
        PutBank(BANK_SLOT, 69, "Turn the slot again");

        W32(SLOT_LISTPTR, (uint32_t)(uintptr_t)base);
        W8(SLOT_SLOT,    2);
        W8(SLOT_PHASE,   SLOT_PHASE_INPUT);
        W8(SLOT_CURSOR,  SLOT_OPT_CAST);
        W8(SLOT_CMDID,   16);

        // The pre-write frame: the roll has not produced anything yet.
        W8(SLOT_MAGICID, 0); W8(SLOT_TIMES, 0);
        PollLimitMenus();
        check(g_speakCount == 0, "the frame before the roll lands stays silent");

        W8(SLOT_MAGICID, 0x33); W8(SLOT_TIMES, 3);
        PollLimitMenus();
        checkStr(g_spoken, "Slot. Full-Cure, 3 times. Do over",
                 "the Slot reads the spell, how many casts, and the option under the cursor");

        W8(SLOT_CURSOR, 1);
        PollLimitMenus();
        checkStr(g_spoken, "Cast",
                 "**the option announces its NAME, not its explanation** -- v0.36.1 said "
                 "\"Use indicated magic\" here");

        g_spoken[0] = '\0';
        g_keyDown = 1; PollLimitMenus(); g_keyDown = 0;
        checkStr(g_spoken, "Use indicated magic", "and \"/\" gives the explanation");

        // Re-roll: a new spell must be announced without a cursor move.
        W8(SLOT_MAGICID, 0x02); W8(SLOT_TIMES, 5);
        PollLimitMenus();
        checkStr(g_spoken, "Fira, 5 times. Cast",
                 "**a re-roll announces itself** -- the cursor has not moved, the spell has");

        // A single cast is "1 time". The BAT read "Wall, 1 times".
        W8(SLOT_MAGICID, 0x01); W8(SLOT_TIMES, 1);
        PollLimitMenus();
        checkStr(g_spoken, "Fire, 1 time. Cast", "one cast reads as one time");

        // A phase that is not the input phase says nothing new.
        const int before = g_speakCount;
        W8(SLOT_PHASE, 3);
        PollLimitMenus();
        check(g_speakCount == before, "the executing phase is not a menu");

        // **THE v0.36.0 DEFECT, PINNED.** The slot byte held 2 (Selphie's party
        // slot) and the reader compared it against 5 (her savemap index), so the
        // window was refused every time it opened. A slot can never be 5.
        W8(SLOT_PHASE, SLOT_PHASE_INPUT);
        ResetReader();
        W8(SLOT_SLOT, 2);
        W32(SLOT_LISTPTR, (uint32_t)(uintptr_t)ListFor(2));
        W8(SLOT_MAGICID, 0x33); W8(SLOT_TIMES, 3); W8(SLOT_CURSOR, 0);
        PollLimitMenus();
        check(g_speakCount == 1, "a Slot window in party slot 2 is READ, not refused");

        // The pointer and the slot byte have to agree with each other.
        ResetReader();
        W8(SLOT_SLOT, 1);                       // pointer still says slot 2
        PollLimitMenus();
        check(g_speakCount == 0, "a slot byte that disagrees with the pointer is refused");

        // ...and the command that opened it has to have been Slot.
        ResetReader();
        W8(SLOT_SLOT, 2);
        W8(SLOT_CMDID, 15);                     // Blue Magic
        PollLimitMenus();
        check(g_speakCount == 0, "a window opened by some other command is refused");
        W8(SLOT_CMDID, SLOT_COMMAND_ID);

        // A slot byte outside the party is refused.
        ResetReader();
        W8(SLOT_SLOT, 5);
        PollLimitMenus();
        check(g_speakCount == 0, "a slot byte of 5 is not a party slot at all");

        // The list menu's signature must not be read as a Slot pointer.
        ResetReader();
        W8(SLOT_SLOT, 2);
        W32(SLOT_LISTPTR, LIM_LIST_BASEFN_VALUE);
        PollLimitMenus();
        check(g_speakCount == 0, "and the OTHER limit menu is not mistaken for it");
    }

    // ---- the predicate that stops the command menu talking over us --------
    // The BAT's failure was "Shot. Normal Ammo, 20 left" followed 10 ms later by
    // "Attack", spoken with interrupt=true by the generic submenu handler. That
    // handler now asks this, and it has to be true while the window is up and
    // false when it is not -- with no dependence on a flag set later in the frame.
    {
        ResetReader();
        memset((void*)0x01D76800, 0, 0x200);
        check(!LimitMenuIsOpenNow(), "nothing open -> false");

        Row rows[4] = { {1, 30, 0}, {2, 8, 0}, {3, 0, 0}, {4, 12, 0x02} };
        BuildList(0, rows, 4);
        OpenList(LIMIT_KIND_AMMO, 0, 4, 0, 14);
        check(LimitMenuIsOpenNow(), "**an ammo list is open -> true, before anything has polled**");

        W32(LIM_BASEFN, 0x20);
        check(!LimitMenuIsOpenNow(), "and false again the moment the signature goes");

        uint8_t* base = ListFor(2);
        memset(base, 0, 5 * 8);
        base[5] = 67; base[10] = 66;
        W32(SLOT_LISTPTR, (uint32_t)(uintptr_t)base);
        W8(SLOT_SLOT, 2); W8(SLOT_CMDID, SLOT_COMMAND_ID);
        W8(SLOT_PHASE, SLOT_PHASE_INPUT); W8(SLOT_CURSOR, 0);
        W8(SLOT_MAGICID, 0x33); W8(SLOT_TIMES, 3);
        check(LimitMenuIsOpenNow(), "a Slot window counts too");
    }

    // ---- KIND 0: Seifer and Edea (v0.38.3, #99) ---------------------------
    //
    // Aaron cannot BAT either of them -- Seifer is playable for a few battles
    // near the start and Edea only much later. Everything about their limits is
    // readable from the shipped files, so this is where it gets checked:
    // kernel section 0 says Fire Cross (17) and Sorcery (18) both carry menu
    // kind 0x87, 0x87 dispatches to 0x004C8190, that pushes 0 and installs
    // 0x0047E6B0 / 0x0047E6E0, and section 18 entry 0 is No Mercy and entry 1
    // is Ice Strike. The bytes below ARE section 18's, so what this asserts is
    // a statement about FF8 rather than about this file.
    //
    // **A one-row list is the shape that broke v0.14.13** -- Quistis had one
    // Blue Magic spell and the reader announced the command and stopped. Seifer
    // and Edea have exactly one row each, permanently, so that failure mode is
    // their normal case and it is checked first.
    {
        ResetReader();
        memset((void*)0x01D76800, 0, 0x200);
        SetFormation(6 /* Seifer */, 0, 7 /* Edea */);

        Row seifer[1] = { { LIMIT_KIND0_SEIFER, 1, 0 } };
        BuildList(0, seifer, 1);
        OpenList(LIMIT_KIND_TEMP, 0, 1, 0, 17);
        PollLimitMenus();
        checkStr(g_spoken, "Fire Cross. No Mercy",
                 "**Seifer: one row, named from kernel section 18, titled by the command**");

        // The description "/" would read comes off the OTHER resolver, and it
        // is full of >= 0xE8 pair codes -- eighteen characters out of fourteen
        // bytes -- so a reader that confused name and description, or that lost
        // the pair table, cannot pass this.
        g_keyDown = 1; PollLimitMenus(); g_keyDown = 0;
        checkStr(g_spoken, "Damage all enemies",
                 "and its description decodes the pair codes the kernel actually uses");

        ResetReader();
        memset((void*)0x01D76800, 0, 0x200);
        Row edea[1] = { { LIMIT_KIND0_EDEA, 1, 0 } };
        BuildList(2, edea, 1);
        OpenList(LIMIT_KIND_TEMP, 2, 1, 0, 18);
        PollLimitMenus();
        checkStr(g_spoken, "Sorcery. Ice Strike",
                 "**Edea: the same path, a different row and a different command**");

        // A greyed row still announces itself rather than going silent -- the
        // player has to know the option is there and unavailable.
        ResetReader();
        memset((void*)0x01D76800, 0, 0x200);
        Row locked[1] = { { LIMIT_KIND0_SEIFER, 1, 0x02 } };
        BuildList(0, locked, 1);
        OpenList(LIMIT_KIND_TEMP, 0, 1, 0, 17);
        PollLimitMenus();
        checkStr(g_spoken, "Fire Cross. No Mercy, not available",
                 "a disabled row is named AND marked, never dropped");

        // Kind 0 is not ammunition: it must not read a count.
        ResetReader();
        memset((void*)0x01D76800, 0, 0x200);
        Row stocked[1] = { { LIMIT_KIND0_SEIFER, 7, 0 } };
        BuildList(0, stocked, 1);
        OpenList(LIMIT_KIND_TEMP, 0, 1, 0, 17);
        PollLimitMenus();
        check(strstr(g_spoken, "left") == nullptr,
              "and it is not ammunition, so no count is spoken however much stock says");

        // The kind byte and the resolver must still agree for kind 0.
        ResetReader();
        memset((void*)0x01D76800, 0, 0x200);
        BuildList(0, seifer, 1);
        OpenList(LIMIT_KIND_TEMP, 0, 1, 0, 17);
        W32(LIM_NAMEFN, LIMIT_NAME_FN[LIMIT_KIND_BLUE]);
        PollLimitMenus();
        check(g_speakCount == 0,
              "a kind-0 byte with Quistis's resolver installed is refused, like every other kind");
    }

    // ---- the limit command's own name on the turn line ---------------------
    //
    // 0x004BCEFC reads ONE BYTE at 0x01CFF02E + slot*464 and hands it to
    // 0x0047EBD0; that is the command the game scrolls across row 0 while the
    // limit toggle is set. Reading it is what lets the turn line say
    // "Renzokuken" or "Fire Cross" instead of a generic "Limit Break" -- and it
    // is the reason Squall can stand in for Seifer in a BAT: same byte, same
    // resolver, same sentence.
    {
        char name[64];
        // **Literal address and literal stride, from 0x004BB77E and 0x004BCEFC
        // -- not LIMIT_CMD_ORIGIN / LIMIT_LIST_STRIDE.** Writing through the
        // same constants the reader uses would pass for any value of them; this
        // way, moving the constant one byte fails the probe.
        for (int slot = 0; slot < 3; slot++)
            *(uint8_t*)(uintptr_t)(0x01CFF02E + slot * 464) = 0;

        *(uint8_t*)(uintptr_t)(0x01CFF02E + 0 * 464) = 17;  // Seifer -- Fire Cross
        *(uint8_t*)(uintptr_t)(0x01CFF02E + 1 * 464) = 5;   // Squall -- Renzokuken
        *(uint8_t*)(uintptr_t)(0x01CFF02E + 2 * 464) = 18;  // Edea   -- Sorcery

        check(LimitCommandIdForSlot(0) == 17 && LimitCommandIdForSlot(1) == 5 &&
              LimitCommandIdForSlot(2) == 18,
              "the limit command id is one byte at 0x01CFF02E, striding 464 like the row array");
        check(LimitCommandIdForSlot(3) == -1 && LimitCommandIdForSlot(-1) == -1,
              "and a slot outside the party has none");

        check(LimitCommandNameForSlot(0, name, sizeof(name)) && strcmp(name, "Fire Cross") == 0,
              "**slot 0 -> Fire Cross, which is what the game draws for Seifer**");
        check(LimitCommandNameForSlot(1, name, sizeof(name)) && strcmp(name, "Renzokuken") == 0,
              "slot 1 -> Renzokuken, the BAT-able case that proves the same three steps");
        check(LimitCommandNameForSlot(2, name, sizeof(name)) && strcmp(name, "Sorcery") == 0,
              "slot 2 -> Sorcery, for Edea");
        check(!LimitCommandNameForSlot(3, name, sizeof(name)) && name[0] == '\0',
              "and an impossible slot yields nothing to say rather than a wrong word");
    }

    printf(bad ? "battle_limit_compile: FAILED (%d bad)\n"
               : "battle_limit_compile: OK (%d bad)\n", bad);
    return bad ? 1 : 0;
}
