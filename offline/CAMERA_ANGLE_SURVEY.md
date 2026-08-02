# FF8 field camera angles — offline extraction from `field.fs`

Extracted 2026-08-01 from `Data/lang-en/field.fs` (894 of 900 fields carry a `.ca`).
Parser validated against the mod's own `[CA]` runtime log lines — `gpbig1a` 38.32, `bgryo1_4` 46.60,
`bghall_1` 7.73, `bg2f_1` 65.38, `bgroom_1` −62.49, `bghall_4` 23.80 all match exactly.

Full table: `CAMERA_ANGLES.csv`.

## 1. FF8 cameras are NOT near-axis-aligned

The 90° quantizer (v0.17.5) rests on the idea that camera angles round cleanly to a world cardinal.
Across all 894 fields they do not — the distribution of discarded angle is close to uniform:

| discarded | fields | cumulative |
|---|---|---|
| 0–5° | 314 | 35.1% |
| 5–10° | 95 | 45.7% |
| 10–15° | 94 | 56.3% |
| 15–20° | 69 | 64.0% |
| 20–25° | 84 | 73.4% |
| 25–30° | 74 | 81.7% |
| 30–35° | 57 | 88.0% |
| 35–40° | 64 | 95.2% |
| 40–45° | 43 | 100.0% |

Only **35%%** of fields are within 5° of a cardinal. **44%%** discard more than 20°.
The quantizer is doing heavy lifting on most of the game, not tidying up a rounding error.

## 2. The threshold is not 45° — it is somewhere in (23.8°, 38.32°]

Ground truth available today:

| field | angle | current snap | verdict |
|---|---|---|---|
| `bghall_1` | 7.73° | 0° | works |
| `ggroom1` | 17.55° | 0° | works |
| `ggsta1` | 19.80° | 0° | works |
| `bghall_4` | 23.80° | 0° | works |
| **`gpbig1a`** | **38.32°** | **0°** | **broken — empirically needs 90°** |
| `bgryo1_4` | 46.60° | 90° | works |
| `bg2f_1` | 65.38° | 90° | works |
| `bgroom_1` | −62.49° | −90° | works |

`gpbig1a`'s measured response (arrow RIGHT → world 84°, DOWN → world −5.7°, two independent arrows)
gives `camRight ≈ 90°`, not the 0° the quantizer picked and not the 38.32° raw value. So the engine
**does** quantize — it just uses a lower threshold than `roundf`.

## 3. 93 fields would flip if the threshold is ~30° rather than 45°

These are the fields with `|angle mod 90|` between 30° and 45° — they currently snap **down** and would snap **up**.

**Every field known to work today is outside this band.** `gpbig1a` is inside it. That is the
single strongest argument that the hypothesis is safe, and the single reason it still needs one more
confirmation before shipping — 93 fields is a large blast radius to bet on one confirmed data point.

### Immediate test available in the same dungeon

| field | angle | prediction |
|---|---|---|
| `gpgmn3` | +38.80° | **should be broken exactly like `gpbig1a`** (0.5° apart) |
| `gpbrdg1` | +129.10° | should be broken (|mod 90| = 39.10) |
| `gppark1` | −46.80° | should be **fine** — just the other side of the boundary |
| `gpbig2a` | −24.76° | outside the band; if it misbehaves, the cause is something else |

## 4. Second, independent failure mode: multi-camera fields

45 fields carry more than one camera. The mod reads **camera 0 only**
(`LoadCameraAxes`: *"Reads the first camera setting (index 0 = default camera)"*).
**18 of them have cameras that snap to different cardinals**, so if the field script switches camera
the mod's axes are a full cardinal wrong with nothing in the log to say so:

| field | camera angles |
|---|---|
| `bgmast_5` | 108.0°, 5.2° |
| `bgryo2_1` | 0.0°, 48.8° |
| `ebcont2` | -169.8°, -33.7° |
| `epwork3` | -159.8°, 6.5° |
| `ewbrdg1` | 0.0°, 120.8° |
| `felast1` | 2.3°, 51.9° |
| `fhwise13` | 24.5°, -145.5° |
| `gdsand1` | -65.9°, 0.0° |
| `gdtrain1` | -90.0°, -0.2° |
| `gggate2` | 59.1°, 2.6° |
| `ggroom1` | 17.6°, 60.5° |
| `ggstaen1` | 48.6°, 141.8° |
| `glclub1` | -9.5°, -102.9° |
| `glclub4` | -115.9°, 45.2° |
| `glstage1` | 179.6°, -11.2° |
| `glwitch1` | 168.6°, 13.8° |
| `glwitch3` | 168.6°, 13.8° |
| `rgcock3` | -28.1°, -152.4° |

`glwitch1`, `glstage1` and `ggroom1` are on that list and all three appear in the sewer/gate work.

## 5. Full boundary-risk list

107 fields sit within 10° of the 45° coin-flip (discarded ≥ 35°). See `flip_band` and
`discarded_deg` in the CSV. This turns "you randomly hit a broken field" into a finite, named list.
