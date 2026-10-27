param(
    [string]$Exe = "build\mingw-release-static\bin\quikmark.exe"
)

$ErrorActionPreference = "Stop"
$projectRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot "..")).Path
if (-not [IO.Path]::IsPathRooted($Exe)) {
    $Exe = Join-Path $projectRoot $Exe
}
if (-not (Test-Path -LiteralPath $Exe)) {
    throw "Executable not found: $Exe"
}
if (Get-Process -Name quikmark,liteMark,litemark -ErrorAction SilentlyContinue) {
    throw "Close all running QuikMark or liteMark instances first."
}

Add-Type @'
using System;
using System.Runtime.InteropServices;
public static class HelpRegressionNative {
    [DllImport("user32.dll")] public static extern IntPtr GetMenu(IntPtr hWnd);
    [DllImport("user32.dll")] public static extern IntPtr GetSubMenu(IntPtr menu, int position);
    [DllImport("user32.dll")] public static extern int GetMenuItemID(IntPtr menu, int position);
    [DllImport("user32.dll")] public static extern IntPtr SendMessage(IntPtr hWnd, uint message, IntPtr wParam, IntPtr lParam);
    [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr hWnd);
}
'@

function Wait-Title(
    [System.Diagnostics.Process]$Process,
    [string]$Pattern,
    [int]$TimeoutSeconds = 15
) {
    $deadline = [DateTime]::UtcNow.AddSeconds($TimeoutSeconds)
    while ([DateTime]::UtcNow -lt $deadline) {
        $Process.Refresh()
        if ($Process.HasExited) {
            throw "Process exited while waiting for title '$Pattern'."
        }
        if ($Process.MainWindowTitle -like $Pattern) {
            return
        }
        Start-Sleep -Milliseconds 25
    }
    throw "Timed out waiting for title '$Pattern'."
}

function Reset-TestDirectory([string]$Path) {
    if (Test-Path -LiteralPath $Path) {
        $resolved = (Resolve-Path -LiteralPath $Path).Path
        if (-not $resolved.StartsWith(
                $projectRoot, [StringComparison]::OrdinalIgnoreCase)) {
            throw "Refusing to remove unexpected path: $resolved"
        }
        Remove-Item -LiteralPath $resolved -Recurse -Force
    }
    New-Item -ItemType Directory -Path $Path -Force | Out-Null
}

$configDir = Join-Path $projectRoot "build\help-regression-config"
Reset-TestDirectory $configDir
$oldQuikmarkConfig = $env:QUIKMARK_CONFIG_DIR
$oldLitemarkConfig = $env:LITEMARK_CONFIG_DIR
$env:QUIKMARK_CONFIG_DIR = $configDir
$env:LITEMARK_CONFIG_DIR = $configDir
$process = $null
try {
    $process = Start-Process -FilePath $Exe -PassThru
    Wait-Title $process "*QuikMark*"
    Start-Sleep -Milliseconds 500
    [void][HelpRegressionNative]::SetForegroundWindow(
        $process.MainWindowHandle)

    $mainMenu = [HelpRegressionNative]::GetMenu(
        $process.MainWindowHandle)
    $helpMenu = [HelpRegressionNative]::GetSubMenu($mainMenu, 2)
    $usageCommand = [HelpRegressionNative]::GetMenuItemID($helpMenu, 0)
    if ($usageCommand -le 0) {
        throw "Usage menu command was not found."
    }
    [void][HelpRegressionNative]::SendMessage(
        $process.MainWindowHandle, 0x0111,
        [IntPtr]$usageCommand, [IntPtr]::Zero)
    Wait-Title $process "*使用说明.md*"
    Start-Sleep -Milliseconds 500

    if ((Get-Process -Name quikmark,liteMark,litemark `
            -ErrorAction SilentlyContinue).Count -ne 1) {
        throw "Opening help started an additional QuikMark instance."
    }
    if (-not $process.CloseMainWindow()) {
        throw "QuikMark did not accept a normal window close."
    }
    if (-not $process.WaitForExit(5000)) {
        throw "QuikMark did not exit after closing the test window."
    }

    $workspacePath = Join-Path $configDir "workspace.ini"
    if (Test-Path -LiteralPath $workspacePath) {
        $workspace = Get-Content -LiteralPath $workspacePath -Raw
        if ($workspace -match "recent\.count=([1-9][0-9]*)") {
            throw "Opening help should not add it to recent documents."
        }
    }

    [pscustomobject]@{
        HelpTab = "PASS"
        SingleInstance = "PASS"
        RecentDocuments = "PASS"
        Result = "PASS"
    } | Format-List
} finally {
    if ($process -and -not $process.HasExited) {
        Stop-Process -Id $process.Id -Force
    }
    $env:QUIKMARK_CONFIG_DIR = $oldQuikmarkConfig
    $env:LITEMARK_CONFIG_DIR = $oldLitemarkConfig
}
