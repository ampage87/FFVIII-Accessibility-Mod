// missile_terminal_model.inl -- the PURE model of the Galbadia Missile Base
// coordinate terminal (field `gmmoni1`).
//
// Included from field_dialog.cpp before field_missile_terminal.inl, and compiled
// standalone by tests/missile_terminal_compile.cpp. Nothing here touches game
// memory, Windows or the screen reader.
//
// ============================================================================
// WHERE THIS COMES FROM (v0.40.0, #101 -- read out of field.fs and FF8_EN.exe)
// ============================================================================
//
// Aaron: *"there is a scene where the player must interact with a computer
// terminal and set the error ratio to maximum."* The terminal is built entirely
// out of field entities and background layers -- no menu module is involved,
// which is why nothing in the mod sees its state -- and every screen it has is
// a loop over FIELD VARIABLES at `0x01CFE9B8 + index`.
//
// Field `gmmoni1` has FIVE interactive screens, and the script names them:
//
//   Director::default        main menu           cursor var[1024]  0..3
//   settarget::targetmenu    SET TARGET submenu  cursor var[1025]  0..3
//   control::errorratioset   the ratio bar             var[482]    0..-156
//   control::uploader        DATA UPLOAD yes/no  cursor var[1028]  0..1
//   control::equipment       CONFIRM EQUIPMENT   cursor var[1028]  0..4
//
// **Cursor value == message id on both menus.** `Director::default` ends each of
// its four blocks with `MES(win 1, msg n, 16, 160)` where n is the cursor, and
// `settarget::targetmenu` does the same with messages 0, 4, 5, 3. So the labels
// below are a table lookup, not an interpretation -- and the probe asserts them
// against the real `gmmoni1.msd` bytes rather than against this file.
//
// **The ratio is a bar position, not a number.** `control::errorratioset` holds
// right/left (`0x2000`/`0x8000`) to run steps that do `var[482] -= 6` while
// `var[482] > -156`, so there are 27 positions from 0 to -156. On confirm:
//
//     if (var[482] <= -150)  var[484] |= 1;      <- accepted
//     else                   nothing happens     <- SILENTLY refused
//
// That silent refusal is the trap the scene is built around, and it is why the
// mod reports the outcome rather than only the value.
//
// **DATA UPLOAD does nothing until the ratio is accepted.** `control::uploader`
// on confirm does `if (var[484] & 1) var[484] |= 2;` -- choosing Yes with the
// ratio unset is indistinguishable, on screen, from choosing Yes with it set.
//
// **And SET TARGET's EXIT refuses to leave** until BOTH bits are set, with one
// of the party's nag lines. Reading `var[484]` says why in one clause.
//
// ============================================================================
// TWO THINGS A FIELD-SCRIPT READER GETS WRONG SILENTLY (both bit me here)
// ============================================================================
//
// * A method's entry index is `entries[start + M + 1]`, not `entries[start + M]`
//   -- the group word's `start` points at the entry BEFORE the entity's first
//   method. Off by one, every entity's LAST method disappears, and the first
//   pass through this field found three screens instead of five: the SET TARGET
//   submenu and CONFIRM EQUIPMENT were both an entity's last method.
// * The entity id a `REQ` uses is not the group-table index; they differ by a
//   rotation of sixteen. The `.sym` names are in entity-id order, which is why
//   they look scrambled against the group table.
//
// Neither is a claim about this file's tables -- they are why the tables have
// five screens in them and not three.
// ============================================================================

#pragma once

