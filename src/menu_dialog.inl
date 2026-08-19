// menu_dialog.inl -- v0.29.0 (#88)
//
// The menu system's SHARED yes/no window. PART OF menu_tts.cpp -- TEXTUAL
// INCLUDE, before any screen that puts one up. Do NOT compile standalone.
//
// ---------------------------------------------------------------------------
// WHY THIS EXISTS
//
// Every confirmation dialog in the main menu goes through one window opener,
// and none of them was being read. The audit of #88 found the Save screen's
// **"Data exists.  Overwrite?"** announced as nothing at all -- a modal that
// defaults to No, so a blind player who pressed Confirm expecting to save was
// silently dropped back to the block list believing he had saved.
//
// The plumbing:
//     0x004C2B10(bodyText, id)  ->  0x004C2A20(bodyText, 0, 0, id)
//     0x004C2A20:
//         arg2 == 0 -> option 1 text = 0x004BD630(0, 0, 0x2F, 0)   ("Yes")
//         arg3 == 0 -> option 2 text = 0x004BD630(0, 0, 0x30, 0)   ("No")
//         [0x01D77300] = body      (0x004C2A5A)
//         [0x01D772F0] = option 1  (0x004C2AA6)
//         [0x01D772E0] = option 2  (0x004C2AAE)
//
// Those three are GLOBALS, not module fields, so one reader serves every screen
// that opens the window: Save's overwrite and format prompts, Magic's transfer
// confirmation, Switch's junction exchange, the Junction screen's "Off" and
// "Keep previous setting", GF's "Don't learn anything?", Status's "Won't learn
// anything". The text is whatever the game itself put on screen, so this cannot
// drift the way a hardcoded sentence would -- and it is the fix for having
// assumed a YES/NO order once already (v0.26.2, the exam's "Really?" dialog,
// which lists NO first).
// ---------------------------------------------------------------------------

static const uintptr_t MDLG_BODY_PTR = 0x01D77300;
static const uintptr_t MDLG_OPT1_PTR = 0x01D772F0;
static const uintptr_t MDLG_OPT2_PTR = 0x01D772E0;

static const int MDLG_RAW_MAX = 512;

// Raw bytes only: the pointer chase needs SEH, and SEH may not share a function
// with anything that unwinds (MSVC C2712). Decoding happens in the caller.
struct MenuDialogRaw
{
    unsigned char body[MDLG_RAW_MAX];
    int           bodyLen;
    unsigned char opt[2][64];
    int           optLen[2];
};

static bool MenuDialogReadRaw(MenuDialogRaw& raw)
{
    memset(&raw, 0, sizeof(raw));
    __try {
        const uint8_t* b = *(const uint8_t* volatile*)MDLG_BODY_PTR;
        if (!b) return false;
        int i = 0;
        for (; i < MDLG_RAW_MAX - 1; i++) {
            const uint8_t c = b[i];
            raw.body[i] = c;
            if (c == 0x00 || c == 0x01) { i++; break; }
        }
        raw.bodyLen = i;

        const uintptr_t srcs[2] = { MDLG_OPT1_PTR, MDLG_OPT2_PTR };
        for (int o = 0; o < 2; o++) {
            const uint8_t* p = *(const uint8_t* volatile*)srcs[o];
            if (!p) continue;
            int k = 0;
            for (; k < 63; k++) {
                const uint8_t c = p[k];
                raw.opt[o][k] = c;
                if (c == 0x00 || c == 0x01) { k++; break; }
            }
            raw.optLen[o] = k;
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
    return raw.bodyLen > 0;
}

// The whole dialog as one spoken line: the question, then the option the cursor
// is on. `cursor` is the module's own 2-option cursor byte.
//
// The option WORDS are read off the window rather than assumed. On this screen
// family they are usually Yes then No, but the mod has already been caught once
// by a dialog that lists NO first, and the cost of being wrong here is that the
// player is told the opposite of what they are about to confirm.
static bool MenuDialogCompose(int cursor, char* out, size_t n)
{
    if (!out || n == 0) return false;
    out[0] = '\0';

    MenuDialogRaw raw;
    if (!MenuDialogReadRaw(raw)) return false;

    std::string body = FF8TextDecode::Decode(raw.body, (size_t)raw.bodyLen);
    // FF8 pads its dialog text out to fixed columns; collapse the runs so SAPI
    // does not stall mid-question.
    std::string tidy;
    tidy.reserve(body.size());
    bool sp = false;
    for (size_t i = 0; i < body.size(); i++) {
        const char c = body[i];
        if (c == ' ' || c == '\n' || c == '\t') { sp = true; continue; }
        if (sp && !tidy.empty()) tidy += ' ';
        sp = false;
        tidy += c;
    }
    if (tidy.empty()) return false;
    snprintf(out, n, "%s", tidy.c_str());

    const int c = (cursor == 1) ? 1 : 0;
    if (raw.optLen[c] > 0) {
        std::string opt = FF8TextDecode::Decode(raw.opt[c], (size_t)raw.optLen[c]);
        // Trim; the option strings carry trailing padding too.
        while (!opt.empty() && (opt[opt.size()-1] == ' ' || opt[opt.size()-1] == '\n'))
            opt.erase(opt.size() - 1);
        if (!opt.empty()) {
            const size_t l = strlen(out);
            snprintf(out + l, (l < n) ? n - l : 0, ". %s", opt.c_str());
        }
    }
    return out[0] != '\0';
}
