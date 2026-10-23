param(
    [string]$Exe = "build\mingw-release-static\bin\litemark.exe",
    [string]$OutputDirectory = "dist\liteMark-perf-kit",
    [switch]$Zip
)

$ErrorActionPreference = "Stop"
$projectRoot = Split-Path -Parent $PSScriptRoot
if (-not [IO.Path]::IsPathRooted($Exe)) {
    $Exe = Join-Path $projectRoot $Exe
}
if (-not [IO.Path]::IsPathRooted($OutputDirectory)) {
    $OutputDirectory = Join-Path $projectRoot $OutputDirectory
}
if (-not (Test-Path -LiteralPath $Exe)) {
    throw "Executable not found: $Exe"
}
if (Test-Path -LiteralPath $OutputDirectory) {
    throw "Output directory already exists: $OutputDirectory"
}

New-Item -ItemType Directory -Path $OutputDirectory -Force | Out-Null
$toolDirectory = Join-Path $OutputDirectory "scripts"
New-Item -ItemType Directory -Path $toolDirectory -Force | Out-Null

Copy-Item -LiteralPath $Exe -Destination (Join-Path $OutputDirectory "liteMark.exe")
foreach ($name in @(
    "benchmark.ps1",
    "block-stress-benchmark.ps1",
    "compare-power-modes.ps1",
    "continuous-scroll-benchmark.ps1",
    "media-regression.ps1"
)) {
    Copy-Item -LiteralPath (Join-Path $PSScriptRoot $name) `
        -Destination $toolDirectory
}
foreach ($name in @(
    "README.md",
    "run-benchmark.ps1",
    "run-block-stress.ps1",
    "run-power-modes.ps1",
    "run-continuous-scroll.ps1",
    "run-media-regression.ps1"
)) {
    Copy-Item -LiteralPath (Join-Path $PSScriptRoot "perf-kit\$name") `
        -Destination $OutputDirectory
}
Copy-Item -LiteralPath (Join-Path $projectRoot "doc\11-静态EXE性能测试交接.md") `
    -Destination (Join-Path $OutputDirectory "HANDOFF.md")
$docsDirectory = Join-Path $OutputDirectory "docs"
New-Item -ItemType Directory -Path $docsDirectory -Force | Out-Null
foreach ($name in @(
    "14-启动阶段打点与复测.md",
    "15-超大单块虚拟化.md",
    "16-DocumentView拆分记录.md"
)) {
    Copy-Item -LiteralPath (Join-Path $projectRoot "doc\$name") `
        -Destination $docsDirectory
}

$hash = Get-FileHash -LiteralPath (Join-Path $OutputDirectory "liteMark.exe") `
    -Algorithm SHA256
"$($hash.Hash)  liteMark.exe" |
    Set-Content -LiteralPath (Join-Path $OutputDirectory "SHA256SUMS.txt") `
        -Encoding ASCII

if ($Zip) {
    $zipPath = "$OutputDirectory.zip"
    $compressed = $false
    for ($attempt = 1; $attempt -le 5; ++$attempt) {
        try {
            Compress-Archive -LiteralPath $OutputDirectory `
                -DestinationPath $zipPath -Force -ErrorAction Stop
            $compressed = $true
            break
        } catch {
            if ($attempt -eq 5) {
                throw
            }
            Write-Warning "Archive attempt $attempt failed; retrying after antivirus scan."
            Start-Sleep -Seconds 2
        }
    }
    if (-not $compressed) {
        throw "Failed to create performance kit archive."
    }
    $zipHash = Get-FileHash -LiteralPath $zipPath -Algorithm SHA256
    "$($zipHash.Hash)  $([IO.Path]::GetFileName($zipPath))" |
        Set-Content -LiteralPath "$zipPath.sha256" -Encoding ASCII
    Write-Host "Performance kit: $zipPath"
} else {
    Write-Host "Performance kit: $OutputDirectory"
}