namespace MissileTerminal {

// Field variables live at this base, indexed by byte (JSM opcodes 0x0A..0x11).
static const uintptr_t FIELD_VAR_BASE = 0x01CFE9B8;

static const int VAR_MAIN_CURSOR = 1024;   // 0x01CFEDB8  Director::default
static const int VAR_SUB_CURSOR  = 1025;   // 0x01CFEDB9  settarget::targetmenu
static const int VAR_INPUT_LATCH = 1027;   // 0x01CFEDBB
static const int VAR_SHARED_CUR  = 1028;   // 0x01CFEDBC  uploader AND equipment
static const int VAR_CONFIRM     = 1029;   // 0x01CFEDBD
static const int VAR_SCREEN      = 1030;   // 0x01CFEDBE
static const int VAR_RATIO       = 482;    // 0x01CFEB9A  SIGNED WORD
static const int VAR_FLAGS       = 484;    // 0x01CFEB9C

static const int FLAG_RATIO_SET  = 0x01;
static const int FLAG_UPLOADED   = 0x02;

// The bar: 0 down to -156 in steps of 6, and confirm is accepted at -150.
static const int RATIO_MIN_VALUE = -156;   // furthest the bar travels
static const int RATIO_STEP      = 6;
static const int RATIO_ACCEPT_AT = -150;   // `var[482] <= -150` in the script

enum Screen {
    SCR_NONE = 0,
    SCR_MAIN,      // Director::default
    SCR_TARGET,    // settarget::targetmenu
    SCR_RATIO,     // control::errorratioset
    SCR_UPLOAD,    // control::uploader
    SCR_EQUIP      // control::equipment
};

// Cursor value == message id. Kept as literals only so the reader has something
// to say; tests/missile_terminal_compile.cpp checks each one against the
// decoded bytes of gmmoni1.msd and fails if they drift.
static const char* const MAIN_LABELS[4] = {
    "Set target", "Confirm equipment", "Simulation", "Exit"
};
static const char* const SUB_LABELS[4] = {
    "Set target", "Set error ratio", "Data upload", "Exit"
};

static inline bool MainCursorValid(int c)   { return c >= 0 && c <= 3; }
static inline bool SubCursorValid(int c)    { return c >= 0 && c <= 3; }
static inline bool UploadCursorValid(int c) { return c >= 0 && c <= 1; }
static inline bool EquipCursorValid(int c)  { return c >= 0 && c <= 4; }

// Is this a bar position the script could actually have produced?
static inline bool RatioValid(int v)
{
    return v <= 0 && v >= RATIO_MIN_VALUE && (v % RATIO_STEP) == 0;
}

// 0 at the left end, 26 at the right.
static inline int RatioSteps(int v)
{
    if (v > 0) v = 0;
    if (v < RATIO_MIN_VALUE) v = RATIO_MIN_VALUE;
    return -v / RATIO_STEP;
}

// Rounded, so the far end reads 100 and not 99.
static inline int RatioPercent(int v)
{
    if (v > 0) v = 0;
    if (v < RATIO_MIN_VALUE) v = RATIO_MIN_VALUE;
    return (-v * 100 + (-RATIO_MIN_VALUE / 2)) / -RATIO_MIN_VALUE;
}

// The only question the scene actually asks.
static inline bool RatioAtMax(int v) { return v <= RATIO_ACCEPT_AT; }

// `var[1028]` is shared by the uploader and the equipment screen, so the value
// alone cannot say which is open. The path can: the uploader is reached from
// SET TARGET item 2 and the equipment screen from main-menu item 1. A value of
// 2 or more settles it outright, because the uploader has only two options.
static inline Screen SharedCursorScreen(int value, int lastMainCursor, int lastSubCursor)
{
    if (value >= 2) return SCR_EQUIP;
    if (lastSubCursor == 2)  return SCR_UPLOAD;
    if (lastMainCursor == 1) return SCR_EQUIP;
    return SCR_UPLOAD;
}

// What confirming does on the upload screen, which is what the mod says --
// the on-screen sprites are YES and NO, but the effect is what is true
// whichever way round they sit.
static inline const char* UploadLabel(int cursor)
{
    return (cursor == 1) ? "Yes, upload" : "No, cancel";
}

}  // namespace MissileTerminal
