# ============================================================
# FF8 Original PC Accessibility Mod - Deploy UI
# Shows a progress dialog while running deploy.bat, then
# displays a success or error result dialog.
# Screen reader accessible via AccessibilityNotifyClients.
# ============================================================

Add-Type -AssemblyName System.Windows.Forms
Add-Type -AssemblyName System.Drawing

$scriptDir  = Split-Path -Parent $MyInvocation.MyCommand.Path
$projectDir = Split-Path -Parent $scriptDir
$batPath    = Join-Path $scriptDir "deploy.bat"
$logsDir    = Join-Path $projectDir "Logs"
if (-not (Test-Path $logsDir)) { New-Item -ItemType Directory -Path $logsDir | Out-Null }
$logPath    = Join-Path $logsDir "build_latest.log"

# Extract version from ff8_accessibility.h
$headerPath = Join-Path $scriptDir "ff8_accessibility.h"
$modVersion = "unknown"
if (Test-Path $headerPath) {
    $versionLine = Select-String -Path $headerPath -Pattern '^#define FF8OPC_VERSION "([^"]+)"' | Select-Object -First 1
    if ($versionLine) { $modVersion = $versionLine.Matches[0].Groups[1].Value }
}

# --- Shared state: use a hashtable so event actions get the same
#     object instances via -MessageData ---
$shared = @{
    LogContent    = New-Object System.Text.StringBuilder
    LatestStatus  = "Building FF8 Original PC Accessibility Mod Version $modVersion"
    StatusChanged = $true
}

# --- Build the progress form ---
$form = New-Object System.Windows.Forms.Form
$form.Text            = "Building FF8 OPC Accessibility Mod Version $modVersion"
$form.StartPosition   = "CenterScreen"
$form.FormBorderStyle = "FixedDialog"
$form.MaximizeBox     = $false
$form.MinimizeBox     = $false
$form.Width           = 420
$form.Height          = 150
$form.TopMost         = $true
$form.ControlBox      = $false

# Use a read-only TextBox so screen readers announce content automatically
$statusBox = New-Object System.Windows.Forms.TextBox
$statusBox.Text        = $shared.LatestStatus
$statusBox.ReadOnly    = $true
$statusBox.Multiline   = $true
$statusBox.WordWrap    = $true
$statusBox.BorderStyle = "None"
$statusBox.BackColor   = $form.BackColor
$statusBox.TabStop     = $true
$statusBox.Width       = 380
$statusBox.Height      = 70
$statusBox.Location    = New-Object System.Drawing.Point(14, 16)
$form.Controls.Add($statusBox)

# When the form first opens, activate and focus for screen reader
$form.Add_Shown({
    $form.Activate()
    $statusBox.Focus()
    $statusBox.SelectionStart  = 0
    $statusBox.SelectionLength = 0
})

# --- Start the build process asynchronously ---
$process = New-Object System.Diagnostics.Process
$process.StartInfo.FileName               = "cmd.exe"
$process.StartInfo.Arguments              = "/c `"`"$batPath`"`""
$process.StartInfo.WorkingDirectory        = $projectDir
$process.StartInfo.RedirectStandardOutput  = $true
$process.StartInfo.RedirectStandardError   = $true
$process.StartInfo.UseShellExecute         = $false
$process.StartInfo.CreateNoWindow          = $true
$process.EnableRaisingEvents               = $true

Register-ObjectEvent -InputObject $process -EventName OutputDataReceived -MessageData $shared -Action {
    $line = $Event.SourceEventArgs.Data
    if ($null -ne $line) {
        $state = $Event.MessageData
        [void]$state.LogContent.AppendLine($line)
        $newStatus = $null
        if     ($line -match "Locating Visual Studio")  { $newStatus = "Locating build tools..." }
        elseif ($line -match "Building dinput8")         { $newStatus = "Building dinput8.dll... This may take a moment." }
        elseif ($line -match "Build successful")          { $newStatus = "Build successful! Preparing deployment..." }
        elseif ($line -match "Deploying files")           { $newStatus = "Deploying files to game directory..." }
        elseif ($line -match "Deployment Complete")       { $newStatus = "Deployment complete!" }
        elseif ($line -match "^ERROR:")                   { $newStatus = $line }
        elseif ($line -match "^WARNING:")                 { $newStatus = $line }
        if ($null -ne $newStatus) {
            $state.LatestStatus  = $newStatus
            $state.StatusChanged = $true
        }
    }
} | Out-Null

Register-ObjectEvent -InputObject $process -EventName ErrorDataReceived -MessageData $shared -Action {
    $line = $Event.SourceEventArgs.Data
    if ($null -ne $line) {
        [void]$Event.MessageData.LogContent.AppendLine($line)
    }
} | Out-Null

