# Next Session Prompt: v0.17.8.1 — Bug #3 (tutorial TTS garbage)

## Greeting

Start with `## Claude Says`. Read `DEVNOTES.md` and THIS file before any work.

## Where we are at session open

**v0.17.8.0 BAT'd CLEAN.** Aaron confirmed both fixes:
- Bug #5 (GF-BP diagnostic spam) — no GF breakpoint TTS announcement heard.
- Bug #6 (GF damage announce during HP-SUB window) — heard the GF take damage in place of the character.

**Two possible session-open paths:**

### Path A: Aaron has already pushed v0.17.8.0

GitHub HEAD is now at the v0.17.8.0 commit. Verify by calling `github:list_commits` and confirming the top commit is titled `v0.17.8.0`. Then proceed to bug #3 work.

### Path B: Aaron hasn't pushed yet

Local tree has v0.17.8.0 ready to push (CHANGELOG top heading matches `FF8OPC_VERSION`). Push utility: `Utilities/push_to_github.ps1`. Aaron pushes; Claude doesn't. Confirm push completed via `github:list_commits` before starting v0.17.8.1 work.

Either way: once pushed, proceed to bug #3.

## Bug #3: Tutorial TTS garbage

### What we know

From the 2026-05-18 Fire Cavern playthrough (v0.16.5.2 BAT triage):

```
[POLL] win[0] Speaking: ",e 3in*retone3 e~HP~B:All08E%~!/..."
```

…fires after `[TUTO]` mode transitions from 10 → 1 (tutorial scene completes). The text is garbled — looks like tokens / unprintable bytes / unset character codes. SAPI then attempts to speak this garbage, which the screen reader reads as a string of nonsense.

### Two candidate fixes

**Option A (preferred): Token rejection in POLL path.** Filter out `[…]` tokens (FF8 dialog control codes that weren't decoded), unprintable bytes (< 0x20 except whitespace), and characters above the standard ASCII range that aren't part of valid encoded text. Reject the whole speak attempt if it fails some validity threshold (e.g. >25% non-letters).

Pro: catches similar future cases regardless of source.
Con: needs careful threshold tuning so legitimate punctuation-heavy text isn't rejected.

**Option B (alternative): Suppress POLL win[0] briefly on tutorial-end.** Track the `[TUTO]` mode 10→1 transition and suppress any POLL speech from win[0] for ~500 ms after.

Pro: surgical, no risk of rejecting legitimate text.
Con: tutorial-specific. If garbage appears in other contexts, this won't catch it.

**Suggested approach**: implement Option A as the primary fix (general-purpose) and consider Option B as a fallback if Option A proves too aggressive.

### Investigation steps

1. **Locate the POLL pipeline.** Likely in `dialog_inject.cpp`, `src/dialog_inject.h`, or one of the field/menu TTS files. The log line `[POLL] win[0] Speaking: ...` should be searchable via `filesystem:edit_file` dryRun across `src/`.

2. **Find the `[TUTO]` mode tracker.** Search for `[TUTO]` log emissions to identify which file emits the mode 10→1 transition log.

3. **Find the win[0] data source.** The POLL path likely reads from FF8's window object array (`ff8_win_obj` per memory notes). Identify the read site and the text buffer.

4. **Trace the text encoding.** FF8 dialog text uses encoded byte values (the FF8 character map). The mod has `DecodeFF8String()` somewhere — if the POLL path bypasses that, raw bytes would look garbled. Alternatively, the engine may have already started overwriting the buffer before the POLL fires (race condition).

5. **Reproduce the garble.** Aaron's 2026-05-18 Fire Cavern playthrough should have logged a clean example. Search `Logs/ff8_dialog.log` (or wherever `[POLL]` lives) for the date/time of that playthrough.

### Implementation plan (Option A)

Pseudocode for the filter:

```cpp
static bool IsGarbledText(const char* text) {
    if (!text || !text[0]) return false;  // empty is fine, not garbled
    int len = (int)strlen(text);
    int badCount = 0;
    int totalCount = 0;
    
    for (int i = 0; i < len; i++) {
        unsigned char c = (unsigned char)text[i];
        totalCount++;
        
        // Reject control codes (except whitespace)
        if (c < 0x20 && c != ' ' && c != '\t' && c != '\n') { badCount++; continue; }
        
        // Reject undecoded FF8 control tokens (typically '~' followed by hex)
        // — Actual character depends on what the tokens look like in the log
        if (c == '~' && i + 1 < len && (isxdigit(text[i+1]) || text[i+1] == ' ')) {
            badCount++;
            continue;
        }
        
        // Reject extended ASCII / control bytes
        if (c >= 0x7F && c < 0xA0) { badCount++; continue; }
    }
    
    if (totalCount == 0) return false;
    return (badCount * 100 / totalCount) > 25;  // >25% bad → reject
}

// In the POLL Speak path, before calling SAPI:
if (IsGarbledText(textBuf)) {
    Log::Battle("[POLL] win[0] REJECTED garbled text: \"%s\"", textBuf);
    return;  // don't speak
}
```

The exact tokens to reject need to be matched against the actual sample (`",e 3in*retone3 e~HP~B:All08E%~!/..."`). The `~HP~`, `~B:`, `%~` patterns look like undecoded FF8 dialog macros — `~` followed by a letter/code is a strong garble signal.

### Files likely involved

- `src/dialog_inject.cpp` / `src/dialog_inject.h` — dialog injection pipeline (per memory notes)
- `src/field_dialog.cpp` (or similar field dialog file) — field-side POLL hooks
- `src/ff8_text_decode.cpp` / `.h` — text decoder (if it exists)
- `src/battle_tts.cpp` — has its own DecodeFF8String? Worth checking.

Step 1 will narrow it down.

## File-access reminder

**Mod files are on Windows.** Use `filesystem:`-prefixed MCP tools. BAT logs at `Logs/ff8_*.log`. F11 screenshots at `Logs/screenshots/f11_HHMMSS_NNN.png` — use `filesystem:read_media_file` to view (load via `tool_search("read image media file")` if not already loaded).

For mid-file log searches, use `filesystem:edit_file` with `dryRun=true` and a unique `oldText` anchor.

## Session checkpoint rule reminder

After v0.17.8.1 implementation:
1. Bump `FF8OPC_VERSION` in `src/ff8_accessibility.h` to `0.17.8.1`.
2. Add top entry `## v0.17.8.1` in `CHANGELOG.md`.
3. Update `DEVNOTES.md` to reflect the new in-tree state.
4. Rewrite this `NEXT_SESSION_PROMPT.md` for the BAT-triage step.

After BAT:
1. Mark v0.17.8.1 ✅ in DEVNOTES (move bug #3 from active to closed).
2. If pushing: Aaron runs `Utilities/push_to_github.ps1`. Claude never pushes.
3. Suggest next chapter — likely bug #4 (party-as-NPC) or bug #1 (Quistis FMV race) depending on Aaron's priority.

## Remaining Fire Cavern bug list

1. Quistis' FMV in the Infirmary fired prematurely — deferred
2. ~~Manual field navigation direction lag~~ — ✅ closed by v0.17.7.6.2
3. Garbage announced by TTS following completion of a tutorial scene — **current target (v0.17.8.1)**
4. Party member announced as NPC in catalog when party consists of just two members — deferred
5. ~~Breakpoint on display timer announced when GF sequence starts~~ — ✅ closed by v0.17.8.0
6. ~~Damage not announced when a character is summoning and the GF takes the damage in place of the character~~ — ✅ closed by v0.17.8.0
