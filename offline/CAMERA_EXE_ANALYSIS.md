# FF8_EN.exe world-map camera + input->movement: definitive EXE analysis

Source: static disassembly of `FF8_EN.exe` (Steam 2013), files in
`Game Files/disassembly/FF8_EN_.text_0x00501000.asm` unless noted.
All addresses are VAs (image base 0x400000). Angle units: 1 turn = 4096 ("au");
0x400 = 90 deg. All angle words wrap mod 4096 (the engine re-normalizes with the
`shr 0xc / shl 0xc` idiom seen throughout).

Everything below was read directly from the binary; each claim carries the
instruction address that proves it.

---

## 1. The one-function core: `sub_557A90` (input -> heading -> camera)

Both world-map tick loops call the same chain once per world-map frame, in this
order (caller excerpts at `0x53FADE..0x53FC08` and `0x540F54..0x541070`):

```
0x559240  poll pad          -> 0x203FDE8[parity]
0x5573C0  build input struct -> 0x203ED50      (or 0x559140 = null input during warp cinematic)
0x548270  sync player struct/locator
0x557A90  INPUT->HEADING + CAMERA-FOLLOW      args: (0x203EE80, 0x203ED50, 0x203FE48, 0x203ECF8)
0x558950  wrap player-struct local coords (+0 and +4 into [-0x800,0x800))
0x552AD0  build render/view camera            args: (0x203FE48, 0x203ECF8, 0x2040130, flag)
0x544490  look-target point (reads camYaw 0x544496, heading 0x54449D)
0x545480  per-vehicle MOVEMENT dispatch (applies heading to position, collision, slide)
```

Key consequence for the mod: **heading is recomputed from input inside
0x557A90, and position is integrated later in the same tick (0x545480).**
An asynchronous write to the heading word is overwritten every frame a
direction key is down; a write performed *between* 0x557A90 and 0x545480 would
stick (needs a hook, not a BAT).

### Argument identification (from push sequence at 0x53FBA0..0x53FBB4)

| arg | value | meaning |
|---|---|---|
| [ebp+8] | `0x203EE80` | player world position (dwords X/Y/Z at +0/+4/+8) |
| [ebp+0xC] | `0x203ED50` | **input struct** (layout in section 3) |
| [ebp+0x10] | `0x203FE48` | **player object struct** (pos words +0/+2/+4, angles pitch/yaw/roll at +8/+0xA/+0xC => heading = 0x203FE52) |
| [ebp+0x14] | `0x203ECF8` | **camera struct** (pos words +0/+2/+4 = 0x203ECF8/FA/FC; angles pitch/yaw/roll at +8/+0xA/+0xC = **0x203ED00 / 0x203ED02 / 0x203ED04**) |

This is why direct writers of `0x203FE52` are rare in the disassembly: most code
addresses it as `[struct+0xA]`.

---

## 2. Verified memory map

