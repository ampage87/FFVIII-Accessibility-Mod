# FF8 Accessibility Mod — Changelog

Newest on top. Each entry begins with a `## vMAJOR.MINOR.BUILD` heading followed by a blank line and the commit message body. The push utility (`Utilities/push_to_github.ps1`) reads the top heading to determine the version being pushed and uses everything between that heading and the next `## v` heading as the commit message body.

The version in the top heading **must** match `FF8OPC_VERSION` in `src/ff8_accessibility.h`. The push utility refuses to push if they don't.

## v0.14.108

Filter party members from the field entity catalog (take 2)

When the player has 2+ active party members, the followers (Zell, Quistis,
etc.) appear in the field navigation catalog because they have throughonoff
set (you walk through them so they don't block) but no talkonoff/pushonoff.
The existing classification chain falls through to ENT_EXIT, which is
wrong — they're not exits, they're invisible-to-interaction party
members. Pressing X on them does nothing. They're noise that pads out
the F9 cycle list.

v0.14.107 attempted a savemap-aware filter that cross-referenced the
entity's model ID against the active party formation at savemap+0xAF0
(0x01CFE74C). That assumed canonical model→charId mapping (0=Squall,
1=Zell, 2=Irvine, etc.). BAT on bggate_1 with a Squall + Zell + Selphie
party (formation [1,0,5,255]) revealed the assumption is wrong: the
followers showed up as model 2 and model 4 — canonical Irvine and Rinoa
IDs, neither in this party. The savemap cross-reference correctly
returned false, the filter no-op'd, and the followers stayed in the
catalog. The engine reuses model slots per-field; the canonical mapping
is an averaged truth that doesn't hold per-field.

v0.14.108 replaces the model→charId filter with a behavioral fingerprint
that catches followers regardless of which model slot the field assigned
them:

  modelId in [0, 9]   visible character (party-character model range)
  throughonoff  > 0   player walks through them
  talkonoff    == 0   not talkable
  pushonoff    == 0   no collision

The modelId < 10 guard excludes save points (model 24) and other non-
character interactive objects with throughonoff. Save point detection
runs later in RefreshCatalog() via the modelId == 24 check, so save
points qualify and reach the JSM-based reclassification regardless.

This filter also catches non-interactive cutscene characters that walk
through scenes — actually correct behavior since they're not navigation
targets either.

Known trade-offs:
- A real NPC whose script sets throughonoff before TALKRADIUS sets
  talkonoff would be transiently filtered. The catalog refreshes on
  every F9 press, so the next press picks them up. Acceptable race.
- Followers using model 10+ (generic NPC range) wouldn't be caught.
  Empirically followers seem to land in 0–9, but if a future BAT shows
  otherwise this can be relaxed.

The v0.14.107 helpers (IsCharacterInActiveParty, ModelIdToCharId) and
the per-field [party-state] formation diagnostic stay in place. They're
harmless, well-documented, and may be useful for future party-aware
features. Only the filter call site changes.

Files: src/field_nav_catalog.inl (replace v0.14.107 filter block with
the behavioral fingerprint, ~12 lines), src/ff8_accessibility.h
(version bump). v0.14.107 helpers in field_nav_helpers.inl and the
[party-state] diagnostic UNCHANGED.

## v0.14.107

Filter party members from the field entity catalog

When the player has 2+ active party members, the followers (Zell, Quistis,
etc.) appear in the field navigation catalog as if they were interactable
NPCs because they have talkonoff set. Pressing X on them does nothing —
they're noise that pads out the F9 cycle list. v0.14.107 filters them out
using a savemap-aware check.

Approach: in RefreshCatalog(), for each non-player entity, if its model
ID maps to a character ID currently in the active party formation, skip
it. The model→charId map is straightforward (model 0=Squall, 1=Zell,
2=Irvine, 3=Quistis, 4=Rinoa, 5=Selphie, 6=Seifer, 7=Edea, with model 8
Quistis-uniform also mapping to charId 3); the formation array lives at
savemap+0xAF0 (absolute 0x01CFE74C), four bytes, each a charId 0–7 or
0xFF for empty. Same address used by Junction TTS and save block content
TTS — confirmed reliable across many BATs.

Why cross-reference savemap formation rather than filtering on model ID
alone: the engine reuses model slots across scenes. A model 7 in early-
game might be a generic background character, not Edea. The April 2026
entity-classification thread confirmed this empirically. Only filter
when the model corresponds to a character actually in the active party
right now.

Edge cases handled by design:
- Solo Squall (Fire Cavern, formation [0xFF, 0x00, 0xFF, 0xFF]) — Squall
  is the player and excluded by the `i != s_playerEntityIdx` guard, so
  nothing filters.
- Pre-recruitment cutscene NPCs sharing a party-character model — the
  formation byte for that charId is still 0xFF, IsCharacterInActiveParty
  returns false, filter no-ops, NPC stays in catalog.
- Quistis as classroom instructor (model 8) — only filtered when she's
  in the active party; otherwise kept.

Adds two helpers in field_nav_helpers.inl: IsCharacterInActiveParty
(charId) reads the 4-byte formation array under SEH; ModelIdToCharId
(modelId) maps 0–7 directly and 8→3 with the Quistis-uniform special
case. New per-field [party-state] diagnostic logs the formation array
contents once per field load; new [party-filter] line per filtered
entity for verification.

Files: src/field_nav_helpers.inl (two new helpers near top, ~50 lines
including documentation), src/field_nav_catalog.inl (filter check before
classification + one-shot per-field diagnostic, ~50 lines), src/field_
navigation.cpp (1 line: new s_partyDiagDumped flag declaration), src/
field_nav_fieldscripts.inl (1 line: reset s_partyDiagDumped = false on
field load), src/ff8_accessibility.h (version bump).

## v0.14.106

v0.14.105 + v0.14.106: Fix speech rate persistence + INI template

v0.14.105: Speech rate (and other accessibility settings) were creeping up
by 1 each session because Alt+F4 close was firing the F4 IncreaseRate()
handler on the way out. Every F-key accessibility handler (F1-F8, F11) is
now gated on !alt (GetAsyncKeyState(VK_MENU)) so Alt+combos no-op. Also
added a one-shot LogActualSAPIState diagnostic to confirm the fix; BAT
verified persistence is sound and SAPI preserves rate/volume across voice
changes.

v0.14.106: Strip the diagnostic harness now that the fix is verified. Add
a human-readable commented template to ff8_accessibility.ini explaining
each setting, its range, and the in-game shortcut. Existing INIs auto-
upgrade on next launch (preserving all values). New helpers in config.cpp:
HasTemplateMarker(), EnsureTemplate().
