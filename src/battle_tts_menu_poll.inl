// battle_tts_menu_poll.inl — Per-frame menu state-machine polling. Owns the
// monster PollTurnAndCommands function (turn-start/end detection, command
// cursor navigation, submenu entry/exit detection across THREE mechanisms,
// submenu cursor announcements for Magic/GF/Item/Draw, all-target entry/cancel,
// deferred GF cancel) plus PollDeferredTurnAnnounce.
//
// Included LAST in the menu chain from battle_tts_menu.inl. Reads state from
// battle_tts_menu_state.inl, calls builders from battle_tts_menu_lists.inl,
// uses helpers from battle_tts_menu_helpers.inl. Do not compile independently.
//
// v0.16.5: Extracted from the v0.16.4 monolithic battle_tts_menu.inl as
// part of the mechanical .inl split. No behavior change. PollTurnAndCommands
// is kept as a single function because its internal blocks share local state
// (cmdCursorChangedThisFrame, subCursor) and live inside one outer SEH guard;
// splitting them into separate functions would require restructuring that
// risks regressing user-facing menu TTS.


static void PollTurnAndCommands()
{
    if (!s_pActiveCharId) return;
    
    __try {
        uint8_t activeChar = *s_pActiveCharId;
        
        // Turn start: active_char_id transitions to a valid slot
        if (activeChar < 3 && activeChar != s_turnActiveCharId) {
            // v0.13.48: Handle char→char turn transition (no 0xFF gap).
            // If the departing character was commanding a GF, enable HP substitution.
            // The turn-END handler (activeChar==0xFF) normally does this, but char→char
            // transitions skip 0xFF entirely — high GF compatibility makes this common.
            if (s_turnActiveCharId < BATTLE_ALLY_SLOTS && s_submenuCommandId == 0x15) {
                s_gfHpSubstitutionActive[s_turnActiveCharId] = true;
                s_gfAnimFired[s_turnActiveCharId] = false;
                s_gfHpTracking[s_turnActiveCharId] = false;
                s_prevSlotSummoning[s_turnActiveCharId] = false;
                uint8_t lastGFCursor = s_turnSubmenuCursor;
                if (lastGFCursor < s_turnGFCount) {
                    s_gfSummonedIdx[s_turnActiveCharId] = s_turnGFList[lastGFCursor].gfIdx;
                    Log::Battle("BattleTTS: [GF-HP-SUB] Enabled for slot %d (char->char transition, gfIdx=%d '%s')",
                               (int)s_turnActiveCharId, (int)s_turnGFList[lastGFCursor].gfIdx,
                               s_turnGFList[lastGFCursor].name);
                } else {
                    s_gfSummonedIdx[s_turnActiveCharId] = 0xFF;
                    Log::Battle("BattleTTS: [GF-HP-SUB] Enabled for slot %d (char->char transition, cursor=%d out of range)",
                               (int)s_turnActiveCharId, (int)lastGFCursor);
                }
            }
            
            s_turnActiveCharId = activeChar;
            BuildCharCommandList(activeChar);
            
            // v0.12.46: Clear GF HP substitution for this slot — their GF is done
            s_gfHpSubstitutionActive[activeChar] = false;
            s_gfSummonedIdx[activeChar] = 0xFF;  // v0.12.83
            
            // v0.12.77: Suppress any pending GF cancel — new turn starting means
            // the previous GF was confirmed (turn transitioned char→char, not char→0xFF).
            if (s_pendingGFCancel) {
                Log::Battle("BattleTTS: [TARGET-ACTIVE] Pending GF cancel suppressed (new turn started)");
                s_pendingGFCancel = false;
            }
            
            // Reset sub-menu state for new turn
            s_inSubmenu = false;
            s_turnSubmenuCursor = 0xFF;
            s_submenuCommandId = 0;
            s_magicListBuilt = false;
            s_turnMagicCount = 0;
            s_gfListBuilt = false;
            s_turnGFCount = 0;
            s_itemListBuilt = false;
            s_turnItemCount = 0;
            s_drawListBuilt = false;
            s_turnDrawCount = 0;
            s_drawTargetSlot = -1;
            s_drawCursorPrev = 0xFF;
            s_drawStockCastPrev = 0xFF;
            s_drawLastMenuPhase = 0xFF;
            s_drawPhase14Visited = false;  // v0.12.82
            s_pendingSubmenuEntry = false;
            s_pendingSubmenuTick = 0;
            s_prevMenuPhaseForTarget = 0xFF;  // v0.12.56
            s_wasInTargetPhase = false;  // v0.12.61
            s_gfTargetAnnounced = false;  // v0.12.65
            s_prevTargetActive = 0xFF;  // v0.12.66
            s_pendingGFCancel = false;  // v0.12.72
            s_prevSubmenuMode = 0xFE;  // v0.12.79: reset so GF entry transition is detected
            s_submenuOpenByDword = false;  // v0.13.49: reset dword-based submenu detection
            s_submenuDebouncing = true;
            s_submenuDebounceTick = GetTickCount();
            
            // v0.10.97 fix: Snapshot current target bitmask+scope on new turn so
            // PollTargetSelection doesn't see a false "change" from 0 to stale value.
            __try { s_lastTargetBitmask = *(uint8_t*)BATTLE_TARGET_BITMASK; } __except(EXCEPTION_EXECUTE_HANDLER) {}
            __try { s_lastTargetScope = *(uint8_t*)BATTLE_TARGET_SCOPE; } __except(EXCEPTION_EXECUTE_HANDLER) {}
            s_inTargetSelect = false;
            
            // v0.10.22: Check limit toggle byte for initial announcement
            uint8_t initToggle = 0;
            __try { initToggle = *(uint8_t*)BATTLE_LIMIT_TOGGLE; } __except(EXCEPTION_EXECUTE_HANDLER) {}
            s_limitBreakActive = (initToggle == 64);
            s_lastLimitToggle = initToggle;
            
            // Announce "[Name]'s turn. [First command]." (or Limit Break if toggle=64)
            const char* name = GetBattleCharName(activeChar);
            const char* cmd = s_limitBreakActive ? "Limit Break" : GetCommandName(s_turnCharCommands[0]);
            char buf[128];
            snprintf(buf, sizeof(buf), "%s's turn. %s.", name, cmd);

            // v0.13.52: Defer turn TTS if damage is pending/animating/speaking.
            // The EWM cap in battle_tts_ewm.inl prevents most same-frame races,
            // but when the engine sets activeChar on the exact frame an enemy
            // attack lands, we need to order the audio manually: let the damage
            // TTS speak first, then fire the turn announcement. All state setup
            // above still runs (command list, submenu reset, etc.) — only the
            // spoken "X's turn. Y." line is held back. PollDeferredTurnAnnounce()
            // fires it when the damage conditions clear.
            uint8_t engDmgAnimTurn = 0;
            __try { engDmgAnimTurn = *(uint8_t*)0x01D280C0; } __except(EXCEPTION_EXECUTE_HANDLER) {}
            bool turnDamageInFlight = s_ewmHoldForDamageTTS || s_anyHpPending ||
                                      s_damageAnimWasActive || (engDmgAnimTurn != 0);
            if (turnDamageInFlight) {
                strncpy(s_deferredTurnBuf, buf, sizeof(s_deferredTurnBuf) - 1);
                s_deferredTurnBuf[sizeof(s_deferredTurnBuf) - 1] = '\0';
                s_deferredTurnPending = true;
                s_deferredTurnTick = GetTickCount();
                s_deferredTurnChar = activeChar;  // v0.13.53: anchor to this turn
                Log::Battle("BattleTTS: [TURN] Deferred (damage in flight): %s (tts=%d hp=%d anim=%d engAnim=%d)",
                           buf, (int)s_ewmHoldForDamageTTS, (int)s_anyHpPending,
                           (int)s_damageAnimWasActive, (int)engDmgAnimTurn);
            } else {
                BattleSpeak(buf, PRIO_TURN, true);
                Log::Battle("BattleTTS: [TURN] %s (slot %d, limitToggle=%u)", buf, (int)activeChar, (unsigned)initToggle);
            }
            
            // v0.12.52: Snapshot all party magic inventories for Draw validation
            SnapshotAllMagicInventories();
            
            // Set cursor to 0 so we don't re-announce the initial command
            s_turnCmdCursor = 0;
        }
        else if (activeChar == 0xFF && s_turnActiveCharId != 0xFF) {
            // Turn ended
            // v0.12.72: Suppress any pending GF cancel — turn ending means confirm, not cancel
            if (s_pendingGFCancel) {
                Log::Battle("BattleTTS: [TARGET-ACTIVE] Pending GF cancel suppressed (turn ended = confirm)");
                s_pendingGFCancel = false;
            }
            // v0.12.46: If the command was GF, enable HP substitution for this slot
            if (s_submenuCommandId == 0x15 && s_turnActiveCharId < BATTLE_ALLY_SLOTS) {
                s_gfHpSubstitutionActive[s_turnActiveCharId] = true;
                // v0.13.47: Clear animation-fired flag so HP check works for this new summon.
                // entity+0x7C stays non-zero between consecutive summons (stale flag),
                // so the 0->non-zero transition in PollGFSummonState never fires.
                // This is the definitive moment a new GF summon starts for this slot.
                s_gfAnimFired[s_turnActiveCharId] = false;
                s_gfHpTracking[s_turnActiveCharId] = false;
                s_prevSlotSummoning[s_turnActiveCharId] = false;  // v0.13.48: force PollGFSummonState edge re-detection
                // v0.12.83: Record which GF was selected from the submenu list
                uint8_t lastGFCursor = s_turnSubmenuCursor;
                if (lastGFCursor < s_turnGFCount) {
                    s_gfSummonedIdx[s_turnActiveCharId] = s_turnGFList[lastGFCursor].gfIdx;
                    Log::Battle("BattleTTS: [GF-HP-SUB] Enabled for slot %d (GF command confirmed, gfIdx=%d '%s')",
                               (int)s_turnActiveCharId, (int)s_turnGFList[lastGFCursor].gfIdx,
                               s_turnGFList[lastGFCursor].name);
                } else {
                    s_gfSummonedIdx[s_turnActiveCharId] = 0xFF;
                    Log::Battle("BattleTTS: [GF-HP-SUB] Enabled for slot %d (GF command confirmed, cursor=%d out of range)",
                               (int)s_turnActiveCharId, (int)lastGFCursor);
                }
            }
            s_turnActiveCharId = 0xFF;
            s_turnCmdCursor = 0xFF;
            s_inSubmenu = false;
            s_turnSubmenuCursor = 0xFF;
        }
        
        // Command cursor navigation (only while a turn is active)
        if (s_turnActiveCharId < 3) {
            bool cmdCursorChangedThisFrame = false;  // v0.10.112: suppress false submenu entry
            uint8_t cursor = *(uint8_t*)BATTLE_CMD_CURSOR;
            if (cursor < 4 && cursor != s_turnCmdCursor) {
                s_turnCmdCursor = cursor;
                // Returning to command menu from sub-menu
                if (s_inSubmenu) {
                    s_inSubmenu = false;
                    s_turnSubmenuCursor = 0xFF;
                    // Reset tracking so re-entry rebuilds lists and announces items
                    s_drawCursorPrev = 0xFF;
                    s_drawStockCastPrev = 0xFF;
                    s_drawListBuilt = false;
                    s_drawLastMenuPhase = 0xFF;
                    s_drawPhase14Visited = false;
                    s_magicListBuilt = false;
                    s_gfListBuilt = false;
                    s_itemListBuilt = false;
                    Log::Battle("BattleTTS: [SUBMENU] Exited sub-menu, back to command menu");
                }
                // v0.10.112: Suppress false submenu entry on this frame AND capture
                // baseline. Then schedule a delayed forced entry after 150ms so the
                // command name has time to speak before the submenu item queues.
                cmdCursorChangedThisFrame = true;
                __try { s_turnSubmenuCursor = *(uint8_t*)BATTLE_SUBMENU_CURSOR; } __except(EXCEPTION_EXECUTE_HANDLER) {}
                s_pendingSubmenuEntry = false;
                // v0.10.22: cursor=0 may be Attack or Limit Break depending on toggle byte
                const char* cmd;
                if (cursor == 0) {
                    uint8_t toggle = 0;
                    __try { toggle = *(uint8_t*)BATTLE_LIMIT_TOGGLE; } __except(EXCEPTION_EXECUTE_HANDLER) {}
                    s_limitBreakActive = (toggle == 64);
                    s_lastLimitToggle = toggle;
                    cmd = s_limitBreakActive ? "Limit Break" : GetCommandName(s_turnCharCommands[0]);
                } else {
                    cmd = GetCommandName(s_turnCharCommands[cursor]);
                }
                BattleSpeak(cmd, PRIO_MENU, true);
                Log::Battle("BattleTTS: [CMD-NAV] cursor=%d -> %s", (int)cursor, cmd);
            }
            
            // v0.10.19/20: Limit Break toggle detection moved to PollLimitToggleFast()
            
            // v0.10.17: Sub-menu cursor tracking
            // Debounce: ignore sub-menu cursor for 300ms after turn start.
            // The engine resets this byte during turn transitions, causing false
            // sub-menu entry detection (v0.10.17 glitch: "Fire" spoken on cmd menu).
            if (s_submenuDebouncing) {
                if (GetTickCount() - s_submenuDebounceTick > 300) {
                    s_submenuDebouncing = false;
                    // Capture current value as baseline after debounce expires
                    s_turnSubmenuCursor = *(uint8_t*)BATTLE_SUBMENU_CURSOR;
                    Log::Battle("BattleTTS: [SUBMENU] Debounce expired");
                    // v0.13.49: Snapshot submenuMode so the first post-debounce frame
                    // doesn't see a false change from stale per-frame handler writes.
                    __try { s_prevSubmenuMode = *(uint8_t*)0x01D768EB; } __except(EXCEPTION_EXECUTE_HANDLER) {}
                    // v0.13.49: Snapshot dword state so the first post-debounce frame
                    // doesn't see a false transition if the engine hasn't fully settled.
                    {
                        uint32_t dw = 0;
                        __try { dw = *(uint32_t*)BATTLE_MENU_PHASE; } __except(EXCEPTION_EXECUTE_HANDLER) {}
                        s_submenuOpenByDword = (dw >= 0x00400000);
                    }
                }
            }
            
            // v0.12.61: Detect cancel from target selection via pure menuPhase tracking.
            // Attack target = phase 1, Draw target = phase 3.
            // Cancel exits to phase 5 or 6. Confirm exits to phase 7 (execute).
            // No dependency on s_inTargetSelect or target bitmask.
            {
                uint8_t curPhase = 0xFF;
                __try { curPhase = *(uint8_t*)BATTLE_MENU_PHASE; } __except(EXCEPTION_EXECUTE_HANDLER) {}
                bool isTargetPhase = (curPhase == 1 || curPhase == 3);
                
                if (isTargetPhase && !s_wasInTargetPhase) {
                    s_wasInTargetPhase = true;
                    Log::Battle("BattleTTS: [TARGET] Entered target phase %u", (unsigned)curPhase);
                    // v0.12.62: Force-announce current target on entry.
                    // PollTargetSelection won't fire if mask hasn't changed since last time.
                    uint8_t entryMask = 0, entryScope = 0;
                    __try { entryMask = *(uint8_t*)BATTLE_TARGET_BITMASK; } __except(EXCEPTION_EXECUTE_HANDLER) {}
                    __try { entryScope = *(uint8_t*)BATTLE_TARGET_SCOPE; } __except(EXCEPTION_EXECUTE_HANDLER) {}
                    if (entryMask != 0) {
                        bool isAll = (entryScope != 3 && entryScope != 0 && CountBits(entryMask) > 1);
                        char tgtBuf[128];
                        if (isAll) {
                            int slot = BitmaskToSlot(entryMask);
                            snprintf(tgtBuf, sizeof(tgtBuf), "%s",
                                     (slot >= BATTLE_ALLY_SLOTS) ? "All enemies" : "All allies");
                        } else if (CountBits(entryMask) == 1) {
                            int slot = BitmaskToSlot(entryMask);
                            if (slot >= 0) {
                                char nameBuf[64];
                                snprintf(tgtBuf, sizeof(tgtBuf), "%s", GetSlotName(slot, nameBuf, sizeof(nameBuf)));
                                // v0.13.63: Append active statuses for enemy targets.
                                if (slot >= BATTLE_ALLY_SLOTS) {
                                    char _st[160];
                                    if (BuildStatusString(slot, _st, sizeof(_st)) > 0) {
                                        size_t n = strlen(tgtBuf);
                                        snprintf(tgtBuf + n, sizeof(tgtBuf) - n, ", %s", _st);
                                    }
                                }
                            } else { tgtBuf[0] = '\0'; }
                        } else {
                            bool hasEn = (entryMask & 0x78) != 0;
                            bool hasAl = (entryMask & 0x07) != 0;
                            snprintf(tgtBuf, sizeof(tgtBuf), "%s",
                                     (hasEn && !hasAl) ? "All enemies" : (hasAl && !hasEn) ? "All allies" : "All targets");
                        }
                        if (tgtBuf[0] != '\0') {
                            BattleSpeak(tgtBuf, PRIO_MENU, true);
                            Log::Battle("BattleTTS: [TARGET] Entry announce: %s (mask=0x%02X scope=%u)",
                                       tgtBuf, (unsigned)entryMask, (unsigned)entryScope);
                        }
                        // Sync PollTargetSelection tracking so it doesn't double-announce
                        s_lastTargetBitmask = entryMask;
                        s_lastTargetScope = entryScope;
                        s_inTargetSelect = true;
                    }
                }
                
                if (s_wasInTargetPhase && !isTargetPhase) {
                    s_wasInTargetPhase = false;
                    // Only announce on cancel destinations (phase 5=cmd menu, 6=intermediate).
                    // Phase 7=execute, 14=Draw spell list, 0xFF=unknown — don't announce.
                    bool isCancelPhase = (curPhase == 5 || curPhase == 6);
                    if (isCancelPhase && s_turnCmdCursor < 4) {
                        const char* cmd;
                        if (s_turnCmdCursor == 0) {
                            uint8_t toggle = 0;
                            __try { toggle = *(uint8_t*)BATTLE_LIMIT_TOGGLE; } __except(EXCEPTION_EXECUTE_HANDLER) {}
                            cmd = (s_limitBreakActive || toggle == 64) ? "Limit Break" : GetCommandName(s_turnCharCommands[0]);
                        } else {
                            cmd = GetCommandName(s_turnCharCommands[s_turnCmdCursor]);
                        }
                        BattleSpeak(cmd, PRIO_MENU, true);
                        Log::Battle("BattleTTS: [TARGET-EXIT] Phase %u->%u, cancelled, announcing: %s",
                                   (unsigned)s_prevMenuPhaseForTarget, (unsigned)curPhase, cmd);
                    }
                    // Sync 0x9D tracker so TARGET-ACTIVE handler doesn't double-announce
                    __try { s_prevTargetActive = *(uint8_t*)0x01D7689D; } __except(EXCEPTION_EXECUTE_HANDLER) {}
                }
                
                // v0.12.63: Detect Draw spell list entry (phase 14).
                // When phase transitions to 14, force-announce the current draw spell
                // and reset draw cursor tracking so navigation works immediately.
                if (curPhase == 14 && s_prevMenuPhaseForTarget != 14) {
                    // v0.12.82: Only reset draw cursor on RE-ENTRY to phase 14.
                    // s_drawPhase14Visited is false on first visit (set at submenu
                    // entry and turn start), true after. This avoids the double-
                    // announce where the draw cursor poll fires before this handler.
                    if (s_drawPhase14Visited) {
                        s_drawCursorPrev = 0xFF;
                    }
                    s_drawPhase14Visited = true;
                    s_drawStockCastPrev = 0xFF;
                    Log::Battle("BattleTTS: [DRAW] Entered spell list (phase %u->14, revisit=%d)",
                               (unsigned)s_prevMenuPhaseForTarget, (int)(s_drawPhase14Visited));
                }
                
                s_prevMenuPhaseForTarget = curPhase;
            }
            
            uint8_t subCursor = *(uint8_t*)BATTLE_SUBMENU_CURSOR;

            // v0.12.34: Detect submenu entry/exit via 0x01D768EB.
            // This byte is the engine's "submenu mode" indicator:
            //   0xFE = command menu (arrow keys control command cursor)
            //   0x02 = Magic/GF submenu (arrow keys control spell/GF cursor)
            // Discovered via F12 battle state snapshot diagnostic (session 46).
            {
                static const uint32_t SUBMENU_MODE_ADDR = 0x01D768EB;
                uint8_t sm = 0xFF;
                __try { sm = *(uint8_t*)SUBMENU_MODE_ADDR; } __except(EXCEPTION_EXECUTE_HANDLER) {}
                // s_prevSubmenuMode is file-scoped (v0.12.75) for cross-block access
                // v0.13.49: Removed the wasCommandMenu requirement. The old check
                // only recognized 0xFE and 0x00 as "command menu" values, but
                // submenuMode is actually the ACTIVE PARTY SLOT INDEX for input
                // routing (0/1/2 = submenu for that slot, 0xFE = command cursor).
                // Stale per-frame handler writes from the previous turn can leave
                // it at a non-0xFE/non-0x00 value even when we're on the command menu.
                // The correct condition: if we're NOT in a submenu (!s_inSubmenu)
                // and the mode just changed to a submenu value, that's an entry.
                bool nowInSubmenu = (sm != 0xFE && sm != 0x00);
                bool nowCommandMenu = (sm == 0xFE);
                // v0.12.72: When sm transitions TO 0x00, check menuPhase as fallback.
                // Engine sometimes uses 0x00 instead of 0x01/0x02 for submenus.
                // Only check on transitions to avoid per-frame spam.
                if (sm == 0x00 && sm != s_prevSubmenuMode) {
                    uint8_t fallbackPhase = 0xFF;
                    __try { fallbackPhase = *(uint8_t*)BATTLE_MENU_PHASE; } __except(EXCEPTION_EXECUTE_HANDLER) {}
                    if (fallbackPhase == 32 || fallbackPhase == 80) {
                        nowInSubmenu = true;
                        nowCommandMenu = false;
                        Log::Battle("BattleTTS: [SUBMENU] Mode 0x%02X->0x00 + phase %u -> treating as submenu entry",
                                   (unsigned)s_prevSubmenuMode, (unsigned)fallbackPhase);
                    } else {
                        // 0x00 with non-submenu phase = command menu
                        nowCommandMenu = true;
                        nowInSubmenu = false;
                    }
                } else if (sm == 0x00 && sm == s_prevSubmenuMode) {
                    // Steady state 0x00 — no transition, don't change anything
                    nowInSubmenu = false;
                    nowCommandMenu = false;  // prevent false exit detection
                }
                if (sm != s_prevSubmenuMode) {
                    if (nowInSubmenu && !s_inSubmenu && !s_submenuDebouncing) {
                        // v0.13.49: Use shared helper for consistent entry across all paths.
                        if (s_turnCmdCursor < 4) {
                            char modeSrc[64];
                            snprintf(modeSrc, sizeof(modeSrc), "submenu mode 0x%02X->0x%02X",
                                     (unsigned)s_prevSubmenuMode, (unsigned)sm);
                            EnterSubmenu(s_turnCharCommands[s_turnCmdCursor], modeSrc);
                        }
                    } else if (nowCommandMenu && s_inSubmenu) {
                        // v0.12.75: Simplified exit check — if mod knows we're in submenu
                        // and engine says command menu, that's an exit. Previous check
                        // (!wasCommandMenu) failed when prevMode was 0x00.
                        // v0.13.50: EXCEPTION: Draw (0x16) has a complex multi-phase flow
                        // where submenuMode briefly flips to 0xFE during internal phase
                        // transitions (target → spell list → Stock/Cast). This causes false
                        // exits. For Draw, we rely on the cmdCursor change handler for exit
                        // detection instead (user presses cancel → cursor returns to cmd menu).
                        // v0.13.50 / v0.14.38: EXCEPTION for Draw (0x16) and Item (0x17).
                        // Both submenus exhibit transient submenuMode 0x01->0xFE flips during
                        // normal in-submenu navigation, NOT real exits.
                        //   Draw (v0.13.50): multi-phase flow (target -> spell list -> Stock/Cast)
                        //     causes the engine to briefly drop submenuMode during phase
                        //     transitions.
                        //   Item (v0.14.38): every cursor up/down within the submenu causes
                        //     submenuMode to flip 0x01->0xFE for ~1 frame and back to 0x01.
                        //     v0.14.37 BAT proved this: per-item BattleSpeak was firing
                        //     correctly with the right text, but ~10ms later this block fired
                        //     a spurious "Item" announce with interrupt=true, purging the
                        //     in-progress per-item speech via SAPI's PURGEBEFORESPEAK. Aaron
                        //     only ever heard the "Item" exit announce.
                        // For both, we rely on the cmdCursor change handler for real exit
                        // detection (user presses cancel -> cursor returns to cmd menu).
                        if (s_submenuCommandId == 0x16 || s_submenuCommandId == 0x17) {
                            Log::Battle("BattleTTS: [SUBMENU] Suppressed false exit for %s (mode 0x%02X->0xFE, transient cursor flip)",
                                       GetCommandName(s_submenuCommandId),
                                       (unsigned)s_prevSubmenuMode);
                        } else {
                            // Returning to command menu — announce current command
                            uint8_t exitCmd = s_submenuCommandId;
                            s_inSubmenu = false;
                            s_magicListBuilt = false;
                            s_gfListBuilt = false;
                            s_itemListBuilt = false;
                            s_gfTargetAnnounced = false;  // v0.12.65
                            s_drawCursorPrev = 0xFF;
                            s_drawStockCastPrev = 0xFF;
                            s_drawListBuilt = false;
                            s_drawLastMenuPhase = 0xFF;
                            Log::Battle("BattleTTS: [SUBMENU] Exit detected via submenu mode 0x%02X->0xFE",
                                       (unsigned)s_prevSubmenuMode);
                            // Announce the command we're returning to
                            if (s_turnCmdCursor < 4) {
                                const char* cmd = GetCommandName(s_turnCharCommands[s_turnCmdCursor]);
                                BattleSpeak(cmd, PRIO_MENU, true);
                                Log::Battle("BattleTTS: [SUBMENU] Exit announce: %s (cursor=%d, was cmd 0x%02X)",
                                           cmd, (int)s_turnCmdCursor, (unsigned)exitCmd);
                            }
                            // Sync menuPhase tracker so target-exit doesn't also fire
                            __try { s_prevMenuPhaseForTarget = *(uint8_t*)BATTLE_MENU_PHASE; } __except(EXCEPTION_EXECUTE_HANDLER) {}
                        }
                    }
                }
                s_prevSubmenuMode = sm;
            }

            // v0.12.73: MenuPhase byte fallback removed in v0.13.49. Phase 80 is
            // ambiguous (command menu AND submenu). Superseded by dword detection.
            // v0.13.49: Definitive submenu detection via menuPhase DWORD.
            // When the engine opens a submenu, 0x1D768D0 transitions from a small
            // phase number (0-43) to a FUNCTION POINTER (0x004XXXXX range).
            // Reading as uint32 and checking > 0xFF is unambiguous.
            // Discovered via disassembly: submenu handler at 0x4FDD90 executes
            // `call dword ptr [0x1d768d0]` — treating the value as a code pointer.
            {
                uint32_t menuDword = 0;
                __try { menuDword = *(uint32_t*)BATTLE_MENU_PHASE; } __except(EXCEPTION_EXECUTE_HANDLER) {}
                bool submenuNowOpen = (menuDword >= 0x00400000);
                if (submenuNowOpen && !s_submenuOpenByDword && !s_submenuDebouncing &&
                    !s_inSubmenu && s_turnCmdCursor < 4) {
                    uint8_t cmdAtCursor = s_turnCharCommands[s_turnCmdCursor];
                    if (cmdAtCursor == 0x14 || cmdAtCursor == 0x15 || cmdAtCursor == 0x16 || cmdAtCursor == 0x17) {
                        char dwordSrc[64];
                        snprintf(dwordSrc, sizeof(dwordSrc), "menuPhase dword 0x%08X", menuDword);
                        EnterSubmenu(cmdAtCursor, dwordSrc);
                    }
                }
                // Only track state after debounce — during debounce the dword may
                // still hold a stale function pointer from the previous turn's submenu,
                // which would poison the flag and suppress the real transition later.
                if (!s_submenuDebouncing)
                    s_submenuOpenByDword = submenuNowOpen;
            }
            if (!s_submenuDebouncing && !cmdCursorChangedThisFrame && subCursor != s_turnSubmenuCursor) {
                // v0.14.14: Suppress the regular command-submenu path while a Limit
                // Break submenu is open. The regular handler classifies subCursor
                // changes by `s_turnCharCommands[s_turnCmdCursor]`, which for
                // cmdCursor=0 always resolves to Attack (0x01) — that's wrong
                // when the engine is actually in a limit submenu (e.g. Quistis'
                // Blue Magic spell list). The limit-submenu path in PollLimitDiag
                // owns the announcement for those cursor moves.
                if (s_inLimitSubmenu) {
                    s_turnSubmenuCursor = subCursor;
                } else
                if (!s_inSubmenu && s_turnCmdCursor < 4) {
                    // Entering sub-menu — use shared helper
                    EnterSubmenu(s_turnCharCommands[s_turnCmdCursor], "subCursor change");
                }
                
                s_turnSubmenuCursor = subCursor;
                
                // Announce the current sub-menu item
                if (s_inSubmenu) {
                    if (s_submenuCommandId == 0x14 && s_magicListBuilt) {
                        // Magic sub-menu: read spell at cursor position
                        // Cursor 0-3 is visible position. With <=4 spells, maps directly.
                        // TODO: handle scroll offset for >4 spells (need to find page offset byte)
                        if ((int)subCursor < s_turnMagicCount) {
                            const char* spellName = GetMagicName(s_turnMagicList[subCursor].id);
                            int qty = (int)s_turnMagicList[subCursor].qty;
                            char buf[128];
                            snprintf(buf, sizeof(buf), "%s, %d", spellName, qty);
                            BattleSpeak(buf, PRIO_MENU, true);
                            Log::Battle("BattleTTS: [SUBMENU-NAV] Magic cursor=%d -> %s x%d (id=0x%02X)",
                                       (int)subCursor, spellName, qty,
                                       (unsigned)s_turnMagicList[subCursor].id);
                        } else {
                            Log::Battle("BattleTTS: [SUBMENU-NAV] Magic cursor=%d out of range (count=%d)",
                                       (int)subCursor, s_turnMagicCount);
                        }
                    } else if (s_submenuCommandId == 0x15 && s_gfListBuilt) {
                        // v0.10.98: GF sub-menu — announce junctioned GF at cursor position
                        // v0.13.46: Clamp cursor to valid range — stale cursor from previous
                        // character's GF selection can be out of range for this character.
                        // v0.13.49: Announce GF name or "Empty GF slot" based on cursor position.
                        // Cursor positions >= s_turnGFCount represent empty/unjunctioned slots.
                        int gfCur = (int)subCursor;
                        if (gfCur < s_turnGFCount) {
                            const char* gfName = s_turnGFList[gfCur].name;
                            char buf[128];
                            snprintf(buf, sizeof(buf), "%s", gfName);
                            BattleSpeak(buf, PRIO_MENU, true);
                            Log::Battle("BattleTTS: [SUBMENU-NAV] GF cursor=%d -> %s (gfIdx=%d)",
                                       gfCur, gfName, (int)s_turnGFList[gfCur].gfIdx);
                        } else {
                            BattleSpeak("Empty GF slot", PRIO_MENU, true);
                            Log::Battle("BattleTTS: [SUBMENU-NAV] GF cursor=%d -> Empty GF slot (count=%d)",
                                       (int)subCursor, s_turnGFCount);
                        }
                    } else if (s_submenuCommandId == 0x17) {
                        // v0.14.42: Item sub-menu announce.
                        // Read directly from the engine's battle items buffer at
                        // 0x1D28E78 + cursor * 5. The cursor is the visible position;
                        // empty buffer entries (id=0 or qty=0) announce as "Empty".
                        int sc = (int)subCursor;
                        uint8_t bufId = 0, bufQty = 0;
                        bool bufOk = ReadBattleItemEntry(sc, &bufId, &bufQty);
                        int page = 0, itemNum = 0;
                        GetItemVisualPos(sc, &page, &itemNum);

                        if (bufOk && bufId >= 1 && bufId < 33 && bufQty > 0) {
                            const char* itemName = GetBattleItemName(bufId);
                            char buf[128];
                            snprintf(buf, sizeof(buf), "%s, quantity %d, page %d, item %d",
                                     itemName, (int)bufQty, page, itemNum);
                            BattleSpeak(buf, PRIO_MENU, true);
                            Log::Battle("BattleTTS: [ITEM] cursor=%d -> %s x%d page%d item%d (id=%u src=battle_buffer)",
                                       sc, itemName, (int)bufQty, page, itemNum, (unsigned)bufId);
                        } else {
                            char buf[64];
                            snprintf(buf, sizeof(buf), "Empty, page %d, item %d", page, itemNum);
                            BattleSpeak(buf, PRIO_MENU, true);
                            Log::Battle("BattleTTS: [ITEM] cursor=%d -> Empty page%d item%d (bufOk=%d id=%u qty=%u)",
                                       sc, page, itemNum, (int)bufOk, (unsigned)bufId, (unsigned)bufQty);
                        }
                    } else if (s_submenuCommandId == 0x16 && s_drawListBuilt) {
                        // v0.10.112: Draw sub-menu — generic subCursor fires on phase transitions,
                        // NOT during active navigation. All draw spell announces go through the
                        // draw-specific cursor poll at 0x01D768D8 below. Log only here.
                        Log::Battle("BattleTTS: [DRAW-NAV] generic subCursor=%d (ignored, handled by draw poll)",
                                   (int)subCursor);
                    } else {
                        // Other sub-menus — log for diagnostic
                        Log::Battle("BattleTTS: [SUBMENU-NAV] cmd=0x%02X cursor=%d (unhandled)",
                                   (unsigned)s_submenuCommandId, (int)subCursor);
                    }
                }
            }
            
            // v0.10.112: Delayed submenu entry after command scroll.
            // 150ms after scrolling to a new command, force-enter the submenu and
            // announce the current item with interrupt=false (queued after command name).
            if (s_pendingSubmenuEntry && !s_inSubmenu && 
                GetTickCount() - s_pendingSubmenuTick > 150) {
                s_pendingSubmenuEntry = false;
                if (s_turnCmdCursor < 4) {
                    uint8_t sc = 0;
                    __try { sc = *(uint8_t*)BATTLE_SUBMENU_CURSOR; } __except(EXCEPTION_EXECUTE_HANDLER) {}
                    EnterSubmenu(s_turnCharCommands[s_turnCmdCursor], "delayed entry");
                    s_turnSubmenuCursor = sc;
                    
                    // Announce current item (queued, not interrupting command name)
                    if (s_submenuCommandId == 0x14 && s_magicListBuilt) {
                        if ((int)sc < s_turnMagicCount) {
                            const char* spellName = GetMagicName(s_turnMagicList[sc].id);
                            int qty = (int)s_turnMagicList[sc].qty;
                            char buf[128];
                            snprintf(buf, sizeof(buf), "%s, %d", spellName, qty);
                            BattleSpeak(buf, PRIO_MENU, false);
                            Log::Battle("BattleTTS: [SUBMENU-DELAYED] Magic cursor=%d -> %s x%d",
                                       (int)sc, spellName, qty);
                        }
                    } else if (s_submenuCommandId == 0x15 && s_gfListBuilt) {
                        if ((int)sc < s_turnGFCount) {
                            BattleSpeak(s_turnGFList[sc].name, PRIO_MENU, false);
                            Log::Battle("BattleTTS: [SUBMENU-DELAYED] GF cursor=%d -> %s",
                                       (int)sc, s_turnGFList[sc].name);
                        } else {
                            BattleSpeak("Empty GF slot", PRIO_MENU, false);
                            Log::Battle("BattleTTS: [SUBMENU-DELAYED] GF cursor=%d -> Empty GF slot (count=%d)",
                                       (int)sc, s_turnGFCount);
                        }
                    } else if (s_submenuCommandId == 0x17 && s_itemListBuilt) {
                        // v0.14.42: Read directly from battle items buffer.
                        uint8_t bufId = 0, bufQty = 0;
                        bool bufOk = ReadBattleItemEntry((int)sc, &bufId, &bufQty);
                        if (bufOk && bufId >= 1 && bufId < 33 && bufQty > 0) {
                            const char* itemName = GetBattleItemName(bufId);
                            char buf[128];
                            int page = 0, itemNum = 0;
                            GetItemVisualPos((int)sc, &page, &itemNum);
                            snprintf(buf, sizeof(buf), "%s, quantity %d, page %d, item %d",
                                     itemName, (int)bufQty, page, itemNum);
                            BattleSpeak(buf, PRIO_MENU, false);
                            Log::Battle("BattleTTS: [SUBMENU-DELAYED] Item cursor=%d -> %s x%d page%d item%d",
                                       (int)sc, itemName, (int)bufQty, page, itemNum);
                        }
                    }
                    // Draw and Item are handled by their dedicated poll blocks below
                }
            }
            
            // v0.10.109: Draw-specific cursor poll.
            // Draw uses a SEPARATE cursor byte (0x01D768D8) from other sub-menus.
            // 0x01D768EC only fires during engine init/phase transitions, NOT during
            // active up/down navigation. We poll 0x01D768D8 independently here.
            // NOTE: Also retry BuildDrawList if it hasn't succeeded yet — the target
            // bitmask at 0x01D76884 may not be set until after target confirmation,
            // which happens AFTER the submenu entry event fires.
            if (s_inSubmenu && s_submenuCommandId == 0x16 && !s_drawListBuilt) {
                BuildDrawList();  // retry until target bitmask is populated
            }
            if (s_inSubmenu && s_submenuCommandId == 0x16 && s_drawListBuilt) {
                // v0.10.112: Keep drawer slot updated every frame while draw submenu is open
                if (s_turnActiveCharId < 3)
                    s_lastDrawerPartySlot = s_turnActiveCharId;
                
                // v0.10.112: Detect menuPhase transitions to reset cursor tracking.
                // When canceling from Stock/Cast (phase 23) back to spell list (phase 14),
                // reset draw cursor prev so the current spell re-announces.
                // When canceling from spell list back to target select, reset everything.
                uint8_t drawPhaseNow = 0;
                __try { drawPhaseNow = *(uint8_t*)0x01D768D0; } __except(EXCEPTION_EXECUTE_HANDLER) {}
                if (s_drawLastMenuPhase != 0xFF && drawPhaseNow != s_drawLastMenuPhase) {
                    if (s_drawLastMenuPhase == 23 && drawPhaseNow < 23) {
                        // Left Stock/Cast prompt backward → back to spell list
                        s_drawCursorPrev = 0xFF;
                        s_drawStockCastPrev = 0xFF;
                        Log::Battle("BattleTTS: [DRAW] Phase %u->%u: reset cursor tracking (back to spell list)",
                                   (unsigned)s_drawLastMenuPhase, (unsigned)drawPhaseNow);
                    }
                    if (s_drawLastMenuPhase == 14 && drawPhaseNow < 14) {
                        // Left spell list backward → back to target selection
                        s_drawCursorPrev = 0xFF;
                        s_drawStockCastPrev = 0xFF;
                        s_drawListBuilt = false;  // force rebuild with potentially new target
                        s_lastTargetBitmask = 0;  // force target re-announce
                        s_lastTargetScope = 0;
                        Log::Battle("BattleTTS: [DRAW] Phase %u->%u: reset target+draw tracking (back to target select)",
                                   (unsigned)s_drawLastMenuPhase, (unsigned)drawPhaseNow);
                    }
                }
                s_drawLastMenuPhase = drawPhaseNow;
                
                uint8_t drawCur = 0xFF;
                __try { drawCur = *(uint8_t*)DRAW_CURSOR_ADDR; } __except(EXCEPTION_EXECUTE_HANDLER) {}
                if (drawCur != s_drawCursorPrev && drawCur < DRAW_SLOTS_PER_ENEMY) {
                    s_drawCursorPrev = drawCur;
                    uint8_t mid = s_turnDrawList[drawCur].magicId;
                    if (mid != 0) {
                        char drawNameBuf[64];
                        const char* drawName = GetDrawEntryName(mid, drawNameBuf, sizeof(drawNameBuf));
                        char buf[128];
                        // v0.13.46: Prefix GF entries with "GF: " to distinguish from spells
                        if (mid >= 0x40) {
                            snprintf(buf, sizeof(buf), "GF: %s", drawName);
                        } else {
                            snprintf(buf, sizeof(buf), "%s", drawName);
                        }
                        BattleSpeak(buf, PRIO_MENU, true);
                        Log::Battle("BattleTTS: [DRAW-CUR] draw_cursor=%d -> %s (id=%u%s)",
                                   (int)drawCur, drawName, (unsigned)mid,
                                   (mid >= 0x40) ? " GF" : "");
                    } else {
                        BattleSpeak("Empty", PRIO_MENU, true);
                        Log::Battle("BattleTTS: [DRAW-CUR] draw_cursor=%d -> Empty", (int)drawCur);
                    }
                } else if (drawCur != s_drawCursorPrev && drawCur != 0xFF) {
                    s_drawCursorPrev = drawCur;  // out of range, track but don't announce
                }
                // v0.10.111: Stock/Cast cursor at 0x01D768D9 (0=Stock, 1=Cast)
                // v0.10.112: Only poll during Stock/Cast phase (menuPhase == 23).
                // menuPhase=14 is the spell list; Stock/Cast prompt is specifically at 23.
                // Without this guard, D9=0 (stale) triggers false "Stock" during spell list.
                uint8_t drawMenuPhase = 0;
                __try { drawMenuPhase = *(uint8_t*)0x01D768D0; } __except(EXCEPTION_EXECUTE_HANDLER) {}
                if (drawMenuPhase == 23) {
                    uint8_t stockCast = 0xFF;
                    __try { stockCast = *(uint8_t*)DRAW_STOCK_CAST_ADDR; } __except(EXCEPTION_EXECUTE_HANDLER) {}
                    if (stockCast != s_drawStockCastPrev && stockCast <= 1) {
                        s_drawStockCastPrev = stockCast;
                        const char* actionName = (stockCast == 0) ? "Stock" : "Cast";
                        BattleSpeak(actionName, PRIO_MENU, true);
                        Log::Battle("BattleTTS: [DRAW-ACTION] Stock/Cast cursor=%u -> %s (phase=%u)",
                                   (unsigned)stockCast, actionName, (unsigned)drawMenuPhase);
                    }
                }
            }
            
            // v0.12.72: Process deferred GF cancel.
            // If 150ms pass without turn ending, it's a real cancel.
            if (s_pendingGFCancel && GetTickCount() - s_pendingGFCancelTick > 150) {
                s_pendingGFCancel = false;
                BattleSpeak(s_pendingGFCancelName, PRIO_MENU, true);
                Log::Battle("BattleTTS: [TARGET-ACTIVE] Deferred GF cancel confirmed, announcing: %s",
                           s_pendingGFCancelName);
            }

            // v0.12.66: All-target entry detection via 0x01D7689D transition.
            // GF target doesn't use menuPhase 1 or 3, so phase-based entry
            // doesn't work. Watch target-active byte 0→1 transition instead.
            // Gated by !s_wasInTargetPhase to avoid double-announcing Attack/Draw.
            {
                uint8_t tgtAct = 0;
                __try { tgtAct = *(uint8_t*)0x01D7689D; } __except(EXCEPTION_EXECUTE_HANDLER) {}
                if (tgtAct == 1 && s_prevTargetActive == 0 && !s_wasInTargetPhase) {
                    uint8_t aMask = 0, aScope = 0;
                    __try { aMask = *(uint8_t*)BATTLE_TARGET_BITMASK; } __except(EXCEPTION_EXECUTE_HANDLER) {}
                    __try { aScope = *(uint8_t*)BATTLE_TARGET_SCOPE; } __except(EXCEPTION_EXECUTE_HANDLER) {}
                    if (aMask != 0) {
                        // v0.13.46: If this is a GF command and we never entered the submenu
                        // (detection was missed), build the GF list now and announce the GF name.
                        if (s_turnCmdCursor < 4 && s_turnCharCommands[s_turnCmdCursor] == 0x15 && !s_gfListBuilt) {
                            BuildGFList(s_turnActiveCharId);
                            s_submenuCommandId = 0x15;
                            s_inSubmenu = true;
                            if (s_turnGFCount > 0) {
                                // v0.13.46: Read actual submenu cursor instead of hardcoding 0
                                uint8_t subCur = 0;
                                __try { subCur = *(uint8_t*)BATTLE_SUBMENU_CURSOR; } __except(EXCEPTION_EXECUTE_HANDLER) {}
                                if (subCur >= s_turnGFCount) subCur = 0;
                                const char* gfName = s_turnGFList[subCur].name;
                                char preBuf[128];
                                snprintf(preBuf, sizeof(preBuf), "%s", gfName);
                                BattleSpeak(preBuf, PRIO_MENU, true);
                                Log::Battle("BattleTTS: [TARGET-ACTIVE] GF submenu missed — built GF list, announcing: %s (cursor=%u)",
                                           gfName, (unsigned)subCur);
                            }
                        }
                        // For non-phase targets (GF), scope alone determines targeting:
                        // scope=1 = All enemies, scope=2 = All allies (mask is just cursor anchor)
                        char tgtBuf[128];
                        if (aScope == 1) {
                            snprintf(tgtBuf, sizeof(tgtBuf), "All enemies");
                        } else if (aScope == 2) {
                            snprintf(tgtBuf, sizeof(tgtBuf), "All allies");
                        } else if (CountBits(aMask) == 1) {
                            int slot = BitmaskToSlot(aMask);
                            char nameBuf[64];
                            snprintf(tgtBuf, sizeof(tgtBuf), "%s",
                                     (slot >= 0) ? GetSlotName(slot, nameBuf, sizeof(nameBuf)) : "???");
                            // v0.13.63: Append active statuses for enemy targets.
                            if (slot >= BATTLE_ALLY_SLOTS) {
                                char _st[160];
                                if (BuildStatusString(slot, _st, sizeof(_st)) > 0) {
                                    size_t n = strlen(tgtBuf);
                                    snprintf(tgtBuf + n, sizeof(tgtBuf) - n, ", %s", _st);
                                }
                            }
                        } else {
                            snprintf(tgtBuf, sizeof(tgtBuf), "All targets");
                        }
                        BattleSpeak(tgtBuf, PRIO_MENU, true);
                        Log::Battle("BattleTTS: [TARGET-ACTIVE] 0x9D 0->1: %s (mask=0x%02X scope=%u)",
                                   tgtBuf, (unsigned)aMask, (unsigned)aScope);
                        s_lastTargetBitmask = aMask;
                        s_lastTargetScope = aScope;
                        s_inTargetSelect = true;
                    }
                }
                // v0.12.67/72: Detect GF target cancel via 0x9D going 1->0.
                // For GF command, defer the cancel announce by 150ms.
                // If the turn ends within that window (activeChar→0xFF), it was
                // a confirm, not a cancel, and the pending announce is suppressed.
                if (tgtAct == 0 && s_prevTargetActive == 1 && !s_wasInTargetPhase &&
                    s_inTargetSelect && s_turnActiveCharId < 3) {
                    uint8_t cancelPhase = 0xFF;
                    __try { cancelPhase = *(uint8_t*)BATTLE_MENU_PHASE; } __except(EXCEPTION_EXECUTE_HANDLER) {}
                    bool isDrawProgression = (cancelPhase == 11 || cancelPhase == 12 ||
                                              cancelPhase == 14 || cancelPhase == 21 ||
                                              cancelPhase == 23 || cancelPhase == 28 ||
                                              cancelPhase == 33 || cancelPhase == 34);
                    s_inTargetSelect = false;
                    if (!isDrawProgression && s_turnCmdCursor < 4) {
                        if (s_submenuCommandId == 0x15 && s_gfListBuilt && s_turnGFCount > 0) {
                            // v0.12.72: Defer GF cancel — might be confirm
                            uint8_t gfCur = 0;
                            __try { gfCur = *(uint8_t*)BATTLE_SUBMENU_CURSOR; } __except(EXCEPTION_EXECUTE_HANDLER) {}
                            const char* gfName = (gfCur < s_turnGFCount) ? s_turnGFList[gfCur].name : s_turnGFList[0].name;
                            strncpy(s_pendingGFCancelName, gfName, sizeof(s_pendingGFCancelName) - 1);
                            s_pendingGFCancelName[sizeof(s_pendingGFCancelName) - 1] = '\0';
                            s_pendingGFCancel = true;
                            s_pendingGFCancelTick = GetTickCount();
                            Log::Battle("BattleTTS: [TARGET-ACTIVE] 0x9D 1->0: GF cancel deferred for: %s", gfName);
                        } else if (s_inSubmenu && s_submenuCommandId == 0x14) {
                            // v0.12.80: Magic target cancel — returns to spell list.
                            // Reset cursor tracking so current spell re-announces.
                            s_turnSubmenuCursor = 0xFF;
                            Log::Battle("BattleTTS: [TARGET-ACTIVE] 0x9D 1->0: Magic target cancel, reset spell cursor");
                        } else if (s_inSubmenu && s_submenuCommandId == 0x16) {
                            // v0.12.81: Draw target cancel — returns to Stock/Cast.
                            // Only reset Stock/Cast tracking, NOT spell cursor.
                            // Resetting drawCursorPrev causes a false spell announce
                            // when the user is at the Stock/Cast prompt.
                            s_drawStockCastPrev = 0xFF;
                            Log::Battle("BattleTTS: [TARGET-ACTIVE] 0x9D 1->0: Draw target cancel, reset Stock/Cast cursor");
                        } else {
                            const char* cmd = GetCommandName(s_turnCharCommands[s_turnCmdCursor]);
                            BattleSpeak(cmd, PRIO_MENU, true);
                            Log::Battle("BattleTTS: [TARGET-ACTIVE] 0x9D 1->0: cancelled, announcing: %s", cmd);
                        }
                    }
                }
                s_prevTargetActive = tgtAct;
            }
        }

    } __except (EXCEPTION_EXECUTE_HANDLER) {}
}

