param(
    [ValidateRange(1, 60)][int]$RefreshSeconds = 3,
    [switch]$Once
)
$root = $PSScriptRoot
$stateFile = Join-Path $root '_Build\agent-loop\state.json'
function ShortText([string]$text, [int]$length = 160) {
    $text = ($text -replace '[\r\n]+', ' ').Trim()
    if ($text.Length -gt $length) { return $text.Substring(0, $length) + '...' }
    return $text
}
do {
    if (-not $Once) { Clear-Host }
    Write-Host 'KYTYPS5 / UFC 5 - LIVE DEVELOPMENT PROGRESS' -ForegroundColor Cyan
    Write-Host "Updated: $(Get-Date -Format 'yyyy-MM-dd HH:mm:ss') | Ctrl+C closes this viewer only"
    Write-Host ''
    try {
        $state = Get-Content -LiteralPath $stateFile -Raw -ErrorAction Stop | ConvertFrom-Json
        $supervisor = Get-Process -Id $state.supervisor_pid -ErrorAction SilentlyContinue
        Write-Host "Supervisor: $(if ($supervisor) { 'ALIVE' } else { 'NOT RUNNING' }) (PID $($state.supervisor_pid))"
        Write-Host "Cycle: $($state.cycles) | Model: $($state.model) | Status: $($state.status)"
        if ($state.last_error) {
            Write-Host "Previous cycle error (not current activity): $(ShortText $state.last_error)" -ForegroundColor DarkYellow
        }
        Write-Host ''
        Write-Host 'RECENT AGENT ACTIVITY' -ForegroundColor Cyan
        $activity = @()
        if ($state.output -and (Test-Path -LiteralPath $state.output)) {
            foreach ($line in Get-Content -LiteralPath $state.output -Tail 60 -ErrorAction SilentlyContinue) {
                try { $event = $line | ConvertFrom-Json -ErrorAction Stop } catch { continue }
                if ($event.type -eq 'tool_use') {
                    $part = $event.part
                    $inputText = $part.state.input | ConvertTo-Json -Compress -Depth 4
                    $activity += "$($part.tool) [$($part.state.status)]: $(ShortText $inputText)"
                } elseif ($event.type -eq 'text') {
                    $activity += "Agent: $(ShortText $event.part.text 200)"
                } elseif ($event.type -eq 'error') {
                    $activity += "ERROR: $(ShortText ($event.error | ConvertTo-Json -Compress -Depth 5))"
                }
            }
        }
        if ($activity.Count) { $activity | Select-Object -Last 8 | ForEach-Object { Write-Host "  $_" } }
        else { Write-Host '  Waiting for the first activity event...' }
    } catch { Write-Host "Waiting for supervisor status: $($_.Exception.Message)" -ForegroundColor Yellow }
    Write-Host ''
    $counter = Join-Path $root 'build-iteration.txt'
    if (Test-Path -LiteralPath $counter) { Write-Host "Build counter: $(Get-Content -LiteralPath $counter -TotalCount 1) (not proof of successful install)" }
    $emulators = @(Get-Process -Name kyty_emulator -ErrorAction SilentlyContinue)
    Write-Host "Emulator: $(if ($emulators.Count) { ($emulators | ForEach-Object { $_.MainWindowTitle }) -join '; ' } else { 'Not running - agent may be researching/building/testing' })"
    $lab = Join-Path $root '_Build\autolab'
    if (Test-Path -LiteralPath $lab) {
        $latest = Get-ChildItem -LiteralPath $lab -Directory | Sort-Object LastWriteTime -Descending | Select-Object -First 1
        if ($latest) {
            Write-Host ''
            Write-Host "LATEST GAME SESSION: $($latest.Name)" -ForegroundColor Cyan
            $summary = Join-Path $latest.FullName 'summary.txt'
            if (Test-Path -LiteralPath $summary) {
                Get-Content -LiteralPath $summary | Where-Object {
                    $_ -match '^(build|exit_reason|duration_s|fight_detected|first_fight_s|fight_median_fps|fight_min_fps|max_rss_mb)='
                } | ForEach-Object { Write-Host "  $_" }
            } else { Write-Host '  Session in progress; summary will appear when it finishes.' }
        }
    }
    Write-Host ''
    Write-Host 'Full evidence: LOOP-LOG.md | HANDOFF.md | _Build\agent-loop\'
    if (-not $Once) { Start-Sleep -Seconds $RefreshSeconds }
} while (-not $Once)
