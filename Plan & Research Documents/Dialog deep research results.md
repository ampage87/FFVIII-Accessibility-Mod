# FF8 Steam 1.2 US Dialog Capture Gaps in an FFNx-Based Accessibility Mod

## Executive summary

Your current capture stack is very close, but three specific implementation mismatches strongly explain why some in‑game lines (including “Squall hallway thoughts”) are missed even though your hooks “fire”:

1. **Your window-object stride is wrong (0x38 vs 0x3C)**, causing you to index into the `windows` array incorrectly whenever `window_id != 0` and whenever you “scan all windows.” In the FFNx `ff8_win_obj` definition, the struct includes two callbacks at the end and is **0x3C bytes**, not 0x38. This means `GetWindowObj(i)` becomes misaligned starting at window 1 and will present garbage pointers/states—exactly what your own `[RAWDUMP]` lines show for non‑zero window indices (e.g., `p=00800080`, `p=00FFFF00`). (Observed in provided source: `src/field_dialog.cpp` uses `WIN_OBJ_SIZE = 0x38` at ~L259; FFNx struct in provided FFNx source: `FFNx.../src/ff8.h` shows `ff8_win_obj` ending with `callback1`, `callback2`, making size 0x3C at ~L584–L615; observed in provided log: `ff8_accessibility.log` contains repeated `[RAWDUMP]` entries with implausible pointers/states for windows other than 0.)

2. **Your `field_get_dialog_string` hook prototype is almost certainly incorrect and (per your log) not executing at all.** FFNx’s voice layer replaces `field_get_dialog_string` with a function that takes **two parameters**—a base message table pointer and a dialog/message index—and returns a pointer to the actual string (`msg + offset[dialog_id]`). Your mod’s typedef/hook currently takes **one** `int messageId`. If your hook were actually being invoked from `opcode_mes`, this mismatch would typically crash; instead, your log shows the hook was installed but **no `[GETSTR]` / `[GETSTR-RAW]` lines ever occur**, implying the hook is not being hit (or the logging path is bypassed). Fixing the signature to match the real call site is a prerequisite to using message IDs reliably. (Observed in provided source: `src/field_dialog.cpp` typedef at ~L61 and hook at ~L631; FFNx voice implementation in provided FFNx source: `FFNx.../src/voice.cpp` defines `char *ff8_field_get_dialog_string(char *msg, int dialog_id)` at ~L960–L965 and installs it via `replace_function` at ~L1424; FFNx resolves the target from `opcode_mes+0x5D` in provided FFNx source `ff8_data.cpp` at ~L845; your mod resolves the same address in provided source `src/ff8_addresses.cpp` at ~L549–L554.)

3. **Pointer-equality de-duplication in `show_dialog` is not safe if FF8 reuses a stable buffer address and overwrites contents in place.** Your `Hook_show_dialog` currently returns early when `text1 == lastTextPtr`. That will miss any dialog where the engine keeps the same `text_data1` pointer and simply updates bytes for new pages / new lines—precisely the pattern FFNx avoids by using state/transition tracking (`open_close_transition` and `win->state`) rather than pointer changes. In other words: FFNx’s behavior is strong circumstantial evidence that “pointer changed” is not a reliable proxy for “text changed.” (Observed in provided source: `src/field_dialog.cpp` early-return at ~L867–L875; FFNx voice logic uses `open_close_transition` and paging/opening detection in provided FFNx source `voice.cpp` at ~L284–L320 and ~L1170–L1173.)

A secondary but important contributor: your `TrimDecoded()` removes leading/trailing spaces **and periods**, which turns a line made solely of `"..."` / `"......"` into an empty string and you then drop it. A lot of Squall’s internal monologue in early scenes is ellipsis-heavy; even if you captured it, your pipeline may intentionally discard it. (Observed in provided source: `TrimDecoded` at ~L407–L413 plus min-length checks at ~L313, ~L651, ~L876.)

