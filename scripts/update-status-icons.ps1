param(
    [Parameter(Mandatory = $true)]
    [string]$Resvg,
    [Parameter(Mandatory = $true)]
    [string]$SvgDirectory,
    [int]$Size = 128
)

$ErrorActionPreference = "Stop"
$projectRoot = (Get-Location).Path
$assetDir = Join-Path $projectRoot "assets\status-icons"
$manifest = Join-Path $assetDir "manifest.txt"

if (-not (Test-Path -LiteralPath $Resvg)) {
    throw "resvg not found: $Resvg"
}
if (-not (Test-Path -LiteralPath $SvgDirectory)) {
    throw "Twemoji SVG directory not found: $SvgDirectory"
}

$entries = Get-Content -LiteralPath $manifest
foreach ($entry in $entries) {
    if ([string]::IsNullOrWhiteSpace($entry)) {
        continue
    }
    $fields = $entry.Split('|')
    if ($fields.Count -ne 4) {
        throw "Invalid manifest entry: $entry"
    }
    $assetName = $fields[2]
    $svg = Join-Path $SvgDirectory ($assetName + ".svg")
    $png = Join-Path $assetDir ($assetName + ".png")
    if (-not (Test-Path -LiteralPath $svg)) {
        throw "Missing Twemoji SVG: $svg"
    }
    & $Resvg --width $Size --height $Size $svg $png
    if ($LASTEXITCODE -ne 0) {
        throw "resvg failed for $svg"
    }
}

Write-Host "Generated $($entries.Count) status icons at ${Size}x${Size}."
