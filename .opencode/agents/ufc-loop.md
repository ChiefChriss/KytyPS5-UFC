---
description: Performs one evidence-driven local UFC 5 emulator engine cycle and records resumable results.
mode: primary
permission:
  edit: allow
  bash: allow
  read: allow
  task: deny
  question: deny
  doom_loop: allow
  external_directory:
    "C:/Users/dalda/Downloads/UFC5-extracted/**": allow
    "C:/Users/dalda/Downloads/UFC-Transfer-Dev088/**": allow
    "C:/Program Files (x86)/Microsoft Visual Studio/**": allow
    "C:/VulkanSDK/**": allow
    "C:/Qt/**": allow
    "C:/Users/dalda/Downloads/AnyPS5-main/**": allow
---

Work autonomously on this local KytyPS5-UFC checkout (branch `dev`, game
`C:\Users\dalda\Downloads\UFC5-extracted`; PC: RTX 5070 Ti, Ryzen 9 9950X3D).

## Latest overnight mandate (overrides old stops and priority deferrals)

First read `_Build/agent-reports/OVERNIGHT-PLAN-20261006.md`. The user is asleep and has
authorized continuous corrective work and upgrades. Blackout was immediate on loading into
gameplay. Target actual media/intro playback and a repeated, visually correct 25-FPS milestone,
with 30 FPS as the longer-term objective. If a bounded blackout investigation is inconclusive,
move to independent intro/readback/per-draw work in that plan; do not ask for more routine
details or repeat the old Task-A blocker. Every cycle must advance a concrete diagnostic,
regression, engine correction or necessary acceptance check. Do not promise or declare the
morning target achieved without measured evidence. Read only current status; old repeated
blocked-cycle entries are historical and not authority for this new request.

## The Claude job runner does all builds, tests and game runs

The Claude job runner (`Start-Claude-Worker.cmd`, `_Build/claude-runner/Claude-JobRunner.ps1`) owns
the GPU and the game on this PC. Use it for every build, CTest run and fight:

    py -3.13 RunnerJob.py build --label dev095-<topic>
    py -3.13 RunnerJob.py tests                         # full suite; --filter <ctest regex> for one
    py -3.13 RunnerJob.py fight --label <label> --monitor-seconds 300
    py -3.13 RunnerJob.py fight --label <label> --tracy-seconds 30   # separate Tracy run
    py -3.13 RunnerJob.py fight --label d094-load-media-diag --monitor-seconds 420 --tracy-seconds 60 --tracy-start-seconds 45 --screenshot-every-sec 1 --shader-dump

`RunnerJob.py` queues a JSON job in `_Build/claude-runner/queue/`, waits for
`done/<job>.exit`, and prints the log tail (a fight prints `session=...` and its summary).
Never run `Build-Windows.ps1`, cmake, ninja, ctest, `AutoFight.ps1`, the launcher or the emulator
yourself, and never stop or restart the runner. If it reports the runner is not running, record a
blocker and return. A `build` job refuses while the user's launcher or emulator is open: record
that blocker and return. Jobs run one at a time, so a fight never overlaps CTest.
The user's instruction authorizes unattended local iterations; human-review rules apply only
to upstream submission. Do not wait for approval between cycles.

Perform ONE focused, evidence-driven engine cycle per invocation, then return. An external
supervisor schedules the next cycle and handles quota fallback. Do not start a second
supervisor, modify its files/configuration, or launch other agents.

If the active plan reaches a bounded stop that needs missing human reproduction details,
and there is no independent work permitted by that plan, record the blocker once in
`_Build/agent-loop/WAIT-FOR-INPUT.json` (an object with a nonempty `reason` string and a
`questions` array), then return. The supervisor remains alive and waits without launching
more model cycles until the request is cleared after new user input. Do not remove that
request yourself to repeat the same blocked cycle, and do not keep appending unchanged
blocker entries to HANDOFF.md/LOOP-LOG.md. This is for required information, not routine approval.
The overnight plan explicitly permits independent work when the blackout cannot reproduce;
missing blackout timing is therefore not a reason to pause the entire loop again.

## Read first (latest sections only)

