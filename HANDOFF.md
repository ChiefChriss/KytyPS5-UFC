# KytyPS5-UFC — handoff for the automated loop

## Latest local iteration

### Integrated upstream baseline (dev branch)

The `dev` branch is based on current upstream `0e8ded3`, which is also the current fork
`main`; 43 upstream commits are included. Full comparison: `UPSTREAM-COMPARISON.md`.
Windows Clang build/install **UFC Dev 035** succeeded. Integrated regression suite is
**53/53 passing**; upstream's Windows PEEK/WAITALL change clears the previous lone
`kernel_file_system` failure. Automation harness is **11/11 passing**. No merged-build UFC
benchmark has been run yet. The 60-FPS/low-resource/high-quality target remains unmet.

Historical local-loop snapshot below: UFC Dev 027, unchanged renderer. Cycle 14 used existing
GPU checkpoints to attempt late-fight device-loss reproduction; no emulator code patch.
Changed: `HANDOFF.md`, `LOOP-LOG.md`, `build-iteration.txt` (26 -> 27). All prior
wave32/BDA and working-tree changes preserved. Build/install passed, automation **9/9**,
focused resource/compute **3/3**, full suite **50/51**, only known kernel_file_system
failure. Artifacts: `_Build/loop14-build.log`, `_Build/loop14-test-build.log`,
`_Build/loop14-focused-tests.log`, `_Build/loop14-tests.log` (PowerShell UTF-16 logs).

Latest DIAGNOSTIC session: `_Build/autolab/loop14-late-fight-device-loss-20261003-103442/`.
Command: `powershell -ExecutionPolicy Bypass -File .\AutoFight.ps1 -Label loop14-late-fight-device-loss -MonitorSeconds 300 -MenuDrive -ShaderValidation -GpuCrashDiagnostics -TracySeconds 0`.
2560x1440/Mailbox/default short/long schedule. Timeout **301.1 s**, no matched errors
or device loss. Actual fight detected **110.48 s**, **172 samples**, **190.62 s** observed,
max gap **2.07 s**, no zero samples. Diagnostic fight median/min **2/1 FPS**, peak RSS
**10729.3 MiB**, device-wide VRAM **11877 MiB**, GPU median **100%**, CPU **0.762 cores**.
Serialized checkpoints/snapshots alter timing heavily: NOT clean performance/resource
evidence, NOT proof of a fix. Latest clean performance baseline remains cycle 13 below.
Reviewed shot_0051: standing clinch, fighters/referee/cage/HUD present; speckled skin,
clothes/canvas and suspect lighting persist. Different pose than clean submission image;
no matched quality gain. Retain diagnostic evidence only; renderer unchanged.

Blocker: late device loss did not reproduce with serialization, so failing operation is
still unknown. Next narrow hypothesis: flat-SRT refresh CPU cost involves control-flow
walk/guest-read evaluation rather than specialization construction. Inspect/measure
that split with coarse scopes or bounded counters, not per-expression Tracy events.
SrtWalker already memoizes within each evaluation generation; do not cache across draws
without guest-memory invalidation proof. Saved cycle 13 RefreshFlatSrt is 3.809 us/call,
BuildSpecialization 0.074 us/call; inclusive timings are not exclusive CPU shares.
Stable 60-FPS fight goal remains unmet; no goal-reached file.

### Previous iteration (cycle 13; superseded by cycle 14 above)

Current installed build: **UFC Dev 026**, restored baseline. Cycle 13 rejected three
resource profiling scopes after late gameplay device loss; no emulator code retained.
Net files changed: `HANDOFF.md`, `LOOP-LOG.md`, build counter (23 -> 26). All prior
working-tree changes and wave32/BDA fixes preserved. Builds/install passed; automation
**9/9**, focused resource/compute **3/3**, experimental and rollback full suites **50/51**,
only known kernel_file_system failure. Latest build/tests: `_Build/loop13-rollback-build.log`,
`_Build/loop13-rollback-tests.log`. Detailed commands and evidence: cycle 13 in LOOP-LOG.md.

