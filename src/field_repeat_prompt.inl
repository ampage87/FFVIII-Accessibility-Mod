// field_repeat_prompt.inl -- position in a run of identical choice dialogs.
//
// Included from field_dialog.cpp before field_dialog_scan.inl.
//
// ============================================================================
// WHY (v0.41.0, #102 -- the Galbadia Missile Base password)
// ============================================================================
//
// The Missile Base password is not a keyboard. It is **four ordinary AASK choice
// dialogs in a row**, all on one message (`gmtika4.msd` #23):
//
//     {3F}Please enter / your password.{3E} / A / B / C / D / E / F
//
// `AASK(win 0, msg 23, firstLine 2, lastLine 7, default 2, cancel 2, 20, 20)`,
// so the six choices are lines 2-7 and the returned value is the 0-based choice
// index. The script (dwords 2189-2320) checks 4, 3, 4, 0 over A-F -- **E, D, E,
// A** -- and the Wounded Soldier says so once, in message 19, before it starts.
//
// AASK is already hooked, so the prompt and the six letters read. What is
// missing is **position**: four identical prompts with nothing to say which
// letter is being entered.
//
// **And on one path there is nothing at all.** The four AASKs reuse the same
// window without closing it, so the scanner sees the same prompt each time with
// the cursor reset to the default -- which is line 2, "A". Its dedup is
// `prompt == lastChoicePrompt && curChoice == lastSpokenChoice`, so **if the
// player picks A for any letter, the next prompt is byte-for-byte the state the
// scanner just spoke and it says NOTHING** -- no prompt, no letter, no cue that
// a new entry has begun. The password is E, D, E, **A**, so the last letter of
// the only password in the game hits that path every time.
//
// The counter therefore has to speak on the "nothing changed" path too, which
// is why it is a prefix owned here rather than a tweak inside the dedup.
//
// ============================================================================
// WHAT v0.41.0 GOT WRONG, AND WHAT THE ENGINE SAYS INSTEAD (v0.42.0)
// ============================================================================
//
// v0.41.0 counted **consecutive openings** -- one per ASK/AASK opcode firing --
// on the assumption that an opcode fires once per question. It does not. The
// 2026-08-20 BAT is unambiguous: forty `[REPEAT]` lines in three seconds,
// cycling "Letter 1 of 4" through "Letter 4 of 4" over and over while the
// player had not entered a single letter, and the run-position line firing on
// every one of them. What was being counted was FRAMES.
//
// **A blocking field opcode re-runs from the same instruction every frame**, and
// the engine's own dispatcher says so in four instructions (`0x0052A621`):
//
//     mov  cx, [esi + 0x176]        ; IP = dword index of THIS instruction
//     ...
//     call [0x00B8DE94 + op*4]      ; the handler
//     test al, 2
//     je   +0x19                    ; <- skip the increment
//     inc  word ptr [esi + 0x176]   ; only advance when the handler says so
//
// and AASK's waiting path (`0x00529755`) is `mov eax, 5 / ret`. **5 & 2 == 0**,
// so while the dialog is up the IP does not move and the same instruction fires
// again next frame. That is not an inference about how menus feel -- it is the
// return value the handler loads, and `tests/repeat_prompt_compile.cpp` decodes
// both fragments out of FF8_EN.exe rather than trusting this comment.
//
// So the discriminator is the INSTRUCTION POINTER, not the count of firings:
//
//   * same (context, IP) as last time  -> the same question, still unanswered:
//     say nothing, count nothing;
//   * a different IP                   -> the script advanced, so this is the
//     next question.
//
// Position is then **where that IP sits in the order the sites were first
// seen**, which makes a retry come out right for free: a wrong password jumps
// back to the first AASK, whose IP is already site 1, so it says "Letter 1 of
// 4" again rather than "Letter 5 of 4". Nothing about the four call sites is
// written down here -- the script's own addresses are the table.
//
// SCOPE. This fires only for a (field, prompt substring) pair in the table
// below, and only when an ASK/AASK OPCODE fired -- not on the poll rescan,
// which has no instruction pointer to offer. Every other choice dialog in the
// game is untouched.
// ============================================================================

struct RepeatPromptCue
{
    const char* field;        // field name this applies to
    const char* promptMatch;  // substring that identifies the prompt
    int         total;        // how many the script asks for
    const char* noun;         // what one of them is called
};

static const RepeatPromptCue REPEAT_PROMPT_CUES[] = {
    { "gmtika4", "password", 4, "Letter" },
};
static const int REPEAT_PROMPT_CUE_COUNT =
    (int)(sizeof(REPEAT_PROMPT_CUES) / sizeof(REPEAT_PROMPT_CUES[0]));

// Byte offset of the VM instruction pointer inside a field-script context. The
// dispatcher reads it at 0x0052A621 and increments it at 0x0052A675; the same
// offset is documented independently in field_nav_mapjump_diag.inl, which uses
// it to log the firing IP of a MAPJUMP. The probe decodes it out of the two
// exe fragments rather than copying it from either file.
static const unsigned REPEAT_PROMPT_IP_OFFSET = 0x176;

