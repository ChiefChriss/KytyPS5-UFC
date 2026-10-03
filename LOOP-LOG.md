# UFC 5 local iteration log

## Cycle 15 — flat-SRT setup versus walk diagnostic — 2026-10-03

### Intent

Saved cycle 13 RefreshFlatSrt costs 3.809 us/call, but specialization only
0.074 us/call. Add coarse scopes inside RefreshFlatBuffer for total refresh,
control-flow setup and traversal/evaluation. This separates vector initialization
from the walk/evaluation aggregate, NOT guest-read time from expression time.
No per-expression events, caching, skipped reads, semantic/settings changes or
performance claim. Link standalone SRT test targets to the existing Tracy target.
Preserve all prior wave32/BDA work. Build/install -SkipTests, automation/focused/full
regressions, then default MenuDrive/ShaderValidation 300 seconds with a separate
30-second fight Tracy capture. Reject if new failures appear; record limitations.

## Cycle 14 — late-fight checkpoint reproduction — 2026-10-03

### Result and decision

Retain evidence only; no emulator code patch or rendering/settings change. Installed
**UFC Dev 027**. Files changed: `HANDOFF.md`, `LOOP-LOG.md`, build-script-generated
`build-iteration.txt` (26 -> 27). Prior wave32 scalar mask branches, BDA batching and
all existing local work preserved. No commits/pushes or supervisor edits.

- `powershell -ExecutionPolicy Bypass -File .\Build-Windows.ps1 -SkipTests`: build/install
  passed; `_Build/loop14-build.log`. Built `kyty_tests` in vcvars64 environment;
  `_Build/loop14-test-build.log`.
- `py -3.13 tests/AutomationHarnessTests.py`: **9/9 passed**.
- `ctest --test-dir _Build/windows -R 'shader_recompiler_compute|resource_materialization|resource_tracking' --output-on-failure`:
  **3/3 passed**, 21.53 s; `_Build/loop14-focused-tests.log`.
- `ctest --test-dir _Build/windows --output-on-failure`: **50/51 passed**, 136.73 s;
  only documented kernel_file_system PEEK/WAITALL failure, exit 0xc0000409.
  `_Build/loop14-tests.log`. Logs are UTF-16; decoded explicitly for inspection.
- Session command: `powershell -ExecutionPolicy Bypass -File .\AutoFight.ps1 -Label loop14-late-fight-device-loss -MonitorSeconds 300 -MenuDrive -ShaderValidation -GpuCrashDiagnostics -TracySeconds 0`.
- Session: `_Build/autolab/loop14-late-fight-device-loss-20261003-103442/`, timeout
  **301.1 s**, no matched errors/device loss. 2560x1440/Mailbox/default 54 short/long
  presses, shader validation, checkpoints on, Tracy off. No correction request generated.
- Actual fight detected **110.48 s**, **172 FPS samples**, **190.62 s** observed,
  sample span **189.26 s**, max gap **2.07 s**, no zero samples. Diagnostic median/min
  **2/1 FPS**, peak RSS **10729.3 MiB**, device-wide VRAM **11877 MiB**, GPU median
  **100%**, CPU median **0.762 busy-core equivalents** (171 intervals).
  Serialized checkpoints/full barriers and snapshots are NOT comparative performance
  evidence. Resource numbers also cannot establish savings versus clean cycle 13.
- Visual review: shot_0051 shows standing clinch, fighters/referee/cage/HUD, pronounced
  speckling on skin/clothes/canvas and suspect lighting. Clean cycle 13 shot_0051 is
  submission/top-down; different pose/camera prevents matched quality comparison.
- Inspected SrtWalker.cpp: RefreshFlatBuffer traverses control flow and evaluates SRT
  reads (855-905); EvaluateWide already has generation-scoped dense memoization (334-379).
  AcquireContext advances generation for each walker (321-327). No speculative
  cross-draw cache: guest memory can change even if user data/pointers do not.

Decision: unchanged baseline retained, diagnostic evidence retained; no FPS/resource/
quality improvement claimed. Late-fight failure remains unresolved, not disproved by
this survival. Stable 60-FPS fight objective remains unmet; no goal-reached file.

Blocker / next hypothesis: checkpoints alter timing and game-time progress (latest
clinch clock 4:12 vs clean submission 2:36), so 300 wall seconds does not reproduce
the same gameplay workload. Failing operation still unknown. Follow the existing saved
flat-SRT hotspot evidence with a narrowly bounded evaluation-vs-control-flow diagnostic
(coarse scopes/counters, not per-expression events), preserving all guest reads/checks.
Saved RefreshFlatSrt 3.809 us/call dominates BuildSpecialization 0.074 us/call within
that inclusive capture, but does not prove exclusive host saturation. Any future late
loss must be triaged separately using checkpoint evidence rather than semantic guesses.