| address | size | meaning | writers (proof) |
|---|---|---|---|
| 0x0203EE80 | dword | player world X (0..0x3FFFF, torus wrap 0x40000) | 0x548B7C (spawn), 0x54A1A0 (vehicle swap), movement dispatch (0x53C423 on-foot walker) |
| 0x0203EE84 | dword | player world Y (0..0x2FFFF, wrap 0x30000) | 0x548B89, 0x54A1A9, 0x53C429 |
| 0x0203EE88 | dword | player world Z (height) | 0x548B8F, 0x53C403 |
| 0x0203FE30 | dword | ground height under player | 0x53C482 (from triangle record +0x24) |
| 0x0203FE48 | struct | player object: +0 localX(w), +2 localZ(w), +4 localY(w) (wrapped +-0x800 by 0x558950), +8 pitch, **+0xA heading (0x0203FE52)**, +0xC roll | see below |
| **0x0203FE52** | word | **move heading**, 0..4095; dX=sin(h*2pi/4096)*spd, dY=cos(h*2pi/4096)*spd | 0x54815C (warp snap), 0x548B75 (map reset=0), 0x54A1BD (vehicle exit restore), **0x557D57/0x557D81 (turn +-0x200), 0x557D99 (snap = desired)**, 0x557B2A (vehicle L/R steer via [struct+0xA]), 0x5580E0-area (car road-bias), wrap at 0x558927 |
| 0x0203ECF8 | struct | camera object: +0/+2/+4 pos words, +8 pitch, +0xA yaw, +0xC roll | see below |
| 0x0203ED00 | word | **camera pitch** (0..-0x200 on foot) | 0x558444/0x55844C/0x55846A (-6/frame to -0x200), 0x5584EA (+4/frame toward default 0xC76D28), 0x54D005 (boarding anim), 0x558900/0x558910/0x55891A (Ragnarok: target -0x100 - vertSpeed/12) |
| **0x0203ED02** | word | **camera yaw** (logical AND render yaw) | **complete writer list:** 0x548155 (warp snap), 0x54A1C8 (vehicle exit restore), 0x54CFA7/0x54D030 (boarding cinematic), 0x54D3AC (another transition), **0x558676 / 0x55871A / 0x55873E (+= vel>>3, the follow/manual-rotate integrator), 0x55885E / 0x5588C1 (Ragnarok variant)**, 0x558F61/0x558F6E/0x558F7A (arrival controller: +-0x20/frame toward forced yaw 0xC76D22, snap within 0x20) |
| 0x0203ED04 | word | camera roll | transitions only |
| 0x0203ED50 | 16 bytes | **input struct** (section 3) | 0x5573C0 every frame (0x559140 nulls it during warp cinematics) |
| 0x0203ECE0/E2/E4 | 3 words | camera-rotated input vector (world-space move dir) | 0x557A90 (zeroed 0x557AA4, written by ApplyMatrix call 0x557C3F) |
| 0x0203FDE8 | dword[2] | raw pad state, double-buffered; index = 0x20409BC (0/1) | poll 0x5592A1; low word = buttons (bit layout section 3); high word = trigger copy |
| 0x02040998 / 0x0204099C | dword | analog stick X / Y (0..255, center 0x80) | 0x55930D / 0x55931D |
| 0x020409BC | word | pad buffer parity (flips each poll, 0x559250) | |
| 0x020409E0 | dword | movement/animation mode: 0..9 on-foot states, 0x10-0x16 ?, 0x20-0x28 cars, 0x30 Garden, 0x31 chocobo, 0x32 Ragnarok, 0x40-0x42 ?, 0x80, 0x84 | |
| 0x02036B70 | byte | current vehicle id (0 = on foot, 0xD = special foot mode, 0xC = map-view, 1..8 vehicles) | |
| 0x020409E4 | dword | **camera/movement lock flag** (per warp-entry byte +0x6C, set 0x54A21B; init 0 at 0x542BA0; also vehicle handlers 0x54B4C9/0x54B50F/0x54B7FC/0x54B9C6). When 1, arrival controller drives camera yaw toward **0xC76D22** | |
| 0x0C76D22 | word | **forced camera yaw target** (data block; engaged when 0x20409E4==1, applied at 0x558F23..0x558F7A) | not written from .text (config/region data) |
| 0x0C76D28 | word | default camera pitch target | data |
| **0x0204DAE8** | word | **camera yaw angular velocity** (on-foot/cars), clamp +-0x80; yaw += vel>>3 | 0x558666/0x558710/0x558734 |
| 0x0204DAE4 / 0x0204DAE6 | word | Ragnarok camera velocity pair | 0x558820/0x55882A/0x558852, 0x5588B7 |
| 0x0204DAE0 | dword | camera idle timer (set 0x3C; decremented by input activity 0x558507/0x558514/0x558521; gates pitch recovery) | |
| **0x020409EC** | word | **per-triangle heading bias** from triangle record +0x28 (0x53C48E); on-foot desired heading += bias>>1 (0x557C7E..0x557C92); cars: heading += bias>>3 per frame (0x5580CE..0x5580DC); zeroed on relocate (0x5445F9) and init (0x542BB9) | |
| 0x020409E8 | dword | vehicle throttle/speed accumulator (0x557E5D..0x557E6D) | |
| 0x0204DAD8 | dword | chocobo(0xD) auto-run target heading (0x557F39) | |
| 0x0203FD5C | dword | **scripted-camera pointer** (nonzero => keyframed camera anim overrides everything, 0x552ADF -> 0x54BCB0) | 0x548BA3 (=0 on reset), 0x54CC76 (=0), 0x54CCF6/0x54CD34 (= random entry from anim table [0x2040960], vehicle cinematics) |
| 0x0203FCF8/FC/D00 | dwords | smoothed camera target point (torus-aware smoothing 0x552B46..0x552BDA: t=(t+p)/2 per frame) | |
| 0x02040968/6A/6C | 3 words | render-camera extra angle triple (terrain-tilt adjustments 0x552C1F/0x552C2E, composed into view matrix 0x552C9F) | |
| 0x02040130 | struct | render camera object (matrix at +0x10, built by 0x552AD0) | |
| 0x0C75CF8 | dword | **minimap display mode 0..3** (Select-button cycle at 0x557A05..0x557A18) — *not* a camera zoom | |
| 0x02036B6C | dword | pointer to 12-byte spawn records: {X dw, Y dw, Z w, camYawByte, headingByte}; warp snap does yaw=byte<<4, heading=byte<<4 (0x548142..0x54815C) | |

