// battle_tts_validate.inl — TTS announcement breadcrumb logging
//
// Included from battle_tts.cpp. Do not compile independently.
//
// ============================================================================
// v0.13.81 — Minimal breadcrumb logger (replaces v0.13.80 correlation machinery)
// ============================================================================
//
// v0.13.80 architecture retrospective
// -----------------------------------
// v0.13.80 tried to correlate each announcement with an entry in the popup
// record table at 0x01D280C4 and fire a validation screenshot on the popup
// sprite's lifetime band transitions. BAT5 definitively proved the popup
// table does NOT hold floating damage number sprites — it holds action-
// announce popups (Target labels, enemy-attack incoming, spell-cast labels).
// The correlation was syntactically working but semantically wrong: damage
// announcements were being paired with unrelated later action-announce
// popups on the same slot. sprite_value=2 for a damage=114 announcement
// made no sense because the popup table never contained the damage number.
//
// v0.13.81 philosophy
// -------------------
// Drop the pretense of in-memory value validation. The engine renders
// floating numbers through a graphics path we cannot read from memory.
// The ONLY validation possible is visual: "did a screenshot taken at a
// moment when a sprite was visible show what the TTS claimed?"
//
// Screenshots are now fired by independent event-driven triggers:
//
//   * Popup table NEW / KIND events — already handled by PollPopupRecords
//     in battle_tts_screenshot.inl. Covers action-announces, spell-cast
//     labels, Miss text overwrites, and status popups (once catalogued).
//     Caps raised to 60/60 per battle.
//
//   * Anim flag 0x01D280C0 transition 0→1 — fires a direct screenshot
//     from HP-TRACK's PollHPChanges when the damage-number animation
//     begins. This is the first frame the damage/heal number is visible.
//     BAT5 confirmed flag lifetimes match real animation durations
//     (281ms for physical, 1266ms for Quistis taking damage).
//
// Validate_AnnounceEvent simply emits a [VALIDATE] log line at the
// moment of TTS announcement. Audit pairs these breadcrumbs with the
// independent screenshot events by timestamp proximity.
//
// No state machine. No slot-based correlation. No "orphan" classification
// claiming to know which events should have produced sprites. Each
// subsystem does its narrow job and the audit is the sum of what was
// written to logs + what was captured to PNGs.
//
// Thread safety
// -------------
// Validate_AnnounceEvent is called from multiple threads (mod thread
// from HP-TRACK; game thread from HookedSpellResultDispatch /
// HookedPopupSprite hook bodies; also from battle_status.inl on the
// mod thread). Log::Battle is already proven safe from all these
// contexts per existing usage patterns. Nothing else is touched.

static void Validate_AnnounceEvent(const char* kind,
                                    int slot,
                                    int claimedValue,
                                    const char* claimedText,
                                    const char* trigger)
{
    if (!kind)        kind = "unknown";
    if (!claimedText) claimedText = "";
    if (!trigger)     trigger = "immediate";

    // Single structured log line per announcement. That's the full
    // extent of this function now. Screenshot coverage comes from the
    // popup poll (screenshot.inl) and anim flag trigger (hp.inl), each
    // of which fires independently without any coordination with us.
    Log::Battle("BattleTTS: [VALIDATE] kind=%s slot=%d value=%d trigger=%s tts=\"%s\"",
                kind, slot, claimedValue, trigger, claimedText);
}

// Per-battle reset. Kept for symmetry with other *_Reset() calls in
// OnBattleEnter, even though this module now holds no state.
static void Validate_Reset()
{
    // Intentionally empty. v0.13.81 has no per-battle state.
}