### Intent

Cycle 13 lost the device at 220.87 s, with debug_op=3 and args 4,1,0,0,
but without checkpoints identifying the failing shader. Rebuild the unchanged baseline
and run automation, focused compute/resource and full regressions, then one 300-second
default MenuDrive/ShaderValidation session with GPU crash diagnostics and Tracy disabled.
Use existing checkpoint instrumentation rather than a speculative semantic patch.
This is a diagnostic-only cycle; serialized FPS is not optimization evidence. Preserve
all prior working-tree changes, especially wave32 scalar branches and BDA batching.
If failure does not reproduce, record the timing/serialization limitation and inspect
flat-SRT refresh from the saved capture/source for the next narrow hypothesis.

## Cycle 13 — rejected diagnostic / restored UFC Dev 026 — 2026-10-03

### Result and decision

Rejected three new resource profiling scopes after late gameplay device loss. Removed
only this cycle's profiler include/scopes and two Tracy test-target dependencies.
No emulator code change retained; all prior working-tree changes and wave32/BDA fixes
preserved. Net changed files: `HANDOFF.md`, `LOOP-LOG.md`, `build-iteration.txt` (23 -> 26).
No supervisor changes, commits or pushes. No performance/quality gain claimed.

- Build/install 024 passed (`_Build/loop13-build.log`); standalone resource tests then
  exposed a missing Tracy include dependency. Linking two test targets to Tracy resolved
  it; build/install 025 passed (`_Build/loop13-build-final.log`). Both dependency edits
  were subsequently removed with the profiling experiment.
- Automation **9/9**, focused resource_materialization/resource_tracking/compute **3/3**
  (`_Build/loop13-focused-tests.log`). First combined shell exceeded its 240-second outer
  timeout; repeated full suite completed **50/51**, only documented kernel_file_system
  PEEK/WAITALL failure (`_Build/loop13-tests.log`). No new accepted test baseline.
- clang-format dry-run reported extensive existing formatting violations; avoided
  whole-file formatting of prior work. Final `git diff --check` passed.
- Experiment command: `powershell -ExecutionPolicy Bypass -File .\AutoFight.ps1 -Label
  loop13-resource-zones -MonitorSeconds 300 -MenuDrive -ShaderValidation -TracySeconds 30
  -TracyStartSeconds 300`. Session `_Build/autolab/loop13-resource-zones-20261003-101252/`.
  Build 025, fight detected **104.41 s**, Tracy started **105.41 s**, span **30.15 s**.
  Device lost **220.87 s**, tick **388627**, debug_op **3**, debug_submit **337650**,
  args `4,1,0,0,0x00000011bd3b91b0`, commandScheduler.cpp:392. Read correction-request.md
  and triage. No checkpoints: actual failing shader/instruction unknown; this is later
  gameplay failure, not evidence the old post-title wave32 dispatch recurred.
- Experimental actual fight **10/1 FPS** median/min, **106 samples**, **116.46 s**
  observed, gap **1.30 s**, RSS **10679.5 MiB**, device VRAM **11901 MiB**, GPU **40%**,
  CPU **1.575 cores**. Profiled/unstable, insufficient 120-second coverage, not FPS evidence.
- Capture: combined MaterializeResources **6.167 s / 1,192,428 calls**; RefreshFlatSrt
  **4.542 s / 1,192,443 calls = 3.809 us/call**; BuildSpecialization **0.088 s /
  1,192,443 calls = 0.074 us/call**. No IndirectImage zone appeared. Inclusive timings
  overlap, not exclusive CPU shares. Reporter output: experiment `tracy-comparison.md`.
- Rollback build/install **026** passed (`_Build/loop13-rollback-build.log`), kyty_tests
  rebuilt, full suite **50/51**, only baseline failure (`_Build/loop13-rollback-tests.log`).
- Rollback command: `powershell -ExecutionPolicy Bypass -File .\AutoFight.ps1 -Label
  loop13-restored-clean -MonitorSeconds 300 -MenuDrive -ShaderValidation -TracySeconds 0`.
  Session `_Build/autolab/loop13-restored-clean-20261003-101947/`: timeout **300.16 s**,
  no matched errors/device loss. Both runs used **2560x1440/Mailbox**, default short/long
  schedule, shader validation, no GPU crash diagnostics.
