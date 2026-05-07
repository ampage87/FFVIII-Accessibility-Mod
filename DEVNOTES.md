**Purpose & context**

Aaron is the sole developer of the FF8 Accessibility Mod — a `dinput8.dll` injection for Final Fantasy VIII Steam 2013 edition (App ID 39150, FF8_EN.exe + FFNx v1.23.x) that makes the game playable for blind players via Windows SAPI text-to-speech, navigation assistance, and entity catalogs. Aaron is blind himself and is the primary tester, using NVDA as his screen reader. The mod is open-source at `github.com/ampage87/FFVIII-Accessibility-Mod`.

**Project root:** `C:/Users/ampag/OneDrive/Documents/FFVIII-Accessibility-Mod/FF8_OriginalPC_mod/`

---

**Current build: v0.14.102 BAT-PASSED. Awaiting ChatGPT deep research before v0.14.103.**

**v0.14.102 BAT result (Wed 2026-05-07):** PASS. Aaron: "AD did work to move the car now!" The v0.11.13/v0.11.14 design (A=gas held continuously alongside arrow steering) is fully restored. A=gas (scan 0x1E, NOT extended) is still the correct binding.

---

**Aaron's v0.14.103 decision:**

**One bigger build that knocks all four priorities out together (except fuel, deferred).** Fuel awareness is skipped for now since A* already optimizes path length, which approximates optimal-fuel routing. If AD turns out to be indirect enough that fuel becomes a problem, fallback is to disable fuel consumption / give the player unlimited fuel rather than route-plan around it.

**Behavioral clarifications from Aaron:**
- **Cars CAN enter "Balamb-style" towns** (Balamb, Dollet, Deling City, Esthar). The engine auto-dismounts the player when the car enters the town and the field loads normally.
- **Cars CANNOT enter "Garden-style" locations** (Balamb Garden and similar). The car physically bounces off the location's collision walls — it just stops short.

This simplifies arrival detection: AD doesn't need a per-location car-entry table. The natural behavior handles both cases:
- Car-friendly town → engine fires field transition → existing arrival path runs.
- Non-car-friendly location → car bounces, AD's stuck-detection fires within the "near target" radius → announce *"Arrived near [Location]. Dismount and walk to enter."* and stop.

---

**v0.14.103 scope (single coherent build):**

1. **Vehicle detection** — find a reliable runtime "in-vehicle" flag; the byte at `0x02040A5E` reads 6 (foot) even in the rental car (confirmed by v0.14.101 BAT log).
2. **Car position runtime address** — separate from foot character at `0x0203EE80/84/88`; while in car the foot position freezes.
3. **Forest avoidance for car AD** — extend `s_terrainGrid[][]` from 2-state (land/ocean) to 3-state (land/forest/ocean); `IsSegmentTraversable` returns false for forest when vehicle == VEH_CAR. Existing terrain enum (per `Plan & Research Documents/World Map Terrain and Locomotion Reference.md`) puts forest at values 0–5.
4. **Separate car AD path** — once vehicle detection works, AD reads car position for steering/distance/arrival. SetDriveKeys behavior is already correct (A always injected; harmless on foot).
5. **Arrive-near-location announce for non-car-friendly locations** — stuck-near-target while in car → "Arrived near [Location]. Dismount and walk to enter."

---

**Deep research prompt drafted:**

Saved at `Plan & Research Documents/Vehicle state and car position deep research prompt.md`. Three research questions:

1. Runtime address of the vehicle-state flag (current vehicle being piloted, separate from the unreliable `0x02040A5E` locomotion byte).
2. Runtime address of the car's world-map position (separate from the frozen foot position).
3. Data source for per-location car-entry capability (per-polygon ENTERABLE flag in wmx.obj? per-trigger-program flag in wmsetus Section 8? hardcoded engine list?).