Math helpers (all verified):
- `0x56D100` cos: returns `cos(a*2pi/4096)*4096` (fmul [0xB6B980]=2pi/4096, fmul [0xB69540]=4096.0)
- `0x56D130` sin: same, sine
- `0x56D160` ratan2(a,b): returns `atan2(a,b) * 4096/(2pi)` (fpatan, fmul [0xB6B984]), signed -2048..2047
- `0x56CE30` RotMatrix(angles{pitch,yaw,roll}, m): m = RotZ(roll)*RotX(pitch)*RotY(yaw); RotY (0x56D020) = [[c,0,s],[0,1,0],[-s,0,c]] (PSX convention)
- `0x56C4F0` ApplyMatrix(m, v, out): out = m*v / 4096

---

## 3. Input pipeline

### Poll (`0x559240`, once per world-map frame)
Flips parity `0x20409BC`, reads engine input API (`0x49ED30`, `0x4A2D60`,
`0x49F0A0`) and stores a dword at `0x203FDE8[parity]` (0x5592A1). Low word =
button bits, PSX layout:

- byte1 (bits 8..15): 0x01 SELECT (cycles minimap mode 0xC75CF8, 0x5579CD..0x557A18),
  **0x10 UP, 0x20 RIGHT, 0x40 DOWN, 0x80 LEFT**
- byte0 (bits 0..7): 0x04 / 0x08 = **camera-rotate pair** (L/R shoulder slots;
  0x04 -> input+0xE=+0x7F, 0x08 -> input+0xE=-0x7F; 0x557622..0x557630),
  0x80 = **camera-hold** (input+0xA=1, 0x55760F..0x557614), 0x10/0x80 also serve as
  vehicle brake/throttle in the vehicle branch (0x557461..0x557471)

Analog stick: X at `0x2040998`, Y at `0x204099C` (0..255, center 0x80,
deadzone 0x2D).

### Fill (`0x5573C0`) -> input struct `0x203ED50`

| off | type | meaning | on-foot source (branch 0x557562) |
|---|---|---|---|
| +0 | int16 | inX (**+ = screen RIGHT**) | RIGHT: +v, LEFT: -v; stick: stickX-0x80 |
| +2 | int16 | inY (vertical, unused on foot) | 0 |
| +4 | int16 | inZ (**+ = screen UP/forward**) | UP: +v, DOWN: -v; stick: 0x7F-stickY |
| +8 | byte | move-input present flag | 1 if any dpad/stick (0x557586/0x557600) |
| +9 | byte | (cleared) | |
| +0xA | byte | **camera-follow inhibit** | 0 while moving (0x55760B); 1 if idle (default 0x557411, sticky 0x557651), 1 if camera-hold button (0x557614), 1 while a large turn is in progress (set by 0x557D51/0x557D7B), 1 for one frame after manual rotate (0x55764A) |
| +0xB | byte | vehicle throttle axis | |
| +0xC | byte | vehicle strafe? (analog) | |
| +0xD | byte | vehicle turn axis (L/R steer; on foot unused) | |
| +0xE | byte | **manual camera rotate axis** (+0x7F / -0x7F) | |

