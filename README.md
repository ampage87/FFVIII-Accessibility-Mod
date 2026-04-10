# FFVIII Accessibility Mod

An accessibility mod for **Final Fantasy VIII** (Steam 2013 edition, App ID 39150) that makes the game playable for blind and low-vision players through SAPI text-to-speech, audio-described cutscenes, field and world map navigation, and accessible battle and menu systems.

**Version:** v0.12.25 (active development — pre-release)

> **Important:** This mod uses Windows SAPI for speech output. It is **not** designed to be used with a screen reader running at the same time. Turn off JAWS, NVDA, Narrator, or any other screen reader before playing the game.

---

## For Players

### What This Mod Does

This mod adds comprehensive accessibility to Final Fantasy VIII so that blind and low-vision players can experience the full game. It provides:

- **Text-to-Speech for everything:** Dialog, menus, battles, navigation — all spoken aloud via Windows SAPI.
- **FMV audio descriptions:** Pre-authored descriptions play during full-motion video cutscenes, so you know what's happening on screen. FMVs can also be skipped entirely. There are 104 in-game video files with corresponding audio descriptions. (See the Contributing section below — these descriptions were AI-generated and need human review.)
- **Field navigation:** An entity catalog lets you browse nearby NPCs, exits, and interactive objects. Manual navigation is recommended at this point. Auto-drive is available but is still being refined and can be unreliable on some fields.
- **World map navigation:** A 38-location catalog with distance and bearing. Both manual navigation and auto-drive work on the world map, though auto-drive still needs refinement. Auto-drive persists through random encounters.
- **Battle accessibility:** Command menus, Magic/GF/Draw/Item sub-menus, target selection, damage and healing announcements, and HP readouts are all spoken. Enhanced Wait Mode (EWM) pauses the ATB gauge so you have time to act.
- **Menu accessibility:** Junction screen (partial), Item screen, and Save/Load screen are spoken. Other menus (Magic, GF, Status, Ability, etc.) are on the roadmap but not yet implemented.
- **Naming screen bypass:** Characters and GFs are automatically given their default names, skipping screens that require sight.
- **Configurable settings:** Adjust game volume, speech volume, speech rate, and TTS voice during gameplay with hotkeys.

### Current Feature Status

| Feature | Status |
|---|---|
| Title screen TTS | Working |
| FMV audio descriptions + skip | Working (AI-generated, needs human review) |
| Field dialog TTS (all dialog types) | Working |
| Field navigation (manual) | Working |
| Field navigation (auto-drive) | In progress — usable but unreliable on some fields |
| World map navigation (manual + auto-drive) | Working (auto-drive needs refinement) |
| Battle TTS + Enhanced Wait Mode | Working |
| Menu TTS (Junction, Item, Save/Load) | Working (Junction is partial; other menus not yet implemented) |
| Naming screen bypass | Working |
| Interactive object detection (doors, desks, etc.) | In progress |
| Battle sub-menu refinements (cancel/back, scrolling) | In progress |

### Known Issues

