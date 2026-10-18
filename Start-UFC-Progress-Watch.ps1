param([string]$TaskName = 'OpenCode-UFC-Progress-5min')
$ErrorActionPreference = 'Stop'
$script = Join-Path $PSScriptRoot 'Check-UFC-Progress.py'
if (-not (Test-Path -LiteralPath $script)) { throw "Missing checker: $script" }
$python = (Get-Command py -ErrorAction Stop).Source
$arguments = '-3.13 "' + $script + '"'
$action = New-ScheduledTaskAction -Execute $python -Argument $arguments -WorkingDirectory $PSScriptRoot
$trigger = New-ScheduledTaskTrigger -Once -At ((Get-Date).AddMinutes(5)) -RepetitionInterval (New-TimeSpan -Minutes 5)
$principal = New-ScheduledTaskPrincipal -UserId ([Security.Principal.WindowsIdentity]::GetCurrent().Name) -LogonType Interactive -RunLevel Limited
$settings = New-ScheduledTaskSettingsSet -MultipleInstances IgnoreNew -ExecutionTimeLimit (New-TimeSpan -Minutes 4) -StartWhenAvailable -AllowStartIfOnBatteries -DontStopIfGoingOnBatteries
$existing = Get-ScheduledTask -TaskName $TaskName -ErrorAction SilentlyContinue
if ($existing -and ($existing.Description -ne 'Read-only free-model UFC progress check every five minutes.')) {
    throw "A different task already uses $TaskName; choose a different name."
}
Register-ScheduledTask -TaskName $TaskName -Action $action -Trigger $trigger -Principal $principal -Settings $settings -Description 'Read-only free-model UFC progress check every five minutes.' -Force | Out-Null
Start-ScheduledTask -TaskName $TaskName
Get-ScheduledTask -TaskName $TaskName | Format-List TaskName,State,Description
Get-ScheduledTaskInfo -TaskName $TaskName | Format-List LastRunTime,NextRunTime,LastTaskResult
