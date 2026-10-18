param(
    [string]$GameDir = $(if (Test-Path -LiteralPath (Join-Path (Split-Path -Parent $PSScriptRoot) 'transfer-settings.json')) { (Get-Content -LiteralPath (Join-Path (Split-Path -Parent $PSScriptRoot) 'transfer-settings.json') -Raw | ConvertFrom-Json).GameDir } else { 'E:\ps5 games\UFC5-extracted' }),
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
    # Guest SysV red-zone protection (--redzone). Also enabled by _Build\claude-runner\redzone.flag.
    [switch]$RedZone,
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
    [ValidateRange(10, 300)][int]$FightStallSeconds = 45,
    # Keep the Windows display composited for the whole run so PrintWindow capture is valid even
    # when the PC is idle (DISPLAY: none otherwise makes the client area read back black).
    [bool]$KeepDisplayOn = $true,
    # PrintWindow cannot read this Vulkan window's client area; capture the composited screen
    # region instead (requires the window to be visible, which KeepDisplayOn/topmost ensures).
    [bool]$ScreenCapture = $true
)
$ErrorActionPreference = 'Stop'
$root = $PSScriptRoot
# A previous emulator that is still exiting holds ~14 GiB of reserved guest address space, and a
# new launch then fails with "failed to reserve guest address space". Wait for it to go away.
for ($w = 0; $w -lt 90 -and (Get-Process -Name kyty_emulator -ErrorAction SilentlyContinue); $w++) { Start-Sleep -Seconds 1 }
Start-Sleep -Seconds 20
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
    [DllImport("user32.dll")] public static extern bool GetClientRect(IntPtr hwnd, out RECT r);
    [DllImport("user32.dll")] public static extern bool ClientToScreen(IntPtr hwnd, ref POINT p);
    [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr hwnd);
    [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr hwnd, int nCmdShow);
    [DllImport("user32.dll")] public static extern bool IsIconic(IntPtr hwnd);
    [DllImport("user32.dll")] public static extern bool SetWindowPos(IntPtr hwnd, IntPtr after, int x, int y, int cx, int cy, uint flags);
    [DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow();
    [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr hwnd);
    [DllImport("kernel32.dll")] public static extern uint SetThreadExecutionState(uint esFlags);
    [DllImport("user32.dll")] public static extern bool SetProcessDPIAware();
    public struct RECT { public int L,T,R,B; }
    public struct POINT { public int X,Y; }
    // PrintWindow returns a black client area for this Vulkan window even when the game renders
    // (verified by comparing against a desktop screen capture). Capture the composited screen
    // region instead; the caller keeps the window visible/topmost first. Brightness ignores the
    // title bar so a black client area is not masked by it.
    public static double ShotBrightness(IntPtr hwnd, string path, bool screenCapture) {
        RECT r; GetWindowRect(hwnd, out r);
        int x = Math.Max(r.L, 0), y = Math.Max(r.T, 0);
        int w = Math.Max(r.R - r.L, 1), h = Math.Max(r.B - r.T, 1);
        using (var bmp = new Bitmap(w, h, PixelFormat.Format24bppRgb)) {
            if (screenCapture) {
                using (var g = Graphics.FromImage(bmp)) {
                    g.CopyFromScreen(x, y, 0, 0, new Size(w, h), CopyPixelOperation.SourceCopy);
                }
            } else {
                using (var g = Graphics.FromImage(bmp)) {
                    IntPtr hdc = g.GetHdc();
                    try { PrintWindow(hwnd, hdc, 2); } finally { g.ReleaseHdc(hdc); }
                }
            }
            bmp.Save(path, ImageFormat.Png);
            var d = bmp.LockBits(new Rectangle(0,0,w,h), ImageLockMode.ReadOnly, PixelFormat.Format24bppRgb);
            try {
                int stride = d.Stride;
                var buf = new byte[stride * h];
                Marshal.Copy(d.Scan0, buf, 0, buf.Length);
                long sum = 0; long n = 0;
                int top = Math.Min(40, Math.Max(h - 1, 0));
                for (int yy = top; yy < h; yy += 8) for (int xx = 0; xx < w; xx += 8) {
                    int i = yy * stride + xx * 3;
                    sum += buf[i] + buf[i + 1] + buf[i + 2]; n++;
                }
                return (double)sum / (n * 3.0);
            } finally { bmp.UnlockBits(d); }
        }
    }
}
"@ -ReferencedAssemblies System.Drawing