// ============================================================================
// v0.13.52: Deferred turn announcement poll.
// Called from battle_tts.cpp Update() after PollHPChanges so any damage TTS
// raised this frame is reflected in s_ewmHoldForDamageTTS before we decide
// whether to fire.
//
// v0.13.53 changes:
//   1. Cancel the pending TTS if activeChar no longer matches what it was
//      at defer time. Damage TTS can hold long enough for the player to
//      finish their entire action; firing "X's turn. Attack." during X+1's
//      turn (or during the gap) is worse than silently dropping it.
//   2. Drop the ScreenReader::IsSpeaking() gate. It was causing indefinite
//      deferrals when the user was actively navigating menus — SAPI was
//      never idle, the TTS never fired, and ended up ~4 seconds stale.
//      PRIO_TURN will interrupt whatever lower-priority speech is playing,
//      so firing promptly is fine.
// Release conditions:
//   - Damage signals clear, OR
//   - 5-second safety timeout (signals stuck), OR
//   - activeChar changed — cancel (return without firing).
// ============================================================================
static void PollDeferredTurnAnnounce()
{
    if (!s_deferredTurnPending) return;

    // v0.13.53: If the turn we deferred for has already ended or advanced
    // to a different character, cancel instead of firing a stale line.
    uint8_t curChar = 0xFF;
    if (s_pActiveCharId) {
        __try { curChar = *s_pActiveCharId; } __except(EXCEPTION_EXECUTE_HANDLER) {}
    }
    if (curChar != s_deferredTurnChar) {
        Log::Battle("BattleTTS: [TURN] Deferred cancelled (char %u -> %u, stale): %s",
                   (unsigned)s_deferredTurnChar, (unsigned)curChar, s_deferredTurnBuf);
        s_deferredTurnPending = false;
        s_deferredTurnBuf[0] = '\0';
        s_deferredTurnChar = 0xFF;
        return;
    }

    DWORD elapsed = GetTickCount() - s_deferredTurnTick;
    bool timeout = (elapsed >= 5000);

    uint8_t engDmgAnim = 0;
    __try { engDmgAnim = *(uint8_t*)0x01D280C0; } __except(EXCEPTION_EXECUTE_HANDLER) {}
    bool damageStillActive = s_ewmHoldForDamageTTS || s_anyHpPending ||
                             s_damageAnimWasActive || (engDmgAnim != 0);

    // Wait for damage conditions to clear (unless we hit the safety timeout).
    if (!timeout && damageStillActive) return;

    // v0.13.53: No IsSpeaking() gate — fire now. PRIO_TURN interrupts anything
    // queued below it, and waiting for SAPI idle was producing stale 4s+
    // deferrals when the user was actively navigating menus.
    BattleSpeak(s_deferredTurnBuf, PRIO_TURN, true);
    Log::Battle("BattleTTS: [TURN] Deferred fired after %u ms: %s%s",
               (unsigned)elapsed, s_deferredTurnBuf, timeout ? " (timeout)" : "");
    s_deferredTurnPending = false;
    s_deferredTurnBuf[0] = '\0';
    s_deferredTurnChar = 0xFF;
}
