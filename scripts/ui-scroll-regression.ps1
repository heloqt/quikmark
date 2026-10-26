param(
    [string]$Exe = "D:\work\litemark\build\mingw-release-static\bin\quikmark.exe"
)

$ErrorActionPreference = "Stop"

if (Get-Process -Name quikmark,liteMark,litemark -ErrorAction SilentlyContinue) {
    throw "Close all running QuikMark or liteMark instances before running this test."
}
if (-not (Test-Path -LiteralPath $Exe)) {
    throw "Executable not found: $Exe"
}

Add-Type @'
using System;
using System.Runtime.InteropServices;
public static class ScrollRegressionNative {
    [StructLayout(LayoutKind.Sequential)]
    public struct RECT { public int Left; public int Top; public int Right; public int Bottom; }
    [StructLayout(LayoutKind.Sequential)]
    public struct SCROLLINFO {
        public uint cbSize;
        public uint fMask;
        public int nMin;
        public int nMax;
        public uint nPage;
        public int nPos;
        public int nTrackPos;
    }
    public delegate bool EnumProc(IntPtr hWnd, IntPtr lParam);
    [DllImport("user32.dll")] public static extern bool EnumChildWindows(IntPtr parent, EnumProc callback, IntPtr data);
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr hWnd, out RECT rect);
    [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr hWnd);
    [DllImport("user32.dll")] public static extern void keybd_event(byte key, byte scan, uint flags, UIntPtr extra);
    [DllImport("user32.dll")] public static extern IntPtr SendMessage(IntPtr hWnd, uint message, IntPtr wParam, IntPtr lParam);
    [DllImport("user32.dll")] public static extern bool GetScrollInfo(IntPtr hWnd, int bar, ref SCROLLINFO info);
}
'@

function Wait-Title(
    [System.Diagnostics.Process]$Process,
    [string]$Pattern,
    [int]$TimeoutSeconds = 15
) {
    $deadline = [DateTime]::UtcNow.AddSeconds($TimeoutSeconds)
    while ([DateTime]::UtcNow -lt $deadline) {
        $live = Get-Process -Id $Process.Id -ErrorAction SilentlyContinue
        if ($null -eq $live) {
            throw "Process exited while waiting for title '$Pattern'."
        }
        $live.Refresh()
        if ($live.MainWindowTitle -like $Pattern) {
            return
        }
        Start-Sleep -Milliseconds 25
    }
    $live = Get-Process -Id $Process.Id -ErrorAction SilentlyContinue
    $actual = if ($null -eq $live) {
        "<exited>"
    } else {
        "title='$($live.MainWindowTitle)' hwnd=$($live.MainWindowHandle)"
    }
    throw "Timed out waiting for title '$Pattern'; $actual."
}

function Get-LargestChild([IntPtr]$Parent) {
    $items = [System.Collections.ArrayList]::new()
    [void][ScrollRegressionNative]::EnumChildWindows($Parent, {
        param($handle, $unused)
        $rect = [ScrollRegressionNative+RECT]::new()
        [void][ScrollRegressionNative]::GetWindowRect($handle, [ref]$rect)
        $area = ($rect.Right - $rect.Left) * ($rect.Bottom - $rect.Top)
        [void]$items.Add([pscustomobject]@{
            Handle = $handle
            Rect = $rect
            Area = $area
        })
        return $true
    }, [IntPtr]::Zero)
    return $items | Sort-Object Area -Descending | Select-Object -First 1
}

function Get-VerticalScroll([IntPtr]$Window) {
    $info = [ScrollRegressionNative+SCROLLINFO]::new()
    $info.cbSize = [Runtime.InteropServices.Marshal]::SizeOf($info)
    $info.fMask = 0x17  # SIF_RANGE | SIF_PAGE | SIF_POS | SIF_TRACKPOS
    if (-not [ScrollRegressionNative]::GetScrollInfo($Window, 1, [ref]$info)) {
        throw "GetScrollInfo failed for window $Window."
    }
    return $info.nPos
}

function Send-WheelDown([IntPtr]$Window, [int]$Count) {
    $rect = [ScrollRegressionNative+RECT]::new()
    [void][ScrollRegressionNative]::GetWindowRect($Window, [ref]$rect)
    $x = $rect.Left + [int](($rect.Right - $rect.Left) / 2)
    $y = $rect.Top + [int](($rect.Bottom - $rect.Top) / 2)
    $lParam = [IntPtr]([uint32](($y -band 0xffff) -shl 16) -bor
                       [uint32]($x -band 0xffff))
    $wParam = [IntPtr](-7864320)
    for ($i = 0; $i -lt $Count; ++$i) {
        [void][ScrollRegressionNative]::SendMessage(
            $Window, 0x020A, $wParam, $lParam)
        Start-Sleep -Milliseconds 40
    }
}

