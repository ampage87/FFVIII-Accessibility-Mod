// live_join_order_model.inl -- WHEN THREE PEOPLE SHARE ONE MODEL, THE ORDER
//                              STILL TELLS THEM APART
//
// v0.132.3 (#shumi). Aaron's v0.132.2 run finally listed the Shumi standing in
// the workshop -- and called him "NPC". The Elder's house, both halves of the
// workshop and the Artisan's house all show the same thing: real, named
// characters announced as "NPC".
//
// THE JOIN IS RIGHT TO REFUSE AND WRONG TO STOP THERE. BuildLiveJsmMap matches a
// live entity to its script entity by the model id at +0x218, because the live
// "others" array is COMPACTED -- it holds only the entities the scene actually
// instantiated, so its index is not the script slot. (That is not a theory: it
// is why Aaron once found "Quistis" in the catalog and got a different NPC.)
// When several script entities share one model the match is ambiguous, and the
// join resolves it by the nearest static SET3 position, or accepts a first
// candidate only when every candidate is the same name bar a trailing digit.
//
// tmkobo2 defeats both. `Shou`, `Otuki` and `Tukurite` all run SETMODEL 6; they
// are not name-twins; and the Shumi at the statue has walked 407 units from his
// SET3, far past the 64-unit tiebreak. So all three come out UNRESOLVED, and an
// entity with no name is announced by its type.
//
// **THE ENGINE BUILDS THE ARRAY BY WALKING THE SCRIPT'S ENTITY LIST, SO ORDER
// SURVIVES THE COMPACTION.** Compaction removes whole entities; it cannot
// reorder the ones that remain. So among the entities sharing one model, the
// k-th live is the k-th in slot order.
//
// THIS IS TESTED, NOT ASSUMED. tmsand1 also has three model-6 Shumi --
// `Shou2`, `Shou21`, `Shou22` -- and there the position tiebreak DOES resolve
// them, because those three never move. The answer it gives is
//
//     ent5 -> Shou2    ent7 -> Shou21    ent8 -> Shou22
//
// which is exactly what order predicts, on a field where independent evidence
// exists. And in tmkobo2 the live block of the first unresolved model-6 entity
// sits at (155,1118) -- the exact coordinates of `SET3 (155,1118,0) tri 60` in
// **Shou's** own method 1, which is the entity order puts there. Two fields, two
// independent confirmations, no counter-example.
//
// THE COUNT GUARD IS WHAT KEEPS IT HONEST. Order only means anything if we are
// looking at the same set twice. If the field has three unresolved script
// entities on model 6 and three unresolved live ones, pairing them in order is
// sound. If the numbers differ -- one did not spawn, or one already matched by
// position, or the model id changed at run time because the script ran SETMODEL
// twice (tmkobo2's `Munba` runs 7 then 8, and the scanner keeps the first) --
// then the two lists are not the same set and the pass declines, exactly as
// today. It only ever names an entity the join had already given up on, so it
// cannot rename anything the existing passes resolved.

// Should the order pass pair these two lists?
//
// nUnresolvedLive: live entities carrying this model that nothing has claimed.
// nUnresolvedJsm:  script entities declaring this model that nothing has claimed.
//
// Equal and non-zero is the only case where the k-th of one is the k-th of the
// other. A single unresolved pair is included deliberately: it is the same
// argument with one element, and it is what names a lone character whose model
// is shared with someone who did not spawn.
static bool LiveJoinOrderApplies(int nUnresolvedLive, int nUnresolvedJsm)
{
    if (nUnresolvedLive <= 0) return false;
    return nUnresolvedLive == nUnresolvedJsm;
}
