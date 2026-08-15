// autotest_cmd.inl - File-polled remote key-injection channel for automated BATs
// v0.18.3.213 (#72 test automation infrastructure).
//
// WHY: Claude's computer-use tooling cannot deliver extended-scancode keys
// (arrow keys) to FF8's DirectInput-only keyboard pipeline -- the same failure
// class InjectKey/PressKey fixed in v0.14.102 (arrows need KEYEVENTF_SCANCODE
// + KEYEVENTF_EXTENDEDKEY, wVk = 0). Letter keys arrive, arrows never do.
// This channel lets external automation write simple commands to a text file;
// the mod injects them with correct scancode semantics via SendInput.
//
// COMMAND FILE (consumed -- deleted after each successful read):
//   <dev Logs dir>\autotest_cmd.txt
// GRAMMAR (one command per line, '#' starts a comment line):
//   KEY <NAME>[+<NAME>...] [holdMs]   hold key(s) together holdMs, default 120
//   WAIT <ms>                         pause queue for ms
// Key names: UP DOWN LEFT RIGHT X Z C A S D W Q E RETURN ESC SPACE TAB
//            EQUALS MINUS BACKSLASH PGUP PGDN
// Every consumed command is acknowledged in ff8_mod.log as an [AUTOTEST] line,
// so live log tailing (v0.18.3.212) doubles as the ack channel.
//
// SAFETY: completely inert unless the command file exists. Off switch:
// AUTOTEST_CMD_ENABLED 0 (gate, don't delete -- per project convention).
// v0.18.3.225: gated OFF by default for shipped builds -- this is a file-polled
// keystroke-injection surface and should not be active in a public release.
// Flip to 1 and rebuild to drive the game from automation during a test session.

// v0.20.70/.71 flipped this to 1 for an automated overnight session that never
// ran -- the agent that was to drive it could not be started. Returned to 0:
// what Aaron BATs should be exactly what he can push, and this is a file-polled
// keystroke-injection surface with no business in a release build. Nothing in
// .70 or .71 depends on it; the [GDTRACE] diagnostics are independent.
#define AUTOTEST_CMD_ENABLED 0

#if AUTOTEST_CMD_ENABLED

#include <share.h>     // _fsopen share modes
#include <sys/stat.h>  // _stat

