// bahamut_light_overlay.cpp -- the Deep Sea Research Center blue-light puzzle
// (#bahamut-light). See bahamut_light_overlay.h for the design notes and
// bahamut_light_model.inl for the script transcript everything here rests on.
//
// v0.100.0: the prompt no longer goes through DialogInject. It borrowed window
// slot 2, which is the slot Bahamut's entire scene speaks through, and left it
// stuck open. The model file carries the log lines that prove it.
//
// <windows.h> MUST come before ff8_addresses.h: that header declares accessors
// returning Windows types without pulling windows.h in itself, so it relies on
// the includer having defined it first.
#include <windows.h>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "bahamut_light_overlay.h"
#include "ff8_accessibility.h"
#include "ff8_addresses.h"
#include "field_dialog.h"   // v0.102.0: IsDialogOpen -- never arm over a scene
#include "battle_tts.h"     // v0.103.0: diagnostic screenshots, see BAHAMUT_LIGHT_DIAG_SHOTS
#include "field_navigation.h"  // v0.106.0: the Game Controls box
#include "mod_forward_decls.h"

#include "bahamut_light_model.inl"

// v0.103.0, SWITCHED OFF v0.107.0. Aaron: "You may also want to add automatic
// screenshot capture for this mini-game while we build so you can verify the
// logs against the visual look of the light. We can disable / remove the
// automatic screenshot capture once this puzzle is done." It is done -- "Okay I
// think it is working well. I got through the Ruby Dragons and to the Bahamut
// Boss Battle" -- and the pictures it was for are already in the record:
// bahamut_05_armed.png blazing white against bahamut_06_safe.png nearly dark,
// three seconds apart, which is what proved the retimed script and the lethal
// byte share one clock. The scaffolding stays, at 0, because the next change to
// the timing will want it again; every use of it compiles out.
#define BAHAMUT_LIGHT_DIAG_SHOTS 0

