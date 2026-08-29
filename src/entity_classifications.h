// entity_classifications.h - Auto-generated entity classification tables
// Generated from field_entity_survey.json by generate_classifications.py
// DO NOT EDIT MANUALLY - regenerate from survey data instead.
//
// Coverage: 900 fields, 13590 total entities, 2985 unique SYM names

#pragma once

// ============================================================================
// Controller/effect entities to hide from the navigation catalog.
// These are invisible script controllers, lighting, camera, effects, etc.
// 213 entries.
// ============================================================================
static const char* ENTITY_SKIP_NAMES[] = {
    "a_light1",
    "a_light2",
    "alight",
    "arrowlight",
    "backlight",
    "battleyarou",
    "bedlight",
    "BGanimekun",
    "biglight",
    "black",
    "blacklight",
    "blindlight",
    "bluelaser",
    "bluelight",
    "bokeie",
    "buttonlight",
    "c_light",
    "camera",
    "camera1",
    "camera2",
    "candlelight",
    "clocklight",
    "contlight",
    "cork",
    "cornerlight",
    "counterlight",
    "curtain",
    "cut",
    "d_light",
    "desklight",
    "desklight1",
    "desklight2",
    "Director",
    "director0",
    "director1",
    "director2",
    "director3",
    "disp2light",
    "disp3light",
    "displight",
    "door01",
    "doorcont",
    "doorlight",
    "downlight",
    "e_light",
    "elefog",
    "elelight",
    "emergencylight",
    "enzetu",
    "eventline0",
    "eventline1",
    "floorlight",
    "floorlightcont",
    "fog",
    "fog1",
    "fog2",
    "footlight",
    "gardenlight",
    "gatelight",
    "glass",
    "greenlight",
    "h_light",
    "hamon1",
    "hamon2",
    "hamon3",
    "hansha",
    "hantei",
    "harplight1",
    "harplight2",
    "headlight",
    "heallight",
    "Henka",
    "hotellight",
    "infolight",
    "Jokantoku",
    "JumpTimer",
    "kabelight",
    "kage",
    // v0.20.15: Caraway's Mansion glass-puzzle CONTROLLER -- REQ-dispatches to a
    // dozen entities (like director1) and was surfacing as a phantom 'Object'
    // co-located with the 'cup' Glass. 'kakusi' = "hidden": never player-facing.
    "kakusi",
    "kanbanlight",
    "KANTOKU",
    "kantoku",
    "Kantoku",
    "kantoku_suketto",
    "Keyjokantoku",
    "Keykantoku",
    "l_doorlight",
    "laser",
    "leftred",
    "lens",
    "light",
    "light0",
    "light1",
    "light10",
    "light11",
    "light12",
    "light13",
    "light2",
    "light23",
    "light3",
    "light4",
    "light5",
    "light6",
    "light7",
    "light8",
    "light9",
    "light_d",
    "light_k",
    "light_r",
    "light_s",
    "lightfog",
    "lightning",
    "llight",
    "lowerlight",
    "map1scroll",
    "map2scroll",
    "map3scroll",
    "Mawaruu",
    "Mawaruu2",
    "movelight",
    "mugi",
    "Musickantoku",
    "Musickantoku2",
    "neonlight",
    "noise",
    "orangelight",
    "orangelight1",
    "orangelight2",
    "orangelight3",
    "Partikuru",
    "petlight",
    "pillarlight",
    "plane",
    "plane1",
    "platelight",
    "publight",
    "r_doorlight",
    "raillight",
    "rain",
    "redlaser",
    "redlight",
    "redlight1",
    "redlight2",
    "redlight3",
    "redlight4",
    "redlight5",
    "redspell1",
    "redspell2",
    "redspell3",
    "redspell4",
    "redspell5",
    "rightred",
    "rlight",
    "roomlight",
    "s_light",
    "saveline0",
    "screen",
    "Scroll",
    "scrollcont",
    "Scrollkun",
    "ScrollLine",
    "seiferout",
    "Seigyo",
    "seigyo",
    "Shake",
    "shoplight",
    "sidelight",
    "smalllight",
    "sounddir0",
    "spellcont",
    "stairlight",
    "stairslight",
    "standlight",
    "steam",
    "steplight",
    "steplight1",
    "steplight2",
    "stl0",
    "stl1",
    "stoplight",
    "stoplight1",
    "stoplight2",
    "stopperlight",
    "Timer",
    "timer0",
    "timerdir0",
    "TimerJump",
    "timerkun",
    "toplight",
    "Trainscroll",
    "trap",
    "tunnellight",
    "tvlens",
    "tvlight",
    "uplight",
    "upperlight",
    "upperlight1",
    "upperlight2",
    "upperlight3",
    "upperlight4",
    "Urakata",
    "view",
    "walllight",
    "water",
    "waterlight",
    "whitelight",
    "wind",
    "witchin",
    "witchout",
    "woodlight",
    "wpnlight",
    "yellowlaser",
    "yellowlight",
    "zo",
    nullptr
};

