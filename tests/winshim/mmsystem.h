// mmsystem.h -- host shim (v0.123.0, #centra)
//
// The winshim exists so Win32 translation units can be syntax-checked on the
// build host. field_navigation.cpp and world_map.cpp were the two it could not
// reach, both for the same reason: they call PlaySound and there was no
// mmsystem.h. DEVNOTES has carried "src/world_map.cpp CANNOT be
// host-syntax-checked" as a standing limitation ever since.
//
// v0.123.0 needed a syntax check on new hook code in field_navigation.cpp's
// include chain, so the gap is closed rather than worked around. Only the
// symbols this codebase actually uses are declared -- a shim that pretends to
// be the whole multimedia API would be a second, worse copy of it.
#pragma once
#include <windows.h>

#define SND_SYNC        0x0000
#define SND_ASYNC       0x0001
#define SND_NODEFAULT   0x0002
#define SND_MEMORY      0x0004
#define SND_LOOP        0x0008
#define SND_PURGE       0x0040
#define SND_FILENAME    0x00020000

extern "C" BOOL WINAPI PlaySoundA(LPCSTR pszSound, HMODULE hmod, DWORD fdwSound);
extern "C" BOOL WINAPI PlaySoundW(LPCWSTR pszSound, HMODULE hmod, DWORD fdwSound);
#ifdef UNICODE
#define PlaySound PlaySoundW
#else
#define PlaySound PlaySoundA
#endif

extern "C" DWORD WINAPI timeGetTime(void);
