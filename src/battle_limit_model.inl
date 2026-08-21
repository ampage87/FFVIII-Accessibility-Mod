// battle_limit_model.inl -- the PURE model of FF8's limit-break menus.
//
// Included from battle_tts_menu.inl BEFORE battle_tts_limit.inl, and compiled
// standalone by tests/battle_limit_compile.cpp. Nothing in this file touches
// game memory, Windows, or the screen reader: it is arithmetic and table
// lookups derived from the disassembly, so the probe can exercise every branch
// without a running battle. Do not put reads of live memory here.
//
// ============================================================================
// WHERE THIS COMES FROM (v0.36.0, #94 -- read out of FF8_EN.exe)
// ============================================================================
//
// A battle command's kernel record (section 0, 39 entries of 8 bytes) carries
// a MENU-KIND byte at +5. `0x004BC7EA` masks it with 0x1F and, when bit 0x20
// is clear, jumps through the 9-entry table at `0x004BCA58`:
//
//   +5     command        idx  handler                       what it opens
//   0xA0   Attack, Renzokuken(5), Duel(11), Mug, Cast, Stock, MiniMog
//                                --   bit 0x20 SET           NO SUBMENU
//   0x80   Magic (2)        0   0x004C8840                   magic list
//   0x81   GF (3)           1   0x004C8280                   GF list
//   0x82   Item (4)         2   0x004C87A0 / 0x004C8550      item list
//   0x83   Draw (6)         3   0x004ADD10                   draw list
//   0x84   Shot (14)        4   0x004C8220 -> kind 3         IRVINE, ammo
//   0x85   Slot (16)        5   0x004C7920                   SELPHIE, own UI
//   0x86   Blue Magic (15)  6   0x004C81C0 -> kind 1         QUISTIS
//   0x87   Fire Cross(17), Sorcery(18), Limit(20,21,22)
//                           7   0x004C8190 -> kind 0         Seifer/Edea/Laguna/Kiros/Ward
//   0x88   Combine (19)     8   0x004C81F0 -> kind 2         RINOA
//
// **Squall and Zell have no submenu at all** -- Renzokuken and Duel both carry
// 0xA0, so choosing the command goes straight to targeting. That is why
// "just let me pick it from the command menu" is the whole job for those two:
// there is no list to read, and the existing v0.10.22 toggle announce already
// names the command.
//
// The four kinds are ONE list menu (`0x004C7D00` -> `0x004FF0C0`) that differs
// only in which pair of name/description resolvers it installs:
//
//   kind 0  name 0x0047E6B0  desc 0x0047E6E0   kernel sec 18, stride 24, 5 rows
//   kind 1  name 0x0047E650  desc 0x0047E680   kernel sec 19, stride 16, 16 rows
//   kind 2  name 0x0047E4F0  desc 0x004952D0   kernel sec 24, stride  8, 2 rows
//   kind 3  name 0x0047EA30  desc 0x0047EA90   ITEMS (the shop's own resolvers)
//
// Decoded from the shipped kernel.bin, those are exactly:
//   kind 0 = No Mercy / Ice Strike / Desperado / Blood Pain / Massive Anchor
//   kind 1 = Laser Eye ... Shockwave Pulsar   (Quistis's sixteen Blue Magic)
//   kind 2 = <character name> / Angel Wing    (Rinoa -- row 0's name comes from
//            the SAVEMAP, which is why 0x0047E4F0 special-cases index 0 and
//            returns 0x01CFDC88 rather than a kernel string)
//   kind 3 = ammunition, by item id
//
// ============================================================================
// KIND 0 -- SEIFER AND EDEA (v0.38.3, #99, verified statically)
// ============================================================================
//
// Aaron: *"Seifer is No Mercy and Edea is Ice Strike... hoping you can verify
// without me having to BAT those two."* Every link in that chain is readable
// from the shipped files, so it is:
//
// **1. The commands.** Kernel section 0 is 312 bytes = 39 records of 8, and the
// menu-kind byte at +5 reads 0x87 for exactly five of them:
//
//   17  Fire Cross   0x87     Seifer
//   18  Sorcery      0x87     Edea
//   20  Limit        0x87     Laguna
//   21  Limit        0x87     Kiros
//   22  Limit        0x87     Ward
//
// Five commands, five characters. (Earlier revisions of this comment wrote the
// set as "20-22", which named the right characters but the wrong ids -- Fire
// Cross and Sorcery are 17 and 18, and 19 in between is Rinoa's Combine.)
//
// **2. The dispatch.** 0x87 & 0x1F = 7, bit 0x20 clear, so 0x004BC7EA jumps to
// `[0x004BCA58 + 7*4]` = 0x004C8190, which is six pushes and
// `call 0x004C7D00` with the kind argument **0** (`push 0` at 0x004C81A9).
// 0x004C7D00 then takes the `test eax,eax / jne` branch at 0x004C7D2D and
// installs `[0x01D768D4] = 0x0047E6B0`, `[0x01D768D8] = 0x0047E6E0` -- the two
// resolvers this file already calls kind 0.
//
// **3. The names.** `0x0047E6B0(i)` is
// `ax = word[0x01CF82C8 + i*24]`, i.e. kernel section 18, stride 24. Section 18
// is 120 bytes = 5 records, and its text lives in section 48 at file offset
// 0x89C0. Decoding the five name offsets against that base gives, exactly:
//
//   i  offset  name              description
//   0  0x0000  No Mercy          "Damage all enemies"      <- SEIFER
//   1  0x0018  Ice Strike        "Damage 1 enemy"          <- EDEA
//   2  0x002F  Desperado         "Damage all enemies"
//   3  0x0048  Blood Pain        "Damage 1 enemy"
//   4  0x005F  Massive Anchor    "Damage all enemies"
//
// and searching kernel.bin for the encoded bytes of each name lands on
// 0x89C0/0x89D8/0x89EF/0x8A08/0x8A1F -- the offsets, independently.
//
// **4. Nothing in this file is kind-0-specific.** The list geometry, the row
// stride of 5, the remaining/selectable rule and the row composer are the same
// code that reads Quistis's Blue Magic and Rinoa's Combine, both BAT-confirmed.
// A one-row kind-0 list is a one-row instance of the list that already works,
// which is why this could be settled without a battle. tests/battle_limit_
// compile.cpp drives it with the real section-18 bytes.
// ============================================================================

