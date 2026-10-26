param(
    [int]$Samples = 20,
    [int]$LargeSamples = 5,
    [string]$Name = ""
)

$ErrorActionPreference = "Stop"
$root = $PSScriptRoot
$exe = Join-Path $root "quikmark.exe"
$script = Join-Path $root "scripts\compare-power-modes.ps1"
if ([string]::IsNullOrWhiteSpace($Name)) {
    $Name = "power-" + (Get-Date -Format "yyyyMMdd-HHmmss")
}
& $script -Samples $Samples -LargeSamples $LargeSamples `
    -Modes @("balanced", "efficiency", "performance") -OutputName $Name
if ($LASTEXITCODE -ne 0) {
    throw "Power mode comparison failed."
}

$resultDir = Join-Path $root "results\$Name"
New-Item -ItemType Directory -Path $resultDir -Force | Out-Null
$source = Join-Path $root "build\power-performance\$Name"
Copy-Item -Path (Join-Path $source "*") -Destination $resultDir `
    -Recurse -Force
Write-Host "Results: $resultDir"
