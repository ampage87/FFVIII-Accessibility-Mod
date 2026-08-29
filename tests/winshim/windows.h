// tests/winshim/windows.h -- just enough of the Win32 surface for a HOST SYNTAX
// CHECK of the Win32-only translation units. Never linked, never run: the point
// is that a change to field_archive.cpp (which cannot be compiled on the build
// host otherwise) is still parsed and type-checked by the gate.
#pragma once
#include <cstdint>
#include <cstddef>
#include <cstdio>
#include <cstring>
typedef unsigned long  DWORD;
typedef int            BOOL;
typedef unsigned char  BYTE;
typedef unsigned short WORD;
typedef void*          HANDLE;
typedef void*          HMODULE;
typedef void*          HWND;
typedef const char*    LPCSTR;
typedef char*          LPSTR;
typedef long           LONG;
typedef unsigned int   UINT;
typedef intptr_t       LPARAM;
typedef uintptr_t      WPARAM;
#define WINAPI
// v0.62.3.1: enough of the Win32 pointer typedefs for MinHook's header, so the
// gate can syntax-check field_navigation.cpp -- the translation unit that
// consumes field_catalog.inl. It could not before, which is exactly how a
// v0.62.3 identifier that resolves in the scanner's TU and in the harness but
// not in the mod's reached Aaron's compiler with every local gate green.
typedef void            VOID;
typedef void*           LPVOID;
typedef const wchar_t*  LPCWSTR;
typedef wchar_t*        LPWSTR;
typedef wchar_t         WCHAR;
#define __cdecl
// v0.63.3.1: THE SEGMENTED-MEMORY GHOSTS. windows.h still defines far, near,
// pascal and huge as empty macros, thirty years after the memory model that
// needed them. A local called `far` therefore reaches MSVC as `const int = ...`
// -- "error C2513: no variable declared before '='" -- and compiles perfectly
// on this side of the wire, which is exactly how v0.63.3's boost nudge got as
// far as Aaron's compiler with every local gate green. Defined here so it
// cannot happen again silently.
#define far
#define near
#define pascal
#define huge
#define TRUE  1
#define FALSE 0
#define MAX_PATH 260
#define INVALID_HANDLE_VALUE ((HANDLE)(intptr_t)-1)
#define EXCEPTION_EXECUTE_HANDLER 1
// libstdc++ already defines __try (bits/exception_defines.h) but NOT __except,
// so these have to be guarded separately -- a single #ifndef __try around both
// silently leaves __except undefined the moment <string> or <vector> is included.
#ifndef __try
#define __try    try
#endif
#ifndef __except
#define __except(x) catch (...)
#endif
// v0.63.1: a probe that RUNS a Win32 translation unit (rather than only
// syntax-checking it) needs a clock and a keyboard it can drive. Define
// WINSHIM_HOST_CLOCK / WINSHIM_HOST_INPUT before including and supply your own;
// leave them alone and the inert stubs below keep the syntax gate working.
#ifndef WINSHIM_HOST_CLOCK
inline DWORD  GetTickCount() { return 0; }
#endif
#define VK_SHIFT   0x10
#define VK_CONTROL 0x11
#define VK_MENU    0x12
#define VK_RETURN  0x0D
#define VK_F9      0x78
#define VK_OEM_2   0xBF
#ifndef WINSHIM_HOST_INPUT
inline short  GetAsyncKeyState(int) { return 0; }
#endif
inline DWORD  GetLastError() { return 0; }
inline void   Sleep(DWORD) {}
inline HMODULE GetModuleHandleA(LPCSTR) { return nullptr; }
inline int    _stricmp(const char* a, const char* b) { return strcasecmp(a, b); }
inline int    _strnicmp(const char* a, const char* b, size_t n) { return strncasecmp(a, b, n); }
#include <cmath>
inline DWORD GetModuleFileNameA(HMODULE, LPSTR buf, DWORD n) { if (n) buf[0] = '\0'; return 0; }

// v0.65.0: the GDI surface src/field_overlay.cpp rasterises its text with, so
// that file can be parsed on the host too. Inert -- see tests/winshim/gl/GL.h.
typedef void* HDC;
typedef void* HFONT;
typedef void* HBITMAP;
typedef void* HBRUSH;
typedef void* HGDIOBJ;
typedef unsigned int COLORREF;
struct RECT { LONG left, top, right, bottom; };
struct TEXTMETRICA { LONG tmHeight, tmAscent, tmDescent, tmInternalLeading,
                     tmExternalLeading, tmAveCharWidth, tmMaxCharWidth,
                     tmWeight, tmOverhang, tmDigitizedAspectX, tmDigitizedAspectY; };
struct BITMAPINFOHEADER { DWORD biSize; LONG biWidth, biHeight; WORD biPlanes, biBitCount;
                          DWORD biCompression, biSizeImage; LONG biXPelsPerMeter, biYPelsPerMeter;
                          DWORD biClrUsed, biClrImportant; };
struct RGBQUAD { BYTE rgbBlue, rgbGreen, rgbRed, rgbReserved; };
struct BITMAPINFO { BITMAPINFOHEADER bmiHeader; RGBQUAD bmiColors[1]; };
#define BI_RGB            0
#define DIB_RGB_COLORS    0
#define TRANSPARENT       1
#define FW_SEMIBOLD       600
#define DEFAULT_CHARSET   1
#define OUT_DEFAULT_PRECIS 0
#define CLIP_DEFAULT_PRECIS 0
#define ANTIALIASED_QUALITY 4
#define FIXED_PITCH       1
#define FF_MODERN         48
#define RGB(r,g,b) ((COLORREF)(((BYTE)(r))|(((BYTE)(g))<<8)|(((BYTE)(b))<<16)))
inline HDC     GetDC(HWND) { return nullptr; }
inline int     ReleaseDC(HWND, HDC) { return 1; }
inline HDC     CreateCompatibleDC(HDC) { return nullptr; }
inline BOOL    DeleteDC(HDC) { return TRUE; }
inline HGDIOBJ SelectObject(HDC, HGDIOBJ) { return nullptr; }
inline BOOL    DeleteObject(HGDIOBJ) { return TRUE; }
inline HFONT   CreateFontA(int,int,int,int,int,DWORD,DWORD,DWORD,DWORD,DWORD,DWORD,DWORD,DWORD,LPCSTR) { return nullptr; }
inline BOOL    GetTextMetricsA(HDC, TEXTMETRICA*) { return TRUE; }
inline HBITMAP CreateDIBSection(HDC, const BITMAPINFO*, UINT, void**, HANDLE, DWORD) { return nullptr; }
inline HBRUSH  CreateSolidBrush(COLORREF) { return nullptr; }
inline int     FillRect(HDC, const RECT*, HBRUSH) { return 1; }
inline int     FrameRect(HDC, const RECT*, HBRUSH) { return 1; }
inline int     SetBkMode(HDC, int) { return 1; }
inline COLORREF SetTextColor(HDC, COLORREF) { return 0; }
inline BOOL    TextOutA(HDC, int, int, LPCSTR, int) { return TRUE; }
inline BOOL GdiFlush(void) { return TRUE; }