// The four resolver pairs, as the engine installs them at 0x004C7D3F /
// 0x004C7DB4 / 0x004C7E29 / 0x004C7E8F. Testing against these is positive
// identification: the value is a known code address, not a small integer that
// something else could plausibly be holding.
static const uint32_t LIMIT_NAME_FN[4] = { 0x0047E6B0, 0x0047E650, 0x0047E4F0, 0x0047EA30 };
static const uint32_t LIMIT_DESC_FN[4] = { 0x0047E6E0, 0x0047E680, 0x004952D0, 0x0047EA90 };

static const int LIMIT_KIND_TEMP  = 0;   // Seifer / Edea / Laguna / Kiros / Ward
static const int LIMIT_KIND_BLUE  = 1;   // Quistis
static const int LIMIT_KIND_COMB  = 2;   // Rinoa
static const int LIMIT_KIND_AMMO  = 3;   // Irvine

// ============================================================================
// v0.36.1 (#94): THE INDEX IS A PARTY SLOT, NOT A CHARACTER INDEX.
// ============================================================================
//
// The 2026-08-19 BAT settled this in one line. Irvine's Shot opened and logged
//
//     [LIMIT] open kind=3 char=0 cmd=14 rows=1 cursor=0
//
// with Irvine in party slot 0 -- while the mod's own LIMIT-DIAG on the same
// turn printed `slot=0 charIdx=2`. So `0x01D768EB` (and `0x01D768D9` for the
// Slot) is the BATTLE PARTY SLOT, 0..2, and `0x004C7CD0` takes that slot.
// v0.36.0 compared it against a savemap character index and therefore refused
// every Selphie Slot window it saw: the test `slot == 5` can never pass.
//
// The proof is arithmetic, not inference. Selphie was in slot 2, and the
// pointer the engine stored was 0x01CFF3D2:
//
//     0x01CFF032 + 2 * 464 == 0x01CFF3D2      exactly.
//
// The formula was right; what was fed into it was not. **A field whose meaning
// you have assumed is not a field you have identified** -- and the value that
// exposed it was in the mod's own log the whole time, one line above.
static const int LIMIT_PARTY_SLOTS = 3;
static bool LimitPartySlotValid(int slot)
{ return slot >= 0 && slot < LIMIT_PARTY_SLOTS; }

