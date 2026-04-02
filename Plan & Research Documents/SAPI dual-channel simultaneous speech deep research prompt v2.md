# Deep Research Prompt: SAPI Dual-Channel Simultaneous Speech (v2)

## Goal

We need two independent SAPI5 ISpVoice instances that produce **truly simultaneous audio output** — both voices speaking at the same time on separate audio streams, so neither interrupts, serializes with, or silences the other. This is for a game accessibility mod (DLL injected into FF8_EN.exe, 32-bit Windows process) that provides screen reader TTS for blind players during battle sequences.

## What We're Building

- **Channel 1 (menu voice):** Announces command menu navigation — "Attack", "Magic", "GF", "Draw", turn announcements like "Squall's turn. Attack."
- **Channel 2 (event voice):** Announces damage/healing events — "Bite Bug takes 52 damage. Defeated." These events occur during action execution phases. There are cases where Channel 2 speech needs to fire while Channel 1 is still speaking (e.g. damage happens during or just after a turn announcement).

The two channels must be able to play audio simultaneously — overlapping, not serialized. If Channel 2 speaks, it must NOT cut off or silence Channel 1, and vice versa.

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

Both voices use the default SAPI audio output — we **never call `SetOutput` on either voice**.

## The Problem — Confirmed Behavior

Despite being separate `ISpVoice` COM objects, the two voices do NOT produce simultaneous audio. When Channel 2 speaks, it interrupts/cuts off whatever Channel 1 is currently saying. The behavior is identical to using a single voice — they serialize on the same audio stream rather than overlapping.

**This has been confirmed by the user (who is blind):** damage announcements on Channel 2 consistently interrupt command menu readouts on Channel 1. The effect is as if both voices share a single audio pipeline and whichever Speak() call is more recent takes over.

**Key observation:** We never call `SetOutput()` on either voice. Both voices are using whatever SAPI gives them by default. Our hypothesis is that SAPI's default audio output is a shared singleton — all ISpVoice instances in the same process that use the default output share one underlying audio stream, and only one can render at a time.

## Environment & Constraints

