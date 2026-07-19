// field_nav_catalog.inl — Entity catalog building (RefreshCatalog).
// Included from field_navigation.cpp inside the FieldNavigation namespace.
// Do not compile independently.
//
// v0.12.18: Extracted from field_navigation.cpp for readability.
// v0.17.7.0: Two large blocks moved to dedicated helper files for size
//            compliance (catalog.inl was 75.77 KB, 4 KB under the 80 KB
//            hard fail). Helpers live in:
//              field_nav_catalog_diag.inl
//                — DumpEntityDiagOnce, DumpBgDiagOnce,
//                  DumpPartyStateOnce, DumpCoordDiagOnce
//              field_nav_catalog_lateres.inl
//                — ResolveLatePositions, MatchSet3LateCaptures,
//                  ResolveStructPositions
//            Both included BEFORE this file in field_navigation.cpp so
//            their static functions are visible to RefreshCatalog.
//            Behavior byte-for-byte identical to v0.17.6.2 source except
//            the v0.12.17 VARBLOCK-POS unreachable `if (false)` block is
//            dropped. Git history at v0.17.6.2 preserves it.

// v0.18.3.228: IsPartyCharacterSym / PartyCharacterDisplayName moved to
// field_nav_helpers.inl (included earlier, so both stay visible here) to keep
// this file under the 80 KB source-size CI guard.