// Character indices, savemap order.
static const int LIMIT_CHAR_SQUALL  = 0;
static const int LIMIT_CHAR_ZELL    = 1;
static const int LIMIT_CHAR_IRVINE  = 2;
static const int LIMIT_CHAR_QUISTIS = 3;
static const int LIMIT_CHAR_RINOA   = 4;
static const int LIMIT_CHAR_SELPHIE = 5;

// Which kind a resolver address means. Returns -1 for anything else, which is
// the answer that matters: if the dword is not one of these four, the limit
// list menu is NOT what is on screen and nothing should be read out of the
// scratch block.
static int LimitKindFromNameFn(uint32_t nameFn)
{
    for (int i = 0; i < 4; i++) if (LIMIT_NAME_FN[i] == nameFn) return i;
    return -1;
}

// The kind byte the engine stored at 0x01D768E6 and the resolver it installed
// are written by the same call. Requiring BOTH is the "two identifications that
// must agree" shape this project settled on after the module-pool work: either
// one alone can be a stale value from a previous menu.
static bool LimitKindAgrees(int kindByte, uint32_t nameFn)
{
    return kindByte >= 0 && kindByte < 4 && LIMIT_NAME_FN[kindByte] == nameFn;
}

// Rinoa's row 0 has no kernel name -- 0x0047E4F0 returns the savemap pointer
// 0x01CFDC88 for index 0. The engine draws the dog's name there.
static bool LimitRowUsesSavemapName(int kind, int row)
{
    return kind == LIMIT_KIND_COMB && row == 0;
}

// A row's remaining stock. 0x004FE2A5 reads entry+1, and for a 4-column menu
// (0x01D768F2 != 1 and != 3) subtracts what has already been queued this turn
// from 0x01D76904[row]. Every limit list is 4 columns wide, so the subtraction
// always applies to us -- but the rule is coded as the engine has it rather
// than assumed, because Draw and Item reach the same routine with 1 and 3.
static int LimitRowRemaining(int columns, int stock, int usedThisTurn)
{
    if (columns == 1 || columns == 3) return stock;
    return stock - usedThisTurn;
}

// 0x004FE2BB / 0x004FE2BD: a row is choosable when something is left AND the
// entry's flag byte (+4) does not have bit 1 set.
static bool LimitRowSelectable(int remaining, int flags)
{
    if (remaining <= 0) return false;
    if (flags & 0x02) return false;
    return true;
}

// ============================================================================
// SELPHIE'S SLOT -- a different UI entirely (0x004C7920)
// ============================================================================
//
// The Slot window is not a list of rows. It shows ONE rolled spell with the
// number of times it will cast, and a two-row menu underneath. Up/Down at
// 0x004C74E9 / 0x004C7513 walks 0x01D768D8 between 0 and 1 with wraparound --
// there is no third option and no timer.
//
// The two option labels are the game's own strings. **There are TWO call sites
// and they are one step apart** -- v0.36.1 used the wrong one for both:
//
//   0x004C7A92  the ROW DRAWER:   label = 0x0047EC70(id)         <- no offset
//   0x004C7538  the HELP LINE:    help  = 0x0047EC70(id + 2)     -> 0x01D76860
//
// where `id = listBase[(cursor + 1) * 5]`. Kernel section 30 puts the pair two
// apart on purpose: **66 "Cast" / 68 "Use indicated magic"**, **67 "Do over" /
// 69 "Turn the slot again"**. Reading `id + 2` as the label therefore announced
// each option's EXPLANATION in place of its name, and `/` then ran two further
// on into "AP received".
//
// The 2026-08-19 BAT said so plainly -- *"Slot. Sleep, 2 times. Turn the slot
// again"* -- and the confirm proved which row was which: the player picked
// cursor 1 and the popup carried `value=0x33`, Full-Cure, so **cursor 1 is Cast
// and cursor 0 is Do over**. The list holds 67 at entry 1 and 66 at entry 2.
//
// The v0.36.1 probe passed this because its fixture planted ids 64/65 and
// asserted 66/67 -- **numbers chosen to match the code rather than the game.**
// The fixture now uses the real section-30 indices and the entry order the BAT
// observed, so it is a statement about FF8 and not about this file.
// The battle command that opens the Slot window (kernel section 0 entry 16).
// 0x004C7963 stores it at 0x01D768E0, so it is a second field the creator
// writes -- and the scratch block needs one, because 0x01D768E0 is used by
// another menu entirely (eleven writers in 0x004AExxx).
static const int SLOT_COMMAND_ID = 16;

