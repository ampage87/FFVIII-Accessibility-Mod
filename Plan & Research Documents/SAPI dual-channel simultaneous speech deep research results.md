# SAPI5 dual-channel speech: bypassing the hidden serialization lock

**Two `ISpVoice` instances in the same process always serialize because SAPI5 deliberately queues all voices targeting any `ISpAudio`-implementing output.** The default `SpMMAudioOut` object implements `ISpAudio`, triggering a cross-voice priority queue where `SPVPRI_NORMAL` voices wait for each other — by design. The fix requires routing one or both voices to an output that *does not* implement `ISpAudio`, which disables serialization entirely, or creating truly separate audio device objects. This report covers the root cause, four escalating solutions with complete C++ code, and a production-proven architecture from NVDA.

## Why SAPI forces your two voices into a single queue

The `ISpAudio` interface documentation states the mechanism explicitly: *"In order to prevent multiple TTS voices or engines from speaking simultaneously, SAPI serializes output to objects which implement the ISpAudio interface."* When `SetOutput` is never called, each `ISpVoice` internally creates its own `SpMMAudioOut` COM object — so they are distinct objects — but SAPI recognizes they target the **same audio device** and enqueues them together.

The serialization operates through three priority tiers set via `ISpVoice::SetPriority`. At `SPVPRI_NORMAL` (the default), a voice *"will wait until other voices in the system have completed before it will begin rendering its input queue."* At `SPVPRI_ALERT`, a voice interrupts the normal queue, speaks, then resumes the interrupted voice — exactly the Channel-2-cuts-off-Channel-1 behavior you observe. At `SPVPRI_OVER`, voices theoretically mix simultaneously, but **the SAPI 5.3 and 5.4 documentation explicitly states: "SPVPRI_OVER priority voices only mix on Windows 2000."** This limitation appears in both versions of the `SetPriority` reference, meaning Microsoft never extended audio-level mixing to XP or later.

Critically, the serialization is tied to `ISpAudio`, not to the physical audio device. The documentation provides the master escape hatch: *"If the output object does not implement ISpAudio, no serialization will occur, and all voices will be treated as if their priority is SPVPRI_OVER."* The underlying Windows audio stack — waveOut calls routed through WASAPI shared mode since Vista — can absolutely mix multiple streams simultaneously. The **bottleneck is purely SAPI's application-level queue**, not any OS or hardware limitation.

## Solution 1: Separate SpMMAudioOut objects via SetOutput (simplest change)

Create two independent SpMMAudioOut COM objects, assign one to each voice via SetOutput. Each wraps its own waveOut handle. May bypass serialization if SAPI doesn't identity-match the device tokens. Try first — 5 minute implementation.

## Solution 2: Memory stream synthesis with dual waveOut playback (guaranteed bypass)

Guaranteed to bypass serialization because ISpStream wrapping CreateStreamOnHGlobal does not implement ISpAudio. Synthesize to memory buffer, play through independent waveOut handles. Each channel on its own thread with own COM apartment. ~50ms latency for short battle phrases.

## Solution 3: Custom ISpStreamFormat with ring buffer for real-time streaming

Custom COM object implementing ISpStreamFormat but NOT ISpAudio. QueryInterface returns E_NOINTERFACE for IID_ISpAudio — this is the critical design choice that disables serialization. SAPI calls Write() with PCM chunks as they're synthesized. Ring buffer feeds waveOut playback thread. Most robust approach.

## Solution 4: NoSerializeAccess registry attribute

Registry-based approach at HKLM\SOFTWARE\Microsoft\Speech\AudioOutput\TokenEnums\MMAudioOut\Attributes — requires admin privileges, system-wide effect. Best as diagnostic tool only.

## How NVDA solved this

NVDA implements custom ISpAudio COM object (SynthDriverAudioStream) that intercepts Write() calls and routes through own WASAPI WavePlayer. Overkill for our use case — ISpStreamFormat-only approach (Solution 3) suffices.

## Recommended order

1. Solution 1 (separate SpMMAudioOut) — try first, may work
2. Solution 2 (memory stream + waveOut) — guaranteed bypass, minimal latency
3. Solution 3 (custom ISpStreamFormat + ring buffer) — if real-time streaming needed

---

NOTE: Full code samples for all four solutions are preserved in the original research document uploaded as compass_artifact. The code above is summarized for reference — see the full prompt results for complete compilable implementations.