What FFNx (and therefore Echo‑S, which depends on FFNx) strongly suggests is that the robust path is:

- treat `ff8_win_obj` layout/stride as authoritative (0x3C, includes `text_data2`),
- capture **message IDs** at `field_get_dialog_string` with the correct signature,
- detect dialog changes using **state/transition** and/or **content hashing**, not pointer equality.

FFNx is explicitly positioned as the modding platform enabling FF8 voice acting (Echo‑S) (FFNx GitHub README). citeturn13view0 Echo‑S 8’s own page distributes an “FFNX Echo‑S 8 Demo,” reinforcing that it runs on FFNx rather than a separate public hook layer. citeturn9search0

---

## Assumptions & unspecified details

The following are assumed because they are not fully specified in your prompt and/or are not directly inferable from the provided zip/logs:

- The missed lines are **in-field** (MODE_FIELD=1) and not menu/battle text (you’re already hooking `menu_draw_text` and `get_character_width`). (Unspecified.)
- The specific “hallway scene with Squall and Quistis” is the early SeeD hallway interaction; the exact field name / script label is not provided. (Unspecified.)
- Your TTS output is triggered by `SpeakText()` and not by an external audio pipeline. (Inferred from provided `FieldDialog` patterns; exact TTS engine is unspecified.)
- “Steam 1.2 US” corresponds to the original PC Steam release (not the HD Remaster); this report explicitly excludes Remaster-only details as requested.

---

## Findings from the provided source and logs

### Window object layout mismatch is real and observable

**Your mod’s assumed layout / stride**
- `WIN_OBJ_TEXT1_OFFSET = 0x08` (correct for `text_data1`).
- `WIN_OBJ_SIZE = 0x38` (incorrect). (Observed in provided source: `src/field_dialog.cpp` ~L255–L263.)

**FFNx’s `ff8_win_obj` layout in the same zip**
In the included FFNx source (`FFNx-Steam-v1.23.0.182/Source Code/FFNx-canary/src/ff8.h`), `struct ff8_win_obj` contains:
- `char *text_data1;` at offset 0x08
- `char *text_data2;` at offset 0x0C
- two callbacks at the end: `uint32_t callback1; uint32_t callback2;`  
This makes the struct **0x3C bytes**. (Observed in provided FFNx source: `.../src/ff8.h` ~L584–L615, especially ~L612–L615.)

**Your own log corroborates mis-stride effects**
Your `DiagRawWindowDump()` prints plausible data for window 0 but nonsensical pointer/state combos for other windows, e.g.:
- `[1 p=00800080 st=117440512 ...]`
- `[7 p=00FFFF00 st=0 ...]`  
These are typical “garbage-looking” values you’d see when reading the wrong bytes as a pointer due to a stride error. (Observed in provided log: `ff8_accessibility.log`, multiple `[RAWDUMP]` lines around 14:02:04–14:02:34.)

**Impact**
- Any dialog rendered in windows 1–7 can be silently missed by scanning logic (`ScanAndSpeakAllWindows`) and any per-window de-dup state will be attached to the wrong memory. Even if your hallway example happens to be window 0, many other scenes (choices, multi-window overlays, tutorials, world prompts) can use other windows.

### Your `show_dialog` de-dup logic matches the “pointer reuse” failure mode

In `Hook_show_dialog` you implemented:

```cpp
if (text1 == s_sdLastTextPtr[window_id]) return result;
s_sdLastTextPtr[window_id] = text1;
```

(Observed in provided source: `src/field_dialog.cpp` ~L867–L871.)

This is explicitly described as “pointer-change detection” and is applied “for ALL modes.” If FF8 overwrites the same buffer, the pointer won’t change and you’ll never decode/speak new content for that window during `show_dialog`-driven capture.

### `field_get_dialog_string` hook is installed but never logs

