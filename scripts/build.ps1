param(
    [ValidateSet("mingw-debug", "mingw-release", "mingw-release-static")]
    [string]$Preset = "mingw-debug",
    [switch]$Clean
)

$ErrorActionPreference = "Stop"
$toolchainBin = "D:\msys64-33\ucrt64\bin"
$buildDir = Join-Path (Get-Location) "build\$Preset"

if (-not (Test-Path -LiteralPath "$toolchainBin\g++.exe")) {
    throw "MinGW toolchain not found at $toolchainBin"
}
if (-not (Test-Path -LiteralPath "$toolchainBin\wx-config")) {
    throw "wxWidgets 3.3 wx-config not found at $toolchainBin"
}

$env:PATH = "$toolchainBin;$env:PATH"

if ($Clean -and (Test-Path -LiteralPath $buildDir)) {
    $resolved = (Resolve-Path -LiteralPath $buildDir).Path
    if ($resolved -ne $buildDir) {
        throw "Refusing to remove unexpected path: $resolved"
    }
    Remove-Item -LiteralPath $resolved -Recurse -Force
}

cmake --preset $Preset
if ($LASTEXITCODE -ne 0) {
    throw "CMake configure failed for preset $Preset"
}

cmake --build --preset $Preset
if ($LASTEXITCODE -ne 0) {
    throw "Build failed for preset $Preset"
}

$exe = Join-Path $buildDir "bin\litemark.exe"
if (-not (Test-Path -LiteralPath $exe)) {
    throw "Build finished but executable was not found: $exe"
}

Write-Host "Built: $exe"
