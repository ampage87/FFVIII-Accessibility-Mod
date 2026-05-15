// chase_wndproc.cpp -- WndProc subclass that drops arrow-key WM_KEY* messages
// during chase Auto. See chase_wndproc.h for the design rationale and the
// v0.15.9.11.3.1-.5 BAT chain that motivates this fourth coordinated hook.

#include "chase_wndproc.h"
#include "chase_keyboard.h"
#include "ff8_accessibility.h"
#include "mod_forward_decls.h"

#include <mutex>
#include <vector>

namespace ChaseWndProc {

namespace {

// ============================================================================
// State
// ============================================================================

// One entry per window we've subclassed. We support multiple windows because
// FFNx may create overlay/debug windows in addition to FF8's main game window;
// arrow keys go to whichever has keyboard focus, so subclassing them all is
// safer than guessing.
struct Subclassed {
    HWND hwnd;
    WNDPROC origProc;
    bool isUnicode;
};

// Append-only during install; iterated under lock both for install (write)
// and for dispatch lookup (read). x86 alignment plus the std::vector
// stability of pre-existing entries (push_back may reallocate, but
// subsequent reads under the same lock see the new pointer) means the lock
// is the only synchronization we need.
static std::vector<Subclassed> g_subclassed;
static std::mutex g_mutex;

// ============================================================================
// Subclass procedure
// ============================================================================

// Linear lookup. We expect 1-2 entries; vector::find isn't worth it.
// Caller must hold g_mutex.
static const Subclassed* FindSubclassed(HWND hwnd)
{
    for (const auto& s : g_subclassed) {
        if (s.hwnd == hwnd) return &s;
    }
    return nullptr;
}

// The subclass. Runs on whichever thread pumped the message -- for FF8's
// main game window, that's FF8's main thread (the same thread that runs the
// chase script's catch evaluator, the same thread chase_keyboard.cpp's
// GetDeviceState detour fires on). So when we return 0 here without
// forwarding, the WM_KEY* message and its [+0xb48] dispatch never run -- no
// global is written, no main-thread cycles are spent on it, no message-pump
// volume effect.
static LRESULT CALLBACK SubclassProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    // Look up the original WndProc for this hwnd. We take the lock for the
    // duration of the lookup but release it before forwarding -- forwarding
    // can in principle re-enter our subclass via nested SendMessage and we
    // don't want a re-entrancy deadlock on the same thread. (std::mutex is
    // non-recursive.)
    WNDPROC orig = nullptr;
    bool isUnicode = true;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        const Subclassed* s = FindSubclassed(hwnd);
        if (s != nullptr) {
            orig = s->origProc;
            isUnicode = s->isUnicode;
        }
    }

    // Drop arrow-key WM_KEY* when chase Auto is driving. Outside chase Auto,
    // and for all non-arrow keys at all times, fall through.
    //
    // ChaseKeyboard::IsActive() reads its s_active volatile bool -- atomic on
    // x86, no lock needed. Worst-case race (Activate fires between our read
    // and the message dispatch): one message gets through that "should have"
    // been blocked, or one gets blocked just after Deactivate -- both
    // self-correcting on the next message, both harmless.
    if (ChaseKeyboard::IsActive()) {
        if (msg == WM_KEYDOWN || msg == WM_KEYUP ||
            msg == WM_SYSKEYDOWN || msg == WM_SYSKEYUP) {
            if (wParam == VK_UP || wParam == VK_DOWN ||
                wParam == VK_LEFT || wParam == VK_RIGHT) {
                // Consume. Return 0 per MSDN's documented "handled" reply for
                // WM_KEYDOWN/KEYUP. FF8's WndProc and its [+0xb48] dispatch
                // table at 0x0040AC5B never see the message.
                return 0;
            }
        }
    }

    // Pass through. If we somehow lost our entry (shouldn't happen during
    // normal operation), fall back to DefWindowProc rather than dropping
    // the message; better to risk a slightly-wrong handler than to drop
    // paint/timer/destroy messages FF8 needs.
    if (orig != nullptr) {
        return isUnicode
            ? CallWindowProcW(orig, hwnd, msg, wParam, lParam)
            : CallWindowProcA(orig, hwnd, msg, wParam, lParam);
    }
    return isUnicode
        ? DefWindowProcW(hwnd, msg, wParam, lParam)
        : DefWindowProcA(hwnd, msg, wParam, lParam);
}

// ============================================================================
// Window discovery
// ============================================================================

struct EnumCtx {
    DWORD pid;
    std::vector<HWND> hwnds;
};