Latest CLEAN session: `_Build/autolab/loop13-restored-clean-20261003-101947/`.
Command: `powershell -ExecutionPolicy Bypass -File .\AutoFight.ps1 -Label loop13-restored-clean -MonitorSeconds 300 -MenuDrive -ShaderValidation -TracySeconds 0`.
2560x1440/Mailbox/default short/long schedule, no crash diagnostics. Timeout **300.16 s**,
no matched errors/device loss. Actual fight median/min **14/8 FPS**, detected **110.42 s**,
**173 samples**, **189.74 s** observed, max gap **1.29 s**, no zero samples.
Fight peak RSS **10891.4 MiB**, device-wide VRAM **12210 MiB**, GPU median **41%**,
CPU median **1.810 cores**. No performance/resource gain claimed: unchanged renderer,
different standing/submission workload versus cycle 11 clean 6/3 shows run variance.
Latest submission image: fighters/HUD render, speckled skin/canvas and suspect lighting
persist versus cycle 12; different pose/camera, no matched quality gain.

Rejected experiment: `_Build/autolab/loop13-resource-zones-20261003-101252/`, build 025,
lost device **220.87 s** during standing clinch. Actual fights **10/1 FPS**, **116.46 s**
observed, RSS **10679.5 MiB**, device VRAM **11901 MiB** (profiled/unstable).
Read correction request: `vkQueueSubmit ErrorDeviceLost`, tick **388627**, debug_op **3**,
debug_submit **337650**, args `4,1,0,0,0x00000011bd3b91b0`. Failing shader unknown;
no checkpoints, profiling causation unproved. Saved 30.15-second capture:
RefreshFlatSrt **3.809 us/call**, BuildSpecialization **0.074 us/call**, combined
MaterializeResources **6.167 s / 1,192,428 calls**. No IndirectImage zone observed.
Inclusive timings overlap; not exclusive CPU shares.

Next: diagnostic-only late-fight device-loss reproduction with
`AutoFight.ps1 -Label late-fight-device-loss -MonitorSeconds 300 -MenuDrive -ShaderValidation -GpuCrashDiagnostics -TracySeconds 0`.
Checkpoints are NOT performance evidence. Identify failing operation before semantic fixes.
If unreproduced, record limitation then investigate flat-SRT refresh from saved evidence.
Stable 60-FPS goal remains unmet; no completion file.

### Previous iteration (cycle 12; superseded by cycle 13 above)

Current installed build: **UFC Dev 023**. Cycle 12 retained a standalone tested Tracy
CSV reporter, merging duplicate source rows and recomputing weighted inclusive per-call
costs. No renderer/shader/settings change. Build/install passed, automation **9/9**,
focused compute passed, full suite **50/51**, only known `kernel_file_system` failure.
Artifacts: `_Build/loop12-build.log`, `_Build/loop12-tests.log` (UTF-16).

Latest session: `_Build/autolab/loop12-tracy-zones-20261003-095347/`.
Command: `powershell -ExecutionPolicy Bypass -File .\AutoFight.ps1 -Label loop12-tracy-zones -MonitorSeconds 300 -MenuDrive -ShaderValidation -TracySeconds 30 -TracyStartSeconds 300`.
2560x1440/Mailbox/default short/long schedule; no crash diagnostics. Survived **300.55 s**,
no matched errors/device loss. Fight detected **110.45 s**; 173 samples over **190.10 s**,
maximum gap **1.33 s**, median/min **12/9 FPS**. This is a PROFILED run, NOT comparative
clean FPS evidence; latest clean baseline remains cycle 11 **6/3 FPS**.
Fight peak RSS **10825.6 MiB**, device-wide VRAM **12147 MiB**, GPU median **40%**,
process CPU median **1.848 busy-core equivalents**. No resource improvement claimed.
Fighters/HUD render; speckled skin/canvas and suspect lighting remain. Latest screenshot
is a submission/top-down view versus cycle 11 standing view, not matched quality evidence.
Changed: `TracyZones.py`, `tests/AutomationHarnessTests.py`, logs/handoff, build counter.
Retain diagnostic only. Capture began at fight detection (111.71 s), duration **30.17 s**.
`tracy-comparison.md` combines source rows: PrepareDrawRenderState **14.392 vs 14.220 us**,
RefreshShaders **8.011 vs 8.117 us**, MaterializeResources **4.904 vs 4.936 us** (cycle 9).
Correction: cycle 9 combined MaterializeResources total is **6.420 s**, not earlier **2.9 s**.
Inclusive totals overlap and do not prove exclusive CPU saturation or scheduler waits.
Next: narrow resource-materialization diagnostic distinguishing SRT evaluation/guest reads
from specialization construction; separately reproduce clean standing-fight versus submission
cost differences. Current source-zone costs alone do not explain the clean 6-FPS runs.

