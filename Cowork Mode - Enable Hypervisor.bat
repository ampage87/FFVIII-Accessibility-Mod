@echo off
net session >nul 2>&1
if %errorlevel% neq 0 (
    echo This script must be run as Administrator.
    echo Right-click and select "Run as administrator".
    pause
    exit /b 1
)

echo ============================================================
echo   Cowork Mode - Enable Hypervisor + VBS + Memory Integrity
echo ============================================================
echo.
echo This re-enables the Windows Hypervisor, Virtualization Based
echo Security, and Memory Integrity so Cowork and other Hyper-V
echo features work. Games may run slower until you switch back
echo to Gaming Mode.
echo.
echo A reboot is required for this to take effect.
echo.

echo [1/3] Enabling hypervisor launch...
bcdedit /set hypervisorlaunchtype auto

echo [2/3] Enabling Virtualization Based Security (VBS)...
reg add "HKLM\SYSTEM\CurrentControlSet\Control\DeviceGuard" /v "EnableVirtualizationBasedSecurity" /t REG_DWORD /d 1 /f
reg add "HKLM\SYSTEM\CurrentControlSet\Control\DeviceGuard\Scenarios\HypervisorEnforcedCodeIntegrity" /v "Enabled" /t REG_DWORD /d 1 /f

echo [3/3] Enabling Credential Guard...
reg add "HKLM\SYSTEM\CurrentControlSet\Control\Lsa" /v "LsaCfgFlags" /t REG_DWORD /d 1 /f

echo.
echo All virtualization security features will be ON after reboot.
echo.
choice /m "Reboot now?"
if %errorlevel% equ 1 (
    shutdown /r /t 5 /c "Rebooting to enable hypervisor and VBS for Cowork."
) else (
    echo Remember to reboot before using Cowork.
)
pause
