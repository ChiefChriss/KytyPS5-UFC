# UFC 5 local iteration log

## Current state — read before the history below (updated 2026-10-07)

- **Installed build: Dev 124.** Full CTest 56/56. Entries below are newest-first and all remain valid
  as history; the newest entry is the current one.
- **The one live, proven engine defect:** `TextureCache::DownloadImageMemory`
  (`textureCache.cpp:2002-2005`) publishes new guest backing from a deferred priority callback
  without making resident native buffers coherent. Reproduced by a failing standalone fixture
  (`--image-backing-bda-only`, deliberately NOT enrolled in the normal suite until corrected).
  **Ordering matters:** mark-dirty-then-write is wrong (a concurrent BDA sync in that window
  uploads the stale backing and clears the dirty bit). Write the backing, then perform a
  **no-flush** CPU-dirty transition. Do **not** call `InvalidateMemory` in the callback — it
  flushes via `ReadMemory`, which `EXIT`s for deferred operations (`bufferCache.cpp:354`).
- **Still-open review item:** scalar-mask branch correction is compute-only
  (`spirvEmitterFlow.cpp:693`). Needs a vertex-stage fixture. Unproven for UFC; wave64 graphics
  is not emulated, so removing the stage guard is not a fix.
- **Do not re-attempt these levers** — each was shadow-verified and rejected with evidence.
  Re-reading the corresponding entries below before proposing them again is expected:
  - **SRT memoization** (2.07% -> 3.24% hit rate at 64 slots; `shadow_mismatch=0` proved the
    mechanism correct but useless here). Per-draw SRT is irreducible. Raw evidence retained in
    `_Build/autolab/d095-srtmemo*`.
  - **BDA `BDA_SYNC_GENERATION` skip** — ~97-99% fast-path coverage but no robust FPS gain; stays
    opt-in/OFF (`bufferCache.cpp:77`, `UfcOptIn`). **Not the same thing as the base dirty log,
    which is ON by default** (`bufferCache.cpp:53`, `UfcSwitch`) despite stale comments and docs
    calling it opt-in. The base dirty log is a live corruption suspect, not a closed question.
  - **GPU-sourced indirect draws**, **GPU DCC / fill-convert**, **SRT multi-slot**, **relaxed
    readback** (banned: stale data).
- **30 FPS is not reachable by the current approach.** Measured fight FPS has sat at 17-20 across
  every lever tried, and the A/B runs are phase-confounded enough that they cannot resolve a real
  gain. Correctness first; revisit perf with clean baselines.
- **Process note:** the manual ground-and-pound / stretched-mesh check by the user has been
  outstanding for 40+ iterations. It is the only signal that can confirm a visual fix and should
  be treated as a gate, not a footnote.

---

## Dev 124 — completed image download leaves cached native buffer stale; focused reproducer — 2026-10-07, 02:28

Concrete failing-regression milestone on unchanged engine; no installed correction or FPS claim.
Only cycle source edit: ShaderRecompilerComputeTests.cpp standalone image-backing-BDA fixture.
Real Vulkan R32Uint clear, existing clean cached buffer, actual DownloadImageMemory deferred
WriteBacking, exact priority completion tick, native transfer checks without re-obtaining.
Worker focused `oc20261007-022425-tests` **0/1,3.27s**, after completed teardown:
backing/image-copy55667788,CPUdirty0,dirty-log native11223344,forced-full-walk11223344,
neighbor11223344,CPU invalidation/store control55667789. Producer, publication and ordinary
CPU update succeed; backing->cached-native notification is missing. No dropped log/poisoning.
Not a translated BDA consumer or live UFC resource correlation; no mesh/blackout culprit claim.

Initial fixture-only clear-union compile error in022354 fixed. Known-failing standalone
`--image-backing-bda-only` retained, NOT registered in normal suite pending correction.
Temporary CTest registration removed (not DISABLED/WILL_FAIL); no net cycle CMake edit.
Full worker `oc20261007-022612-tests` **56/56,29.39s**, excludes reproducer; scoped diff-check
passes. Build remains124, no engine/default/install change, no new gameplay needed/data.
Preflight no Kyty game/compiler/test, shell envempty; RunnerJob checked heartbeat each job.

Current reference only `_Build/autolab/d124-publication-control-framedump-b-20261007-020916`:
first95.48,observed324.99,6690frames/20.68FPS,opening45 17.08,median/min19/14,
RSS11183.3/VRAM12828MB,fgFalse,latecp84.92/other94.75/untagged7.07/write0.79ms/s.
No new visual clearance; prior124A and122 stretching remains positive; manual transient check
missing. Retain diagnostic reproducer ONLY; media/correct20/25/30 unmet, no goal file.
Fresh06:24UTC advisory30min unchanged soak/55tests not accepted; stronger real publication
defect now actionable. No scripts/config/agents/supervisor/commits/pushes or routine questions.

Next bounded engine correction: ordered image->resident-buffer publication or atomic backing/
tracker transaction, with pending/newer CPU/GPU-write/mixed-page/padding/lifetime/epoch tests.
No naive completion-thread InvalidateMemory (GPU-dirty neighbor can trigger forbidden readback).
Full walks cannot repair a change with no dirty bits. Register new regression normally with
the fix, full57+ then worker420-s acceptance; no more unchanged soaks to chase unmatched FPS.
Detailed report/replay: `_Build/agent-reports/dev124-image-publication-regression-20261007.md`.

## Dev 124 — bounded generation/log publication correction; 56/56; positive geometry failure — 2026-10-07, 02:18

Real dormant-path correction, not FPS/visual success. Changed cycle portions of bufferCache.cpp,
ShaderRecompilerComputeTests.cpp,CMakeLists.txt; previous source work/defaults preserved.
Generation mode accidentally fell through to default dirty-log synchronization on transitions;
post-take generation sample could also acknowledge an unconsumed write. Snapshot now precedes
take, idle requires complete/empty ranges, generation transitions force conservative batch walk.
No new default/submission/wait/quality change, opt-in still OFF. Fixture deliberately drops an
entry and proves actual native bytes/no-drain/neighbor and late acknowledged-generation behavior;
not proof of a live tracker omission or cause of Dev122 mesh corruption.

Worker failing old-engine regression `oc20261007-015820-tests`: stale native publication;
fixed focused `oc20261007-015916-tests` **1/1,0.41s**; full
`oc20261007-015937-tests` **56/56,30.61s**; standard `oc20261007-020018-build` installs124.
Scoped diff-check passes; ownership/env preflight repeated before every serial job.

