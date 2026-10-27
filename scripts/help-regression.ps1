param(
    [string]$Exe = "build\mingw-release-static\bin\quikmark.exe"
)

$ErrorActionPreference = "Stop"
$projectRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot "..")).Path
if (-not [IO.Path]::IsPathRooted($Exe)) {
    $Exe = Join-Path $projectRoot $Exe
}
if (-not (Test-Path -LiteralPath $Exe)) {
    throw "Executable not found: $Exe"
}
if (Get-Process -Name quikmark,liteMark,litemark -ErrorAction SilentlyContinue) {
    throw "Close all running QuikMark or liteMark instances first."
}

Add-Type @'
using System;
using System.Runtime.InteropServices;
using System.Text;
public static class HelpRegressionNative {
    public delegate bool EnumProc(IntPtr hWnd, IntPtr lParam);
    [DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc callback, IntPtr data);
    [DllImport("user32.dll")] public static extern bool EnumChildWindows(IntPtr parent, EnumProc callback, IntPtr data);
    [DllImport("user32.dll")] public static extern IntPtr GetMenu(IntPtr hWnd);
    [DllImport("user32.dll")] public static extern IntPtr GetSubMenu(IntPtr menu, int position);
    [DllImport("user32.dll")] public static extern int GetMenuItemID(IntPtr menu, int position);
    [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern int GetMenuString(IntPtr menu, uint item, StringBuilder text, int count, uint flags);
    [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern int GetWindowText(IntPtr hWnd, StringBuilder text, int count);
    [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern int GetClassName(IntPtr hWnd, StringBuilder text, int count);
    [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr hWnd, out uint processId);
    [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr hWnd);
    [DllImport("user32.dll")] public static extern IntPtr SendMessage(IntPtr hWnd, uint message, IntPtr wParam, IntPtr lParam);
    [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr hWnd, uint message, IntPtr wParam, IntPtr lParam);
    [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr hWnd);
}
'@

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

function Get-ProcessWindowText([int]$ProcessId) {
    $items = [System.Collections.ArrayList]::new()
    [void][HelpRegressionNative]::EnumWindows({
        param($handle, $unused)
        if (-not [HelpRegressionNative]::IsWindowVisible($handle)) {
            return $true
        }
        $windowProcess = [uint32]0
        [void][HelpRegressionNative]::GetWindowThreadProcessId(
            $handle, [ref]$windowProcess)
        if ($windowProcess -eq $ProcessId) {
            $title = [Text.StringBuilder]::new(256)
            $class = [Text.StringBuilder]::new(128)
            [void][HelpRegressionNative]::GetWindowText(
                $handle, $title, $title.Capacity)
            [void][HelpRegressionNative]::GetClassName(
                $handle, $class, $class.Capacity)
            [void]$items.Add(
                "$($class.ToString()): $($title.ToString())")
        }
        return $true
    }, [IntPtr]::Zero)
    return @($items)
}

function Get-ProcessWindows([int]$ProcessId) {
    $items = [System.Collections.ArrayList]::new()
    [void][HelpRegressionNative]::EnumWindows({
        param($handle, $unused)
        if (-not [HelpRegressionNative]::IsWindowVisible($handle)) {
            return $true
        }
        $windowProcess = [uint32]0
        [void][HelpRegressionNative]::GetWindowThreadProcessId(
            $handle, [ref]$windowProcess)
        if ($windowProcess -eq $ProcessId) {
            $title = [Text.StringBuilder]::new(256)
            $class = [Text.StringBuilder]::new(128)
            [void][HelpRegressionNative]::GetWindowText(
                $handle, $title, $title.Capacity)
            [void][HelpRegressionNative]::GetClassName(
                $handle, $class, $class.Capacity)
            [void]$items.Add([pscustomobject]@{
                Handle = $handle
                Class = $class.ToString()
                Text = $title.ToString()
            })
        }
        return $true
    }, [IntPtr]::Zero)
    return @($items)
}

function Get-VisibleChildText([IntPtr]$Parent) {
    $items = [System.Collections.ArrayList]::new()
    [void][HelpRegressionNative]::EnumChildWindows($Parent, {
        param($handle, $unused)
        if ([HelpRegressionNative]::IsWindowVisible($handle)) {
            $text = [Text.StringBuilder]::new(256)
            [void][HelpRegressionNative]::GetWindowText(
                $handle, $text, $text.Capacity)
            if (-not [string]::IsNullOrWhiteSpace($text.ToString())) {
                [void]$items.Add($text.ToString())
            }
        }
        return $true
    }, [IntPtr]::Zero)
    return @($items)
}

function Reset-TestDirectory([string]$Path) {
    if (Test-Path -LiteralPath $Path) {
        $resolved = (Resolve-Path -LiteralPath $Path).Path
        if (-not $resolved.StartsWith(
                $projectRoot, [StringComparison]::OrdinalIgnoreCase)) {
            throw "Refusing to remove unexpected path: $resolved"
        }
        Remove-Item -LiteralPath $resolved -Recurse -Force
    }
    New-Item -ItemType Directory -Path $Path -Force | Out-Null
}

$configDir = Join-Path $projectRoot "build\help-regression-config"
Reset-TestDirectory $configDir
$oldQuikmarkConfig = $env:QUIKMARK_CONFIG_DIR
$oldLitemarkConfig = $env:LITEMARK_CONFIG_DIR
$env:QUIKMARK_CONFIG_DIR = $configDir
$env:LITEMARK_CONFIG_DIR = $configDir
$process = $null
try {
    $process = Start-Process -FilePath $Exe -PassThru
    Wait-Title $process "*QuikMark*"
    Start-Sleep -Milliseconds 500
    [void][HelpRegressionNative]::SetForegroundWindow(
        $process.MainWindowHandle)

    $mainMenu = [HelpRegressionNative]::GetMenu(
        $process.MainWindowHandle)
    $initialTopMenu = [Text.StringBuilder]::new(128)
    [void][HelpRegressionNative]::GetMenuString(
        $mainMenu, 0, $initialTopMenu, $initialTopMenu.Capacity, 0x0400)
    $systemIsChinese = $initialTopMenu.ToString() -eq "文件"
    $initialHelpName = if ($systemIsChinese) { "使用说明.md" } else { "Usage.md" }
    $switchedHelpName = if ($systemIsChinese) { "Usage.md" } else { "使用说明.md" }
    $languageItemPosition = if ($systemIsChinese) { 1 } else { 2 }
    $languagePreference = if ($systemIsChinese) { "language=en" } else { "language=zh_CN" }
    $expectedTopMenu = if ($systemIsChinese) { "File" } else { "文件" }

    $helpMenu = [HelpRegressionNative]::GetSubMenu($mainMenu, 2)
    $usageCommand = [HelpRegressionNative]::GetMenuItemID($helpMenu, 0)
    if ($usageCommand -le 0) {
        throw "Usage menu command was not found."
    }
    [void][HelpRegressionNative]::SendMessage(
        $process.MainWindowHandle, 0x0111,
        [IntPtr]$usageCommand, [IntPtr]::Zero)
    Wait-Title $process "*$initialHelpName*"
    Start-Sleep -Milliseconds 500

    $mainMenu = [HelpRegressionNative]::GetMenu(
        $process.MainWindowHandle)
    $viewMenu = [HelpRegressionNative]::GetSubMenu($mainMenu, 1)
    $findCommand = [HelpRegressionNative]::GetMenuItemID($viewMenu, 0)
    [void][HelpRegressionNative]::SendMessage(
        $process.MainWindowHandle, 0x0111,
        [IntPtr]$findCommand, [IntPtr]::Zero)
    Start-Sleep -Milliseconds 300
    $findTexts = Get-VisibleChildText $process.MainWindowHandle
    if ($findTexts -notcontains "查找" -and
        $findTexts -notcontains "Find") {
        throw "Initial find bar caption was not localized."
    }

    $mainMenu = [HelpRegressionNative]::GetMenu(
        $process.MainWindowHandle)
    $viewMenu = [HelpRegressionNative]::GetSubMenu($mainMenu, 1)
    $languageMenu =
        [HelpRegressionNative]::GetSubMenu($viewMenu, 3)
    $languageCommand =
        [HelpRegressionNative]::GetMenuItemID(
            $languageMenu, $languageItemPosition)
    if ($languageCommand -le 0) {
        throw "Language menu command was not found."
    }
    [void][HelpRegressionNative]::SendMessage(
        $process.MainWindowHandle, 0x0111,
        [IntPtr]$languageCommand, [IntPtr]::Zero)
    Start-Sleep -Milliseconds 700
    $process.Refresh()
    if ($process.HasExited) {
        throw "QuikMark exited after language switch with code $($process.ExitCode)."
    }

    $mainMenu = [HelpRegressionNative]::GetMenu(
        $process.MainWindowHandle)
    $topMenuText = [Text.StringBuilder]::new(128)
    [void][HelpRegressionNative]::GetMenuString(
        $mainMenu, 0, $topMenuText, $topMenuText.Capacity, 0x0400)
    if ($topMenuText.ToString() -ne $expectedTopMenu) {
        throw "Menu language did not switch to '$expectedTopMenu'. Got '$topMenuText'."
    }
    $englishFindTexts = Get-VisibleChildText $process.MainWindowHandle
    if ($systemIsChinese -and
        ($englishFindTexts -notcontains "Find" -or
         $englishFindTexts -notcontains "Type to search")) {
        throw "Find bar did not refresh to English."
    }
    if (-not $systemIsChinese -and
        ($englishFindTexts -notcontains "查找" -or
         $englishFindTexts -notcontains "输入内容后开始查找")) {
        throw "Find bar did not refresh to Simplified Chinese."
    }

    $helpMenu = [HelpRegressionNative]::GetSubMenu($mainMenu, 2)
    $aboutCommand = [HelpRegressionNative]::GetMenuItemID($helpMenu, 2)
    [void][HelpRegressionNative]::PostMessage(
        $process.MainWindowHandle, 0x0111,
        [IntPtr]$aboutCommand, [IntPtr]::Zero)
    Start-Sleep -Milliseconds 500
    $aboutTitle = if ($systemIsChinese) {
        "About QuikMark"
    } else {
        "关于 QuikMark"
    }
    $aboutButtonText = if ($systemIsChinese) { "OK" } else { "确定" }
    $aboutWindow = Get-ProcessWindows $process.Id |
        Where-Object { $_.Text -like "*$aboutTitle*" } |
        Select-Object -First 1
    if ($aboutWindow -eq $null) {
        throw "Localized About dialog did not open."
    }
    $aboutChildren = Get-VisibleChildText $aboutWindow.Handle
    if ($aboutChildren -notcontains $aboutButtonText) {
        throw "Localized About button was not found. Text: $($aboutChildren -join ' | ')"
    }
    [void][HelpRegressionNative]::SendMessage(
        $aboutWindow.Handle, 0x0010, [IntPtr]::Zero, [IntPtr]::Zero)
    Start-Sleep -Milliseconds 250

    $usageCommand = [HelpRegressionNative]::GetMenuItemID($helpMenu, 0)
    $helpItemText = [Text.StringBuilder]::new(128)
    [void][HelpRegressionNative]::GetMenuString(
        $helpMenu, 0, $helpItemText, $helpItemText.Capacity, 0x0400)
    if ($usageCommand -le 0 -or
        $helpItemText.ToString() -notmatch "Usage Guide") {
        throw "English Usage Guide menu item was not rebuilt correctly."
    }
    [void][HelpRegressionNative]::SendMessage(
        $process.MainWindowHandle, 0x0111,
        [IntPtr]$usageCommand, [IntPtr]::Zero)
    Start-Sleep -Milliseconds 800
    $process.Refresh()
    if ($process.MainWindowTitle -notlike "*$switchedHelpName*") {
        $windows = Get-ProcessWindowText $process.Id
        throw "Switched help document did not open. Windows: $($windows -join ' | ')"
    }
    Start-Sleep -Milliseconds 500

    if ((Get-Process -Name quikmark,liteMark,litemark `
            -ErrorAction SilentlyContinue).Count -ne 1) {
        throw "Opening help started an additional QuikMark instance."
    }
    if (-not $process.CloseMainWindow()) {
        throw "QuikMark did not accept a normal window close."
    }
    if (-not $process.WaitForExit(5000)) {
        throw "QuikMark did not exit after closing the test window."
    }

    $configDataDir = Join-Path $configDir "QuikMark"
    $settingsPath = Join-Path $configDataDir "settings.ini"
    if (-not (Test-Path -LiteralPath $settingsPath)) {
        throw "Language preference was not saved."
    }
    $settings = Get-Content -LiteralPath $settingsPath -Raw
    if ($settings -notmatch [regex]::Escape($languagePreference)) {
        throw "Language preference '$languagePreference' was not saved."
    }

    $workspacePath = Join-Path $configDataDir "workspace.ini"
    if (Test-Path -LiteralPath $workspacePath) {
        $workspace = Get-Content -LiteralPath $workspacePath -Raw
        if ($workspace -match "recent\.count=([1-9][0-9]*)") {
            throw "Opening help should not add it to recent documents."
        }
    }

    $process = Start-Process -FilePath $Exe -PassThru
    Wait-Title $process "*QuikMark*"
    Start-Sleep -Milliseconds 500
    $mainMenu = [HelpRegressionNative]::GetMenu(
        $process.MainWindowHandle)
    $topMenuText = [Text.StringBuilder]::new(128)
    [void][HelpRegressionNative]::GetMenuString(
        $mainMenu, 0, $topMenuText, $topMenuText.Capacity, 0x0400)
    if ($topMenuText.ToString() -ne $expectedTopMenu) {
        throw "Persisted menu language was not restored. Got '$topMenuText'."
    }
    if (-not $process.CloseMainWindow()) {
        throw "Restarted QuikMark did not accept a normal close."
    }
    if (-not $process.WaitForExit(5000)) {
        throw "Restarted QuikMark did not exit."
    }

    [pscustomobject]@{
        InitialHelpTab = $initialHelpName
        SwitchedHelpTab = $switchedHelpName
        MenuLanguage = $expectedTopMenu
        AboutDialog = "PASS"
        LanguagePersistence = "PASS"
        SingleInstance = "PASS"
        RecentDocuments = "PASS"
        Result = "PASS"
    } | Format-List
} finally {
    if ($process -and -not $process.HasExited) {
        Stop-Process -Id $process.Id -Force
    }
    $env:QUIKMARK_CONFIG_DIR = $oldQuikmarkConfig
    $env:LITEMARK_CONFIG_DIR = $oldLitemarkConfig
}
