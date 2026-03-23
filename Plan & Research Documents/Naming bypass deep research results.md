# FF8 Original PC Naming Screen Bypass — Technical Implementation Plan for an FFNx Companion DLL

## Executive summary

This plan describes a robust, minimally invasive way to **prevent or automatically bypass** the in-game **name entry (character/GF naming) UI** in *Final Fantasy VIII* **original PC edition (Steam 1.2 US)** from an **FFNx-based companion DLL** (your accessibility mod v04.22). The key discovery is that the **field script opcode `MENUNAME` is opcode index `0x129`** (menus category) in community opcode listings, and FFNx itself resolves an **`opcode_menuname` handler pointer** from the game’s **opcode dispatch table** at index `0x129`. citeturn4search1

Because the precise internal semantics/stack parameters of `MENUNAME` are **not documented in the available opcode pages** (only its existence/name/index is documented), the recommended implementation uses a **two-tier strategy**:

- **Tier A (recommended default): Auto-accept** the naming UI by detouring the `MENUNAME` handler and running a bounded **“confirm injection” loop** during the naming interaction window. This preserves the game’s side effects (state flags, save buffers, progression), and it typically makes the naming screen disappear almost instantly.
- **Tier B (optional, “hard bypass”): Skip the opcode** (return success without calling original) for players who want the UI never to appear; gate this behind a config flag because side effects are not fully characterized.

The plan also includes an **optional “set configured names” path** based on the documented *FF8 Steam save format*, which shows that several “preview names” (e.g., Squall/Rinoa/Angelo/Boko) and GF names are stored as **12-byte, 0x00-terminated strings**, and that the Steam save is **LZS-compressed** with offsets starting at **0x0180 (384)**. citeturn12view1 This supports two future enhancements: (1) patch save writes, or (2) locate the in-memory save buffer and write names directly.

## Evidence and entry points in Steam 1.2 US

### Field opcode and handler resolution

Community opcode tables list:

- **`0x129 MENUNAME`** (Menus category). citeturn4search1

FFNx’s FF8 externals show that the engine maintains an **opcode dispatch table** and that the handler pointer for `MENUNAME` is obtained by indexing that table:

- FFNx sets `common_externals.execute_opcode_table` from `update_field_entities + 0x65A`, then assigns `ff8_externals.opcode_menuname = common_externals.execute_opcode_table[0x129];`  
  **observed in provided source:** `FFNx-Steam-v1.23.0.182/Source Code/FFNx-canary/src/ff8_data.cpp#L329-L356`
- FFNx’s externals struct explicitly includes `uint32_t opcode_menuname;`  
  **observed in provided source:** `FFNx-Steam-v1.23.0.182/Source Code/FFNx-canary/src/ff8.h#L1464-L1472`

Your accessibility DLL already duplicates the same resolution approach:

- It resolves `pExecuteOpcodeTable` from `update_field_entities + 0x65A`.  
  **observed in provided source:** `src/ff8_addresses.cpp#L446-L452`
- It already reads other opcode handler addresses out of `pExecuteOpcodeTable` (e.g., MES/ASK/AASK, etc.).  
  **observed in provided source:** `src/ff8_addresses.cpp#L526-L537`

Therefore, **Steam 1.2 US exact runtime address** for the naming handler should be treated as:

- **`opcode_menuname = pExecuteOpcodeTable[0x129]`** (runtime-discovered).  
  The numeric address is **currently unspecified** because it is not logged in v04.22.

### Save-format evidence for name fields

The *FF8 Steam save format* documentation (by myst6re) provides concrete byte layouts relevant to naming:

- Steam saves are **LZS-compressed**, and “Offsets start from 384 (0x0180).” citeturn12view1
- The save contains multiple **12-byte, 0x00-terminated name fields** in the “Preview” area (Squall, Rinoa, Angelo, Boko). citeturn12view1
- Each GF record includes a **12-byte, 0x00-terminated Name** field. citeturn12view1

This is useful for optional “configured names” work (save patching or memory patching), even if the immediate goal is to bypass/auto-accept the in-game naming UI.

## Bypass approaches and recommendation

### Comparison of implementation options