static void RefreshCatalog()
{
    if (!FF8Addresses::pFieldStateOthers || !FF8Addresses::pFieldStateOtherCount) return;
    __try {
        uint8_t entCount = *FF8Addresses::pFieldStateOtherCount;
        if (entCount == 0) return;
        uint8_t* base = *reinterpret_cast<uint8_t**>(FF8Addresses::pFieldStateOthers);
        if (!base) return;
        uint8_t lim = (entCount < MAX_ENTITIES) ? entCount : (uint8_t)MAX_ENTITIES;

        // Re-detect player.
        // v0.18.3.262 (#83 follow-up): the player controls the party LEADER, which
        // is formation[0] -- NOT necessarily Squall (setpc 0). The old setpc==0
        // rule mis-identified the player whenever Squall was not the leader (e.g.
        // Caraway's Mansion party led by Irvine, formation [2,0,3]): it pointed
        // s_playerEntityIdx at Squall's follower entity, so the real controlled
        // character (Irvine) was treated as a catalog NPC and player-relative
        // navigation used the wrong entity's position. Match the leader's setpc;
        // fall back to setpc==0, then entity 0, if the formation is unreadable.
        {
            // v0.18.3.263: leader = first slot of the FIELD controlled-party array
            // (0x01CFE990, the one the engine uses), NOT the savemap array. In the
            // split-party Caraway arc these differ, so the old read pointed at the
            // wrong team's leader. Fall back to setpc==0, then entity 0.
            uint8_t leaderChar = GetFieldPartyLeaderChar();
            int found = -1, foundSquall = -1;
            for (int i = 0; i < (int)lim; i++) {
                uint8_t setpc = *(base + ENTITY_STRIDE * i + 0x255);
                if (leaderChar != 0xFF && setpc == leaderChar) { found = i; break; }
                if (setpc == 0 && foundSquall < 0) foundSquall = i;
            }
            if (found < 0) found = (foundSquall >= 0) ? foundSquall : 0;
            s_playerEntityIdx = found;
        }

        // v0.18.3.264 (#83 follow-up): detect an ASSEMBLY scene.
        //
        // The field controlled-party roster (0x01CFE990) tells us who the "party
        // train" is, but NOT whether they are currently FOLLOWING (walking field)
        // or STANDING as interactable scene actors (gather scene). In Caraway's
        // Mansion the roster [Squall,Irvine,Rinoa] is placed at distinct scripted
        // positions and every member is talkable; filtering them by roster hid the
        // interactable ones. Their per-entity flags are identical to a follower's
        // (talk=0 push=0 thru=0), so nothing on the entity distinguishes the two
        // modes.
        //
        // What DOES distinguish them: a walking field instantiates ONLY the
        // controlled party; an assembly scene also places party characters that
        // are NOT in the controlled roster (the other team / extra members). So if
        // any placed party-character entity's setpc is NOT in the field roster, the
        // party is assembled/standing, and the roster members are talkable too.
        // (glfurin4: Zell/Quistis/Selphie are placed and not in [0,2,4] -> assembly.
        //  glfury1: only the roster [2,0,3] is present -> following, filter them.)
        bool sceneAssembly = false;
        for (int i = 0; i < (int)lim; i++) {
            uint8_t setpc = *(base + ENTITY_STRIDE * i + 0x255);
            if (!IsPartyCharacterSetpc(setpc)) continue;
            if (IsInFieldControlledParty(setpc)) continue;   // in the roster
            uint16_t tri = *(uint16_t*)(base + ENTITY_STRIDE * i + 0x1FA);
            int32_t  fx  = *(int32_t*)(base + ENTITY_STRIDE * i + 0x190);
            int32_t  fy  = *(int32_t*)(base + ENTITY_STRIDE * i + 0x194);
            if (tri > 0 || fx != 0 || fy != 0) { sceneAssembly = true; break; }
        }
        if (!s_scanTraced)
            Log::Field("FieldNavigation: [party-state] sceneAssembly=%d (placed non-roster party char present)",
                       sceneAssembly ? 1 : 0);

        // One-shot diagnostic dumps (extracted v0.17.7.0).
        // See field_nav_catalog_diag.inl. Each helper no-ops on subsequent calls.
        DumpEntityDiagOnce(base, lim);
        DumpExtendedEntityScanOnce(base, entCount);   // v0.18.3.231 DIAG
        DumpBgDiagOnce(lim);
        DumpPartyStateOnce();
        DumpPuzzleDiagOnce();      // v0.18.3.267: glass/statue puzzle objects
        DumpCoordDiagOnce(base, lim);

        // Build set of currently-qualifying entity indices.
        bool qualifies[MAX_ENTITIES] = {};
        EntityInfo fresh[MAX_ENTITIES] = {};
        for (int i = 0; i < (int)lim; i++) {
            uint8_t* block = base + ENTITY_STRIDE * i;
            int16_t  modelId      = *(int16_t*)(block + 0x218);
            uint16_t triId        = *(uint16_t*)(block + 0x1FA);
            uint8_t  setpc        = *(block + 0x255);
            uint8_t  talkonoff    = *(block + 0x24B);
            uint8_t  pushonoff    = *(block + 0x249);
            uint8_t  throughonoff = *(block + 0x24C);
            // v0.12.08 Fix D: Read position for placement validation.
            int32_t  fpX          = *(int32_t*)(block + 0x190);
            int32_t  fpY          = *(int32_t*)(block + 0x194);

            // v0.17.8.3: Resolve this entity's SYM name now (needed by the
            // party filter below). Same offset mapping used elsewhere in this
            // function for JSM lookups.
            const char* symName = "";
            {
                int symIdxFilt = s_symOthersOffset + i;
                if (symIdxFilt >= 0 && symIdxFilt < s_symNameCount)
                    symName = s_symNames[symIdxFilt];
            }

            // v0.18.3.228: Race-free TALKABILITY signal. Runtime flags (talk
            // @0x24B / push @0x249) are set by TALKRADIUS/TALKON during script
            // execution, so a talkable NPC can still read talk=0 at scan time.
            // The static JSM hasTalkSetup flag (script uses TALKRADIUS or TALKON)
            // is parsed at load and cannot race. On ggsta1 TALKRADIUS never fires
            // at all (scripts use TALKON), so runtime capture alone missed the
            // station attendant 'ekiin' and the 'gsm*' students.
            bool jsmTalk = false;
            {
                int symIdxT = s_symOthersOffset + i;
                if (symIdxT >= 0 && symIdxT < s_symNameCount) {
                    const FieldArchive::JSMEntityInfo* jsmT = FindJSMBySym(s_symNames[symIdxT]);
                    if (jsmT && jsmT->hasTalkSetup) jsmTalk = true;
                }
            }
            // v0.18.3.235: talkability is STICKY per field. The talkonoff flag is
            // not just set late, it is TRANSIENT — on ggroom1 Quistis reads talk=1
            // on the first catalog build and 0 on every build after, so she was
            // kept on entry and then party-filtered a second later (a party member
            // WITH a talk radius must stay). Latch the observation instead.
            if (i < MAX_ENTITIES && talkonoff > 0) s_entSeenTalkable[i] = true;
            // Runtime capture (v0.18.3.227) is kept as a secondary signal: it
            // catches entities whose talk radius is enabled dynamically at
            // runtime rather than declared in the static script.
            bool talkable = (talkonoff > 0) || jsmTalk ||
                            (i < MAX_ENTITIES && (s_entSeenTalkable[i] ||
                                                  s_entTalkRadius[i] > 0));

            // v0.18.3.228: per-entity scan trace (see field_nav_catalog_diag.inl).
            // v0.18.3.234: once per field load, not on every rebuild.
            if (!s_scanTraced)
                LogScanEntity(i, symName, (int)modelId, (unsigned)triId,
                              (int)talkonoff, (int)pushonoff, (int)throughonoff, jsmTalk,
                              (unsigned)(i < MAX_ENTITIES ? s_entTalkRadius[i] : 0),
                              talkable, fpX, fpY);

            // v0.14.108 / v0.17.8.3: Party-member / non-interactive-character filter.
            // A FOLLOWING party member is identified by behavioral fingerprint:
            // a visible character (model 0-9) the player walks through
            // (throughonoff>0) with no talk/push -- model slots are field-local
            // so canonical-ID matching (the failed v0.14.107 approach) doesn't
            // work. v0.17.8.3 adds STANDING / high-model scene actors (dorm,
            // Laguna dream): party members placed static or walk-through with no
            // talk/push, sometimes on full NPC models (model>=10), caught by the
            // SYM NAME. The name is also the safe discriminator that protects
            // draw points reusing a party model (Fire Cavern 'drpoint' = model 9,
            // flags 0): 'drpoint'/'savePoint'/'l1' don't match IsPartyCharacterSym
            // so they're KEPT for downstream reclassification. Rules:
            //   - model 0-9 + no talk/push + thru>0       -> following member
            //   - character SYM + no talk/push (any model) -> scene-placed member
            //   - non-character SYM                        -> KEEP
            // Talkable characters (talk>0) are never filtered. Real exits come
            // from the trigger-line/gateway path, not runtime entities, so this
            // never drops an exit. Race: TALKRADIUS setting talkonoff after this
            // scan could transiently filter an NPC; mitigated by per-F9 refresh.
            {
                // v0.18.3.232: PARTY FILTER — driven by setpc, not the SYM name.
                //
                // setpc (0x255) holds the character ID (0-7) for an entity that is
                // an actual party character, and 0xFE for anything that is not. The
                // catalog already trusts this byte to identify the player.
                //
                // The previous SYM-based rules were built on a false premise. The
                // engine instantiates only the ACTIVE party members, while the JSM
                // SYM list names all six playable characters, so every NPC slot is
                // shifted. On ggsta1 (party = Squall+Zell+Quistis) slot2 is Quistis
                // but carries SYM 'irvine', and the station attendant in slot3
                // carries SYM 'rinoa' — so the "named party member" rule deleted the
                // train guard the player needs to buy a ticket, along with two
                // students. Only the two slots whose shifted SYMs happened to look
                // non-party survived, which is exactly the reported symptom.
                //
                // setpc has no such ambiguity: a real NPC is never a party character
                // no matter which SYM lands on it. Party members are still filtered
                // (preserving the v0.17.8.3 dormitory/classroom behavior), EXCEPT
                // when talkable — an interactable party member is kept and labeled by
                // proper name, per the interactable-party-member requirement.
                bool noInteract  = (talkonoff == 0 && pushonoff == 0 && !talkable);
                bool isPartyChar = IsPartyCharacterSetpc(setpc);
                // v0.18.3.236 (#71): IN-PARTY rule, from the bg2f_2/bg2f_1
                // Selphie evidence (2026-07-12 run). An entity whose setpc
                // character is currently in the ACTIVE party formation is the
                // follow entity (or a scene double of a recruited member) —
                // never a catalog target, even when talkable. Post-join
                // Selphie on bg2f_2 was talk=1 thru=0, so only the roster
                // identifies her.
                //
                // Deliberately NOT a walk-through (thru>0) rule: the .235
                // ggroom1 fix keeps Quistis (talk=1 push=1 thru=1, NOT in the
                // active party during that scene) as a named catalog entry —
                // flags alone cannot separate her from a follower. The roster
                // is the discriminator that preserves both behaviors.
                //
                // Known remaining gap (#71): a NOT-yet-recruited scene actor
                // parked invisible pre-scene (bg2f_2 Selphie before her run-in)
                // still lists — the entity SHOW/HIDE flag was never located
                // (v05.69 VISDIAG investigation closed without a result).
                // Needs a per-session flag-discovery diagnostic on bg2f_2.
                // v0.18.3.263 (#83 follow-up): use the FIELD controlled-party
                // array (0x01CFE990), not the savemap array. This is how the game
                // itself distinguishes a following party member from an
                // interactable one: the controlled team's setpc values are in
                // 0x01CFE990; the OTHER team standing in the room is not, so its
                // members fall through to the talkable-scene-actor path. Fixes both
                // the whole-party over-listing (the walking train is the field
                // party) and the split-party leader confusion.
                bool inActiveParty = isPartyChar &&
                                     IsInFieldControlledParty(setpc);
                // v0.18.3.261 (#83): CORRECTED talk-suppress polarity, scoped to
                // the party-filter keep decision. Disassembly of FF8_EN.exe proved
                // the talk-selection routine (0x004796E0) SKIPS an entity whose
                // 0x24B byte is nonzero and considers it only when 0x24B==0 --
                // TALKON writes 0, TALKOFF writes 1. So 0x24B==0 is TALK-ENABLED,
                // the opposite of the mod's historic `talkonoff>0` reading.
                //
                // v0.18.3.262 (#83 follow-up): a talkable scene actor must be a
                // NON-active-party member. The active-party entities are the field
                // "party train" -- the controlled leader plus its followers -- and
                // they read talkonoff==0 too (the untouched default), so an earlier
                // `talkonoff==0`-only rule listed the whole walking party as NPCs in
                // every normal field (glfury1: Irvine/Squall/Quistis all kept). The
                // talk byte cannot separate a following active member from an
                // interactable one, so we gate on roster membership instead:
                //   - NON-active party char, placed, not suppressed (talkonoff==0)
                //     -> talkable SCENE ACTOR, keep and name (glfurin4 Quistis/Zell/
                //     Selphie -- the #83 case).
                //   - Active-party char (leader or follower) -> filtered as the
                //     party train (fixes the glfurin4-fix regression on glfury1).
                // Tradeoff: active members that happen to be talkable in a gather
                // scene (glfurin4 Irvine/Rinoa) are not surfaced -- acceptable vs.
                // listing the whole party in every walk-around field.
                // "Placed" (tri or fp nonzero) guards against unplaced ghost slots.
                //
                // v0.18.3.264 (#83 follow-up): in an ASSEMBLY scene (sceneAssembly),
                // the roster members are STANDING and interactable, so keep them
                // too -- only the player (controlled character) is excluded, since
                // you cannot talk to yourself. In a walking field (not assembly) the
                // roster is the follow train and is filtered. A non-roster placed
                // party char is always a talkable scene actor.
                bool isPlayer = (i == s_playerEntityIdx);
                bool placed = (triId > 0) || (fpX != 0 || fpY != 0);
                bool talkableActor = !isPlayer && placed && (talkonoff == 0) &&
                                     (modelId >= 0) &&
                                     (!inActiveParty || sceneAssembly);
                // The player (leader) is always filtered: never a catalog target.
                if (isPartyChar && !talkableActor &&
                    (noInteract || inActiveParty || isPlayer)) {
                    const char* pn = PartyCharacterNameById(setpc);
                    Log::Field("FieldNavigation: [party-filter] ent%d model=%d setpc=%d (%s) "
                               "sym='%s' filtered (party member; thru=%d inParty=%d noInteract=%d)",
                               i, (int)modelId, (int)setpc, pn ? pn : "?",
                               symName, (int)throughonoff, inActiveParty ? 1 : 0,
                               noInteract ? 1 : 0);
                    continue;
                }
            }

            // v0.17.8.3: the v0.17.8.2 [party-filter-miss] diagnostic was
            // removed here once the fix was BAT-confirmed on bgryo2_1 (all six
            // party entities -- squalls/squallsd/zell/zells/selphie/selphies,
            // including the model-11 selphie -- filtered as named party members,
            // zero misses, navigation intact). Draw-point safety holds by
            // construction: 'drpoint' is not a character name and has thru=0, so
            // neither the follower nor the named-party branch touches it.

            // v0.18.3.269 (#71): HIDDEN-ENTITY filter. The SHOW/HIDE flag is
            // entity flags dword @0x160, bit 3 (0x08): HIDE sets it, SHOW clears
            // it. Found by resolving the engine's opcode table (base 0x00B8DE94,
            // validated against [0x57]=TALKON/[0x58]=TALKOFF): SHOW (opcode 0x60)
            // @0x0051EAD0 does `and ecx,0xFFFFFFF7`, HIDE (opcode 0x61)
            // @0x0051EB40 does `or ecx,8`, both on [entity+0x160].
            //
            // This is the flag the v05.69 VISDIAG investigation failed to locate.
            // Without it the catalog announces actors that are scripted into the
            // scene but not yet drawn -- e.g. Zell and Selphie listed on the
            // Caraway statue screen (glfurin3) before they appear, which only
            // happens once the glass is placed. Same root cause as the #71
            // "not-yet-recruited scene actor parked invisible pre-scene" gap.
            {
                uint32_t entFlags = *(uint32_t*)(block + 0x160);
                if ((entFlags & 0x08) != 0 && i != s_playerEntityIdx) {
                    if (!s_scanTraced)
                        Log::Field("FieldNavigation: [SCAN-DROP] ent%d sym='%s' hidden "
                                   "(flags@0x160=0x%08X bit3 set by HIDE) -- skipped",
                                   i, symName, (unsigned)entFlags);
                    continue;
                }
            }

            // v0.17.7.1: Walkmesh exclusion rule.
            //
            // Drop entities that are BOTH non-talkable AND non-pushable AND
            // positioned off the walkmesh. These are typically light sources,
            // particle emitters, decorative props, and other scenery the
            // player cannot reach or interact with. The OR-with-talkonoff /
            // pushonoff condition preserves entities like over-railing guards
            // (off-mesh but talkable) and walking NPCs whose model puts them
            // briefly off-mesh between steps (talk radius keeps them).
            //
            // Light sources entering via JSM_ENT_INTERACTIVE_OBJECT promotion
            // (bypassing the existing ENTITY_SKIP_NAMES BG filter) drop here
            // because lights have no talkradius. fepic1's three exit Lines
            // pass through unaffected because they're injected from the
            // SETLINE/JSM-MAP_EXIT block, not the runtime loop -- their
            // walkmesh check lives in those blocks (added separately).
            //
            // The v0.12.08 reachability filter (REMOVED in v0.12.09 because
            // bggate_6 has a guard on tri=87 while the player stands on
            // tri=22, disconnected islands within one screen) does not
            // recur here: the guard has talkonoff>0 so the OR keeps it.
            //
            // Skip player and entities without a readable position (fpX=fpY=0
            // covers the placeholder case where the engine hasn't placed the
            // entity yet -- treat that as on-mesh provisionally rather than
            // dropping prematurely).
            if (i != s_playerEntityIdx &&
                talkonoff == 0 && pushonoff == 0 &&
                (fpX != 0 || fpY != 0)) {
                float wmX = (float)(fpX / 4096);
                float wmY = (float)(fpY / 4096);
                if (!IsInsideWalkmesh(wmX, wmY)) {
                    Log::Field("FieldNavigation: [walkmesh-excl] ent%d model=%d "
                               "pos=(%.0f,%.0f) off-mesh + no-talk/push -- excluded",
                               i, (int)modelId, wmX, wmY);
                    continue;
                }
            }

            // v05.52: Classify entity type by interaction flags.
            // setpc==0 means this IS the player; setpc!=0 means it isn't.
            // Interaction flags determine what the player can do with it.
            // v0.07.97: Entities with visible generic character models (modelId >= 10)
            // always classify as NPC. Walking NPCs get pushonoff before talkonoff
            // (PUSHRADIUS fires in init, TALKRADIUS fires later), so push-only
            // at catalog-build time doesn't mean "object" for visible characters.
            // The pushonoff → Object path only applies to invisible (model<0) entities.
            // v0.18.3.228: `talkable` (static JSM talk setup, or either runtime
            // signal) classifies the entity as an NPC up front, so a talkable NPC
            // whose talkonoff flag has not been set yet no longer falls through to
            // the push-only skip below and get discarded.
            EntityType etype = ENT_UNKNOWN;
            if (talkable)                         etype = ENT_NPC;
            else if (pushonoff > 0 && modelId >= 10) etype = ENT_NPC;   // v0.07.97: walking NPC, talk not yet set
            else if (pushonoff > 0 && modelId >= 0) {
                // v0.12.12: visible push-only entity — not interactable, skip.
                // v0.18.3.228: now logs its reason instead of dropping silently.
                if (!s_scanTraced) LogScanDropPushOnly(i, symName, (int)modelId);
                continue;
            }
            else if (pushonoff > 0)               etype = ENT_OBJECT;
            // v0.18.3.230: the EXIT branch now requires an INVISIBLE entity.
            // throughonoff just means "the player can walk through this"; on a
            // visible character that makes it an NPC, not an exit. ggsta1's
            // 'ekiin'/'gsl0' (model 8, thru=1) were surfacing as type=Exit, which
            // is both wrong to announce and wrong for navigation grouping. Real
            // exits are invisible trigger entities (model < 0) — and genuine map
            // exits come from the trigger-line/gateway path anyway, not from here.
            else if (throughonoff > 0 && modelId < 0) etype = ENT_EXIT;
            else                                  etype = ENT_NPC;  // visible character, default to NPC
            bool hasModel = (modelId >= 0);
            bool hasInteraction = (talkonoff > 0 || pushonoff > 0 || throughonoff > 0);
            // v0.12.08 Fix D: Entities at (0,0) with triId=0 are inactive placeholders
            // even if they have a model assigned. Must have either a valid walkmesh
            // triangle OR a non-zero position to be considered placed.
            bool isPlaced = (triId > 0) || (hasModel && (fpX != 0 || fpY != 0));
            bool isSpecialJSM = false;
            if (!hasModel) {
                int symIdx2 = s_symOthersOffset + i;
                if (symIdx2 >= 0 && symIdx2 < s_symNameCount) {
                    const FieldArchive::JSMEntityInfo* jsm2 = FindJSMBySym(s_symNames[symIdx2]);
                    if (jsm2 && (jsm2->type == FieldArchive::JSM_ENT_SAVE_POINT ||
                                 jsm2->type == FieldArchive::JSM_ENT_DRAW_POINT ||
                                 jsm2->type == FieldArchive::JSM_ENT_SHOP ||
                                 jsm2->type == FieldArchive::JSM_ENT_CARD_GAME)) {
                        // Check if the entity has a valid runtime position
                        int32_t fpX2 = *(int32_t*)(block + 0x190);
                        int32_t fpY2 = *(int32_t*)(block + 0x194);
                        if (fpX2 != 0 || fpY2 != 0 || triId > 0) {
                            isSpecialJSM = true;
                            isPlaced = true;
                        }
                    }
                }
            }
            if (isPlaced && (hasModel || isSpecialJSM)) {
                qualifies[i] = true;
                EntityInfo ei_info = {};
                ei_info.entityIdx  = i;
                ei_info.modelId    = modelId;
                ei_info.triangleId = triId;
                ei_info.type       = etype;
                ei_info.gatewayIdx = -1;
                ei_info.name[0]    = '\0';
                // v0.18.3.235: entity type refinement + display naming.
                // Extracted to field_nav_catalog_naming.inl — a statement fragment
                // included inline (same pattern as field_nav_catalog_dedupe.inl) to
                // hold this file under the 80 KB source-size CI guard. It declares
                // `entName`, which is consumed immediately below.
                #include "field_nav_catalog_naming.inl"
                strncpy(ei_info.name, entName, sizeof(ei_info.name) - 1);
                ei_info.name[sizeof(ei_info.name) - 1] = '\0';
                fresh[i] = ei_info;
                if (!s_scanTraced)
                    LogScanKeep(i, symName, EntityTypeName(ei_info.type), ei_info.name);
            } else {
                // v0.18.3.228: the other formerly-silent drop path (unplaced
                // placeholder entity) now records its reason.
                if (!s_scanTraced)
                    LogScanDropUnplaced(i, symName, (int)modelId, (unsigned)triId,
                                        fpX, fpY, hasModel, isSpecialJSM);
            }
        }
        // v0.18.3.234: the scan trace has now emitted one full pass for this
        // field; suppress it on subsequent rebuilds (RefreshCatalog runs ~1/sec).
        s_scanTraced = true;

        // v05.70: Screen filtering — exclude entities on the other side of
        // any active SETLINE trigger line from the player. This hides NPCs
        // that are on a different camera screen (e.g. front vs back of
        // bgroom_1 classroom). Only applies when we have trigger lines and
        // can read the player position.
        // v05.71: Track which entities were screen-filtered so we can identify
        // which trigger lines are true screen transitions (they separate the
        // player from at least one filtered entity).
        bool screenFiltered[MAX_ENTITIES] = {};
        if (s_capturedLineCount > 0 && s_playerEntityIdx >= 0) {
            float playerX, playerY;
            if (GetEntityPos(s_playerEntityIdx, playerX, playerY)) {
                int filtered = 0;
                for (int i = 0; i < (int)lim; i++) {
                    if (!qualifies[i]) continue;
                    if (i == s_playerEntityIdx) continue;  // never filter the player
                    float entX, entY;
                    if (GetEntityPos(i, entX, entY)) {
                        if (IsSeparatedByTriggerLine(playerX, playerY, entX, entY)) {
                            qualifies[i] = false;
                            screenFiltered[i] = true;
                            filtered++;
                        }
                    }
                }
                if (filtered > 0) {
                    Log::Field("FieldNavigation: [screen] filtered %d entities on other side of trigger lines (player at %.0f,%.0f)",
                               filtered, playerX, playerY);
                }
            }
        }

        // v0.12.08 Fix B: Walkmesh reachability filter — REMOVED in v0.12.09.
        // Was filtering entities on disconnected walkmesh islands, but FF8 fields
        // often have multiple elevation layers that create disconnected islands
        // within the same playable screen (e.g., bggate_6 guard on tri=87 vs
        // player on tri=22). This caused false positives, removing valid NPCs.
        // The existing trigger-line-based screen filtering handles off-screen
        // entities adequately without walkmesh connectivity checks.

        // Remember which entry the user had selected (entity or gateway).
        int prevSelectedEntity = -2;  // -2 = none, -1 = gateway, >=0 = entity
        int prevSelectedGateway = -1;
        if (s_selectedCatalogIdx >= 0 && s_selectedCatalogIdx < s_catalogCount) {
            prevSelectedEntity  = s_catalog[s_selectedCatalogIdx].entityIdx;
            prevSelectedGateway = s_catalog[s_selectedCatalogIdx].gatewayIdx;
        }

        // Rebuild: first, retain existing entity entries that still qualify (in order).
        // v05.51: Also retain background entities (entityIdx <= -100) — they'll be
        // re-evaluated below. Only retain "others" entities here.
        EntityInfo newCatalog[MAX_CATALOG] = {};
        int newCount = 0;
        for (int c = 0; c < s_catalogCount && newCount < MAX_CATALOG; c++) {
            int ei = s_catalog[c].entityIdx;
            if (ei >= 0 && ei < (int)lim && qualifies[ei]) {
                newCatalog[newCount++] = fresh[ei];
                qualifies[ei] = false;  // mark as placed
            }
            // Gateway and background entries are re-added below — skip them here.
        }
        // Then append any newly-qualifying entities at the end.
        int added = 0;
        for (int i = 0; i < (int)lim && newCount < MAX_CATALOG; i++) {
            if (qualifies[i]) {
                newCatalog[newCount++] = fresh[i];
                added++;
            }
        }

        // v0.07.83: JSM-based exit detection for screen boundary trigger lines.
        // Each JSM_ENT_LINE_SCREEN_BOUND captured line becomes an ENT_EXIT entry
        // with the destination resolved from the MAPJUMP destination field ID.
        // Replaces INF gateway exits entirely (INF data is vestigial PS1 data).
        //
        // v0.17.7.1: Removed the v0.12.24 field-wide demote that converted
        // SCREEN_BOUND lines into Interactions whenever ANY entity on the
        // field was an Interactive Object. That rule fired on fepic1 (Front
        // Gate 5) and turned the three legitimate exit Lines into
        // 'Interaction 1/2/3'. Per-line discrimination now happens in the
        // JSM scanner via TALKRADIUS/TALKON detection -- if a Line really IS
        // dual-purpose (dormitory bed: MAPJUMP + dialog + TALK setup) the
        // scanner classifies it as JSM_ENT_LINE_INTERACTIVE and this exit
        // loop skips it on lineType alone (no field-wide lookup needed).
        if (s_capturedLineCount > 0 && s_playerEntityIdx >= 0) {
            float scrPlayerX = 0, scrPlayerY = 0;
            if (GetEntityPos(s_playerEntityIdx, scrPlayerX, scrPlayerY)) {
                for (int t = 0; t < s_capturedLineCount && newCount < MAX_CATALOG; t++) {
                    if (!s_capturedLines[t].active) continue;
                    if (s_capturedLines[t].lineType != FieldArchive::JSM_ENT_LINE_SCREEN_BOUND) continue;
                    // v0.17.7.1.2 / v0.17.7.5.4: SCREEN_BOUND lines that
                    // genuinely REQ a dialog-bearing entity are dual-purpose
                    // (exit-via-interaction). The Line REQs a background
                    // entity that fires dialog (dorm bed: bed Line REQs the
                    // bed Background, which shows "Sleep?"; the MAPJUMP fires
                    // as a consequence of the player choosing yes, not as a
                    // walk-across event). These show only as Interactions
                    // below, not as Exits here -- showing both would be
                    // confusing and the Exit name (next-day field) is
                    // uninformative anyway.
                    //
                    // fepic1's three exit Lines, bgroad_5 squalls (Hallway 5
                    // -> Dormitory), and similar pure-exit Lines do NOT
                    // REQ dialog entities (they may still use 0x1C extended
                    // dispatch for sound/particle effects, but that's not
                    // a dual-purpose signal), so they pass through here as
                    // Exits.
                    //
                    // The check used to be `hasExtDispatch` which incorrectly
                    // suppressed bgroad_5 squalls because squalls' own script
                    // uses 0x1C for non-dialog purposes. v0.17.7.5.4 split
                    // hasExtDispatch into two signals: hasExtDispatch (own
                    // 0x1C usage, very common, not a dual-purpose indicator)
                    // and hasDialogReqTarget (REQ to dialog/ext-dispatch
                    // entity, only set by REQ-following post-pass). The
                    // catalog now uses hasDialogReqTarget, which only fires
                    // for genuine dual-purpose Lines.
                    if (s_capturedLines[t].hasDialogReqTarget) continue;

                    // v0.17.7.5.5: Self-loop detection. SCREEN_BOUND lines whose
                    // resolved destField equals the CURRENT field id are in-place
                    // state transitions, not navigational exits. The canonical
                    // case is a dormitory bed: walking onto it does a MAPJUMP to
                    // the same field id (which the engine treats as "reload this
                    // field, advancing some state like day/night"). The player
                    // doesn't move to a different room -- they wake up where
                    // they slept.
                    //
                    // BAT'd on bgryo1_4 (Dormitory Double 4, field 240): ent0
                    // 'squall' SCREEN_BOUND with destField=240 was labeled
                    // "Exit to B-Garden - Dormitory Double 4" -- nonsense
                    // because that IS the field the player is on. Aaron
                    // correctly identified the bed should be an Interaction.
                    //
                    // Block 2 below picks up self-loop SCREEN_BOUND lines as
                    // Interactions (same condition mirrored there). This is
                    // the same pattern as the hasDialogReqTarget split for
                    // genuinely dual-purpose Lines: Block 1 suppresses, Block 2
                    // emits the appropriate Interaction label.
                    //
                    // Safety: an in-place state-change Line that ISN'T a sleep
                    // transition (e.g. a script-driven looping animation Line)
                    // would also be treated as an Interaction here. That's
                    // mostly fine -- such a Line is still something the player
                    // CAN interact with, even if the meaning differs from
                    // "sleep here". A bare "Exit" label to the current field
                    // is unambiguously wrong; Interaction is at worst slightly
                    // imprecise.
                    {
                        uint16_t curFid = FF8Addresses::pCurrentFieldId
                                          ? *FF8Addresses::pCurrentFieldId : 0xFFFF;
                        if (s_capturedLines[t].destFieldId == (int)curFid) continue;
                    }
                    float tcx = (float)(s_capturedLines[t].x1 + s_capturedLines[t].x2) / 2.0f;
                    float tcy = (float)(s_capturedLines[t].y1 + s_capturedLines[t].y2) / 2.0f;

                    // Reachability: trigger center must not be separated from player
                    // by any other active screen-boundary trigger line.
                    //
                    // v0.18.3.278: BOUNDED segment test, matching the v0.17.8.10
                    // gateway fix and the v0.18.3.268 interaction fix. This block
                    // was the last user of the infinite-line side test, which
                    // extends every screen-bound line forever, so an exit could be
                    // "separated" by a short line that does not lie between it and
                    // the player. On glfurin1 that made the Mansion 4 exit
                    // (line1, centre 146,500) appear and vanish as the player moved
                    // -- present at 19:01:26, gone at 19:01:33 -- because line0's
                    // infinite extension flipped sides. Also skips testing a line
                    // against itself.
                    {
                        bool exitCrossed = false;
                        for (int dt = 0; dt < s_capturedLineCount && !exitCrossed; dt++) {
                            if (!s_capturedLines[dt].active) continue;
                            if (dt == t) continue;
                            if (s_capturedLines[dt].lineType != FieldArchive::JSM_ENT_LINE_SCREEN_BOUND &&
                                s_capturedLines[dt].lineType != FieldArchive::JSM_ENT_UNKNOWN)
                                continue;
                            if (SegmentsCross(scrPlayerX, scrPlayerY, tcx, tcy,
                                              (float)s_capturedLines[dt].x1, (float)s_capturedLines[dt].y1,
                                              (float)s_capturedLines[dt].x2, (float)s_capturedLines[dt].y2)) {
                                exitCrossed = true;
                                Log::Field("FieldNavigation: [refresh] exit line%d center=(%.0f,%.0f) "
                                           "filtered: path crosses screen-bound line%d", t, tcx, tcy, dt);
                            }
                        }
                        if (exitCrossed) continue;
                    }

                    // v0.17.7.1.1: Robust destination recovery for PSHM_W-sourced
                    // MAPJUMPs. When the JSM static scan couldn't extract a usable
                    // destFieldId (the script pushed a memory-variable marker like
                    // 0x8000xxxx at MAPJUMP time, which the scanner treats as a
                    // marker and the marker survives into this code path as a
                    // negative int32 or an out-of-range positive), match the
                    // SETLINE center to the nearest INF gateway. INF gateway
                    // destFieldIds are static binary data in the .inf file and
                    // reliable when present. Threshold: 1000 world units --
                    // SETLINE trigger lines and INF gateway lines for the same
                    // physical exit are typically co-located (both at the screen
                    // boundary), often within ~200 units; 1000 gives generous
                    // margin without risking cross-matching to a different exit.
                    //
                    // World-map dest (-2) is preserved -- those resolve correctly
                    // through the WorldMapJump branch below.
                    //
                    // The dedup-against-existing-exit check in the v0.07.94 INF
                    // gateway block runs after this and catches the duplicate via
                    // displayName strcmp (same FIELD_DISPLAY_NAMES table on both
                    // paths), so the INF gateway won't be added as a separate
                    // entry once we've recovered its destId here.
                    int destId = s_capturedLines[t].destFieldId;
                    if ((destId < 0 || destId >= FIELD_DISPLAY_NAMES_COUNT) &&
                        destId != -2 && s_gatewayCount > 0) {
                        float bestDistSq = 1000.0f * 1000.0f;
                        int bestGw = -1;
                        for (int gi = 0; gi < s_gatewayCount; gi++) {
                            float gdx = s_gateways[gi].centerX - tcx;
                            float gdy = s_gateways[gi].centerZ - tcy;
                            float dsq = gdx*gdx + gdy*gdy;
                            if (dsq < bestDistSq) {
                                bestDistSq = dsq;
                                bestGw = gi;
                            }
                        }
                        if (bestGw >= 0) {
                            int recoveredId = (int)s_gateways[bestGw].destFieldId;
                            Log::Field("FieldNavigation: [refresh] SETLINE line%d "
                                       "center=(%.0f,%.0f) destId=%d unresolvable -> "
                                       "matched INF gateway %d destId=%d (dist=%.0f) "
                                       "-- recovering",
                                       t, tcx, tcy, destId, bestGw, recoveredId,
                                       sqrtf(bestDistSq));
                            destId = recoveredId;
                        } else {
                            Log::Field("FieldNavigation: [refresh] SETLINE line%d "
                                       "center=(%.0f,%.0f) destId=%d unresolvable, "
                                       "no INF gateway within 1000 units -- staying generic",
                                       t, tcx, tcy, destId);
                        }
                    }

                    // Resolve destination name from MAPJUMP field ID.
                    char exitName[48];
                    if (destId >= 0 && destId < FIELD_DISPLAY_NAMES_COUNT) {
                        snprintf(exitName, sizeof(exitName), "Exit to %s", FIELD_DISPLAY_NAMES[destId]);
                    } else if (destId == -2) {
                        strncpy(exitName, "Exit to World Map", sizeof(exitName) - 1);
                    } else {
                        strncpy(exitName, "Exit", sizeof(exitName) - 1);
                    }
                    exitName[sizeof(exitName) - 1] = '\0';

                    EntityInfo trigExit = {};
                    trigExit.entityIdx  = -200 - t;
                    trigExit.modelId    = -1;
                    trigExit.triangleId = 0;
                    trigExit.type       = ENT_EXIT;
                    trigExit.gatewayIdx = -1;
                    strncpy(trigExit.name, exitName, sizeof(trigExit.name) - 1);
                    trigExit.name[sizeof(trigExit.name) - 1] = '\0';
                    newCatalog[newCount++] = trigExit;
                }
            }
        }

        // v05.72: Add reachable event triggers (non-screen-transition) as "Event".
        // These are active trigger lines on the player's screen that don't
        // separate screen-filtered entities. They fire script events when crossed.
        if (s_capturedLineCount > 0 && s_playerEntityIdx >= 0) {
            float evPlayerX = 0, evPlayerY = 0;
            if (GetEntityPos(s_playerEntityIdx, evPlayerX, evPlayerY)) {
                for (int t = 0; t < s_capturedLineCount && newCount < MAX_CATALOG; t++) {
                    if (!s_capturedLines[t].active) continue;
                    // Skip if already added as a screen transition.
                    bool alreadyAdded = false;
                    for (int c = 0; c < newCount; c++) {
                        if (newCatalog[c].entityIdx == (-200 - t)) { alreadyAdded = true; break; }
                    }
                    if (alreadyAdded) continue;
                    // v0.07.84: Skip lines already classified by JSM as camera pans or events.
                    // Only unclassified (UNKNOWN) lines should appear as "Event" entries.
                    // Camera pan lines are transparent navigation markers, not interactable.
                    // v0.12.12: Also skip UNKNOWN lines — these are unclassified trigger lines
                    // that don't fire any player-visible event. Showing them as "Event"
                    // is confusing (player arrives and nothing happens).
                    // v0.17.8.7: ALSO skip LINE_INTERACTIVE. With campan/event/screenbound/
                    // unknown all skipped, LINE_INTERACTIVE was the ONLY type this block still
                    // emitted -- and the Interaction block below ALSO emits it (same -200-t
                    // sentinel), so every interactive line was injected TWICE: once as "Event"
                    // (type ENT_OBJECT) and once as "Interaction N" (type ENT_INTERACTION). On
                    // bghall_1 line5 (a pathway sign) showed as both, and the F9 cursor appeared
                    // to "flicker" between Event and Interaction. Worse, the bogus ENT_OBJECT
                    // "Event" entry tripped the JSM-injection block's `alreadyInCatalog`
                    // (type==ENT_OBJECT) test, suppressing the real Directory (igyous1, also
                    // ENT_OBJECT). Skipping LINE_INTERACTIVE here makes this block emit nothing
                    // (its original UNKNOWN-only purpose was already removed in v0.12.12); genuine
                    // interactions still surface once, via the Interaction block.
                    if (s_capturedLines[t].lineType == FieldArchive::JSM_ENT_LINE_CAMERA_PAN ||
                        s_capturedLines[t].lineType == FieldArchive::JSM_ENT_LINE_EVENT ||
                        s_capturedLines[t].lineType == FieldArchive::JSM_ENT_LINE_SCREEN_BOUND ||
                        s_capturedLines[t].lineType == FieldArchive::JSM_ENT_LINE_INTERACTIVE ||
                        s_capturedLines[t].lineType == FieldArchive::JSM_ENT_UNKNOWN)
                        continue;
                    // Reachability check (same as screen transitions).
                    float tcx = (float)(s_capturedLines[t].x1 + s_capturedLines[t].x2) / 2.0f;
                    float tcy = (float)(s_capturedLines[t].y1 + s_capturedLines[t].y2) / 2.0f;
                    bool reachable = true;
                    for (int o = 0; o < s_capturedLineCount; o++) {
                        if (o == t) continue;
                        if (!s_capturedLines[o].active) continue;
                        float olx1 = (float)s_capturedLines[o].x1;
                        float oly1 = (float)s_capturedLines[o].y1;
                        float olx2 = (float)s_capturedLines[o].x2;
                        float oly2 = (float)s_capturedLines[o].y2;
                        float odx = olx2 - olx1;
                        float ody = oly2 - oly1;
                        float crossP = odx * (evPlayerY - oly1) - ody * (evPlayerX - olx1);
                        float crossT = odx * (tcy - oly1) - ody * (tcx - olx1);
                        if (crossP * crossT < -1.0f) { reachable = false; break; }
                    }
                    if (!reachable) continue;
                    // v0.07.78: Skip event triggers near save/draw points already in catalog.
                    // Check both JSM positions AND runtime entity positions (JSM SET3 can be
                    // inaccurate, but runtime entities always have correct coordinates).
                    bool overlapsSaveDrawPt = false;
                    // Check JSM scan positions.
                    for (int j = 0; j < s_jsmEntityCount && !overlapsSaveDrawPt; j++) {
                        const FieldArchive::JSMEntityInfo& je = s_jsmEntities[j];
                        if (!je.hasPosition) continue;
                        if (je.type != FieldArchive::JSM_ENT_SAVE_POINT &&
                            je.type != FieldArchive::JSM_ENT_DRAW_POINT) continue;
                        float jdx = tcx - (float)je.posX;
                        float jdy = tcy - (float)je.posY;
                        if (sqrtf(jdx*jdx + jdy*jdy) < 1000.0f)
                            overlapsSaveDrawPt = true;
                    }
                    // Check runtime catalog entries already classified as save/draw.
                    for (int c2 = 0; c2 < newCount && !overlapsSaveDrawPt; c2++) {
                        if (newCatalog[c2].type != ENT_SAVE_POINT &&
                            newCatalog[c2].type != ENT_DRAW_POINT) continue;
                        int cei = newCatalog[c2].entityIdx;
                        if (cei < 0 || cei >= MAX_ENTITIES) continue;
                        float ex2 = 0, ey2 = 0;
                        if (GetEntityPos(cei, ex2, ey2)) {
                            float edx = tcx - ex2;
                            float edy = tcy - ey2;
                            if (sqrtf(edx*edx + edy*edy) < 1000.0f)
                                overlapsSaveDrawPt = true;
                        }
                    }
                    if (overlapsSaveDrawPt) continue;

                    EntityInfo evEntry = {};
                    evEntry.entityIdx  = -200 - t;
                    evEntry.modelId    = -1;
                    evEntry.triangleId = 0;
                    evEntry.type       = ENT_OBJECT;  // "Event" in announcement
                    evEntry.gatewayIdx = -1;
                    strncpy(evEntry.name, "Event", sizeof(evEntry.name) - 1);
                    evEntry.name[sizeof(evEntry.name) - 1] = '\0';
                    newCatalog[newCount++] = evEntry;
                }
            }
        }

        // v0.12.24 / v0.17.7.1: Add SETLINE-triggered interactive objects as
        // "Interaction N". Line entities classified as JSM_ENT_LINE_INTERACTIVE
        // by the JSM scanner have dialog opcodes (MES/ASK/AMES/AASK) AND a
        // TALKRADIUS/TALKON setup -- genuine player-facing interactions
        // (dormitory bed/desk/wardrobe, classroom desk/sign, etc.).
        //
        // v0.17.7.1: dropped the dual-purpose SCREEN_BOUND-promote-to-Interactive
        // path. JSM scanner now classifies dual-purpose lines (MAPJUMP + dialog
        // + talk setup, like dormitory beds) directly as LINE_INTERACTIVE
        // because TALKRADIUS/TALKON wins over MAPJUMP in the new priority
        // ordering. fepic1's three exit Lines (MAPJUMP only, no dialog, no
        // talk setup) stay LINE_SCREEN_BOUND and are added as Exits above
        // rather than mislabeled here.
        // v0.18.3.268 BUG A: GetEntityPos() returns false when the player's tri
        // id is 0 ("not yet placed") -- normal on close-up screens where coords
        // are still valid. glfurin3 (statue) reads tri=0, so the old
        // `if (GetEntityPos(...))` wrapper skipped EVERY interaction there.
        // Position is only needed for the screen-side filter below, so degrade
        // to "no filtering" rather than dropping everything.
        if (s_capturedLineCount > 0) {
            float intPlayerX = 0, intPlayerY = 0;
            const bool gotIntPlayer = (s_playerEntityIdx >= 0) &&
                                      GetEntityPos(s_playerEntityIdx, intPlayerX, intPlayerY);
            {
                int interactionNum = 0;
                // v0.18.3.268: solo-interaction naming. With exactly ONE active
                // interactive line, that line IS the field's puzzle object, so
                // name it rather than "Interaction 1". 'megami' outranks 'cup':
                // the statue screen holds both, but the interaction is the
                // statue. With 2+ lines the pairing is ambiguous -- keep generic.
                const char* soloName = nullptr;
                {
                    int nInter = 0;
                    for (int t2 = 0; t2 < s_capturedLineCount; t2++)
                        if (s_capturedLines[t2].active &&
                            s_capturedLines[t2].lineType == FieldArchive::JSM_ENT_LINE_INTERACTIVE)
                            nInter++;
                    if (nInter == 1) {
                        bool hasStatue = false, hasGlass = false;
                        for (int j2 = 0; j2 < s_jsmEntityCount; j2++) {
                            if (s_jsmEntities[j2].type != FieldArchive::JSM_ENT_INTERACTIVE_OBJECT) continue;
                            if (_stricmp(s_jsmEntities[j2].symName, "megami") == 0) hasStatue = true;
                            else if (_stricmp(s_jsmEntities[j2].symName, "cup") == 0) hasGlass = true;
                        }
                        if (hasStatue)      soloName = "Statue";
                        else if (hasGlass)  soloName = "Glass";
                    }
                }
                for (int t = 0; t < s_capturedLineCount && newCount < MAX_CATALOG; t++) {
                    if (!s_capturedLines[t].active) continue;
                    // v0.17.7.1.2 / v0.17.7.5.4 / v0.17.7.5.5: Accept
                    // SCREEN_BOUND lines as Interactions in two cases:
                    //   1. hasDialogReqTarget=true (genuine dual-purpose,
                    //      e.g. dorm bed Line REQs dialog-bearing Background)
                    //   2. destFieldId == currentFieldId (self-loop sleep
                    //      transition, e.g. bgryo1_4 bed MAPJUMPs to field 240
                    //      which IS bgryo1_4) -- introduced v0.17.7.5.5 after
                    //      Aaron BAT'd the bed-as-exit mislabel.
                    //
                    // The SETLINE-Exit block above skips those same lines so
                    // they only appear here.
                    //
                    // Pure-exit SCREEN_BOUND lines (fepic1, bgroad_5 squalls)
                    // have hasDialogReqTarget=false AND destFieldId pointing
                    // to a different field -- they fall through this whole
                    // block and remain as Exits emitted by Block 1.
                    bool isInteractive =
                        (s_capturedLines[t].lineType == FieldArchive::JSM_ENT_LINE_INTERACTIVE);
                    if (!isInteractive &&
                        s_capturedLines[t].lineType == FieldArchive::JSM_ENT_LINE_SCREEN_BOUND) {
                        if (s_capturedLines[t].hasDialogReqTarget) {
                            isInteractive = true;
                        } else {
                            // v0.17.7.5.5: self-loop check.
                            uint16_t curFid = FF8Addresses::pCurrentFieldId
                                              ? *FF8Addresses::pCurrentFieldId : 0xFFFF;
                            if (s_capturedLines[t].destFieldId == (int)curFid) {
                                isInteractive = true;
                            }
                        }
                    }
                    if (!isInteractive) continue;
                    // Don't check alreadyAdded -- Interactions use sentinel -600-t,
                    // distinct from exit sentinel -200-t, so both can coexist.
                    // Reachability: must be on same side of screen-boundary trigger lines.
                    float tcx = (float)(s_capturedLines[t].x1 + s_capturedLines[t].x2) / 2.0f;
                    float tcy = (float)(s_capturedLines[t].y1 + s_capturedLines[t].y2) / 2.0f;
                    // v0.18.3.268 BUG B: BOUNDED segment-crossing test, not the
                    // infinite-line side test. IsSeparatedByTriggerLine extends
                    // every screen-bound line to infinity, so a short doorway
                    // line elsewhere can falsely "separate" an interaction on the
                    // far side -- the same over-reach fixed for INF gateways in
                    // v0.17.8.10 via SegmentsCross, which this block never got.
                    // glfurin1: line0's infinite extension filtered the glass
                    // shelf's line1 while line2 (same side as player) survived.
                    if (gotIntPlayer) {
                        bool intCrossed = false;
                        for (int dt = 0; dt < s_capturedLineCount && !intCrossed; dt++) {
                            if (!s_capturedLines[dt].active) continue;
                            if (dt == t) continue;   // never test a line against itself
                            if (s_capturedLines[dt].lineType != FieldArchive::JSM_ENT_LINE_SCREEN_BOUND &&
                                s_capturedLines[dt].lineType != FieldArchive::JSM_ENT_UNKNOWN)
                                continue;
                            if (SegmentsCross(intPlayerX, intPlayerY, tcx, tcy,
                                              (float)s_capturedLines[dt].x1, (float)s_capturedLines[dt].y1,
                                              (float)s_capturedLines[dt].x2, (float)s_capturedLines[dt].y2)) {
                                intCrossed = true;
                                Log::Field("FieldNavigation: [refresh] interaction line%d center=(%.0f,%.0f) "
                                           "filtered: path crosses screen-bound line%d", t, tcx, tcy, dt);
                            }
                        }
                        if (intCrossed) continue;
                    }
                    // v0.17.8.8: If the scanner flagged this line's owning
                    // entity as a save line (own MENUSAVE, or REQ to a save
                    // point), surface it as "Save Point" not "Interaction N".
                    // This restores the bghall_1 Hall 1 save-point label: its
                    // 'savePoint' has PSHM-only X/Y, never injects standalone,
                    // and reaches the catalog only via this trigger line.
                    // Map: captured line t -> JSM line entity jsmIndex (doors+t).
                    bool lineIsSave = false;
                    {
                        int wantIdx = s_jsmDoors + t;
                        for (int j = 0; j < s_jsmEntityCount; j++) {
                            if (s_jsmEntities[j].jsmCategory == 1 &&
                                s_jsmEntities[j].jsmIndex == wantIdx &&
                                s_jsmEntities[j].isSaveLine) {
                                lineIsSave = true; break;
                            }
                        }
                    }
                    // v0.18.3.272: never add a SECOND Save Point. The v0.17.8.8
                    // trigger-line fallback was added because bghall_1's
                    // 'savePoint' has PSHM-only X/Y and "never injects
                    // standalone". It does now (it appears as runtime entity
                    // ent6), so both paths fired and Hall 1 announced
                    // "Save Point 1 of 2" / "2 of 2" for one physical save point.
                    // Keep the fallback for fields where the entity genuinely
                    // doesn't inject; skip it once one is already catalogued.
                    if (lineIsSave) {
                        bool haveSave = false;
                        for (int c = 0; c < newCount; c++)
                            if (newCatalog[c].type == ENT_SAVE_POINT) { haveSave = true; break; }
                        if (haveSave) {
                            Log::Field("FieldNavigation: [refresh] line%d save-line skipped: "
                                       "Save Point already in catalog", t);
                            continue;
                        }
                    }
                    EntityInfo intEntry = {};
                    intEntry.entityIdx  = -200 - t;  // same sentinel as exits -- position lookup works identically
                    intEntry.modelId    = -1;
                    intEntry.triangleId = 0;
                    intEntry.gatewayIdx = -1;
                    if (lineIsSave) {
                        intEntry.type = ENT_SAVE_POINT;
                        strncpy(intEntry.name, "Save Point", sizeof(intEntry.name) - 1);
                        intEntry.name[sizeof(intEntry.name) - 1] = '\0';
                        Log::Field("FieldNavigation: [refresh] line%d surfaced as "
                                   "Save Point (isSaveLine) [v0.17.8.8]", t);
                    } else {
                        interactionNum++;
                        intEntry.type = ENT_INTERACTION;
                        if (soloName)
                            snprintf(intEntry.name, sizeof(intEntry.name), "%s", soloName);
                        else
                            snprintf(intEntry.name, sizeof(intEntry.name), "Interaction %d", interactionNum);
                    }
                    newCatalog[newCount++] = intEntry;
                }
            }
        }

        // v05.52: Background entities removed from cycling catalog.
        // They have no walkmesh position and can't be auto-driven to.
        // Active bg entities are still logged in BGDIAG for diagnostics.
        // Interactive objects (terminals, bulletin boards) are script-triggered
        // by walk-on zones — the player discovers them by exploring, not by
        // navigating to an entity position.

        // Late position resolution (extracted v0.17.7.0).
        // See field_nav_catalog_lateres.inl. Must run in this order — STRUCT-POS
        // depends on LATE-RESOLVE having populated hasPosition for entities that
        // had only hasPshmCoords on entry.
        ResolveLatePositions();
        MatchSet3LateCaptures();
        ResolveStructPositions();

        // v0.07.74: Inject JSM-classified special entities not already in the catalog.
        // These are entities beyond the runtime state array (SYM index >= entCount)
        // or entities in the array that weren't caught by approach A above.
        // Uses SET3 positions extracted by the JSM scanner when available.
        for (int j = 0; j < s_jsmEntityCount && newCount < MAX_CATALOG; j++) {
            const FieldArchive::JSMEntityInfo& je = s_jsmEntities[j];
            if (je.type != FieldArchive::JSM_ENT_SAVE_POINT &&
                je.type != FieldArchive::JSM_ENT_DRAW_POINT &&
                je.type != FieldArchive::JSM_ENT_SHOP &&
                je.type != FieldArchive::JSM_ENT_CARD_GAME &&
                je.type != FieldArchive::JSM_ENT_INTERACTIVE_OBJECT) continue;
                // v0.12.17: JSM_ENT_INTERACTIVE_OBJECT RE-ENABLED.
                // v0.12.12 removed it ("typically background visual effects"),
                // but that also blocked the Directory (dic) on bghall_1.
                // The paired-entity detection + shift-pattern position provides
                // adequate positions for genuine interactive objects.
            // v0.07.80: Check if this type exists as a runtime entity ANYWHERE on
            // the field, even if screen-filtered. JSM SET3 positions are unreliable
            // (bghall_1 saves at 135,588 instead of -700,-8593). Runtime entities
            // always have correct positions. If a runtime entity of matching type
            // exists, prefer it — don't inject JSM with wrong coordinates.
            EntityType jt = JSMTypeToCatalogType(je.type);
            bool runtimeEntityExists = false;
            for (int i2 = 0; i2 < (int)lim; i2++) {
                if (fresh[i2].entityIdx >= 0 && fresh[i2].type == jt) {
                    runtimeEntityExists = true; break;
                }
            }
            if (runtimeEntityExists) continue;
            // Also check what's already in the catalog (from other sources).
            bool alreadyInCatalog = false;
            for (int c = 0; c < newCount; c++) {
                if (newCatalog[c].type == jt) { alreadyInCatalog = true; break; }
            }
            if (alreadyInCatalog) continue;

            // v0.12.09: Draw point consolidation.
            // The JSM draw point position often points to an invisible script entity,
            // not the actual interaction trigger. Check if any interactive catalog
            // entity (NPC/draw point/save point) exists near the JSM position.
            // If not, the real interaction entity is elsewhere — find the closest
            // non-party NPC in the catalog and reclassify it as Draw Point.
            if (jt == ENT_DRAW_POINT && je.hasPosition && s_walkmesh.valid && s_playerEntityIdx >= 0) {
                // Check if any catalog entity with interaction is near the JSM draw point.
                bool interactiveNearDP = false;
                for (int c = 0; c < newCount; c++) {
                    if (newCatalog[c].entityIdx < 0) continue;
                    if (newCatalog[c].entityIdx == s_playerEntityIdx) continue;
                    float nx = 0, ny = 0;
                    if (GetEntityPos(newCatalog[c].entityIdx, nx, ny)) {
                        float ddx = (float)je.posX - nx;
                        float ddy = (float)je.posY - ny;
                        float dd = sqrtf(ddx*ddx + ddy*ddy);
                        if (dd < 300.0f) { interactiveNearDP = true; break; }
                    }
                }
                if (!interactiveNearDP) {
                    // No interactive entity near the JSM draw point position.
                    // Find closest non-party NPC on the player's walkmesh and reclassify.
                    uint16_t pTriDP = 0xFFFF;
                    __try {
                        uint8_t* baseDP = *reinterpret_cast<uint8_t**>(FF8Addresses::pFieldStateOthers);
                        if (baseDP) pTriDP = *(uint16_t*)(baseDP + ENTITY_STRIDE * s_playerEntityIdx + 0x1FA);
                    } __except(EXCEPTION_EXECUTE_HANDLER) {}
                    int bestCatIdx = -1;
                    float bestDist = 1e30f;
                    for (int c = 0; c < newCount; c++) {
                        if (newCatalog[c].entityIdx < 0) continue;
                        if (newCatalog[c].entityIdx == s_playerEntityIdx) continue;
                        if (newCatalog[c].type != ENT_NPC) continue;
                        // Skip party character models (0-9)
                        if (newCatalog[c].modelId >= 0 && newCatalog[c].modelId < 10) continue;
                        float nx = 0, ny = 0;
                        if (GetEntityPos(newCatalog[c].entityIdx, nx, ny)) {
                            float ddx = (float)je.posX - nx;
                            float ddy = (float)je.posY - ny;
                            float dd = sqrtf(ddx*ddx + ddy*ddy);
                            if (dd < bestDist) { bestDist = dd; bestCatIdx = c; }
                        }
                    }
                    if (bestCatIdx >= 0) {
                        newCatalog[bestCatIdx].type = ENT_DRAW_POINT;
                        strncpy(newCatalog[bestCatIdx].name, "Draw Point",
                                sizeof(newCatalog[bestCatIdx].name) - 1);
                        Log::Field("FieldNavigation: [catalog] Draw point consolidation: "
                                   "reclassified ent%d as Draw Point (JSM dp '%s' at %d,%d "
                                   "has no nearby interactive entity, NPC dist=%.0f)",
                                   newCatalog[bestCatIdx].entityIdx, je.symName,
                                   je.posX, je.posY, bestDist);
                        continue; // don't inject the JSM draw point
                    }
                }
            }

            // Not in catalog yet.
            // v0.12.12: No-position draw point fallback.
            // When a JSM draw point exists on the field but has no position
            // (entity beyond runtime window, e.g. Fire Cavern 'drpoint'),
            // reclassify the nearest non-player entity as Draw Point.
            // This is less precise than position-based consolidation but
            // ensures draw points get correct labels in the catalog.
            if (jt == ENT_DRAW_POINT && !je.hasPosition) {
                int bestCatIdx2 = -1;
                for (int c = 0; c < newCount; c++) {
                    if (newCatalog[c].entityIdx < 0) continue;
                    if (newCatalog[c].entityIdx == s_playerEntityIdx) continue;
                    if (newCatalog[c].type != ENT_NPC) continue;
                    // v0.12.12: Don't filter by model<10 here.
                    // On Fire Cavern, the draw point entity uses model 9
                    // (party character range). Without position data,
                    // we accept any NPC as a candidate.
                    float nx2 = 0, ny2 = 0;
                    if (GetEntityPos(newCatalog[c].entityIdx, nx2, ny2)) {
                        // Without JSM position, we can't measure proximity.
                        // Pick the first (and likely only) non-player NPC.
                        bestCatIdx2 = c;
                        break;
                    }
                }
                if (bestCatIdx2 >= 0) {
                    newCatalog[bestCatIdx2].type = ENT_DRAW_POINT;
                    strncpy(newCatalog[bestCatIdx2].name, "Draw Point",
                            sizeof(newCatalog[bestCatIdx2].name) - 1);
                    Log::Field("FieldNavigation: [catalog] Draw point no-position fallback: "
                               "reclassified ent%d as Draw Point (JSM dp '%s' has no position)",
                               newCatalog[bestCatIdx2].entityIdx, je.symName);
                    continue;
                }
            }

            // Try to inject using JSM SET3 position.
            if (!je.hasPosition) continue;
            // Validate position is in plausible range.
            if (je.posX == 0 && je.posY == 0 && je.posZ == 0) continue;

            // v0.17.7.1: Walkmesh exclusion for off-mesh Interactive Objects
            // without TALKRADIUS/TALKON. Lights and decorative props get
            // incorrectly promoted to JSM_ENT_INTERACTIVE_OBJECT during the
            // JSM scan (foundDialogOp/foundExtDispatch + SET3 position make
            // them look like real interactive objects). Almost all of them
            // sit off-walkmesh, so excluding off-mesh + no-talk-setup catches
            // the bug without dropping real signs/desks (those land on the
            // walkmesh because the player has to stand on top of them or
            // adjacent to them to read).
            //
            // Save/Draw/Shop/Card points are NOT filtered here: they may
            // use proximity (PARTICLEON + MENUSAVE etc.) rather than
            // TALKRADIUS, and they're always valuable navigation targets.
            // MAP_EXIT injection runs in a separate block below; it's also
            // not subject to this filter -- exits are always valuable.
            if (jt == ENT_OBJECT && !je.hasTalkSetup &&
                !IsInsideWalkmesh((float)je.posX, (float)je.posY)) {
                Log::Field("FieldNavigation: [walkmesh-excl] JSM ent%d '%s' "
                           "INTERACTIVE_OBJECT pos=(%d,%d) off-mesh + no-talk-setup -- excluded",
                           je.jsmIndex, je.symName, (int)je.posX, (int)je.posY);
                continue;
            }
            EntityInfo jsmEntry = {};
            jsmEntry.entityIdx  = -300 - j;  // unique sentinel for JSM-injected entities
            jsmEntry.modelId    = -1;
            jsmEntry.triangleId = je.posTriangle;
            jsmEntry.type       = jt;
            jsmEntry.gatewayIdx = -1;
            // v0.12.17: Resolve friendly display name for interactive objects.
            // For save/draw points, use the type name. For interactive objects,
            // resolve the SYM name to a user-friendly name (e.g. "dic" -> "Directory").
            char friendlyBuf[48] = {};
            const char* jtName = EntityTypeName(jt);
            if (je.symName[0] != '\0') {
                ResolveFriendlyName(je.symName, friendlyBuf, sizeof(friendlyBuf));
                if (friendlyBuf[0] != '\0' && jt != ENT_SAVE_POINT && jt != ENT_DRAW_POINT
                    && jt != ENT_SHOP && jt != ENT_CARD_GAME)
                    jtName = friendlyBuf;  // use friendly name for interactive objects
            }
            strncpy(jsmEntry.name, jtName, sizeof(jsmEntry.name) - 1);
            jsmEntry.name[sizeof(jsmEntry.name) - 1] = '\0';
            newCatalog[newCount++] = jsmEntry;
            Log::Field("FieldNavigation: [refresh] JSM-injected %s at (%d,%d) sym='%s'",
                       jtName, (int)je.posX, (int)je.posY, je.symName);
        }

        // v0.07.83 JSM_ENT_MAP_EXIT catalog injection. Extracted to
        // field_nav_catalog_mapexits.inl (v0.18.3.266) to keep this file under
        // the CI source-file size ceiling (hard fail > 80 KB); the fragment runs
        // inline here (own braces) and operates on the local newCatalog[]/
        // newCount and the surrounding scan state. Pure textual move — no logic
        // change. See that file for the full logic.
        #include "field_nav_catalog_mapexits.inl"

        // v0.07.94 INF gateway exit injection. Extracted to
        // field_nav_catalog_gateways.inl (v0.18.3.276) to bring this file back
        // under the CI size ceiling (hard fail > 80 KB; it had reached 82.2 KB
        // and the push was refused). The fragment runs inline here (own braces)
        // and operates on the local newCatalog[]/newCount plus the gateway and
        // captured-line state. Pure textual move — no logic change. Same pattern
        // as _mapexits / _dedupe / _naming. See that file for the full logic.
        #include "field_nav_catalog_gateways.inl"

        // v0.17.8.8 object/line dedupe + raw-SYM relabel. Extracted to
        // field_nav_catalog_dedupe.inl (v0.17.8.9) to keep this file under the
        // size ceiling; the fragment runs inline here (own braces) and operates
        // on the local newCatalog[]/newCount. See that file for the full logic.
        #include "field_nav_catalog_dedupe.inl"

        // Detect changes and log.
        bool changed = (newCount != s_catalogCount || added > 0);
        if (!changed) {
            for (int c = 0; c < newCount; c++) {
                if (newCatalog[c].entityIdx != s_catalog[c].entityIdx ||
                    newCatalog[c].gatewayIdx != s_catalog[c].gatewayIdx) {
                    changed = true; break;
                }
            }
        }

        // Commit.
        memcpy(s_catalog, newCatalog, sizeof(s_catalog));
        s_catalogCount = newCount;
        s_nonPlayerCount = 0;
        for (int c = 0; c < s_catalogCount; c++) {
            if (s_catalog[c].entityIdx != s_playerEntityIdx)
                s_nonPlayerCount++;
        }

        // Restore selection to the same entity/gateway/bg, or clamp.
        s_selectedCatalogIdx = 0;
        if (prevSelectedEntity != -2) {
            for (int c = 0; c < s_catalogCount; c++) {
                if (s_catalog[c].entityIdx == prevSelectedEntity &&
                    s_catalog[c].gatewayIdx == prevSelectedGateway) {
                    s_selectedCatalogIdx = c; break;
                }
            }
        }

        if (changed) {
            Log::Field("FieldNavigation: [refresh] catalog: %d entries (%d navigable, %d new entities), player=ent%d",
                       s_catalogCount, s_nonPlayerCount, added, s_playerEntityIdx);
            for (int c = 0; c < s_catalogCount; c++) {
                if (s_catalog[c].entityIdx == s_playerEntityIdx) continue;
                if (s_catalog[c].entityIdx <= -300) {
                    int ji = -(s_catalog[c].entityIdx + 300);
                    if (ji >= 0 && ji < s_jsmEntityCount)
                        Log::Field("FieldNavigation: [refresh]   cat%d JSM ent%d type=%s name='%s' pos=(%d,%d)",
                                   c, ji, EntityTypeName(s_catalog[c].type), s_catalog[c].name,
                                   (int)s_jsmEntities[ji].posX, (int)s_jsmEntities[ji].posY);
                }
                else if (s_catalog[c].entityIdx <= -200) {
                    int ti = -(s_catalog[c].entityIdx + 200);
                    float tcx = (ti < s_capturedLineCount) ? (float)(s_capturedLines[ti].x1 + s_capturedLines[ti].x2) / 2.0f : 0;
                    float tcz = (ti < s_capturedLineCount) ? (float)(s_capturedLines[ti].y1 + s_capturedLines[ti].y2) / 2.0f : 0;
                    Log::Field("FieldNavigation: [refresh]   cat%d TRIGGER line%d center=(%.0f,%.0f) name='%s'",
                               c, ti, tcx, tcz, s_catalog[c].name);
                }
                else
                    Log::Field("FieldNavigation: [refresh]   cat%d ent%d model=%d type=%s name='%s'",
                               c, s_catalog[c].entityIdx, (int)s_catalog[c].modelId,
                               EntityTypeName(s_catalog[c].type), s_catalog[c].name);
            }
        }
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        Log::Field("FieldNavigation: Exception in RefreshCatalog()");
    }
}

// ============================================================================
// v0.08.24: One-shot hex dump of PSHM_W entity-scope functions
// ============================================================================
// Reads raw x86 instruction bytes from the game's .text segment at runtime.
// These are the same bytes the CPU executes — we just log them so we can
// disassemble the parametric curve formula offline.
//
// Target addresses (Steam 2013 en-US, no ASLR):
//   0x00532890 — entity-scope parametric curve subroutine (~300 insns)
//   0x0051C9C0 — type-clamping dispatch (caller of 0x00532890)
