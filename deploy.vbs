' ============================================================
' FF8 Original PC Accessibility Mod - Deploy Launcher
' Double-click this file to build and deploy. A progress
' dialog will keep you informed, and no console window
' will appear.
' ============================================================
Set fso = CreateObject("Scripting.FileSystemObject")
Set WshShell = CreateObject("WScript.Shell")

scriptDir = fso.GetParentFolderName(WScript.ScriptFullName)
ps1Path = fso.BuildPath(scriptDir, "deploy.ps1")

WshShell.CurrentDirectory = scriptDir
WshShell.Run "powershell -NoProfile -ExecutionPolicy Bypass -WindowStyle Hidden -File """ & ps1Path & """", 0, True
