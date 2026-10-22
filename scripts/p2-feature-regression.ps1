param(
    [string]$Exe = "D:\work\litemark\build\mingw-release-static\bin\litemark.exe"
)

$ErrorActionPreference = "Stop"

if (Get-Process litemark -ErrorAction SilentlyContinue) {
    throw "Close all running litemark instances before running this test."
}
if (-not (Test-Path -LiteralPath $Exe)) {
    throw "Executable not found: $Exe"
}

Add-Type @'
using System;
using System.Runtime.InteropServices;
public static class P2RegressionNative {
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
    [DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow();
    [DllImport("user32.dll")] public static extern bool BringWindowToTop(IntPtr hWnd);
    [DllImport("user32.dll")] public static extern IntPtr SetFocus(IntPtr hWnd);
    [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr hWnd, out uint processId);
    [DllImport("kernel32.dll")] public static extern uint GetCurrentThreadId();
    [DllImport("user32.dll")] public static extern bool AttachThreadInput(uint attach, uint attachTo, bool value);
    [DllImport("user32.dll")] public static extern void keybd_event(byte key, byte scan, uint flags, UIntPtr extra);
    [DllImport("user32.dll")] public static extern IntPtr SendMessage(IntPtr hWnd, uint message, IntPtr wParam, IntPtr lParam);
    [DllImport("user32.dll")] public static extern bool SetWindowPos(IntPtr hWnd, IntPtr after, int x, int y, int width, int height, uint flags);
    [DllImport("user32.dll")] public static extern bool GetScrollInfo(IntPtr hWnd, int bar, ref SCROLLINFO info);
    [DllImport("user32.dll")] public static extern IntPtr GetMenu(IntPtr hWnd);
    [DllImport("user32.dll")] public static extern IntPtr GetSubMenu(IntPtr menu, int position);
    [DllImport("user32.dll")] public static extern int GetMenuItemID(IntPtr menu, int position);
    [DllImport("user32.dll")] public static extern int GetMenuItemCount(IntPtr menu);
    [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern int GetMenuString(IntPtr menu, uint item, System.Text.StringBuilder text, int count, uint flags);
    [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern bool SetWindowText(IntPtr hWnd, string text);
    [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern int GetWindowText(IntPtr hWnd, System.Text.StringBuilder text, int count);
    [DllImport("user32.dll")] public static extern bool GetClientRect(IntPtr hWnd, out RECT rect);
    [DllImport("user32.dll")] public static extern IntPtr GetParent(IntPtr hWnd);
    [DllImport("user32.dll")] public static extern int GetDlgCtrlID(IntPtr hWnd);
    [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern int GetClassName(IntPtr hWnd, System.Text.StringBuilder name, int count);
    [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr hWnd);
}
'@
Add-Type -AssemblyName System.Drawing

function Wait-Title(
    [System.Diagnostics.Process]$Process,
    [string]$Pattern,
    [int]$TimeoutSeconds = 15
) {
    $deadline = [DateTime]::UtcNow.AddSeconds($TimeoutSeconds)
    while ([DateTime]::UtcNow -lt $deadline) {
        $Process.Refresh()
        if ($Process.HasExited) {
            throw "Process exited while waiting for title '$Pattern'."
        }
        if ($Process.MainWindowTitle -like $Pattern) {
            return
        }
        Start-Sleep -Milliseconds 25
    }
    throw "Timed out waiting for title '$Pattern'."
}

function Get-LargestChild([IntPtr]$Parent) {
    $items = [System.Collections.ArrayList]::new()
    [void][P2RegressionNative]::EnumChildWindows($Parent, {
        param($handle, $unused)
        $rect = [P2RegressionNative+RECT]::new()
        [void][P2RegressionNative]::GetWindowRect($handle, [ref]$rect)
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
    $info = [P2RegressionNative+SCROLLINFO]::new()
    $info.cbSize = [Runtime.InteropServices.Marshal]::SizeOf($info)
    $info.fMask = 0x17
    if (-not [P2RegressionNative]::GetScrollInfo($Window, 1, [ref]$info)) {
        throw "GetScrollInfo failed."
    }
    return $info.nPos
}

function Get-VisibleListBox([IntPtr]$Parent) {
    $items = [System.Collections.ArrayList]::new()
    [void][P2RegressionNative]::EnumChildWindows($Parent, {
        param($handle, $unused)
        if ([P2RegressionNative]::IsWindowVisible($handle)) {
            $count = [int][P2RegressionNative]::SendMessage(
                $handle, 0x82BD, [IntPtr]::Zero, [IntPtr]::Zero)
            if ($count -gt 0) {
                [void]$items.Add($handle)
            }
        }
        return $true
    }, [IntPtr]::Zero)
    return $items | Select-Object -First 1
}

function Save-WindowScreenshot([IntPtr]$Window, [string]$Path) {
    $rect = [P2RegressionNative+RECT]::new()
    [void][P2RegressionNative]::GetWindowRect($Window, [ref]$rect)
    $width = $rect.Right - $rect.Left
    $height = $rect.Bottom - $rect.Top
    $bitmap = [System.Drawing.Bitmap]::new($width, $height)
    $graphics = [System.Drawing.Graphics]::FromImage($bitmap)
    try {
        $graphics.CopyFromScreen($rect.Left, $rect.Top, 0, 0,
                                 [System.Drawing.Size]::new($width, $height))
        $bitmap.Save($Path, [System.Drawing.Imaging.ImageFormat]::Png)
    } finally {
        $graphics.Dispose()
        $bitmap.Dispose()
    }
}

function Count-ColorPixels([string]$Path, [System.Drawing.Color]$Color) {
    $bitmap = [System.Drawing.Bitmap]::new($Path)
    $count = 0
    try {
        for ($y = 0; $y -lt $bitmap.Height; $y += 2) {
            for ($x = 0; $x -lt $bitmap.Width; $x += 2) {
                $pixel = $bitmap.GetPixel($x, $y)
                if ($pixel.R -eq $Color.R -and $pixel.G -eq $Color.G -and
                    $pixel.B -eq $Color.B) {
                    ++$count
                }
            }
        }
    } finally {
        $bitmap.Dispose()
    }
    return $count
}

function Send-MenuCommand(
    [IntPtr]$Window,
    [int]$SubmenuPosition,
    [int]$ItemPosition
) {
    $menu = [P2RegressionNative]::GetMenu($Window)
    $submenu = [P2RegressionNative]::GetSubMenu($menu, $SubmenuPosition)
    $id = [P2RegressionNative]::GetMenuItemID($submenu, $ItemPosition)
    if ($id -le 0) {
        throw "Could not resolve menu command $SubmenuPosition/$ItemPosition."
    }
    [void][P2RegressionNative]::SendMessage(
        $Window, 0x0111, [IntPtr]$id, [IntPtr]::Zero)
    Start-Sleep -Milliseconds 150
}

function Get-MenuText([IntPtr]$Menu, [int]$Position) {
    $text = [Text.StringBuilder]::new(256)
    [void][P2RegressionNative]::GetMenuString(
        $Menu, [uint32]$Position, $text, 256, 0x00000400)
    return $text.ToString()
}

function Get-VisibleEditControl([IntPtr]$Parent) {
    $items = [System.Collections.ArrayList]::new()
    [void][P2RegressionNative]::EnumChildWindows($Parent, {
        param($handle, $unused)
        $name = [Text.StringBuilder]::new(128)
        [void][P2RegressionNative]::GetClassName($handle, $name, 128)
        if ($name.ToString() -eq 'Edit' -and
            [P2RegressionNative]::IsWindowVisible($handle)) {
            [void]$items.Add($handle)
        }
        return $true
    }, [IntPtr]::Zero)
    return $items | Select-Object -First 1
}

function Get-VisibleWindowTexts([IntPtr]$Parent) {
    $items = [System.Collections.ArrayList]::new()
    [void][P2RegressionNative]::EnumChildWindows($Parent, {
        param($handle, $unused)
        if ([P2RegressionNative]::IsWindowVisible($handle)) {
            $text = [Text.StringBuilder]::new(512)
            [void][P2RegressionNative]::GetWindowText($handle, $text, 512)
            if ($text.Length -gt 0) {
                [void]$items.Add($text.ToString())
            }
        }
        return $true
    }, [IntPtr]::Zero)
    return @($items)
}

function Send-CtrlKey([byte]$Key) {
    [P2RegressionNative]::keybd_event(0x11, 0, 0, [UIntPtr]::Zero)
    [P2RegressionNative]::keybd_event($Key, 0, 0, [UIntPtr]::Zero)
    [P2RegressionNative]::keybd_event($Key, 0, 0x0002, [UIntPtr]::Zero)
    [P2RegressionNative]::keybd_event(0x11, 0, 0x0002, [UIntPtr]::Zero)
}

function Focus-Window([IntPtr]$Window, [IntPtr]$FocusTarget = [IntPtr]::Zero) {
    $foreground = [P2RegressionNative]::GetForegroundWindow()
    $foregroundProcess = [uint32]0
    $foregroundThread = [P2RegressionNative]::GetWindowThreadProcessId(
        $foreground, [ref]$foregroundProcess)
    $currentThread = [P2RegressionNative]::GetCurrentThreadId()
    $attached = $false
    if ($foregroundThread -ne 0 -and $foregroundThread -ne $currentThread) {
        $attached = [P2RegressionNative]::AttachThreadInput(
            $currentThread, $foregroundThread, $true)
    }
    try {
        [void][P2RegressionNative]::BringWindowToTop($Window)
        [void][P2RegressionNative]::SetForegroundWindow($Window)
        if ($FocusTarget -ne [IntPtr]::Zero) {
            [void][P2RegressionNative]::SetFocus($FocusTarget)
        }
    } finally {
        if ($attached) {
            [void][P2RegressionNative]::AttachThreadInput(
                $currentThread, $foregroundThread, $false)
        }
    }
}

function Send-MouseDrag(
    [IntPtr]$Window,
    [int]$StartX,
    [int]$StartY,
    [int]$EndX,
    [int]$EndY
) {
    $start = [IntPtr]([int64](($StartY -band 0xffff) -shl 16) -bor
                      [int64]($StartX -band 0xffff))
    $end = [IntPtr]([int64](($EndY -band 0xffff) -shl 16) -bor
                    [int64]($EndX -band 0xffff))
    [void][P2RegressionNative]::SendMessage($Window, 0x0201, [IntPtr]1, $start)
    Start-Sleep -Milliseconds 80
    [void][P2RegressionNative]::SendMessage($Window, 0x0200, [IntPtr]1, $end)
    Start-Sleep -Milliseconds 80
    [void][P2RegressionNative]::SendMessage($Window, 0x0202, [IntPtr]0, $end)
    Start-Sleep -Milliseconds 150
}

function Send-WheelDown([IntPtr]$Window, [int]$Count) {
    $rect = [P2RegressionNative+RECT]::new()
    [void][P2RegressionNative]::GetWindowRect($Window, [ref]$rect)
    $x = $rect.Left + [int](($rect.Right - $rect.Left) / 2)
    $y = $rect.Top + [int](($rect.Bottom - $rect.Top) / 2)
    $lParam = [IntPtr]([int64](($y -band 0xffff) -shl 16) -bor
                       [int64]($x -band 0xffff))
    for ($i = 0; $i -lt $Count; ++$i) {
        [void][P2RegressionNative]::SendMessage(
            $Window, 0x020A, [IntPtr](-7864320), $lParam)
        Start-Sleep -Milliseconds 40
    }
    Start-Sleep -Milliseconds 200
}

function Get-ProjectRoot {
    return (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot "..")).Path
}

$projectRoot = Get-ProjectRoot
$fixtureDir = Join-Path $projectRoot "build\p2-regression"
$configRoot = Join-Path $projectRoot "build\p2-regression-config"
if (Test-Path -LiteralPath $configRoot) {
    $resolved = (Resolve-Path -LiteralPath $configRoot).Path
    if (-not $resolved.StartsWith($projectRoot, [StringComparison]::OrdinalIgnoreCase)) {
        throw "Refusing to remove unexpected test config path: $resolved"
    }
    Remove-Item -LiteralPath $resolved -Recurse -Force
}
New-Item -ItemType Directory -Path $fixtureDir -Force | Out-Null
New-Item -ItemType Directory -Path $configRoot -Force | Out-Null

$selectionFile = Join-Path $fixtureDir "selection.md"
$anchorFile = Join-Path $fixtureDir "anchor.md"
$selectionLines = @()
for ($i = 1; $i -le 40; ++$i) {
    $selectionLines += "Selection filler paragraph $i."
}
$selectionText = @'
# Selection Start

Alpha paragraph line one.

Beta paragraph line two.

Gamma paragraph line three.

## Selection End

Delta paragraph after the target heading.

'@ + ($selectionLines -join "`n`n")
$anchorLines = @()
for ($i = 1; $i -le 40; ++$i) {
    $anchorLines += "Filler paragraph $i."
}
$anchorText = @'
[Jump to target](#target-heading)

'@ + ($anchorLines -join "`n`n") + @'

## Target Heading

Target body.
'@
[IO.File]::WriteAllText($selectionFile, $selectionText,
                        [Text.UTF8Encoding]::new($false))
[IO.File]::WriteAllText($anchorFile, $anchorText,
                        [Text.UTF8Encoding]::new($false))

$env:LITEMARK_CONFIG_DIR = $configRoot
$selectionProcess = Start-Process -FilePath $Exe -ArgumentList $selectionFile -PassThru
try {
    Wait-Title $selectionProcess "selection.md*"
    Start-Sleep -Milliseconds 700
    $mainMenu = [P2RegressionNative]::GetMenu(
        $selectionProcess.MainWindowHandle)
    $topMenuCount = [P2RegressionNative]::GetMenuItemCount($mainMenu)
    if ($topMenuCount -ne 2) {
        throw "Expected only File and View top-level menus."
    }
    $fileMenu = [P2RegressionNative]::GetSubMenu($mainMenu, 0)
    $viewMenu = [P2RegressionNative]::GetSubMenu($mainMenu, 1)
    $fileLabels = @(for ($i = 0;
                       $i -lt [P2RegressionNative]::GetMenuItemCount($fileMenu);
                       ++$i) { Get-MenuText $fileMenu $i })
    $viewLabels = @(for ($i = 0;
                       $i -lt [P2RegressionNative]::GetMenuItemCount($viewMenu);
                       ++$i) { Get-MenuText $viewMenu $i })
    if ($fileLabels -match '关闭当前 Tab' -or
        $viewLabels -match '上一个 Tab' -or
        $viewLabels -match '下一个 Tab') {
        throw "Hidden tab menu entry is still visible."
    }
    if (-not ($viewLabels -match '查找')) {
        throw "Find menu entry is missing. View items: $($viewLabels -join ' | ')"
    }
    $document = Get-LargestChild $selectionProcess.MainWindowHandle
    Send-MouseDrag $document.Handle 35 45 520 250
    $screenshot = Join-Path $projectRoot "build\p2-selection.png"
    Save-WindowScreenshot $selectionProcess.MainWindowHandle $screenshot
    $selectionPixels = Count-ColorPixels $screenshot `
        ([System.Drawing.Color]::FromArgb(0xB6, 0xD7, 0xFF))
    [void][P2RegressionNative]::SendMessage(
        $document.Handle, 0x0301, [IntPtr]::Zero, [IntPtr]::Zero)
    Start-Sleep -Milliseconds 250
    $copied = Get-Clipboard -Raw
    if ($copied -notlike "*Selection Start*" -or
        $copied -notlike "*Selection End*" -or
        $copied -like "*# Selection Start*") {
        throw "Cross-block plain-text copy failed. Clipboard: $copied"
    }

    Focus-Window $selectionProcess.MainWindowHandle $document.Handle
    Send-CtrlKey 0x46
    Start-Sleep -Milliseconds 200
    $findEdit = Get-VisibleEditControl $selectionProcess.MainWindowHandle
    if ($findEdit -eq $null) {
        throw "Find bar did not open."
    }
    [void][P2RegressionNative]::SetWindowText($findEdit, "")
    foreach ($character in "Selection filler paragraph 35".ToCharArray()) {
        [void][P2RegressionNative]::SendMessage(
            $findEdit, 0x0102, [IntPtr][int]$character, [IntPtr]::Zero)
    }
    Start-Sleep -Milliseconds 800
    $document = Get-LargestChild $selectionProcess.MainWindowHandle
    $findScroll = Get-VerticalScroll $document.Handle
    if ($findScroll -le 0) {
        throw "Search did not scroll to the matching content."
    }
    $searchScreenshot = Join-Path $projectRoot "build\p2-search.png"
    Save-WindowScreenshot $selectionProcess.MainWindowHandle $searchScreenshot
    $searchPixels = Count-ColorPixels $searchScreenshot `
        ([System.Drawing.Color]::FromArgb(0xFF, 0xD6, 0x66))
    if ($searchPixels -lt 20) {
        $texts = Get-VisibleWindowTexts $selectionProcess.MainWindowHandle
        throw "Active search match highlight was not visible. Texts: $($texts -join ' | ')"
    }
} finally {
    if ($selectionProcess -and -not $selectionProcess.HasExited) {
        Stop-Process -Id $selectionProcess.Id -Force
    }
}

$anchorProcess = Start-Process -FilePath $Exe -ArgumentList $anchorFile -PassThru
try {
    Wait-Title $anchorProcess "anchor.md*"
    Start-Sleep -Milliseconds 700
    $document = Get-LargestChild $anchorProcess.MainWindowHandle
    Focus-Window $anchorProcess.MainWindowHandle $document.Handle
    Send-MouseDrag $document.Handle 65 52 65 52
    Start-Sleep -Milliseconds 300
    $anchorPosition = Get-VerticalScroll $document.Handle
    if ($anchorPosition -le 0) {
        throw "Internal anchor click did not scroll the document."
    }
    [void][P2RegressionNative]::SendMessage(
        $document.Handle, 0x0115, [IntPtr]6, [IntPtr]::Zero)
    Start-Sleep -Milliseconds 250
    $documentRectBefore = [P2RegressionNative+RECT]::new()
    [void][P2RegressionNative]::GetWindowRect(
        $document.Handle, [ref]$documentRectBefore)
    $documentWidthBefore =
        $documentRectBefore.Right - $documentRectBefore.Left
    Send-MenuCommand $anchorProcess.MainWindowHandle 1 4
    $outlineList = Get-VisibleListBox $anchorProcess.MainWindowHandle
    if ($outlineList -eq $null) {
        throw "Outline list was not visible after opening the drawer."
    }
    $outlineCount = [int][P2RegressionNative]::SendMessage(
        $outlineList, 0x82BD, [IntPtr]::Zero, [IntPtr]::Zero)
    if ($outlineCount -lt 1) {
        throw "Outline drawer did not contain headings."
    }
    $outlinePanel = [P2RegressionNative]::GetParent($outlineList)
    $outlineRect = [P2RegressionNative+RECT]::new()
    $mainClientRect = [P2RegressionNative+RECT]::new()
    $documentRectAfter = [P2RegressionNative+RECT]::new()
    [void][P2RegressionNative]::GetWindowRect($outlinePanel, [ref]$outlineRect)
    [void][P2RegressionNative]::GetClientRect(
        $anchorProcess.MainWindowHandle, [ref]$mainClientRect)
    [void][P2RegressionNative]::GetWindowRect(
        $document.Handle, [ref]$documentRectAfter)
    $outlineRatio =
        ($outlineRect.Right - $outlineRect.Left) /
        [double]($mainClientRect.Right - $mainClientRect.Left)
    if ($outlineRatio -lt 0.20 -or $outlineRatio -gt 0.30) {
        throw "Outline width ratio was $outlineRatio."
    }
    $documentWidthAfter =
        $documentRectAfter.Right - $documentRectAfter.Left
    if ($documentWidthAfter -ge $documentWidthBefore - 100) {
        throw "Docked outline did not reflow the document."
    }
    $outlineClick = [IntPtr]((10 -shl 16) -bor 20)
    [void][P2RegressionNative]::SendMessage(
        $outlineList, 0x0201, [IntPtr]1, $outlineClick)
    [void][P2RegressionNative]::SendMessage(
        $outlineList, 0x0202, [IntPtr]0, $outlineClick)
    Start-Sleep -Milliseconds 300
    $outlinePosition = Get-VerticalScroll $document.Handle
    if ($outlinePosition -le 0) {
        throw "Outline item click did not scroll the document."
    }
    Send-MenuCommand $anchorProcess.MainWindowHandle 1 4
    if ([P2RegressionNative]::IsWindowVisible($outlineList)) {
        throw "Outline drawer did not close."
    }
    $documentRectRestored = [P2RegressionNative+RECT]::new()
    [void][P2RegressionNative]::GetWindowRect(
        $document.Handle, [ref]$documentRectRestored)
    $documentWidthRestored =
        $documentRectRestored.Right - $documentRectRestored.Left
    if ([math]::Abs($documentWidthRestored - $documentWidthBefore) -gt 4) {
        throw "Document width was not restored after closing the outline."
    }
} finally {
    if ($anchorProcess -and -not $anchorProcess.HasExited) {
        Stop-Process -Id $anchorProcess.Id -Force
    }
}

$workspaceProcess = Start-Process -FilePath $Exe -ArgumentList $selectionFile -PassThru
try {
    Wait-Title $workspaceProcess "selection.md*"
    Start-Sleep -Milliseconds 500
    [void][P2RegressionNative]::SetWindowPos(
        $workspaceProcess.MainWindowHandle, [IntPtr]::Zero,
        80, 70, 1000, 720, 0x0014)
    Start-Sleep -Milliseconds 300
    Send-MenuCommand $workspaceProcess.MainWindowHandle 1 4
    Start-Sleep -Milliseconds 200
    if ((Get-VisibleListBox $workspaceProcess.MainWindowHandle) -eq $null) {
        throw "Outline was not persistent before saving workspace state."
    }
    $workspaceDocument = Get-LargestChild $workspaceProcess.MainWindowHandle
    Send-WheelDown $workspaceDocument.Handle 5
    $savedSelectionScroll = Get-VerticalScroll $workspaceDocument.Handle
    if ($savedSelectionScroll -le 0) {
        throw "Workspace fixture did not scroll before saving."
    }
    $secondary = Start-Process -FilePath $Exe -ArgumentList $anchorFile -PassThru -Wait
    if ($secondary.ExitCode -ne 0) {
        throw "Secondary instance failed."
    }
    Wait-Title $workspaceProcess "anchor.md*"
    Start-Sleep -Milliseconds 500
    [void][P2RegressionNative]::SendMessage(
        $workspaceProcess.MainWindowHandle, 0x0010, [IntPtr]::Zero, [IntPtr]::Zero)
    Wait-Process -Id $workspaceProcess.Id -Timeout 10
} finally {
    if ($workspaceProcess -and -not $workspaceProcess.HasExited) {
        Stop-Process -Id $workspaceProcess.Id -Force
    }
}

$workspaceFile = Join-Path $configRoot "liteMark\workspace.ini"
$workspace = Get-Content -LiteralPath $workspaceFile -Raw
if ($workspace -notmatch "tabs.count=2" -or
    $workspace -notmatch "tabs.selected=1" -or
    $workspace -notmatch "recent.count=2" -or
    $workspace -notmatch "window.width=1000" -or
    $workspace -notmatch "outline.visible=1" -or
    $workspace -notmatch "tab\.0\.scroll=([1-9][0-9]*)") {
    throw "Workspace state did not contain the expected tabs, recents, or geometry."
}

$restoredProcess = Start-Process -FilePath $Exe -PassThru
try {
    Wait-Title $restoredProcess "anchor.md*"
    Start-Sleep -Milliseconds 700
    $restoredRect = [P2RegressionNative+RECT]::new()
    [void][P2RegressionNative]::GetWindowRect(
        $restoredProcess.MainWindowHandle, [ref]$restoredRect)
    $restoredWindowWidth = $restoredRect.Right - $restoredRect.Left
    if ([math]::Abs($restoredWindowWidth - 1000) -gt 16) {
        throw "Restored window width was $restoredWindowWidth, expected 1000."
    }
    if ((Get-VisibleListBox $restoredProcess.MainWindowHandle) -eq $null) {
        throw "Outline visibility was not restored."
    }
    Focus-Window $restoredProcess.MainWindowHandle
    Send-CtrlKey 0x09
    Wait-Title $restoredProcess "selection.md*"
    Start-Sleep -Milliseconds 500
    $restoredDocument = Get-LargestChild $restoredProcess.MainWindowHandle
    $restoredSelectionScroll = Get-VerticalScroll $restoredDocument.Handle
    if ([math]::Abs($restoredSelectionScroll - $savedSelectionScroll) -gt 50) {
        throw "Restored scroll was $restoredSelectionScroll, expected $savedSelectionScroll."
    }
    Focus-Window $restoredProcess.MainWindowHandle
    Send-CtrlKey 0x57
    Wait-Title $restoredProcess "anchor.md*"
    if ($restoredProcess.HasExited) {
        throw "Ctrl+W closed the application instead of the current tab."
    }
} finally {
    if ($restoredProcess -and -not $restoredProcess.HasExited) {
        Stop-Process -Id $restoredProcess.Id -Force
    }
}

if (Test-Path -LiteralPath $configRoot) {
    $resolved = (Resolve-Path -LiteralPath $configRoot).Path
    if (-not $resolved.StartsWith(
            $projectRoot, [StringComparison]::OrdinalIgnoreCase)) {
        throw "Refusing to remove unexpected test config path: $resolved"
    }
    Remove-Item -LiteralPath $resolved -Recurse -Force
}
New-Item -ItemType Directory -Path $configRoot -Force | Out-Null
$env:LITEMARK_CONFIG_DIR = $configRoot
$largeOutlineFile = Join-Path $projectRoot "doc\03-设计文档.md"
$outlineWheelProcess = Start-Process -FilePath $Exe `
    -ArgumentList $largeOutlineFile -PassThru
try {
    Wait-Title $outlineWheelProcess "*03-设计文档.md*"
    Start-Sleep -Milliseconds 800
    Send-MenuCommand $outlineWheelProcess.MainWindowHandle 1 4
    $largeOutlineList = Get-VisibleListBox $outlineWheelProcess.MainWindowHandle
    if ($largeOutlineList -eq $null) {
        throw "Large document outline did not open."
    }
    $largeOutlineCount = [int][P2RegressionNative]::SendMessage(
        $largeOutlineList, 0x82BD, [IntPtr]::Zero, [IntPtr]::Zero)
    if ($largeOutlineCount -lt 80) {
        throw "Large document outline was unexpectedly short."
    }
    $largeDocument = Get-LargestChild $outlineWheelProcess.MainWindowHandle
    $documentScrollBefore = Get-VerticalScroll $largeDocument.Handle
    $outlineTopBefore = [int][P2RegressionNative]::SendMessage(
        $largeOutlineList, 0x82BE, [IntPtr]::Zero, [IntPtr]::Zero)
    $timer = [Diagnostics.Stopwatch]::StartNew()
    for ($i = 0; $i -lt 6; ++$i) {
        [void][P2RegressionNative]::SendMessage(
            $largeOutlineList, 0x020A, [IntPtr](-7864320), [IntPtr]::Zero)
    }
    $timer.Stop()
    $outlineTopAfter = [int][P2RegressionNative]::SendMessage(
        $largeOutlineList, 0x82BE, [IntPtr]::Zero, [IntPtr]::Zero)
    $documentScrollAfter = Get-VerticalScroll $largeDocument.Handle
    if ($outlineTopAfter -le $outlineTopBefore) {
        throw "Outline wheel did not move the outline."
    }
    if ($documentScrollAfter -ne $documentScrollBefore) {
        throw "Outline wheel leaked into the document."
    }
    if ($timer.ElapsedMilliseconds -gt 1000) {
        throw "Outline wheel processing took $($timer.ElapsedMilliseconds) ms."
    }
    $outlineWheelMilliseconds = $timer.ElapsedMilliseconds
} finally {
    if ($outlineWheelProcess -and -not $outlineWheelProcess.HasExited) {
        Stop-Process -Id $outlineWheelProcess.Id -Force
    }
    Remove-Item Env:LITEMARK_CONFIG_DIR -ErrorAction SilentlyContinue
}

[pscustomobject]@{
    CrossBlockCopy = "PASS"
    DragHighlightPixels = $selectionPixels
    SearchHighlightPixels = $searchPixels
    InternalAnchorClick = "PASS ($anchorPosition px)"
    OutlineSidebar = "PASS ($outlineCount items, $([math]::Round($outlineRatio * 100))%)"
    OutlineBodyReflow = "PASS ($documentWidthBefore -> $documentWidthAfter -> $documentWidthRestored)"
    OutlinePersistentState = "PASS"
    OutlineWheel = "PASS ($largeOutlineCount items, $outlineWheelMilliseconds ms)"
    WorkspaceTabs = "2"
    RecentFiles = "2"
    RestoredWindowWidth = $restoredWindowWidth
    RestoredScroll = $restoredSelectionScroll
    HiddenMenuShortcuts = "PASS"
    Result = "PASS"
} | Format-List
