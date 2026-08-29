// field_disc3.inl -- the three disc-3 blockers, wired up.
//
// PART OF field_navigation.cpp -- TEXTUAL INCLUDE. Do NOT compile standalone.
// Included AFTER esthar_pandora_model.inl, space_rescue_model.inl and
// propagator_model.inl, and after field_minigame_bgbtl.inl (it borrows that
// module's tone).
//
// ============================================================================
// HOW A FIELD IS RECOGNISED, AND WHY IT IS BOTH WAYS
// ============================================================================
//
// Every other minigame in this mod keys on `pCurrentFieldId`, because it is
// exact and immediate. Here the ids had to be DERIVED: Aaron has never stood in
// Esthar, the Ragnarok or space with the mod running, so no log has ever paired
// those ids with their names.
//
// The derivation is sound -- field ids are alphabetical by internal name with
// `_` sorting before digits, fitted to 487 (id, name) pairs mined from Aaron's
// own logs, then anchored per block by a fact from the game's own scripts:
//
//     ec  offset +97   ecenc1/2/3 = 417/418/419, which is what test14's debug
//                      room MAPJUMPs to for "Encounter Lunatic Pandora 1/2/3"
//     rg  offset +74   rgcock3 = 826, which is where rgroad1::lift::talk jumps
//                      when var[446] reaches 255 (the puzzle's own exit)
//     ss  offset +74   ssspace2 = 877, which is the MAPJUMPO ssspace3's win
//                      path takes
//
// Sound is not the same as verified, and a wrong id would fail SILENTLY -- the
// whole feature simply never speaks. So every check below is **id OR name**.
// `pCurrentFieldName` lags the id by 2-5 seconds (chase_detector.cpp finding 3),
// which is useless for the space skip and perfectly fine for the other two, so
// the id is what makes it responsive and the name is what makes it correct.
//
// And the first time a name matches, the id observed alongside it is logged.
// One BAT in any of these three places hands back the true ids whether the
// derivation was right or wrong.

namespace Disc3 {

typedef int (__cdecl *OpcodeFunc_t)(void* ctx, int param);   // unused here; kept
                                                             // for symmetry with
                                                             // the other modules
static const uint32_t D3_VAR_BASE = 0x01CFE9B8u;
static uint32_t D3VarAddr(int index) { return D3_VAR_BASE + (uint32_t)index; }

static bool D3ReadU8(uint32_t addr, uint8_t* out)
{
    __try { *out = *(volatile const uint8_t*)(uintptr_t)addr; return true; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
static bool D3ReadU16(uint32_t addr, uint16_t* out)
{
    __try { *out = *(volatile const uint16_t*)(uintptr_t)addr; return true; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
static bool D3ReadI32(uint32_t addr, int32_t* out)
{
    __try { *out = *(volatile const int32_t*)(uintptr_t)addr; return true; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
static void D3WriteI32(uint32_t addr, int32_t v)
{
    __try { *(volatile int32_t*)(uintptr_t)addr = v; }
    __except (EXCEPTION_EXECUTE_HANDLER) { }
}

static const char* D3FieldName()
{
    const char* n = FF8Addresses::pCurrentFieldName;
    return (n && n[0]) ? n : "";
}
static uint16_t D3FieldId()
{
    return FF8Addresses::pCurrentFieldId ? *FF8Addresses::pCurrentFieldId : 0xFFFF;
}

// id OR name, and log the pair the first time a name matches so a wrong
// derivation corrects itself on the next BAT rather than failing silently.
static bool D3Here(uint16_t wantId, const char* wantName, const char** why)
{
    const uint16_t id = D3FieldId();
    const char* nm = D3FieldName();
    const bool byName = (wantName && *wantName && _stricmp(nm, wantName) == 0);
    const bool byId   = (id == wantId);
    if (byName) {
        static const char* s_logged[8] = { nullptr };
        static int s_n = 0;
        bool seen = false;
        for (int i = 0; i < s_n; i++) if (s_logged[i] == wantName) seen = true;
        if (!seen && s_n < 8) {
            s_logged[s_n++] = wantName;
            Log::Field("FieldNavigation: [DISC3] field '%s' is id %u (derived %u) -- %s",
                       wantName, (unsigned)id, (unsigned)wantId,
                       byId ? "derivation CONFIRMED" : "**DERIVATION WRONG, use the observed id**");
        }
    }
    if (why) *why = byName ? "name" : (byId ? "id" : "");
    return byName || byId;
}

static void D3Say(const char* tag, const char* text, bool interrupt)
{
    if (!text || !*text) return;
    ScreenReader::Speak(text, interrupt);
    Log::Field("FieldNavigation: [%s] \"%s\"", tag, text);
}

// The help key, edge-triggered. Shared by all three so they cannot double-fire.
static bool s_slashWas = false;
static bool D3SlashPressed()
{
    const bool down = (GetAsyncKeyState(VK_OEM_2) & 0x8000) != 0;
    const bool hit = down && !s_slashWas;
    s_slashWas = down;
    return hit;
}
static bool s_f9Was = false;
static bool D3F9Pressed()
{
    const bool down = (GetAsyncKeyState(VK_F9) & 0x8000) != 0;
    const bool hit = down && !s_f9Was;
    s_f9Was = down;
    return hit;
}

#include "field_disc3_esthar.inl"
#include "field_disc3_space.inl"
#include "field_disc3_propagator.inl"

// v0.63.0 (#111): the catalog asks this. field_catalog.inl is compiled inside
// field_navigation.cpp AFTER this file, and separately by the catalog harness,
// which supplies its own `return false`. A named seam rather than a direct
// Disc3::Space::Active() call is what lets both compile the same source.
static void Update()
{
    const bool slash = D3SlashPressed();
    const bool f9    = D3F9Pressed();
    Esthar::Update(slash);
    Space::Update(slash, f9);
    Props::Update(slash);
}

} // namespace Disc3

// The seam field_catalog.inl consumes. See the note above Disc3::Update.
static bool SpaceRescueActive() { return Disc3::Space::Active(); }
