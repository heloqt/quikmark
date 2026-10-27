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
using System.Text;
public static class TableCopyRegressionNative {
    [StructLayout(LayoutKind.Sequential)]
    public struct RECT { public int Left; public int Top; public int Right; public int Bottom; }
    public delegate bool EnumProc(IntPtr hWnd, IntPtr lParam);
    [DllImport("user32.dll")] public static extern bool EnumChildWindows(IntPtr parent, EnumProc callback, IntPtr data);
    [DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc callback, IntPtr data);
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr hWnd, out RECT rect);
    [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr hWnd);
    [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern int GetClassName(IntPtr hWnd, StringBuilder name, int count);
    [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr hWnd);
    [DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow();
    [DllImport("user32.dll")] public static extern bool BringWindowToTop(IntPtr hWnd);
    [DllImport("user32.dll")] public static extern IntPtr SetFocus(IntPtr hWnd);
    [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr hWnd, out uint processId);
    [DllImport("kernel32.dll")] public static extern uint GetCurrentThreadId();
    [DllImport("user32.dll")] public static extern bool AttachThreadInput(uint attach, uint attachTo, bool value);
    [DllImport("user32.dll")] public static extern IntPtr SendMessage(IntPtr hWnd, uint message, IntPtr wParam, IntPtr lParam);
    [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr hWnd, uint message, IntPtr wParam, IntPtr lParam);
    [DllImport("user32.dll")] public static extern bool SetWindowPos(IntPtr hWnd, IntPtr after, int x, int y, int width, int height, uint flags);
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

function Get-LargestChild([IntPtr]$Parent) {
    $items = [System.Collections.ArrayList]::new()
    [void][TableCopyRegressionNative]::EnumChildWindows($Parent, {
        param($handle, $unused)
        $rect = [TableCopyRegressionNative+RECT]::new()
        [void][TableCopyRegressionNative]::GetWindowRect($handle, [ref]$rect)
        [void]$items.Add([pscustomobject]@{
            Handle = $handle
            Area = ($rect.Right - $rect.Left) * ($rect.Bottom - $rect.Top)
        })
        return $true
    }, [IntPtr]::Zero)
    return $items | Sort-Object Area -Descending | Select-Object -First 1
}

function Make-LParam([int]$X, [int]$Y) {
    return [IntPtr]([int64](($Y -band 0xffff) -shl 16) -bor
                    [int64]($X -band 0xffff))
}

function Send-MouseDrag(
    [IntPtr]$Window,
    [int]$StartX,
    [int]$StartY,
    [int]$EndX,
    [int]$EndY
) {
    $start = Make-LParam $StartX $StartY
    $end = Make-LParam $EndX $EndY
    [void][TableCopyRegressionNative]::SendMessage($Window, 0x0201, [IntPtr]1, $start)
    Start-Sleep -Milliseconds 80
    [void][TableCopyRegressionNative]::SendMessage($Window, 0x0200, [IntPtr]1, $end)
    Start-Sleep -Milliseconds 80
    [void][TableCopyRegressionNative]::SendMessage($Window, 0x0202, [IntPtr]0, $end)
    Start-Sleep -Milliseconds 150
}

function Get-PopupMenus() {
    $items = [System.Collections.ArrayList]::new()
    [void][TableCopyRegressionNative]::EnumWindows({
        param($handle, $unused)
        if ([TableCopyRegressionNative]::IsWindowVisible($handle)) {
            $name = [Text.StringBuilder]::new(128)
            [void][TableCopyRegressionNative]::GetClassName(
                $handle, $name, $name.Capacity)
            if ($name.ToString() -eq "#32768") {
                [void]$items.Add($handle)
            }
        }
        return $true
    }, [IntPtr]::Zero)
    return @($items)
}

function Focus-Window([IntPtr]$Window, [IntPtr]$FocusTarget = [IntPtr]::Zero) {
    $foreground = [TableCopyRegressionNative]::GetForegroundWindow()
    $foregroundProcess = [uint32]0
    $foregroundThread =
        [TableCopyRegressionNative]::GetWindowThreadProcessId(
            $foreground, [ref]$foregroundProcess)
    $currentThread = [TableCopyRegressionNative]::GetCurrentThreadId()
    $attached = $false
    if ($foregroundThread -ne 0 -and $foregroundThread -ne $currentThread) {
        $attached = [TableCopyRegressionNative]::AttachThreadInput(
            $currentThread, $foregroundThread, $true)
    }
    try {
        [void][TableCopyRegressionNative]::BringWindowToTop($Window)
        [void][TableCopyRegressionNative]::SetForegroundWindow($Window)
        if ($FocusTarget -ne [IntPtr]::Zero) {
            [void][TableCopyRegressionNative]::SetFocus($FocusTarget)
        }
    } finally {
        if ($attached) {
            [void][TableCopyRegressionNative]::AttachThreadInput(
                $currentThread, $foregroundThread, $false)
        }
    }
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

$fixtureDir = Join-Path $projectRoot "build\table-copy-regression"
$configDir = Join-Path $projectRoot "build\table-copy-regression-config"
Reset-TestDirectory $fixtureDir
Reset-TestDirectory $configDir

$fixture = Join-Path $fixtureDir "table-copy.md"
$fixtureText = @'
| Description | State |
| --- | --- |
| Alpha item | Ready |
| Beta item | Done |
'@
[IO.File]::WriteAllText(
    $fixture, $fixtureText, [Text.UTF8Encoding]::new($false))

$oldQuikmarkConfig = $env:QUIKMARK_CONFIG_DIR
$oldLitemarkConfig = $env:LITEMARK_CONFIG_DIR
$env:QUIKMARK_CONFIG_DIR = $configDir
$env:LITEMARK_CONFIG_DIR = $configDir
$process = $null
try {
    $process = Start-Process -FilePath $Exe -ArgumentList $fixture -PassThru
    Wait-Title $process "table-copy.md*"
    [void][TableCopyRegressionNative]::SetWindowPos(
        $process.MainWindowHandle, [IntPtr]::Zero,
        50, 50, 1000, 700, 0x0040)
    Start-Sleep -Milliseconds 900

    $document = Get-LargestChild $process.MainWindowHandle
    if ($document -eq $null) {
        throw "Document view was not found."
    }
    Focus-Window $process.MainWindowHandle $document.Handle

    Send-MouseDrag $document.Handle 43 96 80 96
    [void][TableCopyRegressionNative]::SendMessage(
        $document.Handle, 0x0301, [IntPtr]::Zero, [IntPtr]::Zero)
    Start-Sleep -Milliseconds 250
    $selected = Get-Clipboard -Raw
    if ($selected -notmatch "Alpha" -or
        $selected -match "Description|State|Beta|\t") {
        throw "Table cell selection copy failed. Clipboard: $selected"
    }

    Send-MouseDrag $document.Handle 43 96 175 128
    [void][TableCopyRegressionNative]::SendMessage(
        $document.Handle, 0x0301, [IntPtr]::Zero, [IntPtr]::Zero)
    Start-Sleep -Milliseconds 250
    $crossCellText = (Get-Clipboard -Raw) -replace "`r`n", "`n"
    if ($crossCellText -notmatch "Alpha item`tReady`nBeta" -or
        $crossCellText -match "Description|State") {
        throw "Table cross-cell selection copy failed. Clipboard: $crossCellText"
    }

    Focus-Window $process.MainWindowHandle $document.Handle
    $documentRect = [TableCopyRegressionNative+RECT]::new()
    [void][TableCopyRegressionNative]::GetWindowRect(
        $document.Handle, [ref]$documentRect)
    $menuPoint = Make-LParam `
        ($documentRect.Left + 90) ($documentRect.Top + 60)
    [void][TableCopyRegressionNative]::PostMessage(
        $document.Handle, 0x007B, $document.Handle, $menuPoint)
    Start-Sleep -Milliseconds 400
    $menuWindows = @(Get-PopupMenus)
    if ($menuWindows.Count -eq 0) {
        throw "Table context menu did not open."
    }
    foreach ($menuWindow in $menuWindows) {
        [void][TableCopyRegressionNative]::SendMessage(
            $menuWindow, 0x0100, [IntPtr]0x28, [IntPtr]::Zero)
        [void][TableCopyRegressionNative]::SendMessage(
            $menuWindow, 0x0101, [IntPtr]0x28, [IntPtr]::Zero)
        [void][TableCopyRegressionNative]::SendMessage(
            $menuWindow, 0x0100, [IntPtr]0x0D, [IntPtr]::Zero)
        [void][TableCopyRegressionNative]::SendMessage(
            $menuWindow, 0x0101, [IntPtr]0x0D, [IntPtr]::Zero)
    }
    Start-Sleep -Milliseconds 400

    $tableText = (Get-Clipboard -Raw) -replace "`r`n", "`n"
    $tableText = $tableText.TrimEnd("`r", "`n")
    $expected = "Description`tState`nAlpha item`tReady`nBeta item`tDone"
    if ($tableText -ne $expected) {
        throw "Whole-table TSV copy failed. Clipboard: $tableText"
    }

    Add-Type -AssemblyName System.Windows.Forms
    $clipboardData = [Windows.Forms.Clipboard]::GetDataObject()
    $formats = $clipboardData.GetFormats()
    if ($formats -notcontains "HTML Format") {
        throw "Whole-table copy did not publish HTML Format."
    }
    $html = [string][Windows.Forms.Clipboard]::GetData("HTML Format")
    if ($html -notmatch "<table" -or
        $html -notmatch "Alpha item" -or
        $html -notmatch "Beta item") {
        throw "Whole-table HTML copy is incomplete."
    }

    [pscustomobject]@{
        CellSelectionText = $selected.Trim()
        CrossCellSelection = "PASS"
        WholeTableText = "PASS"
        WholeTableHtml = "PASS"
        ClipboardFormats = ($formats -join ", ")
        Result = "PASS"
    } | Format-List
} finally {
    if ($process -and -not $process.HasExited) {
        Stop-Process -Id $process.Id -Force
    }
    $env:QUIKMARK_CONFIG_DIR = $oldQuikmarkConfig
    $env:LITEMARK_CONFIG_DIR = $oldLitemarkConfig
}
