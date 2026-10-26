param(
    [string]$Name = ""
)

$ErrorActionPreference = "Stop"
$root = $PSScriptRoot
$exe = Join-Path $root "quikmark.exe"
$script = Join-Path $root "scripts\media-regression.ps1"
if (-not (Test-Path -LiteralPath $exe)) {
    throw "quikmark.exe not found next to this script."
}
if ([string]::IsNullOrWhiteSpace($Name)) {
    $Name = "media-" + (Get-Date -Format "yyyyMMdd-HHmmss")
}

& $script -Exe $exe
$succeeded = $?
if (-not $succeeded) {
    throw "Media regression failed."
}

$resultDir = Join-Path $root "results\$Name"
New-Item -ItemType Directory -Path $resultDir -Force | Out-Null
Copy-Item -Path (Join-Path $root "build\media-regression\*") `
    -Destination $resultDir -Recurse -Force
Write-Host "Results: $resultDir"