- Clean rollback actual fight **14/8 FPS**, detection **110.42 s**, **173 samples**,
  **189.74 s** observed, span **188.59 s**, gap **1.29 s**, no zero samples. Peak fight RSS
  **10891.4 MiB**, device-wide VRAM **12210 MiB**, GPU median **41%**, CPU **1.810 cores**.
  Renderer unchanged; different poses/work distribution versus cycle 11 clean **6/3**
  demonstrate run variance, NOT optimization or resource improvement.
- Visuals: experiment shot_0037 shows standing clinch/fighters/referee/cage/HUD with
  pronounced speckled skin/canvas. Rollback shot_0051 shows submission near cage;
  compared with cycle 12 submission image, fighters/HUD render, speckling and suspect
  lighting persist. Camera/pose differ; no matched quality improvement claim.

Decision: retain evidence only, reject code conservatively, restore prior renderer.
Profiling causation of device loss is NOT proved. Stable 60-FPS objective remains unmet.
No goal-reached file.

Next hypothesis / blocker: late device loss may involve a distinct indirect dispatch or
timing-sensitive hazard. One clean rollback survival cannot establish cause or fix it.
Prioritize diagnostic-only reproduction:
`powershell -ExecutionPolicy Bypass -File .\AutoFight.ps1 -Label late-fight-device-loss -MonitorSeconds 300 -MenuDrive -ShaderValidation -GpuCrashDiagnostics -TracySeconds 0`.
Use checkpoints/device-fault evidence to identify the operation, NOT to measure FPS.
If unreproduced, record limitation then inspect SrtWalker flat refresh using saved capture
rather than speculative specialization caching (construction was small in this capture).

### Intent

Cycle 12 measured combined MaterializeResources at 4.904 us/call, but inclusive
source rows cannot distinguish SRT guest-read/evaluation from specialization work.
Add three coarse profiling scopes only: flat-SRT refresh, indirect-image materialization,
and BuildResourceSpecialization. Keep all resource reads, checks, ordering and rendering
unchanged; preserve wave32/BDA fixes. Avoid per-word/per-expression instrumentation.
Build/install with -SkipTests, automation and focused resource/compute regressions,
full suite, then a 300-second default MenuDrive/ShaderValidation run with a separate
30-second fight Tracy capture. Profiled FPS is not clean comparative performance evidence.

## Cycle 12 — UFC Dev 023 — 2026-10-03

### Result and decision

Retain narrow standalone `TracyZones.py` diagnostic and its two automation regressions.
No renderer/shader or rendering/settings changes; prior wave32 branch/BDA fixes preserved.
Changed this cycle: `TracyZones.py`, `tests/AutomationHarnessTests.py`, `HANDOFF.md`,
`LOOP-LOG.md`, build-script-generated `build-iteration.txt` (22 -> 23). No supervisor edits.

- `py -3.13 tests/AutomationHarnessTests.py`: **9/9 passed**. New tests cover duplicate
  per-thread sources, weighted mean, separate lines, zero counts, invalid totals/counts,
  PowerShell UTF-8 BOM and quoted CSV names. Reporter ignores exported rounded means
  and percentages; inclusive nested costs are explicitly not exclusive CPU shares.
- `powershell -ExecutionPolicy Bypass -File .\Build-Windows.ps1 -SkipTests`: build/install
  passed; `_Build/loop12-build.log` confirms UFC Dev 023. Initial test-shell quoting failed
  before tests; corrected by loading vcvars64 environment as the build script does.
- Built `kyty_tests`; focused `ctest --test-dir _Build/windows -R
  "shader_recompiler_compute|wave" --output-on-failure`: **1/1 passed**. Complete
  `ctest --test-dir _Build/windows --output-on-failure`: **50/51 passed**, only known
  `kernel_file_system` PEEK/WAITALL failure. `_Build/loop12-tests.log` (UTF-16).
- Command: `powershell -ExecutionPolicy Bypass -File .\AutoFight.ps1 -Label
  loop12-tracy-zones -MonitorSeconds 300 -MenuDrive -ShaderValidation -TracySeconds 30
  -TracyStartSeconds 300`.
- Session: `_Build/autolab/loop12-tracy-zones-20261003-095347/`, timeout **300.55 s**,
  no matched errors/device loss. 2560x1440/Mailbox/default 54-press short/long schedule,
  shader validation enabled, no GPU crash diagnostics. Tracy began **111.71 s** after
  detected fighting at **110.45 s**; capture time span **30.17 s**, 39,331,684 zones.