0. `_Build/agent-reports/OVERNIGHT-PLAN-20261006.md` -- latest user mandate and concrete next jobs.
   Also read `_Build/progress-watch/latest.md` if present: the user's five-minute free-model
   progress subagent leaves advisory feedback there. Check its timestamp and evidence; it
   does not override the user plan or prove an FPS/correctness claim. Address a useful fresh
   recommendation in the next concrete milestone; do not spend cycles repeating its status.
1. `_Build/agent-reports/NEXT-STEPS-DEV094.md` -- feature letters/reference work, subject to the override.
2. `HANDOFF.md` "Latest local iteration" (newest entry only).
3. `LOOP-LOG.md` newest substantive milestone, not repeated historical blocked entries.
4. `ENGINE-REVIEW.md` or comparison/port reports only for the subsystem being investigated.
Read older history only when a specific question needs it.

## Pre-flight (every cycle)

- Repeat these ownership checks immediately before each build, test, install or launch,
  including incremental builds. The supervisor also waits at cycle boundaries without
  spending a model invocation when pre-flight is blocked.
- **The user plays manual builds on this PC.** If a `launcher` or `kyty_emulator` process is
  running that you did not start, the user is playing: do not build, install, run CTest or
  launch the game this cycle (a build closes their game). Record a blocker and return.
- The Claude job runner must be running (heartbeat under 60 s old); `RunnerJob.py` checks it.
  It is the only owner of builds, tests and game runs. Do not wait for it to stop.
- Never run CTest and the game at the same time (each reserves ~13.5 GiB guest memory).
- Wait for competing builds, test executables and game automation to finish; do not stop
  processes you did not start. Inspect the actual `KYTY_UFC_*` environment before measurement.
- "failed to reserve guest address space" / "could not reserve 13824 MB" at startup means a
  previous emulator still holds memory. AutoFight.ps1 now waits up to 90 s + 5 s for
  `kyty_emulator` to exit; if it still fails, wait and relaunch. It is not a code fault.
- Guest null read at pc 0x9034b8c02 (Job threads, ~55-110 s) is SysV red-zone corruption by
  Windows exception dispatch, NOT a bad PROFILE save. Red-zone protection is default on since
  Dev 077 (`KYTY_UFC_REDZONE=0` disables). If it reappears, check that protection is active
  before attributing it to code. Do not restore PROFILE for this crash.
- Device lost with `gpu_loss_class=timeout` (nvlddmkm 153, InstructionPointerUnknown, breadcrumbs
  DrawIndex) is a pre-existing GPU hang, also seen on the Dev 044 source. One occurrence does not
  implicate your patch; use an unchanged-code control.

## Current baseline (UFC Dev 077; use the newest HANDOFF entry for subsequent builds)

Preserve all of these; do not remove or weaken them:
- Wave32 scalar mask-branch fix; BDA batch walk (the default sync path); UFC Dev 041 runtime
  viewport push data; empty-retirement scheduler fast path; loop cycle 21 `descriptors.cpp`
  alias-provenance hoist; `TileGetTextureSize` per-thread memo.
- Dev 078 decoder tail-branch fix: known forward targets beyond earlier `S_ENDPGM`
  instructions remain decoded. Preserve code-span-exit validation and disabled debug branches.
- Red-zone protection default on (`emulatorConfig.cpp`, fixes the 0x9034b8c02 crash).
- SPIR-V validation cache (`pipelineCache.cpp` `ValidatedSpirvCache`,
  `_PipelineCache/validated_spirv.bin`): skips re-validating SPIR-V already proven valid by the
  same SPIRV-Tools version; new SPIR-V is still fully validated. Repeat-launch validation
  5.5 s -> ~30 ms. `KYTY_UFC_SPIRV_VALIDATION_CACHE=0` disables.
- Shader build timing line before every `Shaders:` line:
  `Shader build ms: translate X compile Y validate Z module W | pipelines N in T ms`.
- GPU command thread + present thread pinned to P-core CPU sets
  (`Common::PinThreadToPerformanceCores`; `KYTY_UFC_PIN_GPU=0` disables).
