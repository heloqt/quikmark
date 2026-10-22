param(
    [string]$Exe = "",
    [int]$Events = 120,
    [int]$IntervalMs = 2,
    [string[]]$Modes = @("balanced", "efficiency"),
    [string]$OutputName = "latest"
)

$ErrorActionPreference = "Stop"

if (Get-Process litemark -ErrorAction SilentlyContinue) {
    throw "Close all running litemark instances before benchmarking."
}
$projectRoot = Split-Path -Parent $PSScriptRoot
if ([string]::IsNullOrWhiteSpace($Exe)) {
    $portableExe = Join-Path $projectRoot "liteMark.exe"
    $Exe = if (Test-Path -LiteralPath $portableExe) {
        $portableExe
    } else {
        Join-Path $projectRoot "build\mingw-release-static\bin\litemark.exe"
    }
} elseif (-not [IO.Path]::IsPathRooted($Exe)) {
    $Exe = Join-Path $projectRoot $Exe
}
if (-not (Test-Path -LiteralPath $Exe)) {
    throw "Executable not found: $Exe"
}

$fixtureDir = Join-Path $projectRoot "build\continuous-scroll"
$outputDir = Join-Path $projectRoot "build\continuous-scroll-results\$OutputName"
New-Item -ItemType Directory -Path $fixtureDir -Force | Out-Null
New-Item -ItemType Directory -Path $outputDir -Force | Out-Null

$chunk = @'
# Continuous scroll fixture

Paragraph with 中文内容 and **bold** text for layout.

- alpha item
- beta item

> quoted line

```text
sample code block
```

| Name | Value | Note |
| --- | --- | --- |
| A | 1 | stable content |

'@
$builder = [Text.StringBuilder]::new(170000)
while ($builder.Length -lt 150 * 1024) {
    [void]$builder.Append($chunk)
}
$fixture = Join-Path $fixtureDir "continuous-150k.md"
[IO.File]::WriteAllText($fixture, $builder.ToString(),
                        [Text.UTF8Encoding]::new($false))

Add-Type @'
using System;
using System.Runtime.InteropServices;
public static class ScrollBurstNative {
    [StructLayout(LayoutKind.Sequential)]
    public struct RECT { public int Left; public int Top; public int Right; public int Bottom; }
    [StructLayout(LayoutKind.Sequential)]
    public struct SCROLLINFO {
        public uint cbSize;
        public uint fMask;
        public int nMin;
        public int nMax;
        public uint nPage;
        public int nPos;
        public int nTrackPos;
    }
    public delegate bool EnumProc(IntPtr hWnd, IntPtr lParam);
    [DllImport("user32.dll")] public static extern bool EnumChildWindows(IntPtr parent, EnumProc callback, IntPtr data);
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr hWnd, out RECT rect);
    [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr hWnd, uint message, IntPtr wParam, IntPtr lParam);
    [DllImport("user32.dll")] public static extern bool GetScrollInfo(IntPtr hWnd, int bar, ref SCROLLINFO info);
}
'@

$definitions = @{
    balanced = [pscustomobject]@{
        Label = "平衡"
        Overlay = "00000000-0000-0000-0000-000000000000"
    }
    efficiency = [pscustomobject]@{
        Label = "最佳能效"
        Overlay = "961cc777-2547-4f9d-8174-7d86181b8a7a"
    }
}

function Get-OverlayState {
    $base = "HKLM:\SYSTEM\CurrentControlSet\Control\Power\User\PowerSchemes"
    $state = Get-ItemProperty -LiteralPath $base
    $battery = Get-CimInstance Win32_Battery -ErrorAction SilentlyContinue |
        Select-Object -First 1
    $onAc = $battery -and $battery.BatteryStatus -eq 2
    return [pscustomobject]@{
        OnAc = [bool]$onAc
        Active = if ($onAc) {
            $state.ActiveOverlayAcPowerScheme
        } else {
            $state.ActiveOverlayDcPowerScheme
        }
    }
}

function Set-PowerMode([string]$Name) {
    $definition = $definitions[$Name]
    if ($null -eq $definition) {
        throw "Unknown power mode: $Name"
    }
    powercfg /overlaysetactive $definition.Overlay
    if ($LASTEXITCODE -ne 0) {
        throw "powercfg failed for mode $Name"
    }
    Start-Sleep -Seconds 1
    $state = Get-OverlayState
    if ($state.Active -ne $definition.Overlay) {
        throw "Power mode verification failed: expected $($definition.Overlay), got $($state.Active)"
    }
}

function Wait-Title(
    [System.Diagnostics.Process]$Process,
    [string]$Pattern,
    [int]$TimeoutSeconds = 15
) {
    $deadline = [DateTime]::UtcNow.AddSeconds($TimeoutSeconds)
    while ([DateTime]::UtcNow -lt $deadline) {
        $Process.Refresh()
        if ($Process.HasExited) {
            throw "Process exited while waiting for the document window."
        }
        if ($Process.MainWindowTitle -like $Pattern) {
            return
        }
        Start-Sleep -Milliseconds 25
    }
    throw "Timed out waiting for the document window."
}

