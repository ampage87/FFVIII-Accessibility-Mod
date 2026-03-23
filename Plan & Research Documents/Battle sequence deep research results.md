# Battle Accessibility Mod: Commands, UI Elements, and Implementation Plan

## Battle Commands and Menus Inventory

FF8’s battle system offers several base commands plus many junctioned command abilities. All standard battle commands that must be supported include (with citations for key categories):

- **Core Commands** (always available): **Attack** (physical attack)【2†L290-L298】, and the six character-specific Limit Break commands (e.g. **Renzokuken**, **Duel**, **Shot**, **Blue Magic**, **Slot**, **Fire Cross**, **Sorcery**, **Combine**, **Angel Wing**, plus the Laguna/Kiros/Ward trio listed under “Limit” commands)【2†L290-L298】. These may replace or augment Attack when equipped (e.g. *Mug* when equipped replaces Attack).  
- **Magic/GF/Draw/Item/Defend**: By default only Attack and Defend (if equipped) are usable. Other actions **Magic**, **GF**, **Draw**, and **Item** become available when the character equips the corresponding command Ability (via GF junction scrolls)【9†L209-L217】【2†L290-L298】. (“Magic” lets the player select spells; “GF” summons a Guardian Force; “Draw” starts the Draw menu; “Item” opens the inventory in battle.)  
- **Other Equippable Commands**: Additional junctioned commands include **Card**, **Doom**, **Mad Rush**, **Treatment**, **Darkside**, **Recover**, **Absorb**, **Revive**, **LV Down/Up**, **Kamikaze**, **Devour**, **Defend**, **MiniMog** (and others listed on the command abilities wiki). For example, *Card* turns an enemy into a card【9†L209-L217】; *Doom* inflicts instant-death【4†L339-L347】; *Defend* halves damage【4†L349-L357】; *Mad Rush* buffing party【4†L342-L347】; *Treatment* cures statuses【4†L348-L351】; etc. Our mod must be able to speak all of these when selected.  
- **Submenus**: Selecting Magic, GF, Draw, or Item leads into submenus. These have their own lists:
  - **Magic menu:** list of equipped spells on the active character. The mod should announce the highlighted spell name and when a spell is cast, speak any resulting damage/status.  
  - **GF menu:** list of available GFs. Announce the selected GF name. On confirm, battle enters summoning sequence (with “Boost” mechanic to increase damage【9†L209-L217】). The mod should detect when Boost input is active and optionally provide hints.  
  - **Draw menu:** first choose *Stock* vs *Cast*, then a list of magic stocked in the target. The mod should announce “Stock”/“Cast” and the chosen spell from the Draw list.  
  - **Item menu:** list of inventory items. Announce item name and target if selected.  
- **Target Window**: By default FF8 shows a camera view; players can press **H** to “Show/Hide target window”【7†L12-L17】, which lists all valid targets (enemies or allies) by name. The mod should intercept the target window text and announce the currently highlighted target (enemy or ally). This ensures blind players know exactly who is targeted.  
- **Command Flow**: The UI flow is “Main Command → (Submenu if any) → Target selection → Action execution.” We must cover all nodes. 

**Sources:** The above commands are documented on the FFVIII wiki【2†L290-L298】【4†L339-L347】【9†L209-L217】. The official PC manual key settings show that *H* toggles the target window【7†L12-L17】. 

## Core UI Elements and Data Hooks

To implement this, we’ll hook into the FF8 battle system at several points:

- **Game Mode Detection:** Use `FF8Addresses::pGameMode` (battle mode is 999) to detect when a battle starts and ends. Like existing FFNX code, trigger a “Battle Active” mode to enable special handling.  
- **Dialog/Text Hooks:** The mod already hooks `show_dialog` to capture on-screen text. In battle mode, use this to intercept:
  - **Battle menu text:** e.g. when the game draws “Attack”, “Magic”, etc., or submenu lists, decode those from the text buffer. Announce the *selected* entry (tracked via window’s current_choice index).  
  - **Message window:** battle messages like “Squall casts Firaga” or “Gong!!” (when a weapon skill is used). We should speak battle log messages (perhaps with priority lower than menus).  
  - **Scan/Item/Status windows:** If a status or Scan effect brings up a popup window (e.g. after using Scan, or from the Status Screen), capture that with `show_dialog` and announce its contents (enemy HP/weakness or character status).  
- **Target Window Hooks:** When the target window is toggled (with key H), it’s also a dialog window. When `show_dialog` sees a target-list window, parse the list and announce the currently highlighted name. Keying off both window ID and content (e.g. presence of enemy names, “all” keyword) can identify this window.  

