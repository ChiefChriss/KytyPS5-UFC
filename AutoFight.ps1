param(
    [string]$GameDir = "E:\ps5 games\UFC5-extracted",
    # Run a copied install so a concurrent Build-Windows.ps1 never finds this exe locked and kills it.
    [string]$Emulator = (Join-Path $PSScriptRoot '_Build\windows\install\kyty_emulator.exe'),
    [int]$MonitorSeconds = 180,
    [string]$Label = "manual",
    [int]$ScreenWidth = 2560,
    [int]$ScreenHeight = 1440,
    [switch]$VulkanValidation,
    [switch]$ShaderValidation,
    [switch]$GpuCrashDiagnostics,
    # Unserialized per-command buffer markers; on by default so clean runs still locate a loss.
    [switch]$NoGpuBreadcrumbs,
    [switch]$ShaderDump,
    [ValidateSet('None', 'Size', 'Performance')][string]$ShaderOptimization = 'Performance',
    [int]$TracySeconds = 30,
    [int]$TracyStartSeconds = 120,
    [int]$ScreenshotEverySec = 5,
    [switch]$MenuDrive,
    # Seconds after game launch, WAIT:COUNTxINTERVAL groups (see MenuDrive.py).
    # Menu sequence, 30 extra short presses, then 12 four-second holds with 1.5s release gaps.
    [string]$PressSchedule = "9:1x0,1.5:3x1.5,3:8x1.5,1.5:30x1.5,1.5:12x5.5@4",
    [double]$PressHold = 0.25,
    [string]$Python = "py",
    [string[]]$PythonArgs = @('-3.13'),
    [switch]$DisableFightWatch,
    [ValidateRange(10, 300)][int]$FightStallSeconds = 45
)
$ErrorActionPreference = 'Stop'
$root = $PSScriptRoot
$session = Join-Path $root ("_Build\autolab\" + $Label + "-" + (Get-Date -Format 'yyyyMMdd-HHmmss'))
New-Item -ItemType Directory -Path $session -Force | Out-Null
$emu = $Emulator
if (-not (Test-Path -LiteralPath $emu)) { throw "Emulator not found: $emu (run Build-Windows.ps1)" }
if (-not (Test-Path -LiteralPath (Join-Path $GameDir 'eboot.bin'))) { throw "No eboot.bin in $GameDir" }

Add-Type @"
using System;
using System.Drawing;
using System.Drawing.Imaging;
using System.Runtime.InteropServices;
public static class WinCap {
    [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr hwnd, IntPtr hdcBlt, uint nFlags);
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr hwnd, out RECT r);
    public struct RECT { public int L,T,R,B; }
    public static double ShotBrightness(IntPtr hwnd, string path) {
        RECT r; GetWindowRect(hwnd, out r);
        int w = Math.Max(r.R - r.L, 1), h = Math.Max(r.B - r.T, 1);
        using (var bmp = new Bitmap(w, h, PixelFormat.Format24bppRgb)) {
            using (var g = Graphics.FromImage(bmp)) {
                IntPtr hdc = g.GetHdc();
                try { PrintWindow(hwnd, hdc, 2); } finally { g.ReleaseHdc(hdc); }
            }
            bmp.Save(path, ImageFormat.Png);
            var d = bmp.LockBits(new Rectangle(0,0,w,h), ImageLockMode.ReadOnly, PixelFormat.Format24bppRgb);
            try {
                int stride = d.Stride;
                var buf = new byte[stride * h];
                Marshal.Copy(d.Scan0, buf, 0, buf.Length);
                long sum = 0; long n = 0;
                for (int y = 0; y < h; y += 8) for (int x = 0; x < w; x += 8) {
                    int i = y * stride + x * 3;
                    sum += buf[i] + buf[i + 1] + buf[i + 2]; n++;
                }
                return (double)sum / (n * 3.0);
            } finally { bmp.UnlockBits(d); }
        }
    }
}
"@ -ReferencedAssemblies System.Drawing