Your log shows:
- `FF8Addresses: field_get_dialog_string = 0x00530750 (from opcode_mes+0x5D)`
- `FieldDialog: Hooked field_get_dialog_string: target=0x00530750 trampoline=...`

(Observed in provided log: `ff8_accessibility.log` near 14:01:45.)

However, there are **zero** occurrences of:
- `FieldDialog: [GETSTR-RAW] ...`
- `FieldDialog: [GETSTR] ...`

(Observed in provided log: no `GETSTR` substring at all.)

Given your hook logs the first five calls unconditionally (after calling the original), the absence of any GETSTR lines indicates the hook function is not actually being invoked in practice.

### Ellipsis-only lines are currently discarded by design

Your decode pipeline:
- trims whitespace and leading/trailing periods:
  - `TrimDecoded` uses `find_first_not_of(" .")` and `find_last_not_of(" .")` (Observed in provided source: `src/field_dialog.cpp` ~L407–L413.)
- rejects empty or length `< MIN_TEXT_LENGTH` (3) (Observed in provided source: `MIN_TEXT_LENGTH` ~L313; checks at ~L651 and ~L876.)

So a line consisting of `"..."` becomes `""` and is dropped—this can materially reduce perceived completeness for Squall’s internal monologue.

---

## What FFNx’s own implementation implies about reliable dialog capture

### FFNx resolves and hooks the same call sites you’re targeting

In the included FFNx source (`ff8_data.cpp`), FFNx resolves:
- `field_get_dialog_string` from `opcode_mes + 0x5D`
- `show_dialog` from a call inside `sub_4A0C00 + 0x5F`

(Observed in provided FFNx source: `FFNx.../src/ff8_data.cpp` ~L845–L863.)

This matches your mod’s address resolution strategy (Observed in provided source: `src/ff8_addresses.cpp` ~L549–L554 for `field_get_dialog_string` and ~L586–L587 for `show_dialog_addr`.)

### FFNx’s voice layer uses state/transition tracking, not pointer equality

In the included FFNx voice code:
- dialog open/close/paging detection is derived from `open_close_transition` and `win->state`:
  - `is_dialog_opening(code == 0)`
  - `is_dialog_paging(old_code == 10 && new_code == 1)` for FF8
  - `is_dialog_closing(new_code < old_code)` for FF8  
(Observed in provided FFNx source: `FFNx.../src/voice.cpp` ~L284–L320.)

In `ff8_show_dialog`, FFNx computes:
- `_is_dialog_opening`, `_is_dialog_starting`, `_is_dialog_paging` using those transition/state values (Observed in provided FFNx source: `voice.cpp` ~L1158–L1173.)

And when FFNx needs the visible dialog text for voice tokenization, it decodes:
- `win->text_data1` (Observed in provided FFNx source: `voice.cpp` occurrences around ~L1248, ~L1282, ~L1328.)

**Key inference (explicitly labeled as inference):**  
Because FFNx’s production voice capture relies on `open_close_transition` + `state` rather than pointer changes, it is likely (not proven, but strongly suggested) that the **pointer to dialog text is often stable** while the contents change between pages/lines; otherwise a cheaper pointer-change test would be a natural optimization. This supports your pointer‑reuse hypothesis. (Inference supported by observed FFNx behavior above; no direct “the pointer is reused” comment was found in the sources.)

### FFNx’s `field_get_dialog_string` signature differs from your mod’s

FFNx replaces `field_get_dialog_string` with:

```cpp
char *ff8_field_get_dialog_string(char *msg, int dialog_id) {
    ff8_current_window_dialog_id = dialog_id;
    return msg + *(uint32_t *)(msg + 4 * dialog_id);
}
```

(Observed in provided FFNx source: `FFNx.../src/voice.cpp` ~L960–L965.)

Your mod currently declares:

```cpp
typedef char* (__cdecl *FieldGetDialogString_t)(int messageId);
static char* __cdecl Hook_field_get_dialog_string(int messageId)
```

