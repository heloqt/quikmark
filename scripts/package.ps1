param(
    [string]$Version = ""
)

$ErrorActionPreference = "Stop"
$projectRoot = (Get-Location).Path
if ([string]::IsNullOrWhiteSpace($Version)) {
    $cmakeText = Get-Content -LiteralPath (Join-Path $projectRoot "CMakeLists.txt") -Raw
    if ($cmakeText -match 'project\s*\(\s*QuikMark\s+VERSION\s+([0-9]+\.[0-9]+\.[0-9]+)') {
        $Version = $Matches[1]
    } else {
        throw "Unable to read the QuikMark version from CMakeLists.txt"
    }
}
$sourceExe = Join-Path $projectRoot "build\mingw-release-static\bin\quikmark.exe"
$makensis = "D:\msys64-33\ucrt64\bin\makensis.exe"
$script = Join-Path $projectRoot "installer\quikmark.nsi"
$license = Join-Path $projectRoot "LICENSE"
$notice = Join-Path $projectRoot "THIRD_PARTY_NOTICES.md"
$usageSource = Join-Path $projectRoot "doc\12-使用说明.md"
$usageFile = Join-Path $projectRoot "build\QuikMark-usage.md"
$distDir = Join-Path $projectRoot "dist"
$output = Join-Path $distDir "quikmark-$Version-setup.exe"

if (-not (Test-Path -LiteralPath $makensis)) {
    throw "makensis not found at $makensis"
}
if (-not (Test-Path -LiteralPath $sourceExe)) {
    throw "Release executable not found. Build mingw-release-static first."
}
if (-not (Test-Path -LiteralPath $license)) {
    throw "LICENSE file not found."
}
if (-not (Test-Path -LiteralPath $notice)) {
    throw "Third-party notices file not found."
}
if (-not (Test-Path -LiteralPath $usageSource)) {
    throw "Usage document not found."
}

New-Item -ItemType Directory -Path $distDir -Force | Out-Null
Copy-Item -LiteralPath $usageSource -Destination $usageFile -Force

& $makensis `
    "/DAPP_VERSION=$Version" `
    "/DSOURCE_EXE=$sourceExe" `
    "/DLICENSE_FILE=$license" `
    "/DNOTICE_FILE=$notice" `
    "/DUSAGE_FILE=$usageFile" `
    "/DOUTPUT_FILE=$output" `
    "/INPUTCHARSET" `
    "UTF8" `
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