| Option | What you hook/modify | What happens in-game | Pros | Cons / risks | Recommended use |
|---|---|---|---|---|---|
| Auto-accept via `MENUNAME` detour | Detour `opcode_menuname` (0x129); run bounded “confirm injection” while naming UI is active | UI appears briefly, then auto-confirms | Preserves side effects; minimal RE needed; easy rollback | Requires reliable confirm input injection; needs good gating to avoid misfires | **Default** |
| Hard bypass (skip opcode) | Detour `opcode_menuname`; return success without calling original | UI never appears | Very fast; no injection dependencies | Side effects unknown; could leave name unset in edge cases | Optional “experimental” |
| Patch save writes | Hook file I/O; decompress LZS; rewrite name bytes at known offsets; recompress | UI may still appear, but names end up set & persistent | Deterministic persistence; supports configured names | Complex; needs LZS; careful checksum handling | Future enhancement |
| Patch menu state machine | Find/patch the menu-mode function that runs naming UI | UI never appears (or instantly completes) | Best UX when fully mapped | Needs deeper RE (state vars, call graph) | Later, after instrumentation |

### Recommended path

Implement in this order:

1. **Add `opcode_menuname` address resolution and detour** (fast, clear).
2. Start with **Auto-accept** (inject confirm), with strict gating and timeouts.
3. Add **Hard bypass** as a config option once auto-accept is validated and you understand any needed side effects.
4. If you need configured (custom) names, add either:
   - **Input injection for typing** (works if the naming UI reads keyboard events reliably), or
   - **Save/memory patching** based on the documented 12-byte name fields. citeturn12view1

## Implementation steps in the v04.22 codebase

### Add opcode address plumbing for `MENUNAME`

Add a new extern in `src/ff8_addresses.h` next to the other opcode handler addresses:

```cpp
// Opcode handler addresses (read from pExecuteOpcodeTable at init).
extern uint32_t opcode_menuname;   // 0x129 — MENUNAME (naming UI)
```

Insertion point:
- **observed in provided source:** `src/ff8_addresses.h#L225-L234`

Then resolve it in `src/ff8_addresses.cpp` where other opcodes are loaded from `pExecuteOpcodeTable`:

```cpp
if (pExecuteOpcodeTable != nullptr) {
    // existing...
    opcode_aask     = pExecuteOpcodeTable[0x6F];

    // new:
    opcode_menuname = pExecuteOpcodeTable[0x129];
    Log::Write("FF8Addresses:   opcode_menuname [0x129] = 0x%08X", opcode_menuname);
}
```

Insertion point:
- **observed in provided source:** `src/ff8_addresses.cpp#L526-L544`

This turns the “exact handler address” into a **runtime log**, which is the correct approach for Steam 1.2 US across minor binary variations.

### Add a new module `NameBypass`

Create two new files:

- `src/name_bypass.h`
- `src/name_bypass.cpp`

Suggested header shape:

```cpp
#pragma once

namespace NameBypass {
    bool Initialize();
    void Shutdown();
    void Update();  // called each frame from AccessibilityThread
}
```

Add module initialization and per-frame update calls in `src/dinput8.cpp`:

- Initialize right after other modules:

  - **observed in provided source:** `src/dinput8.cpp#L77-L85`

  Add:

  ```cpp
  NameBypass::Initialize();
  ```

- Update each frame inside the loop:

  - **observed in provided source:** `src/dinput8.cpp#L108-L130`

  Add:

  ```cpp
  NameBypass::Update();
  ```

### Hook `opcode_menuname` with MinHook (same pattern as dialog opcodes)

Your codebase already uses MinHook for opcode detours and documents opcode handler calling convention patterns in `field_dialog.cpp` (cdecl, single `int entityPtr`).  
**observed in provided source:** `src/field_dialog.cpp#L60-L66`

Replicate in `name_bypass.cpp`:

```cpp
#include "ff8_accessibility.h"
#include "ff8_addresses.h"
#include "minhook/include/MinHook.h"

namespace NameBypass {

using OpcodeHandler_t = int (__cdecl *)(int);
static OpcodeHandler_t s_origMenuname = nullptr;

enum class BypassMode {
    AutoAccept,   // call original, but auto-confirm quickly
    SkipOpcode    // do not call original (experimental)
};

static struct {
    bool enabled = true;
    BypassMode mode = BypassMode::AutoAccept;
    DWORD maxWindowMs = 4000;      // how long we’ll try to auto-confirm
    DWORD pressEveryMs = 250;      // throttle confirm presses
    WORD  confirmVk = VK_RETURN;   // configurable key
    WORD  optOutVk = VK_SHIFT;     // hold to disable bypass temporarily
} g_cfg;

static volatile LONG s_sessionId = 0;
static DWORD s_deadline = 0;
static DWORD s_lastPress = 0;
static bool s_pending = false;
static WORD s_expectedMode = 0; // optional: store *pGameMode snapshot

static void SendKeyPress(WORD vk) {
    INPUT in[2] = {};
    in[0].type = INPUT_KEYBOARD;
    in[0].ki.wVk = vk;
    in[1] = in[0];
    in[1].ki.dwFlags = KEYEVENTF_KEYUP;
    SendInput(2, in, sizeof(INPUT));
}

static int __cdecl Hook_opcode_menuname(int entityPtr) {
    if (!g_cfg.enabled || FF8Addresses::pGameMode == nullptr) {
        return s_origMenuname ? s_origMenuname(entityPtr) : 0;
    }

    if (GetAsyncKeyState(g_cfg.optOutVk) & 0x8000) {
        // user is holding opt-out key
        return s_origMenuname ? s_origMenuname(entityPtr) : 0;
    }

    const WORD modeNow = *FF8Addresses::pGameMode;
    Log::Write("NameBypass: MENUNAME enter entity=0x%08X mode=%u", entityPtr, (unsigned)modeNow);

    if (g_cfg.mode == BypassMode::SkipOpcode) {
        Log::Write("NameBypass: MENUNAME bypass=SkipOpcode returning success");
        return 0; // experimental: assume “success”
    }

    // AutoAccept: enable pending injection before calling original.
    InterlockedIncrement(&s_sessionId);
    s_deadline = GetTickCount() + g_cfg.maxWindowMs;
    s_lastPress = 0;
    s_pending = true;

    const int ret = s_origMenuname ? s_origMenuname(entityPtr) : 0;

    // Naming UI should be done; stop injection.
    s_pending = false;
    Log::Write("NameBypass: MENUNAME exit ret=%d", ret);
    return ret;
}

bool Initialize() {
    if (FF8Addresses::opcode_menuname == 0) {
        Log::Write("NameBypass: opcode_menuname not resolved; skipping.");
        return false;
    }

    const MH_STATUS st = MH_CreateHook(
        (LPVOID)FF8Addresses::opcode_menuname,
        (LPVOID)Hook_opcode_menuname,
        (LPVOID*)&s_origMenuname);

    Log::Write("NameBypass: MH_CreateHook(opcode_menuname=0x%08X) => %d",
               FF8Addresses::opcode_menuname, (int)st);
    return (st == MH_OK);
}

void Shutdown() {
    s_pending = false;
    if (FF8Addresses::opcode_menuname)
        MH_DisableHook((LPVOID)FF8Addresses::opcode_menuname);
}

void Update() {
    if (!s_pending || !g_cfg.enabled || FF8Addresses::pGameMode == nullptr)
        return;

    if (GetAsyncKeyState(g_cfg.optOutVk) & 0x8000) {
        // user opted out mid-flow
        s_pending = false;
        Log::Write("NameBypass: pending cancelled by opt-out key");
        return;
    }

    DWORD now = GetTickCount();
    if (now > s_deadline) {
        s_pending = false;
        Log::Write("NameBypass: pending timed out");
        return;
    }

    // Gate: only inject when the game is in menu mode (reduces misfires).
    // MODE_MENU = 6 in your codebase.
    if (*FF8Addresses::pGameMode != FF8Addresses::MODE_MENU)
        return;

    if (s_lastPress == 0 || (now - s_lastPress) >= g_cfg.pressEveryMs) {
        SendKeyPress(g_cfg.confirmVk);
        s_lastPress = now;
        Log::Write("NameBypass: injected CONFIRM vk=%u", (unsigned)g_cfg.confirmVk);
    }
}

} // namespace NameBypass
```

Key points:

- The detour is anchored to **`opcode_menuname`**, which is derived from `pExecuteOpcodeTable[0x129]` (same concept as FFNx). **observed in provided source:** `FFNx-Steam-v1.23.0.182/Source Code/FFNx-canary/src/ff8_data.cpp#L329-L356`
- Input injection is strictly **time-bounded** and gated by **MODE_MENU==6**, which is already defined in your codebase. **observed in provided source:** `src/ff8_addresses.h#L120-L135`
- `SkipOpcode` is explicitly treated as experimental.

