// chase_wndproc.h -- WndProc subclass that drops arrow-key WM_KEY* messages
// during chase Auto. Closes the last untreated keyboard delivery path to FF8's
// own WndProc -- the [+0xb48] per-message handler dispatch invoked from
// FF8_EN.exe's window procedure at 0x0040AC5B.
//
// WHY: across v0.15.9.11.3.1 through v0.15.9.11.3.5, the chase still got caught
// on doopen2a whenever Aaron pressed arrow keys, even though the DirectInput
// keyboard buffer (chase_keyboard.cpp's GetDeviceState detour), the
// GetAsyncKeyState path, and the SendInput collision (v0.15.9.11.3.4) were all
// confirmed clean by their respective BATs. The v0.15.9.11.3.5 disassembly walk
// through 0x0040AC5B-0x0040AE14 (the WndProc + Ctrl+Q hotkey region) showed
// every arrow WM_KEYDOWN/WM_KEYUP message routes through [+0xb48] in FF8's
// WndProc dispatch \u2014 a mechanism the synthetic-buffer hooks don't touch.
// Either FF8's WndProc writes a global the chase-progress director script polls
// for "is the player struggling against the chase", OR the volume of WM_KEY*
// dispatch on Aaron's keypresses shifts the main thread's frame timing relative
// to the chase director's catch evaluator. Either way, the messages have to
// stop reaching FF8's WndProc.
//
// HOW: a WndProc subclass installed via SetWindowLongPtrW. Our subclass
// receives every message Windows would otherwise deliver to FF8's WndProc.
// When ChaseKeyboard::IsActive() AND msg is an arrow-key WM_KEY*, we return 0
// (the documented "I handled this" reply for WM_KEYDOWN/KEYUP) without
// forwarding -- FF8's WndProc and its [+0xb48] dispatch never see the message.
// Everything else (mouse, paint, timer, non-arrow keys, all messages when
// chase is inactive) forwards via CallWindowProcW/A so FF8's behavior is
// unchanged outside the narrow arrow-key/chase-Auto window.
//
// WHY THIS IS NOT WH_KEYBOARD_LL: the v0.15.9.11.1/.11.2 attempts to install
// a low-level keyboard hook (WH_KEYBOARD_LL) caused chase auto-pilot timing
// to fall apart on doopen2a (caught at wp 5-6/26, ~2s after engagement).
// WH_KEYBOARD_LL routes through SetWindowsHookEx and forces every SendInput
// call to synchronously round-trip through the main thread's message pump.
// WndProc subclassing runs synchronously inside the same thread that pumps
// the message -- no cross-thread round trip, no SendInput latency.
//
// PERMANENT INSTALL: EnsureInstalled is idempotent and never uninstalls
// during gameplay. Reasoning: SetWindowLongPtrW is cross-thread safe for the
// install itself, but a mid-gameplay uninstall would race against in-flight
// DispatchMessage calls on the main thread. The subclass is cheap outside
// chase Auto (one ChaseKeyboard::IsActive() bool read per message, false
// short-circuit), so leaving it installed permanently costs nothing and
// avoids the race entirely. Shutdown exists for completeness but isn't wired
// into normal process teardown.

#pragma once

#include <windows.h>

namespace ChaseWndProc {

// Idempotent. Enumerates top-level visible windows owned by the current
// process and installs the subclass on any not already subclassed. Safe to
// call from any thread (uses SetWindowLongPtrW which is documented as
// cross-thread safe). Safe to call before FF8 has created its window
// (logs a warning and returns; a subsequent call once the window exists
// will succeed).
//
// Called lazily from ChaseKeyboard::Activate() so we don't enumerate
// windows at startup before FF8/FFNx has created them. The first chase
// activation happens many seconds after launch (player has to walk into
// the Dollet chase scene), so the window definitely exists by then.
void EnsureInstalled();

// Restores original WndProcs. Not called during normal teardown -- the OS
// reclaims everything at process exit and there's no point fighting an
// in-flight DispatchMessage on the way out. Provided for completeness and
// future-symmetry with ChaseKeyboard::Shutdown.
void Shutdown();

}  // namespace ChaseWndProc