function Send-NextTabCommand([IntPtr]$Window) {
    [void][ScrollRegressionNative]::SendMessage(
        $Window, 0x0111, [IntPtr]6009, [IntPtr]::Zero)
}

$projectRoot = (Get-Location).Path
$fixtureDir = Join-Path $projectRoot "build\scroll-regression"
$configRoot = Join-Path $projectRoot "build\scroll-regression-config"
$traceFile = Join-Path $projectRoot "build\scroll-regression.csv"
if (Test-Path -LiteralPath $configRoot) {
    $resolved = (Resolve-Path -LiteralPath $configRoot).Path
    if (-not $resolved.StartsWith(
            $projectRoot, [StringComparison]::OrdinalIgnoreCase)) {
        throw "Refusing to remove unexpected test config path: $resolved"
    }
    Remove-Item -LiteralPath $resolved -Recurse -Force
}
New-Item -ItemType Directory -Path $fixtureDir -Force | Out-Null
New-Item -ItemType Directory -Path $configRoot -Force | Out-Null

$chunk = @'
# Scroll regression

This document is intentionally longer than one viewport.

- alpha
- beta
- gamma

## A section

Paragraph text with 中文内容 for layout measurement.

> quoted line

```text
sample block
```

'@
$builder = [Text.StringBuilder]::new()
while ($builder.Length -lt 180 * 1024) {
    [void]$builder.Append($chunk)
}
$firstFile = Join-Path $fixtureDir "first.md"
$secondFile = Join-Path $fixtureDir "second.md"
[IO.File]::WriteAllText($firstFile, $builder.ToString(),
                        [Text.UTF8Encoding]::new($false))
[IO.File]::WriteAllText($secondFile, $builder.ToString(),
                        [Text.UTF8Encoding]::new($false))

[IO.File]::WriteAllText($traceFile, "", [Text.UTF8Encoding]::new($false))
$env:LITEMARK_PERF_TRACE = $traceFile
$env:LITEMARK_CONFIG_DIR = $configRoot
$primary = Start-Process -FilePath $Exe -ArgumentList $firstFile -PassThru
try {
    Wait-Title $primary "first.md*"
    Start-Sleep -Milliseconds 500
    $document = Get-LargestChild $primary.MainWindowHandle
    Send-WheelDown $document.Handle 6
    Start-Sleep -Milliseconds 250
    $savedPosition = Get-VerticalScroll $document.Handle
    if ($savedPosition -le 0) {
        throw "The first document did not scroll; position=$savedPosition."
    }

    $secondary = Start-Process -FilePath $Exe `
        -ArgumentList $secondFile -PassThru -Wait
    if ($secondary.ExitCode -ne 0) {
        throw "Second instance failed with exit code $($secondary.ExitCode)."
    }
    Wait-Title $primary "second.md*"
    Start-Sleep -Milliseconds 500
    $document = Get-LargestChild $primary.MainWindowHandle
    $newDocumentPosition = Get-VerticalScroll $document.Handle
    if ($newDocumentPosition -ne 0) {
        throw "New document inherited scroll position $newDocumentPosition."
    }

    [void][ScrollRegressionNative]::SetForegroundWindow(
        $primary.MainWindowHandle)
    Send-NextTabCommand $primary.MainWindowHandle
    Wait-Title $primary "first.md*"
    Start-Sleep -Milliseconds 300
    $document = Get-LargestChild $primary.MainWindowHandle
    $restoredPosition = Get-VerticalScroll $document.Handle
    if ([math]::Abs($restoredPosition - $savedPosition) -gt 2) {
        throw "Old tab restored $restoredPosition instead of $savedPosition."
    }

    [pscustomobject]@{
        FirstDocumentScroll = $savedPosition
        NewDocumentScroll = $newDocumentPosition
        RestoredDocumentScroll = $restoredPosition
        Result = "PASS"
    } | Format-List
} finally {
    if ($primary -and -not $primary.HasExited) {
        Stop-Process -Id $primary.Id -Force
        Wait-Process -Id $primary.Id -Timeout 5 -ErrorAction SilentlyContinue
    }
    Remove-Item Env:LITEMARK_PERF_TRACE -ErrorAction SilentlyContinue
    Remove-Item Env:LITEMARK_CONFIG_DIR -ErrorAction SilentlyContinue
}