The prompt includes all confirmed runtime addresses, the savemap WORLDMAP struct layout, the locomotion enum, the terrain enum, and the SAVEMAP HEADER CORRECTION (76-byte header, not 96). It also asks ChatGPT to verify or refute the prior-research caveat that PC version may omit position arrays from the runtime savemap struct.

**Aaron's next step:** Paste the prompt into ChatGPT deep research mode. When results return, paste them back; I'll ship v0.14.103 integrating all four pieces.

---

**Files changed in v0.14.102 (BAT-confirmed):**
- `src/world_map.cpp` — PressKey/ReleaseKey extended param, s_keyGasHeld state, SetDriveKeys A injection, ReleaseAllDriveKeys A release, removed [DIAG-LOCO] calls, file-header CURRENT STATE block updated.
- `src/ff8_accessibility.h` — FF8OPC_VERSION 0.14.101 → 0.14.102.

---

**GitHub push state:**

`main` HEAD = `77e6ef28` (v0.14.98). Local at v0.14.102 (four ahead, all BAT-PASSED).

**Recommended bundle push:** v0.14.99 + v0.14.100 + v0.14.101 + v0.14.102 as ONE commit (all related world-map AD work):
- v0.14.99: sweep-abort placement fix
- v0.14.100: Balamb Town refined-coord baseline + bearing final-approach
- v0.14.101: car-no-movement diagnostic
- v0.14.102: A=gas key restoration (car AD fix)

Suggested commit message for the bundled push (Aaron uses `Utilities/push_to_github.bat`):

