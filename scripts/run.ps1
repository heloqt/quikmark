param(
    [ValidateSet("mingw-debug", "mingw-release", "mingw-release-static")]
    [string]$Preset = "mingw-debug",
    [string]$Document,
    [switch]$SmokeTest
)

$ErrorActionPreference = "Stop"
$toolchainBin = "D:\msys64-33\ucrt64\bin"
$exe = Join-Path (Get-Location) "build\$Preset\bin\quikmark.exe"

if (-not (Test-Path -LiteralPath $exe)) {
    throw "Executable not found. Run scripts/build.ps1 first: $exe"
}

$env:PATH = "$toolchainBin;$env:PATH"
$arguments = @()
if ($SmokeTest) {
    $arguments += "--smoke-test"
}
if ($Document) {
    $arguments += $Document
}

if ($SmokeTest) {
    $process = Start-Process -FilePath $exe -ArgumentList $arguments -Wait -PassThru -WindowStyle Hidden
    if ($process.ExitCode -ne 0) {
        throw "Smoke test failed with exit code $($process.ExitCode)"
    }
    Write-Host "Smoke test passed: $exe"
} else {
    Start-Process -FilePath $exe -ArgumentList $arguments
}