$process.Start() | Out-Null
$process.BeginOutputReadLine()
$process.BeginErrorReadLine()

# --- Timer keeps the UI alive and updates the status ---
$timer = New-Object System.Windows.Forms.Timer
$timer.Interval = 200

$timer.Add_Tick({
    if ($shared.StatusChanged) {
        $statusBox.Text = $shared.LatestStatus
        $statusBox.SelectionStart  = 0
        $statusBox.SelectionLength = 0

        # Notify screen readers that the value has changed
        $statusBox.AccessibilityNotifyClients(
            [System.Windows.Forms.AccessibleEvents]::NameChange, 0)

        $shared.StatusChanged = $false
    }
    if ($process.HasExited) {
        $timer.Stop()
        $form.Close()
    }
})

$timer.Start()
$form.ShowDialog() | Out-Null

# --- Clean up ---
$timer.Dispose()
$process.WaitForExit()
$exitCode = $process.ExitCode

Get-EventSubscriber | Unregister-Event

# ============================================================
# Step 0 chase-protection guard (DEVNOTES Track A).
# Runs AFTER every build so a chase-route-breaking change is visible the
# instant a deploy finishes -- before the game launches.
#
# NON-BLOCKING: this NEVER changes $exitCode. A guard failure must not block a
# deploy/BAT (mid-edit deploys are expected during nav work). It only appends a
# clearly marked block to the END of build_latest.log so a FAIL is impossible
# to miss on "BAT". The push-time / CI gate (.github/workflows/safety-checks.yml)
# is the BLOCKING authority.
#
# Two layers:
#   (A) C++ harness  - compiles the REAL src/field_nav_pathfinding.inl against
#                      generated real-walkmesh fixtures and runs it. The compile
#                      itself catches incompatible nav-core refactors; the run
#                      hard-gates walkmesh mesh integrity and reports route /
#                      out-of-mesh / robot-distance metrics.
#   (B) portal check - fast pure-Python portal-correctness model (complement).
# ============================================================
function Add-GuardLine([string]$text) { [void]$shared.LogContent.AppendLine($text) }

$pyExe = $null
foreach ($cand in @("python", "py", "python3")) {
    if (Get-Command $cand -ErrorAction SilentlyContinue) { $pyExe = $cand; break }
}

Add-GuardLine ""
Add-GuardLine "============================================================"
Add-GuardLine "CHASEGUARD (C++ harness) - real nav core on real Dollet fixtures"
Add-GuardLine "------------------------------------------------------------"

$harnessSrc  = Join-Path $projectDir "tests\chase_harness.cpp"
$harnessExe  = Join-Path $projectDir "tests\chase_harness.exe"
$genScript   = Join-Path $projectDir "tests\gen_chase_fixture.py"
$walkmeshJson = Join-Path $projectDir "Plan & Research Documents\ff8_walkmeshes.json"
$fixtureHdr  = Join-Path $projectDir "tests\chase_fixtures.h"

$cppRan = $false
if (-not (Test-Path $harnessSrc)) {
    Add-GuardLine "*** NOT RUN *** - tests\chase_harness.cpp missing (non-blocking)"
} elseif ($null -eq $pyExe) {
    Add-GuardLine "*** NOT RUN *** - no python on PATH to generate the fixture (non-blocking)"
} elseif (-not (Test-Path $walkmeshJson)) {
    Add-GuardLine "*** NOT RUN *** - Plan & Research Documents\ff8_walkmeshes.json missing (non-blocking)"
} else {
    # 1) Generate the fixture header from the committed walkmesh extract.
    $genOut = & $pyExe $genScript $walkmeshJson $fixtureHdr 2>&1
    if ($LASTEXITCODE -ne 0 -or -not (Test-Path $fixtureHdr)) {
        Add-GuardLine "*** NOT RUN *** - fixture generation failed (non-blocking):"
        foreach ($gl in $genOut) { Add-GuardLine "  $gl" }
    } else {
        # 2) Compile: prefer g++ if present, else MSVC cl via vcvars64.
        if (Test-Path $harnessExe) { Remove-Item $harnessExe -ErrorAction SilentlyContinue }
        $compileLog = ""
        if (Get-Command "g++" -ErrorAction SilentlyContinue) {
            $compileLog = & g++ -std=c++17 -O0 -o $harnessExe $harnessSrc 2>&1
        } else {
            # Locate Visual Studio's vcvars64.bat via vswhere, then compile in a
            # cmd subprocess that has the MSVC environment loaded.
            $vswhere = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"
            $vcvars  = $null
            if (Test-Path $vswhere) {
                $vsPath = (& $vswhere -latest -property installationPath 2>$null | Select-Object -First 1)
                if ($vsPath) {
                    $cand = Join-Path $vsPath "VC\Auxiliary\Build\vcvars64.bat"
                    if (Test-Path $cand) { $vcvars = $cand }
                }
            }
            if ($null -eq $vcvars) {
                $compileLog = "no g++ and no MSVC vcvars64.bat found"
            } else {
                $clCmd = "call `"$vcvars`" >NUL 2>&1 && cl /nologo /EHsc /std:c++17 /Fe:`"$harnessExe`" /Fo:`"$($projectDir)\tests\`" `"$harnessSrc`""
                $compileLog = & cmd.exe /c $clCmd 2>&1
            }
        }
        # 3) Run the harness if it built.
        if (Test-Path $harnessExe) {
            $runOut  = & $harnessExe 2>&1
            $runExit = $LASTEXITCODE
            foreach ($rl in $runOut) { Add-GuardLine "  $rl" }
            if ($runExit -ne 0) {
                Add-GuardLine "  (harness exit $runExit -- mesh-integrity FAIL; investigate before pushing) (non-blocking)"
            }
            $cppRan = $true
        } else {
            Add-GuardLine "*** NOT RUN *** - harness compile failed (non-blocking):"
            foreach ($cl in $compileLog) { Add-GuardLine "  $cl" }
        }
    }
}

