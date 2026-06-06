// battle_tts_menu.inl — Turn announce, command menu, magic/GF/item/draw sub-menus.
// Included from battle_tts.cpp inside `namespace BattleTTS`. Do not compile
// independently.
//
// v0.16.5: Carved into a slim shell + four sub-`.inl` files (state, lists,
// helpers, poll). Behavior byte-for-byte identical to v0.16.4 — pure
// mechanical split. See DEVNOTES.md for the rationale and refactor recipe.
//
// Include order matters:
//   state    — all statics, constants, name tables, structs (FIRST).
//   lists    — BuildMagicList / BuildGFList / BuildItemList / BuildDrawList
//              / SnapshotAllMagicInventories / DiffMagicInventories.
//              Reads state.
//   helpers  — EnterSubmenu / GetBattleCharName / BuildCharCommandList.
//              Reads state, calls list builders.
//   poll     — PollTurnAndCommands + PollDeferredTurnAnnounce (LAST).
//              Reads everything above.


#include "battle_tts_menu_state.inl"
#include "battle_tts_menu_lists.inl"
#include "battle_tts_menu_helpers.inl"
#include "battle_tts_menu_poll.inl"