```
v0.14.102

World map auto-drive: Balamb Town arrival from any save state, plus
restored car AD (v0.14.99 + v0.14.100 + v0.14.101 + v0.14.102)

Four builds bundled. v0.14.99 fixed an unreachable sweep-abort safety;
v0.14.100 added Balamb Town's refined-coord baseline plus bearing-based
final-approach steering; v0.14.101 added a one-shot diagnostic that
identified the car-AD-no-movement issue; v0.14.102 restored the A=gas
key injection from the v0.11.13/v0.11.14 era that the v0.14.x rewrite
had silently dropped.

v0.14.99 -- Sweep-abort placement fix

The v0.14.97 sweep-abort-on-drift safety was placed in the else branch
of "if (isOnFoot && dist < DRIVE_FINAL_APPROACH_DIST)" in
UpdateAutoDrive. The sweep state machine at the top of UpdateAutoDrive
has an early-return that fires BEFORE the final-approach branch is
ever evaluated, so when sweep was active the abort was unreachable.
Moved the abort check to before the sweep state machine. Removed the
now-dead abort copy in the final-approach else branch.

v0.14.100 -- Balamb Town refined-coord baseline + bearing final-approach

The v0.14.99 BAT exposed that Balamb Town's catalog (13249,-26779) is
the world-map ICON CENTER, not the actual gate trigger. Prior BATs
captured refined entry coords showing the gate is ~350 west / 70 north
at (12896,-26711). When AD targeted the catalog and the player was
sitting at the catalog with arbitrary save-induced heading, walk-
forward went a random direction.

Two integrated changes:
(1) Hardcode Balamb Town's refined-coord baseline at (12896,-26711)
in Initialize. Bootstraps the refined-coord system on fresh sessions
until persistence ships (v0.15.x).
(2) Bearing-based final-approach steering for the 200..1000 unit
range. Below 200 units keeps walk-forward (avoids near-target
oscillation). New constant FINAL_APPROACH_FORWARD_DIST = 200.

v0.14.100 BAT-PASSED Wed 2026-05-06 18:22-18:25. Drive arrived at
Balamb Town with [DRIVE] Arrival via game-mode (mode=1 MODE_FIELD,
fieldId=0x006A, fieldName='bcgate_1', dist=65) after surviving 3
random encounters and the sweep-abort firing once mid-drift.

v0.14.101 -- Diagnostic build for car-AD-no-movement

Same v0.14.100 BAT also tested car AD: rented a car in Balamb,
attempted drive to Balamb Garden, all 6 stuck checks read "moved 0
units in 3000ms window". v0.14.101 added two log lines emitting the
locomotion byte and player position at drive start and at every stuck
check, to distinguish "engine ignoring keystrokes" from "moving
sub-threshold" from "byte transitioning mid-drive."

v0.14.102 -- Restore A=gas key injection (car AD fix)

The v0.14.101 BAT showed locomotion=6 (Selphie foot) AND player
position frozen at (16031,-26948) for all 5 stuck windows -- yet
Aaron was demonstrably in the car (engine running sound). Aaron's
reminder that "car AD was working before the whole Sonnet regression"
prompted a conversation_search of past Claude sessions. Recovered the
v0.11.13/v0.11.14 design from a previous session's empirical F12 key-
state diagnostic:

- A key (VK=0x41, scan 0x1E) is the gas pedal -- must be HELD
  continuously like a real gas pedal.
- W key (VK=0x57, scan 0x11) is reverse.
- Arrow keys steer ONLY -- they don't accelerate the car.
- A and W are NOT extended keys (no KEYEVENTF_EXTENDEDKEY); arrow
  keys ARE extended.
- v0.11.14 always injected A whenever wantUp=true: harmless on foot
  (A unbound) and essential for car. The v0.14.x rewrite during the
  "Sonnet regression" lost this entirely.

FIX: PressKey/ReleaseKey gain an "extended" parameter (default true
preserves all arrow-key call sites). New s_keyGasHeld state. SetDrive-
Keys injects A (scan 0x1E, NOT extended) alongside UP arrow.
ReleaseAllDriveKeys also releases A. Removed v0.14.101 [DIAG-LOCO]
logging since we now have the answer.

Not shipping W (reverse) yet -- forward-only AD doesn't need it; the
v0.11.14 forest-stuck recovery is deferred to v0.14.103+.

v0.14.102 BAT-PASSED Wed 2026-05-07. Aaron: "AD did work to move the
car now!" Car AD restored after the regression.

LESSONS

- For feature regressions where Aaron mentions "used to work before
  the Sonnet regression," ALWAYS run conversation_search on past
  Claude sessions BEFORE writing new logic. Pre-Sonnet builds may
  have already solved the problem empirically -- recreating wastes
  BAT cycles. New persistent rule.
- Place safety checks BEFORE early-returns, not in branches the
  early-return skips. The v0.14.97 sweep-abort was unreachable for
  exactly the case it was meant to handle.
- Catalog (X,Y) coords are world-map icon centers, NOT trigger zones.
  Refined-coord steering with a hardcoded baseline (until persistence
  ships) is essential for narrow-gate locations.

NO new addresses, NO new hooks, NO build script changes.

DEFERRED to v0.14.103+:

- Vehicle detection / separate car AD path (needs runtime address for
  the live "in-vehicle" flag + car world-map position; ChatGPT deep
  research prompt drafted at Plan & Research Documents/Vehicle state
  and car position deep research prompt.md).
- Forest avoidance for car AD (extends terrain grid; ships once
  vehicle detection lands).
- Arrive-near-location announce for non-car-friendly destinations
  (cars bounce off Garden-style location walls).
- v0.15.x persistent accessibility settings (refined-coord
  serialization first slice).
- Empty-path refined-coord steering for narrow-gate locations.
- Cosmetic: "Entered final approach zone" log spam edge-detection.
- Audit other story-gated programs for disassembler scope errors.
```

---

**Deferred queue (post-v0.14.103):**

1. Fuel awareness (low-fuel warning, route weighting toward roads). Backup plan: disable fuel consumption.
2. v0.15.x persistent accessibility settings (refined-coord serialization).
3. Empty-path refined-coord steering for narrow-gate locations.
4. Final-approach log-spam edge-detection.
5. Audit other story-gated programs.
6. Remove party members from field entity catalog.
7. Issue #27 (SeeD rank R key).
8. X-ATM092 chase scene.
9. Walk-and-talk dialog gap.
