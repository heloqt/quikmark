param(
    [string]$Version = "",
    [string]$Exe = "build\mingw-release-static\bin\quikmark.exe",
    [string]$Output = ""
)

$ErrorActionPreference = "Stop"
$projectRoot = Split-Path -Parent $PSScriptRoot
$wixDirectory = Join-Path $projectRoot ".cache\wix5"
$wix = Join-Path $wixDirectory "wix.exe"

if ([string]::IsNullOrWhiteSpace($Version)) {
    $cmakeText = Get-Content -LiteralPath `
        (Join-Path $projectRoot "CMakeLists.txt") -Raw
    if ($cmakeText -match `
        'project\s*\(\s*QuikMark\s+VERSION\s+([0-9]+\.[0-9]+\.[0-9]+)') {
        $Version = $Matches[1]
    } else {
        throw "Unable to read the QuikMark version from CMakeLists.txt"
    }
}

if (-not [IO.Path]::IsPathRooted($Exe)) {
    $Exe = Join-Path $projectRoot $Exe
}
if (-not (Test-Path -LiteralPath $Exe)) {
    throw "Release executable not found: $Exe"
}

if (-not (Test-Path -LiteralPath $wix)) {
    New-Item -ItemType Directory -Path $wixDirectory -Force | Out-Null
    dotnet tool install --tool-path $wixDirectory wix --version 5.0.2
    if ($LASTEXITCODE -ne 0) {
        throw "WiX Toolset 5.0.2 installation failed."
    }
    & $wix extension add -g WixToolset.UI.wixext/5.0.2
    if ($LASTEXITCODE -ne 0) {
        throw "WiX UI extension installation failed."
    }
}

$distDir = Join-Path $projectRoot "dist"
New-Item -ItemType Directory -Path $distDir -Force | Out-Null
if ([string]::IsNullOrWhiteSpace($Output)) {
    $Output = Join-Path $distDir "quikmark-$Version-x64.msi"
} elseif (-not [IO.Path]::IsPathRooted($Output)) {
    $Output = Join-Path $projectRoot $Output
}

$usageSource = Join-Path $projectRoot "doc\12-使用说明.md"
$usageEnglishSource = Join-Path $projectRoot "doc\12-Usage.md"
$usageFile = Join-Path $projectRoot "build\QuikMark-usage-zh-CN-msi.md"
$usageEnglishFile = Join-Path $projectRoot "build\QuikMark-usage-en-msi.md"
$localeDir = Join-Path $projectRoot "build\mingw-release-static\bin\locale"
$iconFile = Join-Path $projectRoot "build\QuikMark-installer.ico"
$licenseFile = Join-Path $projectRoot "LICENSE"
$noticeFile = Join-Path $projectRoot "THIRD_PARTY_NOTICES.md"
$licenseRtf = Join-Path $projectRoot "installer\license-summary.rtf"
$source = Join-Path $projectRoot "installer\quikmark.wxs"
foreach ($required in @(
    $usageSource, $usageEnglishSource, $localeDir,
    $licenseFile, $noticeFile, $licenseRtf, $source
)) {
    if (-not (Test-Path -LiteralPath $required)) {
        throw "Required MSI input not found: $required"
    }
}
Copy-Item -LiteralPath $usageSource -Destination $usageFile -Force
Copy-Item -LiteralPath $usageEnglishSource -Destination $usageEnglishFile -Force
$usageHeading = Get-Content -LiteralPath $usageFile -TotalCount 1
$usageEnglishHeading = Get-Content -LiteralPath $usageEnglishFile -TotalCount 1
if ($usageHeading -ne "# QuikMark 使用说明") {
    throw "Unexpected Chinese usage document heading: $usageHeading"
}
if ($usageEnglishHeading -ne "# QuikMark Usage Guide") {
    throw "Unexpected English usage document heading: $usageEnglishHeading"
}
$usageHash = (Get-FileHash -LiteralPath $usageFile -Algorithm SHA256).Hash
$usageEnglishHash =
    (Get-FileHash -LiteralPath $usageEnglishFile -Algorithm SHA256).Hash
if ($usageHash -eq $usageEnglishHash) {
    throw "Chinese and English usage documents must not be identical."
}

Add-Type -AssemblyName System.Drawing
$icon = [System.Drawing.Icon]::ExtractAssociatedIcon($Exe)
if ($null -eq $icon) {
    throw "Unable to extract the application icon from: $Exe"
}
$iconStream = [IO.File]::Create($iconFile)
try {
    $icon.Save($iconStream)
} finally {
    $iconStream.Dispose()
    $icon.Dispose()
}

& $wix build `
    -arch x64 `
    -ext WixToolset.UI.wixext `
    -d "ProductVersion=$Version" `
    -d "SourceExe=$Exe" `
    -d "AppIcon=$iconFile" `
    -d "LicenseFile=$licenseFile" `
    -d "NoticeFile=$noticeFile" `
    -d "LicenseRtf=$licenseRtf" `
    -d "UsageFile=$usageFile" `
    -d "UsageEnglishFile=$usageEnglishFile" `
    -d "LocaleDir=$localeDir" `
    -pdbtype none `
    -o $Output `
    $source
if ($LASTEXITCODE -ne 0) {
    throw "WiX MSI build failed."
}

$artifact = Get-Item -LiteralPath $Output
if ($artifact.Length -ge 10MB) {
    throw "MSI exceeds 10 MB: $($artifact.Length) bytes"
}
$hash = Get-FileHash -LiteralPath $artifact.FullName `
    -Algorithm SHA256
"$($hash.Hash)  $($artifact.Name)" |
    Set-Content -LiteralPath "$($artifact.FullName).sha256" `
        -Encoding ASCII
Write-Host ("Packaged: {0} ({1:N2} MB)" -f `
    $artifact.FullName, ($artifact.Length / 1MB))