(Observed in provided source: `src/field_dialog.cpp` ~L61 and ~L631.)

That mismatch is significant enough that it can explain why your hook is either not being hit or not safely usable.

### Echo‑S dependency visibility

FFNx documentation explicitly calls out “Voice acting!” and mentions Echo‑S as the first mod to take advantage of it for FF8. citeturn13view0 Echo‑S 8’s page distributes an FFNx-based package (“Download FFNX Echo‑S 8 DEMO”). citeturn9search0

Echo‑S does not appear to publish its own low-level hooking source publicly in the materials retrieved here; therefore, the most defensible conclusion is that **Echo‑S relies on FFNx’s hooking layer** (voice/text pipeline) rather than a separate independently verifiable text hook. (Unspecified beyond the dependency evidence above.)

---

## Hypothesis evaluation and mapping of `text_data1` vs `text_data2`

### Pointer-reuse hypothesis

**Supported (indirectly) by:**
- Your own `show_dialog` code assumes pointer changes are a safe “new text” proxy; FFNx instead uses transition/state, suggesting pointer-change is insufficient. (Observed in provided source: `Hook_show_dialog` ~L867–L871; observed in provided FFNx source: `voice.cpp` ~L284–L320 and ~L1158–L1173.)
- Your missed “thought-like” lines align with the class of text that may update rapidly inside the same window without reallocating.

**Not directly proven by sources:**
- None of the retrieved sources explicitly state “FF8 reuses the same `text_data1` pointer and overwrites it in place.” So this remains a high-probability inference, not a confirmed fact.

### Wrong window stride/offset hypothesis

**Strongly supported and effectively confirmed** by:
- Concrete disagreement between your window stride (`0x38`) and FFNx’s struct size (`0x3C`). (Observed in provided source vs provided FFNx source.)
- Your `[RAWDUMP]` showing implausible pointers/states for windows other than 0, consistent with mis-stride reads. (Observed in provided log.)

### `text_data1` (+0x08) vs `text_data2` (+0x0C) usage mapping

**What is known from sources you provided:**
- FFNx defines both fields (`text_data1`, `text_data2`). (Observed in provided FFNx source: `ff8.h` ~L584–L595.)
- FFNx voice capture decodes **only `text_data1`** for spoken dialog tokenization in multiple places. (Observed in provided FFNx source: `voice.cpp` lines near ~L1248, ~L1282, ~L1328.)
- Your mod currently reads **only `text_data1`**. (Observed in provided source: `GetWinText1` and its usages e.g. `ScanAndSpeakAllWindows` ~L428–L434; `Hook_show_dialog` ~L865–L875.)

**What is not specified / not found:**
- No retrieved FFNx code in the provided snapshot uses `text_data2` for dialog capture.
- No retrieved community doc explicitly explains what `text_data2` contains for specific opcodes/modes.

**Therefore (explicitly):**  
A definitive mapping of which FF8 dialog types populate `text_data2` is **unspecified** in the available sources. The correct move is to instrument and empirically map it (see experiments below).

### How field opcodes relate to dialog windows (context for mapping)

The FF8 field script opcode list identifies core message opcodes:
- `MES` (0x047), `ASK` (0x04A), `MESMODE` (0x106), etc. citeturn9search2turn9search1  
These docs explain which opcodes open message windows and how option lines are specified, giving you a framework for correlating opcode execution with changes in `text_data1`/`text_data2`. citeturn9search1turn9search2

---

## Prioritized code changes, diagnostics, and a confirmatory test plan

### High-priority code changes

#### Fix the window stride and add `text_data2`

**Goal:** Make `GetWindowObj(i)` correct for all windows and enable probing of both text pointers.

**Change**
- `WIN_OBJ_SIZE`: `0x38` → `0x3C`
- add `WIN_OBJ_TEXT2_OFFSET = 0x0C`
- add `GetWinText2(winObj)` and scan/speak based on whichever pointer is valid and non-empty.