- Actual fights: **12/9 FPS** median/min, 173 samples, **190.10 s** observed,
  **188.84 s** sample span, **1.33 s** maximum gap, no zero samples. This is a profiled
  session, not a clean FPS benchmark or evidence of improvement over cycle 11's 6/3.
- Fight peak RSS **10825.6 MiB**, device-wide VRAM **12147 MiB**, median GPU **40%**,
  median process CPU **1.848 cores** (172 valid intervals). No resource gain claimed.
- Visual review: `shot_0051.png` vs cycle 11 `shot_0051.png`: fighters/HUD present,
  speckled/sparkling skin/canvas and suspect lighting persist. Submission/top-down vs
  standing/cage views differ substantially; no matched visual improvement claim.

Reproduce the source comparison:
`py -3.13 TracyZones.py --current _Build/autolab/loop12-tracy-zones-20261003-095347/tracy-zones.csv --baseline _Build/autolab/loop9-restored-baseline-20261003-022019/tracy-zones.csv`.
Saved stdout: latest session `tracy-comparison.md` (UTF-16).

| Inclusive zone | Latest total | Latest mean | Cycle 9 mean |
|---|---:|---:|---:|
| CommandProcessor::Process | 27.757 s | 398.518 us | 398.341 us |
| PrepareDrawRenderState | 10.654 s | 14.392 us | 14.220 us |
| RefreshShaders | 5.930 s | 8.011 us | 8.117 us |
| MaterializeResources (combined) | 6.080 s | 4.904 us | 4.936 us |
| Draw::PrepareBda | 2.719 s | 47.609 us | 48.621 us |

Important correction to historical partial-row interpretation: combined cycle 9
MaterializeResources = **6.420 s / 1,300,798 calls**, not **2.9 s**. Latest has
1,239,755 calls; nested zones overlap. TranslateProgram ran only **17** times,
87 ms inclusive; compilation churn is still not supported as the dominant cost.
Source-level per-call draw/resource costs are close to cycle 9 and do not explain the
clean 6-FPS sessions. No scheduler-wait attribution is established by these CSV zones.
Retain reporting only, no performance/quality claim, no goal-reached file.

Next hypothesis: repeated SRT/resource materialization is a meaningful host cost worth
splitting diagnostically into expression/guest-read versus specialization construction
before attempting memoization (which must respect guest-memory changes). Gameplay pose
and work distribution may explain some run variance; clean standing/submission intervals
need separate reproduction. Preserve all correctness checks and rendering work.

### Original intent

Use a smaller profiling diagnostic, not a speculative renderer patch. The cycle 9
CSV contains repeated source zones (including MaterializeResources) from different
threads; raw rows/percentages are not a reliable combined cost comparison. Add a
tested standalone CSV reporter that combines identical name/file/line rows and
recomputes weighted per-call time from total_ns/counts. Report inclusive costs,
never exclusive CPU percentages or measured FPS gains. Build/install and focused/full
tests, then capture 30 seconds starting at detected fighting (timed fallback delayed
to 300 s) in a 300-second default-schedule ShaderValidation session. Compare cycle 9
and new capture per-call costs; preserve all existing rendering/settings/fixes.

## Cycle 11 — UFC Dev 022 — 2026-10-03

### Result and decision

Retain the narrow CPU-use reporter and regression; no emulator rendering change or
performance/quality improvement is claimed. Files changed this cycle: `FightMetrics.py`,
`tests/AutomationHarnessTests.py`, `LOOP-LOG.md`, `HANDOFF.md`, and build-script-generated
`build-iteration.txt` (21 -> 22). Prior wave32 scalar branch and BDA fixes preserved.

- Build/install: `powershell -ExecutionPolicy Bypass -File .\Build-Windows.ps1 -SkipTests`
  succeeded; `_Build/loop11-build.log`.
- `py -3.13 tests/AutomationHarnessTests.py`: **7/7 passed**, including CPU-slope
  exclusion of pre-fight samples, missing/invalid counters, resets, and duplicate times.
- Built `kyty_tests` with vcvars64. Focused
  `ctest --test-dir _Build/windows -R "shader_recompiler_compute|wave" --output-on-failure`
  passed; full `ctest --test-dir _Build/windows --output-on-failure`: **50/51 passed**,
  only known `kernel_file_system` PEEK/WAITALL failure. `_Build/loop11-tests.log` (UTF-16);
  repeated focused/full tests visibly confirmed the same result.
