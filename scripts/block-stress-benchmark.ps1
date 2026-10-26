param(
    [string]$Exe = "build\mingw-release-static\bin\quikmark.exe",
    [int]$Samples = 5,
    [int]$TableRows = 5000,
    [int]$ParagraphWords = 1900000
)

$ErrorActionPreference = "Stop"

if (Get-Process -Name quikmark,liteMark,litemark -ErrorAction SilentlyContinue) {
    throw "Close all running QuikMark or liteMark instances before block stress testing."
}

$projectRoot = Split-Path -Parent $PSScriptRoot
if (-not [IO.Path]::IsPathRooted($Exe)) {
    $Exe = Join-Path $projectRoot $Exe
}
if (-not (Test-Path -LiteralPath $Exe)) {
    throw "Executable not found: $Exe"
}

$outputDir = Join-Path $projectRoot "build\block-stress"
$fixtureDir = Join-Path $outputDir "fixtures"
$configDir = Join-Path $outputDir "config"
New-Item -ItemType Directory -Path $fixtureDir, $configDir -Force | Out-Null

function Read-Milliseconds([string[]]$Lines, [string]$EventName) {
    $line = $Lines |
        Where-Object { $_ -like "$EventName,*" } |
        Select-Object -Last 1
    if ($null -eq $line) {
        return -1
    }
    return [math]::Round(
        ([double]$line.Split(',')[1]) / 1000.0, 3)
}

function Get-Stats([double[]]$Values) {
    if ($Values.Count -eq 0) {
        return [pscustomobject]@{
            Samples = 0
            Min = 0
            P50 = 0
            P95 = 0
            Max = 0
        }
    }
    $sorted = @($Values | Sort-Object)
    return [pscustomobject]@{
        Samples = $sorted.Count
        Min = [math]::Round($sorted[0], 3)
        P50 = [math]::Round(
            $sorted[[math]::Floor(($sorted.Count - 1) / 2)], 3)
        P95 = [math]::Round(
            $sorted[[math]::Ceiling($sorted.Count * 0.95) - 1], 3)
        Max = [math]::Round($sorted[-1], 3)
    }
}

function Wait-TraceEvent(
    [string]$Path,
    [string]$EventName,
    [int]$TimeoutSeconds = 30
) {
    $deadline = [DateTime]::UtcNow.AddSeconds($TimeoutSeconds)
    while ([DateTime]::UtcNow -lt $deadline) {
        if (Test-Path -LiteralPath $Path) {
            if (@(Get-Content -LiteralPath $Path |
                    Where-Object { $_ -like "$EventName,*" }).Count -gt 0) {
                return
            }
        }
        Start-Sleep -Milliseconds 25
    }
    throw "Timed out waiting for $EventName in $Path"
}

function Invoke-StressCase(
    [string]$Name,
    [string]$Path,
    [int]$SampleIndex
) {
    $trace = Join-Path $outputDir "$Name-$SampleIndex-trace.csv"
    [IO.File]::WriteAllText($trace, "", [Text.UTF8Encoding]::new($false))
    $env:LITEMARK_CONFIG_DIR = $configDir
    $env:LITEMARK_PERF_TRACE = $trace

    $process = Start-Process -FilePath $Exe -ArgumentList ('"' + $Path + '"') `
        -PassThru
    try {
        Wait-TraceEvent $trace "cold_start"
        Wait-TraceEvent $trace "parse"
        $lines = Get-Content -LiteralPath $trace
        return [pscustomobject]@{
            Case = $Name
            Sample = $SampleIndex
            FileBytes = (Get-Item -LiteralPath $Path).Length
            ColdStartMs = Read-Milliseconds $lines "cold_start"
            PaintMs = Read-Milliseconds $lines "paint"
            ParseMs = Read-Milliseconds $lines "parse"
            Md4cParseMs = Read-Milliseconds $lines "md4c_parse"
            NormalizeMs = Read-Milliseconds $lines "normalize"
            LayoutInitMs = Read-Milliseconds $lines "layout_init"
            VisibleLayoutMs = Read-Milliseconds $lines "visible_layout"
            DrawMs = Read-Milliseconds $lines "draw"
        }
    } finally {
        if (-not $process.HasExited) {
            Stop-Process -Id $process.Id -Force
            Wait-Process -Id $process.Id -Timeout 5 `
                -ErrorAction SilentlyContinue
        }
    }
}