namespace {  // file-local (included from dinput8.cpp only)

static const char* AT_CMD_PATH =
    "C:\\Users\\ampag\\OneDrive\\Documents\\FFVIII-Accessibility-Mod"
    "\\FF8_OriginalPC_mod\\Logs\\autotest_cmd.txt";

// v0.18.3.214: direction names inject BOTH the extended (arrow) and
// non-extended (numpad) scancode forms -- FF8 2013's default keyboard
// bindings are the NUMPAD directions (DIK_NUMPAD8/2/4/6), and the .213
// arrow-only injection moved nothing. Dual injection covers either binding.
// N-prefixed names force the numpad-only form for diagnostics.
// v0.18.3.218: each key also carries its VirtualKey. AtInject additionally
// issues a VK SendInput from inside the game process, so GetAsyncKeyState
// readers (the mod's own hotkeys: catalog =/-/\, F-keys, and the field
// camera-calibration observer's arrow sampling) see the key too. External
// OS-level synthesis was setting neither the DirectInput buffer NOR VK
// async state for arrows, so calibration never sampled. vk=0 => overlay only.
struct AtKeyDef { const char* name; WORD scan; bool ext; bool dual; BYTE vk; };
static const AtKeyDef AT_KEYS[] = {
    { "UP",        0x48, true,  true,  VK_UP     },
    { "DOWN",      0x50, true,  true,  VK_DOWN   },
    { "LEFT",      0x4B, true,  true,  VK_LEFT   },
    { "RIGHT",     0x4D, true,  true,  VK_RIGHT  },
    { "NUP",       0x48, false, false, 0         },
    { "NDOWN",     0x50, false, false, 0         },
    { "NLEFT",     0x4B, false, false, 0         },
    { "NRIGHT",    0x4D, false, false, 0         },
    { "X",         0x2D, false, false, 'X'       },
    { "Z",         0x2C, false, false, 'Z'       },
    { "C",         0x2E, false, false, 'C'       },
    { "A",         0x1E, false, false, 'A'       },
    { "S",         0x1F, false, false, 'S'       },
    { "D",         0x20, false, false, 'D'       },
    { "W",         0x11, false, false, 'W'       },
    { "Q",         0x10, false, false, 'Q'       },
    { "E",         0x12, false, false, 'E'       },
    { "RETURN",    0x1C, false, false, VK_RETURN },
    { "ESC",       0x01, false, false, VK_ESCAPE },
    { "SPACE",     0x39, false, false, VK_SPACE  },
    { "TAB",       0x0F, false, false, VK_TAB    },
    { "EQUALS",    0x0D, false, false, VK_OEM_PLUS  },
    { "MINUS",     0x0C, false, false, VK_OEM_MINUS },
    { "BACKSLASH", 0x2B, false, false, VK_OEM_5  },
    { "PGUP",      0x49, true,  false, VK_PRIOR  },
    { "PGDN",      0x51, true,  false, VK_NEXT   },
};

static const AtKeyDef* AtFindKey(const char* name)
{
    for (size_t i = 0; i < sizeof(AT_KEYS) / sizeof(AT_KEYS[0]); i++)
        if (_stricmp(AT_KEYS[i].name, name) == 0) return &AT_KEYS[i];
    return nullptr;
}

// v0.18.3.215: delivery switched from SendInput to the ChaseKeyboard DIK
// OVERLAY. Empirical .213/.214 result: OS-injected scancode events (both
// extended-arrow and numpad forms, JAWS exited) reach GetAsyncKeyState
// readers but never appear in FF8's DirectInput buffer for direction keys,
// while letters (X) do -- root cause not worth chasing when the mod already
// owns the GetDeviceState detour FF8 reads from. The overlay ORs our DIK
// bytes into every read at exactly that point: deterministic, focus-proof,
// screen-reader-proof. Dual scancode forms are preserved (both DIKs held).
static void AtInject(WORD scan, bool ext, bool down, BYTE vk)
{
    ChaseKeyboard::SetOverlayKey(
        ChaseKeyboard::ScancodeToDik((uint8_t)scan, ext), down);
    // v0.18.3.218: also raise the VK at OS level (SendInput from inside the
    // game process) so GetAsyncKeyState readers -- mod hotkeys and the field
    // camera-calibration observer -- see the key. vk==0 => overlay only.
    if (vk != 0) {
        INPUT inp    = {};
        inp.type     = INPUT_KEYBOARD;
        inp.ki.wVk   = vk;
        inp.ki.wScan = scan;
        inp.ki.dwFlags = (down ? 0 : KEYEVENTF_KEYUP)
                       | (ext ? KEYEVENTF_EXTENDEDKEY : 0);
        SendInput(1, &inp, sizeof(INPUT));
    }
}

struct AtCmd {
    int   nKeys;              // 0 => pure WAIT (or SHOT)
    bool  shot;               // v0.18.3.214: capture screenshot (focus-free F11)
    WORD  scan[8];            // dual-form directions use two slots
    bool  ext[8];
    BYTE  vk[8];              // v0.18.3.218: OS-level VK (0 = overlay only)
    DWORD holdMs;
    char  label[48];
};

static AtCmd  s_atQueue[64];
static int    s_atCount   = 0;      // queued
static int    s_atCur     = 0;      // executing index
static int    s_atPhase   = 0;      // 0=idle 1=holding 2=gap
static DWORD  s_atTick    = 0;
static DWORD  s_atPollTick = 0;

static void AtParseFile()
{
    FILE* f = _fsopen(AT_CMD_PATH, "r", _SH_DENYNO);
    if (!f) return;

    s_atCount = 0; s_atCur = 0; s_atPhase = 0;
    char line[256];
    while (fgets(line, sizeof(line), f) && s_atCount < 64) {
        // Strip newline / comments / blanks
        char* h = strchr(line, '#'); if (h) *h = '\0';
        char* nl = strpbrk(line, "\r\n"); if (nl) *nl = '\0';
        char tok[5][64]; int nTok = 0;
        const char* p = line;
        while (nTok < 5) {
            while (*p == ' ' || *p == '\t') p++;
            if (!*p) break;
            int n = 0;
            while (*p && *p != ' ' && *p != '\t' && n < 63) tok[nTok][n++] = *p++;
            tok[nTok][n] = '\0'; nTok++;
        }
        if (nTok == 0) continue;

        AtCmd& c = s_atQueue[s_atCount];
        memset(&c, 0, sizeof(c));

        if (_stricmp(tok[0], "WAIT") == 0 && nTok >= 2) {
            c.nKeys = 0;
            c.holdMs = (DWORD)atoi(tok[1]);
            snprintf(c.label, sizeof(c.label), "WAIT %lu", (unsigned long)c.holdMs);
            s_atCount++;
        } else if (_stricmp(tok[0], "SHOT") == 0) {
            c.nKeys = 0; c.shot = true; c.holdMs = 30;
            snprintf(c.label, sizeof(c.label), "SHOT");
            s_atCount++;
        } else if (_stricmp(tok[0], "KEY") == 0 && nTok >= 2) {
            // tok[1] = NAME[+NAME...]
            char names[64];
            strncpy_s(names, tok[1], _TRUNCATE);
            char* ctx = nullptr;
            char* t = strtok_s(names, "+", &ctx);
            bool ok = true;
            while (t && c.nKeys < 7) {
                const AtKeyDef* kd = AtFindKey(t);
                if (!kd) { ok = false; break; }
                c.scan[c.nKeys] = kd->scan;
                c.ext[c.nKeys]  = kd->ext;
                c.vk[c.nKeys]   = kd->vk;
                c.nKeys++;
                if (kd->dual) {  // also inject the numpad (non-extended) form
                    c.scan[c.nKeys] = kd->scan;
                    c.ext[c.nKeys]  = false;
                    c.vk[c.nKeys]   = 0;   // VK already raised by the arrow form
                    c.nKeys++;
                }
                t = strtok_s(nullptr, "+", &ctx);
            }
            c.holdMs = (nTok >= 3) ? (DWORD)atoi(tok[2]) : 120;
            if (c.holdMs < 30) c.holdMs = 30;
            if (ok && c.nKeys > 0) {
                snprintf(c.label, sizeof(c.label), "KEY %s %lu",
                         tok[1], (unsigned long)c.holdMs);
                s_atCount++;
            } else {
                Log::Mod("[AUTOTEST] parse error, unknown key in: %s", line);
            }
        } else {
            Log::Mod("[AUTOTEST] parse error, unknown command: %s", line);
        }
    }
    fclose(f);

    // Consume the file (retry once -- OneDrive transient EPERM pattern)
    if (remove(AT_CMD_PATH) != 0) { Sleep(50); remove(AT_CMD_PATH); }

    Log::Mod("[AUTOTEST] queued %d command(s)", s_atCount);
}

}  // anonymous namespace

