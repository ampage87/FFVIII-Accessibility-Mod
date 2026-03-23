@echo off
net session >nul 2>&1
if %errorlevel% neq 0 (
    echo This script must be run as Administrator.
    echo Right-click and select "Run as administrator".
    pause
    exit /b 1
)

echo ============================================================
echo   Gaming Mode - Disable Hypervisor + VBS + Memory Integrity
echo ============================================================
echo.
echo This disables the Windows Hypervisor, Virtualization Based
echo Security, and Memory Integrity so games run at full speed.
echo Cowork will not work until you re-enable with the other script.
echo.
echo A reboot is required for this to take effect.
echo.

echo [1/3] Disabling hypervisor launch...
bcdedit /set hypervisorlaunchtype off

echo [2/3] Disabling Virtualization Based Security (VBS)...
reg add "HKLM\SYSTEM\CurrentControlSet\Control\DeviceGuard" /v "EnableVirtualizationBasedSecurity" /t REG_DWORD /d 0 /f
reg add "HKLM\SYSTEM\CurrentControlSet\Control\DeviceGuard\Scenarios\HypervisorEnforcedCodeIntegrity" /v "Enabled" /t REG_DWORD /d 0 /f

echo [3/3] Disabling Credential Guard...
reg add "HKLM\SYSTEM\CurrentControlSet\Control\Lsa" /v "LsaCfgFlags" /t REG_DWORD /d 0 /f

echo.
echo All virtualization security features will be OFF after reboot.
echo.
echo To verify after reboot, open Start and search "msinfo32".
echo Look for "Virtualization-based security" - it should say
echo "Not enabled".
echo.
choice /m "Reboot now?"
if %errorlevel% equ 1 (
    shutdown /r /t 5 /c "Rebooting to disable hypervisor and VBS for gaming."
) else (
    echo Remember to reboot before launching the game.
)
pause
