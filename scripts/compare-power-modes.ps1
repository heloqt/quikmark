param(
    [int]$Samples = 20,
    [int]$LargeSamples = 5,
    [string[]]$Modes = @("balanced", "efficiency"),
    [string]$OriginalOverlay = "961cc777-2547-4f9d-8174-7d86181b8a7a",
    [string]$OutputName = "latest"
)

$ErrorActionPreference = "Stop"

if (Get-Process litemark -ErrorAction SilentlyContinue) {
    throw "Close all running litemark instances before benchmarking."
}

$projectRoot = Split-Path -Parent $PSScriptRoot
$outputDir = Join-Path $projectRoot "build\power-performance\$OutputName"
$benchmarkScript = Join-Path $projectRoot "scripts\benchmark.ps1"
$resultFile = Join-Path $projectRoot "build\performance-results.json"
New-Item -ItemType Directory -Path $outputDir -Force | Out-Null

$definitions = @{
    balanced = [pscustomobject]@{
        Label = "平衡"
        Overlay = "00000000-0000-0000-0000-000000000000"
    }
    efficiency = [pscustomobject]@{
        Label = "最佳能效"
        Overlay = "961cc777-2547-4f9d-8174-7d86181b8a7a"
    }
    performance = [pscustomobject]@{
        Label = "高性能"
        Overlay = "ded574b5-45a0-4f42-8737-46345c09c238"
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
        Ac = $state.ActiveOverlayAcPowerScheme
        Dc = $state.ActiveOverlayDcPowerScheme
        BatteryPercent = if ($battery) {
            $battery.EstimatedChargeRemaining
        } else {
            $null
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
    return $state
}

$summary = [System.Collections.ArrayList]::new()
try {
    foreach ($modeName in $Modes) {
        if (-not $definitions.ContainsKey($modeName)) {
            throw "Unknown power mode: $modeName"
        }
        $before = Set-PowerMode $modeName
        if ($null -ne $before.BatteryPercent -and
            $before.BatteryPercent -lt 10) {
            throw "Battery is below 10%; aborting to avoid shutdown."
        }

        Write-Host "Running $($definitions[$modeName].Label) mode..."
        & $benchmarkScript -Samples $Samples -LargeSamples $LargeSamples
        if ($LASTEXITCODE -ne 0) {
            throw "Benchmark failed for mode $modeName"
        }

        $after = Get-OverlayState
        $prefix = "$modeName-$($summary.Count + 1)"
        $savedResult = Join-Path $outputDir "$prefix.json"
        Copy-Item -LiteralPath $resultFile -Destination $savedResult -Force
        foreach ($traceName in @(
            "perf-cold.csv",
            "perf-hot-switch.csv",
            "perf-large-cold.csv",
            "perf-large-interactive.csv",
            "perf-scroll.csv"
        )) {
            $trace = Join-Path $projectRoot "build\$traceName"
            if (Test-Path -LiteralPath $trace) {
                Copy-Item -LiteralPath $trace `
                    -Destination (Join-Path $outputDir "$prefix-$traceName") `
                    -Force
            }
        }

        $result = Get-Content -LiteralPath $savedResult -Raw |
            ConvertFrom-Json
        [void]$summary.Add([pscustomobject]@{
            Run = $summary.Count + 1
            Mode = $modeName
            Label = $definitions[$modeName].Label
            Overlay = $before.Active
            OnAc = $before.OnAc
            BatteryBefore = $before.BatteryPercent
            BatteryAfter = $after.BatteryPercent
            ColdP50 = $result.ColdStartMs.P50
            ColdP95 = $result.ColdStartMs.P95
            HotP50 = $result.HotOpenMs.P50
            HotP95 = $result.HotOpenMs.P95
            SwitchP50 = $result.TabSwitchMs.P50
            SwitchP95 = $result.TabSwitchMs.P95
            ScrollP50 = $result.ScrollMs.P50
            ScrollP95 = $result.ScrollMs.P95
            ScrollMax = $result.ScrollMs.Max
            LargeColdP95 = $result.Large10MB.ColdStartMs.P95
            LargeHotP95 = $result.Large10MB.HotOpenMs.P95
            LargeSwitchP95 = $result.Large10MB.TabSwitchMs.P95
            LargeScrollP95 = $result.Large10MB.ScrollMs.P95
            ResultFile = $savedResult
        })
    }
} finally {
    powercfg /overlaysetactive $OriginalOverlay | Out-Null
    Start-Sleep -Seconds 1
}

$summary | ConvertTo-Json -Depth 4 |
    Set-Content -LiteralPath (Join-Path $outputDir "summary.json") -Encoding UTF8
$summary | Format-Table -AutoSize
