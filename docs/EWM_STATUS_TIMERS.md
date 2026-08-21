# Enhanced Wait Mode and the timed-status clock

*v0.37.0 (#95). Read out of `FF8_EN.exe`. The behavioural claim at the end is a
prediction the next BAT settles, and is labelled as one.*

---

## 1. The report

Aaron, after two limit-break BATs:

> *"when I cast Aura twice it ended before the character's ATB gauge filled. It
> seemed to end Aura unusually quick in this BAT and the previous one. I am
> curious if the Enhanced Wait Mode system might not be accounting for status
> effects / buffs."*

Measured from the 2026-08-19 log: Aura applied at 23:23:07, gone at 23:23:22 —
**fifteen seconds**, most of them with the ATB frozen. A second cast lasted
nineteen.

---

## 2. `sub_483470` is the timed-status timer

The mod has hooked this address since v0.13.55 under the name
`HookedProcessReady`, described in its own comment as *"the engine's process
ready characters / dispatch turns function"*. That was a guess, and it is wrong.

```
00483479  mov ebp, 0x01D27B90          ; entity + 0x78, stride 0xD0, end 0x01D28140 (7 entities)
00483482  test byte ptr [ebp], 5       ; KO | Petrify -> skip this entity
0048348E  lea eax, [ebp - 0x2c]        ; entity + 0x4C  <- the timer array
00483499  mov dx, word ptr [eax]       ; this status's remaining duration
0048349C  cmp dx, 0xFBA9               ; the "permanent / no timer" sentinel
004834AC  shl ebx, cl                  ; ebx = 1 << bitIndex
004834AE  test dx, dx / jg 0x483640    ; >0 -> tick; <=0 -> it has just expired
00483643  esi = 2  (3 with Haste, 1 with Slow)
004836C1  sub word ptr [eax], si       ; THE DECREMENT
004835E7  not ebx / and edx, ebx       ; THE FLAG CLEAR, at entity+0x00
004835EB  mov dword ptr [ebp - 0x78], edx
004836D0  cmp ecx, 0x0E                ; fourteen timers per entity
```

So each entity carries **14 `int16` durations at +0x4C**, indexed by the bit
position of the timed-status flag at entity+0x00. Aura is byte +0x01 bit `0x01`
— bit 8 — so its counter is at **entity+0x5C**. `0xFBA9` means "no timer".

Corroborating side effects inside the same loop: bit 4 queues a periodic heal
(Regen), bit 10 queues a death on expiry (Doom), bit 12 sets Petrify in
entity+0x78 on expiry (Gradual Petrify). There is no turn dispatch anywhere in
it.

The setter confirms the layout independently:

```
004832F0  set_status_timer(entIdx, mask)
00483303  cmp eax, 0x0E / jge   ; only bits 0..13 get a timer
0048332E  mov word ptr [eax*2 + 0x01D27B64], dx   ; 0x01D27B18 + 0x4C
```

**v0.13.55's own BAT already refuted the "dispatcher" reading** — it blocked 6 of
6 calls and the enemy attack landed anyway — and the conclusion drawn was "hook
`sub_482F80` as well" rather than "this is not the dispatcher".

---

## 3. The engine keeps the ATB and the status timers on one clock

```
004842BD  mov byte ptr [0x01D28DEB], 0   ; ATB update, on entry / early-out
004842EE  mov byte ptr [0x01D28DEB], 1   ; set only when the ATB loops really run
...
0047D7CD  mov al, byte ptr [0x01D28DEB]  ; battle state machine, state 4
0047D7D4  test al, al / je -> skip
0047D7F1  call 0x00483470                ; the status timers
```

Those four are the **only** references to `0x01D28DEB` in the executable. In
vanilla, whenever the ATB does not advance — Wait mode with a menu open, an
action in progress — the status timers do not advance either.

---

## 4. What EWM was doing

`HookedATBUpdate` saves every gauge, calls the original, and restores the saved
values. The gauges do not move — but the original ran, so it set `0x01D28DEB`,
and the engine downstream aged every timed status on a frame where no ATB
progress happened at all.

A blind player spends far longer in the menus than a sighted one: reading a
magic list, walking a target row, hearing a description. **Every one of those
seconds was charged to Aura, Haste, Protect, Shell, Regen and Reflect while the
gauges stood still.**

And the safety net that might have caught it was never connected:
`EWM_InstallProcessReadyHook()` and `EWM_InstallActionExecuteHook()` were
defined in v0.13.56 and **called from nowhere**. No shipped build has ever had
either hook. The log proves it — the startup banner lists the ATB and GF hooks
and nothing else, and there is not one `[DISPATCH]` line in 344 KB of battle
log. `s_blockProcessReady` was maintained faithfully for eleven versions gating
a hook that did not exist.

---

## 5. The fix

While the mod holds the ATB, hold the status timers too — the same shape as the
ATB freeze and the GF-loading freeze that already sit beside it. `sub_483470`
does nothing but age statuses, so skipping it holds status time and touches
nothing else.

`EWM_SetFreeze()` is now the only thing that may write either flag, and
`tests/lint_freeze.py` fails the build if anything else does. The failure this
guards against is not the fix being wrong — it is the two flags **drifting
apart** later, which is precisely what happened to `s_blockProcessReady`.

### What was deliberately NOT done

`0x01D28DEB` is not cleared. That would be a faithful "the ATB did not advance"
signal, but the same gate guards `sub_482F80` on the very next instruction, and
what that function does has not been established — it reads the battle-config
byte at `0x01CFE97A` and queues actions, and that is as far as the evidence
goes. Blocking a function nobody has read, on a frame where the active
character's own gauge *is* still advancing, is how turns get stuck. One hook, on
the one function whose contents are proven.

The `sub_482F80` hook is deleted rather than left dormant. Dead code carrying an
unverified belief is worse than no code.

---

## 6. Confirmed by BAT (2026-08-20)

```
[STATUS-TIMER] calls=962 held=849 ran=113 hold=1 | Aura s0=272 s1=-1111 s2=162
[STATUS-TIMER] calls=962 held=849 ran=113 hold=1 | Aura s0=272 s1=-1111 s2=162
[STATUS-TIMER] calls=962 held=849 ran=113 hold=1 | Aura s0=272 s1=-1111 s2=162
```

**849 of 962 ticks held**, and the two live Aura counters stand still across
three consecutive seconds with the freeze on. `-1111` is `0xFBA9` read as a
signed word — the "no timer" sentinel — so slot 1 simply had no Aura, which is
itself a check on the offset being right. One Aura covered testing two different
limit breaks.

## 6a. What the next BAT settles

**Prediction:** with the hold in place, a status counter stands still while the
freeze is on. The new `[STATUS-TIMER]` line reports it directly:

```
[STATUS-TIMER] calls=N held=H ran=R hold=1 | Aura s0=.. s1=.. s2=..
```

If `held` climbs while the Aura counters do not move, the hold works. If the
counters fall while `held` climbs, it does not — and the number is right there
rather than needing to be inferred from how a battle felt.

**Not claimed:** that Aura's *intended* duration is longer than fifteen seconds.
The base-duration table at `0x01CF8B14` and its multiplier at `0x01CFE738` are
populated at runtime and are not readable from the file, so the intended length
has not been computed. What is established is that time was being charged to it
while the battle was standing still.

---

## 7. Where the code lives

| file | what |
|---|---|
| `src/battle_tts_ewm_state.inl` | the disassembly notes, the flags, `EWM_SetFreeze()` |
| `src/battle_tts_ewm_status_timers.inl` | the hook, the installer, the `[STATUS-TIMER]` diagnostic |
| `src/battle_tts_ewm_atb_hook.inl` | the ATB freeze this pairs with |
| `tests/lint_freeze.py` | the two flags may only be written together |