- **OS:** Windows 10/11 (standard consumer install, no dev tools assumed)
- **Audio:** Default Windows audio output (whatever the user's system default is — could be speakers, headphones, USB audio, Bluetooth)
- **Process:** 32-bit (x86) Win32 application (FF8_EN.exe, Steam 2013 edition)
- **DLL:** Our mod loads as `dinput8.dll` (DLL proxy), creates a background accessibility thread that handles all TTS
- **COM threading:** `COINIT_APARTMENTTHREADED` on our accessibility thread
- **SAPI version:** SAPI5 (whatever ships with Windows 10/11 — typically SAPI 5.4)
- **Screen reader:** NVDA may or may not be running alongside. We send speech to NVDA via its controller COM API for braille output, but the audio channel issue is purely SAPI-to-speakers
- **Compiler:** MSVC 2022 (cl.exe), targeting x86 Win32
- **No external dependencies:** Cannot require the user to install any additional software, SDKs, or runtime libraries. Must work with what ships on stock Windows.
- **FFNx compatibility:** Another DLL mod (FFNx, an OpenGL/DirectX renderer) is loaded in the same process. Our solution must not conflict with FFNx's audio (it uses nxAudioEngine for game BGM/SFX via DirectSound/WASAPI)
- **Threading:** All SAPI calls currently happen on a single thread (our accessibility thread). We can create additional threads if needed.

## What We Need From Research

### 1. Root Cause: Why Two Default ISpVoice Instances Share Audio

Please explain the SAPI5 audio pipeline architecture:
- When you create two `ISpVoice` objects and never call `SetOutput()`, do they share a single underlying audio renderer/stream?
- Is SAPI's default audio output (`SpMMAudioOut` or whatever the default is) a per-process singleton, or per-ISpVoice?
- At what level does the serialization happen — SAPI's internal audio queue, the waveOut handle, WASAPI endpoint, or Windows audio mixer?
- Is this behavior documented anywhere, or is it a known limitation discovered by developers?

### 2. Solution: How to Achieve True Simultaneous SAPI Speech

For each approach below, provide **complete, compilable C/C++ code** showing exactly how to set it up. Rate each approach on: (a) likelihood of producing true simultaneous audio, (b) complexity, (c) reliability across different audio hardware.

#### Approach A: Separate SpMMAudioOut Objects
Create two independent `SpMMAudioOut` (or `ISpMMSysAudio`) COM objects, each configured for the default audio device, and assign one to each voice via `SetOutput()`. Does each `SpMMAudioOut` instance get its own `waveOut` handle, giving independent audio streams that Windows will mix at the WASAPI level?

```cpp
// Pseudocode — need real working version:
ISpMMSysAudio* pAudio1 = nullptr;
ISpMMSysAudio* pAudio2 = nullptr;
CoCreateInstance(CLSID_SpMMAudioOut, NULL, CLSCTX_ALL, IID_ISpMMSysAudio, (void**)&pAudio1);
CoCreateInstance(CLSID_SpMMAudioOut, NULL, CLSCTX_ALL, IID_ISpMMSysAudio, (void**)&pAudio2);
// Set both to WAVE_MAPPER (default device)
pAudio1->SetDeviceId(WAVE_MAPPER);
pAudio2->SetDeviceId(WAVE_MAPPER);
s_pVoice->SetOutput(pAudio1, TRUE);
s_pVoice2->SetOutput(pAudio2, TRUE);
```

Will this actually produce independent audio streams? What are the exact COM interfaces, CLSIDs, and method calls?

#### Approach B: Separate Threads with Separate COM Apartments
Create `s_pVoice2` on its own dedicated thread with its own `CoInitializeEx()` call. Does a separate COM apartment result in a separate audio output object internally?

#### Approach C: DirectSound Secondary Buffers
Route one or both voices to DirectSound secondary buffers instead of the default SAPI audio output. DirectSound guarantees hardware mixing of multiple secondary buffers. Would this require implementing `ISpAudio` as a custom DirectSound wrapper?

#### Approach D: WASAPI Shared-Mode Streams
Create two WASAPI shared-mode render clients for the same endpoint device, wrap each in a custom `ISpAudio` implementation, and assign one to each voice. WASAPI shared mode explicitly supports multiple clients being mixed by the Windows audio engine.

#### Approach E: SpStreamFormatConverter / IStream Wrapper
Route SAPI output to an in-memory IStream, decode the PCM audio ourselves, and play it through a second audio API (waveOut, DirectSound, WASAPI). This gives full control but is the most work.

#### Approach F: Different Output Tokens
Instead of creating `SpMMAudioOut` manually, use `SpEnumTokens(SPCAT_AUDIOOUT)` to get the default audio token, and call `SetOutput(pToken, TRUE)` on each voice. Does using the token (vs. no SetOutput) create independent streams?

### 3. COM Threading Model Questions

- Does `COINIT_APARTMENTTHREADED` vs `COINIT_MULTITHREADED` affect whether SAPI creates shared or independent audio outputs?
- If we create Voice 2 on a second thread with `COINIT_MULTITHREADED`, does that change the audio sharing behavior?
- Are there COM marshaling issues to watch out for when calling `Speak()` on a voice created in a different apartment?

### 4. SAPI Audio Output Internals

- What is the exact CLSID for the default SAPI audio output object? Is it `CLSID_SpMMAudioOut`?
- Does `SpMMAudioOut` use `waveOut` API or WASAPI internally on Windows 10/11?
- When two `SpMMAudioOut` instances target WAVE_MAPPER, do they each open their own `waveOut` handle?
- Is there a way to verify at runtime that two audio output objects are using independent streams (e.g., checking handle counts or audio session IDs)?

### 5. Known Working Examples

Are there any known open-source projects, accessibility tools, game mods, or Microsoft samples that successfully achieve simultaneous SAPI speech from two ISpVoice instances in the same process? How did they solve it?

### 6. Fallback: Non-SAPI Alternatives

If SAPI fundamentally cannot do true simultaneous speech from two voices (even with separate outputs), what lightweight alternatives exist?

- **Windows.Media.SpeechSynthesis** (UWP TTS API): Can it be used from a Win32 DLL? Does it create independent audio streams per synthesizer instance?
- **eSpeak NG**: Small, open source, C library. Could we compile it into our DLL and use it as Channel 2 while SAPI remains Channel 1?
- **Manual PCM mixing**: Route both voices to memory buffers, mix the PCM ourselves, output through a single waveOut/WASAPI stream. How complex is this?

### 7. Priority Ranking

Please rank all viable approaches from simplest/most likely to work → most complex:
1. [Recommended approach]
2. [Second choice]
3. [etc.]

For the top 2-3 approaches, provide complete, ready-to-compile code snippets that I can drop into our existing `InitSAPI()` function with minimal changes.

## Current Code Context

Our TTS initialization and speech functions live in a ScreenReader namespace. The relevant parts:

```cpp
namespace ScreenReader {
    static ISpVoice* s_pVoice = nullptr;   // Channel 1 (menu)
    static ISpVoice* s_pVoice2 = nullptr;  // Channel 2 (events)
    
    void InitSAPI() {
        CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
        CoCreateInstance(CLSID_SpVoice, NULL, CLSCTX_ALL, IID_ISpVoice, (void**)&s_pVoice);
        s_pVoice->SetRate(s_currentRate);
        CoCreateInstance(CLSID_SpVoice, NULL, CLSCTX_ALL, IID_ISpVoice, (void**)&s_pVoice2);
        s_pVoice2->SetRate(s_currentRate);
        s_pVoice2->SetVolume(s_currentVolume);
    }
    
    void Speak(const char* text, bool interrupt) {
        // Convert to wchar_t, call s_pVoice->Speak(...)
    }
    
    void SpeakChannel2(const char* text, bool interrupt) {
        // Convert to wchar_t, call s_pVoice2->Speak(...)
    }
}
```

Both `Speak` and `SpeakChannel2` are called from the same thread (our mod's accessibility poll thread). All calls use `SPF_ASYNC`.

## What Would Be Ideal

The simplest possible code change to our existing `InitSAPI()` function that makes `s_pVoice` and `s_pVoice2` produce truly independent, overlapping audio. Ideally just adding the right `SetOutput()` calls or audio object creation — minimal infrastructure change. We want to keep using `ISpVoice::Speak` with `SPF_ASYNC` on both channels.

If `SetOutput()` with separate `SpMMAudioOut` objects doesn't work, the next best thing is a clean, self-contained alternative that we can implement in a single .cpp file with no external dependencies.

## IMPORTANT: Savemap Offset Correction Note

This prompt is NOT about savemap offsets, but for consistency with our research protocol: all ChatGPT deep research assumes a 96-byte (0x60) savemap header. Actual header is 76 bytes (0x4C). Subtract 0x14 from all post-header research offsets.
