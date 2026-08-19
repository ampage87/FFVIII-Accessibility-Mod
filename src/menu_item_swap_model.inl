// menu_item_swap_model.inl -- v0.29.0 (#88)
//
// Pure decision: did an item arrangement actually happen? PART OF menu_tts.cpp
// (textual include) and of tests/menu_sim.cpp. No Win32, no SEH, no absolute
// addresses -- the memory reads live in menu_tts_item.inl, this is only the
// judgement they feed.
//
// ---------------------------------------------------------------------------
// WHY THIS IS ITS OWN FILE
//
// Both item-arrangement flows -- the battle-item order (focus 36) and the
// inventory rearrange (focus 99) -- leave their destination screen for the SAME
// state whether the player confirmed the swap or backed out of it. The mod
// watched that transition and announced "Swapped" either way, so **cancelling an
// arrange was reported as a completed swap**. A sighted player can see the list
// did not move. A blind player is told the wrong thing about his own inventory
// and has nothing to check it against.
//
// So do not watch the transition. Remember the item id sitting at the SOURCE
// slot when the swap was armed, and compare it afterwards: the slot is evidence,
// the state change is not.
// ---------------------------------------------------------------------------

// 0xFFFF means "not armed" / "could not read the slot".
static const uint16_t ITEM_SWAP_NO_ID = 0xFFFF;

// Two slots holding the SAME id look identical after a swap and after a cancel,
// so this returns false for them. That is deliberate: swapping two identical
// items is a no-op, so "Cancelled" is true of the inventory either way, while
// "Swapped" would be a claim about a change that did not happen. When an
// ambiguity cannot be resolved, fail towards the statement that stays true.
static inline bool ItemSwapDecide(uint16_t armedId, uint16_t nowId)
{
    return armedId != ITEM_SWAP_NO_ID &&
           nowId   != ITEM_SWAP_NO_ID &&
           nowId   != armedId;
}