// ============================================================================
// FIELD-SCOPED OVERRIDES (v0.65.0)
// ============================================================================
//
// ENTITY_DISPLAY_NAMES below is keyed on the SYM alone, which is right when a
// symbol means one thing everywhere. Some do not. `handle` is the Missile Base
// valve wheel the player actually turns (bgmd1_4, gfcar1) AND the escape pod's
// release lever, which no player ever touches -- squall's own script REQs it,
// as `[REQ-TARGET] ent9 'squall' m=12 opcParam=7 -> target ent16 'handle'` in
// Aaron's 2026-08-23 log records. One table cannot say both things.
//
// So this one is keyed on FIELD AND SYM, and it wins over everything: a NULL
// display drops the thing from the catalog entirely; a non-NULL display names
// it and, on an exit-bearing line, replaces the "Exit to <destination>" label
// the destination would otherwise earn.
//
// The bar for adding a row is a specific observation, named in the comment. It
// is deliberately not a rule: "an entity nothing can interact with" would also
// describe things Aaron navigates to today, and there is no way to check that
// across all 882 fields from here.
struct FieldScopedEntity {
    const char* field;    // internal field name, e.g. "sspod2"
    const char* sym;      // the entity or line's SYM
    const char* display;  // nullptr = drop it from the catalog entirely
};

