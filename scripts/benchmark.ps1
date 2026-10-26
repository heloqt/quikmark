param(
    [string]$Exe = "",
    [int]$Samples = 20,
    [int]$LargeSamples = 3
)

$ErrorActionPreference = "Stop"

if (Get-Process -Name quikmark,liteMark,litemark -ErrorAction SilentlyContinue) {
    throw "Close all running QuikMark or liteMark instances before benchmarking."
}
$projectRoot = if ($PSScriptRoot) {
    Split-Path -Parent $PSScriptRoot
} else {
    (Get-Location).Path
}
if ([string]::IsNullOrWhiteSpace($Exe)) {
    $portableExe = Join-Path $projectRoot "quikmark.exe"
    $Exe = if (Test-Path -LiteralPath $portableExe) {
        $portableExe
    } else {
        Join-Path $projectRoot "build\mingw-release-static\bin\quikmark.exe"
    }
} elseif (-not [IO.Path]::IsPathRooted($Exe)) {
    $Exe = Join-Path $projectRoot $Exe
}
if (-not (Test-Path -LiteralPath $Exe)) {
    throw "Executable not found: $Exe"
}

$performanceConfigDir = Join-Path $projectRoot "build\perf-config"
if (Test-Path -LiteralPath $performanceConfigDir) {
    $resolvedConfig = (Resolve-Path -LiteralPath $performanceConfigDir).Path
    if (-not $resolvedConfig.StartsWith(
            $projectRoot, [StringComparison]::OrdinalIgnoreCase)) {
        throw "Refusing to remove unexpected benchmark config path: $resolvedConfig"
    }
    Remove-Item -LiteralPath $resolvedConfig -Recurse -Force
}
New-Item -ItemType Directory -Path $performanceConfigDir -Force | Out-Null
$env:LITEMARK_CONFIG_DIR = $performanceConfigDir
$cpu = Get-CimInstance Win32_Processor | Select-Object -First 1
$system = Get-CimInstance Win32_ComputerSystem
$operatingSystem = Get-CimInstance Win32_OperatingSystem
$powerPlan = (powercfg /getactivescheme | Out-String).Trim()
$powerOverlayState = Get-ItemProperty -LiteralPath `
    "HKLM:\SYSTEM\CurrentControlSet\Control\Power\User\PowerSchemes"
$battery = Get-CimInstance Win32_Battery -ErrorAction SilentlyContinue |
    Select-Object -First 1
$onAc = -not $battery -or $battery.BatteryStatus -eq 2
$powerOverlay = if ($onAc) {
    $powerOverlayState.ActiveOverlayAcPowerScheme
} else {
    $powerOverlayState.ActiveOverlayDcPowerScheme
}
$powerModeName = switch ($powerOverlay) {
    "00000000-0000-0000-0000-000000000000" { "balanced"; break }
    "961cc777-2547-4f9d-8174-7d86181b8a7a" { "best-efficiency"; break }
    "ded574b5-45a0-4f42-8737-46345c09c238" { "best-performance"; break }
    default { "unknown" }
}
$exeItem = Get-Item -LiteralPath $Exe
$exeHash = (Get-FileHash -LiteralPath $Exe -Algorithm SHA256).Hash
$diskModels = @(
    Get-CimInstance Win32_DiskDrive -ErrorAction SilentlyContinue |
        ForEach-Object { $_.Model }
)
$fixtureDir = Join-Path $projectRoot "build\perf-fixtures"
$largeFixtureDir = Join-Path $projectRoot "build\perf-large-fixtures"
$coldTrace = Join-Path $projectRoot "build\perf-cold.csv"
$interactiveTrace = Join-Path $projectRoot "build\perf-hot-switch.csv"
$largeColdTrace = Join-Path $projectRoot "build\perf-large-cold.csv"
$largeInteractiveTrace = Join-Path $projectRoot "build\perf-large-interactive.csv"
$scrollTrace = Join-Path $projectRoot "build\perf-scroll.csv"
$resultFile = Join-Path $projectRoot "build\performance-results.json"
New-Item -ItemType Directory -Path $fixtureDir -Force | Out-Null
New-Item -ItemType Directory -Path $largeFixtureDir -Force | Out-Null

$chunk = @"
# Performance fixture

This benchmark document contains Chinese 中文文本, **bold**, *italic*,
[link](https://example.com), and `inline code`.

- first list item
- second list item

> quoted content

`````text
code block
`````

| Name | Value |
| --- | --- |
| A | 1 |

"@

$builder = [Text.StringBuilder]::new(170000)
while ($builder.Length -lt 150 * 1024) {
    [void]$builder.Append($chunk)
}
$fixtureContent = $builder.ToString()
$fixtures = @()
for ($i = 1; $i -le $Samples; ++$i) {
    $file = Join-Path $fixtureDir ("perf-{0:D2}.md" -f $i)
    [IO.File]::WriteAllText($file, $fixtureContent, [Text.UTF8Encoding]::new($false))
    $fixtures += $file
}

$largeBuilder = [Text.StringBuilder]::new(11 * 1024 * 1024)
while ($largeBuilder.Length -lt 10 * 1024 * 1024) {
    [void]$largeBuilder.Append($chunk)
}
$largeContent = $largeBuilder.ToString()
$largeSamples = [math]::Max(3, $LargeSamples)
$largeFixtures = @()
for ($i = 1; $i -le $largeSamples; ++$i) {
    $file = Join-Path $largeFixtureDir ("large-{0:D2}.md" -f $i)
    [IO.File]::WriteAllText($file, $largeContent, [Text.UTF8Encoding]::new($false))
    $largeFixtures += $file
}

function Clear-Trace([string]$Path) {
    [IO.File]::WriteAllText($Path, "", [Text.UTF8Encoding]::new($false))
}

function Read-Metric([string]$Path, [string]$EventName) {
    if (-not (Test-Path -LiteralPath $Path)) {
        return @()
    }
    return @(Get-Content -LiteralPath $Path -ErrorAction SilentlyContinue |
        Where-Object { $_ -like "$EventName,*" } |
        ForEach-Object { [double]($_.Split(',')[1]) / 1000.0 })
}

function Wait-MetricCount(
    [string]$Path,
    [string]$EventName,
    [int]$Expected,
    [int]$TimeoutSeconds = 8
) {
    $deadline = [DateTime]::UtcNow.AddSeconds($TimeoutSeconds)
    while ([DateTime]::UtcNow -lt $deadline) {
        if ((Read-Metric $Path $EventName).Count -ge $Expected) {
            return
        }
        Start-Sleep -Milliseconds 25
    }
    throw "Timed out waiting for $EventName count $Expected in $Path"
}

function Get-Stats([double[]]$Values) {
    if ($Values.Count -eq 0) {
        return [pscustomobject]@{ Samples = 0; Min = 0; P50 = 0; P95 = 0; Max = 0 }
    }
    $sorted = @($Values | Sort-Object)
    return [pscustomobject]@{
        Samples = $sorted.Count
        Min = [math]::Round($sorted[0], 3)
        P50 = [math]::Round($sorted[[math]::Floor(($sorted.Count - 1) / 2)], 3)
        P95 = [math]::Round($sorted[[math]::Ceiling($sorted.Count * 0.95) - 1], 3)
        Max = [math]::Round($sorted[-1], 3)
    }
}

function Wait-MainWindow([System.Diagnostics.Process]$Process, [int]$TimeoutSeconds = 8) {
    $deadline = [DateTime]::UtcNow.AddSeconds($TimeoutSeconds)
    while ([DateTime]::UtcNow -lt $deadline) {
        $Process.Refresh()
        if ($Process.HasExited) {
            throw "Process $($Process.Id) exited before showing a window."
        }
        if ($Process.MainWindowHandle -ne [IntPtr]::Zero) {
            return
        }
        Start-Sleep -Milliseconds 25
    }
    throw "Timed out waiting for process $($Process.Id) window."
}

function Send-NextTabCommand([IntPtr]$Window) {
    [void][BenchmarkNative]::SendMessage(
        $Window, 0x0111, [IntPtr]6009, [IntPtr]::Zero)
}

Add-Type @'
using System;
using System.Runtime.InteropServices;
public static class BenchmarkNative {
    [StructLayout(LayoutKind.Sequential)]
    public struct RECT { public int Left; public int Top; public int Right; public int Bottom; }
    public delegate bool EnumProc(IntPtr hWnd, IntPtr lParam);
    [DllImport("user32.dll")] public static extern bool EnumChildWindows(IntPtr parent, EnumProc callback, IntPtr data);
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr hWnd, out RECT rect);
    [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr hWnd);
    [DllImport("user32.dll")] public static extern void keybd_event(byte key, byte scan, uint flags, UIntPtr extra);
    [DllImport("user32.dll")] public static extern IntPtr SendMessage(IntPtr hWnd, uint message, IntPtr wParam, IntPtr lParam);
}
'@

function Get-LargestChild([IntPtr]$Parent) {
    $items = [System.Collections.ArrayList]::new()
    [void][BenchmarkNative]::EnumChildWindows($Parent, {
        param($handle, $unused)
        $rect = [BenchmarkNative+RECT]::new()
        [void][BenchmarkNative]::GetWindowRect($handle, [ref]$rect)
        $area = ($rect.Right - $rect.Left) * ($rect.Bottom - $rect.Top)
        [void]$items.Add([pscustomobject]@{
            Handle = $handle
            Rect = $rect
            Area = $area
        })
        return $true
    }, [IntPtr]::Zero)
    return $items | Sort-Object Area -Descending | Select-Object -First 1
}

# Cold startup: process creation to first readable viewport.
Clear-Trace $coldTrace
$env:LITEMARK_PERF_TRACE = $coldTrace
for ($i = 0; $i -lt $Samples; ++$i) {
    $process = Start-Process -FilePath $Exe -ArgumentList $fixtures[$i] -PassThru
    try {
        Wait-MetricCount $coldTrace "cold_start" ($i + 1)
        Wait-MetricCount $coldTrace "parse" ($i + 1)
        Wait-MetricCount $coldTrace "file_read" ($i + 1)
        Wait-MetricCount $coldTrace "utf8_convert" ($i + 1)
        Wait-MetricCount $coldTrace "prefix_md4c_parse" ($i + 1)
        Wait-MetricCount $coldTrace "md4c_parse" ($i + 1)
        Wait-MetricCount $coldTrace "normalize" ($i + 1)
        Wait-MetricCount $coldTrace "layout_init" ($i + 1)
        Wait-MetricCount $coldTrace "visible_layout" ($i + 1)
        Wait-MetricCount $coldTrace "draw" ($i + 1)
    } finally {
        if (-not $process.HasExited) {
            Stop-Process -Id $process.Id -Force
            Wait-Process -Id $process.Id -Timeout 5 -ErrorAction SilentlyContinue
        }
    }
    Start-Sleep -Milliseconds 80
}
$coldStats = Get-Stats (Read-Metric $coldTrace "cold_start")
$coldParseStats = Get-Stats (Read-Metric $coldTrace "parse")
$coldFileReadStats = Get-Stats (Read-Metric $coldTrace "file_read")
$coldUtf8Stats = Get-Stats (Read-Metric $coldTrace "utf8_convert")
$coldPrefixStats = Get-Stats (Read-Metric $coldTrace "prefix_md4c_parse")
$coldMd4cStats = Get-Stats (Read-Metric $coldTrace "md4c_parse")
$coldNormalizeStats = Get-Stats (Read-Metric $coldTrace "normalize")
$coldLayoutInitStats = Get-Stats (Read-Metric $coldTrace "layout_init")
$coldVisibleLayoutStats = Get-Stats (Read-Metric $coldTrace "visible_layout")
$coldDrawStats = Get-Stats (Read-Metric $coldTrace "draw")
Write-Host "Completed regular cold-start samples: $($coldStats.Samples)"

# Hot open followed by tab switching in one primary process.
Clear-Trace $interactiveTrace
$env:LITEMARK_PERF_TRACE = $interactiveTrace
$primary = Start-Process -FilePath $Exe -ArgumentList $fixtures[0] -PassThru
try {
    Wait-MetricCount $interactiveTrace "hot_open" 1
    for ($i = 1; $i -lt $Samples; ++$i) {
        $secondary = Start-Process -FilePath $Exe -ArgumentList $fixtures[$i] -PassThru -Wait
        if ($secondary.ExitCode -ne 0) {
            throw "Secondary instance failed with $($secondary.ExitCode)"
        }
        Wait-MetricCount $interactiveTrace "hot_open" ($i + 1)
    }

    $primary.Refresh()
    [void][BenchmarkNative]::SetForegroundWindow($primary.MainWindowHandle)
    Start-Sleep -Milliseconds 200
    $switchCount = [math]::Min(40, $Samples * 2)
    for ($i = 0; $i -lt $switchCount; ++$i) {
        [void][BenchmarkNative]::SetForegroundWindow($primary.MainWindowHandle)
        Send-NextTabCommand $primary.MainWindowHandle
        Wait-MetricCount $interactiveTrace "tab_switch" ($i + 1) 8
    }
} finally {
    if ($primary -and -not $primary.HasExited) {
        Stop-Process -Id $primary.Id -Force
        Wait-Process -Id $primary.Id -Timeout 5 -ErrorAction SilentlyContinue
    }
}
$hotStats = Get-Stats (Read-Metric $interactiveTrace "hot_open")
$switchStats = Get-Stats (Read-Metric $interactiveTrace "tab_switch")
$paintStats = Get-Stats (Read-Metric $interactiveTrace "paint")
Write-Host "Completed regular hot-open and tab-switch samples."

# A 10 MB document from process creation to first readable paint.
Clear-Trace $largeColdTrace
$env:LITEMARK_PERF_TRACE = $largeColdTrace
for ($i = 0; $i -lt $largeSamples; ++$i) {
    $process = Start-Process -FilePath $Exe -ArgumentList $largeFixtures[$i] -PassThru
    try {
        Wait-MetricCount $largeColdTrace "cold_start" ($i + 1) 60
        Wait-MetricCount $largeColdTrace "parse" ($i + 1) 60
        Wait-MetricCount $largeColdTrace "file_read" ($i + 1) 60
        Wait-MetricCount $largeColdTrace "utf8_convert" ($i + 1) 60
        Wait-MetricCount $largeColdTrace "prefix_md4c_parse" ($i + 1) 60
        Wait-MetricCount $largeColdTrace "md4c_parse" ($i + 1) 60
        Wait-MetricCount $largeColdTrace "normalize" ($i + 1) 60
        Wait-MetricCount $largeColdTrace "layout_init" ($i + 1) 60
        Wait-MetricCount $largeColdTrace "visible_layout" ($i + 1) 60
        Wait-MetricCount $largeColdTrace "draw" ($i + 1) 60
    } finally {
        if (-not $process.HasExited) {
            Stop-Process -Id $process.Id -Force
            Wait-Process -Id $process.Id -Timeout 5 -ErrorAction SilentlyContinue
        }
    }
    Start-Sleep -Milliseconds 100
}
$largeColdStats = Get-Stats (Read-Metric $largeColdTrace "cold_start")
$largeParseStats = Get-Stats (Read-Metric $largeColdTrace "parse")
$largeFileReadStats = Get-Stats (Read-Metric $largeColdTrace "file_read")
$largeUtf8Stats = Get-Stats (Read-Metric $largeColdTrace "utf8_convert")
$largePrefixStats = Get-Stats (Read-Metric $largeColdTrace "prefix_md4c_parse")
$largeMd4cStats = Get-Stats (Read-Metric $largeColdTrace "md4c_parse")
$largeNormalizeStats = Get-Stats (Read-Metric $largeColdTrace "normalize")
$largeLayoutInitStats = Get-Stats (Read-Metric $largeColdTrace "layout_init")
$largeVisibleLayoutStats = Get-Stats (Read-Metric $largeColdTrace "visible_layout")
$largeDrawStats = Get-Stats (Read-Metric $largeColdTrace "draw")
Write-Host "Completed 10 MB cold-start samples: $($largeColdStats.Samples)"

# Keep one process warm and open each 10 MB document as its own tab.
Clear-Trace $largeInteractiveTrace
$env:LITEMARK_PERF_TRACE = $largeInteractiveTrace
$largePrimary = Start-Process -FilePath $Exe `
    -ArgumentList $largeFixtures[0] -PassThru