`oc20261007-020054-fight` -> `_Build/autolab/d124-bdagen-publication-framedump-a-20261007-020116`:
generation ON,420.38s,first95.52,observed324.86,4957frames/**15.31FPS**,
opening45**16.72**,median/min17/1,RSS11129.4/VRAM12763MB.
After positive A, planned same-mode repetition replaced by necessary same-source switch control:
`oc20261007-020854-fight` -> `_Build/autolab/d124-publication-control-framedump-b-20261007-020916`:
generation OFF/default dirty log ON,420.47s,first95.48,observed324.99,6690frames/**20.68FPS**,
opening45**17.08**,median/min19/14,RSS11183.3/VRAM12828MB.
Both timeout/exit0,2560x1440/Mailbox,validation/red-zone/breadcrumbs/engine frames,
trace/dumps/Tracy OFF,fg=False,no GPU loss/driver/VUID/FATAL/ERROR,stderr empty/live watcher.
Waits last nominal30s ms/s A/B cp427.74/84.92,other479.97/94.75,untagged18.61/7.07,
write1.11/0.79. Final BDA fast/walk A402951/122584,B721464/19760 (76.67/97.33%fast),
generation skips and dirty-log calls not identical mechanisms. Metadata/indirect waits absent.

A50standing/80ground/140ground render, **A1602:00/1701:48/1751:44 R1 have huge stretched
skin/black fighter triangles with HUD**. Non-quiescent full walk did not clear the defect.
B120standing2:20,160ground1:08,175submission0:39,185ground0:21 render fighters, upper
background dark/speckling persist. Samples not visual clearance, manual transient check missing.
Late workloads differ: no matched FPS regression/improvement verdict; default-OFF correction
not established as artifact cause, earlier122 generation-OFF stretching remains evidence.
**Retain bounded regression-proven dormant correction; reject generation activation/visual/
performance acceptance**. No correct20/25/30/media goal or goal file. Fresh06:13:50UTC advisory
triage consumed, actual suite56 not55, its19.58FPS/stale-heartbeat claims not accepted.

Next GPU-image->buffer/backing publication fixture: retained native buffer/BDA consumer bytes,
deferred WriteBacking ownership/completion and epoch invalidation proof. Full walks already
failed to clear positive, no more unchanged soaks/generation FPS promotion. Independent current
readback lead cp84.92/other94.75ms/s requires pending-write/lifetime proof. No agents,
supervisor/script/config edits,commits/pushes; no routine input requested.
Report `_Build/agent-reports/dev124-bda-generation-milestone-20261007.md`.

## Dev 123 — owned graphics shader checkpoint metadata; 55/55; two worker fights — 2026-10-07, 01:54

Concrete engine diagnostic/publication correction, not a visual fix or speed claim.
Changed renderer gpuCheckpointHistory.h,gpuCrashDiagnostics.cpp/.h,context.cpp,render.h,
renderDraw.cpp and ResourceTrackingTests.cpp. Prepared compiled graphics-stage hashes,
native IDs/types/waves copied into every execution phase after resource restarts;
initial/non-draw checkpoints empty. Shared direct/indexed/auto/GPU-indirect/mesh/tess
path, no inherited annotation. Mutex-protected bounded history returns owned coherent
copies and refuses stale annotation; no added GPU work/wait/default/output changes.
Preserved existing source changes. Preflight clear before every job; worker-only execution.

Focused `oc20261007-013048-tests` resource_tracking **1/1,0.03 s**;
full `oc20261007-013201-tests` **55/55,33.20 s** (compute10.34,scheduler0.28);
standard `oc20261007-013248-build` installs123. Test proves four-stage/phase ownership,
source mutation independence, empty resets, compute isolation, zero/expired/wrapped
tokens and 50,000 concurrent publications/read copies. Scoped diff-check passes.

`oc20261007-013325-fight` -> `d123-graphics-checkpoint-nantrace-framedump-20261007-013347`:
600.12 s,first_fight142.74,observed457.38,8921 frames/**19.58 FPS**,median/min19/5,
opening45**15.01 FPS**,RSS11816.5/VRAM12934 MB. NaN trace/shader dump ON.
`oc20261007-014422-fight` -> `d123-graphics-checkpoint-framedump-repeat-20261007-014443`:
419.93 s,first95.50,observed324.43,6233 frames/**19.27 FPS**,median/min16/13,
opening45**14.54 FPS**,RSS11183.7/VRAM12807 MB. NaN trace/shader dump OFF.
Sessions under `_Build/autolab/`; both exit0/timeout,2560x1440/Mailbox,
validation/red-zone/breadcrumbs/engine frames,no Tracy,no GPU loss/driver events/errors,
live watch,**fg=False**. Not comparable focused performance controls/acceptance.
Last nominal30-s wait ms/s A/B:cp**22.41/75.83**,other**38.57/88.23**,
untagged**16.80/12.18**,write**1.75/1.79** (different lateR2/lateR1 workload).

Live A19/B14 graphics samples with nonzero hashes/IDs, PS-present/absent and
DrawIndex/Auto; A seq10207232 VS e6c549c4d345d6b0 program627,PS10e819a1a4f7f467
program626 match actual `.bin/.spv` dumps. Legacy shader_hash scalar stays compute-only;
draw stages are graphics[i], owned not post-fault memory. No TDR reproduced here;
report path at actual device loss remains unobserved. A22 completed trace-site reports;
no new inputs from prior positive2c4e/c44c/3438 group, no first-stamp culprit claim.

Reviewed A engine50/120/180/240/244 and B50/120/160/185: real standing/ground R1/R2,
fighters/HUD/arena render,speckling persists,B120 upper background dark. No large
geometry failure in selected shots; NOT clearance, manual transient check missing.
Dev122 trace-OFF stretching still valid positive. Retain diagnostic/publication fix
only; no verified media/correct20/25/30-FPS milestone, no measured FPS improvement.
Fresh05:49UTC progress advisory consumed but unmatched gain/event assertions rejected;
no input-schedule tuning or unchanged soak to chase0.42 FPS. No agents/scripts/config
changes/commits/pushes. Next: reproduce dirty-log GPU-image->buffer publication /CPU
write/BDA epoch missed upload with focused fixture, then fix; independent readback
work may use cp75.83ms/s with explicit publication/invalidation proof.
Report `_Build/agent-reports/dev123-graphics-checkpoint-milestone-20261007.md`.

## Dev 122 — timeout reconciled; unchanged trace-OFF control still has stretched geometry — 2026-10-07, 01:25

Necessary targeted diagnostic/control, no engine/test/default/save changes or rebuild.
Reconciled prior `d122-nantrace-input-framedump-20261006-183410`: 587.29 s,first_fight151.50,
435.79 detected fight s,3057 frames/7.03 FPS,median/min15/0,RSS11142.6/VRAM12881 MB,
timeout,nvlddmkm153x5,InstructionPointerUnknown. Engine140 black HUD/196 large black meshes
precede TDR; two DrawIndex suspects seq20469630/31 have zero hashes. Three selected wave64
VS completed input snapshots exist; no GPU resource contents/causal shader proof.

Worker `oc20261007-011229-fight` exit0 ->
`_Build/autolab/d122-tdr-control-framedump-20261007-011250`,600.89 s,timeout,no GPU loss/events,
unchanged122,2560x1440/Mailbox,red-zone/validation/breadcrumbs,engine frames,no shader dump/Tracy,
NaN tracer OFF (harness clears env; console no activation),a-p ON/dirty log ON,pool counters0.
Preflight no game/test/compiler, shell KYTY_UFC env empty, worker heartbeat current; branch dev.

**New positive:** engine120 R1 2:26 has long stretched skin/mesh strips during ground-and-pound;
126/128 render normally later. Standing40/submission100/finish130/replay140/results196/262
reviewed; speckling persists. This artifact occurs WITHOUT trace; not proof about prior
blackout/TDR causality. Shot132 black HUD at frame6975 occurs at fight-finish transition,
not verified sustained blackout. No immediate-load blackout in inspected fight-start image.

Raw summary first_fight93.57,detected507.32 s,6968 frames/13.77 FPS,median/min8/6,
RSS11458.9 MB,VRAM13097 MB. **Watcher keeps counting replay/results after KO**. Conservative
combat94.98->301.23:4523 frames/206.25 s/**21.93 FPS**; interpolated opening45 **15.76 FPS**.
Runner fg=False; scenario/cache/instrumentation/standing-ground differ, no gain/control claim.
Nominal30-s combat4106->4144 waits ms/s cp81.79,other93.74,untagged5.30,write0.70.
Final results4556->4571 instead cp27.58,other16.79,cpu-read17.04,srt-read12.15,untagged/write0.

Reused verified `oc20261006-183222-tests` **55/55,34.53 s**, resource_tracking0.02,
compute12.61,scheduler0.31; `oc20261006-183309-build` installed122. No fresh focused/full
tests claimed (no code changes). Changed only HANDOFF/LOOP-LOG/report.
**Retain existing candidate for diagnosis only, reject visual/stability acceptance**; manual
transient check missing, no verified media or correct repeated20/25/30-FPS milestone.
Next: real engine provenance correction attaching owned compiled graphics-stage hashes to
actual draw breadcrumbs (phase resets/direct/indirect/mesh), focused provenance/publication/
expiration regression and worker full suite + targeted shader dump. Only compute currently
annotates hash. No post-crash live memory lookup, clamps, skips, rollback, commits or agents.
Fresh progress advice recommends this control; completed, no additional unchanged soak.
Report: `_Build/agent-reports/dev122-tdr-control-milestone-20261007.md`.

## Dev 121 — completion-backed NaN event/output records, 55/55, two qualifying diagnostic fights — 2026-10-06, 18:29

Concrete engine milestone: replace ambiguous per-site stamp-only attribution with a recording
token + raw output float4 owned by the single CAS winner. Changed `NanTrace.{cpp,h}`,
`nanTraceSnapshot.h`, `render.h`, `pipeline/{descriptors,pipelineCache}.cpp`,
`ShaderRecompiler.cpp`, `ir/ShaderIR.h`, `ir/passes/BindingLayout.{cpp,h}`,
`spirv/{SpirvEmitter,spirvEmitterModule,spirvEmitterMemory}.cpp`, and
`{ResourceTrackingTests,ShaderRecompilerComputeTests}.cpp`. Snapshot completion/barriers unchanged;
trace-only word31 reservation spills guest data to existing storage path and preserves mesh/
viewport words. Bounded history validates IDs instead of attributing recycled records.
No extra submit/wait, clamp/output/default change. Diagnostic remains opt-in/OFF normally.

Initial test-only namespace/link coupling failures corrected using explicit allocator capacity,
without CMake changes. Worker focused `oc20261006-180149-tests` scheduler **1/1**, 0.46 s;
`oc20261006-180252-tests` resource tracking **1/1**, 0.03 s; full
`oc20261006-180312-tests` **55/55, 32.49 s**; standard build `oc20261006-180353-build`
installs **Dev121**. Gated GPU snapshot, CAS token/raw ownership, later-event non-overwrite,
64-layer unchanged output, history expiration and exact/overflowing push banks covered.
Scoped tracked-file diff check passes. Existing work preserved; no automation/supervisor edits.

A `oc20261006-180443-fight`, session `d121-nantrace-event-framedump-20261006-180505`,
shader dumping ON: **601.06 s**, first fight **406.92 s**, observed fight **194.14 s**,
**3366 frames / 17.51 FPS**, interpolated opening45 **16.17 FPS**, fight median/min **16/9**,
RSS **11637.9 MB**, VRAM **13540 MB**. B `oc20261006-181533-fight`, session
`d121-nantrace-event-framedump-repeat-20261006-181553`, shader dumping OFF: **420.50 s**,
first fight **157.88 s**, observed fight **262.62 s**, **4827 frames / 18.48 FPS**,
opening45 **14.44 FPS**, median/min **16/8**, RSS **11119.3 MB**, VRAM **12721 MB**.
Both 2560x1440/Mailbox launch, validation/red-zone/foreground ON, trace+engine frames,
no Tracy, Makhachev/Della Maddalena; exit0/timeout, live watcher, no GPU loss/driver events,
empty stderr. Fight zero samples=0; overall zero samples are loading, not fight blackout.
Nominal last30-s wait ms/s A/B: cp_read_fault **71.13/68.49**, other_threads **88.42/88.02**,
gpu_untagged **12.80/13.63**, cpu_write_flush **1.48/1.39**. Not an A/B/FPS gain; cold
loading/ground timing differs. Existing 3840x2160 guest frame source, no quality cuts.

A18/B20 completed NaN/Inf reports all have matched nonzero event IDs, none expired.
Repeated CS `8dbc785aee6c1b3d` binding19 NaN/Inf now link to a single actual recording
event (A9928081 / B889099), not its later polling frame. B VS `7863cceac4a3fa7e`
position NaN event238437: recorded frame729/tick137932/submit123917, DrawIndex phase100,
54 indices/1 instance, raw `7fffffff/7fffffff/0/3f800000`; pre-fight first hit is not a
fighter-corruption culprit. Event debug `address` is op-dependent (graphics first-instance),
not full resource provenance. Binding token does not distinguish multi-draw subdraws.

Inspected A150/165/180/220 and B65/90/120/155: fighters, HUD, arena, standing and ground
render; skin/mat/cage speckling remains. No large failure in those inspected images, **not
visual clearance**, manual transient-artifact check missing; neither reaches the old late-R2
positive. Retain **opt-in diagnostic only**; media and correct repeated 20/25/30 FPS unmet.
Next bounded correction: event-selected immutable SGPR/resource-descriptor inputs and output
format/components for the positive Dev120 VS group (`2c4e5b0c45d615eb`, `c44c878e59f9e5ff`,
`343814752fbf40a9`), preserving the late-R2 lead. No unchanged soak/first-stamp accusation,
no clamps/skipped work, no commits/pushes. Report:
`_Build/agent-reports/dev121-nantrace-event-milestone-20261006.md`.

## Dev 120 — completion-backed NaN snapshots + atomic slot reads, 55/55, two diagnostic fights — 2026-10-06, 17:53

Concrete milestone: fix the tracer's live mapped-buffer host race and plain GPU slot-load
race before assigning causality to shaders. Added bounded `renderer/nanTraceSnapshot.h`;
updated `renderContext.{cpp,h}`, `NanTrace.{cpp,h}`, `spirvEmitterMemory.cpp`, and
`ShaderRecompilerComputeTests.cpp`. Queue-ordered barriered transfer, timeline-completed
host invalidation, immutable owned words; refuse reuse while pending. No extra submit/wait
or output changes. Atomic slot reads match atomic publication. Frame/tick identify the
snapshot boundary only, counter stamps are not causal GPU-event order. Opt-in tracer stays
OFF absent `KYTY_UFC_NAN_TRACE=1`; other engine/default/uncommitted work preserved.

Worker focused `oc20261006-173052-tests` **1/1** (command_scheduler_timeline 0.48 s);
full `oc20261006-173205-tests` **55/55, 37.06 s**; standard build
`oc20261006-173307-build` installs **Dev 120**. GPU fixture verifies gated incomplete
completion, unsubmitted rejection, pending-slot protection, same-submit later writes,
owned snapshot reuse, valid atomic-read SPIR-V, 64 contending layered fragment exports
and unchanged NaN/Inf/finite outputs. Scoped tracked-file diff check passes.

Execution A: `oc20261006-173410-fight`, session
`_Build/autolab/d120-nantrace-snapshot-framedump-20261006-173431`, exit 0,
600.54 s, first fight **226.12 s**, observed **374.42 s**, **3639 frames / 9.77 FPS**,
opening45 interpolated **16.32 FPS**; fight median/min **15/1**, RSS **11429.7 MB**,
device VRAM **13178 MB**. Snapshot logs **51** sites. Engine 145/170/207 show large
black/stretched geometry with live HUD; 75/130 show rendered ground fighting. Last ~30-s
wait ms/s: cp_read_fault **658.87**, other_threads **610.73**, gpu_untagged **13.85**,
cpu_write_flush **0.72**. Shader dumping ON; cold pipeline creation cumulative ~88.4 s.

Execution B: `oc20261006-174458-fight`, session
`_Build/autolab/d120-nantrace-snapshot-framedump-repeat-20261006-174519`, exit 0,
421.02 s, first fight **163.15 s**, observed **257.87 s**, **4431 frames / 17.26 FPS**,
opening45 **13.48 FPS**; fight median/min **15/7**, RSS **11204.1 MB**, device VRAM
**12895 MB**. Snapshot logs **18** sites. Engine 90 standing and 155 ground render;
speckling remains, no large failure in those inspected images. Last ~30-s wait ms/s:
cp_read_fault **79.03**, other_threads **89.76**, gpu_untagged **8.06**, cpu_write_flush
**1.31**. Shader dumping OFF, otherwise same capture settings/fighters and no Tracy.

Both runs: 2560x1440/Mailbox launch, validation/red-zone/breadcrumbs ON, live watcher,
no device loss/driver events. Engine dumps have existing 3840x2160 source; no resolution
or quality changes. These are instrumentation/scenario-divergent measurements, not
an FPS gain/control. Existing Dev 119 already captures corruption, but this diagnostic
does not certify visual acceptance or establish a patch-regression/culprit verdict.
**Retain only the synchronization correction.** Media/25/30-FPS goals unmet; manual
transient-artifact check missing. Next separate milestone: exact dispatch/draw/resource
identity for completed trace records and actual VS position-output inputs (A snapshot
1177 tick315494 / 1646 tick467322), retaining the late-R2 five-site lead as well.
No further unchanged soak needed for a positive; no clamps/skipped work/commits/pushes.
Report: `_Build/agent-reports/dev120-nantrace-snapshot-milestone-20261006.md`.

## Dev 119 — completed NaN/frame-dump diagnostic: late R2 black shapes, live watcher, bounded five-shader lead — 2026-10-06, 17:25

Explicit Dev 119 restart in the active overnight plan supersedes the historical stop
entry below. One necessary diagnostic/verification milestone, engine unchanged; no
build/install or default edits. Worker `oc20261006-171124-tests` **55/55, 35.77 s**;
compute regression 14.19 s. Previous watch-publication focused tests were not rerun
standalone here; actual live watcher survival was verified instead.

First capture `d119-nantrace-engine-review-20261006-170242` ended at 374.94 s with
dead watcher and no device loss. Its final image visibly shows R1 fighting, contradicting
the stale `fight_detected=False`. Do not infer a no-fight/no-corruption control from it.

Ownership check found the worker already running follow-up `oc20261006-171138-fight`;
consumed it, no duplicate launched. Session
`_Build/autolab/d119-nantrace-framedump-followup-20261006-171221`, exit **0**, **600.14 s**,
2560x1440/Mailbox launch, validation/red-zone ON, NaN trace + engine dumps, no Tracy.
No watcher traceback; final watch snapshot still detects fight and says continue.
First fight **136.35 s**, observed **463.79 s**, **8578 frames / 18.54 FPS**,
opening interpolated 45-s throughput **14.06 FPS**, fight median/min **18/4**, no zero-FPS
fight samples. Max RSS **11703.6 MB**, device VRAM **13379 MB**. No device loss/driver events.
Last ~30-s counter interval: cp_read_fault **30.49 ms/s**, other_threads **40.48**,
gpu_untagged **11.86**, cpu_write_flush **1.77**; no metadata/indirect-specific waits.

**Positive visual failure:** inspected engine frames 240–244 intact R2 standing,
245 black shapes near head/arms/cage, 246–247 larger black silhouettes with live HUD;
desktop shots 258/260 corroborate around 593–598 s. Engine 60/150 shows earlier ground
fighting; 220 the round transition. Five new NaN sites logged between dumps 242/243:
CS `e17349e0d437757b` binding12 and `3c6058d66788f544` binding2; PS `654607b31fe5f6d6`,
`487864de9705a169`, `811633d84dc57d64` MRT0. All guest/SPIR-V dumps available in the first
session. Polling guest frame 5243 is not native title/metrics frame; no causal verdict.
Partial corruption did not trigger the watcher's fatal blackout detector.

**Decision:** retain existing code unchanged; visual acceptance fails. This is a diagnostic
not an improvement measurement, and not immediate-load timing reproduction. No movie-decoder
proof, repeated correct 25/30-FPS milestone, or manual transient-artifact clearance.
**Next concrete engine correction:** trace publication/synchronization (live mapped-buffer
host Poll currently lacks completion proof; plain GPU slot loads race with atomic stores),
with a producer/consumer regression, then exact draw/dispatch and resource provenance for
the five-site group. Do not clamp/skip work or collect another unchanged soak unnecessarily.
Only HANDOFF/LOOP-LOG/report changed; no commits/pushes/rollback/supervisor changes.
Report: `_Build/agent-reports/dev119-nantrace-visual-correlation-20261006.md`.

## User stop and reconciled evidence review — 2026-10-06

User explicitly requested stop, then an update. STOP marker written; all relevant project
processes were absent at inspection, queue empty, supervisor system power request cleared.
Stale state now says stopped by user (last recorded cycle 92). No engine edits, rebuilds,
tests, games, source rollback or commits occurred in the review. Do not restart automatically.

Read `../LOOP-STOP-REVIEW-20261006.md` for the actual record. At the initial stop inspection,
installed binary and counter were **Dev 116**, despite stale top notes/experiment labels.
Build `oc20261006-152941-build`
passed, full `oc20261006-153010-tests` **55/55, 34.11 s**. Source includes cycle-92 fine-grained
compute/map/extract-plan zones and EquivalentValue pointer-pair hash-set membership. Final
game `d118-equiv-hashset-20261006-153120` exited at 293.35 s without logged device loss or
fight detection; retention/performance acceptance was not completed.

Independent profile check corrects the prior next-target hypothesis: Shader::ExtractPlan is
the multi-second leaf (Dev 115 7.031 s total/4.878 s max; Dev 116 6.863 s/4.739 s, 87 calls
each), while PrepareProgram/MapLookup are short. This does not establish a gain; the stall
persists. The detected Dev 115 24.73-FPS interval lasted only 57.25 s, with Tracy enabled,
and therefore fails the >=120-s/repeated gameplay criterion. Media/25-FPS objectives remain
unverified; real late-fight black/mesh corruption and occasional TDR remain unresolved.
Useful retained work includes engine-frame capture, 55-test coverage, targeted regressions,
thread/dispatch/image/BDA profiling and opt-in experiments. Preserve all evidence and source.

Later in this review, Explorer launched a new worker (15:48) and a manual launcher/game
(15:58) showing Dev 117. These are separate activity, not resumed OpenCode cycles; STOP
still present, no autonomous supervisor/agent running. No manual/new-worker process was
terminated. The stopped-loop report covers through Dev 116; later Dev 117 is not certified here.

## Dev 114 binary — compute-dispatch attribution: `CpOpDispatchDirect` is program-lookup/SRT-prep dominated with rare multi-second `GetComputeProgram` stalls, not descriptor re-setup (diagnostic + retained zones) — 2026-10-06, 15:20

One bounded diagnostic milestone (overnight plan item 2), one behavior-neutral engine change:
fine-grained Tracy sub-zones were added to the compute-dispatch host path in
`src/graphics/host_gpu/renderer/renderCompute.cpp` (`RenderExecutor::DispatchDirect` and
`DispatchIndirect`): `CS::GetProgram`, `CS::FillChecks`, `CS::PrepareBindings`, `CS::PrepareBda`,
`CS::Rebind`, `CS::ObtainArgs`, `CS::PostBarrier`. No control-flow/semantic change; zones are
inert unless the profiler is attached.

Build `dev117-cs-dispatch-zones` → installed **UFC Dev 114** (build-iteration 113→114). Full
CTest **55/55** (`oc20261006-150827-tests`). Targeted from-launch capture
`d117-csdispatch-tracy-framedump` (job `oc20261006-150928-fight`, exit 0, 420.04 s,
2560x1440/Mailbox, validation/red-zone ON, a–p ON, `framedump`, screenshots 1 s), session
`_Build/autolab/d117-csdispatch-tracy-framedump-20261006-150951`, Tracy 29.07 s + 60 s.

**Attribution (Tracy window 60 s):**

| zone | calls | total | mean | max |
|---|---|---|---|---|
| `CpOpDispatchDirect` | 252,304 | 10.694 s | 42.4 µs | **4.691 s** |
| `CpOpDispatchIndirect` | 20,036 | 3.962 s | 197.7 µs | **2.110 s** |
| `CS::GetProgram` (direct) | 252,304 | 6.096 s | 24.2 µs | **4.690 s** |
| `CS::GetProgram` (indirect) | 20,036 | 2.994 s | 149.4 µs | **2.109 s** |
| `CS::PrepareBindings` (direct) | 240,060 | 1.248 s | 5.2 µs | 15.1 ms |
| `CS::Rebind` (direct) | 240,060 | 1.379 s | 5.7 µs | 3.8 ms |
| `CS::FillChecks` | 182,315 | 0.153 s | 0.84 µs | 1.0 ms |
| `CS::PostBarrier` | 240,060 | 0.103 s | 0.43 µs | 68 µs |
| `CS::ObtainArgs` (indirect) | 20,036 | 0.0024 s | 0.12 µs | 7 µs |

Combined dispatch zones are **14.66 s / 60 s (24.4 %)**; `CS::GetProgram` is **9.09 s = 62 %**
of that (57 % of direct, 76 % of indirect). No `CS::PrepareBda` zone fired (no DMA dispatches).

**Key correction to Dev 116's read:** the ~42–50 µs *mean* is dominated by a handful of
multi-second `GetProgram` stalls (direct max 4.690 s, indirect max 2.109 s — nearly equal to the
whole `CpOpDispatchDirect`/`CpOpDispatchIndirect` max, so the entire stall is inside
`GetComputeProgram`). Excluding the single direct max, the typical direct dispatch is
**≈23.8 µs** and `CS::GetProgram` **≈5.6 µs**. The stall is **not** translation
(`Shader::TranslateProgram` 536 calls, 1.85 s total, max 128 ms) and **not** SRT
materialization (`Shader::MaterializeResources` compute 272,179 calls, 1.58 s, max 0.15 ms) —
both zoned and small. It lies in the un-zoned `PrepareProgram` step
(`ShaderGetMappedData` mutex+find / `ShaderGetStaticInputInfoCS`, which does no guest reads).
Descriptor/pipeline re-setup is therefore **not** the dominant compute cost
(`PipelineCache::CreatePipeline(Compute)` only 0.067 s).

**Correctness/visual:** `gpu_device_lost=False`, no driver events, breadcrumbs clean
(`recorded=20,856,832 started=completed=20,856,666`); engine frames render content through the
run (frame_000003 mean 35, frame_000100 mean 69, frame_000168 mean 84; only frame_000000 and the
load-transition frame_000030 are fully black — same as Dev 116). No errors except the benign
`SPIR-V validation cache: 799 known-valid shaders`. `fight_detected=False` is the known
`framedump` FightWatch screenshot false-negative; **desktop shots remain black, manual live check
still required**.

**Observation (not attributed):** this 420 s run accumulated far larger CPU-side waits than
Dev 116's 241 s run — final `cp_read_fault=4362/143,106 ms`, `other_threads=6278/153,332 ms`
(vs 6,997 / 13,217 ms at 241 s), with `gpu_device_lost=False` and clean breadcrumbs, so it is
CPU/thread wait accumulation, not a GPU hang; treat late-run FPS as scenario-divergent.

**Decision:** retain the sub-zones (behavior-neutral, 55/55, inert without profiler). No
FPS/25-FPS/media claim. Report: `_Build/agent-reports/dev114-csdispatch-attribution-20261006.md`.

**Next:** localize the multi-second `GetComputeProgram` stall — add a zone around
`PrepareProgram` / `ShaderGetMappedData` (the only un-zoned step large enough) and test whether
it is `g_shader_map_mutex` contention under many guest CPU threads; if so, replace the global
mutex+hashmap with a sharded/read-mostly map. Alternatively, if the stall is scheduling noise,
continue with the even ~24 µs per-dispatch components.

## Dev 116 — launch-window capture: gameplay renders; load-into-gameplay "blackout" is a ~24 s 1–7 FPS window (diagnostic) — 2026-10-06, 14:30

One bounded diagnostic, **no source/default change**, on unchanged **UFC Dev 113**. The prior
launch capture (`d094-load-media-diag`) started Tracy at 45 s and used desktop `PrintWindow` shots
(always black for the Vulkan client). This run captures from launch with **engine presented-frame
dumps** (`framedump`) so rendered output can be judged independently of the compositor.

Worker job `oc20261006-141918-fight`, exit 0, 241.56 s; session
`d095-load-intro-diag-framedump-20261006-141940`; 2560x1440/Mailbox, ShaderValidation/red-zone ON,
a–p ON, `framedump`+`shader_dump`, screenshots every 1 s; Tracy 29.01 s + 50 s; 104 engine frames,
1619 shader files. Triage: no errors, no SPIR-V validation failures, `gpu_device_lost=False`, no
driver events, breadcrumbs clean; `SPIR-V validation cache: 799 known-valid shaders`; shader builds
translate 2749 / compile 1343 / validate 3 / module 413 ms for 866 pipelines (~4.5 s one-time).

**Engine frames render every phase correctly:** EA Sports montage (~10 s), UFC 5 logo (~24 s),
character select with HUD (~48 s), arena walkout lit (~81 s), ground-and-pound fight with HUD
(~144 s, ~167 s). Fully black frames only at the first presented frame (~2 s) and one
load-transition frame (~80 s). **No video-decoder activity logged** (only MP3 audio); the
automation does not exercise the movie path.

**FPS (`metrics.csv`):** 54–61 boot/menus (2–29 s) → 9 (31 s, first load) → 19–28
(character-select/intro, 33–53 s) → **VRAM 7.7→10.8 GB at 54.8 s → 1–7 FPS for ~24 s (56–80 s, the
load-into-gameplay black/dark window)** → 14–17 (82–133 s) → 14–30 fight (sustained 24–30 around
152–190 s) → 15–21 (192–241 s). Median 19, min 1, max RSS 11,183 MB, VRAM ~12.3 GB.

**Load-window attribution (Tracy 29–79 s, includes the stall):** `DrawIndex` 28.9%,
`PrepareDrawRenderState` 15.2%, `ExecutePreparedDraw` 14.3%, `RefreshShaders` 13.3%,
`CpOpDispatchDirect` 11.7% (185,801 dispatches, mean **50 µs**), `MasterSemaphore::Wait` 11.1%,
`PrepareGraphicsBindings` 9.4%, `SrtRefreshFlatBuffer` 8.5%, `Draw::PrepareBda` 7.1%,
`BufferCache::ReadMemory` 6.4% (6,590 calls, mean 770 µs, max 136 ms), image create+delete ~7.3%.
Cumulative waits `cp_read_fault=6997 ms`, `other_threads=13217 ms`; BDA fast path 96.9%
(`fast=287222 walk=9120`, `verify_missed=0`); `image pool hits=0 misses=0` (pool OFF).

**Conclusion:** the user's "blackout immediately on loading into gameplay" reproduces as the
**~24 s near-zero-FPS load/transition window (56–80 s)** between character select and the arena
walkout — **not** a persistent rendering failure; no single lettered feature is implicated.
Desktop `PrintWindow` shots remain black (known artifact); engine frames are the only reliable
visual evidence, so a **manual live check remains required**, and movie playback is unverified.

**Decision:** no engine/default/script change; no 25/30-FPS or media claim. Red-zone, validation,
wave32 mask branches, BDA batch and runtime viewport preserved. Report:
`_Build/agent-reports/dev116-load-intro-diag-20261006.md`.

**Next:** attribute the 56–80 s load window — whether the ~50 µs `CpOpDispatchDirect` cost
(per-dispatch pipeline/descriptor re-setup) or CPU read-fault drains can be reduced with a bounded,
shadow-verified change; else continue the fight-window P4 work. Confirm movie playback only via
the manual path.

## Dev 115 — clean BDA dirty-log (c) ON/OFF A/B: 97–98% walk cut, early-window +11–16% FPS (acceptance) — 2026-10-06, 14:20

One acceptance-check milestone, no source/default change. Dev 114 proved the dirty-log fast path
(`KYTY_UFC_BDA_DIRTY_LOG` = feature **c**) causes no tracker-visible missed upload; this cycle
measures its FPS benefit with the required repeated alternating controls: four 300 s runs on
unchanged **UFC Dev 113**, 2560x1440/Mailbox, ShaderValidation/red-zone ON, `framedump`, a,b,d–p ON.
Pair 1 cON→cOFF, pair 2 cOFF→cON.

| run | label | session | exit | `bda sync` |
|---|---|---|---|---|
| A  | `d115-cab-a-con-framedump` | `...-135116` | 0, 300.99 s | fast=432884 walk=13506 (**97.0%**) |
| B  | `d115-cab-b-bis-c-framedump` | `...-135646` | 0, 301.16 s | fast=0 walk=413431 (0%) |
| B2 | `d115-cab-b2-bis-c-framedump` | `...-140252` | 0, 301.38 s | fast=0 walk=315177 (0%) |
| A2 | `d115-cab-a2r-con-framedump` | `...-140912` | 0, 301.39 s | fast=607106 walk=13815 (**97.8%**) |

(A2's first attempt `...-140825` was an exit-321 duration-0 guest-memory launch failure — retried,
not data.) `gpu_device_lost=False`, no driver events, all four.

**Mechanism:** fast-path coverage 97.0–97.8% with c ON vs 0% with `bis-c`; `verify_missed=0` both
c-ON runs.

**FPS (metrics.csv `frame` deltas, cON mean vs cOFF mean):** 60–105 s 10.03 vs 8.65 (**+15.9%**);
110–145 s 15.62 vs 14.07 (**+11.0%**); 150–195 s 16.27 vs 19.77 (−17.7%); 200–245 s 21.61 vs 26.06
(−17.1%); 250–300 s 24.91 vs 17.48 (+42.5%); whole 60–300 s 17.98 vs 17.60 (**+2.2%**). The
early-fight windows favor c ON in **both** alternating pairs; the later windows are dominated by
scene/workload divergence, so no robust whole-run gain is claimed. `fight_detected=False` is the
known framedump FightWatch screenshot false-negative.

**Correctness:** a late-fight dark engine-frame phase (dark-fraction ~0.40–0.45) appears with c
**both OFF (B frames 111–131, B2 125–132) and ON (A2 intermittent)** → not c-specific, further
weakening the Dev 108 association. **User manual mesh-explosion/ground-and-pound check remains
outstanding.** RSS 11,063/11,001/11,177/11,169 MB.

**Decision:** no source/default/script change; keep the run-level `bis-c` quarantine (harness rule:
manual visual check outstanding). No 25/30-FPS or media claim. Report
`_Build/agent-reports/dev115-ca-dirtylog-ab-20261006.md`.

**Next:** (1) if the manual check clears, drop `bis-c` from acceptance runs and re-baseline;
(2) else next measured P4 zone (`RefreshShaders`/`PrepareGraphicsBindings`) with shadow proof;
(3) media/intro early-window attribution (plan item 2) — shader-build totals here are translate
2408 / compile 723 / validate 3 / module 28 ms for 868 pipelines, so first-use translation is a
~2.4 s one-time cost, not a sustained-FPS lever.

## Dev 114 — BDA dirty-log fast-path verify: no tracker-visible missed upload (P2 negative) — 2026-10-06, 13:50

One bounded P2 diagnostic, no source/default change. Dev 107's fighting profile puts
`Draw::PrepareBda`/`SynchronizeBuffersInRanges` at ~6.6% of all traced CPU (~33% of the command
thread, 119 µs/call) with the dirty log (c) OFF. Two worker-owned 420 s runs on unchanged Dev 113,
2560x1440/Mailbox, validation/red-zone ON, `framedump`, d–p ON, a/b OFF (to separate the dirty-log
path from the blackout suspects): `d114-dlverify-bis-ab-framedump-20261006-132944` (c ON +
`KYTY_UFC_BDA_DIRTY_VERIFY=1`) and `d114-dlon-bis-ab-framedump-20261006-133851` (c ON, verify OFF).

**Result:** run 1 `bda sync fast=817642 walk=8629 verify_missed=0 verify_raced=171759`; run 2
`fast=653022 walk=11550 verify_missed=0 verify_raced=0`; the `BDA dirty log MISSED` line fired **0**
times. Fast path covered 98.3–99.0% of syncs (vs 75.5–81.9% for Dev 112 `bdagen`);
`VerifyBdaDirtyLog` found no CPU-dirty page absent from the log across 1.47M fast-path syncs. **The
historical dirty-log missed upload is not reproduced as a tracker-visible miss on Dev 113** — stop
re-running the same oracle. No FPS claim (duration/a-b/verify differ; Windows shots still black,
engine frames render). Sampled median 14/20; max RSS 11,050.5/11,279.1 MB. Report:
`_Build/agent-reports/dev114-dirtylog-verify-20261006.md`.

**Incidental:** both runs show transient giant black mesh geometry / an all-black engine frame near
end-of-fight (run 1 frames 180–185 dark 0.51–0.73, clean at 160; run 2 frame 181 fully black, 186
0.46). Not attributed: the 300 s c-OFF `framedump` baselines end before the ~405–420 s artifact window
(duration confound), and one historical 420 s c-OFF run also showed it.

**Decision:** no engine/default change; keep c default ON with the `bis-c` quarantine; no promotion
(user manual visual check outstanding). No 25/30-FPS or media claim. **Next:** 420 s c-OFF `framedump`
control at matched duration/a-b state; if c is exonerated, repeated 300 s c ON vs c OFF same-settings
A/B for the 99% walk reduction (gated on the manual check); otherwise a fast-path upload-set coverage
self-check; independent media/intro early-window attribution.

## Dev 113 — multi-slot SRT memo: shadow-verified but per-draw SRT is irreducible (rejected lever) — 2026-10-06, 13:35

One bounded P4 engine experiment. The d094 early capture (45–105 s) attributes `MaterializeResources`
≈ 13.7 s / 60 s (SRT `RefreshFlatBuffer` 10.6 s) — the largest single command-thread zone — while
`KYTY_UFC_SRT_MEMO` reused only 2.1% of calls (Dev 113 control `reused=268187 inputs=12459340`).
Hypothesis: the memo's single slot is clobbered by interleaved keys, so a bounded multi-slot
per-source memo would reuse more. Implemented `KYTY_UFC_SRT_MEMO_SLOTS` (default **1** = original
single slot; 8/64 opt-in) with the same `ReplaySrtReads` invalidation proof and a shadow mode.
Files: `src/graphics/host_gpu/renderer/pipeline/pipelineCache.cpp`, `AutoFight.ps1` (labels
`*srtmemoslots*`=8, `*srtmemoslots64*`=64, `*srtmemoslots-shadow*`=shadow verify).

Build **oc20261006-125918-build = UFC Dev 113** installed; focused
**resource_materialization/resource_tracking/shader_cfg/scalar_provenance/shader_recompiler_compute
5/5**; full **tests 55/55 (34.24 s)**.

Three 300 s runs, Dev 113, 2560x1440/Mailbox, validation/red-zone ON, a/b,d–p ON, c OFF, `framedump`;
all `gpu_device_lost=False`, no driver events, no post-intro dark engine frame:

| run | slots/mode | reused | inputs | reads | shadow_mismatch | hit rate |
|---|---|---|---|---|---|---|
| `d095-srtmemodefault-bis-c-framedump-20261006-131525` | 1 / 1 | 268192 | 12694369 | 1 | 0 | **2.07%** |
| `d095-srtmemoslots-shadow-bis-c-framedump-20261006-130051` | 8 / shadow | 262361 | 11798930 | 6547 | **0** | **2.18%** |
| `d095-srtmemoslots64-shadow-bis-c-framedump-20261006-130726` | 64 / 1 | 400835 | 11877275 | 79494 | 0 | **3.24%** |

**Finding:** 1→8→64 slots moves the hit rate only 2.07%→2.18%→3.24%; even a 64-slot
(memory-costly) cache adds ~1.2% of calls. `reads` (a slot matched inputs but a logged guest read
replayed differently) climbs 1→6547→79494, so the SRT guest data itself changes between frames and
the materialization is genuinely per-draw work, not redundant. `shadow_mismatch=0` on every reuse
proves the multi-slot reuse was exact — the mechanism is correct, just not useful here. FPS is not
comparable across runs (phases differ: 60–300 s 13.87 / 12.31 / 17.30 FPS); the ~1% call delta is
far below run noise, so no FPS claim.

**Decision:** keep `KYTY_UFC_SRT_MEMO_SLOTS` **opt-in / default 1** (default counters identical to
the Dev 113 control → baseline unchanged); do not promote; stop investing in cross-draw SRT reuse.
No default behavior changed; red-zone/validation/wave32/BDA batch/runtime viewport preserved. No
25/30-FPS or media claim. Manual ground-and-pound / mesh-explosion check still outstanding.

**Next:** per-draw SRT is irreducible; the other large cost is BDA sync (`Draw::PrepareBda` ≈ 7.3 s /
60 s, 51 µs/call) — attack the walk cost (dirty-log P2 missed-upload root cause / dirty-buffer index),
or run the plan item-2 early-window attribution on Dev 113.

## Dev 112 unchanged — clean `bdagen` A/B acceptance: skip works, FPS gain not robust, no blackout — 2026-10-06, 12:50

One acceptance-check milestone (no source change): the Dev 112 next step — two clean
`KYTY_UFC_BDA_SYNC_GENERATION` runs (label `bdagen`, **verify OFF** so the skip fires) plus two
matched `bdagen`-OFF controls, alternating order, Dev 112, 2560x1440/Mailbox, a/b,d–p ON, c OFF,
`framedump`, 300 s each. Sessions A1 `d113-bdagen-clean-a-bis-c-framedump-r2-20261006-122446`,
A2 `d113-bdagen-clean-a2r-bis-c-framedump-20261006-124516`, B1
`d113-control-clean-b-bis-c-framedump-20261006-123025`, B2
`d113-control-clean-b2r-bis-c-framedump-20261006-123818`. Report:
`_Build/agent-reports/dev113-bdagen-clean-ab-20261006.md`.

**Mechanism:** gate fires as designed — A1/A2 `fast/walk = 414484/134617` and `482246/106672`
(75.5%/81.9% skip) vs B1/B2 `0/356067`, `0/332932` (all walk); walks −62–70%.

**FPS (matched `metrics.csv` frame windows, FPS A-mean vs B-mean):** 60–105 s 10.74 vs 8.13
(+32%); 110–145 s 16.34 vs 12.80 (+28%); 150–195 s 16.79 vs 16.53 (+2%); 200–245 s 22.87 vs 26.46
(−14%); 250–300 s 20.34 vs 21.74 (−6%); whole 60–300 s 17.68 vs 17.56 (+0.7%). Trajectories:
`bdagen` leads by +270–290 frames at 160–200 s, then the last ~60 s erases it. Span-restricted:
100–200 s +13.3%, 100–240 s +3.0%, 100–300 s −1.7%. **The gain is phase-dependent, not robust;
Dev 108's single-pair +19.8% matched-window is not reproduced as a stable effect.**

**Correctness:** engine-frame darkness — both `bdagen` runs have only 3–4 near-black frames, all
the same startup/loading frames (indices 0–31) as the controls, longest run 2–3. **No sustained
post-intro blackout in either `bdagen` run** (weakens the Dev 108 association; now 3 clean vs 2
black historically); the user manual ground-and-pound check remains outstanding.

**One pre-existing GPU TDR** on the A2 first attempt (`d113-bdagen-clean-a2-...-124347`), 30.4 s:
`gpu_loss_class=timeout`, nvlddmkm:153, breadcrumbs `DrawIndex` ×7, fault `WriteInvalid` addr 0 —
P1 baseline fault also seen on unchanged Dev 044 source; retried (A2r). Two launch failures
(exit 321 / duration 0, guest-address reserve) were retried, not data.

**Decision:** keep `KYTY_UFC_BDA_SYNC_GENERATION` **opt-in / default OFF; do not promote** — the
FPS benefit is not robust and the manual visual check is outstanding. No source/default change.
No FPS or 25/30-FPS claim. Last-30 s waits: `cp_read_fault` 610–1660 ms, `other_threads`
1040–2044 ms, `gpu_untagged` 24–441 ms. **Next:** pivot to the intro/media window (60–105 s is the
slowest segment at 8–11 FPS) with an early `--tracy-start-seconds` attribution on Dev 112, or begin
the measured P4 per-draw work (`PrepareGraphicsBindings` 7.4%, `RefreshShaders` 4.8%) with a
shadow-verified cache and invalidation proof. Light phases already exceed 25 FPS (B2 25–28 FPS at
253–280 s; 200–245 s window 21–26 FPS), so the engine can pass 25 FPS; the average is dragged by
heavy phases.

## Dev 112 — post-oracle generation recheck: all oracle hits are races, verify_missed=0 — 2026-10-06, 12:30

One decisive diagnostic that settles Dev 110 vs Dev 111. Moved per-hit counting out of the oracle:
`BdaSkipWouldMissUpload` returns the CPU-dirty span count and no longer calls `WaitStats`;
`SynchronizeBuffersInRanges` re-reads `CpuDirtyGeneration()` **after** the long scan and classifies
each hit `raced` (generation bumped) vs `missed` (unchanged), emitting `BDA gen-skip MISSED: hits=`
only on a genuine miss. Default behavior unchanged; all behind `KYTY_UFC_BDA_SYNC_VERIFY=1`.
Files: `renderer/cache/bufferCache.{h,cpp}`. Report:
`_Build/agent-reports/dev112-bdagen-reclass-20261006.md`.

Build **oc20261006-121117-build = UFC Dev 112** installed; full **tests 55/55 (34.78 s)**.
Diagnostic **oc20261006-121239-fight** →
`_Build/autolab/d112-bdagenverify-reclass-bis-c-framedump-20261006-121239`, exit 0, 421.97 s
(Dev 112, 2560x1440/Mailbox, validation/red-zone ON, a/b,d–p ON, c OFF, bdagen+verify ON,
framedump; no device loss, all breadcrumbs started==completed; 187 engine PNGs, max RSS 11,179.9 MB).

**Result:** `bda sync fast=490605 walk=200077 verify_missed=0 verify_raced=19279` — every one of
the 19,279 oracle hits raced; the `MISSED` line fired **0** times; 128 `BDA postwalk` reports all
had `gen_post > gen_pre`. Dev 110's `verify_missed=11,080` was an oracle-scan race artifact; the
generation invariant holds. Engine frames: 3 near-black samples, all startup/loading; no sustained
post-intro blackout. Decision: keep `KYTY_UFC_BDA_SYNC_GENERATION` opt-in/default OFF; the verify
path forces the walk and cannot show the gain. No FPS claim (FightWatch false-negative); 25/30 FPS
and smooth media unmet. Next: two clean `bdagen`-only runs (verify OFF) + matched controls, first-45-s
frame counts and contact sheets, promote only with a user manual visual check.

## Dev 111 — BDA post-walk recheck: walk publishes, residual dirty is a race — 2026-10-06, 12:05

One concrete diagnostic that changes the Dev 110 conclusion. Added an opt-in, side-effect-free
`BufferCache::BdaCpuDirtyBufferCount(ranges, log)` (same iteration as the Dev 110 oracle,
`IsRegionCpuModifiedExisting`, never creates a region) and ran it **after** the forced full walk
in the `KYTY_UFC_BDA_SYNC_VERIFY` path. It logs `BDA postwalk: gen_pre=<g> gen_post=<g>
still_dirty=<n>` and `BDA postwalk DIRTY: ...`. Default behavior unchanged; opt-in only. Files:
`renderer/cache/bufferCache.{h,cpp}`. Report:
`_Build/agent-reports/dev111-bda-postwalk-20261006.md`.

Build **oc20261006-115533-build = UFC Dev 111** installed; full **oc20261006-115609-tests 55/55
(38.66 s)**. Diagnostic **oc20261006-115802-fight** →
`_Build/autolab/d111-bdagenverify-postwalk-bis-c-framedump-20261006-115823`, exit 0, 421.86 s
(Dev 111, 2560x1440/Mailbox, validation/red-zone ON, a/b,d–p ON, c OFF, bdagen+verify ON,
framedump; no device loss; 186 engine PNGs, max RSS 11,168 MB).

**Result:** 128 post-walk checks — 54 `still_dirty=0`, 74 `still_dirty>0`, and **all 74 had
`gen_post > gen_pre`; none had an unchanged generation.** So the forced walk clears/publishes the
pages it visits; residual dirty pages are fresh concurrent guest CPU writes that bump the
generation. `verify_missed=8972` vs `verify_raced=2` is better explained by the oracle's long scan
window racing guest writes than by a non-bumping transition → **Dev 110's "generation invariant
disproven" is not supported** (association, not yet proven). Engine frames: no sustained
post-intro blackout (2/47 sampled near-black, both startup). No FPS claim (FightWatch n/a);
25/30 FPS and smooth media unmet. Decision: keep `KYTY_UFC_BDA_SYNC_GENERATION` opt-in/default
OFF; retain the post-walk counter. Next: re-read the generation **after** the oracle scan and
reclassify racing hits as `raced`; if `verify_missed` collapses, bdagen becomes a default
candidate (+19.8% matched Dev 108 window) pending a manual visual check.

## Dev 110 — BDA generation gate proven unsound by an exact skip oracle — 2026-10-06, 11:47

One concrete verification milestone that changes the bdagen decision. Added an opt-in,
side-effect-free oracle for the Dev 108 generation-gated quiescent BDA skip
(`KYTY_UFC_BDA_SYNC_VERIFY=1`, label `*bdagenverify*`): `BdaSkipWouldMissUpload` checks whether
any tracked buffer inside the mapped ranges still has CPU-dirty pages — exactly the pages a full
walk uploads — and forces the conservative walk on a hit. It needed
`MemoryTracker::IsRegionCpuModifiedExisting` (`Iterate<false>`; never creates a region, unlike
`IsRegionCpuModified`, whose `Iterate<true>` self-reports misses). Default behavior unchanged;
oracle opt-in only. Dev 109's first oracle build used the creating query and is discarded.

Build **oc20261006-113214-build = UFC Dev 110**; full **oc20261006-113243-tests 55/55 (35.20 s)**.
Diagnostic **oc20261006-113512-fight** →
`_Build/autolab/d110-bdagenverify-bis-c-framedump-pooloff-r2-20261006-113533`, exit 0, 421.79 s
(a first launch, `...-113443`, was the guest-memory-reserve failure, exit 321/duration 0, not
data); Dev 110, 2560x1440/Mailbox, validation/red-zone ON, a/b,d–p ON, c OFF, bdagen+verify ON,
framedump, pool OFF; no device loss; 186 engine PNGs, max RSS 11,169 MB.

**Result:** `bda sync fast=423424 walk=194393 verify_missed=11080 verify_raced=1` — the gate
hides uploads on **1.8%** of its calls; 32 `BDA gen-skip MISSED` lines repeat on the same buffers
(`0x12e0000000+0x22f4000` and `0x1294fe0000+0x14000`, 12× each). The generation assumption is
disproven, not suspected. Corroboration: the oracle-corrected run's engine frames have only 5
near-black frames and a longest post-intro black run of **3**, versus Dev 108 bdagen's runs of
6–15. Decision: keep `KYTY_UFC_BDA_SYNC_GENERATION` **opt-in/default OFF; do not promote**;
retain the oracle + no-create observer. Report:
`_Build/agent-reports/dev110-bda-gate-oracle-20261006.md`. No FPS claim; 25/30 FPS and smooth
media unmet. Next: instrument per-miss post-walk clearing / generation bump to root-cause the
hidden transition before any bdagen promotion.

## Dev 108 unchanged — bdagen Tracy attribution + blackout finding — 2026-10-06, 11:15

One diagnostic/verification milestone: consumed the planned matched Tracy capture for the
retained Dev 108 `bdagen` candidate and validated its visuals. Job **oc20261006-110920-fight**,
exit 0, 300.37 s, session `d108-bdagen-tracy-bis-c-framedump-pooloff-20261006-110941`; Dev 108,
2560x1440/Mailbox, validation/red-zone ON, a/b,d–p ON, c OFF, bdagen ON, framedump, pool OFF;
no device loss/driver events; 118 engine PNGs. No source change.

Matched-window attribution vs `d107-perdraw-tracy-bis-c-framedump-pooloff-20261006-103732`
(same `--tracy-start-seconds 110`): `SynchronizeBuffersInRanges` **9,347.9 → 6,593.9 ms
(-29.5%)**; frame counter 110–145 s **400/31.56 = 12.67 → 477/31.42 = 15.18 FPS
(+2.51 / +19.8%)**, +13% draws processed. Session median (16) is phase variance, not used.

New finding: per-frame darkness + contact sheets show post-intro engine-frame BLACK windows in
**2 of 3 `bdagen` runs** (tracy frames 82–108 maxrun 15; run B 170–186 maxrun 6) and **0 of 4
non-`bdagen` Dev 107 runs** (clean through long standing phases). Live HUD + running clock over
hard-edged black geometry replacing fighters/arena = dirty-log mesh-explosion / user-blackout
shape. Association only; intermittent, phase-confounded; causation NOT established.

Decision: keep `KYTY_UFC_BDA_SYNC_GENERATION` **opt-in, default OFF; do not default/promote**
while implicated. No default baseline change; report
`_Build/agent-reports/dev108-bdagen-tracy-attribution-20261006.md`. Next: two `bdagen`-OFF Dev
108 framedump runs + one repeat `bdagen` run (compare contact sheets, not one frame) to
confirm/exonerate, then the a–p letter bisect only if exonerated. 25/30 FPS and smooth media
unmet.

## Dev 108 — generation-gated BDA sync retained as opt-in — 2026-10-06, 11:07

One concrete engine-change milestone from the completed per-draw profile. The top attributable
non-wait cost was the full BDA walk `SynchronizeBuffersInRanges` (6.58% / 9.35 s per 30 s /
311 ms/s). Added `MemoryTracker::CpuDirtyGeneration()`, bumped in `RecordCpuDirty` on every
CPU-dirty transition before the overflow early-return; `SynchronizeBuffersInRanges` skips the
walk only while no transition happened, no registration and the mapped-ranges version is
unchanged, and otherwise performs the full conservative walk (never the risky per-range
dirty-sync). A lost dirty-log entry therefore cannot cause a stale upload. Opt-in
`KYTY_UFC_BDA_SYNC_GENERATION=1`, label `bdagen`; default baseline unchanged. Files:
`memoryTracker.{h,cpp}`, `renderer/cache/bufferCache.{h,cpp}`, `AutoFight.ps1`,
`tests/MemoryTrackerTests.cpp` (`TestCpuDirtyGeneration`). Report:
`_Build/agent-reports/dev108-bda-sync-generation-20261006.md`.

Build **oc20261006-104920-build = UFC Dev 108** installed; focused **oc20261006-105131-tests
memory_tracker 1/1**; full **oc20261006-105144-tests 55/55 (31.64 s)**. Preflight clear before
every serial job; heartbeat good; no manual play/inherited overrides.

Matched gameplay: A **oc20261006-105227-fight** → `d108-bdagen-bis-c-framedump-pooloff-20261006-105250`
(exit 0, 422.0 s, `fast=445,768 walk=176,126`, session avg 21.58, RSS 11,141 MB);
B **oc20261006-110022-fight** → `...-b-20261006-110044` (exit 0, 421.6 s,
`fast=387,338 walk=140,690`, session avg 23.02, RSS 11,176 MB). Control
`d107-dcc-fixture-check-bis-c-framedump-pooloff-20261006-102543`: `fast=0 walk=810,893`,
session avg 17.95. Result: full walks **78–83% lower**; 71–73% of calls skip. No device
loss/driver events; engine `frame_000184` shows a correct live ground fight.
`FightWatch` false-negative kept (`fight_*` n/a) — session avg is phase-mixed, not causal FPS.
**Retain opt-in.** Next: `bdagen` Tracy to attribute the removed walk to FPS; manual
ground-and-pound check before considering default. 25/30 FPS and smooth media unmet.


## Dev 107 unchanged — DCC producer/predicate-ring semantic fixture retained — 2026-10-06, 10:34

One concrete regression milestone, per latest handoff. Added CPU/GPU producer sequence
to `tests/ShaderRecompilerComputeTests.cpp`: CPU 7/GPU **2,053 snapshots / 4,106 slices**,
exact target pixels/expanded bytes, selected-layer and rejected-key sentinel preservation,
last-word nonuniformity, predicate wrap, nonblocking submission, rejected first-use image
initialization and coherent publication superseded by new GPU write. **No demonstrated engine
defect** in this coverage. Preserved source work, defaults and pool-OFF quarantine; no install,
numbered build or engine/harness edit. Report: `_Build/agent-reports/dcc-producer-sequence-20261006.md`.

Worker incrementally built tests. Final focused **oc20261006-102318-tests 1/1 (2.93 s)**;
full **oc20261006-102406-tests 55/55 (31.38 s)**. Development failures corrected fixture
slice-count and native padded-linear storage assumptions, not an engine fix. Exact oracle
retained; `git diff --check` passes. Preflight immediately before every serial job clear,
worker heartbeat good, no manual play/competing work/inherited overrides.

Unchanged-runtime necessary gameplay **oc20261006-102521-fight**, exit 0, **421.51 s**:
`_Build/autolab/d107-dcc-fixture-check-bis-c-framedump-pooloff-20261006-102543`.
Configured 2560x1440/Mailbox, red-zone/ShaderValidation ON, c/pool OFF, a/b/d–p ON,
185 engine PNGs, 197 black Windows shots, no Tracy/shader dump. Native source 3840x2160 /
diagnostic PNG 960x540, no render reduction. Foreground acquired once. Same Makhachev black /
Della Maddalena green at T-Mobile Arena; named HUD ~94.65–96.77 s, >300 s fighting,
standing/ground/standing/clinch, final R1 0:45. Periodic/final reviewed engine images lack prior
giant black geometry, not a clean transient verdict. Speckling/manual check remain.

Raw automatic first_fight_s/fight_frame_rate/fight_frames n/a false negatives unchanged.
Diagnostic **4,808/324.74 = 14.8057 FPS**, fighting sampled median/min **14/11**,
common opening **549/42.65 = 12.8722**, not exact 45-s or causal improvement.
Whole-session median/min **14/0**, RSS/device VRAM **11,204.5/12,676 MB**, entrance/loading
**4.7059 FPS**, not decoder-only or smooth-media proof. Dump readbacks/workload affect timing.
Final nominal waits cp/other/untagged/CPU-write **20.857/31.870/10.300/1.170 ms/s** (overlap);
metadata/indirect drains absent. GPU evaluated slices **2,447**, CPU/GPU fills **11,205/9,473**,
GPU indirect **21,072**, BDA fast=0/walk **810,893**, SRT reused **361,598**, pool zero.
No device loss/triaged failure (benign known-valid SPIR-V cache banner only).

Retain regression, engine unchanged. Passing same-queue transfer fixture does not vindicate
arbitrary compute/BDA producers or concurrent aliases; no a/b culprit/default verdict.
**25/30 FPS and smooth media unmet. Next independent current-PC per-draw/readback profile**:
30 s Tracy explicitly timed ~110 s, engine PNG phase verification because FightWatch fails;
command in latest HANDOFF/report. Then one evidenced engine correction/work reduction,
with producer ordering/invalidation proof. No more repeated negative group controls or
same passing fixture, routine input pause, new agent/supervisor, commit/push or goal file.

## Dev 107 unchanged — SRT restored, two sampled negatives; next semantic fixture — 2026-10-06, 10:10

One bounded pending discriminator milestone: restored only d versus previous a–d-OFF
controls, a–c/pool OFF. **No reference giant black geometry/live-HUD occlusion in reviewed
prepared PNGs of either valid run.** Not a culprit/fix verdict; restored metadata waits may
mask ordering races. Full reference, jobs, images, counters and next fixture:
`_Build/agent-reports/dev107-abc-discriminator-20261006.md`.

No engine/default/harness change, build or CTest; focused pool/allocation/full **55/55**
inherited, not rerun. Existing source preserved. Changed report/latest HANDOFF/this log,
generated contact PNG/JSON with existing CPU analysis. Preflight immediately before every
worker job clear: no manual play/competing work/inherited overrides; heartbeat passed.
- **oc20261006-095133-fight** → `_Build/autolab/d107-bis-abc-framedump-pooloff-20261006-095157`, exit 0, **421.01 s**.
- **oc20261006-095953-fight** → `d107-bis-abc-framedump-pooloff-b-20261006-100014`, duration 0,
  exit_code 321, guest-address-space reservation failure, excluded; waited 30 s/rechecked ownership.
- **oc20261006-100111-fight** → `_Build/autolab/d107-bis-abc-framedump-pooloff-b-retry-20261006-100132`, exit 0, **421.11 s**.

Dev 107, configured 2560x1440/Mailbox, red-zone/validation ON, a–c OFF/d–p ON/pool OFF,
186 engine PNGs each, requested 1 s Windows shots, no Tracy/shader dump; source 3840x2160,
PNG 960x540, no quality/settings change. Foreground acquired once, not continuously verified.
Same Makhachev black/Della Maddalena green, T-Mobile Arena; advancing intro, named HUD
~96.6–96.7 s, >300 s fighting. Periodic sheets/direct 175/185 of each lack prior giant black
geometry: A late ground HUD 0:41→0:20, B standing 0:54→0:37. Speckling persists/manual transient
check missing. Windows shots all black; automatic first_fight_s/fight_frames/fight_frame_rate
n/a remain false negatives. Darkness threshold not visual authority; sparse review not acceptance.

Diagnostic fight A/B **5,451/324.30 = 16.8085**, **4,992/324.51 = 15.3832 FPS**, sampled
fighting median/min **15/11**, **14/11**. Common ~42 s opening **12.9983/13.1840 FPS**, not
exact first-45-s qualification. Whole-session median/min **15/0**, **14/5**, max RSS/device
VRAM **11,041.2/12,578**, **11,036.8/12,454 MB**. Entrance/loading **4.9843/5.0608 FPS**, not
decoder-only; workload differs/readback perturbation, no causal FPS claim.
Final nominal waits ms/s metadata **78.093/80.707**, cp **1.403/1.353**, other **6.053/6.220**,
untagged **6.677/6.810**, CPU-write **1.340/1.153** (overlap). GPU indirect **21,853/21,573**,
BDA fast=0/walk **659,808/772,768**, pool 0; SRT reused **384,063/355,401**, inputs
**19,029,935/19,029,926**, invalidated reads **2/1**, replayed **164/343**. Zero shadow mismatch
not proof (reuse mode). No device loss/error beyond benign SPIR-V cache banner. Final translate/
compile/validate/module **2392/711/3/28**, **2389/710/3/28** ms, cumulative not movie-only.

Retain unchanged defaults/pool quarantine OFF; **25/30 FPS and smooth media remain unmet**.
**End negative-baseline branch now. Next source-driven CPU/GPU DCC/fill semantic fixture**:
extend existing color-metadata regressions for producer changes/nonuniform slices/selected
layers/sentinel target preservation/predicate-ring wrap, compare target pixels plus expanded
metadata. Review evaluator-before-image-init and coherent-fill publication ordering. No defect
proven by source reading; do not flip defaults from sampled negatives alone. If inconclusive,
independent intro/readback/per-draw work, no WAIT-FOR-INPUT, agents/supervisor, commit/push.

## Dev 107 unchanged — repeated a–d-OFF smaller discriminator, sampled negative — 2026-10-06, 09:49

One focused bounded diagnostic milestone using c-OFF/pool-OFF black-geometry reference ~400 s.
Restored e–h with a–d/pool OFF. **No prior giant black geometry/live-HUD occlusion in reviewed
periodic and direct engine PNGs of either completed run**; timing/phase effects prevent culprit
or fix verdict. Full jobs, images, analysis/limits and next action:
`_Build/agent-reports/dev107-abcd-discriminator-20261006.md`.

No engine/default/harness change, build or CTest; focused pool/allocation and full **55/55**
inherited, not rerun. Source work preserved. Changed report, this log, latest HANDOFF; reused
CPU analysis generating contact PNG/JSON. Preflight immediately before jobs found no manual
play/competing work or inherited KYTY_UFC overrides; worker heartbeat passed, all serialized.
- **oc20261006-093034-fight** → `_Build/autolab/d107-bis-abcd-framedump-pooloff-20261006-093055`, exit 0, **420.78 s**.
- **oc20261006-093830-fight** → `d107-bis-abcd-framedump-pooloff-b-20261006-093852`, duration 0,
  exit_code 321, guest-address-space reservation failure, excluded; waited 30 s/rechecked preflight.
- **oc20261006-093948-fight** → `_Build/autolab/d107-bis-abcd-framedump-pooloff-b-retry-20261006-094009`, exit 0, **420.79 s**.

Unchanged Dev 107, manifest 2560x1440/Mailbox, validation/red-zone ON, a–d OFF/e–p ON/pool OFF,
187 prepared PNGs per valid run, 1 s Windows shots, no Tracy/shader dumps. Dump source
3840x2160/PNG 960x540; settings/quality unchanged. Same Makhachev black/Della Maddalena green,
T-Mobile Arena, advancing intro, named HUD ~92–96 s, >300 s fighting. A later standing then
ground, B later ground: direct A 175/186 HUD 0:47/0:26, B 186 HUD 0:05 render fighters.
Sparse samples cannot certify transients; speckling/manual check missing. Windows captures
all black, automated first_fight_s/fight_frames/fight_frame_rate n/a remain false negatives.

Diagnostic conservative fighting A/B **5,215/324.25 = 16.083**, **5,894/324.05 = 18.189 FPS**,
sampled fighting median/min **15/12**, **17.5/12**. ≤400 s **15.955/17.795 FPS**; common opening
~42 s **16.958/13.632 FPS**, not exact first-45-s acceptance. Session median/min **15/1**, **18/0**,
max RSS/device-wide VRAM **11,057.6/12,469**, **10,898.4/12,499 MB**. Entrance/loading **5.359/5.429 FPS**,
not codec-only. Foreground acquired once; workload differs/readbacks perturb timing, no FPS gain.
Last nominal wait ms/s metadata **87.997/122.263**, cp **1.523/2.307**, other **6.430/8.877**,
untagged **7.893/9.587**, CPU-write **1.380/1.810** (overlap). Restored metadata waits may mask
races. GPU indirect **22,141/23,138**, BDA fast=0/walk **788,084/688,119**, SRT/pool counters zero.
No device loss/triaged validation error; benign validation-cache banner only. Final shader
translation **2357/2365**, compile **704/708**, validate **3/3**, module **28/28** ms, not intro-only.

Retain unchanged defaults/pool quarantine OFF. Next **`d107-bis-abc-framedump-pooloff`** restores
only d/SRT memo: positive prioritizes explicit memo invalidation fixture; sampled negative
gets one repeat then a/b CPU-vs-GPU clear/fill semantic comparison. Bound further narrowing;
inconclusive → independent intro/readback/per-draw work. Smooth media/audio/codec and repeated
visually correct **25/30 FPS unmet**. No WAIT-FOR-INPUT, agents/supervisor, commit/push.

## Dev 107 unchanged — repeated a–h-OFF discriminator: no prior black occlusion in reviewed samples — 2026-10-06, 09:22

One focused bounded feature-group milestone, reference c-OFF/pool-OFF positive around 400 s.
No engine/default/harness edit, build or CTest; earlier focused pool/allocation tests and
full **55/55** inherited, not rerun. Existing source preserved. Changed HANDOFF, this log,
`_Build/agent-reports/dev107-abcdefgh-discriminator-20261006.md`; reused CPU analysis to generate
per-session contact PNG/JSON. Immediate ownership/env checks clear before each serial worker
job, heartbeat passed, no inherited KYTY_UFC variables; no process kill or worker restart.

- **oc20261006-090458-fight**, exit 0, `_Build/autolab/d107-bis-abcdefgh-framedump-pooloff-20261006-090520`, **421.11 s**.
- **oc20261006-091315-fight**, exit 0, `_Build/autolab/d107-bis-abcdefgh-framedump-pooloff-b-20261006-091339`, **421.55 s**.
Both unchanged Dev 107, configured 2560x1440/Mailbox, red-zone/validation ON, **a–h OFF, i–p ON,
pool OFF**; 186 prepared frames each, requested 1 s Windows shots, no Tracy/shader dump.
Dump source 3840x2160/PNG 960x540 noted separately, no rendering/settings reduction.

Same fighters/arena, HUD ~94–97 s, advancing EA imagery and >300 s actual fighting. Reviewed
periodic and direct late engine PNGs lack giant black geometry/live-HUD occlusion. A frame
185 entirely black **without HUD**, not a reference-failure positive; B 175/179/185 show
ground fighting, timer **0:36/0:27/0:13**. All Windows shots black, automatic first_fight_s /
fight_frames / fight_frame_rate n/a false negatives preserved. Darkness screen not used as
visual authority. Sparse review cannot prove transient correctness, speckling/manual check remain.

Frame-counter diagnostic windows ≤400 s A/B **5,803/303.19 = 19.140**, **5,119/301.38 = 16.985 FPS**;
fighting sampled median/min **20/12**, **15/11**. Common ~42 s opening **13.628/14.172 FPS**,
not exact first-45-s acceptance. Whole-session sampled median/min **21/1**, **15/0**;
max RSS/device VRAM **11,259/12,648**, **11,066.4/12,517 MB**. Entrance/loading **5.383/5.002 FPS**,
not verified codec-only footage. Phase mix differs, foreground acquired once; PNG readbacks
perturb timing. A full generated span includes HUD-absent black end frame, not pure fighting.

Last nominal 30 s waits A/B (ms/s): metadata **142.937/130.187**, cp **2.020/1.840**, other
**496.877/461.337**, untagged **4.437/7.937**, CPU-write **0.853/1.223**; attribution overlaps.
Metadata drains restored by a/b OFF, versus zero in prior positive; timing can suppress races.
GPU indirect **23,893/22,698**, no args blocking; BDA fast=0/walk **522,038/663,835**, SRT memo
and pool counters zero. No device loss/FATAL/ERROR/validation errors; benign cache banner only.

Decision: retain engine/defaults, pool stays quarantined OFF. **Two sampled negative controls**
motivate smaller-group testing, not a definitive a–h culprit verdict or immediate-load fix:
late workload/ordering differ and failure is intermittent. Next **`d107-bis-abcd-framedump-pooloff`**,
420 s / 1 s shots, restores e–h with c/pool OFF; positive points at e–h/interactions, negative
requires matching repeat. Bound narrowing; if inconclusive use independent intro/per-draw work.
Smooth media/audio/codec unverified, no causal FPS gain, **25/30 FPS unmet**. No WAIT-FOR-INPUT,
new supervisor/agent, commit/push. One discriminator milestone completed this invocation.

## Dev 107 unchanged — BOTH pool and dirty log OFF still reproduce black geometry / live HUD — 2026-10-06, 09:0x

Focused pending discriminator completed; no engine/default/harness edit, build or new tests.
Earlier focused pool/allocation and full **55/55** inherited, not rerun. Branch/source preserved.
Changed HANDOFF, this log and `_Build/agent-reports/dev107-pooloff-dirtylogoff-positive-20261006.md`;
existing CPU analysis generated session contact sheet/JSON. Immediate ownership/env checks
clear before each serialized worker job, heartbeat valid, no inherited KYTY_UFC variables.

`oc20261006-085042-fight` zero-duration guest-address-space reservation failure excluded.
Waited and retried after clear preflight: **oc20261006-085148-fight**, worker exit 0, session
`_Build/autolab/d107-bis-c-framedump-pooloff-retry-20261006-085210`, **421.38 s**, 186 engine PNGs.
2560x1440/Mailbox, validation/red-zone ON, **pool OFF, c OFF**, remaining a-p ON, prepared-frame
dumps + 1 s Windows shots, no Tracy/shader dumps. BDA `fast=0 walk=650465`, pool counters all 0.

**Actual positive:** direct engine frame 174 (~398 s) renders fighters; 175 (~400 s) black
shape above/over head; 176/177 grow across head/torso, 178/179 huge black geometry across both
fighters, 185 (~421 s) most scene occluded with right cage/canvas visible. HUD **R1 0:44 → 0:27**
advances. This is not a between-round transition. **Pool and dirty log are not required for this
failure.** Late onset still doesn't establish equivalence to user's immediate-load timing.

Same Makhachev black / Della Maddalena green, T-Mobile Arena, opening imagery and >300 s fighting
visible. HUD bars ~94.57 s, named HUD ~96.69 s; standing/clinch/ground/standing. All Windows shots
black; automatic first_fight_s/fight_frames/fight_frame_rate n/a are false negatives. **Generated
darkness screen also misses this actual positive:** no >85%-dark candidates, yet frame 185
**79.10%** dark and obvious black occlusion. Direct PNG review overrides that screen, not vice versa.

Frame-counter diagnostics (readbacks perturb timing, phases/foreground not fully controlled):
- Pre-observed-failure **96.69–398.04: 5,236/301.35 = 17.375 FPS**, fighting median/min **15/11**.
- Full **5,568/324.69 = 17.149 FPS includes corruption**, not qualified performance.
- Opening **571/42.47 = 13.445 FPS**, approximate sampled window, not exact first-45-s acceptance.
- Session median/min **15/5**, max RSS/device VRAM **11,221.2/12,651 MB**; entrance ~54–80 s
  **4.637 FPS**, includes loading/3D, not decoder-only measurement.
- Final nominal cp/other/untagged waits **28.987/41.763/11.917 ms/s**, attribution overlaps;
  metadata/indirect drains absent, GPU indirect **22,218**. No device loss/FATAL/validation errors;
  benign cache banner only. No FPS gain from these unmatched workload comparisons.

Decision: retain unchanged code and default-OFF pool quarantine, **stop pool-only culprit chase**.
Both switches OFF isn't a fix; dirty-log risk separately unresolved. Source review of recycling,
view destruction and timeline-deferred retirement doesn't prove an early-free defect; no
speculative correction. Next **`d107-bis-abcdefgh-framedump-pooloff`**, 420 s / 1 s shots through
worker: inspect engine PNGs directly; positive excludes a-h as required, negative needs repeat.
Then source-driven producer/consumer/BDA/indirect or shader fixture with draw identity. Manual
transient check absent, speckling persists, smooth movie/audio/codec unverified, **25/30 FPS unmet**.
No WAIT-FOR-INPUT, another supervisor/agent, commit/push; one discriminator this invocation.

## Dev 107 unchanged — dirty-log-OFF repeat still has giant black mesh and advancing HUD; not a sufficient workaround — 2026-10-06, 08:4x

One bounded pool/dirty-log separation milestone, using previous c-ON failure as reference.
No engine/default/harness edits, build or new CTest; focused pool/allocation and full **55/55**
inherited. Existing work untouched. Changed HANDOFF, this log, CPU analysis script optional
session args, and `_Build/agent-reports/dev107-pool-dirtylog-separation-20261006.md` plus
generated contact sheets/JSON. Report contains exact image paths and source-triage boundary.

Clear ownership/env checks immediately before each worker job, heartbeat valid, all serial.
Dev 107, 2560x1440/Mailbox, red-zone/validation ON, **pool ON, only dirty log c OFF**, 512 MiB cap,
prepared-frame dumps + 1 s Windows shots, no Tracy/shader dumps:
- `oc20261006-082600-fight`: `_Build/autolab/d107-imgpool-bis-c-framedump-20261006-082623`,
  exit 0, **422.04 s**, 186 engine frames.
- `oc20261006-083435-fight`: `d107-imgpool-bis-c-framedump-b-20261006-083456`, **duration 0** /
  guest-address-space reservation failure; excluded, waited and retried after preflight.
- `oc20261006-083533-fight`: `_Build/autolab/d107-imgpool-bis-c-framedump-b-20261006-083555`,
  exit 0, **421.29 s**, 187 engine frames.

Both EA opening progressions and >300 s actual gameplay, same fighters/arena (Makhachev black,
Della Maddalena green, T-Mobile). HUD ~92–95 s. Windows capture false negatives persist;
automatic first_fight_s/fight_frame_rate/fight_frames n/a, not evidence of non-fights.
A: no post-frame-37 >85%-dark candidates, periodic images lack giant occlusion. **B: frame 178
(~404 s) giant black polygons; 179–186 >85% dark.** Reviewed 182 HUD 0:34 and 186 HUD 0:26 with
black mesh and some arena visible. This is live-HUD corruption, not a round transition, and
occurs with BDA `fast=0 walk=659664`. **Dirty log is not required for this reproduced failure.**

Measured conservative diagnostic spans:
- A **97.07–422.04 s: 5,861/324.97 = 18.036 FPS**, fighting median/min **15/12**.
- B pre-failure **96.61–397.93: 5,177/301.32 = 17.181 FPS**, median/min **15/11**; full
  **5,708/324.68 = 17.580 FPS includes failure**, not qualified sustained visual throughput.
- Opening common ~42 s **12.796/12.807 FPS**, not exact first-45-s qualification.
  Session median/min **16/0**, **16/1**. A/B max RSS/device-wide VRAM **11,246.7/13,073** and
  **11,212.1/13,221 MB**. Entrance/loading ~54–80 s **4.530/4.044 FPS**, not decoder-only work.
- Final nominal cp/other/untagged waits A **23.47/33.27/9.23**, B **43.08/52.89/5.48 ms/s**,
  overlaps between thread sites; metadata/indirect drains absent. GPU indirect 23,471/22,221.
- Pool hits/misses/evictions A **54,000/116,333/114,321**, B **55,980/110,239/108,143**;
  **31.70/33.68% native allocation avoidance**, not a visual/FPS improvement. BDA full walks
  A **590,139**, B **659,664**. No device loss/FATAL/validation error; benign cache banner only.

Decision: **pool visual acceptance still fails; retain quarantine/default OFF, no cap tuning.**
No dirty-log culprit verdict/default edit on these data. Diagnostics perturb timing; scenario
standing/ground phases differ; foreground acquired once, manual transient-artifact check absent.
EA animation visible, smooth media/audio/codec unverified; speckling persists. **25/30 FPS unmet.**
Next discriminator `d107-bis-c-framedump-pooloff` (420 s, 1 s shots) keeps c OFF and pool OFF;
then targeted GPU pattern/retirement/recycled backing and image→buffer-BDA fixture, not another
handle-only test. No shader/draw identity yet. No WAIT-FOR-INPUT, supervisor, agents, commit/push.

## Dev 107 unchanged — native-image pool fails visual qualification: mesh explosion and live-HUD black regions; pool-OFF control does not reproduce — 2026-10-06, 08:19

One focused necessary acceptance milestone for the retained default-OFF pool, against the
previous prepared-frame OFF reference. No engine/default edits, build or new CTest; inherited
focused image-pool/allocation tests and **55/55**, not rerun. Files changed: HANDOFF, this log,
`_Build/agent-reports/dev107-imgpool-qualification-20261006.md` and CPU-only
`dev107-pool-qualification-analysis.py` plus generated contact sheets/JSON. Existing work intact.

Preflight clear immediately before both worker jobs; no inherited KYTY_UFC overrides, worker
heartbeat current. Unchanged Dev 107, 2560x1440/Mailbox, red-zone/ShaderValidation ON, a-p ON,
prepared-frame dumps + requested 1 s Windows shots, no Tracy/shader dumps. Pool alone differs:
- ON `oc20261006-080316-fight`, exit 0, session
  `_Build/autolab/d107-imgpool-framedump-20261006-080338`, **419.06 s**, **171** engine PNGs.
- OFF unchanged-code control `oc20261006-081131-fight`, exit 0, session
  `_Build/autolab/d107-framedump-blackout-control-20261006-081151`, **420.90 s**, **188** PNGs.

Windows shots still black; automatic first_fight_s/fight_frames/fight_frame_rate n/a are false
negatives. Both engine captures show EA imagery then Makhachev black/Della Maddalena green,
T-Mobile Arena, HUD ~90–93 s. Actual ON **frame 60 (~143 s) shows a stretched skin-textured
triangle**; **140/141 (~320/323 s)** have almost-black scene with live updating HUD; 143 timer
advances, 160 giant black region occludes most 3D. **14/134** post-frame-37 PNGs >85% dark.
OFF reviewed through frame 177 (~400 s) renders ground/submission; single dark frame 178 is
HUD-absent corner transition, not a blackout. Prior OFF reference no dark candidates. No
culprit verdict or claim this late failure equals user's immediate-load failure; dirty log
ON is a known mesh-explosion suspect and workloads/writes differ.

Measured diagnostic frame-counter evidence:
- ON **96.77–419.06 s: 4,252 frames / 322.29 s = 13.193 FPS**, fighting sampled median/min **16/0**.
  Late **323.28–419.06: 504 / 95.78 = 5.262 FPS**; max RSS/VRAM **11,097.2 / 13,305 MB**.
- OFF pre-cutscene **97.04–399.63 s: 7,047 / 302.59 = 23.289 FPS**, sampled median/min **24/16**,
  span RSS/VRAM **11,189.8 / 12,711 MB**; session median/min **24/1**, max **11,609.1 / 12,974 MB**.
- Common sampled opening ~97–140 s: old OFF **15.864**, ON **15.760**, new OFF **17.774 FPS**;
  not exact 45 s. Whole-fight workloads diverge (ON mostly standing later; OFF mostly ground),
  so these are not causal optimization gains or 25-FPS evidence.
- Final ON pool **37,897 hits / 111,952 misses / 109,659 evictions / 372,630.5 MiB recycled**,
  **25.29%** measured CreateImage requests avoid native allocation; cap still thrashes.
- Nominal last 30 s ON waits cp **791.54**, other **775.54**, untagged **6.28 ms/s**, versus
  pre-failure **28.90/40.32/8.85**; OFF last **47.79/60.18/1.80**, includes corner phase.
  Thread wait attribution overlaps; metadata/indirect drains absent, GPU indirect active.
- No device loss/FATAL/validation errors in either run. Foreground acquired once only;
  readback PNGs perturb timing, smooth media/audio/codec unverified, speckling/manual check missing.

Decision: **pool fails acceptance; do not enable or tune cap**. Preserve quarantined existing
opt-in code/tests default OFF. **25/30 FPS unmet.** Next concrete job: same pool-ON run with
only c OFF (`d107-imgpool-bis-c-framedump`, 420 s) to separate dirty-log missed-upload from pool
reuse/lifetime interaction, unless source yields a targeted failing regression first. A clean
run is not a culprit verdict; repeated controls needed. Failing shader/draw identity not yet
captured. No routine input request, second supervisor, commit or push.

## Dev 107 unchanged — prepared frames establish black-window capture false negatives; actual fighting 19.05 diagnostic FPS — 2026-10-06, 08:0x

Milestone: bounded validation of the capture/presentation ambiguity (overnight plan + latest
Dev 107 next action), not another blackout status-only cycle. No engine/harness change,
build or tests this cycle; prior focused image-pool/allocation tests and **55/55** full suite
inherited, not rerun. Files changed: this log and `HANDOFF.md`; uncommitted source preserved.

Worker job `oc20261006-075042-fight` exit 0; session
`_Build/autolab/d107-framedump-capture-validation-20261006-075104`, **421.45 s**,
2560x1440/Mailbox, ShaderValidation/red-zone ON, no Tracy or shader dumps. All a-p defaults
ON; existing image pool **OFF**, counters zero. Ownership preflight clear and worker alive;
no inherited `KYTY_UFC_*` overrides. Window acquired foreground once (`fg=True`).

Evidence: **197 dark Windows shots versus 187 GPU-completed prepared-frame PNGs**. Reviewed
engine frames 000004/5/6 show changing EA opening imagery/logo; 000025 dark lit arena entrance;
000030 black transition; 000031-34 referee/arena; **000035 shows HUD/fighters at 07:52:35.871
write time (~91 s launch-relative)**. Frames 40/60 standing/clinch, 80/100 ground, 120/140/160
standing, 180/186 ground/submission, ending 07:58:06.373. Scenario: Makhachev black shorts vs
Della Maddalena green shorts, T-Mobile Arena. `fight_detected=False` and `first_fight_s=n/a`
are false negatives from the Windows capture path; the latest earlier "black non-fight"
interpretation is not justified solely by those shots. No verdict on the user's live-HUD
blackout or every unreviewed frame.

Measured diagnostic throughput from unchanged raw `metrics.csv`:
- HUD-confirmed conservative **92.49-421.45 s**, **6,266 frames / 328.96 s = 19.048 FPS**.
- Opening **92.49-136.97 s**, **706 frames / 44.48 s = 15.872 FPS**; approximate sampled
  opening, not the exact fight-triggered first-45-s qualification metric.
- Ground **200.61-240.95 s**, **972 / 40.34 s = 24.095 FPS**.
- Entrance/loading trough **54.33-79.76 s**, **139 / 25.43 s = 5.466 FPS**; not a proven
  video-decoder workload.
- Session median/min **18/1**, max RSS **11,100.3 MB**, device-wide max VRAM **12,768 MB**.
  Automated fight-only fields remain n/a; no device loss/FATAL. Triage's SPIR-V match is the
  benign validation-cache banner.
- Nominal final 30 s waits: cp_read_fault **36.84 ms/s**, other_threads **50.60**, gpu_untagged
  **10.91**. Early nominal ~30-60 s: cp **79.39**, other **61.77**, untagged **41.90**.
  Metadata/indirect blocking sites absent; GPU indirect 24,549 cumulative. Thread waits overlap.

Retain unchanged Dev 107, pool default OFF. No FPS gain or goal claim. Prepared PNGs are
downsampled diagnostics, not a render-quality change; guest sources include 3840x2160 while
host configuration stays 2560x1440. Opening animation progresses, but smooth media playback,
audio and codec attribution remain unverified. Reviewed fighters/arena/HUD are present;
skin/cage/canvas speckling visible, no manual transient-artifact check. Readback perturbation
and sparse samples preclude clean performance/visual acceptance. **25/30 FPS unmet.**

Next: qualify existing pool ON with `d107-imgpool-framedump` against this pool-OFF scenario,
before cap tuning/default changes. Compare opening and matched standing/ground frame windows,
pool hits and VRAM; repeat comparable controls for a small gain. Independent media profiling
can now separate the actual opening animation from the much slower 3D entrance/loading window.
No requests for routine input, new supervisors, commits or pushes.

## Dev 107 — opt-in native-image backing pool: ~25% fewer vmaCreateImage/vmaDestroyImage calls, but thrashes under a too-small cap; automated run black non-fight — 2026-10-06 (cycle 07:4x)

Cycle goal (overnight plan §3/§4, ENGINE-REVIEW P5; Dev 105's next hypothesis): reduce the
~11.5% command-thread native image create/destroy churn (`Image::~Image`+`DeleteImage`,
~535/s) with a bounded backing pool rather than a cache-policy change.

Engine change (opt-in, default OFF): `GraphicContext` holds freed `VkImage+VmaAllocation`
backings in `m_image_pool`; `CreateImage` recycles a full-create-parameter match, resetting the
layout to the request's `initialLayout`; `DeleteImage` pools instead of `vmaDestroyImage`.
Caps: 512 MiB (`KYTY_UFC_IMAGE_POOL_BYTES`) / 2048 (`KYTY_UFC_IMAGE_POOL_COUNT`);
`DestroyAllocator` flushes. `KYTY_UFC_IMAGE_POOL=1`; AutoFight label `*imgpool*`. New 30 s
counters `image pool hits/misses/evict/recycled_mb` in `masterSemaphore`. Files:
`graphicContext.h`, `vma.cpp`, `renderer/masterSemaphore.h/.cpp`, tests + `CMakeLists.txt`.

Evidence:
- Builds Dev 106/107 OK; focused `host_image_pool` (recycle handle + fresh sync state; distinct
  descriptor not reused) and `host_image_allocation` pass; full CTest **55/55**.
- `d107-imgpool-a-20261006-074140` (421.8 s, 2560x1440/Mailbox, ShaderValidation+red-zone, no
  device loss/FATAL, RSS 11,270 MB).
- Pool ON: `hits=51,162 misses=153,571 evict=151,889 recycled_mb=526,111` → create/destroy
  pairs ≈153.6k vs baseline ≈204.7k (**−25%** driver calls). Hit rate 25% only because the
  512 MiB cap (~50 backings at ≈10 MB each) is far below the working set; ~75% evicted before
  reuse. The cap is the binding constraint.
- **No FPS/visual gain claimed:** `fight_detected=False`; 68/68 screenshots black client area
  while title reported fps 30/frames incrementing. Consistent with the automated black window
  in the overnight plan, not a validated live-HUD failure.

Retain/reject: **retain opt-in (default OFF)** + focused test + counters; not proven in a real
fight. 25/30 FPS unmet.

Next: (a) dump engine-presented frames (`*framedump*`) to separate capture/present black from a
real blackout; (b) pursue the pool only with a real-fight A/B and a tuned cap or hot-class
retention.

## Dev 105 — image create/destroy churn attributed: 95% is "no cached image" acquire, not layout expand/replace (2026-10-06, cycle 07:3x)

Cycle goal (overnight plan §3/§4 + ENGINE-REVIEW P5 "find image creation sources before changing
policy"): the command thread's second un-attacked leaf is image create/destroy churn (Dev 102:
`Image::Image` ~57 us + `Image::~Image` ~102 us + `GraphicContext::DeleteImage` ~119 us, 16,047
destroys/30 s ≈ 535/s = 11.5% of the thread). Before changing cache policy, attribute which
acquisition decision creates each native image.

Engine change (diagnostic, always on, additive counters only): `WaitStats::NoteImageCreate(reason)`
+ accessors, `image_creates[6]` in the 30 s `GPU waits` line. Reasons set at the five
`InsertImage` call sites in `textureCache.cpp`: 0 null, 1 stencil association, 2 new acquire
(no cached image), 3 expand, 4 replace (incompatible cached image), 5 other. Files:
`masterSemaphore.h/.cpp`, `textureCache.cpp`. No behavioral change; no default switch needed
(atomic counters).

Verification: build **UFC Dev 105** OK; full CTest **54/54**. Fight
`d105-image-churn-attr-a-20261006-072857` (301.0 s, 2560x1440/Mailbox, ShaderValidation + red-zone
ON, no Tracy, no device loss, no FATAL, RSS 11,032 MB, VRAM 12,380 MB; `fight_detected=False`, so
this is a menu/streaming-load session, not a measured fight).

Measured (cumulative in the 30 s `GPU waits` line):
- **`image creates null=2 stencil=22270 acquire=132115 expand=237 replace=2312 other=0`** over
  301 s. Deltas per 30 s: `acquire` +12.8k–17.0k (~470–570/s), `stencil` +2.4k–3.8k (~85–127/s),
  `expand` ~0. So **~95% of native image creation is the "no cached image for this range" acquire
  path (`FindImage` line ~1454) plus the stencil-association path; layout expand/replace is
  negligible (0.2%).**
- `images gc_freed=0` for the whole run → the texture GC is **not** the churn source. The frees
  come from the acquire/overlap decisions (`ResolveOverlap` evictions/merges) and `FindImage`
  resize, then the same range is re-created next time it is bound. This matches the Dev 102
  fight Tracy (~535 destroys/s), so the churn is present in fighting too.
- Other counters: `gpu_indirect=17569`, `bda sync fast=493572 walk=15178 verify_missed=0`,
  `srt memo reused=304540 inputs=14395633` (~2.1%), `buffers created=12935`.

Retain: the attribution counters (diagnostic, additive, no behavior/FPS claim). 25/30 FPS unmet.

Next hypothesis (directly grounded): a **bounded native-image backing pool** keyed by the Vulkan
create parameters (format/type/extent/layers/mips/samples/usage/flags) in
`GraphicContext::CreateImage/DeleteImage` (vma.cpp), returning freed `VulkanImage+allocation` to a
size-capped free list and recycling with layout reset to `eUndefined`. Frees already run after the
owning timeline tick (deferred `m_slot_images.erase`), so recycled backings are not in flight.
Must be opt-in (`KYTY_UFC_IMAGE_POOL`, default OFF), VRAM-bounded, and shadow-verified (pool
hit/miss + recycled-byte counters) before any default-on change. Measure acquire-rate reduction
and fight frame counter.

## Dev 103/104 — SRT invariant-phi cache measured as a no-op (`srt phi computed=0`): SRT plans never evaluate Phis; #1 leaf is pure per-draw value evaluation — 2026-10-06 (cycle 07:2x)

Cycle goal (overnight plan §3, Dev 102 next hypothesis): reduce the command thread's #1 leaf
`SrtRefreshFlatBuffer` (24.8% of the thread). Cross-draw snapshot reuse (`KYTY_UFC_SRT_MEMO`) is
capped at ~2% by per-draw `user_data` changes, so the remaining candidate was the per-materialization
structural `ResolveInvariantPhi` BFS + heap scratch in `EvaluatePhi`.

Engine change (candidate): per-`ResourcePlan` cache of the structural phi resolution (keyed by the
dense evaluation index) + a `srt phi cached/computed` counter. Files: `ShaderIR.h`, `ir/Program.cpp`,
`ir/passes/SrtWalker.cpp`, `renderer/masterSemaphore.cpp`, `tests/ResourceMaterializationTests.cpp`.

Evidence:
- Build Dev 103 OK; focused `resource_materialization` passed; full CTest **54/54**.
- `d103-srt-phi-cache-a-20261006-071111` (421.9 s, 2560x1440/Mailbox, ShaderValidation + red-zone,
  no device loss, no FATAL, RSS 11,340 MB).
- **`srt phi cached=0 computed=0` for the whole run** → `EvaluatePhi` is never called during
  materialization; UFC SRT plans have no evaluated Phis (diamond phis are lowered to host
  `SelectU32`). The candidate is a measured **no-op** and was reverted.
- `srt memo reused=448,533 / inputs=22,574,401` (1.9%), confirming the SRT memo cap.
- Frame windows: 40-80 s 11.47, 80-120 s 15.62, 120-160 s 16.73, 160-200 s 17.57, 200-240 s
  26.15, 240-280 s 26.17, 280-320 s 22.21, 320-360 s 19.09; median 21, peak ~26; workload timing
  differs from Dev 102, so no gain/regression claimed.

Retain/reject: **revert** (build Dev 104, focused + full 54/54). No retained change; 25/30 unmet.

Next: image create/destroy churn (11.5%, 535 destroys/s at ~119 µs) and per-draw descriptor/binding
prep (~23%) are the largest un-attacked leaves; SRT needs a compact precompiled evaluation plan.

## Dev 102 — thread-role registry + kernel/user split: the frame-rate limiter is definitively the GPU command thread, user-space compute-bound (0.84-0.89 cores, 14-18% kernel); SRT refresh is its #1 leaf — 2026-10-06 (cycle 06:5x)

Cycle goal (overnight plan §3, Dev 101's next hypothesis a): Dev 101 showed the busiest thread at
0.88 cores but `GetThreadDescription` resolved no names, so the *identity* of the limiting thread —
and therefore whether to attack per-draw CPU work or cross-thread serialization — was unproven.

Engine change (diagnostic, opt-in sampler already default-off): `SetCurrentThreadName` now also
records `GetCurrentThreadId() -> role` in a process-local registry (`src/common/threads.cpp`); the
sampler prints the registry name first and its `GetThreadDescription` fallback second. The sampler
also reports each thread's kernel/user CPU split (`GetThreadTimes` kernel vs user), so a
syscall/spin-bound thread can be told from a compute-bound one. Also fixed the `std::wstring`
off-by-one in the description path. Files: `src/common/threads.cpp`.

Verification:
- Build `dev102-thread-name-registry` OK (**UFC Dev 102**). Full CTest **54/54**.
- Flight `d102-threadcpu-name-20261006-064823` (301.9 s, 2560x1440/Mailbox, ShaderValidation +
  red-zone ON, no Tracy overhead on the sampler, no device loss; first two launches failed to
  reserve guest address space = known stale-memory hazard, third clean).
- **Result: `tid=44820 Gpu 0.84-0.89 cores (kernel=14-18%)` is the busiest thread every interval.**
  Seven unnamed guest workers ~0.22-0.32 each, total 2.5-3.1 cores. The registry resolves the
  role; the kernel fraction is low, so the thread is **user-space compute-bound**, not
  syscall/spin/lock-bound. This resolves Dev 101's open question: per-draw command-thread CPU work
  is the lever, not cross-thread serialization.
- Frame-counter windows: 120-150 s **18.16**, 150-180 s **23.60**, 180-210 s **27.85** (fight);
  60-90 s **8.02** (intro/entrance trough); 240-300 s **6.0** (post-fight/menu). Median 20, min 1,
  RSS 11,143 MB, background-capture dark (display off), no device loss.
- GPU-wait counters (cumulative by 300 s): `cp_read_fault=4369/51094.0 ms`, `other_threads=6216/59004.7 ms`,
  `gpu_untagged=15960/4303.0`, `cpu_read_fault=2071/1192.8`; srt memo reused=287,441 / inputs=12,474,904
  (~2.3% hit). The two large waits are almost entirely in the **post-fight/menu tail** (last 30 s:
  +198 cp_read_fault calls / +22.0 s; +200 other_threads / +23.1 s), consistent with Task C: the
  media/readback phase blocks the command thread on host reads of GPU-owned memory.
- Fresh fight Tracy, `tracy-zones.csv` (30 s at 120-150 s), leaf ranking on the command thread
  (`CommandProcessor::Process` = 28.38 s of 30 s = **94.6% busy**): `SrtRefreshFlatBuffer` 7.03 s
  (**24.8%**), `PrepareBda`/`SynchronizeBuffersInRanges` 4.53 s (16.0%), `Image::~Image` +
  `DeleteImage` 3.26 s (11.5%, 16,047 destroys = 535/s at ~100-119 us each), `CpOpDispatchDirect`
  2.81 s (9.9%), `MasterSemaphore::Wait` 2.79 s (9.8%).

Retain the registry + kernel split (diagnostic only, default off; no FPS claim, no regression, no
device loss). No commit/push. 25/30 FPS unmet (best window 27.85).

Next hypothesis (b, now grounded): attack the command thread's #1 leaf `SrtRefreshFlatBuffer`
(24.8% of the thread) with the plan's shadow-verified cross-draw reuse (the existing SRT memo only
hits 2.3%), and/or the 11.5% image create/destroy churn (535/s at ~100 us/destroy). BDA sync
(16.0%) is upload work — Dev 100 already showed range-bookkeeping is not it.

## Dev 101 — opt-in per-thread CPU sampler: frame rate is latency/serialization-bound (busiest thread 0.88 cores, GPU <=60% in the best window); no throughput saturation — 2026-10-06 (cycle 06:3x)

Cycle goal (overnight plan §3): the aggregate zone profile is flat and repeated per-draw CPU
optimizations moved no FPS, so identify the actual frame-rate-limiting host thread.

Engine change (opt-in, default OFF): `KYTY_UFC_THREAD_CPU=1` (label `threadcpu`). A sampler thread
every 30 s enumerates the process threads (`CreateToolhelp32Snapshot` + `GetThreadTimes`) and prints
each thread's CPU as busy cores; `SetCurrentThreadName` names Gpu/Present/Main. Code:
`src/common/threads.cpp`, `AutoFight.ps1`.

Evidence:
- Build Dev 101 OK; full CTest **54/54**.
- `d101-threadcpu-20261006-062645` (421.7 s, 2560x1440/Mailbox, ShaderValidation+red-zone ON, no
  Tracy, no device loss; first launch failed to reserve guest address space = stale-memory hazard,
  reran). Per-thread CPU: **total 2.5-3.2 cores, busiest thread only 0.84-0.90 cores**, seven workers
  ~0.25-0.33 each -> **no thread saturated**.
  | window | fps | GPU med (max) | CPU cores |
  |---|---|---|---|
  | 100-150 s | 14.54 | 38% (45%) | 2.79 |
  | 150-250 s | 19.73 | 44% (60%) | 2.92 |
  | 250-300 s | 24.62 | 53% (60%) | 2.85 |
- Even in the best gameplay window neither CPU nor GPU is saturated: FPS is limited by serialized
  dependencies/waits, not per-draw throughput. GPU-wait counters: `other_threads` ~25.1 s and
  `cp_read_fault` ~17.4 s cumulated by 420 s (cross-thread / host-read sync).
- Limitation: `GetThreadDescription` returned no names (top tids all `-`), so the 0.88-core leading
  thread's identity is unproven (GPU command thread expected).

Retain: diagnostic only, default off. No FPS gain claimed. No commit/push.

Next: (a) in-process tid->name registry, re-run, identify the leading thread; (b) attack
serialization (command-lane `SendCommandSync` round-trips, GPU-owned host reads/writes draining the
queue) instead of per-draw CPU work.

## Dev 100 — media/intro framedump acceptance: intro/menu/gameplay render correctly; one transient all-black presented frame at ~61 s scene cut; intro 8.2 fps, sustained 21.8 (no engine change) — 2026-10-06 (cycle 06:16)

Cycle goal (overnight plan §2, immediate mandate "working media/intro playback"): make the
intro/media window visible with the monitor output off, confirm playback renders, measure pacing,
and check for the on-load blackout. The earlier intro capture (`d094-load-media-diag`) was blind
(display off), so it could not settle playback.

Action: ran the Dev 099 opt-in engine `FrameDumper` (`KYTY_UFC_FRAME_DUMP`, label `*framedump*`)
via the worker — `d095-load-media-framedump-20261006-061041`, 300.7 s, 2560x1440/Mailbox,
ShaderValidation+red-zone ON. No device loss, breadcrumbs complete, no FATAL/validation errors.
No engine change this cycle.

Evidence (engine-presented frames, 3840x2160 -> 960x540, 131 frames):
- Visual: EA Sports UFC 5 splash (f2), main menu (f12), T-Mobile Arena entrance + walk-in corridor
  (f24-28), fighters + referee in cage (f32-34), full gameplay ROUND 1 with HUD (f36..f130).
  Intro/menu/gameplay **render correctly**.
- Transient black: f8-10 (~17-21 s) = normal splash->menu load; f24-25 = dim cinematic entrance
  (not black); **f30 (~61 s) fully black at the corridor->arena cut** — a single all-black
  presented frame, the class of the reported blackout, but not sustained.
- Frame-counter windows: 50-90 s **8.23**, 100-150 s **15.19**, 150-250 s **21.77**, 250-300 s
  **16.25**; median 17; peak ~27 (217-239 s). The 8.2 trough = entrance/scene cut.
- Intro 45-105 s Tracy: `PrepareBda`/`SynchronizeBuffersInRanges` 9.7% (138,812 calls, mean
  74 us) is the largest self-contained cost; `SrtRefreshFlatBuffer` 9.5%; one-off
  `CpOpDispatchDirect` max **4.69 s**; `DrawIndex` 33.5%; `RefreshShaders` 13.0%.

Retain/reject: no engine change; no FPS gain claimed; 25/30 FPS unmet (best sustained 5-min window
21.8, peak 27). No commit/push.

Next: (a) bound whether the first presented frame after a scene switch can be a cleared/unprepared
image (1 s-cadence dump around a known cut); (b) identify the 4.69 s `CpOpDispatchDirect` intro
stall from the guest dispatch packet/CS hash. Sustained-FPS target stays BDA-sync/SRT per-draw.

## Dev 100 — BDA dirty-log fast-path range merge: implemented, unit/fixture-verified (54/54), 2v2 A/B shows no FPS benefit → kept opt-in default-off — 2026-10-06 (cycle 06:05)

Cycle goal (overnight plan §3, evidence-first): eliminate the per-range `m_buffers` tree search in the
BDA dirty-log fast path — the largest self-contained per-draw cost in the fresh Dev 099 30 s fight
Tracy (`Draw::PrepareBda`/`SynchronizeBuffersInRanges` 3.11%, 51,777 calls, mean 91.7 us; 6.92% in the
45–105 s early-load capture). The full-walk path already retains a buffer cursor to avoid one tree
search per range; the fast path did not.

Change (opt-in, default OFF, `KYTY_UFC_BDA_SYNC_MERGE`, label `bdamerge`): the fast path clips the
CPU-dirty + registered ranges, sorts/coalesces them (`CoalesceRanges` in `rangeSet.h`), and walks
`m_buffers` once with a retained cursor. `SynchronizeBuffer` never mutates `m_buffers`; coalescing
cannot drop a range and `ForEachUploadRange` still uploads only CPU-dirty pages, so widened gaps
upload nothing clean. Code: `bufferCache.{h,cpp}`, `rangeSet.h`, `AutoFight.ps1`, `CMakeLists.txt`,
`tests/MemoryTrackerTests.cpp`.

Evidence:
- New `TestCoalesceRanges` (memory_tracker): sort, overlap, adjacent, nested, single, empty — passed.
- New ctest **`buffer_cache_ranges_merge`** runs the existing native-readback BDA fixture
  (`--buffer-cache-range-only`) with `KYTY_UFC_BDA_SYNC_MERGE=1`; it passed, so the merged walk uploads
  exactly the ranges the per-range path did (real `vkCmdCopyBuffer` readback + `BdaSyncOutcome` checks).
- Full CTest **54/54** (prior 53/53).
- 2v2 A/B, 2560x1440/Mailbox, ShaderValidation/red-zone ON, no Tracy, 301 s each, no device loss:
  | run | 50–90 s | 100–150 s | 150–250 s | max RSS |
  |---|---|---|---|---|
  | `d100-bdamerge-a` | 7.76 | 16.13 | 21.99 | 11046 |
  | `d100-bdamerge-b` | 8.06 | 15.92 | 22.22 | 10982 |
  | `d100-bdabase-a` | 7.63 | 16.45 | 21.86 | 10974 |
  | `d100-bdabase-b` | 8.26 | 17.60 | 18.13 | 11113 |
  Merge means 16.03/22.11 vs base 17.03/20.00 at 100–150/150–250; intro identical. Run-to-run
  workload-phase variance (base-b 18.13) exceeds any difference.

Retain/reject: retain opt-in default-off (neutral, no regression, no device loss — same precedent as
the Dev 095 metadata-clear memo). Negative result: the BDA-sync cost is upload/metadata work, not
per-range tree searches. No FPS gain claimed; 25/30 FPS unmet (best sustained window ~22–23).
No commit/push.

New attribution (from the existing early-load Tracy zones, `d094-load-media-diag-20261006-022414`):
the intro (45–105 s) is command-thread-bound — `CommandProcessor::Process` 53.4%, `DrawIndex` 33.0%,
`RefreshShaders` 14.5%, `MaterializeResources` ~13% — with large one-off stalls (single
`CpOpDispatchDirect` max 4.69 s, `PrepareBda` max 17.67 s, `ReadMemory` max 170 ms,
`MasterSemaphore::Wait` 6.82%). Next: target the intro stalls or per-draw SRT materialization.

## Dev 099 — opt-in engine presented-frame dump works display-off; intro, menu and gameplay all render correctly (2 runs) — 2026-10-06 (cycle 05:10)

Cycle goal (overnight plan §2 and its declared next job): restore unattended visual truth while the
monitor output is off, so media/intro playback and correctness can be judged, and use it to settle the
reported immediate-on-load black.

Engine change (opt-in, default OFF): added a `FrameDumper` to `Presenter::Impl::Present`
(`KYTY_UFC_FRAME_DUMP=<dir>`, `_START_MS` default 1000, `_INTERVAL_MS` default 2000, `_MAX_WIDTH`
default 960). It records one `vkCmdCopyImageToBuffer` of the prepared guest frame into a host-visible,
coherent buffer inside the existing present command buffer, then polls the present scheduler timeline
(`IsFree`) on the *next* present and encodes a downscaled PNG. There is deliberately no in-present wait
(that stall hung the Dev 096 attempt). Handles the game's 10-bit presented format
(`A2R10G10B10`/`A2B10G10R10_UNORM_PACK32`) as well as 8-bit UNORM. `AutoFight.ps1` gained a
`*framedump*` label hook. The dump is diagnostic-only; the default path is unchanged.

Evidence:
- Full CTest **53/53** after the Dev 098 and again after the Dev 099 edits. A first Dev 098 dump run
  wrote one 4-byte frame then rejected the 10-bit presented format (value 58); 10-bit support added.
- `d099-framedump-diag2` (301.12 s, 132 frames, source 3840x2160): median 17–20; frame-counter windows
  60–100 s 10.04, 100–150 s 16.23, 150–250 s 23.29, 250–300 s 23.20; RSS 11.1 GB; no device loss.
- `d099-framedump-diag3` (240.88 s, 104 frames): 9.35 / 15.08 / 20.72; no device loss.
- Control `d099-clean-baseline` (300.95 s, feature OFF): median 17; 9.75 / 17.14 / 20.75 / 17.4; no
  `frames/` directory; no device loss — same fast-mode range, so the opt-in feature does not regress it.
- Visual (identical in both dump runs): EA Sports UFC 5 splash, the full animated intro cinematic
  (helmet/soccer-ball), the main menu, and full gameplay — Makhachev vs Della Maddalena with the
  health/stamina HUD, round timer, cage, crowd and referee. Wait deltas unchanged in kind (cp_read_fault
  remains the largest attributable site at ~9.8 s cumulative).

Retain/reject: retain the dump as an opt-in acceptance tool (no device loss, unchanged default path).
No FPS gain claimed; 25/30 FPS unmet (best sustained window ~23.3). No commit/push.

Next: drive per-draw/intro A/B with this evidence — `RefreshShaders` (`MaterializeResources`/SRT walk),
`Draw::PrepareBda`, `cp_read_fault`; and sample the intro at a fine cadence to attribute the cinematic
and menu stalls. The reported on-load black is not reproducible in the automated path.

## Dev 097 — the overnight "blackout" is the display output being OFF, not a renderer regression; Dev 095 metadata-clear memo A/B shows no gain (2v2) — 2026-10-06 (cycle 04:02–04:35)

Cycle goal (overnight plan §1, then §3): resolve whether the since-02:24 all-black captures are a real
renderer regression, and if inconclusive, advance an evidenced measured milestone. Both done; no engine
change this cycle (diagnosis correction + retained-feature A/B).

Correction — the black is a display-off artifact, not a rendered black scene:
- Right now `nvidia-smi --query-gpu=display_active` = **Disabled** and `powercfg /requests` shows
  **DISPLAY: none** (only a SYSTEM request from the supervisor's python). With the display output
  disabled the desktop compositor does not update, so `CopyFromScreen`, `PrintWindow` and a direct
  desktop-DC capture all return a black client area while the emulator keeps presenting.
- `SetThreadExecutionState(DISPLAY|SYSTEM)` (returns prior state 2147483648, i.e. accepted), the
  monitor-power VK message (`WM_SYSCOMMAND 0xF170,-1`), and synthetic mouse/keyboard input all leave
  `display_active=Disabled` (monitor physically off / output off). Screen capture is therefore
  impossible overnight; the Dev 097 `CopyFromScreen` "fix" did not and could not change that (3.1 vs
  the old 10.6 are both black).
- The first all-dark run is `d094-load-media-diag` at 02:24; the last bright runs were `d094-all` at
  01:10/01:21/01:27 while the user was awake. The transition matches the user going to sleep and the
  monitor turning off, **not** a build change.
- The `frame`-counter and RSS trajectories of a "bright" (01:10, Dev 094 all-on) and a "black"
  (03:57 all-off) run are nearly identical (both 60 fps early → ~25 fps at 30 s → collapse at ~53 s,
  RSS to ~10–15 GiB; same 284 VS / 245 PS / 270 CS), so the guest does identical work in both. This is
  a measurement artifact, not a render defect.
- Therefore the previous cycle's "blackout now reproduced as real, not a Dev 094 regression" is
  superseded: the user's on-load blackout is **not currently reproducible** (screen is off), and no
  Dev 094 default feature is implicated (all-off `d097-bis-abcdefghijklmnop` was black only because the
  display was off).

Measured milestone — Dev 095 metadata-clear memo A/B (plan §"A/B the memo before enablement"), using
frame-counter FPS, which is valid with the display off:
- `d097-memclear-on-a`/`-b` (KYTY_UFC_META_CLEAR_MEMO=1): 100–150 s = 16.50 / 17.47, 150–250 s = 20.30 /
  21.01, 250–300 s = 20.62 / 27.24; RSS 11.0 GB.
- `d097-memobase-a`/`-b` (memo OFF): 100–150 s = 18.24 / 16.29, 150–250 s = 26.29 / 16.53, 250–300 s =
  15.71 / 15.47; RSS 10.9 GB.
- Pre-fight 60–100 s is identical across all four (9.73 / 10.25 / 9.74 / 9.83), i.e. comparable runs.
  2v2 means are effectively equal (100–150: 16.99 vs 17.27; 150–250: 20.66 vs 21.41) with large
  run-to-run phase variance. **No FPS benefit from the memo; it stays opt-in default-off.**
- One launch failed with reservation exit 321 duration 0 (known rerun), then ran clean.

Retain/reject: no engine change this cycle. Keep the memo opt-in default-off. No FPS gain claimed;
25/30 FPS unmet. No device loss in any of the four runs. No commit/push.

Next (steps 2/3, display off): (1) engine-owned presented-frame output is the only way to re-enable
overnight visual truth — implement it off the present command buffer (dedicated one-shot submission, no
in-present wait) with a verify mode, since the Dev 096 in-present readback hung; (2) meanwhile target the
measured per-draw/intro work with frame-counter A/B: `cp_read_fault` (8.5–10.4 s/run), `Draw::PrepareBda`
+ `SynchronizeBuffersInRanges`, `PrepareDrawRenderState`/`RefreshShaders`/`SrtRefreshFlatBuffer`.

## Dev 097 — overnight capture was blind: PrintWindow returns black; fixed with DPI-aware screen capture, and the current blackout is reproduced as real (all Dev094 defaults OFF still black) — 2026-10-06

Cycle goal (overnight plan §1/§2): make overnight visual measurement truthful so media/intro playback and
the reported immediate-on-load blackout can be judged at all, then use it to isolate the blackout.

Work: the only edit this cycle is `AutoFight.ps1`. (1) Keep the Windows display composited for the run
(`SetThreadExecutionState` DISPLAY|SYSTEM, released at the end) and force the capture window visible/topmost
and foreground. (2) Replace the screenshot path: `WinCap::ShotBrightness` now captures the composited screen
region (`CopyFromScreen`), with the process made DPI-aware so `GetWindowRect` returns physical coordinates;
it measures only the client rows (skips the title bar). `PrintWindow` remains available behind
`-ScreenCapture:$false`. No engine/build/install change; Dev 097 install untouched.

Evidence:
- `PrintWindow` is the blind path. A desktop screen capture taken while a run was live
  (`%TEMP%\opencode\deskcap\desk2_09.png`, 2560x1440) shows the emulator window's client area is genuinely
  black while its title bar renders `[UFC Dev 097 ...] frame: 2120, fps: 8`. All overnight runs since
  02:24 (`d095-memoclearverify`, `d096-control-a`, `d097-revert-baseline`) reported constant brightness
  ~10.6 from PrintWindow, and `d094-load-bis-ab` (Dev 094) was black at 02:33 while the same all-on Dev 094
  build showed content at 01:12 — the change was in capture/environment, not the build.
- The DISPLAY power request was verified to register (`powercfg /requests` DISPLAY: [powershell]) and the
  emulator window was reported `visible=True fg=True`, yet PrintWindow still returned black — so the earlier
  "display not composited" explanation is insufficient. `CopyFromScreen` is the working capture.
- After the fix, `d097-screencap-test3` (physical shot 2582x1496) reports the client brightness **3.1** (dark),
  i.e. truthful, versus the old constant 10.6.
- Blackout isolation: `d097-bis-abcdefghijklmnop` (all Dev 094 default features OFF, 151.12 s, timeout)
  recorded **25/25 dark** client frames and no fight. So none of the Dev 094 bake-off switches (GPU DCC,
  fill-convert, BDA dirty log/table-init, mprotect-invalidate, SRT memo, indirect paths, GC/power) causes
  the black scene — it reproduces on the common path.
- Regression: full CTest **53/53**; automation harness `tests/AutomationHarnessTests.py` **23/23**.

Other runs this cycle: `d097-displayon-baseline` 361.18 s (timeout, median 18; PrintWindow still all dark),
`d097-topmost-capture-test` 121.48 s, plus two short reservation-failure launches
(`failed to reserve guest address space`, exit 321 at duration 0 — rerun, not data) and one intermittent
device-loss (`d096`-class TDR: nvlddmkm 153, DrawIndex breadcrumbs, WriteInvalid 0x0).

Retain/reject: retain the AutoFight capture fix (verified truthful, no engine/FPS effect). No engine change,
no commit/push. No FPS gain claimed; 25/30 FPS unmet. The early-on-load blackout is now reproducible with a
live emulator and is **not** a Dev 094 default-feature regression.

Next hypotheses: (1) the guest may be stuck on a black screen that should carry media/intro (the user's
target) — inspect the guest video-decode path (no video decoder activity appears in console besides MP3);
(2) saved profile/save state or guest network/login could hold it at a black screen (identical build rendered
content earlier); (3) GPU/driver state after the Dev 096 TDR — control with a freshly reset driver/reboot.
With truthful capture, re-run the known-good Dev 094 all-on case and a fight to confirm capture turns bright
when content renders, then attribute the stuck-black scene from console/Tracy.

## Dev 097 — engine presented-frame dump attempted, reproducibly hung the GPU, reverted; frame-counter FPS proven valid with the display off — 2026-10-06

Cycle goal (overnight plan §1/§2 and the Dev 095 next hypothesis): make overnight visual measurement
independent of the desktop compositor by adding an emulator-owned presented-frame dump, so the
black-on-load scene and media/intro playback can be verified with the monitor off.

Implementation (behind `KYTY_UFC_FRAME_DUMP=<dir>` + interval/scale env, default off): in
`Presenter::Impl::Present`, after the present command buffer is recorded, copy the prepared guest
frame (`layers[0].frame`, already in `eTransferSrcOptimal`) to a VMA host-visible buffer, then wait
on the present tick and write a downscaled PNG (`stb_image_write`). AUTOFIGHT gained a temporary
`*framedump*` label hook. Build `dev096-frame-dump` OK (UFC Dev 096).

Result — **confirmed regression, reverted**:
- `d096-framedump-diag` (32.49 s) and `d096-framedump-diag-b` (32.34 s) both died with
  `exit_code=321`, `gpu_device_lost=True`, `gpu_loss_class=timeout`, nvlddmkm 153, and identical
  breadcrumb ends (`DrawIndex`, submit 104690/109036, fault `WriteInvalid reported=0x0`), ~2 s after
  presents resumed post-load. Two dumps were written in each run (framedump_00000/00001), both
  all-black at ~2 s and ~6 s.
- Control `d096-control-a` (same Dev 096 binary, `KYTY_UFC_FRAME_DUMP` unset so the feature is
  inactive) ran **301.09 s clean**, no device loss. A prior 421.69 s Dev 095 run was also clean.
- Two feature-on crashes vs one clean feature-off control on the same binary → the in-present GPU
  readback is the cause, not the known intermittent TDR. Reverted exactly this cycle's edits
  (`swapchain.cpp`, `AutoFight.ps1`); rebuilt as **UFC Dev 097**, full CTest **53/53**, and
  `d097-revert-baseline` ran **301.8 s clean** (timeout, no device loss).

Measurement insight (kept): the `frame` column of `metrics.csv` tracks the real rendered frame
counter even when PrintWindow returns black. Frame-derived FPS over fixed windows is therefore valid
with the monitor off: Dev 097 reverted baseline 100–150 s = **15.26**, 150–250 s = **15.37**;
Dev 096 clean control = **15.93** / **18.02**. This means FightWatch start detection (screenshots)
is the only blind part; throughput can still be compared overnight.

Retain/reject: nothing retained. The engine dump path is rejected as-is (GPU hang), but the
capability is still needed. No FPS gain claimed; 25/30 FPS unmet. No commit/push.

Next hypotheses: (1) do **not** record the readback in the present command buffer — use a dedicated
off-thread one-shot submission on the present queue that waits (timeline) only for the frame's
producer tick and holds the frame until the copy publishes, with a shadow/verify mode; or (2) keep
the display composited during overnight fights (the Dev 095 finding) so PrintWindow capture works,
then A/B the Dev 095 metadata-clear memo with alternating controls; and (3) use the frame-counter
FPS path to compare per-draw/SRT work without needing screenshots.

## Dev 095 — HTile metadata-clear memo implemented (opt-in); overnight fight captures blind when display is not composited — 2026-10-06

Consumed the completed `d094-load-media-diag` evidence (per plan: do not relaunch it) and profiled the
saved 45–105 s Tracy window. Bounded blackout investigation is inconclusive as a render defect: the one
"black" run used `--shader-dump`/1 s captures/early Tracy and never exceeded brightness ~11, while the
three Dev 094 all-on runs reached a fight at ~102 s on the same build. Root cause of black captures found
later this cycle (below): the display is not being kept composited, so `PrintWindow` returns a black client
area while the emulator renders normally.

Engine change (this cycle's milestone): ported the metadata-clear memo from `ports-design-notes.md` §2 as an
opt-in switch. `TextureCache::IsMetaCleared` now reuses its last answer without taking `m_lock` while an
atomic `m_surface_meta_generation` is unchanged; every mutation of `m_surface_metas` (DeleteImage,
MaterializeColorClear, FindDepthTarget emplace, ClearMeta, TouchMeta on mask change, UnmapMemory) bumps the
generation. `KYTY_UFC_META_CLEAR_MEMO=1` enables it; `KYTY_UFC_META_CLEAR_MEMO_VERIFY=1` re-runs the locked
lookup on every hit and EXITs on mismatch. The memo is GPU-thread-only (single caller renderDraw.cpp depth
target) and default OFF, so the default renderer is unchanged.

Evidence: build `dev095-meta-clear-memo` OK (UFC Dev 095). Focused tests `compute_meta_clear_classification`,
`texture_cache_htile_clear`, `texture_cache_layered_image`, `texture_cache_image_overlap` passed; full CTest
**53/53**. Fight `d095-memoclearverify-20261006-025122`, 421.69 s, exit_reason=timeout, no crash/EXIT; **no
memo mismatch over the whole run** (verify mode active). That validates the memo path's correctness on a real
workload. FightWatch did **not** detect a fight because all 68 screenshots were black (brightness max 10.6):
the emulator rendered continuously (title frame 9737, ~17-24 fps; engine counters — gpu_indirect 27531,
buffers created 13235, bda sync fast 650672 — proportionate to the clean baseline), so this is a capture
artifact, not a render regression.

Infrastructure finding (important for the overnight loop): `powercfg /requests` shows DISPLAY: none — no
process holds the display awake (the supervisor holds only a SYSTEM request). With the monitor not
composited, PrintWindow's PW_RENDERFULLCONTENT returns a black client area even though the emulator
presents. This also explains the earlier `d094-load-media-diag` all-black run. Automated fight FPS/FightWatch
are therefore blind whenever the display is off.

Retain/reject: retained as an **opt-in, default-off** engine change (53/53, no default behavior change). No
FPS gain claimed: its fight benefit is unmeasured because no valid fight window was captured this cycle. 30
FPS unmet, 25 FPS unmet with evidence. No commit/push.

Next hypothesis: (1) add an emulator-owned presented-frame dump (independent of PrintWindow/display state) or
hold the display on during fights so overnight measurements are valid; (2) then A/B the memo with alternating
`d095-memoclear-a`/`d095-memclearoff` runs (same scene, foreground) before any default enablement.

## Overnight mandate revised after user clarification — 2026-10-06

User: black immediately on loading into gameplay; restart and continue unsupervised while
they sleep, with media playback and 25 FPS as the immediate measured milestone. They approved
project-only automation recovery, preventing sleep and diagnostics up to 15 minutes. Monitor
may be turned off with its cable connected and the PC/consoles left running.

`OVERNIGHT-PLAN-20261006.md` overrides the old three-run Task A stop and Task B/C deferral.
Startup failure is investigated before/around first gameplay, including pre-HUD images;
if reproduction is inconclusive, proceed to intro/media profiling and evidenced per-draw/
readback/target preparation improvements rather than repeated status-only cycles. No quality
cuts, skipped guest/media work, shader clamps, stale reads or unproved success claims.

At restart preflight both worker and supervisor had exited, no emulator/build/test was active,
and RunnerJob correctly rejected the stale heartbeat. Preparing fresh starts; all GPU/build/
CTest execution remains worker-owned. Added early capture/shader-dump parameters to helper
and stopped worker source, an exclusive worker checkout lock, process-scoped keep-awake,
absent-worker recovery and wrapper retry. Two no-progress cycles trigger a brief model cooldown
and next-action guidance. These operational controls are not an engine/FPS improvement.
First job: `d094-load-media-diag` with a 420 s diagnostic, 60 s Tracy starting at 45 s,
1 s requested screenshot interval and shader dump; verify the actual video window before
attributing decoder cost. Resume real corrective and performance work from its evidence.

## Dev 094 — repeated-blocker loop corrected; gameplay needs reproduction details — 2026-10-06, 02:04

User asked why no gameplay was visible. The runner was healthy and queue empty; no emulator
was active. Three valid all-on runs had already completed (13.20/18.93/20.73 frame-derived FPS,
~199 s fighting each, no captured blackout or GPU loss). Cycles after the bounded Task A limit
kept returning the same blocker and appending status text. Those cycles were not useful progress.

Added a required-input gate: `_Build/agent-loop/WAIT-FOR-INPUT.json` contains the actual missing
reproduction details and the three evidence sessions. Run-AutonomousLoop.py now reports
`waiting for user input`, polling every 60 seconds before invoking any model. Agent instructions
request one blocker record rather than repeated unchanged handoffs. Malformed requests fail
closed; removing the request after new user information restores normal ownership checks.

Four focused CPU-only automation checks passed, including wait/no-cycle-spend/clear-to-resume
and the existing Sol/OpenRouter fallback checks. `opencode debug agent ufc-loop` validated the
agent file, and scoped git diff whitespace checks passed. Reloaded between cycles without
terminating the worker, emulator or a model invocation; old supervisor completed cycle 53,
new PID 43912 is alive in `waiting for user input`, STOP absent and worker heartbeat fresh.
No engine candidate/build/C++ tests or new gameplay occurred in this scheduler correction.

Next required information: roughly how long into playing the blackout occurred, first or later
match in one process, and mode/fighters (ideally a live-HUD screenshot). Record new details or
explicit new user steering before clearing the request; do not restart the three-run limit or
infer feature safety from negative evidence. OpenRouter DeepSeek fallback remains active.

## Dev 094 — Task A bounded stop verified; actual trigger missing — 2026-10-06, 02:02

Read overriding plan first, transfer paths and required reviews; verified the original
three-baseline-limit entry. Three prescribed all-on attempts captured no blackout, and
this request supplies no new trigger. No fourth baseline, negative-only bisect or
speculative engine/default change. Ownership snapshot: no UFC launcher/emulator or
competing build/test/game automation; caller KYTY_UFC_* empty. Runner heartbeat
02:02:39.637 was ~2 s old at clock snapshot. No job queued; runner/supervisor untouched.
Build remains Dev 094, branch dev; all pre-existing work preserved. Files edited:
HANDOFF.md, LOOP-LOG.md only. Build/focused/full tests not run; historical 53/53 unchanged.
No new session or measurements. Prior sessions under `_Build/autolab/`, respectively:
`d094-all-20261006-011057`, `d094-all-20261006-012113`, `d094-all-20261006-012712`.
Scenario: Makhachev/Della Maddalena, T-Mobile arena, standing/ground, 2560x1440/Mailbox,
validation/red-zone ON. first_fight_s 102.00/101.89/101.92; fight_frames 2601/3741/4095;
fight_frame_rate 13.20/18.93/20.73; median/min 14/2, 19/7, 22/12; opening FPS
14.02/15.97/15.28; RSS 15303.3/11111.8/11017.7 MiB; device VRAM 15062/13321/12503 MiB.
Saved run 2/3 final wait-site ms/s: gpu_untagged 18.35/3.06, cpu_write_flush 2.25/0.47,
cpu_read_fault 0.10/0, srt_read_fault 0/0, cp_read_fault 37.97/49.32,
other_threads 54.14/61.35; color_metadata/indirect_read_fault/args_copy zero counted waits.
Existing captures show fighters/mat/cage/HUD and old speckling, not a visual-correctness
verdict; manual transient/ground-and-pound check missing. No candidate retained/rejected,
feature clearance or performance gain claimed; 30 FPS unmet. Task B/C deferred.
Concrete blocker/next hypothesis: actual blackout timing, first/later match in one process,
mode/fighters and ideally a live-HUD black screenshot needed. Longer/later-match trigger
unproven. Resume prescribed halving after positive reproduction, two clean runs per clean
verdict. No commit/push or process termination.

## Dev 094 — Task A blocked on actual blackout scenario — 2026-10-06, 02:01

Verified current plan, transfer paths, required reviews and saved three-baseline-limit
evidence. No new trigger supplied; all three prescribed all-on attempts captured no
blackout. No fourth baseline, negative-only bisect or speculative engine/default change.
No UFC launcher/emulator or competing build/test/game automation observed; caller
KYTY_UFC_* empty. Heartbeat content 02:01:30 was ~10 s before clock snapshot; no job
queued. Branch dev/installed Dev 094/all existing uncommitted work preserved.
Files edited: HANDOFF.md, LOOP-LOG.md only. Build/focused/full tests not run; historical
53/53 unchanged. No new session or visual evidence. Prior `_Build/autolab/d094-all-`
sessions `20261006-011057`, `20261006-012113`, `20261006-012712`: first_fight_s
102.00/101.89/101.92; frames 2601/3741/4095; frame rate 13.20/18.93/20.73 FPS;
fight median/min 14/2, 19/7, 22/12; max RSS 15303.3/11111.8/11017.7 MiB;
device VRAM 15062/13321/12503 MiB. Opening FPS 14.02/15.97/15.28; wait-site ms/s
remain in the three-baseline-limit entry. Scenario: Makhachev/Della Maddalena,
T-Mobile arena, standing/ground, 2560x1440/Mailbox, validation/red-zone ON.
No candidate retained/rejected, feature cleared or performance gain claimed; manual
transient-artifact check missing, 30 FPS unmet. Task B/C deferred. Concrete blocker:
need blackout timing into fighting, first/later match in one emulator process and
mode/fighters, ideally a black-scene screenshot with live HUD. Longer/later-match
trigger remains unproven. Resume prescribed halving after positive reproduction,
two clean runs per clean verdict. No runner/supervisor changes, commit or push.

## Dev 094 — Task A non-reproduction stop; scenario needed — 2026-10-06, 02:00

Read current plan first, transfer settings, ufc-loop and required review sections;
verified the three-baseline-limit evidence. No new blackout trigger supplied. The
prescribed three all-on attempts already captured no blackout; no fourth baseline,
negative-only feature bisect or speculative default change is justified.
Ownership: no UFC launcher/emulator or competing build/test/game automation observed;
caller KYTY_UFC_* empty. Runner heartbeat 02:00:21 was ~2.2 s old at the clock snapshot.
No runner job queued. Installed Dev 094/branch dev/all existing work preserved.
Files changed: HANDOFF.md, LOOP-LOG.md only. Build/focused/full tests not run;
historical Dev 094 53/53 remains the latest evidence. No new gameplay session or metrics.
Prior sessions under `_Build/autolab/`: `d094-all-20261006-011057`, `...-012113`,
`...-012712`; first_fight_s 102.00/101.89/101.92, fight frames 2601/3741/4095,
fight_frame_rate 13.20/18.93/20.73, fight median/min 14/2, 19/7, 22/12,
max RSS 15303.3/11111.8/11017.7 MiB, device VRAM 15062/13321/12503 MiB.
Opening windows and wait-site ms/s remain unchanged in the three-baseline-limit entry.
No new visual evidence; manual transient-artifact check missing. No candidate retained/
rejected, feature cleared or performance gain claimed; 30 FPS unmet; Task B/C deferred.
Concrete blocker: actual blackout timing, first/later match in one emulator process,
mode/fighters and ideally a screenshot with black scene and live HUD are missing.
Longer/later-match triggering is unproven. Resume prescribed halving after a positive
reproduction, two clean runs per clean verdict. No runner/supervisor modifications,
process termination, commit or push.

## Dev 094 — Task A stop condition verified; trigger missing — 2026-10-06, 01:59

Read NEXT-STEPS-DEV094 first, transfer paths and required review sections. Runner
availability `oc20261006-015906-gitstatus`: **exit 0**. No UFC launcher/emulator or
competing build/test/game automation observed; caller KYTY_UFC_* empty.
Saved three-baseline-limit entry confirms no captured blackout in the three prescribed
all-on attempts. No new trigger supplied; stop condition still applies. No redundant
baseline, negative-only bisect, speculative default or Task B/C engine patch.
Installed Dev 094/branch dev/all pre-existing work preserved. Files changed this
invocation: HANDOFF.md, LOOP-LOG.md only. Build/focused/full tests: not run; supplied
**53/53** remains historical. No new session/scenario or metrics. References under
`_Build/autolab/`: `d094-all-20261006-011057`, `...-012113`, `...-012712`;
first_fight_s **102.00/101.89/101.92**, fight frames **2601/3741/4095**, frame-derived
FPS **13.20/18.93/20.73**, fight median/min **14/2, 19/7, 22/12**, max RSS
**15303.3/11111.8/11017.7 MiB**, device VRAM **15062/13321/12503 MiB**.
Prior opening-window and wait-site ms/s evidence unchanged in the three-baseline entry;
no new visual evidence, manual transient check missing. No candidate retained/rejected,
feature clearance or improvement claimed; **30 FPS unmet**. Concrete blocker/next
hypothesis: missing actual blackout timing, first/later match in one process and
mode/fighters; ideally obtain a live-HUD black screenshot. Longer/later-match triggering
is unproven. Resume prescribed halving after positive reproduction with two clean runs
per clean verdict. No runner/supervisor modifications, process termination or commit/push.

## Dev 094 — Task A bounded stop; reproduction details missing — 2026-10-06, 01:58

Current plan/transfer paths/required reviews read; saved three-baseline evidence
verified. Runner availability job `oc20261006-015757-gitstatus` exited 0. No UFC
launcher/emulator or competing build/test observed; caller KYTY_UFC_* empty.
The prescribed three all-on attempts have no captured blackout; no new trigger
supplied. Do not reset the count or infer feature safety from negative-only evidence.
Dev 094/branch dev/existing uncommitted work preserved. Files edited this invocation:
HANDOFF.md/LOOP-LOG.md only. No build, focused/full tests or new fight/session;
historical 53/53 and saved first_fight_s, fight_frame_rate/frames, median/min FPS,
RSS/VRAM, wait-site ms/s and visual evidence unchanged (three-baseline entry below).
No candidate retained/rejected or performance gain claimed; manual transient check
missing, 30 FPS unmet. Task B/C deferred. Concrete blocker: actual blackout timing,
first/later match in the same process and mode/fighters missing; ideally obtain a
live-HUD black screenshot. Longer/later-match triggering remains a hypothesis.
Resume prescribed halving after positive reproduction, two clean runs per clean
verdict; no redundant baseline, speculative defaults, runner/supervisor changes,
commit or push.

## Dev 094 — Task A reproduction blocker; bounded stop retained — 2026-10-06, 01:56

Current plan read first, transfer paths and required reviews verified. Saved three-baseline
entry confirms the all-on reproduction limit was exhausted without captured blackout;
no new triggering scenario supplied. Do not reset the count or infer feature safety from
negative-only evidence. No UFC launcher/emulator or competing build/test observed;
caller KYTY_UFC_* empty; heartbeat content 01:56:47 was ~1.5 s before clock snapshot.
Branch dev/installed Dev 094/all pre-existing engine work and defaults preserved.
Files edited: HANDOFF.md/LOOP-LOG.md only. No runner job, build, focused/full tests or
new fight/session; historical 53/53 and three-baseline session/scenario, first_fight_s,
fight_frame_rate/frames, median/min FPS, RSS/VRAM, wait-site ms/s and visuals unchanged.
No candidate retained/rejected, feature clearance or speedup claimed. Manual transient
check missing; 30 FPS unmet; Task B/C deferred. Concrete blocker: blackout time into
fighting, first/later match in one process and mode/fighters unknown; ideally obtain a
live-HUD black screenshot. Longer/later-match trigger remains a hypothesis. Resume
prescribed halving after positive reproduction, two clean runs per clean verdict.
No redundant fight, speculative engine/default patch, runner/supervisor change or commit.

## Dev 094 — Task A bounded stop verified; no new trigger — 2026-10-06, 01:55

Read the current plan first, transfer paths, ufc-loop and required review/history sections.
Verified the three-baseline entry: all-on reproduction attempts exhausted without captured
blackout. No new scenario supplied; do not restart the count, infer a culprit from negative
evidence or proceed to Task B/C. No UFC launcher/emulator or competing build/test observed;
caller KYTY_UFC_* empty. Heartbeat content 01:55:43 was four seconds before the clock check;
no runner job queued. Branch dev/installed Dev 094/existing uncommitted work preserved.
Files changed: HANDOFF.md/LOOP-LOG.md only. No build, focused/full tests or new session;
historical 53/53 and saved session/scenario, first_fight_s, fight_frame_rate/frames, median/min,
RSS/VRAM, wait-site ms/s and visuals unchanged. No engine candidate retained/rejected,
feature clearance or performance gain claimed; manual transient check missing, 30 FPS unmet.
Concrete blocker/next evidence: blackout time into fighting, first/later match in one process,
mode/fighters, ideally a live-HUD black screenshot. Longer/later-match triggering unproven.
Resume prescribed feature halving after positive reproduction, two clean runs per clean
verdict. No redundant fight or speculative engine/default patch; runner/supervisor untouched.

## Dev 094 — Task A stop condition; missing reproduction scenario — 2026-10-06, 01:54

Current plan read first; transfer paths and required review/history sections verified.
Runner availability **oc20261006-015439-gitstatus**, **exit 0**; no UFC launcher/emulator
or competing build/test observed; caller KYTY_UFC_* empty. The three saved all-on attempts
already exhaust the bounded reproduction sequence without captured blackout. No new trigger
supplied; no further baseline, negative-only bisect or speculative engine/default patch.
Dev 094/branch dev/existing work preserved; HANDOFF.md/LOOP-LOG.md only edited. No build,
focused/full tests or new fight/session. Historical **53/53** and three-baseline session,
first_fight_s, frame rate/frames, median/min FPS, RSS/VRAM, wait-site ms/s and visuals unchanged.
No candidate retained/rejected or gain claimed; manual transient check missing, **30 FPS unmet**.
Task B/C deferred. Next evidence needed: actual blackout timing, first/later match in one
process, mode/fighters, ideally a live-HUD black screenshot. Longer/later-match trigger remains
a hypothesis. Resume prescribed bis-letters halving after positive reproduction, two clean
runs per clean verdict. Runner/supervisor untouched; no commit/push or process termination.

## Dev 094 — Task A reproduction blocker verified — 2026-10-06, 01:53

Read current plan first, transfer paths and required review/history sections. Runner availability
job **oc20261006-015318-gitstatus**, **exit 0**. No UFC launcher/emulator or competing build/test
observed; caller KYTY_UFC_* empty. Existing three all-on baselines exhausted the prescribed
reproduction limit without captured blackout, and this request supplies no new trigger.
Preserved Dev 094/branch dev/defaults and all existing uncommitted work. Files changed this
invocation: HANDOFF.md/LOOP-LOG.md only. No build, focused/full tests or gameplay; historical
**53/53** and three-baseline session/scenario, first_fight_s, fight_frame_rate/frames, median/min,
RSS/VRAM, wait-site ms/s and visual evidence unchanged. No candidate retained/rejected or
performance improvement claimed; manual transient-artifact check missing, **30 FPS unmet**.
Blocker/next hypothesis: need actual blackout timing, first/later match in the same process,
mode/fighters and ideally a live-HUD black screenshot; longer/later-match triggering unproven.
Do not reset the baseline count or infer a culprit from negative-only bisects. Resume prescribed
halving after positive reproduction, two clean runs per clean verdict. Task B/C deferred;
no runner/supervisor changes, commit or push.

## Dev 094 — Task A non-reproduction blocker; no redundant run — 2026-10-06, 01:52

Read current plan first, transfer paths, ufc-loop and required review/history sections.
Three recorded all-on attempts already exhaust the plan's reproduction limit; no new trigger
provided. Runner availability job **oc20261006-015211-gitstatus**, **exit 0**. No UFC
launcher/emulator or competing build/test observed; caller KYTY_UFC_* empty. Branch dev,
installed Dev 094, defaults and existing engine work preserved. Files edited: HANDOFF.md and
LOOP-LOG.md only. No build, focused/full tests or fight; supplied **53/53** remains historical.
No new session/scenario, first_fight_s, fight_frame_rate/frames, median/min FPS, RSS/VRAM,
wait-site ms/s or visuals; existing three-run reference remains below. No candidate retained/
rejected or performance gain claimed; manual transient check missing, **30 FPS unmet**.
Blocker/next hypothesis: missing actual blackout scenario, potentially longer/later-match
play; need time into fighting, first/later match, mode/fighters and ideally live-HUD black
screenshot. Do not reset the baseline count or infer a culprit from negative-only bisects.
Resume prescribed halving after positive reproduction, two clean runs per clean verdict.
Task B/C deferred; runner/supervisor untouched, no commit/push.

## Dev 094 — Task A blocked; saved baseline summaries verified — 2026-10-06, 01:51

Read current plan first, transfer paths and required ufc-loop/history/review sections.
Verified saved summaries for `_Build/autolab/d094-all-20261006-011057`, `...-012113`,
`...-012712`: first_fight_s **102.00/101.89/101.92**, fight_frames **2601/3741/4095**,
fight_frame_rate **13.20/18.93/20.73**, fight median/min **14/2, 19/7, 22/12**,
RSS **15303.3/11111.8/11017.7 MiB**, device VRAM **15062/13321/12503 MiB**.
All are prior 300 s runs with >198 s fighting and no device loss. No new measurement:
prior opening-window, wait-site ms/s and screenshot evidence remain in the three-baseline
entry below. No captured blackout in the existing scan/review; this is not a visual cure
or feature-clearance verdict. Manual transient-artifact check missing, **30 FPS unmet**.
Runner availability job **oc20261006-015059-gitstatus**, **exit 0**. No UFC launcher/emulator
or competing build/test observed; caller KYTY_UFC_* empty. Branch dev/installed Dev 094
and all pre-existing work/defaults preserved. Files edited: HANDOFF.md/LOOP-LOG.md only.
No build, focused/full tests or fight; supplied **53/53** remains historical. No candidate
retained/rejected or gain claimed. Task A's three-run stop condition applies; Task B/C
deferred. Concrete blocker/next hypothesis: actual blackout timing, first/later match and
mode/fighters missing; longer/repeated-match triggering unproven. Need that scenario,
ideally a live-HUD black screenshot, before positive reproduction and bis-letters halving
(two clean runs per clean verdict). Do not reset baseline count. Runner/supervisor
untouched; no commit/push.

## Dev 094 — Task A reproduction blocker unchanged — 2026-10-06, 01:48

Current plan read first; transfer paths and required ufc-loop/review/history sections read.
Existing three all-on sessions (`...-011057`, `...-012113`, `...-012712`) have no captured
blackout, reaching the prescribed stop condition. No new trigger supplied; no redundant
baseline, negative-only bisect or speculative default change. Runner availability job
**oc20261006-014804-gitstatus**, **exit 0**. No UFC launcher/emulator or competing build/test
observed; caller KYTY_UFC_* empty. Branch dev/installed Dev 094/defaults/existing work
preserved. Files edited: HANDOFF.md/LOOP-LOG.md only. No build, focused/full tests or fight;
latest supplied **53/53** historical. No new session/first_fight_s, fight_frame_rate/frames,
median/min FPS, RSS/VRAM, wait-site ms/s or visuals. No candidate retained/rejected or gain
claimed; manual transient check missing, **30 FPS unmet**. Task B/C deferred. Concrete
blocker/next hypothesis: actual blackout timing, first/later match and mode/fighters missing;
longer/repeated-match trigger unproven. Need that scenario, ideally a live-HUD black screenshot,
before resuming reproduction and feature halving with two clean runs per clean verdict.
Do not reset the baseline count; runner/supervisor untouched, no commit/push.

## Dev 094 — existing Task A reproduction stop verified — 2026-10-06, 01:47

Current plan read first; transfer paths and required notes verified. Existing three all-on
sessions have no captured blackout; no new trigger supplied. Runner availability job
**oc20261006-014700-gitstatus**, exit **0**. No UFC launcher/emulator or competing build/test
observed; caller KYTY_UFC_* empty. Dev branch/installed Dev 094/defaults/existing work
preserved. Files edited: HANDOFF.md/LOOP-LOG.md only. No build, focused/full tests or fight;
latest supplied **53/53** historical. No new session/first_fight_s, fight_frame_rate/frames,
median/min, RSS/VRAM, wait-site ms/s or visuals; preceding three-run reference unchanged.
No candidate retained/rejected, feature cleared or speedup claimed; manual transient check
missing, **30 FPS unmet**. Task B/C deferred. Blocker/next hypothesis: longer/repeated-match
trigger is unproven; need actual blackout time, first/later match, mode/fighters and ideally
live-HUD black screenshot. Do not reset the three-baseline count; resume halving only after
positive reproduction, two clean runs per clean verdict. Runner/supervisor untouched.

## Dev 094 — Task A reproduction blocker remains — 2026-10-06, 01:46

Current plan read first, transfer paths and required notes verified. Three all-on baselines
already exhausted the non-reproduction limit; no new triggering scenario supplied. Runner
availability job **oc20261006-014551-gitstatus**, exit **0**; no UFC launcher/emulator or
competing build/test observed, caller KYTY_UFC_* empty. Branch dev/installed Dev 094 and
all existing work/defaults preserved. Files edited: HANDOFF.md/LOOP-LOG.md only. No build,
focused/full tests or fight; supplied **53/53** historical. No new session/first_fight_s,
fight_frame_rate/frames, median/min, RSS/VRAM, wait-site ms/s or visual observations.
No candidate retained/rejected or speedup claimed; manual transient check missing,
**30 FPS unmet**. Task B/C deferred. Need blackout time into fighting, first/later match,
mode/fighters and ideally a live-HUD black screenshot; longer/repeated-match triggering
remains unproven. Resume halving after positive reproduction, two clean runs per clean
verdict; do not reset the three-baseline count. Runner/supervisor unchanged; no commit/push.

## Dev 094 — reproduction blocker; bounded stop retained — 2026-10-06, 01:44

Current plan, transfer paths and required sections read. Three all-on baselines already
failed to capture blackout; no new triggering scenario supplied. Runner availability job
**oc20261006-014436-gitstatus**, exit **0**; no UFC launcher/emulator or competing build/test
observed, caller KYTY_UFC_* empty. Branch dev/installed Dev 094/defaults/existing work preserved.
Only HANDOFF.md/LOOP-LOG.md edited. No build, focused/full tests or fight; supplied **53/53**
historical. No new session/first_fight_s, fight_frame_rate/frames, median/min, RSS/VRAM,
wait-site ms/s or visual observations; prior three-run evidence remains reference. No new
candidate retained/rejected or gain claimed, manual transient check missing, **30 FPS unmet**.
Task B/C deferred. Blocker/next hypothesis: need actual blackout timing, first/later match,
mode/fighters and ideally live-HUD black screenshot; longer/repeated-match trigger unproven.
Resume prescribed halving after positive reproduction, two clean runs per clean verdict;
do not repeat the exhausted baseline sequence. Runner/supervisor unchanged; no commit/push.

## Dev 094 — bounded Task A stop condition preserved — 2026-10-06, 01:43

Read current plan first, transfer settings and required handoff/history/review sections.
No new blackout scenario supplied; the three recorded all-on baselines already reach
the plan's non-reproduction stop condition. No redundant baseline, negative-only
bisect or speculative engine/default change. Runner `gitstatus` availability job
**oc20261006-014316-gitstatus**, exit **0**. No UFC launcher/emulator or competing
build/test observed; caller KYTY_UFC_* empty. Installed **Dev 094**, branch dev,
existing uncommitted work and defaults preserved; only HANDOFF.md/LOOP-LOG.md edited.
No build, focused/full tests or fight; latest supplied **53/53** remains historical.
No new session/first_fight_s, fight_frame_rate/frames, median/min FPS, RSS/VRAM,
wait-site ms/s or visuals. No candidate retained/rejected or improvement claimed;
manual transient-artifact check missing, **30 FPS unmet**. Task B/C deferred.
Blocker/next hypothesis: missing actual blackout timing, first/later match and
mode/fighters, ideally a live-HUD black screenshot; longer/repeated-match triggering
unproven. Resume `d094-bis-abcdefgh` after positive reproduction, two clean runs per
clean verdict; do not reset the three-run count. Runner/supervisor untouched; no
foreign processes stopped, commit or push.

## Dev 094 — Task A blocked by non-reproduction — 2026-10-06, 01:42

Current plan/transfer paths and required sections read. Three all-on baselines already
meet the plan's stop condition; no new scenario supplied. No repeat baseline, negative-only
bisect or speculative default change. Runner availability job **oc20261006-014211-gitstatus**
exited **0**. No UFC launcher/emulator or competing build/test observed; caller KYTY_UFC_*
empty. Existing Dev 094/branch dev/defaults/uncommitted engine work preserved. Files edited:
HANDOFF.md and LOOP-LOG.md only. No build, focused/full tests or fight; latest supplied
**53/53** historical. No new session/first_fight_s, fight_frame_rate/frames, median/min FPS,
RSS/VRAM, wait-site ms/s or visuals. No candidate retained/rejected or improvement claimed;
manual transient-artifact check missing, **30 FPS unmet**. Task B/C deferred. Concrete
blocker/next hypothesis: missing actual blackout timing, first/later match, mode/fighters
and ideally live-HUD black screenshot; longer/repeated-match trigger unproven. Resume
prescribed bisect after positive reproduction, two clean runs per clean verdict. Do not
restart the three-run baseline sequence. Runner/supervisor unchanged; no commit/push.

## Dev 094 — existing reproduction stop condition honored — 2026-10-06, 01:41

Read current plan, transfer paths and required handoff/history/review sections.
Three all-on baselines already failed to reproduce blackout; no new scenario supplied.
Runner check **oc20261006-014056-gitstatus**, exit **0**; no UFC launcher/emulator or
competing build/test observed, caller KYTY_UFC_* empty. Existing dev branch work and
installed Dev 094/defaults preserved. Files changed: HANDOFF.md/LOOP-LOG.md only.
No build, focused/full tests or fight; latest supplied **53/53** historical. No new
session/scenario, fight_frame_rate/fight_frames, median/min, RSS/VRAM, wait-site ms/s
or visuals; prior three-run evidence remains reference, not acceptance. No candidate
retained/rejected or speedup; manual transient check missing, **30 FPS unmet**.
Task B/C deferred. Concrete blocker/next hypothesis: actual blackout timing,
first/later match, mode/fighters and a live-HUD black screenshot are missing;
longer/repeated-match trigger unproven. Resume prescribed bisect only after positive
reproduction, two clean runs per clean verdict. No redundant baselines, speculative
engine changes, runner/supervisor changes, commits or pushes.

## Dev 094 — Task A blocker unchanged; no redundant baseline — 2026-10-06, 01:40

Current plan, transfer paths and required latest/historical sections read. Three all-on
attempts already reached the non-reproduction stop condition; no new blackout scenario
in this request. No negative-only bisect, speculative default change or Task B/C work.
No launcher/emulator or competing build/test observed; caller KYTY_UFC_* overrides empty.
Runner `py -3.13 RunnerJob.py gitstatus`, **oc20261006-013952-gitstatus**, **exit 0**.
Installed **Dev 094**/branch dev and existing uncommitted work preserved. Files edited:
**HANDOFF.md, LOOP-LOG.md** only. No build/install, focused/full tests or fight queued;
latest supplied **53/53** remains historical evidence. No new session/first_fight_s,
fight_frame_rate/fight_frames, median/min FPS, RSS/VRAM, wait-site ms/s or visual data.
No candidate retained/rejected or improvement claimed; manual transient-artifact check
missing, **30 FPS remains unmet**. Blocker/next hypothesis: missing blackout timing,
first/later match and mode/fighters, ideally a live-HUD black screenshot; longer/repeated
matches remain an unproven trigger. Resume prescribed halving after positive reproduction,
two clean runs per clean verdict. No runner/supervisor changes, commit or push.

