# UFC 5 local development automation

## Current result

`dev` is based on current upstream `0e8ded3` plus the UFC work. UFC Dev 035 builds, installs,
and passes **53/53 CTest tests**; automation tests pass **11/11**. The old post-title hang is
fixed. The latest game benchmark is from pre-rebase build 033 (7 FPS median/5 min, no device
loss in 300 s). The integrated build has not yet had its own game benchmark. Intermittent
late-fight device loss and visual artifacts remain; target is not complete. See
`UPSTREAM-COMPARISON.md`, `HANDOFF.md`, and `LOOP-LOG.md`.

## Input schedule

`AutoFight.ps1 -MenuDrive` now defaults to:

```text
9:1x0,1.5:3x1.5,3:8x1.5,1.5:30x1.5,1.5:12x5.5@4
```

- Preserve the initial 12 menu presses through 27 seconds.
- Add 30 short X presses, 1.5 seconds apart, from 28.5 to 72 seconds.
- Add 12 four-second holds, with 1.5 seconds released between holds, from 73.5 to 134 seconds.
- The last button releases at 138 seconds. Default monitoring lasts 180 seconds.
- `presses.csv` includes target time, actual press time, and hold duration.

## Fight/error checker

`FightWatch.py` requires Pillow (installed here). It checks screenshot HUD regions for
paired stamina bars and fighter names; two consecutive candidates latch `fight_detected`.
Brightness alone is not used to claim a fight. `watch.json` records the evidence.

It incrementally watches stdout/stderr without repeatedly rereading the entire log.
Fatal/device-loss/SPIR-V-validation evidence writes `correction-request.md` and requests
that AutoFight stop its emulator/input processes. After detection, sampling becomes once
per second; sustained zero FPS for 45 seconds also ends the run. The checker classifies
failures; the coding agent investigates and fixes them in the next iteration.

Detection is a HUD heuristic, not proof of correct rendering or all possible errors.
Saved screenshots and surrounding console evidence remain essential.

## Profiling and FPS

Both Tracy 0.14.1 tools are built in `_Build/tracy-capture` and `_Build/tracy-csvexport`.
The harness begins capture on fight detection, or at `-TracyStartSeconds` (default 120)
if no fight is detected. It saves `fight.tracy`, `tracy-capture.log`, and `tracy-zones.csv`.
Use `-TracySeconds 0` for unprofiled performance measurements. Summary includes separate
fight median/min FPS. Do not compare title-screen FPS to gameplay FPS or use serialized
GPU crash diagnostics as performance evidence.

## Device-loss evidence on clean runs

AutoFight passes `--gpu-breadcrumbs true` unless `-NoGpuBreadcrumbs` is given. Each
draw/dispatch/end-of-pipe event writes top- and bottom-of-pipe `VK_AMD_buffer_marker`
values without the barriers `-GpuCrashDiagnostics` adds, so run timing is preserved
(no measurable fight-FPS change in one A/B pair on build 033). Every 30 s the console logs
`GPU breadcrumbs: recorded=N gpu_started=N gpu_completed=N`. On device loss it logs the
in-flight range and each `GPU breadcrumb suspect:` (op, args, PS/ES/GS guest addresses,
compute shader hash). `VK_EXT_device_fault` is now enabled on every run.

After each run AutoFight saves NVIDIA/Display driver events to `gpu-driver-events.csv`
and `GpuLossReport.py` adds `gpu_loss_class` to the summary and triage:
`timeout` (nvlddmkm 153 / Display 4101, a TDR: long-running or endless GPU work),
`memory_fault` (device-fault Read/Write/ExecuteInvalid), `driver_error`, `unknown`, or `none`.
The build-025 late-fight loss and the earlier post-title hangs were all nvlddmkm 153 timeouts.
The `debug_op` in `vkQueueSubmit failed` is only the last command recorded before submit
(`3` = EopWrite), not the failing draw.

`Build-Windows.ps1` refuses to build/install/test while a launcher or emulator is active.
It rechecks before installation/tests and retries transient install locks without killing games.
Close only processes started by your own run. Serialize builds, CTest and gameplay.

## Persistent OpenCode supervisor

The current unattended plan is `_Build/agent-reports/OVERNIGHT-PLAN-20261006.md`. It authorizes
early-load/media diagnostics and independent engine work when blackout reproduction is
inconclusive; the old Task-A-only deferral no longer applies. The start shortcut now requests
process-scoped keep-awake and absent-worker recovery, and retries an unexpectedly exited
supervisor. A live worker/manual game is never terminated; an exclusive worker lock serializes
ownership. STOP and verified goal markers remain intentional exits.