static const FieldScopedEntity FIELD_SCOPED_ENTITIES[] = {
    // Aaron, 2026-08-23, in the escape pod: "there is an empty capsule Squall
    // has to enter and it is being identified as an exit to Desert. It should
    // just read out as 'Capsule'."
    //
    // `pod` is line 0 of sspod2, a SCREEN_BOUND whose destination the scanner
    // resolves to field 638 (Desert 1) -- correct, and useless: he is not
    // walking to a desert, he is climbing into the capsule that eventually
    // lands in one. It stays an exit, because stepping onto it is still how the
    // scene proceeds; only the words change.
    { "sspod2", "pod", "Capsule" },

    // "There is another item in the catalog for 'handle' that is only
    // interacted with by the scene automatically so it can be excluded."
    //
    // Confirmed in the script: sspod2's handle has no TALKRADIUS, no dialogue
    // and no MAPJUMP -- its talk and push methods are animation opcodes only --
    // and the 20:23:41 scan agrees: talk=0 push=0 jsmTalk=0 rtRad=0 talkable=0.
    // sspod3's handle is the same shape. The Missile Base ones are NOT listed
    // here and keep their name, which is the whole reason this table is keyed
    // on the field.
    { "sspod2", "handle", nullptr },
    { "sspod3", "handle", nullptr },

    // v0.112.0 (#dsrc): ddtower3 -- Level 3 -- has TWO terminals, and Aaron
    // could not tell them apart: "there is an additional control panel to open
    // the steam room. However, I couldn't seem to find it." Both were named
    // "Terminal" by the SYM table below, so the catalog offered "Terminal 1 of
    // 2" and "Terminal 2 of 2" with nothing to choose between them.
    //
    // The field's own text says the Steam Room costs exactly four:
    //   msg 0  "The Steam Room and the left door are linked. 4 RSP will be
    //           expended to enter the Steam Room."
    // and of the two terminals only `Tanme2` speaks the four-unit family --
    // its AASK ids are 2, 3 and 5, all "Expending 4" variants -- while `Tanme`
    // carries the full twelve-value pressure ladder (ids 6-24) the level door
    // needs. `Tanme2` is the shorter script by 400 dwords, which is the same
    // story: one fixed cost against a whole ladder.
    //
    // That is an inference from the message sets, not a decode of the door it
    // opens, so it is written down here to be confirmed or corrected by the
    // next run rather than left implicit. If they turn out to be the other way
    // round, swap these two lines and nothing else changes.
    { "ddtower3", "Tanme2", "Steam Room Terminal" },
    { "ddtower3", "Tanme",  "Level Terminal" },

    // v0.115.0 (#centra): THE CENTRA RUINS LADDERS. Aaron: "look through the
    // Centra Ruins fields to ensure all the entities needed to get around are
    // properly reflected in the catalog." Every ladder, both eye statues, the
    // power switch and the tower console are Line entities whose only dispatch
    // is PREQEW (opcode 0x019), which the classifier does not recognise -- so
    // all fourteen fell to the CAMERA_PAN silent default and the catalog hid
    // them. See line_camera_pan_surface_model.inl for the decode and for why
    // the naming table, not the classifier, is the thing that surfaces them.
    //
    // The direction on each ladder is not a guess. Every one of these lines
    // PREQEWs a named party-member script, and Square's names are unambiguous:
    // crtower1's leftlad0 -> `squall::p0_ladup0` (MAPJUMP3 to field 284,
    // crtower2 -- up a floor); crtower2's leftlad0 -> `squall::leftdw0`
    // (MAPJUMP3 to 283, crtower1 -- down a floor); leftlad1 -> `leftup1`;
    // leftlad2 -> `leftdw1`; rightlad0 -> `rightdwpre0`; crtower3's ladup0 and
    // laddw0 -> `squall::ladup0` / `squall::laddw0`. crroof1's lad0 runs
    // `squall::lad0`, the one script in the set that contains a literal
    // LADDERDOWN opcode; lad1 is its counterpart back up.
    //
    // `zou` is 像 -- statue. It and crtower3's `sw0` both run `squall::eye0`,
    // which is the same interaction on both statues: the one on the roof shows
    // the code, the one by the door asks for it. Naming them both "Eye Statue"
    // is the truth and is what a player looking for "the statue with the eyes"
    // will scan the catalog for.
    // v0.116.0 (#centra): THESE TWO WERE THE WRONG WAY ROUND, and Aaron caught
    // it: "the ladder up to the tip-top was announced as a ladder down when it
    // should have been a ladder up. Once I got to the top and needed to climb
    // back down, the ladder down was then announced as a ladder up."
    //
    // v0.115.0 named lad0 "Down" because `squall::lad0` is the one script in
    // the whole set carrying a literal LADDERDOWN opcode. That opcode names the
    // CLIMBING ANIMATION, not the direction of travel, and two independent
    // pieces of evidence say so. The scripts' own destinations: lad0 ends at
    // z=19496 and lad1 at z=18403, a thousand units lower. And the 2026-08-27
    // log, which is decisive -- the player arrives on crroof1 at tri 38 in the
    // lower camera zone and the ONLY line reachable there is lad0:
    //
    //   [refresh] zone-filter active: player tri=38, 65/80 triangles reachable
    //   [refresh]   cat1 TRIGGER line0 center=(916,-896) name='Ladder Down'
    //   [refresh] 'Ladder Up' filtered: another camera zone (unreachable)
    //
    // and after he climbs he is at tri 14 in a 9-triangle zone with lad1 and
    // the statue. lad0 is the way UP; lad1 is the way back down. The same
    // reversal is what made him report that the way down "wasn't in the
    // catalog" on the statue field -- it was, under the opposite name.
    //
    // A LADDER opcode is not a direction. Where a name and a destination
    // disagree, take the destination.
    { "crroof1",  "lad0",      "Ladder Up" },
    { "crroof1",  "lad1",      "Ladder Down" },
    { "crroof1",  "zou",       "Eye Statue" },
    { "crtower1", "leftlad0",  "Left Ladder Up" },
    { "crtower1", "rightlad0", "Right Ladder Up" },
    { "crtower1", "console0",  "Control Panel" },
    { "crtower2", "leftlad0",  "Left Ladder Down" },
    { "crtower2", "leftlad1",  "Left Ladder Up" },
    { "crtower2", "leftlad2",  "Left Ladder Down" },
    { "crtower2", "rightlad0", "Right Ladder Down" },
    { "crtower3", "ladup0",    "Ladder Up" },
    { "crtower3", "laddw0",    "Ladder Down" },
    { "crtower3", "sw0",       "Eye Statue" },
    { "crpower1", "sw0",       "Power Switch" },

    { nullptr, nullptr, nullptr }
};