- Session command:
  `powershell -ExecutionPolicy Bypass -File .\AutoFight.ps1 -Label loop11-cpu-baseline -MonitorSeconds 300 -MenuDrive -ShaderValidation -TracySeconds 0`.
- Session: `_Build/autolab/loop11-cpu-baseline-20261003-094051/`.
  Unchanged 2560x1440/Mailbox/default 54-press short/long schedule, shader validation,
  no crash diagnostics or Tracy. Timeout **300.94 s**, no matched errors/device loss.
- Actual fighting detected **141.81 s**, **145 FPS samples**, **159.13 s** observed,
  sample span **157.56 s**, maximum gap **1.57 s**. Median/min **6/3 FPS**, no zero samples.
  Meets observation duration, emphatically NOT the stable 60-FPS goal.
- Fight peak RSS **10657.9 MiB**, peak device-wide VRAM **11919 MiB**, GPU utilization
  median **35%**. CPU median **1.714 busy-core equivalents** across 144 valid intervals;
  this is process-wide consumption, not proof of render-thread saturation.
- Visual review: latest `shot_0051.png` compared to cycle 10 `shot_0040.png`:
  fighters/cage/HUD present, sparkling/speckled skin and canvas plus suspect lighting
  persist. Poses differ; no pixel-matched quality comparison or improvement claim.

Reprocessed cycle 10 via `py -3.13 FightMetrics.py --session
_Build/autolab/loop10-fight-metrics-20261003-092839`: median CPU **1.714 cores** over 101
intervals, median/min FPS **6/5**, RSS **10507.3 MiB**, device VRAM **11742 MiB**, GPU **37%**.
The latest run reproduces median 6 FPS; lower minimum and higher resource peaks occur
over a longer/different fight interval, not evidence of reporter-induced rendering changes.
No new renderer experiment to reject; retain diagnostic only. No goal-reached file.

Next hypothesis: a mostly serial host draw/resource-preparation or synchronization cost
limits fights. Process CPU/GPU figures alone cannot distinguish CPU work from waits.
Take a separate same-settings gameplay Tracy capture and compare PrepareDrawRenderState,
RefreshShaders, MaterializeResources and scheduler waits against cycle 9's capture before
choosing a targeted renderer patch. Do not use the profiled run as a clean FPS benchmark.

### Original intent

Intent: reproduce cycle 10 with a 300-second clean same-settings fight run,
and add a narrow CPU-use diagnostic from existing cumulative `proc_cpu_s` samples.
Report median busy CPU-core equivalents only between valid consecutive fight samples;
reject counter resets, zero time deltas and missing counters. This measures process-wide
CPU consumption, not render-thread saturation. No renderer/shader/settings changes or
performance improvement claimed. Build/install, automation/focused/full tests, then
default MenuDrive + ShaderValidation with TracySeconds 0. Preserve all prior fixes.

## Cycle 10 — UFC Dev 021 — 2026-10-03

### Result and decision

Completed the pending measurement diagnostic, not a renderer optimization. Retained
`FightMetrics.py`, its AutoFight integration (launch-relative sampling/settings/fight-only
summary), and regression coverage in `tests/AutomationHarnessTests.py`. This invocation
added sorted timestamp/max sample-gap reporting and two gap assertions plus an initial-gap
test; preserved every pre-existing emulator/shader change. Also updated `HANDOFF.md`,
`LOOP-LOG.md`; the build script advanced `build-iteration.txt` to 21 (it was already 20 on
disk despite the stale build-019 handoff). No supervisor files/configuration were changed.

- `powershell -ExecutionPolicy Bypass -File .\Build-Windows.ps1 -SkipTests`: passed,
  installed **UFC Dev 021**; `_Build/loop10-build.log`.
- `py -3.13 tests/AutomationHarnessTests.py`: **6/6 passed**.
- VS vcvars64 environment: `cmake --build _Build/windows --target kyty_tests`, then
  `ctest --test-dir _Build/windows -R "shader_recompiler_compute|wave" --output-on-failure`
  and `ctest --test-dir _Build/windows --output-on-failure`: focused test passed;
  full **50/51 passed**, only documented kernel PEEK/WAITALL failure. Artifact:
  `_Build/loop10-tests.log` (UTF-16); results also verified visibly with installed CTest.
- Session command:
  `powershell -ExecutionPolicy Bypass -File .\AutoFight.ps1 -Label loop10-fight-metrics -MonitorSeconds 240 -MenuDrive -ShaderValidation -TracySeconds 0`.