```powershell
py -3.13 Run-AutonomousLoop.py --dry-run
py -3.13 Run-AutonomousLoop.py
py -3.13 Run-AutonomousLoop.py --background  # detach and return the supervisor PID
```

Default cycle count is unlimited; each agent invocation performs one focused cycle.
The current objective is stable, correct **30 FPS** actual fights, using the Dev 077 safe
defaults and the latest verified HANDOFF candidate. Updated AnyPS5 references are recorded in
`_Build/agent-reports/anyps5-update-comparison-20261005.md`.
Before starting a model cycle, the supervisor checks games/launchers, active builds/tests,
competing automation/CLI agents and a Claude runner heartbeat newer than two minutes.
It waits without spending a cycle or model quota while blocked, and fails closed if process
inspection fails. `py -3.13 Run-AutonomousLoop.py --preflight-only` checks this without an agent.
Agents repeat ownership checks immediately before build/test/install/launch operations.
When a bounded investigation cannot continue without required user information, the agent
writes `_Build/agent-loop/WAIT-FOR-INPUT.json` with a `reason` and `questions`. The supervisor
stays alive with `status=waiting for user input`, polling every 60 seconds without starting
model cycles or repeating unchanged status documentation. After recording new user input in
the handoff/plan, clear that request to resume normal ownership checks and model selection.
Malformed input requests fail closed. This wait is distinct from the STOP marker.
The supervisor checks models in this order between cycles:

1. `openai/gpt-6.1-sol` (`high` reasoning variant)
2. `openrouter/deepseek/deepseek-v4.1-flash`

The OpenRouter fallback was requested on 2026-10-06 and its ID verified with a refreshed
`opencode models openrouter` catalog on OpenCode 1.18.34. OpenRouter credentials are installed;
the model advertises tool calling and image input. Space Bunny Alpha was not present in the
refreshed catalog and its candidate model endpoints returned 404, so it is not configured.
Quota errors place
the affected model on a five-hour cooldown. Sol is preferred again when its cooldown
expires, at the next cycle boundary. If all models are cooling down, it waits and checks
periodically. Other execution/provider failures cool down for five minutes. No credits
or API keys are written into the project. Actual fallback/authentication success depends
on the configured providers and available quotas; only an observed event proves it.

This machine's desktop environment initially caused CLI "Session not found" via inherited
server Basic Auth settings. The shared desktop SQLite database also lacks `replacement_seq`
for this CLI build. The supervisor clears only those server-auth variables in its child
environment and uses `_Build/agent-loop/opencode-loop.db`, leaving desktop history intact.
A read-only Sol CLI heartbeat returned READY successfully with this isolation. Current policy
is recorded as `model_priority` in state.json when the supervisor starts. A source/policy
update takes effect after restarting the supervisor between cycles; active runner jobs must
finish and the Claude worker must remain running. Provider catalog/auth presence alone does
not prove a successful quota-triggered fallback; consult actual cycle logs.

Live status: `_Build/agent-loop/state.json`. Agent output: `cycle-NNNNN.jsonl` and its
stderr log. `LOOP-LOG.md` is the cross-model handoff. A workspace lock prevents two
supervisors from editing simultaneously. Do not run a second interactive coding agent
against the same files while it works.

Stop between cycles by creating `_Build/agent-loop/STOP`. To interrupt immediately,
terminate the supervisor and its agent process tree using the PIDs in `state.json`.
Remove STOP before deliberately restarting. A verified `LOOP-GOAL-REACHED.md` also
stops the supervisor; it must contain actual gameplay/performance/quality evidence.

New `.opencode/agents/ufc-loop.md` and `opencode.json` were validated by
`opencode debug agent ufc-loop`. Quit and restart interactive OpenCode to load these
configuration changes; new CLI invocations load them automatically.

## Verification

```powershell
py -3.13 tests/AutomationHarnessTests.py
```

These tests verify non-overlapping short/long schedules, fragmented fatal-log detection,
nonfatal-noise handling, and the five-hour Sol → OpenRouter DeepSeek V4.1 Flash → Sol policy.
Two status-only cycles cause a short model cooldown and explicit next-action guidance. Only
engine/test changes and completed build/test/fight evidence count for this liveness check;
documentation or Git-status jobs alone do not. It is not a performance acceptance metric.
The current emulator CTest baseline is **53/53 passing**; upstream fixed the former
`kernel_file_system` PEEK/WAITALL failure. No automatic commits or pushes.