- **Battle Loop & Structures:** For dynamic stats (HP, damage, ATB fill):
  - FFNx resolves pointers for character stats and battle state (e.g. `battle_current_active_character_id`) via call chains in `ff8_data.cpp`. While we lack the resolved values here, FFNX internally provides them. We should use the same pointers (or call the same compute_char_stats function) to read current/maximum HP and statuses of each battler. 
  - Alternatively, the easiest approach is to poll the characters’ HP from the in-memory character stats array (accessible once per frame in battle). Each party member has a `curr_hp` and `max_hp`. Poll these each game tick or in the battle loop hook to detect changes (damage/heal events).  

**Memory and Pointer Sources:**  
We rely on FFNx’s existing `ff8_addresses` logic. As an example, FFNx code shows: `ff8_char_computed_stats` pointer is found by following `compute_char_stats_sub_495960`【source】. This yields an array of 3 character stat structs (with current HP etc). Also, internal functions/variables indicate *active character index* and *attack target index*. These can be polled. The FF8 executable has no ASLR, so these resolved addresses are stable (FFNx demonstrates this).  

## Speech Events and Content Design

For each relevant event or UI element, plan what to say:

- **Menus & Selection:** Whenever a menu choice changes (main command or submenu), speak the new highlighted command or item. E.g. “Magic” then “Firaga” as the player scrolls. Include context like “Command:” or “Spell:” if needed, but brevity is key.  

- **Target Announcements:** When target selection changes (in target window or after confirming a command), announce the target’s name and party position (e.g. “Target: Ifrit (Enemy)”). For multi-target spells, announce “All enemies” or “All allies” if that option is selected.  

- **HP Announcements:** When control switches to a character (default **V** cycles active character) or after an action, announce that character’s current HP. E.g. “Squall – HP 1234.” This tells the player health status. Also on-demand (e.g. a hotkey or Pause screen) read all party HP/status.  

- **Status Ailments:** If a character or enemy becomes affected by a status, announce it. Track status bitmasks: common negative statuses include **Poison, Petrify, Darkness, Silence, Berserk, Zombie, Sleep, Slow, Stop, Doom, Confuse**, etc.【12†L278-L287】. Also positive states like **Protect/Shell/Haste/Reflect** if meaningful. When status data changes, generate speech like “Rinoa is poisoned” or “Protect wears off on Zell.”  

- **Damage/Healing Feedback:** 
  - *Damage:* When a battler takes damage, optionally speak it if it’s significant. Because battles can have many hits (multi-hit combos, ATB ticks), throttle this. Perhaps announce every attack’s damage only if it would otherwise go unannounced (e.g. if it defeats an enemy or is critical). If we do announce, use “Character X takes N damage.”  
  - *Healing:* Similarly announce heals, e.g. “Squall healed 500” or “Party healed”. For Limit Break healing (Slot or Regen), announce accordingly.  
  - These should not trigger on every frame; best to detect actual HP delta events. One approach: keep last HP values for each entity and when we see a drop or rise of, say, ≥1, announce once. Deduplicate fast consecutive hits (say, announce aggregate after multi-hit or skip non-critical values).  

- **Limit Breaks:** When a limit break menu is entered (e.g. “Limit” command appears or status allows it), announce “Limit ready.” Upon selection, speak the specific limit interface (e.g. “Renzokuken sequence” or “Shot – select ammo”). If a timed trigger (Gunblade), perhaps say “Gunblade!” once per sequence.  

- **Guardian Force Summons:** On summoning, announce “Summoning [GF name]” and optionally “Boost ready” if applicable. As described on [9], Boost lets you hold Select and tap a key to raise damage up to 250%【9†L209-L217】; the mod could announce “Boost 100%” etc., but that may overcomplicate. At minimum, announce the GF’s arrival and any menu like “Ifrit attacks!” etc.  

- **Scan Command:** When a party member uses **Scan**, the enemy’s info (HP, weaknesses) is shown. Hook this window (which likely calls `show_dialog`) and read it aloud, e.g. “Bite Bug – HP 500 – weak to Fire.”  

- **GF Draw Sequence:** After a draw, announce what magic was drawn from which target. E.g. “Drawn Tornado from enemy.”  

- **Critical/Berserk Messages:** Battle logs often include text like “Critical!” or “X was absorbed”. The mod’s `show_dialog` hook will catch those. Make sure to also speak these event messages if they convey important info (e.g. announce “Critical hit!” or “Enemy was absorbed!”).  

- **Party Interface (Party HP and Status):** Consider assigning a hotkey (like a custom voice hint) that, when pressed, reads all party members’ names, HP, and active statuses. This mirrors how “Status” screen works but via speech. 