function Quote([string]$s) { if ($s -match '[\s"]') { '"' + ($s -replace '"', '\"') + '"' } else { $s } }

$emuArgs = @('--game', $GameDir, '--screen-width', $ScreenWidth, '--screen-height', $ScreenHeight,
    '--user-name', 'Kyty', '--user-id', '1000', '--present-mode', 'Mailbox',
    '--vblank-frequency', '60', '--console-language', '1',
    '--vulkan-validation', ([bool]$VulkanValidation).ToString().ToLower(),
    '--shader-validation', ([bool]$ShaderValidation).ToString().ToLower(),
    '--gpu-crash-diagnostics', ([bool]$GpuCrashDiagnostics).ToString().ToLower(),
    '--gpu-breadcrumbs', (-not $NoGpuBreadcrumbs).ToString().ToLower(),
    '--shader-optimization-type', $ShaderOptimization,
    '--printf-direction', 'Silent', '--profile', '--spirv-debug-printf', 'false',
    '--keymap', 'Cross=F9')
if ($ShaderDump) {
    $emuArgs += @('--shader-log-direction', 'File', '--shader-log-folder', (Join-Path $session 'shaders'),
        '--graphics-debug-dump', 'true')
} else {
    $emuArgs += @('--shader-log-direction', 'Silent')
}
$argLine = ($emuArgs | ForEach-Object { Quote "$_" }) -join ' '
$log = Join-Path $session 'console.log'
$errLog = Join-Path $session 'stderr.log'
$launchEpoch = [DateTimeOffset]::UtcNow.ToUnixTimeMilliseconds() / 1000.0
$launchTime = Get-Date
$p = Start-Process -FilePath $emu -ArgumentList $argLine -WorkingDirectory (Split-Path $emu) `
    -RedirectStandardOutput $log -RedirectStandardError $errLog -PassThru
$null = $p.Handle
Write-Host "Launched pid $($p.Id), session $session"

$menuProc = $null
if ($MenuDrive) {
    $menuArgs = $PythonArgs + @((Join-Path $root 'MenuDrive.py'), '--button', 'cross',
        '--schedule', $PressSchedule, '--start-epoch', $launchEpoch.ToString([Globalization.CultureInfo]::InvariantCulture),
        '--hold', $PressHold, '--log-csv', (Join-Path $session 'presses.csv'))
    $menuProc = Start-Process -FilePath $Python -ArgumentList (($menuArgs | ForEach-Object { Quote "$_" }) -join ' ') `
        -RedirectStandardOutput (Join-Path $session 'menudrive.log') -RedirectStandardError (Join-Path $session 'menudrive.err.log') `
        -NoNewWindow -PassThru
    Write-Host "MenuDrive schedule (s after launch): $PressSchedule"
}

$capture = Join-Path $root '_Build\tracy-capture\tracy-capture.exe'
$tracyOut = Join-Path $session 'fight.tracy'
$tracyJob = $null
if (-not (Test-Path -LiteralPath $capture)) {
    Write-Host "tracy-capture.exe not found, skipping Tracy capture"
}

$nvsmi = (Get-Command nvidia-smi -ErrorAction SilentlyContinue).Source

$watchProc = $null
$watchState = $null
if (-not $DisableFightWatch) {
    $watchArgs = $PythonArgs + @((Join-Path $root 'FightWatch.py'), '--session', $session,
        '--start-epoch', $launchEpoch.ToString([Globalization.CultureInfo]::InvariantCulture))
    $watchProc = Start-Process -FilePath $Python -ArgumentList (($watchArgs | ForEach-Object { Quote "$_" }) -join ' ') `
        -RedirectStandardOutput (Join-Path $session 'watch.log') -RedirectStandardError (Join-Path $session 'watch.err.log') `
        -NoNewWindow -PassThru
}

$csv = Join-Path $session 'metrics.csv'
't,elapsed_s,fps,proc_cpu_s,proc_rss_mb,gpu_util_pct,gpu_mem_mb,brightness' | Out-File $csv -Encoding utf8
$shot_i = 0
$start = Get-Date
$lastShot = -1000
$exitReason = 'timeout'
$build = ''
$stallStart = $null
while (((Get-Date) - $start).TotalSeconds -lt $MonitorSeconds) {
    Start-Sleep -Seconds $(if ($watchState -and $watchState.fight_detected) { 1 } else { 2 })
    # Same launch-relative clock as FightWatch.first_fight_s, not monitor setup time.
    $elapsed = [math]::Round(([DateTimeOffset]::UtcNow.ToUnixTimeMilliseconds() / 1000.0) - $launchEpoch, 2)
    $ep = Get-Process -Id $p.Id -ErrorAction SilentlyContinue
    if (-not $ep -or $ep.HasExited) { $exitReason = 'emulator exited'; break }
    $watchFile = Join-Path $session 'watch.json'
    if (Test-Path -LiteralPath $watchFile) {
        try { $watchState = Get-Content -LiteralPath $watchFile -Raw | ConvertFrom-Json } catch { }
        if ($watchState -and $watchState.action -eq 'stop') {
            $exitReason = 'correction required'
            Write-Host "Ending failed session early: $($watchState.reason)"
            break
        }
    }
    if (-not $tracyJob -and $TracySeconds -gt 0 -and (Test-Path -LiteralPath $capture) -and
        (($watchState -and $watchState.fight_detected) -or $elapsed -ge $TracyStartSeconds)) {
        $tracyJob = Start-Job -ScriptBlock { param($c, $o, $s) & $c -a 127.0.0.1 -o $o -s $s } -ArgumentList $capture, $tracyOut, $TracySeconds
        Write-Host "Starting Tracy at ${elapsed}s (fight_detected=$($watchState.fight_detected))"
    }

    $fps = ''
    if (-not $build -and $ep.MainWindowTitle -match 'UFC Dev \d+') { $build = $Matches[0] }
    if ($ep.MainWindowTitle -match 'fps:\s*([\d.]+)') { $fps = $Matches[1] }
    if ($watchState -and $watchState.fight_detected -and $fps -ne '' -and [double]$fps -eq 0) {
        if ($null -eq $stallStart) { $stallStart = Get-Date }
        if (((Get-Date) - $stallStart).TotalSeconds -ge $FightStallSeconds) {
            $exitReason = 'fight stalled'
            "# Correction required`n`nFight HUD was detected, then FPS stayed zero for $FightStallSeconds seconds. Inspect logs and screenshots before deciding on a fix." |
                Out-File (Join-Path $session 'correction-request.md') -Encoding utf8
            break
        }
    } else { $stallStart = $null }
    $gu = ''; $gm = ''
    if ($nvsmi) {
        try {
            $gpu = (& $nvsmi --query-gpu=utilization.gpu,memory.used --format=csv,noheader,nounits) | Select-Object -First 1
            $gu, $gm = $gpu -split ',\s*'
        } catch { }
    }
    $bright = ''
    if (($elapsed - $lastShot) -ge $ScreenshotEverySec -and $ep.MainWindowHandle -ne [IntPtr]::Zero) {
        $shot = Join-Path $session ("shot_{0:D4}.png" -f $shot_i); $shot_i++
        $lastShot = $elapsed
        try { $bright = [math]::Round([WinCap]::ShotBrightness($ep.MainWindowHandle, $shot), 1) } catch { $bright = 'ERR' }
    }
    $rss = [math]::Round($ep.WorkingSet64 / 1MB, 1)
    "$((Get-Date).ToString('HH:mm:ss')),$elapsed,$fps,$([math]::Round($ep.CPU,1)),$rss,$gu,$gm,$bright" | Out-File $csv -Append -Encoding utf8
}

$exitCode = ''
$ep = Get-Process -Id $p.Id -ErrorAction SilentlyContinue
if ($ep) { Stop-Process -Id $p.Id -Force -ErrorAction SilentlyContinue } else { try { $exitCode = $p.ExitCode } catch { } }
if ($menuProc -and -not $menuProc.HasExited) { Stop-Process -Id $menuProc.Id -Force -ErrorAction SilentlyContinue }
if ($watchProc -and -not $watchProc.HasExited) { Stop-Process -Id $watchProc.Id -Force -ErrorAction SilentlyContinue }
if ($tracyJob) {
    Wait-Job $tracyJob -Timeout 60 | Out-Null
    Receive-Job $tracyJob -ErrorAction SilentlyContinue | Out-File (Join-Path $session 'tracy-capture.log') -Encoding utf8
    Remove-Job $tracyJob -Force
}

# Driver events tell a TDR timeout (nvlddmkm 153) apart from other GPU errors. Display 4123
# is an HDR notification, so only its 4101 "driver recovered" event is kept.
if (Select-String -LiteralPath $log -Pattern 'DeviceLost' -SimpleMatch -Quiet -ErrorAction SilentlyContinue) {
    Start-Sleep -Seconds 3
}
$gpuEvents = @(Get-WinEvent -FilterHashtable @{ LogName = 'System'; ProviderName = 'nvlddmkm', 'Display'
        StartTime = $launchTime.AddSeconds(-5); EndTime = (Get-Date).AddSeconds(5) } -ErrorAction SilentlyContinue |
    Where-Object { $_.ProviderName -eq 'nvlddmkm' -or $_.Id -eq 4101 } | Sort-Object TimeCreated)
$gpuEvents | ForEach-Object {
    [pscustomobject]@{
        time      = $_.TimeCreated.ToString('o')
        elapsed_s = [math]::Round(($_.TimeCreated - $launchTime).TotalSeconds, 1)
        provider  = $_.ProviderName
        id        = $_.Id
        data      = (($_.Properties | Select-Object -First 2 | ForEach-Object { "$($_.Value)" }) -join ' | ')
    }
} | Export-Csv -LiteralPath (Join-Path $session 'gpu-driver-events.csv') -NoTypeInformation -Encoding utf8
$gpuLoss = & $Python @PythonArgs (Join-Path $root 'GpuLossReport.py') --session $session
if ($LASTEXITCODE -ne 0) { throw "GpuLossReport failed for $session" }

# Log triage: rank the most frequent error lines.
$patterns = [ordered]@{
    'fatal'      = '\[FATAL\]|EXIT\(|Assertion|assert'
    'error'      = '\[ERROR\]|\berror\b'
    'vulkan'     = 'VUID-|Validation Error|vkQueue|VK_ERROR'
    'spirv'      = 'SPIR-V|spirv|spv'
    'unimpl'     = 'not implemented|unimplemented|unsupported'
}
$lines = @()
foreach ($f in $log, $errLog) { if (Test-Path -LiteralPath $f) { $lines += Get-Content -LiteralPath $f -ErrorAction SilentlyContinue } }
$hits = foreach ($l in $lines) {
    foreach ($k in $patterns.Keys) {
        if ($l -match $patterns[$k]) {
            $norm = ($l -replace '0x[0-9a-fA-F]+', '0x?' -replace '\b\d+\b', 'N').Trim()
            [pscustomobject]@{ Class = $k; Line = $norm; Raw = $l.Trim() }
            break
        }
    }
}
$top = @($hits | Group-Object Line | Sort-Object Count -Descending | Select-Object -First 5)
$triage = @("# Triage: $Label", "", "exit_reason: $exitReason $(if ($exitCode -ne '') { "(code $exitCode)" })", "")
$triage += ($hits | Group-Object Class | ForEach-Object { "- $($_.Name): $($_.Count)" })
$triage += @("", "## Top errors", "")
$i = 1
foreach ($g in $top) {
    $sample = $g.Group[0]
    $hint = if ($sample.Raw -match '([\w/\\]+\.(?:cpp|h|inc)):(\d+)') { " - $($Matches[1]):$($Matches[2])" } else { '' }
    $triage += "$i. [$($sample.Class)] x$($g.Count)$hint"
    $triage += "   ``$($sample.Raw)``"
    $i++
}
if (-not $top) { $triage += "(no error lines matched)" }
$triage += @("", "## GPU loss", "") + ($gpuLoss | ForEach-Object { "- $_" })
$lossLines = @($lines | Where-Object { $_ -match 'GPU breadcrumb|GPU device fault|  address type=|  vendor "' } |
    Where-Object { $_ -notmatch 'GPU breadcrumbs: (recorded=\d+ gpu_started=\d+ gpu_completed=\d+)$|marker buffer ready|VK_AMD_buffer_marker' })
if ($lossLines) { $triage += @("", '```') + $lossLines + '```' }
$lastLines = $lines | Select-Object -Last 15
$triage += @("", "## Last log lines", "", '```') + $lastLines + '```'
$triage | Out-File (Join-Path $session 'triage.md') -Encoding utf8
# TODO(Sol-loop): watch triage.md top error -> patch -> Build-Windows.ps1 -> rerun AutoFight with same Label.

$rows = @(Import-Csv $csv)
$fpsRows = @($rows | Where-Object { $_.fps -ne '' })
$fpsVals = @($fpsRows | ForEach-Object { [double]$_.fps } | Sort-Object)
$fightRow = $rows | Where-Object { $_.fps -ne '' -and [double]$_.fps -gt 0 -and $_.brightness -notin @('', 'ERR') -and [double]$_.brightness -gt 12 } | Select-Object -First 1
if (-not $build) { $build = 'unknown' }
$summary = @"
build=$build
resolution=${ScreenWidth}x${ScreenHeight}
present_mode=Mailbox
shader_validation=$([bool]$ShaderValidation)
gpu_crash_diagnostics=$([bool]$GpuCrashDiagnostics)
gpu_breadcrumbs=$(-not $NoGpuBreadcrumbs)
tracy_seconds=$TracySeconds
session=$session
exit_reason=$exitReason
exit_code=$exitCode
duration_s=$(if ($rows) { $rows[-1].elapsed_s } else { 0 })
samples=$($fpsVals.Count)
median_fps=$(if ($fpsVals.Count) { $fpsVals[[int]($fpsVals.Count/2)] } else { 'n/a' })
min_fps=$(if ($fpsVals.Count) { $fpsVals[0] } else { 'n/a' })
max_rss_mb=$(($rows | ForEach-Object { [double]$_.proc_rss_mb } | Measure-Object -Maximum).Maximum)
dark_frames=$(@($rows | Where-Object { $_.brightness -notin @('', 'ERR') -and [double]$_.brightness -lt 12 }).Count)
first_bright_fps_s=$(if ($fightRow) { $fightRow.elapsed_s } else { 'n/a' })
fight_detected=$(if ($watchState) { $watchState.fight_detected } else { 'unknown' })
first_fight_s=$(if ($watchState -and $null -ne $watchState.first_fight_s) { $watchState.first_fight_s } else { 'n/a' })
screenshots=$shot_i
tracy=$(if (Test-Path -LiteralPath $tracyOut) { $tracyOut } else { 'none' })
"@
$fightMetrics = & $Python @PythonArgs (Join-Path $root 'FightMetrics.py') --session $session
if ($LASTEXITCODE -ne 0) { throw "FightMetrics failed for $session" }
$summary += "`n" + ($fightMetrics -join "`n")
$summary += "`n" + ($gpuLoss -join "`n")
$csvExport = Join-Path $root '_Build\tracy-csvexport\tracy-csvexport.exe'
if ((Test-Path -LiteralPath $csvExport) -and (Test-Path -LiteralPath $tracyOut)) {
    $zones = Join-Path $session 'tracy-zones.csv'
    try {
        & $csvExport $tracyOut | Out-File $zones -Encoding utf8
        $summary += "`ntracy_zones=$zones"
    } catch { }
}
$summary | Out-File (Join-Path $session 'summary.txt') -Encoding utf8
Write-Host $summary
Write-Host "Triage: $(Join-Path $session 'triage.md')"