namespace AutotestCmd {

static void Update()
{
    DWORD now = GetTickCount();

    // Executing?
    if (s_atCur < s_atCount) {
        AtCmd& c = s_atQueue[s_atCur];
        switch (s_atPhase) {
        case 0:  // start command
            if (c.shot) {
                SYSTEMTIME wt;
                GetLocalTime(&wt);
                char base[512];
                snprintf(base, sizeof(base), "%s\\at_%02d%02d%02d_%03d",
                         BattleTTS::GetScreenshotDir(),
                         wt.wHour, wt.wMinute, wt.wSecond, wt.wMilliseconds);
                BattleTTS::RequestScreenshotAsync(base);
                Log::Mod("[AUTOTEST] SHOT at_%02d%02d%02d_%03d.png",
                         wt.wHour, wt.wMinute, wt.wSecond, wt.wMilliseconds);
            } else if (c.nKeys > 0) {
                for (int i = 0; i < c.nKeys; i++) AtInject(c.scan[i], c.ext[i], true, c.vk[i]);
            }
            Log::Mod("[AUTOTEST] exec %s", c.label);
            s_atTick = now; s_atPhase = 1;
            break;
        case 1:  // holding / waiting
            if (now - s_atTick >= c.holdMs) {
                if (c.nKeys > 0)
                    for (int i = 0; i < c.nKeys; i++) AtInject(c.scan[i], c.ext[i], false, c.vk[i]);
                s_atTick = now; s_atPhase = 2;
            }
            break;
        case 2:  // inter-command gap (let the engine see the key edges)
            if (now - s_atTick >= 150) {
                s_atCur++; s_atPhase = 0;
                if (s_atCur >= s_atCount)
                    Log::Mod("[AUTOTEST] queue complete (%d command(s))", s_atCount);
            }
            break;
        }
        return;
    }

    // Idle: poll for a new command file every 250 ms.
    if (now - s_atPollTick < 250) return;
    s_atPollTick = now;

    struct _stat st;
    if (_stat(AT_CMD_PATH, &st) == 0 && st.st_size > 0)
        AtParseFile();
}

}  // namespace AutotestCmd

#else  // AUTOTEST_CMD_ENABLED == 0

namespace AutotestCmd { static void Update() {} }

#endif  // AUTOTEST_CMD_ENABLED