Cardinals put +-0x7F (127) in one axis; diagonals +-0x5A (90 ~ 127/sqrt2)
in both (0x55757A..0x557581) — i.e. an approximately unit 8-way vector.

During warp/arrival cinematics the loop calls `0x559140(input, 1)` instead:
all zero, +0xA=1 (input dead, camera frozen).

---

## 4. INPUT -> MOVEMENT: the exact transform (answers Q1)

Inside `0x557A90`, on-foot/chocobo path (mode 0..9, 0x80, 0x31), executed only
when `input+8 != 0`:

```
0x557BB9  in = (inX, inY, inZ) from input struct        ; scaled: on-foot /4 (<<4 >>6), chocobo /2
0x557C0D  ang = camera.angles (0x203ED00/02/04); pitch=0, roll=0   ; keep ONLY yaw 0x203ED02
0x557C2D  M   = RotY(camYaw)                            ; call 0x56CE30
0x557C3F  wv  = M * in            -> 0x203ECE0/E2/E4    ; call 0x56C4F0
0x557C79  a   = ratan2(-wv.z, wv.x)                     ; call 0x56D160
0x557C7E  bias= [0x20409EC] >> 1                        ; per-triangle heading bias
0x557C92  desired = a + bias + 0x400                    ; wrapped mod 4096
0x557CEC  delta = wrapSigned(desired - heading)         ; heading = 0x203FE52
if |delta| <= 0x100:  heading = desired                 ; SNAP (0x557D90..0x557D99)
else:                 heading += sign(delta)*0x200      ; turn 512 au/frame (0x557D3E..0x557D85)
                      input.+0xA = 1                    ; inhibit camera follow while turning
```

Working out `a + 0x400` (RotY = [[c,0,s],[0,1,0],[-s,0,c]], so
wv.x = inX*cos+inZ*sin, wv.z = -inX*sin+inZ*cos):

**desired_heading = camYaw + keyOffset + triBias/2 (mod 4096)**

| key(s) | (inX,inZ) | keyOffset |
|---|---|---|
| UP | (0,+) | **0** |
| UP+RIGHT | (+,+) | **+0x200 (512)** |
| RIGHT | (+,0) | **+0x400 (1024)** |
| DOWN+RIGHT | (+,-) | +0x600 |
| DOWN | (0,-) | +0x800 (2048) |
| DOWN+LEFT | (-,-) | +0xA00 |
| LEFT | (-,0) | +0xC00 (3072) |
| UP+LEFT | (-,+) | +0xE00 (3584) |

i.e. exactly `heading = camYaw + k*512` with k counted **clockwise from UP**
(clockwise = increasing heading = N->E->S->W with dX=sin(h), dY=cos(h)).
Analog stick gives continuous offsets: `keyOffset = ratan2(-inZ, inX) + 0x400 =
atan2_cw_from_up(inX, inZ)`.

- There is NO table; it is a real atan2 of the camera-rotated vector.
- It is written **incrementally** when the change is large: snap if within
  0x100 (~8.8 deg), else 0x200/frame (max 4 frames to reverse).
- The ONLY camera variable used is the yaw word **0x203ED02** (pitch/roll
  zeroed before the matrix build, 0x557C25/0x557C29).
- Extra term: `[0x20409EC]/2` — a per-walkmesh-triangle bias (field +0x28 of
  the current triangle record, latched at 0x53C48E; slope/road deflection).
  0 on ordinary flat triangles; **log it** (see section 8).