The post-title GPU hang was fixed by making
native wave32 compute EXEC/VCC scalar mask branches wave-uniform rather than lane-local.
The regression suite remains **50/51 passing**, including a new GPU scalar-branch fixture;
only the known `kernel_file_system` failure remains. Multiple 180-second sessions now reach
actual fighting without device loss. Earlier fight performance was about **11 FPS**; latest
clean measurement is **6 FPS**, so the
overall optimization objective is NOT complete. BDA batching reduced measured per-call CPU
time from ~73 to ~48 microseconds but has not materially raised fight FPS.

Earlier baseline artifacts: `_Build/autolab/loop9-restored-baseline-20261003-022019/`.
All experiment evidence: `LOOP-LOG.md`. Full latest tests: `_Build/loop12-tests.log` (UTF-16).
Tracy capture and CSV export tools are now built and verified.

The user's latest instruction authorizes continuing local cycles without human interaction.
`Run-AutonomousLoop.py` serializes one OpenCode agent invocation per cycle, with verified
model IDs: `openai/gpt-6.1-sol`, `meta/muse-spark-1.3`, then
`opencode/muse-spark-1.3-contributor-free`. Quota errors put a model on a five-hour cooldown;
Sol becomes eligible again after that period. This is an external supervisor, not an
unsupported `fallback_models` config field. Provider availability/authentication/quota
errors are recorded rather than claiming fallback success before one occurs.
The CLI needs an isolated database on this machine (`_Build/agent-loop/opencode-loop.db`)
and must clear inherited desktop server-auth variables in its child environment. The
supervisor now does this; a Sol read-only heartbeat succeeded. See `AUTOMATION.md`.

Automation details: `AUTOMATION.md`. `FightWatch.py` detects paired stamina/name HUD
regions in two screenshots, then AutoFight samples every second. Fatal console evidence
terminates a failed session early and produces `correction-request.md`. It does not prove
visual correctness and does not itself edit emulator code.

Status as of build **UFC Dev 012** (2026-10-03). This file supersedes the "fine-tune tasks"
and defaults in `CLAUDE.md`; the project overview and upstream rules in `CLAUDE.md` still apply.

## TL;DR

- The toolchain is installed and one command builds, installs and tests the emulator.
- `AutoFight.ps1` launches UFC 5 and drives the menus with a virtual controller. It records
  FPS, memory and screenshots, then writes a triage file of errors.
- UFC 5 boots, renders the title screen at a steady 60 fps, and accepts controller input.
- **Main blocker:** about 25–75 s in, during the scene change after the title screen, one
  compute shader hangs the GPU. Windows resets the driver and the emulator exits with
  `vkQueueSubmit failed: ErrorDeviceLost`. The shader and dispatch are pinned down; the root
  cause is not fixed yet (see "What is not working").

## Machine

- Intel i7-14700K (microcode 0x12B), NVIDIA RTX 4080 SUPER (driver 591.86), Windows 11, ASRock B760M Pro RS.
- Previously developed on a Ryzen 9 9950X3D + RTX 5070 Ti. The `_Build` folder was first
  configured on that PC (user `dalda`); it has been reconfigured for this one.
- Game: `E:\ps5 games\UFC5-extracted` (title ID `PPSA03541`, contains `eboot.bin`).

## Toolchain (installed and verified)

| Tool | Version / location |
|---|---|
| Visual Studio 2022 Community + C++ Clang tools (`clang-cl`) | `C:\Program Files\Microsoft Visual Studio\2022\Community` |
| CMake / Ninja | 3.31.6 / 1.12.1 (bundled with VS) |
| Qt | 6.10.3 msvc2022_64 at `C:\Qt\6.10.3\msvc2022_64` (installed with `aqtinstall`) |
| Vulkan SDK | 1.4.363.0 at `C:\VulkanSDK\1.4.363.0` (`glslangValidator`, `spirv-dis`, `vulkaninfoSDK`) |
| Python | 3.13 (`py -3.13`) with `vgamepad`, `pre-commit`, `aqtinstall` |
| ViGEmBus | Installed and running (virtual DS4 driver) |
| Tracy capture tools | **Not built** (`_Build\tracy-capture`, `_Build\tracy-csvexport` are missing; Tracy is skipped) |