### Where to load settings

Your mod currently does not appear to load a user config file (v04.22). If you want minimal config without adding dependencies, implement a simple `ff8_accessibility.ini` parser (key=value lines) and read it inside `NameBypass::Initialize()`.

Recommended settings:

- `naming_bypass_enabled` (default `1`)
- `naming_bypass_mode` (`auto_accept` / `skip_opcode`)
- `naming_confirm_vk` (default `VK_RETURN`)
- `naming_optout_vk` (default `VK_SHIFT`)
- `naming_timeout_ms` (default `4000`)
- `naming_press_every_ms` (default `250`)

If config is not implemented in the first iteration, hardcode defaults and only expose a **runtime toggle hotkey** (e.g., F9) using `GetAsyncKeyState`, as already used in FMV skip and title screen modules. **observed in provided source:** `src/fmv_skip.cpp#L349-L349` and `src/title_screen.cpp#L90-L95`

## Detection heuristics, fallback handling, and test plan

### Detection heuristics

Primary signals (reliable, low effort):

- **Opcode trigger:** `opcode_menuname` detour hit (start of naming flow).  
  Rationale: `MENUNAME` is explicitly listed as opcode 0x129. citeturn4search1
- **Module mode:** Only inject confirm when `*pGameMode == MODE_MENU (6)`.  
  Rationale: reduces risk of confirming unrelated dialogs. **observed in provided source:** `src/ff8_addresses.h#L120-L135`

Secondary signals (optional instrumentation):

- Use your existing opcode-dispatch instrumentation (already patches call site in `update_field_entities`) to confirm `0x129` frequency in real gameplay.  
  **observed in provided source:** `src/field_dialog.cpp#L92-L179`

Fallback strategy:

- If MODE_MENU gating prevents confirmation (i.e., naming UI runs in a different mode in some scenes), add a second gating rule:
  - Allow injection when `modeNow == MODE_FIELD (1)` *and* `MENUNAME` was called within the last N ms.
  - Keep the timeout short (e.g., 750–1500 ms) for this looser gate.

### Logging format (explicit and greppable)

Use single-line records to simplify analysis:

- On entry:

  `NameBypass: MENUNAME enter entity=0x%08X mode=%u field=%s session=%ld`

- On injection:

  `NameBypass: injected CONFIRM vk=%u mode=%u t=%lu session=%ld`

- On exit:

  `NameBypass: MENUNAME exit ret=%d session=%ld`

- On timeout:

  `NameBypass: pending timed out after %ums session=%ld`

### Test plan with expected log signatures

**Test case: new game naming prompt**

Expected:

- At least 1 `NameBypass: MENUNAME enter ...`
- Several `NameBypass: injected CONFIRM ...` (maybe 1–3 presses)
- `NameBypass: MENUNAME exit ...` within < 1–2 seconds
- No timeout lines

Failure signatures and what they mean:

- `MENUNAME enter` appears, but **no injected CONFIRM**: Update() not called (missing `NameBypass::Update()` in loop) or pending not set.
- Injected CONFIRM appears, but naming UI remains: confirm key incorrect (VK mismatch), injection not reaching game, or menu mode gating preventing injection.
- Frequent timeouts: gating too strict; mode differs; increase timeout slightly or loosen gate as described.

**Test case: GF naming after acquisition**

Expected same pattern; ideally **one MENUNAME per GF**. Note that some GF naming occurs after battle/EXP screens; this is consistent with players reporting GF rename screens appearing immediately after such sequences (even though that comment is on a Remaster thread, it describes common series behavior and should not be treated as authoritative for Steam 2013). Avoid relying on that for logic.

**Test case: opt-out**

Hold SHIFT during naming:

- `MENUNAME enter` then immediate pass-through (no injection logs), and UI remains interactive.

## Configuration and UX expectations

### UX behavior

When bypass triggers:

- Speak a short announcement once per session: “Naming screen bypassed.” (optional).
- Provide an opt-out hold key (SHIFT by default).
- Provide a toggle hotkey to disable/enable bypass without editing config (F9 suggested).

