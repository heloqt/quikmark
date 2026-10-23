param(
    [string]$Exe = "build\mingw-release-static\bin\litemark.exe"
)

$ErrorActionPreference = "Stop"

if (Get-Process litemark -ErrorAction SilentlyContinue) {
    throw "Close all running liteMark instances before media regression."
}

$projectRoot = (Get-Location).Path
if (-not [IO.Path]::IsPathRooted($Exe)) {
    $Exe = Join-Path $projectRoot $Exe
}
if (-not (Test-Path -LiteralPath $Exe)) {
    throw "Executable not found: $Exe"
}

Add-Type -AssemblyName System.Drawing
Add-Type @'
using System;
using System.Runtime.InteropServices;
public static class MediaRegressionNative {
    [StructLayout(LayoutKind.Sequential)]
    public struct RECT { public int Left; public int Top; public int Right; public int Bottom; }
    public delegate bool EnumProc(IntPtr hWnd, IntPtr lParam);
    [DllImport("user32.dll")]
    public static extern bool EnumChildWindows(IntPtr parent, EnumProc callback, IntPtr data);
    [DllImport("user32.dll")]
    public static extern bool GetWindowRect(IntPtr hWnd, out RECT rect);
    [DllImport("user32.dll")]
    public static extern IntPtr SendMessage(IntPtr hWnd, uint message, IntPtr wParam, IntPtr lParam);
}
'@

$fixtureDir = Join-Path $projectRoot "build\media-regression"
$configDir = Join-Path $projectRoot "build\media-regression-config"
New-Item -ItemType Directory -Path $fixtureDir, $configDir -Force |
    Out-Null

$imagePath = Join-Path $fixtureDir "red.png"
$bitmap = [System.Drawing.Bitmap]::new(80, 60)
$graphics = [System.Drawing.Graphics]::FromImage($bitmap)
try {
    $graphics.Clear([System.Drawing.Color]::Red)
    $bitmap.Save($imagePath, [System.Drawing.Imaging.ImageFormat]::Png)
} finally {
    $graphics.Dispose()
    $bitmap.Dispose()
}

$markdownPath = Join-Path $fixtureDir "media.md"
[IO.File]::WriteAllText($markdownPath, @'
# Media regression

![red](red.png)

```mermaid
flowchart TD
A[Start] --> B[End]
```
'@, [Text.UTF8Encoding]::new($false))

function Get-LargestChild([IntPtr]$Parent) {
    $items = [System.Collections.ArrayList]::new()
    [void][MediaRegressionNative]::EnumChildWindows($Parent, {
        param($handle, $unused)
        $rect = [MediaRegressionNative+RECT]::new()
        [void][MediaRegressionNative]::GetWindowRect($handle, [ref]$rect)
        [void]$items.Add([pscustomobject]@{
            Handle = $handle
            Area = ($rect.Right - $rect.Left) *
                   ($rect.Bottom - $rect.Top)
        })
        return $true
    }, [IntPtr]::Zero)
    return $items | Sort-Object Area -Descending |
        Select-Object -First 1
}

$env:LITEMARK_CONFIG_DIR = $configDir
$process = Start-Process -FilePath $Exe `
    -ArgumentList ('"' + $markdownPath + '"') -PassThru
try {
    $deadline = [DateTime]::UtcNow.AddSeconds(15)
    do {
        Start-Sleep -Milliseconds 100
        $process.Refresh()
    } while ($process.MainWindowHandle -eq [IntPtr]::Zero -and
             -not $process.HasExited -and [DateTime]::UtcNow -lt $deadline)
    if ($process.HasExited -or
        $process.MainWindowHandle -eq [IntPtr]::Zero) {
        throw "liteMark did not show its main window."
    }

    $document = Get-LargestChild $process.MainWindowHandle
    if ($null -eq $document) {
        throw "Document view was not found."
    }

    $images = 0
    $flowcharts = 0
    $deadline = [DateTime]::UtcNow.AddSeconds(10)
    do {
        $images = [int][MediaRegressionNative]::SendMessage(
            $document.Handle, 0x8000 + 711,
            [IntPtr]::Zero, [IntPtr]::Zero)
        $flowcharts = [int][MediaRegressionNative]::SendMessage(
            $document.Handle, 0x8000 + 712,
            [IntPtr]::Zero, [IntPtr]::Zero)
        if ($images -gt 0 -and $flowcharts -gt 0) {
            break
        }
        Start-Sleep -Milliseconds 50
    } while ([DateTime]::UtcNow -lt $deadline)

    if ($images -lt 1) {
        throw "Local image did not enter the media cache."
    }
    if ($flowcharts -lt 1) {
        throw "Mermaid flowchart did not enter the media cache."
    }

    [pscustomobject]@{
        LocalImages = $images
        Flowcharts = $flowcharts
        Result = "PASS"
    } | Format-List
} finally {
    if ($process -and -not $process.HasExited) {
        Stop-Process -Id $process.Id -Force
    }
    Remove-Item Env:LITEMARK_CONFIG_DIR -ErrorAction SilentlyContinue
}