## Dev 094 — Task A blocked; no new reproduction input — 2026-10-06, 01:38

Read NEXT-STEPS-DEV094 first, transfer settings and required handoff/review sections.
The three all-on baseline attempts below already reached the plan's stop condition;
this transfer request contains no new blackout timing/scenario. No repeat baseline,
negative-only bisect or speculative default change was justified. Task B/C deferred.

Runner availability: `py -3.13 RunnerJob.py gitstatus`, **oc20261006-013838-gitstatus**,
**exit 0**. No UFC launcher/emulator or competing build/test observed; caller KYTY_UFC_*
overrides empty. No build/install, CTest or fight queued. No processes stopped,
runner/supervisor modified, commit or push. **Dev 094**, branch dev and all existing
engine work/defaults preserved. Files edited: **HANDOFF.md, LOOP-LOG.md** only.

Focused/full tests not rerun; latest supplied **53/53** remains historical evidence.
No new session/scenario, fight_frame_rate/fight_frames, median/min FPS, RSS/VRAM,
wait-site ms/s or visuals; reuse the three sessions below without a cure claim.
No candidate accepted/rejected or FPS improvement established. Manual transient/
ground-and-pound check missing; **30 FPS remains unmet**. Concrete blocker/next
hypothesis: actual blackout timing, first/later match, mode/fighters and ideally a
live-HUD black screenshot are missing; longer/repeated-match trigger is unproven.
Resume feature-halving only after positive reproduction, two clean runs per verdict.