namespace BahamutLightOverlay {

using namespace BahamutLightModel;

// ============================================================================
// Constants
// ============================================================================

// The fallback arming delay, for a player who reaches a Hojo safe pocket before
// the first pulse and so never gives us a var[1024] edge to trigger on.
static const DWORD SETTLE_MS = 6000;

// The two answer keys. Deliberately NOT directional: `battlekun1`/`battlekun4`
// throw a random encounter for a held direction while the light is armed, and
// the v0.98.0 ASK -- whose cursor IS the arrow keys -- walked Aaron straight
// into one at 00:19:29. G and S are free in field mode; the keys the mod and
// the game already claim there are the arrows, `-`, `+`, `\`, Backspace, Space
// and the function keys (field_nav_handlekeys.inl).
static const int KEY_GUIDE = 'G';
static const int KEY_SKIP  = 'S';

// The Game Controls screen, in the shape every other mini-game's uses: short
// lines, the keys last. Lines are kept inside the game window's wrap so the
// box does not have to reflow them.
static const char* const CONTROLS_TEXT =
    "THE BLUE LIGHT\n"
    "Walk only while the light is dark.\n"
    "Move when you hear \"Go\".\n"
    "Stand still when you hear \"Stop\".\n"
    "Moving while it glows starts a fight.\n"
    "The mod slows the light down so this\n"
    "is playable: about 3 seconds dark,\n"
    "3 seconds lit, and Stop comes a\n"
    "beat before the light comes back.\n"
    "G guide me    S skip the puzzle";

static const char* const PROMPT_TEXT =
    "Blue light puzzle. Press G to be guided at a slower pace, "
    "or S to skip the puzzle.";

// ============================================================================
// State
// ============================================================================
static bool   s_initialized   = false;
static bool   s_inField       = false;
static DWORD  s_enteredMs     = 0;
static bool   s_pulseSeen     = false;   // var[1024] observed at 1 this visit
static int    s_mode          = (int)BL_MODE_NONE;
static int    s_promptCount   = 0;
static bool   s_promptSpoken  = false;
static DWORD  s_promptAtMs    = 0;
static int    s_prevCue       = (int)BL_CUE_NONE;
static DWORD  s_cueSinceMs    = 0;       // when the current cue began
static bool   s_spokeLast     = false;   // the last speech on the wire was ours
static DWORD  s_spokeAtMs     = 0;
static bool   s_skipAnnounced = false;
static bool   s_keyLatched    = false;   // an answer key is still held
// v0.101.0: the early-Stop timer. Armed on the Go edge; the arm edge is only a
// backstop for the cycles where the field script was suspended in between.
static bool   s_stopWarned    = false;
static DWORD  s_goAtMs        = 0;
// v0.102.0: the metronome. While it runs the mod owns var[1024] and holds the
// Director out of it with var[1028]; if a write ever fails it stops and the
// module falls back to watching the game's own pulse (the v0.101.0 path).
static int    s_tempo         = (int)BL_TEMPO_OFF;
static bool   s_tempoOk       = true;
static DWORD  s_tempoAtMs     = 0;
static bool   s_tempoWarned   = false;
// v0.103.0: the script retiming. When this takes, the game's own loop runs at
// the mod's tempo and the LIGHT agrees with the byte -- so the metronome stays
// off and the module goes back to simply watching the edges.
static bool   s_scriptPatched = false;
// v0.105.0: the loop's address, remembered so the steady-state re-check is
// twelve reads rather than a window search.
static const uint32_t* s_scriptLoop = nullptr;
static DWORD  s_scriptCheckAt = 0;
static int    s_scriptRedos   = 0;
// v0.104.0: parked while a scene is up. The v0.103.0 BAT said "Stop" six times
// over Bahamut's own dialogue because the park reset the warn flag every tick.
static bool   s_tempoParked   = false;
// v0.106.0: the Game Controls box, open while the prompt is unanswered.
static bool   s_boxOpen       = false;
#if BAHAMUT_LIGHT_DIAG_SHOTS
static int    s_shotCount     = 0;
static const int BL_MAX_SHOTS = 24;   // enough for a corridor, not a disk-filler
#endif

// ============================================================================
// Guarded memory access. No C++ objects in any function that uses __try
// (MSVC C2712); see tests/lint_seh.py.
// ============================================================================
static bool ReadVarB(int index, int* out)
{
    __try {
        *out = (int)(*(volatile const unsigned char*)
                     (uintptr_t)(BL_VAR_BASE + (unsigned)index));
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

static bool WriteVarB(int index, unsigned char v)
{
    __try {
        *(volatile unsigned char*)(uintptr_t)(BL_VAR_BASE + (unsigned)index) = v;
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

// ============================================================================
// Field identity
// ============================================================================
//
// THE ID IS THE AUTHORITY AND THE NAME IS ONLY EVER A LOG LINE.
// `pCurrentFieldName` lags the id by seconds -- the v0.99.0 BAT caught it
// mid-lag, logging `id=846 name='sdisle1'` on entry and `pCurrentFieldName =
// 'sdcore1'` thirty seconds later -- so a module that writes memory must key on
// the id or it will go on writing several seconds into the NEXT field, where
// 0x01CFEDBC means something else entirely.
//
// 846 is not a derived id. sdcore2's own `Director::default` opens with
// `PSHM_W var[84] ; PSHN_L 846 ; ==` -- the game asking "did the party arrive
// from sdcore1?" -- so the number is the script's, not a table's. The 2026-08-26
// log confirmed it live: `pCurrentFieldId = 0x01CD2FC0 (val=846)` alongside
// `pCurrentFieldName = 'sdcore1'`.
static bool FieldIsPuzzle()
{
    if (FF8Addresses::pCurrentFieldId) {
        return (unsigned)(*FF8Addresses::pCurrentFieldId) == BL_FIELD_ID;
    }
    const char* fn = FF8Addresses::pCurrentFieldName;
    return fn && _stricmp(fn, BL_FIELD) == 0;
}

// ============================================================================
// Speech
// ============================================================================
static void SayCue(const char* text)
{
    const DWORD now = GetTickCount();
    const bool busy = ScreenReader::IsSpeaking();
    const BlSpeakAct act = BlSpeakDecision(busy, s_spokeLast,
                                           (unsigned)(now - s_spokeAtMs),
                                           BL_OWN_MS);
    if (act == BL_SPEAK_DROP) {
        // Deliberately silent: a queued cue arrives after the window it was
        // describing has closed, and the next edge is at most a few seconds away.
        Log::Field("BahamutLight: cue '%s' DROPPED -- something else is speaking", text);
        s_spokeLast = false;
        return;
    }
    ScreenReader::Speak(text, act == BL_SPEAK_INTERRUPT);
    s_spokeLast = true;
    s_spokeAtMs = now;
}

static void CloseBox(const char* why)
{
    if (!s_boxOpen) return;
    s_boxOpen = false;
    FieldNavigation::CloseControlsBox();
    Log::Field("BahamutLight: game controls closed (%s)", why);
}

static void CommitMode(BlMode mode)
{
    CloseBox("answered");
    s_mode          = (int)mode;
    s_skipAnnounced = false;
    // Force the next reading to look like an edge so the player is told where
    // they stand immediately rather than waiting out the current phase.
    s_prevCue    = (int)BL_CUE_NONE;
    s_stopWarned = false;
    s_goAtMs     = GetTickCount();

    if (mode == BL_MODE_SKIP) {
        ScreenReader::Speak("Light puzzle off. Walk to the light.", true);
        Log::Field("BahamutLight: SKIP chosen (holding var[%d]=1, var[%d]=0)",
                   BL_VAR_SAFE, BL_VAR_LIGHT);
    } else {
        ScreenReader::Speak("Guiding you. Move on Go, stand still on Stop.", true);
        s_tempo       = (int)BL_TEMPO_OFF;   // started on the next tick
        s_tempoWarned = false;
        Log::Field("BahamutLight: GUIDE chosen");
    }
    s_spokeLast = true;
    s_spokeAtMs = GetTickCount();
}

// ============================================================================
// Per-visit state
// ============================================================================
static void EnterField()
{
    s_inField       = true;
    s_enteredMs     = GetTickCount();
    s_pulseSeen     = false;
    s_mode          = (int)BL_MODE_NONE;
    s_promptCount   = 0;
    s_promptSpoken  = false;
    s_promptAtMs    = s_enteredMs;
    s_prevCue       = (int)BL_CUE_NONE;
    s_cueSinceMs    = s_enteredMs;
    s_spokeLast     = false;
    s_skipAnnounced = false;
    s_keyLatched    = false;
    s_stopWarned    = false;
    s_goAtMs        = s_enteredMs;
    s_tempo         = (int)BL_TEMPO_OFF;
    s_tempoOk       = true;
    s_tempoAtMs     = s_enteredMs;
    s_tempoWarned   = false;
    s_scriptPatched = false;
    s_scriptLoop    = nullptr;
    s_scriptCheckAt = 0;
    s_scriptRedos   = 0;
    s_tempoParked   = false;
    s_boxOpen       = false;
#if BAHAMUT_LIGHT_DIAG_SHOTS
    s_shotCount     = 0;
#endif
    Log::Field("BahamutLight: entered the puzzle field (id %u)", BL_FIELD_ID);
}

static void LeaveField()
{
    if (!s_inField) return;
    CloseBox("left the field");
    s_inField = false;
    s_mode    = (int)BL_MODE_NONE;
    s_prevCue = (int)BL_CUE_NONE;
    s_tempo   = (int)BL_TEMPO_OFF;
    Log::Field("BahamutLight: left the puzzle field; state reset");
}

// ============================================================================
// Diagnostic capture (v0.103.0, temporary)
// ============================================================================
#if BAHAMUT_LIGHT_DIAG_SHOTS
// One shot per phase change. `phase` goes into the filename so a directory
// listing pairs each picture with what the byte said at the moment it was taken.
static void ShotPhase(const char* phase)
{
    if (s_shotCount >= BL_MAX_SHOTS) return;
    s_shotCount++;
    char base[512];
    snprintf(base, sizeof base, "%s\\bahamut_%02d_%s",
             BattleTTS::GetScreenshotDir(), s_shotCount, phase);
    BattleTTS::RequestScreenshotAsync(base);
    Log::Field("BahamutLight: [SHOT] %d/%d requested at %s.png (phase=%s)",
               s_shotCount, BL_MAX_SHOTS, base, phase);
}
#else
static void ShotPhase(const char*) {}
#endif

// ============================================================================
// The script retiming
// ============================================================================
//
// Reads the loaded code array through the pointer the engine's own opcode fetch
// uses (0x0052A629), verifies thirty-five dwords against sdcore1's file, and
// rewrites twelve. No C++ objects in a function with __try (MSVC C2712).
// The window searched around the pointer, in dwords each way. sdcore1's whole
// code array is 1,517 dwords, and the v0.103.0 BAT found the pointer sitting 217
// dwords into it, so 2,048 either side covers the array from anywhere inside it.
static const int BL_WIN = 2048;
static uint32_t s_win[BL_WIN * 2];

// Read the window in chunks so one unmapped page does not lose the whole search.
// A chunk that faults is filled with a word that cannot appear in the signature.
static bool ReadWindow(const uint32_t* base)
{
    bool any = false;
    const int CHUNK = 256;
    for (int off = 0; off < BL_WIN * 2; off += CHUNK) {
        __try {
            for (int i = 0; i < CHUNK; i++)
                s_win[off + i] = base[off - BL_WIN + i];
            any = true;
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            for (int i = 0; i < CHUNK; i++) s_win[off + i] = 0xFFFFFFFFu;
        }
    }
    return any;
}

static const uint32_t* ScriptPointer()
{
    __try {
        return *(const uint32_t* volatile*)(uintptr_t)BL_SCRIPT_CODE_PTR;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return nullptr;
    }
}

static bool WriteScriptWord(const uint32_t* base, int winIdx, uint32_t word)
{
    __try {
        uint32_t* p = (uint32_t*)(base + (winIdx - BL_WIN));
        *p = word;
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

// Returns true once the loop is running at the mod's tempo -- whether this call
// wrote it or a previous one did.
// Twelve reads at the remembered address. True while the loop is still ours.
static bool StillRetimed()
{
    if (s_scriptLoop == nullptr) return false;
    __try {
        for (int i = 0; i < BL_PATCH_COUNT; i++)
            if (s_scriptLoop[BL_PATCH[i].idx] != BL_PATCH[i].to) return false;
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

static int TryPatchScript()
{
    // The cheap check first: nothing has moved, nothing to do. This is the case
    // on every tick except the one after a battle rebuilt the field.
    if (StillRetimed()) return (int)BLP_ALREADY;

    const uint32_t* base = ScriptPointer();
    if (base == nullptr || !ReadWindow(base)) return (int)BLP_FAIL;

    int  found  = -1;
    bool already = false;
    const int hits = BlScriptScan(s_win, BL_WIN * 2, &found, &already);
    if (hits != 1) {
        if (s_scriptLoop != nullptr)
            Log::Field("BahamutLight: Director loop NOT found in +/-%d dwords of %p "
                       "(%d matches). win[%d]=0x%08X win[%d]=0x%08X",
                       BL_WIN, (const void*)base, hits,
                       BL_WIN + 637, s_win[BL_WIN + 637],
                       BL_WIN + 645, s_win[BL_WIN + 645]);
        s_scriptLoop = nullptr;
        return (int)BLP_FAIL;
    }

    s_scriptLoop = base + (found - BL_WIN);
    if (already) return (int)BLP_ALREADY;

    int wrote = 0;
    for (int i = 0; i < BL_PATCH_COUNT; i++) {
        if (!WriteScriptWord(base, found + BL_PATCH[i].idx, BL_PATCH[i].to)) {
            Log::Field("BahamutLight: script write FAILED at dword %d after %d words -- "
                       "the loop is now half-retimed; falling back to the metronome, "
                       "which owns the byte regardless of what the script does",
                       BL_PATCH[i].idx, wrote);
            s_scriptLoop = nullptr;
            return (int)BLP_FAIL;
        }
        wrote++;
    }
    return (int)BLP_WROTE;
}

// ============================================================================
// Public API
// ============================================================================
void Initialize()
{
    if (s_initialized) return;
    s_initialized = true;
    s_inField     = false;
    s_mode        = (int)BL_MODE_NONE;
    s_prevCue     = (int)BL_CUE_NONE;
    Log::Mod("BahamutLightOverlay: Initialized.");
}

void Shutdown()
{
    if (!s_initialized) return;
    LeaveField();
    s_initialized = false;
}

bool IsPromptPending()
{
    return s_initialized && s_inField && s_mode == (int)BL_MODE_NONE;
}

void Update()
{
    if (!s_initialized) return;

    const bool here = FieldIsPuzzle();
    if (here != s_inField) {
        if (here) EnterField(); else LeaveField();
    }
    if (!here) return;

    const DWORD now = GetTickCount();

    // ---- the two bytes -----------------------------------------------------
    int light = 0, safe = 0, story = 0;
    const bool okL = ReadVarB(BL_VAR_LIGHT, &light);
    const bool okS = ReadVarB(BL_VAR_SAFE,  &safe);
    const bool okC = ReadVarB(BL_VAR_STORY, &story);
    const bool readOk = okL && okS;

    if (readOk && light == 1) s_pulseSeen = true;

    const bool answered = (s_mode != (int)BL_MODE_NONE);

    // ---- the prompt --------------------------------------------------------
    if (!answered) {
        // Hold the light disarmed while the player is listening to a question
        // the mod asked. The v0.98.0 ASK let a pulse arm underneath it and the
        // player was pulled into a random encounter mid-prompt.
        if (BlHoldSafeWhilePrompting(true, false) && readOk &&
            BlSkipWriteNeeded(light, safe)) {
            WriteVarB(BL_VAR_SAFE,  1);
            WriteVarB(BL_VAR_LIGHT, 0);
        }

        const bool armed = BlPromptArmed(true, false, s_pulseSeen,
                                         okC && story != 1,
                                         (unsigned)(now - s_enteredMs),
                                         (unsigned)SETTLE_MS);
        if (BlPromptDue(armed, s_promptCount, BL_PROMPT_MAX, s_promptSpoken,
                        (unsigned)(now - s_promptAtMs), BL_PROMPT_REPEAT_MS)) {
            s_promptSpoken = true;
            s_promptAtMs   = now;
            s_promptCount++;
            if (!s_boxOpen) {
                s_boxOpen = true;
                const bool gameBox = FieldNavigation::OpenControlsBox(CONTROLS_TEXT);
                Log::Field("BahamutLight: game controls open (%s)",
                           gameBox ? "the game's own window"
                                   : "the mod's overlay -- the game window refused");
            }
            ScreenReader::Speak(PROMPT_TEXT, true);
            s_spokeLast = true;
            s_spokeAtMs = now;
            Log::Field("BahamutLight: prompt %d of %d (pulseSeen=%d story=%d)",
                       s_promptCount, BL_PROMPT_MAX, (int)s_pulseSeen, story);
        }

        const bool gk = (GetAsyncKeyState(KEY_GUIDE) & 0x8000) != 0;
        const bool sk = (GetAsyncKeyState(KEY_SKIP)  & 0x8000) != 0;
        const BlMode pick = BlKeyChoice(gk, sk);
        if (pick != BL_MODE_NONE && !s_keyLatched) {
            s_keyLatched = true;
            CommitMode(pick);
            return;
        }
        if (!gk && !sk) s_keyLatched = false;

        // Out of repeats and still no answer: guide, which changes nothing.
        if (s_promptSpoken && s_promptCount >= BL_PROMPT_MAX &&
            (unsigned)(now - s_promptAtMs) >= BL_PROMPT_REPEAT_MS) {
            Log::Field("BahamutLight: no answer after %d prompts -- defaulting to guide",
                       s_promptCount);
            CommitMode(BlModeOnTimeout());
        }
        return;
    }

    // ---- skip: hold the game's own disarm pair -----------------------------
    if (s_mode == (int)BL_MODE_SKIP) {
        if (readOk && BlSkipWriteNeeded(light, safe)) {
            const bool a = WriteVarB(BL_VAR_SAFE,  1);
            const bool b = WriteVarB(BL_VAR_LIGHT, 0);
            if (!a || !b) {
                Log::Field("BahamutLight: SKIP hold write FAILED -- disengaging");
                s_mode = (int)BL_MODE_GUIDE;
            } else if (!s_skipAnnounced) {
                s_skipAnnounced = true;
                Log::Field("BahamutLight: SKIP engaged -- var[%d] 1, var[%d] 0",
                           BL_VAR_SAFE, BL_VAR_LIGHT);
            }
        }
        return;
    }

    // ---- guide: retime the script if we can --------------------------------
    //
    // One attempt per visit. If it takes, the game's own loop runs at the mod's
    // tempo, the light animation and the byte share one clock, and the mod goes
    // back to simply watching the edges -- with an exact lead, because the safe
    // window is no longer the earliest of three random tails.
    // Re-checked, not done once: a battle reloads the field script from the
    // archive and the retiming goes with it. The steady-state cost of this is
    // twelve reads a second at a remembered address.
    if (s_scriptCheckAt == 0 ||
        (unsigned)(now - s_scriptCheckAt) >= BL_SCRIPT_RECHECK_MS) {
        s_scriptCheckAt = now;
        const int r = TryPatchScript();
        if (BlPatchWorthLogging(r, s_scriptPatched)) {
            if (r == (int)BLP_WROTE && s_scriptPatched) {
                s_scriptRedos++;
                Log::Field("BahamutLight: script RETIMED AGAIN (%d) -- a battle reloaded "
                           "the field and the loop was back on its own clock",
                           s_scriptRedos);
            } else if (r == (int)BLP_WROTE) {
                Log::Field("BahamutLight: script RETIMED -- safe %d frames (%u ms), "
                           "armed %d frames (%u ms); the light and the byte now share "
                           "one clock",
                           BlRtSafeFrames(), (unsigned)BlRtSafeFrames() * BL_FRAME_MS,
                           BlRtArmedFrames(), (unsigned)BlRtArmedFrames() * BL_FRAME_MS);
            } else {
                Log::Field("BahamutLight: script retiming LOST and could not be "
                           "re-applied -- the metronome takes over");
            }
        }
        s_scriptPatched = (r != (int)BLP_FAIL);
    }

    // ---- guide: the metronome ---------------------------------------------
    //
    // The mod owns var[1024] and keeps the Director out of it with var[1028] --
    // the same pair the corridor's own Hojo safe pockets write. See the model
    // file for why taking the clock beats predicting it.
    if (!s_scriptPatched && s_tempoOk) {
        bool wrote = true;
        if (readOk && safe != 1) wrote = WriteVarB(BL_VAR_SAFE, 1) && wrote;

        if (s_tempo == (int)BL_TEMPO_OFF) {
            // Wait out the mod's own "Guiding you..." line before starting the
            // clock. In the v0.103.0 BAT the first "Stop" landed 2,578 ms after
            // commit, while that sentence was still on the wire, and was dropped.
            if (ScreenReader::IsSpeaking()) {
                if (readOk && light != 0) WriteVarB(BL_VAR_LIGHT, 0);
                return;
            }
            s_tempo       = (int)BL_TEMPO_SAFE;
            s_tempoAtMs   = now;
            s_tempoWarned = false;
            wrote = WriteVarB(BL_VAR_LIGHT, 0) && wrote;
            Log::Field("BahamutLight: metronome started -- safe %u ms, armed %u ms, "
                       "Stop %u ms before the arm",
                       BL_TEMPO_SAFE_MS, BL_TEMPO_ARMED_MS, BL_TEMPO_LEAD_MS);
            SayCue(BlCueText(BL_CUE_GO));
        }

        const bool dialogUp = FieldDialog::IsDialogOpen();

        // PARKED. Koe's voice line and Bahamut's whole ending put a window up and
        // take player control; the metronome holds the corridor safe and says
        // nothing until the screen clears, then starts a fresh full window. The
        // v0.103.0 BAT reset the warn flag on every parked tick and so talked
        // over Bahamut six times.
        if (dialogUp) {
            if (!s_tempoParked) {
                s_tempoParked = true;
                Log::Field("BahamutLight: metronome parked -- a scene is up");
            }
            s_tempo     = (int)BL_TEMPO_SAFE;
            s_tempoAtMs = now;
            if (readOk && light != 0) wrote = WriteVarB(BL_VAR_LIGHT, 0) && wrote;
            if (!wrote) {
                s_tempoOk = false;
                s_tempo   = (int)BL_TEMPO_OFF;
                Log::Field("BahamutLight: metronome write FAILED while parked -- "
                           "falling back to watching the game's own pulse");
            }
            return;
        }
        if (s_tempoParked) {
            s_tempoParked = false;
            s_tempoWarned = false;
            s_tempoAtMs   = now;
            Log::Field("BahamutLight: metronome resumed -- a fresh safe window");
        }

        const unsigned inPhase = (unsigned)(now - s_tempoAtMs);
        const BlTempo  phase   = (BlTempo)s_tempo;

        if (BlTempoWarnDue(phase, s_tempoWarned, inPhase,
                           BL_TEMPO_SAFE_MS, BL_TEMPO_LEAD_MS)) {
            s_tempoWarned = true;
            Log::Field("BahamutLight: Stop (metronome, %u ms of %u into the safe phase)",
                       inPhase, BL_TEMPO_SAFE_MS);
            SayCue(BlCueText(BL_CUE_STOP));
        }

        if (BlTempoPhaseOver(phase, inPhase, BL_TEMPO_SAFE_MS, BL_TEMPO_ARMED_MS)) {
            if (phase == BL_TEMPO_SAFE) {
                // BlTempoMayArm is belt and braces: the parked branch above has
                // already returned on an open dialog, and this is the guard the
                // test pins so a future edit cannot arm into a scene.
                if (BlTempoMayArm(dialogUp)) {
                    s_tempo     = (int)BL_TEMPO_ARMED;
                    s_tempoAtMs = now;
                    wrote = WriteVarB(BL_VAR_LIGHT, 1) && wrote;
                    Log::Field("BahamutLight: metronome armed");
                    ShotPhase("armed");
                }
            } else {
                s_tempo       = (int)BL_TEMPO_SAFE;
                s_tempoAtMs   = now;
                s_tempoWarned = false;
                wrote = WriteVarB(BL_VAR_LIGHT, 0) && wrote;
                ShotPhase("safe");
                SayCue(BlCueText(BL_CUE_GO));
            }
        } else if (readOk && light != BlTempoLightVar((BlTempo)s_tempo)) {
            // Something else moved the byte -- Koe::touch re-arms it behind
            // itself. Put it back where the metronome says it should be.
            wrote = WriteVarB(BL_VAR_LIGHT, BlTempoLightVar((BlTempo)s_tempo)) && wrote;
        }

        if (!wrote) {
            s_tempoOk = false;
            s_tempo   = (int)BL_TEMPO_OFF;
            Log::Field("BahamutLight: metronome write FAILED -- falling back to "
                       "watching the game's own pulse");
        }
        return;
    }

    // ---- guide: the early Stop --------------------------------------------
    //
    // The cue that matters is this one, not the edge. See the model file: the
    // arm is Go + 35, 50 or 60 frames and only the earliest is safe to plan
    // against, so the warning goes out a fixed lead before Go + 35.
    const unsigned warnDelay = s_scriptPatched
        ? BlWarnDelayMs(BlRtSafeFrames(), BL_TEMPO_LEAD_MS)
        : BlStopWarnDelayMs();
    if (s_prevCue == (int)BL_CUE_GO &&
        BlStopWarnDue(true, s_stopWarned, (unsigned)(now - s_goAtMs), warnDelay)) {
        s_stopWarned = true;
        Log::Field("BahamutLight: Stop (early, %u ms after Go; the arm is at %u ms, "
                   "%s)", (unsigned)(now - s_goAtMs),
                   (unsigned)(s_scriptPatched ? BlRtSafeFrames() : BL_SAFE_MIN_FRAMES)
                       * BL_FRAME_MS,
                   s_scriptPatched ? "known exactly -- the script is retimed"
                                   : "the earliest of three random tails");
        SayCue(BlCueText(BL_CUE_STOP));
    }

    // ---- guide: the edges --------------------------------------------------
    const BlCue cue = BlCueFor(readOk, light, safe);
    if (!BlCueChanged((BlCue)s_prevCue, cue)) return;

    // Both phases measured. The v0.99.0 BAT confirmed the frame rate the script
    // runs at: armed held 5,297 ms against 158 script frames, and safe 1,703 and
    // 1,734 ms against 35-60 -- so the field script ticks at 30 Hz and the safe
    // window really is between 1.2 and 2.0 seconds.
    const unsigned heldMs = (unsigned)(now - s_cueSinceMs);
    Log::Field("BahamutLight: %s  (previous phase %s held %u ms; "
               "script says armed %d frames, safe %d/%d/%d)",
               BlCueText(cue),
               BlCueText((BlCue)s_prevCue),
               heldMs,
               BlArmedFrames(),
               BlSafeFrames(BL_F_TAIL_LO),
               BlSafeFrames(BL_F_TAIL_MID),
               BlSafeFrames(BL_F_TAIL_HI));

    const bool warnedThisWindow = s_stopWarned;
    if (cue == BL_CUE_GO) { s_goAtMs = now; s_stopWarned = false; }
    if (cue == BL_CUE_GO)   ShotPhase("safe");
    if (cue == BL_CUE_STOP) ShotPhase("armed");

    s_prevCue    = (int)cue;
    s_cueSinceMs = now;

    // The arm edge is a backstop. Saying "Stop" again over a player who stopped
    // 800 ms ago is noise; saying nothing when the early warning never fired is
    // the v0.100.0 bug back again.
    if (cue == BL_CUE_STOP && !BlSpeakOnArm(warnedThisWindow)) {
        // The lead ACTUALLY achieved, measured against the delay this cycle used.
        // v0.104.0 printed it against the legacy delay and so reported 2,757 ms
        // where the real lead was 984.
        Log::Field("BahamutLight: arm reached %u ms after the early Stop -- not repeating",
                   heldMs > warnDelay ? heldMs - warnDelay : 0u);
        return;
    }
    if (cue == BL_CUE_STOP) {
        Log::Field("BahamutLight: arm reached with no early warning "
                   "(the field script was suspended) -- speaking late");
    }
    SayCue(BlCueText(cue));
}

}  // namespace BahamutLightOverlay