- Buffer GC age measured in frames (`KYTY_UFC_GC_FRAMES=0` disables).
- Diagnostics: Tracy stall zones and the 30 s console line `GPU waits (cumulative count/ms): ...`
  incl. indirect args outcomes and `indirect sgpr locs`. Keep these counters; use them as evidence.

**Dev 094 changed the defaults:** every switch below, plus `KYTY_UFC_SRT_MEMO`, `KYTY_UFC_TEX_GC_FRAMES`,
`KYTY_UFC_BDA_TABLE_INIT`, `KYTY_UFC_MPROTECT_INVALIDATE`, `KYTY_UFC_EARLY_EOP_IRQ`,
`KYTY_UFC_VRAM_GC_BUDGET` and `KYTY_UFC_GPU_PRIORITY`, is now ON by default (`=0` turns it off;
`KYTY_UFC_POWER_THROTTLING=1` turns the throttling opt-out off). One of them blacks out the 3D scene
mid-fight. Isolate it with the `bis-<letters>` fight labels in NEXT-STEPS-DEV094.md, then return
that one feature to opt-in. The history below explains why each was once off:
- `KYTY_UFC_BDA_DIRTY_LOG` -- dirty-log fast path in `SynchronizeBuffersInRanges`.
  **Confirmed cause of fighter mesh explosions** (user bisect, Dev 073 vs 074). It misses some
  uploads. Do not enable by default until the missed case is found and covered by a test.
- `KYTY_UFC_ARGS_COPY` (indirect-args host copies), `KYTY_UFC_GPU_INDIRECT` (GPU-sourced
  indirect draws; works, every indirect draw GPU-served, but **no FPS gain** in a same-mode
  control), `KYTY_UFC_FILL_CONVERT` (colour-metadata fill as coherent buffer fill),
  `KYTY_UFC_GPU_DCC` (GPU DCC clear evaluator), `KYTY_UFC_PCORES` (whole process on P-cores;
  not faster). `KYTY_UFC_POWER_THROTTLING=0` opts threads out of EcoQoS (effect unproven).
  Re-enable any of these only one at a time, with multi-run stability evidence and a manual
  ground-and-pound visual check by the user.

Rejected (do not retry as-is): submitting the command buffer after each indirect-args copy;
literal SRT operand fast path (cycle 17); GPU-first indirect args as an FPS lever (no gain);
indirect SGPR-location patching (UFC is 100% `no_fetch`); occlusion-query work (UFC uses none).

## How to measure (same settings every time)

`py -3.13 RunnerJob.py fight --label <label> --monitor-seconds 300` (the runner calls
`AutoFight.ps1 -MenuDrive -ShaderValidation`; 2560x1440, Mailbox, default press schedule, red-zone
protection via `_Build/claude-runner/redzone.flag`). Use Tracy only in separate runs
(`--tracy-seconds 30`). A result with `exit_reason=emulator exited` and `duration_s=0` is a launch
failure (guest memory still held): rerun it, it is not data.
- Primary FPS metric: `fight_frame_rate` / `fight_frames` in summary.txt, plus the first 45 s
  of fighting (from the `frame` column in metrics.csv). Sampled `fight_median_fps` is noisy.
- Runs are bimodal: "fast" (`first_fight_s` ~100-106 s, 13-16 FPS opening) vs "slow" (~116 s,
  ~9-10 FPS). The user confirmed slow = game window not in focus. Only compare runs in the same
  mode, using the first-45-s window (whole-fight FPS depends on standing vs ground fighting).
- References, fast mode, first 45 s: Dev 070 (= Dev 044 source) 12.1; Dev 069 (dirty log + GC +
  args copy + GPU indirect) 14.6; Dev 056-060 (all on) 14.6-16.3; Dev 077 13.0 (whole fight
  16.06, RSS 10.7 GB, VRAM 12.3 GB). Runs with the dirty log on also had mesh explosions.
- Drain evidence: difference consecutive `GPU waits` lines (30 s apart) for ms/s per site.
- Report RSS/VRAM (`fight_max_rss_mb`, `fight_max_device_vram_mb`).
- **Automated screenshots (5.5 s cadence) miss momentary mesh explosions.** Never call visuals
  "correct" from them; ask for / record that a manual check is still needed.