## Dev 094 — Task A reproduction blocker persists — 2026-10-06, 01:37

Read transfer paths, ufc-loop and required current/historical sections. No new reproduction
input; honor the existing three-all-on-baseline stop condition, not a fresh baseline/bisect.
Runner `gitstatus` job **oc20261006-013729-gitstatus** exited **0**. No UFC launcher/emulator
or competing build/test observed; caller KYTY_UFC_* overrides empty. No build/install,
CTest or fight queued, foreign processes stopped, runner/supervisor changed or commit made.
**Dev 094**/branch dev/all pre-existing engine work/defaults unchanged. Files changed:
**HANDOFF.md, LOOP-LOG.md** only. Focused/full tests not rerun; supplied **53/53** remains
latest. No new session, first_fight_s, frame rate/frames, median/min, RSS/VRAM, wait-site
ms/s or visual observations; use the three recorded sessions below, not new acceptance.
No candidate accepted/rejected, feature cleared or performance gain claimed. Manual
transient/ground-and-pound check missing; **30 FPS remains unmet**. Next actionable input:
actual blackout timing, first/later match and mode/fighters, ideally a live-HUD black shot.
Longer/repeated-match trigger is only a hypothesis. Task B/C deferred pending Task A.

## Dev 094 — transfer re-entry; existing Task A stop condition honored — 2026-10-06