# Screen capture (CopyFromScreen) works in physical pixels; make this process DPI-aware so
# GetWindowRect returns matching coordinates instead of DPI-virtualized ones.
$null = [WinCap]::SetProcessDPIAware()

# Keep the display composited for the duration of the run. With only a SYSTEM power request the
# monitor/compositor can turn off while the PC is idle, and PrintWindow then returns a black
# client area even though the emulator presents frames (Dev 095/097 finding). This is a
# thread-scoped request released at the end of the script; it is not a persistent power-plan edit.
$ES_CONTINUOUS        = [uint32]2147483648
$ES_DISPLAY_REQUIRED  = [uint32]2
$ES_SYSTEM_REQUIRED   = [uint32]1
if ($KeepDisplayOn) {
    $keepAwakeFlags = [uint32](2147483648 -bor 2 -bor 1)
    $null = [WinCap]::SetThreadExecutionState($keepAwakeFlags)
    Write-Host 'AutoFight: display keep-awake requested (valid PrintWindow capture overnight)'
}

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
if ($RedZone -or (Test-Path (Join-Path $root '_Build\claude-runner\redzone.flag'))) {
    $emuArgs += @('--redzone')
    Write-Host 'AutoFight: guest red-zone protection enabled (--redzone)'
}
if ($ShaderDump) {
    $emuArgs += @('--shader-log-direction', 'File', '--shader-log-folder', (Join-Path $session 'shaders'),
        '--graphics-debug-dump', 'true')
} else {
    $emuArgs += @('--shader-log-direction', 'Silent')
}
# Dirty-log diagnostics by label (process-scoped env, inherited by the emulator):
# '*dlverify*' = dirty log + verify walk (counts/repairs misses), '*dlon*' = dirty log only.
Remove-Item Env:KYTY_UFC_BDA_DIRTY_LOG, Env:KYTY_UFC_BDA_DIRTY_VERIFY -ErrorAction SilentlyContinue
if ($Label -match 'dlverify') {
    $env:KYTY_UFC_BDA_DIRTY_LOG = '1'; $env:KYTY_UFC_BDA_DIRTY_VERIFY = '1'
    Write-Host 'AutoFight: BDA dirty log ON with verify (label)'
} elseif ($Label -match 'dlon') {
    $env:KYTY_UFC_BDA_DIRTY_LOG = '1'
    Write-Host 'AutoFight: BDA dirty log ON (label)'
}
# Generation-gated BDA walk by label: '*bdagen*' (KYTY_UFC_BDA_SYNC_GENERATION=1, default OFF).
# '*bdagenverify*' additionally runs the exact skip oracle (KYTY_UFC_BDA_SYNC_VERIFY=1).
Remove-Item Env:KYTY_UFC_BDA_SYNC_GENERATION, Env:KYTY_UFC_BDA_SYNC_VERIFY -ErrorAction SilentlyContinue
if ($Label -match 'bdagen') { $env:KYTY_UFC_BDA_SYNC_GENERATION = '1'; Write-Host 'AutoFight: BDA sync generation ON (label)' }
if ($Label -match 'bdagenverify') { $env:KYTY_UFC_BDA_SYNC_VERIFY = '1'; Write-Host 'AutoFight: BDA sync generation verify ON (label)' }
# Buffer GC age by label: '*gcoff*' = fixed GC age (KYTY_UFC_GC_FRAMES=0).
Remove-Item Env:KYTY_UFC_GC_FRAMES -ErrorAction SilentlyContinue
if ($Label -match 'gcoff') { $env:KYTY_UFC_GC_FRAMES = '0'; Write-Host 'AutoFight: GC frames OFF (label)' }
# Guest mprotect over GPU memory by label: '*mpinv*' = invalidate (KYTY_UFC_MPROTECT_INVALIDATE=1).
Remove-Item Env:KYTY_UFC_MPROTECT_INVALIDATE -ErrorAction SilentlyContinue
if ($Label -match 'mpinv') { $env:KYTY_UFC_MPROTECT_INVALIDATE = '1'; Write-Host 'AutoFight: mprotect invalidation ON (label)' }
# BDA fault buffers kept from GC by label: '*bdakeep*' (KYTY_UFC_BDA_KEEP=1).
Remove-Item Env:KYTY_UFC_BDA_KEEP -ErrorAction SilentlyContinue
if ($Label -match 'bdakeep') { $env:KYTY_UFC_BDA_KEEP = '1'; Write-Host 'AutoFight: BDA keep ON (label)' }
if ($Label -match 'nokeep') { $env:KYTY_UFC_BDA_KEEP = '0'; Write-Host 'AutoFight: BDA keep OFF (label)' }
# BDA dirty-log fast path range merge by label: '*bdamerge*' (KYTY_UFC_BDA_SYNC_MERGE=1).
Remove-Item Env:KYTY_UFC_BDA_SYNC_MERGE -ErrorAction SilentlyContinue
if ($Label -match 'bdamerge') { $env:KYTY_UFC_BDA_SYNC_MERGE = '1'; Write-Host 'AutoFight: BDA sync merge ON (label)' }
# Colour-metadata paths by label: '*dcc*' = GPU DCC clear evaluator, '*fill*' = coherent fills.
Remove-Item Env:KYTY_UFC_GPU_DCC -ErrorAction SilentlyContinue
if ($Label -match 'dcc') { $env:KYTY_UFC_GPU_DCC = '1'; Write-Host 'AutoFight: GPU DCC ON (label)' }
if ($Label -match 'fill') { $env:KYTY_UFC_FILL_CONVERT = '1'; Write-Host 'AutoFight: fill convert ON (label)' }
# Texture GC age in frames by label: '*texgc*' (KYTY_UFC_TEX_GC_FRAMES=1).
Remove-Item Env:KYTY_UFC_TEX_GC_FRAMES -ErrorAction SilentlyContinue
if ($Label -match 'texgc') { $env:KYTY_UFC_TEX_GC_FRAMES = '1'; Write-Host 'AutoFight: texture GC frames ON (label)' }
# GPU-sourced indirect draws by label: '*gpuind*' (KYTY_UFC_GPU_INDIRECT=1).
Remove-Item Env:KYTY_UFC_GPU_INDIRECT -ErrorAction SilentlyContinue
if ($Label -match 'gpuind') { $env:KYTY_UFC_GPU_INDIRECT = '1'; Write-Host 'AutoFight: GPU indirect ON (label)' }
if ($Label -match 'noind') { $env:KYTY_UFC_GPU_INDIRECT = '0'; Write-Host 'AutoFight: GPU indirect OFF (label)' }
# Native-image backing pool by label: '*imgpool*' (KYTY_UFC_IMAGE_POOL=1, default OFF).
Remove-Item Env:KYTY_UFC_IMAGE_POOL -ErrorAction SilentlyContinue
if ($Label -match 'imgpool') { $env:KYTY_UFC_IMAGE_POOL = '1'; Write-Host 'AutoFight: image backing pool ON (label)' }
# BDA page-table zero-init by label: '*bdainit*' (KYTY_UFC_BDA_TABLE_INIT=1).
Remove-Item Env:KYTY_UFC_BDA_TABLE_INIT -ErrorAction SilentlyContinue
if ($Label -match 'bdainit') { $env:KYTY_UFC_BDA_TABLE_INIT = '1'; Write-Host 'AutoFight: BDA table init ON (label)' }
# SRT materialization memo by label: '*srtshadow*' = shadow-verify, '*srtmemo*' = reuse.
Remove-Item Env:KYTY_UFC_SRT_MEMO -ErrorAction SilentlyContinue
if ($Label -match 'srtshadow') {
    $env:KYTY_UFC_SRT_MEMO = 'shadow'; Write-Host 'AutoFight: SRT memo shadow (label)'
} elseif ($Label -match 'srtmemo') {
    $env:KYTY_UFC_SRT_MEMO = '1'; Write-Host 'AutoFight: SRT memo ON (label)'
}
# HTile metadata-clear memo by label: '*memoclear*' (KYTY_UFC_META_CLEAR_MEMO=1);
# '*memoclearverify*' additionally compares every memo hit against the locked lookup.
Remove-Item Env:KYTY_UFC_META_CLEAR_MEMO, Env:KYTY_UFC_META_CLEAR_MEMO_VERIFY -ErrorAction SilentlyContinue
if ($Label -match 'memoclear') {
    $env:KYTY_UFC_META_CLEAR_MEMO = '1'; Write-Host 'AutoFight: metadata-clear memo ON (label)'
}
if ($Label -match 'memoclearverify') {
    $env:KYTY_UFC_META_CLEAR_MEMO_VERIFY = '1'; Write-Host 'AutoFight: metadata-clear memo verify ON (label)'
}
# Engine presented-frame dump by label: '*framedump*' writes presented frames to
# <session>\frames (KYTY_UFC_FRAME_DUMP), independent of the desktop compositor.
Remove-Item Env:KYTY_UFC_FRAME_DUMP -ErrorAction SilentlyContinue
if ($Label -match 'framedump') {
    $env:KYTY_UFC_FRAME_DUMP = (Join-Path $session 'frames')
    Write-Host 'AutoFight: engine presented-frame dump ON (label)'
}
# Per-thread host CPU sampler by label: '*threadcpu*' (KYTY_UFC_THREAD_CPU=1). It prints the
# busy-core split per host thread every 30 s so the frame-rate-limiting thread can be identified.
Remove-Item Env:KYTY_UFC_THREAD_CPU -ErrorAction SilentlyContinue
if ($Label -match 'threadcpu') {
    $env:KYTY_UFC_THREAD_CPU = '1'; Write-Host 'AutoFight: per-thread CPU sampler ON (label)'
}
# End-of-pipe labels written at host completion by label: '*labeleop*' (KYTY_UFC_LABEL_AT_HOST_EOP=1).
Remove-Item Env:KYTY_UFC_LABEL_AT_HOST_EOP -ErrorAction SilentlyContinue
if ($Label -match 'labeleop') { $env:KYTY_UFC_LABEL_AT_HOST_EOP = '1'; Write-Host 'AutoFight: labels at host EOP (label)' }
# End-of-pipe interrupts at parse time by label: '*earlyirq*' (KYTY_UFC_EARLY_EOP_IRQ=1).
Remove-Item Env:KYTY_UFC_EARLY_EOP_IRQ -ErrorAction SilentlyContinue
if ($Label -match 'earlyirq') { $env:KYTY_UFC_EARLY_EOP_IRQ = '1'; Write-Host 'AutoFight: early EOP interrupts (label)' }
# Dev 094 all-on bisect. Every feature below is ON by default in the emulator. A label containing
# 'bis-<letters>' turns the lettered features OFF for that run, e.g. 'd094-bis-abcdefgh' turns off
# the first eight. Each run prints the full ON/OFF table so results can be bisected afterwards.
$bisectFeatures = [ordered]@{
    a = @('KYTY_UFC_GPU_DCC', '0', 'GPU DCC clear check')
    b = @('KYTY_UFC_FILL_CONVERT', '0', 'colour-metadata fill conversion')
    c = @('KYTY_UFC_BDA_DIRTY_LOG', '0', 'BDA dirty log')
    d = @('KYTY_UFC_SRT_MEMO', '0', 'SRT memo reuse')
    e = @('KYTY_UFC_TEX_GC_FRAMES', '0', 'texture GC frame age')
    f = @('KYTY_UFC_BDA_TABLE_INIT', '0', 'BDA page-table zero init')
    g = @('KYTY_UFC_MPROTECT_INVALIDATE', '0', 'mprotect invalidation')
    h = @('KYTY_UFC_EARLY_EOP_IRQ', '0', 'early EOP interrupts')
    i = @('KYTY_UFC_VRAM_GC_BUDGET', '0', 'VRAM-budget GC')
    j = @('KYTY_UFC_GPU_PRIORITY', '0', 'GPU thread priority band')
    k = @('KYTY_UFC_ARGS_COPY', '0', 'indirect-args host copies')
    l = @('KYTY_UFC_POWER_THROTTLING', '1', 'power-throttling opt-out')
    m = @('KYTY_UFC_GPU_INDIRECT', '0', 'GPU-sourced indirect draws')
    n = @('KYTY_UFC_BDA_KEEP', '0', 'BDA keep from GC')
    o = @('KYTY_UFC_FAULT_BUFFER_INIT', '0', 'fault bitmap zero init')
    p = @('KYTY_UFC_GC_FRAMES', '0', 'buffer GC frame age')
}
$bisectOff = if ($Label -match 'bis-([a-p]+)') { $Matches[1] } else { '' }
if ($Label -match 'bis-') {
    foreach ($key in $bisectFeatures.Keys) { Remove-Item "Env:$($bisectFeatures[$key][0])" -ErrorAction SilentlyContinue }
}
foreach ($key in $bisectFeatures.Keys) {
    $feature = $bisectFeatures[$key]
    $off = $bisectOff.Contains($key)
    if ($off) { Set-Item -Path "Env:$($feature[0])" -Value $feature[1] }
    $state = if ($off) { 'OFF' } elseif (Test-Path "Env:$($feature[0])") { "env=$((Get-Item "Env:$($feature[0])").Value)" } else { 'ON' }
    Write-Host ("AutoFight feature {0} {1,-32} {2}" -f $key, $feature[2], $state)
}
# Multi-slot SRT materialization memo by label: '*srtmemoslots*' sets KYTY_UFC_SRT_MEMO_SLOTS=8
# (default 1 = original single-slot behavior). Placed after the bisect table so the slots choice
# and an optional shadow verify survive a 'bis-<letters>' label that clears the SRT-memo env var.
Remove-Item Env:KYTY_UFC_SRT_MEMO_SLOTS -ErrorAction SilentlyContinue
if ($Label -match 'srtmemoslots64') {
    $env:KYTY_UFC_SRT_MEMO_SLOTS = '64'; Write-Host 'AutoFight: SRT memo slots=64 (label)'
} elseif ($Label -match 'srtmemoslots') {
    $env:KYTY_UFC_SRT_MEMO_SLOTS = '8'; Write-Host 'AutoFight: SRT memo slots=8 (label)'
}
if ($Label -match 'srtmemoslots-shadow') {
    $env:KYTY_UFC_SRT_MEMO = 'shadow'; Write-Host 'AutoFight: SRT memo shadow verify (slots label)'
}
# Queue-thread submission (ported from Senaxx, Dev 117) by label: '*syncsubmit*' submits on the GPU thread (KYTY_ASYNC_SUBMIT=0).
Remove-Item Env:KYTY_ASYNC_SUBMIT -ErrorAction SilentlyContinue
if ($Label -match 'syncsubmit') { $env:KYTY_ASYNC_SUBMIT = '0'; Write-Host 'AutoFight: async queue submit OFF (label)' }
# NaN/Inf tracer (Dev 119) by label: '*nantrace*' logs 'NaN trace hit' lines naming the first shader write sites that produce NaN or Inf.
Remove-Item Env:KYTY_UFC_NAN_TRACE -ErrorAction SilentlyContinue
if ($Label -match 'nantrace') { $env:KYTY_UFC_NAN_TRACE = '1'; Write-Host 'AutoFight: NaN trace ON (label)' }
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
't,elapsed_s,fps,proc_cpu_s,proc_rss_mb,gpu_util_pct,gpu_mem_mb,brightness,frame' | Out-File $csv -Encoding utf8
$shot_i = 0
$start = Get-Date
$lastShot = -1000
$exitReason = 'timeout'
$build = ''
$focusDone = $false
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
    # PrintWindow needs the DWM redirection surface active; a restored, visible window keeps it
    # alive even with no user present. Force topmost once so occluding consoles cannot blank the
    # client area, then bring it to the foreground.
    if (-not $focusDone -and $ep.MainWindowHandle -ne [IntPtr]::Zero) {
        try {
            $hwnd = $ep.MainWindowHandle
            if ([WinCap]::IsIconic($hwnd)) { $null = [WinCap]::ShowWindow($hwnd, 9) }
            $HWND_TOPMOST = [IntPtr](-1)
            $SWP = [uint32](0x0002 -bor 0x0001 -bor 0x0040)  # NOMOVE|NOSIZE|SHOWWINDOW
            $null = [WinCap]::SetWindowPos($hwnd, $HWND_TOPMOST, 0, 0, 0, 0, $SWP)
            $null = [WinCap]::SetForegroundWindow($hwnd)
            Write-Host ("AutoFight: capture window visible={0} topmost-set fg={1}" -f `
                [WinCap]::IsWindowVisible($hwnd), ([WinCap]::GetForegroundWindow() -eq $hwnd))
        } catch { }
        $focusDone = $true
    }
    if ($ep.MainWindowTitle -match 'fps:\s*([\d.]+)') { $fps = $Matches[1] }
    # The title's cumulative frame counter measures frames actually rendered between samples.
    $frame = ''
    if ($ep.MainWindowTitle -match 'frame:\s*(\d+)') { $frame = $Matches[1] }
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
        try { $bright = [math]::Round([WinCap]::ShotBrightness($ep.MainWindowHandle, $shot, [bool]$ScreenCapture), 1) } catch { $bright = 'ERR' }
    }
    $rss = [math]::Round($ep.WorkingSet64 / 1MB, 1)
    "$((Get-Date).ToString('HH:mm:ss')),$elapsed,$fps,$([math]::Round($ep.CPU,1)),$rss,$gu,$gm,$bright,$frame" | Out-File $csv -Append -Encoding utf8
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
tracy_start_seconds=$TracyStartSeconds
screenshot_every_sec=$ScreenshotEverySec
shader_dump=$([bool]$ShaderDump)
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
if ($KeepDisplayOn) { $null = [WinCap]::SetThreadExecutionState($ES_CONTINUOUS) }
