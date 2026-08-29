@echo off
setlocal enabledelayedexpansion

REM ============================================================
REM FF8 Original PC Accessibility Mod - Build and Deploy
REM Lives in src\ — invoked by deploy.vbs in project root.
REM ============================================================

:: Extract version from ff8_accessibility.h
:: v0.15.10.1 fix: add /B (begin-of-line anchor) so findstr matches only the
:: actual #define line, not historical comment entries that mention the macro
:: by name. v0.15.3 tightened the literal from "FF8OPC_VERSION " to
:: "#define FF8OPC_VERSION " (with trailing space) but did not anchor to BOL.
:: As version history accumulated in line 12's trailing comment, the v0.15.3
:: entry's own commentary embedded the literal string "#define FF8OPC_VERSION"
:: while describing what its fix matches -- ironically re-breaking the
:: regex. findstr matched that comment line too, and for /f's
:: last-iteration-wins behavior left VERSION set to token 3 of the comment
:: line ("SINGLE-PRONGED" from "// v0.15.3: SINGLE-PRONGED CLEANUP..."),
:: producing "Version: SINGLE-PRONGED" in deploy logs from v0.15.3 onward.
:: Adding /B fixes it: the real #define starts at column 0; all historical
:: mentions are indented "  // ..." so they no longer false-positive.
:: The %%~V modifier strips surrounding quotes from "X.Y.Z".
set "VERSION=unknown"
for /f "tokens=3 delims= " %%V in ('findstr /B /C:"#define FF8OPC_VERSION " "%~dp0ff8_accessibility.h"') do (
    set "VERSION=%%~V"
)

echo Building FF8 Original PC Accessibility Mod Version %VERSION%
echo Build started at %date% %time%
echo.