- **Recent refactoring regressions:** The mod was recently refactored into smaller source files (.inl split) to streamline development. This has introduced some regressions in previously-working features including Enhanced Wait Mode, the battle command menu, naming bypass, and others. These bugs are tracked in [Issues](https://github.com/ampage87/FFVIII-Accessibility-Mod/issues) and will be addressed in future builds.
- Some interactive objects on field maps are labeled generically ("Interaction 1") rather than by name.
- INF gateway exit compass directions may be slightly inaccurate on some fields.
- Walk-and-talk dialog (dialog triggered while walking on a hardcoded engine path) is not captured. This is a low-priority edge case.
- Auto-drive on field maps can get stuck or take suboptimal paths on certain field layouts.
- Not all game menus are accessible yet. Currently only Junction (partial), Item, and Save/Load are implemented. Magic, GF, Status, Ability, and other menus are planned for future builds.

### Requirements

- **Final Fantasy VIII** — Steam 2013 edition (App ID 39150). This is the original PC port, *not* the 2019 Remaster.
- **FFNx v1.23.x** — The modern graphics/audio driver for FF7/FF8. Install it into the game directory before installing this mod.
- **FF78Launcher** — A replacement launcher that bypasses the inaccessible default FF8 launcher. See installation instructions below.
- **Windows 10 or 11** — Required for SAPI text-to-speech.
- **No screen reader running** — The mod uses SAPI directly. Turn off JAWS, NVDA, Narrator, etc. before launching the game.

### Installation

Installing the 2013 Steam edition of FF8 can be tricky for blind users because the game includes an inaccessible launcher and Square Enix registration window. Follow these steps carefully:

1. **Download and install the game from Steam.** Search for "FINAL FANTASY VIII" (App ID 39150) — make sure it is the 2013 original, not the 2019 Remaster.

2. **Run the game once.** When you hear the FF8 launcher open, press Enter. Another window will appear — this is the Square Enix registration window, which is inaccessible to screen readers. Close the game at this point (Alt+F4 or use Task Manager).

3. **Install FFNx and FF78Launcher.** These two mods bypass the inaccessible launcher and provide the modern driver the accessibility mod depends on.
   - Download [FFNx v1.23.x for FF8 Steam](https://github.com/julianxhokaxhiu/FFNx/releases) and extract it into the game directory.
   - Download [FF78Launcher](https://github.com/julianxhokaxhiu/FF78Launcher) and place it in the game directory, replacing the original launcher. This allows the game to start directly without the inaccessible launcher window.

4. **Test the game launch.** Run the game again. If all goes well, you should hear game sounds start — a sound with the Square Electronic Arts animation, and eventually the game's background music. When you hear this, close the game.

5. **Install the accessibility mod.** Download the latest `dinput8.dll` from the [Releases](https://github.com/ampage87/FFVIII-Accessibility-Mod/releases) page. Copy it to the game directory (default: `C:\Program Files (x86)\Steam\steamapps\common\FINAL FANTASY VIII\`).

6. **Launch the game.** You should now hear the game start along with SAPI speech from the accessibility mod: "Final Fantasy 8 Accessibility Mod loaded."

### Controls

The mod adds these key bindings. The game's original controls remain unchanged.

| Key | Action |
|---|---|
| `` ` `` (backtick) | Repeat last dialog or battle event |
| V | Announce mod version |
| F1 | Cycle TTS voice |
| F3 / F4 | Decrease / increase game volume |
| F5 / F6 | Decrease / increase speech volume |
| F7 / F8 | Decrease / increase speech rate |
| F9 / F10 | Field navigation controls |
| F11 | Menu summary (Shift = monitor, Ctrl = dump) |
| G | Announce Gil |
| T | Announce play time |
| L | Announce location |
| R | Announce SeeD rank/salary |
| / | Speak help bar text |
| O | Toggle Enhanced Wait Mode |
| 1 / 2 / 3 | Check HP of party member 1 / 2 / 3 |
| H | Check HP of all party members |

---

## Supporting This Project

The FF8 Accessibility Mod is free and always will be. However, development takes significant time and effort, and the AI tools used in building it (such as Claude by Anthropic) have ongoing costs.

If you find this mod valuable, there are several ways you can help:

**Review audio descriptions** — The 104 FMV audio descriptions were generated using AI (Claude) and have not been vetted by humans. Even if you're not technical, you can help by watching the in-game videos, listening to their audio descriptions, and providing feedback on accuracy. The audio description scripts are in the `Audio Descriptions/` folder in VTT format.

**Report bugs and test** — Play through the game and report any issues you encounter. Bug reports are especially valuable for areas of the game the developer hasn't reached yet.

**Make a donation** — If you're able and willing, you can sponsor this project through [GitHub Sponsors](https://github.com/sponsors/ampage87). Every contribution helps keep development going.

Thank you to everyone who has supported this project in any way.

---

## For Developers

### How It Works

The mod operates as a DirectInput proxy DLL (`dinput8.dll`). When the game loads it, the mod initializes its hooks via [MinHook](https://github.com/TsudaKageyu/minhook) and passes through legitimate DirectInput calls to the real library. It runs alongside [FFNx](https://github.com/julianxhokaxhiu/FFNx), hooking both the original game engine and FFNx's own functions where needed.

Key technical approaches:

- **Dialog capture:** Hooks FFNx's JSM opcode dispatch to intercept dialog opcodes (MES, ASK, AMES, AASK, AMESW, RAMESW) before they reach the rendering pipeline, extracting raw text and sending it to SAPI.
- **Field navigation:** Parses walkmesh data from field archive files, builds a deduplicated triangle mesh, and runs A* pathfinding with SSFA (Simple Stupid Funnel Algorithm) for smooth paths. Analog steering is achieved by hooking the keyboard state reader and injecting a fake gamepad device for true 360-degree movement.
- **Battle hooks:** Reads battle engine memory structures to track ATB state, command menus, sub-menu cursors, target selection, and damage events. Enhanced Wait Mode works by pre-capping ATB values in an FFNx GF loading hook.
- **Menu hooks:** Reads menu state memory to track cursor positions across Junction, Item, and Save/Load screens. Save files are decompressed from LZSS format to read slot details.
- **World map:** Hooks world map engine functions to track player position on a torus-wrapped map, with a 38-location catalog providing distance and bearing. Auto-drive uses `keybd_event()` with extended scan codes.
- **FMV descriptions:** Hooks the ReadFile API to detect FMV playback via EOF signatures, then plays pre-authored WAV audio descriptions synchronized to frame counts.
- **Interactive objects:** Hooks SETLINE opcode to detect trigger zones for doors, desks, and other interactive objects on field maps. Entity classification uses JSM script analysis to distinguish NPCs, exits, interactions, and background elements.

### Project Structure

```
src/
  ff8_accessibility.h          # Version constant and shared declarations
  dinput8.cpp                   # DLL entry, DirectInput proxy, global hotkeys
  screen_reader.cpp             # SAPI TTS wrapper
  field_navigation.cpp + .inl   # Field nav: pathfinding, catalog, auto-drive, GPS
  field_archive.cpp + .inl      # Field archive parsing, JSM script scanner
  field_dialog.cpp              # Dialog opcode hooks
  battle_tts.cpp + .inl         # Battle TTS, EWM, GF hooks, sub-menus
  menu_tts.cpp + .inl           # Menu TTS: junction, item, save/load
  world_map.cpp                 # World map navigation + auto-drive
  fmv_audio_desc.cpp            # FMV audio descriptions
  fmv_skip.cpp                  # FMV skip functionality
  game_audio.cpp                # BGM/FMV volume control via FFNx
  ff8_addresses.cpp             # Game memory addresses and function pointers
  nav_log.cpp                   # Multi-channel logging system
Audio Descriptions/             # FMV audio description VTT scripts
Plan & Research Documents/      # Design docs and research notes
```

Large source files are split into `.inl` textual includes (e.g., `field_nav_catalog.inl`, `battle_tts_ewm.inl`) to keep individual files manageable while avoiding the complexity of separate compilation units.

### Building from Source

**Prerequisites:**
- Visual Studio Build Tools (MSVC v142+ toolset)
- Windows SDK

**Build:**
```
deploy.bat
```

This handles vcvars32 detection, resource compilation, source compilation, linking, and copies the resulting `dinput8.dll` to the game directory.

### Contributing

This project is in active development. Areas where contributions would be especially valuable:

- **Audio description review:** The 104 FMV audio descriptions were AI-generated and need human review. Watch the videos, listen to the descriptions, and report inaccuracies. No technical skills required — this is the easiest way to contribute.
- **Testing:** Play through the game and report issues, especially in areas the developer hasn't reached yet.
- **Battle system:** The battle TTS has several open issues tracked in [Issues](https://github.com/ampage87/FFVIII-Accessibility-Mod/issues).
- **Code contributions:** The mod is pure C++ with MinHook. If you're comfortable with reverse engineering and memory hooking, there's plenty to do.

Please open an issue to discuss before starting work on a contribution.

---

## Acknowledgments

- **[FF6 Screen Reader](https://github.com/BlindGuyNW/FF6ScreenReader)** by BlindGuyNW — the accessibility mod for Final Fantasy VI Pixel Remaster that inspired this project. Seeing what was possible with FF6 is what motivated me to attempt this for FF8, with the assistance of Claude.
- **[FFNx](https://github.com/julianxhokaxhiu/FFNx)** by Julian Xhokaxhiu and contributors — the modern FF7/FF8 driver that makes this mod possible.
- **[FF78Launcher](https://github.com/julianxhokaxhiu/FF78Launcher)** by Julian Xhokaxhiu — the alternative launcher that bypasses the inaccessible default FF8 launcher.
- **[Echo-S](https://github.com/tangtang95/ff8-echo-s)** voice mod — confirmed that field dialog hooks are viable, providing early proof of concept.
- **Qhimm community** — decades of FF8 reverse engineering that documented the game's internal structures.

## License

This project is licensed under the MIT License. See [LICENSE](LICENSE) for details.