You don't need a Developer Prompt: `Build-Windows.ps1` loads the VS environment itself.

## How to use

All commands run from `E:\dev\KytyPS5-UFC` in PowerShell.

### Build

```powershell
.\Build-Windows.ps1              # configure, build, install, run all tests
.\Build-Windows.ps1 -SkipTests   # skip the test suite (fast iteration)
.\Build-Windows.ps1 -Fresh       # wipe CMake cache first
```

- Output: `_Build\windows\install\kyty_emulator.exe` (and `launcher.exe`).
- Each build bumps the number in `build-iteration.txt`. It shows as `UFC Dev NNN` at the start
  of the game window title and in the launcher title. A failed build hands its number back.
- The install step retries up to 5 times, because a crashed emulator or an antivirus scan can
  briefly lock `kyty_emulator.exe`.
- To capture a log, run it as `powershell -ExecutionPolicy Bypass -File .\Build-Windows.ps1 -SkipTests *> _Build\build-log.txt`.

### Run a test session

```powershell
.\AutoFight.ps1 -Label baseline -MonitorSeconds 120 -MenuDrive
```

Main parameters:

| Parameter | Default | Meaning |
|---|---|---|
| `-GameDir` | `E:\ps5 games\UFC5-extracted` | Folder containing `eboot.bin` |
| `-MonitorSeconds` | 180 | How long to watch before killing the emulator |
| `-Label` | `manual` | Session folder prefix |
| `-MenuDrive` | off | Press X on a schedule with a virtual DS4 |
| `-PressSchedule` | `9:1x0,1.5:3x1.5,3:8x1.5,1.5:30x1.5,1.5:12x5.5@4` | Menu sequence + 30 short presses + 12 long holds |
| `-PressHold` | 0.25 | Seconds each press is held |
| `-ScreenshotEverySec` | 5 | Screenshot interval |
| `-GpuCrashDiagnostics` | off | GPU checkpoints + device-fault report on device loss (slow, serialized) |
| `-ShaderDump` | off | Dump every shader (original `.bin` + generated `.spv`) into the session |
| `-VulkanValidation` | off | Khronos validation layer (do not combine with `-GpuCrashDiagnostics`, see below) |
| `-ShaderValidation` | off | Emulator shader validation |
| `-DisableFightWatch` | off | Disable local HUD/error watcher |
| `-FightStallSeconds` | 45 | End run after sustained zero FPS following fight detection |
| `-TracySeconds` | 30 | Capture duration; 0 disables capture for clean FPS runs |
| `-TracyStartSeconds` | 120 | Start capture at this time if no fight has been detected; otherwise start at detection |

Switches take no value (`-MenuDrive`, not `-MenuDrive $true`).

**Press schedule format:** comma-separated groups of `WAIT:COUNTxINTERVAL`. `WAIT` is the
delay before the group's first press, measured from the previous press (or from game launch
for the first group). The group then presses `COUNT` times, `INTERVAL` seconds apart. The
default preserves 12 presses at 9, 10.5, 12, 13.5, 16.5, 18, 19.5, 21, 22.5, 24, 25.5 and 27 s,
then adds 30 more short Cross presses at 1.5-second intervals from 28.5 through 72 s.
Finally, 12 four-second holds start from 73.5 through 134 s, spaced 5.5 s apart (1.5 s
released between holds). There are 54 presses total and the final hold ends at 138 s.
An optional `@HOLD` suffix overrides the hold for a group; intervals must exceed holds.
That gets past "Press any button" and EA's "online features require PSN" notice. Presses
are scheduled against the launch timestamp, so they don't drift; check `presses.csv` for
target versus actual times.

### Session output