- Vehicles (cars 0x20-0x28/0x84, Garden 0x30, Ragnarok 0x32) don't use this;
  they steer tank-style: `heading += (-input.turn)>>2` (>>3 while camera far
  off) at 0x557B0A..0x557B2A, plus per-frame road bias `[0x20409EC]>>3`
  (0x5580CE).

### Heading -> position

Candidate-step builder `0x53EC50`: speed = `0x546E10(mode 0x20409E0)`
(jump-table of per-mode base speeds), then `0x53DA20(entity, ..., speed,
heading[read 0x53ECA5], pos)` computes `newpos = pos + RotY(heading + wobble) *
(0,0,spd)` => `dX = sin(h*2pi/4096)*spd, dY = cos(h*2pi/4096)*spd` (matches
prior finding). Torus wraps inside 0x53DA20: X mod 0x40000 (0x53DB40..0x53DB55),
Y mod 0x30000 (0x53DB1F..0x53DB2C). The validator/slide (0x53E7A0 family,
called from the movement dispatch 0x545480) then accepts, rejects, or slides
the step along blocking edges — see section 7 (this is what poisoned the live
measurements).

---

## 5. CAMERA STATE & PER-FRAME UPDATE (answers Q2)

Camera struct `0x203ECF8`: pos words +0/+2/+4 (rarely used), **pitch 0x203ED00,
yaw 0x203ED02, roll 0x203ED04**. `0x203ED02` is BOTH the logic yaw (input
transform) and the render yaw (view matrix, section 6). There is no separate
"target yaw" variable; the target is the player heading, tracked through a
**velocity**, not a direct lerp:

### On-foot follow physics (inside 0x557A90, 0x55858B..0x558745)

```
delta = wrapSigned(heading[0x203FE52] - yaw[0x203ED02])     ; 0x55858B/0x558592
rot   = input.+0xE          ; manual rotate axis
vel   = [0x204DAE8] (int16)

if rot > 0:            vel -= 8                      ; 0x5586FC (rotate key: yaw decreases)
elif rot < 0:          vel += 8                      ; 0x558633
elif input.+0xA != 0:  vel = (|vel|<8 ? 0 : vel*3/4) ; follow inhibited -> decay (0x558649..0x55869D)
else:                                                 ; auto-follow toward heading
    if |delta| < 0x80:          vel = vel*3/4        ; close enough -> decay (0x558696)
    elif 0x800-|delta| < 0x80:  vel >>= 1            ; ~180deg ambiguous -> damp (0x5586DE)
    elif delta >= +0x80:        vel += 8             ; 0x5586F0
    else (delta <= -0x80):      vel -= 8             ; 0x5586FC

vel = clamp(vel, -0x80, +0x80)                        ; 0x558706..0x558734
yaw += vel >> 3                                       ; 0x558676 / 0x55871A / 0x55873E
```

So: max yaw rate 16 au/frame, acceleration 1 au/frame^2, and the camera only
chases while the player is actually moving (input.+0xA==0) and stops inside a
+-0x80 (7 deg) deadband of the heading. **The old model
`yaw += clamp(wrap(heading-yaw),+-0x80)>>3` is wrong in detail** — same
address, but it is `yaw += clamp(vel)>>3` with `vel` a ramped/decayed velocity;
update `ff8_walkmesh.py::camera_lerp` accordingly.

### Pitch (0x203ED00)
On foot (modes 0..9/0x80/0x31): -6/frame, floor -0x200 (0x558436..0x55846A).
Recovery +4/frame toward default `[0xC76D28]` only after idle timer
`0x204DAE0` (set 0x3C, decremented by input activity) expires
(0x5584CD..0x5584F3). Ragnarok: pitch target `-0x100 - [0x203FE4A]/12`
(0x5588D1..0x55891A). There is **no player zoom variable** on foot; the orbit
distance is baked into the view construction (0x552AD0), and `0xC75CF8`
(Select cycling 0..3) is the minimap mode, not camera zoom.

---

## 6. RENDER camera / view matrix (answers Q3 "which yaw feeds it")

`0x552AD0(player, cam, 0x2040130, flag)`:

1. **Scripted-camera bypass**: if `flag==0 && [0x203FD5C]!=0`, a keyframed
   animation (`0x54BCB0`, frame counter 0x2040304) drives the view AND writes
   the camera angle triple (it receives `cam+8` = 0x203ED00 as an output,
   0x552B17). `0x203FD5C` is set from an animation table at `[0x2040960]`
   for vehicle-boarding cinematics (0x54CC60) and cleared on reset (0x548BA3).
2. Normal path: smoothed target `0x203FCF8/FC/D00` moves halfway to the player
   each frame with torus wrap (0x552B46..0x552BDA); view matrix =
   `RotMatrix(0x203ED00 pitch/yaw/roll)` (0x552C66..0x552C6B) composed with the
   terrain-tilt triple `0x2040968/6A/6C` (0x552C79..0x552C9F) and the
   translation; rows negated for the view inverse (0x552CDE..0x552CE9); result
   into render object `0x2040130`.

So **0x203ED02 IS the render yaw** (plus small terrain-tilt adjustments),
except while a scripted camera (0x203FD5C) is active.

### Player camera controls (on foot)
- **Rotate**: buttons pad-bits 0x0004/0x0008 -> input+0xE -> `vel -= / += 8`
  per frame (section 5). Releasing decays vel (x3/4/frame).
- **Camera-hold**: pad bit 0x0080 -> input+0xA=1 -> follow inhibited.
- **No zoom on foot**; Select (bit 0x0100) cycles the minimap 0..3.
- Vehicles: input+0xD steers the vehicle itself; Ragnarok has its own
  camera-velocity pair (0x204DAE4/6, 0x55874A..0x5588C1).

### Fixed / forced per-region camera
Two distinct mechanisms (both seen by the mod as "fixed camera"):
1. `0x20409E4 == 1` (per warp-entry byte +0x6C): an arrival controller
   (0x558F17..0x558F7A, state bits 0x20409D8) turns yaw +-0x20/frame toward the
   **forced yaw at 0xC76D22** and snaps within 0x20. The input transform still
   uses 0x203ED02, so movement stays consistent with the frozen view.
2. `0x203FD5C != 0`: scripted keyframe camera (cinematics).

### Warp snap (verified)
Spawn routine at 0x548110..0x54815C: 12-byte record from `[0x2036B6C] + 12*idx`
= {X dword, Y dword, Z word, camYawByte, headingByte};
`0x203ED02 = yawByte<<4` (0x548155), `0x203FE52 = headingByte<<4` (0x54815C).
Vehicle exit restore: 0x54A1BD/0x54A1C8 (heading and yaw from the parked-record
fields +0xA/+0x60).

---

## 7. Reconciling the live "3556 anomaly" (answers Q4)

Live data: wedged against a mountain wall, `0x203ED02` read a constant 3556;
key UP (k=0) produced measured world-motion bearing ~104 (also seen ~4034);
k=7 produced ~2069. The mod predicted `motion = camYaw + k*512`.

**The formula `heading = camYaw + k*512` (k clockwise from UP) is exactly what
the EXE computes (section 4). The measurements were poisoned because the mod
measured MOTION bearing while wedged: the collision validator slides blocked
steps along the wall edge, so measured motion is the WALL direction, not the
heading.** Check the numbers against a single wall of direction ~104/2152 au:

- k=0: heading = 3556. Angle to wall dir 104: wrap(3556-104) = -644, |644| < 1024
  => slides along **104**. Measured ~104 / ~4034 (noise across 0). MATCH.
- k=7 (UP-LEFT if k counted clockwise... with the mod's k orientation giving
  3556+7*512 = 3044): angle to 104 is 1156 (>1024) => slides along the
  opposite sense **2152**; measured ~2069 (2152-83, quantization + corner
  geometry). MATCH.

One wall direction (~104) explains BOTH samples simultaneously, including the
"inconsistent between keys" complaint — different desired headings project onto
the two opposite senses of the same wall line. No second camera variable, no
handedness flip, and no FFNx-side transform is needed.

