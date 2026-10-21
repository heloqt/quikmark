param(
    [string]$Version = "0.1.0"
)

$ErrorActionPreference = "Stop"
$projectRoot = (Get-Location).Path
$sourceExe = Join-Path $projectRoot "build\mingw-release-static\bin\litemark.exe"
$makensis = "D:\msys64-33\ucrt64\bin\makensis.exe"
$script = Join-Path $projectRoot "installer\litemark.nsi"
$license = Join-Path $projectRoot "LICENSE"
$distDir = Join-Path $projectRoot "dist"
$output = Join-Path $distDir "liteMark-$Version-setup.exe"

if (-not (Test-Path -LiteralPath $makensis)) {
    throw "makensis not found at $makensis"
}
if (-not (Test-Path -LiteralPath $sourceExe)) {
    throw "Release executable not found. Build mingw-release-static first."
}
if (-not (Test-Path -LiteralPath $license)) {
    throw "LICENSE file not found."
}

New-Item -ItemType Directory -Path $distDir -Force | Out-Null

& $makensis `
    "/DAPP_VERSION=$Version" `
    "/DSOURCE_EXE=$sourceExe" `
    "/DLICENSE_FILE=$license" `
    "/DOUTPUT_FILE=$output" `
    $script
if ($LASTEXITCODE -ne 0) {
    throw "NSIS packaging failed"
}

$artifact = Get-Item -LiteralPath $output
$limit = 10MB
if ($artifact.Length -ge $limit) {
    throw "Installer exceeds 10 MB: $($artifact.Length) bytes"
}

$hash = Get-FileHash -LiteralPath $output -Algorithm SHA256
"$($hash.Hash)  $($artifact.Name)" |
    Set-Content -LiteralPath (Join-Path $distDir "SHA256.txt") -Encoding ASCII

Write-Host ("Packaged: {0} ({1:N2} MB)" -f $artifact.FullName, ($artifact.Length / 1MB))