- Session: `_Build/autolab/loop10-fight-metrics-20261003-092839/`.
  2560x1440/Mailbox, default 54-press short/long schedule, shader validation enabled,
  normal scheduling; timeout at **240.87 s**, no matched errors/device loss.
- Actual fight detection **129.70 s**; **102 FPS samples**, **111.17 s** observed,
  **110.13 s** sample span, **1.27 s** maximum sampling gap. Median/min **6/5 FPS**;
  no zero-FPS fight samples. `fight_120s_observed=False` accurately exposes inadequate
  coverage despite a 240-second session. Do not treat this as a qualifying completion run.
- Fight peak RSS **10507.3 MiB**; peak **device-wide** VRAM **11742 MiB** (not exclusive
  emulator usage); median GPU utilization **37%**.
- Visual comparison: latest `shot_0040.png` vs cycle 9 `shot_0033.png` shows fighters,
  referee, cage and HUD in both; conspicuous speckled skin/canvas and suspect lighting
  persist. Different animation/camera poses prevent a pixel-matched quality comparison.

Historical cycle 9 reprocessed with the same reporter: fight **11/9 FPS**, 68 samples,
74.53 s observed, 2 s max gap, RSS **10715.2 MiB**, device VRAM **11947 MiB**, GPU median
37%. Latest measured FPS is worse, not better. Cycle 9 used a 30-second Tracy capture,
different observation interval and an older sampling clock; this is not a controlled A/B
proof of causation or resource improvement. This cycle changes reporting only, not game
rendering, so retain the tested diagnostic and flag the unexplained slowdown for reproduction.
No optimization claim, no goal-reached file, no commits/pushes.

Next hypothesis: gameplay is limited by CPU draw/resource preparation or a run-dependent
host bottleneck, consistent with low reported GPU utilization and prior Tracy costs, but
not yet proved. First obtain a **300-second** clean same-settings run (later detection
requires more margin for >=120 seconds of fights); then separately capture Tracy to
attribute the 6-vs-11 FPS difference before choosing a narrow renderer change.

### Original intent

Choose a smaller measurement diagnostic rather than another broad optimizer change.
Cycle 9 captured only ~75 seconds after HUD detection and its summary lacks fight-only
resource peaks/sample coverage. Add tested fight-interval reporting (including zero FPS,
missing-data handling and true median), align sample elapsed time to the watcher's launch
epoch, and record run settings. Build/install and run the complete suite, then a 240-second
default-schedule, shader-validation session with Tracy capture disabled. This extends the
observation interval without changing rendering quality or input. No FPS gain is claimed.
Resume the pending diagnostic and add maximum sampling-gap reporting so a long elapsed
interval cannot conceal sparse measurements. Preserve all pre-existing code changes.
Latest Tracy evidence: PrepareDrawRenderState ~10.9 s, RefreshShaders ~6.2 s,
MaterializeResources ~2.9 s across graphics/compute, but only five TranslateProgram calls
in the capture; compilation churn is not yet supported as the primary gameplay hotspot.

## Cycle 1 — UFC Dev 013 — 2026-10-03

### Change

Added a diagnostic-only GPU-to-host snapshot of SSBO buffer-array element 2 for compute
shader `af3f147d3e75c30e`. Enabled only with GPU crash checkpoints. Copies at most 1 MiB
from the actual committed Vulkan descriptor, with transfer/host visibility barriers and
a GPU completion marker. On device loss, reports high-16-bit statistics and the first
32 words. Does not clamp shader loops or change guest shader semantics.

Files: `src/graphics/host_gpu/renderer/gpuCrashDiagnostics.{cpp,h}` and `renderCompute.cpp`.
Formatted with the installed clang-format. Existing working-tree changes were retained.

### Verification

- `powershell -ExecutionPolicy Bypass -File .\Build-Windows.ps1 -SkipTests`: passed;
  installed UFC Dev 013.
- Built `kyty_tests` in the VS developer environment and ran
  `ctest --test-dir _Build/windows --output-on-failure`: 50/51 passed.
  Only `kernel_file_system` failed with the same PEEK/WAITALL error documented in HANDOFF.md.
- One game session:
  `powershell -ExecutionPolicy Bypass -File .\AutoFight.ps1 -Label loop1-binding2 -MonitorSeconds 120 -MenuDrive -GpuCrashDiagnostics`
- Artifacts: `_Build/autolab/loop1-binding2-20261003-012454/`.
- Exit: emulator exited, code 321, duration 29 seconds. Device loss remains.
- Sample median FPS 55, max RSS 5985.1 MiB; these are NOT optimization measurements:
  diagnostics serialize GPU work, and test compilation/testing ran concurrently.