# (B) Fast Python portal-correctness complement.
Add-GuardLine "------------------------------------------------------------"
Add-GuardLine "CHASEGUARD (portal check) - pure-Python portal-correctness model"
$guardPath = Join-Path $projectDir "tests\chase_pathfinding_guard.py"
if (-not (Test-Path $guardPath)) {
    Add-GuardLine "*** NOT RUN *** - tests\chase_pathfinding_guard.py missing (non-blocking)"
} elseif ($null -eq $pyExe) {
    Add-GuardLine "*** NOT RUN *** - no python on PATH (non-blocking)"
} else {
    $guardOut  = & $pyExe $guardPath 2>&1
    $guardExit = $LASTEXITCODE
    if ($guardExit -eq 0) {
        Add-GuardLine "PASS - portal check OK (non-blocking)"
    } else {
        Add-GuardLine "*** FAIL *** - portal check exit $guardExit (non-blocking; investigate before pushing)"
    }
    foreach ($gl in $guardOut) { Add-GuardLine "  $gl" }
}
Add-GuardLine "============================================================"

# Write the log file
[System.IO.File]::WriteAllText($logPath, $shared.LogContent.ToString())

# --- Show the result dialog ---
if ($exitCode -eq 0) {
    [System.Windows.Forms.MessageBox]::Show(
        "FF8 Original PC Accessibility Mod Version $modVersion built and deployed successfully.",
        "Building FF8 OPC Accessibility Mod Version $modVersion",
        [System.Windows.Forms.MessageBoxButtons]::OK,
        [System.Windows.Forms.MessageBoxIcon]::Information
    ) | Out-Null
}
else {
    $errForm = New-Object System.Windows.Forms.Form
    $errForm.Text            = "Building FF8 OPC Accessibility Mod Version $modVersion - Error"
    $errForm.StartPosition   = "CenterScreen"
    $errForm.FormBorderStyle = "FixedDialog"
    $errForm.MaximizeBox     = $false
    $errForm.MinimizeBox     = $false
    $errForm.Width           = 420
    $errForm.Height          = 180

    $errLabel = New-Object System.Windows.Forms.Label
    $errLabel.Text     = "An error occurred during build or deploy. Check the build log for details."
    $errLabel.AutoSize = $false
    $errLabel.Width    = 380
    $errLabel.Height   = 60
    $errLabel.Location = New-Object System.Drawing.Point(14, 16)
    $errForm.Controls.Add($errLabel)

    $btnLog = New-Object System.Windows.Forms.Button
    $btnLog.Text     = "View Build Log"
    $btnLog.Width    = 120
    $btnLog.Height   = 32
    $btnLog.Location = New-Object System.Drawing.Point(148, 90)
    $btnLog.Add_Click({ Start-Process "notepad.exe" -ArgumentList $logPath })
    $errForm.Controls.Add($btnLog)

    $btnClose = New-Object System.Windows.Forms.Button
    $btnClose.Text         = "Close"
    $btnClose.Width        = 90
    $btnClose.Height       = 32
    $btnClose.Location     = New-Object System.Drawing.Point(280, 90)
    $btnClose.DialogResult = [System.Windows.Forms.DialogResult]::Cancel
    $errForm.CancelButton  = $btnClose
    $errForm.Controls.Add($btnClose)

    $errForm.AcceptButton = $btnLog
    [void]$errForm.ShowDialog()
}