static BOOL CALLBACK EnumProc(HWND hwnd, LPARAM lp)
{
    auto* ctx = reinterpret_cast<EnumCtx*>(lp);

    DWORD wndPid = 0;
    GetWindowThreadProcessId(hwnd, &wndPid);
    if (wndPid != ctx->pid) return TRUE;  // not our process
    if (!IsWindowVisible(hwnd)) return TRUE;  // hidden helper windows

    LONG_PTR style = GetWindowLongPtrW(hwnd, GWL_STYLE);
    if ((style & WS_CHILD) != 0) return TRUE;  // child controls

    // Top-level, visible, owned by us. Add it.
    ctx->hwnds.push_back(hwnd);
    return TRUE;
}

}  // namespace

// ============================================================================
// Public API
// ============================================================================

void EnsureInstalled()
{
    std::lock_guard<std::mutex> lock(g_mutex);

    EnumCtx ctx{ GetCurrentProcessId(), {} };
    EnumWindows(&EnumProc, reinterpret_cast<LPARAM>(&ctx));

    if (ctx.hwnds.empty()) {
        Log::Field("ChaseWndProc: EnumWindows found no top-level visible "
                   "windows owned by FF8_EN.exe. Subclass NOT installed this "
                   "attempt; will retry on next chase Auto activate. (This "
                   "should not happen during gameplay -- if it does, FF8's "
                   "main window has not been created yet or has been "
                   "destroyed.)");
        return;
    }

    int newlyInstalled = 0;
    for (HWND hwnd : ctx.hwnds) {
        if (FindSubclassed(hwnd) != nullptr) continue;  // already subclassed

        // Match the window's character set so CallWindowProc{W,A} translates
        // string-bearing messages correctly. For our hot path (WM_KEY*) this
        // doesn't matter, but it matters for any other message that might
        // pass through (WM_SETTEXT, WM_CHAR, etc.).
        bool isUnicode = (IsWindowUnicode(hwnd) != FALSE);

        WNDPROC orig = nullptr;
        SetLastError(0);
        if (isUnicode) {
            orig = reinterpret_cast<WNDPROC>(SetWindowLongPtrW(
                hwnd, GWLP_WNDPROC,
                reinterpret_cast<LONG_PTR>(&SubclassProc)));
        } else {
            orig = reinterpret_cast<WNDPROC>(SetWindowLongPtrA(
                hwnd, GWLP_WNDPROC,
                reinterpret_cast<LONG_PTR>(&SubclassProc)));
        }

        if (orig == nullptr) {
            DWORD err = GetLastError();
            // SetWindowLongPtr returning 0 with err == 0 means the old value
            // was genuinely 0, which on a valid HWND would mean we'd already
            // hooked it and stored nullptr -- shouldn't happen given the
            // FindSubclassed check above, but log it and move on either way.
            Log::Field("ChaseWndProc: SetWindowLongPtr%s(GWLP_WNDPROC) "
                       "returned NULL on HWND 0x%08X (GetLastError=%lu). "
                       "This window will NOT have arrow-key WM_KEY messages "
                       "suppressed during chase Auto.",
                       isUnicode ? "W" : "A",
                       (unsigned)(uintptr_t)hwnd, (unsigned long)err);
            continue;
        }

        g_subclassed.push_back({hwnd, orig, isUnicode});
        ++newlyInstalled;
        Log::Field("ChaseWndProc: WndProc subclass INSTALLED on HWND 0x%08X "
                   "(unicode=%d, origProc=0x%08X). Arrow-key WM_KEYDOWN/KEYUP "
                   "(plus WM_SYSKEYDOWN/UP) will be dropped from this window "
                   "while chase_keyboard is Active. Permanent install: the "
                   "subclass stays in place for the rest of the process "
                   "lifetime; outside chase Auto it is a cheap pass-through.",
                   (unsigned)(uintptr_t)hwnd, (int)isUnicode,
                   (unsigned)(uintptr_t)orig);
    }

    if (newlyInstalled == 0) {
        // Quietly succeed -- this is the steady state on every subsequent
        // chase Auto activation after the first.
    }
}

void Shutdown()
{
    std::lock_guard<std::mutex> lock(g_mutex);

    // Restore original WndProcs in reverse install order. We don't clear the
    // vector -- any in-flight SubclassProc call on the main thread can still
    // find its entry and forward correctly. Once SetWindowLongPtr returns,
    // new messages route directly to the original WndProc; we just need to
    // keep the vector entries alive long enough for any in-progress dispatch
    // to complete. Since this is only called at process exit and the OS
    // tears everything down, leaving the vector populated is fine.
    for (auto it = g_subclassed.rbegin(); it != g_subclassed.rend(); ++it) {
        SetLastError(0);
        if (it->isUnicode) {
            SetWindowLongPtrW(it->hwnd, GWLP_WNDPROC,
                              reinterpret_cast<LONG_PTR>(it->origProc));
        } else {
            SetWindowLongPtrA(it->hwnd, GWLP_WNDPROC,
                              reinterpret_cast<LONG_PTR>(it->origProc));
        }
    }
}

}  // namespace ChaseWndProc
