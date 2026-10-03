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

`Build-Windows.ps1` force-kills every `kyty_emulator` process if install finds the exe locked.
When another agent may rebuild concurrently, run a copy:
`Copy-Item _Build\windows\install _Build\install-snapshot -Recurse` then
`AutoFight.ps1 -Emulator "$PWD\_Build\install-snapshot\kyty_emulator.exe" ...`.

## Persistent OpenCode supervisor

```powershell
py -3.13 Run-AutonomousLoop.py --dry-run
py -3.13 Run-AutonomousLoop.py
py -3.13 Run-AutonomousLoop.py --background  # detach and return the supervisor PID
```

Default cycle count is unlimited; each agent invocation performs one focused cycle.
The supervisor checks models in this order between cycles:

1. `openai/gpt-6.1-sol` (`high` reasoning variant)
2. `meta/muse-spark-1.3`
3. `opencode/muse-spark-1.3-contributor-free`

These IDs were verified with `opencode models` on OpenCode 1.16.2. Quota errors place
the affected model on a five-hour cooldown. Sol is preferred again when its cooldown
expires, at the next cycle boundary. If all models are cooling down, it waits and checks
periodically. Other execution/provider failures cool down for five minutes. No credits
or API keys are written into the project. Actual fallback/authentication success depends
on the configured providers and available quotas; only an observed event proves it.

This machine's desktop environment initially caused CLI "Session not found" via inherited
server Basic Auth settings. The shared desktop SQLite database also lacks `replacement_seq`
for this CLI build. The supervisor clears only those server-auth variables in its child
environment and uses `_Build/agent-loop/opencode-loop.db`, leaving desktop history intact.
A read-only Sol CLI heartbeat returned READY successfully with this isolation. The actual
quota-driven switch to Muse has not yet been observed.

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
nonfatal-noise handling, and the five-hour Sol → paid Muse → free Muse → Sol policy.
The emulator's existing CTest baseline is 50/51 passing, with only the documented
`kernel_file_system` PEEK/WAITALL failure. No automatic commits or pushes.