function Get-LargestChild([IntPtr]$Parent) {
    $items = [System.Collections.ArrayList]::new()
    [void][ScrollBurstNative]::EnumChildWindows($Parent, {
        param($handle, $unused)
        $rect = [ScrollBurstNative+RECT]::new()
        [void][ScrollBurstNative]::GetWindowRect($handle, [ref]$rect)
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

function Get-VerticalScroll([IntPtr]$Window) {
    $info = [ScrollBurstNative+SCROLLINFO]::new()
    $info.cbSize = [Runtime.InteropServices.Marshal]::SizeOf($info)
    $info.fMask = 0x17
    if (-not [ScrollBurstNative]::GetScrollInfo($Window, 1, [ref]$info)) {
        throw "GetScrollInfo failed."
    }
    return $info.nPos
}

function Read-Metric([string]$Path, [string]$EventName) {
    if (-not (Test-Path -LiteralPath $Path)) {
        return @()
    }
    return @(Get-Content -LiteralPath $Path |
        Where-Object { $_ -like "$EventName,*" } |
        ForEach-Object { [double]$_.Split(',')[1] / 1000.0 })
}

function Get-Stats([double[]]$Values) {
    if ($Values.Count -eq 0) {
        return [pscustomobject]@{
            Samples = 0
            P50 = 0
            P95 = 0
            Max = 0
            Over16_7 = 0
            Over50 = 0
        }
    }
    $sorted = @($Values | Sort-Object)
    return [pscustomobject]@{
        Samples = $sorted.Count
        P50 = [math]::Round(
            $sorted[[math]::Floor(($sorted.Count - 1) / 2)], 3)
        P95 = [math]::Round(
            $sorted[[math]::Ceiling($sorted.Count * 0.95) - 1], 3)
        Max = [math]::Round($sorted[-1], 3)
        Over16_7 = @($sorted | Where-Object { $_ -gt 16.7 }).Count
        Over50 = @($sorted | Where-Object { $_ -gt 50 }).Count
    }
}

$originalState = Get-OverlayState
$summary = [System.Collections.ArrayList]::new()
try {
    foreach ($modeName in $Modes) {
        Set-PowerMode $modeName
        $configDir = Join-Path $outputDir "$modeName-config"
        $traceFile = Join-Path $outputDir "$modeName-trace.csv"
        New-Item -ItemType Directory -Path $configDir -Force | Out-Null
        [IO.File]::WriteAllText($traceFile, "",
                                [Text.UTF8Encoding]::new($false))
        $env:LITEMARK_CONFIG_DIR = $configDir
        $env:LITEMARK_PERF_TRACE = $traceFile

        Write-Host "Running continuous scroll in $($definitions[$modeName].Label) mode..."
        $process = Start-Process -FilePath $Exe -ArgumentList $fixture -PassThru
        try {
            Wait-Title $process "continuous-150k.md*"
            Start-Sleep -Milliseconds 800
            [IO.File]::WriteAllText($traceFile, "",
                                    [Text.UTF8Encoding]::new($false))
            $document = Get-LargestChild $process.MainWindowHandle
            $rect = $document.Rect
            $x = $rect.Left + [int](($rect.Right - $rect.Left) / 2)
            $y = $rect.Top + [int](($rect.Bottom - $rect.Top) / 2)
            $lParam = [IntPtr]([int64](($y -band 0xffff) -shl 16) -bor
                               [int64]($x -band 0xffff))
            $wParam = [IntPtr](-7864320)
            $initialScroll = Get-VerticalScroll $document.Handle
            $timer = [Diagnostics.Stopwatch]::StartNew()
            for ($i = 0; $i -lt $Events; ++$i) {
                if (-not [ScrollBurstNative]::PostMessage(
                        $document.Handle, 0x020A, $wParam, $lParam)) {
                    throw "Failed to post wheel message."
                }
                if ($IntervalMs -gt 0) {
                    Start-Sleep -Milliseconds $IntervalMs
                }
            }
            $timer.Stop()

            $lastCount = -1
            $stableChecks = 0
            $deadline = [DateTime]::UtcNow.AddSeconds(8)
            while ([DateTime]::UtcNow -lt $deadline -and $stableChecks -lt 3) {
                Start-Sleep -Milliseconds 200
                $count = (Read-Metric $traceFile "scroll").Count
                if ($count -gt 0 -and $count -eq $lastCount) {
                    ++$stableChecks
                } else {
                    $stableChecks = 0
                    $lastCount = $count
                }
            }
            if ($lastCount -le 0) {
                throw "No scroll paint samples were recorded."
            }
            $scrollValues = Read-Metric $traceFile "scroll"
            $paintValues = Read-Metric $traceFile "paint"
            $finalScroll = Get-VerticalScroll $document.Handle
            [void]$summary.Add([pscustomobject]@{
                Mode = $modeName
                Label = $definitions[$modeName].Label
                EventsPosted = $Events
                IntervalMs = $IntervalMs
                PostingMs = [math]::Round($timer.Elapsed.TotalMilliseconds, 3)
                ScrollFrames = $scrollValues.Count
                ScrollP50 = (Get-Stats $scrollValues).P50
                ScrollP95 = (Get-Stats $scrollValues).P95
                ScrollMax = (Get-Stats $scrollValues).Max
                ScrollOver16_7 = (Get-Stats $scrollValues).Over16_7
                ScrollOver50 = (Get-Stats $scrollValues).Over50
                PaintP95 = (Get-Stats $paintValues).P95
                PaintMax = (Get-Stats $paintValues).Max
                InitialScroll = $initialScroll
                FinalScroll = $finalScroll
            })
        } finally {
            if ($process -and -not $process.HasExited) {
                Stop-Process -Id $process.Id -Force
                Wait-Process -Id $process.Id -Timeout 5 `
                    -ErrorAction SilentlyContinue
            }
            Remove-Item Env:LITEMARK_CONFIG_DIR -ErrorAction SilentlyContinue
            Remove-Item Env:LITEMARK_PERF_TRACE -ErrorAction SilentlyContinue
        }
    }
} finally {
    powercfg /overlaysetactive $originalState.Active | Out-Null
    Start-Sleep -Seconds 1
}

$summary | ConvertTo-Json -Depth 4 |
    Set-Content -LiteralPath (Join-Path $outputDir "summary.json") -Encoding UTF8
$summary | Format-Table -AutoSize
