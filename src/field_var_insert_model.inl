// field_var_insert_model.inl -- WHICH SLOT A 0x04 NUMERIC INSERT MEANS
//
// v0.116.0 (#89, closing the .298 probe). The 2026-08-27 Centra Ruins log has
// the answer the probe was waiting for, twice:
//
//   [VAR-EXPAND] "Squall stocked . " s" -> "Squall stocked  Aeros"
//   [VAR-DROP] code=0x04 param=0x37 ... table[0x20..0x27] = 0 0 0 0 0 0 0 6
//   [VAR-EXPAND] "Rinoa stocked . " s"  -> "Rinoa stocked  Pains"
//   [VAR-DROP] code=0x04 param=0x37 ... table[0x20..0x27] = 0 0 0 0 0 0 0 7
//
// Every draw at a field draw point has been announcing the magic without the
// quantity since the mod first spoke one. The value was sitting in the probe's
// own dump the whole time.
//
// v0.18.3.298 refused to widen the accepted range on that evidence alone --
// "if params >= 0x28 resolve through a different table, we would splice an
// unrelated number into the sentence" -- and it was right to. So this does not
// widen anything by inference. It reads sub_4B8E40 (0x004B8E40) out of
// FF8_EN.exe and reproduces the branch table:
//
//   0x004B8E56  cmp eax,0x20 / jl  -> next     ; range A
//   0x004B8E63  cmp eax,0x27 / jg  -> next
//   0x004B8E72  mov ecx,[eax*4 + 0x1D2B4B0]
//
//   0x004B8F3F  cmp eax,0x30 / jl  -> next     ; range B
//   0x004B8F44  cmp eax,0x37 / jg  -> next
//   0x004B8F4F  mov ecx,[eax*4 + 0x1D2B470]
//
//   0x004B8FBE  cmp eax,0x40 / jl  -> out      ; range C
//   0x004B8FC3  cmp eax,0x47 / jg  -> out
//   0x004B8FC8  mov edi,[eax*4 + 0x1D2B430]
//
// AND THE THREE BASES ARE THE SAME EIGHT DWORDS. Each base is pre-biased by
// its own range start, so every one of them resolves to 0x01D2B530:
//
//   0x1D2B4B0 + 0x20*4 = 0x1D2B530
//   0x1D2B470 + 0x30*4 = 0x1D2B530
//   0x1D2B430 + 0x40*4 = 0x1D2B530
//
// There is ONE array of eight numbers, addressed three ways. The ranges differ
// only in how the digits are DRAWN: range A groups them with the separator
// glyph at [0x1D76620] (thousands commas), range B emits the bare decimal, and
// range C emits eight fixed-width nibbles through the glyph table at
// 0x1D7660F. For speech all of that collapses to the decimal value, which is
// why A and B can share one path here.
//
// That is what makes 0x37 safe: it is not a different table, it is slot 7 of
// the same table under the no-comma renderer -- exactly the dword the probe
// printed as `table[0x27]`, and exactly the 6 and the 7 Aaron drew.
//
// RANGE C IS STILL DROPPED, deliberately. Its renderer is not decimal (nibble
// + 1 through a glyph table, all eight emitted including leading ones), no
// message in this log or the previous three uses it, and a format nobody has
// seen produce a number on screen is not one to guess at. It gets its own log
// line so the next occurrence names itself.

static const uintptr_t FVI_SLOT_BASE  = 0x01D2B530u;  // dword[slot], 8 slots
static const int       FVI_SLOT_COUNT = 8;
static const int       FVI_NO_SLOT    = -1;

// Range A (0x20..0x27) and range B (0x30..0x37) both mean "slot n, as decimal".
// Range C (0x40..0x47) means slot n in a non-decimal format and is reported
// separately rather than answered wrongly.
static int FviSlotForParam(int param)
{
    if (param >= 0x20 && param <= 0x27) return param - 0x20;
    if (param >= 0x30 && param <= 0x37) return param - 0x30;
    return FVI_NO_SLOT;
}

static bool FviParamIsNibbleFormat(int param)
{
    return param >= 0x40 && param <= 0x47;
}

static uintptr_t FviAddressForSlot(int slot)
{
    if (slot < 0 || slot >= FVI_SLOT_COUNT) return 0;
    return FVI_SLOT_BASE + (uintptr_t)slot * 4u;
}
