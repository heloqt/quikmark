param(
    [Parameter(Mandatory = $true)]
    [ValidateSet("stop-tencent", "stop-honor", "restore", "status")]
    [string]$Action
)

$ErrorActionPreference = "Stop"

$projectRoot = Split-Path -Parent $PSScriptRoot
$outputDir = Join-Path $projectRoot "build\security-impact"
$stateFile = Join-Path $outputDir "security-state.json"
$logFile = Join-Path $outputDir "security-control.log"
New-Item -ItemType Directory -Path $outputDir -Force | Out-Null

$tencentServices = @("QQPCRTP")
$honorServices = @(
    "QAXDefender.SDK",
    "QaxEngManager",
    "MBAMainService",
    "HnPCAIService",
    "HnPCInferEngine",
    "HnUpdateService"
)

function Write-Log([string]$Message) {
    $line = "{0:o} {1}" -f (Get-Date), $Message
    Add-Content -LiteralPath $logFile -Value $line -Encoding UTF8
    Write-Host $line
}

function Get-ServiceSnapshot {
    $result = [ordered]@{}
    foreach ($name in @($tencentServices + $honorServices)) {
        $service = Get-Service -Name $name -ErrorAction SilentlyContinue
        if ($null -ne $service) {
            $result[$name] = [ordered]@{
                DisplayName = $service.DisplayName
                Status = $service.Status.ToString()
                StartType = $service.StartType.ToString()
            }
        }
    }
    return $result
}

function Save-InitialState {
    if (-not (Test-Path -LiteralPath $stateFile)) {
        [ordered]@{
            CapturedAt = (Get-Date).ToString("o")
            Services = Get-ServiceSnapshot
        } |
            ConvertTo-Json -Depth 6 |
            Set-Content -LiteralPath $stateFile -Encoding UTF8
        Write-Log "Captured initial security service state: $stateFile"
    }
}

function Stop-ServiceIfPresent([string]$Name) {
    $service = Get-Service -Name $Name -ErrorAction SilentlyContinue
    if ($null -eq $service) {
        Write-Log "Service not present: $Name"
        return
    }

    try {
        if ($service.Status -ne "Stopped") {
            Stop-Service -Name $Name -Force -ErrorAction Stop
            Wait-ServiceState -Name $Name -Expected "Stopped" -TimeoutSeconds 12
        }
        Write-Log "Stopped service: $Name"
    } catch {
        Write-Log "WARN: Could not stop service ${Name}: $($_.Exception.Message)"
    }
}

function Wait-ServiceState(
    [string]$Name,
    [string]$Expected,
    [int]$TimeoutSeconds = 12
) {
    $deadline = (Get-Date).AddSeconds($TimeoutSeconds)
    do {
        $service = Get-Service -Name $Name -ErrorAction SilentlyContinue
        if ($null -eq $service -or $service.Status.ToString() -eq $Expected) {
            return
        }
        Start-Sleep -Milliseconds 200
    } while ((Get-Date) -lt $deadline)

    throw "Timed out waiting for $Name to become $Expected"
}

function Stop-MatchingProcesses([scriptblock]$Predicate, [string]$Label) {
    $processes = @(
        Get-CimInstance Win32_Process -ErrorAction SilentlyContinue |
            Where-Object {
                & $Predicate $_
            } |
            Sort-Object ProcessId
    )
    foreach ($process in $processes) {
        try {
            Stop-Process -Id $process.ProcessId -Force -ErrorAction Stop
            Write-Log "Stopped $Label process: $($process.Name) [$($process.ProcessId)]"
        } catch {
            Write-Log "WARN: Could not stop $Label process $($process.Name) [$($process.ProcessId)]: $($_.Exception.Message)"
        }
    }
}

function Stop-Tencent {
    Save-InitialState
    Write-Log "Stopping Tencent PC Manager real-time protection."
    foreach ($name in $tencentServices) {
        Stop-ServiceIfPresent $name
    }
    Start-Sleep -Milliseconds 800
    Stop-MatchingProcesses {
        param($process)
        $path = [string]$process.ExecutablePath
        $name = [string]$process.Name
        return (
            $path -like "*\Tencent\QQPCMgr\*" -or
            $name -match "^(QQPCMgr|QQPCTray|QQPCRtp|qmlauncher|qmbsrv).*"
        )
    } "Tencent"
}

function Stop-Honor {
    Save-InitialState
    Write-Log "Stopping HONOR PC Manager and QAX services."
    foreach ($name in $honorServices) {
        Stop-ServiceIfPresent $name
    }
    Start-Sleep -Milliseconds 800
    Stop-MatchingProcesses {
        param($process)
        $path = [string]$process.ExecutablePath
        $name = [string]$process.Name
        return (
            $path -like "*\HONOR\*" -or
            $path -like "*\qianxin\*" -or
            $name -match "^(HnPC|HNPC|MBAMain|MBA[A-Za-z]|qaxdefender|QaxEng).*"
        )
    } "HONOR/QAX"
}

function Restore-Services {
    if (-not (Test-Path -LiteralPath $stateFile)) {
        Write-Log "No saved security state; nothing to restore."
        return
    }

    $state = Get-Content -LiteralPath $stateFile -Raw | ConvertFrom-Json
    foreach ($property in $state.Services.PSObject.Properties) {
        $name = $property.Name
        $initialStatus = [string]$property.Value.Status
        if ($initialStatus -ne "Running") {
            continue
        }
        try {
            $service = Get-Service -Name $name -ErrorAction SilentlyContinue
            if ($null -eq $service) {
                Write-Log "WARN: Service no longer exists: $name"
                continue
            }
            if ($service.Status -ne "Running") {
                Start-Service -Name $name -ErrorAction Stop
                Wait-ServiceState -Name $name -Expected "Running" -TimeoutSeconds 20
            }
            Write-Log "Restored service: $name"
        } catch {
            Write-Log "WARN: Could not restore service ${name}: $($_.Exception.Message)"
        }
    }
}

function Write-Status {
    $snapshot = Get-ServiceSnapshot
    $defender = Get-MpComputerStatus -ErrorAction SilentlyContinue |
        Select-Object AMRunningMode, RealTimeProtectionEnabled,
            BehaviorMonitorEnabled, IoavProtectionEnabled,
            OnAccessProtectionEnabled
    [ordered]@{
        CapturedAt = (Get-Date).ToString("o")
        Services = $snapshot
        Defender = $defender
    } |
        ConvertTo-Json -Depth 6 |
        Set-Content -LiteralPath (Join-Path $outputDir "security-status-$(Get-Date -Format 'yyyyMMdd-HHmmss').json") `
            -Encoding UTF8
    $snapshot | ConvertTo-Json -Depth 6
    $defender | Format-List
}

Write-Log "Action: $Action; elevated: $((New-Object Security.Principal.WindowsPrincipal([Security.Principal.WindowsIdentity]::GetCurrent())).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator))"

switch ($Action) {
    "stop-tencent" { Stop-Tencent }
    "stop-honor" { Stop-Honor }
    "restore" { Restore-Services }
    "status" { Write-Status }
}

Write-Status