**Pseudo/C++**
```cpp
// Constants
static const int WIN_OBJ_SIZE = 0x3C;          // was 0x38
static const int WIN_OBJ_TEXT1_OFFSET = 0x08;  // text_data1
static const int WIN_OBJ_TEXT2_OFFSET = 0x0C;  // text_data2 (new)

// Helpers
static inline char* GetWinText1(uint8_t* winObj) {
    return *(char**)(winObj + WIN_OBJ_TEXT1_OFFSET);
}
static inline char* GetWinText2(uint8_t* winObj) {
    return *(char**)(winObj + WIN_OBJ_TEXT2_OFFSET);
}
```

**Scan logic adjustment (conceptual)**
- For each window, try decode from `text_data1`; if empty/invalid, try `text_data2`; if both valid, log both and prefer the one that changes with paging/transition.

**Why this matters**
- It resolves confirmed misalignment issues and directly addresses scenes that may render in windows other than 0.
- It allows you to verify whether the missing lines appear in `text_data2`.

#### Fix `field_get_dialog_string` hook signature and logging

**Goal:** Actually see message IDs and capture “bypass window system” text reliably.

**Expected real signature (per FFNx)**
- `char* field_get_dialog_string(char* msgBase, int dialogId)`

**Change**
```cpp
typedef char* (__cdecl *FieldGetDialogString_t)(char* msgBase, int dialogId);

static char* __cdecl Hook_field_get_dialog_string(char* msgBase, int dialogId) {
    char* result = s_origGetDialogString(msgBase, dialogId);

    // Safe logging: log both args + result
    Log::Write("FieldDialog: [GETSTR-RAW] base=0x%08X id=%d -> ptr=0x%08X",
               (uint32_t)(uintptr_t)msgBase, dialogId, (uint32_t)(uintptr_t)result);

    // decode guarded by ProbeGetstrResult(result), then store pending as you already do
    ...
    return result;
}
```

**Key diagnostic expectation**
- After this change, you should immediately see `GETSTR-RAW` lines in the log during normal dialog, because `MES` (and related message opcodes) depend on message retrieval.

If you still see none, then:
- you are not actually hooking the function being called (e.g., the call target differs from what you resolved), or
- FFNx (the driver) has already rerouted that call to a different stub after you resolve it.

In that case, the next experiment is to hook the call site inside `opcode_mes` instead of the callee function (see “call-site hook experiment” below).

#### Replace pointer-equality de-dup with a prefix-hash (or transition/state gating)

**Goal:** Detect in-place buffer overwrites while keeping performance safe.

**Minimal-change approach:** Keep pointer check, but if pointer is unchanged, compute a small hash of the first N bytes and compare to last hash.

A fast FNV‑1a 32-bit hash uses the standard constants (offset basis 2166136261, prime 16777619). citeturn7search48

**Pseudo/C++**
```cpp
static uint32_t fnv1a32_prefix(const uint8_t* p, size_t n) {
    uint32_t h = 2166136261u;
    for (size_t i = 0; i < n; i++) h = (h ^ p[i]) * 16777619u;
    return h;
}

static char* s_sdLastTextPtr[MAX_WINDOWS];
static uint32_t s_sdLastHash[MAX_WINDOWS];

...

char* text1 = GetWinText1(winObj);
if (!text1 || !IsValidTextPointer(text1) || !ProbePointer(text1)) return result;

uint32_t h = fnv1a32_prefix((const uint8_t*)text1, 64); // 32–128 bytes is usually enough
if (text1 == s_sdLastTextPtr[window_id] && h == s_sdLastHash[window_id]) {
    return result; // truly unchanged
}
s_sdLastTextPtr[window_id] = text1;
s_sdLastHash[window_id] = h;

// now decode and process
```

