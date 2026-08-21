// field_menuname_model.inl -- what opcode_menuname's parameter actually selects.
//
// Included from field_dialog.cpp BEFORE field_dialog_menuname.inl, and compiled
// standalone by tests/menuname_compile.cpp. Nothing here touches game memory,
// Windows, or the screen reader: it is three tables read out of FF8_EN.exe, so
// the probe can check every parameter without a running game.
//
// ============================================================================
// WHERE THIS COMES FROM (v0.38.1, #98 -- read out of FF8_EN.exe)
// ============================================================================
//
// `opcode_menuname` is 0x00521DA0. It sets menu mode 5 (`[0x01CE4760] = 5`),
// pops one parameter off the script stack, and dispatches:
//
//   0x00521DC6  cmp edx, 0x14
//   0x00521DC9  ja  0x00521FB0            <- default, sets no kind at all
//   0x00521DCF  jmp dword ptr [edx*4 + 0x00521FB8]
//
// So the parameter is 0..0x14 -- TWENTY-ONE cases. Every case writes a screen
// kind to `[0x01CE4762]` and returns 3; sixteen of them also do this first:
//
//   0x00521E03  push 0              <- GF index
//   0x00521E05  mov word [0x01CE4762], 5
//   0x00521E0E  call 0x0047E480
//
// and `0x0047E480` is four instructions long:
//
//   mov eax, [esp+4] / mov ecx, eax / shl ecx, 4 / add ecx, eax
//   lea eax, [ecx*4 + 0x01CFDCB9] / mov cl, [ecx*4 + 0x01CFDCB9]
//   or cl, 1 / mov [eax], cl / ret
//
// `0x01CFDCB9` is savemap + 0x4C + 0x11 and the scale is 68 (0x44) -- the GF
// record stride. That is the GF "obtained" flag, and its argument is therefore
// a GF INDEX, 0..15, not a character index.
//
// **This is the bug that hid here for eleven versions.** The old hook read the
// parameter as a party-member index and spoke
// {Squall, Zell, Irvine, Quistis, Rinoa, Selphie, Seifer, Edea}[param], so the
// classroom study-panel scene -- which names Quezacotl and Shiva -- announced
// "Quistis" and "Rinoa". The comment in the old code even rationalised it
// ("Quistis/Rinoa at study panel"): the wrong model had been fitted to the
// symptom instead of being checked against the switch.
//
// KIND IS GF INDEX + 5 for all sixteen GF cases, without exception. What is
// NOT uniform is the jump table, which transposes two pairs:
//
//   param  8 -> 0x00521E99  push 6   kind 0x0B     Carbuncle
//   param  9 -> 0x00521E80  push 5   kind 0x0A     Diablos
//   param 14 -> 0x00521F2F  push 0xC kind 0x11     Bahamut
//   param 15 -> 0x00521F16  push 0xB kind 0x10     Doomtrain
//
// The handlers are laid out in kind order; the table entries for those two
// pairs are swapped. Reading the handlers in address order and assuming the
// parameter follows them gets Diablos/Carbuncle and Doomtrain/Bahamut backwards
// -- which is exactly why the probe decodes the real jump table rather than
// trusting a transcription.
//
// THE FIVE THAT ARE NOT GFs. Params 0, 1, 2 (kinds 2, 3, 4) and params 19, 20
// (kinds 0x1B, 0x1C) make no GF call. FF8 has exactly five names a player can
// change that are not GFs -- Squall, Rinoa, Angelo, Boko, Griever -- and the
// engine reads all five live out of the savemap (see the ladder at 0x004B8D94,
// documented in ff8_text_decode.cpp). 16 + 5 = 21 = the case count, so these
// five parameters ARE those five names; that part is closed.
//
// WHICH is which is only closed for param 0. Kinds 2/3/4 are contiguous and sit
// immediately below kind 5 = GF 0, in the same order as the savemap's own name
// slots (Squall +0x14, Rinoa +0x20, Angelo +0x2C, stride 12), and param 0 =
// Squall is confirmed by the mod's own field logs. Params 1 and 2 are therefore
// Rinoa and Angelo by ordering, which is an INFERENCE -- the name buffer the
// naming screen writes to lives in a menu.fs overlay, not in FF8_EN.exe, so it
// cannot be settled statically from the executable alone. It is labelled
// INFERRED below and the hook logs the parameter next to the name it spoke, so
// one field visit either confirms it or refutes it.
//
// Params 19 and 20 are the remaining two, Boko and Griever, in an order this
// file does NOT claim. They resolve to name id 0 and the hook stays silent
// rather than speaking a coin flip.
// ============================================================================

#pragma once

namespace FieldMenunameModel {

// Engine: `cmp edx, 0x14 / ja default`.
static const int MENUNAME_PARAM_COUNT = 21;

// GF index pushed to 0x0047E480, or -1 when this parameter names a non-GF.
static const int MENUNAME_GF[MENUNAME_PARAM_COUNT] = {
    -1, -1, -1,               //  0, 1, 2   nameable, no GF call
     0,  1,  2,  3,  4,       //  3..7      Quezacotl Shiva Ifrit Siren Brothers
     6,  5,                   //  8, 9      TRANSPOSED: Carbuncle, Diablos
     7,  8,  9, 10,           // 10..13     Leviathan Pandemona Cerberus Alexander
    12, 11,                   // 14, 15     TRANSPOSED: Bahamut, Doomtrain
    13, 14, 15,               // 16..18     Cactuar Tonberry Eden
    -1, -1                    // 19, 20     nameable, no GF call
};

// Screen kind written to [0x01CE4762]. GF cases satisfy kind == GF + 5.
static const int MENUNAME_KIND[MENUNAME_PARAM_COUNT] = {
    0x02, 0x03, 0x04,
    0x05, 0x06, 0x07, 0x08, 0x09,
    0x0B, 0x0A,
    0x0C, 0x0D, 0x0E, 0x0F,
    0x11, 0x10,
    0x12, 0x13, 0x14,
    0x1B, 0x1C
};

// 0x03 name id for the non-GF parameters, resolved live from the savemap by
// FF8TextDecode. 0 means "this parameter names something, but which of the two
// remaining names is not settled" -- speak nothing.
//   0x30 Squall (confirmed)   0x34 Rinoa (INFERRED)   0x40 Angelo (INFERRED)
static const int MENUNAME_NAME_ID[MENUNAME_PARAM_COUNT] = {
    0x30, 0x34, 0x40,
    0, 0, 0, 0, 0,
    0, 0,
    0, 0, 0, 0,
    0, 0,
    0, 0, 0,
    0, 0
};

static inline bool MenunameParamValid(int param)
{
    return param >= 0 && param < MENUNAME_PARAM_COUNT;
}

// GF index for a parameter, or -1 (not a GF, or out of range).
static inline int MenunameGfIndex(int param)
{
    return MenunameParamValid(param) ? MENUNAME_GF[param] : -1;
}

// Screen kind for a parameter, or -1. The engine's own default (param > 0x14)
// writes no kind at all, which is why out of range is -1 and not 0.
static inline int MenunameKind(int param)
{
    return MenunameParamValid(param) ? MENUNAME_KIND[param] : -1;
}

// 0x03 name id for a parameter, or 0 when there is nothing safe to say.
static inline int MenunameNameId(int param)
{
    return MenunameParamValid(param) ? MENUNAME_NAME_ID[param] : 0;
}

}  // namespace FieldMenunameModel
