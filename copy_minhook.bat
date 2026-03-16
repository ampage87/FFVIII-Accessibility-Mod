@echo off
REM copy_minhook.bat - Copy MinHook sources from Remaster to Original PC mod
REM Run from: C:\Users\ampag\OneDrive\Documents\FFVIII-Accessibility-Mod\

set SRC=FFVIII-Remastered-Accessibility-Mod\ff8_demaster\minhook
set DST=FF8_OriginalPC_mod\src\minhook

echo Copying MinHook library files...

REM Create directory structure
mkdir "%DST%\include" 2>nul
mkdir "%DST%\src\hde" 2>nul

REM Copy include
copy "%SRC%\include\MinHook.h" "%DST%\include\MinHook.h"

REM Copy src
copy "%SRC%\src\buffer.h" "%DST%\src\buffer.h"
copy "%SRC%\src\buffer.c" "%DST%\src\buffer.c"
copy "%SRC%\src\hook.c" "%DST%\src\hook.c"
copy "%SRC%\src\trampoline.h" "%DST%\src\trampoline.h"
copy "%SRC%\src\trampoline.c" "%DST%\src\trampoline.c"

REM Copy HDE (Hacker Disassembler Engine)
copy "%SRC%\src\hde\hde32.h" "%DST%\src\hde\hde32.h"
copy "%SRC%\src\hde\hde32.c" "%DST%\src\hde\hde32.c"
copy "%SRC%\src\hde\hde64.h" "%DST%\src\hde\hde64.h"
copy "%SRC%\src\hde\hde64.c" "%DST%\src\hde\hde64.c"
copy "%SRC%\src\hde\pstdint.h" "%DST%\src\hde\pstdint.h"
copy "%SRC%\src\hde\table32.h" "%DST%\src\hde\table32.h"
copy "%SRC%\src\hde\table64.h" "%DST%\src\hde\table64.h"

REM Copy license
copy "%SRC%\LICENSE.txt" "%DST%\LICENSE.txt"

echo Done. %DST% should now contain all MinHook sources.
pause