**Alternative (more “FFNx-like”)**
- Instead of hashing, read `open_close_transition` and/or `win->state` and only decode on:
  - opening,
  - “starting” transition,
  - paging transitions,
  - option change.  
This mirrors FFNx voice logic (observed in provided FFNx source: `voice.cpp` ~L284–L320 and ~L1158–L1173).

**Performance bounds**
- Hashing 64 bytes per `show_dialog` call at ~150 calls/sec is ~9.6 KB/sec of byte reads and trivial CPU overhead.
- You retain the primary win: you still avoid a full `Decode(..., 512)` on every call.

#### Decide how to handle ellipsis-only lines

If you want a screen-reader-friendly representation of silent/ellipsis thoughts, adjust `TrimDecoded` to preserve lines that are entirely dots/spaces.

**Option A (keep raw periods)**
- Trim whitespace only, not periods.

**Option B (convert to a token)**
- If the trimmed result becomes empty but the raw contains at least 3 dots, emit e.g. `"(pause)"` or `"(thinking...)"`.

Example:
```cpp
static std::string TrimDecodedPreserveEllipsis(const std::string& text) {
    std::string trimmed = TrimWhitespace(text);
    if (trimmed.find_first_not_of(".") == std::string::npos) {
        // all dots
        return "(...)"; // or "" if you truly want silence
    }
    return TrimWhitespaceAndOuterDots(text);
}
```

### Core diagnostic experiments (ordered)

#### Experiment: log both text pointers and key state fields per window

**Add to `DiagRawWindowDump`**
- `text_data1` pointer + first 8 bytes hex
- `text_data2` pointer + first 8 bytes hex
- `open_close_transition` (0x1C) and `win->state` (0x24)

This lets you differentiate:
- “text in `text_data2`” vs
- “text overwritten in place in `text_data1`” vs
- “window id != 0 used.”

**Suggested log format**
```
[RAWDUMP] w=%d winObj=0x%08X
  t1=0x%08X h1=%08X st=%u tr=%d
  t2=0x%08X h2=%08X st=%u tr=%d
```

#### Experiment: confirm pointer reuse explicitly

In `Hook_show_dialog` log:
- window_id
- `text_data1` pointer
- prefix-hash
- decoded (first 60 chars)
- `open_close_transition`, `state`

**Expected signatures**
- If pointer reuse occurs: same `ptr=0xXXXXXXXX` appears across multiple different `hash=` values and different decoded strings.
- If pointer reuse does not occur: pointer changes whenever string changes, hashes follow pointer changes.

#### Experiment: call-site hook for `field_get_dialog_string` (if function hook still shows no GETSTR)

Because you already know `opcode_mes` and the relative offset to the call, you can detour the call instruction site (like FFNx does for some hooks) instead of detouring the callee. This avoids “already patched callee” edge cases.

**Concept**
- At `opcode_mes + 0x5D`, replace `CALL field_get_dialog_string` with CALL to your wrapper.
- Your wrapper must preserve calling convention and then call the original target.

This is more invasive but can definitively answer “is the call executed?”

### Candidate offsets/fields to probe table

| Offset | Field | Type (FFNx struct) | Expected semantics | Why probe for missed dialog |
|---:|---|---|---|---|
| 0x08 | `text_data1` | `char*` | Primary window text buffer (used by FFNx voice decoding) | Missing lines may be overwritten in place here |
| 0x0C | `text_data2` | `char*` | Secondary text buffer (semantics unspecified in retrieved sources) | Some dialog types may populate this, not `text_data1` |
| 0x18 | `win_id` | `uint8_t` | Window id | Correlate with `show_dialog(window_id)` |
| 0x1A | `mode1` | `uint16_t` | Message/window mode (semantics unspecified here) | May differ for “thought” vs “normal” |
| 0x1C | `open_close_transition` | `int16_t` | Transition code: opening/paging/closing | FFNx uses this for reliable dialog state detection |
| 0x24 | `state` | `uint32_t` | Message opcode/state machine | FFNx uses this for paging detection |
| 0x29–0x2C | question fields | bytes | ASK option bookkeeping | Helps correlate ASK/AASK with buffer changes |
| 0x30 | `field_30` | `uint16_t` | Battle dialog id (FFNx uses it as dialog id change) | Battle text change detection |
| 0x34/0x38 | callbacks | `uint32_t` | UI callback pointers | Confirms struct size/stride correctness |

