param(
    [ValidateSet("mingw-debug", "mingw-release", "mingw-release-static")]
    [string]$Preset = "mingw-release-static",
    [switch]$SkipBuild,
    [switch]$Integration
)

$ErrorActionPreference = "Stop"
$buildDir = Join-Path (Get-Location) "build\$Preset"

if (-not $SkipBuild) {
    powershell -NoProfile -ExecutionPolicy Bypass `
        -File .\scripts\build.ps1 -Preset $Preset
    if ($LASTEXITCODE -ne 0) {
        throw "Build failed for preset $Preset"
    }
}

ctest --test-dir $buildDir --output-on-failure -L unit
if ($LASTEXITCODE -ne 0) {
    throw "Unit tests failed"
}

if ($Integration) {
    $exe = Join-Path $buildDir "bin\litemark.exe"
    powershell -NoProfile -ExecutionPolicy Bypass `
        -File .\scripts\ui-scroll-regression.ps1 -Exe $exe
    if ($LASTEXITCODE -ne 0) {
        throw "UI integration regression failed"
    }
    powershell -NoProfile -ExecutionPolicy Bypass `
        -File .\scripts\p2-feature-regression.ps1 -Exe $exe
    if ($LASTEXITCODE -ne 0) {
        throw "P2 feature regression failed"
    }
    powershell -NoProfile -ExecutionPolicy Bypass `
        -File .\scripts\media-regression.ps1 -Exe $exe
    if ($LASTEXITCODE -ne 0) {
        throw "Media regression failed"
    }
}