**Event Throttling & Queues:** Because battles can be fast, we must prevent speech from overlapping or flooding. Use a FIFO queue: high-priority events (e.g. “character KO” or “limit break used”) interrupt or preempt, while lower (regular damage, menu navigation) queue up. Combine similar messages: if multiple hits occur, it may suffice to say “Zell took heavy damage” once rather than “Zell took 200, 150, 180.” Summarize multi-target results (“All enemies hit for 300”).  

## Claude-Ready Implementation Tasks and Pseudocode

1. **Hook Battle Mode Entry/Exit:** In the mod’s main update loop, detect when `pGameMode==999` (battle). On entry, initialize battle-specific state (clear caches). On exit, flush any pending speech.  
2. **Window Classification:** Inside the existing `FieldDialog::Hook_Show_Dialog`, add a branch for `FF8_MODE_BATTLE`. Classify each window ID/text as: *Command Menu, Submenu (Magic/GF/Draw/Item), Target Window, Message Log, Scan/Status Window, Party Status Window*. Use window position or known key words to distinguish.  
3. **List Window Handling:** For menu windows (Command, Magic, GF, Draw, Item), parse the decoded multi-line text into options. Use the window’s `first_question`/`last_question` indexes or cursor position fields (available via the WinObj struct) to determine the highlighted line. Speak only the current choice (skip unhighlighted entries). Example pseudocode:  
   ```cpp
   if (window_is_command_menu) {
       vector<string> options = win_decode_lines();
       int idx = win_obj.current_choice - win_obj.first_choice;
       speak(options[idx]);
   }
   ```
4. **Target Window Handling:** If a window is identified as the target list, use a similar list parse and speak the highlighted target name. If the text contains “All” or a special marker, announce “All targets”.  
5. **Battle Message Window:** Treat any text that is not a menu/target (usually in bottom-left) as narrative. Filter out “!” or empty dialogs, but read substantive lines like attack names (“X uses Y”). Possibly prefix with character name if not already included.  
6. **HP/Status Polling:** Every frame (or on ATB hit), check `current_char_id` pointer (from FFNx) to know whose turn it is. For that character, read from the computed stats array:  
   - `curr = char_computed[active_id].curr_hp; max = char_computed[active_id].max_hp; status = char_computed[active_id].status_flags;`  
   If active character changed or HP changed since last check, speak “Name HP curr out of max” and list any status flags (e.g. “poisoned”). Maintain last values to detect changes.  
7. **Damage/Heal Events:** After each round of actions, compare stored HP values to detect damage or heal events. If damage detected on enemy or self, queue speech “Enemy X took N” or “You took N.” On heal, “Healed N”. Apply throttling: if many small hits occur in quick succession, either skip or aggregate.  
8. **Limit Breaks & GF:** Detect menu text “Limit” or `win_obj.command_id == someID`. When active and accepted, announce “Limit break!” If it's a selectable limit (e.g. Cell Limit), announce which. For GF summons, when GF sequence starts, announce “Summoning [GF name]”. When GF attack damage (Boost) resolves, optionally announce damage multiplier.  
9. **Scan and Status Window:** When Scan is used, the game shows enemy HP/weakness text in a window. The `show_dialog` hook will catch it; simply speak that text (condensing if needed). For the pause/status screen, hook key (maybe `A`) to iterate the status window and speak it (existing mod approach for menus can apply).  
10. **Hotkeys and Controls:** Optionally add a hotkey (e.g. `P` for “Party”) that triggers a readout of party HP/status. Document it for users. 

## Testing and Validation

- **Menu navigation:** Verify each menu’s speech by cycling commands (arrow keys). The mod should speak new command names.  
- **Submenus:** In Magic/GF/Draw/Item, navigate lists and confirm the spoken name matches selection.  
- **Target list:** Open target window and move cursor; ensure spoken target matches on-screen highlight.  
- **Scan:** Cast Scan on each enemy; verify the spoken information matches displayed HP/weakness lines.  
- **Status effects:** Inflict poison, silence, etc., on a character; the mod should announce it (via message or status readout). Remove statuses (Remedy) and announce cures.  
- **HP changes:** Compare visual HP bar and spoken HP; use and heal spells and confirm announcements (“X took N”, “Y healed N”). Test continuous damage (e.g. Poison) for throttling correctness.  
- **Limit breaks and GF:** Charge a limit and trigger it, verify the limit break name is announced. Summon a GF and hold Boost; optionally ensure any prompts are spoken.  
- **End-of-battle:** Announce victory screen text (rarely needed) or returning to world.

**Sources:** Command lists from the Final Fantasy VIII wiki【2†L290-L298】【4†L339-L347】【9†L209-L217】; target window key from the official PC manual【7†L12-L17】; status effect list from the FFVIII wiki【12†L278-L287】. These document the content to cover in speech. 