### Mermaid flowchart: call flow and probe insertion points

```mermaid
flowchart TD
  A[Field script executes opcode] --> B{Opcode is message-related?}
  B -- MES / AMES / MESW / ASK / AASK --> C[opcode_mes(...) etc]
  C --> D[CALL field_get_dialog_string @ opcode_mes+0x5D]
  D -->|PROBE 1: log base ptr + dialogId + result ptr| D1[Hook_field_get_dialog_string]
  D --> E[CALL set_window_object @ opcode_mes+0x66]
  E -->|updates ff8_win_obj.text_data1/text_data2 + state/transition| F[pWindowsArray / windows[]]
  F --> G[show_dialog(window_id, state, a3) called frequently]
  G -->|PROBE 2: log win->text_data1/text_data2 ptr + hash + open_close_transition + win->state| H[Hook_show_dialog]
  H --> I[Decode + de-dup + SpeakText]

  B -- nonstandard/bypass --> J[Other engine path]
  J -->|still may call field_get_dialog_string OR directly mutate window buffers| F

  K[PollWindows timer] -->|PROBE 3: scan all windows with correct stride 0x3C| L[ScanAndSpeakAllWindows]
  L --> I
```

### Short test plan with expected confirming log signatures

#### Test scene set
- Hallway scene with Squall/Quistis (your reported repro).
- A multi-choice prompt (`ASK`) to validate option changes and window usage.
- A tutorial prompt / borderless dialog (`MESMODE`) to validate mode transitions. (`MESMODE` exists as opcode 0x106 in opcode list.) citeturn9search2
- One battle dialog line (to validate `field_30` / battle handling if you use it).

#### Hypothesis confirmations

1. **Window stride bug**
   - Before fix: `[RAWDUMP]` shows nonsense pointers for windows 1–7 (already observed).
   - After fix: `[RAWDUMP]` shows either `NULL` or readable pointers for windows 1–7, and any missing dialog that previously used a nonzero window begins to appear.

2. **Pointer reuse**
   - After adding hash logging: same pointer address + different hash values across time, with different decoded strings:
     - `ptr=0x1557CAA0 hash=AAAA... text="Line 1"`
     - `ptr=0x1557CAA0 hash=BBBB... text="Line 2"`
   - Correspondingly, previously missing “thought” lines now appear without requiring pointer changes.

3. **Wrong `field_get_dialog_string` signature / hook not firing**
   - After fixing signature: `FieldDialog: [GETSTR-RAW] ...` appears immediately during any dialog.
   - If still absent: switch to call-site hook at `opcode_mes+0x5D` and expect call counts to log.

4. **`text_data2` usage**
   - If missing lines were in `text_data2`, you will see moments where:
     - `text_data1` is null/empty while `text_data2` decodes to meaningful text (or vice versa).
   - Adapt speaking logic accordingly.

5. **Ellipsis trimming**
   - If you decide to preserve ellipsis lines, you should see new decoded outputs like `"(...)”` where previously nothing was spoken.

---

## References and actionable next steps

### Web references

(Links are provided in a code block per system constraints.)

