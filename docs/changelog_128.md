## v0.20.128

#minigame-bgbtl: **the Garden battle is done, so the scaffolding comes down —
and the log had two warts left in it.**

> *"Please review the log in full and in detail to check for any other problems
> or issues with the mini-game that we may have overlooked. After that look for
> any diagnostics, dead code, etc. left over from building this out and do the
> necessary clean up."* — Aaron

---

### What the log review found

The 2026-08-15 16:12–16:16 run is the winning one: two attempts, a deliberate
loss, a heavy punch that took the soldier 600 → 148, six blocks, best streak 3,
F9 into a rescue FMV that played in full and handed off to G-Garden on the
game's **own** `MAPJUMP3` at clock 1185. Nothing in the mini-game machinery
misbehaved. Three things around it did.

**1. The fight announced its own archive filename.** Twice — once per attempt:

```
16:12:29  FieldAnnounce: announced fieldId=0x0098 name='bgbtl_1' spoken='bgbtl_1'
16:14:17  FieldAnnounce: announced fieldId=0x0098 name='bgbtl_1' spoken='bgbtl_1'
```

`FIELD_DISPLAY_NAMES[152]` is a placeholder — the table's own comment says
`// 152: bgbtl_1 (???)` — so the player heard a filename in the two seconds
before the Game Controls briefing opened. Field 152 joins field 95 on
`NO_NAME_FIELDS` for the same reason field 95 is there: **it is not a place.**
It is a fight, and the briefing that follows a heartbeat later says everything
worth saying about it.

**2. The loss was announced twice.**

```
16:13:06  "You are down."
16:13:07  "You 0"
```

The outcome path announces and returns, but the *next* poll found the rounded
percentage had changed and reported it as if the fight were still live. Once
the round is decided there is nothing left to report, so health reports now stop
at the outcome.

**3. `+4294933093ms`.** DWORD underflow in the per-REQ timestamps: `EndBriefing`
restarts the clock so REQ offsets read from the start of the fight, but the ring
still held events stamped during the briefing. It went out with the log line
that printed it.

### The scaffolding

Every instrument in this module was built to answer a question that is now
answered. A run used to emit **711 `[BGBTL]` lines; it now emits about 75.**

| removed | lines/run it was costing |
|---|---|
| `[BGBTL-REQ]` — every script REQ, named | 260 |
| `[BGBTL-CLOCK]` — var 80, once a second | 188 |
| `[BGBTL-KEY]` — raw X/Enter/F9 during the briefing | 102 |
| `[BGBTL-BTN]` — the button word, change-only | 57 |
| `[BGBTL-STATE]` — the fight's state bytes | 28 |

What stays is what a future question would actually need: the arm/disarm
summary, the briefing, `[BGBTL-LEARN]`, `[BGBTL-STREAK]`, `[BGBTL-HP]` on
change, `[BGBTL-BLOCK]` and `[BGBTL-DLG]`.

**Gone with them:** `LogKeys`, `LogKeyChanges`, `WatchButtons`, `WatchState`,
`ReadButtonsAnySource` (and the `frozen` plumbing that fed it), `KeyCand`, seven
orphaned statics (`s_aDownAtCue`, `s_lastKeyMask`, `s_state`, `s_stateValid`,
`s_lastBtn`, `s_lastFieldBtn`, `s_lastKeyDiag`, `s_clockDone`) and fourteen
unused constants (`ARM_CAP_MS`, `CLOCK_FIGHT_END`, `CLOCK_LADDER_END`,
`CLOCK_NUDGE_HZ`, `CLOCK_STALL_MS`, `FIELD_BUTTONS_PREV`, `FIELD_MAIN_ADDR`,
`GUARD_POLL_MS`, `HEARTBEAT_DEAD_MS`, `REACTION_WINDOW_MS`, `STATE_FIRST`,
`STATE_LAST`, `WINDUP_FRAMES_PUNCH`, `WINDUP_FRAMES_KICK`, `X86_RET`).

**One line was also just wrong**, and had been for four builds:

```
[BGBTL] Squall's guard script REQ'd (label 31, squall::squ_kicking0)
```

Label 31 in field 152 is `squ_kicking0`; the guard is 32. The check dated from
before the `.sym` off-by-one was found, and the log printed the contradiction in
its own text every run. It is deleted rather than corrected — `s_guardSeen` has
come from `WatchStreak` since v0.20.120, because var 340 increments once per
**blocked hit** where the flag has one rising edge per **hold**.

The disarm summary now reports the vetoed-attack count it has been keeping
privately, and says whether the dialog box was shown rather than "last pause
ON/OFF", which described a mechanism that no longer exists.

### `minigame/` must not be pushed

`git add -A` would have staged it. It holds the extracted `bgbtl_1` and
`bg2f_31` field archives, `chara.one`, and disassembled `FF8_EN.exe` handler
listings — **718 KB of game data, none of it ours to redistribute.** Added to
`.gitignore` with the reason written beside it. The findings that came out of it
live in `DEVNOTES.md` and the project's `GARDEN_BATTLE_MINIGAME_FINDINGS.md`,
which is where they belong.

### `FF8OPC_VERSION` was lying

It said `"0.20.118"`. It had said that through .119, .120, .121, .122, .123,
.124, .125, .126 and .127 — ten builds, every one of them logging a banner for a
build that was not running. Bumped to `0.20.128`, its 3 KB inline comment cut
down to a paragraph, and a one-line history entry written for each of the ten
builds that never got one.

### Verification

* `minigame_bgbtl_compile`: **0 errors, 0 bad.** The `WatchState` assertions are
  replaced by ones that hold the same ground through the code that survives:
  `GuardVarFor` returns 1030 for field 144 and 1028 for field 152, and
  `ApplyBlockAssist` writes that byte **and never var 1031, the soldier's**, in
  either field. The probe's own `#include <cmath>` also moved above the SEH
  macros — `#define __try if(1)` was turning libstdc++'s own
  `__try { } __catch(...) { }` into a syntax error.
* `lint_seh` OK (88 files); `garden_harness` 26 ok / 0 bad; `catalog_story_test`
  0 failures; `garden_aboard_test` and `world_map_harness` pass.
* `field_navigation.cpp` **untouched at 81,645** — 275 from the hard fail.
  `field_minigame_bgbtl.inl` is **64,716, down from 71,785**.

**NOT MSVC-built.**

### Still worth a look, not changed here

Two things the log shows that are outside this cleanup and would change what you
hear, so they are yours to call rather than mine:

1. **The background movie's audio description plays during the fight.**
   `disc01_33h.vtt` cues land mid-round — *"Turquoise energy and fire flash…"*
   at 16:14:07, one second before *"Heavy punch ready"* had to fight it for the
   channel. The briefing already suppresses FMV narration; extending that to the
   whole fight and lifting it at the win would keep the round's audio clear
   without losing the rescue scene's description.
2. **The game's own legend and name labels are read out at each fight start** —
   "Punch Block Kick", "Squall", "Galbadian Soldier" — while the mod's briefing
   is covering the same ground in more detail.

### BAT

Nothing about the fight should feel different. What should change:

1. No **"bgbtl_1"** spoken as you enter the fight, on either attempt.
2. Lose on purpose: **"You are down."** and then nothing — no trailing "You 0".
3. `ff8_field.log` should be quiet. Grep `[BGBTL]` — roughly 75 lines, not 700 —
   and the banner at the top should now read **v0.20.128**.