Current plan and transfer paths read; branch dev and pre-existing uncommitted work preserved.
The preceding cycle exhausted NEXT-STEPS-DEV094's three all-on baseline attempts without
reproducing black 3D plus live HUD. This invocation received no new reproduction timing or
scenario, so no further identical baseline or negative-only bisect was run. Task B/C remain
deferred, not silently substituted for the unresolved stability task.

Availability check: `py -3.13 RunnerJob.py gitstatus`, job `oc20261006-013556-gitstatus`,
exit **0**. This proves the Claude runner is responsive despite the initial process query
not showing its command line. No UFC launcher/emulator or competing build/test was observed;
caller KYTY_UFC_* overrides were empty. No runner restart, foreign-process termination,
build/install, CTest or fight occurred. Files edited: **HANDOFF.md, LOOP-LOG.md** only.

Build **Dev 094** unchanged; focused/full tests not rerun (latest supplied **53/53**).
Session/scenario/frame rate/frames/median/min/RSS/VRAM/wait-site ms/s/visual observations:
**no new measurements**, preceding three-session evidence remains the reference. No feature
cleared, no engine candidate accepted/rejected and no FPS gain claimed. Manual transient
artifact/ground-and-pound check remains missing; **30 FPS remains unmet**.

Concrete blocker/next hypothesis: the user's blackout may require a longer fight or later
match in the same emulator process, or different fighters/mode/input. Need approximate
blackout timing, first versus later match, mode/fighters, ideally a live-HUD black screenshot
to validate detector sensitivity. Do not reset the three-baseline count. Once reproduced,
resume `d094-bis-abcdefgh` and the prescribed halving with two clean runs per clean verdict;
only an isolated culprit justifies a default-OFF engine change and runner acceptance cycle.

## Dev 094 — Task A three-baseline limit reached / black-scene reproduction blocked — 2026-10-06

Milestone: reproduce the user's intermittent black 3D scene with HUD still updating before
isolating a default-on feature. Reference: `d094-all-20261006-011057` (one screened-clean run,
first fight 102.00 s, opening 14.02 FPS). No work-elimination/FPS claim applies to this diagnostic
milestone. Correctness/acceptance: unchanged 2560x1440/Mailbox/ShaderValidation/red-zone,
a-p all ON, confirmed HUD plus two consecutive black fight screenshots for failure; two clean
runs per later bisect verdict. Follow NEXT-STEPS-DEV094's bounded three-baseline rule.

No engine candidate was implemented: feature attribution requires a positive reproduction.
Existing dirty working tree (branch dev) was inspected and preserved. Local transfer paths
were read. No user launcher/emulator, competing build/test or game automation before either
job, no caller KYTY_UFC_* overrides, queue empty; RunnerJob checked heartbeat before queueing.
Both fights were serial and exclusively runner-owned; no other processes killed or supervisors
changed. Commands (twice): `py -3.13 RunnerJob.py fight --label d094-all --monitor-seconds 300`.