```text
FFNx (official GitHub repository): https://github.com/julianxhokaxhiu/FFNx
FFNx README (mentions FF8 voice acting + Echo-S): https://github.com/julianxhokaxhiu/FFNx
Echo-S 8 (Tsunamods page; FFNx-based demo download): https://www.tsunamods.com/echo-s-8/
FF8 Field Script Opcodes list (Final Fantasy Inside wiki): https://wiki.ffrtt.ru/index.php/FF8/Field/Script/Opcodes
ASK opcode details (Final Fantasy Inside wiki): https://wiki.ffrtt.ru/index.php/FF8/Field/Script/Opcodes/04A_ASK
FNV hash constants reference (Wikipedia): https://en.wikipedia.org/wiki/Fowler%E2%80%93Noll%E2%80%93Vo_hash_function
```

Key supporting citations:
- FFNx README: voice acting + Echo‑S mention. citeturn13view0  
- Echo‑S 8 page: distributes FFNx Echo‑S demo (dependency signal). citeturn9search0  
- FF8 field opcode list and ASK opcode details. citeturn9search2turn9search1  
- FNV constants table (for hash strategy). citeturn7search48  

### Actionable next steps checklist (with provided-source file/line references)

**Fix correctness first (likely to immediately increase capture rate):**
- [ ] **Change window stride**: `WIN_OBJ_SIZE` from `0x38` to `0x3C`. (Observed in provided source: `src/field_dialog.cpp` ~L259; FFNx struct size evidence in provided FFNx source: `FFNx.../src/ff8.h` ~L584–L615.)
- [ ] **Add `text_data2` support**: define `WIN_OBJ_TEXT2_OFFSET = 0x0C`, implement `GetWinText2`, and log/probe it in `DiagRawWindowDump()` and scanning. (Observed in provided FFNx source: `text_data2` exists at `ff8.h` ~L588–L590.)
- [ ] **Fix `field_get_dialog_string` typedef + hook signature** to `(char* msgBase, int dialogId)` and log `base`, `id`, `result`. (Observed in provided source: typedef at `src/field_dialog.cpp` ~L61; hook at ~L631; FFNx reference behavior in provided FFNx source: `voice.cpp` ~L960–L965; address resolution parity in provided source: `src/ff8_addresses.cpp` ~L549–L554.)

**Then fix “missed lines despite hook firing” (pointer reuse / de-dup):**
- [ ] **Replace pointer-only de-dup in `Hook_show_dialog`** with pointer+hash gating (or transition/state gating). (Observed in provided source: early return at `src/field_dialog.cpp` ~L867–L871.)
- [ ] **Log `open_close_transition` and `state`** alongside pointer/hash to compare with FFNx’s paging heuristic (for FF8: paging when old_code==10 and new_code==1). (Observed in provided FFNx source: `voice.cpp` ~L296–L304; `ff8_win_obj` has `open_close_transition` at `ff8.h` ~L600–L602.)

**Quality-of-experience decisions (optional, but relevant for Squall thoughts):**
- [ ] **Decide whether to speak ellipsis-only lines**; if yes, adjust `TrimDecoded()` so `"..."` isn’t dropped. (Observed in provided source: `TrimDecoded` ~L407–L413; min-length checks ~L651 / ~L876.)

**If GETSTR still never appears after signature fix:**
- [ ] Implement a **call-site hook** at `opcode_mes + 0x5D` (you already resolve this relationship) to prove the call executes and capture the args at the call boundary. (Observed in provided source: resolution comment and code in `src/ff8_addresses.cpp` ~L549–L554; FFNx does similar relative-call resolution in provided FFNx source `ff8_data.cpp` ~L845.)

**Immediate “did we fix it?” checkpoints (log-based):**
- [ ] Confirm `[RAWDUMP]` shows sane pointers for windows 1–7 after stride fix (not `0x00800080` / `0x00FFFF00`). (Observed problematic values in provided log `ff8_accessibility.log`.)
- [ ] Confirm `FieldDialog: [GETSTR-RAW]` lines appear during normal dialog after signature fix (currently none). (Observed in provided log: no GETSTR at all.)
- [ ] Confirm `show_dialog` logs show pointer-stable but hash-changing cases during the hallway scene if pointer reuse is real.

---

*End of report.*