// Returns the row for this field+sym, or nullptr. Case-insensitive on both,
// because SYMs and field names both arrive from the game in mixed case.
static const FieldScopedEntity* FieldScopedFor(const char* field, const char* sym)
{
    if (!field || !field[0] || !sym || !sym[0]) return nullptr;
    for (const FieldScopedEntity* r = FIELD_SCOPED_ENTITIES; r->field; r++) {
        if (_stricmp(field, r->field) == 0 && _stricmp(sym, r->sym) == 0) return r;
    }
    return nullptr;
}

// ============================================================================
// SYM name -> friendly display name for TTS.
// 148 entries.
// ============================================================================
struct EntityDisplayName {
    const char* sym;
    const char* display;
};

static const EntityDisplayName ENTITY_DISPLAY_NAMES[] = {
    { "adplate", "Sign" },
    { "Agittrain", "Train" },
    { "Anaun", "Announcer" },
    { "betunikun", "Student" },
    { "BGMonorail", "Monorail" },
    { "book", "Book" },
    // v0.101.0 (#derived-pos): sdcore1's Bahamut trigger line. The catalog
    // entry the player walks to; its coordinate comes from NAV_DERIVED_POS.
    { "BossBattle", "Blue Light" },
    { "Boy1", "Boy" },
    { "Boy2", "Boy" },
    { "cameraman", "Cameraman" },
    { "cardgamemaster", "Card Player" },
    { "cardgamemaster2", "Card Player" },
    { "Cardtanto", "Card Player" },
    { "Cat1", "Cat" },
    { "Cat2", "Cat" },
    { "celone", "Ellone" },
    { "cid", "Headmaster Cid" },
    // v0.18.3.267: Caraway's Mansion wine-glass puzzle (glfurin1/glfurin3).
    // 'cup' is the wine glass Quistis takes from the shelf and places in the
    // statue's hands to open the secret passage.
    { "cup", "Glass" },
    { "Daitouryo", "President" },
    { "dic", "Directory" },
    { "igyous1", "Directory" },  // v0.12.16: paired dialog entity for dic
    { "door", "Door" },
    { "door1", "Door" },
    { "door2", "Door" },
    { "dp01", "Draw Point" },
    { "dragon", "Dragon" },
    { "DrawPoint", "Draw Point" },
    { "DrawPointSampleCode", "Draw Point" },
    { "drpoint", "Draw Point" },
    { "Edea", "Edea" },
    { "edea", "Edea" },
    { "edea0", "Edea" },
    { "edea1", "Edea" },
    { "edea2", "Edea" },
    { "edea3", "Edea" },
    { "edea4", "Edea" },
    { "elone", "Ellone" },
    // v0.62.3 (#123): Piet is the Lunar Base technician you speak to on sscont1,
    // sscont2, sspod2 and eapod1 -- four fields, one person, announced as "NPC"
    // in the escape pod where he is one of three people in the room. `handle` is
    // the pod's release lever (sspod2/sspod3) and the Missile Base valve wheel
    // (bgmd1_4/gfcar1): it carries a model, so the catalog types it NPC, and a
    // lever announced as a person is a person the player goes looking for.
    { "piet", "Piet" },
    { "handle", "Handle" },
    { "evl1", "Elevator" },
    { "Fish", "Fish" },
    { "Fish2", "Fish" },
    { "G_Army", "Galbadian Soldier" },
    { "G_Army01", "Galbadian Soldier" },
    { "G_Army02", "Galbadian Soldier" },
    { "GalbadiaArmy01", "Galbadian Soldier" },
    { "GalbadiaSS", "Galbadian Soldier" },
    { "GalHei3", "Galbadian Soldier" },
    { "GalHei4", "Galbadian Soldier" },
    { "GalQuistis", "Quistis" },
    { "GalRinoa", "Rinoa" },
    { "GalZell", "Zell" },
    { "gate0", "Gate" },
    { "gate1", "Gate" },
    { "Girl", "Girl" },
    { "Girl2", "Girl" },
    { "hasigomodel", "Ladder" },
    { "hoteldoor", "Door" },
    { "info", "Information" },
    { "Irvine", "Irvine" },
    { "irvine", "Irvine" },
    { "kadowaki", "Dr. Kadowaki" },
    { "Kani", "Crab" },
    { "kero", "Frog" },
    { "Kiros", "Kiros" },
    { "kiros", "Kiros" },
    { "knob", "Knob" },
    { "ladder", "Ladder" },
    // v0.131.6 (#centra): Aaron, on Centra Ruins 7 -- "you have to walk onto an
    // automated lift, but the lift was identified as an NPC". It was announced
    // as "NPC" because nothing named it. `stone` is catalogued as an
    // interactive entity on exactly two fields disc-wide -- crsphi1 and
    // crtower1, the lift's two stops, both classified Map Exit by the scanner --
    // and is a Background everywhere else it appears (crtower3, eccway12,
    // gnroad1, tvglen2), which the catalog never surfaces. So this row names the
    // lift and nothing else, which is the bar this project sets before trusting
    // a SYM: checked against all 900 fields, not inferred from the word.
    { "stone", "Lift" },
    // v0.18.3.290 (#85): the v0.18.3.288 'ladline0'-'ladline7' -> "Ladder"
    // mappings were REMOVED here. They were added purely because the SYM name
    // reads like "ladder", which directly violates this project's standing rule
    // that SYM names are unreliable identity hints (see DEVNOTES.md; 'kanban2'
    // was Xu). That guess was wrong and actively harmful: glwater3's 'ladline7'
    // is the gate the player must open to proceed -- it sits ~1 step from where
    // Aaron stood facing that gate (its tri-83 centroid (-287,815) vs his
    // (-238,706)), it is the ONLY catalog entry anywhere near it, Aaron
    // independently reported this entry's coords put him "right in front of the
    // gate to sewer 2", and its script dispatches REQs like the 'saku' gates
    // rather than doing anything ladder-like. Labeling it "Ladder" is what made
    // the real gate look absent from the catalog for three BAT cycles.
    // 'ladline5'/'ladline6' are likewise unconfirmed -- with no behavioral
    // evidence for what they are, they now fall through to the generic
    // "Object" type name rather than asserting a wrong specific identity.
    // Do NOT re-add SYM-derived names here without behavioral evidence.
    //
    // v0.18.3.291: 'ladline7' now HAS that evidence, so it gets a name -- but
    // note the evidence is empirical, not nominal. In the .290 BAT Aaron walked
    // the entire catalog while standing at the blocking gate and reported that
    // this was the ONLY entry that placed him at the right spot; the log agrees
    // (it is the sole entry reaching "In range." there, at ~1 step, while every
    // Gate is 500-1300 units away). Combined with its script REQ-dispatching
    // like the 'saku' gates, that is behavioral confirmation it IS a gate.
    // Deliberately unnumbered: "Gate 1"-"Gate 3" are saku1-saku3, and this is a
    // 4th distinct mechanism, not saku4 (that's the Director). An unnumbered
    // "Gate" avoids implying an ordering we haven't verified.
    { "ladline7", "Gate" },
    { "Laguna", "Laguna" },
    { "laguna", "Laguna" },
    { "laguna02", "Laguna" },
    { "laguna99", "Laguna" },
    { "Lift", "Elevator" },
    // v0.111.0 (#dsrc): the Deep Sea Research Center's own vocabulary. Aaron:
    // "make sure everything - the terminals, the hatchways, the side rooms,
    // etc. are supported." The hatchways are exits and now name themselves; the
    // terminals were reading as "Interaction 1".
    //
    // 端末 tanmatsu is Japanese for a computer TERMINAL, and FF8's authors spelt
    // it three ways across the six floors -- `Tanmatu` on ddtower2, `Tanme` and
    // `Tanme2` on ddtower3 and ddtower4, `Tanma` on ddtower5, ddtower6,
    // ddruins6 and ddsteam1. Every one is an Interactive Line, and a sweep of
    // all 900 extracted fields finds these four symbols in the Research Center
    // and nowhere else, which is what makes a SYM-keyed row safe here.
    { "Tanma",   "Terminal" },
    { "Tanmatu", "Terminal" },
    { "Tanme",   "Terminal" },
    { "Tanme2",  "Terminal" },
    // 解説 kaisetsu, "explanation" -- ddtower1's briefing panel, the one thing
    // in that room the catalog offered and called "Interaction 1". One field on
    // the whole disc.
    { "Kaisetu", "Information Panel" },
    { "Lifter", "Elevator" },
    { "majo", "Sorceress" },
    { "Man", "Man" },
    { "manhole", "Manhole" },
    { "maniadoor", "Door" },
    // v0.18.3.267: Caraway's Mansion wine-glass puzzle. 'megami' = goddess —
    // the statue whose hands receive the glass. (Same Japanese-SYM convention
    // as 'majo' -> Sorceress above.) Its companion 'te' (= hand) is deliberately
    // NOT mapped: a two-letter SYM is too collision-prone to label globally.
    { "megami", "Statue" },
    { "memo", "Memo" },
    { "meskun", "Student" },
    { "mess", "Desk" },
    { "mindoor", "Door" },
    { "moni", "Study Panel" },
    { "monitor", "Study Panel" },
    { "Monorail", "Monorail" },
    { "monorail", "Monorail" },
    { "Munba2", "Moomba" },
    { "Munba3", "Moomba" },
    { "Munbamini", "Moomba" },
    { "naidarokun", "Student" },
    { "Newspaper", "Newspaper" },
    { "nida", "Nida" },
    { "Obaasan", "Old Woman" },
    { "petdoor", "Door" },
    { "Quistis", "Quistis" },
    { "quistis", "Quistis" },
    { "Quistis2", "Quistis" },
    { "raijin", "Raijin" },
    { "Rinoa", "Rinoa" },
    { "rinoa", "Rinoa" },
    { "Rinoa1", "Rinoa" },
    { "Rinoa2", "Rinoa" },
    { "Rinoau", "Rinoa" },
    { "rinoau", "Rinoa" },
    // v0.18.3.286 (#85): Deling City sewer gate maze (glwater2-5). 'sakuN'
    // entities are the movable gate/valve mechanisms the player toggles to
    // route water through the maze. Mapped to "Gate N" (Aaron's own term for
    // them) instead of the raw SYM-derived "SakuN" fallback -- plain "Saku"
    // isn't meaningful to a screen-reader user. Same sym numbering repeats
    // across the 4 fields (e.g. every field has its own 'saku1'), which is
    // fine: the catalog is per-field, so there's no cross-field collision.
    { "saku1", "Gate 1" },
    { "saku2", "Gate 2" },
    { "saku3", "Gate 3" },
    { "saku4", "Gate 4" },
    { "saku5", "Gate 5" },
    { "saku6", "Gate 6" },
    { "saku7", "Gate 7" },
    { "saku8", "Gate 8" },
    // v0.20.41 (#85 gate maze): the Deling sewer gate CONTROLLERS -- the entity the
    // player actually walks up to and presses action on to open a gate. Static
    // analysis of glwater3.jsm: each carries its own model + SET3 triangle and REQs a
    // 'sakuN' gate visual (ct_lf tri83->saku3 = the blocking gate to Sewer 2;
    // ct_rt tri138/ct_rt2 tri147->saku4; rt_up tri181->saku5). The visuals themselves
    // have no reliable runtime position, so the controller is the navigable anchor.
    // Needs a curated name to clear the junk-gate filter (same as saku = "Gate N").
    { "ct_lf", "Gate" },
    { "ct_rt", "Gate" },
    { "ct_rt2", "Gate" },
    { "rt_up", "Gate" },
    // v0.20.42 (#85 gate maze): the SAME control-mechanism family in the OTHER sewer
    // fields, enumerated from static analysis of every glwater*.sym (complete set, not a
    // pattern): glwater4 ct_lf_dw/lf_up/ct_rt_dw/ct_lt_up/ct_rt_dw2, glwater5 ct_rt_up/
    // ct_rt_dw, glwater1 seigyo. Each is an Other with a model that REQs a gate visual --
    // the point the player operates to open a gate. Per-field catalog, so no cross-field
    // collision (same rationale as the saku block above).
    { "ct_lf_dw", "Gate" },
    { "lf_up", "Gate" },
    { "ct_rt_dw", "Gate" },
    { "ct_lt_up", "Gate" },
    { "ct_rt_dw2", "Gate" },
    { "ct_rt_up", "Gate" },
    { "seigyo", "Gate" },
    { "savePoint", "Save Point" },
    { "scoaul", "Squall" },
    { "Seifer", "Seifer" },
    { "seifer", "Seifer" },
    { "seito1", "Student" },
    { "seito2", "Student" },
    { "seito3", "Student" },
    { "seito4", "Student" },
    { "seito5", "Student" },
    { "seito6", "Student" },
    { "sel_arms", "Selphie" },
    { "Selphie", "Selphie" },
    { "selphie", "Selphie" },
    { "Selphie_u", "Selphie" },
    { "SelphieDummy", "Selphie" },
    { "selphies", "Selphie" },
    { "Soldier1", "Soldier" },
    { "Soldier2", "Soldier" },
    { "SPObasan", "Old Woman" },
    { "Squall", "Squall" },
    { "squall", "Squall" },
    { "Squall2", "Squall" },
    { "squall2", "Squall" },
    { "Squall_O", "Squall" },
    { "Squall_u", "Squall" },
    { "squallo", "Squall" },
    { "squalls", "Squall" },
    { "squallsd", "Squall" },
    { "squallsp", "Squall" },
    { "student1", "Student" },
    { "student2", "Student" },
    { "Train", "Train" },
    { "Train1", "Train" },
    { "Train2", "Train" },
    { "Train3", "Train" },
    { "uketsuke", "Receptionist" },
    { "Ward", "Ward" },
    { "ward", "Ward" },
    { "Window1", "Window" },
    { "Woman", "Woman" },
    { "Women1", "Woman" },
    { "wpndoor", "Door" },
    { "Zell", "Zell" },
    { "zell", "Zell" },
    { "Zell1", "Zell" },
    { "Zell2", "Zell" },
    { "Zell_u", "Zell" },
    { "zells", "Zell" },
    { "ZonJichan", "Zone" },
    { nullptr, nullptr }
};