| Job / session under `_Build/autolab/` | Duration / first fight / observed fighting (s) | Fight frames / frame-derived FPS | Opening counter window | Fight median/min | Fight max RSS / device VRAM (MiB) |
|---|---|---|---|---|---|
| `oc20261006-012050-fight` / `d094-all-20261006-012113` | 300.84 / 101.89 / 198.95 | 3741 / 18.93 | 103.25->146.38 s, frames 2479->3168: 689/43.13 = 15.97 FPS | 19/7 | 11111.8 / 13321 |
| `oc20261006-012650-fight` / `d094-all-20261006-012712` | 300.97 / 101.92 / 199.05 | 4095 / 20.73 | 103.45->146.50 s, frames 2492->3150: 658/43.05 = 15.28 FPS | 22/12 | 11017.7 / 12503 |

Jobs exited 0, installed **UFC Dev 094**, timeout-ended as requested. ShaderValidation and
red-zone ON, no Tracy, all a-p feature switches printed ON. No GPU loss or driver events,
matched FATAL/ERROR or shader-validation failures. Informational validation-cache line is
the only triage SPIR-V hit (776 known-valid shaders); new shaders remain validated.

Read-only `dev094-black-scene-scan.py` found **zero black candidates** in 37 post-detection
HUD screenshots in each run (54 total shots each); max dark fraction 0.548880 / 0.526875.
Loading shots excluded; no two-consecutive BLACK result. Positive sensitivity is still
unvalidated because no real black-scene screenshot has been supplied/captured.
Visual review: run 2 shots 19/25/40/52, run 3 shots 19/40/53, standing and ground, visible
fighters/mat/cage/HUD. Makhachev/Della Maddalena, T-Mobile arena, same initial standing camera.
Old skin/cage speckling remains. Run 2 is on the ground by 144.16 s; run 3 by ~182.01 s:
whole-fight and opening workload differences preclude attributing their FPS differences to
an improvement (code is identical). **Manual transient-artifact/ground-and-pound check missing.**

Last two 30 s cumulative wait reports (#8->#9), ms/s (run 2 / run 3):
gpu_untagged **18.35/3.06**, cpu_write_flush **2.25/0.47**, cpu_read_fault **0.10/0**,
srt_read_fault **0/0**, cp_read_fault **37.97/49.32**, other_threads **54.14/61.35**.
color_metadata/indirect_read_fault/args_copy absent (zero counted waits; formatter omits
zero-count sites). Final GPU indirect outcomes **16909/17237**, no_fetch matches those counts;
gpu metadata slices **2263/2339**, BDA fault pages **365/333**, buffer gc_retired **0/0**.
Dirty verify and SRT shadow are not enabled, so their zero mismatch counters prove nothing.
No isolated mechanism speedup or lower-resource causal claim is made.

Decision: **three all-on baselines total screened-clean; Task A blocked by non-reproduction**,
exactly the stop condition in NEXT-STEPS-DEV094 section 2 step 1. Do not label a-p clean,
change defaults speculatively, or reset this baseline counter and spend another cycle repeating
the same experiment. No bis-letters run was justified; no feature isolated. Retain existing
Dev 094 unchanged, no new candidate accepted/rejected. Focused/full tests not rerun because
engine/test code unchanged; supplied Dev 094 **53/53** remains the latest full-suite evidence.
Files edited this cycle: **HANDOFF.md, LOOP-LOG.md** only; no build/install/commit/push.
**30-FPS goal remains unmet; Task B performance and Task C video work remain deferred.**

Next concrete blocker/hypothesis: reproduce the user's actual scenario rather than assuming
the 300 s first-match schedule reaches it. Need blackout timing into fighting, first versus
later match in one emulator process, mode/fighters and ideally a black screenshot. A longer
or repeated-match workload is a hypothesis, not established cause. Once a baseline BLACK
exists, resume `d094-bis-abcdefgh` and halve the guilty set, requiring two screened-clean runs
per clean verdict. Isolated feature goes default OFF (=1 opt-in), then runner build/full
53+ suite/two clean all-on acceptance fights. No routine approval is required.

## Dev 094 — first runner-owned OpenCode reproduction / overnight handoff — 2026-10-06

The user supplied the Dev 094 next-steps guide and requested that the loop continue overnight.
The new runner ownership takes precedence: every build, CTest and fight is queued through
`RunnerJob.py`; the Claude worker remains the sole executor and is never stopped by an agent.
Existing engine changes were preserved. Python 3.13 and the local game/settings are available.

`py -3.13 Run-AutonomousLoop.py --preflight-only` returned clear. Real helper/worker integration
completed `RunnerJob.py gitstatus` (job `oc20261006-010959-gitstatus`, exit 0), then:

```powershell
py -3.13 RunnerJob.py fight --label d094-all --monitor-seconds 300
```

Job `oc20261006-011034-fight` exited 0. Session
`_Build/autolab/d094-all-20261006-011057`: installed Dev 094, 2560x1440/Mailbox,
ShaderValidation/red-zone ON, Tracy OFF, all a-p features printed ON. Duration 300.78 s;
HUD first detected 102.00 s; fighting observed 198.78 s; **2601 frames / 13.20 FPS**;
first-45-s counter window 2426->3032 at 103.74->146.96 s = **606/43.22 = 14.02 FPS**;
sampled fight median/min 14/2; RSS 15303.3 MiB, device VRAM 15062 MiB. No GPU loss or
driver events; the triage's one SPIR-V hit is the known-valid-cache informational line.
Several 2-4 FPS periods occurred with high GPU utilization; inspected low-FPS shot_0039
still shows visible fighters/arena, so those periods are not black-scene proof.

Saved the supplied darkness calculation as a read-only analysis artifact:
`_Build/agent-reports/dev094-black-scene-scan.py`. Screenshot timestamps are paired with
the nonempty-brightness metric rows; pre-fight images are excluded and existing FightWatch
HUD detection gates candidates. Across 53 screenshots / 36 post-detection HUD screenshots:
0 black candidates, max post-detection dark fraction 0.610052 (<0.85). The first loading
screens score 1.0 but have no HUD and precede detection. Visual checks: shot_0019 (standing),
shot_0039 (3-FPS standing), shot_0052 (ground), all with visible scene data. No real black-scene
positive exists to validate sensitivity yet; do not treat the negative scan as a visual cure.

Retain/reject: no engine candidate was introduced; Dev 094 remains unchanged. One baseline
run is screened-clean only. Next cycle: `d094-all` again, up to three baseline runs total,
then the guide's bisect if black reproduces. Two clean runs are required per clean verdict;
one confirmed black run suffices for a failure. If all three baselines stay clean, record
the failed reproduction and ask for timing/settings when the user returns, preserving the
unattended supervisor and runner. Intro-video performance requires a separate intro capture;
the game-binary/movie-pipeline findings alone are not measured bottleneck attribution.

No build/test suite rerun was needed for an unchanged engine. The supplied guide reports
Dev 094's 53/53; this cycle executed the helper/worker path and gameplay, not new C++ tests.
Detailed continuation evidence: `_Build/agent-reports/dev094-overnight-start-20261006.md`.

## Claude session 4 — Dev 081 accepted (cycle 28); explosions seen with dirty log OFF; Dev 082 diagnostics — 2026-10-05

**Dev 081 = cycle-28 page-footprint correction, accepted.** Review: `RecordCpuDirty` now logs the
same whole-tracker-page footprint that `ChangeState` dirties/unlocks; uploads are already
page-granular (`ForEachModifiedRange` hands whole pages to the upload copy) and cache buffers are
16 KiB aligned, so the fix is consistent. Note: UFC's BDA ranges are `RenderContext::m_mapped_ranges`
(page-aligned MapMemory ranges), so the sub-page case the fixture reproduces probably never fires in
UFC; this fix alone is unlikely to explain the mesh explosions. Full CTest **53/53** (68 s) on the
cycle-28 source; standard build/install **UFC Dev 081**. Fights (420 s, default switches, dirty log OFF):
- `dev081-pagefoot-a`: failed to reserve guest address space at startup (launch right after the
  build; not a code fault).
- `dev081-pagefoot-b-20261005-122941`: HUD 103.39 s, **5014 frames / 15.86 FPS**, first 45 s **12.75**,
  RSS 10786 MiB, VRAM 12324 MiB, no device loss. Within Dev 079/080 range.
- `dev081-pagefoot-c-20261005-123649`: HUD 103.27 s, **7.49 FPS**, first 45 s 11.84. From ~148 s to the
  end: repeated 1-3 FPS samples with GPU 100% (62 samples). `shot_0050.png` shows a **full fighter
  mesh explosion during ground-and-pound with the BDA dirty log OFF** (`shot_0072.png` standing, clean).
  `color_metadata` wait time 188 s vs 53 s in run b (GPU drains wait behind the huge fills).

**Finding: explosions are not only the dirty log.** They occur on the default configuration too
(intermittent; user's Dev 074 manual test did not hit one). Proxy over all autolab runs
(fight samples with fps<=3 and GPU util>=95%): Dev 070 (= Dev 044 source) run b 13 samples then
**device lost**; Dev 076 redzone-a 2 then device lost; Dev 077 pin-a 3 then device lost; Dev 078 a 1;
Dev 081 c 62; every other run 0. Hypothesis: the explosions (giant triangles, GPU 100%) are what
drives the intermittent GPU TDR (device lost, timeout). Fixing the explosion source may fix both.

**Explosions are a baseline bug (screenshot scan, 2026-10-05 13:10).** `skinscan2.py` (skin-colour
fraction per screenshot; an explosion frame scores 0.4-0.65, normal close-ups <= 0.28) over every
autolab run, then visual confirmation: full-screen fighter explosions in
`dev070-044src-b/shot_0049.png` (**the Dev 044 source**, the run that then lost the device),
`dev078-branch-tail-a/shot_0064.png`, `dev081-pagefoot-c/shot_0050.png` (dirty log OFF) and
`dev082-dlverify-a/shot_0045.png` (dirty log ON with every missed upload repaired). None in the
10-03 loop runs. So the user's bisect (Dev 044 clean) and "dirty log = explosions" were artifacts of
an intermittent bug (roughly 1 run in 3-4 recently). The low-FPS+GPU-100% metric is NOT a reliable
explosion proxy (dev082-dlverify-a had 41 such samples on mostly normal frames).
Dirty-log verify on Dev 082 (`dev082-dlverify-a`): fast=431k walk=6k **verify_missed=4**
(whole-buffer ranges, e.g. 0x1285c80000+0x37c000) raced=31.7k. Dev 082 logged the CPU-dirty range
after releasing the region lock, so a fault in that window is counted as missed; Dev 083 logs under
the region lock (and before publishing a new region) so a miss is definitive.
SRT memo shadow on Dev 082 (`dev082-srtshadow-a`): 0 mismatches in 341k reuses, but only 2% hit:
inputs changed in 17.0M of 17.3M materializations (user data differs almost every call). Dev 083
keys the memo on the user-data words the evaluation actually read (walker records a mask) and
refreshes `ResourceSnapshot::user_data` (a copy of all words) on reuse.
Guest `sceKernelMprotect` sets host protection directly (`ProtectMappedUnlocked`), which would drop
the tracker's write/read watches on tracked pages: later CPU writes no longer fault and GPU copies
stay stale (a persistent-corruption candidate). Dev 083 logs `Guest mprotect over GPU-mapped
memory #n` and, with `KYTY_UFC_MPROTECT_INVALIDATE=1` (label `mpinv`), treats GPU-mapped parts as
CPU-written first (`RenderContext::InvalidateMappedOverlap`).

**Likely explosion mechanism: BDA reads of garbage-collected buffers return zero (Dev 084).**
BDA shader loads translate guest pages through the BDA page table; an unbacked page records a
fault bit and **returns 0** for that draw (`get_bda_pointer`/`LoadBdaDword`); the fault manager
creates the owner afterwards. BDA reads never touch the buffer LRU, so the garbage collector
(active whenever device usage exceeds the trigger, i.e. every fight at ~12.3-12.9 GB) retires
buffers that BDA shaders still read every frame. Zeroed skinning/bone data collapses vertices to
one point = the spike "explosion"; giant triangles also pin the GPU at 100%. New counter
`bda fault pages=` (Dev 084): default runs **10,965-17,511 zero-read fault pages per fight**,
growing with GC retirements. `KYTY_UFC_BDA_KEEP=1` (label `bdakeep`): buffers created for a BDA
fault (and, Dev 085, any buffer over a faulted page, `m_bda_hot_ranges`) are never retired by GC.
`dev084-bdakeep-a`: **2,555 fault pages** (most are first touches), VRAM 12639 MiB (lower than
default 12892), gc_retired 24k vs 41k, first 45 s 13.1 / whole 16.01, no explosion frame.
Needs more runs (the bug is intermittent). Test: `buffer_cache_dirty_gc` now checks a BDA-faulted
owner survives normal GC while an equally old clean owner is retired.
SRT memo with the user-data mask (`dev084-srtshadow-a`): still only 2% reuse (335k of 17.1M),
0 mismatches. The SRT inputs genuinely change per draw in UFC: **rejected as an FPS lever**
(left opt-in, off).
Tracy on Dev 084 (`_Build/agent-reports/dev084-tracy-zones.csv`, inclusive, per frame at 12.5
frames/s under Tracy): DrawIndex 48 ms (2293 draws/frame x 21 us), PrepareDrawRenderState 32.5
(RefreshShaders 18.3, PrepareGraphicsBindings 11.5), ExecutePreparedDraw 20.7
(ResolveRenderColorTarget 12.0, 2/draw), MasterSemaphore::Wait 42 (BufferCache::ReadMemory 13.7,
ColorMetadataReadback 9.4), SrtRefreshFlatBuffer 14.0, PrepareBda/SynchronizeBuffersInRanges 7.5.

**Dev 084 batch results (7-min fights; first-45-s FPS / whole-fight FPS / BDA zero-read pages):**
default a/b/c 12.56/17.48/10965, 12.4/13.46/25142, 12.86/14.19/23990; bdakeep a/b/c 13.1/16.01/2555,
17.91/16.52/1694, 14.2/14.76/3903; dlon-a 13.51/14.88/27467; bdakeep+dlon a 14.26/16.93/2769;
**bdakeep+dlon b 6.42/5.18 with explosions** (shots 46 and 54; zero-read count flat at ~2059 while
the explosion persisted, so the BDA zero reads are NOT the whole explosion story); bdakeep+dcc a/b
12.6/16.67, 11.49/12.99 (DCC removes color_metadata waits, but gpu_read_fault/gpu_untagged waits
grow by the same amount: no FPS gain); bdakeep+dlon+dcc+fill a: address-reservation failure;
**b: boot device loss, WriteInvalid at page 0, gpu_loss_class=timeout** (same signature as Dev
053/060/069 boot losses): keep FILL_CONVERT and GPU_DCC off. VRAM with bdakeep 12.5-13.2 GB (no
growth). BDA keep is retained as a correctness fix (it removes ~90% of zero-read frames) and is
**default ON from Dev 085** (`KYTY_UFC_BDA_KEEP=0` disables; label `nokeep`).
LLVM cross-check of the decoder (`_Build/agent-reports/decoder-llvm-xcheck*`): 496 unique UFC shaders,
171,574 instructions, gfx1013 `llvm-mc` vs Kyty opcode tables: no real mismatches (only renamed
FMA/ADDC/WAITCNT aliases that Kyty already translates with the right semantics; s_call/s_setreg only
in data after s_endpgm).
Self-time Tracy (cloud-built tracy-csvexport 0.14.1 -e on `dev084-tracy`, per frame): MasterSemaphore::
Wait 42 ms (all threads), SynchronizeBuffersInRanges 7.5, SrtControlFlowWalkEvaluate 7.0,
SrtRefreshFlatBuffer 6.8, MaterializeResources ~5.1, DeleteImage 3.3 + CreateImage 1.9 (texture GC
churn ~30 images/frame), PrepareBindings 3.1, Submit 2.6, RebindBuffers 2.5, CommitBindings 2.1.
Dev 085 (53/53): BDA keep default ON + hot-range retention; IR `Value`/`Inst` hot accessors inlined
(SRT walker and recompiler); `RenderContext::InvalidateMemory` now invalidates the GPU-mapped part of
a host write that straddles mapped memory (it used to skip the whole range; logged as
`Partial GPU-memory invalidation`); new wait site `srt_read_fault` (read faults inside SRT
materialization); `KYTY_UFC_TEX_GC_FRAMES=1` (label `texgc`): texture GC age >= 2 frames below the
critical threshold (pressured mode used an 80-tick age, ~half a frame); `images gc_freed=` counter.

**Dev 085/086 (5-min fights from here; 53/53 both).** Dev 085 default: first 45 s 13.52 / whole 15.51,
zero-read pages 427 (hot-range keep), gc_retired 12.7k (was 40k+); texture GC freed only 12 images per
run, so image churn (~30 deletes/frame) is NOT GC-driven and `texgc` was dropped. Dev 085 dlon-a
13.92/15.87. Dev 086 adds read-fault attribution (`ScopedFaultSite`, sites `indirect_read_fault`,
`cp_read_fault`, `srt_read_fault`): GPU-thread read faults are almost all **indirect draw arguments**
(`indirect_read_fault=9272/18421 ms` per run; srt 320/117 ms, cp 355/345 ms). DispatchIndirect is already
GPU-sourced; the CPU path is DrawIndirect with `KYTY_UFC_GPU_INDIRECT` off (it was turned off only
because of the debunked explosion blame). Dev 086: default-a 13.84/16.26, dlon-a 13.74/16.03,
**gpuind-a 14.15/17.73** (label `gpuind`). Remaining big wait: `color_metadata` (~37 s/run): CPU
readback of DCC/CMASK fast-clear metadata written by uniform-fill compute (16k) and shaders (11k).
User paused all runs at 16:05 local; held jobs in `_Build/claude-runner/hold/`
(153 tracy, 154 gpuind-b, 155 dlon-gpuind-b, 156 default-b, 157 dlon-b).
Next: confirm gpuind over 2-3 more runs, then make GPU indirect the default; then attack the
color-metadata readbacks (track known uniform-fill values instead of draining).

**Dev 082 (diagnostics only, defaults unchanged):**
- `KYTY_UFC_BDA_DIRTY_VERIFY=1` (with `KYTY_UFC_BDA_DIRTY_LOG=1`): after every dirty-log fast path,
  walk the BDA ranges; a CPU-dirty page with no pending log entry is a **missed upload**: counted,
  logged (`BDA dirty log MISSED: ...`, first 32) and uploaded. Racing faults (already in the next log)
  counted separately. Counters on the 30 s line: `bda sync fast= walk= verify_missed= verify_raced=`.
  `MemoryTracker::CpuDirtyLogIntersects`. Test: `BufferCacheBdaDirtyPage` now drops a log entry and
  checks the fast path stays stale, verify counts exactly one miss, repairs it, and stays idle after.
- `KYTY_UFC_SRT_MEMO=shadow|1`: per-program memo of `MaterializeResources` (the SRT walk +
  descriptor materialization, ~21 ms/frame in the 10-03 Tracy: SrtControlFlowWalkEvaluate 8.3,
  SrtRefreshFlatBuffer 7.7, MaterializeResources 5.7). Evaluation is a pure function of
  (plan, user data, shader base, guest-memory read results), so the memo records every base read
  (`SrtReadLog`) and reuses the previous result only when inputs match and an in-order replay of
  every read returns the same success and words. `shadow` recomputes anyway and compares snapshot,
  specialization and active sources (`SRT memo shadow MISMATCH` + `shadow_mismatch=` counter).
  Counters: `srt memo reused= inputs= reads= shadow_mismatch= replayed_reads=`. Test:
  `TestSrtReadLogReplayDetectsChanges` (log does not change results, replay accepts unchanged and
  rejects a changed guest word, rematerialization refreshes the log).
- AutoFight label switches (process env): `dlverify` (dirty log + verify), `dlon` (dirty log),
  `srtshadow`, `srtmemo`, `gcoff` (KYTY_UFC_GC_FRAMES=0).
- `_Build/Test-DirtyLog.bat`: manual launcher with `KYTY_UFC_BDA_DIRTY_LOG=1`.
Full CTest **53/53** (52 s) on the Dev 082 source before install.

## Current cycle — P2 tracker-page dirty-log coverage (2026-10-05, milestone)

Mechanism: CPU dirtiness and write protection are tracked in 4 KiB pages, but
`RecordCpuDirty` currently records only the caller's bytes (a write fault supplies
one byte). The opt-in BDA walk intersects those bytes with its requested ranges.
A dirty-page transition outside a sub-page requested range can unlock subsequent
writes inside that range without another fault; the dirty-log walk then misses
the upload that the full walk would perform. Prove this with a real native-buffer
readback without an intervening ObtainBuffer refresh, plus tracker coverage tests.
Bounded correction: log the full tracker-page footprint of each CPU-dirty transition
before intersecting it with requested ranges. Preserve overflow fallback, ownership,
upload synchronization, Dev 078-080 ISA fixes and all safe defaults. Dirty-log remains
OFF; this case is not yet established as the UFC mesh-explosion cause, and common
page-aligned UFC mappings may not exercise it. P1 remains blocked by zero hashes in
saved timeout breadcrumbs, as documented in Dev 080.

Reference: Dev 080 B fast-start HUD 103.23 s, opening 573/43.66 = 13.12 FPS,
4879 fight frames / 15.43 FPS, RSS 10792.7 / VRAM 12295 MiB. Expected eliminated
work is a skipped required upload, not a demonstrated default-path FPS gain.
Acceptance: pre-fix failing tracker/native-buffer fixture, focused page/sub-page,
boundary/overflow/control coverage, full 53/53+ CTest, standard numbered install,
two serial 420 s default-switch 2560x1440/Mailbox/ShaderValidation fights with
>=120 s fighting, counter-window/wait/resource comparison. No gameplay dirty-log
enablement or manual-visual-proof claim. Initial ownership clear, heartbeat >8 h old,
branch dev, actual KYTY_UFC_* overrides empty; existing working-tree work preserved.

## Dev 080 — empty-EXEC readfirstlane ISA correction — 2026-10-05 (retained)

One engine change in `src/graphics/shader/recompiler/backend/spirv/spirvEmitterFlow.cpp`:
`EmitReadFirstLane` selects guest lane 0 when both EXEC ballot words are zero,
rather than shuffling from an undefined empty-ballot FindLSB result. Nonempty
EXEC continues to select its least-significant active lane across both wave64
halves. No change to `FirstLane`'s other callers, readlane/writelane, DPP16,
mask branches, decoder, guest work, loop bounds, synchronization or validation.
This is an ISA correctness fix, not an identified UFC timeout fix or FPS lever.

### Verification

- Pre-fix real Vulkan regression: `ReadFirstLaneExecWave32Mask0` produced
  **0x12341f1f (lane 31)** at all 32 destinations, instead of **0x12340000 (lane 0)**.
- Post-fix `shader_recompiler_compute_tests.exe --readfirstlane-exec-only`:
  **8/8 GPU execution fixtures pass**, also included in the full compute suite.
  New tests in `tests/ShaderRecompilerComputeTests.cpp` use lane-distinct GPU loads,
  then inspect the scalar result in every lane after restoring EXEC. Wave32/wave64
  coverage: empty EXEC, low bits 4/5, high bits 40/41 only, and low lane 7 versus
  high lane 33. Wave32 ignores EXEC_HI. Updated AnyPS5 `ReadFirstLane.cpp` is the
  semantic reference. Partial workgroups and graphics helper-lane behavior are
  not separately covered by this new matrix.
- Incremental focused target build passed; `cmake --build _Build/windows --target
  kyty_tests` passed; `ctest --test-dir _Build/windows --output-on-failure`:
  **53/53 passed in 54.70 s**. Detailed log:
  `_Build/windows/Testing/Temporary/LastTest.log`; durable verification summary:
  `_Build/agent-reports/dev080-readfirst-verification.md`.
- Standard `powershell -ExecutionPolicy Bypass -File .\Build-Windows.ps1 -SkipTests`
  build/install succeeded as **UFC Dev 080**. Compiler/dependency warnings are
  pre-existing. No failed build, counter hand-back or extra numbered installation.
- Repeated ownership/process/heartbeat checks before each build/test/install-wrapper/
  launch; standard installer rechecked game closure. No foreign process stopped,
  no CTest/game overlap. Actual KYTY_UFC_* overrides empty; AutoFight --redzone
  active; GPU/Present performance-core pinning confirmed in both logs. Dirty-log,
  GPU indirect, args-copy, DCC and fill-convert remain OFF. Prior working-tree
  changes preserved; no commit/push or supervisor changes.

### Same-settings gameplay

Both runs used 2560x1440, Mailbox, ShaderValidation, GPU breadcrumbs, no Tracy,
default MenuDrive schedule. Makhachev/Della Maddalena, T-Mobile UFC octagon, initial
standing camera match the reference; later ground workload differs.

| Session under `_Build/autolab/` | Duration / HUD start s | Fighting s | Fight frames / FPS | Opening counter-window FPS | Median/min | RSS MiB | Device VRAM MiB |
|---|---|---:|---|---:|---|---:|---:|
| `dev080-readfirst-a-20261005-110252` | 420.67 / **128.50** | 292.17 | 4157 / **14.31** | **12.14** | 14/6 | 10700.2 | 12256 |
| `dev080-readfirst-b-20261005-111014` | 420.75 / **103.23** | 317.52 | 4879 / **15.43** | **13.12** | 14/8 | 10792.7 | 12295 |

Opening window = first/last counter samples inside `[first_fight_s, +45]`:
A 2353->2870, 130.16->172.73 s, **517/42.57**; B 2456->3029,
104.48->148.14 s, **573/43.66**. B is fast-start mode; inspected opening
shots 20/24 remain standing, as in Dev 079 A. B's 13.12 FPS is within the
Dev 079 12.90/13.17 range; whole-fight 15.43 is within 14.97/18.23.
No causal speedup or regression established. A is **not a fast-mode comparable**:
pipeline-build total **33707 ms**, validation **1079 ms**, versus B pipelines
**742 ms**, validation **65 ms** (new shader binaries trigger new validation and
driver compilation). The stall totals accompany the delayed start; they do not
prove its sole cause. Window foreground state is not recorded. Cache validation
semantics are unchanged; no checks removed. A final translation/compile/module
2425/821/24 ms; B 2495/849/24 ms.

Consecutive fully-fighting wait reports, 30 s apart: A #5->#6, B #4->#5.
These are different fighting phases and site costs, not exclusive frame times.

| Site, ms/s | A | B |
|---|---:|---:|
| gpu_untagged | 11.14 | 10.51 |
| cpu_write_flush | 2.73 | 3.01 |
| color_metadata | 115.97 | 117.81 |
| cpu_read_fault | 0 | 0 |
| gpu_read_fault | 51.58 | 49.26 |
| other_threads | 353.79 | 358.80 |

No drain reduction expected or demonstrated. RSS/VRAM are roughly within prior
safe-baseline samples, but fight coverage differs; no resource-use benefit claimed.
Both summaries: no device loss or driver events. Both stderr logs contain only
the previously seen timeout-teardown `SymInitialize FAILED 0xc0000004`; no in-fight
guest fault or shader-validation failure. Triage also matches informational
validation-cache lines; do not report these as entirely error-free sessions.

Screenshots inspected: A 25/60 (standing), B 20/24 (standing) and 60 (ground),
compared with Dev 079 A 20. Fighters/referee/cage/HUD render; old skin speckling
and trails/afterglow remain. No obvious new static corruption in those samples,
but automated screenshots cannot certify absence of transient mesh explosions.
**Manual ground-and-pound/transient-artifact check still missing.**

Decision: **retain the demonstrated ISA fix** with full tests and two qualifying
fights, not a performance or intermittent-TDR cure. Actual UFC empty-EXEC
readfirstlane incidence is unquantified. **30 FPS remains unmet; no goal file.**
Next hypothesis: P1 timeout attribution is concretely blocked by zero graphics
shader hashes in saved breadcrumbs. Resolve native-program/dump identity for the
recorded guest PS/ES addresses before selecting another hang-specific ISA patch;
avoid a series of unrelated semantic fixes claimed as TDR progress. Independently,
P2 still needs a failing producer->BDA stale-upload fixture (tracker bypass/image
publication/epoch), with dirty-log OFF until manual visual proof. No P2 code
change was made this cycle.

### Milestone recorded before implementation

Mechanism: `EmitReadFirstLane` passes an empty EXEC ballot to `FirstLane` and then
shuffles from the undefined result (wave32) or lane 31 (emulated wave64). RDNA2
`v_readfirstlane_b32` reads lane 0 when EXEC is zero, as exercised by the updated
AnyPS5 `ReadFirstLane.cpp`. A scalar value used in control flow must not depend on
an undefined host shuffle selector. Target only this fallback, preserving active
EXEC selection and wave64 halves; do not clamp loops or remove shader work.
Reference: Dev 079 fast-mode opening 12.90/13.17 FPS, whole fights 14.97/18.23;
no expected CPU-drain elimination or FPS claim. Acceptance: pre-fix failing Vulkan
fixture, empty/low/high EXEC cases with GPU-loaded values, full 53/53+ CTest,
standard numbered install, two serial 420 s same-settings fights, >=120 s fighting,
counter windows, waits, RSS/VRAM and screenshot comparisons. Manual transient-artifact
check remains missing. Preserve all previous work and every safe opt-in default.
P1 attribution blocker: Dev 077 timeout suspects seq=4630345/4630346 record guest
PS/ES addresses but `shader_hash=0`; addresses have no matching hash entries in
that session log and no address-named saved dump was found. This correction is an
independent ISA proof, not an identified cause of the UFC timeout. Initial pre-flight:
branch dev, no active game/build/test, heartbeat >8 h old, actual KYTY_UFC_* empty.

## Dev 079 — DPP16 inactive-source destination preservation — 2026-10-05 (retained)

One engine change: `src/graphics/shader/recompiler/backend/spirv/spirvEmitterFlow.cpp`
adds the missing source-EXEC ballot test to the destination write predicate for DPP16
BC=0/FI=0. Pass the IR EXEC value to the helper so the existing `ctx.Ballot` combines
wave64 halves correctly. No change to the operand fetch, DPP8, FI/BC overrides,
guest draws, loop bounds, resolution, synchronization or validation. Tests added only
in `tests/ShaderRecompilerComputeTests.cpp`; status docs and the normal build counter
also updated. All pre-existing uncommitted work preserved; no commit/push.

### Verification

- Before the emitter fix, real Vulkan execution failed:
  `VectorDpp16InactiveSourceWave32Ctrl75MovFi0Bc0` returned zero at active destination
  lanes 0/1 (and others) whose source EXEC bit was clear, rather than `0xdeaddead`.
- After the fix: **40/40 new GPU cases**, plus **6/6 existing DPP cases** pass with
  `shader_recompiler_compute_tests.exe --dpp-only`. New matrix covers wave32/wave64,
  move/add, quad permute, row shift bounds (FI=0), masked row mirror, asymmetric EXEC
  halves, source/destination inactivity and FI/BC overrides. Distinct DPP8 zero-fetch
  behavior is explicitly preserved. This matrix does not test FI=1 out-of-bounds row
  fetches; that is a separate contract, not a reason to broaden this patch.
- `cmake --build _Build/windows --target kyty_tests`: passed; full CTest **53/53**,
  49.83 s (40 cases also included in the full compute suite). Logs:
  `_Build/agent-reports/dev079-dpp-focused.log`, `dev079-ctest.log` (PowerShell UTF-16),
  and `_Build/windows/Testing/Temporary/LastTest.log` (readable detailed evidence).
- Standard `Build-Windows.ps1 -SkipTests` build/install succeeded as **UFC Dev 079**.
  An initial wrapper stopped on a CMake deprecation warning due to PowerShell's
  native-stderr/Stop handling, before any build/install. Its own counter increment
  was handed back; rerun succeeded without source changes. Initial test compilation
  also caught a fixture-name type error, corrected before the reproduced GPU failure.
- Ownership/process/heartbeat checks repeated before every invocation; no user or
  foreign process stopped. CTest and both fights were serial. Actual KYTY_UFC_*
  environment empty, red-zone flag active, GPU/Present P-core pinning confirmed in
  both console logs. Dirty log, GPU indirect, args-copy, DCC, fill-convert remain OFF.
  `git diff --check` passed.

### Same-settings gameplay

Both runs: 2560x1440, Mailbox, ShaderValidation, GPU breadcrumbs, Tracy off, default
MenuDrive schedule; Makhachev versus Della Maddalena in the same branded UFC octagon.
Sessions under `_Build/autolab/`:

| Session | Duration / HUD start | Fighting | Fight frames / frame rate | Opening counter window FPS | Fight median/min | RSS MiB | Device VRAM MiB |
|---|---|---:|---|---:|---|---:|---:|
| `dev079-dpp16-a-20261005-103011` | 420.24 / 103.25 s | 316.99 s | 4727 / **14.97** | **12.90** | 14/12 | 10827.3 | 12229 |
| `dev079-dpp16-b-20261005-103735` | 420.52 / 103.21 s | 317.31 s | 5762 / **18.23** | **13.17** | 19/7 | 11038.7 | 12365 |

Opening calculations take the first/last counter samples inside `[first_fight_s,
first_fight_s+45]`: A frames 2466->3030, 104.52->148.24 s, **564/43.72**;
B 2431->3006, 104.41->148.07 s, **575/43.66**. Dev 078 references 12.15/12.48 FPS,
whole fights 14.02/15.39; Dev 077 opening 13.01. Both current runs are fast-start mode,
but B is already on the ground by 140.28 s (`shot_0023`, frame 2894), whereas A is
standing at 140.45 s (`shot_0023`, frame 2926). Thus even the opening B window is not
fully scene-matched; later whole-fight rates differ strongly with ground workload.
**No FPS gain or resource reduction is established.** Current RSS/VRAM are modestly
above some Dev 078 samples (10792.5-10801.8 / 12212-12260 MiB), with different coverage.

Consecutive fight wait reports #4->#5, 30 s apart (ms/s; cumulative differences):

| Site | A | B |
|---|---:|---:|
| gpu_untagged | 9.92 | 9.00 |
| cpu_write_flush | 2.97 | 2.33 |
| color_metadata | 112.27 | 126.09 |
| cpu_read_fault | 0 | 0 |
| gpu_read_fault | 49.41 | 58.91 |
| other_threads | 342.90 | 383.79 |

Dev 078 B example was color_metadata 116.89 / gpu_read_fault 48.51 /
other_threads 360.71 ms/s. These are site costs, not additive exclusive frame times.
No drain was expected to disappear. Final shader timings: A translate 2510 / compile
843 / validate 12 / module 24 ms, 869 pipelines in 230 ms; validation cache preserved.
Both summaries report no GPU loss/driver events; no fatal/error matches, stderr empty.
Triage only matched the informational validation-cache line.

Standing and ground screenshots inspected (A 20/23/25/60; B 20/23/25/40), with Dev 078
B shot 20 as a standing reference. Fighters, referee, crowd, cage and HUD render;
skin speckling and old afterglow/trails persist. No new obvious static corruption in
these samples, but sparse automated screenshots **do not establish transient visual
correctness**. Manual ground-and-pound/transient-artifact check remains outstanding.

Decision: **retain the demonstrated ISA correctness fix**, not a TDR cure or performance
win. UFC coverage of the exact corrected DPP16 condition is not quantified; two clean
runs cannot resolve intermittent TDR causation. **30 FPS remains unmet**, no goal file.
Next hypothesis: resolve the historical timeout DrawIndex VS/PS hashes against saved
shader dumps and audit EXEC/lane-dependent loop exits. If the implicated shaders do not
use this DPP16 condition, do not keep attributing the hang to it; pursue a concrete
dirty-log missed-upload fixture (image->buffer synchronization/tracker bypass) separately,
still OFF until regression and manual visual proof.

### Milestone recorded before implementation

ISA mechanism: `EmitDppWriteCondition` currently checks destination EXEC, row/bank masks
and bounds, but not source EXEC for DPP16 BC=0/FI=0. It therefore overwrites the old
destination with a result using zero instead of suppressing that guest write. The latest
AnyPS5 `Dpp16InactiveSource` fixture supplies a concrete semantic reference. This is a
bounded P1 ISA investigation, not an established TDR cause or a speed optimization.
Reference: installed Dev 078, fast-mode opening 12.15/12.48 FPS; whole fights 14.02/15.39.
Expected work eliminated: erroneous destination writes, not guest draws or GPU drains.
Conditions: preserve active-source behavior, FI/BC overrides, row/bank masks, inactive
destinations, wave32/wave64 halves, and distinct DPP8 zero-fetch behavior. Acceptance:
pre-fix failing real Vulkan regression, focused semantic matrix, full 53/53+ CTest,
standard numbered install, two serial 420 s same-settings fights with >=120 s fighting,
frame-counter windows/waits/RSS/VRAM and screenshot comparison; manual transient-artifact
check remains required. All Dev 077 safe defaults and Dev 078 decoder work are preserved.
Initial pre-flight: branch dev, no active game/build/test, runner heartbeat >7 h old,
no actual KYTY_UFC_* environment overrides; existing uncommitted changes preserved.

## Dev 078 — updated AnyPS5 comparison and tail-branch decoder fix — 2026-10-05

User requested comparison of `C:/Users/cramo/Downloads/AnyPS5-main (1)/AnyPS5-main` and
continued development, with no further approval questions. The new archive has 116 GPU
execution-test sources versus 84 recorded previously; the previous source directory is absent,
so this is a current source audit plus comparison to the prior report, not an exact archive diff.
Shader and system-library findings: `_Build/agent-reports/anyps5-update-comparison-20261005.md`.

Selected one bounded semantic adaptation from AnyPS5 `DecodeRdnaProgram` / `BranchPastEndpgm`:
`ShaderDecoder.cpp` tracks the furthest direct branch target instead of checking only the word
immediately following `S_ENDPGM`. A tail branch after an intermediate return may jump backward
into an earlier tail block or loop. The decoder now retains that code, avoids the old bitmap
allocation/indexing, validates bounds and preserves exact-span exit/debug-branch behavior.

Evidence: added host regression failed before the fix with "decoder truncated a forward branch
beyond an earlier S_ENDPGM" (CTest shader_cfg). Fixed host suite passed, as did 12 real GPU cases
covering wave32/wave64, all early/tail returns and 0/1/3 loop iterations. Numbered build/install
Dev 078 passed. Full CTest **53/53** in 38.59 s; `_Build/agent-reports/dev078-ctest.log`.
Existing Dev 077 engine work and opt-in defaults preserved.

| Run | HUD start | Fighting observed | Fight frames / FPS | Opening sample-window FPS | Median/min | RSS MiB | Device VRAM MiB | Device loss |
|---|---:|---:|---|---:|---|---:|---:|---|
| `dev078-branch-tail-a-20261005-095232` | 103.36 s | 317.36 s | 4427 / 14.02 | 12.15 | 14/2 | 10801.8 | 12260 | none |
| `dev078-branch-tail-b-20261005-100648` | 103.25 s | 317.87 s | 4866 / 15.39 | 12.48 | 13/11 | 10792.5 | 12212 | none |

Both used 420-second AutoFight at 2560x1440/Mailbox, ShaderValidation, GPU breadcrumbs,
Tracy off and default controls, serially after CTest. Opening calculations use frame samples
inside `[first_fight_s, first_fight_s + 45]`: A 519 frames / 42.72 s, B 532 / 42.63 s.
The prior Dev 077 control gives 555 / 42.67 = 13.01 FPS. Current results are slightly lower;
no paired control or recorded foreground/clock evidence establishes a patch-caused change.
Whole-fight rates differ with standing/ground workload; claim no FPS gain.

Example consecutive fight wait counters (B, 30 s): color_metadata 116.89 ms/s,
gpu_read_fault 48.51 ms/s, other_threads 360.71 ms/s. These are site costs, not an additive
exclusive frame-time breakdown. Shader validation cache remains effective (~19–37 ms totals).
A's teardown logs `SymInitialize FAILED 0xc0000004`; no shader validation failure or GPU loss.
B has no error beyond triage matching the informational validation-cache line.
Inspected standing/ground screenshots show fighters and HUD, with old skin/cage/afterglow
artifacts; sparse screenshots cannot certify transient mesh correctness.

Decision: retain the proven decoder correctness fix; GPU TDR and the 30-FPS objective remain
open. Next concrete semantic reference: AnyPS5 DPP16 BC=0/FI=0 inactive-source write suppression,
which Kyty's update helper lacks. Prove it with a GPU fixture before an independent patch;
prioritize TDR draw/hash capture if the hang recurs. ULT queue pooling/lifecycle and RTC overflow
guards are documented library candidates, not automatically imported or claimed UFC hotspots.

Loop maintenance completed: removed obsolete Dev 053/profile-restore/GPU-first priorities,
set 30-FPS acceptance, refreshed auto-loaded review and new archive access. Supervisor waits
on fresh runner heartbeat, active play, builds/tests and foreign automation; own wrapper
ancestors are ignored, actual games/build/test executables are always blockers. Failed process
inspection blocks launch. `Build-Windows.ps1` refuses active play and rechecks before install/
tests; transient install locks no longer cause force-kills. Automation **17/17**, config debug
and dry-run validated; actual pre-flight correctly blocked run A and later cleared for run B.
No commit/push; existing changes preserved. Supervisor resumption is checked after handoff.

## Claude session (part 3) — GPU-first indirect args, SGPR-location check, power-throttling opt-out — UFC Dev 056–061 — 2026-10-04

Source idea: AnyPS5 (`core/libs/prx/libSceAgcDriver/Execution/src/Driver/Draw/IndirectDraw.cpp`)
classifies indirect draws up front and never reads GPU arguments on the CPU when the draw is
eligible; it also checks the packet's BASE_VTX_LOC / START_INST_LOC against the shader's
offset SGPRs.

Changes (final tree = Dev 061):
- `DrawIndirectAt` tries the GPU-sourced draw first, even when a submitted host copy exists
  (Dev 055 waited for the copy's submission first: `args_copy`). The watch stays registered
  (`BufferCache::WatchIndirectArgs`) so a deferred NUM_INSTANCES read can still use a copy.
  Host copy remains the fallback for ineligible draws, then the guest read.
  `KYTY_UFC_GPU_INDIRECT_FIRST=0` restores the Dev 055 order.
- Counters: `lazy_copy` / `lazy_guest` (deferred NUM_INSTANCES source) and
  `indirect sgpr locs vertex|instance match/none/mismatch/no_fetch` (first 16 mismatches
  logged). Pure helpers `IndirectOffsetLocSlot` / `ClassifyIndirectOffsetLoc` in renderDraw.
- Cycle 24's uncommitted `GpuIndirectMulti` test never compiled (it sat in
  `CheckUnifiedTextureCacheFlow` and used `CheckRasterization` locals). Moved into
  `CheckRasterization`; CPU tables are written before `MapMemory` (that harness has no fault
  handler/guest GPU); GPU tables live on a separate page; teardown drops GPU ownership via
  `BufferCacheTestAccess::DropGpuOwnership`. Added "GPU-sourced preferred over submitted host
  copy" and "deferred instances from host copy" asserts. New `IndirectOffsetLoc` unit test.
- Windows power-throttling opt-out (`Common::OptOutThreadPowerThrottling` on guest pthreads,
  Common::Thread, GPU and present threads; process-level `SetProcessInformation` fails with
  error 1 on this machine, per-thread works). `KYTY_UFC_POWER_THROTTLING=1` keeps the default.

Tests: full CTest **53/53** on Dev 056, 057, 059 and final 061; focused rasterization /
buffer-cache / draw-offset suites pass.

Fights (2560x1440, Mailbox, ShaderValidation, 420 s, no Tracy):

| run | build | fight start | first 45 s | whole fight | args_copy | gpu_read_fault |
|---|---|---|---|---|---|---|
| dev055-indexed-retry2 (baseline) | 055 | 116.4 s | 9.6 | 11.07 | 16.6 s | 5.7 s |
| dev056-gpufirst | 056 | 103.4 s | 16.0 | 21.53 | 0 | 24.5 s |
| dev056-gpufirst-repeat | 056 | 116.4 s | 10.3 | 12.08 | 0 | 6.6 s |
| dev057-nothrottle | 057 | 103.3 s | 16.3 | 19.74 | 0 | 24.2 s |
| dev059-fg | 059 | 103.3 s | 14.9 | 17.72 | 0 | 24.3 s |
| dev059-bg (user was away: not a background test) | 059 | 103.3 s | 15.0 | 16.68 | 0 | 21.8 s |
| dev060-control-retry (host copy first) | 060 | 103.3 s | 14.6 | 19.09 | 25.7 s | 22.6 s |

(`dev060-control-hostcopy` lost the device at 30 s during boot, WriteInvalid at 0 after
DrawIndexAuto; the retry was clean. Device loss had not been seen since loop13.)

Findings:
- Mechanism works: `copy=0`, `args_copy=0`, every indirect draw GPU-served (`gpu_indirect`),
  `lazy_*`=0, no device loss in the GPU-first runs, visuals correct (standing exchanges,
  ground-and-pound, referee, blood decals, HUD).
- **No FPS gain from GPU-first.** The same-mode control (Dev 060, host copy first) ran 14.6 /
  19.09 FPS with 25.7 s of `args_copy` waits, the same as the GPU-first runs; those waits are
  not on the frame's critical path.
- **Runs are bimodal**: "slow" (fight HUD at 116.4 s, ~10 FPS opening) vs "fast" (103.3 s,
  15-16 FPS opening, same standing scene; game clock advances ~1.7x faster). The process CPU
  rate is the same in both modes (~2.3 busy cores), so per-CPU-second throughput differs:
  most likely core placement or clock (hybrid i7-14700K, EcoQoS for a non-foreground window),
  not emulator code. Every Claude run except one was fast; the loop's Dev 054/055 runs and
  the dev056 repeat were slow. The power-throttling opt-out is **unproven** (all runs after
  it were fast, but no real background run was made). Earlier "no gain" or "gain" verdicts
  that compared runs across modes are unreliable; compare only same-mode runs (check
  `first_fight_s`).
- Whole-fight FPS also varies with the fight's course (ground fighting from a top-down
  camera is cheaper than standing with crowd); compare first-45-s windows.
