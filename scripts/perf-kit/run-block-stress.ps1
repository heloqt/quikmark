param(
    [int]$Samples = 5,
    [int]$TableRows = 5000,
    [int]$ParagraphWords = 1900000,
    [string]$Name = ""
)

$ErrorActionPreference = "Stop"
$root = $PSScriptRoot
$exe = Join-Path $root "liteMark.exe"
$script = Join-Path $root "scripts\block-stress-benchmark.ps1"
if (-not (Test-Path -LiteralPath $exe)) {
    throw "liteMark.exe not found next to this script."
}
if ([string]::IsNullOrWhiteSpace($Name)) {
    $Name = "block-stress-" + (Get-Date -Format "yyyyMMdd-HHmmss")
}

& $script -Exe $exe -Samples $Samples `
    -TableRows $TableRows -ParagraphWords $ParagraphWords
$succeeded = $?
if (-not $succeeded) {
    throw "Block stress benchmark failed."
}

$resultDir = Join-Path $root "results\$Name"
New-Item -ItemType Directory -Path $resultDir -Force | Out-Null
Copy-Item -Path (Join-Path $root "build\block-stress\*") `
    -Destination $resultDir -Recurse -Force
Write-Host "Results: $resultDir"