Each run writes `_Build\autolab\<Label>-<yyyyMMdd-HHmmss>\`:

| File | Contents |
|---|---|
| `summary.txt` | `build`, `exit_reason` (`timeout` = survived, `emulator exited` = crash), `duration_s`, median/min FPS, max RSS, dark frames, first-bright-frame time |
| `triage.md` | Error counts by class (fatal/error/vulkan/spirv/unimpl), top 5 normalized error lines with file:line hints, last 15 log lines |
| `metrics.csv` | Every ~2 s: `t,elapsed_s,fps,proc_cpu_s,proc_rss_mb,gpu_util_pct,gpu_mem_mb,brightness` |
| `shot_NNNN.png` | Window screenshots (brightness < 12 counts as a dark frame) |
| `console.log` / `stderr.log` | Emulator stdout/stderr (fatal errors, crash diagnostics, `Controller N connected: …`) |
| `presses.csv`, `menudrive.log` | Virtual controller schedule and actual press times |
| `shaders\` | Only with `-ShaderDump` |

FPS comes from the window title (`fps: N`). GPU numbers come from `nvidia-smi` (left blank if it's missing).

### How it works

1. `AutoFight.ps1` launches `kyty_emulator.exe --game <dir> …` with output redirected into the
   session folder, and records the launch time.
2. With `-MenuDrive`, it immediately starts `py -3.13 MenuDrive.py --schedule … --start-epoch
   <launch>`. That creates a virtual DS4 through ViGEmBus and presses Cross on schedule.
3. Every ~2 s it samples the window title (FPS, build number), process CPU/RSS, `nvidia-smi`, and a
   `PrintWindow` screenshot whose average brightness is computed.
4. When the emulator exits or the time runs out, it kills the processes, then writes
   `triage.md` and `summary.txt`.

## What was added or changed

Nothing is committed. A backup of the GPU work-in-progress diff from before this session is at
`_Build\ufc-wip-backup.patch`.

### Scripts (repo root, untracked)

- `Build-Windows.ps1`: one-shot build, install and test. Loads `vcvars64` and the Vulkan SDK, bumps the build number, and retries the install.
- `AutoFight.ps1`: test harness. Handles early exit, missing Tracy, missing `nvidia-smi` and a
  missing window handle; works in Windows PowerShell 5; quotes paths with spaces; writes
  triage and summary; has the switches listed above.
- `MenuDrive.py`: virtual DS4 driver. D-pad names fixed for `vgamepad`; supports `--schedule` and `--start-epoch`.
- `build-iteration.txt`: build counter (currently 12).
- `HANDOFF.md`: this file.

### Emulator changes made this session

- **Image-view usage fix** (`image/imageView.cpp`, `imageView.h`, `tests/TextureMipTrimTests.cpp`).
  The previous WIP put each view's exact usage into the view cache key. That fragmented the
  cache and broke 6 tests (`shader_recompiler_compute`, `compute_meta_clear_classification`,
  4× `texture_cache_*`). The cache key is back to storage vs non-storage (upstream behavior).
  `ImageViewOps::ResolveViewUsage(backing, storage, format_features)` now drops sampled and
  color-attachment usage the view format can't support (for example RGB9E5 as a render
  target), which is what the WIP was after.
- **Controller input** (`libs/controller.cpp`, `presentation/window/window.cpp`):
  - The emulator sets `SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS=1`, so gamepads work while the window is unfocused.
  - Pressing a button on any connected gamepad makes it player 1. Before, only the
    first-connected pad was used, so a virtual pad was ignored when Steam Input or a real
    controller connected first.
  - It logs `Controller N connected: <name>` and `Controller N is now the active pad` to the console.
- **GPU crash diagnostics** (new `renderer/gpuCrashDiagnostics.{h,cpp}`; hooks in `context.cpp`,
  `commandScheduler.cpp`, `masterSemaphore.cpp`, `renderCompute.cpp`, `vulkanWindow.cpp`; flag
  in `common/emulatorConfig.*`, `main.cpp`). Turn it on with `--gpu-crash-diagnostics true`.
  - Enables `VK_NV_device_diagnostic_checkpoints`, `VK_NV_device_diagnostics_config` and `VK_EXT_device_fault` when available.
  - Every `CommandBuffer::SetDebugInfo` call (each draw, dispatch and end-of-pipe event)
    records a checkpoint in a 64K ring: op, submit ID, args, PS/ES/GS guest addresses, and
    the shader hash for compute.
  - Outside render passes it adds a full barrier before each checkpoint, so "the GPU reached
    checkpoint N at bottom-of-pipe" means all earlier work finished.
  - The 12 bytes of arguments for each indirect dispatch are copied into a CPU-readable ring
    before the dispatch.
  - On device loss it prints the last checkpoint each pipeline stage reached, the last 8
    indirect argument sets, and the device-fault address info.
- **Build number** (`generate_version.cmake`, `kytyGitVersion.h.in`, `launcher/src/mainDialog.cpp`):
  `KYTY_BUILD_ITERATION_LABEL` ("UFC Dev NNN | ") is read from `build-iteration.txt` and
  prefixed to `KYTY_BUILD_LABEL` (window title and crash reports) and the launcher title.

The rest of the uncommitted diff (`tile.*`, `descriptors.cpp`, `pipelineCache.cpp`,
`renderDraw.cpp`, `masterSemaphore.cpp` error path, `ResourceMaterialization.cpp`, `copyGeometry.h`,
`TextureMipTrimTests.cpp`, `CMakeLists.txt`) is the earlier UFC work-in-progress from before this session.

## What is not working

### 1. GPU hang during the post-title scene change (main blocker)

**Historical investigation below; resolved in UFC Dev 016 and retained in 019.** Matched
GPU snapshots showed the failing dispatch's high-16-bit buffer counts were at most 1.
Wave32 scalar mask-branch lowering was the successful fix. Current blockers are fight
FPS/resource costs and remaining visual inaccuracies, not this specific hang.

**Symptom.** About 25–75 s after launch (depending on press timing), FPS falls from 60 to about 7, RSS
jumps about 1.2 GB (4.3 to 6.0 GB), GPU memory goes from about 5 to 6.3 GB, and then `vkQueueSubmit
failed: ErrorDeviceLost` occurs at `commandScheduler.cpp:388`. The System event log shows
`nvlddmkm` event 153 (a GPU driver timeout reset) at the same second. It reproduces on every run with `-MenuDrive`.

**What the diagnostics proved** (`-GpuCrashDiagnostics`, serialized):
- The last checkpoint reached is the same command at both top-of-pipe and bottom-of-pipe:
  a **`DispatchIndirect`**, mode `0x8041` (wave32), compute shader guest address
  **`0x1140f04000`**, shader hash **`af3f147d3e75c30e`**, arguments at guest `0x11_429x_xxxx`
  (the address varies slightly per run).
- Its indirect arguments were **1216×1×1**: sane, and far below the limits (2147483647×65535×65535). The
  dispatches before it are a chain of indirect dispatches (0, 8320, 35904, 64384, 0, 7104, 20096, 0 groups).
- `VK_EXT_device_fault` reports `InstructionPointerInvalid` at a host GPU VA (`0x2007xxxxxx`).
- Vulkan validation reports no API errors before the hang. Its only error is a side effect:
  NVIDIA sets every timeline semaphore to `UINT64_MAX` after a device loss.
- Fixing the image-view usage bug did not change the hang.
- The hardware is fine as far as checked: the microcode has the Raptor Lake fix, and the WHEA
  ID 3 events coincide with display or HDR changes and aren't CPU errors.

**The shader** (`_Build\autolab\diag5-20261003-010850\shaders\0305_new_shader_cs_af3f147d3e75c30e.{bin,spv}`,
disassembly in `…\diag5-…\af3f147d3e75c30e.spvasm`):
- Local size 8×8×1. It uses `GroupNonUniformBallot` and `Shuffle`, and has **no atomics or barriers**.
  It contains exactly **one loop** (`OpLoopMerge` at spvasm line ~3563).
- Loop condition: counter `%3268 < %2440 && %3265`, where **`%2440 = buffers[2][idx] >> 16`**.
  It's a bounds-checked load from SSBO binding 2 (`idx = (%2377 >> 2) + 1 + ((%196 & 255) >> 2)`),
  so each lane can run **up to 65,535 iterations** of a very large body (the SPIR-V is about 15k lines). The loop
  also exits early through ballot-based masks (`%7743`/`%7754` → `%7763`, exec mask `%3266`/`%7764`).
- The pattern looks like a per-tile light/decal list: the count sits in the top 16 bits of a tile
  header word written by the earlier indirect-dispatch chain.

**Leading hypothesis.** The count word in binding 2 is stale or wrong when this dispatch runs.
Possible reasons: a missing GPU-to-GPU write-after-write or read-after-write sync, the buffer
cache binding the wrong guest range or offset, the earlier zero-group dispatches skipping work
that should initialize the header, or a mis-recompiled load or offset. Any of these turns a
small loop into a timeout. The second hypothesis is a recompiler bug in the ballot/exec-mask loop
exit (the exec mask emulation for wave32 on NVIDIA's 32-wide subgroups).

**Next steps for the loop:**
1. In diagnostics mode, copy binding 2's buffer into a CPU-readable buffer right before the hung
   dispatch (the same way the diagnostics already capture indirect arguments), and log the
   maximum value of `word >> 16`.
   If it's huge, trace the writer (the previous dispatches in the chain) and the buffer cache sync.
2. As a quick experiment (diagnostic only, never ship it), clamp `%2440` to something like 256 in the
   recompiler for hash `af3f147d3e75c30e`. If the hang disappears, the loop count is the culprit.
3. Check that `ShaderWriteHazardBarrier`/`ShaderAccessBarrier` cover the producer-to-consumer edge
   between the chained indirect dispatches, and that `ObtainBuffer` merging doesn't move binding 2 after binding.
4. Optional: raise Windows `TdrDelay` (registry; needs admin and a reboot) to see whether the
   dispatch eventually finishes (very long but finite) or never does (infinite).

### 2. Smaller issues

- **The `kernel_file_system` test fails on clean upstream too** ("Net receive forwards PEEK and
  WAITALL without consuming bytes"). It's pre-existing, not caused by this fork. The expected test result is 50 of 51 passing.
- **Vulkan validation combined with device loss:** the validation callback calls `EXIT` on the
  post-loss semaphore error before the crash diagnostics can print. Run `-GpuCrashDiagnostics`
  **without** `-VulkanValidation`.
- **`-GpuCrashDiagnostics` is slow** (a full barrier before every checkpoint). Use it only to locate hangs, never for FPS numbers.
- **Startup flake, seen once:** `failed to reserve guest address space at 0x0000000008040000`
  (`memoryAddressSpace.inc:1132`), exit code 321 at 0 s. Rerunning fixed it. If it recurs,
  suspect a DLL loaded into that range (overlays such as Discord, Steam or NVIDIA).
- **The game needs X presses** to get past "Press any button" and the EA "online features" notice;
  without `-MenuDrive` it sits on the title screen forever.
- **The active-pad switch only happens on a button press,** not on stick movement (deliberate, to avoid
  stick drift stealing player 1).
- **Tracy capture and csvexport aren't built,** so profiling is skipped. Build them from `3rdparty/tracy`
  into `_Build\tracy-capture\tracy-capture.exe` and `_Build\tracy-csvexport\tracy-csvexport.exe` for FPS work.
- `kyty_tests` builds about 3 warnings in `shader_recompiler_compute_tests` (pre-existing).

## Baseline numbers (UFC Dev 012 era, 2560×1440, Mailbox)

- Title screen: 60 fps median (41 minimum during loading), RSS about 4.2 GB, GPU utilization about 75%, about 4.9 GB VRAM.
- Without input it stays stable on the title screen for 120 s or more.
- With `-MenuDrive`, it crashes every run in the post-title scene change, typically at 27–75 s.

## Suggested loop recipe

Each iteration:

1. Make one small, focused change aimed at the current top issue (see "What is not working" above).
2. `.\Build-Windows.ps1 -SkipTests *> _Build\build-log.txt`. On failure, read `_Build\build-log.txt`, fix the problem, and retry.
3. `.\AutoFight.ps1 -Label <short-change-name> -MonitorSeconds 120 -MenuDrive`
   (add `-GpuCrashDiagnostics` when chasing the hang; add `-ShaderDump` when you need shader files).
4. Read `summary.txt` and `triage.md`. Success for the hang means `exit_reason=timeout` and no
   `ErrorDeviceLost`, with screenshots showing the main menu.
5. Before considering a change done, run the full `.\Build-Windows.ps1` (tests). The bar is 50 of
   51 passing, with only `kernel_file_system` failing.
6. Record the build number (`build=` in `summary.txt`), the change, and the result in a running log.

Stop and ask a human when:
- the same failure persists after about 5 attempts with no new evidence;
- a fix would touch guest JIT entitlements, the `guest_address_space` linker setup, or shader IR passes beyond a targeted fix;
- tests regress.

## Rules (from upstream `README.md`, AI policy)

- AI may assist, but a human must understand, review and test everything submitted.
- PR descriptions and comments must be written by the human; disclose the scope of AI involvement and the tests run.
- **Never auto-commit or auto-push.** Keep diffs small; match surrounding code style; run `pre-commit` clang-format on `src/**`.
- Windows is the primary target; don't regress it.