static const int SLOT_OPT_CAST    = 0;
static const int SLOT_OPT_DO_OVER = 1;
static const int SLOT_OPTION_COUNT = 2;

// The engine's own index arithmetic, kept in one place so the reader and the
// probe cannot drift apart.
static int SlotOptionEntryOffset(int cursor) { return (cursor + 1) * 5; }
static int SlotOptionLabelIndex(int entryId) { return entryId; }        // 0x004C7A92
static int SlotOptionHelpIndex(int entryId)  { return entryId + 2; }    // 0x004C7538

static bool SlotCursorValid(int cursor)
{
    return cursor >= 0 && cursor < SLOT_OPTION_COUNT;
}

// Selphie's window has nothing to say until the roll has produced a spell.
// 0x004C7920 zeroes the phase byte and the roll only fills 0x01D768DC/DD in
// phase 0 (0x004C7491), so a frame read before that carries id 0 -- the same
// pre-write frame that made the refine quantity screen say "0" in v0.33.1.
static bool SlotSpellReady(int magicId, int times)
{
    return magicId > 0 && times > 0;
}

// The Slot phase byte at 0x01D768DB indexes a jump table of ELEVEN entries
// (0x004C7454: `cmp eax, 0x0A / ja`). Anything else means we are not looking
// at a live Slot window.
static bool SlotPhaseValid(int phase) { return phase >= 0 && phase <= 0x0A; }

// "1 time", not "1 times". The BAT read *"Wall, 1 times"* and *"Thundaga, 1
// times"*; a screen reader gives you no punctuation to soften that.
static const char* LimitTimesWord(int n) { return (n == 1) ? "time" : "times"; }

// ============================================================================
// THE LIMIT COMMAND'S OWN NAME (v0.38.3, #99)
// ============================================================================
//
// When the limit toggle at 0x01D7684A is set, the game does not draw "Attack"
// on row 0 -- it draws the character's limit command, scrolling. The drawer is
// at 0x004BCE80:
//
//   mov edx, [0x01D76834]        ; the four ordinary commands
//   mov al,  [0x01D7684A]        ; the limit toggle
//   test al, al        / je  normal
//   test [edi+3], 4    / je  normal
//   ...
//   mov ebx, [0x01D76838]
//   mov al,  [ebx]               ; <- the LIMIT command id
//   call 0x0047EBD0              ; <- the game's own name for it
//
// and 0x004BB77E sets `[0x01D76838] = slot*464 + 0x01CFF02E`. So the limit
// command id is ONE BYTE at 0x01CFF02E + slot*464, and 0x0047EBD0 names it.
//
// That is what the player sees, so it is what the mod should say: "Renzokuken"
// for Squall, "Duel" for Zell, "Fire Cross" for Seifer, "Sorcery" for Edea --
// not the generic "Limit Break" the turn line used from v0.10.22 to v0.38.2.
// **And it makes Seifer and Edea BAT-able through Squall**: the byte, the
// resolver and the announcement are the same three steps for all of them.
static const uint32_t LIMIT_CMD_ORIGIN = 0x01CFF02E;   // + slot * 464
static const uint8_t  LIMIT_TOGGLE_ON  = 64;           // 0x01D7684A

// The ids kernel section 0 marks 0x87 -- the five characters whose limit opens
// a kind-0 list. Nothing dispatches on this; it exists so the probe can state
// which commands the claim covers.
static const int LIMIT_KIND0_COMMANDS[5] = { 17, 18, 20, 21, 22 };

// Section 18 row ids, for the same reason.
static const int LIMIT_KIND0_SEIFER = 0;   // No Mercy
static const int LIMIT_KIND0_EDEA   = 1;   // Ice Strike
