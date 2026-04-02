# Deep Research Prompt: SAPI Dual-Channel Simultaneous Speech

## Goal

We need two independent SAPI5 ISpVoice instances that produce **truly simultaneous audio output** — both voices speaking at the same time on separate audio streams, so neither interrupts or serializes with the other. This is for a game accessibility mod (DLL injected into FF8_EN.exe, 32-bit Windows process) that provides screen reader TTS for blind players during battle sequences.

## What We're Building

- **Channel 1 (menu voice):** Announces command menu navigation — "Attack", "Magic", "GF", "Draw", turn announcements like "Squall's turn. Attack."
- **Channel 2 (event voice):** Announces damage/healing events — "Bite Bug takes 52 damage. Defeated." These events occur during action execution phases, often while menu speech from Channel 1 may still be playing.

The two channels must be able to play at the same time without one cutting off or waiting for the other.

## Current Implementation

We create two separate `ISpVoice` COM objects via independent `CoCreateInstance` calls:

```cpp
// In InitSAPI(), called from our accessibility thread:
HRESULT hr = CoCreateInstance(CLSID_SpVoice, NULL, CLSCTX_ALL,
                              IID_ISpVoice, (void**)&s_pVoice);
s_pVoice->SetRate(s_currentRate);

hr = CoCreateInstance(CLSID_SpVoice, NULL, CLSCTX_ALL,
                      IID_ISpVoice, (void**)&s_pVoice2);
s_pVoice2->SetRate(s_currentRate);
s_pVoice2->SetVolume(s_currentVolume);
```

Channel 1 speaks via:
```cpp
s_pVoice->Speak(text, SPF_ASYNC, NULL);                    // queue
s_pVoice->Speak(text, SPF_ASYNC | SPF_PURGEBEFORESPEAK, NULL); // interrupt
```

Channel 2 speaks via:
```cpp
s_pVoice2->Speak(text, SPF_ASYNC, NULL);                    // queue
s_pVoice2->Speak(text, SPF_ASYNC | SPF_PURGEBEFORESPEAK, NULL); // interrupt
```

COM is initialized on the accessibility thread with `CoInitializeEx(NULL, COINIT_APARTMENTTHREADED)`.

Both voices use the default SAPI audio output (we never call `SetOutput` on either).

## The Problem

Despite being separate `ISpVoice` COM objects, the two voices do NOT produce simultaneous audio. When Channel 2 speaks, it interrupts/cuts off whatever Channel 1 is currently saying. The behavior is identical to using a single voice — they serialize on the same audio stream rather than overlapping.

This has been confirmed by the user (who is blind and using the mod): damage announcements on Channel 2 consistently interrupt command menu readouts on Channel 1.

## Environment

- **OS:** Windows 10/11 (standard consumer install)
- **Audio:** Default Windows audio output (whatever the user's system default is)
- **Process:** 32-bit (x86) Win32 application (FF8_EN.exe, Steam 2013 edition)
- **DLL:** Our mod loads as `dinput8.dll` (DLL proxy), creates an accessibility thread that handles all TTS
- **COM threading:** `COINIT_APARTMENTTHREADED` on our accessibility thread
- **SAPI version:** SAPI5 (whatever ships with Windows 10/11)
- **Screen reader:** NVDA may or may not be running alongside. We also send speech to NVDA's controller API for braille output, but the audio channel issue is purely SAPI
- **Compiler:** MSVC 2022 (cl.exe), targeting x86 Win32

## What We Need From Research

### 1. Why two ISpVoice instances share the same audio stream

Is this expected SAPI5 behavior? Do two default `ISpVoice` objects always route to the same underlying audio renderer, causing serialization? Is there a shared audio queue or render stream at the SAPI or WASAPI level that forces sequential playback?

### 2. How to achieve true simultaneous SAPI speech

Specific, tested approaches to make two `ISpVoice` instances produce overlapping audio. For each approach, provide complete C/C++ code examples. Possible avenues to investigate:

- **Separate audio output objects:** Can we create two different `ISpMMSysAudio` or `ISpStreamFormat` objects pointed at the same physical device but as independent streams? Example code for `SetOutput()` with custom audio objects.
- **Different WASAPI sessions:** Can we force each voice into its own WASAPI audio session so Windows mixes them independently?
- **DirectSound or XAudio2 output:** Can we redirect one or both voices to a DirectSound or XAudio2 buffer instead of the default SAPI audio output?
- **SpMMAudioOut with different device instances:** Does creating two `SpMMAudioOut` objects and assigning one to each voice help?
- **Custom ISpAudio implementation:** Is there a way to implement a custom audio output that renders to an independent stream?
- **ISpVoice on separate threads with separate COM apartments:** Would creating each voice on its own thread (each with its own `CoInitializeEx`) help with audio stream independence?
- **Third-party alternatives:** Are there lightweight C/C++ TTS libraries (not SAPI) that can produce independent audio streams more easily? Must work from a 32-bit DLL without external dependencies beyond what ships with Windows.

### 3. Constraints

- Must work from inside a 32-bit Win32 DLL injected into a game process
- Cannot require the user to install additional software
- Must use APIs available on stock Windows 10/11
- Should not conflict with FFNx (an OpenGL/DirectX rendering mod also loaded in the same process) or with NVDA screen reader
- Audio output must go through the same physical speakers/headphones the game uses
- Both voices should respect the same rate and volume settings
- Solution must be reliable — not dependent on specific audio driver behavior

### 4. SAPI internals

- How does SAPI5's default audio output work internally? Does it use a shared WASAPI endpoint in shared mode?
- Is there a way to inspect or control the audio session ID that SAPI uses?
- Does `ISpVoice::SetOutput` with an `ISpObjectToken` for the same audio device create a new independent audio stream, or does it reuse the existing one?
- What happens if we create an `ISpMMSysAudio` object, call `SetDeviceId` with `WAVE_MAPPER`, and pass it to `SetOutput`? Does each `ISpMMSysAudio` instance get its own waveOut handle?

### 5. Practical examples

Any known open-source projects, accessibility tools, or game mods that successfully achieve simultaneous SAPI speech from two voices in the same process? How did they solve it?

## What Would Be Ideal

The simplest possible code change to our existing `InitSAPI()` function that makes `s_pVoice` and `s_pVoice2` produce truly independent, overlapping audio. Ideally just adding the right `SetOutput()` calls or audio object creation. The less infrastructure change, the better — we want to keep using `ISpVoice::Speak` with `SPF_ASYNC` on both channels.

## IMPORTANT: Savemap Offset Correction Note

This prompt is NOT about savemap offsets, but for consistency with our research protocol: all ChatGPT deep research assumes a 96-byte (0x60) savemap header. Actual header is 76 bytes (0x4C). Subtract 0x14 from all post-header research offsets.
