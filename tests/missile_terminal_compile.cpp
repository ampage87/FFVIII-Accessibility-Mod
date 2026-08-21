// missile_terminal_compile.cpp -- the Missile Base terminal model, checked
// against the game's own bytes.
//
//   g++ -std=c++17 -O0 -Isrc -o missile_terminal_compile \
//       tests/missile_terminal_compile.cpp src/ff8_text_decode.cpp
//
// The labels are NOT re-typed from the model. Below are the FF8-encoded bytes of
// messages 0-5 of `gmmoni1.msd`, lifted out of field.fs, decoded here by the
// real decoder, and compared to what the reader will say. Cursor value equals
// message id on both menus, so `MAIN_LABELS[i]` must equal message i and
// `SUB_LABELS[i]` must equal message {0,4,5,3}[i] -- and a shifted reading of
// the .msd (its first dword is the first message's OFFSET, not a count, which
// turns "SET TARGET" into "CONFIRM EQUIPMENT") fails this.
//
// v0.40.0 (#101).

#include <cstdio>
#include <cstdint>
#include <cstring>
#include <string>

#include "ff8_text_decode.h"
#include "missile_terminal_model.inl"

using namespace MissileTerminal;

static int bad = 0;
static void check(bool ok, const char* what)
{
    if (!ok) { std::printf("  BAD: %s\n", what); bad++; }
}

// ---- gmmoni1.msd messages 0-5, verbatim -----------------------------------
static const uint8_t MSG0[] = {0xAE,0x57,0x49,0x58,0x20,0x58,0x45,0x56,0x4B,0x49,0x58,0xAF,0};
static const uint8_t MSG1[] = {0xAE,0x47,0x53,0x52,0x4A,0x4D,0x56,0x51,0x20,0x49,0x55,0x59,0x4D,0x54,0x51,0x49,0x52,0x58,0xAF,0};
static const uint8_t MSG2[] = {0xAE,0x57,0x4D,0x51,0x59,0x50,0x45,0x58,0x4D,0x53,0x52,0xAF,0};
static const uint8_t MSG3[] = {0xAE,0x49,0x5C,0x4D,0x58,0xAF,0};
static const uint8_t MSG4[] = {0xAE,0x57,0x49,0x58,0x20,0x49,0x56,0x56,0x53,0x56,0x20,0x56,0x45,0x58,0x4D,0x53,0xAF,0};
static const uint8_t MSG5[] = {0xAE,0x48,0x45,0x58,0x45,0x20,0x59,0x54,0x50,0x53,0x45,0x48,0xAF,0};

static const uint8_t* const MSG[6] = { MSG0, MSG1, MSG2, MSG3, MSG4, MSG5 };
static const size_t MSGLEN[6] = { sizeof(MSG0), sizeof(MSG1), sizeof(MSG2),
                                  sizeof(MSG3), sizeof(MSG4), sizeof(MSG5) };

// The mod says "Set target"; the screen says "SET TARGET". Compare ignoring case
// so the reader can be pronounceable without weakening the assertion.
static bool SameIgnoringCase(const char* a, const std::string& b)
{
    size_t i = 0;
    for (; a[i] && i < b.size(); i++) {
        char ca = a[i], cb = b[i];
        if (ca >= 'A' && ca <= 'Z') ca = (char)(ca - 'A' + 'a');
        if (cb >= 'A' && cb <= 'Z') cb = (char)(cb - 'A' + 'a');
        if (ca != cb) return false;
    }
    return a[i] == '\0' && i == b.size();
}