Why 3556 stayed constant: the camera only chases while `input.+0xA==0` AND it
stops within +-0x80 of heading; while wedged/turning/holding extra buttons the
velocity decays to 0 (section 5) — and if that region had `0x20409E4==1`, yaw
is actively held at the forced value `[0xC76D22]` (=3556 = -540 mod 4096,
plausibly a scenery-facing region yaw). Both are consistent with a frozen 3556.

Residual uncertainty (cheap to close): the `k` orientation (is the mod's k=1
UP-RIGHT or UP-LEFT on screen) and the `0x20409EC` bias were not observable in
the contaminated data. One logging BAT resolves everything:

**1-BAT logging plan** — sample per frame, on open flat ground (NOT wedged),
tapping each of the 8 keys ~1s each:
1. `0x203FE52` (heading, word) — primary truth; compare to camYaw+k*512
2. `0x203ED02` (camYaw, word)
3. `0x20409EC` (triangle bias, word) — expect 0 on flat ground
4. `0x204DAE8` (camera velocity, int16)
5. `0x203ED50` bytes +0..+0xE (input vector actually seen by the engine)
6. `0x20409E4` (lock flag), `0x203FD5C` (scripted cam), `0x20409E0` (mode),
   `0x2036B70` (vehicle id)
7. `0xC76D22` (forced yaw) — equals 3556 in that region if hypothesis 2 holds

Expected: `0x203FE52 == 0x203ED02 + k*512 + [0x20409EC]/2` exactly (after the
<=4-frame turn transient). If the lateral keys come out mirrored vs the mod's k
convention, flip the sign of k in the mod (engine: RIGHT=+1024 with
heading-clockwise = N->E->S->W in dX=sin/dY=cos world coords).

---

## 8. Simulator spec (difference equations for the Python sim)

State (per frame; all angles int mod 4096, positions int):
```
X, Y            # world pos, wrap X mod 0x40000, Y mod 0x30000
h               # heading      (0x203FE52)
cy              # camera yaw   (0x203ED02)
cv              # camera yaw velocity (0x204DAE8), clamp [-128,127]
cp              # camera pitch (0x203ED00)  [cosmetic]
bias            # per-triangle bias (0x20409EC), from triangle field +0x28 (0 on most ground)
follow_inhibit  # bool: no move-input, or camera-hold button, or |turn delta|>0x100 this frame
```

Per frame, with key vector (ix,iz) in {(-127..127)}, cardinals 127 / diagonals 90:

```
if move_input:
    off     = atan2_units(ix, iz)            # 0 for (0,+), +1024 for (+,0)  [= ratan2(-z',x')+0x400 folded]
    desired = (cy + off + bias//2) & 0xFFF
    d       = wrap_signed(desired - h)       # [-2048, 2047]
    if abs(d) <= 0x100: h = desired
    else:               h = (h + (0x200 if d > 0 else -0x200)) & 0xFFF; follow_inhibit = True
    # movement (only when move_input): step spd along h, then validator/slide gate
    X = (X + sin4096(h)*spd//4096) % 0x40000     # engine: sin(h*2pi/4096)*spd
    Y = (Y + cos4096(h)*spd//4096) % 0x30000
# camera
d2 = wrap_signed(h - cy)
if   rot_key > 0:      cv -= 8
elif rot_key < 0:      cv += 8
elif follow_inhibit:   cv = 0 if abs(cv) < 8 else cv*3//4
elif abs(d2) < 0x80:   cv = cv*3//4
elif 0x800-abs(d2) < 0x80: cv //= 2
else:                  cv += 8 if d2 >= 0x80 else -8
cv = clamp(cv, -0x80, 0x80)
cy = (cy + (cv >> 3)) & 0xFFF                # arithmetic shift (rounds toward -inf)
```

Also model: warp snap sets h and cy from the spawn record (both may differ);
regions with lock flag pull cy toward the forced yaw at 0x20/frame. Speed spd =
mode table 0x546E10 (on-foot value matches the ~40 u/frame already calibrated
in the sim; keep the empirical value). The step gate/slide from
NAV_SIM_FINDINGS stays as implemented; note that when the gate blocks, actual
motion = projection onto the blocking edge (this is what the sim must produce
for measured-bearing parity with live logs).