- RSS 10.8-11.3 GiB, VRAM 12.2-12.9 GiB, GPU util 55-64% in fast mode (37-42% slow).

Decision: retain GPU-first (removes ~26 s of waits per run, tests stronger, no regressions),
**no FPS claim**. Retain the throttling opt-out as harmless and gated, effect unproven.
SGPR locations: UFC indirect draws all use shaders without embedded fetch (`no_fetch`=100%),
so the check found nothing to fix here.

Follow-up — UFC Dev 062: a manual launcher session on Dev 061 lost the device (`WriteInvalid` at
page 0, same signature as `dev060-control-hostcopy`). That signature appears only in builds with
the per-thread power-throttling opt-out (Dev 059+; 2 of 4 sessions), never in Dev 056/057 or
earlier runs. Dev 062 makes the opt-out opt-in (`KYTY_UFC_POWER_THROTTLING=0`); everything else
is unchanged. Full CTest 53/53. Still to verify: repeated sessions on Dev 062 without device loss.
If it recurs, the race is independent of the opt-out; capture breadcrumbs (AutoFight enables them).

User manual confirmation (Dev 062, opt-out off): 15-16 FPS with the game window in front, 9-11 FPS
as soon as another window is clicked. The slow/fast benchmark modes are window focus.
Manual session also hit the bad-PROFILE guest crash (pc 0x9034b8c02, null read in Job6 about 1 min
after a PROFILE save); restored PROFILE from install-snapshot-033, bad copy in
_Build/claude-runner/savedata-bad-20261005-0050.

REGRESSION (user manual test): Dev 062 shows momentary vertex explosions on fighters (skin/hair
meshes stretched into spikes or a screen-filling smear, mostly in ground-and-pound) and some missing
background geometry. install-snapshot-033 (Dev 033) shows none. Introduced between Dev 034 and 062.
Bisect first with KYTY_UFC_GPU_INDIRECT=0 (disables Dev 054-061 GPU-sourced indirect draws).
Automated screenshot checks (5.5 s cadence) miss momentary geometry corruption; do not mark visuals
"correct" from a handful of screenshots.
Stutters 15-23 FPS with dips to 3 FPS coincide with synchronous shader compiles mid-fight.

BISECT (user manual tests; source states rebuilt into _Build/bisect-0NN-src): Dev 033 snapshot good;
Dev 044 source good (no explosions/crash; white specks/afterglow trails on the cage behind fighters
noted as a separate, older artifact); Dev 053 source BAD (device lost, WriteInvalid at page 0, at
boot). Regression is inside Claude sessions 1-2 (Dev 045-053, `_Build/claude-session-changes.patch`).
Prime suspect: the BDA dirty-log fast path in `SynchronizeBuffersInRanges` (skips the per-BDA full
walk, so GPU-image-to-buffer syncs or unlogged changes can be missed). Dev 068 adds switches
KYTY_UFC_BDA_DIRTY_LOG=0 / KYTY_UFC_GC_FRAMES=0 / KYTY_UFC_ARGS_COPY=0 to bisect by environment.

BISECT RESULT AND REVERT (2026-10-05): user env tests on Dev 068 -- test 1 (BDA dirty log off) crashed
(guest null read pc 0x9034b8c02); test 2 (dirty log, GC frames, args copy, GPU DCC, fill convert all
off) clean; test 3 (GC frames + args copy off) exploded. Dev 069 made fill-convert and GPU-DCC opt-in,
but automated run dev069-safe-a still lost the device at boot (same breadcrumbs as dev060: DrawIndexAuto
args 1024/1280/1536/1792,3 with ps 0x1140337500 at VS 87/PS 118; WriteInvalid page 0). So the
regression is not (only) those two; it is somewhere in the Dev 045-053 patch and is intermittent.
FPS evidence: none of Dev 045-061 measurably raised FPS in same-mode comparisons.
DECISION: dev source reverted to the user-verified Dev 044 state (UFC Dev 070 build). Everything
later is preserved: _Build/claude-bisect/dev062-state, dev069-state, moved-out/ (DCC evaluator
files), _Build/claude-session-changes.patch (Dev 045-053), claude-session3-changes.patch (cumulative).
Re-introduce pieces one at a time only with multi-run stability evidence (boot device loss, guest
crash, manual ground-and-pound check for mesh explosions).

FOLLOW-UP (Dev 070-073): Dev 070 (= Dev 044 source) also lost the device mid-fight in one of two runs
(InstructionPointerUnknown, 172 addresses), so GPU instability predates the sessions. The guest null
read at pc 0x9034b8c02 also predates them (loop runs on Dev 037/039/042/043). The boot-time WriteInvalid
page-0 device loss is so far only seen on Dev 053+ builds with GC-frames + args-copy + dirty log on.
Restored the Dev 045-062 tree (dev069-state), then made opt-in: FILL_CONVERT, GPU_DCC, ARGS_COPY,
GPU_INDIRECT, POWER_THROTTLING opt-out, PCORES (new: restrict the process to P-core CPU sets; one
fast-mode run was not faster). Default on: BDA dirty log, GC frames. Final tree UFC Dev 073, 53/53.
Fast-mode first-45 s FPS: Dev 070 (044 src) 12.1; Dev 072 (dirty log + P-cores) 11.8; Dev 069 (dirty
log + GC + args + GPU indirect) 14.6; Dev 056-060 (all on) 14.6-16.3. Slow mode: Dev 073 9.1, Dev 071
7.9, Dev 055 9.6. Needs a manual check of Dev 073 for mesh explosions, then measure GC frames alone.

DEV 074-077 (2026-10-05): user confirmed Dev 074 (BDA dirty log OFF) has no mesh explosions -> the
dirty-log fast path is the explosion cause (opt-in KYTY_UFC_BDA_DIRTY_LOG=1 until the missed-upload
case is found). Shader build timing (Dev 075, printed with every 'Shaders:' line): ~800 shaders per
session cost translate ~2.8 s, compile ~0.9 s, SPIR-V validation ~5.5 s (~7 ms each), driver pipelines
~0.3 s -- the intro/character-select 2-5 FPS stalls. Dev 076: on-disk known-valid SPIR-V cache
(_PipelineCache/validated_spirv.bin, keyed by XXH3-128 of the SPIR-V and the SPIRV-Tools version):
validation 4006 ms -> ~30 ms on the next launch; new SPIR-V is still validated.
The recurring guest crash (null read at pc 0x9034b8c02, Job threads, ~55-110 s, 3 of 4 Dev 076 runs)
is red-zone corruption: the code reloads a pointer from [rsp-0x28] after a store that can hit a
write-tracked page, and the Windows exception dispatch overwrites the red zone. Kyty's existing
--redzone guest-instruction patcher fixes it: 0 guest crashes in 4 protected runs (AutoFight now passes
--redzone when _Build/claude-runner/redzone.flag exists). Dev 077 makes red-zone protection the default
on Windows (KYTY_UFC_REDZONE=0 disables) and pins only the GPU command and present threads to the P-core
CPU set (AnyPS5 WorkerAffinity design; KYTY_UFC_PIN_GPU=0 disables): fight HUD at 100.3/106.3 s, one
clean run 16.06 FPS whole fight, 13.0 first 45 s. 53/53 tests on Dev 076 and 077.
Remaining stability issue now that runs survive the intro: GPU TDR mid/early fight (device lost,
gpu_loss_class=timeout, nvlddmkm 153, InstructionPointerUnknown x hundreds; breadcrumbs DrawIndex x2),
also seen on the Dev 044 source -- a guest shader hang, pre-existing. AutoFight now waits for a
previous emulator to exit (back-to-back runs failed to reserve guest address space).

Next hypotheses: (1) make benchmarks mode-aware: record `first_fight_s`, foreground window,
and process/thread core placement; a real background A/B for the opt-out. (2) The slow/fast
modes show the fight is bound by guest CPU threads, not GPU drains: profile the guest
threads (`other_threads` waits 125-196 s per run) with Tracy rather than removing more GPU
waits. (3) `gpu_read_fault` (~22-24 s per fast run) is now the largest named GPU wait; it
moved in from `args_copy`, but is also likely off the critical path.

## Cycle 24 — M1 milestone (in progress) — superseded: multi routing built and tested in Claude session part 3

Mechanism: `DrawIndirectMulti` directly reads every guest argument, bypassing the
Dev 053–055 host-copy/native paths. Route each element through the same implementation
as single indirect draws (no native batching or GPU count-read change). Reference:
Dev 055 `dev055-indexed-retry2-20261003-235012`, ~116 s scenario, 3356 / 11.07 FPS,
fight gpu_read_fault ~17 ms/s. Expected elimination: GPU argument readback per eligible
multi element. Conditions: preserve count clamp/zero-count early return, 64-bit stride,
offsets, ordering, final NUM_INSTANCES side effect and all indexed/mesh/quad/restart
fallbacks. Acceptance: GPU-producer -> renderer pixel equivalence and subsequent direct
draw state fixture; native/fallback fraction; 53/53+ CTest; qualifying 420 s fight,
scenario/workload/screenshot matched title frames, wait deltas, RSS/VRAM. Zero eligible
coverage is not a completed performance milestone. Preflight: dev, actual tree Dev 055;
runner heartbeat >87 min old, no active emulator/CTest/build. Preserve prior changes.

## Cycle 23 — GPU-sourced indexed indirect draws (path unexercised in fight) — UFC Dev 055 — 2026-10-03/04

One engine change (ENGINE-REVIEW priority 1, second slice): indexed indirect draws whose
arguments are GPU-owned and have no submitted host copy are issued as
`vkCmdDrawIndexedIndirect` from the buffer cache (barrier-ordered, no drain/submit) instead
of draining the GPU to read them on the CPU. Served only when provably safe: 16/32-bit
index type (8-bit needs CPU expansion), `INDEX_BUFFER_SIZE == 0` (clamping needs the
CPU-visible count), non-quad primitive, non-mesh shader, and primitive restart decidable
without scanning the unknown indices (custom reset values fall back). The index buffer is
bound at the guest index base from the already-cached allocation (never created/joined
here; oversized remainders fall back), CPU-dirty pages over the remainder are synchronized,
and firstIndex/vertexOffset come from the GPU arguments (guest
`DrawIndexedIndirectArgs` is word-for-word `VkDrawIndexedIndirectCommand`; static_asserted
field offsets). `m_num_instances` stays lazy but is now size-aware (16 vs 20 bytes), since
both layouts keep `instance_count` at the same offset. `KYTY_UFC_GPU_INDIRECT=0` disables.
Preserved: all Dev 045–054 work (BDA dirty log, coherent fills, GPU DCC, non-indexed GPU
indirect + host copies, wait-site counters extended with `gpu_indexed`/`idx_fallback`,
viewport/scheduler/descriptors).

Files: `render.h` (`GpuIndirectDraw` index binding, `DrawIndexGpuIndirect` decl),
`renderDraw.cpp` (restart-without-scan helper, `DrawIndexGpuIndirect`, indexed emission +
binding in `ExecutePreparedDraw`), `bufferCache.h/.cpp` (`TryObtainGpuIndexBinding`),
`graphicsRun.cpp` (indexed GPU attempt, size-aware `ResolveNumInstances`, layout asserts),
`commandProcessor.h` (pending args size), `masterSemaphore.h/.cpp` (outcomes 6/7 + report),
`ShaderRecompilerComputeTests.cpp` (index-binding asserts in `BufferCacheIndirectArgsCopy`).

Tests: `Build-Windows.ps1 -SkipTests` → **UFC Dev 055** (pre-existing warnings only).
Focused 7/7 (buffer_cache_dirty_gc incl. new asserts, memory_tracker, texture_mip_trim,
compute_meta_clear_classification, scheduler/resource). Full CTest **53/53** before the
gameplay run and again after it — no regressions.

Runs (same settings 2560x1440/Mailbox/ShaderValidation, no Tracy): first attempt
`dev055-indexed-20261003-233751` stalled on the boot splash (exit 0 at 409 s, no fight —
known harness variance, cf. cycle 19); immediate retry failed to launch (`failed to reserve
guest address space` — previous-process memory hazard, waited 4 min). Qualifying retry
`dev055-indexed-retry2-20261003-235012`: timeout 421 s, fight 116.41 s (≈116 s scenario,
same as Dev 051/053/054), 304.6 s fighting, median/min **10/8**, title-counter **3356
frames / 11.07 fps** whole-fight (Dev 054: 3229 / 10.68 — parity, no gain claimed); RSS
10669 MiB, VRAM 12479 MiB; no device loss, no validation/matched errors (triage clean);
visuals correct (clinch/grappling, cage/crowd/HUD/canvas intact, pre-existing speckling
only).

Drain evidence (30 s console counters): `gpu_indexed`=**0**, `idx_fallback`=**0** for the
entire run — the GPU indexed path never triggered. Every indexed argument read was served
by a host copy (`copy`=8283); indexed producers in this workload are always submitted
before the draw, unlike non-indexed same-command-buffer producers (`gpu_indirect`=7615).
Fight `gpu_read_fault` ~17 ms/s (Dev 054: 10–28), `gpu_untagged` ~13 ms/s, color metadata
0. Nothing was removed from the drain, and nothing regressed.

Decision: **provisionally retain** (tests 53/53 twice, FPS/RSS/VRAM parity, no stability or
visual regression, fallback on any doubt, new counters have diagnostic value), but **no
benefit claimed** — the draw path is unit-tested, never executed in-game, and removed no
drains. Next cycle: attribute the remaining fight `gpu_read_fault` — prime suspect is
`DrawIndirectMulti`, which `memcpy`s guest args directly with no TryRead/counter — then
either watch or GPU-source multi-draws; alternatively prove submit-ordering so GPU-sourced
draws can be preferred even when a host copy exists (kills the ~2 ms `args_copy` waits).

## Cycle 22 — GPU-sourced non-indexed indirect draws — UFC Dev 054 — 2026-10-03/04

One engine change (ENGINE-REVIEW priority 1, first slice): indirect draws whose argument
producer is in the same command buffer no longer drain the GPU to read their arguments on
the CPU. Non-indexed `DrawIndirect` whose arguments are GPU-owned and 4-byte aligned is
issued as `vkCmdDrawIndirect` from the buffer-cache buffer (guest `DrawIndirectArgs` is
word-for-word `VkDrawIndirectCommand`), with a barrier ordering earlier GPU writes before
the indirect read. Indexed indirect, multi-draws, quad-list primitives and mesh shaders
fall back to the existing CPU path. `m_num_instances` (NUM_INSTANCES side effect) is
resolved lazily: the GPU path marks it pending and a later draw that needs it reads the
arguments then (host copy once submitted, drained guest read otherwise). Everything is
preserved: BDA dirty log + batch-walk fallback, coherent metadata fills, GPU DCC clear
evaluation, indirect-args host copies, wait-site counters (new `gpu_indirect` outcome),
viewport/descriptor/scheduler work. `KYTY_UFC_GPU_INDIRECT=0` disables the new path.

Files: `bufferCache.h/.cpp` (`TryObtainGpuIndirectArgs`: no-drain obtain + indirect-read
barrier, no submit/wait), `render.h` (`GpuIndirectDraw`, `DrawAutoGpuIndirect` decl,
`ExecutePreparedDraw` indirect param), `renderDraw.cpp` (`DrawAutoGpuIndirect` mirrors
`DrawAuto` head: metadata/decompression consume, VS check, topology, rect-list skip, mesh
and quad-list fallback; emit via `drawIndirect`), `graphicsRun.cpp` (wiring, deferred
`ResolveNumInstances`, env gate), `commandProcessor.h` (pending state),
`masterSemaphore.cpp` (6th indirect-args counter + report), `ShaderRecompilerComputeTests.cpp`
(new `gpu indirect args` / `bad size` / `misalignment` asserts in `BufferCacheIndirectArgsCopy`).

Tests: `Build-Windows.ps1 -SkipTests` → **UFC Dev 054** (only pre-existing warnings).
Focused 7/7 (`buffer_cache_dirty_gc` incl. new asserts, `memory_tracker`,
`texture_mip_trim`, `compute_meta_clear_classification`, scheduler/resource). Full CTest
**53/53** before the gameplay run and again after it — no regressions.

Run `_Build/autolab/dev054-gpuindirect-20261003-230715` (420.37 s, same settings
2560x1440/Mailbox/ShaderValidation, exit=timeout): fight 116.43 s (≈116 s scenario, same
as Dev 051/053), 303.9 s fighting, median/min **10/8**, title-counter **3229 frames /
10.68 fps** whole-fight; 45 s windows: 9.64 / **12.16** / 10.76 / 10.0 / 11.87 / 10.12 /
10.01. RSS 10994 MiB, VRAM 12730 MiB (Dev 053: 10827/12515; delta is content variance,
not allocated by this change — per-draw cost is one barrier + no new persistent buffers).
No device loss, no driver events, no validation/matched errors (triage clean). Visuals
correct: fighters/cage/canvas/HUD render, pre-existing speckling only, blood decals and
grappling sequences intact — no overdraw/corruption from GPU-sourced args. GPU util 37%
median (CPU-bound, rules out a stale-args GPU-overload bug).

Drain evidence (30 s console counters): `gpu_indirect` **6788** total vs `unsubmitted`
6777 — essentially every same-command-buffer indirect read is now GPU-served;
`gpu_read_fault` ~10–28 ms/s in fights (Dev 053: 30–60); `gpu_untagged` ~1–35 ms/s
(unchanged); color metadata still 0. Remaining `gpu_read_fault` is indexed indirect (not
yet converted) plus host-copy waits (`copy`=8816, each ~0.2 ms).

Decision: **provisionally retain** (mechanism proven, drains down, tests/visuals clean,
within-run windows reach Dev 053-level 12.2 fps so no systematic regression is
established), but **no FPS gain claimed** — the whole-fight 10.68 vs Dev 053's 13.3
shots-window cannot be compared directly (old runs lack the frame column; this fight ran
heavy grappling), and one run is weak evidence. Next cycle: A/B control — same Dev 054
build with `KYTY_UFC_GPU_INDIRECT=0` plus a repeat — then GPU-sourced *indexed* indirect
(index-range clamp, NUM_INSTANCES already deferred, 8-bit expansion stays CPU).

## Claude session (part 2) — GPU DCC clear evaluation, indirect-args host copies — UFC Dev 049–053 — 2026-10-03

Continues the Dev 045–048 entry below. All changes uncommitted on `dev`; final tree = Dev 053.

### Changes

1. **Selected-slice metadata check** (`textureCache.cpp`). `MaterializeColorClear` only decodes the
   selected slices, so only those bytes must be current: the GPU-ownership test and any readback
   now cover `[first, first + count)` slices instead of the whole metadata allocation.
2. **GPU-side clear-code evaluation** (new `cache/dccClearEvaluator.{h,cpp}`,
   `shaders/dcc_clear_check.comp`, `textureCache.*`, `vulkanWindow.cpp`, `graphicContext.h`,
   CMake). When the selected metadata is GPU-owned, a render target (single-sample, non-volume,
   one level) records a compute check (one workgroup per slice: all bytes equal the first, and
   that byte is one of the codes `DecodeColorClear` accepts) that writes one predicate per
   candidate and expands a matching slice to 0xff, then clears each slice with
   `vkCmdClearAttachments` under `VK_EXT_conditional_rendering` per candidate color. Guest
   contents are initialized before the conditional clear; the image is committed as GPU-written.
   Same decision rules and 0xff expansion as the CPU path; CPU path remains for clean metadata,
   other binding types and when the extension is unavailable. `KYTY_UFC_GPU_DCC=0` disables.
3. **Indirect-argument host copies** (`bufferCache.*`, `renderCompute.cpp`, `graphicsRun.cpp`).
   `DrawIndirect` watches its argument ranges. A tracked GPU write to a watched range marks it
   pending; after each compute dispatch and PM4 DMA operation `RecordPendingArgsCopies` copies
   pending ranges to a host-visible slot. `TryReadIndirectArgs` uses the copy only when the range
   is GPU-owned, the copy is valid, no tracked write and no BDA shader (`PrepareBda` epoch) came
   after it, and its command buffer was already submitted; otherwise the original guest read
   (and drain) happens.