:: Set paths — deploy.bat now lives in src\
:: Normalize %~dp0 to strip trailing backslash (avoids \" quoting issues with cl.exe)
for %%I in ("%~dp0.") do set "SRC_DIR=%%~fI"
set "PROJECT_DIR=%SRC_DIR%\.."
set "BUILD_DIR=%PROJECT_DIR%\build"
set "MINHOOK_DIR=%SRC_DIR%\minhook"
set "GAME_DIR=C:\Program Files (x86)\Steam\steamapps\common\FINAL FANTASY VIII"

echo Project Directory: "%PROJECT_DIR%"
echo Source Directory: "%SRC_DIR%"
echo Game Directory: "%GAME_DIR%"
echo.

:: Check if source exists
if not exist "%SRC_DIR%\dinput8.cpp" (
    echo ERROR: Source file not found at: "%SRC_DIR%\dinput8.cpp"
    goto :error
)

:: Check if game directory exists
if not exist "%GAME_DIR%" (
    echo ERROR: Game directory not found at: "%GAME_DIR%"
    echo Update GAME_DIR in deploy.bat to your actual FF8 install path.
    goto :error
)

:: Ensure Logs directory exists
if not exist "%PROJECT_DIR%\Logs" mkdir "%PROJECT_DIR%\Logs"

:: Grab previous logs before they get overwritten by new game launch
:: (The new multi-channel log system archives old logs automatically,
::  but we also copy the game-dir logs to the project Logs folder.)
for %%F in ("%GAME_DIR%\ff8_mod.log" "%GAME_DIR%\ff8_battle.log" "%GAME_DIR%\ff8_field.log" "%GAME_DIR%\ff8_world.log" "%GAME_DIR%\ff8_menu.log" "%GAME_DIR%\ff8_dialog.log") do (
    if exist %%F (
        copy /Y %%F "%PROJECT_DIR%\Logs\" >nul 2>nul
    )
)
echo Previous game logs copied to Logs directory.
echo.

:: Clean previous build artifacts to avoid stale .obj files
if exist "%BUILD_DIR%" (
    echo Cleaning previous build artifacts...
    del /q "%BUILD_DIR%\*.obj" 2>nul
    del /q "%BUILD_DIR%\*.dll" 2>nul
    del /q "%BUILD_DIR%\*.lib" 2>nul
    del /q "%BUILD_DIR%\*.exp" 2>nul
    del /q "%BUILD_DIR%\*.res" 2>nul
    echo.
)
if not exist "%BUILD_DIR%" mkdir "%BUILD_DIR%"

:: ============================================================
:: Find Visual Studio environment
:: ============================================================
echo Locating Visual Studio x86 build tools...

set "VCVARS_FOUND="

REM Check if we're already in a VS Developer Command Prompt
where /q cl.exe
if %errorlevel%==0 (
    set "VCVARS_FOUND=1"
    echo Already in a Visual Studio environment.
    goto :build
)

REM Search for vcvars32.bat across VS editions
for %%E in (BuildTools Community Professional Enterprise) do (
    for %%Y in (2022 2019) do (
        for %%P in ("%ProgramFiles%" "%ProgramFiles(x86)%") do (
            set "VCVARS_PATH=%%~P\Microsoft Visual Studio\%%Y\%%E\VC\Auxiliary\Build\vcvars32.bat"
            if exist "!VCVARS_PATH!" (
                echo Found: !VCVARS_PATH!
                call "!VCVARS_PATH!" >nul 2>&1
                set "VCVARS_FOUND=1"
                goto :build
            )
        )
    )
)

if not defined VCVARS_FOUND (
    echo ERROR: Could not find Visual Studio x86 build tools.
    echo Please install Visual Studio 2019 or 2022 with the C++ Desktop workload.
    goto :error
)

:build
echo.

:: ============================================================
:: Build dinput8.dll
:: ============================================================
echo Building dinput8.dll (x86)...
echo.

cd /d "%PROJECT_DIR%"

REM ---- Step 1: Compile resources (VTT files embedded in DLL) ----
echo Compiling resources...
rc /nologo /fo "%BUILD_DIR%\resources.res" "%SRC_DIR%\resources.rc"
if errorlevel 1 (
    echo RESOURCE COMPILATION FAILED
    goto :error
)
echo   resources.res OK
echo.

REM ---- Step 2: Compile and link ----
echo Build output saved to: "%PROJECT_DIR%\Logs\build_latest.log"
cl /nologo /W3 /EHsc /O2 /MD /LD ^
    /I"%SRC_DIR%" ^
    /I"%MINHOOK_DIR%\include" ^
    /Fo"%BUILD_DIR%\\" ^
    /Fe"%BUILD_DIR%\dinput8.dll" ^
    "%SRC_DIR%\dinput8.cpp" ^
    "%SRC_DIR%\ff8_addresses.cpp" ^
    "%SRC_DIR%\screen_reader.cpp" ^
    "%SRC_DIR%\log.cpp" ^
    "%SRC_DIR%\config.cpp" ^
    "%SRC_DIR%\title_screen.cpp" ^
    "%SRC_DIR%\fmv_audio_desc.cpp" ^
    "%SRC_DIR%\fmv_skip.cpp" ^
    "%SRC_DIR%\gf_audio_desc.cpp" ^
    "%SRC_DIR%\scan_tts.cpp" ^
    "%SRC_DIR%\field_dialog.cpp" ^
    "%SRC_DIR%\field_archive.cpp" ^
    "%SRC_DIR%\field_navigation.cpp" ^
    "%SRC_DIR%\name_bypass.cpp" ^
    "%SRC_DIR%\nav_log.cpp" ^
    "%SRC_DIR%\game_audio.cpp" ^
    "%SRC_DIR%\audio_ducker.cpp" ^
    "%SRC_DIR%\menu_tts.cpp" ^
    "%SRC_DIR%\battle_tts.cpp" ^
    "%SRC_DIR%\world_map.cpp" ^
    "%SRC_DIR%\chase_diag.cpp" ^
    "%SRC_DIR%\chase_detector.cpp" ^
    "%SRC_DIR%\chase_ask_overlay.cpp" ^
    "%SRC_DIR%\train_mode_ask_overlay.cpp" ^
    "%SRC_DIR%\bahamut_light_overlay.cpp" ^
    "%SRC_DIR%\centra_code_overlay.cpp" ^
    "%SRC_DIR%\chase_auto_pilot.cpp" ^
    "%SRC_DIR%\chase_keyboard.cpp" ^
    "%SRC_DIR%\chase_wndproc.cpp" ^
    "%SRC_DIR%\chase_kani_freeze.cpp" ^
    "%SRC_DIR%\chase_battle_freeze.cpp" ^
    "%SRC_DIR%\countdown_timer.cpp" ^
    "%SRC_DIR%\dialog_inject.cpp" ^
    "%SRC_DIR%\field_announce.cpp" ^
    "%SRC_DIR%\field_overlay.cpp" ^
    "%SRC_DIR%\ff8_text_decode.cpp" ^
    "%MINHOOK_DIR%\src\buffer.c" ^
    "%MINHOOK_DIR%\src\hook.c" ^
    "%MINHOOK_DIR%\src\trampoline.c" ^
    "%MINHOOK_DIR%\src\hde\hde32.c" ^
    "%MINHOOK_DIR%\src\hde\hde64.c" ^
    /link /DEF:"%SRC_DIR%\dinput8.def" ^
    "%BUILD_DIR%\resources.res" ^
    user32.lib ^
    ole32.lib ^
    uuid.lib ^
    psapi.lib ^
    gdi32.lib ^
    gdiplus.lib ^
    dxguid.lib ^
    opengl32.lib ^
    winmm.lib > "%PROJECT_DIR%\Logs\build_latest.log" 2>&1

if errorlevel 1 (
    echo.
    echo ERROR: Build failed! See build log:
    type "%PROJECT_DIR%\Logs\build_latest.log"
    goto :error
)

echo.
echo Build successful!
echo.

:: ============================================================
:: Deploy to game directory
:: ============================================================
echo Deploying files to game directory...

echo Copying dinput8.dll...
copy /Y "%BUILD_DIR%\dinput8.dll" "%GAME_DIR%\dinput8.dll"
if errorlevel 1 (
    echo ERROR: Failed to copy dinput8.dll. Is the game running? Try running as Administrator.
    goto :error
)

echo.
echo ============================================================
echo Deployment Complete!
echo Version: %VERSION%
echo Files deployed to: "%GAME_DIR%"
echo Build ended at %date% %time%
echo ============================================================
echo.
echo To test: Launch FF8 through Steam.
echo Logs will appear in BOTH:
echo   - "%GAME_DIR%\ff8_*.log" (6 channel files)
echo   - "%PROJECT_DIR%\Logs\ff8_*.log"
goto :end

:error
echo.
echo Build ended at %date% %time%
exit /b 1

:end
exit /b 0