### Evidence and interpretation

- Failed checkpoint: `seq=677903`, `DispatchIndirect`, shader `af3f147d3e75c30e`,
  arguments `1216x1x1`. Both reported pipeline-stage checkpoints identify this sequence.
- Device fault: `InstructionPointerInvalid`, address `0x0000000200707710`.
- Completed snapshot: **seq=676527**, guest `0x001171cc0000`, Vulkan offset 252444672,
  range/captured bytes 32640. All captured words have high16=0 (first 32 words entirely zero).
- Important: snapshot and fault sequences differ. The shader executes more than once.
  The one-shot snapshot captured an EARLIER invocation. This does not prove that the failing
  invocation's count is zero, nor rule out stale data or the wave/exec-mask hypothesis.
- Whole-buffer high16 maxima alone are not proof of excessive loop counts: only values at
  the actual shader-indexed tile headers are relevant.

### Next cycle (awaiting user confirmation)

Replace the one-shot snapshot with a bounded per-dispatch ring, publishing sequence and
descriptor metadata alongside each completed copy. Match the snapshot to `seq=677903`
or the next failing checkpoint. Account for descriptor alignment/push-data offsets when
interpreting shader indices. Then use that evidence to choose between producer/cache
ordering investigation and a targeted ballot/exec-mask recompiler investigation.

Cycle 1 is a completed diagnostic experiment, not a GPU-hang fix or FPS improvement.

## Cycles 2–9 — 2026-10-03

| Cycle/build | Experiment | Result |
|---|---|---|
| 2 / 014 | Eight-slot GPU snapshot ring, tagged by GPU-published dispatch sequence | Matched failing seq 689009: entire 32640-byte binding-2 snapshot had max high16=1. Hang remained at 27 s. |
| 3 / 015 | Disable NVIDIA pipeline optimization for suspect shader | Same hang at 27 s; rejected and diagnostic toggle subsequently removed. |
| 4 / 016 | Native wave32 compute EXEC/VCC scalar branches use wave-uniform ballot reduction | Survived 122 s with shader validation and serialized diagnostics; no matched errors. |
| 5 / 016 | 50 extra short X presses; normal GPU scheduling | Survived 181 s and screenshot confirmed fighters in the cage. |
| 6 / 016 | Revised 30 short presses + 12 four-second holds; HUD/error watcher; Tracy | Survived 180 s; fight detected at 106.46 s; captured gameplay. |
| 7 / 017 | Batch BDA mapped-range synchronization with one monotonic cache-buffer walk | Survived 181 s; fight median 11 FPS, min 10. BDA per-call CPU cost ~73 → ~48 us. |
| 8 / 018 | Enable SPIR-V performance optimization passes | Startup/compilation regression, no fight in 181 s; removed. |
| 9 / 019 | Restore successful SPIR-V path, retain correctness fix and BDA batch | Survived 181 s; fight detected at 106.47 s, median 11/min 9 FPS. |

### Retained correctness fix

`spirvEmitterFlow.cpp`: compute wave32 EXECZ/EXECNZ and VCCZ/VCCNZ conditions are scalar
wave decisions, not lane-local branches. Zero tests reduce across participating lanes;
nonzero tests reduce with any lane set. Capability analysis was updated in `SpirvEmitter.cpp`.
This retains inactive host lanes for scalar work and fixes the post-title GPU hang.
`Wave32ScalarMaskBranchesPreserveInactiveLanes` is a GPU regression fixture that masks all
but lane zero, branches around scalar updates, restores EXEC, and verifies every lane.
It passed in the focused wave suite and is included in the full compute test cases.

### Retained performance change

`BufferCache::SynchronizeBuffersInRanges` processes sorted mapped ranges and cached buffers
in one forward walk, retaining a buffer across gaps. `RenderContext::PrepareBda` uses it.
This preserves the same byte intersections and synchronization operations. Profiling reduced
about 17.8 million per-range events to about 65k batch events in the comparable capture.
Measured BDA call cost improved ~34%; gameplay FPS did **not** materially improve (still 11).
No memory-use or image-quality improvement is claimed for this change.

### Automation changes and verification

- Default schedule: 54 total presses, including 30 extra short presses and 12 four-second
  holds with 1.5-second release gaps. Last hold ends at 138 s; default run is 180 s.