// ============================================================================
// SYM name -> entity type for special entities (draw/save/shop/card).
// ============================================================================
enum EntityClassificationType {
    EC_NONE = 0,
    EC_DRAW_POINT,
    EC_SAVE_POINT,
    EC_SHOP,
    EC_CARD_GAME,
    EC_NPC,
    EC_INTERACTIVE_OBJECT,
};

struct EntityTypeEntry {
    const char* sym;
    EntityClassificationType type;
};

static const EntityTypeEntry ENTITY_TYPE_TABLE[] = {
    { "cardgamemaster", EC_CARD_GAME },
    { "cardgamemaster2", EC_CARD_GAME },
    { "Cardtanto", EC_CARD_GAME },
    { "dp01", EC_DRAW_POINT },
    { "DrawPoint", EC_DRAW_POINT },
    { "DrawPointSampleCode", EC_DRAW_POINT },
    { "drpoint", EC_DRAW_POINT },
    { "savePoint", EC_SAVE_POINT },
    { nullptr, EC_NONE }
};

// Classification statistics:
//   Skip (controllers): 213 names
//   draw_point: 4 names
//   save_point: 1 names
//   shop: 0 names
//   card_game: 3 names
//   npc: 2395 names
//   Total classified: 2403 names

// ============================================================================
// v0.113.0 (#dsrc): the rule for naming a TRIGGER LINE from the two tables
// above. It lives with the tables rather than beside its one caller, because
// every translation unit that has the tables should have the rule that reads
// them -- the catalog harness includes field_catalog.inl directly and would
// otherwise miss it.
// ============================================================================
#include "line_display_name_model.inl"
#include "line_gate_name_model.inl"
#include "line_camera_pan_surface_model.inl"
