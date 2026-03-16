# FFVIII Accessibility Mod

An accessibility mod for **Final Fantasy VIII** (Steam 2013 edition, App ID 39150) that makes the game playable for blind and low-vision players.

## What This Mod Does

This mod is a Win32 DLL (`dinput8.dll`) that injects alongside the game and [FFNx](https://github.com/julianxhokaxhiu/FFNx) (v1.23.x) to provide:

- **Text-to-Speech (TTS):** All in-game dialog, menus, and UI elements are read aloud via Windows SAPI.
- **FMV Audio Descriptions:** Pre-authored audio descriptions play during full-motion video cutscenes, with the option to skip FMVs entirely.
- **Field Dialog TTS:** Hooks into the JSM scripting engine to capture and speak all dialog opcodes (MES, ASK, AMES, AASK, AMESW, RAMESW) plus tutorial/thought popups.
- **Field Navigation:** A* walkmesh pathfinding with analog joystick emulation, entity catalog (NPCs, interactions, exits), and auto-drive to reach destinations hands-free.
- **Naming Screen Bypass:** Automatically names characters and GFs using default names, skipping screens that are inaccessible without sight.
- **Configurable Settings:** Adjustable music volume, TTS speech rate, and other accessibility options via in-game key bindings.

## Current Status

**Version:** v05.96 (active development)

| Feature | Status |
|---|---|
| Title screen TTS | Complete (v02.00) |
| FMV audio descriptions + skip | Complete (v03.00) |
| Field dialog TTS (all opcodes) | Complete (v04.36) |
| Field navigation + auto-drive | In progress (v05.96) |
| Battle accessibility | Research phase |

## Requirements

- **Final Fantasy VIII** — Steam 2013 edition (App ID 39150). This is the original PC port, *not* the 2019 Remaster.
- **FFNx v1.23.x** — Installed separately. The mod relies on FFNx being loaded alongside it.
- **Windows 10/11** — Required for SAPI text-to-speech support.
- **A screen reader (recommended)** — NVDA works well. JAWS requires pressing Insert+3 for key passthrough in fullscreen DirectX.

## Installation

1. Install Final Fantasy VIII (Steam 2013 edition).
2. Install [FFNx v1.23.x](https://github.com/julianxhokaxhiu/FFNx/releases) into the game directory.
3. Download the latest `dinput8.dll` from the [Releases](https://github.com/ampage87/FFVIII-Accessibility-Mod/releases) page.
4. Place `dinput8.dll` in the game's root directory (same folder as `FF8_EN.exe`).
5. Launch the game normally through Steam.

## Controls

The mod adds the following key bindings during gameplay:

| Key | Action |
|---|---|
| Tab | Open entity catalog / cycle through nearby entities |
| Enter | Auto-drive to selected entity |
| F1 | Decrease music volume |
| F2 | Increase music volume |
| F3 | Decrease TTS speech rate |
| F4 | Increase TTS speech rate |
| Escape | Cancel auto-drive |

## Building from Source

### Prerequisites

- Visual Studio Build Tools (MSVC v142+ toolset)
- Windows SDK

### Build

```
deploy.bat
```

This handles vcvars32 detection, resource compilation, source compilation, linking, and copies the resulting `dinput8.dll` to the game directory.

## Project Structure

```
src/
  ff8_accessibility.h      # Version constant and shared declarations
  ff8_addresses.h/.cpp     # Game memory addresses and function pointers
  field_navigation.cpp      # A* pathfinding, entity catalog, auto-drive
  field_archive.cpp         # Field archive and walkmesh parsing
  ...
Plan Documents/             # Research and design documents
Audio Descriptions/         # FMV audio description scripts
```

## How It Works

The mod operates as a DirectInput proxy DLL. When the game loads `dinput8.dll`, the mod initializes its hooks and passes through legitimate DirectInput calls to the real library. Key technical details:

- **Dialog capture:** Hooks FFNx's JSM opcode dispatch to intercept dialog opcodes before they reach the rendering pipeline, extracting the raw text and sending it to SAPI.
- **Navigation:** Loads the walkmesh from field archive ID files (FF7/PSX inline vertex format), builds a deduplicated vertex array, and runs A* pathfinding with SSFA (Simple Stupid Funnel Algorithm) for path smoothing.
- **Auto-drive:** Emulates an analog joystick by hooking the keyboard state reader and injecting a fake gamepad device, enabling true 360-degree steering along computed paths.
- **FMV descriptions:** Hooks the ReadFile API to detect FMV playback via EOF signatures, then plays pre-authored WAV audio descriptions synchronized to frame counts.

## Acknowledgments

- **[FFNx](https://github.com/julianxhokaxhiu/FFNx)** by Julian Xhokaxhiu and contributors — the modern FF7/FF8 driver that makes this mod possible.
- **[Echo-S](https://github.com/tangtang95/ff8-echo-s)** voice mod — confirmed that field dialog hooks are viable, providing early proof of concept.
- **Qhimm community** — decades of FF8 reverse engineering that documented the game's internal structures.

## License

This project is licensed under the MIT License. See [LICENSE](LICENSE) for details.

## Contributing

This project is in active development. If you're interested in contributing — especially to battle accessibility research or audio description authoring — please open an issue to discuss.
