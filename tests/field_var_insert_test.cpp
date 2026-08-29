// field_var_insert_test.cpp -- 0x04 numeric-insert slots (#89, v0.116.0).
//
// Every assertion was written against a mutant. The numbers come from
// sub_4B8E40 in FF8_EN.exe, not from a guess about what "looks like" a range.
#include <cstdio>
#include <cstring>
#include <cstdint>

#include "field_var_insert_model.inl"

static int g_fail = 0;
static void CHECK(bool c, const char* w) { if (!c) { printf("FAIL: %s\n", w); g_fail++; } }

int main()
{
    // THE PROPERTY THAT MADE THE FIX SAFE: range A and range B are the same
    // eight dwords. The engine reaches them through pre-biased bases --
    // 0x1D2B4B0 + param*4 for A, 0x1D2B470 + param*4 for B -- and both land on
    // 0x1D2B530 + slot*4. If that is ever untrue the fix is splicing an
    // unrelated number into a sentence, which is the exact failure v0.18.3.298
    // refused to risk. Asserted here as arithmetic so it cannot rot.
    for (int p = 0x20; p <= 0x27; p++) {
        CHECK(FviAddressForSlot(FviSlotForParam(p)) == 0x01D2B4B0u + (uintptr_t)p * 4u,
              "range A resolves to the address the old code read");
    }
    for (int p = 0x30; p <= 0x37; p++) {
        CHECK(FviAddressForSlot(FviSlotForParam(p)) == 0x01D2B470u + (uintptr_t)p * 4u,
              "range B resolves to the engine's own base + param*4");
    }
    // ...and therefore A[n] and B[n] are one and the same slot.
    for (int n = 0; n < 8; n++) {
        CHECK(FviSlotForParam(0x20 + n) == n, "range A slot index");
        CHECK(FviSlotForParam(0x30 + n) == n, "range B slot index");
        CHECK(FviAddressForSlot(n) == 0x01D2B530u + (uintptr_t)n * 4u, "slot address");
    }

    // THE LIVE CASE. 2026-08-27: param 0x37 with table[0x27] holding 6, then 7 --
    // "Squall stocked 6 Aeros", "Rinoa stocked 7 Pains". 0x37 is slot 7, which
    // is the dword the probe printed as table[0x27]. Kills any mutant that maps
    // 0x37 to slot 0x37 or drops it.
    CHECK(FviSlotForParam(0x37) == 7, "0x37 is slot 7 -- the drawn quantity");
    CHECK(FviAddressForSlot(7) == 0x01D2B4B0u + 0x27u * 4u,
          "slot 7 is exactly what the .298 probe logged as table[0x27]");

    // The prison floor indicator is param 0x30 -> slot 0, the same dword the
    // .299 probe watched count 6 -> 5 -> 4.
    CHECK(FviSlotForParam(0x30) == 0, "0x30 is slot 0 -- the prison floor number");

    // BOUNDARIES. Kills >= / > and <= / < mutants on all four edges.
    CHECK(FviSlotForParam(0x1F) == FVI_NO_SLOT, "0x1F is below range A");
    CHECK(FviSlotForParam(0x28) == FVI_NO_SLOT, "0x28 is above range A");
    CHECK(FviSlotForParam(0x2F) == FVI_NO_SLOT, "0x2F is below range B");
    CHECK(FviSlotForParam(0x38) == FVI_NO_SLOT, "0x38 is above range B");
    CHECK(FviSlotForParam(0x00) == FVI_NO_SLOT, "0 is not a slot");

    // RANGE C IS A SLOT WE REFUSE TO ANSWER, and it must not silently become a
    // decimal one. Kills a mutant that folds 0x40..0x47 into FviSlotForParam:
    // its renderer emits eight fixed-width nibbles through a glyph table, so
    // reading it as decimal would speak a number the screen never showed.
    for (int p = 0x40; p <= 0x47; p++) {
        CHECK(FviSlotForParam(p) == FVI_NO_SLOT, "range C is not a decimal slot");
        CHECK(FviParamIsNibbleFormat(p), "range C is recognised as the nibble format");
    }
    CHECK(!FviParamIsNibbleFormat(0x3F), "0x3F is not range C");
    CHECK(!FviParamIsNibbleFormat(0x48), "0x48 is not range C");
    CHECK(!FviParamIsNibbleFormat(0x37), "a decimal param is not the nibble format");

    // An out-of-range slot has no address rather than a plausible-looking one.
    CHECK(FviAddressForSlot(-1) == 0, "no address for a non-slot");
    CHECK(FviAddressForSlot(8)  == 0, "no address past the eighth slot");

    printf("field_var_insert_test: fail=%d\n", g_fail);
    return g_fail ? 1 : 0;
}