int main()
{
    // ---- labels vs the game's bytes ---------------------------------------
    std::string decoded[6];
    for (int i = 0; i < 6; i++) {
        int dropped = 0;
        decoded[i] = FF8TextDecode::Decode(MSG[i], MSGLEN[i], &dropped);
        if (dropped != 0) {
            std::printf("  BAD: message %d lost %d byte(s) in decoding\n", i, dropped);
            bad++;
        }
    }
    check(SameIgnoringCase("Set target", decoded[0]),        "message 0 is SET TARGET");
    check(SameIgnoringCase("Confirm equipment", decoded[1]), "message 1 is CONFIRM EQUIPMENT");
    check(SameIgnoringCase("Simulation", decoded[2]),        "message 2 is SIMULATION");
    check(SameIgnoringCase("Exit", decoded[3]),              "message 3 is EXIT");
    check(SameIgnoringCase("Set error ratio", decoded[4]),   "message 4 is SET ERROR RATIO");
    check(SameIgnoringCase("Data upload", decoded[5]),       "message 5 is DATA UPLOAD");

    // **Cursor value == message id.** This is the assertion the whole readout
    // rests on, and it is checked against the decode above, not against a
    // second copy of the table.
    for (int c = 0; c <= 3; c++)
        check(SameIgnoringCase(MAIN_LABELS[c], decoded[c]),
              "main-menu cursor c reads message c");
    static const int SUB_MSG[4] = { 0, 4, 5, 3 };
    for (int c = 0; c <= 3; c++)
        check(SameIgnoringCase(SUB_LABELS[c], decoded[SUB_MSG[c]]),
              "SET TARGET cursor c reads message {0,4,5,3}[c]");

    // ---- the bar ----------------------------------------------------------
    check(RatioSteps(0) == 0 && RatioSteps(-156) == 26, "27 positions, 0 to 26");
    check(RatioSteps(-6) == 1 && RatioSteps(-150) == 25, "and one step is six units");
    check(RatioPercent(0) == 0, "the left end is 0 percent");
    check(RatioPercent(-156) == 100, "**the right end is 100 percent, not 99** -- the division rounds");
    check(RatioPercent(-78) == 50, "halfway is 50 percent");
    check(!RatioAtMax(-144), "one step short of the threshold is not maximum");
    check(RatioAtMax(-150), "**-150 IS accepted** -- the script is `<= -150`, not `== -156`");
    check(RatioAtMax(-156), "and so is the far end");
    check(!RatioAtMax(0), "the left end is not");
    check(RatioValid(0) && RatioValid(-6) && RatioValid(-156), "positions the script can produce");
    check(!RatioValid(-3) && !RatioValid(6) && !RatioValid(-162),
          "and ones it cannot: off-step, positive, past the end");
    check(RatioPercent(500) == 0 && RatioPercent(-9999) == 100 &&
          RatioSteps(500) == 0 && RatioSteps(-9999) == 26,
          "a nonsense read is clamped rather than spoken as a wild number");

    // ---- which screen owns var[1028] --------------------------------------
    // The uploader has two options and the equipment screen five, so 2..4 is
    // decisive; below that the path decides.
    check(SharedCursorScreen(2, 1, 2) == SCR_EQUIP &&
          SharedCursorScreen(4, 0, 2) == SCR_EQUIP,
          "**a cursor of 2 or more can only be the equipment screen**");
    check(SharedCursorScreen(1, 0, 2) == SCR_UPLOAD,
          "reached from SET TARGET item 2, it is the upload confirmation");
    check(SharedCursorScreen(1, 1, 0) == SCR_EQUIP,
          "reached from main-menu item 1, it is the equipment screen");
    check(SharedCursorScreen(0, 1, 2) == SCR_UPLOAD,
          "and SET TARGET wins when both look plausible, because it is the deeper screen");

    // ---- what confirm does on the upload screen ---------------------------
    check(std::strcmp(UploadLabel(1), "Yes, upload") == 0,
          "**cursor 1 is the branch that sets the uploaded bit**");
    check(std::strcmp(UploadLabel(0), "No, cancel") == 0,
          "cursor 0 is the branch that just leaves");

    // ---- addresses --------------------------------------------------------
    check(FIELD_VAR_BASE + VAR_MAIN_CURSOR == 0x01CFEDB8, "main cursor at 0x01CFEDB8");
    check(FIELD_VAR_BASE + VAR_SUB_CURSOR  == 0x01CFEDB9, "SET TARGET cursor at 0x01CFEDB9");
    check(FIELD_VAR_BASE + VAR_SHARED_CUR  == 0x01CFEDBC, "shared cursor at 0x01CFEDBC");
    check(FIELD_VAR_BASE + VAR_RATIO       == 0x01CFEB9A, "ratio word at 0x01CFEB9A");
    check(FIELD_VAR_BASE + VAR_FLAGS       == 0x01CFEB9C, "progress flags at 0x01CFEB9C");

    check(MainCursorValid(3) && !MainCursorValid(4) && !MainCursorValid(-1), "main menu has four items");
    check(UploadCursorValid(1) && !UploadCursorValid(2), "the upload confirmation has two");
    check(EquipCursorValid(4) && !EquipCursorValid(5), "the equipment screen has five");

    std::printf(bad ? "missile_terminal_compile: FAILED (%d bad)\n"
                    : "missile_terminal_compile: OK (%d bad)\n", bad);
    return bad ? 1 : 0;
}