### Per-save persistence

If you use **AutoAccept** and leave names unchanged, the game should naturally persist default names in the save (no extra work). For configured names and stronger persistence, the save-format doc shows names are stored in explicit fields in the decompressed save. citeturn12view1

If you later implement save write patching:

- Patch these decompressed offsets (Steam save logical offsets):
  - `0x0028` Squall name (12 bytes, 0x00 terminated) citeturn12view1
  - `0x0034` Rinoa name (12 bytes, 0x00 terminated) citeturn12view1
  - `0x0040` Angelo name (12 bytes, 0x00 terminated) citeturn12view1
  - `0x004C` Boko name (12 bytes, 0x00 terminated) citeturn12view1
- Patch GF name field inside each GF record: offset `0x00` within each 68-byte GF block. citeturn12view1  
  This supports “configured default names” even if the UI is bypassed.

## Risk analysis and mitigations

### Compatibility risk: mode gating and unintended confirmations

Risk: confirm injections might dismiss other menus/dialogs.

Mitigations:

- Only inject when:
  - `pending == true` AND
  - `now <= deadline` AND
  - `*pGameMode == MODE_MENU` AND
  - optional: `wasTriggeredByMENUNAMERecently == true`
- Keep the injection window short and throttle presses.

### Risk: `SkipOpcode` breaks side effects

Risk: skipping the `MENUNAME` handler may fail to set internal flags or finalize buffers.

Mitigations:

- Keep `SkipOpcode` experimental.
- Default to AutoAccept.
- Add a “health check” after skip (optional): detect name non-empty if you later find the name buffer, otherwise do not ship skip as default.

### Localization / encoding

Steam 1.2 US names should be safe as ASCII, but the save format does not explicitly state encoding beyond “0x00 terminated.” citeturn12view1  
If you implement configured names:

- Restrict to ASCII in v1.
- Truncate to 11 chars + 0x00 terminator (12 bytes total), and log truncation.

### Interaction with FFNx and other hooks

FFNx is a separate modding platform DLL with its own hooks. Use the same principle as your existing modules:

- Resolve addresses dynamically (you already do this from executable machine-code chains).
- Prefer detouring stable dispatch-table handlers rather than hardcoded absolute addresses (Steam 2013 builds vary).

FFNx repository reference (primary upstream): citeturn13search0

## Actionable next steps checklist

- [ ] Add `opcode_menuname` extern declaration  
  **observed in provided source:** `src/ff8_addresses.h#L225-L234`
- [ ] Resolve `opcode_menuname = pExecuteOpcodeTable[0x129]` and log the resolved address  
  **observed in provided source:** `src/ff8_addresses.cpp#L526-L544`
- [ ] Create `src/name_bypass.h` / `src/name_bypass.cpp` implementing:
  - MinHook detour for `FF8Addresses::opcode_menuname`
  - Pending bounded injection loop gated by `MODE_MENU`  
  (new files; no existing line refs)
- [ ] Register module in core loop:
  - Add `NameBypass::Initialize()`  
    **observed in provided source:** `src/dinput8.cpp#L77-L85`
  - Add `NameBypass::Update()`  
    **observed in provided source:** `src/dinput8.cpp#L108-L130`
- [ ] Add log lines exactly as specified (MENUNAME enter/exit, inject CONFIRM, timeout)
- [ ] Validate `0x129` is actually invoked in your target naming scenarios by checking logs and/or extending existing opcode histogram instrumentation  
  **observed in provided source:** `src/field_dialog.cpp#L92-L179`
- [ ] After validation, optionally add:
  - Config file parsing (INI-style)
  - “SkipOpcode” experimental mode
  - Save-format-based persistence patching using documented 12-byte name fields (future) citeturn12view1

```text
References / links (non-Remaster)

- FFNx repository (upstream): https://github.com/julianxhokaxhiu/FFNx
- FF8 field opcode list (includes 0x129 MENUNAME): https://wiki.ffrtt.ru/index.php/FF8/Field/Script/Opcodes
- Mirror opcode list (includes 0x129 MENUNAME): https://ff7-mods.github.io/ff7-flat-wiki/FF8/Field/Script/Opcodes.html
- FF8 Steam save format (names, GF name fields, LZS compression): https://wiki.ffrtt.ru/index.php/FF8/GameSaveFormat
```