- One run per build is weak evidence; GPU faults here are intermittent. Run changes at least twice.

## Method: measured structural upgrades, with bounded milestones

Adopt the approach that produced the Claude-session gains: remove an evidenced source of
engine work or GPU draining, validate its semantics, and measure the mechanism and actual
gameplay. A build number, passing suite, successful run, or extra profiling scope alone is
not a performance improvement. Preserve the current candidate when taking over a cycle;
do not keep restarting investigations or stack unrelated experiments.

Before implementing, record a short milestone: the expensive mechanism, current reference
run/window, expected work eliminated, correctness conditions, and acceptance evidence.
Choose from the milestones under "Engine priorities (current)" below, in order.

Use the latest known-good build/save and comparable reference evidence. Fight-start timing
is a scenario hint, not the whole match: verify fighters, arena, camera/standing versus ground
workload, settings, and frame-counter window. Use paired/repeated measurements when the gain
is small; do not repeat an expensive baseline unnecessarily when a valid one already exists.
Report absolute frame rates and mechanism costs, distinguishing measured benefits, estimates,
and limitations. If a milestone is blocked, isolate that concrete blocker or advance another
independent evidenced milestone; do not fill multiple cycles with logs-only activity.

A crash after a patch is evidence to investigate, not proof of causation. Check the known hazards in
Pre-flight, and use an unchanged-code control before deciding. A
confirmed patch regression still requires fixing or reverting only that patch. Do not discard
an otherwise unrelated engine improvement merely because a known baseline fault recurred.

### Fast development cycle (without weakening final acceptance)

Spend the cycle budget on implementation and targeted verification, not repeated startup.
Use these phases within a candidate milestone:

1. Reuse the latest valid scenario-matched baseline and saved capture. Read only current
   handoff/milestone evidence and the source involved; avoid rereading all history/reports.
2. During local coding iterations, use incremental builds of the affected executable/test
   targets and focused regressions. Do not bump/install a new numbered build, rebuild the
   launcher, reconfigure CMake or refresh unchanged submodules for every small correction.
   Use the existing build directory and installed toolchain; reconfigure when inputs/deps
   actually change. Preserve the standard final Build-Windows build/install procedure.
3. Validate the mechanism cheaply first: GPU producer/consumer semantic fixtures, cache
   invalidation tests or a representative host workload. For a new draw path, prove its eligibility
   and draw behavior before assuming it reaches common UFC draws. An additional short game
   smoke run is optional when it will resolve a specific risk; avoid adding one by habit.
   If coverage is zero or the mechanism fails, fix/reject promptly rather than repeatedly
   performing 420-second measurements that cannot demonstrate the proposed benefit.
4. Once the final candidate is ready, run the complete regression suite once and then the
   required 420-second acceptance run, serially. Repeat focused tests after a fix; repeat
   full tests/game measurements when new code, a failure, uncertain comparisons or a small
   gain justify it. A retained patch still requires 53/53 or better and qualifying fighting.
5. Reuse the reference window and compare frame counts plus the mechanism's wait/self-CPU
   cost. Only take another full Tracy capture when existing counters/capture cannot answer
   the implementation question. Stop data collection when it has answered that question.

Keep one GPU-test owner. CPU-only analysis can be separate; GPU tests and clean performance
runs must not overlap another game, CTest process or heavy build. Do not sacrifice resource
invalidation proof or gameplay correctness for speed. A tested smaller supported engine
path is a valid intermediate step; report its actual eligible fraction and next expansion.

## Engine priorities (current, strongest evidence first)

- P1 stability: GPU TDR mid/early fight (device lost, timeout, InstructionPointerUnknown,
  breadcrumbs end at DrawIndex x2). Pre-existing. Capture the breadcrumb draw's VS/PS hashes over
  several runs, dump their SPIR-V, and look for loops whose exit depends on wave32 scalar
  branches / EXEC handling or on uninitialised or stale resources. Fix ISA semantics; never clamp
  loops or skip the draw.
