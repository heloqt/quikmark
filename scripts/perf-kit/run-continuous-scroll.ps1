param(
    [int]$Events = 120,
    [int]$IntervalMs = 0,
    [string]$Name = ""
)

$ErrorActionPreference = "Stop"
$root = $PSScriptRoot
$script = Join-Path $root "scripts\continuous-scroll-benchmark.ps1"
if ([string]::IsNullOrWhiteSpace($Name)) {
    $Name = "continuous-" + (Get-Date -Format "yyyyMMdd-HHmmss")
}
& $script -Events $Events -IntervalMs $IntervalMs `
    -Modes @("balanced", "efficiency") -OutputName $Name
if ($LASTEXITCODE -ne 0) {
    throw "Continuous scroll benchmark failed."
}

$resultDir = Join-Path $root "results\$Name"
New-Item -ItemType Directory -Path $resultDir -Force | Out-Null
$source = Join-Path $root "build\continuous-scroll-results\$Name"
Copy-Item -Path (Join-Path $source "*") -Destination $resultDir `
    -Recurse -Force
Write-Host "Results: $resultDir"