try {
    Wait-MainWindow $largePrimary
    [void][BenchmarkNative]::SetForegroundWindow($largePrimary.MainWindowHandle)
    Wait-MetricCount $largeInteractiveTrace "hot_open" 1 60
    for ($i = 1; $i -lt $largeSamples; ++$i) {
        $secondary = Start-Process -FilePath $Exe `
            -ArgumentList $largeFixtures[$i] -PassThru -Wait
        if ($secondary.ExitCode -ne 0) {
            throw "Large secondary instance failed with $($secondary.ExitCode)"
        }
        Wait-MetricCount $largeInteractiveTrace "hot_open" ($i + 1) 60
    }

    $largeSwitchCount = 20
    for ($i = 0; $i -lt $largeSwitchCount; ++$i) {
        $largePrimary.Refresh()
        [void][BenchmarkNative]::SetForegroundWindow($largePrimary.MainWindowHandle)
        Send-NextTabCommand $largePrimary.MainWindowHandle
        Wait-MetricCount $largeInteractiveTrace "tab_switch" ($i + 1) 20
    }

    Start-Sleep -Milliseconds 250
    $largeProcess = $largePrimary
    $document = Get-LargestChild $largeProcess.MainWindowHandle
    $rect = $document.Rect
    $screenX = $rect.Left + [int](($rect.Right - $rect.Left) / 2)
    $screenY = $rect.Top + [int](($rect.Bottom - $rect.Top) / 2)
    $lParam = [IntPtr]([uint32](($screenY -band 0xffff) -shl 16) -bor
                       [uint32]($screenX -band 0xffff))
    $wParam = [IntPtr](-7864320)
    for ($i = 0; $i -lt 30; ++$i) {
        [void][BenchmarkNative]::SendMessage(
            $document.Handle, 0x020A, $wParam, $lParam)
        Start-Sleep -Milliseconds 35
    }
    Wait-MetricCount $largeInteractiveTrace "scroll" 25 20
} finally {
    if ($largePrimary -and -not $largePrimary.HasExited) {
        Stop-Process -Id $largePrimary.Id -Force
        Wait-Process -Id $largePrimary.Id -Timeout 5 -ErrorAction SilentlyContinue
    }
}
$largeHotStats = Get-Stats (Read-Metric $largeInteractiveTrace "hot_open")
$largeSwitchStats = Get-Stats (Read-Metric $largeInteractiveTrace "tab_switch")
$largeScrollStats = Get-Stats (Read-Metric $largeInteractiveTrace "scroll")
$largePaintStats = Get-Stats (Read-Metric $largeInteractiveTrace "paint")
Write-Host "Completed 10 MB hot-open, tab-switch and scroll samples."

# Repeat scrolling in a fresh process with one 10 MB document to verify the
# first visible large-document scroll independently of the switch phase.
Clear-Trace $scrollTrace
$env:LITEMARK_PERF_TRACE = $scrollTrace
$scrollProcess = Start-Process -FilePath $Exe -ArgumentList $largeFixtures[0] -PassThru
try {
    Wait-MetricCount $scrollTrace "hot_open" 1 15
    Start-Sleep -Milliseconds 500
    $scrollProcess.Refresh()
    $document = Get-LargestChild $scrollProcess.MainWindowHandle
    $rect = $document.Rect
    $screenX = $rect.Left + [int](($rect.Right - $rect.Left) / 2)
    $screenY = $rect.Top + [int](($rect.Bottom - $rect.Top) / 2)
    $lParam = [IntPtr]([uint32](($screenY -band 0xffff) -shl 16) -bor
                       [uint32]($screenX -band 0xffff))
    $wParam = [IntPtr](-7864320)
    for ($i = 0; $i -lt 30; ++$i) {
        [void][BenchmarkNative]::SendMessage(
            $document.Handle, 0x020A, $wParam, $lParam)
        Start-Sleep -Milliseconds 35
    }
    Wait-MetricCount $scrollTrace "scroll" 25 8
} finally {
    if ($scrollProcess -and -not $scrollProcess.HasExited) {
        Stop-Process -Id $scrollProcess.Id -Force
        Wait-Process -Id $scrollProcess.Id -Timeout 5 -ErrorAction SilentlyContinue
    }
    Remove-Item Env:LITEMARK_PERF_TRACE -ErrorAction SilentlyContinue
}
$scrollStats = Get-Stats (Read-Metric $scrollTrace "scroll")
Write-Host "Completed fresh-process 10 MB scroll samples: $($scrollStats.Samples)"

$result = [pscustomobject]@{
    Environment = [pscustomobject]@{
        Machine = "AMD 8845"
        Cpu = $cpu.Name.Trim()
        Cores = $cpu.NumberOfCores
        LogicalProcessors = $cpu.NumberOfLogicalProcessors
        SystemModel = "$($system.Manufacturer) $($system.Model)".Trim()
        MemoryGB = [math]::Round($system.TotalPhysicalMemory / 1GB, 1)
        OperatingSystem = "$($operatingSystem.Caption) $($operatingSystem.Version) build $($operatingSystem.BuildNumber)"
        DisplayScale = "100% (96 DPI)"
        PowerPlan = $powerPlan
        PowerMode = $powerModeName
        PowerOverlay = $powerOverlay
        PowerSource = if ($onAc) { "AC" } else { "Battery" }
        ExePath = $exeItem.FullName
        ExeBytes = $exeItem.Length
        ExeSha256 = $exeHash
        DiskModels = $diskModels
        Build = "Release static"
        SampleDocuments = $Samples
        DocumentSize = "About 150 KB"
        LargeDocumentSize = "10 MB"
    }
    ColdStartMs = $coldStats
    HotOpenMs = $hotStats
    TabSwitchMs = $switchStats
    ScrollMs = $scrollStats
    PaintMs = $paintStats
    ColdStartBreakdown = [pscustomobject]@{
        ParseMs = $coldParseStats
        FileReadMs = $coldFileReadStats
        Utf8ConvertMs = $coldUtf8Stats
        PrefixMd4cParseMs = $coldPrefixStats
        Md4cParseMs = $coldMd4cStats
        NormalizeMs = $coldNormalizeStats
        LayoutInitMs = $coldLayoutInitStats
        VisibleLayoutMs = $coldVisibleLayoutStats
        DrawMs = $coldDrawStats
    }
    Large10MB = [pscustomobject]@{
        ColdStartMs = $largeColdStats
        HotOpenMs = $largeHotStats
        TabSwitchMs = $largeSwitchStats
        ScrollMs = $largeScrollStats
        PaintMs = $largePaintStats
        ColdStartBreakdown = [pscustomobject]@{
            ParseMs = $largeParseStats
            FileReadMs = $largeFileReadStats
            Utf8ConvertMs = $largeUtf8Stats
            PrefixMd4cParseMs = $largePrefixStats
            Md4cParseMs = $largeMd4cStats
            NormalizeMs = $largeNormalizeStats
            LayoutInitMs = $largeLayoutInitStats
            VisibleLayoutMs = $largeVisibleLayoutStats
            DrawMs = $largeDrawStats
        }
        ColdStartChallengePass = $largeColdStats.P95 -le 300
    }
    Pass = (
        $coldStats.P95 -le 500 -and
        $hotStats.P95 -le 300 -and
        $switchStats.P95 -le 50 -and
        $scrollStats.P95 -le 16.7 -and
        $scrollStats.Max -le 50
    )
}

$result | ConvertTo-Json -Depth 5 |
    Set-Content -LiteralPath $resultFile -Encoding UTF8
Remove-Item Env:LITEMARK_CONFIG_DIR -ErrorAction SilentlyContinue
$result | Format-List
Write-Host "Results: $resultFile"
