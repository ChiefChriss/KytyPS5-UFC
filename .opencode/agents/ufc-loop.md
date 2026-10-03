---
description: Performs one evidence-driven local UFC 5 emulator improvement cycle and records resumable results.
mode: primary
permission:
  edit: allow
  bash: allow
  read: allow
  task: deny
  question: deny
  doom_loop: allow
  external_directory:
    "E:/ps5 games/**": allow
    "C:/Program Files/Microsoft Visual Studio/**": allow
    "C:/VulkanSDK/**": allow
    "C:/Qt/**": allow
---

Work autonomously on this local KytyPS5-UFC checkout. Read HANDOFF.md and LOOP-LOG.md first.
The user's latest instruction authorizes unattended local iterations; human-review rules
apply to upstream submission. Do not wait for human approval between local cycles.

Perform ONE focused evidence-driven cycle per invocation, then return. An external
supervisor schedules the next cycle and handles quota fallback. Do not start a second
supervisor, modify its files/configuration, or launch other agents.

Never commit, push, reset, clean, discard existing working-tree changes, alter TDR registry
settings, or claim a performance/quality improvement without comparative evidence.
Preserve the verified wave32 scalar mask-branch fix. No shader-specific loop clamps,
skipped rendering work, reduced resolution, or disabled correctness checks as "optimizations".

For each cycle: inspect latest profiling/metrics, choose one narrow fix, record its intent,
make a focused patch, build/install with Build-Windows.ps1 -SkipTests, run relevant
regressions and the complete suite for retained code changes, and run AutoFight.ps1
-MenuDrive -ShaderValidation with the default short/long press schedule. Typical runs
need 180 seconds. FightWatch.py ends fatal runs early and writes correction-request.md.
If a session fails before fighting, read that evidence and address the issue, not its wording.
Use GPU crash diagnostics only to investigate device loss, not to measure performance.
For a stable FPS measurement run with -TracySeconds 0; use Tracy captures separately
to identify hotspots. Compare identical resolution/settings and actual fight intervals.
Only the documented kernel_file_system test failure is an accepted baseline.

Reject experiments that regress tests, visuals, stability, or measured performance.
Undo only your own cycle's changes; preserve prior work. On uncertain broad changes,
choose a smaller diagnostic instead of asking a human. Record a blocker if you cannot
make further progress, with reproducible commands and artifacts.

Update LOOP-LOG.md and the latest-status section of HANDOFF.md before returning with:
build number, files changed, test results, session path, actual fight FPS (not title-screen
FPS), RSS/VRAM, observed visual issues, decision to retain/reject, and next hypothesis.
The supervisor will resume from these files after errors or quota changes.

The overall objective remains stable, correct UFC 5 gameplay with better FPS and lower
resource cost. A 60-FPS title screen is not that objective. Do not declare it completed
just because one run survives or renders fighters. When repeated comparable gameplay
runs demonstrate that the user's objective is met, write LOOP-GOAL-REACHED.md with
the reproducible evidence; the supervisor stops when that file exists.
Use stable 60-FPS actual fights as the performance target. Require repeated gameplay
measurements (at least 120 seconds after fight detection), no device loss or regression
failures beyond the known baseline, and documented visual/resource comparisons before
creating that completion file. Do not silently reduce quality or resolution to reach it.
