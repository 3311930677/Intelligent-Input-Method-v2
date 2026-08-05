# Registers the OwO core service as a scheduled task that starts at logon and
# restarts automatically on failure, then starts it now. Replaces the old
# "startup folder shortcut" approach: a shortcut could not auto-recover from
# crashes, which left users unable to type. Scheduled-task restart policy
# brings Core back within ~1 minute of a crash.
#
# ASCII-only on purpose (Windows PowerShell 5.1 decodes a BOM-less .ps1 as ANSI).
#
# Usage:
#   powershell -NoProfile -ExecutionPolicy Bypass -File install-service.ps1 `
#       -ExePath "C:\...\bin\owo_core_service.exe" `
#       -LexiconPath "C:\...\lexicon\rime-ice-cn.owolx" `
#       -BinDir "C:\...\bin"

param(
    [Parameter(Mandatory = $true)][string]$ExePath,
    [Parameter(Mandatory = $true)][string]$LexiconPath,
    [Parameter(Mandatory = $true)][string]$BinDir
)

$ErrorActionPreference = 'Stop'
$taskName = 'OwO Core Service'

if (-not (Test-Path -LiteralPath $ExePath)) { throw "Core exe not found: $ExePath" }

$taskArgs = '--lexicon "' + $LexiconPath + '"'
$action = New-ScheduledTaskAction -Execute $ExePath -Argument $taskArgs -WorkingDirectory $BinDir
$trigger = New-ScheduledTaskTrigger -AtLogon
# ExecutionTimeLimit Zero = run indefinitely; RestartCount/Interval = auto-recover
# from crashes; MultipleInstances IgnoreNew = do not launch a second Core.
$settings = New-ScheduledTaskSettingsSet `
    -AllowStartIfOnBatteries -DontStopIfGoingOnBatteries `
    -ExecutionTimeLimit ([TimeSpan]::Zero) `
    -RestartCount 3 -RestartInterval (New-TimeSpan -Minutes 1) `
    -MultipleInstances IgnoreNew
$principal = New-ScheduledTaskPrincipal `
    -UserId "$env:USERDOMAIN\$env:USERNAME" `
    -LogonType Interactive -RunLevel Limited

Register-ScheduledTask -TaskName $taskName -Action $action -Trigger $trigger `
    -Settings $settings -Principal $principal -Force | Out-Null

# Stop any Core started out-of-band, then start via the task so it lives under
# the task scheduler's restart policy.
Get-Process -Name 'owo_core_service' -ErrorAction SilentlyContinue |
    Stop-Process -Force -ErrorAction SilentlyContinue
Start-Sleep -Milliseconds 300
Start-ScheduledTask -TaskName $taskName