- P2 dirty-log missed upload: find which buffer change the dirty log fails to record (suspects:
  GPU image -> buffer syncs, writes that bypass the tracker, BDA epoch changes). Write a fixture
  that reproduces it, fix, then re-enable `KYTY_UFC_BDA_DIRTY_LOG` by default only after the user
  confirms no explosions. Earlier ~2 FPS estimates are hypotheses, not isolated proof.
- P3 translated-shader disk cache: first-launch intro / character-select stalls (2-5 FPS) come
  from ~800 shader builds: translate ~2.8 s, compile ~0.9 s, pipelines ~0.3 s (validation is now
  cached). Cache translated SPIR-V keyed by guest code hash + all state the translation reads,
  incl. the ResourcePlan / IR values it produces. Compare AnyPS5's ShaderDiskCache design. A hit
  must reproduce exactly what translation would produce; verify with a shadow compare mode first.
- P4 per-draw CPU work: `PrepareDrawRenderState` ~17 us/draw (RefreshShaders,
  SrtRefreshFlatBuffer, MaterializeResources). Any cross-draw reuse needs an explicit
  invalidation proof (SGPRs, guest memory writes incl. BDA, image rebinds); prefer a
  shadow-verified cache (compute both, compare, count mismatches) before trusting it.
- P5 buffer/image lifetime reuse: ~200 buffer creations/s, ~110 GC retirements/s, ~230-300
  image create/delete/s. Find creation sources before changing policy; pooled temporary buffers;
  watch VRAM.
- P6 visual correctness: white specks / afterglow trails on the cage behind fighters (also on
  Dev 044), skin speckling. Only with an identified shader/effect and ISA-correct fix.

## Rules

Work on engine code (`src/graphics/**`, shader recompiler, kernel/libs only when profiling
justifies it). Do not spend cycles on scripts, prompts, configuration or documentation polish.
Change one thing per cycle and implement a real engine change, not only profiling scopes.
Never commit, push, reset, clean, discard existing working-tree changes, or alter TDR
settings. Do not change resolution, skip guest work, clamp shaders, reduce quality, or weaken
validation/correctness checks to gain FPS. Every retained change needs focused regression
tests plus full CTest (`py -3.13 RunnerJob.py tests`; must stay 53/53 or more) and a 300 s
same-settings gameplay run through the runner with at least 120 s of fighting. Reject changes that regress tests,
visuals, stability or measured performance; undo only your own cycle's changes.
New risky engine paths go behind a `KYTY_UFC_*` switch (see `UfcSwitch` / `UfcOptIn` in
bufferCache.cpp) so the user can bisect by environment. Default them off until proven.
A single guest fault or device loss does not establish causation: check the Pre-flight hazards
and run an unchanged-code control before attributing it.

Build/install: `py -3.13 RunnerJob.py build --label <topic>` (the runner runs
`Build-Windows.ps1 -SkipTests`, which increments the "UFC Dev NNN" number and installs to
`_Build/windows/install`; ThinLTO is on). It refuses while the user's launcher is open: never close
it, record a blocker. The build log is printed by RunnerJob.py; on failure fix the compile error and
queue another build.

## Before returning

Update `LOOP-LOG.md` (new top entry) and the latest-status section of `HANDOFF.md` with: build
number, files changed, focused and full test results, session path, scenario
(`first_fight_s`), `fight_frame_rate`/`fight_frames`, median/min FPS, RSS/VRAM, wait-site ms/s,
visual observations, retain/reject decision, and the next hypothesis.

The immediate overnight milestone is media/intro playback and stable, correct **25 FPS**,
documented with comparable repeated gameplay. The longer-term objective is **30 FPS** with
lower resource use. A fast title
screen is not that objective. Create `LOOP-GOAL-REACHED.md` only after repeated comparable
gameplay runs (each >= 120 s of fighting) show at least 30 FPS in comparable focused-window
first-45-s windows and sustained fighting, no device loss or test regressions, and documented
visual/resource comparisons including a manual transient-artifact check. Do not reduce quality
or resolution to reach it. Record a missing manual check; do not ask for routine approvals.
