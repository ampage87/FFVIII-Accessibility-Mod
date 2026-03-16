# ============================================================
# FF8 Original PC Accessibility Mod - Deploy UI
# Shows a progress dialog while running deploy.bat, then
# displays a success or error result dialog.
# Screen reader accessible via AccessibilityNotifyClients.
# ============================================================

Add-Type -AssemblyName System.Windows.Forms
Add-Type -AssemblyName System.Drawing

$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$batPath   = Join-Path $scriptDir "deploy.bat"
$logsDir   = Join-Path $scriptDir "Logs"
if (-not (Test-Path $logsDir)) { New-Item -ItemType Directory -Path $logsDir | Out-Null }
$logPath   = Join-Path $logsDir "build_latest.log"

# Extract version from ff8_accessibility.h
$headerPath = Join-Path $scriptDir "src\ff8_accessibility.h"
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
$process.StartInfo.WorkingDirectory        = $scriptDir
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
