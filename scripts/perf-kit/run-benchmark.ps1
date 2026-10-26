param(
    [int]$Samples = 20,
    [int]$LargeSamples = 5,
    [string]$Name = ""
)

$ErrorActionPreference = "Stop"
$root = $PSScriptRoot
$exe = Join-Path $root "quikmark.exe"
$benchmark = Join-Path $root "scripts\benchmark.ps1"
if (-not (Test-Path -LiteralPath $exe)) {
    throw "quikmark.exe not found next to this script."
}
if ([string]::IsNullOrWhiteSpace($Name)) {
    $Name = Get-Date -Format "yyyyMMdd-HHmmss"
}

$resultDir = Join-Path $root "results\$Name"
New-Item -ItemType Directory -Path $resultDir -Force | Out-Null
$global:LASTEXITCODE = 0
& $benchmark -Exe $exe -Samples $Samples -LargeSamples $LargeSamples
if ($LASTEXITCODE -ne 0) {
    throw "Benchmark failed."
}

$build = Join-Path $root "build"
Copy-Item -LiteralPath (Join-Path $build "performance-results.json") `
    -Destination (Join-Path $resultDir "performance-results.json") -Force
foreach ($name in @(
    "perf-cold.csv",
    "perf-hot-switch.csv",
    "perf-large-cold.csv",
    "perf-large-interactive.csv",
    "perf-scroll.csv"
)) {
    $source = Join-Path $build $name
    if (Test-Path -LiteralPath $source) {
        Copy-Item -LiteralPath $source -Destination $resultDir -Force
    }
}

Write-Host "Results: $resultDir"