---

## 9. Control spec (exact write targets for auto-drive) (answers Q5)

**Why writing 0x203FE52 alone did not aim (the .156 BAT result):** while any
direction key is held, `0x557A90` recomputes `0x203FE52` from
`camYaw + keyOffset (+bias/2)` every tick BEFORE the movement dispatch
(0x545480) consumes it — the overwrite is at 0x557D57/0x557D81/0x557D99. With
no key held there is no overwrite, but there is also no motion: on-foot motion
only occurs when `input+8 != 0` (the whole vector path is inside
`test cl,cl / je` at 0x557BB1); there is no coasting velocity on foot.

**Recommended (no hooks): steer through the camera.** Each frame:
1. write `0x203ED02` (word) = desired world bearing (engine units, 0=N
   convention identical to heading);
2. write `0x204DAE8` (word) = 0 (kill follow velocity so the engine doesn't
   drift the yaw you just wrote; its max correction is only +-16/frame anyway);
3. hold UP (synthetic key, as today).

The engine itself then sets `heading = camYaw + 0 + bias/2` (snap within
0x100, else 512/frame — worst case 4 frames), and all collision/slide logic
runs untouched. The render camera equals the travel direction, which is also
the correct accessibility behavior. Residual error = `bias/2` from
`0x20409EC`: read it each frame and pre-subtract (`write = desired - bias/2`),
or ignore it and closed-loop correct (read back `0x203FE52` and trim the next
camera write by the observed difference).

Caveats:
- If `0x20409E4 == 1` (region lock), the arrival controller fights the write at
  +-0x20/frame between your writes; either also write `0xC76D22` = desired, or
  write `0x20409E4 = 0` for the drive duration (restore after). Check
  `0x203FD5C == 0` (scripted camera) before trusting the view.
- Do not drive during warp cinematics (`input` is nulled via 0x559140; detect
  via `0x203ED50+0xA == 1` with +8==0 while your UP is down, or the warp timer
  0x203FD54/0x2040A58 nonzero).

**Alternatives:**
- Write `0x203FE52` from a mid-frame hook placed after 0x557A90 and before
  0x545480 (e.g., trampoline at 0x53FBB9/0x541021, the `0x558950` call site)
  — sticks even with keys held; camera will then follow it naturally.
- Write the input vector `0x203ED50` +0/+4 (int16, magnitude ~127) with +8=1:
  gives continuous analog-precision aiming (`off = atan2_units(inX,inZ)`), but
  it is rebuilt by 0x5573C0 each tick, so this also needs the mid-frame hook
  (between the 0x5573C0 and 0x557A90 call sites) or a patched stick state
  (0x2040998/0x204099C = 0x80 + 127*sin/cos of the desired camera-relative
  offset, which the filler converts for you — stick writes survive because the
  poll reads hardware each frame, so write these two dwords every frame).
- Camera snap only (cosmetic): single write to 0x203ED02 (+ zero 0x204DAE8),
  exactly what the warp routine does.

Character position for teleports: 0x203EE80/84/88 plus the locator/refresh path
(0x544570) — unchanged from prior findings.

---

## 10. Corrections to prior docs

- CAMERA_STEERING.md: "LEFT/RIGHT add to the heading (turn)" is true only for
  VEHICLES (input+0xD, 0x557B2A). On foot, arrows are screen-relative through
  the atan2-of-rotated-vector transform (section 4). The old
  `camera_lerp(yaw, heading)` (direct clamped lerp) must become the
  velocity-based model of section 5.
- "Camera yaw is purely visual": mostly true for a heading-writing strategy,
  but it is an INPUT to the heading whenever a direction key is held — which is
  why the camera-write strategy of section 9 is the cleanest lever.
- Heading convention confirmed: dX=sin(h)*spd, dY=cos(h)*spd, warp records
  store heading/yaw as byte<<4; UP maps motion to exactly camYaw.