4. Rejected: Dev 052 also submitted the command buffer right after each copy. All indirect reads
   then used the copy, but the wait averaged the same ~2.5 ms as a drain; reverted in Dev 053.

### Tests

Full CTest **53/53** for Dev 049, 050, 051, 052, 053 (`_Build/claude-dev053-tests.log`,
`_Build/claude-dev053-LastTest.log`). `compute_meta_clear_classification` and `--dcc-clear-only`
rerun the complete RenderExecutorColorMetadataClear suite with GPU evaluation enabled
(`ColorMetadataClearOnGpu ok`, asserts the GPU path ran). `buffer_cache_dirty_gc` adds
BufferCacheIndirectArgsCopy (unwatched, unsubmitted, stale-after-write, stale-after-BDA,
refreshed copy, published guest arguments). The harness enables conditional rendering when the
device supports it, with the runtime flag off for all other cases.

### Runs (2560x1440/Mailbox, default MenuDrive, ShaderValidation, Tracy off, 420 s)

| Build | Session | First fight | Fight median/min | Frames shot 40→70 | RSS / VRAM MiB |
|---|---|---|---|---|---|
| 049 | `claude-dev049-gpudcc-20261003-210434` | 116.5 s | 10/8 | 1658 (10.0 fps) | 10836 / 12587 |
| 051 | `claude-dev051-args-20261003-212209` | 116.5 s | 12/8 | **2224 (13.5 fps)** | 10846 / 12526 |
| 053 | `claude-dev053-goodsave-20261003-220948` | 116.4 s | 12/8 | **2194 (13.3 fps)** | 10827 / 12515 |

Same scenario reference: Dev 043 (loop20 retry) 1478 frames (9.0 fps). Dev 048 2190. No device
loss/driver events; only the known shutdown `SymInitialize FAILED`. Visuals match earlier builds.
Fight GPU-thread waits (ms/s): color metadata 70–85 → **0**; total ~125 → ~50–70. Indirect args:
~25% of fight reads use the host copy (~0.2 ms wait), the rest still drain (copy in the same
command buffer as the draw).

### Save-data incident (not an engine regression)

The Dev 052 run crashed at 57 s with the known guest null fault (PC 0x9034b8c02) while the game
was writing `_Build/windows/install/_SaveData/PPSA03541/PROFILE`. Every later launch (Dev 052,
Dev 053, with and without EA `errorsdata` files) crashed at ~57 s (PCs 0x9034b8c02 and
0x9055cd780, null-structure write) right after the profile rewrite; two launches also failed to
reserve guest address space. Restoring PROFILE from `_Build/install-snapshot-033` fixed it
(Dev 053 ran 420 s). The suspect profile is in `_Build/claude-runner/savedata-backup-20261004-0212`.
If the 57 s fault recurs, check/restore the profile before attributing it to code.

Next hypothesis: indirect draws whose producer is in the same command buffer (most fight reads)
need GPU-sourced `vkCmdDraw*Indirect` (index-buffer range and NUM_INSTANCES side effects must be
handled); remaining CPU cost is per-draw SRT/descriptor work (~17 us/draw PrepareDrawRenderState).

## Claude session — BDA dirty log, drain diagnostics, metadata fills — UFC Dev 045–048 — 2026-10-03

Interactive session while the OpenCode loop was paused (loop cycle 21's uncommitted
`descriptors.cpp` alias-provenance hoist was left in place and is part of these builds).
Builds/runs were serialized through `_Build/claude-runner/Claude-JobRunner.ps1`
(fixed actions: Build-Windows -SkipTests, CTest, AutoFight, git status). Nothing committed.

### Changes (all retained)

1. **BDA CPU-dirty log** (`memoryTracker.*`, `bufferCache.*`, `renderContext.*`; applied from
   `_Build/agent-reports/fps-fix-1-bda-dirty-log.patch`). Every path that sets CPU-dirty bits
   (region creation, `MarkRegionAsCpuModified`, `UntrackMemory`, write-fault invalidation)
   records the range in a 256-entry spin-locked log. `SynchronizeBuffersInRanges` syncs only
   logged + newly registered ranges; overflow, a mapped-range version change, or the first call
   fall back to the existing ordered BDA batch walk (kept). `TakeCpuDirtyRanges` reserves before
   locking (fault handler may spin on that lock).
2. **Texture-size memo** (`tile.cpp`, fps-fix-3): per-thread 32-entry memo of the pure
   `TileGetTextureSize` result; linear layouts keep caller-owned mip fields.
3. **Wait diagnostics** (fps-fix-2 Tracy zones + new console counters). `MasterSemaphore::Wait`
   attributes real blocking waits to a thread-local `WaitSite` and logs a cumulative
   `GPU waits (...)` line every 30 s: per-site count/ms, color-metadata readback writer mask,
   uniform-fill conversion outcomes, buffer-cache creations/GC retirements. GPU-thread
   unattributed waits and other threads are reported separately.
4. **Color-metadata uniform fills** (`renderCompute.cpp`, `textureCache.*`, `bufferCache.*`). A
   dispatch already proven by `ResolveComputeBufferFill` (single store, every element written
   with one value, exact descriptor coverage) that overlaps render-target DCC/CMASK metadata
   and is ≤4 MiB/dword aligned is executed as `BufferCache::FillBufferCoherent` instead of the
   shader. Clean ranges are filled in guest memory (same as PM4 DMA fill); GPU-owned whole 4 KiB
   pages are filled on the GPU timeline **and** the same bytes are written to the guest backing,
   then unmarked GPU-modified, so the next render-target discovery decodes the clear code without
   draining the GPU. Partial pages keep the old GPU-only path. `KYTY_UFC_FILL_CONVERT=0`
   disables it for A/B.
5. **Buffer GC age in frames** (`bufferCache.*`, `renderContext.cpp`). GC ticks advance once per
   command-processor pass (~100–150 per UFC frame), so the fixed 160-tick age retired clean
   buffers reused every frame. Below the critical threshold the age is now
   max(160 ticks, 2 measured frames, capped at 4096 ticks); the critical path keeps 80 ticks.
   Retirements still ~110/s and creations ~200/s in fights; no separate gain proven.

### Tests

New: `memory_tracker` TestCpuDirtyLog (creation/explicit/fault/untrack logging, overflow → walk);
`texture_mip_trim` memo consistency incl. linear caller fields; `buffer_cache_dirty_gc`
CheckBufferCacheCoherentFill (no submit/drain, ownership released only for whole pages, guest +
native bytes, partial-page and clean paths). Full CTest **53/53** for Dev 045, 046, 047, 048
(`_Build/claude-runner/done/*-tests.log`, `_Build/claude-dev045-tests.log`).

### Runs (all 2560x1440/Mailbox, default MenuDrive, ShaderValidation, breadcrumbs, Tracy off, 420 s)

| Build | Session | First fight | Fight median/min FPS | Frames shot 40→70 (≈165 s) | RSS / VRAM MiB |
|---|---|---|---|---|---|
| 045 | `_Build/autolab/claude-dev045-on-20261003-195946` | 116.5 s | 11/6 | 1890 (11.5 fps) | 10655 / 12452 |
| 046 | `_Build/autolab/claude-dev046-diag-20261003-201228` | 122.5 s | 8/7 | 1680 (10.2 fps) | 10686 / 12465 |
| 047 | `_Build/autolab/claude-dev047-coherent-20261003-202402` | 104.3 s | **17/12** | **2828 (17.1 fps)** | 10925 / 12695 |
| 048 | `_Build/autolab/claude-dev048-gcframes-20261003-204413` | 116.5 s | 10/7 | **2190 (13.3 fps)** | 10757 / 12482 |

Same-schedule references from the window-title frame counter: fight at ~104 s — Dev 037
2137 (13.0 fps), Dev 038 2649 (16.1 fps); fight at ~116–128 s — Dev 043 (loop20 retry) 1478
(9.0 fps). The game reaches one of two fight scenarios depending on menu timing; compare
within a scenario. No device loss, driver events or matched errors in any run. Fighters, cage,
crowd and HUD render; speckling unchanged; no visual regression seen.

Tracy (`_Build/autolab/claude-dev047-tracy-20261003-203106`, vs cycle 16 capture, per call):
`SynchronizeBuffersInRanges` 87.1 → **5.8 us**; `DrawIndex` 38.2 → **27.1 us**;
`PrepareGraphicsBindings` 10.9 → 3.9 us; `ResolveRenderColorTarget` 3.2 → 2.0 us.

### Remaining GPU-thread drains (fight, per second)

- Color-metadata readbacks ~70–85 ms/s. Writer mask: every one follows a **shader storage
  write whose descriptor spans 36–92 MiB and covers several RT metadata ranges** (e.g. shader
  0xea0aceac518ec52d write 0x1165b60000+0x2400000), so tracking must assume the DCC changed.
  Before item 4, half also followed a uniform fill and those are now gone.
- GPU-thread read faults ~35–50 ms/s ≈ `CpOpDrawIndirect` reading GPU-written indirect args.

Next hypothesis: GPU-side evaluation of DCC clear codes (compute check + conditional clear) to
remove the remaining metadata drains without CPU readback; then GPU-sourced indirect draws.

## Cycle 20 — scheduler fast path measured, retained — UFC Dev 043 — 2026-10-03

### Intent

Cycle 19 implemented the ENGINE-REVIEW priority-2 scheduler empty-retirement
fast path but obtained no qualifying gameplay measurement (one boot-splash
stall, one known guest null fault). Per the one-thing-at-a-time rule, this
cycle adds no new engine code: it rebuilds the identical patched tree,
re-runs the full regression suite, and obtains the required clean
same-settings gameplay comparison. Verifying a pending patch is not a
baseline-only repeat.

### Result and decision

**Retain the scheduler fast path with full evidence; no performance gain
claimed.** No engine file was modified this cycle; net changes are this log,
the handoff, and the generated build counter (42 -> 43).

- `Build-Windows.ps1 -SkipTests` passed, installed **UFC Dev 043**
  (`_Build/loop20-build.log`). Engine code is bit-identical to Dev 042
  (only `commandScheduler.cpp` fast path + `SchedulerTimeline` test from
  cycle 19 on top of the retained Dev 041 viewport patch).
- `kyty_tests` rebuilt (`_Build/loop20-test-build.log`). Focused
  `scheduler|resource_materialization|resource_tracking`: **3/3 passed**,
  including `command_scheduler_timeline` covering the empty-queue no-op and
  later retirement. Full CTest: **53/53 passed**, 40.50 s
  (`_Build/loop20-tests.log`). `git diff --check` clean.
- CLEAN attempt `_Build/autolab/loop20-scheduler-clean-20261003-185733/`:
  emulator exited at **71.55 s** with no fighting. Fault context is the
  **known intermittent guest null fault**: Job4, 0xc0000005, PC
  **0x9034b8c02**, access=1/address=0, runtimeLinker.cpp:719 — the identical
  PC/file:line as cycle 19-retry (Job6) and cycle 17 (Job2), now on a third
  host patch across three job threads. No device loss. This recurrence on
  different engine code confirms guest/harness variance, not patch causation.
- RETRY `_Build/autolab/loop20-scheduler-retry-20261003-185908/` (same Dev 043
  install, 2560x1440/Mailbox, default 54-press MenuDrive, ShaderValidation,
  Tracy off): timeout **420.72 s**, no matched errors/device loss/driver
  events. Fight detected **128.56 s**, **266 samples**, **292.16 s** observed,
  max gap **1.91 s**, no zero-FPS fight samples. Actual fight median/min
  **8/5 FPS**, CPU **1.810 cores**, peak RSS **10696.0 MiB**, device-wide VRAM
  **13033 MiB**, GPU median **35%**. End-of-run shader totals: VS 282 / PS 246
  / CS 268 — viewport fix holding (286 in Dev 041 run vs 1052 pre-viewport).
  `shot_0051.png`: grappling fighters, cage/crowd/HUD render; skin speckling
  persists, no visual regression versus prior runs.
- Comparison: 8/5 vs cycle-16 merged clean 7/5 and Dev 041 run 7/5 is within
  run/workload variance (standing vs grappling coverage differs); the
  per-draw timeline query is a small constant saving next to ~20 us draw
  preparation, so parity is the expected result. No FPS/resource/quality
  improvement is claimed. What is established: the fast path preserves
  waits/cleanup (regression tests), causes no test/gameplay stability
  regression, and survives 292 s of fighting without device loss.

Next: structural per-draw work for steady fight FPS — the cycle-16 profile
attributes PrepareDrawRenderState 9.447 s / MaterializeResources 5.730 s /
SRT refresh 4.254 s inclusive per 30 s of fighting. Candidate: cache
identical host bindings/dynamic state at the command-buffer level with
reset/layout/image-rebind invalidation (ENGINE-REVIEW draw/descriptor row),
retaining alias checks and synchronization. Do not cache SRT across draws
without guest-memory invalidation proof. Objective unmet (8 FPS vs stable
60); no goal-reached file.

## Retained runtime viewport engine change — UFC Dev 041

Implemented the shader-research finding instead of another profiling-only cycle. Native
screen-space vertex shaders now read viewport scale/offset from per-draw push data.
Shader cache keys no longer contain these four changing floats in runtime mode. Half
extents, clipping mode and all other semantic state stay specialized. Reserve the header
before shader resources; mesh's six draw words remain separate. The same descriptor
commit sends viewport data without adding a separate per-draw push command.

Regression checks: identical keys/SPIR-V for changed scale/offset; valid PushConstant
loads; distinct static vs runtime modes and half extents; 128 animated viewport samples.
Static standalone compiler behavior retained. Full CTest: **53/53 passed** in 48.63 s,
`_Build/viewport-runtime-tests.log`. Build/install `_Build/viewport-runtime-build.log`.

Actual run: `_Build/autolab/engine-runtime-viewport-20261003-181440/`; 2560x1440/Mailbox,
default 54-press schedule, shader validation, normal breadcrumbs, Tracy off. Survived
300.47 s, fight detected 122.56 s, observed fighting 177.91 s. No device loss, driver
events, matched errors or zero-FPS fight samples. Median/min **7/5 FPS**; process CPU
1.714 cores, peak RSS 10642.3 MiB, device-wide VRAM 12547 MiB. Menus and standing fight
HUD/fighters render; speckled materials remain. No causal FPS/memory/visual gain claim.

Compared to merged clean cycle16: VS totals **1052 -> 286**; source entries **1028 -> 260**
(unique guest hashes 249 -> 260). UI hash `a193f5b64015ea12` source entries **194 -> 1**.
Workload coverage differs, but actual eliminated viewport duplication and compiler
invariance checks establish the intended engine benefit. No loading-time claim based
on the ~7-second detection-time difference. Broader subsystem assessment and next
gameplay upgrades are in `ENGINE-REVIEW.md`. The background loop was kept paused for
this work; resume only after this handoff is complete, preserving the retained patch.

## Cycle 19 — scheduler empty-retirement fast path — 2026-10-03

### Intent

ENGINE-REVIEW.md priority 2: `CommandScheduler::PopPendingOperations` calls
`MasterSemaphore::Refresh()` (a Vulkan `getSemaphoreCounterValue` driver query)
on every invocation, including per-draw (`renderDraw.cpp` DrawIndex) and
per-dispatch (`renderCompute.cpp`) calls, before checking whether any deferred
retirement work exists. `DeferOperation` is only enqueued on resource-slot
eviction, temporary cleanup, fault unmaps — infrequent next to ~480k draws per
30 s of fighting — so most calls refresh the timeline for an empty queue.
`CommandPool::Commit` and `IsFree` already use cached-check-then-refresh; only
`PopPendingOperations` refreshes unconditionally.

### Patch (retained provisionally)

`src/graphics/host_gpu/renderer/commandScheduler.cpp`: return early under
`m_operation_mutex` when `m_pending_operations` is empty, before `Refresh()`.
When work exists the path is bit-for-bit unchanged (same Refresh, loop, waits,
priority handling, cleanup). Safety: `KnownGpuTick` is monotonic, so skipping
`Refresh` can only defer a retirement to a later call, never retire early; the
only other `KnownGpuTick` readers (`CommandPool::Commit`, `IsFree`, `Wait`)
refresh explicitly on miss. No guest, resolution, validation, quality or
synchronization change. Prior wave32/BDA/viewport work preserved.

`tests/ShaderRecompilerComputeTests.cpp` (`CheckSchedulerTimeline`): after the
implicit-boundary check the queue is empty — pop on the empty queue and require
no state change, then defer/flush/wait/pop one more operation and require it
still retires. Direct regression proof the fast path preserves semantics.

### Result and decision

**Retain provisionally on regression evidence; no performance claim.**
Gameplay comparison is still required before any further engine change.

- Build/install -SkipTests passed, **UFC Dev 042** (`_Build/loop19-build.log`).
- Focused `shader_recompiler_compute|resource_materialization|resource_tracking`:
  **3/3 passed**, 23.30 s (includes the extended SchedulerTimeline test).
- Full CTest: **53/53 passed** twice (47.92 s transcript, 50.68 s saved in
  `_Build/loop19-tests.log`), including `command_scheduler_timeline`,
  `stream_buffer_ring`, `gpu_command_lane` and buffer-cache suites that exercise
  this code. Former `kernel_file_system` baseline passes. `git diff --check`
  clean. Only intended files changed (`commandScheduler.cpp`,
  `ShaderRecompilerComputeTests.cpp`); retained Dev 041 viewport patch untouched.
- CLEAN attempt `_Build/autolab/loop19-scheduler-clean-20261003-184214/`
  (2560x1440/Mailbox, default 54-press MenuDrive, ShaderValidation, Tracy off):
  emulator exited at **409.12 s** with **no fighting** (0 fight samples) and no
  device loss/matched errors. The game never left the boot splash (all 65 shots
  the EA UFC 5 logo with spinner, 60 fps, brightness 45.5, GPU completing all
  ~13M breadcrumb ops); console shows only Http/NP/PlayerInvitationDialog
  unresolved-stub activity before the stall. Non-qualifying run, no FPS evidence.
- RETRY `_Build/autolab/loop19-scheduler-retry-20261003-185044/`: guest fault at
  **58.73 s**, exit 321, no fighting. Fault context is the **known intermittent
  guest null fault**: Job6, 0xc0000005, PC **0x9034b8c02**, access=1/address=0,
  runtimeLinker.cpp:719 — the identical PC/file:line as cycle 17's fault (Job2,
  different host patch, pre-fight phase). No device loss.
- Non-attribution: the patch cannot inject a guest null PC or stall guest boot
  logic — it skips one host timeline query only when there is nothing to
  retire, and every wait/cleanup path is unchanged. Two consecutive failures
  with *different* guest-side signatures (clean-exit boot stall vs known null
  fault at a cross-build identical PC) indicate harness/guest variance, not a
  deterministic patch regression. Per the solitary-fault rule, neither failure
  establishes causation, so no revert is warranted; per the no-evidence rule,
  no FPS/resource/quality improvement is claimed.

Next: run one clean 420-second same-settings gameplay comparison of Dev 042
before any further engine change. If the known guest null fault recurs,
compare an unchanged-code control at the same fault context. Cycle 18's
upload-fallback diagnostic intent remains open but lower priority than that
comparison; structural per-draw SRT/binding reuse stays the fight-FPS target.
Objective unmet (no 60-FPS fight evidence); no goal-reached file.

## Cycle 18 — upload fallback allocation attribution — 2026-10-03

### Intent

Cycle16's merged fight profile measures BDA synchronization at 3.492 s and
14,839 Buffer constructors at 1.381 s, without allocation caller attribution.
Before changing staging strategy, add one coarse engine-only Tracy scope around
UploadCopies' Map-failure fallback. Its nested Buffer constructor identifies
temporary uploads versus other allocation origins. No guest reads, copies,
barriers, memory capacity, validation, quality or rendering work changes.
This is a smaller diagnostic, not a claimed FPS optimization; byte distribution
and Map failure causes remain unmeasured. Preserve prior wave32/BDA work.
Build/install -SkipTests, relevant/full CTest, then 420-second default MenuDrive
ShaderValidation clean run and separate fight-triggered 30-second capture at
2560x1440/Mailbox. Compare actual fighting, not title FPS; reject on regressions.

## Cycle 17 — literal SRT operand fast path — 2026-10-03

### Result and decision

**Rejected** the U32-literal Arg fast path; restored the prior engine and removed
only this cycle's operand regression. Installed **UFC Dev 038** (experiment 037).
No C++/test semantic diff remains (`git diff --` on both files is empty); patching
touched their line endings, so Git status may still mark them modified. Net content
changes this cycle: `HANDOFF.md`, `LOOP-LOG.md`, generated `build-iteration.txt`
(36 -> 38). No tooling/configuration changes, commits, pushes or supervisor edits.
Prior wave32/BDA/integrated changes preserved. No improvement claimed.

- Experiment build/install -SkipTests passed: `_Build/loop17-build.log`.
  Built kyty_tests (`loop17-test-build.log`); focused resource_materialization,
  resource_tracking, shader_recompiler_compute **3/3 passed**, 97.24 s. Added cases
  verified U32 wrapping/zero extension, identity fallback, U64/F32 and failed operands.
- First combined test command exceeded its 240-second outer timeout. Its full CTest
  continued as an orphan; an early repeat overlapped that process and failed guest
  13,824-MB commit reservations (free virtual memory only ~8 GiB). Not accepted as a
  test baseline. After orphan processes exited, a fresh complete run **53/53 passed**,
  41.42 s, `_Build/loop17-tests-final.log`. No game ran during the final tests.
- CLEAN experiment command:
  `powershell -ExecutionPolicy Bypass -File .\AutoFight.ps1 -Label loop17-literal-clean -MonitorSeconds 420 -MenuDrive -ShaderValidation -TracySeconds 0`.
  Session `_Build/autolab/loop17-literal-clean-20261003-171858/`, build 037:
  timeout **421.01 s**, fighting detected **103.33 s**, **317.68 s** observed,
  **282 samples**, max gap **1.94 s**, zero zero-FPS samples. Fight median/min
  **12/10 FPS**, CPU **1.810 cores**, RSS **10852.4 MiB**, device-wide VRAM
  **12811 MiB**, GPU **38%**. No device loss/driver event. Triage has one shutdown
  `SymInitialize FAILED 0xc0000004`; no gameplay validation error was observed.
  Faster than cycle 16's 7/5 but different progression/coverage; not causal evidence.
- Separate PROFILE attempt command:
  `powershell -ExecutionPolicy Bypass -File .\AutoFight.ps1 -Label loop17-literal-profile -MonitorSeconds 420 -MenuDrive -ShaderValidation -TracySeconds 30 -TracyStartSeconds 420`.
  Session `_Build/autolab/loop17-literal-profile-20261003-172621/` exited **63.19 s**
  before fighting, no capture, no fight FPS. Read triage, stderr and console fault
  context (lines 5963-5989); **no correction-request.md was produced**. Job2 guest
  fault: code **0xc0000005**, PC **0x9034b8c02**, access **1**, address **0**;
  runtimeLinker.cpp:719. No device loss. Actual fault cause unknown, not shown to be
  SRT evaluation or Tracy (capture never began). Rejected conservatively for stability
  and absent attributable benefit rather than making an unrelated runtime patch.
- Rollback build/install -SkipTests passed: `_Build/loop17-rollback-build.log`;
  kyty_tests rebuild `loop17-rollback-test-build.log`, full CTest **53/53 passed**,
  **66.12 s**, `loop17-rollback-tests.log` (all build/test logs are PowerShell UTF-16).
  `git diff --check` passed. Former kernel_file_system baseline is also passing.
- Restored CLEAN command:
  `powershell -ExecutionPolicy Bypass -File .\AutoFight.ps1 -Label loop17-restored-clean -MonitorSeconds 420 -MenuDrive -ShaderValidation -TracySeconds 0`.
  Session `_Build/autolab/loop17-restored-clean-20261003-173058/`, build 038:
  timeout **420.37 s**, no matched errors/device loss/driver events. Fight detected
  **103.35 s**, **317.02 s** observed, **289 samples**, max gap **1.90 s**, no zeros.
  Actual fight median/min **16/9 FPS**, CPU **1.905 cores**, RSS **11076 MiB**,
  device-wide VRAM **12888 MiB**, GPU **43%**. Both clean sessions used identical
  **2560x1440/Mailbox**, default short/long schedule, ShaderValidation, Tracy off,
  crash diagnostics off and existing breadcrumbs on. No resolution/quality changes.
- Visual review: experiment and cycle16 `shot_0051.png` show standing fighting;
  restored `shot_0051.png` is grappling/top-down. Fighters, cage/crowd/HUD present,
  speckled skin/canvas/shorts and suspect lighting persist. Poses/cameras/game clocks
  differ; no matched visual improvement or proven visual regression. Higher restored
  median than patched median reinforces rejection, but workloads differ, so it does
  not prove fast-path slowdown. RAM/VRAM peaks likewise cannot establish savings.

Next hypothesis: prefer coarse engine diagnostics attributing **BDA upload fallback
allocations** before another pure-operand micro-optimization. Cycle16 measured
SynchronizeBuffersInRanges **3.492 s / 87.132 us** and **14,839 Buffer constructors /
1.381 s**; caller attribution is missing. Instrument only the UploadCopies staging
Map-failure branch with bounded counters/coarse zones; do not grow staging speculatively,
skip dirty checks or guest reads. Existing cycle16 profile remains the valid merged
hotspot evidence. If Job2 null fault recurs on restored code, investigate its guest
PC/module/context separately, not device-loss diagnostics. Objective is still unmet:
neither 12 nor 16 FPS is stable 60, and no completion file was created.

### Intent

Merged cycle 16 has a qualifying clean fight baseline (7/5 FPS) and a separate
fight-triggered capture. SRT control-flow walk/evaluation costs 2.230 s inclusive,
versus 0.067 s setup. Test one bounded engine change: Arg directly returns literal
U32 operands instead of calling recursive EvaluateWide (Resolve/type dispatch).
All nonliteral operands still use the existing evaluator; no changed guest reads,
generation memoization, masks, validation or rendering/settings. Add operand
regressions, build/install -SkipTests, focused/full CTest, then default MenuDrive
and ShaderValidation clean fighting at 2560x1440/Mailbox for 420 seconds. Obtain a
separate capture to compare SRT costs if stable. Reject on regression or evidence
of performance loss; do not attribute run variance to this small patch.

## Cycle 16 — merged-engine gameplay baseline — 2026-10-03

### Result and decision

Retain baseline evidence only; no C++/shader/tooling/settings patch in this cycle.
Installed **UFC Dev 036**. Changed only `HANDOFF.md`, `LOOP-LOG.md` and the
build-script-generated `build-iteration.txt` (35 -> 36). Wave32 scalar mask branches,
BDA batching and all integrated engine work preserved. No commit/push. Concurrent
external changes appeared in `.opencode/agents/ufc-loop.md` and
`Run-AutonomousLoop.py`; these were not made or edited by this cycle.

- `powershell -ExecutionPolicy Bypass -File .\Build-Windows.ps1 -SkipTests`:
  build/install passed; `_Build/loop16-build.log` (PowerShell UTF-16).
- Built `kyty_tests` after loading VS vcvars64; `_Build/loop16-test-build.log`.
  Focused CTest regex `shader_recompiler_compute|resource_materialization|resource_tracking|srt`:
  **3/3 passed**, 24.98 s (no separate SRT-named test was selected).
  Full `ctest --test-dir _Build/windows --output-on-failure`: **53/53 passed**,
  44.85 s; `_Build/loop16-tests.log` (UTF-16, decoded for verification). The former
  kernel_file_system baseline failure is now passing. No game ran during tests.
  Existing automation tests **11/11 passed**; no harness changes.
- CLEAN command: `powershell -ExecutionPolicy Bypass -File .\AutoFight.ps1 -Label loop16-merged-clean -MonitorSeconds 300 -MenuDrive -ShaderValidation -TracySeconds 0`.
  Session `_Build/autolab/loop16-merged-clean-20261003-165244/`: timeout **300.37 s**,
  no matched errors/device loss/driver events. **2560x1440/Mailbox**, default short/long
  schedule, shader validation, crash diagnostics off. Existing **GPU breadcrumbs on**
  in both sessions; this is a Tracy-free baseline of current defaults, not telemetry-free.
  Fight detected **129.70 s**, **156 FPS samples**, **170.67 s** observed,
  sample span **169.44 s**, maximum gap **1.32 s**, no zero-FPS fight samples.
  Actual fight median/min **7/5 FPS**; CPU median **1.810 busy-core equivalents**,
  peak RSS **10674.6 MiB**, peak **device-wide** VRAM **12512 MiB**, GPU median **35.5%**.
- PROFILE command: `powershell -ExecutionPolicy Bypass -File .\AutoFight.ps1 -Label loop16-merged-profile -MonitorSeconds 300 -MenuDrive -ShaderValidation -TracySeconds 30 -TracyStartSeconds 300`.
  Session `_Build/autolab/loop16-merged-profile-20261003-165752/`: timeout **300.89 s**,
  no matched errors/device loss/driver events. Same settings and schedule. Detection
  was unusually late (**231.73 s**); capture began **232.87 s**, span **30.16 s**,
  **27,018,158 zones**, `fight.tracy` and `tracy-zones.csv` present.
  Only **69.16 s** observed fighting / **63 samples**, gap **1.36 s**: NOT a qualifying
  120-second gameplay measurement. The separate clean run supplies that coverage.
  Profiled fight median/min **7/5 FPS**, RSS **10537.3 MiB**, device VRAM **12531 MiB**,
  CPU **1.905 cores**, GPU **34%**; not clean comparative performance evidence.
- Reviewed clean `shot_0051.png` and profile `shot_0047.png`: standing fighters, cage,
  crowd and HUD render; pronounced skin/canvas speckling and suspect lighting persist.
  Shorts also have noisy patterned surfaces. Different fight clocks/poses/cameras;
  no matched image-quality improvement claim. No correction request generated.

Reproduce hotspot aggregation using the existing reporter:
`py -3.13 TracyZones.py --current _Build/autolab/loop16-merged-profile-20261003-165752/tracy-zones.csv`.

| Merged-engine inclusive zone | Total | Calls | Mean |
| --- | ---: | ---: | ---: |
| CommandProcessor::Process | 27.738 s | 44,041 | 629.828 us |
| PrepareDrawRenderState | 9.447 s | 479,226 | 19.714 us |
| ExecutePreparedDraw | 9.486 s | 477,843 | 19.851 us |
| RefreshShaders | 5.638 s | 479,226 | 11.764 us |
| MaterializeResources (combined) | 5.730 s | 812,131 | 7.055 us |
| SrtRefreshFlatBuffer | 4.254 s | 812,136 | 5.238 us |
| SrtControlFlowWalkEvaluate | 2.230 s | 211,413 | 10.550 us |
| SrtControlFlowSetup | 0.067 s | 211,414 | 0.317 us |
| SynchronizeBuffersInRanges | 3.492 s | 40,075 | 87.132 us |
| Draw::PrepareBda | 3.498 s | 40,075 | 87.279 us |
| Buffer::Buffer | 1.381 s | 14,839 | 93.089 us |

Only seven TranslateProgram calls (95 ms) and seven CompilePermutation calls
(168 ms); compilation churn is not the strongest fight hotspot. Inclusive costs
overlap, include possible waits and cannot establish exclusive CPU/GPU saturation.
Historical captures/settings/workloads differ; no post-rebase performance/resource
gain or regression causation is claimed. Clean median 7 is far from stable 60 FPS.

Next engine hypothesis: resource evaluation and buffer synchronization/allocation,
not SRT control-flow initialization, are actionable host costs. Highest-confidence
bounded target is the SRT walk/evaluation path (33x setup total), preserving every
guest read and generation-scoped memoization; inspect runtime operand dispatch for
redundant pure evaluation work before choosing one optimization. Do not cache across
draws without invalidation proof. Alternate measured target: BDA synchronization's
3.492 s; source `UploadCopies` allocates a temporary Buffer when staging Map fails,
but the 14,839 Buffer constructors have not been attributed to that caller. If pursuing
that hypothesis, first add coarse allocation-origin/fallback-byte diagnostics rather
than increasing staging memory speculatively or skipping clean/dirty checks.
Use >=420 seconds for future profile sessions if late fight detection repeats.
Decision: unchanged engine retained, prerequisite merged baseline established;
no optimization experiment to accept/reject, no goal-reached file.

### Intent

Integrated dev 035 has no comparable post-rebase gameplay profile. Establish that
baseline before modifying an engine hotspot: build/install with -SkipTests, build
kyty_tests, focused compute/resource/SRT tests and full CTest with no game running.
Run default MenuDrive/ShaderValidation at 2560x1440/Mailbox for 300 seconds with
TracySeconds 0, then separately capture 30 seconds around detected fighting in a
300-second session. No GPU crash diagnostics, settings change or engine patch in
this baseline cycle. Preserve wave32 scalar branches and all integrated work.
Inspect fight-only metrics, screenshots and merged-engine inclusive Tracy zones;
select the next narrow engine hypothesis from new evidence, not old source costs.

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