// Case-insensitive substring, so the table can be written in ordinary words
// whatever case the game's own text uses.
static bool RepeatPromptContains(const std::string& hay, const char* needle)
{
    if (!needle || !*needle) return false;
    const size_t n = strlen(needle);
    if (hay.size() < n) return false;
    for (size_t i = 0; i + n <= hay.size(); i++) {
        size_t k = 0;
        for (; k < n; k++) {
            char a = hay[i + k], b = needle[k];
            if (a >= 'A' && a <= 'Z') a = (char)(a - 'A' + 'a');
            if (b >= 'A' && b <= 'Z') b = (char)(b - 'A' + 'a');
            if (a != b) break;
        }
        if (k == n) return true;
    }
    return false;
}

static int RepeatPromptFind(const char* field, const std::string& prompt)
{
    if (!field) return -1;
    for (int i = 0; i < REPEAT_PROMPT_CUE_COUNT; i++) {
        const RepeatPromptCue& c = REPEAT_PROMPT_CUES[i];
        if (!UrgentSameName(field, c.field)) continue;
        if (!RepeatPromptContains(prompt, c.promptMatch)) continue;
        return i;
    }
    return -1;
}

// One AASK call site: the script context it ran in and the instruction index
// inside that context's method. Two entities can be at the same IP, so both
// halves are part of the identity.
struct RepeatPromptSite { uintptr_t ctx; unsigned ip; };

static const int REPEAT_PROMPT_MAX_SITES = 16;

static std::string      s_repeatPrompt;
static RepeatPromptSite s_repeatSites[REPEAT_PROMPT_MAX_SITES];
static int              s_repeatSiteCount = 0;
static RepeatPromptSite s_repeatLastSite = { 0, 0 };
static bool             s_repeatHaveLast = false;
static char             s_repeatPrefix[64] = {0};

static bool RepeatPromptSameSite(const RepeatPromptSite& a, const RepeatPromptSite& b)
{
    return a.ctx == b.ctx && a.ip == b.ip;
}

static void RepeatPromptReset()
{
    s_repeatPrompt.clear();
    s_repeatSiteCount = 0;
    s_repeatHaveLast  = false;
}

// Reading a pointer the engine handed us. No object with a destructor in this
// frame (MSVC C2712 -- tests/lint_seh.py); the host probe substitutes a plain
// read so the same offset is exercised without SEH.
static bool RepeatPromptReadIp(uintptr_t entityPtr, unsigned* out)
{
    if (!entityPtr || !out) return false;
#if defined(FF8_REPEAT_PROMPT_HOST_TEST)
    *out = (unsigned)*(const uint16_t*)
        ((const uint8_t*)entityPtr + REPEAT_PROMPT_IP_OFFSET);
    return true;
#else
    bool ok = false;
    __try {
        *out = (unsigned)*(volatile const uint16_t*)
            ((const uint8_t*)entityPtr + REPEAT_PROMPT_IP_OFFSET);
        ok = true;
    } __except (EXCEPTION_EXECUTE_HANDLER) { ok = false; }
    return ok;
#endif
}

// Called once per ASK/AASK opcode, with the prompt the scanner decoded and the
// VM context the opcode handler was given. Leaves a prefix for the scanner to
// put in front of whatever it says next -- or, when the scanner is about to say
// nothing, a line of its own.
static void RepeatPromptOnOpcode(const std::string& prompt, uintptr_t entityPtr)
{
    s_repeatPrefix[0] = '\0';

    const char* field = FF8Addresses::pCurrentFieldName ? FF8Addresses::pCurrentFieldName : "";
    const int cueIdx = RepeatPromptFind(field, prompt);
    if (cueIdx < 0) { RepeatPromptReset(); return; }

    RepeatPromptSite site;
    site.ctx = entityPtr;
    site.ip  = 0;
    if (!RepeatPromptReadIp(entityPtr, &site.ip)) { RepeatPromptReset(); return; }

    if (prompt != s_repeatPrompt) { RepeatPromptReset(); s_repeatPrompt = prompt; }

    // THE WHOLE FIX. Same instruction as last time = the same question, still
    // waiting for an answer -- AASK re-runs every frame until confirm.
    if (s_repeatHaveLast && RepeatPromptSameSite(site, s_repeatLastSite)) return;
    s_repeatLastSite = site;
    s_repeatHaveLast = true;

    const RepeatPromptCue& c = REPEAT_PROMPT_CUES[cueIdx];

    int pos = 0;
    for (int i = 0; i < s_repeatSiteCount; i++)
        if (RepeatPromptSameSite(s_repeatSites[i], site)) { pos = i + 1; break; }

    if (pos == 0) {
        // More distinct call sites than the script asks for means the cue's
        // total is wrong for this scene. Start the run over rather than count
        // past the end.
        if (s_repeatSiteCount >= c.total || s_repeatSiteCount >= REPEAT_PROMPT_MAX_SITES)
            s_repeatSiteCount = 0;
        s_repeatSites[s_repeatSiteCount++] = site;
        pos = s_repeatSiteCount;
    }

    snprintf(s_repeatPrefix, sizeof(s_repeatPrefix), "%s %d of %d. ",
             c.noun, pos, c.total);
    Log::Dialog("FieldDialog: [REPEAT] %s(site ip=%u)%s", s_repeatPrefix, site.ip,
                (pos == 1) ? " (first)" : "");
}

// Take the pending prefix, if any. Consumed on read so it can never attach to a
// second utterance.
static std::string RepeatPromptTakePrefix()
{
    std::string p = s_repeatPrefix;
    s_repeatPrefix[0] = '\0';
    return p;
}

static bool RepeatPromptHasPrefix() { return s_repeatPrefix[0] != '\0'; }