- `FightWatch.py`: two-screenshot HUD detector, incremental fatal/error watch, atomic
  `watch.json`, and `correction-request.md`. AutoFight terminates failed runs early and
  samples once per second after detection; prolonged zero FPS also ends a run.
- Tracy capture + CSV export are now built and working. Capture starts on fight detection
  (with timed fallback), and clean FPS runs can use `-TracySeconds 0`.
- Python automation tests passed (schedule, fragmented fatal evidence, quota policy).
- Latest full emulator suite: **50/51 passing**, only known `kernel_file_system` failure,
  saved in `_Build/loop9-tests.log` (UTF-16). The known failing test still causes CTest and
  Build-Windows.ps1 without -SkipTests to return failure; inspect the results rather than
  blindly rebuilding that baseline failure.
- OpenCode model IDs and named agent configuration validated against installed 1.16.2.
  Supervisor/model fallbacks: `AUTOMATION.md`; provider fallback execution is not claimed
  until it is actually observed.

### Current baseline and next work

Latest session: `_Build/autolab/loop9-restored-baseline-20261003-022019/`.
2560x1440, Mailbox, shader validation, normal scheduling, Tracy 30-second fight capture.
Actual fight median/min: **11/9 FPS**. Peak process RSS: **10715.2 MiB**. Fighters and HUD
render; remaining visible quality includes noisy/sparkling surfaces and suspect lighting.
The overall low-resource, high-FPS, high-quality objective is still **in progress**.

Next: measure unprofiled actual fights, investigate draw preparation/resource materialization
and shader/pipeline compilation churn. Preserve the successful scalar branch fix. Reject
broad optimizer experiments unless repeated comparable runs demonstrate a real improvement.

### Supervisor startup verification

Initial detached invocations failed before model work with "Session not found". Clearing
inherited desktop server Basic Auth environment variables exposed a shared-database
schema mismatch (`no such column: replacement_seq`). Instead of altering desktop history,
the supervisor uses its own `opencode-loop.db` and a cleaned child environment.
A read-only `openai/gpt-6.1-sol` CLI heartbeat then returned READY. Supervisor tests cover
the environment isolation and five-hour model-priority policy. Actual Muse quota fallback
remains unobserved. `--background` starts a detached serial supervisor with status/log files.

## Upstream + UFC dev integration — UFC Dev 034 — 2026-10-03

- Fork `ChiefChriss/KytyPS5-UFC` `main` was at `0e8ded3`, identical to current upstream
  `KytyPS5/KytyPS5` `main`. Local checkout was 43 commits behind at `b3e419f`.
- Created local `dev` from latest upstream and applied the saved UFC worktree; reconciled
  overlaps in image-view usage, LDS clamp, image atomic descriptor validation, Vulkan
  device-fault features, and resource tests. The original stash remains as a recovery point.
- Upstream brings 43 commits of engine/platform improvements (full categorized review in
  `UPSTREAM-COMPARISON.md`), including hot-path shader/resource optimization, dirty-memory
  query and image-alias fixes, RDNA2 image atomic max, controller/audio improvements,
  Windows `PEEK/WAITALL`, Hades II, and cross-platform CI fixes.
- Local UFC wave32 scalar-branch fix, BDA mapped-range batch walk, GPU crash diagnostics,
  image-view format filtering, telemetry/controller changes, tests, and local automation
  were retained. No claim that this merge alone improves UFC FPS or image quality.
- Integration caught two source/test mismatches: a PM4 test had incorrectly categorized
  the active PS user-data address registers at SH offsets `0x2/0x3` as removed legacy
  holes; and repeated `PrepareBindings` left stale temporary buffer vectors. Fixed the test
  classification with precise failure diagnostics and clear the per-call vectors.
- `Build-Windows.ps1` build/install passed for **UFC Dev 034**.
- Full CTest on the integrated tree: **53/53 passed** in 58.25 s. Upstream's PEEK/WAITALL
  fix removes the previous lone `kernel_file_system` failure. Automation tests: **11/11**.
- Early CTest attempts while a UFC run reserved ~11 GiB failed 9 tests at guest VA commit
  reservation; a code issue/test assumption also failed shader tests. Re-ran after the game
  process exited and fixes landed; final CTest passed. Do not report the contaminated run.
- One 300 s UFC experiment happened just before integration (build snapshot 033): fight
  median/min 7/5 FPS, no device loss, max RAM 10.6 GiB / VRAM 12.6 GiB. It is not evidence
  for the merged build 034. Next: benchmark merged 034 in a clean actual fight.
- Fork `main` remains unchanged. Intended result is a committed/pushed `dev` branch.