$tablePath = Join-Path $fixtureDir "oversized-table.md"
$table = [Text.StringBuilder]::new()
[void]$table.AppendLine("# Large table")
[void]$table.AppendLine()
[void]$table.AppendLine("| A | B | C |")
[void]$table.AppendLine("| --- | --- | --- |")
for ($row = 0; $row -lt $TableRows; ++$row) {
    [void]$table.AppendLine(
        "| row $row | value $row | text $row |")
}
[IO.File]::WriteAllText(
    $tablePath, $table.ToString(), [Text.UTF8Encoding]::new($false))

$paragraphPath = Join-Path $fixtureDir "single-paragraph.md"
$paragraph = [Text.StringBuilder]::new()
[void]$paragraph.AppendLine("# Single paragraph")
[void]$paragraph.AppendLine()
$wordChunk = "word "
$written = 0
while ($written -lt $ParagraphWords) {
    $count = [math]::Min(10000, $ParagraphWords - $written)
    [void]$paragraph.Append($wordChunk * $count)
    $written += $count
}
[IO.File]::WriteAllText(
    $paragraphPath, $paragraph.ToString(),
    [Text.UTF8Encoding]::new($false))

try {
    $definitions = @(
        [pscustomobject]@{
            Name = "oversized-table"
            Path = $tablePath
        },
        [pscustomobject]@{
            Name = "single-paragraph"
            Path = $paragraphPath
        }
    )
    $rawSamples = [System.Collections.ArrayList]::new()
    $summaries = [System.Collections.ArrayList]::new()
    foreach ($definition in $definitions) {
        $caseSamples = [System.Collections.ArrayList]::new()
        for ($sample = 1; $sample -le [math]::Max(1, $Samples);
             ++$sample) {
            $value = Invoke-StressCase `
                $definition.Name $definition.Path $sample
            [void]$caseSamples.Add($value)
            [void]$rawSamples.Add($value)
        }
        [void]$summaries.Add([pscustomobject]@{
            Case = $definition.Name
            FileBytes = (Get-Item -LiteralPath $definition.Path).Length
            ColdStartMs = Get-Stats @($caseSamples.ColdStartMs)
            PaintMs = Get-Stats @($caseSamples.PaintMs)
            ParseMs = Get-Stats @($caseSamples.ParseMs)
            Md4cParseMs = Get-Stats @($caseSamples.Md4cParseMs)
            NormalizeMs = Get-Stats @($caseSamples.NormalizeMs)
            LayoutInitMs = Get-Stats @($caseSamples.LayoutInitMs)
            VisibleLayoutMs = Get-Stats @($caseSamples.VisibleLayoutMs)
            DrawMs = Get-Stats @($caseSamples.DrawMs)
        })
    }
    $result = [pscustomobject]@{
        Environment = [pscustomobject]@{
            Cpu = (Get-CimInstance Win32_Processor |
                Select-Object -First 1).Name.Trim()
            OperatingSystem = (Get-CimInstance Win32_OperatingSystem).Caption
            ExeSha256 = (Get-FileHash -LiteralPath $Exe `
                -Algorithm SHA256).Hash
        }
        Cases = $summaries
        Samples = $rawSamples
    }
    $resultPath = Join-Path $outputDir "results.json"
    $result | ConvertTo-Json -Depth 5 |
        Set-Content -LiteralPath $resultPath -Encoding UTF8
    $summaries |
        Select-Object Case, FileBytes,
            @{Name="ColdP50";Expression={$_.ColdStartMs.P50}},
            @{Name="ColdP95";Expression={$_.ColdStartMs.P95}},
            @{Name="PaintP50";Expression={$_.PaintMs.P50}},
            @{Name="PaintP95";Expression={$_.PaintMs.P95}},
            @{Name="ParseP95";Expression={$_.ParseMs.P95}},
            @{Name="NormalizeP95";Expression={$_.NormalizeMs.P95}} |
        Format-Table -AutoSize
    Write-Host "Results: $resultPath"
} finally {
    Remove-Item Env:LITEMARK_PERF_TRACE -ErrorAction SilentlyContinue
    Remove-Item Env:LITEMARK_CONFIG_DIR -ErrorAction SilentlyContinue
}
